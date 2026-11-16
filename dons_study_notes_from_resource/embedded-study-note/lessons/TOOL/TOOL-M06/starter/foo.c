/* TOOL-M06 libfoo: moving-average filter for a sensor stream (public API). */
#include <stdio.h>
#include <stdlib.h>
#include "foo_internal.h"

#ifdef FOO_ABI_V2
#define FOO_VERSION_STR "2.0.0"
#else
#define FOO_VERSION_STR "1.2.0"
#endif

FOO_API int foo_abi_check(int app_abi, size_t app_stats_size)
{
	/* TODO(C): refuse an app compiled against another ABI (version or struct size):
	 * print one line to stderr and return -1; return 0 when they match. */
	(void)app_abi;
	(void)app_stats_size;
	return 0;
}

FOO_API foo_t *foo_open(uint32_t window)
{
	if (window == 0 || window > FOO_MAX_WINDOW)
		return NULL;
	foo_t *f = calloc(1, sizeof *f);
	if (f) {
		f->window = window;
		f->st.min = INT32_MAX;
		f->st.max = INT32_MIN;
	}
	return f;
}

FOO_API int32_t foo_push(foo_t *f, int32_t sample)
{
	ring_put(f, sample);
	f->st.count++;
	if (sample < f->st.min)
		f->st.min = sample;
	if (sample > f->st.max)
		f->st.max = sample;
	int32_t avg = round_div(f->sum, f->n);
#ifdef FOO_ABI_V2
	f->st.avg = avg;
#endif
	return avg;
}

FOO_API void foo_get_stats(const foo_t *f, struct foo_stats *out)
{
	*out = f->st; /* writes sizeof(struct foo_stats) as THIS library sees it */
}

FOO_API void foo_close(foo_t *f)
{
	free(f);
}

FOO_API const char *foo_version(void)
{
	return FOO_VERSION_STR;
}
