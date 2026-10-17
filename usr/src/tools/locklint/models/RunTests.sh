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
# Run focused integration tests for the supplied locklint models.
#

SCRIPT_DIR=$(CDPATH= cd "$(dirname "$0")" && pwd)

set -e
"$SCRIPT_DIR/check-kmod-ddi-blocking.ksh"
"$SCRIPT_DIR/check-kmod-ddi-callbacks.ksh"
"$SCRIPT_DIR/check-kmod-ddi-forwarding.ksh"
"$SCRIPT_DIR/check-kmod-streams-dispatch.ksh"
"$SCRIPT_DIR/check-model-streams-perimeter.ksh"
"$SCRIPT_DIR/check-model-streams-service.ksh"
"$SCRIPT_DIR/check-model-streams-openclose.ksh"
"$SCRIPT_DIR/check-model-streams-qwriter.ksh"
"$SCRIPT_DIR/check-model-streams-qproc.ksh"
"$SCRIPT_DIR/check-model-streams-qproc-close.ksh"
