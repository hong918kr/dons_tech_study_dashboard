/* DS-A03 추가 연습 — 한 번에 검증
 *
 *   연습 1  mpmc_size_approx: 동시에 바뀌는 두 위치로 "대략 크기" — 왜 clamp 가 필요한가
 *   연습 2  mpmc_init_at: 위치를 SIZE_MAX 근처에서 시작 → wrap 을 여러 번 통과해도 FIFO · 정확히 한 번
 *   연습 3  ticket 판 (rigtorp/MPMCQueue 의 push/pop 모양): CAS 대신 fetch_add 로 위치를 받고 슬롯 차례를 기다림
 *   연습 4  32 B 레코드 페이로드 (memcpy, 비원자) — seq 의 release/acquire 만으로 찢어진 레코드 0
 *
 * 스트레스 크기: 기본 생산자당 200000, ASan/TSan 빌드는 1/10 (DS_A03_MSGS 로 변경 가능).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr
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
#include <time.h>

static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

#if defined(__has_feature)
#  if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
#    define DS_SANITIZED 1
#  endif
#endif
#ifndef DS_SANITIZED
#  define DS_SANITIZED 0
#endif
_Static_assert(sizeof(size_t) == sizeof(intptr_t), "size_t and intptr_t must have the same width");

static inline void cpu_relax(void) {
#if defined(__aarch64__) || defined(__arm__)
    __asm__ volatile("yield" ::: "memory");
#endif
}
static double now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (double)ts.tv_sec + ts.tv_nsec * 1e-9; }
static void backoff(unsigned *f) { if ((++*f & 63u) == 0) sched_yield(); else cpu_relax(); }

static uint32_t stress_msgs(void) {
    const char *s = getenv("DS_A03_MSGS");
    if (s && *s) { long v = strtol(s, NULL, 10); if (v > 0 && v < 50000000) return (uint32_t)v; }
    return DS_SANITIZED ? 20000u : 200000u;
}

/* ---------------- 본문의 Vyukov (solution.c 와 같음, 훅 없음) ---------------- */
#define LINE 128
typedef struct { atomic_size_t seq; uint64_t data; } cell_t;
typedef struct {
    cell_t *buf;
    size_t mask;
    _Alignas(LINE) atomic_size_t enq_pos;
    _Alignas(LINE) atomic_size_t deq_pos;
    char pad_[LINE - sizeof(atomic_size_t)];
} mpmc_t;

static bool mpmc_try_enqueue(mpmc_t *q, uint64_t v) {
    size_t pos = atomic_load_explicit(&q->enq_pos, memory_order_relaxed);
    cell_t *c;
    for (;;) {
        c = &q->buf[pos & q->mask];
        intptr_t dif = (intptr_t)(atomic_load_explicit(&c->seq, memory_order_acquire) - pos);
        if (dif == 0) {
            if (atomic_compare_exchange_weak_explicit(&q->enq_pos, &pos, pos + 1, memory_order_relaxed, memory_order_relaxed)) break;
        } else if (dif < 0) return false;
        else pos = atomic_load_explicit(&q->enq_pos, memory_order_relaxed);
    }
    c->data = v;
    atomic_store_explicit(&c->seq, pos + 1, memory_order_release);
    return true;
}
static bool mpmc_try_dequeue(mpmc_t *q, uint64_t *out) {
    size_t pos = atomic_load_explicit(&q->deq_pos, memory_order_relaxed);
    cell_t *c;
    for (;;) {
        c = &q->buf[pos & q->mask];
        intptr_t dif = (intptr_t)(atomic_load_explicit(&c->seq, memory_order_acquire) - (pos + 1));
        if (dif == 0) {
            if (atomic_compare_exchange_weak_explicit(&q->deq_pos, &pos, pos + 1, memory_order_relaxed, memory_order_relaxed)) break;
        } else if (dif < 0) return false;
        else pos = atomic_load_explicit(&q->deq_pos, memory_order_relaxed);
    }
    *out = c->data;
    atomic_store_explicit(&c->seq, pos + q->mask + 1, memory_order_release);
    return true;
}

/* ======================= 연습 2 — 임의 시작 위치 ======================= */
/* 슬롯 i 의 seq = start 이상에서 처음으로 i 에 대응하는 위치 = start + ((i - start) & mask). */
static bool mpmc_init_at(mpmc_t *q, cell_t *cells, size_t cap, size_t start) {
    if (cap < 2 || (cap & (cap - 1)) != 0) return false;
    q->buf = cells;
    q->mask = cap - 1;
    for (size_t i = 0; i < cap; i++) {
        atomic_init(&cells[i].seq, start + ((i - start) & q->mask));
        cells[i].data = 0;
    }
    atomic_init(&q->enq_pos, start);
    atomic_init(&q->deq_pos, start);
    return true;
}
static bool mpmc_init(mpmc_t *q, cell_t *cells, size_t cap) { return mpmc_init_at(q, cells, cap, 0); }

/* ======================= 연습 1 — 대략 크기 ======================= */
/* deq 를 먼저, enq 를 나중에 읽는다. 둘 사이에 다른 스레드가 움직이므로 결과는 "그 무렵" 값일 뿐이고,
 * 부호 있는 차이가 음수(소비자가 앞서 보임)나 cap 초과로 나올 수 있다 → [0, cap] 로 자른다. */
static size_t mpmc_size_approx(mpmc_t *q, intptr_t *raw) {
    size_t d = atomic_load_explicit(&q->deq_pos, memory_order_relaxed);
    size_t e = atomic_load_explicit(&q->enq_pos, memory_order_relaxed);
    intptr_t diff = (intptr_t)(e - d);
    if (raw) *raw = diff;
    if (diff < 0) return 0;
    if ((size_t)diff > q->mask + 1) return q->mask + 1;
    return (size_t)diff;
}

/* ======================= 연습 3 — ticket 판 (blocking) ======================= */
/* 위치를 fetch_add 로 "무조건" 받는다 → CAS 재시도가 없다. 대신 받은 순간 그 슬롯 차례가 올 때까지 기다려야
 * 한다 (try 가 불가능). abort 는 테스트가 멈추지 않게 하는 안전장치. */
static atomic_bool g_abort;
static bool tq_push(mpmc_t *q, uint64_t v) {
    size_t pos = atomic_fetch_add_explicit(&q->enq_pos, 1, memory_order_relaxed);
    cell_t *c = &q->buf[pos & q->mask];
    unsigned f = 0;
    while (atomic_load_explicit(&c->seq, memory_order_acquire) != pos) {
        if (atomic_load_explicit(&g_abort, memory_order_relaxed)) return false;
        backoff(&f);
    }
    c->data = v;
    atomic_store_explicit(&c->seq, pos + 1, memory_order_release);
    return true;
}
static bool tq_pop(mpmc_t *q, uint64_t *out) {
    size_t pos = atomic_fetch_add_explicit(&q->deq_pos, 1, memory_order_relaxed);
    cell_t *c = &q->buf[pos & q->mask];
    unsigned f = 0;
    while (atomic_load_explicit(&c->seq, memory_order_acquire) != pos + 1) {
        if (atomic_load_explicit(&g_abort, memory_order_relaxed)) return false;
        backoff(&f);
    }
    *out = c->data;
    atomic_store_explicit(&c->seq, pos + q->mask + 1, memory_order_release);
    return true;
}

/* ======================= 연습 4 — 32 B 레코드 ======================= */
typedef struct { uint32_t producer, seqno; uint64_t ts; uint32_t payload[3]; uint32_t sum; } rec_t;   /* 32 B */
_Static_assert(sizeof(rec_t) == 32, "rec_t is 32 bytes");
typedef struct { atomic_size_t seq; rec_t r; } rcell_t;
typedef struct {
    rcell_t *buf;
    size_t mask;
    _Alignas(LINE) atomic_size_t enq_pos;
    _Alignas(LINE) atomic_size_t deq_pos;
    char pad_[LINE - sizeof(atomic_size_t)];
} rq_t;
static uint32_t rec_sum(const rec_t *r) {
    uint32_t h = 2166136261u;
    const uint32_t w[6] = { r->producer, r->seqno, (uint32_t)r->ts, (uint32_t)(r->ts >> 32), r->payload[0], r->payload[1] };
    for (int k = 0; k < 6; k++) h = (h ^ w[k]) * 16777619u;
    return (h ^ r->payload[2]) * 16777619u;
}
static void rq_init(rq_t *q, rcell_t *cells, size_t cap) {
    q->buf = cells; q->mask = cap - 1;
    for (size_t i = 0; i < cap; i++) { atomic_init(&cells[i].seq, i); memset(&cells[i].r, 0, sizeof cells[i].r); }
    atomic_init(&q->enq_pos, 0); atomic_init(&q->deq_pos, 0);
}
static bool rq_try_enqueue(rq_t *q, const rec_t *r) {
    size_t pos = atomic_load_explicit(&q->enq_pos, memory_order_relaxed);
    rcell_t *c;
    for (;;) {
        c = &q->buf[pos & q->mask];
        intptr_t dif = (intptr_t)(atomic_load_explicit(&c->seq, memory_order_acquire) - pos);
        if (dif == 0) {
            if (atomic_compare_exchange_weak_explicit(&q->enq_pos, &pos, pos + 1, memory_order_relaxed, memory_order_relaxed)) break;
        } else if (dif < 0) return false;
        else pos = atomic_load_explicit(&q->enq_pos, memory_order_relaxed);
    }
    memcpy(&c->r, r, sizeof *r);                    /* 비원자 복사 — 이 슬롯은 지금 나만 만진다 */
    atomic_store_explicit(&c->seq, pos + 1, memory_order_release);
    return true;
}
static bool rq_try_dequeue(rq_t *q, rec_t *out) {
    size_t pos = atomic_load_explicit(&q->deq_pos, memory_order_relaxed);
    rcell_t *c;
    for (;;) {
        c = &q->buf[pos & q->mask];
        intptr_t dif = (intptr_t)(atomic_load_explicit(&c->seq, memory_order_acquire) - (pos + 1));
        if (dif == 0) {
            if (atomic_compare_exchange_weak_explicit(&q->deq_pos, &pos, pos + 1, memory_order_relaxed, memory_order_relaxed)) break;
        } else if (dif < 0) return false;
        else pos = atomic_load_explicit(&q->deq_pos, memory_order_relaxed);
    }
    memcpy(out, &c->r, sizeof *out);
    atomic_store_explicit(&c->seq, pos + q->mask + 1, memory_order_release);
    return true;
}

/* ======================= 공통 스트레스 하네스 ======================= */
enum { MODE_TRY, MODE_TICKET, MODE_REC };
typedef struct {
    int mode;
    mpmc_t *q;
    rq_t *rq;
    unsigned id, nprod;
    uint32_t per;
    atomic_size_t *consumed;
    _Atomic unsigned char *hits;
    size_t quota;                    /* ticket 판: 소비자마다 정확히 이만큼 pop */
    size_t order_bad, torn, got;
    atomic_int *size_bad;            /* 연습 1: clamp 결과가 [0, cap] 밖 (0 이어야 함) */
    atomic_int *raw_out;             /* 연습 1: raw 차이가 범위 밖이었던 횟수 (관찰용) */
    double deadline;
} sarg_t;

static void *s_producer(void *p) {
    sarg_t *a = p;
    for (uint32_t i = 0; i < a->per; i++) {
        uint64_t v = ((uint64_t)a->id << 32) | i;
        unsigned f = 0;
        if (a->mode == MODE_TICKET) { if (!tq_push(a->q, v)) return NULL; continue; }
        if (a->mode == MODE_REC) {
            rec_t r = { a->id, i, (uint64_t)i * 1000003u, { i ^ 0x5A5A5A5Au, ~i, i * 7u }, 0 };
            r.sum = rec_sum(&r);
            while (!rq_try_enqueue(a->rq, &r)) { if (atomic_load(&g_abort)) return NULL; backoff(&f); }
            continue;
        }
        while (!mpmc_try_enqueue(a->q, v)) {
            if (atomic_load_explicit(&g_abort, memory_order_relaxed)) return NULL;
            backoff(&f);
            if ((f & 1023u) == 0 && now_s() > a->deadline) { atomic_store(&g_abort, true); return NULL; }
        }
        if (a->size_bad) {
            intptr_t raw;
            size_t s = mpmc_size_approx(a->q, &raw);
            if (s > a->q->mask + 1) atomic_fetch_add(a->size_bad, 1);
            if (raw < 0 || raw > (intptr_t)(a->q->mask + 1)) atomic_fetch_add(a->raw_out, 1);
        }
    }
    return NULL;
}
static void *s_consumer(void *p) {
    sarg_t *a = p;
    uint32_t last1[16] = {0};
    size_t total = (size_t)a->nprod * a->per;
    unsigned f = 0;
    for (;;) {
        uint64_t v;
        if (a->mode == MODE_TICKET) {
            if (a->got == a->quota || !tq_pop(a->q, &v)) break;
        } else if (a->mode == MODE_REC) {
            if (atomic_load_explicit(a->consumed, memory_order_relaxed) >= total) break;
            rec_t r;
            if (!rq_try_dequeue(a->rq, &r)) {
                if (atomic_load(&g_abort)) break;
                backoff(&f);
                if ((f & 1023u) == 0 && now_s() > a->deadline) { atomic_store(&g_abort, true); break; }
                continue;
            }
            if (r.sum != rec_sum(&r) || r.payload[1] != ~r.seqno) a->torn++;
            v = ((uint64_t)r.producer << 32) | r.seqno;
        } else {
            if (atomic_load_explicit(a->consumed, memory_order_relaxed) >= total) break;
            if (!mpmc_try_dequeue(a->q, &v)) {
                if (atomic_load(&g_abort)) break;
                backoff(&f);
                if ((f & 1023u) == 0 && now_s() > a->deadline) { atomic_store(&g_abort, true); break; }
                continue;
            }
        }
        atomic_fetch_add_explicit(a->consumed, 1, memory_order_relaxed);
        unsigned pr = (unsigned)(v >> 32);
        uint32_t i = (uint32_t)v;
        if (pr >= a->nprod || (uint64_t)i + 1u <= last1[pr]) a->order_bad++;
        else last1[pr] = i + 1u;
        if (pr < a->nprod && i < a->per) atomic_fetch_add_explicit(&a->hits[(size_t)pr * a->per + i], 1, memory_order_relaxed);
        a->got++;
        f = 0;
    }
    return NULL;
}
typedef struct { bool ok; size_t torn, order_bad, lost, dup; int size_bad, raw_out; } sres_t;
static sres_t stress(int mode, unsigned np, unsigned nc, size_t cap, size_t start, uint32_t per, bool size_probe) {
    sres_t r = {0};
    mpmc_t q; rq_t rq;
    cell_t *cells = calloc(cap, sizeof *cells);
    rcell_t *rcells = calloc(cap, sizeof *rcells);
    size_t n = (size_t)np * per;
    _Atomic unsigned char *hits = calloc(n, 1);
    atomic_size_t consumed; atomic_init(&consumed, 0);
    atomic_int size_bad, raw_out; atomic_init(&size_bad, 0); atomic_init(&raw_out, 0);
    atomic_store(&g_abort, false);
    mpmc_init_at(&q, cells, cap, start);
    rq_init(&rq, rcells, cap);
    pthread_t th[32]; sarg_t args[32];
    double dl = now_s() + 15.0;
    for (unsigned k = 0; k < np + nc; k++) {
        args[k] = (sarg_t){ .mode = mode, .q = &q, .rq = &rq, .id = k < np ? k : k - np, .nprod = np, .per = per,
                            .consumed = &consumed, .hits = hits, .quota = n / nc,
                            .size_bad = size_probe ? &size_bad : NULL, .raw_out = &raw_out, .deadline = dl };
        pthread_create(&th[k], NULL, k < np ? s_producer : s_consumer, &args[k]);
    }
    for (unsigned k = 0; k < np + nc; k++) pthread_join(th[k], NULL);
    size_t got = 0;
    for (unsigned k = np; k < np + nc; k++) { got += args[k].got; r.torn += args[k].torn; r.order_bad += args[k].order_bad; }
    for (size_t k = 0; k < n; k++) { unsigned h = atomic_load(&hits[k]); r.lost += h == 0; r.dup += h > 1; }
    r.size_bad = atomic_load(&size_bad); r.raw_out = atomic_load(&raw_out);
    size_t endpos = start + n;
    bool pos_ok = mode == MODE_REC ? atomic_load(&rq.enq_pos) == n && atomic_load(&rq.deq_pos) == n
                                   : atomic_load(&q.enq_pos) == endpos && atomic_load(&q.deq_pos) == endpos;
    r.ok = !atomic_load(&g_abort) && got == n && r.lost == 0 && r.dup == 0 && r.order_bad == 0 && r.torn == 0 && pos_ok;
    free((void *)hits); free(cells); free(rcells);
    return r;
}

int main(void) {
    uint32_t m = stress_msgs();

    printf("== 연습 1: mpmc_size_approx ==\n");
    {
        cell_t c[8]; mpmc_t q;
        mpmc_init(&q, c, 8);
        uint64_t v;
        CHECK(mpmc_size_approx(&q, NULL) == 0u);
        for (int k = 0; k < 5; k++) mpmc_try_enqueue(&q, (uint64_t)k);
        CHECK(mpmc_size_approx(&q, NULL) == 5u);
        mpmc_try_dequeue(&q, &v);
        CHECK(mpmc_size_approx(&q, NULL) == 4u);
        atomic_store(&q.deq_pos, 7);                       /* "소비자가 앞서 보이는" 순간을 흉내: raw = -2 */
        intptr_t raw;
        CHECK(mpmc_size_approx(&q, &raw) == 0u && raw == -2);
        atomic_store(&q.deq_pos, 1); atomic_store(&q.enq_pos, 20);   /* raw = 19 > cap */
        CHECK(mpmc_size_approx(&q, &raw) == 8u && raw == 19);
        sres_t r = stress(MODE_TRY, 4, 4, 16, 0, m, true);
        printf("  4x4 cap 16 with size probes: raw diff outside [0,cap] seen %d times (observation only)\n", r.raw_out);
        CHECK(r.ok && r.size_bad == 0);
    }

    printf("== 연습 2: mpmc_init_at (wrap through SIZE_MAX) ==\n");
    {
        cell_t c[4]; mpmc_t q;
        size_t start = SIZE_MAX - 5;
        CHECK(mpmc_init_at(&q, c, 4, start));
        CHECK(atomic_load(&c[(start) & 3].seq) == start && atomic_load(&c[(start + 3) & 3].seq) == start + 3);
        bool ok = true;
        uint64_t in = 0, out = 0, v;
        for (int round = 0; ok && round < 5; round++) {          /* 5 바퀴 = 20 위치, SIZE_MAX→0 통과 */
            for (int k = 0; k < 4; k++) ok = ok && mpmc_try_enqueue(&q, in++);
            ok = ok && !mpmc_try_enqueue(&q, 999);
            for (int k = 0; k < 4; k++) ok = ok && mpmc_try_dequeue(&q, &v) && v == out++;
            ok = ok && !mpmc_try_dequeue(&q, &v);
        }
        CHECK(ok && atomic_load(&q.enq_pos) == start + 20 && atomic_load(&q.enq_pos) == 14u);
        sres_t r = stress(MODE_TRY, 4, 4, 8, SIZE_MAX - 1000, m, false);
        CHECK(r.ok);
    }

    printf("== 연습 3: ticket (fetch_add) MPMC ==\n");
    {
        cell_t c[4]; mpmc_t q;
        mpmc_init(&q, c, 4);
        uint64_t v = 0;
        atomic_store(&g_abort, false);
        CHECK(tq_push(&q, 7) && tq_push(&q, 8) && tq_pop(&q, &v) && v == 7u && tq_pop(&q, &v) && v == 8u);
        sres_t r = stress(MODE_TICKET, 4, 4, 1024, 0, m, false);
        CHECK(r.ok);
        r = stress(MODE_TICKET, 4, 4, 2, 0, m / 4u, false);     /* 2 칸: 대부분이 차례를 기다림 */
        CHECK(r.ok);
    }

    printf("== 연습 4: 32-byte records (non-atomic memcpy payload) ==\n");
    {
        sres_t r = stress(MODE_REC, 4, 4, 64, 0, m, false);
        printf("  torn %zu, order_bad %zu, lost %zu, dup %zu\n", r.torn, r.order_bad, r.lost, r.dup);
        CHECK(r.ok && r.torn == 0u);
    }

    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
