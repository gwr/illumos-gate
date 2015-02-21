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

LIBRARY= libcrypto.a
# See SHLIB_VERSION_NUMBER in crypto/opensslv.h
VERS= .1.0.0

# Also build the private SUNW prefix library.
SUNW_LIBNAME=	sunw_crypto
SUNW_DYNLIB=	$(SUNW_LIBNAME:%=lib%.so.1)
SUNW_LIBLINKS=	$(SUNW_LIBNAME:%=lib%.so)
SUNW_LINTLIB=	$(SUNW_LIBNAME:%=llib-l%.ln)

include ../../Makefile.conf
include ../../Makefile.crypto

# CRYPTO_OBJECTS comes from Makefile.crypto
OBJECTS= $(CRYPTO_OBJECTS)

include ../../../Makefile.lib
include ../../../Makefile.rootfs

# The including makefiles use this
SRCDIR=		$(OPENSSLDIR)/crypto
INCDIR=		$(OPENSSLDIR)

LIBS =		$(DYNLIB) $(LINTLIB) $(SUNW_DYNLIB) $(SUNW_LINTLIB)
$(LINTLIB) :=	SRCS = ../$(LINTSRC)
$(SUNW_LINTLIB) := SRCS = ../$(LINTSRC)

LDLIBS +=	-lsocket -lnsl -lc
MAPFILES=

# Not running lint, so no SRCS
SRCS=

COPTFLAG = -_cc=-xO3 -_gcc=-O3
COPTFLAG64 = -_cc=-xO3 -_gcc=-O3

CFLAGS	+=	$(CCVERBOSE)
CPPFLAGS +=	-I$(INCDIR) -I$(SRCDIR) \
		-I$(SRCDIR)/asn1 -I$(SRCDIR)/evp -I$(SRCDIR)/modes

CPPFLAGS +=	$(OPENSSL_CONF_CPPFLAGS)

# New name for inline assembler keyword
CPPFLAGS +=	-Dasm=__asm__
CPPFLAGS +=	\
	-D_REENTRANT	\
	-DAES_ASM	\
	-DDSO_DLFCN	\
	-DGHASH_ASM	\
	-DHAVE_DLFCN_H	\
	-DMD5_ASM	\
	-DOPENSSL_BN_ASM_MONT	\
	-DOPENSSL_PIC	\
	-DOPENSSL_THREADS	\
	-DSHA1_ASM	\
	-DSHA256_ASM	\
	-DSHA512_ASM

LINTFLAGS64 += -errchk=longptr64

CERRWARN +=	-_gcc=-Wno-old-style-declaration
CERRWARN +=	-_gcc=-Wno-uninitialized
CERRWARN +=	-_gcc=-Wno-unused-label
CERRWARN +=	-_gcc=-Wno-unused-variable

CERRWARN += -erroff=E_CONST_PROMOTED_UNSIGNED_LONG
CERRWARN += -erroff=E_CONST_PROMOTED_UNSIGNED_LL
CERRWARN += -erroff=E_END_OF_LOOP_CODE_NOT_REACHED
CERRWARN += -erroff=E_TYP_STORAGE_CLASS_OBSOLESCENT

.KEEP_STATE:

all:	$(LIBS)

# Suppress lint check
lint:

# because LINTSRC is in ../ (not SRCDIR)
$(ROOTLINTDIR)/%: ../%
	$(INS.file)

# Special rules to build SUNW_DYNLIB, SUNW_LIBLINKS
include $(SRC)/lib/openssl/Makefile.sunw

include $(SRC)/lib/openssl/Makefile.targ
include $(SRC)/lib/Makefile.targ
