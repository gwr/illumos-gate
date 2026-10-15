/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 */

/*
 * Exercise repeated shared-header types whose callback signatures refer to
 * opaque tagged structures through typedefs.
 */

typedef struct callback_handle *callback_handle_t;
typedef struct callback_cookie *callback_cookie_t;
typedef unsigned int callback_uint_t;

struct callback_consistency {
	callback_uint_t version;
#ifdef TYPE_CALLBACK_DIFFERENT
	int (*notify)(callback_handle_t, callback_cookie_t, int);
#else
	int (*notify)(callback_handle_t, callback_cookie_t, void *);
#endif
	void (*finalize)(callback_handle_t, callback_cookie_t, int, void *);
};
