# Scanner — аудит и план унификации общих слоёв

## Актуальный срез — 06.11.2026

Установлены F002/F006 STOP-only shutdown с общим3s, F003 guard,
SSI status codec, UART RX boundary и подтверждённая подготовка STOP/FLUSH
перед START. Последний Debug48740/72/15136,0errors/0warnings; HOST20
preparation scenarios/36commands +18shutdown groups PASS. Пользователь
подтвердил чистую сборку. BENCH/Release NOT RUN.
Следующий блок — barcode receive/recovery и проверка непустой строки
в controller. Это AGREED, ещё НЕ installed CODE. Начинать с NEXT_SESSION
и последних датированных записей; старая матрица FAIL не отражает внесённые
блоки. Полный кандидат20 замен не применять заново поверх текущего дерева.
Текущий снимок/коммит запрошен пользователем, parent ea1eacb.

## Историческая точка — 02.11.2026

Точка останова — конец02.11.2026. Следующий согласованный блок:
управляемая подготовка F002/F006 со STOP-only путём после reset-latch,
общий предел3s на всю подготовку. AGREED, ещё не реализовано. См. последний
раздел и NEXT_SESSION_PROMPT.md. Состояние кода описано ниже отдельно.

Актуальное состояние02.11.2026: RX и TX USART3 переведены на UART-прерывания,
накопитель RX128B; оба DMA-запроса удалены пользователем через Cube.
См. раздел «UART RX/TX без DMA — фактическое состояние02.11.2026» в конце
документа. CODE частично, BUILD чистый по сообщению пользователя;
закрытие RX перед terminal выполнено и проверено02.11; BENCH остаётся OPEN.
См. последний раздел «U22 — закрытие RX на завершении запроса02.11.2026».
Назначение STOP уточнено в разделе
разделе «Остановка сканера перед reset и Flash — уточнение02.11.2026»:
обязательный STOP ACK после успеха не вводится; подготовка F002/F006 и
защита F003 от активного обмена требуют отдельной реализации.

Дата исходного аудита: 28.10.2026. Актуальные результаты — в разделах29/30.10.
Формулировки исходной матрицы ниже описывают состояние до исправлений.
Галочка означает выполненную проверку/работу, а не принятие платы целиком.
CODE/HOST/BUILD/BENCH различаются; прежние сборки и тесты сами по себе
не закрывают последующие изменения.

## Выполнение на29.10.2026 — статус на конец дня

- [x] U01 CODE: CAN_ConfigureFilterBank/CAN_UpdateDirectFilter совпадают
  с HC6abc1d2 по коду без комментариев/форматирования; bank0 broadcast,
  bank1 runtime NodeID, настройка до HAL_CAN_Start; прототип/include связаны.
- [x] U09 CODE: F005 использует отдельный Dispatcher_IsValidNodeId,
  Scanner60..6F; RAM -> direct bank1 -> DONE. Отличия HC: семейство,
  локальная parsed и существующее имя CAN_SendNackPublic. Повтор helper удалён.
  Валидация длины кадра уже находится до Dispatcher; data_len=5 задаёт парсер.
  Reserved отправляются нулевыми по wire-контракту; дополнительных проверок
  приёмника в F005 нет, как в HC.
- [x] BUILD: пользователь сообщил чистую сборку после добавления include.
  Это результат пользователя; новый независимый build ассистентом не выполнялся.
- [ ] U01/U09 BENCH: direct/broadcast и смена60->6F->60 ещё не проверены.
- [x] U02 AGREED29.10: software acceptance и parser по HC согласованы.
- [x] U02 CODE/BUILD29.10: software acceptance, parser и RX-путь
  сверены с HC; проект собран ассистентом без ошибок/предупреждений.
- [ ] U02 HOST/BENCH: сценарии фильтрации/распаковки ещё не подтверждены.
- [x] U06 CODE/HOST29.10: функция snapshot совпадает с HC; проверены
  PRIMASK0/1, копирование под запретом IRQ, полнота снимка и NULL без эффектов.
  [Результат](test_artifacts/u06_20261029/host_result.txt).
- [x] U06 BUILD: пользователь сообщил чистую сборку.
- [ ] U06 BENCH: измерение на MCU не выполнялось; HOST не подменяет его.
- [x] U07 CAN CODE/HOST29.10: блок ожидания совпадает с HC;
  RX/TX/оба флага и idle timeout дают heartbeat, Resource/Parameter — нет.
  [Результат](test_artifacts/u07_can_20261029/host_result.txt).
- [x] U07 CAN BUILD: пользователь сообщил чистую сборку.
- [x] U07 Dispatcher CODE/HOST29.10: ожидание/heartbeat совпадают с HC,
  адаптация только parsed; ACK перед F020/F021 или switch сохранён.
  [Результат](test_artifacts/u07_dispatcher_20261029/host_result.txt).
- [x] U07 Dispatcher BUILD: пользователь сообщил чистую сборку.
- [x] U07 Scan queue CODE/HOST29.10: механизм ожидания очереди соответствует
  HC Dispatcher с адаптацией queue/message/client; обработчики команд сохранены.
  [Результат](test_artifacts/u07_scan_queue_20261029/host_result.txt).
- [x] U07 Scan queue BUILD: пользователь сообщил чистую сборку.
- [x] U07 Scan UART CODE/HOST29.10: heartbeat по исходам ожидания;
  адаптация Scanner — общий RTOS deadline, slice <=500ms и оставшегося срока,
  отдельный E602 при ошибке RTOS, E603 при отсутствии ответа. STOP сохранён.
  HOST: фактически внесённый цикл — срок/RX/error/остаток/tick wrap PASS.
  [Результат](test_artifacts/u07_uart_20261029/host_result.txt).
- [x] U07 Scan UART BUILD: пользователь сообщил чистую сборку.
- [ ] U07 аппаратные проверки остаются открытыми.
  Проверки иных ожиданий драйвера/захвата относятся к последующим блокам;
  завершение данного U07 не означает готовность всей доменной операции.
- [x] U03 CODE/HOST/BUILD29.10: по прямому разрешению пользователя
  перенесены RX pending/error callbacks HC в USER CODE; очередь/overflow,
  HAL RX failure и запись HAL error/ESR проверены HOST; сборка без предупреждений.
  [Артефакт](test_artifacts/u03_20261029/host_result.txt).
- [ ] U03 BENCH: инъекция потерь/ошибок CAN на MCU не выполнялась.
- [x] U04/U08 F004 CODE/HOST29.10: DATA-helper и UID-helper совпадают с HC;
  прототип/три вызова обновлены; F00780 сохранён, F00400/81+DONE.
  HOST: UID bytes, seq, zero padding, length cap/NULL, runtime src — PASS.
  [Артефакт](test_artifacts/u04_f004_20261029/host_result.txt).
- [x] U04/U08 F004 BUILD: пользователь сообщил чистую сборку.
- [ ] U04/U08 BENCH: кадры F004/F007 на шине не подтверждены данным блоком.
- [x] U05 TX wrapper CODE/HOST/BUILD29.10: CAN_QueueTxFrame совпадает
  с HC; NULL/success/full queue/source unchanged PASS, сборка без предупреждений.
  [Артефакт](test_artifacts/u05_20261029/host_result.txt).
- [ ] U05 нагрузочные/аппаратные проверки полноты ответов остаются открытыми;
  очередь не гарантирует физическую доставку. Длинная диагностика не изменялась.
- [x] U11 CODE/HOST/BUILD29.10: по прямому разрешению пользователя F007
  вынесен в Dispatcher_HandleGetStatus по HC, сохранено имя SendStatusMetric.
  HOST:18 IDs/order/LE,seq80,one snapshot,DONE last — PASS; сборка без предупреждений.
  [Артефакт](test_artifacts/u11_20261029/host_result.txt).
- [ ] U11 BENCH: F007 на MCU и достоверность счётчиков при нагрузке не проверены.
- [x] U12 CODE/HOST/BUILD29.10: по прямому разрешению пользователя
  AppConfig_Init перенесён по механизму Thermo c43368e: mutex NULL fatal,
  magic/checksum/family60..6F, defaults в RAM без erase. Layout16B/CRC сохранены.
  HOST: valid/invalid family/magic/CRC, mutex fail/reuse, Flash неизменна — PASS.
  [Артефакт](test_artifacts/u12_20261029/host_result.txt). Сборка без предупреждений.
- [ ] U12 BENCH: загрузка конфигурации на MCU с этими изменениями не проверена.
- [x] U13 CODE/HOST/BUILD29.10: по прямому разрешению пользователя
  AppConfig_Commit/FactoryReset перенесены целиком из HC6abc1d2; исполнимый
  код совпадает. Mutex, проверки Unlock/Erase/Program/Lock, bool FactoryReset;
  F006 использует bool для DONE/NACK FLASH_WRITE, RAM NodeID сохранён.
  HOST: отказы mutex/HAL и каждого из4 слов, layout16B/CRC, ключ/ответ/reset PASS.
  [Артефакты](test_artifacts/u13_20261029/host_result.txt). Сборка без предупреждений.
- [ ] U13 BENCH: сохранение/стирание/reload на MCU не проверены; readback
  не добавлялся. Commit сохраняет поведение HC: результат release не проверяется.
- [ ] U10 остаётся открыт: PrepareReset/latch/safe-state перед F002/F006
  ещё не реализованы; проверка bool Flash не закрывает F006 целиком.
- [ ] Остальные блоки плана остаются открытыми.

Ниже сохранены исходный аудит28.10 и программа работ. FAIL исходной матрицы
описывают состояние до правок; актуальные закрытия CODE перечислены выше.
Чистая сборка не означает закрытия BENCH либо всей унификации.

## Выполнение на30.10.2026 — текущий статус

- [x] U14 CODE частично: app_safety.c/.h, AppSafety_AllOff,
  PrepareReset/IsResetPending; CTS/TRIG/AIMWAKE HIGH, latch по HC6abc1d2.
  Gates wake/CTS/каждого TX DMA/RX start-rearm; отмена RTS/TX wait,
  проверяемый TX abort с terminal Error_Handler при отказе.
  RX callback после latch сохраняет только raw diagnostics;
  bool ScanUart_CancelRx закрывает capture и аннулирует holding_len/rx_prefix.
- [x] U14 BUILD частичных блоков: пользователь сообщил чистые сборки.
- [x] U14 CODE частично, доменная интеграция: Scan_SyncResetState вызывает
  CancelRx, отменяет текущее SCANNING без счётчика timeout, очищает RX flag
  и публикует ScanTask_IsResetPrepared; отказ подготовки -> Error_Handler.
  Checkpoints до ожидания очереди и после получения команды; queued -> DEVICE_BUSY.
  Пользователь сообщил чистую сборку, фактические файлы проверены.
- [x] U14 CODE частично, capture0603/0604: SyncResetState до запроса,
  после отказа wake/send, в цикле capture и после последнего ожидания;
  отмена -> DEVICE_BUSY. Фактический код проверен, BUILD чистый со слов пользователя.
- [x] U14 CODE частично, scan0601: отмена START отделена от UART failure;
  RX wait проверяет latch, после wait SyncResetState -> DEVICE_BUSY и пропуск STOP.
  После ошибки размещения блока ассистент по прямому запросу восстановил
  startup SCAN_ENABLE/объявление cmd и перенёс обработку в START внутри цикла.
  BUILD ассистента PASS; terminal-result race ещё не закрыт.
- [x] U14 CODE частично, terminal0603/0604: Scan_TryAcceptTerminalResult
  принимает решение под IRQ-save до CAN_SendDiagnosticResponse; передача вне
  критической секции. При latch до gate -> SyncResetState/DEVICE_BUSY.
  CODE проверен, пользователь сообщил чистую сборку.
- [x] U14 CODE частично, terminal0601/0602: gate0602 после копии shadow,
  gate0601 до обработки RX/timeout/error и изменения статистики.
  Решение до latch допускает завершение ответа; latch до решения -> DEVICE_BUSY.
  Фактические файлы проверены, BUILD чистый по сообщению пользователя.
- [x] U14 аудит связности после F002/F006: фактические gates, abort/cancel,
  queued/in-flight и terminal checkpoints проверены. HOST21 групп с U10/U16 PASS
  на извлечённом фактическом коде и mock HAL/RTOS; границы модели в evidence.json.
- [x] U14 API cleanup: пользователь удалил повтор ScanTask_IsResetPrepared
  из scan_controller.h; декларация осталась только в task_scan_controller.h.
  Фактические заголовки проверены.
- [x] U14 BUILD30.10: полная принудительная Debug-сборка ассистента,
  0errors/0warnings, text47132/data68/bss15128; хеш ELF сохранён.
- [ ] U14 BENCH: реальные DMA/SSI/GPIO, scheduler races и времена NOT RUN.
- [x] U16 CODE: Error_Handler и NMI/HardFault/MemManage/BusFault/UsageFault
  по Fluidics ac1aa2b: IRQ off -> AppSafety_AllOff -> DSB/ISB -> terminal loop.
- [x] U16 BUILD: пользователь сообщил чистую сборку.
- [x] U16 HOST30.10: фактический префикс Error_Handler и пяти fault handlers
  проверен в модели; terminal loops заменены longjmp только в host harness.
  Проверены IRQ off/safe GPIO/отсутствие RTOS/CAN в этом пути.
- [ ] U16 BENCH: реальный fault exception/safe-state/IWDG recovery NOT RUN.
- [x] U10/U14 AGREED: latch/safe-state в Dispatcher -> подтверждённая
  подготовка задачей Scanner -> F002/F006; Flash только после подтверждения.
  Queued commands -> DEVICE_BUSY; ошибка остановки RX или истечение
  ограниченного ожидания подготовки -> terminal Error_Handler без начала Flash.
  Бюджет подготовки выбран1000ms (одна секунда RTOS), HOST/BENCH ещё открыты.
- [x] U10 CODE частично, F002: ReadLe16/key/length по HC6abc1d2;
  Dispatcher_PrepareReset устанавливает latch, ждёт prepared до1s,
  затем DONE -> проверяемый osDelay100ticks -> reset.
  При deadline/ошибке ожидания -> terminal Error_Handler.
  CODE проверен, BUILD чистый по сообщению пользователя.
- [x] U10 F006 CODE/BUILD: key/length -> подтверждённая подготовка -> Flash bool
  -> DONE/FLASH_WRITE -> проверяемый osDelay100ticks -> reset. CODE проверен,
  пользователь сообщил чистую сборку; полная сборка ассистента также PASS.
- [x] U10 HOST30.10: invalid key/length без latch/Flash; подготовка500ticks
  перед Flash/ответом, F006 Flash failure, deadline1000ticks, delay/RX failures,
  tick wrap и другая RTOS frequency; выполнено на фактических функциях с mocks.
- [ ] U10 BENCH подготовки/reset/Flash/CAN остаётся открытым.

Артефакты проверки D: [u10_u14_u16_20261030](test_artifacts/u10_u14_u16_20261030/),
host_result.txt/run_host.py/host.c/extracted.inc/evidence.json/build.log.

### Начало E — проверка watchdog-контракта30.10

Фактический Scanner:500ms/3misses, supervisor Normal ниже CAN High.
HC6abc1d2:500ms/2misses, supervisor Realtime; App-источник без рабочих изменений.
§2.7.1 стандарта требует прогресса каждого клиента в предыдущем окне;
§2.7.3 разрешает500ms/2misses только как эксперимент HC.
Перенос threshold HC или сохранение500ms/3misses Scanner не является
автоматически принятым watchdog-профилем. Предложение для согласования:
Scanner1000ms/1miss по глобальному reference, idle500ms сохраняется.
Независимые переносы проверки osDelay и terminal Error_Handler допустимы
до выбора профиля, но сами по себе U15 не закрывают.

### Завершённые части E — уточнение30.10

- [x] U15 AGREED/CODE: профиль Scanner1000ms/1miss, idle500ms;
  три клиента CAN/Dispatcher/Scan. Supervisor перенесён по HC6abc1d2:
  period в RTOS ticks, нулевой period/ошибка osDelay/отказ клиента -> Error_Handler.
  Различие с HC500ms/2misses — согласованный нормативный профиль Scanner.
- [x] U15 CODE: supervisor Realtime48 выше CAN High40; main.c и .ioc согласованы.
- [x] U15 HOST: штатный прогресс; отсутствие прогресса каждого клиента;
  immediate/later wait error; zero period; counter wrap;100/1000Hz;
  invalid client. Mock RTOS/IWDG, реальное вытеснение не моделировалось.
- [x] U15 BUILD: проверочная Debug-сборка ассистента без ошибок/предупреждений.
- [x] U17 CODE: проверки4queue и4thread в USER CODE до запуска scheduler,
  по HC с адаптацией Scanner handles; NULL -> AllOff/Error_Handler.
- [x] U17 HOST:32 комбинации NULL/non-NULL фактических guard-блоков PASS.
  Это модель проверок, не инъекция реального отказа FreeRTOS allocator.
- [x] U17 BUILD: пользователь сообщил чистую сборку.
- [x] U18 статический расчёт памяти: ARM sizeof и heap_4 дают6688B
  на4task/4queue/config mutex из8192B; остаток1504B до служебного
  выравнивания/end marker и иных allocations. Idle/timer task/queue статические.
- [ ] U15/U17/U18 BENCH: idle/fault/IWDG recovery, фактический heap minimum
  и stack high-water mark открыты. Кадры Scan task+ScanQueryRevision ранее
  занимали944B из1536B без остальных вложенных вызовов; это не stack bound.
- [x] U19 Debug30.10: после последней SSI-правки перекомпилирована задача и
  выполнена линковка; 0errors/0warnings, text47804/data68/bss15392. SHA256 ELF
  и build.log: test_artifacts/ssi_packet_assembly_20261030/.
- [ ] U19 Release NOT RUN; итоговая приёмка всей платы остаётся открытой.

Артефакты завершённых HOST-проверок E:
[u15_u17_20261030](test_artifacts/u15_u17_20261030/).

### Согласованные решения SSI —30.10

- [x] AGREED: целевой ответ штрихкода — SSI DECODE_DATA F3, параметрEE=1
  (packeted). Заводской raw не является целевым режимом этой реализации.
  Фактическая настройка физического модуля остаётся BENCH NOT CONFIRMED.
- [x] AGREED: сохраняем текущую побайтовую TX DMA и контроль RTS/CTS,
  локальные пределы ожиданий. Дополнительный общий TX deadline сейчас не вводим.
- [x] AGREED/CODE: входящий валидный SSI ACK не завершает0601 и не
  перезапускает timeout_sec; при0 используется4s. Отсчёт остаётся после START
  и handshake. Scan_SkipAckPackets сохраняет следующий пакет в том же RX-событии.
- [x] CODE: GetReceived возвращает IRQ-safe snapshot; RX HT отключён
  после успешного запуска DMA_NORMAL; IDLE/TC сохраняются.
- [x] CODE/BUILD: DECODE_DATA data[0] — Bar Code Type, строка копируется
  с data[1], длина data_len-1; отсутствие Type отклоняется. CODE проверен,
  пользователь сообщил чистую сборку. Подготовленная правка проверена HOST;
  итоговая регрессия всего RX после всех изменений ещё открыта.
- [x] AGREED: нормальный порядок — получить/проверить DECODE_DATA ->
  исходящий SSI CMD_ACK сканеру -> CAN DATA фрагменты Дирижёру -> CAN DONE.
- [x] AGREED: после передачи SSI CMD_ACK МК ждёт отпускания RTS в HIGH,
  затем отправляет CAN DATA и DONE. Это завершение аппаратного обмена;
  отдельного ответа о принятии ACK протокол не предусматривает.
- [x] AGREED: сбой отправки SSI ACK — CAN NACK E602 (SCAN_COMM), без DATA/DONE.
  Reset сохраняет приоритет: DEVICE_BUSY вместо E602.
- [x] CODE/BUILD: связанный блок проверки DECODE_DATA без изменения состояния,
  отправки CMD_ACK перед принятием результата и обработки E602 внесён пользователем,
  фактический код проверен; пользователь сообщил чистую сборку. HOST фактических исходников PASS
  (обычный обмен, неверная checksum, отказ TX ACK, reset во время ACK,
  входящий ACK без перезапуска срока ожидания); BENCH NOT RUN.
  Доказательства: test_artifacts/ssi_outgoing_ack_20261030/ (runner, модель,
  результат, SHA256 исходников). Отличия от кандидата — оформление и место helper.
- [ ] OPEN: исходящий SSI NAK и resend policy не согласованы.
- [x] U22 CODE/HOST/BUILD частично: Scan_CollectPacket вместо Scan_SkipAckPackets,
  сборка одного SSI-пакета по Length+2 в статическом rx_packet. Части сохраняются
  между чтениями задачи; полный валидный ACK пропускается, включая разделённый.
  Срок запроса не перезапускается; неполный пакет по истечении срока -> E603.
  Некорректная длина/checksum остаются в существующей обработке E601.
  Исходящий ACK и reset gates сохранены. Правка внесена ассистентом по прямому
  разрешению; HOST фактических исходников PASS, Debug без ошибок/предупреждений.
  Артефакты: test_artifacts/ssi_packet_assembly_20261030/.
- [ ] U22 OPEN — следующая работа: сохранение RX-событий до чтения задачей.
  Callback всё ещё перезаписывает holding_buf/holding_len, GetReceived делает
  снимок без освобождения данных; последовательные callbacks могут заместить
  непрочитанные байты. Новые RX-события не означают новый штрихкод.
  Предыдущий блок накопления драйвера/out_overflow НЕ внесён и отложен.
  Механизм и поведение при исчерпании ёмкости согласовать перед выдачей кода.
  Кольцевой буфер и увеличение DMA не приняты как решение.
- [x] Уточнение размеров:134B — локальная ёмкость SSI_MAX_PAYLOAD128+header4+
  checksum2 из первой реализации3e82ec7, не измеренная длина ответа сканера.
  В DECODE_DATA тело включает Type1B; строка из64ASCII без prefix/suffix
  занимает71B в SSI и помещается в DMA128B. Увеличение DMA на основании134B
  не согласовано. Ограничение64 в старом плане предварительное, не физическое
  ограничение SE965HP и не подтверждённый максимум всех реагентных этикеток.
- [ ] BENCH: валидный SSI-ответ/штрихкод и настройка модуля не подтверждены.
  Последний сохранённый результат27.10 — RX0, его причина не установлена.

Новые изменения RX требуют отдельной регрессии reset/cancel/terminal gates;
прежние HOST-снимки блока D не являются доказательством для изменённых файлов.

Общий блок D остаётся открытым. GPIO HIGH не доказывает остановку
сканирования, ранее запущенного SSI. Свежие статусы уточняют исходную
матрицу28.10 и записи29.10, не заменяя их исторические результаты.

## 1. Основание, версии и порядок работы

Действующие [правила Scanner](WORK_RULES.md) обновлены28.10 по последним
правилам Sensor/§4 Thermo. Перед выполнением каждого блока обязательны
сверка контракта и конкретного эталонного механизма, границы, согласование,
точные блоки для пользователя, затем аудит фактических файлов.
Само наличие пункта в плане разрешением на реализацию не является.


- [x] Прочитаны локальные правила Scanner: сначала решение/точные блоки,
  firmware вносит пользователь, затем аудит фактических файлов.
- [x] Прочитаны [руководство общих слоёв](../EXECUTOR_COMMON_LAYERS_GUIDE.md),
  [канонический header](../dds240_global_config.h),
  [CAN-матрица](../CAN_TRANSPORT_AUDIT_MATRIX.md).
- [x] Проверены фактические функции образцов, а не только имена проектов:
  HC `6abc1d2706acac36d842594e6331de180c2241ea`,
  Thermo `c43368ec5509f2693caed8797d370047af0af346`,
  Fluidics `ac1aa2b12b3d270d98f080a8b0bdbe5e4ad9ca49`.
  HC Core/main.h/.ioc/.cproject имеют рабочие изменения; App-источники
  соответствуют роли эталона. Точные worktree-хеши и статусы записаны в
  [sources.json](test_artifacts/unification_audit_20261028/sources.json).
- [x] Зафиксирован Scanner HEAD `99acccd`; новые GPIO пользователя и ранее
  внесённый ассистентом handshake НЕ входят в HEAD. Их наличие не означает
  согласование реализации. Не откатывать вместе с ними пользовательский Cube.
- [x] Статически проверены все слои: HAL/IRQ, CAN transport, Dispatcher/service,
  config/Flash, safety, watchdog/fatal, RTOS/build, Scanner/Studio contract.
- [x] 29.10 согласован первый объём: два банка, software validation, F005.
- [ ] Повторно сверить ранее выданные блоки по обязательной процедуре29.10
  [WORK_RULES.md](WORK_RULES.md). Выдача не означает принятие реализации.
- [ ] Переиздать F005: структура helper/handler HC, явные необходимые адаптации;
  reserved/data_len расхождение с HC разрешить до включения в код.
- [ ] После каждого блока проверить фактическую правку и нужную регрессию.
  Не менять исходники самостоятельно; NEXT_SESSION только при завершении сессии.

## 2. CAN-фильтры: ответ на вопрос о двух банках

**Сейчас не два банка.** В `App/src/tasks/task_can_handler.c:279–294`
настраивается только bank0, IDMASK32, mask IDE=1. Любой Extended ID проходит
аппаратно, остальные ограничения возложены на программу. bank1 исходниками
не настраивается. Состояние регистров реальной платы здесь не измерено.

Эталон HC: `CAN_ConfigureFilterBank`, `CAN_UpdateDirectFilter`, startup
`app_start_task_can_handler`. Нужны два IDMASK32 банка FIFO0:

| Банк | Принимает | FilterIdHigh / Low при Node60 | MaskHigh / Low |
|---:|---|---|---|
| 0 | broadcast COMMAND, dst00 | 0000 / 0004 | 1FF8 / 0004 |
| 1 | direct COMMAND, текущий dst60 | 0300 / 0004 | 1FF8 / 0004 |

ID-регистр: `(CAN_BUILD_ID(0, COMMAND, dst, 0) << 3) | (1 << 2)`.
Маска в координатах bxCAN: `(0x03FF << 19) | (1 << 2)`.
Проверяются dst/type/IDE; source, RTR, priority аппаратной маской НЕ ограничивать.
Источник и RTR проверяются программно. На F005 обновляется bank1, bank0 неизменен.
Порядок: configure banks -> Start -> notifications, каждый HAL result проверять.

- [x] U01 CODE29.10: заменён permissive bank0; исходная задача — заменить permissive bank0 на broadcast+runtime-direct по HC;
  добавить объявление/вызов обновления direct filter и связать с F005.
- [ ] Проверить моделью маски dst00/60/6F/чужой, COMMAND/ACK/DATA, IDE;
  отдельно показать, что RTR/src требуют программной проверки.
- [ ] На стенде проверить broadcast F001 и direct F001 до/после F005.

## 3. Матрица расхождений по слоям

Пути Scanner относительно STM32F103_scanner. HC/Thermo/Fluidics — отдельные
проекты workspace. FAIL/PARTIAL ниже относятся к CODE, не к аппаратуре.

| ID | Факт Scanner и статус | Эталон: файл / функция | Требуемая адаптация и критерий |
|---|---|---|---|
| U01 | FAIL: один permissive bank0 | HC task_can_handler.c / CAN_ConfigureFilterBank, CAN_UpdateDirectFilter | Два банка как §2; HAL failures -> fatal; смена Node обновляет bank1 |
| U02 | FAIL: RX порядок IDE -> dst -> type -> DLC, нет RTR/src | HC task_can_handler.c / CAN_IsAcceptedCommand, CAN_ParseCommand | IDE -> RTR -> DLC -> type -> src -> dst; source10; invalid без ACK/NACK; прежние common counters |
| U03 | PARTIAL: ISR передаёт raw queue, но overflow только TODO; CAN error/FIFO callbacks не подключены | HC Core main.c + it.c / HAL_CAN callbacks; task_can_handler diagnostics | Подключить overflow, HAL/ESR/FIFO события к существующим метрикам; без CAN-ответов из ISR |
| U04 | PASS по CODE: ACK/NACK/DATA/DONE полностью memset, ExtId runtime, IDE/RTR/DLC заданы; PARTIAL: DATA helper всегда80 | HC task_can_handler.c / CAN_Send* | Сохранить обнуление и runtime src, добавить явный sequence_info в DATA и обновить вызовы |
| U05 | PARTIAL: RX/Dispatcher/application/TX queue failure учитываются, кроме ISR RX; обычный enqueue void может потерять DATA, затем поставить DONE | HC CAN_QueueTxFrame; текущий Scanner CAN_SendDiagnosticResponse | Сохранить common queue policy; не заявлять доставку по enqueue. Проверить полноту ответов при давлении очереди, не лечить только увеличением queue |
| U06 | FAIL: CAN_Diagnostics_GetSnapshot безусловно включает IRQ | HC CAN_Diagnostics_GetSnapshot | Сохранить/восстановить PRIMASK, тест вызова при IRQ enabled и disabled |
| U07 | FAIL: heartbeat CAN отмечается при любом flags result; Dispatcher/Scan — до проверки queue result | HC task_can_handler.c / app_start_task_can_handler; проверка RTOS путей | Штатное событие/timeout считаются прогрессом, Resource/Parameter/error — нет |
| U08 | PARTIAL: F001 выдаёт00/01/82 через fragment helper; F004 выдаёт80/80 | HC Dispatcher_HandleGetDeviceInfo/GetUid | F001 сохранить; F004 DATA00/81; нулевой padding; один UID source |
| U09 | FAIL: F005 принимает02..7F (чужие семьи); нет direct-update | HC Dispatcher_IsValidNodeId/HandleServiceCommand + global family ranges | Только Scanner60..6F, остальные INVALID_PARAM без RAM/filter side effects; old ACK/new DONE; padding по контракту |
| U10 | FAIL: F002/F006 без PrepareReset; F006 void/unchecked erase и всегда DONE | HC Dispatcher_HandleServiceCommand; Thermo/Fluidics reset-перенос | Ключ -> latch/safe-state -> для F006 Flash bool -> DONE/NACK -> bounded reset; неверный ключ ничего не отменяет |
| U11 | PARTIAL: F007 IDs0001..0012 и80-records правильны, snapshot/часть источников недостоверны | HC Dispatcher_HandleGetStatus + CAN diagnostics | Один coherent snapshot, подключить источники U03/U06, не добавлять UART значения в common metrics |
| U12 | FAIL: config mutex NULL не проверен; Flash load только magic/CRC без family NodeID | Thermo app_flash.c / AppConfig_Init | NULL -> fatal; magic/checksum/60..6F; invalid -> RAM defaults без erase |
| U13 | FAIL: Commit игнорирует Unlock/Lock; FactoryReset без mutex и bool | HC/Thermo app_flash.c / AppConfig_Commit/FactoryReset | Сериализация, failed Unlock запрещает erase/program, учитывать Erase/Program/Lock; RAM address сохранять до reset |
| U14 | FAIL: safe-state обслуживает STBY/LED, не CTS/TRIG/AIM; нет latch/gates/cancel | HC app_safety.c + heater_outputs.c; Fluidics controller cancellation | Reset latch атомарно с IRQ-save; запрет новых wake/SSI/trigger, release outputs; отмена queued/in-flight/late completion |
| U15 | FAIL: watchdog Normal ниже CAN High; bad client -> RTOS sleep loop; osDelay result не проверен | HC task_watchdog.c / app_start_task_watchdog | Supervisor выше CAN; period и return checks; fatal при отказе; локальные 3 клиента/500ms/3 пропуска обосновать, не копировать числа HC |
| U16 | FAIL: Error_Handler safe-state до IRQ-off; fault handlers пустые loops | Fluidics Core main.c/it.c / Error_Handler и fault handlers | IRQ off -> safe-state -> DSB/ISB -> terminal loop без RTOS/refresh, восстановление IWDG |
| U17 | FAIL: четыре queue и четыре thread создаются без NULL checks | HC/Fluidics Core main.c / RTOS creation | Каждый объект checked до допуска операций; при ошибке safe fatal; ownership/stack/heap подтвердить |
| U18 | PASS по CODE: tick1000, структура ParsedCanCommand_t без envelope, отдельная доменная queue, Flash63K+pageFC00 | HC main/queues; Thermo linker/config | Сохранить платные размеры и layout16B/CRC; CRC-комментарий CCITT неверен: алгоритм reflectedA001, не менять сохранённый формат без миграции |
| U19 | PARTIAL: Debug/Release source App и include настроены; Release в этом аудите не собирался | HC/Fluidics .cproject/.ioc/linker | USER CODE для ручных правок, toolchain/ELF hash; Release отдельно NOT RUN, не объявлять готовым по Debug |
| U20 | PARTIAL: F020/F021 общий envelope, board blocksD501/D502; checked DATA/DONE | Global engineering contract + Studio common services/uart profiles | Сохранить типизацию/schema/index и общую процедуру; пересогласовать смысл D502 после byte-DMA, не менять семантику без host обновления |
| U21 | FAIL/PARTIAL: handshake bool скрывает этап отказа; per-byte timeouts без общего deadline; Studio +6s лишь оценка | Общая обязанность bounded transaction; SSI как доменный источник | Типизированный локальный результат и общий deadline; действующие NACK применить по согласованному mapping, новые коды не придумывать |
| U22 | FAIL: task_scan выходит на первом RX, ACK становится E601; holding_buf shared; HT rearm даёт BUSY; fragment assembly отсутствует | Общая граница domain/driver; SSI специфика Scanner | Согласованный stream/RX ownership, ACK отдельно от barcode, NAK отдельный исход, общий scan deadline; raw/packet mode согласовать |
| U23 | FAIL: 0601 в старом протоколе STOP/START-action, фактически timeout16; Studio ASCII<=64 vs firmware shadow128 | Global/низкоуровневый контракт и текущий Scanner | Сначала закрепить действующий контракт, длины/packing; не менять wire по устаревшему тексту.0603/0604 уже добавлены в global |
| U24 | PARTIAL: Studio общий service/transport/profile путь; ложное «без DONE» при0603/0604 | Studio ARCHITECTURE.md, common Services/ObservedDomainReply | Устранить рассогласование observers в рамках общего lifecycle, не отдельным флагом кнопки; tests с monitoring on/off |

## 4. Что уже верно и сохраняется

- [x] CAN profile: HSI8/2*PLL16=64MHz, PCLK1=32MHz, Prescaler2,
  1+11+4=16TQ ->1Mbit/s; AutoRetransmission и TXFIFO priority ENABLE.
  Это расчёт по настройкам, не измерение точности генератора.
- [x] HAL ConfigFilter/Start/ActivateNotification проверяют return.
- [x] ISR -> raw queue -> Handler -> ParsedCanCommand_t -> Dispatcher -> domain queue.
- [x] ISR не формирует ACK/DONE. Dispatcher даёт ACK до выполнения операции.
- [x] Общие NACK0001..0006, keys55AA/DEAD, metric0001..0012 совпадают с global.
- [x] Все четыре TX helper обнуляют frame, используют текущий RAM NodeID.
- [x] F001 содержит DeviceType05,1channel,UID96; sequence00/01/82.
- [x] Конфигурация выделена в0x0800FC00, linker исключает её из63KiB кода.
- [x] GPIO пользователя: CTS PA10, RTS PA9, PWRDWN PA8, TRIG PB12,
  AIM PB13, DLED PB14, BPR PB15; выходыCTS/TRIG/AIM стартуют HIGH.
- [x] SSI и управление этими GPIO остаются доменом Scanner, не общим CAN-слоем.

## 5. Последовательность согласованных блоков (выполнение ещё не начато)

- [ ] A. Зафиксировать контракт и U01/U02/U06/U07: CAN приём, фильтры,
  IRQ-safe snapshot и корректный heartbeat. Согласовать блок до кода.
- [ ] B. U03/U04/U05/U08/U09/U11: IRQ glue, ответы, F004/F005/F007;
  direct filter и изменение адреса внедрять одним связным изменением.
- [x] C. U12/U13 CODE/HOST/BUILD: config/mutex/Flash с сохранением Scanner layout/CRC.
  BENCH commit/reset/reload остаётся открытым.
- [ ] D. U10/U14/U16: PrepareReset, gates, cancel, early/fatal safe-state.
  Согласовать физическое ограничение: TRIG HIGH не гарантирует остановку
  сканирования, запущенного SSI. Нельзя объявлять гарантированное гашение лазера
  без подтверждённого поведения модуля/аппаратного управления питанием.
- [ ] E. U15/U17/U18/U19: watchdog supervisor, создание RTOS-объектов,
  stack/heap/приоритеты и выбранная сборка.
- [ ] F. U20–U24: согласовать и исправить Scanner handshake/deadline,
  диагностику, ACK/RX stream, wire contract, host lifecycle.
- [ ] После каждого: пользователь вносит выданные блоки, аудит diff,
  необходимые HOST/BUILD, запись результата. Не смешивать CODE/HOST/BENCH.

## 6. Конечный профиль проверки до зачёта

Это предлагаемый конечный объём, согласовать перед стендом; повторять только
затронутые случаи при новых изменениях/сбоях. Автоматические CAN/flash не разрешены.

- [ ] HOST transport: по одному случаю IDE/RTR/DLC/type/src/dst, direct/broadcast,
  filter model; PRIMASK0/1, timeout vs RTOS error. Failure injection очередей.
- [ ] HOST config: magic/CRC/Node range; defaults без erase; NULL mutex;
  Unlock/Erase/Program/Lock failures; bool F003/F006 и RAM-ID до reset.
- [ ] HOST safety: activation vs latch, queued/in-flight cancel и late callback,
  invalid keys, IRQ0/1; HAL abort result и владение DMA buffer после отказа.
- [ ] HOST domain: RTS stuck high/low/delayed, timeout между байтами, общий
  deadline, отсутствие ложного complete, ACK->DATA, split/combined RX,
  HT/IDLE/TC, NAK, raw/packet mode по принятому контракту.
- [ ] Studio: target node/type, missing/duplicate/reordered DATA и EOT,
  partial/empty capture, monitor on/off, CANCEL/reset lifecycle по контракту.
- [ ] BUILD: clean Debug, hooks disabled, source/toolchain/ELF hashes;
  Release отдельно по согласованному объёму, иначе явно DEFERRED.
- [ ] CAN bench: три цикла60->6F->60, old ACK/new DONE, direct+F001 broadcast;
  invalid F005 5F/70/10/00 не изменяет RAM/filter; invalid transport без ответа.
- [ ] CAN bench: F001/F004/F007 полные,18metrics, одинаковый UID; F020/F021 D501/D502;
  контролируемое давление на queue с записью потерь/metrics, без ложного DONE.
- [ ] Flash bench: F003/reboot/readback; F006 correct/incorrect key;
  отдельный Flash readback либо пометка NOT RUN, не подменять чтением RAM.
- [ ] GPIO/SSI bench: CTS/RTS/AIM/TRIG до startup, при reset и fault;
  валидный ответ версии и штрихкод; минимум три повторных запроса после timeout.
- [ ] Watchdog: штатный idle5min, по одному контролируемому зависанию каждого
  из3 клиентов, приоритет supervisor/fatal/IWDG recovery. Реальный fault и
  вызов handler функцией отмечать отдельно. GDB restore breakpoints/DBGMCU.
- [ ] Зачесть старые результаты только при совпадении исходников/условий;
  оформить PASS/FAIL/PARTIAL/DEFERRED отдельно, затем запросить конец сессии
  для обновления NEXT_SESSION. Коммит/прошивка — отдельное указание пользователя.

## 7. Границы выполненного аудита

Это статическая сверка кода с названными функциями эталонов и нормативными
контрактами. В ходе этого аудита код не редактировался, сборка и стенд не
запускались. Прежние583 Studio PASS и build43444/68/15120 исторические,
не доказательство устранения U01–U24. Безусловное «унифицировано» недопустимо.
Тонкие гонки HAL/RTOS и электрические уровни требуют обозначенных HOST/BENCH.


### Итог сессии30.10.2026

D: U10/U14/U16 и E: U15/U17 имеют перечисленные выше CODE/HOST/BUILD,
BENCH остаётся открытым. U18 — только статическая оценка. U21 сохраняет
согласованные локальные TX bounds без дополнительного общего TX deadline.
U22 закрыт частично: сборка из полученных задачей частей готова, сохранение
самих RX-событий до чтения — следующий шаг. U23/прочие открытые пункты и
Studio observer этим не закрываются. На аппаратуре сегодня проверок не было;
разделение пакета UART IDLE — возможность реализации, не наблюдённый факт.
Сначала продолжать оставшуюся часть U22 по текущему коду и правилам консультации.

## U22 — согласованное изменение RX02.11.2026

### Решение и границы

- [x] AGREED: убрать DMA только из приёма USART3; принимать байты по
  UART-прерыванию. USART3 остаётся9600 8N1, PB10/PB11.
- [x] AGREED: драйвер сохраняет поступающие байты в продолжение непрочитанных;
  задача забирает их и освобождает занятое место. Безусловную замену
  holding_buf/holding_len последним RX-событием устранить.
- [x] AGREED: задача уведомляется о доступных данных, а не только о полном
  SSI-пакете. Сборка Length+2 и проверка SSI остаются в задаче/домене.
  Уведомление не является счётчиком байтов или подтверждением готового пакета.
- [x] AGREED: TX byte-DMA, RTS/CTS, wake и согласованные локальные TX bounds
  сохраняются. Удаление RX DMA не означает удаления TX DMA или всех DMA IRQ.
- [ ] OPEN: конкретный RX API, организация и ёмкость буфера, способ короткой
  синхронизации ISR/task, поведение при нехватке места и ошибках UART.
  Mutex с ожиданием в ISR не использовать. Решение о переходе не утверждает
  автоматически прежнюю выдачу out_overflow или конкретный кольцевой буфер.

Это решение заменяет прежнее намерение сохранить RX DMA128. Кольцевой DMA,
HT/TC/IDLE как основа нового RX и увеличение DMA не являются выбранным
направлением. Старые разделы сохранены как история; фактический firmware
по-прежнему использует DMA_NORMAL и ещё не изменён.

### Порядок реализации

- [ ] Подготовить связный блок: scan_uart_driver.c/.h, все потребители RX
  в task_scan_controller.c, USART3 IRQ/HAL callbacks, конфигурация RX в
  stm32f1xx_hal_msp.c и .ioc. Согласовать точные изменения Cube-конфигурации;
  сохранить USER CODE, пользовательские GPIO и работающий TX DMA.
- [ ] Обеспечить сохранение порядка и однократную выдачу байтов, атомарное
  получение/освобождение данных и отсутствие потери уведомления при гонке
  чтения с поступлением нового байта. Границу нового запроса и старые RX-данные
  определить явно; общий срок0601 не перезапускать.
- [ ] Адаптировать D501 RX diagnostics и capture0603/0604 к побайтовому приёму.
  Уточнить смысл sequence/length/event metadata, зависящих сейчас от HAL
  RX-события; не менять их молча и не выдавать RXNE за DMA IDLE/HT/TC.
- [ ] Сохранить reset-latch, запрет нового приёма после latch, остановку RX,
  поздние callbacks и terminal gates; проверяемый отказ остановки сохранить.
  Исходящий SSI ACK перед CAN DATA/DONE и существующие коды результата
  не менять без отдельного согласования.
- [ ] Выдать точные блоки пользователю; после внесения проверить фактические
  файлы и diff. Этот раздел не разрешает ассистенту самостоятельно менять
  firmware, .ioc, выполнять коммит, прошивку или аппаратные команды.

### Проверки и критерий завершения

- [ ] CODE: приёмные вызовы/IRQ/config согласованы, RX DMA больше не используется;
  TX DMA сохранён, владельцы общего состояния и все исходы API определены.
- [ ] HOST: несколько поступлений до чтения задачей, чередование чтения и ISR,
  порядок/однократность байтов, повторное использование освобождённого места,
  границы ёмкости и согласованный исход ошибки. Неполный и последовательные
  пакеты, ACK+DECODE_DATA, checksum, исходный timeout, reset/late callback,
  диагностика и capture проверить на фактическом новом коде.
- [ ] BUILD: Debug без ошибок/предупреждений; пересчитать память и сохранить
  доказательства. Release отдельно, прежний результат на него не распространяется.
- [ ] BENCH, после отдельного разрешения: при9600 8N1 байт занимает примерно
  1.042ms (10бит); подтвердить обслуживание UART до аппаратного overrun с
  учётом IRQ latency/запрета IRQ и освобождение программного буфера при
  фактических блокирующих TX-ожиданиях и нагрузке задач. Заранее зафиксировать
  объём/длительность теста и критерий отсутствия пропусков/повторов.
  HOST и BUILD не доказывают отсутствие потерь на MCU.

На02.11: AGREED — только перечисленный механизм и объём. Новый CODE отсутствует;
HOST/BUILD/BENCH нового RX — NOT RUN. Полная унификация и физическое чтение
штрихкода остаются открытыми. NEXT_SESSION и checkpoints30.10 исторические;
для нового направления U22 приоритет имеет этот раздел.

### Уточнение реализации U22 — согласовано02.11.2026

Пользователь подтвердил конкретизации подготовленного кандидата:

- [x] AGREED: линейный накопитель256B и отдельный task snapshot256B;
  задача атомарно забирает все непрочитанные байты и освобождает накопитель.
- [x] AGREED: ошибка UART или невозможность сохранить данные делает текущий
  запрос0601 ошибочным: существующий E602 без DATA/DONE повреждённого результата.
- [x] AGREED: D501 сохраняет формат schema1, но при побайтовом HAL IT содержит
  последний принятый байт; capture0603/0604 сохраняет сырую последовательность.

Это уточняет три соответствующие части OPEN выше. Прочие детали кандидата
подлежат сверке при внесении; автоматического зачёта всего U22 нет.
Рабочий firmware/config не изменён. Кандидат из8 файлов подготовлен отдельно
в /tmp/scanner_rx_irq_proposal/: patch, полные файлы и REVIEW.txt.
HOST кандидата PASS, включая9920 разделений ACK+DATA для payload1..128.
ARM -fsyntax-only пяти C-файлов PASS; это не Debug build/линковка.
git apply --check PASS на57740f3. Внесённый CODE, полный BUILD и BENCH ещё OPEN.

### Итоговое решение U22 — RX128B и приём полного цикла,02.11.2026

Этот раздел имеет приоритет над прежними размерами и границами накопления.
Пользователь подтвердил следующий вариант для дальнейшей ручной реализации:

- [x] AGREED: RX USART3 по UART-прерыванию; TX byte-DMA/RTS/CTS/wake
  и согласованные локальные TX bounds сохраняются.
- [x] AGREED: накопитель непрочитанных RX-байтов128B. ISR добавляет байты,
  задача атомарно забирает накопленное и освобождает место. Задача обрабатывает
  данные по мере поступления; ожидать заполнения буфера или окончания всего
  цикла перед разбором не нужно. Это не два слота сканов и не постоянный журнал.
- [x] AGREED: сборка SSI по Length+2, checksum и последовательный разбор
  ACK/DECODE_DATA/NAK остаются в задаче/домене. Уведомление — о доступных
  данных, не счётчик байтов и не признак готовности всего цикла.
- [x] AGREED: принимать и обрабатывать ответ STOP_DECODE; закрытие RX до
  отправки STOP, как в прежнем кандидате, не соответствует этому решению.
- [x] Сохраняются согласованные E602 при ошибке UART/невозможности сохранить
  данные без выдачи повреждённого результата, приоритет reset -> DEVICE_BUSY,
  D501 schema1 с последним принятым байтом и независимый capture0603/0604.

Основание размера — реальный образец artifacts/reagent.jpg, ASAT2.
Визуально читаемая подпись:90222260300810701,17цифр. Это чтение подписи
на фотографии, не декодирование штрихового изображения и не UART-измерение.
При передаче этой строки ASCII без prefix/suffix получаем:

| Принятые данные | Байты |
|---|---:|
| ACK на START_DECODE | 6 |
| DECODE_DATA: header4 + Type1 + строка17 + checksum2 | 24 |
| ACK на STOP_DECODE | 6 |
| Штатный цикл RX | 36 |
| Тот же цикл с двумя дополнительными передачами DECODE_DATA | 84 |

SCAN_ENABLE выполняется отдельно при запуске; его ACK добавляет6B, если
рассматривать обмен вместе со стартовой настройкой. При включённом ACK/NAK
сканер может повторить DECODE_DATA ещё два раза при отсутствии подтверждения.
Это основание запаса128B для данного сценария, не новая согласованная
реализация retry и не доказанный максимум для любых этикеток/настроек.
NAK занимает7B вместо6B ACK и содержит причину отказа.

Источник домена: Zebra SSI Programmer's Guide72E-40451-03 RevA,
§1-7, таблица1-1, CMD_ACK/CMD_NAK:
https://www.bsr.at/mediafiles/Handbuch/Zebra/DS3600_Simple-Serial-Interface-Programmer-Guide.pdf
Расширение до максимума протокола (тело251B/пакет257B/накопитель263B)
не принято для текущего блока. Старый SSI_MAX_PAYLOAD128 появился в первой
реализации3e82ec7 без найденного обоснования; его совпадение с выбранной
ёмкостью RX128B не делает эти два ограничения одним требованием.

- [ ] OPEN: подтвердить фактический SSI-ответ, prefix/suffix и допустимые
  длины остальных используемых этикеток. Одно фото не задаёт их максимум.
- [ ] OPEN: определить ожидание/исход отсутствия ACK либо NAK на STOP.
  Сейчас CAN terminal-результат отправляется до STOP; согласованный порядок
  SSI ACK -> CAN DATA/DONE автоматически не меняется, второй terminal-ответ
  по той же команде не добавлять. Сроки и конкретное поведение согласовать.
- [ ] OPEN: конкретная обработка повторного DECODE_DATA и причин NAK,
  включая исключение повторной выдачи одного результата. Расчёт ёмкости
  с повторами сам по себе не закрывает прежний OPEN SSI NAK/resend.
- [ ] Подготовить заново связные кодовые блоки драйвера/API/задачи и Cube
  в формате «файл -> точное место -> действие -> полный код -> обоснование».
  Патч и команда git apply не являются выбранным способом выдачи.
- [ ] После внесения проверить новый CODE/HOST/Debug BUILD; добавить сценарии
  STOP ACK/NAK/отсутствия ответа, границы128B, реальные17цифр и повторы по
  согласованной обработке. BENCH — отдельно после разрешения, с проверкой
  IRQ latency, запрета IRQ и блокирующих ожиданий TX/нагрузки задачи.

Прежний кандидат /tmp/scanner_rx_irq_proposal использует256B и закрывает
накопление до исходящего ACK/STOP; он снят с готовности к внесению.
Его HOST/ARM syntax результаты относятся только к прежнему кандидату,
не подтверждают новый вариант128B/полный цикл. Новая реализация отсутствует;
HOST/BUILD/BENCH актуального варианта NOT RUN. Firmware/config не изменены.

### UART RX/TX без DMA — фактическое состояние02.11.2026

Этот раздел уточняет предыдущие записи02.11. Указание сохранять TX DMA
было пересмотрено с пользователем: принято убрать DMA у USART3 полностью.
Это не отменяет согласованный RTS/CTS, wake, локальные bounds и reset-gates.

- [x] AGREED/CODE: RX через HAL_UART_Receive_IT(...,1), TX через
  HAL_UART_Transmit_IT(...,1). RTS проверяется перед каждым передаваемым
  байтом; существующий TxCpltCallback сохраняет START diagnostics и отпускает
  CTS после последнего UART TC. HAL_UART_AbortTransmit поддерживает IT;
  владение буфером и существующий terminal путь отказа abort сохраняются.
- [x] CODE: накопитель128B и task snapshot128B; ISR добавляет непрочитанные
  байты, GetReceived(out_len,out_error) атомарно копирует и освобождает их.
  Ошибка текущего приёма сохраняется до закрытия/нового обмена.
- [x] CODE частично: задача вызывает BeginRead после успешного StartScan,
  перед wake/START. При отказе START вызывает EndRead; ошибочный EndRead
  в ScanQueryRevision удалён. Эти две коррекции ассистент внёс по отдельному
  поручению «исправь сам». Новый вызов GetReceived и чтение до ожидания
  внесены пользователем; общий срок, watchdog heartbeat и reset сохранены.
- [x] CODE: пользователь удалил оба DMA-запроса через Cube и сгенерировал код.
  Нет UART DMA handles/link/init/deinit, DMA IRQ Channel2/3 и MX_DMA_Init.
  USART3 остаётся PB10/PB11,9600 8N1, flow None, IRQ priority6/subpriority0.
  USER CODE в main/MSP сохранён; удалённые DMA USER CODE контейнеры были пусты.
  Изменения форматирования main.c не выданы за функциональные изменения.
- [x] CODE: D501 остаётся schema1/40 records, последнее HAL IT completion
  содержит1байт, eventTC; capture0603/0604 остаётся независимым сырым захватом.
  Ошибки UART сохраняются до повторного arm; при reset поздний RX не
  продолжает доменную операцию и не запускает новый приём.
- [x] Комментарии: после перехода RX/TX на IT ассистент по поручению пользователя
  обновил комментарии driver/header/task/app_safety. Сравнение C-токенов до/после
  подтвердило отсутствие изменений исполняемого кода; отдельная сборка
  ради комментариев не запускалась.
- [x] BUILD, результат пользователя: после замены TX на IT сообщена чистая
  Debug-сборка. Независимая полная сборка ассистента именно после удаления
  обоих DMA не выполнялась. Предыдущая сборка ассистента47500/68/15264
  относится к стадии до удаления DMA и не является доказательством этого шага.
- [x] HOST, ограниченный объём: модель драйвера RX128B/IT TX проверила
  накопление/освобождение, границы ёмкости, UART errors/re-arm, diagnostics,
  capture, reset/abort и поздний TX callback.405 разделений ACK+DATA при
  payload1..18; это тестовый диапазон вокруг17-байтовой строки с Type,
  не измеренный UART-ответ. Текущий driver/header совпали с проверенным
  кандидатом по C-токенам; полная задача с новым STOP lifecycle не проверялась.
  Отдельная ARM-проверка синтаксиса кандидата драйвера PASS, не полный BUILD.

Источники нового TX: локальный HAL Scanner57740f3, HAL_UART_Transmit_IT,
UART_Transmit_IT, UART_EndTransmit_IT и HAL_UART_AbortTransmit вместе с
USART3_IRQHandler -> HAL_UART_IRQHandler. Локальные HAL-исходники не менялись.
Адаптация: запуск передачи через IT вместо DMA; структура handshake,
порядок защиты reset/запуска/ожидания callback и диагностика сохраняются.
Аппаратных каналов DMA у USART3 больше нет. Наличие HAL DMA в библиотеке,
её типах или HAL конфигурации не означает использование DMA USART3.

Остаток U22 — не считать завершённым:

- [ ] AGREED, но CODE OPEN: приём и обработка ответа STOP_DECODE.
  Фактически CAN terminal всё ещё отправляется до STOP, ожидания STOP ACK/NAK
  нет, результат ScanUart_Send для STOP не обрабатывается.
- [ ] OPEN: нормальное EndRead после завершения полного цикла; сейчас
  EndRead вызывается при отказе START, а CancelRx закрывает обмен при reset.
  Одной чистой сборки недостаточно для зачёта владения RX на всех выходах.
- [ ] До следующего блока согласовать срок/исход STOP ACK/NAK/timeout и
  взаимный порядок STOP и CAN terminal; второй terminal автоматически не вводить.
- [ ] Проверить итоговую ошибку RX на всех terminal/deadline путях, обработку
  причин NAK и повторов без дублирования результата; прежние OPEN сохраняются.
- [ ] После связного завершения — HOST фактической задачи и Debug BUILD
  с фиксацией исходников/памяти. Release отдельно; BENCH NOT RUN.

Реальный SSI-ответ/штрихкод, prefix/suffix, остальные этикетки, IRQ latency,
реальные UART errors и runtime stack/heap остаются неподтверждёнными.
NEXT_SESSION и checkpoints30.10 не обновлялись; текущий источник — этот план.
Разовые разрешения на две коррекции и комментарии не расширяются на новый код.

### Остановка сканера перед reset и Flash — уточнение02.11.2026

Пользователь поручил зафиксировать выводы проверки конкретных сервисных
путей. Этот раздел уточняет прежние записи об обязательном STOP в каждом
цикле; согласование направления не является внесённым кодом.

#### Назначение STOP и автоматическое завершение

STOP_SESSION/STOP_DECODE E5 отменяет незавершённую попытку сканирования.
После успешного завершения Decode Mode дополнительный STOP для завершения
этой попытки не нужен. Не вводить обязательный STOP ACK как условие выдачи
успешного штрихкода и не переносить CAN terminal ради такого условия.
Прежнее предложение отдельного2s ожидания STOP после каждого запроса снято.
Фактически безусловная отправка STOP в конце0601 пока остаётся в коде;
её приведение к согласованному назначению — будущий связный блок.

SE965HP Decode Session Timeout88h по умолчанию3.0s, диапазон0.5..9.9s.
В Level/Pulse лазер выключается при успешном чтении либо по таймауту;
Continuous имеет другое поведение. Поэтому наш timeout0601 сам по себе
не доказывает, что лазер ещё активен. Фактические настройки данного экземпляра
не читались; заводские значения не выдавать за измеренный режим.
Источник: Motorola SE965HP72E-151566-01 RevA §7-12, §7-14:
https://www.scribd.com/document/586071361/51785c-f82347f83adb43908f2c8d95182b60e1-2
SSI STOP/завершение Decode Mode:72E-40451-03 RevA table2-88, STOP_SESSION:
https://www.bsr.at/mediafiles/Handbuch/Zebra/DS3600_Simple-Serial-Interface-Programmer-Guide.pdf

#### Проверенные пути проекта и выбранное направление

| Ситуация | Вывод и направление доработки |
|---|---|
| F002, плановый reboot/reset STM32 | При незавершённой операции нужно управляемо прекратить обмен и активную попытку сканера перед отключением UART/перезагрузкой. Предусмотреть ограниченную попытку STOP; reset STM32 не является подтверждением reset/остановки SE965HP. |
| F006, factory reset | Такая же доменная подготовка должна предшествовать стиранию Flash и reboot. Отмена UART на МК не заменяет остановку внешнего модуля. |
| F003, Flash Commit | Нужна исключённая конкуренция с UART. Согласованное направление: BUSY при сканировании/capture/активном обмене, вместо принудительного обрыва операции ради записи. Проверку и начало Flash связать с запретом конкурентного старта, включая queued/in-flight команды. |
| Аварийный watchdog/fault reset | STOP/ACK с ожиданием в fatal-путь не добавлять. Сохранить IRQ off -> аппаратное safe-state -> terminal/IWDG; работоспособность UART/RTOS в аварии не предполагается. |
| Успешное чтение | SSI ACK, проверка итоговой ошибки RX/закрытие накопления, единственный CAN terminal. Дополнительный обязательный STOP для успешно завершившейся попытки не вводить. |

Плановый reboot и программный reset здесь оканчиваются одним
NVIC_SystemReset. Отличать их от аварийного пути, где штатную процедуру
обмена SSI выполнять нельзя. Гарантированное физическое гашение лазера
при аппаратном reset/fault кодом SSI не доказано.

#### Фактические пробелы CODE

- F002/F006 вызывают Dispatcher_PrepareReset. Он сначала устанавливает
  AppSafety_PrepareReset latch, затем ждёт ScanTask_IsResetPrepared.
  ScanUart_Wake/Send после latch возвращают отказ; Scan_SyncResetState
  вызывает CancelRx/отменяет доменное состояние, но STOP сканеру не отправляет.
  Подтверждение prepared сейчас относится к состоянию МК/UART, не к
  подтверждённой остановке SE965HP. Прежние HOST этой подготовки не доказывают
  физическую остановку лазера.
- Общий бюджет Dispatcher_PrepareReset —1s. Нельзя просто вставить туда
  предложенное2s ожидание ответа STOP или вызвать обычный Send после latch.
- F003 непосредственно вызывает AppConfig_Commit. Config mutex защищает
  конфигурацию, но не согласует Flash с задачей Scanner и UART.
- Код и обработчики IRQ исполняются из Flash. По ST PM0075 §2 во время
  program/erase выборка из Flash останавливается до завершения операции;
  это затрагивает обслуживание UART-прерываний. Запись во Flash при активном
  RX может привести к overrun/потере данных; размер RX-накопителя этого
  аппаратного перерыва обслуживания не устраняет.
  Источник: https://www.st.com.cn/resource/en/programming_manual/pm0075-stm32f10xxx-flash-memory-microcontrollers-stmicroelectronics.pdf

#### Следующие блоки и границы приёмки

- [ ] U22: закончить EndRead и проверку ошибки RX на нормальном/ошибочном
  завершении, без обязательного STOP после успешного чтения.
- [ ] U10/U14: согласовать порядок запрета новых операций, управляемой
  остановки активного сканера и окончательного отключения UART перед F002/F006.
  Определить отдельный допустимый путь выключения, общий срок подготовки,
  исход STOP ACK/NAK/timeout и отказа связи. Не ослаблять общий reset-gate
  произвольным разрешением новых UART-команд после latch.
- [ ] Flash/F003: подготовить согласование с доменом и BUSY без гонки между
  проверкой занятости и началом Flash. Сканирование, probes/capture и pending
  UART-ответы учитывать как обмен, а не проверять только g_state==SCANNING.
- [ ] Сохранить единственный terminal каждой CAN-команды и приоритет reset;
  исход отменённой0601 и сервисный результат F002/F006 различать.
- [ ] HOST: idle/queued/in-flight reset, error/timeout STOP по выбранной
  политике, отказ UART, Flash BUSY/гонки старта; отдельно проверить отсутствие
  RTOS/UART waits в fatal-пути. BUILD после связного внесения, BENCH отдельно.

Статус: проверка исходников и доменных/Flash источников выполнена;
направление доработок зафиксировано по поручению пользователя. Механизм
shutdown/Flash arbitration, коды всех исходов и таймауты ещё не выданы как
готовые блоки. Новый CODE отсутствует, HOST/BUILD/BENCH этих доработок NOT RUN.
Firmware/config и NEXT_SESSION этой записью не менялись.

### U22 — закрытие RX на завершении запроса02.11.2026

По конкретному поручению пользователя ассистент изменил только согласованный
блок task_scan_controller.c:

- [x] CODE: после исходящего SSI ACK, перед SyncResetState/terminal gate
  вызывается EndRead; отрицательный итог устанавливает wait_error и приводит
  к существующему E602 без DATA/DONE повреждённого результата. Ошибка,
  пришедшая во время ACK или на границе deadline, не теряется при закрытии.
- [x] CODE: существующий STOP после неуспеха сохранён; после успешного
  ProcessReceived, устанавливающего DATA_READY, дополнительный STOP не идёт.
  Нового ожидания STOP ACK и изменения CAN terminal order нет.
- [x] HOST фактического кода:13 сценариев — полный/побайтовый приём и
  ACK+DATA17цифр, checksum, UART error, нехватка места, timeout/partial,
  отказ отправки SSI ACK, reset в ожидании/во время ACK, UART error во время
  ACK и на deadline. Проверены закрытие RX/пустой накопитель, один terminal,
  отсутствие STOP после успеха и сохранение прежней попытки STOP после ошибки.
- [x] BUILD ассистента: Debug incremental make -j4 all с перекомпиляцией
  затронутых App sources и линковкой;0errors/0warnings,
  text45740/data68/bss15128. SHA256 ELF и build.log сохранены.

Артефакты: [rx_finish_20261102](test_artifacts/rx_finish_20261102/) —
host.c/extracted.inc/run_host.py/include, результат, source hashes,
build.log/build_evidence.json. HAL/RTOS модель, не реальный UART/лазер.
BUILD относится к текущему дереву с UART RX/TX IT и сгенерированным Cube
без UART DMA. Release/BENCH NOT RUN.

Прежние OPEN о нормальном EndRead уточнены этим результатом. U22 целиком
не закрыт: причины NAK/повторы и аппаратные условия остаются открытыми.
Управляемый STOP перед F002/F006, budget/gate и Flash arbitration F003
этой правкой не реализованы. Следующий блок — согласовать их конкретный
механизм по предыдущему разделу. Разрешение на эту правку не расширяется
на другие firmware/config изменения. NEXT_SESSION и коммит не выполнялись.

### Конец02.11.2026 — согласован следующий блок F002/F006

Пользователь подтвердил следующий порядок управляемой подготовки reset:

- [x] AGREED: AppSafety_PrepareReset сохраняет немедленный latch и аппаратный
  safe-state по общей инструкции/механизму HC. Обычные команды, START,
  аппаратный trigger/AIM и обычный Wake/Send после latch не разрешать.
- [x] AGREED: задаче Scanner нужен отдельный ограниченный путь выключения,
  допускающий только SSI STOP после latch. Это доменное дополнение Scanner,
  не перенос готовой функции HC и не общий обход reset-gates.
- [x] AGREED: завершить/разобрать прерванный SSI-обмен; старый ACK не принимать
  за подтверждение STOP. Предыдущий результат не должен стать успешным после
  решения об отмене. RX, нужный для подготовки, согласовать с существующими
  post-latch gates; ISR сейчас прекращает обычный re-arm после latch.
- [x] AGREED: для STOP использовать аппаратный handshake CTS/RTS, без
  активации AIM. По SE965HP table1-2 CTS LOW также будит модуль; это основание
  выбранного пути, не подтверждение реального подключения или ответа стенда.
- [x] AGREED: только после подтверждённой остановки закрыть UART RX и
  опубликовать prepared. Тогда F002 продолжает service reply/reboot,
  F006 — Flash/service reply/reboot. Для уже завершённой операции отдельно
  определить условие отсутствия необходимости STOP; не судить о модуле
  только по ошибочному/сброшенному состоянию контроллера МК.
- [x] AGREED: общий бюджет подготовки3s вместо действующего1s. Включает
  ожидание задачи, завершение предыдущего обмена, STOP и подтверждение.
  Это единый предел; отдельные локальные ожидания не добавляют к нему время.
- [x] AGREED: при ошибке остановки/исчерпании бюджета prepared не публиковать,
  Flash F006 не начинать; сохраняется существующий terminal путь отказа
  подготовки. Такой fallback не доказывает физическую остановку лазера.
- [x] Сохраняются аварийный путь без RTOS/UART waits, единственный terminal
  каждой CAN-команды и аппаратная граница владения UART задачей Scanner.

- [ ] CODE следующего блока отсутствует: Dispatcher по-прежнему ждёт1s,
  обычные UART API отвергают вызовы после latch, SyncResetState отменяет
  RX без SSI STOP. Не менять только1s на3s, выдавая это за готовую подготовку.
- [ ] Подготовить точные малые связанные блоки, включая обработку старых
  ответов и частичного TX, условия STOP, единый deadline и допустимый RX
  после latch. До выдачи сверить нормативные/эталонные источники и все вызовы.
- [ ] HOST/BUILD/BENCH новой подготовки NOT RUN. Нужны idle/queued/in-flight,
  reset в TX/ACK/capture, старые ACK, отказ STOP/NAK/timeout, бюджет и запрет
  Flash до prepared; физические SSI/лазер и реальные времена отдельно.

Сессия завершена по прямой просьбе пользователя. Точка входа обновлена;
снимок документов и последних доказательств: readme/checkpoints/20261102/.
HEAD остаётся57740f3, сегодняшние runtime/config изменения не закоммичены.
Нового разрешения на коммит, прошивку или аппаратуру не было.
Следующим после подготовки reset остаётся F003 BUSY/согласование с UART;
это направление уже зафиксировано, конкретный механизм ещё OPEN.

### Фиксация состояния02.11.2026 в Git

После точки останова пользователь отдельно поручил коммит и сверку объёма
с предыдущим57740f3. В фиксацию включаются текущие UART RX/TX IT, накопитель128B,
закрытие RX/условный STOP, соответствующие Cube изменения и checkpoint20261102
с доказательствами. Shutdown3s и F003 arbitration остаются только планом.
Прежнее «коммит не выполнялся» описывает момент сохранения точки останова.
База —57740f3; итоговая версия — git log -1 --oneline в Scanner. Push и новые
действия с аппаратурой не поручались. Сверка runtime/checkpoint/ELF hashes PASS;
main.c по C-токенам отличается только согласованным удалением DMA.

### 03.11.2026 — подтверждение назначения STOP и порядка продолжения

- [x] AGREED: STOP используется для прекращения текущей попытки перед
  плановым F002/F006; назначение STOP уже согласовано, повторное обсуждение
  его необходимости не является следующим этапом работы.
- [x] AGREED: проверка ключа -> немедленный latch/safe GPIO и запрет новых
  сканирований -> задача Scanner выполняет STOP-only путь с проверкой
  подтверждения -> закрытие RX/очистка отменённого обмена -> prepared ->
  сервисный ответ/reset; для F006 Flash выполняется после prepared.
- [x] AGREED: единый бюджет3s; отказ подготовки не разрешает prepared/Flash
  или успешный результат отменённого сканирования. Аварийный путь без ожиданий.
- [x] AGREED: после reset новое сканирование начинается по новой команде
  Дирижёра; автоматическое возобновление старого запроса не вводится.
- [ ] Подготовить связный блок реализации с проверкой частичного TX,
  старого ACK и post-latch RX. Конкретный механизм учёта фаз пока не принят;
  обсуждение ассистента не считать утверждением новой машины состояний.
- [ ] CODE/HOST/BUILD/BENCH этой подготовки отсутствуют. Аппаратный reset
  SE965HP, переключение питания и сброс настроек в объём не добавлены.

Подробная запись решения — в SCANNER_EXECUTOR_REPORT.md от03.11.2026.
Следом остаётся отдельный блок F003/согласование Flash с UART. Firmware
вносит пользователь; согласование порядка не разрешает прямое редактирование.

### 03.11.2026 — кандидат управляемой подготовки для ручного внесения

- [x] PROPOSED: подготовлен связный [блок STOP/reset](code_proposals/reset_stop_20261103/MANUAL_IMPLEMENTATION.md),
  14 точных замен в7 App-файлах, с нормативной/эталонной сверкой и полным
  списком адаптаций. Исходники проекта ea1eacb не менялись.
- [x] HOST кандидата: shutdown18групп PASS; регрессия13 прежних сценариев
  задачи PASS. Ручные шаги побайтно воспроизводят проверенный кандидат.
- [x] ARM GCC13.3.1 syntax/type/include для4C-файлов PASS, Wall/Wextra/Werror.
  Это НЕ штатная сборка и не доказательство аппаратного SSI.
- [ ] Внесение пользователем -> аудит фактических файлов -> Debug BUILD.
- [ ] BENCH: новый handshake/приоритет ответа STOP, частичный START,
  подтверждённая остановка и общий срок. Физическая приёмка остаётся OPEN.
- [ ] Полная NAK/resend-политика и изоляция нового чтения после reboot от
  повторов старого DECODE_DATA остаются OPEN. STOP не объявляется flush/reset
  модуля. F003/Flash arbitration в данный кандидат не включён.

Установленный CODE нового shutdown отсутствует; U10/U14/U22 целиком не закрыты.

### 03.11.2026 — первый блок app_safety внесён отдельно

- [x] По прямому разрешению пользователя ассистент внёс только app_safety.h/.c:
  начало срока при первом latch, предел3000ms, проверка истечения без ожиданий.
  PRIMASK/latch/safe GPIO сохранены; повторный запрос не продлевает срок.
- [x] HOST фактического блока PASS; Debug incremental build PASS,
  0errors/0warnings, text45772/data68/bss15136. Артефакты:
  [reset_budget_20261103](test_artifacts/reset_budget_20261103/evidence.json).
- [ ] Подключить общий срок в Dispatcher/UART и внести STOP-only путь
  следующими связанными блоками после выдачи кода. Сейчас Dispatcher ещё1s,
  старое подтверждение prepared остаётся без проверенного SSI STOP.

Уточняет прежнее отсутствие установленного CODE: внесён только первый
вспомогательный блок, полный shutdown не реализован и не принят.
BENCH/Release NOT RUN; разрешение на правку остальных файлов не получено.

### 03.11.2026 — выделен следующий промежуточный RX-блок

- [x] PROPOSED: scan_uart_driver.c, RX после latch до окончательного CancelRx,
  с приватным re-arm и запретом обычного StartRx/старого результата.
- [x] Проверен именно промежуточный вариант: HOST RX boundary +13 прежних
  сценариев PASS, ARM syntax PASS. TX/STOP и Dispatcher ещё прежние.
- [ ] Выдача полных блоков в чат -> внесение -> аудит/Debug BUILD.

Артефакты: code_proposals/reset_rx_step_20261103/. Это не установленный CODE;
последнее разрешение относилось только к уже внесённому app_safety.

### 03.11.2026 — RX-блок установлен по отдельному разрешению

- [x] Пользователю выдан код в чат; по прямому разрешению внесён только
  RX-блок scan_uart_driver.c. Приватный re-arm после latch, обычный StartRx
  запрещён; окончательный rx_cancelled предотвращает поздний re-arm.
- [x] Фактический код совпал по C-токенам с проверенным кандидатом.
  HOST RX boundaries +13 прежних сценариев PASS.
- [x] Debug incremental build PASS:0errors/0warnings,
  text45996/data68/bss15136. Артефакты: test_artifacts/reset_rx_20261103/.
- [ ] Следующий блок: STOP-only TX и открытие накопления его ответа;
  затем ACK-проверка задачей и общий deadline в Dispatcher.

reset_stop_active пока false; полный shutdown не завершён. Стенд/Release
NOT RUN. Разрешение на этот RX-блок не распространяется на следующие правки.

### 03.11.2026 — следующий STOP TX-блок подготовлен к выдаче

- [x] PROPOSED: узкий STOP-only UART TX и граница накопления ответа в TC
  до CTS HIGH. Промежуточный вариант использует уже внесённый RX/app_safety;
  задача/Dispatcher пока прежние. ACK/ prepared новый TX API не подтверждает.
- [x] HOST8групп TX (6 позиций прерывания START), RX boundaries и13 прежних
  сценариев PASS; ARM syntax PASS. Артефакты: code_proposals/reset_tx_step_20261103/.
- [ ] Выдача в чат -> внесение -> аудит/Debug BUILD; затем задача/Dispatcher.

Фактический firmware этим шагом не менялся, нового разрешения на правку нет.

### 03.11.2026 — STOP TX установлен по отдельному разрешению

- [x] По прямому разрешению пользователя внесён выданный в чат блок
  scan_uart_driver.h/.c: STOP-only TX после latch, CTS/RTS без AIM,
  открытие накопления ответа до CTS HIGH и общий deadline.
- [x] Сверка C-токенов с проверенным кандидатом PASS. HOST фактических
  исходников:8групп TX/6 позиций прерывания START, RX boundaries,
  13 прежних сценариев PASS.
- [x] Debug incremental build PASS:0errors/0warnings,
  text46300/data68/bss15136. Артефакты: test_artifacts/reset_tx_20261103/.
- [ ] Следующий блок: задача вызывает STOP, проверяет ACK, закрывает RX,
  публикует prepared; Dispatcher использует общий срок вместо прежнего1s.

Задача/Dispatcher пока побайтно совпадают с HEAD. Полный shutdown ещё не
реализован; Стенд/Release NOT RUN. Разрешение ограничено STOP TX-блоком.

### 03.11.2026 — подготовка F002/F006 подключена

- [x] По прямому поручению «подключай» задача Scanner вызывает STOP,
  проверяет первый ACK, закрывает RX, отменяет состояние и публикует prepared.
  Ошибки/NAK/deadline prepared не разрешают; обычная отменённая команда не
  получает успех. Условия service handlers и Flash bool сохранены.
- [x] Dispatcher использует общий3s срок от первого latch вместо собственного1s.
  F006 Flash только после prepared. Аварийные handlers без ожиданий сохранены.
- [x] Сверка C-токенов всего установленного7-файлового блока с кандидатом PASS.
  HOST фактических исходников:18групп shutdown/6 позиций прерывания START
  и13 сценариев регрессии PASS.
- [x] Debug incremental build PASS:0errors/0warnings,
  text47044/data68/bss15136. Артефакты: test_artifacts/reset_connected_20261103/.
- [ ] BENCH STOP/ACK, реальная остановка и времена; Release NOT RUN.
- [ ] Полная NAK/resend-политика, изоляция нового чтения после reboot от
  старого DECODE_DATA и runtime stack/heap остаются OPEN.
- [ ] F003/Flash UART arbitration — следующий отдельный блок, не менялся.

Прежние записи «Dispatcher1s», «задача не вызывает STOP» и «shutdown не
реализован» описывают промежуточное состояние и уточняются этой записью.
CODE/HOST/Debug BUILD не заменяют BENCH и не закрывают U10/U14/U22 целиком.

### 05.11.2026 — согласован механизм F003/Flash UART arbitration

Пользователь поручил «фиксируй» после обсуждения механизма. Статус AGREED,
не CODE: исходники firmware этим решением не менялись.

- [x] AGREED: единый счётчик незавершённых доменных команд охватывает queued
  и in-flight, включая scan, revision/BEEP capture и GET_DATA. Dispatcher
  увеличивает счётчик до osMessageQueuePut; при отказе постановки отменяет
  увеличение, сохраняя существующие overflow metric и DEVICE_BUSY.
- [x] AGREED: задача Scanner освобождает учёт команды только после полной
  обработки, включая завершение capture/RX и завершающего TX. Одна точка
  освобождения в начале следующей итерации внешнего цикла охватывает все
  выходы через continue. Timeout/ошибка ожидания пустой очереди не освобождают
  чужую команду; внутренние циклы ожидания не завершают учёт операции.
- [x] AGREED: стартовые Init/Wake/SCAN_ENABLE учитываются как занятость
  с момента запуска MCU, до начала выполнения задачи Scanner включительно.
- [x] AGREED: F003 при ненулевом счётчике сразу возвращает DEVICE_BUSY;
  при нуле выполняет существующий AppConfig_Commit. Успех -> DONE,
  ошибка Flash -> NACK FLASH_WRITE. Reset-latch и отмена операции не вводятся.
- [x] AGREED: основание отсутствия гонки — Dispatcher является единственным
  producer scan_queue и единственным исполнителем F003. Между проверкой нуля
  и окончанием Commit он не принимает новые доменные команды в scan_queue;
  CAN transport продолжает использовать собственные очереди. Чтение/изменение
  общего счётчика выполняются короткими критическими секциями с сохранением
  состояния IRQ, без ожиданий внутри них. Нового таймаута не требуется.
- [x] Граница: UART RX остаётся включённым в idle. Сохранность произвольных
  запоздалых байтов во время Flash этим механизмом не гарантируется.
- [ ] Подготовить связные полные блоки для ручного внесения и проверить
  счётчик при enqueue failure, dequeue/preemption, всех terminal/continue,
  startup, capture, scan и исходах Flash. Проверить соответствие эталону
  и сохранить уже внесённый F002/F006 shutdown.
- [ ] Новый F003 CODE/HOST/BUILD/BENCH отсутствует. Согласование механизма
  и поручение зафиксировать его не разрешают прямые firmware/config-правки,
  коммит, прошивку или аппаратные команды.

Нормативная основа: DDS-240_ECOSYSTEM_STANDARD §3.1/§3.1.1, global config
F003=0xF003, DEVICE_BUSY=0x0003, FLASH_WRITE=0x0005; общая инструкция,
разделы Dispatcher/сервисы и Конфигурация/Flash. Эталон bool Commit/ответов:
HC6abc1d2, Dispatcher_HandleServiceCommand/AppConfig_Commit, и
Thermo c43368e, app_start_task_dispatcher. Их применимые файлы без рабочих
изменений на момент сверки. Учёт queued/in-flight/startup — согласованная
адаптация Scanner, отсутствующая в этих обработчиках HC/Thermo.

### 05.11.2026 — кандидат F003 подготовлен для ручного внесения

- [x] PROPOSED: [полные ручные блоки](code_proposals/flash_uart_20261105/MANUAL_IMPLEMENTATION.md),
  6 точных замен в3 файлах: task_scan_controller.h/.c и task_dispatcher.c.
  Startup token1, Reserve до enqueue/Release при отказе, Release в начале
  следующего внешнего оборота после полной обработки, BUSY gate F003.
- [x] HOST кандидата PASS: startup до задачи и отказ startup wake/TX,
  IRQ-save enabled/disabled,4 доменные команды/queued BUSY, enqueue failure,
  завершение consumer до возврата Put, dequeue gap, несколько владельцев,
 13 scan paths, capture/GET_DATA/unknown/ранние отказы, cancellation,
  queue timeout/error, Commit true/false и следующий enqueue.
- [x] Регрессия с извлечённым кандидатом:18групп shutdown и13 сценариев
  чтения PASS; fixture резервирует каждую доставляемую задаче команду.
- [x] ARM GCC13.3.1 syntax/type/include обоих C-файлов PASS,
  Wall/Wextra/Werror. Ручные замены побайтно воспроизводят кандидат;
  исходные3 файла проекта сохранили SHA256. Доказательства:
  [evidence.json](code_proposals/flash_uart_20261105/evidence.json).
- [ ] Выдача полных блоков в чат -> ручное внесение -> аудит фактических
  файлов -> Debug BUILD. Новый F003 CODE ещё не установлен.
- [ ] Полная Debug/Release BUILD и BENCH кандидата NOT RUN.

UART RX/idle late traffic и единственный Dispatcher producer/Commit owner
остаются явными границами. Новых отличий от согласованного механизма нет;
F002/F006, UART driver, Flash layout/config mutex/Commit не изменяются.

### 05.11.2026 — F003 внесён по прямому разрешению пользователя

- [x] CODE: после изучения выданных блоков пользователь разрешил внести
  компоненты самостоятельно. Внесены ровно6 согласованных замен в3 App-файлах:
  task_scan_controller.h/.c и task_dispatcher.c. Остальные App-файлы сохранены.
- [x] SHA256 всех3 фактических файлов совпали с проверенным кандидатом.
  Startup/queued/in-flight счётчик и BUSY gate установлены; F003 сохраняет
  bool Commit -> DONE/FLASH_WRITE. Отдельный F002/F006 shutdown сохранён.
- [x] HOST установленного кода: новые проверки F003 PASS,18групп shutdown
  и13 сценариев чтения PASS. В extracted.inc заново извлечены фактические
  App-функции; enqueue/F003 handlers извлечены из текущего Dispatcher.
- [x] Debug incremental make -j4 all PASS,0errors/0warnings,
  text47284/data72/bss15136. Перекомпилированы main.c и оба изменённых
  C-файла, ELF перелинкован; это не clean build. Доказательства и hashes:
  [flash_uart_20261105](test_artifacts/flash_uart_20261105/evidence.json).
- [ ] Release/BENCH NOT RUN; физическая приёмка Scanner остаётся открытой.

Уточняет предыдущий статус PROPOSED/не установлен: согласованный блок теперь
имеет CODE/HOST/Debug BUILD в указанных границах. Произвольный запоздалый UART
трафик во время Flash не защищён; runtime stack/heap не измерены. Сохраняется
условие единственного Dispatcher producer scan_queue/исполнителя F003.
Коммит, push, прошивка и аппаратные команды не выполнялись. Разрешение
относится к этим6 заменам и не расширяет режим будущих firmware-правок.

### 05.11.2026 — пользователь подтвердил сборку после внесения F003

- [x] BUILD, сообщение пользователя: «ок, проверил, сборка чистая».
  Это отдельное подтверждение пользователя после внесения согласованных
  компонентов; не приравнивается к принудительному clean rebuild.
- [ ] BENCH остаётся NOT RUN: сообщение не содержит результата прошивки,
  аппаратного F003 при scan/capture или проверки SSI/штрихкода.

Новых firmware-правок и повторных проверок по этому сообщению не выполнялось.

### 05.11.2026 — уточнение U21/U23 после обсуждения следующих работ

- [x] Предложение ассистента вводить типизированные причины отказа UART
  отозвано как недостаточно обоснованное. До выдачи предложения не были
  определены полное соответствие существующим NACK, потребитель подробной
  причины и необходимое поведение системы. Новый enum/API, новые NACK,
  поля диагностики и изменения Studio НЕ согласованы и НЕ реализованы.
- [x] Дополнительный общий TX deadline не выбран: сохраняется решение30.10
  о локальных пределах UART/handshake. Единый3s deadline подготовки F002/F006
  остаётся отдельным уже реализованным механизмом.
- [x] Проверено текущее использование0602: ручная команда Get Last Data
  в Studio, обработчик/декодер ответа и тесты; вызовов в App/Core проекта
  STM32H723_mother_board не найдено. По словам пользователя, вероятное
  назначение — ручная тестовая проверка платы; это не доказанное историческое
  требование.0602 не является обязательным вторым шагом после0601.
- [x] Повторное согласование семантики0601/0602 не состоялось. Обсуждение
  не разрешает менять wire-контракт, firmware, global config или Studio.
  Расхождения U23 сохраняются OPEN, а не объявляются исправленными.
- [ ] Следующий аудит U21: проверить фактические ветки отказов по действующим
  NACK, однозначность terminal-результата и возможность следующего запроса.
  До предложения исправления установить конкретный дефект, его последствия,
  нормативное основание и проверяемый результат. При отсутствии дефекта
  сообщить результат проверки без добавления кода ради детализации.

Проверенные текущие ветки, не новая классификация: Wake/START/query TX failure
-> E601; RX/RTOS wait/исходящий ACK failure -> E602; отсутствие данных за срок
-> E603; нет сохранённого штрихкода -> E600; занятость/отмена запросом reset
-> DEVICE_BUSY. Ошибка подготовки самого F002/F006 остаётся terminal fatal,
не подменяется NACK отменённой доменной команды. Отдельного поля этапа
отказа в действующем NACK нет, reserved bytes не занимать самовольно.
Эта сверка не доказывает корректность классификации всех возможных отказов.

Статусы F003 CODE/HOST/BUILD сохранены. U21 не закрыт и новый блок не принят;
новых firmware-правок, тестов, сборки и аппаратных действий после обсуждения
не выполнялось. NEXT_SESSION в этом промежуточном шаге не обновляется.

### 05.11.2026 — аудит U21: NACK, terminal и следующий запрос

По поручению «проверяй» выполнен аудит установленного кода без firmware-правок.
Хеши применимых исходников совпали с сохранённым состоянием после F003.
HOST использует извлечённые App-функции, действительный CAN fragment helper
и HAL/RTOS-модель. Артефакты:
[u21_audit_20261105](test_artifacts/u21_audit_20261105/evidence.json).

- [x] HOST:25 пар/50 команд.22 пары подтвердили обработку исхода и успешное
  следующее чтение: checksum/invalid length/SSI NAK, RX/storage, timeout/partial,
  ACK TX, RTS initial/grant/interbyte/release, HAL_BUSY, TX completion timeout,
  wake delay failure, BeginRead failure, RTOS flags failure и штатные варианты.
  Для scan-пар проверено по одному terminal helper call на команду; физическая
  доставка CAN и давление TX queue этим не подтверждены.
- [ ] U21-A, REPRODUCED: валидный неожиданный SSI opcode A4 в ожидании
  штрихкода -> E601, но ScanCtrl_ProcessReceived оставляет SCANNING.
  Ветка задачи отправляет NACK без завершения доменного состояния; обычный
  STOP его также не завершает. Следующий0601 получает DEVICE_BUSY.
  Места: scan_controller.c:83 и task_scan_controller.c:553.
- [ ] U21-B, REPRODUCED: DECODE_DATA с одним Type без символов проходит
  проверки, получает исходящий SSI ACK, сохраняет пустую строку и DATA_READY.
  CAN fragment helper с длиной0 не передаёт DATA, задача отправляет DONE.
  Места: scan_controller.c:54, task_scan_controller.c:550. Это ложный успех
  без штрихкода, а не нормальный успешный результат0601.
- [ ] U21-C, REPRODUCED в fault-модели RTOS: ScanQueryRevision игнорирует
  возврат osDelay, продолжает heartbeat и завершает capture обычным ответом.
  Воспроизведено3000 возвратов osError при продолжающемся аппаратном tick.
  Место: task_scan_controller.c:262. Реальная причина такого отказа в штатном
  thread context не установлена; это проверка обработки отказа, не BENCH.

Основания: единый DONE как успешный результат atomic-команды (§2.3 стандарта),
согласованный0601 -> штрихкод DATA/DONE, отсутствие активной операции после
terminal failure и общий watchdog-контракт §2.7.1 (ошибка RTOS не доказывает
прогресс). Наблюдаемое mapping существующих NACK сохранено в evidence.json;
оно не превращается в новую согласованную классификацию по именам кодов.
Новых enum/API/NACK/diagnostic fields для аудита не вводилось.

Следующая работа — согласовать исправления конкретных U21-A/B/C с проверкой
всех затронутых мест; готовый код пока не выдавался и не вносился. SSI NAK/
resend, source/continuation и изоляция старых ответов относятся к открытому
U22 и не объявляются завершёнными этим аудитом. F003 результаты сохранены.
Сборка/Release/BENCH в этом шаге NOT RUN: firmware не менялся.

### 05.11.2026 — подготовлена минимальная правка terminal state U21-A

PROPOSED, не установленный CODE: в существующей ветке E601 после отказа
ScanCtrl_ProcessReceived добавлен существующий ScanCtrl_HandleTransportError.
Этот helper уже применяется в BeginRead/START TX/RX/ACK failure branches;
изменение только завершает оставшийся SCANNING перед terminal NACK.
Классификация/пропуск SSI-пакетов, E601, порядок STOP и reset gates сохранены.
Обсуждавшийся перенос STOP перед NACK не включён; новая политика игнорирования
посторонних пакетов также не включена и не считается согласованной.

HOST кандидата:25 пар/50 команд; U21-A теперь E601 -> ERROR -> следующий
успешный DATA/DONE.22 ранее проходившие пары сохранены; U21-B/C по-прежнему
воспроизводятся и остаются OPEN. ARM GCC syntax/type/include PASS,
Wall/Wextra/Werror. Артефакты:
[u21_terminal_state_20261105](code_proposals/u21_terminal_state_20261105/evidence.json).
Полная BUILD/BENCH NOT RUN; firmware проекта не менялся. Следующее действие —
выдать точное место и полный ручной блок; прямого разрешения на внесение нет.

### 06.11.2026 — AGREED: локальное SSI recovery в рамках одной0601

По поручению пользователя зафиксировано принятое решение. Полная запись:
[отчёт, раздел от06.11](SCANNER_EXECUTOR_REPORT.md#06112026--принято-решение-о-локальном-восстановлении-ssi-u21u22).
Эта запись уточняет исторические OPEN/«не согласовано» относительно политики
recovery. Реализация и проверка по-прежнему OPEN.

- [x] AGREED: Дирижёр -> CAN0601 -> МК F103; МК самостоятельно ведёт
  UART/SSI с SE965HP. SSI ACK/NAK не подменяют итоговый CAN NACK.
- [x] AGREED: ошибка checksum полного пакета -> SSI CMD_NAK RESEND ->
  ожидание повторной передачи в рамках текущего чтения. Модуль использует
  штатные две повторные передачи после первой; отдельный произвольный лимит
  дополнительных повторов не вводится. Исходный срок чтения не перезапускается.
- [x] AGREED: пригодный непустой F3 -> SSI ACK -> сохранение -> CAN DATA/DONE;
  повторный F3 не порождает второй результат той же операции.
- [x] AGREED: валидный посторонний A4 во время ожидания штрихкода отбрасывается
  внутри МК, ожидание продолжается; ACK на START не завершает чтение.
  SSI NAK разбирается по причине; DENIED и RESEND требуют разной реакции.
- [x] AGREED: итоговый CAN E603 при отсутствии штрихкода за срок без ошибок
  связи; E602 при неудачном восстановлении повреждённой передачи/отказе UART
  или отправки SSI NAK; E601 при отказе модуля/непригодном содержимом ожидаемого
  результата. Прежние reset -> DEVICE_BUSY и shutdown fatal сохраняются.
- [x] AGREED: SCANNING сохраняется до завершения чтения/recovery; перед
  итоговым NACK операция должна быть завершена и в программном контроллере.
- [x] AGREED как требование: поздний пакет прежней операции не должен стать
  результатом следующего чтения. Конкретный механизм изоляции ещё не выбран.
- [ ] Подготовить полный связный механизм, включая границы пакетов/повторов,
  источник/статус SSI, принадлежность поздних данных, deadline и reset gates.
  Не считать отдельную правку U21-A реализацией всего принятого решения.
- [ ] HOST нового recovery: повреждение -> пригодный повтор -> один результат;
  исчерпание восстановления; timeout без данных; повторные/поздние пакеты;
  reset и следующий запрос после ошибки. Затем внесение и аудит/BUILD/BENCH.

Новые CAN-команды/NACK-коды/формат и Studio не входят в блок. Автоматический
новый START, отдельный общий TX deadline и изменение обычного STOP/terminal
порядка этим решением не утверждены. EE=1/ACK-NAK физического модуля не
подтверждены. U21-C остаётся отдельным OPEN. CODE/HOST/BUILD/BENCH recovery
отсутствуют; firmware не менялся. NEXT_SESSION по промежуточному шагу не менялся.

### 06.11.2026 — подготовка кода recovery: пакетный приёмник и граница чтений

- [x] PROPOSED preparation: подготовлены приватные helpers сборки полного
  SSI-пакета с сохраняемым cursor RX snapshot и классификации ACK/NAK/F3/A4,
  checksum failure -> событие RESEND. Полный обмен/драйвер ими не заменён.
- [x] HOST ASan/UBSan:63 разбиения snapshot, побайтовый поток, ACK+A4+
  повреждённый F3+повторный F3 в одном потоке, golden SSI NAK RESEND,
  empty/source/status/framing checks и различение причин NAK PASS.
- [x] Проверено ограничение: старый отложенный первый F3 может быть побайтно
  одинаков с новым первым F3. Очистка RX МК, checksum и Retransmit bit не
  доказывают принадлежность новому чтению. По SSI §1-4/1-5 ACK нового START
  может предшествовать отложенным старым данным; сам ACK не является flush.
- [ ] PROPOSED, НЕ AGREED: перед каждым новым START подтвердить STOP,
  затем FLUSH_QUEUE/D2 и его ACK, с границей старого RX на host-handshake.
  После отказа подготовки новый START не разрешать. Это дополнительный
  механизм для принятого требования изоляции, не автоматически внесённое
  решение. Budget/точное поведение и поддержка экземпляром SE965HP открыты.

FLUSH_QUEUE и ACK/NAK проверены по SSI Programmer's Guide §2-43/Table1-1;
в руководстве SE965HP явной записи FLUSH_QUEUE не найдено. Поддержка физическим
модулем не подтверждена, generic SSI не заменяет BENCH. Bsr.at копия guide
при проверке не открылась; использована manufacturer-authored копия на studylib.

Артефакты и описание предлагаемых этапов:
[ssi_recovery_20261106](code_proposals/ssi_recovery_20261106/BOUNDARY_PROPOSAL.md).
Статус — неполная подготовка кандидата; целиком recovery не готов и не
объявляется установленным/проверенным. Firmware hashes после F003 неизменны;
full-operation HOST/BUILD/BENCH NOT RUN. Требование изоляции не ослабляется;
до выдачи связного блока нужно согласовать конкретную границу чтений.

### 06.11.2026 — принята граница STOP/FLUSH; подготовлен связный кандидат

- [x] AGREED по ответу пользователя «давай так»: перед новым чтением
  STOP -> подтверждение -> FLUSH_QUEUE -> подтверждение -> START.
  Отказ подготовки не разрешает START. Это уточняет прежний PROPOSED
  статус данной последовательности; поддержка физическим модулем всё ещё
  BENCH NOT CONFIRMED.
- [x] PROPOSED: полный кандидат,20 замен в6 App-файлах, с новой нормальной
  RX boundary в последнем TX TC до CTS HIGH, SSI status encoder, подготовкой
  STOP/FLUSH, cursor assembly, checksum RESEND и согласованными CAN исходами.
  F002/F006/F003, capture и общий CAN wire не меняются.
- [x] HOST ASan/UBSan:34 сценария/63 CAN-команды, включая два чтения,
  старые данные модуля, старый ACK до TC, checksum/retry, NAK causes,
  пустые/непригодные пакеты, отказы подготовки, reset и следующий запрос PASS.
- [x] Регрессия18групп shutdown PASS. Модель дополнена ACK подготовки;
  STOP перед обычным чтением учитывается отдельно от единственного reset STOP.
  Действующие codec golden tests PASS.4 C-файла скомпилированы ARM GCC13.3.1
  Wall/Wextra/Werror, без линковки firmware.
- [ ] Настройки задачи предложены явно, но точные значения отдельно не
  утверждены: ACK preparation wait2000ms (software bound, не readback) и
  максимум2 повтора того же host packet только по RESEND. Для START повтор
  допускается лишь до его ACK, с retransmit bit. Значение host limit —
  адаптация Scanner, не обязательное число из спецификации для host.
- [ ] Интеграция operation timeout OPEN: новая подготовка добавляет время
  до START, текущее окно Studio timeout+7s её худший случай не покрывает.
  Studio по этому блоку не менялась; прежнее утверждение о ненужности любых
  изменений Studio не является доказательством совместимости её таймаута.
- [ ] Внесение/аудит/полная Debug BUILD/Release/BENCH не выполнялись.

Ручные блоки и доказательства:
[MANUAL_IMPLEMENTATION](code_proposals/ssi_recovery_20261106/MANUAL_IMPLEMENTATION.md),
[connected_evidence](code_proposals/ssi_recovery_20261106/connected_evidence.json).
Выдача кода начинается с codec-констант и явного status encoder; остальные
части подготовлены связанно, но установленным CODE не объявляются.
U21-C остаётся отдельным OPEN. Рабочие hashes6 файлов после F003 неизменны.

### 06.11.2026 — codec-блок внесён по отдельному разрешению

- [x] CODE: после выдачи кода в чат пользователь поручил «внеси этот блок
  сам». Изменены только scan_ssi.h/.c: FLUSH_QUEUE/SSI status/NAK constants,
  Ssi_EncodeWithStatus и прежний Ssi_Encode как status0 wrapper.
- [x] Сверка с проверенным кандидатом PASS по C-токенам с учётом выданных
  скобок вокруг прежней argument guard; остальные различия — комментарии
  и форматирование. SHA256 остальных App-файлов не изменились.
- [x] HOST установленного codec: прежние golden tests и отдельные проверки
  START/retransmitted START/FLUSH/NAK RESEND, checksum с выбранным status,
  max payload, status0 equivalence и invalid arguments ASan/UBSan PASS.
- [x] Debug incremental make -j4 all PASS,0errors/0warnings,
  text47332/data72/bss15136. Это не clean build. Доказательства/hashes:
  [ssi_codec_20261106](test_artifacts/ssi_codec_20261106/evidence.json).
- [ ] Следующие части UART/task recovery ещё не внесены. Наличие opcode
  FLUSH_QUEUE не означает отправку команды или её проверку на модуле.

Разрешение относится только к выданному codec-блоку. Firmware driver/task,
Studio/config и NEXT_SESSION не менялись; Release/BENCH, коммит/прошивка/
аппаратные команды не выполнялись. Полный recovery ещё не установлен.

### 06.11.2026 — пользователь подтвердил codec BUILD; подготовлен UART-блок

- [x] BUILD пользователя после codec: «сборка чистая идем дальше».
  Это подтверждение сборки, не заявление о forced clean rebuild/BENCH.
- [x] PROPOSED: выделен следующий промежуточный блок в2 driver-файлах,
  8 точных замен. ScanUart_SendForReply использует обычный reset gate,
  BeginRead owner, RX boundary до CTS HIGH; ошибки RX сохраняются.
  Новые deadlines/retry counters/task/controller не входят в этот шаг.
- [x] HOST промежуточного кандидата: boundary cases,6 позиций reset,
  shutdown18 и прежние reading13 PASS под ASan/UBSan. Проверялся именно
  новый driver с фактическим старым task/controller и установленным codec.
- [x] ARM GCC13.3.1 Wall/Wextra/Werror compilation driver PASS, без линковки
  firmware. Ручные замены точно воспроизводят проверенный driver.
- [ ] Выдача кода -> внесение -> аудит фактических файлов -> Debug BUILD.
  Наличие кандидата не является установленным CODE или разрешением его внести.

Артефакты: [ssi_uart_step_20261106](code_proposals/ssi_uart_step_20261106/MANUAL_IMPLEMENTATION.md).
SHA256 рабочих driver-файлов неизменны; следующее отдельное разрешение
потребуется для прямого внесения. BENCH/Release NOT RUN, NEXT_SESSION не менялся.

### 06.11.2026 — UART boundary внесён по прямому разрешению

- [x] CODE: после выдачи8 изменений пользователь поручил «можешь внести
  изменения». Изменены только scan_uart_driver.h/.c; codec, task/controller,
  F003 и прежний reset shutdown сохранены.
- [x] Побайтное совпадение обоих файлов с проверенным промежуточным кандидатом
  PASS. SHA256 остальных App-файлов не изменились.
- [x] HOST фактического installed C: boundary cases,6 позиций reset,
  прежние18групп shutdown и13 reading scenarios PASS под ASan/UBSan.
- [x] Debug incremental make -j4 all PASS,0errors/0warnings,
  text47492/data72/bss15136. Driver/task перекомпилированы, ELF перелинкован;
  это не clean build. Доказательства/hashes:
  [ssi_uart_step_20261106](test_artifacts/ssi_uart_step_20261106/evidence.json).
- [ ] Следующий блок: task/controller и конкретные параметры подготовки/
  повторов. Новый метод пока задачей не вызывается, STOP/FLUSH/recovery
  ещё не подключены; весь recovery установленным не объявлять.

Release/BENCH NOT RUN. Доставка/ACK модуля, FLUSH support и runtime timing
этими проверками не подтверждены. NEXT_SESSION, Studio/config, коммит,
прошивка и аппаратные команды не выполнялись/не менялись. Разрешение
относится к этому driver-блоку и не переносится на следующие правки.

### 06.11.2026 — driver BUILD подтверждён; подготовлен task preparation

- [x] Пользователь подтвердил чистую сборку после UART-блока.
- [x] PROPOSED: выделен промежуточный task-блок,2 вставки в одном файле:
  ожидание первого ответа STOP/FLUSH, закрытие RX и допуск START только
  после успешной подготовки. Старые barcode loop/controller сохранены.
- [x] HOST20 intermediate scenarios/36commands и18shutdown groups PASS,
  ASan/UBSan. ARM task per-file compile Wall/Wextra/Werror PASS.
- [ ] Предлагаемые значения видны в коде: reply wait2000ms и2 повторные
  передачи того же host control packet только по явному SSI RESEND.
  Это project choices, не автоматически заданные decoder retry policy.
- [ ] Выдача/внесение/аудит/Debug BUILD; task в проекте ещё не менялся.

Опора: установленный Scan_WaitResetStopAck и UART boundary, адаптации —
normal gate, ответ6/7B ACK/NAK, STOP/FLUSH, EndRead и запрет START при отказе.
Полные блоки/результаты:
[ssi_prepare_step_20261106](code_proposals/ssi_prepare_step_20261106/MANUAL_IMPLEMENTATION.md).
Новый общий TX deadline/Studio changes не включены. Временная интеграция
подготовки остаётся OPEN, полный barcode recovery ещё не подключён.

### 06.11.2026 — task preparation STOP/FLUSH внесён по разрешению

- [x] AGREED/CODE: после уточнения о NACK пользователь поручил «вноси блок
  сам». Внесены2 выданные вставки в task_scan_controller.c с показанными
  настройками: reply wait2000ms, максимум2 повторные передачи того же
  STOP/FLUSH только по RESEND. Молчание не вызывает повтор. SSI NAK causes
  отделены от CAN NACK; CAN-коды соответствуют global config.
- [x] Побайтное совпадение task с проверенным промежуточным кандидатом PASS;
  SHA256 остальных App-файлов не изменились.
- [x] HOST фактического C:20 intermediate scenarios/36commands и18shutdown
  groups PASS под ASan/UBSan. START запрещён при неподтверждённой подготовке;
  reset сохраняет свой STOP-only path и приоритет DEVICE_BUSY.
- [x] Debug incremental make -j4 all PASS,0errors/0warnings,
  text48740/data72/bss15136. Task перекомпилирован, ELF перелинкован;
  не clean build. Evidence/hashes:
  [ssi_prepare_step_20261106](test_artifacts/ssi_prepare_step_20261106/evidence.json).
- [ ] Следующий блок — barcode receive/recovery и непустая строка в controller.
  Старый цикл чтения пока сохранён; полное recovery не объявлять установленным.

BENCH/Release NOT RUN. Поддержка FLUSH модулем и фактические времена не
подтверждены; внешний operation timeout/окно Studio остаются OPEN.
NEXT_SESSION, Studio/config, коммит/прошивка/аппаратные команды не менялись/
не выполнялись. Разрешение относится к внесённому preparation-блоку.

### 06.11.2026 — подтверждение BUILD и фиксация текущего состояния

- [x] Пользователь подтвердил успешную сборку установленного preparation-блока.
  Это подтверждение BUILD, не forced clean rebuild и не BENCH.
- [x] Отчёт, планы, NEXT_SESSION, правила и указатели проекта актуализированы:
  reset/F003/codec/UART boundary/STOP-FLUSH установлены; barcode recovery и
  проверка непустой строки остаются следующим невнесённым блоком.
- [x] По прямому поручению сохраняются текущие9 App-файлов и снимок документов/
  доказательств в readme/checkpoints/20261106/ обычным коммитом поверх ea1eacb.
  Итоговый hash определяется коммитом, содержащим снимок: git log -1 --oneline.

При подготовке снимка firmware не менялся; повторные HOST/BUILD не запускались.
Сохранены результаты последнего установленного блока и SHA256 сверены с
фактическими исходниками/ELF. Release/BENCH NOT RUN. Push, прошивка и аппаратные
команды в это поручение не входят.
