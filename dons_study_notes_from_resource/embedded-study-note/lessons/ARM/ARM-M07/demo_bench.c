/* ARM-M07 demo_bench: NEON asm vs scalar C vs libc / auto-vectorized C, at 64 B, 4 KB, 1 MB
 *   cc -std=c11 -O2 demo_bench.c solution.S -o /tmp/ARM-M07_bench && /tmp/ARM-M07_bench
 * Each sample repeats the call until ~16 MB is processed; median of 31 samples -> GB/s.
 * memchr: no match (worst case, scans all n). Timer: clock_gettime_nsec_np(CLOCK_UPTIME_RAW).
 */
#define _DARWIN_C_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern const void *neon_memchr(const void *p, int c, size_t n);
extern uint64_t    checksum_u8(const uint8_t *p, size_t n);
extern void        minmax_u16(const uint16_t *s, size_t n, uint16_t *mn, uint16_t *mx);

/* scalar references: vectorizer explicitly disabled (checked with objdump: no q registers) */
static const void *scalar_memchr(const void *p, int c, size_t n) {
    const uint8_t *b = p;
#pragma clang loop vectorize(disable) interleave(disable)
    for (size_t i = 0; i < n; i++) if (b[i] == (uint8_t)c) return b + i;
    return NULL;
}
static uint64_t scalar_sum(const uint8_t *p, size_t n) {
    uint64_t s = 0;
#pragma clang loop vectorize(disable) interleave(disable)
    for (size_t i = 0; i < n; i++) s += p[i];
    return s;
}
static uint64_t autovec_sum(const uint8_t *p, size_t n) {          /* plain loop: clang -O2 vectorizes */
    uint64_t s = 0;
    for (size_t i = 0; i < n; i++) s += p[i];
    return s;
}
static void scalar_minmax(const uint16_t *s, size_t n, uint16_t *mn, uint16_t *mx) {
    uint16_t lo = 0xFFFF, hi = 0;
#pragma clang loop vectorize(disable) interleave(disable)
    for (size_t i = 0; i < n; i++) { if (s[i] < lo) lo = s[i]; if (s[i] > hi) hi = s[i]; }
    *mn = lo; *mx = hi;
}

typedef const void *(*chr_fn)(const void *, int, size_t);
typedef uint64_t (*sum_fn)(const uint8_t *, size_t);
typedef void (*mm_fn)(const uint16_t *, size_t, uint16_t *, uint16_t *);
static void *(*volatile libc_memchr)(const void *, int, size_t) = memchr;
static const void *libc_chr(const void *p, int c, size_t n) { return libc_memchr(p, c, n); }
static chr_fn volatile v_libc = libc_chr, v_neon = neon_memchr, v_scal = scalar_memchr;
static sum_fn volatile v_sneon = checksum_u8, v_sscal = scalar_sum, v_sauto = autovec_sum;
static mm_fn  volatile v_mneon = minmax_u16, v_mscal = scalar_minmax;
static volatile uint64_t sink;

enum { SAMPLES = 31 };
static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return (x > y) - (x < y);
}
static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
static double median_gbs(uint64_t *t, size_t bytes_per_sample) {
    qsort(t, SAMPLES, sizeof t[0], cmp_u64);
    return (double)bytes_per_sample / (double)t[SAMPLES / 2];      /* bytes/ns = GB/s */
}
#define REPS(n) ((16u << 20) / (n) < 4 ? 4 : (16u << 20) / (n))

static double b_chr(chr_fn f, const uint8_t *p, size_t n) {
    size_t reps = REPS(n); uint64_t t[SAMPLES];
    for (int k = 0; k < SAMPLES; k++) {
        uint64_t t0 = now_ns();
        for (size_t r = 0; r < reps; r++) sink += (uintptr_t)f(p, 0x7E, n);
        t[k] = now_ns() - t0;
    }
    return median_gbs(t, reps * n);
}
static double b_sum(sum_fn f, const uint8_t *p, size_t n) {
    size_t reps = REPS(n); uint64_t t[SAMPLES];
    for (int k = 0; k < SAMPLES; k++) {
        uint64_t t0 = now_ns();
        for (size_t r = 0; r < reps; r++) sink += f(p, n);
        t[k] = now_ns() - t0;
    }
    return median_gbs(t, reps * n);
}
static double b_mm(mm_fn f, const uint16_t *s, size_t nbytes) {
    size_t reps = REPS(nbytes); uint64_t t[SAMPLES]; uint16_t mn, mx;
    for (int k = 0; k < SAMPLES; k++) {
        uint64_t t0 = now_ns();
        for (size_t r = 0; r < reps; r++) { f(s, nbytes / 2, &mn, &mx); sink += mn + mx; }
        t[k] = now_ns() - t0;
    }
    return median_gbs(t, reps * nbytes);
}

int main(void) {
    const size_t sizes[] = {64, 4096, 1u << 20};
    uint8_t *buf = aligned_alloc(64, 1u << 20);
    if (!buf) return 2;
    for (size_t i = 0; i < (1u << 20); i++) buf[i] = (uint8_t)((i * 131u + 7u) % 0x7Du);   /* no 0x7E */
    uint16_t *adc = (uint16_t *)buf;                     /* reinterpret as samples for minmax */
    printf("GB/s (median of %d)      size:      64 B     4 KB     1 MB\n", SAMPLES);
    const char *names[] = {"memchr  scalar C (no vec)", "memchr  neon asm         ", "memchr  libc             ",
                           "sum8    scalar C (no vec)", "sum8    clang auto-vec C ", "sum8    neon asm         ",
                           "minmax  scalar C (no vec)", "minmax  neon asm         "};
    for (int row = 0; row < 8; row++) {
        printf("%s           ", names[row]);
        for (int si = 0; si < 3; si++) {
            size_t n = sizes[si]; double g = 0;
            switch (row) {
            case 0: g = b_chr(v_scal, buf, n); break;
            case 1: g = b_chr(v_neon, buf, n); break;
            case 2: g = b_chr(v_libc, buf, n); break;
            case 3: g = b_sum(v_sscal, buf, n); break;
            case 4: g = b_sum(v_sauto, buf, n); break;
            case 5: g = b_sum(v_sneon, buf, n); break;
            case 6: g = b_mm(v_mscal, adc, n); break;
            case 7: g = b_mm(v_mneon, adc, n); break;
            }
            printf("%8.2f ", g);
        }
        printf("\n");
    }
    free(buf);
    return 0;
}
