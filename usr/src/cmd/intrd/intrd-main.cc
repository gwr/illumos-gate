//
// CDDL HEADER START
//
// The contents of this file are subject to the terms of the
// Common Development and Distribution License (the "License").
// You may not use this file except in compliance with the License.
//
// You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
// or http://www.opensolaris.org/os/licensing.
// See the License for the specific language governing permissions
// and limitations under the License.
//
// When distributing Covered Code, include this CDDL HEADER in each
// file and include the License file at usr/src/OPENSOLARIS.LICENSE.
// If applicable, add the following below this CDDL HEADER, with the
// fields enclosed by brackets "[]" replaced with your own identifying
// information: Portions Copyright [yyyy] [name of copyright owner]
//
// CDDL HEADER END
//

//
// Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
//

//
// intrd-main.cc - interrupt distribution daemon
//
// C++ translation of intrd.pl. Reads per-CPU and per-interrupt kstats,
// computes a "goodness" metric for the current interrupt distribution,
// and moves interrupts between CPUs when the distribution is imbalanced.
//
// The algorithm is unchanged from the Perl implementation. See intrd.pl
// in the git history.  One can actually compare this side-by-side with
// the Perl code.  They are as nearly identical as possible.
//

#include <sys/types.h>
#include <signal.h>
#include <syslog.h>
#include <unistd.h>
#include <err.h>
#include <string.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "intrd_sys.h"

#ifndef	NANOSEC
#define	NANOSEC		1000000000LL
#endif

// Value representation choices
//
// Kstat counter values (interrupt and CPU nanosecond accumulators) are stored
// as uint64_t - they are always non-negative and never used in signed
// arithmetic.  See: IVecStat::time, CpuDelta::tot.
//
// Delta values (differences of kstat counters, bounded by sleep interval) are
// stored as int64_t - they participate in signed goal arithmetic and are
// compared with int64_t quantities.  See: IVecDelta::time, CpuDelta::intrs,
// CpuDelta::bigintr, IVecAlg::time, Delta::avgintrnsec.
//
// Kstat snaptime-derived values (absolute timestamps and durations computed
// from them) are stored as hrtime_t, matching the kstat snaptime type and
// allowing signed arithmetic on differences.  See: Stat::snaptime,
// Delta::minsnap, Delta::maxsnap, deltas_tottime.
//
// Signed time values where the result of arithmetic can go negative are
// stored as int64_t.  See: goal and GoalResult::load in do_reconfig_cpu2cpu()
// and do_find_goal().
//
// Ratios and scores (inherently fractional, typically in [0, 1]) are stored
// as double.  See: CpuDelta::intrload, Delta::avgintrload, Delta::goodness.
//
// In Perl all of these are untyped scalars; the C++ types reflect the
// range and semantics of each value explicitly.


// Per-interrupt-vector data within a Stat.
// ref. intrd.pl:142

struct IVecStat {
	uint64_t	time;
	hrtime_t	crtime;
	int		pil;
	int		ino;
	int		num_ino;
	int		ihs;		// interrupt handler share count
	std::string	buspath;
	std::string	name;
};

// Per-CPU data within a Stat.
// ref. intrd.pl:139

struct CpuStat {
	uint64_t			tot;	// total nsec (idle+user+kern)
	hrtime_t			crtime;
	std::map<std::string, IVecStat>	ivecs;	// cookie -> IVecStat
};

// A complete stat, output of getstat(). snaptime is nanoseconds since boot.
// ref. intrd.pl:138

struct Stat {
	hrtime_t		snaptime;
	std::map<int, CpuStat>	cpus;
};

// Per-interrupt-vector data within a Delta.
// ref. intrd.pl:354

struct IVecDelta {
	int64_t		time;		// delta nsec
	int		pil;
	int		ino;
	int		num_ino;
	int		ihs;
	std::string	buspath;
	std::string	name;
	int		origcpu;	// cpu at start of do_reconfig(); 0 otherwise
	int		nowcpu;		// current cpu during planning; 0 otherwise
};

// Per-CPU data within a Delta.
// ref. intrd.pl:349

struct CpuDelta {
	uint64_t				tot;
	int64_t				intrs;
	int64_t				bigintr;
	double					intrload;
	std::map<std::string, IVecDelta>	ivecs;
};


// A complete delta, output of generate_delta() or compress_deltas().
// minsnap/maxsnap are nanoseconds since boot. goodness is set by the main
// loop after compress_deltas() returns.
// ref. intrd.pl:341

struct Delta {
	int			missing;
	hrtime_t		minsnap;
	hrtime_t		maxsnap;
	double			goodness;
	double			avgintrload;
	int64_t			avgintrnsec;
	std::map<int, CpuDelta>	cpus;
};


// Algorithm state used by find_goal() / do_find_goal().
// IVecAlg is a local, value-typed view of an ivec used during cpu2cpu
// optimisation. It is decoupled from the delta maps so that find_goal()
// can freely set the 'goal' field without touching the live delta, and
// so that subsequent move_intr() calls do not invalidate any pointers.
// ref. intrd.pl:1107

struct IVecAlg {
	std::string	inum;	// cookie, key in delta CpuDelta::ivecs
	int		origcpu;	// cpu at start of do_reconfig_cpu2cpu()
	int		nowcpu;
	int64_t		time;		// delta nsec, copied from IVecDelta::time
	bool		goal;
};

// Perl: $load is an untyped scalar; int64_t because goal arithmetic can go negative
struct GoalResult {
	int64_t			 load;
	std::vector<IVecAlg *>	 goals;
};

// Forward declarations
// ref. intrd.pl:99

static std::optional<Stat> getstat(const KSnapshot &, bool);
static Delta generate_delta(const Stat &, const Stat &);
static std::optional<Delta> compress_deltas(const std::vector<Delta> &);
static void dumpdelta(const Delta &);

static double goodness(const Delta &);
static bool imbalanced(double, double);
static int do_reconfig(Delta &);

static double goodness_cpu(const CpuDelta &, double);
static void move_intr(Delta &, const std::string &, int, int);
static GoalResult do_find_goal(std::vector<IVecAlg> &,
    const std::vector<int64_t> &, int64_t, size_t);
static void find_goal(std::vector<IVecAlg> &, int64_t);
static void do_reconfig_cpu2cpu(Delta &, int, int, double);
static void do_reconfig_cpu(Delta &, std::vector<int> &, int);


// Tunables. Match the Perl defaults.
// ref. intrd.pl:38

static int	normal_sleeptime	= 10;	// time to sleep between samples
static int	idle_sleeptime		= 45;	// time to sleep when idle
static int	onecpu_sleeptime	= (60 * 15); // used if only 1 CPU

static double	idle_intrload		= 0.1;	// idle if interrupt load < 10%
// static double timerange_toohi	= 0.01; // See next

// hrtime_t timerange_toohi_factor: kstat collection must complete
// within 1% of sleeptime.  Stored as ns/s so the check is:
// (maxsnap - minsnap) > sleeptime * timerange_toohi_factor
// i.e. (maxsnap - minsnap) / (sleeptime * NANOSEC) > 1/100
static hrtime_t	timerange_toohi_factor	= NANOSEC / 100;

// time period (in nsec) to keep in deltas
static hrtime_t	statslen = 60LL * NANOSEC;	// 60 sec.

// any goodness over goodness_unsafe_load is considered really bad
// goodness must drop by at least goodness_mindelta for a reconfig
// ref. intrd.pl:660
static double	goodness_unsafe_load	= 0.9;
static double	goodness_mindelta	= 0.1;

static int	sleeptime;	// either normal_ or idle_ or onecpu_
bool		debug_flag = false;

static volatile sig_atomic_t gotsig = 0;

// don't die in the middle of retargeting
static void
sig_handler(int)
{
	gotsig = 1;
}
// ref. intrd.pl:43

static void
set_sleeptime(int t)
{
	if (t == sleeptime)
		return;
	syslog(LOG_DEBUG, "sleeptime: %d -> %d", sleeptime, t);
	sleeptime = t;
}


// intrd_verify: non-fatal assertion. Logs msg to syslog and returns true if
// cond is false (assertion failed). Matches Perl's VERIFY() which returns 1
// on failure, 0 on success.
// Usage: if (intrd_verify(cond, "msg")) { handle_failure; }
// ref. intrd.pl:85

static bool
intrd_verify(bool cond, const char *msg)
{
	if (!cond)
		syslog(LOG_DEBUG, "intrd_verify: %s", msg);
	return (!cond);
}

//
// What follow are the basic data structures routines of intrd.
//
// getstat() is responsible for reading the kstats and generating a "stat" hash.
//
// generate_delta() is responsible for taking two "stat" hashes and creating
// a new "delta" hash that represents what has changed over time.
//
// compress_deltas() is responsible for taking a list of deltas and generating
// a single delta hash that encompasses all the time periods described by the
// deltas.
// ref. intrd.pl:117
//

//
// getstat() is handed a reference to a kstat and generates a hash, returned
// by reference, containing all the fields from the kstats which we need.
// If it returns the scalar 0, it failed to gather the kstats, and the caller
// should react accordingly.
//
// getstat() is also responsible for maintaining a reasonable sleeptime.
//
// {"snaptime"}          kstat's snaptime
// {<cpuid>}             one hash reference per online cpu
//  ->{"tot"}            == cpu:<cpuid>:sys:cpu_nsec_{user + kernel + idle}
//  ->{"crtime"}         == cpu:<cpuid>:sys:crtime
//  ->{"ivecs"}
//     ->{<cookie#>}     iterates over pci_intrs::<nexus>:cookie
//        ->{"time"}     == pci_intrs:<ivec#>:<nexus>:time (in nsec)
//        ->{"pil"}      == pci_intrs:<ivec#>:<nexus>:pil
//        ->{"crtime"}   == pci_intrs:<ivec#>:<nexus>:crtime
//        ->{"ino"}      == pci_intrs:<ivec#>:<nexus>:ino
//        ->{"num_ino"}  == num inos of single device instance sharing this entry
//			Will be > 1 on pcplusmp X86 systems for devices
//			with multiple MSI interrupts.
//        ->{"buspath"}  == pci_intrs:<ivec#>:<nexus>:buspath
//        ->{"name"}     == pci_intrs:<ivec#>:<nexus>:name
//        ->{"ihs"}      == pci_intrs:<ivec#>:<nexus>:ihs
// ref. intrd.pl:130
//

static std::optional<Stat>
getstat(const KSnapshot &ks, bool pcplusmp_sys)
{
	Stat stat;
	int cpucnt = 0;

	// Map of map which matches (MSI device, ino) combos to kstats.
	// ( name -> ino -> ivec* )
	std::map<std::string, std::map<int, IVecStat *>> msidevs;

	// kstats are not generated atomically. Each kstat hierarchy will
	// have been generated within the kernel at a different time. On a
	// thrashing system, we may not run quickly enough in order to get
	// coherent kstat timing information across all the kstats. To
	// determine if this is occurring, minsnap/maxsnap are used to
	// find the breadth between the first and last snaptime of all the
	// kstats we access. maxsnap - minsnap roughly represents the
	// total time taken up in getstat(). If this time approaches the
	// time between snapshots, our results may not be useful.

	hrtime_t minsnap = 0;
	hrtime_t maxsnap = 0;

	// Iterate over the cpus in cpu:<cpuid>::. Check
	// cpu_info:<cpuid>:cpu_info<cpuid>:state to make sure the
	// processor is "on-line". If not, it isn't accepting interrupts
	// and doesn't concern us.
	//
	// Record cpu:<cpuid>:sys:snaptime, and check minsnap/maxsnap.

	for (const auto &[cpuid, cs] : ks.cpu) {
		auto ci = ks.cpu_info.find(cpuid);
		if (ci == ks.cpu_info.end())
			continue;
		if (ci->second.state != "on-line")
			continue;

		CpuStat &cpustat = stat.cpus[cpuid];
		cpustat.tot = cs.cpu_nsec_idle + cs.cpu_nsec_user +
		    cs.cpu_nsec_kernel;
		cpustat.crtime = cs.crtime;

		hrtime_t snap = cs.snaptime;
		if (minsnap == 0 || snap < minsnap)
			minsnap = snap;
		if (snap > maxsnap)
			maxsnap = snap;
		cpucnt++;
	}

	if (cpucnt <= 1) {
		set_sleeptime(onecpu_sleeptime);
		return (std::nullopt);
	}

	// Iterate over the ivecs. If the cpu is not on-line, ignore the
	// ivecs mapped to it, if any.
	//
	// Record pci_intrs:{inum}:<nexus>:time, snaptime, crtime, pil,
	// ino, name, and buspath. Check minsnap/maxsnap.

	for (const PciIntrKStat &pi : ks.pci_intrs) {
		int cpu = pi.cpu;
		auto cpuit = stat.cpus.find(cpu);
		if (cpuit == stat.cpus.end())
			continue;
		if (pi.type == "disabled")
			continue;

		hrtime_t snap = pi.snaptime;
		if (snap < minsnap)
			minsnap = snap;
		else if (snap > maxsnap)
			maxsnap = snap;

		std::string cookie = pi.buspath + " " +
		    std::to_string(pi.ino);

		CpuStat &cpustat = cpuit->second;
		auto it = cpustat.ivecs.find(cookie);
		if (it != cpustat.ivecs.end()) {
			IVecStat &existing = it->second;
			existing.time += pi.time;
			existing.name += "/" + pi.name;

			// If this new interrupt sharing cookie represents a
			// change from an earlier getstat, make sure that
			// generate_delta will see the change by setting
			// crtime to the most recent crtime of its components.

			if (pi.crtime > existing.crtime)
				existing.crtime = pi.crtime;
			existing.ihs++;
			continue;
		}

		IVecStat &ivec = cpustat.ivecs[cookie];
		ivec.time    = pi.time;
		ivec.crtime  = pi.crtime;
		ivec.pil     = pi.pil;
		ivec.ino     = pi.ino;
		ivec.num_ino = 1;
		ivec.ihs     = 1;
		ivec.buspath = pi.buspath;
		ivec.name    = pi.name;

		if (pcplusmp_sys && pi.type == "msi")
			msidevs[pi.name][pi.ino] = &ivec;
	}

	// All MSI interrupts of a device instance share a single MSI address.
	// On X86 systems with an APIC, this MSI address is interpreted as CPU
	// routing info by the APIC.  For this reason, on these platforms, all
	// interrupts for MSI devices must be moved to the same CPU at the same
	// time.
	//
	// Since all interrupts will be on the same CPU on these platforms, all
	// interrupts can be consolidated into one ivec entry.  For such devices,
	// num_ino will be > 1 to denote that a group move is needed.  

	// Loop thru all MSI devices on X86 pcplusmp systems.
	// Nop on other systems.

	for (auto &[devname, inomap] : msidevs) {

		// Loop thru inos of the device, sorted by lowest value first
		// For each cookie found for a device, incr num_ino for the
		// lowest cookie and remove other cookies.

		// Assumes PIL is the same for first and current cookies

		int first_ino = -1;
		IVecStat *first_ivec = nullptr;

		for (auto &[ino, ivec] : inomap) {
			if (first_ino < 0) {
				first_ino = ino;
				first_ivec = ivec;
				continue;
			}
			first_ivec->num_ino++;
			first_ivec->time += ivec->time;
			if (ivec->crtime > first_ivec->crtime)
				first_ivec->crtime = ivec->crtime;
			// Invalidate this cookie, less complicated and
			// more efficient than deleting it.
			ivec->num_ino = 0;
		}
	}

	// We define the timerange as the amount of time spent gathering the
	// various kstats, divided by our sleeptime. If we take a lot of time
	// to access the kstats, and then we create a delta comparing these
	// kstats with a prior set of kstats, that delta will cover
	// substantially different amount of time depending upon which
	// interrupt or CPU is being examined.
	//
	// By checking the timerange here, we guarantee that any deltas
	// created from these kstats will contain self-consistent data,
	// in that all CPUs and interrupts cover a similar span of time.
	//
	// timerange_toohi is the upper bound. Any timerange above
	// this is thrown out as garbage. If the stat is safely within this
	// bound, we treat the stat as representing an instant in time, rather
	// than the time range it actually spans. We arbitrarily choose minsnap
	// as the snaptime of the stat.
	//
	// The limit is (sleeptime * timerange_toohi_factor), which enforces
	// that kstat collection completes within 1% of sleeptime.  See the
	// definition of timerange_toohi_factor for the derivation.

	stat.snaptime = minsnap;
	if ((maxsnap - minsnap) > (sleeptime * timerange_toohi_factor))
		return (std::nullopt);

	return (stat);
}

//
// dumpdelta takes a reference to our "delta" structure:
// {"missing"}           "1" if the delta's component stats had inconsistencies
// {"minsnap"}           time of the first kstat snaptime used in this delta
// {"maxsnap"}           time of the last kstat snaptime used in this delta
// {"goodness"}          cost function applied to this delta
// {"avgintrload"}       avg of interrupt load across cpus, as a percentage
// {"avgintrnsec"}       avg number of nsec spent in interrupts, per cpu
// {<cpuid>}             iterates over on-line cpus
//  ->{"intrs"}          cpu's movable intr time (sum of "time" for each ivec)
//  ->{"tot"}            CPU load from all sources in nsec
//  ->{"bigintr"}        largest value of {ivecs}{<ivec#>}{time} from below
//  ->{"intrload"}       intrs / tot
//  ->{"ivecs"}
//     ->{<ivec#>}       iterates over ivecs for this cpu
//        ->{"time"}     time used by this interrupt (in nsec)
//        ->{"pil"}      pil level of this interrupt
//        ->{"ino"}      interrupt number (or base vector if MSI group)
//        ->{"buspath"}  filename of the directory of the device's bus
//        ->{"name"}     device name
//        ->{"ihs"}      number of different handlers sharing this ino
//        ->{"num_ino"}  number of interrupt vectors in MSI group
//
// It prints out the delta structure in a nice, human readable display.
// ref. intrd.pl:341
//

static void
dumpdelta(const Delta &delta)
{
	// print global info

	syslog(LOG_DEBUG, "dumpdelta:");
	if (delta.missing > 0)
		syslog(LOG_DEBUG, " RECONFIGURATION IN DELTA");
	syslog(LOG_DEBUG, " avgintrload: %5.2f%%  avgintrnsec: %lld",
	    delta.avgintrload * 100.0, (long long)delta.avgintrnsec);
	if (delta.goodness >= 0.0)
		syslog(LOG_DEBUG, "    goodness: %5.2f%%",
		    delta.goodness * 100.0);

	// iterate over cpus

	for (const auto &[cpu, cpst] : delta.cpus) {
		syslog(LOG_DEBUG, "    cpu %3d intr %7.3f%%  "
		    "(bigintr %7.3f%%)",
		    cpu, cpst.intrload * 100.0,
		    (cpst.tot > 0 ?
		    (double)cpst.bigintr * 100.0 / (double)cpst.tot : 0.0));
		syslog(LOG_DEBUG, "        intrs %lld, bigintr %lld",
		    (long long)cpst.intrs,
		    (long long)cpst.bigintr);

		for (const auto &[ivec_key, ivst] : cpst.ivecs) {
			syslog(LOG_DEBUG, "    %15s:\"%s\": %7.3f%%  %llu",
			    (ivst.ihs > 1 ?
			    (ivst.name + "(" +
			    std::to_string(ivst.ihs) + ")").c_str() :
			    ivst.name.c_str()),
			    ivec_key.c_str(),
			    (cpst.tot > 0 ?
			    (double)ivst.time * 100.0 / (double)cpst.tot : 0.0),
			    (unsigned long long)ivst.time);
		}
	}
}

//
// generate_delta(stat, newstat) takes two stat references, returned from
// getstat(), and creates a %delta. %delta (not surprisingly) contains the
// same basic info as stat and newstat, but with the timestamps as deltas
// instead of absolute times. We return a reference to the delta.
// ref. intrd.pl:401
//

static Delta
generate_delta(const Stat &stat, const Stat &newstat)
{
	Delta delta;
	double intrload = 0.0;
	int64_t intrnsec = 0;
	int cpus = 0;

	delta.avgintrload = 0.0;
	delta.avgintrnsec = 0;
	delta.goodness    = -1.0;	// not yet computed

	// Take the worstcase timerange
	delta.minsnap = stat.snaptime;
	delta.maxsnap = newstat.snaptime;
	if (intrd_verify(delta.maxsnap > delta.minsnap,
	    "generate_delta: stats aren't ascending")) {
		delta.missing = 1;
		return (delta);
	}

	// if there are a different number of cpus in the stats, set missing

	delta.missing = (stat.cpus.size() != newstat.cpus.size()) ? 1 : 0;
	if (intrd_verify(delta.missing == 0,
	    "generate_delta: number of CPUs changed")) {
		return (delta);
	}

	// scan through every cpu in %newstat and compare against %stat

	for (const auto &[cpu, newcpst] : newstat.cpus) {
		auto oldit = stat.cpus.find(cpu);

		// If %stat is missing a cpu from %newstat, then it was just
		// onlined. Mark missing.

		if (intrd_verify(oldit != stat.cpus.end() &&
		    oldit->second.crtime == newcpst.crtime,
		    "generate_delta: cpu changed")) {
			delta.missing = 1;
			return (delta);
		}
		const CpuStat &cpst = oldit->second;

		CpuDelta &dcpu = delta.cpus[cpu];
		if (newcpst.tot >= cpst.tot)
			dcpu.tot = newcpst.tot - cpst.tot;
		else
			dcpu.tot = 0;

		if (intrd_verify(newcpst.tot >= cpst.tot,
		    "generate_delta: deltas are not ascending?")) {
			delta.missing = 1;
			delta.cpus.erase(cpu);
			return (delta);
		}

		// Avoid remote chance of division by zero
		if (dcpu.tot == 0)
			dcpu.tot = 1;

		dcpu.intrs    = 0;
		dcpu.bigintr  = 0;
		dcpu.intrload = 0.0;

		// if the number of ivecs differs, set missing

		if (intrd_verify(cpst.ivecs.size() == newcpst.ivecs.size(),
		    "generate_delta: cpu has more/less interrupts")) {
			delta.missing = 1;
			return (delta);
		}

		for (const auto &[inum, newivec] : newcpst.ivecs) {

			// Unused cookie, corresponding to an MSI vector which
			// is part of a group.  The whole group is accounted for
			// by a different cookie.
			if (newivec.num_ino == 0)
				continue; // MSI group member, skip

			// If this ivec doesn't exist in stat, or if stat
			// shows a different crtime, set missing.
			auto oivit = cpst.ivecs.find(inum);
			if (intrd_verify(oivit != cpst.ivecs.end() &&
			    oivit->second.crtime == newivec.crtime,
			    "generate_delta: ivec changed")) {
				delta.missing = 1;
				return (delta);
			}
			const IVecStat &ivec = oivit->second;

			// Create delta{cpu}{ivecs}{inum}.
			IVecDelta &divec = dcpu.ivecs[inum];

			// calculate time used by this interrupt

			int64_t t = (newivec.time >= ivec.time) ?
			    (newivec.time - ivec.time) : 0;
			if (intrd_verify(newivec.time >= ivec.time,
			    "generate_delta: ivec went backwards?")) {
				delta.missing = 1;
				dcpu.ivecs.erase(inum);
				return (delta);
			}

			dcpu.intrs += t;
			divec.time = t;
			if (t > dcpu.bigintr)
				dcpu.bigintr = t;

			// Transfer over basic info about the kstat. We
			// don't have to worry about discrepancies between
			// ivec and newivec because we verified that both
			// have the same crtime.

			divec.pil     = newivec.pil;
			divec.ino     = newivec.ino;
			divec.buspath = newivec.buspath;
			divec.name    = newivec.name;
			divec.ihs     = newivec.ihs;
			divec.num_ino = newivec.num_ino;
		}

		if (dcpu.tot < dcpu.intrs) {
			// Ewww! Hopefully just a rounding error.
			// Make something up.
			dcpu.tot = dcpu.intrs;
		}

		dcpu.intrload = (double)dcpu.intrs / (double)dcpu.tot;
		intrload += dcpu.intrload;
		intrnsec += dcpu.intrs;
		cpus++;
	}

	if (cpus > 0) {
		delta.avgintrload = intrload / (double)cpus;
		delta.avgintrnsec = intrnsec / cpus;
	}
	return (delta);
}

// compress_delta takes a list of deltas, and returns a single new delta
// which represents the combined information from all the deltas. The deltas
// provided are assumed to be sequential in time. The resulting compressed
// delta looks just like any other delta. This new delta is also more accurate
// since its statistics are averaged over a longer period than any of the
// original deltas.
// ref. intrd.pl:551

static std::optional<Delta>
compress_deltas(const std::vector<Delta> &deltas)
{
	Delta newdelta;
	int64_t intrs = 0;
	double tot   = 0.0;
	int    cpus  = 0;
	double high_intrload = 0.0;

	if (intrd_verify(!deltas.empty(), "compress_deltas: list of deltas is empty"))
		return (std::nullopt);

	newdelta.minsnap = deltas.front().minsnap;
	newdelta.maxsnap = deltas.back().maxsnap;
	newdelta.missing = 0;
	newdelta.goodness = -1.0;

	for (const Delta &delta : deltas) {
		if (intrd_verify(delta.missing == 0, "compressing bad deltas"))
			return (std::nullopt);

		for (const auto &[cpuid, cpu] : delta.cpus) {
			intrs += cpu.intrs;
			tot   += (double)cpu.tot;

			CpuDelta &nc = newdelta.cpus[cpuid];
			nc.intrs += cpu.intrs;
			nc.tot   += cpu.tot;

			for (const auto &[inum, ivec] : cpu.ivecs) {
				IVecDelta &ni = nc.ivecs[inum];
				ni.time    += ivec.time;
				ni.pil      = ivec.pil;
				ni.ino      = ivec.ino;
				ni.buspath  = ivec.buspath;
				ni.name     = ivec.name;
				ni.ihs      = ivec.ihs;
				ni.num_ino  = ivec.num_ino;
			}
		}
	}

	for (auto &[cpuid, cpu] : newdelta.cpus) {
		cpus++;

		cpu.bigintr = 0;
		for (const auto &[inum, ivec] : cpu.ivecs) {
			if (ivec.time > cpu.bigintr)
				cpu.bigintr = ivec.time;
		}

		if (cpu.tot <= 0)
			cpu.tot = 1;
		cpu.intrload = (double)cpu.intrs / (double)cpu.tot;
		if (cpu.intrload > high_intrload)
			high_intrload = cpu.intrload;
	}

	if (cpus == 0) {
		newdelta.avgintrnsec = 0;
		newdelta.avgintrload = 0.0;
	} else {
		newdelta.avgintrnsec = intrs / cpus;
		newdelta.avgintrload = (tot > 0.0) ? ((double)intrs / tot) : 0.0;
	}

	set_sleeptime((high_intrload < idle_intrload) ?
	    idle_sleeptime : normal_sleeptime);

	return (newdelta);
}


// What follow are the core functions responsible for examining the deltas
// generated above and deciding what to do about them.
//
// goodness() and its helper goodness_cpu() return a heuristic which describe
// how good (or bad) the current interrupt balance is. The value returned will
// be between 0 and 1, with 0 representing maximum goodness, and 1 representing
// maximum badness.
//
// imbalanced() compares a current and historical value of goodness, and
// determines if there has been enough change to warrant evaluating a
// reconfiguration of the interrupts
//
// do_reconfig(), and its helpers, do_reconfig_cpu(), do_reconfig_cpu2cpu(),
// find_goal(), do_find_goal(), and move_intr(), are responsible for examining
// a delta and determining the best possible assignment of interrupts to CPUs.
//
// It is important that do_reconfig() be in alignment with goodness(). If
// do_reconfig were to generate a new interrupt distribution that worsened
// goodness, we could get into a pathological loop with intrd fighting itself,
// constantly deciding that things are imbalanced, and then changing things
// only to make them worse.
// ref. intrd.pl:636

// goodness(%delta) examines a delta and return its "goodness". goodness will
// be between 0 (best) and 1 (major bad). goodness is determined by evaluating
// the goodness of each individual cpu, and returning the worst case. This
// helps on systems with many CPUs, where otherwise a single pathological CPU
// might otherwise be ignored because the average was OK.
//
// To calculate the goodness of an individual CPU, we start by looking at its
// load due to interrupts. If the load is above a certain high threshold and
// there is more than one interrupt assigned to this CPU, we set goodness
// to worst-case. If the load is below the average interrupt load of all CPUs,
// then we return best-case, since what's to complain about?
//
// Otherwise we look at how much the load is above the average, and return
// that as the goodness, with one caveat: we never return more than the CPU's
// interrupt load ignoring its largest single interrupt source. This is
// because a CPU with one high-load interrupt, and no other interrupts, is
// perfectly balanced. Nothing can be done to improve the situation, and thus
// it is perfectly balanced even if the interrupt's load is 100%.
// ref. intrd.pl:666

static double
goodness(const Delta &delta)
{
	if (delta.missing > 0)
		return (1.0);

	double high = 0.0;

	for (const auto &[cpu, cpst] : delta.cpus) {
		double g = goodness_cpu(cpst, delta.avgintrload);
		if (intrd_verify(g >= 0.0 && g <= 1.0,
		    "goodness: cpu goodness out of range")) {
			return (1.0);
		}
		if (g == 1.0)
			return (1.0);
		if (g > high)
			high = g;
	}
	return (high);
}
// ref. intrd.pl:713

static double
goodness_cpu(const CpuDelta &cpu, double avgintrload)
{
	double load = (double)cpu.intrs / (double)cpu.tot;

	if (load < avgintrload)
		return (0.0);

	// Calculate load_no_bigintr, which represents the load
	// due to interrupts, excluding the one biggest interrupt.
	// This is the most gain we can get on this CPU from
	// offloading interrupts.

	double load_no_bigintr =
	    (double)(cpu.intrs - cpu.bigintr) / (double)cpu.tot;

	// A major imbalance is indicated if a CPU is saturated
	// with interrupt handling, and it has more than one
	// source of interrupts. Those other interrupts could be
	// starved if of a lower pil. Return a goodness of 1,
	// which is the worst possible return value,
	// which will effectively contaminate this entire delta.

	if (load > goodness_unsafe_load && (int)cpu.ivecs.size() > 1)
		return (1.0);

	double g = load - avgintrload;
	if (g > load_no_bigintr)
		g = load_no_bigintr;
	return (g);
}

// imbalanced() is used by the main routine to determine if the goodness
// has shifted far enough from our last baseline to warrant a reassignment
// of interrupts. A very high goodness indicates that a CPU is way out of
// whack. If the goodness has varied too much since the baseline, then
// perhaps a reconfiguration is worth considering.
// ref. intrd.pl:749

static bool
imbalanced(double g, double baseline)
{
	// Return 1 if we are pathological, or creeping away from the baseline
	if (g > 0.50)
		return (true);
	if (std::fabs(g - baseline) > goodness_mindelta)
		return (true);
	return (false);
}

// move_intr(\%delta, inum, oldcpu, newcpu)
// used by reconfiguration code to move an interrupt between cpus within
// a delta. This manipulates data structures, and does not actually move
// the interrupt on the running system.
// ref. intrd.pl:790

static void
move_intr(Delta &delta, const std::string &inum, int oldcpuid, int newcpuid)
{
	CpuDelta &oldcpu = delta.cpus.at(oldcpuid);
	IVecDelta ivec = oldcpu.ivecs.at(inum); // copy before erase

	// Remove ivec from old cpu

	(void) intrd_verify(oldcpu.intrs >= ivec.time,
	    "move_intr: intr's time > total time?");
	(void) intrd_verify(ivec.time <= oldcpu.bigintr,
	    "move_intr: intr's time > bigintr?");

	oldcpu.intrs -= ivec.time;
	oldcpu.intrload = (double)oldcpu.intrs / (double)oldcpu.tot;
	oldcpu.ivecs.erase(inum);

	if (ivec.time >= oldcpu.bigintr) {
		int64_t bigtime = 0;
		for (const auto &[k, v] : oldcpu.ivecs) {
			if (v.time > bigtime)
				bigtime = v.time;
		}
		oldcpu.bigintr = bigtime;
	}

	// Add ivec onto new cpu

	CpuDelta &newcpu = delta.cpus.at(newcpuid);

	ivec.nowcpu = newcpuid;
	newcpu.intrs += ivec.time;
	newcpu.intrload = (double)newcpu.intrs / (double)newcpu.tot;
	newcpu.ivecs[inum] = ivec;

	if (ivec.time > newcpu.bigintr)
		newcpu.bigintr = ivec.time;
}

static void
move_intr_check(const Delta &delta, int oldcpuid, int newcpuid)
{
	const CpuDelta &oc = delta.cpus.at(oldcpuid);
	const CpuDelta &nc = delta.cpus.at(newcpuid);
	(void) intrd_verify(oc.tot >= oc.intrs,
	    "Moved interrupts left 100+% load on src cpu");
	(void) intrd_verify(nc.tot >= nc.intrs,
	    "Moved interrupts left 100+% load on tgt cpu");
}

// do_reconfig(), do_reconfig_cpu(), and do_reconfig_cpu2cpu(), are the
// decision-making functions responsible for generating a new interrupt
// distribution. They are designed with the definition of goodness() in
// mind, i.e. they use the same definition of "good distribution" as does
// goodness().
//
// do_reconfig() is responsible for deciding whether a redistribution is
// actually warranted. If the goodness is already pretty good, it doesn't
// waste the CPU time to generate a new distribution. If it
// calculates a new distribution and finds that it is not sufficiently
// improved from the prior distribution, it will not do the redistribution,
// mainly to avoid the disruption to system performance caused by
// rejuggling interrupts.
//
// Its main loop works by going through a list of cpus sorted from
// highest to lowest interrupt load. It removes the highest-load cpus
// one at a time and hands them off to do_reconfig_cpu(). This function
// then re-sorts the remaining CPUs from lowest to highest interrupt load,
// and one at a time attempts to rejuggle interrupts between the original
// high-load CPU and the low-load CPU. Rejuggling on a high-load CPU is
// considered finished as soon as its interrupt load is within
// goodness_mindelta of the average interrupt load. Such a CPU will have
// a goodness of below the goodness_mindelta threshold.
//
// Returns:
//   0  current config is already optimal (or close enough)
//   1  reconfiguration occurred
//  -1  failure
// ref. intrd.pl:766

static int
do_reconfig(Delta &delta)
{
	double g = delta.goodness;

	// We can't improve goodness to better than 0. We should stop here
	// if, even if we achieve a goodness of 0, the improvement is still
	// too small to merit the action.

	if (g - 0.0 < goodness_mindelta) {
		syslog(LOG_DEBUG, "goodness good enough, don't reconfig");
		return (0);
	}

	syslog(LOG_NOTICE, "Optimizing interrupt assignments");

	if (intrd_verify(delta.missing == 0,
	    "RECONFIG Aborted: should not have a delta with missing")) {
		return (-1);
	}

	// Make a list of all cpuids, and also add some extra information
	// to the ivec structures.

	std::vector<int> cpusortlist;

	for (auto &[cpuid, cpu] : delta.cpus) {
		cpusortlist.push_back(cpuid);
		for (auto &[inum, ivec] : cpu.ivecs) {
			ivec.origcpu = cpuid;
			ivec.nowcpu  = cpuid;
		}
	}

	// Sort the list of CPUs from highest to lowest interrupt load.
	// Remove the top CPU from that list and attempt to redistribute
	// its interrupts. If the CPU has a goodness below a threshold,
	// just ignore the CPU and move to the next one. If the CPU's
	// load falls below the average load plus that same threshold,
	// then there are no CPUs left worth reconfiguring, and we're done.

	while (!cpusortlist.empty()) {
		// Re-sort cpusortlist each time, since do_reconfig_cpu can
		// move interrupts around.

		std::sort(cpusortlist.begin(), cpusortlist.end(),
		    [&delta](int a, int b) {
			    double la = delta.cpus.at(a).intrload;
			    double lb = delta.cpus.at(b).intrload;
			    if (la != lb)
				    return (lb < la); // descending
			    return (a < b);
		    });

		int cpu = cpusortlist.front();
		cpusortlist.erase(cpusortlist.begin());

		if ((delta.cpus.at(cpu).intrload <= goodness_unsafe_load) &&
		    (delta.cpus.at(cpu).intrload <=
		    delta.avgintrload + goodness_mindelta)) {
			syslog(LOG_DEBUG, "finished reconfig: "
			    "cpu %d load %g avgload %g",
			    cpu, delta.cpus.at(cpu).intrload,
			    delta.avgintrload);
			break;
		}
		// XXX Why open a block here?
		{
			const CpuDelta &dc = delta.cpus.at(cpu);
			double gc = goodness_cpu(dc, delta.avgintrload);
			syslog(LOG_DEBUG,
			    "do_reconfig outer: cpu=%d gc=%a mindelta=%a skip=%d",
			    cpu, gc, goodness_mindelta, (gc < goodness_mindelta));
			if (gc < goodness_mindelta)
				continue;
		}
		do_reconfig_cpu(delta, cpusortlist, cpu);
	}

	// How good a job did we do? If the improvement was minimal, and
	// our goodness wasn't pathological (and thus needing any help it
	// can get), then don't bother moving the interrupts.

	double newgoodness = goodness(delta);
	(void) intrd_verify(newgoodness <= g,
	    "reconfig: result has worse goodness?");

	if ((g != 1.0 || newgoodness == 1.0) &&
	    g - newgoodness < goodness_mindelta) {
		syslog(LOG_DEBUG,
		    "goodness already near optimum, don't reconfig");
		return (0);
	}
	syslog(LOG_DEBUG, "goodness %5.2f%% --> %5.2f%%",
	    g * 100.0, newgoodness * 100.0);

	// Time to move those interrupts!
	//
	// After planning, each ivec lives under its planned destination CPU.
	// origcpu still names the pre-planning source, so origcpu == cpuid
	// means this interrupt was not moved.

	int ret = 1;
	bool warned = false;

	for (auto &[cpuid, cpu] : delta.cpus) {
		for (auto &[inum, ivec] : cpu.ivecs) {
			if (ivec.origcpu == cpuid)
				continue; // did not move

			if (intr_move(ivec.buspath.c_str(), ivec.origcpu,
			    ivec.ino, cpuid, ivec.num_ino) != 0) {
				if (!warned) {
					syslog(LOG_WARNING,
					    "Unable to move interrupts");
					warned = true;
				}
				syslog(LOG_DEBUG,
				    "Unable to move buspath %s ino %d "
				    "to cpu %d",
				    ivec.buspath.c_str(), ivec.ino, cpuid);
				ret = -1;
			}
		}
	}

	syslog(LOG_NOTICE, "Interrupt assignments optimized");
	return (ret);
}

// We have been asked to rejuggle interrupts between oldcpuid and
// other CPUs found on cpusortlist so as to improve the load on
// oldcpuid. We reverse cpusortlist to get our own copy of the
// list, sorted from lowest to highest interrupt load. One at a
// time, shift a CPU off of this list of CPUs, and attempt to
// rejuggle interrupts between the two CPUs. Don't do this if the
// other CPU has a higher load than oldcpuid. We're done rejuggling
// once oldcpuid's goodness falls below a threshold.
// ref. intrd.pl:969

static void
do_reconfig_cpu(Delta &delta, std::vector<int> &cpusortlist, int oldcpuid)
{
	syslog(LOG_DEBUG, "reconfiguring %d", oldcpuid);

	// cpu aliases delta.cpus[oldcpuid]: gc sees the updated intrload
	// after each do_reconfig_cpu2cpu() has moved interrupts away.
	CpuDelta &cpu = delta.cpus.at(oldcpuid);
	double avgintrload = delta.avgintrload;

	// Work from lowest to highest load on the target side.
	std::vector<int> cputargetlist(cpusortlist.rbegin(),
	    cpusortlist.rend());

	while (!cputargetlist.empty()) {
		double gc = goodness_cpu(cpu, avgintrload);
		syslog(LOG_DEBUG,
		    "do_reconfig_cpu inner: cpu=%d gc=%a mindelta=%a skip=%d",
		    oldcpuid, gc, goodness_mindelta, (gc < goodness_mindelta));
		if (gc < goodness_mindelta)
			break;

		int tgtcpuid = cputargetlist.front();
		cputargetlist.erase(cputargetlist.begin());

		double load    = cpu.intrload;
		double tgtload = delta.cpus.at(tgtcpuid).intrload;
		if (tgtload > load)
			break;

		do_reconfig_cpu2cpu(delta, oldcpuid, tgtcpuid, load);
	}
}

// We've been asked to consider interrupt juggling between srccpuid
// (with a high interrupt load) and tgtcpuid (with a lower interrupt
// load). First, make a single list with all of the ivecs from both
// CPUs, and sort the list from highest to lowest load.
// ref. intrd.pl:1000

static void
do_reconfig_cpu2cpu(Delta &delta, int srccpuid, int tgtcpuid, double srcload)
{
	syslog(LOG_DEBUG, "exchanging intrs between %d and %d",
	    srccpuid, tgtcpuid);

	// Gather together all the ivecs and sort by load

	std::vector<IVecAlg> ivecs;
	for (auto &[inum, ivec] : delta.cpus.at(srccpuid).ivecs)
		ivecs.push_back({inum, ivec.origcpu, srccpuid, ivec.time, false});
	for (auto &[inum, ivec] : delta.cpus.at(tgtcpuid).ivecs)
		ivecs.push_back({inum, ivec.origcpu, tgtcpuid, ivec.time, false});

	if (ivecs.empty())
		return;

	std::sort(ivecs.begin(), ivecs.end(),
	    [](const IVecAlg &a, const IVecAlg &b) {
		    return (b.time < a.time); // descending
	    });

	// Our "goal" load for srccpuid is the average load across all CPUs.
	// find_goal() will determine the optimum selection of the
	// available interrupts which comes closest to this goal without
	// falling below the goal.

	// Perl: $goal and $avgnsec are untyped scalars; int64_t because goal can go negative
	int64_t goal = delta.avgintrnsec;

	// We know that the interrupt load on tgtcpuid is less than that on
	// srccpuid, but its load could still be above avgintrnsec. Don't
	// choose a goal which would bring srccpuid below the load on tgtcpuid.

	int64_t avgnsec =
	    (delta.cpus.at(srccpuid).intrs +
	    delta.cpus.at(tgtcpuid).intrs) / 2;
	if (goal < avgnsec)
		goal = avgnsec;

	// If the largest of the interrupts is on srccpuid, leave it there.
	// This can help minimize the disruption caused by moving interrupts.

	if (ivecs[0].origcpu == srccpuid) {
		syslog(LOG_DEBUG, "Keeping %s on %d",
		    ivecs[0].inum.c_str(), srccpuid);
		goal -= ivecs[0].time;
		ivecs.erase(ivecs.begin());
	}

	syslog(LOG_DEBUG, "GOAL: inums should total %lld", (long long)goal);
	find_goal(ivecs, goal);

	// find_goal() returned its results to us by setting ivec.goal if
	// the ivec should be on srccpuid, or clearing it for tgtcpuid.
	// Call move_intr() to update our delta with the new results.

	for (const IVecAlg &iv : ivecs) {
		syslog(LOG_DEBUG, "ivec %s goal %d",
		    iv.inum.c_str(), (int)iv.goal);
		(void) intrd_verify(iv.nowcpu == srccpuid || iv.nowcpu == tgtcpuid,
		    "cpu2cpu found an interrupt not on src or tgt cpu");

		if (iv.goal && iv.nowcpu != srccpuid)
			move_intr(delta, iv.inum, iv.nowcpu, srccpuid);
		else if (!iv.goal && iv.nowcpu != tgtcpuid)
			move_intr(delta, iv.inum, iv.nowcpu, tgtcpuid);
	}
	move_intr_check(delta, srccpuid, tgtcpuid);

	double newload = (double)delta.cpus.at(srccpuid).intrs /
	    (double)delta.cpus.at(srccpuid).tot;
	(void) intrd_verify(newload <= srcload &&
	    newload > delta.avgintrload,
	    "cpu2cpu: new load didn't end up in expected range");
}


// find_goal() and its helper do_find_goal() are used to find the best
// combination of interrupts in order to generate a load that is as close
// as possible to a goal load without falling below that goal. Before returning
// to its caller, find_goal() sets a new value in the hash of each interrupt,
// {goal}, which if set signifies that this interrupt is one of the interrupts
// identified as part of the set of interrupts which best meet the goal.
//
// The arguments to find_goal are a list of ivecs (hash references), sorted
// by descending {time}, and the goal load. The goal is relative to {time}.
// The best fit is determined by performing a depth-first search. do_find_goal
// is the recursive subroutine which carries out the search.
//
// It is passed an index as an argument, originally 0. On a given invocation,
// it is only to consider interrupts in the ivecs array starting at that index.
// It then considers two possibilities:
//   1) What is the best goal-fit if I include ivecs[index]?
//   2) What is the best goal-fit if I exclude ivecs[index]?
// To determine case 1, it subtracts the load of ivecs[index] from the goal,
// and calls itself recursively with that new goal and index++.
// To determine case 2, it calls itself recursively with the same goal and
// index++.
//
// It then compares the two results, decide which one best meets the goals,
// and returns the result. The return value is the best-fit's interrupt load,
// followed by a list of all the interrupts which make up that best-fit.
//
// As an optimization, a second array loads[] is created which mirrors ivecs[].
// loads[i] will equal the total loads of all ivecs[i..#ivecs]. This is used
// by do_find_goal to avoid recursing all the way to the end of the ivecs
// array if including all remaining interrupts will still leave the best-fit
// at below goal load. If so, it then includes all remaining interrupts on
// the goal list and returns.
// ref. intrd.pl:1074

static void
find_goal(std::vector<IVecAlg> &ivecs, int64_t goal)
{
	std::vector<IVecAlg *> goals;

	if (goal > 0 && !ivecs.empty()) {
		syslog(LOG_DEBUG, "finding goal from %zu intrs", ivecs.size());

		// Generate @loads array

		int64_t tot = 0;
		for (const auto &iv : ivecs)
			tot += iv.time;

		std::vector<int64_t> loads;
		for (const auto &iv : ivecs) {
			loads.push_back(tot);
			tot -= iv.time;
		}

		GoalResult result = do_find_goal(ivecs, loads, goal, 0);
		(void) intrd_verify(result.load >= goal,
		    "find_goal didn't meet goals");
		goals = std::move(result.goals);
	}

	syslog(LOG_DEBUG, "goals found: %zu ivecs", goals.size());

	// Set or clear ivec->{goal} for each ivec, based on returned @goals

	for (IVecAlg &iv : ivecs) {
		if (!goals.empty() && &iv == goals.front()) {
			iv.goal = true;
			goals.erase(goals.begin());
		} else {
			iv.goal = false;
		}
	}
}


// do_find_goal() is the recursive core of find_goal(). It performs a
// depth-first search over the ivec array (sorted descending by time) to
// find the combination of ivecs whose total time comes as close as
// possible to 'goal' without falling below it.
//
// Returns the best-fit load and pointers to the ivecs that compose it.
// Pointers are stable because ivecs is never modified during recursion.
// ref. intrd.pl:1152

static GoalResult
do_find_goal(std::vector<IVecAlg> &ivecs,
    const std::vector<int64_t> &loads, int64_t goal, size_t idx)
{
	if (idx >= ivecs.size())
		return (GoalResult{0, {}});

	int64_t load = ivecs[idx].time;

	syslog(LOG_DEBUG, "%zu: finding goal %lld inum %s",
	    idx, (long long)goal, ivecs[idx].inum.c_str());

	// If we include all remaining items and we're still below goal,
	// stop here. We can just return a result that includes idx and all
	// subsequent ivecs. Since this will still be below goal, there's
	// nothing better to be done.

	if (loads[idx] <= goal) {
		GoalResult r;
		r.load = loads[idx];
		for (size_t i = idx; i < ivecs.size(); i++)
			r.goals.push_back(&ivecs[i]);
		syslog(LOG_DEBUG, "%zu: including all remaining intrs "
		    "with load %lld", idx, (long long)loads[idx]);
		return (r);
	}

	// Evaluate the "with" option, i.e. the best matching goal which
	// includes ivecs->[idx]. If idx's load is more than our goal load,
	// stop here. Once we're above the goal, there is no need to consider
	// further interrupts since they'll only take us further from the goal.

	// Perl: $with/$without are untyped scalars; int64_t because load and goal are int64_t

	std::vector<IVecAlg *> goals_with;
	int64_t with_load;

	if (goal <= load) {
		with_load = load;
	} else {
		GoalResult wr = do_find_goal(ivecs, loads, goal - load, idx + 1);
		with_load = wr.load + load;
		goals_with = std::move(wr.goals);
	}
	syslog(LOG_DEBUG, "%zu: with-load %lld",
	    idx, (long long)with_load);

	// Evaluate the "without" option, i.e. the best matching goal which
	// excludes ivecs->[idx].

	GoalResult without_r = do_find_goal(ivecs, loads, goal, idx + 1);
	syslog(LOG_DEBUG, "%zu: without-load %lld",
	    idx, (long long)without_r.load);

	// We now have our "with" and "without" options, and we choose which
	// best fits the goal. If one is greater than goal and the other is
	// below goal, we choose the one that is greater. If they are both 
	// below goal, then we choose the one that is greater. If they are
	// both above goal, then we choose the smaller.

	int64_t without_load = without_r.load;
	bool use_without;

	if (with_load >= goal && without_load < goal)
		use_without = false;
	else if (with_load < goal && without_load >= goal)
		use_without = true;
	else if (with_load >= goal && without_load >= goal)
		use_without = (without_load < with_load);
	else
		use_without = (without_load > with_load);

	// Return the load of our best case scenario, followed by all the ivecs
	// which compose that goal.

	if (use_without) {
		syslog(LOG_DEBUG, "%zu: going without", idx);
		return (without_r);
	}

	syslog(LOG_DEBUG, "%zu: going with", idx);
	GoalResult r;
	r.load = with_load;
	r.goals.push_back(&ivecs[idx]);
	r.goals.insert(r.goals.end(), goals_with.begin(), goals_with.end());
	return (r);
}
// ref. intrd.pl:1296

int
main(int argc, char *argv[])
{
	openlog("intrd", LOG_PID | LOG_ODELAY, LOG_DAEMON);

	// Parse arguments. intrd does not accept any public arguments; the two
	// arguments below are meant for testing purposes. -d enables debug
	// syslog output. Non-option arguments are passed to optional_args(),
	// which the test build uses as kstat input files.

	int c;
	while ((c = getopt(argc, argv, "d")) != -1) {
		if (c == 'd')
			debug_flag = true;
	}
	optional_args(argc - optind, argv + optind);

	(void) signal(SIGINT,  sig_handler);
	(void) signal(SIGHUP,  sig_handler);
	(void) signal(SIGTERM, sig_handler);

	sleeptime = normal_sleeptime;

	KstatHandle *kh = intrd_ks_open();
	if (kh == NULL)
		err(1, "intrd_ks_open");

	KSnapshot ks = intrd_ks_update(kh);

	// If no pci_intrs kstats were found, we need to exit, but we can't because
	// SMF will restart us and/or report an error to the administrator. But
	// there's nothing an administrator can do. So print out a message for SMF
	// logs and silently pause forever.

	if (ks.pci_intrs.empty()) {
		syslog(LOG_ERR, "no interrupts were found; "
		    "your PCI bus may not yet be supported");
		while (gotsig == 0)
			pause();
		intrd_ks_close(kh);
		return (0);
	}

	// See if this is a system with a pcplusmp APIC.
	// Such systems will get special handling.
	// Assume that if one bus has a pcplusmp APIC that they all do.

	syslog(LOG_DEBUG, "intrd is starting%s",
	    debug_flag ? " (debug)" : "");

	bool pcplusmp_sys =
	    (is_apic(ks.pci_intrs[0].buspath.c_str()) > 0);

	std::optional<Stat> stat = getstat(ks, pcplusmp_sys);

	std::vector<Delta> deltas;
	hrtime_t deltas_tottime    = 0;
	double baseline_goodness = 0.0;

	auto clear_deltas = [&]() {
		deltas.clear();
		deltas_tottime = 0;
		stat.reset();	// prevent next generate_delta() from setting missing
	};

	for (;;) {
		if (gotsig)
			break;

		// 1. Sleep, update the kstats, and save the new stats in newstat.


		// TODO: sleep before reading - see open question in
		// doc/intrd-cxx.md "Sleep placement and signal handling".


		if (gotsig)
			break;

		ks = intrd_ks_update(kh);
		std::optional<Stat> newstat = getstat(ks, pcplusmp_sys);

		// stat or newstat could be zero if they're uninitialized, or if
		// getstat() failed. If stat is zero, move newstat to stat, sleep
		// and try again. If newstat is zero, then we also sleep and try
		// again, hoping the problem will clear up.

		if (!newstat)
			continue;
		if (!stat) {
			stat = newstat;
			continue;
		}

		// 2. Compare newstat with the prior set of values, result in delta.

		Delta delta = generate_delta(*stat, *newstat);
		if (debug_flag)
			dumpdelta(delta);
		stat = newstat;	// the new stats now become the old stats

		// 3. If delta.missing, then there has been a reconfiguration of
		// either cpus or interrupts (probably both). We need to toss out our
		// old set of statistics and start from scratch.
		//
		// Also, if the delta covers a very long range of time, then we've
		// been experiencing a system overload that has resulted in intrd
		// not being allowed to run effectively for a while now. As above,
		// toss our old statistics and start from scratch.

		hrtime_t deltatime = delta.maxsnap - delta.minsnap;
		if (delta.missing > 0 || deltatime > statslen) {
			clear_deltas();
			syslog(LOG_DEBUG, "evaluating interrupt assignments");
			continue;
		}

		// 4. Incorporate new delta into the list of deltas, and associated
		// statistics. If we've just now received statslen deltas, then it's
		// time to evaluate a reconfiguration.

		// do_reconfig_flag fires on the single iteration where
		// deltas_tottime crosses statslen; subsequent triggers come
		// from the imbalanced() check below.
		bool below_statslen = (deltas_tottime < statslen);
		deltas_tottime += deltatime;
		bool do_reconfig_flag =
		    (below_statslen && deltas_tottime >= statslen);
		deltas.push_back(delta);

		// 5. Remove old deltas if total time is more than statslen. We use
		// deltas as a moving average of the last statslen period (60sec.).
		// Shift off the oldest deltas, but only if that doesn't cause us
		// to fall below statslen.

		while (deltas.size() > 1) {
			hrtime_t olddt = deltas[0].maxsnap - deltas[0].minsnap;
			hrtime_t newtime = deltas_tottime - olddt;
			if (newtime < statslen)
				break;
			deltas.erase(deltas.begin());
			deltas_tottime = newtime;
		}

		// 6. The brains of the operation are here. First, check if we're
		// imbalanced, and if so set do_reconfig. If do_reconfig is set,
		// either because of imbalance or above in step 4, we evaluate a
		// new configuration.
		//
		// First, take deltas and generate a single "compressed" delta
		// which summarizes them all. Pass that to do_reconfig and see
		// what it does with it:
		//
		// ret == -1 : failure
		// ret ==  0 : current config is optimal (or close enough)
		// ret ==  1 : reconfiguration has occurred
		//
		// If ret is -1 or 1, dump all our deltas and start from scratch.
		// Step 4 above will set do_reconfig soon thereafter.
		//
		// If ret is 0, then nothing has happened because we're already
		// good enough. Set baseline_goodness to current goodness.

		auto compdelta_opt = compress_deltas(deltas);
		if (!compdelta_opt) {
			clear_deltas();
			continue;
		}
		Delta compdelta = std::move(*compdelta_opt);
		compdelta.goodness = goodness(compdelta);
		if (debug_flag)
			dumpdelta(compdelta);

		double g = compdelta.goodness;
		syslog(LOG_DEBUG, "GOODNESS: %5.2f%%", g * 100.0);

		if (deltas_tottime >= statslen &&
		    imbalanced(g, baseline_goodness)) {
			do_reconfig_flag = true;
		}

		if (do_reconfig_flag) {
			int ret = do_reconfig(compdelta);
			if (ret != 0) {
				clear_deltas();
				if (ret == -1)
					syslog(LOG_DEBUG,
					    "do_reconfig FAILED!");
			} else {
				syslog(LOG_DEBUG,
				    "setting new baseline of %g", g);
				baseline_goodness = g;
			}
		}
		syslog(LOG_DEBUG, "---------------------------------------");
	}

	intrd_ks_close(kh);
	return (0);
}
