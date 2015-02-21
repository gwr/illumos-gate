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

LIBRARY= libssl.a
# See SHLIB_VERSION_NUMBER in crypto/opensslv.h
VERS= .1.0.0

# Also build the private SUNW prefix library.
SUNW_LIBNAME=	sunw_ssl
SUNW_DYNLIB=	$(SUNW_LIBNAME:%=lib%.so.1)
SUNW_LIBLINKS=	$(SUNW_LIBNAME:%=lib%.so)
SUNW_LINTLIB=	$(SUNW_LIBNAME:%=llib-l%.ln)

include ../../Makefile.conf
include ../../Makefile.ssl

# SSL_OBJ comes from Makefile.crypto
OBJECTS= $(SSL_OBJ)

include ../../../Makefile.lib
include ../../../Makefile.rootfs

INCDIR=		$(OPENSSLDIR)
SRCDIR=		$(OPENSSLDIR)/ssl
CRSRCDIR=	$(OPENSSLDIR)/crypto

LIBS =		$(DYNLIB) $(LINTLIB) $(SUNW_DYNLIB) $(SUNW_LINTLIB)
$(LINTLIB) :=	SRCS = ../$(LINTSRC)
$(SUNW_LINTLIB) := SRCS = ../$(LINTSRC)

$(DYNLIB)	:=	LDLIBS += -lcrypto -lc
$(SUNW_DYNLIB)	:=	LDLIBS += -lsunw_crypto -lc
MAPFILES=

# Not running lint, so no SRCS
SRCS=

CFLAGS	+=	$(CCVERBOSE)
CPPFLAGS +=	-I$(INCDIR) -I$(SRCDIR) -I$(CRSRCDIR)

CPPFLAGS +=	$(OPENSSL_CONF_CPPFLAGS)

CPPFLAGS +=	\
	-D_REENTRANT	\
	-DAES_ASM	\
	-DDSO_DLFCN	\
	-DGHASH_ASM	\
	-DHAVE_DLFCN_H	\
	-DMD5_ASM	\
	-DOPENSSL_BN_ASM_GF2m	\
	-DOPENSSL_BN_ASM_MONT	\
	-DOPENSSL_IA32_SSE2	\
	-DOPENSSL_PIC	\
	-DOPENSSL_THREADS	\
	-DSHA1_ASM	\
	-DSHA256_ASM	\
	-DSHA512_ASM	\
	-DVPAES_ASM

LINTFLAGS64 += -errchk=longptr64

CERRWARN +=	-_gcc=-Wno-uninitialized
CERRWARN +=	-_gcc=-Wno-unused-label

CERRWARN += -erroff=E_CONST_PROMOTED_UNSIGNED_LONG
CERRWARN += -erroff=E_END_OF_LOOP_CODE_NOT_REACHED
CERRWARN += -erroff=E_INIT_DOES_NOT_FIT
CERRWARN += -erroff=E_INIT_SIGN_EXTEND

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
