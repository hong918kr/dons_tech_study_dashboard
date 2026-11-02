/* Bare-metal benchmark sketch (bandwidth, latency percentiles, pointer-chase load latency).
 * On target read_cycles() = PMU cycle counter / 64-bit system timer; on host it uses clock_gettime.
 * cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined bench_sketch.c && ./a.out
 */
#define _POSIX_C_SOURCE 199309L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t read_cycles(void)          /* host stand-in: 1 "cycle" = 1 ns */
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
#define CPU_HZ 1000000000ull

/* Throughput: bytes moved per second over many iterations, timer overhead removed. */
uint64_t bench_bw(void *dst, const void *src, size_t bytes, uint32_t iters)
{
    uint64_t o = read_cycles();
    uint64_t overhead = read_cycles() - o;
    memcpy(dst, src, bytes);                       /* warm-up: cache/TLB state */
    uint64_t t0 = read_cycles();
    for (uint32_t i = 0; i < iters; i++)
        memcpy(dst, src, bytes);                   /* on target: DMA start + wait done */
    uint64_t cyc = read_cycles() - t0 - overhead;
    if (cyc == 0)
        cyc = 1;
    return (uint64_t)bytes * iters * CPU_HZ / cyc; /* bytes/s; 1 MiB*100*1e9 fits in u64 */
}

/* Latency: keep every sample, report percentiles, not just the mean. */
static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

uint64_t percentile(uint64_t *s, uint32_t n, uint32_t p)   /* p in 0..100, sorts s */
{
    qsort(s, n, sizeof s[0], cmp_u64);
    uint32_t idx = (uint32_t)(((uint64_t)p * (n - 1) + 50) / 100);
    return s[idx];
}

/* Memory load latency: each load depends on the previous one (pointer chase),
 * so the CPU cannot overlap them -> cycles/hop = true access latency for that footprint. */
/* Random single-cycle order (Sattolo): a constant stride would be caught by the HW prefetcher
 * and report prefetch-hidden latency instead of real DRAM latency. */
void chase_build(uintptr_t *buf, size_t n, size_t *idx)
{
    uint32_t r = 2463534242u;                      /* xorshift32 seed */
    for (size_t i = 0; i < n; i++)
        idx[i] = i;
    for (size_t i = n - 1; i > 0; i--) {
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        size_t j = r % i;                          /* j < i -> one big cycle */
        size_t t = idx[i]; idx[i] = idx[j]; idx[j] = t;
    }
    for (size_t i = 0; i < n; i++)
        buf[idx[i]] = (uintptr_t)&buf[idx[(i + 1) % n]];
}

uint64_t chase_run(const uintptr_t *buf, uint32_t hops, uintptr_t *sink)
{
    uintptr_t p = (uintptr_t)buf;
    uint64_t t0 = read_cycles();
    for (uint32_t i = 0; i < hops; i++)
        p = *(const uintptr_t *)p;
    uint64_t cyc = read_cycles() - t0;
    *sink = p;                                     /* keep the chain alive */
    return cyc / hops;
}

int main(void)
{
    enum { BYTES = 1 << 20, N = 1000 };
    uint8_t *a = malloc(BYTES), *b = malloc(BYTES);
    memset(a, 0x5A, BYTES);
    printf("memcpy bw   : %.2f GB/s\n", (double)bench_bw(b, a, BYTES, 100) / 1e9);

    static uint64_t lat[N];
    for (uint32_t i = 0; i < N; i++) {
        uint64_t t0 = read_cycles();
        for (int k = 0; k < 16; k++)               /* batch: host timer resolution is ~1 us */
            memcpy(b, a, 4096);                    /* the "operation" under test */
        lat[i] = (read_cycles() - t0) / 16;
    }
    uint64_t p50 = percentile(lat, N, 50), p99 = percentile(lat, N, 99), mx = percentile(lat, N, 100);
    printf("4K copy lat : p50 %llu  p99 %llu  max %llu cycles\n",
           (unsigned long long)p50, (unsigned long long)p99, (unsigned long long)mx);
    int ok = p50 <= p99 && p99 <= mx;

    size_t n = (64u << 20) / sizeof(uintptr_t);    /* 64 MiB footprint > caches */
    uintptr_t *chain = malloc(n * sizeof *chain), sink;
    size_t *idx = malloc(n * sizeof *idx);
    chase_build(chain, n, idx);
    free(idx);
    printf("DDR-ish load: %llu ns/hop\n", (unsigned long long)chase_run(chain, 2000000, &sink));
    ok = ok && sink != 0;

    free(a); free(b); free(chain);
    puts(ok ? "ALL PASS" : "FAIL");
    return !ok;
}
