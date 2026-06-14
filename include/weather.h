#ifndef DAFT_WEATHER_H
#define DAFT_WEATHER_H

#include <stdint.h>

#include "metrics.h"
#include "status.h"

/*
 * The "weather" system: raw metric samples are smoothed with per-metric
 * exponential moving averages (time constants chosen per the design brief)
 * and normalized adaptively against a ~30 minute rolling min/max history,
 * so the music is alive on both idle laptops and loaded servers.
 *
 * Outputs are slow 0..1 levels plus one fast, rate-limited event: a
 * network burst (the single deliberately legible mapping - the distant
 * chime).
 */
typedef struct
{
    double cpu_level;   /* event density driver        */
    double mem_level;   /* register / voicing weight   */
    double net_level;   /* mode brightness driver      */
    double disk_level;  /* drone presence driver       */
    int burst;          /* 1 when a network burst fired this sample */
    double burst_mag;   /* burst size, 0..1            */
} daft_weather_t;

daft_status_t daft_weather_init(void);

/* Call at ~1 Hz with a fresh metric sample. */
daft_status_t daft_weather_update(const daft_metrics_sample_t *sample,
                                  uint64_t now_ms, daft_weather_t *out);

#endif /* DAFT_WEATHER_H */
