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
# Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
# Copyright (c) 2011, 2016 by Delphix. All rights reserved.
#
# Copyright (c) 2018, Joyent, Inc.

LIBRARY = libdtrace.a
VERS = .1

LIBSRCS = \
	dt_aggregate.c \
	dt_as.c \
	dt_buf.c \
	dt_cc.c \
	dt_cg.c \
	dt_consume.c \
	dt_decl.c \
	dt_dis.c \
	dt_dof.c \
	dt_error.c \
	dt_errtags.c \
	dt_sugar.c \
	dt_handle.c \
	dt_ident.c \
	dt_inttab.c \
	dt_link.c \
	dt_list.c \
	dt_open.c \
	dt_options.c \
	dt_program.c \
	dt_map.c \
	dt_module.c \
	dt_names.c \
	dt_parser.c \
	dt_pcb.c \
	dt_pid.c \
	dt_pq.c \
	dt_pragma.c \
	dt_print.c \
	dt_printf.c \
	dt_proc.c \
	dt_provider.c \
	dt_regset.c \
        dt_string.c \
	dt_strtab.c \
	dt_subr.c \
	dt_work.c \
	dt_xlator.c

LIBISASRCS = \
	dt_isadep.c

OBJECTS = dt_lex.o dt_grammar.o $(MACHOBJS) $(LIBSRCS:%.c=%.o) $(LIBISASRCS:%.c=%.o)

DRTISRCS = dlink_init.c dlink_common.c
DRTIOBJS = $(DRTISRCS:%.c=pics/%.o)
DRTIOBJ = drti.o

LIBDAUDITSRCS = dlink_audit.c dlink_common.c
LIBDAUDITOBJS = $(LIBDAUDITSRCS:%.c=pics/%.o)
LIBDAUDIT = libdtrace_forceload.so

DLINKSRCS = dlink_common.c dlink_init.c dlink_audit.c

# These are the generated *.d files.  See Makefile for the static ones.
# Note i386/Makefile sets this = regs.d (effectively prepended)
DLIBSRCS += \
	errno.d \
	io.d \
	ip.d \
	net.d \
	procfs.d \
	signal.d \
	sysevent.d \
	tcp.d \
	udp.d

include ../../Makefile.lib

CSTD = $(CSTD_GNU99)
SRCS = $(LIBSRCS:%.c=../common/%.c) $(LIBISASRCS:%.c=../$(MACH)/%.c)
LIBS = $(DYNLIB)

SRCDIR = ../common

CLEANFILES += dt_lex.c dt_grammar.c dt_grammar.h y.output
CLEANFILES += dt_errtags.c dt_names.c
CLEANFILES += $(DLIBSRCS)
CLEANFILES += $(LIBDAUDITOBJS) $(DRTIOBJS)

CLOBBERFILES += $(LIBDAUDIT) drti.o

CPPFLAGS += -I../common -I.
CFLAGS += $(CCVERBOSE) $(C_BIGPICFLAGS)
CFLAGS64 += $(CCVERBOSE) $(C_BIGPICFLAGS)

CERRWARN += -_gcc=-Wno-unused-variable
CERRWARN += -_gcc=-Wno-parentheses
CERRWARN += $(CNOWARN_UNINIT)
CERRWARN += -_gcc=-Wno-switch

# because of labels from yacc
pics/dt_grammar.o := CERRWARN += -_gcc=-Wno-unused-label
pics/dt_lex.o := CERRWARN += -_gcc=-Wno-unused-label

# not linted
SMATCH=off

YYCFLAGS =
LDLIBS += -lgen -lproc -lrtld_db -lnsl -lsocket -lctf -lelf -lc
DRTILDLIBS = $(LDLIBS.lib) -lc
LIBDAUDITLIBS = $(LDLIBS.lib) -lmapmalloc -lc -lproc $(LDSTACKPROTECT)

yydebug := YYCFLAGS += -DYYDEBUG

LFLAGS = -t -v
YFLAGS = -d -v

ROOTDLIBDIR = $(ROOT)/usr/lib/dtrace
ROOTDLIBDIR64 = $(ROOT)/usr/lib/dtrace/64

ROOTDLIBS = $(DLIBSRCS:%=$(ROOTDLIBDIR)/%)
ROOTDOBJS = $(ROOTDLIBDIR)/$(DRTIOBJ) $(ROOTDLIBDIR)/$(LIBDAUDIT)
ROOTDOBJS64 = $(ROOTDLIBDIR64)/$(DRTIOBJ) $(ROOTDLIBDIR64)/$(LIBDAUDIT)

#
# We do not build drti.o with the stack protector as otherwise
# everything that uses dtrace -G may have a surprise stack protector
# requirement right now. While in theory this could be handled by libc,
# this will make the overall default transition smoother.
#
# Need both DRTIOBJ, DRTIOBJS here or make rebuilds every time
# ouf of confusion over when this assignment applies.
#
$(DRTIOBJ) $(DRTIOBJS) := STACKPROTECT = none

$(ROOTDLIBDIR)/%.d := FILEMODE=444
$(ROOTDLIBDIR)/%.o := FILEMODE=444
$(ROOTDLIBDIR64)/%.o :=	FILEMODE=444
$(ROOTDLIBDIR)/%.so := FILEMODE=555
$(ROOTDLIBDIR64)/%.so := FILEMODE=555

.KEEP_STATE:

all: $(LIBS) $(DRTIOBJ) $(LIBDAUDIT) $(DLIBSRCS)

genlibs: $(DLIBSRCS)

rootlibs: $(ROOTDLIBS)

dt_lex.c: $(SRCDIR)/dt_lex.l dt_grammar.h
	$(LEX) $(LFLAGS) $(SRCDIR)/dt_lex.l > $@

dt_grammar.c dt_grammar.h: $(SRCDIR)/dt_grammar.y
	$(YACC) $(YFLAGS) $(SRCDIR)/dt_grammar.y
	@mv y.tab.h dt_grammar.h
	@mv y.tab.c dt_grammar.c

pics/dt_lex.o pics/dt_grammar.o := CFLAGS += $(YYCFLAGS)
pics/dt_lex.o pics/dt_grammar.o := CFLAGS64 += $(YYCFLAGS)

dt_errtags.c: ../common/mkerrtags.sh ../common/dt_errtags.h
	sh ../common/mkerrtags.sh < ../common/dt_errtags.h > $@

dt_names.c: ../common/mknames.sh $(SRC)/uts/common/sys/dtrace.h
	sh ../common/mknames.sh < $(SRC)/uts/common/sys/dtrace.h > $@

errno.d: ../common/mkerrno.sh $(SRC)/uts/common/sys/errno.h
	sh ../common/mkerrno.sh < $(SRC)/uts/common/sys/errno.h > $@

signal.d: ../common/mksignal.sh $(SRC)/uts/common/sys/iso/signal_iso.h
	sh ../common/mksignal.sh < $(SRC)/uts/common/sys/iso/signal_iso.h > $@


%.d: ../common/%.d.in ../common/%.gen.c
	$(COMPILE.c) -o $*.gen.i -D_KERNEL -E ../common/$*.gen.c
	nawk -f ../common/xyzzy2d.awk $*.gen.i \
	    ../common/$*.d.in > $*.tmp
	mv -f $*.tmp $*.d
	rm $*.gen.i

pics/%.o: ../$(MACH)/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: ../$(MACH)/%.s
	$(COMPILE.s) -o $@ $<
	$(POST_PROCESS_O)

$(DRTIOBJ): $(DRTIOBJS)
	$(LD) -o $@ -r $(BLOCAL) $(BREDUCE) $(DRTIOBJS)
	$(POST_PROCESS_O)

$(LIBDAUDIT): $(LIBDAUDITOBJS)
	$(LINK.c) -o $@ $(GSHARED) -Wl,-h$(LIBDAUDIT) $(ZTEXT) $(ZDEFS) \
	    $(BDIRECT) $(MAPFILE.PGA:%=-Wl,-M%) $(MAPFILE.NED:%=-Wl,-M%) \
	    $(LIBDAUDITOBJS) $(LIBDAUDITLIBS)
	$(POST_PROCESS_SO)

$(ROOTDLIBDIR):
	$(INS.dir)

$(ROOTDLIBDIR64): $(ROOTDLIBDIR)
	$(INS.dir)

$(ROOTDLIBDIR)/%.d: %.d
	$(INS.file)

$(ROOTDLIBDIR)/%.o: %.o
	$(INS.file)

$(ROOTDLIBDIR64)/%.o: %.o
	$(INS.file)

$(ROOTDLIBDIR)/%.so: %.so
	$(INS.file)

$(ROOTDLIBDIR64)/%.so: %.so
	$(INS.file)

$(ROOTDLIBS): $(ROOTDLIBDIR)

$(ROOTDOBJS): $(ROOTDLIBDIR)

$(ROOTDOBJS64): $(ROOTDLIBDIR64)

include ../../Makefile.targ
