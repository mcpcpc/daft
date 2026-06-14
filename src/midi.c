#include <fcntl.h>
#include <stddef.h>
#include <sys/stat.h>
#include <unistd.h>

#include "midi.h"
#include "util.h"

#define DAFT_MIDI_MAX_DELTA 0x0FFFFFFFu
#define DAFT_MIDI_SMF_LEN_OFFSET 18

/* Variable-length quantity, most significant 7-bit group first. */
static size_t daft_midi_vlq(uint32_t value, uint8_t out[4])
{
    size_t n = 0u;
    uint32_t v = (value > DAFT_MIDI_MAX_DELTA) ? DAFT_MIDI_MAX_DELTA : value;
    uint8_t groups[4];
    size_t count = 0u;

    /* Bounded: at most 4 seven-bit groups for a 28-bit value. */
    do {
        groups[count] = (uint8_t)(v & 0x7Fu);
        v >>= 7;
        count++;
    } while ((v > 0u) && (count < 4u));

    while (count > 0u) {
        count--;
        out[n] = (count > 0u) ? (uint8_t)(groups[count] | 0x80u)
                              : groups[count];
        n++;
    }
    return n;
}

static daft_status_t daft_midi_emit(daft_midi_t *midi, uint64_t t_ms,
                                    const uint8_t *bytes, size_t n)
{
    daft_status_t status;

    if (midi->sink == DAFT_MIDI_SINK_SMF) {
        uint8_t delta_buf[4];
        uint64_t delta64 = (t_ms > midi->last_ms) ? (t_ms - midi->last_ms)
                                                  : 0u;
        uint32_t delta = (delta64 > (uint64_t)DAFT_MIDI_MAX_DELTA)
                             ? DAFT_MIDI_MAX_DELTA
                             : (uint32_t)delta64;
        size_t delta_len = daft_midi_vlq(delta, delta_buf);

        status = daft_util_write_all(midi->fd, delta_buf, delta_len);
        if (status == DAFT_STATUS_OK) {
            status = daft_util_write_all(midi->fd, bytes, n);
        }
        if (status == DAFT_STATUS_OK) {
            midi->track_len += (uint32_t)delta_len + (uint32_t)n;
            midi->last_ms = (midi->last_ms > t_ms) ? midi->last_ms : t_ms;
        }
    } else {
        status = daft_util_write_all(midi->fd, bytes, n);
    }
    return status;
}

daft_status_t daft_midi_open(daft_midi_t *midi, daft_midi_sink_t sink,
                             const char *path)
{
    /* MThd: format 0, one track, division 500 ticks per quarter note. */
    static const uint8_t k_smf_header[14] = {
        0x4Du, 0x54u, 0x68u, 0x64u, 0x00u, 0x00u, 0x00u, 0x06u,
        0x00u, 0x00u, 0x00u, 0x01u, 0x01u, 0xF4u
    };
    /* MTrk chunk header, zero length placeholder (patched on close). */
    static const uint8_t k_smf_track_header[8] = {
        0x4Du, 0x54u, 0x72u, 0x6Bu, 0x00u, 0x00u, 0x00u, 0x00u
    };
    /* Set tempo meta event, 500000 us per quarter, at delta 0. */
    static const uint8_t k_smf_tempo[7] = {
        0x00u, 0xFFu, 0x51u, 0x03u, 0x07u, 0xA1u, 0x20u
    };
    daft_status_t status = DAFT_STATUS_OK;

    if ((midi == NULL) || (path == NULL) ||
        ((sink != DAFT_MIDI_SINK_STREAM) && (sink != DAFT_MIDI_SINK_SMF))) {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }

    midi->sink = sink;
    midi->last_ms = 0u;
    midi->track_len = 0u;

    if (sink == DAFT_MIDI_SINK_SMF) {
        midi->fd = open(path, O_WRONLY | O_CREAT | O_TRUNC,
                        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    } else {
        midi->fd = open(path, O_WRONLY);
    }
    if (midi->fd < 0) {
        return DAFT_STATUS_IO_ERROR;
    }

    if (sink == DAFT_MIDI_SINK_SMF) {
        status = daft_util_write_all(midi->fd, k_smf_header,
                                     sizeof(k_smf_header));
        if (status == DAFT_STATUS_OK) {
            status = daft_util_write_all(midi->fd, k_smf_track_header,
                                         sizeof(k_smf_track_header));
        }
        if (status == DAFT_STATUS_OK) {
            status = daft_util_write_all(midi->fd, k_smf_tempo,
                                         sizeof(k_smf_tempo));
        }
        if (status == DAFT_STATUS_OK) {
            midi->track_len = (uint32_t)sizeof(k_smf_tempo);
        } else {
            (void)close(midi->fd);
            midi->fd = -1;
        }
    }
    return status;
}

static daft_status_t daft_midi_msg3(daft_midi_t *midi, uint64_t t_ms,
                                    uint8_t hi, uint8_t ch, uint8_t d1,
                                    uint8_t d2)
{
    uint8_t msg[3];

    if ((midi == NULL) || (midi->fd < 0) || (ch > 15u) || (d1 > 127u) ||
        (d2 > 127u)) {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }
    msg[0] = (uint8_t)(hi | ch);
    msg[1] = d1;
    msg[2] = d2;
    return daft_midi_emit(midi, t_ms, msg, 3u);
}

daft_status_t daft_midi_note_on(daft_midi_t *midi, uint64_t t_ms, uint8_t ch,
                                uint8_t note, uint8_t velocity)
{
    return daft_midi_msg3(midi, t_ms, 0x90u, ch, note, velocity);
}

daft_status_t daft_midi_note_off(daft_midi_t *midi, uint64_t t_ms, uint8_t ch,
                                 uint8_t note)
{
    return daft_midi_msg3(midi, t_ms, 0x80u, ch, note, 64u);
}

daft_status_t daft_midi_control(daft_midi_t *midi, uint64_t t_ms, uint8_t ch,
                                uint8_t controller, uint8_t value)
{
    return daft_midi_msg3(midi, t_ms, 0xB0u, ch, controller, value);
}

daft_status_t daft_midi_program(daft_midi_t *midi, uint64_t t_ms, uint8_t ch,
                                uint8_t program)
{
    uint8_t msg[2];

    if ((midi == NULL) || (midi->fd < 0) || (ch > 15u) || (program > 127u)) {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }
    msg[0] = (uint8_t)(0xC0u | ch);
    msg[1] = program;
    return daft_midi_emit(midi, t_ms, msg, 2u);
}

daft_status_t daft_midi_close(daft_midi_t *midi, uint64_t t_ms)
{
    /* End-of-track meta event; the delta is prepended by daft_midi_emit. */
    static const uint8_t k_smf_end_of_track[3] = {
        0xFFu, 0x2Fu, 0x00u
    };
    daft_status_t status = DAFT_STATUS_OK;

    if ((midi == NULL) || (midi->fd < 0)) {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }

    if (midi->sink == DAFT_MIDI_SINK_SMF) {
        status = daft_midi_emit(midi, t_ms, k_smf_end_of_track,
                                sizeof(k_smf_end_of_track));
        if (status == DAFT_STATUS_OK) {
            uint8_t len_be[4];

            len_be[0] = (uint8_t)((midi->track_len >> 24) & 0xFFu);
            len_be[1] = (uint8_t)((midi->track_len >> 16) & 0xFFu);
            len_be[2] = (uint8_t)((midi->track_len >> 8) & 0xFFu);
            len_be[3] = (uint8_t)(midi->track_len & 0xFFu);

            /* cppcheck-suppress misra-c2012-17.3 ; lseek is declared in
             * <unistd.h> (POSIX); the analyzer does not expand system
             * headers. */
            if (lseek(midi->fd, (off_t)DAFT_MIDI_SMF_LEN_OFFSET, SEEK_SET) ==
                (off_t)-1) {
                status = DAFT_STATUS_IO_ERROR;
            } else {
                status = daft_util_write_all(midi->fd, len_be, 4u);
            }
        }
    }

    if (close(midi->fd) != 0) {
        status = DAFT_STATUS_IO_ERROR;
    }
    midi->fd = -1;
    return status;
}
