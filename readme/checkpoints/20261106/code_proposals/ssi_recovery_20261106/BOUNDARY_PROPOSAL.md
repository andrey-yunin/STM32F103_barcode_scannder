# SSI recovery preparation — 06.11.2026

Status: incomplete PROPOSED preparation, not installed firmware and not a
ready manual integration block. The operation-level recovery agreement is
recorded in the canonical Scanner report dated 06.11.2026.

## Work prepared and checked

`receive_helpers.inc` contains the proposed private task helpers. A cursor
preserves every packet in an RX snapshot, including bytes following a damaged
packet. Complete supported-length packets are classified before the task
chooses an SSI action or terminal CAN outcome. Checksum errors produce a RESEND
event, valid ACK/NAK are separate events, A4 is separate from a barcode, and
empty barcode/source/status errors cannot become successful barcode results.
No UART transmission, new START, CAN response, reset handling or task wiring
is present in these helpers. Multipacket barcode support is not implemented.

HOST with actual project `scan_ssi.c`, AddressSanitizer/UBSan: 63 snapshot
splits, bytewise input, combined ACK/A4/bad F3/repeated F3, source/empty/status
checks, preserved NAK causes, and golden host NAK RESEND bytes PASS.
The helpers retain the project's payload/buffer limits. They do not establish
the correct configuration or physical capabilities of the module.

## Required reading boundary: unresolved implementation decision

The agreed condition is that a previous reading's late packet must never be
reported as the next reading's result. SSI F3 contains no CAN transaction ID.
An old queued initial transmission can have the same source, opcode, status,
payload and checksum as a valid new reading. Clearing the MCU's buffer,
checking checksum, inspecting Retransmit, or waiting for START ACK alone does
not prove that such a packet belongs to the new operation.

The protocol states that the reply to a new host command precedes a postponed
decoder message (§1-4/1-5). Therefore START ACK can be followed by old data.
The receive-only ambiguity is demonstrated in `receive_host.c`; this is a
protocol/model argument, not a physical observation of the scanner.

## Proposed module-side boundary, not yet agreed

Before admitting barcode data for each new reading:

1. Send SSI STOP/E5 and confirm its response, stopping any old decode attempt.
2. Send SSI FLUSH_QUEUE/D2 and confirm its response, removing old queued data.
3. At each new host handshake, discard earlier RX through the final TX byte
   before CTS is released, so the first new response is not an older ACK.
4. Only after both preparations succeed, open the new read/issue its START.
5. Preparation failure must prevent START and report an appropriate existing
   error; it must never silently fall back to an unisolated reading.

This adds module-side commands and confirmation to the previously agreed
mechanism; it needs explicit agreement before it is presented as ready code.
SSI Programmer's Guide 72E-40451-03 Rev A §2-43 defines FLUSH_QUEUE/D2 as
clearing the transmission queue; Table1-1 defines ACK/NAK responses. Sending
STOP alone or CMD_NAK CANCEL does not establish an empty module queue.

Source checked in the manufacturer-authored guide mirrored at:
https://studylib.net/doc/18232588/simple-serial-interface-programmer-s-guide
The earlier bsr.at copy failed to open during this session.

Support by the actual SE965HP has not been confirmed. Its integration guide
was also inspected; no explicit FLUSH_QUEUE entry was found there. A generic
SSI command definition is not a bench confirmation for this module.
Unknown command/NAK must fail preparation, not admit potentially old data.

The exact preparation deadline, error precedence, ordinary TX admission and
post-TC RX boundary must be checked as one block with F002/F006 and F003.
No new preparation timeout has been chosen in this draft. The existing
read timeout starts after START; this draft does not change that contract.
The two-retry module policy applies to the module's data transmissions; it
does not automatically choose a new host START retry counter or retry policy.

## Pending work

Agree the module-side boundary and its budget before full task/driver code.
Then integrate and test actual corruption/RESEND/repeated-data sequences,
late data across separate reads, cancellation during each phase, NAK causes,
deadline/error outcomes, outgoing ACK and one terminal result. No firmware,
project configuration, Studio, flashing, hardware commands or commits changed.
No full HOST/Debug BUILD/BENCH claim for the recovery operation is made.
