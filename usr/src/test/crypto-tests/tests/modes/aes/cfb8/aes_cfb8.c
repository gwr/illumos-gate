/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 */

/*
 * Copyright 2026 RackTop Systems, Inc.
 */

#include <strings.h>
#include <stdio.h>
#include <aes/aes_impl.h>

#include "cryptotest.h"
#include "aes_cfb8.h"

static size_t updatelens[] = {
	1, 2, 3, CTEST_UPDATELEN_WHOLE, CTEST_UPDATELEN_END
};

int
main(void)
{
	int errs = 0;
	int i;
	uint8_t output[32];
	cryptotest_t args = {
		.out = output,
		.outlen = sizeof (output),
		.plen = AES_BLOCK_LEN,
		.mechname = SUN_CKM_AES_CFB8,
		.updatelens = updatelens
	};

	for (i = 0; i < sizeof (cfb8_vectors) / sizeof (cfb8_vectors[0]);
	    i++) {
		args.in = cfb8_vectors[i].input;
		args.key = cfb8_vectors[i].key;
		args.param = cfb8_vectors[i].iv;
		args.inlen = cfb8_vectors[i].input_len;
		args.keylen = cfb8_vectors[i].key_len;
		errs += run_test(&args, cfb8_vectors[i].output,
		    cfb8_vectors[i].input_len,
		    cfb8_vectors[i].encrypt ? ENCR_FG : DECR_FG);
		(void) fprintf(stderr, "----------\n");
	}

	if (errs != 0)
		(void) fprintf(stderr, "%d tests failed\n", errs);

	return (errs);
}
