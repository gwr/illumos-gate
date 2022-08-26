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
# Copyright 2022 RackTop Systems, Inc.
#


PROG=	knock

OBJS_LOCAL=	knock_main.o

OBJS_SMBSHA=	smb_share_doorclnt.o

OBJS=	${OBJS_LOCAL} ${OBJS_SMBSHA}

include ../../Makefile.cmd
include ../../Makefile.ctf

INCS += -I../../../lib/libshare/smb
INCS +=	-I../../../uts/common
INCS +=	-I../../../uts/common/smbsrv
INCS +=	-I../../../common/smbsrv

CSTD=		$(CSTD_GNU99)

CFLAGS += $(CCVERBOSE)
CFLAGS64 += $(CCVERBOSE)

CERRWARN	+= -_gcc=-Wno-parentheses

CPPFLAGS += -D_REENTRANT
CPPFLAGS += -DTESTJIG
CPPFLAGS += -Dsyslog=smb_syslog
CPPFLAGS += -D_LARGEFILE64_SOURCE=1
# Always debug here
CPPFLAGS += -DDEBUG
CPPFLAGS += $(INCS)

LDFLAGS += $(ZNOLAZYLOAD)
LDFLAGS += '-R$$ORIGIN/..'

LDFLAGS += -R/usr/lib/smbsrv
LDLIBS += -L$(ROOT)/usr/lib/smbsrv -lsmb -lsocket

ROOTSMBDDIR = $(ROOTLIB)/smbsrv
ROOTSMBDFILE = $(PROG:%=$(ROOTSMBDDIR)/%)

SMATCH=off

.KEEP_STATE:

all: $(PROG)

$(PROG): $(OBJS)
	$(LINK.c) -o $(PROG) $(OBJS) $(LDLIBS)
	$(POST_PROCESS)

clean:
	-$(RM) $(OBJS)

include ../../Makefile.targ

install: all $(ROOTSMBDFILE)

# OBJS_SMBSHA
%.o: ../../../lib/libshare/smb/%.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $<
	$(POST_PROCESS_O)

$(ROOTSMBDDIR)/%: %
	$(INS.file)
