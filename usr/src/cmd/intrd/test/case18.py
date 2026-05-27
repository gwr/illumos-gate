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
# case18.py - spec for gen_case.py
#
# Tests that interrupt identity is correctly keyed on buspath+ino, not ino
# alone.  Two interrupts share ino=32 but are on different PCI host bridges
# (/pci@0,0 and /pci@1,0).  They must not be merged into a single IHS entry.
#
# Scenario: 4 CPUs, one interrupt per CPU at balanced load (10% each).
# ino=32 appears on both /pci@0,0 (cpu0) and /pci@1,0 (cpu1).  Goodness
# stays at 0% throughout — no reconfig expected.  If keying were wrong the
# two ino=32 vectors would merge into an IHS entry, distorting goodness.

description = "case18: two interrupts share ino=32 on different buspaths. Expected: no IHS merge, no reconfig."

cpus    = 4
samples = 12

interrupts = [
    dict(name='igb0',  itype='fixed', ino=32, pil=6, cpu=0, load=0.10,
         buspath='/pci@0,0'),
    dict(name='em0',   itype='fixed', ino=32, pil=6, cpu=1, load=0.10,
         buspath='/pci@1,0'),
    dict(name='nvme0', itype='msix',  ino=33, pil=6, cpu=2, load=0.10),
    dict(name='ahci0', itype='fixed', ino=34, pil=5, cpu=3, load=0.10),
]
