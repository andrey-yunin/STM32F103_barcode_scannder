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
Previously supplied F005 needs re-review; its extra inline checks are not an
accepted reference transfer. Earlier assistant-written SSI handshake remains
unagreed worktree code. Do not treat its presence as approval or revert it
without instruction. Do not delegate to subagents unless the user requests it.
