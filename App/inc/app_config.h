#ifndef APP_CONFIG_H_
#define APP_CONFIG_H_

#include "main.h"

#define FW_REV_MAJOR                0x01
#define FW_REV_MINOR                0x00
#define FW_REV_PATCH                0x00

#define CAN_NODE_ID                 0x60
#define CAN_DEVICE_TYPE_SCANNER     0x05
#define CAN_DATA_MAX_LEN            8

#define CAN_RX_QUEUE_LEN            16
#define CAN_TX_QUEUE_LEN            16
#define DISPATCHER_QUEUE_LEN        8
#define SCAN_QUEUE_LEN              4

#define FLAG_CAN_RX                 0x01
#define FLAG_CAN_TX                 0x02

typedef struct {
    CAN_RxHeaderTypeDef header;
    uint8_t data[CAN_DATA_MAX_LEN];
} CanRxFrame_t;

typedef struct {
    CAN_TxHeaderTypeDef header;
    uint8_t data[CAN_DATA_MAX_LEN];
} CanTxFrame_t;

typedef struct {
    uint16_t cmd_code;
    uint8_t  device_id;
    uint8_t  data[5];
    uint8_t  data_len;
} ParsedCanCommand_t;

#define FLAG_SCAN_UART_RX   0x04

typedef struct {
    uint16_t cmd_code;      // 0x0601 (SCAN_REQUEST) or 0x0602 (SCAN_GET_DATA)
    uint16_t timeout_sec;   // timeout in seconds (only for SCAN_REQUEST)
    uint8_t  barcode[128];  // result barcode
    uint8_t  barcode_len;
} ScanResult_t;

extern CAN_HandleTypeDef hcan;

#endif
