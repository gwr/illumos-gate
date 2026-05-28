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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# Makefile for interrupt distribution daemon
#

PROG=		intrd
TESTPROG=	intrd-prks intrd-test
MANIFEST=	intrd.xml
SVCMETHOD=	svc-intrd

include ../Makefile.cmd
include ../Makefile.cmd.64

TARGET=	all

ROOTMANIFESTDIR=	$(ROOTSVCSYSTEM)

# install macros and rule
#
ROOTPROG=	$(ROOTLIB)/$(PROG)
$(ROOTPROG)	:= FILEMODE= 555

CCFLAGS += $(CCVERBOSE)
CCFLAGS += -_g++=-std=c++17
# CCFLAGS +=  -_g++=-mfpmath=sse -_g++=-msse2
CCFLAGS64 += $(CCVERBOSE)
CCFLAGS64 += -_g++=-std=c++17
CSTD = $(CSTD_GNU17)

.KEEP_STATE:

all: $(PROG) $(TESTPROG)

install: all .WAIT $(ROOTPROG) $(ROOTMANIFEST) $(ROOTSVCMETHOD)

clean: FRC
	-rm -f *.o $(PROG) $(TESTPROG)

check:	$(CHKMANIFEST)

test: $(TESTPROG)

${ROOTLIB}/%: %
	${INS.file}

lint:

FRC:


intrd := LDLIBS +=  -lkstat -lm -lc
PROG_OBJS= intrd-main.o intrd-kstat.o intrd-move.o
intrd: $(PROG_OBJS)
	$(LINK.cc) $(PROG_OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)


intrd-prks := LDLIBS +=  -lkstat -lc
PRKS_OBJS= intrd-prks.o intrd-kstat.o
intrd-prks: $(PRKS_OBJS)
	$(LINK.cc) $(PRKS_OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

intrd-test := LDLIBS += -lm -lc
TEST_OBJS= intrd-main.o intrd-test.o
intrd-test: $(TEST_OBJS)
	$(LINK.cc) $(TEST_OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)


include ../Makefile.targ
