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
# Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

ASFLAGS += $(AS_PICFLAGS) -D_ASM

MACHOBJS = dis_tables.o
DLIBSRCS = regs.d

include ../Makefile.com

SRCS += $(SRC)/common/dis/i386/dis_tables.c
CPPFLAGS += -I$(SRC)/common/dis/i386

CLEANFILES += regs.d

regs.d: regs.d.in regs.gen.c
	$(COMPILE.c) -o regs.gen.i -D_KERNEL -E regs.gen.c
	nawk -f ../common/xyzzy2d.awk regs.gen.i \
	    regs.d.in > regs.tmp
	mv -f regs.tmp $@
	rm regs.gen.i

pics/%.o: $(SRC)/common/dis/i386/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

install yydebug: all $(ROOTLIBS) $(ROOTLINKS) \
	$(ROOTDLIBS) $(ROOTDOBJS)
