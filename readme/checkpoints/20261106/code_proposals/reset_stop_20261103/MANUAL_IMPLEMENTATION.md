# F002/F006 через SSI STOP — блок для ручного внесения, 03.11.2026

Статус: PROPOSED, проверен кандидат; в проект НЕ внесён.
База Scanner: `ea1eacb`, исходное рабочее дерево чистое. Семь файлов App;
Cube/Core/IOC, обычный успешный scan, F003 и CAN wire-контракт не меняются.
Все шаги ниже составляют один связный блок. Проверка промежуточного состояния
до внесения всех шагов не является проверкой этого решения.

## Требование, эталон и сверка

Требование: [стандарт DDS-240 §3.1.1](../../../DDS-240_ECOSYSTEM_STANDARD.md#311-подготовка-executor-к-сервисному-reset),
[общая инструкция: Safety и граница домена](../../../EXECUTOR_COMMON_LAYERS_GUIDE.md#safety-и-граница-домена),
Scanner U10/U14 и решение пользователя от02/03.11 в отчёте и плане унификации.
Global: F002/F006, ключи и коды ответов сохраняются; новых wire-кодов нет.

Эталон: `STM32F103_heater_cooler`, HEAD `6abc1d2`:
`AppSafety_PrepareReset/IsResetPending/AllOff`,
`Dispatcher_ReadLe16/Dispatcher_HandleServiceCommand`, gates
`HeaterOutputs_Enable` и `PeltierPwm_Start`. Применимые App-файлы эталона
без рабочих изменений; рабочие изменения HC Core/IOC/.cproject существуют,
в этот перенос не включаются. Прочитаны вызовы AllOff и функции выключения.

Сохранены: проверка ключа до latch, атомарные latch/safe GPIO с сохранением
PRIMASK, запрет обычной активации, F006 Flash только после подготовки,
Flash bool/DONE/NACK и дальнейший reset, аварийные handlers без ожиданий.
В HC выходы выключаются локально; готовой функции подтверждения SSI STOP нет.

Все адаптации Scanner этого блока:

1. Общий срок3s от первого latch хранится в app_safety и проверяется по
   HAL tick всеми участниками. Это один deadline; локальные UART bounds
   сохранены и не могут продлить общий срок. Повторный latch срок не сбрасывает.
2. Обычные Wake/Send/SendStart/StartRx после latch запрещены. Приватный
   RX callback продолжает дренирование до окончательного CancelRx. После
   CancelRx признак rx_cancelled запрещает re-arm из позднего callback.
3. Единственный специальный TX API принимает только точные6байт STOP,
   сформированные доменом. Сохраняется существующий IT/CTS/RTS механизм,
   AIM не активируется, D502 START не перезаписывается.
4. Для планового F002/F006 STOP отправляется также при локальном idle или
   DATA_READY: состояние MCU не используется как доказательство состояния
   внешнего модуля. Это отдельный reset-путь; штатное успешное чтение по-прежнему
   не отправляет дополнительный STOP.
5. Старый RX отбрасывается до окончания TX STOP. Последний TC открывает
   накопление нового ответа до CTS HIGH. Задача проверяет именно первый
   ответ нового handshake; не ищет ACK после NAK/неверного пакета.
6. Только после валидного STOP ACK и проверенного закрытия RX публикуется
   prepared; при ошибке/сроке остаётся terminal failure path, без Flash.

SSI-основание пункта5: [Zebra 72E-40451-03 Rev A, §1-4/1-5](https://www.bsr.at/wp-content/uploads/DS3600_Simple-Serial-Interface-Programmer-Guide.pdf#page=15).
При неполном host-пакете модуль формирует NAK; аппаратный handshake следующей
команды отделяет новый приём от старого. При прерывании decoder TX новая
host-команда получает ответ раньше возобновления отложенной передачи.
CTS LOW сохраняется весь пакет STOP, так что старые байты, уже бывшие в линии,
завершаются до открытия накопления в последнем TC. Это применение требований
SSI к исправному модулю, а не измерение фактического экземпляра.
На стенде нужно подтвердить этот порядок, включая частичный START/старый ACK.

STOP прекращает попытку декодирования; он не объявляется аппаратным reset или
очисткой всех внутренних очередей SE965HP. Возможные повторы старого DECODE_DATA
не получают CAN-успеха в отменённой операции; полная NAK/resend-политика остаётся
открытым пунктом U22. Изоляция нового чтения после reboot от таких повторов
этой HOST-моделью не подтверждена и не заявляется закрытой.

## Проверки кандидата и границы доказательств

- HOST модели shutdown: PASS, 18 групп проверок, включая6 позиций прерывания
  START, старый ACK без нового, NAK перед старым ACK, RX/HAL/RTOS ошибки,
  частичный ACK, истечение общего срока, wrap HAL tick, queued/in-flight,
  F002/F006 с valid/invalid key и F006 Flash failure.
- Регрессия прежних13 сценариев задачи чтения: PASS. При reset ожидается
  согласованное изменение — один проверенный STOP, вместо прежней отмены RX.
- ARM GCC13.3.1: `-fsyntax-only -Wall -Wextra -Werror`, четыре затронутых C-файла
  с реальными HAL/RTOS/project headers: PASS. Это НЕ compile/link/BUILD.
- Статическая сверка: обычный scan-task, capture, terminal-gates, AllOff,
  обычный Wake, AbortTx, BeginRead/EndRead/GetReceived и service handlers
  сохранены; полный список адаптаций приведён выше.
- BUILD/Release/BENCH: NOT RUN. Firmware/IOC проекта не менялись.

Артефакты: [evidence.json](evidence.json), [HOST](host_result.txt),
[регрессия](regression_result.txt), [ARM syntax](syntax_result.txt).
[candidate/](candidate/) содержит полные семь файлов для сверки, а не команду
автоматической установки. Для воспроизведения HOST: `python3 run_host.py`.

## Точные блоки для внесения

Все пути ниже относительно `STM32F103_scanner/`. В каждом пункте приведены
место, действие и полный заменяющий код. После внесения требуется аудит
фактических файлов и штатная Debug-сборка.


### 1. App/inc/app_safety.h

**Место:** Объявление `AppSafety_IsResetPending` перед `#endif`.

**Действие: ЗАМЕНИТЬ.** Добавляет проверку одного общего срока.

```c
bool AppSafety_IsResetPending(void);

// --- Общий предел подготовки F002/F006: 3 секунды от первого latch ---
// Не перезапускает срок; используется в task context без ожиданий.
bool AppSafety_IsResetPreparationExpired(void);
```


### 2. App/src/app_safety.c

**Место:** Существующее объявление `app_safety_reset_pending`.

**Действие: ЗАМЕНИТЬ.** Начало общего срока хранится рядом с latch; никаких RTOS-зависимостей.

```c
static volatile bool app_safety_reset_pending = false;

// --- Единый срок подготовки планового reset ---
// HAL tick используется и в UART; ожиданий и зависимости от RTOS здесь нет.
static uint32_t app_safety_reset_started;
#define APP_RESET_PREPARE_TIMEOUT_MS 3000U
```


### 3. App/src/app_safety.c

**Место:** От комментария «Атомарная фиксация запрета…» до конца файла.

**Действие: ЗАМЕНИТЬ.** Атомарность и safe GPIO по HC сохранены; timestamp ставится один раз, повторный вызов не продлевает 3s.

```c
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
```


### 4. App/inc/scan_uart_driver.h

**Место:** Объявление `ScanUart_SendStart`, перед комментарием `Task-only coherent copy`.

**Действие: ЗАМЕНИТЬ.** Объявление STOP-only API; проверка SSI ACK остаётся в задаче.

```c
bool ScanUart_SendStart(const uint8_t *data, uint16_t len);

// --- Task-only STOP после latch, без AIM/WAKE ---
// Принимает только точный шестибайтовый SSI STOP; D502 не меняется.
// true означает завершение TX/handshake; ACK ещё должна проверить задача.
bool ScanUart_SendStopForReset(const uint8_t *data, uint16_t len);
```


### 5. App/src/scan_uart_driver.c

**Место:** Объявление `read_error`, перед независимым capture.

**Действие: ЗАМЕНИТЬ.** Приватные признаки RX-closure и владения STOP; обычные API не получают разрешения после latch.

```c
static volatile bool read_error;

// --- Владение RX при плановом выключении ---
// После latch RX дренирует прежний обмен до окончательной отмены задачей.
// Накопление ответа STOP открывается только после последнего TX completion.
static volatile bool rx_cancelled;
static volatile bool reset_stop_active;

static bool ScanUart_StartRxInternal(bool reset_drain);
```


### 6. App/src/scan_uart_driver.c

**Место:** Функция `void HAL_UART_TxCpltCallback(` целиком.

**Действие: ЗАМЕНИТЬ.** Граница накопления нового ответа устанавливается до CTS HIGH в последнем TC; старые байты не подтверждают STOP.

```c
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
```


### 7. App/src/scan_uart_driver.c

**Место:** Функция `void HAL_UART_ErrorCallback(` целиком.

**Действие: ЗАМЕНИТЬ.** RX дренирует UART после latch; ошибки подтверждения STOP остаются наблюдаемыми.

```c
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

	if (rx_cancelled)
		return;

	if (read_active
			&& (!AppSafety_IsResetPending() || reset_stop_active))
		read_error = true;

	// --- RX нужен для завершения обмена, даже после планового latch ---
	if (huart->RxState == HAL_UART_STATE_READY)
		(void) ScanUart_StartRxInternal(true);

	if (!AppSafety_IsResetPending() || reset_stop_active)
		ScanUart_NotifyRead();
}
```


### 8. App/src/scan_uart_driver.c

**Место:** Комментарий «Отменяет RX…» и вся функция `ScanUart_CancelRx`.

**Действие: ЗАМЕНИТЬ.** Закрытие фиксируется до AbortReceive; поздний callback не запускает RX вновь.

```c
/*
 * Отменяет RX после установки reset-latch.
 * Вызывает задача Scanner; диагностика сохраняется.
 * rx_cancelled запрещает повторный запуск из позднего callback.
 */
bool ScanUart_CancelRx(void) {
	// --- Закрытие сырого захвата ---
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
```


### 9. App/src/scan_uart_driver.c

**Место:** Блок от `// --- Инициализация приёма;` до следующего ` * Выполняет аппаратный wake` (следующий блок не включать).

**Действие: ЗАМЕНИТЬ.** Init, приватный re-arm, обычный StartRx и WaitRts связаны вместе. Обычные gates сохранены; у STOP тот же deadline, что у Dispatcher.

```c
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
 * Вызывается при инициализации и из RX callbacks.
 * Проверка reset-latch и запуск неделимы.
 */
static bool ScanUart_StartRxInternal(bool reset_drain) {
	// --- Защищённая проверка запрета приёма ---
	uint32_t irq_state = __get_PRIMASK();
	__disable_irq();

	if (rx_cancelled || (AppSafety_IsResetPending() && !reset_drain)) {
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

// --- Обычный запуск RX после latch остаётся запрещён ---
bool ScanUart_StartRx(void) {
    return ScanUart_StartRxInternal(false);
}

// --- Допуск только обычной передачи либо специального STOP ---
static bool ScanUart_IsTxAllowed(bool reset_stop) {
    if (reset_stop)
        return AppSafety_IsResetPending()
                && !AppSafety_IsResetPreparationExpired();

    return !AppSafety_IsResetPending();
}

/*
 * Ожидает уровень RTS в пределах существующего таймаута.
 * Обычное ожидание прерывает latch; STOP ограничен общим сроком reset.
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
```


### 10. App/src/scan_uart_driver.c

**Место:** Блок от `// --- Побайтовая передача SSI через UART-прерывания ---` до следующего ` * Открывает накопление ответа перед wake/START.` (следующий блок не включать).

**Действие: ЗАМЕНИТЬ.** Существующий побайтовый IT TX сохранён. Новый API принимает только точный STOP; CTS/RTS без AIM, обычные Send/SendStart после latch запрещены.

```c
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
 * Пакет формирует домен. Проверка байтов не допускает произвольные команды.
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
```


### 11. App/src/scan_uart_driver.c

**Место:** Функция `void HAL_UART_RxCpltCallback(` целиком.

**Действие: ЗАМЕНИТЬ.** После latch старый обмен дренируется; накопление нового ответа разрешает только владелец STOP. SSI в ISR не разбирается.

```c
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

	// --- Независимый сырой capture, включая повреждённый байт ---
	if (capture_active && !AppSafety_IsResetPending()) {
		capture.errors |= errors;

		if (capture.length < SCAN_UART_CAPTURE_SIZE)
			capture.bytes[capture.length++] = byte;
		else
			capture.overflow = true;
	}

	// --- Добавление только к разрешённому владельцем обмену ---
	if (read_active && accepting) {
		if (errors != HAL_UART_ERROR_NONE
				|| holding_len == SCAN_UART_PENDING_SIZE)
			read_error = true;
		else
			holding_buf[holding_len++] = byte;
	}

	// --- Дренирование сохраняется до окончательного CancelRx ---
	if (errors == HAL_UART_ERROR_NONE)
		(void) ScanUart_StartRxInternal(true);

	if (accepting)
		ScanUart_NotifyRead();
}
```


### 12. App/inc/tasks/task_scan_controller.h

**Место:** Комментарий перед `ScanTask_IsResetPrepared`.

**Действие: ЗАМЕНИТЬ.** Уточнение значения prepared.

```c
// Устанавливается задачей Scanner после подтверждённого STOP и закрытия RX.
```


### 13. App/src/tasks/task_scan_controller.c

**Место:** Комментарий перед `Scan_SyncResetState` и функция целиком, до комментария перед `Scan_TryAcceptTerminalResult`.

**Действие: ЗАМЕНИТЬ.** Вместо прежнего CancelRx/prepared: STOP, первый валидный ACK, закрытие RX, prepared. Неуспех терминален. Обычная задача и её CAN terminal-gates не меняются.

```c
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
```


### 14. App/src/tasks/task_dispatcher.c

**Место:** Блок от `/*` до следующего `// --- F004:` (следующий блок не включать).

**Действие: ЗАМЕНИТЬ.** Dispatcher использует тот же срок от latch; обработчики ключей, F006 Flash bool, ответы и окно reset сохранены.

```c
/*
 * Фиксирует latch/safe-state и ждёт подтверждённой остановки Scanner.
 * Единые 3 секунды начинаются в AppSafety_PrepareReset и не продлеваются.
 * Отказ подготовки терминален; F006 не начинает Flash до подтверждения.
 */
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
```
