#!/bin/sh
# Non-production test tool (see tools/README.md).
# Generates a deterministic synthetic 8-hour metric trace on stdout:
#   t_seconds,cpu_permille,mem_permille,net_Bps,disk_sps
# Usage: tools/gen_trace.sh [seconds] > trace.csv

SECONDS_TOTAL="${1:-28800}"

awk -v total="$SECONDS_TOTAL" 'BEGIN {
    srand(42); # fixed seed: deterministic per awk implementation
    print "t,cpu_permille,mem_permille,net_Bps,disk_sps";
    for (t = 0; t < total; t++) {
        # CPU: idle noise + a long compile-like busy block + a smaller one
        cpu = 40 + int(rand() * 60);
        if (t >= 7200 && t < 10800)  cpu = 550 + int(rand() * 300);
        if (t >= 18000 && t < 19800) cpu = 250 + int(rand() * 150);

        # Memory: slow tide up then partially released
        if (t < 14400)      mem = 350 + int(t * 300 / 14400);
        else                mem = 650 - int((t - 14400) * 200 / 14400);
        mem += int(rand() * 20);

        # Network: light chatter, a streaming hour, sparse sharp bursts
        net = 1500 + int(rand() * 6000);
        if (t >= 21600 && t < 25200) net = 220000 + int(rand() * 60000);
        if (int(rand() * 600) == 0)  net = 300000 + int(rand() * 500000);

        # Disk: quiet with occasional flushes, busier during the build
        dsk = int(rand() * 300);
        if (t >= 7200 && t < 10800)  dsk = 1500 + int(rand() * 2500);
        if (int(rand() * 900) == 0)  dsk = 8000 + int(rand() * 8000);

        printf "%d,%d,%d,%d,%d\n", t, cpu, mem, net, dsk;
    }
}'
