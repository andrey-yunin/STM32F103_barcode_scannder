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
Current handoff: 2026-11-06, canonical Scanner/NEXT_SESSION_PROMPT.md and
readme/checkpoints/20261106/. Commit parent ea1eacb; use git log -1 for the
resulting snapshot commit. Installed: reset STOP/ACK/RX-close with shared3s,
F003 startup/queued/in-flight guard, SSI status codec, normal SendForReply RX
boundary, and task STOP/ACK -> FLUSH_QUEUE/ACK preparation before START.
Preparation uses a2000ms reply limit and up to2 resends of the same STOP/FLUSH
only after explicit SSI RESEND. No retry on silence. New START is forbidden
after failed preparation. No ordinary STOP after successful barcode result.
Latest installed HOST:20 preparation scenarios/36 commands plus18 shutdown
groups PASS; Debug incremental BUILD PASS (48740/72/15136), user confirmed.
Release/BENCH NOT RUN. Actual FLUSH/EE=1/ACK-NAK/module settings unverified.
NEXT: wire the agreed barcode receive/recovery loop and reject empty barcode
in controller. Those changes are NOT installed; U21-A/B/C remain as documented.
Full ssi_recovery_20261106 candidate is preparation, not installed CODE. Its
old20-step instructions cannot be replayed over already-installed components;
rebase only remaining receive/controller blocks, preserving reset/F003/prep.
No new START after its ACK; local retransmission is not a new CAN operation.
Preparation adds time before START; Studio timeout+7 integration remains OPEN.
No broad TX deadline, Studio changes or hardware actions are authorized.
The user's current request authorizes this checkpoint/commit only. Prior
specific firmware permissions do not authorize the next block automatically.
Consultation, exact source comparison and manual code workflow remain in force.

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
