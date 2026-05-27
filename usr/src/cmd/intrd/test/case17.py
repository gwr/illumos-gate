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
# case17.py - spec for gen_case.py
#
# Tests the intr_move() failure path in do_reconfig() and subsequent recovery.
# fail_move_count=2 causes the first two intr_move() calls to fail, making
# do_reconfig() return -1 and the main loop log "do_reconfig FAILED!" and call
# clear_deltas().  After the window refills (8 more samples), the retry attempt
# succeeds normally, confirming the algorithm recovers from a transient failure.
#
# Scenario identical to case02 (all interrupts on CPU 0, clear imbalance).

description = "case17: same as case02 but first two intr_move() calls fail. Expected: FAILED then recovery."

cpus            = 4
samples         = 17
fail_move_count = 2

interrupts = [
    dict(name='igb0',  itype='msi',   ino=32, pil=6, cpu=0, load=0.35),
    dict(name='nvme0', itype='msix',  ino=33, pil=6, cpu=0, load=0.20),
    dict(name='ahci0', itype='fixed', ino=34, pil=5, cpu=0, load=0.15),
]
