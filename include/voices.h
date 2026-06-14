#ifndef DAFT_VOICES_H
#define DAFT_VOICES_H

#include <stdint.h>

#include "midi.h"
#include "status.h"

/*
 * Active-note table. Enforces polyphony caps by releasing the oldest
 * sounding note early (graceful note-off, never a hard steal) and
 * guarantees every note-on is paired with a note-off.
 */
#define DAFT_VOICES_CAP 16u
#define DAFT_VOICES_PER_CHANNEL 4u
#define DAFT_VOICES_GLOBAL 12u

typedef struct
{
    uint64_t on_ms;
    uint64_t off_ms;
    uint8_t ch;
    uint8_t note;
    uint8_t active;
} daft_voice_t;

typedef struct
{
    daft_voice_t slot[DAFT_VOICES_CAP];
} daft_voices_t;

daft_status_t daft_voices_init(daft_voices_t *voices);

/* Emit a note-on now and register its note-off at now + dur_ms. */
daft_status_t daft_voices_note_on(daft_voices_t *voices, daft_midi_t *midi,
                                  uint64_t now, uint8_t ch, uint8_t note,
                                  uint8_t velocity, uint32_t dur_ms);

/* Emit note-offs for every voice whose time has come. */
daft_status_t daft_voices_tick(daft_voices_t *voices, daft_midi_t *midi,
                               uint64_t now);

/* Emit note-offs for all active voices (shutdown path). */
daft_status_t daft_voices_all_off(daft_voices_t *voices, daft_midi_t *midi,
                                  uint64_t now);

#endif /* DAFT_VOICES_H */
