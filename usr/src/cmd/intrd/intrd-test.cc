//
// This file and its contents are supplied under the terms of the
// Common Development and Distribution License ("CDDL"), version 1.0.
// You may only use this file in accordance with the terms of version
// 1.0 of the CDDL.
//
// A full copy of the text of the CDDL should have accompanied this
// source.  A copy of the CDDL is also available via the Internet at
// http://www.illumos.org/license/CDDL.
//

//
// Copyright 2026 Gordon W. Ross
//

//
// intrd-test.cc - scenario adapter for the C++ intrd test binary
//
// Provides stubs for intrd_ks_open/close/update(), intr_move(), is_apic(),
// openlog/closelog(), and a syslog() interposer.  Reads test input in
// kstat -p format (see test/README.md).
//
// Usage:  intrd-test [-d] <input.kstats>
//

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include <string>
#include <vector>

#include "intrd_sys.h"

#ifndef	NANOSEC
#define	NANOSEC		1000000000LL
#endif

// Set by optional_args(); used by intrd_ks_open().
static const char	*input_file = nullptr;
static int		 test_is_apic_val = 0;
static int		 test_fail_move_count = 0;

void
optional_args(int argc, char **argv)
{
	if (argc < 1) {
		fprintf(stderr, "Usage: intrd-test [-d] <input.kstats>\n");
		exit(1);
	}
	input_file = argv[0];
}


//
// Parse the input file into a vector of KSnapshot objects.
//
// Lines beginning with "# sample N" delimit snapshots.
// Lines of the form module:instance:name:stat\tvalue are kstat data.
// intrd_test:0:config entries are consumed as config, not kstats.
//

struct KstatHandle {
	std::vector<KSnapshot>	snaps;
	size_t			idx;
};

// Parse a "seconds.fraction" kstat time value into nanoseconds.
// The fractional part must be all zeros (i.e., ".0", ".00", etc.).
static hrtime_t
parse_sec_to_nsec(const char *s)
{
	char *end;
	int64_t sec = strtoll(s, &end, 10);
	if (*end == '.') {
		for (const char *p = end + 1; *p != '\0'; p++) {
			if (*p != '0') {
				fprintf(stderr, "intrd-test: unexpected "
				    "fractional time value: %s\n", s);
				exit(1);
			}
		}
	} else if (*end != '\0') {
		fprintf(stderr, "intrd-test: unexpected time value: %s\n", s);
		exit(1);
	}
	return ((hrtime_t)sec * NANOSEC);
}

KstatHandle *
intrd_ks_open(void)
{
	KstatHandle *kh = new KstatHandle();
	kh->idx = 0;

	FILE *fp = fopen(input_file, "r");
	if (fp == NULL) {
		perror(input_file);
		delete kh;
		return (NULL);
	}

	KSnapshot *snap = nullptr;
	char line[4096];

	while (fgets(line, sizeof (line), fp) != NULL) {
		size_t len = strlen(line);
		if (len > 0 && line[len - 1] == '\n')
			line[len - 1] = '\0';

		if (line[0] == '\0')
			continue;

		// "# sample N" delimiter starts a new snapshot
		if (strncmp(line, "# sample ", 9) == 0) {
			if (snap != nullptr)
				kh->snaps.push_back(*snap);
			if (snap == nullptr)
				snap = new KSnapshot();
			else
				*snap = KSnapshot();
			continue;
		}

		if (line[0] == '#')
			continue;

		// Parse: module:instance:name:stat\tvalue
		char *tab = strchr(line, '\t');
		if (tab == NULL)
			continue;
		*tab = '\0';
		const char *value = tab + 1;

		char module[64], ksname[64], stat[64];
		int instance;
		if (sscanf(line, "%63[^:]:%d:%63[^:]:%63s",
		    module, &instance, ksname, stat) != 4)
			continue;

		if (strcmp(module, "intrd_test") == 0) {
			if (strcmp(stat, "is_apic") == 0)
				test_is_apic_val = atoi(value);
			if (strcmp(stat, "fail_move_count") == 0)
				test_fail_move_count = atoi(value);
			continue;
		}

		if (snap == nullptr)
			snap = new KSnapshot();

		if (strcmp(module, "cpu") == 0 &&
		    strcmp(ksname, "sys") == 0) {
			CpuSysKStat &cs = snap->cpu[instance];
			cs.instance = instance;
			cs.ks_name  = ksname;
			if (strcmp(stat, "cpu_nsec_idle") == 0)
				cs.cpu_nsec_idle = strtoull(value, NULL, 10);
			else if (strcmp(stat, "cpu_nsec_user") == 0)
				cs.cpu_nsec_user = strtoull(value, NULL, 10);
			else if (strcmp(stat, "cpu_nsec_kernel") == 0)
				cs.cpu_nsec_kernel = strtoull(value, NULL, 10);
			else if (strcmp(stat, "crtime") == 0)
				cs.crtime = parse_sec_to_nsec(value);
			else if (strcmp(stat, "snaptime") == 0)
				cs.snaptime = parse_sec_to_nsec(value);

		} else if (strcmp(module, "cpu_info") == 0) {
			CpuInfoKStat &ci = snap->cpu_info[instance];
			ci.instance = instance;
			ci.ks_name  = ksname;
			if (strcmp(stat, "state") == 0)
				ci.state = value;
			else if (strcmp(stat, "crtime") == 0)
				ci.crtime = parse_sec_to_nsec(value);
			else if (strcmp(stat, "snaptime") == 0)
				ci.snaptime = parse_sec_to_nsec(value);

		} else if (strcmp(module, "pci_intrs") == 0) {
			// Find or create the entry for this instance+ksname
			PciIntrKStat *pi = nullptr;
			for (auto &p : snap->pci_intrs) {
				if (p.instance == instance &&
				    p.ks_name == ksname) {
					pi = &p;
					break;
				}
			}
			if (pi == nullptr) {
				snap->pci_intrs.emplace_back();
				pi = &snap->pci_intrs.back();
				pi->instance = instance;
				pi->ks_name  = ksname;
			}
			if (strcmp(stat, "cpu") == 0)
				pi->cpu = atoi(value);
			else if (strcmp(stat, "pil") == 0)
				pi->pil = atoi(value);
			else if (strcmp(stat, "ino") == 0)
				pi->ino = atoi(value);
			else if (strcmp(stat, "time") == 0)
				pi->time = strtoull(value, NULL, 10);
			else if (strcmp(stat, "type") == 0)
				pi->type = value;
			else if (strcmp(stat, "buspath") == 0)
				pi->buspath = value;
			else if (strcmp(stat, "name") == 0)
				pi->name = value;
			else if (strcmp(stat, "crtime") == 0)
				pi->crtime = parse_sec_to_nsec(value);
			else if (strcmp(stat, "snaptime") == 0)
				pi->snaptime = parse_sec_to_nsec(value);
		}
	}

	if (snap != nullptr) {
		kh->snaps.push_back(*snap);
		delete snap;
	}

	fclose(fp);
	return (kh);
}

void
intrd_ks_close(KstatHandle *kh)
{
	delete kh;
}

KSnapshot
intrd_ks_update(KstatHandle *kh)
{
	if (kh->idx >= kh->snaps.size())
		exit(0);
	return (kh->snaps[kh->idx++]);
}

int
intr_move(const char *buspath, int oldcpu, int ino, int newcpu, int num_ino)
{
	printf("intrmove: buspath=%s oldcpu=%d ino=%d newcpu=%d num_ino=%d\n",
	    buspath, oldcpu, ino, newcpu, num_ino);
	if (test_fail_move_count > 0) {
		test_fail_move_count--;
		return (-1);
	}
	return (0);
}

int
is_apic(const char *buspath)
{
	(void) buspath;
	return (test_is_apic_val);
}


//
// syslog() interposer.
//
// Only pass through LOG_DEBUG lines that reflect intrd decisions.
// Suppress dumpdelta noise, separators, and reconfig internals.
// so the output matches the filtering in intrd-test.pl.
//
// However, if running with -d (debug_flag) show everything.
//

void
syslog(int priority, const char *fmt, ...)
{
	if (priority == LOG_DEBUG && !debug_flag) {
		if (strncmp(fmt, "GOODNESS:",            9) != 0 &&
		    strncmp(fmt, "goodness ",            9) != 0 &&
		    strncmp(fmt, "setting new baseline", 20) != 0 &&
		    strncmp(fmt, "evaluating interrupt", 20) != 0 &&
		    strncmp(fmt, "intrd is starting",   17) != 0 &&
		    strncmp(fmt, "sleeptime:",           10) != 0 &&
		    strncmp(fmt, "do_reconfig FAILED",  18) != 0)
			return;
	}

	const char *level;
	switch (priority) {
	case LOG_DEBUG:		level = "debug";   break;
	case LOG_NOTICE:	level = "notice";  break;
	case LOG_WARNING:	level = "warning"; break;
	case LOG_ERR:		level = "err";     break;
	default:		level = "info";    break;
	}

	char buf[4096];
	va_list ap;
	va_start(ap, fmt);
	(void) vsnprintf(buf, sizeof (buf), fmt, ap);
	va_end(ap);

	// intrd-test always runs as a debug build; ensure the startup
	// message always carries the "(debug)" suffix regardless of -d.
	// This lets the output match the *.ref test files.
	if (strcmp(buf, "intrd is starting") == 0)
		(void) strlcat(buf, " (debug)", sizeof (buf));

	// Match Perl output format: "syslog <level> <message>"
	printf("syslog %s %s\n", level, buf);
}

// openlog() and closelog() are no-ops in test mode.

void
openlog(const char *ident, int logopt, int facility)
{
	(void) ident;
	(void) logopt;
	(void) facility;
}

void
closelog(void)
{
}
