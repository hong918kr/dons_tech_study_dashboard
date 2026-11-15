/* ENV: linux */
/* OS-M15 demo_bench.c — POSIX mq vs pipe vs Unix 소켓: 왕복 지연(RTT) · 단방향 처리량 · 블록 없는 한 쌍 비용
 *
 * OS-B14 하네스를 축소해 넣었다: 채널 = "양방향 두 끝" (parent 끝 0, child 끝 1), send/recv 는 메시지 단위.
 *   pipe     : 파이프 2개 (바이트 스트림 → read_all 로 정확히 n 바이트)
 *   stream   : socketpair(AF_UNIX, SOCK_STREAM)   (바이트 스트림)
 *   seqpkt   : socketpair(AF_UNIX, SOCK_SEQPACKET) (메시지 경계 유지 — mq 와 가장 비슷한 소켓)
 *   mq       : POSIX 큐 2개 (P→C, C→P), maxmsg 10 (비특권 msg_max), msgsize 8192
 *
 *   rtt   : 부모가 n 바이트 보내고 자식이 그대로 돌려줄 때까지 = 왕복 1회 (CLOCK_MONOTONIC)
 *           크기 1/64/1024/8192 B, 처음 WARM 회 버림, run 마다 min/median/p99 → runs 번의 중앙값(min 은 최솟값)
 *   bulk  : 부모 → 자식 단방향 N 개, 자식이 다 받으면 1 바이트 확인 → msgs/s, MB/s (runs 번 중앙값)
 *   pair  : 프로세스 1개에서 블록 없는 send+recv 한 쌍 (문맥 교환 없이 syscall 2번의 비용)
 *
 * build: gcc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o bench
 * run  : ./bench rtt [runs=5] [rounds=20000] | ./bench bulk [runs=5] | ./bench pair [runs=5]
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum { V_PIPE, V_STREAM, V_SEQ, V_MQ, V_N };
static const char *const V_NAME[V_N] = { "pipe", "stream", "seqpkt", "mq" };
#define MSGMAX 8192
#define WARM 1000

typedef struct {
    int v;
    int rfd[2], wfd[2];      /* 끝 e 가 읽는 fd / 쓰는 fd */
    mqd_t mq[2];             /* mq[0]: P→C, mq[1]: C→P */
    char name[2][64];
} chan_t;

static double now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e9 + t.tv_nsec;
}

static int chan_create(chan_t *c, int v, int id) {
    memset(c, 0, sizeof *c);
    c->v = v;
    if (v == V_PIPE) {
        int a[2], b[2]; /* a: P→C, b: C→P */
        if (pipe(a) || pipe(b)) return -1;
        c->wfd[0] = a[1]; c->rfd[1] = a[0]; c->wfd[1] = b[1]; c->rfd[0] = b[0];
    } else if (v == V_STREAM || v == V_SEQ) {
        int s[2];
        if (socketpair(AF_UNIX, v == V_STREAM ? SOCK_STREAM : SOCK_SEQPACKET, 0, s)) return -1;
        c->rfd[0] = c->wfd[0] = s[0];
        c->rfd[1] = c->wfd[1] = s[1];
    } else {
        struct mq_attr a = { .mq_maxmsg = 10, .mq_msgsize = MSGMAX };
        for (int i = 0; i < 2; i++) {
            snprintf(c->name[i], 64, "/OS-M15-bench-%d-%d-%d", (int)getpid(), id, i);
            mq_unlink(c->name[i]);
            c->mq[i] = mq_open(c->name[i], O_CREAT | O_EXCL | O_RDWR, 0600, &a);
            if (c->mq[i] == (mqd_t)-1) return -1;
            mq_unlink(c->name[i]); /* 이름은 바로 지운다 — fork 한 자식은 디스크립터를 물려받는다 */
        }
    }
    return 0;
}
static void chan_close(chan_t *c) {
    if (c->v == V_MQ) { mq_close(c->mq[0]); mq_close(c->mq[1]); return; }
    close(c->rfd[0]); close(c->wfd[0]);
    if (c->v == V_PIPE) { close(c->rfd[1]); close(c->wfd[1]); }
    else close(c->rfd[1]);
}
static int send_m(chan_t *c, int end, const void *buf, size_t n) {
    if (c->v == V_MQ) return mq_send(c->mq[end], buf, n, 0);
    const char *p = buf;
    while (n) {
        ssize_t w = write(c->wfd[end], p, n);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        p += w; n -= (size_t)w;
    }
    return 0;
}
static int recv_m(chan_t *c, int end, void *buf, size_t n) {
    if (c->v == V_MQ) { /* 끝 0(부모) 은 C→P 큐에서, 끝 1(자식) 은 P→C 큐에서 받는다 */
        static char tmp[MSGMAX];
        ssize_t r = mq_receive(c->mq[1 - end], tmp, sizeof tmp, NULL);
        if (r != (ssize_t)n) return -1;
        memcpy(buf, tmp, n);
        return 0;
    }
    char *p = buf;
    while (n) {
        ssize_t r = read(c->rfd[end], p, n);
        if (r <= 0) { if (r < 0 && errno == EINTR) continue; return -1; }
        p += r; n -= (size_t)r;
    }
    return 0;
}
/* mq 는 끝 0 이 mq[0] 에 보내고 끝 1 이 mq[0] 에서 받는다 → send 는 mq[end], recv 는 mq[1-end] */

static int cmp_d(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static double pct(double *v, int n, double p) {
    int k = (int)(p * n + 0.999999) - 1;
    if (k < 0) k = 0;
    if (k >= n) k = n - 1;
    return v[k];
}
static double med(double *v, int n) { qsort(v, (size_t)n, sizeof *v, cmp_d); return pct(v, n, 0.5); }

static const size_t SZ[] = { 1, 64, 1024, 8192 };
#define NSZ 4
static char g_buf[MSGMAX], g_buf2[MSGMAX];

static void bench_rtt(int runs, int rounds) {
    double *lat = malloc(sizeof(double) * (size_t)rounds);
    double mins[V_N][NSZ][16], meds[V_N][NSZ][16], p99s[V_N][NSZ][16];
    for (int v = 0; v < V_N; v++)
        for (int r = 0; r < runs; r++) {
            chan_t c;
            if (chan_create(&c, v, r) < 0) { perror("chan_create"); exit(1); }
            fflush(stdout);
            pid_t pid = fork();
            if (pid == 0) {
                for (int s = 0; s < NSZ; s++)
                    for (int k = 0; k < WARM + rounds; k++)
                        if (recv_m(&c, 1, g_buf, SZ[s]) || send_m(&c, 1, g_buf, SZ[s])) _exit(3);
                _exit(0);
            }
            for (int s = 0; s < NSZ; s++) {
                memset(g_buf, (int)s, SZ[s]);
                for (int k = 0; k < WARM + rounds; k++) {
                    double t0 = now_ns();
                    if (send_m(&c, 0, g_buf, SZ[s]) || recv_m(&c, 0, g_buf2, SZ[s])) { fprintf(stderr, "io\n"); exit(1); }
                    double t1 = now_ns();
                    if (k >= WARM) lat[k - WARM] = t1 - t0;
                }
                qsort(lat, (size_t)rounds, sizeof *lat, cmp_d);
                mins[v][s][r] = lat[0];
                meds[v][s][r] = pct(lat, rounds, 0.5);
                p99s[v][s][r] = pct(lat, rounds, 0.99);
            }
            waitpid(pid, NULL, 0);
            chan_close(&c);
        }
    printf("RTT (ns)  runs=%d rounds=%d  — min / median / p99 (runs 의 중앙값, min 은 최솟값)\n", runs, rounds);
    printf("%-8s", "size");
    for (int v = 0; v < V_N; v++) printf(" | %-22s", V_NAME[v]);
    printf("\n");
    for (int s = 0; s < NSZ; s++) {
        printf("%-8zu", SZ[s]);
        for (int v = 0; v < V_N; v++) {
            double mn = mins[v][s][0];
            for (int r = 1; r < runs; r++) if (mins[v][s][r] < mn) mn = mins[v][s][r];
            printf(" | %6.0f %7.0f %7.0f", mn, med(meds[v][s], runs), med(p99s[v][s], runs));
        }
        printf("\n");
    }
    free(lat);
}

static void bench_bulk(int runs) {
    const size_t bs[2] = { 64, 8192 };
    const int cnt[2] = { 200000, 50000 };
    printf("단방향 처리량 (부모 → 자식, 자식이 다 받으면 1 B 확인)  runs=%d, 중앙값\n", runs);
    printf("%-8s %-8s %12s %10s\n", "variant", "size", "msgs/s", "MB/s");
    for (int b = 0; b < 2; b++)
        for (int v = 0; v < V_N; v++) {
            double rate[16];
            for (int r = 0; r < runs; r++) {
                chan_t c;
                if (chan_create(&c, v, 100 + r) < 0) { perror("chan_create"); exit(1); }
                fflush(stdout);
                pid_t pid = fork();
                if (pid == 0) {
                    for (int k = 0; k < cnt[b]; k++) if (recv_m(&c, 1, g_buf, bs[b])) _exit(3);
                    _exit(send_m(&c, 1, "k", 1) ? 4 : 0);
                }
                double t0 = now_ns();
                for (int k = 0; k < cnt[b]; k++) if (send_m(&c, 0, g_buf, bs[b])) exit(1);
                char k1;
                if (recv_m(&c, 0, &k1, 1)) exit(1);
                double t1 = now_ns();
                waitpid(pid, NULL, 0);
                chan_close(&c);
                rate[r] = cnt[b] / ((t1 - t0) / 1e9);
            }
            double m = med(rate, runs);
            printf("%-8s %-8zu %12.0f %10.1f\n", V_NAME[v], bs[b], m, m * (double)bs[b] / 1e6);
        }
}

static void bench_pair(int runs) {
    enum { N = 200000 };
    printf("블록 없는 send+recv 한 쌍 (프로세스 1개, 64 B), ns/쌍, runs=%d 중앙값\n", runs);
    for (int v = 0; v < V_N; v++) {
        double t[16];
        for (int r = 0; r < runs; r++) {
            chan_t c;
            if (chan_create(&c, v, 200 + r) < 0) { perror("chan_create"); exit(1); }
            for (int k = 0; k < 1000; k++) { send_m(&c, 0, g_buf, 64); recv_m(&c, 1, g_buf2, 64); }
            double t0 = now_ns();
            for (int k = 0; k < N; k++)
                if (send_m(&c, 0, g_buf, 64) || recv_m(&c, 1, g_buf2, 64)) exit(1);
            t[r] = (now_ns() - t0) / N;
            chan_close(&c);
        }
        printf("  %-8s %7.0f ns\n", V_NAME[v], med(t, runs));
    }
}

int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "rtt";
    int runs = argc > 2 ? atoi(argv[2]) : 5;
    if (runs < 1) runs = 1;
    if (runs > 16) runs = 16;
    if (!strcmp(mode, "rtt")) bench_rtt(runs, argc > 3 ? atoi(argv[3]) : 20000);
    else if (!strcmp(mode, "bulk")) bench_bulk(runs);
    else if (!strcmp(mode, "pair")) bench_pair(runs);
    else { fprintf(stderr, "usage: %s rtt|bulk|pair [runs] [rounds]\n", argv[0]); return 2; }
    return 0;
}
