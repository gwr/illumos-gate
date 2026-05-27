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
# case08.py - spec for gen_case.py
#
# Tests the drift-based imbalanced() trigger. The initial load is unbalanced
# but goodness is below mindelta (10%), so the first evaluation sets a baseline
# via "goodness good enough" rather than reconfiguring. When one interrupt's
# load increases mid-run, goodness rises gradually through the rolling 60s
# window. Once the window reflects enough new-load deltas, goodness drifts
# more than mindelta above the baseline, tripping imbalanced() and triggering
# a reconfig. Confirms intrd responds to gradual load changes, not just
# sudden step imbalances.

description = "case08: intr_b load drifts from 5% to 20% at sample 10. Expected: baseline set at delta 6, drift reconfig at delta 14."

cpus    = 4
samples = 14

interrupts = [
    dict(name='igb0',  itype='fixed', ino=32, pil=6, cpu=0, load=0.25),
    dict(name='nvme0', itype='fixed', ino=33, pil=6, cpu=0, load=0.05),
]

def per_sample(n, spec):
    if n >= 10:
        for intr in spec['interrupts']:
            if intr['name'] == 'nvme0':
                intr['load'] = 0.20
    return spec
