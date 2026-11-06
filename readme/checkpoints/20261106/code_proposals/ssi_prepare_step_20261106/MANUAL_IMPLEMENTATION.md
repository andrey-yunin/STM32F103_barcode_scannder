# Intermediate task preparation block — 06.11.2026

PROPOSED, not installed. One file, two insertions. Uses the actual installed
codec/UART boundary. Adds STOP/ACK -> FLUSH/ACK before the existing START/read
sequence. Does not replace the barcode receive/recovery loop or controller.

Source: agreed STOP/FLUSH sequence and SSI72E-40451-03 Table1-1/§1-4/§2-43.
Exact installed reference for bounded first-answer handling is
Scan_WaitResetStopAck with ScanUart_SendStopForReset. Scanner adaptations:
normal reset-gated SendForReply, ACK/NAK6/7, idempotent STOP/FLUSH selection,
close/check EndRead after each phase, existing terminal reset priority and
failure of preparation prevents START. Runtime state/queue/F003 ownership stay
with the existing task/Dispatcher. Generic SSI is not physical capability proof.

Proposed values visible in code: 2000 ms control reply timeout (software
policy, not module readback); two retransmissions of the same host STOP/FLUSH
only after explicit RESEND and using status bit0. The decoder's fixed retry
count does not mandate a host limit; this is a stated project choice for review.
No retry on silence; no new START retry in this intermediate block. Each
reply wait ends immediately on a valid response. Existing read timeout still
begins after START. Preparation duration/worst-case Studio timeout integration
remain OPEN; Studio unchanged. No added general UART TX deadline.

HOST ASan/UBSan: 20 intermediate scenarios / 36 commands PASS including normal
STOP/FLUSH, NAK/silence/stale ACK, host RESEND, next reading, wrap, resets.
18 prior shutdown groups PASS with preparation responses accounted separately.
ARM GCC13.3.1 task per-file compile Wall/Wextra/Werror PASS. Manual replay exact.
No project firmware write, full Debug link, Release, BENCH or hardware actions.

## 1. App/src/tasks/task_scan_controller.c

Location: Before ScanQueryRevision, after existing Scan_CollectPacket

Replace:

```c
// --- Read-only revision probe or single BEEP: DONE means RX capture complete ---
```

With:

```c
// --- SSI host response limit: project policy matches the existing 2 s RTS bound ---
// This is a software limit, not readback of the module's current parameter.
#define SCAN_SSI_RESPONSE_TIMEOUT_MS 2000U

// --- At most three transmissions of the same explicitly rejected host packet ---
// Host retry limit is a proposed Scanner adaptation. The SSI guide separately
// specifies two retries for decoder data and requires the host to limit retries.
#define SCAN_SSI_HOST_RESENDS 2U

/*
 * Receives only the first answer of a freshly established control handshake.
 * STOP/FLUSH expect ACK, not a barcode. Old postponed data after this answer
 * is discarded by EndRead; the following control handshake opens a new boundary.
 * Valid NAK requests no ACK/NAK response. RESEND asks to retransmit the same
 * command with the status bit; it does not silently authorize a new operation.
 */
static uint16_t Scan_WaitControlReply(bool *resend) {
    uint8_t packet[7];
    uint16_t used = 0U;
    const uint32_t started = osKernelGetTickCount();
    const uint32_t duration = (uint32_t) (((uint64_t)
            SCAN_SSI_RESPONSE_TIMEOUT_MS * osKernelGetTickFreq()) / 1000U);
    *resend = false;

    // --- One bounded reply; reset is finalized by the caller's domain path ---
    while (!AppSafety_IsResetPending()
            && (uint32_t) (osKernelGetTickCount() - started) < duration) {
        uint16_t length;
        bool error;
        const uint8_t *received = ScanUart_GetReceived(&length, &error);
        if (error)
            return CAN_ERR_SCAN_COMM;

        for (uint16_t i = 0U; received != NULL && i < length; ++i) {
            packet[used++] = received[i];
            if (packet[0] != 4U && packet[0] != 5U)
                return CAN_ERR_SCAN_COMM;
            if (used < (uint16_t) packet[0] + 2U)
                continue;

            SsiMsg_t message;
            if ((uint32_t) (osKernelGetTickCount() - started) >= duration
                    || !Ssi_Decode(packet, used, &message)
                    || message.msg_src != SSI_MSG_SRC_DECODER
                    || (message.status & (uint8_t) ~SSI_STATUS_RETRANSMIT) != 0U)
                return CAN_ERR_SCAN_COMM;

            if (message.opcode == SSI_OP_CMD_ACK && message.data_len == 0U)
                return CAN_ERR_NONE;

            if (message.opcode == SSI_OP_CMD_NAK && message.data_len == 1U) {
                *resend = message.data[0] == SSI_NAK_RESEND;
                return *resend ? CAN_ERR_SCAN_COMM : CAN_ERR_SCAN_HARDWARE;
            }

            return CAN_ERR_SCAN_COMM;
        }

        if (osDelay(1U) != osOK)
            return CAN_ERR_SCAN_COMM;
        AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
    }
    return CAN_ERR_SCAN_COMM;
}

/*
 * One idempotent preparation command, including RX ownership and response.
 * Only an explicit decoder RESEND permits retransmission. Timeout is not
 * treated as proof that the command was rejected. A failed boundary forbids START.
 */
static uint16_t Scan_RunPreparationCommand(uint8_t opcode) {
    for (uint32_t attempt = 0U; attempt <= SCAN_SSI_HOST_RESENDS; ++attempt) {
        if (!ScanUart_BeginRead())
            return CAN_ERR_SCAN_COMM;

        uint8_t packet[6];
        uint8_t length = Ssi_EncodeWithStatus(opcode,
                attempt == 0U ? 0U : SSI_STATUS_RETRANSMIT, NULL, 0U, packet);
        if (!ScanUart_Wake() || !ScanUart_SendForReply(packet, length)) {
            (void) ScanUart_EndRead();
            return CAN_ERR_SCAN_COMM;
        }

        bool resend;
        uint16_t result = Scan_WaitControlReply(&resend);
        if (!ScanUart_EndRead())
            return CAN_ERR_SCAN_COMM;
        if (AppSafety_IsResetPending())
            return CAN_ERR_SCAN_COMM;
        if (!resend)
            return result;
    }
    return CAN_ERR_SCAN_COMM;
}

/*
 * Defines a module-side boundary for every new barcode reading.
 * A STOP ACK confirms the old decode session stopped; FLUSH ACK confirms its
 * queued packets were eliminated. Neither MCU buffer reset nor START ACK alone
 * establishes this boundary. Physical support remains subject to BENCH.
 */
static uint16_t Scan_PrepareRead(void) {
    uint16_t result = Scan_RunPreparationCommand(SSI_OP_STOP_DECODE);
    if (result != CAN_ERR_NONE)
        return result;
    return Scan_RunPreparationCommand(SSI_OP_FLUSH_QUEUE);
}

// --- Read-only revision probe or single BEEP: DONE means RX capture complete ---
```

## 2. App/src/tasks/task_scan_controller.c

Location: before normal BeginRead/START

Replace:

```c
		// --- Открытие накопления ответа перед отправкой START ---
```

With:

```c
		// --- Confirmed module-side boundary before a new reading ---
		uint16_t prepare_error = Scan_PrepareRead();
		if (prepare_error != CAN_ERR_NONE) {
			if (Scan_SyncResetState() || !Scan_TryAcceptTerminalResult()) {
				(void) Scan_SyncResetState();
				CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_DEVICE_BUSY);
			} else {
				ScanCtrl_HandleTransportError();
				CAN_SendNackPublic(cmd.cmd_code, prepare_error);
			}
			continue;
		}

		// --- Открытие накопления ответа перед отправкой START ---
```

