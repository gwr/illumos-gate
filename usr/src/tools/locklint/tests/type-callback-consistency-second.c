/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 */

/*
 * Supply the repeated translation-unit instance of the callback type.
 */

struct callback_handle;
extern int callback_handle_use(struct callback_handle *);

#include "type-callback-consistency.h"

static struct callback_consistency second_callback_consistency;
