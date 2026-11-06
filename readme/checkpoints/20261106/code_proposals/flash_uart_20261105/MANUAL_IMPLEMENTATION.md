# F003: согласованный кандидат для ручного внесения, 05.11.2026

Статус PROPOSED: firmware проекта не изменён. Шесть шагов в трёх App-файлах.
База: ea1eacb + установленный shutdown F002/F006 от03.11; SHA256 трёх
исходных файлов — source_hashes.json. Начальное владение охватывает startup,
резервирование перед enqueue — очередь и исполнение, освобождение — следующий
оборот внешнего цикла после полной обработки. Release вызывается ровно один
раз для каждого Reserve и один раз для startup; при scan_queue длиной4
счётчик ограничен числом queued/in-flight и текущим резервированием Dispatcher.
Новое API требует сохранения единственного producer/исполнителя F003.

Требование: стандарт DDS-240 §3.1/§3.1.1, global config F003/DEVICE_BUSY/
FLASH_WRITE, общая инструкция Dispatcher и Flash, решение пользователя05.11.
Эталон: HC6abc1d2 Dispatcher_HandleServiceCommand/AppConfig_Commit и
Thermo c43368e app_start_task_dispatcher. Применимые файлы эталонов без
рабочих изменений при сверке. Bool Commit -> DONE/FLASH_WRITE сохранён;
счётчик, startup token и BUSY gate — все отличия этой адаптации Scanner.
AppConfig_Commit, драйвер UART, F002/F006, очередь/RTOS config не меняются.

Без ожиданий внутри IRQ-save секций, новых timeout/fallback/кодов ошибок.
UART RX остаётся включённым в idle; произвольные запоздалые байты во время
Flash этим механизмом не защищены. CODE/HOST/BUILD/BENCH следует различать.

## 1. App/inc/tasks/task_scan_controller.h

ВСТАВИТЬ объявления перед подтверждением reset.

Точный исходный фрагмент:

```c
// --- Подтверждение доменной подготовки к reset ---
```

ЗАМЕНИТЬ на полный фрагмент:

```c
// --- Учёт queued/in-flight команд для допуска F003 ---
// Reserve вызывается Dispatcher до enqueue; Release — при отказе enqueue
// либо задачей Scanner после полной обработки. Busy читает только Dispatcher:
// он единственный producer scan_queue и не ставит команды во время Commit.
void ScanTask_ReserveCommand(void);
void ScanTask_ReleaseCommand(void);
bool ScanTask_IsFlashCommitBusy(void);

// --- Подтверждение доменной подготовки к reset ---
```

## 2. App/src/tasks/task_scan_controller.c

ВСТАВИТЬ счётчик и три функции после includes, перед scan_reset_prepared.

Точный исходный фрагмент:

```c
// --- Подтверждение подготовки; единственный писатель — задача Scanner ---
```

ЗАМЕНИТЬ на полный фрагмент:

```c
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
```

## 3. App/src/tasks/task_scan_controller.c

ЗАМЕНИТЬ начало внешнего for в app_start_task_scan_controller; startup выше не менять.

Точный исходный фрагмент:

```c
	for (;;) {

		// --- Подготовка выполняется и при отсутствии команд ---
```

ЗАМЕНИТЬ на полный фрагмент:

```c
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
```

## 4. App/src/tasks/task_scan_controller.c

ВСТАВИТЬ принятие владения после успешного Get, перед heartbeat.

Точный исходный фрагмент:

```c
		// --- Команда получена и передаётся на обработку ---
		AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
```

ЗАМЕНИТЬ на полный фрагмент:

```c
		// --- Принятие владения, уже зарезервированного Dispatcher ---
		// Dequeue не уменьшает счётчик: он остаётся занят до конца обработки.
		release_pending = true;

		// --- Команда получена и передаётся на обработку ---
		AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN);
```

## 5. App/src/tasks/task_dispatcher.c

ЗАМЕНИТЬ начало постановки доменной команды; сохранить NACK и конец ветки.

Точный исходный фрагмент:

```c
			if (osMessageQueuePut(scan_queueHandle, &scan_cmd, 0, 0) != osOK) {
				CAN_Diagnostics_RecordAppQueueOverflow();
```

ЗАМЕНИТЬ на полный фрагмент:

```c
			// --- Учёт начинается до enqueue и сохраняется после dequeue ---
			ScanTask_ReserveCommand();
			if (osMessageQueuePut(scan_queueHandle, &scan_cmd, 0, 0) != osOK) {
				// Отказ очереди оставляет владение у Dispatcher.
				ScanTask_ReleaseCommand();
				CAN_Diagnostics_RecordAppQueueOverflow();
```

## 6. App/src/tasks/task_dispatcher.c

ВСТАВИТЬ BUSY gate в начало F003; существующий bool Commit/ответы не менять.

Точный исходный фрагмент:

```c
		case CAN_CMD_SRV_FLASH_COMMIT: {
			// --- COMMIT: сохранение RAM-конфигурации в Flash ---
```

ЗАМЕНИТЬ на полный фрагмент:

```c
		case CAN_CMD_SRV_FLASH_COMMIT: {
			// --- Занятый домен не прерываем и не ждём ради Commit ---
			// Dispatcher — единственный producer scan_queue; после нулевого
			// снимка новая операция не появится до завершения этого handler.
			if (ScanTask_IsFlashCommitBusy()) {
				CAN_SendNackPublic(parsed.cmd_code, CAN_ERR_DEVICE_BUSY);
				break;
			}

			// --- COMMIT: сохранение RAM-конфигурации в Flash ---
```

