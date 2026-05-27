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
# case13.py - spec for gen_case.py
#
# Tests that do_reconfig()'s outer loop processes all overloaded CPUs, not
# just the first one. The loop re-sorts CPUs by load after each iteration and
# continues until every CPU falls within mindelta of the average. With two
# CPUs both exceeding that threshold, both must be visited and intrmove called
# for each. Confirms the multi-CPU rebalancing loop runs to full completion.

description = "case13: two overloaded CPUs, do_reconfig processes both"

cpus    = 4
samples = 12

interrupts = [
    dict(name='igb0',  itype='fixed', ino=32, pil=6, cpu=0, load=0.60),
    dict(name='nvme0', itype='msix',  ino=33, pil=6, cpu=0, load=0.15),
    dict(name='ahci0', itype='fixed', ino=34, pil=5, cpu=1, load=0.55),
    dict(name='em0',   itype='fixed', ino=35, pil=5, cpu=1, load=0.15),
]
