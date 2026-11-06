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

// --- Result of consuming bytes, independent of UART interrupt boundaries ---
typedef enum {
    SCAN_RX_MORE,
    SCAN_RX_PACKET,
    SCAN_RX_BAD_LENGTH
} ScanRxStep_t;

// --- A completed packet selects a task action, not a CAN terminal result ---
typedef enum {
    SCAN_PACKET_ACK,
    SCAN_PACKET_NAK,
    SCAN_PACKET_BARCODE,
    SCAN_PACKET_REVISION,
    SCAN_PACKET_RESEND,
    SCAN_PACKET_INVALID
} ScanPacketKind_t;

/*
 * Consumes at most one complete packet. The caller retains cursor within
 * its RX snapshot, handles the packet, clears packet_len and resumes with
 * the same snapshot. This preserves packets following a damaged packet.
 * Length outside supported framing is an error, not permission to search
 * arbitrary payload bytes for a possible F3 header.
 */
static ScanRxStep_t Scan_CollectPacket(const uint8_t *data, uint16_t len,
        uint16_t *cursor, uint8_t *packet, uint16_t *packet_len) {
    // --- Assembly continues across RX snapshots ---
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
 * Called only for a complete supported-length packet from the assembler.
 * CRC failure requests SSI RESEND; it is not a CAN NACK decision.
 * Valid ACK/NAK are exposed to the exchange owner without generating a reply.
 * Segmented or invalid expected data is not a successful single barcode.
 */
static ScanPacketKind_t Scan_ClassifyPacket(const uint8_t *packet,
        uint16_t packet_len, SsiMsg_t *message) {
    // --- Framing belongs to the assembler; a remaining decode failure is checksum ---
    if (!Ssi_Decode(packet, packet_len, message))
        return SCAN_PACKET_RESEND;

    // --- The module is the sender of incoming packets ---
    if (message->msg_src != SSI_MSG_SRC_DECODER)
        return SCAN_PACKET_INVALID;

    // --- Revision fragments are outside the active barcode operation ---
    // Dropping them does not require assembling their revision text.
    if (message->opcode == SSI_OP_REPLY_REVISION)
        return (message->status & (uint8_t) ~0x03U) == 0U ?
                SCAN_PACKET_REVISION : SCAN_PACKET_INVALID;

    // --- Single-packet ACK, NAK and barcode use only the retransmit status bit ---
    // Multipacket barcode assembly is not claimed by this candidate fragment.
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
        // Type plus at least one byte of barcode data.
        return message->data_len >= 2U ?
                SCAN_PACKET_BARCODE : SCAN_PACKET_INVALID;

    default:
        return SCAN_PACKET_INVALID;
    }
}

// --- SSI host response limit: project policy matches the existing 2 s RTS bound ---
// This is a software limit, not readback of the module's current parameter.
#define SCAN_SSI_RESPONSE_TIMEOUT_MS 2000U

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
static uint16_t Scan_WaitControlReply(bool *resend) {
    uint8_t packet[7];
    uint16_t used = 0U;
    const uint32_t started = osKernelGetTickCount();
    const uint32_t duration = (uint32_t) (((uint64_t)
            SCAN_SSI_RESPONSE_TIMEOUT_MS * osKernelGetTickFreq()) / 1000U);
    *resend = false;

    // --- One bounded reply; reset is finalized by the caller's domain path ---
    while (!AppSafety_IsResetPending()
            && (uint32_t) (osKernelGetTickCount() - started) < duration) {
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
            if ((uint32_t) (osKernelGetTickCount() - started) >= duration
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
static uint16_t Scan_RunPreparationCommand(uint8_t opcode) {
    for (uint32_t attempt = 0U; attempt <= SCAN_SSI_HOST_RESENDS; ++attempt) {
        if (!ScanUart_BeginRead())
            return CAN_ERR_SCAN_COMM;

        uint8_t packet[6];
        uint8_t length = Ssi_EncodeWithStatus(opcode,
                attempt == 0U ? 0U : SSI_STATUS_RETRANSMIT, NULL, 0U, packet);
        if (!ScanUart_Wake() || !ScanUart_SendForReply(packet, length)) {
            (void) ScanUart_EndRead();
            return CAN_ERR_SCAN_COMM;
        }

        bool resend;
        uint16_t result = Scan_WaitControlReply(&resend);
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
static uint16_t Scan_PrepareRead(void) {
    uint16_t result = Scan_RunPreparationCommand(SSI_OP_STOP_DECODE);
    if (result != CAN_ERR_NONE)
        return result;
    return Scan_RunPreparationCommand(SSI_OP_FLUSH_QUEUE);
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

	// Capture retains raw RX, including malformed and fragmented responses.
	// Fixed 3 s covers the default 2 s SSI response timeout with margin.
	uint32_t started = osKernelGetTickCount();
	uint32_t duration = 3U * osKernelGetTickFreq();
	// --- Ожидание capture с проверкой отмены ---
	while ((uint32_t) (osKernelGetTickCount() - started) < duration) {
		if (Scan_SyncResetState()) {
			CAN_SendNackPublic(command, CAN_ERR_DEVICE_BUSY);
			return;
		}

		AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
		osDelay((osKernelGetTickFreq() + 19U) / 20U);
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
		uint16_t prepare_error = Scan_PrepareRead();
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

		// --- Отправка START_DECODE ---
		ssi_len = Ssi_Encode(SSI_OP_START_DECODE, NULL, 0, ssi_packet);
		// Retain this attempt separately from the subsequent STOP_DECODE.
		if (!ScanUart_Wake() || !ScanUart_SendStart(ssi_packet, ssi_len)) {
			// --- Закрытие накопления при несостоявшемся запуске ---
			(void) ScanUart_EndRead();

			// --- Reset отличается от ошибки передачи START ---
			if (Scan_SyncResetState()) {
				CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_DEVICE_BUSY);
			} else {
				ScanCtrl_HandleTransportError();
				CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_COMM);
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
		uint32_t timeout_ticks = (uint32_t) (((uint64_t) timeout_ms * tick_freq)
				/ 1000U);
		uint32_t slice_ticks = (500U * tick_freq) / 1000U;
		uint32_t started = osKernelGetTickCount();

        bool rx_received = false;
        bool wait_error = false;
        bool hardware_error = false;
        bool checksum_seen = false;
        bool start_acked = false;
        uint32_t start_resends = 0U;

        // --- Packet storage and snapshot cursor belong to the Scanner task ---
        static uint8_t rx_packet[SSI_PACKET_MAX_LEN];
        uint16_t rx_len = 0U;
        const uint8_t *rx_data = NULL;

        // --- Recover SSI within the original reading deadline ---
        for (;;) {
            if (AppSafety_IsResetPending())
                break;
            uint32_t elapsed = osKernelGetTickCount() - started;
            if (elapsed >= timeout_ticks)
                break;

            uint16_t received_len;
            bool receive_error;
            const uint8_t *received = ScanUart_GetReceived(&received_len, &receive_error);
            if (receive_error) {
                wait_error = true;
                break;
            }

            if (received != NULL) {
                uint16_t cursor = 0U;
                while (cursor < received_len) {
                    if (AppSafety_IsResetPending()
                            || (uint32_t) (osKernelGetTickCount() - started) >= timeout_ticks)
                        break;

                    ScanRxStep_t step = Scan_CollectPacket(received, received_len,
                            &cursor, rx_packet, &rx_len);
                    if (step == SCAN_RX_BAD_LENGTH) {
                        wait_error = true;
                        break;
                    }
                    if (step == SCAN_RX_MORE)
                        break;

                    SsiMsg_t message;
                    ScanPacketKind_t kind = Scan_ClassifyPacket(rx_packet, rx_len, &message);
                    if (kind == SCAN_PACKET_BARCODE) {
                        rx_data = rx_packet;
                        rx_received = true;
                        break;
                    }

                    // --- Every handled non-barcode packet releases the assembler ---
                    rx_len = 0U;
                    if (kind == SCAN_PACKET_ACK) {
                        start_acked = true;
                    } else if (kind == SCAN_PACKET_REVISION) {
                        // A natural reply to another command is not this reading's result.
                    } else if (kind == SCAN_PACKET_RESEND) {
                        checksum_seen = true;
                        const uint8_t cause = SSI_NAK_RESEND;
                        ssi_len = Ssi_Encode(SSI_OP_CMD_NAK, &cause, 1U, ssi_packet);
                        if (!ScanUart_Wake() || !ScanUart_SendForReply(ssi_packet, ssi_len)) {
                            wait_error = true;
                            break;
                        }
                    } else if (kind == SCAN_PACKET_NAK) {
                        if (message.data[0] == SSI_NAK_RESEND) {
                            // Retransmit only a START explicitly rejected before its ACK.
                            // A later NAK must never restart an already accepted decode.
                            if (start_acked || start_resends >= SCAN_SSI_HOST_RESENDS) {
                                wait_error = true;
                                break;
                            }
                            ++start_resends;
                            ssi_len = Ssi_EncodeWithStatus(SSI_OP_START_DECODE,
                                    SSI_STATUS_RETRANSMIT, NULL, 0U, ssi_packet);
                            if (!ScanUart_Wake() || !ScanUart_SendStart(ssi_packet, ssi_len)) {
                                wait_error = true;
                                break;
                            }
                        } else {
                            hardware_error = true;
                            break;
                        }
                    } else {
                        hardware_error = true;
                        break;
                    }
                    AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
                }
                if (rx_received || wait_error || hardware_error)
                    break;
                AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
                continue;
            }

            // --- Recompute remaining time after task work, never renew the deadline ---
            elapsed = osKernelGetTickCount() - started;
            if (elapsed >= timeout_ticks)
                break;
            uint32_t remaining = timeout_ticks - elapsed;
            uint32_t wait_ticks = remaining < slice_ticks ? remaining : slice_ticks;
            uint32_t flags = osThreadFlagsWait(FLAG_SCAN_UART_RX, osFlagsWaitAny, wait_ticks);
            if (flags != osFlagsErrorTimeout && (flags & osFlagsError) != 0U) {
                wait_error = true;
                break;
            }
            AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
        }

        // --- Confirm one valid nonempty barcode before accepting its CAN result ---
        bool ack_error = false;
        if (rx_received && !wait_error && !hardware_error) {
            ssi_len = Ssi_Encode(SSI_OP_CMD_ACK, NULL, 0U, ssi_packet);
            ack_error = !ScanUart_Wake() || !ScanUart_Send(ssi_packet, ssi_len);
        }

		// --- Закрытие накопления и итоговая проверка RX ---
		// EndRead атомарно сохраняет итог ошибки и закрывает накопление.
		// Данные результата уже находятся в rx_packet задачи.
		if (!ScanUart_EndRead())
			wait_error = true;

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

		// --- Раздельные исходы ошибки ожидания и отсутствия ответа ---
		if (wait_error || ack_error) {
			ScanCtrl_HandleTransportError();
			CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_COMM);
		} else if (hardware_error) {
			ScanCtrl_HandleTransportError();
			CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_HARDWARE);
		} else if (!rx_received && checksum_seen) {
			ScanCtrl_HandleTransportError();
			CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_COMM);
		} else if (!rx_received) {
			ScanCtrl_HandleTimeout();
			CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_TIMEOUT);
		} else {

			if (rx_data != NULL && ScanCtrl_ProcessReceived(rx_data, rx_len)) {
				uint8_t buf[SHADOW_BUF_SIZE];
				uint8_t buf_len = 0;

				ScanCtrl_GetLastBarcode(buf, &buf_len);
				CAN_SendDataFragmented(cmd.cmd_code, buf, buf_len);
				CAN_SendDone(cmd.cmd_code, 0);
			} else {
				// --- Terminal failure also ends the controller operation ---
				ScanCtrl_HandleTransportError();
				CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_HARDWARE);
			}
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
