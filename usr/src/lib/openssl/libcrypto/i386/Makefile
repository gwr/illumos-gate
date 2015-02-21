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

CPUID_OBJ= x86cpuid.o
BN_ASM= bn-586.o co-586.o x86-mont.o x86-gf2m.o
DES_ENC= des-586.o crypt586.o
AES_ENC= aes-586.o vpaes-x86.o aesni-x86.o
BF_ENC= bf-586.o
CAST_ENC= c_enc.o
RC4_ENC= rc4-586.o
RC5_ENC= rc5-586.o
MD5_ASM_OBJ= md5-586.o
SHA1_ASM_OBJ= sha1-586.o sha256-586.o sha512-586.o
RMD160_ASM_OBJ= rmd-586.o
WP_ASM_OBJ= wp_block.o
CMLL_ENC= cmll-x86.o
MODES_ASM_OBJ= ghash-x86.o
ENGINES_ASM_OBJ=

include ../Makefile.com

ASFLAGS += -m32 $(i386_C_BIGPICFLAGS)

i386_C_PICFLAGS = $(i386_C_BIGPICFLAGS)

CPPFLAGS += \
	-DPK11_LIB_LOCATION="/usr/lib/libpkcs11.so.1" \
	-DOPENSSL_NO_INLINE_ASM \
	-DOPENSSL_BN_ASM_PART_WORDS \
	-DRMD160_ASM \
	-DL_ENDIAN \
	-DOPENSSL_BN_ASM_GF2m \
	-DOPENSSL_IA32_SSE2 \
	-DVPAES_ASM

# Let CPP rename external symbols in sunw/*.o
sunw/des-586.o	:= CPPFLAGS += -DDES_ede3_cbc_encrypt=sunw_DES_ede3_cbc_encrypt
sunw/des-586.o	:= CPPFLAGS += -DDES_ncbc_encrypt=sunw_DES_ncbc_encrypt
sunw/co-586.o	:= CPPFLAGS += -Dbn_mul_comba4=sunw_bn_mul_comba4
sunw/co-586.o	:= CPPFLAGS += -Dbn_mul_comba8=sunw_bn_mul_comba8
sunw/co-586.o	:= CPPFLAGS += -Dbn_sqr_comba4=sunw_bn_sqr_comba4
sunw/co-586.o	:= CPPFLAGS += -Dbn_sqr_comba8=sunw_bn_sqr_comba8
sunw/bn-586.o	:= CPPFLAGS += -Dbn_add_words=sunw_bn_add_words
sunw/bn-586.o	:= CPPFLAGS += -Dbn_div_words=sunw_bn_div_words
sunw/bn-586.o	:= CPPFLAGS += -Dbn_mul_add_words=sunw_bn_mul_add_words
sunw/bn-586.o	:= CPPFLAGS += -Dbn_mul_words=sunw_bn_mul_words
sunw/bn-586.o	:= CPPFLAGS += -Dbn_sqr_words=sunw_bn_sqr_words
sunw/bn-586.o	:= CPPFLAGS += -Dbn_sub_words=sunw_bn_sub_words
sunw/bf-586.o	:= CPPFLAGS += -DBF_cbc_encrypt=sunw_BF_cbc_encrypt
sunw/bf-586.o	:= CPPFLAGS += -DBF_decrypt=sunw_BF_decrypt
sunw/bf-586.o	:= CPPFLAGS += -DBF_encrypt=sunw_BF_encrypt
sunw/x86cpuid.o	:= CPPFLAGS += -DOPENSSL_cleanse=sunw_OPENSSL_cleanse
sunw/aes-586.o	:= CPPFLAGS += -Dprivate_AES_set_decrypt_key=sunw_private_AES_set_decrypt_key
sunw/aes-586.o	:= CPPFLAGS += -Dprivate_AES_set_encrypt_key=sunw_private_AES_set_encrypt_key
sunw/aes-586.o	:= CPPFLAGS += -DAES_encrypt=sunw_AES_encrypt
sunw/aes-586.o	:= CPPFLAGS += -DAES_decrypt=sunw_AES_decrypt
sunw/rc4-586.o	:= CPPFLAGS += -DRC4=sunw_RC4

# PERL script options
PLFLAGS= -DOPENSSL_IA32_SSE2 -fpic
# Other possible options
#	-m64 -xarch=v9
#	-DOPENSSL_SMALL_FOOTPRINT
#	-DB_ENDIAN

# Intermediate files
CLEANFILES += \
	x86cpuid.S \
	md5-586.s \
	sha1-586.s \
	sha256-586.s \
	sha512-586.s \
	rmd-586.s \
	des-586.S \
	crypt586.s \
	aes-586.S \
	vpaes-x86.s \
	aesni-x86.s \
	rc4-586.S \
	bf-586.S \
	cast-586.s \
	cmll-x86.s \
	ghash-x86.s \
	bn-586.S \
	co-586.S \
	x86-mont.s \
	x86-gf2m.s

pics/%.o sunw/%.o: %.S
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_A)

pics/%.o sunw/%.o: %.s
	$(COMPILE.s) -o $@ $<
	$(POST_PROCESS_A)

x86cpuid.S: $(SRCDIR)/x86cpuid.pl $(SRCDIR)/perlasm/x86asm.pl
	$(PERL) $(SRCDIR)/x86cpuid.pl elf $(PLFLAGS) > $@

md5-586.s: $(SRCDIR)/md5/asm/md5-586.pl \
	$(SRCDIR)/perlasm/x86asm.pl
	$(PERL) $(SRCDIR)/md5/asm/md5-586.pl elf $(PLFLAGS) > $@

sha1-586.s:	$(SRCDIR)/sha/asm/sha1-586.pl \
	$(SRCDIR)/perlasm/x86asm.pl
	$(PERL) $(SRCDIR)/sha/asm/sha1-586.pl elf $(PLFLAGS) > $@
sha256-586.s:	$(SRCDIR)/sha/asm/sha256-586.pl \
	$(SRCDIR)/perlasm/x86asm.pl
	$(PERL) $(SRCDIR)/sha/asm/sha256-586.pl elf $(PLFLAGS) > $@
sha512-586.s:	$(SRCDIR)/sha/asm/sha512-586.pl \
	$(SRCDIR)/perlasm/x86asm.pl
	$(PERL) $(SRCDIR)/sha/asm/sha512-586.pl elf $(PLFLAGS) > $@

rmd-586.s:	$(SRCDIR)/ripemd/asm/rmd-586.pl \
	$(SRCDIR)/perlasm/x86asm.pl
	$(PERL) $(SRCDIR)/ripemd/asm/rmd-586.pl elf $(PLFLAGS) > $@

des-586.S:	$(SRCDIR)/des/asm/des-586.pl \
	$(SRCDIR)/perlasm/x86asm.pl $(SRCDIR)/perlasm/cbc.pl
	$(PERL) $(SRCDIR)/des/asm/des-586.pl elf $(PLFLAGS) > $@
crypt586.s:	$(SRCDIR)/des/asm/crypt586.pl \
	$(SRCDIR)/perlasm/x86asm.pl $(SRCDIR)/perlasm/cbc.pl
	$(PERL) $(SRCDIR)/des/asm/crypt586.pl elf $(PLFLAGS) > $@

aes-586.S:	$(SRCDIR)/aes/asm/aes-586.pl \
	$(SRCDIR)/perlasm/x86asm.pl
	$(PERL) $(SRCDIR)/aes/asm/aes-586.pl elf $(PLFLAGS) > $@
vpaes-x86.s:	$(SRCDIR)/aes/asm/vpaes-x86.pl \
	$(SRCDIR)/perlasm/x86asm.pl
	$(PERL) $(SRCDIR)/aes/asm/vpaes-x86.pl elf $(PLFLAGS) > $@
aesni-x86.s:	$(SRCDIR)/aes/asm/aesni-x86.pl \
	$(SRCDIR)/perlasm/x86asm.pl
	$(PERL) $(SRCDIR)/aes/asm/aesni-x86.pl elf $(PLFLAGS) > $@

rc4-586.S:	$(SRCDIR)/rc4/asm/rc4-586.pl \
	$(SRCDIR)/perlasm/x86asm.pl
	$(PERL) $(SRCDIR)/rc4/asm/rc4-586.pl elf $(PLFLAGS) > $@

bf-586.S:	$(SRCDIR)/bf/asm/bf-586.pl \
	$(SRCDIR)/perlasm/x86asm.pl $(SRCDIR)/perlasm/cbc.pl
	$(PERL) $(SRCDIR)/bf/asm/bf-586.pl elf $(PLFLAGS) > $@

cast-586.s:	$(SRCDIR)/cast/asm/cast-586.pl \
	$(SRCDIR)/perlasm/x86asm.pl $(SRCDIR)/perlasm/cbc.pl
	$(PERL)	$(SRCDIR)/cast/asm/cast-586.pl elf $(PLFLAGS) > $@

cmll-x86.s:	$(SRCDIR)/camellia/asm/cmll-x86.pl \
	$(SRCDIR)/perlasm/x86asm.pl
	$(PERL) $(SRCDIR)/camellia/asm/cmll-x86.pl elf $(PLFLAGS) > $@

ghash-x86.s:	$(SRCDIR)/modes/asm/ghash-x86.pl
	$(PERL) $(SRCDIR)/modes/asm/ghash-x86.pl elf $(PLFLAGS) > $@

bn-586.S:	$(SRCDIR)/bn/asm/bn-586.pl \
	$(SRCDIR)/perlasm/x86asm.pl
	$(PERL) $(SRCDIR)/bn/asm/bn-586.pl elf $(PLFLAGS) > $@
co-586.S:	$(SRCDIR)/bn/asm/co-586.pl \
	$(SRCDIR)/perlasm/x86asm.pl
	$(PERL) $(SRCDIR)/bn/asm/co-586.pl elf $(PLFLAGS) > $@
x86-mont.s:	$(SRCDIR)/bn/asm/x86-mont.pl \
	$(SRCDIR)/perlasm/x86asm.pl
	$(PERL) $(SRCDIR)/bn/asm/x86-mont.pl elf $(PLFLAGS) > $@
x86-gf2m.s:	$(SRCDIR)/bn/asm/x86-gf2m.pl \
	$(SRCDIR)/perlasm/x86asm.pl
	$(PERL) $(SRCDIR)/bn/asm/x86-gf2m.pl elf $(PLFLAGS) > $@

install: all $(ROOTLIBS) $(ROOTLINKS) $(ROOTLINT)
