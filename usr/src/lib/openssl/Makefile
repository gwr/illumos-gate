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
# Copyright 2015 Gary Mills
#

include ../Makefile.lib

OPENSSLDIR = $(CODEMGR_WS)/external/openssl/openssl-1.0.1h

# Note intentional omission: opensslconf.h
# This has its own rule below.
HDRS= \
	aes.h \
	asn1.h \
	asn1_mac.h \
	asn1t.h \
	bio.h \
	blowfish.h \
	bn.h \
	buffer.h \
	camellia.h \
	cast.h \
	cmac.h \
	cms.h \
	comp.h \
	conf.h \
	conf_api.h \
	crypto.h \
	des.h \
	des_old.h \
	dh.h \
	dsa.h \
	dso.h \
	dtls1.h \
	e_os2.h \
	ebcdic.h \
	ec.h \
	ecdh.h \
	ecdsa.h \
	engine.h \
	err.h \
	evp.h \
	hmac.h \
	idea.h \
	krb5_asn.h \
	kssl.h \
	lhash.h \
	md2.h \
	md4.h \
	md5.h \
	mdc2.h \
	modes.h \
	obj_mac.h \
	objects.h \
	ocsp.h \
	opensslv.h \
	ossl_typ.h \
	pem.h \
	pem2.h \
	pkcs12.h \
	pkcs7.h \
	pqueue.h \
	rand.h \
	rc2.h \
	rc4.h \
	ripemd.h \
	rsa.h \
	safestack.h \
	seed.h \
	sha.h \
	srp.h \
	srtp.h \
	ssl.h \
	ssl2.h \
	ssl23.h \
	ssl3.h \
	stack.h \
	symhacks.h \
	tls1.h \
	ts.h \
	txt_db.h \
	ui.h \
	ui_compat.h \
	whrlpool.h \
	x509.h \
	x509_vfy.h \
	x509v3.h

HDRDIR =	$(OPENSSLDIR)/include/openssl
ROOTHDRDIR =    $(ROOT)/usr/include/openssl

SUBDIRS = \
	libcrypto	\
	libssl	\
	libpk11

# Serialize
libssl:	libcrypto
libpk11:	libcrypto

all 	:=	TARGET= all
clean 	:=	TARGET= clean
clobber	:=	TARGET= clobber
install :=	TARGET= install
lint :=		TARGET= lint

.KEEP_STATE:

all clean clobber lint: $(SUBDIRS)

install: $(SUBDIRS) $(ROOTLIBDIR)/pkgconfig/openssl.pc

install_h: $(ROOTHDRDIR) $(ROOTHDRS) \
	$(ROOTHDRDIR)/opensslconf.h \
	$(ROOTHDRDIR)/sunw_prefix.h

#
# Perhaps should generate this using a patch applied to
# external/openssl/openssl-1.0.1h/crypto/opensslconf.h
# This was edited for 32/64-bit support and committed.
#
$(ROOTHDRDIR)/%: libcrypto/common/%
	$(INS.file)

# Suppress header checking
check:

$(ROOTHDRDIR):
	$(INS.dir)

$(ROOTLIBDIR)/pkgconfig/% : %
	$(INS.file)

$(SUBDIRS): FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:

include ../Makefile.targ
