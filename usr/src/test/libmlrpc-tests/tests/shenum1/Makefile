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

PROGS = srvsvc1srv srvsvc1clnt

include $(SRC)/cmd/Makefile.cmd

INSTPROGS = $(PROGS:%=$(TESTDIR)/%)
$(INSTPROGS) := FILEMODE = 0555

CPPFLAGS += -I../common
CPPFLAGS += -DDEBUG

CSTD= $(CSTD_GNU99)

LDFLAGS += $(ZNOLAZYLOAD)
LDFLAGS += '-R$$ORIGIN/../../lib' '-R$$ORIGIN/../../../usr/lib'
LDLIBS += -lmlrpc -luuid

# not linted
SMATCH=off

CLEANFILES +=  srvsvc1_ndr.c

all: $(PROGS)

install: all $(INSTPROGS)

lint:

clobber: clean
	$(RM) $(PROGS)

clean:
	$(RM) *.o $(CLEANFILES)

$(INSTPROGS): $(TESTDIR) $(PROGS)

$(TESTDIR):
	$(INS.dir)

$(TESTDIR)/%: %
	$(INS.file)

$(TESTDIR)/%: %
	$(INS.file)

# srvsvc1clnt
OBJS_CLNT=   clnt_main.o clnt_pipe.o srvsvc1_clnt.o \
	srvsvc1_ndr.o test_util.o

srvsvc1clnt:	$(OBJS_CLNT)
	$(LINK.c) -o $@ $(OBJS_CLNT) $(LDLIBS)
	$(POST_PROCESS)

# srvsvc1srv
OBJS_SRV=   srv_main.o srv_pipe.o srv_fake.o srvsvc1_svc.o \
	srvsvc1_ndr.o test_util.o

srvsvc1srv:	$(OBJS_SRV)
	$(LINK.c) -o $@ $(OBJS_SRV) $(LDLIBS)
	$(POST_PROCESS)

include $(SRC)/cmd/Makefile.targ

srvsvc1_ndr.c : srvsvc1.ndl
	$(NDRGEN) -Y $(ANSI_CPP) $(CPPFLAGS) srvsvc1.ndl

%.o : ../common/%.c
	$(COMPILE.c) $(OUTPUT_OPTION) $< $(CTFCONVERT_HOOK)
	$(POST_PROCESS_O)

.KEEP_STATE:
