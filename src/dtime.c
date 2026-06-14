#include <errno.h>
/*
 * Deviation DEV-001 (docs/deviations/DEV-001-time-h.md): <time.h> is
 * required for the POSIX monotonic clock; access is confined to this
 * module per AGENTS.md portability and determinism rules.
 */
/* cppcheck-suppress misra-c2012-21.10 ; DEV-001 */
#include <time.h>

#include "dtime.h"

#define DAFT_TIME_MAX_SLEEP_RETRIES 8u

typedef struct
{
    int initialized;
    daft_time_mode_t mode;
    uint64_t base_ms;  /* REAL: monotonic time at init */
    uint64_t fake_ms;  /* FAKE: current simulated time */
} daft_time_state_t;

static daft_time_state_t g_time = { 0, DAFT_TIME_REAL, 0u, 0u };

static daft_status_t daft_time_read_monotonic(uint64_t *out_ms)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return DAFT_STATUS_IO_ERROR;
    }
    *out_ms = ((uint64_t)ts.tv_sec * 1000u) + ((uint64_t)ts.tv_nsec / 1000000u);
    return DAFT_STATUS_OK;
}

daft_status_t daft_time_init(daft_time_mode_t mode)
{
    daft_status_t status = DAFT_STATUS_OK;

    if ((mode != DAFT_TIME_REAL) && (mode != DAFT_TIME_FAKE)) {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }

    g_time.mode = mode;
    g_time.fake_ms = 0u;
    g_time.base_ms = 0u;

    if (mode == DAFT_TIME_REAL) {
        status = daft_time_read_monotonic(&g_time.base_ms);
    }
    if (status == DAFT_STATUS_OK) {
        g_time.initialized = 1;
    }
    return status;
}

daft_status_t daft_time_now_ms(uint64_t *out_ms)
{
    daft_status_t status = DAFT_STATUS_OK;

    if (out_ms == NULL) {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }
    if (g_time.initialized == 0) {
        return DAFT_STATUS_UNAVAILABLE;
    }

    if (g_time.mode == DAFT_TIME_FAKE) {
        *out_ms = g_time.fake_ms;
    } else {
        uint64_t now = 0u;
        status = daft_time_read_monotonic(&now);
        if (status == DAFT_STATUS_OK) {
            *out_ms = now - g_time.base_ms;
        }
    }
    return status;
}

daft_status_t daft_time_sleep_ms(uint32_t ms)
{
    if (g_time.initialized == 0) {
        return DAFT_STATUS_UNAVAILABLE;
    }

    if (g_time.mode == DAFT_TIME_FAKE) {
        g_time.fake_ms += (uint64_t)ms;
        return DAFT_STATUS_OK;
    }

    {
        struct timespec req;
        unsigned int attempts = 0u;
        int rc = 0;
        uint32_t whole_s = ms / 1000u;
        uint32_t frac_ns = (ms % 1000u) * 1000000u;

        req.tv_sec = (time_t)whole_s;
        req.tv_nsec = (long)frac_ns;

        /* Bounded retry on interruption; clock_nanosleep returns the
         * error number directly (it does not set errno). */
        do {
            struct timespec rem = { 0, 0 };
            /* cppcheck-suppress misra-c2012-17.3 ; clock_nanosleep is
             * declared in <time.h> (POSIX.1-2001 Timers); the analyzer
             * does not expand system headers. */
            rc = clock_nanosleep(CLOCK_MONOTONIC, 0, &req, &rem);
            if (rc == EINTR) {
                req = rem;
            }
            attempts++;
        } while ((rc == EINTR) &&
                 (attempts < DAFT_TIME_MAX_SLEEP_RETRIES));

        return (rc == 0) ? DAFT_STATUS_OK : DAFT_STATUS_IO_ERROR;
    }
}
