#ifndef DAFT_SCHED_H
#define DAFT_SCHED_H

#include <stdint.h>

#include "status.h"

/*
 * Bounded static queue of pending note onsets. Note-offs are not queued
 * here; the voice table (daft_voices) owns note lifetimes.
 */
#define DAFT_SCHED_CAP 64u

typedef struct
{
    uint64_t t_ms;
    uint32_t dur_ms;
    uint8_t ch;
    uint8_t note;
    uint8_t velocity;
    uint8_t used;
} daft_sched_event_t;

typedef struct
{
    daft_sched_event_t slot[DAFT_SCHED_CAP];
} daft_sched_t;

daft_status_t daft_sched_init(daft_sched_t *sched);

/* DAFT_STATUS_FULL when no slot is free (caller drops the note gracefully). */
daft_status_t daft_sched_push(daft_sched_t *sched, uint64_t t_ms, uint8_t ch,
                              uint8_t note, uint8_t velocity,
                              uint32_t dur_ms);

/* Remove and return the earliest event with t_ms <= now.
 * *got = 1 when an event was returned, 0 when none is due. */
daft_status_t daft_sched_pop_due(daft_sched_t *sched, uint64_t now,
                                 daft_sched_event_t *out, int *got);

/* *near = 1 when any pending onset lies within window_ms of t_ms
 * (coincidence damping query). */
daft_status_t daft_sched_onset_near(const daft_sched_t *sched, uint64_t t_ms,
                                    uint32_t window_ms, int *near);

#endif /* DAFT_SCHED_H */
