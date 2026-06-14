# tools/ — non-production test support

Everything in this directory is explicitly **non-production** code per
AGENTS.md ("unless explicitly marked as non-production"). These tools never
run on a target system; they audit and exercise the production binary from
the outside.

- `smfcheck.c` — Standard MIDI File auditor for simulation output. Verifies
  note-on/note-off pairing, polyphony caps, pentatonic pitch-class
  membership, velocity ceiling, and onset-density bounds. Built by `make`
  with the full production flag set, but exempt from the production logging
  and MISRA process (it uses stdio deliberately).
- `gen_trace.sh` — deterministic synthetic metric-trace generator (awk) for
  the simulation mode. Output format:
  `t_seconds,cpu_permille,mem_permille,net_Bps,disk_sps`.
