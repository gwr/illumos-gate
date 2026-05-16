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
# Copyright 2014 Garrett D'Amore <garrett@damore.org>
# Copyright 2018, Joyent, Inc.
#

include $(SRC)/Makefile.master

TESTSUBDIR = common
PROG = c_symbols_test

OBJS = $(PROG:%=%.o) hdr_test_common.o
OBJS32 = $(OBJS:%.o=%.$(MACH).o)
PROG32 = $(PROG:%=%.$(MACH))

$(BUILD64) OBJS64 = $(OBJS:%.o=%.$(MACH64).o)
$(BUILD64) PROG64= $(PROG:%=%.$(MACH64))

SRCS = $(PROG:%=%.c) hdr_test_common.c

# This gets CSTD, *FLAGS, *DIR
include ../Makefile.com

LDLIBS += -lcustr
LDLIBS64 += -lcustr

# Things we'll install
CMDS =	$(PROG32:%=$(TESTDIR)/%) \
	$(PROG64:%=$(TESTDIR)/%)

$(CMDS) := FILEMODE = 0555

all: $(PROG32) $(PROG64)

install: $(TESTDIR) $(CMDS)

clobber: clean
	-$(RM) $(PROG32) $(PROG64)

clean:
	-$(RM) $(OBJS32) $(OBJS64)

$(PROG32): $(OBJS32)
	$(LINK.c) $(OBJS32) -o $@ $(LDLIBS)
	$(POST_PROCESS)

$(PROG64): $(OBJS64)
	$(LINK64.c) $(OBJS64) -o $@ $(LDLIBS64)
	$(POST_PROCESS)

%.$(MACH).o: %.c
	$(COMPILE.c) -o $@ $(CFLAGS_$(MACH)) -DARCH=\"$(MACH)\" $<

%.$(MACH64).o: %.c
	$(COMPILE64.c) -o $@ $(CFLAGS_$(MACH64)) -DARCH=\"$(MACH64)\" $<
