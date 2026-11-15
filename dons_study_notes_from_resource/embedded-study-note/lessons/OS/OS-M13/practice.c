/* ENV: linux */
/* BUILD: -pthread */
/* OS-M13 추가 연습 — 단일 스레드 ET 에코 서버 (이 파일만으로 완결)에 기능을 하나씩 얹는다.
 *   연습 1  ET 로 등록한 listen 소켓: accept 한 번 vs EAGAIN 까지 accept (연결이 백로그에 갇히는 버그)
 *   연습 2  배압(backpressure): 출력 버퍼가 HIWAT 를 넘으면 읽기를 멈추고, LOWAT 밑으로 내려가면 MOD 로 재개
 *   연습 3  반쪽 닫기(half-close): read()==0 이어도 남은 에코를 다 보내고 닫기
 *   연습 4  유휴 타임아웃: epoll_wait 의 timeout 을 가장 이른 마감까지로 → 놀고 있는 연결을 닫기
 * build (Lima VM): gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define READ_SZ 4096
#define MAX_FD  1024

typedef struct { int accept_loop; size_t hiwat, lowat; int halfclose, idle_ms, sndbuf; } cfg_t;
typedef struct { char *buf; size_t off, len, cap; } obuf_t;
typedef struct { int fd; uint32_t events; obuf_t out; int rd_paused, rd_eof; long last_ms; } conn_t;
typedef struct {
    cfg_t cfg;
    int lfd, epfd, wakefd, port, running;
    atomic_int stop;
    conn_t *conns[MAX_FD]; /* 서버 스레드만 만진다 */
    pthread_t th;
    atomic_long accepts, paused, resumed, max_out, idle_closed;
} server_t;

static long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}
static void sleep_ms(int ms) {
    struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
    while (nanosleep(&t, &t) != 0 && errno == EINTR) { }
}
static size_t pending(const conn_t *c) { return c->out.len - c->out.off; }
static int reserve(obuf_t *o, size_t need) {
    if (o->off == o->len) o->off = o->len = 0;
    if (o->cap - o->len >= need) return 0;
    if (o->off) { memmove(o->buf, o->buf + o->off, o->len - o->off); o->len -= o->off; o->off = 0; }
    if (o->cap - o->len >= need) return 0;
    size_t nc = o->cap ? o->cap : 8192;
    while (nc - o->len < need) nc *= 2;
    char *nb = realloc(o->buf, nc);
    if (!nb) return -1;
    o->buf = nb;
    o->cap = nc;
    return 0;
}
static int ep_ctl(server_t *s, int op, int fd, uint32_t ev) {
    struct epoll_event e;
    memset(&e, 0, sizeof e);
    e.events = ev;
    e.data.u64 = (uint64_t)fd;
    return epoll_ctl(s->epfd, op, fd, &e);
}

static void conn_close(server_t *s, conn_t *c) {
    s->conns[c->fd] = NULL;
    close(c->fd); /* close 가 epoll 에서도 뺀다 (이 fd 를 dup 한 곳이 없으므로) */
    free(c->out.buf);
    free(c);
}

/* 연습 1: accept_loop=0 이면 이벤트당 accept 한 번 (ET 에서 버그), 1 이면 EAGAIN 까지 */
static void do_accept(server_t *s) {
    do {
        int fd = accept4(s->lfd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) {
            if (errno == EINTR || errno == ECONNABORTED) continue;
            return;
        }
        if (fd >= MAX_FD) { close(fd); continue; }
        if (s->cfg.sndbuf) setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &s->cfg.sndbuf, sizeof s->cfg.sndbuf);
        conn_t *c = calloc(1, sizeof *c);
        if (!c) { close(fd); continue; }
        c->fd = fd;
        c->events = EPOLLIN | EPOLLET;
        c->last_ms = now_ms();
        s->conns[fd] = c;
        if (ep_ctl(s, EPOLL_CTL_ADD, fd, c->events) < 0) { conn_close(s, c); continue; }
        atomic_fetch_add(&s->accepts, 1);
    } while (s->cfg.accept_loop);
}

/* 연습 2: EAGAIN 까지 읽되 출력이 hiwat 이상 쌓이면 멈추고 rd_paused 를 남긴다. 1 / 0(EOF) / -1 */
static int drain(server_t *s, conn_t *c) {
    for (;;) {
        if (s->cfg.hiwat && pending(c) >= s->cfg.hiwat) {
            c->rd_paused = 1; /* ET: 커널엔 아직 데이터가 있을 수 있다 → 재개할 때 MOD 가 다시 알려 준다 */
            atomic_fetch_add(&s->paused, 1);
            return 1;
        }
        if (reserve(&c->out, READ_SZ) < 0) return -1;
        ssize_t n = read(c->fd, c->out.buf + c->out.len, READ_SZ);
        if (n > 0) {
            c->out.len += (size_t)n;
            if ((long)pending(c) > atomic_load(&s->max_out)) atomic_store(&s->max_out, (long)pending(c));
            continue;
        }
        if (n == 0) return 0;
        if (errno == EINTR) continue;
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? 1 : -1;
    }
}

static int flush(conn_t *c) {
    while (pending(c)) {
        ssize_t n = send(c->fd, c->out.buf + c->out.off, pending(c), MSG_NOSIGNAL);
        if (n > 0) { c->out.off += (size_t)n; continue; }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
        return -1;
    }
    c->out.off = c->out.len = 0;
    return 0;
}

static void on_event(server_t *s, conn_t *c, uint32_t ev) {
    int r = 1;
    if ((ev & (EPOLLIN | EPOLLHUP | EPOLLERR)) && !c->rd_paused && !c->rd_eof) {
        r = drain(s, c);
        c->last_ms = now_ms();
        if (r == 0 && s->cfg.halfclose) { c->rd_eof = 1; r = 1; } /* 연습 3: 상대가 쓰기만 닫았다 */
    }
    if (r > 0 && flush(c) < 0) r = -1;
    if (r > 0 && c->rd_paused && pending(c) <= s->cfg.lowat) { /* 연습 2: 재개 */
        c->rd_paused = 0;
        atomic_fetch_add(&s->resumed, 1);
    }
    if (r > 0 && c->rd_eof && pending(c) == 0) r = 0; /* 연습 3: 다 보냈으니 이제 닫는다 */
    if (r <= 0) { conn_close(s, c); return; }
    uint32_t want = EPOLLET | (c->rd_paused || c->rd_eof ? 0 : EPOLLIN) | (pending(c) ? EPOLLOUT : 0);
    if (want != c->events) { /* 재개 시 MOD 가 "지금 읽을 게 있나" 를 다시 본다 → 새 edge 가 없어도 이벤트 */
        c->events = want;
        if (ep_ctl(s, EPOLL_CTL_MOD, c->fd, want) < 0) conn_close(s, c);
    }
}

/* 연습 4: 가장 이른 유휴 마감까지 남은 ms (없으면 1000). 지난 것은 닫는다. */
static int idle_sweep(server_t *s) {
    if (!s->cfg.idle_ms) return 1000;
    long now = now_ms(), next = 1000;
    for (int fd = 0; fd < MAX_FD; fd++) {
        conn_t *c = s->conns[fd];
        if (!c) continue;
        long left = c->last_ms + s->cfg.idle_ms - now;
        if (left <= 0) { conn_close(s, c); atomic_fetch_add(&s->idle_closed, 1); continue; }
        if (left < next) next = left;
    }
    return (int)next;
}

static void *loop(void *arg) {
    server_t *s = arg;
    struct epoll_event ev[64];
    while (!atomic_load(&s->stop)) {
        int n = epoll_wait(s->epfd, ev, 64, idle_sweep(s));
        for (int i = 0; i < n; i++) {
            int fd = (int)ev[i].data.u64;
            if (fd == s->wakefd) atomic_store(&s->stop, 1);
            else if (fd == s->lfd) do_accept(s);
            else if (s->conns[fd]) on_event(s, s->conns[fd], ev[i].events);
        }
    }
    return NULL;
}

static server_t *server_create(cfg_t cfg) {
    server_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->cfg = cfg;
    s->lfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    s->epfd = epoll_create1(EPOLL_CLOEXEC);
    s->wakefd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    socklen_t al = sizeof a;
    if (bind(s->lfd, (struct sockaddr *)&a, sizeof a) < 0 || listen(s->lfd, 64) < 0 ||
        getsockname(s->lfd, (struct sockaddr *)&a, &al) < 0 || ep_ctl(s, EPOLL_CTL_ADD, s->wakefd, EPOLLIN) < 0 ||
        ep_ctl(s, EPOLL_CTL_ADD, s->lfd, EPOLLIN | EPOLLET) < 0) { /* listen 소켓도 ET */
        close(s->lfd);
        close(s->epfd);
        close(s->wakefd);
        free(s);
        return NULL;
    }
    s->port = ntohs(a.sin_port);
    return s;
}
static void server_run(server_t *s) { s->running = pthread_create(&s->th, NULL, loop, s) == 0; }
static void server_free(server_t *s) {
    uint64_t one = 1;
    if (write(s->wakefd, &one, sizeof one) < 0) perror("wake");
    if (s->running) pthread_join(s->th, NULL);
    for (int fd = 0; fd < MAX_FD; fd++) if (s->conns[fd]) conn_close(s, s->conns[fd]);
    close(s->lfd);
    close(s->epfd);
    close(s->wakefd);
    free(s);
}

/* ---------------- 클라이언트 도우미 ---------------- */
static unsigned char pat(size_t i) { return (unsigned char)(i * 7u + (i >> 9)); }
static int cli_connect(int port, int rcvbuf) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    if (rcvbuf) setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf);
    struct timeval tv = { 2, 0 }; /* 블로킹 send/recv 의 watchdog */
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons((uint16_t)port),
                             .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    if (connect(fd, (struct sockaddr *)&a, sizeof a) < 0) { close(fd); return -1; }
    return fd;
}
static size_t send_n(int fd, size_t n) { /* 블로킹, SO_SNDTIMEO 가 상한 */
    unsigned char b[4096];
    size_t s = 0;
    while (s < n) {
        size_t k = n - s < sizeof b ? n - s : sizeof b;
        for (size_t i = 0; i < k; i++) b[i] = pat(s + i);
        ssize_t w = send(fd, b, k, MSG_NOSIGNAL);
        if (w <= 0) break;
        s += (size_t)w;
    }
    return s;
}
/* EOF/오류/시간 초과까지 받으며 패턴을 검사. *eof = 정상 EOF 를 봤나 */
static long recv_check(int fd, int *eof) {
    unsigned char b[4096];
    size_t got = 0;
    *eof = 0;
    for (;;) {
        ssize_t r = recv(fd, b, sizeof b, 0);
        if (r == 0) { *eof = 1; break; }
        if (r < 0) { if (errno == EINTR) continue; break; }
        for (ssize_t i = 0; i < r; i++) if (b[i] != pat(got + (size_t)i)) return -1;
        got += (size_t)r;
    }
    return (long)got;
}

typedef struct { int fd; size_t n; size_t sent; } snd_t;
static void *sender(void *p) { snd_t *a = p; a->sent = send_n(a->fd, a->n); return NULL; }

/* 쓰는 스레드는 계속 보내고, 읽기는 delay_ms 뒤에야 시작 → 서버 출력 버퍼가 쌓인다 */
static long slow_reader_echo(int port, size_t n, int delay_ms) {
    int fd = cli_connect(port, 4096);
    if (fd < 0) return -1;
    snd_t a = { fd, n, 0 };
    pthread_t th;
    pthread_create(&th, NULL, sender, &a);
    sleep_ms(delay_ms);
    long got = 0;
    unsigned char b[4096];
    while ((size_t)got < n) {
        ssize_t r = recv(fd, b, sizeof b, 0);
        if (r <= 0) break;
        for (ssize_t i = 0; i < r; i++) if (b[i] != pat((size_t)got + (size_t)i)) { got = -1; break; }
        if (got < 0) break;
        got += r;
    }
    pthread_join(th, NULL);
    close(fd);
    return a.sent == n ? got : -2;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);

    printf("== 연습 1: ET listen 소켓 — accept 한 번 vs EAGAIN 까지 ==\n");
    for (int loopmode = 0; loopmode < 2; loopmode++) {
        server_t *s = server_create((cfg_t){ .accept_loop = loopmode });
        if (!s) return 1;
        int cl[9];
        for (int i = 0; i < 8; i++) cl[i] = cli_connect(s->port, 0); /* 루프가 돌기 전에 8 개가 백로그에 */
        server_run(s);
        sleep_ms(200);
        long a8 = atomic_load(&s->accepts);
        cl[8] = cli_connect(s->port, 0); /* 9 번째 연결 = 새 edge */
        sleep_ms(200);
        long a9 = atomic_load(&s->accepts);
        printf("  accept_loop=%d: 8 개 대기 → accepts %ld, 9 번째 연결 뒤 → %ld\n", loopmode, a8, a9);
        if (!loopmode) CHECK(a8 == 1 && a9 == 2); /* edge 하나에 accept 하나 → 7 개가 백로그에 갇힌다 */
        else CHECK(a8 == 8 && a9 == 9);
        for (int i = 0; i < 9; i++) if (cl[i] >= 0) close(cl[i]);
        server_free(s);
    }

    printf("== 연습 2: 배압 — HIWAT 64 KB / LOWAT 16 KB ==\n");
    {
        server_t *s = server_create((cfg_t){ .accept_loop = 1, .hiwat = 65536, .lowat = 16384, .sndbuf = 16384 });
        if (!s) return 1;
        server_run(s);
        long got = slow_reader_echo(s->port, 300000, 100);
        printf("  배압 켬: echo %ld B, paused %ld resumed %ld, 최대 출력 버퍼 %ld B\n", got,
               atomic_load(&s->paused), atomic_load(&s->resumed), atomic_load(&s->max_out));
        CHECK(got == 300000);
        CHECK(atomic_load(&s->paused) > 0 && atomic_load(&s->resumed) > 0);
        CHECK(atomic_load(&s->max_out) <= 65536 + READ_SZ); /* 멈춘 시점 + read 한 번 이하 */
        server_free(s);
        s = server_create((cfg_t){ .accept_loop = 1, .sndbuf = 16384 });
        if (!s) return 1;
        server_run(s);
        got = slow_reader_echo(s->port, 300000, 100);
        printf("  배압 끔: echo %ld B, 최대 출력 버퍼 %ld B\n", got, atomic_load(&s->max_out));
        CHECK(got == 300000 && atomic_load(&s->max_out) > 65536 + READ_SZ); /* 끄면 사용자 공간에 쌓인다 */
        server_free(s);
    }

    printf("== 연습 3: 반쪽 닫기 — 200000 B 보내고 shutdown(SHUT_WR), EOF 까지 읽기 ==\n");
    for (int hc = 0; hc < 2; hc++) {
        server_t *s = server_create((cfg_t){ .accept_loop = 1, .halfclose = hc, .sndbuf = 16384 });
        if (!s) return 1;
        server_run(s);
        int fd = cli_connect(s->port, 4096), eof = 0;
        size_t sent = fd >= 0 ? send_n(fd, 200000) : 0;
        if (fd >= 0) shutdown(fd, SHUT_WR);
        long got = fd >= 0 ? recv_check(fd, &eof) : -1;
        printf("  halfclose=%d: 보냄 %zu, 받음 %ld, 정상 EOF %d\n", hc, sent, got, eof);
        if (hc) CHECK(sent == 200000 && got == 200000 && eof);
        else CHECK(sent == 200000 && got < 200000); /* EOF 에 바로 닫으면 남은 에코를 잃는다 */
        if (fd >= 0) close(fd);
        server_free(s);
    }

    printf("== 연습 4: 유휴 타임아웃 200 ms ==\n");
    {
        server_t *s = server_create((cfg_t){ .accept_loop = 1, .idle_ms = 200 });
        if (!s) return 1;
        server_run(s);
        int idle = cli_connect(s->port, 0), busy = cli_connect(s->port, 0), eof = 0;
        long t0 = now_ms();
        int busy_ok = 1;
        unsigned char one = pat(0), back;
        for (int i = 0; i < 20 && busy >= 0; i++) { /* 30 ms 마다 1 B 왕복 = 600 ms 동안 바쁨 */
            if (send(busy, &one, 1, MSG_NOSIGNAL) != 1 || recv(busy, &back, 1, 0) != 1 || back != one) busy_ok = 0;
            sleep_ms(30);
        }
        long got = idle >= 0 ? recv_check(idle, &eof) : -1; /* 서버가 닫으면 EOF (SO_RCVTIMEO 2 s) */
        printf("  놀던 연결: 받음 %ld, EOF %d (%ld ms 경과) | 바쁜 연결 600 ms 동안 정상 %d | idle_closed %ld\n", got,
               eof, now_ms() - t0, busy_ok, atomic_load(&s->idle_closed));
        CHECK(got == 0 && eof);                /* 놀던 연결은 닫혔다 */
        CHECK(busy_ok);                        /* 바쁜 연결은 살아 있었다 */
        if (idle >= 0) close(idle);
        if (busy >= 0) close(busy);
        server_free(s);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
