#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

#include "metrics.h"
#include "util.h"

#define DAFT_METRICS_FILE_CAP 16384u
#define DAFT_METRICS_CPU_FIELDS 10u
#define DAFT_TRACE_CARRY_CAP 768u
#define DAFT_TRACE_LINE_CAP 256u
#define DAFT_TRACE_MAX_LINES_PER_CALL 120u
#define DAFT_TRACE_MAX_FILL_READS 8u

typedef struct
{
    uint64_t cpu_busy;
    uint64_t cpu_total;
    uint64_t net_bytes;
    uint64_t disk_sectors;
    uint64_t t_ms;
    int valid;
} daft_proc_prev_t;

typedef struct
{
    int fd;
    int eof;
    char carry[DAFT_TRACE_CARRY_CAP];
    size_t carry_len;
    int have_next;
    uint64_t next_t_ms;
    daft_metrics_sample_t next_vals;
    daft_metrics_sample_t cur;
} daft_trace_state_t;

typedef struct
{
    int initialized;
    daft_metrics_mode_t mode;
    daft_proc_prev_t prev;
    daft_trace_state_t trace;
} daft_metrics_state_t;

static daft_metrics_state_t g_metrics;

/* ------------------------------------------------------------------ */
/* Linux /proc backend                                                  */
/* ------------------------------------------------------------------ */

/* Returns 1 and advances *pos when buf[*pos..] starts with lit. */
static int daft_match_prefix(const char *buf, size_t len, size_t *pos,
                             const char *lit)
{
    size_t i = 0u;
    size_t p = *pos;

    /* Bounded by literal length (all literals are short and fixed). */
    while (lit[i] != '\0')
    {
        if ((p >= len) || (buf[p] != lit[i]))
        {
            return 0;
        }
        p++;
        i++;
    }
    *pos = p;
    return 1;
}

static daft_status_t daft_proc_parse_cpu(const char *buf, size_t len,
                                         uint64_t *busy, uint64_t *total)
{
    size_t pos = 0u;
    uint64_t field[DAFT_METRICS_CPU_FIELDS];
    size_t nf = 0u;
    uint64_t sum = 0u;
    uint64_t idle;
    size_t i;

    if (daft_match_prefix(buf, len, &pos, "cpu") != 1)
    {
        return DAFT_STATUS_FORMAT_ERROR;
    }

    /* Bounded by DAFT_METRICS_CPU_FIELDS. */
    while (nf < DAFT_METRICS_CPU_FIELDS)
    {
        daft_status_t status = daft_util_skip_ws(buf, len, &pos);
        if (status != DAFT_STATUS_OK)
        {
            return status;
        }
        if ((pos >= len) || (buf[pos] < '0') || (buf[pos] > '9'))
        {
            break;
        }
        status = daft_util_parse_u64(buf, len, &pos, &field[nf]);
        if (status != DAFT_STATUS_OK)
        {
            return status;
        }
        nf++;
    }

    if (nf < 4u)
    {
        return DAFT_STATUS_FORMAT_ERROR;
    }

    for (i = 0u; i < nf; i++)
    {
        sum += field[i];
    }
    idle = field[3] + ((nf > 4u) ? field[4] : 0u);
    *total = sum;
    *busy = sum - idle;
    return DAFT_STATUS_OK;
}

/* Parse "Key: value kB" style lines from /proc/meminfo. */
static daft_status_t daft_proc_parse_mem(const char *buf, size_t len,
                                         uint32_t *permille)
{
    size_t pos = 0u;
    uint64_t total = 0u;
    uint64_t avail = 0u;
    uint64_t mem_free = 0u;
    int have_avail = 0;

    /* Bounded by buffer length: each iteration advances pos to the next
     * line. */
    while (pos < len)
    {
        size_t line_pos = pos;
        uint64_t value = 0u;
        uint64_t *target = NULL;

        if (daft_match_prefix(buf, len, &line_pos, "MemTotal:") == 1)
        {
            target = &total;
        }
        else if (daft_match_prefix(buf, len, &line_pos, "MemAvailable:") == 1)
        {
            target = &avail;
            have_avail = 1;
        }
        else if (daft_match_prefix(buf, len, &line_pos, "MemFree:") == 1)
        {
            target = &mem_free;
        }
        else
        {
            /* Not a line of interest. */
        }

        if (target != NULL)
        {
            daft_status_t status = daft_util_skip_ws(buf, len, &line_pos);
            if (status == DAFT_STATUS_OK)
            {
                status = daft_util_parse_u64(buf, len, &line_pos, &value);
            }
            if (status == DAFT_STATUS_OK)
            {
                *target = value;
            }
        }

        /* Advance to the byte after the next newline. Bounded by len. */
        while ((pos < len) && (buf[pos] != '\n'))
        {
            pos++;
        }
        pos++;
    }

    if (total == 0u)
    {
        return DAFT_STATUS_FORMAT_ERROR;
    }
    {
        uint64_t not_used = (have_avail == 1) ? avail : mem_free;
        uint64_t used = (total > not_used) ? (total - not_used) : 0u;
        *permille = (uint32_t)((used * 1000u) / total);
    }
    return DAFT_STATUS_OK;
}

/* Sum rx+tx byte counters over all non-loopback interfaces. */
static daft_status_t daft_proc_parse_net(const char *buf, size_t len,
                                         uint64_t *bytes)
{
    size_t pos = 0u;
    uint64_t sum = 0u;

    /* Bounded by buffer length: each iteration consumes one line. */
    while (pos < len)
    {
        size_t name_start;
        size_t name_len = 0u;
        int is_lo;
        int ok = 1;

        if (daft_util_skip_ws(buf, len, &pos) != DAFT_STATUS_OK)
        {
            return DAFT_STATUS_INTERNAL_ERROR;
        }
        name_start = pos;
        /* Interface name runs to ':'. Bounded by len. */
        while ((pos < len) && (buf[pos] != ':') && (buf[pos] != '\n'))
        {
            pos++;
        }
        if ((pos >= len) || (buf[pos] != ':'))
        {
            ok = 0; /* header or malformed line */
        }
        else
        {
            name_len = pos - name_start;
            pos++;
        }

        is_lo = ((name_len == 2u) && (buf[name_start] == 'l') &&
                 (buf[name_start + 1u] == 'o'))
                    ? 1
                    : 0;

        if ((ok == 1) && (is_lo == 0))
        {
            uint64_t field[9];
            size_t nf = 0u;
            int fields_ok = 1;

            /* rx bytes is field 0, tx bytes is field 8. Bounded. */
            while ((nf < 9u) && (fields_ok == 1))
            {
                daft_status_t status = daft_util_skip_ws(buf, len, &pos);
                if (status != DAFT_STATUS_OK)
                {
                    return DAFT_STATUS_INTERNAL_ERROR;
                }
                if ((pos >= len) || (buf[pos] < '0') || (buf[pos] > '9'))
                {
                    fields_ok = 0;
                }
                else if (daft_util_parse_u64(buf, len, &pos, &field[nf]) !=
                         DAFT_STATUS_OK)
                {
                    fields_ok = 0;
                }
                else
                {
                    nf++;
                }
            }
            if (nf == 9u)
            {
                sum += field[0] + field[8];
            }
        }

        /* Advance past the line end. Bounded by len. */
        while ((pos < len) && (buf[pos] != '\n'))
        {
            pos++;
        }
        pos++;
    }

    *bytes = sum;
    return DAFT_STATUS_OK;
}

/* Sum sectors read+written across all block devices. Partitions are
 * counted alongside their parents; adaptive normalization downstream makes
 * the absolute scale irrelevant, only consistency matters. */
static daft_status_t daft_proc_parse_disk(const char *buf, size_t len,
                                          uint64_t *sectors)
{
    size_t pos = 0u;
    uint64_t sum = 0u;

    /* Bounded by buffer length: each iteration consumes one line. */
    while (pos < len)
    {
        uint64_t field[7];
        size_t nf = 0u;
        uint64_t skip_val = 0u;
        int ok = 1;

        /* major, minor */
        if (daft_util_skip_ws(buf, len, &pos) != DAFT_STATUS_OK)
        {
            return DAFT_STATUS_INTERNAL_ERROR;
        }
        if (daft_util_parse_u64(buf, len, &pos, &skip_val) != DAFT_STATUS_OK)
        {
            ok = 0;
        }
        if (ok == 1)
        {
            if (daft_util_skip_ws(buf, len, &pos) != DAFT_STATUS_OK)
            {
                return DAFT_STATUS_INTERNAL_ERROR;
            }
            if (daft_util_parse_u64(buf, len, &pos, &skip_val) !=
                DAFT_STATUS_OK)
            {
                ok = 0;
            }
        }
        /* device name token */
        if (ok == 1)
        {
            if (daft_util_skip_ws(buf, len, &pos) != DAFT_STATUS_OK)
            {
                return DAFT_STATUS_INTERNAL_ERROR;
            }
            while ((pos < len) && (buf[pos] != ' ') && (buf[pos] != '\t') &&
                   (buf[pos] != '\n'))
            {
                pos++;
            }
        }
        /* reads, reads_merged, sectors_read, read_ms,
         * writes, writes_merged, sectors_written */
        while ((ok == 1) && (nf < 7u))
        {
            if (daft_util_skip_ws(buf, len, &pos) != DAFT_STATUS_OK)
            {
                return DAFT_STATUS_INTERNAL_ERROR;
            }
            if ((pos >= len) || (buf[pos] < '0') || (buf[pos] > '9'))
            {
                ok = 0;
            }
            else if (daft_util_parse_u64(buf, len, &pos, &field[nf]) !=
                     DAFT_STATUS_OK)
            {
                ok = 0;
            }
            else
            {
                nf++;
            }
        }
        if ((ok == 1) && (nf == 7u))
        {
            sum += field[2] + field[6];
        }

        while ((pos < len) && (buf[pos] != '\n'))
        {
            pos++;
        }
        pos++;
    }

    *sectors = sum;
    return DAFT_STATUS_OK;
}

static daft_status_t daft_proc_sample(uint64_t now_ms,
                                      daft_metrics_sample_t *out)
{
    static char buf[DAFT_METRICS_FILE_CAP];
    size_t len = 0u;
    uint64_t cpu_busy = 0u;
    uint64_t cpu_total = 0u;
    uint64_t net_bytes = 0u;
    uint64_t disk_sectors = 0u;
    uint32_t mem_permille = 0u;
    daft_status_t status;

    status = daft_util_read_file("/proc/stat", buf, sizeof(buf), &len);
    if (status == DAFT_STATUS_OK)
    {
        status = daft_proc_parse_cpu(buf, len, &cpu_busy, &cpu_total);
    }
    if (status == DAFT_STATUS_OK)
    {
        status = daft_util_read_file("/proc/meminfo", buf, sizeof(buf), &len);
    }
    if (status == DAFT_STATUS_OK)
    {
        status = daft_proc_parse_mem(buf, len, &mem_permille);
    }
    if (status == DAFT_STATUS_OK)
    {
        status = daft_util_read_file("/proc/net/dev", buf, sizeof(buf), &len);
    }
    if (status == DAFT_STATUS_OK)
    {
        status = daft_proc_parse_net(buf, len, &net_bytes);
    }
    if (status == DAFT_STATUS_OK)
    {
        status = daft_util_read_file("/proc/diskstats", buf, sizeof(buf),
                                     &len);
    }
    if (status == DAFT_STATUS_OK)
    {
        status = daft_proc_parse_disk(buf, len, &disk_sectors);
    }
    if (status != DAFT_STATUS_OK)
    {
        return status;
    }

    out->cpu_permille = 0u;
    out->mem_permille = mem_permille;
    out->net_bytes_per_s = 0.0;
    out->disk_sectors_per_s = 0.0;

    if ((g_metrics.prev.valid == 1) && (now_ms > g_metrics.prev.t_ms))
    {
        uint64_t dt_ms = now_ms - g_metrics.prev.t_ms;
        uint64_t d_total = (cpu_total > g_metrics.prev.cpu_total)
                               ? (cpu_total - g_metrics.prev.cpu_total)
                               : 0u;
        uint64_t d_busy = (cpu_busy > g_metrics.prev.cpu_busy)
                              ? (cpu_busy - g_metrics.prev.cpu_busy)
                              : 0u;
        uint64_t d_net = (net_bytes > g_metrics.prev.net_bytes)
                             ? (net_bytes - g_metrics.prev.net_bytes)
                             : 0u;
        uint64_t d_disk = (disk_sectors > g_metrics.prev.disk_sectors)
                              ? (disk_sectors - g_metrics.prev.disk_sectors)
                              : 0u;

        if (d_total > 0u)
        {
            uint64_t pm = (d_busy * 1000u) / d_total;
            out->cpu_permille = (pm > 1000u) ? 1000u : (uint32_t)pm;
        }
        out->net_bytes_per_s = ((double)d_net * 1000.0) / (double)dt_ms;
        out->disk_sectors_per_s = ((double)d_disk * 1000.0) / (double)dt_ms;
    }

    g_metrics.prev.cpu_busy = cpu_busy;
    g_metrics.prev.cpu_total = cpu_total;
    g_metrics.prev.net_bytes = net_bytes;
    g_metrics.prev.disk_sectors = disk_sectors;
    g_metrics.prev.t_ms = now_ms;
    g_metrics.prev.valid = 1;
    return DAFT_STATUS_OK;
}

/* ------------------------------------------------------------------ */
/* Trace backend (deterministic simulation)                             */
/* ------------------------------------------------------------------ */

/* Top up the carry buffer from the file. Bounded read attempts. */
static daft_status_t daft_trace_fill(daft_trace_state_t *tr)
{
    unsigned int attempts = 0u;

    while ((tr->eof == 0) && (tr->carry_len < DAFT_TRACE_CARRY_CAP) &&
           (attempts < DAFT_TRACE_MAX_FILL_READS))
    {
        ssize_t n = read(tr->fd, &tr->carry[tr->carry_len],
                         DAFT_TRACE_CARRY_CAP - tr->carry_len);
        if (n > 0)
        {
            tr->carry_len += (size_t)n;
        }
        else if (n == 0)
        {
            tr->eof = 1;
        }
        else
        {
            int err = errno;
            if (err != EINTR)
            {
                return DAFT_STATUS_IO_ERROR;
            }
        }
        attempts++;
    }
    return DAFT_STATUS_OK;
}

/* Extract the next complete line into line/cap. *got = 0 at end of data. */
static daft_status_t daft_trace_next_line(daft_trace_state_t *tr, char *line,
                                          size_t cap, size_t *out_len,
                                          int *got)
{
    size_t nl = DAFT_TRACE_CARRY_CAP;
    size_t i;
    size_t take;
    daft_status_t status = daft_trace_fill(tr);

    if (status != DAFT_STATUS_OK)
    {
        return status;
    }

    *got = 0;
    /* Bounded by carry capacity. */
    for (i = 0u; i < tr->carry_len; i++)
    {
        if (tr->carry[i] == '\n')
        {
            nl = i;
            break;
        }
    }

    if (nl == DAFT_TRACE_CARRY_CAP)
    {
        if ((tr->eof == 1) && (tr->carry_len > 0u))
        {
            nl = tr->carry_len; /* final unterminated line */
        }
        else if (tr->carry_len >= DAFT_TRACE_CARRY_CAP)
        {
            return DAFT_STATUS_FORMAT_ERROR; /* oversize line */
        }
        else
        {
            return DAFT_STATUS_OK; /* no more data */
        }
    }

    take = (nl < (cap - 1u)) ? nl : (cap - 1u);
    for (i = 0u; i < take; i++)
    {
        line[i] = tr->carry[i];
    }
    line[take] = '\0';
    *out_len = take;

    {
        size_t consumed = (nl < tr->carry_len) ? (nl + 1u) : tr->carry_len;
        size_t remain = tr->carry_len - consumed;
        if (remain > 0u)
        {
            (void)memmove(&tr->carry[0], &tr->carry[consumed], remain);
        }
        tr->carry_len = remain;
    }
    *got = 1;
    return DAFT_STATUS_OK;
}

/* Parse one CSV line. *ok = 0 for ignorable lines (headers/comments). */
static daft_status_t daft_trace_parse_line(const char *line, size_t len,
                                           uint64_t *t_ms,
                                           daft_metrics_sample_t *vals,
                                           int *ok)
{
    size_t pos = 0u;
    uint64_t raw[5];
    size_t i;

    *ok = 0;
    if (daft_util_skip_ws(line, len, &pos) != DAFT_STATUS_OK)
    {
        return DAFT_STATUS_INTERNAL_ERROR;
    }
    if ((pos >= len) || (line[pos] < '0') || (line[pos] > '9'))
    {
        return DAFT_STATUS_OK; /* ignorable */
    }

    /* Bounded: exactly 5 comma-separated fields. */
    for (i = 0u; i < 5u; i++)
    {
        daft_status_t status;

        if (i > 0u)
        {
            if ((pos >= len) || (line[pos] != ','))
            {
                return DAFT_STATUS_FORMAT_ERROR;
            }
            pos++;
        }
        if (daft_util_skip_ws(line, len, &pos) != DAFT_STATUS_OK)
        {
            return DAFT_STATUS_INTERNAL_ERROR;
        }
        status = daft_util_parse_u64(line, len, &pos, &raw[i]);
        if (status != DAFT_STATUS_OK)
        {
            return DAFT_STATUS_FORMAT_ERROR;
        }
        if (daft_util_skip_ws(line, len, &pos) != DAFT_STATUS_OK)
        {
            return DAFT_STATUS_INTERNAL_ERROR;
        }
    }

    *t_ms = raw[0] * 1000u;
    vals->cpu_permille = (raw[1] > 1000u) ? 1000u : (uint32_t)raw[1];
    vals->mem_permille = (raw[2] > 1000u) ? 1000u : (uint32_t)raw[2];
    vals->net_bytes_per_s = (double)raw[3];
    vals->disk_sectors_per_s = (double)raw[4];
    *ok = 1;
    return DAFT_STATUS_OK;
}

/* Load the next parseable line into next_t_ms/next_vals. */
static daft_status_t daft_trace_load_next(daft_trace_state_t *tr)
{
    char line[DAFT_TRACE_LINE_CAP];
    size_t line_len = 0u;
    unsigned int attempts = 0u;

    tr->have_next = 0;
    /* Bounded: skip at most a fixed number of ignorable lines per call. */
    while (attempts < DAFT_TRACE_MAX_LINES_PER_CALL)
    {
        int got = 0;
        int ok = 0;
        daft_status_t status = daft_trace_next_line(tr, line, sizeof(line),
                                                    &line_len, &got);
        if (status != DAFT_STATUS_OK)
        {
            return status;
        }
        if (got == 0)
        {
            return DAFT_STATUS_OK; /* end of trace: hold last values */
        }
        status = daft_trace_parse_line(line, line_len, &tr->next_t_ms,
                                       &tr->next_vals, &ok);
        if (status != DAFT_STATUS_OK)
        {
            return status;
        }
        if (ok == 1)
        {
            tr->have_next = 1;
            return DAFT_STATUS_OK;
        }
        attempts++;
    }
    return DAFT_STATUS_FORMAT_ERROR;
}

static daft_status_t daft_trace_sample(uint64_t now_ms,
                                       daft_metrics_sample_t *out)
{
    daft_trace_state_t *tr = &g_metrics.trace;
    unsigned int steps = 0u;

    /* Bounded: consume at most DAFT_TRACE_MAX_LINES_PER_CALL entries. */
    while ((tr->have_next == 1) && (tr->next_t_ms <= now_ms) &&
           (steps < DAFT_TRACE_MAX_LINES_PER_CALL))
    {
        daft_status_t status;

        tr->cur = tr->next_vals;
        status = daft_trace_load_next(tr);
        if (status != DAFT_STATUS_OK)
        {
            return status;
        }
        steps++;
    }

    *out = tr->cur;
    return DAFT_STATUS_OK;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

daft_status_t daft_metrics_init(daft_metrics_mode_t mode,
                                const char *trace_path)
{
    daft_status_t status = DAFT_STATUS_OK;

    if ((mode != DAFT_METRICS_PROC) && (mode != DAFT_METRICS_TRACE))
    {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }

    g_metrics.mode = mode;
    g_metrics.prev.valid = 0;
    g_metrics.trace.fd = -1;
    g_metrics.trace.eof = 0;
    g_metrics.trace.carry_len = 0u;
    g_metrics.trace.have_next = 0;
    g_metrics.trace.cur.cpu_permille = 0u;
    g_metrics.trace.cur.mem_permille = 0u;
    g_metrics.trace.cur.net_bytes_per_s = 0.0;
    g_metrics.trace.cur.disk_sectors_per_s = 0.0;

    if (mode == DAFT_METRICS_TRACE)
    {
        if (trace_path == NULL)
        {
            return DAFT_STATUS_INVALID_ARGUMENT;
        }
        g_metrics.trace.fd = open(trace_path, O_RDONLY);
        if (g_metrics.trace.fd < 0)
        {
            return DAFT_STATUS_IO_ERROR;
        }
        status = daft_trace_load_next(&g_metrics.trace);
        if (status != DAFT_STATUS_OK)
        {
            (void)close(g_metrics.trace.fd);
            g_metrics.trace.fd = -1;
            return status;
        }
    }

    g_metrics.initialized = 1;
    return status;
}

daft_status_t daft_metrics_sample(uint64_t now_ms,
                                  daft_metrics_sample_t *out)
{
    if (out == NULL)
    {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }
    if (g_metrics.initialized == 0)
    {
        return DAFT_STATUS_UNAVAILABLE;
    }

    return (g_metrics.mode == DAFT_METRICS_TRACE)
               ? daft_trace_sample(now_ms, out)
               : daft_proc_sample(now_ms, out);
}

daft_status_t daft_metrics_close(void)
{
    daft_status_t status = DAFT_STATUS_OK;

    if ((g_metrics.mode == DAFT_METRICS_TRACE) && (g_metrics.trace.fd >= 0))
    {
        if (close(g_metrics.trace.fd) != 0)
        {
            status = DAFT_STATUS_IO_ERROR;
        }
        g_metrics.trace.fd = -1;
    }
    g_metrics.initialized = 0;
    return status;
}
