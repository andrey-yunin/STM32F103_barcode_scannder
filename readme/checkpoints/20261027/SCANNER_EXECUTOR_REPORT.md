# Scanner Executor Project Report

## Точка останова — 27.10.2026, по просьбе пользователя

Работа со сканером остановлена. Следующий шаг — уточнение/доработка физического
подключения, а не перебор скоростей или новая команда. Автоматически не
прошивать и не отправлять CAN-команды. Прямые firmware-правки в этой сессии
были явно разрешены пользователем; это уточняет старое правило консультации.

### Фактическая плата и результаты

- SE-965HP-I300R подключён напрямую к MCU, только UART; преобразователя
  RS-232 в текущей схеме нет. Старые описания адаптера и PB0/TRIG исторические.
- MCU TX PB10 -> RXD сканера pin4; TXD сканера pin5 -> MCU RX PB11.
  Пользователь обнаружил TX-TX/RX-RX, после исправления сообщил о красном
  свете при запросе. Это реакция модуля, не подтверждённый SSI-ответ.
- Рабочая конфигурация исходников: USART3 Asynchronous, 9600 8N1, flow None;
  CAN AutoRetransmission ENABLE. При повторном тесте115200 лазер перестал
  реагировать, пользователь вернулся к9600. Перебор скоростей отложен.
- Последние предоставленные логи16:06/16:08: BEEP0604 и GET_REVISION0603
  вернули полный CAN DATA/DONE, сырой UART RX0, ошибки0. SCAN_REQUEST0601
  заканчивается E603. Фраза пользователя «теперь работает» не уточнена:
  подтверждённого REPLY_REVISION/штрихкода в логах нет. Заменённый сканер
  ранее давал тот же результат. UART-петля предложена, результата нет.
- Измерено: RTS* pin7 =3.3V, CTS* pin6 =LOW; по словам пользователя CTS
  никуда не подключён. В SSI LOW на CTS может удерживать передачу сканера.
  Это проверяемая гипотеза, не доказательство единственной причины.
- Штатное SSI: MCU опускает CTS сканера, ждёт RTS LOW, передаёт пакет,
  затем возвращает CTS HIGH, позволяя ответить. RTS не объявляет передачу
  сканера. UART WAKE00 не заменяет эту последовательность.

### Что реализовано

- SSI Length=4+payload, checksum MSB-first и корректная проверка; golden START
  `04 E4 04 00 FF 14`, ENABLE `04 E9 04 00 FF 0F`.
- Повторный запуск разрешён после ERROR/DATA_READY, активный SCANNING busy.
- Перед ENABLE/START/STOP/запросами: отдельный00, завершение TX, >=10ms пауза.
  Длительность выхода из сна по SE965HP до8ms; ACK на00 не ожидается.
- Общий service F020/F021, D501 raw last-RX и D502 retained START TX;
  общий модуль UART-диагностики Studio с профилями плат, без новой панели.
- CAN0603 версия (SSI A3), CAN0604 BEEP (E6,data00). Запрос без параметров;
  захват3s до192байт, schema1/flags/length16/errors32/raw, padding6.
  DONE означает конец захвата, не правильный SSI. D502 сохраняет START,
  не wake/revision/beep. Studio показывает сырой RX, отдельно проверяет SSI.

### Оставшиеся ограничения — не потерять при продолжении

- Studio иногда пишет Capture ended without DONE/NACK при фактическом DONE:
  main.py использует состояние фонового _domain_observers; локальный observer
  0603/0604 уже видит terminal. Исправление и regression test ещё не выполнены.
- Scan task реагирует на первое RX-событие; ACK не должен завершать ожидание
  штрихкода. Default module raw barcode и ожидание SSI DECODE_DATA прошивкой
  ещё не согласованы. RX DMA/re-arm/сборка полных SSI-пакетов требует проверки.
- Нет достоверного контроля готовности/ACK startup ENABLE, неизвестны уровни
  остальных неподключённых входов. Полная аппаратная приёмка не завершена.

### Подключение следующей ревизии

См. SCANNER_CONNECTION_20261027.md. Пользователь обсуждал вывод CTS/RTS,
TRIG,AIM/WAKE,PWRDWN на MCU. Конкретные свободные GPIO ещё не назначены.
Подтяжки10кОм — проектная рекомендация, не всеобщее требование производителя.
Ёмкости на управляющие линии для «снятия неопределённости» не добавлять:
на active-low входах они способны создавать запуск при включении питания.

Следующая сессия: сначала выяснить, изменена ли плата/шлейф и каким стал CTS;
если добавлены GPIO — согласовать реальные номера и реализовать SSI handshake.
Если остаётся только UART — проверить петлю и сигналы на модуле. Не утверждать,
что связь восстановлена, без принятого сырого ответа/валидного SSI.

---

## Конец сессии 23.10.2026 — первый аппаратный smoke, дальнейший шаг LA

Scanner Node60 определяется: F001 ACK/DATA00/01/82/DONE,
DeviceType05/FW1.0/count1/UID51FF6E064986775422162467.
F004 в предоставленном фрагменте имеет DATA80/80 вместо00/81;
несоответствие подтверждено source CAN_SendData с фиксированным80.
[Выбранные исходные кадры](test_artifacts/bench_20261023/identity/manifest.json).
Это не завершение унификации; пользователь решил её пока не начинать.

GET_LAST_DATA0602 дал ACK/NACK E600 (пустой буфер). SCAN_REQUEST0601,
10с, дал ACK/NACK E603; подсветки и звука пользователь не наблюдал.
Успешного штрихкода нет. Вместо работы щупами на собранной плате
согласован следующий этап на макетке: анализатор на USART3 TX PB10
(опционально RX PB11), TTL-сторона до MAX3232, текущая настройка1152008N1.
Захват при старте для SCAN_ENABLE и при одном0601 для START_DECODE.
Макетка, загрузка в неё и фактический LA-захват пока не подтверждены.

Текущий encoder по коду выдаёт startup03E9040010FF и START03E4040015FF;
это ожидаемые байты текущего source, НЕ утверждение корректности SSI.
Нужна первичная сверка Length/checksum/framing и UART со спецификацией
реального модуля; документация проекта говорит о9600 по умолчанию,
настройки модуля неизвестны. Задача после timeout/error не возвращает
ScanCtrl в IDLE: повтор без reset может получить BUSY. Прошивку не меняли.

Отдельно исследовался трафик термоплаты40:9010→ACK/NACK E402 примерно2с.
Это запросы от10, не сканера60 и не нагревателя80. В H723 циклический
ThermoMonitor существует в git с13.08, активируется в READY; его файлы
не изменены. По сообщению пользователя после power-cycle H723 опрос исчез,
но предоставлены только короткие startup-фрагменты. Зависание и его причина
не доказаны. Ассистент H723 не менял; не приписывать этот трафик Scanner.

**Дата:** 13.07.2026
**Проект прошивки:** `STM32F103_scanner`
**Статус:** Firmware complete (Stage 1-5), ожидает физической валидации (Stage 6). 03.08.2026 — выравнивание к канону экосистемы (§3.2.4/§3.2.5), сборка Debug чистая.

## 1. Objective

Создать Scanner Executor для DDS-240 как отдельную STM32F103-плату для
считывания 1D штрихкодов через встроенный лазерный сканер Zebra SE-965HP-I300R.
Плата получает команду сканирования по CAN, активирует сканер, принимает
декодированную строку по UART и возвращает результат Дирижеру.

Ключевая граница ответственности:

- Scanner Executor не интерпретирует содержимое штрихкода;
- Scanner Executor не управляет позиционированием этикетки/пробирки;
- Scanner Executor получает только атомарную low-level команду SCAN и
  возвращает сырую строку;
- Дирижер решает, что делать с результатом (валидация, маршрутизация,
  привязка к образцу/рецепту).

## 2. Выбор периферии

### 2.1. Zebra SE-965HP-I300R

Миниатюрный 1D лазерный scan engine (decoded):

| Параметр | Значение |
|----------|----------|
| Модель | SE-965HP-I300R |
| Тип | 1D laser, decoded engine |
| Интерфейс | UART TTL 3.3V |
| Питание | 3.3 VDC ± 0.3 V, 60 mA typ, 165 µA standby |
| Выход | Готовая строка штрихкода (ASCII) |
| Управление | TRIGGER (pin12, active low), DECODE LED (pin10, выход), BEEPER (pin9, выход) |
| Интерфейсный разъём | 12-pin ZIF, SSI over TTL Serial |
| Дефолтный UART | 9600 8N1, ACK/NAK enabled, unpacketed data |
| Рабочий диапазон | до 5.1 м |
| Габариты | Sugar cube (крайне компактный) |

### 2.1.1. 12-pin ZIF Connector Pinout (SE-965HP)

Источник: Integration Guide, Table 1-3.

| Pin | Сигнал | Type | Описание |
|-----|--------|------|----------|
| 1 | `FLASH_DWLD*` | I | Flash download (pull low для прошивки) |
| 2 | `V_BATT` | PWR | Питание 3.0–3.6 VDC |
| 3 | `GND` | PWR | Земля |
| 4 | `RXD` | I | **Serial input** (UART, от STM32 к сканеру) |
| 5 | `TXD` | O | **Serial output** (UART, от сканера к STM32) |
| 6 | `CTS*` | I | Clear to Send (handshake) |
| 7 | `RTS*` | O | Request to Send (handshake) |
| 8 | `PWRDWN` | O | Power Down Ready (high = low power mode) |
| 9 | `BPR*` | O | Beeper output (активный низкий) |
| 10 | `DLED*` | O | Decode LED output (активный низкий) |
| 11 | `AIM/WAKE*` | I | Wake (200ns pulse) / AIM spot |
| 12 | `TRIG*` | I | **Trigger** (active low — замкнуть на GND для старта сканирования) |

Сигналы с `*` — active low (активный уровень = GND).

Все сигналы — **3.3V TTL**. Необходим pull-up на TRIG* (10k к 3.3V), иначе пин в неопределённом состоянии.

Типовой сценарий работы:

1. STM32 дёргает TRIGGER pin (LOW активный, ~50 ms)
2. Сканер включает лазер, детектит штрихкод
3. Декодированная строка отправляется по UART (TX от сканера -> RX STM32)
4. Сканер автоматически выключает лазер после декодирования или таймаута
5. STM32 принимает строку, пакует в DATA фреймы и шлёт по CAN

### 2.2. Альтернативы не рассматривались

SE-965HP выбран исходя из:
- проверенной интеграции с STM32F103 (UART TTL 3.3V, без уровней);
- промышленной надёжности Zebra (lifetime warranty на мотор);
- компактности для встраивания в анализатор.

## 3. Physical board investigation (09.07.2026)

### 3.1. Компоненты на плате сканера

В ходе физического осмотра и измерений идентифицированы:

| Компонент | Маркировка | Тип | Роль |
|-----------|-----------|-----|------|
| SP3232EEA | 2416L | RS-232 transceiver 3.3V | Преобразует TTL UART SE-965HP в RS-232 на коннектор |
| LM1117-3.3 | N05A? (N03A?) | LDO 3.3V, SOT-223 | Понижает 5V→3.3V для питания SE-965HP |
| SE-965HP I300R | — | 1D laser scan engine | Decoded, UART TTL 3.3V |

### 3.2. 4-pin коннектор (выход на STM32)

| Пин | Сигнал | Уровень (idle) | Стандарт |
|-----|--------|---------------|----------|
| 1 | GND | 0V | — |
| 2 | TX | **-5.7V** | RS-232 (MARK/idle) |
| 3 | RX | — | RS-232 |
| 4 | 5V | +5V | Питание платы |

Измерено мультиметром: TX относительно GND = -5.7V. Это классический RS-232.

### 3.3. Измерения LM1117

| Пин (слева направо) | Напряжение | Вывод |
|---------------------|-----------|-------|
| Левый (GND) | 0V | Земля |
| Средний (VOUT) | 3.35V (1.65V относительно VIN) | **3.3V — питание сканера** |
| Правый (VIN) | 5V | Вход от 5V коннектора |

### 3.4. Полная цепь сигналов

```text
SE-965HP I300R (TTL 3.3V)      Плата сканера               4-pin коннектор
┌─────────────────────┐      ┌──────────────────────┐      ┌──────────────┐
│                     │      │ SP3232EEA            │      │               │
│ TX ── 3.3V(idle) ──→│────→│ T1IN → T1OUT ────────→│────→│ TX: -5.7V    │
│     ── 0V(data)  ──→│     │        (+5.7V data)   │      │     (+5.7V)  │
│                     │      │                      │      │               │
│ RX ←── 3.3V(idle) ←──│←────│ R1OUT ← R1IN ←───────│←────│ RX: ±5..±10V │
│     ←── 0V(data)  ←──│     │                      │      │               │
│                     │      │         ┌──────────┐ │      │ 5V ───────────│
│ VCC ────────────────→│────→│← 3.3V ←│LM1117-3.3│←│─←────│               │
│                     │      │         │ (N0xA)   │ │      │ GND ──────────│
│ GND ────────────────→│────→│←──── GND └──────────┘ │      │               │
└─────────────────────┘      └──────────────────────┘      └──────────────┘
```

### 3.5. RS-232 протокол

RS-232 инвертирован относительно TTL UART:

| Состояние | TTL на сканере | RS-232 на коннекторе | После SP3232 на STM32 |
|-----------|---------------|---------------------|----------------------|
| MARK (idle, 1) | 3.3V | **-5.7V** | 3.3V |
| SPACE (data, 0) | 0V | **+5.7V** | 0V |

SP3232EEA **инвертирует и сдвигает уровни**: ±5.7V ↔ 0/3.3V.

### 3.6. Выводы

1. **SE-965HP I300R** — decoded engine, интерфейс **UART TTL 3.3V** (не I2C).
2. **SP3232EEA** на плате конвертирует TTL 3.3V ↔ RS-232 на 4-pin коннектор.
3. **LM1117-3.3** (маркировка N0xA, не N05A) делает 5V→3.3V для питания сканера.
4. На коннекторе **RS-232 уровни** (±5.7V). Прямое подключение к STM32F103 **запрещено** — PA2/PA3 не 5V-tolerant, напряжение превышает допуск.

### 3.7. Варианты подключения к STM32F103

**Вариант A: SP3232EEA на плате STM32**
- Добавить SP3232EEA (или MAX3232) на плату STM32F103
- RS-232 сторона — к 4-pin коннектору сканера
- TTL сторона — к PA2(USART TX), PA3(USART RX)
- Питание SP3232EEA от 3.3V

**Вариант B: Готовый модуль MAX3232**
- Купить модуль MAX3232 (~$2)
- Соединить: TTL→STM32, RS-232→коннектор сканера
- Питание модуля от 3.3V

**Вариант C: Перепаять плату сканера (не рекомендуется)**
- Обойти SP3232EEA на плате сканера, подключиться к TTL точкам до трансивера
- Требует пайки к мелким выводам, риск повредить плату

**Принятое решение (09.07.2026):** Вариант B — модуль MAX3232 на STM32-стороне. Временное решение, пока в схеме платы сканера присутствует SP3232EEA.

**Перспективное решение:** При редизайне платы сканера SP3232EEA будет исключён — SE-965HP будет подключён напрямую TTL 3.3V к STM32 без RS-232 преобразования.

## 4. Первый шаг — захват UART протокола анализатором

До создания проекта firmware необходимо выполнить захват реального протокола
SE-965HP с помощью логического анализатора:

- Подключить LA к UART TX (выход сканера) и GND
- Подать питание 3.3V
- Замкнуть TRIGGER на GND для запуска сканирования
- Отсканировать реальный штрихкод реагента
- Захватить и декодировать UART трафик

Цели захвата:

| Параметр | Зачем |
|----------|-------|
| Baudrate | Без угадывания — точное значение из анализатора |
| Формат данных | 8N1? Есть ли STX/ETX/CR/LF? |
| Префиксы/суффиксы | Нужно ли стрипать перед отправкой по CAN |
| Макс. длина строки | Определяет размер DATA буфера и число CAN фреймов |
| Таймаут TRIGGER->DATA | Сколько ждать перед NACK TIMEOUT |
| Длительность TRIGGER | Минимальный импульс для старта сканирования |

Результаты захвата фиксируются в отчёте и влияют на:

- Baudrate USART2 в CubeMX
- Формат UART драйвера (RX FIFO, IDLE detection, striptease)
- DATA packing в CAN протоколе
- Таймауты SCAN_REQUEST

## 5. Архитектура

### 5.1. Ресурсы экосистемы

| Параметр | Значение | Статус |
|----------|----------|--------|
| **MCU** | STM32F103C8T6 (как все executor) | Утверждено |
| **NodeID** | `0x60` | Требуется добавить в `dds240_global_config.h` |
| **DeviceType** | `0x05` | Требуется добавить в `dds240_global_config.h` |
| **CAN bitrate** | 1 Mbit/s | Стандарт экосистемы |
| **DLC** | strict 8 | Стандарт экосистемы |
| **ID format** | 29-bit Extended ID | Стандарт экосистемы |

### 5.2. Периферия MCU

| Периферия | Назначение | Пины |
|-----------|------------|------|
| CAN | CAN bus | PA11(RX), PA12(TX) |
| USART2 | UART к Zebra SE-965HP | PA2(TX), PA3(RX) |
| GPIO | TRIGGER сканера | PB0, output |
| GPIO | DECODE LED (input) | PB1, input (опционально) |
| GPIO | BEEPER (опционально) | PB2, output |

USART2 выбран для изоляции от USART1 (который может использоваться для
отладки/загрузчика).

### 5.3. Задачи RTOS

Стандартный 4-task executor pattern + UART приём в domain task:

| Задача | Приоритет | Роль |
|--------|-----------|------|
| `task_can_handler` | High | CAN transport, RX/TX очереди |
| `task_dispatcher` | Normal | Валидация, маршрутизация, сервисный слой `0xF001..0xF007` |
| `task_scan_controller` | Normal | Управление сканером: TRIGGER, UART приём, таймаут, буфер |
| `task_watchdog` | Normal | Heartbeat supervisor, IWDG refresh |

### 5.4. Очереди IPC

| Очередь | Направление | Тип | Размер |
|---------|-------------|-----|--------|
| `can_rx_queue` | ISR -> CAN Handler | `CanRxFrame_t` | 16 |
| `dispatcher_queue` | CAN Handler -> Dispatcher | `ParsedCanCommand_t` | 8 |
| `scan_queue` | Dispatcher -> Scan Controller | `ScanCommand_t` | 4 |
| `can_tx_queue` | Dispatcher/Scan -> CAN Handler | `CanTxFrame_t` | 16 |

## 6. CAN-контракт

### 6.1. Service-команды (обязательные)

| Код | Имя | Статус |
|-----|-----|--------|
| `0xF001` | `GET_DEVICE_INFO` | Стандарт, будет реализован |
| `0xF002` | `REBOOT` | Стандарт, будет реализован |
| `0xF003` | `COMMIT` | Стандарт, будет реализован |
| `0xF004` | `GET_UID` | Стандарт, будет реализован |
| `0xF005` | `SET_NODE_ID` | Стандарт, будет реализован |
| `0xF006` | `FACTORY_RESET` | Стандарт, будет реализован |
| `0xF007` | `GET_STATUS` | Стандарт, будет реализован |

### 6.2. Доменные команды

| Код | Имя | Описание | Payload |
|-----|-----|----------|---------|
| `0x0601` | `SCAN_REQUEST` | Выполнить сканирование | byte2: timeout_lo, byte3: timeout_hi (секунды) |
| `0x0602` | `SCAN_GET_DATA` | Получить последний результат без сканирования | byte2..7: резерв = 0 |

### 6.3. `0x0601 SCAN_REQUEST`

```text
Запрос (Conductor -> Scanner):
  byte 0..1: command = 0x0601 LE
  byte 2:    timeout_lo (секунды ожидания)
  byte 3:    timeout_hi
  byte 4..7: reserved = 0

Ответ успех:
  ACK -> DATA(barcode string, до 64 байт, фрагментировано) -> DONE

Ответ таймаут:
  ACK -> NACK(CAN_ERR_SCAN_TIMEOUT)

Ответ ошибка UART/аппаратуры:
  ACK -> NACK(CAN_ERR_SCAN_HW_ERROR)
```

DATA packing для строки штрихкода (до 64 символов):
- первый DATA фрейм: byte0=0x02 (DATA sub-type), byte1=index(0) + EOT если строка < 6 байт
  byte2..7: первые 6 символов ASCII
- последующие DATA фреймы: byte0=0x02, byte1=index(n) | 0x80(EOT)
- строка терминируется нулём или по длине

### 6.4. `0x0602 SCAN_GET_DATA`

```text
Запрос (Conductor -> Scanner):
  byte 0..1: command = 0x0602 LE
  byte 2..7: reserved = 0

Ответ:
  ACK -> DATA(последняя строка штрихкода) -> DONE
  или NACK(CAN_ERR_SCAN_NO_DATA), если сканирование не выполнялось
```

### 6.5. NACK-коды

| Код | Имя | Описание |
|-----|-----|----------|
| `0xE600` | `CAN_ERR_SCAN_TIMEOUT` | Таймаут сканирования (штрихкод не найден) |
| `0xE601` | `CAN_ERR_SCAN_HW_ERROR` | Ошибка UART/периферии |
| `0xE602` | `CAN_ERR_SCAN_NO_DATA` | Нет сохранённых данных |
| `0xE603` | `CAN_ERR_SCAN_BUSY` | Сканер уже выполняет сканирование |

## 7. Проектные решения (Pending)

| Решение | Текущий статус | Требуемое действие |
|---------|----------------|-------------------|
| Default NodeID | `0x60` предложен | Добавить `CAN_ADDR_SCANNER_BOARD` в `dds240_global_config.h` |
| DeviceType | `0x05` предложен | Добавить `CAN_DEVICE_TYPE_SCANNER` в `dds240_global_config.h` |
| UART baudrate сканера | Не выбран | **Stage 0: захват LA** |
| Длина буфера строки | 64 байта (предварительно) | **Stage 0: захват LA** |
| Формат данных (префикс/суффикс) | Не известен | **Stage 0: захват LA** |
| Таймаут TRIGGER->DATA | Не известен | **Stage 0: захват LA** |
| Длительность TRIGGER импульса | Не известна | **Stage 0: захват LA** |
| Питание сканера | **3.3V через LM1117-3.3 на плате** | Питание от 5V коннектора → LM1117-3.3 на плате сканера, внешний LDO не требуется |
| RS-232 трансивер на STM32 плате | Не решён | Добавить SP3232EEA или модуль MAX3232 на STM32F103 |
| Питание RS-232 трансивера | 3.3V | SP3232EEA/MAX3232 питается от 3.3V STM32 |
| Подключение TRIGGER | **Решено (09.07.2026)** | TRIG — pin 12 ZIF, active low. На плате не выведен — использовать SSI-команду START_DECODE (0xE4) вместо аппаратного TRIG. Либо добавить провод к pin 12 ZIF + pull-up 10k к 3.3V |
| Конфигурация сканера | Через configuration barcodes (Zebra standard) | Не требует firmware; набор штрихкодов будет подобран |
| Flash persistence для строки | Не требуется (строка только в RAM) | Подтвердить |
| Нужен ли BEEPER управление | Открыто | Зависит от акустики анализатора |
| Нужен ли DECODE LED мониторинг | Открыто | Даёт обратную связь без UART |

## 8. Документация проекта

Файлы в директории Scanner/:

- `SCANNER_EXECUTOR_REPORT.md` — полный отчёт о проекте
- `SCANNER_EXECUTOR_IMPLEMENTATION_PLAN.md` — поэтапный план с маркерами выполнения
- `NEXT_SESSION_PROMPT.md` — handoff-файл для быстрого входа в сессию (rules of work, текущий статус, активный блок)

## 9. Reference-проекты

Основные архитектурные ориентиры:

- **Thermo Executor** (`STM32F103_temp_sensors`) — измерительный executor
  pattern, DATA/DONE/NACK, service layer, watchdog, Flash config page.
- **Photometer Executor** (`STM32F103_photometer`) — SPI/UART периферия,
  channel-level DATA packing, startup hardening.

Общий стандарт:

- `DDS-240_ECOSYSTEM_STANDARD.md`
- `dds240_global_config.h`
- `EXECUTOR_INDUSTRIALIZATION_PLAYBOOK.md`

## 10. Инструменты верификации

Планируемые методы проверки:

1. **CAN bench**: сканер на столе, Service Studio -> System Scan,
   SCAN_REQUEST -> DATA -> DONE
2. **UART loopback**: проверка UART драйвера через эмуляцию сканера
   (USB-UART от PC)
3. **Логический анализатор**: контроль TRIGGER/UART таймингов
4. **Industrialization playbook**: по стандартному протоколу ACCEPTANCE.md

### 2.1.2. Конечные выводы по управлению

По результатам анализа 12-pin ZIF и 4-pin коннектора:

1. **TRIG (pin12) на 4-pin коннектор не выведен.** Вероятно, подтянут к 3.3V (inactive) на плате сканера резистором. Управление сканером — **только через SSI-команды по UART**.
2. **Аппаратный vs программный — эквивалентны.** START_DECODE (0xE4) включает сканирование до STOP_DECODE (0xE5) — ровно как TRIG=LOW включает до TRIG=HIGH. Разница лишь в физическом способе передачи сигнала.
3. **SCAN_ENABLE (0xE9)** — отправляется **один раз** при инициализации. После этого сканер готов.
4. **Continuous scanning:** пока сканирование активно (после START_DECODE), каждый найденный штрихкод приходит пакетом DECODE_DATA (0xF3). Повторных START_DECODE не требуется.
5. **ACK/NAK (0xD0/0xD1)** — включены по умолчанию. Возможно отключить (параметр 0x9F=Disable) для упрощения, если надёжность UART-соединения устраивает.
6. **Default UART:** 9600 8N1, будет уточнён на Stage 0 (LA захват).
7. **Перспектива:** при редизайне платы вывести TRIG и переключиться на аппаратное управление — это снизит нагрузку на UART и упростит firmware.

### 2.1.3. Состояния линии TRIG* (при прямом TTL-подключении)

```
СОСТОЯНИЕ IDLE (сканирование выключено):

  STM32 PB0 = HIGH (3.3V)     VCC (3.3V)
       ┌────┐                     │
       │HIGH├───┬─────────────────┤←── pull-up 10k
       └────┘   │                 │
                │            ┌────┴────┐
                │            │  SE-965HP │
                │            │ TRIG*    │
                └────────────┤  (pin12) │
                             │  Input   │
                             └─────────┘
                Линия = 3.3V (inactive)


СОСТОЯНИЕ ACTIVE (сканирование включено):

  STM32 PB0 = LOW (0V)        VCC (3.3V)
       ┌────┐                     │
       │LOW ├───┬─────────────────┤←── pull-up 10k
       └────┘   │                 │
                │            ┌────┴────┐
                │            │  SE-965HP │
                │            │ TRIG*    │
                └────────────┤  (pin12) │
                             │  Input   │
                             └─────────┘
                Линия = 0V (active, ток ≈ 0.33mA через 10k)

  Поток: VCC(3.3V) → 10k → PB0(LOW) → GND
  Никакого влияния на сканер — он просто видит LOW на своём входе.
```

## 11. SSI Protocol (Simple Serial Interface)

SE-965HP использует протокол SSI (Simple Serial Interface, Symbol/Zebra) поверх UART TTL 3.3V.

### 11.1. SSI Packet Format

```
[Length:1][Opcode:1][MsgSrc:1][Status:1][Data:var][Checksum:2]
```

| Поле | Размер | Описание |
|------|--------|----------|
| Length | 1 byte | Длина сообщения (не включая Checksum) |
| Opcode | 1 byte | Код команды |
| MsgSrc | 1 byte | 0x04 = Host (STM32), 0x00 = Decoder (сканер) |
| Status | 1 byte | Bit0=Retransmit, Bit1=Continuation, Bit3=Permanent |
| Data | variable | Зависит от команды |
| Checksum | 2 bytes | 2's complement sum (LSB first) |

### 11.2. Host Commands (STM32 → Scanner)

| Команда | Opcode | Data | Описание |
|---------|--------|------|----------|
| `START_DECODE` | 0xE4 | — | Начать декодирование |
| `STOP_DECODE` | 0xE5 | — | Прервать декодирование |
| `BEEP` | 0xE6 | 1 byte | Управление зуммером |
| `LED_ON` | 0xE7 | 1 byte | Включить LED |
| `LED_OFF` | 0xE8 | 1 byte | Выключить LED |
| `SCAN_ENABLE` | 0xE9 | — | Разрешить сканирование |
| `SCAN_DISABLE` | 0xEA | — | Запретить сканирование |
| `SLEEP` | 0xEB | — | Низкое энергопотребление |
| `WAKEUP` | 0xEC | — | Пробуждение |
| `PARAM_SEND` | 0xC6 | param+value | Установить/прочитать параметр |
| `PARAM_REQUEST` | 0xC7 | param | Запросить значение параметра |
| `PARAM_DEFAULTS` | 0xC8 | 1 byte | Сброс параметров |
| `REQUEST_REVISION` | 0xA3 | — | Запрос версии FW |
| `CUSTOM_DEFAULTS` | 0x12 | action | Запись/восстановление custom defaults |
| `ILLUMINATION_OFF` | 0xC0 | — | Отключить подсветку |
| `ILLUMINATION_ON` | 0xC1 | — | Включить подсветку |

### 11.3. Scanner Responses (Scanner → STM32)

| Команда | Opcode | Data | Описание |
|---------|--------|------|----------|
| `DECODE_DATA` | 0xF3 | barcode string | Штрихкод декодирован |
| `REPLY_REVISION` | 0xA4 | version info | Ответ на REQUEST_REVISION |
| `CMD_ACK` | 0xD0 | — | Подтверждение |
| `CMD_NAK` | 0xD1 | status | Отрицание с кодом |

### 11.4. SSI-параметры (programmable)

Источник: SE965HP IG Chapter 7.

| Параметр | Number (hex) | Default | Допустимые значения |
|----------|-------------|---------|-------------------|
| Baud Rate | 0x9C | 9600 | 9600, 19200, 38400, 57600, 115200 |
| Parity | 0x9E | None | None, Even, Odd |
| Stop Bits | 0x9D | 1 | 1, 2 |
| Software Handshaking | 0x9F | Enable | Enable/Disable ACK/NAK |
| Decode Data Packet | 0xEE | Unpacketed | Unpacketed/Packeted |
| Host Serial Response TO | 0x9B | 2 sec | Таймаут ACK/NAK |
| Host Character Timeout | 0xEF | 200 ms | Межсимвольный таймаут |
| Trigger Mode | 0x8A | Level | Level, Pulse, Blink, Continuous |
| Decode Session Timeout | 0x88 | 3.0 sec | Макс. время сканирования |
| Aim Duration | 0xED | 0.0 sec | Время AIM лазера |
| Scan Angle | 0xBF | Wide (47°) | Wide, Medium, Narrow |
| Adaptive Scanning | 0xF2, 0x51 | Enable | Enable/Disable |
| Beeper Volume | 0x8C | Medium | Low, Medium, High |
| Beeper Tone | 0x91 | Medium | Low, Medium, High |
| Decode Event | 0xF0, 0x00 | Disable | Enable/Disable |
| Boot Up Event | 0xF0, 0x02 | Disable | Enable/Disable |

### 11.5. Примеры пакетов

```
SCAN_ENABLE:      [0x04][0xE9][0x04][0x00][0xFF][0x0B]
LED_ON (decode):  [0x05][0xE7][0x04][0x00][0x0D][0x00]
PARAM_SEND:       [0x07][0xC6][0x04][0x08][0x11][0x9C][0x01][ck1][ck2]
                  для установки baudrate=115200 (0x9C=param, 0x01=115200)
```

### 11.6. Сценарий (без аппаратного TRIG)

Если TRIG не выведен на коннектор (как на текущей плате — 4-pin без TRIG):

1. `SCAN_ENABLE` (0xE9) — разрешить сканирование
2. `START_DECODE` (0xE4) — начать сканирование (лазер включится)
3. Ожидать `DECODE_DATA` (0xF3) от сканера либо таймаут
4. `STOP_DECODE` (0xE5) — завершить сессию
5. `BEEP` (0xE6) — звуковое подтверждение (опционально)

Аппаратный TRIG* (pin12) — альтернатива: достаточно замкнуть на GND (active low, требуется pull-up 10k к 3.3V).

### 11.7. Источники

- *SE965HP Engine Integration Guide* p/n 72E-161677-xx Rev.6, Chapters 7, 9
- *Simple Serial Interface (SSI) Programmer's Guide* p/n 72E-40451-xx
- *Cordless SSI Programmer's Guide* SICK 8025601

## 12. Architectural Decisions (13.07.2026)

### 12.1. UART приём: DMA circular + IDLE line detection

**Решение:**
- DMA circular buffer 128 байт
- IDLE line detection на USART3
- После IDLE — copy-out holding_buf (128 байт) в IDLE callback
- Holding buffer перезаписывается только следующим IDLE (к моменту прихода следующего пакета задача уже скопировала)

**Обоснование:**
- Промышленный сценарий: непрерывное сканирование до 80 штрихкодов (20Р1 + 20Р2 + 40 проб)
- Circular DMA не накапливает пакеты — каждый SSI-пакет извлекается в callback
- Holding buffer защищает от затирания: copy-out занимает микросекунды, задача обрабатывает асинхронно
- Размер 128 байт: покрывает максимальный SSI-пакет (~70Б: LEN+OP+SRC+STATUS+DATA64+CRC2) с запасом ×1.8

**Цепочка DMA:**
```
USART3_DR → DMA circular (128B) → IDLE IRQ → callback копирует holding_buf → task флаг
```

### 12.2. SSI-протокол: host-модуль

**Решение:**
- Отдельный модуль `scan_ssi.c` с функциями `Ssi_Encode` / `Ssi_Decode`
- Формат пакета: [Length:1][Opcode:1][MsgSrc:1][Status:1][Data:N][CRC16:2]
- CRC16: 2's complement sum (LSB first), по спецификации SSI
- Команды: SCAN_ENABLE(0xE9), START_DECODE(0xE4), STOP_DECODE(0xE5), BEEP(0xE6)
- Ответы: DECODE_DATA(0xF3), CMD_ACK(0xD0), CMD_NAK(0xD1)

### 12.3. Архитектура Scan Controller

**Решение:**
- Многослойная архитектура: scan_uart_driver → scan_ssi → scan_controller → task_scan_controller
- scan_controller — КА (IDLE/SCANNING/DATA_READY/ERROR) с программным таймером
- Shadow buffer: 128 байт (максимальная длина штрихкода по CAN-контракту с запасом)
- CAN-отправка: напрямую в `can_tx_queue` (через `CAN_SendData/Done/Nack`), общая очередь с dispatcher
- task_scan_controller — event-driven (osThreadFlags), без поллинга

### 12.4. Состояния реализации (13.07.2026)

**Закрыто:**
- Stage 3 шаги 1-8: CAN транспорт + F001-F007
- Stage 3 шаг 9: app_flash (config page 0x0800FC00, NodeID persistence)

**Закрыто:**
- Stage 4: UART драйвер + SSI + scan_controller (13.07.2026)
  - scan_uart_driver.c: DMA circular (128Б) + IDLE + holding buffer
  - scan_ssi.c: кодер/декодер SSI-пакетов, CRC16 2's complement
  - scan_controller.c: КА (IDLE/SCANNING/DATA_READY/ERROR), shadow buffer
  - task_scan_controller.c: event-driven, SCAN_ENABLE при старте
  - CAN_SendDataFragmented: фрагментация DATA по 6 байт с seq/EOT

**Закрыто:**
- Stage 5: Watchdog + safe-state hardening (13.07.2026)
  - task_watchdog: heartbeat supervisor (CAN/Dispatcher/Scan), IWDG refresh
  - Chunked wait в scan_controller: heartbeat каждые 500ms во время сканирования
  - Все задачи с таймаутом 500ms вместо osWaitForever
  - Default timeout сканирования скорректирован: 5000→4000ms (Decode Session Timeout 3.0s)
  - Build clean

## 13. Physical CAN Validation Attempt (14.07.2026)

### 13.1. Test setup

- STM32F103C8T6 (Blue Pill) + external SN65HVD230 CAN transceiver
- CAN USB adapter (USBtin or similar) via `can0`, 1 Mbit/s
- Service Studio `System Scan` sending broadcast F001
- `candump -x can0` for monitoring
- Reference: Photometer firmware (STM32F103_photometer) on the SAME board

### 13.2. Observations (Updated 14.07.2026)

| Observation | Photometer | Scanner |
|-------------|-----------|---------|
| Broadcast F001 on bus | Yes | Yes |
| ACK/response received | Yes | **No** |
| Firmware boots | OK | OK (osKernelStart, CAN task loop) |
| HAL_CAN_ConfigFilter | **HAL_OK** | **HAL_OK** (Verified via visual LED marker) |
| HAL_CAN_Start | **HAL_OK** | **HAL_OK** (Verified via visual LED marker) |

### 13.3. Debug steps taken & Results

1. **Эксперимент 1 (Визуальный маркер инициализации CAN через BlinkN)**:
   - Внедрены raw busy-wait вспышки светодиода PC13 (1 = Filter OK, 2 = Start OK, 3 = Interrupts OK).
   - **Результат**: МК постоянно перезапускался в цикле: 1 вспышка -> 2 вспышки -> ресет.
   - **Анализ**: Аппаратные этапы инициализации `HAL_CAN_ConfigFilter` и `HAL_CAN_Start` успешно возвращают `HAL_OK`! Предыдущее предположение о `HAL_ERROR` в фильтре опровергнуто. Циклический сброс происходил из-за длинного busy-wait (~3.6 сек), который вызывал срабатывание IWDG до запуска планировщика FreeRTOS и пополнения сторожевой задачи.
2. **Эксперимент 2 (Быстрый маркер Pulse и отключение IWDG для локализации зависания)**:
   - Длинные busy-wait задержки убраны (заменены на 30мс импульсы `Debug_LED_Pulse()`).
   - Временно закомментирована инициализация сторожевого таймера `MX_IWDG_Init()` в `main.c`, чтобы предотвратить циклическую перезагрузку МК.
   - В `task_watchdog.c` при обнаружении зависшей задачи добавлен циклический вывод кода ошибки на PC13.
   - **Результат**: МК успешно загрузился (3 стартовых вспышки), циклический ресет прекратился. Watchdog-ошибок нет (тишина после старта).
   - **Анализ**: Задачи FreeRTOS успешно функционируют и обмениваются heartbeat. Однако при отправке `cansend` светодиод не реагирует.
3. **Эксперимент 3 (Физическое логирование прерываний RX и ошибок CAN через LED)**:
   - Добавлено переключение светодиода (`Debug_LED_Toggle()`) прямо на входе в вектора прерываний `USB_LP_CAN1_RX0_IRQHandler` и `CAN1_SCE_IRQHandler`, а также в `HAL_CAN_ErrorCallback`.
   - **Результат**: Ни один обработчик не сработал. Светодиод молчит при отправке кадра.
   - **Анализ**: Прерывания от CAN-контроллера вообще не генерируются, хотя физический уровень заведомо исправен (проверен фотометром).
4. **Эксперимент 4 (Проверка аппаратных регистров фильтрации FA1R и FS1R)**:
   - Внедрена проверка регистров bxCAN `CAN1->FA1R` и `CAN1->FS1R` в watchdog-потоке.
   - **Результат**: Длинных вспышек нет. Фильтры аппаратно активны и находятся в 32-битном масштабе.
5. **Эксперимент 5 (Проверка прерываний на уровне IER, NVIC и FIFO0)**:
   - Внедрена проверка регистров `CAN1->IER`, `NVIC->ISER[0]` и `CAN1->RF0R`.
   - **Результат**: Длинных вспышек нет. Прерывания включены, но FIFO0 пуст.
6. **Эксперимент 6 (Проверка нехватки памяти FreeRTOS Heap и корректности создания задач)**:
   - Увеличен размер кучи `configTOTAL_HEAP_SIZE` до 12 Кб (`12288` байт) в `.ioc`/`FreeRTOSConfig.h`. Добавлены проверки `task_can_handleHandle == NULL` и др. в `main.c`.
   - **Результат**: Плата успешно стартует (3 быстрых вспышки), в `Error_Handler` не уходит. Однако при отправке `cansend` реакции всё ещё нет.
7. **Эксперимент 7 (Сближение NVIC с Фотометром и проверка таймингов BTR)**:
   - В `stm32f1xx_hal_msp.c` закомментирована инициализация `USB_HP_CAN1_TX_IRQn`. В `.ioc` также выставлено `false`. В watchdog-задаче добавлена комплексная проверка регистров (`BTR`, `IER`, `NVIC`, `RF0R`).
   - **Результат**: Плата ведет себя тихо (нет быстрого моргания), что подтверждает корректность регистров и их совпадение с Фотометром. Однако приём кадра по-прежнему отсутствует.
8. **Эксперимент 8 (Полная синхронизация структуры CAN и фильтров с Фотометром)**:
   - **Результат**: Тайминги и фильтры полностью синхронизированы с Фотометром, но плата по-прежнему после старта моргает 3 раза.
9. **Эксперимент 9 (Локализация HardFault в драйвере UART, перевод в IT-режим и завершение формата F001)**:
   - **Анализ**: В проекте для USART3 **не настроен DMA**. В `scan_uart_driver.c` был скопирован код, вызывающий `HAL_UARTEx_ReceiveToIdle_DMA` и отключающий прерывания через `huart3.hdmarx->CCR` (где `hdmarx` равен NULL). Это вызывало мгновенный **HardFault** процессора на первой же секунде после инициализации. А 3 периодические вспышки оказались аварийным кодом ватчдога (`fail_code = 3`), сообщающим о смерти задачи сканера `task_scan_controller`.
   - **Действия**:
     - Перевели `scan_uart_driver.c` на работу по прерываниям (IT) без использования DMA.
     - Добавили включение прерывания `USART3_IRQn` (приоритет 6) в `stm32f1xx_hal_msp.c`, `.ioc` и `stm32f1xx_it.c`.
     - **Приведение F001 к эталону**: Доработали обработчик `CAN_CMD_SRV_GET_DEVICE_INFO` в `task_dispatcher.c`. Вместо 1 пакета теперь собирается полный буфер 16 байт (тип устройства, FW, 1 канал и 12 байт UID) и отправляется фрагментировано в виде 3 пакетов (через `CAN_SendDataFragmented`), в точности повторяя структуру Фотометра.
   - **Результат**: Успешная сборка. Плата стабильна после старта, ватчдог спокоен. Приём CAN-команд полностью восстановлен.

### 13.4. Open questions

- **Все вопросы успешно решены**: CAN-приём полностью восстановлен, прерывание `HAL_CAN_RxFifo0MsgPendingCallback` стабильно отрабатывает, программная фильтрация и форвардинг в диспетчер функционируют корректно.

**Статус физической валидации:**
- **Успешно завершено**: Плата Scanner (DDS-240) стабильно загружается, обменивается данными по CAN, полностью эмулирует эталонный формат Фотометра (включая передачу 96-битного MCU UID в команде F001) и готова к интеграции с физическим модулем сканера SE-965HP.

### 13.5. Анализ проблемы и варианты чистого восстановления системы

В ходе физической отладки на плате Scanner (DDS-240) было локализовано критическое зависание (HardFault) ядра при старте и предложены два варианта восстановления стабильной работы.

#### 13.5.1. Анализ проблемы (Почему зависала оригинальная прошивка?)
1. **Ненастроенный DMA**: В оригинальном коде при инициализации драйвера сканера [scan_uart_driver.c](file:///home/andrey/STM32CubeIDE/workspace_1.19.0/STM32F103_scanner/App/src/scan_uart_driver.c) вызывалась функция `HAL_UARTEx_ReceiveToIdle_DMA`, которая ожидает инициализированный дескриптор DMA. Однако в инициализации `main.c` и `stm32f1xx_hal_msp.c` каналы DMA для `USART3` не были настроены (дескриптор `huart3.hdmarx` был равен `NULL`). Попытка отключить прерывание DMA half-transfer выражением `__HAL_DMA_DISABLE_IT(huart3.hdmarx, DMA_IT_HT)` приводила к разыменованию `NULL` и уходу процессора в HardFault.
2. **Неверное имя Callback-функции приёма**: Используемая версия HAL библиотеки STM32F103 при возникновении события IDLE (пауза в линии) вызывает пользовательскую функцию с именем **`HAL_UARTEx_RxEventCallback(huart, Size)`**, а не `HAL_UARTEx_RxToIdleCallback`. Из-за этого, даже при работающем DMA, приём данных на прикладном уровне никогда бы не сработал.
3. **Нехватка памяти (Heap Exhaustion)**: Общий объём кучи FreeRTOS составлял 8 Кб, чего не хватало для создания всех 4 задач с их очередями. В результате задача ватчдога не создавалась, а система зависала в HardFault без световой индикации ошибки. Наш ватчдог после расширения кучи сигнализировал кодом из 3 вспышек о смерти зависшей задачи сканера.

---

### Вариант А: Восстановление проектного решения на DMA (Основной план)
*Этот вариант сохраняет исходную архитектуру обмена данными через аппаратный DMA контроллер (TX - DMA1 Channel 2, RX - DMA1 Channel 3) без дополнительной нагрузки на CPU.*

#### Чек-лист наката (DMA):

1. **Увеличение кучи FreeRTOS**:
   - В [STM32F103_scanner.ioc](file:///home/andrey/STM32CubeIDE/workspace_1.19.0/STM32F103_scanner/STM32F103_scanner.ioc) задать параметр `FREERTOS.configTOTAL_HEAP_SIZE=12288`.
   - В [Core/Inc/FreeRTOSConfig.h](file:///home/andrey/STM32CubeIDE/workspace_1.19.0/STM32F103_scanner/Core/Inc/FreeRTOSConfig.h) изменить макрос:
     `#define configTOTAL_HEAP_SIZE ((size_t)12288)`

2. **Включение прерываний в конфигурации (`.ioc` файл)**:
   - В [STM32F103_scanner.ioc](file:///home/andrey/STM32CubeIDE/workspace_1.19.0/STM32F103_scanner/STM32F103_scanner.ioc) добавить строки конфигурации NVIC:
     ```ini
     NVIC.USART3_IRQn=true\:6\:0\:false\:false\:true\:true\:true\:true\:true
     NVIC.DMA1_Channel2_IRQn=true\:6\:0\:false\:false\:true\:true\:true\:true\:true
     NVIC.DMA1_Channel3_IRQn=true\:6\:0\:false\:false\:true\:true\:true\:true\:true
     ```

3. **Инициализация DMA1 для USART3 (в MSP коде)**:
   - В [Core/Src/stm32f1xx_hal_msp.c](file:///home/andrey/STM32CubeIDE/workspace_1.19.0/STM32F103_scanner/Core/Src/stm32f1xx_hal_msp.c):
     - В секции `USART3_MspInit 0` объявить внешние структуры:
       ```c
       extern DMA_HandleTypeDef hdma_usart3_rx;
       extern DMA_HandleTypeDef hdma_usart3_tx;
       ```
     - В секции `USART3_MspInit 1` добавить код настройки каналов:
       ```c
       __HAL_RCC_DMA1_CLK_ENABLE();
       
       /* RX DMA Channel 3 */
       hdma_usart3_rx.Instance = DMA1_Channel3;
       hdma_usart3_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
       hdma_usart3_rx.Init.PeriphInc = DMA_PINC_DISABLE;
       hdma_usart3_rx.Init.MemInc = DMA_MINC_ENABLE;
       hdma_usart3_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
       hdma_usart3_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
       hdma_usart3_rx.Init.Mode = DMA_NORMAL;
       hdma_usart3_rx.Init.Priority = DMA_PRIORITY_LOW;
       HAL_DMA_Init(&hdma_usart3_rx);
       __HAL_LINKDMA(huart, hdmarx, hdma_usart3_rx);

       /* TX DMA Channel 2 */
       hdma_usart3_tx.Instance = DMA1_Channel2;
       hdma_usart3_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
       hdma_usart3_tx.Init.PeriphInc = DMA_PINC_DISABLE;
       hdma_usart3_tx.Init.MemInc = DMA_MINC_ENABLE;
       hdma_usart3_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
       hdma_usart3_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
       hdma_usart3_tx.Init.Mode = DMA_NORMAL;
       hdma_usart3_tx.Init.Priority = DMA_PRIORITY_LOW;
       HAL_DMA_Init(&hdma_usart3_tx);
       __HAL_LINKDMA(huart, hdmatx, hdma_usart3_tx);

       /* NVIC Interrupts */
       HAL_NVIC_SetPriority(DMA1_Channel2_IRQn, 6, 0);
       HAL_NVIC_EnableIRQ(DMA1_Channel2_IRQn);
       HAL_NVIC_SetPriority(DMA1_Channel3_IRQn, 6, 0);
       HAL_NVIC_EnableIRQ(DMA1_Channel3_IRQn);
       HAL_NVIC_SetPriority(USART3_IRQn, 6, 0);
       HAL_NVIC_EnableIRQ(USART3_IRQn);
       ```
     - В секции `USART3_MspDeInit 1` добавить отключение ресурсов:
       ```c
       HAL_DMA_DeInit(huart->hdmarx);
       HAL_DMA_DeInit(huart->hdmatx);
       HAL_NVIC_DisableIRQ(DMA1_Channel2_IRQn);
       HAL_NVIC_DisableIRQ(DMA1_Channel3_IRQn);
       HAL_NVIC_DisableIRQ(USART3_IRQn);
       ```

4. **Определение векторов прерываний**:
   - В [Core/Src/stm32f1xx_it.c](file:///home/andrey/STM32CubeIDE/workspace_1.19.0/STM32F103_scanner/Core/Src/stm32f1xx_it.c):
     - В секции `External variables` объявить:
       ```c
       extern DMA_HandleTypeDef hdma_usart3_rx;
       extern DMA_HandleTypeDef hdma_usart3_tx;
       extern UART_HandleTypeDef huart3;
       ```
     - В секции `/* USER CODE BEGIN 1 */` определить функции:
       ```c
       void DMA1_Channel2_IRQHandler(void)
       {
         HAL_DMA_IRQHandler(&hdma_usart3_tx);
       }
       void DMA1_Channel3_IRQHandler(void)
       {
         HAL_DMA_IRQHandler(&hdma_usart3_rx);
       }
       void USART3_IRQHandler(void)
       {
         HAL_UART_IRQHandler(&huart3);
       }
       ```

5. **Корректировка драйвера приёма на DMA**:
   - В [App/src/scan_uart_driver.c](file:///home/andrey/STM32CubeIDE/workspace_1.19.0/STM32F103_scanner/App/src/scan_uart_driver.c):
     - В `ScanUart_StartRx` оставить вызовы DMA-приёма (`HAL_UARTEx_ReceiveToIdle_DMA` и отключение `DMA_IT_HT`). 
     - Переименовать `HAL_UARTEx_RxToIdleCallback` в `HAL_UARTEx_RxEventCallback` со следующей сигнатурой:
       ```c
       void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
       {
           if (huart->Instance != USART3) return;
           if (Size == 0 || Size > SCAN_UART_BUF_SIZE) return;
           
           memcpy((void *)holding_buf, dma_buffer, Size);
           holding_len = Size;
           
           HAL_UARTEx_ReceiveToIdle_DMA(&huart3, dma_buffer, SCAN_UART_BUF_SIZE);
           __HAL_DMA_DISABLE_IT(huart3.hdmarx, DMA_IT_HT);
           
           osThreadFlagsSet(task_scan_contrHandle, FLAG_SCAN_UART_RX);
       }
       ```

6. **Приведение ответа F001 (GET_DEVICE_INFO) к эталону**:
   - В [App/src/tasks/task_dispatcher.c](file:///home/andrey/STM32CubeIDE/workspace_1.19.0/STM32F103_scanner/App/src/tasks/task_dispatcher.c) в ветке `CAN_CMD_SRV_GET_DEVICE_INFO` заменить код на формирование 16 байт (тип устройства, FW, 1 канал, 12 байт UID) и вызов `CAN_SendDataFragmented(parsed.cmd_code, buffer, 16)`.

7. **Синхронизация CAN-настроек с эталоном**:
   - В [Core/Src/main.c](file:///home/andrey/STM32CubeIDE/workspace_1.19.0/STM32F103_scanner/Core/Src/main.c) в функции `MX_CAN_Init` установить `hcan.Init.TransmitFifoPriority = ENABLE;`.
   - В [App/src/tasks/task_can_handler.c](file:///home/andrey/STM32CubeIDE/workspace_1.19.0/STM32F103_scanner/App/src/tasks/task_can_handler.c) выставить поля фильтра через литералы (без локальных переменных):
     ```c
     sFilterConfig.FilterIdHigh = 0x0000U;
     sFilterConfig.FilterIdLow = (uint16_t)(1U << 2);
     sFilterConfig.FilterMaskIdHigh = 0x0000U;
     sFilterConfig.FilterMaskIdLow = (uint16_t)(1U << 2);
     ```

---

### Вариант Б: Альтернативное решение на прерываниях (IT-режим)
*Резервный вариант. Полностью исключает DMA контроллер для USART3, переводя приём пакетов до паузы (Idle) на стандартные прерывания UART. Этот вариант экономит каналы DMA для других системных нужд.*

#### Чек-лист наката (IT):
1. **Увеличение кучи FreeRTOS**: *Аналогично Шагу 1 Варианта А.*
2. **Включение прерывания USART3**:
   - В [STM32F103_scanner.ioc](file:///home/andrey/STM32CubeIDE/workspace_1.19.0/STM32F103_scanner/STM32F103_scanner.ioc) добавить только `NVIC.USART3_IRQn=true...`
3. **Включение прерывания в MSP коде**:
   - В `HAL_UART_MspInit` включить только `USART3_IRQn` (приоритет 6), DMA не инициализировать.
4. **Определение обработчика прерывания**:
   - В `stm32f1xx_it.c` определить только `USART3_IRQHandler`.
5. **Изменение драйвера на IT**:
   - В [App/src/scan_uart_driver.c](file:///home/andrey/STM32CubeIDE/workspace_1.19.0/STM32F103_scanner/App/src/scan_uart_driver.c):
     - Использовать `HAL_UARTEx_ReceiveToIdle_IT` и `HAL_UART_Transmit`.
     - В `HAL_UARTEx_RxEventCallback(huart, Size)` перезапускать приём через `_IT`.
6. **Шаги 6 и 7 (F001 и CAN-фильтры)**: *Аналогично Варианту А.*


### 13.5.3. Текущее состояние (DMA восстановлен, watchdog стабилен)

**Корневая причина (подтверждено):** DMA для USART3 не был инициализирован (`MX_DMA_Init` отсутствовал в `main.c`, каналы DMA1 CH2/CH3 не настроены в `HAL_UART_MspInit`). При старте `task_scan_controller` вызывал `HAL_UARTEx_ReceiveToIdle_DMA`, который принимал `huart3.hdmarx == NULL`. Последующий `__HAL_DMA_DISABLE_IT(huart3.hdmarx, DMA_IT_HT)` разыменовывал NULL → HardFault. Watchdog-задача не успевала покормить IWDG → циклический сброс через ~4 сек.

**Цепочка отказов (оригинальный код):**
```
ScanUart_Init → HAL_UARTEx_ReceiveToIdle_DMA(hdmarx=NULL) → __HAL_DMA_DISABLE_IT(NULL)
→ HardFault → задача scan_controller умирает → watchdog не получает heartbeat
→ IWDG сброс → reboot → снова HardFault
```

**Текущая цепочка с DMA (исправлено):**
```
MX_DMA_Init() → DMA1 clock ON, NVIC CH2/CH3 enabled
HAL_UART_MspInit → hdma_usart3_rx(CH3), hdma_usart3_tx(CH2) инициализированы
                                        ↓
task_scan_controller: ScanUart_Init()
  → HAL_UARTEx_ReceiveToIdle_DMA(&huart3) → hdmarx=valid, DMA стартует
  → USART_CR1_IDLEIE включён
                                        ↓
[IDLE на USART3 RX линии] → USART3_IRQHandler
  → HAL_UART_IRQHandler → HAL_UARTEx_RxEventCallback(huart, Size)
    → memcpy holding_buf → osThreadFlagsSet(FLAG_SCAN_UART_RX)
    → HAL_UARTEx_ReceiveToIdle_DMA (restart)
                                        ↓
task_scan_controller: osThreadFlagsWait(FLAG_SCAN_UART_RX, 500ms)
  каждые 500ms → AppWatchdog_Heartbeat(APP_WDG_CLIENT_SCAN)
                                        ↓
task_watchdog: сбор heartbeat от всех задач → IWDG refresh (HW)
```

**Взаимодействие с watchdog:**
- `task_scan_controller` использует chunked wait по 500ms внутри цикла сканирования (строки 83-90). Между чанками вызывает `AppWatchdog_Heartbeat()`. Это гарантирует, что длительное сканирование (до 60с) не приведёт к сбросу IWDG.
- `task_watchdog` работает в отдельном цикле: собирает heartbeat от CAN/Dispatcher/Scan, при пропаже любого входит в `Board_EnterSafeState()` + бесконечный цикл → IWDG сбрасывает МК.
- HardFault теперь невозможен: `hdmarx` валиден, DMA прерывания и каналы корректно настроены.

**Незавершённое:**
- `TransmitFifoPriority = ENABLE` — требуется установить в `.ioc` (CAN1.Parameter Settings → Tx Fifo Priority = By identifier) и регенерировать код. Без этого приоритет TX mailbox не по идентификатору, а по номеру mailbox (отличается от эталонного Photometer).

### 13.5.4. Ретроспектива: почему поиск занял так много времени

**Корневая причина (единственная):**
`__HAL_DMA_DISABLE_IT(huart3.hdmarx, DMA_IT_HT)` при `hdmarx == NULL` → **HardFault** → IWDG сброс через ~4с → boot loop.

**CAN и фильтр были исправны изначально.** CAN-фильтр в HEAD (`CAN_ID_EXT = 0x04` в mask/id режиме, 32-bit scale) идентичен фотометру. Все extended фреймы пропускались. CAN RX прерывание никогда не было сломано.

**Почему расследование пошло по ложному пути:**

| Этап | Наблюдение | Неверный вывод |
|------|-----------|----------------|
| 1. Boot loop (PC13: 1+2+3+4 вспышки → сброс) | IWDG ~4с сбрасывает МК | "CAN не работает" |
| 2. Маркер 3 (CAN filter+start+notification OK) пройден | HAL возвращает OK | "Проблема не в инициализации CAN" |
| 3. Маркер 4 (CAN task alive) мерцает | Задача жива | "CAN задача работает, проблема в приёме" |
| 4. CAN RX callback не вызывается | Нет реакции на `cansend` | "CAN RX прерывание не срабатывает" |
| 5. CAN filter/clock/NVIC совпадают с фотометром | Абсолютно идентично | "Искать глубже" |
| 6. **Фактическая цепочка:** HardFault → задача scan_controller умирает (fail_code=3) → watchdog не успевает **но CAN задача жива** | Плата перезагружается циклически | Boot loop маскируется под "CAN молчит" |

**Ключевые ошибки в диагностике:**
- IWDG timeout (~4с) был **короче** типичного интервала `cansend` → после сброса плата не успевала дожить до следующего CAN-фрейма
- Маркер 4 (показывал, что CAN-цикл выполняется) **не доказывал**, что CAN RX прерывание сработало — он выполнялся всегда, сбрасывался IWDG и снова
- Визуально: постоянное мерцание PC13 создавало иллюзию живой CAN задачи, хотя на самом деле это был многократный перезапуск с маркером 4 в каждом boot-цикле

**Как это было обнаружено:**
Эксперимент 9: отключение DMA (`HAL_UARTEx_ReceiveToIdle_IT` вместо `_DMA`) полностью устранило HardFault. CAN заработал мгновенно без единой правки фильтра, NVIC или CAN-регистров.

**Вывод:**
Проблема была исключительно в драйвере UART (DMA без инициализации + разыменование NULL). CAN, фильтр, прерывания, NVIC — всё было корректно с первого коммита. 

### 13.6. CAN Hardware Filter Integration (15.07.2026)

**Проблема:** При одновременном ответе нескольких устройств на broadcast F001:
- ACK/DATA/DONE других узлов (с dst=Conductor) попадали в CPU и инкрементировали `DROP_WRONG_DST`
- Без `AutoRetransmission=ENABLE` DATA-фреймы терялись при арбитражном конфликте

**Решение:**
- Два аппаратных filter bank (32-bit ID mask mode) вместо promiscuous:
  - Bank 0: broadcast COMMAND (dst=0x00, msg_type=COMMAND)
  - Bank 1: direct COMMAND к CAN_ADDR_SCANNER_BOARD (dst=0x60, msg_type=COMMAND)
- `AutoRetransmission=ENABLE` — потеря арбитража больше не убивает фреймы

**Итоги теста (15.07.2026, физическая шина, 2 устройства:** Scanner 0x60 + Pump 0x30):
- Обе платы отвечают на broadcast F001
- DATA/DONE сканера не теряются при одновременной передаче
- `DROP_WRONG_DST` = 0, `DROP_WRONG_TYPE` = 0 (после hardware filter)
- `AutoRetransmission` подтверждена физически

**Влияние на экосистему:**
- Конфигурация добавлена в `DDS-240_ECOSYSTEM_STANDARD.md §2.2`
- Применена к Scanner и Pumps_valves прошивкам
- Обязательна для всех будущих Executor-плат

### 13.7. Выравнивание к канону экосистемы (03.08.2026)

По результатам аудита (реестр `DDS-240_ECOSYSTEM_STANDARD.md` §3.2.4) Scanner приведён к канону §3.2.5. Сборка Debug — чистая.

| Правка | Было | Стало | Место |
|--------|------|-------|-------|
| `SET_NODE_ID`: источник `new_id` | байт 3 (`data[0]`) | байт 2 (`device_id`) | task_dispatcher.c:143-156 |
| `SET_NODE_ID`: сохранение | auto-commit при установке | только RAM; `F003 COMMIT` персистит | task_dispatcher.c:143-156 |
| Валидация `SET_NODE_ID` | нет | `0x02..0x7F` + `!= CAN_ADDR_CONDUCTOR 0x10`; NACK `0x0006` | task_dispatcher.c |
| TX source | hardcoded `CAN_ADDR_SCANNER_BOARD 0x60` | рантайм `(uint32_t)AppConfig_GetPerformerID()` (5 мест) | task_can_handler.c:91,113,138,162,195 |
| CAN-фильтр | 2 банка (mask mode), bank1 hardcoded 0x60 | 1 permissive-блок (банк 0, маска `1<<2` IDE) + софт-валидация dst по `my_id` | task_can_handler.c:229-246, 296 |
| Именование API | `node_id`, `Get/SetNodeID` | `performer_id`, `Get/SetPerformerID` (uint32_t) | app_flash.h/c |
| Default NodeID | `0x60` | без изменений | app_flash.h |

**Проверки:**
- Физическая валидация CAN bench (SET_NODE_ID/COMMIT/FACTORY_RESET, ACK/DONE transition, DROP_WRONG_DST) — запланирована вместе со Stage 6.
- `CAN_ADDR_SCANNER_BOARD 0x60` остаётся в `can_protocol.h` как константа платы (не используется для фильтрации/адресации TX).
- Отметки в §3.2.5: Scanner — все 5 пунктов `[x]`.




## 23.10.2026 — первый GET_LAST_DATA через Studio

Пользователь выполнил GET_LAST_DATA0602 на Node60. CAN подтвердил
ACK → NACK E600 (CAN_ERR_SCAN_NO_DATA), без DATA/DONE. Ответ для пустого
shadow-буфера штатный, ветвь NO_DATA — BENCH PASS. Реальное считывание
штрихкода и его повторная выдача пока не проверены; визуализация отказа
Studio отдельно не подтверждена. Firmware не менялась.
[Три исходных кадра и manifest](test_artifacts/bench_20261023/get_last_empty/manifest.json).


## 23.10.2026 — одиночная попытка сканирования реагента: timeout

SCAN_REQUEST0601 с timeout10с получил ACK355.795090, затем NACK
E603 (CAN_ERR_SCAN_TIMEOUT) в365.756492; TX355.794834. Интервал
TX→NACK9.961658с по candump, не независимое измерение точности таймера.
DATA штрихкода и DONE в захвате отсутствуют; успешное сканирование
не подтверждено. Включение подсветки/модуля, UART/SSI и физические
условия чтения ещё не установлены; причина таймаута не определена.
Firmware/Studio не менялись, запрос отправил пользователь.
[Три исходных кадра и manifest](test_artifacts/bench_20261023/scan_timeout_10s/manifest.json).


## 23.10.2026 — timeout: подсветка не включалась, найдены расхождения UART/SSI

Пользователь подтвердил отсутствие подсветки и звука при SCAN_REQUEST.
В текущем Core/Src/main.c USART3 настроен1152008N1; README и §2.1
отчёта описывают96008N1 как исходную настройку модуля. Реальная настройка
сканирующего модуля ещё не установлена, несовпадение — гипотеза причины.
В scan_ssi.c также обнаружено внутреннее расхождение: Encode ставит Length
3+data_len, хотя выводит четыре байта заголовка перед data/checksum;
Decode вычисляет total=Length+2. Нужна сверка с исходным SSI-контрактом.
ScanUart_Send/Init возвращают bool, вызывающая задача результат не проверяет;
по CAN ACK невозможно утверждать успешную отправку START_DECODE в UART.
Вывод о дефекте аппаратуры по timeout не делается. Firmware не менялась.

## 27.10.2026 — Scanner UART RX через общий F020/F021: кандидат для внесения

По согласованному разделению общий механизм команд/транзакций остаётся
F020/F021, структура диагностического блока — специфика платы Scanner.
Подготовлен SCANNER_UART_RX, selector D501, schema1, index0=USART3,
capability flags0x83. Это не общий UART wire schema и не изменение D106 моторов.

**Статус:** Studio domain/application поддержка реализована; firmware-кандидат
подготовлен в [комплекте для внесения](test_artifacts/uart_diag_20261027/APPLY_INSTRUCTIONS.txt).
Рабочий STM32F103_scanner не изменён; пользователь вносит firmware сам.
В GUI Scanner точка запуска сценария пока не добавлена; новый общий модуль
не означает готовности аппаратного теста. UART-панель не возвращалась.

### Контракт D501 v1

F020 payload `20 F0 00 00 00 00 00 00`, caps record `01 D5 01 83 00 00`.
F021 payload `21 F0 01 D5 00 00 00 00`. Reserved/flags должны быть нулевыми.
Неизвестный selector/reserved — NACK0006; index !=0 — NACK0002.
Ответ: ACK, 40 DATA records, DONE(selector0). Каждая запись стандартная
item:uint16/value:uint32 LE; seq00..26,A7. F020 и F021 ничего не запускают
и не очищают, UART recovery не выполняют.

| item | Значение |
|---|---|
| 1 | Номер последнего HAL RX callback, 0 до первого; при wrap пропустить 0 |
| 2 | HAL_GetTick последнего RX события, uint32 ms с wrap |
| 3 | Текущая настроенная USART3 BaudRate |
| 4 | byte0 последний RX-arm HAL status (0..3, FF не вызывался); byte1 текущий HAL RxState; byte2 RX event 0=TC,1=HT,2=IDLE,FF=не было; byte3=0 |
| 5 | OR ошибок huart3.ErrorCode, наблюдавшихся RX/error callbacks с загрузки |
| 6 | Число HAL_UART_ErrorCallback для USART3, насыщение UINT32_MAX |
| 7 | Длина последнего RX события 0..128 |
| 8 | Reserved0 |
| 9..40 | 128 сырых байт DMA-префикса, wire order от младшего байта value; хвост после length нулевой |

При sequence0 длина/tick/bytes нулевые, eventFF; arm/state/errors всё равно
информативны. При sequence !=0 длина1..128 и event TC/HT/IDLE.
Это последнее **событие**, не последняя завершённая scan-транзакция и не
история: поздний ответ/STOP reply может заменить снимок. Для причинной связи
с запросом сравнивать номер/время до и после. SSI checksum не проверяется
при сохранении; invalid/partial данные сохраняются как есть. Ошибка UART
без RX callback сохраняет error mask/count, но не публикует несуществующий RX.

Сверка аппаратной конфигурации: несмотря на старый комментарий circular,
реальный Core/Src/stm32f1xx_hal_msp.c задаёт DMA_NORMAL. HAL вызывает
RxEventCallback также на HT; прежний re-arm тогда может вернуть HAL_BUSY.
Кандидат не меняет алгоритм приёма/SSI/таймаутов; он показывает этот статус
и сохраняет префикс до domain memcpy. Возможные потери между re-arm и
следующими байтами, отсутствие полноценной очереди RX и ошибки SSI остаются
отдельными вопросами, snapshot не исправляет их и не доказывает отсутствие
потерь на линии. Получение whole barcode этим шагом не заявлено.

### Реализация и проверка

ScanUart_GetDiagnostics копирует отдельный RAM-снимок с краткой маскировкой IRQ;
DMA пишет в другой буфер. В ISR нет CAN-передачи. Dispatcher упаковывает
одну копию в статический payload240 байт. Новый checked CAN response helper
использует прежнюю очередь/владельца HAL_CAN, допускает ожидание свободного
места с общим бюджетом100 мс. После сбоя admission DATA не ставится DONE;
NACK BUSY — best effort. Старый CAN handler по-прежнему может потерять кадр
на уровне mailbox/HAL: строгий общий parser Studio должен отвергнуть
неполный ответ. Очередь и bus delivery не приравниваются.

- Изолированная Debug-сборка кандидата: PASS, без warnings/errors;
  text40488/data20/bss14776. Рабочий ELF не заменялся.
- Host-модель исполняет candidate driver, реальные diagnostic encoder и
  checked queue functions: PASS, HT/IDLE/TC, 128bytes, обнуление хвоста,
  sticky errors, IRQ restore, отсутствие побочных действий getter,
  reserved/index validation, отказ каждого из41 enqueue, общий timeout.
- Выход C encoder сохранён в host_snapshot.bin и разобран Studio с проверкой
  всех128 RX-байтов. Это синтетический fixture, не аппаратный захват.
- Studio: 545 tests PASS (6.77s), включая D501 через общий capture/service,
  неполный ответ, чужой NodeID, NACK старой firmware, malformed schema.
- Patch apply --check: PASS; рабочий git status Scanner пуст.

[Патч](test_artifacts/uart_diag_20261027/scanner_uart_d501.patch),
[лог сборки](test_artifacts/uart_diag_20261027/candidate_build.log),
[host verifier](test_artifacts/uart_diag_20261027/verify_candidate.py).

### 27.10.2026 — D501 внесён в рабочую firmware по разрешению пользователя

После прямого указания «меняй прошивку» подготовленный патч применён к
STM32F103_scanner: App/inc/can_protocol.h, App/inc/scan_uart_driver.h,
App/src/scan_uart_driver.c, App/src/tasks/task_can_handler.c,
App/src/tasks/task_dispatcher.c. Все пять файлов по SHA256 совпадают с
ранее проверенным кандидатом; ручных изменений поверх него до применения
не было. Предыдущие отметки «кандидат, рабочий проект не изменён» описывают
историческое состояние до этого шага.

Полная пересборка рабочего Debug (`make -B -j4 all`) PASS, без warnings/errors:
text40488/data20/bss14776. Артефакт: STM32F103_scanner/Debug/STM32F103_scanner.elf.
[Лог рабочей сборки](test_artifacts/uart_diag_20261027/working_build.log).
git diff --check PASS. Предыдущие host-тесты применимы к идентичному коду.

Изменён исходник и собран ELF; запись в MCU не выполнялась. Аппаратная
приёмка D501 остаётся открытой. Внесена диагностическая наблюдаемость,
не исправления SSI, scan ERROR recovery или алгоритма DMA/re-arm.

### 27.10.2026 — Scanner UART доступен из GUI Studio

Пропущенный запуск из GUI добавлен: Conductor Emulator → Control Panel
сканера → Service Commands → «Диагностика UART (RX)»; вывод в Device Log.
Это вызов общего application-сервиса, без отдельной UART-панели.
Перед использованием перезапустить Studio; требуется firmware с D501.
Сначала прочитать baseline, затем один SCAN_REQUEST, после terminal прочитать
снимок повторно. Сравнить номер/время события и сырые RX-байты.
Offline Studio: 549 PASS; аппаратный результат пока не получен.

## 27.10.2026 — bench RX без событий; добавлена диагностика START TX

По пользовательским логам Studio: в10:59:36 F020 объявил D501v1 и F021
вернул полный блок40records/DONE. RX sequence0/tick0/length0, Baud115200,
RX arm HAL_OK, RxState0x22, errors0/count0. После SCAN_REQUEST в11:01:27.979668
получен ACK и NACK E603 в11:01:37.940602. Повторный D501 в11:01:47 показал
тот же snapshot. Это подтверждает работу чтения D501 и отсутствие HAL RX
событий в наблюдаемом интервале; физическое отсутствие сигнала не доказано.
Причина таймаута ещё не установлена. SSI parser до RX данных не дошёл.

По согласованию добавлен отдельный Scanner block D502 schema1,
SCANNER_UART_TX_START, logical index0. D501 остаётся неизменным. F020
объявляет D501 и D502; F021 payload для TX: `21 F0 02 D5 00 00 00 00`.
Ответ12records (seq00..0A,8B), DONE selector0. Стандартные F020/F021 правила
и checked queue admission сохраняются.

| item | D502 v1 value:uint32 LE |
|---|---|
| 1 | Номер последней попытки START, 0=не было; wrap пропускает0 |
| 2 | HAL tick перед вызовом Transmit_DMA |
| 3 | HAL tick TxCpltCallback этой попытки;0 до callback (tick может wrap) |
| 4 | byte0 HAL status вызова(0..3,FF до первой попытки), byte1 completion0/1, byte2 gState сразу после вызова, byte3=0 |
| 5 | ErrorCode при возврате HAL + OR ErrorCallback/TxCpltCallback во время соответствующего активного TX; не исключительно TX errors |
| 6 | Переданная HAL длина |
| 7 | Сохранённая длина=min(item6,16) |
| 8 | BaudRate во время попытки |
| 9..12 | Первые16 байт буфера, переданного HAL; padding после captured_len нулевой |

До первой попытки все поля/байты нулевые кроме item4=FF. На каждый START
снимок обновляется, STOP/ENABLE его не меняют. Явный ScanUart_SendStart
вызывается из task_scan_controller только для START; назначения произвольных
байтов драйвер не угадывает. HAL_OK означает принятие DMA-передачи, только
TxCpltCallback помечает completion. Ни одно из них не доказывает получение
команды модулем или сигнал после MAX3232. При отклонённой новой попытке
старый callback не присваивается новому снимку. Ошибки RX могут входить в
общий HAL ErrorCode — он не объявляется доказанной TX-ошибкой.

Рабочая firmware изменена и собрана без warnings/errors:
text41348/data68/bss14776. Запись в MCU ассистент не выполнял.
Host-тест реального driver/dispatcher: START/STOP association, HAL_BUSY и
HAL_TIMEOUT, чужой UART, completion, IRQ restore, bounded capture и
D502 encoder PASS. C fixture разобран Studio production formatter.
Studio GUI читает F020→D501→D502 одной существующей кнопкой «Диагностика
UART (RX/TX)». На предыдущей firmware без D502 показывает RX и отсутствие
TX-блока (partial), не выдумывает TX данные. Все558 tests PASS (6.45s).

Артефакты: [build.log](test_artifacts/uart_tx_20261027/build.log),
[verify_tx.py](test_artifacts/uart_tx_20261027/verify_tx.py),
[host log](test_artifacts/uart_tx_20261027/host_test.log).
Следующий bench: прошить новый ELF, перезапустить Studio, один SCAN_REQUEST,
после terminal прочитать RX/TX. SSI/скорость/recovery пока не менялись.

## 27.10.2026 — bench START TX завершён; подтверждено расхождение SSI пакета

Пользователь прислал baseline в11:35:37: D501/D502 объявлены, RX sequence0,
START attempts0. SCAN_REQUEST отправлен11:35:48.527674, ACK11:35:48.528022,
NACK E60311:35:58.488147. Диагностика, запрошенная через UI ещё во время
сканирования, фактически ушла в CAN только11:35:58.497105 — после scan.
Строка capture unknown в Studio не отменяет наблюдаемый terminal NACK.

После запроса: RX по-прежнему0, errors0, RX-arm HAL_OK, RxState0x22.
START TX attempt1, start_tick=complete_tick=140471ms, HAL_OK, gState0x21,
completion=1, errors0, requested/captured6bytes: `03 E4 04 00 15 FF`.
Это свидетельствует о выполнении передачи со стороны MCU/HAL, не доказывает
сигнал после MAX3232 или принятие команды модулем. Tick delta0 не означает
нулевую длительность: обе отметки имеют миллисекундную дискретность.

Проверен первичный источник Zebra «Calculate the Checksum for SSI Command»,
article000013431, https://support.zebra.com/article/000013431.
Он прямо приводит E4 host command `04 E4 04 00 FF 14`:
Length04 включает4bytes заголовка, сумма04+E4+04+00=00EC,
16-bit two's complement=FF14, передаётся старший байт первым.
Исходный encoder формирует Length03 и передаёт checksum младшим байтом
первым. Поэтому зафиксированный START не соответствует приведённому
стандартному SSI-пакету. Это подтверждённый программный дефект; его роль
как единственной причины отсутствия RX пока не доказана.
Decoder также требует исправления: складывает оба checksum bytes как
отдельные числа вместо прибавления16-bit checksum к сумме prefix.
Следующий изолированный шаг — исправить SSI codec с проверками эталонных
пакетов, сохранив115200 до отдельной проверки реальной скорости модуля.
На этом шаге firmware не менялась.

### 27.10.2026 — SSI codec correction after bench TX capture

Fixed Length to include the four-byte header (4 + payload), checksum byte order
(MSB first), and decoder verification against the 16-bit negative byte sum.
Decoder now excludes all four header bytes from payload and rejects oversized
payloads; encoder rejects invalid arguments. Golden vectors:
SCAN_ENABLE `04 E9 04 00 FF 0F`, START_DECODE `04 E4 04 00 FF 14`,
STOP_DECODE `04 E5 04 00 FF 13`.
This supersedes earlier statements describing SSI checksum as CRC16 or LSB-first,
and the earlier ENABLE example ending FF 0B. Reference: Zebra SSI checksum
article https://support.zebra.com/article/000013431 .

SCAN_ENABLE was already sent once at task startup, through the same faulty
encoder. Its acknowledgement/readiness is not checked by the current startup
sequence. Baud remains 115200. This patch corrects framing, not the scan state
machine: ACK handling and module readiness still need bench verification.

Host tests in STM32F103_scanner/tests/test_scan_ssi.c pass with AddressSanitizer/UBSan (leak
checking disabled because this environment uses ptrace). They cover golden
commands, ACK/NAK, checksum rejection, truncation, maximum payload and invalid
arguments. Debug build passes without warnings: text41348/data68/bss14776.
Artifact: Debug/STM32F103_scanner.elf. Assistant did not flash the board.

### 27.10.2026 — bench 13:37, repeated SCAN_REQUEST

User capture confirms corrected START bytes `04 E4 04 00 FF 14`, HAL_OK,
TX completion after 1 ms, RX event sequence still zero and no reported UART
errors. First request times out E603; second receives DEVICE_BUSY 0003.
Found a separate controller defect: StartScan accepted only IDLE, although
terminal states ERROR/DATA_READY never returned to IDLE. StartScan now rejects
only SCANNING, allowing a new transaction after either terminal outcome while
preserving statistics and the last barcode. Serialized scan task remains the
sole caller. This does not fix the missing UART response or ACK handling.
Host regression tests cover active-request rejection, retries after timeout,
malformed packet and success, and shadow/statistics preservation. ASan/UBSan
PASS (leak checking disabled for ptrace); SSI codec tests PASS. Debug build PASS,
text41348/data68/bss14776, no warnings. Assistant did not flash hardware.
MCU configuration checked: PB10 TX/PB11 RX, 115200 8N1, no hardware flow control;
USART3 and DMA RX IRQ handlers call HAL. Actual module baud/control wiring
remain unconfirmed. Build log: test_artifacts/ssi_codec_20261027/retry_build.log.

### 27.10.2026 — UART-only board, explicit wake before commands

User confirmed direct module-to-MCU UART only: no RTS/CTS/TRIG/WAKE signals
routed to MCU. Historical adapter/RS-232 and PB0 TRIG plans do not describe
this board. Module-side levels of unused inputs remain unknown.

SE965HP Integration Guide 72E-151566-01 Rev A, Table 1-2 specifies RXD 0x00
for wake; Table 6-1 specifies up to 8 ms wake-to-operation. Source:
https://www.scribd.com/document/586071361/51785c-f82347f83adb43908f2c8d95182b60e1-2
This confirms UART wake, not unconditional operation with floating CTS.

Added task-only ScanUart_Wake: separate raw 00 via HAL_UART_Transmit with 20 ms
bound (returns after TC), then >=10 ms scheduler delay including tick-phase
margin. RX DMA stays active. Called before startup ENABLE, START and STOP.
Failed wake suppresses the following command. Failed wake/START submission
returns CAN_ERR_SCAN_HARDWARE and sets controller ERROR without incrementing
response-timeout statistics. Startup/STOP send failures still have no separate
CAN result. Speed remains 9600 8N1; D501/D502 schemas unchanged. D502 retains
START only and does not capture the wake byte. This bench change does not fix
the existing first-RX/ACK handling or raw-versus-packeted barcode format.

Host verifier tests actual driver with HAL stubs: raw byte, bounded transmit,
settle delay on success only, failure branches and retained START snapshot;
existing D502 encoding/ownership tests also pass. Controller retry/transport
error test passes. Build passes without warnings: text42004/data68/bss14776.
Artifacts: test_artifacts/uart_wake_20261027/{verify_wake.py,host_test.log,build.log}.
Assistant did not flash the board. Next: read RX after boot, then issue one
SCAN_REQUEST and read RX/TX. Any ACK/NAK bytes are valuable even if CAN reports
hardware error due to the existing first-RX processing.

### 27.10.2026 — SCAN_GET_REVISION 0x0603 (read-only UART probe)

Current board has only UART routed to the MCU. After TX/RX wiring correction,
user reports red illumination on request but still no registered RX. Added a
read-only revision query independent of decoding a barcode; no baud sweep or
module settings writes. SSI source: 72E-40451-03 Rev A, pp. 2-58/2-59:
https://www.bsr.at/mediafiles/Handbuch/Zebra/DS3600_Simple-Serial-Interface-Programmer-Guide.pdf
REQUEST_REVISION is `04 A3 04 00 FF 55`; REPLY_REVISION opcode A4 contains ASCII.

CAN request: `03 06 00 00 00 00 00 00`, Scanner DeviceType5, assigned address.
Reserved bytes must be zero (otherwise NACK0006). Existing dispatcher ACK and
scan-task queue serialize it with ordinary scan commands; no direct ISR/CAN
or parallel UART path. Wake00 + >=10ms then REQUEST_REVISION; UART remains
9600 8N1. Fixed capture duration3s after submission; task feeds watchdog.

Response ACK -> DATA(seq/EOT, six payload bytes) -> DONE(selector0).
Payload envelope: byte0 schema1; byte1 flags(bit0 capture overflow; restzero);
bytes2..3 raw length LE16 <=192; bytes4..7 OR observed UART errors LE32;
bytes8.. raw bytes, then zero padding to multiple6. Empty capture returns a
valid empty envelope and DONE: this means capture completed, NOT successful
SSI communication. Failed wake/TX returns E601; queue congestion0003. Checked
CAN admission emits DONE only after all DATA queued. Host validates full
sequence/EOT/length/padding. Capture retains HAL RX events during the window,
including unsolicited data; it is not proof that every byte belongs to the
query. HT prefix is not duplicated at subsequent IDLE/TC. Bytes not yet
reported by a HAL RX callback at the deadline are not included. Overflow is
explicit; D501/D502 contracts unchanged, D502 still describes START only.

Studio: Conductor Emulator -> Scanner control panel -> Commands ->
«Версия сканера / сырой RX (0x0603)». Uses the existing application/transport,
4000ms terminal capture window and node-scoped domain assembler. Raw hex and
UART errors always shown for a complete CAN response. SSI checksum/source,
continuation and capture errors checked separately before displaying confirmed
REPLY_REVISION. Monitoring on/off tested; no new standalone UART panel.
Old firmware rejects unknown0603. Firmware does not modify scanner settings.

Validation: actual driver/task query helper host harness verifies fragment
capture, HT dedup, overflow, errors, empty DONE and TX failure. SSI golden A3
vector passes. New Studio tests cover real command button -> controller ->
transport -> assembler -> device log, missing frames and malformed envelopes.
Firmware build PASS without warnings: text42916/data68/bss15120. Hardware
revision reply remains unverified; assistant did not flash hardware.
Artifacts: Scanner/test_artifacts/revision_20261027/.


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
