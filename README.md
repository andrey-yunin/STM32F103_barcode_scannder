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
