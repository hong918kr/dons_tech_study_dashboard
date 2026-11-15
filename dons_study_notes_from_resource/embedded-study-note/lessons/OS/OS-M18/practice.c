/* ENV: linux */
/* BUILD: -pthread */
/* OS-M18 추가 연습 — 한 번에 검증
 *   연습 1  EFD_SEMAPHORE 로 토큰 풀 (동시에 최대 N 개만 일한다)
 *   연습 2  timerfd 도 같은 epoll 에: read = 만료 횟수 (eventfd 처럼 카운터)
 *   연습 3  pidfd_send_signal: pid 재사용 경쟁 없는 kill, 회수 뒤엔 ESRCH
 *   연습 4  막힌 시그널 마스크는 fork + exec 를 건너간다 → 자식에서 되돌려야 한다
 *   연습 5  pidfd 없이: signalfd(SIGCHLD) 레코드 1 개 → waitpid(-1, WNOHANG) 루프로 전부 회수
 * build (Lima VM elinux): gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/pidfd.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int g_pass, g_total;
#define CHECK(c) do { g_total++; if (c) { g_pass++; printf("PASS  %s\n", #c); } \
                      else printf("FAIL  %s  (line %d)\n", #c, __LINE__); } while (0)

static void sleep_ms(long ms) {
    struct timespec d = { ms / 1000, (ms % 1000) * 1000000L };
    while (nanosleep(&d, &d) != 0 && errno == EINTR) { }
}
static void restore_mask(const sigset_t *old, int sig) { /* 쌓인 sig 를 먹고 복구 */
    sigset_t w;
    sigemptyset(&w);
    sigaddset(&w, sig);
    struct timespec zero = { 0, 0 };
    while (sigtimedwait(&w, NULL, &zero) > 0) { }
    pthread_sigmask(SIG_SETMASK, old, NULL);
}

/* ---------- 연습 1: EFD_SEMAPHORE 토큰 풀 ---------- */
typedef struct { int efd, iters; atomic_int active, max_active, done; } tokpool_t;
static void *tok_worker(void *arg) {
    tokpool_t *t = arg;
    for (int i = 0; i < t->iters; i++) {
        uint64_t one;
        while (read(t->efd, &one, sizeof one) < 0 && errno == EINTR) { }   /* 토큰 1 개 (없으면 잠듦) */
        int a = atomic_fetch_add(&t->active, 1) + 1;
        int m = atomic_load(&t->max_active);
        while (a > m && !atomic_compare_exchange_weak(&t->max_active, &m, a)) { }
        for (volatile int k = 0; k < 2000; k++) { }                       /* "일" */
        atomic_fetch_sub(&t->active, 1);
        atomic_fetch_add(&t->done, 1);
        one = 1;
        ssize_t w = write(t->efd, &one, sizeof one);                       /* 토큰 반납 */
        (void)w;
    }
    return NULL;
}
static void ex1(void) {
    printf("== 연습 1: EFD_SEMAPHORE 토큰 풀 (토큰 3, 스레드 8) ==\n");
    tokpool_t t = { .efd = eventfd(3, EFD_SEMAPHORE | EFD_CLOEXEC), .iters = 200 };
    atomic_init(&t.active, 0); atomic_init(&t.max_active, 0); atomic_init(&t.done, 0);
    pthread_t th[8];
    for (int i = 0; i < 8; i++) pthread_create(&th[i], NULL, tok_worker, &t);
    for (int i = 0; i < 8; i++) pthread_join(th[i], NULL);
    int fl = fcntl(t.efd, F_GETFL);
    fcntl(t.efd, F_SETFL, fl | O_NONBLOCK);               /* 남은 토큰 세기: 이제 NONBLOCK 으로 */
    int left = 0;
    uint64_t one;
    while (read(t.efd, &one, sizeof one) == (ssize_t)sizeof one) left++;
    printf("  info: max_active=%d\n", atomic_load(&t.max_active));
    CHECK(atomic_load(&t.done) == 1600);
    CHECK(atomic_load(&t.max_active) >= 1 && atomic_load(&t.max_active) <= 3);   /* 절대 3 을 넘지 않는다 */
    CHECK(left == 3);                                                             /* 토큰은 보존된다 */
    close(t.efd);
}

/* ---------- 연습 2: timerfd + eventfd 를 한 epoll 에 ---------- */
static void ex2(void) {
    printf("== 연습 2: timerfd 만료 횟수 = 카운터 ==\n");
    int ep = epoll_create1(EPOLL_CLOEXEC);
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    int efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    struct epoll_event e1 = { .events = EPOLLIN, .data.u32 = 1 }, e2 = { .events = EPOLLIN, .data.u32 = 2 };
    epoll_ctl(ep, EPOLL_CTL_ADD, tfd, &e1);
    epoll_ctl(ep, EPOLL_CTL_ADD, efd, &e2);
    struct itimerspec its = { .it_interval = { 0, 2000000L }, .it_value = { 0, 2000000L } };   /* 2 ms 주기 */
    timerfd_settime(tfd, 0, &its, NULL);
    sleep_ms(30);                                          /* 안 읽고 30 ms → 만료가 쌓인다 (15 번 근처) */
    uint64_t one = 1, exp = 0;
    ssize_t w = write(efd, &one, sizeof one);
    (void)w;
    struct epoll_event ev[2];
    int n = epoll_wait(ep, ev, 2, 1000);
    int saw = 0;
    for (int i = 0; i < n; i++) saw |= 1 << ev[i].data.u32;
    ssize_t r = read(tfd, &exp, sizeof exp);
    printf("  info: 30 ms 뒤 만료 %llu 번\n", (unsigned long long)exp);
    CHECK(n == 2 && saw == ((1 << 1) | (1 << 2)));         /* 둘 다 같은 epoll_wait 에서 */
    CHECK(r == (ssize_t)sizeof exp && exp >= 10);          /* 하한만 (30 ms ≥ 2 ms × 15) */
    CHECK(read(tfd, &exp, sizeof exp) == -1 && errno == EAGAIN);   /* 읽으면 0 으로 */
    close(tfd); close(efd); close(ep);
}

/* ---------- 연습 3: pidfd_send_signal ---------- */
static void ex3(void) {
    printf("== 연습 3: pidfd_send_signal — 회수 뒤엔 ESRCH ==\n");
    pid_t pid = fork();
    if (pid == 0) { alarm(5); for (;;) pause(); }
    int pfd = pidfd_open(pid, 0);
    CHECK(pfd >= 0 && pidfd_getpid(pfd) == pid);
    CHECK(pidfd_send_signal(pfd, SIGKILL, NULL, 0) == 0);
    struct pollfd p = { pfd, POLLIN, 0 };
    CHECK(poll(&p, 1, 2000) == 1);                        /* 죽으면 readable */
    CHECK(pidfd_send_signal(pfd, 0, NULL, 0) == 0);        /* 좀비(회수 전): 아직 "있다" */
    siginfo_t si;
    memset(&si, 0, sizeof si);
    CHECK(waitid(P_PIDFD, (id_t)pfd, &si, WEXITED) == 0 && si.si_code == CLD_KILLED && si.si_status == SIGKILL);
    errno = 0;
    CHECK(pidfd_send_signal(pfd, SIGKILL, NULL, 0) == -1 && errno == ESRCH);   /* 회수 뒤: 같은 pid 가 재사용돼도 안전 */
    errno = 0;
    CHECK(waitid(P_PIDFD, (id_t)pfd, &si, WEXITED | WNOHANG) == -1 && errno == ECHILD);
    int gp = pidfd_getpid(pfd), ge = errno;               /* 인자 평가 순서는 미지정 → 먼저 받아 둔다 */
    printf("  info: 회수 뒤 pidfd_getpid = %d (errno %s)\n", gp, strerror(ge));
    close(pfd);
}

/* ---------- 연습 4: 마스크는 exec 를 건너간다 ---------- */
static int run_sh(int reset_mask, const sigset_t *orig) {
    pid_t pid = fork();
    if (pid == 0) {
        if (reset_mask) sigprocmask(SIG_SETMASK, orig, NULL);
        alarm(5);
        execl("/bin/sh", "sh", "-c", "kill -TERM $$; exit 42", (char *)NULL);
        _exit(127);
    }
    int st = -1;
    waitpid(pid, &st, 0);
    return st;
}
static void ex4(void) {
    printf("== 연습 4: SIGTERM 을 막은 데몬이 exec 한 자식 ==\n");
    sigset_t set, old;
    sigemptyset(&set);
    sigaddset(&set, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &set, &old);
    int bug = run_sh(0, &old), fix = run_sh(1, &old);
    restore_mask(&old, SIGTERM);
    printf("  info: 안 되돌림 → %s %d | 되돌림 → %s %d\n",
           WIFEXITED(bug) ? "exit" : "signal", WIFEXITED(bug) ? WEXITSTATUS(bug) : WTERMSIG(bug),
           WIFEXITED(fix) ? "exit" : "signal", WIFEXITED(fix) ? WEXITSTATUS(fix) : WTERMSIG(fix));
    CHECK(WIFEXITED(bug) && WEXITSTATUS(bug) == 42);       /* 자식은 SIGTERM 을 못 받는다 (systemctl stop 무시) */
    CHECK(WIFSIGNALED(fix) && WTERMSIG(fix) == SIGTERM);   /* 되돌리면 정상적으로 죽는다 */
}

/* ---------- 연습 5: signalfd(SIGCHLD) + waitpid 루프 ---------- */
static int reap_all(int sfd, int *codes, int max, int *records) {
    struct signalfd_siginfo si[8];
    ssize_t r;
    *records = 0;
    while ((r = read(sfd, si, sizeof si)) > 0) *records += (int)(r / (ssize_t)sizeof si[0]);
    int n = 0, st;
    pid_t pid;
    while ((pid = waitpid(-1, &st, WNOHANG)) > 0)          /* 레코드 수가 아니라 "더 없을 때까지" */
        if (n < max) codes[n++] = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    return n;
}
static void ex5(void) {
    printf("== 연습 5: SIGCHLD 1 개 → waitpid 루프로 5 명 회수 ==\n");
    sigset_t set, old;
    sigemptyset(&set);
    sigaddset(&set, SIGCHLD);
    pthread_sigmask(SIG_BLOCK, &set, &old);
    int sfd = signalfd(-1, &set, SFD_NONBLOCK | SFD_CLOEXEC);
    int gate[2];
    if (pipe(gate) != 0) return;
    pid_t kid[5];
    for (int i = 0; i < 5; i++)
        if ((kid[i] = fork()) == 0) { char c; close(gate[1]); alarm(5); while (read(gate[0], &c, 1) < 0 && errno == EINTR) { } _exit(20 + i); }
    close(gate[0]);
    close(gate[1]);
    int zombies = 0;
    for (int t = 0; t < 2000 && zombies < 5; t++) {       /* 5 명 모두 좀비가 될 때까지 — WNOWAIT = 엿보기만, 회수 안 함 */
        zombies = 0;
        for (int k = 0; k < 5; k++) {
            siginfo_t si;
            memset(&si, 0, sizeof si);
            if (waitid(P_PID, (id_t)kid[k], &si, WEXITED | WNOHANG | WNOWAIT) == 0 && si.si_pid == kid[k]) zombies++;
        }
        if (zombies < 5) sleep_ms(1);
    }
    struct pollfd p = { sfd, POLLIN, 0 };
    poll(&p, 1, 1000);
    int codes[8] = { 0 }, records = 0;
    int n = reap_all(sfd, codes, 8, &records);
    int sum = 0;
    for (int i = 0; i < n; i++) sum += codes[i];
    printf("  info: SIGCHLD 레코드 %d 개, waitpid 루프 회수 %d 명\n", records, n);
    CHECK(n == 5 && sum == 20 + 21 + 22 + 23 + 24);        /* 레코드가 몇 개든 전부 회수 */
    CHECK(records >= 1 && records < 5);
    errno = 0;
    CHECK(waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD);
    close(sfd);
    restore_mask(&old, SIGCHLD);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(30);
    ex1();
    ex2();
    ex3();
    ex4();
    ex5();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
