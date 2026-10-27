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

// --- DMA_NORMAL receive buffer + domain holding buffer ---
static uint8_t dma_buffer[SCAN_UART_BUF_SIZE];
static uint8_t holding_buf[SCAN_UART_BUF_SIZE];
static volatile uint16_t holding_len = 0;
static ScanUartCapture_t capture;
static volatile bool capture_active;
static uint16_t rx_prefix;

// --- Capture ownership changes only in scan-task context with IRQs masked ---
void ScanUart_BeginCapture(void)
{
    uint32_t irq_state = __get_PRIMASK();
    __disable_irq();
    memset(&capture, 0, sizeof(capture));
    capture_active = true;
    __set_PRIMASK(irq_state);
}

void ScanUart_EndCapture(ScanUartCapture_t *out)
{
    uint32_t irq_state = __get_PRIMASK();
    __disable_irq();
    capture_active = false;
    if (out != NULL) *out = capture;
    __set_PRIMASK(irq_state);
}

// --- HT and subsequent IDLE/TC share a prefix; retain each byte only once ---
static void ScanUart_CaptureRx(uint16_t size, bool half_transfer)
{
    uint16_t offset = size >= rx_prefix ? rx_prefix : 0U;
    if (capture_active) {
        uint16_t count = size - offset;
        uint16_t room = SCAN_UART_CAPTURE_SIZE - capture.length;
        if (count > room) { count = room; capture.overflow = true; }
        memcpy(capture.bytes + capture.length, dma_buffer + offset, count);
        capture.length += count;
        capture.errors |= huart3.ErrorCode;
    }
    rx_prefix = half_transfer ? size : 0U;
}

// --- Diagnostic storage is separate from the SSI/domain holding buffer ---
static ScanUartDiagnostics_t rx_diag;
static uint8_t rx_arm_status = 0xFFU;
static uint8_t rx_event_type = 0xFFU;

// --- One retained START attempt and ownership of the in-flight TX completion ---
static ScanUartTxDiagnostics_t start_tx = {.state = 0xFFU};
static uint32_t active_start_sequence;

// --- Read-only snapshot; callbacks cannot change the copy while IRQs are masked ---
void ScanUart_GetTxDiagnostics(ScanUartTxDiagnostics_t *out)
{
    if (out == NULL) return;
    uint32_t irq_state = __get_PRIMASK();
    __disable_irq();
    *out = start_tx;
    __set_PRIMASK(irq_state);
}

// --- HAL completion belongs only to a successfully accepted tracked transfer ---
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART3) return;
    if (active_start_sequence != 0U && active_start_sequence == start_tx.sequence) {
        start_tx.complete_tick = HAL_GetTick();
        start_tx.state |= 0x100U;
        start_tx.error_mask |= huart->ErrorCode;
    }
    active_start_sequence = 0U;
}

// --- Read-only task snapshot: DMA never writes rx_diag ---
void ScanUart_GetDiagnostics(ScanUartDiagnostics_t *out)
{
    if (out == NULL) return;
    uint32_t irq_state = __get_PRIMASK();
    __disable_irq();
    *out = rx_diag;
    out->baud = huart3.Init.BaudRate;
    out->state = (uint32_t)rx_arm_status | ((uint32_t)huart3.RxState << 8) |
                 ((uint32_t)rx_event_type << 16);
    __set_PRIMASK(irq_state);
}

// --- Capture the stable prefix reported by HAL, including half-transfer ---
// DMA_NORMAL: HT prefix remains stable while DMA writes its second half.
// Do not interpret this prefix as a validated SSI packet.
static void ScanUart_RecordRx(uint16_t size)
{
    ++rx_diag.sequence;
    if (rx_diag.sequence == 0U) ++rx_diag.sequence;
    rx_diag.tick_ms = HAL_GetTick();
    rx_diag.length = size;
    rx_event_type = (uint8_t)HAL_UARTEx_GetRxEventType(&huart3);
    memset(rx_diag.bytes, 0, sizeof(rx_diag.bytes));
    memcpy(rx_diag.bytes, dma_buffer, size);
    rx_diag.error_mask |= huart3.ErrorCode;
}

// --- Preserve UART errors; this observer does not change recovery policy ---
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART3) return;
    rx_diag.error_mask |= huart->ErrorCode;
    if (capture_active) capture.errors |= huart->ErrorCode;
    if (active_start_sequence != 0U && active_start_sequence == start_tx.sequence)
        start_tx.error_mask |= huart->ErrorCode;
    if (rx_diag.error_count != UINT32_MAX) ++rx_diag.error_count;
}

bool ScanUart_Init(void)
{
    holding_len = 0;
    return ScanUart_StartRx();
}

bool ScanUart_StartRx(void)
{
    // --- Record the actual HAL result, including HAL_BUSY at a HT event ---
    uint32_t irq_state = __get_PRIMASK();
    __disable_irq();
    HAL_StatusTypeDef status = HAL_UARTEx_ReceiveToIdle_DMA(&huart3, dma_buffer, SCAN_UART_BUF_SIZE);
    rx_arm_status = (uint8_t)status;
    __set_PRIMASK(irq_state);
    return status == HAL_OK;
}

// --- SE965HP UART-only wake (IG Table 1-2 and wake timing Table 6-1) ---
// This is a raw byte, not an SSI packet. Blocking TX is bounded and returns
// after TC, so the stack byte is no longer referenced when this function exits.
// Called only by the scan task; RX DMA remains enabled throughout.
bool ScanUart_Wake(void)
{
    uint8_t wake = 0x00U;
    if (HAL_UART_Transmit(&huart3, &wake, 1U, 20U) != HAL_OK) return false;

    // Spec: up to 8 ms to full operation. Allow 10 ms after completed TX.
    // Extra tick covers calling osDelay just before a scheduler tick boundary.
    uint32_t ticks = (osKernelGetTickFreq() + 99U) / 100U + 1U;
    return osDelay(ticks) == osOK;
}

// --- Submit TX with IRQ-safe association between HAL acceptance and completion ---
// HAL_OK means DMA submission; only TxCpltCallback marks completion. RX errors
// may also reach ErrorCallback, so their mask is not labelled as a TX-only error.
static bool ScanUart_SendInternal(const uint8_t *data, uint16_t len, bool track_start)
{
    if (data == NULL || len == 0U) return false;
    uint32_t irq_state = __get_PRIMASK();
    __disable_irq();
    if (track_start) {
        uint32_t next = start_tx.sequence + 1U;
        if (next == 0U) next = 1U;
        memset(&start_tx, 0, sizeof(start_tx));
        start_tx.sequence = next;
        start_tx.start_tick = HAL_GetTick();
        start_tx.state = 0xFFU;
        start_tx.requested_len = len;
        start_tx.captured_len = len < SCAN_UART_TX_CAPTURE_SIZE ? len : SCAN_UART_TX_CAPTURE_SIZE;
        start_tx.baud = huart3.Init.BaudRate;
        memcpy(start_tx.bytes, data, start_tx.captured_len);
    }

    // --- Retain an earlier active owner when a later submission returns BUSY ---
    HAL_StatusTypeDef status = HAL_UART_Transmit_DMA(&huart3, (uint8_t *)data, len);
    if (status == HAL_OK)
        active_start_sequence = track_start ? start_tx.sequence : 0U;
    if (track_start) {
        start_tx.state = (uint32_t)status | ((uint32_t)huart3.gState << 16);
        start_tx.error_mask |= huart3.ErrorCode;
    }
    __set_PRIMASK(irq_state);
    return status == HAL_OK;
}

// --- Normal commands never replace the retained START snapshot ---
bool ScanUart_Send(const uint8_t *data, uint16_t len)
{
    return ScanUart_SendInternal(data, len, false);
}

// --- Explicit domain hook avoids guessing START from arbitrary UART bytes ---
bool ScanUart_SendStart(const uint8_t *data, uint16_t len)
{
    return ScanUart_SendInternal(data, len, true);
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

    // --- Save raw evidence before domain processing or DMA re-arm ---
    ScanUart_RecordRx(Size);
    ScanUart_CaptureRx(Size, rx_event_type == 1U);
    memcpy((void *)holding_buf, dma_buffer, Size);
    holding_len = Size;

    (void)ScanUart_StartRx();

    osThreadFlagsSet(task_scan_contrHandle, FLAG_SCAN_UART_RX);
}
