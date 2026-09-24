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
# Copyright 2020 Tintri by DDN, Inc. All rights reserved.
# Copyright 2026 RackTop Systems Inc.
#

include $(SRC)/Makefile.master
include $(SRC)/cmd/Makefile.cmd
include $(SRC)/test/Makefile.com

ROOTOPTPKG = $(ROOT)/opt/libmlrpc-tests
ROOTBIN = $(ROOTOPTPKG)/bin

CPROGS = gen_samlogon_config
PROGS = libmlrpctest $(CPROGS)

CMDS = $(PROGS:%=$(ROOTBIN)/%)
$(CMDS) := FILEMODE = 0555

CSTD = $(CSTD_GNU99)
LDFLAGS += -R/usr/lib/smbsrv
LDLIBS += -L$(ROOT)/usr/lib/smbsrv -lsmb

all: $(CPROGS)

%: %.c
	$(LINK.c) -o $@ $< $(LDLIBS)
	$(POST_PROCESS)

lint:

clean:

clobber: clean
	-$(RM) $(CPROGS)

install: all $(CMDS)

$(CMDS): $(ROOTBIN)

$(ROOTBIN):
	$(INS.dir)

$(ROOTBIN)/%: %
	$(INS.file)

$(ROOTBIN)/%: %.ksh
	$(INS.rename)
