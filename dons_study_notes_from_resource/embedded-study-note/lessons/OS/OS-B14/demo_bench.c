/* OS-B14 demo_bench.c — 4가지 IPC 의 왕복 지연(RTT)과 대량 전송 처리량을 "같은 코드" 로 잰다
 *
 * solution.c 의 채널(chan_create / chan_attach / chan_send / chan_recv)을 그대로 가져다 쓴다.
 * 수단(variant): pipe, fifo, sock(기본 버퍼), sock256k(SO_SNDBUF/SO_RCVBUF = 256 KB), shm(+sem)
 *
 *   rtt   : 클라이언트가 n 바이트 보내고 서버가 그대로 돌려줄 때까지 = 왕복 1회.
 *           크기 1 B .. 64 KB, 왕복마다 mach_absolute_time, 처음 WARM 회는 버림.
 *           한 run 의 min / median / p99 → runs 번 반복 → 각 통계의 중앙값(min 은 최솟값)을 출력.
 *   bulk  : MB 메가바이트(MiB)를 chunk KB 씩 한 방향으로 보내고, 서버가 EOF 를 본 뒤 보내는 8 바이트
 *           확인 응답까지의 시간 → MB/s (1 MB = 10^6 B). runs 번의 중앙값과 최소·최대.
 *   reads : 64 KB 씩 써도 read() 한 번은 몇 바이트를 주나 (짧은 읽기 분포 → read_all 이 필요한 이유)
 *   pair  : 블록 없는 "보내기+받기" 한 쌍의 비용 (프로세스 1개): pipe write+read, socketpair write+read,
 *           이름 있는 세마포어 sem_post+sem_wait — 1 B 왕복 지연의 바닥이 어디서 오는지 본다
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o bench      (solution.c 가 같은 폴더에 있어야 함)
 * run  : ./bench rtt  [runs=5] [rounds=3000]
 *        ./bench bulk [runs=5] [MB=256] [chunkKB=64]
 *        ./bench reads
 *        ./bench pair [runs=5]
 */
#define _DARWIN_C_SOURCE
#define OSB14_NO_MAIN
#pragma clang diagnostic ignored "-Wunused-function"   /* solution.c 의 테스트 전용 함수는 안 쓴다 */
#include "solution.c"
#include <mach/mach_time.h>

#define WARM 300
enum { V_PIPE, V_FIFO, V_SOCK, V_SOCK256, V_SHM, V_N };
static const char *const V_NAME[V_N] = { "pipe", "fifo", "sock", "sock256k", "shm+sem" };
static const int V_T[V_N] = { T_PIPE, T_FIFO, T_SOCK, T_SOCK, T_SHM };

static mach_timebase_info_data_t g_tb;
static double t2ns(uint64_t t) { return (double)t * g_tb.numer / g_tb.denom; }
static int cmp_d(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static double pct(double *v, int n, double p) {        /* 정렬된 배열의 p 분위 (nearest-rank) */
    int k = (int)(p * n + 0.999999) - 1;
    if (k < 0) k = 0;
    if (k >= n) k = n - 1;
    return v[k];
}
static double median_of(double *v, int n) { qsort(v, (size_t)n, sizeof *v, cmp_d); return pct(v, n, 0.5); }

static int g_id;
static int open_pair(chan_t *c, int v, pid_t *pid) {       /* 채널 생성 + fork. 자식이면 0, 부모면 1 */
    chan_prepare(c, V_T[v], g_id++);
    if (chan_create(c) < 0) { perror("chan_create"); exit(1); }
    if (v == V_SOCK256) {
        int sz = 256 * 1024;
        for (int i = 0; i < 2; i++) {
            setsockopt(c->fd[i], SOL_SOCKET, SO_SNDBUF, &sz, sizeof sz);
            setsockopt(c->fd[i], SOL_SOCKET, SO_RCVBUF, &sz, sizeof sz);
        }
    }
    fflush(stdout);
    *pid = fork();
    if (*pid < 0) { perror("fork"); exit(1); }
    if (*pid == 0) { if (chan_attach(c, 1) < 0) _exit(2); return 0; }
    if (chan_attach(c, 0) < 0) { perror("attach"); exit(1); }
    return 1;
}

/* ---------------- rtt ---------------- */
static const uint32_t SZ[] = { 1, 64, 1024, 4096, 16384, 65536 };
#define NSZ ((int)(sizeof SZ / sizeof SZ[0]))
static uint8_t g_buf[1 << 20], g_buf2[1 << 20];

static void bench_rtt(int runs, int rounds) {
    double *lat = malloc(sizeof(double) * (size_t)rounds);
    double mins[V_N][NSZ][16], meds[V_N][NSZ][16], p99s[V_N][NSZ][16];
    for (int v = 0; v < V_N; v++)
        for (int r = 0; r < runs; r++) {
            chan_t c;
            pid_t pid;
            if (open_pair(&c, v, &pid) == 0) {                 /* 에코 서버: 크기 순서를 클라이언트와 같이 돈다 */
                for (int s = 0; s < NSZ; s++)
                    for (int k = 0; k < WARM + rounds; k++)
                        if (chan_recv(&c, g_buf, SZ[s]) != (ssize_t)SZ[s] || chan_send(&c, g_buf, SZ[s]) != (ssize_t)SZ[s]) _exit(3);
                chan_close(&c);
                _exit(0);
            }
            memset(g_buf, 0x5a, sizeof g_buf);
            for (int s = 0; s < NSZ; s++) {
                for (int k = 0; k < WARM + rounds; k++) {
                    uint64_t t0 = mach_absolute_time();
                    if (chan_send(&c, g_buf, SZ[s]) != (ssize_t)SZ[s] || chan_recv(&c, g_buf2, SZ[s]) != (ssize_t)SZ[s]) {
                        fprintf(stderr, "io error\n"); exit(1);
                    }
                    uint64_t t1 = mach_absolute_time();
                    if (k >= WARM) lat[k - WARM] = t2ns(t1 - t0);
                }
                qsort(lat, (size_t)rounds, sizeof *lat, cmp_d);
                mins[v][s][r] = lat[0]; meds[v][s][r] = pct(lat, rounds, 0.5); p99s[v][s][r] = pct(lat, rounds, 0.99);
            }
            chan_close(&c);
            int st;
            waitpid(pid, &st, 0);
            chan_force_cleanup(&c);
        }
    printf("RTT (ns) — %d runs x %d round trips per size (first %d discarded); median over runs of {min, median, p99}\n",
           runs, rounds, WARM);
    printf("%-9s", "size");
    for (int v = 0; v < V_N; v++) printf(" | %-24s", V_NAME[v]);
    printf("\n%-9s", "");
    for (int v = 0; v < V_N; v++) printf(" | %7s %7s %8s", "min", "med", "p99");
    printf("\n");
    for (int s = 0; s < NSZ; s++) {
        printf("%-9u", SZ[s]);
        for (int v = 0; v < V_N; v++) {
            double mn = mins[v][s][0];
            for (int r = 1; r < runs; r++) if (mins[v][s][r] < mn) mn = mins[v][s][r];
            printf(" | %7.0f %7.0f %8.0f", mn, median_of(meds[v][s], runs), median_of(p99s[v][s], runs));
        }
        printf("\n");
    }
    free(lat);
}

/* ---------------- bulk ---------------- */
static void bench_bulk(int runs, int mb, int chunk_kb) {
    size_t total = (size_t)mb << 20, chunk = (size_t)chunk_kb << 10;
    if (chunk > sizeof g_buf) chunk = sizeof g_buf;
    printf("bulk: %d MiB one way in %d KB writes, %d runs; MB/s with 1 MB = 10^6 B\n", mb, chunk_kb, runs);
    memset(g_buf, 0xa5, sizeof g_buf);
    for (int v = 0; v < V_N; v++) {
        double mbs[16];
        for (int r = 0; r < runs; r++) {
            chan_t c;
            pid_t pid;
            if (open_pair(&c, v, &pid) == 0) {                 /* 받기만 하는 서버 */
                uint64_t got = 0;
                for (;;) {
                    ssize_t n = chan_recv(&c, g_buf2, chunk);
                    if (n < 0) _exit(3);
                    got += (uint64_t)n;
                    if ((size_t)n < chunk) break;
                }
                chan_send(&c, &got, sizeof got);
                chan_close(&c);
                _exit(got == total ? 0 : 4);
            }
            uint64_t t0 = mach_absolute_time();
            for (size_t off = 0; off < total; off += chunk)
                if (chan_send(&c, g_buf, chunk) != (ssize_t)chunk) { fprintf(stderr, "send error\n"); exit(1); }
            chan_shutdown_write(&c);
            uint64_t ack = 0;
            if (chan_recv(&c, &ack, sizeof ack) != (ssize_t)sizeof ack || ack != total) { fprintf(stderr, "ack error\n"); exit(1); }
            uint64_t t1 = mach_absolute_time();
            chan_close(&c);
            int st;
            waitpid(pid, &st, 0);
            chan_force_cleanup(&c);
            mbs[r] = (double)total / 1e6 / (t2ns(t1 - t0) / 1e9);
        }
        qsort(mbs, (size_t)runs, sizeof *mbs, cmp_d);
        printf("%-9s median %8.0f MB/s   (min %8.0f, max %8.0f)   runs:", V_NAME[v], pct(mbs, runs, 0.5), mbs[0], mbs[runs - 1]);
        for (int r = 0; r < runs; r++) printf(" %.0f", mbs[r]);
        printf("\n");
    }
}

/* ---------------- reads: read() 한 번이 돌려주는 크기 ---------------- */
static void bench_reads(void) {
    const size_t W = 65536, TOTAL = 16u << 20;
    static const size_t edge[] = { 1024, 4096, 8192, 16384, 32768, 65535, 65536 };
    static const char *lab[] = { "<1K", "1K-4K", "4K-8K", "8K-16K", "16K-32K", "32K-64K", "=64K" };
    printf("reads: writer does write(fd, buf, 64 KB) x %zu; reader does read(fd, buf, 64 KB) and bins the return value\n", TOTAL / W);
    for (int v = V_PIPE; v <= V_SOCK256; v++) {
        if (v == V_FIFO) continue;                                /* FIFO = 이름 붙은 파이프, pipe 와 같다 */
        chan_t c;
        pid_t pid;
        if (open_pair(&c, v, &pid) == 0) {
            long shortw = 0;
            for (size_t off = 0; off < TOTAL; off += W) {
                ssize_t w = write(c.wfd, g_buf, W);                /* 일부러 write_all 이 아니라 write 한 번 */
                if (w < 0) _exit(3);
                if ((size_t)w < W) { shortw++; if (write_all(c.wfd, g_buf + w, W - (size_t)w) < 0) _exit(3); }
            }
            chan_close(&c);
            _exit(shortw > 255 ? 255 : (int)shortw);
        }
        long bins[7] = { 0 }, calls = 0;
        for (;;) {
            ssize_t n = read(c.rfd, g_buf2, W);
            if (n <= 0) break;
            calls++;
            int b = 0;
            while (b < 6 && (size_t)n > edge[b]) b++;
            if ((size_t)n < 1024) b = 0;
            bins[b]++;
        }
        chan_close(&c);
        int st;
        waitpid(pid, &st, 0);
        chan_force_cleanup(&c);
        printf("%-9s read() calls %6ld (ideal %zu), short writes %d |", V_NAME[v], calls, TOTAL / W,
               WIFEXITED(st) ? WEXITSTATUS(st) : -1);
        for (int b = 0; b < 7; b++) if (bins[b]) printf(" %s:%ld", lab[b], bins[b]);
        printf("\n");
    }
}

/* ---------------- pair: 블록 없는 보내기+받기 한 쌍 ---------------- */
static void bench_pair(int runs) {
    enum { N = 100000 };
    int p[2], s[2];
    char nm[32];
    snprintf(nm, sizeof nm, "/osb14.%d.pair", (int)getpid());
    sem_t *sem = sem_open(nm, O_CREAT | O_EXCL, 0600, 0);
    sem_unlink(nm);
    if (pipe(p) < 0 || socketpair(AF_UNIX, SOCK_STREAM, 0, s) < 0 || sem == SEM_FAILED) { perror("pair"); return; }
    const char *name[3] = { "pipe write+read", "sock write+read", "sem_post+sem_wait" };
    printf("pair: %d x %d ops in one process (never blocks), ns per pair, median of runs\n", runs, N);
    for (int k = 0; k < 3; k++) {
        double v[16];
        for (int r = 0; r < runs; r++) {
            char b = 1;
            uint64_t t0 = mach_absolute_time();
            for (int i = 0; i < N; i++) {
                if (k == 0) { if (write(p[1], &b, 1) != 1 || read(p[0], &b, 1) != 1) exit(1); }
                else if (k == 1) { if (write(s[0], &b, 1) != 1 || read(s[1], &b, 1) != 1) exit(1); }
                else { if (sem_post(sem) < 0 || sem_wait(sem) < 0) exit(1); }
            }
            v[r] = t2ns(mach_absolute_time() - t0) / N;
        }
        qsort(v, (size_t)runs, sizeof *v, cmp_d);
        printf("%-18s median %6.1f ns  (min %6.1f, max %6.1f)\n", name[k], pct(v, runs, 0.5), v[0], v[runs - 1]);
    }
    sem_close(sem);
    close(p[0]); close(p[1]); close(s[0]); close(s[1]);
}

int main(int argc, char **argv) {
    mach_timebase_info(&g_tb);
    signal(SIGPIPE, SIG_IGN);
    g_tag = getpid();
    mkdir("/tmp/OS-B14_work", 0700);
    snprintf(g_dir, sizeof g_dir, "/tmp/OS-B14_work/run.%d", (int)g_tag);
    mkdir(g_dir, 0700);
    const char *mode = argc > 1 ? argv[1] : "rtt";
    int a2 = argc > 2 ? atoi(argv[2]) : 5;
    if (a2 < 1) a2 = 1;
    if (a2 > 16) a2 = 16;
    if (strcmp(mode, "rtt") == 0) bench_rtt(a2, argc > 3 ? atoi(argv[3]) : 3000);
    else if (strcmp(mode, "bulk") == 0) bench_bulk(a2, argc > 3 ? atoi(argv[3]) : 256, argc > 4 ? atoi(argv[4]) : 64);
    else if (strcmp(mode, "reads") == 0) bench_reads();
    else if (strcmp(mode, "pair") == 0) bench_pair(a2);
    else fprintf(stderr, "usage: %s rtt|bulk|reads|pair ...\n", argv[0]);
    rmdir(g_dir);
    rmdir("/tmp/OS-B14_work");
    return 0;
}
