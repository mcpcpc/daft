```text
Deviation ID: DEV-001
Date: 2026-06-12
Author: daft project (implemented with Claude Code)
Reviewer: project owner
Affected file(s): src/dtime.c
Affected function(s): daft_time_read_monotonic, daft_time_sleep_ms
MISRA rule or project rule: MISRA C:2012 Rule 21.10 (the Standard Library
    time and date facilities shall not be used)
Description of deviation: <time.h> is included for clock_gettime(),
    clock_nanosleep(), struct timespec and CLOCK_MONOTONIC.
Reason compliance is not practical: The program is a real-time MIDI
    daemon; POSIX monotonic-clock access has no alternative interface.
    Rule 21.10 targets the C standard library's wall-clock facilities
    (time, ctime, localtime); none of those are used.
Safety impact: None. Only the monotonic clock is read; no calendar or
    locale-dependent behavior is introduced.
Security impact: None.
Determinism impact: Contained. All time access is confined to the dtime
    module behind the project time interface, which provides a
    deterministic FAKE backend for tests and simulation (AGENTS.md
    determinism rule satisfied by construction).
Alternatives considered: gettimeofday (worse: wall clock, non-monotonic);
    busy-wait loops (worse: nondeterministic timing and CPU waste).
Risk controls or mitigations: Single module; bounded EINTR retry; return
    values checked; FAKE backend used for all reproducibility-sensitive
    verification.
Evidence supporting bounded behavior: daft_time_sleep_ms bounds retries
    to DAFT_TIME_MAX_SLEEP_RETRIES; clock_gettime is non-blocking.
Expiration or review date: review with each MISRA baseline update
Approval status: approved (project owner accepted plan on 2026-06-12)
Approver: project owner
```
