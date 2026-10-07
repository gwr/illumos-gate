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
 * Manage selected output destinations shared by development dumps and
 * user-facing reports.  Registration order determines stdout publication
 * order, while explicit pathnames are checked globally for conflicts.
 */

#ifndef OUTPUT_H
#define	OUTPUT_H

#include <stdbool.h>
#include <stdio.h>

struct locklint_output {
	const char *name;
	const char *path;
	FILE *stream;
	bool interleaved;
	bool selected;
	bool header_written;
	bool owned;
	bool spooled;
	bool registered;
	struct locklint_output *next;
};

#define	LOCKLINT_OUTPUT_INITIALIZER(output_name, output_interleaved)	\
	{								\
		.name = (output_name),					\
		.interleaved = (output_interleaved)			\
	}

void locklint_output_register(struct locklint_output *);
bool locklint_output_option(const char *, struct locklint_output *);
void locklint_output_enable(struct locklint_output *);
bool locklint_output_is_enabled(const struct locklint_output *);
void locklint_outputs_prepare(void);
FILE *locklint_output_stream(struct locklint_output *);
FILE *locklint_output_section_stream(struct locklint_output *);
void locklint_outputs_finish(void);

#endif /* OUTPUT_H */
