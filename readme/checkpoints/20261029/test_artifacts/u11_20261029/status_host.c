#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "can_protocol.h"
static CanDiagnostics_t live;
static unsigned snapshots,count,done;
static uint8_t payloads[18][6];
void CAN_Diagnostics_GetSnapshot(CanDiagnostics_t *p){++snapshots;*p=live;}
void CAN_SendData(uint16_t cmd,uint8_t seq,const uint8_t *data,uint8_t len){
 assert(cmd==0xF007 && seq==0x80 && len==6 && count<18);
 memcpy(payloads[count++],data,6);memset(&live,0xFF,sizeof live);
}
void CAN_SendDone(uint16_t cmd,uint8_t selector){assert(cmd==0xF007 && selector==0 && count==18);++done;}
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
static void Dispatcher_HandleGetStatus(void) {
	CanDiagnostics_t diagnostics;

	CAN_Diagnostics_GetSnapshot(&diagnostics);

	SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_RX_TOTAL, diagnostics.rx_total);

	SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_TX_TOTAL, diagnostics.tx_total);

	SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_RX_QUEUE_OVERFLOW, diagnostics.rx_queue_overflow);

	SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_TX_QUEUE_OVERFLOW, diagnostics.tx_queue_overflow);

	SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_DISPATCHER_OVERFLOW, diagnostics.dispatcher_queue_overflow);

	SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_DROP_NOT_EXT, diagnostics.dropped_not_ext);

	SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_DROP_WRONG_DST, diagnostics.dropped_wrong_dst);

	SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_DROP_WRONG_TYPE, diagnostics.dropped_wrong_type);

	SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_DROP_WRONG_DLC, diagnostics.dropped_wrong_dlc);

	SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_TX_MAILBOX_TIMEOUT, diagnostics.tx_mailbox_timeout);

	SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_TX_HAL_ERROR, diagnostics.tx_hal_error);

	SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_ERROR_CALLBACK, diagnostics.can_error_callback_count);

	SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_ERROR_WARNING, diagnostics.error_warning_count);

	SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_ERROR_PASSIVE, diagnostics.error_passive_count);

	SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_BUS_OFF, diagnostics.bus_off_count);

	SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_LAST_HAL_ERROR, diagnostics.last_hal_error);

	SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_LAST_ESR, diagnostics.last_esr);

	SendStatusMetric(CAN_CMD_SRV_GET_STATUS,
	CAN_STATUS_APP_QUEUE_OVERFLOW, diagnostics.app_queue_overflow);

	CAN_SendDone(CAN_CMD_SRV_GET_STATUS, 0U);
}
int main(void){
live.rx_total=0x12340000U+1U;
live.tx_total=0x12340000U+2U;
live.rx_queue_overflow=0x12340000U+3U;
live.tx_queue_overflow=0x12340000U+4U;
live.dispatcher_queue_overflow=0x12340000U+5U;
live.dropped_not_ext=0x12340000U+6U;
live.dropped_wrong_dst=0x12340000U+7U;
live.dropped_wrong_type=0x12340000U+8U;
live.dropped_wrong_dlc=0x12340000U+9U;
live.tx_mailbox_timeout=0x12340000U+10U;
live.tx_hal_error=0x12340000U+11U;
live.can_error_callback_count=0x12340000U+12U;
live.error_warning_count=0x12340000U+13U;
live.error_passive_count=0x12340000U+14U;
live.bus_off_count=0x12340000U+15U;
live.last_hal_error=0x12340000U+16U;
live.last_esr=0x12340000U+17U;
live.app_queue_overflow=0x12340000U+18U;
 Dispatcher_HandleGetStatus();assert(snapshots==1 && count==18 && done==1);
 for(unsigned i=0;i<18;++i){uint8_t *b=payloads[i];assert(b[0]==i+1 && b[1]==0);
 uint32_t v=(uint32_t)b[2]|((uint32_t)b[3]<<8)|((uint32_t)b[4]<<16)|((uint32_t)b[5]<<24);
 assert(v==0x12340000U+i+1);}
 puts("PASS: HC status handler with existing packer name; 18 IDs/order/LE, seq80, one immutable snapshot, DONE last selector0");}
