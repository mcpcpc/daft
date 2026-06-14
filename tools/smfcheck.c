/*
 * smfcheck - non-production test tool (see tools/README.md).
 *
 * Audits a format-0 Standard MIDI File produced by daft's simulation mode:
 *   - every note-on is paired with a note-off; nothing left sounding
 *   - polyphony caps: 4 per channel, 12 global
 *   - note-on pitch classes belong to the pentatonic set on the root
 *   - velocity ceiling (ambient dies at forte)
 *   - onset density bounds per minute
 *
 * Usage: smfcheck FILE [root_pc]   (root_pc defaults to 5 = F)
 * Exit status: 0 = all checks passed, 1 = violations found or parse error.
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define SMF_BUF_CAP (4u * 1024u * 1024u)
#define MAX_MINUTES 1024u
#define MAX_POLY_CH 4u
#define MAX_POLY_GLOBAL 12u
#define MAX_VELOCITY 85u
#define MAX_ONSETS_PER_MIN 35u

static uint8_t g_buf[SMF_BUF_CAP];
static uint32_t g_minute_onsets[MAX_MINUTES];
static uint8_t g_active[16][128];

static const uint8_t k_degree[5] = { 0u, 2u, 4u, 7u, 9u };

static int pc_in_set(uint8_t note, uint32_t root_pc)
{
    uint32_t pc = (((uint32_t)note % 12u) + 12u - root_pc) % 12u;
    size_t i;

    for (i = 0u; i < 5u; i++) {
        if (pc == (uint32_t)k_degree[i]) {
            return 1;
        }
    }
    return 0;
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

/* Variable-length quantity; returns bytes consumed, 0 on error. */
static size_t read_vlq(const uint8_t *p, size_t avail, uint32_t *out)
{
    uint32_t v = 0u;
    size_t i;

    for (i = 0u; (i < 4u) && (i < avail); i++) {
        v = (v << 7) | ((uint32_t)p[i] & 0x7Fu);
        if ((p[i] & 0x80u) == 0u) {
            *out = v;
            return i + 1u;
        }
    }
    return 0u;
}

int main(int argc, char **argv)
{
    int fd;
    ssize_t n;
    size_t len = 0u;
    size_t pos;
    size_t track_end;
    uint32_t root_pc = 5u;
    uint64_t ticks = 0u;
    uint8_t running = 0u;
    int done = 0;

    unsigned long errors = 0u;
    unsigned long onsets = 0u;
    unsigned long offs = 0u;
    unsigned long cc_events = 0u;
    unsigned int poly_global = 0u;
    unsigned int poly_global_max = 0u;
    unsigned int poly_ch[16] = { 0u };
    unsigned int poly_ch_max[16] = { 0u };
    unsigned long onsets_ch[16] = { 0u };
    unsigned int vel_max = 0u;
    uint64_t last_tick = 0u;

    if ((argc < 2) || (argc > 3)) {
        (void)fprintf(stderr, "usage: smfcheck FILE [root_pc]\n");
        return 1;
    }
    if (argc == 3) {
        root_pc = (uint32_t)strtoul(argv[2], NULL, 10) % 12u;
    }

    fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        (void)fprintf(stderr, "smfcheck: cannot open %s\n", argv[1]);
        return 1;
    }
    do {
        n = read(fd, &g_buf[len], SMF_BUF_CAP - len);
        if (n > 0) {
            len += (size_t)n;
        }
    } while ((n > 0) && (len < SMF_BUF_CAP));
    (void)close(fd);

    if ((len < 22u) || (be32(&g_buf[0]) != 0x4D546864u) ||
        (be32(&g_buf[4]) != 6u) || (be16(&g_buf[8]) != 0u) ||
        (be16(&g_buf[10]) != 1u)) {
        (void)fprintf(stderr, "smfcheck: not a format-0 single-track SMF\n");
        return 1;
    }
    if (be16(&g_buf[12]) != 500u) {
        (void)fprintf(stderr, "smfcheck: unexpected division %u\n",
                      (unsigned int)be16(&g_buf[12]));
        return 1;
    }
    if (be32(&g_buf[14]) != 0x4D54726Bu) {
        (void)fprintf(stderr, "smfcheck: missing MTrk\n");
        return 1;
    }
    track_end = 22u + (size_t)be32(&g_buf[18]);
    if (track_end > len) {
        (void)fprintf(stderr, "smfcheck: truncated track\n");
        return 1;
    }

    pos = 22u;
    while ((pos < track_end) && (done == 0)) {
        uint32_t delta = 0u;
        size_t used = read_vlq(&g_buf[pos], track_end - pos, &delta);
        uint8_t status_byte;

        if (used == 0u) {
            (void)fprintf(stderr, "smfcheck: bad delta at %zu\n", pos);
            return 1;
        }
        pos += used;
        ticks += delta;

        if (pos >= track_end) {
            break;
        }
        status_byte = g_buf[pos];
        if (status_byte >= 0x80u) {
            pos++;
            if (status_byte < 0xF0u) {
                running = status_byte;
            }
        } else {
            status_byte = running;
            if (status_byte == 0u) {
                (void)fprintf(stderr, "smfcheck: data byte with no status\n");
                return 1;
            }
        }

        if (status_byte == 0xFFu) {
            uint32_t mlen = 0u;
            uint8_t mtype = g_buf[pos];

            pos++;
            used = read_vlq(&g_buf[pos], track_end - pos, &mlen);
            if (used == 0u) {
                (void)fprintf(stderr, "smfcheck: bad meta length\n");
                return 1;
            }
            pos += used + (size_t)mlen;
            if (mtype == 0x2Fu) {
                done = 1;
            }
        } else if ((status_byte == 0xF0u) || (status_byte == 0xF7u)) {
            uint32_t slen = 0u;

            used = read_vlq(&g_buf[pos], track_end - pos, &slen);
            if (used == 0u) {
                (void)fprintf(stderr, "smfcheck: bad sysex length\n");
                return 1;
            }
            pos += used + (size_t)slen;
        } else {
            uint8_t kind = status_byte & 0xF0u;
            uint8_t ch = status_byte & 0x0Fu;
            uint8_t d1 = g_buf[pos];
            uint8_t d2 = 0u;
            size_t dlen = ((kind == 0xC0u) || (kind == 0xD0u)) ? 1u : 2u;

            if ((pos + dlen) > track_end) {
                (void)fprintf(stderr, "smfcheck: truncated event\n");
                return 1;
            }
            if (dlen == 2u) {
                d2 = g_buf[pos + 1u];
            }
            pos += dlen;

            if ((kind == 0x90u) && (d2 > 0u)) {
                onsets++;
                onsets_ch[ch]++;
                if (g_active[ch][d1] != 0u) {
                    (void)fprintf(stderr,
                                  "smfcheck: double note-on ch%u note%u "
                                  "at tick %llu\n",
                                  (unsigned int)ch, (unsigned int)d1,
                                  (unsigned long long)ticks);
                    errors++;
                } else {
                    g_active[ch][d1] = 1u;
                    poly_ch[ch]++;
                    poly_global++;
                    if (poly_ch[ch] > poly_ch_max[ch]) {
                        poly_ch_max[ch] = poly_ch[ch];
                    }
                    if (poly_global > poly_global_max) {
                        poly_global_max = poly_global;
                    }
                }
                if (pc_in_set(d1, root_pc) == 0) {
                    (void)fprintf(stderr,
                                  "smfcheck: pitch class violation ch%u "
                                  "note%u\n",
                                  (unsigned int)ch, (unsigned int)d1);
                    errors++;
                }
                if ((unsigned int)d2 > vel_max) {
                    vel_max = d2;
                }
                if (d2 > MAX_VELOCITY) {
                    (void)fprintf(stderr,
                                  "smfcheck: velocity %u above ceiling\n",
                                  (unsigned int)d2);
                    errors++;
                }
                {
                    uint64_t minute = ticks / 60000u;
                    if (minute < (uint64_t)MAX_MINUTES) {
                        g_minute_onsets[minute]++;
                    }
                }
            } else if ((kind == 0x80u) || ((kind == 0x90u) && (d2 == 0u))) {
                offs++;
                if (g_active[ch][d1] == 0u) {
                    (void)fprintf(stderr,
                                  "smfcheck: orphan note-off ch%u note%u\n",
                                  (unsigned int)ch, (unsigned int)d1);
                    errors++;
                } else {
                    g_active[ch][d1] = 0u;
                    poly_ch[ch]--;
                    poly_global--;
                }
            } else if (kind == 0xB0u) {
                cc_events++;
            } else {
                /* program change, pitch bend etc.: structural only */
            }
        }
        last_tick = ticks;
    }

    /* Final state: nothing may still be sounding. */
    {
        unsigned int ch;
        unsigned int note;

        for (ch = 0u; ch < 16u; ch++) {
            for (note = 0u; note < 128u; note++) {
                if (g_active[ch][note] != 0u) {
                    (void)fprintf(stderr,
                                  "smfcheck: stuck note ch%u note%u\n", ch,
                                  note);
                    errors++;
                }
            }
        }
    }
    if (poly_global_max > MAX_POLY_GLOBAL) {
        (void)fprintf(stderr, "smfcheck: global polyphony %u exceeds %u\n",
                      poly_global_max, (unsigned int)MAX_POLY_GLOBAL);
        errors++;
    }
    {
        unsigned int ch;
        for (ch = 0u; ch < 16u; ch++) {
            if (poly_ch_max[ch] > MAX_POLY_CH) {
                (void)fprintf(stderr,
                              "smfcheck: ch%u polyphony %u exceeds %u\n", ch,
                              poly_ch_max[ch], (unsigned int)MAX_POLY_CH);
                errors++;
            }
        }
    }
    {
        uint64_t minutes = (last_tick / 60000u) + 1u;
        uint64_t m;
        uint32_t dens_max = 0u;

        if (minutes > (uint64_t)MAX_MINUTES) {
            minutes = (uint64_t)MAX_MINUTES;
        }
        for (m = 0u; m < minutes; m++) {
            if (g_minute_onsets[m] > dens_max) {
                dens_max = g_minute_onsets[m];
            }
            if (g_minute_onsets[m] > MAX_ONSETS_PER_MIN) {
                (void)fprintf(stderr,
                              "smfcheck: %u onsets in minute %llu exceeds "
                              "%u\n",
                              (unsigned int)g_minute_onsets[m],
                              (unsigned long long)m,
                              (unsigned int)MAX_ONSETS_PER_MIN);
                errors++;
            }
        }

        (void)printf("smfcheck: %s\n", argv[1]);
        (void)printf("  duration        %llu min\n",
                     (unsigned long long)(last_tick / 60000u));
        (void)printf("  note onsets     %lu (%.2f/min avg, %u/min max)\n",
                     onsets,
                     (double)onsets / (double)minutes,
                     (unsigned int)dens_max);
        (void)printf("  note offs       %lu\n", offs);
        {
            unsigned int ch;
            for (ch = 0u; ch < 16u; ch++) {
                if (onsets_ch[ch] > 0u) {
                    (void)printf("    ch%-2u onsets   %lu\n", ch,
                                 onsets_ch[ch]);
                }
            }
        }
        (void)printf("  cc events       %lu\n", cc_events);
        (void)printf("  max polyphony   %u global\n", poly_global_max);
        (void)printf("  max velocity    %u\n", vel_max);
        (void)printf("  violations      %lu\n", errors);
    }

    return (errors == 0u) ? 0 : 1;
}
