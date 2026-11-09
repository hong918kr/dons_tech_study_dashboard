/* OS-M10 교착 상태: 식사하는 철학자와 4가지 조건
 *
 * 호스트 모델: pthread 하나 = 철학자 하나 = RTOS 태스크 하나, 포크 = 뮤텍스(공유 자원).
 *   철학자 p 의 왼쪽 포크 = F[p], 오른쪽 포크 = F[(p+1) % n]  (OSTEP 31.6 과 같은 번호).
 *   Part A  그대로 구현(왼쪽 → 오른쪽)한 교착을 "강제 스케줄" 로 매번 재현하고 워치독이 탐지·회복한다.
 *           해법 1: 전역 락 순서 (번호가 작은 포크 먼저) — TODO A1.
 *   Part B  해법 2: trylock + 무작위 지수 백오프 — TODO B1 (백오프), B2 (포크 잡기).
 *           백오프를 무작위로 하지 않으면 라이브락: 락스텝 강제 스케줄에서 식사 0 번.
 *   Part C  해법 3: 동시 착석 n-1 명 (mutex+cond 로 만든 카운팅 세마포어) — TODO C1.
 *           펌웨어: 순위(rank) 있는 RTOS 락 — 순서 위반 · ISR 에서의 대기를 거부 — TODO C2.
 *
 * 교착이 나도 멈추지 않는다: 모든 대기는 시간 제한(cond_timedwait 조각) + halt 플래그를 본다.
 *   워치독(main) = 식사가 STALL_MS 동안 0 → 대기 그래프(wait-for) 스냅샷에서 순환을 찾으면
 *   "DEADLOCK: P0 holds F0 waits F1, ..." 를 찍고 halt → 모두 포크를 놓고 끝난다 (회복).
 * 맥에는 pthread_barrier_t · pthread_mutex_timedlock · sem_init(무명 세마포어) 이 없다 → 전부 mutex+cond 로.
 * 체크는 "올바르면 매 실행 성립" 하는 것만 본다. 처리량·공정성은 demo_bench.c.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
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

#define MAXP            16
#define WAIT_SLICE_MS   20     /* 모든 cond 대기는 이 조각으로 끊어서 halt 를 본다 */
#define STALL_MS        200    /* 식사 0 번이 이만큼 이어지면 대기 그래프를 본다 */
#define HARD_STALL_MS   2000   /* 순환이 없는데도 진전이 없으면 (라이브락 · 굶주림) 그래도 멈춘다 */
#define GATE_TIMEOUT_MS 2000

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec * 1e-6;
}
static void deadline_after(struct timespec *ts, int ms) {     /* cond_timedwait 는 CLOCK_REALTIME 절대 시각 */
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) { ts->tv_sec++; ts->tv_nsec -= 1000000000L; }
}
static void sleep_us(unsigned us) {
    struct timespec ts = { (time_t)(us / 1000000u), (long)(us % 1000000u) * 1000L };
    nanosleep(&ts, NULL);
}
static void spin_work(unsigned iters) {          /* 생각하기 · 먹기 = CPU 일 */
    volatile unsigned x = 0;
    for (unsigned i = 0; i < iters; i++) x += i;
}
static uint32_t xorshift32(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}

/* ===== 포크 = mutex+cond 로 만든 잠드는 락. 기다림은 항상 시간 제한 + halt 확인 ===== */
typedef struct {
    pthread_mutex_t m;
    pthread_cond_t cv;
    int holder;        /* -1 = 놓임, 아니면 철학자 번호 (m 보호) */
    int waiters;
} fork_t;

static void fork_init(fork_t *f) {
    pthread_mutex_init(&f->m, NULL);
    pthread_cond_init(&f->cv, NULL);
    f->holder = -1; f->waiters = 0;
}
static void fork_destroy(fork_t *f) { pthread_mutex_destroy(&f->m); pthread_cond_destroy(&f->cv); }
/* 잡을 때까지 기다린다 (pthread_mutex_lock 처럼). halt 가 서면 false. */
static bool fork_take(fork_t *f, int p, const atomic_int *halt) {
    pthread_mutex_lock(&f->m);
    f->waiters++;
    for (;;) {
        if (atomic_load(halt)) { f->waiters--; pthread_mutex_unlock(&f->m); return false; }   /* 회복 중엔 새로 잡지 않는다 */
        if (f->holder == -1) break;
        struct timespec ts;
        deadline_after(&ts, WAIT_SLICE_MS);
        pthread_cond_timedwait(&f->cv, &f->m, &ts);
    }
    f->waiters--;
    f->holder = p;
    pthread_mutex_unlock(&f->m);
    return true;
}
/* 한 번만 시도 (pthread_mutex_trylock 처럼). */
static bool fork_try(fork_t *f, int p) {
    pthread_mutex_lock(&f->m);
    bool ok = (f->holder == -1);
    if (ok) f->holder = p;
    pthread_mutex_unlock(&f->m);
    return ok;
}
static void fork_put(fork_t *f, int p) {
    pthread_mutex_lock(&f->m);
    if (f->holder == p) f->holder = -1;
    if (f->waiters > 0) pthread_cond_signal(&f->cv);
    pthread_mutex_unlock(&f->m);
}
static int fork_holder(fork_t *f) {
    pthread_mutex_lock(&f->m);
    int h = f->holder;
    pthread_mutex_unlock(&f->m);
    return h;
}
static void fork_wake_all(fork_t *f) {
    pthread_mutex_lock(&f->m); pthread_cond_broadcast(&f->cv); pthread_mutex_unlock(&f->m);
}

/* ===== 강제 스케줄용 문(gate): 첫 포크를 잡은 철학자는 여기서 기다리고, 워치독이 연다 ===== */
typedef struct { pthread_mutex_t m; pthread_cond_t cv; int arrived, open; } gate_t;
static bool gate_arrive_wait(gate_t *g, const atomic_int *halt) {
    pthread_mutex_lock(&g->m);
    g->arrived++;
    while (!g->open && !atomic_load(halt)) {
        struct timespec ts;
        deadline_after(&ts, WAIT_SLICE_MS);
        pthread_cond_timedwait(&g->cv, &g->m, &ts);
    }
    bool ok = g->open != 0;
    pthread_mutex_unlock(&g->m);
    return ok;
}
static void gate_open(gate_t *g) {
    pthread_mutex_lock(&g->m); g->open = 1; pthread_cond_broadcast(&g->cv); pthread_mutex_unlock(&g->m);
}

/* ===== 재사용 배리어 (맥에는 pthread_barrier_t 가 없다). 1 = 마지막 도착, 0 = 통과, -1 = 시간 초과 ===== */
typedef struct { pthread_mutex_t m; pthread_cond_t cv; int n, count; unsigned gen; } bar_t;
static void bar_init(bar_t *b, int n) {
    pthread_mutex_init(&b->m, NULL); pthread_cond_init(&b->cv, NULL);
    b->n = n; b->count = 0; b->gen = 0;
}
static void bar_destroy(bar_t *b) { pthread_mutex_destroy(&b->m); pthread_cond_destroy(&b->cv); }
static int bar_wait(bar_t *b, int timeout_ms) {
    pthread_mutex_lock(&b->m);
    unsigned gen = b->gen;
    if (++b->count == b->n) {
        b->count = 0; b->gen++;
        pthread_cond_broadcast(&b->cv);
        pthread_mutex_unlock(&b->m);
        return 1;
    }
    struct timespec ts;
    deadline_after(&ts, timeout_ms);
    while (gen == b->gen) {
        if (pthread_cond_timedwait(&b->cv, &b->m, &ts) == ETIMEDOUT && gen == b->gen) {
            b->count--;
            pthread_mutex_unlock(&b->m);
            return -1;
        }
    }
    pthread_mutex_unlock(&b->m);
    return 0;
}

/* ===== 카운팅 세마포어 (맥: sem_init = 지원 안 됨) ===== */
typedef struct { pthread_mutex_t m; pthread_cond_t cv; int count; } csem_t;
static void csem_init(csem_t *s, int initial) {
    pthread_mutex_init(&s->m, NULL); pthread_cond_init(&s->cv, NULL);
    s->count = initial;
}
static void csem_destroy(csem_t *s) { pthread_mutex_destroy(&s->m); pthread_cond_destroy(&s->cv); }
static void csem_wake_all(csem_t *s) {
    pthread_mutex_lock(&s->m); pthread_cond_broadcast(&s->cv); pthread_mutex_unlock(&s->m);
}

/* C1: 자리가 날 때까지 기다렸다가 하나 차지한다. halt 가 서거나 max_ms(<0 = 무제한) 가 지나면 false.
 *     조건 변수 규칙(OS-B06): 락을 쥐고, while 로 다시 확인, 잠은 WAIT_SLICE_MS 조각으로. */
static bool csem_wait(csem_t *s, const atomic_int *halt, int max_ms) {
    double t0 = now_ms();
    pthread_mutex_lock(&s->m);
    while (s->count == 0) {
        if ((halt && atomic_load(halt)) || (max_ms >= 0 && now_ms() - t0 >= (double)max_ms)) {
            pthread_mutex_unlock(&s->m);
            return false;
        }
        struct timespec ts;
        deadline_after(&ts, WAIT_SLICE_MS);
        pthread_cond_timedwait(&s->cv, &s->m, &ts);
    }
    s->count--;
    pthread_mutex_unlock(&s->m);
    return true;
}
/* C1: 자리를 돌려주고 기다리는 한 명을 깨운다. */
static void csem_post(csem_t *s) {
    pthread_mutex_lock(&s->m);
    s->count++;
    pthread_cond_signal(&s->cv);
    pthread_mutex_unlock(&s->m);
}

/* ===== 식탁 ===== */
enum { S_NAIVE, S_ORDERED, S_TRYLOCK, S_TRYLOCK_FIXED, S_SEATED, S_COUNT };
enum { ST_THINK, ST_WAIT_SEAT, ST_WAIT_FIRST, ST_AT_GATE, ST_WAIT_SECOND, ST_EAT, ST_DONE };

typedef struct {
    int n, strat;
    long meals_target;      /* > 0: 각자 이만큼 먹으면 끝. 0: run_ms 동안 */
    int run_ms;
    bool forced;            /* 첫 판: 모두 첫 포크를 쥔 뒤 gate 에서 대기 → 워치독이 연다 */
    unsigned think_iters, eat_iters, backoff_us;
    uint32_t seed;
    bool quiet;             /* DEADLOCK/STALL 줄을 찍지 않음 (벤치마크용) */
} cfg_t;

typedef struct {
    cfg_t c;
    fork_t fork[MAXP];
    atomic_int wants[MAXP];        /* 기다리는 포크 번호, 없으면 -1 */
    atomic_int state[MAXP];
    atomic_int eating[MAXP];
    atomic_long meals[MAXP];
    atomic_long total_meals, retries;
    atomic_int halt;               /* 워치독이 세움: 교착 회복 또는 시간 끝 */
    atomic_int seated, max_seated, violations, done_threads;
    csem_t seats;
    gate_t gate;
} table_t;

typedef struct {
    int deadlocked, stalled, gate_timeout, cycle_len, violations, max_seated, forks_free_after;
    char report[600];
    long meals[MAXP], total, retries;
    double ms, deadlock_at_ms;
} result_t;

/* A1: 철학자 p 가 먼저 잡을 포크와 나중 잡을 포크.
 *     naive = 왼쪽(p) → 오른쪽((p+1)%n). ordered = 번호가 작은 쪽 먼저 (전역 순서 → 순환 대기 불가). */
static void fork_order(int strat, int p, int n, int *first, int *second) {
    int left = p, right = (p + 1) % n;
    if (strat == S_ORDERED) {
        *first = left < right ? left : right;
        *second = left < right ? right : left;
        return;
    }
    *first = left; *second = right;
}

/* B1: 실패 attempt 번째 뒤에 쉴 "칸" 수 (>= 1).
 *     fixed: 항상 1.  randomized: 1 + rng % 2^min(attempt, 6)  — 창이 실패마다 두 배, 64 에서 멈춤. */
static unsigned backoff_rounds(uint32_t *rng, unsigned attempt, bool randomized) {
    if (!randomized) return 1u;
    unsigned cap = attempt < 6u ? attempt : 6u;
    return 1u + xorshift32(rng) % (1u << cap);
}

static bool take_one(table_t *t, int p, int f, int st) {
    atomic_store(&t->state[p], st);
    atomic_store(&t->wants[p], f);
    bool ok = fork_take(&t->fork[f], p, &t->halt);
    atomic_store(&t->wants[p], -1);
    return ok;
}
static bool pass_gate(table_t *t, int p, bool *first_round) {
    if (!t->c.forced || !*first_round) return true;
    *first_round = false;
    atomic_store(&t->state[p], ST_AT_GATE);
    return gate_arrive_wait(&t->gate, &t->halt);
}
/* naive / ordered / seated: 첫 포크를 쥔 채 둘째를 기다린다 (hold-and-wait). */
static bool take_forks_blocking(table_t *t, int p, int first, int second, bool *first_round) {
    if (!take_one(t, p, first, ST_WAIT_FIRST)) return false;
    if (!pass_gate(t, p, first_round)) { fork_put(&t->fork[first], p); return false; }
    if (!take_one(t, p, second, ST_WAIT_SECOND)) { fork_put(&t->fork[first], p); return false; }
    return true;
}
/* B2: 첫 포크는 기다려서, 둘째는 fork_try 한 번. 실패하면 첫 포크를 내려놓고(no preemption 을 스스로 깸)
 *     retries 를 세고, backoff_rounds(...) * backoff_us 만큼 잔 뒤 처음부터.
 *     무작위 여부 = (strat == S_TRYLOCK). halt 면 false (쥔 포크는 놓고). */
static bool take_forks_trylock(table_t *t, int p, int first, int second, bool *first_round, uint32_t *rng) {
    unsigned attempt = 0;
    for (;;) {
        if (!take_one(t, p, first, ST_WAIT_FIRST)) return false;
        if (!pass_gate(t, p, first_round)) { fork_put(&t->fork[first], p); return false; }
        atomic_store(&t->state[p], ST_WAIT_SECOND);
        if (fork_try(&t->fork[second], p)) return true;
        fork_put(&t->fork[first], p);
        atomic_fetch_add(&t->retries, 1);
        if (atomic_load(&t->halt)) return false;
        unsigned r = backoff_rounds(rng, ++attempt, t->c.strat == S_TRYLOCK);
        if (t->c.backoff_us) sleep_us(r * t->c.backoff_us);
    }
}

typedef struct { table_t *t; int p; } philo_arg_t;

static void *philo_main(void *arg) {
    philo_arg_t *a = arg;
    table_t *t = a->t;
    int p = a->p, n = t->c.n;
    uint32_t rng = t->c.seed * 2654435761u + (uint32_t)p * 40503u + 1u;
    if (rng == 0) rng = 1;
    xorshift32(&rng);                                   /* 한 번 섞어 둔다 (인접 seed 상관 줄이기) */
    int first, second;
    fork_order(t->c.strat, p, n, &first, &second);
    bool first_round = true;
    for (long k = 0; t->c.meals_target <= 0 || k < t->c.meals_target;) {
        if (atomic_load(&t->halt)) break;
        atomic_store(&t->state[p], ST_THINK);
        spin_work(t->c.think_iters);
        bool seated = false;
        if (t->c.strat == S_SEATED) {
            atomic_store(&t->state[p], ST_WAIT_SEAT);
            if (!csem_wait(&t->seats, &t->halt, -1)) break;
            seated = true;
            int s = atomic_fetch_add(&t->seated, 1) + 1;
            int mx = atomic_load(&t->max_seated);
            while (s > mx && !atomic_compare_exchange_weak(&t->max_seated, &mx, s)) {}
        }
        bool ok = (t->c.strat == S_TRYLOCK || t->c.strat == S_TRYLOCK_FIXED)
                      ? take_forks_trylock(t, p, first, second, &first_round, &rng)
                      : take_forks_blocking(t, p, first, second, &first_round);
        if (ok) {
            atomic_store(&t->state[p], ST_EAT);
            atomic_store(&t->eating[p], 1);
            if (atomic_load(&t->eating[(p + n - 1) % n]) || atomic_load(&t->eating[(p + 1) % n]))
                atomic_fetch_add(&t->violations, 1);           /* 이웃과 동시에 먹음 = 상호 배제 깨짐 */
            spin_work(t->c.eat_iters);
            atomic_store(&t->eating[p], 0);
            fork_put(&t->fork[second], p);
            fork_put(&t->fork[first], p);
            atomic_fetch_add(&t->meals[p], 1);
            atomic_fetch_add(&t->total_meals, 1);
            k++;
        }
        if (seated) { atomic_fetch_sub(&t->seated, 1); csem_post(&t->seats); }
        if (!ok) break;
    }
    atomic_store(&t->state[p], ST_DONE);
    atomic_fetch_add(&t->done_threads, 1);
    return NULL;
}

/* 대기 그래프(wait-for graph): 철학자 p → (wants[p] 를 쥔) holder[wants[p]].
 * 각자 기다리는 것은 하나뿐이라 따라가기만 하면 된다. 순환을 가장 작은 번호부터 cyc[] 에. 길이, 없으면 0. */
static int wfg_find_cycle(int n, const int holder[], const int wants[], int cyc[]) {
    for (int s = 0; s < n; s++) {
        int pos[MAXP], path[MAXP], len = 0;
        for (int i = 0; i < n; i++) pos[i] = -1;
        int p = s;
        while (p >= 0 && pos[p] < 0) {
            pos[p] = len; path[len++] = p;
            int f = wants[p];
            p = (f >= 0) ? holder[f] : -1;
        }
        if (p >= 0) {
            int start = pos[p], clen = len - start, mi = start;
            for (int i = start; i < len; i++) if (path[i] < path[mi]) mi = i;
            for (int i = 0; i < clen; i++) cyc[i] = path[start + (mi - start + i) % clen];
            return clen;
        }
    }
    return 0;
}
static void format_deadlock(char *buf, size_t cap, int len, const int cyc[], const int wants[]) {
    size_t off = (size_t)snprintf(buf, cap, "DEADLOCK:");
    for (int i = 0; i < len && off < cap; i++) {
        int p = cyc[i], prev = cyc[(i + len - 1) % len];
        off += (size_t)snprintf(buf + off, cap - off, "%s P%d holds F%d waits F%d",
                                i ? "," : "", p, wants[prev], wants[p]);
    }
}
static bool gate_settled(table_t *t) {                 /* 모두 gate 에 있거나, gate 에 있는 사람 때문에 막힘 */
    int n = t->c.n, at = 0;
    for (int p = 0; p < n; p++) if (atomic_load(&t->state[p]) == ST_AT_GATE) at++;
    for (int p = 0; p < n; p++) {
        int st = atomic_load(&t->state[p]);
        if (st == ST_AT_GATE) continue;
        if (st == ST_WAIT_FIRST) {
            int f = atomic_load(&t->wants[p]);
            int h = f >= 0 ? fork_holder(&t->fork[f]) : -1;
            if (h >= 0 && h != p && atomic_load(&t->state[h]) == ST_AT_GATE) continue;
        }
        if (st == ST_WAIT_SEAT && at == n - 1) continue;   /* 자리 n-1 개를 gate 대기자가 다 차지 */
        return false;
    }
    return true;
}
static void halt_all(table_t *t) {
    atomic_store(&t->halt, 1);
    for (int f = 0; f < t->c.n; f++) fork_wake_all(&t->fork[f]);
    csem_wake_all(&t->seats);
    pthread_mutex_lock(&t->gate.m); pthread_cond_broadcast(&t->gate.cv); pthread_mutex_unlock(&t->gate.m);
}

static table_t g_table;     /* 큰 구조체라 전역 하나 (한 번에 한 판) */

static void run_table(const cfg_t *c, result_t *r) {
    table_t *t = &g_table;
    memset(t, 0, sizeof *t);
    memset(r, 0, sizeof *r);
    t->c = *c;
    int n = c->n;
    for (int f = 0; f < n; f++) fork_init(&t->fork[f]);
    for (int p = 0; p < n; p++) { atomic_init(&t->wants[p], -1); atomic_init(&t->state[p], ST_THINK); }
    csem_init(&t->seats, n - 1);
    pthread_mutex_init(&t->gate.m, NULL); pthread_cond_init(&t->gate.cv, NULL);
    pthread_t th[MAXP];
    philo_arg_t args[MAXP];
    double t0 = now_ms();
    for (int p = 0; p < n; p++) { args[p] = (philo_arg_t){ t, p }; pthread_create(&th[p], NULL, philo_main, &args[p]); }

    bool gate_done = !c->forced;
    long last_meals = 0;
    double last_change = t0;
    int prev_cyc[MAXP], prev_len = 0;
    while (atomic_load(&t->done_threads) < n) {
        sleep_us(2000);
        double now = now_ms();
        if (atomic_load(&t->halt)) continue;
        if (!gate_done) {
            bool to = now - t0 > GATE_TIMEOUT_MS;
            if (gate_settled(t) || to) { r->gate_timeout = to; gate_open(&t->gate); gate_done = true; last_change = now; }
            continue;
        }
        if (c->run_ms > 0 && now - t0 >= c->run_ms) { halt_all(t); continue; }
        long m = atomic_load(&t->total_meals);
        if (m != last_meals) { last_meals = m; last_change = now; prev_len = 0; continue; }
        if (now - last_change < STALL_MS) continue;
        int holder[MAXP], wants[MAXP], cyc[MAXP];
        for (int i = 0; i < n; i++) { wants[i] = atomic_load(&t->wants[i]); holder[i] = fork_holder(&t->fork[i]); }
        int len = wfg_find_cycle(n, holder, wants, cyc);
        if (len > 0 && len == prev_len && memcmp(cyc, prev_cyc, sizeof(int) * (size_t)len) == 0) {
            r->deadlocked = 1; r->cycle_len = len; r->deadlock_at_ms = now - t0;
            format_deadlock(r->report, sizeof r->report, len, cyc, wants);
            if (!c->quiet) printf("%s\n", r->report);
            halt_all(t);                                  /* 회복 = 모두에게서 포크를 "빼앗는" 대신 스스로 놓게 */
            continue;
        }
        memcpy(prev_cyc, cyc, sizeof prev_cyc); prev_len = len;
        if (now - last_change >= HARD_STALL_MS) {
            r->stalled = 1;
            if (!c->quiet) printf("STALL: no meal for %d ms, no wait-for cycle (livelock/starvation?)\n", HARD_STALL_MS);
            halt_all(t);
        }
    }
    for (int p = 0; p < n; p++) pthread_join(th[p], NULL);
    r->ms = now_ms() - t0;
    r->forks_free_after = 1;
    for (int f = 0; f < n; f++) if (fork_holder(&t->fork[f]) != -1) r->forks_free_after = 0;
    for (int p = 0; p < n; p++) { r->meals[p] = atomic_load(&t->meals[p]); r->total += r->meals[p]; }
    r->retries = atomic_load(&t->retries);
    r->violations = atomic_load(&t->violations);
    r->max_seated = atomic_load(&t->max_seated);
    for (int f = 0; f < n; f++) fork_destroy(&t->fork[f]);
    csem_destroy(&t->seats);
    pthread_mutex_destroy(&t->gate.m); pthread_cond_destroy(&t->gate.cv);
}

static void ring_report(char *buf, size_t cap, int n) {   /* 기대값: P0 holds F0 waits F1, ..., P(n-1) holds F(n-1) waits F0 */
    size_t off = (size_t)snprintf(buf, cap, "DEADLOCK:");
    for (int p = 0; p < n && off < cap; p++)
        off += (size_t)snprintf(buf + off, cap - off, "%s P%d holds F%d waits F%d", p ? "," : "", p, p, (p + 1) % n);
}
static bool all_meals(const result_t *r, int n, long target) {
    for (int p = 0; p < n; p++) if (r->meals[p] != target) return false;
    return true;
}
static void print_run(const char *tag, const result_t *r, int n) {
    long mn = r->meals[0], mx = r->meals[0];
    for (int p = 1; p < n; p++) { if (r->meals[p] < mn) mn = r->meals[p]; if (r->meals[p] > mx) mx = r->meals[p]; }
    printf("  [%s] deadlocked=%d meals=%ld (min %ld max %ld) retries=%ld max_seated=%d %.0f ms\n",
           tag, r->deadlocked, r->total, mn, mx, r->retries, r->max_seated, r->ms);
}

/* ===== Part B 락스텝 강제 스케줄: 모두 같은 박자로 (배리어 3 개/판) → 백오프가 고정이면 라이브락 ===== */
typedef struct {
    int n, rounds; bool randomized; uint32_t seed;
    bar_t bar; fork_t fork[MAXP]; atomic_int halt;
    long meals[MAXP], attempts[MAXP]; atomic_int anomalies, bar_timeouts;
} lockstep_t;
typedef struct { lockstep_t *L; int p; } ls_arg_t;

static void ls_bar(lockstep_t *L) { if (bar_wait(&L->bar, 2000) < 0) atomic_fetch_add(&L->bar_timeouts, 1); }
static void *ls_main(void *arg) {
    ls_arg_t *a = arg;
    lockstep_t *L = a->L;
    int p = a->p, left = p, right = (p + 1) % L->n;
    uint32_t rng = L->seed * 2654435761u + (uint32_t)p * 40503u + 1u;
    unsigned attempt = 0, skip = 0;
    for (int r = 0; r < L->rounds; r++) {
        bool go = (skip == 0);
        if (!go) skip--;
        if (go && !fork_try(&L->fork[left], p)) atomic_fetch_add(&L->anomalies, 1);   /* 1 박: 왼쪽 */
        ls_bar(L);
        bool got = go && fork_try(&L->fork[right], p);                                  /* 2 박: 오른쪽 try */
        ls_bar(L);
        if (go) {                                                                       /* 3 박: 먹거나 내려놓기 */
            L->attempts[p]++;
            if (got) { L->meals[p]++; attempt = 0; fork_put(&L->fork[right], p); }
            else skip = backoff_rounds(&rng, ++attempt, L->randomized) - 1u;
            fork_put(&L->fork[left], p);
        }
        ls_bar(L);
    }
    return NULL;
}
static lockstep_t g_ls;
static void run_lockstep(int n, int rounds, bool randomized, uint32_t seed, long meals[], long attempts[], int *bad) {
    lockstep_t *L = &g_ls;
    memset(L, 0, sizeof *L);
    L->n = n; L->rounds = rounds; L->randomized = randomized; L->seed = seed;
    bar_init(&L->bar, n);
    for (int f = 0; f < n; f++) fork_init(&L->fork[f]);
    pthread_t th[MAXP]; ls_arg_t args[MAXP];
    for (int p = 0; p < n; p++) { args[p] = (ls_arg_t){ L, p }; pthread_create(&th[p], NULL, ls_main, &args[p]); }
    for (int p = 0; p < n; p++) pthread_join(th[p], NULL);
    for (int p = 0; p < n; p++) { meals[p] = L->meals[p]; attempts[p] = L->attempts[p]; }
    *bad = atomic_load(&L->anomalies) + atomic_load(&L->bar_timeouts);
    for (int f = 0; f < n; f++) fork_destroy(&L->fork[f]);
    bar_destroy(&L->bar);
}

/* ===== Part C 펌웨어: 순위 있는 RTOS 락 (단일 스레드 스크립트 모델) ===== */
enum { FW_OK = 0, FW_E_ISR = -1, FW_E_ORDER = -2, FW_E_BUSY = -3, FW_E_NOTOWNER = -4 };
typedef struct { const char *name; int rank; int owner; } fw_lock_t;   /* owner = 태스크 id, -1 = 놓임 */
#define FW_MAXHELD 8
typedef struct { int id; bool in_isr; int nheld; fw_lock_t *held[FW_MAXHELD]; } fw_task_t;

/* C2: 규칙 (순서대로 검사):
 *   1) ISR 문맥이면 FW_E_ISR — ISR 은 뮤텍스를 기다릴 수 없다 (C-M14).
 *   2) 이미 쥔 락 중 rank >= lk->rank 가 하나라도 있으면 FW_E_ORDER — 순위는 오름차순으로만 (재귀도 여기서 걸림).
 *      실제로 막히기 "전에" 거부하므로, 지금 교착이 안 나도 순서 위반이 드러난다.
 *   3) 남이 쥐고 있으면 FW_E_BUSY (실제 RTOS 라면 여기서 블록 — 모델은 스크립트를 계속).
 *   4) 아니면 owner = 태스크, held 에 push, FW_OK. */
static int fw_take(fw_task_t *tk, fw_lock_t *lk) {
    if (tk->in_isr) return FW_E_ISR;
    for (int i = 0; i < tk->nheld; i++)
        if (tk->held[i]->rank >= lk->rank) return FW_E_ORDER;
    if (lk->owner != -1) return FW_E_BUSY;
    if (tk->nheld == FW_MAXHELD) return FW_E_ORDER;
    lk->owner = tk->id;
    tk->held[tk->nheld++] = lk;
    return FW_OK;
}
static int fw_give(fw_task_t *tk, fw_lock_t *lk) {
    if (lk->owner != tk->id) return FW_E_NOTOWNER;
    for (int i = 0; i < tk->nheld; i++)
        if (tk->held[i] == lk) { tk->held[i] = tk->held[--tk->nheld]; break; }
    lk->owner = -1;
    return FW_OK;
}

typedef struct { csem_t *s; atomic_int got; } csem_arg_t;
static void *csem_waiter(void *arg) {
    csem_arg_t *a = arg;
    atomic_store(&a->got, csem_wait(a->s, NULL, 2000) ? 1 : 2);
    return NULL;
}

#ifndef PHILO_NO_MAIN
int main(void) {
    char want[600];
    result_t r;

    /* ---------- Part A: 교착 재현 · 탐지 · 회복, 해법 1 = 전역 순서 ---------- */
    printf("== Part A: naive forced schedule (expect DEADLOCK) ==\n");
    cfg_t naive5 = { .n = 5, .strat = S_NAIVE, .meals_target = 50, .forced = true, .seed = 1 };
    run_table(&naive5, &r);
    ring_report(want, sizeof want, 5);
    CHECK(r.deadlocked == 1 && r.cycle_len == 5);
    CHECK(strcmp(r.report, want) == 0);
    CHECK(r.total == 0 && r.forks_free_after == 1 && r.gate_timeout == 0);   /* 회복: 모두 끝나고 포크는 놓임 */

    cfg_t naive12 = naive5; naive12.n = 12;
    run_table(&naive12, &r);
    ring_report(want, sizeof want, 12);
    CHECK(r.deadlocked == 1 && r.cycle_len == 12 && strcmp(r.report, want) == 0);

    {   /* 대기 그래프 단위 테스트: 순환이 P2 에서 시작해도 가장 작은 번호부터 */
        int holder[4] = { 0, 3, 1, -1 }, wants[4] = { -1, 1, -1, 2 }, cyc[MAXP];   /* P3 → F2(P1) → F1(P3) */
        CHECK(wfg_find_cycle(4, holder, wants, cyc) == 2 && cyc[0] == 1 && cyc[1] == 3);
        int h2[3] = { 0, 1, -1 }, w2[3] = { 1, 2, -1 };                             /* 사슬일 뿐 순환 없음 */
        CHECK(wfg_find_cycle(3, h2, w2, cyc) == 0);
    }

    int f1, s1, f2, s2, f3, s3;
    fork_order(S_ORDERED, 4, 5, &f1, &s1);
    fork_order(S_ORDERED, 0, 5, &f2, &s2);
    fork_order(S_ORDERED, 11, 12, &f3, &s3);
    CHECK(f1 == 0 && s1 == 4 && f2 == 0 && s2 == 1 && f3 == 0 && s3 == 11);
    fork_order(S_NAIVE, 4, 5, &f1, &s1);
    CHECK(f1 == 4 && s1 == 0);
    bool unit_a = (f2 == 0 && s2 == 1 && f3 == 0);

    printf("== Part A: ordered (lowest fork first) ==\n");
    cfg_t ord = naive5; ord.strat = S_ORDERED;
    run_table(&ord, &r); print_run("ordered n=5 forced", &r, 5);
    CHECK(r.deadlocked == 0 && all_meals(&r, 5, 50) && r.violations == 0 && r.forks_free_after);
    ord.n = 12;
    run_table(&ord, &r); print_run("ordered n=12 forced", &r, 12);
    CHECK(r.deadlocked == 0 && all_meals(&r, 12, 50) && r.violations == 0);
    if (unit_a) {
        cfg_t fr = { .n = 12, .strat = S_ORDERED, .meals_target = 20000, .think_iters = 100, .eat_iters = 100, .seed = 7 };
        run_table(&fr, &r); print_run("ordered n=12 free", &r, 12);
        CHECK(r.deadlocked == 0 && r.stalled == 0 && all_meals(&r, 12, 20000) && r.violations == 0);
    } else {
        printf("SKIP 1 stress check (Part A): unit checks failed\n");
    }

    /* ---------- Part B: 해법 2 = trylock + 무작위 백오프, 그리고 라이브락 ---------- */
    printf("== Part B: backoff + lockstep livelock ==\n");
    uint32_t rng = 12345u;
    bool in_range = true, varied = false;
    unsigned firstv = backoff_rounds(&rng, 3, true);
    for (int i = 0; i < 200; i++) {
        unsigned a = (unsigned)(i % 9) + 1u, v = backoff_rounds(&rng, a, true);
        unsigned cap = a < 6u ? a : 6u;
        if (v < 1u || v > (1u << cap)) in_range = false;
        if (v != firstv) varied = true;
    }
    CHECK(backoff_rounds(&rng, 1, false) == 1u && backoff_rounds(&rng, 9, false) == 1u);
    CHECK(in_range && varied);

    long meals[MAXP], att[MAXP], meals2[MAXP], att2[MAXP];
    int bad;
    run_lockstep(5, 200, false, 1, meals, att, &bad);
    long sum_m = 0, sum_a = 0;
    for (int p = 0; p < 5; p++) { sum_m += meals[p]; sum_a += att[p]; }
    printf("  lockstep fixed     : meals=%ld attempts=%ld (fork pick-ups never stop, nobody eats)\n", sum_m, sum_a);
    CHECK(bad == 0 && sum_m == 0 && sum_a == 5 * 200);          /* 라이브락: 일은 하는데 진전 0 */

    run_lockstep(5, 200, true, 1, meals, att, &bad);
    int bad2;
    run_lockstep(5, 200, true, 1, meals2, att2, &bad2);
    bool everyone = true, same = true;
    sum_m = 0;
    for (int p = 0; p < 5; p++) {
        sum_m += meals[p];
        if (meals[p] < 1) everyone = false;
        if (meals[p] != meals2[p] || att[p] != att2[p]) same = false;
    }
    printf("  lockstep randomized: meals=%ld [%ld %ld %ld %ld %ld]\n", sum_m, meals[0], meals[1], meals[2], meals[3], meals[4]);
    CHECK(bad == 0 && bad2 == 0 && everyone && same);           /* 같은 seed → 같은 결과 (결정적) */

    printf("== Part B: trylock + randomized backoff on the real table ==\n");
    cfg_t tl = { .n = 5, .strat = S_TRYLOCK, .meals_target = 50, .forced = true, .backoff_us = 5, .seed = 3 };
    run_table(&tl, &r); print_run("trylock n=5 forced", &r, 5);
    CHECK(r.deadlocked == 0 && all_meals(&r, 5, 50) && r.retries >= 1 && r.violations == 0 && r.forks_free_after);
    tl.n = 12;
    run_table(&tl, &r); print_run("trylock n=12 forced", &r, 12);
    CHECK(r.deadlocked == 0 && all_meals(&r, 12, 50) && r.retries >= 1 && r.violations == 0);
    if (in_range && varied) {
        cfg_t fr = { .n = 5, .strat = S_TRYLOCK, .meals_target = 20000, .think_iters = 100, .eat_iters = 100,
                     .backoff_us = 2, .seed = 9 };
        run_table(&fr, &r); print_run("trylock n=5 free", &r, 5);
        CHECK(r.deadlocked == 0 && r.stalled == 0 && all_meals(&r, 5, 20000) && r.violations == 0);
    } else {
        printf("SKIP 1 stress check (Part B): unit checks failed\n");
    }

    /* ---------- Part C: 해법 3 = 동시 착석 n-1, 펌웨어 순위 락 ---------- */
    printf("== Part C: counting semaphore + seated n-1 ==\n");
    csem_t s;
    csem_init(&s, 2);
    bool w1 = csem_wait(&s, NULL, 50), w2 = csem_wait(&s, NULL, 50), w3 = csem_wait(&s, NULL, 50);
    CHECK(w1 && w2 && !w3);                                      /* 2 자리 → 셋째는 50 ms 뒤 포기 */
    csem_post(&s);
    CHECK(csem_wait(&s, NULL, 50));
    csem_arg_t ca = { &s, 0 };
    pthread_t wt;
    pthread_create(&wt, NULL, csem_waiter, &ca);
    sleep_us(20000);
    csem_post(&s);                                               /* 자고 있던 대기자를 깨운다 */
    pthread_join(wt, NULL);
    CHECK(atomic_load(&ca.got) == 1);
    csem_destroy(&s);
    bool unit_c = w1 && w2 && !w3;

    cfg_t st = naive5; st.strat = S_SEATED;
    run_table(&st, &r); print_run("seated n=5 forced", &r, 5);
    CHECK(r.deadlocked == 0 && all_meals(&r, 5, 50) && r.max_seated == 4 && r.violations == 0 && r.forks_free_after);
    if (unit_c) {
        cfg_t fr = { .n = 12, .strat = S_SEATED, .meals_target = 20000, .think_iters = 100, .eat_iters = 100, .seed = 5 };
        run_table(&fr, &r); print_run("seated n=12 free", &r, 12);
        CHECK(r.deadlocked == 0 && r.stalled == 0 && all_meals(&r, 12, 20000) && r.max_seated <= 11 && r.violations == 0);
    } else {
        printf("SKIP 1 stress check (Part C): unit checks failed\n");
    }

    printf("== Part C: firmware ranked locks ==\n");
    fw_lock_t i2c = { "i2c_bus", 10, -1 }, cache = { "sensor_cache", 20, -1 }, logl = { "log", 30, -1 };
    fw_task_t sensor = { .id = 1 }, logger = { .id = 2 }, isr = { .id = 9, .in_isr = true };
    CHECK(fw_take(&sensor, &i2c) == FW_OK && fw_take(&sensor, &cache) == FW_OK && sensor.nheld == 2);
    CHECK(fw_take(&logger, &logl) == FW_OK && fw_take(&logger, &i2c) == FW_E_ORDER && i2c.owner == 1 && logger.nheld == 1);
    CHECK(fw_take(&logger, &logl) == FW_E_ORDER);                /* 재귀 = 자기 자신과의 교착 */
    CHECK(fw_take(&isr, &logl) == FW_E_ISR && isr.nheld == 0);
    fw_task_t idle = { .id = 3 };
    CHECK(fw_take(&idle, &i2c) == FW_E_BUSY && idle.nheld == 0);
    CHECK(fw_give(&logger, &i2c) == FW_E_NOTOWNER);
    /* 고친 로거: 메시지를 복사하고 log 를 놓은 뒤, 낮은 순위부터 다시 */
    CHECK(fw_give(&logger, &logl) == FW_OK && fw_give(&sensor, &cache) == FW_OK && fw_give(&sensor, &i2c) == FW_OK);
    CHECK(fw_take(&logger, &i2c) == FW_OK && fw_take(&logger, &logl) == FW_OK && logger.nheld == 2);

    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
#endif
