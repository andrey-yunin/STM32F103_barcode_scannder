#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "scan_ssi.h"
#include "receive_helpers.inc"

// --- Reference construction: decoder source/status followed by independent checksum ---
static unsigned decoder_packet(uint8_t opcode, const uint8_t *body,
        unsigned len, uint8_t status, uint8_t *packet) {
    unsigned total = Ssi_Encode(opcode, body, (uint16_t) len, packet);
    assert(total != 0U);
    packet[2] = SSI_MSG_SRC_DECODER;
    packet[3] = status;
    uint16_t sum = 0U;
    for (unsigned i = 0; i < total - 2U; ++i)
        sum = (uint16_t) (sum + packet[i]);
    sum = (uint16_t) (0U - sum);
    packet[total - 2U] = (uint8_t) (sum >> 8);
    packet[total - 1U] = (uint8_t) sum;
    return total;
}

// --- Snapshot consumer preserves the offset of every complete packet ---
static unsigned inspect(const uint8_t *data, uint16_t len, uint8_t *packet,
        uint16_t *used, ScanPacketKind_t *kinds, unsigned count) {
    uint16_t cursor = 0U;
    while (cursor < len) {
        ScanRxStep_t result = Scan_CollectPacket(data, len, &cursor, packet, used);
        assert(result != SCAN_RX_BAD_LENGTH);
        if (result == SCAN_RX_PACKET) {
            SsiMsg_t message;
            kinds[count++] = Scan_ClassifyPacket(packet, *used, &message);
            *used = 0U;
        }
    }
    return count;
}

int main(void) {
    uint8_t ack[SSI_PACKET_MAX_LEN], bad[SSI_PACKET_MAX_LEN];
    uint8_t good[SSI_PACKET_MAX_LEN], revision[SSI_PACKET_MAX_LEN];
    const uint8_t barcode[] = { 3U, '9', '0', '2', '2', '2', '2', '6', '0', '3',
            '0', '0', '8', '1', '0', '7', '0', '1' };
    unsigned a = decoder_packet(SSI_OP_CMD_ACK, NULL, 0U, 0U, ack);
    unsigned r = decoder_packet(SSI_OP_REPLY_REVISION, (const uint8_t *) "v1", 2U, 0U, revision);
    unsigned b = decoder_packet(SSI_OP_DECODE_DATA, barcode, sizeof(barcode), 0U, bad);
    unsigned g = decoder_packet(SSI_OP_DECODE_DATA, barcode, sizeof(barcode), 1U, good);
    bad[b - 1U] ^= 1U;
    uint8_t stream[128], assembled[SSI_PACKET_MAX_LEN];
    unsigned length = 0U;
    memcpy(stream + length, ack, a); length += a;
    memcpy(stream + length, revision, r); length += r;
    memcpy(stream + length, bad, b); length += b;
    memcpy(stream + length, good, g); length += g;

    // --- Every two-snapshot split, including the same-snapshot damaged/retry pair ---
    for (unsigned split = 0U; split <= length; ++split) {
        ScanPacketKind_t kinds[8];
        uint16_t used = 0U;
        unsigned count = inspect(stream, split, assembled, &used, kinds, 0U);
        count = inspect(stream + split, length - split, assembled, &used, kinds, count);
        assert(count == 4U && used == 0U);
        assert(kinds[0] == SCAN_PACKET_ACK && kinds[1] == SCAN_PACKET_REVISION);
        assert(kinds[2] == SCAN_PACKET_RESEND && kinds[3] == SCAN_PACKET_BARCODE);
    }
    printf("PASS %u snapshot splits: ACK/revision/bad-checksum/repeated F3 preserved in order\n", length + 1U);

    // --- Bytewise RX does not expose partial packets as completed results ---
    ScanPacketKind_t kinds[8];
    uint16_t used = 0U;
    unsigned count = 0U;
    for (unsigned i = 0U; i < length; ++i)
        count = inspect(stream + i, 1U, assembled, &used, kinds, count);
    assert(count == 4U && used == 0U);
    puts("PASS bytewise RX and retained partial packet state");

    // --- RESEND is an ordinary existing SSI packet, not a new CAN code ---
    uint8_t packet[SSI_PACKET_MAX_LEN];
    uint8_t cause = 1U;
    const uint8_t expected[] = { 5U, 0xD1U, 4U, 0U, 1U, 0xFFU, 0x25U };
    assert(Ssi_Encode(SSI_OP_CMD_NAK, &cause, 1U, packet) == sizeof(expected));
    assert(memcmp(packet, expected, sizeof(expected)) == 0);
    puts("PASS SSI host NAK RESEND golden vector 05 D1 04 00 01 FF 25");

    // --- ACK/NAK and invalid payloads are distinct task events ---
    SsiMsg_t message;
    unsigned n = decoder_packet(SSI_OP_CMD_NAK, &cause, 1U, 0U, packet);
    assert(Scan_ClassifyPacket(packet, n, &message) == SCAN_PACKET_NAK && message.data[0] == 1U);
    cause = 6U;
    n = decoder_packet(SSI_OP_CMD_NAK, &cause, 1U, 0U, packet);
    assert(Scan_ClassifyPacket(packet, n, &message) == SCAN_PACKET_NAK && message.data[0] == 6U);
    n = decoder_packet(SSI_OP_REPLY_REVISION, (const uint8_t *) "v1", 2U, 2U, packet);
    assert(Scan_ClassifyPacket(packet, n, &message) == SCAN_PACKET_REVISION);
    n = decoder_packet(SSI_OP_DECODE_DATA, barcode, 1U, 0U, packet);
    assert(Scan_ClassifyPacket(packet, n, &message) == SCAN_PACKET_INVALID);
    n = decoder_packet(SSI_OP_DECODE_DATA, barcode, sizeof(barcode), 2U, packet);
    assert(Scan_ClassifyPacket(packet, n, &message) == SCAN_PACKET_INVALID);
    n = Ssi_Encode(SSI_OP_DECODE_DATA, barcode, sizeof(barcode), packet);
    assert(Scan_ClassifyPacket(packet, n, &message) == SCAN_PACKET_INVALID);
    puts("PASS NAK causes preserved; empty barcode, continuation and host-source echo cannot become success");

    // --- Unsupported length never permits scanning arbitrary payload for F3 ---
    const uint8_t invalid[] = { 3U, 0xF3U, 0U, 0U };
    used = 0U;
    uint16_t cursor = 0U;
    assert(Scan_CollectPacket(invalid, sizeof(invalid), &cursor, assembled, &used) == SCAN_RX_BAD_LENGTH);
    assert(cursor == 1U);
    puts("PASS malformed length reported without sliding resynchronization");

    // --- A module-side old first transmission has no reading identifier ---
    uint8_t old[SSI_PACKET_MAX_LEN], fresh[SSI_PACKET_MAX_LEN];
    n = decoder_packet(SSI_OP_DECODE_DATA, barcode, sizeof(barcode), 0U, old);
    assert(decoder_packet(SSI_OP_DECODE_DATA, barcode, sizeof(barcode), 0U, fresh) == n);
    assert(memcmp(old, fresh, n) == 0);
    assert(Scan_ClassifyPacket(old, n, &message) == SCAN_PACKET_BARCODE);
    assert(Scan_ClassifyPacket(fresh, n, &message) == SCAN_PACKET_BARCODE);
    puts("CONFIRMED LIMITATION: old queued first F3 and new first F3 can be byte-identical; receive parser alone cannot prove ownership");
    puts("Preparation only: task integration, RX/TX ownership, recovery deadline and module boundary NOT VERIFIED.");
    return 0;
}
