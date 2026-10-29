#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "can_protocol.h"
#define CAN_ID_EXT 4U
#define CAN_RTR_DATA 0U
typedef struct {uint32_t ExtId,IDE,RTR,DLC,TransmitGlobalTime;} Header;
typedef struct {Header header;uint8_t data[8];} CanTxFrame_t;
static CanTxFrame_t frames[8];static unsigned count;static uint32_t node=0x60;
static uint32_t AppConfig_GetPerformerID(void){return node;}
static void CAN_QueueTxFrame(CanTxFrame_t *f){assert(count<8);frames[count++]=*f;}
static void AppConfig_GetMCU_UID(uint8_t *out){for(unsigned i=0;i<12;++i)out[i]=(uint8_t)(i+1);}
void CAN_SendData(uint16_t cmd_code, uint8_t sequence_info, const uint8_t *data,
		uint8_t len) {
	CanTxFrame_t tx_frame;
	uint8_t copy_len;

	(void) cmd_code;

	memset(&tx_frame, 0, sizeof(tx_frame));

	copy_len = len;
	if (copy_len > CAN_DATA_PAYLOAD_MAX) {
		copy_len = CAN_DATA_PAYLOAD_MAX;
	}

	tx_frame.header.ExtId = CAN_BUILD_ID(CAN_PRIORITY_NORMAL,
			CAN_MSG_TYPE_DATA_DONE_LOG, CAN_ADDR_CONDUCTOR,
			AppConfig_GetPerformerID());

	tx_frame.header.IDE = CAN_ID_EXT;
	tx_frame.header.RTR = CAN_RTR_DATA;
	tx_frame.header.DLC = CAN_FRAME_DLC;

	tx_frame.data[0] = CAN_SUB_TYPE_DATA;
	tx_frame.data[1] = sequence_info;

	if ((data != NULL) && (copy_len > 0U)) {
		memcpy(&tx_frame.data[2], data, copy_len);
	}

	CAN_QueueTxFrame(&tx_frame);
}
void CAN_SendDone(uint16_t cmd_code, uint8_t device_id) {
	CanTxFrame_t tx;
	memset(&tx, 0, sizeof(tx));

	tx.header.ExtId = CAN_BUILD_ID(CAN_PRIORITY_NORMAL,
			CAN_MSG_TYPE_DATA_DONE_LOG, CAN_ADDR_CONDUCTOR,
			(uint32_t )AppConfig_GetPerformerID());

	tx.header.IDE = CAN_ID_EXT;
	tx.header.RTR = CAN_RTR_DATA;
	tx.header.DLC = 8;

	tx.data[0] = CAN_SUB_TYPE_DONE;
	tx.data[1] = (uint8_t) (cmd_code & 0xFF);
	tx.data[2] = (uint8_t) ((cmd_code >> 8) & 0xFF);
	tx.data[3] = device_id;

	CAN_QueueTxFrame(&tx);
}
static void Dispatcher_HandleGetUid(void) {
	uint8_t uid[12];

	AppConfig_GetMCU_UID(uid);

	CAN_SendData(CAN_CMD_SRV_GET_UID, 0x00U, &uid[0], 6U);

	CAN_SendData(CAN_CMD_SRV_GET_UID, (uint8_t) (CAN_DATA_SEQ_EOT_MASK | 0x01U),
			&uid[6], 6U);

	CAN_SendDone(CAN_CMD_SRV_GET_UID, 0U);
}
static void SendStatusMetric(uint16_t cmd_code, uint16_t metric_id,
		uint32_t value) {
	uint8_t data[6];

	data[0] = (uint8_t) (metric_id & 0xFF);
	data[1] = (uint8_t) ((metric_id >> 8) & 0xFF);
	data[2] = (uint8_t) (value & 0xFF);
	data[3] = (uint8_t) ((value >> 8) & 0xFF);
	data[4] = (uint8_t) ((value >> 16) & 0xFF);
	data[5] = (uint8_t) ((value >> 24) & 0xFF);

	CAN_SendData(cmd_code, CAN_DATA_SEQ_EOT_MASK, data, sizeof(data));
}
int main(void){
 Dispatcher_HandleGetUid();assert(count==3);
 assert(frames[0].data[0]==2 && frames[0].data[1]==0 && frames[1].data[1]==0x81);
 for(unsigned i=0;i<12;++i)assert(frames[i/6].data[2+i%6]==i+1);
 assert(frames[2].data[0]==1 && frames[2].data[1]==4 && frames[2].data[2]==0xF0);
 for(unsigned i=0;i<3;++i){assert(frames[i].header.DLC==8 && frames[i].header.IDE==4 && frames[i].header.RTR==0 && !frames[i].header.TransmitGlobalTime);assert(CAN_GET_SRC_ADDR(frames[i].header.ExtId)==0x60);}
 count=0;SendStatusMetric(0xF007,0x12,0x12345678);assert(count==1 && frames[0].data[1]==0x80);
 const uint8_t metric[]={0x12,0,0x78,0x56,0x34,0x12};assert(!memcmp(frames[0].data+2,metric,6));
 count=0;node=0x6F;uint8_t bytes[]={9,8,7,6,5,4,3};CAN_SendData(0xF004,0x81,bytes,2);
 assert(CAN_GET_SRC_ADDR(frames[0].header.ExtId)==0x6F && frames[0].data[2]==9 && frames[0].data[3]==8);
 for(unsigned i=4;i<8;++i)assert(!frames[0].data[i]);
 CAN_SendData(0xF004,0,bytes,7);assert(!memcmp(frames[1].data+2,bytes,6));
 CAN_SendData(0xF004,0,NULL,6);for(unsigned i=2;i<8;++i)assert(!frames[2].data[i]);
 puts("PASS: HC code equality, UID DATA00/81+DONE and bytes, F00780/LE, padding, length cap, NULL, runtime source");
}
