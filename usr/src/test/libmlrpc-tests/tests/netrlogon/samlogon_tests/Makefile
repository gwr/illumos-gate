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

include $(SRC)/Makefile.master

TESTSUBDIR = netrlogon/samlogon_tests
TESTCOMMONDIR = ../../common
PROG = samlogon
KSHPROG = \
	run_samlogon_tests \
	run_samlogon_aes_seal \
	run_samlogon_aes_sign \
	run_samlogon_rc4_seal \
	run_samlogon_rc4_sign

include ../../Makefile.com

LDFLAGS += -R/usr/lib/smbsrv
LDLIBS += -L$(ROOT)/usr/lib/smbsrv
LDLIBS += -lmlsvc -lsmb
CPPFLAGS += -Dsyslog=smb_syslog
CPPFLAGS += -I$(SRC)/lib/smbsrv/libmlsvc/common
