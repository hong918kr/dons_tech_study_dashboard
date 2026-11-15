/* OS-M12 demo_load.c — 동시 연결 N 개(기본 1000)로 세 I/O 모델을 같은 조건에서 잰다 */
/* BUILD: -pthread */
/*
 * solution.c 의 서버를 그대로 쓴다 (ECHO_NO_MAIN 으로 테스트 main 만 뺌).
 * 서버는 fork 한 자식 프로세스에서 돈다 → RSS · 스레드 수 · 문맥 교환 · CPU 시간이 서버 것만 잡힌다.
 * 부하 생성기(부모)는 kqueue 하나로 N 개 소켓을 돌린다: 연결 N 개 → 각자 64 B 왕복 M 번(닫힌 고리).
 *   보고: 연결 수립 시간(conn/s), 왕복 RTT p50/p99, msgs/s, 서버 RSS(유휴 → N 연결 유지 중),
 *         연결당 메모리, 서버 스레드 수, 자발적/비자발적 문맥 교환, 서버 CPU 시간.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_load.c -o demo_load
 * run:   ./demo_load [N=1000] [M=100] [model=all|thread|select|kqueue|kqclear]
 *        ./demo_load 1100 10 select     → FD_SETSIZE 가드가 연결을 거절하는 모습
 *        ./demo_load 1000 2000 all 10   → 1000 개 연결 중 10 개만 활성: select 는 매번 1000 개를 훑는다
 * 숫자는 공유된 맥(다른 작업이 도는 중)에서 잰 것이다. 절댓값보다 모델 사이의 비율을 본다.
 */
#define _DARWIN_C_SOURCE     /* struct rusage 의 ru_maxrss · ru_nvcsw 는 strict POSIX 에서 안 보인다.
                                (부작용: select 가 "제한 없는" 변형이 된다. 서버의 sel_fd_ok 가드는 그대로 동작) */
#define ECHO_NO_MAIN
#include "solution.c"
#include <mach/mach.h>

typedef struct {
    long rss, maxrss, threads, nvcsw, nivcsw, accepted, rejected, live, loops;
    double cpu;
} report_t;

static long task_rss(void) {
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t cnt = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &cnt) != KERN_SUCCESS) return -1;
    return (long)info.resident_size;
}
static long task_nthreads(void) {
    thread_act_array_t th;
    mach_msg_type_number_t n = 0;
    if (task_threads(mach_task_self(), &th, &n) != KERN_SUCCESS) return -1;
    for (mach_msg_type_number_t i = 0; i < n; i++) mach_port_deallocate(mach_task_self(), th[i]);
    vm_deallocate(mach_task_self(), (vm_address_t)th, n * sizeof th[0]);
    return (long)n;
}
static int64_t now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec;
}
static bool read_full(int fd, void *p, size_t n) {
    size_t g = 0;
    while (g < n) {
        ssize_t r = read(fd, (char *)p + g, n - g);
        if (r <= 0) { if (r < 0 && errno == EINTR) continue; return false; }
        g += (size_t)r;
    }
    return true;
}

/* 자식: 서버를 띄우고 제어 파이프 명령을 기다린다. 'R' = 보고, 'Q' = 정지 후 종료 */
static void child_server(srv_cfg_t cfg, int ctl, int rep) {
    server_t s;
    if (server_start(&s, cfg) != 0) _exit(2);
    int port = s.port;
    ssize_t w = write(rep, &port, sizeof port);
    (void)w;
    char cmd;
    while (read_full(ctl, &cmd, 1) && cmd == 'R') {
        struct rusage ru;
        getrusage(RUSAGE_SELF, &ru);
        report_t r = { task_rss(), ru.ru_maxrss, task_nthreads(), ru.ru_nvcsw, ru.ru_nivcsw,
                       st_get(&s.st.accepted), st_get(&s.st.rejected), st_get(&s.st.live), st_get(&s.st.loops),
                       (double)ru.ru_utime.tv_sec + ru.ru_utime.tv_usec * 1e-6 +
                       (double)ru.ru_stime.tv_sec + ru.ru_stime.tv_usec * 1e-6 };
        w = write(rep, &r, sizeof r);
    }
    server_stop(&s);
    _exit(0);
}

static int cmp_i64(const void *a, const void *b) {
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

typedef struct { int fd, rt, tries; size_t got; int64_t t0; bool up, dead, done; } lconn_t;

static int start_connect(int kq, lconn_t *c, const struct sockaddr_in *a) {
    c->fd = socket(AF_INET, SOCK_STREAM, 0);
    set_nonblock(c->fd, true);
    if (connect(c->fd, (const struct sockaddr *)a, sizeof *a) < 0 && errno != EINPROGRESS) {
        close(c->fd);
        c->fd = -1;
        return -1;
    }
    struct kevent ch;
    EV_SET(&ch, c->fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, c);
    return kevent(kq, &ch, 1, NULL, 0, NULL) < 0 ? -1 : 0;
}

static void run_model(const char *name, srv_cfg_t cfg, int N, int M, int A) {
    int ctl[2], rep[2];
    if (pipe(ctl) || pipe(rep)) { perror("pipe"); return; }
    pid_t pid = fork();
    if (pid == 0) {
        close(ctl[1]); close(rep[0]);
        child_server(cfg, ctl[0], rep[1]);
    }
    close(ctl[0]); close(rep[1]);
    int port = 0;
    report_t idle, busy, fin;
    memset(&busy, 0, sizeof busy);
    if (!read_full(rep[0], &port, sizeof port)) { printf("%s: server failed\n", name); return; }
    ssize_t w = write(ctl[1], "R", 1);
    read_full(rep[0], &idle, sizeof idle);

    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    lconn_t *cs = calloc((size_t)N, sizeof *cs);
    int64_t *rtt = malloc((size_t)N * (size_t)M * sizeof *rtt);
    long nrtt = 0;
    int kq = kqueue();
    struct kevent ev[256];

    /* 1) 연결 N 개: 논블로킹 connect → EVFILT_WRITE 로 완료 확인.
     *    listen 큐(macOS 는 128 로 잘림)가 넘치면 연결이 RST 로 거절될 수 있다 → 진짜 클라이언트처럼 다시 시도하고 센다 */
    int64_t t_setup0 = now_ns();
    int up = 0, failed = 0, first_err = 0, retries = 0;
    for (int i = 0; i < N; i++)
        if (start_connect(kq, &cs[i], &a) != 0) { cs[i].dead = true; failed++; }
    int64_t dl = now_ms() + 30000;
    while (up + failed < N && now_ms() < dl) {
        struct timespec t = { 0, 100 * 1000000L };
        int n = kevent(kq, NULL, 0, ev, 256, &t);
        for (int i = 0; i < n; i++) {
            lconn_t *c = ev[i].udata;
            int err = 0;
            socklen_t el = sizeof err;
            getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &err, &el);
            if (!err) { c->up = true; up++; continue; }
            if (!first_err) first_err = err;
            close(c->fd);
            retries++;
            if (++c->tries > 50 || start_connect(kq, c, &a) != 0) { c->fd = -1; c->dead = true; failed++; }
        }
    }
    int64_t t_setup = now_ns() - t_setup0;

    /* 2) 각 연결이 64 B 왕복 M 번 (닫힌 고리) */
    char msg[64];
    memset(msg, 'm', sizeof msg);
    int64_t t_run0 = now_ns();
    int active = 0, eofs = 0;
    for (int i = 0; i < N; i++) {
        if (!cs[i].up || i >= A) continue;          /* 앞의 A 개만 왕복, 나머지는 연결만 유지(유휴) */
        struct kevent ch;
        EV_SET(&ch, cs[i].fd, EVFILT_READ, EV_ADD, 0, 0, &cs[i]);
        kevent(kq, &ch, 1, NULL, 0, NULL);
        cs[i].t0 = now_ns();
        if (write(cs[i].fd, msg, 64) != 64) { cs[i].dead = true; continue; }
        active++;
    }
    dl = now_ms() + 60000;
    while (active > 0 && now_ms() < dl) {
        struct timespec t = { 0, 100 * 1000000L };
        int n = kevent(kq, NULL, 0, ev, 256, &t);
        for (int i = 0; i < n; i++) {
            lconn_t *c = ev[i].udata;
            if (c->done || c->dead) continue;
            char buf[64];
            ssize_t r = read(c->fd, buf, 64 - c->got);
            if (r <= 0) {
                if (r < 0 && errno == EAGAIN) continue;
                c->dead = true; eofs++; active--;
                continue;
            }
            c->got += (size_t)r;
            if (c->got < 64) continue;
            rtt[nrtt++] = now_ns() - c->t0;
            c->got = 0;
            if (++c->rt == M) { c->done = true; active--; continue; }
            c->t0 = now_ns();
            if (write(c->fd, msg, 64) != 64) { c->dead = true; active--; }
        }
    }
    int64_t t_run = now_ns() - t_run0;

    /* 3) N 연결을 연 채로 서버 상태 보고 → 닫고 정지 */
    w = write(ctl[1], "R", 1);
    read_full(rep[0], &busy, sizeof busy);
    for (int i = 0; i < N; i++) if (cs[i].fd >= 0) close(cs[i].fd);
    struct timespec settle = { 0, 200 * 1000000L };
    nanosleep(&settle, NULL);
    w = write(ctl[1], "R", 1);
    read_full(rep[0], &fin, sizeof fin);
    w = write(ctl[1], "Q", 1);
    (void)w;
    int st;
    waitpid(pid, &st, 0);
    close(ctl[1]); close(rep[0]); close(kq);

    qsort(rtt, (size_t)nrtt, sizeof *rtt, cmp_i64);
    double p50 = nrtt ? rtt[nrtt / 2] / 1e3 : 0, p99 = nrtt ? rtt[(long)(nrtt * 0.99)] / 1e3 : 0;
    printf("%-14s %5d %5d %8.0f %9.0f %8.1f %8.1f %6.1f→%6.1f %6.1f %5ld %8ld %7ld %8ld %6.2f %5ld %4d\n",
           name, N, up, up / (t_setup / 1e9), nrtt / (t_run / 1e9), p50, p99,
           idle.rss / 1048576.0, busy.rss / 1048576.0,
           up ? (busy.rss - idle.rss) / 1024.0 / up : 0.0, busy.threads,
           fin.nvcsw - idle.nvcsw, fin.nivcsw - idle.nivcsw, fin.loops - idle.loops, fin.cpu - idle.cpu, busy.rejected, eofs);
    if (retries || failed || up + failed < N)
        printf("%-14s connect retries=%d (first error: %s), gave up=%d, unresolved=%d\n", "", retries,
               first_err ? strerror(first_err) : "-", failed, N - up - failed);
    free(cs);
    free(rtt);
}

int main(int argc, char **argv) {
    int N = argc > 1 ? atoi(argv[1]) : 1000, M = argc > 2 ? atoi(argv[2]) : 100;
    const char *only = argc > 3 ? argv[3] : "all";
    int A = argc > 4 ? atoi(argv[4]) : N;                 /* 활성 연결 수 (기본: 전부) */
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, NULL);
    raise_nofile(8192);
    struct rlimit rl;
    getrlimit(RLIMIT_NOFILE, &rl);
    printf("N=%d connections (%d active) x M=%d round trips of 64 B, RLIMIT_NOFILE cur=%llu, CHUNK=%d B/conn buffer\n",
           N, A, M, (unsigned long long)rl.rlim_cur, CHUNK);
    printf("%-14s %5s %5s %8s %9s %8s %8s %15s %6s %5s %8s %7s %8s %6s %5s %4s\n", "model", "N", "up", "conn/s",
           "msgs/s", "p50(us)", "p99(us)", "RSS MB idle→N", "KB/c", "thr", "vcsw", "ivcsw", "wakeups", "cpu s",
           "rej", "eof");
    struct { const char *name, *key; srv_cfg_t cfg; } models[] = {
        { "thread", "thread", { MODEL_THREAD, false, 0 } },
        { "select", "select", { MODEL_SELECT, false, 0 } },
        { "kqueue-level", "kqueue", { MODEL_KQUEUE, false, 0 } },
        { "kqueue-clear", "kqclear", { MODEL_KQUEUE, true, 0 } },
    };
    for (size_t i = 0; i < sizeof models / sizeof models[0]; i++)
        if (!strcmp(only, "all") || !strcmp(only, models[i].key)) {
            fflush(stdout);
            run_model(models[i].name, models[i].cfg, N, M, A);
        }
    return 0;
}
