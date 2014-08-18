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

#
# Copyright 2013 Nexenta Systems, Inc.  All rights reserved.
#

LIBRARY =	libfakekernel.a
VERS =		.1

COBJS = \
	cred.o \
	clock.o \
	cond.o \
	copy.o \
	kiconv.o \
	kmem.o \
	kmisc.o \
	ksocket.o \
	kstat.o \
	mutex.o \
	printf.o \
	random.o \
	rwlock.o \
	sema.o \
	taskq.o \
	thread.o \
	uio.o

OBJECTS=	$(COBJS)

include ../../Makefile.lib

SRCDIR=		../common

LIBS =		$(DYNLIB) $(LINTLIB)
SRCS=   $(COBJS:%.o=$(SRCDIR)/%.c)

$(LINTLIB) :=	SRCS = $(SRCDIR)/$(LINTSRC)

# hack hack - need our sys first
DTS_ERRNO += -I../common
INCS += -I$(SRC)/common/smbsrv

CFLAGS +=	$(CCVERBOSE)
CPPFLAGS += $(INCS) -D_REENTRANT -D_FAKE_KERNEL
CPPFLAGS += -D_FILE_OFFSET_BITS=64
${NOT_RELEASE_BUILD} CPPFLAGS += -DDEBUG

LINTCHECKFLAGS += -erroff=E_INCONS_ARG_DECL2
LINTCHECKFLAGS += -erroff=E_INCONS_VAL_TYPE_DECL2

LDLIBS += -lumem -lcryptoutil -lsocket -lc

# CERRWARN += -_cc=-erroff=E_ASSIGNMENT_TYPE_MISMATCH

.KEEP_STATE:

all: $(LIBS)

lint: lintcheck

include ../../Makefile.targ
