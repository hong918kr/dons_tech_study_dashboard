// C28_locks_bench.c — OSTEP Ch.28 의 락들을 C11 atomics 로 직접 만들고
// macOS(Apple M2) 의 pthread_mutex / os_unfair_lock 과 비교한다.
//
//  tas       : test-and-set 스핀락        (Fig 28.3, atomic_exchange → swpa)
//  ttas      : test-and-test-and-set      (먼저 읽기만 하며 기다림)
//  cas       : compare-and-swap 스핀락     (Fig 28.4, → casa)
//  llsc      : LL/SC 스핀락, arm64 인라인 어셈블리 ldaxr/stxr (Fig 28.6)
//  ticket    : fetch-and-add 티켓락        (Fig 28.7, → ldadd)
//  tas_yield : TAS + sched_yield()         (Fig 28.8)
//  futex     : 3-상태 futex 뮤텍스 (Drepper) — macOS 의 os_sync_wait_on_address 사용
//  twophase  : 잠깐 스핀 후 futex 로 잠드는 2단계 락 (28.16)
//  pthread   : pthread_mutex_t
//  unfair    : os_unfair_lock (macOS 의 저수준 락)
//
// 실험 1 (처리량): T 개 스레드가 각자 ITERS 번 lock → counter++ → unlock. 결과값 검증 + 시간.
// 실험 2 (공정성): 4 스레드가 300ms 동안 경쟁. 스레드별 획득 횟수의 min/max.
//
// build: cc -Wall -Wextra -O2 -pthread code/C28_locks_bench.c -o .work/bin/C28_locks_bench
#include <os/lock.h>
#include <os/os_sync_wait_on_address.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ------------------------------------------------------------ 락 구현들
typedef struct {
    _Alignas(128) atomic_int flag;     // tas/ttas/cas/llsc/yield/futex 공용 (M2 캐시라인 128B)
    _Alignas(128) atomic_int ticket;   // ticket lock
    _Alignas(128) atomic_int turn;
    pthread_mutex_t mutex;
    os_unfair_lock unfair;
} lock_t;

static atomic_long futex_waits;        // futex 계열이 실제로 커널에서 잠든 횟수

// ---- test-and-set
static void tas_lock(lock_t *l) {
    while (atomic_exchange_explicit(&l->flag, 1, memory_order_acquire) == 1)
        ;                                                     // spin
}
static void spin_unlock(lock_t *l) { atomic_store_explicit(&l->flag, 0, memory_order_release); }

// ---- test-and-test-and-set: 풀린 것처럼 "보일" 때만 원자 교환 시도
static void ttas_lock(lock_t *l) {
    for (;;) {
        while (atomic_load_explicit(&l->flag, memory_order_relaxed) == 1)
            ;                                                 // 캐시에서 읽기만 하며 대기
        if (atomic_exchange_explicit(&l->flag, 1, memory_order_acquire) == 0)
            return;
    }
}

// ---- compare-and-swap
static void cas_lock(lock_t *l) {
    int expected = 0;
    while (!atomic_compare_exchange_weak_explicit(&l->flag, &expected, 1,
                                                  memory_order_acquire, memory_order_relaxed))
        expected = 0;                                         // 실패하면 expected 가 덮어써지므로 리셋
}

// ---- load-linked / store-conditional (arm64 전용 인라인 어셈블리)
static void llsc_lock(lock_t *l) {
#if defined(__aarch64__)
    int tmp, fail;
    __asm__ volatile(
        "1: ldaxr  %w0, [%2]      \n"   // LoadLinked (acquire) : 값 읽고 '감시' 시작
        "   cbnz   %w0, 1b        \n"   // 1 이면 잡혀 있음 → 다시
        "   mov    %w0, #1        \n"
        "   stxr   %w1, %w0, [%2] \n"   // StoreConditional : 그 사이 누가 썼으면 실패(fail=1)
        "   cbnz   %w1, 1b        \n"   // 실패하면 처음부터
        : "=&r"(tmp), "=&r"(fail)
        : "r"(&l->flag)
        : "memory");
#else
    cas_lock(l);
#endif
}

// ---- ticket lock (fetch-and-add)
static void ticket_lock(lock_t *l) {
    int myturn = atomic_fetch_add_explicit(&l->ticket, 1, memory_order_relaxed);
    while (atomic_load_explicit(&l->turn, memory_order_acquire) != myturn)
        ;                                                     // 내 차례까지 spin
}
static void ticket_unlock(lock_t *l) {
    atomic_fetch_add_explicit(&l->turn, 1, memory_order_release);
}

// ---- TAS + yield
static void yield_lock(lock_t *l) {
    while (atomic_exchange_explicit(&l->flag, 1, memory_order_acquire) == 1)
        sched_yield();                                        // CPU 를 양보
}

// ---- futex mutex: 0 = free, 1 = locked(대기자 없음), 2 = locked(대기자 있을 수 있음)
static void fwait(atomic_int *addr, int val) {
    atomic_fetch_add_explicit(&futex_waits, 1, memory_order_relaxed);
    os_sync_wait_on_address((void *)addr, (uint64_t)val, sizeof(int), OS_SYNC_WAIT_ON_ADDRESS_NONE);
}
static void futex_slow(lock_t *l, int c) {
    if (c != 2) c = atomic_exchange_explicit(&l->flag, 2, memory_order_acquire);
    while (c != 0) {
        fwait(&l->flag, 2);                                   // *flag == 2 일 때만 잠든다
        c = atomic_exchange_explicit(&l->flag, 2, memory_order_acquire);
    }
}
static void futex_lock(lock_t *l) {
    int c = 0;
    if (atomic_compare_exchange_strong_explicit(&l->flag, &c, 1,
                                                memory_order_acquire, memory_order_relaxed))
        return;                                               // fast path: 경쟁 없음 → 시스템콜 0
    futex_slow(l, c);
}
static void futex_unlock(lock_t *l) {
    if (atomic_fetch_sub_explicit(&l->flag, 1, memory_order_release) != 1) {
        atomic_store_explicit(&l->flag, 0, memory_order_release);
        os_sync_wake_by_address_any((void *)&l->flag, sizeof(int), OS_SYNC_WAKE_BY_ADDRESS_NONE);
    }
}

// ---- two-phase: 1단계 스핀(최대 SPIN 번) → 2단계 futex 수면
#define SPIN 200
static void twophase_lock(lock_t *l) {
    for (int i = 0; i < SPIN; i++) {
        int c = 0;
        if (atomic_load_explicit(&l->flag, memory_order_relaxed) == 0 &&
            atomic_compare_exchange_weak_explicit(&l->flag, &c, 1,
                                                  memory_order_acquire, memory_order_relaxed))
            return;
    }
    int c = 0;
    if (atomic_compare_exchange_strong_explicit(&l->flag, &c, 1,
                                                memory_order_acquire, memory_order_relaxed))
        return;
    futex_slow(l, c);
}

// ---- 라이브러리 락
static void pth_lock(lock_t *l)      { pthread_mutex_lock(&l->mutex); }
static void pth_unlock(lock_t *l)    { pthread_mutex_unlock(&l->mutex); }
static void unfair_lock(lock_t *l)   { os_unfair_lock_lock(&l->unfair); }
static void unfair_unlock(lock_t *l) { os_unfair_lock_unlock(&l->unfair); }

typedef struct {
    const char *name;
    void (*lock)(lock_t *);
    void (*unlock)(lock_t *);
} lockops_t;

static const lockops_t LOCKS[] = {
    {"tas", tas_lock, spin_unlock},       {"ttas", ttas_lock, spin_unlock},
    {"cas", cas_lock, spin_unlock},       {"llsc", llsc_lock, spin_unlock},
    {"ticket", ticket_lock, ticket_unlock}, {"tas_yield", yield_lock, spin_unlock},
    {"futex", futex_lock, futex_unlock},  {"twophase", twophase_lock, futex_unlock},
    {"pthread", pth_lock, pth_unlock},    {"unfair", unfair_lock, unfair_unlock},
};
#define NLOCKS (int)(sizeof(LOCKS) / sizeof(LOCKS[0]))

// ------------------------------------------------------------ 벤치마크
static lock_t L;
static const lockops_t *ops;
static long counter;                   // 락으로 보호되는 공유 변수 (일부러 atomic 아님)
static long iters;
static atomic_int stop;
static atomic_int go;

typedef struct { _Alignas(128) long acquired; double end; } perthread_t;
static double now(void);
static long last_owner = -1, handoffs;  // 락 안에서만 갱신: 소유자가 바뀐 횟수
static perthread_t stats[64];

static void reset_lock(void) {
    memset(&L, 0, sizeof(L));
    pthread_mutex_init(&L.mutex, NULL);
    L.unfair = OS_UNFAIR_LOCK_INIT;
    counter = 0;
    last_owner = -1;
    handoffs = 0;
    atomic_store(&futex_waits, 0);
}

static void *throughput_worker(void *arg) {
    perthread_t *me = arg;
    while (!atomic_load(&go)) ;
    for (long i = 0; i < iters; i++) {
        if (atomic_load_explicit(&stop, memory_order_relaxed)) break;   // 시간 초과 시 중단
        ops->lock(&L);
        counter++;
        ops->unlock(&L);
        __atomic_store_n(&me->acquired, me->acquired + 1, __ATOMIC_RELAXED);  // main 이 읽어 감
    }
    me->end = now();                    // 각자 끝난 시각 → 전체 시간 = max(end) - 시작
    return NULL;
}

static void *fairness_worker(void *arg) {
    perthread_t *me = arg;
    while (!atomic_load(&go)) ;
    while (!atomic_load_explicit(&stop, memory_order_relaxed)) {
        ops->lock(&L);
        counter++;
        if (last_owner != (long)(me - stats)) { handoffs++; last_owner = (long)(me - stats); }
        me->acquired++;
        ops->unlock(&L);
    }
    return NULL;
}

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// 실험 1 한 칸: ns/op 를 돌려준다. DEADLINE 초를 넘기면 중단하고 *timedout = 1.
#define DEADLINE 3.0
static double run_throughput(int nthreads, int *timedout) {
    pthread_t t[64];
    reset_lock();
    memset(stats, 0, sizeof(stats));
    atomic_store(&go, 0);
    atomic_store(&stop, 0);
    for (int i = 0; i < nthreads; i++) pthread_create(&t[i], NULL, throughput_worker, &stats[i]);
    double t0 = now();
    atomic_store(&go, 1);
    *timedout = 0;
    for (;;) {                                  // main 은 감시자: 다 끝났나? 시간 초과인가?
        long done = 0;
        for (int i = 0; i < nthreads; i++) done += __atomic_load_n(&stats[i].acquired, __ATOMIC_RELAXED);
        if (done == iters * nthreads) break;
        if (now() - t0 > DEADLINE) { atomic_store(&stop, 1); *timedout = 1; break; }
        struct timespec ts = {0, 1000 * 1000};
        nanosleep(&ts, NULL);
    }
    for (int i = 0; i < nthreads; i++) pthread_join(t[i], NULL);
    double dt = 0;
    for (int i = 0; i < nthreads; i++) if (stats[i].end - t0 > dt) dt = stats[i].end - t0;
    long done = 0;
    for (int i = 0; i < nthreads; i++) done += stats[i].acquired;
    if (counter != done) {                      // 상호 배제 검증: 잃어버린 ++ 가 없어야 한다
        printf("\n!! %s: counter=%ld expected=%ld\n", ops->name, counter, done);
        exit(1);
    }
    return dt * 1e9 / (double)done;
}

int main(int argc, char *argv[]) {
    iters = (argc > 1) ? atol(argv[1]) : 200000;
    const int TH[] = {1, 2, 4, 8, 16};
    const int NTH = (int)(sizeof(TH) / sizeof(TH[0]));

    printf("== 실험 1: 처리량 (각 스레드 %ld 회 lock/++/unlock, ns/op = 총시간/총획득수)\n", iters);
    printf("%-10s", "lock");
    for (int j = 0; j < NTH; j++) printf("   T=%-2d ns/op", TH[j]);
    printf("   futex sleeps@T=8\n");
    printf("(* = %.0f s 안에 못 끝나서 중단, 그때까지 처리한 것으로 계산)\n", DEADLINE);
    ops = &LOCKS[0];
    { int to; run_throughput(1, &to); }          // 워밍업 (페이지 폴트·주파수 상승)
    for (int k = 0; k < NLOCKS; k++) {
        ops = &LOCKS[k];
        printf("%-10s", ops->name);
        long sleeps8 = -1;
        for (int j = 0; j < NTH; j++) {
            int to;
            double nsop = run_throughput(TH[j], &to);
            printf("   %11.1f%s", nsop, to ? "*" : " ");
            if (TH[j] == 8) sleeps8 = atomic_load(&futex_waits);
            fflush(stdout);
        }
        if (k == 6 || k == 7) printf("   %ld", sleeps8);
        printf("\n");
    }

    const int FT = 4;
    printf("\n== 실험 2: 공정성 (%d 스레드, 300 ms 경쟁, 스레드별 획득 횟수)\n", FT);
    printf("   handoff = 연속 두 획득의 소유 스레드가 달랐던 비율 (100%%면 매번 다른 스레드에게 넘어감)\n");
    for (int k = 0; k < NLOCKS; k++) {
        ops = &LOCKS[k];
        pthread_t t[8];
        reset_lock();
        memset(stats, 0, sizeof(stats));
        atomic_store(&go, 0);
        atomic_store(&stop, 0);
        for (int i = 0; i < FT; i++) pthread_create(&t[i], NULL, fairness_worker, &stats[i]);
        atomic_store(&go, 1);
        struct timespec ts = {0, 300 * 1000 * 1000};
        nanosleep(&ts, NULL);
        atomic_store(&stop, 1);
        for (int i = 0; i < FT; i++) pthread_join(t[i], NULL);
        long mn = stats[0].acquired, mx = stats[0].acquired, sum = 0;
        for (int i = 0; i < FT; i++) {
            long a = stats[i].acquired;
            if (a < mn) mn = a;
            if (a > mx) mx = a;
            sum += a;
        }
        printf("%-10s total=%9ld  min=%9ld  max=%9ld  max/min=%5.2f  handoff=%5.1f%%  [",
               ops->name, sum, mn, mx, mn ? (double)mx / (double)mn : 0.0,
               100.0 * (double)handoffs / (double)sum);
        for (int i = 0; i < FT; i++) printf("%s%ld", i ? " " : "", stats[i].acquired);
        printf("]\n");
    }
    return 0;
}
