/* OS-M07 스핀락 구현: TAS, TTAS, ticket lock
 *
 * 호스트 모델: pthread 하나 = 코어 하나 (SMP RTOS 커널 락, 멀티코어 펌웨어의 공유 자원).
 *   Part A  TAS(atomic_exchange) · TTAS(먼저 읽기) + 지수 백오프 · ticket lock(next/serving, fetch_add)
 *           각 lock 함수는 "실패한 RMW 횟수" 를 돌려준다 = 캐시 라인을 독점하려다 헛친 횟수 (데모에서 쓴다).
 *   Part B  공정성 통계(min/max/mean/stddev/Jain 지수), 상호 배제 스트레스(임계 구역 동시 인원 ≤ 1,
 *           정확한 합계), ticket lock 의 FIFO 를 "강제 스케줄" 로 검증.
 *   Part C  펌웨어: RP2040 SIO 하드웨어 스핀락 모델(읽기 = 잡기, 쓰기 = 풀기) 위의 락,
 *           spin_lock_irqsave / spin_unlock_irqrestore (같은 코어 ISR 과의 자기 교착 방지) 모델.
 *
 * 체크는 "올바르면 매 실행 항상 성립" 하는 것만 본다. 처리량·공정성 분산은 demo_bench.c / demo_convoy.c.
 * 멀티스레드 스트레스는 단일 스레드 단위 테스트가 모두 통과해야 실행한다 (시작 코드가 멈추지 않게).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static void cpu_relax(void) {
#if defined(__aarch64__)
    __asm__ volatile("yield" ::: "memory");   /* demo_asm.c: __builtin_arm_yield() 도 같은 yield */
#elif defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("pause" ::: "memory");
#endif
}

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* ======================= Part A — 세 가지 스핀락 ======================= */
typedef struct { _Alignas(128) atomic_uint v; } spin_t;                 /* 0 = 풀림, 1 = 잠김 */
typedef struct { _Alignas(128) atomic_uint next; atomic_uint serving; } ticket_t;  /* 한 라인에 둘 */
#define SPIN_INIT   { 0 }
#define TICKET_INIT { 0, 0 }

/* A1: 한 번 시도. 잡으면 true. */
static bool tas_trylock(spin_t *l) {
    return atomic_exchange_explicit(&l->v, 1u, memory_order_acquire) == 0u;
}
/* A2: 잡을 때까지 매번 exchange(쓰기). 돌려주는 값 = 실패한 exchange 횟수. */
static unsigned tas_lock(spin_t *l) {
    unsigned fails = 0;
    while (atomic_exchange_explicit(&l->v, 1u, memory_order_acquire) != 0u) fails++;
    return fails;
}
/* A3: TAS/TTAS 공용 해제. */
static void spin_unlock(spin_t *l) {
    atomic_store_explicit(&l->v, 0u, memory_order_release);
}

/* A4: 지수 백오프의 다음 대기량. 0 → 1, 그다음 2 배, max 에서 멈춘다 (bounded). */
static unsigned backoff_next(unsigned cur, unsigned max) {
    if (cur == 0u) return 1u <= max ? 1u : max;
    return cur >= max / 2u ? max : cur * 2u;
}

/* A5: TTAS. 풀릴 때까지 "읽기만" 하며 기다리고, 풀린 걸 봤을 때만 exchange.
 *     exchange 에 지면(다른 코어가 먼저 잡음) backoff_max > 0 일 때 지수 백오프.
 *     돌려주는 값 = 실패한 exchange 횟수. */
static unsigned ttas_lock(spin_t *l, unsigned backoff_max) {
    unsigned fails = 0, delay = 0;
    for (;;) {
        while (atomic_load_explicit(&l->v, memory_order_relaxed) != 0u) cpu_relax();
        if (atomic_exchange_explicit(&l->v, 1u, memory_order_acquire) == 0u) return fails;
        fails++;
        if (backoff_max) {
            delay = backoff_next(delay, backoff_max);
            for (unsigned i = 0; i < delay; i++) cpu_relax();
        }
    }
}

/* A6: ticket lock. 번호표(next)를 fetch_add 로 뽑고, serving 이 내 번호가 될 때까지 읽으며 기다린다.
 *     돌려주는 값 = 내 번호표 (FIFO 테스트용). */
static unsigned ticket_lock(ticket_t *t) {
    unsigned my = atomic_fetch_add_explicit(&t->next, 1u, memory_order_relaxed);
    while (atomic_load_explicit(&t->serving, memory_order_acquire) != my) cpu_relax();
    return my;
}
static void ticket_unlock(ticket_t *t) {
    unsigned s = atomic_load_explicit(&t->serving, memory_order_relaxed);   /* 소유자만 쓴다 */
    atomic_store_explicit(&t->serving, s + 1u, memory_order_release);
}
/* A7: 기다리는 사람이 없고 풀려 있을 때만 (next == serving) 번호표를 뽑는다. */
static bool ticket_trylock(ticket_t *t) {
    unsigned s = atomic_load_explicit(&t->serving, memory_order_relaxed);
    unsigned n = s;
    return atomic_compare_exchange_strong_explicit(&t->next, &n, s + 1u,
                                                   memory_order_acquire, memory_order_relaxed);
}

/* ======================= Part B — 공정성 · 상호 배제 · FIFO ======================= */
typedef struct { uint64_t min, max; double mean, stddev, jain; } fair_t;

/* B1: 스레드별 획득 횟수 → min/max/mean/모표준편차/Jain 지수 (Σx)²/(n·Σx²). n ≥ 1.
 *     모두 0 이면 jain = 1 (차이가 없다) 로 정의. */
static fair_t fair_stats(const uint64_t *cnt, int n) {
    fair_t f = { cnt[0], cnt[0], 0.0, 0.0, 1.0 };
    double sum = 0.0, sq = 0.0;
    for (int i = 0; i < n; i++) {
        if (cnt[i] < f.min) f.min = cnt[i];
        if (cnt[i] > f.max) f.max = cnt[i];
        sum += (double)cnt[i];
        sq += (double)cnt[i] * (double)cnt[i];
    }
    f.mean = sum / n;
    double var = sq / n - f.mean * f.mean;
    f.stddev = var > 0.0 ? sqrt(var) : 0.0;
    if (sq > 0.0) f.jain = (sum * sum) / ((double)n * sq);
    return f;
}

enum { K_TAS, K_TTAS, K_TTAS_BO, K_TICKET, K_NKIND };
static const char *const kind_name[K_NKIND] = { "TAS", "TTAS", "TTAS+backoff", "ticket" };

typedef struct {
    int kind;
    spin_t sl;
    ticket_t tk;
    _Alignas(128) atomic_int inside;   /* 임계 구역 안 인원 (관측용) */
    atomic_int max_inside;
    _Alignas(128) uint64_t plain;      /* 락이 보호하는 일반 변수 */
    uint64_t plain2;
} shared_t;

static void any_lock(shared_t *s) {
    switch (s->kind) {
    case K_TAS:     (void)tas_lock(&s->sl); break;
    case K_TTAS:    (void)ttas_lock(&s->sl, 0u); break;
    case K_TTAS_BO: (void)ttas_lock(&s->sl, 64u); break;
    default:        (void)ticket_lock(&s->tk); break;
    }
}
static void any_unlock(shared_t *s) {
    if (s->kind == K_TICKET) ticket_unlock(&s->tk); else spin_unlock(&s->sl);
}

#define ME_THREADS 6
#define ME_ITERS   40000
static void *me_worker(void *arg) {
    shared_t *s = arg;
    for (int i = 0; i < ME_ITERS; i++) {
        any_lock(s);
        int in = atomic_fetch_add_explicit(&s->inside, 1, memory_order_relaxed) + 1;
        int mx = atomic_load_explicit(&s->max_inside, memory_order_relaxed);
        while (in > mx && !atomic_compare_exchange_weak_explicit(&s->max_inside, &mx, in,
                                                                 memory_order_relaxed, memory_order_relaxed)) { }
        s->plain += 1;            /* 일반 RMW 두 개: 락이 없으면 잃는다 */
        s->plain2 += 3;
        atomic_fetch_sub_explicit(&s->inside, 1, memory_order_relaxed);
        any_unlock(s);
    }
    return NULL;
}

/* 강제 스케줄 FIFO: main 이 락을 잡은 채로 대기자를 1 명씩 출발시키고, 그 대기자가 번호표를 뽑은 것
 * (next 증가)을 확인한 뒤에 다음 대기자를 출발시킨다. 그 뒤 해제 → 입장 순서를 기록한다. */
#define FIFO_W 6
typedef struct { ticket_t *tk; int id; int *order; atomic_int *pos; unsigned ticket; } fifo_arg_t;
static void *fifo_waiter(void *arg) {
    fifo_arg_t *a = arg;
    a->ticket = ticket_lock(a->tk);
    a->order[atomic_fetch_add_explicit(a->pos, 1, memory_order_relaxed)] = a->id;  /* 락 안 */
    ticket_unlock(a->tk);
    return NULL;
}
static bool wait_next(ticket_t *tk, unsigned want, double timeout_s) {
    double t0 = now_s();
    while (atomic_load_explicit(&tk->next, memory_order_relaxed) != want) {
        if (now_s() - t0 > timeout_s) return false;
        cpu_relax();
        struct timespec ts = { 0, 100000 };
        nanosleep(&ts, NULL);
    }
    return true;
}

/* ======================= Part C — 펌웨어 ======================= */
/* C-1. RP2040 SIO 하드웨어 스핀락 32 개의 "레지스터" 모델 (Cortex-M0+ 에는 LDREX/STREX 가 없다).
 *      SPINLOCKn 읽기: 풀려 있었으면 잡고 0 이 아닌 값, 이미 잡혀 있으면 0. 쓰기(아무 값): 풀기.
 *      (실리콘은 버스 쪽에서 이 동작을 원자적으로 한다. 여기서는 fetch_or/fetch_and 로 흉내.) */
typedef struct { _Alignas(128) atomic_uint state; } sio_t;    /* bit n = SPINLOCKn 잠김 (SPINLOCK_ST 와 같은 모양) */
static uint32_t sio_spinlock_read(sio_t *sio, unsigned n) {
    uint32_t bit = 1u << n;
    uint32_t old = atomic_fetch_or_explicit(&sio->state, bit, memory_order_acquire);
    return (old & bit) ? 0u : bit;
}
static void sio_spinlock_write(sio_t *sio, unsigned n) {
    atomic_fetch_and_explicit(&sio->state, ~(1u << n), memory_order_release);
}
/* C1: 하드웨어 스핀락 위의 lock / trylock / unlock (pico-sdk spin_lock_blocking 의 몸통에 해당). */
static void hw_spin_lock(sio_t *sio, unsigned n) {
    while (sio_spinlock_read(sio, n) == 0u) cpu_relax();
}
static bool hw_spin_trylock(sio_t *sio, unsigned n) {
    return sio_spinlock_read(sio, n) != 0u;
}
static void hw_spin_unlock(sio_t *sio, unsigned n) {
    sio_spinlock_write(sio, n);
}

/* C-2. 코어 하나 + 그 코어의 IRQ 모델. ISR 은 같은 락을 잡으려 한다 (예: UART TX 링 버퍼).
 *      실제 칩에서 ISR 이 "같은 코어가 이미 잡은 스핀락" 을 돌면 영원히 못 나온다 (자기 교착).
 *      모델에서는 ISR 이 trylock 에 실패하면 deadlock 으로 센다. */
typedef struct {
    bool irq_on;
    int pending;        /* 마스크된 동안 들어온 인터럽트 */
    int isr_runs, isr_deadlocks;
    ticket_t *lk;
    int shared_log;     /* 락이 보호하는 자원 */
} cpu_t;

static void isr_body(cpu_t *c) {
    c->isr_runs++;
    if (!ticket_trylock(c->lk)) { c->isr_deadlocks++; return; }   /* 실리콘: 여기서 영원히 스핀 */
    c->shared_log += 100;
    ticket_unlock(c->lk);
}
static void raise_irq(cpu_t *c) {                 /* 인터럽트 도착 */
    if (c->irq_on) isr_body(c); else c->pending++;
}
static unsigned irq_save(cpu_t *c) { unsigned f = c->irq_on; c->irq_on = false; return f; }  /* cpsid i */
static void irq_restore(cpu_t *c, unsigned f) {   /* PRIMASK 복원 → 펜딩된 것이 바로 들어온다 */
    c->irq_on = f != 0u;
    while (c->irq_on && c->pending > 0) { c->pending--; isr_body(c); }
}
/* C2: 순서가 핵심. 잡을 때 = IRQ 끄고 → 락. 풀 때 = 락 풀고 → IRQ 복원. 돌려주는 값 = 이전 IRQ 상태. */
static unsigned spin_lock_irqsave(cpu_t *c) {
    unsigned f = irq_save(c);
    (void)ticket_lock(c->lk);
    return f;
}
static void spin_unlock_irqrestore(cpu_t *c, unsigned flags) {
    ticket_unlock(c->lk);
    irq_restore(c, flags);
}

/* 2 코어 로그: 각 코어가 레코드 4 워드를 하드웨어 스핀락 안에서 쓴다 → 레코드가 섞이면 안 된다. */
#define LOG_N 4000
typedef struct { sio_t sio; uint32_t ring[LOG_N * 2 * 4]; unsigned wr; } log_t;
typedef struct { log_t *lg; uint32_t core; } core_arg_t;
static void *core_logger(void *arg) {
    core_arg_t *a = arg;
    for (uint32_t i = 0; i < LOG_N; i++) {
        hw_spin_lock(&a->lg->sio, 7u);
        unsigned w = a->lg->wr;
        for (int k = 0; k < 4; k++) a->lg->ring[w + (unsigned)k] = (a->core << 24) | i;
        a->lg->wr = w + 4u;
        hw_spin_unlock(&a->lg->sio, 7u);
    }
    return NULL;
}

/* ======================= 테스트 ======================= */
static shared_t g_sh;
static log_t g_log;

int main(void) {
    /* --- Part A: 단일 스레드 단위 --- */
    int fail0 = g_fail;
    spin_t s = SPIN_INIT;
    CHECK(tas_trylock(&s) == true);
    CHECK(tas_trylock(&s) == false);                       /* 잡혀 있으면 실패 */
    spin_unlock(&s);
    CHECK(atomic_load(&s.v) == 0u);
    CHECK(tas_lock(&s) == 0u && atomic_load(&s.v) == 1u);   /* 경합 없음 → 실패 0 */
    spin_unlock(&s);
    CHECK(ttas_lock(&s, 64u) == 0u && tas_trylock(&s) == false);
    spin_unlock(&s);

    CHECK(backoff_next(0u, 64u) == 1u && backoff_next(1u, 64u) == 2u && backoff_next(16u, 64u) == 32u);
    CHECK(backoff_next(32u, 64u) == 64u && backoff_next(64u, 64u) == 64u);   /* 상한에서 멈춤 */
    CHECK(backoff_next(48u, 64u) == 64u && backoff_next(0u, 0u) == 0u);      /* 넘지 않음 */
    {
        unsigned d = 0, seq[9];
        for (int i = 0; i < 9; i++) { d = backoff_next(d, 100u); seq[i] = d; }
        CHECK(seq[0] == 1u && seq[6] == 64u && seq[7] == 100u && seq[8] == 100u);
    }

    ticket_t t = TICKET_INIT;
    CHECK(ticket_lock(&t) == 0u && atomic_load(&t.next) == 1u && atomic_load(&t.serving) == 0u);
    CHECK(ticket_trylock(&t) == false && atomic_load(&t.next) == 1u);   /* 잡혀 있음 → 번호표 안 뽑음 */
    ticket_unlock(&t);
    CHECK(atomic_load(&t.serving) == 1u);
    CHECK(ticket_trylock(&t) == true && atomic_load(&t.next) == 2u);
    ticket_unlock(&t);
    atomic_store(&t.next, 0xFFFFFFFFu); atomic_store(&t.serving, 0xFFFFFFFFu);   /* 번호표 wrap-around */
    CHECK(ticket_lock(&t) == 0xFFFFFFFFu && atomic_load(&t.next) == 0u);
    ticket_unlock(&t);
    CHECK(atomic_load(&t.serving) == 0u && ticket_lock(&t) == 0u);
    ticket_unlock(&t);

    /* --- Part B: 공정성 통계 (고정 입력) --- */
    {
        uint64_t even[4] = { 5, 5, 5, 5 }, hog[4] = { 10, 0, 0, 0 }, ramp[4] = { 1, 2, 3, 4 };
        fair_t a = fair_stats(even, 4), b = fair_stats(hog, 4), c = fair_stats(ramp, 4);
        CHECK(a.min == 5 && a.max == 5 && fabs(a.stddev) < 1e-12 && fabs(a.jain - 1.0) < 1e-12);
        CHECK(b.min == 0 && b.max == 10 && fabs(b.mean - 2.5) < 1e-12 && fabs(b.jain - 0.25) < 1e-12);
        CHECK(fabs(c.stddev - sqrt(1.25)) < 1e-12 && fabs(c.jain - 100.0 / 120.0) < 1e-12);
        CHECK(fabs(b.stddev - sqrt(18.75)) < 1e-12);
    }
    bool unit_ok = (g_fail == fail0);

    /* --- Part B: 상호 배제 스트레스 (락 4종) --- */
    if (unit_ok) {
        for (int k = 0; k < K_NKIND; k++) {
            memset(&g_sh, 0, sizeof g_sh);
            g_sh.kind = k;
            pthread_t th[ME_THREADS];
            for (int i = 0; i < ME_THREADS; i++) pthread_create(&th[i], NULL, me_worker, &g_sh);
            for (int i = 0; i < ME_THREADS; i++) pthread_join(th[i], NULL);
            bool ok = atomic_load(&g_sh.max_inside) == 1 && g_sh.plain == (uint64_t)ME_THREADS * ME_ITERS &&
                      g_sh.plain2 == 3u * g_sh.plain;
            printf("  %-13s max_inside=%d plain=%llu\n", kind_name[k], atomic_load(&g_sh.max_inside),
                   (unsigned long long)g_sh.plain);
            CHECK(ok);
        }
        /* --- Part B: ticket FIFO (강제 스케줄) --- */
        ticket_t ft = TICKET_INIT;
        int order[FIFO_W];
        atomic_int pos = 0;
        fifo_arg_t fa[FIFO_W];
        pthread_t th[FIFO_W];
        (void)ticket_lock(&ft);                                   /* main = 번호표 0 */
        bool started = true;
        for (int i = 0; i < FIFO_W; i++) {
            fa[i] = (fifo_arg_t){ &ft, i + 1, order, &pos, 0u };
            pthread_create(&th[i], NULL, fifo_waiter, &fa[i]);
            if (!wait_next(&ft, (unsigned)i + 2u, 2.0)) started = false;   /* i 번째가 번호표를 뽑을 때까지 */
        }
        ticket_unlock(&ft);
        for (int i = 0; i < FIFO_W; i++) pthread_join(th[i], NULL);
        bool fifo = started && atomic_load(&pos) == FIFO_W;
        for (int i = 0; i < FIFO_W && fifo; i++) fifo = order[i] == i + 1 && fa[i].ticket == (unsigned)i + 1u;
        CHECK(fifo);
    } else {
        printf("SKIP 5 stress checks (Part B): unit checks failed\n");
        g_fail += 5;
    }

    /* --- Part C-1: RP2040 SIO 하드웨어 스핀락 --- */
    int fail1 = g_fail;
    {
        sio_t sio = { 0 };
        CHECK(sio_spinlock_read(&sio, 0u) == 1u && sio_spinlock_read(&sio, 0u) == 0u);   /* 주어진 모델 */
        sio_spinlock_write(&sio, 0u);
        CHECK(hw_spin_trylock(&sio, 3u) == true && atomic_load(&sio.state) == 0x8u);
        CHECK(hw_spin_trylock(&sio, 3u) == false);                /* 이미 잡힘 → 읽기가 0 */
        CHECK(hw_spin_trylock(&sio, 4u) == true && atomic_load(&sio.state) == 0x18u);   /* 락끼리 독립 */
        hw_spin_unlock(&sio, 3u);
        CHECK(atomic_load(&sio.state) == 0x10u);
        hw_spin_lock(&sio, 3u);
        CHECK(atomic_load(&sio.state) == 0x18u);
        hw_spin_unlock(&sio, 3u); hw_spin_unlock(&sio, 4u);
        CHECK(atomic_load(&sio.state) == 0u);
    }
    /* --- Part C-2: irqsave 모델 --- */
    {
        ticket_t lk = TICKET_INIT;
        cpu_t c = { true, 0, 0, 0, &lk, 0 };
        /* 잘못된 예 (주어진 코드): IRQ 를 켠 채로 락 → 그 사이 ISR 이 같은 락 → 자기 교착 */
        (void)ticket_lock(&lk); raise_irq(&c); ticket_unlock(&lk);
        CHECK(c.isr_runs == 1 && c.isr_deadlocks == 1);
        /* 올바른 예 */
        c = (cpu_t){ true, 0, 0, 0, &lk, 0 };
        unsigned f = spin_lock_irqsave(&c);
        CHECK(f == 1u && c.irq_on == false && atomic_load(&lk.serving) != atomic_load(&lk.next));
        c.shared_log += 1;
        raise_irq(&c);                                             /* 마스크 중 → 펜딩 */
        CHECK(c.pending == 1 && c.isr_runs == 0);
        spin_unlock_irqrestore(&c, f);                             /* 락 먼저 풀고 → IRQ 켜기 → ISR */
        CHECK(c.irq_on && c.pending == 0 && c.isr_runs == 1 && c.isr_deadlocks == 0 && c.shared_log == 101);
        /* 중첩: 바깥에서 이미 IRQ 를 껐으면 안쪽 restore 가 켜면 안 된다 */
        unsigned f1 = irq_save(&c);
        unsigned f2 = spin_lock_irqsave(&c);
        raise_irq(&c);
        spin_unlock_irqrestore(&c, f2);
        CHECK(f2 == 0u && c.irq_on == false && c.pending == 1 && c.isr_runs == 1);
        irq_restore(&c, f1);
        CHECK(c.isr_runs == 2 && c.isr_deadlocks == 0 && c.shared_log == 201);
    }
    /* --- Part C-1 스트레스: 2 코어 로그 레코드가 섞이지 않는다 --- */
    if (g_fail == fail1) {
        pthread_t th[2];
        core_arg_t ca[2] = { { &g_log, 1u }, { &g_log, 2u } };
        for (int i = 0; i < 2; i++) pthread_create(&th[i], NULL, core_logger, &ca[i]);
        for (int i = 0; i < 2; i++) pthread_join(th[i], NULL);
        bool ok = g_log.wr == LOG_N * 2u * 4u;
        uint32_t seen_next[3] = { 0, 0, 0 };
        for (unsigned r = 0; ok && r < LOG_N * 2u; r++) {
            uint32_t w0 = g_log.ring[r * 4u];
            for (int k = 1; k < 4; k++) ok = ok && g_log.ring[r * 4u + (unsigned)k] == w0;
            uint32_t core = w0 >> 24, idx = w0 & 0xFFFFFFu;
            ok = ok && (core == 1u || core == 2u) && idx == seen_next[core];   /* 코어별 순서 유지 */
            if (ok) seen_next[core]++;
        }
        CHECK(ok && seen_next[1] == LOG_N && seen_next[2] == LOG_N);
    } else {
        printf("SKIP 1 stress check (Part C): unit checks failed\n");
        g_fail += 1;
    }

    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
