#!/bin/ksh
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
# Run the checked-in real-module comparisons for new locklint.  Add modules
# here only after their individual comparison has been accepted.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)

"$SCRIPT_DIR/check-kmod-wc.ksh" || exit $?
"$SCRIPT_DIR/check-kmod-ugen.ksh" || exit $?
