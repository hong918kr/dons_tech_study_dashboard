/* OS-M12 추가 연습 — 한 번에 검증 */
/* BUILD: -pthread */
/*
 *   연습 1  poll() 에코 루프: FD_SETSIZE(1024) 를 넘는 fd 도 처리한다 (select 와 같은 O(n), 상한만 없음)
 *   연습 2  줄 단위 프레이밍: TCP 는 바이트 스트림 → 메시지 경계는 앱이 만든다 (UART AT 명령 파서와 같은 문제)
 *   연습 3  kqueue EVFILT_USER 로 루프 깨우기(파이프 없이) + EVFILT_TIMER 주기 틱
 *   연습 4  플랫폼 사실 고정 테스트: select(nfds > 1024) → EINVAL, accept 가 O_NONBLOCK 상속, 끊긴 소켓 쓰기 → EPIPE
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice
 */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static int64_t now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static void sleep_ms(int ms) { struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L }; nanosleep(&ts, NULL); }
static int set_nonblock(int fd, bool on) {
    int fl = fcntl(fd, F_GETFL);
    return fl < 0 ? -1 : fcntl(fd, F_SETFL, on ? (fl | O_NONBLOCK) : (fl & ~O_NONBLOCK));
}
static int make_listener(int *port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0), one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    socklen_t al = sizeof a;
    if (bind(fd, (struct sockaddr *)&a, sizeof a) || listen(fd, 128) || getsockname(fd, (struct sockaddr *)&a, &al)) {
        close(fd);
        return -1;
    }
    set_nonblock(fd, true);
    *port = ntohs(a.sin_port);
    return fd;
}
static int connect_to(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) < 0) { close(fd); return -1; }   /* 루프백: 바로 끝난다 */
    set_nonblock(fd, true);
    return fd;
}
static int count_open_fds(void) {
    int n = 0;
    for (int fd = 0; fd < 4096; fd++) n += fcntl(fd, F_GETFD) != -1;
    return n;
}

/* ===================== 연습 1 — poll() 에코 루프 ===================== */
enum { CH = 16384, MAXC = 64 };
typedef struct { int fd; size_t off, len; unsigned char buf[CH]; } pconn_t;
typedef struct { int lfd, port, wake[2]; pthread_t th; long served, max_fd; } psrv_t;

static int pc_flush(pconn_t *c) {                       /* 0 = 다 씀, 1 = EAGAIN(쓰기 대기), -1 = 닫기 */
    while (c->off < c->len) {
        ssize_t w = write(c->fd, c->buf + c->off, c->len - c->off);
        if (w < 0) return (errno == EAGAIN || errno == EINTR) ? 1 : -1;
        c->off += (size_t)w;
    }
    c->off = c->len = 0;
    return 0;
}
static void *poll_server(void *arg) {
    psrv_t *s = arg;
    pconn_t *cs[MAXC] = { 0 };
    struct pollfd pf[MAXC + 2];
    for (;;) {
        int n = 0;
        pf[n++] = (struct pollfd){ s->wake[0], POLLIN, 0 };
        pf[n++] = (struct pollfd){ s->lfd, POLLIN, 0 };
        int idx[MAXC];
        for (int i = 0; i < MAXC; i++)
            if (cs[i]) {                                /* 원하는 방향 한 쪽만: 출력 대기면 POLLOUT */
                idx[n - 2] = i;
                pf[n++] = (struct pollfd){ cs[i]->fd, cs[i]->len ? POLLOUT : POLLIN, 0 };
            }
        if (poll(pf, (nfds_t)n, -1) < 0) { if (errno == EINTR) continue; break; }
        if (pf[0].revents) break;
        for (int k = 2; k < n; k++) {
            pconn_t *c = cs[idx[k - 2]];
            if (!pf[k].revents) continue;
            int r = 0;
            if (c->len) r = pc_flush(c);
            else {
                ssize_t got = read(c->fd, c->buf, CH);
                if (got > 0) { c->len = (size_t)got; r = pc_flush(c); }
                else if (got == 0 || errno != EAGAIN) r = -1;
            }
            if (r < 0) { close(c->fd); free(c); cs[idx[k - 2]] = NULL; }
        }
        if (pf[1].revents & POLLIN) {
            int fd;
            while ((fd = accept(s->lfd, NULL, NULL)) >= 0) {
                int slot = -1;
                for (int i = 0; i < MAXC && slot < 0; i++) if (!cs[i]) slot = i;
                if (slot < 0) { close(fd); continue; }
                set_nonblock(fd, true);
                cs[slot] = calloc(1, sizeof(pconn_t));
                cs[slot]->fd = fd;
                s->served++;
                if (fd > s->max_fd) s->max_fd = fd;
            }
        }
    }
    for (int i = 0; i < MAXC; i++) if (cs[i]) { close(cs[i]->fd); free(cs[i]); }
    return NULL;
}
static bool echo_once(int fd, size_t n, int seed) {        /* 보내면서 읽는다, 2 초 마감 */
    unsigned char *m = malloc(n), *r = malloc(n);
    for (size_t i = 0; i < n; i++) m[i] = (unsigned char)(i * 7 + (size_t)seed + (i >> 8));
    size_t s = 0, g = 0;
    int64_t dl = now_ms() + 2000;
    while (g < n && now_ms() < dl) {
        struct pollfd p = { fd, (short)(POLLIN | (s < n ? POLLOUT : 0)), 0 };
        poll(&p, 1, 10);
        if (s < n && (p.revents & POLLOUT)) { ssize_t w = write(fd, m + s, n - s); if (w > 0) s += (size_t)w; }
        if (p.revents & POLLIN) { ssize_t k = read(fd, r + g, n - g); if (k > 0) g += (size_t)k; else if (k == 0) break; }
    }
    bool ok = g == n && memcmp(m, r, n) == 0;
    free(m);
    free(r);
    return ok;
}
static void ex1_poll_beyond_fdsetsize(void) {
    printf("== 연습 1: poll() beyond FD_SETSIZE ==\n");
    int fds0 = count_open_fds();
    psrv_t s;
    memset(&s, 0, sizeof s);
    s.lfd = make_listener(&s.port);
    CHECK(s.lfd >= 0 && pipe(s.wake) == 0);
    pthread_create(&s.th, NULL, poll_server, &s);
    int dups[FD_SETSIZE + 8], nd = 0, fd;
    while (nd < FD_SETSIZE + 8 && (fd = open("/dev/null", O_RDONLY)) >= 0) {
        dups[nd++] = fd;
        if (fd >= FD_SETSIZE - 1) break;
    }
    int c = connect_to(s.port);
    CHECK(c >= FD_SETSIZE);
    CHECK(c >= 0 && echo_once(c, 1, 1) && echo_once(c, 65536, 2) && echo_once(c, 300000, 3));
    if (c >= 0) close(c);
    for (int i = 0; i < nd; i++) close(dups[i]);
    ssize_t w = write(s.wake[1], "q", 1);
    (void)w;
    pthread_join(s.th, NULL);
    printf("  client fd=%d, server max accepted fd=%ld\n", c, s.max_fd);
    CHECK(s.served == 1 && s.max_fd >= FD_SETSIZE);
    close(s.lfd); close(s.wake[0]); close(s.wake[1]);
    CHECK(count_open_fds() == fds0);
}

/* ===================== 연습 2 — 줄 단위 프레이밍 ===================== */
enum { LF_MAX = 32 };
typedef struct {
    char line[LF_MAX + 1];
    size_t len;
    bool overflow;               /* 너무 긴 줄: 다음 '\n' 까지 버린다 */
    int nlines, nerr;
    char out[8][LF_MAX + 1];
} lf_t;
/* 들어온 조각을 먹고, 완성된 줄('\n' 기준, 끝의 '\r' 제거)을 out 에 쌓는다. 너무 긴 줄은 nerr++ 후 버림 */
static void lf_feed(lf_t *f, const char *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char ch = p[i];
        if (ch == '\n') {
            if (f->overflow) f->nerr++;
            else {
                size_t L = f->len;
                if (L && f->line[L - 1] == '\r') L--;
                if (f->nlines < 8) { memcpy(f->out[f->nlines], f->line, L); f->out[f->nlines][L] = 0; }
                f->nlines++;
            }
            f->len = 0;
            f->overflow = false;
        } else if (!f->overflow) {
            if (f->len == LF_MAX) f->overflow = true;
            else f->line[f->len++] = ch;
        }
    }
}
static void ex2_line_framing(void) {
    printf("== 연습 2: line framing over a byte stream ==\n");
    const char *s = "AT\r\nAT+GMR\r\nOK\n";
    size_t n = strlen(s);
    bool all = true;
    for (size_t a = 0; a <= n; a++)                       /* 모든 2-분할 · 3-분할 위치에서 같은 결과 */
        for (size_t b = a; b <= n; b++) {
            lf_t f;
            memset(&f, 0, sizeof f);
            lf_feed(&f, s, a);
            lf_feed(&f, s + a, b - a);
            lf_feed(&f, s + b, n - b);
            all = all && f.nlines == 3 && !strcmp(f.out[0], "AT") && !strcmp(f.out[1], "AT+GMR") &&
                  !strcmp(f.out[2], "OK") && f.len == 0;
        }
    CHECK(all);
    lf_t f;
    memset(&f, 0, sizeof f);
    lf_feed(&f, "ab", 2);
    CHECK(f.nlines == 0 && f.len == 2);                   /* 줄이 안 끝났으면 아직 아무것도 안 나간다 */
    char longl[80];
    memset(longl, 'x', sizeof longl);
    lf_feed(&f, longl, sizeof longl);
    lf_feed(&f, "\nOK\n", 4);
    CHECK(f.nerr == 1 && f.nlines == 1 && !strcmp(f.out[0], "OK"));
    /* 실제 소켓: 한 바이트씩 보내도 줄은 그대로 */
    int sv[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    for (size_t i = 0; i < n; i++) { ssize_t w = write(sv[0], s + i, 1); (void)w; }
    close(sv[0]);
    lf_t g;
    memset(&g, 0, sizeof g);
    char buf[5];
    ssize_t k;
    while ((k = read(sv[1], buf, sizeof buf)) > 0) lf_feed(&g, buf, (size_t)k);
    close(sv[1]);
    CHECK(g.nlines == 3 && !strcmp(g.out[1], "AT+GMR"));
}

/* ===================== 연습 3 — EVFILT_USER / EVFILT_TIMER ===================== */
static int g_kq;
static void *trigger_later(void *arg) {
    (void)arg;
    sleep_ms(50);
    struct kevent ev;
    EV_SET(&ev, 1, EVFILT_USER, 0, NOTE_TRIGGER, 0, NULL);
    kevent(g_kq, &ev, 1, NULL, 0, NULL);
    return NULL;
}
static void ex3_user_and_timer(void) {
    printf("== 연습 3: EVFILT_USER wake + EVFILT_TIMER ==\n");
    g_kq = kqueue();
    struct kevent ev, out[4];
    EV_SET(&ev, 1, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, NULL);
    CHECK(kevent(g_kq, &ev, 1, NULL, 0, NULL) == 0);
    pthread_t th;
    pthread_create(&th, NULL, trigger_later, NULL);
    struct timespec t2 = { 2, 0 }, t0 = { 0, 0 };
    int n = kevent(g_kq, NULL, 0, out, 4, &t2);          /* 파이프 없이 다른 스레드가 깨운다 */
    pthread_join(th, NULL);
    CHECK(n == 1 && out[0].filter == EVFILT_USER && out[0].ident == 1);
    CHECK(kevent(g_kq, NULL, 0, out, 4, &t0) == 0);      /* EV_CLEAR: 한 번 받으면 리셋 */
    EV_SET(&ev, 7, EVFILT_TIMER, EV_ADD, 0, 20, NULL);    /* data = 20 ms (기본 단위 ms), 주기 타이머 */
    CHECK(kevent(g_kq, &ev, 1, NULL, 0, NULL) == 0);
    int ticks = 0;
    int64_t dl = now_ms() + 2000;
    while (ticks < 3 && now_ms() < dl) {
        n = kevent(g_kq, NULL, 0, out, 4, &t2);
        for (int i = 0; i < n; i++)
            if (out[i].filter == EVFILT_TIMER) ticks += (int)out[i].data;   /* data = 지난 만료 횟수 */
    }
    CHECK(ticks >= 3);
    close(g_kq);
}

/* ===================== 연습 4 — 플랫폼 사실 고정 ===================== */
static void ex4_platform_facts(void) {
    printf("== 연습 4: platform facts (macOS) ==\n");
    fd_set rs;
    FD_ZERO(&rs);
    struct timeval tv = { 0, 0 };
    errno = 0;
    int r = select(FD_SETSIZE + 1, &rs, NULL, NULL, &tv);   /* strict POSIX 빌드 → 제한 있는 select */
    CHECK(r == -1 && errno == EINVAL);
    int port, l = make_listener(&port);                    /* listen 소켓은 O_NONBLOCK */
    int c = connect_to(port);
    struct pollfd p = { l, POLLIN, 0 };
    poll(&p, 1, 1000);
    int a = accept(l, NULL, NULL);
    CHECK(a >= 0 && (fcntl(a, F_GETFL) & O_NONBLOCK));     /* macOS(BSD): 상속. Linux accept 는 상속 안 함 */
    close(c);                                              /* 상대가 닫음 → 첫 쓰기는 RST 를 부르고, 다음 쓰기는 EPIPE */
    int err = 0;
    int64_t dl = now_ms() + 1000;
    while (!err && now_ms() < dl) {
        if (write(a, "x", 1) < 0 && errno != EAGAIN) err = errno;
        else sleep_ms(5);
    }
    printf("  write after peer close -> %s\n", strerror(err));
    CHECK(err == EPIPE || err == ECONNRESET);              /* SIGPIPE 를 무시했으므로 죽지 않는다 */
    close(a);
    close(l);
}

int main(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, NULL);
    struct rlimit rl;
    getrlimit(RLIMIT_NOFILE, &rl);
    if (rl.rlim_cur < 4096) { rl.rlim_cur = rl.rlim_max < 4096 ? rl.rlim_max : 4096; setrlimit(RLIMIT_NOFILE, &rl); }
    ex1_poll_beyond_fdsetsize();
    ex2_line_framing();
    ex3_user_and_timer();
    ex4_platform_facts();
    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
