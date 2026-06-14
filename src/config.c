#include <stddef.h>

#include "config.h"
#include "util.h"

#define DAFT_CONFIG_DEFAULT_SEED 0xDA47C0DE5EEDULL
#define DAFT_CONFIG_DEFAULT_SIM_MINUTES 480u

/* Bounded string equality: strings longer than limit never match. */
static int daft_str_eq(const char *a, const char *b, size_t limit)
{
    size_t i;

    if ((a == NULL) || (b == NULL)) {
        return 0;
    }
    /* Bounded by limit. */
    for (i = 0u; i < limit; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
        if (a[i] == '\0') {
            return 1;
        }
    }
    return 0;
}

/* Parse a full NUL-terminated decimal string (no trailing junk). */
static daft_status_t daft_parse_u64_str(const char *s, uint64_t *out)
{
    size_t pos = 0u;
    size_t len = 0u;
    daft_status_t status;

    if (s == NULL) {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }
    /* Bounded length scan. */
    while ((len < 21u) && (s[len] != '\0')) {
        len++;
    }
    status = daft_util_parse_u64(s, len, &pos, out);
    if ((status == DAFT_STATUS_OK) && (pos != len)) {
        status = DAFT_STATUS_FORMAT_ERROR;
    }
    return status;
}

daft_status_t daft_config_default(daft_config_t *cfg)
{
    daft_status_t status;

    if (cfg == NULL) {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }

    status = daft_util_str_copy(cfg->midi_path, sizeof(cfg->midi_path),
                                "/dev/midi", 16u);
    if (status == DAFT_STATUS_OK) {
        cfg->trace_path[0] = '\0';
        cfg->seed = DAFT_CONFIG_DEFAULT_SEED;
        cfg->root_pc = 5u;          /* F */
        cfg->mood_dark = 0u;        /* bright */
        cfg->density_percent = 100u;
        cfg->sim_minutes = 0u;      /* live */
    }
    return status;
}

static daft_status_t daft_config_set_mood(daft_config_t *cfg,
                                          const char *value)
{
    daft_status_t status = DAFT_STATUS_OK;

    if (daft_str_eq(value, "bright", 8u) == 1) {
        cfg->mood_dark = 0u;
    } else if (daft_str_eq(value, "dark", 8u) == 1) {
        cfg->mood_dark = 1u;
    } else {
        status = DAFT_STATUS_FORMAT_ERROR;
    }
    return status;
}

static daft_status_t daft_config_set_root(daft_config_t *cfg,
                                          const char *value)
{
    uint64_t v = 0u;
    daft_status_t status = daft_parse_u64_str(value, &v);

    if (status == DAFT_STATUS_OK) {
        if (v > 11u) {
            status = DAFT_STATUS_OUT_OF_RANGE;
        } else {
            cfg->root_pc = (uint32_t)v;
        }
    }
    return status;
}

static daft_status_t daft_config_set_density(daft_config_t *cfg,
                                             const char *value)
{
    uint64_t v = 0u;
    daft_status_t status = daft_parse_u64_str(value, &v);

    if (status == DAFT_STATUS_OK) {
        if ((v < 25u) || (v > 400u)) {
            status = DAFT_STATUS_OUT_OF_RANGE;
        } else {
            cfg->density_percent = (uint32_t)v;
        }
    }
    return status;
}

static daft_status_t daft_config_set_seed(daft_config_t *cfg,
                                          const char *value)
{
    uint64_t v = 0u;
    daft_status_t status = daft_parse_u64_str(value, &v);

    if (status == DAFT_STATUS_OK) {
        cfg->seed = v;
    }
    return status;
}

daft_status_t daft_config_parse_args(daft_config_t *cfg, int argc,
                                     char **argv)
{
    int i;
    daft_status_t status = DAFT_STATUS_OK;
    int simulate_seen = 0;

    if ((cfg == NULL) || (argv == NULL) || (argc < 0)) {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }

    /* Bounded by argc. Every option takes exactly one value. */
    for (i = 1; (i < argc) && (status == DAFT_STATUS_OK); i += 2) {
        const char *opt = argv[i];
        const char *val = ((i + 1) < argc) ? argv[i + 1] : NULL;

        if (val == NULL) {
            status = DAFT_STATUS_FORMAT_ERROR;
        } else if (daft_str_eq(opt, "--midi-out", 16u) == 1) {
            status = daft_util_str_copy(cfg->midi_path,
                                        sizeof(cfg->midi_path), val,
                                        DAFT_CONFIG_PATH_MAX);
        } else if (daft_str_eq(opt, "--seed", 16u) == 1) {
            status = daft_config_set_seed(cfg, val);
        } else if (daft_str_eq(opt, "--root", 16u) == 1) {
            status = daft_config_set_root(cfg, val);
        } else if (daft_str_eq(opt, "--mood", 16u) == 1) {
            status = daft_config_set_mood(cfg, val);
        } else if (daft_str_eq(opt, "--density", 16u) == 1) {
            status = daft_config_set_density(cfg, val);
        } else if (daft_str_eq(opt, "--simulate", 16u) == 1) {
            status = daft_util_str_copy(cfg->trace_path,
                                        sizeof(cfg->trace_path), val,
                                        DAFT_CONFIG_PATH_MAX);
            simulate_seen = 1;
        } else if (daft_str_eq(opt, "--sim-minutes", 16u) == 1) {
            uint64_t v = 0u;
            status = daft_parse_u64_str(val, &v);
            if (status == DAFT_STATUS_OK) {
                if ((v == 0u) || (v > 6000u)) {
                    status = DAFT_STATUS_OUT_OF_RANGE;
                } else {
                    cfg->sim_minutes = (uint32_t)v;
                }
            }
        } else {
            status = DAFT_STATUS_FORMAT_ERROR;
        }
    }

    if ((status == DAFT_STATUS_OK) && (simulate_seen == 1) &&
        (cfg->sim_minutes == 0u)) {
        cfg->sim_minutes = DAFT_CONFIG_DEFAULT_SIM_MINUTES;
    }
    return status;
}
