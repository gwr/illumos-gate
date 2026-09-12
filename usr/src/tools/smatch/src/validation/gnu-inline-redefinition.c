extern int repeated_inline(int);

extern __inline__ __attribute__((__gnu_inline__)) int
repeated_inline(int value)
{
	return value;
}

extern __inline__ __attribute__((__gnu_inline__)) int
repeated_inline(int value)
{
	return value + 1;
}

/*
 * check-name: GNU extern inline same translation unit
 * check-command: sparse -fdiagnostic-prefix=sparse $file
 *
 * check-error-start
sparse: gnu-inline-redefinition.c:10:1: warning: multiple definitions for function 'repeated_inline'
sparse: gnu-inline-redefinition.c:4:1:  the previous one is here
 * check-error-end
 */
