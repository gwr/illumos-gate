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
 * Verify that locklint accepts the signed one-bit fields used by illumos
 * interfaces even though Sparse diagnoses their value range by default.
 */

struct signed_one_bit_state {
	int enabled : 1;
};

static struct signed_one_bit_state signed_one_bit_state;

int
signed_one_bit_value(void)
{
	return (signed_one_bit_state.enabled);
}
