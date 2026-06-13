#include <math.h>
#include <stddef.h>

#include "conductor.h"

#define DAFT_PENT_DEGREES 5u
#define DAFT_CAND_CAP 32u
#define DAFT_DRONE_LO 36u
#define DAFT_TWO_PI 6.28318530717958647692
#define DAFT_DARKNESS_STEP 0.0008 /* per ~1 s update; full sweep ~20 min */

typedef struct
{
    int initialized;
    uint32_t root_pc;
    double base_darkness;
    double darkness;
    double user_scale;
    double breath_period_ms;
    uint64_t last_ms;
    daft_conductor_params_t params;
} daft_conductor_state_t;

static daft_conductor_state_t g_cond;

daft_status_t daft_conductor_init(uint32_t root_pc, uint32_t mood_dark,
                                  uint32_t density_percent, daft_rng_t *rng)
{
    uint32_t breath = 0u;
    daft_status_t status;

    if ((root_pc > 11u) || (mood_dark > 1u) || (rng == NULL) ||
        (density_percent < 25u) || (density_percent > 400u))
    {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }

    /* Breathing LFO period: 7..15 minutes, randomized once per run. */
    status = daft_rng_range(rng, 480000u, &breath);
    if (status != DAFT_STATUS_OK)
    {
        return status;
    }

    g_cond.root_pc = root_pc;
    g_cond.base_darkness = (mood_dark == 1u) ? 0.7 : 0.25;
    g_cond.darkness = g_cond.base_darkness;
    g_cond.user_scale = (double)density_percent / 100.0;
    g_cond.breath_period_ms = 420000.0 + (double)breath;
    g_cond.last_ms = 0u;
    g_cond.params.density = 1.0;
    g_cond.params.brightness = 0.5;
    g_cond.params.drone_presence = 0.5;
    g_cond.params.register_shift = 0;
    g_cond.initialized = 1;
    return DAFT_STATUS_OK;
}

daft_status_t daft_conductor_update(const daft_weather_t *weather,
                                    uint64_t now_ms)
{
    double target;
    double step;
    double breath;
    double density;

    if (weather == NULL)
    {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }
    if (g_cond.initialized == 0)
    {
        return DAFT_STATUS_UNAVAILABLE;
    }

    /* Modal drift: sustained network activity brightens the palette,
     * one small step at a time - no jarring transitions. */
    target = g_cond.base_darkness + 0.25 - (0.5 * weather->net_level);
    if (target < 0.0)
    {
        target = 0.0;
    }
    if (target > 1.0)
    {
        target = 1.0;
    }
    step = target - g_cond.darkness;
    if (step > DAFT_DARKNESS_STEP)
    {
        step = DAFT_DARKNESS_STEP;
    }
    if (step < -DAFT_DARKNESS_STEP)
    {
        step = -DAFT_DARKNESS_STEP;
    }
    g_cond.darkness += step;

    /* Density tide: CPU-driven liveliness times a slow autonomous
     * breathing LFO, so the piece has tides even on an idle machine. */
    breath = 1.0 + (0.3 * sin(DAFT_TWO_PI * ((double)now_ms /
                                             g_cond.breath_period_ms)));
    density = g_cond.user_scale * (0.5 + (1.5 * weather->cpu_level)) *
              breath;
    if (density < 0.2)
    {
        density = 0.2;
    }
    if (density > 2.5)
    {
        density = 2.5;
    }

    g_cond.params.density = density;
    g_cond.params.brightness = weather->net_level;
    g_cond.params.drone_presence = 0.3 + (0.7 * weather->disk_level);
    /* High memory pressure pulls the upper voices down and darker. */
    {
        double shift = (0.5 - weather->mem_level) * 8.0;
        g_cond.params.register_shift = (int)shift;
    }
    g_cond.last_ms = now_ms;
    return DAFT_STATUS_OK;
}

daft_status_t daft_conductor_params(daft_conductor_params_t *out)
{
    if (out == NULL)
    {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }
    if (g_cond.initialized == 0)
    {
        return DAFT_STATUS_UNAVAILABLE;
    }
    *out = g_cond.params;
    return DAFT_STATUS_OK;
}

/* Degree index of note, or DAFT_PENT_DEGREES when not in the set. */
static size_t daft_degree_of(uint8_t note, uint32_t root_pc)
{
    /* Pentatonic degree offsets from the root: 1, 2, 3, 5, 6. */
    static const uint8_t k_degree[DAFT_PENT_DEGREES] = {
        0u, 2u, 4u, 7u, 9u
    };
    uint32_t pc = ((uint32_t)note + 120u - root_pc) % 12u;
    size_t i;

    for (i = 0u; i < DAFT_PENT_DEGREES; i++)
    {
        if ((uint32_t)k_degree[i] == pc)
        {
            return i;
        }
    }
    return DAFT_PENT_DEGREES;
}

daft_status_t daft_conductor_choose_note(daft_rng_t *rng, uint8_t last_note,
                                         uint8_t lo, uint8_t hi, uint8_t *out)
{
    /* Degree emphasis: bright leans on root/5th (Airports dawn); dark
     * leans on the major 6th - the relative minor root - and softens the
     * major 3rd. */
    static const double k_weight_bright[DAFT_PENT_DEGREES] = {
        1.0, 0.55, 0.65, 0.9, 0.6
    };
    static const double k_weight_dark[DAFT_PENT_DEGREES] = {
        0.6, 0.5, 0.35, 0.8, 1.0
    };
    /* Random-walk interval preference by scale-step distance. */
    static const double k_walk[6] = { 0.25, 1.0, 0.8, 0.4, 0.15, 0.05 };
    uint8_t cand[DAFT_CAND_CAP];
    double weight[DAFT_CAND_CAP];
    size_t n_cand = 0u;
    size_t last_idx = DAFT_CAND_CAP;
    double total = 0.0;
    double pick = 0.0;
    double cum = 0.0;
    size_t i;
    uint8_t note;
    daft_status_t status;

    if ((rng == NULL) || (out == NULL) || (lo > hi) || (hi > 127u))
    {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }
    if (g_cond.initialized == 0)
    {
        return DAFT_STATUS_UNAVAILABLE;
    }

    /* Collect pentatonic candidates in the register window.
     * Bounded by the window size and DAFT_CAND_CAP. */
    for (note = lo; (note <= hi) && (n_cand < DAFT_CAND_CAP); note++)
    {
        size_t deg = daft_degree_of(note, g_cond.root_pc);
        if (deg < DAFT_PENT_DEGREES)
        {
            double w = k_weight_bright[deg] +
                       (g_cond.darkness *
                        (k_weight_dark[deg] - k_weight_bright[deg]));
            cand[n_cand] = note;
            weight[n_cand] = w;
            n_cand++;
        }
        if (note == 127u)
        {
            break;
        }
    }
    if (n_cand == 0u)
    {
        return DAFT_STATUS_OUT_OF_RANGE;
    }

    /* Voice leading: prefer small scale-step moves from the last note. */
    if (last_note <= 127u)
    {
        uint8_t best_dist = 255u;
        for (i = 0u; i < n_cand; i++)
        {
            uint8_t d = (cand[i] > last_note) ? (uint8_t)(cand[i] - last_note)
                                              : (uint8_t)(last_note - cand[i]);
            if (d < best_dist)
            {
                best_dist = d;
                last_idx = i;
            }
        }
    }
    for (i = 0u; i < n_cand; i++)
    {
        if (last_idx < DAFT_CAND_CAP)
        {
            size_t d = (i > last_idx) ? (i - last_idx) : (last_idx - i);
            weight[i] *= (d < 6u) ? k_walk[d] : k_walk[5];
        }
        total += weight[i];
    }

    status = daft_rng_unit(rng, &pick);
    if (status != DAFT_STATUS_OK)
    {
        return status;
    }
    pick *= total;

    *out = cand[n_cand - 1u];
    for (i = 0u; i < n_cand; i++)
    {
        cum += weight[i];
        if (pick < cum)
        {
            *out = cand[i];
            break;
        }
    }
    return DAFT_STATUS_OK;
}

daft_status_t daft_conductor_choose_drone(daft_rng_t *rng, uint8_t *out)
{
    double pick = 0.0;
    daft_status_t status;
    uint8_t root_note;

    if ((rng == NULL) || (out == NULL))
    {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }
    if (g_cond.initialized == 0)
    {
        return DAFT_STATUS_UNAVAILABLE;
    }

    status = daft_rng_unit(rng, &pick);
    if (status != DAFT_STATUS_OK)
    {
        return status;
    }

    root_note = (uint8_t)(DAFT_DRONE_LO + g_cond.root_pc);
    /* Mostly the root; sometimes the fifth above it. */
    *out = (pick < 0.7) ? root_note : (uint8_t)(root_note + 7u);
    return DAFT_STATUS_OK;
}
