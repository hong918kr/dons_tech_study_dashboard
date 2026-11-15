// app.cpp — C++ 애플리케이션이 C 링 버퍼를 쓴다 (TOOL-B05 s3)
#include <cstdio>
#include "ring.h"

static unsigned g_dropped;

void ring_overflow_hook(ring_t *r, uint8_t b)   // ring.c 가 부르는 콜백
{
    (void)r;
    (void)b;
    g_dropped++;
}

int main()
{
    uint8_t storage[4];
    ring_t rx;
    ring_init(&rx, storage, sizeof storage);
    for (uint8_t i = 1; i <= 6; i++)
        ring_push(&rx, i);
    unsigned sum = 0;
    uint8_t v;
    while (ring_pop(&rx, &v))
        sum += v;
    std::printf("sum=%u dropped=%u\n", sum, g_dropped);
    return 0;
}
