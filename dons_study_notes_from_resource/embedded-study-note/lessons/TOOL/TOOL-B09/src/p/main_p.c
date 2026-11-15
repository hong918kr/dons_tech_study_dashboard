/* Practice app: owns the registry, runs whatever registered itself.
 * It never names a test function -> nothing references libtests.a members. */
#include <stdio.h>
#include "registry.h"

#define MAX_TESTS 16
static const char *names[MAX_TESTS];
static test_fn fns[MAX_TESTS];
static int n_tests;

void test_register(const char *name, test_fn fn)
{
	if (n_tests < MAX_TESTS) {
		names[n_tests] = name;
		fns[n_tests] = fn;
		n_tests++;
	}
}

int main(void)
{
	int pass = 0;

	for (int i = 0; i < n_tests; i++)
		pass += fns[i]() == 0;
	printf("P: %d tests registered, %d passed\n", n_tests, pass);
	return n_tests > 0 && pass == n_tests ? 0 : 1;
}
