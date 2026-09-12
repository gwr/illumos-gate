extern int emitted_function(void);

int
emitted_function(void)
{
	return 0;
}

/*
 * check-name: emitted external definition across translation units
 * check-command: sparse -fdiagnostic-prefix=sparse $file $file
 *
 * check-error-start
sparse: external-definition-multi.c:4:1: warning: multiple definitions for function 'emitted_function'
sparse: external-definition-multi.c:4:1:  the previous one is here
 * check-error-end
 */
