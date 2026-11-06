# Intermediate UART RX boundary block — 06.11.2026

PROPOSED, not installed. Eight exact replacements in scan_uart_driver.h/.c.
This candidate uses the actual installed codec and task/controller. It does
not wire STOP/FLUSH or reading recovery, and introduces no new task deadline,
host retry count, CAN code or diagnostic schema.

Requirement: the agreed STOP/FLUSH preparation needs its first new response
separated from previous RX before CTS is released. Domain source SSI72E-40451-03
§1-4/1-5 states host response priority before postponed decoder data. Common
normal reset gates and single Scanner task ownership remain in force.

Exact local reference: installed ScanUart_SendStopForReset and
HAL_UART_TxCpltCallback in ea1eacb + reset_connected_20261103 worktree.
Adaptations: a separate ordinary reply_boundary_pending flag; BeginRead is
required; old bytes are drained through final TC while UART errors stay
latched; ordinary reset gate remains mandatory. Reset-only STOP retains its
existing exclusive path. Init/CancelRx/reset STOP clear the new flag.
SendForReply true means UART/handshake completion only; the task must still
parse/confirm the response. D501 still records raw bytes; D502 is preserved.

HOST intermediate candidate: boundary cases including six reset positions
PASS, prior shutdown18 groups PASS, prior reading13 scenarios PASS, ASan/UBSan.
ARM GCC13.3.1 Wall/Wextra/Werror driver compilation PASS. No full Debug link,
Release or BENCH. Manual replacements exactly reproduce the checked candidate.
Project firmware hashes remain unchanged.

## 1. App/inc/scan_uart_driver.h

Location: UART normal API after ScanUart_Send

Replace:

```c
// --- START_DECODE evidence is retained independently of later STOP/ENABLE ---
```

With:

```c
// --- Normal task-owned exchange with a fresh RX boundary ---
// BeginRead must already own RX. Old incoming bytes are drained until the
// final transmitted byte completes, before CTS is released for the new reply.
// RX errors remain latched; reset gates are identical to ordinary Send.
bool ScanUart_SendForReply(const uint8_t *data, uint16_t len);

// --- START_DECODE evidence is retained independently of later STOP/ENABLE ---
```

## 2. App/src/scan_uart_driver.c

Location: RX ownership flags

Replace:

```c
static volatile bool reset_stop_active;
```

With:

```c
static volatile bool reset_stop_active;

// --- Boundary of a normal task-owned control/RESEND exchange ---
// Does not bypass reset latch. Only TxCplt of the last byte opens new data.
static volatile bool reply_boundary_pending;
```

## 3. App/src/scan_uart_driver.c

Location: normal RX boundary in final-byte TxCplt before CTS HIGH

Replace:

```c
			read_active = true;
		}

		HAL_GPIO_WritePin(CTS_GPIO_Port, CTS_Pin, GPIO_PIN_SET);
```

With:

```c
			read_active = true;
		}

		// --- New normal reply starts before decoder TX is permitted ---
		if (reply_boundary_pending) {
			holding_len = 0U;
			if (!rx_cancelled && !AppSafety_IsResetPending()) {
				read_error |= huart3.RxState != HAL_UART_STATE_BUSY_RX
						|| rx_arm_status != HAL_OK;
			}
			reply_boundary_pending = false;
		}

		HAL_GPIO_WritePin(CTS_GPIO_Port, CTS_Pin, GPIO_PIN_SET);
```

## 4. App/src/scan_uart_driver.c

Location: CancelRx closes normal boundary

Replace:

```c
	rx_cancelled = true;
	reset_stop_active = false;
```

With:

```c
	rx_cancelled = true;
	reset_stop_active = false;
	reply_boundary_pending = false;
```

## 5. App/src/scan_uart_driver.c

Location: Init clears normal boundary

Replace:

```c
	rx_cancelled = false;
	reset_stop_active = false;
```

With:

```c
	rx_cancelled = false;
	reset_stop_active = false;
	reply_boundary_pending = false;
```

## 6. App/src/scan_uart_driver.c

Location: new normal SendForReply before SendStart; old Send/SendStart unchanged

Replace:

```c
// --- Explicit domain hook avoids guessing START from arbitrary UART bytes ---
```

With:

```c
/*
 * A normal synchronous exchange with a boundary at the final UART TC.
 * Called only by Scanner task for preparation commands or SSI NAK RESEND.
 * RX remains armed and read errors remain latched while old bytes are drained.
 * No ordinary byte may be launched after reset latch.
 */
bool ScanUart_SendForReply(const uint8_t *data, uint16_t len) {
	// --- Require the existing read owner and ordinary UART admission ---
	if (data == NULL || len == 0U || len > 134U)
		return false;

	const uint32_t saved_primask = __get_PRIMASK();
	__disable_irq();

	bool ready = !AppSafety_IsResetPending() && !rx_cancelled
			&& read_active && !read_error
			&& huart3.RxState == HAL_UART_STATE_BUSY_RX
			&& rx_arm_status == HAL_OK;
	if (ready) {
		holding_len = 0U;
		reply_boundary_pending = true;
	}
	__set_PRIMASK(saved_primask);

	if (!ready)
		return false;

	// --- Existing TX/abort/CTS handling, with the ordinary reset gate ---
	bool sent = ScanUart_SendInternal(data, len, false, false);

	const uint32_t cleanup_primask = __get_PRIMASK();
	__disable_irq();
	reply_boundary_pending = false;
	bool clean = !read_error && !rx_cancelled && !AppSafety_IsResetPending();
	__set_PRIMASK(cleanup_primask);
	return sent && clean;
}

// --- Explicit domain hook avoids guessing START from arbitrary UART bytes ---
```

## 7. App/src/scan_uart_driver.c

Location: reset STOP takes over normal RX boundary

Replace:

```c
    reset_stop_active = true;
```

With:

```c
    reset_stop_active = true;
    reply_boundary_pending = false;
```

## 8. App/src/scan_uart_driver.c

Location: RX drains old bytes during normal control TX and still latches UART errors

Replace:

```c
		if (errors != HAL_UART_ERROR_NONE
				|| holding_len == SCAN_UART_PENDING_SIZE)
			read_error = true;
		else
			holding_buf[holding_len++] = byte;
```

With:

```c
		if (errors != HAL_UART_ERROR_NONE)
			read_error = true;
		else if (!reply_boundary_pending) {
			if (holding_len == SCAN_UART_PENDING_SIZE)
				read_error = true;
			else
				holding_buf[holding_len++] = byte;
		}
```

