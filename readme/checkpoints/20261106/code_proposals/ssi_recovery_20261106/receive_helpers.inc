/*
 * Preparation for the agreed Scanner SSI recovery block.
 * These helpers are NOT installed or connected to the firmware task.
 * They assemble/classify packets only; ownership between separate readings
 * requires a confirmed module-side boundary, described separately.
 */

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
