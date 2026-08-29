static void preserve_stores(int *ptr)
{
	*ptr = 1;
	__context__(*ptr, 0, 1);
	*ptr = 2;
}

static int preserve_loads(int *ptr)
{
	int first = *ptr;

	__context__(*ptr, 0, 1);
	return first + *ptr;
}

/*
 * check-name: context-tag-barrier
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-ignore
 * check-output-pattern(2): context .*tag 1
 * check-output-pattern(2): load\\.
 * check-output-pattern(2): store\\.
 */
