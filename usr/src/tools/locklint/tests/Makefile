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

LOCKLINT = ../locklint

CHECK_REFS = check.ref calls.ref effects.ref assertions.ref user-mutex.ref
REFS = events.ref annotations.ref $(CHECK_REFS) cross.ref

all: test

test: smoke-test $(REFS) assertion-events-test

smoke-test: FRC
	$(LOCKLINT) --dump-parsed smoke.c > parsed.out
	grep -q 'smoke' parsed.out
	$(LOCKLINT) --dump-linearized smoke.c > linearized.out
	grep -Eq 'load|store' linearized.out
	$(LOCKLINT) --dump-accesses smoke.c > accesses.out
	grep -q 'load arg.nested.value ' accesses.out
	grep -q 'store global_smoke.head ' accesses.out
	grep -q 'store local.values ' accesses.out
	grep -q 'load arg.next.value ' accesses.out
	grep -q 'store static_value ' accesses.out

events.ref: events.c FRC
	$(LOCKLINT) --dump-events events.c > events.out
	diff -u events.ref events.out

annotations.ref: annotations.c FRC
	$(LOCKLINT) --dump-annotations annotations.c > annotations.out
	diff -u annotations.ref annotations.out

cross.ref: cross-caller.c cross-callee.c cross.h FRC
	$(LOCKLINT) --check-locks cross-caller.c cross-callee.c \
	    > cross.out 2>&1
	diff -u cross.ref cross.out

assertion-events-test: assertions.c FRC
	$(LOCKLINT) --check-locks --dump-events assertions.c \
	    > assertion-events.out 2>&1
	grep -q 'CALL mutex_owned' assertion-events.out
	if grep -q 'CALL assfail' assertion-events.out; then exit 1; fi

%.ref: %.c FRC
	$(LOCKLINT) --check-locks $< > $*.out 2>&1
	diff -u $@ $*.out

clean: FRC
	rm -f parsed.out linearized.out accesses.out annotations.out \
	    assertion-events.out assertions.out calls.out check.out cross.out \
	    effects.out events.out user-mutex.out

FRC:
