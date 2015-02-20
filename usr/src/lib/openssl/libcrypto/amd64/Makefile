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

CPUID_OBJ= x86_64cpuid.o
BN_ASM= x86_64-gcc.o x86_64-mont.o x86_64-mont5.o x86_64-gf2m.o modexp512-x86_64.o
DES_ENC= des_enc.o fcrypt_b.o
AES_ENC= aes-x86_64.o vpaes-x86_64.o bsaes-x86_64.o aesni-x86_64.o aesni-sha1-x86_64.o
BF_ENC= bf_enc.o
CAST_ENC= c_enc.o
RC4_ENC= rc4-x86_64.o rc4-md5-x86_64.o
RC5_ENC= rc5_enc.o
MD5_ASM_OBJ= md5-x86_64.o
SHA1_ASM_OBJ= sha1-x86_64.o sha256-x86_64.o sha512-x86_64.o
RMD160_ASM_OBJ= 
WP_ASM_OBJ= wp_block.o
CMLL_ENC= cmll-x86_64.o cmll_misc.o
MODES_ASM_OBJ= ghash-x86_64.o
ENGINES_ASM_OBJ=

include ../Makefile.com
include ../../../Makefile.lib.64

ASFLAGS += -m64 $(amd64_C_BIGPICFLAGS)

amd64_C_PICFLAGS = $(amd64_C_BIGPICFLAGS)

CPPFLAGS += -DPK11_LIB_LOCATION="/usr/lib/64/libpkcs11.so.1" \
	-DOPENSSL_BN_ASM_MONT5 \
	-DBSAES_ASM \
	-DL_ENDIAN \
	-DOPENSSL_BN_ASM_GF2m \
	-DOPENSSL_IA32_SSE2 \
	-DVPAES_ASM

# Let CPP rename external symbols
pics/x86_64cpuid.o	:= CPPFLAGS += -DOPENSSL_cleanse=sunw_OPENSSL_cleanse
pics/aes-x86_64.o	:= CPPFLAGS += -Dprivate_AES_set_decrypt_key=sunw_private_AES_set_decrypt_key
pics/aes-x86_64.o	:= CPPFLAGS += -Dprivate_AES_set_encrypt_key=sunw_private_AES_set_encrypt_key
pics/aes-x86_64.o	:= CPPFLAGS += -DAES_encrypt=sunw_AES_encrypt
pics/aes-x86_64.o	:= CPPFLAGS += -DAES_decrypt=sunw_AES_decrypt
pics/rc4-x86_64.o	:= CPPFLAGS += -DRC4=sunw_RC4

# Intermediate files
CLEANFILES += \
	pics/x86_64cpuid.S \
	pics/md5-x86_64.s \
	pics/sha1-x86_64.s \
	pics/sha256-x86_64.s \
	pics/sha512-x86_64.s \
	pics/aes-x86_64.S \
	pics/vpaes-x86_64.s \
	pics/bsaes-x86_64.s \
	pics/aesni-x86_64.s \
	pics/aesni-sha1-x86_64.s \
	pics/rc4-x86_64.S \
	pics/rc4-x86_64.s \
	pics/rc4-md5-x86_64.s \
	pics/cmll-x86_64.s \
	pics/ghash-x86_64.s \
	pics/x86_64-mont.s \
	pics/x86_64-mont5.s \
	pics/x86_64-gf2m.s \
	pics/modexp512-x86_64.s

pics/%.o: pics/%.S
	$(COMPILE.S) -o $@ $<
	$(POST_PROCESS_A)

pics/x86_64cpuid.S: $(SRCDIR)/x86_64cpuid.pl
	$(PERL) $(SRCDIR)/x86_64cpuid.pl void > $@

pics/md5-x86_64.s:	$(SRCDIR)/md5/asm/md5-x86_64.pl
	$(PERL) $(SRCDIR)/md5/asm/md5-x86_64.pl void > $@

pics/sha1-x86_64.s:	$(SRCDIR)/sha/asm/sha1-x86_64.pl
	$(PERL) $(SRCDIR)/sha/asm/sha1-x86_64.pl void > $@
pics/sha256-x86_64.s:	$(SRCDIR)/sha/asm/sha512-x86_64.pl
	$(PERL) $(SRCDIR)/sha/asm/sha512-x86_64.pl void $@
pics/sha512-x86_64.s:	$(SRCDIR)/sha/asm/sha512-x86_64.pl
	$(PERL) $(SRCDIR)/sha/asm/sha512-x86_64.pl void $@

pics/aes-x86_64.S: $(SRCDIR)/aes/asm/aes-x86_64.pl
	$(PERL) $(SRCDIR)/aes/asm/aes-x86_64.pl void > $@
pics/vpaes-x86_64.s:	$(SRCDIR)/aes/asm/vpaes-x86_64.pl
	$(PERL) $(SRCDIR)/aes/asm/vpaes-x86_64.pl void > $@
pics/bsaes-x86_64.s:	$(SRCDIR)/aes/asm/bsaes-x86_64.pl
	$(PERL) $(SRCDIR)/aes/asm/bsaes-x86_64.pl void > $@
pics/aesni-x86_64.s:	$(SRCDIR)/aes/asm/aesni-x86_64.pl
	$(PERL) $(SRCDIR)/aes/asm/aesni-x86_64.pl void > $@
pics/aesni-sha1-x86_64.s:	$(SRCDIR)/aes/asm/aesni-sha1-x86_64.pl
	$(PERL) $(SRCDIR)/aes/asm/aesni-sha1-x86_64.pl void > $@

pics/rc4-x86_64.S: $(SRCDIR)/rc4/asm/rc4-x86_64.pl
	$(PERL) $(SRCDIR)/rc4/asm/rc4-x86_64.pl void > $@
pics/rc4-md5-x86_64.s:	$(SRCDIR)/rc4/asm/rc4-md5-x86_64.pl
	$(PERL) $(SRCDIR)/rc4/asm/rc4-md5-x86_64.pl void > $@

pics/cmll-x86_64.s:  $(SRCDIR)/camellia/asm/cmll-x86_64.pl
	$(PERL) $(SRCDIR)/camellia/asm/cmll-x86_64.pl void > $@

pics/ghash-x86_64.s:	$(SRCDIR)/modes/asm/ghash-x86_64.pl
	$(PERL) $(SRCDIR)/modes/asm/ghash-x86_64.pl void > $@

pics/x86_64-gcc.o:	$(SRCDIR)/bn/asm/x86_64-gcc.c
	$(COMPILE.c) -o $@ $(SRCDIR)/bn/asm/x86_64-gcc.c
	$(POST_PROCESS_O)
pics/x86_64-mont.s:	$(SRCDIR)/bn/asm/x86_64-mont.pl
	$(PERL) $(SRCDIR)/bn/asm/x86_64-mont.pl void > $@
pics/x86_64-mont5.s:	$(SRCDIR)/bn/asm/x86_64-mont5.pl
	$(PERL) $(SRCDIR)/bn/asm/x86_64-mont5.pl void > $@
pics/x86_64-gf2m.s:	$(SRCDIR)/bn/asm/x86_64-gf2m.pl
	$(PERL) $(SRCDIR)/bn/asm/x86_64-gf2m.pl void > $@
pics/modexp512-x86_64.s:	$(SRCDIR)/bn/asm/modexp512-x86_64.pl
	$(PERL) $(SRCDIR)/bn/asm/modexp512-x86_64.pl void > $@

install: all $(ROOTLIBS64) $(ROOTLINKS64)
