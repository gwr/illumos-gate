static void tagged(int *ptr)
{
	__context__(*ptr, 1, 4294967296UL);
}

/*
 * check-name: context-tag-balance
 */
