/* OS-B07 추가 연습 — 한 번에 검증
 *
 *   연습 1  정리 후 "시그널로" 죽기: die_by_signal()  (SIGTERM 을 받고 exit(0) 하면 부모가 속는다)
 *   연습 2  두 번째 Ctrl-C 는 즉시 종료: SA_RESETHAND
 *   연습 3  write_all(): EINTR 과 부분 쓰기(short write)를 모두 처리
 *   연습 4  핸들러 없이 받기: 막아 두고 sigwait()
 *
 * 멈출 수 있는 것은 전부 자식에서 돌리고 부모가 시간 제한으로 지켜본다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static void hx_sigaction(int sig, void (*fn)(int), int flags) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = fn;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = flags;
    sigaction(sig, &sa, NULL);
}
static long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static void sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) { }
}
/* 자식 pid 를 timeout_ms 동안 기다린다. 반환: waitpid status, 시간 초과면 SIGKILL 후 -2 */
static int wait_child(pid_t c, int timeout_ms) {
    long t0 = now_ms();
    int st;
    for (;;) {
        pid_t r = waitpid(c, &st, WNOHANG);
        if (r == c) return st;
        if (r < 0 && errno != EINTR) return -1;
        if (now_ms() - t0 > timeout_ms) {
            kill(c, SIGKILL);
            while (waitpid(c, &st, 0) < 0 && errno == EINTR) { }
            return -2;
        }
        sleep_ms(2);
    }
}

/* ---------------- 연습 1 — 정리 후 시그널로 죽기 ---------------- */
static volatile sig_atomic_t g_term;
static void on_term(int s) { (void)s; g_term = 1; }

/* sig 의 기본 동작으로 이 프로세스를 끝낸다: 기본 동작 복원 → 막혀 있으면 풀기 → raise.
 * (기본 동작이 무시인 시그널이면 돌아올 수 있으니 마지막에 _exit(128+sig)) */
static void die_by_signal(int sig) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
    sigset_t s;
    sigemptyset(&s);
    sigaddset(&s, sig);
    sigprocmask(SIG_UNBLOCK, &s, NULL);
    raise(sig);
    _exit(128 + sig);
}

static void child_ex1(int fd, bool proper) {
    hx_sigaction(SIGTERM, on_term, SA_RESTART);
    kill(getpid(), SIGTERM);              /* "systemd 가 stop 을 보냈다" */
    while (!g_term) { }                   /* 메인 루프가 플래그를 본다 */
    (void)!write(fd, "cleanup\n", 8);     /* 임시 파일 삭제, 플래시 flush 등 */
    if (proper) die_by_signal(SIGTERM);
    _exit(0);                             /* 흔한 실수: 성공으로 끝남 */
}
static void ex1(void) {
    for (int proper = 0; proper <= 1; proper++) {
        int p[2];
        if (pipe(p) != 0) { CHECK(!"pipe"); return; }
        fflush(stdout);
        pid_t c = fork();
        if (c == 0) { close(p[0]); child_ex1(p[1], proper); }
        close(p[1]);
        int st = wait_child(c, 2000);
        char buf[16] = { 0 };
        ssize_t k = read(p[0], buf, sizeof buf - 1);
        close(p[0]);
        printf("ex1 %-14s: cleanup=%s  status: %s %d\n", proper ? "die_by_signal" : "exit(0)",
               k == 8 ? "yes" : "no", st >= 0 && WIFSIGNALED(st) ? "signaled" : "exited",
               st >= 0 && WIFSIGNALED(st) ? WTERMSIG(st) : (st >= 0 ? WEXITSTATUS(st) : st));
        CHECK(k == 8 && strcmp(buf, "cleanup\n") == 0);
        if (proper) CHECK(st >= 0 && WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM);
        else        CHECK(st >= 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0);   /* 부모는 "정상 종료" 로 본다 */
    }
}

/* ---------------- 연습 2 — 두 번째 Ctrl-C 는 즉시 종료 ---------------- */
static volatile sig_atomic_t g_int;
static void on_int(int s) { (void)s; g_int = 1; }

/* fn 을 "한 번만" 설치: 핸들러가 불리는 순간 커널이 기본 동작으로 되돌린다. */
static int install_once(int sig, void (*fn)(int)) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = fn;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND | SA_RESTART;
    return sigaction(sig, &sa, NULL);
}
static void ex2(void) {
    int p[2];
    if (pipe(p) != 0) { CHECK(!"pipe"); return; }
    fflush(stdout);
    pid_t c = fork();
    if (c == 0) {
        close(p[0]);
        if (install_once(SIGINT, on_int) != 0) _exit(10);
        raise(SIGINT);                                    /* 1번째: 우아한 종료 시작 */
        struct sigaction q;
        sigaction(SIGINT, NULL, &q);
        char r = (g_int && q.sa_handler == SIG_DFL) ? 'G' : 'B';
        (void)!write(p[1], &r, 1);
        raise(SIGINT);                                    /* 2번째: 기본 동작 = 즉시 종료 */
        _exit(0);                                         /* 여기 오면 틀린 것 */
    }
    close(p[1]);
    int st = wait_child(c, 2000);
    char r = 0;
    (void)!read(p[0], &r, 1);
    close(p[0]);
    printf("ex2 first SIGINT -> %s, second SIGINT -> %s %d\n", r == 'G' ? "flag set, reset to SIG_DFL" : "?",
           st >= 0 && WIFSIGNALED(st) ? "killed by" : "exit", st >= 0 && WIFSIGNALED(st) ? WTERMSIG(st) : st);
    CHECK(r == 'G');
    CHECK(st >= 0 && WIFSIGNALED(st) && WTERMSIG(st) == SIGINT);
}

/* ---------------- 연습 3 — write_all: EINTR + 부분 쓰기 ---------------- */
/* buf 의 n 바이트를 전부 쓴다. EINTR 이면 다시, 일부만 쓰였으면 나머지를 이어서.
 * 성공하면 n, 진짜 에러면 -1. */
static ssize_t write_all(int fd, const void *buf, size_t n) {
    const char *p = buf;
    size_t left = n;
    while (left > 0) {
        ssize_t k = write(fd, p, left);
        if (k < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += k;
        left -= (size_t)k;
    }
    return (ssize_t)n;
}

static volatile sig_atomic_t g_alrm;
static void on_alrm(int s) { (void)s; g_alrm = g_alrm + 1; }
static uint32_t fnv1a(uint32_t h, const unsigned char *p, size_t n) {
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}
#define EX3_N (1u << 20)
static void ex3(void) {
    static unsigned char data[EX3_N];
    for (size_t i = 0; i < EX3_N; i++) data[i] = (unsigned char)(i * 131u + (i >> 9));
    uint32_t want = fnv1a(2166136261u, data, EX3_N);
    int p[2];
    if (pipe(p) != 0) { CHECK(!"pipe"); return; }
    fflush(stdout);
    pid_t c = fork();
    if (c == 0) {                                        /* 느린 독자: 4 KB 씩, 가끔 쉰다 */
        close(p[1]);
        alarm(5);                                        /* 독자 쪽 워치독 */
        unsigned char b[4096];
        uint32_t h = 2166136261u;
        size_t tot = 0;
        ssize_t k;
        int i = 0;
        while ((k = read(p[0], b, sizeof b)) > 0) {
            h = fnv1a(h, b, (size_t)k);
            tot += (size_t)k;
            if (++i % 32 == 0) sleep_ms(1);
        }
        _exit(tot == EX3_N && h == want ? 0 : 1);
    }
    close(p[0]);
    hx_sigaction(SIGPIPE, SIG_IGN, 0);                   /* 독자가 죽으면 EPIPE 로 받기 */
    hx_sigaction(SIGALRM, on_alrm, 0);                   /* SA_RESTART 없음 → EINTR/부분 쓰기 유발 */
    struct itimerval it = { { 0, 500 }, { 0, 500 } };    /* 0.5 ms 마다 */
    setitimer(ITIMER_REAL, &it, NULL);
    ssize_t first = write(p[1], data, EX3_N);            /* 맨 write 한 번이 얼마나 쓰나 */
    int first_errno = errno;
    ssize_t r = first > 0 ? write_all(p[1], data + first, EX3_N - (size_t)first) : write_all(p[1], data, EX3_N);
    struct itimerval off;
    memset(&off, 0, sizeof off);
    setitimer(ITIMER_REAL, &off, NULL);
    close(p[1]);
    int st = wait_child(c, 5000);
    printf("ex3 plain write(1 MiB) returned %zd%s; write_all finished the rest; SIGALRM x%d during transfer\n",
           first, first < 0 && first_errno == EINTR ? " (EINTR)" : "", (int)g_alrm);
    CHECK(r >= 0);
    CHECK(st >= 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0);   /* 독자가 1 MiB 를 그대로 받았다 */
    CHECK(g_alrm > 0);
    hx_sigaction(SIGALRM, SIG_DFL, 0);
}

/* ---------------- 연습 4 — sigwait: 핸들러 없는 동기 수신 ---------------- */
static volatile sig_atomic_t g_handler_ran;
static void on_any(int s) { (void)s; g_handler_ran = 1; }

/* set 의 시그널 중 펜딩된 것 하나를 꺼내 번호를 돌려준다 (호출자가 set 을 막아 둔 상태). 에러면 -1. */
static int next_signal(const sigset_t *set) {
    int sig = 0;
    if (sigwait(set, &sig) != 0) return -1;              /* sigwait 는 errno 대신 에러 번호를 반환 */
    return sig;
}
static void ex4(void) {
    hx_sigaction(SIGUSR1, on_any, 0);
    hx_sigaction(SIGTERM, on_any, 0);
    sigset_t set, old, pend;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    sigaddset(&set, SIGTERM);
    sigprocmask(SIG_BLOCK, &set, &old);
    kill(getpid(), SIGUSR1);
    kill(getpid(), SIGUSR1);                             /* 합쳐진다 */
    kill(getpid(), SIGTERM);
    int a = next_signal(&set);
    int b = next_signal(&set);
    sigpending(&pend);
    bool none_left = !sigismember(&pend, SIGUSR1) && !sigismember(&pend, SIGTERM);
    sigprocmask(SIG_SETMASK, &old, NULL);
    printf("ex4 sigwait got %d then %d (USR1=%d TERM=%d), pending after: %s, handler ran: %d\n",
           a, b, SIGUSR1, SIGTERM, none_left ? "none" : "some", (int)g_handler_ran);
    CHECK((a == SIGUSR1 && b == SIGTERM) || (a == SIGTERM && b == SIGUSR1));
    CHECK(none_left);                                    /* USR1 2번 → 1개만 펜딩이었다 */
    CHECK(g_handler_ran == 0);                           /* sigwait 로 꺼낸 시그널은 핸들러를 안 탄다 */
    hx_sigaction(SIGUSR1, SIG_DFL, 0);
    hx_sigaction(SIGTERM, SIG_DFL, 0);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    ex1();
    ex2();
    ex3();
    ex4();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
