/* OS-M12 I/O 모델 비교: blocking·non-blocking·select·kqueue 에코 서버 */
/* BUILD: -pthread */
/*
 * 같은 TCP 에코 서버(받은 바이트를 그대로 돌려줌)를 세 가지 I/O 모델로 만든다. 한 파일에 서버 3개 + 테스트.
 *   Part A  연결당 스레드(thread-per-connection): 블로킹 read → write_all. 스레드가 곧 연결 상태다.
 *           TODO A1 write_all (짧은 쓰기 · EINTR), TODO A2 thread_conn_loop (EOF 까지 read → write_all).
 *   Part B  논블로킹 + select: 연결 상태를 직접 들고 다닌다 (conn_t = 대기 출력 버퍼 하나).
 *           TODO B1 conn_flush (EAGAIN 이면 남은 출력을 들고 "쓰기 대기"), B2 conn_on_readable (역압 포함),
 *           TODO B3 sel_fd_ok (FD_SETSIZE = 1024 가드), B4 sel_build_sets (연결마다 원하는 방향 한 쪽만).
 *   Part C  kqueue: EVFILT_READ / EVFILT_WRITE 중 지금 원하는 쪽만 켠다. 레벨(기본) 모드와 EV_CLEAR 모드.
 *           TODO C1 kq_want (관심 방향 바꾸기, 같으면 시스템 콜 0), TODO C2 kq_on_event (이벤트 → 처리기).
 *
 * 하네스(주어짐): 127.0.0.1 포트 0 + getsockname, 서버는 자기 스레드에서 돌고 wake 파이프로 멈춘다.
 *   클라이언트 스레드 64 개가 {1, 100, 4096, 65536, 300000} 바이트를 보내며 동시에 읽고, 바이트 단위로 비교한다.
 *   300000 은 소켓 버퍼보다 크다 → 서버는 부분 쓰기 / EAGAIN 을 만나 출력을 들고 기다려야 한다.
 *   모든 대기에는 마감 시각이 있어서 시작 코드도 몇 초 안에 FAIL 로 끝난다. fd 누수는 fcntl(F_GETFD) 로 센다.
 *   시간 측정(처리량 · RTT · RSS)은 demo_load.c 에서만 한다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so   (starter.c 도 같은 명령)
 * 데모가 서버만 쓸 때: #define ECHO_NO_MAIN 후 #include "solution.c"
 */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
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
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define GIVEN __attribute__((unused))

/* ======================= 공통 도우미 (주어짐) ======================= */
static int64_t now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static int set_nonblock(int fd, bool on) {
    int fl = fcntl(fd, F_GETFL);
    if (fl < 0) return -1;
    return fcntl(fd, F_SETFL, on ? (fl | O_NONBLOCK) : (fl & ~O_NONBLOCK));
}
/* 127.0.0.1 포트 0 → 커널이 고른 포트를 getsockname 으로 읽는다. 고정 포트는 절대 쓰지 않는다. */
static int make_listener(int *port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = 0;
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);   /* INADDR_LOOPBACK 은 strict POSIX 빌드에서 안 보인다 */
    socklen_t al = sizeof a;
    if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0 || listen(fd, 1024) < 0 ||  /* macOS 는 128 로 잘림 */
        getsockname(fd, (struct sockaddr *)&a, &al) < 0 || set_nonblock(fd, true) < 0) {
        close(fd);
        return -1;
    }
    *port = ntohs(a.sin_port);
    return fd;
}
GIVEN static int count_open_fds(void) {
    struct rlimit rl;
    getrlimit(RLIMIT_NOFILE, &rl);
    int lim = rl.rlim_cur > 65536 ? 65536 : (int)rl.rlim_cur, n = 0;
    for (int fd = 0; fd < lim; fd++)
        if (fcntl(fd, F_GETFD) != -1) n++;
    return n;
}
GIVEN static void raise_nofile(rlim_t want) {
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) != 0 || rl.rlim_cur >= want) return;
    rl.rlim_cur = (rl.rlim_max != RLIM_INFINITY && rl.rlim_max < want) ? rl.rlim_max : want;
    setrlimit(RLIMIT_NOFILE, &rl);
}

/* ======================= 서버 상태 · 통계 (주어짐) ======================= */
typedef struct {
    _Atomic long accepted, rejected, closed, live, max_live;
    _Atomic long bytes_in, bytes_out, reads, writes, short_writes, eagain, kq_changes, loops;
} srv_stats_t;
static void st_add(_Atomic long *a, long v) { atomic_fetch_add_explicit(a, v, memory_order_relaxed); }
static long st_get(_Atomic long *a) { return atomic_load_explicit(a, memory_order_relaxed); }
static void st_live(srv_stats_t *st, long d) {
    long v = atomic_fetch_add(&st->live, d) + d, m = atomic_load(&st->max_live);
    while (v > m && !atomic_compare_exchange_weak(&st->max_live, &m, v)) {}
}

enum { CHUNK = 16384 };                                      /* 한 번에 읽는 양 = 연결당 출력 버퍼 크기 */
enum { NEXT_NONE = -1, NEXT_READ = 0, NEXT_WRITE = 1, NEXT_CLOSE = 2 };
typedef struct conn {
    int fd;
    int want;                  /* NEXT_READ: 읽기 대기, NEXT_WRITE: 대기 출력 있음 (읽기 멈춤) */
    size_t off, len;           /* 대기 출력 = buf[off, len) */
    struct conn *prev, *next;
    unsigned char buf[CHUNK];
} conn_t;

enum { MODEL_THREAD, MODEL_SELECT, MODEL_KQUEUE };
typedef struct { int model; bool kq_clear; int sndbuf; } srv_cfg_t;   /* sndbuf 0 = 커널 기본 */
typedef struct trec { pthread_t th; int fd; bool started; struct server *srv; } trec_t;
typedef struct server {
    srv_cfg_t cfg;
    int lfd, port, wake[2];
    pthread_t th;
    srv_stats_t st;
    conn_t *head;                         /* select/kqueue: 살아 있는 연결 목록 (서버 스레드만 만짐) */
    pthread_mutex_t mu;                   /* thread 모델: recs 와 각 rec->fd 보호 */
    trec_t **recs;
    int nrecs, caprecs;
} server_t;

/* accept 하나. 없으면 -1 (EAGAIN). sndbuf 설정 · 통계 */
static int accept_one(server_t *s) {
    for (;;) {
        int fd = accept(s->lfd, NULL, NULL);
        if (fd < 0) {
            if (errno == EINTR || errno == ECONNABORTED) continue;
            return -1;
        }
        if (s->cfg.sndbuf > 0) setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &s->cfg.sndbuf, sizeof s->cfg.sndbuf);
        st_add(&s->st.accepted, 1);
        return fd;
    }
}

/* ======================= Part A — 연결당 스레드 ======================= */

/* 블로킹 fd 에 n 바이트를 전부 쓴다. 0 = 성공, -1 = 오류(EPIPE · ECONNRESET 등).
 * write 는 요청보다 적게 쓰고 돌아올 수 있다(짧은 쓰기). 신호에 끊기면 EINTR. 통계: writes, short_writes, bytes_out */
static int write_all(int fd, const unsigned char *p, size_t n, srv_stats_t *st) {
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        st_add(&st->writes, 1);
        if ((size_t)w < n) st_add(&st->short_writes, 1);
        st_add(&st->bytes_out, (long)w);
        p += w;
        n -= (size_t)w;
    }
    return 0;
}

/* 연결 하나를 맡은 스레드의 본체: EOF(0) 또는 오류까지 read → write_all. 통계: reads, bytes_in */
static void thread_conn_loop(int fd, srv_stats_t *st) {
    unsigned char buf[CHUNK];
    for (;;) {
        ssize_t r = read(fd, buf, sizeof buf);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return;                               /* 0 = 상대가 닫음, <0 = 오류 · shutdown */
        st_add(&st->reads, 1);
        st_add(&st->bytes_in, (long)r);
        if (write_all(fd, buf, (size_t)r, st) != 0) return;
    }
}

static void *thread_conn_main(void *arg) {
    trec_t *r = arg;
    server_t *s = r->srv;
    pthread_mutex_lock(&s->mu);
    int fd = r->fd;
    pthread_mutex_unlock(&s->mu);
    thread_conn_loop(fd, &s->st);
    pthread_mutex_lock(&s->mu);                           /* -1 로 바꾼 뒤에 close: 정지 쪽 shutdown 과 경쟁 없음 */
    r->fd = -1;
    pthread_mutex_unlock(&s->mu);
    close(fd);
    st_add(&s->st.closed, 1);
    st_live(&s->st, -1);
    return NULL;
}

static void *thread_server_main(void *arg) {
    server_t *s = arg;
    struct pollfd p[2] = { { s->lfd, POLLIN, 0 }, { s->wake[0], POLLIN, 0 } };
    for (;;) {
        if (poll(p, 2, -1) < 0) {
            if (errno == EINTR) continue;
            break;
        }
        st_add(&s->st.loops, 1);
        if (p[1].revents) break;                          /* 정지 요청 */
        if (!(p[0].revents & POLLIN)) continue;
        int fd;
        while ((fd = accept_one(s)) >= 0) {
            set_nonblock(fd, false);      /* macOS: accept 한 소켓은 listen 소켓의 O_NONBLOCK 을 물려받는다 */
            trec_t *r = calloc(1, sizeof *r);
            r->fd = fd;
            r->srv = s;
            pthread_mutex_lock(&s->mu);
            if (s->nrecs == s->caprecs) {
                s->caprecs = s->caprecs ? 2 * s->caprecs : 64;
                s->recs = realloc(s->recs, (size_t)s->caprecs * sizeof *s->recs);
            }
            s->recs[s->nrecs++] = r;
            pthread_mutex_unlock(&s->mu);
            st_live(&s->st, +1);
            if (pthread_create(&r->th, NULL, thread_conn_main, r) == 0) {
                r->started = true;
            } else {                                      /* 스레드를 못 만들면 연결을 거절 */
                pthread_mutex_lock(&s->mu);
                r->fd = -1;
                pthread_mutex_unlock(&s->mu);
                close(fd);
                st_add(&s->st.rejected, 1);
                st_live(&s->st, -1);
            }
        }
    }
    pthread_mutex_lock(&s->mu);                           /* 블로킹 read/write 에 잠든 스레드를 깨운다 */
    for (int i = 0; i < s->nrecs; i++)
        if (s->recs[i]->fd >= 0) shutdown(s->recs[i]->fd, SHUT_RDWR);
    pthread_mutex_unlock(&s->mu);
    for (int i = 0; i < s->nrecs; i++) {
        if (s->recs[i]->started) pthread_join(s->recs[i]->th, NULL);
        free(s->recs[i]);
    }
    s->nrecs = 0;
    return NULL;
}

/* ======================= Part B — 논블로킹 연결 상태기계 + select ======================= */

/* 대기 출력 buf[off, len) 을 EAGAIN 까지 쓴다.
 * 다 썼으면 off = len = 0 으로 비우고 NEXT_READ, EAGAIN 이면 NEXT_WRITE (eagain++), 오류면 NEXT_CLOSE.
 * 통계: writes, short_writes(요청보다 적게 쓴 write), bytes_out, eagain */
static int conn_flush(conn_t *c, srv_stats_t *st) {
    while (c->off < c->len) {
        size_t want = c->len - c->off;
        ssize_t w = write(c->fd, c->buf + c->off, want);
        if (w < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                st_add(&st->eagain, 1);
                return NEXT_WRITE;
            }
            return NEXT_CLOSE;                            /* EPIPE · ECONNRESET */
        }
        st_add(&st->writes, 1);
        if ((size_t)w < want) st_add(&st->short_writes, 1);
        st_add(&st->bytes_out, (long)w);
        c->off += (size_t)w;
    }
    c->off = c->len = 0;
    return NEXT_READ;
}

/* 읽을 수 있다는 알림을 받았을 때. 대기 출력이 남아 있으면 읽지 않는다(역압) → NEXT_WRITE.
 * CHUNK 만큼 read → 바로 conn_flush. drain=false 면 read 한 번만(공정성), true 면 EAGAIN 이나 대기 출력이
 * 생길 때까지 반복(EV_CLEAR 모드에 필요). 0 바이트(EOF) · 오류 → NEXT_CLOSE. 통계: reads, bytes_in */
static int conn_on_readable(conn_t *c, srv_stats_t *st, bool drain) {
    if (c->off < c->len) return NEXT_WRITE;
    for (;;) {
        ssize_t r = read(c->fd, c->buf, CHUNK);
        if (r < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return NEXT_READ;
            return NEXT_CLOSE;
        }
        if (r == 0) return NEXT_CLOSE;
        st_add(&st->reads, 1);
        st_add(&st->bytes_in, (long)r);
        c->off = 0;
        c->len = (size_t)r;
        int nx = conn_flush(c, st);
        if (nx != NEXT_READ || !drain) return nx;
    }
}

/* select 가 다룰 수 있는 fd 인가: 0 <= fd < FD_SETSIZE (macOS 1024). 밖이면 FD_SET 은 정의되지 않은 동작. */
static bool sel_fd_ok(int fd) {
    return fd >= 0 && fd < FD_SETSIZE;
}

/* 연결마다 원하는 방향 한 쪽만 집합에 넣는다: want == NEXT_WRITE → ws, 아니면 rs.
 * 반환: 넣은 fd 중 최댓값 (연결이 없으면 -1). FD_ZERO 와 listen · wake fd 는 호출자가 한다. */
static int sel_build_sets(const conn_t *head, fd_set *rs, fd_set *ws) {
    int maxfd = -1;
    for (const conn_t *c = head; c; c = c->next) {
        if (c->want == NEXT_WRITE) FD_SET(c->fd, ws);
        else FD_SET(c->fd, rs);
        if (c->fd > maxfd) maxfd = c->fd;
    }
    return maxfd;
}

static conn_t *conn_new(server_t *s, int fd) {
    conn_t *c = malloc(sizeof *c);
    c->fd = fd;
    c->want = NEXT_NONE;
    c->off = c->len = 0;
    c->prev = NULL;
    c->next = s->head;
    if (s->head) s->head->prev = c;
    s->head = c;
    st_live(&s->st, +1);
    return c;
}
static void conn_close(server_t *s, conn_t *c) {           /* 목록에서 빼고 닫는다. free 는 호출자 */
    if (c->prev) c->prev->next = c->next;
    else s->head = c->next;
    if (c->next) c->next->prev = c->prev;
    close(c->fd);
    c->fd = -1;
    st_add(&s->st.closed, 1);
    st_live(&s->st, -1);
}
static void close_all_conns(server_t *s) {
    while (s->head) {
        conn_t *c = s->head;
        conn_close(s, c);
        free(c);
    }
}

static void *select_server_main(void *arg) {
    server_t *s = arg;
    for (;;) {
        fd_set rs, ws;
        FD_ZERO(&rs);
        FD_ZERO(&ws);
        FD_SET(s->lfd, &rs);
        FD_SET(s->wake[0], &rs);
        int maxfd = s->lfd > s->wake[0] ? s->lfd : s->wake[0];
        int m = sel_build_sets(s->head, &rs, &ws);
        if (m > maxfd) maxfd = m;
        if (select(maxfd + 1, &rs, &ws, NULL, NULL) < 0) {   /* 매번 집합 전체를 커널에 복사 · 검사: O(maxfd) */
            if (errno == EINTR) continue;
            break;
        }
        st_add(&s->st.loops, 1);
        if (FD_ISSET(s->wake[0], &rs)) break;
        for (conn_t *c = s->head, *nx; c; c = nx) {          /* 준비된 fd 를 찾으려면 또 전부 훑는다 */
            nx = c->next;
            int r;
            if (c->want == NEXT_WRITE && FD_ISSET(c->fd, &ws)) r = conn_flush(c, &s->st);
            else if (c->want != NEXT_WRITE && FD_ISSET(c->fd, &rs)) r = conn_on_readable(c, &s->st, false);
            else continue;
            if (r == NEXT_CLOSE) {
                conn_close(s, c);
                free(c);
            } else {
                c->want = r;
            }
        }
        if (FD_ISSET(s->lfd, &rs)) {
            int fd;
            while ((fd = accept_one(s)) >= 0) {
                if (!sel_fd_ok(fd)) {                        /* fd >= 1024: select 로는 감시 불가 → 거절 */
                    close(fd);
                    st_add(&s->st.rejected, 1);
                    continue;
                }
                set_nonblock(fd, true);
                conn_t *c = conn_new(s, fd);
                c->want = NEXT_READ;
            }
        }
    }
    close_all_conns(s);
    return NULL;
}

/* ======================= Part C — kqueue ======================= */

/* 연결 c 의 관심 방향을 want(NEXT_READ / NEXT_WRITE)로 바꾼다. 이미 같으면 아무것도 안 한다(시스템 콜 0).
 * 원하는 쪽 필터는 EV_ADD|EV_ENABLE, 반대쪽은 EV_ADD|EV_DISABLE. clear 면 둘 다 EV_CLEAR. udata = c.
 * 성공하면 c->want = want, kq_changes++ 후 0. 실패 -1. */
static int kq_want(int kq, conn_t *c, int want, bool clear, srv_stats_t *st) {
    if (c->want == want) return 0;
    unsigned short cl = clear ? EV_CLEAR : 0;
    struct kevent ch[2];
    EV_SET(&ch[0], c->fd, EVFILT_READ, EV_ADD | cl | (want == NEXT_READ ? EV_ENABLE : EV_DISABLE), 0, 0, c);
    EV_SET(&ch[1], c->fd, EVFILT_WRITE, EV_ADD | cl | (want == NEXT_WRITE ? EV_ENABLE : EV_DISABLE), 0, 0, c);
    if (kevent(kq, ch, 2, NULL, 0, NULL) < 0) return -1;
    st_add(&st->kq_changes, 1);
    c->want = want;
    return 0;
}

/* 이벤트 하나 처리 → 다음 상태. EV_ERROR → NEXT_CLOSE.
 * EVFILT_WRITE: conn_flush. 다 썼고 clear 모드면 곧바로 conn_on_readable(drain) — EV_CLEAR 에서는
 *   쓰는 동안 도착한 데이터에 대한 "새" 읽기 이벤트가 안 올 수 있다.
 * EVFILT_READ: conn_on_readable(c, st, clear). 레벨 모드는 한 번만 읽어도 남은 데이터가 다음 kevent 에 또 뜬다. */
static int kq_on_event(conn_t *c, const struct kevent *ev, srv_stats_t *st, bool clear) {
    if (ev->flags & EV_ERROR) return NEXT_CLOSE;
    if (ev->filter == EVFILT_WRITE) {
        int r = conn_flush(c, st);
        if (r == NEXT_READ && clear) r = conn_on_readable(c, st, true);
        return r;
    }
    if (ev->filter == EVFILT_READ) return conn_on_readable(c, st, clear);
    return c->want;
}

static void *kq_server_main(void *arg) {
    server_t *s = arg;
    bool clear = s->cfg.kq_clear;
    int kq = kqueue();
    struct kevent ch[2];
    EV_SET(&ch[0], s->lfd, EVFILT_READ, EV_ADD, 0, 0, &s->lfd);        /* udata 로 listen · wake 를 구분 */
    EV_SET(&ch[1], s->wake[0], EVFILT_READ, EV_ADD, 0, 0, &s->wake);
    if (kq < 0 || kevent(kq, ch, 2, NULL, 0, NULL) < 0) {
        if (kq >= 0) close(kq);
        return NULL;
    }
    struct kevent ev[64];
    bool stop = false;
    while (!stop) {
        int n = kevent(kq, NULL, 0, ev, 64, NULL);        /* 준비된 것만 돌아온다: O(준비된 수) */
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        st_add(&s->st.loops, 1);
        conn_t *zombies = NULL;                           /* 이번 배치가 끝날 때까지 free 미룸 (udata 보호) */
        for (int i = 0; i < n; i++) {
            if (ev[i].udata == (void *)&s->wake) { stop = true; continue; }
            if (ev[i].udata == (void *)&s->lfd) {
                int fd;
                while ((fd = accept_one(s)) >= 0) {
                    set_nonblock(fd, true);
                    conn_t *c = conn_new(s, fd);
                    if (kq_want(kq, c, NEXT_READ, clear, &s->st) != 0) {
                        conn_close(s, c);
                        free(c);
                    }
                }
                continue;
            }
            conn_t *c = ev[i].udata;
            if (c->fd < 0) continue;                      /* 같은 배치에서 이미 닫힌 연결 */
            int r = kq_on_event(c, &ev[i], &s->st, clear);
            if (r == NEXT_CLOSE || kq_want(kq, c, r, clear, &s->st) != 0) {
                conn_close(s, c);                         /* close 하면 knote 도 자동 삭제 (man kqueue) */
                c->next = zombies;
                zombies = c;
            }
        }
        while (zombies) {
            conn_t *z = zombies;
            zombies = z->next;
            free(z);
        }
    }
    close_all_conns(s);
    close(kq);
    return NULL;
}

/* ======================= 서버 시작 · 정지 (주어짐) ======================= */
static int server_start(server_t *s, srv_cfg_t cfg) {
    memset(s, 0, sizeof *s);
    s->cfg = cfg;
    pthread_mutex_init(&s->mu, NULL);
    s->lfd = make_listener(&s->port);
    if (s->lfd < 0) return -1;
    if (pipe(s->wake) != 0) { close(s->lfd); return -1; }
    void *(*fn)(void *) = cfg.model == MODEL_THREAD ? thread_server_main
                        : cfg.model == MODEL_SELECT ? select_server_main : kq_server_main;
    if (pthread_create(&s->th, NULL, fn, s) != 0) {
        close(s->lfd); close(s->wake[0]); close(s->wake[1]);
        return -1;
    }
    return 0;
}
static void server_stop(server_t *s) {
    ssize_t w = write(s->wake[1], "q", 1);               /* 죽이지 않는다: 깨워서 스스로 정리하게 */
    (void)w;
    pthread_join(s->th, NULL);
    close(s->lfd);
    close(s->wake[0]);
    close(s->wake[1]);
    free(s->recs);
    s->recs = NULL;
    pthread_mutex_destroy(&s->mu);
}

#ifndef ECHO_NO_MAIN
/* ======================= 테스트 하네스 (주어짐) ======================= */
static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static const size_t SIZES[5] = { 1, 100, 4096, 65536, 300000 };
#define SIZES_TOTAL ((long)(1 + 100 + 4096 + 65536 + 300000))

static void fill_pattern(unsigned char *m, size_t n, int id, int k) {
    for (size_t i = 0; i < n; i++)
        m[i] = (unsigned char)(((uint32_t)i * 2654435761u >> 13) ^ (uint32_t)(id * 31 + k * 7) ^ (uint32_t)(i >> 9));
}

typedef struct { pthread_mutex_t m; pthread_cond_t c; int n, arrived; } bar_t;
static void bar_wait(bar_t *b, int64_t deadline) {        /* 마감이 있는 배리어 (맥에는 pthread_barrier_t 없음) */
    pthread_mutex_lock(&b->m);
    b->arrived++;
    pthread_cond_broadcast(&b->c);
    while (b->arrived < b->n && now_ms() < deadline) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 20 * 1000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        pthread_cond_timedwait(&b->c, &b->m, &ts);
    }
    pthread_mutex_unlock(&b->m);
}

typedef struct {
    int port, id, nsizes, rcvbuf;
    const size_t *sizes;
    bool lazy;                       /* 다 보내기 전에는 읽지 않는다 (gate 가 있으면 서버 eagain > 0 이 될 때까지) */
    _Atomic long *gate;
    bool rst;                        /* 64 KB 보내고 읽지 않은 채 SO_LINGER 0 으로 RST 종료 */
    bar_t *bar;
    int64_t deadline;
    bool ok, timed_out;
    char why[96];
} client_t;

static int connect_to(int port, int rcvbuf, int64_t deadline) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    if (rcvbuf > 0) setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf);   /* connect 전에 */
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    set_nonblock(fd, true);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) < 0 && errno != EINPROGRESS) { close(fd); return -1; }
    struct pollfd p = { fd, POLLOUT, 0 };
    int left = (int)(deadline - now_ms());
    int err = 0;
    socklen_t el = sizeof err;
    if (left <= 0 || poll(&p, 1, left) != 1 || getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el) != 0 || err) {
        close(fd);
        return -1;
    }
    return fd;
}

/* 보내면서 동시에 읽는다(안 그러면 큰 메시지에서 클라이언트-서버가 서로 쓰기에서 막힌다). */
static bool client_echo(client_t *cl, int fd, const unsigned char *msg, size_t n) {
    unsigned char *rb = malloc(n);
    size_t sent = 0, got = 0;
    bool fail = false;
    while (got < n && !fail) {
        if (now_ms() > cl->deadline) {
            cl->timed_out = true;
            snprintf(cl->why, sizeof cl->why, "timeout: sent %zu got %zu of %zu", sent, got, n);
            fail = true;
            break;
        }
        bool rd = !cl->lazy ? true : cl->gate ? st_get(cl->gate) > 0 : sent == n;
        struct pollfd p = { fd, (short)((sent < n ? POLLOUT : 0) | (rd ? POLLIN : 0)), 0 };
        if (poll(&p, 1, 10) < 0 && errno != EINTR) { fail = true; break; }
        if (sent < n && (p.revents & (POLLOUT | POLLERR | POLLHUP))) {
            ssize_t w = write(fd, msg + sent, n - sent);
            if (w > 0) sent += (size_t)w;
            else if (w < 0 && errno != EAGAIN && errno != EINTR) {
                snprintf(cl->why, sizeof cl->why, "write: %s (sent %zu got %zu)", strerror(errno), sent, got);
                fail = true;
            }
        }
        if (!fail && rd && (p.revents & (POLLIN | POLLHUP | POLLERR))) {
            ssize_t r = read(fd, rb + got, n - got);
            if (r > 0) got += (size_t)r;
            else if (r == 0) {
                snprintf(cl->why, sizeof cl->why, "EOF from server after %zu of %zu", got, n);
                fail = true;
            } else if (errno != EAGAIN && errno != EINTR) {
                snprintf(cl->why, sizeof cl->why, "read: %s", strerror(errno));
                fail = true;
            }
        }
    }
    bool ok = !fail && got == n && memcmp(rb, msg, n) == 0;
    if (!fail && !ok) snprintf(cl->why, sizeof cl->why, "echo mismatch (%zu bytes)", n);
    free(rb);
    return ok;
}

static void *client_main(void *arg) {
    client_t *cl = arg;
    cl->ok = false;
    int fd = connect_to(cl->port, cl->rcvbuf, cl->deadline);
    if (fd < 0) {
        snprintf(cl->why, sizeof cl->why, "connect failed");
    } else if (cl->rst) {
        static unsigned char junk[65536];
        size_t sent = 0;
        int64_t until = now_ms() + 200;
        while (sent < sizeof junk && now_ms() < until) {      /* 서버가 쓰기를 시작하게 충분히 보낸다 */
            ssize_t w = write(fd, junk + sent, sizeof junk - sent);
            if (w > 0) sent += (size_t)w;
            else { struct pollfd p = { fd, POLLOUT, 0 }; poll(&p, 1, 10); }
        }
        struct linger lg = { 1, 0 };
        setsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof lg);
        cl->ok = sent == sizeof junk;
    } else {
        bool ok = true;
        for (int k = 0; k < cl->nsizes && ok; k++) {
            size_t n = cl->sizes[(k + cl->id) % cl->nsizes];
            unsigned char *m = malloc(n);
            fill_pattern(m, n, cl->id, k);
            ok = client_echo(cl, fd, m, n);
            free(m);
        }
        cl->ok = ok;
    }
    if (cl->bar) bar_wait(cl->bar, cl->deadline);         /* 전원이 동시에 연결된 순간을 만든다 */
    if (fd >= 0) close(fd);
    return NULL;
}

/* n 개 클라이언트를 동시에 돌린다. 반환: 성공한 수. 첫 실패 이유를 출력 */
static int run_clients(int port, int n, bool lazy, _Atomic long *gate, int rcvbuf, bool rst, int ms) {
    client_t *cl = calloc((size_t)n, sizeof *cl);
    pthread_t *th = calloc((size_t)n, sizeof *th);
    bar_t bar = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, n, 0 };
    int64_t dl = now_ms() + ms;
    for (int i = 0; i < n; i++) {
        cl[i] = (client_t){ .port = port, .id = i, .nsizes = 5, .sizes = SIZES, .lazy = lazy, .gate = gate,
                            .rcvbuf = rcvbuf, .rst = rst, .bar = n > 1 ? &bar : NULL, .deadline = dl };
        if (pthread_create(&th[i], NULL, client_main, &cl[i]) != 0) th[i] = NULL;
    }
    int ok = 0;
    bool printed = false;
    for (int i = 0; i < n; i++) {
        if (th[i]) pthread_join(th[i], NULL);
        if (cl[i].ok) ok++;
        else if (!printed) { printf("  client %d: %s\n", i, cl[i].why); printed = true; }
    }
    free(cl);
    free(th);
    return ok;
}

static bool wait_live_zero(server_t *s, int ms) {
    int64_t dl = now_ms() + ms;
    while (st_get(&s->st.live) != 0 && now_ms() < dl) {
        struct timespec ts = { 0, 5 * 1000000L };
        nanosleep(&ts, NULL);
    }
    return st_get(&s->st.live) == 0;
}

static bool wait_stat(_Atomic long *a, long want, int ms) {   /* 통계 카운터는 write 가 돌아온 "뒤"에 오른다 */
    int64_t dl = now_ms() + ms;
    while (st_get(a) != want && now_ms() < dl) {
        struct timespec ts = { 0, 2 * 1000000L };
        nanosleep(&ts, NULL);
    }
    return st_get(a) == want;
}

static void print_stats(const char *tag, server_t *s) {
    srv_stats_t *t = &s->st;
    printf("  [%s] accepted=%ld rejected=%ld max_live=%ld in=%ld out=%ld reads=%ld writes=%ld short=%ld "
           "eagain=%ld loops=%ld kq_changes=%ld\n", tag, st_get(&t->accepted), st_get(&t->rejected),
           st_get(&t->max_live), st_get(&t->bytes_in), st_get(&t->bytes_out), st_get(&t->reads),
           st_get(&t->writes), st_get(&t->short_writes), st_get(&t->eagain), st_get(&t->loops),
           st_get(&t->kq_changes));
}

/* 서버 하나를 같은 시나리오로 검사한다: 스모크 → 64 동시 → 느린 독자 → RST → 정지 · fd 누수 */
static void suite(const char *tag, srv_cfg_t cfg) {
    printf("== %s ==\n", tag);
    int fds0 = count_open_fds();
    server_t s;
    CHECK(server_start(&s, cfg) == 0 && s.port > 0);
    int smoke = run_clients(s.port, 1, false, NULL, 0, false, 1500);
    CHECK(smoke == 1);
    if (smoke == 1) {
        int ok = run_clients(s.port, 64, false, NULL, 0, false, 10000);
        CHECK(ok == 64);
        CHECK(st_get(&s.st.max_live) >= 64 && st_get(&s.st.accepted) == 65);
        CHECK(wait_stat(&s.st.bytes_in, 65 * SIZES_TOTAL, 1000) && wait_stat(&s.st.bytes_out, 65 * SIZES_TOTAL, 1000));
        _Atomic long *gate = cfg.model == MODEL_THREAD ? NULL : &s.st.eagain;
        int lazy = run_clients(s.port, 1, true, gate, 16384, false, 3000);
        CHECK(lazy == 1 && (cfg.model == MODEL_THREAD || st_get(&s.st.eagain) >= 1));
        int rst = run_clients(s.port, 1, false, NULL, 0, true, 2000);
        CHECK(rst == 1 && wait_live_zero(&s, 2000));
    } else {
        printf("SKIP 64-client / slow-reader / RST tests (smoke failed)\n");
    }
    print_stats(tag, &s);
    server_stop(&s);
    CHECK(st_get(&s.st.live) == 0 && count_open_fds() == fds0);
}

/* TCP 연결 한 쌍 (a = 서버 쪽 논블로킹, b = 상대) */
static bool tcp_pair(int *a, int *b) {
    int port, l = make_listener(&port);
    if (l < 0) return false;
    *b = connect_to(port, 0, now_ms() + 1000);
    struct pollfd p = { l, POLLIN, 0 };
    poll(&p, 1, 1000);
    *a = accept(l, NULL, NULL);
    close(l);
    if (*a < 0 || *b < 0) { if (*a >= 0) close(*a); if (*b >= 0) close(*b); return false; }
    set_nonblock(*a, true);
    return true;
}
/* fd 에 정확히 n 바이트가 도착할 때까지(최대 ms) 기다렸다가 읽는다 */
static size_t read_exact(int fd, unsigned char *buf, size_t n, int ms) {
    size_t got = 0;
    int64_t dl = now_ms() + ms;
    while (got < n && now_ms() < dl) {
        struct pollfd p = { fd, POLLIN, 0 };
        if (poll(&p, 1, 10) <= 0) continue;
        ssize_t r = read(fd, buf + got, n - got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    return got;
}
static void wait_bytes(int fd, size_t n, int ms) {           /* MSG_PEEK 로 n 바이트 도착 확인 */
    unsigned char *tmp = malloc(n);
    int64_t dl = now_ms() + ms;
    while (now_ms() < dl) {
        ssize_t r = recv(fd, tmp, n, MSG_PEEK);
        if (r >= (ssize_t)n) break;
        struct timespec ts = { 0, 2 * 1000000L };
        nanosleep(&ts, NULL);
    }
    free(tmp);
}

static void test_part_a_units(void) {
    printf("== Part A: write_all ==\n");
    int sv[2];
    socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    int small = 4096;
    setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &small, sizeof small);
    size_t n = 300000;
    unsigned char *m = malloc(n), *r = malloc(n);
    fill_pattern(m, n, 7, 1);
    pid_t pid = fork();                                       /* 읽는 쪽은 자식: 천천히 읽어도 전부 받아야 한다 */
    if (pid == 0) {
        close(sv[0]);
        alarm(5);                                             /* 워치독 */
        size_t got = read_exact(sv[1], r, n, 4000);
        _exit(got == n && memcmp(m, r, n) == 0 ? 0 : 1);
    }
    close(sv[1]);
    srv_stats_t st;
    memset(&st, 0, sizeof st);
    alarm(5);
    int rc = write_all(sv[0], m, n, &st);
    alarm(0);
    close(sv[0]);
    int status = -1;
    waitpid(pid, &status, 0);
    CHECK(rc == 0 && st_get(&st.bytes_out) == (long)n);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    free(m);
    free(r);
}

static void test_part_b_units(void) {
    printf("== Part B: conn_flush / conn_on_readable / select helpers ==\n");
    int a, b;
    CHECK(tcp_pair(&a, &b));
    conn_t *c = calloc(1, sizeof *c);
    c->fd = a;
    srv_stats_t st;
    memset(&st, 0, sizeof st);
    /* 상대가 안 읽으면 결국 EAGAIN: 남은 출력을 들고 NEXT_WRITE */
    long pushed = 0;
    int r = NEXT_READ;
    for (int i = 0; i < 4000 && r == NEXT_READ; i++) {
        fill_pattern(c->buf, CHUNK, 3, i);
        c->off = 0;
        c->len = CHUNK;
        r = conn_flush(c, &st);
        pushed += CHUNK;
    }
    CHECK(r == NEXT_WRITE && c->off < c->len && st_get(&st.eagain) == 1);
    /* 상대가 다 읽어 주면 다시 비워진다 */
    unsigned char *sink = malloc((size_t)pushed);
    size_t got = 0;
    int64_t dl = now_ms() + 2000;
    while (r == NEXT_WRITE && now_ms() < dl) {
        ssize_t k = read(b, sink + got, (size_t)pushed - got);
        if (k > 0) got += (size_t)k;
        r = conn_flush(c, &st);
    }
    got += read_exact(b, sink + got, (size_t)pushed - got, 1000);
    CHECK(r == NEXT_READ && c->off == 0 && c->len == 0 && got == (size_t)pushed &&
          st_get(&st.bytes_out) == pushed);
    free(sink);

    /* read: drain=false 는 한 번만, drain=true 는 EAGAIN 까지 */
    size_t big = 3 * CHUNK + 5;
    unsigned char *m = malloc(big), *back = malloc(big);
    fill_pattern(m, big, 9, 0);
    CHECK(write(b, m, big) == (ssize_t)big);
    wait_bytes(a, big, 1000);
    memset(&st, 0, sizeof st);
    r = conn_on_readable(c, &st, false);
    CHECK(r == NEXT_READ && st_get(&st.reads) == 1 && st_get(&st.bytes_in) == CHUNK);
    r = conn_on_readable(c, &st, true);
    CHECK(r == NEXT_READ && st_get(&st.reads) == 4 && st_get(&st.bytes_in) == (long)big);
    CHECK(read_exact(b, back, big, 1000) == big && memcmp(m, back, big) == 0);
    /* 대기 출력이 있으면 읽지 않는다 (역압) */
    c->off = 0;
    c->len = 10;
    memset(&st, 0, sizeof st);
    CHECK(conn_on_readable(c, &st, true) == NEXT_WRITE && st_get(&st.reads) == 0);
    c->len = 0;
    /* 상대가 닫으면 NEXT_CLOSE */
    close(b);
    int r2 = NEXT_READ;
    for (int i = 0; i < 100 && r2 == NEXT_READ; i++) {
        r2 = conn_on_readable(c, &st, true);
        if (r2 == NEXT_READ) { struct timespec ts = { 0, 2000000L }; nanosleep(&ts, NULL); }
    }
    CHECK(r2 == NEXT_CLOSE);
    close(a);
    free(c);
    free(m);
    free(back);

    CHECK(sel_fd_ok(0) && sel_fd_ok(FD_SETSIZE - 1) && !sel_fd_ok(FD_SETSIZE) && !sel_fd_ok(-1));
    conn_t x = { .fd = 5, .want = NEXT_READ }, y = { .fd = 700, .want = NEXT_WRITE }, z = { .fd = 9, .want = NEXT_READ };
    x.next = &y;
    y.next = &z;
    fd_set rs, ws;
    FD_ZERO(&rs);
    FD_ZERO(&ws);
    int mx = sel_build_sets(&x, &rs, &ws);
    CHECK(mx == 700 && FD_ISSET(5, &rs) && FD_ISSET(9, &rs) && FD_ISSET(700, &ws) &&
          !FD_ISSET(700, &rs) && !FD_ISSET(5, &ws));
    FD_ZERO(&rs);
    CHECK(sel_build_sets(NULL, &rs, &ws) == -1);
}

/* fd 0..1023 을 전부 채운 뒤 들어온 연결은 select 서버가 거절해야 한다 */
static void test_select_guard(void) {
    printf("== Part B: FD_SETSIZE guard ==\n");
    int fds0 = count_open_fds();
    server_t s;
    srv_cfg_t cfg = { MODEL_SELECT, false, 16384 };
    CHECK(server_start(&s, cfg) == 0);
    int dups[FD_SETSIZE + 8], nd = 0, fd;
    while (nd < FD_SETSIZE + 8 && (fd = open("/dev/null", O_RDONLY)) >= 0) {
        dups[nd++] = fd;
        if (fd >= FD_SETSIZE - 1) break;
    }
    int ok = run_clients(s.port, 1, false, NULL, 0, false, 1500);   /* 클라·서버 소켓 모두 fd >= 1024 */
    CHECK(ok == 0 && st_get(&s.st.rejected) == 1);
    for (int i = 0; i < nd; i++) close(dups[i]);
    ok = run_clients(s.port, 1, false, NULL, 0, false, 1500);
    CHECK(ok == 1 && st_get(&s.st.rejected) == 1);
    printf("  filled %d fds (last %d), rejected=%ld\n", nd, nd ? dups[nd - 1] : -1, st_get(&s.st.rejected));
    server_stop(&s);
    CHECK(count_open_fds() == fds0);
}

static void test_part_c_units(void) {
    printf("== Part C: kq_want / EV_CLEAR semantics ==\n");
    for (int clear = 0; clear <= 1; clear++) {
        int a, b;
        if (!tcp_pair(&a, &b)) { CHECK(false && "tcp_pair"); continue; }
        int kq = kqueue();
        conn_t *c = calloc(1, sizeof *c);
        c->fd = a;
        c->want = NEXT_NONE;
        srv_stats_t st;
        memset(&st, 0, sizeof st);
        struct kevent ev[4];
        struct timespec t300 = { 0, 300 * 1000000L }, t0 = { 0, 0 };
        CHECK(write(b, "xy", 2) == 2);
        wait_bytes(a, 2, 1000);
        CHECK(kq_want(kq, c, NEXT_READ, clear, &st) == 0 && c->want == NEXT_READ);
        int n1 = kevent(kq, NULL, 0, ev, 4, &t300);
        CHECK(n1 == 1 && ev[0].filter == EVFILT_READ && ev[0].udata == c && ev[0].data == 2);
        /* 읽지 않고 다시 물어본다: 레벨이면 또 뜨고, EV_CLEAR 면 새 데이터가 올 때까지 안 뜬다 */
        int n2 = kevent(kq, NULL, 0, ev, 4, &t0);
        printf("  %s: second kevent without reading -> %d event(s)\n", clear ? "EV_CLEAR" : "level", n2);
        CHECK(n2 == (clear ? 0 : 1));
        CHECK(write(b, "z", 1) == 1);
        wait_bytes(a, 3, 1000);
        int n3 = kevent(kq, NULL, 0, ev, 4, &t300);
        CHECK(n3 == 1 && ev[0].filter == EVFILT_READ && ev[0].data == 3);
        /* 쓰기 대기로 바꾸면 읽을 데이터가 있어도 READ 는 안 뜨고 WRITE 만 뜬다 */
        CHECK(kq_want(kq, c, NEXT_WRITE, clear, &st) == 0);
        int n4 = kevent(kq, NULL, 0, ev, 4, &t300);
        CHECK(n4 == 1 && ev[0].filter == EVFILT_WRITE && ev[0].udata == c);
        long ch = st_get(&st.kq_changes);
        CHECK(kq_want(kq, c, NEXT_WRITE, clear, &st) == 0 && st_get(&st.kq_changes) == ch && ch == 2);
        close(kq);
        close(a);
        close(b);
        free(c);
    }
}

int main(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, NULL);           /* 끊긴 소켓에 쓰면 SIGPIPE 로 죽는 대신 EPIPE 를 받는다 */
    raise_nofile(4096);                      /* FD_SETSIZE 가드 테스트에 fd 1030 개 정도 필요 */
    int fds_start = count_open_fds();

    test_part_a_units();
    suite("Part A: thread-per-connection", (srv_cfg_t){ MODEL_THREAD, false, 16384 });
    test_part_b_units();
    test_select_guard();
    suite("Part B: select", (srv_cfg_t){ MODEL_SELECT, false, 16384 });
    test_part_c_units();
    suite("Part C: kqueue (level)", (srv_cfg_t){ MODEL_KQUEUE, false, 16384 });
    suite("Part C: kqueue (EV_CLEAR)", (srv_cfg_t){ MODEL_KQUEUE, true, 16384 });

    CHECK(count_open_fds() == fds_start);
    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
#endif /* ECHO_NO_MAIN */
