/* TOOL-M06 libfoo internals — shared by foo.c and foo_util.c, never installed. */
#ifndef FOO_INTERNAL_H
#define FOO_INTERNAL_H
#include <stdint.h>
#include "foo.h"

#define FOO_MAX_WINDOW 16

struct foo {                      /* opaque to apps: they only hold foo_t * */
	uint32_t window, n, head;
	int64_t sum;
	int32_t buf[FOO_MAX_WINDOW];
	struct foo_stats st;
};

/* Cross-file helpers: must be global (two .c files use them) but are NOT API. */
int32_t round_div(int64_t num, uint32_t den);
void ring_put(struct foo *f, int32_t sample);

#endif
