/* boot.c — bootloader "UART RX" path reusing ringbuf (public API only). */
#include <stdio.h>
#include "ringbuf.h"

int main(void)
{
    rb_t rx;
    uint8_t b = 0;
    unsigned n = 0;

    rb_init(&rx);
    for (unsigned i = 0; i < 20; i++)          /* 4 bytes overflow: dropped */
        if (rb_put(&rx, (uint8_t)('A' + i)) == 0)
            n++;
    while (rb_get(&rx, &b) == 0)
        ;
    printf("[boot] accepted=%u last=%c sizeof(rb_t)=%u lib=%u\n", n, b,
           (unsigned)sizeof(rb_t), (unsigned)rb_struct_size());
    return sizeof(rb_t) == rb_struct_size() ? 0 : 1;
}
