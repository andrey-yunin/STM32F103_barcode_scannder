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


// --- Счётчики CAN diagnostics ---
static volatile CanDiagnostics_t g_can_diag;


// --- Вспомогательные функции диагностики ---

void CAN_Diagnostics_GetSnapshot(CanDiagnostics_t *out)
{
    if (out == NULL) {
        return;
    }

    __disable_irq();
    memcpy(out, (const void *)&g_can_diag, sizeof(CanDiagnostics_t));
    __enable_irq();
}

void CAN_Diagnostics_RecordRxQueueOverflow(void)
{
    g_can_diag.rx_queue_overflow++;
}

void CAN_Diagnostics_RecordAppQueueOverflow(void)
{
    g_can_diag.app_queue_overflow++;
}

void CAN_Diagnostics_RecordCanError(uint32_t hal_error, uint32_t esr)
{
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


// --- CAN-отправка: единая точка постановки в TX-очередь ---

static void CAN_QueueTxFrame(CanTxFrame_t *tx)
{
    if (osMessageQueuePut(can_tx_queueHandle, tx, 0, 0) == osOK) {
        osThreadFlagsSet(task_can_handleHandle, FLAG_CAN_TX);
    } else {
        g_can_diag.tx_queue_overflow++;
    }
}

static void CAN_SendNack(uint16_t cmd_code, uint16_t error_code)
{
    CanTxFrame_t tx;
    memset(&tx, 0, sizeof(tx));

    tx.header.ExtId = CAN_BUILD_ID(CAN_PRIORITY_NORMAL,
                                   CAN_MSG_TYPE_NACK,
                                   CAN_ADDR_CONDUCTOR,
                                   CAN_ADDR_SCANNER_BOARD);

    tx.header.IDE = CAN_ID_EXT;
    tx.header.RTR = CAN_RTR_DATA;
    tx.header.DLC = 8;

    tx.data[0] = (uint8_t)(cmd_code & 0xFF);
    tx.data[1] = (uint8_t)((cmd_code >> 8) & 0xFF);
    tx.data[2] = (uint8_t)(error_code & 0xFF);
    tx.data[3] = (uint8_t)((error_code >> 8) & 0xFF);

    CAN_QueueTxFrame(&tx);
}

void CAN_SendAck(uint16_t cmd_code)
{
    CanTxFrame_t tx;
    memset(&tx, 0, sizeof(tx));

    tx.header.ExtId = CAN_BUILD_ID(CAN_PRIORITY_NORMAL,
                                   CAN_MSG_TYPE_ACK,
                                   CAN_ADDR_CONDUCTOR,
                                   CAN_ADDR_SCANNER_BOARD);

    tx.header.IDE = CAN_ID_EXT;
    tx.header.RTR = CAN_RTR_DATA;
    tx.header.DLC = 8;

    tx.data[0] = (uint8_t)(cmd_code & 0xFF);
    tx.data[1] = (uint8_t)((cmd_code >> 8) & 0xFF);

    CAN_QueueTxFrame(&tx);
}

void CAN_SendNackPublic(uint16_t cmd_code, uint16_t error_code)
{
    CAN_SendNack(cmd_code, error_code);
}

void CAN_SendDone(uint16_t cmd_code, uint8_t device_id)
{
    CanTxFrame_t tx;
    memset(&tx, 0, sizeof(tx));

    tx.header.ExtId = CAN_BUILD_ID(CAN_PRIORITY_NORMAL,
                                   CAN_MSG_TYPE_DATA_DONE_LOG,
                                   CAN_ADDR_CONDUCTOR,
                                   CAN_ADDR_SCANNER_BOARD);

    tx.header.IDE = CAN_ID_EXT;
    tx.header.RTR = CAN_RTR_DATA;
    tx.header.DLC = 8;

    tx.data[0] = CAN_SUB_TYPE_DONE;
    tx.data[1] = (uint8_t)(cmd_code & 0xFF);
    tx.data[2] = (uint8_t)((cmd_code >> 8) & 0xFF);
    tx.data[3] = device_id;

    CAN_QueueTxFrame(&tx);
}

void CAN_SendData(uint16_t cmd_code, uint8_t *data, uint8_t len)
{
    (void)cmd_code;

    CanTxFrame_t tx;
    memset(&tx, 0, sizeof(tx));

    tx.header.ExtId = CAN_BUILD_ID(CAN_PRIORITY_NORMAL,
                                   CAN_MSG_TYPE_DATA_DONE_LOG,
                                   CAN_ADDR_CONDUCTOR,
                                   CAN_ADDR_SCANNER_BOARD);

    tx.header.IDE = CAN_ID_EXT;
    tx.header.RTR = CAN_RTR_DATA;
    tx.header.DLC = 8;

    tx.data[0] = CAN_SUB_TYPE_DATA;
    tx.data[1] = 0x80; // EOT=1, Seq=0

    for (uint8_t i = 0; i < len && i < 6; i++) {
        tx.data[2 + i] = data[i];
    }

    CAN_QueueTxFrame(&tx);
}

// --- DATA (фрагментированная) — для строк штрихкода длиннее 6 байт ---
// Пакет: byte0=CAN_SUB_TYPE_DATA, byte1=(seq|EOT), bytes 2..7=payload
// EOT=0x80 на последнем фрагменте, seq 0..N для промежуточных
void CAN_SendDataFragmented(uint16_t cmd_code, const uint8_t *data, uint8_t total_len)
{
    (void)cmd_code;

    uint8_t offset = 0;
    uint8_t seq = 0;

    while (offset < total_len) {
        CanTxFrame_t tx;
        memset(&tx, 0, sizeof(tx));

        tx.header.ExtId = CAN_BUILD_ID(CAN_PRIORITY_NORMAL,
                                       CAN_MSG_TYPE_DATA_DONE_LOG,
                                       CAN_ADDR_CONDUCTOR,
                                       CAN_ADDR_SCANNER_BOARD);
        tx.header.IDE = CAN_ID_EXT;
        tx.header.RTR = CAN_RTR_DATA;
        tx.header.DLC = 8;

        uint8_t chunk = total_len - offset;
        if (chunk > 6) chunk = 6;

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



void app_start_task_can_handler(void *argument)
{
	(void)argument;
	CanRxFrame_t rx_frame;
	CanTxFrame_t tx_frame;
	uint32_t txMailbox;

    // --- Настройка CAN-фильтра (promiscuous: все Extended ID фреймы) ---
    // Программная фильтрация dst_addr и DLC выполняется ниже в цикле.
    // Аппаратный фильтр на 0/0 пропускает всё на FIFO0.
    CAN_FilterTypeDef sFilterConfig;
    uint32_t filter_id   = 0x00000000 | CAN_ID_EXT;
    uint32_t filter_mask = 0x00000000 | CAN_ID_EXT;

    sFilterConfig.FilterBank = 0;
    sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
    sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
    sFilterConfig.FilterIdHigh = (uint16_t)(filter_id >> 16);
    sFilterConfig.FilterIdLow = (uint16_t)(filter_id & 0xFFFF);
    sFilterConfig.FilterMaskIdHigh = (uint16_t)(filter_mask >> 16);
    sFilterConfig.FilterMaskIdLow = (uint16_t)(filter_mask & 0xFFFF);
    sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
    sFilterConfig.FilterActivation = ENABLE;
    sFilterConfig.SlaveStartFilterBank = 14;

    if (HAL_CAN_ConfigFilter(&hcan, &sFilterConfig) != HAL_OK) {
    	Error_Handler();
    }

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


    // --- Основной цикл приёма (event-driven, RX + TX) ---
    for (;;) {

    	uint32_t flags = osThreadFlagsWait(FLAG_CAN_RX | FLAG_CAN_TX,
    												osFlagsWaitAny,
													osWaitForever);

    	if ((flags & osFlagsError) != 0U) {
    		continue;
    	}


    	// --- Обработка всех накопившихся RX-фреймов ---

    	if (flags & FLAG_CAN_RX) {

    		while (osMessageQueueGet(can_rx_queueHandle, &rx_frame, NULL, 0) == osOK) {

    			// Проверка Extended ID
    			if (rx_frame.header.IDE != CAN_ID_EXT) {
    				g_can_diag.dropped_not_ext++;
    				continue;
    			}

    			// Программная фильтрация адреса назначения (наш ID или Broadcast)
    			uint32_t can_id = rx_frame.header.ExtId;
    			uint8_t dst_addr = CAN_GET_DST_ADDR(can_id);
    			if (dst_addr != CAN_ADDR_SCANNER_BOARD && dst_addr != CAN_ADDR_BROADCAST) {
    				g_can_diag.dropped_wrong_dst++;
    				continue;
    			}

    			// Проверка типа сообщения — только COMMAND
    			uint8_t msg_type = CAN_GET_MSG_TYPE(can_id);
    			if (msg_type != CAN_MSG_TYPE_COMMAND) {
    				g_can_diag.dropped_wrong_type++;
    				continue;
    			}

    			// Строгий DLC=8 для COMMAND
    			if (rx_frame.header.DLC != 8) {
    				g_can_diag.dropped_wrong_dlc++;
    				continue;
    			}

    			// Распаковка в ParsedCanCommand_t
    			ParsedCanCommand_t parsed;
    			parsed.cmd_code = (uint16_t)(rx_frame.data[0] |
    							((uint16_t)rx_frame.data[1] << 8));
    			parsed.device_id = rx_frame.data[2];
    			parsed.data_len = 5;
    			for (uint8_t i = 0; i < 5; i++) {
    				parsed.data[i] = rx_frame.data[3 + i];
    			}

    			// Форвард в dispatcher. Если очередь полна — фрейм теряется.
    			if (osMessageQueuePut(dispatcher_queueHandle, &parsed, 0, 0) == osOK) {
    				g_can_diag.rx_total++;
    			}
    			else {
    				g_can_diag.dispatcher_queue_overflow++;
    			}
    		}
    	}


    	// === Обработка TX ===

    	if (flags & FLAG_CAN_TX) {
    		while (osMessageQueueGet(can_tx_queueHandle, &tx_frame, NULL, 0) == osOK) {
    			// Bounded wait 10ms на свободный mailbox
    			uint32_t start_tick = osKernelGetTickCount();
    			while (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0) {
    				if ((osKernelGetTickCount() - start_tick) > 10) {
    					break;
    				}
    				osDelay(1);
    			}

    			if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
    				if (HAL_CAN_AddTxMessage(&hcan, &tx_frame.header, tx_frame.data, &txMailbox) == HAL_OK) {
    					g_can_diag.tx_total++;
    					}
    				else {
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

