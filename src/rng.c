#include <stddef.h>

#include "rng.h"

/* xorshift64* multiplier (Vigna, "An experimental exploration of
 * Marsaglia's xorshift generators"). */
#define DAFT_RNG_MULT 2685821657736338717ULL
#define DAFT_RNG_DEFAULT_STATE 0x9E3779B97F4A7C15ULL

daft_status_t daft_rng_seed(daft_rng_t *rng, uint64_t seed)
{
    if (rng == NULL) {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }
    /* xorshift state shall be nonzero. */
    rng->state = (seed != 0u) ? seed : DAFT_RNG_DEFAULT_STATE;
    return DAFT_STATUS_OK;
}

static uint64_t daft_rng_next(daft_rng_t *rng)
{
    uint64_t s = rng->state;

    s ^= s >> 12;
    s ^= s << 25;
    s ^= s >> 27;
    rng->state = s;
    return s * DAFT_RNG_MULT;
}

static daft_status_t daft_rng_u32(daft_rng_t *rng, uint32_t *out)
{
    if ((rng == NULL) || (out == NULL) || (rng->state == 0u)) {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }
    *out = (uint32_t)(daft_rng_next(rng) >> 32);
    return DAFT_STATUS_OK;
}

daft_status_t daft_rng_range(daft_rng_t *rng, uint32_t bound, uint32_t *out)
{
    uint32_t raw = 0u;
    daft_status_t status;

    if ((out == NULL) || (bound == 0u)) {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }

    status = daft_rng_u32(rng, &raw);
    if (status == DAFT_STATUS_OK) {
        /* Multiply-shift range reduction: negligible bias, no modulo. */
        *out = (uint32_t)(((uint64_t)raw * (uint64_t)bound) >> 32);
    }
    return status;
}

daft_status_t daft_rng_unit(daft_rng_t *rng, double *out)
{
    if ((rng == NULL) || (out == NULL) || (rng->state == 0u)) {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }
    /* 53 high-quality bits into [0, 1). */
    *out = (double)(daft_rng_next(rng) >> 11) *
           (1.0 / 9007199254740992.0);
    return DAFT_STATUS_OK;
}
