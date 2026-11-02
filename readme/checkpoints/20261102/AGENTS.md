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
Current stop: end of 2026-11-02; see canonical Scanner/NEXT_SESSION_PROMPT.md
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
