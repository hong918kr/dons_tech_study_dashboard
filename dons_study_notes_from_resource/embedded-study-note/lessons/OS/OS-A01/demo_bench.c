/* ENV: linux */
/* OS-A01 demo_bench — futex 뮤텍스들 vs glibc pthread_mutex_t (Lima VM elinux)
 *
 *   ./bench syscall              syscall 기본 비용: getppid, FUTEX_WAKE(빈 큐) private/shared, FUTEX_WAIT 즉시 EAGAIN
 *   ./bench uncontended          스레드 1 개, lock/unlock 쌍당 ns + syscall 수
 *   ./bench contended [ms]       같은 락 하나를 T = 1,2,4,6,8,12 스레드가 고정 시간 동안 두드림
 *                                tight = 임계 구역 밖 일 없음 / work = 안 ~50, 밖 ~400 회 산술
 *   ./bench lost                 naiveq 를 진짜 경합에서 (watchdog 5 ms) → 강제 스케줄 없이도 lost wakeup 이 나나
 *                                (계속 두드리기 500 ms + 라운드 방식 5000 번)
 *   ./bench one <lock> <T> <N>   perf stat / strace 용: 락 하나, T 스레드, 스레드당 N 쌍 (tight)
 *
 * 락: naive(0/1, 항상 wake) · mutex2 · mutex3 · spin+futex (practice.c) · pthread(기본) · pthread_adaptive
 * build: gcc -std=c11 -O2 -pthread demo_bench.c -o bench   (practice.c 와 같은 디렉터리)
 */
#define NO_HOOKS            /* 훅 호출을 컴파일에서 뺀다 (측정에 섞이지 않게) */
#define OSA01_LIB_ONLY      /* practice.c 의 함수만 쓰고 하네스 main 은 뺀다 */
#pragma GCC diagnostic ignored "-Wunused-function"
#include "practice.c"

static pthread_mutex_t g_pm, g_pa;
static void pm_lock(lock_t *m) { (void)m; pthread_mutex_lock(&g_pm); }
static void pm_unlock(lock_t *m) { (void)m; pthread_mutex_unlock(&g_pm); }
static void pa_lock(lock_t *m) { (void)m; pthread_mutex_lock(&g_pa); }
static void pa_unlock(lock_t *m) { (void)m; pthread_mutex_unlock(&g_pa); }
static const lock_ops_t OPS_PM = { "pthread", pm_lock, pm_unlock };
static const lock_ops_t OPS_PA = { "pthread_adaptive", pa_lock, pa_unlock };
static const lock_ops_t *ALL[] = { &OPS_NAIVE, &OPS_MUTEX2, &OPS_MUTEX3, &OPS_SPIN, &OPS_PM, &OPS_PA };
enum { NLOCKS = sizeof ALL / sizeof ALL[0] };

static volatile unsigned g_sink;
static long g_cs; /* 락으로만 보호되는 카운터 (끝에서 총 op 와 맞는지 확인) */
static void work(int n) { unsigned x = 1; for (int i = 0; i < n; i++) x = x * 1664525u + 1013904223u; g_sink = x; }

typedef struct { const lock_ops_t *ops; lock_t *m; atomic_int *stop; int in_w, out_w; long n, iters; } cjob_t;
static void *cworker(void *p) {
    cjob_t *j = p;
    long n = 0;
    if (j->iters) {
        for (long i = 0; i < j->iters; i++) { j->ops->lock(j->m); g_cs++; j->ops->unlock(j->m); }
        n = j->iters;
    } else {
        while (!atomic_load_explicit(j->stop, memory_order_relaxed)) {
            j->ops->lock(j->m);
            g_cs++;
            if (j->in_w) work(j->in_w);
            j->ops->unlock(j->m);
            if (j->out_w) work(j->out_w);
            n++;
        }
    }
    j->n = n;
    return NULL;
}

/* 고정 시간 ms 동안 T 스레드 → 총 op, 스레드별 min/max, syscall 수 */
static void contended_one(const lock_ops_t *ops, int T, int ms, int in_w, int out_w) {
    lock_t m = LOCK_INIT;
    atomic_int stop = 0;
    cjob_t j[16];
    pthread_t t[16];
    g_cs = 0;
    fsnap_t s0 = snap();
    long t0 = now_ns();
    for (int i = 0; i < T; i++) {
        j[i] = (cjob_t){ ops, &m, &stop, in_w, out_w, 0, 0 };
        pthread_create(&t[i], NULL, cworker, &j[i]);
    }
    sleep_ms(ms);
    atomic_store(&stop, 1);
    for (int i = 0; i < T; i++) pthread_join(t[i], NULL);
    long dt = now_ns() - t0;
    fsnap_t d = diff(snap(), s0);
    long tot = 0, mn = j[0].n, mx = j[0].n;
    for (int i = 0; i < T; i++) { tot += j[i].n; if (j[i].n < mn) mn = j[i].n; if (j[i].n > mx) mx = j[i].n; }
    int ours = ops != &OPS_PM && ops != &OPS_PA;
    double mean = (double)tot / T;
    if (ours)
        printf("  %-17s T=%-2d %8.2f Mops/s  %7.1f ns/op  wait/op %.4f  wake/op %.4f  min/mean %.2f max/mean %.2f%s\n",
               ops->name, T, tot / (dt / 1e3), (double)dt / tot, (double)d.wait / tot, (double)d.wake / tot,
               mn / mean, mx / mean, g_cs == tot ? "" : "  COUNT MISMATCH");
    else
        printf("  %-17s T=%-2d %8.2f Mops/s  %7.1f ns/op  wait/op   -     wake/op   -     min/mean %.2f max/mean %.2f%s\n",
               ops->name, T, tot / (dt / 1e3), (double)dt / tot, mn / mean, mx / mean,
               g_cs == tot ? "" : "  COUNT MISMATCH");
}

/* 라운드 워커: lock → 짧은 일 → unlock → 모두 모일 때까지 회전 (센스 반전 없이 단조 증가 카운터) */
typedef struct { const lock_ops_t *ops; lock_t *m; int T, rounds; } rjob_t;
static atomic_int g_bar;
static void *rworker(void *p) {
    rjob_t *j = p;
    for (int r = 1; r <= j->rounds; r++) {
        j->ops->lock(j->m);
        work(50);
        j->ops->unlock(j->m);
        atomic_fetch_add(&g_bar, 1);
        while (atomic_load(&g_bar) < r * j->T) cpu_relax();
    }
    return NULL;
}

static double per_call_ns(long n, long dt) { return (double)dt / n; }

int main(int argc, char **argv) {
    pthread_mutexattr_t a;
    pthread_mutex_init(&g_pm, NULL);
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_ADAPTIVE_NP);
    pthread_mutex_init(&g_pa, &a);
    const char *mode = argc > 1 ? argv[1] : "contended";

    if (!strcmp(mode, "syscall")) {
        enum { N = 1000000 };
        atomic_int w = 0;
        for (int rep = 0; rep < 3; rep++) {
            long t0 = now_ns();
            for (int i = 0; i < N; i++) g_sink += (unsigned)getppid();
            long t1 = now_ns();
            for (int i = 0; i < N; i++) syscall(SYS_futex, (int *)&w, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
            long t2 = now_ns();
            for (int i = 0; i < N; i++) syscall(SYS_futex, (int *)&w, FUTEX_WAKE, 1, NULL, NULL, 0);
            long t3 = now_ns();
            for (int i = 0; i < N; i++) syscall(SYS_futex, (int *)&w, FUTEX_WAIT_PRIVATE, 1, NULL, NULL, 0);
            long t4 = now_ns();
            printf("  rep %d: getppid %.0f ns | FUTEX_WAKE_PRIVATE(빈 큐) %.0f ns | FUTEX_WAKE shared(빈 큐) %.0f ns"
                   " | FUTEX_WAIT_PRIVATE 즉시 EAGAIN %.0f ns\n", rep, per_call_ns(N, t1 - t0),
                   per_call_ns(N, t2 - t1), per_call_ns(N, t3 - t2), per_call_ns(N, t4 - t3));
        }
    } else if (!strcmp(mode, "uncontended")) {
        enum { N = 5000000 };
        for (int k = 0; k < NLOCKS; k++) {
            double best = 1e30;
            fsnap_t d = { 0, 0, 0, 0, 0, 0 };
            for (int rep = 0; rep < 3; rep++) {
                lock_t m = LOCK_INIT;
                fsnap_t s0 = snap();
                long t0 = now_ns();
                for (int i = 0; i < N; i++) { ALL[k]->lock(&m); g_cs++; ALL[k]->unlock(&m); }
                double ns = per_call_ns(N, now_ns() - t0);
                if (ns < best) best = ns;
                d = diff(snap(), s0);
            }
            if (ALL[k] == &OPS_PM || ALL[k] == &OPS_PA)
                printf("  %-17s %6.2f ns/쌍 (best of 3)  syscalls/쌍 (래퍼 밖 → perf stat 로)\n", ALL[k]->name, best);
            else
                printf("  %-17s %6.2f ns/쌍 (best of 3)  syscalls/쌍 %.3f\n", ALL[k]->name, best,
                       (double)(d.wait + d.wake) / N);
        }
    } else if (!strcmp(mode, "contended")) {
        int ms = argc > 2 ? atoi(argv[2]) : 100;
        static const int TS[] = { 1, 2, 4, 6, 8, 12 };
        const char *prof[2] = { "tight (CS 안·밖 일 없음)", "work (CS 안 ~50, 밖 ~400 회 산술)" };
        int inw[2] = { 0, 50 }, outw[2] = { 0, 400 };
        for (int p = 0; p < 2; p++) {
            printf("[%s]\n", prof[p]);
            for (int k = 0; k < NLOCKS; k++)
                for (unsigned ti = 0; ti < sizeof TS / sizeof TS[0]; ti++) contended_one(ALL[k], TS[ti], ms, inw[p], outw[p]);
        }
    } else if (!strcmp(mode, "lost")) {
        static const struct timespec wd = { 0, 5000000L }; /* 5 ms */
        g_watchdog = &wd;
        const lock_ops_t *L[2] = { &OPS_NAIVEQ, &OPS_NAIVE };
        for (int k = 0; k < 2; k++)
            for (int T = 2; T <= 8; T *= 2) {
                lock_t m = LOCK_INIT;
                atomic_int stop = 0;
                cjob_t j[16];
                pthread_t t[16];
                g_cs = 0;
                fsnap_t s0 = snap();
                for (int i = 0; i < T; i++) {
                    j[i] = (cjob_t){ L[k], &m, &stop, 50, 400, 0, 0 };
                    pthread_create(&t[i], NULL, cworker, &j[i]);
                }
                sleep_ms(500);
                atomic_store(&stop, 1);
                for (int i = 0; i < T; i++) pthread_join(t[i], NULL);
                fsnap_t d = diff(snap(), s0);
                long tot = 0;
                for (int i = 0; i < T; i++) tot += j[i].n;
                printf("  %-7s T=%d  500 ms: ops %8ld  futex_wait %7ld  timedout(=잃어버린 wakeup) %4ld  eagain %6ld%s\n",
                       L[k]->name, T, tot, d.wait, d.timedout, d.eagain, g_cs == tot ? "" : "  COUNT MISMATCH");
            }
        /* 라운드 방식: 매 라운드 각 스레드가 lock/unlock 딱 한 번 → 배리어. "마지막 unlock" 이 매 라운드 생긴다 */
        for (int k = 0; k < 2; k++)
            for (int T = 2; T <= 4; T *= 2) {
                lock_t m = LOCK_INIT;
                rjob_t j[16];
                pthread_t t[16];
                atomic_store(&g_bar, 0);
                fsnap_t s0 = snap();
                long t0 = now_ns();
                for (int i = 0; i < T; i++) { j[i] = (rjob_t){ L[k], &m, T, 5000 }; pthread_create(&t[i], NULL, rworker, &j[i]); }
                for (int i = 0; i < T; i++) pthread_join(t[i], NULL);
                fsnap_t d = diff(snap(), s0);
                printf("  %-7s T=%d  5000 라운드 (%.0f ms): futex_wait %6ld  timedout(=잃어버린 wakeup) %4ld  eagain %5ld\n",
                       L[k]->name, T, (now_ns() - t0) / 1e6, d.wait, d.timedout, d.eagain);
            }
    } else if (!strcmp(mode, "one") && argc == 5) {
        const lock_ops_t *ops = NULL;
        for (int k = 0; k < NLOCKS; k++) if (!strcmp(ALL[k]->name, argv[2])) ops = ALL[k];
        if (!ops) { fprintf(stderr, "unknown lock %s\n", argv[2]); return 2; }
        int T = atoi(argv[3]);
        long N = atol(argv[4]);
        lock_t m = LOCK_INIT;
        cjob_t j[16];
        pthread_t t[16];
        g_cs = 0;
        fsnap_t s0 = snap();
        long t0 = now_ns();
        for (int i = 0; i < T; i++) { j[i] = (cjob_t){ ops, &m, NULL, 0, 0, 0, N }; pthread_create(&t[i], NULL, cworker, &j[i]); }
        for (int i = 0; i < T; i++) pthread_join(t[i], NULL);
        long dt = now_ns() - t0;
        fsnap_t d = diff(snap(), s0);
        printf("%s T=%d N=%ld: %.1f ns/op, wrapper counted wait %ld wake %ld (total %ld) %s\n", ops->name, T, N,
               (double)dt / (T * N), d.wait, d.wake, d.wait + d.wake, g_cs == T * N ? "count ok" : "COUNT MISMATCH");
    } else {
        fprintf(stderr, "usage: %s syscall|uncontended|contended [ms]|lost|one <lock> <T> <N>\n", argv[0]);
        return 2;
    }
    return 0;
}
