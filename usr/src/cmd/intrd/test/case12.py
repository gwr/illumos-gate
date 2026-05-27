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
# case12.py - spec for gen_case.py
#
# Tests the "goodness already near optimum" path in do_reconfig() — the case
# where the full rebalancing algorithm runs to completion but the computed
# improvement is less than mindelta. This is distinct from the "goodness good
# enough" early exit, which never enters the algorithm at all. Here goodness
# is high enough to trigger do_reconfig(), but the load distribution is
# structured so that the best achievable rearrangement is only marginally
# better than the existing arrangement. Confirms intrd avoids disruptive
# interrupt moves when the gain is marginal.

description = "case12: goodness improvement < mindelta, near optimum path"

cpus    = 3
samples = 12

interrupts = [
    dict(name='igb0',  itype='fixed', ino=32, pil=6, cpu=0, load=0.45),
    dict(name='nvme0', itype='msix',  ino=33, pil=6, cpu=0, load=0.15),
    dict(name='ahci0', itype='fixed', ino=34, pil=5, cpu=1, load=0.35),
    dict(name='em0',   itype='fixed', ino=35, pil=5, cpu=1, load=0.15),
]
