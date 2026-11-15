/* ARCH-M06 demo_bench.c — Apple Silicon(arm64) 에서 "0 쓰기" 네 가지 + 부분 라인 쓰기 측정
 *
 *   1) DCZID_EL0 읽기: DC ZVA 블록 크기 (EL0 에서 읽을 수 있는 레지스터)
 *   2) DC ZVA 정확성: 블록 하나만 0 이 되는지
 *   3) 128 MiB 버퍼 0 채우기: stp xzr / stnp xzr(비시간적 힌트) / dc zva / libc memset, 그리고 읽기 합
 *   4) 라인(128 B)마다 8 B 만 쓰기 — 쓴 바이트는 1/16 인데 시간은?
 *   각 7 회, 중앙값. 시간 측정은 이 파일에만 있다 (체크 테스트에는 없음).
 *
 * build: cc -std=c11 -O2 -Wall -Wextra demo_bench.c -o demo_bench && ./demo_bench      (arm64 전용)
 */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !defined(__aarch64__)
int main(void) { puts("arm64 only"); return 0; }
#else
#define MB   (1024u * 1024u)
#define SIZE (128u * MB)
#define REPS 7

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec + 1e-9 * (double)t.tv_nsec; }
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

static void zero_stp(uint8_t *p, size_t n) {
    for (uint8_t *e = p + n; p < e; p += 64)
        __asm__ volatile("stp xzr, xzr, [%0]\n\tstp xzr, xzr, [%0, #16]\n\t"
                         "stp xzr, xzr, [%0, #32]\n\tstp xzr, xzr, [%0, #48]" :: "r"(p) : "memory");
}
static void zero_stnp(uint8_t *p, size_t n) {
    for (uint8_t *e = p + n; p < e; p += 64)
        __asm__ volatile("stnp xzr, xzr, [%0]\n\tstnp xzr, xzr, [%0, #16]\n\t"
                         "stnp xzr, xzr, [%0, #32]\n\tstnp xzr, xzr, [%0, #48]" :: "r"(p) : "memory");
}
static size_t g_zva;
static void zero_zva(uint8_t *p, size_t n) {
    for (uint8_t *e = p + n; p < e; p += g_zva) __asm__ volatile("dc zva, %0" :: "r"(p) : "memory");
}
static void zero_memset(uint8_t *p, size_t n) { memset(p, 0, n); }
static void sparse8(uint8_t *p, size_t n) {          /* 128 B 마다 8 B 하나 */
    for (uint8_t *e = p + n; p < e; p += 128) __asm__ volatile("str xzr, [%0]" :: "r"(p) : "memory");
}
static volatile uint64_t g_sink;
static void read_sum(uint8_t *p, size_t n) {
    uint64_t s = 0; const uint64_t *q = (const uint64_t *)(void *)p;
    for (size_t i = 0; i < n / 8; i += 4) s += q[i] ^ q[i + 1] ^ q[i + 2] ^ q[i + 3];
    g_sink = s;
}

static double bench(void (*f)(uint8_t *, size_t), uint8_t *buf) {
    double t[REPS];
    for (int r = 0; r < REPS; r++) {
        memset(buf, 0xA5, SIZE);                       /* 시간 밖: 페이지 확보 + 0 아닌 내용 */
        double t0 = now(); f(buf, SIZE); t[r] = now() - t0;
        if (f != read_sum && f != sparse8)             /* 시간 밖: 정말 0 이 됐나 (4 KB 마다 표본) */
            for (size_t i = 0; i < SIZE; i += 4093) if (buf[i] != 0) { printf("not zero at %zu\n", i); exit(1); }
    }
    qsort(t, REPS, sizeof t[0], cmpd);
    return t[REPS / 2];
}

int main(void) {
    uint64_t dczid;
    __asm__ volatile("mrs %0, dczid_el0" : "=r"(dczid));
    g_zva = (size_t)4u << (dczid & 15u);
    printf("DCZID_EL0 = 0x%llx  -> BS=%llu, ZVA block = %zu B, DZP(금지)=%llu\n",
           (unsigned long long)dczid, (unsigned long long)(dczid & 15u), g_zva, (unsigned long long)(dczid >> 4 & 1u));
    if (dczid >> 4 & 1u) { puts("DC ZVA prohibited"); return 0; }

    uint8_t *buf = aligned_alloc(16384, SIZE);
    if (!buf) return 1;
    memset(buf, 0xA5, 4 * g_zva);
    zero_zva(buf + g_zva, g_zva);                      /* 블록 1 개만 */
    int ok = 1;
    for (size_t i = 0; i < 4 * g_zva; i++) ok &= buf[i] == ((i >= g_zva && i < 2 * g_zva) ? 0 : 0xA5);
    printf("dc zva on one block: %s\n", ok ? "exactly that block zeroed" : "MISMATCH");

    struct { const char *name; void (*f)(uint8_t *, size_t); double bytes; } k[] = {
        { "stp xzr (64 B/iter)  ", zero_stp,    SIZE },
        { "stnp xzr (non-temp.) ", zero_stnp,   SIZE },
        { "dc zva               ", zero_zva,    SIZE },
        { "libc memset(0)       ", zero_memset, SIZE },
        { "read sum (reference) ", read_sum,    SIZE },
        { "str 8 B per 128 B    ", sparse8,     SIZE / 16 },
    };
    printf("128 MiB buffer, median of %d runs\n", REPS);
    printf("  kernel                    ms     GB/s(written or read)   ns per 128 B line\n");
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++) {
        double t = bench(k[i].f, buf);
        printf("  %s %8.2f   %8.1f              %8.3f\n", k[i].name, t * 1e3, k[i].bytes / t / 1e9,
               t * 1e9 / (double)(SIZE / 128u));
    }
    free(buf);
    return 0;
}
#endif
