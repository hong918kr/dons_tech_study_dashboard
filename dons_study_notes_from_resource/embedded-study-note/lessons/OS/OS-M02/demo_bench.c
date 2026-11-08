/* OS-M02 demo_bench.c — 파이프 핑퐁으로 컨텍스트 스위치(정확히는 "블록 → 깨움") 비용 측정
 *
 * 측정 항목 (모드):
 *   getppid   : 시스템 콜 하나 (모드 전환 왕복) — 배치 시간 / 호출 수
 *   selfpipe  : 같은 스레드에서 pipe 에 1바이트 write → 바로 read (절대 블록하지 않음) = 스위치 없는 기준선
 *   thread    : 두 스레드가 파이프 두 개로 1바이트 핑퐁 (같은 주소 공간)
 *   proc      : fork 한 두 프로세스가 파이프 두 개로 1바이트 핑퐁 (주소 공간이 다름)
 *   cond      : 두 스레드가 mutex + 조건 변수 두 개로 핑퐁 (pthread_cond = 커널 대기/깨움)
 *   spin      : 두 스레드가 원자 변수 하나를 돌며 핑퐁 (커널 없음 → 코어 간 캐시 라인 이동 비용만)
 *
 * 왕복(RT) 하나마다 mach_absolute_time 으로 잰다 (Apple Silicon: 24 MHz, 1 tick = 41.67 ns).
 * 처음 WARM 왕복은 버린다(워밍업). 각 RT 에서 "핑이 깨어난 코어" 와 "퐁이 깨어난 코어" 를
 * pthread_cpu_number_np 로 기록해 같은 코어 깨움 / 다른 코어 깨움으로 나눠 본다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_bench.c -o bench
 * run  : ./bench [mode] [runs] [rounds] [qos]     qos = def | ui | bg
 *        ./bench all 5 200000                     (기본: 모든 모드, 5회, 20만 왕복)
 */
#define _DARWIN_C_SOURCE
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/qos.h>
#include <sys/wait.h>
#include <unistd.h>

#define WARM 2000u

static mach_timebase_info_data_t g_tb;
static qos_class_t g_qos = QOS_CLASS_UNSPECIFIED;

static double t2ns(uint64_t t) { return (double)t * g_tb.numer / g_tb.denom; }
static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}
static void set_qos(void) {
    if (g_qos != QOS_CLASS_UNSPECIFIED) pthread_set_qos_class_self_np(g_qos, 0);
}
static unsigned cpu_now(void) {
    size_t c = 0;
    pthread_cpu_number_np(&c);
    return (unsigned)c;
}

/* ------------------------------------------------------------------ */
typedef struct {
    int a2b[2], b2a[2];          /* 핑 → 퐁, 퐁 → 핑 파이프 */
    unsigned rounds;
    uint64_t *rt;                /* 왕복 시간 (tick) — 핑 쪽만 씀 */
    uint8_t *cpu_ping, *cpu_pong;/* 깨어난 직후의 코어 번호 (proc 모드는 공유 mmap) */
} pp_t;

static void pong_loop(pp_t *p) {
    set_qos();
    uint8_t b;
    for (unsigned i = 0; i < p->rounds; i++) {
        if (read(p->a2b[0], &b, 1) != 1) _exit(2);
        p->cpu_pong[i] = (uint8_t)cpu_now();
        b++;
        if (write(p->b2a[1], &b, 1) != 1) _exit(3);
    }
}
static void ping_loop(pp_t *p) {
    set_qos();
    uint8_t b = 0, r;
    for (unsigned i = 0; i < p->rounds; i++) {
        uint64_t t0 = mach_absolute_time();
        if (write(p->a2b[1], &b, 1) != 1) _exit(4);
        if (read(p->b2a[0], &r, 1) != 1) _exit(5);
        uint64_t t1 = mach_absolute_time();
        p->cpu_ping[i] = (uint8_t)cpu_now();
        p->rt[i] = t1 - t0;
        if (r != (uint8_t)(b + 1)) { fprintf(stderr, "bad echo\n"); exit(6); }
        b = r;
    }
}
static void *pong_thread(void *arg) { pong_loop(arg); return NULL; }

/* cond 모드: turn 0 = 핑 차례, 1 = 퐁 차례 */
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv_ping = PTHREAD_COND_INITIALIZER, g_cv_pong = PTHREAD_COND_INITIALIZER;
static int g_turn;
static void *cond_pong(void *arg) {
    pp_t *p = arg;
    set_qos();
    for (unsigned i = 0; i < p->rounds; i++) {
        pthread_mutex_lock(&g_mu);
        while (g_turn != 1) pthread_cond_wait(&g_cv_pong, &g_mu);
        p->cpu_pong[i] = (uint8_t)cpu_now();
        g_turn = 0;
        pthread_cond_signal(&g_cv_ping);
        pthread_mutex_unlock(&g_mu);
    }
    return NULL;
}
static void cond_ping(pp_t *p) {
    set_qos();
    for (unsigned i = 0; i < p->rounds; i++) {
        uint64_t t0 = mach_absolute_time();
        pthread_mutex_lock(&g_mu);
        g_turn = 1;
        pthread_cond_signal(&g_cv_pong);
        while (g_turn != 0) pthread_cond_wait(&g_cv_ping, &g_mu);
        pthread_mutex_unlock(&g_mu);
        uint64_t t1 = mach_absolute_time();
        p->cpu_ping[i] = (uint8_t)cpu_now();
        p->rt[i] = t1 - t0;
    }
}

/* spin 모드: 원자 변수 하나. 짝수 = 핑 차례, 홀수 = 퐁 차례 */
static _Alignas(128) atomic_uint g_ball;
static void *spin_pong(void *arg) {
    pp_t *p = arg;
    set_qos();
    for (unsigned i = 0; i < p->rounds; i++) {
        while (atomic_load_explicit(&g_ball, memory_order_acquire) != 2u * i + 1u) { }
        p->cpu_pong[i] = (uint8_t)cpu_now();
        atomic_store_explicit(&g_ball, 2u * i + 2u, memory_order_release);
    }
    return NULL;
}
static void spin_ping(pp_t *p) {
    set_qos();
    for (unsigned i = 0; i < p->rounds; i++) {
        uint64_t t0 = mach_absolute_time();
        atomic_store_explicit(&g_ball, 2u * i + 1u, memory_order_release);
        while (atomic_load_explicit(&g_ball, memory_order_acquire) != 2u * i + 2u) { }
        uint64_t t1 = mach_absolute_time();
        p->cpu_ping[i] = (uint8_t)cpu_now();
        p->rt[i] = t1 - t0;
    }
}

/* ------------------------------------------------------------------ */
static void report(const char *mode, int run, pp_t *p, uint64_t total) {
    unsigned n = p->rounds - WARM;
    uint64_t *s = malloc(n * sizeof *s), *same = malloc(n * sizeof *s), *cross = malloc(n * sizeof *s);
    unsigned ns = 0, nc = 0;
    for (unsigned i = 0; i < n; i++) {
        unsigned k = i + WARM;
        s[i] = p->rt[k];
        if (p->cpu_ping[k] == p->cpu_pong[k]) same[ns++] = p->rt[k]; else cross[nc++] = p->rt[k];
    }
    qsort(s, n, sizeof *s, cmp_u64);
    qsort(same, ns, sizeof *s, cmp_u64);
    qsort(cross, nc, sizeof *s, cmp_u64);
    size_t p99 = (size_t)((99u * (uint64_t)n + 99u) / 100u) - 1u;   /* nearest-rank */
    unsigned ecore = 0;
    for (unsigned i = WARM; i < p->rounds; i++) ecore += p->cpu_ping[i] < 6u;
    printf("%-9s run%d  mean %7.0f  min %6.0f  med %6.0f  p99 %7.0f  max %9.0f ns | first %6.0f | "
           "same-core %5.1f%% (med %6.0f) cross %5.1f%% (med %6.0f) | ping on E %4.1f%%\n",
           mode, run, t2ns(total) / p->rounds, t2ns(s[0]), t2ns(s[n / 2]), t2ns(s[p99]), t2ns(s[n - 1]),
           t2ns(p->rt[0]), 100.0 * ns / n, ns ? t2ns(same[ns / 2]) : 0.0, 100.0 * nc / n,
           nc ? t2ns(cross[nc / 2]) : 0.0, 100.0 * ecore / n);
    free(s); free(same); free(cross);
}

static void run_pp(const char *mode, int run, unsigned rounds) {
    pp_t p = { .rounds = rounds };
    p.rt = calloc(rounds, sizeof *p.rt);
    p.cpu_ping = calloc(rounds, 1);
    /* proc 모드에서 자식이 쓴 코어 번호를 부모가 보려면 공유 매핑이어야 한다 */
    p.cpu_pong = mmap(NULL, rounds, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
    if (pipe(p.a2b) || pipe(p.b2a)) { perror("pipe"); exit(1); }
    atomic_store(&g_ball, 0u);
    g_turn = 0;
    uint64_t t0 = mach_absolute_time();
    if (!strcmp(mode, "proc")) {
        pid_t pid = fork();
        if (pid == 0) { close(p.a2b[1]); close(p.b2a[0]); pong_loop(&p); _exit(0); }
        close(p.a2b[0]); close(p.b2a[1]);
        ping_loop(&p);
        int st;
        waitpid(pid, &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st)) { fprintf(stderr, "child failed\n"); exit(1); }
    } else {
        pthread_t t;
        void *(*fn)(void *) = !strcmp(mode, "thread") ? pong_thread : !strcmp(mode, "cond") ? cond_pong : spin_pong;
        pthread_create(&t, NULL, fn, &p);
        if (!strcmp(mode, "thread")) ping_loop(&p);
        else if (!strcmp(mode, "cond")) cond_ping(&p);
        else spin_ping(&p);
        pthread_join(t, NULL);
    }
    uint64_t t1 = mach_absolute_time();
    report(mode, run, &p, t1 - t0);
    for (int i = 0; i < 2; i++) { close(p.a2b[i]); close(p.b2a[i]); }
    free(p.rt); free(p.cpu_ping); munmap(p.cpu_pong, rounds);
}

/* 기준선: 블록하지 않는 시스템 콜. 호출 1회는 타이머 해상도(42 ns)보다 짧으므로 배치로 잰다. */
static void run_base(const char *mode, int run, unsigned rounds) {
    set_qos();
    enum { BATCH = 100 };
    unsigned nb = rounds / BATCH;
    uint64_t *s = malloc(nb * sizeof *s), total = 0;
    int fd[2];
    if (pipe(fd)) { perror("pipe"); exit(1); }
    uint8_t b = 7;
    volatile pid_t sink = 0;
    for (unsigned k = 0; k < nb + 20u; k++) {            /* 앞 20 배치는 워밍업 */
        uint64_t t0 = mach_absolute_time();
        if (!strcmp(mode, "getppid")) {
            for (int i = 0; i < BATCH; i++) sink += getppid();
        } else {
            for (int i = 0; i < BATCH; i++) {
                if (write(fd[1], &b, 1) != 1 || read(fd[0], &b, 1) != 1) exit(7);
            }
        }
        uint64_t d = mach_absolute_time() - t0;
        if (k >= 20u) { s[k - 20u] = d; total += d; }
    }
    (void)sink;
    qsort(s, nb, sizeof *s, cmp_u64);
    size_t p99 = (size_t)((99u * (uint64_t)nb + 99u) / 100u) - 1u;
    printf("%-9s run%d  mean %7.1f  min %6.1f  med %6.1f  p99 %7.1f ns per %s (batch of %d)\n", mode, run,
           t2ns(total) / ((double)nb * BATCH), t2ns(s[0]) / BATCH, t2ns(s[nb / 2]) / BATCH,
           t2ns(s[p99]) / BATCH, !strcmp(mode, "getppid") ? "call" : "write+read", BATCH);
    close(fd[0]); close(fd[1]); free(s);
}

int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "all";
    int runs = argc > 2 ? atoi(argv[2]) : 5;
    unsigned rounds = argc > 3 ? (unsigned)atoi(argv[3]) : 200000u;
    if (argc > 4) g_qos = !strcmp(argv[4], "ui") ? QOS_CLASS_USER_INTERACTIVE
                        : !strcmp(argv[4], "bg") ? QOS_CLASS_BACKGROUND : QOS_CLASS_UNSPECIFIED;
    if (rounds <= WARM) rounds = WARM + 1000u;
    mach_timebase_info(&g_tb);
    printf("timebase %u/%u (1 tick = %.2f ns), rounds/run %u (first %u discarded), qos %s\n", g_tb.numer,
           g_tb.denom, (double)g_tb.numer / g_tb.denom, rounds, WARM, argc > 4 ? argv[4] : "def");
    const char *all[] = { "getppid", "selfpipe", "thread", "proc", "cond", "spin" };
    for (int m = 0; m < 6; m++) {
        if (strcmp(mode, "all") && strcmp(mode, all[m])) continue;
        for (int r = 1; r <= runs; r++) {
            if (m < 2) run_base(all[m], r, rounds * 10u);
            else run_pp(all[m], r, rounds);
        }
    }
    return 0;
}
