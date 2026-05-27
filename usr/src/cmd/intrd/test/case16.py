#!/usr/bin/env python3
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# Copyright 2026 Gordon W. Ross
#
# case16.py - generate case16.kstats for the intrd test suite.
#
# Tests the timerange_toohi rejection path in getstat(): when the spread
# between the first and last kstat snaptime in one collection cycle exceeds
# sleeptime * NANOSEC/100 (i.e. 1% of sleeptime = 100ms for normal_sleeptime
# of 10s), getstat() discards the snapshot and returns nullopt.  The main
# loop skips that iteration (no delta, no GOODNESS line).
#
# Scenario: 4 CPUs, one interrupt per CPU at 10% load (balanced, goodness=0,
# no reconfig expected).  Samples 4 and 9 have a 1-second spread between
# cpu kstat snaptimes and pci_intrs kstat snaptimes — well above the 100ms
# limit — causing getstat() to reject them.  The resulting output has 9
# GOODNESS lines instead of the 11 that would appear without the rejections.
#
# Note: parse_sec_to_nsec() in intrd-test.cc requires integer-second values
# (no fractional component), so the snaptime offset must be a whole number
# of seconds.  1 second >> 100ms limit, so this reliably triggers rejection.
#
# Usage: python3 case16.py > case16.kstats

import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_case import init_interrupts, make_state, gen_sample

CPUS       = 4
SAMPLES    = 12
INTERVAL   = 10.0     # seconds between normal samples
START_TIME = 1000.0   # snaptime of first sample (integer seconds)
INTR_LOAD  = 0.10     # interrupt load per CPU (10%)
standalone = True     # standalone script; gen_case.py must not generate additional output

# Samples whose pci_intrs snaptimes are offset by 1s, triggering rejection.
SLOW_SAMPLES = {4, 9}

# One interrupt per CPU, ino=32..35
interrupts = [
    dict(name='igb0',  itype='msi',   ino=32, pil=6, cpu=0, load=INTR_LOAD),
    dict(name='nvme0', itype='msix',  ino=33, pil=6, cpu=1, load=INTR_LOAD),
    dict(name='ahci0', itype='fixed', ino=34, pil=5, cpu=2, load=INTR_LOAD),
    dict(name='em0',   itype='fixed', ino=35, pil=5, cpu=3, load=INTR_LOAD),
]

print("# case16: 4 CPUs, 1 interrupt per CPU at 10% (balanced, goodness=0).")
print("# Samples 4 and 9 have a 1s snaptime spread between cpu and pci_intrs")
print("# kstats, triggering getstat() timerange_toohi rejection.")
print()

init_interrupts(interrupts)
state = make_state(CPUS, interrupts)

for n in range(1, SAMPLES + 1):
    snaptime = START_TIME + (n - 1) * INTERVAL
    pci_snap = snaptime + 1.0 if n in SLOW_SAMPLES else snaptime
    comment  = '(slow - triggers timerange_toohi rejection)' if n in SLOW_SAMPLES else ''
    gen_sample(n, snaptime, CPUS, interrupts, state, pci_snaptime=pci_snap, comment=comment)
