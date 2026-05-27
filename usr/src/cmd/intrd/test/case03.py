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
# case03.py - spec for gen_case.py
#
# Tests the pathological overload path: when a CPU's interrupt load exceeds
# goodness_unsafe_load (90%) with multiple interrupt sources, goodness_cpu()
# returns 1.0 (worst case), which trips imbalanced() immediately via the
# "goodness > 0.5" fast path — without waiting for baseline drift. This is
# distinct from case02's normal imbalance: here the concern is interrupt
# starvation, so intrd reacts before a full window of history accumulates.

description = "case03: 4 CPUs, 3 interrupts all on CPU 0 (95% load). Expected: reconfig."

cpus    = 4
samples = 10

interrupts = [
    dict(name='igb0',  itype='msi',   ino=32, pil=6, cpu=0, load=0.50),
    dict(name='nvme0', itype='msix',  ino=33, pil=6, cpu=0, load=0.30),
    dict(name='ahci0', itype='fixed', ino=34, pil=5, cpu=0, load=0.15),
]
