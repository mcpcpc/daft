#ifndef DAFT_RNG_H
#define DAFT_RNG_H

#include <stdint.h>

#include "daft_status.h"

/*
 * Deterministic, explicitly seeded PRNG (xorshift64*). Not cryptographic;
 * used solely for musical decisions. Identical seeds yield identical
 * sequences on all platforms.
 */
typedef struct
{
    uint64_t state;
} daft_rng_t;

daft_status_t daft_rng_seed(daft_rng_t *rng, uint64_t seed);

/* Uniform integer in [0, bound). bound shall be > 0. */
daft_status_t daft_rng_range(daft_rng_t *rng, uint32_t bound, uint32_t *out);

/* Uniform double in [0.0, 1.0). */
daft_status_t daft_rng_unit(daft_rng_t *rng, double *out);

#endif /* DAFT_RNG_H */
