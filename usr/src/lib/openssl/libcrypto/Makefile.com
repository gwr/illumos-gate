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

LBASE=	libcrypto
LPREF=	libsunw_crypto
LIBRARY= $(LPREF).a
VERS= .1

LIBOBJobjects= o_names.o obj_dat.o obj_lib.o obj_err.o obj_xref.o
LIBOBJmd2=md2_dgst.o md2_one.o
LIBOBJmd4=md4_dgst.o md4_one.o
LIBOBJmd5=md5_dgst.o md5_one.o $(MD5_ASM_OBJ)
LIBOBJsha=sha_dgst.o sha1dgst.o sha_one.o sha1_one.o sha256.o sha512.o $(SHA1_ASM_OBJ)
LIBOBJhmac=hmac.o hm_ameth.o hm_pmeth.o
LIBOBJripemd=rmd_dgst.o rmd_one.o $(RMD160_ASM_OBJ)
LIBOBJdes= set_key.o  ecb_enc.o  cbc_enc.o \
	      ecb3_enc.o cfb64enc.o cfb64ede.o cfb_enc.o  ofb64ede.o \
	      enc_read.o enc_writ.o ofb64enc.o \
	      ofb_enc.o  str2key.o  pcbc_enc.o qud_cksm.o rand_key.o \
	      ${DES_ENC} \
	      fcrypt.o xcbc_enc.o rpc_enc.o  cbc_cksm.o \
	      ede_cbcm_enc.o des_old.o des_old2.o read2pwd.o
LIBOBJaes=aes_misc.o aes_ecb.o aes_cfb.o aes_ofb.o aes_ctr.o aes_ige.o aes_wrap.o \
       $(AES_ENC)
LIBOBJrc2=rc2_ecb.o rc2_skey.o rc2_cbc.o rc2cfb64.o rc2ofb64.o
LIBOBJrc4=$(RC4_ENC) rc4_utl.o
LIBOBJbf=bf_skey.o bf_ecb.o $(BF_ENC) bf_cfb64.o bf_ofb64.o
LIBOBJcast=c_skey.o c_ecb.o $(CAST_ENC) c_cfb64.o c_ofb64.o
LIBOBJcamellia= cmll_ecb.o cmll_ofb.o cmll_cfb.o cmll_ctr.o cmll_utl.o $(CMLL_ENC)
LIBOBJmodes= cbc128.o ctr128.o cts128.o cfb128.o ofb128.o gcm128.o \
	      ccm128.o xts128.o $(MODES_ASM_OBJ)
LIBOBJbn=	bn_add.o bn_div.o bn_exp.o bn_lib.o bn_ctx.o bn_mul.o bn_mod.o \
	      bn_print.o bn_rand.o bn_shift.o bn_word.o bn_blind.o \
	      bn_kron.o bn_sqrt.o bn_gcd.o bn_prime.o bn_err.o bn_sqr.o $(BN_ASM) \
	      bn_recp.o bn_mont.o bn_mpi.o bn_exp2.o bn_gf2m.o bn_nist.o \
	      bn_depr.o bn_const.o bn_x931p.o
LIBOBJrsa= rsa_eay.o rsa_gen.o rsa_lib.o rsa_sign.o rsa_saos.o rsa_err.o \
	      rsa_pk1.o rsa_ssl.o rsa_none.o rsa_oaep.o rsa_chk.o rsa_null.o \
	      rsa_pss.o rsa_x931.o rsa_asn1.o rsa_depr.o rsa_ameth.o rsa_prn.o \
	      rsa_pmeth.o rsa_crpt.o
LIBOBJdsa= dsa_gen.o dsa_key.o dsa_lib.o dsa_asn1.o dsa_vrf.o dsa_sign.o \
	      dsa_err.o dsa_ossl.o dsa_depr.o dsa_ameth.o dsa_pmeth.o dsa_prn.o
LIBOBJdh= dh_asn1.o dh_gen.o dh_key.o dh_lib.o dh_check.o dh_err.o dh_depr.o \
	      dh_ameth.o dh_pmeth.o dh_prn.o
LIBOBJdso= dso_dl.o dso_dlfcn.o dso_err.o dso_lib.o dso_null.o \
	      dso_openssl.o dso_win32.o dso_vms.o dso_beos.o
LIBOBJengine= eng_err.o eng_lib.o eng_list.o eng_init.o eng_ctrl.o \
	      eng_table.o eng_pkey.o eng_fat.o eng_all.o \
	      tb_rsa.o tb_dsa.o tb_ecdsa.o tb_dh.o tb_ecdh.o tb_rand.o tb_store.o \
	      tb_cipher.o tb_digest.o tb_pkmeth.o tb_asnmth.o \
	      eng_openssl.o eng_cnf.o eng_dyn.o eng_cryptodev.o \
	      eng_rsax.o eng_rdrand.o
LIBOBJbuffer= buffer.o buf_str.o buf_err.o
LIBOBJbio= bio_lib.o bio_cb.o bio_err.o \
	      bss_mem.o bss_null.o bss_fd.o \
	      bss_file.o bss_sock.o bss_conn.o \
	      bf_null.o bf_buff.o b_print.o b_dump.o \
	      b_sock.o bss_acpt.o bf_nbio.o bss_log.o bss_bio.o \
	      bss_dgram.o
LIBOBJstack=stack.o
LIBOBJlhash=lhash.o lh_stats.o
LIBOBJrand=md_rand.o randfile.o rand_lib.o rand_err.o rand_egd.o \
	      rand_win.o rand_unix.o rand_os2.o rand_nw.o
LIBOBJerr=err.o err_all.o err_prn.o
LIBOBJevp=	encode.o digest.o evp_enc.o evp_key.o evp_acnf.o evp_cnf.o \
	      e_des.o e_bf.o e_idea.o e_des3.o e_camellia.o\
	      e_rc4.o e_aes.o names.o e_seed.o \
	      e_xcbc_d.o e_rc2.o e_cast.o e_rc5.o \
	      m_null.o m_md2.o m_md4.o m_md5.o m_sha.o m_sha1.o m_wp.o \
	      m_dss.o m_dss1.o m_mdc2.o m_ripemd.o m_ecdsa.o\
	      p_open.o p_seal.o p_sign.o p_verify.o p_lib.o p_enc.o p_dec.o \
	      bio_md.o bio_b64.o bio_enc.o evp_err.o e_null.o \
	      c_all.o c_allc.o c_alld.o evp_lib.o bio_ok.o \
	      evp_pkey.o evp_pbe.o p5_crpt.o p5_crpt2.o \
	      e_old.o pmeth_lib.o pmeth_fn.o pmeth_gn.o m_sigver.o evp_fips.o \
	      e_aes_cbc_hmac_sha1.o e_rc4_hmac_md5.o
LIBOBJasn1= a_object.o a_bitstr.o a_utctm.o a_gentm.o a_time.o a_int.o a_octet.o \
	      a_print.o a_type.o a_set.o a_dup.o a_d2i_fp.o a_i2d_fp.o \
	      a_enum.o a_utf8.o a_sign.o a_digest.o a_verify.o a_mbstr.o a_strex.o \
	      x_algor.o x_val.o x_pubkey.o x_sig.o x_req.o x_attrib.o x_bignum.o \
	      x_long.o x_name.o x_x509.o x_x509a.o x_crl.o x_info.o x_spki.o nsseq.o \
	      x_nx509.o d2i_pu.o d2i_pr.o i2d_pu.o i2d_pr.o \
	      t_req.o t_x509.o t_x509a.o t_crl.o t_pkey.o t_spki.o t_bitst.o \
	      tasn_new.o tasn_fre.o tasn_enc.o tasn_dec.o tasn_utl.o tasn_typ.o \
	      tasn_prn.o ameth_lib.o \
	      f_int.o f_string.o n_pkey.o \
	      f_enum.o x_pkey.o a_bool.o x_exten.o bio_asn1.o bio_ndef.o asn_mime.o \
	      asn1_gen.o asn1_par.o asn1_lib.o asn1_err.o a_bytes.o a_strnid.o \
	      evp_asn1.o asn_pack.o p5_pbe.o p5_pbev2.o p8_pkey.o asn_moid.o

LIBOBJpem=	pem_sign.o pem_seal.o pem_info.o pem_lib.o pem_all.o pem_err.o \
	      pem_x509.o pem_xaux.o pem_oth.o pem_pk8.o pem_pkey.o pvkfmt.o
LIBOBJx509= x509_def.o x509_d2.o x509_r2x.o x509_cmp.o \
	      x509_obj.o x509_req.o x509spki.o x509_vfy.o \
	      x509_set.o x509cset.o x509rset.o x509_err.o \
	      x509name.o x509_v3.o x509_ext.o x509_att.o \
	      x509type.o x509_lu.o x_all.o x509_txt.o \
	      x509_trs.o by_file.o by_dir.o x509_vpm.o
LIBOBJx509v3= v3_bcons.o v3_bitst.o v3_conf.o v3_extku.o v3_ia5.o v3_lib.o \
v3_prn.o v3_utl.o v3err.o v3_genn.o v3_alt.o v3_skey.o v3_akey.o v3_pku.o \
v3_int.o v3_enum.o v3_sxnet.o v3_cpols.o v3_crld.o v3_purp.o v3_info.o \
v3_ocsp.o v3_akeya.o v3_pmaps.o v3_pcons.o v3_ncons.o v3_pcia.o v3_pci.o \
pcy_cache.o pcy_node.o pcy_data.o pcy_map.o pcy_tree.o pcy_lib.o \
v3_asid.o v3_addr.o
LIBOBJconf=	conf_err.o conf_lib.o conf_api.o conf_def.o conf_mod.o \
	      conf_mall.o conf_sap.o
LIBOBJtxt_db=txt_db.o
LIBOBJpkcs7= pk7_asn1.o pk7_lib.o pkcs7err.o pk7_doit.o pk7_smime.o pk7_attr.o \
	      pk7_mime.o bio_pk7.o
LIBOBJpkcs12= p12_add.o p12_asn.o p12_attr.o p12_crpt.o p12_crt.o p12_decr.o \
	      p12_init.o p12_key.o p12_kiss.o p12_mutl.o\
	      p12_utl.o p12_npas.o pk12err.o p12_p8d.o p12_p8e.o
LIBOBJcomp=	comp_lib.o comp_err.o \
	      c_rle.o c_zlib.o
LIBOBJocsp= ocsp_asn.o ocsp_ext.o ocsp_ht.o ocsp_lib.o ocsp_cl.o \
	      ocsp_srv.o ocsp_prn.o ocsp_vfy.o ocsp_err.o
LIBOBJui= ui_err.o ui_lib.o ui_openssl.o ui_util.o $(COMPATOBJ)
LIBOBJkrb5= krb5_asn.o
LIBOBJcms= cms_lib.o cms_asn1.o cms_att.o cms_io.o cms_smime.o cms_err.o \
	      cms_sd.o cms_dd.o cms_cd.o cms_env.o cms_enc.o cms_ess.o \
	      cms_pwri.o
LIBOBJpqueue=pqueue.o
LIBOBJts= ts_err.o ts_req_utils.o ts_req_print.o ts_rsp_utils.o ts_rsp_print.o \
	      ts_rsp_sign.o ts_rsp_verify.o ts_verify_ctx.o ts_lib.o ts_conf.o \
	      ts_asn1.o
LIBOBJsrp=srp_lib.o srp_vfy.o
LIBOBJcmac=cmac.o cm_ameth.o cm_pmeth.o

COMMONO= \
        cryptlib.o mem.o mem_dbg.o cversion.o ex_data.o cpt_err.o ebcdic.o \
	uid.o o_time.o o_str.o o_dir.o o_fips.o o_init.o fips_ers.o \
	$(CPUID_OBJ)

OBJECTS= \
	$(LIBOBJobjects) $(LIBOBJmd2) $(LIBOBJmd4) $(LIBOBJmd5) \
	$(LIBOBJsha) $(LIBOBJhmac) $(LIBOBJripemd) $(LIBOBJdes) \
	$(LIBOBJaes) $(LIBOBJrc2) $(LIBOBJrc4) $(LIBOBJbf) \
	$(LIBOBJcast) $(LIBOBJcamellia) $(LIBOBJmodes) $(LIBOBJbn) \
	$(LIBOBJrsa) $(LIBOBJdsa) $(LIBOBJdh) $(LIBOBJdso) \
	$(LIBOBJengine) $(LIBOBJbuffer) $(LIBOBJbio) $(LIBOBJstack) \
	$(LIBOBJlhash) $(LIBOBJrand) $(LIBOBJerr) $(LIBOBJevp) \
	$(LIBOBJasn1) $(LIBOBJpem) $(LIBOBJx509) $(LIBOBJx509v3) \
	$(LIBOBJconf) $(LIBOBJtxt_db) $(LIBOBJpkcs7) $(LIBOBJpkcs12) \
	$(LIBOBJcomp) $(LIBOBJocsp) $(LIBOBJui) $(LIBOBJkrb5) \
	$(LIBOBJcms) $(LIBOBJpqueue) $(LIBOBJts) $(LIBOBJsrp) \
	$(LIBOBJcmac) $(COMMONO)

include ../../../Makefile.lib
include ../../../Makefile.rootfs

LIBSRCobjects= objects/o_names.c objects/obj_dat.c objects/obj_lib.c objects/obj_err.c objects/obj_xref.c
LIBSRCmd2= md2/md2_dgst.c md2/md2_one.c
LIBSRCmd4= md4/md4_dgst.c md4/md4_one.c
LIBSRCmd5= md5/md5_dgst.c md5/md5_one.c
LIBSRCsha= sha/sha_dgst.c sha/sha1dgst.c sha/sha_one.c sha/sha1_one.c sha/sha256.c sha/sha512.c
LIBSRChmac= hmac/hmac.c hmac/hm_ameth.c hmac/hm_pmeth.c
LIBSRCripemd= ripemd/rmd_dgst.c ripemd/rmd_one.c
LIBSRCdes= des/cbc_cksm.c des/cbc_enc.c des/cfb64enc.c des/cfb_enc.c \
	      des/ecb3_enc.c des/ecb_enc.c  des/enc_read.c des/enc_writ.c \
	      des/fcrypt.c des/ofb64enc.c des/ofb_enc.c  des/pcbc_enc.c \
	      des/qud_cksm.c des/rand_key.c des/rpc_enc.c  des/set_key.c  \
	      des/des_enc.c des/fcrypt_b.c \
	      des/xcbc_enc.c \
	      des/str2key.c  des/cfb64ede.c des/ofb64ede.c des/ede_cbcm_enc.c des/des_old.c des/des_old2.c \
	      des/read2pwd.c
LIBSRCaes= aes/aes_core.c aes/aes_misc.c aes/aes_ecb.c aes/aes_cbc.c aes/aes_cfb.c aes/aes_ofb.c \
       aes/aes_ctr.c aes/aes_ige.c aes/aes_wrap.c
LIBSRCrc2= rc2/rc2_ecb.c rc2/rc2_skey.c rc2/rc2_cbc.c rc2/rc2cfb64.c rc2/rc2ofb64.c
LIBSRCrc4= rc4/rc4_skey.c rc4/rc4_enc.c rc4/rc4_utl.c
LIBSRCbf= bf/bf_skey.c bf/bf_ecb.c bf/bf_enc.c bf/bf_cfb64.c bf/bf_ofb64.c
LIBSRCcast= cast/c_skey.c cast/c_ecb.c cast/c_enc.c cast/c_cfb64.c cast/c_ofb64.c
LIBSRCcamellia= camellia/camellia.c camellia/cmll_misc.c camellia/cmll_ecb.c camellia/cmll_cbc.c camellia/cmll_ofb.c \
	         camellia/cmll_cfb.c camellia/cmll_ctr.c camellia/cmll_utl.c
LIBSRCmodes= modes/cbc128.c modes/ctr128.c modes/cts128.c modes/cfb128.c modes/ofb128.c modes/gcm128.c \
	      modes/ccm128.c modes/xts128.c
LIBSRCbn= bn/bn_add.c bn/bn_div.c bn/bn_exp.c bn/bn_lib.c bn/bn_ctx.c bn/bn_mul.c bn/bn_mod.c \
	      bn/bn_print.c bn/bn_rand.c bn/bn_shift.c bn/bn_word.c bn/bn_blind.c \
	      bn/bn_kron.c bn/bn_sqrt.c bn/bn_gcd.c bn/bn_prime.c bn/bn_err.c bn/bn_sqr.c bn/bn_asm.c \
	      bn/bn_recp.c bn/bn_mont.c bn/bn_mpi.c bn/bn_exp2.c bn/bn_gf2m.c bn/bn_nist.c \
	      bn/bn_depr.c bn/bn_const.c bn/bn_x931p.c
LIBSRCrsa= rsa/rsa_eay.c rsa/rsa_gen.c rsa/rsa_lib.c rsa/rsa_sign.c rsa/rsa_saos.c rsa/rsa_err.c \
	      rsa/rsa_pk1.c rsa/rsa_ssl.c rsa/rsa_none.c rsa/rsa_oaep.c rsa/rsa_chk.c rsa/rsa_null.c \
	      rsa/rsa_pss.c rsa/rsa_x931.c rsa/rsa_asn1.c rsa/rsa_depr.c rsa/rsa_ameth.c rsa/rsa_prn.c \
	      rsa/rsa_pmeth.c rsa/rsa_crpt.c
LIBSRCdsa= dsa/dsa_gen.c dsa/dsa_key.c dsa/dsa_lib.c dsa/dsa_asn1.c dsa/dsa_vrf.c dsa/dsa_sign.c \
	      dsa/dsa_err.c dsa/dsa_ossl.c dsa/dsa_depr.c dsa/dsa_ameth.c dsa/dsa_pmeth.c dsa/dsa_prn.c
LIBSRCdh= dh/dh_asn1.c dh/dh_gen.c dh/dh_key.c dh/dh_lib.c dh/dh_check.c dh/dh_err.c dh/dh_depr.c \
	      dh/dh_ameth.o dh/dh_pmeth.o dh/dh_prn.o
LIBSRCdso= dso/dso_dl.c dso/dso_dlfcn.c dso/dso_err.c dso/dso_lib.c dso/dso_null.c \
	      dso/dso_openssl.c dso/dso_win32.c dso/dso_vms.c dso/dso_beos.c
LIBSRCengine= engine/eng_err.c engine/eng_lib.c engine/eng_list.c engine/eng_init.c engine/eng_ctrl.c \
	      engine/eng_table.o engine/eng_pkey.o engine/eng_fat.o engine/eng_all.o \
	      tb_rsa.o tb_dsa.o tb_ecdsa.o tb_dh.o tb_ecdh.o tb_rand.o tb_store.o \
	      tb_cipher.o tb_digest.o tb_pkmeth.o tb_asnmth.o \
	      engine/eng_openssl.o engine/eng_cnf.o engine/eng_dyn.o engine/eng_cryptodev.o \
	      engine/eng_rsax.o engine/eng_rdrand.o
LIBSRCbuffer= buffer/buffer.c buffer/buf_str.c buffer/buf_err.c
LIBSRCbio= bio/bio_lib.c bio/bio_cb.c bio/bio_err.c \
	      bio/bss_mem.c bio/bss_null.c bio/bss_fd.c \
	      bio/bss_file.c bio/bss_sock.c bio/bss_conn.c \
	      bf_null.c bf_buff.c b_print.c b_dump.c \
	      b_sock.c bio/bss_acpt.c bf_nbio.c bio/bss_log.c bio/bss_bio.c \
	      bio/bss_dgram.c
LIBSRCstack= stack/stack.c
LIBSRClhash= lhash/lhash.c lhash/lh_stats.c
LIBSRCrand= rand/md_rand.c rand/randfile.c rand/rand_lib.c rand/rand_err.c rand/rand_egd.c \
	      rand/rand_win.c rand/rand_unix.c rand/rand_os2.c rand/rand_nw.c
LIBSRCerr= err/err.c err/err_all.c err/err_prn.c
LIBSRCevp= evp/encode.c evp/digest.c evp/evp_enc.c evp/evp_key.c evp/evp_acnf.c evp/evp_cnf.c \
	      evp/e_des.c evp/e_bf.c evp/e_idea.c evp/e_des3.c evp/e_camellia.c\
	      evp/e_rc4.c evp/e_aes.c names.c evp/e_seed.c \
	      evp/e_xcbc_d.c evp/e_rc2.c evp/e_cast.c evp/e_rc5.c \
	      evp/m_null.c evp/m_md2.c evp/m_md4.c evp/m_md5.c evp/m_sha.c evp/m_sha1.c evp/m_wp.c \
	      evp/m_dss.c evp/m_dss1.c evp/m_mdc2.c evp/m_ripemd.c evp/m_ecdsa.c\
	      evp/p_open.c evp/p_seal.c evp/p_sign.c evp/p_verify.c evp/p_lib.c evp/p_enc.c evp/p_dec.c \
	      evp/bio_md.c evp/bio_b64.c evp/bio_enc.c evp/evp_err.c evp/e_null.c \
	      evp/c_all.c evp/c_allc.c evp/c_alld.c evp/evp_lib.c evp/bio_ok.c \
	      evevp/p_pkey.c evevp/p_pbe.c p5_crpt.c p5_crpt2.c \
	      evp/e_old.c evp/pmeth_lib.c evp/pmeth_fn.c evp/pmeth_gn.c evp/m_sigver.c evp/evp_fips.c\
	      evp/e_aes_cbc_hmac_sha1.c evp/e_rc4_hmac_md5.c
LIBSRCasn1= asn1/a_object.c asn1/a_bitstr.c asn1/a_utctm.c asn1/a_gentm.c asn1/a_time.c asn1/a_int.c asn1/a_octet.c \
	      asn1/a_print.c asn1/a_type.c asn1/a_set.c asn1/a_dup.c asn1/a_d2i_fp.c asn1/a_i2d_fp.c \
	      asn1/a_enum.c asn1/a_utf8.c asn1/a_sign.c asn1/a_digest.c asn1/a_verify.c asn1/a_mbstr.c asn1/a_strex.c \
	      asn1/x_algor.c asn1/x_val.c asn1/x_pubkey.c asn1/x_sig.c asn1/x_req.c asn1/x_attrib.c asn1/x_bignum.c \
	      asn1/x_long.c asn1/x_name.c asn1/x_x509.c asn1/x_x509a.c asn1/x_crl.c asn1/x_info.c asn1/x_spki.c asn1/nsseq.c \
	      asn1/x_nx509.c asn1/d2i_pu.c asn1/d2i_pr.c asn1/i2d_pu.c asn1/i2d_pr.c\
	      asn1/t_req.c asn1/t_x509.c asn1/t_x509a.c asn1/t_crl.c asn1/t_pkey.c asn1/t_spki.c asn1/t_bitst.c \
	      asn1/tasn_new.c asn1/tasn_fre.c asn1/tasn_enc.c asn1/tasn_dec.c asn1/tasn_utl.c asn1/tasn_typ.c \
	      asn1/tasn_prn.c asn1/ameth_lib.c \
	      asn1/f_int.c asn1/f_string.c asn1/n_pkey.c \
	      asn1/f_enum.c asn1/x_pkey.c asn1/a_bool.c asn1/x_exten.c asn1/bio_asn1.c asn1/bio_ndef.c asn1/asn_mime.c \
	      asn1/asn1_gen.c asn1/asn1_par.c asn1/asn1_lib.c asn1/asn1_err.c asn1/a_bytes.c asn1/a_strnid.c \
	      asn1/evp_asn1.c asn1/asn_pack.c asn1/p5_pbe.c asn1/p5_pbev2.c asn1/p8_pkey.c asn1/asn_moid.c
LIBSRCpem= pem/pem_sign.c pem/pem_seal.c pem/pem_info.c pem/pem_lib.c pem/pem_all.c pem/pem_err.c \
	      pem/pem_x509.c pem/pem_xaux.c pem/pem_oth.c pem/pem_pk8.c pem/pem_pkey.c pem/pvkfmt.c
LIBSRCx509= x509/x509_def.c x509/x509_d2.c x509/x509_r2x.c x509/x509_cmp.c \
	      x509/x509_obj.c x509/x509_req.c x509spki.c x509/x509_vfy.c \
	      x509/x509_set.c x509cset.c x509rset.c x509/x509_err.c \
	      x509name.c x509/x509_v3.c x509/x509_ext.c x509/x509_att.c \
	      x509type.c x509/x509_lu.c x_all.c x509/x509_txt.c \
	      x509/x509_trs.c by_file.c by_dir.c x509/x509_vpm.c
LIBSRCx509v3= x509v3/v3_bcons.c x509v3/v3_bitst.c x509v3/v3_conf.c x509v3/v3_extku.c x509v3/v3_ia5.c x509v3/v3_lib.c \
x509v3/v3_prn.c x509v3/v3_utl.c v3err.c x509v3/v3_genn.c x509v3/v3_alt.c x509v3/v3_skey.c x509v3/v3_akey.c x509v3/v3_pku.c \
x509v3/v3_int.c x509v3/v3_enum.c x509v3/v3_sxnet.c x509v3/v3_cpols.c x509v3/v3_crld.c x509v3/v3_purp.c x509v3/v3_info.c \
x509v3/v3_ocsp.c x509v3/v3_akeya.c x509v3/v3_pmaps.c x509v3/v3_pcons.c x509v3/v3_ncons.c x509v3/v3_pcia.c x509v3/v3_pci.c \
pcy_cache.c pcy_node.c pcy_data.c pcy_map.c pcy_tree.c pcy_lib.c \
x509v3/v3_asid.c x509v3/v3_addr.c
LIBSRCconf= conf/conf_err.c conf/conf_lib.c conf/conf_api.c conf/conf_def.c conf/conf_mod.c \
	       conf/conf_mall.c conf/conf_sap.c
LIBSRCtxt_db= txt_db/txt_db.c
LIBSRCpkcs7= pkcs7/pk7_asn1.c pkcs7/pk7_lib.c pkcs7/pkcs7err.c pkcs7/pk7_doit.c pkcs7/pk7_smime.c pkcs7/pk7_attr.c \
	      pkcs7/pk7_mime.c pkcs7/bio_pk7.c
LIBSRCpkcs12= pkcs12/p12_add.c pkcs12/p12_asn.c pkcs12/p12_attr.c pkcs12/p12_crpt.c pkcs12/p12_crt.c pkcs12/p12_decr.c \
	      pkcs12/p12_init.c pkcs12/p12_key.c pkcs12/p12_kiss.c pkcs12/p12_mutl.c\
	      pkcs12/p12_utl.c pkcs12/p12_npas.c pk12err.c pkcs12/p12_p8d.c pkcs12/p12_p8e.c
LIBSRCcomp= comp/comp_lib.c comp/comp_err.c \
	      comp/c_rle.c comp/c_zlib.c
LIBSRCocsp= ocsp/ocsp_asn.c ocsp/ocsp_ext.c ocsp/ocsp_ht.c ocsp/ocsp_lib.c ocsp/ocsp_cl.c \
	      ocsp/ocsp_srv.c ocsp/ocsp_prn.c ocsp/ocsp_vfy.c ocsp/ocsp_err.c
COMPATSRC= ui_compat.c
LIBSRCui= ui/ui_err.c ui/ui_lib.c ui/ui_openssl.c ui/ui_util.c ui/$(COMPATSRC)
LIBSRCkrb5= krb5/krb5_asn.c
LIBSRCcms= cms/cms_lib.c cms/cms_asn1.c cms/cms_att.c cms/cms_io.c cms/cms_smime.c cms/cms_err.c \
	      cms/cms_sd.c cms/cms_dd.c cms/cms_cd.c cms/cms_env.c cms/cms_enc.c cms/cms_ess.c \
	      cms/cms_pwri.c
LIBSRCpqueue= pqueue/pqueue.c
LIBSRCts= ts/ts_err.c ts/ts_req_utils.c ts/ts_req_print.c ts/ts_rsp_utils.c ts/ts_rsp_print.c \
	      ts/ts_rsp_sign.c ts/ts_rsp_verify.c ts/ts_verify_ctx.c ts/ts_lib.c ts/ts_conf.c \
	      ts/ts_asn1.c
LIBSRCsrp= srp/srp_lib.c srp/srp_vfy.c
LIBSRCcmac= cmac/cmac.c cmac/cm_ameth.c cmac/cm_pmeth.c

COMMS=	cryptlib.c mem.c mem_clr.c mem_dbg.c cversion.c ex_data.c cpt_err.c \
	ebcdic.c uid.c o_time.c o_str.c o_dir.c o_fips.c o_init.c fips_ers.c
SRCS= \
	$(LIBSRCobjects) $(LIBSRCmd2) $(LIBSRCmd4) $(LIBSRCmd5) \
	$(LIBSRCsha) $(LIBSRChmac) $(LIBSRCripemd) $(LIBSRCdes) \
	$(LIBSRCaes) $(LIBSRCrc2) $(LIBSRCrc4) $(LIBSRCbf) \
	$(LIBSRCcast) $(LIBSRCcamellia) $(LIBSRCmodes) $(LIBSRCbn) \
	$(LIBSRCrsa) $(LIBSRCdsa) $(LIBSRCdh) $(LIBSRCdso) \
	$(LIBSRCengine) $(LIBSRCbuffer) $(LIBSRCbio) $(LIBSRCstack) \
	$(LIBSRClhash) $(LIBSRCrand) $(LIBSRCerr) $(LIBSRCevp) \
	$(LIBSRCasn1) $(LIBSRCpem) $(LIBSRCx509) $(LIBSRCx509v3) \
	$(LIBSRCconf) $(LIBSRCtxt_db) $(LIBSRCpkcs7) $(LIBSRCpkcs12) \
	$(LIBSRCcomp) $(LIBSRCocsp) $(LIBSRCui) $(LIBSRCkrb5) \
	$(LIBSRCcms) $(LIBSRCpqueue) $(LIBSRCts) $(LIBSRCsrp) \
	$(LIBSRCcmac) $(COMMS)

SRCDIR=		../common
INCDIR=		../../include

LIBS =		$(DYNLIB) $(LINTLIB)
$(LINTLIB) :=	SRCS = $(SRCDIR)/$(LINTSRC)
LDLIBS +=	-lsocket -lnsl -lc

# Omit the prefix for the link target but retain it for the library file
ROOTLINKS=	$(ROOTLIBDIR)/$(LBASE).so
ROOTLINKS64=	$(ROOTLIBDIR64)/$(LBASE).so

$(ROOTLINKS): $(ROOTLIBDIR)/$(LIBLINKS)$(VERS)
	$(INS.liblink)

$(ROOTLINKS64): $(ROOTLIBDIR64)/$(LIBLINKS)$(VERS)
	$(INS.liblink64)

COPTFLAG = -_cc=-xO3 -_gcc=-O3
COPTFLAG64 = -_cc=-xO3 -_gcc=-O3

CFLAGS	+=	$(CCVERBOSE)
CPPFLAGS +=	-I$(INCDIR) -I$(SRCDIR) -I$(SRCDIR)/asn1 -I$(SRCDIR)/evp \
		-I$(SRCDIR)/modes
# New name for inline assembler keyword
CPPFLAGS +=	-Dasm=__asm__
CPPFLAGS +=	\
	-D_REENTRANT	\
	-DAES_ASM	\
	-DDSO_DLFCN	\
	-DGHASH_ASM	\
	-DHAVE_DLFCN_H	\
	-DMD5_ASM	\
	-DNO_WINDOWS_BRAINDEATH	\
	-DOPENSSL_BN_ASM_MONT	\
	-DOPENSSL_PIC	\
	-DOPENSSL_THREADS	\
	-DSHA1_ASM	\
	-DSHA256_ASM	\
	-DSHA512_ASM	\
	-DSOLARIS_OPENSSL
CPPFLAGS +=	\
	-DOPENSSL_NO_EC	\
	-DOPENSSL_NO_EC_NISTP_64_GCC_128	\
	-DOPENSSL_NO_ECDH	\
	-DOPENSSL_NO_ECDSA	\
	-DOPENSSL_NO_GMP	\
	-DOPENSSL_NO_GOST	\
	-DOPENSSL_NO_HW_4758_CCA	\
	-DOPENSSL_NO_HW_AEP	\
	-DOPENSSL_NO_HW_ATALLA	\
	-DOPENSSL_NO_HW_CHIL	\
	-DOPENSSL_NO_HW_CSWIFT	\
	-DOPENSSL_NO_HW_GMP	\
	-DOPENSSL_NO_HW_NCIPHER	\
	-DOPENSSL_NO_HW_NURON	\
	-DOPENSSL_NO_HW_PADLOCK	\
	-DOPENSSL_NO_HW_SUREWARE	\
	-DOPENSSL_NO_HW_UBSEC	\
	-DOPENSSL_NO_IDEA	\
	-DOPENSSL_NO_JPAKE	\
	-DOPENSSL_NO_MDC2	\
	-DOPENSSL_NO_RC3	\
	-DOPENSSL_NO_RC5	\
	-DOPENSSL_NO_RFC3779	\
	-DOPENSSL_NO_SCTP	\
	-DOPENSSL_NO_SEED	\
	-DOPENSSL_NO_STORE	\
	-DOPENSSL_NO_WHIRLPOOL	\
	-DOPENSSL_NO_WHRLPOOL

LINTFLAGS64 += -errchk=longptr64

CERRWARN +=	-_gcc=-Wno-unused-label
CERRWARN +=	-_gcc=-Wno-uninitialized
CERRWARN +=	-_gcc=-Wno-old-style-declaration
CERRWARN +=	-_gcc=-Wno-unused-variable

CERRWARN += -erroff=E_END_OF_LOOP_CODE_NOT_REACHED
CERRWARN += -erroff=E_TYP_STORAGE_CLASS_OBSOLESCENT
CERRWARN += -erroff=E_CONST_PROMOTED_UNSIGNED_LONG
CERRWARN += -erroff=E_CONST_PROMOTED_UNSIGNED_LL

.KEEP_STATE:

all:	$(LIBS)

# Suppress lint check
lint:

pics/%.o: pics/%.s
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_A)

pics/%.o: $(SRCDIR)/objects/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/md2/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/md4/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/md5/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/md5/asm/%.s
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_A)

pics/%.o: $(SRCDIR)/sha/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/sha/asm/%.s
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_A)

pics/%.o: $(SRCDIR)/hmac/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/ripemd/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/ripemd/asm/%.s
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_A)

pics/%.o: $(SRCDIR)/des/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/des/asm/%.s
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_A)

pics/%.o: $(SRCDIR)/aes/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/aes/asm/%.s
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_A)

pics/%.o: $(SRCDIR)/rc2/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/rc4/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/rc4/asm/%.s
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_A)

pics/%.o: $(SRCDIR)/bf/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/bf/asm/%.s
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_A)

pics/%.o: $(SRCDIR)/cast/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/cast/asm/%.s
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_A)

pics/%.o: $(SRCDIR)/camellia/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/camellia/asm/%.s
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_A)

pics/%.o: $(SRCDIR)/modes/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/modes/asm/%.s
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_A)

pics/%.o: $(SRCDIR)/bn/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/bn/asm/%.s
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_A)

pics/%.o: $(SRCDIR)/rsa/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/dsa/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/dh/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/dso/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/engine/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/buffer/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/bio/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/stack/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/lhash/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/rand/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/err/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/evp/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/asn1/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/pem/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/x509/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/x509v3/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/conf/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/txt_db/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/pkcs7/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/pkcs12/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/comp/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/ocsp/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/ui/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/krb5/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/cms/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/pqueue/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/ts/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/srp/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/%.o: $(SRCDIR)/cmac/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

include $(SRC)/lib/Makefile.targ
