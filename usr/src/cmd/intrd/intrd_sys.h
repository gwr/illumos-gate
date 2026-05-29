/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * intrd_sys.h - interface between intrd.cc and its system helper modules
 *
 * intrd.cc calls three functions that are implemented differently in the
 * production and test builds:
 *
 *   intrd_ks_update()  - return a fresh kstat snapshot
 *   intr_move()     - move an interrupt to a new CPU (wraps PCITOOL ioctl)
 *   is_apic()      - return non-zero if the bus uses a pcplusmp/APIX APIC
 *
 * Production build links: intrd-kstat.cc + intrd-move.cc
 * Test build links:        intrd-test.cc  (provides all three)
 */

#ifndef _INTRD_SYS_H
#define _INTRD_SYS_H

#include <sys/types.h>
#include <sys/time.h>
#include <stdint.h>

#include <map>
#include <string>
#include <vector>

/* Allow building on non-illumos platforms. */
#ifndef	__sun
typedef int64_t		hrtime_t;
#endif

/*
 * Common kstat identity fields, corresponding to the kstat_t header.
 * ks_module is implicit in which KSnapshot field the entry lives in.
 */
struct KstatBase {
	int		instance;	/* ks_instance */
	std::string	ks_name;	/* ks_name */
	hrtime_t	crtime;		/* ks_crtime */
	hrtime_t	snaptime;	/* ks_snaptime */
};

/*
 * Raw kstat data for one CPU's sys kstat (cpu:<id>:sys).
 * Corresponds to $ks->{cpu}{$id}{sys}{...} in the Perl implementation.
 */
struct CpuSysKStat : KstatBase {
	uint64_t	cpu_nsec_idle;
	uint64_t	cpu_nsec_user;
	uint64_t	cpu_nsec_kernel;
};

/*
 * Raw kstat data for one CPU's cpu_info kstat.
 * Corresponds to $ks->{cpu_info}{$id}{"cpu_info$id"}{state}.
 */
struct CpuInfoKStat : KstatBase {
	std::string	state;		/* "on-line", "off-line", etc. */
};

/*
 * Raw kstat data for one pci_intrs entry.
 * Corresponds to $ks->{pci_intrs}{$inum}{$nexus}{...}.
 */
struct PciIntrKStat : KstatBase {
	int		cpu;
	int		pil;
	int		ino;
	uint64_t	time;		/* cumulative nsec */
	std::string	type;		/* "fixed", "msi", "msix", "disabled" */
	std::string	buspath;
	std::string	name;		/* device name; distinct from ks_name */
};

/*
 * A complete kstat snapshot, as returned by intrd_ks_update().
 * Mirrors the three-level nested hash $ks in the Perl implementation.
 */
struct KSnapshot {
	std::map<int, CpuSysKStat>	cpu;
	std::map<int, CpuInfoKStat>	cpu_info;
	std::vector<PciIntrKStat>	pci_intrs;
};

/*
 * KstatHandle is an opaque type. Its definition lives only in
 * intrd-kstat.cc (production) or intrd-test.cc (test). Consumers
 * hold a pointer to it.
 */
struct KstatHandle;

/*
 * Provided by intrd-kstat.cc (production) or intrd-test.cc (test).
 * intrd_ks_open() initialises the kstat subsystem and returns a handle,
 * or NULL with errno set on failure.
 * intrd_ks_close() releases all resources associated with the handle.
 * intrd_ks_update() reads the current kstat chain and returns a snapshot.
 * In test mode, intrd_ks_update() exits when all snapshots are exhausted.
 */
extern KstatHandle *intrd_ks_open(void);
extern void         intrd_ks_close(KstatHandle *);
extern KSnapshot    intrd_ks_update(KstatHandle *);

/* Defined in intrd-main.cc; set true by the -d command-line option. */
extern bool debug_flag;

/*
 * Provided by intrd-move.cc (production) or intrd-test.cc (test).
 * Move interrupt ino on buspath from oldcpu to newcpu.
 * num_ino > 1 indicates an MSI group move.
 * Returns 0 on success, or -1 with errno set on failure.
 */
extern int intr_move(const char *buspath, int oldcpu, int ino,
    int newcpu, int num_ino);

/*
 * Provided by intrd-move.cc (production) or intrd-test.cc (test).
 * Returns 0 if the bus does not use a pcplusmp/APIX APIC,
 * 1 if it does, or -1 with errno set on failure.
 */
extern int is_apic(const char *buspath);

/* Called from main() after getopt() with remaining non-option arguments. */
extern void optional_args(int argc, char **argv);

#endif /* _INTRD_SYS_H */
