
// This file and its contents are supplied under the terms of the
// Common Development and Distribution License ("CDDL"), version 1.0.
// You may only use this file in accordance with the terms of version
// 1.0 of the CDDL.
//
// A full copy of the text of the CDDL should have accompanied this
// source.  A copy of the CDDL is also available via the Internet at
// http://www.illumos.org/license/CDDL.



// dump-kstats - dump the kstat snapshot intrd would see on this system
//
// Diagnostic tool: opens the kstat subsystem, takes one snapshot of the
// three subtrees intrd reads (cpu:sys, cpu_info, pci_intrs), and prints
// them in kstat(8) -p format to stdout for direct comparison.


#include <stdio.h>
#include <stdlib.h>
#include <err.h>

#include "intrd_sys.h"

#define	NSEC_PER_SEC	1000000000.0

static void
dump_cpu(const KSnapshot &snap)
{
	for (const auto &kv : snap.cpu) {
		const CpuSysKStat &cs = kv.second;
		const char *m = "cpu";
		int i = cs.instance;
		const char *n = cs.ks_name.c_str();

		printf("%s:%d:%s:cpu_nsec_idle\t%llu\n", m, i, n,
		    (unsigned long long)cs.cpu_nsec_idle);
		printf("%s:%d:%s:cpu_nsec_user\t%llu\n", m, i, n,
		    (unsigned long long)cs.cpu_nsec_user);
		printf("%s:%d:%s:cpu_nsec_kernel\t%llu\n", m, i, n,
		    (unsigned long long)cs.cpu_nsec_kernel);
		printf("%s:%d:%s:crtime\t%.4f\n", m, i, n,
		    (double)cs.crtime / NSEC_PER_SEC);
		printf("%s:%d:%s:snaptime\t%.4f\n", m, i, n,
		    (double)cs.snaptime / NSEC_PER_SEC);
	}
}

static void
dump_cpu_info(const KSnapshot &snap)
{
	for (const auto &kv : snap.cpu_info) {
		const CpuInfoKStat &ci = kv.second;
		printf("cpu_info:%d:%s:state\t%s\n",
		    ci.instance, ci.ks_name.c_str(), ci.state.c_str());
	}
}

static void
dump_pci_intrs(const KSnapshot &snap)
{
	for (const PciIntrKStat &pi : snap.pci_intrs) {
		const char *m = "pci_intrs";
		int i = pi.instance;
		const char *n = pi.ks_name.c_str();

		printf("%s:%d:%s:cpu\t%d\n",    m, i, n, pi.cpu);
		printf("%s:%d:%s:type\t%s\n",   m, i, n, pi.type.c_str());
		printf("%s:%d:%s:time\t%llu\n", m, i, n,
		    (unsigned long long)pi.time);
		printf("%s:%d:%s:pil\t%d\n",    m, i, n, pi.pil);
		printf("%s:%d:%s:ino\t%d\n",    m, i, n, pi.ino);
		printf("%s:%d:%s:buspath\t%s\n", m, i, n, pi.buspath.c_str());
		printf("%s:%d:%s:name\t%s\n",   m, i, n, pi.name.c_str());
		printf("%s:%d:%s:crtime\t%.4f\n", m, i, n,
		    (double)pi.crtime / NSEC_PER_SEC);
		printf("%s:%d:%s:snaptime\t%.4f\n", m, i, n,
		    (double)pi.snaptime / NSEC_PER_SEC);
	}
}

int
main(void)
{
	KstatHandle *kh = intrd_ks_open();
	if (kh == NULL)
		err(1, "intrd_ks_open");

	KSnapshot snap = intrd_ks_update(kh);
	intrd_ks_close(kh);

	dump_cpu(snap);
	dump_cpu_info(snap);
	dump_pci_intrs(snap);

	return (0);
}
