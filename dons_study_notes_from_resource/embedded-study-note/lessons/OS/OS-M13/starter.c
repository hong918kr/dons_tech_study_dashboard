/* ENV: linux */
/* BUILD: -pthread */
/* OS-M13 epoll 에코 서버: level vs edge triggered
 *
 * OS-M12 의 kqueue 에코 서버를 리눅스 epoll 로 옮긴 축약판 (이 파일만으로 완결, Lima VM elinux 에서 빌드).
 *   Part A  LT(level-triggered) 단일 스레드 에코 서버. 이벤트당 read 한 번이면 충분하다.
 *           쓰기는 출력 버퍼 + "보낼 게 남았을 때만" EPOLLOUT  (TODO flush_out, set_interest)
 *   Part B  ET(EPOLLET): EAGAIN 까지 다 읽기 (TODO drain_read)
 *           vs 버그판 ET (이벤트당 read 한 번) → 두 조각 보내기로 멈춤을 매번 재현
 *   Part C  EPOLLONESHOT 워커 풀: 워커 4 개가 epfd 하나를 공유, 처리 후 EPOLL_CTL_MOD 로 재무장 (TODO pool_handle)
 *
 * 서버는 127.0.0.1 포트 0 (getsockname 으로 읽음), 자기 스레드에서 돌고 eventfd 로 멈춘다.
 * 클라이언트 스레드가 1 / 100 / 4096 / 65536 / 300000 B 를 보내고 바이트 단위로 비교한다.
 * 모든 대기에는 "진척 없음 시간 제한" 이 있어 틀린 코드는 멈추지 않고 FAIL 한다. 시간은 CHECK 하지 않는다.
 * demo_pool.c / demo_load.c 는 OSM13_LIB_ONLY 를 정의하고 이 파일을 #include 한다 (main 제외).
 *
 * build (Lima VM elinux): gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so
 */
#define _GNU_SOURCE /* accept4, SOCK_NONBLOCK, eventfd 등 리눅스 API 를 -std=c11 에서 보이게 */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
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

enum { M_LT, M_ET, M_ET_BUGGY, M_POOL };
static const char *const MODE_NAME[] = { "LT", "ET", "ET-buggy", "pool" };
#define READ_SZ     4096 /* read() 한 번에 읽는 최대 바이트 */
#define MAX_FD      4096 /* conns[] 크기 (demo_load 1000 연결 + 여유) */
#define MAX_WORKERS 8

typedef struct { char *buf; size_t off, len, cap; } obuf_t; /* 아직 못 보낸 바이트 = buf[off, len) */

typedef struct {
    int fd;
    uint32_t events; /* 단일 스레드 모드: 지금 epoll 에 등록된 관심 집합 (같으면 MOD 생략) */
    obuf_t out;      /* 에코할 바이트 */
    atomic_int busy;      /* Part C: 이 연결을 지금 처리 중인 워커 수 (0 아니면 1 이어야 한다) */
    atomic_int rearming;  /* Part C: 아직 EPOLL_CTL_MOD 에서 돌아오지 않은 이전 주인 수 (0 이어야 닫을 수 있다) */
} conn_t;

typedef struct {
    atomic_long wakeups; /* epoll_wait 가 이벤트를 들고 돌아온 횟수 */
    atomic_long events, reads, writes, write_eagain, mods, accepts, closes, overlaps;
} stats_t;

typedef struct server server_t;
struct server {
    int mode, nworkers, oneshot, sndbuf;
    int lfd, epfd, wakefd, port, nthreads;
    atomic_int stop;
    _Atomic(conn_t *) conns[MAX_FD];
    pthread_t th[MAX_WORKERS];
    stats_t st;
    void (*hook)(server_t *, conn_t *); /* Part C 강제 스케줄 자리. 스레드 시작 전에만 설정한다 */
};
#define ST(s, f)  atomic_fetch_add_explicit(&(s)->st.f, 1, memory_order_relaxed)
#define STV(s, f) atomic_load(&(s)->st.f)

/* ======================= 출력 버퍼 ======================= */
static inline int obuf_pending(const obuf_t *o) { return o->len > o->off; }

static int obuf_reserve(obuf_t *o, size_t need) { /* buf[len..] 에 need 바이트 자리 확보 */
    if (!obuf_pending(o)) o->off = o->len = 0;
    if (o->cap - o->len >= need) return 0;
    if (o->off > 0) {
        memmove(o->buf, o->buf + o->off, o->len - o->off);
        o->len -= o->off;
        o->off = 0;
        if (o->cap - o->len >= need) return 0;
    }
    size_t nc = o->cap ? o->cap : 8192;
    while (nc - o->len < need) nc *= 2;
    char *nb = realloc(o->buf, nc);
    if (!nb) return -1;
    o->buf = nb;
    o->cap = nc;
    return 0;
}

/* ======================= epoll 도우미 ======================= */
static uint32_t base_events(const server_t *s) { /* 연결 fd 의 기본 관심 집합 (EPOLLOUT 제외) */
    switch (s->mode) {
    case M_LT:   return EPOLLIN;
    case M_POOL: return s->oneshot ? EPOLLIN | EPOLLET | EPOLLONESHOT : EPOLLIN;
    default:     return EPOLLIN | EPOLLET; /* M_ET, M_ET_BUGGY */
    }
}

static int ep_ctl(server_t *s, int op, int fd, uint32_t events) {
    struct epoll_event e;
    memset(&e, 0, sizeof e);
    e.events = events;
    e.data.u64 = (uint64_t)fd; /* 이벤트에서 fd 를 꺼내 conns[fd] 로 찾는다 */
    return epoll_ctl(s->epfd, op, fd, &e);
}

/* 이벤트당 read 한 번. LT 에서는 정상 (남은 바이트가 있으면 epoll 이 또 알려 준다).
 * 반환 1 = 계속, 0 = EOF, -1 = 오류. 읽은 바이트는 곧바로 출력 버퍼에 쌓는다 (에코). */
static int read_once(server_t *s, conn_t *c) {
    if (obuf_reserve(&c->out, READ_SZ) < 0) return -1;
    ssize_t n = read(c->fd, c->out.buf + c->out.len, READ_SZ);
    if (n > 0) { c->out.len += (size_t)n; ST(s, reads); return 1; }
    if (n == 0) return 0;
    return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 1 : -1;
}

/* ======================= Part B: ET 는 EAGAIN 까지 ======================= */
/* TODO(B): 커널 수신 버퍼가 빌 때까지(EAGAIN) 읽어서 출력 버퍼에 쌓는다.
 *          반환 1 = EAGAIN 까지 다 읽음, 0 = EOF, -1 = 오류. EINTR 은 다시. */
static int drain_read(server_t *s, conn_t *c) {
    (void)s;
    (void)c;
    return 1; /* TODO(B) */
}

/* ======================= Part A: 쓰기 쪽 ======================= */
/* TODO(A1): 출력 버퍼를 send(MSG_NOSIGNAL) 로 보낼 수 있는 만큼 보낸다.
 *           EAGAIN → write_eagain 를 세고 0 (나머지는 EPOLLOUT 이 오면). 다 보냈으면 버퍼를 비운다.
 *           반환 0 = 정상, -1 = 연결 오류 (EPIPE, ECONNRESET ...). 성공한 send 마다 writes 를 센다. */
static int flush_out(server_t *s, conn_t *c) {
    (void)s;
    (void)c;
    return 0; /* TODO(A1) */
}

/* TODO(A2): 원하는 관심 집합 = base_events(s) | (보낼 게 남았으면 EPOLLOUT).
 *           지금 등록된 것(c->events)과 같으면 아무것도 안 한다 (syscall 아끼기 + ET 재무장 안 하기).
 *           다르면 c->events 를 먼저 갱신하고 EPOLL_CTL_MOD, mods 를 센다. 반환 0 / -1. */
static int set_interest(server_t *s, conn_t *c) {
    (void)s;
    (void)c;
    return 0; /* TODO(A2) */
}

/* ======================= 연결 수락 / 닫기 ======================= */
static void conn_close(server_t *s, conn_t *c) {
    atomic_store(&s->conns[c->fd], NULL); /* close 전에 지운다: 곧 같은 fd 번호가 재사용될 수 있다 */
    epoll_ctl(s->epfd, EPOLL_CTL_DEL, c->fd, NULL);
    close(c->fd);
    free(c->out.buf);
    free(c);
    ST(s, closes);
}

static void accept_all(server_t *s) { /* 대기 중인 연결을 EAGAIN 까지 전부 받는다 */
    for (;;) {
        int fd = accept4(s->lfd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) {
            if (errno == EINTR || errno == ECONNABORTED) continue;
            return; /* EAGAIN (다 받음) 또는 EMFILE 등 */
        }
        if (fd >= MAX_FD) { close(fd); continue; }
        if (s->sndbuf > 0) setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &s->sndbuf, sizeof s->sndbuf);
        conn_t *c = calloc(1, sizeof *c);
        if (!c) { close(fd); continue; }
        c->fd = fd;
        c->events = base_events(s);
        atomic_store(&s->conns[fd], c);
        if (ep_ctl(s, EPOLL_CTL_ADD, fd, c->events) < 0) {
            atomic_store(&s->conns[fd], NULL);
            close(fd);
            free(c);
            continue;
        }
        ST(s, accepts);
    }
}

/* ======================= 단일 스레드 루프 (LT / ET / ET-buggy) ======================= */
static void on_conn_event(server_t *s, conn_t *c, uint32_t ev) {
    int r = 1;
    if (ev & (EPOLLIN | EPOLLHUP | EPOLLERR))
        r = (s->mode == M_ET) ? drain_read(s, c) : read_once(s, c); /* LT 와 버그판 ET 는 read 한 번 */
    if (r > 0 && flush_out(s, c) < 0) r = -1; /* EPOLLOUT 이든 EPOLLIN 이든 일단 보내 본다 */
    if (r > 0 && set_interest(s, c) < 0) r = -1;
    if (r <= 0) conn_close(s, c);
}

static void *loop_single(void *arg) {
    server_t *s = arg;
    struct epoll_event ev[64];
    while (!atomic_load(&s->stop)) {
        int n = epoll_wait(s->epfd, ev, 64, 1000);
        if (n <= 0) continue; /* 시간 초과 또는 EINTR */
        ST(s, wakeups);
        atomic_fetch_add_explicit(&s->st.events, n, memory_order_relaxed);
        for (int i = 0; i < n; i++) {
            int fd = (int)ev[i].data.u64;
            if (fd == s->wakefd) { atomic_store(&s->stop, 1); continue; }
            if (fd == s->lfd) { accept_all(s); continue; }
            conn_t *c = atomic_load(&s->conns[fd]);
            if (c) on_conn_event(s, c, ev[i].events);
        }
    }
    return NULL;
}

/* ======================= Part C: EPOLLONESHOT 워커 풀 ======================= */
/* 여러 워커가 같은 epfd 에서 epoll_wait 한다. ONESHOT 이면 이벤트 하나가 나간 순간 그 fd 는 꺼진다
 * → 처리하는 워커가 그 연결을 독점한다. 처리가 끝나면 EPOLL_CTL_MOD 로 다시 켠다(재무장). */
static void pool_handle(server_t *s, conn_t *c, uint32_t ev) {
    if (atomic_fetch_add_explicit(&c->busy, 1, memory_order_acq_rel) != 0) { /* 다른 워커가 처리 중! */
        ST(s, overlaps);
        atomic_fetch_sub_explicit(&c->busy, 1, memory_order_acq_rel);
        return; /* ONESHOT 이면 절대 오지 않는 길 (demo_pool.c 가 ONESHOT 없이 세어 본다) */
    }
    /* TODO(C): ① EPOLLIN|HUP|ERR 이면 drain_read, 성공이면 flush_out
     *          ② s->hook 이 있으면 부른다 (테스트의 강제 스케줄: "처리 중" 에 멈추는 자리)
     *          ③ 실패/EOF 면: 이전 주인들의 재무장이 다 끝날 때까지(rearming == 0, acquire) 기다린 뒤 conn_close
     *          ④ fd 와 원하는 관심 집합(base_events | 남았으면 EPOLLOUT)을 지역 변수에 먼저 계산
     *          ⑤ rearming += 1, busy 를 내린다(release) — 이 뒤로 c 는 rearming 말고는 만지지 않는다
     *          ⑥ EPOLL_CTL_MOD 로 재무장(mods 를 센다), 그다음 rearming -= 1 (release) */
    (void)ev;
    atomic_fetch_sub_explicit(&c->busy, 1, memory_order_release); /* TODO(C) */
}

static void *pool_worker(void *arg) {
    server_t *s = arg;
    struct epoll_event ev[8];
    while (!atomic_load(&s->stop)) {
        int n = epoll_wait(s->epfd, ev, 8, 200);
        if (n <= 0) continue;
        ST(s, wakeups);
        atomic_fetch_add_explicit(&s->st.events, n, memory_order_relaxed);
        for (int i = 0; i < n; i++) {
            int fd = (int)ev[i].data.u64;
            if (fd == s->wakefd) { atomic_store(&s->stop, 1); continue; } /* LT 로 둬서 모든 워커가 본다 */
            if (fd == s->lfd) {
                accept_all(s);
                if (s->oneshot) ep_ctl(s, EPOLL_CTL_MOD, s->lfd, EPOLLIN | EPOLLONESHOT); /* 수락자도 한 명씩 */
                continue;
            }
            conn_t *c = atomic_load(&s->conns[fd]);
            if (c) pool_handle(s, c, ev[i].events);
        }
    }
    return NULL;
}

/* ======================= 서버 생성 / 시작 / 정지 ======================= */
static server_t *server_create(int mode, int nworkers, int sndbuf) {
    server_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->mode = mode;
    s->nworkers = mode == M_POOL ? (nworkers < 1 ? 1 : nworkers > MAX_WORKERS ? MAX_WORKERS : nworkers) : 1;
    s->oneshot = 1;
    s->sndbuf = sndbuf;
    s->lfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    s->epfd = epoll_create1(EPOLL_CLOEXEC);
    s->wakefd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0; /* 포트 0 = 커널이 빈 포트를 고른다 */
    socklen_t al = sizeof a;
    if (s->lfd < 0 || s->epfd < 0 || s->wakefd < 0 || bind(s->lfd, (struct sockaddr *)&a, sizeof a) < 0 ||
        listen(s->lfd, 1024) < 0 || getsockname(s->lfd, (struct sockaddr *)&a, &al) < 0 ||
        ep_ctl(s, EPOLL_CTL_ADD, s->wakefd, EPOLLIN) < 0 ||
        ep_ctl(s, EPOLL_CTL_ADD, s->lfd, mode == M_POOL ? EPOLLIN | EPOLLONESHOT : EPOLLIN) < 0) {
        if (s->lfd >= 0) close(s->lfd);
        if (s->epfd >= 0) close(s->epfd);
        if (s->wakefd >= 0) close(s->wakefd);
        free(s);
        return NULL;
    }
    s->port = ntohs(a.sin_port);
    return s;
}

static int server_run(server_t *s) { /* oneshot/hook 은 이 전에 정한다 */
    if (s->mode == M_POOL && !s->oneshot) ep_ctl(s, EPOLL_CTL_MOD, s->lfd, EPOLLIN);
    for (int i = 0; i < s->nworkers; i++) {
        if (pthread_create(&s->th[i], NULL, s->mode == M_POOL ? pool_worker : loop_single, s) != 0) return -1;
        s->nthreads++;
    }
    return 0;
}

static void server_join(server_t *s) { /* 워커가 끝나길 기다리고 모든 fd 를 닫는다 */
    for (int i = 0; i < s->nthreads; i++) pthread_join(s->th[i], NULL);
    s->nthreads = 0;
    for (int fd = 0; fd < MAX_FD; fd++) {
        conn_t *c = atomic_load(&s->conns[fd]);
        if (c) conn_close(s, c);
    }
    close(s->lfd);
    close(s->epfd);
    close(s->wakefd);
    s->lfd = s->epfd = s->wakefd = -1;
}

static void server_stop(server_t *s) {
    uint64_t one = 1;
    atomic_store(&s->stop, 1);
    if (write(s->wakefd, &one, sizeof one) != (ssize_t)sizeof one) perror("wake");
    server_join(s);
}

static void print_stats(const server_t *s) {
    printf("  [%s] wakeups %ld events %ld reads %ld writes %ld write_eagain %ld mods %ld accepts %ld overlaps %ld\n",
           MODE_NAME[s->mode], STV(s, wakeups), STV(s, events), STV(s, reads), STV(s, writes),
           STV(s, write_eagain), STV(s, mods), STV(s, accepts), STV(s, overlaps));
}

/* ======================= 테스트 클라이언트 ======================= */
#define PROGRESS_MS 300 /* 이만큼 아무 진척이 없으면 "멈췄다" 로 보고 포기 */
#define CHUNK 10000     /* Part B 두 조각 실험: READ_SZ 보다 큰 조각 */
static const size_t SIZES[] = { 1, 100, 4096, 65536, 300000 };
#define NSIZES 5

static void sleep_ms(int ms) {
    struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
    while (nanosleep(&t, &t) != 0 && errno == EINTR) { }
}
static unsigned char pat(int id, size_t i) { return (unsigned char)((unsigned)id * 131u + i * 7u + (i >> 9)); }

static int cli_connect(int port) {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) < 0) { close(fd); return -1; }
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    return fd;
}

/* tx 를 보내면서(lazy=0), 또는 다 보낸 뒤에야(lazy=1) 같은 길이를 돌려받는다.
 * 반환: 받은 바이트 수 (받은 부분이 tx 와 다르면 -1). PROGRESS_MS 동안 진척이 없으면 그때까지의 수. */
static long xfer(int fd, const unsigned char *tx, size_t len, int lazy) {
    unsigned char *rx = malloc(len ? len : 1);
    size_t sent = 0, got = 0;
    while (rx && got < len) {
        struct pollfd p = { fd, 0, 0 };
        if (sent < len) p.events |= POLLOUT;
        if (!lazy || sent == len) p.events |= POLLIN;
        int r = poll(&p, 1, PROGRESS_MS);
        if (r == 0) break;
        if (r < 0) { if (errno == EINTR) continue; break; }
        if (p.revents & POLLOUT) {
            ssize_t n = send(fd, tx + sent, len - sent, MSG_NOSIGNAL);
            if (n > 0) sent += (size_t)n;
            else if (n < 0 && errno != EAGAIN) break;
        }
        if (p.revents & (POLLIN | POLLHUP | POLLERR)) {
            ssize_t n = recv(fd, rx + got, len - got, 0);
            if (n > 0) got += (size_t)n;
            else if (n == 0 || errno != EAGAIN) break;
        }
    }
    long res = (rx && memcmp(rx, tx, got) == 0) ? (long)got : -1;
    free(rx);
    return res;
}

typedef struct { int port, id, ok; } cli_arg_t;
static void *cli_thread(void *arg) { /* 한 연결에서 5 가지 크기를 차례로 왕복 */
    cli_arg_t *a = arg;
    int fd = cli_connect(a->port), good = 0;
    unsigned char *tx = malloc(300000);
    for (int k = 0; fd >= 0 && tx && k < NSIZES && good == k; k++) { /* 하나 틀리면 그만 (starter 를 빨리 끝내려고) */
        for (size_t i = 0; i < SIZES[k]; i++) tx[i] = pat(a->id * 8 + k, i);
        good += xfer(fd, tx, SIZES[k], 0) == (long)SIZES[k];
    }
    a->ok = (good == NSIZES);
    free(tx);
    if (fd >= 0) close(fd);
    return NULL;
}

static int run_clients(int port, int n) { /* n 개 동시 연결 → 전부 바이트 일치한 클라이언트 수 */
    pthread_t th[64];
    cli_arg_t a[64];
    int ok = 0, started = 0;
    for (int i = 0; i < n && i < 64; i++, started++) {
        a[i] = (cli_arg_t){ port, i, 0 };
        if (pthread_create(&th[i], NULL, cli_thread, &a[i]) != 0) break;
    }
    for (int i = 0; i < started; i++) { pthread_join(th[i], NULL); ok += a[i].ok; }
    return ok;
}

static int send_all(int fd, const unsigned char *p, size_t len) {
    size_t s = 0;
    while (s < len) {
        struct pollfd q = { fd, POLLOUT, 0 };
        if (poll(&q, 1, PROGRESS_MS) <= 0) return -1;
        ssize_t n = send(fd, p + s, len - s, MSG_NOSIGNAL);
        if (n > 0) s += (size_t)n;
        else if (n < 0 && errno != EAGAIN && errno != EINTR) return -1;
    }
    return 0;
}

static size_t recv_upto(int fd, unsigned char *p, size_t len, int idle_ms) { /* idle_ms 동안 조용하면 멈춤 */
    size_t got = 0;
    while (got < len) {
        struct pollfd q = { fd, POLLIN, 0 };
        int r = poll(&q, 1, idle_ms);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        ssize_t n = recv(fd, p + got, len - got, 0);
        if (n > 0) got += (size_t)n;
        else if (n == 0 || (errno != EAGAIN && errno != EINTR)) break;
    }
    return got;
}

/* Part B 재현: CHUNK 바이트를 보내고, 30 ms 뒤 또 CHUNK, 그리고 2*CHUNK 에코를 기다린다 (300 ms 조용하면 멈춤).
 * after_poke != NULL 이면 멈춘 뒤 1 바이트를 더 보내 "새 edge" 로 얼마나 더 오는지 잰다. */
static long two_chunk_probe(int port, long *after_poke) {
    static unsigned char tx[2 * CHUNK + 1], rx[2 * CHUNK + 1]; /* main 스레드만 쓴다 */
    int fd = cli_connect(port);
    long got = -1, total = -1;
    if (fd < 0) return -1;
    for (size_t i = 0; i < sizeof tx; i++) tx[i] = pat(77, i);
    if (send_all(fd, tx, CHUNK) == 0) {
        sleep_ms(30);
        if (send_all(fd, tx + CHUNK, CHUNK) == 0) {
            got = (long)recv_upto(fd, rx, 2 * CHUNK, 300);
            total = got;
            if (after_poke && send_all(fd, tx + 2 * CHUNK, 1) == 0)
                total += (long)recv_upto(fd, rx + got, 2 * CHUNK + 1 - (size_t)got, 300);
            if (memcmp(rx, tx, (size_t)total) != 0) got = total = -1;
        }
    }
    if (after_poke) *after_poke = total;
    close(fd);
    return got;
}

typedef struct { long got, idle_wakeups; } lazy_res_t;
/* 300000 B 를 "다 보낸 뒤에야 읽는" 느린 클라이언트 → 서버는 부분 쓰기(EAGAIN)를 겪어야 한다.
 * 에코가 끝난 뒤 그 연결을 100 ms 놀려 두고 서버가 몇 번 깨어나는지 센다 (EPOLLOUT 을 껐으면 0). */
static lazy_res_t lazy_then_idle(server_t *s) {
    lazy_res_t r = { -1, -1 };
    unsigned char *tx = malloc(300000);
    int fd = cli_connect(s->port);
    if (fd >= 0 && tx) {
        for (size_t i = 0; i < 300000; i++) tx[i] = pat(99, i);
        r.got = xfer(fd, tx, 300000, 1);
        sleep_ms(20);
        long w0 = STV(s, wakeups);
        sleep_ms(100);
        r.idle_wakeups = STV(s, wakeups) - w0;
    }
    if (fd >= 0) close(fd);
    free(tx);
    return r;
}

static int count_fds(void) {
    int n = 0;
    for (int fd = 0; fd < 1024; fd++) n += fcntl(fd, F_GETFD) != -1;
    return n;
}

static int wait_flag(atomic_int *f, int ms) {
    for (int i = 0; i < ms; i++) {
        if (atomic_load(f)) return 1;
        sleep_ms(1);
    }
    return atomic_load(f) != 0;
}

/* 강제 스케줄: 처음으로 연결을 처리하는 워커가 "처리 중" 자리에서 g_gate_go 를 (최대 2 s) 기다린다 */
static atomic_int g_gate_at, g_gate_go, g_gate_used;
static void gate_hook(server_t *s, conn_t *c) {
    (void)s;
    (void)c;
    if (atomic_exchange(&g_gate_used, 1)) return;
    atomic_store(&g_gate_at, 1);
    wait_flag(&g_gate_go, 2000);
}

#ifndef OSM13_LIB_ONLY
/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    int fd0 = count_fds();
    lazy_res_t lz;

    printf("== Part A: LT 에코 서버 (EPOLLOUT 은 보낼 게 남았을 때만) ==\n");
    server_t *s = server_create(M_LT, 1, 16384); /* SO_SNDBUF 16 KB: 큰 메시지에서 부분 쓰기를 확실히 */
    CHECK(s != NULL && server_run(s) == 0 && s->port > 0);
    if (!s) return 1;
    CHECK(run_clients(s->port, 8) == 8);                   /* 8 동시 연결 × 5 크기, 바이트 일치 */
    lz = lazy_then_idle(s);
    CHECK(lz.got == 300000);                               /* 느린 클라이언트도 끝까지 */
    CHECK(STV(s, write_eagain) > 0 && STV(s, mods) >= 2);  /* 부분 쓰기 → EPOLLOUT 켜고 → 끄기 */
    CHECK(lz.idle_wakeups == 0);                           /* 다 보낸 뒤 EPOLLOUT 을 껐다 = 헛 깨어남 0 */
    CHECK(two_chunk_probe(s->port, NULL) == 2 * CHUNK);    /* LT 는 read 한 번이어도 남은 바이트를 또 알려 준다 */
    server_stop(s);
    print_stats(s);
    free(s);
    CHECK(count_fds() == fd0);                             /* fd 누수 없음 */

    printf("== Part B: ET — EAGAIN 까지 읽기 vs 이벤트당 read 한 번 ==\n");
    s = server_create(M_ET, 1, 16384);
    CHECK(s != NULL && server_run(s) == 0);
    if (!s) return 1;
    CHECK(run_clients(s->port, 8) == 8);
    lz = lazy_then_idle(s);
    CHECK(lz.got == 300000 && STV(s, write_eagain) > 0 && lz.idle_wakeups == 0);
    CHECK(two_chunk_probe(s->port, NULL) == 2 * CHUNK);    /* drain 하면 두 조각 모두 */
    server_stop(s);
    print_stats(s);
    free(s);

    s = server_create(M_ET_BUGGY, 1, 16384);
    CHECK(s != NULL && server_run(s) == 0);
    if (!s) return 1;
    long after = -1, got = two_chunk_probe(s->port, &after);
    printf("  ET-buggy: %d B x 2 보냄 → 에코 %ld B 에서 멈춤 (300 ms 조용), 1 B 더 보내니 → 누적 %ld B\n",
           CHUNK, got, after);
    CHECK(got >= 0 && got < 2 * CHUNK);                    /* 남은 바이트가 커널에 있는데 epoll 은 조용 */
    CHECK(after > got);                                    /* 새 데이터 = 새 edge → 그제서야 또 READ_SZ 만큼 */
    server_stop(s);
    print_stats(s);
    free(s);
    CHECK(count_fds() == fd0);

    printf("== Part C: EPOLLONESHOT 워커 풀 (워커 4, epfd 1 개 공유) ==\n");
    s = server_create(M_POOL, 4, 16384);
    CHECK(s != NULL && server_run(s) == 0);
    if (!s) return 1;
    CHECK(run_clients(s->port, 16) == 16);                 /* 16 동시 연결 × 5 크기 */
    lz = lazy_then_idle(s);
    CHECK(lz.got == 300000 && lz.idle_wakeups == 0);
    CHECK(STV(s, overlaps) == 0);                          /* 한 연결을 두 워커가 동시에 쥔 적 없음 */
    server_stop(s);
    print_stats(s);
    free(s);

    s = server_create(M_POOL, 4, 16384);
    if (!s) return 1;
    s->hook = gate_hook;                                   /* 첫 처리 워커를 "처리 중" 에 붙잡는다 */
    CHECK(server_run(s) == 0);
    unsigned char tx[2000], rx[2000];
    for (int i = 0; i < 2000; i++) tx[i] = pat(5, (size_t)i);
    int cfd = cli_connect(s->port);
    int sent_a = cfd >= 0 && send_all(cfd, tx, 1000) == 0;
    int at = wait_flag(&g_gate_at, 600);                   /* 워커 W1 이 A 를 읽고 붙잡혔다 */
    long e0 = STV(s, events);
    int sent_b = sent_a && send_all(cfd, tx + 1000, 1000) == 0; /* 그 사이 같은 fd 에 B 도착 */
    sleep_ms(100);                                         /* 틀린 구현이라면 다른 워커가 B 를 받을 시간 */
    long e1 = STV(s, events);
    atomic_store(&g_gate_go, 1);                           /* W1 놓아줌 → 재무장 → B 이벤트 */
    size_t n = cfd >= 0 ? recv_upto(cfd, rx, 2000, PROGRESS_MS) : 0;
    printf("  gate: 붙잡힌 동안 새 이벤트 %ld 개, 놓아준 뒤 에코 %zu/2000 B\n", e1 - e0, n);
    CHECK(at && sent_b && e1 == e0 && STV(s, overlaps) == 0); /* 꺼진 fd 는 아무 워커에게도 안 간다 */
    CHECK(n == 2000 && memcmp(rx, tx, 2000) == 0);         /* 재무장하자 B 가 왔다 (MOD 가 준비 상태를 다시 본다) */
    if (cfd >= 0) close(cfd);
    server_stop(s);
    print_stats(s);
    free(s);
    CHECK(count_fds() == fd0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif /* OSM13_LIB_ONLY */
