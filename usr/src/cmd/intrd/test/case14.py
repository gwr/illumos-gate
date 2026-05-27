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
# case14.py - spec for gen_case.py
#
# Tests interrupt handler sharing (IHS): when multiple pci_intrs kstat entries
# share the same (buspath, ino) cookie — as with some legacy hardware where
# several devices share a single IRQ line — getstat() must merge them into one
# ivec with ihs>1 and summed time, rather than treating them as independent
# vectors. The rebalancing algorithm then moves the merged entry as a unit
# with a single intrmove call. Confirms getstat() aggregates shared-handler
# interrupts correctly so load accounting reflects the true hardware vector.

description = "case14: two pci_intrs share ino=33 (IHS), merged into one ivec"

cpus    = 2
samples = 12

interrupts = [
    dict(name='igb0',  itype='fixed', ino=32, pil=6, cpu=0, load=0.40),
    # dev_a and dev_b share ino=33 on CPU0 (ihs=2 after merge in getstat)
    dict(name='dev_a', itype='fixed', ino=33, pil=5, cpu=0, load=0.15),
    dict(name='dev_b', itype='fixed', ino=33, pil=5, cpu=0, load=0.15),
]
