#include <stddef.h>

#include "daft_conductor.h"
#include "daft_layers.h"
#include "daft_log.h"

#define DAFT_LAYER_COUNT 5u
#define DAFT_RECENT_CAP 8u
#define DAFT_FLURRY_WINDOW_MS 10000u
#define DAFT_FLURRY_LIMIT 5u
#define DAFT_COINCIDENCE_MS 200u
#define DAFT_NO_NOTE 0xFFu
#define DAFT_CHIME_CH 5u
#define DAFT_CHIME_LO 84u
#define DAFT_CHIME_HI 96u

typedef struct
{
    uint8_t ch;
    uint32_t period_ms;   /* mutually incommensurate across layers */
    double base_p;        /* per-cycle emission probability        */
    uint8_t lo;
    uint8_t hi;
    uint8_t vel_lo;
    uint8_t vel_hi;
    uint32_t dur_lo_ms;
    uint32_t dur_hi_ms;
    uint8_t is_drone;
    uint8_t shiftable;    /* register_shift applies                */
} daft_layer_cfg_t;

/* Periods chosen near primes so the combined super-period exceeds hours. */
static const daft_layer_cfg_t k_layer[DAFT_LAYER_COUNT] = {
    /* drone  */ { 0u, 47700u, 0.92, 36u, 48u, 45u, 60u, 30000u, 90000u,
                   1u, 0u },
    /* bed 1  */ { 1u, 17300u, 0.50, 48u, 64u, 35u, 60u, 8000u, 20000u,
                   0u, 1u },
    /* bed 2  */ { 2u, 23900u, 0.45, 55u, 67u, 35u, 58u, 9000u, 22000u,
                   0u, 1u },
    /* mel 1  */ { 3u, 31400u, 0.33, 72u, 84u, 35u, 65u, 3000u, 8000u,
                   0u, 1u },
    /* mel 2  */ { 4u, 61100u, 0.30, 76u, 88u, 32u, 60u, 3000u, 9000u,
                   0u, 1u }
};

typedef struct
{
    uint64_t next_cycle_ms;
    uint8_t last_note;
} daft_layer_state_t;

typedef struct
{
    int initialized;
    daft_layer_state_t layer[DAFT_LAYER_COUNT];
    uint8_t chime_last_note;
    uint64_t recent_onset[DAFT_RECENT_CAP];
    size_t recent_idx;
} daft_layers_state_t;

static daft_layers_state_t g_layers;

static void daft_layers_record_onset(uint64_t onset_ms)
{
    g_layers.recent_onset[g_layers.recent_idx] = onset_ms;
    g_layers.recent_idx = (g_layers.recent_idx + 1u) % DAFT_RECENT_CAP;
}

static size_t daft_layers_recent_count(uint64_t now)
{
    size_t i;
    size_t count = 0u;
    uint64_t horizon = (now > (uint64_t)DAFT_FLURRY_WINDOW_MS)
                           ? (now - (uint64_t)DAFT_FLURRY_WINDOW_MS)
                           : 0u;

    /* Bounded by DAFT_RECENT_CAP. */
    for (i = 0u; i < DAFT_RECENT_CAP; i++)
    {
        if ((g_layers.recent_onset[i] != 0u) &&
            (g_layers.recent_onset[i] >= horizon))
        {
            count++;
        }
    }
    return count;
}

daft_status_t daft_layers_init(daft_rng_t *rng, uint64_t now)
{
    size_t i;

    if (rng == NULL)
    {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }

    /* Stagger first cycles randomly so layers never start aligned. */
    for (i = 0u; i < DAFT_LAYER_COUNT; i++)
    {
        uint32_t stagger = 0u;
        daft_status_t status = daft_rng_range(rng, k_layer[i].period_ms,
                                              &stagger);
        if (status != DAFT_STATUS_OK)
        {
            return status;
        }
        /* First events within the first half-period, plus a short
         * settling delay after the program changes. */
        g_layers.layer[i].next_cycle_ms = now + 2000u +
                                          ((uint64_t)stagger / 2u);
        g_layers.layer[i].last_note = DAFT_NO_NOTE;
    }
    for (i = 0u; i < DAFT_RECENT_CAP; i++)
    {
        g_layers.recent_onset[i] = 0u;
    }
    g_layers.recent_idx = 0u;
    g_layers.chime_last_note = DAFT_NO_NOTE;
    g_layers.initialized = 1;
    return DAFT_STATUS_OK;
}

/* Apply the conductor's register shift to a window, clamped to MIDI range. */
static void daft_layers_shift_window(const daft_layer_cfg_t *cfg, int shift,
                                     uint8_t *lo, uint8_t *hi)
{
    int s = (cfg->shiftable == 1u) ? shift : 0;
    int new_lo = (int)cfg->lo + s;
    int new_hi = (int)cfg->hi + s;

    if (new_lo < 0)
    {
        new_lo = 0;
    }
    if (new_hi > 127)
    {
        new_hi = 127;
    }
    if (new_hi < (new_lo + 12))
    {
        new_hi = new_lo + 12; /* keep at least an octave of candidates */
    }
    *lo = (uint8_t)new_lo;
    *hi = (uint8_t)new_hi;
}

/* Schedule one onset with jitter and coincidence damping. */
static daft_status_t daft_layers_schedule(uint64_t now, daft_rng_t *rng,
                                          daft_sched_t *sched,
                                          const daft_layer_cfg_t *cfg,
                                          uint8_t note, uint8_t velocity,
                                          uint32_t dur_ms)
{
    uint32_t jitter = 0u;
    uint64_t onset;
    int near = 0;
    daft_status_t status = daft_rng_range(rng,
                                          (cfg->period_ms * 15u) / 100u,
                                          &jitter);

    if (status != DAFT_STATUS_OK)
    {
        return status;
    }
    onset = now + (uint64_t)jitter;

    /* Coincidence damping: near-simultaneity that is not a deliberate
     * chord sounds like an error - push the onset away. */
    status = daft_sched_onset_near(sched, onset, DAFT_COINCIDENCE_MS, &near);
    if (status != DAFT_STATUS_OK)
    {
        return status;
    }
    if (near == 1)
    {
        uint32_t push = 0u;
        status = daft_rng_range(rng, 1500u, &push);
        if (status != DAFT_STATUS_OK)
        {
            return status;
        }
        onset += 500u + (uint64_t)push;
    }

    status = daft_sched_push(sched, onset, cfg->ch, note, velocity, dur_ms);
    if (status == DAFT_STATUS_FULL)
    {
        /* Degrade safely: drop the note, keep playing. */
        (void)daft_log_write(DAFT_LOG_ID_SCHED_FULL); /* LOG-1 */
        status = DAFT_STATUS_OK;
    }
    if (status == DAFT_STATUS_OK)
    {
        daft_layers_record_onset(onset);
    }
    return status;
}

static daft_status_t daft_layers_cycle(uint64_t now, daft_rng_t *rng,
                                       daft_sched_t *sched, size_t idx,
                                       const daft_conductor_params_t *params)
{
    const daft_layer_cfg_t *cfg = &k_layer[idx];
    daft_layer_state_t *st = &g_layers.layer[idx];
    double p_eff;
    double roll = 0.0;
    daft_status_t status;

    /* Emission probability: the weather scales the garden's liveliness. */
    p_eff = cfg->base_p * params->density;
    if (cfg->is_drone == 1u)
    {
        /* The drone is what makes randomness sound intentional. */
        p_eff = (p_eff < 0.85) ? 0.85 : p_eff;
    }
    if (p_eff > 0.97)
    {
        p_eff = 0.97;
    }

    /* Post-flurry calm: silence shaped deliberately reads as intention. */
    if ((cfg->is_drone == 0u) &&
        (daft_layers_recent_count(now) >= DAFT_FLURRY_LIMIT))
    {
        return DAFT_STATUS_OK;
    }

    status = daft_rng_unit(rng, &roll);
    if (status != DAFT_STATUS_OK)
    {
        return status;
    }
    if (roll >= p_eff)
    {
        return DAFT_STATUS_OK; /* silence, a first-class output */
    }

    {
        uint8_t note = 0u;
        uint8_t lo = cfg->lo;
        uint8_t hi = cfg->hi;
        uint32_t vel_span = 0u;
        uint32_t dur_span = 0u;
        uint8_t velocity;
        uint32_t dur_ms;

        if (cfg->is_drone == 1u)
        {
            status = daft_conductor_choose_drone(rng, &note);
        }
        else
        {
            daft_layers_shift_window(cfg, params->register_shift, &lo, &hi);
            status = daft_conductor_choose_note(rng, st->last_note, lo, hi,
                                                &note);
        }
        if (status != DAFT_STATUS_OK)
        {
            return status;
        }

        status = daft_rng_range(rng,
                                ((uint32_t)cfg->vel_hi -
                                 (uint32_t)cfg->vel_lo) + 1u,
                                &vel_span);
        if (status != DAFT_STATUS_OK)
        {
            return status;
        }
        velocity = (uint8_t)((uint32_t)cfg->vel_lo + vel_span);

        status = daft_rng_range(rng, (cfg->dur_hi_ms - cfg->dur_lo_ms) + 1u,
                                &dur_span);
        if (status != DAFT_STATUS_OK)
        {
            return status;
        }
        dur_ms = cfg->dur_lo_ms + dur_span;

        status = daft_layers_schedule(now, rng, sched, cfg, note, velocity,
                                      dur_ms);
        if (status == DAFT_STATUS_OK)
        {
            st->last_note = note;
        }
    }
    return status;
}

daft_status_t daft_layers_tick(uint64_t now, daft_rng_t *rng,
                               daft_sched_t *sched)
{
    size_t i;
    daft_conductor_params_t params;
    daft_status_t status;

    if ((rng == NULL) || (sched == NULL))
    {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }
    if (g_layers.initialized == 0)
    {
        return DAFT_STATUS_UNAVAILABLE;
    }

    status = daft_conductor_params(&params);
    if (status != DAFT_STATUS_OK)
    {
        return status;
    }

    /* Bounded by DAFT_LAYER_COUNT. */
    for (i = 0u; i < DAFT_LAYER_COUNT; i++)
    {
        if (now >= g_layers.layer[i].next_cycle_ms)
        {
            status = daft_layers_cycle(now, rng, sched, i, &params);
            if (status != DAFT_STATUS_OK)
            {
                return status;
            }
            /* One decision point per cycle; if the process stalled past
             * several periods, re-anchor instead of replaying them. */
            g_layers.layer[i].next_cycle_ms += k_layer[i].period_ms;
            if (g_layers.layer[i].next_cycle_ms <= now)
            {
                g_layers.layer[i].next_cycle_ms =
                    now + k_layer[i].period_ms;
            }
        }
    }
    return DAFT_STATUS_OK;
}

daft_status_t daft_layers_chime(uint64_t now, double magnitude,
                                daft_rng_t *rng, daft_sched_t *sched)
{
    double p;
    double roll = 0.0;
    double mag = magnitude;
    daft_status_t status;

    if ((rng == NULL) || (sched == NULL))
    {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }
    if (g_layers.initialized == 0)
    {
        return DAFT_STATUS_UNAVAILABLE;
    }

    if (mag < 0.0)
    {
        mag = 0.0;
    }
    if (mag > 1.0)
    {
        mag = 1.0;
    }

    /* Probabilistic, never 1:1 - the chime stays a distant suggestion. */
    p = 0.35 + (0.6 * mag);
    status = daft_rng_unit(rng, &roll);
    if (status != DAFT_STATUS_OK)
    {
        return status;
    }
    if (roll >= p)
    {
        return DAFT_STATUS_OK;
    }

    {
        uint8_t note = 0u;
        uint32_t delay = 0u;
        uint32_t dur_span = 0u;
        uint8_t velocity;

        status = daft_conductor_choose_note(rng, g_layers.chime_last_note,
                                            DAFT_CHIME_LO, DAFT_CHIME_HI,
                                            &note);
        if (status != DAFT_STATUS_OK)
        {
            return status;
        }
        status = daft_rng_range(rng, 400u, &delay);
        if (status != DAFT_STATUS_OK)
        {
            return status;
        }
        status = daft_rng_range(rng, 2500u, &dur_span);
        if (status != DAFT_STATUS_OK)
        {
            return status;
        }
        velocity = (uint8_t)(25u + (uint32_t)(mag * 25.0));

        status = daft_sched_push(sched, now + (uint64_t)delay, DAFT_CHIME_CH,
                                 note, velocity, 1500u + dur_span);
        if (status == DAFT_STATUS_FULL)
        {
            (void)daft_log_write(DAFT_LOG_ID_SCHED_FULL); /* LOG-1 */
            status = DAFT_STATUS_OK;
        }
        if (status == DAFT_STATUS_OK)
        {
            g_layers.chime_last_note = note;
        }
    }
    return status;
}
