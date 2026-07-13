/*
 * scan_controller.h
 *
 *  Created on: Jul 13, 2026
 *      Author: andrey
 */

#ifndef INC_SCAN_CONTROLLER_H_
#define INC_SCAN_CONTROLLER_H_

#include <stdint.h>
#include <stdbool.h>

#define SHADOW_BUF_SIZE         128

// Состояния КА Scan Controller
typedef enum {
    SCAN_STATE_IDLE = 0,
    SCAN_STATE_SCANNING,
    SCAN_STATE_DATA_READY,
    SCAN_STATE_ERROR
} ScanState_t;

typedef struct {
    uint32_t ssi_decode_errors;
    uint32_t uart_overrun;
    uint32_t scan_timeouts;
    uint32_t scan_success;
} ScanStats_t;

void ScanCtrl_Init(void);
bool ScanCtrl_StartScan(uint16_t timeout_sec);
bool ScanCtrl_ProcessReceived(const uint8_t *data, uint16_t len);
bool ScanCtrl_GetLastBarcode(uint8_t *out_buf, uint8_t *out_len);
bool ScanCtrl_HandleTimeout(void);
ScanState_t ScanCtrl_GetState(void);
void ScanCtrl_GetStats(ScanStats_t *out);


#endif /* INC_SCAN_CONTROLLER_H_ */
