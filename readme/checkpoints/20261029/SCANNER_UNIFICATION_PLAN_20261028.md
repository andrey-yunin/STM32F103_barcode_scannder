# Scanner — аудит и план унификации общих слоёв

Дата: 28.10.2026. Статус: аудит CODE выполнен, исправления по этому плану НЕ начаты.
Галочка означает выполненную проверку/работу, а не принятие платы целиком.
BUILD/HOST/BENCH для будущих изменений — NOT RUN. Прежние сборки/тесты
не закрывают приведённые ниже расхождения.

## Выполнение на29.10.2026 — текущий статус

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
