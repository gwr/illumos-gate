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
# case19.py - spec for gen_case.py
#
# Tests that the correct buspath is passed to intr_move() when two interrupts
# on different PCI host bridges share the same ino.  cpu0 is overloaded with
# two interrupts, one of which is ino=32 on /pci@0,0.  A separate device also
# uses ino=32 but on /pci@1,0 (on cpu2, not overloaded).
#
# When reconfig fires, ino=32 from /pci@0,0 must be moved (not the one on
# /pci@1,0).  The intrmove output line shows buspath=/pci@0,0, confirming the
# correct vector was selected.  If keying were wrong the two ino=32 vectors
# could be confused, causing either the wrong move or a corrupted delta.

description = "case19: two ino=32 on different buspaths; cpu0 overloaded. Expected: reconfig moves /pci@0,0 ino=32."

cpus    = 4
samples = 10

interrupts = [
    # cpu0 overloaded: two interrupts totalling 55%
    dict(name='igb0',  itype='fixed', ino=32, pil=6, cpu=0, load=0.35,
         buspath='/pci@0,0'),
    dict(name='nvme0', itype='msix',  ino=33, pil=6, cpu=0, load=0.20),
    # ino=32 on a different bus, different CPU — must not be confused with igb0
    dict(name='em0',   itype='fixed', ino=32, pil=6, cpu=2, load=0.10,
         buspath='/pci@1,0'),
    # one more interrupt to give cpu3 some load
    dict(name='ahci0', itype='fixed', ino=34, pil=5, cpu=3, load=0.10),
]
