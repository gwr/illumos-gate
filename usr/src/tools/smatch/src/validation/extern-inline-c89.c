extern int c89_inline(void);

extern __inline__ int
c89_inline(void)
{
	return 1;
}

/*
 * check-name: C89 extern inline across translation units
 * check-command: sparse -std=c89 $file $file
 * check-description: Sparse applies GNU89 extern-inline semantics in the
 * pre-C99 language modes that support inline as a GNU extension.
 */
