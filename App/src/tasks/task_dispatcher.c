/*
 * task_dispatcher.c
 *
 *  Created on: Jul 10, 2026
 *      Author: andrey
 *
 *  Задача Dispatcher (прикладной уровень).
 *  Получает ParsedCanCommand_t из dispatcher_queue (от CAN Handler).
 *  Жизненный цикл команды: ACK -> [DATA/forward] -> DONE/NACK.
 *  Сервисные команды 0xF0xx обрабатывает напрямую.
 *  Доменные команды 0x06xx форвардит в scan_queue.
 */

#include <string.h>

#include "task_dispatcher.h"

#include "cmsis_os.h"
#include "app_queues.h"
#include "app_config.h"
#include "can_protocol.h"
#include "app_flash.h"
#include "task_watchdog.h"


// --- Отправка одной метрики GET_STATUS ---
// Формат DATA: byte 0..1 = metric_id (LE), byte 2..5 = value (LE)
static void SendStatusMetric(uint16_t cmd_code, uint16_t metric_id, uint32_t value)
{
    uint8_t data[6];

    data[0] = (uint8_t)(metric_id & 0xFF);
    data[1] = (uint8_t)((metric_id >> 8) & 0xFF);
    data[2] = (uint8_t)(value & 0xFF);
    data[3] = (uint8_t)((value >> 8) & 0xFF);
    data[4] = (uint8_t)((value >> 16) & 0xFF);
    data[5] = (uint8_t)((value >> 24) & 0xFF);

    CAN_SendData(cmd_code, data, sizeof(data));
}


void app_start_task_dispatcher(void *argument)
{
    (void)argument;

    ParsedCanCommand_t parsed;

    for (;;) {
    	AppWatchdog_Heartbeat(APP_WDG_CLIENT_DISPATCHER);

        // 1. Ожидание команды
        if (osMessageQueueGet(dispatcher_queueHandle, &parsed, NULL,  APP_WATCHDOG_TASK_IDLE_TIMEOUT_MS) != osOK) {
            continue;
        }

        // 2. Немедленный ACK (золотое правило)
        CAN_SendAck(parsed.cmd_code);

        // 3. Маршрутизация
        switch (parsed.cmd_code) {

        // ============================================================
        // ДОМЕННЫЕ КОМАНДЫ 0x06xx — форвард в scan_queue
        // ============================================================
        case CAN_CMD_SCAN_REQUEST:
        case CAN_CMD_SCAN_GET_DATA: {
            ScanResult_t scan_cmd;
            memset(&scan_cmd, 0, sizeof(scan_cmd));
            scan_cmd.cmd_code = parsed.cmd_code;
            // timeout_sec из CAN-фрейма: byte2=lo, byte3=hi (через device_id и data[0])
            if (parsed.cmd_code == CAN_CMD_SCAN_REQUEST) {
                scan_cmd.timeout_sec = (uint16_t)(parsed.device_id) |
                                    ((uint16_t)parsed.data[0] << 8);
            }

            if (osMessageQueuePut(scan_queueHandle, &scan_cmd, 0, 0) != osOK) {
                CAN_Diagnostics_RecordAppQueueOverflow();
                CAN_SendNackPublic(parsed.cmd_code, CAN_ERR_DEVICE_BUSY);
            }
            break;
        }

        // ============================================================
        // УНИВЕРСАЛЬНЫЕ СЕРВИСНЫЕ КОМАНДЫ 0xF0xx
        // ============================================================
        case CAN_CMD_SRV_GET_DEVICE_INFO: {
            uint8_t data[6];
            // Пакет 1: DeviceType, FW Rev
            data[0] = CAN_DEVICE_TYPE_SCANNER;
            data[1] = FW_REV_MAJOR;
            data[2] = FW_REV_MINOR;
            data[3] = 0; // номер канала (1 scanner = 1)
            data[4] = 0;
            data[5] = 0;
            CAN_SendData(parsed.cmd_code, data, 6);

            CAN_SendDone(parsed.cmd_code, 0);
            break;
        }

        case CAN_CMD_SRV_REBOOT: {
            uint16_t key = (uint16_t)(parsed.data[0] | ((uint16_t)parsed.data[1] << 8));
            if (key == SRV_MAGIC_REBOOT) {
                CAN_SendDone(parsed.cmd_code, 0);
                osDelay(100);
                NVIC_SystemReset();
            } else {
                CAN_SendNackPublic(parsed.cmd_code, CAN_ERR_INVALID_KEY);
            }
            break;
        }

        case CAN_CMD_SRV_FLASH_COMMIT: {
            // --- COMMIT: сохранение RAM-конфигурации в Flash ---
            if (AppConfig_Commit()) {
                CAN_SendDone(parsed.cmd_code, 0);
            } else {
                CAN_SendNackPublic(parsed.cmd_code, CAN_ERR_FLASH_WRITE);
            }
            break;
        }

        case CAN_CMD_SRV_GET_UID: {
            // F004: возвращаем 96-bit UID (12 байт) в двух DATA-пакетах
            uint8_t uid[12];
            uint8_t data[6];

            // --- GET_UID: чтение через app_flash (инкапсулирует адрес UID) ---
            AppConfig_GetMCU_UID(uid);

            // Пакет 1: UID[0..5]
            memcpy(data, &uid[0], 6);
            CAN_SendData(parsed.cmd_code, data, 6);

            // Пакет 2: UID[6..11]
            memcpy(data, &uid[6], 6);
            CAN_SendData(parsed.cmd_code, data, 6);

            CAN_SendDone(parsed.cmd_code, 0);
            break;
        }

        case CAN_CMD_SRV_SET_NODE_ID: {
            // --- SET_NODE_ID: смена NodeID с сохранением в Flash ---
            uint8_t new_id = parsed.data[0];
            if (new_id < CAN_DYNAMIC_NODE_ID_MIN || new_id > CAN_DYNAMIC_NODE_ID_MAX) {
                CAN_SendNackPublic(parsed.cmd_code, CAN_ERR_INVALID_PARAM);
                break;
            }

            AppConfig_SetNodeID(new_id);

            if (AppConfig_Commit()) {
                CAN_SendDone(parsed.cmd_code, 0);
            } else {
                CAN_SendNackPublic(parsed.cmd_code, CAN_ERR_FLASH_WRITE);
            }
            break;
        }

        case CAN_CMD_SRV_FACTORY_RESET: {
            uint16_t key = (uint16_t)(parsed.data[0] | ((uint16_t)parsed.data[1] << 8));
            if (key == SRV_MAGIC_FACTORY_RESET) {
                // --- FACTORY_RESET: стирание Flash + перезагрузка ---
                AppConfig_FactoryReset();
                CAN_SendDone(parsed.cmd_code, 0);
                osDelay(100);
                NVIC_SystemReset();
            } else {
                CAN_SendNackPublic(parsed.cmd_code, CAN_ERR_INVALID_KEY);
            }
            break;
        }

        case CAN_CMD_SRV_GET_STATUS: {
            CanDiagnostics_t diag;
            CAN_Diagnostics_GetSnapshot(&diag);

            SendStatusMetric(parsed.cmd_code, CAN_STATUS_RX_TOTAL, diag.rx_total);
            SendStatusMetric(parsed.cmd_code, CAN_STATUS_TX_TOTAL, diag.tx_total);
            SendStatusMetric(parsed.cmd_code, CAN_STATUS_RX_QUEUE_OVERFLOW, diag.rx_queue_overflow);
            SendStatusMetric(parsed.cmd_code, CAN_STATUS_TX_QUEUE_OVERFLOW, diag.tx_queue_overflow);
            SendStatusMetric(parsed.cmd_code, CAN_STATUS_DISPATCHER_OVERFLOW, diag.dispatcher_queue_overflow);
            SendStatusMetric(parsed.cmd_code, CAN_STATUS_DROP_NOT_EXT, diag.dropped_not_ext);
            SendStatusMetric(parsed.cmd_code, CAN_STATUS_DROP_WRONG_DST, diag.dropped_wrong_dst);
            SendStatusMetric(parsed.cmd_code, CAN_STATUS_DROP_WRONG_TYPE, diag.dropped_wrong_type);
            SendStatusMetric(parsed.cmd_code, CAN_STATUS_DROP_WRONG_DLC, diag.dropped_wrong_dlc);
            SendStatusMetric(parsed.cmd_code, CAN_STATUS_TX_MAILBOX_TIMEOUT, diag.tx_mailbox_timeout);
            SendStatusMetric(parsed.cmd_code, CAN_STATUS_TX_HAL_ERROR, diag.tx_hal_error);
            SendStatusMetric(parsed.cmd_code, CAN_STATUS_ERROR_CALLBACK, diag.can_error_callback_count);
            SendStatusMetric(parsed.cmd_code, CAN_STATUS_ERROR_WARNING, diag.error_warning_count);
            SendStatusMetric(parsed.cmd_code, CAN_STATUS_ERROR_PASSIVE, diag.error_passive_count);
            SendStatusMetric(parsed.cmd_code, CAN_STATUS_BUS_OFF, diag.bus_off_count);
            SendStatusMetric(parsed.cmd_code, CAN_STATUS_LAST_HAL_ERROR, diag.last_hal_error);
            SendStatusMetric(parsed.cmd_code, CAN_STATUS_LAST_ESR, diag.last_esr);
            SendStatusMetric(parsed.cmd_code, CAN_STATUS_APP_QUEUE_OVERFLOW, diag.app_queue_overflow);

            CAN_SendDone(parsed.cmd_code, 0);
            break;
        }

        default:
            CAN_SendNackPublic(parsed.cmd_code, CAN_ERR_UNKNOWN_CMD);
            break;
        }
    }
}
