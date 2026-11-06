/*
 * scan_ssi.h
 *
 *  Created on: Jul 13, 2026
 *      Author: andrey
 */

#ifndef INC_SCAN_SSI_H_
#define INC_SCAN_SSI_H_

#include <stdint.h>
#include <stdbool.h>

// --- SSI Packet Format ---
// [Length:1][Opcode:1][MsgSrc:1][Status:1][Data:N][Checksum:2]
// Length = 4 + N; checksum excludes itself and is transmitted MSB first.

#define SSI_MSG_SRC_HOST        0x04
#define SSI_MSG_SRC_DECODER     0x00

// --- Host Commands (STM32 -> Scanner) ---
#define SSI_OP_REQUEST_REVISION 0xA3
#define SSI_OP_REPLY_REVISION   0xA4
#define SSI_OP_SCAN_ENABLE      0xE9
#define SSI_OP_START_DECODE     0xE4
#define SSI_OP_STOP_DECODE      0xE5
#define SSI_OP_BEEP             0xE6
#define SSI_OP_LED_ON           0xE7
#define SSI_OP_LED_OFF          0xE8

// --- Scanner Responses (Scanner -> STM32) ---
#define SSI_OP_DECODE_DATA      0xF3
#define SSI_OP_CMD_ACK          0xD0
#define SSI_OP_CMD_NAK          0xD1

#define SSI_MAX_PAYLOAD         128
#define SSI_PACKET_MAX_LEN      (4 + SSI_MAX_PAYLOAD + 2) // 134

typedef struct {
    uint8_t  opcode;
    uint8_t  msg_src;
    uint8_t  status;
    uint16_t data_len;
    uint8_t  data[SSI_MAX_PAYLOAD];
} SsiMsg_t;

// Caller supplies SSI_PACKET_MAX_LEN bytes; returns 0 on invalid arguments.
uint8_t Ssi_Encode(uint8_t opcode, const uint8_t *data,
                    uint16_t data_len, uint8_t *out_packet);

// Decodes the first packet; trailing bytes are allowed, no stream assembly.
// Returns false without modifying out_msg when validation fails.
bool Ssi_Decode(const uint8_t *buf, uint16_t buf_len, SsiMsg_t *out_msg);



#endif /* INC_SCAN_SSI_H_ */
