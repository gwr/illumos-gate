/*
 * Verify that protection auditing inventories direct file-scope pointer
 * storage independently of the pointer's target.
 */

#define	_NOTE(arg)

extern void consume_pointer(void *);

static void *pointer;
static int scalar;

int
audit_global_pointer(void)
{
	int value;

	_NOTE(COMPETING_THREADS_NOW)
	consume_pointer(pointer);
	value = scalar;
	_NOTE(NO_COMPETING_THREADS_NOW)

	return (value);
}
