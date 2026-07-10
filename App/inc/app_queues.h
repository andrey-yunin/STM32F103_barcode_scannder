#ifndef APP_QUEUES_H_
#define APP_QUEUES_H_

#include "cmsis_os.h"

extern osMessageQueueId_t can_rx_queueHandle;
extern osMessageQueueId_t can_tx_queueHandle;
extern osMessageQueueId_t dispatcher_queueHandle;
extern osMessageQueueId_t scan_queueHandle;

extern osThreadId_t task_can_handleHandle;

#endif
