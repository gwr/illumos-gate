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

include ../../Makefile.conf
include ../../Makefile.crypto

# Combine libssl into this lib
include ../../Makefile.ssl

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

# Shrink the list of libssl objects too.
SSL_OBJ= \
	s3_clnt.o  s3_lib.o  s3_enc.o s3_pkt.o s3_both.o s3_cbc.o \
	t1_clnt.o  t1_lib.o  t1_enc.o t1_reneg.o d1_srtp.o\
	ssl_lib.o ssl_err.o ssl_err2.o ssl_cert.o ssl_sess.o \
	ssl_ciph.o ssl_rsa.o ssl_asn1.o ssl_algs.o

OBJECTS= $(SSL_OBJ) $(CRYPTO_OBJECTS) stubs.o

# drop several things for standalone
LIBOBJcast=
LIBOBJcommon_uid=
LIBOBJcommon= cryptlib.o mem.o mem_dbg.o cpt_err.o ex_data.o \
	o_time.o o_dir.o o_init.o $(CPUID_OBJ)
LIBOBJconf= conf_err.o conf_lib.o conf_api.o conf_def.o conf_mod.o
LIBOBJdso= dso_lib.o dso_null.o dso_openssl.o
LIBOBJengine=
LIBOBJkrb5=
LIBOBJocsp= ocsp_asn.o ocsp_ext.o ocsp_err.o
LIBOBJrc4=
LIBOBJripemd=
LIBOBJts= ts_err.o
LIBOBJui= ui_err.o

include ../../../Makefile.lib

# Special install locations
ROOTLIBDIR=	$(ROOT)/stand/lib
ROOTLIBDIR64=	$(ROOT)/stand/lib/$(MACH64)

INCDIR=		$(OPENSSLDIR)/include

# The including makefiles use this
SRCDIR=		$(OPENSSLDIR)/crypto
STANDDIR=	$(SRC)/stand
SYSDIR	=	$(SRC)/uts

LIBS =		$(LIBRARY)
MAPFILES=
LIBLINKS=

# Not running lint, so no SRCS
SRCS=
LINTSRC=

CFLAGS	+=	$(CCVERBOSE)

#
# Configure the appropriate #defines and #include path for building
# standalone bits.  Note that we turn off access to /usr/include and
# the proto area since those headers match libc's implementation, and
# libc is of course not available to standalone binaries.
#
CPPDEFS	= 	-D_BOOT
CPPINCS	= 	-YI,$(STANDDIR)/lib/sa -I$(STANDDIR)/lib/sa \
		-I$(STANDDIR) -I$(STANDDIR)/$(MACH) \
		-I$(SYSDIR)/common -I$(SYSDIR)/$(ARCH)

# Note: _override_ CPPFLAGS here (= not +=)
CPPFLAGS =	$(CPPDEFS) $(CPPINCS)
AS_CPPFLAGS =	$(CPPDEFS) $(CPPINCS:-YI,%=-I%)
ASFLAGS =	-P -D__STDC__ -D_ASM

CPPFLAGS +=	-I../common -I$(INCDIR) -I$(OPENSSLDIR) -I$(SRCDIR) \
		-I$(SRCDIR)/asn1 -I$(SRCDIR)/evp -I$(SRCDIR)/modes

CPPFLAGS +=	$(OPENSSL_CONF_CPPFLAGS)

# Special for wanboot
CPPFLAGS += \
	-DNO_CHMOD \
	-DNO_SYSLOG \
	-DOPENSSL_NO_ASM \
	-DOPENSSL_NO_CAST \
	-DOPENSSL_NO_DSO \
	-DOPENSSL_NO_DTLS1 \
	-DOPENSSL_NO_ENGINE \
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

CERRWARN +=	-_gcc=-Wno-old-style-declaration
CERRWARN +=	-_gcc=-Wno-uninitialized
CERRWARN +=	-_gcc=-Wno-unused-label
CERRWARN +=	-_gcc=-Wno-unused-function
CERRWARN +=	-_gcc=-Wno-unused-variable

CERRWARN += -erroff=E_CONST_PROMOTED_UNSIGNED_LONG
CERRWARN += -erroff=E_CONST_PROMOTED_UNSIGNED_LL
CERRWARN += -erroff=E_END_OF_LOOP_CODE_NOT_REACHED
CERRWARN += -erroff=E_TYP_STORAGE_CLASS_OBSOLESCENT
CERRWARN += -erroff=E_INIT_DOES_NOT_FIT
CERRWARN += -erroff=E_INIT_SIGN_EXTEND

CLOBBERFILES += linktest ltmain.o

.KEEP_STATE:

all:	linktest # $(LIBS)

linktest: ltmain.o $(LIBRARY)
	$(LD) -o $@ ltmain.o $(LIBRARY)

%.o objs/%.o : ../%.c
	$(COMPILE.c) -o $@ $<

objs/randfile.o := CPPFLAGS += -DOPENSSL_NO_POSIX_IO

# Suppress lint check
lint:

include $(SRC)/lib/openssl/Makefile.targ
include $(SRC)/lib/Makefile.targ

$(ROOTLIBS) : $(ROOTLIBDIR)
$(ROOTLIBDIR):
	$(INS.dir)

$(ROOTLIBS64) : $(ROOTLIBDIR64)
$(ROOTLIBDIR64):
	$(INS.dir)
