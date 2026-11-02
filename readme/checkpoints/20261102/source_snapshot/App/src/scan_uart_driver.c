/*
 * scan_uart_driver.c
 *
 *  Created on: Jul 13, 2026
 *      Author: andrey
 */

#include "scan_uart_driver.h"
#include "main.h"
#include "app_queues.h"
#include "app_config.h"
#include "task_watchdog.h"
#include <string.h>
#include <cmsis_os.h>
#include "app_safety.h"

extern UART_HandleTypeDef huart3;

// --- Побайтовый приём UART и накопитель непрочитанных данных ---
// ISR добавляет байты текущего обмена.
// Задача забирает накопленное и освобождает место под защитой PRIMASK.
static uint8_t rx_byte;
static uint8_t holding_buf[SCAN_UART_PENDING_SIZE];
static volatile uint16_t holding_len;
static volatile bool read_active;
static volatile bool read_error;

// --- Независимый сырой захват для диагностических команд ---
static ScanUartCapture_t capture;
static volatile bool capture_active;

// --- Capture ownership changes only in scan-task context with IRQs masked ---
void ScanUart_BeginCapture(void) {
	uint32_t irq_state = __get_PRIMASK();
	__disable_irq();
	memset(&capture, 0, sizeof(capture));
	capture_active = true;
	__set_PRIMASK(irq_state);
}

void ScanUart_EndCapture(ScanUartCapture_t *out) {
	uint32_t irq_state = __get_PRIMASK();
	__disable_irq();
	capture_active = false;
	if (out != NULL)
		*out = capture;
	__set_PRIMASK(irq_state);
}

// --- Diagnostic storage is separate from the SSI/domain holding buffer ---
static ScanUartDiagnostics_t rx_diag;
static uint8_t rx_arm_status = 0xFFU;
static uint8_t rx_event_type = 0xFFU;

// --- One retained START attempt and ownership of the in-flight TX completion ---
static ScanUartTxDiagnostics_t start_tx = { .state = 0xFFU };
static uint32_t active_start_sequence;
static volatile bool tx_byte_done;
static bool tx_last_byte;

// --- Read-only snapshot; callbacks cannot change the copy while IRQs are masked ---
void ScanUart_GetTxDiagnostics(ScanUartTxDiagnostics_t *out) {
	if (out == NULL)
		return;
	uint32_t irq_state = __get_PRIMASK();
	__disable_irq();
	*out = start_tx;
	__set_PRIMASK(irq_state);
}

// --- HAL completion belongs only to a successfully accepted tracked transfer ---
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
	if (huart->Instance != USART3)
		return;
	if (active_start_sequence != 0U
			&& active_start_sequence == start_tx.sequence) {
		start_tx.complete_tick = HAL_GetTick();
		start_tx.state |= 0x100U;
		start_tx.error_mask |= huart->ErrorCode;
	}
	active_start_sequence = 0U;
	tx_byte_done = true;
	if (tx_last_byte)
		HAL_GPIO_WritePin(CTS_GPIO_Port, CTS_Pin, GPIO_PIN_SET);
}

// --- Read-only task snapshot; UART callbacks are excluded during copying ---
void ScanUart_GetDiagnostics(ScanUartDiagnostics_t *out) {
	if (out == NULL)
		return;
	uint32_t irq_state = __get_PRIMASK();
	__disable_irq();
	*out = rx_diag;
	out->baud = huart3.Init.BaudRate;
	out->state = (uint32_t) rx_arm_status | ((uint32_t) huart3.RxState << 8)
			| ((uint32_t) rx_event_type << 16);
	__set_PRIMASK(irq_state);
}

// --- Диагностика последнего завершённого побайтового приёма ---
// D501 сохраняет формат. Последний принятый байт находится в bytes[0].
// Остальные байты статического снимка остаются нулевыми.
static void ScanUart_RecordRx(uint8_t byte) {
	if (++rx_diag.sequence == 0U)
		++rx_diag.sequence;

	rx_diag.tick_ms = HAL_GetTick();
	rx_diag.length = 1U;
	rx_event_type = 0U; // TC: завершён HAL_UART_Receive_IT(..., 1).
	rx_diag.bytes[0] = byte;
	rx_diag.error_mask |= huart3.ErrorCode;
}

// --- Неблокирующее уведомление задачи о доступных данных ---
// Отказ уведомления сохраняется как ошибка текущего приёма.
// Задача проверяет состояние накопителя независимо от числа уведомлений.
static void ScanUart_NotifyRead(void) {
	if (read_active
			&& (osThreadFlagsSet(task_scan_contrHandle, FLAG_SCAN_UART_RX)
					& osFlagsError) != 0U)
		read_error = true;
}

// --- Сохранение ошибки до повторного запуска UART RX ---
// HAL может остановить приём при ошибке.
// Продолжающийся приём не перезапускаем.
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
	if (huart->Instance != USART3)
		return;

	rx_diag.error_mask |= huart->ErrorCode;

	if (capture_active)
		capture.errors |= huart->ErrorCode;

	if (active_start_sequence != 0U
			&& active_start_sequence == start_tx.sequence)
		start_tx.error_mask |= huart->ErrorCode;

	if (rx_diag.error_count != UINT32_MAX)
		++rx_diag.error_count;

	if (AppSafety_IsResetPending())
		return;

	if (read_active)
		read_error = true;

	if (huart->RxState == HAL_UART_STATE_READY)
		(void) ScanUart_StartRx();

	ScanUart_NotifyRead();
}

/*
 * Отменяет RX после установки reset-latch.
 * Вызывает задача Scanner; диагностика сохраняется.
 * Latch запрещает повторный запуск из позднего callback.
 */
bool ScanUart_CancelRx(void) {
	// --- Закрытие сырого захвата ---
	const uint32_t saved_primask = __get_PRIMASK();
	__disable_irq();

	capture_active = false;
	__DMB();

	__set_PRIMASK(saved_primask);

	// --- Остановка побайтового UART RX ---
	HAL_StatusTypeDef status = HAL_UART_AbortReceive(&huart3);

	// --- Аннулирование данных отменённого обмена ---
	// После latch callback эти поля больше не обновляет.
	read_active = false;
	holding_len = 0U;
	read_error = false;

	if (status != HAL_OK)
		return false;

	return true;
}

// --- Инициализация приёма; накопление ответа пока закрыто ---
bool ScanUart_Init(void) {
	read_active = false;
	read_error = false;
	holding_len = 0U;

	return ScanUart_StartRx();
}

/*
 * Запускает приём одного байта через UART-прерывание.
 * Вызывается при инициализации и из RX callbacks.
 * Проверка reset-latch и запуск неделимы.
 */
bool ScanUart_StartRx(void) {
	// --- Защищённая проверка запрета приёма ---
	uint32_t irq_state = __get_PRIMASK();
	__disable_irq();

	if (AppSafety_IsResetPending()) {
		__set_PRIMASK(irq_state);
		return false;
	}

	// --- Запуск следующего побайтового приёма ---
	HAL_StatusTypeDef status = HAL_UART_Receive_IT(&huart3, &rx_byte, 1U);

	rx_arm_status = (uint8_t) status;

	if (status != HAL_OK && read_active)
		read_error = true;

	__DSB();
	__set_PRIMASK(irq_state);

	return status == HAL_OK;
}

/*
 * Ожидает уровень RTS в пределах существующего таймаута.
 * Подготовка к reset прекращает ожидание с результатом false.
 * Атомарный запрет последующей активации обеспечивают вызывающие gates.
 */
static bool ScanUart_WaitRts(GPIO_PinState level, uint32_t timeout_ms) {
	// --- Начало существующего срока ожидания ---
	uint32_t started = HAL_GetTick();

	// --- Ожидание уровня с проверкой отмены ---
	while (HAL_GPIO_ReadPin(RTS_GPIO_Port, RTS_Pin) != level) {
		if (AppSafety_IsResetPending())
			return false;

		if ((uint32_t) (HAL_GetTick() - started) >= timeout_ms)
			return false;

		AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);

		if (osDelay(1U) != osOK)
			return false;
	}

	// --- Уровень RTS не означает успех отменённой операции ---
	return !AppSafety_IsResetPending();
}

/*
 * Выполняет аппаратный wake только до подготовки к reset.
 * Проверка latch и активация AIMWAKE неделимы.
 * Отпускание линии разрешено всегда; задержки выполняются с
 * восстановленным состоянием IRQ.
 */
bool ScanUart_Wake(void) {
	// --- Атомарная проверка запрета и активация ---
	const uint32_t saved_primask = __get_PRIMASK();
	__disable_irq();

	if (AppSafety_IsResetPending()) {
		__set_PRIMASK(saved_primask);
		return false;
	}

	HAL_GPIO_WritePin(AIMWAKE_GPIO_Port, AIMWAKE_Pin, GPIO_PIN_RESET);

	__DSB();
	__set_PRIMASK(saved_primask);

	// --- Существующий импульс wake и безусловное отпускание линии ---
	uint32_t ticks = (osKernelGetTickFreq() + 999U) / 1000U + 1U;
	bool ok = osDelay(ticks) == osOK;

	HAL_GPIO_WritePin(AIMWAKE_GPIO_Port, AIMWAKE_Pin, GPIO_PIN_SET);

	// --- Существующее ожидание готовности после wake ---
	ticks = (osKernelGetTickFreq() + 99U) / 100U + 1U;
	bool settled = osDelay(ticks) == osOK;

	// Reset во время ожидания исключает успешный результат wake.
	return settled && ok && !AppSafety_IsResetPending();
}

/*
 * Вызывается только задачей-владельцем TX.
 * Закрывает принадлежность START completion до остановки UART TX.
 * При ошибке остановки не возвращает управление владельцу буфера:
 * фактический Error_Handler терминален.
 */
static void ScanUart_AbortTx(void) {
	// --- Закрытие принадлежности позднего START completion ---
	const uint32_t saved_primask = __get_PRIMASK();
	__disable_irq();

	active_start_sequence = 0U;
	__DMB();

	__set_PRIMASK(saved_primask);

	// --- Проверяемая остановка UART TX ---
	if (HAL_UART_AbortTransmit(&huart3) != HAL_OK) {
		Error_Handler();
	}

	// --- Сброс локальных признаков после остановки ---
	tx_byte_done = false;
	tx_last_byte = false;

	// --- Отпускание запроса передачи ---
	HAL_GPIO_WritePin(CTS_GPIO_Port, CTS_Pin, GPIO_PIN_SET);
}

// --- Побайтовая передача SSI через UART-прерывания ---
// Перед каждым байтом проверяется разрешение RTS.
// Последний TxCpltCallback фиксирует завершение START
// и отпускает CTS после фактического окончания передачи UART.
static bool ScanUart_SendInternal(const uint8_t *data, uint16_t len,
bool track_start) {
	if (data == NULL || len == 0U || len > 134U)
		return false;
	// The preceding transaction must have released its grant before a new one.
	if (!ScanUart_WaitRts(GPIO_PIN_SET, 2000U))
		return false;

	// --- Атомарная проверка запрета и запрос передачи ---
	// PrepareReset не может вклиниться между проверкой и CTS LOW.
	const uint32_t saved_primask = __get_PRIMASK();
	__disable_irq();

	if (AppSafety_IsResetPending()) {
		__set_PRIMASK(saved_primask);
		return false;
	}

	HAL_GPIO_WritePin(CTS_GPIO_Port, CTS_Pin, GPIO_PIN_RESET);

	__DSB();
	__set_PRIMASK(saved_primask);

	// --- Существующая передача SSI ---
	bool success = false;
	for (uint16_t i = 0; i < len; ++i) {
		// Bound character gaps below the default 200 ms intercharacter timeout.
		if (!ScanUart_WaitRts(GPIO_PIN_RESET, i == 0U ? 2000U : 100U))
			break;
		// --- Атомарный запрет запуска следующего UART-байта ---
		uint32_t irq_state = __get_PRIMASK();
		__disable_irq();

		if (AppSafety_IsResetPending()) {
			__set_PRIMASK(irq_state);
			break;
		}

		tx_byte_done = false;
		tx_last_byte = i + 1U == len;
		if (track_start && i == 0U) {
			uint32_t next = start_tx.sequence + 1U;
			if (next == 0U)
				next = 1U;
			memset(&start_tx, 0, sizeof(start_tx));
			start_tx.sequence = next;
			start_tx.start_tick = HAL_GetTick();
			start_tx.requested_len = len;
			start_tx.captured_len =
					len < SCAN_UART_TX_CAPTURE_SIZE ?
							len : SCAN_UART_TX_CAPTURE_SIZE;
			start_tx.baud = huart3.Init.BaudRate;
			memcpy(start_tx.bytes, data, start_tx.captured_len);
		}
		HAL_StatusTypeDef status = HAL_UART_Transmit_IT(&huart3,
				&data[i], 1U);
		active_start_sequence =
				status == HAL_OK && track_start && tx_last_byte ?
						start_tx.sequence : 0U;
		if (track_start) {
			start_tx.state = (uint32_t) status
					| ((uint32_t) huart3.gState << 16);
			start_tx.error_mask |= huart3.ErrorCode;
		}
		// --- Завершение критической секции запуска UART TX ---
		__DSB();
		__set_PRIMASK(irq_state);

		if (status != HAL_OK)
			break;
		// --- Ожидание завершения байта с проверкой отмены ---
		uint32_t started = HAL_GetTick();

		while (!tx_byte_done && (uint32_t) (HAL_GetTick() - started) < 100U) {
			if (AppSafety_IsResetPending())
				break;

			AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);

			if (osDelay(1U) != osOK)
				break;
		}

		// --- Отмена имеет приоритет над полученным completion ---
		// Буфер возвращается вызывающему только после остановки TX.
		if (!tx_byte_done || AppSafety_IsResetPending()) {
			ScanUart_AbortTx();
			break;
		}

		success = tx_last_byte;
	}
	HAL_GPIO_WritePin(CTS_GPIO_Port, CTS_Pin, GPIO_PIN_SET);
	if (!success)
		return false;
	return ScanUart_WaitRts(GPIO_PIN_SET, 2000U);
}

// --- Normal commands never replace the retained START snapshot ---
bool ScanUart_Send(const uint8_t *data, uint16_t len) {
	return ScanUart_SendInternal(data, len, false);
}

// --- Explicit domain hook avoids guessing START from arbitrary UART bytes ---
bool ScanUart_SendStart(const uint8_t *data, uint16_t len) {
	return ScanUart_SendInternal(data, len, true);
}

/*
 * Открывает накопление ответа перед wake/START.
 * Вызывает только задача Scanner.
 * Прежние байты и уведомление удаляются до открытия накопления.
 * UART продолжает принимать независимо от состояния накопителя.
 */
bool ScanUart_BeginRead(void) {
	// --- Начало нового обмена без вмешательства RX callback ---
	uint32_t saved_primask = __get_PRIMASK();
	__disable_irq();

	read_active = false;
	holding_len = 0U;
	read_error = false;

	uint32_t flags = osThreadFlagsClear(FLAG_SCAN_UART_RX);

	bool ready = (flags & osFlagsError) == 0U && !AppSafety_IsResetPending()
			&& huart3.RxState == HAL_UART_STATE_BUSY_RX
			&& rx_arm_status == HAL_OK;

	read_active = ready;

	__set_PRIMASK(saved_primask);

	return ready;
}

/*
 * Закрывает накопление по решению задачи Scanner.
 * Возвращает итог приёма до очистки состояния.
 * Сам UART, диагностика и независимый capture продолжают работать.
 */
bool ScanUart_EndRead(void) {
	// --- Закрытие накопления и чтение ошибки неделимы ---
	uint32_t saved_primask = __get_PRIMASK();
	__disable_irq();

	bool ok = !read_error;

	read_active = false;
	holding_len = 0U;
	read_error = false;

	__set_PRIMASK(saved_primask);

	return ok;
}

/*
 * Передаёт задаче все непрочитанные байты и освобождает накопитель.
 * snapshot принадлежит задаче и действителен до следующего вызова.
 * Ошибка сохраняется до закрытия обмена, в том числе при пустом накопителе.
 */
const uint8_t* ScanUart_GetReceived(uint16_t *out_len, bool *out_error) {
	// --- Отдельный снимок исключает изменение прочитанного из ISR ---
	static uint8_t snapshot[SCAN_UART_PENDING_SIZE];

	if (out_len == NULL || out_error == NULL)
		return NULL;

	// --- Копирование и освобождение накопителя ---
	uint32_t saved_primask = __get_PRIMASK();
	__disable_irq();

	uint16_t len = holding_len;

	memcpy(snapshot, holding_buf, len);
	holding_len = 0U;
	*out_error = read_error;

	__set_PRIMASK(saved_primask);

	*out_len = len;

	return len > 0U ? snapshot : NULL;
}

/*
 * HAL завершил приём одного байта.
 * Callback сохраняет байт и восстанавливает приём; SSI здесь не разбирается.
 * При UART error повторный запуск выполняет ErrorCallback, чтобы не очистить
 * ErrorCode до окончания ошибочной ветки HAL_UART_IRQHandler.
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
	if (huart->Instance != USART3)
		return;

	const uint8_t byte = rx_byte;
	const uint32_t errors = huart->ErrorCode;

	// --- Диагностика фактического приёма сохраняется и после latch ---
	ScanUart_RecordRx(byte);

	if (AppSafety_IsResetPending())
		return;

	// --- Независимый сырой capture, включая повреждённый байт ---
	if (capture_active) {
		capture.errors |= errors;

		if (capture.length < SCAN_UART_CAPTURE_SIZE)
			capture.bytes[capture.length++] = byte;
		else
			capture.overflow = true;
	}

	// --- Добавление к непрочитанным данным текущего обмена ---
	if (read_active) {
		if (errors != HAL_UART_ERROR_NONE
				|| holding_len == SCAN_UART_PENDING_SIZE)
			read_error = true;
		else
			holding_buf[holding_len++] = byte;
	}

	// --- Повторный запуск и уведомление задачи ---
	if (errors == HAL_UART_ERROR_NONE)
		(void) ScanUart_StartRx();

	ScanUart_NotifyRead();
}
