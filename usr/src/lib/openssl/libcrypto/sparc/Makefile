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

CPUID_OBJ= sparcv9cap.o sparccpuid.o
BN_ASM= sparcv8.o sparcv9-mont.o sparcv9a-mont.o
# XXX getting compiler errors with des_enc-sparc.o
#DES_ENC= des_enc-sparc.o fcrypt_b.o #XXX? dest4-sparcv9.o
DES_ENC= des_enc.o fcrypt_b.o
AES_ENC= aes_core.o aes_cbc.o aes-sparcv9.o #XXX? aest4-sparcv9.o
BF_ENC= bf_enc.o
CAST_ENC= c_enc.o
RC4_ENC= rc4_enc.o rc4_skey.o
RC5_ENC= rc5_enc.o
MD5_ASM_OBJ= #XXX? md5-sparcv9.o
SHA1_ASM_OBJ= sha1-sparcv9.o sha256-sparcv9.o sha512-sparcv9.o
RMD160_ASM_OBJ= 
WP_ASM_OBJ= wp_block.o
CMLL_ENC= camellia.o cmll_misc.o cmll_cbc.o
MODES_ASM_OBJ= ghash-sparcv9.o
ENGINES_ASM_OBJ=

include ../Makefile.com

ILSRCDIR = ../../inline-t4

# sparc_CFLAGS += -xarch=sparc
ASFLAGS += -m32 -xarch=v8plus $(sparc_C_BIGPICFLAGS)

sparc_C_PICFLAGS = $(sparc_C_BIGPICFLAGS)

# Suppress assembler warnings
pics/vis3-mont.o	:= sparc_CFLAGS += -Wa,-n
pics/sparct4-mont.o	:= ASFLAGS += -Wa,-n
pics/aest4-sparcv9.o	:= ASFLAGS += -Wa,-n

CPPFLAGS += -I$(ILSRCDIR)
CPPFLAGS += \
	-DPK11_LIB_LOCATION="/usr/lib/libpkcs11.so.1" \
	-DBN_DIV2W \
	-DB_ENDIAN

# Let CPP rename external symbols in sunw/*.o
sunw/sparccpuid.o	:= CPPFLAGS += -DOPENSSL_cleanse=sunw_OPENSSL_cleanse
sunw/sparcv8.o	:= CPPFLAGS += -Dbn_mul_comba4=sunw_bn_mul_comba4
sunw/sparcv8.o	:= CPPFLAGS += -Dbn_mul_comba8=sunw_bn_mul_comba8
sunw/sparcv8.o	:= CPPFLAGS += -Dbn_sqr_comba4=sunw_bn_sqr_comba4
sunw/sparcv8.o	:= CPPFLAGS += -Dbn_sqr_comba8=sunw_bn_sqr_comba8
sunw/sparcv8.o	:= CPPFLAGS += -Dbn_add_words=sunw_bn_add_words
sunw/sparcv8.o	:= CPPFLAGS += -Dbn_div_words=sunw_bn_div_words
sunw/sparcv8.o	:= CPPFLAGS += -Dbn_mul_add_words=sunw_bn_mul_add_words
sunw/sparcv8.o	:= CPPFLAGS += -Dbn_mul_words=sunw_bn_mul_words
sunw/sparcv8.o	:= CPPFLAGS += -Dbn_sqr_words=sunw_bn_sqr_words
sunw/sparcv8.o	:= CPPFLAGS += -Dbn_sub_words=sunw_bn_sub_words
sunw/aes-sparcv9.o	:= CPPFLAGS += -DAES_encrypt=sunw_AES_encrypt
sunw/aes-sparcv9.o	:= CPPFLAGS += -DAES_decrypt=sunw_AES_decrypt

# PERL script options
PLFLAGS= -DB_ENDIAN -fpic
# Other possible options
#	-DOPENSSL_IA32_SSE2
#	-m64 -xarch=v9
#	-DOPENSSL_SMALL_FOOTPRINT

# Intermediate files
CLEANFILES += \
	aes-sparcv9.S \
	sparcv9a-mont.s \
	sparcv9-mont.s \
	des_enc-sparc.S \
	ghash-sparcv9.s \
	sha1-sparcv9.S \
	sha256-sparcv9.S \
	sha512-sparcv9.S \
	vis3-mont.s \
	sparct4-mont.S \
	sparcv9-gf2m.S \
	aest4-sparcv9.S \
	md5-sparcv9.S \
	dest4-sparcv9.s

pics/%.o sunw/%.o: %.S
	$(COMPILE.S) -o $@ $<
	$(POST_PROCESS_A)

pics/%.o sunw/%.o: %.s
	$(COMPILE.s) -o $@ $<
	$(POST_PROCESS_A)

pics/sparccpuid.o sunw/sparccpuid.o: $(SRCDIR)/sparccpuid.S
	$(COMPILE.S) -o $@ $(SRCDIR)/sparccpuid.S
	$(POST_PROCESS_A)

pics/sparcv9cap.o: $(SRCDIR)/sparcv9cap.c
	$(COMPILE.c) -o $@ $(SRCDIR)/sparcv9cap.c
	$(POST_PROCESS_O)

# aes/Makefile:
# aes-sparcv9.s: asm/aes-sparcv9.pl
# 	$(PERL) asm/aes-sparcv9.pl $(CFLAGS) > $@
# aest4-sparcv9.s: asm/aest4-sparcv9.pl
# 	$(PERL) asm/aest4-sparcv9.pl $(CFLAGS) > $@
aes-sparcv9.S:	$(SRCDIR)/aes/asm/aes-sparcv9.pl
	$(PERL) $(SRCDIR)/aes/asm/aes-sparcv9.pl void $(PLFLAGS) > $@
aest4-sparcv9.S:	$(ILSRCDIR)/aest4-sparcv9.pl
	$(PERL) $(ILSRCDIR)/aest4-sparcv9.pl void $(PLFLAGS) > $@

# bn/Makefile:
# sparcv8.o:	asm/sparcv8.S
# 	$(CC) $(CFLAGS) -c asm/sparcv8.S
# bn-sparcv9.o:	asm/sparcv8plus.S
# 	$(CC) $(CFLAGS) -c -o $@ asm/sparcv8plus.S
# sparcv9a-mont.s:	asm/sparcv9a-mont.pl
# 	$(PERL) asm/sparcv9a-mont.pl $(CFLAGS) > $@
# sparcv9-mont.s:	asm/sparcv9-mont.pl
# 	$(PERL) asm/sparcv9-mont.pl $(CFLAGS) > $@
# vis3-mont.s:		asm/vis3-mont.pl
# 	$(PERL) asm/vis3-mont.pl $(CFLAGS) > $@
# sparct4-mont.S:	asm/sparct4-mont.pl
# 	$(PERL) asm/sparct4-mont.pl $(CFLAGS) > $@
# sparcv9-gf2m.S:	asm/sparcv9-gf2m.pl
# 	$(PERL) asm/sparcv9-gf2m.pl $(CFLAGS) > $@
pics/sparcv8.o sunw/sparcv8.o:	$(SRCDIR)/bn/asm/sparcv8.S
	$(COMPILE.S) -o $@ $(SRCDIR)/bn/asm/sparcv8.S
	$(POST_PROCESS_A)
sparcv9a-mont.s:	$(SRCDIR)/bn/asm/sparcv9a-mont.pl
	$(PERL) $(SRCDIR)/bn/asm/sparcv9a-mont.pl void $(PLFLAGS) > $@
sparcv9-mont.s:	$(SRCDIR)/bn/asm/sparcv9-mont.pl
	$(PERL) $(SRCDIR)/bn/asm/sparcv9-mont.pl void $(PLFLAGS) > $@
vis3-mont.s:	$(ILSRCDIR)/vis3-mont.pl
	$(PERL) $(ILSRCDIR)/vis3-mont.pl void $(PLFLAGS) > $@
sparct4-mont.S:	$(ILSRCDIR)/sparct4-mont.pl
	$(PERL) $(ILSRCDIR)/sparct4-mont.pl void $(PLFLAGS) > $@
sparcv9-gf2m.S:	$(ILSRCDIR)/sparcv9-gf2m.pl
	$(PERL) $(ILSRCDIR)/sparcv9-gf2m.pl void $(PLFLAGS) > $@

# des/Makefile:
# des_enc-sparc.S:	asm/des_enc.m4
# 	m4 -B 8192 asm/des_enc.m4 > des_enc-sparc.S
# dest4-sparcv9.s:	asm/dest4-sparcv9.pl
# 	$(PERL) asm/dest4-sparcv9.pl $(CFLAGS) > $@
des_enc-sparc.S:	$(SRCDIR)/des/asm/des_enc.m4
	$(M4) -B 8192 $(SRCDIR)/des/asm/des_enc.m4 > $@
dest4-sparcv9.s:	$(ILSRCDIR)/dest4-sparcv9.pl
	$(PERL) $(ILSRCDIR)/dest4-sparcv9.pl void $(PLFLAGS) > $@

# md5/Makefile:
# md5-sparcv9.S:	asm/md5-sparcv9.pl
# 	$(PERL) asm/md5-sparcv9.pl $@ $(CFLAGS)
md5-sparcv9.S:	$(ILSRCDIR)/md5-sparcv9.pl
	$(PERL) $(ILSRCDIR)/md5-sparcv9.pl $@ $(PLFLAGS)

# modes/Makefile:
# ghash-sparcv9.s:	asm/ghash-sparcv9.pl
# 	$(PERL) asm/ghash-sparcv9.pl $@ $(CFLAGS)
ghash-sparcv9.s:	$(SRCDIR)/modes/asm/ghash-sparcv9.pl
	$(PERL) $(SRCDIR)/modes/asm/ghash-sparcv9.pl $@ $(PLFLAGS)

# sha/Makefile:
# sha1-sparcv9.S:	asm/sha1-sparcv9.pl;	$(PERL) asm/sha1-sparcv9.pl $@ $(CFLAGS)
# sha256-sparcv9.S:asm/sha512-sparcv9.pl;	$(PERL) asm/sha512-sparcv9.pl $@ $(CFLAGS)
# sha512-sparcv9.S:asm/sha512-sparcv9.pl;	$(PERL) asm/sha512-sparcv9.pl $@ $(CFLAGS)
sha1-sparcv9.S:	$(SRCDIR)/sha/asm/sha1-sparcv9.pl
	$(PERL) $(SRCDIR)/sha/asm/sha1-sparcv9.pl $@ $(PLFLAGS)
sha256-sparcv9.S:	$(SRCDIR)/sha/asm/sha512-sparcv9.pl
	$(PERL) $(SRCDIR)/sha/asm/sha512-sparcv9.pl $@ $(PLFLAGS)
sha512-sparcv9.S:	$(SRCDIR)/sha/asm/sha512-sparcv9.pl
	$(PERL) $(SRCDIR)/sha/asm/sha512-sparcv9.pl $@ $(PLFLAGS)

install: all $(ROOTLIBS) $(ROOTLINKS) $(ROOTLINT)
