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

#include "task_watchdog.h"

#include "app_safety.h"
#include "cmsis_os.h"
#include "main.h"

#include <stdbool.h>

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
