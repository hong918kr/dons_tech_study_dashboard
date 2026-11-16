/* OS-A12 demo_bench.c — 견고한 shm 링의 비용을 pipe 와 비교하고, 사망 감지 지연을 잰다
 *
 * solution.c 의 링을 그대로 포함한다 (main 만 이름을 바꿈). 프로세스 2 개 (fork), 64 B 메시지.
 *   thru  : 한 방향 N 개 → msgs/s.  shm(BLOCK, cap 1024) vs pipe(메시지마다 write 64 B / read 64 B)
 *   rtt   : 핑퐁 왕복(요청 링 + 응답 링) median / p99.  shm vs pipe 두 개
 *   detect: 생산자를 SIGKILL 한 순간부터 소비자가 아는 순간까지
 *           (a) 감독자가 즉시 회수 → kill(pid,0) ESRCH   (b) 회수 안 함(좀비) → 하트비트 STALE (stale 100 ms)
 *           (c) kqueue EVFILT_PROC NOTE_EXIT
 * 각 항목 runs 번 반복해 중앙값. 기계가 공유·부하 상태라 절대값보다 비율을 볼 것 (uptime 을 같이 출력).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o bench && ./bench [runs=5]
 */
#define main solution_main
#include "solution.c"
#undef main
#include <sys/event.h>

#define MSG 64u
/* 지연 측정용 ns 시계: macOS 의 CLOCK_MONOTONIC 은 1 µs 단위(clock_getres = 1000 ns 실측)라 RTT 엔 거칠다 */
static uint64_t tns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
static double g_wait_ms;
static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}
static uint64_t pctl(uint64_t *v, size_t n, double p) { size_t k = (size_t)(p * (double)(n - 1)); return v[k]; }
static double median_d(double *v, int n) {
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && v[j - 1] > v[j]; j--) { double t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
    return v[n / 2];
}
static bool read_all(int fd, void *b, size_t n) {
    uint8_t *p = b;
    while (n) { ssize_t k = read(fd, p, n); if (k <= 0) return false; p += k; n -= (size_t)k; }
    return true;
}
static bool write_all(int fd, const void *b, size_t n) {
    const uint8_t *p = b;
    while (n) { ssize_t k = write(fd, p, n); if (k <= 0) return false; p += k; n -= (size_t)k; }
    return true;
}

/* ---------- thru ---------- */
static double thru_shm(uint64_t n) {
    const char *nm = new_name();
    rb_t c = {0};
    if (rb_create(&c, nm, 1024u, RB_BLOCK) != RB_OK) return 0;
    c.role = RB_CONSUMER;
    rb_start(&c);
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        rb_t p = {0};
        if (rb_attach(&p, nm, RB_PRODUCER) != RB_OK) _exit(1);
        rb_start(&p);
        uint8_t b[MSG] = { 0 };
        for (uint64_t i = 0; i < n; i++) { memcpy(b, &i, 8); if (rb_send(&p, b, MSG, i, 2000) != RB_OK) _exit(2); }
        _exit(0);
    }
    uint8_t b[RB_MAX_PAYLOAD];
    rb_view_t v;
    uint64_t t0 = now_ns(), bad = 0;
    for (uint64_t i = 0; i < n; i++) {
        if (rb_recv(&c, b, sizeof b, &v, 2000) != RB_OK || v.msg_seq != i) { bad++; break; }
    }
    double dt = (double)(now_ns() - t0) / 1e9;
    reap(pid, 3000);
    rb_close(&c);
    shm_unlink(nm);
    return bad ? 0 : (double)n / dt;
}
static double thru_pipe(uint64_t n) {
    int fd[2];
    if (pipe(fd) != 0) return 0;
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        close(fd[0]);
        uint8_t b[MSG] = { 0 };
        for (uint64_t i = 0; i < n; i++) { memcpy(b, &i, 8); if (!write_all(fd[1], b, MSG)) _exit(2); }
        _exit(0);
    }
    close(fd[1]);
    uint8_t b[MSG];
    uint64_t t0 = now_ns(), bad = 0;
    for (uint64_t i = 0; i < n; i++) {
        uint64_t s;
        if (!read_all(fd[0], b, MSG)) { bad++; break; }
        memcpy(&s, b, 8);
        if (s != i) { bad++; break; }
    }
    double dt = (double)(now_ns() - t0) / 1e9;
    close(fd[0]);
    reap(pid, 3000);
    return bad ? 0 : (double)n / dt;
}

/* ---------- rtt ---------- */
#define ROUNDS 20000
#define WARM 2000
static uint64_t g_lat[ROUNDS];
static void rtt_shm(uint64_t *p50, uint64_t *p99) {
    const char *nq = new_name(), *ns = new_name();
    rb_t rq = {0}, rs = {0};
    if (rb_create(&rq, nq, 64u, RB_BLOCK) != RB_OK || rb_create(&rs, ns, 64u, RB_BLOCK) != RB_OK) return;
    rb_start(&rq);                       /* rq: 부모가 생산자 */
    rs.role = RB_CONSUMER;
    rb_start(&rs);                       /* rs: 부모가 소비자 */
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        rb_t q = {0}, s = {0};
        if (rb_attach(&q, nq, RB_CONSUMER) != RB_OK || rb_attach(&s, ns, RB_PRODUCER) != RB_OK) _exit(1);
        rb_start(&q); rb_start(&s);
        uint8_t b[RB_MAX_PAYLOAD];
        rb_view_t v;
        for (int i = 0; i < ROUNDS + WARM; i++) {
            if (rb_recv(&q, b, sizeof b, &v, 2000) != RB_OK) _exit(2);
            if (rb_send(&s, b, v.len, v.msg_seq, 2000) != RB_OK) _exit(3);
        }
        _exit(0);
    }
    uint8_t b[RB_MAX_PAYLOAD] = { 0 };
    rb_view_t v;
    for (int i = 0; i < ROUNDS + WARM; i++) {
        uint64_t t0 = tns();
        if (rb_send(&rq, b, MSG, (uint64_t)i, 2000) != RB_OK || rb_recv(&rs, b, sizeof b, &v, 2000) != RB_OK) break;
        if (i >= WARM) g_lat[i - WARM] = tns() - t0;
    }
    reap(pid, 3000);
    rb_close(&rq); rb_close(&rs);
    shm_unlink(nq); shm_unlink(ns);
    qsort(g_lat, ROUNDS, sizeof g_lat[0], cmp_u64);
    *p50 = pctl(g_lat, ROUNDS, 0.5); *p99 = pctl(g_lat, ROUNDS, 0.99);
}
static void rtt_pipe(uint64_t *p50, uint64_t *p99) {
    int a[2], b2[2];
    if (pipe(a) != 0 || pipe(b2) != 0) return;
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        close(a[1]); close(b2[0]);
        uint8_t b[MSG];
        for (int i = 0; i < ROUNDS + WARM; i++) { if (!read_all(a[0], b, MSG) || !write_all(b2[1], b, MSG)) _exit(2); }
        _exit(0);
    }
    close(a[0]); close(b2[1]);
    uint8_t b[MSG] = { 0 };
    for (int i = 0; i < ROUNDS + WARM; i++) {
        uint64_t t0 = tns();
        if (!write_all(a[1], b, MSG) || !read_all(b2[0], b, MSG)) break;
        if (i >= WARM) g_lat[i - WARM] = tns() - t0;
    }
    close(a[1]); close(b2[0]);
    reap(pid, 3000);
    qsort(g_lat, ROUNDS, sizeof g_lat[0], cmp_u64);
    *p50 = pctl(g_lat, ROUNDS, 0.5); *p99 = pctl(g_lat, ROUNDS, 0.99);
}

/* ---------- detect ---------- */
enum { D_REAP, D_ZOMBIE, D_KQUEUE };
static double detect_once(int mode) {
    const char *nm = new_name();
    rb_t c = {0};
    if (rb_create(&c, nm, 8u, RB_BLOCK) != RB_OK) return -1;
    c.role = RB_CONSUMER;
    rb_start(&c);
    c.stale_ns = 100u * 1000000u;
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        alarm(5);
        rb_t p = {0};
        if (rb_attach(&p, nm, RB_PRODUCER) != RB_OK) _exit(1);
        rb_start(&p);
        for (;;) { beat(&p); sleep_us(1000); }          /* 보낼 게 없어도 1 ms 마다 박동 */
    }
    uint8_t b[RB_MAX_PAYLOAD];
    rb_view_t v;
    while (atomic_load(&c.h->prod_pid) != (uint32_t)pid) sleep_us(100);
    (void)rb_recv(&c, b, sizeof b, &v, 20);             /* 감시 시작 (ALIVE 확인) */
    int kq = kqueue();
    struct kevent ev, out;
    EV_SET(&ev, (uintptr_t)pid, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT, 0, NULL);
    if (mode == D_KQUEUE) kevent(kq, &ev, 1, NULL, 0, NULL);
    uint64_t t0 = tns();
    kill(pid, SIGKILL);
    double ms = -1;
    if (mode == D_KQUEUE) {
        struct timespec to = { 2, 0 };
        if (kevent(kq, NULL, 0, &out, 1, &to) == 1) ms = (double)(tns() - t0) / 1e6;
        reap(pid, 2000);
    } else {
        if (mode == D_REAP) { waitpid(pid, NULL, 0); g_wait_ms = (double)(tns() - t0) / 1e6; }  /* 감독자가 곧바로 회수 */
        int rc = rb_recv(&c, b, sizeof b, &v, 2000);
        if (rc == RB_PEER_DEAD) ms = (double)(tns() - t0) / 1e6;
        if (mode == D_ZOMBIE) reap(pid, 2000);
    }
    close(kq);
    rb_close(&c);
    shm_unlink(nm);
    return ms;
}

int main(int argc, char **argv) {
    int runs = argc > 1 ? atoi(argv[1]) : 5;
    if (runs < 1 || runs > 10) runs = 5;          /* shm 이름 표(64칸) 안에서 */
    (void)solution_main;
    char up[256] = "";
    FILE *f = popen("uptime", "r");
    if (f) { if (!fgets(up, sizeof up, f)) up[0] = 0; pclose(f); }
    printf("OS-A12 bench: %d runs, 64 B messages | uptime: %s", runs, up);

    const uint64_t N = 1000000u;
    double ts[50], tp[50];
    for (int i = 0; i < runs; i++) { ts[i] = thru_shm(N); tp[i] = thru_pipe(N); }
    printf("thru  shm ring (BLOCK, cap 1024) : %8.2f M msgs/s  (median of %d, N = %llu)\n", median_d(ts, runs) / 1e6, runs, (unsigned long long)N);
    printf("thru  pipe (write/read 64 B)     : %8.2f M msgs/s\n", median_d(tp, runs) / 1e6);

    double s50[50], s99[50], p50[50], p99[50];
    for (int i = 0; i < runs; i++) {
        uint64_t a = 0, b = 0, c = 0, d = 0;
        rtt_shm(&a, &b); rtt_pipe(&c, &d);
        s50[i] = (double)a; s99[i] = (double)b; p50[i] = (double)c; p99[i] = (double)d;
    }
    printf("rtt   shm ring  p50 %7.2f us  p99 %8.2f us  (%d rounds, median of %d)\n",
           median_d(s50, runs) / 1e3, median_d(s99, runs) / 1e3, ROUNDS, runs);
    printf("rtt   pipe      p50 %7.2f us  p99 %8.2f us\n", median_d(p50, runs) / 1e3, median_d(p99, runs) / 1e3);

    static const char *const DN[] = { "reaped -> kill ESRCH", "zombie -> hb STALE(100ms)", "kqueue NOTE_EXIT" };
    for (int m = 0; m < 3; m++) {
        double d[50], w[50];
        double mn = 1e9, mx = 0;
        for (int i = 0; i < runs; i++) { d[i] = detect_once(m); w[i] = g_wait_ms; if (d[i] < mn) mn = d[i]; if (d[i] > mx) mx = d[i]; }
        printf("detect %-26s median %7.2f ms  (min %.2f, max %.2f)", DN[m], median_d(d, runs), mn, mx);
        if (m == D_REAP) printf("  [of which kill->waitpid %.2f ms]", median_d(w, runs));
        printf("\n");
    }
    int leaked = 0;
    for (int i = 0; i < g_nnames; i++) { int fd = shm_open(g_names[i], O_RDONLY, 0); if (fd >= 0) { leaked++; close(fd); shm_unlink(g_names[i]); } }
    printf("shm names leaked: %d\n", leaked);
    return 0;
}
