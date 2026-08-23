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

.PHONY: all test clean smoke-test assertion-events-test $(REFS)

all: test

test: smoke-test $(REFS) assertion-events-test

smoke-test:
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

events.ref: events.c
	$(LOCKLINT) --dump-events events.c > events.out
	diff -u events.ref events.out

annotations.ref: annotations.c
	$(LOCKLINT) --dump-annotations annotations.c > annotations.out
	diff -u annotations.ref annotations.out

$(CHECK_REFS): %.ref: %.c
	$(LOCKLINT) --check-locks $< > $*.out 2>&1
	diff -u $@ $*.out

cross.ref: cross-caller.c cross-callee.c cross.h
	$(LOCKLINT) --check-locks cross-caller.c cross-callee.c \
	    > cross.out 2>&1
	diff -u cross.ref cross.out

assertion-events-test: assertions.c
	$(LOCKLINT) --check-locks --dump-events assertions.c \
	    > assertion-events.out 2>&1
	grep -q 'CALL mutex_owned' assertion-events.out
	! grep -q 'CALL assfail' assertion-events.out

clean:
	rm -f parsed.out linearized.out accesses.out annotations.out \
	    assertion-events.out assertions.out calls.out check.out cross.out \
	    effects.out events.out user-mutex.out
