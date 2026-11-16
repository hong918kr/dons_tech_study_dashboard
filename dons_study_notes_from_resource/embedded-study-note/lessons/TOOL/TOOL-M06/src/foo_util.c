/* TOOL-M06 libfoo internal helpers (global, used by foo.c). */
#include "foo_internal.h"

int32_t round_div(int64_t num, uint32_t den) /* round half away from zero */
{
	int64_t d = (int64_t)den;
	return (int32_t)(num >= 0 ? (num + d / 2) / d : (num - d / 2) / d);
}

void ring_put(struct foo *f, int32_t sample)
{
	if (f->n == f->window)
		f->sum -= f->buf[f->head];  /* overwrite the oldest sample */
	else
		f->n++;
	f->buf[f->head] = sample;
	f->sum += sample;
	f->head = (f->head + 1) % f->window;
}
