# Scanner Executor Project Report

Точка останова — конец02.11.2026: следующий согласованный блок F002/F006,
STOP-only shutdown после latch, общий бюджет3s. Это AGREED без нового CODE.
См. последнюю запись и актуальный NEXT_SESSION_PROMPT.md.

Актуальное состояние02.11.2026: RX и TX USART3 работают через UART-прерывания,
накопитель RX128B; пользователь удалил оба DMA-запроса через Cube и сообщил
чистую сборку. Закрытие RX перед terminal выполнено и проверено ассистентом;
Debug45740/68/15128,0errors/0warnings. См. последнюю запись02.11 и план U22.
Открыты доменная остановка перед F002/F006, защита F003 от активного UART и BENCH.
Обязательный STOP ACK после успешного чтения не является выбранным решением.

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

### Локальные коммиты точки останова27.10

- Firmware99acccd относительно5a7e704:22файла,+2834/-49, включая снимки
  общей документации в readme/checkpoints/20261027.
- Studio5d8f21d относительно7b62898:45файлов,+6778/-635. Это накопленный
  checkpoint общих слоёв/Motion/Sensor/Scanner, не только работа27.10.
- Проверено583 Studio tests PASS; firmware rebuild без предупреждений,
  text43028/data68/bss15120; host SSI/controller PASS.
- Push не выполнялся. В Studio .pvs оставлен вне коммита как локальный файл.
- Общая документация вне Git; её состояние до записи этих хешей сохранено
  в firmware checkpoint. В исторических копиях и сыром build.log сохранены
  существующие завершающие пробелы; функциональные файлы ими не изменялись.

## 28.10.2026 — GPIO назначены, начато аппаратное согласование SSI

Пользователь добавил в Cube: CTS PA10 Output, RTS PA9 Input, PWRDWN PA8 Input,
TRIG PB12 Output, AIMWAKE PB13 Output, DLED PB14 Input, BPR PB15 Input.
CTS/TRIG/AIMWAKE исправлены на начальный HIGH в .ioc и main.c; запись уровня
предшествует HAL_GPIO_Init. Внешние pull-up10кОм предполагаются по схеме;
их фактическая установка и физическое подключение новых проводов не проверены.
FLASH_DWLD pin1 не подключать к MCU; оставляется свободным/сервисная точка.
Это уточняет прежние записи о неназначенных GPIO и UART-only.

Авторизованное изменение драйвера: все SSI-пакеты проходят один общий
ScanUart_SendInternal. CTS HIGH в покое, ожидание RTS HIGH перед транзакцией,
CTS LOW -> ожидание RTS LOW -> побайтовая DMA-передача с проверкой разрешения
перед каждым байтом. Финальный UART TC callback освобождает CTS HIGH;
ожидается снятие RTS. На выходах с ошибкой CTS также освобождается.
Ожидание первого grant/release ограничено2с, межбайтового grant100мс,
одного TX100мс; watchdog обслуживается при ожидании, DMA abort при зависании.
Методы Send теперь синхронно дожидаются окончания handshake, буфер вызывающего
кода не остаётся во владении DMA после возврата. RX DMA продолжает работать.

Wake теперь аппаратный: AIMWAKE LOW >=1мс, возврат HIGH, пауза >=10мс.
Необрамлённый00 больше не отправляется. TRIG удерживается HIGH: запуск через
SSI остаётся прежним. PWRDWN/DLED/BPR пока только объявлены входами, алгоритм
их не читает. D502 сохраняет целый запрошенный START, последний HAL status,
а completion означает TC последнего байта; при поэтапной передаче снимок
не доказывает, что весь пакет принят сканером. До первого HAL launch при
отсутствии grant снимок D502 не обновляется. CAN/D501/D502 схемы не изменены.

Источник: SSI Programmer Guide72E-40451-03, Hardware Handshaking,1-3/1-4.
https://www.bsr.at/mediafiles/Handbuch/Zebra/DS3600_Simple-Serial-Interface-Programmer-Guide.pdf
Host test tests/test_scan_handshake.py компилирует реальный driver со stubs:
wake, побайтовые grant, финальный TC/release, RTS stuck HIGH/LOW, watchdog,
зависание DMA/abort, HAL_BUSY и повтор после ошибки — PASS.
Старые внешние harness UART-only описывают историческую реализацию.
Остаются отдельными задачами ACK/сборка результата сканирования и диагностическое
отображение handshake; аппаратная приёмка новой проводки не выполнена.

Проверка28.10: сборка text43444/data68/bss15120 без предупреждений. В Studio
окно capture для0603/0604 увеличено до10с, для0601 — timeout+7с, чтобы учесть
ограниченные ожидания GPIO.30 профильных Studio tests PASS. Увеличение окна
не исправляет ранее зафиксированное ложное сообщение фонового observer.
Новый ELF не прошивался ассистентом; коммит и NEXT_SESSION не обновлялись.

## 28.10.2026 — аудит унификации до дальнейших правок

По поручению пользователя выполнена статическая сверка всех слоёв Scanner
с функциями HC/Thermo/Fluidics и каноном экосистемы. План с24 строками матрицы,
чекбоксами этапов и конечным профилем проверок:
[SCANNER_UNIFICATION_PLAN_20261028.md](SCANNER_UNIFICATION_PLAN_20261028.md).
Ключевое: в source один permissive bank0, а требуются broadcast/direct два
банка; нет software RTR/source checks, F005 допускает чужие семьи, F00480/80,
Flash Unlock/Lock игнорируются, reset-latch/cancel отсутствует, watchdog/fatal
и RTOS creation не выровнены. В плане отдельно сохранены правильные части
и доменные вопросы SSI. Source SHA256/status/HEAD сохранены в
[test_artifacts/unification_audit_20261028/sources.json](test_artifacts/unification_audit_20261028/sources.json).
В этом аудите исходники не редактировались; новые build/HOST/BENCH не запускались.
NEXT_SESSION не обновлялся. Унификация не объявлена завершённой.

## 28.10.2026 — актуализация правил работы

По прямому поручению пользователя перенесены15 актуальных правил из Sensor
(источник Fluidics/§4 Thermo) с адаптацией доменной границы Scanner.
Канонический файл: [WORK_RULES.md](WORK_RULES.md). В plan и исторический
раздел правил handoff добавлены ссылки. Порядок: сверка контракта и эталона
до предложения -> границы -> согласование -> точные блоки -> пользователь
вносит -> аудит. Обновление правил не является изменением firmware, новым
эталоном или повышением статуса приёмки. Код до продолжения не изменялся.

## 29.10.2026 — обязательная проверка до выдачи решения

По прямому поручению пользователя уточнены правила унификации: контракт
экосистемы -> конкретный механизм эталона -> перечень адаптаций -> сравнение
подготовленного блока ДО предложения кода. Противоречия эталона норме
обсуждаются отдельно; дополнительные проверки не добавляются скрыто.
Процедура в WORK_RULES.md, автоматическая точка входа — firmware AGENTS.md.
В плане scope CAN отмечен согласованным, повторная сверка/переиздание F005
оставлены открытыми. Самостоятельные правки firmware не разрешены.
В этом изменении обновлены только правила/документация; исходники неизменны.

## 29.10.2026 — фильтры и связь F005 проверены по коду

Пользователь внёс изменения, сообщил чистую сборку. Две функции фильтров
сверены с HC6abc1d2, порядок startup/F005 и header/include проверены.
U01/U09 CODE отмечены выполненными в плане; BENCH открыты. Проверку длины
выполняет CAN Handler до Dispatcher, парсер задаёт фиксированный data_len5.
Следующий U02 — CAN_IsAcceptedCommand/CAN_ParseCommand; до согласования
новые блоки не выдаются и код не вносится. Новых build/стендовых действий нет.

## 29.10.2026 — завершённые блоки CAN унификации

Источник механизмов — HC6abc1d2, App/src/tasks/task_can_handler.c;
адаптация семейства F005 — Scanner60..6F из global config.

| Блок | Завершённый результат | Подтверждение | Ограничение |
|---|---|---|---|
| U01/U09 | Два bank IDMASK32: broadcast COMMAND и runtime direct COMMAND; F005 helper диапазона, RAM -> bank1 -> DONE; прототип/include связаны | CODE: функции фильтров совпадают с HC без комментариев/форматирования; BUILD: чистая сборка сообщена пользователем | Смена адреса и аппаратный приём на MCU ещё не проверены |
| U02 | CAN_IsAcceptedCommand: IDE/RTR/DLC/type/src/dst; CAN_ParseCommand: memset и фиксированные поля, data_len5; RX очередь связана | CODE: обе функции совпадают с HC; BUILD: сборка ассистентом без ошибок/предупреждений после внесения пользователем | HOST/BENCH сценарии транспортного отбора ещё не выполнены |
| U06 | Снимок CAN diagnostics сохраняет/восстанавливает PRIMASK | CODE: совпадает с HC; HOST PASS: PRIMASK0/1, копирование под IRQ mask, полнота данных, NULL без эффектов; BUILD: чистая сборка сообщена пользователем | Модель IRQ не является измерением MCU |
| U07, только CAN | Ожидание с переводом ms в ticks; heartbeat после события/штатного timeout, без heartbeat при остальных ошибках | CODE: блок совпадает с HC; HOST PASS: RX/TX/оба флага, timeout, Resource/Parameter; BUILD: чистая сборка сообщена пользователем | Dispatcher/Scan и аппаратная watchdog-проверка остаются открытыми |

Артефакты: [U06](test_artifacts/u06_20261029/host_result.txt),
[U07 CAN](test_artifacts/u07_can_20261029/host_result.txt).
Актуальный статус — [план с чекбоксами](SCANNER_UNIFICATION_PLAN_20261028.md).
Следующая работа — обсудить перенос heartbeat Dispatcher по конкретному
механизму HC. Полная унификация и аппаратная приёмка не объявляются.
NEXT_SESSION в ходе рабочего блока не менялся; новых прошивок платы и
коммитов ассистент не выполнял. Мелкие исправления внесения отдельно не
регистрируются: в отчёт попадают завершённые блоки и их доказательства.

## 29.10.2026 — U07 Dispatcher

Блок ожидания/heartbeat сверён с HC6abc1d2, отличие — имя parsed.
ACK до прикладной обработки, F020/F021 bypass и обычный switch проверены.
HOST PASS: OK/timeout/Resource/Parameter; heartbeat, ACK и маршрутизация
происходят только в своих ветках. Чистая сборка сообщена пользователем.
[Артефакт](test_artifacts/u07_dispatcher_20261029/host_result.txt).
CODE/HOST и BUILD отмечены в плане. U07 Scan и аппаратная проверка открыты.
Исходники ассистентом не менялись, новых действий с платой не выполнялось.

## 29.10.2026 — U07 Scan: ожидание очереди

Внесённое пользователем ожидание scan_queue сверено с механизмом HC Dispatcher:
адаптации scan_queueHandle/cmd/APP_WDG_CLIENT_SCAN, логика без отличий.
Обработка0601/0602/0603/0604 после ожидания сохранена. HOST PASS для
OK/timeout/Resource/Parameter: heartbeat только при штатных исходах, обработка
cmd только при OK. Чистая сборка сообщена пользователем.
[Артефакт](test_artifacts/u07_scan_queue_20261029/host_result.txt).
План обновлён. UART wait Scan и аппаратные проверки остаются открытыми.
Исходники прошивки ассистентом не изменялись.

## 29.10.2026 — U07 Scan UART wait

Внесённый цикл совпадает с согласованным предложением. Общий механизм
HC — heartbeat после штатного wait; адаптация Scanner — конечный срок
по RTOS tick difference, slice не более500ms/remaining, E602 при ошибке
ожидания с HandleTransportError, E603 с HandleTimeout. RX-разбор сохранён,
STOP расположен после всех трёх исходов. HOST на извлечённом фактическом
цикле PASS: общий срок, RX, RTOS error без heartbeat, уменьшение последнего
ожидания и wrap ticks. Маршрутизация исходов/STOP проверена статически.
Чистая сборка сообщена пользователем; BENCH NOT RUN.
[Артефакт](test_artifacts/u07_uart_20261029/host_result.txt).
План обновлён. ACK/SSI stream, safe-state/watchdog supervisor и остальные
открытые строки не закрываются этим результатом. Код ассистентом не менялся.

## 29.10.2026 — U03 CAN IRQ diagnostics

По прямому разрешению пользователя ассистент перенёс два callback из HC6abc1d2
в Core/Src/stm32f1xx_it.c, USER CODE1; can_protocol.h подключён в USER CODE Includes.
Исполнимый код callback совпадает с HC; добавлены только комментарии. RX pending
кладёт raw frame в очередь, при отказе учитывает существующий overflow; CAN error
сохраняет HAL error/ESR. Новые метрики, CAN-ответы из ISR и recovery не вводились.
HOST PASS: успешный RX/queue-full/HAL RX failure/error snapshot. Сборка ассистентом
без ошибок и предупреждений. BENCH NOT RUN; MCU не прошивался, коммита нет.
[Артефакты](test_artifacts/u03_20261029/host_result.txt). План обновлён.

## 29.10.2026 — U04 / U08 F004 DATA format

Пользователь перенёс CAN_SendData с явным sequence_info и Dispatcher_HandleGetUid.
Исполнимый код обоих helper совпадает с HC6abc1d2. Прототип/все вызовы проверены,
F007 сохраняет DATA80, F004 формирует00/81 и DONE. F001 не менялся.
HOST на извлечённых функциях PASS: UID12bytes и порядок, F007 LE, padding,
ограничение6bytes, NULL и runtime source60/6F. Чистая сборка сообщена пользователем.
[Артефакт](test_artifacts/u04_f004_20261029/host_result.txt). План обновлён.
BENCH NOT RUN; queue admission не означает доставку на шине. Новых действий
с платой и самостоятельного редактирования firmware не выполнялось.

## 29.10.2026 — U05 TX queue wrapper

Пользователь внёс CAN_QueueTxFrame(const CanTxFrame_t*), совпадает с HC6abc1d2
по исполнимому коду. HOST PASS: NULL без эффектов, успешный enqueue/FLAG_CAN_TX,
queue-full с overflow без flag и неизменность исходного кадра. Сборка
ассистентом без ошибок/предупреждений. План обновлён.
[Артефакты](test_artifacts/u05_20261029/host_result.txt). Давление на очередь
и физическая доставка остаются BENCH NOT RUN; отдельный diagnostic enqueue
не менялся. Ассистент исходники не редактировал, плату не прошивал.

## 29.10.2026 — U11 F007 handler

По прямому разрешению пользователя ассистент вынес F007 в отдельный
Dispatcher_HandleGetStatus из HC6abc1d2. Единственная адаптация helper —
существующее имя SendStatusMetric. Case вызывает helper и break. Формат
не менялся:18 common IDs0001..0012, LE32, DATA80, единый snapshot, DONE0.
HOST PASS на фактических функциях, включая изменение live-счётчиков во время
выдачи: ответ остаётся из одного снимка. Сборка без ошибок/предупреждений.
[Артефакты](test_artifacts/u11_20261029/host_result.txt). План обновлён.
BENCH NOT RUN; на MCU не прошивалось, коммита нет. Queue admission не
гарантирует отсутствие потерь ответа в CAN, нагрузочные проверки открыты.

## 29.10.2026 — U12 AppConfig_Init

По прямому разрешению пользователя ассистент изменил только AppConfig_Init
и подключил can_protocol.h в app_flash.c. Источник механизма — Thermo c43368e,
AppConfig_Init. Адаптации: Scanner60..6F/default60, прежний layout16B и CRC,
без ROM-map/default loops Thermo. Mutex failure -> Error_Handler/return;
valid magic/checksum/Node -> RAM copy; invalid -> RAM defaults без erase.
HOST PASS на фактической функции:60/6F, чужие адреса, bad magic/CRC, NULL
mutex и повторная инициализация, reserved/full record, размер16B/CRC, Flash
не меняется. Сборка без ошибок/предупреждений.
[Артефакты](test_artifacts/u12_20261029/host_result.txt). План обновлён.
Commit/FactoryReset не менялись, это следующий U13. BENCH NOT RUN,
прошивка платы/коммит не выполнялись.


## 29.10.2026 — U13 Commit/FactoryReset; завершение дня

По прямому разрешению пользователя ассистент изменил AppConfig_Commit и
AppConfig_FactoryReset в app_flash.c, bool-прототип в app_flash.h и обработку
результата FactoryReset в F006 Dispatcher. Эталон — HC6abc1d2,
App/src/app_flash.c без рабочих изменений. Обе Flash-функции совпадают
с эталоном после удаления комментариев/пробелов. Scanner сохраняет собственные
layout16B, CRC и конфигурационную страницу; RAM NodeID до reset не меняется.
Основание — EXECUTOR_COMMON_LAYERS_GUIDE, раздел «Конфигурация и Flash».

HOST PASS на извлечённых фактических функциях: NULL/acquire failure без Flash,
отказ Unlock без erase/program, отказ erase, каждого из4 program words и Lock;
успешная запись16B с checksum, reset стирает страницу без изменения RAM;
ошибка release FactoryReset вызывает fatal. Commit, как HC, игнорирует результат
release — усиление этого механизма не вводилось. F006: неверный ключ без
стирания/reset; успех DONE, отказ NACK FLASH_WRITE, затем прежний reset.
Это не закрывает U10: PrepareReset, запрет активации и safe-state ещё открыты.

[Артефакты](test_artifacts/u13_20261029/host_result.txt), исходный host harness,
хеши/сверка в evidence.json и build.log находятся в той же папке.
BUILD PASS без ошибок/предупреждений: text45260, data68, bss15120.
BENCH NOT RUN. Независимого Flash readback нет; атомарность сохранения
и сохранность прежней записи при ошибке после erase не заявляются.
План и NEXT_SESSION_PROMPT обновлены. Плата не прошивалась, CAN-команды
не отправлялись, коммит не выполнялся. Следующий блок — U10/U14/U16
по плану, сначала сверка точных функций HC и адаптации к Scanner.


## 29.10.2026 — фиксация текущего состояния в Git

Пользователь разрешил коммит по полному diff относительно Scanner99acccd.
Коммит включает пользовательские GPIO/Cube, текущую частичную унификацию
CAN/service/RTOS/config и ранее внесённый SSI handshake с его host harness.
Фиксация handshake не является согласованием его механизма или аппаратной
приёмкой. Следующий блок U10/U14/U16 остаётся открытым. Снимок документации,
плана и доказательств сохранён в firmware readme/checkpoints/20261029.

Коммит выполнен: `5569467` — `refactor(scanner): align CAN and config layers with executor references`.
База сравнения `99acccd`;57 файлов,5336 добавлений/872 удаления (включая
снимок документации и доказательства). Рабочее дерево Scanner после коммита
чистое. Изменения отдельного репозитория service_studio не коммитились.

## 30.10.2026 — U14, частичная реализация и согласование TX cancellation

Пользователь внёс GPIO safe-state CTS/TRIG/AIMWAKE HIGH; по прямому запросу
ассистент переименовал существующий модуль board_safety в app_safety и
Board_EnterSafeState в AppSafety_AllOff во всех актуальных вызовах.
Пользователь добавил latch PrepareReset/IsResetPending по HC6abc1d2,
gates AIMWAKE, CTS и каждого запуска TX DMA, отмену ожидания RTS по latch.
Фактические файлы проверены; сборки последних шагов чистые по сообщениям
пользователя. HOST/BENCH для этих новых изменений ещё не выполнялись.

Согласована адаптация отмены TX: закрыть принадлежность START completion,
проверить HAL_UART_AbortTransmit; при успехе отпустить CTS и вернуть отказ
операции, при ошибке перейти в терминальный Error_Handler без возврата
владельцу DMA-буфера. Применить также к существующему TX timeout.
Это политика Scanner, а не буквальный перенос отказа ADC из HC.
На момент записи данный helper и его вызов ещё не внесены.
F002/F006 пока не подключены к PrepareReset. Отмена RX, queued/in-flight
доменных операций, terminal-result gates и U16 остаются открытыми.
HIGH на TRIG не является доказательством остановки SSI-сканирования.

### Продолжение30.10 — TX/RX cancellation и аварийный путь

Пользователь внёс проверяемый ScanUart_AbortTx и выход из TX wait по latch;
при отказе abort вызывается Error_Handler. Добавлены gate RX start/re-arm,
отсечение позднего RX callback после сырого диагностического снимка,
bool ScanUart_CancelRx с закрытием capture и аннулированием holding_len/rx_prefix.
Фактические функции и прототипы проверены, сборки чистые по сообщениям пользователя.
ScanUart_CancelRx пока не подключён к доменной подготовке reset.

В Error_Handler и пяти исключениях NMI/HardFault/MemManage/BusFault/UsageFault
внесён порядок Fluidics ac1aa2b: IRQ off -> AppSafety_AllOff -> DSB/ISB ->
терминальный цикл без RTOS/CAN/IWDG refresh. Helper EnterSafeFaultState и
все пять вызовов сверены с фактическими файлами; пользователь сообщил чистую сборку.
Это CODE и пользовательский BUILD, не HOST/BENCH приёмка U16.

Следующая незакрытая граница — доменная синхронизация reset, queued/in-flight
операции, момент принятия terminal-результата и подтверждение подготовки
до Flash в F006. Подключать один latch в F002/F006 без этой связности нельзя.

### Согласованный порядок доменной подготовки30.10

Пользователь согласовал: Dispatcher устанавливает latch/safe-state, задача
Scanner отменяет свою операцию, останавливает RX, очищает старое уведомление
и подтверждает подготовку; лишь затем Dispatcher продолжает F002/F006.
Queued commands получают существующий DEVICE_BUSY. Ожидание подтверждения
ограничивается; ошибка остановки RX или истечение срока подготовки ведут
в terminal Error_Handler без начала Flash. Конкретный бюджет ещё не выбран.
План дополнен текущими статусами30.10; новое согласование не является CODE
или разрешением самостоятельного редактирования firmware.

### Доменная синхронизация30.10 — idle и queued

Пользователь внёс Scan_SyncResetState, volatile подтверждение и
ScanTask_IsResetPrepared в API задачи. Helper после latch останавливает RX,
отменяет SCANNING через существующий HandleTransportError, очищает RX flag
и лишь затем публикует prepared. При отказе RX/очистки flag -> Error_Handler.
Вызовы стоят до queue wait и после получения команды; queued -> DEVICE_BUSY.
Фактические файлы/объявления проверены, сборка чистая по сообщению пользователя.
HOST/BENCH NOT RUN. In-flight scan/capture и terminal-result gates ещё открыты;
F002/F006 пока не используют новый порядок.

### Capture0603/0604 — checkpoints отмены30.10

Пользователь внёс SyncResetState до запроса, после отказа wake/send,
внутри capture wait и после последнего ожидания. Отмена -> DEVICE_BUSY;
длительность3s и существующий шаг ожидания сохранены. CODE проверен,
BUILD чистый по сообщению пользователя; HOST/BENCH NOT RUN.
Гонка принятия terminal-результата остаётся отдельным открытым шагом.

### Scan0601 — checkpoints отмены30.10

В RX wait добавлен выход по latch, после wait — SyncResetState с DEVICE_BUSY
и пропуском STOP. При внесении START error block оказался в startup вместо
командной ветки, также отсутствовало объявление cmd; сборка завершилась ошибками.
По прямому запросу пользователя ассистент восстановил SCAN_ENABLE через обычный
Send и ScanResult_t cmd, а обработку reset перенёс в START внутри command loop.
CODE связность проверена; проверочная Debug-сборка завершилась с exit0.
Terminal-result race, Dispatcher F002/F006 и HOST/BENCH остаются открытыми.

### Terminal gate0603/0604 —30.10

Пользователь внёс Scan_TryAcceptTerminalResult и gate перед отправкой
диагностического ответа. Решение под IRQ-save по Fluidics_ServiceStops ac1aa2b;
CAN queue wait выполняется вне критической секции. Если latch предшествует gate,
результат отменяется через SyncResetState/DEVICE_BUSY; принятое до latch решение
разрешает завершить ответ, затем задача подтверждает подготовку reset.
Фактический CODE проверен, BUILD чистый по сообщению пользователя.
Gates0601/0602 и HOST/BENCH ещё открыты.

### Terminal gates0601/0602 —30.10

Пользователь внёс gate0602 после подготовки локальной копии shadow и gate0601
до обработки terminal RX/timeout/error, статистики и сохранения barcode.
Фактические файлы проверены; BUILD чистый по сообщению пользователя.
При latch до gate -> SyncResetState/DEVICE_BUSY, при принятии до latch
разрешено закончить ответ. Dispatcher ждёт готовности задачи Scanner.
Интеграция Dispatcher пока не внесена; HOST/BENCH NOT RUN.

### F002 — подтверждённая подготовка30.10

Пользователь внёс ReadLe16 по HC6abc1d2, Dispatcher_PrepareReset и F002.
Неверные key/length -> INVALID_KEY без latch. Валидный запрос -> latch/safe-state,
ожидание prepared до1000ms, DONE, проверяемое окно100ticks и reset.
Deadline/ошибка osDelay -> Error_Handler. Heartbeat Dispatcher публикуется
только после успешного прохода ожидания; прямого IWDG refresh нет.
Бюджет1s выбран с учётом существующих ожиданий Scanner <=500ms и
диагностической очереди <=100ms, это проектный предел, не аппаратное измерение.
Фактический CODE проверен, BUILD чистый по сообщению пользователя.
F006 пока прежний; общий HOST/BENCH подготовки ещё не выполнен.

### F006 и проверка связного блока D —30.10

Пользователь внёс F006 по HC6abc1d2 с адаптацией Dispatcher_PrepareReset:
key/length -> latch и подтверждение задачи Scanner -> Flash bool -> DONE/NACK
-> проверяемое окно100ticks -> reset. Неверный ключ ничего не отменяет.
Ошибка Flash не снимает latch. Фактический код проверен.

HOST PASS21 групп: извлечены фактические app_safety, полный UART driver,
controller/SSI codec, полная задача Scanner, Dispatcher helpers/cases и
Error_Handler/fault handlers. Использованы детерминированные HAL/RTOS mocks;
terminal loops в host заменены longjmp, прочая проверяемая логика не заменяется.
Проверены PRIMASK0/1; запрет wake/CTS/TX/RX после latch; reset во время TX,
поздний TX callback в abort, ошибка abort и невозврат к штатному успеху;
поздний RX только в diagnostics; CancelRx и ошибки подготовки/очистки flags;
queued cancellation и reset при RX/timeout/error в фактической задаче без
DONE и ложной статистики; отмена capture0603/0604 и обычный capture;
принятие terminal до reset; F002/F006 ключи/порядок/Flash failure;
deadline, задержки, RTOS tick wrap и другая frequency; аварийные префиксы.

BUILD PASS: make -B -j4 all, полная Debug-пересборка, 0errors/0warnings;
text47132/data68/bss15128. Исходные SHA256 и ELF SHA256 сохранены.
[Артефакты](test_artifacts/u10_u14_u16_20261030/host_result.txt),
run_host.py, host.c, extracted.inc, evidence.json, host_build.log и build.log
находятся в той же папке. HOST не моделирует настоящий планировщик/аппаратный
DMA и не подтверждает физическую доставку CAN, SSI STOP, fault exception
или IWDG reset. BENCH NOT RUN; плата не прошивалась, аппаратные команды
не отправлялись, коммит не выполнялся.

При итоговом просмотре найден повтор объявления ScanTask_IsResetPrepared
в доменном scan_controller.h. Реализация и правильный task_scan_controller.h
связаны, сборке повтор не мешает; удалить только лишнее объявление с комментарием.
Это открытая API cleanup, firmware ассистент в этой проверке не редактировал.
Полная аппаратная приёмка блока D и платы не заявляется.

Уточнение30.10: пользователь удалил лишнюю декларацию из scan_controller.h;
фактические заголовки проверены, объявление осталось только в заголовке задачи.
API cleanup закрыта. Код watchdog на этот момент ещё прежний:500ms/3misses,
без проверки osDelay и с RTOS-циклом после safe-state; выданные изменения U15
ещё не внесены. Выбор нового профиля не следует из исправления декларации.

## 30.10.2026 — результат watchdog/RTOS и согласованный контракт SSI

### Общие слои: завершённые CODE/HOST части

Профиль watchdog Scanner согласован1000ms/1miss, idle500ms. Три клиента:
CAN, Dispatcher, Scan. Исполнимый supervisor соответствует HC6abc1d2,
включая period_ticks, zero-period guard, проверку osDelay и terminal
Error_Handler. Отличающийся профиль следует §2.7.1; эксперимент HC500ms/2misses
не переносится. Приоритет supervisor Realtime48 выше CAN High40, согласован
в main.c/.ioc. HOST проверил штатный прогресс, каждый stalled client,
немедленный и поздний RTOS error, нулевой период, tick frequency100/1000Hz,
wrap счётчиков и недопустимый client. Проверочная Debug-сборка чистая.

По HC добавлены guards4queue/4thread в USER CODE до допуска приложения.
Фактические блоки проверены;32 комбинации handles на HOST PASS. Пользователь
сообщил чистую сборку. Это модель guard, не отказ реального allocator.
Расчёт по ARM sizeof/heap_4:6688B для4task/4queue/config mutex из8192B;
1504B остаётся до служебного выравнивания/end marker и иных allocations.
Idle/timer task/queue размещаются статически. Это не runtime minimum heap
и не подтверждение достаточности stack. BENCH idle/fault/recovery/stack
high-water mark остаётся открытым; Release не проверялся.
[HOST-артефакты E](test_artifacts/u15_u17_20261030/).

### SSI: принятые решения и фактическое состояние

Целевой формат данных нашего сканера — packeted DECODE_DATA F3, EE=1.
Наличие factory-default raw в руководстве не меняет выбранного контракта.
Фактическая настройка экземпляра ещё не подтверждена; последняя сохранённая
аппаратная запись27.10 содержит RX0. Причина отсутствия приёма не установлена.

Принято сохранить побайтовую TX DMA с проверкой RTS перед символом,
CTS release после UART completion и существующие локальные таймауты.
Дополнительный общий TX deadline сейчас не вводится. Существующий срок0601
timeout_sec/default4s отсчитывается после START/handshake; валидный входящий
ACK не завершает запрос и не начинает этот срок заново.

Внесены и просмотрены: устойчивый RX snapshot с сохранением PRIMASK,
Scan_SkipAckPackets (включая ACK перед следующим пакетом в том же RX-событии),
отключение HT после успешного RX start при DMA_NORMAL. Подготовленный ACK-блок
проверялся HOST, сборка после удаления лишней копии helper была чистой.
В DECODE_DATA первый байт тела — Bar Code Type; пользователь внёс копирование
строки с data[1] длиной data_len-1 и проверку наличия Type. Фактический CODE
проверен, пользователь сообщил чистую сборку. Подготовленная правка прошла HOST.

Принят нормальный порядок обмена:
сканер -> DECODE_DATA -> МК проверяет пакет -> МК отправляет SSI CMD_ACK
сканеру -> ожидание отпускания RTS в HIGH -> CAN DATA фрагменты Дирижёру ->
CAN DONE. RTS HIGH завершает аппаратный обмен, отдельного сообщения
о принятии ACK протокол не предусматривает. Согласован отказ отправки ACK:
CAN NACK E602 (SCAN_COMM), без DATA/DONE; reset сохраняет приоритет
и возвращает DEVICE_BUSY. SSI NAK/resend policy не принята.
Связанный блок внесён пользователем и проверен по фактическому файлу: предварительная проверка DECODE_DATA
без изменения состояния/статистики, отправка CMD_ACK, затем существующее
принятие terminal-результата и передача CAN. Пользователь сообщил чистую
сборку. HOST фактических исходников PASS: успешный обмен и точные байты
ACK перед CAN DATA/DONE, повреждённая checksum без ACK, отказ TX ACK с E602,
reset во время ACK без принятия результата, входящий ACK без продления срока.
Доказательства сохранены в test_artifacts/ssi_outgoing_ack_20261030/:
runner, модель, результат и SHA256 исходников. Это модель HAL/RTOS;
BENCH NOT RUN. Старые HOST-результаты D не переносятся
автоматически на последующие изменённые UART/domain функции.

134B получены в первой реализации3e82ec7 от13.07 из локальных констант
header4 + body128 + checksum2. Это не наблюдённый пакет и не характеристика
длины каждого штрихкода. Тело DECODE_DATA включает Type1B;64ASCII без
prefix/suffix дают71B полного SSI-пакета, что помещается в DMA128B.
Старый план64символа был предварительным. Из этих чисел не следует
необходимость увеличивать DMA или вводить кольцевой буфер.
Сборка разделённых пакетов, сохранение нескольких RX-событий и overflow policy
остаются OPEN; конкретная кольцевая архитектура не согласована.

Нормативные источники домена: SE965HP72E-151566-01 RevA §7-94/7-95
(ACK/NAK и формат); Zebra SSI72E-40451-03 RevA §1-7 и Tables2-33/2-34
(DECODE_DATA, Type, подтверждение).
Копии: https://www.scribd.com/document/586071361/51785c-f82347f83adb43908f2c8d95182b60e1-2
и https://www.bsr.at/mediafiles/Handbuch/Zebra/DS3600_Simple-Serial-Interface-Programmer-Guide.pdf.
Firmware в ходе документирования не менялся, стенд/коммит не выполнялись.


## Итог сессии30.10.2026 — U22 частично, точка продолжения

По прямому поручению пользователя ассистент заменил Scan_SkipAckPackets
на Scan_CollectPacket в task_scan_controller.c. Задача собирает один SSI-пакет
по Length+2 в статическом rx_packet, сохраняя части между RX-чтениями.
Валидный полный ACK пропускается, включая ACK, разделённый RX-событиями,
и ACK перед DECODE_DATA в одном событии. Неполный пакет ожидается в пределах
исходного срока0601; срок не перезапускается, истечение даёт E603. Некорректная
длина/checksum передаётся существующей обработке E601. Отправка SSI ACK перед
CAN DATA/DONE, E602 при отказе отправки и приоритет reset сохранены.
Эта запись уточняет прежнюю формулировку OPEN для всей сборки SSI-пакета.

CODE проверен. HOST фактических исходников PASS: все места разделения пакета
для payload1..128 (тестовый диапазон ёмкости, не измеренный размер штрихкода),
разделённый ACK+DECODE_DATA, побайтовая подача в фактическую задачу,
исходный timeout при неполном пакете, reset во время сборки, повреждённая
checksum и ранее согласованный исходящий ACK. В harness22 группы и дополнительные
сценарии SSI; HAL/RTOS модель, не аппаратная приёмка.
Артефакты: test_artifacts/ssi_packet_assembly_20261030/ — runner, host.c,
extracted.inc, host_result.txt, evidence.json с SHA256 фактических исходников,
build.log, build_evidence.json и stack-usage.

BUILD ассистента: Debug, перекомпиляция задачи и линковка, 0errors/0warnings;
text47804/data68/bss15392. Release NOT RUN. Текущая .su: задача512B,
ScanQueryRevision440B, Scan_CollectPacket168B; сумма задачи и revision952B
не включает остальные вложенные вызовы/контекст и не является верхней оценкой
стека1536B. Runtime heap minimum и stack high-water mark остаются BENCH.

U22 целиком НЕ закрыт. Следующая часть: callback всё ещё перезаписывает
holding_buf/holding_len, GetReceived возвращает снимок без освобождения.
Сборщик не восстанавливает байты, замещённые до чтения задачей.
Предыдущая выдача накопления драйвера/out_overflow не внесена и отложена;
не считать её согласованной готовой реализацией для следующего автоматического
переноса. Сначала объяснить конкретное владение данными, согласовать механизм
сохранения RX-событий и поведение при исчерпании ёмкости. DMA128 остаётся,
кольцевая архитектура и увеличение DMA не утверждены. Разделение пакета через
IDLE возможно по механизму UART, но на сканере стендом не подтверждено.

Сегодня прошивка и аппаратные команды не выполнялись. BENCH остаётся NOT RUN;
целевая настройка EE=1 и валидное чтение реального штрихкода не подтверждены.
Полная унификация платы не объявляется завершённой. Пользователь поручил
обновить отчёты/точку входа и сделать коммит состояния сессии.

## 02.11.2026 — согласован переход UART RX на прерывания

После чтения точки останова30.10 проверены фактические исходники Scanner:
HEAD57740f3, рабочее дерево чистое. SHA25616 исходных файлов и общего config
совпали с evidence.json проверки ssi_packet_assembly_20261030. Это проверка
соответствия сохранённым доказательствам, а не новый запуск HOST/BUILD/BENCH.

В текущем HAL_UARTEx_RxEventCallback данные каждого события записываются
с начала holding_buf, holding_len заменяется. Следующий callback до чтения
задачей может уничтожить предыдущие байты даже при свободном месте в буфере.
ScanUart_GetReceived даёт защищённый снимок без освобождения данных.
Scan_CollectPacket уже собирает Length+2 из дошедших до задачи частей;
утраченные до чтения байты он восстановить не может. Проявление этой потери
на физическом сканере не подтверждено.

Согласовано убрать DMA из RX USART3 и принимать байты по UART-прерыванию.
Драйвер хранит непрочитанные байты до получения задачей и освобождает их
после чтения; задача уведомляется о доступных данных и продолжает сборку
SSI по Length+2 с проверкой checksum. Сборку полного SSI-пакета в ISR
и ожидание mutex в ISR не выбирать. TX byte-DMA/RTS/CTS/wake и его локальные
таймауты сохраняются; скорость9600 8N1 и назначение PB10/PB11 не меняются.

Мотивация: при9600 8N1 максимум960байт/с; RX IRQ — выбранное упрощение
приёма вместо введения кольцевого DMA. Само решение не доказывает отсутствие
потерь: требуется проверить задержку обслуживания UART, запрет IRQ и запас
программного буфера при блокирующих операциях задачи. Конкретные RX API,
организация/ёмкость хранения, обработка нехватки места и UART errors ещё OPEN.
Прежняя выдача накопления с out_overflow не принята автоматически.

План U22 дополнен связанным объёмом: драйвер/API/задача, RX Cube/HAL/IRQ,
сохранение TX DMA, адаптация diagnostics/capture, reset/cancel/terminal gates,
новые CODE/HOST/BUILD и отдельно разрешаемый BENCH. Значения диагностических
полей, зависящих от DMA-событий, требуют явного решения до реализации.
Дополнительно уточнена точка входа исторического implementation plan.

Статус: направление AGREED, новый CODE отсутствует; HOST/BUILD/BENCH нового
RX NOT RUN. В этой работе изменены только документы по поручению пользователя.
Firmware, конфигурация Cube, checkpoints и NEXT_SESSION не изменялись;
коммит, прошивка и аппаратные команды не выполнялись. Консультационный режим
сохраняется: конкретные firmware-правки вносит пользователь, если отдельно
не разрешена определённая правка ассистенту.

### 02.11.2026 — согласованы конкретизации RX-кандидата

Подготовлен отдельный кандидат8 файлов в /tmp/scanner_rx_irq_proposal/;
рабочий Scanner57740f3 не менялся. Пользователь подтвердил линейный
накопитель256B с атомарным потреблением задачей, E602 при ошибке UART или
невозможности сохранить данные без выдачи повреждённого результата, D501
с последним принятым байтом при сохранении сырого capture0603/0604.

HOST кандидата PASS: накопление/чтение/освобождение, PRIMASK, ошибки RX,
capture/diagnostics, reset и фактическая кандидатная задача. Дополнительно
9920 разделений ACK+DECODE_DATA для payload1..128. Это mock HAL/RTOS.
ARM -Wall -Wextra -Werror -fsyntax-only пяти C-файлов PASS; объектная сборка
и линковка не выполнялись. git apply --check PASS, патч не применялся.
После пользовательского внесения нужны аудит фактического diff и полный
Debug BUILD. BENCH отдельно; отсутствие потерь на MCU ещё не подтверждено.

### 02.11.2026 — итоговое согласование RX128B по реальному образцу

Изучено фото artifacts/reagent.jpg: на флаконе ASAT2 под штрихкодом визуально
читается90222260300810701,17цифр. Это подпись на этикетке, а не результат
приёма SSI. Без prefix/suffix такая строка даст DECODE_DATA24B. ACK на START
и ACK на STOP дают ещё12B: штатный цикл RX36B; при двух дополнительных
передачах DECODE_DATA —84B. Отдельный стартовый SCAN_ENABLE ACK добавляет6B.
Источник размеров/повторов: SSI72E-40451-03 RevA §1-7, table1-1 и CMD_ACK/NAK,
https://www.bsr.at/mediafiles/Handbuch/Zebra/DS3600_Simple-Serial-Interface-Programmer-Guide.pdf

Пользователь подтвердил RX USART3 по прерыванию с накопителем непрочитанных
байтов128B. Задача потребляет/освобождает данные по мере поступления и
собирает SSI Length+2; ожидания заполнения буфера или всего цикла нет.
В обработку включается ответ STOP_DECODE. TX byte-DMA/RTS/CTS/wake сохраняется.
Ошибка RX/нехватка места -> согласованный E602 без повреждённого результата;
приоритет reset, D501 last-byte schema1 и сырой capture сохраняются.

128B — ёмкость RX с запасом относительно рассчитанного обмена, не предел
этикетки и не два слота штрихкодов. Максимум прочих этикеток, фактическая
настройка prefix/suffix и SSI-ответ остаются неподтверждёнными. Первоначальный
SSI_MAX_PAYLOAD128 из3e82ec7 не имеет найденного обоснования и сам по себе
не задаёт требование к RX. Расширение251/257/263B не принято.

Прежний кандидат256B также закрывает накопление до исходящего ACK/STOP;
он не соответствует новому объёму и снят с готовности к внесению. Его HOST
и ARM syntax PASS остаются историческими результатами именно того кода.
Для актуального варианта CODE отсутствует, HOST/BUILD/BENCH NOT RUN.

До реализации полного цикла требуется согласовать ожидание и исход отсутствия
ACK/NAK на STOP: текущий CAN terminal уже отправляется до STOP, менять порядок
либо добавлять второй terminal-ответ автоматически нельзя. Обработка повторов
DECODE_DATA/причин NAK не считается принятой только по расчёту памяти.
Дальнейшая выдача — точные места и полные кодовые блоки для ручного внесения
пользователем; формат применения патча отозван. Рабочий Scanner не изменён,
коммит/прошивка/аппаратные команды не выполнялись, NEXT_SESSION не обновлялся.

### 02.11.2026 — внесён переход RX и TX USART3 на UART IT

По итоговому согласованию DMA убран с обеих сторон USART3. Пользователь
внёс RX-драйвер с накопителем128B и новым API BeginRead/EndRead/GetReceived,
обновил цикл чтения задачи и переключил TX на HAL_UART_Transmit_IT(...,1).
Ассистент по отдельному поручению исправил только две связанные вставки:
BeginRead после успешного StartScan; EndRead при отказе START вместо
ScanQueryRevision. Тогда Debug был собран ассистентом:0errors/0warnings,
text47500/data68/bss15264. Эта сборка предшествовала удалению DMA через Cube.

Затем пользователь удалил USART3_RX и USART3_TX DMA requests в Cube.
Сверка .ioc/Core: удалены оба DMA handles, setup/link/deinit, IRQ Channel2/3,
MX_DMA_Init; сохранились PB10/PB11,9600 8N1, flow None и USART3 IRQ6/0.
USER CODE main/MSP сохранён; удалённые DMA IRQ-контейнеры были пустыми.
После пользовательской замены вызова TX на IT сообщена чистая сборка.
Независимого полного BUILD ассистента после этой стадии не было.

Решение TX проверено по локальным HAL_UART_Transmit_IT -> UART_Transmit_IT ->
UART_EndTransmit_IT -> TxCpltCallback и HAL_UART_AbortTransmit. Передача
остаётся побайтовой с прежними RTS/CTS/wake, локальными сроками, reset-gates
и D502 START diagnostics. HAL UART-исходники не менялись.

HOST кандидата RX128B/IT TX PASS в модели: сохранность/освобождение байтов,
snapshot/PRIMASK, границы128B и sticky error, UART NE/ORE/re-arm, отказы API,
capture/D501, reset, TX timeout/abort/late callback.405 разделений ACK+DATA
для payload1..18. Driver/header после пользовательского внесения и правки
комментариев совпали с проверенным кандидатом по C-токенам. Это проверка
драйвера и сборщика, не полного нового цикла задачи. ARM syntax кандидата
PASS; реальные UART/HAL IRQ/scheduler и отсутствие потерь на MCU не проверены.

По поручению «неисправленные комменты замени сам» обновлены только комментарии
в scan_uart_driver.c/.h, task_scan_controller.c и app_safety.c. C-токены
до/после совпали, ради комментариев повторная сборка не выполнялась.

U22 остаётся PARTIAL. Накопление открывается перед START, но нормальное
закрытие после STOP ещё отсутствует; EndRead есть на отказе START, reset
обрабатывается через CancelRx. Сейчас CAN DATA/DONE или NACK выдаётся до
STOP, ответ STOP не ожидается и не разбирается, результат отправки STOP
не используется. Следующий блок требует согласовать срок/исход STOP
ACK/NAK/timeout и порядок с CAN terminal, проверить все выходы RX и повторы.
Согласование приёма полного цикла не является выполненным CODE.

BENCH NOT RUN: прошивка/аппаратные команды не выполнялись. Release не проверен,
физический SSI/штрихкод и runtime stack/heap остаются открытыми. NEXT_SESSION
обновляется в конце сессии по отдельной просьбе. Коммит не выполнялся.

### 02.11.2026 — проверены STOP, reset/reboot и Flash Commit

По поручению пользователя проверены фактические F002/F003/F006,
Dispatcher_PrepareReset, AppSafety latch, Scan_SyncResetState,
ScanUart_CancelRx/Wake/Send, app_flash и аварийный путь.

Выводы, принятые как направление следующей доработки:

- F002 reboot и F006 factory reset: незавершённую операцию внешнего сканера
  нужно управляемо прекратить до отключения обмена/перезагрузки; у F006
  подготовка должна предшествовать Flash. Предусмотреть ограниченный STOP,
  не считать отмену UART на МК доказательством остановки модуля.
- F003: обеспечить отсутствие конфликтующего UART-обмена; выбранное направление
  — BUSY при активном сканировании/capture/обмене. Проверка занятости и допуск
  к Flash должны исключать гонку с queued/in-flight стартом. Автоматически
  прерывать текущую доменную операцию ради Commit не выбрано.
- Watchdog/fault: не вводить ожидание SSI STOP/ACK в terminal-путь.
- После успешного чтения не нужен дополнительный STOP для завершения попытки;
  обязательное ожидание STOP ACK и перенос CAN terminal ради него отозваны.
  Следующая независимая правка U22 — EndRead/итоговая ошибка RX.

Фактические недостатки: F002/F006 сначала устанавливают reset-latch,
запрещающий обычный Wake/Send. Prepared подтверждается после отмены UART,
без отправки SSI STOP. Предел подготовки1s не допускает механического
добавления ранее предложенного2s ожидания STOP. F003 сразу вызывает Commit;
config mutex не обеспечивает покой UART. Эти механизмы ещё не исправлены.

ST PM0075 подтверждает остановку выборки из Flash во время program/erase;
исполняемые из Flash UART handlers в это время также не обслуживаются.
При активном приёме возможен overrun; увеличением программного RX-буфера
это не решается. Источник:
https://www.st.com.cn/resource/en/programming_manual/pm0075-stm32f10xxx-flash-memory-microcontrollers-stmicroelectronics.pdf

SE965HP72E-151566-01 RevA §7-12/7-14: Decode Session Timeout88h имеет
default3.0s, диапазон0.5..9.9s; Level/Pulse выключают лазер при valid decode
или таймауте, Continuous отличается. Эти данные уже присутствовали в отчёте
и должны учитываться до рекомендации STOP. Текущие параметры экземпляра
не считывались; default не является аппаратным результатом.
https://www.scribd.com/document/586071361/51785c-f82347f83adb43908f2c8d95182b60e1-2
SSI table2-88 описывает завершение Decode Mode при successful decode;
STOP_SESSION прекращает незавершённую попытку. ACK не измеряет состояние лазера.
https://www.bsr.at/mediafiles/Handbuch/Zebra/DS3600_Simple-Serial-Interface-Programmer-Guide.pdf

Уточнение исторических §2.1.2 пп.2/4: формулировки «START до STOP» и
«каждый найденный штрихкод без нового START» не использовать как доказательство
непрерывного сканирования в выбранном режиме. Режим и настройки определяют
поведение; эти исторические утверждения не заменяют спецификацию и стенд.

Обязательные STOP ACK после каждого запроса и новое2s ожидание после успеха
сняты с готовности. Направление управляемой отмены перед плановым reset
зафиксировано, но порядок, предел ожидания и исходы отказа ещё требуют
согласованного блока. Прежние HOST reset подтверждают отмену операции МК,
не физическое гашение лазера. Новый CODE/HOST/BUILD/BENCH отсутствует.
Изменены только документы; firmware, .ioc и NEXT_SESSION не менялись.

### 02.11.2026 — завершено закрытие RX перед terminal-результатом

По поручению «скорректируй этот блок сам» ассистент внёс две согласованные
правки в task_scan_controller.c: EndRead после исходящего SSI ACK и до
reset/terminal gates; STOP в конце0601 только если контроллер не DATA_READY.
Отрицательный результат EndRead учитывается в wait_error -> E602, reset
сохраняет приоритет DEVICE_BUSY. После успешного чтения дополнительного STOP
нет; прежняя попытка STOP после неуспеха пока остаётся без проверки ответа.

HOST13 сценариев фактического кода PASS: полный/побайтовый/ACK+DATA приём
17-байтовой строки90222260300810701, checksum, UART/storage errors,
timeout/partial, отказ TX ACK, reset при ожидании/ACK, поздняя ошибка RX
во время ACK/на deadline. Проверены закрытый накопитель на выходе,
единственный CAN terminal и отсутствие STOP после успеха. Это HAL/RTOS модель.

Независимый Debug build ассистента после удаления обоих DMA и новой правки:
make -j4 all, перекомпиляция App sources/линковка,0errors/0warnings,
text45740/data68/bss15128. Артефакты:
[rx_finish_20261102](test_artifacts/rx_finish_20261102/), source hashes,
host.c/extracted.inc/runner/headers, host_result, build.log и SHA256 ELF.
Это не принудительная clean-сборка и не аппаратная приёмка.

F002/F006 управляемая остановка, F003 Flash arbitration, полная политика
NAK/retry и BENCH остаются открытыми. Firmware-правка ограничилась указанным
блоком задачи; .ioc/остальной runtime код в этой итерации не изменялся.
Release, прошивка, аппаратные команды и коммит не выполнялись;
NEXT_SESSION не обновлялся.

### Конец02.11.2026 — следующий блок согласован, сессия завершена

По подтверждению пользователя зафиксирован порядок F002/F006:
немедленный reset-latch/safe GPIO -> запрет обычных команд -> задача Scanner
разбирает прерванный SSI-обмен -> специальный STOP-only путь выключения ->
подтверждённая остановка -> закрытие RX -> prepared -> дальнейший сервис.
Старый ACK не должен подтверждать STOP. Для пробуждения в этом пути выбран
CTS/RTS handshake без AIM; основание — SE965HP table1-2, CTS LOW wake.
Запрет обычных Wake/Send/START после latch сохраняется.

Принят общий бюджет3s на всю подготовку вместо текущего1s; ожидания отдельных
этапов входят в него. При отказе STOP или исчерпании срока не публиковать
prepared и не начинать Flash, сохраняется terminal путь ошибки подготовки.
Аварийные fault/watchdog handlers SSI-ожиданиями не расширяются.

Это согласованный следующий блок, не реализованная функция. Критерий
необходимости STOP, разбор pending/частичных ответов, post-latch RX и точные
API/deadline ещё предстоит подготовить и проверить; обычный TX gate нельзя
просто отключить. Текущий код продолжает1s подготовку с отменой UART без STOP.
Новый CODE/HOST/BUILD/BENCH подготовки отсутствует. За ним остаётся F003
Flash BUSY/исключение конкурентного обмена, ранее согласованное по направлению.

Пользователь завершил рабочий день. NEXT_SESSION обновлён, исторические точки
сохранены. Снимок документов/доказательств и manifest рабочего состояния:
readme/checkpoints/20261102/. HEAD57740f3; рабочие изменения оставлены,
коммит не выполнялся. Последняя проверенная сборка45740/68/15128 и HOST13
относятся к закрытию RX, не новой подготовке. Прошивка/аппаратные команды
не выполнялись, Release/BENCH NOT RUN. Следующая сессия — консультационная;
разовые разрешения сегодняшних конкретных правок на новый код не переносятся.

### 02.11.2026 — дополнительное поручение на коммит

После завершения сессии пользователь поручил сделать коммит, предварительно
сравнив его объём с предыдущим. Предыдущий57740f3 включал runtime/config,
документацию checkpoint и доказательства. Для новой фиксации выбран тот же
объём: UART RX/TX IT, накопитель128B, закрытие RX/условный STOP, генерация Cube,
entry points и снимок20261102. Исполняемый код main.c, кроме удаления DMA,
совпал с базой по C-токенам; пользовательские изменения сохраняются.

Source/checkpoint/ELF hashes совпали с последними доказательствами. Новое
повторное тестирование неизменённого runtime не требовалось: HOST13 PASS,
Debug45740/68/15128 без ошибок/предупреждений. Это не статус всех исторических
тестов проекта и не аппаратная приёмка. Shutdown3s/F003 arbitration не реализованы.
База фиксации57740f3, итоговый идентификатор — git log -1 --oneline.
Прежние записи об отсутствии коммита описывают момент до нового поручения.
Push, прошивка и аппаратные команды не поручались.
