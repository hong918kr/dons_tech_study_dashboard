/* ENV: linux */
/* LPERF-B10 스레드 시간 분해: on-CPU·런큐 대기·sleep
 *
 * 한 스레드의 wall time 을 세 칸으로 나눈다:
 *     wall = run (CPU 위에서 실행) + wait (runnable, 런큐에서 CPU 대기) + off (sleep/block, 깨워 주길 기다림)
 * run, wait 는 /proc/<pid>/task/<tid>/schedstat 이 직접 주고, off 는 나머지(residual)로 구한다.
 *
 *   Part A  /proc 파서 3종: schedstat (3 숫자) / status 의 (non)voluntary_ctxt_switches / stat 의 comm·state·utime·stime
 *   Part B  두 샘플 → 시간 분해(account) + 상태 히스토그램 + 판정(classify)
 *   Part C  카메라 게이트웨이(4코어) 고정 샘플로 병목 스레드 찾기 + wall/CPU 용량 검증(verify_wall)
 *           + 실제 스레드 하나를 /proc 로 읽는 라이브 테스트(read_task_sample)
 *
 * 체크는 고정 문자열 또는 "매 실행 항상 성립하는 것" 만 검사한다 (타이밍 수치는 CHECK 하지 않는다).
 * 실제 오버서브스크립션 측정은 demo_workload.c + demo_ttime.c (이 파일을 #include 해서 같은 함수를 쓴다).
 *
 * build (Lima VM elinux): gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so
 */
#define _GNU_SOURCE /* gettid(), pthread_setname_np: 리눅스 전용 API (맥의 _DARWIN_C_SOURCE 자리) */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ======================= 공통 자료형 ======================= */
typedef struct {
    uint64_t run_ns;   /* 1번째: CPU 위에서 실행한 시간 (se.sum_exec_runtime) */
    uint64_t wait_ns;  /* 2번째: runnable 인데 CPU 를 못 받고 런큐에서 기다린 시간 (sched_info.run_delay) */
    uint64_t slices;   /* 3번째: CPU 에 올라간 횟수 = 타임슬라이스 수 (sched_info.pcount) */
} schedstat_t;

typedef struct {
    uint64_t vcsw;     /* voluntary_ctxt_switches   : 스스로 잠들어서(블록) CPU 를 내놓은 횟수 */
    uint64_t nvcsw;    /* nonvoluntary_ctxt_switches: 아직 돌 수 있는데 빼앗긴(선점) 횟수 */
} ctxsw_t;

typedef struct {
    int pid;           /* stat 1번째: tid (스레드면 tid) */
    char comm[16];     /* stat 2번째: (comm) — 공백·괄호가 들어갈 수 있다. 커널 TASK_COMM_LEN = 16 */
    char state;        /* stat 3번째: R S D T t Z X I ... */
    uint64_t utime;    /* stat 14번째: user CPU, clock tick (USER_HZ) 단위 */
    uint64_t stime;    /* stat 15번째: system CPU, clock tick 단위 */
} taskstat_t;

typedef struct {
    uint64_t t_ns;     /* 이 샘플을 읽은 CLOCK_MONOTONIC 시각 */
    schedstat_t ss;
    ctxsw_t cs;
    taskstat_t st;
} sample_t;

typedef struct {
    uint64_t wall_ns, run_ns, wait_ns, off_ns;
    uint64_t skew_ns;  /* run + wait 가 wall 을 넘친 양 (샘플링 시각 차이). 넘치면 off = 0 */
    uint64_t slices, vcsw, nvcsw;
} breakdown_t;

typedef struct { unsigned n, R, S, D, other; } state_hist_t;

typedef enum { TT_ON_CPU, TT_RUNQ_WAIT, TT_SLEEP_S, TT_BLOCK_D } tt_class_t;
static const char *const tt_class_name[] = { "ON-CPU", "RUNQ-WAIT", "SLEEP(S)", "BLOCK(D)" };

/* 주어진 도우미: 공백을 건너뛰고 10진 부호 없는 정수 하나. 성공하면 *pp 를 숫자 뒤로 옮기고 0. */
static int next_u64(const char **pp, uint64_t *out) {
    const char *p = *pp;
    while (*p == ' ' || *p == '\t') p++;
    if (!isdigit((unsigned char)*p)) return -1;          /* strtoull 은 "-1" 도 받아 버리므로 먼저 막는다 */
    errno = 0;
    char *end;
    unsigned long long v = strtoull(p, &end, 10);
    if (errno == ERANGE) return -1;
    *out = (uint64_t)v;
    *pp = end;
    return 0;
}

/* ======================= Part A — /proc 파서 3종 ======================= */

/* "run wait slices\n" → out. 숫자가 3개 미만이거나 숫자 아닌 것이 끼면 -1.
 * 뒤에 숫자가 더 붙어 있으면(미래 커널이 필드를 늘린 경우) 앞의 3개만 쓰고 0. */
static int parse_schedstat(const char *s, schedstat_t *out) {
    (void)s; (void)out; (void)next_u64;
    return -1; /* TODO */
}

/* buf 안에서 "줄 맨 앞" 이 key 로 시작하는 줄을 찾아 key 바로 뒤를 가리키는 포인터, 없으면 NULL. */
static const char *find_line_key(const char *buf, const char *key) {
    (void)buf; (void)key;
    return NULL; /* TODO: 줄 맨 앞에서만 key 를 비교 */
}

/* /proc/<pid>/task/<tid>/status 전체 텍스트에서 두 카운터. 둘 중 하나라도 없으면 -1. */
static int parse_status_ctxsw(const char *buf, ctxsw_t *out) {
    (void)buf; (void)out;
    return -1; /* TODO: find_line_key 두 번 + next_u64 */
}

/* 주어진 도우미: 공백 구분 필드 하나를 건너뛴다 (음수 "-1" 같은 필드도 그냥 넘긴다). */
static const char *skip_field(const char *p) {
    while (*p == ' ') p++;
    if (*p == '\0') return NULL;
    while (*p != ' ' && *p != '\0') p++;
    return p;
}

/* /proc/<pid>/task/<tid>/stat 한 줄: "tid (comm) state ppid ... utime stime ..."
 * comm 은 '(' 다음부터 "마지막" ')' 앞까지 (comm 자체에 ") " 가 들어갈 수 있다). 길면 15자로 자른다. */
static int parse_stat(const char *buf, taskstat_t *out) {
    (void)buf; (void)out; (void)skip_field;
    return -1; /* TODO */
}

/* ======================= Part B — 시간 분해와 판정 ======================= */

/* 같은 스레드의 두 샘플 a(앞) → b(뒤). 시간이 거꾸로거나 카운터가 줄었으면(다른 스레드, tid 재사용) -1. */
static int account(const sample_t *a, const sample_t *b, breakdown_t *out) {
    (void)a; (void)b; (void)out;
    return -1; /* TODO */
}

/* 주기 샘플링한 stat 의 state 문자 하나를 히스토그램에 더한다. */
static void state_hist_add(state_hist_t *h, char st) {
    (void)h; (void)st; /* TODO */
}

/* 가장 큰 칸이 판정. 동률이면 run > wait > off 순으로 우선.
 * off 가 가장 크면 히스토그램에서 D 표본이 S 표본보다 많을 때 BLOCK(D), 아니면 SLEEP(S). */
static tt_class_t classify(const breakdown_t *b, const state_hist_t *h) {
    (void)b; (void)h;
    return TT_ON_CPU; /* TODO */
}

/* ======================= Part C — 도구로 묶기 ======================= */

/* 세 파일의 텍스트 + 읽은 시각 → sample. 하나라도 파싱 실패면 -1. */
static int sample_from_text(const char *schedstat, const char *status, const char *stat,
                            uint64_t t_ns, sample_t *out) {
    (void)schedstat; (void)status; (void)stat; (void)t_ns;
    memset(out, 0, sizeof *out);
    return -1; /* TODO */
}

/* 주어진 도우미: 단조 시계 ns. */
static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

/* 주어진 도우미: 파일 전체를 buf 에 (NUL 종료). 읽은 바이트 수, 실패하면 -1. */
static long read_file(const char *path, char *buf, size_t sz) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    size_t n = 0;
    while (n + 1 < sz) {
        ssize_t r = read(fd, buf + n, sz - 1 - n);
        if (r < 0) { if (errno == EINTR) continue; close(fd); return -1; }
        if (r == 0) break;
        n += (size_t)r;
    }
    close(fd);
    buf[n] = '\0';
    return (long)n;
}

/* /proc/<pid>/task/<tid>/{schedstat,status,stat} 를 읽어 sample 하나. 시각은 schedstat 을 읽기 직전. */
static int read_task_sample(int pid, int tid, sample_t *out) {
    (void)pid; (void)tid; (void)now_ns; (void)read_file;
    memset(out, 0, sizeof *out);
    return -1; /* TODO: 세 파일 읽기 → sample_from_text */
}

typedef struct {
    double util;         /* Σrun / (ncpu × wall)  : CPU 용량 대비 사용률 (0..1) */
    double avg_runnable; /* Σ(run+wait) / wall    : 평균 "돌고 싶은" 스레드 수 (스레드판 load) */
    int oversub;         /* avg_runnable > ncpu   : CPU 보다 돌고 싶은 스레드가 많았다 */
    int bad_threads;     /* skew_ns > tol × wall 인 스레드 수 */
} sys_check_t;

/* 회계 검증. wall 은 n 개 중 가장 긴 wall. 위반 개수(bad_threads + Σrun 용량 초과면 1)를 반환. */
static int verify_wall(const breakdown_t *b, int n, int ncpu, double tol, sys_check_t *out) {
    (void)b; (void)n; (void)ncpu; (void)tol;
    memset(out, 0, sizeof *out);
    return -1; /* TODO */
}

#ifndef TTIME_LIB_ONLY /* demo_ttime.c 는 위 함수만 가져가고 아래 하네스는 빼고 컴파일한다 */
/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)

/* ---- Part C 고정 장면: 4코어 카메라 게이트웨이, 1초 창 ----
 * cam-capture 가 33 ms 프레임 마감을 놓친다는 버그 리포트. 인코더 4개가 CPU 를 다 먹고 있다. */
typedef struct {
    const char *comm;
    uint64_t run_ms, wait_ms, slices, vcsw, nvcsw;
    const char *states;    /* 주기 샘플 20개의 state 문자 */
} fx_t;
static const fx_t g_fx[] = {
    { "cam-capture", 120, 610, 300,  30, 270, "RRRRRRRRRRRRRRSSSSSR" },
    { "enc-0",       900, 100, 410,   5, 405, "RRRRRRRRRRRRRRRRRRRR" },
    { "enc-1",       900, 100, 395,   3, 392, "RRRRRRRRRRRRRRRRRRRR" },
    { "enc-2",       900, 100, 402,   4, 398, "RRRRRRRRRRRRRRRRRRRR" },
    { "enc-3",       900, 100, 388,   6, 382, "RRRRRRRRRRRRRRRRRRRR" },
    { "logger",       15,   5,  64,  60,   4, "DDDDDDDDDDDDDSDDDDDD" },
    { "mqtt",          5,   1,  21,  20,   1, "SSSSSSSSSSSSSSSSSSSS" },
};
#define NFX ((int)(sizeof g_fx / sizeof g_fx[0]))

/* 고정 숫자 → /proc 형식 텍스트 3개 → sample_from_text (파서까지 같이 거친다) */
static int fx_sample(int i, int at_end, sample_t *out) {
    char ss[96], st[256], stat[512];
    uint64_t base_run = 5000000000u, base_wait = 700000000u, base_sl = 1000, base_v = 100, base_nv = 50;
    const fx_t *f = &g_fx[i];
    uint64_t k = at_end ? 1u : 0u;
    snprintf(ss, sizeof ss, "%" PRIu64 " %" PRIu64 " %" PRIu64 "\n",
             base_run + k * f->run_ms * 1000000u, base_wait + k * f->wait_ms * 1000000u, base_sl + k * f->slices);
    snprintf(st, sizeof st, "Name:\t%s\nState:\tS (sleeping)\nvoluntary_ctxt_switches:\t%" PRIu64
             "\nnonvoluntary_ctxt_switches:\t%" PRIu64 "\n", f->comm, base_v + k * f->vcsw, base_nv + k * f->nvcsw);
    snprintf(stat, sizeof stat, "%d (%s) %c 900 900 900 0 -1 4194368 120 0 0 0 %" PRIu64 " %" PRIu64
             " 0 0 20 0 9 0 1613\n", 901 + i, f->comm, f->states[19], 500 + k * f->run_ms / 10, (uint64_t)3);
    return sample_from_text(ss, st, stat, 10000000000u + k * 1000000000u, out);
}

/* ---- Part C 라이브: 일하고(30 ms CPU) → 자고(30 ms) → 조건변수에서 주차하는 스레드 ---- */
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static int g_tid, g_phase;   /* 0 시작 전, 1 tid 게시, 2 go 받음, 3 주차, 4 끝내라 */

static uint64_t thread_cpu_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}
/* phase 가 want 이상이 될 때까지 최대 ms 밀리초 기다린다 (시작 코드가 틀려도 멈추지 않게). */
static int wait_phase(int want, int ms) {
    struct timespec dl;
    clock_gettime(CLOCK_REALTIME, &dl);
    dl.tv_sec += ms / 1000;
    dl.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (dl.tv_nsec >= 1000000000L) { dl.tv_sec++; dl.tv_nsec -= 1000000000L; }
    pthread_mutex_lock(&g_mu);
    int rc = 0;
    while (g_phase < want && rc == 0) rc = pthread_cond_timedwait(&g_cv, &g_mu, &dl);
    int ok = g_phase >= want;
    pthread_mutex_unlock(&g_mu);
    return ok;
}
static void set_phase(int p) {
    pthread_mutex_lock(&g_mu);
    g_phase = p;
    pthread_cond_broadcast(&g_cv);
    pthread_mutex_unlock(&g_mu);
}
static void *worker(void *arg) {
    (void)arg;
    pthread_mutex_lock(&g_mu);
    g_tid = (int)gettid();
    pthread_mutex_unlock(&g_mu);
    set_phase(1);
    wait_phase(2, 5000);
    uint64_t c0 = thread_cpu_ns();
    volatile uint64_t x = 0;
    while (thread_cpu_ns() - c0 < 30000000u) x = x + 1;      /* CPU 30 ms (on-CPU) */
    struct timespec d = { 0, 30000000L };
    nanosleep(&d, NULL);                                        /* 30 ms 잠 (S, voluntary) */
    set_phase(3);
    wait_phase(4, 5000);                                        /* 조건변수에서 주차 (S) */
    return NULL;
}

int main(void) {
    /* ---------- Part A ---------- */
    schedstat_t ss = { 7, 7, 7 };
    CHECK(parse_schedstat("936709328 97281085 49\n", &ss) == 0 &&
          ss.run_ns == 936709328u && ss.wait_ns == 97281085u && ss.slices == 49u);
    CHECK(parse_schedstat("18446744073709551615 0 1", &ss) == 0 && ss.run_ns == UINT64_MAX && ss.slices == 1u);
    CHECK(parse_schedstat("1 2 3 4\n", &ss) == 0 && ss.slices == 3u);         /* 필드가 늘어도 앞 3개 */
    CHECK(parse_schedstat("12 34\n", &ss) == -1);                             /* 2개뿐 */
    CHECK(parse_schedstat("12 -5 3\n", &ss) == -1);                           /* 음수 없음 */
    CHECK(parse_schedstat("12x 5 3\n", &ss) == -1);                           /* 숫자 뒤 쓰레기 */
    CHECK(parse_schedstat("", &ss) == -1);

    const char *status =
        "Name:\tworker\nState:\tS (sleeping)\nTgid:\t2028\nPid:\t2031\n"
        "Cpus_allowed_list:\t0-5\n"
        "nonvoluntary_ctxt_switches:\t17\n"          /* 실제 파일은 voluntary 가 먼저지만 순서에 기대지 않는다 */
        "voluntary_ctxt_switches:\t4242\n";
    ctxsw_t cs = { 0, 0 };
    CHECK(parse_status_ctxsw(status, &cs) == 0 && cs.vcsw == 4242u && cs.nvcsw == 17u);
    CHECK(parse_status_ctxsw("nonvoluntary_ctxt_switches:\t9\n", &cs) == -1);    /* voluntary 줄이 없다 */
    CHECK(parse_status_ctxsw("voluntary_ctxt_switches:\t1\nnonvoluntary_ctxt_switches:\t2", &cs) == 0 &&
          cs.vcsw == 1u && cs.nvcsw == 2u);                                       /* 마지막 줄 \n 없음 */
    CHECK(find_line_key("Name:\tx\nPid:\t5\n", "id:") == NULL);                  /* 줄 중간 일치는 무시 */

    taskstat_t ts;
    memset(&ts, 0, sizeof ts);
    CHECK(parse_stat("2028 (cat) R 1989 1989 1989 0 -1 4194304 467 0 0 0 7 3 0 0 20 0 1 0 1613\n", &ts) == 0 &&
          ts.pid == 2028 && strcmp(ts.comm, "cat") == 0 && ts.state == 'R' && ts.utime == 7u && ts.stime == 3u);
    CHECK(parse_stat("77 (my) prog S x) D 1 1 1 0 -1 0 0 0 0 0 250 40 0 0\n", &ts) == 0 &&
          strcmp(ts.comm, "my) prog S x") == 0 && ts.state == 'D' && ts.utime == 250u && ts.stime == 40u);
    CHECK(parse_stat("5 (a-very-long-thread-name) S 1 1 1 0 -1 0 0 0 0 0 1 2\n", &ts) == 0 &&
          strcmp(ts.comm, "a-very-long-thr") == 0 && ts.state == 'S');          /* 15자 + NUL */
    CHECK(parse_stat("5 (x) S 1 1 1\n", &ts) == -1);                           /* utime 전에 끝남 */
    CHECK(parse_stat("garbage", &ts) == -1);

    /* ---------- Part B ---------- */
    sample_t a, b;
    memset(&a, 0, sizeof a);
    a.t_ns = 1000000000u; a.ss = (schedstat_t){ 100, 50, 10 }; a.cs = (ctxsw_t){ 5, 1 };
    b = a;
    b.t_ns = 2000000000u; b.ss = (schedstat_t){ 100 + 300000000u, 50 + 200000000u, 40 }; b.cs = (ctxsw_t){ 25, 11 };
    breakdown_t bd;
    memset(&bd, 0xA5, sizeof bd);
    CHECK(account(&a, &b, &bd) == 0 && bd.wall_ns == 1000000000u && bd.run_ns == 300000000u &&
          bd.wait_ns == 200000000u && bd.off_ns == 500000000u && bd.skew_ns == 0u);
    CHECK(bd.slices == 30u && bd.vcsw == 20u && bd.nvcsw == 10u);
    CHECK(bd.run_ns + bd.wait_ns + bd.off_ns == bd.wall_ns);                    /* 회계 항등식 */
    sample_t c = b;
    c.ss.run_ns = a.ss.run_ns + 900000000u; c.ss.wait_ns = a.ss.wait_ns + 150000000u;   /* 1.05 s > wall */
    CHECK(account(&a, &c, &bd) == 0 && bd.off_ns == 0u && bd.skew_ns == 50000000u);
    CHECK(account(&b, &a, &bd) == -1);                                          /* 시간 역행 */
    c = b; c.ss.slices = 3;
    CHECK(account(&a, &c, &bd) == -1);                                          /* 카운터 감소 = 다른 스레드 */
    c = b; c.cs.nvcsw = 0;
    CHECK(account(&a, &c, &bd) == -1);

    state_hist_t h = { 0, 0, 0, 0, 0 };
    for (const char *p = "RRSSSDDZtI"; *p; p++) state_hist_add(&h, *p);
    CHECK(h.n == 10u && h.R == 2u && h.S == 3u && h.D == 2u && h.other == 3u);

    breakdown_t k1 = { .wall_ns = 1000, .run_ns = 400, .wait_ns = 300, .off_ns = 300 };
    breakdown_t k2 = { .wall_ns = 1000, .run_ns = 200, .wait_ns = 500, .off_ns = 300 };
    breakdown_t k3 = { .wall_ns = 1000, .run_ns = 100, .wait_ns = 100, .off_ns = 800 };
    breakdown_t k4 = { .wall_ns = 1000, .run_ns = 400, .wait_ns = 400, .off_ns = 200 };
    state_hist_t hs = { 10, 2, 6, 2, 0 }, hd = { 10, 2, 2, 6, 0 }, hz = { 10, 2, 4, 4, 0 };
    CHECK(classify(&k1, &hs) == TT_ON_CPU);
    CHECK(classify(&k2, &hs) == TT_RUNQ_WAIT);
    CHECK(classify(&k3, &hs) == TT_SLEEP_S && classify(&k3, &hd) == TT_BLOCK_D);
    CHECK(classify(&k3, &hz) == TT_SLEEP_S);                                   /* D == S 면 SLEEP */
    CHECK(classify(&k4, &hd) == TT_ON_CPU);                                    /* 동률은 run 우선 */

    /* ---------- Part C (고정 장면) ---------- */
    breakdown_t fb[NFX];
    int fx_ok = 1, cam = -1, worst = -1;
    double worst_wait = -1.0;
    for (int i = 0; i < NFX; i++) {
        sample_t s0, s1;
        state_hist_t fh = { 0, 0, 0, 0, 0 };
        if (fx_sample(i, 0, &s0) != 0 || fx_sample(i, 1, &s1) != 0 || account(&s0, &s1, &fb[i]) != 0) { fx_ok = 0; continue; }
        for (const char *p = g_fx[i].states; *p; p++) state_hist_add(&fh, *p);
        tt_class_t want = i == 0 ? TT_RUNQ_WAIT : i <= 4 ? TT_ON_CPU : i == 5 ? TT_BLOCK_D : TT_SLEEP_S;
        if (classify(&fb[i], &fh) != want) fx_ok = 0;
        if (strcmp(s1.st.comm, g_fx[i].comm) != 0) fx_ok = 0;
        double wf = (double)fb[i].wait_ns / (double)fb[i].wall_ns;
        if (wf > worst_wait) { worst_wait = wf; worst = i; }
        if (strcmp(g_fx[i].comm, "cam-capture") == 0) cam = i;
        printf("  fx %-11s run %4" PRIu64 " wait %4" PRIu64 " off %4" PRIu64 " ms -> %s\n", s1.st.comm,
               fb[i].run_ns / 1000000u, fb[i].wait_ns / 1000000u, fb[i].off_ns / 1000000u,
               tt_class_name[classify(&fb[i], &fh)]);
    }
    CHECK(fx_ok);                                               /* 7개 스레드 모두 파싱·분해·판정 일치 */
    CHECK(cam == 0 && worst == 0 && fb[0].off_ns == 270000000u); /* 런큐 대기 비율 1위 = cam-capture */
    sys_check_t sc;
    CHECK(verify_wall(fb, NFX, 4, 0.01, &sc) == 0 && sc.bad_threads == 0);
    CHECK(sc.util > 0.934 && sc.util < 0.936);                  /* Σrun 3.74 s / (4 × 1 s) */
    CHECK(sc.avg_runnable > 4.755 && sc.avg_runnable < 4.757 && sc.oversub);   /* 4.756 > 4 코어 */
    breakdown_t bad[NFX];
    memcpy(bad, fb, sizeof bad);
    bad[5].run_ns = 1040000000u; bad[5].off_ns = 0; bad[5].skew_ns = 45000000u;  /* 4.5 % 넘침 */
    CHECK(verify_wall(bad, NFX, 4, 0.01, &sc) >= 1 && sc.bad_threads == 1);
    CHECK(verify_wall(fb, NFX, 3, 0.01, &sc) >= 1 && sc.util > 1.0);          /* 3코어라면 Σrun 이 용량 초과 */
    CHECK(verify_wall(fb, 0, 4, 0.01, &sc) == -1);

    /* ---------- Part C (라이브: 진짜 스레드 하나) ---------- */
    pthread_t th;
    pthread_create(&th, NULL, worker, NULL);
    int got_tid = wait_phase(1, 5000);
    pthread_mutex_lock(&g_mu);
    int tid = g_tid;
    pthread_mutex_unlock(&g_mu);
    sample_t l0, l1;
    memset(&l0, 0, sizeof l0);
    memset(&l1, 0, sizeof l1);
    int r0 = read_task_sample((int)getpid(), tid, &l0);
    set_phase(2);
    int parked = wait_phase(3, 5000);
    char seen = '?';                                        /* 주차한 뒤 state 가 S 가 될 때까지 (최대 2 s) */
    for (int i = 0; i < 200 && seen != 'S'; i++) {
        sample_t tmp;
        if (read_task_sample((int)getpid(), tid, &tmp) == 0) seen = tmp.st.state;
        struct timespec d = { 0, 10000000L };
        nanosleep(&d, NULL);
    }
    int r1 = read_task_sample((int)getpid(), tid, &l1);
    set_phase(4);
    pthread_join(th, NULL);
    breakdown_t lb;
    memset(&lb, 0, sizeof lb);
    int ra = account(&l0, &l1, &lb);
    CHECK(got_tid && parked && r0 == 0 && r1 == 0 && ra == 0);
    CHECK(l1.st.pid == tid && seen == 'S');                     /* 조건변수 대기 = S */
    CHECK(lb.run_ns >= 29000000u);                              /* 30 ms 돌았다 (같은 sum_exec_runtime 기반) */
    CHECK(lb.off_ns >= 30000000u);                              /* 30 ms nanosleep + 주차 시간 */
    CHECK(lb.vcsw >= 2u && lb.slices >= 2u);                    /* nanosleep, cond_wait 두 번 스스로 잠듦 */
    CHECK(lb.skew_ns > 0 || lb.run_ns + lb.wait_ns + lb.off_ns == lb.wall_ns);  /* 항등식 (넘침이 없으면) */
    printf("  live: wall %.1f ms = run %.1f + wait %.3f + off %.1f  (slices %" PRIu64 ", vcsw %" PRIu64
           ", nvcsw %" PRIu64 ")\n", lb.wall_ns / 1e6, lb.run_ns / 1e6, lb.wait_ns / 1e6, lb.off_ns / 1e6,
           lb.slices, lb.vcsw, lb.nvcsw);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif /* TTIME_LIB_ONLY */
