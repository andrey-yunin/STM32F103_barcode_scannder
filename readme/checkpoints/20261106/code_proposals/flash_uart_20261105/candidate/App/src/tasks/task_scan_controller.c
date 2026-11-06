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

// --- Проверка DECODE_DATA без сохранения результата и изменения статистики ---
static bool Scan_IsValidDecodeData(const uint8_t *data, uint16_t len) {
	SsiMsg_t message;

	return Ssi_Decode(data, len, &message)
			&& message.opcode == SSI_OP_DECODE_DATA && message.data_len >= 1U;
}

/*
 * Задача собирает SSI-пакет по Length, независимо от границ RX-событий.
 * Полный валидный ACK пропускается; следующий пакет собирается далее.
 * Повреждённый пакет передаётся существующей обработке ошибок.
 */
static bool Scan_CollectPacket(const uint8_t *data, uint16_t len,
		uint8_t *packet, uint16_t *packet_len) {
	for (uint16_t i = 0U; i < len; ++i) {
		packet[(*packet_len)++] = data[i];

		// --- Некорректная длина обрабатывается как ошибка SSI ---
		if (packet[0] < 4U || packet[0] > 4U + SSI_MAX_PAYLOAD)
			return true;

		// Length включает заголовок и данные, но не checksum.
		uint16_t expected = (uint16_t) packet[0] + 2U;

		if (*packet_len < expected)
			continue;

		// --- Полный ACK не завершает ожидание штрихкода ---
		SsiMsg_t message;

		if (Ssi_Decode(packet, *packet_len,
				&message) && message.opcode == SSI_OP_CMD_ACK) {
			*packet_len = 0U;
			continue;
		}

		return true;
	}

	return false;
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
		uint32_t timeout_ticks = (uint32_t) (((uint64_t) timeout_ms * tick_freq)
				/ 1000U);
		uint32_t slice_ticks = (500U * tick_freq) / 1000U;
		uint32_t started = osKernelGetTickCount();

		bool rx_received = false;
		bool wait_error = false;

		// --- Накопитель одного SSI-пакета принадлежит задаче Scanner ---
		static uint8_t rx_packet[SSI_PACKET_MAX_LEN];
		uint16_t rx_len = 0U;
		const uint8_t *rx_data = NULL;

		// --- Приём пакета в пределах исходного срока запроса ---
		// Сначала забираем накопленные байты, затем при необходимости ждём.
		// Число уведомлений не определяет количество доступных данных.
		for (;;) {
			// --- Отмена и общий срок ---
			if (AppSafety_IsResetPending())
				break;

			uint32_t elapsed = osKernelGetTickCount() - started;

			if (elapsed >= timeout_ticks)
				break;

			// --- Потребление накопленных байтов ---
			uint16_t received_len;
			bool receive_error;

			const uint8_t *received = ScanUart_GetReceived(&received_len,
					&receive_error);

			if (receive_error) {
				wait_error = true;
				break;
			}

			if (received != NULL) {
				AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);

				if (!Scan_CollectPacket(received, received_len, rx_packet,
						&rx_len))
					continue;

				rx_data = rx_packet;
				rx_received = true;
				break;
			}

			// --- Ожидание следующих данных без перезапуска срока ---
			uint32_t remaining = timeout_ticks - elapsed;
			uint32_t wait_ticks =
					remaining < slice_ticks ? remaining : slice_ticks;

			uint32_t flags = osThreadFlagsWait(
			FLAG_SCAN_UART_RX, osFlagsWaitAny, wait_ticks);

			if (flags == osFlagsErrorTimeout) {
				AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
				continue;
			}

			if ((flags & osFlagsError) != 0U) {
				wait_error = true;
				break;
			}

			AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
		}

		// --- Подтверждение штрихкода сканеру до принятия результата и отправки CAN ---
		bool ack_error = false;

		if (rx_received && !wait_error
				&& Scan_IsValidDecodeData(rx_data, rx_len)) {
			ssi_len = Ssi_Encode(SSI_OP_CMD_ACK, NULL, 0, ssi_packet);
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
