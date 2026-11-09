/* OS-A05 추가 연습 — 한 번에 검증
 *
 *   연습 1  논문 그림 1 그대로의 size_t 인덱스: 빈 새 deque 에서 take / steal 이 쓰레기를 돌려준다 → 비교만 고친 판
 *   연습 2  늘어나는 deque: 가득 차면 2 배 배열로 복사 후 release 게시, 도둑은 bottom 다음에 배열 포인터를 acquire.
 *           옛 배열은 끝날 때 해제 (실행 중 회수는 LLAT-M03 hazard pointer / OS-A03 epoch 의 몫)
 *   연습 3  분리(detached) 태스크 parallel_for: join 이 없으니 pending 카운터만으로 끝을 안다
 *   연습 4  병렬 병합 정렬 (spawn/join), qsort 결과와 바이트 단위로 같아야 한다
 * 아래 deque · 풀 코드는 solution.c 와 같다 (훅만 뺐다).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr
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

static void dq_init(deque_t *q) {
    atomic_init(&q->top, 0);
    atomic_init(&q->bottom, 0);
    for (int i = 0; i < DQ_CAP; i++) atomic_init(&q->buf[i], NULL);
}

/* A1: 주인만 호출. 가득 차면 false (고정 크기 — 성장은 연습 2). */
static bool dq_push(deque_t *q, task_t *x) {
    int64_t b = atomic_load_explicit(&q->bottom, memory_order_relaxed);   /* 내 변수: 내가 마지막에 쓴 값 */
    int64_t t = atomic_load_explicit(&q->top, memory_order_acquire);      /* 도둑이 슬롯을 다 읽었음을 본다 */
    if (b - t >= DQ_CAP) return false;
    atomic_store_explicit(&q->buf[b & DQ_MASK], x, memory_order_relaxed);
    atomic_store_explicit(&q->bottom, b + 1, memory_order_release);       /* 논문: fence(release) + relaxed store */
    return true;
}

/* A2: 주인만 호출. bottom 쪽(가장 최근)에서 꺼낸다. 비었으면 DQ_EMPTY. */
static task_t *dq_take(deque_t *q) {
    int64_t b = atomic_load_explicit(&q->bottom, memory_order_relaxed) - 1;
    atomic_store_explicit(&q->bottom, b, memory_order_relaxed);           /* ① 먼저 "예약" 을 알린다 */
    atomic_thread_fence(memory_order_seq_cst);                            /* ② store → load 순서 (SB 금지) */
    int64_t t = atomic_load_explicit(&q->top, memory_order_relaxed);      /* ③ 그다음 top 을 본다 */
    task_t *x;
    if (t <= b) {                                                         /* 하나 이상 */
        x = atomic_load_explicit(&q->buf[b & DQ_MASK], memory_order_relaxed);
        if (t == b) {                                                     /* 마지막 하나: 도둑과 CAS 로 결판 */
            if (!atomic_compare_exchange_strong_explicit(&q->top, &t, t + 1,
                    memory_order_seq_cst, memory_order_relaxed))
                x = DQ_EMPTY;                                             /* 도둑이 가져감 */
            atomic_store_explicit(&q->bottom, b + 1, memory_order_relaxed);   /* 어느 쪽이든 빈 상태 top == bottom */
        }
    } else {                                                              /* 비어 있었음: 되돌린다 */
        x = DQ_EMPTY;
        atomic_store_explicit(&q->bottom, b + 1, memory_order_relaxed);
    }
    return x;
}

/* A3: 아무 스레드나 호출. top 쪽(가장 오래된)에서 훔친다. DQ_EMPTY / DQ_ABORT / 태스크. */
static task_t *dq_steal(deque_t *q) {
    int64_t t = atomic_load_explicit(&q->top, memory_order_acquire);
    atomic_thread_fence(memory_order_seq_cst);                            /* top 읽기 → bottom 읽기 순서 */
    int64_t b = atomic_load_explicit(&q->bottom, memory_order_acquire);   /* push 의 release 와 짝 → 슬롯 내용이 보인다 */
    if (t >= b) return DQ_EMPTY;
    task_t *x = atomic_load_explicit(&q->buf[t & DQ_MASK], memory_order_relaxed);   /* CAS 전에 읽어야 한다 */
    if (!atomic_compare_exchange_strong_explicit(&q->top, &t, t + 1,
            memory_order_seq_cst, memory_order_relaxed))
        return DQ_ABORT;                                                  /* 다른 도둑이나 주인이 먼저 */
    return x;
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
    atomic_fetch_add_explicit(&w->pool->pending, 1, memory_order_relaxed);
    w->spawned++;
    if (!dq_push(&w->dq, t)) { w->overflow++; run_task(w, t); }
}

/* C2: 할 일 찾기. 먼저 내 deque(take), 비었으면 무작위 희생자(나 빼고)에게서 최대 n 번 steal.
 *     ABORT 는 aborts++ 후 다음 시도, 성공은 steals++. 없으면 NULL. */
static task_t *find_work(worker_t *w) {
    task_t *t = dq_take(&w->dq);
    if (t != DQ_EMPTY) return t;
    pool_t *p = w->pool;
    for (int i = 1; i < p->n; i++) {
        int v = (int)(xorshift32(&w->rng) % (uint32_t)(p->n - 1));
        if (v >= w->id) v++;                       /* 나는 건너뛴다 */
        t = dq_steal(&p->w[v].dq);
        if (t == DQ_ABORT) { w->aborts++; continue; }
        if (t != DQ_EMPTY) { w->steals++; return t; }
    }
    return NULL;
}

/* C3: join. 자식의 done 을 acquire 로 볼 때까지 "놀지 말고" 다른 일을 한다 (find_work → run_task).
 *     일이 없으면 idle_wait. pool->abort 가 서면 그냥 돌아간다 (결과는 틀려도 멈추지 않음). */
static void task_join(worker_t *w, task_t *t) {
    unsigned idle = 0;
    while (!atomic_load_explicit(&t->done, memory_order_acquire)) {
        if (atomic_load_explicit(&w->pool->abort, memory_order_relaxed)) return;
        task_t *x = find_work(w);
        if (x) { run_task(w, x); idle = 0; }
        else idle_wait(w, &idle);
    }
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


static void child_check(task_t *c) {
    if (atomic_load_explicit(&c->runs, memory_order_relaxed) != 1)
        atomic_fetch_add_explicit(&g_bad_runs, 1, memory_order_relaxed);
}

/* ======================= 연습 1 — 논문 그림 그대로의 size_t 인덱스 ======================= */
#define U_CAP 8
typedef struct {
    _Atomic(size_t) top, bottom;
    _Atomic(task_t *) buf[U_CAP];
} udq_t;
static task_t g_sentinel;                  /* 슬롯에 남아 있던 "옛 값" 표지 */
static void udq_init(udq_t *q, size_t start) {
    atomic_init(&q->top, start); atomic_init(&q->bottom, start);
    for (int i = 0; i < U_CAP; i++) atomic_init(&q->buf[i], &g_sentinel);
}
static void udq_push(udq_t *q, task_t *x) {        /* 테스트에서 넘치지 않게만 쓴다 */
    size_t b = atomic_load_explicit(&q->bottom, memory_order_relaxed);
    atomic_store_explicit(&q->buf[b % U_CAP], x, memory_order_relaxed);
    atomic_store_explicit(&q->bottom, b + 1, memory_order_release);
}
/* 논문 그림 1 의 비교 그대로 (t <= b, t < b) — bottom == 0 에서 b = SIZE_MAX 가 된다 */
static task_t *udq_take_paper(udq_t *q) {
    size_t b = atomic_load_explicit(&q->bottom, memory_order_relaxed) - 1;
    atomic_store_explicit(&q->bottom, b, memory_order_relaxed);
    atomic_thread_fence(memory_order_seq_cst);
    size_t t = atomic_load_explicit(&q->top, memory_order_relaxed);
    task_t *x;
    if (t <= b) {
        x = atomic_load_explicit(&q->buf[b % U_CAP], memory_order_relaxed);
        if (t == b) {
            if (!atomic_compare_exchange_strong_explicit(&q->top, &t, t + 1, memory_order_seq_cst, memory_order_relaxed))
                x = DQ_EMPTY;
            atomic_store_explicit(&q->bottom, b + 1, memory_order_relaxed);
        }
    } else { x = DQ_EMPTY; atomic_store_explicit(&q->bottom, b + 1, memory_order_relaxed); }
    return x;
}
static task_t *udq_steal_paper(udq_t *q) {
    size_t t = atomic_load_explicit(&q->top, memory_order_acquire);
    atomic_thread_fence(memory_order_seq_cst);
    size_t b = atomic_load_explicit(&q->bottom, memory_order_acquire);
    if (!(t < b)) return DQ_EMPTY;
    task_t *x = atomic_load_explicit(&q->buf[t % U_CAP], memory_order_relaxed);
    if (!atomic_compare_exchange_strong_explicit(&q->top, &t, t + 1, memory_order_seq_cst, memory_order_relaxed))
        return DQ_ABORT;
    return x;
}
/* 고친 판: 부호 없는 인덱스는 그대로, 비교만 "차이를 부호 있게" — 2^63 미만 거리면 wrap 에도 맞다 */
static task_t *udq_take_fixed(udq_t *q) {
    size_t b = atomic_load_explicit(&q->bottom, memory_order_relaxed) - 1;
    atomic_store_explicit(&q->bottom, b, memory_order_relaxed);
    atomic_thread_fence(memory_order_seq_cst);
    size_t t = atomic_load_explicit(&q->top, memory_order_relaxed);
    task_t *x;
    if ((ptrdiff_t)(b - t) >= 0) {
        x = atomic_load_explicit(&q->buf[b % U_CAP], memory_order_relaxed);
        if (t == b) {
            if (!atomic_compare_exchange_strong_explicit(&q->top, &t, t + 1, memory_order_seq_cst, memory_order_relaxed))
                x = DQ_EMPTY;
            atomic_store_explicit(&q->bottom, b + 1, memory_order_relaxed);
        }
    } else { x = DQ_EMPTY; atomic_store_explicit(&q->bottom, b + 1, memory_order_relaxed); }
    return x;
}
static task_t *udq_steal_fixed(udq_t *q) {
    size_t t = atomic_load_explicit(&q->top, memory_order_acquire);
    atomic_thread_fence(memory_order_seq_cst);
    size_t b = atomic_load_explicit(&q->bottom, memory_order_acquire);
    if ((ptrdiff_t)(b - t) <= 0) return DQ_EMPTY;
    task_t *x = atomic_load_explicit(&q->buf[t % U_CAP], memory_order_relaxed);
    if (!atomic_compare_exchange_strong_explicit(&q->top, &t, t + 1, memory_order_seq_cst, memory_order_relaxed))
        return DQ_ABORT;
    return x;
}

/* ======================= 연습 2 — 늘어나는 deque (배열 교체) ======================= */
typedef struct garr { int64_t cap; struct garr *prev; _Atomic(task_t *) slot[]; } garr_t;
typedef struct {
    _Alignas(128) _Atomic(int64_t) top;
    _Alignas(128) _Atomic(int64_t) bottom;
    _Alignas(128) _Atomic(garr_t *) arr;
    int grows;                               /* 주인만 */
} gdq_t;
static garr_t *garr_new(int64_t cap, garr_t *prev) {
    garr_t *a = malloc(sizeof *a + (size_t)cap * sizeof a->slot[0]);
    if (!a) abort();
    a->cap = cap; a->prev = prev;
    for (int64_t i = 0; i < cap; i++) atomic_init(&a->slot[i], NULL);
    return a;
}
static void gdq_init(gdq_t *q, int64_t cap) {
    atomic_init(&q->top, 0); atomic_init(&q->bottom, 0); atomic_init(&q->arr, garr_new(cap, NULL)); q->grows = 0;
}
static void gdq_destroy(gdq_t *q) {          /* 옛 배열은 여기서야 해제 (실행 중 회수 = LLAT-M03 / OS-A03) */
    garr_t *a = atomic_load(&q->arr);
    while (a) { garr_t *p = a->prev; free(a); a = p; }
}
static garr_t *gdq_grow(gdq_t *q, garr_t *a, int64_t t, int64_t b) {
    garr_t *n = garr_new(a->cap * 2, a);
    for (int64_t i = t; i < b; i++)
        atomic_store_explicit(&n->slot[i & (n->cap - 1)],
            atomic_load_explicit(&a->slot[i & (a->cap - 1)], memory_order_relaxed), memory_order_relaxed);
    atomic_store_explicit(&q->arr, n, memory_order_release);   /* 복사한 내용과 함께 게시 */
    q->grows++;
    return n;
}
static void gdq_push(gdq_t *q, task_t *x) {
    int64_t b = atomic_load_explicit(&q->bottom, memory_order_relaxed);
    int64_t t = atomic_load_explicit(&q->top, memory_order_acquire);
    garr_t *a = atomic_load_explicit(&q->arr, memory_order_relaxed);   /* 주인이 마지막에 쓴 값 */
    if (b - t >= a->cap) a = gdq_grow(q, a, t, b);
    atomic_store_explicit(&a->slot[b & (a->cap - 1)], x, memory_order_relaxed);
    atomic_store_explicit(&q->bottom, b + 1, memory_order_release);
}
static task_t *gdq_take(gdq_t *q) {
    int64_t b = atomic_load_explicit(&q->bottom, memory_order_relaxed) - 1;
    garr_t *a = atomic_load_explicit(&q->arr, memory_order_relaxed);
    atomic_store_explicit(&q->bottom, b, memory_order_relaxed);
    atomic_thread_fence(memory_order_seq_cst);
    int64_t t = atomic_load_explicit(&q->top, memory_order_relaxed);
    task_t *x;
    if (t <= b) {
        x = atomic_load_explicit(&a->slot[b & (a->cap - 1)], memory_order_relaxed);
        if (t == b) {
            if (!atomic_compare_exchange_strong_explicit(&q->top, &t, t + 1, memory_order_seq_cst, memory_order_relaxed))
                x = DQ_EMPTY;
            atomic_store_explicit(&q->bottom, b + 1, memory_order_relaxed);
        }
    } else { x = DQ_EMPTY; atomic_store_explicit(&q->bottom, b + 1, memory_order_relaxed); }
    return x;
}
static task_t *gdq_steal(gdq_t *q) {
    int64_t t = atomic_load_explicit(&q->top, memory_order_acquire);
    atomic_thread_fence(memory_order_seq_cst);
    int64_t b = atomic_load_explicit(&q->bottom, memory_order_acquire);
    if (t >= b) return DQ_EMPTY;
    garr_t *a = atomic_load_explicit(&q->arr, memory_order_acquire);   /* bottom 다음에 (논문: consume) */
    task_t *x = atomic_load_explicit(&a->slot[t & (a->cap - 1)], memory_order_relaxed);
    if (!atomic_compare_exchange_strong_explicit(&q->top, &t, t + 1, memory_order_seq_cst, memory_order_relaxed))
        return DQ_ABORT;
    return x;
}
#define G_N (100000 / SCALE)
static gdq_t g_gq;
static task_t *g_gitems;
static atomic_int *g_gseen;
static atomic_int g_gdone;
static void *g_thief(void *arg) {
    (void)arg;
    for (;;) {
        bool fin = atomic_load_explicit(&g_gdone, memory_order_acquire);
        task_t *x = gdq_steal(&g_gq);
        if (x == DQ_ABORT) continue;
        if (x != DQ_EMPTY) { atomic_fetch_add_explicit(&g_gseen[x - g_gitems], 1, memory_order_relaxed); continue; }
        if (fin) break;
        cpu_relax();
    }
    return NULL;
}

/* ======================= 연습 3 — 분리(detached) 태스크 + pending 종료 판정 ======================= */
static atomic_uchar *g_hit;
static task_t *g_arena;
static atomic_long g_arena_next;
static long g_arena_cap;
static int64_t g_for_grain = 512;
static task_t *arena_alloc(void) {
    long i = atomic_fetch_add_explicit(&g_arena_next, 1, memory_order_relaxed);
    if (i >= g_arena_cap) abort();
    return &g_arena[i];
}
static void for_task(worker_t *w, task_t *t) {     /* join 없음: 쪼개서 뿌리고 바로 끝난다 */
    if (t->b - t->a <= g_for_grain) {
        for (int64_t i = t->a; i < t->b; i++) atomic_fetch_add_explicit(&g_hit[i], 1, memory_order_relaxed);
        return;
    }
    int64_t mid = t->a + (t->b - t->a) / 2;
    task_t *l = arena_alloc(), *r = arena_alloc();
    task_init(l, for_task, t->a, mid);
    task_init(r, for_task, mid, t->b);
    task_spawn(w, l);
    task_spawn(w, r);
}

/* ======================= 연습 4 — 병렬 병합 정렬 (spawn/join) ======================= */
static int32_t *g_arr, *g_tmp;
static int cmp_i32(const void *x, const void *y) { int32_t a = *(const int32_t *)x, b = *(const int32_t *)y; return (a > b) - (a < b); }
static void merge(int64_t lo, int64_t mid, int64_t hi) {
    int64_t i = lo, j = mid, k = lo;
    while (i < mid && j < hi) g_tmp[k++] = g_arr[i] <= g_arr[j] ? g_arr[i++] : g_arr[j++];
    while (i < mid) g_tmp[k++] = g_arr[i++];
    while (j < hi) g_tmp[k++] = g_arr[j++];
    memcpy(&g_arr[lo], &g_tmp[lo], (size_t)(hi - lo) * sizeof g_arr[0]);
}
static void sort_task(worker_t *w, task_t *t) {
    if (t->b - t->a <= 2048) { qsort(&g_arr[t->a], (size_t)(t->b - t->a), sizeof g_arr[0], cmp_i32); return; }
    int64_t mid = t->a + (t->b - t->a) / 2;
    task_t l, r;
    task_init(&l, sort_task, t->a, mid);
    task_init(&r, sort_task, mid, t->b);
    task_spawn(w, &r);
    sort_task(w, &l);
    task_join(w, &r);
    child_check(&r);
    merge(t->a, mid, t->b);                         /* 두 반쪽이 모두 끝난 뒤 (join 의 acquire 가 r 의 쓰기를 보여 준다) */
}

typedef struct { bool ok; long spawned, executed, pending; } run_info;
static run_info run_root(int workers, void (*fn)(worker_t *, task_t *), int64_t a, int64_t b) {
    pool_t *p = pool_new(workers);
    task_t root;
    task_init(&root, fn, a, b);
    run_info r;
    r.ok = pool_run(p, &root, 2000);
    long st, ov;
    pool_totals(p, &r.spawned, &r.executed, &st, &ov);
    r.pending = atomic_load(&p->pending);
    free(p);
    return r;
}

int main(void) {
    /* 연습 1 */
    udq_t u;
    udq_init(&u, 0);
    CHECK(udq_take_paper(&u) == &g_sentinel);                     /* 빈 새 deque 에서 옛 슬롯 값을 돌려준다 */
    udq_init(&u, 0);
    atomic_store(&u.bottom, SIZE_MAX);                            /* 주인의 take 가 bottom 을 0-1 로 내린 순간 */
    CHECK(udq_steal_paper(&u) == &g_sentinel && atomic_load(&u.top) == 1);   /* 도둑도 쓰레기를 훔치고 top 을 올린다 */
    udq_init(&u, 0);
    CHECK(udq_take_fixed(&u) == DQ_EMPTY && atomic_load(&u.bottom) == 0);
    atomic_store(&u.bottom, SIZE_MAX);
    CHECK(udq_steal_fixed(&u) == DQ_EMPTY && atomic_load(&u.top) == 0);
    {
        task_t it[6];
        udq_init(&u, SIZE_MAX - 2);                               /* 인덱스가 SIZE_MAX → 0 을 넘어간다 */
        for (int i = 0; i < 6; i++) udq_push(&u, &it[i]);
        bool ok = udq_steal_fixed(&u) == &it[0] && udq_steal_fixed(&u) == &it[1];
        for (int i = 5; i >= 2; i--) ok &= udq_take_fixed(&u) == &it[i];
        ok &= udq_take_fixed(&u) == DQ_EMPTY && udq_steal_fixed(&u) == DQ_EMPTY;
        CHECK(ok && atomic_load(&u.top) == 0 && atomic_load(&u.bottom) == 0);   /* 마지막 CAS 가 SIZE_MAX → 0 */
    }

    /* 연습 2 */
    {
        gdq_t q;
        gdq_init(&q, 4);
        task_t *it = calloc(5000, sizeof *it);
        for (int i = 0; i < 5000; i++) gdq_push(&q, &it[i]);
        CHECK(q.grows == 11 && atomic_load(&q.arr)->cap == 8192);   /* 4 → 8192 */
        bool ok = true;
        for (int i = 0; i < 10; i++) ok &= gdq_steal(&q) == &it[i];
        for (int i = 4999; i >= 10; i--) ok &= gdq_take(&q) == &it[i];
        CHECK(ok && gdq_take(&q) == DQ_EMPTY && gdq_steal(&q) == DQ_EMPTY);
        free(it);
        gdq_destroy(&q);
    }
    {
        gdq_init(&g_gq, 2);
        g_gitems = calloc(G_N, sizeof *g_gitems);
        g_gseen = calloc(G_N, sizeof *g_gseen);
        atomic_store(&g_gdone, 0);
        pthread_t th[3];
        for (int i = 0; i < 3; i++) pthread_create(&th[i], NULL, g_thief, NULL);
        uint32_t rng = 777u;
        for (int i = 0; i < G_N; i++) {
            gdq_push(&g_gq, &g_gitems[i]);
            if ((xorshift32(&rng) & 7u) == 0) { task_t *x = gdq_take(&g_gq); if (x) atomic_fetch_add(&g_gseen[x - g_gitems], 1); }
        }
        for (;;) { task_t *x = gdq_take(&g_gq); if (!x) break; atomic_fetch_add(&g_gseen[x - g_gitems], 1); }
        atomic_store_explicit(&g_gdone, 1, memory_order_release);
        for (int i = 0; i < 3; i++) pthread_join(th[i], NULL);
        bool once = true;
        for (int i = 0; i < G_N; i++) once &= atomic_load(&g_gseen[i]) == 1;
        CHECK(once && g_gq.grows >= 1);
        printf("     (growable stress: %d items, grows %d, final cap %lld)\n", G_N, g_gq.grows,
               (long long)atomic_load(&g_gq.arr)->cap);
        free(g_gitems); free(g_gseen);
        gdq_destroy(&g_gq);
    }

    /* 연습 3 */
    {
        const int64_t n = (1 << 18) / SCALE;
        g_hit = calloc((size_t)n, sizeof *g_hit);
        g_arena_cap = 4 * (n / g_for_grain + 1);
        g_arena = calloc((size_t)g_arena_cap, sizeof *g_arena);
        atomic_store(&g_arena_next, 0);
        run_info r = run_root(6, for_task, 0, n);
        bool once = true;
        for (int64_t i = 0; i < n; i++) once &= atomic_load_explicit(&g_hit[i], memory_order_relaxed) == 1;
        CHECK(r.ok && once && r.pending == 0);
        CHECK(r.spawned == r.executed && r.spawned == 1 + atomic_load(&g_arena_next));   /* 루트 + 모든 자식 */
        free(g_arena); free((void *)g_hit);
    }

    /* 연습 4 */
    {
        const int64_t n = (1 << 20) / SCALE;
        g_arr = malloc((size_t)n * sizeof *g_arr);
        g_tmp = malloc((size_t)n * sizeof *g_tmp);
        int32_t *ref = malloc((size_t)n * sizeof *ref);
        uint32_t s = 42u;
        for (int64_t i = 0; i < n; i++) ref[i] = g_arr[i] = (int32_t)xorshift32(&s);
        qsort(ref, (size_t)n, sizeof ref[0], cmp_i32);
        atomic_store(&g_bad_runs, 0);
        run_info r = run_root(8, sort_task, 0, n);
        CHECK(r.ok && memcmp(ref, g_arr, (size_t)n * sizeof ref[0]) == 0);
        CHECK(atomic_load(&g_bad_runs) == 0 && r.spawned == r.executed && r.pending == 0);
        free(g_arr); free(g_tmp); free(ref);
    }

    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
