#include <stddef.h>

#include "weather.h"

#define DAFT_WEATHER_BUCKETS 30u
#define DAFT_WEATHER_BUCKET_MS 60000u
#define DAFT_WEATHER_BURST_GAP_MS 8000u
#define DAFT_WEATHER_BURST_FLOOR 51200.0 /* 50 KiB/s */

/* One smoothed, adaptively normalized metric channel. */
typedef struct
{
    double ema;
    double alpha;        /* EMA step for the ~1 Hz sample cadence */
    double range_floor;  /* minimum dynamic range for normalization */
    int has_ema;
    double bucket_min[DAFT_WEATHER_BUCKETS];
    double bucket_max[DAFT_WEATHER_BUCKETS];
    uint8_t bucket_used[DAFT_WEATHER_BUCKETS];
    uint64_t cur_bucket;
} daft_channel_t;

typedef struct
{
    int initialized;
    daft_channel_t cpu;
    daft_channel_t mem;
    daft_channel_t net;
    daft_channel_t disk;
    uint64_t last_burst_ms;
    int burst_primed;
} daft_weather_state_t;

static daft_weather_state_t g_weather;

/* alpha = dt / (tau + dt) for dt = 1 s. */
static void daft_channel_init(daft_channel_t *ch, double tau_s,
                              double range_floor)
{
    size_t i;

    ch->ema = 0.0;
    ch->alpha = 1.0 / (tau_s + 1.0);
    ch->range_floor = range_floor;
    ch->has_ema = 0;
    ch->cur_bucket = 0u;
    /* Bounded by DAFT_WEATHER_BUCKETS. */
    for (i = 0u; i < DAFT_WEATHER_BUCKETS; i++) {
        ch->bucket_used[i] = 0u;
    }
}

static double daft_channel_update(daft_channel_t *ch, double raw,
                                  uint64_t now_ms)
{
    uint64_t bucket = now_ms / (uint64_t)DAFT_WEATHER_BUCKET_MS;
    size_t idx = (size_t)(bucket % (uint64_t)DAFT_WEATHER_BUCKETS);
    double lo;
    double hi;
    double level;
    size_t i;

    if (ch->has_ema == 0) {
        ch->ema = raw;
        ch->has_ema = 1;
    } else {
        ch->ema += ch->alpha * (raw - ch->ema);
    }

    if ((ch->bucket_used[idx] == 0u) || (bucket != ch->cur_bucket)) {
        /* Entering a (possibly recycled) bucket: reset it. */
        ch->bucket_min[idx] = ch->ema;
        ch->bucket_max[idx] = ch->ema;
        ch->bucket_used[idx] = 1u;
        ch->cur_bucket = bucket;
    } else {
        if (ch->ema < ch->bucket_min[idx]) {
            ch->bucket_min[idx] = ch->ema;
        }
        if (ch->ema > ch->bucket_max[idx]) {
            ch->bucket_max[idx] = ch->ema;
        }
    }

    lo = ch->ema;
    hi = ch->ema;
    /* Bounded by DAFT_WEATHER_BUCKETS. */
    for (i = 0u; i < DAFT_WEATHER_BUCKETS; i++) {
        if (ch->bucket_used[i] == 1u) {
            if (ch->bucket_min[i] < lo) {
                lo = ch->bucket_min[i];
            }
            if (ch->bucket_max[i] > hi) {
                hi = ch->bucket_max[i];
            }
        }
    }
    if ((hi - lo) < ch->range_floor) {
        hi = lo + ch->range_floor;
    }

    level = (ch->ema - lo) / (hi - lo);
    if (level < 0.0) {
        level = 0.0;
    }
    if (level > 1.0) {
        level = 1.0;
    }
    return level;
}

daft_status_t daft_weather_init(void)
{
    /* Time constants and normalization floors per the design brief. */
    daft_channel_init(&g_weather.cpu, 90.0, 50.0);      /* permille  */
    daft_channel_init(&g_weather.mem, 240.0, 50.0);     /* permille  */
    daft_channel_init(&g_weather.net, 180.0, 10240.0);  /* bytes/s   */
    daft_channel_init(&g_weather.disk, 60.0, 200.0);    /* sectors/s */
    g_weather.last_burst_ms = 0u;
    g_weather.burst_primed = 0;
    g_weather.initialized = 1;
    return DAFT_STATUS_OK;
}

daft_status_t daft_weather_update(const daft_metrics_sample_t *sample,
                                  uint64_t now_ms, daft_weather_t *out)
{
    double raw_net;

    if ((sample == NULL) || (out == NULL)) {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }
    if (g_weather.initialized == 0) {
        return DAFT_STATUS_UNAVAILABLE;
    }

    raw_net = sample->net_bytes_per_s;

    out->cpu_level = daft_channel_update(&g_weather.cpu,
                                         (double)sample->cpu_permille,
                                         now_ms);
    out->mem_level = daft_channel_update(&g_weather.mem,
                                         (double)sample->mem_permille,
                                         now_ms);
    out->net_level = daft_channel_update(&g_weather.net, raw_net, now_ms);
    out->disk_level = daft_channel_update(&g_weather.disk,
                                          sample->disk_sectors_per_s,
                                          now_ms);

    /* Network burst: the one immediate, legible mapping. Rate-limited. */
    out->burst = 0;
    out->burst_mag = 0.0;
    {
        double threshold = (3.0 * g_weather.net.ema) +
                           DAFT_WEATHER_BURST_FLOOR;
        uint64_t since = now_ms - g_weather.last_burst_ms;

        if ((g_weather.burst_primed == 1) && (raw_net > threshold) &&
            ((g_weather.last_burst_ms == 0u) ||
             (since >= (uint64_t)DAFT_WEATHER_BURST_GAP_MS))) {
            double mag = raw_net /
                         ((6.0 * g_weather.net.ema) +
                          DAFT_WEATHER_BURST_FLOOR);
            out->burst = 1;
            out->burst_mag = (mag > 1.0) ? 1.0 : mag;
            g_weather.last_burst_ms = now_ms;
        }
        /* Never fire on the very first sample (EMA not yet meaningful). */
        g_weather.burst_primed = 1;
    }
    return DAFT_STATUS_OK;
}
