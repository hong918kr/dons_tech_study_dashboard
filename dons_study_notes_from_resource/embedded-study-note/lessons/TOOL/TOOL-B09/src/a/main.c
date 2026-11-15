/* Part A app: needs foo_entry() from libfoo.a */
#include <stdio.h>

int foo_entry(int x);

int main(void)
{
	printf("A: foo_entry(5) = %d\n", foo_entry(5));
	return 0;
}
