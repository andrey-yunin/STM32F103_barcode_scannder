#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#define osOK 0
#define FLAG_CAN_TX 2U
typedef struct {uint32_t header;uint8_t data[8];} CanTxFrame_t;
static struct {unsigned tx_queue_overflow;} g_can_diag;
static unsigned can_tx_queueHandle=1,task_can_handleHandle=2,calls,flags;
static int status;
static CanTxFrame_t captured;
static int osMessageQueuePut(unsigned q,const void *p,unsigned priority,unsigned timeout){assert(q==1 && priority==0 && timeout==0);++calls;captured=*(const CanTxFrame_t*)p;return status;}
static unsigned osThreadFlagsSet(unsigned t,unsigned f){assert(t==2 && f==2);++flags;return f;}
static void CAN_QueueTxFrame(const CanTxFrame_t *tx_frame) {
	if (tx_frame == NULL) {
		return;
	}

	if (osMessageQueuePut(can_tx_queueHandle, tx_frame, 0U, 0U) == osOK) {
		(void) osThreadFlagsSet(task_can_handleHandle, FLAG_CAN_TX);
	} else {
		g_can_diag.tx_queue_overflow++;
	}
}
int main(void){CanTxFrame_t tx;memset(&tx,0xA5,sizeof tx);CanTxFrame_t original=tx;
 CAN_QueueTxFrame(NULL);assert(!calls && !flags && !g_can_diag.tx_queue_overflow);
 CAN_QueueTxFrame(&tx);assert(calls==1 && flags==1 && !g_can_diag.tx_queue_overflow && !memcmp(&tx,&original,sizeof tx) && !memcmp(&captured,&tx,sizeof tx));
 status=-3;CAN_QueueTxFrame(&tx);assert(calls==2 && flags==1 && g_can_diag.tx_queue_overflow==1 && !memcmp(&tx,&original,sizeof tx));
 puts("PASS: HC equality; NULL no effects, successful queue and flag, rejected queue increments overflow only, source unchanged");}
