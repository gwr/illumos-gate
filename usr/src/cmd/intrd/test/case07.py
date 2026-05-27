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
# case07.py - spec for gen_case.py
#
# Tests interrupt identity change detection via crtime. If an interrupt's
# crtime changes between samples — as happens when a device is removed and
# re-added or resets mid-run — the time delta would be meaningless or
# negative. generate_delta() detects the crtime mismatch and sets missing=1,
# triggering clear_deltas() so accumulation starts fresh. Confirms intrd
# handles device-level reconfiguration events without corrupting its window.

description = "case07: igb0 crtime changes at sample 7. Expected: delta cleared, restart."

cpus    = 4
samples = 12

interrupts = [
    dict(name='igb0',  itype='msi',   ino=32, pil=6, cpu=0, load=0.001),
    dict(name='nvme0', itype='msix',  ino=33, pil=6, cpu=1, load=0.001),
    dict(name='ahci0', itype='fixed', ino=34, pil=5, cpu=2, load=0.001),
]

def per_sample(n, spec):
    if n >= 7:
        for intr in spec['interrupts']:
            if intr['name'] == 'igb0':
                intr['crtime'] = 70.0   # changed from default 20.0
    return spec
