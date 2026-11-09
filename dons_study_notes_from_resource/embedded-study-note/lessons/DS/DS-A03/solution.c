/* DS-A03 Bounded MPMC 큐: Vyukov 시퀀스 방식
 *
 * 호스트 모델: pthread 하나 = 코어 하나 (멀티코어 MCU 의 코어들, 또는 여러 RTOS 태스크/ISR).
 *   Part A  Vyukov bounded MPMC (1024cores "Bounded MPMC queue"): 슬롯마다 sequence,
 *           enqueue_pos / dequeue_pos 는 CAS 로 예약. dif = seq - 기대값 의 세 경우.
 *   Part B  검증 도구(정확히 한 번 · 생산자별 순서) + 4×4 스트레스 + "강제 일시정지" 스케줄:
 *           CAS 와 sequence store 사이에서 멈춘 생산자가 소비자를 막는다 (= 엄밀한 lock-free 아님).
 *   Part C  펌웨어: 여러 ISR + main 이 올리는 이벤트 큐. ISR 은 절대 기다리지 않는다 (try + drop 카운트).
 *           단일 코어 선점 = "중첩 호출" 로 결정적으로 흉내 낸다.
 *
 * 체크는 "올바르면 매 실행 항상 성립" 하는 것만 본다. 처리량은 demo_bench.c, 깨진 판은 demo_broken.c.
 * 멀티스레드 스트레스는 단일 스레드 단위 테스트가 모두 통과해야 실행한다 (시작 코드가 멈추지 않게).
 * 스트레스 크기: 기본 생산자당 500000 개 (4×4 = 200 만), ASan/TSan 빌드는 1/10.
 *   환경 변수 DS_A03_MSGS=<생산자당 개수> 로 바꿀 수 있다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#define _POSIX_C_SOURCE 200809L
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

static int g_pass, g_fail;
static bool g_all_ok = true;         /* 하나라도 실패하면 뒤의 멀티스레드 단계는 건너뛴다 */
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; g_all_ok = false; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

#if defined(__has_feature)
#  if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
#    define DS_SANITIZED 1
#  endif
#endif
#ifndef DS_SANITIZED
#  define DS_SANITIZED 0
#endif

/* (intptr_t)(seq - pos): 부호 없는 차를 부호 있는 값으로 해석한다. 범위 밖 변환은 구현 정의
 * (C11 6.3.1.3p3) — clang/gcc 는 2의 보수 모듈러로 정의한다. 크기가 같아야 의미가 맞다. */
_Static_assert(sizeof(size_t) == sizeof(intptr_t), "size_t and intptr_t must have the same width");
_Static_assert(ATOMIC_LONG_LOCK_FREE == 2 || ATOMIC_LLONG_LOCK_FREE == 2, "atomic_size_t must be lock-free");

static inline void cpu_relax(void) {
#if defined(__aarch64__) || defined(__arm__)
    __asm__ volatile("yield" ::: "memory");
#elif defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("pause" ::: "memory");
#endif
}

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* ======================= Part A — Vyukov bounded MPMC ======================= */
#define LINE 128                     /* 이 맥: sysctl hw.cachelinesize = 128 */

typedef struct {
    atomic_size_t seq;               /* 이 슬롯의 "차례 번호" — 누가 다음에 쓸/읽을 수 있나 */
    uint64_t      data;              /* 일반(비원자) 데이터 — seq 의 release/acquire 가 보호한다 */
} cell_t;

typedef struct {
    cell_t *buf;
    size_t  mask;                    /* cap - 1, cap = 2 의 거듭제곱 ≥ 2 */
    _Alignas(LINE) atomic_size_t enq_pos;   /* 다음 enqueue 위치 (생산자끼리만 경쟁) */
    _Alignas(LINE) atomic_size_t deq_pos;   /* 다음 dequeue 위치 (소비자끼리만 경쟁) */
    char pad_[LINE - sizeof(atomic_size_t)];
} mpmc_t;

/* 테스트 전용 훅: enqueue 의 CAS 성공 직후, 데이터 쓰기 전에 불린다 ("이 순간 선점됐다면?").
 * 실전 코드에는 없다 (demo_bench.c 판에는 없음). */
typedef void (*claim_hook_fn)(size_t pos);
static _Atomic(claim_hook_fn) g_claim_hook;
static inline void after_claim(size_t pos) {
    claim_hook_fn h = atomic_load_explicit(&g_claim_hook, memory_order_relaxed);
    if (h) h(pos);
}

/* dif = seq - expect 의 세 경우. enqueue 는 expect = pos, dequeue 는 expect = pos + 1. */
typedef enum { SLOT_NOT_READY = -1, SLOT_MINE = 0, SLOT_STALE = 1 } slot_case_t;

/* A1: (intptr_t)(seq - expect) 의 부호로 분류. 0 → MINE, 음수 → NOT_READY(가득/빔/아직 게시 전),
 *     양수 → STALE(내가 읽은 pos 가 낡았다: 다른 스레드가 이미 이 위치를 가져갔다). wrap 에도 맞아야 한다. */
static slot_case_t slot_case(size_t seq, size_t expect) {
    intptr_t dif = (intptr_t)(seq - expect);
    if (dif == 0) return SLOT_MINE;
    return dif < 0 ? SLOT_NOT_READY : SLOT_STALE;
}

/* A2: cap 이 2 의 거듭제곱이고 ≥ 2 일 때만 true. 슬롯 i 의 seq = i, 두 위치 = 0. */
static bool mpmc_init(mpmc_t *q, cell_t *cells, size_t cap) {
    if (cap < 2 || (cap & (cap - 1)) != 0) return false;
    q->buf = cells;
    q->mask = cap - 1;
    for (size_t i = 0; i < cap; i++) {
        atomic_init(&cells[i].seq, i);
        cells[i].data = 0;
    }
    atomic_init(&q->enq_pos, 0);
    atomic_init(&q->deq_pos, 0);
    return true;
}

/* A3: 한 번 시도. 가득이면 false. 성공하면 CAS 직후 after_claim(pos) 를 부르고 나서 데이터를 쓴다. */
static bool mpmc_try_enqueue(mpmc_t *q, uint64_t v) {
    size_t pos = atomic_load_explicit(&q->enq_pos, memory_order_relaxed);
    cell_t *c;
    for (;;) {
        c = &q->buf[pos & q->mask];
        size_t seq = atomic_load_explicit(&c->seq, memory_order_acquire);
        slot_case_t k = slot_case(seq, pos);
        if (k == SLOT_MINE) {
            if (atomic_compare_exchange_weak_explicit(&q->enq_pos, &pos, pos + 1,
                                                      memory_order_relaxed, memory_order_relaxed))
                break;                       /* 실패하면 pos 에 현재 값이 들어와 있다 → 다시 */
        } else if (k == SLOT_NOT_READY) {
            return false;                    /* 한 바퀴 전 소비자가 아직 안 비웠다 = 가득 */
        } else {
            pos = atomic_load_explicit(&q->enq_pos, memory_order_relaxed);
        }
    }
    after_claim(pos);
    c->data = v;
    atomic_store_explicit(&c->seq, pos + 1, memory_order_release);   /* "pos 에 데이터 있음" 게시 */
    return true;
}

/* A4: 한 번 시도. 비었으면(또는 그 자리 생산자가 아직 게시 전이면) false. */
static bool mpmc_try_dequeue(mpmc_t *q, uint64_t *out) {
    size_t pos = atomic_load_explicit(&q->deq_pos, memory_order_relaxed);
    cell_t *c;
    for (;;) {
        c = &q->buf[pos & q->mask];
        size_t seq = atomic_load_explicit(&c->seq, memory_order_acquire);
        slot_case_t k = slot_case(seq, pos + 1);
        if (k == SLOT_MINE) {
            if (atomic_compare_exchange_weak_explicit(&q->deq_pos, &pos, pos + 1,
                                                      memory_order_relaxed, memory_order_relaxed))
                break;
        } else if (k == SLOT_NOT_READY) {
            return false;
        } else {
            pos = atomic_load_explicit(&q->deq_pos, memory_order_relaxed);
        }
    }
    *out = c->data;
    atomic_store_explicit(&c->seq, pos + q->mask + 1, memory_order_release);  /* 다음 바퀴 생산자에게 */
    return true;
}

/* ======================= Part B — 검증 도구 + 강제 스케줄 ======================= */
/* 메시지 = (생산자 번호 << 32) | 생산자 안의 일련번호. */
static inline uint64_t msg_make(unsigned p, uint32_t i) { return ((uint64_t)p << 32) | i; }

/* B1: 한 소비자가 본 메시지 하나를 검사. last1[p] = 생산자 p 에서 마지막으로 본 번호 + 1 (0 = 아직 없음).
 *     p ≥ nprod 이거나, 번호가 이전보다 크지 않으면 false. 맞으면 last1 갱신 후 true. */
static bool order_step(uint32_t *last1, unsigned nprod, uint64_t v) {
    unsigned p = (unsigned)(v >> 32);
    uint32_t i = (uint32_t)v;
    if (p >= nprod) return false;
    if ((uint64_t)i + 1u <= last1[p]) return false;
    last1[p] = i + 1u;
    return true;
}

/* B2: hits[k] = 메시지 k 를 받은 횟수. 0 인 칸 수 → *lost, 2 이상인 칸 수 → *dup. 둘 다 0 이면 true. */
static bool exactly_once(const unsigned char *hits, size_t n, size_t *lost, size_t *dup) {
    size_t l = 0, d = 0;
    for (size_t k = 0; k < n; k++) {
        if (hits[k] == 0) l++;
        else if (hits[k] > 1) d++;
    }
    *lost = l;
    *dup = d;
    return l == 0 && d == 0;
}

/* B3: 비어 있지 않을 때까지 최대 max_tries 번 try_dequeue (사이에 cpu_relax). 받으면 true. */
static bool mpmc_dequeue_bounded(mpmc_t *q, uint64_t *out, unsigned max_tries) {
    for (unsigned t = 0; t < max_tries; t++) {
        if (mpmc_try_dequeue(q, out)) return true;
        cpu_relax();
    }
    return false;
}

/* ======================= Part C — 펌웨어: ISR 이벤트 큐 ======================= */
/* 이벤트 = src(8) | type(8) | ts(16) | arg(32). 여러 ISR(서로 다른 우선순위) + main 이 올리고 main 이 비운다. */
static inline uint64_t ev_pack(uint8_t src, uint8_t type, uint16_t ts, uint32_t arg) {
    return ((uint64_t)src << 56) | ((uint64_t)type << 48) | ((uint64_t)ts << 32) | arg;
}
static inline uint8_t ev_src(uint64_t e) { return (uint8_t)(e >> 56); }

typedef struct {
    mpmc_t q;
    cell_t cells[8];
    atomic_uint drops;               /* 가득 차서 버린 이벤트 수 (ISR 은 기다리지 않는다) */
    atomic_uint posted;
} evq_t;

/* C1: ISR/태스크 어디서든 부를 수 있는 게시. try 한 번만: 성공 → posted++, true. 가득 → drops++, false.
 *     절대 돌며 기다리지 않는다 (높은 우선순위 ISR 이 낮은 쪽이 쥔 슬롯을 기다리면 영원히 끝나지 않는다). */
static bool evq_post(evq_t *e, uint64_t ev) {
    if (mpmc_try_enqueue(&e->q, ev)) {
        atomic_fetch_add_explicit(&e->posted, 1u, memory_order_relaxed);
        return true;
    }
    atomic_fetch_add_explicit(&e->drops, 1u, memory_order_relaxed);
    return false;
}

/* C2: main 루프에서 비우기. 최대 max 개를 out 에, 받은 개수를 돌려준다 (비었거나 게시 전 슬롯에서 멈춤). */
static size_t evq_drain(evq_t *e, uint64_t *out, size_t max) {
    size_t n = 0;
    while (n < max && mpmc_try_dequeue(&e->q, &out[n])) n++;
    return n;
}

/* ======================= 테스트 하네스 (수정하지 않는다) ======================= */

/* ---- 강제 일시정지 게이트: 첫 번째 claim 에서만 멈춘다 ---- */
static pthread_mutex_t gate_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_c = PTHREAD_COND_INITIALIZER;
static int gate_armed, gate_claimed, gate_open, gate_done;
static size_t gate_pos;

static struct timespec abs_deadline(double timeout_s) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    double t = (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9 + timeout_s;
    ts.tv_sec = (time_t)t;
    ts.tv_nsec = (long)((t - (double)ts.tv_sec) * 1e9);
    return ts;
}
static void gate_wait_until(int *flag, double timeout_s) {   /* gate_m 을 쥔 채 호출 */
    struct timespec ts = abs_deadline(timeout_s);
    while (!*flag)
        if (pthread_cond_timedwait(&gate_c, &gate_m, &ts) == ETIMEDOUT) break;
}
static void pause_hook(size_t pos) {
    pthread_mutex_lock(&gate_m);
    if (gate_armed) {
        gate_armed = 0;
        gate_claimed = 1;
        gate_pos = pos;
        pthread_cond_broadcast(&gate_c);
        gate_wait_until(&gate_open, 5.0);       /* main 이 열어 줄 때까지 "선점된 채" */
    }
    pthread_mutex_unlock(&gate_m);
}
typedef struct { mpmc_t *q; uint64_t v; bool ok; } enq_arg_t;
static void *enq_once_thread(void *p) {
    enq_arg_t *a = p;
    a->ok = mpmc_try_enqueue(a->q, a->v);
    pthread_mutex_lock(&gate_m);
    gate_done = 1;
    pthread_cond_broadcast(&gate_c);
    pthread_mutex_unlock(&gate_m);
    return NULL;
}

/* ---- 스트레스 ---- */
typedef struct {
    mpmc_t *q;
    unsigned id, nprod, ncons;
    uint32_t per_prod;
    atomic_size_t *consumed;
    _Atomic unsigned char *hits;
    atomic_bool *abort_flag;
    double deadline;
    uint64_t sum;
    size_t got, order_bad;
} stress_arg_t;

static void *stress_producer(void *p) {
    stress_arg_t *a = p;
    for (uint32_t i = 0; i < a->per_prod; i++) {
        uint64_t v = msg_make(a->id, i);
        unsigned fails = 0;
        while (!mpmc_try_enqueue(a->q, v)) {
            if (atomic_load_explicit(a->abort_flag, memory_order_relaxed)) return NULL;
            if ((++fails & 63u) == 0) {
                sched_yield();
                if (now_s() > a->deadline) { atomic_store(a->abort_flag, true); return NULL; }
            } else cpu_relax();
        }
        a->sum += v;
    }
    return NULL;
}
static void *stress_consumer(void *p) {
    stress_arg_t *a = p;
    uint32_t last1[16] = {0};
    size_t total = (size_t)a->nprod * a->per_prod;
    unsigned fails = 0;
    while (atomic_load_explicit(a->consumed, memory_order_relaxed) < total) {
        uint64_t v;
        if (mpmc_try_dequeue(a->q, &v)) {
            atomic_fetch_add_explicit(a->consumed, 1, memory_order_relaxed);
            if (!order_step(last1, a->nprod, v)) a->order_bad++;
            unsigned pr = (unsigned)(v >> 32);
            uint32_t i = (uint32_t)v;
            if (pr < a->nprod && i < a->per_prod)
                atomic_fetch_add_explicit(&a->hits[(size_t)pr * a->per_prod + i], 1, memory_order_relaxed);
            a->sum += v;
            a->got++;
            fails = 0;
        } else {
            if (atomic_load_explicit(a->abort_flag, memory_order_relaxed)) break;
            if ((++fails & 63u) == 0) {
                sched_yield();
                if (now_s() > a->deadline) { atomic_store(a->abort_flag, true); break; }
            } else cpu_relax();
        }
    }
    return NULL;
}

typedef struct { bool exact, order, sums, drained, positions, no_abort; size_t lost, dup; double secs; } stress_res_t;

static stress_res_t run_stress(unsigned np, unsigned nc, size_t cap, uint32_t per_prod) {
    stress_res_t r = {0};
    mpmc_t q;
    cell_t *cells = calloc(cap, sizeof *cells);
    size_t n = (size_t)np * per_prod;
    _Atomic unsigned char *hits = calloc(n, sizeof *hits);
    atomic_size_t consumed;
    atomic_bool abort_flag;
    atomic_init(&consumed, 0);
    atomic_init(&abort_flag, false);
    if (!cells || !hits || !mpmc_init(&q, cells, cap)) { free(cells); free((void *)hits); return r; }
    pthread_t th[32];
    stress_arg_t args[32];
    double t0 = now_s();
    for (unsigned k = 0; k < np + nc; k++) {
        args[k] = (stress_arg_t){ .q = &q, .id = k < np ? k : k - np, .nprod = np, .ncons = nc,
                                  .per_prod = per_prod, .consumed = &consumed, .hits = hits,
                                  .abort_flag = &abort_flag, .deadline = t0 + 15.0 };
        pthread_create(&th[k], NULL, k < np ? stress_producer : stress_consumer, &args[k]);
    }
    for (unsigned k = 0; k < np + nc; k++) pthread_join(th[k], NULL);
    r.secs = now_s() - t0;
    uint64_t psum = 0, csum = 0;
    size_t got = 0, bad = 0;
    for (unsigned k = 0; k < np; k++) psum += args[k].sum;
    for (unsigned k = np; k < np + nc; k++) { csum += args[k].sum; got += args[k].got; bad += args[k].order_bad; }
    unsigned char *plain = malloc(n);
    for (size_t k = 0; plain && k < n; k++) plain[k] = atomic_load_explicit(&hits[k], memory_order_relaxed);
    r.exact = plain && exactly_once(plain, n, &r.lost, &r.dup) && got == n;
    r.order = bad == 0;
    r.sums = psum == csum && got == n;
    uint64_t tmp;
    r.drained = !mpmc_try_dequeue(&q, &tmp);
    r.positions = atomic_load(&q.enq_pos) == n && atomic_load(&q.deq_pos) == n;
    r.no_abort = !atomic_load(&abort_flag);
    free(plain);
    free((void *)hits);
    free(cells);
    return r;
}

static uint32_t stress_msgs(void) {
    const char *s = getenv("DS_A03_MSGS");
    if (s && *s) { long v = strtol(s, NULL, 10); if (v > 0 && v < 50000000) return (uint32_t)v; }
    return DS_SANITIZED ? 50000u : 500000u;
}

/* ---- Part C: 중첩 ISR 시나리오 ---- */
static evq_t g_ev;
static int g_isr_burst;              /* 이번 "인터럽트" 에서 ISR 이 올릴 이벤트 수 */
static size_t g_isr_drained;         /* ISR 안에서 시도한 drain 이 받은 개수 (0 이어야 함) */
static bool g_isr_fired;
static void isr_on_claim(size_t pos) {
    (void)pos;
    if (g_isr_fired) return;          /* 한 번만 "선점" — ISR 자신의 게시에서는 다시 안 들어온다 */
    g_isr_fired = true;
    for (int k = 0; k < g_isr_burst; k++) evq_post(&g_ev, ev_pack(2, 7, (uint16_t)k, 1000u + (uint32_t)k));
    uint64_t tmp[8];
    g_isr_drained = evq_drain(&g_ev, tmp, 8);   /* ISR 이 소비를 시도해도 돌지 않고 0 을 받아 나온다 */
}

int main(void) {
    printf("== Part A: slot_case / init / FIFO ==\n");
    CHECK(slot_case(5, 5) == SLOT_MINE && slot_case(4, 5) == SLOT_NOT_READY && slot_case(13, 5) == SLOT_STALE);
    CHECK(slot_case(2, SIZE_MAX - 1) == SLOT_STALE);          /* seq 가 0 을 넘어 wrap: 여전히 "앞섰다" */
    CHECK(slot_case(SIZE_MAX, 1) == SLOT_NOT_READY);          /* seq 가 pos 보다 2 뒤 (wrap 너머) */
    CHECK(slot_case(0, 0) == SLOT_MINE && slot_case(SIZE_MAX, SIZE_MAX) == SLOT_MINE);

    cell_t cells[8];
    memset(cells, 0, sizeof cells);             /* 시작 코드가 init 을 안 해도 정해진 값 */
    mpmc_t q;
    CHECK(!mpmc_init(&q, cells, 6) && !mpmc_init(&q, cells, 1) && !mpmc_init(&q, cells, 0));
    CHECK(mpmc_init(&q, cells, 4) && q.mask == 3u && atomic_load(&cells[3].seq) == 3u);
    uint64_t v = 99;
    CHECK(!mpmc_try_dequeue(&q, &v) && v == 99u);              /* 빈 큐: 실패, out 그대로 */
    CHECK(mpmc_try_enqueue(&q, 10) && mpmc_try_enqueue(&q, 11) && mpmc_try_enqueue(&q, 12) && mpmc_try_enqueue(&q, 13));
    CHECK(!mpmc_try_enqueue(&q, 14));                           /* cap 개 전부 사용 (빈칸 낭비 없음) */
    CHECK(atomic_load(&cells[0].seq) == 1u && atomic_load(&cells[3].seq) == 4u);   /* 게시 = pos+1 */
    CHECK(mpmc_try_dequeue(&q, &v) && v == 10u && atomic_load(&cells[0].seq) == 4u); /* 비움 = pos+cap */
    CHECK(mpmc_try_enqueue(&q, 14) && atomic_load(&cells[0].seq) == 5u);          /* 두 번째 바퀴 */
    {
        bool fifo = true;
        uint64_t expect = 11;
        for (int k = 0; k < 4; k++) fifo = fifo && mpmc_try_dequeue(&q, &v) && v == expect++;
        CHECK(fifo && !mpmc_try_dequeue(&q, &v));
    }
    {   /* 1000 바퀴 돌려도 FIFO, cap=2 (최소) */
        cell_t c2[2];
        memset(c2, 0, sizeof c2);
        mpmc_t q2;
        bool ok = mpmc_init(&q2, c2, 2);
        uint64_t next_in = 0, next_out = 0;
        for (int round = 0; ok && round < 1000; round++) {
            ok = mpmc_try_enqueue(&q2, next_in++) && mpmc_try_enqueue(&q2, next_in++) && !mpmc_try_enqueue(&q2, 777);
            for (int k = 0; ok && k < 2; k++) ok = mpmc_try_dequeue(&q2, &v) && v == next_out++;
        }
        CHECK(ok && atomic_load(&q2.enq_pos) == 2000u && atomic_load(&q2.deq_pos) == 2000u);
    }

    printf("== Part B: checkers ==\n");
    {
        uint32_t last1[3] = {0};
        bool a = order_step(last1, 3, msg_make(0, 0)) && order_step(last1, 3, msg_make(1, 5)) &&
                 order_step(last1, 3, msg_make(0, 7)) && order_step(last1, 3, msg_make(1, 6));
        CHECK(a && last1[0] == 8u && last1[1] == 7u && last1[2] == 0u);
        CHECK(!order_step(last1, 3, msg_make(0, 7)));          /* 같은 번호 다시 = 중복 */
        CHECK(!order_step(last1, 3, msg_make(1, 2)));          /* 뒤로 감 = 순서 위반 */
        CHECK(!order_step(last1, 3, msg_make(3, 0)) && order_step(last1, 3, msg_make(2, 0)));
    }
    {
        unsigned char h1[6] = {1, 1, 1, 1, 1, 1}, h2[6] = {1, 0, 2, 1, 3, 0};
        size_t lost = 9, dup = 9;
        CHECK(exactly_once(h1, 6, &lost, &dup) && lost == 0u && dup == 0u);
        CHECK(!exactly_once(h2, 6, &lost, &dup) && lost == 2u && dup == 2u);
    }

    printf("== Part B: forced pause between CAS and seq store ==\n");
    if (g_all_ok) {
        cell_t c4[4];
        memset(c4, 0, sizeof c4);
        mpmc_t q4;
        mpmc_init(&q4, c4, 4);
        gate_armed = 1; gate_claimed = 0; gate_open = 0; gate_done = 0;
        atomic_store(&g_claim_hook, pause_hook);
        enq_arg_t a = { &q4, 100, false };
        pthread_t th;
        pthread_create(&th, NULL, enq_once_thread, &a);
        pthread_mutex_lock(&gate_m);
        struct timespec dl = abs_deadline(2.0);      /* claim 없이 끝나면(gate_done) 바로 나온다 */
        while (!gate_claimed && !gate_done)
            if (pthread_cond_timedwait(&gate_c, &gate_m, &dl) == ETIMEDOUT) break;
        int claimed = gate_claimed;
        pthread_mutex_unlock(&gate_m);
        CHECK(claimed && gate_pos == 0u && atomic_load(&q4.enq_pos) == 1u && atomic_load(&c4[0].seq) == 0u);
        /* 생산자 A 가 슬롯 0 을 예약만 하고 멈춘 상태. 다른 생산자(main)는 계속 간다: */
        bool b_ok = mpmc_try_enqueue(&q4, 201) && mpmc_try_enqueue(&q4, 202) && mpmc_try_enqueue(&q4, 203);
        CHECK(b_ok && !mpmc_try_enqueue(&q4, 204));          /* 4 칸 중 1 칸은 A 가 예약 → 3 개 후 가득 */
        /* B 의 enqueue 는 이미 "완료" 했다. 그런데 소비자는 아무것도 못 받는다 — 슬롯 0 에 막힘: */
        unsigned empty_answers = 0;
        for (int k = 0; k < 1000; k++) { uint64_t t; if (!mpmc_try_dequeue(&q4, &t)) empty_answers++; }
        CHECK(empty_answers == 1000u && atomic_load(&q4.deq_pos) == 0u);
        uint64_t t = 0;
        CHECK(!mpmc_dequeue_bounded(&q4, &t, 100000u));      /* "기다리는" dequeue 도 A 가 올 때까지 못 끝남 */
        pthread_mutex_lock(&gate_m);
        gate_open = 1;
        pthread_cond_broadcast(&gate_c);
        pthread_mutex_unlock(&gate_m);
        pthread_join(th, NULL);
        atomic_store(&g_claim_hook, (claim_hook_fn)NULL);
        uint64_t o[4] = {0};
        bool all = mpmc_dequeue_bounded(&q4, &o[0], 1000u) && mpmc_dequeue_bounded(&q4, &o[1], 1000u) &&
                   mpmc_dequeue_bounded(&q4, &o[2], 1000u) && mpmc_dequeue_bounded(&q4, &o[3], 1000u);
        CHECK(a.ok && all && o[0] == 100u && o[1] == 201u && o[2] == 202u && o[3] == 203u);  /* 위치 순서 */
    } else {
        printf("SKIP 5 forced-schedule checks: earlier checks failed\n");
        g_fail += 5;
    }

    printf("== Part B: stress (exactly once, per-producer order, exact counts) ==\n");
    if (g_all_ok) {
        uint32_t m = stress_msgs();
        struct { unsigned np, nc; size_t cap; } cfg[] = { {4, 4, 1024}, {4, 4, 2}, {6, 2, 16}, {1, 6, 64} };
        for (size_t k = 0; k < sizeof cfg / sizeof cfg[0]; k++) {
            stress_res_t r = run_stress(cfg[k].np, cfg[k].nc, cfg[k].cap, m);
            printf("  %u prod x %u cons, cap %4zu, %u msgs/prod: %.2f s, lost %zu dup %zu\n",
                   cfg[k].np, cfg[k].nc, cfg[k].cap, m, r.secs, r.lost, r.dup);
            CHECK(r.no_abort && r.exact && r.order && r.sums && r.drained && r.positions);
        }
    } else {
        printf("SKIP 4 stress checks: earlier checks failed\n");
        g_fail += 4;
    }

    printf("== Part C: ISR event queue (nested preemption model) ==\n");
    {
        bool init = mpmc_init(&g_ev.q, g_ev.cells, 8);
        atomic_init(&g_ev.drops, 0u);
        atomic_init(&g_ev.posted, 0u);
        CHECK(init && evq_post(&g_ev, ev_pack(1, 1, 0, 1)) && atomic_load(&g_ev.posted) == 1u);
        uint64_t out[16];
        CHECK(evq_drain(&g_ev, out, 16) == 1u && ev_src(out[0]) == 1u && evq_drain(&g_ev, out, 16) == 0u);
        /* main 이 게시 중(슬롯 예약 직후) ISR 이 들어와 이벤트 3 개를 올리고 drain 도 시도한다 */
        g_isr_burst = 3; g_isr_fired = false; g_isr_drained = 99;
        atomic_store(&g_claim_hook, isr_on_claim);
        bool main_ok = evq_post(&g_ev, ev_pack(0, 3, 0, 42));
        atomic_store(&g_claim_hook, (claim_hook_fn)NULL);
        size_t n = evq_drain(&g_ev, out, 16);
        CHECK(main_ok && g_isr_fired && g_isr_drained == 0u);   /* ISR 안의 drain: 기다리지 않고 0 */
        CHECK(n == 4u && ev_src(out[0]) == 0u && (uint32_t)out[0] == 42u && (uint32_t)out[3] == 1002u);
        /* 큐 8 칸, main 이 1 칸 예약한 채 ISR 이 10 개 폭주 → 7 개 성공, 3 개 drop (정확히) */
        g_isr_burst = 10; g_isr_fired = false;
        unsigned drops0 = atomic_load(&g_ev.drops);
        atomic_store(&g_claim_hook, isr_on_claim);
        main_ok = evq_post(&g_ev, ev_pack(0, 3, 1, 43));
        atomic_store(&g_claim_hook, (claim_hook_fn)NULL);
        n = evq_drain(&g_ev, out, 16);
        CHECK(main_ok && atomic_load(&g_ev.drops) - drops0 == 3u && n == 8u && (uint32_t)out[0] == 43u);
        CHECK((uint32_t)out[7] == 1006u && atomic_load(&g_ev.posted) == 1u + 4u + 8u);
    }

    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
