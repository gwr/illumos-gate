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
# Copyright (c) 2014 Joyent, Inc.
# Copyright 2016 Gordon W. Ross
#

# OAM User Test program
PROG=		oamu_defs
TESTS=		oamu_01 oamu_02

include $(SRC)/cmd/Makefile.cmd

ROOTOPTPKG = $(ROOT)/opt/util-tests
ROOTCMDDIR = $(ROOTOPTPKG)/bin
TESTDIR    = $(ROOTOPTPKG)/tests/liboamcmd
ROOTTESTS  = $(TESTS:%=$(TESTDIR)/%)

OBJS = $(PROG:%=%.o)
SRCS = $(OBJS:%.o=%.c)

LDLIBS += -loamcmd

all: $(PROG)

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install: $(ROOTCMD) $(ROOTTESTS)
$(ROOTTESTS) : $(TESTDIR)

clean:
	$(RM) $(OBJECTS)

lint:	lint_PROG

.KEEP_STATE:

$(TESTDIR)/%: %.ksh
	$(INS.rename)

$(TESTDIR):
	$(INS.dir)

include $(SRC)/cmd/Makefile.targ
