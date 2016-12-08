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
		defaults.o \
		errmsg.o \
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

# include library definitions
include ../../Makefile.lib

SRCDIR =	../common

LIBS=		$(DYNLIB) $(LINTLIB)

LINTOUT =	lint.out

LINTSRC =	$(LINTLIB:%.ln=%)
ROOTLINTDIR =	$(ROOTLIBDIR)
ROOTLINT =	$(LINTSRC:%=$(ROOTLINTDIR)/%)

CLEANFILES=	$(LLINTLIB)
CLOBBERFILES=	$(DATEFILE)

CPPFLAGS=	-I. -I$(SRCDIR) $(CPPFLAGS.master)
CPPFLAGS +=	-D_USERDEFS_INTERNAL
CERRWARN +=	-_gcc=-Wno-parentheses
CERRWARN +=	-_gcc=-Wno-type-limits
CERRWARN +=	-_gcc=-Wno-unused-variable
LDLIBS +=	-lproject -lc

ARFLAGS=	cr
AROBJS=		`$(LORDER) $(OBJS) | $(TSORT)`
LINTFLAGS=	-u

$(LINTLIB) :=	SRCS = ../common/llib-loamcmd

# CLOBBERFILES += $(LIBRARY)

.KEEP_STATE:

all:		$(LIBS)

lint:		lintcheck

$(LLINTLIB):	$(SRCS)
	$(LINT.c) -o $(LIBRARY:lib%.a=lib) $(SRCS) > $(LINTOUT) 2>&1

# include library targets
include ../../Makefile.targ
