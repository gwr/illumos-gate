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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# Copyright 2013 Nexenta Systems, Inc.  All rights reserved.
#

default: all

include Makefile.ncp3

SUBDIRS =	librtld_db ncp3_npreload ncp3_brand ncp3_support zone cmd
MSGSUBDIRS =	ncp3_support zone

all :=		TARGET= all
install :=	TARGET= install
clean :=	TARGET= clean
clobber :=	TARGET= clobber
lint :=		TARGET= lint
_msg :=		TARGET= _msg

INSDIRS = $(ETCBRANDDIR) $(ROOTBRANDDIR) $(ROOTBRANDDIR64)
$(BUILD64) INSDIRS += $(ROOTBRANDDIR)/64

.KEEP_STATE:

all clean clobber lint: $(SUBDIRS)

install: insdirs .WAIT $(SUBDIRS)

_msg: $(MSGSUBDIRS)

insdirs: $(INSDIRS)

$(ETCBRANDDIR) $(ROOTBRANDDIR) $(ROOTBRANDDIR64):
	$(INS.dir)

$(ROOTBRANDDIR)/64:
	-$(RM) -r $@; $(SYMLINK) $(MACH64) $@

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:
