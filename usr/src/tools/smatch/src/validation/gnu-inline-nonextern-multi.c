__inline__ __attribute__((__gnu_inline__)) int
emitted_inline(void)
{
	return 1;
}

/*
 * check-name: non-extern GNU inline across translation units
 * check-command: sparse -fdiagnostic-prefix=sparse $file $file
 * check-description: The gnu_inline attribute does not make a plain inline
 * definition implementation-only; it still emits an external definition.
 *
 * check-error-start
sparse: gnu-inline-nonextern-multi.c:2:1: warning: multiple definitions for function 'emitted_inline'
sparse: gnu-inline-nonextern-multi.c:2:1:  the previous one is here
 * check-error-end
 */
