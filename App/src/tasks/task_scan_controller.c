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

void app_start_task_scan_controller(void *argument)
{
    (void)argument;

    uint8_t ssi_packet[SSI_PACKET_MAX_LEN];
    uint16_t ssi_len;

    ScanCtrl_Init();
    ScanUart_Init();

    // --- SCAN_ENABLE: однократно при старте ---
    ssi_len = Ssi_Encode(SSI_OP_SCAN_ENABLE, NULL, 0, ssi_packet);
    ScanUart_Send(ssi_packet, ssi_len);

    ScanResult_t cmd;

    for (;;) {
    	AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);

        // Ожидание команды из scan_queue (от dispatcher)
        if (osMessageQueueGet(scan_queueHandle, &cmd, NULL,  APP_WATCHDOG_TASK_IDLE_TIMEOUT_MS) != osOK) {
            continue;
        }

        if (cmd.cmd_code == CAN_CMD_SCAN_GET_DATA) {
            // --- SCAN_GET_DATA: вернуть последний shadow buffer ---
            uint8_t barcode[SHADOW_BUF_SIZE];
            uint8_t barcode_len = 0;
            if (ScanCtrl_GetLastBarcode(barcode, &barcode_len)) {
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

        // --- SCAN_REQUEST ---
        if (!ScanCtrl_StartScan(cmd.timeout_sec)) {
            CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_DEVICE_BUSY);
            continue;
        }

        // Отправка START_DECODE
        ssi_len = Ssi_Encode(SSI_OP_START_DECODE, NULL, 0, ssi_packet);
        ScanUart_Send(ssi_packet, ssi_len);

        // Ожидание UART RX с таймаутом
        uint32_t timeout_ms = (cmd.timeout_sec > 0)
                              ? (uint32_t)cmd.timeout_sec * 1000
                              : 4000;

        uint32_t elapsed = 0;
        uint32_t flags;

        while (elapsed < timeout_ms) {
            AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);    // «я жив»
            flags = osThreadFlagsWait(FLAG_SCAN_UART_RX, osFlagsWaitAny, 500);
            if ((flags & osFlagsError) == 0) {
                break;    // данные пришли — выходим
            }
            elapsed += 500;   // прошло ещё 500ms
        }

        if ((flags & osFlagsError) != 0) {
            // Таймаут
            ScanCtrl_HandleTimeout();
            CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_TIMEOUT);
        } else {
            // Данные получены
            uint16_t rx_len = 0;
            const uint8_t *rx_data = ScanUart_GetReceived(&rx_len);
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

        // STOP_DECODE
        ssi_len = Ssi_Encode(SSI_OP_STOP_DECODE, NULL, 0, ssi_packet);
        ScanUart_Send(ssi_packet, ssi_len);
    }
}
