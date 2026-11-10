# Scanner: instructions for the coding assistant

Before working in this project, read the canonical rules:
[Scanner WORK_RULES](../DDS-240_readme/DDS-240_eko_system/Scanner/WORK_RULES.md).
The mandatory pre-proposal unification procedure dated 2026-10-29 applies.

The user edits firmware. Do not edit, revert, or replace firmware/project
configuration unless the user explicitly authorizes that specific edit.
Agreement to a plan or implementation scope alone does not authorize edits.

Before proposing any solution, verify the applicable ecosystem contract,
inspect the exact agreed reference functions and their dependencies, and
compare the proposed block against them. Report every necessary Scanner
adaptation. Do not silently add validation, helpers, policies, timeouts or
other improvements absent from the agreed reference mechanism. Surface any
reference-versus-contract conflict before presenting affected code as ready.

Give exact file/location/action/full code and a short evidence-based comparison.
After user edits, audit actual files and appropriate agreed checks. CODE,
HOST, BUILD and BENCH evidence are distinct. Never mark a block unified merely
because it builds. Preserve user changes and historical documentation.

Current plan:
[Scanner unification plan](../DDS-240_readme/DDS-240_eko_system/Scanner/SCANNER_UNIFICATION_PLAN_20261028.md).
The plan title defines the local task: **Scanner — audit and unification of
common layers**, not a single defect. Target board: Scanner (NodeID 0x60,
family 60..6F). Reference boards and pinned sources:
HC `6abc1d2706acac36d842594e6331de180c2241ea`,
Thermo `c43368ec5509f2693caed8797d370047af0af346`,
Fluidics `ac1aa2b12b3d270d98f080a8b0bdbe5e4ad9ca49`.

Common-vs-specific rule (EXECUTOR_COMMON_LAYERS_GUIDE.md and
DDS-240_ECOSYSTEM_STANDARD.md:550,683,696-709): common layers (HAL/IRQ,
CAN transport, Dispatcher/service, config/Flash, safety/watchdog, RTOS/build)
must keep the same contract and behavior as the accepted reference; only
documented hardware/resource adaptations are allowed. Domain specifics
(Scanner SSI/Zebra, UART handshake, STOP/FLUSH preparation, barcode recovery,
E600-E603 mapping, 0601/0602/0603/0604) stay at the mapping/translation/
response-policy boundary: use the closest functional analog or own
implementation, and never create a bypass protocol or change the common
transaction lifecycle. "Same" means same contract/behavior, not byte-identical
code; layout, defaults and safe-state remain board-specific.

Current state (2026-11-10): barcode receive/recovery loop and empty-barcode
rejection are installed and committed in
App/src/tasks/task_scan_controller.c and App/src/scan_controller.c
(commit of 2026-11-10 over 0cf2189; see git log -1).
U21-A/B addressed; U21-C CODE+BUILD (checked osDelay -> Error_Handler,
SCAN_CAPTURE_WINDOW_MS/POLL_MS); typed outcome CODE+BUILD (ScanReadOutcome_t,
terminal switch preserves E601/E602/E603 priority). Debug incremental BUILD
PASS (49380/72/15136, 0 errors/0 warnings), ELF SHA256
bbe6687d4f5a990b1041ffad2f93cce4834e05cb2b31cad2cc8906a38b9b0bfc.
HOST SSI codec and controller PASS; HOST task-loop NOT RUN.
Release/BENCH NOT RUN; actual FLUSH/EE=1/ACK-NAK/module settings unverified.
Remaining U21/U22: common transaction deadline, full RX-event ownership/
fragment assembly, multipacket conditional. STOP/terminal is DECIDED by
SCANNER_EXECUTOR_REPORT.md:2734-2735/2075: success sends no STOP; ordinary
error sends CAN terminal then the existing trailing STOP; no STOP ACK wait;
no reorder; reset remains a separate STOP-only path.
This block is closed in report/plan/NEXT/README and committed with explicit
permission. Next: continue the U-matrix (common deadline / RX ownership, not
STOP). The common preparation+START+read deadline has no reference precedent
in HC/Thermo/Fluidics and requires an explicit user policy decision first. No new START after its ACK;
local retransmission is not a new CAN operation. Preparation adds time before
START; Studio timeout+7 integration remains OPEN. No broad TX deadline, Studio
changes or hardware actions are authorized. Consultation, exact source
comparison and manual code workflow remain in force.

## Industrial baseline — стабильное правило
- HC/Thermo/Fluidics — фактический промышленный baseline проекта.
  Их P0-статус не сертифицируется и не пересматривается в рамках Scanner.
  Не заявлять «эталоны P0» или «эталоны не P0» без полного аудита.
- Scanner сравнивается только с фактической реализацией эталонов
  и своими SSI/Zebra-требованиями.
- Кандидатные патчи `readme/checkpoints/.../code_proposals/` —
  подготовка, не эталон и не требование.
- Пункт P0 для Scanner — только если тот же механизм есть в промышленной
  реализации эталона или прямо требуется `DDS-240_ECOSYSTEM_STANDARD.md`
  / `DDS240-ACCEPT-1`.
- Объективные факты без P0-фрейминга: Release не собран; блоки плана
  A/B/D/E/F не закрыты. Остальное — дельта после сравнения, не новое требование.
- Новая сессия, найдя кандидатный патч, не превращает его в требование:
  сначала ищет эталонную реализацию или нормативный источник.

Historical handoff (superseded by Nov 6): verified 2026-11-05;
see canonical Scanner/NEXT_SESSION_PROMPT.md
and the final 2026-11-03 entry in the unification plan. F002/F006 controlled
STOP-only preparation IS installed in seven uncommitted App files over ea1eacb.
It uses a shared 3000 ms budget from the first latch, validates the first STOP
ACK, closes RX, and publishes prepared before service/reset or F006 Flash.
All seven source hashes and the Debug ELF match reset_connected_20261103
evidence. Saved evidence: HOST18 shutdown groups and HOST13 regression PASS;
Debug incremental BUILD PASS (47044/68/15136). No new test/build run on Nov 5.
BENCH/Release remain NOT RUN; do not declare full SSI or board acceptance.
Next separate block: F003 Flash/UART arbitration, still unimplemented. Protect
active scan/capture/UART and queued/in-flight operations without a check/start
race. Do not automatically interrupt an operation for Commit or set reset-latch
for F003. Read contracts and exact reference functions before proposing code.
Consultation/manual firmware editing rules remain in force. No new firmware,
configuration, commit, flash, hardware command or delegation authorization.

Historical stop (superseded by the verified Nov 5 handoff): end of 2026-11-02;
see canonical Scanner/NEXT_SESSION_PROMPT.md
and readme/checkpoints/20261102/. RX/TX USART3 now use UART interrupts; RX
pending storage is 128 bytes. Both DMA channels were removed by the user in
Cube. RX close/error handling and no STOP after success have scoped HOST13
and Debug evidence (45740/68/15128); physical SSI and EE=1 are still unverified.
Next agreed block, NOT implemented: controlled F002/F006 preparation with
immediate latch/safe GPIO, a task-owned STOP-only shutdown path, correct
handling of old replies, prepared only after confirmed stop/RX closure, and
one shared 3-second preparation budget replacing the current 1 second.
Normal UART commands stay forbidden after latch. Do not simply bypass gates
or extend every wait; post-latch RX/TX behavior needs a complete checked block.
F003 Flash/UART arbitration follows separately. Never wait for SSI in fatal
paths. Preserve the code-versus-BENCH distinction and user's working changes.
Specific edit permissions from this session do not authorize future firmware
edits, configuration changes, commits, flashing or hardware commands.
Give exact manual code blocks; do not offer git apply as the editing workflow.
Do not delegate without the user's request.

Historical stop (superseded by 2026-11-02): end of 2026-10-30; see canonical Scanner/NEXT_SESSION_PROMPT.md
and repository readme/checkpoints/20261030/. D/E and outgoing SSI ACK have
scoped CODE/HOST/BUILD evidence. Scan_CollectPacket assembles packets delivered
to the task; U22 remains open for preserving RX events before task consumption.
The earlier driver accumulation/out_overflow proposal was deferred and not
applied. Do not apply it automatically. Byte-DMA TX with existing local bounds
is now agreed; physical SSI operation and EE=1 are not bench confirmed.
The specific edit and commit permissions for 2026-10-30 do not authorize new
firmware edits, flashing, hardware commands, or future commits. Do not delegate
to subagents unless the user requests it.
