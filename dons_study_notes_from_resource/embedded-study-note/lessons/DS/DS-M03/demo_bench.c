/* DS-M03 데모 — 슬랩 할당기 vs 시스템 malloc (macOS libmalloc)
 *   1) 속도: LIFO 쌍 / 묶음 FIFO / 랜덤 혼합 — 각 5번 재서 최솟값
 *   2) 내부 단편화: 같은 메시지 크기에 대해 슬랩 클래스 vs malloc_size()
 * solution.c 의 할당기를 그대로 쓰려고 main 이름만 바꿔 포함한다 (테스트 main 은 호출하지 않음).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
 */
#define _DARWIN_C_SOURCE
#include <malloc/malloc.h>
#include <stdlib.h>
#include <time.h>
#define main dsm03_solution_tests
#include "solution.c"
#undef main

static uint64_t now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}
#define ESCAPE(p) __asm__ volatile("" : : "r"(p) : "memory")   /* 컴파일러가 alloc/free 쌍을 지우지 못하게 */

static void *s_alloc(size_t n) { return slab_malloc(n); }
static void  s_free(void *p)   { slab_free(p); }
static void *m_alloc(size_t n) { return malloc(n); }
static void  m_free(void *p)   { free(p); }
typedef void *(*alloc_fn)(size_t);
typedef void (*free_fn)(void *);

static void *g_ptr[4096];

/* 1) 같은 크기 alloc → 1바이트 쓰기 → free, 2M 번 */
static double bench_lifo(alloc_fn A, free_fn F) {
    enum { N = 2000000 };
    uint64_t t0 = now_ns();
    for (int i = 0; i < N; i++) {
        uint8_t *p = A(48);
        *(volatile uint8_t *)p = (uint8_t)i;
        ESCAPE(p);
        F(p);
    }
    return (double)(now_ns() - t0) / N;
}
/* 2) 64B 3000개 할당 → 할당한 순서대로 해제, 200 라운드 */
static double bench_batch(alloc_fn A, free_fn F) {
    enum { R = 200, K = 3000 };
    uint64_t t0 = now_ns();
    for (int r = 0; r < R; r++) {
        for (int i = 0; i < K; i++) { g_ptr[i] = A(64); *(volatile uint8_t *)g_ptr[i] = 1; }
        for (int i = 0; i < K; i++) F(g_ptr[i]);
    }
    return (double)(now_ns() - t0) / ((double)R * K);
}
/* 3) 슬롯 1000개, 슬롯이 차 있으면 free 비어 있으면 1..128B alloc, 2M 번 */
static double bench_mixed(alloc_fn A, free_fn F, uint32_t *fails) {
    enum { N = 2000000, S = 1000 };
    memset(g_ptr, 0, sizeof g_ptr);
    rng_state = 12345u;
    *fails = 0;
    uint64_t t0 = now_ns();
    for (int i = 0; i < N; i++) {
        uint32_t r = xorshift32();
        uint32_t k = r % S;
        if (g_ptr[k]) { F(g_ptr[k]); g_ptr[k] = NULL; }
        else {
            g_ptr[k] = A(1u + (r >> 16) % 128u);
            if (g_ptr[k]) *(volatile uint8_t *)g_ptr[k] = 1; else (*fails)++;
        }
    }
    double ns = (double)(now_ns() - t0) / N;
    for (int k = 0; k < S; k++) if (g_ptr[k]) { F(g_ptr[k]); g_ptr[k] = NULL; }
    return ns;
}

static double best(double a, double b) { return a < b ? a : b; }

int main(void) {
    slab_sys_init();
    double sl = 1e9, ml = 1e9, sb = 1e9, mb = 1e9, sm = 1e9, mm = 1e9;
    uint32_t fs = 0, fm = 0;
    for (int rep = 0; rep < 5; rep++) {
        sl = best(sl, bench_lifo(s_alloc, s_free));   ml = best(ml, bench_lifo(m_alloc, m_free));
        sb = best(sb, bench_batch(s_alloc, s_free));  mb = best(mb, bench_batch(m_alloc, m_free));
        sm = best(sm, bench_mixed(s_alloc, s_free, &fs)); mm = best(mm, bench_mixed(m_alloc, m_free, &fm));
    }
    slab_reap();
    printf("speed (ns per alloc+free pair, or per op for mixed; best of 5)\n");
    printf("  pattern                 slab    malloc   malloc/slab\n");
    printf("  LIFO pair 48B        %7.2f  %7.2f   %5.1fx\n", sl, ml, ml / sl);
    printf("  batch 3000x64B FIFO  %7.2f  %7.2f   %5.1fx\n", sb, mb, mb / sb);
    printf("  mixed 1..128B (op)   %7.2f  %7.2f   %5.1fx   (slab NULLs %u)\n", sm, mm, mm / sm, fs);
    printf("  arena intact after bench: %s\n", slab_check() && g_pool_n == NSLABS ? "yes" : "NO");

    printf("\ninternal fragmentation per request size\n");
    printf("  req   slab class   malloc_size\n");
    static const size_t req[] = { 1, 8, 16, 17, 20, 24, 33, 48, 65, 72, 82, 100, 128 };
    for (unsigned i = 0; i < sizeof req / sizeof req[0]; i++) {
        void *p = malloc(req[i]);
        ESCAPE(p);
        printf("  %4zu   %10u   %11zu\n", req[i], 16u << size_to_class(req[i]), malloc_size(p));
        free(p);
    }
    /* Part C 와 같은 메시지 혼합 (가중치 20/40/15/15/10 %) */
    static const struct { size_t n; unsigned w; } mix[] = { { 20, 20 }, { 24, 40 }, { 48, 15 }, { 72, 15 }, { 82, 10 } };
    double rq = 0, sc = 0, mc = 0;
    for (unsigned i = 0; i < 5; i++) {
        void *p = malloc(mix[i].n);
        ESCAPE(p);
        rq += (double)mix[i].w * mix[i].n;
        sc += (double)mix[i].w * (16u << size_to_class(mix[i].n));
        mc += (double)mix[i].w * malloc_size(p);
        free(p);
    }
    printf("  message mix: slab %.1f%% waste, malloc %.1f%% waste\n", 100 * (sc - rq) / sc, 100 * (mc - rq) / mc);
    return 0;
}
