/*
 * board_safety.c
 *
 *  Created on: Jul 13, 2026
 *      Author: andrey
 */


#include "board_safety.h"
#include "main.h"

void Board_EnterSafeState(void)
{
    GPIO_InitTypeDef gpio = {0};

    /*
     * Must be callable from startup, fault handlers and Error_Handler —
     * no FreeRTOS, CAN, UART or Flash dependencies.
     */
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /*
     * Safe-state GPIO levels:
     * - STBY LOW: CAN transceiver active (normal operation)
     * - LED_CAN LOW: LED off
     * - LED_WORK LOW: LED off
     *
     * Preload output data before switching pins to output mode.
     */
    HAL_GPIO_WritePin(GPIOA,
                      STBY_Pin | LED_CAN_Pin | LED_WORK_Pin,
                      GPIO_PIN_RESET);

    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = STBY_Pin | LED_CAN_Pin | LED_WORK_Pin;
    HAL_GPIO_Init(GPIOA, &gpio);

    HAL_GPIO_WritePin(GPIOA,
                      STBY_Pin | LED_CAN_Pin | LED_WORK_Pin,
                      GPIO_PIN_RESET);
}
