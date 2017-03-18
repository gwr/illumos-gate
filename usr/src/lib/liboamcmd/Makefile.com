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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

LIBRARY= 	liboamcmd.a
VERS=		.1

OBJECTS= 	putgrent.o \
		errmsg.o \
		defaults.o \
		file.o \
		vgid.o \
		vgname.o \
		vgroup.o \
		vuid.o \
		vlogin.o \
		vproj.o \
		dates.o \
		vexpire.o \
		putprojent.o \
		vprojid.o \
		vprojname.o

include ../../Makefile.lib

LIBS=		$(DYNLIB) $(LINTLIB)

SRCDIR=		../common

LINTOUT =	lint.out

LINTSRC =	$(LINTLIB:%.ln=%)
ROOTLINTDIR =	$(ROOTLIBDIR)
ROOTLINT =	$(LINTSRC:%=$(ROOTLINTDIR)/%)

CPPFLAGS=	-I. -I$(SRCDIR) $(CPPFLAGS.master)
CERRWARN +=	-_gcc=-Wno-parentheses
CERRWARN +=	-_gcc=-Wno-type-limits
CERRWARN +=	-_gcc=-Wno-unused-variable
LDLIBS +=	-lproject -lc

LINTFLAGS=	-u
LINTFLAGS +=	-erroff=E_BAD_PTR_CAST_ALIGN
LINTFLAGS64 +=	-erroff=E_BAD_PTR_CAST_ALIGN

$(LINTLIB) :=	SRCS= $(SRCDIR)/$(LINTSRC)

.KEEP_STATE:

all:		$(LIBS)

lint:		lintcheck

include ../../Makefile.targ
