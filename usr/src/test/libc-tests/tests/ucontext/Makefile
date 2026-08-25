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
# Copyright 2026 Gordon W. Ross
#

PROG = ucontext-abi

PROGS32 = \
	$(PROG).gnu89.32 \
	$(PROG).xpg4v2.32

PROGS64 = \
	$(PROG).gnu89.64 \
	$(PROG).xpg4v2.64

OBJS32 = $(PROGS32:%=%.o)
OBJS64 = $(PROGS64:%=%.o)

ROOTOPTDIR = $(ROOT)/opt/libc-tests/tests
ROOTOPTUCONTEXT = $(ROOTOPTDIR)/ucontext
ROOTOPTPROGS = \
	$(PROGS32:%=$(ROOTOPTUCONTEXT)/%) \
	$(PROGS64:%=$(ROOTOPTUCONTEXT)/%)

include $(SRC)/cmd/Makefile.cmd
include $(SRC)/test/Makefile.com

$(PROGS32) $(PROGS64) := CSTD = $(CSTD_GNU89)

$(PROG).gnu89.32 $(PROG).gnu89.64 := \
	CPPFLAGS += -DTEST_ENV=\"GNU89\"

$(PROG).xpg4v2.32 $(PROG).xpg4v2.64 := \
	CPPFLAGS += -DTEST_ENV=\"XPG4v2\" \
	-D_XOPEN_SOURCE -D_XOPEN_SOURCE_EXTENDED=1

.KEEP_STATE:

all: $(PROGS32) $(PROGS64)

install: $(ROOTOPTPROGS)

$(ROOTOPTDIR):
	$(INS.dir)

$(ROOTOPTUCONTEXT): $(ROOTOPTDIR)
	$(INS.dir)

$(ROOTOPTPROGS): $(PROGS32) $(PROGS64) $(ROOTOPTUCONTEXT)

$(ROOTOPTUCONTEXT)/%: %
	$(INS.file)

%.32.o: ucontext-abi.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

%.32: %.32.o
	$(LINK.c) -o $@ $< $(LDLIBS)
	$(POST_PROCESS)

%.64.o: ucontext-abi.c
	$(COMPILE64.c) -o $@ $<
	$(POST_PROCESS_O)

%.64: %.64.o
	$(LINK64.c) -o $@ $< $(LDLIBS64)
	$(POST_PROCESS)

clean:
	-$(RM) $(OBJS32) $(OBJS64)

clobber: clean
	-$(RM) $(PROGS32) $(PROGS64)
