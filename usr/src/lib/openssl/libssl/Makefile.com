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

LBASE=	libssl
LPREF=  libsunw_ssl
LIBRARY= $(LPREF).a
VERS= .1

OBJECTS= \
	s2_meth.o  s2_srvr.o  s2_clnt.o  s2_lib.o  s2_enc.o s2_pkt.o \
	s3_meth.o  s3_srvr.o  s3_clnt.o  s3_lib.o  s3_enc.o s3_pkt.o s3_both.o s3_cbc.o \
	s23_meth.o s23_srvr.o s23_clnt.o s23_lib.o          s23_pkt.o \
	t1_meth.o   t1_srvr.o t1_clnt.o  t1_lib.o  t1_enc.o \
	d1_meth.o   d1_srvr.o d1_clnt.o  d1_lib.o  d1_pkt.o \
	d1_both.o d1_enc.o d1_srtp.o\
	ssl_lib.o ssl_err2.o ssl_cert.o ssl_sess.o \
	ssl_ciph.o ssl_stat.o ssl_rsa.o \
	ssl_asn1.o ssl_txt.o ssl_algs.o \
	bio_ssl.o ssl_err.o kssl.o tls_srp.o t1_reneg.o


include ../../../Makefile.lib
include ../../../Makefile.rootfs


SRCS=	\
	s2_meth.c   s2_srvr.c s2_clnt.c  s2_lib.c  s2_enc.c s2_pkt.c \
	s3_meth.c   s3_srvr.c s3_clnt.c  s3_lib.c  s3_enc.c s3_pkt.c s3_both.c s3_cbc.c \
	s23_meth.c s23_srvr.c s23_clnt.c s23_lib.c          s23_pkt.c \
	t1_meth.c   t1_srvr.c t1_clnt.c  t1_lib.c  t1_enc.c \
	d1_meth.c   d1_srvr.c d1_clnt.c  d1_lib.c  d1_pkt.c \
	d1_both.c d1_enc.c d1_srtp.c \
	ssl_lib.c ssl_err2.c ssl_cert.c ssl_sess.c \
	ssl_ciph.c ssl_stat.c ssl_rsa.c \
	ssl_asn1.c ssl_txt.c ssl_algs.c \
	bio_ssl.c ssl_err.c kssl.c tls_srp.c t1_reneg.c

SRCDIR=		../common
CRSRCDIR=	../../libcrypto/common
INCDIR=		../../include

LIBS =		$(DYNLIB) $(LINTLIB)
$(LINTLIB) :=	SRCS = $(SRCDIR)/$(LINTSRC)
LDLIBS +=	-lcrypto -lc

# Omit the prefix for the link target but retain it for the library file
ROOTLINKS=	$(ROOTLIBDIR)/$(LBASE).so
ROOTLINKS64=	$(ROOTLIBDIR64)/$(LBASE).so

$(ROOTLINKS): $(ROOTLIBDIR)/$(LIBLINKS)$(VERS)
	$(INS.liblink)

$(ROOTLINKS64): $(ROOTLIBDIR64)/$(LIBLINKS)$(VERS)
	$(INS.liblink64)

CFLAGS	+=	$(CCVERBOSE)
CPPFLAGS +=	-I$(INCDIR) -I$(SRCDIR) -I$(CRSRCDIR)
CPPFLAGS +=	\
	-D_REENTRANT	\
	-DAES_ASM	\
	-DDSO_DLFCN	\
	-DGHASH_ASM	\
	-DHAVE_DLFCN_H	\
	-DL_ENDIAN	\
	-DMD5_ASM	\
	-DNO_WINDOWS_BRAINDEATH	\
	-DOPENSSL_BN_ASM_GF2m	\
	-DOPENSSL_BN_ASM_MONT	\
	-DOPENSSL_IA32_SSE2	\
	-DOPENSSL_PIC	\
	-DOPENSSL_THREADS	\
	-DSHA1_ASM	\
	-DSHA256_ASM	\
	-DSHA512_ASM	\
	-DSOLARIS_OPENSSL	\
	-DVPAES_ASM
CPPFLAGS +=	\
	-DOPENSSL_NO_EC	\
	-DOPENSSL_NO_EC_NISTP_64_GCC_128	\
	-DOPENSSL_NO_ECDH	\
	-DOPENSSL_NO_ECDSA	\
	-DOPENSSL_NO_GMP	\
	-DOPENSSL_NO_GOST	\
	-DOPENSSL_NO_HW_4758_CCA	\
	-DOPENSSL_NO_HW_AEP	\
	-DOPENSSL_NO_HW_ATALLA	\
	-DOPENSSL_NO_HW_CHIL	\
	-DOPENSSL_NO_HW_CSWIFT	\
	-DOPENSSL_NO_HW_GMP	\
	-DOPENSSL_NO_HW_NCIPHER	\
	-DOPENSSL_NO_HW_NURON	\
	-DOPENSSL_NO_HW_PADLOCK	\
	-DOPENSSL_NO_HW_SUREWARE	\
	-DOPENSSL_NO_HW_UBSEC	\
	-DOPENSSL_NO_IDEA	\
	-DOPENSSL_NO_JPAKE	\
	-DOPENSSL_NO_MDC2	\
	-DOPENSSL_NO_RC3	\
	-DOPENSSL_NO_RC5	\
	-DOPENSSL_NO_RFC3779	\
	-DOPENSSL_NO_SCTP	\
	-DOPENSSL_NO_SEED	\
	-DOPENSSL_NO_STORE	\
	-DOPENSSL_NO_WHIRLPOOL	\
	-DOPENSSL_NO_WHRLPOOL

LINTFLAGS64 += -errchk=longptr64

CERRWARN +=	-_gcc=-Wno-unused-label
CERRWARN +=	-_gcc=-Wno-uninitialized

CERRWARN += -erroff=E_END_OF_LOOP_CODE_NOT_REACHED
CERRWARN += -erroff=E_CONST_PROMOTED_UNSIGNED_LONG
CERRWARN += -erroff=E_INIT_DOES_NOT_FIT
CERRWARN += -erroff=E_INIT_SIGN_EXTEND

.KEEP_STATE:

all:	$(LIBS)

# Suppress lint check
lint:

include $(SRC)/lib/Makefile.targ
