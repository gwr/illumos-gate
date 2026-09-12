extern int f(int);

extern __inline__ int
f(int x)
{
        return x;
}

extern int g(int);

extern __inline__ __attribute__((__gnu_inline__)) int
g(int x)
{
        return x;
}


/*
 * check-name: GNU extern inline across translation units
 * check-command: sparse $file $file
 * check-description: A GNU extern-inline body is a translation-unit inline
 * implementation, not an emitted external definition.  Repeating the body
 * in another translation unit is valid and must remain type-compatible with
 * its ordinary external prototype.
 */
