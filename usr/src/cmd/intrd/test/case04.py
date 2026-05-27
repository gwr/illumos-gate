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
# case04.py - spec for gen_case.py
#
# Tests that intrd recognizes a structurally optimal assignment. With only
# one interrupt vector, load_no_bigintr is always zero, so goodness_cpu()
# returns 0 regardless of how high the load is. do_reconfig() exits via
# "goodness good enough" without ever calling intrmove. Confirms the
# algorithm doesn't thrash when there is nothing to move.

description = "case04: 4 CPUs, 1 interrupt on CPU 0 (60% load). Expected: goodness 0%, no reconfig."

cpus    = 4
samples = 10

interrupts = [
    dict(name='igb0', itype='msi', ino=32, pil=6, cpu=0, load=0.60),
]
