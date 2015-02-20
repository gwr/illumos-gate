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
BN_ASM= bn-sparcv9.o sparcv9-mont.o sparcv9a-mont.o vis3-mont.o sparct4-mont.o sparcv9-gf2m.o
DES_ENC= des_enc-sparc.o fcrypt_b.o dest4-sparcv9.o
AES_ENC= aes_core.o aes_cbc.o aes-sparcv9.o aest4-sparcv9.o
BF_ENC= bf_enc.o
CAST_ENC= c_enc.o
RC4_ENC= rc4_enc.o rc4_skey.o
RC5_ENC= rc5_enc.o
MD5_ASM_OBJ= md5-sparcv9.o
SHA1_ASM_OBJ= sha1-sparcv9.o sha256-sparcv9.o sha512-sparcv9.o
RMD160_ASM_OBJ= 
WP_ASM_OBJ= wp_block.o
CMLL_ENC= camellia.o cmll_misc.o cmll_cbc.o
MODES_ASM_OBJ= ghash-sparcv9.o
ENGINES_ASM_OBJ=

include ../Makefile.com

# cw translates this xarch value into: -mcpu=ultrasparc -mvis
sparc_CFLAGS += -xarch=sparcvis
ASFLAGS += -m32 -xarch=sparcvis $(sparc_C_BIGPICFLAGS)

sparc_C_PICFLAGS = $(sparc_C_BIGPICFLAGS)

# Suppress assembler warnings
pics/vis3-mont.o	:= sparc_CFLAGS += -Wa,-n
pics/sparct4-mont.o	:= ASFLAGS += -Wa,-n
pics/aest4-sparcv9.o	:= ASFLAGS += -Wa,-n

CPPFLAGS += -DOPENSSL_NO_INLINE_ASM \
	-DPK11_LIB_LOCATION="/usr/lib/libpkcs11.so.1" \
	-DBN_DIV2W \
	-DB_ENDIAN

# Let CPP rename external symbols
pics/des_enc-sparc.o	:= CPPFLAGS += -DDES_ede3_cbc_encrypt=sunw_DES_ede3_cbc_encrypt
pics/des_enc-sparc.o	:= CPPFLAGS += -DDES_ncbc_encrypt=sunw_DES_ncbc_encrypt
pics/sparcv8.o	:= CPPFLAGS += -Dbn_mul_comba4=sunw_bn_mul_comba4
pics/sparcv8.o	:= CPPFLAGS += -Dbn_mul_comba8=sunw_bn_mul_comba8
pics/sparcv8.o	:= CPPFLAGS += -Dbn_sqr_comba4=sunw_bn_sqr_comba4
pics/sparcv8.o	:= CPPFLAGS += -Dbn_sqr_comba8=sunw_bn_sqr_comba8
pics/sparcv8.o	:= CPPFLAGS += -Dbn_add_words=sunw_bn_add_words
pics/sparcv8.o	:= CPPFLAGS += -Dbn_div_words=sunw_bn_div_words
pics/sparcv8.o	:= CPPFLAGS += -Dbn_mul_add_words=sunw_bn_mul_add_words
pics/sparcv8.o	:= CPPFLAGS += -Dbn_mul_words=sunw_bn_mul_words
pics/sparcv8.o	:= CPPFLAGS += -Dbn_sqr_words=sunw_bn_sqr_words
pics/sparcv8.o	:= CPPFLAGS += -Dbn_sub_words=sunw_bn_sub_words
pics/bn-sparcv9.o	:= CPPFLAGS += -Dbn_mul_comba4=sunw_bn_mul_comba4
pics/bn-sparcv9.o	:= CPPFLAGS += -Dbn_mul_comba8=sunw_bn_mul_comba8
pics/bn-sparcv9.o	:= CPPFLAGS += -Dbn_sqr_comba4=sunw_bn_sqr_comba4
pics/bn-sparcv9.o	:= CPPFLAGS += -Dbn_sqr_comba8=sunw_bn_sqr_comba8
pics/bn-sparcv9.o	:= CPPFLAGS += -Dbn_add_words=sunw_bn_add_words
pics/bn-sparcv9.o	:= CPPFLAGS += -Dbn_div_words=sunw_bn_div_words
pics/bn-sparcv9.o	:= CPPFLAGS += -Dbn_mul_add_words=sunw_bn_mul_add_words
pics/bn-sparcv9.o	:= CPPFLAGS += -Dbn_mul_words=sunw_bn_mul_words
pics/bn-sparcv9.o	:= CPPFLAGS += -Dbn_sqr_words=sunw_bn_sqr_words
pics/bn-sparcv9.o	:= CPPFLAGS += -Dbn_sub_words=sunw_bn_sub_words
pics/sparccpuid.o	:= CPPFLAGS += -DOPENSSL_cleanse=sunw_OPENSSL_cleanse
pics/aes-sparcv9.o	:= CPPFLAGS += -DAES_encrypt=sunw_AES_encrypt
pics/aes-sparcv9.o	:= CPPFLAGS += -DAES_decrypt=sunw_AES_decrypt
pics/aest4-sparcv9.o	:= CPPFLAGS += -DAES_encrypt=sunw_AES_encrypt
pics/aest4-sparcv9.o	:= CPPFLAGS += -DAES_decrypt=sunw_AES_decrypt

# PERL script options
PLFLAGS= -DB_ENDIAN -fpic
# Other possible options
#	-DOPENSSL_IA32_SSE2
#	-m64 -xarch=v9
#	-DOPENSSL_SMALL_FOOTPRINT

# Intermediate files
CLEANFILES += \
	pics/aes-sparcv9.S \
	pics/sparcv9a-mont.s \
	pics/sparcv9-mont.s \
	pics/des_enc-sparc.S \
	pics/ghash-sparcv9.s \
	pics/sha1-sparcv9.S \
	pics/sha256-sparcv9.S \
	pics/sha512-sparcv9.S \
	pics/vis3-mont.s \
	pics/sparct4-mont.S \
	pics/sparcv9-gf2m.S \
	pics/aest4-sparcv9.S \
	pics/md5-sparcv9.S \
	pics/dest4-sparcv9.s

pics/%.o: pics/%.S
	$(COMPILE.S) -o $@ $<
	$(POST_PROCESS_A)

pics/sparccpuid.o: $(SRCDIR)/sparccpuid.S
	$(COMPILE.S) -o $@ $(SRCDIR)/sparccpuid.S
	$(POST_PROCESS_A)
pics/sparcv9cap.o: $(SRCDIR)/sparcv9cap.c
	$(COMPILE.c) -o $@ $(SRCDIR)/sparcv9cap.c
	$(POST_PROCESS_O)

# aes/Makefile:
# aes-sparcv9.s: asm/aes-sparcv9.pl
#       $(PERL) asm/aes-sparcv9.pl $(CFLAGS) > $@
# aest4-sparcv9.s: asm/aest4-sparcv9.pl
# 	$(PERL) asm/aest4-sparcv9.pl $(CFLAGS) > $@
pics/aes-sparcv9.S:	$(SRCDIR)/aes/asm/aes-sparcv9.pl
	$(PERL) $(SRCDIR)/aes/asm/aes-sparcv9.pl void $(PLFLAGS) > $@
pics/aest4-sparcv9.S:	$(SRCDIR)/aes/asm/aest4-sparcv9.pl
	$(PERL) $(SRCDIR)/aes/asm/aest4-sparcv9.pl void $(PLFLAGS) > $@

# bn/Makefile:
# sparcv8.o:    asm/sparcv8.S
#       $(CC) $(CFLAGS) -c asm/sparcv8.S
# bn-sparcv9.o: asm/sparcv8plus.S
#       $(CC) $(CFLAGS) -c -o $@ asm/sparcv8plus.S
# sparcv9a-mont.s:      asm/sparcv9a-mont.pl
#       $(PERL) asm/sparcv9a-mont.pl $(CFLAGS) > $@
# sparcv9-mont.s:               asm/sparcv9-mont.pl
#       $(PERL) asm/sparcv9-mont.pl $(CFLAGS) > $@
# vis3-mont.s:		asm/vis3-mont.pl
# 	$(PERL) asm/vis3-mont.pl $(CFLAGS) > $@
# sparct4-mont.S:	asm/sparct4-mont.pl
# 	$(PERL) asm/sparct4-mont.pl $(CFLAGS) > $@
# sparcv9-gf2m.S:	asm/sparcv9-gf2m.pl
# 	$(PERL) asm/sparcv9-gf2m.pl $(CFLAGS) > $@
pics/sparcv8.o:	$(SRCDIR)/bn/asm/sparcv8.S
	$(COMPILE.S) -o $@ $(SRCDIR)/bn/asm/sparcv8.S
	$(POST_PROCESS_A)
pics/bn-sparcv9.o:	$(SRCDIR)/bn/asm/sparcv8plus.S
	$(COMPILE.S) -o $@ $(SRCDIR)/bn/asm/sparcv8plus.S
	$(POST_PROCESS_A)
pics/sparcv9a-mont.s:	$(SRCDIR)/bn/asm/sparcv9a-mont.pl
	$(PERL) $(SRCDIR)/bn/asm/sparcv9a-mont.pl void $(PLFLAGS) > $@
pics/sparcv9-mont.s:	$(SRCDIR)/bn/asm/sparcv9-mont.pl
	$(PERL) $(SRCDIR)/bn/asm/sparcv9-mont.pl void $(PLFLAGS) > $@
pics/vis3-mont.s:	$(SRCDIR)/bn/asm/vis3-mont.pl
	$(PERL) $(SRCDIR)/bn/asm/vis3-mont.pl void $(PLFLAGS) > $@
pics/sparct4-mont.S:	$(SRCDIR)/bn/asm/sparct4-mont.pl
	$(PERL) $(SRCDIR)/bn/asm/sparct4-mont.pl void $(PLFLAGS) > $@
pics/sparcv9-gf2m.S:	$(SRCDIR)/bn/asm/sparcv9-gf2m.pl
	$(PERL) $(SRCDIR)/bn/asm/sparcv9-gf2m.pl void $(PLFLAGS) > $@

# des/Makefile:
# des_enc-sparc.S:      asm/des_enc.m4
#       m4 -B 8192 asm/des_enc.m4 > des_enc-sparc.S
# dest4-sparcv9.s:	asm/dest4-sparcv9.pl
# 	$(PERL) asm/dest4-sparcv9.pl $(CFLAGS) > $@
pics/des_enc-sparc.S:	$(SRCDIR)/des/asm/des_enc.m4
	$(M4) -B 8192 $(SRCDIR)/des/asm/des_enc.m4 > $@
pics/dest4-sparcv9.s:	$(SRCDIR)/des/asm/dest4-sparcv9.pl
	$(PERL) $(SRCDIR)/des/asm/dest4-sparcv9.pl void $(PLFLAGS) > $@

# md5/Makefile:
# md5-sparcv9.S:	asm/md5-sparcv9.pl
# 	$(PERL) asm/md5-sparcv9.pl $@ $(CFLAGS)
pics/md5-sparcv9.S:	$(SRCDIR)/md5/asm/md5-sparcv9.pl
	$(PERL) $(SRCDIR)/md5/asm/md5-sparcv9.pl $@ $(PLFLAGS)

# modes/Makefile:
# ghash-sparcv9.s:      asm/ghash-sparcv9.pl
#       $(PERL) asm/ghash-sparcv9.pl $@ $(CFLAGS)
pics/ghash-sparcv9.s:	$(SRCDIR)/modes/asm/ghash-sparcv9.pl
	$(PERL) $(SRCDIR)/modes/asm/ghash-sparcv9.pl $@ $(PLFLAGS)

# sha/Makefile:
# sha1-sparcv9.s:       asm/sha1-sparcv9.pl;    $(PERL) asm/sha1-sparcv9.pl $@ $(CFLAGS)
# sha256-sparcv9.s:asm/sha512-sparcv9.pl;       $(PERL) asm/sha512-sparcv9.pl $@ $(CFLAGS)
# sha512-sparcv9.s:asm/sha512-sparcv9.pl;       $(PERL) asm/sha512-sparcv9.pl $@ $(CFLAGS)
pics/sha1-sparcv9.S:	$(SRCDIR)/sha/asm/sha1-sparcv9.pl
	$(PERL) $(SRCDIR)/sha/asm/sha1-sparcv9.pl $@ $(PLFLAGS)
pics/sha256-sparcv9.S:	$(SRCDIR)/sha/asm/sha512-sparcv9.pl
	$(PERL) $(SRCDIR)/sha/asm/sha512-sparcv9.pl $@ $(PLFLAGS)
pics/sha512-sparcv9.S:	$(SRCDIR)/sha/asm/sha512-sparcv9.pl
	$(PERL) $(SRCDIR)/sha/asm/sha512-sparcv9.pl $@ $(PLFLAGS)

install: all $(ROOTLIBS) $(ROOTLINKS) $(ROOTLINT)
