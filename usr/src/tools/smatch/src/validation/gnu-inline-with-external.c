extern int inline_with_external(void);

extern __inline__ __attribute__((__gnu_inline__)) int
inline_with_external(void)
{
	return 1;
}

/*
 * check-name: GNU inline with one external definition
 * check-command: sparse -std=gnu99 $file gnu-inline-with-external.h
 * check-description: One emitted external definition may accompany any
 * number of translation-unit GNU extern-inline implementations.
 */
