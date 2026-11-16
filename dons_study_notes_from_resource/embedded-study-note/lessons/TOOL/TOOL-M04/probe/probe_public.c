/* probe_public.c — a new app-side file: public header + link ringbuf, nothing else. */
#include "ringbuf.h"

int main(void)
{
    rb_t rb;
    rb_init(&rb);
    return rb_put(&rb, 1) == 0 && rb_count(&rb) == 1u ? 0 : 1;
}
