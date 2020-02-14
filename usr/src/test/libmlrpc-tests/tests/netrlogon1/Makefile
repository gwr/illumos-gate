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
# Copyright 2015 Joyent, Inc.
#

include $(SRC)/Makefile.master

ROOTOPTPKG = $(ROOT)/opt/libmlrpc-tests
TESTDIR = $(ROOTOPTPKG)/tests

PROGS =	netr-clnt1

include $(SRC)/cmd/Makefile.cmd
# This breaks debug support (sigh... am I the only person in this project who ever uses -g?)
# include $(SRC)/test/Makefile.com

INSTPROGS = $(PROGS:%=$(TESTDIR)/%)
$(INSTPROGS) := FILEMODE = 0555

CPPFLAGS += -I$(SRC)/lib/libsmbfs
CPPFLAGS += -DDEBUG

CSTD= $(CSTD_GNU99)

LDFLAGS += $(ZNOLAZYLOAD)
LDFLAGS += '-R$$ORIGIN/../../lib' '-R$$ORIGIN/../../../usr/lib'
LDLIBS += -lmlrpc

# not linted
SMATCH=off

CLEANFILES +=  netlogon1_ndr.c

all: $(PROGS)

install: all $(INSTPROGS)

lint:

clobber: clean
	$(RM) $(PROGS)

clean:
	$(RM) $(OBJS1) $(CLEANFILES)

$(INSTPROGS): $(TESTDIR) $(PROGS)

$(TESTDIR):
	$(INS.dir)

$(TESTDIR)/%: %
	$(INS.file)

$(TESTDIR)/%: %
	$(INS.file)

# srvsvc1
OBJS1=   srvsvc1_main.o srvsvc1_svc.o srvsvc_fake.o test_pipe.o \
	netlogon1_ndr.o

srvsvc1:	$(OBJS1)
	$(LINK.c) -o $@ $(OBJS1) $(LDLIBS)
	$(POST_PROCESS)

include $(SRC)/cmd/Makefile.targ

netlogon1_ndr.c : netlogon1.ndl
	$(NDRGEN) -Y $(ANSI_CPP) $(CPPFLAGS) netlogon1.ndl

.KEEP_STATE:
