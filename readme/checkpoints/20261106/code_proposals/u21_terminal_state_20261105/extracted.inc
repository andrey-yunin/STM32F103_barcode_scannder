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
#define FLAG_CAN_RX                 0x01
#define FLAG_CAN_TX                 0x02
#define FLAG_SCAN_UART_RX   0x04
typedef struct {
    uint16_t cmd_code;
    uint8_t  device_id;
    uint8_t  data[5];
    uint8_t  data_len;
} ParsedCanCommand_t;
typedef struct {
    uint16_t cmd_code;      // 0x0601 (SCAN_REQUEST) or 0x0602 (SCAN_GET_DATA)
    uint16_t timeout_sec;   // timeout in seconds (only for SCAN_REQUEST)
    uint8_t  barcode[128];  // result barcode
    uint8_t  barcode_len;
} ScanResult_t;
/*
 * app_safety.c
 *
 *  Created on: Jul 13, 2026
 *      Author: andrey
 */




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

	// --- Начало общего срока только при первом запросе ---
	// Повторный вызов не продлевает уже начавшуюся подготовку.
	if (!app_safety_reset_pending)
		app_safety_reset_started = HAL_GetTick();

	// --- Необратимый запрет активации и безопасные уровни GPIO ---
	app_safety_reset_pending = true;
	AppSafety_AllOff();
	__DSB();

	__set_PRIMASK(saved_primask);
}

// --- Проверка общего срока подготовки ---
// Только чтение, без ожиданий.
// Вычитание unsigned сохраняет проверку при переполнении HAL tick.
bool AppSafety_IsResetPreparationExpired(void) {
	return app_safety_reset_pending
			&& (uint32_t) (HAL_GetTick() - app_safety_reset_started)
						>= APP_RESET_PREPARE_TIMEOUT_MS;
}

/*
 * scan_uart_driver.c
 *
 *  Created on: Jul 13, 2026
 *      Author: andrey
 */










extern UART_HandleTypeDef huart3;

// --- Побайтовый приём UART и накопитель непрочитанных данных ---
// ISR добавляет байты текущего обмена.
// Задача забирает накопленное и освобождает место под защитой PRIMASK.
static uint8_t rx_byte;
static uint8_t holding_buf[SCAN_UART_PENDING_SIZE];
static volatile uint16_t holding_len;
static volatile bool read_active;
static volatile bool read_error;

// --- Владение RX при плановом выключении ---
// После latch RX принимает и отбрасывает прежний обмен до отмены задачей.
// Накопление ответа STOP разрешит отдельный путь выключения.
static volatile bool rx_cancelled;
static volatile bool reset_stop_active;

static bool ScanUart_StartRxInternal(bool reset_drain);

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
	if (tx_last_byte) {
		// --- Граница ответа STOP до разрешения decoder TX ---
		// CTS ещё LOW: старые байты отбрасываются, затем разрешается ответ.
		// По SSI §1-4/1-5 новый ответ предшествует отложенной передаче.
		if (reset_stop_active && !rx_cancelled) {
			holding_len = 0U;
			read_error = huart3.RxState != HAL_UART_STATE_BUSY_RX
					|| rx_arm_status != HAL_OK;
			read_active = true;
		}

		HAL_GPIO_WritePin(CTS_GPIO_Port, CTS_Pin, GPIO_PIN_SET);
	}
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
// Продолжающийся приём не перезапускаем.
// После latch RX нужен для завершения обмена до окончательного CancelRx.
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
	if (huart->Instance != USART3)
		return;

	// --- Диагностика сохраняется независимо от владельца обмена ---
	rx_diag.error_mask |= huart->ErrorCode;

	if (capture_active)
		capture.errors |= huart->ErrorCode;

	if (active_start_sequence != 0U
			&& active_start_sequence == start_tx.sequence)
		start_tx.error_mask |= huart->ErrorCode;

	if (rx_diag.error_count != UINT32_MAX)
		++rx_diag.error_count;

	// --- Окончательное закрытие запрещает повторный запуск ---
	if (rx_cancelled)
		return;

	if (read_active
			&& (!AppSafety_IsResetPending() || reset_stop_active))
		read_error = true;

	// --- Восстановление RX для разрешённого завершения обмена ---
	if (huart->RxState == HAL_UART_STATE_READY)
		(void) ScanUart_StartRxInternal(true);

	if (!AppSafety_IsResetPending() || reset_stop_active)
		ScanUart_NotifyRead();
}

/*
 * Окончательно закрывает RX после установки reset-latch.
 * Вызывает только задача Scanner; диагностика сохраняется.
 * Запрет фиксируется до HAL AbortReceive, чтобы поздний callback
 * не восстановил приём во время или после отмены.
 */
bool ScanUart_CancelRx(void) {
	// --- Закрытие владельцев RX до обращения к HAL ---
	const uint32_t saved_primask = __get_PRIMASK();
	__disable_irq();

	capture_active = false;
	rx_cancelled = true;
	reset_stop_active = false;
	read_active = false;
	__DMB();

	__set_PRIMASK(saved_primask);

	// --- Остановка побайтового UART RX ---
	HAL_StatusTypeDef status = HAL_UART_AbortReceive(&huart3);

	// --- Аннулирование данных отменённого обмена ---
	// После rx_cancelled callback эти поля больше не обновляет.
	read_active = false;
	holding_len = 0U;
	read_error = false;

	if (status != HAL_OK)
		return false;

	return true;
}

// --- Инициализация приёма; накопление ответа пока закрыто ---
bool ScanUart_Init(void) {
	rx_cancelled = false;
	reset_stop_active = false;
	read_active = false;
	read_error = false;
	holding_len = 0U;

	return ScanUart_StartRx();
}

/*
 * Запускает приём одного байта через UART-прерывание.
 * Обычный запуск запрещён после latch; внутренний re-arm
 * сохраняет приём для завершения обмена.
 * После CancelRx повторный запуск запрещён обоим путям.
 */
static bool ScanUart_StartRxInternal(bool reset_drain) {
	// --- Неделимая проверка разрешения и запуск RX ---
	uint32_t irq_state = __get_PRIMASK();
	__disable_irq();

	if (rx_cancelled || (AppSafety_IsResetPending() && !reset_drain)) {
		__set_PRIMASK(irq_state);
		return false;
	}

	// --- Следующий побайтовый приём ---
	HAL_StatusTypeDef status = HAL_UART_Receive_IT(&huart3, &rx_byte, 1U);

	rx_arm_status = (uint8_t) status;

	if (status != HAL_OK && read_active)
		read_error = true;

	__DSB();
	__set_PRIMASK(irq_state);

	return status == HAL_OK;
}

// --- Обычный запуск RX после latch остаётся запрещён ---
bool ScanUart_StartRx(void) {
	return ScanUart_StartRxInternal(false);
}

// --- Допуск обычной передачи либо специального STOP ---
static bool ScanUart_IsTxAllowed(bool reset_stop) {
    if (reset_stop)
        return AppSafety_IsResetPending()
                && !AppSafety_IsResetPreparationExpired();

    return !AppSafety_IsResetPending();
}

/*
 * Ожидает уровень RTS в пределах существующего таймаута.
 * Обычное ожидание прерывается по latch.
 * STOP ограничен тем же общим сроком подготовки reset.
 * Атомарный запрет последующей активации обеспечивают вызывающие gates.
 */
static bool ScanUart_WaitRts(GPIO_PinState level, uint32_t timeout_ms,
        bool reset_stop) {
    // --- Локальный срок сохраняется внутри общего бюджета reset ---
    const uint32_t started = HAL_GetTick();

    while (HAL_GPIO_ReadPin(RTS_GPIO_Port, RTS_Pin) != level) {
        if (!ScanUart_IsTxAllowed(reset_stop)
                || (uint32_t) (HAL_GetTick() - started) >= timeout_ms)
            return false;

        AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);

        if (osDelay(1U) != osOK)
            return false;
    }

    return ScanUart_IsTxAllowed(reset_stop);
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
bool track_start, bool reset_stop) {
	if (data == NULL || len == 0U || len > 134U)
		return false;
	// The preceding transaction must have released its grant before a new one.
	if (!ScanUart_WaitRts(GPIO_PIN_SET, 2000U, reset_stop))
		return false;

	// --- Атомарная проверка запрета и запрос передачи ---
	// PrepareReset не может вклиниться между проверкой и CTS LOW.
	const uint32_t saved_primask = __get_PRIMASK();
	__disable_irq();

	if (!ScanUart_IsTxAllowed(reset_stop)) {
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
		if (!ScanUart_WaitRts(GPIO_PIN_RESET, i == 0U ? 2000U : 100U,
				reset_stop))
			break;
		// --- Атомарный запрет запуска следующего UART-байта ---
		uint32_t irq_state = __get_PRIMASK();
		__disable_irq();

		if (!ScanUart_IsTxAllowed(reset_stop)) {
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
			if (!ScanUart_IsTxAllowed(reset_stop))
				break;

			AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);

			if (osDelay(1U) != osOK)
				break;
		}

		// --- Отмена имеет приоритет над полученным completion ---
		// Буфер возвращается вызывающему только после остановки TX.
		if (!tx_byte_done || !ScanUart_IsTxAllowed(reset_stop)) {
			ScanUart_AbortTx();
			break;
		}

		success = tx_last_byte;
	}
	HAL_GPIO_WritePin(CTS_GPIO_Port, CTS_Pin, GPIO_PIN_SET);
	if (!success)
		return false;
	return ScanUart_WaitRts(GPIO_PIN_SET, 2000U, reset_stop);
}

// --- Normal commands never replace the retained START snapshot ---
bool ScanUart_Send(const uint8_t *data, uint16_t len) {
	return ScanUart_SendInternal(data, len, false, false);
}

// --- Explicit domain hook avoids guessing START from arbitrary UART bytes ---
bool ScanUart_SendStart(const uint8_t *data, uint16_t len) {
	return ScanUart_SendInternal(data, len, true, false);
}

/*
 * Единственный task-only TX-путь после latch: только точный SSI STOP.
 * Пакет формирует домен. Проверка байтов исключает произвольные команды.
 * AIM/WAKE не используется; CTS LOW будит модуль и запрашивает передачу.
 */
bool ScanUart_SendStopForReset(const uint8_t *data, uint16_t len) {
    static const uint8_t stop_only[] = { 0x04U, 0xE5U, 0x04U, 0x00U,
            0xFFU, 0x13U };

    // --- Узкий допуск выключения ---
    if (data == NULL || len != sizeof(stop_only)
            || memcmp(data, stop_only, sizeof(stop_only)) != 0
            || !ScanUart_IsTxAllowed(true))
        return false;

    // --- Отмена накопления прежнего ответа, RX продолжает дренирование ---
    const uint32_t saved_primask = __get_PRIMASK();
    __disable_irq();

    if (rx_cancelled || !ScanUart_IsTxAllowed(true)) {
        __set_PRIMASK(saved_primask);
        return false;
    }

    capture_active = false;
    read_active = false;
    holding_len = 0U;
    read_error = false;
    reset_stop_active = true;

    uint32_t flags = osThreadFlagsClear(FLAG_SCAN_UART_RX);
    bool ready = (flags & osFlagsError) == 0U;

    if (ready && huart3.RxState == HAL_UART_STATE_READY)
        ready = ScanUart_StartRxInternal(true);

    ready = ready && huart3.RxState == HAL_UART_STATE_BUSY_RX
            && rx_arm_status == HAL_OK;

    __set_PRIMASK(saved_primask);

    if (!ready)
        return false;

    // --- Новый handshake завершает прежнюю передачу и даёт STOP приоритет ---
    // Накопление ACK откроет TxCpltCallback перед отпусканием CTS.
    return ScanUart_SendInternal(data, len, false, true);
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
 * Callback сохраняет байт и восстанавливает приём; SSI здесь не разбирается.
 * После latch прежние байты принимаются без передачи обычной операции.
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

	if (rx_cancelled)
		return;

	const bool accepting = !AppSafety_IsResetPending() || reset_stop_active;

	// --- Обычный capture после latch не продолжается ---
	if (capture_active && !AppSafety_IsResetPending()) {
		capture.errors |= errors;

		if (capture.length < SCAN_UART_CAPTURE_SIZE)
			capture.bytes[capture.length++] = byte;
		else
			capture.overflow = true;
	}

	// --- Накопление только для разрешённого владельцем обмена ---
	if (read_active && accepting) {
		if (errors != HAL_UART_ERROR_NONE
				|| holding_len == SCAN_UART_PENDING_SIZE)
			read_error = true;
		else
			holding_buf[holding_len++] = byte;
	}

	// --- Приём сохраняется до окончательного CancelRx ---
	if (errors == HAL_UART_ERROR_NONE)
		(void) ScanUart_StartRxInternal(true);

	if (accepting)
		ScanUart_NotifyRead();
}

/*
 * scan_controller.c
 *
 *  Created on: Jul 13, 2026
 *      Author: andrey
 */





// --- Локальные данные ---
static ScanState_t g_state = SCAN_STATE_IDLE;
static uint8_t g_shadow_buf[SHADOW_BUF_SIZE];
static uint8_t g_shadow_len = 0;
static ScanStats_t g_stats;

static uint8_t g_barcode_buf[SHADOW_BUF_SIZE];
static uint8_t g_barcode_len = 0;

void ScanCtrl_Init(void) {
	g_state = SCAN_STATE_IDLE;
	memset(&g_stats, 0, sizeof(g_stats));
	memset(g_shadow_buf, 0, SHADOW_BUF_SIZE);
	g_shadow_len = 0;
	g_barcode_len = 0;
}

bool ScanCtrl_StartScan(uint16_t timeout_sec) {
	(void) timeout_sec;
	// Only an active transaction is busy. ERROR and DATA_READY retain the
	// previous result until the next request; neither blocks a new scan.
	if (g_state == SCAN_STATE_SCANNING)
		return false;

	g_state = SCAN_STATE_SCANNING;
	g_barcode_len = 0;
	return true;
}

bool ScanCtrl_ProcessReceived(const uint8_t *data, uint16_t len) {
	if (g_state != SCAN_STATE_SCANNING)
		return false;

	SsiMsg_t msg;
	if (!Ssi_Decode(data, len, &msg)) {
		g_stats.ssi_decode_errors++;
		g_state = SCAN_STATE_ERROR;
		return false;
	}

	if (msg.opcode == SSI_OP_DECODE_DATA) {
		// --- Обязательный байт типа штрихкода ---
		if (msg.data_len < 1U) {
			g_stats.ssi_decode_errors++;
			g_state = SCAN_STATE_ERROR;
			return false;
		}

		// --- Строка начинается после Bar Code Type ---
		uint16_t barcode_len = msg.data_len - 1U;
		uint16_t copy_len =
				barcode_len < SHADOW_BUF_SIZE ? barcode_len : SHADOW_BUF_SIZE;

		memcpy(g_barcode_buf, &msg.data[1], copy_len);
		g_barcode_len = (uint8_t) copy_len;

		// --- Сохранение последнего успешно считанного штрихкода ---
		memcpy(g_shadow_buf, g_barcode_buf, g_barcode_len);
		g_shadow_len = g_barcode_len;

		g_stats.scan_success++;
		g_state = SCAN_STATE_DATA_READY;
		return true;
	}

	if (msg.opcode == SSI_OP_CMD_NAK) {
		g_stats.scan_timeouts++;
		g_state = SCAN_STATE_ERROR;
		return false;
	}

	// ACK (0xD0) — игнорируем, ждём DATA
	return false;
}

bool ScanCtrl_GetLastBarcode(uint8_t *out_buf, uint8_t *out_len) {
	if (out_buf == NULL || out_len == NULL)
		return false;
	if (g_shadow_len == 0)
		return false;

	memcpy(out_buf, g_shadow_buf, g_shadow_len);
	*out_len = g_shadow_len;
	return true;
}

bool ScanCtrl_HandleTimeout(void) {
	if (g_state != SCAN_STATE_SCANNING)
		return false;
	g_stats.scan_timeouts++;
	g_state = SCAN_STATE_ERROR;
	return true;
}

void ScanCtrl_HandleTransportError(void) {
	if (g_state == SCAN_STATE_SCANNING)
		g_state = SCAN_STATE_ERROR;
}

ScanState_t ScanCtrl_GetState(void) {
	return g_state;
}

void ScanCtrl_GetStats(ScanStats_t *out) {
	if (out != NULL)
		memcpy(out, &g_stats, sizeof(g_stats));
}


/*
 * scan_ssi.c
 *
 *  Created on: Jul 13, 2026
 *      Author: andrey
 */




// --- Checksum SSI: negative byte sum, transmitted as a big-endian word ---
static uint16_t Ssi_Checksum(const uint8_t *data, uint16_t len)
{
    uint32_t sum = 0;
    for (uint16_t i = 0; i < len; i++) {
        sum += data[i];
    }
    return (uint16_t)(0U - sum);
}

// --- Encode one packet; Length includes the four-byte header ---
uint8_t Ssi_Encode(uint8_t opcode, const uint8_t *data,
                   uint16_t data_len, uint8_t *out_packet)
{
    if (out_packet == NULL || data_len > SSI_MAX_PAYLOAD ||
        (data_len != 0 && data == NULL)) return 0;

    uint16_t idx = 0;
    out_packet[idx++] = (uint8_t)(4 + data_len);
    out_packet[idx++] = opcode;
    out_packet[idx++] = SSI_MSG_SRC_HOST;
    out_packet[idx++] = 0x00;
    for (uint16_t i = 0; i < data_len; i++) {
        out_packet[idx++] = data[i];
    }

    uint16_t checksum = Ssi_Checksum(out_packet, idx);
    out_packet[idx++] = (uint8_t)(checksum >> 8);
    out_packet[idx++] = (uint8_t)checksum;
    return (uint8_t)idx;
}

// --- Validate the first complete packet before publishing its contents ---
bool Ssi_Decode(const uint8_t *buf, uint16_t buf_len, SsiMsg_t *out_msg)
{
    if (buf == NULL || out_msg == NULL || buf_len < 6) return false;

    uint16_t length = buf[0];
    if (length < 4 || length > 4 + SSI_MAX_PAYLOAD) return false;
    if (buf_len < length + 2) return false;

    uint16_t received_checksum = (uint16_t)(((uint16_t)buf[length] << 8) |
                                          buf[length + 1]);
    if (received_checksum != Ssi_Checksum(buf, length)) return false;

    out_msg->opcode = buf[1];
    out_msg->msg_src = buf[2];
    out_msg->status = buf[3];
    out_msg->data_len = length - 4;
    memcpy(out_msg->data, buf + 4, out_msg->data_len);
    return true;
}

/*
 * task_scan_controller.c
 *
 *  Created on: Jul 10, 2026
 *      Author: andrey
 *
 *  Задача Scan Controller.
 *  Управляет сканером SE-965HP через USART3 (SSI-протокол).
 *  Получает команды из scan_queue (START/STOP сканирования),
 *  отправляет результат сканирования через dispatcher в CAN.
 */













// --- Учёт операций до очереди, во время обработки и при startup ---
// Начальная единица удерживает BUSY ещё до первого запуска задачи Scanner.
// Dispatcher добавляет единицу до enqueue и снимает её при отказе очереди.
// Задача снимает единицу после всей обработки, включая завершающий UART TX.
static volatile uint32_t scan_pending_commands = 1U;

// --- Резервирование команды Dispatcher до её публикации в очереди ---
void ScanTask_ReserveCommand(void) {
	const uint32_t saved_primask = __get_PRIMASK();
	__disable_irq();
	scan_pending_commands++;
	__set_PRIMASK(saved_primask);
}

// --- Завершение ровно одного ранее зарезервированного владения ---
void ScanTask_ReleaseCommand(void) {
	const uint32_t saved_primask = __get_PRIMASK();
	__disable_irq();
	scan_pending_commands--;
	__set_PRIMASK(saved_primask);
}

// --- Допуск F003 без ожидания завершения доменных операций ---
// После чтения нуля только Dispatcher может добавить новую команду;
// пока он выполняет Commit, новых enqueue не будет. IRQ во Flash разрешены.
bool ScanTask_IsFlashCommitBusy(void) {
	const uint32_t saved_primask = __get_PRIMASK();
	__disable_irq();
	const bool busy = scan_pending_commands != 0U;
	__set_PRIMASK(saved_primask);
	return busy;
}

// --- Подтверждение подготовки; единственный писатель — задача Scanner ---
static volatile bool scan_reset_prepared = false;

// --- Чтение подтверждения задачей Dispatcher ---
bool ScanTask_IsResetPrepared(void) {
	return scan_reset_prepared;
}

/*
 * Проверяет первый ответ нового STOP-handshake; вызывается задачей Scanner.
 * До отпускания CTS драйвер отбросил прежний RX. По SSI §1-4/1-5 ответ
 * на новую команду предшествует отложенному прежнему сообщению.
 * ACK не ищется среди посторонних пакетов: NAK/неверный ответ означает отказ.
 */
static bool Scan_WaitResetStopAck(void) {
	uint8_t packet[6];
	uint16_t used = 0U;

	// --- Один ACK может поступить несколькими RX-фрагментами ---
	while (!AppSafety_IsResetPreparationExpired()) {
		uint16_t length;
		bool error;
		const uint8_t *received = ScanUart_GetReceived(&length, &error);

		if (error)
			return false;

		if (received != NULL) {
			uint16_t count = (uint16_t) (sizeof(packet) - used);
			if (count > length)
				count = length;

			memcpy(&packet[used], received, count);
			used += count;

			// CMD_ACK без payload имеет Length=4, всего 6 байт.
			if (packet[0] != 4U)
				return false;

			if (used == sizeof(packet)) {
				SsiMsg_t message;
				return !AppSafety_IsResetPreparationExpired()
						&& Ssi_Decode(packet, sizeof(packet), &message)
						&& message.opcode == SSI_OP_CMD_ACK
						&& message.msg_src == SSI_MSG_SRC_DECODER
						&& message.data_len == 0U
						&& (message.status & 0x02U) == 0U;
			}
		}

		// --- Прогресс ограниченного ожидания, без нового срока ---
		if (osDelay(1U) != osOK)
			return false;

		AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
	}

	return false;
}

/*
 * Вызывается только задачей Scanner после возврата из обычного UART TX.
 * При F002/F006 всегда выполняет STOP: состояние контроллера MCU само по
 * себе не подтверждает состояние внешнего модуля. Это не STOP после успеха
 * обычной команды чтения; здесь выполняется отдельная подготовка reset.
 */
static bool Scan_SyncResetState(void) {
	// --- Проверка запроса и ранее завершённой подготовки ---
	if (!AppSafety_IsResetPending())
		return false;

	if (scan_reset_prepared)
		return true;

	// --- Только STOP; прежняя операция уже не может стать успешной ---
	uint8_t packet[6];
	const uint8_t length = Ssi_Encode(SSI_OP_STOP_DECODE, NULL, 0U, packet);
	bool stopped = ScanUart_SendStopForReset(packet, length)
			&& Scan_WaitResetStopAck();

	// --- Окончательное закрытие RX, включая отказ подготовки ---
	const bool read_ok = ScanUart_EndRead();
	const bool closed = ScanUart_CancelRx();

	if (!stopped || !read_ok || !closed
			|| AppSafety_IsResetPreparationExpired())
		Error_Handler();

	ScanCtrl_HandleTransportError();

	// --- Очистка уведомления отменённого обмена ---
	uint32_t flags = osThreadFlagsClear(FLAG_SCAN_UART_RX);
	if ((flags & osFlagsError) != 0U
			|| AppSafety_IsResetPreparationExpired())
		Error_Handler();

	// --- Только подтверждённая подготовка разрешает service/Flash ---
	__DMB();
	scan_reset_prepared = true;

	return true;
}

/*
 * Принимает решение о terminal-результате до возможного reset.
 * Решение сохраняется вызывающим кодом; передача CAN выполняется
 * после восстановления IRQ. Доставку ответа этот gate не подтверждает.
 */
static bool Scan_TryAcceptTerminalResult(void) {
	// --- Короткая критическая секция принятия результата ---
	const uint32_t saved_primask = __get_PRIMASK();
	__disable_irq();

	bool accepted = !AppSafety_IsResetPending();

	__set_PRIMASK(saved_primask);

	return accepted;
}

// --- Проверка DECODE_DATA без сохранения результата и изменения статистики ---
static bool Scan_IsValidDecodeData(const uint8_t *data, uint16_t len) {
	SsiMsg_t message;

	return Ssi_Decode(data, len, &message)
			&& message.opcode == SSI_OP_DECODE_DATA && message.data_len >= 1U;
}

/*
 * Задача собирает SSI-пакет по Length, независимо от границ RX-событий.
 * Полный валидный ACK пропускается; следующий пакет собирается далее.
 * Повреждённый пакет передаётся существующей обработке ошибок.
 */
static bool Scan_CollectPacket(const uint8_t *data, uint16_t len,
		uint8_t *packet, uint16_t *packet_len) {
	for (uint16_t i = 0U; i < len; ++i) {
		packet[(*packet_len)++] = data[i];

		// --- Некорректная длина обрабатывается как ошибка SSI ---
		if (packet[0] < 4U || packet[0] > 4U + SSI_MAX_PAYLOAD)
			return true;

		// Length включает заголовок и данные, но не checksum.
		uint16_t expected = (uint16_t) packet[0] + 2U;

		if (*packet_len < expected)
			continue;

		// --- Полный ACK не завершает ожидание штрихкода ---
		SsiMsg_t message;

		if (Ssi_Decode(packet, *packet_len,
				&message) && message.opcode == SSI_OP_CMD_ACK) {
			*packet_len = 0U;
			continue;
		}

		return true;
	}

	return false;
}

// --- Read-only revision probe or single BEEP: DONE means RX capture complete ---
static void ScanQueryRevision(uint16_t command) {

	// --- Отмена до начала запроса ---
	if (Scan_SyncResetState()) {
		CAN_SendNackPublic(command, CAN_ERR_DEVICE_BUSY);
		return;
	}

	static uint8_t packet[SSI_PACKET_MAX_LEN];
	const uint8_t beep_code = 0;
	uint8_t length =
			command == CAN_CMD_SCAN_BEEP ?
					Ssi_Encode(SSI_OP_BEEP, &beep_code, 1, packet) :
					Ssi_Encode(SSI_OP_REQUEST_REVISION, NULL, 0, packet);
	ScanUartCapture_t rx;
	ScanUart_BeginCapture();

	if (!ScanUart_Wake() || !ScanUart_Send(packet, length)) {
		ScanUart_EndCapture(NULL);

		// --- Отмена reset отличается от неисправности UART ---
		if (Scan_SyncResetState()) {
			CAN_SendNackPublic(command, CAN_ERR_DEVICE_BUSY);
		} else {
			CAN_SendNackPublic(command, CAN_ERR_SCAN_HARDWARE);
		}
		return;
	}

	// Capture retains raw RX, including malformed and fragmented responses.
	// Fixed 3 s covers the default 2 s SSI response timeout with margin.
	uint32_t started = osKernelGetTickCount();
	uint32_t duration = 3U * osKernelGetTickFreq();
	// --- Ожидание capture с проверкой отмены ---
	while ((uint32_t) (osKernelGetTickCount() - started) < duration) {
		if (Scan_SyncResetState()) {
			CAN_SendNackPublic(command, CAN_ERR_DEVICE_BUSY);
			return;
		}

		AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
		osDelay((osKernelGetTickFreq() + 19U) / 20U);
	}

	// --- Проверка отмены после последнего ожидания ---
	if (Scan_SyncResetState()) {
		CAN_SendNackPublic(command, CAN_ERR_DEVICE_BUSY);
		return;
	}

	ScanUart_EndCapture(&rx);

	// v1 envelope: schema8, flags8 (bit0 overflow), length16, errors32, raw.
	// Little-endian metadata; padding to six bytes is zero and checked by host.
	uint8_t response[204] = { 1U, 0U };
	response[1] = rx.overflow ? 1U : 0U;
	response[2] = (uint8_t) rx.length;
	response[3] = (uint8_t) (rx.length >> 8);
	for (unsigned i = 0; i < 4; ++i)
		response[4 + i] = (uint8_t) (rx.errors >> (8U * i));
	memcpy(response + 8, rx.bytes, rx.length);
	uint16_t total = (uint16_t) (((8U + rx.length + 5U) / 6U) * 6U);

	// --- Принятие результата capture либо завершение отмены ---
	if (!Scan_TryAcceptTerminalResult()) {
		(void) Scan_SyncResetState();
		CAN_SendNackPublic(command, CAN_ERR_DEVICE_BUSY);
		return;
	}
	if (!CAN_SendDiagnosticResponse(command, response, total))
		CAN_SendNackPublic(command, CAN_ERR_DEVICE_BUSY);
}

void app_start_task_scan_controller(void *argument) {
	(void) argument;

	uint8_t ssi_packet[SSI_PACKET_MAX_LEN];
	uint16_t ssi_len;

	ScanCtrl_Init();
	ScanUart_Init();

	// --- SCAN_ENABLE: однократно при старте ---
	ssi_len = Ssi_Encode(SSI_OP_SCAN_ENABLE, NULL, 0, ssi_packet);
	if (ScanUart_Wake())
		ScanUart_Send(ssi_packet, ssi_len);

	ScanResult_t cmd;

	// --- Интервал ожидания команды в тиках RTOS ---
	const uint32_t idle_wait_ticks = (APP_WATCHDOG_TASK_IDLE_TIMEOUT_MS
			* osKernelGetTickFreq()) / 1000U;

	// --- Первое владение соответствует завершённой startup-последовательности ---
	bool release_pending = true;

	for (;;) {
		// --- Единая точка завершения startup либо предыдущей команды ---
		// Все continue внешнего цикла проходят здесь; внутреннее ожидание RX
		// не снимает владение. Ошибка/timeout пустой очереди не создают владение.
		if (release_pending) {
			ScanTask_ReleaseCommand();
			release_pending = false;
		}

		// --- Подготовка выполняется и при отсутствии команд ---
		(void) Scan_SyncResetState();

		osStatus_t queue_status;

		// --- Ожидание команды от Dispatcher ---
		queue_status = osMessageQueueGet(scan_queueHandle, &cmd,
		NULL, idle_wait_ticks);

		// Пустая очередь — штатный режим, задача исполняется.
		if (queue_status == osErrorTimeout) {
			AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
			continue;
		}

		// Ошибка очереди не подтверждает прогресс.
		// Содержимое cmd при ошибке не обрабатываем.
		if (queue_status != osOK) {
			continue;
		}

		// --- Принятие владения, уже зарезервированного Dispatcher ---
		// Dequeue не уменьшает счётчик: он остаётся занят до конца обработки.
		release_pending = true;

		// --- Команда получена и передаётся на обработку ---
		AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);

		// --- Команда из очереди не запускается после reset-latch ---
		if (Scan_SyncResetState()) {
			CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_DEVICE_BUSY);
			continue;
		}

		if (cmd.cmd_code == CAN_CMD_SCAN_GET_REVISION
				|| cmd.cmd_code == CAN_CMD_SCAN_BEEP) {
			ScanQueryRevision(cmd.cmd_code);
			continue;
		}

		if (cmd.cmd_code == CAN_CMD_SCAN_GET_DATA) {
			// --- Подготовка результата из сохранённого штрихкода ---
			uint8_t barcode[SHADOW_BUF_SIZE];
			uint8_t barcode_len = 0;
			bool has_barcode = ScanCtrl_GetLastBarcode(barcode, &barcode_len);

			// --- Принятие результата либо завершение отмены ---
			if (!Scan_TryAcceptTerminalResult()) {
				(void) Scan_SyncResetState();
				CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_DEVICE_BUSY);
				continue;
			}

			// --- Передача принятого результата вне критической секции ---
			if (has_barcode) {
				CAN_SendDataFragmented(cmd.cmd_code, barcode, barcode_len);
				CAN_SendDone(cmd.cmd_code, 0);
			} else {
				CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_NO_DATA);
			}
			continue;
		}

		if (cmd.cmd_code != CAN_CMD_SCAN_REQUEST) {
			CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_UNKNOWN_CMD);
			continue;
		}

		// --- Запуск состояния текущего запроса ---
		if (!ScanCtrl_StartScan(cmd.timeout_sec)) {
			CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_DEVICE_BUSY);
			continue;
		}

		// --- Открытие накопления ответа перед отправкой START ---
		// Открываем накопление только после принятия запроса контроллером.
		if (!ScanUart_BeginRead()) {
			if (Scan_SyncResetState()) {
				CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_DEVICE_BUSY);
			} else {
				ScanCtrl_HandleTransportError();
				CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_COMM);
			}
			continue;
		}

		// --- Отправка START_DECODE ---
		ssi_len = Ssi_Encode(SSI_OP_START_DECODE, NULL, 0, ssi_packet);
		// Retain this attempt separately from the subsequent STOP_DECODE.
		if (!ScanUart_Wake() || !ScanUart_SendStart(ssi_packet, ssi_len)) {
			// --- Закрытие накопления при несостоявшемся запуске ---
			(void) ScanUart_EndRead();

			// --- Reset отличается от ошибки передачи START ---
			if (Scan_SyncResetState()) {
				CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_DEVICE_BUSY);
			} else {
				ScanCtrl_HandleTransportError();
				CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_HARDWARE);
			}
			continue;
		}

		// Ожидание UART RX с таймаутом
		// --- Общий срок ожидания ответа Scanner ---
		// Считаем фактически прошедшие тики, включая время выполнения задачи.
		uint32_t timeout_ms =
				cmd.timeout_sec > 0U ?
						(uint32_t) cmd.timeout_sec * 1000U : 4000U;
		uint32_t tick_freq = osKernelGetTickFreq();
		uint32_t timeout_ticks = (uint32_t) (((uint64_t) timeout_ms * tick_freq)
				/ 1000U);
		uint32_t slice_ticks = (500U * tick_freq) / 1000U;
		uint32_t started = osKernelGetTickCount();

		bool rx_received = false;
		bool wait_error = false;

		// --- Накопитель одного SSI-пакета принадлежит задаче Scanner ---
		static uint8_t rx_packet[SSI_PACKET_MAX_LEN];
		uint16_t rx_len = 0U;
		const uint8_t *rx_data = NULL;

		// --- Приём пакета в пределах исходного срока запроса ---
		// Сначала забираем накопленные байты, затем при необходимости ждём.
		// Число уведомлений не определяет количество доступных данных.
		for (;;) {
			// --- Отмена и общий срок ---
			if (AppSafety_IsResetPending())
				break;

			uint32_t elapsed = osKernelGetTickCount() - started;

			if (elapsed >= timeout_ticks)
				break;

			// --- Потребление накопленных байтов ---
			uint16_t received_len;
			bool receive_error;

			const uint8_t *received = ScanUart_GetReceived(&received_len,
					&receive_error);

			if (receive_error) {
				wait_error = true;
				break;
			}

			if (received != NULL) {
				AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);

				if (!Scan_CollectPacket(received, received_len, rx_packet,
						&rx_len))
					continue;

				rx_data = rx_packet;
				rx_received = true;
				break;
			}

			// --- Ожидание следующих данных без перезапуска срока ---
			uint32_t remaining = timeout_ticks - elapsed;
			uint32_t wait_ticks =
					remaining < slice_ticks ? remaining : slice_ticks;

			uint32_t flags = osThreadFlagsWait(
			FLAG_SCAN_UART_RX, osFlagsWaitAny, wait_ticks);

			if (flags == osFlagsErrorTimeout) {
				AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
				continue;
			}

			if ((flags & osFlagsError) != 0U) {
				wait_error = true;
				break;
			}

			AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
		}

		// --- Подтверждение штрихкода сканеру до принятия результата и отправки CAN ---
		bool ack_error = false;

		if (rx_received && !wait_error
				&& Scan_IsValidDecodeData(rx_data, rx_len)) {
			ssi_len = Ssi_Encode(SSI_OP_CMD_ACK, NULL, 0, ssi_packet);
			ack_error = !ScanUart_Wake() || !ScanUart_Send(ssi_packet, ssi_len);
		}

		// --- Закрытие накопления и итоговая проверка RX ---
		// EndRead атомарно сохраняет итог ошибки и закрывает накопление.
		// Данные результата уже находятся в rx_packet задачи.
		if (!ScanUart_EndRead())
			wait_error = true;

		// --- Reset имеет приоритет над RX, timeout и ошибкой ожидания ---
		if (Scan_SyncResetState()) {
			CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_DEVICE_BUSY);
			continue;
		}

		// --- Принятие terminal-результата текущего сканирования ---
		// Gate предшествует изменению статистики и сохранению штрихкода.
		if (!Scan_TryAcceptTerminalResult()) {
			(void) Scan_SyncResetState();
			CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_DEVICE_BUSY);
			continue;
		}

		// --- Раздельные исходы ошибки ожидания и отсутствия ответа ---
		if (wait_error || ack_error) {
			ScanCtrl_HandleTransportError();
			CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_COMM);
		} else if (!rx_received) {
			ScanCtrl_HandleTimeout();
			CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_TIMEOUT);
		} else {

			if (rx_data != NULL && ScanCtrl_ProcessReceived(rx_data, rx_len)) {
				uint8_t buf[SHADOW_BUF_SIZE];
				uint8_t buf_len = 0;

				ScanCtrl_GetLastBarcode(buf, &buf_len);
				CAN_SendDataFragmented(cmd.cmd_code, buf, buf_len);
				CAN_SendDone(cmd.cmd_code, 0);
			} else {
				// --- Ошибка завершает и запрос, и состояние контроллера ---
				// Следующая команда не должна видеть завершённое чтение как SCANNING.
				ScanCtrl_HandleTransportError();
				CAN_SendNackPublic(cmd.cmd_code, CAN_ERR_SCAN_HARDWARE);
			}
		}

		// --- Прекращение попытки после неуспешного чтения ---
		// После успешного чтения дополнительный STOP не отправляем.
		// Проверка исхода STOP будет частью отдельного блока отмены.
		if (ScanCtrl_GetState() != SCAN_STATE_DATA_READY) {
			ssi_len = Ssi_Encode(SSI_OP_STOP_DECODE, NULL, 0, ssi_packet);

			if (ScanUart_Wake())
				(void) ScanUart_Send(ssi_packet, ssi_len);
		}
	}
}

static void Dispatcher_PrepareReset(void) {
	// --- Немедленный запрет и начало общего срока ---
	AppSafety_PrepareReset();

	// --- Ожидание подтверждения домена в пределах того же срока ---
	for (;;) {
		if (AppSafety_IsResetPreparationExpired())
			Error_Handler();

		if (ScanTask_IsResetPrepared())
			return;

		if (osDelay(1U) != osOK)
			Error_Handler();

		AppWatchdog_Heartbeat(APP_WDG_CLIENT_DISPATCHER);
	}
}
static uint16_t Dispatcher_ReadLe16(const uint8_t *data) {
	return (uint16_t) data[0] | ((uint16_t) data[1] << 8U);
}
static void test_service(uint16_t command,uint16_t key) {
ParsedCanCommand_t parsed={.cmd_code=command,.data_len=2};
parsed.data[0]=(uint8_t)key;parsed.data[1]=(uint8_t)(key>>8);
switch(command) {
case CAN_CMD_SRV_REBOOT: {
			// --- Проверка ключа до любых действий подготовки ---
			if ((parsed.data_len < 2U)
					|| (Dispatcher_ReadLe16(parsed.data) != SRV_MAGIC_REBOOT)) {
				CAN_SendNackPublic(parsed.cmd_code, CAN_ERR_INVALID_KEY);
				break;
			}

			// --- Подтверждённая подготовка предшествует DONE ---
			Dispatcher_PrepareReset();

			CAN_SendDone(parsed.cmd_code, 0U);

			// --- Существующее окно отправки HC: 100 тиков ---
			// Постановка DONE в очередь не гарантирует доставку по CAN.
			if (osDelay(100U) != osOK) {
				Error_Handler();
			}

			NVIC_SystemReset();
			break;
		}

		case CAN_CMD_SRV_FACTORY_RESET: {
			// --- Проверка ключа до подготовки и стирания Flash ---
			if ((parsed.data_len < 2U)
					|| (Dispatcher_ReadLe16(parsed.data)
							!= SRV_MAGIC_FACTORY_RESET)) {
				CAN_SendNackPublic(parsed.cmd_code, CAN_ERR_INVALID_KEY);
				break;
			}

			// --- Подтверждённая подготовка предшествует Flash ---
			Dispatcher_PrepareReset();

			// --- Результат стирания определяет сервисный ответ ---
			if (AppConfig_FactoryReset()) {
				CAN_SendDone(parsed.cmd_code, 0U);
			} else {
				CAN_SendNackPublic(parsed.cmd_code, CAN_ERR_FLASH_WRITE);
			}

			// --- После ошибки управление также не возобновляется ---
			// Существующее окно HC: 100 тиков, без гарантии доставки CAN.
			if (osDelay(100U) != osOK) {
				Error_Handler();
			}

			NVIC_SystemReset();
			break;
		}

		default:assert(0);
}}