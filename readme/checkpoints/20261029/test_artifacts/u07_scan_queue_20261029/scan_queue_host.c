#include <stdint.h>
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#define APP_WATCHDOG_TASK_IDLE_TIMEOUT_MS 500U
#define APP_WDG_CLIENT_SCAN 2U
#define osErrorTimeout -2
#define osOK 0
typedef int osStatus_t;
typedef struct {uint16_t cmd_code;} ScanResult_t;
static unsigned scan_queueHandle,waits,beats,processed;
static int status;
static jmp_buf end;
static uint32_t osKernelGetTickFreq(void){return 1000;}
static osStatus_t osMessageQueueGet(unsigned q,ScanResult_t *c,void *prio,uint32_t ticks){
 (void)q;assert(prio==NULL && ticks==500);if(waits++)longjmp(end,1);
 if(status==osOK){c->cmd_code=0x0601;}return status;
}
static void AppWatchdog_Heartbeat(unsigned c){assert(c==2);++beats;}
static void run(void){ScanResult_t cmd;
const uint32_t idle_wait_ticks = (APP_WATCHDOG_TASK_IDLE_TIMEOUT_MS
			* osKernelGetTickFreq()) / 1000U;

	for (;;) {
		osStatus_t queue_status;

		// --- Ожидание команды от Dispatcher ---
		queue_status = osMessageQueueGet(scan_queueHandle, &cmd,
		NULL, idle_wait_ticks);

		// Пустая очередь — штатный режим, задача исполняется.
		if (queue_status == osErrorTimeout) {
			AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
			continue;
		}

		// Ошибка очереди не подтверждает прогресс.
		// Содержимое cmd при ошибке не обрабатываем.
		if (queue_status != osOK) {
			continue;
		}

		// --- Команда получена и передаётся на обработку ---
		AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);

		assert(cmd.cmd_code==0x0601);++processed;break;
}}
int main(void){int states[]={0,-2,-3,-4};
 for(unsigned i=0;i<4;++i){status=states[i];waits=beats=processed=0;
 if(!setjmp(end)){run();}assert(beats==(i<2?1U:0U));assert(processed==(i==0?1U:0U));}
 puts("PASS: actual scan queue wait matches HC with named domain adaptations; OK/timeout/Resource/Parameter paths");
}
