/*
 * task_watchdog.h
 *
 *  Created on: Jul 10, 2026
 *      Author: andrey
 */

#ifndef INC_TASKS_TASK_WATCHDOG_H_
#define INC_TASKS_TASK_WATCHDOG_H_

#include <stdint.h>

#define APP_WATCHDOG_SUPERVISOR_PERIOD_MS     500U
#define APP_WATCHDOG_MAX_MISSED_CHECKS        3U
#define APP_WATCHDOG_TASK_IDLE_TIMEOUT_MS     500U

typedef enum {
	APP_WDG_CLIENT_CAN = 0,
	APP_WDG_CLIENT_DISPATCHER,
	APP_WDG_CLIENT_SCAN,
	APP_WDG_CLIENT_COUNT
} AppWatchdogClient_t;

void AppWatchdog_Heartbeat(AppWatchdogClient_t client);

void app_start_task_watchdog(void *argument);

#endif /* INC_TASKS_TASK_WATCHDOG_H_ */
