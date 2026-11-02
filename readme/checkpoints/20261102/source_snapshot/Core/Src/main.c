/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_queues.h"
#include "app_config.h"
#include "task_can_handler.h"
#include "task_dispatcher.h"
#include "task_scan_controller.h"
#include "app_flash.h"
#include "task_watchdog.h"
#include "app_safety.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
CAN_HandleTypeDef hcan;

IWDG_HandleTypeDef hiwdg;

UART_HandleTypeDef huart3;

/* Definitions for task_can_handle */
osThreadId_t task_can_handleHandle;
const osThreadAttr_t task_can_handle_attributes = {
  .name = "task_can_handle",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for task_dispatcher */
osThreadId_t task_dispatcherHandle;
const osThreadAttr_t task_dispatcher_attributes = {
  .name = "task_dispatcher",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for task_scan_contr */
osThreadId_t task_scan_contrHandle;
const osThreadAttr_t task_scan_contr_attributes = {
  .name = "task_scan_contr",
  .stack_size = 384 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for task_watchdog */
osThreadId_t task_watchdogHandle;
const osThreadAttr_t task_watchdog_attributes = {
  .name = "task_watchdog",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityRealtime,
};
/* USER CODE BEGIN PV */

osMessageQueueId_t can_rx_queueHandle;
osMessageQueueId_t can_tx_queueHandle;
osMessageQueueId_t dispatcher_queueHandle;
osMessageQueueId_t scan_queueHandle;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_CAN_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_IWDG_Init(void);
void start_task_can_handler(void *argument);
void start_task_dispatcher(void *argument);
void task_scan_controller(void *argument);
void start_task_watchdog(void *argument);

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_CAN_Init();
  MX_USART3_UART_Init();
  MX_IWDG_Init();
  /* USER CODE BEGIN 2 */
	AppConfig_Init();
	AppSafety_AllOff();

  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
	/* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
	/* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
	/* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
	can_rx_queueHandle = osMessageQueueNew(CAN_RX_QUEUE_LEN,
			sizeof(CanRxFrame_t), NULL);
	can_tx_queueHandle = osMessageQueueNew(CAN_TX_QUEUE_LEN,
			sizeof(CanTxFrame_t), NULL);
	dispatcher_queueHandle = osMessageQueueNew(DISPATCHER_QUEUE_LEN,
			sizeof(ParsedCanCommand_t), NULL);
	scan_queueHandle = osMessageQueueNew(SCAN_QUEUE_LEN, sizeof(ScanResult_t),
	NULL);

	// --- Проверка создания обязательных очередей ---
	/*
	 * Все четыре очереди необходимы для работы Executor.
	 * При отказе прекращаем запуск до создания задач.
	 */
	if ((can_rx_queueHandle == NULL) || (can_tx_queueHandle == NULL)
			|| (dispatcher_queueHandle == NULL) || (scan_queueHandle == NULL)) {
		AppSafety_AllOff();
		Error_Handler();
	}
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of task_can_handle */
  task_can_handleHandle = osThreadNew(start_task_can_handler, NULL, &task_can_handle_attributes);

  /* creation of task_dispatcher */
  task_dispatcherHandle = osThreadNew(start_task_dispatcher, NULL, &task_dispatcher_attributes);

  /* creation of task_scan_contr */
  task_scan_contrHandle = osThreadNew(task_scan_controller, NULL, &task_scan_contr_attributes);

  /* creation of task_watchdog */
  task_watchdogHandle = osThreadNew(start_task_watchdog, NULL, &task_watchdog_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
	/* add threads, ... */
	// --- Проверка создания обязательных задач ---
	/*
	 * До запуска планировщика проверяем полный набор задач.
	 * При отказе прекращаем запуск Executor через safe-state.
	 */
	if ((task_watchdogHandle == NULL) || (task_can_handleHandle == NULL)
			|| (task_dispatcherHandle == NULL)
			|| (task_scan_contrHandle == NULL)) {
		AppSafety_AllOff();
		Error_Handler();
	}
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
	/* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
	while (1) {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	}
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL16;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief CAN Initialization Function
  * @param None
  * @retval None
  */
static void MX_CAN_Init(void)
{

  /* USER CODE BEGIN CAN_Init 0 */

  /* USER CODE END CAN_Init 0 */

  /* USER CODE BEGIN CAN_Init 1 */

  /* USER CODE END CAN_Init 1 */
  hcan.Instance = CAN1;
  hcan.Init.Prescaler = 2;
  hcan.Init.Mode = CAN_MODE_NORMAL;
  hcan.Init.SyncJumpWidth = CAN_SJW_1TQ;
  hcan.Init.TimeSeg1 = CAN_BS1_11TQ;
  hcan.Init.TimeSeg2 = CAN_BS2_4TQ;
  hcan.Init.TimeTriggeredMode = DISABLE;
  hcan.Init.AutoBusOff = DISABLE;
  hcan.Init.AutoWakeUp = DISABLE;
  hcan.Init.AutoRetransmission = ENABLE;
  hcan.Init.ReceiveFifoLocked = DISABLE;
  hcan.Init.TransmitFifoPriority = ENABLE;
  if (HAL_CAN_Init(&hcan) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CAN_Init 2 */

  /* USER CODE END CAN_Init 2 */

}

/**
  * @brief IWDG Initialization Function
  * @param None
  * @retval None
  */
static void MX_IWDG_Init(void)
{

  /* USER CODE BEGIN IWDG_Init 0 */

  /* USER CODE END IWDG_Init 0 */

  /* USER CODE BEGIN IWDG_Init 1 */

  /* USER CODE END IWDG_Init 1 */
  hiwdg.Instance = IWDG;
  hiwdg.Init.Prescaler = IWDG_PRESCALER_256;
  hiwdg.Init.Reload = 624;
  if (HAL_IWDG_Init(&hiwdg) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN IWDG_Init 2 */

  /* USER CODE END IWDG_Init 2 */

}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 9600;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, STBY_Pin|LED_CAN_Pin|LED_WORK_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, TRIG_Pin|AIMWAKE_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(CTS_GPIO_Port, CTS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pins : STBY_Pin LED_CAN_Pin LED_WORK_Pin CTS_Pin */
  GPIO_InitStruct.Pin = STBY_Pin|LED_CAN_Pin|LED_WORK_Pin|CTS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : TRIG_Pin AIMWAKE_Pin */
  GPIO_InitStruct.Pin = TRIG_Pin|AIMWAKE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : DLED_Pin BPR_Pin */
  GPIO_InitStruct.Pin = DLED_Pin|BPR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : PWRDWN_Pin RTS_Pin */
  GPIO_InitStruct.Pin = PWRDWN_Pin|RTS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/* USER CODE BEGIN Header_start_task_can_handler */
/**
 * @brief  Function implementing the task_can_handle thread.
 * @param  argument: Not used
 * @retval None
 */
/* USER CODE END Header_start_task_can_handler */
void start_task_can_handler(void *argument)
{
  /* USER CODE BEGIN 5 */
	app_start_task_can_handler(argument);
	/* Infinite loop */
	for (;;) {
		osDelay(1);
	}
  /* USER CODE END 5 */
}

/* USER CODE BEGIN Header_start_task_dispatcher */
/**
 * @brief Function implementing the task_dispatcher thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_start_task_dispatcher */
void start_task_dispatcher(void *argument)
{
  /* USER CODE BEGIN start_task_dispatcher */
	app_start_task_dispatcher(argument);
	/* Infinite loop */
	for (;;) {
		osDelay(1);
	}
  /* USER CODE END start_task_dispatcher */
}

/* USER CODE BEGIN Header_task_scan_controller */
/**
 * @brief Function implementing the task_scan_contr thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_task_scan_controller */
void task_scan_controller(void *argument)
{
  /* USER CODE BEGIN task_scan_controller */
	app_start_task_scan_controller(argument);
	/* Infinite loop */
	for (;;) {
		osDelay(1);
	}
  /* USER CODE END task_scan_controller */
}

/* USER CODE BEGIN Header_start_task_watchdog */
/**
 * @brief Function implementing the task_watchdog thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_start_task_watchdog */
void start_task_watchdog(void *argument)
{
  /* USER CODE BEGIN start_task_watchdog */
	app_start_task_watchdog(argument);
	/* Infinite loop */
	for (;;) {
		osDelay(1);
	}
  /* USER CODE END start_task_watchdog */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM4 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM4)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
	/* User can add his own implementation to report the HAL error return state */

	// --- Терминальный аварийный останов ---
	/*
	 * Запрещаем переключение задач до отпускания управляющих линий.
	 * Штатное управление не возобновляется.
	 * RTOS, CAN и обслуживание IWDG здесь не используются.
	 */
	__disable_irq();
	AppSafety_AllOff();
	__DSB();
	__ISB();

	// --- Ожидание аппаратного сброса ---
	/*
	 * Запущенный IWDG выполнит reset по своему таймауту.
	 * До запуска IWDG ожидаем внешнего сброса.
	 */
	while (1) {
	}
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
