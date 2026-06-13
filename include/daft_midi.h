#ifndef DAFT_MIDI_H
#define DAFT_MIDI_H

#include <stdint.h>

#include "daft_status.h"

/*
 * MIDI output. Two sinks share one event API:
 *  - STREAM: raw MIDI bytes via POSIX write() to a character device or
 *    FIFO (default /dev/midi). Event time is ignored; the caller emits
 *    in real time. Note: opening a FIFO blocks until a reader connects.
 *  - SMF: Standard MIDI File (format 0). Division is 500 ticks/quarter and
 *    tempo is fixed at 500000 us/quarter, so one tick equals exactly one
 *    millisecond; event times map directly to deltas. Used by the
 *    deterministic simulation mode.
 */
typedef enum
{
    DAFT_MIDI_SINK_STREAM = 0,
    DAFT_MIDI_SINK_SMF = 1
} daft_midi_sink_t;

typedef struct
{
    int fd;
    daft_midi_sink_t sink;
    uint64_t last_ms;    /* SMF: time of the previous event */
    uint32_t track_len;  /* SMF: bytes written to the track chunk */
} daft_midi_t;

daft_status_t daft_midi_open(daft_midi_t *midi, daft_midi_sink_t sink,
                             const char *path);

daft_status_t daft_midi_note_on(daft_midi_t *midi, uint64_t t_ms, uint8_t ch,
                                uint8_t note, uint8_t velocity);

daft_status_t daft_midi_note_off(daft_midi_t *midi, uint64_t t_ms, uint8_t ch,
                                 uint8_t note);

daft_status_t daft_midi_control(daft_midi_t *midi, uint64_t t_ms, uint8_t ch,
                                uint8_t controller, uint8_t value);

daft_status_t daft_midi_program(daft_midi_t *midi, uint64_t t_ms, uint8_t ch,
                                uint8_t program);

/* Finalize (SMF: end-of-track and header patch) and close the sink. */
daft_status_t daft_midi_close(daft_midi_t *midi, uint64_t t_ms);

#endif /* DAFT_MIDI_H */
