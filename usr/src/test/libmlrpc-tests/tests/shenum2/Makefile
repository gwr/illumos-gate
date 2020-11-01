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

PROG = srvsvc2clnt

include $(SRC)/cmd/Makefile.cmd

INSTPROG = $(PROG:%=$(TESTDIR)/%)
$(INSTPROG) := FILEMODE = 0555

CPPFLAGS += -I../common
CPPFLAGS += -DDEBUG

CSTD= $(CSTD_GNU99)

LDFLAGS += $(ZNOLAZYLOAD)
LDFLAGS += '-R$$ORIGIN/../../lib' '-R$$ORIGIN/../../../usr/lib'
LDLIBS += -lmlrpc -luuid

# not linted
SMATCH=off

CLOBBERFILES +=  srvsvc1_ndr.c

all: $(PROG)

install: all $(INSTPROG)

lint:

clean:
	$(RM) *.o $(CLEANFILES)

$(INSTPROG): $(TESTDIR) $(PROG)

$(TESTDIR):
	$(INS.dir)

$(TESTDIR)/%: %
	$(INS.file)

$(TESTDIR)/%: %
	$(INS.file)

# srvsvc2clnt
OBJS_CLNT=   clnt_main.o clnt_pipe.o srvsvc1_clnt.o \
	srvsvc1_ndr.o test_util.o enum200.o shares202.o

srvsvc2clnt:	$(OBJS_CLNT)
	$(LINK.c) -o $@ $(OBJS_CLNT) $(LDLIBS)
	$(POST_PROCESS)

include $(SRC)/cmd/Makefile.targ

srvsvc1_ndr.c : srvsvc1.ndl
	$(NDRGEN) -Y $(ANSI_CPP) $(CPPFLAGS) srvsvc1.ndl

%.o : ../common/%.c
	$(COMPILE.c) $(OUTPUT_OPTION) $< $(CTFCONVERT_HOOK)
	$(POST_PROCESS_O)

.KEEP_STATE:
