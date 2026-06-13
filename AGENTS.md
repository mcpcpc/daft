# AGENTS.md

## Purpose

This repository contains mission-critical POSIX C code. All development agents, contributors, and automated code-generation systems must follow this file before creating, modifying, reviewing, or refactoring code.

The required baseline is:

- Language: ISO C99
- Platform: strictly portable POSIX C
- Compiler: GCC
- Build system: Make
- Static analysis: cppcheck with MISRA-capable checking
- Compliance: mandatory MISRA-C compliance
- Safety principles: NASA/JPL "Power of 10" rules
- Domain: domain-agnostic mission-critical software

These rules apply equally to handwritten code, generated code, test support code intended for target use, build scripts, examples, and templates unless explicitly marked as non-production.

---

## Non-Negotiable Development Rules

### 1. MISRA-C Compliance Is Mandatory

All production C code shall comply with the applicable MISRA-C rule set selected by the project.

MISRA violations are not allowed unless a formal deviation has been approved and recorded. Advisory and required rules shall both be treated as mandatory unless the project safety authority explicitly classifies a rule differently in writing.

Generated code shall be checked, reviewed, and held to the same MISRA standard as handwritten code.

### 2. NASA Power of 10 Alignment

All code shall follow the intent of NASA/JPL's Power of 10 rules:

1. Restrict control flow to simple, analyzable constructs.
2. Give all loops statically provable upper bounds.
3. Do not use dynamic memory allocation after initialization; this project forbids it entirely.
4. Keep functions small and focused.
5. Use a limited number of assertions and runtime checks for critical assumptions.
6. Declare data objects at the smallest possible scope.
7. Check return values from all non-void functions.
8. Limit preprocessor usage.
9. Limit pointer indirection and avoid complex pointer manipulation.
10. Compile with all warnings enabled and treat warnings as errors.

### 3. No Dynamic Memory Allocation

The following are forbidden in all production code:

- `malloc`
- `calloc`
- `realloc`
- `free`
- `strdup`
- `asprintf`
- Any custom heap allocator
- Any library interface that hides unbounded heap allocation behind the API

All memory shall be statically allocated or provided by caller-owned fixed-size buffers with explicit length parameters.

### 4. Recursion Is Allowed Only With Strict Proof

Recursive functions are forbidden by default.

A recursive function may be used only when all of the following are true:

- Maximum recursion depth is statically provable.
- Stack usage is bounded and documented.
- Termination is proven by a simple, reviewable argument.
- The recursive implementation is clearer and safer than the iterative alternative.
- A formal deviation record is approved before merge.

### 5. All Loops Must Be Bounded

Every loop shall have a statically provable upper bound.

This applies to:

- `for` loops
- `while` loops
- `do while` loops
- retry loops
- polling loops
- thread wait loops
- I/O loops
- parser loops

Unbounded loops such as `while (1)` are forbidden unless they are part of a formally approved top-level scheduler or service loop with documented exit, watchdog, and timing behavior.

### 6. Multithreading Is Allowed Only Under Strict Rules

Multithreaded code is allowed only when concurrency behavior is explicit, bounded, and reviewable.

Requirements:

- No data races.
- Shared mutable state shall be minimized.
- All shared data shall have a documented ownership and synchronization model.
- Only approved POSIX synchronization primitives may be used.
- Blocking operations shall have bounded wait behavior.
- Lock ordering shall be documented when more than one lock may be acquired.
- Deadlock, livelock, priority inversion, and starvation risks shall be considered.
- Thread creation shall be bounded and deterministic.
- Detached threads are forbidden unless formally justified.

### 7. Determinism and Reproducibility

Production behavior shall be deterministic and reproducible.

Forbidden unless isolated behind an approved deterministic abstraction:

- Time-based randomness
- Environment-dependent behavior
- Uncontrolled dependence on wall-clock time
- Uncontrolled file-system ordering
- Race-dependent behavior
- Undefined scheduling assumptions
- Unseeded pseudo-random number generation

Where time is required, access shall occur only through project-approved time interfaces. Tests shall be able to replace time sources with deterministic fakes.

---

## Error Handling

The required error-handling model is explicit return codes only.

### Rules

- Every fallible function shall return a project-defined status code.
- Output values shall be returned through validated pointer arguments.
- Return values shall never be ignored.
- Do not use ambiguous sentinel-only error handling such as returning `NULL`, `-1`, or `0` without a typed status result.
- Do not use global mutable last-error state.
- Use `errno` only immediately after approved POSIX calls that require it.
- Copy `errno` into a local variable immediately after failure.
- Translate POSIX errors into project-defined status codes.
- Recovery behavior shall be explicit: retry, degrade safely, fail closed, or propagate the error.

### Error Code Pattern

```c
#ifndef PROJECT_STATUS_H
#define PROJECT_STATUS_H

typedef enum
{
    PROJECT_STATUS_OK = 0,
    PROJECT_STATUS_INVALID_ARGUMENT,
    PROJECT_STATUS_OUT_OF_RANGE,
    PROJECT_STATUS_IO_ERROR,
    PROJECT_STATUS_TIMEOUT,
    PROJECT_STATUS_INTERNAL_ERROR
} project_status_t;

#endif
```

### Function Pattern

```c
#include <stddef.h>
#include "project_status.h"

project_status_t module_get_value(int * const out_value)
{
    if (out_value == NULL)
    {
        return PROJECT_STATUS_INVALID_ARGUMENT;
    }

    *out_value = 0;
    return PROJECT_STATUS_OK;
}
```

---

## Logging and Diagnostics

Production code shall use only the approved deterministic logging interface.

Forbidden in application code:

- Direct `printf`
- Direct `fprintf`
- Direct `syslog`
- Variadic logging wrappers
- Dynamic log message construction
- Logging that allocates memory
- Logging that changes control flow
- Logging from signal handlers unless restricted to async-signal-safe operations

Requirements:

- Fixed message IDs.
- Fixed format strings.
- Bounded message length.
- No dynamic allocation.
- Deterministic timing impact.
- No logging side effects that affect safety behavior.

### Logging Wrapper Pattern

```c
#ifndef PROJECT_LOG_H
#define PROJECT_LOG_H

#include "project_status.h"

typedef enum
{
    PROJECT_LOG_ID_STARTUP = 1,
    PROJECT_LOG_ID_SHUTDOWN = 2,
    PROJECT_LOG_ID_IO_FAILURE = 3,
    PROJECT_LOG_ID_INTERNAL_FAULT = 4
} project_log_id_t;

project_status_t project_log_write(project_log_id_t id);

#endif
```

```c
#include "project_log.h"

project_status_t project_log_write(project_log_id_t id)
{
    switch (id)
    {
        case PROJECT_LOG_ID_STARTUP:
        case PROJECT_LOG_ID_SHUTDOWN:
        case PROJECT_LOG_ID_IO_FAILURE:
        case PROJECT_LOG_ID_INTERNAL_FAULT:
            /* Platform-specific bounded logging implementation belongs here. */
            return PROJECT_STATUS_OK;

        default:
            return PROJECT_STATUS_INVALID_ARGUMENT;
    }
}
```

---

## Secure Coding Requirements

All code shall avoid undefined, unspecified, and implementation-defined behavior.

Requirements:

- Validate all external inputs.
- Validate all pointer arguments before dereference.
- Validate all array indices.
- Use explicit buffer lengths.
- Avoid signed integer overflow.
- Check integer conversions, truncation, and promotions.
- Avoid pointer arithmetic unless simple, bounded, and justified.
- Do not violate strict aliasing rules.
- Do not read uninitialized objects.
- Do not shift by negative values or by a value greater than or equal to the width of the promoted operand.
- Do not depend on object layout, padding, byte order, or alignment unless isolated and documented.
- Do not use unsafe string functions.

Forbidden functions include, but are not limited to:

- `gets`
- `strcpy`
- `strcat`
- `sprintf`
- `scanf` without bounded field widths
- `atoi`, `atol`, `atoll`

Prefer bounded, checked project wrappers over raw library calls.

---

## POSIX Portability Rules

Code shall remain strictly portable across POSIX systems.

- Do not use GNU extensions.
- Do not rely on GCC-specific language extensions.
- Do not rely on non-standard headers.
- Do not rely on Linux-only behavior unless isolated behind an approved portability layer.
- Compile with options that reject non-C99 extensions.

---

## Build Requirements

The project shall build with `make` and GCC in C99 mode.

Warnings shall be enabled and treated as errors.

### Required GCC Flags

```make
CFLAGS += -std=c99
CFLAGS += -pedantic
CFLAGS += -Wall
CFLAGS += -Wextra
CFLAGS += -Werror
CFLAGS += -Wconversion
CFLAGS += -Wsign-conversion
CFLAGS += -Wshadow
CFLAGS += -Wstrict-prototypes
CFLAGS += -Wmissing-prototypes
CFLAGS += -Wold-style-definition
CFLAGS += -Wpointer-arith
CFLAGS += -Wcast-align
CFLAGS += -Wcast-qual
CFLAGS += -Wwrite-strings
CFLAGS += -Wswitch-enum
CFLAGS += -Wundef
CFLAGS += -Wdouble-promotion
CFLAGS += -Wformat=2
CFLAGS += -Winit-self
CFLAGS += -Wmissing-declarations
CFLAGS += -Wredundant-decls
CFLAGS += -Wunreachable-code
CFLAGS += -fno-common
CFLAGS += -fno-strict-aliasing
```

Note: `-fno-strict-aliasing` does not permit unsafe aliasing. Code shall still avoid aliasing violations. This flag reduces compiler optimization risk but is not a substitute for compliance.

### Starter Makefile

```make
CC := gcc

CFLAGS := -std=c99
CFLAGS += -pedantic
CFLAGS += -Wall -Wextra -Werror
CFLAGS += -Wconversion -Wsign-conversion
CFLAGS += -Wshadow
CFLAGS += -Wstrict-prototypes -Wmissing-prototypes -Wold-style-definition
CFLAGS += -Wpointer-arith -Wcast-align -Wcast-qual
CFLAGS += -Wwrite-strings -Wswitch-enum -Wundef
CFLAGS += -Wdouble-promotion -Wformat=2
CFLAGS += -Winit-self -Wmissing-declarations -Wredundant-decls
CFLAGS += -Wunreachable-code
CFLAGS += -fno-common -fno-strict-aliasing

CPPFLAGS := -Iinclude

SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)
TARGET := app

.PHONY: all clean analyze

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

analyze:
	cppcheck --std=c99 --enable=all --inconclusive --force --error-exitcode=1 \
	    --suppress=missingIncludeSystem \
	    --check-level=exhaustive \
	    --addon=misra.json \
	    src include

clean:
	rm -f $(OBJ) $(TARGET)
```

The `misra.json` cppcheck addon configuration shall be maintained by the project and version controlled.

---

## Static Analysis Requirements

cppcheck shall run in CI and before merge.

Minimum required command pattern:

```sh
cppcheck --std=c99 --enable=all --inconclusive --force --error-exitcode=1 \
    --suppress=missingIncludeSystem \
    --check-level=exhaustive \
    --addon=misra.json \
    src include
```

Rules:

- No cppcheck errors are allowed.
- No MISRA violations are allowed without formal deviation approval.
- Suppressions shall be specific, local, justified, and reviewed.
- Broad suppressions are forbidden.
- Generated code shall be included in analysis.

---

## Deviation Process

A deviation is allowed only when compliance is impossible or would increase total system risk.

Each deviation shall be documented before code is merged.

### Required Deviation Record

```text
Deviation ID:
Date:
Author:
Reviewer:
Affected file(s):
Affected function(s):
MISRA rule or project rule:
Description of deviation:
Reason compliance is not practical:
Safety impact:
Security impact:
Determinism impact:
Alternatives considered:
Risk controls or mitigations:
Evidence supporting bounded behavior:
Expiration or review date:
Approval status:
Approver:
```

Deviation records shall be stored in version control under a project-defined location such as:

```text
docs/deviations/
```

---

## Code Structure Rules

- Keep functions short and single-purpose.
- Prefer internal linkage with `static` for file-local functions and objects.
- Avoid global mutable state.
- Use explicit initialization.
- Prefer fixed-width integer types when size matters.
- Keep declarations close to use while maintaining C99 compatibility and readability.
- Avoid complex macros.
- Avoid function-like macros unless no safer alternative exists.
- Do not hide control flow inside macros.
- Header files shall be self-contained and protected by include guards.
- Public APIs shall validate inputs and document ownership, lifetime, and bounds.

---

## Agent Instructions

When modifying this repository, agents shall:

1. Prefer simple, analyzable C over clever or compact code.
2. Preserve deterministic behavior.
3. Avoid introducing dynamic allocation.
4. Ensure every loop has a documented or obvious upper bound.
5. Check every return value.
6. Use explicit project status codes.
7. Avoid direct standard I/O logging in application code.
8. Avoid undefined, unspecified, and implementation-defined behavior.
9. Update deviation records when required.
10. Run or update Make and cppcheck workflows when code changes.

Agents shall not:

- Add dependencies without explicit approval.
- Add GNU extensions.
- Add heap allocation.
- Ignore static analysis findings.
- Silence warnings without justification.
- Replace clear bounded code with abstraction that hides control flow or resource usage.
- Generate code that bypasses MISRA checks.

---

## Pre-Merge Checklist

Before code is merged, confirm:

```text
[ ] Builds with make using GCC C99 flags.
[ ] Builds with zero warnings.
[ ] cppcheck completes with zero unapproved findings.
[ ] MISRA violations are fixed or have approved deviation records.
[ ] No dynamic memory allocation is used.
[ ] All loops have statically provable bounds.
[ ] All recursion has approved deviation and bounded-depth proof.
[ ] All fallible functions use explicit return codes.
[ ] All return values are checked.
[ ] All shared state is synchronized and documented.
[ ] Logging uses only the approved deterministic interface.
[ ] No undefined, unspecified, or implementation-defined behavior is introduced.
[ ] Generated code, if any, was analyzed and reviewed like handwritten code.
```

---

## Default Decision Rule

When there is uncertainty, choose the option that is:

1. More deterministic.
2. Easier to statically analyze.
3. More portable across POSIX systems.
4. More explicit about failure behavior.
5. Simpler to review.
6. Safer under worst-case execution conditions.
