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
# case05.py - spec for gen_case.py
#
# Tests the MSI group-move logic on pcplusmp APIC systems. When is_apic=1,
# all MSI vectors of the same device instance must be routed to the same CPU
# by hardware, so intrd must move them together as a group. The smaller
# interrupt group (igb0, 4 vectors) is moved rather than the dominant single
# interrupt (nvme0), producing a single intrmove call with num_ino=4. Confirms
# that the group-move aggregation and the "keep the largest, move the rest"
# heuristic both work correctly.

description = "case05: 4 CPUs, igb0 with 4 MSI vectors on CPU 0 (30% combined), nvme0 msix 50%. is_apic=1. Expected: group move num_ino=4."

cpus    = 4
samples = 10
is_apic = 1

# igb0 group (combined 30%) is smaller than nvme0 (50%), so the algorithm
# keeps nvme0 on CPU 0 and moves the igb0 group — producing num_ino=4.
interrupts = [
    dict(name='igb0',  itype='msi',   ino=32, pil=6, cpu=0, load=0.10),
    dict(name='igb0',  itype='msi',   ino=33, pil=6, cpu=0, load=0.08),
    dict(name='igb0',  itype='msi',   ino=34, pil=6, cpu=0, load=0.07),
    dict(name='igb0',  itype='msi',   ino=35, pil=6, cpu=0, load=0.05),
    dict(name='nvme0', itype='msix',  ino=40, pil=6, cpu=0, load=0.50),
]
