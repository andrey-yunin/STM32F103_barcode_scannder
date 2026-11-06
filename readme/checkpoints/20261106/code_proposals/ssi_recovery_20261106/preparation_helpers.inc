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
