/*
 * scan_uart_driver.h
 *
 *  Created on: Jul 13, 2026
 *      Author: andrey
 */

#ifndef INC_SCAN_UART_DRIVER_H_
#define INC_SCAN_UART_DRIVER_H_

#include <stdint.h>
#include <stdbool.h>

#define SCAN_UART_BUF_SIZE      128

bool ScanUart_Init(void);
bool ScanUart_StartRx(void);
bool ScanUart_Send(const uint8_t *data, uint16_t len);

const uint8_t* ScanUart_GetReceived(uint16_t *out_len);

#endif /* INC_SCAN_UART_DRIVER_H_ */
