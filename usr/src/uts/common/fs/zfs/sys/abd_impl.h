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
 * Copyright (c) 2014 by Chunwei Chen. All rights reserved.
 * Copyright (c) 2016 by Delphix. All rights reserved.
 */

#ifndef _ABD_IMPL_H
#define	_ABD_IMPL_H

#include <sys/abd.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum abd_flags {
	ABD_FLAG_LINEAR	= 1 << 0,	/* is buffer linear (or scattered)? */
	ABD_FLAG_OWNER	= 1 << 1,	/* does it own its data buffers? */
	ABD_FLAG_META	= 1 << 2,	/* does this represent FS metadata? */
	ABD_FLAG_GANG	= 1 << 6,	/* multiple ABDs chained together */
	ABD_FLAG_GANG_FREE = 1 << 7	/* gang ABD owns this ABD */
} abd_flags_t;

struct abd {
	abd_flags_t	abd_flags;
	uint_t		abd_size;	/* excludes scattered abd_offset */
	list_node_t	abd_gang_link;
	struct abd	*abd_parent;
	zfs_refcount_t	abd_children;
	kmutex_t	abd_mtx;
	union {
		struct abd_scatter {
			uint_t	abd_offset;
			uint_t	abd_chunk_size;
			void	*abd_chunks[];
		} abd_scatter;
		struct abd_linear {
			void	*abd_buf;
		} abd_linear;
		struct abd_gang {
			list_t	abd_gang_chain;
		} abd_gang;
	} abd_u;
};

#define	ABD_GANG(abd)		((abd)->abd_u.abd_gang)

#ifdef __cplusplus
}
#endif

#endif	/* _ABD_IMPL_H */
