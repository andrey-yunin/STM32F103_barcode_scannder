# Scanner Executor Implementation Plan

> Уточнение02.11.2026: действующий план работ —
> [план унификации, U22](SCANNER_UNIFICATION_PLAN_20261028.md#u22--согласованное-изменение-rx02112026).
> RX и TX USART3 переведены на UART IT; оба DMA-запроса удалены через Cube.
> Накопитель RX128B потребляется задачей по мере поступления, сборка Length+2.
> CODE частично, чистая сборка после TX IT сообщена пользователем.
> Закрытие RX перед terminal реализовано и проверено02.11: HOST13 сценариев,
> Debug ассистента45740/68/15128 без ошибок/предупреждений.
> STOP нужен для управляемой отмены
> незавершённой попытки; обязательный STOP ACK после успеха не вводится.
> Отдельные открытые блоки: доменная остановка перед F002/F006 и защита
> F003 от активного UART. Бюджет reset1s и запрет TX после latch требуют
> согласованного механизма; он ещё не реализован. BENCH NOT RUN.
> Последняя редакция — раздел «Остановка сканера перед reset и Flash —
> уточнение02.11.2026» плана унификации.
> Конец02.11: следующий блок F002/F006 согласован — STOP-only shutdown после
> latch, единый бюджет3s вместо текущего1s. Код ещё не внесён. Начинать с
> актуального NEXT_SESSION и последнего раздела плана, F003 следует отдельно.
> Прежний кандидат256B, DMA-описания Stage4 и статус Firmware complete
> ниже исторические; NEXT_SESSION30.10 не отражает сегодняшние изменения.

> Контрольная точка 27.10.2026: работа остановлена, аппаратное чтение ещё не принято.
> Исторические этапы ниже не описывают фактическую разводку новой платы.
> Актуальны NEXT_SESSION_PROMPT.md и SCANNER_CONNECTION_20261027.md.

**Дата старта:** 08.07.2026
**Проект прошивки:** `STM32F103_scanner`
**Статус:** Firmware complete (Stage 1-5), 03.08.2026 выровнен к канону экосистемы (§3.2.5). Ожидает физической валидации (Stage 6, CAN bench).

## Этапы

### Stage 0: Logic analyzer — захват протокола SE-965HP

- [ ] Приобрести/подготовить модуль MAX3232 (RS-232↔TTL) для подключения LA к RS-232 линиям коннектора
- [ ] Либо найти TTL точки на плате сканера (до SP3232EEA) для прямого подключения LA
- [ ] Подключить логический анализатор к UART TX (через MAX3232 или с TTL точек) и GND
- [ ] Подать питание 5V на 4-pin коннектор
- [ ] Подать питание 3.3V на STM32-сторону SP3232/MAX3232 (если используется модуль)
- [x] Найден TRIGGER на 12-pin ZIF SE-965HP — **pin 12** (active low, Integration Guide Table 1-3)
- [ ] Дёрнуть TRIGGER (замкнуть на GND через кнопку/перемычку; обязателен pull-up 10k к 3.3V)
- [ ] Отсканировать реальный штрихкод реагента
- [ ] Захватить UART трафик анализатором
- [ ] Определить:
      - [ ] baudrate
      - [ ] формат (8N1?)
      - [ ] префиксы/суффиксы (STX/ETX/CR/LF)
      - [ ] максимальная длина строки
      - [ ] таймаут между TRIGGER и началом данных
      - [ ] длительность TRIGGER импульса для старта
- [ ] Зафиксировать результат в отчёте
- [ ] Скорректировать CAN-контракт DATA packing под реальный формат

**Проверка:** осциллограмма/захват PulseView сохранён как артефакт,
  параметры протокола зафиксированы в отчёте.

---

### Stage 1: Ecosystem config

- [ ] Добавить `CAN_ADDR_SCANNER_BOARD = 0x60` в `dds240_global_config.h`
- [ ] Добавить `CAN_DEVICE_TYPE_SCANNER = 0x05` в `dds240_global_config.h`
- [ ] Добавить доменные команды `0x0601..0x0602` в `dds240_global_config.h`
- [ ] Добавить NACK-коды `0xE600..0xE603` в `dds240_global_config.h`
- [ ] Синхронизировать `CAN_Protocol/5_Low_Level_Commands.md`
- [ ] Синхронизировать `CAN_Protocol/3_Application_Layer.md`

**Проверка:** `git diff --check` на `dds240_global_config.h`; code review
  изменений.

---

### Stage 2: Project creation (CubeMX + baseline)

- [ ] Создать копию reference-проекта (`STM32F103_temp_sensors` как
      измерительный executor) в `STM32F103_scanner`
- [ ] Сменить NodeID: `CAN_ADDR_SCANNER_BOARD = 0x60`
- [ ] Сменить DeviceType: `CAN_DEVICE_TYPE_SCANNER = 0x05`
- [ ] Настроить USART2: PA2(TX), PA3(RX), baudrate=<из LA захвата>,
      8N1, без прерываний (RX через DMA или polling)
- [ ] Настроить GPIO:
      - PB0 = SCAN_TRIGGER (output, idle HIGH)
      - PB1 = SCAN_DECODE_LED (input, опционально)
- [ ] Настроить CAN profile executor: `Prescaler=2`, `BS1=11TQ`, `BS2=4TQ`
- [ ] Включить FreeRTOS CMSIS_V2
- [ ] Создать 4 задачи: `task_can_handler`, `task_dispatcher`,
      `task_scan_controller`, `task_watchdog`
- [ ] Создать очереди: `can_rx_queue`(16), `can_tx_queue`(16),
      `dispatcher_queue`(8), `scan_queue`(4)
- [ ] Создать `can_protocol.h` (локальный, синхронизированный с global config)
- [ ] Создать `app_config.h`, `app_queues.h`
- [ ] Настроить IWDG: `Prescaler=256`, `Reload=624`
- [ ] Настроить `configTOTAL_HEAP_SIZE` = 8192 (initial)
- [ ] Проверить: Debug-сборка проходит (`text`, `data`, `bss` в лимитах F103)

**Проверка:** проект собирается в STM32CubeIDE, `git diff --check` чист.

---

### Stage 3: CAN transport + service layer

- [x] Реализовать RX ISR callback, Extended ID filter, strict DLC=8
- [x] Реализовать TX очередь с drain, bounded mailbox wait (10 ms)
- [x] Реализовать drop counters (not_ext, wrong_dst, wrong_type, wrong_dlc)
- [x] Реализовать dispatcher: ACK policy, service/domain routing
- [x] Реализовать `F001 GET_DEVICE_INFO`
- [x] Реализовать `F002 REBOOT`
- [x] Реализовать `F003 COMMIT` (заглушка NACK, нет app_flash)
- [x] Реализовать `F004 GET_UID`
- [x] Реализовать `F005 SET_NODE_ID` (заглушка NACK, нет app_flash)
- [x] Реализовать `F006 FACTORY_RESET`
- [x] Реализовать `F007 GET_STATUS` с common metrics `0x0001..0x0012`
- [x] Реализовать `app_flash` (config page `0x0800FC00`)
- [ ] Добавить `Board_EnterSafeState()`: GPIO в безопасное состояние
- [ ] Проверить: Error_Handler -> safe-state
- [ ] Проверить: startup safe-state до scheduler

**CAN bench проверка:**
- [ ] `F001` возвращает корректный DeviceType=0x05, NodeID=0x60
- [ ] `F004` возвращает UID
- [ ] `F007` возвращает `ACK -> 18 DATA -> DONE`
- [ ] NACK на неизвестную команду
- [ ] Foreign address игнорируется без ACK/NACK

---

### Stage 4: UART + Scanner driver

**Архитектура (утверждено 13.07.2026):**

```
task_scan_controller (event-driven через flags)
  │
  ├── scan_uart_driver.c — DMA single-shot + IDLE restart (USART3, 9600 8N1)
  ├── scan_ssi.c         — кодер/декодер SSI-пакетов (CRC16 2's complement)
  └── scan_controller.c  — КА: IDLE/SCANNING/DATA_READY/ERROR, shadow buffer 128B
```

- UART приём: **DMA circular (128B) + IDLE line detection + copy-out в holding_buf (128B)**
- scan_controller отправляет CAN DATA/DONE/NACK напрямую в `can_tx_queue` (через `CAN_Send*`)
- TRIG не выведен на коннектор — управление через SSI-команды (`SCAN_ENABLE`, `START_DECODE`, `STOP_DECODE`)
- Baudrate: 9600 (default SE-965HP, будет уточнён после LA захвата)
- SSI-пакеты: [Length][Opcode][MsgSrc][Status][Data..][CRC16]

- [x] Реализовать `scan_uart_driver.c`:
      - `ScanUart_Init()` — настройка USART3 (PB10/PB11), DMA, IDLE
      - `ScanUart_StartRx()` — запуск DMA circular + IDLE
      - `ScanUart_Send()` — прямая отправка SSI-пакета через HAL_UART_Transmit_DMA
      - `HAL_UARTEx_RxToIdleCallback()` — копирование holding_buf, флаг task'у
- [x] Реализовать `scan_ssi.c`:
      - `Ssi_Encode(opcode, data, len, out_packet)` — сборка с CRC16 2's complement
      - `Ssi_Decode(buf, len, out_msg)` — валидация CRC + разбор
      - Структура `SsiMsg_t`: opcode, status, data[128], data_len
- [x] Реализовать `scan_controller.c`:
      - `ScanCtrl_Init()` — инициализация shadow buffer, КА
      - `ScanCtrl_ProcessReceived()` — декодирование SSI → DATA/NAK
      - `ScanCtrl_GetLastBarcode(str, &len)` — shadow buffer
      - `ScanCtrl_GetStats()` — счётчики ошибок SSI/таймаутов
      - Состояния: IDLE / SCANNING / DATA_READY / ERROR
- [x] Реализовать `task_scan_controller.c`:
      - Обработка `SCAN_REQUEST`: START_DECODE → osThreadFlagsWait → CAN_SendDataFragmented/Done/NACK
      - Обработка `SCAN_GET_DATA`: shadow → CAN_SendDataFragmented/Done/NACK
      - Таймаут через osThreadFlagsWait(timeout)
      - SCAN_ENABLE однократно при старте
- [x] Реализовать DATA packing (фрагментация по 6 байт с seq/EOT) — `CAN_SendDataFragmented`

**Проверка (без сканера):**
- [ ] USB-UART адаптер подключен к PB10/PB11 (через MAX3232)
- [ ] PC отправляет SSI-пакеты в UART
- [ ] `SCAN_REQUEST` с эмуляцией SSI возвращает DATA -> DONE
- [ ] Fake IDLE генерируется

**CAN bench проверка:**
- [ ] `SCAN_REQUEST` -> ACK -> DATA(строка) -> DONE
- [ ] `SCAN_GET_DATA` -> ACK -> DATA(последняя строка) -> DONE
- [ ] `SCAN_REQUEST` с таймаутом -> ACK -> NACK(0xE603 TIMEOUT)
- [ ] `SCAN_GET_DATA` без сканирования -> NACK(0xE600 NO_DATA)

---

### Stage 5: Watchdog + safe-state hardening

- [x] Реализовать `task_watchdog`: heartbeat supervisor для CAN, Dispatcher,
      Scan Controller
- [x] Настроить IWDG через CubeMX, refresh только из watchdog task
- [x] Убедиться, что safe-state дёргает SCAN_TRIGGER=HIGH (лазер выключен)
- [ ] Проверить idle 60s без ложного сброса (CAN bench, Stage 6)
- [ ] Fault-injection: остановка heartbeat scan_controller -> ожидать reset (CAN bench, Stage 6)

---

### Stage 6: Physical scanner validation

**Status: BLOCKED — CAN transport not responding (14.07.2026)**

Pre-condition: scanner must ACK broadcast F001 on CAN bus.
Current state: `HAL_CAN_ConfigFilter` returns HAL_ERROR, no frames received.
Root cause not yet identified.

- [ ] **BLOCKER**: Resolve CAN communication failure
      - Compare `MX_CAN_Init` / `HAL_CAN_MspInit` between Scanner and Photometer
      - Identify why `HAL_CAN_ConfigFilter` fails on Scanner
      - Fix and verify F001 response
- [ ] Подключить Zebra SE-965HP к STM32F103_scanner
      - VCC=3.3V, GND
      - TX сканера -> PA3(RX STM32)
      - RX сканера -> PA2(TX STM32, опционально для конфигурации)
      - TRIGGER -> PB0
- [ ] Настроить сканер через configuration barcodes (baudrate, symbologies)
- [ ] Проверить UART приём реальных штрихкодов
- [ ] Проверить все 3 сценария:
      - [ ] Штрихкод найден -> ACK -> DATA -> DONE
      - [ ] Таймаут -> ACK -> NACK(0xE600)
      - [ ] Повторный SCAN без ожидания -> NACK(0xE603 BUSY)

### Stage 6.1: Повторное тестирование на стенде после выравнивания (03.08.2026)

**Причина:** 03.08.2026 внесены правки к канону экосистемы (§3.2.5): SET_NODE_ID (байт 2, RAM-only), TX source из рантайма, permissive-фильтр + софт-валидация, переименование API. Требуется повторная физическая проверка на стенде — поведение CAN изменилось.

- [ ] Broadcast `F001 GET_DEVICE_INFO`: сканер отвечает ACK/DONE (DeviceType=0x05, NodeID=0x60)
- [ ] `F005 SET_NODE_ID` (валидный, байт 2): ACK со старого адреса → DONE с нового; перезагрузка без `F003 COMMIT` — адрес не сохранился (RAM-only)
- [ ] `F005 SET_NODE_ID` (невалидный: 0x00, 0x01, 0x10 Conductor, 0x7F/0x80): NACK `0x0006 CAN_ERR_INVALID_PARAM`
- [ ] `F003 COMMIT` после SET_NODE_ID: адрес сохранился после перезагрузки; `F004 GET_UID` корректен
- [ ] `F006 FACTORY_RESET` (key 0xDEAD): возврат к default NodeID 0x60 после перезагрузки
- [ ] Perceptive-фильтр: фреймы с чужим dst отбрасываются софтом, `DROP_WRONG_DST` не растёт без нагрузки
- [ ] TX source: исходящие фреймы адресованы новым NodeID (после SET_NODE_ID + COMMIT)
- [ ] Сверка по §6 «Регрессионная сверка протокола» (NodeID, команды, NACK, ACK/DONE transition)

---

### Stage 7: Service Studio integration

- [ ] Добавить Scanner в `domain.dds240_decoder.py`:
      - `CAN_ADDR_SCANNER_BOARD = 0x60`
      - `CAN_DEVICE_TYPE_SCANNER = 0x05`
      - `CMD_SCAN_REQUEST = 0x0601`
      - `CMD_SCAN_GET_DATA = 0x0602`
- [ ] Добавить декодирование DATA фреймов Scanner в `decode_can_payload()`
- [ ] Добавить кнопку "Scan Barcode" в `Conductor Emulator` или отдельную вкладку
- [ ] Проверить System Scan -> обнаружение Scanner на шине

---

### Stage 8: Integration with Conductor

- [ ] Добавить Scanner NodeID/DeviceType в Conductor device mapping
- [ ] Добавить packer/unpacker для `0x0601/0x0602`
- [ ] Определить Host-команду для сканера (например, `0x7000 BARCODE_SCAN`)
- [ ] Реализовать Conductor recipe action для сканирования
- [ ] Проверить E2E: Host -> Conductor -> Scanner -> Conductor -> Host

---

### Stage 9: Acceptance

- [ ] Пройти `EXECUTOR_INDUSTRIALIZATION_PLAYBOOK.md`
- [ ] CAN acceptance level A (service commands, transport, NACK matrix)
- [ ] CAN acceptance level B (domain commands, DATA packing)
- [ ] CAN acceptance level C (broadcast, fault injection, bus-off recovery)
- [ ] IWDG fault-injection test
- [ ] Safe-state validation
- [ ] Flash persistence test (commit, reboot, verify)
- [ ] Фиксация версии прошивки

---

## Условные обозначения

- `[ ]` — задача не начата
- `[~]` — задача в работе
- `[x]` — задача завершена
- `[CAN]` — проверка на CAN стенде (Service Studio)
- `[LA]` — проверка логическим анализатором


## 2026-10-27 — одиночный BEEP, CAN 0x0604

Пользователь уточнил фактически установленный сканер: Zebra SE-965HP-I200R
(раньше в отчёте указан I300R). Спецификация Zebra подтверждает SSI over TTL
Serial для SE965-HP и питание I200R 3.3 V ±0.3 V. Исправления SSI Length и
checksum сохраняют силу. REQUEST_REVISION: Length04, передача6bytes; BEEP:
Length05, передача7bytes `05 E6 04 00 00 FF 11`, code00=один короткий высокий бип.
Источник: Zebra SSI Programmer's Guide 72E-40451-03, tables2-10..2-12.

Добавлен CAN_CMD_SCAN_BEEP=0x0604, request `04 06 00 00 00 00 00 00`:
нулевые аргументы обязательны, на плате используется та же очередь/задача,
что и для сканирования. Wake00 + пауза, один SSI BEEP, захват rawRX3s.
Ответ имеет envelope как 0x0603: schema1,flags8,length16,errors32,raw,padding0.
ACK/DATA/DONE означают выполнение CAN-процедуры, не доказательство звука.
Ошибки wake/TX -> E601; недоступность очереди -> BUSY. Повторной отправки
BEEP автоматически нет. Команда не запускает лазерное сканирование.

Studio: Control Panel сканера -> Commands -> «Один короткий бип / сырой RX
(0x0604)». Окно ожидания4s. Показаны сырые байты и SSI ACK/NAK; корректный
ACK трактуется как ответ в окне захвата, а не аппаратное подтверждение звука.
Для звука нужен подключённый звукоизлучатель.

Проверки: SSI golden BEEP7bytes; реальный UART-driver/task helper на host
передаёт HAL ровно7bytes и формирует DONE0604; ошибочный wake даёт NACK.
Сборка Debug PASS. Тесты Studio включают кнопку -> controller -> service ->
transport, ACK/NAK/empty/truncated/error capture. Сигнал на плату пока не
отправлялся: установленная до этого прошивка не имеет обработчика0604.
