static void tagged(int *ptr)
{
	__context__(*ptr, 0, 7);
	__test_pre_buffer_context(*ptr, 1, 4294967296UL);
}

/*
 * check-name: context-tag
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-start
tagged:
.L0:
	<entry-point>
	context     0, tag 7
	context     1, tag 4294967296
	ret


 * check-output-end
 */
