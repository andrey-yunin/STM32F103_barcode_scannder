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
    // Инициализация USART3 уже выполнена в MX_USART3_UART_Init()
    holding_len = 0;
    return ScanUart_StartRx();
}

bool ScanUart_StartRx(void)
{
    // Запуск DMA single-shot: приём до IDLE, buf_size лимит
    if (HAL_UARTEx_ReceiveToIdle_DMA(&huart3, dma_buffer, SCAN_UART_BUF_SIZE) != HAL_OK) {
        return false;
    }
    // Отключаем прерывание DMA half-transfer — не нужно для IDLE
    __HAL_DMA_DISABLE_IT(huart3.hdmarx, DMA_IT_HT);
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
void HAL_UARTEx_RxToIdleCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART3) return;

    uint16_t received = SCAN_UART_BUF_SIZE - __HAL_DMA_GET_COUNTER(huart->hdmarx);
    if (received == 0 || received > SCAN_UART_BUF_SIZE) return;

    // Копирование в holding buffer (ISR, минимальное время)
    memcpy((void *)holding_buf, dma_buffer, received);
    holding_len = received;

    // Перезапуск DMA
    HAL_UARTEx_ReceiveToIdle_DMA(&huart3, dma_buffer, SCAN_UART_BUF_SIZE);
    __HAL_DMA_DISABLE_IT(huart3.hdmarx, DMA_IT_HT);

    // Уведомление задачи scan_controller
    osThreadFlagsSet(task_scan_contrHandle, FLAG_SCAN_UART_RX);
}
