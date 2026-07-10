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

#include "cmsis_os.h"
#include "app_queues.h"
#include "app_config.h"

void app_start_task_watchdog(void *argument)
{
    (void)argument;

    for (;;) {
        osDelay(1);
    }
}
