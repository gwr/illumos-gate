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

MANSECT= 	3openssl

# MANFILES from section 3 files in OPENSSLDIR/{crypto,ssl}
# MANLINKS LINKSRC from util/extract-names.pl

include		Makefile.crypto
include		Makefile.ssl

MANFILES= \
	$(MANFILES_all_crypto) \
	$(MANFILES_all_ssl)

MANLINKS= \
	$(MANLINKS_all_crypto) \
	$(MANLINKS_all_ssl)

.KEEP_STATE:

include		$(SRC)/man/Makefile.man

# These man pages are generated
CLOBBERFILES +=	$(MANFILES)

# Don't check openssl man pages
MANCHECKS=

all: $(MANFILES)

# build local MANFILES from *.pod files
%.$(MANSECT) : $(OPENSSLDIR)/doc/crypto/%.pod
	(cd $(OPENSSLDIR)/doc/crypto && ../../util/pod2man.pl \
	  --section=$(MANSECT) --center=OpenSSL \
	  --release=$(OPENSSL_VERSION) $*.pod ) > $@

%.$(MANSECT) : $(OPENSSLDIR)/doc/ssl/%.pod
	(cd $(OPENSSLDIR)/doc/ssl && ../../util/pod2man.pl \
	  --section=$(MANSECT) --center=OpenSSL \
	  --release=$(OPENSSL_VERSION) $*.pod ) > $@

install:	$(ROOTMANFILES) $(ROOTMANLINKS)
