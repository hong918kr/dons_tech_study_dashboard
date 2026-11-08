/* ENV: linux */
/* OS-A01 futex 기반 뮤텍스 구현 (Drepper 방식)
 *
 * futex = "사용자 공간의 int 하나 + 커널의 대기 큐". 경합이 없으면 원자 연산 하나로 끝나고(syscall 0),
 * 경합일 때만 FUTEX_WAIT / FUTEX_WAKE 로 커널에 들어간다.
 *
 *   Part A  raw syscall(SYS_futex) 래퍼 (FUTEX_WAIT_PRIVATE / FUTEX_WAKE_PRIVATE, -EAGAIN/-EINTR/-ETIMEDOUT)
 *           + Drepper "Futexes Are Tricky" §5 mutex2: 0 = 풀림, 1 = 잠김(대기자 없음), 2 = 잠김(대기자 있을 수 있음)
 *   Part B  순진한 0/1 버전 두 개
 *             naiveq : 잠드는 자리(q)가 상태 단어(v)와 따로 → 강제 스케줄에서 lost wakeup
 *             naive  : 상태 단어에서 잠듦 → 정확하지만 unlock 마다 FUTEX_WAKE syscall
 *           강제 스케줄(훅) 실행기 + 경합 없는 syscall 수 세기
 *   Part C  IMU 데몬의 I2C 버스 락: timedlock (시간 초과 + EINTR 재시도) + 1 kHz 제어 틱
 *
 * CHECK 는 매 실행 항상 성립하는 것만 검사한다 (정확한 syscall 수, 상호 배제, 강제 스케줄 결과).
 * 타이밍·처리량은 demo_bench.c (이 파일 대신 practice.c 를 #include) 에서 잰다.
 *
 * build (Lima VM elinux): gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so
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

static void on_sigusr1(int s) { (void)s; } /* 아무것도 안 함: FUTEX_WAIT 를 -EINTR 로 끊는 용도 */

/* ---- Part A 래퍼 테스트용 스레드 ---- */
static atomic_int g_word, g_tid, g_ret;
static void *wake_target(void *p) {
    (void)p;
    atomic_store(&g_tid, (int)gettid());
    struct timespec two = { 2, 0 };
    atomic_store(&g_ret, futex_wait(&g_word, 0, &two));
    return NULL;
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

/* ---- Part C: timedlock 을 다른 스레드에서 ---- */
typedef struct { lock_t *m; long budget; atomic_int tid, ret, done; } tjob_t;
static void *timed_waiter(void *p) {
    tjob_t *j = p;
    atomic_store(&j->tid, (int)gettid());
    int r = mutex2_timedlock(j->m, j->budget);
    if (r == 0) mutex2_unlock(j->m);
    atomic_store(&j->ret, r);
    atomic_store(&j->done, 1);
    return NULL;
}

int main(void) {
    static const struct timespec wd = { 0, 300000000L }; /* 300 ms */
    g_watchdog = &wd;
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigusr1;                /* SA_RESTART 없음 → FUTEX_WAIT 가 -EINTR 로 돌아온다 */
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    puts("== Part A: futex 래퍼 ==");
    fsnap_t s0 = snap();
    atomic_store(&g_word, 7);
    CHECK(futex_wait(&g_word, 0, NULL) == -EAGAIN);          /* 값이 다르면 잠들지 않는다 (커널 비교) */
    struct timespec t20 = { 0, 20000000L };
    CHECK(futex_wait(&g_word, 7, &t20) == -ETIMEDOUT);       /* 같으면 잔다 → 20 ms 후 시간 초과 */
    CHECK(futex_wake(&g_word, 1) == 0);                      /* 아무도 없으면 0 명 */
    fsnap_t d = diff(snap(), s0);
    CHECK(d.wait == 2 && d.eagain == 1 && d.timedout == 1 && d.wake == 1 && d.woken == 0);

    atomic_store(&g_word, 0); atomic_store(&g_tid, 0); atomic_store(&g_ret, 99);
    pthread_t th;
    pthread_create(&th, NULL, wake_target, NULL);
    int seen = wait_flag(&g_tid, 1000) && in_futex_wait(atomic_load(&g_tid), &g_word, 1000);
    atomic_store(&g_word, 1);
    int woke = futex_wake(&g_word, 1);
    pthread_join(th, NULL);
    CHECK(seen && woke == 1 && atomic_load(&g_ret) == 0);    /* 자는 것을 확인 → wake 1 명 → 0 반환 */

    atomic_store(&g_word, 0); atomic_store(&g_tid, 0); atomic_store(&g_ret, 99);
    pthread_create(&th, NULL, wake_target, NULL);
    seen = wait_flag(&g_tid, 1000) && in_futex_wait(atomic_load(&g_tid), &g_word, 1000);
    pthread_kill(th, SIGUSR1);
    pthread_join(th, NULL);
    CHECK(seen && atomic_load(&g_ret) == -EINTR);            /* 시그널 → -EINTR (SA_RESTART 없음) */

    puts("== Part A: mutex2 상태 ==");
    lock_t m = LOCK_INIT;
    s0 = snap();
    mutex2_lock(&m);
    CHECK(atomic_load(&m.v) == 1);                           /* 0 → 1 */
    mutex2_unlock(&m);
    CHECK(atomic_load(&m.v) == 0);
    for (int i = 0; i < 1000; i++) { mutex2_lock(&m); mutex2_unlock(&m); }
    d = diff(snap(), s0);
    CHECK(d.wait == 0 && d.wake == 0);                       /* 경합 없음 = syscall 0 */
    atomic_store(&m.v, 2);                                   /* "대기자 있을 수 있음" 상태에서 unlock 하면 */
    s0 = snap();
    mutex2_unlock(&m);
    d = diff(snap(), s0);
    CHECK(atomic_load(&m.v) == 0 && d.wake == 1 && d.woken == 0); /* wake 1 번 (아무도 없어서 0 명) */

    tjob_t tj = { &m, 2000000000L, 0, 99, 0 };              /* 대기자 하나가 실제로 잠들면 v == 2 */
    mutex2_lock(&m);
    pthread_create(&th, NULL, timed_waiter, &tj);
    seen = wait_flag(&tj.tid, 1000) && in_futex_wait(atomic_load(&tj.tid), &m.v, 1000);
    CHECK(seen && atomic_load(&m.v) == 2);
    s0 = snap();
    mutex2_unlock(&m);
    pthread_join(th, NULL);
    d = diff(snap(), s0);
    CHECK(atomic_load(&tj.ret) == 0 && atomic_load(&m.v) == 0); /* 깨어나 잡고(2 로) 풀었다 */
    CHECK(d.wake == 2 && d.woken == 1);                      /* main 의 wake 는 1 명, 대기자의 unlock 은 헛 wake */

    puts("== Part A: 경합 스트레스 (8 스레드) ==");
    CHECK(stress(&OPS_MUTEX2, 8, 20000));
    CHECK(stress(&OPS_NAIVE, 8, 20000));

    puts("== Part B: 강제 스케줄 — 잠들기 직전에 unlock 이 끼어들면 ==");
    sched_out_t so;
    CHECK(forced_schedule(&OPS_NAIVEQ, &so) == 0 && so.acquired);
    printf("  naiveq: wait %ld wake %ld woken %ld eagain %ld timedout %ld\n",
           so.d.wait, so.d.wake, so.d.woken, so.d.eagain, so.d.timedout);
    CHECK(so.d.timedout == 1 && so.d.woken == 0);           /* lost wakeup: wake 는 빈 큐로, W 는 watchdog 까지 잠 */
    CHECK(forced_schedule(&OPS_NAIVE, &so) == 0 && so.acquired);
    printf("  naive : wait %ld wake %ld woken %ld eagain %ld timedout %ld\n",
           so.d.wait, so.d.wake, so.d.woken, so.d.eagain, so.d.timedout);
    CHECK(so.d.eagain == 1 && so.d.timedout == 0);          /* 커널이 v==1 아님을 보고 즉시 돌려보냄 */
    CHECK(forced_schedule(&OPS_MUTEX2, &so) == 0 && so.acquired);
    printf("  mutex2: wait %ld wake %ld woken %ld eagain %ld timedout %ld\n",
           so.d.wait, so.d.wake, so.d.woken, so.d.eagain, so.d.timedout);
    CHECK(so.d.eagain == 1 && so.d.timedout == 0 && so.v_after == 0);

    puts("== Part B: 경합 없는 1000 쌍의 syscall ==");
    fsnap_t u = count_uncontended(&OPS_NAIVE, 1000);
    printf("  naive : wait %ld wake %ld\n", u.wait, u.wake);
    CHECK(u.wake == 1000 && u.wait == 0 && u.woken == 0);   /* unlock 마다 헛 FUTEX_WAKE */
    u = count_uncontended(&OPS_MUTEX2, 1000);
    printf("  mutex2: wait %ld wake %ld\n", u.wait, u.wake);
    CHECK(u.wake == 0 && u.wait == 0);

    puts("== Part C: timedlock / IMU 버스 ==");
    lock_t b = LOCK_INIT;
    CHECK(mutex2_timedlock(&b, 1000000L) == 0 && atomic_load(&b.v) == 1); /* 풀려 있으면 즉시 */
    s0 = snap();
    CHECK(mutex2_timedlock(&b, 30000000L) == ETIMEDOUT);    /* 자기 자신이 쥔 락 (비재귀) → 30 ms 후 포기 */
    d = diff(snap(), s0);
    CHECK(atomic_load(&b.v) == 2 && d.timedout >= 1);        /* 포기해도 "대기자 있음" 표시는 남는다 */
    s0 = snap();
    CHECK(mutex2_timedlock(&b, 0) == ETIMEDOUT);
    CHECK(diff(snap(), s0).wait == 0);                       /* 예산 0 → 자지 않고 바로 포기 */
    s0 = snap();
    mutex2_unlock(&b);
    d = diff(snap(), s0);
    CHECK(atomic_load(&b.v) == 0 && d.wake == 1 && d.woken == 0); /* 그래서 헛 wake 1 번 — 정확성엔 무해 */

    tjob_t ej = { &b, 5000000000L, 0, 99, 0 };              /* 5 s 예산, 도중에 시그널 */
    mutex2_lock(&b);
    s0 = snap();
    pthread_create(&th, NULL, timed_waiter, &ej);
    seen = wait_flag(&ej.tid, 1000) && in_futex_wait(atomic_load(&ej.tid), &b.v, 1000);
    pthread_kill(th, SIGUSR1);
    int eintr_seen = 0;
    for (int i = 0; i < 1000 && !eintr_seen; i++) { eintr_seen = diff(snap(), s0).eintr >= 1; sleep_ms(1); }
    int back = in_futex_wait(atomic_load(&ej.tid), &b.v, 1000); /* EINTR 후 남은 시간으로 다시 잤다 */
    int early = atomic_load(&ej.done);
    mutex2_unlock(&b);
    pthread_join(th, NULL);
    CHECK(seen && eintr_seen && back && !early);
    CHECK(atomic_load(&ej.ret) == 0 && atomic_load(&b.v) == 0); /* EINTR 은 실패가 아니다 → 결국 획득 */

    imu_t imu = { LOCK_INIT, 0, -1, 0, 0 };
    imu.reg = 100;
    CHECK(ctrl_tick(&imu, 800000L) == 1 && imu.last == 100); /* 버스 한가함 → 새 샘플 */
    mutex2_lock(&imu.bus);                                   /* 로거가 긴 eMMC 덤프 중 버스를 쥠 */
    imu.reg = 200;
    CHECK(ctrl_tick(&imu, 800000L) == 0 && imu.last == 100); /* 800 µs 안에 못 잡음 → 직전 샘플 */
    mutex2_unlock(&imu.bus);
    CHECK(ctrl_tick(&imu, 800000L) == 1 && imu.last == 200 && imu.fresh == 2 && imu.stale == 1);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif /* OSA01_LIB_ONLY */
