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
# case06.py - spec for gen_case.py
#
# Tests CPU hotplug detection. When the number of on-line CPUs changes
# between samples, generate_delta() detects the mismatch and sets missing=1,
# causing the main loop to call clear_deltas() and restart accumulation.
# Confirms intrd doesn't incorporate a delta computed across mismatched CPU
# sets into its rolling window — which would corrupt load averages.

description = "case06: CPU 3 comes online at sample 7. Expected: delta cleared, restart."

cpus    = 4
samples = 12

interrupts = [
    dict(name='igb0',  itype='msi',   ino=32, pil=6, cpu=0, load=0.001),
    dict(name='nvme0', itype='msix',  ino=33, pil=6, cpu=1, load=0.001),
    dict(name='ahci0', itype='fixed', ino=34, pil=5, cpu=2, load=0.001),
]

def per_sample(n, spec):
    if n <= 6:
        spec['cpus'] = 3   # CPU 3 not yet online
    return spec
