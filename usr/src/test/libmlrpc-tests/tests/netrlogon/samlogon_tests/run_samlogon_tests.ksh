#!/usr/bin/ksh

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
# Copyright 2020 Tintri by DDN, Inc. All rights reserved.
# Copyright 2026 RackTop Systems Inc.
#

if [[ -z $MLRPC_TESTS ]]; then
	echo "MLRPC_TESTS not set" >&2
	exit 1
fi

if (( $# != 1 )); then
	echo "usage: $0 <aes-seal|aes-sign|rc4-seal|rc4-sign>" >&2
	exit 2
fi

. $MLRPC_TESTS/cfg/samlogon.config

exec "$MLRPC_TESTS/tests/netrlogon/samlogon_tests/samlogon" \
    "$NETBIOS_DOMAIN" "$DC_FQDN" "$USERNAME" "$CLIENT_COMPUTER" \
    "$CHALLENGE_FILE" "$NT_RESPONSE_FILE" "$LM_RESPONSE_FILE" "$1"
