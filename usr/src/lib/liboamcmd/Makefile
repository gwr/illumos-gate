#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#
#
# Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

include ../Makefile.lib

SUBDIRS = $(MACH)
# Not bothering with 64-bit libs for this.
# $(BUILD64)SUBDIRS += $(MACH64)

DATEFILE= 	ugdates
DATEFILESRC= 	ugdates.dat
TXT= 		$(DATEFILESRC)

ROOTUSRSADM=	$(ROOT)/usr/sadm
ROOTUSRSADMFILE=$(DATEFILE:%=$(ROOTUSRSADM)/%)

all :=		TARGET= all
clean :=	TARGET= clean
clobber :=	TARGET= clobber
install :=	TARGET= install
lint :=		TARGET= lint

.KEEP_STATE:

all clean clobber lint: $(SUBDIRS)
install: install_local  $(SUBDIRS)

install_local: $(ROOTUSRSADMFILE)

install_h:

check:

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

$(ROOTUSRSADM)/%: %
	$(INS.file)

$(DATEFILE):	$(DATEFILESRC)
	$(GREP) -v "^#ident" $(DATEFILESRC) > $(DATEFILE)

FRC:

include ../Makefile.targ
