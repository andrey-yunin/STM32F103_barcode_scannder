# DDS-240 Scanner Executor

![MCU](https://img.shields.io/badge/MCU-STM32F103C8T6-03234B)
![RTOS](https://img.shields.io/badge/RTOS-FreeRTOS%20CMSIS--V2-0B6E4F)
![CAN](https://img.shields.io/badge/CAN-1%20Mbit%2Fs-2E6FBB)
![Scanner](https://img.shields.io/badge/Scanner-Zebra%20SE--965HP--I300R-8B0000)

Firmware for the **Scanner Executor** board in the DDS-240 analyzer ecosystem.
The board is an STM32F103-based low-level executor responsible for controlling
the Zebra SE-965HP-I300R laser barcode scanner over SSI protocol via UART and
returning decoded barcode data to the DDS-240 Conductor over CAN.

## Current Status

| Area | Status |
| --- | --- |
| STM32CubeIDE project | Builds locally |
| FreeRTOS task baseline | Implemented |
| CAN transport | Implemented |
| Service commands `F001..F007` | Implemented |
| `app_flash` (config page) | Implemented |
| UART DMA + IDLE driver | Implemented |
| SSI protocol encoder/decoder | Implemented |
| Scan controller state machine | Implemented |
| Board safe-state | Implemented |
| Watchdog task (heartbeat + IWDG) | Implemented |

Latest confirmed build:

```text
STM32CubeIDE Debug build clean after Stage 5 (Watchdog + safe-state).
All firmware code complete. Waiting for physical validation (Stage 6).
```

### 03.08.2026 — выравнивание к канону экосистемы (Stage 6)

Прошивка приведена к канону `DDS-240_ECOSYSTEM_STANDARD.md` §3.2.4/§3.2.5 (сборка Debug — чистая):

- `SET_NODE_ID` (`0xF005`): `new_id` читается из **байта 2** payload; валидация `0x02..0x7F` + `!= CAN_ADDR_CONDUCTOR (0x10)`; запись только в RAM, сохранение через `F003 COMMIT`; NACK `CAN_ERR_INVALID_PARAM 0x0006`; ACK со старого адреса → DONE с нового (task_dispatcher.c:143-156)
- TX source: все исходящие фреймы берут адрес из рантайма `(uint32_t)AppConfig_GetPerformerID()` (5 мест, task_can_handler.c:91,113,138,162,195)
- CAN-фильтр: один permissive-блок (банк 0, маска = IDE-бит `1<<2`, FIFO0) + программная dst-валидация по рантайм-адресу `my_id` (task_can_handler.c:229-246, 296)
- Переименование API: `node_id` → `performer_id`, `Get/SetNodeID` → `Get/SetPerformerID` (uint32_t); default остаётся `0x60`
- `app_flash.h` включён в `task_can_handler.c` (устранено `implicit declaration of 'AppConfig_GetPerformerID'`)

## Hardware Baseline

Target MCU:

- `STM32F103C8T6`
- `SYSCLK = 64 MHz`
- FreeRTOS CMSIS-V2
- bxCAN at `1 Mbit/s`
- USART3 at `9600 8N1` (default SE-965HP, to be confirmed by LA capture)

Scanner hardware:

- `Zebra SE-965HP-I300R` — miniature 1D laser scan engine (decoded)
- Interface: UART TTL 3.3V via SSI protocol
- Connection: RS-232 (4-pin connector) -> MAX3232 -> USART3 (PB10/PB11)
- Trigger: SSI command `START_DECODE` (0xE4), hardware TRIG not on connector

UART pinout:

| Signal | STM32 pin | Scanner side |
| --- | --- | --- |
| `USART3_TX` | `PB10` | RX (scanner input, via MAX3232) |
| `USART3_RX` | `PB11` | TX (scanner output, via MAX3232) |
| `VCC` | - | 3.3V from LM1117-3.3 on scanner board |
| `GND` | - | Common ground |

## Firmware Architecture

Main application layers:

```text
Core/
  STM32Cube HAL startup, interrupts, generated RTOS object creation

App/inc, App/src
  common executor config, queues, flash config, CAN protocol helpers,
  scan_uart_driver, scan_ssi, scan_controller

App/src/tasks
  task_can_handler      bxCAN RX/TX transport owner
  task_dispatcher       service/domain routing and ACK/NACK policy
  task_scan_controller  scan domain task, SSI lifecycle
  task_watchdog         heartbeat supervisor + IWDG refresh
```

RTOS objects:

- tasks: `task_can_handle`, `task_dispatcher`, `task_scan_contr`, `task_watchdog`
- queues: `can_rx_queue`, `can_tx_queue`, `dispatcher_queue`, `scan_queue`
- mutexes: `config_mutex`

Data flow:

```text
UART (SSI) -> DMA circular + IDLE -> holding_buf -> task_scan_controller
                                                      |
                                                      v
                                               CAN_SendDataFragmented
                                                      |
                                                      v
                                               can_tx_queue
                                                      |
                                                      v
                                               task_can_handler -> CAN bus
```

## CAN Executor Contract

DDS-240 executor CAN profile:

- 29-bit Extended ID only
- strict `DLC = 8`
- Conductor address: `0x10`
- Scanner default NodeID: `0x60`
- Scanner device type: `0x05`

Service commands implemented:

| Command | Code | Status |
| --- | --- | --- |
| `GET_DEVICE_INFO` | `0xF001` | implemented |
| `REBOOT` | `0xF002` | implemented |
| `FLASH_COMMIT` | `0xF003` | implemented |
| `GET_UID` | `0xF004` | implemented |
| `SET_NODE_ID` | `0xF005` | implemented |
| `FACTORY_RESET` | `0xF006` | implemented |
| `GET_STATUS` | `0xF007` | implemented |

Scanner domain commands:

| Command | Code | Current behavior |
| --- | --- | --- |
| `SCAN_REQUEST` | `0x0601` | `START_DECODE` -> wait UART (SSI) -> `DATA`/`DONE` or `NACK(TIMEOUT)` |
| `SCAN_GET_DATA` | `0x0602` | Return last barcode from shadow buffer or `NACK(NO_DATA)` |

Normal response pattern:

```text
COMMAND -> ACK -> DATA... -> DONE
COMMAND -> ACK -> NACK
```

## Build

The project is generated for STM32CubeIDE 1.19.x.

From STM32CubeIDE:

1. Import the project.
2. Select the Debug configuration.
3. Build `STM32F103_scanner`.

Headless local build:

```bash
make -C Debug all -j4
```

## Repository Layout

```text
App/
  inc/                 application headers
  src/                 application modules
  src/tasks/           FreeRTOS task entry implementations

Core/
  STM32Cube generated startup, main, interrupts

Drivers/
  STM32 HAL and CMSIS

Middlewares/
  FreeRTOS

readme/
  local workspace documentation pointers
```

## Documentation

This firmware repository contains the publishable firmware project. During local
development, detailed DDS-240 ecosystem documentation is maintained in the
workspace-level `DDS-240_readme` folder. The local pointer file is:

```text
readme/README.md
```

### 27.10.2026 — Scanner UART RX diagnostics (D501 v1)

Implemented optional read-only F020/F021 for SCANNER_UART_RX D501/schema1,
logical UART index0 (USART3). The block contains 40 records: last raw HAL RX
callback prefix (up to128bytes, before SSI decoding), event sequence/tick,
HT/TC/IDLE type, RX-arm status, HAL RxState and accumulated UART errors.
This is the last event snapshot, not a complete scan transcript. Subsequent
callbacks replace it. Existing SSI and scan/re-arm behavior is unchanged.
Diagnostic queue admission is checked with a100ms total budget; DONE is not
queued after DATA admission failure. Physical delivery still requires the
receiver to validate the complete response.

Working Debug full rebuild PASS, no warnings/errors: text40488/data20/bss14776.
Firmware has not been flashed as part of this change. The domain/application
parser in Studio supports D501; Scanner GUI launch remains separate work.

### 27.10.2026 — retained START_DECODE TX (D502 v1)

Added optional D502/index0 beside unchanged D501. It retains the last START
submission bytes (up to16), requested length, HAL return status, gState,
UART errors and a separately associated TxCpltCallback/timestamp. STOP and
ENABLE do not overwrite it. HAL submission and completion do not prove scanner
reception. Full contract and bench status are in the workspace Scanner report.
Working build: text41348/data68/bss14776, no warnings/errors. Not flashed by
assistant. Studio's existing «Диагностика UART (RX/TX)» action reads both blocks.

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

Host tests in tests/test_scan_ssi.c pass with AddressSanitizer/UBSan (leak
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

