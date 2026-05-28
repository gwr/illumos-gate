
// This file and its contents are supplied under the terms of the
// Common Development and Distribution License ("CDDL"), version 1.0.
// You may only use this file in accordance with the terms of version
// 1.0 of the CDDL.
//
// A full copy of the text of the CDDL should have accompanied this
// source.  A copy of the CDDL is also available via the Internet at
// http://www.illumos.org/license/CDDL.



// intrd-kstat.cc - production kstat interface for intrd
//
// Implements intrd_ks_open(), intrd_ks_close(), and intrd_ks_update() for the
// production intrd binary, reading the three kstat subtrees that
// intrd needs:
//
//   cpu:<id>:sys            - per-CPU time accounting
//   cpu_info:<id>:cpu_info<id> - CPU online/offline state
//   pci_intrs:<inum>:<nexus>   - per-interrupt vector statistics
//
// Functionally equivalent to the Sun::Solaris::Kstat usage in intrd.pl.


#include <sys/types.h>
#include <kstat.h>
#include <string.h>
#include <string>

#include "intrd_sys.h"


// The handle definition is private to this file.
// Consumers in intrd.cc hold a KstatHandle * and call through it.

struct KstatHandle {
	kstat_ctl_t	*kc;
};


// Open the kstat subsystem.
// Returns a handle on success, or NULL with errno set on failure.

KstatHandle *
intrd_ks_open(void)
{
	kstat_ctl_t *kc = kstat_open();
	if (kc == NULL)
		return (NULL);

	KstatHandle *kh = new (std::nothrow) KstatHandle;
	if (kh == NULL) {
		(void) kstat_close(kc);
		return (NULL);
	}
	kh->kc = kc;
	return (kh);
}


// Release all resources associated with the handle.

void
intrd_ks_close(KstatHandle *kh)
{
	if (kh == NULL)
		return;
	(void) kstat_close(kh->kc);
	delete kh;
}


// Find a named field in a KSTAT_TYPE_NAMED kstat by field name.
// Returns a pointer to the kstat_named_t, or NULL if not found.

static kstat_named_t *
named_lookup(kstat_t *kp, const char *field)
{
	kstat_named_t *knp = KSTAT_NAMED_PTR(kp);
	uint_t n;

	for (n = kp->ks_ndata; n > 0; n--, knp++) {
		if (strcmp(knp->name, field) == 0)
			return (knp);
	}
	return (NULL);
}


// Read a uint64 field from a named kstat.
// Returns 0 if the field is not found.

static uint64_t
named_uint64(kstat_t *kp, const char *field)
{
	kstat_named_t *knp = named_lookup(kp, field);
	if (knp == NULL)
		return (0);
	switch (knp->data_type) {
	case KSTAT_DATA_UINT64:
		return (knp->value.ui64);
	case KSTAT_DATA_INT64:
		return ((uint64_t)knp->value.i64);
	case KSTAT_DATA_UINT32:
		return (knp->value.ui32);
	case KSTAT_DATA_INT32:
		return ((uint64_t)knp->value.i32);
	default:
		return (0);
	}
}


// Read a string field from a named kstat.
// Returns an empty string if the field is not found.

static std::string
named_string(kstat_t *kp, const char *field)
{
	kstat_named_t *knp = named_lookup(kp, field);
	if (knp == NULL)
		return ("");
	switch (knp->data_type) {
	case KSTAT_DATA_STRING:
		if (KSTAT_NAMED_STR_PTR(knp) != NULL)
			return (std::string(KSTAT_NAMED_STR_PTR(knp)));
		return ("");
	case KSTAT_DATA_CHAR:
		// May not be NUL-terminated; limit to field width
		return (std::string(knp->value.c,
		    strnlen(knp->value.c, sizeof (knp->value.c))));
	default:
		return ("");
	}
}


// Refresh the kstat chain and return a fresh snapshot.
// Calls kstat_chain_update() to pick up any topology changes (CPU
// online/offline, device add/remove), then walks the three kstat
// subtrees intrd needs.

KSnapshot
intrd_ks_update(KstatHandle *kh)
{
	KSnapshot snap;
	kstat_ctl_t *kc = kh->kc;

	(void) kstat_chain_update(kc);


	// Walk cpu:<id>:sys for each CPU.

	for (kstat_t *kp = kc->kc_chain; kp != NULL; kp = kp->ks_next) {
		if (strcmp(kp->ks_module, "cpu") != 0 ||
		    strcmp(kp->ks_name, "sys") != 0)
			continue;

		int cpuid = kp->ks_instance;
		if (kstat_read(kc, kp, NULL) == -1)
			continue;

		CpuSysKStat cs;
		cs.instance        = cpuid;
		cs.ks_name         = kp->ks_name;
		cs.crtime          = kp->ks_crtime;
		cs.snaptime        = kp->ks_snaptime;
		cs.cpu_nsec_idle   = named_uint64(kp, "cpu_nsec_idle");
		cs.cpu_nsec_user   = named_uint64(kp, "cpu_nsec_user");
		cs.cpu_nsec_kernel = named_uint64(kp, "cpu_nsec_kernel");
		snap.cpu[cpuid]    = cs;
	}


	// Walk cpu_info:<id>:cpu_info<id> for CPU state.

	for (kstat_t *kp = kc->kc_chain; kp != NULL; kp = kp->ks_next) {
		if (strcmp(kp->ks_module, "cpu_info") != 0)
			continue;

		int cpuid = kp->ks_instance;
		if (kstat_read(kc, kp, NULL) == -1)
			continue;

		CpuInfoKStat ci;
		ci.instance        = cpuid;
		ci.ks_name         = kp->ks_name;
		ci.crtime          = kp->ks_crtime;
		ci.snaptime        = kp->ks_snaptime;
		ci.state           = named_string(kp, "state");
		snap.cpu_info[cpuid] = ci;
	}


	// Walk pci_intrs:<inum>:<nexus> for interrupt vectors.

	for (kstat_t *kp = kc->kc_chain; kp != NULL; kp = kp->ks_next) {
		if (strcmp(kp->ks_module, "pci_intrs") != 0)
			continue;
		if (kstat_read(kc, kp, NULL) == -1)
			continue;

		PciIntrKStat pi;
		pi.instance = kp->ks_instance;
		pi.ks_name  = kp->ks_name;
		pi.crtime   = kp->ks_crtime;
		pi.snaptime = kp->ks_snaptime;
		pi.cpu      = (int)named_uint64(kp, "cpu");
		pi.pil      = (int)named_uint64(kp, "pil");
		pi.ino      = (int)named_uint64(kp, "ino");
		pi.time     = named_uint64(kp, "time");
		pi.type     = named_string(kp, "type");
		pi.buspath  = named_string(kp, "buspath");
		pi.name     = named_string(kp, "name");
		snap.pci_intrs.push_back(pi);
	}

	return (snap);
}

void
optional_args(int argc, char **argv)
{
	(void) argc;
	(void) argv;
}
