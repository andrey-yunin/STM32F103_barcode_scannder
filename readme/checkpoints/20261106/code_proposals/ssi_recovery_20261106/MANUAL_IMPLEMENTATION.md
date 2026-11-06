# SSI recovery — full manual candidate, 06.11.2026

PROPOSED, not installed. STOP -> ACK -> FLUSH_QUEUE -> ACK -> START was
accepted by the user. Recovery actions/CAN error mapping were accepted earlier.
The actual module capability remains BENCH NOT CONFIRMED. No silent fallback
is allowed after preparation failure. Project source hashes remain unchanged.

## Reference and Scanner adaptations

Common lifecycle/reset/Flash ownership remain as in the installed HC/Thermo
transfers. Domain sources are Zebra SSI72E-40451-03 RevA §1-4/1-5, §1-7,
CMD_ACK/CMD_NAK and FLUSH_QUEUE (§2-43), plus SE965HP Integration Guide
§7-94..7-98. These define the module protocol, not the CAN architecture.

Exact source of the normal RX reply boundary: existing installed
ScanUart_SendStopForReset/TxCpltCallback mechanism from 03.11. Reused final-byte
TC-before-CTS sequencing, adapted to normal reset-gated TX and persistent
read_error. No normal command can bypass reset latch. RX diagnostics remain raw.

Changes: six App files, 20 replacements. New normal SendForReply, explicit
status encoding for retransmitting the same host packet, STOP/FLUSH response
validation, cursor-based packet assembly/classification, RESEND loop,
source/status/nonempty validation and terminal state/error mapping.
F002/F006 shutdown, F003 reservations, capture commands, UART IT size/config,
wire CAN fields and Studio are retained. No ordinary success STOP is added;
the extra STOP prepares the next reading. Existing failure STOP remains after
CAN terminal; preparation failures forbid START.

## Two explicit project settings proposed with the candidate

- SCAN_SSI_RESPONSE_TIMEOUT_MS = 2000: waiting for one STOP/FLUSH answer.
  This is a software bound using the existing project's 2 s handshake policy;
  it is not confirmed readback of the module's response-time parameter.
- SCAN_SSI_HOST_RESENDS = 2: same host packet may be retransmitted twice, only
  on an explicit RESEND and with status bit0 set. For START this is allowed
  only before its ACK. It is not a fresh automatic decode after an accepted
  START. The guide specifies two retries for decoder data and separately
  requires the host to bound retries; two host retries is a proposed Scanner
  adaptation, not a quoted mandatory manufacturer value.

These new project settings are surfaced for review; agreement to the
STOP/FLUSH sequence alone is not reported as approval of their exact values.
Read timeout still starts after initial START and is not renewed by retries.
No new general TX deadline is added; existing local UART/RTS bounds apply.
Preparation adds time before START. Studio currently uses timeout+7 s and
has not been changed: worst-case added preparation is not covered by that
window. Application-level timeout integration remains OPEN; this candidate
must not be called fully accepted across the ecosystem on HOST alone.

## Checks and limits

34 operation scenarios / 63 CAN commands PASS under ASan/UBSan with a model
that implements response priority, STOP and FLUSH semantics. Includes two
reads, old queued packets, stale ACK during TX, corrupt/repeated F3, no DATA,
NAK causes and host retries, empty/source/framing, cancellation and next read.
18 prior shutdown groups PASS (normal preparation replies added to the model,
extra pre-read STOP accounted separately). Existing SSI codec tests PASS.
Four C files compile for ARM GCC13.3.1, Wall/Wextra/Werror. No full Debug link,
Release, hardware commands, flashing or BENCH were performed.
Manual steps replay to exactly the checked six candidate files.

Stack usage is static per-function compiler output only. New main scan task
frame 544 B and control reply frame 208 B are not runtime stack bounds;
normal/IRQ nested call reserves remain unmeasured.

## Exact manual steps

### 1. App/inc/scan_ssi.h

Location: SSI command constants

Replace exactly:

```c
#define SSI_OP_STOP_DECODE      0xE5
```

With the full fragment:

```c
#define SSI_OP_STOP_DECODE      0xE5
#define SSI_OP_FLUSH_QUEUE      0xD2

// --- SSI status and NAK causes; these are not CAN error codes ---
#define SSI_STATUS_RETRANSMIT   0x01U
#define SSI_NAK_RESEND          0x01U
#define SSI_NAK_BAD_CONTEXT     0x02U
#define SSI_NAK_DENIED          0x06U
#define SSI_NAK_CANCEL          0x0AU
```

### 2. App/inc/scan_ssi.h

Location: codec declaration before Ssi_Decode

Replace exactly:

```c
// Decodes the first packet; trailing bytes are allowed, no stream assembly.
```

With the full fragment:

```c
// --- Same packet encoding with an explicit SSI status byte ---
// Used only for retransmission of the same host command.
uint8_t Ssi_EncodeWithStatus(uint8_t opcode, uint8_t status, const uint8_t *data,
                           uint16_t data_len, uint8_t *out_packet);

// Decodes the first packet; trailing bytes are allowed, no stream assembly.
```

### 3. App/src/scan_ssi.c

Location: encoder implementation

Replace exactly:

```c
uint8_t Ssi_Encode(uint8_t opcode, const uint8_t *data,
                   uint16_t data_len, uint8_t *out_packet)
```

With the full fragment:

```c
uint8_t Ssi_EncodeWithStatus(uint8_t opcode, uint8_t status, const uint8_t *data,
                           uint16_t data_len, uint8_t *out_packet)
```

### 4. App/src/scan_ssi.c

Location: encoded SSI status

Replace exactly:

```c
out_packet[idx++] = 0x00;
```

With the full fragment:

```c
out_packet[idx++] = status;
```

### 5. App/src/scan_ssi.c

Location: retain the original API before Ssi_Decode

Replace exactly:

```c
// --- Validate the first complete packet before publishing its contents ---
```

With the full fragment:

```c
// --- Existing callers send a first transmission with status zero ---
uint8_t Ssi_Encode(uint8_t opcode, const uint8_t *data,
                   uint16_t data_len, uint8_t *out_packet)
{
    return Ssi_EncodeWithStatus(opcode, 0U, data, data_len, out_packet);
}

// --- Validate the first complete packet before publishing its contents ---
```

### 6. App/inc/scan_uart_driver.h

Location: UART normal API after ScanUart_Send

Replace exactly:

```c
// --- START_DECODE evidence is retained independently of later STOP/ENABLE ---
```

With the full fragment:

```c
// --- Normal task-owned exchange with a fresh RX boundary ---
// BeginRead must already own RX. Old incoming bytes are drained until the
// final transmitted byte completes, before CTS is released for the new reply.
// RX errors remain latched; reset gates are identical to ordinary Send.
bool ScanUart_SendForReply(const uint8_t *data, uint16_t len);

// --- START_DECODE evidence is retained independently of later STOP/ENABLE ---
```

### 7. App/src/scan_uart_driver.c

Location: RX ownership flags

Replace exactly:

```c
static volatile bool reset_stop_active;
```

With the full fragment:

```c
static volatile bool reset_stop_active;

// --- Boundary of a normal task-owned control/RESEND exchange ---
// Does not bypass reset latch. Only TxCplt of the last byte opens new data.
static volatile bool reply_boundary_pending;
```

### 8. App/src/scan_uart_driver.c

Location: normal RX boundary in final-byte TxCplt before CTS HIGH

Replace exactly:

```c
			read_active = true;
		}

		HAL_GPIO_WritePin(CTS_GPIO_Port, CTS_Pin, GPIO_PIN_SET);
```

With the full fragment:

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

### 9. App/src/scan_uart_driver.c

Location: CancelRx closes normal boundary

Replace exactly:

```c
	rx_cancelled = true;
	reset_stop_active = false;
```

With the full fragment:

```c
	rx_cancelled = true;
	reset_stop_active = false;
	reply_boundary_pending = false;
```

### 10. App/src/scan_uart_driver.c

Location: Init clears normal boundary

Replace exactly:

```c
	rx_cancelled = false;
	reset_stop_active = false;
```

With the full fragment:

```c
	rx_cancelled = false;
	reset_stop_active = false;
	reply_boundary_pending = false;
```

### 11. App/src/scan_uart_driver.c

Location: new normal SendForReply before SendStart; old Send/SendStart unchanged

Replace exactly:

```c
// --- Explicit domain hook avoids guessing START from arbitrary UART bytes ---
```

With the full fragment:

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

### 12. App/src/scan_uart_driver.c

Location: reset STOP takes over normal RX boundary

Replace exactly:

```c
    reset_stop_active = true;
```

With the full fragment:

```c
    reset_stop_active = true;
    reply_boundary_pending = false;
```

### 13. App/src/scan_uart_driver.c

Location: RX drains old bytes during normal control TX and still latches UART errors

Replace exactly:

```c
		if (errors != HAL_UART_ERROR_NONE
				|| holding_len == SCAN_UART_PENDING_SIZE)
			read_error = true;
		else
			holding_buf[holding_len++] = byte;
```

With the full fragment:

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

### 14. App/src/scan_controller.c

Location: reject Type without a nonempty barcode

Replace exactly:

```c
if (msg.data_len < 1U)
```

With the full fragment:

```c
if (msg.data_len < 2U)
```

### 15. App/src/tasks/task_scan_controller.c

Location: replace Scan_IsValidDecodeData/Scan_CollectPacket and add preparation helpers

Replace exactly:

```c
// --- Проверка DECODE_DATA без сохранения результата и изменения статистики ---
static bool Scan_IsValidDecodeData(const uint8_t *data, uint16_t len) {
	SsiMsg_t message;

	return Ssi_Decode(data, len, &message)
			&& message.opcode == SSI_OP_DECODE_DATA && message.data_len >= 1U;
}

/*
 * Задача собирает SSI-пакет по Length, независимо от границ RX-событий.
 * Полный валидный ACK пропускается; следующий пакет собирается далее.
 * Повреждённый пакет передаётся существующей обработке ошибок.
 */
static bool Scan_CollectPacket(const uint8_t *data, uint16_t len,
		uint8_t *packet, uint16_t *packet_len) {
	for (uint16_t i = 0U; i < len; ++i) {
		packet[(*packet_len)++] = data[i];

		// --- Некорректная длина обрабатывается как ошибка SSI ---
		if (packet[0] < 4U || packet[0] > 4U + SSI_MAX_PAYLOAD)
			return true;

		// Length включает заголовок и данные, но не checksum.
		uint16_t expected = (uint16_t) packet[0] + 2U;

		if (*packet_len < expected)
			continue;

		// --- Полный ACK не завершает ожидание штрихкода ---
		SsiMsg_t message;

		if (Ssi_Decode(packet, *packet_len,
				&message) && message.opcode == SSI_OP_CMD_ACK) {
			*packet_len = 0U;
			continue;
		}

		return true;
	}

	return false;
}


```

With the full fragment:

```c
// --- Result of consuming bytes, independent of UART interrupt boundaries ---
typedef enum {
    SCAN_RX_MORE,
    SCAN_RX_PACKET,
    SCAN_RX_BAD_LENGTH
} ScanRxStep_t;

// --- A completed packet selects a task action, not a CAN terminal result ---
typedef enum {
    SCAN_PACKET_ACK,
    SCAN_PACKET_NAK,
    SCAN_PACKET_BARCODE,
    SCAN_PACKET_REVISION,
    SCAN_PACKET_RESEND,
    SCAN_PACKET_INVALID
} ScanPacketKind_t;

/*
 * Consumes at most one complete packet. The caller retains cursor within
 * its RX snapshot, handles the packet, clears packet_len and resumes with
 * the same snapshot. This preserves packets following a damaged packet.
 * Length outside supported framing is an error, not permission to search
 * arbitrary payload bytes for a possible F3 header.
 */
static ScanRxStep_t Scan_CollectPacket(const uint8_t *data, uint16_t len,
        uint16_t *cursor, uint8_t *packet, uint16_t *packet_len) {
    // --- Assembly continues across RX snapshots ---
    while (*cursor < len) {
        if (*packet_len >= SSI_PACKET_MAX_LEN)
            return SCAN_RX_BAD_LENGTH;

        packet[(*packet_len)++] = data[(*cursor)++];

        if (packet[0] < 4U || packet[0] > 4U + SSI_MAX_PAYLOAD)
            return SCAN_RX_BAD_LENGTH;

        if (*packet_len == (uint16_t) packet[0] + 2U)
            return SCAN_RX_PACKET;
    }

    return SCAN_RX_MORE;
}

/*
 * Called only for a complete supported-length packet from the assembler.
 * CRC failure requests SSI RESEND; it is not a CAN NACK decision.
 * Valid ACK/NAK are exposed to the exchange owner without generating a reply.
 * Segmented or invalid expected data is not a successful single barcode.
 */
static ScanPacketKind_t Scan_ClassifyPacket(const uint8_t *packet,
        uint16_t packet_len, SsiMsg_t *message) {
    // --- Framing belongs to the assembler; a remaining decode failure is checksum ---
    if (!Ssi_Decode(packet, packet_len, message))
        return SCAN_PACKET_RESEND;

    // --- The module is the sender of incoming packets ---
    if (message->msg_src != SSI_MSG_SRC_DECODER)
        return SCAN_PACKET_INVALID;

    // --- Revision fragments are outside the active barcode operation ---
    // Dropping them does not require assembling their revision text.
    if (message->opcode == SSI_OP_REPLY_REVISION)
        return (message->status & (uint8_t) ~0x03U) == 0U ?
                SCAN_PACKET_REVISION : SCAN_PACKET_INVALID;

    // --- Single-packet ACK, NAK and barcode use only the retransmit status bit ---
    // Multipacket barcode assembly is not claimed by this candidate fragment.
    if ((message->status & (uint8_t) ~0x01U) != 0U)
        return SCAN_PACKET_INVALID;

    switch (message->opcode) {
    case SSI_OP_CMD_ACK:
        return message->data_len == 0U ?
                SCAN_PACKET_ACK : SCAN_PACKET_INVALID;

    case SSI_OP_CMD_NAK:
        return message->data_len == 1U ?
                SCAN_PACKET_NAK : SCAN_PACKET_INVALID;

    case SSI_OP_DECODE_DATA:
        // Type plus at least one byte of barcode data.
        return message->data_len >= 2U ?
                SCAN_PACKET_BARCODE : SCAN_PACKET_INVALID;

    default:
        return SCAN_PACKET_INVALID;
    }
}

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


```

### 16. App/src/tasks/task_scan_controller.c

Location: before normal BeginRead/START

Replace exactly:

```c
		// --- Открытие накопления ответа перед отправкой START ---
```

With the full fragment:

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

### 17. App/src/tasks/task_scan_controller.c

Location: normal START wake/TX failure uses the agreed E602

Replace exactly:

```c
				ScanCtrl_HandleTransportError();
				CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_HARDWARE);
```

With the full fragment:

```c
				ScanCtrl_HandleTransportError();
				CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_COMM);
```

### 18. App/src/tasks/task_scan_controller.c

Location: replace receive loop and outgoing barcode ACK; read timeout setup above is retained

Replace exactly:

```c
		bool rx_received = false;
		bool wait_error = false;

		// --- Накопитель одного SSI-пакета принадлежит задаче Scanner ---
		static uint8_t rx_packet[SSI_PACKET_MAX_LEN];
		uint16_t rx_len = 0U;
		const uint8_t *rx_data = NULL;

		// --- Приём пакета в пределах исходного срока запроса ---
		// Сначала забираем накопленные байты, затем при необходимости ждём.
		// Число уведомлений не определяет количество доступных данных.
		for (;;) {
			// --- Отмена и общий срок ---
			if (AppSafety_IsResetPending())
				break;

			uint32_t elapsed = osKernelGetTickCount() - started;

			if (elapsed >= timeout_ticks)
				break;

			// --- Потребление накопленных байтов ---
			uint16_t received_len;
			bool receive_error;

			const uint8_t *received = ScanUart_GetReceived(&received_len,
					&receive_error);

			if (receive_error) {
				wait_error = true;
				break;
			}

			if (received != NULL) {
				AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);

				if (!Scan_CollectPacket(received, received_len, rx_packet,
						&rx_len))
					continue;

				rx_data = rx_packet;
				rx_received = true;
				break;
			}

			// --- Ожидание следующих данных без перезапуска срока ---
			uint32_t remaining = timeout_ticks - elapsed;
			uint32_t wait_ticks =
					remaining < slice_ticks ? remaining : slice_ticks;

			uint32_t flags = osThreadFlagsWait(
			FLAG_SCAN_UART_RX, osFlagsWaitAny, wait_ticks);

			if (flags == osFlagsErrorTimeout) {
				AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
				continue;
			}

			if ((flags & osFlagsError) != 0U) {
				wait_error = true;
				break;
			}

			AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
		}

		// --- Подтверждение штрихкода сканеру до принятия результата и отправки CAN ---
		bool ack_error = false;

		if (rx_received && !wait_error
				&& Scan_IsValidDecodeData(rx_data, rx_len)) {
			ssi_len = Ssi_Encode(SSI_OP_CMD_ACK, NULL, 0, ssi_packet);
			ack_error = !ScanUart_Wake() || !ScanUart_Send(ssi_packet, ssi_len);
		}


```

With the full fragment:

```c
        bool rx_received = false;
        bool wait_error = false;
        bool hardware_error = false;
        bool checksum_seen = false;
        bool start_acked = false;
        uint32_t start_resends = 0U;

        // --- Packet storage and snapshot cursor belong to the Scanner task ---
        static uint8_t rx_packet[SSI_PACKET_MAX_LEN];
        uint16_t rx_len = 0U;
        const uint8_t *rx_data = NULL;

        // --- Recover SSI within the original reading deadline ---
        for (;;) {
            if (AppSafety_IsResetPending())
                break;
            uint32_t elapsed = osKernelGetTickCount() - started;
            if (elapsed >= timeout_ticks)
                break;

            uint16_t received_len;
            bool receive_error;
            const uint8_t *received = ScanUart_GetReceived(&received_len, &receive_error);
            if (receive_error) {
                wait_error = true;
                break;
            }

            if (received != NULL) {
                uint16_t cursor = 0U;
                while (cursor < received_len) {
                    if (AppSafety_IsResetPending()
                            || (uint32_t) (osKernelGetTickCount() - started) >= timeout_ticks)
                        break;

                    ScanRxStep_t step = Scan_CollectPacket(received, received_len,
                            &cursor, rx_packet, &rx_len);
                    if (step == SCAN_RX_BAD_LENGTH) {
                        wait_error = true;
                        break;
                    }
                    if (step == SCAN_RX_MORE)
                        break;

                    SsiMsg_t message;
                    ScanPacketKind_t kind = Scan_ClassifyPacket(rx_packet, rx_len, &message);
                    if (kind == SCAN_PACKET_BARCODE) {
                        rx_data = rx_packet;
                        rx_received = true;
                        break;
                    }

                    // --- Every handled non-barcode packet releases the assembler ---
                    rx_len = 0U;
                    if (kind == SCAN_PACKET_ACK) {
                        start_acked = true;
                    } else if (kind == SCAN_PACKET_REVISION) {
                        // A natural reply to another command is not this reading's result.
                    } else if (kind == SCAN_PACKET_RESEND) {
                        checksum_seen = true;
                        const uint8_t cause = SSI_NAK_RESEND;
                        ssi_len = Ssi_Encode(SSI_OP_CMD_NAK, &cause, 1U, ssi_packet);
                        if (!ScanUart_Wake() || !ScanUart_SendForReply(ssi_packet, ssi_len)) {
                            wait_error = true;
                            break;
                        }
                    } else if (kind == SCAN_PACKET_NAK) {
                        if (message.data[0] == SSI_NAK_RESEND) {
                            // Retransmit only a START explicitly rejected before its ACK.
                            // A later NAK must never restart an already accepted decode.
                            if (start_acked || start_resends >= SCAN_SSI_HOST_RESENDS) {
                                wait_error = true;
                                break;
                            }
                            ++start_resends;
                            ssi_len = Ssi_EncodeWithStatus(SSI_OP_START_DECODE,
                                    SSI_STATUS_RETRANSMIT, NULL, 0U, ssi_packet);
                            if (!ScanUart_Wake() || !ScanUart_SendStart(ssi_packet, ssi_len)) {
                                wait_error = true;
                                break;
                            }
                        } else {
                            hardware_error = true;
                            break;
                        }
                    } else {
                        hardware_error = true;
                        break;
                    }
                    AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
                }
                if (rx_received || wait_error || hardware_error)
                    break;
                AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
                continue;
            }

            // --- Recompute remaining time after task work, never renew the deadline ---
            elapsed = osKernelGetTickCount() - started;
            if (elapsed >= timeout_ticks)
                break;
            uint32_t remaining = timeout_ticks - elapsed;
            uint32_t wait_ticks = remaining < slice_ticks ? remaining : slice_ticks;
            uint32_t flags = osThreadFlagsWait(FLAG_SCAN_UART_RX, osFlagsWaitAny, wait_ticks);
            if (flags != osFlagsErrorTimeout && (flags & osFlagsError) != 0U) {
                wait_error = true;
                break;
            }
            AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
        }

        // --- Confirm one valid nonempty barcode before accepting its CAN result ---
        bool ack_error = false;
        if (rx_received && !wait_error && !hardware_error) {
            ssi_len = Ssi_Encode(SSI_OP_CMD_ACK, NULL, 0U, ssi_packet);
            ack_error = !ScanUart_Wake() || !ScanUart_Send(ssi_packet, ssi_len);
        }


```

### 19. App/src/tasks/task_scan_controller.c

Location: terminal mapping for module/content failure and unrecovered checksum

Replace exactly:

```c
		} else if (!rx_received) {
			ScanCtrl_HandleTimeout();
```

With the full fragment:

```c
		} else if (hardware_error) {
			ScanCtrl_HandleTransportError();
			CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_HARDWARE);
		} else if (!rx_received && checksum_seen) {
			ScanCtrl_HandleTransportError();
			CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_COMM);
		} else if (!rx_received) {
			ScanCtrl_HandleTimeout();
```

### 20. App/src/tasks/task_scan_controller.c

Location: no completed command leaves SCANNING

Replace exactly:

```c
			} else {
				CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_HARDWARE);
			}
		}

		// --- Прекращение попытки после неуспешного чтения ---
```

With the full fragment:

```c
			} else {
				// --- Terminal failure also ends the controller operation ---
				ScanCtrl_HandleTransportError();
				CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_HARDWARE);
			}
		}

		// --- Прекращение попытки после неуспешного чтения ---
```

