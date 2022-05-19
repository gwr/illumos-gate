#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License, Version 1.0 only
# (the "License").  You may not use this file except in compliance
# with the License.
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
# Copyright (c) 1997 by Sun Microsystems, Inc.
# All rights reserved.
#

PROG=		testacl
OBJS=           $(PROG).o
SRCS=           $(OBJS:%.o=../%.c)

include $(SRC)/cmd/Makefile.cmd

# LDLIBS += -lsec
LDLIBS += ../../lib/libsec/static/libsec.a
LDLIBS += -lavl -lidmap

CFLAGS	+=	$(CCVERBOSE)

CFLAGS64 +=	$(CCVERBOSE)
CPPFLAGS += -D_FILE_OFFSET_BITS=64

# main() can be too hairy
SMATCH=off

.KEEP_STATE:

install all:	$(PROG)

clean:
	$(RM) $(CLEANFILES)

include $(SRC)/cmd/Makefile.targ
