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

// --- Накопитель непрочитанных байтов обмена Scanner ---
// ISR добавляет байты, задача забирает их и освобождает место.
// 128B выбраны с запасом для рассчитанного цикла с 17-байтовой
// строкой без prefix/suffix: 36B штатно, 84B с двумя повторами DATA.
#define SCAN_UART_PENDING_SIZE  128U

// --- Board-specific raw RX snapshot, captured before SSI decoding ---
// Last HAL RX completion: one byte in interrupt mode, not a complete scan or log.
typedef struct {
	uint32_t sequence;
	uint32_t tick_ms;
	uint32_t baud;
	uint32_t state;      // byte0 RX-arm status; byte1 RxState; byte2 event type
	uint32_t error_mask;  // OR of UART errors observed since boot
	uint32_t error_count;
	uint32_t length;
	uint32_t reserved;
	uint8_t bytes[SCAN_UART_BUF_SIZE];
} ScanUartDiagnostics_t;

// Copies one coherent snapshot in task context; does not restart/clear RX.
void ScanUart_GetDiagnostics(ScanUartDiagnostics_t *out);

// --- Отмена RX при подготовке reset ---
// Только из задачи-владельца, после установки reset-latch.
// Возвращает результат остановки; диагностические снимки сохраняются.
bool ScanUart_CancelRx(void);

bool ScanUart_Init(void);
bool ScanUart_StartRx(void);
// Task-only hardware AIM/WAKE low pulse, then >=10 ms settling.
// False means no following SSI command may be submitted.
bool ScanUart_Wake(void);
// Task-only synchronous SSI handshake; UART IT sends each byte after RTS grant.
// CTS is released on every exit; false includes handshake/transport failure.
bool ScanUart_Send(const uint8_t *data, uint16_t len);

// --- Normal task-owned exchange with a fresh RX boundary ---
// BeginRead must already own RX. Old incoming bytes are drained until the
// final transmitted byte completes, before CTS is released for the new reply.
// RX errors remain latched; reset gates are identical to ordinary Send.
bool ScanUart_SendForReply(const uint8_t *data, uint16_t len);

// --- START_DECODE evidence is retained independently of later STOP/ENABLE ---
#define SCAN_UART_TX_CAPTURE_SIZE 16U
typedef struct {
	uint32_t sequence, start_tick, complete_tick, state;
	uint32_t error_mask, requested_len, captured_len, baud;
	uint8_t bytes[SCAN_UART_TX_CAPTURE_SIZE];
} ScanUartTxDiagnostics_t;
// Tracks the actual HAL call, not merely generation of a command buffer.
bool ScanUart_SendStart(const uint8_t *data, uint16_t len);

// --- Task-only STOP после latch, без AIM/WAKE ---
// Принимает только точный шестибайтовый SSI STOP; D502 не меняется.
// true означает завершение TX/handshake; ACK ещё должна проверить задача.
bool ScanUart_SendStopForReset(const uint8_t *data, uint16_t len);

// Task-only coherent copy; no new UART transmission or state reset.
void ScanUart_GetTxDiagnostics(ScanUartTxDiagnostics_t *out);

// --- Bounded raw capture for a read-only SSI probe; independent of snapshots ---
#define SCAN_UART_CAPTURE_SIZE 192U
typedef struct {
	uint16_t length;
	bool overflow;
	uint32_t errors;
	uint8_t bytes[SCAN_UART_CAPTURE_SIZE];
} ScanUartCapture_t;
void ScanUart_BeginCapture(void);
void ScanUart_EndCapture(ScanUartCapture_t *out);

// --- Границы накопления ответа; вызывает только задача Scanner ---
// BeginRead удаляет старые данные и уведомление перед wake/START.
// Возвращает false, если открыть приём ответа невозможно.
bool ScanUart_BeginRead(void);

// Закрывает накопление, сохраняя диагностику и независимый raw capture.
// Возвращает false, если при приёме текущего ответа произошла ошибка.
bool ScanUart_EndRead(void);

// --- Передача накопленных байтов задаче ---
// Возвращает устойчивый снимок и освобождает накопитель для новых байтов.
// Снимок действителен до следующего вызова; ISR его не изменяет.
// out_error сообщает ошибку текущего приёма, даже при отсутствии байтов.
const uint8_t* ScanUart_GetReceived(uint16_t *out_len, bool *out_error);

#endif /* INC_SCAN_UART_DRIVER_H_ */
