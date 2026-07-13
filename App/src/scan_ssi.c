/*
 * scan_ssi.c
 *
 *  Created on: Jul 13, 2026
 *      Author: andrey
 */

#include "scan_ssi.h"
#include <string.h>

// --- Checksum SSI: 2's complement sum ---
// Сумма всех байт пакета (включая checksum) = 0x0000 при валидном пакете.
static uint16_t Ssi_Checksum(const uint8_t *data, uint16_t len)
{
    uint32_t sum = 0;
    for (uint16_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return (uint16_t)(-(int16_t)(uint16_t)(sum & 0xFFFF));
}

uint8_t Ssi_Encode(uint8_t opcode, const uint8_t *data,
                    uint16_t data_len, uint8_t *out_packet)
{
    uint8_t idx = 0;

    out_packet[idx++] = (uint8_t)(3 + data_len); // Len: opcode + src + status + data
    out_packet[idx++] = opcode;
    out_packet[idx++] = SSI_MSG_SRC_HOST;
    out_packet[idx++] = 0x00; // Status

    for (uint8_t i = 0; i < data_len; i++) {
        out_packet[idx++] = data[i];
    }

    uint16_t cksum = Ssi_Checksum(out_packet, idx);
    out_packet[idx++] = (uint8_t)(cksum & 0xFF);
    out_packet[idx++] = (uint8_t)((cksum >> 8) & 0xFF);

    return idx;
}

bool Ssi_Decode(const uint8_t *buf, uint16_t buf_len, SsiMsg_t *out_msg)
{
    if (buf == NULL || out_msg == NULL || buf_len < 5) return false;

    uint8_t len_field = buf[0];

    // Len field = opcode + src + status + data_len (without checksum)
    if (len_field < 3) return false;
    uint16_t expected_total = (uint16_t)len_field + 2; // +2 for checksum
    if (buf_len < expected_total) return false;

    // Verify checksum: sum of all bytes must be 0 (16-bit)
    uint32_t sum = 0;
    for (uint8_t i = 0; i < expected_total; i++) {
        sum += buf[i];
    }
    if ((sum & 0xFFFF) != 0) return false;

    out_msg->opcode  = buf[1];
    out_msg->msg_src = buf[2];
    out_msg->status  = buf[3];
    out_msg->data_len = len_field - 3;

    for (uint8_t i = 0; i < out_msg->data_len && i < SSI_MAX_PAYLOAD; i++) {
        out_msg->data[i] = buf[4 + i];
    }

    return true;
}
