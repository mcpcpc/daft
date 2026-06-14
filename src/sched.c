#include <stddef.h>

#include "sched.h"

daft_status_t daft_sched_init(daft_sched_t *sched)
{
    size_t i;

    if (sched == NULL)
    {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }
    /* Bounded by DAFT_SCHED_CAP. */
    for (i = 0u; i < DAFT_SCHED_CAP; i++)
    {
        sched->slot[i].used = 0u;
    }
    return DAFT_STATUS_OK;
}

daft_status_t daft_sched_push(daft_sched_t *sched, uint64_t t_ms, uint8_t ch,
                              uint8_t note, uint8_t velocity, uint32_t dur_ms)
{
    size_t i;

    if ((sched == NULL) || (ch > 15u) || (note > 127u) || (velocity > 127u))
    {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }

    /* Bounded by DAFT_SCHED_CAP. */
    for (i = 0u; i < DAFT_SCHED_CAP; i++)
    {
        if (sched->slot[i].used == 0u)
        {
            sched->slot[i].t_ms = t_ms;
            sched->slot[i].dur_ms = dur_ms;
            sched->slot[i].ch = ch;
            sched->slot[i].note = note;
            sched->slot[i].velocity = velocity;
            sched->slot[i].used = 1u;
            return DAFT_STATUS_OK;
        }
    }
    return DAFT_STATUS_FULL;
}

daft_status_t daft_sched_pop_due(daft_sched_t *sched, uint64_t now,
                                 daft_sched_event_t *out, int *got)
{
    size_t i;
    size_t best = DAFT_SCHED_CAP;

    if ((sched == NULL) || (out == NULL) || (got == NULL))
    {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }

    *got = 0;
    /* Bounded by DAFT_SCHED_CAP. */
    for (i = 0u; i < DAFT_SCHED_CAP; i++)
    {
        if ((sched->slot[i].used == 1u) && (sched->slot[i].t_ms <= now))
        {
            if ((best == DAFT_SCHED_CAP) ||
                (sched->slot[i].t_ms < sched->slot[best].t_ms))
            {
                best = i;
            }
        }
    }

    if (best < DAFT_SCHED_CAP)
    {
        *out = sched->slot[best];
        sched->slot[best].used = 0u;
        *got = 1;
    }
    return DAFT_STATUS_OK;
}

daft_status_t daft_sched_onset_near(const daft_sched_t *sched, uint64_t t_ms,
                                    uint32_t window_ms, int *near)
{
    size_t i;

    if ((sched == NULL) || (near == NULL))
    {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }

    *near = 0;
    /* Bounded by DAFT_SCHED_CAP. */
    for (i = 0u; i < DAFT_SCHED_CAP; i++)
    {
        if (sched->slot[i].used == 1u)
        {
            uint64_t a = sched->slot[i].t_ms;
            uint64_t diff = (a > t_ms) ? (a - t_ms) : (t_ms - a);
            if (diff <= (uint64_t)window_ms)
            {
                *near = 1;
            }
        }
    }
    return DAFT_STATUS_OK;
}
