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
# case09.py - spec for gen_case.py
#
# Tests the idle sleeptime transition mid-run. The system starts with active
# but balanced interrupt load, so the first evaluation sets a baseline without
# reconfiguring. Load then drops sharply. Because compress_deltas() works over
# a rolling 60s window, the sleeptime transition doesn't fire until enough
# low-load deltas have displaced the earlier high-load ones. Confirms the
# hysteresis in idle detection — the rolling average prevents premature idle
# transitions from a brief dip in load.

description = "case09: load drops from 20% to 1% at sample 8. Expected: goodness good enough at delta 6, idle sleeptime at delta 12."

cpus    = 4
samples = 14

interrupts = [
    dict(name='igb0',  itype='fixed', ino=32, pil=6, cpu=0, load=0.20),
    dict(name='nvme0', itype='fixed', ino=33, pil=6, cpu=1, load=0.20),
    dict(name='ahci0', itype='fixed', ino=34, pil=5, cpu=2, load=0.20),
]

def per_sample(n, spec):
    if n >= 8:
        for intr in spec['interrupts']:
            intr['load'] = 0.01
    return spec
