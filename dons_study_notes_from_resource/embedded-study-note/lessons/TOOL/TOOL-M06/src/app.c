/* TOOL-M06 app: links libfoo dynamically (built by check.sh). */
#include <stdint.h>
#include <stdio.h>
#include "foo.h"

int32_t round_div(int64_t num, uint32_t den); /* app_util.c: the app's own helper */

int main(void)
{
	static const int32_t samples[] = { 10, 13, 17, 22, 31, 40, -5 };

	if (foo_init() != 0) {
		printf("app: libfoo ABI mismatch -> refusing to run\n");
		return 3;
	}
	printf("libfoo %s (app abi %d)\n", foo_version(), FOO_ABI_VERSION);

	foo_t *f = foo_open(4);
	if (!f)
		return 2;
	printf("avg:");
	for (size_t i = 0; i < sizeof samples / sizeof samples[0]; i++)
		printf(" %d", (int)foo_push(f, samples[i]));
	printf("\n");

	struct {
		struct foo_stats st;
		volatile uint32_t guard; /* the app's next field in memory */
	} box = { .guard = 0xC0FFEEu };
	foo_get_stats(f, &box.st);
	printf("stats: count=%u min=%d max=%d", (unsigned)box.st.count, (int)box.st.min, (int)box.st.max);
#ifdef FOO_ABI_V2
	printf(" avg=%d", (int)box.st.avg);
#endif
	printf(" guard=%s\n", box.guard == 0xC0FFEEu ? "ok" : "CLOBBERED");

	printf("app round_div(7,2)=%d\n", (int)round_div(7, 2));
	foo_close(f);
	return 0;
}
