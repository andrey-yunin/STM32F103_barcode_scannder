/*
 * scan_uart_driver.c
 *
 *  Created on: Jul 13, 2026
 *      Author: andrey
 */


#include "scan_uart_driver.h"
#include "main.h"
#include "app_queues.h"
#include "app_config.h"
#include <string.h>
#include <cmsis_os.h>

extern UART_HandleTypeDef huart3;

// --- DMA circular buffer + holding buffer ---
static uint8_t dma_buffer[SCAN_UART_BUF_SIZE];
static uint8_t holding_buf[SCAN_UART_BUF_SIZE];
static volatile uint16_t holding_len = 0;

bool ScanUart_Init(void)
{
    holding_len = 0;
    return ScanUart_StartRx();
}

bool ScanUart_StartRx(void)
{
    if (HAL_UARTEx_ReceiveToIdle_DMA(&huart3, dma_buffer, SCAN_UART_BUF_SIZE) != HAL_OK) {
        return false;
    }
    return true;
}

bool ScanUart_Send(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0) return false;
    return (HAL_UART_Transmit_DMA(&huart3, (uint8_t *)data, len) == HAL_OK);
}

const uint8_t* ScanUart_GetReceived(uint16_t *out_len)
{
    if (out_len == NULL) return NULL;
    *out_len = holding_len;
    return (holding_len > 0) ? holding_buf : NULL;
}

// --- IDLE callback (ISR контекст) ---
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance != USART3) return;
    if (Size == 0 || Size > SCAN_UART_BUF_SIZE) return;

    memcpy((void *)holding_buf, dma_buffer, Size);
    holding_len = Size;

    HAL_UARTEx_ReceiveToIdle_DMA(&huart3, dma_buffer, SCAN_UART_BUF_SIZE);

    osThreadFlagsSet(task_scan_contrHandle, FLAG_SCAN_UART_RX);
}
