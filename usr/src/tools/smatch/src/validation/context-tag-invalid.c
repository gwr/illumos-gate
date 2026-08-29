static void tagged(int *ptr)
{
	__context__(*ptr, 0, *ptr);
}

/*
 * check-name: context-tag-invalid
 *
 * check-error-start
context-tag-invalid.c:3:30: error: bad constant expression
 * check-error-end
 */
