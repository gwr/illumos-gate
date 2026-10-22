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
 * Copyright 2026 RackTop Systems, Inc.
 */

/*
 * Selects CRYPTO_DATA_UIO output for the scatter-gather backend
 * (cryptotest_sg.c).
 */

#include "cryptotest.h"

const crypto_data_format_t cryptotest_sg_format = CRYPTO_DATA_UIO;
