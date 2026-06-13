#ifndef DAFT_CONDUCTOR_H
#define DAFT_CONDUCTOR_H

#include <stdint.h>

#include "rng.h"
#include "status.h"
#include "weather.h"

/*
 * The Conductor owns global musical state - the "garden", not the plants.
 * Layers read parameters from it and never coordinate with each other;
 * emergent counterpoint comes from phase relationships alone.
 *
 * Pitch material is a pentatonic set (root, M2, M3, P5, M6) whose
 * per-degree probability weights blend between a bright (Airports) and a
 * dark (relative-minor emphasis) table. Darkness drifts slowly toward a
 * target set by smoothed network activity; the pitch-class set itself
 * never changes, so any vertical combination stays consonant.
 */
typedef struct
{
    double density;         /* global emission multiplier, 0.2..2.5  */
    double brightness;      /* 0..1, drives CC74                      */
    double drone_presence;  /* 0..1, drives drone CC11                */
    int register_shift;     /* semitones, applied to shiftable layers */
} daft_conductor_params_t;

daft_status_t daft_conductor_init(uint32_t root_pc, uint32_t mood_dark,
                                  uint32_t density_percent, daft_rng_t *rng);

/* Call at ~1 Hz after daft_weather_update. */
daft_status_t daft_conductor_update(const daft_weather_t *weather,
                                    uint64_t now_ms);

daft_status_t daft_conductor_params(daft_conductor_params_t *out);

/* Weighted random-walk note choice from the pentatonic set within
 * [lo, hi]. last_note = 0xFF means "no previous note". */
daft_status_t daft_conductor_choose_note(daft_rng_t *rng, uint8_t last_note,
                                         uint8_t lo, uint8_t hi,
                                         uint8_t *out);

/* Drone pitch: the root (usually) or the fifth in the drone register. */
daft_status_t daft_conductor_choose_drone(daft_rng_t *rng, uint8_t *out);

#endif /* DAFT_CONDUCTOR_H */
