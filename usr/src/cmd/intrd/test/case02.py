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
# case02.py - spec for gen_case.py
#
# Tests the core rebalancing path end-to-end: all interrupts start on one
# CPU, creating clear imbalance. imbalanced() fires once the rolling window
# accumulates enough history, do_reconfig() runs the full sort-and-move
# algorithm, and intrmove is called to spread the interrupts across idle CPUs.
# Confirms the basic goodness → imbalanced → do_reconfig → intrmove pipeline.

description = "case02: 4 CPUs, 3 interrupts all on CPU 0 (70% load). Expected: reconfig."

cpus    = 4
samples = 10

interrupts = [
    dict(name='igb0',  itype='msi',   ino=32, pil=6, cpu=0, load=0.35),
    dict(name='nvme0', itype='msix',  ino=33, pil=6, cpu=0, load=0.20),
    dict(name='ahci0', itype='fixed', ino=34, pil=5, cpu=0, load=0.15),
]
