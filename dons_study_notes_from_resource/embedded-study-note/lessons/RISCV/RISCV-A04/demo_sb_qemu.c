/* ENV: qemu */
/* RISCV-A04 demo_sb_qemu.c — Store Buffering 리트머스를 qemu-riscv64 (user mode, MTTCG) 위에서 돌린다.
 * Lima VM elinux (aarch64 호스트) 안에서:
 *   riscv64-linux-gnu-gcc -std=c11 -Wall -Wextra -Werror -O2 -static -pthread demo_sb_qemu.c -o sb_rv
 *   qemu-riscv64 ./sb_rv 200000          # 변형 5개 × N 회
 * 변형: plain / fence.tso / fence r,rw / fence w,r / fence rw,rw (스펙상 앞의 셋은 weird 허용, 뒤 둘은 금지)
 * 각 반복: 두 스레드가 스핀 배리어로 맞춰 출발 → T0: x=1; [fence]; r0=y   T1: y=1; [fence]; r1=x
 * "weird" = r0==0 && r1==0 (SC 에서는 불가능, TSO/RVWMO 에서는 허용).
 * 관찰 0 ≠ 금지, 관찰 > 0 이면 "그 구현(QEMU TCG + arm64 호스트)이 재배치를 실제로 드러냈다".
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#define PAD 16                      /* 변수 간 간격 (int 16개 = 64 B) */

static int *xs, *ys, *r0s, *r1s;
static atomic_int arrive;
static int g_variant;
static long g_n;

/* 변형별 "store; fence; load" — 순수 RISC-V 명령을 inline asm 으로 고정 (컴파일러 재배치도 차단) */
static inline int sb_side(int *mine, int *other, int variant)
{
    int r;
    switch (variant) {
    case 0: __asm__ volatile("sw %2, 0(%1)\n\tlw %0, 0(%3)" : "=&r"(r) : "r"(mine), "r"(1), "r"(other) : "memory"); break;
    case 1: __asm__ volatile("sw %2, 0(%1)\n\tfence.tso\n\tlw %0, 0(%3)" : "=&r"(r) : "r"(mine), "r"(1), "r"(other) : "memory"); break;
    case 2: __asm__ volatile("sw %2, 0(%1)\n\tfence r, rw\n\tlw %0, 0(%3)" : "=&r"(r) : "r"(mine), "r"(1), "r"(other) : "memory"); break;
    case 3: __asm__ volatile("sw %2, 0(%1)\n\tfence w, r\n\tlw %0, 0(%3)" : "=&r"(r) : "r"(mine), "r"(1), "r"(other) : "memory"); break;
    default: __asm__ volatile("sw %2, 0(%1)\n\tfence rw, rw\n\tlw %0, 0(%3)" : "=&r"(r) : "r"(mine), "r"(1), "r"(other) : "memory"); break;
    }
    return r;
}

static void barrier(long i)
{
    atomic_fetch_add_explicit(&arrive, 1, memory_order_acq_rel);
    while (atomic_load_explicit(&arrive, memory_order_acquire) < 2 * (i + 1)) { }
}

static void *thr(void *arg)
{
    int me = (int)(long)arg;
    for (long i = 0; i < g_n; i++) {
        barrier(i);
        int *x = &xs[i * PAD], *y = &ys[i * PAD];
        if (me == 0) r0s[i] = sb_side(x, y, g_variant);
        else         r1s[i] = sb_side(y, x, g_variant);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    static const char *name[5] = {"plain sw;lw", "fence.tso", "fence r,rw", "fence w,r", "fence rw,rw"};
    g_n = argc > 1 ? atol(argv[1]) : 200000;
    xs = calloc((size_t)g_n * PAD, sizeof(int)); ys = calloc((size_t)g_n * PAD, sizeof(int));
    r0s = calloc((size_t)g_n, sizeof(int)); r1s = calloc((size_t)g_n, sizeof(int));
    if (!xs || !ys || !r0s || !r1s) return 2;
    for (int v = 0; v < 5; v++) {
        for (long i = 0; i < g_n * PAD; i++) { xs[i] = 0; ys[i] = 0; }
        atomic_store(&arrive, 0);
        g_variant = v;
        pthread_t a, b;
        pthread_create(&a, NULL, thr, (void *)0L);
        pthread_create(&b, NULL, thr, (void *)1L);
        pthread_join(a, NULL); pthread_join(b, NULL);
        long hist[4] = {0, 0, 0, 0};      /* index = r0*2 + r1 */
        for (long i = 0; i < g_n; i++) hist[r0s[i] * 2 + r1s[i]]++;
        printf("%-12s N=%ld  (r0,r1)=(0,0):%ld  (0,1):%ld  (1,0):%ld  (1,1):%ld\n",
               name[v], g_n, hist[0], hist[1], hist[2], hist[3]);
    }
    free(xs); free(ys); free(r0s); free(r1s);
    return 0;
}
