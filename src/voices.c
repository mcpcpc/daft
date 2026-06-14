#include <stddef.h>

#include "voices.h"

daft_status_t daft_voices_init(daft_voices_t *voices)
{
    size_t i;

    if (voices == NULL) {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }
    /* Bounded by DAFT_VOICES_CAP. */
    for (i = 0u; i < DAFT_VOICES_CAP; i++) {
        voices->slot[i].active = 0u;
    }
    return DAFT_STATUS_OK;
}

/* Release the oldest active voice, optionally restricted to one channel
 * (ch_filter > 15 means any channel). */
static daft_status_t daft_voices_release_oldest(daft_voices_t *voices,
                                                daft_midi_t *midi,
                                                uint64_t now,
                                                uint8_t ch_filter)
{
    size_t i;
    size_t oldest = DAFT_VOICES_CAP;
    daft_status_t status = DAFT_STATUS_OK;

    /* Bounded by DAFT_VOICES_CAP. */
    for (i = 0u; i < DAFT_VOICES_CAP; i++) {
        if ((voices->slot[i].active == 1u) &&
            ((ch_filter > 15u) || (voices->slot[i].ch == ch_filter))) {
            if ((oldest == DAFT_VOICES_CAP) ||
                (voices->slot[i].on_ms < voices->slot[oldest].on_ms)) {
                oldest = i;
            }
        }
    }

    if (oldest < DAFT_VOICES_CAP) {
        status = daft_midi_note_off(midi, now, voices->slot[oldest].ch,
                                    voices->slot[oldest].note);
        voices->slot[oldest].active = 0u;
    }
    return status;
}

daft_status_t daft_voices_note_on(daft_voices_t *voices, daft_midi_t *midi,
                                  uint64_t now, uint8_t ch, uint8_t note,
                                  uint8_t velocity, uint32_t dur_ms)
{
    size_t i;
    size_t free_slot = DAFT_VOICES_CAP;
    size_t ch_count = 0u;
    size_t total = 0u;
    daft_status_t status = DAFT_STATUS_OK;

    if ((voices == NULL) || (midi == NULL) || (ch > 15u) || (note > 127u) ||
        (velocity > 127u)) {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }

    /* Retrigger of an already-sounding note: release it first. */
    for (i = 0u; i < DAFT_VOICES_CAP; i++) {
        if ((voices->slot[i].active == 1u) && (voices->slot[i].ch == ch) &&
            (voices->slot[i].note == note)) {
            status = daft_midi_note_off(midi, now, ch, note);
            voices->slot[i].active = 0u;
        }
    }
    if (status != DAFT_STATUS_OK) {
        return status;
    }

    for (i = 0u; i < DAFT_VOICES_CAP; i++) {
        if (voices->slot[i].active == 1u) {
            total++;
            if (voices->slot[i].ch == ch) {
                ch_count++;
            }
        } else if (free_slot == DAFT_VOICES_CAP) {
            free_slot = i;
        } else {
            /* counting only */
        }
    }

    if (ch_count >= DAFT_VOICES_PER_CHANNEL) {
        status = daft_voices_release_oldest(voices, midi, now, ch);
    }
    if ((status == DAFT_STATUS_OK) && (total >= DAFT_VOICES_GLOBAL)) {
        status = daft_voices_release_oldest(voices, midi, now, 16u);
    }
    if (status != DAFT_STATUS_OK) {
        return status;
    }

    if (free_slot == DAFT_VOICES_CAP) {
        /* Re-scan: a release above freed a slot. Bounded. */
        for (i = 0u; i < DAFT_VOICES_CAP; i++) {
            if (voices->slot[i].active == 0u) {
                free_slot = i;
                break;
            }
        }
    }
    if (free_slot == DAFT_VOICES_CAP) {
        return DAFT_STATUS_INTERNAL_ERROR;
    }

    status = daft_midi_note_on(midi, now, ch, note, velocity);
    if (status == DAFT_STATUS_OK) {
        voices->slot[free_slot].on_ms = now;
        voices->slot[free_slot].off_ms = now + (uint64_t)dur_ms;
        voices->slot[free_slot].ch = ch;
        voices->slot[free_slot].note = note;
        voices->slot[free_slot].active = 1u;
    }
    return status;
}

daft_status_t daft_voices_tick(daft_voices_t *voices, daft_midi_t *midi,
                               uint64_t now)
{
    size_t i;
    daft_status_t status = DAFT_STATUS_OK;

    if ((voices == NULL) || (midi == NULL)) {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }

    /* Bounded by DAFT_VOICES_CAP. */
    for (i = 0u; i < DAFT_VOICES_CAP; i++) {
        if ((voices->slot[i].active == 1u) &&
            (voices->slot[i].off_ms <= now)) {
            daft_status_t s = daft_midi_note_off(midi, now,
                                                 voices->slot[i].ch,
                                                 voices->slot[i].note);
            voices->slot[i].active = 0u;
            if (s != DAFT_STATUS_OK) {
                status = s;
            }
        }
    }
    return status;
}

daft_status_t daft_voices_all_off(daft_voices_t *voices, daft_midi_t *midi,
                                  uint64_t now)
{
    size_t i;
    daft_status_t status = DAFT_STATUS_OK;

    if ((voices == NULL) || (midi == NULL)) {
        return DAFT_STATUS_INVALID_ARGUMENT;
    }

    /* Bounded by DAFT_VOICES_CAP. */
    for (i = 0u; i < DAFT_VOICES_CAP; i++) {
        if (voices->slot[i].active == 1u) {
            daft_status_t s = daft_midi_note_off(midi, now,
                                                 voices->slot[i].ch,
                                                 voices->slot[i].note);
            voices->slot[i].active = 0u;
            if (s != DAFT_STATUS_OK) {
                status = s;
            }
        }
    }
    return status;
}
