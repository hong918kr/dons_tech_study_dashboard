/* ENV: mac — macOS 14.4+ (os_sync_wait_on_address). 리눅스 futex 레슨의 "맥 대응물" 비교용 데모 */
/* OS-A01 demo_mac_ulock — 같은 0/1/2 뮤텍스를 맥의 공개 API 로 + os_unfair_lock / pthread_mutex 와 비교
 *
 *   futex(2) 자리:  os_sync_wait_on_address(addr, expected, 4, NONE)       (<os/os_sync_wait_on_address.h>)
 *                   os_sync_wake_by_address_any(addr, 4, NONE)
 *   차이 하나:      값이 이미 다르면 리눅스는 -EAGAIN, 맥은 "성공(>= 0)" 으로 즉시 돌아온다 (헤더 주석)
 *
 *   ./mac_ulock            정확성(8 스레드 합계) + 경합 없음 ns/쌍 + 경합 T=1,2,4,8 Mops/s
 *
 * build: export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
 *        cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_mac_ulock.c -o /tmp/OS-A01_work/mac_ulock
 */
#define _DARWIN_C_SOURCE
#include <os/lock.h>
#include <os/os_sync_wait_on_address.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

static atomic_long n_wait, n_wake;

typedef struct { atomic_uint v; } m2_t;
static unsigned cas(atomic_uint *p, unsigned e, unsigned d) {
    atomic_compare_exchange_strong_explicit(p, &e, d, memory_order_acquire, memory_order_relaxed);
    return e;
}
static void m2_lock(m2_t *m) {
    unsigned c = cas(&m->v, 0, 1);
    if (c == 0) return;
    do {
        if (c == 2 || cas(&m->v, 1, 2) != 0) {
            atomic_fetch_add_explicit(&n_wait, 1, memory_order_relaxed);
            os_sync_wait_on_address(&m->v, 2, 4, OS_SYNC_WAIT_ON_ADDRESS_NONE); /* -1/EINTR 도 그냥 다시 시도 */
        }
    } while ((c = cas(&m->v, 0, 2)) != 0);
}
static void m2_unlock(m2_t *m) {
    if (atomic_fetch_sub_explicit(&m->v, 1, memory_order_release) != 1) {
        atomic_store_explicit(&m->v, 0, memory_order_release);
        atomic_fetch_add_explicit(&n_wake, 1, memory_order_relaxed);
        os_sync_wake_by_address_any(&m->v, 4, OS_SYNC_WAKE_BY_ADDRESS_NONE); /* 대기자 없으면 -1/ENOENT */
    }
}

static m2_t g_m2;
static os_unfair_lock g_ul = OS_UNFAIR_LOCK_INIT;
static pthread_mutex_t g_pm = PTHREAD_MUTEX_INITIALIZER;
enum { K_M2, K_UNFAIR, K_PTHREAD, NK };
static const char *NAME[NK] = { "m2 (os_sync_wait)", "os_unfair_lock", "pthread_mutex" };
static inline void L(int k) { if (k == K_M2) m2_lock(&g_m2); else if (k == K_UNFAIR) os_unfair_lock_lock(&g_ul); else pthread_mutex_lock(&g_pm); }
static inline void U(int k) { if (k == K_M2) m2_unlock(&g_m2); else if (k == K_UNFAIR) os_unfair_lock_unlock(&g_ul); else pthread_mutex_unlock(&g_pm); }

static long g_cs;
static atomic_int g_stop;
typedef struct { int k; long n; long iters; } job_t;
static void *worker(void *p) {
    job_t *j = p;
    long n = 0;
    if (j->iters) { for (long i = 0; i < j->iters; i++) { L(j->k); g_cs++; U(j->k); } n = j->iters; }
    else while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) { L(j->k); g_cs++; U(j->k); n++; }
    j->n = n;
    return NULL;
}
static uint64_t now(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }

int main(void) {
    /* 정확성: 8 스레드 × 100000 */
    for (int k = 0; k < NK; k++) {
        pthread_t t[8]; job_t j[8];
        g_cs = 0;
        for (int i = 0; i < 8; i++) { j[i] = (job_t){ k, 0, 100000 }; pthread_create(&t[i], NULL, worker, &j[i]); }
        for (int i = 0; i < 8; i++) pthread_join(t[i], NULL);
        printf("정확성 %-18s 8x100000 → %ld %s\n", NAME[k], g_cs, g_cs == 800000 ? "OK" : "MISMATCH");
    }
    /* 경합 없음 */
    for (int k = 0; k < NK; k++) {
        double best = 1e30;
        for (int r = 0; r < 3; r++) {
            uint64_t t0 = now();
            for (int i = 0; i < 5000000; i++) { L(k); g_cs++; U(k); }
            double ns = (double)(now() - t0) / 5e6;
            if (ns < best) best = ns;
        }
        printf("경합 없음 %-18s %.2f ns/쌍 (best of 3)\n", NAME[k], best);
    }
    /* 경합: 고정 100 ms */
    static const int TS[] = { 1, 2, 4, 8 };
    for (int k = 0; k < NK; k++)
        for (int ti = 0; ti < 4; ti++) {
            int T = TS[ti];
            pthread_t t[8]; job_t j[8];
            atomic_store(&g_stop, 0); g_cs = 0;
            long w0 = atomic_load(&n_wait), k0 = atomic_load(&n_wake);
            uint64_t t0 = now();
            for (int i = 0; i < T; i++) { j[i] = (job_t){ k, 0, 0 }; pthread_create(&t[i], NULL, worker, &j[i]); }
            struct timespec d = { 0, 100000000L };
            nanosleep(&d, NULL);
            atomic_store(&g_stop, 1);
            for (int i = 0; i < T; i++) pthread_join(t[i], NULL);
            double dt = (double)(now() - t0);
            long tot = 0, mn = j[0].n, mx = j[0].n;
            for (int i = 0; i < T; i++) { tot += j[i].n; if (j[i].n < mn) mn = j[i].n; if (j[i].n > mx) mx = j[i].n; }
            if (k == K_M2)
                printf("경합 %-18s T=%d %7.2f Mops/s  wait/op %.3f wake/op %.3f  min/mean %.2f max/mean %.2f\n", NAME[k], T,
                       tot / (dt / 1e3), (double)(atomic_load(&n_wait) - w0) / tot, (double)(atomic_load(&n_wake) - k0) / tot,
                       mn / ((double)tot / T), mx / ((double)tot / T));
            else
                printf("경합 %-18s T=%d %7.2f Mops/s  min/mean %.2f max/mean %.2f\n", NAME[k], T, tot / (dt / 1e3),
                       mn / ((double)tot / T), mx / ((double)tot / T));
        }
    return 0;
}
