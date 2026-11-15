/* OS-A11 부하 데모 — 서버를 띄워 클라이언트 C 개가 닫힌 고리(closed loop)로 요청을 보낸다.
 *   GET 80 % / SET 20 %, 고정 시간 동안. 지연은 요청 왕복 전체(클라이언트 관점).
 *   ./demo_load ./so           기본 표: 워커 수 1/2/4/8 (write 만), 그다음 durability 1(fsync), 2(F_FULLFSYNC)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_load.c -o demo_load
 */
#define _DARWIN_C_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;
#define CLIENTS 8
#define MAX_SAMPLES 2000000

static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_MONOTONIC); }
static char g_sock[128];
static atomic_bool g_stop;

typedef struct { int id; uint64_t *lat; size_t n; uint32_t x; } cl_t;

static void *client(void *arg) {
    cl_t *c = arg;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof a.sun_path, "%s", g_sock);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0) return NULL;
    struct timeval tv = { 5, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    char buf[256], out[128];
    size_t have = 0;
    while (!atomic_load(&g_stop) && c->n < MAX_SAMPLES / CLIENTS) {
        c->x ^= c->x << 13; c->x ^= c->x >> 17; c->x ^= c->x << 5;
        unsigned key = c->x % 1000;
        int len;
        if (c->x % 5 == 0) len = snprintf(out, sizeof out, "SET k%u v%u\n", key, c->x & 0xFFFF);
        else               len = snprintf(out, sizeof out, "GET k%u\n", key);
        uint64_t t0 = now_ns();
        if (write(fd, out, (size_t)len) != len) break;
        for (;;) {
            char *nl = memchr(buf, '\n', have);
            if (nl) { size_t used = (size_t)(nl - buf) + 1; have -= used; memmove(buf, buf + used, have); break; }
            ssize_t r = read(fd, buf + have, sizeof buf - have);
            if (r <= 0) goto done;
            have += (size_t)r;
        }
        c->lat[c->n++] = now_ns() - t0;
    }
done:
    close(fd);
    return NULL;
}

static int cmp(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

static void write_cfg(const char *cfg, int durability) {
    FILE *f = fopen(cfg, "w");
    fprintf(f, "version=1\nmax_value=512\ndurability=%d\nalpha=1\nbeta=2\nname=load\n", durability);
    fclose(f);
}

static void run(const char *exe, const char *dir, int workers, int durability, int ms) {
    char logp[160], cfg[160], w[8];
    snprintf(logp, sizeof logp, "%s/wal.log", dir);
    snprintf(cfg, sizeof cfg, "%s/kv.cfg", dir);
    unlink(logp);
    write_cfg(cfg, durability);
    snprintf(w, sizeof w, "%d", workers);
    int p[2];
    if (pipe(p) != 0) return;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, p[1], 1);
    posix_spawn_file_actions_addclose(&fa, p[0]);
    posix_spawn_file_actions_addclose(&fa, p[1]);
    char *av[] = { (char *)exe, "--server", g_sock, logp, cfg, w, "64", "none", "2048", "5000", NULL };
    pid_t pid;
    if (posix_spawn(&pid, exe, &fa, NULL, av, environ) != 0) { perror("spawn"); return; }
    close(p[1]);
    char line[128];
    size_t n = 0;
    while (n + 1 < sizeof line && read(p[0], &line[n], 1) == 1 && line[n] != '\n') n++;

    atomic_store(&g_stop, false);
    pthread_t th[CLIENTS];
    cl_t cl[CLIENTS];
    for (int i = 0; i < CLIENTS; i++) {
        cl[i] = (cl_t){ i, malloc(sizeof(uint64_t) * (MAX_SAMPLES / CLIENTS)), 0, 2463534242u + (uint32_t)i * 977u };
        pthread_create(&th[i], NULL, client, &cl[i]);
    }
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
    atomic_store(&g_stop, true);
    for (int i = 0; i < CLIENTS; i++) pthread_join(th[i], NULL);

    size_t total = 0;
    for (int i = 0; i < CLIENTS; i++) total += cl[i].n;
    uint64_t *all = malloc(sizeof(uint64_t) * (total + 1));
    size_t k = 0;
    for (int i = 0; i < CLIENTS; i++) { memcpy(all + k, cl[i].lat, cl[i].n * sizeof(uint64_t)); k += cl[i].n; free(cl[i].lat); }
    qsort(all, total, sizeof(uint64_t), cmp);
    kill(pid, SIGTERM);
    int st;
    waitpid(pid, &st, 0);
    char bye[200] = "";
    n = 0;
    while (n + 1 < sizeof bye && read(p[0], &bye[n], 1) == 1 && bye[n] != '\n') n++;
    bye[n] = '\0';
    close(p[0]);
    posix_spawn_file_actions_destroy(&fa);
    printf("| %d | %d | %8.0f | %7.1f | %7.1f | %8.1f | %s |\n", workers, durability,
           (double)total / ((double)ms / 1000.0),
           total ? (double)all[total / 2] / 1000.0 : 0, total ? (double)all[total * 99 / 100] / 1000.0 : 0,
           total ? (double)all[total - 1] / 1000.0 : 0, WIFEXITED(st) && WEXITSTATUS(st) == 0 ? "exit 0" : "exit !=0");
    free(all);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s ./so\n", argv[0]); return 2; }
    signal(SIGPIPE, SIG_IGN);
    char dir[] = "/tmp/osa11load.XXXXXX";
    if (!mkdtemp(dir)) return 2;
    snprintf(g_sock, sizeof g_sock, "%s/s.sock", dir);
    printf("클라이언트 %d 개, GET 80%% / SET 20%%, 시간 단위 us (왕복, 클라이언트에서 측정)\n", CLIENTS);
    printf("| workers | durability | ops/s | p50 us | p99 us | max us | 종료 |\n|---|---|---|---|---|---|---|\n");
    for (int w = 1; w <= 8; w *= 2) run(argv[1], dir, w, 0, 1500);
    run(argv[1], dir, 4, 1, 1500);
    run(argv[1], dir, 4, 2, 1500);
    char a[200], b[200];
    snprintf(a, sizeof a, "%s/wal.log", dir); unlink(a);
    snprintf(b, sizeof b, "%s/kv.cfg", dir); unlink(b);
    rmdir(dir);
    return 0;
}
