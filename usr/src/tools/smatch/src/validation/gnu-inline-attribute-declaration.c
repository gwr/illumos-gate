extern __inline__ int attributed_declaration(int)
    __attribute__((__gnu_inline__));

extern __inline__ int
attributed_declaration(int value)
{
	return value;
}

/*
 * check-name: GNU inline attribute on declaration
 * check-command: sparse -std=gnu99 $file $file
 * check-description: A gnu_inline attribute on a preceding declaration
 * classifies the matching extern-inline definition in that translation unit.
 */
