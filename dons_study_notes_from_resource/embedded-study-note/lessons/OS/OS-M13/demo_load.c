/* ENV: linux */
/* BUILD: -pthread */
/* OS-M13 demo_load.c — 연결 1000 개 × 64 B 왕복 20 번: LT / ET / ONESHOT 풀(워커 4) 비교 (관찰용, CHECK 없음).
 * 서버는 fork 한 자식 프로세스에서 돈다 → 자식의 VmHWM(최대 RSS)과 CPU 시간(wait4)을 따로 잰다.
 * 부모: 연결 1000 개를 차례로 connect (conn/s), 클라이언트 스레드 4 개가 각자 epoll 로 250 연결씩 몰고
 * 왕복 지연(RTT) p50/p99 와 msgs/s 를 낸다. 숫자는 공유 VM 위의 것이므로 비율로만 읽을 것.
 * build (Lima VM): gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_load.c -o load && ./load [N] [M] [lt|et|pool]
 */
#define OSM13_LIB_ONLY
#pragma GCC diagnostic ignored "-Wunused-function"
#include "solution.c"
#include <sys/resource.h>
#include <sys/wait.h>

#define NCT 4 /* 클라이언트 스레드 */
static int g_n = 1000, g_m = 20;

static long now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000000L + t.tv_nsec;
}

typedef struct { int fd, rounds; size_t got; long t_sent; } lconn_t;
typedef struct { lconn_t *c; int n; long *rtt; int nrtt; } lthr_t;

static void *drive(void *p) { /* 이 스레드가 맡은 연결들을 epoll 하나로 몬다 */
    lthr_t *t = p;
    unsigned char msg[64], in[64];
    memset(msg, 'x', sizeof msg);
    int ep = epoll_create1(0), done = 0;
    for (int i = 0; i < t->n; i++) {
        struct epoll_event e = { .events = EPOLLIN, .data.u32 = (uint32_t)i };
        epoll_ctl(ep, EPOLL_CTL_ADD, t->c[i].fd, &e);
        t->c[i].t_sent = now_ns();
        if (send(t->c[i].fd, msg, 64, MSG_NOSIGNAL) != 64) done++;
    }
    long deadline = now_ns() + 30000000000L;
    struct epoll_event ev[64];
    while (done < t->n && now_ns() < deadline) {
        int k = epoll_wait(ep, ev, 64, 1000);
        for (int j = 0; j < k; j++) {
            lconn_t *c = &t->c[ev[j].data.u32];
            ssize_t r = recv(c->fd, in, 64 - c->got, 0);
            if (r <= 0) continue;
            c->got += (size_t)r;
            if (c->got < 64) continue;
            t->rtt[t->nrtt++] = now_ns() - c->t_sent;
            c->got = 0;
            if (++c->rounds < g_m) {
                c->t_sent = now_ns();
                if (send(c->fd, msg, 64, MSG_NOSIGNAL) != 64) done++;
            } else {
                done++;
            }
        }
    }
    close(ep);
    return NULL;
}

static int cmp_long(const void *a, const void *b) {
    long x = *(const long *)a, y = *(const long *)b;
    return (x > y) - (x < y);
}

static void run_mode(int mode, int nworkers) {
    server_t *s = server_create(mode, nworkers, 0);
    if (!s) { perror("server_create"); return; }
    int pfd[2];
    if (pipe(pfd) < 0) return;
    pid_t pid = fork();
    if (pid == 0) { /* 자식 = 서버 */
        close(pfd[0]);
        server_run(s);
        server_join(s); /* 부모가 wakefd 에 쓰면 끝난다 */
        char line[256], out[256] = "VmHWM: ?";
        FILE *f = fopen("/proc/self/status", "r");
        while (f && fgets(line, sizeof line, f))
            if (!strncmp(line, "VmHWM:", 6)) { snprintf(out, sizeof out, "%s", line); }
        if (f) fclose(f);
        if (write(pfd[1], out, strlen(out)) < 0) _exit(1);
        _exit(0);
    }
    close(pfd[1]);
    close(s->lfd); /* 부모는 wakefd 만 쓴다 */
    close(s->epfd);

    lconn_t *c = calloc((size_t)g_n, sizeof *c);
    long t0 = now_ns();
    int nc = 0;
    for (; nc < g_n; nc++) {
        c[nc].fd = cli_connect(s->port);
        if (c[nc].fd < 0) break;
    }
    long t1 = now_ns();
    lthr_t th[NCT];
    pthread_t tid[NCT];
    int per = (nc + NCT - 1) / NCT;
    for (int i = 0; i < NCT; i++) {
        int lo = i * per, hi = lo + per > nc ? nc : lo + per;
        th[i] = (lthr_t){ c + lo, hi > lo ? hi - lo : 0, malloc(sizeof(long) * (size_t)per * (size_t)g_m + 8), 0 };
        pthread_create(&tid[i], NULL, drive, &th[i]);
    }
    for (int i = 0; i < NCT; i++) pthread_join(tid[i], NULL);
    long t2 = now_ns();
    long *all = malloc(sizeof(long) * (size_t)nc * (size_t)g_m + 8);
    int na = 0;
    for (int i = 0; i < NCT; i++) {
        memcpy(all + na, th[i].rtt, sizeof(long) * (size_t)th[i].nrtt);
        na += th[i].nrtt;
        free(th[i].rtt);
    }
    qsort(all, (size_t)na, sizeof(long), cmp_long);
    for (int i = 0; i < nc; i++) close(c[i].fd);
    uint64_t one = 1;
    if (write(s->wakefd, &one, sizeof one) < 0) perror("wake");
    struct rusage ru;
    int st;
    wait4(pid, &st, 0, &ru);
    char hwm[256] = { 0 };
    if (read(pfd[0], hwm, sizeof hwm - 1) < 0) hwm[0] = 0;
    hwm[strcspn(hwm, "\n")] = 0;
    close(pfd[0]);
    close(s->wakefd);
    double cpu = (double)ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6 + (double)ru.ru_stime.tv_sec +
                 ru.ru_stime.tv_usec / 1e6;
    printf("%-14s conns %d  setup %.0f conn/s | RTT p50 %.1f us p99 %.1f us | %.0f msgs/s (%d 왕복) | server CPU %.3f s, %s\n",
           mode == M_POOL ? "ONESHOT pool4" : MODE_NAME[mode], nc, nc / ((t1 - t0) / 1e9),
           na ? all[na / 2] / 1e3 : 0.0, na ? all[(long)na * 99 / 100] / 1e3 : 0.0, na / ((t2 - t1) / 1e9), na, cpu,
           hwm);
    free(all);
    free(c);
    free(s);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc > 1) g_n = atoi(argv[1]);
    if (argc > 2) g_m = atoi(argv[2]);
    if (g_n < 1 || g_n > 1900) g_n = 1000; /* 서버 conns[] 는 fd < 4096 */
    if (g_m < 1) g_m = 20;
    struct rlimit rl;
    getrlimit(RLIMIT_NOFILE, &rl);
    rlim_t want = rl.rlim_max < 4096 ? rl.rlim_max : 4096;
    if (rl.rlim_cur < want) { rl.rlim_cur = want; setrlimit(RLIMIT_NOFILE, &rl); }
    getrlimit(RLIMIT_NOFILE, &rl);
    printf("RLIMIT_NOFILE soft %llu hard %llu, N=%d M=%d, client threads %d\n", (unsigned long long)rl.rlim_cur,
           (unsigned long long)rl.rlim_max, g_n, g_m, NCT);
    const char *only = argc > 3 ? argv[3] : ""; /* "lt" / "et" / "pool" 이면 그 모드만 한 번 (strace 용) */
    for (int rep = 0; rep < (*only ? 1 : 3); rep++) {
        if (!*only || !strcmp(only, "lt")) run_mode(M_LT, 1);
        if (!*only || !strcmp(only, "et")) run_mode(M_ET, 1);
        if (!*only || !strcmp(only, "pool")) run_mode(M_POOL, 4);
    }
    return 0;
}
