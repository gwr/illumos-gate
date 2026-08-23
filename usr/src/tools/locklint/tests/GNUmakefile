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

LOCKLINT = ../locklint

.PHONY: all test clean

all: test

test:
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
	$(LOCKLINT) --dump-events events.c > events.out
	diff -u events.ref events.out
	$(LOCKLINT) --dump-annotations annotations.c > annotations.out
	diff -u annotations.ref annotations.out
	$(LOCKLINT) --check-locks check.c > check.out 2>&1
	diff -u check.ref check.out
	$(LOCKLINT) --check-locks calls.c > calls.out 2>&1
	diff -u calls.ref calls.out
	$(LOCKLINT) --check-locks effects.c > effects.out 2>&1
	diff -u effects.ref effects.out

clean:
	rm -f parsed.out linearized.out accesses.out annotations.out calls.out \
	    check.out effects.out events.out
