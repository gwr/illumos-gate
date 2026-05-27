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
# Copyright 2026 Gordon W. Ross
#

# Generates .kstats files from .py specs; runs tests against intrd-test.
# To test from the proto area, set INTRD to the full path of the binary.

TESTS = case01 case02 case03 case04 case05 case06 case07 \
        case08 case09 case10 case11 case12 case13 case14 \
        case15 case16 case17 case18 case19

KSTATS = $(TESTS:%=%.kstats)

all: $(KSTATS)

clean:
	-rm -f case??.out case??.kstats

clobber: clean

test: all FRC
	sh run-tests.sh

FRC:

%.kstats : %.py
	python3 gen_case.py $< > $@
