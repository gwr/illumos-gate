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
# case15.py - spec for gen_case.py
#
# Tests the "tgtload > load" early-exit in the inner loop of
# do_reconfig_cpu(). After one cpu2cpu pass offloads nvme0 from cpu0 to
# the idle cpu1, cpu0 drops from 70% to 50%. The inner loop then tries
# cpu3 (30%) as a target — finds tgtload < load, runs cpu2cpu but makes
# no moves since the goal algorithm keeps ahci0 on cpu0. Next target is
# cpu2 (55%), which is now BUSIER than the lightened cpu0 (50%), so
# tgtload > load fires and the inner loop exits.
#
# Layout (avgintrload = (70+0+55+30)/4 = 38.75%):
#   cpu0: igb0(35%) + nvme0(20%) + ahci0(15%) = 70%
#   cpu1: idle (0%)
#   cpu2: em0(55%)                             — one intr, gc=0, can't help
#   cpu3: virtio(30%)
#
# Expected: nvme0 moves cpu0→cpu1 only. Goodness 31.25% → 11.25%.

description = "case15: tgtload > load break in do_reconfig_cpu inner loop"

cpus    = 4
samples = 10

interrupts = [
    dict(name='igb0',   itype='msi',   ino=32, pil=6, cpu=0, load=0.35),
    dict(name='nvme0',  itype='msix',  ino=33, pil=6, cpu=0, load=0.20),
    dict(name='ahci0',  itype='fixed', ino=34, pil=5, cpu=0, load=0.15),
    dict(name='em0',    itype='fixed', ino=35, pil=5, cpu=2, load=0.55),
    dict(name='virtio', itype='fixed', ino=36, pil=5, cpu=3, load=0.30),
]
