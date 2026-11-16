/* ARM-A04 demo_bench.c — JIT 패치 한 번에 드는 비용 (CHECK 없음; 중앙값 7회, 부하에 따라 흔들림)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c solution.S -o /tmp/ARM-A04_bench && /tmp/ARM-A04_bench
 *        (solution.S 의 cache_sync_range 를 그대로 잰다)
 */
#define _DARWIN_C_SOURCE
#include <libkern/OSCacheControl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

uint64_t cache_sync_range(void *start, size_t len, size_t dline, size_t iline, unsigned flags);

static uint32_t *g_jit, *g_plain;
static size_t g_len;
static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

static void op_toggle(void)     { pthread_jit_write_protect_np(0); pthread_jit_write_protect_np(1); }
static void op_sys(void)        { sys_icache_invalidate(g_jit, g_len); }
static void op_manual(void)     { (void)cache_sync_range(g_jit, g_len, 64, 64, 0); }
static void op_manual_ic(void)  { (void)cache_sync_range(g_jit, g_len, 64, 64, 1); }   /* SKIP_DC */
static void op_mprotect(void)   { mprotect(g_plain, 16384, PROT_READ | PROT_WRITE); mprotect(g_plain, 16384, PROT_READ | PROT_EXEC); }
static void op_patch(void)      { pthread_jit_write_protect_np(0); g_jit[19] ^= 1u << 10; pthread_jit_write_protect_np(1);
                                  sys_icache_invalidate(g_jit + 19, 4); }

static double bench(void (*op)(void), int iters) {
    double s[7];
    for (int r = 0; r < 7; r++) {
        uint64_t t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        for (int i = 0; i < iters; i++) op();
        uint64_t t1 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        s[r] = (double)(t1 - t0) / iters;
    }
    qsort(s, 7, sizeof s[0], cmp_d);
    return s[3];
}

int main(void) {
    g_jit = mmap(NULL, 65536, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
    g_plain = mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (g_jit == MAP_FAILED || g_plain == MAP_FAILED) return 1;
    pthread_jit_write_protect_np(0);
    for (int i = 0; i < 16384; i++) g_jit[i] = 0xD65F03C0u;
    g_jit[19] = 0x91000400u;
    pthread_jit_write_protect_np(1);
    sys_icache_invalidate(g_jit, 65536);

    printf("%-48s %10s\n", "operation", "ns/op");
    printf("%-48s %10.1f\n", "pthread_jit_write_protect_np(0)+(1) pair", bench(op_toggle, 200000));
    const size_t lens[] = {4, 4096, 65536};
    for (int k = 0; k < 3; k++) {
        g_len = lens[k];
        int it = k == 0 ? 200000 : k == 1 ? 20000 : 2000;
        char name[64];
        snprintf(name, sizeof name, "sys_icache_invalidate %zu B", g_len);               printf("%-48s %10.1f\n", name, bench(op_sys, it));
        snprintf(name, sizeof name, "cache_sync_range DC+IC %zu B (64 B lines)", g_len);  printf("%-48s %10.1f\n", name, bench(op_manual, it));
        snprintf(name, sizeof name, "cache_sync_range IC only %zu B", g_len);             printf("%-48s %10.1f\n", name, bench(op_manual_ic, it));
    }
    printf("%-48s %10.1f\n", "mprotect RW + RX pair (non-JIT page, 2 syscalls)", bench(op_mprotect, 20000));
    printf("%-48s %10.1f\n", "full 1-word patch: toggle + store + sys_icache", bench(op_patch, 200000));
    return 0;
}
