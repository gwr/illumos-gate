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
# Copyright 2016 Gordon W. Ross
#

# OAM User Test program
PROG=		oamut

include ../../../cmd/Makefile.cmd

OBJECTS=	oamut.o
CPPFLAGS=	-I. -I../common $(CPPFLAGS.master)
LDLIBS +=	-loamcmd

all:		$(PROG)

$(PROG):	$(OBJECTS)
	$(LINK.c) $(OBJECTS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

install: $(PROG)

clean:
	$(RM) $(OBJECTS)

lint:	lint_PROG

.KEEP_STATE:

include ../../../cmd/Makefile.targ
