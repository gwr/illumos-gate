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
 * Copyright 2024 RackTop Systems, Inc.
 */

/*
 * SMB demon listener functions
 *
 * The SMB server deamon runs a listener thread for each configured
 * listener binding. (See smbd_listener_main)  Each listener thread
 * spends most of its life in an ioctl call (SMB_IOC_LISTEN) which
 * is passed the listener binding address. That ioctl call runs a
 * listen/accept loop until the listen/accept calls return with a
 * fatal error. Typical termination errors include EINTR when an
 * smbd reconfiguration uses pthread_kill to stop a listener, and
 * EADDRNOTAVAIL when an inteface address is unconfigured.
 *
 * Listener threads are started, one per binding (as needed) by
 * smbd_listener_start at smbd startup and after network changes
 * as detected by the smbd_nicmon module (see smbd_nicmon_start).
 * The current set of listener bindings is determined from the list
 * of configured interface addresses (see getifaddrs) using a list
 * of match patterns from the "listener_bindings" config variable.
 * For each network interface included by the match patterns,
 * smbd_listen_start_one is called to start a listener thread
 * for that binding, if one is not already running.
 * (See smbd_listen_start_bindings, ifa_match)
 *
 * When "listener_bindings" is empty, listeners are started on
 * the special "ANY" address, if not already running.
 * (see smbd_listen_start_anyaddr)
 *
 * After a network configuration change, listener bindings that
 * are no longer part of the configured set are "pruned" using a
 * generation number scheme.  (See smbd_listener_prune)
 *
 * When smbd is shutdown, smbd_listener_stop is called to send a
 * SIGTERM to all listener threads.
 */

#include <sys/types.h>
#include <sys/ioccom.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>
#include <signal.h>
#include <atomic.h>
#include <limits.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <libscf.h>
#include <zone.h>
#include <libgen.h>
#include <ifaddrs.h>
#include <fnmatch.h>

#include <smbsrv/smb_ioctl.h>
#include <smbsrv/string.h>
#include <smbsrv/libsmb.h>
#include <smbsrv/libsmbns.h>
#include <smbsrv/libmlsvc.h>
#include "smbd.h"

typedef struct smbd_listener {
	struct list_node sl_ln;
	smb_inaddr_t	sl_addr;
	uint_t		sl_port;
	uint_t		sl_gen;
	pthread_t	sl_tid;
	char		sl_ifname[LIFNAMSIZ];
} smbd_listener_t;

static list_t listeners_list;
static mutex_t listeners_lock;
static uint_t listeners_generation;

static void *
smbd_listener_main(void *arg)
{
	char	astr[INET6_ADDRSTRLEN];
	sigset_t	set;
	smbd_listener_t *sl = arg;
	uint_t tid = pthread_self();
	boolean_t killed;
	int rc;

	if (smb_inet_ntop(&sl->sl_addr, astr, sizeof (astr)) == NULL)
		(void) strlcpy(astr, "?", sizeof (astr));
	if (smbd.s_debug) {
		smbd_report("IP listener(%s) tid %u start", astr, tid);
	}

	(void) sigemptyset(&set);
	(void) sigaddset(&set, SIGTERM);
	(void) sigprocmask(SIG_UNBLOCK, &set, NULL);

again:
	killed = B_FALSE;
	rc = smb_kmod_listen(&sl->sl_addr, sl->sl_port);
	switch (rc) {

	case EINTR:
	case EBADF:
	case ENOTSOCK:
		/* These are normal during shutdown. Silence. */
		killed = B_TRUE;
		if (smbd.s_debug)
			smbd_report("IP listener(%s) rc=%d (exit)", astr, rc);
		break;

	case EAFNOSUPPORT:
	case EADDRINUSE:
	case EADDRNOTAVAIL:
	case ENETDOWN:
		smbd_report("IP listener(%s) rc=%d (exit)", astr, rc);
		break;

	default:
		smbd_report("IP listener(%s) rc=%d (rebind)", astr, rc);
		(void) sleep(1);
		goto again;
	}

	(void) mutex_lock(&listeners_lock);
	/*
	 * In case sl_gen was updated after a signal, 
	 * (we got a reprieve) keep listening.
	 */
	if (killed && sl->sl_gen == listeners_generation) {
		(void) mutex_unlock(&listeners_lock);
		goto again;
	}
	list_remove(&listeners_list, sl);
	(void) mutex_unlock(&listeners_lock);

	if (smbd.s_debug) {
		smbd_report("IP listener(%s) tid %u exit", astr, tid);
	}

	free(sl);
	return (NULL);
}

/*
 * Create a listener and add to this list if not already there.
 */
static int
smbd_listen_start_one(char *ifname, struct sockaddr *sa, in_port_t port)
{
	pthread_attr_t	attr;
	smbd_listener_t *sl, *new_sl;
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;
	int ret;

	new_sl = calloc(1, sizeof (*new_sl));
	if (new_sl == NULL)
		return (ENOMEM);

	switch (sa->sa_family) {
	case AF_INET:
		sin = (struct sockaddr_in *)sa;
		bcopy(&sin->sin_addr, &new_sl->sl_addr.a_ipv4,
		    sizeof (in_addr_t));
		break;

	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)sa;
		bcopy(&sin6->sin6_addr, &new_sl->sl_addr.a_ipv6,
		    sizeof (in6_addr_t));
		break;

	default:
		return (EINVAL);
	}
	new_sl->sl_addr.a_family = sa->sa_family;
	new_sl->sl_port = htons(port);
	(void) strlcpy(new_sl->sl_ifname, ifname, LIFNAMSIZ);

	(void) mutex_lock(&listeners_lock);
	new_sl->sl_gen = listeners_generation;

	for (sl = list_head(&listeners_list);
	    sl != NULL;
	    sl = list_next(&listeners_list, sl)) {
		if (bcmp(&sl->sl_addr, &new_sl->sl_addr,
		    sizeof (smb_inaddr_t)) == 0 &&
		    sl->sl_port == new_sl->sl_port) {
			/* Already exists. Let it be. */
			sl->sl_gen = new_sl->sl_gen;
			ret = 0;
			goto unlock_out;
		}
	}

	(void) pthread_attr_init(&attr);
	(void) pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	ret = pthread_create(&new_sl->sl_tid, &attr,
	    smbd_listener_main, new_sl);
	(void) pthread_attr_destroy(&attr);

	if (ret == 0) {
		list_insert_tail(&listeners_list, new_sl);
		new_sl = NULL; /* smbd_listener_main will free */
	} else {
		smbd_report("listen_start_one, thr_create, rc=%d", ret);
	}

unlock_out:
	free(new_sl);
	(void) mutex_unlock(&listeners_lock);
	return (ret);
}

static boolean_t
ifa_match(struct ifaddrs *i, const char *matcharg)
{
	const char *sep = "|";
	char *match = NULL;
	char *pat;
	char *lastp;
	boolean_t neg;
	boolean_t ret;
	int fnm;

	if (smbd.s_debug) {
		smbd_report("ifa_match: ifname %s match %s",
		    i->ifa_name, matcharg);
	}

	/*
	 * Empty matcharg matches all (for now)
	 */
	if (matcharg == NULL ||
	    matcharg[0] == '\0')
		return (B_TRUE);

	match = strdup(matcharg);
	if (match == NULL) {
		smbd_report("ifa_match: strdup failed");
		return (B_FALSE);
	}
	pat = strtok_r(match, sep, &lastp);
	while (pat != NULL) {
		if (smbd.s_debug > 1) {
			smbd_report("ifa_match: pat %s\n", pat);
		}

		neg = B_FALSE;
		if (pat[0] == '!') {
			neg = B_TRUE;
			pat++;
		}
		fnm = fnmatch(pat, i->ifa_name, 0);
		if (fnm == 0) {
			/* It's a match. */
			ret = !neg;
			goto out;
		}
		if (fnm != FNM_NOMATCH) {
			smbd_report("ifa_match: fnmatch %d match pattern (%s)",
			    fnm, matcharg);
		}
		pat = strtok_r(NULL, sep, &lastp);
	}
	ret = B_FALSE;

out:
	free(match);
	return (ret);
}

static int
smbd_listen_start_bindings(char *match)
{
	struct ifaddrs *ifp, *i;
	const struct in6_addr in6addr_any = IN6ADDR_ANY_INIT;
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;
	char ntop[INET6_ADDRSTRLEN];
	boolean_t ipv6ena;
	boolean_t nbt_ena;
	int rc;

	ipv6ena = smb_config_getbool(SMB_CI_IPV6_ENABLE);
	nbt_ena = smb_config_getbool(SMB_CI_NETBIOS_ENABLE);

	if (getifaddrs(&ifp) != 0) {
		smbd_report("listen_start_bindings, getifaddrs %s",
		    strerror(errno));
		return (-1);
	}

	for (i = ifp; i != NULL; i = i->ifa_next) {
		if (i->ifa_flags & IFF_LOOPBACK)
			continue;
		if ((i->ifa_flags & IFF_UP) == 0)
			continue;
		if (!ifa_match(i, match))
			continue;

		switch (i->ifa_addr->sa_family) {
		case AF_INET:
			sin = (struct sockaddr_in *)i->ifa_addr;
			if (sin->sin_addr.s_addr == INADDR_ANY)
				continue;
			(void) inet_ntop(AF_INET, &sin->sin_addr,
			    ntop, sizeof (ntop));
			rc = smbd_listen_start_one(i->ifa_name, i->ifa_addr,
			    IPPORT_SMB);
			if (rc != 0) {
				smbd_report("listen_start_one(%s:%d) rc=%d",
				    ntop, IPPORT_SMB, rc);
			}
			if (!nbt_ena)
				continue;
			/* Start a NetBIOS listener too. */
			rc = smbd_listen_start_one(i->ifa_name, i->ifa_addr,
			    IPPORT_NETBIOS_SSN);
			if (rc != 0) {
				smbd_report("listen_start_one(%s:%d) rc=%d",
				    ntop, IPPORT_NETBIOS_SSN, rc);
			}
			break;

		case AF_INET6:
			sin6 = (struct sockaddr_in6 *)i->ifa_addr;
			if (memcmp(&in6addr_any,
			    &(sin6)->sin6_addr,
			    sizeof (struct in6_addr)) == 0)
				continue;
			(void) inet_ntop(AF_INET6, &sin6->sin6_addr,
			    ntop, sizeof (ntop));
			if (!ipv6ena) {
				/*
				 * If IPv6 disabled, we normally will not
				 * see inet6 addresses from getifaddrs
				 */
				smbd_report("listen_start_bindings,"
				    " IPv6 not enabled, skip %s", ntop);
				continue;
			}
			rc = smbd_listen_start_one(i->ifa_name, i->ifa_addr,
			    IPPORT_SMB);
			if (rc != 0) {
				smbd_report("listen_start_one(%s:%d) rc=%d",
				    ntop, IPPORT_SMB, rc);
			}
			/*
			 * One could also start a NetBIOS port listener for
			 * IPv6 but NetBIOS is usually IPv4-only.
			 */
			break;

		default:
			break;
		}
	}

	freeifaddrs(ifp);
	return (0);
}

/*
 * smbd_listen_start_anyaddr()
 *
 * Alternative to smbd_listen_start_bindings() used when "listener_bindings"
 * is empty. Listen on the "ANY" address (all zeros).
 *
 * As above, start separate listeners for IPv4 and IPv6 because the kernel
 * always does setsockopt IPPROTO_IPV6, IPV6_V6ONLY.
 */
static int
smbd_listen_start_anyaddr(void)
{
	struct sockaddr_in any;
	struct sockaddr_in6 any6;
	int rc, ret = 0;

	bzero(&any, sizeof (any));
	any.sin_family = AF_INET;
	rc = smbd_listen_start_one("*", (struct sockaddr *)&any, IPPORT_SMB);
	if (rc != 0) {
		smbd_report("listen_start_one(%s:%d), rc=%d",
		    "(v4*)", IPPORT_SMB, rc);
		ret = rc;
	}

	if (smb_config_getbool(SMB_CI_NETBIOS_ENABLE)) {
		/* Start a NetBIOS listener too. */
		rc = smbd_listen_start_one("*",
		    (struct sockaddr *)&any,
		    IPPORT_NETBIOS_SSN);
		if (rc != 0) {
			smbd_report("listen_start_one(%s:%d) rc=%d",
			    "(v4*)", IPPORT_NETBIOS_SSN, rc);
			if (ret == 0)
				ret = rc;
		}
	}

	if (smb_config_getbool(SMB_CI_IPV6_ENABLE)) {
		bzero(&any6, sizeof (any6));
		any6.sin6_family = AF_INET6;

		rc = smbd_listen_start_one("*",
		    (struct sockaddr *)&any6, IPPORT_SMB);
		if (rc != 0) {
			smbd_report("listen_start_one(%s:%d) rc=%d",
			    "(v6*)", IPPORT_SMB, rc);
			if (ret == 0)
				ret = rc;
		}
		/*
		 * One could also start a NetBIOS port listener for
		 * IPv6 but NetBIOS is usually IPv4-only.
		 */
	}
	return (ret);
}

/*
 * Stop listener threads that are not the current generation
 */
static void
smbd_listener_prune(void)
{
	smbd_listener_t *sl;

	(void) mutex_lock(&listeners_lock);
	for (sl = list_head(&listeners_list);
	    sl != NULL;
	    sl = list_next(&listeners_list, sl)) {
		if (sl->sl_gen == listeners_generation)
			continue;
		if (sl->sl_tid != 0) {
			(void) pthread_kill(sl->sl_tid, SIGTERM);
		}
	}
	(void) mutex_unlock(&listeners_lock);
}


/*
 * Stop all listener threads.
 */
void
smbd_listener_stop(void)
{
	smbd_listener_t *sl;

	(void) mutex_lock(&listeners_lock);
	listeners_generation = 0;
	for (sl = list_head(&listeners_list);
	    sl != NULL;
	    sl = list_next(&listeners_list, sl)) {
		if (sl->sl_tid != 0) {
			(void) pthread_kill(sl->sl_tid, SIGTERM);
		}
	}
	(void) mutex_unlock(&listeners_lock);
}

/*
 * Get listener binding configuration from SMF and
 * start listener threads for each binding.
 */
void
smbd_listener_start(void)
{
	char	bindings[MAX_VALUE_BUFLEN];
	int	rc = -1;

	/*
	 * Set a new generation number, so old listeners can be pruned.
	 * Note that zero is used as an invalid generation number.
	 */
	(void) mutex_lock(&listeners_lock);
	listeners_generation++;
	if (listeners_generation == 0)
		listeners_generation = 1;

	/* could move this to a new _init() */
	if (listeners_list.list_size == 0) {
		list_create(&listeners_list,
		    sizeof (smbd_listener_t),
		    offsetof(smbd_listener_t, sl_ln));
	}
	(void) mutex_unlock(&listeners_lock);

	/*
	 * If a bindings list is configured, start a listner
	 * for each configured interface that matches.
	 */
	bindings[0] = '\0';
	(void) smb_config_getstr(SMB_CI_LISTENER_BINDINGS,
	    bindings, sizeof (bindings));
	if (bindings[0] != '\0') {
		rc = smbd_listen_start_bindings(bindings);
		if (rc != 0) {
			smbd_report("listen_start_bindings, rc=%d", rc);
		}
	} else {
		/*
		 * (Else) listen on the "any" address (all interfaces).
		 */
		rc = smbd_listen_start_anyaddr();
		if (rc != 0) {
			smbd_report("listen_start_anyaddr, rc=%d", rc);
		}
	}

	smbd_listener_prune();
}
