#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
#
# Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
# Copyright 2018 Nexenta Systems, Inc.  All rights reserved.
# Copyright 2023 RackTop Systems, Inc.
#

# Note, for debugging, build like this:
# make install SOURCEDEBUG=yes COPTFLAG=-g

PROG=	test-taskq

OBJS_LOCAL=	test_main.o os_misc.o tq_dt.o callb.o
OBJS_OS= taskq.o  # alternatively, os_taskq.o
OBJS=	${OBJS_LOCAL} ${OBJS_OS}

SRCS=	${OBJS_LOCAL:.o=.c}

include ../../Makefile.cmd
include ../../Makefile.ctf

UTSCMN = ../../../uts/common

# Note: need our sys includes _before_ ENVCPPFLAGS, proto etc.
CPPFLAGS.first += -I.
CPPFLAGS.first += -I../../../lib/libfakekernel/common
CPPFLAGS.first += -I$(UTSCMN)

CSTD=		$(CSTD_GNU99)

CFLAGS += $(CCVERBOSE)
CFLAGS64 += $(CCVERBOSE)

CERRWARN	+= -_gcc=-Wno-parentheses

CPPFLAGS += -D_REENTRANT
CPPFLAGS += -DTESTJIG
CPPFLAGS += -D_LARGEFILE64_SOURCE=1
CPPFLAGS += -D_FAKE_KERNEL
# Always debug here
CPPFLAGS += -DDEBUG
CPPFLAGS += $(INCS)

LDFLAGS += $(ZNOLAZYLOAD)
LDFLAGS += '-R$$ORIGIN/..'
LDLIBS += -lfakekernel -lumem -lcmdutils

ROOTSMBDDIR = $(ROOTLIB)/smbsrv
ROOTSMBDFILE = $(PROG:%=$(ROOTSMBDDIR)/%)

CLOBBERFILES =  sys/taskq.h sys/taskq_impl.h

.KEEP_STATE:

all: $(PROG)

$(PROG): $(OBJS)
	$(LINK.c) -o $(PROG) $(OBJS) $(LDLIBS)
	$(POST_PROCESS)

clean:
	-$(RM) $(OBJS)

include ../../Makefile.targ

install: all $(ROOTSMBDFILE)

# This builds using libfakekernel (and it's headers) EXCEPT for
# the taskq headers, for which here we need the real UTS ones.
# Copy those to our private sys area (first include path)
# Not using a patter rule, as that would overwrite other
# private headers under the local sys dir.
install_h: sys/taskq.h sys/taskq_impl.h

sys/taskq.h	: $(UTSCMN)/sys/taskq.h
	$(CP) $(UTSCMN)/sys/taskq.h $@

sys/taskq_impl.h : $(UTSCMN)/sys/taskq_impl.h
	$(CP) $(UTSCMN)/sys/taskq_impl.h $@

test_main.o : test_main.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -D_FAKE_KERNEL -c test_main.c
	$(POST_PROCESS_O)

os_taskq.o : os_taskq.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -D_FAKE_KERNEL -c os_taskq.c
	$(POST_PROCESS_O)

taskq.o : $(UTSCMN)/os/taskq.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -D_FAKE_KERNEL -c $(UTSCMN)/os/taskq.c
	$(POST_PROCESS_O)

$(ROOTSMBDDIR)/%: %
	$(INS.file)
