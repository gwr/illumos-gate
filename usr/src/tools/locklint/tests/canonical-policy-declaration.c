/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 */

/*
 * Declare policy for one exact instance of the shared type.
 */

#include "canonical-policy.h"

_NOTE(MUTEX_PROTECTS_DATA(canonical_policy_state::lock,
    canonical_policy_state::value))
_NOTE(DATA_READABLE_WITHOUT_LOCK(canonical_policy_state::value))
