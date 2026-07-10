/*
 * task_scan_controller.c
 *
 *  Created on: Jul 10, 2026
 *      Author: andrey
 *
 *  Задача Scan Controller.
 *  Управляет сканером SE-965HP через USART3 (SSI-протокол).
 *  Получает команды из scan_queue (START/STOP сканирования),
 *  отправляет результат сканирования через dispatcher в CAN.
 */

#include "task_scan_controller.h"

#include "cmsis_os.h"
#include "app_queues.h"
#include "app_config.h"

void app_start_task_scan_controller(void *argument)
{
    (void)argument;

    for (;;) {
        osDelay(1);
    }
}
