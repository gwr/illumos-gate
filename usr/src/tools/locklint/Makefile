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
# Copyright 2026 Gordon W. Ross
#

#
# This program builds upon the features of the "sparse"library over in
# ../smatch/src/ (See README, Documentation/sparse-README.txt)
#
# For illumos we maintain a branch of that code here:
# https://github.com/illumos/smatch/tree/$SPARSE_VERSION
#
# This Makefile builds what it needs from sparse locally.
#

PROG = locklint
SPARSE_SRCDIR = ../smatch/src
SPARSE_VERSION = 0.6.1-rc1-il-7-locklint

include ../Makefile.tools
include ../../Makefile.master.64

CSTD = $(CSTD_GNU99)
CPPFLAGS += -I. -I$(SPARSE_SRCDIR)
CPPFLAGS += -DGCC_BASE='"/no/such/dir"'
CPPFLAGS += -DMULTIARCH_TRIPLET=NULL

CERRWARN += -_gcc=-Wno-implicit-fallthrough

# not linted
SMATCH=off

SPARSE_OBJS = \
	allocate.o \
	builtin.o \
	char.o \
	compat-bsd.o \
	cse.o \
	dissect.o \
	dominate.o \
	evaluate.o \
	expand.o \
	expression.o \
	flow.o \
	flowgraph.o \
	inline.o \
	ir.o \
	lib.o \
	linearize.o \
	liveness.o \
	macro_table.o \
	memops.o \
	opcode.o \
	optimize.o \
	parse.o \
	pre-process.o \
	ptrlist.o \
	ptrmap.o \
	scope.o \
	show-parse.o \
	simplify.o \
	sort.o \
	ssa.o \
	sset.o \
	stats.o \
	storage.o \
	symbol.o \
	target.o \
	token_store.o \
	tokenize.o \
	unssa.o \
	utils.o

LOCAL_OBJS = \
	locklint.o \
	access.o \
	annotations.o \
	assertions.o \
	callgraph.o \
	check.o \
	events.o \
	identity.o \
	lock_order.o \
	avl.o

OBJS = $(LOCAL_OBJS) $(SPARSE_OBJS) hashtable.o

all: $(PROG)

$(OBJS): version.h

$(PROG): $(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

lib.o := CPPFLAGS += -Dinit_include_path=locklint_init_include_path

%.o: %.c
	$(COMPILE.c) -o $@ $<

%.o: $(SPARSE_SRCDIR)/%.c
	$(COMPILE.c) -o $@ $<

%.o: $(SPARSE_SRCDIR)/cwchash/%.c
	$(COMPILE.c) -o $@ $<

version.h:
	echo '#define SPARSE_VERSION "$(SPARSE_VERSION)"' > $@

test: $(PROG)
	(cd tests && ./RunTests.sh)

clean: FRC
	$(RM) -f $(OBJS) version.h $(PROG)
	$(RM) -f tests/*.out tests/*.raw

FRC:

.KEEP_STATE:
