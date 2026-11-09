/* DS-A04 추가 연습 — 한 번에 검증
 *   연습 1  32비트 워드에 (태그 16 | 인덱스 16) — Cortex-M3/M4 용. 태그가 감기면 ABA 가 돌아온다 (정확히 몇 번?)
 *   연습 2  ABA 가 "안" 생기는 경우: push 는 ABA 에 안전, pop 하는 스레드가 하나(MPSC)면 태그 불필요
 *   연습 3  pop_all = atomic_exchange (Linux llist_del_all 방식) + 뒤집어 FIFO — 4 생산자 스트레스
 *   연습 4  세대(generation) 핸들: 해제 후 재사용된 슬롯을 낡은 핸들로 접근하면 NULL
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

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

static _Thread_local void (*tl_window)(void);
#define WINDOW() do { if (tl_window) tl_window(); } while (0)

#define NIL 0xFFFFFFFFu
#define N 64u
static _Atomic uint32_t g_next[N];

/* ---------- 게이트 (solution.c 와 같은 방식: T1 을 window 에서 한 번 멈춤) ---------- */
static struct { pthread_mutex_t m; pthread_cond_t c; int hits, paused, resume, done; } G = {
    PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0, 0, 0 };
static void gwait(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 1;
    pthread_cond_timedwait(&G.c, &G.m, &ts);
}
static void gate_window(void) {
    pthread_mutex_lock(&G.m);
    if (++G.hits == 1) {
        G.paused = 1;
        pthread_cond_broadcast(&G.c);
        for (int k = 0; k < 3 && !G.resume; k++) gwait();
    }
    pthread_mutex_unlock(&G.m);
}
typedef struct { void (*t1)(void *); void *arg; } job_t;
static void *t1_main(void *p) {
    job_t *j = p;
    tl_window = gate_window;
    j->t1(j->arg);
    tl_window = NULL;
    pthread_mutex_lock(&G.m);
    G.done = 1;
    pthread_cond_broadcast(&G.c);
    pthread_mutex_unlock(&G.m);
    return NULL;
}
/* T1 이 t1(arg) 를 돌다 window 에서 멈춘 사이 main 이 t2(arg) 를 실행. 반환 = window 진입 횟수 (멈춤 실패면 -1) */
static int force(void (*t1)(void *), void (*t2)(void *), void *arg) {
    job_t j = { t1, arg };
    pthread_t t;
    pthread_mutex_lock(&G.m);
    G.hits = G.paused = G.resume = G.done = 0;
    pthread_mutex_unlock(&G.m);
    pthread_create(&t, NULL, t1_main, &j);
    pthread_mutex_lock(&G.m);
    for (int k = 0; k < 3 && !G.paused && !G.done; k++) gwait();
    int paused = G.paused;
    pthread_mutex_unlock(&G.m);
    if (paused) t2(arg);
    pthread_mutex_lock(&G.m);
    G.resume = 1;
    pthread_cond_broadcast(&G.c);
    pthread_mutex_unlock(&G.m);
    pthread_join(t, NULL);
    return paused ? G.hits : -1;
}
/* 무결성: top 부터 걸으며 held 와 겹치는 노드(both), 어디에도 없는 노드(lost), 사이클 */
typedef struct { unsigned walked, both, lost; bool cycle; } integ_t;
static integ_t walk(uint32_t top, unsigned n, const bool *held, uint32_t mask) {
    integ_t r = { 0, 0, 0, false };
    unsigned char seen[N] = { 0 };
    for (uint32_t i = top; i != mask; i = atomic_load(&g_next[i]) & mask) {
        if (i >= n || seen[i]) { r.cycle = true; break; }
        seen[i] = 1;
        r.walked++;
    }
    for (unsigned k = 0; k < n; k++) {
        bool h = held && held[k];
        r.both += (seen[k] && h);
        r.lost += (!seen[k] && !h);
    }
    return r;
}
static bool ok(integ_t r, unsigned on) { return !r.cycle && !r.both && !r.lost && r.walked == on; }

/* ======================= 연습 1 — 16비트 태그 | 16비트 인덱스 ======================= */
#define NIL16 0xFFFFu
typedef struct { _Atomic uint32_t top; } t16_t;
static uint32_t t16_idx(uint32_t w) { return w & 0xFFFFu; }
static uint32_t t16_tag(uint32_t w) { return w >> 16; }
static uint32_t t16_pack(uint32_t idx, uint32_t tag) { return ((tag & 0xFFFFu) << 16) | (idx & 0xFFFFu); }
static void t16_push(t16_t *s, uint32_t i) {
    uint32_t old = atomic_load_explicit(&s->top, memory_order_relaxed), nw;
    do {
        atomic_store_explicit(&g_next[i], t16_idx(old), memory_order_relaxed);
        nw = t16_pack(i, t16_tag(old) + 1u);
    } while (!atomic_compare_exchange_weak_explicit(&s->top, &old, nw, memory_order_release, memory_order_relaxed));
}
static uint32_t t16_pop(t16_t *s) {
    uint32_t old = atomic_load_explicit(&s->top, memory_order_acquire);
    while (t16_idx(old) != NIL16) {
        uint32_t next = atomic_load_explicit(&g_next[t16_idx(old)], memory_order_relaxed) & 0xFFFFu;
        WINDOW();
        uint32_t nw = t16_pack(next, t16_tag(old) + 1u);
        if (atomic_compare_exchange_weak_explicit(&s->top, &old, nw, memory_order_acquire, memory_order_acquire))
            return t16_idx(old);
    }
    return NIL16;
}
/* 태그 비트 b 개: T1 이 멈춘 사이 성공한 CAS 가 정확히 2^b 의 배수 번이면 태그가 같은 값으로 돌아온다. */
static unsigned long long ops_to_wrap(unsigned bits) { return 1ull << bits; }

static t16_t g_t16;
static uint32_t g_t1_got;
static unsigned g_pad_pairs;
static void ex1_t1(void *a) { g_t1_got = t16_pop(a); }
/* main: pop A, pop B, pop C, push A (CAS 4번) + (pop A, push A) × pad 쌍 → 총 4 + 2·pad 번 */
static void ex1_t2(void *a) {
    t16_t *s = a;
    uint32_t x = t16_pop(s);
    (void)t16_pop(s);
    (void)t16_pop(s);
    t16_push(s, x);
    for (unsigned k = 0; k < g_pad_pairs; k++) t16_push(s, t16_pop(s));
}
static void ex1_setup(void) {
    atomic_store(&g_t16.top, t16_pack(NIL16, 0));
    for (uint32_t i = 4; i-- > 0;) t16_push(&g_t16, i);       /* top -> A B C D */
}

/* ======================= 연습 2 — push 의 ABA 는 무해, 단일 소비자는 태그 불필요 ======================= */
static _Atomic uint32_t g_top;                                  /* 인덱스만 (naive) */
static void n_push(uint32_t i) {
    uint32_t old = atomic_load_explicit(&g_top, memory_order_relaxed);
    do {
        atomic_store_explicit(&g_next[i], old, memory_order_relaxed);
        WINDOW();                                               /* next 를 쓴 뒤 · CAS 전 */
    } while (!atomic_compare_exchange_weak_explicit(&g_top, &old, i, memory_order_release, memory_order_relaxed));
}
static uint32_t n_pop(void) {
    uint32_t old = atomic_load_explicit(&g_top, memory_order_acquire);
    while (old != NIL) {
        uint32_t next = atomic_load_explicit(&g_next[old], memory_order_relaxed);
        WINDOW();
        if (atomic_compare_exchange_weak_explicit(&g_top, &old, next, memory_order_acquire, memory_order_acquire))
            return old;
    }
    return NIL;
}
static void n_setup(void) {
    atomic_store(&g_top, NIL);
    for (uint32_t i = 4; i-- > 0;) n_push(i);                   /* top -> A B C D */
}
static uint32_t g_m_b;
static void ex2a_t1(void *a) { (void)a; n_push(4); }            /* T1: E(4) 를 push 하다 멈춤 */
static void ex2a_t2(void *a) { (void)a; uint32_t x = n_pop(); g_m_b = n_pop(); n_push(x); }   /* pop A, pop B, push A */
static void ex2b_t1(void *a) { (void)a; g_t1_got = n_pop(); }   /* 유일한 소비자 T1 */
static void ex2b_t2(void *a) { (void)a; n_push(4); n_push(5); } /* 다른 스레드는 push 만 */

/* ======================= 연습 3 — pop_all (exchange) + reverse ======================= */
/* 생산자: free 스택(태그 64)에서 노드를 얻어 값을 쓰고 inbox 에 push. 소비자 1: inbox 를 통째로 가져가 뒤집어 처리. */
#define NPROD 4
#define PER (50000u / SAN_SCALE)
static uint32_t g_val[N];
static _Atomic uint64_t g_free;
static _Atomic uint32_t g_inbox;
static void f_push(uint32_t i) {
    uint64_t old = atomic_load_explicit(&g_free, memory_order_relaxed), nw;
    do {
        atomic_store_explicit(&g_next[i], (uint32_t)old, memory_order_relaxed);
        nw = ((uint64_t)((uint32_t)(old >> 32) + 1u) << 32) | i;
    } while (!atomic_compare_exchange_weak_explicit(&g_free, &old, nw, memory_order_release, memory_order_relaxed));
}
static uint32_t f_pop(void) {
    uint64_t old = atomic_load_explicit(&g_free, memory_order_acquire);
    while ((uint32_t)old != NIL) {
        uint32_t next = atomic_load_explicit(&g_next[(uint32_t)old], memory_order_relaxed);
        uint64_t nw = ((uint64_t)((uint32_t)(old >> 32) + 1u) << 32) | next;
        if (atomic_compare_exchange_weak_explicit(&g_free, &old, nw, memory_order_acquire, memory_order_acquire))
            return (uint32_t)old;
    }
    return NIL;
}
static void inbox_push(uint32_t i) {                    /* 여러 생산자: 평범한 CAS push (ABA 안전) */
    uint32_t old = atomic_load_explicit(&g_inbox, memory_order_relaxed);
    do atomic_store_explicit(&g_next[i], old, memory_order_relaxed);
    while (!atomic_compare_exchange_weak_explicit(&g_inbox, &old, i, memory_order_release, memory_order_relaxed));
}
/* 비교값이 없다 → "그 사이 A→B→A" 를 신경 쓸 이유가 없다. acquire: 각 노드의 next·값을 본다. */
static uint32_t inbox_pop_all(void) { return atomic_exchange_explicit(&g_inbox, NIL, memory_order_acquire); }
/* LIFO 사슬을 뒤집어 도착 순서(FIFO)로. 소비자 혼자 가진 사슬이라 relaxed 로 충분. */
static uint32_t chain_reverse(uint32_t h) {
    uint32_t prev = NIL;
    while (h != NIL) {
        uint32_t nx = atomic_load_explicit(&g_next[h], memory_order_relaxed);
        atomic_store_explicit(&g_next[h], prev, memory_order_relaxed);
        prev = h;
        h = nx;
    }
    return prev;
}
static atomic_int g_abort3;
static void *ex3_prod(void *arg) {
    uint32_t pid = (uint32_t)(uintptr_t)arg;
    for (uint32_t k = 0; k < PER && !atomic_load(&g_abort3); k++) {
        uint32_t i;
        while ((i = f_pop()) == NIL) if (atomic_load(&g_abort3)) return NULL;
        g_val[i] = pid << 24 | k;
        inbox_push(i);
    }
    return NULL;
}

/* ======================= 연습 4 — 세대 핸들 ======================= */
/* 핸들 = (세대 16 << 16) | 인덱스 16. free 할 때 세대 +1 → 낡은 핸들은 get 에서 거절 (use-after-free 감지). */
#define HN 8u
static _Atomic uint32_t g_gen[HN];
static _Atomic uint32_t g_hfree = 0;                     /* 비트 i = 1 → 슬롯 i 비었음 */
static void h_init(void) { atomic_store(&g_hfree, (1u << HN) - 1u); for (unsigned i = 0; i < HN; i++) atomic_store(&g_gen[i], 0u); }
static uint32_t h_alloc(void) {                          /* 가장 낮은 빈 슬롯 (CAS 루프), 없으면 0xFFFFFFFF */
    uint32_t m = atomic_load_explicit(&g_hfree, memory_order_relaxed);
    while (m) {
        uint32_t i = (uint32_t)__builtin_ctz(m);
        if (atomic_compare_exchange_weak_explicit(&g_hfree, &m, m & ~(1u << i), memory_order_acquire, memory_order_relaxed))
            return (atomic_load_explicit(&g_gen[i], memory_order_relaxed) & 0xFFFFu) << 16 | i;
    }
    return 0xFFFFFFFFu;
}
static bool h_valid(uint32_t h) {
    uint32_t i = h & 0xFFFFu;
    return i < HN && (atomic_load_explicit(&g_gen[i], memory_order_acquire) & 0xFFFFu) == (h >> 16) &&
           !(atomic_load_explicit(&g_hfree, memory_order_relaxed) & (1u << i));
}
static bool h_free(uint32_t h) {
    if (!h_valid(h)) return false;                       /* 이중 해제 / 낡은 핸들 */
    uint32_t i = h & 0xFFFFu;
    atomic_fetch_add_explicit(&g_gen[i], 1u, memory_order_relaxed);
    atomic_fetch_or_explicit(&g_hfree, 1u << i, memory_order_release);
    return true;
}

int main(void) {
    printf("== 연습 1: 16-bit tag wrap\n");
    CHECK(ops_to_wrap(16) == 65536ull && ops_to_wrap(32) == 4294967296ull);
    CHECK(t16_pack(3, 0x1FFFFu) == 0xFFFF0003u && t16_tag(t16_pack(3, 0x10000u)) == 0u);   /* 17번째 비트는 잘림 */
    {
        bool held[N] = { false };
        held[1] = held[2] = true;                                 /* main 이 B, C 를 들고 있다 */
        ex1_setup();
        g_pad_pairs = 32765u;                                     /* 4 + 2·32765 = 65534 번 → 태그 다름 */
        int hits = force(ex1_t1, ex1_t2, &g_t16);
        held[g_t1_got & 63u] = true;
        integ_t r = walk(t16_idx(atomic_load(&g_t16.top)), 4, held, NIL16);
        CHECK(hits == 2 && g_t1_got == 0u && ok(r, 1));           /* T1 의 CAS 실패 → 재시도 → top -> D */
        bool held2[N] = { false };
        held2[1] = held2[2] = true;
        ex1_setup();
        g_pad_pairs = 32766u;                                     /* 4 + 2·32766 = 65536 번 → 태그가 같은 값으로 */
        hits = force(ex1_t1, ex1_t2, &g_t16);
        held2[g_t1_got & 63u] = true;
        r = walk(t16_idx(atomic_load(&g_t16.top)), 4, held2, NIL16);
        printf("  wrap: hits %d, t1 got %u, both %u, walked %u\n", hits, g_t1_got, r.both, r.walked);
        CHECK(hits == 1 && r.both == 2u);                         /* ABA 귀환: B, C 가 스택에도 main 손에도 */
    }

    printf("== 연습 2: when ABA is harmless\n");
    {
        bool held[N] = { false };
        n_setup();
        int hits = force(ex2a_t1, ex2a_t2, NULL);                 /* T1 이 E push 중 멈춤 → main: pop A, pop B, push A */
        held[g_m_b] = true;
        integ_t r = walk(atomic_load(&g_top), 5, held, NIL);
        CHECK(hits == 1 && atomic_load(&g_top) == 4u && atomic_load(&g_next[4]) == 0u && ok(r, 4));   /* E -> A -> C -> D */
        n_setup();
        hits = force(ex2b_t1, ex2b_t2, NULL);                     /* 유일한 소비자 T1 이 pop 중 멈춤 → main 은 push 만 */
        bool held2[N] = { false };
        held2[g_t1_got < N ? g_t1_got : 0] = true;
        r = walk(atomic_load(&g_top), 6, held2, NIL);
        CHECK(hits == 2 && g_t1_got == 5u && ok(r, 5));           /* CAS 실패 → 재시도 → 최신 top(F) 를 가져감 */
    }

    printf("== 연습 3: pop_all + reverse, %d producers x %u\n", NPROD, PER);
    {
        atomic_store(&g_free, (uint64_t)NIL);
        atomic_store(&g_inbox, NIL);
        for (uint32_t i = 0; i < N; i++) f_push(i);
        pthread_t t[NPROD];
        for (int p = 0; p < NPROD; p++) pthread_create(&t[p], NULL, ex3_prod, (void *)(uintptr_t)p);
        uint32_t expect[NPROD] = { 0 };
        unsigned got = 0, order_bad = 0, batches = 0;
        struct timespec t0, now;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        while (got < NPROD * PER) {
            uint32_t h = chain_reverse(inbox_pop_all());
            if (h == NIL) {
                clock_gettime(CLOCK_MONOTONIC, &now);
                if (now.tv_sec - t0.tv_sec > 10) { atomic_store(&g_abort3, 1); break; }
                continue;
            }
            batches++;
            while (h != NIL) {
                uint32_t nx = atomic_load_explicit(&g_next[h], memory_order_relaxed);
                uint32_t pid = g_val[h] >> 24, k = g_val[h] & 0xFFFFFFu;
                if (pid >= NPROD || k != expect[pid]) order_bad++;     /* 생산자별로 0,1,2,... 순서 그대로 */
                else expect[pid]++;
                got++;
                f_push(h);
                h = nx;
            }
        }
        for (int p = 0; p < NPROD; p++) pthread_join(t[p], NULL);
        unsigned all = 0;
        for (int p = 0; p < NPROD; p++) all += expect[p];
        printf("  got %u in %u batches, order_bad %u\n", got, batches, order_bad);
        CHECK(!atomic_load(&g_abort3) && got == NPROD * PER && all == NPROD * PER && order_bad == 0u);
        integ_t r = walk((uint32_t)atomic_load(&g_free), N, NULL, NIL);
        CHECK(ok(r, N) && atomic_load(&g_inbox) == NIL);
    }

    printf("== 연습 4: generation handles\n");
    {
        h_init();
        uint32_t h1 = h_alloc();
        CHECK(h1 == 0x00000000u && h_valid(h1));
        CHECK(h_free(h1) && !h_valid(h1) && !h_free(h1));               /* 이중 해제 거절 */
        uint32_t h2 = h_alloc();                                         /* 같은 슬롯 0, 세대 1 */
        CHECK(h2 == 0x00010000u && h_valid(h2) && !h_valid(h1));        /* 낡은 핸들 = 다른 세대 → 거절 */
        uint32_t hs[HN];
        for (unsigned i = 1; i < HN; i++) hs[i] = h_alloc();
        CHECK(h_alloc() == 0xFFFFFFFFu && (hs[HN - 1] & 0xFFFFu) == HN - 1);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
