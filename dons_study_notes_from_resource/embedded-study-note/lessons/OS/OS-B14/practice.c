/* OS-B14 추가 연습 — 한 번에 검증
 *
 *   연습 1  메시지 경계: SOCK_STREAM 은 경계가 없고 SOCK_DGRAM 은 있다 (짧은 버퍼면 잘린다)
 *   연습 2  PIPE_BUF 원자성: 여러 writer 의 PIPE_BUF 바이트 레코드는 섞이지 않는다
 *   연습 3  FIFO 열기 규칙과 버퍼 용량 (pipe / FIFO / socketpair 기본 / SO_SNDBUF 256 KB)
 *   연습 4  이름은 프로세스보다 오래 산다: unlink 없이 죽은 프로세스의 shm·sem 이 남는다
 *   연습 5  close vs shutdown: fd 사본이 남아 있으면 close 로는 EOF 가 안 간다
 *
 * 블록할 수 있는 곳은 전부 O_NONBLOCK 이거나 자식에 alarm() 워치독이 있다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* 연습 3: 막힐 때까지(EAGAIN) 1 바이트씩 써서 커널 버퍼 용량을 잰다 */
static long capacity(int wfd) {
    int fl = fcntl(wfd, F_GETFL);
    fcntl(wfd, F_SETFL, fl | O_NONBLOCK);
    static char b[4096];
    long n = 0;
    ssize_t k;
    while ((k = write(wfd, b, 1)) == 1) n++;
    return (k < 0 && errno == EAGAIN) ? n : -1;
}

static void ex1_boundaries(void) {
    int s[2], d[2];
    char buf[100];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, s) < 0 || socketpair(AF_UNIX, SOCK_DGRAM, 0, d) < 0) { CHECK(0); return; }
    const char *m10 = "0123456789", *m20 = "abcdefghijklmnopqrst", *m30 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$";
    send(s[0], m10, 10, 0); send(s[0], m20, 20, 0); send(s[0], m30, 30, 0);
    send(d[0], m10, 10, 0); send(d[0], m20, 20, 0); send(d[0], m30, 30, 0);
    ssize_t r = recv(s[1], buf, sizeof buf, 0);
    printf("ex1 stream: one recv(100) -> %zd bytes (3 sends merged)\n", r);
    CHECK(r == 60 && memcmp(buf, m10, 10) == 0 && memcmp(buf + 30, m30, 30) == 0);   /* 경계가 사라졌다 */
    ssize_t a = recv(d[1], buf, sizeof buf, 0);
    ssize_t b = recv(d[1], buf, 5, 0);                 /* 20 바이트 데이터그램을 5 바이트 버퍼로 */
    bool b_ok = b == 5 && memcmp(buf, m20, 5) == 0;
    ssize_t c = recv(d[1], buf, sizeof buf, 0);        /* 나머지 15 바이트는 버려졌다 → 다음 데이터그램 */
    printf("ex1 dgram : recv -> %zd, %zd (truncated), %zd\n", a, b, c);
    CHECK(a == 10 && b_ok && c == 30 && memcmp(buf, m30, 30) == 0);
    close(s[0]); close(s[1]); close(d[0]); close(d[1]);
}

static void ex2_pipe_buf(void) {
    enum { W = 4, PER = 500 };
    int p[2];
    if (pipe(p) < 0) { CHECK(0); return; }
    fflush(stdout);
    pid_t kid[W];
    for (int i = 0; i < W; i++) {
        kid[i] = fork();
        if (kid[i] == 0) {
            alarm(5);
            close(p[0]);
            char rec[PIPE_BUF];
            memset(rec, 'A' + i, sizeof rec);
            for (int k = 0; k < PER; k++) if (write(p[1], rec, sizeof rec) != (ssize_t)sizeof rec) _exit(1);
            _exit(0);
        }
    }
    close(p[1]);                                       /* 내 쓰기 끝을 닫아야 writer 가 다 끝났을 때 EOF 가 온다 */
    static char all[W * PER * PIPE_BUF];
    size_t got = 0;
    ssize_t r;
    while (got < sizeof all && (r = read(p[0], all + got, sizeof all - got)) > 0) got += (size_t)r;
    close(p[0]);
    int mixed = 0, count[W] = { 0 };
    for (size_t off = 0; off + PIPE_BUF <= got; off += PIPE_BUF) {
        char c0 = all[off];
        for (size_t j = 1; j < PIPE_BUF; j++) if (all[off + j] != c0) { mixed++; break; }
        if (c0 >= 'A' && c0 < 'A' + W) count[c0 - 'A']++;
    }
    int ok = 0;
    for (int i = 0; i < W; i++) { int st; waitpid(kid[i], &st, 0); ok += WIFEXITED(st) && WEXITSTATUS(st) == 0; }
    printf("ex2 PIPE_BUF=%d: %zu bytes, mixed records %d, per writer %d %d %d %d\n",
           PIPE_BUF, got, mixed, count[0], count[1], count[2], count[3]);
    CHECK(ok == W && got == sizeof all && mixed == 0);
    CHECK(count[0] == PER && count[1] == PER && count[2] == PER && count[3] == PER);
}

static void ex3_fifo_and_capacity(void) {
    char path[96];
    mkdir("/tmp/OS-B14_work", 0700);
    snprintf(path, sizeof path, "/tmp/OS-B14_work/pr.%d.fifo", (int)getpid());
    unlink(path);
    if (mkfifo(path, 0600) < 0) { CHECK(0); return; }
    errno = 0;
    int w0 = open(path, O_WRONLY | O_NONBLOCK);        /* 읽는 쪽이 없으면 막히는 대신 ENXIO */
    CHECK(w0 == -1 && errno == ENXIO);
    int r = open(path, O_RDONLY | O_NONBLOCK);          /* 읽기 쪽은 쓰는 쪽이 없어도 바로 열린다 */
    int w = open(path, O_WRONLY | O_NONBLOCK);          /* 이제 읽는 쪽이 있으니 성공 */
    CHECK(r >= 0 && w >= 0);
    long fifo_cap = capacity(w);
    close(r); close(w); unlink(path);
    rmdir("/tmp/OS-B14_work");

    int p[2], s[2], s2[2];
    if (pipe(p) < 0 || socketpair(AF_UNIX, SOCK_STREAM, 0, s) < 0 || socketpair(AF_UNIX, SOCK_STREAM, 0, s2) < 0) { CHECK(0); return; }
    long pipe_cap = capacity(p[1]);
    long sock_cap = capacity(s[0]);
    int sz = 256 * 1024;
    setsockopt(s2[0], SOL_SOCKET, SO_SNDBUF, &sz, sizeof sz);
    setsockopt(s2[1], SOL_SOCKET, SO_RCVBUF, &sz, sizeof sz);
    long big_cap = capacity(s2[0]);
    printf("ex3 capacity: pipe %ld, fifo %ld, socketpair %ld, socketpair(256K) %ld bytes\n", pipe_cap, fifo_cap, sock_cap, big_cap);
    CHECK(pipe_cap >= PIPE_BUF && fifo_cap >= PIPE_BUF);  /* POSIX 가 보장하는 최소 */
    CHECK(sock_cap > 0 && big_cap > 4 * sock_cap);         /* 버퍼를 키우면 실제로 더 많이 담긴다 */
    close(p[0]); close(p[1]); close(s[0]); close(s[1]); close(s2[0]); close(s2[1]);
}

static void ex4_names_outlive(void) {
    char shm[32], sem[32];
    snprintf(shm, sizeof shm, "/osb14p.%d", (int)getpid());
    snprintf(sem, sizeof sem, "/osb14p.%d.s", (int)getpid());
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {                                    /* 만들고, 쓰고, unlink 없이 즉사 */
        alarm(5);
        int fd = shm_open(shm, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd < 0 || ftruncate(fd, 4096) < 0) _exit(1);
        int *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (p == MAP_FAILED) _exit(1);
        *p = 42;
        if (sem_open(sem, O_CREAT | O_EXCL, 0600, 3) == SEM_FAILED) _exit(1);
        kill(getpid(), SIGKILL);
        _exit(1);
    }
    int st;
    waitpid(pid, &st, 0);
    CHECK(WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL);
    int fd = shm_open(shm, O_RDONLY, 0);               /* 프로세스는 죽었지만 이름과 내용은 남아 있다 */
    int val = -1;
    if (fd >= 0) {
        int *p = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, 0);
        if (p != MAP_FAILED) { val = *p; munmap(p, 4096); }
        close(fd);
    }
    sem_t *s = sem_open(sem, 0);
    bool sem_left = s != SEM_FAILED;
    bool sem_val3 = sem_left && sem_trywait(s) == 0 && sem_trywait(s) == 0 && sem_trywait(s) == 0 && sem_trywait(s) == -1;
    if (sem_left) sem_close(s);
    printf("ex4 after SIGKILL: shm left=%s value=%d, sem left=%s\n", fd >= 0 ? "yes" : "no", val, sem_left ? "yes" : "no");
    CHECK(fd >= 0 && val == 42 && sem_left && sem_val3);
    CHECK(shm_unlink(shm) == 0 && sem_unlink(sem) == 0);   /* 누군가 지워야 한다 */
    errno = 0;
    CHECK(shm_open(shm, O_RDONLY, 0) == -1 && errno == ENOENT);
}

static void ex5_close_vs_shutdown(void) {
    int s[2];
    char b;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, s) < 0) { CHECK(0); return; }
    int copy = dup(s[0]);                              /* fork 로 물려준 사본과 같은 상황 */
    fcntl(s[1], F_SETFL, O_NONBLOCK);
    close(s[0]);                                       /* 사본(copy)이 남아 있다 → 소켓은 안 닫혔다 */
    errno = 0;
    ssize_t r1 = read(s[1], &b, 1);
    bool no_eof = r1 == -1 && errno == EAGAIN;
    shutdown(copy, SHUT_WR);                           /* 소켓 자체에 "더 안 보냄" — 사본 개수와 무관 */
    ssize_t r2 = read(s[1], &b, 1);
    printf("ex5 after close(one copy): read=%zd (%s); after shutdown(SHUT_WR): read=%zd\n",
           r1, no_eof ? "EAGAIN" : "?", r2);
    CHECK(no_eof);
    CHECK(r2 == 0);
    close(copy); close(s[1]);
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    ex1_boundaries();
    ex2_pipe_buf();
    ex3_fifo_and_capacity();
    ex4_names_outlive();
    ex5_close_vs_shutdown();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
