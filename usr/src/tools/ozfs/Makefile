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

PYFILES = ozfs-import
MAN1ONBLDFILES = ozfs-import.1onbld
POLICYFILES = import-paths.conf

include ../Makefile.tools

ROOTPOLICY = $(ROOTONBLDETC)/ozfs
ROOTPOLICYFILES = $(POLICYFILES:%=$(ROOTPOLICY)/%)
$(ROOTPOLICYFILES) := FILEMODE = 0444
$(ROOTONBLDMAN1ONBLDFILES) := FILEMODE = 0644

.KEEP_STATE:

all: $(PYFILES) $(MAN1ONBLDFILES)

install: all .WAIT $(ROOTONBLDPYFILES) $(ROOTONBLDMAN1ONBLDFILES) \
	$(ROOTPOLICYFILES)

$(ROOTPOLICY)/%: %
	$(INS.file)

check:
	$(PYTHON3) -m unittest -v test_ozfs_import

clean:
	$(RM) $(PYFILES)

include ../Makefile.targ
