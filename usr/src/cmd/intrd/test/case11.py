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
# case11.py - spec for gen_case.py
#
# Tests the intrd-starved path: when the elapsed time between kstat samples
# exceeds statslen (60s), the delta is considered unreliable — the daemon was
# likely starved of CPU by a heavily loaded system. Rather than incorporate
# suspect data into the rolling window, intrd calls clear_deltas() and logs
# "evaluating interrupt assignments". Confirms intrd degrades gracefully under
# CPU starvation instead of acting on stale measurements.

description = "case11: interval=70s > statslen=60s, intrd starved path"

cpus     = 4
samples  = 12
interval = 70.0

interrupts = [
    dict(name='igb0',  itype='msi',   ino=32, pil=6, cpu=0, load=0.25),
    dict(name='nvme0', itype='msix',  ino=33, pil=6, cpu=1, load=0.25),
    dict(name='ahci0', itype='fixed', ino=34, pil=5, cpu=2, load=0.25),
]
