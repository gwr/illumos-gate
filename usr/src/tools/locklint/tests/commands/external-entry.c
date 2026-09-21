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
 * Verify that external-entry declarations suppress only the external-linkage
 * reason for automatic root discovery.
 */

void external_entry_called(void);
void external_entry_called_too(void);
void external_entry_escaped(void);
void external_entry_caller(void);
extern void (*external_entry_pointer)(void);

void
external_entry_caller(void)
{
	external_entry_called();
	external_entry_called_too();
	external_entry_escaped();
}

void (*external_entry_pointer)(void) = external_entry_escaped;
