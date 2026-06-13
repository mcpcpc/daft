#include <stddef.h>

#include "config.h"
#include "util.h"

#define DAFT_CONFIG_FILE_CAP 4096u
#define DAFT_CONFIG_KEY_MAX 32u
#define DAFT_CONFIG_DEFAULT_SEED 0xDA47C0DE5EEDULL
#define DAFT_CONFIG_DEFAULT_SIM_MINUTES 480u

/* Bounded string equality: strings longer than limit never match. */
static int daft_str_eq(const char *a, const char *b, size_t limit)
{
    size_t i;

    if ((a == NULL) || (b == NULL))
    {
        return 0;
    }
    /* Bounded by limit. */
    for (i = 0u; i < limit; i++)
    {
        if (a[i] != b[i])
        {
            return 0;
        }
        if (a[i] == '\0')
        {
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

    if (s == NULL)
    {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }
    /* Bounded length scan. */
    while ((len < 21u) && (s[len] != '\0'))
    {
        len++;
    }
    status = daft_util_parse_u64(s, len, &pos, out);
    if ((status == DAFT_STATUS_OK) && (pos != len))
    {
        status = DAFT_STATUS_FORMAT_ERROR;
    }
    return status;
}

daft_status_t daft_config_default(daft_config_t *cfg)
{
    daft_status_t status;

    if (cfg == NULL)
    {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }

    status = daft_util_str_copy(cfg->midi_path, sizeof(cfg->midi_path),
                                "/dev/midi", 16u);
    if (status == DAFT_STATUS_OK)
    {
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

    if (daft_str_eq(value, "bright", 8u) == 1)
    {
        cfg->mood_dark = 0u;
    }
    else if (daft_str_eq(value, "dark", 8u) == 1)
    {
        cfg->mood_dark = 1u;
    }
    else
    {
        status = DAFT_STATUS_FORMAT_ERROR;
    }
    return status;
}

static daft_status_t daft_config_set_root(daft_config_t *cfg,
                                          const char *value)
{
    uint64_t v = 0u;
    daft_status_t status = daft_parse_u64_str(value, &v);

    if (status == DAFT_STATUS_OK)
    {
        if (v > 11u)
        {
            status = DAFT_STATUS_OUT_OF_RANGE;
        }
        else
        {
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

    if (status == DAFT_STATUS_OK)
    {
        if ((v < 25u) || (v > 400u))
        {
            status = DAFT_STATUS_OUT_OF_RANGE;
        }
        else
        {
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

    if (status == DAFT_STATUS_OK)
    {
        cfg->seed = v;
    }
    return status;
}

static daft_status_t daft_config_apply_kv(daft_config_t *cfg,
                                          const char *key, const char *value)
{
    daft_status_t status;

    if (daft_str_eq(key, "midi_path", DAFT_CONFIG_KEY_MAX) == 1)
    {
        status = daft_util_str_copy(cfg->midi_path, sizeof(cfg->midi_path),
                                    value, DAFT_CONFIG_PATH_MAX);
    }
    else if (daft_str_eq(key, "seed", DAFT_CONFIG_KEY_MAX) == 1)
    {
        status = daft_config_set_seed(cfg, value);
    }
    else if (daft_str_eq(key, "root", DAFT_CONFIG_KEY_MAX) == 1)
    {
        status = daft_config_set_root(cfg, value);
    }
    else if (daft_str_eq(key, "mood", DAFT_CONFIG_KEY_MAX) == 1)
    {
        status = daft_config_set_mood(cfg, value);
    }
    else if (daft_str_eq(key, "density_percent", DAFT_CONFIG_KEY_MAX) == 1)
    {
        status = daft_config_set_density(cfg, value);
    }
    else
    {
        status = DAFT_STATUS_FORMAT_ERROR;
    }
    return status;
}

/* Extract one line [start, end) from buf as key=value and apply it.
 * Blank lines and '#' comments are ignored. */
static daft_status_t daft_config_apply_line(daft_config_t *cfg,
                                            const char *buf, size_t start,
                                            size_t end)
{
    char key[DAFT_CONFIG_KEY_MAX];
    char value[DAFT_CONFIG_PATH_MAX];
    size_t pos = start;
    size_t key_len = 0u;
    size_t value_len = 0u;
    daft_status_t status = daft_util_skip_ws(buf, end, &pos);

    if (status != DAFT_STATUS_OK)
    {
        return status;
    }
    if ((pos >= end) || (buf[pos] == '#'))
    {
        return DAFT_STATUS_OK;
    }

    /* Key: up to '='. Bounded by key buffer. */
    while ((pos < end) && (buf[pos] != '=') && (buf[pos] != ' ') &&
           (buf[pos] != '\t'))
    {
        if (key_len >= (DAFT_CONFIG_KEY_MAX - 1u))
        {
            return DAFT_STATUS_FORMAT_ERROR;
        }
        key[key_len] = buf[pos];
        key_len++;
        pos++;
    }
    key[key_len] = '\0';

    status = daft_util_skip_ws(buf, end, &pos);
    if ((status != DAFT_STATUS_OK) || (pos >= end) || (buf[pos] != '='))
    {
        return DAFT_STATUS_FORMAT_ERROR;
    }
    pos++;
    status = daft_util_skip_ws(buf, end, &pos);
    if (status != DAFT_STATUS_OK)
    {
        return status;
    }

    /* Value: to end of line, trailing CR/space trimmed. Bounded by buffer. */
    while (pos < end)
    {
        if (value_len >= (DAFT_CONFIG_PATH_MAX - 1u))
        {
            return DAFT_STATUS_FORMAT_ERROR;
        }
        value[value_len] = buf[pos];
        value_len++;
        pos++;
    }
    while ((value_len > 0u) && ((value[value_len - 1u] == '\r') ||
                                (value[value_len - 1u] == ' ') ||
                                (value[value_len - 1u] == '\t')))
    {
        value_len--;
    }
    value[value_len] = '\0';

    return daft_config_apply_kv(cfg, key, value);
}

/* Load key=value pairs from a config file (bounded size). Unknown keys
 * are rejected. */
static daft_status_t daft_config_load_file(daft_config_t *cfg,
                                           const char *path)
{
    char buf[DAFT_CONFIG_FILE_CAP];
    size_t len = 0u;
    size_t line_start = 0u;
    size_t i;
    daft_status_t status;

    if ((cfg == NULL) || (path == NULL))
    {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }

    status = daft_util_read_file(path, buf, sizeof(buf), &len);
    if (status != DAFT_STATUS_OK)
    {
        return status;
    }

    /* Bounded by buffer length. */
    for (i = 0u; i <= len; i++)
    {
        if ((i == len) || (buf[i] == '\n'))
        {
            status = daft_config_apply_line(cfg, buf, line_start, i);
            if (status != DAFT_STATUS_OK)
            {
                return status;
            }
            line_start = i + 1u;
        }
    }
    return DAFT_STATUS_OK;
}

daft_status_t daft_config_parse_args(daft_config_t *cfg, int argc,
                                     char **argv)
{
    int i;
    daft_status_t status = DAFT_STATUS_OK;
    int simulate_seen = 0;

    if ((cfg == NULL) || (argv == NULL) || (argc < 0))
    {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }

    /* Bounded by argc. Every option takes exactly one value. */
    for (i = 1; (i < argc) && (status == DAFT_STATUS_OK); i += 2)
    {
        const char *opt = argv[i];
        const char *val = ((i + 1) < argc) ? argv[i + 1] : NULL;

        if (val == NULL)
        {
            status = DAFT_STATUS_FORMAT_ERROR;
        }
        else if (daft_str_eq(opt, "--config", 16u) == 1)
        {
            status = daft_config_load_file(cfg, val);
        }
        else if (daft_str_eq(opt, "--midi-out", 16u) == 1)
        {
            status = daft_util_str_copy(cfg->midi_path,
                                        sizeof(cfg->midi_path), val,
                                        DAFT_CONFIG_PATH_MAX);
        }
        else if (daft_str_eq(opt, "--seed", 16u) == 1)
        {
            status = daft_config_set_seed(cfg, val);
        }
        else if (daft_str_eq(opt, "--root", 16u) == 1)
        {
            status = daft_config_set_root(cfg, val);
        }
        else if (daft_str_eq(opt, "--mood", 16u) == 1)
        {
            status = daft_config_set_mood(cfg, val);
        }
        else if (daft_str_eq(opt, "--density", 16u) == 1)
        {
            status = daft_config_set_density(cfg, val);
        }
        else if (daft_str_eq(opt, "--simulate", 16u) == 1)
        {
            status = daft_util_str_copy(cfg->trace_path,
                                        sizeof(cfg->trace_path), val,
                                        DAFT_CONFIG_PATH_MAX);
            simulate_seen = 1;
        }
        else if (daft_str_eq(opt, "--sim-minutes", 16u) == 1)
        {
            uint64_t v = 0u;
            status = daft_parse_u64_str(val, &v);
            if (status == DAFT_STATUS_OK)
            {
                if ((v == 0u) || (v > 6000u))
                {
                    status = DAFT_STATUS_OUT_OF_RANGE;
                }
                else
                {
                    cfg->sim_minutes = (uint32_t)v;
                }
            }
        }
        else
        {
            status = DAFT_STATUS_FORMAT_ERROR;
        }
    }

    if ((status == DAFT_STATUS_OK) && (simulate_seen == 1) &&
        (cfg->sim_minutes == 0u))
    {
        cfg->sim_minutes = DAFT_CONFIG_DEFAULT_SIM_MINUTES;
    }
    return status;
}
