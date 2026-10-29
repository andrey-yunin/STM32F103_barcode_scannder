/*
 * task_can_handler.c
 *
 *  Created on: Jul 10, 2026
 *      Author: andrey
 *
 *  Задача CAN Handler (транспортный уровень).
 *  Принимает сырые CAN-фреймы из очереди can_rx_queue (из ISR),
 *  отправляет фреймы из can_tx_queue, парсит заголовок и forwarding
 *  в dispatcher_queue. Реагирует на FLAG_CAN_RX / FLAG_CAN_TX.
 */

#include <string.h>

#include "main.h"

#include "task_can_handler.h"
#include "app_config.h"
#include "cmsis_os.h"
#include "app_queues.h"
#include "can_protocol.h"
#include "task_watchdog.h"
#include "app_flash.h"

// --- Счётчики CAN diagnostics ---
static volatile CanDiagnostics_t g_can_diag;

// --- Фильтрация COMMAND по destination и IDE ---
// Как в HC: source, RTR и priority аппаратной маской не проверяются.
#define CAN_FILTER_MSGTYPE_DST_MASK (0x03FFUL << 19)
#define CAN_FILTER_IDE              (1UL << 2)

static void CAN_ConfigureFilterBank(uint8_t bank, uint8_t destination) {
	CAN_FilterTypeDef filter_config;
	uint32_t filter_id;
	uint32_t filter_mask;
	uint32_t filter_reg;

	memset(&filter_config, 0, sizeof(filter_config));

	filter_id = CAN_BUILD_ID(0U, CAN_MSG_TYPE_COMMAND, destination, 0U);

	filter_reg = (filter_id << 3) | CAN_FILTER_IDE;
	filter_mask = CAN_FILTER_MSGTYPE_DST_MASK | CAN_FILTER_IDE;

	filter_config.FilterBank = bank;
	filter_config.FilterMode = CAN_FILTERMODE_IDMASK;
	filter_config.FilterScale = CAN_FILTERSCALE_32BIT;
	filter_config.FilterIdHigh = (uint16_t) (filter_reg >> 16);
	filter_config.FilterIdLow = (uint16_t) (filter_reg & 0xFFFFU);
	filter_config.FilterMaskIdHigh = (uint16_t) (filter_mask >> 16);
	filter_config.FilterMaskIdLow = (uint16_t) (filter_mask & 0xFFFFU);
	filter_config.FilterFIFOAssignment = CAN_RX_FIFO0;
	filter_config.FilterActivation = ENABLE;
	filter_config.SlaveStartFilterBank = 14U;

	if (HAL_CAN_ConfigFilter(&hcan, &filter_config) != HAL_OK) {
		Error_Handler();
	}
}

// --- Программная проверка кадра до чтения payload ---
// Порядок соответствует HC: IDE, RTR, DLC, type, source, destination.
// Невалидный transport отбрасывается без ACK/NACK.
// Для RTR/source дополнительные счётчики не вводятся.
static bool CAN_IsAcceptedCommand(const CanRxFrame_t *rx_frame) {
	uint8_t destination;
	uint8_t source;
	uint8_t node_id;

	if (rx_frame == NULL) {
		return false;
	}

	if (rx_frame->header.IDE != CAN_ID_EXT) {
		g_can_diag.dropped_not_ext++;
		return false;
	}

	if (rx_frame->header.RTR != CAN_RTR_DATA) {
		return false;
	}

	if (rx_frame->header.DLC != CAN_FRAME_DLC) {
		g_can_diag.dropped_wrong_dlc++;
		return false;
	}

	if (CAN_GET_MSG_TYPE(rx_frame->header.ExtId) != CAN_MSG_TYPE_COMMAND) {
		g_can_diag.dropped_wrong_type++;
		return false;
	}

	destination = CAN_GET_DST_ADDR(rx_frame->header.ExtId);
	source = CAN_GET_SRC_ADDR(rx_frame->header.ExtId);
	node_id = (uint8_t) AppConfig_GetPerformerID();

	if (source != CAN_ADDR_CONDUCTOR) {
		return false;
	}

	if ((destination != node_id) && (destination != CAN_ADDR_BROADCAST)) {
		g_can_diag.dropped_wrong_dst++;
		return false;
	}

	return true;
}

// --- Распаковка только после успешной проверки transport ---
// Полностью очищаем структуру и копируем фиксированные поля кадра.
// Payload содержит cmd16, device8 и пять байтов data.
static void CAN_ParseCommand(const CanRxFrame_t *rx_frame,
		ParsedCanCommand_t *parsed_command) {
	memset(parsed_command, 0, sizeof(*parsed_command));

	parsed_command->cmd_code = (uint16_t) rx_frame->data[0]
			| ((uint16_t) rx_frame->data[1] << 8);

	parsed_command->device_id = rx_frame->data[2];

	memcpy(parsed_command->data, &rx_frame->data[3],
			sizeof(parsed_command->data));
	parsed_command->data_len = sizeof(parsed_command->data);
}

// --- Bank 1 следует за текущим NodeID; broadcast bank 0 сохраняется ---
void CAN_UpdateDirectFilter(uint8_t destination) {
	CAN_ConfigureFilterBank(1U, destination);
}

// --- Согласованный снимок с восстановлением исходного состояния IRQ ---
// Механизм HC: временно запрещаем прерывания на время копирования.
// Если IRQ были запрещены вызывающим кодом, они останутся запрещёнными.
void CAN_Diagnostics_GetSnapshot(CanDiagnostics_t *out) {
	uint32_t primask;

	if (out == NULL) {
		return;
	}

	primask = __get_PRIMASK();

	__disable_irq();
	memcpy(out, (const void*) &g_can_diag, sizeof(CanDiagnostics_t));
	__set_PRIMASK(primask);
}

void CAN_Diagnostics_RecordRxQueueOverflow(void) {
	g_can_diag.rx_queue_overflow++;
}

void CAN_Diagnostics_RecordAppQueueOverflow(void) {
	g_can_diag.app_queue_overflow++;
}

void CAN_Diagnostics_RecordCanError(uint32_t hal_error, uint32_t esr) {
	g_can_diag.can_error_callback_count++;
	g_can_diag.last_hal_error = hal_error;
	g_can_diag.last_esr = esr;

	if ((esr & CAN_ESR_EWGF) != 0U) {
		g_can_diag.error_warning_count++;
	}

	if ((esr & CAN_ESR_EPVF) != 0U) {
		g_can_diag.error_passive_count++;
	}

	if ((esr & CAN_ESR_BOFF) != 0U) {
		g_can_diag.bus_off_count++;
	}
}

// --- Единая постановка кадра в программную TX-очередь ---
// Механизм HC: исходный кадр не изменяется, ожидания свободного места нет.
// При отказе учитываем потерю; физическую передачу выполняет CAN-задача.
static void CAN_QueueTxFrame(const CanTxFrame_t *tx_frame) {
	if (tx_frame == NULL) {
		return;
	}

	if (osMessageQueuePut(can_tx_queueHandle, tx_frame, 0U, 0U) == osOK) {
		(void) osThreadFlagsSet(task_can_handleHandle, FLAG_CAN_TX);
	} else {
		g_can_diag.tx_queue_overflow++;
	}
}

static void CAN_SendNack(uint16_t cmd_code, uint16_t error_code) {
	CanTxFrame_t tx;
	memset(&tx, 0, sizeof(tx));

	tx.header.ExtId = CAN_BUILD_ID(CAN_PRIORITY_NORMAL, CAN_MSG_TYPE_NACK,
			CAN_ADDR_CONDUCTOR, (uint32_t )AppConfig_GetPerformerID());

	tx.header.IDE = CAN_ID_EXT;
	tx.header.RTR = CAN_RTR_DATA;
	tx.header.DLC = 8;

	tx.data[0] = (uint8_t) (cmd_code & 0xFF);
	tx.data[1] = (uint8_t) ((cmd_code >> 8) & 0xFF);
	tx.data[2] = (uint8_t) (error_code & 0xFF);
	tx.data[3] = (uint8_t) ((error_code >> 8) & 0xFF);

	CAN_QueueTxFrame(&tx);
}

void CAN_SendAck(uint16_t cmd_code) {
	CanTxFrame_t tx;
	memset(&tx, 0, sizeof(tx));

	tx.header.ExtId = CAN_BUILD_ID(CAN_PRIORITY_NORMAL, CAN_MSG_TYPE_ACK,
			CAN_ADDR_CONDUCTOR, (uint32_t )AppConfig_GetPerformerID());

	tx.header.IDE = CAN_ID_EXT;
	tx.header.RTR = CAN_RTR_DATA;
	tx.header.DLC = 8;

	tx.data[0] = (uint8_t) (cmd_code & 0xFF);
	tx.data[1] = (uint8_t) ((cmd_code >> 8) & 0xFF);

	CAN_QueueTxFrame(&tx);
}

void CAN_SendNackPublic(uint16_t cmd_code, uint16_t error_code) {
	CAN_SendNack(cmd_code, error_code);
}

void CAN_SendDone(uint16_t cmd_code, uint8_t device_id) {
	CanTxFrame_t tx;
	memset(&tx, 0, sizeof(tx));

	tx.header.ExtId = CAN_BUILD_ID(CAN_PRIORITY_NORMAL,
			CAN_MSG_TYPE_DATA_DONE_LOG, CAN_ADDR_CONDUCTOR,
			(uint32_t )AppConfig_GetPerformerID());

	tx.header.IDE = CAN_ID_EXT;
	tx.header.RTR = CAN_RTR_DATA;
	tx.header.DLC = 8;

	tx.data[0] = CAN_SUB_TYPE_DONE;
	tx.data[1] = (uint8_t) (cmd_code & 0xFF);
	tx.data[2] = (uint8_t) ((cmd_code >> 8) & 0xFF);
	tx.data[3] = device_id;

	CAN_QueueTxFrame(&tx);
}

// --- DATA: номер фрагмента и EOT задаёт вызывающий обработчик ---
// Механизм HC: полный сброс кадра, ограничение копирования шестью байтами.
// cmd_code сохраняется в API, но не кодируется в DATA payload.
void CAN_SendData(uint16_t cmd_code, uint8_t sequence_info, const uint8_t *data,
		uint8_t len) {
	CanTxFrame_t tx_frame;
	uint8_t copy_len;

	(void) cmd_code;

	memset(&tx_frame, 0, sizeof(tx_frame));

	copy_len = len;
	if (copy_len > CAN_DATA_PAYLOAD_MAX) {
		copy_len = CAN_DATA_PAYLOAD_MAX;
	}

	tx_frame.header.ExtId = CAN_BUILD_ID(CAN_PRIORITY_NORMAL,
			CAN_MSG_TYPE_DATA_DONE_LOG, CAN_ADDR_CONDUCTOR,
			AppConfig_GetPerformerID());

	tx_frame.header.IDE = CAN_ID_EXT;
	tx_frame.header.RTR = CAN_RTR_DATA;
	tx_frame.header.DLC = CAN_FRAME_DLC;

	tx_frame.data[0] = CAN_SUB_TYPE_DATA;
	tx_frame.data[1] = sequence_info;

	if ((data != NULL) && (copy_len > 0U)) {
		memcpy(&tx_frame.data[2], data, copy_len);
	}

	CAN_QueueTxFrame(&tx_frame);
}

// --- Checked diagnostic queue admission, bounded across the entire response ---
// Runs only in a task. CAN handler retains sole ownership of HAL_CAN.
static bool CAN_QueueDiagnostic(CanTxFrame_t *tx, uint32_t start,
		uint32_t budget) {
	uint32_t elapsed = osKernelGetTickCount() - start;
	if (elapsed >= budget
			|| osMessageQueuePut(can_tx_queueHandle, tx, 0, budget - elapsed)
					!= osOK) {
		++g_can_diag.tx_queue_overflow;
		return false;
	}
	osThreadFlagsSet(task_can_handleHandle, FLAG_CAN_TX);
	return true;
}

// --- Framed diagnostic DATA followed by DONE only if all frames were queued ---
// Queue admission does not prove bus delivery; Studio still validates all DATA.
bool CAN_SendDiagnosticResponse(uint16_t cmd_code, const uint8_t *data,
		uint16_t len) {
	if (data == NULL || len == 0U || len > 768U || len % 6U != 0U)
		return false;
	uint32_t budget = osKernelGetTickFreq() / 10U; // 100 ms maximum total queue wait
	if (budget == 0U)
		budget = 1U;
	uint32_t start = osKernelGetTickCount();
	CanTxFrame_t tx;
	memset(&tx, 0, sizeof(tx));
	tx.header.ExtId = CAN_BUILD_ID(CAN_PRIORITY_NORMAL,
			CAN_MSG_TYPE_DATA_DONE_LOG, CAN_ADDR_CONDUCTOR,
			(uint32_t )AppConfig_GetPerformerID());
	tx.header.IDE = CAN_ID_EXT;
	tx.header.RTR = CAN_RTR_DATA;
	tx.header.DLC = 8;

	// --- Six-byte records preserve existing sequence/EOT semantics ---
	for (uint16_t offset = 0; offset < len; offset += 6U) {
		tx.data[0] = CAN_SUB_TYPE_DATA;
		tx.data[1] = (uint8_t) (offset / 6U);
		if (offset + 6U == len)
			tx.data[1] |= 0x80U;
		memcpy(&tx.data[2], &data[offset], 6U);
		if (!CAN_QueueDiagnostic(&tx, start, budget))
			return false;
	}

	// --- Terminal uses the same checked queue path, selector zero ---
	memset(tx.data, 0, sizeof(tx.data));
	tx.data[0] = CAN_SUB_TYPE_DONE;
	tx.data[1] = (uint8_t) cmd_code;
	tx.data[2] = (uint8_t) (cmd_code >> 8);
	return CAN_QueueDiagnostic(&tx, start, budget);
}

// --- DATA (фрагментированная) — для строк штрихкода длиннее 6 байт ---
// Пакет: byte0=CAN_SUB_TYPE_DATA, byte1=(seq|EOT), bytes 2..7=payload
// EOT=0x80 на последнем фрагменте, seq 0..N для промежуточных
void CAN_SendDataFragmented(uint16_t cmd_code, const uint8_t *data,
		uint8_t total_len) {
	(void) cmd_code;

	uint8_t offset = 0;
	uint8_t seq = 0;

	while (offset < total_len) {
		CanTxFrame_t tx;
		memset(&tx, 0, sizeof(tx));

		tx.header.ExtId = CAN_BUILD_ID(CAN_PRIORITY_NORMAL,
				CAN_MSG_TYPE_DATA_DONE_LOG, CAN_ADDR_CONDUCTOR,
				(uint32_t )AppConfig_GetPerformerID());
		tx.header.IDE = CAN_ID_EXT;
		tx.header.RTR = CAN_RTR_DATA;
		tx.header.DLC = 8;

		uint8_t chunk = total_len - offset;
		if (chunk > 6)
			chunk = 6;

		tx.data[0] = CAN_SUB_TYPE_DATA;
		tx.data[1] = seq;
		if (offset + chunk >= total_len) {
			tx.data[1] |= 0x80; // EOT на последнем фрагменте
		}

		for (uint8_t i = 0; i < chunk; i++) {
			tx.data[2 + i] = data[offset + i];
		}

		CAN_QueueTxFrame(&tx);

		offset += chunk;
		seq++;
	}
}

void app_start_task_can_handler(void *argument) {
	(void) argument;
	CanRxFrame_t rx_frame;
	CanTxFrame_t tx_frame;
	ParsedCanCommand_t parsed_command;
	uint32_t txMailbox;

	// --- Настройка аппаратных CAN-фильтров ---
	// Permissive: принимаем только 29-bit Extended ID (IDE=1).
	// Фильтрация по NodeID и типу сообщения — программно ниже,
	// по рантайм-адресу (корректно работает после SET_NODE_ID).

	// --- Два банка COMMAND: broadcast и текущий RAM NodeID ---
	// Настройка выполняется до HAL_CAN_Start, как в HC.
	CAN_ConfigureFilterBank(0U, CAN_ADDR_BROADCAST);
	CAN_ConfigureFilterBank(1U, (uint8_t) AppConfig_GetPerformerID());

	// --- Запуск CAN и активация прерываний ---
	if (HAL_CAN_Start(&hcan) != HAL_OK) {
		Error_Handler();
	}

	if (HAL_CAN_ActivateNotification(&hcan,
	CAN_IT_RX_FIFO0_MSG_PENDING |
	CAN_IT_RX_FIFO0_FULL |
	CAN_IT_RX_FIFO0_OVERRUN |
	CAN_IT_ERROR_WARNING |
	CAN_IT_ERROR_PASSIVE |
	CAN_IT_BUSOFF |
	CAN_IT_LAST_ERROR_CODE |
	CAN_IT_ERROR) != HAL_OK) {
		Error_Handler();
	}

	// --- Интервал ожидания в тиках RTOS ---
	// Механизм HC: перевод миллисекунд в единицы CMSIS-RTOS.
	const uint32_t idle_wait_ticks = (APP_WATCHDOG_TASK_IDLE_TIMEOUT_MS
			* osKernelGetTickFreq()) / 1000U;

	// --- Основной цикл приёма (event-driven, RX + TX) ---
	for (;;) {
		uint32_t flags;

		// --- Ожидание события или штатное idle-пробуждение ---
		flags = osThreadFlagsWait(
		FLAG_CAN_RX | FLAG_CAN_TX,
		osFlagsWaitAny, idle_wait_ticks);

		// Отсутствие трафика допустимо: задача исполняется.
		if (flags == osFlagsErrorTimeout) {
			AppWatchdog_Heartbeat(APP_WDG_CLIENT_CAN);
			continue;
		}

		// Ошибочное ожидание не подтверждает прогресс задачи.
		if ((flags & osFlagsError) != 0U) {
			continue;
		}

		// Событие получено; далее задача должна обработать RX/TX.
		// Если обработка зависнет, следующие heartbeat не появятся.
		AppWatchdog_Heartbeat(APP_WDG_CLIENT_CAN);

		// --- RX: проверка, распаковка, передача Dispatcher ---
		if ((flags & FLAG_CAN_RX) != 0U) {
			while (osMessageQueueGet(can_rx_queueHandle, &rx_frame, NULL, 0U)
					== osOK) {
				if (!CAN_IsAcceptedCommand(&rx_frame)) {
					continue;
				}

				CAN_ParseCommand(&rx_frame, &parsed_command);

				if (osMessageQueuePut(dispatcher_queueHandle, &parsed_command,
						0U, 0U) == osOK) {
					g_can_diag.rx_total++;
				} else {
					g_can_diag.dispatcher_queue_overflow++;
				}
			}
		}

		// === Обработка TX ===

		if (flags & FLAG_CAN_TX) {
			while (osMessageQueueGet(can_tx_queueHandle, &tx_frame, NULL, 0)
					== osOK) {
				// Bounded wait 10ms на свободный mailbox
				uint32_t start_tick = osKernelGetTickCount();
				while (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0) {
					if ((osKernelGetTickCount() - start_tick) > 10) {
						break;
					}
					osDelay(1);
				}

				if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
					if (HAL_CAN_AddTxMessage(&hcan, &tx_frame.header,
							tx_frame.data, &txMailbox) == HAL_OK) {
						g_can_diag.tx_total++;
					} else {
						g_can_diag.tx_hal_error++;
						g_can_diag.last_hal_error = HAL_CAN_GetError(&hcan);
						g_can_diag.last_esr = hcan.Instance->ESR;
					}
				} else {
					g_can_diag.tx_mailbox_timeout++;
				}
			}
		}
	}
}

