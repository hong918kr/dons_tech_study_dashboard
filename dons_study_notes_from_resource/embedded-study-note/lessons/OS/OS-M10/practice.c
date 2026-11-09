/* OS-M10 추가 연습 — 한 번에 검증
 *   연습 1  lock_pair: 주소 순서로 두 뮤텍스 잡기 (OSTEP 32 "lock ordering by address") + 반대 방향 송금 스트레스
 *   연습 2  wfg_stuck: 순환에 "매달린" 스레드까지 전부 찾기 (순환 위 + 순환을 기다리는 꼬리)
 *   연습 3  mutex_timedlock_poll: 맥에 없는 pthread_mutex_timedlock 을 trylock + 지수 sleep 으로
 *   연습 4  order_acyclic: 철학자별 (첫 포크 → 둘째 포크) 간선 그래프가 비순환인지 → naive / ordered / 홀짝 / Dijkstra 비대칭
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec * 1e-6;
}
static void sleep_us(unsigned us) {
    struct timespec ts = { (time_t)(us / 1000000u), (long)(us % 1000000u) * 1000L };
    nanosleep(&ts, NULL);
}

/* ---------- 연습 1: 주소 순서 ---------- */
static void pair_order(pthread_mutex_t *a, pthread_mutex_t *b, pthread_mutex_t **first, pthread_mutex_t **second) {
    if ((uintptr_t)a < (uintptr_t)b) { *first = a; *second = b; }
    else { *first = b; *second = a; }
}
static void lock_pair(pthread_mutex_t *a, pthread_mutex_t *b) {
    if (a == b) { pthread_mutex_lock(a); return; }       /* 같은 계좌: 한 번만 (두 번이면 자기 교착) */
    pthread_mutex_t *f, *s;
    pair_order(a, b, &f, &s);
    pthread_mutex_lock(f);
    pthread_mutex_lock(s);
}
static void unlock_pair(pthread_mutex_t *a, pthread_mutex_t *b) {
    pthread_mutex_unlock(a);
    if (a != b) pthread_mutex_unlock(b);
}
typedef struct { pthread_mutex_t m; long balance; } account_t;
static account_t acct[2];
static atomic_int transfer_done;
typedef struct { int from, to; long amount; int iters; } xfer_arg_t;
static void *xfer_main(void *arg) {
    xfer_arg_t *x = arg;
    for (int i = 0; i < x->iters; i++) {
        lock_pair(&acct[x->from].m, &acct[x->to].m);
        acct[x->from].balance -= x->amount;
        acct[x->to].balance += x->amount;
        unlock_pair(&acct[x->from].m, &acct[x->to].m);
    }
    atomic_fetch_add(&transfer_done, 1);
    return NULL;
}

/* ---------- 연습 2: 교착에 휘말린 스레드 전부 ---------- */
/* holder[f] = 포크 f 를 쥔 스레드(-1 없음), wants[p] = p 가 기다리는 포크(-1 없음).
 * stuck[p] = 1  ⟺  p 에서 "기다림" 을 따라가면 순환에 닿는다 (순환 위든, 순환을 기다리든 영원히 못 깬다). */
static int wfg_stuck(int n, const int holder[], const int wants[], int stuck[]) {
    int color[16] = { 0 };                       /* 0 = 미확인, 1 = 지금 경로 위, 2 = 끝남 */
    for (int i = 0; i < n; i++) stuck[i] = 0;
    for (int s = 0; s < n; s++) {
        if (color[s]) continue;
        int path[16], len = 0, p = s, verdict = 0;
        while (p >= 0 && color[p] == 0) {
            color[p] = 1; path[len++] = p;
            p = wants[p] >= 0 ? holder[wants[p]] : -1;
        }
        if (p >= 0) verdict = (color[p] == 1) ? 1 : stuck[p];   /* 1 = 방금 순환을 닫음, 2 = 이미 판정된 노드 */
        for (int i = 0; i < len; i++) { color[path[i]] = 2; stuck[path[i]] = verdict; }
    }
    int cnt = 0;
    for (int i = 0; i < n; i++) cnt += stuck[i];
    return cnt;
}

/* ---------- 연습 3: 시간 제한 뮤텍스 ---------- */
static int mutex_timedlock_poll(pthread_mutex_t *m, int timeout_ms) {
    double t0 = now_ms();
    unsigned nap = 1;                                     /* µs, 2 배씩, 1 ms 에서 멈춤 */
    for (;;) {
        int rc = pthread_mutex_trylock(m);
        if (rc == 0) return 0;
        if (rc != EBUSY) return rc;
        if (now_ms() - t0 >= (double)timeout_ms) return ETIMEDOUT;
        sleep_us(nap);
        nap = nap >= 500u ? 1000u : nap * 2u;
    }
}
static pthread_mutex_t g_m3 = PTHREAD_MUTEX_INITIALIZER;
static void *hold_for(void *arg) {
    int ms = *(int *)arg;
    pthread_mutex_lock(&g_m3);
    sleep_us((unsigned)ms * 1000u);
    pthread_mutex_unlock(&g_m3);
    return NULL;
}
static atomic_int g_holding;
static void *hold_signal(void *arg) {
    int ms = *(int *)arg;
    pthread_mutex_lock(&g_m3);
    atomic_store(&g_holding, 1);
    sleep_us((unsigned)ms * 1000u);
    pthread_mutex_unlock(&g_m3);
    return NULL;
}

/* ---------- 연습 4: 락 순서 그래프가 비순환인가 ---------- */
static bool dfs_cycle(int u, int n, const int first[], const int second[], int color[]) {
    color[u] = 1;
    for (int p = 0; p < n; p++) {                        /* 간선 first[p] → second[p] */
        if (first[p] != u) continue;
        int v = second[p];
        if (color[v] == 1) return true;
        if (color[v] == 0 && dfs_cycle(v, n, first, second, color)) return true;
    }
    color[u] = 2;
    return false;
}
static bool order_acyclic(int n, const int first[], const int second[]) {
    int color[16] = { 0 };
    for (int u = 0; u < n; u++)
        if (color[u] == 0 && dfs_cycle(u, n, first, second, color)) return false;
    return true;
}
static void make_order(int kind, int n, int first[], int second[]) {   /* 0 naive, 1 ordered, 2 홀짝, 3 Dijkstra */
    for (int p = 0; p < n; p++) {
        int l = p, r = (p + 1) % n;
        switch (kind) {
        case 0: first[p] = l; second[p] = r; break;
        case 1: first[p] = l < r ? l : r; second[p] = l < r ? r : l; break;
        case 2: if (p % 2) { first[p] = l; second[p] = r; } else { first[p] = r; second[p] = l; } break;
        default: if (p == n - 1) { first[p] = r; second[p] = l; } else { first[p] = l; second[p] = r; } break;
        }
    }
}

int main(void) {
    /* 연습 1 */
    for (int i = 0; i < 2; i++) { pthread_mutex_init(&acct[i].m, NULL); acct[i].balance = 1000000; }
    pthread_mutex_t *f1, *s1, *f2, *s2;
    pair_order(&acct[0].m, &acct[1].m, &f1, &s1);
    pair_order(&acct[1].m, &acct[0].m, &f2, &s2);
    CHECK(f1 == f2 && s1 == s2 && f1 != s1);             /* 인자 순서와 무관하게 같은 순서 */
    xfer_arg_t xa = { 0, 1, 3, 200000 }, xb = { 1, 0, 7, 200000 }, xc = { 0, 0, 1, 1000 };
    pthread_t ta, tb, tc;
    pthread_create(&ta, NULL, xfer_main, &xa);
    pthread_create(&tb, NULL, xfer_main, &xb);
    pthread_create(&tc, NULL, xfer_main, &xc);
    double t0 = now_ms();
    while (atomic_load(&transfer_done) < 3 && now_ms() - t0 < 10000) sleep_us(1000);
    if (atomic_load(&transfer_done) < 3) {                 /* 교착이면 join 하지 않고 실패로 끝낸다 */
        printf("FAIL lock_pair: transfers stuck after 10 s (deadlock?)\n");
        return 1;
    }
    pthread_join(ta, NULL); pthread_join(tb, NULL); pthread_join(tc, NULL);
    CHECK(acct[0].balance + acct[1].balance == 2000000);
    CHECK(acct[0].balance == 1000000 - 3 * 200000 + 7 * 200000);

    /* 연습 2 */
    int stuck[16];
    {   /* P0 → F1(P1) → F2(P2) → F1(P1): 순환 {P1,P2}, P0 은 꼬리, P3 는 무관 */
        int holder[4] = { 3, 1, 2, -1 }, wants[4] = { 1, 2, 1, -1 };
        CHECK(wfg_stuck(4, holder, wants, stuck) == 3 && stuck[0] && stuck[1] && stuck[2] && !stuck[3]);
    }
    {   /* 재귀 락: P0 이 쥔 F0 을 다시 기다림 = 길이 1 순환 */
        int holder[2] = { 0, -1 }, wants[2] = { 0, -1 };
        CHECK(wfg_stuck(2, holder, wants, stuck) == 1 && stuck[0]);
    }
    {   /* 사슬만: P0 → F1(P1) → F2(P2), P2 는 먹는 중 → 아무도 stuck 아님 */
        int holder[3] = { 0, 1, 2 }, wants[3] = { 1, 2, -1 };
        CHECK(wfg_stuck(3, holder, wants, stuck) == 0);
    }
    {   /* 5 명 고리 + 꼬리 2 명 (P5 → P0, P6 → P5) */
        int holder[7] = { 0, 1, 2, 3, 4, 5, 6 }, wants[7] = { 1, 2, 3, 4, 0, 0, 5 };
        CHECK(wfg_stuck(7, holder, wants, stuck) == 7);
    }

    /* 연습 3 */
    {
        int ms = 300;
        pthread_t th;
        atomic_store(&g_holding, 0);
        pthread_create(&th, NULL, hold_signal, &ms);
        while (!atomic_load(&g_holding)) sleep_us(100);
        double a = now_ms();
        int rc = mutex_timedlock_poll(&g_m3, 50);
        double el = now_ms() - a;
        CHECK(rc == ETIMEDOUT && el >= 50.0);               /* 남이 300 ms 쥠 → 50 ms 뒤 포기 */
        pthread_join(th, NULL);
        int ms2 = 20;
        pthread_create(&th, NULL, hold_for, &ms2);
        sleep_us(2000);
        rc = mutex_timedlock_poll(&g_m3, 2000);             /* 20 ms 뒤 풀림 → 잡는다 */
        CHECK(rc == 0);
        if (rc == 0) pthread_mutex_unlock(&g_m3);
        pthread_join(th, NULL);
        CHECK(mutex_timedlock_poll(&g_m3, 10) == 0);        /* 빈 락은 즉시 */
        pthread_mutex_unlock(&g_m3);
    }

    /* 연습 4 */
    int fi[16], se[16], fo[16], so[16];
    make_order(0, 5, fi, se);  CHECK(!order_acyclic(5, fi, se));    /* naive: F0→F1→…→F4→F0 */
    make_order(1, 5, fi, se);  CHECK(order_acyclic(5, fi, se));
    make_order(2, 5, fi, se);  CHECK(order_acyclic(5, fi, se));     /* 홀짝 */
    make_order(2, 12, fi, se); CHECK(order_acyclic(12, fi, se));
    bool same = true;
    for (int n = 2; n <= 16; n++) {                                  /* Dijkstra 비대칭 == 작은 번호 먼저 */
        make_order(1, n, fo, so); make_order(3, n, fi, se);
        for (int p = 0; p < n; p++) if (fo[p] != fi[p] || so[p] != se[p]) same = false;
    }
    CHECK(same);

    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
