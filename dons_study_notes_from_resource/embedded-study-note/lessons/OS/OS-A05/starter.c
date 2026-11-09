/* OS-A05 work-stealing 스레드 풀: Chase-Lev deque
 *
 * 호스트 모델: pthread 하나 = 워커(코어) 하나. 워커마다 자기 deque 를 갖는다.
 *   Part A  Chase–Lev deque, C11 형태 (Lê, Pop, Cohen, Zappa Nardelli, PPoPP 2013 의 그림 1, 고정 크기 배열).
 *           주인(owner)만 bottom 쪽에서 push/take, 도둑(thief)은 top 쪽에서 steal (top 에 CAS).
 *   Part B  강제 스케줄(hook)로 "마지막 한 개" 경쟁을 결정적으로 재현: 주인 승 / 도둑 승 / 펜스 누락.
 *           일부러 고장 낸 take 두 개(dq_take_nocas, dq_take_reordered)가 같은 스케줄에서 중복을 내는 것도 CHECK
 *           (= 테스트에 이빨이 있다). 그리고 실제 스레드 스트레스: 모든 항목이 정확히 한 번 소비.
 *   Part C  스레드 풀: spawn/join(기다리는 동안 일을 돕는 join), 무작위 희생자 steal, pending 카운터로 종료 판정.
 *           재귀 병렬 합(psum)과 fib(cutoff). 결과 정확 + 모든 태스크 정확히 한 번 실행.
 *
 * 체크는 "올바르면 매 실행 항상 성립" 하는 것만 본다. 확장성 수치는 demo_bench.c, 실제 하드웨어 재배치는 demo_sb.c.
 * 스레드 스트레스와 풀(Part B 후반, Part C)은 Part A 단위 테스트가 모두 통과해야 실행한다 (시작 코드가 멈추지 않게).
 * 모든 대기에는 시간 제한이 있다 (gate 2 초, 풀 2 초).
 * 새니타이저 빌드(-fsanitize=thread/address)에서는 반복 수를 1/4 로 줄인다 (SCALE).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#define _DARWIN_C_SOURCE
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__has_feature)
#  if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
#    define SCALE 4
#  endif
#endif
#ifndef SCALE
#  define SCALE 1
#endif

static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static void cpu_relax(void) {
#if defined(__aarch64__)
    __asm__ volatile("yield" ::: "memory");
#elif defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("pause" ::: "memory");
#endif
}
static int64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}
static uint32_t xorshift32(uint32_t *s) {
    uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x;
}

/* ======================= Part A — Chase–Lev deque ======================= */
typedef struct worker worker_t;
typedef struct task task_t;
struct task {
    void (*fn)(worker_t *, task_t *);
    int64_t a, b;            /* 인자: psum = [a, b) 구간, fib = n, cutoff */
    int64_t result;
    atomic_int runs;         /* 실행 횟수 — 정확히 1 이어야 한다 */
    atomic_int done;         /* join 용: 결과를 쓴 뒤 release 로 1 */
};

#define DQ_CAP  256                      /* 2 의 거듭제곱 → 인덱스 & (CAP-1) */
#define DQ_MASK (DQ_CAP - 1)
#define DQ_EMPTY ((task_t *)0)           /* 비었음 */
#define DQ_ABORT ((task_t *)1)           /* steal 이 경쟁에서 짐 (top CAS 실패) — 다시 시도해도 됨 */

typedef struct {
    _Alignas(128) _Atomic(int64_t) top;      /* 도둑들이 CAS 하는 쪽 (가장 오래된 항목) */
    _Alignas(128) _Atomic(int64_t) bottom;   /* 주인만 쓴다 (다음에 넣을 자리) */
    _Alignas(128) _Atomic(task_t *) buf[DQ_CAP];   /* 슬롯도 atomic(relaxed) → TSan 오탐 없음 */
} deque_t;
/* 인덱스는 부호 있는 64비트: take 의 bottom-1 이 0 아래로 내려가도 t <= b 비교가 맞다 (논문 그림은 size_t → 7. 실수 1).
 * 2^63 번 push 해야 넘치므로 wrap 은 현실적으로 없다. 슬롯 위치만 & MASK 로 돈다. */

/* 강제 스케줄용 훅. 평소엔 NULL (분기 하나). 테스트가 스레드를 만들기 전에만 바꾼다. */
typedef enum { HK_TAKE_AFTER_TOP, HK_STEAL_BEFORE_CAS } hook_pt;
static void (*g_hook)(hook_pt, deque_t *);
#define HOOK(p, q) do { if (g_hook) g_hook((p), (q)); } while (0)

static void dq_init(deque_t *q) {
    atomic_init(&q->top, 0);
    atomic_init(&q->bottom, 0);
    for (int i = 0; i < DQ_CAP; i++) atomic_init(&q->buf[i], NULL);
}
static int64_t dq_size(deque_t *q) {   /* 대략값 (동시에 바뀔 수 있음). 테스트는 조용할 때만 읽는다 */
    int64_t b = atomic_load_explicit(&q->bottom, memory_order_relaxed);
    int64_t t = atomic_load_explicit(&q->top, memory_order_relaxed);
    return b - t;
}

/* A1: 주인만 호출. 가득 차면 false (고정 크기 — 성장은 연습 2). */
static bool dq_push(deque_t *q, task_t *x) {
    (void)q; (void)x;
    return false; /* TODO A1 */
}

/* A2: 주인만 호출. bottom 쪽(가장 최근)에서 꺼낸다. 비었으면 DQ_EMPTY.
 *     top 을 읽은 직후 HOOK(HK_TAKE_AFTER_TOP, q) 를 부른다 (강제 스케줄 지점). */
static task_t *dq_take(deque_t *q) {
    (void)q;
    return DQ_EMPTY; /* TODO A2 */
}

/* A3: 아무 스레드나 호출. top 쪽(가장 오래된)에서 훔친다. DQ_EMPTY / DQ_ABORT / 태스크.
 *     슬롯을 읽은 뒤, CAS 하기 직전에 HOOK(HK_STEAL_BEFORE_CAS, q) 를 부른다. */
static task_t *dq_steal(deque_t *q) {
    (void)q;
    return DQ_EMPTY; /* TODO A3 */
}

/* ======================= Part B — 일부러 고장 낸 take 두 개 (주어짐) ======================= */
/* B-① 마지막 하나에서 CAS 를 생략: "주인은 bottom 쪽이니 도둑과 안 부딪친다" 는 착각. */
static task_t *dq_take_nocas(deque_t *q) {
    int64_t b = atomic_load_explicit(&q->bottom, memory_order_relaxed) - 1;
    atomic_store_explicit(&q->bottom, b, memory_order_relaxed);
    atomic_thread_fence(memory_order_seq_cst);
    int64_t t = atomic_load_explicit(&q->top, memory_order_relaxed);
    HOOK(HK_TAKE_AFTER_TOP, q);
    if (t <= b) return atomic_load_explicit(&q->buf[b & DQ_MASK], memory_order_relaxed);
    atomic_store_explicit(&q->bottom, b + 1, memory_order_relaxed);
    return DQ_EMPTY;
}
/* B-② seq_cst 펜스가 없으면 CPU 는 "bottom 쓰기" 를 "top 읽기" 뒤로 미뤄도 된다 (store→load 재배치, 쓰기 버퍼).
 *     그 허용된 실행을 소스 순서로 그대로 적은 것: top 을 먼저 읽고 bottom 을 나중에 쓴다. */
static task_t *dq_take_reordered(deque_t *q) {
    int64_t b = atomic_load_explicit(&q->bottom, memory_order_relaxed) - 1;
    int64_t t = atomic_load_explicit(&q->top, memory_order_relaxed);      /* ③ 이 먼저 */
    HOOK(HK_TAKE_AFTER_TOP, q);
    atomic_store_explicit(&q->bottom, b, memory_order_relaxed);           /* ① 이 나중에 보임 */
    task_t *x;
    if (t <= b) {
        x = atomic_load_explicit(&q->buf[b & DQ_MASK], memory_order_relaxed);
        if (t == b) {
            if (!atomic_compare_exchange_strong_explicit(&q->top, &t, t + 1,
                    memory_order_seq_cst, memory_order_relaxed))
                x = DQ_EMPTY;
            atomic_store_explicit(&q->bottom, b + 1, memory_order_relaxed);
        }
    } else {
        x = DQ_EMPTY;
        atomic_store_explicit(&q->bottom, b + 1, memory_order_relaxed);
    }
    return x;
}
typedef task_t *(*take_fn_t)(deque_t *);

/* --- 강제 스케줄 도구: 단계 번호 + 시간 제한 대기 (멈추지 않는 gate) --- */
static pthread_mutex_t g_gm = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_gc = PTHREAD_COND_INITIALIZER;
static int g_stage;
static void stage_set(int s) {
    pthread_mutex_lock(&g_gm); g_stage = s; pthread_cond_broadcast(&g_gc); pthread_mutex_unlock(&g_gm);
}
static bool stage_wait(int s) {               /* g_stage >= s 가 될 때까지, 최대 2 초 */
    struct timespec dl;
    clock_gettime(CLOCK_REALTIME, &dl);
    dl.tv_sec += 2;
    pthread_mutex_lock(&g_gm);
    int rc = 0;
    while (g_stage < s && rc != ETIMEDOUT) rc = pthread_cond_timedwait(&g_gc, &g_gm, &dl);
    bool ok = g_stage >= s;
    pthread_mutex_unlock(&g_gm);
    return ok;
}

static deque_t g_q;                 /* 시나리오용 deque */
static task_t g_item[3];            /* A, B, X 같은 표지용 항목 (주소만 쓴다) */
static take_fn_t g_take;            /* 시나리오에서 주인이 쓰는 take (정상 / 고장) */
static task_t *g_r1, *g_r2;         /* 훅 안에서 다른 쪽이 얻은 결과 */
static int g_fired;

/* S1: 도둑이 슬롯까지 읽고 CAS 직전에 멈춘 사이, 주인이 take 를 끝까지 한다 (같은 스레드에서 끼워 넣기). */
static void s1_hook(hook_pt p, deque_t *q) {
    if (p == HK_STEAL_BEFORE_CAS && !g_fired) { g_fired = 1; g_r1 = g_take(q); }
}
/* S3: 주인이 top 을 읽은 직후 멈춘 사이, 도둑이 steal 을 두 번 끝까지 한다. */
static void s3_hook(hook_pt p, deque_t *q) {
    if (p == HK_TAKE_AFTER_TOP && !g_fired) { g_fired = 1; g_r1 = dq_steal(q); g_r2 = dq_steal(q); }
}
/* S2: 진짜 스레드 두 개. 도둑 = CAS 직전 멈춤 → 주인 = top 을 읽은 뒤 멈춤 → 도둑 CAS → 주인 CAS. */
static bool g_s2_timeout;
static void s2_hook(hook_pt p, deque_t *q) {
    (void)q;
    if (p == HK_STEAL_BEFORE_CAS) { stage_set(1); if (!stage_wait(2)) g_s2_timeout = true; }
    else if (p == HK_TAKE_AFTER_TOP) { stage_set(2); if (!stage_wait(3)) g_s2_timeout = true; }
}
static void *s2_thief(void *arg) {
    (void)arg;
    g_r1 = dq_steal(&g_q);
    stage_set(3);
    return NULL;
}

/* 결과 목록에서 항목 it 이 몇 번 나왔나 */
static int count_of(task_t *it, task_t *const *rs, int n) {
    int c = 0;
    for (int i = 0; i < n; i++) c += rs[i] == it;
    return c;
}

/* 시나리오 실행기. 각각 "누가 무엇을 얻었나" 를 rs[] 에 담는다. */
static void run_s1(take_fn_t take, task_t *rs[2]) {           /* 항목 1 개: X */
    dq_init(&g_q); dq_push(&g_q, &g_item[2]);
    g_take = take; g_fired = 0; g_r1 = DQ_EMPTY; g_hook = s1_hook;
    rs[0] = dq_steal(&g_q);   /* 도둑 */
    rs[1] = g_r1;             /* 훅 안에서 주인 */
    g_hook = NULL;
}
static bool run_s2(take_fn_t take, task_t *rs[2]) {           /* 항목 1 개: X, 실제 스레드 */
    dq_init(&g_q); dq_push(&g_q, &g_item[2]);
    g_r1 = DQ_EMPTY; g_s2_timeout = false; g_stage = 0; g_hook = s2_hook;
    pthread_t th;
    pthread_create(&th, NULL, s2_thief, NULL);
    bool ok = stage_wait(1);                                   /* 도둑이 CAS 직전에 섰다 */
    rs[1] = ok ? take(&g_q) : DQ_EMPTY;                        /* 주인 */
    if (!ok) stage_set(3);
    pthread_join(th, NULL);
    rs[0] = g_r1;
    g_hook = NULL;
    return ok && !g_s2_timeout;
}
static void run_s3(take_fn_t take, task_t *rs[3]) {           /* 항목 2 개: A(먼저), B */
    dq_init(&g_q); dq_push(&g_q, &g_item[0]); dq_push(&g_q, &g_item[1]);
    g_fired = 0; g_r1 = g_r2 = DQ_EMPTY; g_hook = s3_hook;
    rs[2] = take(&g_q);       /* 주인 */
    rs[0] = g_r1; rs[1] = g_r2;
    g_hook = NULL;
}

/* --- 실제 스레드 스트레스: 주인 1 + 도둑 3, 모든 항목 정확히 한 번 --- */
#define ST_N (200000 / SCALE)
#define ST_THIEVES 3
static task_t *g_st_items;
static atomic_int *g_st_seen;
static atomic_int g_st_done;
static deque_t g_st_q;
static long g_st_stolen[ST_THIEVES], g_st_aborts[ST_THIEVES];

static void st_consume(task_t *x) {
    if (x == DQ_EMPTY || x == DQ_ABORT) return;
    atomic_fetch_add_explicit(&g_st_seen[x - g_st_items], 1, memory_order_relaxed);
}
static void *st_thief(void *arg) {
    int id = (int)(intptr_t)arg;
    for (;;) {
        bool fin = atomic_load_explicit(&g_st_done, memory_order_acquire);
        task_t *x = dq_steal(&g_st_q);
        if (x == DQ_ABORT) { g_st_aborts[id]++; continue; }
        if (x != DQ_EMPTY) { st_consume(x); g_st_stolen[id]++; continue; }
        if (fin) break;          /* done 을 본 뒤에도 비었으면 끝 (주인은 done 전에 다 비웠다) */
        cpu_relax();
    }
    return NULL;
}
static bool stress_deque(long *owner_taken, long *stolen, long *aborts) {
    g_st_items = calloc(ST_N, sizeof *g_st_items);
    g_st_seen = calloc(ST_N, sizeof *g_st_seen);
    dq_init(&g_st_q);
    atomic_store(&g_st_done, 0);
    pthread_t th[ST_THIEVES];
    for (int i = 0; i < ST_THIEVES; i++) { g_st_stolen[i] = g_st_aborts[i] = 0;
        pthread_create(&th[i], NULL, st_thief, (void *)(intptr_t)i); }
    uint32_t rng = 12345u;
    long taken = 0;
    for (int i = 0; i < ST_N; i++) {
        while (!dq_push(&g_st_q, &g_st_items[i])) { task_t *x = dq_take(&g_st_q); if (x) { st_consume(x); taken++; } }
        if ((xorshift32(&rng) & 3u) == 0) { task_t *x = dq_take(&g_st_q); if (x) { st_consume(x); taken++; } }
    }
    for (;;) { task_t *x = dq_take(&g_st_q); if (x == DQ_EMPTY) break; st_consume(x); taken++; }
    atomic_store_explicit(&g_st_done, 1, memory_order_release);
    for (int i = 0; i < ST_THIEVES; i++) pthread_join(th[i], NULL);
    bool once = true;
    for (int i = 0; i < ST_N; i++) once &= atomic_load(&g_st_seen[i]) == 1;
    *owner_taken = taken; *stolen = 0; *aborts = 0;
    for (int i = 0; i < ST_THIEVES; i++) { *stolen += g_st_stolen[i]; *aborts += g_st_aborts[i]; }
    free(g_st_items); free(g_st_seen);
    return once;
}

/* ======================= Part C — work-stealing 스레드 풀 ======================= */
#define MAX_WORKERS 16
typedef struct pool pool_t;
struct worker {
    deque_t dq;                  /* 128 B 정렬 — 이웃 워커의 deque 와 라인을 나누지 않는다 */
    pool_t *pool;
    int id;
    uint32_t rng;
    long spawned, executed, steals, aborts, overflow;   /* 주인만 쓰는 통계 (풀이 끝난 뒤 읽음) */
};
struct pool {
    worker_t w[MAX_WORKERS];
    int n;
    _Alignas(128) atomic_long pending;   /* 아직 끝나지 않은 태스크 수 (대기 + 실행 중) */
    _Alignas(128) atomic_int abort;      /* 시간 초과 → 모두 빠져나온다 (시작 코드가 멈추지 않게) */
    int64_t deadline_ns;
};
static atomic_long g_bad_runs;           /* join 후 runs != 1 인 자식 수 */

static void task_init(task_t *t, void (*fn)(worker_t *, task_t *), int64_t a, int64_t b) {
    t->fn = fn; t->a = a; t->b = b; t->result = 0;
    atomic_init(&t->runs, 0); atomic_init(&t->done, 0);
}
/* 주어짐: 태스크 실행. done 을 release 로 올린 뒤에는 t 를 건드리지 않는다 (부모의 스택에 있을 수 있다). */
static void run_task(worker_t *w, task_t *t) {
    t->fn(w, t);
    atomic_fetch_add_explicit(&t->runs, 1, memory_order_relaxed);
    w->executed++;
    atomic_store_explicit(&t->done, 1, memory_order_release);
    atomic_fetch_sub_explicit(&w->pool->pending, 1, memory_order_release);
}
/* 주어짐: 일이 없을 때 쉬기. 64 번마다 양보, 4096 번마다 마감 시간 확인. */
static void idle_wait(worker_t *w, unsigned *idle) {
    ++*idle;
    cpu_relax();
    if ((*idle & 63u) == 0) sched_yield();
    if ((*idle & 4095u) == 0 && now_ns() > w->pool->deadline_ns)
        atomic_store_explicit(&w->pool->abort, 1, memory_order_relaxed);
}

/* C1: 자식 태스크를 내 deque 에 넣는다. pending 을 먼저 올린다.
 *     deque 가 가득 차면 그 자리에서 직접 실행한다 (고정 크기 deque 의 넘침 처리). spawned/overflow 통계도 센다. */
static void task_spawn(worker_t *w, task_t *t) {
    (void)w; (void)t; /* TODO C1 */
}

/* C2: 할 일 찾기. 먼저 내 deque(take), 비었으면 무작위 희생자(나 빼고)에게서 최대 n 번 steal.
 *     ABORT 는 aborts++ 후 다음 시도, 성공은 steals++. 없으면 NULL. */
static task_t *find_work(worker_t *w) {
    (void)w;
    return NULL; /* TODO C2 */
}

/* C3: join. 자식의 done 을 acquire 로 볼 때까지 "놀지 말고" 다른 일을 한다 (find_work → run_task).
 *     일이 없으면 idle_wait. pool->abort 가 서면 그냥 돌아간다 (결과는 틀려도 멈추지 않음). */
static void task_join(worker_t *w, task_t *t) {
    (void)w; (void)t; /* TODO C3 */
}

/* 주어짐: 워커 루프와 풀 실행. 종료 판정 = pending == 0 (acquire). */
static void worker_loop(worker_t *w) {
    unsigned idle = 0;
    while (atomic_load_explicit(&w->pool->pending, memory_order_acquire) != 0) {
        if (atomic_load_explicit(&w->pool->abort, memory_order_relaxed)) break;
        task_t *t = find_work(w);
        if (t) { run_task(w, t); idle = 0; }
        else idle_wait(w, &idle);
    }
}
static void *worker_main(void *arg) { worker_loop(arg); return NULL; }

static pool_t *pool_new(int n) {
    pool_t *p = aligned_alloc(128, sizeof(pool_t));
    if (!p) abort();
    memset(p, 0, sizeof *p);
    p->n = n;
    for (int i = 0; i < n; i++) {
        dq_init(&p->w[i].dq); p->w[i].pool = p; p->w[i].id = i; p->w[i].rng = 0x9E3779B9u * (uint32_t)(i + 1);
    }
    atomic_init(&p->pending, 0); atomic_init(&p->abort, 0);
    return p;
}
/* 루트 태스크를 워커 0 에 넣고, main 스레드가 워커 0 으로 일한다. 시간 초과면 false. */
static bool pool_run(pool_t *p, task_t *root, int64_t timeout_ms) {
    p->deadline_ns = now_ns() + timeout_ms * 1000000;
    atomic_store(&p->abort, 0);
    atomic_store(&p->pending, 1);
    p->w[0].spawned++;
    if (!dq_push(&p->w[0].dq, root)) { atomic_store(&p->pending, 0); return false; }
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 8u << 20);      /* 기다리며 도운 태스크가 스택에 쌓인다 (macOS 기본 512 KB) */
    pthread_t th[MAX_WORKERS];
    for (int i = 1; i < p->n; i++) pthread_create(&th[i], &at, worker_main, &p->w[i]);
    worker_loop(&p->w[0]);
    for (int i = 1; i < p->n; i++) pthread_join(th[i], NULL);
    pthread_attr_destroy(&at);
    return !atomic_load(&p->abort) && atomic_load_explicit(&root->done, memory_order_acquire);
}
static void pool_totals(pool_t *p, long *spawned, long *executed, long *steals, long *overflow) {
    *spawned = *executed = *steals = *overflow = 0;
    for (int i = 0; i < p->n; i++) {
        *spawned += p->w[i].spawned; *executed += p->w[i].executed;
        *steals += p->w[i].steals; *overflow += p->w[i].overflow;
    }
}

/* --- 워크로드 --- */
static void child_check(task_t *c) {
    if (atomic_load_explicit(&c->runs, memory_order_relaxed) != 1)
        atomic_fetch_add_explicit(&g_bad_runs, 1, memory_order_relaxed);
}
static int64_t fib_serial(int64_t n) { return n < 2 ? n : fib_serial(n - 1) + fib_serial(n - 2); }

/* C4: fib. a = n, b = cutoff. n <= cutoff 면 직렬. 아니면 fib(n-1) 을 spawn, fib(n-2) 는 직접 실행(같은 함수를
 *     지역 태스크로 바로 호출), 그다음 join. join 후 child_check(&자식). */
static void fib_task(worker_t *w, task_t *t) {
    (void)w;
    t->result = 0; /* TODO C4 */
}

static uint32_t *g_data;               /* psum 입력 */
static atomic_uchar *g_visit;          /* 원소별 방문 횟수 — 정확히 1 */
static int64_t g_grain = 4096;

/* C5: psum. 구간 [a, b). 길이 <= g_grain 이면 직접 더하고 g_visit[i] 를 1 씩 올린다 (relaxed).
 *     아니면 반으로 나눠 오른쪽을 spawn, 왼쪽은 직접, join, child_check, 합. */
static void sum_task(worker_t *w, task_t *t) {
    (void)w; (void)g_grain;
    t->result = 0; /* TODO C5 */
}

/* 넓은 태스크: 자식 600 개를 한꺼번에 spawn (deque 256 칸 → 넘침 경로), 그다음 모두 join. */
#define WIDE_KIDS 600
static void wide_task(worker_t *w, task_t *t) {
    task_t kids[WIDE_KIDS];
    int64_t per = (t->b - t->a) / WIDE_KIDS;
    for (int i = 0; i < WIDE_KIDS; i++) {
        int64_t lo = t->a + i * per, hi = (i == WIDE_KIDS - 1) ? t->b : lo + per;
        task_init(&kids[i], sum_task, lo, hi);
        task_spawn(w, &kids[i]);
    }
    int64_t s = 0;
    for (int i = WIDE_KIDS - 1; i >= 0; i--) { task_join(w, &kids[i]); child_check(&kids[i]); s += kids[i].result; }
    t->result = s;
}

typedef struct { bool ok; int64_t result; long spawned, executed, steals, overflow, pending; } run_info;
static run_info run_root(int workers, void (*fn)(worker_t *, task_t *), int64_t a, int64_t b) {
    pool_t *p = pool_new(workers);
    task_t root;
    task_init(&root, fn, a, b);
    run_info r;
    r.ok = pool_run(p, &root, 2000);
    r.result = root.result;
    pool_totals(p, &r.spawned, &r.executed, &r.steals, &r.overflow);
    r.pending = atomic_load(&p->pending);
    free(p);
    return r;
}
static bool visits_all_one(int64_t n) {
    bool ok = true;
    for (int64_t i = 0; i < n; i++) { ok &= atomic_load_explicit(&g_visit[i], memory_order_relaxed) == 1;
                                      atomic_store_explicit(&g_visit[i], 0, memory_order_relaxed); }
    return ok;
}

int main(void) {
    task_t *A = &g_item[0], *B = &g_item[1], *X = &g_item[2];

    /* ---- Part A: 단일 스레드 단위 테스트 ---- */
    int fail0 = g_fail;
    deque_t *q = aligned_alloc(128, sizeof(deque_t));
    dq_init(q);
    CHECK(dq_take(q) == DQ_EMPTY && dq_steal(q) == DQ_EMPTY);
    CHECK(atomic_load(&q->bottom) == 0 && atomic_load(&q->top) == 0);        /* 빈 take 는 bottom 을 되돌린다 */
    CHECK(dq_push(q, A) && dq_push(q, B) && dq_push(q, X) && dq_size(q) == 3);
    CHECK(dq_take(q) == X);                                                 /* 주인 = LIFO (최근 것) */
    CHECK(dq_steal(q) == A);                                                /* 도둑 = FIFO (오래된 것) */
    CHECK(dq_take(q) == B && atomic_load(&q->top) == 2 && atomic_load(&q->bottom) == 2);  /* 마지막 = CAS 경로 */
    CHECK(dq_take(q) == DQ_EMPTY && dq_size(q) == 0);
    {   /* 가득 참: DQ_CAP 개까지, 그다음 false, 하나 훔치면 다시 된다 */
        task_t *items = calloc(DQ_CAP + 1, sizeof *items);
        bool all = true;
        for (int i = 0; i < DQ_CAP; i++) all &= dq_push(q, &items[i]);
        CHECK(all && !dq_push(q, &items[DQ_CAP]));
        CHECK(dq_steal(q) == &items[0] && dq_push(q, &items[DQ_CAP]));
        CHECK(dq_take(q) == &items[DQ_CAP] && dq_take(q) == &items[DQ_CAP - 1]);
        while (dq_take(q) != DQ_EMPTY) { }
        free(items);
    }
    {   /* 슬롯 인덱스가 배열을 여러 바퀴 돈다: 200 push / 100 steal(앞에서) / 100 take(뒤에서) × 10 */
        task_t *items = calloc(2000, sizeof *items);
        bool order = true;
        int next = 0;
        int64_t top0 = atomic_load(&q->top);
        for (int r = 0; r < 10; r++) {
            int base = next;
            for (int i = 0; i < 200; i++) order &= dq_push(q, &items[next++]);
            for (int i = 0; i < 100; i++) order &= dq_steal(q) == &items[base + i];
            for (int i = 0; i < 100; i++) order &= dq_take(q) == &items[base + 199 - i];
        }
        CHECK(order && dq_size(q) == 0 && atomic_load(&q->top) - top0 == 10 * 101);   /* 마지막 take 마다 CAS +1 */
        free(items);
    }
    free(q);
    bool unit_ok = g_fail == fail0;

    /* ---- Part B: 강제 스케줄 ---- */
    task_t *rs[3];
    run_s1(dq_take, rs);      /* 도둑이 CAS 직전에 멈춘 사이 주인이 마지막 하나를 take → 주인 승 */
    CHECK(rs[1] == X && rs[0] == DQ_ABORT && count_of(X, rs, 2) == 1);
    CHECK(atomic_load(&g_q.top) == 1 && atomic_load(&g_q.bottom) == 1);
    bool s2ok = run_s2(dq_take, rs);   /* 도둑 CAS 가 주인의 top 읽기와 CAS 사이에 → 도둑 승 */
    CHECK(s2ok && rs[0] == X && rs[1] == DQ_EMPTY && count_of(X, rs, 2) == 1);
    CHECK(atomic_load(&g_q.top) == 1 && atomic_load(&g_q.bottom) == 1);
    run_s3(dq_take, rs);      /* 항목 2 개, 주인이 top 을 읽은 직후 도둑이 두 번 → A 는 도둑, B 는 주인 */
    CHECK(rs[0] == A && rs[1] == DQ_EMPTY && rs[2] == B);
    CHECK(count_of(A, rs, 3) == 1 && count_of(B, rs, 3) == 1 && dq_size(&g_q) == 0);
    /* 이빨 확인: 고장 낸 take(주어진 코드)는 같은 스케줄에서 중복을 낸다 — 내 push/steal 이 맞아야 재현된다 */
    run_s1(dq_take_nocas, rs);
    CHECK(count_of(X, rs, 2) == 2 && atomic_load(&g_q.top) > atomic_load(&g_q.bottom));
    run_s3(dq_take_reordered, rs);
    CHECK(count_of(B, rs, 3) == 2 && rs[0] == A);

    if (unit_ok) {
        long taken, stolen, aborts;
        bool once = stress_deque(&taken, &stolen, &aborts);
        CHECK(once && taken + stolen == ST_N);
        printf("     (stress: owner %ld, stolen %ld, steal aborts %ld)\n", taken, stolen, aborts);
    } else {
        printf("SKIP 1 stress check (Part B): Part A unit checks failed\n");
    }

    /* ---- Part C: 스레드 풀 ---- */
    if (!unit_ok) {
        printf("SKIP 9 pool checks (Part C): Part A unit checks failed\n");
    } else {
        const int64_t N = (1 << 20) / SCALE;
        g_data = malloc((size_t)N * sizeof *g_data);
        g_visit = calloc((size_t)N, sizeof *g_visit);
        uint32_t s = 2463534242u;
        uint64_t want = 0;
        for (int64_t i = 0; i < N; i++) { g_data[i] = xorshift32(&s) & 0xFFFFu; want += g_data[i]; }

        int fibn = SCALE > 1 ? 22 : 25;
        atomic_store(&g_bad_runs, 0);
        run_info r1 = run_root(1, fib_task, fibn, 12);
        CHECK(r1.ok && r1.result == fib_serial(fibn) && r1.spawned == r1.executed && r1.steals == 0);
        run_info r8 = run_root(8, fib_task, fibn, 12);
        CHECK(r8.ok && r8.result == fib_serial(fibn) && r8.spawned == r8.executed && r8.pending == 0);
        CHECK(atomic_load(&g_bad_runs) == 0 && r8.spawned == r1.spawned);   /* 태스크 수는 스케줄과 무관 */
        printf("     (fib(%d) cutoff 12: tasks %ld, steals with 8 workers %ld)\n", fibn, r8.spawned, r8.steals);

        run_info p1 = run_root(1, sum_task, 0, N);
        CHECK(p1.ok && (uint64_t)p1.result == want && visits_all_one(N));
        run_info p6 = run_root(6, sum_task, 0, N);
        CHECK(p6.ok && (uint64_t)p6.result == want && visits_all_one(N) && p6.spawned == p6.executed);
        CHECK(atomic_load(&g_bad_runs) == 0);

        run_info w1 = run_root(1, wide_task, 0, N);   /* 600 자식 > 256 칸 → 344 개는 넘침 = 직접 실행 */
        CHECK(w1.ok && (uint64_t)w1.result == want && visits_all_one(N) && w1.overflow == WIDE_KIDS - DQ_CAP);
        run_info w4 = run_root(4, wide_task, 0, N);   /* 도둑이 있으면 넘침 수는 실행마다 다르다 → 정확성만 */
        CHECK(w4.ok && (uint64_t)w4.result == want && visits_all_one(N) && w4.spawned == w4.executed);
        CHECK(atomic_load(&g_bad_runs) == 0);
        printf("     (wide: 600 kids, overflow ran inline %ld, steals %ld)\n", w4.overflow, w4.steals);
        free(g_data); free((void *)g_visit);
    }

    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
