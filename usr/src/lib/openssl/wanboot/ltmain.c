/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2013, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2015 Nexenta Systems, Inc.  All rights reserved.
 */

/*
 * Link test for wanboot-openssl.a
 * Derived from userland openssl mapfile.wanboot
 *
 * Note: never actually runs.
 */

int
main(int argc, char **argv)
{
	int x = argc;

#define	SYM(name) \
	{ \
		extern int name(int); \
		x = name(x); \
	}

SYM(ASN1_STRING_cmp)
SYM(ASN1_STRING_free)
SYM(ASN1_STRING_set)
SYM(ASN1_STRING_to_UTF8)
SYM(ASN1_STRING_type_new)
SYM(ASN1_TIME_print)
SYM(ASN1_TYPE_free)
SYM(ASN1_TYPE_new)
SYM(ASN1_TYPE_set)
SYM(ASN1_UTF8STRING_free)
SYM(ASN1_UTF8STRING_new)
SYM(ASN1_mbstring_copy)
SYM(BIO_ctrl)
SYM(BIO_free)
SYM(BIO_new)
SYM(BIO_s_file)
SYM(CRYPTO_free)
SYM(CRYPTO_malloc)
SYM(ERR_clear_error)
SYM(ERR_error_string)
SYM(ERR_get_error)
SYM(ERR_get_next_error_library)
SYM(ERR_load_strings)
SYM(ERR_peek_error)
SYM(ERR_put_error)
SYM(EVP_EncodeBlock)
SYM(EVP_PKCS82PKEY)
SYM(EVP_PKEY_free)
SYM(OBJ_nid2obj)
SYM(OBJ_obj2nid)
SYM(OPENSSL_add_all_algorithms_noconf)
SYM(OPENSSL_asc2uni)
SYM(OPENSSL_uni2asc)
SYM(PKCS12_SAFEBAG_free)
SYM(PKCS12_certbag2x509)
SYM(PKCS12_decrypt_skey)
SYM(PKCS12_free)
SYM(PKCS12_get_attr_gen)
SYM(PKCS12_unpack_authsafes)
SYM(PKCS12_unpack_p7data)
SYM(PKCS12_unpack_p7encdata)
SYM(PKCS12_verify_mac)
SYM(PKCS7_free)
SYM(PKCS8_PRIV_KEY_INFO_free)
SYM(RAND_load_file)
SYM(RAND_status)
SYM(SSL_CIPHER_get_name)
SYM(SSL_CTX_ctrl)
SYM(SSL_CTX_free)
SYM(SSL_CTX_load_verify_locations)
SYM(SSL_CTX_new)
SYM(SSL_CTX_set_cipher_list)
SYM(SSL_CTX_set_default_passwd_cb)
SYM(SSL_CTX_set_default_passwd_cb_userdata)
SYM(SSL_CTX_set_verify_depth)
SYM(SSL_CTX_use_PrivateKey)
SYM(SSL_CTX_use_PrivateKey_file)
SYM(SSL_CTX_use_certificate)
SYM(SSL_CTX_use_certificate_file)
SYM(SSL_connect)
SYM(SSL_free)
SYM(SSL_get_ciphers)
SYM(SSL_get_current_cipher)
SYM(SSL_get_error)
SYM(SSL_get_peer_certificate)
SYM(SSL_get_verify_result)
SYM(SSL_library_init)
SYM(SSL_load_error_strings)
SYM(SSL_new)
SYM(SSL_read)
SYM(SSL_set_connect_state)
SYM(SSL_set_fd)
SYM(SSL_shutdown)
SYM(SSL_write)
SYM(SSLv3_client_method)
SYM(TLSv1_2_client_method)
SYM(X509_ATTRIBUTE_free)
SYM(X509_ATTRIBUTE_new)
SYM(X509_NAME_get_text_by_NID)
SYM(X509_NAME_oneline)
SYM(X509_STORE_add_cert)
SYM(X509_alias_set1)
SYM(X509_check_private_key)
SYM(X509_free)
SYM(X509_get_issuer_name)
SYM(X509_get_subject_name)
SYM(X509_keyid_set1)
SYM(X509_verify_cert_error_string)
SYM(d2i_PKCS12_fp)
SYM(sk_delete)
SYM(sk_new_null)
SYM(sk_num)
SYM(sk_pop_free)
SYM(sk_push)
SYM(sk_value)

	return (x);
}


/*
 * Stubs representing what the standalone libsa provides
 * These don't need to work.  They just need to link.
 */

char __ctype[129];
int __iob[100];

void __assert() {}
void abort() {}
void atoi() {}
void close() {}
void connect(void) {}
void errno() {}
void fclose() {}
void feof() {}
void ferror() {}
void fflush() {}
void fgets() {}
void fopen() {}
void fprintf() {}
void fread() {}
void free() {}
void fseek() {}
void fstat() {}
void ftell() {}
void fwrite() {}
void getenv() {}
void getpid() {}
void gmtime() {}
void localtime() {}
void malloc() {}
void memchr() {}
void memcmp() {}
void memcpy() {}
void memmove() {}
void memset() {}
void open() {}
void qsort() {}
void read() {}
void realloc() {}
void send() {}
void shutdown(void) {}
void socket(void) {}
void socket_close() {}
void socket_read() {}
void stat() {}
void strcasecmp() {}
void strcat() {}
void strchr() {}
void strcmp() {}
void strcpy() {}
void strerror() {}
void strlen() {}
void strncasecmp() {}
void strncmp() {}
void strncpy() {}
void strrchr() {}
void strtoul() {}
void time() {}
void tolower() {}
void vfprintf() {}
