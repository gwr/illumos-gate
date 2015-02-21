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

ASFLAGS += -xarch=amd64 $(amd64_C_BIGPICFLAGS)

amd64_C_PICFLAGS = $(amd64_C_BIGPICFLAGS)

CPPFLAGS += -DPK11_LIB_LOCATION="/usr/lib/64/libpkcs11.so.1" \
	-DOPENSSL_BN_ASM_MONT5 \
	-DBSAES_ASM \
	-DL_ENDIAN \
	-DOPENSSL_BN_ASM_GF2m \
	-DOPENSSL_IA32_SSE2 \
	-DVPAES_ASM

# Let CPP rename external symbols in sunw/*.o
sunw/x86_64cpuid.o	:= CPPFLAGS += -DOPENSSL_cleanse=sunw_OPENSSL_cleanse
sunw/aes-x86_64.o	:= CPPFLAGS += -Dprivate_AES_set_decrypt_key=sunw_private_AES_set_decrypt_key
sunw/aes-x86_64.o	:= CPPFLAGS += -Dprivate_AES_set_encrypt_key=sunw_private_AES_set_encrypt_key
sunw/aes-x86_64.o	:= CPPFLAGS += -DAES_encrypt=sunw_AES_encrypt
sunw/aes-x86_64.o	:= CPPFLAGS += -DAES_decrypt=sunw_AES_decrypt
sunw/rc4-x86_64.o	:= CPPFLAGS += -DRC4=sunw_RC4

# Intermediate files
CLEANFILES += \
	x86_64cpuid.S \
	md5-x86_64.s \
	sha1-x86_64.s \
	sha256-x86_64.s \
	sha512-x86_64.s \
	aes-x86_64.S \
	vpaes-x86_64.s \
	bsaes-x86_64.s \
	aesni-x86_64.s \
	aesni-sha1-x86_64.s \
	rc4-x86_64.S \
	rc4-x86_64.s \
	rc4-md5-x86_64.s \
	cmll-x86_64.s \
	ghash-x86_64.s \
	x86_64-mont.s \
	x86_64-mont5.s \
	x86_64-gf2m.s \
	modexp512-x86_64.s

pics/%.o sunw/%.o: %.S
	$(COMPILE.S) -o $@ $<
	$(POST_PROCESS_A)

pics/%.o sunw/%.o: %.s
	$(COMPILE.s) -o $@ $<
	$(POST_PROCESS_A)

x86_64cpuid.S: $(SRCDIR)/x86_64cpuid.pl
	$(PERL) $(SRCDIR)/x86_64cpuid.pl void > $@

md5-x86_64.s:	$(SRCDIR)/md5/asm/md5-x86_64.pl
	$(PERL) $(SRCDIR)/md5/asm/md5-x86_64.pl void > $@

sha1-x86_64.s:	$(SRCDIR)/sha/asm/sha1-x86_64.pl
	$(PERL) $(SRCDIR)/sha/asm/sha1-x86_64.pl void > $@
sha256-x86_64.s:	$(SRCDIR)/sha/asm/sha512-x86_64.pl
	$(PERL) $(SRCDIR)/sha/asm/sha512-x86_64.pl void $@
sha512-x86_64.s:	$(SRCDIR)/sha/asm/sha512-x86_64.pl
	$(PERL) $(SRCDIR)/sha/asm/sha512-x86_64.pl void $@

aes-x86_64.S: $(SRCDIR)/aes/asm/aes-x86_64.pl
	$(PERL) $(SRCDIR)/aes/asm/aes-x86_64.pl void > $@
vpaes-x86_64.s:	$(SRCDIR)/aes/asm/vpaes-x86_64.pl
	$(PERL) $(SRCDIR)/aes/asm/vpaes-x86_64.pl void > $@
bsaes-x86_64.s:	$(SRCDIR)/aes/asm/bsaes-x86_64.pl
	$(PERL) $(SRCDIR)/aes/asm/bsaes-x86_64.pl void > $@
aesni-x86_64.s:	$(SRCDIR)/aes/asm/aesni-x86_64.pl
	$(PERL) $(SRCDIR)/aes/asm/aesni-x86_64.pl void > $@
aesni-sha1-x86_64.s:	$(SRCDIR)/aes/asm/aesni-sha1-x86_64.pl
	$(PERL) $(SRCDIR)/aes/asm/aesni-sha1-x86_64.pl void > $@

rc4-x86_64.S: $(SRCDIR)/rc4/asm/rc4-x86_64.pl
	$(PERL) $(SRCDIR)/rc4/asm/rc4-x86_64.pl void > $@
rc4-md5-x86_64.s:	$(SRCDIR)/rc4/asm/rc4-md5-x86_64.pl
	$(PERL) $(SRCDIR)/rc4/asm/rc4-md5-x86_64.pl void > $@

cmll-x86_64.s:  $(SRCDIR)/camellia/asm/cmll-x86_64.pl
	$(PERL) $(SRCDIR)/camellia/asm/cmll-x86_64.pl void > $@

ghash-x86_64.s:	$(SRCDIR)/modes/asm/ghash-x86_64.pl
	$(PERL) $(SRCDIR)/modes/asm/ghash-x86_64.pl void > $@

x86_64-mont.s:	$(SRCDIR)/bn/asm/x86_64-mont.pl
	$(PERL) $(SRCDIR)/bn/asm/x86_64-mont.pl void > $@
x86_64-mont5.s:	$(SRCDIR)/bn/asm/x86_64-mont5.pl
	$(PERL) $(SRCDIR)/bn/asm/x86_64-mont5.pl void > $@
x86_64-gf2m.s:	$(SRCDIR)/bn/asm/x86_64-gf2m.pl
	$(PERL) $(SRCDIR)/bn/asm/x86_64-gf2m.pl void > $@
modexp512-x86_64.s:	$(SRCDIR)/bn/asm/modexp512-x86_64.pl
	$(PERL) $(SRCDIR)/bn/asm/modexp512-x86_64.pl void > $@

install: all $(ROOTLIBS64) $(ROOTLINKS64)
