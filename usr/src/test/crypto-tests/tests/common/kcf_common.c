/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2015 Nexenta Systems, Inc.  All rights reserved.
 * Copyright 2019 Joyent, Inc.
 * Copyright 2023-2026 RackTop Systems, Inc.
 */

/*
 * Helpers shared by the test backends that use the KCF mechanism parameter
 * structures and CRYPTO_* error codes: cryptotest_kcf.c, which goes through
 * /dev/crypto, and cryptotest_sg.c, which calls libsoftcrypto directly.
 */

#include <stdio.h>
#include <sys/debug.h>

#include "cryptotest.h"

void
ccm_init_params(void *buf, ulong_t ulDataLen, uchar_t *pNonce,
    ulong_t ulNonceLen, uchar_t *pAAD, ulong_t ulAADLen, ulong_t ulMACLen)
{
	CK_AES_CCM_PARAMS *pp = buf;

	pp->ulDataSize = ulDataLen;
	pp->nonce = pNonce;
	pp->ulNonceSize = ulNonceLen;
	pp->authData = pAAD;
	pp->ulAuthDataSize = ulAADLen;
	pp->ulMACSize = ulMACLen;
}

size_t
ccm_param_len(void)
{
	return (sizeof (CK_AES_CCM_PARAMS));
}

/*
 * KCF always takes CK_AES_GMAC_PARAMS, but the caller may pass
 * either just the IV or IV plus AAD.
 *
 * Some tests pass ulAADLen = 0 and non-NULL pAAD, so allow that,
 * but require ulAADLen=0 if pAAD=NULL.
 */
void
gmac_init_params(void *buf, uchar_t *pIv, uchar_t *pAAD, ulong_t ulAADLen)
{
	CK_AES_GMAC_PARAMS *pp = buf;

	if (pAAD == NULL) {
		VERIFY0(ulAADLen);
	}

	pp->pIv = pIv;
	pp->pAAD = pAAD;	/* may be NULL */
	pp->ulAADLen = ulAADLen;
}

size_t
gmac_param_len(void)
{
	return (sizeof (CK_AES_GMAC_PARAMS));
}

const char *
cryptotest_errstr(int e, char *buf, size_t buflen)
{
	const char *valstr = NULL;

	switch (e) {
	case CRYPTO_SUCCESS:
		valstr = "CRYPTO_SUCCESS";
		break;
	case CRYPTO_CANCEL:
		valstr = "CRYPTO_CANCEL";
		break;
	case CRYPTO_HOST_MEMORY:
		valstr = "CRYPTO_HOST_MEMORY";
		break;
	case CRYPTO_GENERAL_ERROR:
		valstr = "CRYPTO_GENERAL_ERROR";
		break;
	case CRYPTO_FAILED:
		valstr = "CRYPTO_FAILED";
		break;
	case CRYPTO_ARGUMENTS_BAD:
		valstr = "CRYPTO_ARGUMENTS_BAD";
		break;
	case CRYPTO_ATTRIBUTE_READ_ONLY:
		valstr = "CRYPTO_ATTRIBUTE_READ_ONLY";
		break;
	case CRYPTO_ATTRIBUTE_SENSITIVE:
		valstr = "CRYPTO_ATTRIBUTE_SENSITIVE";
		break;
	case CRYPTO_ATTRIBUTE_TYPE_INVALID:
		valstr = "CRYPTO_ATTRIBUTE_TYPE_INVALID";
		break;
	case CRYPTO_ATTRIBUTE_VALUE_INVALID:
		valstr = "CRYPTO_ATTRIBUTE_VALUE_INVALID";
		break;
	case CRYPTO_CANCELED:
		valstr = "CRYPTO_CANCELED";
		break;
	case CRYPTO_DATA_INVALID:
		valstr = "CRYPTO_DATA_INVALID";
		break;
	case CRYPTO_DATA_LEN_RANGE:
		valstr = "CRYPTO_DATA_LEN_RANGE";
		break;
	case CRYPTO_DEVICE_ERROR:
		valstr = "CRYPTO_DEVICE_ERROR";
		break;
	case CRYPTO_DEVICE_MEMORY:
		valstr = "CRYPTO_DEVICE_MEMORY";
		break;
	case CRYPTO_DEVICE_REMOVED:
		valstr = "CRYPTO_DEVICE_REMOVED";
		break;
	case CRYPTO_ENCRYPTED_DATA_INVALID:
		valstr = "CRYPTO_ENCRYPTED_DATA_INVALID";
		break;
	case CRYPTO_ENCRYPTED_DATA_LEN_RANGE:
		valstr = "CRYPTO_ENCRYPTED_DATA_LEN_RANGE";
		break;
	case CRYPTO_KEY_HANDLE_INVALID:
		valstr = "CRYPTO_KEY_HANDLE_INVALID";
		break;
	case CRYPTO_KEY_SIZE_RANGE:
		valstr = "CRYPTO_KEY_SIZE_RANGE";
		break;
	case CRYPTO_KEY_TYPE_INCONSISTENT:
		valstr = "CRYPTO_KEY_TYPE_INCONSISTENT";
		break;
	case CRYPTO_KEY_NOT_NEEDED:
		valstr = "CRYPTO_KEY_NOT_NEEDED";
		break;
	case CRYPTO_KEY_CHANGED:
		valstr = "CRYPTO_KEY_CHANGED";
		break;
	case CRYPTO_KEY_NEEDED:
		valstr = "CRYPTO_KEY_NEEDED";
		break;
	case CRYPTO_KEY_INDIGESTIBLE:
		valstr = "CRYPTO_KEY_INDIGESTIBLE";
		break;
	case CRYPTO_KEY_FUNCTION_NOT_PERMITTED:
		valstr = "CRYPTO_KEY_FUNCTION_NOT_PERMITTED";
		break;
	case CRYPTO_KEY_NOT_WRAPPABLE:
		valstr = "CRYPTO_KEY_NOT_WRAPPABLE";
		break;
	case CRYPTO_KEY_UNEXTRACTABLE:
		valstr = "CRYPTO_KEY_UNEXTRACTABLE";
		break;
	case CRYPTO_MECHANISM_INVALID:
		valstr = "CRYPTO_MECHANISM_INVALID";
		break;
	case CRYPTO_MECHANISM_PARAM_INVALID:
		valstr = "CRYPTO_MECHANISM_PARAM_INVALID";
		break;
	case CRYPTO_OBJECT_HANDLE_INVALID:
		valstr = "CRYPTO_OBJECT_HANDLE_INVALID";
		break;
	case CRYPTO_OPERATION_IS_ACTIVE:
		valstr = "CRYPTO_OPERATION_IS_ACTIVE";
		break;
	case CRYPTO_OPERATION_NOT_INITIALIZED:
		valstr = "CRYPTO_OPERATION_NOT_INITIALIZED";
		break;
	case CRYPTO_PIN_INCORRECT:
		valstr = "CRYPTO_PIN_INCORRECT";
		break;
	case CRYPTO_PIN_INVALID:
		valstr = "CRYPTO_PIN_INVALID";
		break;
	case CRYPTO_PIN_LEN_RANGE:
		valstr = "CRYPTO_PIN_LEN_RANGE";
		break;
	case CRYPTO_PIN_EXPIRED:
		valstr = "CRYPTO_PIN_EXPIRED";
		break;
	case CRYPTO_PIN_LOCKED:
		valstr = "CRYPTO_PIN_LOCKED";
		break;
	case CRYPTO_SESSION_CLOSED:
		valstr = "CRYPTO_SESSION_CLOSED";
		break;
	case CRYPTO_SESSION_COUNT:
		valstr = "CRYPTO_SESSION_COUNT";
		break;
	case CRYPTO_SESSION_HANDLE_INVALID:
		valstr = "CRYPTO_SESSION_HANDLE_INVALID";
		break;
	case CRYPTO_SESSION_READ_ONLY:
		valstr = "CRYPTO_SESSION_READ_ONLY";
		break;
	case CRYPTO_SESSION_EXISTS:
		valstr = "CRYPTO_SESSION_EXISTS";
		break;
	case CRYPTO_SESSION_READ_ONLY_EXISTS:
		valstr = "CRYPTO_SESSION_READ_ONLY_EXISTS";
		break;
	case CRYPTO_SESSION_READ_WRITE_SO_EXISTS:
		valstr = "CRYPTO_SESSION_READ_WRITE_SO_EXISTS";
		break;
	case CRYPTO_SIGNATURE_INVALID:
		valstr = "CRYPTO_SIGNATURE_INVALID";
		break;
	case CRYPTO_SIGNATURE_LEN_RANGE:
		valstr = "CRYPTO_SIGNATURE_LEN_RANGE";
		break;
	case CRYPTO_TEMPLATE_INCOMPLETE:
		valstr = "CRYPTO_TEMPLATE_INCOMPLETE";
		break;
	case CRYPTO_TEMPLATE_INCONSISTENT:
		valstr = "CRYPTO_TEMPLATE_INCONSISTENT";
		break;
	case CRYPTO_UNWRAPPING_KEY_HANDLE_INVALID:
		valstr = "CRYPTO_UNWRAPPING_KEY_HANDLE_INVALID";
		break;
	case CRYPTO_UNWRAPPING_KEY_SIZE_RANGE:
		valstr = "CRYPTO_UNWRAPPING_KEY_SIZE_RANGE";
		break;
	case CRYPTO_UNWRAPPING_KEY_TYPE_INCONSISTENT:
		valstr = "CRYPTO_UNWRAPPING_KEY_TYPE_INCONSISTENT";
		break;
	case CRYPTO_USER_ALREADY_LOGGED_IN:
		valstr = "CRYPTO_USER_ALREADY_LOGGED_IN";
		break;
	case CRYPTO_USER_NOT_LOGGED_IN:
		valstr = "CRYPTO_USER_NOT_LOGGED_IN";
		break;
	case CRYPTO_USER_PIN_NOT_INITIALIZED:
		valstr = "CRYPTO_USER_PIN_NOT_INITIALIZED";
		break;
	case CRYPTO_USER_TYPE_INVALID:
		valstr = "CRYPTO_USER_TYPE_INVALID";
		break;
	case CRYPTO_USER_ANOTHER_ALREADY_LOGGED_IN:
		valstr = "CRYPTO_USER_ANOTHER_ALREADY_LOGGED_IN";
		break;
	case CRYPTO_USER_TOO_MANY_TYPES:
		valstr = "CRYPTO_USER_TOO_MANY_TYPES";
		break;
	case CRYPTO_WRAPPED_KEY_INVALID:
		valstr = "CRYPTO_WRAPPED_KEY_INVALID";
		break;
	case CRYPTO_WRAPPED_KEY_LEN_RANGE:
		valstr = "CRYPTO_WRAPPED_KEY_LEN_RANGE";
		break;
	case CRYPTO_WRAPPING_KEY_HANDLE_INVALID:
		valstr = "CRYPTO_WRAPPING_KEY_HANDLE_INVALID";
		break;
	case CRYPTO_WRAPPING_KEY_SIZE_RANGE:
		valstr = "CRYPTO_WRAPPING_KEY_SIZE_RANGE";
		break;
	case CRYPTO_WRAPPING_KEY_TYPE_INCONSISTENT:
		valstr = "CRYPTO_WRAPPING_KEY_TYPE_INCONSISTENT";
		break;
	case CRYPTO_RANDOM_SEED_NOT_SUPPORTED:
		valstr = "CRYPTO_RANDOM_SEED_NOT_SUPPORTED";
		break;
	case CRYPTO_RANDOM_NO_RNG:
		valstr = "CRYPTO_RANDOM_NO_RNG";
		break;
	case CRYPTO_DOMAIN_PARAMS_INVALID:
		valstr = "CRYPTO_DOMAIN_PARAMS_INVALID";
		break;
	case CRYPTO_BUFFER_TOO_SMALL:
		valstr = "CRYPTO_BUFFER_TOO_SMALL";
		break;
	case CRYPTO_INFORMATION_SENSITIVE:
		valstr = "CRYPTO_INFORMATION_SENSITIVE";
		break;
	case CRYPTO_NOT_SUPPORTED:
		valstr = "CRYPTO_NOT_SUPPORTED";
		break;
	case CRYPTO_QUEUED:
		valstr = "CRYPTO_QUEUED";
		break;
	case CRYPTO_BUFFER_TOO_BIG:
		valstr = "CRYPTO_BUFFER_TOO_BIG";
		break;
	case CRYPTO_INVALID_CONTEXT:
		valstr = "CRYPTO_INVALID_CONTEXT";
		break;
	case CRYPTO_INVALID_MAC:
		valstr = "CRYPTO_INVALID_MAC";
		break;
	case CRYPTO_MECH_NOT_SUPPORTED:
		valstr = "CRYPTO_MECH_NOT_SUPPORTED";
		break;
	case CRYPTO_INCONSISTENT_ATTRIBUTE:
		valstr = "CRYPTO_INCONSISTENT_ATTRIBUTE";
		break;
	case CRYPTO_NO_PERMISSION:
		valstr = "CRYPTO_NO_PERMISSION";
		break;
	case CRYPTO_INVALID_PROVIDER_ID:
		valstr = "CRYPTO_INVALID_PROVIDER_ID";
		break;
	case CRYPTO_VERSION_MISMATCH:
		valstr = "CRYPTO_VERSION_MISMATCH";
		break;
	case CRYPTO_BUSY:
		valstr = "CRYPTO_BUSY";
		break;
	case CRYPTO_UNKNOWN_PROVIDER:
		valstr = "CRYPTO_UNKNOWN_PROVIDER";
		break;
	case CRYPTO_MODVERIFICATION_FAILED:
		valstr = "CRYPTO_MODVERIFICATION_FAILED";
		break;
	case CRYPTO_OLD_CTX_TEMPLATE:
		valstr = "CRYPTO_OLD_CTX_TEMPLATE";
		break;
	case CRYPTO_WEAK_KEY:
		valstr = "CRYPTO_WEAK_KEY";
		break;
	case CRYPTO_FIPS140_ERROR:
		valstr = "CRYPTO_FIPS140_ERROR";
		break;
	default:
		valstr = "Unknown KCF error";
		break;
	}

	(void) snprintf(buf, buflen, "%s (0x%08x)", valstr, e);
	return (buf);
}
