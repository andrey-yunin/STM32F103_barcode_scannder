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
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/Scanner/archive/SCANNER_EXECUTOR_IMPLEMENTATION_PLAN.md
```

Scanner project report:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/Scanner/SCANNER_EXECUTOR_REPORT.md
```

Scanner current specification:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/Scanner/SCANNER.md
```

Scanner current plan:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/Scanner/SCANNER_PLAN.md
```

CAN protocol documentation:

```text
/home/andrey/STM32CubeIDE/workspace_1.19.0/DDS-240_readme/DDS-240_eko_system/CAN_Protocol
```


## Current Scanner handoff — 2026-11-10

Normative document: [Scanner/SCANNER.md](../../DDS-240_readme/DDS-240_eko_system/Scanner/SCANNER.md);
current plan: [Scanner/SCANNER_PLAN.md](../../DDS-240_readme/DDS-240_eko_system/Scanner/SCANNER_PLAN.md).
The specification wins over plan/report/NEXT on disagreement.

Start with the [canonical handoff](../../DDS-240_readme/DDS-240_eko_system/Scanner/NEXT_SESSION_PROMPT.md)
and the [current spec](../../DDS-240_readme/DDS-240_eko_system/Scanner/SCANNER.md).
Installed over 0cf2189 and committed on 2026-11-10: barcode SSI receive/recovery
loop (cursor packet assembly across RX snapshots, ACK/NAK/REVISION handling,
SSI NAK RESEND, limited START retransmission before ACK), nonempty barcode
validation in controller, U21-C capture checks and typed read outcome with
E601/E602/E603 priority preserved. Earlier blocks remain: reset STOP/ACK with
shared3s, F003 startup/queued/in-flight guard, SSI status codec, normal UART RX
reply boundary and confirmed STOP/FLUSH before each new START.

Verified: Debug incremental BUILD PASS (50196/72/15136, 0 errors/0 warnings). Release and BENCH are NOT RUN. Actual FLUSH/EE=1/ACK-NAK/module settings remain unverified.
Remaining U21/U22: full RX-event ownership and fragment assembly, multipacket
conditional. Common deadline 0601 (P1b: 6 s prep budget + read timeout) is
implemented and committed in `477062b`; build PASS 50196/72/15136. As-built protocol worst is 16 s, not 22 s;
there is no hard bound because driver sends block. STOP/terminal is DECIDED:
success sends no STOP; ordinary error sends the CAN terminal then the trailing
STOP; there is no STOP ACK wait and reset remains a separate STOP-only path.
External operation timeout/Studio window integration remains OPEN. Use
git log -1 for the commit hash. No push or hardware actions.

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
and the latest section of the [unification plan](../../DDS-240_readme/DDS-240_eko_system/Scanner/archive/SCANNER_UNIFICATION_PLAN_20261028.md).
RX/TX UART IT and RX128B are implemented; RX closure has HOST13/Debug evidence.
Next agreed work: F002/F006 controlled STOP-only shutdown with a shared
3-second preparation budget; implementation is still open. F003 Flash/UART
arbitration follows. Hardware acceptance is open. This snapshot is based on
57740f3 and is committed with the runtime/config changes at the user's request.
Use git log -1 --oneline for the resulting commit. No push or target actions
were requested.

## Historical Scanner handoff — 2026-10-30

Start with [canonical handoff](../../DDS-240_readme/DDS-240_eko_system/Scanner/NEXT_SESSION_PROMPT.md)
and [unification plan](../../DDS-240_readme/DDS-240_eko_system/Scanner/archive/SCANNER_UNIFICATION_PLAN_20261028.md).
Next: U22, preserve RX events until the scan task consumes them. Packet assembly
in the task is implemented; full U22 and hardware acceptance remain open.
