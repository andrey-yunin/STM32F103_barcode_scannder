#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#define osOK 0
#define osErrorTimeout -2
#define APP_WATCHDOG_TASK_IDLE_TIMEOUT_MS 500U
#define APP_WDG_CLIENT_DISPATCHER 1U
typedef int osStatus_t;
typedef struct {uint16_t cmd_code;} ParsedCanCommand_t;
static unsigned dispatcher_queueHandle,waits,beats,acks,diags,routed;
static int status;
static bool consume;
static jmp_buf stop;
static uint32_t osKernelGetTickFreq(void){return 1000;}
static osStatus_t osMessageQueueGet(unsigned q,ParsedCanCommand_t *p,void *prio,uint32_t ticks){
 (void)q;assert(prio==NULL && ticks==500);
 if(waits++)longjmp(stop,1);
 if(status==osOK)p->cmd_code=0xF020;
 return status;
}
static void AppWatchdog_Heartbeat(unsigned c){assert(c==1);++beats;}
static void CAN_SendAck(uint16_t c){assert(c==0xF020 && beats==1 && !diags);++acks;}
static bool HandleEngineeringDiagnostics(const ParsedCanCommand_t *p){assert(p->cmd_code==0xF020 && acks==1);++diags;return consume;}
static void run(void){ParsedCanCommand_t parsed;
const uint32_t idle_wait_ticks = (APP_WATCHDOG_TASK_IDLE_TIMEOUT_MS
			* osKernelGetTickFreq()) / 1000U;

	for (;;) {
		osStatus_t queue_status;

		// --- Ожидание команды ---
		queue_status = osMessageQueueGet(dispatcher_queueHandle, &parsed,
		NULL, idle_wait_ticks);

		// Пустая очередь — штатный режим, задача продолжает исполняться.
		if (queue_status == osErrorTimeout) {
			AppWatchdog_Heartbeat(APP_WDG_CLIENT_DISPATCHER);
			continue;
		}

		// Ошибка очереди не подтверждает прогресс.
		// При ошибке содержимое parsed не обрабатываем.
		if (queue_status != osOK) {
			continue;
		}

		// --- Команда получена и передаётся на обработку ---
		// Heartbeat не означает завершение команды.
		AppWatchdog_Heartbeat(APP_WDG_CLIENT_DISPATCHER);

		// --- ACK перед прикладной обработкой ---
		CAN_SendAck(parsed.cmd_code);

		// --- Общая точка входа инженерной диагностики ---
		// Обработанная сервисная команда не должна попадать в switch.
		if (HandleEngineeringDiagnostics(&parsed)) {
			continue;
		}

		++routed;break;
}}
int main(void){int cases[]={0,-2,-3,-4};
 for(unsigned i=0;i<4;++i)for(unsigned mode=0;mode<2;++mode){
  status=cases[i];consume=mode;waits=beats=acks=diags=routed=0;
  if(!setjmp(stop))run();
  assert(beats==(i<2?1U:0U));assert(acks==(i==0?1U:0U));
  assert(diags==(i==0?1U:0U));assert(routed==((i==0&&!consume)?1U:0U));
 }
 puts("PASS: HC wait/heartbeat equality; OK/timeout/Resource/Parameter; ACK before diagnostics; consumed diagnostic bypasses switch");
}
