#ifndef DAFT_DTIME_H
#define DAFT_DTIME_H

#include <stdint.h>

#include "daft_status.h"

/*
 * Project time interface (AGENTS.md determinism rule): all time access goes
 * through this module. The REAL backend uses the POSIX monotonic clock and
 * reports milliseconds since daft_time_init(). The FAKE backend is fully
 * deterministic: time advances only via daft_time_sleep_ms(), enabling
 * byte-reproducible fast-forward simulation.
 */
typedef enum
{
    DAFT_TIME_REAL = 0,
    DAFT_TIME_FAKE = 1
} daft_time_mode_t;

daft_status_t daft_time_init(daft_time_mode_t mode);

/* Milliseconds elapsed since daft_time_init(). */
daft_status_t daft_time_now_ms(uint64_t *out_ms);

/* REAL: bounded monotonic-clock sleep. FAKE: advance the clock by ms. */
daft_status_t daft_time_sleep_ms(uint32_t ms);

#endif /* DAFT_DTIME_H */
