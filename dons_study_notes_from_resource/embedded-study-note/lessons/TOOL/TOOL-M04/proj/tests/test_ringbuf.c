/* test_ringbuf.c — WHITE-BOX unit test: it is allowed to include the private header. */
#include <stdio.h>
#include "rb_internal.h"

static int pass, total;
#define CHECK(e) do { total++; if (e) { pass++; printf("PASS %s\n", #e); } \
                      else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)

int main(void)
{
    rb_t rb;
    uint8_t b = 0;
    int ok = 1;

    CHECK(rb_struct_size() == sizeof(rb_t));      /* lib and test agree on the layout (ABI) */
    CHECK(rb_idx(RB_CAP + 3u) == 3u);             /* private helper */
    rb_init(&rb);
    CHECK(rb_get(&rb, &b) == -1);                 /* empty */
    for (uint32_t i = 0; i < RB_CAP; i++)
        ok &= rb_put(&rb, (uint8_t)i) == 0;
    CHECK(ok && rb_count(&rb) == RB_CAP);
    CHECK(rb_put(&rb, 99) == -1);                 /* full */
    CHECK(rb_get(&rb, &b) == 0 && b == 0);        /* FIFO order */
    CHECK(rb_put(&rb, 200) == 0 && rb_count(&rb) == RB_CAP);   /* wrap-around */
    for (uint32_t i = 1; i < RB_CAP; i++)
        ok &= rb_get(&rb, &b) == 0 && b == (uint8_t)i;
    CHECK(ok && rb_get(&rb, &b) == 0 && b == 200);
    printf("test_ringbuf: %d/%d passed\n", pass, total);
    return pass == total ? 0 : 1;
}
