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

LBASE=	libpk11
LPREF=  libsunw_pk11
LIBRARY= $(LPREF).a
VERS= .1

OBJECTS= e_pk11.o

include ../../../Makefile.lib

ROOTLIBDIR=	$(ROOT)/lib/openssl/engines
ROOTLIBDIR64=	$(ROOT)/lib/openssl/engines/$(MACH64)

SRCS=	e_pk11.c

SRCDIR=		../common
INCDIR=		../../include

# Build only a loadable module
LIBS =		$(DYNLIB)

# Needed for variadic macros
C99MODE=	$(C99_ENABLE)

LDLIBS +=	-lcrypto -lc

# Omit the prefix for the link target but retain it for the library file
ROOTLINKS=	$(ROOTLIBDIR)/$(LBASE).so
ROOTLINKS64=	$(ROOTLIBDIR64)/$(LBASE).so

$(ROOTLINKS): $(ROOTLIBDIR)/$(LIBLINKS)$(VERS)
	$(INS.liblink)

$(ROOTLINKS64): $(ROOTLIBDIR64)/$(LIBLINKS)$(VERS)
	$(INS.liblink64)

CFLAGS	+=	$(CCVERBOSE)
CPPFLAGS +=	-I$(INCDIR) -I$(SRCDIR)
CPPFLAGS +=	\
	-DOPENSSL_PIC \
	-DOPENSSL_THREADS \
	-D_REENTRANT \
	-DDSO_DLFCN \
	-DHAVE_DLFCN_H \
	-DSOLARIS_OPENSSL \
	-DNO_WINDOWS_BRAINDEATH \
	-DOPENSSL_BN_ASM_MONT \
	-DSHA1_ASM \
	-DSHA256_ASM \
	-DSHA512_ASM \
	-DMD5_ASM \
	-DAES_ASM \
	-DGHASH_ASM
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

CERRWARN += -erroff=E_NON_CONST_INIT

.KEEP_STATE:

all:	$(LIBS)

# Suppress lint check
lint:

include $(SRC)/lib/Makefile.targ
