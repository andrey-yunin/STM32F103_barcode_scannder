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
#include "task_watchdog.h"
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
static volatile bool tx_byte_done;
static bool tx_last_byte;

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
    tx_byte_done = true;
    if (tx_last_byte) HAL_GPIO_WritePin(CTS_GPIO_Port, CTS_Pin, GPIO_PIN_SET);
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

// --- Bounded task waits keep the watchdog alive; all times are milliseconds ---
static bool ScanUart_WaitRts(GPIO_PinState level, uint32_t timeout_ms)
{
    uint32_t started = HAL_GetTick();
    while (HAL_GPIO_ReadPin(RTS_GPIO_Port, RTS_Pin) != level) {
        if ((uint32_t)(HAL_GetTick() - started) >= timeout_ms) return false;
        AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
        if (osDelay(1U) != osOK) return false;
    }
    return true;
}

// --- Hardware wake: no unframed UART byte is sent in the CTS/RTS mode ---
bool ScanUart_Wake(void)
{
    HAL_GPIO_WritePin(AIMWAKE_GPIO_Port, AIMWAKE_Pin, GPIO_PIN_RESET);
    uint32_t ticks = (osKernelGetTickFreq() + 999U) / 1000U + 1U;
    bool ok = osDelay(ticks) == osOK;
    HAL_GPIO_WritePin(AIMWAKE_GPIO_Port, AIMWAKE_Pin, GPIO_PIN_SET);
    ticks = (osKernelGetTickFreq() + 99U) / 100U + 1U;
    return osDelay(ticks) == osOK && ok;
}

// --- One SSI transaction: request, grant per byte, completed TX, release ---
// One-byte DMA transfers retain the existing HAL completion diagnostics while
// checking decoder permission before every character. Only the last callback
// marks the whole START complete and releases CTS immediately after UART TC.
static bool ScanUart_SendInternal(const uint8_t *data, uint16_t len, bool track_start)
{
    if (data == NULL || len == 0U || len > 134U) return false;
    // The preceding transaction must have released its grant before a new one.
    if (!ScanUart_WaitRts(GPIO_PIN_SET, 2000U)) return false;
    HAL_GPIO_WritePin(CTS_GPIO_Port, CTS_Pin, GPIO_PIN_RESET);
    bool success = false;
    for (uint16_t i = 0; i < len; ++i) {
        // Bound character gaps below the default 200 ms intercharacter timeout.
        if (!ScanUart_WaitRts(GPIO_PIN_RESET, i == 0U ? 2000U : 100U)) break;
        uint32_t irq_state = __get_PRIMASK();
        __disable_irq();
        tx_byte_done = false;
        tx_last_byte = i + 1U == len;
        if (track_start && i == 0U) {
            uint32_t next = start_tx.sequence + 1U;
            if (next == 0U) next = 1U;
            memset(&start_tx, 0, sizeof(start_tx));
            start_tx.sequence = next;
            start_tx.start_tick = HAL_GetTick();
            start_tx.requested_len = len;
            start_tx.captured_len = len < SCAN_UART_TX_CAPTURE_SIZE ? len : SCAN_UART_TX_CAPTURE_SIZE;
            start_tx.baud = huart3.Init.BaudRate;
            memcpy(start_tx.bytes, data, start_tx.captured_len);
        }
        HAL_StatusTypeDef status = HAL_UART_Transmit_DMA(&huart3, (uint8_t *)&data[i], 1U);
        active_start_sequence = status == HAL_OK && track_start && tx_last_byte
                                ? start_tx.sequence : 0U;
        if (track_start) {
            start_tx.state = (uint32_t)status | ((uint32_t)huart3.gState << 16);
            start_tx.error_mask |= huart3.ErrorCode;
        }
        __set_PRIMASK(irq_state);
        if (status != HAL_OK) break;
        uint32_t started = HAL_GetTick();
        while (!tx_byte_done && (uint32_t)(HAL_GetTick() - started) < 100U) {
            AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
            if (osDelay(1U) != osOK) break;
        }
        if (!tx_byte_done) {
            // Stop DMA before returning ownership of the caller's packet buffer.
            HAL_UART_AbortTransmit(&huart3);
            active_start_sequence = 0U;
            break;
        }
        success = tx_last_byte;
    }
    HAL_GPIO_WritePin(CTS_GPIO_Port, CTS_Pin, GPIO_PIN_SET);
    if (!success) return false;
    return ScanUart_WaitRts(GPIO_PIN_SET, 2000U);
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
