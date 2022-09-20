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
#


PROG=	test-negoex

OBJS_LOCAL=	tn_main.o # tn_misc.o
OBJS_SMBD=	smbd_authsvc.o smbd_ntlmssp.o # smbd_krb5ssp.o
OBJS_LIBSMBFS=	spnego.o spnegoparse.o derparse.o
OBJS_LIBSMB=	

OBJS=	${OBJS_LOCAL} ${OBJS_SMBD} ${OBJS_LIBSMBFS} ${OBJS_LIBSMB}

SMBD_SRCDIR=../../../uts/common/fs/smbsrv

include ../../Makefile.cmd
include ../../Makefile.ctf

INCS +=	-I. -I../smbd
INCS += -I $(ROOT)/usr/include/kerberosv5

CSTD=		$(CSTD_GNU99)

CFLAGS += $(CCVERBOSE)
CFLAGS64 += $(CCVERBOSE)

CPPFLAGS += -D_REENTRANT
CPPFLAGS += -DTESTJIG
CPPFLAGS += -Dsyslog=smb_syslog
CPPFLAGS += -D_LARGEFILE64_SOURCE=1
# Always debug here
CPPFLAGS += -DDEBUG
CPPFLAGS += $(INCS)

LDFLAGS += $(ZNOLAZYLOAD)
LDFLAGS += '-R$$ORIGIN' '-R$$ORIGIN/..'
LDLIBS += -L$(ROOT)/usr/lib/smbsrv
# LDLIBS += -lsmbfs
LDLIBS += -lsmb -lsocket -lnsl

ROOTSMBDDIR = $(ROOTLIB)/smbsrv
ROOTSMBDFILE = $(PROG:%=$(ROOTSMBDDIR)/%)

.KEEP_STATE:

all: $(PROG)

$(PROG): $(OBJS)
	$(LINK.c) -o $(PROG) $(OBJS) $(LDLIBS)
	$(POST_PROCESS)

clean:
	-$(RM) $(OBJS)

lint:	# lint_SRCS

include ../../Makefile.targ

install: all $(ROOTSMBDFILE)

# OBJS_SMBD
%.o: ../smbd/%.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $<
	$(POST_PROCESS_O)

# OBJS_LIBSMBFS
%.o: ../../../lib/libsmbfs/smb/%.c
	$(CC) $(CFLAGS) $(CPPFLAGS) \
	-I ../../../lib/libsmbfs/netsmb \
	-I ../../../lib/libsmbfs/smb -c $<
	$(POST_PROCESS_O)

# OBJS_LIBSMB
%.o: ../../../lib/smbsrv/libsmb/common/%.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $<
	$(POST_PROCESS_O)


$(ROOTSMBDDIR)/%: %
	$(INS.file)
