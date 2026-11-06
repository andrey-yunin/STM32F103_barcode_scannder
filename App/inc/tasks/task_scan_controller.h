/*
 * task_scan_controller.h
 *
 *  Created on: Jul 10, 2026
 *      Author: andrey
 */

#ifndef INC_TASKS_TASK_SCAN_CONTROLLER_H_
#define INC_TASKS_TASK_SCAN_CONTROLLER_H_

#include <stdbool.h>

// --- Учёт queued/in-flight команд для допуска F003 ---
// Reserve вызывается Dispatcher до enqueue; Release — при отказе enqueue
// либо задачей Scanner после полной обработки. Busy читает только Dispatcher:
// он единственный producer scan_queue и не ставит команды во время Commit.
void ScanTask_ReserveCommand(void);
void ScanTask_ReleaseCommand(void);
bool ScanTask_IsFlashCommitBusy(void);

// --- Подтверждение доменной подготовки к reset ---
// Устанавливается задачей Scanner после подтверждённого STOP и закрытия RX.
bool ScanTask_IsResetPrepared(void);

void app_start_task_scan_controller(void *argument);

#endif /* INC_TASKS_TASK_SCAN_CONTROLLER_H_ */
