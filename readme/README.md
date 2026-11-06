# DDS-240 shared documentation pointer

This project uses the shared DDS-240 documentation folder from the STM32CubeIDE
workspace instead of keeping a local copy of ecosystem documents.

## Shared documentation

Common documentation root:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme
```

Ecosystem documentation:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system
```

Scanner executor documentation:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/Scanner
```

## Key entry points

Ecosystem standard:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/DDS-240_ECOSYSTEM_STANDARD.md
```

Global executor config:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/dds240_global_config.h
```

Scanner implementation plan:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/Scanner/SCANNER_EXECUTOR_IMPLEMENTATION_PLAN.md
```

Scanner project report:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/Scanner/SCANNER_EXECUTOR_REPORT.md
```

CAN protocol documentation:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/CAN_Protocol
```


## Current Scanner handoff — 2026-11-06

Start with the [canonical handoff](../../DDS-240_readme/DDS-240_eko_system/Scanner/NEXT_SESSION_PROMPT.md)
and [checkpoint/20261106](checkpoints/20261106/README.md).
Installed over ea1eacb: reset STOP/ACK with shared3s, F003 startup/queued/
in-flight guard, SSI status codec, normal UART RX reply boundary and confirmed
STOP/FLUSH before each new START. Latest HOST preparation20 scenarios/36commands
and shutdown18 groups PASS; Debug incremental BUILD PASS (48740/72/15136),
also confirmed by the user. Release/BENCH remain open.

Next: barcode SSI recovery loop and nonempty barcode validation in controller.
These changes are NOT installed. The earlier full candidate must be compared
with the current tree; its20 steps already contain installed components.
External operation timeout/Studio window integration remains OPEN.
Current state is saved in a normal commit over ea1eacb at the user's request;
use git log -1 for its hash. No push or hardware actions were requested.

## Historical Scanner handoff — verified 2026-11-05

Read the [canonical handoff](../../DDS-240_readme/DDS-240_eko_system/Scanner/NEXT_SESSION_PROMPT.md)
and the final 2026-11-03 entry in the shared unification plan. F002/F006
STOP-only shutdown, ACK validation, RX closure and prepared gate are installed
with one shared 3000 ms budget. Seven App files remain uncommitted over ea1eacb.
Their hashes and the Debug ELF match reset_connected_20261103 evidence:
HOST18 shutdown groups + HOST13 regression PASS, Debug incremental BUILD PASS
(47044/68/15136). Tests/build were not rerun on Nov 5. BENCH/Release remain open.
Next separate block: F003 Flash/UART arbitration, including queued/in-flight
operations and exclusion of the check/start race. Consultation mode continues.

## Historical Scanner handoff — 2026-11-02

Start with [canonical handoff](../../DDS-240_readme/DDS-240_eko_system/Scanner/NEXT_SESSION_PROMPT.md)
and the latest section of the [unification plan](../../DDS-240_readme/DDS-240_eko_system/Scanner/SCANNER_UNIFICATION_PLAN_20261028.md).
Session snapshot: [checkpoints/20261102](checkpoints/20261102/).
RX/TX UART IT and RX128B are implemented; RX closure has HOST13/Debug evidence.
Next agreed work: F002/F006 controlled STOP-only shutdown with a shared
3-second preparation budget; implementation is still open. F003 Flash/UART
arbitration follows. Hardware acceptance is open. This snapshot is based on
57740f3 and is committed with the runtime/config changes at the user's request.
Use git log -1 --oneline for the resulting commit. No push or target actions
were requested.

## Historical Scanner handoff — 2026-10-30

Start with [canonical handoff](../../DDS-240_readme/DDS-240_eko_system/Scanner/NEXT_SESSION_PROMPT.md)
and [unification plan](../../DDS-240_readme/DDS-240_eko_system/Scanner/SCANNER_UNIFICATION_PLAN_20261028.md).
The versioned session snapshot is [checkpoints/20261030](checkpoints/20261030/).
Next: U22, preserve RX events until the scan task consumes them. Packet assembly
in the task is implemented; full U22 and hardware acceptance remain open.
