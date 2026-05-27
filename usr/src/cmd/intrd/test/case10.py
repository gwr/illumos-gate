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
# case10.py - spec for gen_case.py
#
# Tests the single-CPU early return in getstat(). With only one CPU on-line,
# there is nowhere to move interrupts and distribution is pointless. getstat()
# detects this, transitions sleeptime to the 15-minute one-CPU poll interval,
# and returns 0 to abort the main loop iteration. Confirms intrd handles
# single-CPU systems without attempting any evaluation or reconfig.

description = "case10: single CPU online. Expected: sleeptime 10 -> 900, no reconfig."

cpus    = 1
samples = 5

interrupts = [
    dict(name='igb0', itype='fixed', ino=32, pil=6, cpu=0, load=0.10),
]
