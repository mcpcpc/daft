```text
Deviation ID: DEV-002
Date: 2026-06-12
Author: daft project (implemented with Claude Code)
Reviewer: project owner
Affected file(s): src/main.c
Affected function(s): daft_handle_stop, daft_setup_signals
MISRA rule or project rule: MISRA C:2012 Rule 21.5 (the Standard Library
    signal handling facilities shall not be used)
Description of deviation: <signal.h> is included for sigaction(),
    sigemptyset(), SIGINT/SIGTERM and sig_atomic_t.
Reason compliance is not practical: The program is a headless daemon whose
    contract requires emitting MIDI all-notes-off before exit; SIGTERM and
    SIGINT are the only POSIX-portable shutdown notifications.
Safety impact: Positive: without the handler, a terminated process leaves
    notes sounding indefinitely on the downstream synthesizer.
Security impact: None.
Determinism impact: Minimal. The handler performs exactly one operation:
    setting a volatile sig_atomic_t flag (async-signal-safe by C99
    definition). All cleanup runs in the main loop.
Alternatives considered: ignoring signals (fails the all-notes-off
    contract); signalfd (Linux-only, violates POSIX portability rule).
Risk controls or mitigations: sigaction (not signal()) for defined
    semantics; no logging, I/O, or allocation in the handler; handler
    shared by both signals.
Evidence supporting bounded behavior: handler body is a single assignment.
Expiration or review date: review with each MISRA baseline update
Approval status: approved (project owner accepted plan on 2026-06-12)
Approver: project owner
```
