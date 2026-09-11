_Noreturn void c11_noreturn(void);
void gnu_noreturn(void) __attribute__((noreturn));
void ordinary(void);

void terminate_calls(int select)
{
	if (select)
		c11_noreturn();
	gnu_noreturn();
	ordinary();
}

/*
 * check-name: noreturn calls
 * check-command: test-linearize -Wno-decl $file
 *
 * check-output-start
terminate_calls:
.L0:
	<entry-point>
	cbr         %arg1, .L1, .L2

.L1:
	call        c11_noreturn
	unreach

.L2:
	call        gnu_noreturn
	unreach


 * check-output-end
 */
