/*
 * Deviation DEV-002 (docs/deviations/DEV-002-signal-h.md): <signal.h> is
 * required for clean daemon shutdown (SIGINT/SIGTERM -> all-notes-off).
 * Handlers only set a volatile sig_atomic_t flag; all cleanup runs in the
 * main loop.
 */
/* cppcheck-suppress misra-c2012-21.5 ; DEV-002 */
#include <signal.h>
#include <stddef.h>
#include <stdlib.h>

#include "conductor.h"
#include "config.h"
#include "dtime.h"
#include "layers.h"
#include "log.h"
#include "metrics.h"
#include "midi.h"
#include "rng.h"
#include "sched.h"
#include "voices.h"
#include "weather.h"

#define DAFT_TICK_MS 50u
#define DAFT_SAMPLE_INTERVAL_MS 1000u
#define DAFT_CC_INTERVAL_MS 100u
#define DAFT_CC_STEP 2.0
#define DAFT_CC_COUNT 5u
#define DAFT_MAX_METRIC_FAILURES 10u

static volatile sig_atomic_t g_stop = 0;

static void daft_handle_stop(int sig)
{
    (void)sig;
    g_stop = 1;
}

static daft_status_t daft_setup_signals(void)
{
    struct sigaction sa;

    sa.sa_handler = daft_handle_stop;
    sa.sa_flags = 0;
    if (sigemptyset(&sa.sa_mask) != 0)
    {
        return DAFT_STATUS_INTERNAL_ERROR;
    }
    if (sigaction(SIGINT, &sa, NULL) != 0)
    {
        return DAFT_STATUS_INTERNAL_ERROR;
    }
    if (sigaction(SIGTERM, &sa, NULL) != 0)
    {
        return DAFT_STATUS_INTERNAL_ERROR;
    }
    return DAFT_STATUS_OK;
}

static daft_status_t daft_setup_channels(daft_midi_t *midi, uint64_t now)
{
    /* GM programs (0-based): Warm Pad, Bowed Glass, Strings 2, Vibraphone,
     * Celesta, Glockenspiel. Reverb does half of Eno's work: CC91 high. */
    static const uint8_t k_program[DAFT_LAYERS_CHANNELS] = {
        88u, 91u, 48u, 11u, 8u, 9u
    };
    static const uint8_t k_pan[DAFT_LAYERS_CHANNELS] = {
        64u, 44u, 84u, 54u, 74u, 64u
    };
    uint8_t ch;
    daft_status_t status = DAFT_STATUS_OK;

    /* Bounded by DAFT_LAYERS_CHANNELS. */
    for (ch = 0u; (ch < DAFT_LAYERS_CHANNELS) &&
                  (status == DAFT_STATUS_OK); ch++)
    {
        status = daft_midi_program(midi, now, ch, k_program[ch]);
        if (status == DAFT_STATUS_OK)
        {
            status = daft_midi_control(midi, now, ch, 7u, 100u); /* volume */
        }
        if (status == DAFT_STATUS_OK)
        {
            status = daft_midi_control(midi, now, ch, 10u, k_pan[ch]);
        }
        if (status == DAFT_STATUS_OK)
        {
            status = daft_midi_control(midi, now, ch, 91u, 100u); /* reverb */
        }
    }
    return status;
}

/* Smoothed continuous controllers: expression on the drone, brightness on
 * the moving voices. Updated at most every DAFT_CC_INTERVAL_MS, stepped
 * gradually, emitted only when the rounded value changes. */
typedef struct
{
    uint8_t ch;
    uint8_t ctrl;
    double current;
    double target;
    uint64_t last_emit_ms;
    uint8_t last_sent;
} daft_cc_t;

static daft_cc_t g_cc[DAFT_CC_COUNT] = {
    { 0u, 11u, 70.0, 70.0, 0u, 255u },
    { 1u, 74u, 60.0, 60.0, 0u, 255u },
    { 2u, 74u, 60.0, 60.0, 0u, 255u },
    { 3u, 74u, 60.0, 60.0, 0u, 255u },
    { 4u, 74u, 60.0, 60.0, 0u, 255u }
};

static void daft_cc_set_targets(const daft_conductor_params_t *params)
{
    double bright_cc = 30.0 + (params->brightness * 65.0);
    size_t i;

    g_cc[0].target = 40.0 + (params->drone_presence * 60.0);
    /* Bounded by DAFT_CC_COUNT. */
    for (i = 1u; i < DAFT_CC_COUNT; i++)
    {
        g_cc[i].target = bright_cc;
    }
}

static daft_status_t daft_cc_tick(daft_midi_t *midi, uint64_t now)
{
    size_t i;
    daft_status_t status = DAFT_STATUS_OK;

    /* Bounded by DAFT_CC_COUNT. */
    for (i = 0u; (i < DAFT_CC_COUNT) && (status == DAFT_STATUS_OK); i++)
    {
        daft_cc_t *cc = &g_cc[i];

        if ((now - cc->last_emit_ms) >= (uint64_t)DAFT_CC_INTERVAL_MS)
        {
            double step = cc->target - cc->current;
            uint8_t rounded;

            if (step > DAFT_CC_STEP)
            {
                step = DAFT_CC_STEP;
            }
            if (step < -DAFT_CC_STEP)
            {
                step = -DAFT_CC_STEP;
            }
            cc->current += step;
            rounded = (uint8_t)(cc->current + 0.5);
            if (rounded > 127u)
            {
                rounded = 127u;
            }

            if (rounded != cc->last_sent)
            {
                status = daft_midi_control(midi, now, cc->ch, cc->ctrl,
                                           rounded);
                if (status == DAFT_STATUS_OK)
                {
                    cc->last_sent = rounded;
                    cc->last_emit_ms = now;
                }
            }
        }
    }
    return status;
}

/* One ~1 Hz weather step: sample metrics, update weather and conductor,
 * maybe ring the burst chime, refresh CC targets. */
static daft_status_t daft_weather_step(uint64_t now, daft_rng_t *rng,
                                       daft_sched_t *sched,
                                       uint32_t *metric_failures)
{
    daft_metrics_sample_t sample;
    daft_weather_t weather;
    daft_conductor_params_t params;
    daft_status_t status = daft_metrics_sample(now, &sample);

    if (status != DAFT_STATUS_OK)
    {
        (*metric_failures)++;
        (void)daft_log_write_u32(DAFT_LOG_ID_METRICS_SAMPLE_FAILED,
                                 (uint32_t)status); /* LOG-1 */
        /* Degrade safely: keep playing on the previous weather. */
        return (*metric_failures >= DAFT_MAX_METRIC_FAILURES)
                   ? DAFT_STATUS_IO_ERROR
                   : DAFT_STATUS_OK;
    }
    *metric_failures = 0u;

    status = daft_weather_update(&sample, now, &weather);
    if (status == DAFT_STATUS_OK)
    {
        status = daft_conductor_update(&weather, now);
    }
    if ((status == DAFT_STATUS_OK) && (weather.burst == 1))
    {
        status = daft_layers_chime(now, weather.burst_mag, rng, sched);
    }
    if (status == DAFT_STATUS_OK)
    {
        status = daft_conductor_params(&params);
    }
    if (status == DAFT_STATUS_OK)
    {
        daft_cc_set_targets(&params);
    }
    return status;
}

static daft_status_t daft_run(const daft_config_t *cfg, daft_rng_t *rng,
                              daft_midi_t *midi, daft_sched_t *sched,
                              daft_voices_t *voices)
{
    uint64_t now = 0u;
    uint64_t sim_end_ms = (uint64_t)cfg->sim_minutes * 60000u;
    uint32_t metric_failures = 0u;
    daft_status_t status = daft_time_now_ms(&now);

    if (status != DAFT_STATUS_OK)
    {
        return status;
    }
    status = daft_setup_channels(midi, now);
    if (status != DAFT_STATUS_OK)
    {
        return status;
    }
    status = daft_layers_init(rng, now);
    if (status != DAFT_STATUS_OK)
    {
        return status;
    }

    /*
     * Top-level service loop (approved per AGENTS.md rule 5): exits when
     * a stop signal is raised, on the first error, or, in simulation
     * mode, when simulated time reaches sim_end_ms (a static bound of
     * sim_minutes * 60000 / 50 iterations). Timing is driven by the
     * project time interface; each iteration performs bounded work and
     * sleeps DAFT_TICK_MS.
     */
    {
        int done = 0;
        uint64_t last_sample_ms = 0u;

        while ((g_stop == 0) && (done == 0) && (status == DAFT_STATUS_OK))
        {
            status = daft_time_now_ms(&now);

            if ((status == DAFT_STATUS_OK) && (sim_end_ms > 0u) &&
                (now >= sim_end_ms))
            {
                (void)daft_log_write(DAFT_LOG_ID_SIM_COMPLETE); /* LOG-1 */
                done = 1;
            }

            if ((status == DAFT_STATUS_OK) && (done == 0) &&
                ((now - last_sample_ms) >=
                 (uint64_t)DAFT_SAMPLE_INTERVAL_MS))
            {
                status = daft_weather_step(now, rng, sched,
                                           &metric_failures);
                last_sample_ms = now;
            }

            if ((status == DAFT_STATUS_OK) && (done == 0))
            {
                status = daft_layers_tick(now, rng, sched);
            }

            if ((status == DAFT_STATUS_OK) && (done == 0))
            {
                /* Commit due onsets. Bounded by the queue capacity. */
                unsigned int pops = 0u;
                int got = 1;

                while ((status == DAFT_STATUS_OK) && (got == 1) &&
                       (pops < DAFT_SCHED_CAP))
                {
                    daft_sched_event_t ev;

                    status = daft_sched_pop_due(sched, now, &ev, &got);
                    if ((status == DAFT_STATUS_OK) && (got == 1))
                    {
                        status = daft_voices_note_on(voices, midi, now,
                                                     ev.ch, ev.note,
                                                     ev.velocity,
                                                     ev.dur_ms);
                    }
                    pops++;
                }
            }

            if ((status == DAFT_STATUS_OK) && (done == 0))
            {
                status = daft_voices_tick(voices, midi, now);
            }
            if ((status == DAFT_STATUS_OK) && (done == 0))
            {
                status = daft_cc_tick(midi, now);
            }
            if ((status == DAFT_STATUS_OK) && (done == 0))
            {
                status = daft_time_sleep_ms(DAFT_TICK_MS);
            }
        }
    }

    if (g_stop != 0)
    {
        (void)daft_log_write(DAFT_LOG_ID_SIGNAL_STOP); /* LOG-1 */
    }
    return status;
}

static daft_status_t daft_shutdown_midi(daft_midi_t *midi,
                                        daft_voices_t *voices)
{
    uint64_t now = 0u;
    daft_status_t status = daft_time_now_ms(&now);
    daft_status_t final_status = status;

    if (status == DAFT_STATUS_OK)
    {
        uint8_t ch;

        status = daft_voices_all_off(voices, midi, now);
        if (status != DAFT_STATUS_OK)
        {
            final_status = status;
        }
        /* Belt and braces: CC123 All Notes Off on every used channel. */
        for (ch = 0u; ch < DAFT_LAYERS_CHANNELS; ch++)
        {
            status = daft_midi_control(midi, now, ch, 123u, 0u);
            if (status != DAFT_STATUS_OK)
            {
                final_status = status;
            }
        }
        (void)daft_log_write(DAFT_LOG_ID_ALL_NOTES_OFF); /* LOG-1 */
    }

    status = daft_midi_close(midi, now);
    if (status != DAFT_STATUS_OK)
    {
        final_status = status;
    }
    return final_status;
}

int main(int argc, char **argv)
{
    static daft_config_t cfg;
    static daft_rng_t rng;
    static daft_midi_t midi;
    static daft_sched_t sched;
    static daft_voices_t voices;
    int sim;
    daft_status_t status = daft_config_default(&cfg);

    if (status == DAFT_STATUS_OK)
    {
        status = daft_config_parse_args(&cfg, argc, argv);
    }
    if (status != DAFT_STATUS_OK)
    {
        (void)daft_log_write(DAFT_LOG_ID_CONFIG_BAD_ARG); /* LOG-1 */
        (void)daft_log_write(DAFT_LOG_ID_USAGE);          /* LOG-1 */
        return EXIT_FAILURE;
    }

    sim = (cfg.sim_minutes > 0u) ? 1 : 0;
    if ((sim == 1) && (cfg.trace_path[0] == '\0'))
    {
        (void)daft_log_write(DAFT_LOG_ID_CONFIG_BAD_ARG); /* LOG-1 */
        (void)daft_log_write(DAFT_LOG_ID_USAGE);          /* LOG-1 */
        return EXIT_FAILURE;
    }

    status = daft_setup_signals();
    if (status != DAFT_STATUS_OK)
    {
        (void)daft_log_write(DAFT_LOG_ID_INTERNAL_FAULT); /* LOG-1 */
        return EXIT_FAILURE;
    }
    status = daft_time_init((sim == 1) ? DAFT_TIME_FAKE : DAFT_TIME_REAL);
    if (status == DAFT_STATUS_OK)
    {
        status = daft_rng_seed(&rng, cfg.seed);
    }
    if (status == DAFT_STATUS_OK)
    {
        status = daft_sched_init(&sched);
    }
    if (status == DAFT_STATUS_OK)
    {
        status = daft_voices_init(&voices);
    }
    if (status == DAFT_STATUS_OK)
    {
        status = daft_weather_init();
    }
    if (status == DAFT_STATUS_OK)
    {
        status = daft_conductor_init(cfg.root_pc, cfg.mood_dark,
                                     cfg.density_percent, &rng);
    }
    if (status != DAFT_STATUS_OK)
    {
        (void)daft_log_write(DAFT_LOG_ID_INTERNAL_FAULT); /* LOG-1 */
        return EXIT_FAILURE;
    }

    status = daft_metrics_init((sim == 1) ? DAFT_METRICS_TRACE
                                          : DAFT_METRICS_PROC,
                               (sim == 1) ? cfg.trace_path : NULL);
    if (status != DAFT_STATUS_OK)
    {
        (void)daft_log_write(DAFT_LOG_ID_METRICS_INIT_FAILED); /* LOG-1 */
        return EXIT_FAILURE;
    }

    status = daft_midi_open(&midi, (sim == 1) ? DAFT_MIDI_SINK_SMF
                                              : DAFT_MIDI_SINK_STREAM,
                            cfg.midi_path);
    if (status != DAFT_STATUS_OK)
    {
        (void)daft_log_write(DAFT_LOG_ID_MIDI_OPEN_FAILED); /* LOG-1 */
        (void)daft_metrics_close();
        return EXIT_FAILURE;
    }

    (void)daft_log_write(DAFT_LOG_ID_STARTUP); /* LOG-1 */

    status = daft_run(&cfg, &rng, &midi, &sched, &voices);
    if (status != DAFT_STATUS_OK)
    {
        (void)daft_log_write_u32(DAFT_LOG_ID_MIDI_WRITE_FAILED,
                                 (uint32_t)status); /* LOG-1 */
    }

    {
        daft_status_t shutdown_status = daft_shutdown_midi(&midi, &voices);
        daft_status_t metrics_status = daft_metrics_close();

        if (status == DAFT_STATUS_OK)
        {
            status = shutdown_status;
        }
        if (status == DAFT_STATUS_OK)
        {
            status = metrics_status;
        }
    }

    (void)daft_log_write(DAFT_LOG_ID_SHUTDOWN); /* LOG-1 */
    return (status == DAFT_STATUS_OK) ? EXIT_SUCCESS : EXIT_FAILURE;
}
