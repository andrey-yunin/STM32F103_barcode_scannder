#include <stdint.h>
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#define FLAG_CAN_RX 1U
#define FLAG_CAN_TX 2U
#define osFlagsWaitAny 0U
#define osFlagsError 0x80000000U
#define osFlagsErrorTimeout 0xFFFFFFFEU
#define APP_WATCHDOG_TASK_IDLE_TIMEOUT_MS 500U
#define APP_WDG_CLIENT_CAN 0U
static jmp_buf exit_wait;
static uint32_t response;
static unsigned calls,beats,processed;
static uint32_t osKernelGetTickFreq(void){return 1000;}
static uint32_t osThreadFlagsWait(uint32_t f,uint32_t o,uint32_t ticks){
 assert(f==3 && o==0 && ticks==500);
 if(calls++)longjmp(exit_wait,1);
 return response;
}
static void AppWatchdog_Heartbeat(unsigned client){assert(client==0);++beats;}
static void run(void){
const uint32_t idle_wait_ticks = (APP_WATCHDOG_TASK_IDLE_TIMEOUT_MS
			* osKernelGetTickFreq()) / 1000U;

	// --- Основной цикл приёма (event-driven, RX + TX) ---
	for (;;) {
		uint32_t flags;

		// --- Ожидание события или штатное idle-пробуждение ---
		flags = osThreadFlagsWait(
		FLAG_CAN_RX | FLAG_CAN_TX,
		osFlagsWaitAny, idle_wait_ticks);

		// Отсутствие трафика допустимо: задача исполняется.
		if (flags == osFlagsErrorTimeout) {
			AppWatchdog_Heartbeat(APP_WDG_CLIENT_CAN);
			continue;
		}

		// Ошибочное ожидание не подтверждает прогресс задачи.
		if ((flags & osFlagsError) != 0U) {
			continue;
		}

		// Событие получено; далее задача должна обработать RX/TX.
		// Если обработка зависнет, следующие heartbeat не появятся.
		AppWatchdog_Heartbeat(APP_WDG_CLIENT_CAN);

		++processed;break;
}}
int main(void){
 uint32_t cases[]={1U,2U,3U,osFlagsErrorTimeout,0xFFFFFFFDU,0xFFFFFFFCU};
 for(unsigned i=0;i<6;++i){calls=beats=processed=0;response=cases[i];
 if(!setjmp(exit_wait))run();
 assert(beats==(i<4?1U:0U));assert(processed==(i<3?1U:0U));}
 puts("PASS: actual CAN wait block matches HC; RX/TX/both and idle timeout heartbeat; Resource/Parameter errors do not heartbeat or enter processing");
}
