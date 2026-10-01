// C33_event_loop.c — Ch.33 이벤트 루프를 select() 와 kqueue (macOS 의 epoll 대응물) 두 가지로 돌린다.
//  - 서버(부모) 1 스레드, 클라이언트(자식 프로세스 fork) 3개가 socketpair 로 요청을 보낸다.
//  - 요청 메시지에 보낸 시각(CLOCK_MONOTONIC, 프로세스 간 공통)을 넣어 서버가 "대기 지연(latency)" 을 잰다.
//  - 3번째 인자 "block" 을 주면 client 0 의 요청을 처리하는 핸들러가 usleep(200ms) 로 블로킹(디스크 읽기 흉내)
//    → 단일 스레드 이벤트 루프 전체가 멈추고 다른 클라이언트의 지연이 폭증하는 걸 본다.
//  - kqueue 모드에서는 EVFILT_TIMER(100ms 주기) 도 같은 루프에서 받는다.
//
// build: cc -Wall -Wextra -O0 code/C33_event_loop.c -o .work/bin/C33_event_loop
// run  : .work/bin/C33_event_loop select | kqueue [block]
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define NC 3          // 클라이언트 수
#define NREQ 3        // 클라이언트당 요청 수

static long long now_us(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}
static long long T0;
static int blocking_handler = 0;
static long long worst_latency[NC];

// ---------------- 클라이언트 (자식 프로세스) ----------------
static void client_proc(int fds[NC]) {
    // 요청 시각표: client i 는 (k*60 + i*10) ms 에 요청 k 를 보낸다
    for (int k = 0; k < NREQ; k++) {
        for (int i = 0; i < NC; i++) {
            long long due = T0 + (k * 60 + i * 10) * 1000LL;
            long long d = due - now_us();
            if (d > 0) usleep((useconds_t)d);
            char msg[64];
            int n = snprintf(msg, sizeof msg, "%d %d %lld\n", i, k, now_us());
            if (write(fds[i], msg, (size_t)n) != n) _exit(1);
        }
    }
    for (int i = 0; i < NC; i++) close(fds[i]);
    _exit(0);
}

// ---------------- 이벤트 핸들러 ----------------
// 한 fd 에서 읽을 수 있는 만큼 읽고 요청 하나하나 처리. 0 = EOF(닫힘)
static int handle_readable(int fd) {
    char buf[512];
    ssize_t n = read(fd, buf, sizeof buf - 1);        // non-blocking fd
    if (n == 0) return 0;
    if (n < 0) return (errno == EAGAIN) ? 1 : 0;
    buf[n] = 0;
    for (char *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
        int c, k; long long sent;
        if (sscanf(line, "%d %d %lld", &c, &k, &sent) != 3) continue;
        long long lat = now_us() - sent;
        if (lat > worst_latency[c]) worst_latency[c] = lat;
        printf("  t=%4lldms handle client%d req%d  queued %6.1fms\n", (now_us() - T0) / 1000, c, k, lat / 1000.0);
        if (blocking_handler && c == 0) usleep(200000);  // 블로킹 호출! 루프 전체가 같이 멈춘다
    }
    return 1;
}

static void loop_select(int fds[NC]) {
    int open_cnt = NC, alive[NC] = {1, 1, 1};
    while (open_cnt > 0) {
        fd_set rfds; FD_ZERO(&rfds);
        int maxfd = -1;
        for (int i = 0; i < NC; i++) if (alive[i]) { FD_SET(fds[i], &rfds); if (fds[i] > maxfd) maxfd = fds[i]; }
        int rc = select(maxfd + 1, &rfds, NULL, NULL, NULL);      // 매번 집합 전체를 다시 넘긴다
        if (rc < 0) { perror("select"); exit(1); }
        for (int i = 0; i < NC; i++)
            if (alive[i] && FD_ISSET(fds[i], &rfds) && !handle_readable(fds[i])) {
                alive[i] = 0; open_cnt--; close(fds[i]);
            }
    }
}

static void loop_kqueue(int fds[NC]) {
    int kq = kqueue();
    struct kevent ch[NC + 1];
    for (int i = 0; i < NC; i++) EV_SET(&ch[i], fds[i], EVFILT_READ, EV_ADD, 0, 0, (void *)(long)i);
    EV_SET(&ch[NC], 1, EVFILT_TIMER, EV_ADD, 0, 100, NULL);       // 100ms 주기 타이머 (ident=1)
    if (kevent(kq, ch, NC + 1, NULL, 0, NULL) < 0) { perror("kevent add"); exit(1); }  // 등록은 한 번만
    int open_cnt = NC, ticks = 0;
    while (open_cnt > 0) {
        struct kevent ev[8];
        int n = kevent(kq, NULL, 0, ev, 8, NULL);                 // 준비된 것만 돌려받는다
        if (n < 0) { perror("kevent"); exit(1); }
        for (int j = 0; j < n; j++) {
            if (ev[j].filter == EVFILT_TIMER) {
                ticks++;
                printf("  t=%4lldms timer tick #%d (fired %ld time(s))\n", (now_us() - T0) / 1000, ticks, (long)ev[j].data);
                continue;
            }
            int fd = (int)ev[j].ident;
            if (!handle_readable(fd) || ((ev[j].flags & EV_EOF) && ev[j].data == 0)) {
                close(fd); open_cnt--;                          // close 하면 kqueue 등록도 자동 해제
            }
        }
    }
    close(kq);
}

int main(int argc, char *argv[]) {
    const char *api = argc > 1 ? argv[1] : "kqueue";
    blocking_handler = (argc > 2 && strcmp(argv[2], "block") == 0);
    setvbuf(stdout, NULL, _IOLBF, 0);

    int srv[NC], cli[NC];
    for (int i = 0; i < NC; i++) {
        int sv[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) { perror("socketpair"); return 1; }
        srv[i] = sv[0]; cli[i] = sv[1];
        fcntl(srv[i], F_SETFL, fcntl(srv[i], F_GETFL) | O_NONBLOCK);
    }
    T0 = now_us() + 20000;                    // 20ms 뒤부터 요청 시작
    pid_t pid = fork();
    if (pid == 0) { for (int i = 0; i < NC; i++) close(srv[i]); client_proc(cli); }
    for (int i = 0; i < NC; i++) close(cli[i]);

    printf("api=%s handler=%s\n", api, blocking_handler ? "BLOCKING (client0 sleeps 200ms)" : "non-blocking");
    if (strcmp(api, "select") == 0) loop_select(srv); else loop_kqueue(srv);
    waitpid(pid, NULL, 0);
    printf("worst queueing latency:");
    for (int i = 0; i < NC; i++) printf(" client%d=%.1fms", i, worst_latency[i] / 1000.0);
    printf("\n");
    return 0;
}
