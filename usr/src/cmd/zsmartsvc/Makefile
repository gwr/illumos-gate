# Copyright 2009-2014 RackTop Systems LLC and/or its affiliates.
# http://www.racktopsystems.com
#
# The contents of this file are subject to the terms of the RackTop Commercial
# License ("RTCL"). You may not use, redistribute, or modify this file in source
# or binary forms except in compliance with the RTCL. You can obtain a copy at

PROG=		zsmartsvc
OBJS=		zsmartsvc_main.o
SRCS=		$(OBJS:%.o=%.c)
# 
# POFILES=	zsmartsvc_main.po
# POFILE=	zsmartsvc.po
# 

include ../Makefile.cmd
include ../Makefile.ctf

LDLIBS += -lzfs -lzfs_core

INCS += -I../../common/zfs -I../../uts/common/fs/zfs

C99MODE=	-xc99=%all
C99LMODE=	-Xc99=%all

CPPFLAGS += -D_LARGEFILE64_SOURCE=1 -D_REENTRANT $(INCS)
$(NOT_RELEASE_BUILD)CPPFLAGS += -DDEBUG

# lint complains about unused _umem_* functions
LINTFLAGS += -xerroff=E_NAME_DEF_NOT_USED2 
LINTFLAGS64 += -xerroff=E_NAME_DEF_NOT_USED2 

CERRWARN += -_gcc=-Wno-switch
CERRWARN += -_gcc=-Wno-type-limits
CERRWARN += -_gcc=-Wno-parentheses
CERRWARN += -_gcc=-Wno-uninitialized
CERRWARN += -_gcc=-Wno-old-style-declaration

ROOTUSRSBINLINKS = $(PROG:%=$(ROOTUSRSBIN)/%)

.KEEP_STATE:

.PARALLEL:

all: $(PROG)

$(PROG): $(OBJS)
	$(LINK.c) -o $@ $(OBJS) $(LDLIBS)
	$(POST_PROCESS)

install: all $(ROOTSBINPROG) $(ROOTUSRSBINLINKS)

$(POFILE): $(POFILES)
	$(RM) $@
	cat $(POFILES) > $@

clean:
	$(RM) $(OBJS)

lint:	lint_SRCS

# Links from /usr/sbin to /sbin
$(ROOTUSRSBINLINKS):
	-$(RM) $@; $(SYMLINK) ../../sbin/$(PROG) $@

FRC:

include ../Makefile.targ
