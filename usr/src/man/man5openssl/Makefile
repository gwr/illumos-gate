#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet
# at http://www.illumos.org/license/CDDL.
#

#
# Copyright 2011, Richard Lowe
# Copyright 2015 Nexenta Systems, Inc.  All rights reserved.
#

include		$(SRC)/Makefile.master

MANSECT= 	5openssl

# XXX Get this from some included makefile?
OPENSSLDIR = $(CODEMGR_WS)/external/openssl/openssl-1.0.1h
OPENSSLVER = 1.0.1h

# MANFILES from section 5 files in OPENSSLDIR/{crypto,ssl}
# MANLINKS LINKSRC from util/extract-names.pl

MANFILES= \
	config.5openssl \
	x509v3_config.5openssl

MANLINKS=

.KEEP_STATE:

include		$(SRC)/man/Makefile.man

# These man pages are generated
CLOBBERFILES +=	$(MANFILES)

# Don't check openssl man pages
MANCHECKS=

all: $(MANFILES)

# build local MANFILES from *.pod files
%.$(MANSECT) : $(OPENSSLDIR)/doc/apps/%.pod
	(cd $(OPENSSLDIR)/doc/apps && ../../util/pod2man.pl \
	  --section=$(MANSECT) --center=OpenSSL \
	  --release=$(OPENSSLVER) $*.pod ) > $@

install:	$(ROOTMANFILES) $(ROOTMANLINKS)
