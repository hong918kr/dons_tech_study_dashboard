/* ARM-B10 demo_bench: byte vs 16B asm memcpy vs libc memcpy, byte strlen vs libc strlen
 *   cc -std=c11 -O2 demo_bench.c solution.S -o /tmp/ARM-B10_bench && /tmp/ARM-B10_bench
 * Each sample repeats the call until ~8 MB (or 8 MB of string) is processed; median of 41 samples.
 * Timer: clock_gettime_nsec_np(CLOCK_UPTIME_RAW) (mach ticks, ~42 ns resolution -> long samples).
 */
#define _DARWIN_C_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern void  *memcpy_byte(void *dst, const void *src, size_t n);
extern size_t strlen_byte(const char *s);
extern void  *memcpy_16(void *dst, const void *src, size_t n);

typedef void *(*copy_fn)(void *, const void *, size_t);
typedef size_t (*len_fn)(const char *);
static copy_fn volatile libc_memcpy = memcpy;   /* volatile: the call can't be inlined/elided */
static len_fn  volatile libc_strlen = strlen;
static volatile size_t sink;

enum { SAMPLES = 41 };
static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return (x > y) - (x < y);
}
static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }

static double bench_copy(copy_fn f, uint8_t *d, const uint8_t *s, size_t n) {
    size_t reps = (8u << 20) / n; if (reps < 4) reps = 4;
    uint64_t t[SAMPLES];
    f(d, s, n);                                          /* warm caches / TLB */
    for (int k = 0; k < SAMPLES; k++) {
        uint64_t t0 = now_ns();
        for (size_t r = 0; r < reps; r++) f(d, s, n);
        t[k] = now_ns() - t0;
        sink += d[n - 1];
    }
    qsort(t, SAMPLES, sizeof t[0], cmp_u64);
    return (double)t[SAMPLES / 2] / (double)reps;        /* median ns per call */
}

static double bench_len(len_fn f, const char *s, size_t n) {
    size_t reps = (8u << 20) / (n + 1); if (reps < 4) reps = 4;
    uint64_t t[SAMPLES];
    sink += f(s);
    for (int k = 0; k < SAMPLES; k++) {
        uint64_t t0 = now_ns();
        for (size_t r = 0; r < reps; r++) sink += f(s);
        t[k] = now_ns() - t0;
    }
    qsort(t, SAMPLES, sizeof t[0], cmp_u64);
    return (double)t[SAMPLES / 2] / (double)reps;
}

int main(void) {
    const size_t sizes[] = {16, 256, 4096, 1u << 20};
    const size_t MAXN = (1u << 20) + 64;
    uint8_t *src = aligned_alloc(64, MAXN), *dst = aligned_alloc(64, MAXN);
    char *str = aligned_alloc(64, MAXN);
    if (!src || !dst || !str) return 1;
    for (size_t i = 0; i < MAXN; i++) src[i] = (uint8_t)(i * 31 + 7);
    memset(dst, 0, MAXN);

    printf("memcpy, ns/call (GB/s) -- src/dst 64-byte aligned\n");
    printf("%9s %20s %20s %20s\n", "size", "memcpy_byte", "memcpy_16", "libc memcpy");
    for (size_t i = 0; i < 4; i++) {
        size_t n = sizes[i];
        double a = bench_copy(memcpy_byte, dst, src, n);
        double b = bench_copy(memcpy_16, dst, src, n);
        double c = bench_copy(libc_memcpy, dst, src, n);
        printf("%9zu %10.1f (%6.2f) %10.1f (%6.2f) %10.1f (%6.2f)\n", n,
               a, n / a, b, n / b, c, n / c);
    }
    printf("\nmemcpy 4096 B, src+1 / dst+3 (misaligned)\n");
    {
        double b = bench_copy(memcpy_16, dst + 3, src + 1, 4096);
        double c = bench_copy(libc_memcpy, dst + 3, src + 1, 4096);
        printf("%9s %20s %10.1f (%6.2f) %10.1f (%6.2f)\n", "4096", "", b, 4096 / b, c, 4096 / c);
    }

    printf("\nstrlen, ns/call (GB/s)\n");
    printf("%9s %20s %20s\n", "length", "strlen_byte", "libc strlen");
    for (size_t i = 0; i < 4; i++) {
        size_t n = sizes[i];
        memset(str, 'k', n); str[n] = '\0';
        double a = bench_len(strlen_byte, str, n);
        double c = bench_len(libc_strlen, str, n);
        printf("%9zu %10.1f (%6.2f) %10.1f (%6.2f)\n", n, a, n / a, c, n / c);
    }
    printf("(sink %zu)\n", (size_t)(sink & 1));
    free(src); free(dst); free(str);
    return 0;
}
