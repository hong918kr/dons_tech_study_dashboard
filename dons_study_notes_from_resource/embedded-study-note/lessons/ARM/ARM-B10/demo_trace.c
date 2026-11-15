/* ARM-B10 demo_trace: one memcpy_16 call to single-step in lldb (dst misaligned by 13, n = 40)
 *   cc -std=c11 -g -O0 demo_trace.c solution.S -o /tmp/ARM-B10_trace && lldb -b -s demo_trace.lldb /tmp/ARM-B10_trace
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern void *memcpy_16(void *dst, const void *src, size_t n);

static _Alignas(64) uint8_t src[64], dst[64];

int main(void) {
    for (int i = 0; i < 64; i++) src[i] = (uint8_t)(0x40 + i);
    memcpy_16(dst + 13, src + 1, 40);
    printf("ok=%d\n", memcmp(dst + 13, src + 1, 40) == 0);
    return 0;
}
