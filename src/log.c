#include <stddef.h>
#include <unistd.h>

#include "log.h"
#include "util.h"

#define DAFT_LOG_FD 2 /* standard error */

typedef struct
{
    const char *text;
    size_t len;
} daft_log_msg_t;

/* All messages are fixed strings with fixed IDs (AGENTS.md logging rules). */
static daft_status_t daft_log_lookup(daft_log_id_t id, daft_log_msg_t *out)
{
    daft_status_t status = DAFT_STATUS_OK;

    switch (id)
    {
        case DAFT_LOG_ID_STARTUP:
            out->text = "daft: started";
            break;
        case DAFT_LOG_ID_SHUTDOWN:
            out->text = "daft: shutdown complete";
            break;
        case DAFT_LOG_ID_SIGNAL_STOP:
            out->text = "daft: stop signal received";
            break;
        case DAFT_LOG_ID_USAGE:
            out->text =
                "usage: daft [--config FILE] [--midi-out PATH] [--seed N]\n"
                "            [--root 0..11] [--mood bright|dark]\n"
                "            [--density PERCENT] [--simulate TRACE]\n"
                "            [--sim-minutes N]";
            break;
        case DAFT_LOG_ID_CONFIG_BAD_ARG:
            out->text = "daft: invalid configuration argument";
            break;
        case DAFT_LOG_ID_CONFIG_FILE_ERROR:
            out->text = "daft: configuration file error";
            break;
        case DAFT_LOG_ID_MIDI_OPEN_FAILED:
            out->text = "daft: failed to open MIDI output";
            break;
        case DAFT_LOG_ID_MIDI_WRITE_FAILED:
            out->text = "daft: MIDI write failed";
            break;
        case DAFT_LOG_ID_METRICS_INIT_FAILED:
            out->text = "daft: metrics initialization failed";
            break;
        case DAFT_LOG_ID_METRICS_SAMPLE_FAILED:
            out->text = "daft: metrics sample failed";
            break;
        case DAFT_LOG_ID_TRACE_ERROR:
            out->text = "daft: trace file error";
            break;
        case DAFT_LOG_ID_SCHED_FULL:
            out->text = "daft: event queue full, note dropped";
            break;
        case DAFT_LOG_ID_ALL_NOTES_OFF:
            out->text = "daft: all notes off";
            break;
        case DAFT_LOG_ID_SIM_COMPLETE:
            out->text = "daft: simulation complete";
            break;
        case DAFT_LOG_ID_INTERNAL_FAULT:
            out->text = "daft: internal fault";
            break;
        default:
            status = DAFT_STATUS_INVALID_ARGUMENT;
            break;
    }

    if (status == DAFT_STATUS_OK)
    {
        size_t n = 0u;
        /* Bounded: longest fixed message is well under 256 bytes. */
        while ((n < 255u) && (out->text[n] != '\0'))
        {
            n++;
        }
        out->len = n;
    }
    return status;
}

static daft_status_t daft_log_emit(const daft_log_msg_t *msg,
                                   const char *suffix, size_t suffix_len)
{
    uint8_t line[300];
    size_t pos = 0u;
    size_t i;

    for (i = 0u; (i < msg->len) && (pos < 280u); i++)
    {
        line[pos] = (uint8_t)msg->text[i];
        pos++;
    }
    for (i = 0u; (i < suffix_len) && (pos < 298u); i++)
    {
        line[pos] = (uint8_t)suffix[i];
        pos++;
    }
    line[pos] = (uint8_t)'\n';
    pos++;

    return daft_util_write_all(DAFT_LOG_FD, line, pos);
}

daft_status_t daft_log_write(daft_log_id_t id)
{
    daft_log_msg_t msg = { NULL, 0u };
    daft_status_t status = daft_log_lookup(id, &msg);

    if (status == DAFT_STATUS_OK)
    {
        status = daft_log_emit(&msg, "", 0u);
    }
    return status;
}

daft_status_t daft_log_write_u32(daft_log_id_t id, uint32_t value)
{
    daft_log_msg_t msg = { NULL, 0u };
    char digits[12];
    size_t digit_len = 0u;
    daft_status_t status = daft_log_lookup(id, &msg);

    if (status == DAFT_STATUS_OK)
    {
        status = daft_util_u32_to_dec(digits, sizeof(digits), value,
                                      &digit_len);
    }
    if (status == DAFT_STATUS_OK)
    {
        char suffix[14];
        size_t i;

        suffix[0] = ' ';
        for (i = 0u; i < digit_len; i++)
        {
            suffix[i + 1u] = digits[i];
        }
        status = daft_log_emit(&msg, suffix, digit_len + 1u);
    }
    return status;
}
