extern int inline_and_external(void);

extern __inline__ __attribute__((__gnu_inline__)) int
inline_and_external(void)
{
	return 1;
}

int
inline_and_external(void)
{
	return 2;
}

/*
 * check-name: GNU inline with same-TU external definition
 * check-command: sparse $file
 * check-description: A GNU extern-inline implementation and the one emitted
 * external definition may appear in the same translation unit.
 */
