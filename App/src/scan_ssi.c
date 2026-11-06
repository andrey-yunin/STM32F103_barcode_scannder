/*
 * scan_ssi.c
 *
 *  Created on: Jul 13, 2026
 *      Author: andrey
 */

#include "scan_ssi.h"
#include <string.h>

// --- Checksum SSI: negative byte sum, transmitted as a big-endian word ---
static uint16_t Ssi_Checksum(const uint8_t *data, uint16_t len)
{
    uint32_t sum = 0;
    for (uint16_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return (uint16_t)(0U - sum);
}

// --- Encode one packet, including the explicit retransmission status ---
uint8_t Ssi_EncodeWithStatus(uint8_t opcode, uint8_t status,
                           const uint8_t *data, uint16_t data_len,
                           uint8_t *out_packet)
{
    if (out_packet == NULL || data_len > SSI_MAX_PAYLOAD ||
        (data_len != 0 && data == NULL)) {
        return 0;
    }

    // --- SSI header and payload ---
    uint16_t idx = 0;
    out_packet[idx++] = (uint8_t)(4 + data_len);
    out_packet[idx++] = opcode;
    out_packet[idx++] = SSI_MSG_SRC_HOST;
    out_packet[idx++] = status;

    for (uint16_t i = 0; i < data_len; i++) {
        out_packet[idx++] = data[i];
    }

    // --- Checksum includes the selected status byte ---
    uint16_t checksum = Ssi_Checksum(out_packet, idx);
    out_packet[idx++] = (uint8_t)(checksum >> 8);
    out_packet[idx++] = (uint8_t)checksum;

    return (uint8_t)idx;
}

// --- Preserve the existing API for a first transmission ---
uint8_t Ssi_Encode(uint8_t opcode, const uint8_t *data,
                   uint16_t data_len, uint8_t *out_packet)
{
    return Ssi_EncodeWithStatus(opcode, 0U, data, data_len, out_packet);
}

// --- Validate the first complete packet before publishing its contents ---
bool Ssi_Decode(const uint8_t *buf, uint16_t buf_len, SsiMsg_t *out_msg)
{
    if (buf == NULL || out_msg == NULL || buf_len < 6) return false;

    uint16_t length = buf[0];
    if (length < 4 || length > 4 + SSI_MAX_PAYLOAD) return false;
    if (buf_len < length + 2) return false;

    uint16_t received_checksum = (uint16_t)(((uint16_t)buf[length] << 8) |
                                          buf[length + 1]);
    if (received_checksum != Ssi_Checksum(buf, length)) return false;

    out_msg->opcode = buf[1];
    out_msg->msg_src = buf[2];
    out_msg->status = buf[3];
    out_msg->data_len = length - 4;
    memcpy(out_msg->data, buf + 4, out_msg->data_len);
    return true;
}
