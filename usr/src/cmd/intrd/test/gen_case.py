#!/usr/bin/env python3
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
# gen_case.py - generate intrd test .kstats files from Python spec files
#
# Usage: python3 gen_case.py caseNN.py > caseNN.kstats
#
# Spec file variables:
#   description  str   one-line description (written as comment in output)
#   cpus         int   number of CPUs (default 4)
#   samples      int   number of snapshots (default 12)
#   interval     float seconds between snapshots (default 10.0)
#   start_time   float snaptime of first snapshot (default 1000.0)
#   is_apic      int   0 or 1, enables MSI group logic (default 0)
#   fail_move_count int  number of intr_move() calls to fail before succeeding (default 0)
#   interrupts   list  of dicts, one per interrupt vector:
#     name       str   device name (e.g. 'igb0')
#     itype      str   'msi', 'msix', or 'fixed'
#     ino        int   interrupt number
#     pil        int   priority level
#     cpu        int   CPU assignment
#     buspath    str   PCI bus path (default '/pci@0,0')
#     nexus      str   nexus driver name (default 'npe')
#     load       float fraction of one CPU's time used by this interrupt
#                      (e.g. 0.60 = 60% of 10B ns per interval = 6B ns)
#
# For scenarios where something changes mid-run (hotplug, intr reassignment),
# the spec can define a per_sample(n, spec) function that returns a modified
# copy of the spec for sample n. See doc/intrd-test-plan.md for details.

import sys
import runpy

TOT_NSEC = 10_000_000_000   # ns per CPU per 10s interval
USER_NSEC =    100_000_000   # simulated user time per CPU per interval

def cpu_intr_nsec(intrs, c):
    """Return total interrupt nsec/interval for CPU c given an interrupt list."""
    return sum(int(i['load'] * TOT_NSEC) for i in intrs if i['cpu'] == c)


def init_interrupts(interrupts):
    """Assign _inst numbers and default nexus/buspath to an interrupt list."""
    for idx, intr in enumerate(interrupts):
        intr.setdefault('nexus',   'npe')
        intr.setdefault('buspath', '/pci@0,0')
        intr['_inst'] = idx + 1


def make_state(cpus, interrupts):
    """Return a fresh accumulator state dict, pre-loaded with 10 intervals."""
    cpu_idle_acc   = {}
    cpu_kernel_acc = {}
    cpu_user_acc   = {}
    intr_time_acc  = {}
    for c in range(cpus):
        intr_ns = cpu_intr_nsec(interrupts, c)
        cpu_idle_acc[c]   = (TOT_NSEC - intr_ns - USER_NSEC) * 10
        cpu_kernel_acc[c] = intr_ns * 10
        cpu_user_acc[c]   = USER_NSEC * 10
    for intr in interrupts:
        intr_time_acc[intr['_inst']] = int(intr['load'] * TOT_NSEC) * 10
    return dict(cpu_idle_acc=cpu_idle_acc, cpu_kernel_acc=cpu_kernel_acc,
                cpu_user_acc=cpu_user_acc, intr_time_acc=intr_time_acc)


def gen_sample(n, snaptime, cur_cpus, cur_intrs, state, pci_snaptime=None, comment=''):
    """Advance state by one interval and print the kstat block for sample n."""
    snaptime = float(snaptime)
    if pci_snaptime is None:
        pci_snaptime = snaptime
    else:
        pci_snaptime = float(pci_snaptime)

    cpu_idle_acc   = state['cpu_idle_acc']
    cpu_kernel_acc = state['cpu_kernel_acc']
    cpu_user_acc   = state['cpu_user_acc']
    intr_time_acc  = state['intr_time_acc']

    suffix = f"  {comment}" if comment else ''
    print(f"# sample {n}  t={snaptime:.1f}{suffix}")

    for c in range(cur_cpus):
        if c not in cpu_idle_acc:
            cpu_idle_acc[c] = cpu_kernel_acc[c] = cpu_user_acc[c] = 0
        intr_ns = cpu_intr_nsec(cur_intrs, c)
        cpu_idle_acc[c]   += TOT_NSEC - intr_ns - USER_NSEC
        cpu_kernel_acc[c] += intr_ns
        cpu_user_acc[c]   += USER_NSEC
    for intr in cur_intrs:
        inst = intr['_inst']
        if inst not in intr_time_acc:
            intr_time_acc[inst] = 0
        intr_time_acc[inst] += int(intr['load'] * TOT_NSEC)

    for c in range(cur_cpus):
        print(f"cpu:{c}:sys:cpu_nsec_idle\t{cpu_idle_acc[c]}")
        print(f"cpu:{c}:sys:cpu_nsec_user\t{cpu_user_acc[c]}")
        print(f"cpu:{c}:sys:cpu_nsec_kernel\t{cpu_kernel_acc[c]}")
        print(f"cpu:{c}:sys:crtime\t1.0")
        print(f"cpu:{c}:sys:snaptime\t{snaptime}")
        print(f"cpu_info:{c}:cpu_info{c}:state\ton-line")

    for intr in cur_intrs:
        inst  = intr['_inst']
        nexus = intr['nexus']
        print(f"pci_intrs:{inst}:{nexus}:cpu\t{intr['cpu']}")
        print(f"pci_intrs:{inst}:{nexus}:type\t{intr['itype']}")
        print(f"pci_intrs:{inst}:{nexus}:time\t{intr_time_acc[inst]}")
        print(f"pci_intrs:{inst}:{nexus}:pil\t{intr['pil']}")
        print(f"pci_intrs:{inst}:{nexus}:ino\t{intr['ino']}")
        print(f"pci_intrs:{inst}:{nexus}:buspath\t{intr['buspath']}")
        print(f"pci_intrs:{inst}:{nexus}:name\t{intr['name']}")
        print(f"pci_intrs:{inst}:{nexus}:crtime\t{intr.get('crtime', 20.0)}")
        print(f"pci_intrs:{inst}:{nexus}:snaptime\t{pci_snaptime}")
    print()


def gen_kstats(spec_path):
    spec = runpy.run_path(spec_path)

    # Script-style specs set standalone=True and produce their own output
    # when executed by runpy above; nothing more to do.
    if spec.get('standalone', False):
        return

    description = spec.get('description', spec_path)
    cpus        = spec.get('cpus', 4)
    samples     = spec.get('samples', 12)
    interval    = spec.get('interval', 10.0)
    start_time  = spec.get('start_time', 1000.0)
    is_apic       = spec.get('is_apic', 0)
    fail_move_count = spec.get('fail_move_count', 0)
    interrupts    = spec.get('interrupts', [])
    per_sample    = spec.get('per_sample', None)

    init_interrupts(interrupts)

    print(f"# {description}")
    if is_apic:
        print(f"intrd_test:0:config:is_apic\t{is_apic}")
    if fail_move_count > 0:
        print(f"intrd_test:0:config:fail_move_count\t{fail_move_count}")
    print()

    state = make_state(cpus, interrupts)

    for n in range(1, samples + 1):
        if per_sample is not None:
            cur = per_sample(n, dict(
                cpus=cpus, interrupts=[dict(i) for i in interrupts]))
            cur_cpus  = cur.get('cpus', cpus)
            cur_intrs = cur.get('interrupts', interrupts)
        else:
            cur_cpus  = cpus
            cur_intrs = interrupts

        snaptime = start_time + (n - 1) * interval
        gen_sample(n, snaptime, cur_cpus, cur_intrs, state)

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <spec.py>", file=sys.stderr)
        sys.exit(1)
    gen_kstats(sys.argv[1])
