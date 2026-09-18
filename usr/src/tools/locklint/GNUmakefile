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
# This is derived from Makefile with just enough here so one can
# compile and run tests on MacOS (and maybe Linux)
#
# There are just enough macros here to allow most of the body
# of this file to look like the illumos Makefile.
#

PROG = locklint

SPARSE_SRCDIR = ../smatch/src
SPARSE_VERSION = 0.6.1-rc1-il-7-locklint

# stuff in place of illumos includes
CC ?= cc
CFLAGS ?= -O0 -g
CFLAGS += -std=gnu99 -fno-strict-aliasing -Wall -Wno-switch
CPPFLAGS += -I$(SPARSE_SRCDIR) -I.
CPPFLAGS += -DGCC_BASE=\"$(shell $(CC) --print-file-name= 2>/dev/null)\"
CPPFLAGS += -DMULTIARCH_TRIPLET=NULL
LDLIBS += -lm
COMPILE.c=	$(CC) $(CFLAGS) $(CPPFLAGS) -c
LINK.c=		$(CC) $(CFLAGS) $(LDFLAGS) $(LDCHECKS)

####  Same as Makefile after here ####

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
	command_parse.o \
	commands.o \
	diagnostics.o \
	events.o \
	identity.o \
	lock_order.o \
	context.o \
	dependency.o \
	provenance.o \
	worklist.o \
	avl.o

OBJS = $(LOCAL_OBJS) $(SPARSE_OBJS) hashtable.o

all: $(PROG)

$(OBJS): version.h

$(PROG): $(OBJS)
	$(LINK.c) -o $@ $(OBJS) $(LDLIBS)

LIBDEFS = -Dinit_include_path=locklint_init_include_path
lib.o: $(SPARSE_SRCDIR)/lib.c
	$(COMPILE.c) $(LIBDEFS) -o $@ $(SPARSE_SRCDIR)/lib.c

%.o: %.c
	$(COMPILE.c) -o $@ $<

%.o: $(SPARSE_SRCDIR)/%.c
	$(COMPILE.c) -o $@ $<

%.o: $(SPARSE_SRCDIR)/cwchash/%.c
	$(COMPILE.c) -o $@ $<

version.h:
	echo '#define SPARSE_VERSION "$(SPARSE_VERSION)"' > $@

CMD_PARSE_TEST_OBJS = command_parse.o command_parse_test.o
command_parse_test: $(CMD_PARSE_TEST_OBJS)
	$(LINK.c) -o $@ $(CMD_PARSE_TEST_OBJS) $(LDLIBS)

CTX_TEST_OBJS = context_test.o context.o avl.o
context_test: $(CTX_TEST_OBJS)
	$(LINK.c) -o $@ $(CTX_TEST_OBJS) $(LDLIBS)

WORKLIST_TEST_OBJS = worklist_test.o worklist.o
worklist_test: $(WORKLIST_TEST_OBJS)
	$(LINK.c) -o $@ $(WORKLIST_TEST_OBJS) $(LDLIBS)

DEP_TEST_OBJS = dependency_test.o dependency.o context.o avl.o
dependency_test: $(DEP_TEST_OBJS)
	$(LINK.c) -o $@ $(DEP_TEST_OBJS) $(LDLIBS)

PROV_TEST_OBJS = provenance_test.o provenance.o context.o avl.o
provenance_test: $(PROV_TEST_OBJS)
	$(LINK.c) -o $@ $(PROV_TEST_OBJS) $(LDLIBS)

test: $(PROG) FRC
	(cd tests && ./RunTests.sh)

UNITTESTS = command_parse_test context_test worklist_test dependency_test \
	provenance_test
unittest: $(UNITTESTS) FRC
	./command_parse_test command_parse_test.in
	./context_test
	./worklist_test
	./dependency_test
	./provenance_test

clean: FRC
	$(RM) -f $(PROG) version.h *.o
	$(RM) -f $(UNITTESTS)
	$(RM) -f tests/*.out tests/*.raw

FRC:

.PHONY: all test clean unittest FRC
