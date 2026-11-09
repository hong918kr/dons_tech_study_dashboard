/* DS-A04 Lock-free 스택과 ABA 문제
 *
 * 호스트 모델: pthread 하나 = 코어 하나 (또는 RTOS 태스크 / ISR). 노드는 고정 풀에서만 나온다 (malloc 없음).
 *   Part A  Treiber 스택 (top = 노드 인덱스 32비트) — push/pop CAS 루프, LIFO, 빈 스택
 *   Part B  ABA 강제 재현: T1 이 pop 의 "읽은 뒤 · CAS 전" 창(window)에서 멈춘 사이
 *           T2(main) 가 pop A, pop B, push A → T1 의 CAS 가 성공해 리스트가 깨진다 (무결성 검사기로 확인).
 *           수정 1: top = (32비트 세대 태그 << 32) | 32비트 인덱스 를 64비트 CAS 한 번으로.
 *   Part C  수정 2: {포인터, 태그} 16바이트를 128비트 CAS 로 — 패킷 버퍼 풀 (ISR 생산자 2 + 워커 6)
 *
 * 체크는 결정적인 것만: 강제 스케줄(훅 + mutex/cond 게이트)의 결과, 무결성 검사기, 정확한 개수,
 * 정확히 한 번 소비(bitmap), 동시 소유 0. 진짜 경합으로 깨지는 모습·처리량은 demo_*.c.
 * 새니타이저 빌드에서는 스트레스 반복 수를 1/SAN_SCALE 로 줄인다 (20 초 안에 끝나게).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

_Static_assert(ATOMIC_INT_LOCK_FREE == 2 && ATOMIC_LLONG_LOCK_FREE == 2, "32/64-bit atomics must be lock-free");

#if defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
#define SAN_SCALE 4u
#endif
#endif
#ifndef SAN_SCALE
#define SAN_SCALE 1u
#endif

static int g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) { g_pass++; printf("PASS %s\n", #e); } \
                      else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)

/* ---------- 강제 스케줄 훅 ----------
 * pop 은 "top 과 top->next 를 읽은 뒤, CAS 직전" 에 POP_WINDOW() 를 부른다. 보통은 NULL 이라 아무 일도 없다.
 * 테스트가 T1 스레드에만 훅을 걸어 그 자리에서 멈추게 한다 → ABA 창(window)을 결정적으로 연다. */
static _Thread_local void (*tl_pop_window)(void);
#define POP_WINDOW() do { if (tl_pop_window) tl_pop_window(); } while (0)

#define NIL 0xFFFFFFFFu
#define POOL_N 64u

/* 노드: next 는 "낡은 top" 을 든 다른 스레드가 동시에 읽을 수 있으므로 atomic(relaxed 로 충분).
 * value 는 소유자만 만진다 (pop 성공 ~ push 전) → 일반 변수. 그 사이 순서는 push release / pop acquire 가 만든다. */
typedef struct {
    _Atomic uint32_t next;
    uint32_t value;
} node_t;
static node_t g_pool[POOL_N];

/* ======================= Part A — Treiber 스택 (top = 인덱스) ======================= */
typedef struct { _Atomic uint32_t top; } istack_t;

static void is_init(istack_t *s) { atomic_init(&s->top, NIL); }

/* push: 노드 i 를 맨 위에 올린다. next 를 먼저 쓰고, CAS 성공(release)으로 게시한다. */
static void is_push(istack_t *s, uint32_t i) {
    uint32_t old = atomic_load_explicit(&s->top, memory_order_relaxed);
    do {
        atomic_store_explicit(&g_pool[i].next, old, memory_order_relaxed);
    } while (!atomic_compare_exchange_weak_explicit(&s->top, &old, i,
                                                    memory_order_release, memory_order_relaxed));
}

/* pop: 맨 위 노드 인덱스를 떼어 반환. 비었으면 NIL. top·next 를 읽은 뒤 CAS 직전에 POP_WINDOW(). */
static uint32_t is_pop(istack_t *s) {
    uint32_t old = atomic_load_explicit(&s->top, memory_order_acquire);
    while (old != NIL) {
        uint32_t next = atomic_load_explicit(&g_pool[old].next, memory_order_relaxed);
        POP_WINDOW();
        if (atomic_compare_exchange_weak_explicit(&s->top, &old, next,
                                                  memory_order_acquire, memory_order_acquire))
            return old;
    }
    return NIL;
}

/* ======================= Part B — 64비트 (태그, 인덱스) packing ======================= */
typedef struct { _Atomic uint64_t top; } tstack_t;

/* 상위 32비트 = 세대 태그(성공한 CAS 마다 +1), 하위 32비트 = 노드 인덱스 (NIL = 비었음). */
static uint64_t tag_pack(uint32_t idx, uint32_t tag) { return ((uint64_t)tag << 32) | idx; }
static uint32_t tag_idx(uint64_t w) { return (uint32_t)w; }
static uint32_t tag_tag(uint64_t w) { return (uint32_t)(w >> 32); }

static void ts_init(tstack_t *s) { atomic_init(&s->top, tag_pack(NIL, 0)); }

static void ts_push(tstack_t *s, uint32_t i) {
    uint64_t old = atomic_load_explicit(&s->top, memory_order_relaxed);
    uint64_t nw;
    do {
        atomic_store_explicit(&g_pool[i].next, tag_idx(old), memory_order_relaxed);
        nw = tag_pack(i, tag_tag(old) + 1u);              /* uint32_t 덧셈: 2^32 에서 0 으로 감김 (정의됨) */
    } while (!atomic_compare_exchange_weak_explicit(&s->top, &old, nw,
                                                    memory_order_release, memory_order_relaxed));
}

static uint32_t ts_pop(tstack_t *s) {
    uint64_t old = atomic_load_explicit(&s->top, memory_order_acquire);
    while (tag_idx(old) != NIL) {
        uint32_t next = atomic_load_explicit(&g_pool[tag_idx(old)].next, memory_order_relaxed);
        POP_WINDOW();
        uint64_t nw = tag_pack(next, tag_tag(old) + 1u);
        if (atomic_compare_exchange_weak_explicit(&s->top, &old, nw,
                                                  memory_order_acquire, memory_order_acquire))
            return tag_idx(old);
    }
    return NIL;
}

/* ======================= Part C — 128비트 {포인터, 태그} CAS: 패킷 버퍼 풀 ======================= */
#define PKT_N 64u
#define PKT_DATA 48u
typedef struct pkt {
    _Atomic(struct pkt *) next;
    _Atomic uint32_t owner;          /* 동시 소유 감지용 (relaxed exchange) */
    uint32_t seq, sum;
    uint8_t data[PKT_DATA];
} pkt_t;
static pkt_t g_pkt[PKT_N];

typedef struct { pkt_t *ptr; uintptr_t tag; } tagptr_t;       /* 패딩 없음: CAS 는 바이트 비교 */
_Static_assert(sizeof(tagptr_t) == 16, "pointer+tag must be exactly 16 bytes, no padding");
typedef struct { _Atomic tagptr_t top; } pstack_t;
_Static_assert(_Alignof(_Atomic tagptr_t) == 16, "16-byte CAS needs 16-byte alignment");

static void ps_init(pstack_t *s) { atomic_init(&s->top, ((tagptr_t){ NULL, 0 })); }

static void ps_push(pstack_t *s, pkt_t *p) {
    tagptr_t old = atomic_load_explicit(&s->top, memory_order_relaxed);
    tagptr_t nw;
    do {
        atomic_store_explicit(&p->next, old.ptr, memory_order_relaxed);
        nw = (tagptr_t){ p, old.tag + 1u };
    } while (!atomic_compare_exchange_weak_explicit(&s->top, &old, nw,
                                                    memory_order_release, memory_order_relaxed));
}

static pkt_t *ps_pop(pstack_t *s) {
    tagptr_t old = atomic_load_explicit(&s->top, memory_order_acquire);
    while (old.ptr != NULL) {
        pkt_t *next = atomic_load_explicit(&old.ptr->next, memory_order_relaxed);
        POP_WINDOW();
        tagptr_t nw = { next, old.tag + 1u };
        if (atomic_compare_exchange_weak_explicit(&s->top, &old, nw,
                                                  memory_order_acquire, memory_order_acquire))
            return old.ptr;
    }
    return NULL;
}

/* ======================= 하네스 (여기부터는 starter 와 같다) ======================= */
/* 세 스택을 같은 모양으로 다루기 위한 얇은 래퍼 (인덱스로 통일; Part C 는 g_pkt 배열 인덱스). */
typedef struct {
    const char *name;
    void (*init)(void *s);
    void (*push)(void *s, uint32_t i);
    uint32_t (*pop)(void *s);
    uint32_t (*top)(void *s);            /* 검사용 (동시 실행 아님) */
    uint32_t (*next_of)(uint32_t i);
} ops_t;

static void w_is_init(void *s) { is_init(s); }
static void w_is_push(void *s, uint32_t i) { is_push(s, i); }
static uint32_t w_is_pop(void *s) { return is_pop(s); }
static uint32_t w_is_top(void *s) { return atomic_load(&((istack_t *)s)->top); }
static uint32_t pool_next(uint32_t i) { return atomic_load(&g_pool[i].next); }
static void w_ts_init(void *s) { ts_init(s); }
static void w_ts_push(void *s, uint32_t i) { ts_push(s, i); }
static uint32_t w_ts_pop(void *s) { return ts_pop(s); }
static uint32_t w_ts_top(void *s) { return tag_idx(atomic_load(&((tstack_t *)s)->top)); }
static uint32_t pidx(const pkt_t *p) { return p ? (uint32_t)(p - g_pkt) : NIL; }
static void w_ps_init(void *s) { ps_init(s); }
static void w_ps_push(void *s, uint32_t i) { ps_push(s, &g_pkt[i]); }
static uint32_t w_ps_pop(void *s) { return pidx(ps_pop(s)); }
static uint32_t w_ps_top(void *s) { return pidx(atomic_load(&((pstack_t *)s)->top).ptr); }
static uint32_t pkt_next(uint32_t i) { return pidx(atomic_load(&g_pkt[i].next)); }

static const ops_t OPS_NAIVE = { "naive idx", w_is_init, w_is_push, w_is_pop, w_is_top, pool_next };
static const ops_t OPS_TAG64 = { "tag64", w_ts_init, w_ts_push, w_ts_pop, w_ts_top, pool_next };
static const ops_t OPS_DW128 = { "dw128", w_ps_init, w_ps_push, w_ps_pop, w_ps_top, pkt_next };

/* ---------- 리스트 무결성 검사기 ----------
 * top 부터 next 를 따라가며 노드별 등장 횟수를 센다 (최대 n 걸음, 같은 노드를 다시 만나면 사이클).
 * held[i] = "지금 어떤 스레드가 들고 있다" 고 알려진 노드. 스택에도 있고 held 이기도 하면 = 두 곳에 있음. */
typedef struct { unsigned walked, bad, both, lost; bool cycle; } integ_t;

static integ_t integrity(const ops_t *o, void *s, unsigned n, const bool *held) {
    integ_t r = { 0, 0, 0, 0, false };
    unsigned char seen[PKT_N > POOL_N ? PKT_N : POOL_N] = { 0 };
    for (uint32_t i = o->top(s); i != NIL; i = o->next_of(i)) {
        if (i >= n) { r.bad++; break; }
        if (seen[i]) { r.cycle = true; break; }
        seen[i] = 1;
        r.walked++;
    }
    for (unsigned k = 0; k < n; k++) {
        bool h = held && held[k];
        if (seen[k] && h) r.both++;
        if (!seen[k] && !h) r.lost++;
    }
    return r;
}
static bool integ_ok(integ_t r, unsigned on_stack) {
    return !r.cycle && r.bad == 0 && r.both == 0 && r.lost == 0 && r.walked == on_stack;
}
static void integ_print(const char *tag, const ops_t *o, void *s, unsigned n, const bool *held) {
    integ_t r = integrity(o, s, n, held);
    printf("  [%s] %-9s top", tag, o->name);
    uint32_t i = o->top(s);
    for (int step = 0; step < 6 && i != NIL && i < n; step++, i = o->next_of(i))
        printf(" -> %c", 'A' + (int)i);
    printf("%s   (walked %u, both %u, lost %u, cycle %s)\n", i != NIL ? " -> ..." : " -> NIL",
           r.walked, r.both, r.lost, r.cycle ? "YES" : "no");
}

/* ---------- 게이트: T1 을 pop window 에서 한 번 멈춘다 (mutex + cond, 시간 제한 있음) ---------- */
static struct { pthread_mutex_t m; pthread_cond_t c; int hits, paused, resume, done; } G = {
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0, 0, 0 };

static void gate_timedwait(int sec) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += sec;
    pthread_cond_timedwait(&G.c, &G.m, &ts);
}
static void gate_window(void) {               /* T1 의 POP_WINDOW() 가 부르는 훅 */
    pthread_mutex_lock(&G.m);
    if (++G.hits == 1) {
        G.paused = 1;
        pthread_cond_broadcast(&G.c);
        for (int k = 0; k < 3 && !G.resume; k++) gate_timedwait(1);
    }
    pthread_mutex_unlock(&G.m);
}
typedef struct { const ops_t *o; void *s; uint32_t got; } t1_arg_t;
static void *t1_main(void *p) {
    t1_arg_t *a = p;
    tl_pop_window = gate_window;
    a->got = a->o->pop(a->s);
    tl_pop_window = NULL;
    pthread_mutex_lock(&G.m);
    G.done = 1;
    pthread_cond_broadcast(&G.c);
    pthread_mutex_unlock(&G.m);
    return NULL;
}

/* 강제 ABA 스케줄. 시작 상태: top -> A -> B -> C -> D.
 *   1) T1: pop 시작 — top=A, next=B 를 읽고 window 에서 멈춤
 *   2) main(T2): pop → A, pop → B, push A        (이제 top -> A -> C -> D, B 는 main 이 들고 있음)
 *   3) T1 재개 — CAS(top: A → B). 인덱스만 비교하면 "A 그대로네" 하고 성공해 버린다. */
typedef struct { bool paused; int hits; uint32_t t1_got, m_a, m_b; } sched_t;
static sched_t force_aba(const ops_t *o, void *s) {
    sched_t r = { false, 0, NIL, NIL, NIL };
    t1_arg_t a = { o, s, NIL };
    pthread_t t;
    pthread_mutex_lock(&G.m);
    G.hits = G.paused = G.resume = G.done = 0;
    pthread_mutex_unlock(&G.m);
    pthread_create(&t, NULL, t1_main, &a);
    pthread_mutex_lock(&G.m);
    for (int k = 0; k < 3 && !G.paused && !G.done; k++) gate_timedwait(1);
    r.paused = G.paused;
    pthread_mutex_unlock(&G.m);
    if (r.paused) {
        r.m_a = o->pop(s);
        r.m_b = o->pop(s);
        if (r.m_a != NIL) o->push(s, r.m_a);
    }
    pthread_mutex_lock(&G.m);
    G.resume = 1;
    pthread_cond_broadcast(&G.c);
    pthread_mutex_unlock(&G.m);
    pthread_join(t, NULL);
    r.hits = G.hits;
    r.t1_got = a.got;
    return r;
}
static void mark(bool *held, uint32_t i) { if (i < 4u) held[i] = true; }
/* top -> A(0) -> B(1) -> C(2) -> D(3) */
static void setup_abcd(const ops_t *o, void *s) {
    o->init(s);
    for (uint32_t i = 4; i-- > 0;) o->push(s, i);
}

/* ---------- 단위 테스트 (단일 스레드) ---------- */
static int g_window_calls;
static void count_window(void) { g_window_calls++; }

static bool unit_lifo(const ops_t *o, void *s, unsigned n) {
    bool ok = true;
    o->init(s);
    ok &= (o->pop(s) == NIL);                                /* 빈 스택 */
    for (uint32_t i = 0; i < n; i++) o->push(s, i);
    ok &= integ_ok(integrity(o, s, n, NULL), n);
    for (uint32_t i = n; i-- > 0;) ok &= (o->pop(s) == i);  /* LIFO */
    ok &= (o->pop(s) == NIL);
    o->push(s, 5);
    ok &= (o->pop(s) == 5 && o->pop(s) == NIL);
    return ok;
}

/* ---------- 8 스레드 스트레스 (Part B/C 의 스택) ----------
 * 스레드마다: pop a, pop b → 소유 표시(relaxed exchange: 0 이 아니면 동시 소유) → value++ (일반 변수!)
 * → 소유 해제 → 순서를 바꿔 가며 push. 끝나면 sum(value) == 성공한 pop 수, 모든 노드가 스택에 정확히 한 번. */
#define NTH 8
static atomic_uint g_own[POOL_N];
typedef struct { const ops_t *o; void *s; unsigned iters, pops, dup, empty; } sw_t;
static void *stress_worker(void *p) {
    sw_t *w = p;
    for (unsigned k = 0; k < w->iters; k++) {
        uint32_t a = w->o->pop(w->s), b = w->o->pop(w->s);
        uint32_t h[2] = { a, b };
        for (int j = 0; j < 2; j++) {
            if (h[j] == NIL) { w->empty++; continue; }
            if (atomic_exchange_explicit(&g_own[h[j]], 1u, memory_order_relaxed) != 0u) w->dup++;
            g_pool[h[j]].value++;
            w->pops++;
            atomic_store_explicit(&g_own[h[j]], 0u, memory_order_relaxed);
        }
        if (k & 1u) { uint32_t t = h[0]; h[0] = h[1]; h[1] = t; }
        for (int j = 0; j < 2; j++) if (h[j] != NIL) w->o->push(w->s, h[j]);
    }
    return NULL;
}
static bool stress_stack(const ops_t *o, void *s, unsigned iters) {
    o->init(s);
    for (uint32_t i = 0; i < POOL_N; i++) { g_pool[i].value = 0; atomic_store(&g_own[i], 0u); o->push(s, i); }
    sw_t w[NTH];
    pthread_t t[NTH];
    for (int i = 0; i < NTH; i++) { w[i] = (sw_t){ o, s, iters, 0, 0, 0 }; pthread_create(&t[i], NULL, stress_worker, &w[i]); }
    unsigned long long pops = 0, sum = 0;
    unsigned dup = 0, empty = 0;
    for (int i = 0; i < NTH; i++) { pthread_join(t[i], NULL); pops += w[i].pops; dup += w[i].dup; empty += w[i].empty; }
    for (uint32_t i = 0; i < POOL_N; i++) sum += g_pool[i].value;
    integ_t r = integrity(o, s, POOL_N, NULL);
    printf("  stress %-9s %d thr x %u: pops %llu, sum %llu, dup %u, empty %u, walked %u\n",
           o->name, NTH, iters, pops, sum, dup, empty, r.walked);
    return dup == 0 && empty == 0 && pops == 2ull * NTH * iters && sum == pops && integ_ok(r, POOL_N);
}

/* ---------- Part C 스트레스: ISR 생산자 2 + 워커 6, free/ready 두 스택 ---------- */
#define NPROD 2
#define NCONS 6
#define PER_PROD (100000u / SAN_SCALE)
static pstack_t g_free, g_ready;
static atomic_uchar g_seen[NPROD * 100000u];
static atomic_uint g_consumed, g_bad, g_dup_pkt;
static atomic_int g_abort;
static struct timespec g_deadline;

static bool past_deadline(void) {
    struct timespec n;
    clock_gettime(CLOCK_MONOTONIC, &n);
    return n.tv_sec > g_deadline.tv_sec || (n.tv_sec == g_deadline.tv_sec && n.tv_nsec > g_deadline.tv_nsec);
}
static uint32_t pkt_sum(const pkt_t *p) {
    uint32_t h = p->seq * 2654435761u;
    for (unsigned i = 0; i < PKT_DATA; i++) h = (h ^ p->data[i]) * 16777619u;
    return h;
}
static void *isr_producer(void *arg) {                  /* "UART RX ISR": 버퍼 할당 → 채움 → ready 에 게시 */
    uint32_t base = (uint32_t)(uintptr_t)arg * PER_PROD;
    for (uint32_t k = 0; k < PER_PROD && !atomic_load(&g_abort); k++) {
        pkt_t *p;
        while ((p = ps_pop(&g_free)) == NULL) {             /* 풀 고갈: 실제 ISR 이면 drop + 카운트 */
            if (past_deadline()) { atomic_store(&g_abort, 1); return NULL; }
            sched_yield();
        }
        if (atomic_exchange_explicit(&p->owner, 1u, memory_order_relaxed) != 0u) atomic_fetch_add(&g_dup_pkt, 1u);
        p->seq = base + k;
        for (unsigned i = 0; i < PKT_DATA; i++) p->data[i] = (uint8_t)(p->seq * 31u + i);
        p->sum = pkt_sum(p);
        atomic_store_explicit(&p->owner, 0u, memory_order_relaxed);
        ps_push(&g_ready, p);
    }
    return NULL;
}
static void *worker(void *arg) {                         /* 태스크: ready 에서 꺼내 검사 → free 로 반납 */
    (void)arg;
    const unsigned total = NPROD * PER_PROD;
    while (atomic_load(&g_consumed) < total && !atomic_load(&g_abort)) {
        pkt_t *p = ps_pop(&g_ready);
        if (p == NULL) {
            if (past_deadline()) { atomic_store(&g_abort, 1); break; }
            sched_yield();
            continue;
        }
        if (atomic_exchange_explicit(&p->owner, 2u, memory_order_relaxed) != 0u) atomic_fetch_add(&g_dup_pkt, 1u);
        if (p->sum != pkt_sum(p) || p->seq >= total) atomic_fetch_add(&g_bad, 1u);
        else atomic_fetch_add_explicit(&g_seen[p->seq], 1u, memory_order_relaxed);
        atomic_store_explicit(&p->owner, 0u, memory_order_relaxed);
        ps_push(&g_free, p);
        atomic_fetch_add(&g_consumed, 1u);
    }
    return NULL;
}

int main(void) {
    istack_t is;
    tstack_t ts;
    pstack_t ps;
    printf("== Part A: Treiber stack (index top)\n");
    bool a_ok = unit_lifo(&OPS_NAIVE, &is, POOL_N);
    CHECK(a_ok);
    setup_abcd(&OPS_NAIVE, &is);
    g_window_calls = 0;
    tl_pop_window = count_window;
    uint32_t x = is_pop(&is);
    tl_pop_window = NULL;
    CHECK(x == 0u && g_window_calls == 1);                  /* 경합 없으면 window 1번, CAS 1번에 성공 */
    a_ok = a_ok && x == 0u && g_window_calls == 1;

    printf("== Part B: forced ABA on the naive stack\n");
    if (a_ok) {
        bool held[POOL_N] = { false };
        setup_abcd(&OPS_NAIVE, &is);
        integ_print("before", &OPS_NAIVE, &is, 4, NULL);
        sched_t r = force_aba(&OPS_NAIVE, &is);
        mark(held, r.t1_got);
        mark(held, r.m_b);
        integ_print("after", &OPS_NAIVE, &is, 4, held);
        integ_t ir = integrity(&OPS_NAIVE, &is, 4, held);
        CHECK(r.paused && r.m_a == 0u && r.m_b == 1u);       /* main 은 A, B 를 떼고 A 를 되돌렸다 */
        CHECK(r.t1_got == 0u && r.hits == 1);                 /* T1 의 CAS 가 첫 시도에 "성공" — 버그 */
        CHECK(w_is_top(&is) == 1u && ir.both == 1u && ir.lost == 0u);       /* B 가 스택 맨 위에도, main 손에도 */
        setup_abcd(&OPS_NAIVE, &is);                          /* 결과를 끝까지: main 이 B 를 반납하면? */
        r = force_aba(&OPS_NAIVE, &is);
        is_push(&is, r.m_b);
        bool held_a[POOL_N] = { false };
        mark(held_a, r.t1_got);                               /* A 는 T1 이 들고 있다 */
        integ_print("B freed", &OPS_NAIVE, &is, 4, held_a);
        ir = integrity(&OPS_NAIVE, &is, 4, held_a);
        uint32_t p1 = is_pop(&is), p2 = is_pop(&is);
        CHECK(ir.cycle && ir.lost == 2u && p1 == 1u && p2 == 1u);   /* B -> B 사이클, C·D 분실, B 를 두 번 나눠 준다 */
    } else {
        printf("SKIP naive ABA checks: Part A failed\n");
        g_total += 4;
    }

    printf("== Part B: fix 1 — 64-bit (tag, index)\n");
    uint64_t w = tag_pack(7u, 3u);
    CHECK(w == 0x0000000300000007ull && tag_idx(w) == 7u && tag_tag(w) == 3u);
    CHECK(tag_idx(tag_pack(NIL, 0xFFFFFFFFu)) == NIL && tag_tag(tag_pack(NIL, 0xFFFFFFFFu)) == 0xFFFFFFFFu);
    bool b_ok = unit_lifo(&OPS_TAG64, &ts, POOL_N);
    CHECK(b_ok);
    ts_init(&ts);
    atomic_store(&ts.top, tag_pack(NIL, 0xFFFFFFFFu));      /* 태그가 막 감기려는 상태 */
    ts_push(&ts, 3u);
    uint64_t after = atomic_load(&ts.top);
    CHECK(tag_idx(after) == 3u && tag_tag(after) == 0u && ts_pop(&ts) == 3u);   /* 감겨도 인덱스는 멀쩡 */
    if (b_ok) {
        bool held[POOL_N] = { false };
        setup_abcd(&OPS_TAG64, &ts);
        sched_t r = force_aba(&OPS_TAG64, &ts);
        mark(held, r.t1_got);
        mark(held, r.m_b);
        integ_print("after", &OPS_TAG64, &ts, 4, held);
        CHECK(r.paused && r.t1_got == 0u && r.m_b == 1u && r.hits == 2);   /* CAS 실패 → 재시도 1번 */
        CHECK(integ_ok(integrity(&OPS_TAG64, &ts, 4, held), 2));           /* top -> C -> D, A·B 는 각자 손에 */
        ts_push(&ts, r.m_b);
        ts_push(&ts, r.t1_got);
        CHECK(integ_ok(integrity(&OPS_TAG64, &ts, 4, NULL), 4));
        CHECK(stress_stack(&OPS_TAG64, &ts, 200000u / SAN_SCALE));
    } else {
        printf("SKIP tag64 schedule/stress: unit checks failed\n");
        g_total += 4;
    }

    printf("== Part C: fix 2 — 128-bit {pointer, tag} packet pool\n");
    CHECK(atomic_is_lock_free(&ps.top));
    bool c_ok = unit_lifo(&OPS_DW128, &ps, PKT_N);
    CHECK(c_ok);
    if (c_ok) {
        bool held[PKT_N] = { false };
        setup_abcd(&OPS_DW128, &ps);
        sched_t r = force_aba(&OPS_DW128, &ps);
        mark(held, r.t1_got);
        mark(held, r.m_b);
        integ_print("after", &OPS_DW128, &ps, 4, held);
        CHECK(r.paused && r.t1_got == 0u && r.m_b == 1u && r.hits == 2 &&
              integ_ok(integrity(&OPS_DW128, &ps, 4, held), 2));
        ps_init(&g_free);
        ps_init(&g_ready);
        for (uint32_t i = 0; i < PKT_N; i++) { atomic_store(&g_pkt[i].owner, 0u); ps_push(&g_free, &g_pkt[i]); }
        clock_gettime(CLOCK_MONOTONIC, &g_deadline);
        g_deadline.tv_sec += 15;
        pthread_t tp[NPROD], tc[NCONS];
        for (int i = 0; i < NPROD; i++) pthread_create(&tp[i], NULL, isr_producer, (void *)(uintptr_t)i);
        for (int i = 0; i < NCONS; i++) pthread_create(&tc[i], NULL, worker, NULL);
        for (int i = 0; i < NPROD; i++) pthread_join(tp[i], NULL);
        for (int i = 0; i < NCONS; i++) pthread_join(tc[i], NULL);
        unsigned once = 0;
        for (unsigned i = 0; i < NPROD * PER_PROD; i++) once += (atomic_load(&g_seen[i]) == 1u);
        printf("  pool: produced %u, consumed %u, exactly-once %u, bad %u, dup-owner %u, abort %d\n",
               NPROD * PER_PROD, atomic_load(&g_consumed), once, atomic_load(&g_bad),
               atomic_load(&g_dup_pkt), atomic_load(&g_abort));
        CHECK(!atomic_load(&g_abort) && atomic_load(&g_consumed) == NPROD * PER_PROD);
        CHECK(once == NPROD * PER_PROD && atomic_load(&g_bad) == 0u && atomic_load(&g_dup_pkt) == 0u);
        CHECK(integ_ok(integrity(&OPS_DW128, &g_free, PKT_N, NULL), PKT_N) && ps_pop(&g_ready) == NULL);
    } else {
        printf("SKIP dw128 schedule/stress: unit checks failed\n");
        g_total += 4;
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
