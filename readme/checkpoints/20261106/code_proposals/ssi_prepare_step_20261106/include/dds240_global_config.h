/*
 * dds240_global_config.h
 *
 * Global reference for the DDS-240 Conductor <-> Executor CAN layer.
 *
 * Scope:
 * - H723 Conductor <-> STM32 Executor boards only.
 * - Host -> Conductor commands from User_commands are not part of this file.
 * - Internal executor enums such as CMD_MOVE_RELATIVE are not part of this file.
 *
 * Rule:
 * - This file keeps one naming style: CAN_*.
 * - No compatibility aliases are kept here. Projects must be corrected to the
 *   canonical names below instead of carrying duplicate command names.
 *
 * Contract model:
 * - Do not infer behavior from the prefix alone. Each block below states what
 *   kind of contract item it defines.
 * - Wire command: CAN_CMD_* values sent by the Conductor to an Executor.
 * - Wire response/NACK: CAN_ERR_* values returned by an Executor in NACK.
 * - F007 scalar metric ID: explicitly named CAN_STATUS_* value returned as
 *   metric_id in GET_STATUS DATA frames.
 * - F007 indexed metric range: CAN_STATUS_*_BASE + index returned as metric_id.
 *   In that case BASE is also the channel-0 metric ID.
 * - Namespace range marker: *_BASE/*_LAST allocation boundaries only. The
 *   marker name is not an emitted metric; the same numeric boundary may still
 *   be used by a scalar metric or indexed metric defined in that range.
 * - Payload value/mask: constants used inside command payload fields or inside
 *   F007 metric values. They are not CAN frames by themselves.
 * - Timeout/config hint: implementation guidance, not a wire response.
 * - Engineering diagnostic command: CAN_CMD_SRV_GET_DIAG_* value used by a
 *   service/engineering tool through the Conductor service path. These commands
 *   are read-only unless this file explicitly says otherwise.
 * - Engineering diagnostic block ID: CAN_DIAG_BLOCK_* selector used inside
 *   GET_DIAG_BLOCK payload. It is not a command code and not a F007 metric ID.
 * - Engineering diagnostic item ID: uint16 item_id returned inside a diagnostic
 *   block DATA frame. Item IDs are local to the block schema unless a common
 *   block explicitly reuses common IDs.
 *
 * F007 consumer classes:
 * - Decision metric: the Conductor may use it for command policy, recovery,
 *   degraded/fault state or Host-visible error mapping.
 * - Service context metric: the Conductor may store or show it for service,
 *   bench validation and diagnostics, but recipe logic must not depend on it.
 *
 * Engineering diagnostics:
 * - Read-only service context by default. Engineering diagnostics may be shown
 *   in engineering software and attached to service logs, but normal recipe
 *   logic must not depend on driver internals or raw diagnostic arrays.
 * - A diagnostic block is supported only when the executor advertises it through
 *   CAN_CMD_SRV_GET_DIAG_CAPS. Defining a CAN_DIAG_BLOCK_* value here does not
 *   mean current firmware already implements it.
 *
 * Firmware-local debug counters, driver internals and local logs are not
 * defined here. If a diagnostic is promoted into this file as an emitted F007
 * metric, the Conductor can receive it and must have a defined consumer.
 */

#ifndef CAN_EXECUTOR_GLOBAL_CONFIG_H_
#define CAN_EXECUTOR_GLOBAL_CONFIG_H_

#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Protocol Version                                                           */
/* -------------------------------------------------------------------------- */

/* Reference version of this Conductor -> Executor CAN contract. */
#define CAN_PROTOCOL_STANDARD_MAJOR                  1U
#define CAN_PROTOCOL_STANDARD_MINOR                  8U

/* -------------------------------------------------------------------------- */
/* CAN Transport Profile                                                      */
/* -------------------------------------------------------------------------- */

/* Physical and frame-level profile used by all STM32 Executor boards. */
#define CAN_BITRATE                                  1000000UL
#define CAN_FRAME_DLC                                8U
#define CAN_CLASSIC_MTU                              8U
#define CAN_EXTENDED_ID_REQUIRED                     1U
#define CAN_CONDUCTOR_TO_EXECUTOR_STRICT_DLC         1U

/*
 * DATA frames use byte 0 for subtype and byte 1 for sequence/info.
 * Therefore bytes 2..7 carry up to 6 command-defined data bytes.
 */
#define CAN_DATA_PAYLOAD_MAX                         6U
#define CAN_DATA_SEQ_EOT_MASK                        0x80U
#define CAN_DATA_SEQ_INDEX_MASK                      0x7FU

/* STM32F103 bxCAN reference timing: APB1=32 MHz, 1 Mbit/s, sample point 75%. */
#define CAN_STM32F103_APB1_HZ                        32000000UL
#define CAN_STM32F103_PRESCALER                      2U
#define CAN_STM32F103_BS1_TQ                         11U
#define CAN_STM32F103_BS2_TQ                         4U
#define CAN_STM32F103_SJW_TQ                         1U
#define CAN_STM32F103_SAMPLE_POINT_PERMILLE          750U

/*
 * bxCAN executors must enable TransmitFifoPriority to preserve response order:
 * ACK -> DATA... -> DONE.
 */
#define CAN_BXCAN_TRANSMIT_FIFO_PRIORITY_ENABLE      1U

/* -------------------------------------------------------------------------- */
/* 29-bit Extended CAN ID Layout                                              */
/* -------------------------------------------------------------------------- */

/* Priority field: Extended ID bits 28..26. */
#define CAN_PRIORITY_HIGH                            0U
#define CAN_PRIORITY_NORMAL                          1U

/* Message type field: Extended ID bits 25..24. */
#define CAN_MSG_TYPE_COMMAND                         0U
#define CAN_MSG_TYPE_ACK                             1U
#define CAN_MSG_TYPE_NACK                            2U
#define CAN_MSG_TYPE_DATA_DONE_LOG                   3U

/* Subtype in payload byte 0 when msg_type == CAN_MSG_TYPE_DATA_DONE_LOG. */
#define CAN_SUB_TYPE_DONE                            0x01U
#define CAN_SUB_TYPE_DATA                            0x02U
#define CAN_SUB_TYPE_LOG                             0x03U

/* Reserved node addresses in Extended ID dst/src fields. */
#define CAN_ADDR_BROADCAST                           0x00U
#define CAN_ADDR_HOST                                0x01U
#define CAN_ADDR_CONDUCTOR                           0x10U

/*
 * Executor address allocation (2026-10-13).
 * The upper nibble selects a board family; the lower nibble selects one of
 * 16 instances (0 = first board). Local channel selectors remain separate.
 * DeviceType is still reported by F001 and checked against the expected family;
 * UID identifies the individual MCU. DeviceType is NOT derived arithmetically
 * from the upper nibble: Motion=0x01, Fluidic=0x03, Thermo=0x02, etc.
 *
 * F005 and Flash configuration validation must both enforce the board's OWN
 * BASE..LAST range. The global MIN..MAX bounds alone do not authorize an address.
 * Addresses below 0x20 are not assignable to executors under this allocation.
 * Address uniqueness must be ensured during provisioning; a range check cannot
 * detect another board already using the requested NodeID.
 */
#define CAN_ADDR_MOTOR_BOARD_BASE                    0x20U
#define CAN_ADDR_MOTOR_BOARD_LAST                    0x2FU
#define CAN_ADDR_PUMP_BOARD_BASE                     0x30U
#define CAN_ADDR_PUMP_BOARD_LAST                     0x3FU
#define CAN_ADDR_THERMO_BOARD_BASE                   0x40U
#define CAN_ADDR_THERMO_BOARD_LAST                   0x4FU
#define CAN_ADDR_PHOTOMETER_BOARD_BASE               0x50U
#define CAN_ADDR_PHOTOMETER_BOARD_LAST               0x5FU
#define CAN_ADDR_SCANNER_BOARD_BASE                  0x60U
#define CAN_ADDR_SCANNER_BOARD_LAST                  0x6FU
#define CAN_ADDR_LLD_BOARD_BASE                      0x70U
#define CAN_ADDR_LLD_BOARD_LAST                      0x7FU
#define CAN_ADDR_HEATER_COOLER_BOARD_BASE            0x80U
#define CAN_ADDR_HEATER_COOLER_BOARD_LAST            0x8FU

/* Sensor board: optical inputs and reed switches; assigned 2026-10-13.
 * F005 and Flash validation accept this range only for the Sensor family.
 */
#define CAN_ADDR_SENSOR_BOARD_BASE                   0x90U
#define CAN_ADDR_SENSOR_BOARD_LAST                   0x9FU

/* Factory defaults: the first address in each assigned family. */
#define CAN_ADDR_MOTOR_BOARD                         CAN_ADDR_MOTOR_BOARD_BASE
#define CAN_ADDR_PUMP_BOARD                          CAN_ADDR_PUMP_BOARD_BASE
#define CAN_ADDR_THERMO_BOARD                        CAN_ADDR_THERMO_BOARD_BASE
#define CAN_ADDR_PHOTOMETER_BOARD                    CAN_ADDR_PHOTOMETER_BOARD_BASE
#define CAN_ADDR_SCANNER_BOARD                       CAN_ADDR_SCANNER_BOARD_BASE
#define CAN_ADDR_LLD_BOARD                           CAN_ADDR_LLD_BOARD_BASE
#define CAN_ADDR_HEATER_COOLER_BOARD                 CAN_ADDR_HEATER_COOLER_BOARD_BASE

#define CAN_ADDR_SENSOR_BOARD                       CAN_ADDR_SENSOR_BOARD_BASE

/* Allocation envelope for assigned families; not an F005 allow-list. */
#define CAN_DYNAMIC_NODE_ID_MIN                      CAN_ADDR_MOTOR_BOARD_BASE
#define CAN_DYNAMIC_NODE_ID_MAX                      CAN_ADDR_SENSOR_BOARD_LAST

#define CAN_BUILD_ID(priority, msg_type, dst_addr, src_addr) \
	((uint32_t)((((uint32_t)(priority) & 0x07UL) << 26) | \
	            (((uint32_t)(msg_type) & 0x03UL) << 24) | \
	            (((uint32_t)(dst_addr)  & 0xFFUL) << 16) | \
	            (((uint32_t)(src_addr)  & 0xFFUL) << 8)))

#define CAN_GET_PRIORITY(id)                         ((uint8_t)(((id) >> 26) & 0x07U))
#define CAN_GET_MSG_TYPE(id)                         ((uint8_t)(((id) >> 24) & 0x03U))
#define CAN_GET_DST_ADDR(id)                         ((uint8_t)(((id) >> 16) & 0xFFU))
#define CAN_GET_SRC_ADDR(id)                         ((uint8_t)(((id) >> 8)  & 0xFFU))

/* -------------------------------------------------------------------------- */
/* Device Type IDs                                                            */
/* -------------------------------------------------------------------------- */

/*
 * Returned by common service command CAN_CMD_SRV_GET_DEVICE_INFO.
 * These values identify executor board class, not CAN NodeID.
 */
#define CAN_DEVICE_TYPE_MOTION                       0x01U
#define CAN_DEVICE_TYPE_THERMO                       0x02U
#define CAN_DEVICE_TYPE_FLUIDIC                      0x03U
#define CAN_DEVICE_TYPE_PHOTOMETER                   0x04U
#define CAN_DEVICE_TYPE_SCANNER                      0x05U
#define CAN_DEVICE_TYPE_LLD               0x06U
#define CAN_DEVICE_TYPE_HEATER_COOLER     0x07U
#define CAN_DEVICE_TYPE_SENSOR                       0x08U

/* Default channel counts reported or assumed by executor classes. */
#define CAN_MOTION_CHANNELS_DEFAULT                  8U
#define CAN_THERMO_CHANNELS_DEFAULT                  8U
#define CAN_FLUIDICS_PUMPS_DEFAULT                   13U
#define CAN_FLUIDICS_VALVES_DEFAULT                  3U
#define CAN_FLUIDICS_CHANNELS_DEFAULT                16U
#define CAN_PHOTOMETER_CHANNELS_DEFAULT              8U
#define CAN_SCANNER_CHANNELS_DEFAULT                 1U
#define CAN_LLD_CHANNELS_DEFAULT          1U
#define CAN_HEATER_COOLER_CHANNELS_DEFAULT 4U

/* Sensor F001 capacity: 18 optical inputs + 2 reed-switch inputs.
 * Installed/required inputs are selected separately by configuration;
 * this count does not report connected or healthy sensors.
 */
#define CAN_SENSOR_CHANNELS_DEFAULT                  20U

/* -------------------------------------------------------------------------- */
/* Common Executor Service Contract: Any Executor Board                        */
/* -------------------------------------------------------------------------- */

/*
 * Common service commands are accepted by all executor boards.
 * Payload bytes 0..1 contain command code in little-endian.
 *
 * Service commands are not Host recipe steps. Their results are consumed by
 * the Conductor service/inventory/recovery layer, not by JobManager as process
 * data.
 */

/* Wire command: service/inventory. Returns board class, firmware and inventory data. */
#define CAN_CMD_SRV_GET_DEVICE_INFO                  0xF001U

/* Wire command: protected service action. Reboots executor after key validation. */
#define CAN_CMD_SRV_REBOOT                           0xF002U

/* Wire command: protected service action. Persists RAM config to executor Flash. */
#define CAN_CMD_SRV_FLASH_COMMIT                     0xF003U

/* Wire command: service/inventory. Returns full MCU UID for inventory binding. */
#define CAN_CMD_SRV_GET_UID                          0xF004U

/*
 * Wire command: changes the runtime NodeID within the executor family's range.
 * Payload byte 2 is the new NodeID; bytes 3..7 are zero padding.
 * Invalid address: CAN_ERR_INVALID_PARAM. ACK uses the old address; update the
 * direct filter before DONE from the new address. F003 persists the change.
 */
#define CAN_CMD_SRV_SET_NODE_ID                      0xF005U

/* Wire command: protected service action. Clears persistent executor configuration. */
#define CAN_CMD_SRV_FACTORY_RESET                    0xF006U

/*
 * Wire command: service/status plane.
 * Returns ACK -> DATA(metric_id/value)... -> DONE, or ACK -> NACK(error_code).
 * Common status metrics and known device-type Decision metrics may drive
 * recovery/fault decisions. Service context metrics may be stored but must not
 * drive recipe logic.
 */
#define CAN_CMD_SRV_GET_STATUS                       0xF007U

/*
 * Payload values/magic keys for protected service commands.
 * REBOOT and FACTORY_RESET must reject requests with wrong key.
 */
#define SRV_MAGIC_REBOOT                             0x55AAU
#define SRV_MAGIC_FACTORY_RESET                      0xDEADU

/* -------------------------------------------------------------------------- */
/* Engineering Diagnostics Service Contract: Optional Read-only Plane          */
/* -------------------------------------------------------------------------- */

/*
 * These commands belong to the service/engineering plane, not to Host user
 * workflow and not to recipe execution.
 *
 * Current implementation status:
 * - Optional contract: Motion implements F020/F021 MOTION_TMC schema v1
 *   for motor 0 startup diagnostics as of 2026-10-26; bench validation pending.
 * - An executor supports engineering diagnostics only after it implements these
 *   commands and advertises concrete blocks through GET_DIAG_CAPS.
 * - Existing F007 common health metrics remain the current implemented health
 *   path for Motion, Fluidics, Thermo and Photometer.
 *
 * Read-only rule:
 * - GET_DIAG_CAPS and GET_DIAG_BLOCK must not start motors, pumps, valves,
 *   heaters, lights, ADC scans or calibration.
 * - They must not modify mapping, calibration, runtime state, driver config or
 *   persistent storage.
 * - The only expected side effect is normal CAN transport counter movement
 *   caused by request/response traffic.
 */

/* Wire command: read-only discovery of supported CAN_DIAG_BLOCK_* selectors. */
#define CAN_CMD_SRV_GET_DIAG_CAPS                    0xF020U

/* Wire command: read-only snapshot of one advertised diagnostic block. */
#define CAN_CMD_SRV_GET_DIAG_BLOCK                   0xF021U

/*
 * GET_DIAG_CAPS response DATA payload:
 * byte 0: CAN_SUB_TYPE_DATA
 * byte 1: sequence
 * byte 2..3: block_id:uint16 little-endian
 * byte 4: schema_version
 * byte 5: CAN_DIAG_CAP_FLAG_* bitmask
 * byte 6..7: reserved, zero
 *
 * GET_DIAG_BLOCK request payload:
 * byte 0..1: CAN_CMD_SRV_GET_DIAG_BLOCK little-endian
 * byte 2..3: block_id:uint16 little-endian
 * byte 4: index selector, usually channel/motor index or CAN_DIAG_INDEX_BOARD
 * byte 5: request flags, zero for baseline read
 * byte 6..7: reserved, zero
 *
 * GET_DIAG_BLOCK response DATA payload:
 * byte 0: CAN_SUB_TYPE_DATA
 * byte 1: sequence
 * byte 2..3: item_id:uint16 little-endian, local to block schema
 * byte 4..7: value:uint32 little-endian
 *
 * Arrays are represented by repeated item_id/value records according to the
 * block schema. Large binary dumps are intentionally out of scope for this
 * baseline.
 */
#define CAN_DIAG_CAP_FLAG_READ_ONLY                  0x01U
#define CAN_DIAG_CAP_FLAG_INDEXED                    0x02U
#define CAN_DIAG_CAP_FLAG_EXPERIMENTAL               0x80U

#define CAN_DIAG_INDEX_BOARD                         0x00U
#define CAN_DIAG_INDEX_ALL                           0xFFU

/*
 * Diagnostic block namespace.
 *
 * These values are request selectors for CAN_CMD_SRV_GET_DIAG_BLOCK. They are
 * not CAN command codes, not F007 metric IDs and not NACK/error codes.
 * A selector is valid for a specific executor only if GET_DIAG_CAPS advertises
 * it for that executor firmware/schema.
 */
#define CAN_DIAG_BLOCK_COMMON_BASE                   0xD000U
#define CAN_DIAG_BLOCK_COMMON_LAST                   0xD0FFU
#define CAN_DIAG_BLOCK_MOTION_BASE                   0xD100U
#define CAN_DIAG_BLOCK_MOTION_LAST                   0xD1FFU
#define CAN_DIAG_BLOCK_FLUIDICS_BASE                 0xD200U
#define CAN_DIAG_BLOCK_FLUIDICS_LAST                 0xD2FFU
#define CAN_DIAG_BLOCK_THERMO_BASE                   0xD300U
#define CAN_DIAG_BLOCK_THERMO_LAST                   0xD3FFU
#define CAN_DIAG_BLOCK_PHOTOMETER_BASE               0xD400U
#define CAN_DIAG_BLOCK_PHOTOMETER_LAST               0xD4FFU

#define CAN_DIAG_BLOCK_SCANNER_BASE                  0xD500U
#define CAN_DIAG_BLOCK_SCANNER_LAST                  0xD5FFU
/* Scanner-specific raw HAL RX snapshot; optional, advertised by F020 only
 * after firmware implementation. Schema: Scanner/SCANNER_EXECUTOR_REPORT.md. */
#define CAN_DIAG_BLOCK_SCANNER_UART_RX               0xD501U
#define CAN_DIAG_SCANNER_UART_SCHEMA                 1U
#define CAN_DIAG_BLOCK_SCANNER_UART_TX_START          0xD502U
#define CAN_DIAG_SCANNER_UART_TX_SCHEMA               1U


#define CAN_DIAG_BLOCK_LLD_BASE           0xD600U
#define CAN_DIAG_BLOCK_LLD_LAST           0xD6FFU

/* LLD engineering diagnostic blocks. */
#define CAN_DIAG_BLOCK_LLD_STATE                  0xD601U
#define CAN_DIAG_BLOCK_LLD_MEASUREMENT            0xD602U
#define CAN_DIAG_BLOCK_LLD_FILTER                 0xD603U

/*
 * Common blocks: optional for all executors.
 * COMMON_HEALTH may mirror F007 common status metrics for a uniform engineering
 * UI, but F007 remains the canonical implemented health command.
 */
#define CAN_DIAG_BLOCK_COMMON_HEALTH                 0xD001U
#define CAN_DIAG_BLOCK_COMMON_RTOS                   0xD002U
#define CAN_DIAG_BLOCK_COMMON_RESET                  0xD003U

/* Motion engineering diagnostic blocks. */
#define CAN_DIAG_BLOCK_MOTION_STATE                  0xD101U
#define CAN_DIAG_BLOCK_MOTION_QUEUE                  0xD102U
#define CAN_DIAG_BLOCK_MOTION_TMC                    0xD103U
#define CAN_DIAG_BLOCK_MOTION_LIMITS                 0xD104U
#define CAN_DIAG_BLOCK_MOTION_FAULTS                 0xD105U
/* D106 v1: last completed UART exchange, twelve scalar records (see protocol doc). */
#define CAN_DIAG_BLOCK_MOTION_UART_TRACE 0xD106U
#define CAN_DIAG_MOTION_UART_TRACE_SCHEMA_VERSION 1U


/* MOTION_TMC schema v1: startup snapshot, index=motor_id; current implementation index 0 only.
 * DATA sequence is 0..N-1, EOT (0x80) only on the last record; DONE selector=0.
 * F020 request bytes 2..7 are zero. F021 flags/reserved are zero.
 * The block reads RAM only: no UART writes, retries or register reads on request.
 * Register values are meaningful only when the matching VALID_MASK bit is set.
 */
#define CAN_DIAG_MOTION_TMC_SCHEMA_VERSION          1U
#define CAN_DIAG_TMC_ITEM_MOTOR_ID                  0x0001U
#define CAN_DIAG_TMC_ITEM_STATE                     0x0002U
#define CAN_DIAG_TMC_ITEM_READY                     0x0003U
#define CAN_DIAG_TMC_ITEM_STAGE                     0x0004U
#define CAN_DIAG_TMC_ITEM_HAL_STATUS                0x0005U
#define CAN_DIAG_TMC_ITEM_VALID_MASK                0x0006U
#define CAN_DIAG_TMC_ITEM_IOIN                      0x0007U
#define CAN_DIAG_TMC_ITEM_GCONF                     0x0008U
#define CAN_DIAG_TMC_ITEM_CHOPCONF                  0x0009U
#define CAN_DIAG_TMC_ITEM_IFCNT                     0x000AU
#define CAN_DIAG_TMC_ITEM_GSTAT                     0x000BU
#define CAN_DIAG_TMC_ITEM_DRV_STATUS                0x000CU
#define CAN_DIAG_TMC_STATE_NOT_STARTED             0U
#define CAN_DIAG_TMC_STATE_CONFIGURING             1U
#define CAN_DIAG_TMC_STATE_READY                   2U
#define CAN_DIAG_TMC_STATE_FAILED                  3U
#define CAN_DIAG_TMC_VALID_IOIN                    (1U << 0)
#define CAN_DIAG_TMC_VALID_GCONF                   (1U << 1)
#define CAN_DIAG_TMC_VALID_CHOPCONF                (1U << 2)
#define CAN_DIAG_TMC_VALID_IFCNT                   (1U << 3)
#define CAN_DIAG_TMC_VALID_GSTAT                   (1U << 4)
#define CAN_DIAG_TMC_VALID_DRV_STATUS              (1U << 5)
/* STAGE: 0 not started, 1 handle init, 2 IOIN/version/ENN, 3 NODECONF,
 * 4 GCONF read, 5 GCONF write, 6 GCONF verification, 7 IHOLD_IRUN,
 * 8 TPOWERDOWN, 9 VACTUAL, 10 CHOPCONF read, 11 CHOPCONF write,
 * 12 microstep setup, 13 CHOPCONF verification, 14 IFCNT, 15 GSTAT,
 * 16 DRV_STATUS, 17 complete. HAL_STATUS: 0 OK, 1 ERROR, 2 BUSY, 3 TIMEOUT.
 * STATE determines startup outcome; during CONFIGURING HAL_STATUS may be from
 * the preceding operation. READY is configuration readiness, not physical motion.
 */


/* Fluidics engineering diagnostic blocks. */
#define CAN_DIAG_BLOCK_FLUIDICS_STATE                0xD201U
#define CAN_DIAG_BLOCK_FLUIDICS_TIMERS               0xD202U
#define CAN_DIAG_BLOCK_FLUIDICS_MAPPING              0xD203U
#define CAN_DIAG_BLOCK_FLUIDICS_IO                   0xD204U
#define CAN_DIAG_BLOCK_FLUIDICS_FAULTS               0xD205U

/* Thermo engineering diagnostic blocks. */
#define CAN_DIAG_BLOCK_THERMO_SUMMARY                0xD301U
#define CAN_DIAG_BLOCK_THERMO_1WIRE_SCAN             0xD302U
#define CAN_DIAG_BLOCK_THERMO_CHANNEL_MAP            0xD303U
#define CAN_DIAG_BLOCK_THERMO_RAW                    0xD304U
#define CAN_DIAG_BLOCK_THERMO_BUS                    0xD305U

/* Photometer engineering diagnostic blocks. */
#define CAN_DIAG_BLOCK_PHOTOMETER_SUMMARY            0xD401U
#define CAN_DIAG_BLOCK_PHOTOMETER_CALIBRATION        0xD402U
#define CAN_DIAG_BLOCK_PHOTOMETER_ADC                0xD403U
#define CAN_DIAG_BLOCK_PHOTOMETER_MCP                0xD404U
#define CAN_DIAG_BLOCK_PHOTOMETER_SPI                0xD405U
#define CAN_DIAG_BLOCK_PHOTOMETER_LIGHT              0xD406U

/*
 * No CLEAR_DIAG_COUNTERS command is allocated in this read-only baseline.
 * Counter reset, active self-test and FORCE/SET operations must be designed as
 * separate protected service actions, not as part of read-only diagnostics.
 */

/* -------------------------------------------------------------------------- */
/* Common Diagnostics: F007 Metric Contract                                   */
/* -------------------------------------------------------------------------- */

/*
 * F007 has one external response path:
 *
 * Conductor -> Executor:
 * byte 0..1: CAN_CMD_SRV_GET_STATUS little-endian
 * byte 2:    device_id, normally 0 for board-level service requests
 * byte 3..7: reserved, must be zero unless a future standard defines them
 *
 * Executor -> Conductor:
 * ACK -> DATA(metric_id/value)... -> DONE
 * or ACK -> NACK(error_code)
 *
 * Only blocks labeled "F007 scalar metric ID" or "F007 indexed metric range"
 * define emitted DATA metric IDs.
 * Each emitted F007 metric block must state whether it is a Decision metric or
 * a Service context metric.
 *
 * Blocks labeled "Namespace range marker" reserve numeric space only. Consumers
 * must not treat the marker name as a metric. If a boundary value is emitted,
 * the emitted metric is named separately as a scalar metric or indexed range.
 *
 * There are no firmware-local "log-only" CAN_STATUS_* metrics in this file.
 */

/* -------------------------------------------------------------------------- */
/* Common NACK Error Codes: Any Executor Board                                */
/* -------------------------------------------------------------------------- */

/*
 * Common NACK values are global and have the same meaning for every executor.
 * Domain NACK values must be globally unique; the Conductor must not infer a
 * different meaning for the same numeric value from source NodeID or command.
 */
#define CAN_ERR_NONE                                 0x0000U
#define CAN_ERR_UNKNOWN_CMD                          0x0001U
#define CAN_ERR_INVALID_DEVICE_ID                    0x0002U
#define CAN_ERR_DEVICE_BUSY                          0x0003U
#define CAN_ERR_INVALID_KEY                          0x0004U
#define CAN_ERR_FLASH_WRITE                          0x0005U
#define CAN_ERR_INVALID_PARAM                        0x0006U
#define CAN_NACK_DOMAIN_MOTION_BASE                  0xE200U
#define CAN_NACK_DOMAIN_FLUIDICS_BASE                0xE300U
#define CAN_NACK_DOMAIN_THERMO_BASE                  0xE400U
#define CAN_NACK_DOMAIN_PHOTOMETER_BASE              0xE500U

/* -------------------------------------------------------------------------- */
/* Common GET_STATUS Metrics: Any Executor Board                              */
/* -------------------------------------------------------------------------- */

/*
 * This section contains F007 metric namespace markers and emitted metric IDs.
 *
 * Emitted DATA frame payload format:
 * metric_id:uint16 little-endian + value:uint32 little-endian.
 *
 * Promotion rule:
 * - a metric is added to common status only when the Conductor has a defined
 *   consumer and action for it;
 * - device-type domain action metrics must be compact summaries;
 * - raw arrays, HAL/register details, driver counters and mutex/resource
 *   internals do not become Decision metrics just because they are transported
 *   in F007; the Conductor may store them as Service context metrics.
 *
 * Backward compatibility:
 * - consumers must parse metric_id/value until DONE;
 * - consumers must not depend on a fixed DATA frame count or final sequence byte;
 * - baseline/delta storage should be keyed by node_id + uid.
 */

/*
 * Namespace range markers for the F007 metric-id space.
 * Marker names are allocation boundaries only. Some boundary numeric values are
 * also emitted by scalar metrics named below.
 */
#define CAN_STATUS_COMMON_ACTION_BASE                0x0001U
#define CAN_STATUS_COMMON_ACTION_LAST                0x001FU
#define CAN_STATUS_COMMON_EXT_BASE                   0x0020U
#define CAN_STATUS_COMMON_EXT_LAST                   0x00FFU
#define CAN_STATUS_COMMON_RESERVED_BASE              0x0100U
#define CAN_STATUS_COMMON_RESERVED_LAST              0x0FFFU

#define CAN_STATUS_DOMAIN_METRIC_BASE                0x1000U
#define CAN_STATUS_DOMAIN_METRIC_LAST                0x7FFFU

/*
 * F007 scalar metric IDs: common Decision metrics.
 *
 * These are mandatory for industrial STM32 Executor baselines. Conductor may
 * use deltas of these metrics for transport health, discovery, recovery and
 * degraded/fault decisions.
 */
#define CAN_STATUS_RX_TOTAL                          0x0001U
#define CAN_STATUS_TX_TOTAL                          0x0002U
#define CAN_STATUS_RX_QUEUE_OVERFLOW                 0x0003U
#define CAN_STATUS_TX_QUEUE_OVERFLOW                 0x0004U
#define CAN_STATUS_DISPATCHER_OVERFLOW               0x0005U
#define CAN_STATUS_DROP_NOT_EXT                      0x0006U
#define CAN_STATUS_DROP_WRONG_DST                    0x0007U
#define CAN_STATUS_DROP_WRONG_TYPE                   0x0008U
#define CAN_STATUS_DROP_WRONG_DLC                    0x0009U
#define CAN_STATUS_TX_MAILBOX_TIMEOUT                0x000AU
#define CAN_STATUS_TX_HAL_ERROR                      0x000BU
#define CAN_STATUS_ERROR_CALLBACK                    0x000CU
#define CAN_STATUS_ERROR_WARNING                     0x000DU
#define CAN_STATUS_ERROR_PASSIVE                     0x000EU
#define CAN_STATUS_BUS_OFF                           0x000FU
#define CAN_STATUS_LAST_HAL_ERROR                    0x0010U
#define CAN_STATUS_LAST_ESR                          0x0011U
#define CAN_STATUS_APP_QUEUE_OVERFLOW                0x0012U

/*
 * Common status action notes:
 * - dropped_wrong_dlc/not_ext/wrong_type: protocol or packer fault; do not
 *   blindly retry the same malformed frame.
 * - dropped_wrong_dst: stale NodeID, wrong routing or discovery mismatch.
 * - queue overflows: executor overloaded/degraded; reduce request rate or fault.
 * - tx mailbox/HAL/CAN warning/passive/bus-off: CAN recovery/degraded policy.
 * - app_queue_overflow: domain task overloaded; stop or retry by command policy.
 */

/* -------------------------------------------------------------------------- */
/* Motion Executor: CAN_ADDR_MOTOR_BOARD                                      */
/* -------------------------------------------------------------------------- */

/*
 * Wire commands: Motion lifecycle/control.
 * The Conductor uses DONE/NACK/timeout to advance or fail the current motion
 * step.
 *
 * Current STM32F103_step_motors_refactored F007 emitted contract:
 * - common Decision metrics 0x0001..0x0012 only;
 * - no Motion domain F007 metrics are emitted today.
 *
 * Motion command outcome is handled by ACK/DONE/NACK/timeout.
 * Motion F007 is used for executor/CAN health diagnostics only.
 */
#define CAN_CMD_MOTOR_ROTATE                         0x0101U
#define CAN_CMD_MOTOR_HOME                           0x0102U
#define CAN_CMD_MOTOR_START_CONTINUOUS               0x0103U
#define CAN_CMD_MOTOR_STOP                           0x0104U

/* Motion aliases for common NACK values; motor_id is a channel/resource id. */
#define CAN_ERR_INVALID_MOTOR_ID                     CAN_ERR_INVALID_DEVICE_ID
#define CAN_ERR_MOTOR_BUSY                           CAN_ERR_DEVICE_BUSY

/* -------------------------------------------------------------------------- */
/* Fluidics Executor: CAN_ADDR_PUMP_BOARD                                     */
/* -------------------------------------------------------------------------- */

/*
 * Wire commands: Fluidics lifecycle/control.
 * Recipe dosing must use PUMP_RUN_DURATION. PUMP_START/STOP and VALVE
 * state commands are service/manual primitives with local safety timeouts.
 *
 * Current STM32F103_pumps_valves F007 emitted contract:
 * - common Decision metrics 0x0001..0x0012 only;
 * - no Fluidics domain F007 metrics are emitted today.
 *
 * Fluidics command outcome is handled by ACK/DONE/NACK/timeout.
 * Fluidics F007 is used for executor/CAN health diagnostics only.
 */
#define CAN_CMD_PUMP_RUN_DURATION                    0x0201U
#define CAN_CMD_PUMP_START                           0x0202U
#define CAN_CMD_PUMP_STOP                            0x0203U
#define CAN_CMD_VALVE_OPEN                           0x0204U
#define CAN_CMD_VALVE_CLOSE                          0x0205U

/* Timeout/config hints: executor-local safety defaults for state commands. */
#define CAN_FLUIDICS_DEFAULT_PUMP_TIMEOUT_MS         60000UL
#define CAN_FLUIDICS_DEFAULT_VALVE_TIMEOUT_MS        300000UL

/* -------------------------------------------------------------------------- */
/* Photometer Executor: CAN_ADDR_PHOTOMETER_BOARD                             */
/* -------------------------------------------------------------------------- */

/*
 * Wire commands: Photometer lifecycle/service.
 * Photometer follows the executor channel/resource convention:
 * command byte 2 is wavelength channel id 0..7. Host wavelength masks are
 * expanded by the Conductor into channel-level low-level transactions.
 *
 * PHOTOMETER_SCAN returns one DATA frame and DONE on successful single-channel
 * acquisition or NACK on fault. PHOTOMETER_CALIBRATE owns one wavelength
 * channel calibration lifecycle. GET_WAVELENGTH returns one wavelength value.
 *
 * Current STM32F103_photometer F007 emitted contract:
 * - common Decision metrics 0x0001..0x0012 only;
 * - no Photometer domain F007 metrics are emitted today.
 *
 * Photometer command outcome is handled by ACK/DONE/NACK/timeout and command
 * DATA frames. ADS/MCP/raw ADC/gain internals belong to a future cross-executor
 * service/debug API, not to the common F007 health contract.
 */
#define CAN_CMD_PHOTOMETER_SCAN                      0x0401U
#define CAN_CMD_PHOTOMETER_CALIBRATE                 0x0402U
#define CAN_CMD_PHOTOMETER_GET_WAVELENGTH            0x0403U

#define CAN_PHOTOMETER_CHANNEL_COUNT                 8U
#define CAN_PHOTOMETER_CHANNEL_340                   0U
#define CAN_PHOTOMETER_CHANNEL_405                   1U
#define CAN_PHOTOMETER_CHANNEL_450                   2U
#define CAN_PHOTOMETER_CHANNEL_510                   3U
#define CAN_PHOTOMETER_CHANNEL_546                   4U
#define CAN_PHOTOMETER_CHANNEL_578                   5U
#define CAN_PHOTOMETER_CHANNEL_630                   6U
#define CAN_PHOTOMETER_CHANNEL_670                   7U

/* Photometer domain NACK codes: globally unique 0xE500..0xE5FF values. */
#define CAN_ERR_PHOT_CALIBRATION_REQUIRED            0xE500U
#define CAN_ERR_PHOT_ADC_OVER_RANGE                  0xE501U
#define CAN_ERR_PHOT_ADC_UNDER_RANGE                 0xE502U
#define CAN_ERR_PHOT_GAIN_LIMIT                      0xE503U
#define CAN_ERR_PHOT_BAD_REFERENCE                   0xE504U
#define CAN_ERR_PHOT_LAMP_NOT_STABLE                 0xE505U
#define CAN_ERR_PHOT_ADC_ERROR                       0xE506U
#define CAN_ERR_PHOT_MCP_ERROR                       0xE507U
#define CAN_ERR_PHOT_SPI_ERROR                       0xE508U
#define CAN_ERR_PHOT_CALIBRATION_FAILED              0xE509U
#define CAN_ERR_PHOT_UNSUPPORTED_WAVELENGTH          0xE50AU
#define CAN_ERR_PHOT_NOT_READY                       0xE50BU

/*
 * Host/Conductor wavelength selection masks.
 * Low-level 0x0401/0x0402/0x0403 commands use wavelength channel id in byte 2;
 * masks are expanded by the Conductor before sending executor commands.
 */
#define CAN_PHOTOMETER_WL_340_MASK                   0x01U
#define CAN_PHOTOMETER_WL_405_MASK                   0x02U
#define CAN_PHOTOMETER_WL_450_MASK                   0x04U
#define CAN_PHOTOMETER_WL_510_MASK                   0x08U
#define CAN_PHOTOMETER_WL_546_MASK                   0x10U
#define CAN_PHOTOMETER_WL_578_MASK                   0x20U
#define CAN_PHOTOMETER_WL_630_MASK                   0x40U
#define CAN_PHOTOMETER_WL_670_MASK                   0x80U
#define CAN_PHOTOMETER_WL_ALL_MASK                   0xFFU

/* Timeout/config hints: Conductor operation limits, validated later on optical bench. */
#define CAN_PHOTOMETER_SCAN_TIMEOUT_MS               250UL
#define CAN_PHOTOMETER_CALIBRATE_CHANNEL_TIMEOUT_MS  1000UL
#define CAN_PHOTOMETER_CALIBRATE_TOTAL_TIMEOUT_MS    10000UL

/* Payload values: Photometer runtime state. These are not metric IDs. */
#define CAN_PHOTOMETER_STATE_INIT                    0U
#define CAN_PHOTOMETER_STATE_READY                   1U
#define CAN_PHOTOMETER_STATE_BUSY_SCAN               2U
#define CAN_PHOTOMETER_STATE_BUSY_CALIBRATE          3U
#define CAN_PHOTOMETER_STATE_NEED_CALIBRATION        4U
#define CAN_PHOTOMETER_STATE_FAULT                   5U
#define CAN_PHOTOMETER_STATE_SAFE_STATE              6U

/* Payload values: Photometer scan status. These are not metric IDs. */
#define CAN_PHOTOMETER_SCAN_STATUS_OK                0U
#define CAN_PHOTOMETER_SCAN_STATUS_CAL_RECOMMENDED   1U
#define CAN_PHOTOMETER_SCAN_STATUS_CAL_REQUIRED      2U
#define CAN_PHOTOMETER_SCAN_STATUS_ADC_OVER_RANGE    3U
#define CAN_PHOTOMETER_SCAN_STATUS_ADC_UNDER_RANGE   4U
#define CAN_PHOTOMETER_SCAN_STATUS_LAMP_FAULT        5U
#define CAN_PHOTOMETER_SCAN_STATUS_ADC_ERROR         6U
#define CAN_PHOTOMETER_SCAN_STATUS_MCP_ERROR         7U
#define CAN_PHOTOMETER_SCAN_STATUS_INVALID_WL_MASK   8U
#define CAN_PHOTOMETER_SCAN_STATUS_BUSY              9U
#define CAN_PHOTOMETER_SCAN_STATUS_NOT_READY         10U

/* Payload values: Photometer calibration status. These are not metric IDs. */
#define CAN_PHOTOMETER_CAL_STATUS_OK                 0U
#define CAN_PHOTOMETER_CAL_STATUS_REQUIRED           1U
#define CAN_PHOTOMETER_CAL_STATUS_IN_PROGRESS        2U
#define CAN_PHOTOMETER_CAL_STATUS_FAILED             3U
#define CAN_PHOTOMETER_CAL_STATUS_BAD_REFERENCE      4U
#define CAN_PHOTOMETER_CAL_STATUS_ADC_UNDER_RANGE    5U
#define CAN_PHOTOMETER_CAL_STATUS_ADC_OVER_RANGE     6U
#define CAN_PHOTOMETER_CAL_STATUS_GAIN_LIMIT_REACHED 7U
#define CAN_PHOTOMETER_CAL_STATUS_MCP_ERROR          8U
#define CAN_PHOTOMETER_CAL_STATUS_ADC_ERROR          9U
#define CAN_PHOTOMETER_CAL_STATUS_LAMP_NOT_STABLE    10U
#define CAN_PHOTOMETER_CAL_STATUS_TIMEOUT            11U

/*
 * Photometer domain diagnostics policy:
 * no Photometer-specific F007 metric IDs are allocated or emitted today.
 * Add ADS/MCP/raw ADC/gain diagnostics only after the shared service/debug
 * contract defines command IDs, payloads and Conductor consumers.
 */

/* -------------------------------------------------------------------------- */
/* Scanner Executor: CAN_ADDR_SCANNER_BOARD                                    */
/* -------------------------------------------------------------------------- */

/*
 * Wire commands: Scanner lifecycle/control.
 * SCAN_REQUEST starts or stops laser decoding via SSI protocol.
 * SCAN_GET_DATA returns the last decoded barcode.
 *
 * Current Scanner F007 emitted contract:
 * - common Decision metrics 0x0001..0x0012 only;
 * - no Scanner domain F007 metrics are emitted today.
 *
 * Scanner command outcome is handled by ACK/DONE/NACK/timeout.
 * Scanner F007 is used for executor/CAN health diagnostics only.
 */
#define CAN_CMD_SCAN_REQUEST                          0x0601U
#define CAN_CMD_SCAN_GET_DATA                         0x0602U
// Read-only SSI revision probe, fixed 3-second raw UART capture.
#define CAN_CMD_SCAN_BEEP 0x0604U // One short high beep; 3 s raw RX capture. No parameters.
#define CAN_CMD_SCAN_GET_REVISION                  0x0603U

/* Scanner domain NACK codes: globally unique 0xE600..0xE6FF values. */
#define CAN_NACK_DOMAIN_SCANNER_BASE                  0xE600U

#define CAN_ERR_SCAN_NO_DATA                          0xE600U
#define CAN_ERR_SCAN_HARDWARE                         0xE601U
#define CAN_ERR_SCAN_COMM                             0xE602U
#define CAN_ERR_SCAN_TIMEOUT                          0xE603U

/* -------------------------------------------------------------------------- */
/* LLD Executor: CAN_ADDR_LLD_BOARD                                           */
/* -------------------------------------------------------------------------- */

/*
 * Wire commands: LLD lifecycle/control.
 * LLD_GET_STATUS returns current F₁, F₂, F_diff and KА state.
 * LLD_ARM enables differential monitoring and resets baseline.
 * LLD_DISARM disables monitoring.
 *
 * LLD_TOUCH_EVENT is an asynchronous event sent by the executor to the
 * Conductor when liquid contact is detected (not a command response).
 *
 * Current LLD F007 emitted contract:
 * - common Decision metrics 0x0001..0x0012 only;
 * - no LLD domain F007 metrics are emitted today.
 *
 * LLD command outcome is handled by ACK/DONE/NACK/timeout.
 * LLD F007 is used for executor/CAN health diagnostics only.
 */
#define CAN_CMD_LLD_GET_STATUS                        0x0701U
#define CAN_CMD_LLD_ARM                               0x0702U
#define CAN_CMD_LLD_DISARM                            0x0703U
#define CAN_CMD_LLD_TOUCH_EVENT                       0x0704U  // async event: liquid contact detected (not a command response)

/* LLD domain NACK codes: globally unique 0xE700..0xE7FF values. */
#define CAN_NACK_DOMAIN_LLD_BASE           0xE700U

#define CAN_ERR_LLD_NO_DATA                           0xE700U
#define CAN_ERR_LLD_HW_ERROR                          0xE701U
#define CAN_ERR_LLD_BUSY                              0xE702U

/* -------------------------------------------------------------------------- */
/* Heater/Cooler Executor: CAN_ADDR_HEATER_COOLER_BOARD                        */
/* -------------------------------------------------------------------------- */

/*
 * Wire commands: Heater/Cooler power control.
 * The executor applies power levels only; thermal regulation (set-points,
 * control loop, zone <-> channel mapping) is owned by the Conductor.
 *
 * Payload format for commands taking a channel:
 *   byte 2 = zero-based channel id
 *            (0..CAN_HEATER_COOLER_CHANNELS_DEFAULT - 1).
 *   Host thermo_id values remain one-based; the Conductor maps
 *   channel = thermo_id - 1 before packing the low-level command.
 *
 * Channel contract accepted 2026-09-23:
 *   channels 0 and 3: GPIO on/off control of external SSR-25DD relays;
 *   channels 1 and 2: Peltier PWM outputs.
 * HEATCTRL_SET_POWER: Peltier channels only, duty 0..100 percent.
 *   Stores requested duty; applies it only while the channel is enabled.
 *   It does not implicitly enable a disabled channel. For heater channels,
 *   reject with CAN_ERR_INVALID_PARAM without changing the output.
 * HEATCTRL_ENABLE: heater GPIO ON; Peltier output enabled at stored duty.
 * HEATCTRL_DISABLE: heater GPIO OFF; Peltier output disabled with applied
 *   duty zero (stored requested duty is retained).
 *   Heater channels have no duty, PWM window or periodic output tick.
 * HEATCTRL_CHANNEL_SELF_TEST: deep INIT (canon level 2, ECOSYSTEM_STANDARD
 *   sec 3.6.2). Runs the per-channel PWM/ADC feedback self-test at a low
 *   diagnostic duty and verifies measured feedback inside the expected band,
 *   then restores safe state. Contract only; implementation scheduled later.
 * HEATCTRL_GET_STATUS: returns DATA {channel, duty_applied, feedback_mV,
 *   fault_flags, state}[].
 * HEATCTRL_SAFE_OFF: board-wide safe state - all channels OFF, PWM duty zero,
 *   power-stage enables inactive (used by Conductor EMERGENCY_STOP / node-loss
 *   fail-safe).
 *
 * Heater/Cooler F007 contract: common Decision metrics 0x0001..0x0012 only;
 * no domain F007 metrics are emitted yet.
 */
#define CAN_CMD_HEATCTRL_SET_POWER                    0x0801U
#define CAN_CMD_HEATCTRL_ENABLE                       0x0802U
#define CAN_CMD_HEATCTRL_DISABLE                      0x0803U
#define CAN_CMD_HEATCTRL_CHANNEL_SELF_TEST            0x0804U
#define CAN_CMD_HEATCTRL_GET_STATUS                   0x0810U
#define CAN_CMD_HEATCTRL_SAFE_OFF                     0x08FFU

/* --- Heater/Cooler GET_STATUS: состояние канала --- */
#define CAN_HC_STATE_OFF                  0U
#define CAN_HC_STATE_ENABLED              1U
#define CAN_HC_STATE_FAULT                2U

/* --- Биты fault_flags; остальные биты зарезервированы --- */
#define CAN_HC_FAULT_NONE                 0x00U
#define CAN_HC_FAULT_RES                  0x01U
#define CAN_HC_FAULT_ADC                  0x02U
#define CAN_HC_FAULT_PWM_START            0x04U

/*
 * Измерение отсутствует или недоступно.
 * Для SSR отсутствие ADC само по себе не является неисправностью.
 */
#define CAN_HC_FEEDBACK_UNAVAILABLE_MV    0xFFFFU

/* Heater/Cooler domain NACK codes: globally unique 0xE800..0xE8FF values. */
#define CAN_NACK_DOMAIN_HEATER_COOLER_BASE            0xE800U

#define CAN_ERR_HC_INVALID_CHANNEL                    0xE800U
#define CAN_ERR_HC_FEEDBACK_OUT_OF_RANGE              0xE801U
#define CAN_ERR_HC_OVERTEMP                           0xE802U

/* -------------------------------------------------------------------------- */
/* Thermo/Sensor Executor: CAN_ADDR_THERMO_BOARD                              */
/* -------------------------------------------------------------------------- */

/*
 * Wire commands: Thermo/Sensor measurement.
 * Existing H723 code currently names these CAN_CMD_THERMO_GET_*; values match,
 * but canonical names for this layer are CAN_CMD_SENSOR_*.
 *
 * Current STM32F103_temp_sensors F007 emitted contract:
 * - common Decision metrics 0x0001..0x0012 only;
 * - no Thermo domain F007 metrics are emitted today.
 *
 * Temperature measurement results are returned by GET_TEMP/GET_ALL_TEMPS DATA
 * frames. Thermo F007 is used for executor/CAN health diagnostics only.
 */
#define CAN_CMD_SENSOR_GET_ALL_TEMPS                 0x9010U
#define CAN_CMD_SENSOR_GET_TEMP                      0x9011U

/*
 * Wire commands: Thermo-specific service/inventory.
 * These are service/inventory/mapping operations, not Host temperature data.
 * SET_CHANNEL_MAP is split into two frames because executor DLC is 8.
 */
#define CAN_CMD_SRV_SCAN_1WIRE                       0xF101U
#define CAN_CMD_SRV_GET_PHYS_ID                      0xF102U
#define CAN_CMD_SRV_SET_CHANNEL_MAP                  0xF103U
#define CAN_CMD_SRV_GET_CHANNEL_MAP                  0xF104U
#define CAN_CMD_SRV_SET_CH_MAP_P2                    0xF105U

/* Thermo aliases and domain NACK values. */
#define CAN_ERR_INVALID_SENSOR_ID                    CAN_ERR_INVALID_DEVICE_ID
#define CAN_ERR_BUSY                                 CAN_ERR_DEVICE_BUSY

#define CAN_ERR_THERMO_SENSOR_FAILURE                0xE400U
#define CAN_ERR_THERMO_SENSOR_NOT_FOUND              0xE401U
#define CAN_ERR_THERMO_COMM                          0xE402U
#define CAN_ERR_THERMO_CONVERSION_TIMEOUT            0xE403U
#define CAN_ERR_THERMO_CALIBRATION                   0xE404U

#define CAN_ERR_SENSOR_FAILURE                       CAN_ERR_THERMO_SENSOR_FAILURE


/* -------------------------------------------------------------------------- */
/* Sensor Executor: CAN_ADDR_SENSOR_BOARD (optical inputs and reed switches)   */
/* -------------------------------------------------------------------------- */

/*
 * Installed-channel configuration; DLC=8, command code and mask little-endian.
 * Bit (channel - 1) identifies an installed channel, channels 1..20.
 * Valid mask range: 0x00000000..0x000FFFFF; default mask: 0.
 *
 * GET: request bytes 2..7 unused. Response: ACK -> DATA -> DONE.
 * DATA bytes: 02 80, uint32 mask little-endian, 00 00.
 *
 * SET: request bytes 3..6 contain the uint32 mask; bytes 2 and 7 unused.
 * Success: update RAM -> DONE after ACK. Invalid mask: NACK INVALID_PARAM.
 * Flash persistence is requested separately with F003 FLASH_COMMIT.
 */
#define CAN_CMD_SENSOR_GET_INSTALLED_MASK             0x9022U
#define CAN_CMD_SENSOR_SET_INSTALLED_MASK             0x9023U

/*
 * Single search transaction. Sensor implements dispenser START only.
 * READ_STATE implements fresh dispenser START state.
 * SEARCH, initially absent: ACK -> DATA(seq=00, found=0, ready)
 *                         -> DATA(seq=81, found=1) -> DONE, BUSY cleared.
 * SEARCH, initially found: ACK -> DATA(seq=80, found=1) -> DONE.
 * READ: ACK -> DATA(seq=80, fresh GPIO state) -> DONE.
 * READ with observation-history overflow: ACK -> NACK(0xE900), no DATA/DONE.
 * The fault is a snapshot at read time; READ does not clear it or history.
 * A new accepted SEARCH clears history and its overflow flag.
 * DATA: 02 sequence resource position_type position_number pair_state found 00.
 * Conductor starts Motion on ready DATA and sends STOP on detection DATA,
 * without waiting for DONE. DATA must be consumed as each frame arrives.
 */
#define CAN_CMD_SENSOR_SEARCH_POSITION                0x9024U
#define CAN_CMD_SENSOR_READ_STATE                     0x9025U

/* Sensor domain NACK codes: globally unique 0xE900..0xE9FF values. */
#define CAN_NACK_DOMAIN_SENSOR_BASE                   0xE900U
#define CAN_ERR_SENSOR_HISTORY_OVERFLOW               0xE900U

/*
 * Positioning resources addressed by the Sensor command selector (byte 2).
 * These are local resource IDs, not input-channel numbers or GPIO indices.
 * Sensor owns the mapping to optical inputs and the recognition algorithm.
 * Reagent/reaction rotor comb algorithms are not implemented by these IDs.
 */
#define CAN_SENSOR_RESOURCE_DISPENSER_DISC            0U
#define CAN_SENSOR_RESOURCE_MIXER_DISC                1U
#define CAN_SENSOR_RESOURCE_REAGENT_ROTOR             2U
#define CAN_SENSOR_RESOURCE_REACTION_ROTOR            3U

/*
 * Position-search parameters, not raw GPIO levels:
 * data[0] (payload byte 3): START or WORK.
 * data[1] (payload byte 4): 0 for START; working-position number for WORK, starting at 1
 * for the nearest position to START, then increasing in position order.
 * Dispenser WORK positions: 1..8, as numbered in the positioning-disc drawing.
 * Mixer WORK positions: 1 = mixer wash, 2 = reaction-disc cuvette.
 * Reagent and reaction rotors: WORK position numbers starting at 1.
 * Position meaning belongs to the selected resource; dispenser positions
 * above reagents are dispenser positions, not reagent-rotor position numbers.
 * data[2..4] (payload bytes 5..7): reserved, zero.
 * Position counts and recognition algorithms are resource-specific;
 * a requested number alone does not establish the physical position.
 */
#define CAN_SENSOR_POSITION_START                    0U
#define CAN_SENSOR_POSITION_WORK                     1U

/* -------------------------------------------------------------------------- */
/* Migration Notes                                                            */
/* -------------------------------------------------------------------------- */

/*
 * Integration checkpoint 23.06.2026:
 * - STM32H723_mother_board can_packer defines Photometer NodeID, low-level
 *   commands, channel constants and globally unique Photometer NACK codes.
 * - STM32F103_photometer local can_protocol.h is synchronized with the
 *   channel-based low-level contract and Photometer NACK 0xE500..0xE50B.
 * - STM32F103_temp_sensors local can_protocol.h maps SENSOR_FAILURE to the
 *   Thermo domain code 0xE400; 0x0003 remains common DEVICE_BUSY.
 * - STM32F103_step_motors_refactored local can_protocol.h keeps Motion names
 *   INVALID_MOTOR_ID/MOTOR_BUSY only as aliases to common
 *   INVALID_DEVICE_ID/DEVICE_BUSY.
 * - STM32F103_pumps_valves local can_protocol.h already uses the common
 *   executor NACK namespace; local duplicated readme copies are replaced by a
 *   pointer to the shared DDS-240 documentation tree.
 * - F007 remains common-only; ADS/MCP/SPI/raw ADC details belong to advertised
 *   CAN_DIAG_BLOCK_PHOTOMETER_* blocks, not to CAN_STATUS_*.
 *
 * Required cleanup for future engineering diagnostics:
 * - do not add Motion/Thermo/Fluidics/Photometer driver internals to F007;
 * - implement CAN_CMD_SRV_GET_DIAG_CAPS before CAN_CMD_SRV_GET_DIAG_BLOCK so
 *   the Conductor/engineering tool can discover supported block schemas;
 * - expose only read-only snapshots from executor task context;
 * - add read-only snapshot APIs before exporting internal live state arrays;
 * - current STM32F103_step_motors_refactored candidates:
 *   CAN_DIAG_BLOCK_MOTION_STATE/QUEUE/TMC/LIMITS/FAULTS;
 * - current STM32F103_temp_sensors candidates:
 *   CAN_DIAG_BLOCK_THERMO_SUMMARY/1WIRE_SCAN/CHANNEL_MAP/RAW/BUS;
 * - current STM32F103_pumps_valves candidates:
 *   CAN_DIAG_BLOCK_FLUIDICS_STATE/TIMERS/MAPPING/IO/FAULTS;
 * - current STM32F103_photometer candidates:
 *   CAN_DIAG_BLOCK_PHOTOMETER_SUMMARY/CALIBRATION/ADC/MCP/SPI/LIGHT;
 * - current STM32F103_lld candidates:
 *   CAN_DIAG_BLOCK_LLD_STATE/MEASUREMENT/FILTER;
 * - keep F007 common-only for executor/CAN health unless a future Decision
 *   metric has an explicit Conductor action and is accepted as common baseline.
 *
 * Required cleanup in documentation/projects:
 * - old STM32F103_step_motors CMD_MOVE_* enum is a legacy internal protocol;
 *   current Motion reference is STM32F103_step_motors_refactored.
 */

#endif /* CAN_EXECUTOR_GLOBAL_CONFIG_H_ */

/* D106 record layout documented in Step_motors/project_report.md (2026-10-27). */
