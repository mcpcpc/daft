```text
Deviation ID: DEV-003
Date: 2026-06-12
Author: daft project (implemented with Claude Code)
Reviewer: project owner
Affected file(s): all src/*.c (project-wide)
Affected function(s): all functions using guarded early returns
MISRA rule or project rule: MISRA C:2012 Rule 15.5 (advisory: a function
    should have a single point of exit at the end)
Description of deviation: Functions return early after argument
    validation and on error-propagation paths.
Reason compliance is not practical: AGENTS.md mandates the explicit
    status-code error-handling model and its own reference pattern
    ("Function Pattern", "Error Code Pattern") demonstrates guarded early
    returns. Single-exit rewrites of deeply validated functions require
    either nested conditionals (reduced reviewability, violates project
    "simpler to review" decision rule) or flag variables that obscure
    control flow.
Safety impact: None; every return path yields a typed status code and
    performs no resource acquisition before validation.
Security impact: Positive: validation-first early return prevents any
    use of unvalidated inputs.
Determinism impact: None.
Alternatives considered: single-exit with status flag (adopted where it
    does not harm clarity, e.g. loop bodies per Rule 15.4); nested
    if/else pyramids (rejected: harder to review).
Risk controls or mitigations: Rule is advisory; cppcheck suppression is
    scoped to exactly this rule ID in the analyze target; all other
    control-flow rules (15.1-15.4, 15.6, 15.7) remain enforced.
Evidence supporting bounded behavior: not applicable (style rule).
Expiration or review date: review with each MISRA baseline update
Approval status: approved (project owner accepted plan on 2026-06-12)
Approver: project owner
```
