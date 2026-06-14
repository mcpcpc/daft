# daft

A generative ambient MIDI engine that treats your computer as weather.

daft plays endless, sparse, Eno-style ambient music — independent voice
layers with mutually incommensurate cycle periods over a root drone, all
drawn from a pentatonic palette that cannot produce a wrong vertical
combination. Your machine's vital signs steer the garden slowly and
indirectly: CPU load breathes life into the event density, memory pressure
pulls the voices lower and darker, sustained network activity brightens the
mode and timbre, disk activity swells the drone. One mapping is deliberately
legible: a network burst may ring a single distant glockenspiel chime.

Strictly portable POSIX C99, zero library dependencies (libc/libm only).
MIDI leaves the program as a raw byte stream through plain `write()`.

## Build

```sh
make            # builds ./daft and tools/smfcheck
make analyze    # cppcheck + MISRA addon (must be clean)
```

## Run (live)

Route the output to any GM-capable synth. With the kernel's virtual MIDI
card (no libraries needed):

```sh
modprobe snd-virmidi        # creates /dev/snd/midiC*D* and /dev/midi
./daft                      # writes raw MIDI to /dev/midi
```

Then connect the virtual port to a synth (e.g. FluidSynth) with your
preferred ALSA routing tool. Any path works: `--midi-out` accepts a rawmidi
device, a FIFO, or a plain file.

```sh
./daft --midi-out /dev/snd/midiC1D0 --root 5 --mood bright --density 100
```

SIGINT/SIGTERM shut down cleanly: every sounding note is released and
CC123 (All Notes Off) is sent on every channel.

Options: `--midi-out PATH`, `--seed N`, `--root 0..11`,
`--mood bright|dark`, `--density 25..400`, `--simulate TRACE`,
`--sim-minutes N`. See `daft.1` for full documentation.

## Simulate (deterministic fast-forward)

Ambient bugs are slow bugs, so eight hours can be auditioned in a fifth of
a second. Simulation uses a fake clock and a CSV metric trace, and writes a
Standard MIDI File; identical seed + trace produce byte-identical output.

```sh
tools/gen_trace.sh > /tmp/trace.csv
./daft --simulate /tmp/trace.csv --sim-minutes 480 \
       --midi-out /tmp/day.mid --seed 42
tools/smfcheck /tmp/day.mid 5     # audit: pairing, polyphony, pitch set,
                                  # velocity ceiling, density bounds
```

The resulting `.mid` plays in any SMF player — a quick way to *listen* to a
whole simulated workday.

## Architecture

```
metrics (/proc or trace) -> weather (EMA + adaptive normalization)
  -> conductor (mode, density, register, brightness; slow modal drift)
    -> 6 layers (drone, 2 beds, 2 melodies, burst chime)
      -> sched (bounded onset queue) -> voices (polyphony caps, note offs)
        -> midi (raw stream or SMF)
```

Engineering rules (MISRA-C, NASA Power of 10, no heap, status codes
everywhere, deterministic time/PRNG interfaces) are in `AGENTS.md`;
approved deviations are recorded in `docs/deviations/`.
