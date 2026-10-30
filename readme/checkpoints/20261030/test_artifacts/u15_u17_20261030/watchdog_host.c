#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "task_watchdog.h"
typedef int IWDG_HandleTypeDef;
IWDG_HandleTypeDef hiwdg;
enum {osOK=0,osError=-1};
static jmp_buf escape;
static uint32_t freq;
static unsigned refreshes, delays;
static int missing, failure, cycles;
static uint32_t osKernelGetTickFreq(void){return freq;}
static void Error_Handler(void){longjmp(escape,1);}
static int HAL_IWDG_Refresh(IWDG_HandleTypeDef *h){assert(h==&hiwdg);refreshes++;return 0;}
static int osDelay(uint32_t ticks){
 assert(ticks==freq);delays++;
 if(failure && delays==(unsigned)failure)return osError;
 if(delays>(unsigned)cycles)longjmp(escape,2);
 for(unsigned i=0;i<APP_WDG_CLIENT_COUNT;i++)if((int)i!=missing)AppWatchdog_Heartbeat((AppWatchdogClient_t)i);
 return osOK;
}
/*
 * task_watchdog.c
 *
 *  Created on: Jul 10, 2026
 *      Author: andrey
 *
 *  Задача Watchdog.
 *  Периодически проверяет heartbeat'ы от остальных задач.
 *  При зависании задачи — сбрасывает систему по IWDG.
 */






extern IWDG_HandleTypeDef hiwdg;

static volatile uint32_t s_heartbeats[APP_WDG_CLIENT_COUNT];

void AppWatchdog_Heartbeat(AppWatchdogClient_t client) {
	if ((uint32_t) client < (uint32_t) APP_WDG_CLIENT_COUNT) {
		s_heartbeats[client]++;
	}
}

static bool AppWatchdog_AllClientsHealthy(
		uint32_t previous[APP_WDG_CLIENT_COUNT],
		uint8_t missed[APP_WDG_CLIENT_COUNT]) {
	bool all_healthy = true;

	for (uint32_t i = 0U; i < (uint32_t) APP_WDG_CLIENT_COUNT; i++) {
		uint32_t current = s_heartbeats[i];

		if (current != previous[i]) {
			missed[i] = 0U;
		} else if (missed[i] < APP_WATCHDOG_MAX_MISSED_CHECKS) {
			missed[i]++;
		}

		if (missed[i] >= APP_WATCHDOG_MAX_MISSED_CHECKS) {
			all_healthy = false;
		}

		previous[i] = current;
	}

	return all_healthy;
}

/*
 * Supervisor по механизму HC6abc1d2.
 * Ошибка ожидания или отказ клиента завершаются через Error_Handler.
 */
void app_start_task_watchdog(void *argument)
{
    uint32_t previous[APP_WDG_CLIENT_COUNT] = {0};
    uint8_t missed[APP_WDG_CLIENT_COUNT] = {0};

    (void)argument;

    // --- Период supervisor в тиках RTOS ---
    const uint32_t period_ticks =
            (APP_WATCHDOG_SUPERVISOR_PERIOD_MS
                    * osKernelGetTickFreq()) / 1000U;

    if (period_ticks == 0U) {
        Error_Handler();
    }

    // --- Первое окно даёт клиентам время начать работу ---
    HAL_IWDG_Refresh(&hiwdg);

    for (;;) {
        // --- Проверяемое ожидание ---
        if (osDelay(period_ticks) != osOK) {
            Error_Handler();
        }

        // --- Отказ клиента прекращает штатное управление ---
        if (!AppWatchdog_AllClientsHealthy(previous, missed)) {
            Error_Handler();
        }

        // --- Обслуживание IWDG после проверки клиентов ---
        HAL_IWDG_Refresh(&hiwdg);
    }
}

static void init(void){freq=1000;refreshes=delays=0;missing=-1;failure=0;cycles=5;memset((void*)s_heartbeats,0,sizeof(s_heartbeats));}
int main(void){
 init();int result=setjmp(escape);if(!result)app_start_task_watchdog(NULL);assert(result==2 && refreshes==6 && delays==6);
 puts("PASS healthy clients: startup refresh plus five verified windows");
 for(int client=0;client<APP_WDG_CLIENT_COUNT;client++){
  init();missing=client;result=setjmp(escape);if(!result)app_start_task_watchdog(NULL);
  assert(result==1 && refreshes==1 && delays==1);
 }
 puts("PASS each of three stalled clients: fatal on first missing window, no further refresh");
 for(int attempt=1;attempt<=3;attempt++){
  init();failure=attempt;result=setjmp(escape);if(!result)app_start_task_watchdog(NULL);
  assert(result==1 && refreshes==(unsigned)attempt && delays==(unsigned)attempt);
 }
 puts("PASS immediate and later RTOS wait failure: fatal before refresh");
 init();freq=0;result=setjmp(escape);if(!result)app_start_task_watchdog(NULL);assert(result==1 && !refreshes && !delays);
 puts("PASS zero period: fatal before initial refresh");
 init();freq=100;result=setjmp(escape);if(!result)app_start_task_watchdog(NULL);assert(result==2 && refreshes==6);
 puts("PASS period converted from milliseconds at 100Hz and 1000Hz");
 init();uint32_t prev[APP_WDG_CLIENT_COUNT];uint8_t missed[APP_WDG_CLIENT_COUNT]={0};
 for(unsigned i=0;i<APP_WDG_CLIENT_COUNT;i++){s_heartbeats[i]=UINT32_MAX;prev[i]=UINT32_MAX;AppWatchdog_Heartbeat((AppWatchdogClient_t)i);}
 assert(AppWatchdog_AllClientsHealthy(prev,missed));assert(!AppWatchdog_AllClientsHealthy(prev,missed));
 puts("PASS counter wrap represents progress; unchanged counters fail next window");
 init();AppWatchdog_Heartbeat((AppWatchdogClient_t)-1);AppWatchdog_Heartbeat(APP_WDG_CLIENT_COUNT);
 for(unsigned i=0;i<APP_WDG_CLIENT_COUNT;i++)assert(!s_heartbeats[i]);
 puts("PASS invalid heartbeat client cannot modify counters");
 puts("HOST PASS: actual supervisor/helper functions; mocked RTOS/IWDG. Real preemption, timing, safe-state and reset remain BENCH.");
}
