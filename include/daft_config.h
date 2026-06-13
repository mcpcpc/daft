#ifndef DAFT_CONFIG_H
#define DAFT_CONFIG_H

#include <stdint.h>

#include "daft_status.h"

#define DAFT_CONFIG_PATH_MAX 256u

/*
 * Runtime configuration. Defaults target live operation: raw MIDI byte
 * stream to /dev/midi, F-rooted bright pentatonic, density 100 %.
 * sim_minutes > 0 selects deterministic simulation: metrics come from
 * trace_path and output is a Standard MIDI File at midi_path.
 */
typedef struct
{
    char midi_path[DAFT_CONFIG_PATH_MAX];
    char trace_path[DAFT_CONFIG_PATH_MAX];
    uint64_t seed;
    uint32_t root_pc;          /* pitch class of the tonal root, 0..11 (5 = F) */
    uint32_t mood_dark;        /* 0 = bright (Airports), 1 = dark (Discreet)   */
    uint32_t density_percent;  /* user density scale, 25..400                  */
    uint32_t sim_minutes;      /* 0 = live mode                                 */
} daft_config_t;

daft_status_t daft_config_default(daft_config_t *cfg);

/* Apply command-line options in order; later options override earlier ones.
 * --config FILE loads key=value pairs (keys: midi_path, seed, root,
 * mood (bright|dark), density_percent); unknown keys are rejected.
 * Options: --config FILE, --midi-out PATH, --seed N, --root N(0..11),
 * --mood bright|dark, --density PERCENT, --simulate TRACE,
 * --sim-minutes N. */
daft_status_t daft_config_parse_args(daft_config_t *cfg, int argc,
                                     char **argv);

#endif /* DAFT_CONFIG_H */
