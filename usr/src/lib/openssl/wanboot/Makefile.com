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

LIBRARY= libopenssl-boot.a

# Where to find the unpacked/patched openssl sources.
# XXX: Get this from some included makefile?
OPENSSLDIR = $(CODEMGR_WS)/external/openssl/openssl-1.0.1h

# Needed by Makefile.libcrypto (rules)
CRYPTOSRC= $(OPENSSLDIR)/crypto

# Needed by Makefile.libssl (rules)
SSL_SRC= $(OPENSSLDIR)/ssl

#
# These are from $(OPENSSLDIR)/Makefile
# Using "vanilla" version here (NO_ASM)
#
CPUID_OBJ= mem_clr.o
BN_ASM= bn_asm.o
DES_ENC= des_enc.o
AES_ENC= aes_core.o aes_cbc.o
BF_ENC= bf_enc.o
CAST_ENC= c_enc.o
RC4_ENC= rc4_enc.o rc4_skey.o
RC5_ENC= rc5_enc.o
CMLL_ENC= camellia.o cmll_misc.o cmll_cbc.o

include ../../Makefile.conf
include ../../Makefile.libcrypto
include ../../Makefile.libssl

OBJECTS= $(SSL_OBJ) $(CRYPTO_OBJECTS) stubs.o

# drop a few things
LIBOBJcast=
LIBOBJdso=dso_lib.o dso_openssl.o dso_null.o
LIBOBJrc4=
LIBOBJripemd=
LIBOBJcommon_uid=

include ../../../Makefile.lib

INCDIR=		$(OPENSSLDIR)

# The including makefiles use this
SRCDIR=		$(OPENSSLDIR)/crypto

LIBS =		$(LIBRARY)
MAPFILES=
LIBLINKS=

# Not running lint, so no SRCS
SRCS=
LINTSRC=

CFLAGS	+=	$(CCVERBOSE)
CPPFLAGS +=	-I../common -I$(INCDIR) -I$(SRCDIR) \
		-I$(SRCDIR)/asn1 -I$(SRCDIR)/evp -I$(SRCDIR)/modes

CPPFLAGS +=	$(OPENSSL_CONF_CPPFLAGS)

# Special for wanboot
CPPFLAGS += \
	-D_BOOT \
	-DNO_CHMOD \
	-DOPENSSL_NO_ASM \
	-DOPENSSL_NO_CAST \
	-DOPENSSL_NO_DSO \
	-DOPENSSL_NO_DTLS1 \
	-DOPENSSL_NO_HEARTBEATS \
	-DOPENSSL_NO_RC4 \
	-DOPENSSL_NO_RIPEMD \
	-DOPENSSL_NO_SRP \
	-DOPENSSL_SMALL_FOOTPRINT

# New name for inline assembler keyword
CPPFLAGS +=	-Dasm=__asm__
CPPFLAGS +=	\
	-D_REENTRANT	\
	-DHAVE_DLFCN_H

LINTFLAGS64 += -errchk=longptr64

CERRWARN +=	-_gcc=-Wno-unused-label
CERRWARN +=	-_gcc=-Wno-uninitialized
CERRWARN +=	-_gcc=-Wno-old-style-declaration
CERRWARN +=	-_gcc=-Wno-unused-variable
CERRWARN +=	-_gcc=-Wno-unused-function

CERRWARN += -erroff=E_END_OF_LOOP_CODE_NOT_REACHED
CERRWARN += -erroff=E_TYP_STORAGE_CLASS_OBSOLESCENT
CERRWARN += -erroff=E_CONST_PROMOTED_UNSIGNED_LONG
CERRWARN += -erroff=E_CONST_PROMOTED_UNSIGNED_LL
CERRWARN += -erroff=E_INIT_DOES_NOT_FIT
CERRWARN += -erroff=E_INIT_SIGN_EXTEND

.KEEP_STATE:

all:	linktest # $(LIBS)

linktest: ltmain.o $(LIBRARY)
	$(LINK.c) -o $@ ltmain.o $(LIBRARY)

%.o objs/%.o : ../%.c
	$(COMPILE.c) -o $@ $<

CLOBBERFILES += linktest ltmain.o

# Suppress lint check
lint:

include $(SRC)/lib/Makefile.targ
