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
#include "scan_uart_driver.h"


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


// --- Encode one standard diagnostic record (item:uint16, value:uint32 LE) ---
static void PackDiagnostic(uint8_t *out, uint16_t item, uint32_t value)
{
    out[0] = (uint8_t)item;
    out[1] = (uint8_t)(item >> 8);
    for (uint8_t i = 0; i < 4U; ++i) out[2U + i] = (uint8_t)(value >> (8U * i));
}

// --- F020/F021 are read-only snapshots; ACK is sent by the common dispatcher ---
static bool HandleEngineeringDiagnostics(const ParsedCanCommand_t *request)
{
    uint16_t cmd = request->cmd_code;
    if (cmd != CAN_CMD_SRV_GET_DIAG_CAPS && cmd != CAN_CMD_SRV_GET_DIAG_BLOCK) return false;
    if (request->data_len != 5U) {
        CAN_SendNackPublic(cmd, CAN_ERR_INVALID_PARAM);
        return true;
    }

    // --- Discovery publishes only the schema implemented by this firmware ---
    if (cmd == CAN_CMD_SRV_GET_DIAG_CAPS) {
        if (request->device_id || request->data[0] || request->data[1] ||
            request->data[2] || request->data[3] || request->data[4]) {
            CAN_SendNackPublic(cmd, CAN_ERR_INVALID_PARAM);
            return true;
        }
        const uint8_t caps[12] = {
            (uint8_t)CAN_DIAG_BLOCK_SCANNER_UART_RX,
            (uint8_t)(CAN_DIAG_BLOCK_SCANNER_UART_RX >> 8),
            CAN_DIAG_SCANNER_UART_SCHEMA, 0x83U, 0, 0,
            (uint8_t)CAN_DIAG_BLOCK_SCANNER_UART_TX_START,
            (uint8_t)(CAN_DIAG_BLOCK_SCANNER_UART_TX_START >> 8),
            CAN_DIAG_SCANNER_UART_TX_SCHEMA, 0x83U, 0, 0
        };
        if (!CAN_SendDiagnosticResponse(cmd, caps, sizeof(caps)))
            CAN_SendNackPublic(cmd, CAN_ERR_DEVICE_BUSY);
        return true;
    }

    // --- Selector belongs to Scanner; logical UART index 0 maps to USART3 ---
    uint16_t block = request->device_id | ((uint16_t)request->data[0] << 8);
    if ((block != CAN_DIAG_BLOCK_SCANNER_UART_RX &&
         block != CAN_DIAG_BLOCK_SCANNER_UART_TX_START) || request->data[2] ||
        request->data[3] || request->data[4]) {
        CAN_SendNackPublic(cmd, CAN_ERR_INVALID_PARAM);
        return true;
    }
    if (request->data[1] != 0U) {
        CAN_SendNackPublic(cmd, CAN_ERR_INVALID_DEVICE_ID);
        return true;
    }

    // --- One coherent copy; ISR never sends CAN and cannot change this payload ---
    // Dispatcher is the only caller; static buffers avoid enlarging its task stack.
    static ScanUartDiagnostics_t snapshot;
    static uint8_t payload[40U * 6U];
    // --- START TX has its own block; later STOP cannot overwrite its evidence ---
    if (block == CAN_DIAG_BLOCK_SCANNER_UART_TX_START) {
        ScanUartTxDiagnostics_t tx;
        ScanUart_GetTxDiagnostics(&tx);
        const uint32_t values[8] = {tx.sequence, tx.start_tick, tx.complete_tick,
            tx.state, tx.error_mask, tx.requested_len, tx.captured_len, tx.baud};
        for (uint8_t i = 0; i < 8U; ++i)
            PackDiagnostic(&payload[i * 6U], i + 1U, values[i]);
        for (uint8_t i = 0; i < 4U; ++i) {
            uint32_t word = 0;
            for (uint8_t b = 0; b < 4U; ++b)
                word |= (uint32_t)tx.bytes[i * 4U + b] << (8U * b);
            PackDiagnostic(&payload[(i + 8U) * 6U], i + 9U, word);
        }
        if (!CAN_SendDiagnosticResponse(cmd, payload, 12U * 6U))
            CAN_SendNackPublic(cmd, CAN_ERR_DEVICE_BUSY);
        return true;
    }
    ScanUart_GetDiagnostics(&snapshot);
    const uint32_t values[8] = {
        snapshot.sequence, snapshot.tick_ms, snapshot.baud, snapshot.state,
        snapshot.error_mask, snapshot.error_count, snapshot.length, snapshot.reserved
    };
    for (uint8_t i = 0; i < 8U; ++i)
        PackDiagnostic(&payload[i * 6U], i + 1U, values[i]);
    for (uint8_t i = 0; i < 32U; ++i) {
        uint32_t word = 0;
        for (uint8_t b = 0; b < 4U; ++b)
            word |= (uint32_t)snapshot.bytes[i * 4U + b] << (8U * b);
        PackDiagnostic(&payload[(i + 8U) * 6U], i + 9U, word);
    }
    if (!CAN_SendDiagnosticResponse(cmd, payload, sizeof(payload)))
        CAN_SendNackPublic(cmd, CAN_ERR_DEVICE_BUSY);
    return true;
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

        if (HandleEngineeringDiagnostics(&parsed)) continue;

        // 3. Маршрутизация
        switch (parsed.cmd_code) {

        // ============================================================
        // ДОМЕННЫЕ КОМАНДЫ 0x06xx — форвард в scan_queue
        // ============================================================
        case CAN_CMD_SCAN_REQUEST:
        case CAN_CMD_SCAN_GET_REVISION:
        case CAN_CMD_SCAN_BEEP:
        case CAN_CMD_SCAN_GET_DATA: {
            if ((parsed.cmd_code == CAN_CMD_SCAN_GET_REVISION || parsed.cmd_code == CAN_CMD_SCAN_BEEP) &&
                (parsed.device_id || parsed.data[0] || parsed.data[1] ||
                 parsed.data[2] || parsed.data[3] || parsed.data[4])) {
                CAN_SendNackPublic(parsed.cmd_code, 0x0006);
                break;
            }
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
            uint8_t uid[12];
            uint8_t buf[16];
            AppConfig_GetMCU_UID(uid);
            buf[0] = CAN_DEVICE_TYPE_SCANNER;
            buf[1] = FW_REV_MAJOR;
            buf[2] = FW_REV_MINOR;
            buf[3] = 1;
            memcpy(&buf[4], uid, 12);
            CAN_SendDataFragmented(parsed.cmd_code, buf, 16);
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
            // --- SET_NODE_ID: канон экосистемы ---
            // new_id из байта 2 payload (device_id); RAM-only; NACK INVALID_PARAM.
            // ACK уже отправлен со старого адреса (в начале цикла).
            if (parsed.device_id >= CAN_DYNAMIC_NODE_ID_MIN &&
                parsed.device_id <= CAN_DYNAMIC_NODE_ID_MAX &&
                parsed.device_id != CAN_ADDR_CONDUCTOR) {
                AppConfig_SetPerformerID(parsed.device_id);
                CAN_SendDone(parsed.cmd_code, parsed.device_id);
            } else {
                CAN_SendNackPublic(parsed.cmd_code, CAN_ERR_INVALID_PARAM);
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
