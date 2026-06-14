# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.0.0] - 2026-06-13

### Added

- Generative ambient MIDI engine inspired by Brian Eno's *Music for Airports*:
  six independent voice layers (drone, two harmonic beds, two melodic fragments,
  burst chime) with mutually incommensurate cycle periods so the music never
  literally repeats.
- System-resource-driven "weather": CPU load scales event density, memory
  pressure pulls voices lower and darker, sustained network activity brightens
  the mode and timbre (CC74), disk activity swells the drone (CC11).  One
  mapping is deliberately legible — a network traffic burst may ring a single
  soft glockenspiel chime, rate-limited to at most one every eight seconds.
- Pentatonic pitch palette that cannot produce a wrong vertical combination;
  configurable root (`--root 0..11`, default F) and mood (`--mood bright|dark`).
- EMA-smoothed metric normalization against a rolling 30-minute history so the
  engine is musically alive on an idle laptop and a loaded server alike.
- Deterministic simulation mode (`--simulate TRACE --sim-minutes N`): metrics
  come from a CSV trace file, time is a fake clock, and output is a Standard
  MIDI File.  Eight simulated hours render in well under a second; identical
  seed and trace produce byte-identical output.
- `tools/smfcheck`: non-production SMF auditor that validates note pairing,
  per-channel and global polyphony caps, pitch-class membership, velocity
  ceiling, and onset-density bounds.
- `tools/gen_trace.sh`: deterministic eight-hour metric trace generator for
  use with simulation mode.
- Command-line options: `--midi-out PATH`, `--seed N`, `--root 0..11`,
  `--mood bright|dark`, `--density 25..400`, `--simulate TRACE`,
  `--sim-minutes N`.
- Clean SIGINT/SIGTERM shutdown: every sounding note is released and CC123
  (All Notes Off) is sent on every channel before the process exits.
- `daft(1)` man page.
- `config.mk` (suckless-style) with `PREFIX`, `BINDIR`, `MANDIR`, and `CC`
  overrides; `make install` / `make uninstall` with full `DESTDIR` support.
- `make analyze` target: `cppcheck` with the MISRA-C:2012 addon, run at
  exhaustive check level.  Approved deviations recorded in `docs/deviations/`.
- Zero runtime dependencies beyond libc and libm; MIDI leaves the process as a
  raw byte stream via `write(2)`.

[Unreleased]: https://github.com/mcpcpc/daft/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/mcpcpc/daft/releases/tag/v1.0.0
