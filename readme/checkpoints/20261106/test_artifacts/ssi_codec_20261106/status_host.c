#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "scan_ssi.h"
int main(void) {
    const uint8_t start[]={4,0xE4,4,0,0xFF,0x14};
    const uint8_t repeat[]={4,0xE4,4,1,0xFF,0x13};
    const uint8_t flush[]={4,0xD2,4,0,0xFF,0x26};
    const uint8_t nak[]={5,0xD1,4,0,1,0xFF,0x25};
    uint8_t bytes[SSI_PACKET_MAX_LEN], ordinary[SSI_PACKET_MAX_LEN];
    assert(Ssi_Encode(SSI_OP_START_DECODE,NULL,0,bytes)==sizeof(start));
    assert(!memcmp(bytes,start,sizeof(start)));
    assert(Ssi_EncodeWithStatus(SSI_OP_START_DECODE,SSI_STATUS_RETRANSMIT,NULL,0,bytes)==sizeof(repeat));
    assert(!memcmp(bytes,repeat,sizeof(repeat)));
    assert(Ssi_Encode(SSI_OP_FLUSH_QUEUE,NULL,0,bytes)==sizeof(flush));
    assert(!memcmp(bytes,flush,sizeof(flush)));
    const uint8_t cause=SSI_NAK_RESEND;
    assert(Ssi_Encode(SSI_OP_CMD_NAK,&cause,1,bytes)==sizeof(nak));
    assert(!memcmp(bytes,nak,sizeof(nak)));
    puts("PASS independent golden vectors: unchanged START, retransmitted START, FLUSH_QUEUE, SSI NAK RESEND");
    uint8_t payload[SSI_MAX_PAYLOAD];memset(payload,0xFF,sizeof(payload));
    unsigned len=Ssi_EncodeWithStatus(SSI_OP_START_DECODE,SSI_STATUS_RETRANSMIT,payload,sizeof(payload),bytes);
    assert(len==SSI_PACKET_MAX_LEN&&bytes[0]==4+SSI_MAX_PAYLOAD&&bytes[3]==SSI_STATUS_RETRANSMIT);
    uint16_t sum=0;for(unsigned i=0;i<len-2;i++)sum=(uint16_t)(sum+bytes[i]);
    sum=(uint16_t)(sum+((uint16_t)bytes[len-2]<<8)+bytes[len-1]);assert(sum==0);
    SsiMsg_t msg;assert(Ssi_Decode(bytes,len,&msg));
    assert(msg.status==SSI_STATUS_RETRANSMIT&&msg.data_len==sizeof(payload)&&!memcmp(msg.data,payload,sizeof(payload)));
    len=Ssi_Encode(SSI_OP_DECODE_DATA,payload,sizeof(payload),ordinary);
    assert(Ssi_EncodeWithStatus(SSI_OP_DECODE_DATA,0,payload,sizeof(payload),bytes)==len);
    assert(!memcmp(ordinary,bytes,len));
    assert(!Ssi_EncodeWithStatus(0,0,NULL,1,bytes));
    assert(!Ssi_EncodeWithStatus(0,0,payload,SSI_MAX_PAYLOAD+1,bytes));
    assert(!Ssi_EncodeWithStatus(0,0,NULL,0,NULL));
    puts("PASS explicit status participates in checksum; maximum payload; status0 equivalence; invalid arguments");
    return 0;
}
