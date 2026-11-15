/* TOOL-B11 demo — what does ASan cost?  (links with solution.c)
 *   cc -std=c11 -O1 -g [-fsanitize=address] demo_cost.c solution.c -o dc && ./dc
 * Three workloads, each timed with CLOCK_MONOTONIC:
 *   alloc : 200k x (pkt_new -> txq_push) then txq_drain   (malloc/free heavy)
 *   crc   : CRC-16 over a 64 KiB buffer, 200 times         (ALU heavy, 1 load / 8 steps)
 *   hist  : byte histogram of the 64 KiB buffer, 500 times (load + store every byte)
 */
#include <stdio.h>
#include <time.h>

#include "buf.h"

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static uint8_t g_big[64 * 1024];

int main(void)
{
    uint8_t pl[PKT_MAX_PAYLOAD];
    for (unsigned i = 0; i < sizeof pl; i++)
        pl[i] = (uint8_t)(i * 7u);
    for (unsigned i = 0; i < sizeof g_big; i++)
        g_big[i] = (uint8_t)(i ^ (i >> 8));

    double t0 = now_ms();
    size_t bytes = 0;
    for (int round = 0; round < 10; round++) {
        txq_t q = { NULL, NULL, 0 };
        for (unsigned i = 0; i < 20000; i++)
            txq_push(&q, pkt_new((uint8_t)(i & 3u), pl, i % (PKT_MAX_PAYLOAD + 1u)));
        bytes += txq_drain(&q);
    }
    double t1 = now_ms();
    unsigned acc = 0;
    for (int r = 0; r < 200; r++)
        acc += crc16_ccitt(g_big, sizeof g_big - (size_t)r);
    double t2 = now_ms();
    static uint32_t hist[256];
    for (int r = 0; r < 500; r++)
        for (size_t i = 0; i < sizeof g_big; i++)
            hist[g_big[i]]++;
    double t3 = now_ms();
    printf("alloc_ms=%.1f crc_ms=%.1f hist_ms=%.1f (bytes=%zu acc=%u h0=%u)\n", t1 - t0,
           t2 - t1, t3 - t2, bytes, acc, (unsigned)hist[0]);
    return 0;
}
