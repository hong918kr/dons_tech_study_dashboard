/* ENV: linux */
/* OS-A01 추가 연습 — 한 번에 검증
 *
 *   연습 1  mutex3 (Drepper §6, xchg 판) + glibc lll_unlock 식 unlock (xchg 0)
 *   연습 2  trylock + "잠깐 돌고 나서 잔다" (spin-then-futex, adaptive)
 *   연습 3  "다시 잡을 때 1 로" 버그: 대기자 2 명 강제 스케줄 → lost wakeup
 *   연습 4  futex 래치 (count-down latch): 마지막 count_down 이 FUTEX_WAKE(INT_MAX) 한 번
 *
 * 본문 solution.c 의 래퍼 · mutex2 · 순진한 버전 · 강제 스케줄 도구 · timedlock 을 그대로 담고 있다.
 * demo_bench.c 는 이 파일을 OSA01_LIB_ONLY 로 #include 해서 같은 락을 잰다.
 *
 * build (Lima VM elinux): gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr
 */
#define _GNU_SOURCE /* syscall(), gettid(), pthread_kill 등 리눅스 API 를 -std=c11 에서 보이게 */
#include <errno.h>
#include <limits.h>
#include <linux/futex.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

_Static_assert(sizeof(atomic_int) == sizeof(int), "futex word must be a plain 32-bit int");

/* ======================= 통계: 래퍼가 syscall 마다 센다 ======================= */
typedef struct { atomic_long wait, wake, woken, eagain, eintr, timedout; } fstats_t;
typedef struct { long wait, wake, woken, eagain, eintr, timedout; } fsnap_t;
static fstats_t g_fs;
#define CNT(f) atomic_fetch_add_explicit(&g_fs.f, 1, memory_order_relaxed)

static fsnap_t snap(void) {
    fsnap_t s = { atomic_load(&g_fs.wait), atomic_load(&g_fs.wake), atomic_load(&g_fs.woken),
                  atomic_load(&g_fs.eagain), atomic_load(&g_fs.eintr), atomic_load(&g_fs.timedout) };
    return s;
}
static fsnap_t diff(fsnap_t b, fsnap_t a) {
    fsnap_t d = { b.wait - a.wait, b.wake - a.wake, b.woken - a.woken,
                  b.eagain - a.eagain, b.eintr - a.eintr, b.timedout - a.timedout };
    return d;
}

/* 잠들기 상한(watchdog). NULL = 무한 (벤치). 테스트는 300 ms: 잃어버린 wakeup 을 "타임아웃" 으로 센다. */
static const struct timespec *g_watchdog;

/* ======================= 강제 스케줄 훅 ======================= */
enum { HK_BEFORE_WAIT = 1 };
static void (*g_hook)(int where); /* 스레드가 없을 때만 바꾼다 (create/join 이 순서를 만든다) */
#ifdef NO_HOOKS
#define HOOK(w) ((void)0)
#else
#define HOOK(w) do { if (g_hook) g_hook(w); } while (0)
#endif

static void sleep_ms(long ms) {
    struct timespec d = { ms / 1000, (ms % 1000) * 1000000L };
    while (nanosleep(&d, &d) != 0 && errno == EINTR) { }
}

/* ======================= Part A — futex 래퍼 ======================= */
/* *addr == expected 일 때만 잠든다 — 비교와 잠들기를 커널이 "한 번에" 한다 (lost wakeup 을 막는 핵심).
 * 반환: 0 = 깨워짐(가짜 깨어남 포함), -EAGAIN = 값이 이미 달랐음, -EINTR = 시그널, -ETIMEDOUT = rel 만료.
 * rel = 상대 시간 (CLOCK_MONOTONIC 기준), NULL = 무한. */
static int futex_wait(atomic_int *addr, int expected, const struct timespec *rel) {
    CNT(wait);
    long r = syscall(SYS_futex, (int *)addr, FUTEX_WAIT_PRIVATE, expected, rel, NULL, 0);
    if (r == 0) return 0;
    int e = errno;
    if (e == EAGAIN) CNT(eagain);
    else if (e == EINTR) CNT(eintr);
    else if (e == ETIMEDOUT) CNT(timedout);
    return -e;
}

/* addr 에서 잠든 스레드를 최대 n 개 깨운다. 반환 = 실제로 깨운 수 (아무도 없으면 0), 실패 시 -errno. */
static int futex_wake(atomic_int *addr, int n) {
    CNT(wake);
    long r = syscall(SYS_futex, (int *)addr, FUTEX_WAKE_PRIVATE, n, NULL, NULL, 0);
    if (r < 0) return -errno;
    atomic_fetch_add_explicit(&g_fs.woken, r, memory_order_relaxed);
    return (int)r;
}

/* ======================= 락 공통 ======================= */
typedef struct { atomic_int v; atomic_int q; } lock_t; /* v = 상태 단어, q = naiveq 전용 "잠드는 자리" */
#define LOCK_INIT { 0, 0 }
typedef struct { const char *name; void (*lock)(lock_t *); void (*unlock)(lock_t *); } lock_ops_t;

/* Drepper 의 cmpxchg: 성공/실패와 무관하게 "이전 값" 을 돌려준다. 성공 시 acquire. */
static int cas(atomic_int *p, int expected, int desired) {
    atomic_compare_exchange_strong_explicit(p, &expected, desired, memory_order_acquire, memory_order_relaxed);
    return expected;
}

/* ======================= Part A — Drepper mutex2 (0/1/2) ======================= */
static void mutex2_lock(lock_t *m) {
    int c = cas(&m->v, 0, 1);
    if (c == 0) return;                              /* 빠른 길: 0→1, syscall 없음 */
    do {
        /* 이미 2 이거나, 1→2 로 "대기자 있음" 표시에 성공했으면 잔다. cas 가 0 을 보면(그새 풀림) 자지 않는다. */
        if (c == 2 || cas(&m->v, 1, 2) != 0) {
            HOOK(HK_BEFORE_WAIT);
            futex_wait(&m->v, 2, g_watchdog);        /* v != 2 면 커널이 즉시 -EAGAIN */
        }
        /* 다시 잡을 때는 1 이 아니라 2 로: 다른 대기자가 아직 자고 있을지 모른다 */
    } while ((c = cas(&m->v, 0, 2)) != 0);
}

static void mutex2_unlock(lock_t *m) {
    if (atomic_fetch_sub_explicit(&m->v, 1, memory_order_release) != 1) { /* 이전 값 2 → 대기자 있을 수 있음 */
        atomic_store_explicit(&m->v, 0, memory_order_release);
        futex_wake(&m->v, 1);                        /* 하나만 깨운다 (나머지는 어차피 다시 잘 것) */
    }                                                /* 이전 값 1 → 대기자 없음 → syscall 없음 */
}

/* ======================= Part B — 순진한 0/1 버전 두 개 (주어진 코드) ======================= */
/* naiveq: 잠드는 자리 q 는 항상 0 → 커널 비교가 아무것도 막지 못한다 = "검사(v) 따로, 잠들기(q) 따로" */
static void naiveq_lock(lock_t *m) {
    while (atomic_exchange_explicit(&m->v, 1, memory_order_acquire) != 0) {
        HOOK(HK_BEFORE_WAIT);
        futex_wait(&m->q, 0, g_watchdog);
    }
}
static void naiveq_unlock(lock_t *m) {
    atomic_store_explicit(&m->v, 0, memory_order_release);
    futex_wake(&m->q, 1);
}
/* naive: 상태 단어 v 에서 "1 이면 재워 줘" → 정확하다. 하지만 대기자가 있는지 모르니 unlock 마다 wake. */
static void naive_lock(lock_t *m) {
    while (atomic_exchange_explicit(&m->v, 1, memory_order_acquire) != 0) {
        HOOK(HK_BEFORE_WAIT);
        futex_wait(&m->v, 1, g_watchdog);
    }
}
static void naive_unlock(lock_t *m) {
    atomic_store_explicit(&m->v, 0, memory_order_release);
    futex_wake(&m->v, 1);
}

static const lock_ops_t OPS_MUTEX2 = { "mutex2", mutex2_lock, mutex2_unlock };
static const lock_ops_t OPS_NAIVEQ = { "naiveq", naiveq_lock, naiveq_unlock };
static const lock_ops_t OPS_NAIVE  = { "naive",  naive_lock,  naive_unlock };

/* ---- 강제 스케줄 도구 (주어진 코드) ---- */
static atomic_int g_gate_at, g_gate_go;
static _Thread_local int t_gate_armed;
/* 훅: 무장된 스레드가 "잠들기 직전" 에 처음 도착하면 멈춰서 main 의 신호를 기다린다 (최대 2 s). */
static void gate_hook(int where) {
    if (where != HK_BEFORE_WAIT || !t_gate_armed) return;
    t_gate_armed = 0;
    atomic_store(&g_gate_at, 1);
    for (int i = 0; i < 2000 && !atomic_load(&g_gate_go); i++) sleep_ms(1);
}
static int wait_flag(atomic_int *f, int ms) {
    for (int i = 0; i < ms; i++) { if (atomic_load(f)) return 1; sleep_ms(1); }
    return atomic_load(f) != 0;
}
typedef struct { const lock_ops_t *ops; lock_t *m; atomic_int acquired; } wjob_t;
static void *gated_waiter(void *p) {
    wjob_t *j = p;
    t_gate_armed = 1;
    j->ops->lock(j->m);
    atomic_store(&j->acquired, 1);
    j->ops->unlock(j->m);
    return NULL;
}

typedef struct { fsnap_t d; int at_window, acquired, v_after; } sched_out_t;

/* Part B-1: 강제 스케줄
 *   ① main 이 잡는다  ② W 가 잡으려다 실패하고 "잠들기 직전" 훅에서 멈춘다
 *   ③ 바로 그 창에서 main 이 unlock (+ wake)  ④ W 를 풀어 잠들러 보낸다  ⑤ join
 * 결과: W 가 잠든 뒤 아무도 깨우지 않으면 watchdog 타임아웃 = lost wakeup. */
static int forced_schedule(const lock_ops_t *ops, sched_out_t *out) {
    lock_t m = LOCK_INIT;
    wjob_t j = { ops, &m, 0 };
    memset(out, 0, sizeof *out);
    atomic_store(&g_gate_at, 0);
    atomic_store(&g_gate_go, 0);
    g_hook = gate_hook;
    ops->lock(&m);                                          /* ① */
    fsnap_t s0 = snap();
    pthread_t t;
    if (pthread_create(&t, NULL, gated_waiter, &j) != 0) { g_hook = NULL; return -1; }
    out->at_window = wait_flag(&g_gate_at, 2000);           /* ② */
    ops->unlock(&m);                                        /* ③ */
    atomic_store(&g_gate_go, 1);                            /* ④ */
    pthread_join(t, NULL);                                  /* ⑤ */
    g_hook = NULL;
    out->d = diff(snap(), s0);
    out->acquired = atomic_load(&j.acquired);
    out->v_after = atomic_load(&m.v);
    return out->at_window ? 0 : -1;
}

/* Part B-2: 경합 없는 lock/unlock n 쌍이 낸 syscall 수 */
static fsnap_t count_uncontended(const lock_ops_t *ops, int n) {
    lock_t m = LOCK_INIT;
    fsnap_t s0 = snap();
    for (int i = 0; i < n; i++) { ops->lock(&m); ops->unlock(&m); }
    return diff(snap(), s0);
}

/* ======================= Part C — timedlock + IMU 버스 ======================= */
static long ts_to_ns(const struct timespec *t) { return t->tv_sec * 1000000000L + t->tv_nsec; }
static long now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return ts_to_ns(&t); }

/* 반환 0 = 획득, ETIMEDOUT = timeout_ns 안에 못 잡음. EINTR/EAGAIN/가짜 깨어남은 삼키고 남은 시간으로 다시 잔다. */
static int mutex2_timedlock(lock_t *m, long timeout_ns) {
    int c = cas(&m->v, 0, 1);
    if (c == 0) return 0;
    long deadline = now_ns() + timeout_ns;
    do {
        if (c == 2 || cas(&m->v, 1, 2) != 0) {
            long left = deadline - now_ns();                /* 매번 다시 계산: EINTR 후 처음 시간으로 다시 자면 늘어진다 */
            if (left <= 0) return ETIMEDOUT;                /* v 는 2 로 남는다 → 다음 unlock 이 헛 wake 1 번 */
            struct timespec rel = { left / 1000000000L, left % 1000000000L };
            HOOK(HK_BEFORE_WAIT);
            futex_wait(&m->v, 2, &rel);                     /* 0 / -EAGAIN / -EINTR / -ETIMEDOUT 모두 → 다시 시도 */
        }
    } while ((c = cas(&m->v, 0, 2)) != 0);
    return 0;
}

/* 1 kHz 제어 루프 한 틱: 버스를 budget 안에 못 잡으면 직전 샘플을 쓴다 (마감 > 신선도) */
typedef struct { lock_t bus; int reg; int last; long fresh, stale; } imu_t;
static int ctrl_tick(imu_t *s, long budget_ns) {
    if (mutex2_timedlock(&s->bus, budget_ns) == 0) {
        s->last = s->reg;                                   /* 버스 트랜잭션 (모형) */
        mutex2_unlock(&s->bus);
        s->fresh++;
        return 1;
    }
    s->stale++;
    return 0;
}

/* ======================= 연습 1 — mutex3 (xchg) ======================= */
static void mutex3_lock(lock_t *m) {
    int c = cas(&m->v, 0, 1);
    if (c == 0) return;
    if (c != 2) c = atomic_exchange_explicit(&m->v, 2, memory_order_acquire); /* 무조건 2 로, 이전 값을 본다 */
    while (c != 0) {                                          /* 이전 값 0 = 그새 풀렸고 방금 내가(2 로) 잡았다 */
        HOOK(HK_BEFORE_WAIT);
        futex_wait(&m->v, 2, g_watchdog);
        c = atomic_exchange_explicit(&m->v, 2, memory_order_acquire);
    }
}
/* glibc lll_unlock 과 같은 모양: xchg(0) 의 이전 값이 1 보다 크면 wake. dec + store 대신 원자 연산 한 번 */
static void mutex3_unlock(lock_t *m) {
    if (atomic_exchange_explicit(&m->v, 0, memory_order_release) > 1) futex_wake(&m->v, 1);
}
static const lock_ops_t OPS_MUTEX3 = { "mutex3", mutex3_lock, mutex3_unlock };

/* ======================= 연습 2 — trylock + spin-then-futex ======================= */
static int mutex2_trylock(lock_t *m) { return cas(&m->v, 0, 1) == 0 ? 0 : EBUSY; } /* 절대 자지 않는다 */

static inline void cpu_relax(void) {
#if defined(__aarch64__)
    __asm__ __volatile__("yield" ::: "memory");
#elif defined(__x86_64__)
    __asm__ __volatile__("pause" ::: "memory");
#endif
}
enum { SPIN_TRIES = 100 };
/* 짧은 임계 구역이면 주인이 곧 놓는다 → syscall 대신 잠깐 "읽기만" 하며 돈다 (0 을 볼 때만 CAS = TTAS) */
static void spinmutex_lock(lock_t *m) {
    for (int i = 0; i < SPIN_TRIES; i++) {
        int v = atomic_load_explicit(&m->v, memory_order_relaxed);
        if (v == 0 && cas(&m->v, 0, 1) == 0) return;
        if (v == 2) break;                                    /* 이미 자는 대기자가 있다 → 새치기하지 말고 줄 선다 */
        cpu_relax();
    }
    mutex3_lock(m);                                           /* 느린 길은 mutex3 그대로 */
}
static const lock_ops_t OPS_SPIN = { "spin+futex", spinmutex_lock, mutex3_unlock };

/* ======================= 연습 3 — "다시 잡을 때 1 로" 버그 ======================= */
static void mutex2_bad_lock(lock_t *m) {
    int c = cas(&m->v, 0, 1);
    if (c == 0) return;
    do {
        if (c == 2 || cas(&m->v, 1, 2) != 0) { HOOK(HK_BEFORE_WAIT); futex_wait(&m->v, 2, g_watchdog); }
    } while ((c = cas(&m->v, 0, 1)) != 0);                    /* 버그: 2 가 아니라 1 → "대기자 없음" 이라고 거짓말 */
}
static const lock_ops_t OPS_MUTEX2_BAD = { "mutex2_bad", mutex2_bad_lock, mutex2_unlock };

/* ======================= 연습 4 — futex 래치 ======================= */
typedef struct { atomic_int count; } latch_t;
static void latch_count_down(latch_t *l) {
    if (atomic_fetch_sub_explicit(&l->count, 1, memory_order_acq_rel) == 1) /* 내가 마지막 */
        futex_wake(&l->count, INT_MAX);                       /* 모두 깨운다 (래치는 모두가 통과해야 한다) */
}
static void latch_wait(latch_t *l) {
    int c;
    while ((c = atomic_load_explicit(&l->count, memory_order_acquire)) != 0)
        futex_wait(&l->count, c, g_watchdog);                 /* 본 값 c 를 그대로 넘긴다 → 그새 줄었으면 -EAGAIN */
}

#ifndef OSA01_LIB_ONLY /* demo_bench.c 는 위 함수만 가져가고 아래 하네스는 빼고 컴파일한다 */
/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)

/* /proc/self/task/<tid>/syscall 의 첫 두 필드가 "SYS_futex addr" 이면 그 스레드는 그 futex 에서 자는 중 */
static int in_futex_wait(int tid, const void *addr, int ms) {
    char path[64], buf[256];
    snprintf(path, sizeof path, "/proc/self/task/%d/syscall", tid);
    for (int i = 0; i < ms; i++) {
        FILE *f = fopen(path, "r");
        if (f) {
            long nr = -1; unsigned long a = 0; int ok = 0;
            if (fgets(buf, sizeof buf, f) && sscanf(buf, "%ld %lx", &nr, &a) == 2)
                ok = (nr == SYS_futex && a == (unsigned long)(uintptr_t)addr);
            fclose(f);
            if (ok) return 1;
        }
        sleep_ms(1);
    }
    return 0;
}

/* ---- 경합 스트레스 ---- */
typedef struct { const lock_ops_t *ops; lock_t *m; int iters; } sjob_t;
static atomic_int g_in_cs, g_violations;
static long g_shared; /* 락으로만 보호되는 평범한 변수 (TSan 이 확인) */
static void *stress_worker(void *p) {
    sjob_t *j = p;
    for (int i = 0; i < j->iters; i++) {
        j->ops->lock(j->m);
        if (atomic_fetch_add_explicit(&g_in_cs, 1, memory_order_relaxed) != 0)
            atomic_fetch_add_explicit(&g_violations, 1, memory_order_relaxed);
        g_shared++;
        atomic_fetch_sub_explicit(&g_in_cs, 1, memory_order_relaxed);
        j->ops->unlock(j->m);
    }
    return NULL;
}
static int stress(const lock_ops_t *ops, int nthreads, int iters) {
    lock_t m = LOCK_INIT;
    pthread_t t[16];
    sjob_t j = { ops, &m, iters };
    atomic_store(&g_in_cs, 0); atomic_store(&g_violations, 0); g_shared = 0;
    for (int i = 0; i < nthreads; i++) pthread_create(&t[i], NULL, stress_worker, &j);
    for (int i = 0; i < nthreads; i++) pthread_join(t[i], NULL);
    return atomic_load(&g_violations) == 0 && g_shared == (long)nthreads * iters && atomic_load(&m.v) == 0;
}

/* ---- 연습 3·4: 대기자 여러 명을 "모두 잠든 것을 확인한 뒤" 진행 ---- */
typedef struct { const lock_ops_t *ops; lock_t *m; latch_t *l; atomic_int tid, done; } pjob_t;
static void *lock_once(void *p) {
    pjob_t *j = p;
    atomic_store(&j->tid, (int)gettid());
    j->ops->lock(j->m);
    j->ops->unlock(j->m);
    atomic_store(&j->done, 1);
    return NULL;
}
static void *latch_waiter(void *p) {
    pjob_t *j = p;
    atomic_store(&j->tid, (int)gettid());
    latch_wait(j->l);
    atomic_store(&j->done, 1);
    return NULL;
}
/* main 이 잡은 채로 대기자 2 명을 재우고(확인), unlock 한 번 → 둘 다 끝날 때까지의 syscall 차이 */
static int two_waiters(const lock_ops_t *ops, fsnap_t *d) {
    lock_t m = LOCK_INIT;
    pjob_t j[2] = { { ops, &m, NULL, 0, 0 }, { ops, &m, NULL, 0, 0 } };
    pthread_t t[2];
    ops->lock(&m);
    int asleep = 1;
    for (int i = 0; i < 2; i++) {
        pthread_create(&t[i], NULL, lock_once, &j[i]);
        asleep &= wait_flag(&j[i].tid, 1000) && in_futex_wait(atomic_load(&j[i].tid), &m.v, 1000);
    }
    fsnap_t s0 = snap();
    ops->unlock(&m);
    for (int i = 0; i < 2; i++) pthread_join(t[i], NULL);
    *d = diff(snap(), s0);
    return asleep && atomic_load(&j[0].done) && atomic_load(&j[1].done) && atomic_load(&m.v) == 0;
}

int main(void) {
    static const struct timespec wd = { 0, 300000000L }; /* 300 ms */
    g_watchdog = &wd;
    sched_out_t so;
    fsnap_t d;

    puts("== 본문 회귀: mutex2 · 순진한 버전 · timedlock ==");
    CHECK(count_uncontended(&OPS_MUTEX2, 1000).wake == 0);
    CHECK(count_uncontended(&OPS_NAIVE, 1000).wake == 1000);
    CHECK(forced_schedule(&OPS_NAIVEQ, &so) == 0 && so.d.timedout == 1);
    CHECK(forced_schedule(&OPS_MUTEX2, &so) == 0 && so.d.eagain == 1 && so.d.timedout == 0);
    imu_t imu = { LOCK_INIT, 7, -1, 0, 0 };
    CHECK(ctrl_tick(&imu, 800000L) == 1 && imu.last == 7);
    mutex2_lock(&imu.bus);
    CHECK(mutex2_timedlock(&imu.bus, 1000000L) == ETIMEDOUT);
    mutex2_unlock(&imu.bus);

    puts("== 연습 1: mutex3 ==");
    d = count_uncontended(&OPS_MUTEX3, 1000);
    CHECK(d.wait == 0 && d.wake == 0);
    CHECK(forced_schedule(&OPS_MUTEX3, &so) == 0 && so.acquired && so.d.eagain == 1 && so.d.timedout == 0);
    printf("  mutex3 forced: wait %ld wake %ld woken %ld eagain %ld\n", so.d.wait, so.d.wake, so.d.woken, so.d.eagain);
    CHECK(stress(&OPS_MUTEX3, 8, 20000));
    CHECK(two_waiters(&OPS_MUTEX3, &d) && d.timedout == 0 && d.woken == 2);

    puts("== 연습 2: trylock + spin-then-futex ==");
    lock_t m = LOCK_INIT;
    CHECK(mutex2_trylock(&m) == 0 && atomic_load(&m.v) == 1);
    CHECK(mutex2_trylock(&m) == EBUSY && atomic_load(&m.v) == 1); /* 실패해도 2 로 표시하지 않는다 */
    mutex2_unlock(&m);
    CHECK(atomic_load(&m.v) == 0);
    d = count_uncontended(&OPS_SPIN, 1000);
    CHECK(d.wait == 0 && d.wake == 0);
    CHECK(forced_schedule(&OPS_SPIN, &so) == 0 && so.acquired && so.d.timedout == 0);
    CHECK(stress(&OPS_SPIN, 8, 20000));

    puts("== 연습 3: 다시 잡을 때 1 로 쓰면 ==");
    CHECK(two_waiters(&OPS_MUTEX2_BAD, &d));
    printf("  mutex2_bad: wake %ld woken %ld timedout %ld\n", d.wake, d.woken, d.timedout);
    CHECK(d.timedout == 1 && d.woken == 1);                  /* 두 번째 대기자는 watchdog 까지 잔다 = lost wakeup */
    CHECK(two_waiters(&OPS_MUTEX2, &d));
    printf("  mutex2    : wake %ld woken %ld timedout %ld\n", d.wake, d.woken, d.timedout);
    CHECK(d.timedout == 0 && d.woken == 2);                  /* 2 로 잡았으니 unlock 이 다음 대기자를 깨운다 */

    puts("== 연습 4: futex 래치 ==");
    latch_t l = { 4 };
    pjob_t lj[4];
    pthread_t lt[4];
    int asleep = 1;
    for (int i = 0; i < 4; i++) {
        lj[i] = (pjob_t){ NULL, NULL, &l, 0, 0 };
        pthread_create(&lt[i], NULL, latch_waiter, &lj[i]);
    }
    for (int i = 0; i < 4; i++)
        asleep &= wait_flag(&lj[i].tid, 1000) && in_futex_wait(atomic_load(&lj[i].tid), &l.count, 1000);
    fsnap_t s0 = snap();
    for (int i = 0; i < 3; i++) latch_count_down(&l);
    int early = 0;
    for (int i = 0; i < 4; i++) early |= atomic_load(&lj[i].done);
    latch_count_down(&l);
    for (int i = 0; i < 4; i++) pthread_join(lt[i], NULL);
    d = diff(snap(), s0);
    CHECK(asleep && !early);                                 /* 3 번까지는 아무도 못 지나간다 */
    CHECK(d.wake == 1 && d.woken == 4);                      /* 마지막 한 번의 FUTEX_WAKE 가 4 명을 */
    s0 = snap();
    latch_wait(&l);                                          /* 이미 0 → syscall 없이 통과 */
    CHECK(diff(snap(), s0).wait == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif /* OSA01_LIB_ONLY */
