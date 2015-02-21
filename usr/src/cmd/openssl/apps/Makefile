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
# Copyright 2015 Nexenta Systems, Inc.  All rights reserved.
#
PROG=		openssl
SHFILES=	CA.pl

#
# X_OBJ From the generated apps/Makefile
#
A_OBJ=apps.o
S_OBJ=	s_cb.o s_socket.o
RAND_OBJ=app_rand.o
E_OBJ=	verify.o asn1pars.o req.o dgst.o dh.o dhparam.o enc.o passwd.o \
	gendh.o errstr.o ca.o pkcs7.o crl2p7.o crl.o \
	rsa.o rsautl.o dsa.o dsaparam.o ec.o ecparam.o \
	x509.o genrsa.o gendsa.o genpkey.o s_server.o s_client.o speed.o \
	s_time.o $(A_OBJ) $(S_OBJ) $(RAND_OBJ) version.o sess_id.o \
	ciphers.o nseq.o pkcs12.o pkcs8.o pkey.o pkeyparam.o pkeyutl.o \
	spkac.o smime.o cms.o rand.o engine.o ocsp.o prime.o ts.o srp.o

OBJS=		openssl.o $(E_OBJ)
SRCS=

include ../../Makefile.cmd

# XXX Get this from some included makefile?
OPENSSLDIR = $(CODEMGR_WS)/external/openssl/openssl-1.0.1h
SRCDIR = $(OPENSSLDIR)

CLOBBERFILES=	$(PROG) $(SHFILES) $(COPIES)

CPPFLAGS +=	-I$(SRCDIR)
CPPFLAGS +=	-DMONOLITH
LDLIBS +=	-R'$$ORIGIN/../../lib'
LDLIBS +=	-lssl -lcrypto -lsocket -lnsl

CCVERBOSE=
# Gak.  apps.c IMPLEMENT_LHASH_* etc.
apps.o := CERRWARN +=	-erroff=E_ASSIGNMENT_TYPE_MISMATCH
CERRWARN +=	-_gcc=-Wno-uninitialized
CERRWARN +=	-_gcc=-Wno-unused-variable

# install rules
$(ROOTINC)/% : %
	$(INS.file)

# A few things we need to copy here for ../test
COPIES= CA.sh openssl.cnf server2.pem
COPYHERE= $(RM) $@; cp $(OPENSSLDIR)/apps/$@ .

all:	$(PROG) $(SHFILES) $(COPIES)

openssl:	$(OBJS)
	$(LINK.c) $(OBJS) -o $@ $(LDLIBS)
	$(POST_PROCESS)

%.o: $(SRCDIR)/apps/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

CA.pl: $(OPENSSLDIR)/apps/CA.pl
	$(COPYHERE)

CA.sh: $(OPENSSLDIR)/apps/CA.sh
	$(COPYHERE)

openssl.cnf: $(OPENSSLDIR)/apps/openssl.cnf
	$(COPYHERE)

server2.pem: $(OPENSSLDIR)/apps/server2.pem
	$(COPYHERE)

install: all $(ROOTPROG) $(ROOTSHFILES) \
	$(ROOTETC)/openssl/openssl.cnf

$(ROOTETC)/openssl/openssl.cnf := FILEMODE = 0644
$(ROOTETC)/openssl/%.cnf : %.cnf
	$(INS.file)

lint:

clean:
	$(RM) $(OBJS)

.KEEP_STATE:

include ../../Makefile.targ
