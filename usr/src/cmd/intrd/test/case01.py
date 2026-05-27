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
# case01.py - spec for gen_case.py
#
# Tests the idle steady-state: when total interrupt load is far below the
# idle threshold (10%), compress_deltas() should transition sleeptime to the
# longer idle interval, and do_reconfig() should exit via "goodness good
# enough" without calling intrmove. Confirms intrd does not thrash when there
# is nothing meaningful to rebalance.

description = "case01: 4 CPUs, 3 interrupts, idle load. Expected: no reconfig."

cpus    = 4
samples = 12

interrupts = [
    dict(name='igb0',  itype='msi',   ino=32, pil=6, cpu=0, load=0.001),
    dict(name='nvme0', itype='msix',  ino=33, pil=6, cpu=1, load=0.001),
    dict(name='ahci0', itype='fixed', ino=34, pil=5, cpu=2, load=0.001),
]
