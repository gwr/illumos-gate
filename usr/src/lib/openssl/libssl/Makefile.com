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

# Also build the "private" library.
# XXX: Do this conditionally?
SUNW_DYNLIB=	libsunw_ssl.so.1
SUNW_LIBLINKS=	libsunw_ssl.so

# Where to find the unpacked/patched openssl sources.
# XXX: Get this from some included makefile?
OPENSSLDIR = $(CODEMGR_WS)/external/openssl/openssl-1.0.1h

# Needed by Makefile.ssl (rules)
SSL_SRC= $(OPENSSLDIR)/ssl

include ../../Makefile.conf
include ../../Makefile.ssl

# SSL_OBJ comes from Makefile.crypto
OBJECTS= $(SSL_OBJ)

include ../../../Makefile.lib
include ../../../Makefile.rootfs

SRCDIR=		../common
INCDIR=		$(OPENSSLDIR)
CRSRCDIR=	$(OPENSSLDIR)/crypto

LIBS =		$(DYNLIB) $(LINTLIB) $(SUNW_DYNLIB)
$(LINTLIB) :=	SRCS = ../common/$(LINTSRC)

$(DYNLIB) :=	LDLIBS +=	-lcrypto -lc
MAPFILES=

# Not running lint, so no SRCS
SRCS=

CFLAGS	+=	$(CCVERBOSE)
CPPFLAGS +=	-I../common -I$(INCDIR) -I$(SSL_SRC) -I$(CRSRCDIR)

CPPFLAGS +=	$(OPENSSL_CONF_CPPFLAGS)

# XXX: Does L_ENDIAN mean "little-endian"?  (hope not)
CPPFLAGS +=	\
	-D_REENTRANT	\
	-DAES_ASM	\
	-DDSO_DLFCN	\
	-DGHASH_ASM	\
	-DHAVE_DLFCN_H	\
	-DL_ENDIAN	\
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

# Special rules to build SUNW_DYNLIB, SUNW_LIBLINKS
$(SUNW_DYNLIB) :=	LDLIBS += -lsunw_crypto -lc
include $(SRC)/lib/openssl/Makefile.sunw

include $(SRC)/lib/Makefile.targ
