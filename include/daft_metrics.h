#ifndef DAFT_METRICS_H
#define DAFT_METRICS_H

#include <stdint.h>

#include "daft_status.h"

/*
 * System metric sampling behind a portability layer (AGENTS.md POSIX rule):
 *  - DAFT_METRICS_PROC: Linux backend reading /proc (the only
 *    platform-specific code in the project).
 *  - DAFT_METRICS_TRACE: deterministic CSV trace backend for simulation.
 *    Line format: t_seconds,cpu_permille,mem_permille,net_Bps,disk_sps
 *    Values hold until the next timestamped line. Lines not starting with
 *    a digit are ignored (headers, comments).
 *
 * All rates are computed inside the backend from counter deltas.
 */
typedef enum
{
    DAFT_METRICS_PROC = 0,
    DAFT_METRICS_TRACE = 1
} daft_metrics_mode_t;

typedef struct
{
    uint32_t cpu_permille;      /* busy CPU time, 0..1000          */
    uint32_t mem_permille;      /* used memory, 0..1000            */
    double net_bytes_per_s;     /* rx+tx across non-loopback ifaces */
    double disk_sectors_per_s;  /* read+written sectors             */
} daft_metrics_sample_t;

daft_status_t daft_metrics_init(daft_metrics_mode_t mode,
                                const char *trace_path);

daft_status_t daft_metrics_sample(uint64_t now_ms,
                                  daft_metrics_sample_t *out);

daft_status_t daft_metrics_close(void);

#endif /* DAFT_METRICS_H */
