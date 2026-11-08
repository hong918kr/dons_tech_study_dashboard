/* OS-A03 추가 연습 — 한 번에 검증
 *
 *   연습 1  읽기 구간 중첩 (rcu_read_lock 은 중첩된다): 깊이 카운터, 바깥 구간만 슬롯을 바꾼다
 *   연습 2  리더 동적 등록: 슬롯을 CAS 로 차지/반납 (스레드가 생겼다 없어지는 서버)
 *   연습 3  QSBR offline/online: 오래 잠드는 태스크가 grace period 를 막지 않게 — online 은 SB 모양이라 seq_cst
 *   연습 4  고전 3-bucket EBR (limbo 목록 3 개를 epoch % 3 로 돌려 쓰기) == 본문의 선형 limbo 와 같은 결과
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CL 128
#define MAX_READERS 16
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t xorshift32(uint32_t *s) { *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5; return *s; }

/* ---- 본문 Part A 의 EBR 핵심 (solution.c 와 같은 규칙) ---- */
typedef struct { _Alignas(CL) _Atomic uint64_t word; } ebr_slot_t;
typedef struct {
    _Alignas(CL) _Atomic uint64_t epoch;
    ebr_slot_t slot[MAX_READERS];
    int nslots;
} ebr_t;
static void ebr_init(ebr_t *d, int n) {
    atomic_init(&d->epoch, 0);
    for (int i = 0; i < MAX_READERS; i++) atomic_init(&d->slot[i].word, 0);
    d->nslots = n;
}
static void ebr_enter(ebr_t *d, int r) {
    uint64_t e = atomic_load_explicit(&d->epoch, memory_order_relaxed);
    atomic_store_explicit(&d->slot[r].word, (e << 1) | 1u, memory_order_seq_cst);
}
static void ebr_exit(ebr_t *d, int r) { atomic_store_explicit(&d->slot[r].word, 0, memory_order_release); }
static bool ebr_try_advance(ebr_t *d) {
    uint64_t g = atomic_load_explicit(&d->epoch, memory_order_seq_cst);
    for (int r = 0; r < d->nslots; r++) {
        uint64_t w = atomic_load_explicit(&d->slot[r].word, memory_order_seq_cst);
        if ((w & 1u) && (w >> 1) != g) return false;
    }
    return atomic_compare_exchange_strong(&d->epoch, &g, g + 1);
}

/* ======================= 연습 1 — 중첩 ======================= */
typedef struct { _Alignas(CL) int depth; } nest_t;    /* 리더 자기만 쓰는 깊이 (원자일 필요 없음) */
static nest_t g_nest[MAX_READERS];
static void ebrn_enter(ebr_t *d, int r) { if (g_nest[r].depth++ == 0) ebr_enter(d, r); }
static void ebrn_exit(ebr_t *d, int r) { if (--g_nest[r].depth == 0) ebr_exit(d, r); }

static void test1(void) {
    ebr_t d;
    ebr_init(&d, 2);
    ebrn_enter(&d, 0);                       /* 바깥 */
    ebrn_enter(&d, 0);                       /* 안쪽 (예: 콜백 안에서 또 설정을 읽음) */
    CHECK(ebr_try_advance(&d));              /* 0 → 1: 리더는 0 에 있었다 */
    ebrn_exit(&d, 0);                        /* 안쪽만 나감 — 여전히 구간 안 */
    CHECK(atomic_load(&d.slot[0].word) == 1u && !ebr_try_advance(&d));
    ebrn_exit(&d, 0);
    CHECK(atomic_load(&d.slot[0].word) == 0u && ebr_try_advance(&d) && g_nest[0].depth == 0);
}

/* ======================= 연습 2 — 동적 등록 ======================= */
typedef struct { _Alignas(CL) _Atomic int owner; _Atomic int inside; } own_t;   /* owner: 0 = 빈 슬롯 */
static own_t g_own[MAX_READERS];
static int ebr_register(ebr_t *d, int me) {           /* me >= 1. 차지한 슬롯 번호, 없으면 -1 */
    for (int r = 0; r < d->nslots; r++) {
        int z = 0;
        if (atomic_compare_exchange_strong_explicit(&g_own[r].owner, &z, me,
                                                    memory_order_acquire, memory_order_relaxed)) return r;
    }
    return -1;
}
static void ebr_unregister(ebr_t *d, int r) {          /* 구간 밖에서만 부른다 (슬롯 word == 0) */
    (void)d;
    atomic_store_explicit(&g_own[r].owner, 0, memory_order_release);
}
#define T2_THREADS 8
#define T2_ITERS 3000
static ebr_t g_d2;
static atomic_long g_t2_ok, g_t2_full, g_t2_bad;
static void *t2_worker(void *arg) {
    int me = (int)(intptr_t)arg;
    long ok = 0, full = 0, bad = 0;
    for (int i = 0; i < T2_ITERS; i++) {
        int r = ebr_register(&g_d2, me);
        if (r < 0) { full++; sched_yield(); continue; }
        if (atomic_fetch_add(&g_own[r].inside, 1) != 0) bad++;     /* 같은 슬롯을 두 명이 쥐면 여기서 걸린다 */
        if (atomic_load(&g_own[r].owner) != me) bad++;
        ebr_enter(&g_d2, r);
        ebr_exit(&g_d2, r);
        atomic_fetch_sub(&g_own[r].inside, 1);
        ebr_unregister(&g_d2, r);
        ok++;
    }
    atomic_fetch_add(&g_t2_ok, ok); atomic_fetch_add(&g_t2_full, full); atomic_fetch_add(&g_t2_bad, bad);
    return NULL;
}
static void test2(void) {
    ebr_init(&g_d2, 3);                                   /* 슬롯 3 개를 8 스레드가 돌려 쓴다 */
    CHECK(ebr_register(&g_d2, 1) == 0 && ebr_register(&g_d2, 2) == 1 && ebr_register(&g_d2, 3) == 2);
    CHECK(ebr_register(&g_d2, 4) == -1);                  /* 꽉 참 */
    ebr_unregister(&g_d2, 1);
    CHECK(ebr_register(&g_d2, 5) == 1);
    for (int r = 0; r < 3; r++) ebr_unregister(&g_d2, r);
    pthread_t th[T2_THREADS];
    for (int t = 0; t < T2_THREADS; t++) pthread_create(&th[t], NULL, t2_worker, (void *)(intptr_t)(t + 1));
    for (int t = 0; t < T2_THREADS; t++) pthread_join(th[t], NULL);
    CHECK(atomic_load(&g_t2_bad) == 0);
    CHECK(atomic_load(&g_t2_ok) + atomic_load(&g_t2_full) == (long)T2_THREADS * T2_ITERS);
    CHECK(ebr_try_advance(&g_d2));                        /* 모두 나갔으니 막는 리더 없음 */
}

/* ======================= 연습 3 — QSBR offline/online ======================= */
#define QOFF UINT64_MAX
#define NPOOL 3
enum { BUF_FREE, BUF_LIVE, BUF_RETIRED };
typedef struct { uint64_t version; uint32_t k[8]; } tab_t;
typedef struct { _Alignas(CL) _Atomic uint64_t seen; } qtask_t;
typedef struct {
    tab_t buf[NPOOL];
    int state[NPOOL];
    uint64_t retired_gp[NPOOL];
    _Alignas(CL) _Atomic(const tab_t *) cur;
    _Alignas(CL) _Atomic uint64_t gp;
    qtask_t task[4];
    int ntask;
} qs_t;
static void qs_init(qs_t *q, int n) {
    memset(q->buf, 0, sizeof q->buf);
    for (int i = 0; i < NPOOL; i++) q->state[i] = BUF_FREE;
    q->buf[0].version = 1; q->state[0] = BUF_LIVE;
    atomic_init(&q->cur, &q->buf[0]);
    atomic_init(&q->gp, 1);
    for (int t = 0; t < 4; t++) atomic_init(&q->task[t].seen, 1);
    q->ntask = n;
}
static const tab_t *qs_get(qs_t *q) { return atomic_load_explicit(&q->cur, memory_order_acquire); }
static void qs_quiescent(qs_t *q, int t) {
    atomic_store_explicit(&q->task[t].seen, atomic_load_explicit(&q->gp, memory_order_acquire), memory_order_release);
}
static void qs_offline(qs_t *q, int t) {                 /* "이제부터 테이블을 안 본다" (잠들기 직전) */
    atomic_store_explicit(&q->task[t].seen, QOFF, memory_order_release);
}
static void qs_online(qs_t *q, int t) {                  /* 깨어나서 다시 보기 시작 */
    /* store(seen) → load(cur) 순서가 writer 의 exchange → load(seen) 과 SB 모양이다. seq_cst 로 막는다. */
    atomic_store_explicit(&q->task[t].seen, atomic_load_explicit(&q->gp, memory_order_seq_cst), memory_order_seq_cst);
    atomic_thread_fence(memory_order_seq_cst);
}
static int qs_poll(qs_t *q) {
    atomic_thread_fence(memory_order_seq_cst);           /* writer 쪽 SB 짝 */
    uint64_t min = UINT64_MAX;
    for (int t = 0; t < q->ntask; t++) {
        uint64_t s = atomic_load_explicit(&q->task[t].seen, memory_order_acquire);
        if (s < min) min = s;
    }
    int n = 0;
    for (int i = 0; i < NPOOL; i++)
        if (q->state[i] == BUF_RETIRED && q->retired_gp[i] <= min) { q->state[i] = BUF_FREE; n++; }
    return n;
}
static int qs_publish(qs_t *q, uint64_t v) {
    qs_poll(q);
    int i = 0;
    while (i < NPOOL && q->state[i] != BUF_FREE) i++;
    if (i == NPOOL) return -1;
    q->buf[i].version = v;
    for (int j = 0; j < 8; j++) q->buf[i].k[j] = (uint32_t)v * 3u + (uint32_t)j;
    q->state[i] = BUF_LIVE;
    const tab_t *old = atomic_exchange_explicit(&q->cur, &q->buf[i], memory_order_seq_cst);
    uint64_t g = atomic_fetch_add_explicit(&q->gp, 1, memory_order_seq_cst) + 1;
    int oi = (int)(old - q->buf);
    q->state[oi] = BUF_RETIRED;
    q->retired_gp[oi] = g;
    return 0;
}
static void test3(void) {
    qs_t q;
    qs_init(&q, 3);
    qs_offline(&q, 2);                                    /* 태스크 2: 큐에서 블록될 예정 → 먼저 offline */
    for (uint64_t v = 2; v < 12; v++) {                   /* 태스크 0,1 만 루프를 돈다 */
        int rc = qs_publish(&q, v);
        qs_quiescent(&q, 0); qs_quiescent(&q, 1);
        if (rc != 0) { CHECK(rc == 0); break; }           /* offline 태스크가 막으면 실패 */
    }
    CHECK(qs_get(&q)->version == 11);
    qs_online(&q, 2);                                     /* 깨어남: 지금 gp 부터 참여 */
    const tab_t *t2 = qs_get(&q);
    CHECK(t2->version == 11);
    CHECK(qs_publish(&q, 12) == 0);
    qs_quiescent(&q, 0); qs_quiescent(&q, 1);
    CHECK(qs_publish(&q, 13) == 0 && qs_publish(&q, 14) == -1);   /* 이제 태스크 2 가 v11 버퍼를 묶는다 */
    CHECK(t2->version == 11);                             /* 쥔 버퍼는 그대로 */
    qs_quiescent(&q, 2);
    CHECK(qs_publish(&q, 14) == 0);
}

/* ======================= 연습 4 — 3-bucket EBR == 선형 limbo ======================= */
#define B_CAP 256
typedef struct { int id; uint64_t epoch; } item_t;
typedef struct { item_t it[B_CAP]; int n; } lin_t;                 /* 본문 방식 */
typedef struct { int ids[3][B_CAP]; int n[3]; } bucket3_t;          /* 고전 방식 */
static uint8_t g_freed_lin[4096], g_freed_b3[4096];
static void lin_retire(lin_t *l, int id, uint64_t g) { l->it[l->n].id = id; l->it[l->n].epoch = g; l->n++; }
static void lin_reclaim(lin_t *l, uint64_t g) {
    int k = 0;
    for (int i = 0; i < l->n; i++) { if (l->it[i].epoch + 2 <= g) g_freed_lin[l->it[i].id] = 1; else l->it[k++] = l->it[i]; }
    l->n = k;
}
static void b3_retire(bucket3_t *b, int id, uint64_t g) { int k = (int)(g % 3); b->ids[k][b->n[k]++] = id; }
static void b3_on_advance(bucket3_t *b, uint64_t newg) {           /* epoch 이 newg 로 올라간 직후 */
    int k = (int)((newg + 1) % 3);                                  /* = (newg - 2) % 3: 2 칸 전 epoch 의 목록 */
    for (int i = 0; i < b->n[k]; i++) g_freed_b3[b->ids[k][i]] = 1;
    b->n[k] = 0;
}
static void test4(void) {
    ebr_t d;
    ebr_init(&d, 3);
    static lin_t lin;
    static bucket3_t b3;
    uint32_t seed = 0xC0FFEEu;
    int next_id = 0, mismatch = 0, adv = 0;
    for (int step = 0; step < 3000 && next_id < 4000; step++) {
        uint32_t x = xorshift32(&seed) % 8u;
        int r = (int)(xorshift32(&seed) % 3u);
        if (x < 2) ebr_enter(&d, r);                                   /* 무작위 강제 스케줄 */
        else if (x < 4) ebr_exit(&d, r);
        else if (x < 6) { uint64_t g = atomic_load(&d.epoch); lin_retire(&lin, next_id, g); b3_retire(&b3, next_id, g); next_id++; }
        else if (ebr_try_advance(&d)) { uint64_t g = atomic_load(&d.epoch); lin_reclaim(&lin, g); b3_on_advance(&b3, g); adv++; }
        for (int i = 0; i < next_id; i++) mismatch += g_freed_lin[i] != g_freed_b3[i];
    }
    CHECK(mismatch == 0 && adv > 100 && next_id > 500);              /* 매 단계 해제 집합이 같다 */
}

int main(void) {
    test1();
    test2();
    test3();
    test4();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
