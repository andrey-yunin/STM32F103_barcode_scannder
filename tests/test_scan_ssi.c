#include "scan_ssi.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

// --- Published command vectors and decoder responses ---
int main(void)
{
    const uint8_t commands[][6] = {
        {4, 0xE4, 4, 0, 0xFF, 0x14},
        {4, 0xE5, 4, 0, 0xFF, 0x13},
        {4, 0xE9, 4, 0, 0xFF, 0x0F},
        {4, 0xA3, 4, 0, 0xFF, 0x55}
    };
    uint8_t packet[SSI_PACKET_MAX_LEN];
    SsiMsg_t msg;
    const uint8_t beep_code=0, beep[]={5,0xE6,4,0,0,0xFF,0x11};
    assert(Ssi_Encode(SSI_OP_BEEP,&beep_code,1,packet)==7);
    assert(!memcmp(packet,beep,sizeof(beep)));
    for (unsigned i = 0; i < sizeof commands / sizeof commands[0]; ++i) {
        assert(Ssi_Encode(commands[i][1], NULL, 0, packet) == 6);
        assert(memcmp(packet, commands[i], 6) == 0);
    }
    const uint8_t ack[] = {4, 0xD0, 0, 0, 0xFF, 0x2C};
    const uint8_t nak[] = {5, 0xD1, 0, 0, 1, 0xFF, 0x29};
    assert(Ssi_Decode(ack, sizeof ack, &msg));
    assert(msg.opcode == SSI_OP_CMD_ACK && msg.data_len == 0);
    assert(Ssi_Decode(nak, sizeof nak, &msg));
    assert(msg.opcode == SSI_OP_CMD_NAK && msg.data_len == 1 && msg.data[0] == 1);

    // --- Invalid packets must leave the result untouched ---
    SsiMsg_t before = msg;
    for (unsigned len = 0; len < sizeof nak; ++len)
        assert(!Ssi_Decode(nak, len, &msg));
    memcpy(packet, nak, sizeof nak);
    packet[5] = 0x29; packet[6] = 0xFF;
    assert(!Ssi_Decode(packet, 7, &msg));
    packet[5] = 0xFF; packet[6] = 0x28;
    assert(!Ssi_Decode(packet, 7, &msg));
    packet[0] = 3;
    assert(!Ssi_Decode(packet, 7, &msg));
    packet[0] = 255;
    assert(!Ssi_Decode(packet, sizeof packet, &msg));
    assert(!Ssi_Decode(NULL, 6, &msg));
    assert(!Ssi_Decode(ack, sizeof ack, NULL));
    assert(memcmp(&msg, &before, sizeof msg) == 0);

    // --- Maximum payload with an independently accumulated checksum ---
    uint8_t data[SSI_MAX_PAYLOAD];
    memset(data, 0xFF, sizeof data);
    unsigned len = Ssi_Encode(0xF3, data, sizeof data, packet);
    assert(len == sizeof packet && packet[0] == 132);
    unsigned sum = 0;
    for (unsigned i = 0; i < len - 2; ++i) sum += packet[i];
    sum += ((unsigned)packet[len - 2] << 8) | packet[len - 1];
    assert((sum & 0xFFFF) == 0);
    assert(Ssi_Decode(packet, len, &msg));
    assert(msg.data_len == sizeof data && memcmp(msg.data, data, sizeof data) == 0);
    assert(Ssi_Encode(0, data, SSI_MAX_PAYLOAD + 1, packet) == 0);
    assert(Ssi_Encode(0, NULL, 1, packet) == 0);
    assert(Ssi_Encode(0, NULL, 0, NULL) == 0);
    puts("SSI codec: PASS");
    return 0;
}
