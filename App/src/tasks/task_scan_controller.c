/*
 * task_scan_controller.c
 *
 *  Created on: Jul 10, 2026
 *      Author: andrey
 *
 *  Задача Scan Controller.
 *  Управляет сканером SE-965HP через USART3 (SSI-протокол).
 *  Получает команды из scan_queue (START/STOP сканирования),
 *  отправляет результат сканирования через dispatcher в CAN.
 */

#include "task_scan_controller.h"
#include "cmsis_os.h"
#include "app_queues.h"
#include "app_config.h"
#include "can_protocol.h"
#include "scan_uart_driver.h"
#include "scan_controller.h"
#include "scan_ssi.h"
#include "task_watchdog.h"
#include <string.h>
#include "app_safety.h"

// --- Учёт операций до очереди, во время обработки и при startup ---
// Начальная единица удерживает BUSY ещё до первого запуска задачи Scanner.
// Dispatcher добавляет единицу до enqueue и снимает её при отказе очереди.
// Задача снимает единицу после всей обработки, включая завершающий UART TX.
static volatile uint32_t scan_pending_commands = 1U;

// --- Резервирование команды Dispatcher до её публикации в очереди ---
void ScanTask_ReserveCommand(void) {
	const uint32_t saved_primask = __get_PRIMASK();
	__disable_irq();
	scan_pending_commands++;
	__set_PRIMASK(saved_primask);
}

// --- Завершение ровно одного ранее зарезервированного владения ---
void ScanTask_ReleaseCommand(void) {
	const uint32_t saved_primask = __get_PRIMASK();
	__disable_irq();
	scan_pending_commands--;
	__set_PRIMASK(saved_primask);
}

// --- Допуск F003 без ожидания завершения доменных операций ---
// После чтения нуля только Dispatcher может добавить новую команду;
// пока он выполняет Commit, новых enqueue не будет. IRQ во Flash разрешены.
bool ScanTask_IsFlashCommitBusy(void) {
	const uint32_t saved_primask = __get_PRIMASK();
	__disable_irq();
	const bool busy = scan_pending_commands != 0U;
	__set_PRIMASK(saved_primask);
	return busy;
}

// --- Подтверждение подготовки; единственный писатель — задача Scanner ---
static volatile bool scan_reset_prepared = false;

// --- Чтение подтверждения задачей Dispatcher ---
bool ScanTask_IsResetPrepared(void) {
	return scan_reset_prepared;
}

/*
 * Проверяет первый ответ нового STOP-handshake; вызывается задачей Scanner.
 * До отпускания CTS драйвер отбросил прежний RX. По SSI §1-4/1-5 ответ
 * на новую команду предшествует отложенному прежнему сообщению.
 * ACK не ищется среди посторонних пакетов: NAK/неверный ответ означает отказ.
 */
static bool Scan_WaitResetStopAck(void) {
	uint8_t packet[6];
	uint16_t used = 0U;

	// --- Один ACK может поступить несколькими RX-фрагментами ---
	while (!AppSafety_IsResetPreparationExpired()) {
		uint16_t length;
		bool error;
		const uint8_t *received = ScanUart_GetReceived(&length, &error);

		if (error)
			return false;

		if (received != NULL) {
			uint16_t count = (uint16_t) (sizeof(packet) - used);
			if (count > length)
				count = length;

			memcpy(&packet[used], received, count);
			used += count;

			// CMD_ACK без payload имеет Length=4, всего 6 байт.
			if (packet[0] != 4U)
				return false;

			if (used == sizeof(packet)) {
				SsiMsg_t message;
				return !AppSafety_IsResetPreparationExpired()
						&& Ssi_Decode(packet, sizeof(packet), &message)
						&& message.opcode == SSI_OP_CMD_ACK
						&& message.msg_src == SSI_MSG_SRC_DECODER
						&& message.data_len == 0U
						&& (message.status & 0x02U) == 0U;
			}
		}

		// --- Прогресс ограниченного ожидания, без нового срока ---
		if (osDelay(1U) != osOK)
			return false;

		AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
	}

	return false;
}

/*
 * Вызывается только задачей Scanner после возврата из обычного UART TX.
 * При F002/F006 всегда выполняет STOP: состояние контроллера MCU само по
 * себе не подтверждает состояние внешнего модуля. Это не STOP после успеха
 * обычной команды чтения; здесь выполняется отдельная подготовка reset.
 */
static bool Scan_SyncResetState(void) {
	// --- Проверка запроса и ранее завершённой подготовки ---
	if (!AppSafety_IsResetPending())
		return false;

	if (scan_reset_prepared)
		return true;

	// --- Только STOP; прежняя операция уже не может стать успешной ---
	uint8_t packet[6];
	const uint8_t length = Ssi_Encode(SSI_OP_STOP_DECODE, NULL, 0U, packet);
	bool stopped = ScanUart_SendStopForReset(packet, length)
			&& Scan_WaitResetStopAck();

	// --- Окончательное закрытие RX, включая отказ подготовки ---
	const bool read_ok = ScanUart_EndRead();
	const bool closed = ScanUart_CancelRx();

	if (!stopped || !read_ok || !closed
			|| AppSafety_IsResetPreparationExpired())
		Error_Handler();

	ScanCtrl_HandleTransportError();

	// --- Очистка уведомления отменённого обмена ---
	uint32_t flags = osThreadFlagsClear(FLAG_SCAN_UART_RX);
	if ((flags & osFlagsError) != 0U
			|| AppSafety_IsResetPreparationExpired())
		Error_Handler();

	// --- Только подтверждённая подготовка разрешает service/Flash ---
	__DMB();
	scan_reset_prepared = true;

	return true;
}

/*
 * Принимает решение о terminal-результате до возможного reset.
 * Решение сохраняется вызывающим кодом; передача CAN выполняется
 * после восстановления IRQ. Доставку ответа этот gate не подтверждает.
 */
static bool Scan_TryAcceptTerminalResult(void) {
	// --- Короткая критическая секция принятия результата ---
	const uint32_t saved_primask = __get_PRIMASK();
	__disable_irq();

	bool accepted = !AppSafety_IsResetPending();

	__set_PRIMASK(saved_primask);

	return accepted;
}

// --- Результат потребления байтов независимо от границ RX-прерываний ---
typedef enum {
	SCAN_RX_MORE,
	SCAN_RX_PACKET,
	SCAN_RX_BAD_LENGTH
} ScanRxStep_t;

// --- Завершённый пакет выбирает действие задачи, а не итог CAN ---
typedef enum {
	SCAN_PACKET_ACK,
	SCAN_PACKET_NAK,
	SCAN_PACKET_BARCODE,
	SCAN_PACKET_REVISION,
	SCAN_PACKET_RESEND,
	SCAN_PACKET_INVALID
} ScanPacketKind_t;

// --- Типизированный итог одного чтения 0601 ---
// Заменяет независимые bool-флаги. Приоритет задаётся явно:
// TRANSPORT > HARDWARE > CHECKSUM_ONLY > TIMEOUT; OK — получен штрихкод.
typedef enum {
	SCAN_READ_TIMEOUT = 0,
	SCAN_READ_CHECKSUM_ONLY,
	SCAN_READ_HARDWARE_ERROR,
	SCAN_READ_TRANSPORT_ERROR,
	SCAN_READ_OK
} ScanReadOutcome_t;

/*
 * Потребляет не более одного полного пакета. Вызывающий сохраняет cursor
 * внутри своего RX-снимка, обрабатывает пакет, очищает packet_len и
 * продолжает с тем же снимком. Это сохраняет пакеты после повреждённого.
 * Length вне поддерживаемого обрамления — ошибка, а не разрешение искать
 * произвольные байты payload в поисках возможного заголовка F3.
 */
static ScanRxStep_t Scan_CollectPacket(const uint8_t *data, uint16_t len,
		uint16_t *cursor, uint8_t *packet, uint16_t *packet_len) {
	// --- Сборка продолжается через границы RX-снимков ---
	while (*cursor < len) {
		if (*packet_len >= SSI_PACKET_MAX_LEN)
			return SCAN_RX_BAD_LENGTH;

		packet[(*packet_len)++] = data[(*cursor)++];

		if (packet[0] < 4U || packet[0] > 4U + SSI_MAX_PAYLOAD)
			return SCAN_RX_BAD_LENGTH;

		if (*packet_len == (uint16_t) packet[0] + 2U)
			return SCAN_RX_PACKET;
	}

	return SCAN_RX_MORE;
}

/*
 * Вызывается только для полного пакета поддерживаемой длины из сборщика.
 * Ошибка checksum запрашивает SSI RESEND; это не решение CAN NACK.
 * Валидные ACK/NAK отдаются владельцу обмена без ответа на них.
 * Сегментированные или недопустимые ожидаемые данные не являются
 * успешным одиночным штрихкодом.
 */
static ScanPacketKind_t Scan_ClassifyPacket(const uint8_t *packet,
		uint16_t packet_len, SsiMsg_t *message) {
	// --- Обрамление проверяет сборщик; остаточная ошибка декодирования — checksum ---
	if (!Ssi_Decode(packet, packet_len, message))
		return SCAN_PACKET_RESEND;

	// --- Отправитель входящих пакетов — модуль сканера ---
	if (message->msg_src != SSI_MSG_SRC_DECODER)
		return SCAN_PACKET_INVALID;

	// --- Фрагменты ревизии вне активной операции со штрихкодом ---
	// Их отбрасывание не требует сборки текста ревизии.
	if (message->opcode == SSI_OP_REPLY_REVISION)
		return (message->status & (uint8_t) ~0x03U) == 0U ?
				SCAN_PACKET_REVISION : SCAN_PACKET_INVALID;

	// --- Одиночные ACK, NAK и штрихкод используют только бит retransmit ---
	// Сборка многопакетного штрихкода этим фрагментом не заявляется.
	if ((message->status & (uint8_t) ~0x01U) != 0U)
		return SCAN_PACKET_INVALID;

	switch (message->opcode) {
	case SSI_OP_CMD_ACK:
		return message->data_len == 0U ?
				SCAN_PACKET_ACK : SCAN_PACKET_INVALID;
	case SSI_OP_CMD_NAK:
		return message->data_len == 1U ?
				SCAN_PACKET_NAK : SCAN_PACKET_INVALID;
	case SSI_OP_DECODE_DATA:
		// Тип плюс хотя бы один байт данных штрихкода.
		return message->data_len >= 2U ?
				SCAN_PACKET_BARCODE : SCAN_PACKET_INVALID;
	default:
		break;
	}

	return SCAN_PACKET_INVALID;
}

// --- SSI host response limit: project policy matches the existing 2 s RTS bound ---
// This is a software limit, not readback of the module's current parameter.
#define SCAN_SSI_RESPONSE_TIMEOUT_MS 2000U
#define SCAN_SSI_PREP_BUDGET_MS 6000U

// --- Окно и период опроса capture 0603/0604 ---
// Окно 3000 мс покрывает штатный SSI-ответ 2000 мс с запасом на передачу
// и обработку. Период 50 мс (20 Гц) ограничивает задержку реакции на reset
// и публикует heartbeat заметно чаще idle-таймаута watchdog 500 мс.
// Это локальные параметры задачи Scanner, а не общий контракт экосистемы.
#define SCAN_CAPTURE_WINDOW_MS 3000U
#define SCAN_CAPTURE_POLL_MS   50U

// --- At most three transmissions of the same explicitly rejected host packet ---
// Host retry limit is a proposed Scanner adaptation. The SSI guide separately
// specifies two retries for decoder data and requires the host to limit retries.
#define SCAN_SSI_HOST_RESENDS 2U

/*
 * Receives only the first answer of a freshly established control handshake.
 * STOP/FLUSH expect ACK, not a barcode. Old postponed data after this answer
 * is discarded by EndRead; the following control handshake opens a new boundary.
 * Valid NAK requests no ACK/NAK response. RESEND asks to retransmit the same
 * command with the status bit; it does not silently authorize a new operation.
 */
static uint32_t Scan_MsToTicks(uint32_t ms) {
    return (uint32_t)(((uint64_t)ms * osKernelGetTickFreq()) / 1000U);
}

static uint32_t Scan_DeadlineRemaining(TickType_t deadline) {
    const int32_t diff = (int32_t)(deadline - osKernelGetTickCount());
    return diff > 0 ? (uint32_t)diff : 0U;
}

static uint32_t Scan_DeadlineRemainingMs(TickType_t deadline)
{
    uint32_t remaining = Scan_DeadlineRemaining(deadline);
    uint32_t frequency = osKernelGetTickFreq();
    if (frequency == 0U)
        return 0U;
    return (uint32_t)(((uint64_t)remaining * 1000U) / frequency);
}

static uint32_t Scan_DeadlineToHalMs(TickType_t deadline)
{
    return HAL_GetTick() + Scan_DeadlineRemainingMs(deadline);
}

static uint16_t Scan_WaitControlReply(bool *resend, TickType_t deadline) {
    uint8_t packet[7];
    uint16_t used = 0U;
    const TickType_t started = osKernelGetTickCount();
    TickType_t reply_deadline = started
            + (TickType_t)Scan_MsToTicks(SCAN_SSI_RESPONSE_TIMEOUT_MS);
    if ((int32_t)(reply_deadline - deadline) > 0)
        reply_deadline = deadline;
    *resend = false;

    // --- One bounded reply; reset is finalized by the caller's domain path ---
    while (!AppSafety_IsResetPending()
            && (int32_t)(reply_deadline - osKernelGetTickCount()) > 0) {
        uint16_t length;
        bool error;
        const uint8_t *received = ScanUart_GetReceived(&length, &error);
        if (error)
            return CAN_ERR_SCAN_COMM;

        for (uint16_t i = 0U; received != NULL && i < length; ++i) {
            packet[used++] = received[i];
            if (packet[0] != 4U && packet[0] != 5U)
                return CAN_ERR_SCAN_COMM;
            if (used < (uint16_t) packet[0] + 2U)
                continue;

            SsiMsg_t message;
            if ((int32_t)(reply_deadline - osKernelGetTickCount()) <= 0
                    || !Ssi_Decode(packet, used, &message)
                    || message.msg_src != SSI_MSG_SRC_DECODER
                    || (message.status & (uint8_t) ~SSI_STATUS_RETRANSMIT) != 0U)
                return CAN_ERR_SCAN_COMM;

            if (message.opcode == SSI_OP_CMD_ACK && message.data_len == 0U)
                return CAN_ERR_NONE;

            if (message.opcode == SSI_OP_CMD_NAK && message.data_len == 1U) {
                *resend = message.data[0] == SSI_NAK_RESEND;
                return *resend ? CAN_ERR_SCAN_COMM : CAN_ERR_SCAN_HARDWARE;
            }

            return CAN_ERR_SCAN_COMM;
        }

        if (osDelay(1U) != osOK)
            return CAN_ERR_SCAN_COMM;
        AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
    }
    return CAN_ERR_SCAN_COMM;
}

/*
 * One idempotent preparation command, including RX ownership and response.
 * Only an explicit decoder RESEND permits retransmission. Timeout is not
 * treated as proof that the command was rejected. A failed boundary forbids START.
 */
static uint16_t Scan_RunPreparationCommand(uint8_t opcode, TickType_t deadline) {
    const uint32_t response_ticks =
            Scan_MsToTicks(SCAN_SSI_RESPONSE_TIMEOUT_MS);

    for (uint32_t attempt = 0U; attempt <= SCAN_SSI_HOST_RESENDS; ++attempt) {
        if (Scan_DeadlineRemaining(deadline) == 0U)
            return CAN_ERR_SCAN_COMM;
        if (attempt > 0U && Scan_DeadlineRemaining(deadline) < response_ticks)
            return CAN_ERR_SCAN_COMM;

        if (!ScanUart_BeginRead())
            return CAN_ERR_SCAN_COMM;

        uint8_t packet[6];
        uint8_t length = Ssi_EncodeWithStatus(opcode,
                attempt == 0U ? 0U : SSI_STATUS_RETRANSMIT, NULL, 0U, packet);
        if (!ScanUart_Wake() || !ScanUart_SendForReplyDeadline(packet, length, Scan_DeadlineToHalMs(deadline))) {
            (void) ScanUart_EndRead();
            return CAN_ERR_SCAN_COMM;
        }

        bool resend;
        uint16_t result = Scan_WaitControlReply(&resend, deadline);
        if (!ScanUart_EndRead())
            return CAN_ERR_SCAN_COMM;
        if (AppSafety_IsResetPending())
            return CAN_ERR_SCAN_COMM;
        if (!resend)
            return result;
    }
    return CAN_ERR_SCAN_COMM;
}

/*
 * Defines a module-side boundary for every new barcode reading.
 * A STOP ACK confirms the old decode session stopped; FLUSH ACK confirms its
 * queued packets were eliminated. Neither MCU buffer reset nor START ACK alone
 * establishes this boundary. Physical support remains subject to BENCH.
 */
static uint16_t Scan_PrepareRead(TickType_t deadline) {
    uint16_t result = Scan_RunPreparationCommand(SSI_OP_STOP_DECODE, deadline);
    if (result != CAN_ERR_NONE)
        return result;
    return Scan_RunPreparationCommand(SSI_OP_FLUSH_QUEUE, deadline);
}

// --- Read-only revision probe or single BEEP: DONE means RX capture complete ---
static void ScanQueryRevision(uint16_t command) {

	// --- Отмена до начала запроса ---
	if (Scan_SyncResetState()) {
		CAN_SendNackPublic(command, CAN_ERR_DEVICE_BUSY);
		return;
	}

	static uint8_t packet[SSI_PACKET_MAX_LEN];
	const uint8_t beep_code = 0;
	uint8_t length =
			command == CAN_CMD_SCAN_BEEP ?
					Ssi_Encode(SSI_OP_BEEP, &beep_code, 1, packet) :
					Ssi_Encode(SSI_OP_REQUEST_REVISION, NULL, 0, packet);
	ScanUartCapture_t rx;
	ScanUart_BeginCapture();

	if (!ScanUart_Wake() || !ScanUart_Send(packet, length)) {
		ScanUart_EndCapture(NULL);

		// --- Отмена reset отличается от неисправности UART ---
		if (Scan_SyncResetState()) {
			CAN_SendNackPublic(command, CAN_ERR_DEVICE_BUSY);
		} else {
			CAN_SendNackPublic(command, CAN_ERR_SCAN_HARDWARE);
		}
		return;
	}

	// --- Окно capture 0603/0604 и период опроса ---
	// Владелец: task_scan_controller. Окно задаёт, сколько задача ждёт сырой
	// ответ на REQUEST_REVISION/BEEP; оно не меняет SSI-таймаут модуля и не
	// перезапускается. Период задаёт частоту проверки reset и heartbeat.
	uint32_t tick_freq = osKernelGetTickFreq();
	uint32_t started = osKernelGetTickCount();

	// +999 — округление вверх до целого тика: окно и период не короче заданных.
	uint32_t duration =
			(SCAN_CAPTURE_WINDOW_MS * tick_freq + 999U) / 1000U;
	uint32_t poll_ticks =
			(SCAN_CAPTURE_POLL_MS * tick_freq + 999U) / 1000U;

	// --- Ожидание capture с проверкой отмены ---
	while ((uint32_t) (osKernelGetTickCount() - started) < duration) {
		if (Scan_SyncResetState()) {
			CAN_SendNackPublic(command, CAN_ERR_DEVICE_BUSY);
			return;
		}

		AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);

		// --- Ошибка RTOS-ожидания не является прогрессом capture ---
		// При отказе osDelay задача не может гарантировать периодичность
		// heartbeat и реакцию на reset; по политике §2.7.1 — терминальный
		// Error_Handler, а не продолжение capture.
		if (osDelay(poll_ticks) != osOK)
			Error_Handler();
	}

	// --- Проверка отмены после последнего ожидания ---
	if (Scan_SyncResetState()) {
		CAN_SendNackPublic(command, CAN_ERR_DEVICE_BUSY);
		return;
	}

	ScanUart_EndCapture(&rx);

	// v1 envelope: schema8, flags8 (bit0 overflow), length16, errors32, raw.
	// Little-endian metadata; padding to six bytes is zero and checked by host.
	uint8_t response[204] = { 1U, 0U };
	response[1] = rx.overflow ? 1U : 0U;
	response[2] = (uint8_t) rx.length;
	response[3] = (uint8_t) (rx.length >> 8);
	for (unsigned i = 0; i < 4; ++i)
		response[4 + i] = (uint8_t) (rx.errors >> (8U * i));
	memcpy(response + 8, rx.bytes, rx.length);
	uint16_t total = (uint16_t) (((8U + rx.length + 5U) / 6U) * 6U);

	// --- Принятие результата capture либо завершение отмены ---
	if (!Scan_TryAcceptTerminalResult()) {
		(void) Scan_SyncResetState();
		CAN_SendNackPublic(command, CAN_ERR_DEVICE_BUSY);
		return;
	}
	if (!CAN_SendDiagnosticResponse(command, response, total))
		CAN_SendNackPublic(command, CAN_ERR_DEVICE_BUSY);
}

void app_start_task_scan_controller(void *argument) {
	(void) argument;

	uint8_t ssi_packet[SSI_PACKET_MAX_LEN];
	uint16_t ssi_len;

	ScanCtrl_Init();
	ScanUart_Init();

	// --- SCAN_ENABLE: однократно при старте ---
	ssi_len = Ssi_Encode(SSI_OP_SCAN_ENABLE, NULL, 0, ssi_packet);
	if (ScanUart_Wake())
		ScanUart_Send(ssi_packet, ssi_len);

	ScanResult_t cmd;

	// --- Интервал ожидания команды в тиках RTOS ---
	const uint32_t idle_wait_ticks = (APP_WATCHDOG_TASK_IDLE_TIMEOUT_MS
			* osKernelGetTickFreq()) / 1000U;

	// --- Первое владение соответствует завершённой startup-последовательности ---
	bool release_pending = true;

	for (;;) {
		// --- Единая точка завершения startup либо предыдущей команды ---
		// Все continue внешнего цикла проходят здесь; внутреннее ожидание RX
		// не снимает владение. Ошибка/timeout пустой очереди не создают владение.
		if (release_pending) {
			ScanTask_ReleaseCommand();
			release_pending = false;
		}

		// --- Подготовка выполняется и при отсутствии команд ---
		(void) Scan_SyncResetState();

		osStatus_t queue_status;

		// --- Ожидание команды от Dispatcher ---
		queue_status = osMessageQueueGet(scan_queueHandle, &cmd,
		NULL, idle_wait_ticks);

		// Пустая очередь — штатный режим, задача исполняется.
		if (queue_status == osErrorTimeout) {
			AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
			continue;
		}

		// Ошибка очереди не подтверждает прогресс.
		// Содержимое cmd при ошибке не обрабатываем.
		if (queue_status != osOK) {
			continue;
		}

		// --- Принятие владения, уже зарезервированного Dispatcher ---
		// Dequeue не уменьшает счётчик: он остаётся занят до конца обработки.
		release_pending = true;

		// --- Команда получена и передаётся на обработку ---
		AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);

		// --- Команда из очереди не запускается после reset-latch ---
		if (Scan_SyncResetState()) {
			CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_DEVICE_BUSY);
			continue;
		}

		if (cmd.cmd_code == CAN_CMD_SCAN_GET_REVISION
				|| cmd.cmd_code == CAN_CMD_SCAN_BEEP) {
			ScanQueryRevision(cmd.cmd_code);
			continue;
		}

		if (cmd.cmd_code == CAN_CMD_SCAN_GET_DATA) {
			// --- Подготовка результата из сохранённого штрихкода ---
			uint8_t barcode[SHADOW_BUF_SIZE];
			uint8_t barcode_len = 0;
			bool has_barcode = ScanCtrl_GetLastBarcode(barcode, &barcode_len);

			// --- Принятие результата либо завершение отмены ---
			if (!Scan_TryAcceptTerminalResult()) {
				(void) Scan_SyncResetState();
				CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_DEVICE_BUSY);
				continue;
			}

			// --- Передача принятого результата вне критической секции ---
			if (has_barcode) {
				CAN_SendDataFragmented(cmd.cmd_code, barcode, barcode_len);
				CAN_SendDone(cmd.cmd_code, 0);
			} else {
				CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_NO_DATA);
			}
			continue;
		}

		if (cmd.cmd_code != CAN_CMD_SCAN_REQUEST) {
			CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_UNKNOWN_CMD);
			continue;
		}

		// --- Запуск состояния текущего запроса ---
		if (!ScanCtrl_StartScan(cmd.timeout_sec)) {
			CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_DEVICE_BUSY);
			continue;
		}

		// --- Confirmed module-side boundary before a new reading ---
		const TickType_t prep_deadline = osKernelGetTickCount()
				+ (TickType_t) Scan_MsToTicks(SCAN_SSI_PREP_BUDGET_MS);
		uint16_t prepare_error = Scan_PrepareRead(prep_deadline);
		if (prepare_error != CAN_ERR_NONE) {
			if (Scan_SyncResetState() || !Scan_TryAcceptTerminalResult()) {
				(void) Scan_SyncResetState();
				CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_DEVICE_BUSY);
			} else {
				ScanCtrl_HandleTransportError();
				CAN_SendNackPublic(cmd.cmd_code, prepare_error);
			}
			continue;
		}

		// --- Открытие накопления ответа перед отправкой START ---
		// Открываем накопление только после принятия запроса контроллером.
		if (!ScanUart_BeginRead()) {
			if (Scan_SyncResetState()) {
				CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_DEVICE_BUSY);
			} else {
				ScanCtrl_HandleTransportError();
				CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_COMM);
			}
			continue;
		}

		if (Scan_DeadlineRemaining(prep_deadline) == 0U) {
			(void) ScanUart_EndRead();
			ScanCtrl_HandleTransportError();
			CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_COMM);
			continue;
		}

		// --- Отправка START_DECODE ---
		ssi_len = Ssi_Encode(SSI_OP_START_DECODE, NULL, 0, ssi_packet);
		// Retain this attempt separately from the subsequent STOP_DECODE.
		if (!ScanUart_Wake() || !ScanUart_SendStartDeadline(ssi_packet, ssi_len, Scan_DeadlineToHalMs(prep_deadline))) {
			// --- Закрытие накопления при несостоявшемся запуске ---
			(void) ScanUart_EndRead();

			// --- Reset отличается от ошибки передачи START ---
			if (Scan_SyncResetState()) {
				CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_DEVICE_BUSY);
			} else {
				ScanCtrl_HandleTransportError();
				CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_HARDWARE);
			}
			continue;
		}

		// Ожидание UART RX с таймаутом
		// --- Общий срок ожидания ответа Scanner ---
		// Считаем фактически прошедшие тики, включая время выполнения задачи.
		uint32_t timeout_ms =
				cmd.timeout_sec > 0U ?
						(uint32_t) cmd.timeout_sec * 1000U : 4000U;
		uint32_t tick_freq = osKernelGetTickFreq();
		uint32_t read_timeout_ticks = (uint32_t) (((uint64_t) timeout_ms * tick_freq)
				/ 1000U);
		uint32_t slice_ticks = (500U * tick_freq) / 1000U;
		TickType_t read_deadline = 0U;
		bool read_started = false;
		const TickType_t txn_deadline = prep_deadline
				+ (TickType_t) read_timeout_ticks;

		ScanReadOutcome_t outcome = SCAN_READ_TIMEOUT;
		bool start_acked = false;
		uint32_t start_resends = 0U;

		// --- Накопитель одного SSI-пакета принадлежит задаче Scanner ---
		static uint8_t rx_packet[SSI_PACKET_MAX_LEN];
		uint16_t rx_len = 0U;
		const uint8_t *rx_data = NULL;

		// --- Восстановление SSI в пределах исходного срока запроса ---
		// Сначала забираем накопленные байты, затем при необходимости ждём.
		// Число уведомлений не определяет количество доступных данных.
		for (;;) {
			// --- Отмена и общий срок ---
			if (AppSafety_IsResetPending())
				break;

			const TickType_t now = osKernelGetTickCount();
			const TickType_t deadline = read_started ? read_deadline : prep_deadline;

			if ((int32_t)(deadline - now) <= 0) {
				if (!read_started)
					outcome = SCAN_READ_TRANSPORT_ERROR;
				break;
			}

			uint16_t received_len;
			bool receive_error;
			const uint8_t *received = ScanUart_GetReceived(&received_len,
					&receive_error);

			if (receive_error) {
				outcome = SCAN_READ_TRANSPORT_ERROR;
				break;
			}

			if (received != NULL) {
				// --- Пакеты внутри одного снимка обрабатываются по курсору ---
				uint16_t cursor = 0U;

				while (cursor < received_len) {
					const TickType_t inner_deadline =
							read_started ? read_deadline : prep_deadline;
					if (AppSafety_IsResetPending()
							|| (int32_t)(inner_deadline
									- osKernelGetTickCount()) <= 0)
						break;

					ScanRxStep_t step = Scan_CollectPacket(received,
							received_len, &cursor, rx_packet, &rx_len);

					if (step == SCAN_RX_BAD_LENGTH) {
						outcome = SCAN_READ_TRANSPORT_ERROR;
						break;
					}

					if (step == SCAN_RX_MORE)
						break;

					SsiMsg_t message;
					ScanPacketKind_t kind = Scan_ClassifyPacket(rx_packet,
							rx_len, &message);

					if (kind == SCAN_PACKET_BARCODE) {
						rx_data = rx_packet;
						outcome = SCAN_READ_OK;
						break;
					}

					// --- Каждый обработанный не-штрихкод освобождает сборщик ---
					rx_len = 0U;

					switch (kind) {
					case SCAN_PACKET_ACK:
						// ACK на START не завершает ожидание штрихкода.
						// С этого момента действует срок чтения, не подготовки.
						start_acked = true;
						read_started = true;
						read_deadline = osKernelGetTickCount()
								+ (TickType_t) read_timeout_ticks;
						if ((int32_t)(read_deadline - txn_deadline) > 0)
							read_deadline = txn_deadline;
						break;
					case SCAN_PACKET_REVISION:
						// Посторонний ответ на другую команду — не результат чтения.
						break;
					case SCAN_PACKET_RESEND: {
						// --- Полный пакет с ошибкой checksum: запрос повтора ---
						outcome = SCAN_READ_CHECKSUM_ONLY;
						const uint8_t cause = SSI_NAK_RESEND;

						ssi_len = Ssi_Encode(SSI_OP_CMD_NAK, &cause, 1U,
								ssi_packet);

						if (!ScanUart_Wake()
								|| !ScanUart_SendForReplyDeadline(ssi_packet, ssi_len, Scan_DeadlineToHalMs(read_started ? read_deadline : prep_deadline))) {
							outcome = SCAN_READ_TRANSPORT_ERROR;
							break;
						}
						break;
					}
					case SCAN_PACKET_NAK: {
						if (message.data[0] == SSI_NAK_RESEND) {
							// Повтор только для START, отклонённого до ACK:
							// поздний NAK не должен перезапускать принятое декодирование.
							if (start_acked
									|| start_resends >= SCAN_SSI_HOST_RESENDS
									|| Scan_DeadlineRemaining(
											read_started ? read_deadline : prep_deadline)
											< Scan_MsToTicks(
													SCAN_SSI_RESPONSE_TIMEOUT_MS)) {
								outcome = SCAN_READ_TRANSPORT_ERROR;
								break;
							}
							++start_resends;
							ssi_len = Ssi_EncodeWithStatus(SSI_OP_START_DECODE,
									SSI_STATUS_RETRANSMIT, NULL, 0U,
									ssi_packet);

							if (!ScanUart_Wake()
									|| !ScanUart_SendStartDeadline(ssi_packet, ssi_len, Scan_DeadlineToHalMs(read_started ? read_deadline : prep_deadline))) {
								outcome = SCAN_READ_TRANSPORT_ERROR;
								break;
							}
						} else {
							outcome = SCAN_READ_HARDWARE_ERROR;
							break;
						}
						break;
					}
					default:
						outcome = SCAN_READ_HARDWARE_ERROR;
						break;
					}

					if (outcome == SCAN_READ_HARDWARE_ERROR
							|| outcome == SCAN_READ_TRANSPORT_ERROR)
						break;

					AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
				}

				if (outcome == SCAN_READ_OK
						|| outcome == SCAN_READ_HARDWARE_ERROR
						|| outcome == SCAN_READ_TRANSPORT_ERROR)
					break;

				AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
				continue;
			}

			// --- Ожидание следующих данных без перезапуска срока ---
			const TickType_t wait_deadline =
					read_started ? read_deadline : prep_deadline;
			const uint32_t remaining = Scan_DeadlineRemaining(wait_deadline);

			if (remaining == 0U) {
				if (!read_started)
					outcome = SCAN_READ_TRANSPORT_ERROR;
				break;
			}
			uint32_t wait_ticks =
					remaining < slice_ticks ? remaining : slice_ticks;

			uint32_t flags = osThreadFlagsWait(FLAG_SCAN_UART_RX,
					osFlagsWaitAny, wait_ticks);

			if (flags == osFlagsErrorTimeout) {
				AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
				continue;
			}

			if ((flags & osFlagsError) != 0U) {
				outcome = SCAN_READ_TRANSPORT_ERROR;
				break;
			}

			AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
		}

		// --- Подтверждение штрихкода сканеру до принятия результата и отправки CAN ---
		if (outcome == SCAN_READ_OK) {
			ssi_len = Ssi_Encode(SSI_OP_CMD_ACK, NULL, 0, ssi_packet);
			if (!ScanUart_Wake() || !ScanUart_Send(ssi_packet, ssi_len))
				outcome = SCAN_READ_TRANSPORT_ERROR;
		}

		// --- Закрытие накопления и итоговая проверка RX ---
		// EndRead атомарно сохраняет итог ошибки и закрывает накопление.
		// Данные результата уже находятся в rx_packet задачи.
		if (!ScanUart_EndRead())
			outcome = SCAN_READ_TRANSPORT_ERROR;

		// --- Reset имеет приоритет над RX, timeout и ошибкой ожидания ---
		if (Scan_SyncResetState()) {
			CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_DEVICE_BUSY);
			continue;
		}

		// --- Принятие terminal-результата текущего сканирования ---
		// Gate предшествует изменению статистики и сохранению штрихкода.
		if (!Scan_TryAcceptTerminalResult()) {
			(void) Scan_SyncResetState();
			CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_DEVICE_BUSY);
			continue;
		}

		// --- Типизированный итог чтения и terminal-ответ ---
		switch (outcome) {
		case SCAN_READ_OK:
			if (rx_data != NULL
					&& ScanCtrl_ProcessReceived(rx_data, rx_len)) {
				uint8_t buf[SHADOW_BUF_SIZE];
				uint8_t buf_len = 0;

				ScanCtrl_GetLastBarcode(buf, &buf_len);
				CAN_SendDataFragmented(cmd.cmd_code, buf, buf_len);
				CAN_SendDone(cmd.cmd_code, 0);
			} else {
				// --- Терминальный отказ также завершает операцию контроллера ---
				ScanCtrl_HandleTransportError();
				CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_HARDWARE);
			}
			break;
		case SCAN_READ_TRANSPORT_ERROR:
		case SCAN_READ_CHECKSUM_ONLY:
			ScanCtrl_HandleTransportError();
			CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_COMM);
			break;
		case SCAN_READ_HARDWARE_ERROR:
			ScanCtrl_HandleTransportError();
			CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_HARDWARE);
			break;
		case SCAN_READ_TIMEOUT:
		default:
			ScanCtrl_HandleTimeout();
			CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_TIMEOUT);
			break;
		}

		// --- Прекращение попытки после неуспешного чтения ---
		// После успешного чтения дополнительный STOP не отправляем.
		// Проверка исхода STOP будет частью отдельного блока отмены.
		if (ScanCtrl_GetState() != SCAN_STATE_DATA_READY) {
			ssi_len = Ssi_Encode(SSI_OP_STOP_DECODE, NULL, 0, ssi_packet);

			if (ScanUart_Wake())
				(void) ScanUart_Send(ssi_packet, ssi_len);
		}
	}
}
