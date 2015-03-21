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

MANSECT= 	1openssl

# MANFILES from section 1 files in OPENSSLDIR/{crypto,ssl}
# MANLINKS LINKSRC from util/extract-names.pl

# Note, these go in man5openssl: config x509v3_config
MANFILES= \
	CA.pl.1openssl \
	asn1parse.1openssl \
	c_rehash.1openssl \
	ca.1openssl \
	ciphers.1openssl \
	cms.1openssl \
	crl.1openssl \
	crl2pkcs7.1openssl \
	dgst.1openssl \
	dhparam.1openssl \
	dsa.1openssl \
	dsaparam.1openssl \
	ec.1openssl \
	ecparam.1openssl \
	enc.1openssl \
	errstr.1openssl \
	gendsa.1openssl \
	genpkey.1openssl \
	genrsa.1openssl \
	nseq.1openssl \
	ocsp.1openssl \
	openssl.1openssl \
	passwd.1openssl \
	pkcs12.1openssl \
	pkcs7.1openssl \
	pkcs8.1openssl \
	pkey.1openssl \
	pkeyparam.1openssl \
	pkeyutl.1openssl \
	rand.1openssl \
	req.1openssl \
	rsa.1openssl \
	rsautl.1openssl \
	s_client.1openssl \
	s_server.1openssl \
	s_time.1openssl \
	sess_id.1openssl \
	smime.1openssl \
	speed.1openssl \
	spkac.1openssl \
	ts.1openssl \
	tsget.1openssl \
	verify.1openssl \
	version.1openssl \
	x509.1openssl

MANLINKS_dgst= \
	sha.1openssl \
	sha1.1openssl \
	mdc2.1openssl \
	ripemd160.1openssl \
	sha224.1openssl \
	sha256.1openssl \
	sha384.1openssl \
	sha512.1openssl \
	md2.1openssl \
	md4.1openssl \
	md5.1openssl \
	dss1.1openssl

$(MANLINKS_dgst) := LINKSRC = dgst.1openssl

MANLINKS= \
	$(MANLINKS_dgst)


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
	  --release=$(OPENSSL_VERSION) $*.pod ) > $@

install:	$(ROOTMANFILES) $(ROOTMANLINKS)
