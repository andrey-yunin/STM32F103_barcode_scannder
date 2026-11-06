/*
 * task_scan_controller.h
 *
 *  Created on: Jul 10, 2026
 *      Author: andrey
 */

#ifndef INC_TASKS_TASK_SCAN_CONTROLLER_H_
#define INC_TASKS_TASK_SCAN_CONTROLLER_H_

#include <stdbool.h>

// --- Подтверждение доменной подготовки к reset ---
// Устанавливается задачей Scanner после подтверждённого STOP и закрытия RX.
bool ScanTask_IsResetPrepared(void);

void app_start_task_scan_controller(void *argument);

#endif /* INC_TASKS_TASK_SCAN_CONTROLLER_H_ */
