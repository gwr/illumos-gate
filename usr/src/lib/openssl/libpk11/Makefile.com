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

LIBRARY= libpk11.a
# This one should NOT be versioned with OpenSSL.
VERS= .1

# Also build the "private" library?
# SUNW_DYNLIB=   libsunw_crypto.so.1
# SUNW_LIBLINKS= libsunw_crypto.so

# Common openssl CPPFLAGS
include ../../Makefile.conf

OBJECTS= e_pk11.o

include ../../../Makefile.lib
include ../../../Makefile.rootfs

ROOTLIBDIR=	$(ROOT)/lib/openssl/engines
ROOTLIBDIR64=	$(ROOT)/lib/openssl/engines/$(MACH64)

SRCS=	e_pk11.c

SRCDIR=		../common

# Build only a loadable module
LIBS =		$(DYNLIB)

# Needed for variadic macros
C99MODE=	$(C99_ENABLE)

LIBCRYPTO=	-lcrypto
# The following is a comment unless building with sunw_openssl
$(SUNW_OPENSSL) LIBCRYPTO=	-lsunw_crypto

LDLIBS +=	$(LIBCRYPTO) -lc

CFLAGS	+=	$(CCVERBOSE)

CPPFLAGS +=	-I$(SRCDIR)

# NB: same as libcrypto
CPPFLAGS +=	$(OPENSSL_CONF_CPPFLAGS)
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

# The following is a comment unless building with sunw_openssl
$(SUNW_OPENSSL) CPPFLAGS	+=	-DOPENSSL_SUNW_PREFIX

CERRWARN += -erroff=E_NON_CONST_INIT

.KEEP_STATE:

all:	$(LIBS)

# Suppress lint check
lint:

include $(SRC)/lib/Makefile.targ
