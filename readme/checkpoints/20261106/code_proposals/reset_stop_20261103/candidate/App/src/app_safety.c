/*
 * app_safety.c
 *
 *  Created on: Jul 13, 2026
 *      Author: andrey
 */

#include "app_safety.h"
#include "main.h"

// --- Постоянный запрет активации до перезапуска MCU ---
static volatile bool app_safety_reset_pending = false;

// --- Единый срок подготовки планового reset ---
// HAL tick используется и в UART; ожиданий и зависимости от RTOS здесь нет.
static uint32_t app_safety_reset_started;
#define APP_RESET_PREPARE_TIMEOUT_MS 3000U

/*
 * Отпускает управляющие входы сканера и выключает индикацию платы.
 * Допускает вызов до MX_GPIO_Init(), из startup и аварийных обработчиков.
 * Не использует RTOS, UART, CAN или Flash.
 *
 * Не отменяет UART и не устанавливает запрет повторной активации.
 * HIGH на TRIG не гарантирует остановку сканирования, запущенного SSI.
 */
void AppSafety_AllOff(void) {
	GPIO_InitTypeDef gpio = { 0 };

	// --- Подготовка портов для раннего и аварийного вызова ---
	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();

	// --- Неактивные уровни до переключения пинов в выходной режим ---
	HAL_GPIO_WritePin(CTS_GPIO_Port, CTS_Pin, GPIO_PIN_SET);
	HAL_GPIO_WritePin(GPIOB, TRIG_Pin | AIMWAKE_Pin, GPIO_PIN_SET);

	/*
	 * Сохраняем текущую политику платы:
	 * CAN-трансивер активен, обе LED выключены.
	 */
	HAL_GPIO_WritePin(GPIOA,
	STBY_Pin | LED_CAN_Pin | LED_WORK_Pin, GPIO_PIN_RESET);

	// --- Настройка только выходов; входные сигналы модуля не меняем ---
	gpio.Mode = GPIO_MODE_OUTPUT_PP;
	gpio.Pull = GPIO_NOPULL;
	gpio.Speed = GPIO_SPEED_FREQ_LOW;

	gpio.Pin = STBY_Pin | LED_CAN_Pin | LED_WORK_Pin | CTS_Pin;
	HAL_GPIO_Init(GPIOA, &gpio);

	gpio.Pin = TRIG_Pin | AIMWAKE_Pin;
	HAL_GPIO_Init(GPIOB, &gpio);
}

// --- Чтение состояния подготовки к reset ---
bool AppSafety_IsResetPending(void) {
	return app_safety_reset_pending;
}

// --- Атомарная фиксация запрета и аппаратный safe-state ---
// Без Flash, CAN и ожиданий RTOS. Состояние IRQ вызывающего сохраняется.
// STOP и окончательное закрытие UART после latch выполняет задача Scanner.
void AppSafety_PrepareReset(void) {
	const uint32_t saved_primask = __get_PRIMASK();
	__disable_irq();

	// Повторный вызов не продлевает уже начавшуюся подготовку.
	if (!app_safety_reset_pending)
		app_safety_reset_started = HAL_GetTick();

	app_safety_reset_pending = true;
	AppSafety_AllOff();
	__DSB();

	__set_PRIMASK(saved_primask);
}

/*
 * Проверяет единый предел подготовки. Только чтение, без ожиданий.
 * Вычитание unsigned сохраняет проверку при переполнении HAL tick.
 */
bool AppSafety_IsResetPreparationExpired(void) {
    return app_safety_reset_pending
            && (uint32_t) (HAL_GetTick() - app_safety_reset_started)
                    >= APP_RESET_PREPARE_TIMEOUT_MS;
}
