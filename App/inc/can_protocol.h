/*
 * can_protocol.h
 *
 *  Created on: Jul 10, 2026
 *      Author: andrey
 */

#ifndef CAN_PROTOCOL_H_
#define CAN_PROTOCOL_H_

#include <stdint.h>
#include <stdbool.h>

// --- Фиксированная длина CAN-кадра DDS-240 ---
#define CAN_FRAME_DLC 8U

// --- Полезные данные DATA-кадра и признак последнего фрагмента ---
#define CAN_DATA_PAYLOAD_MAX   6U
#define CAN_DATA_SEQ_EOT_MASK  0x80U

// --- Приоритеты (биты 28-26 Extended CAN ID) ---
// HIGH — команды Дирижёра, NORMAL — ответы исполнителя.
#define CAN_PRIORITY_HIGH    0
#define CAN_PRIORITY_NORMAL  1

// --- Типы сообщений (биты 25-24 Extended CAN ID) ---
#define CAN_MSG_TYPE_COMMAND        0
#define CAN_MSG_TYPE_ACK            1
#define CAN_MSG_TYPE_NACK           2
#define CAN_MSG_TYPE_DATA_DONE_LOG  3

// --- Подтипы для MSG_TYPE_DATA_DONE_LOG (байт 0 payload) ---
#define CAN_SUB_TYPE_DONE       0x01
#define CAN_SUB_TYPE_DATA       0x02
#define CAN_SUB_TYPE_LOG        0x03

// --- Адреса узлов (биты 23-16 dst, 15-8 src) ---
#define CAN_ADDR_BROADCAST      0x00
#define CAN_ADDR_CONDUCTOR      0x10
#define CAN_ADDR_SCANNER_BOARD  0x60

#define CAN_ADDR_SCANNER_BOARD_BASE  0x60
#define CAN_ADDR_SCANNER_BOARD_LAST  0x6F

#define CAN_DYNAMIC_NODE_ID_MIN     0x02
#define CAN_DYNAMIC_NODE_ID_MAX     0x7F

// --- Тип устройства для команды INFO ---
#define CAN_DEVICE_TYPE_SCANNER 0x05

// --- Команды сканера (байты 0-1 payload, LE) ---
// SCAN_REQUEST — START/STOP лазерного сканирования через SSI.
// SCAN_GET_DATA — возвращает последний раскодированный штрихкод.
#define CAN_CMD_SCAN_REQUEST            0x0601
#define CAN_CMD_SCAN_GET_DATA           0x0602
// Read-only SSI revision probe, fixed 3-second raw UART capture.
#define CAN_CMD_SCAN_BEEP 0x0604U // One short high beep; 3 s raw RX capture. No parameters.
#define CAN_CMD_SCAN_GET_REVISION                  0x0603

// --- Сервисные команды (0xF001..0xF007) ---
#define CAN_CMD_SRV_GET_DEVICE_INFO     0xF001
#define CAN_CMD_SRV_REBOOT              0xF002
#define CAN_CMD_SRV_FLASH_COMMIT        0xF003
#define CAN_CMD_SRV_GET_UID             0xF004
#define CAN_CMD_SRV_SET_NODE_ID         0xF005
#define CAN_CMD_SRV_FACTORY_RESET       0xF006
#define CAN_CMD_SRV_GET_STATUS          0xF007

// --- Optional Scanner UART diagnostics: existing F020/F021 envelope ---
#define CAN_CMD_SRV_GET_DIAG_CAPS       0xF020U
#define CAN_CMD_SRV_GET_DIAG_BLOCK      0xF021U
#define CAN_DIAG_BLOCK_SCANNER_UART_RX  0xD501U
#define CAN_DIAG_SCANNER_UART_SCHEMA    1U
#define CAN_DIAG_BLOCK_SCANNER_UART_TX_START          0xD502U
#define CAN_DIAG_SCANNER_UART_TX_SCHEMA               1U

#define SRV_MAGIC_REBOOT                0x55AA
#define SRV_MAGIC_FACTORY_RESET         0xDEAD

// --- Макросы сборки/разбора 29-bit Extended CAN ID ---
#define CAN_BUILD_ID(priority, msg_type, dst_addr, src_addr) \
    ((uint32_t)(((priority) & 0x07) << 26) | \
                 (((msg_type) & 0x03) << 24) | \
                 (((dst_addr) & 0xFF) << 16) | \
                 (((src_addr) & 0xFF) << 8))

#define CAN_GET_PRIORITY(id)    ((uint8_t)(((id) >> 26) & 0x07))
#define CAN_GET_MSG_TYPE(id)    ((uint8_t)(((id) >> 24) & 0x03))
#define CAN_GET_DST_ADDR(id)    ((uint8_t)(((id) >> 16) & 0xFF))
#define CAN_GET_SRC_ADDR(id)    ((uint8_t)(((id) >> 8)  & 0xFF))

// --- Коды ошибок NACK ---
// Common: одинаковые для всех исполнителей.
#define CAN_ERR_NONE                0x0000
#define CAN_ERR_UNKNOWN_CMD         0x0001
#define CAN_ERR_INVALID_DEVICE_ID   0x0002
#define CAN_ERR_DEVICE_BUSY         0x0003
#define CAN_ERR_INVALID_KEY         0x0004
#define CAN_ERR_FLASH_WRITE         0x0005
#define CAN_ERR_INVALID_PARAM       0x0006

// Scanner domain: уникальные коды 0xE600..0xE603.
#define CAN_ERR_SCAN_NO_DATA        0xE600
#define CAN_ERR_SCAN_HARDWARE       0xE601
#define CAN_ERR_SCAN_COMM           0xE602
#define CAN_ERR_SCAN_TIMEOUT        0xE603

// --- Метрики GET_STATUS (0xF007): metric_id:uint16 LE + value:uint32 LE ---
#define CAN_STATUS_RX_TOTAL             0x0001
#define CAN_STATUS_TX_TOTAL             0x0002
#define CAN_STATUS_RX_QUEUE_OVERFLOW    0x0003
#define CAN_STATUS_TX_QUEUE_OVERFLOW    0x0004
#define CAN_STATUS_DISPATCHER_OVERFLOW  0x0005
#define CAN_STATUS_DROP_NOT_EXT         0x0006
#define CAN_STATUS_DROP_WRONG_DST       0x0007
#define CAN_STATUS_DROP_WRONG_TYPE      0x0008
#define CAN_STATUS_DROP_WRONG_DLC       0x0009
#define CAN_STATUS_TX_MAILBOX_TIMEOUT   0x000A
#define CAN_STATUS_TX_HAL_ERROR         0x000B
#define CAN_STATUS_ERROR_CALLBACK       0x000C
#define CAN_STATUS_ERROR_WARNING        0x000D
#define CAN_STATUS_ERROR_PASSIVE        0x000E
#define CAN_STATUS_BUS_OFF              0x000F
#define CAN_STATUS_LAST_HAL_ERROR       0x0010
#define CAN_STATUS_LAST_ESR             0x0011
#define CAN_STATUS_APP_QUEUE_OVERFLOW   0x0012

// --- Структура диагностики CAN ---
typedef struct {
	uint32_t rx_total;
	uint32_t tx_total;
	uint32_t rx_queue_overflow;
	uint32_t tx_queue_overflow;
	uint32_t dispatcher_queue_overflow;
	uint32_t app_queue_overflow;
	uint32_t dropped_not_ext;
	uint32_t dropped_wrong_dst;
	uint32_t dropped_wrong_type;
	uint32_t dropped_wrong_dlc;
	uint32_t tx_mailbox_timeout;
	uint32_t tx_hal_error;
	uint32_t can_error_callback_count;
	uint32_t error_warning_count;
	uint32_t error_passive_count;
	uint32_t bus_off_count;
	uint32_t last_hal_error;
	uint32_t last_esr;
} CanDiagnostics_t;

// --- Прототипы функций отправки CAN-ответов ---
// Реализованы в task_can_handler.c.
void CAN_SendAck(uint16_t cmd_code);
void CAN_SendNackPublic(uint16_t cmd_code, uint16_t error_code);
void CAN_SendDone(uint16_t cmd_code, uint8_t device_id);
void CAN_SendData(uint16_t cmd_code, uint8_t sequence_info, const uint8_t *data,
		uint8_t len);
void CAN_SendDataFragmented(uint16_t cmd_code, const uint8_t *data,
		uint8_t total_len);

// --- Checked task-context diagnostic response; no DONE after enqueue failure ---
// True confirms queue acceptance only, not physical CAN delivery.
bool CAN_SendDiagnosticResponse(uint16_t cmd_code, const uint8_t *data,
		uint16_t len);

// --- Прототипы диагностики CAN ---
void CAN_Diagnostics_GetSnapshot(CanDiagnostics_t *out);
void CAN_Diagnostics_RecordRxQueueOverflow(void);
void CAN_Diagnostics_RecordAppQueueOverflow(void);
void CAN_Diagnostics_RecordCanError(uint32_t hal_error, uint32_t esr);

#endif /* INC_CAN_PROTOCOL_H_ */
