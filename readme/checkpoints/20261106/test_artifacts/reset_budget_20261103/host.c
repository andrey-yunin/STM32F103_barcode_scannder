
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define GPIOA 1U
#define GPIOB 2U
#define GPIO_PIN_1 (1U<<1)
#define GPIO_PIN_2 (1U<<2)
#define GPIO_PIN_3 (1U<<3)
#define GPIO_PIN_8 (1U<<8)
#define GPIO_PIN_9 (1U<<9)
#define GPIO_PIN_10 (1U<<10)
#define GPIO_PIN_12 (1U<<12)
#define GPIO_PIN_13 (1U<<13)
#define GPIO_PIN_14 (1U<<14)
#define GPIO_PIN_15 (1U<<15)
#define GPIO_MODE_OUTPUT_PP 1U
#define GPIO_NOPULL 0U
#define GPIO_SPEED_FREQ_LOW 1U
#define GPIO_PIN_SET 1U
#define GPIO_PIN_RESET 0U
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
typedef struct {uint32_t Pin,Mode,Pull,Speed;} GPIO_InitTypeDef;
static uint32_t irq,tick,gpioa,gpiob;
static uint32_t HAL_GetTick(void){return tick;}
static uint32_t __get_PRIMASK(void){return irq;}
static void __disable_irq(void){irq=1;}
static void __set_PRIMASK(uint32_t value){irq=value;}
static void __DSB(void){}
static void HAL_GPIO_Init(unsigned port,GPIO_InitTypeDef *g){(void)port;assert(g->Mode==GPIO_MODE_OUTPUT_PP);}
static void HAL_GPIO_WritePin(unsigned port,uint32_t pins,unsigned level){
    uint32_t *value=port==GPIOA?&gpioa:&gpiob;
    if(level)*value|=pins;else *value&=~pins;
}

/*
 * app_safety.h
 *
 *  Created on: Jul 13, 2026
 *      Author: andrey
 */

#ifndef INC_APP_SAFETY_H_
#define INC_APP_SAFETY_H_

#include <stdbool.h>

void AppSafety_AllOff(void);

// --- Подготовка к reset ---
// Фиксирует запрет активации до перезапуска MCU.
void AppSafety_PrepareReset(void);

// Проверка и последующая активация требуют общей критической секции.
bool AppSafety_IsResetPending(void);

// --- Общий срок подготовки F002/F006 ---
// Три секунды от первого reset-latch; проверка не продлевает срок.
bool AppSafety_IsResetPreparationExpired(void);

#endif /* INC_APP_SAFETY_H_ */

#define STBY_Pin GPIO_PIN_1
#define STBY_GPIO_Port GPIOA
#define LED_CAN_Pin GPIO_PIN_2
#define LED_CAN_GPIO_Port GPIOA
#define LED_WORK_Pin GPIO_PIN_3
#define LED_WORK_GPIO_Port GPIOA
#define TRIG_Pin GPIO_PIN_12
#define TRIG_GPIO_Port GPIOB
#define AIMWAKE_Pin GPIO_PIN_13
#define AIMWAKE_GPIO_Port GPIOB
#define DLED_Pin GPIO_PIN_14
#define DLED_GPIO_Port GPIOB
#define BPR_Pin GPIO_PIN_15
#define BPR_GPIO_Port GPIOB
#define PWRDWN_Pin GPIO_PIN_8
#define PWRDWN_GPIO_Port GPIOA
#define RTS_Pin GPIO_PIN_9
#define RTS_GPIO_Port GPIOA
#define CTS_Pin GPIO_PIN_10
#define CTS_GPIO_Port GPIOA
#include "actual_app_safety.inc"

int main(void){
    assert(!AppSafety_IsResetPending()&&!AppSafety_IsResetPreparationExpired());
    for(unsigned saved=0;saved<=1;saved++){
        app_safety_reset_pending=false;irq=saved;tick=100;gpioa=gpiob=0;
        AppSafety_PrepareReset();
        assert(irq==saved&&AppSafety_IsResetPending());
        assert((gpioa&CTS_Pin)==CTS_Pin&&(gpiob&(TRIG_Pin|AIMWAKE_Pin))==(TRIG_Pin|AIMWAKE_Pin));
        tick=3099;assert(!AppSafety_IsResetPreparationExpired());
        AppSafety_PrepareReset();assert(irq==saved&&app_safety_reset_started==100);
        tick=3100;assert(AppSafety_IsResetPreparationExpired());
    }
    app_safety_reset_pending=false;tick=UINT32_MAX-100;irq=0;
    AppSafety_PrepareReset();tick+=2999;assert(!AppSafety_IsResetPreparationExpired());
    tick++;assert(AppSafety_IsResetPreparationExpired());
    puts("PASS actual app_safety: inactive state, PRIMASK0/1, latch/safe GPIO, 2999/3000ms, repeated request, tick wrap");
    puts("HOST HAL model only; Dispatcher/STOP not changed by this block; no BENCH");
}
