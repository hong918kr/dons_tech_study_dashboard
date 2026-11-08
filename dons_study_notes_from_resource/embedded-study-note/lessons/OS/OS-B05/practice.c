/* OS-B05 추가 연습 — 한 번에 검증
 *   연습 1  락 스트라이핑(lock striping) 히스토그램: 버킷 64 개, 락 8 개 + 전체 합계는 락을 오름차순으로
 *   연습 2  경합 계측 뮤텍스: trylock 실패 = 경합 1 회
 *   연습 3  오류 검사 뮤텍스(PTHREAD_MUTEX_ERRORCHECK): 재잠금 EDEADLK, 남의 락 해제 EPERM
 *   연습 4  sloppy counter 종료 flush: 스레드가 끝날 때 자기 로컬을 전역으로
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define CACHE_LINE 128
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

#define NT 4
#define ITERS 100000
static uint32_t xorshift32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* ===== 연습 1 — 락 스트라이핑 히스토그램 ===== */
#define NBUCKET 64
#define NSTRIPE 8
typedef struct {
    struct { _Alignas(CACHE_LINE) pthread_mutex_t m; } lock[NSTRIPE];   /* 버킷 b 는 lock[b % NSTRIPE] */
    long count[NBUCKET];
} hist_t;
static void hist_init(hist_t *h) {
    for (int i = 0; i < NSTRIPE; i++) pthread_mutex_init(&h->lock[i].m, NULL);
    for (int b = 0; b < NBUCKET; b++) h->count[b] = 0;
}
static void hist_add(hist_t *h, unsigned v) {
    unsigned b = v % NBUCKET;
    pthread_mutex_t *m = &h->lock[b % NSTRIPE].m;
    pthread_mutex_lock(m);
    h->count[b]++;
    pthread_mutex_unlock(m);
}
/* 모든 버킷의 일관된 합계: 스트라이프 락을 0 → 7 순서로 전부 잡은 뒤 합하고 역순으로 푼다. */
static long hist_total(hist_t *h) {
    for (int i = 0; i < NSTRIPE; i++) pthread_mutex_lock(&h->lock[i].m);
    long t = 0;
    for (int b = 0; b < NBUCKET; b++) t += h->count[b];
    for (int i = NSTRIPE - 1; i >= 0; i--) pthread_mutex_unlock(&h->lock[i].m);
    return t;
}
static hist_t g_h;
static atomic_int g_done;
static void *hist_worker(void *arg) {
    uint32_t s = (uint32_t)(uintptr_t)arg * 7919u + 1u;
    for (int i = 0; i < ITERS; i++) hist_add(&g_h, xorshift32(&s));
    return NULL;
}
static long g_hist_bad;
static void *hist_observer(void *arg) {         /* 합계는 줄지 않고 총합을 넘지 않는다 */
    (void)arg;
    long last = 0;
    while (!atomic_load(&g_done)) {
        long t = hist_total(&g_h);
        if (t < last || t > (long)NT * ITERS) g_hist_bad++;
        last = t;
    }
    return NULL;
}

/* ===== 연습 2 — 경합 계측 뮤텍스 ===== */
typedef struct {
    pthread_mutex_t m;
    atomic_long acquisitions, contended;
} cmutex_t;
static void cmutex_lock(cmutex_t *c) {
    if (pthread_mutex_trylock(&c->m) != 0) {      /* EBUSY: 누가 쥐고 있다 = 경합 */
        atomic_fetch_add_explicit(&c->contended, 1, memory_order_relaxed);
        pthread_mutex_lock(&c->m);
    }
    atomic_fetch_add_explicit(&c->acquisitions, 1, memory_order_relaxed);
}
static void cmutex_unlock(cmutex_t *c) { pthread_mutex_unlock(&c->m); }
static cmutex_t g_cm;
static void *cm_worker(void *arg) { (void)arg; cmutex_lock(&g_cm); cmutex_unlock(&g_cm); return NULL; }

/* ===== 연습 3 — 오류 검사 뮤텍스 ===== */
static int make_errorcheck(pthread_mutex_t *m) {
    pthread_mutexattr_t a;
    int rc = pthread_mutexattr_init(&a);
    if (rc) return rc;
    rc = pthread_mutexattr_settype(&a, PTHREAD_MUTEX_ERRORCHECK);
    if (rc == 0) rc = pthread_mutex_init(m, &a);
    pthread_mutexattr_destroy(&a);
    return rc;
}
static pthread_mutex_t g_em;
static void *foreign_unlock(void *arg) { *(int *)arg = pthread_mutex_unlock(&g_em); return NULL; }

/* ===== 연습 4 — sloppy counter 종료 flush ===== */
#define SC_MAXT 8
typedef struct { _Alignas(CACHE_LINE) pthread_mutex_t lock; long count; } sc_local_t;
typedef struct { pthread_mutex_t glock; long global, threshold; int nthreads; sc_local_t local[SC_MAXT]; } sloppy_t;
static void sc_init(sloppy_t *c, int n, long s) {
    pthread_mutex_init(&c->glock, NULL);
    c->global = 0; c->threshold = s; c->nthreads = n;
    for (int i = 0; i < n; i++) { pthread_mutex_init(&c->local[i].lock, NULL); c->local[i].count = 0; }
}
static void sc_move(sloppy_t *c, sc_local_t *l) {   /* l->lock 을 쥔 상태에서: local → global */
    pthread_mutex_lock(&c->glock);
    c->global += l->count;
    pthread_mutex_unlock(&c->glock);
    l->count = 0;
}
static void sc_update(sloppy_t *c, int tid, long amt) {
    sc_local_t *l = &c->local[tid];
    pthread_mutex_lock(&l->lock);
    l->count += amt;
    if (l->count >= c->threshold) sc_move(c, l);
    pthread_mutex_unlock(&l->lock);
}
/* 스레드 tid 가 끝날 때 한 번: 남은 로컬을 전역으로. 모두 부르면 sc_get 이 정확해진다. */
static void sc_thread_done(sloppy_t *c, int tid) {
    sc_local_t *l = &c->local[tid];
    pthread_mutex_lock(&l->lock);
    sc_move(c, l);
    pthread_mutex_unlock(&l->lock);
}
static long sc_get(sloppy_t *c) {
    pthread_mutex_lock(&c->glock);
    long v = c->global;
    pthread_mutex_unlock(&c->glock);
    return v;
}
static sloppy_t g_sc;
static void *sc_worker(void *arg) {
    int tid = (int)(intptr_t)arg;
    for (int i = 0; i < ITERS + tid; i++) sc_update(&g_sc, tid, 1);   /* 스레드마다 다른 개수 */
    sc_thread_done(&g_sc, tid);
    return NULL;
}

int main(void) {
    pthread_t th[NT], ob;
    /* 연습 1 */
    hist_init(&g_h);
    pthread_create(&ob, NULL, hist_observer, NULL);
    for (int i = 0; i < NT; i++) pthread_create(&th[i], NULL, hist_worker, (void *)(uintptr_t)i);
    for (int i = 0; i < NT; i++) pthread_join(th[i], NULL);
    atomic_store(&g_done, 1);
    pthread_join(ob, NULL);
    long want[NBUCKET] = {0};
    for (int t = 0; t < NT; t++) {                   /* 기대값: 같은 시드로 단일 스레드 계산 */
        uint32_t s = (uint32_t)t * 7919u + 1u;
        for (int i = 0; i < ITERS; i++) want[xorshift32(&s) % NBUCKET]++;
    }
    int diff = 0;
    for (int b = 0; b < NBUCKET; b++) diff += g_h.count[b] != want[b];
    CHECK(diff == 0);                                /* 버킷마다 정확 */
    CHECK(hist_total(&g_h) == (long)NT * ITERS);
    CHECK(g_hist_bad == 0);

    /* 연습 2 */
    pthread_mutex_init(&g_cm.m, NULL);
    cmutex_lock(&g_cm); cmutex_unlock(&g_cm);
    CHECK(atomic_load(&g_cm.contended) == 0 && atomic_load(&g_cm.acquisitions) == 1);   /* 혼자면 경합 0 */
    pthread_mutex_lock(&g_cm.m);                     /* main 이 쥔 채로 워커를 출발시킨다 → 반드시 경합 */
    pthread_create(&th[0], NULL, cm_worker, NULL);
    for (int i = 0; i < 2000 && atomic_load(&g_cm.contended) == 0; i++) {   /* 최대 2 초 기다림 (멈추지 않게) */
        struct timespec ts = {0, 1000000L};
        nanosleep(&ts, NULL);
    }
    pthread_mutex_unlock(&g_cm.m);
    pthread_join(th[0], NULL);
    CHECK(atomic_load(&g_cm.contended) == 1 && atomic_load(&g_cm.acquisitions) == 2);
    pthread_mutex_destroy(&g_cm.m);

    /* 연습 3 */
    CHECK(make_errorcheck(&g_em) == 0);
    CHECK(pthread_mutex_lock(&g_em) == 0);
    CHECK(pthread_mutex_lock(&g_em) == EDEADLK);    /* 같은 스레드가 다시 잠금 → 교착 대신 오류 */
    int frc = -1;
    pthread_create(&th[0], NULL, foreign_unlock, &frc);
    pthread_join(th[0], NULL);
    CHECK(frc == EPERM);                             /* 소유자가 아닌 스레드의 해제 → 오류 */
    CHECK(pthread_mutex_unlock(&g_em) == 0);
    CHECK(pthread_mutex_unlock(&g_em) == EPERM);    /* 이미 풀린 락을 또 해제 */
    pthread_mutex_destroy(&g_em);

    /* 연습 4 */
    sc_init(&g_sc, NT, 1000);
    for (int i = 0; i < NT; i++) pthread_create(&th[i], NULL, sc_worker, (void *)(intptr_t)i);
    for (int i = 0; i < NT; i++) pthread_join(th[i], NULL);
    long total = (long)NT * ITERS + (0 + 1 + 2 + 3);
    CHECK(sc_get(&g_sc) == total);                   /* get_exact 없이도 정확 */
    int left = 0;
    for (int i = 0; i < NT; i++) left += g_sc.local[i].count != 0;
    CHECK(left == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
