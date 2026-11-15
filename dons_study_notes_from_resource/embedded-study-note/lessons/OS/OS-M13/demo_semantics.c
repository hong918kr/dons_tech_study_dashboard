/* ENV: linux */
/* BUILD: -pthread */
/* OS-M13 demo_semantics.c — epoll 의 의미를 원시 syscall 로 하나씩 관찰한다 (CHECK 없음, 출력만).
 *   E1 LT vs ET: 같은 10000 B 를 안 읽고 epoll_wait(0) 를 다섯 번
 *   E2 ET 에서 EPOLL_CTL_MOD (같은 마스크) = 재무장 → 준비 상태를 다시 본다
 *   E3 EPOLLONESHOT: 한 번 알린 뒤 꺼짐, MOD 로 다시 켬
 *   E4 EPOLLOUT 을 늘 켜 두면 (LT) 놀고 있는 연결에서 몇 번 깨어나나
 *   E5 thundering herd: 스레드 4 개 × (같은 epfd LT / 같은 epfd ET / 각자 epfd / 각자 epfd + EPOLLEXCLUSIVE)
 * build (Lima VM): gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_semantics.c -o sem && ./sem
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static char buf[10000];

static int wait0(int ep) { struct epoll_event e[4]; return epoll_wait(ep, e, 4, 0); }
static void add(int ep, int fd, uint32_t ev, int op) {
    struct epoll_event e = { .events = ev, .data.fd = fd };
    if (epoll_ctl(ep, op, fd, &e) < 0) perror("epoll_ctl");
}
static long now_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000L + t.tv_nsec / 1000;
}
static void wr(int fd, size_t n) { if (write(fd, buf, n) != (ssize_t)n) perror("write"); }
static void rd(int fd, size_t n) { if (read(fd, buf, n) < 0) perror("read"); }

static void e1_e2_e3(void) {
    const char *name[] = { "LT", "ET" };
    for (int et = 0; et < 2; et++) {
        int sv[2];
        socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sv);
        int ep = epoll_create1(0);
        add(ep, sv[0], EPOLLIN | (et ? EPOLLET : 0), EPOLL_CTL_ADD);
        wr(sv[1], 10000);
        printf("E1 %s: 10000 B 도착, 안 읽고 epoll_wait(0) x5 →", name[et]);
        for (int i = 0; i < 5; i++) printf(" %d", wait0(ep));
        rd(sv[0], 100);
        printf(" | 100 B 만 읽고 → %d", wait0(ep));
        wr(sv[1], 1);
        printf(" | 상대가 1 B 더 → %d %d", wait0(ep), wait0(ep));
        if (et) {
            add(ep, sv[0], EPOLLIN | EPOLLET, EPOLL_CTL_MOD);
            printf(" | E2: 같은 마스크로 MOD → %d %d", wait0(ep), wait0(ep));
        }
        printf("\n");
        close(ep);
        close(sv[0]);
        close(sv[1]);
    }
    int sv[2];
    socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sv);
    int ep = epoll_create1(0);
    add(ep, sv[0], EPOLLIN | EPOLLONESHOT, EPOLL_CTL_ADD);
    wr(sv[1], 10);
    printf("E3 ONESHOT(LT): 10 B → %d %d", wait0(ep), wait0(ep));
    wr(sv[1], 10);
    printf(" | 10 B 더 → %d", wait0(ep));
    add(ep, sv[0], EPOLLIN | EPOLLONESHOT, EPOLL_CTL_MOD);
    printf(" | MOD 재무장 → %d %d\n", wait0(ep), wait0(ep));
    close(ep);
    close(sv[0]);
    close(sv[1]);
}

static void e4(void) {
    for (int out = 0; out < 2; out++) {
        int sv[2];
        socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sv);
        int ep = epoll_create1(0);
        add(ep, sv[0], EPOLLIN | (out ? EPOLLOUT : 0), EPOLL_CTL_ADD);
        long t0 = now_us(), wakes = 0;
        struct epoll_event e[4];
        while (now_us() - t0 < 100000) {
            int n = epoll_wait(ep, e, 4, 10);
            if (n > 0) wakes++;
        }
        printf("E4 놀고 있는 연결 100 ms, 관심 = %-16s → 깨어남 %ld 번\n", out ? "EPOLLIN|EPOLLOUT" : "EPOLLIN", wakes);
        close(ep);
        close(sv[0]);
        close(sv[1]);
    }
}

/* E5 ---------------------------------------------------------------- */
static atomic_int g_woke;
typedef struct { int ep; } warg_t;
static void *waiter(void *p) {
    warg_t *w = p;
    struct epoll_event e[4];
    if (epoll_wait(w->ep, e, 4, 300) > 0) atomic_fetch_add(&g_woke, 1);
    return NULL;
}
static int herd_trial(int lfd, int port, int shared, uint32_t ev) {
    enum { T = 4 };
    pthread_t th[T];
    warg_t w[T];
    int shared_ep = epoll_create1(0);
    if (shared) add(shared_ep, lfd, ev, EPOLL_CTL_ADD);
    for (int i = 0; i < T; i++) {
        w[i].ep = shared ? shared_ep : epoll_create1(0);
        if (!shared) add(w[i].ep, lfd, ev, EPOLL_CTL_ADD);
    }
    atomic_store(&g_woke, 0);
    for (int i = 0; i < T; i++) pthread_create(&th[i], NULL, waiter, &w[i]);
    usleep(100000); /* 네 스레드가 모두 epoll_wait 안에서 잠들 시간 (데모라서 sleep) */
    int c = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons((uint16_t)port),
                             .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    if (connect(c, (struct sockaddr *)&a, sizeof a) < 0) perror("connect");
    for (int i = 0; i < T; i++) pthread_join(th[i], NULL);
    int fd = accept(lfd, NULL, NULL); /* 아무도 accept 하지 않았으니 여기서 치운다 */
    if (fd >= 0) close(fd);
    close(c);
    for (int i = 0; i < T; i++) if (!shared) close(w[i].ep);
    close(shared_ep);
    return atomic_load(&g_woke);
}

static void e5(void) {
    int lfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    socklen_t al = sizeof a;
    bind(lfd, (struct sockaddr *)&a, sizeof a);
    listen(lfd, 16);
    getsockname(lfd, (struct sockaddr *)&a, &al);
    int port = ntohs(a.sin_port);
    struct { const char *name; int shared; uint32_t ev; } cfg[] = {
        { "같은 epfd, LT            ", 1, EPOLLIN },
        { "같은 epfd, ET            ", 1, EPOLLIN | EPOLLET },
        { "각자 epfd, 기본          ", 0, EPOLLIN },
        { "각자 epfd, EPOLLEXCLUSIVE", 0, EPOLLIN | EPOLLEXCLUSIVE },
    };
    for (int k = 0; k < 4; k++) {
        printf("E5 스레드 4 개, %s: 연결 1 개에 깨어난 스레드 수 (5 회) →", cfg[k].name);
        for (int r = 0; r < 5; r++) printf(" %d", herd_trial(lfd, port, cfg[k].shared, cfg[k].ev));
        printf("\n");
    }
    close(lfd);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    e1_e2_e3();
    e4();
    e5();
    return 0;
}
