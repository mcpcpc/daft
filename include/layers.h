#ifndef DAFT_LAYERS_H
#define DAFT_LAYERS_H

#include <stdint.h>

#include "rng.h"
#include "sched.h"
#include "status.h"

/*
 * The voice layers - independent cyclic state machines with mutually
 * incommensurate periods (the Music for Airports tape-loop construction).
 * Layers never talk to each other; they read the Conductor and may emit
 * at most one onset per cycle, probabilistically. Silence is a
 * first-class output.
 *
 * Channel plan: 0 drone, 1-2 harmonic beds, 3-4 melodic fragments,
 * 5 chimes (event-driven, network bursts only).
 */
#define DAFT_LAYERS_CHANNELS 6u

daft_status_t daft_layers_init(daft_rng_t *rng, uint64_t now);

/* Advance all cyclic layers; push due onsets into sched. */
daft_status_t daft_layers_tick(uint64_t now, daft_rng_t *rng,
                               daft_sched_t *sched);

/* A network burst arrived: maybe ring a distant chime. */
daft_status_t daft_layers_chime(uint64_t now, double magnitude,
                                daft_rng_t *rng, daft_sched_t *sched);

#endif /* DAFT_LAYERS_H */
