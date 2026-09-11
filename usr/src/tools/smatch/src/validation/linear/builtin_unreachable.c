void function_that_never_returns(void);

int foo(int c)
{
	if (c)
		return 1;
	function_that_never_returns();
	__builtin_unreachable();
}

/*
 * check-name: __builtin_unreachable()
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-start
foo:
.L0:
	<entry-point>
	cbr         %arg1, .L3, .L2

.L3:
	ret.32      $1

.L2:
	call        function_that_never_returns
	unreach


 * check-output-end
 */
