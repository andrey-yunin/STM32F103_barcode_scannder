#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#define CAN_RX_FIFO0 0U
#define HAL_OK 0
#define osOK 0
#define FLAG_CAN_RX 1U
typedef struct {uint32_t ESR;} Registers;
typedef struct {Registers *Instance;} CAN_HandleTypeDef;
typedef struct {uint32_t ExtId;} Header;
typedef struct {Header header;uint8_t data[8];} CanRxFrame_t;
static unsigned can_rx_queueHandle=1,task_can_handleHandle=2;
static unsigned queue_calls,signals,overflow,errorcalls;
static int rx_status,queue_status;
static uint32_t recorded_error,recorded_esr;
static CanRxFrame_t captured;
static int HAL_CAN_GetRxMessage(CAN_HandleTypeDef *h,unsigned fifo,Header *header,uint8_t *data){
 (void)h;assert(fifo==0);if(rx_status)return rx_status;
 header->ExtId=0x601000;memset(data,0xA5,8);return 0;
}
static int osMessageQueuePut(unsigned q,const void *frame,unsigned priority,unsigned timeout){
 assert(q==1 && priority==0 && timeout==0);++queue_calls;captured=*(const CanRxFrame_t*)frame;return queue_status;
}
static unsigned osThreadFlagsSet(unsigned thread,unsigned flags){assert(thread==2 && flags==1);++signals;return flags;}
static void CAN_Diagnostics_RecordRxQueueOverflow(void){++overflow;}
static uint32_t HAL_CAN_GetError(CAN_HandleTypeDef *h){(void)h;return 0x208;}
static void CAN_Diagnostics_RecordCanError(uint32_t error,uint32_t esr){++errorcalls;recorded_error=error;recorded_esr=esr;}
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *can_handle) {
	CanRxFrame_t rx_frame;

	if (HAL_CAN_GetRxMessage(can_handle,
	CAN_RX_FIFO0, &rx_frame.header, rx_frame.data) != HAL_OK) {
		return;
	}

	if (osMessageQueuePut(can_rx_queueHandle, &rx_frame, 0U, 0U) == osOK) {
		(void) osThreadFlagsSet(task_can_handleHandle, FLAG_CAN_RX);
	} else {
		CAN_Diagnostics_RecordRxQueueOverflow();
	}
}

// --- CAN error: retain HAL error bits and ESR without sending CAN from ISR ---
void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *can_handle) {
	CAN_Diagnostics_RecordCanError(HAL_CAN_GetError(can_handle),
			can_handle->Instance->ESR);
}


int main(void){Registers regs={0x1234};CAN_HandleTypeDef h={&regs};
 HAL_CAN_RxFifo0MsgPendingCallback(&h);
 assert(queue_calls==1 && signals==1 && !overflow && captured.header.ExtId==0x601000 && captured.data[7]==0xA5);
 queue_status=-3;HAL_CAN_RxFifo0MsgPendingCallback(&h);assert(queue_calls==2 && signals==1 && overflow==1);
 rx_status=1;HAL_CAN_RxFifo0MsgPendingCallback(&h);assert(queue_calls==2 && signals==1 && overflow==1);
 HAL_CAN_ErrorCallback(&h);assert(errorcalls==1 && recorded_error==0x208 && recorded_esr==0x1234);
 puts("PASS: callbacks match HC; raw RX queued, queue overflow counted, HAL RX failure bypasses queue, HAL error/ESR retained");
}
