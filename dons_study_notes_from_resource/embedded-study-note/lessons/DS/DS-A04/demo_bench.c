/* DS-A04 demo_bench.c — lock-free 스택 vs 락 스택 처리량 (관찰용, 체크 아님)
 *
 * 스레드마다 "pop 하나 + push 하나" 를 200 ms 동안 반복. 풀 64 노드. 3번 측정해 중앙값 (Mpairs/s).
 *   tag64   : 64비트 (태그, 인덱스) CAS  — solution.c Part B
 *   tag64bo : 같은 것 + CAS 실패 시 지수 backoff (yield 명령 1→2→…→64 회 쉬기)
 *   dw128   : 16바이트 {포인터, 태그} CAS (caspa/caspl) — Part C
 *   mutex   : 같은 연결 리스트를 pthread_mutex 로 보호
 *   unfair  : os_unfair_lock (Darwin 의 가벼운 락, 대기 시 커널에서 잠듦)
 * 다른 벤치마크가 동시에 돌 수 있다 → 절대값보다 비율을 볼 것. 실행 시간 약 12 초.
 * build: cc -std=c11 -O2 -pthread demo_bench.c -o db && ./db                          (LSE: casa/casl, caspa/caspl)
 *        cc -std=c11 -O2 -pthread -mcpu=apple-m1+nolse demo_bench.c -o db_llsc && ./db_llsc  (LL/SC: ldaxr/stlxr, ldaxp/stlxp 계열)
 */
#define _DARWIN_C_SOURCE
#include <os/lock.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NIL 0xFFFFFFFFu
#define POOL 64u
typedef struct { _Atomic uint32_t next; uint32_t value; } node_t;
static node_t g_pool[POOL];

static _Atomic uint64_t t64;
static void t64_push(uint32_t i) {
    uint64_t old = atomic_load_explicit(&t64, memory_order_relaxed), nw;
    do {
        atomic_store_explicit(&g_pool[i].next, (uint32_t)old, memory_order_relaxed);
        nw = ((uint64_t)((uint32_t)(old >> 32) + 1u) << 32) | i;
    } while (!atomic_compare_exchange_weak_explicit(&t64, &old, nw, memory_order_release, memory_order_relaxed));
}
static uint32_t t64_pop(void) {
    uint64_t old = atomic_load_explicit(&t64, memory_order_acquire);
    while ((uint32_t)old != NIL) {
        uint32_t next = atomic_load_explicit(&g_pool[(uint32_t)old].next, memory_order_relaxed);
        uint64_t nw = ((uint64_t)((uint32_t)(old >> 32) + 1u) << 32) | next;
        if (atomic_compare_exchange_weak_explicit(&t64, &old, nw, memory_order_acquire, memory_order_acquire)) return (uint32_t)old;
    }
    return NIL;
}

static void backoff(unsigned *b) {
    for (unsigned k = 0; k < *b; k++) __asm__ volatile("yield" ::: "memory");
    if (*b < 64u) *b *= 2u;
}
static void bo_push(uint32_t i) {
    unsigned b = 1;
    uint64_t old = atomic_load_explicit(&t64, memory_order_relaxed), nw;
    for (;;) {
        atomic_store_explicit(&g_pool[i].next, (uint32_t)old, memory_order_relaxed);
        nw = ((uint64_t)((uint32_t)(old >> 32) + 1u) << 32) | i;
        if (atomic_compare_exchange_weak_explicit(&t64, &old, nw, memory_order_release, memory_order_relaxed)) return;
        backoff(&b);
    }
}
static uint32_t bo_pop(void) {
    unsigned b = 1;
    uint64_t old = atomic_load_explicit(&t64, memory_order_acquire);
    while ((uint32_t)old != NIL) {
        uint32_t next = atomic_load_explicit(&g_pool[(uint32_t)old].next, memory_order_relaxed);
        uint64_t nw = ((uint64_t)((uint32_t)(old >> 32) + 1u) << 32) | next;
        if (atomic_compare_exchange_weak_explicit(&t64, &old, nw, memory_order_acquire, memory_order_acquire)) return (uint32_t)old;
        backoff(&b);
    }
    return NIL;
}

typedef struct pn { _Atomic(struct pn *) next; } pn_t;
typedef struct { pn_t *ptr; uintptr_t tag; } tp_t;
static pn_t g_pn[POOL];
static _Atomic tp_t t128;
static void dw_push(uint32_t i) {
    pn_t *p = &g_pn[i];
    tp_t old = atomic_load_explicit(&t128, memory_order_relaxed), nw;
    do {
        atomic_store_explicit(&p->next, old.ptr, memory_order_relaxed);
        nw = (tp_t){ p, old.tag + 1u };
    } while (!atomic_compare_exchange_weak_explicit(&t128, &old, nw, memory_order_release, memory_order_relaxed));
}
static uint32_t dw_pop(void) {
    tp_t old = atomic_load_explicit(&t128, memory_order_acquire);
    while (old.ptr) {
        tp_t nw = { atomic_load_explicit(&old.ptr->next, memory_order_relaxed), old.tag + 1u };
        if (atomic_compare_exchange_weak_explicit(&t128, &old, nw, memory_order_acquire, memory_order_acquire))
            return (uint32_t)(old.ptr - g_pn);
    }
    return NIL;
}

static uint32_t ltop = NIL;
static pthread_mutex_t g_m = PTHREAD_MUTEX_INITIALIZER;
static os_unfair_lock g_u = OS_UNFAIR_LOCK_INIT;
static void l_push(uint32_t i) { atomic_store_explicit(&g_pool[i].next, ltop, memory_order_relaxed); ltop = i; }
static uint32_t l_pop(void) { uint32_t o = ltop; if (o != NIL) ltop = atomic_load_explicit(&g_pool[o].next, memory_order_relaxed); return o; }
static void m_push(uint32_t i) { pthread_mutex_lock(&g_m); l_push(i); pthread_mutex_unlock(&g_m); }
static uint32_t m_pop(void) { pthread_mutex_lock(&g_m); uint32_t o = l_pop(); pthread_mutex_unlock(&g_m); return o; }
static void u_push(uint32_t i) { os_unfair_lock_lock(&g_u); l_push(i); os_unfair_lock_unlock(&g_u); }
static uint32_t u_pop(void) { os_unfair_lock_lock(&g_u); uint32_t o = l_pop(); os_unfair_lock_unlock(&g_u); return o; }

typedef struct { const char *name; void (*push)(uint32_t); uint32_t (*pop)(void); void (*reset)(void); } impl_t;
static void r_t64(void) { atomic_store(&t64, (uint64_t)NIL); }
static void r_dw(void) { atomic_store(&t128, ((tp_t){ NULL, 0 })); }
static void r_l(void) { ltop = NIL; }
static const impl_t IMPL[] = { { "tag64", t64_push, t64_pop, r_t64 }, { "tag64bo", bo_push, bo_pop, r_t64 },
                               { "dw128", dw_push, dw_pop, r_dw },
                               { "mutex", m_push, m_pop, r_l }, { "unfair", u_push, u_pop, r_l } };

static const impl_t *g_impl;
static atomic_int g_ready, g_stop;
static void *run(void *p) {
    unsigned long long n = 0;
    atomic_fetch_add(&g_ready, 1);
    while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
        uint32_t i = g_impl->pop();
        if (i != NIL) { g_pool[i].value++; g_impl->push(i); }   /* 락 버전도 같은 일: 소유 중 value++ */
        n++;
    }
    *(unsigned long long *)p = n;
    return NULL;
}
static double measure(const impl_t *im, int nth) {
    g_impl = im;
    im->reset();
    for (uint32_t i = 0; i < POOL; i++) im->push(i);
    atomic_store(&g_ready, 0);
    atomic_store(&g_stop, 0);
    pthread_t t[16];
    unsigned long long cnt[16];
    for (int i = 0; i < nth; i++) pthread_create(&t[i], NULL, run, &cnt[i]);
    while (atomic_load(&g_ready) < nth) {}
    struct timespec a, b, d = { 0, 200 * 1000 * 1000 };
    clock_gettime(CLOCK_MONOTONIC, &a);
    nanosleep(&d, NULL);
    atomic_store(&g_stop, 1);
    clock_gettime(CLOCK_MONOTONIC, &b);
    unsigned long long tot = 0;
    for (int i = 0; i < nth; i++) { pthread_join(t[i], NULL); tot += cnt[i]; }
    double sec = (double)(b.tv_sec - a.tv_sec) + (double)(b.tv_nsec - a.tv_nsec) * 1e-9;
    return (double)tot / sec / 1e6;
}
static int cmpd(const void *x, const void *y) { double a = *(const double *)x, b = *(const double *)y; return (a > b) - (a < b); }
int main(void) {
    const int ths[] = { 1, 2, 4, 8 };
    printf("Mpairs/s (pop+push), median of 3 [min-max]\n%-8s", "impl");
    for (int k = 0; k < 4; k++) printf("  %4d thr            ", ths[k]);
    printf("\n");
    for (int m = 0; m < 5; m++) {
        printf("%-8s", IMPL[m].name);
        for (int k = 0; k < 4; k++) {
            double r[3];
            for (int j = 0; j < 3; j++) r[j] = measure(&IMPL[m], ths[k]);
            qsort(r, 3, sizeof r[0], cmpd);
            printf("  %6.1f [%5.1f-%5.1f]", r[1], r[0], r[2]);
        }
        printf("\n");
    }
    return 0;
}
