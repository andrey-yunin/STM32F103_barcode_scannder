/*
 * task_can_handler.h
 *
 *  Created on: Jul 10, 2026
 *      Author: andrey
 */

#ifndef INC_TASKS_TASK_CAN_HANDLER_H_
#define INC_TASKS_TASK_CAN_HANDLER_H_


#include <stdint.h>

// --- Обновление direct-фильтра при изменении RAM NodeID ---
void CAN_UpdateDirectFilter(uint8_t destination);

void app_start_task_can_handler(void *argument);

#endif /* INC_TASKS_TASK_CAN_HANDLER_H_ */
