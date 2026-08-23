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

PROG = locklint
SPARSE = ../smatch/src

CC ?= cc
CFLAGS ?= -O0 -g
CFLAGS += -std=gnu99 -fno-strict-aliasing -Wall -Wno-switch
CPPFLAGS += -I$(SPARSE) -I.
CPPFLAGS += -DGCC_BASE=\"$(shell $(CC) --print-file-name= 2>/dev/null)\"
CPPFLAGS += -DMULTIARCH_TRIPLET=NULL
LDLIBS += -lm

SPARSE_SRCS = \
	allocate.c \
	builtin.c \
	char.c \
	compat-bsd.c \
	cse.c \
	dissect.c \
	dominate.c \
	evaluate.c \
	expand.c \
	expression.c \
	flow.c \
	flowgraph.c \
	inline.c \
	ir.c \
	lib.c \
	linearize.c \
	liveness.c \
	macro_table.c \
	memops.c \
	opcode.c \
	optimize.c \
	parse.c \
	pre-process.c \
	ptrlist.c \
	ptrmap.c \
	scope.c \
	show-parse.c \
	simplify.c \
	sort.c \
	ssa.c \
	sset.c \
	stats.c \
	storage.c \
	symbol.c \
	target.c \
	token_store.c \
	tokenize.c \
	unssa.c \
	utils.c

LOCAL_OBJS = locklint.o access.o annotations.o assertions.o check.o events.o
SPARSE_OBJS = $(SPARSE_SRCS:%.c=%.o) hashtable.o
OBJS = $(LOCAL_OBJS) $(SPARSE_OBJS)

.PHONY: all test clean

all: $(PROG)

$(OBJS): version.h

$(PROG): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(LOCAL_OBJS): %.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

%.o: $(SPARSE)/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

lib.o: $(SPARSE)/lib.c
	$(CC) $(CPPFLAGS) $(CFLAGS) \
	    -Dinit_include_path=locklint_init_include_path -c -o $@ $<

hashtable.o: $(SPARSE)/cwchash/hashtable.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

version.h:
	@printf '%s\n' '#define SPARSE_VERSION "locklint prototype"' > $@

test: $(PROG)
	$(MAKE) -C tests test

clean:
	rm -f $(OBJS) version.h $(PROG)
	$(MAKE) -C tests clean
