#ifndef DAFT_LOG_H
#define DAFT_LOG_H

#include <stdint.h>

#include "daft_status.h"

/*
 * Deterministic bounded logging: fixed message IDs, fixed format strings,
 * bounded length, no allocation. Messages are written to the standard
 * error file descriptor with bounded retry.
 *
 * Project decision LOG-1: the return value of the logging functions MAY be
 * explicitly discarded with a (void) cast at call sites. Logging failure has
 * no safe recovery path (the failure cannot itself be logged) and shall never
 * affect control flow.
 */
typedef enum
{
    DAFT_LOG_ID_STARTUP = 1,
    DAFT_LOG_ID_SHUTDOWN = 2,
    DAFT_LOG_ID_SIGNAL_STOP = 3,
    DAFT_LOG_ID_USAGE = 4,
    DAFT_LOG_ID_CONFIG_BAD_ARG = 5,
    DAFT_LOG_ID_CONFIG_FILE_ERROR = 6,
    DAFT_LOG_ID_MIDI_OPEN_FAILED = 7,
    DAFT_LOG_ID_MIDI_WRITE_FAILED = 8,
    DAFT_LOG_ID_METRICS_INIT_FAILED = 9,
    DAFT_LOG_ID_METRICS_SAMPLE_FAILED = 10,
    DAFT_LOG_ID_TRACE_ERROR = 11,
    DAFT_LOG_ID_SCHED_FULL = 12,
    DAFT_LOG_ID_ALL_NOTES_OFF = 13,
    DAFT_LOG_ID_SIM_COMPLETE = 14,
    DAFT_LOG_ID_INTERNAL_FAULT = 15
} daft_log_id_t;

daft_status_t daft_log_write(daft_log_id_t id);

/* As daft_log_write, with a bounded decimal rendering of value appended. */
daft_status_t daft_log_write_u32(daft_log_id_t id, uint32_t value);

#endif /* DAFT_LOG_H */
