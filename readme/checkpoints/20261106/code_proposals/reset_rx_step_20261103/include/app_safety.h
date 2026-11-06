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
