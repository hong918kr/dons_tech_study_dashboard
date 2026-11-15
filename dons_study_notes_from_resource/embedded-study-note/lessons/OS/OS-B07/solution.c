/* OS-B07 시그널: 핸들러, async-signal-safe, self-pipe
 *
 *   Part A  sigaction 으로 핸들러 설치 (sa_mask, sa_flags), 표준 시그널 합쳐짐(coalescing),
 *           SA_RESTART 유무에 따른 blocking read 의 EINTR, 그리고 EINTR 재시도 래퍼.
 *   Part B  핸들러 안에서는 async-signal-safe 함수만: printf 없이 숫자를 문자열로 바꾸고
 *           write() 한 번으로 로그를 남기며 errno 를 보존한다.
 *   Part C  이벤트를 메인 루프로 넘기는 두 방법:
 *           (1) volatile sig_atomic_t 플래그 + sigsuspend (검사↔잠들기 사이 경쟁 없음)
 *           (2) self-pipe trick: 핸들러는 non-blocking pipe 에 1바이트, 메인은 poll() →
 *               SIGTERM/SIGINT 는 종료 요청, SIGCHLD 는 waitpid(WNOHANG) 루프로 전부 회수.
 *
 * 체크는 매 실행 항상 성립하는 것만 본다 (시그널은 raise/kill 로 자기 자신에게 보낸다).
 * 멈출 수 있는 테스트(pause, 꽉 찬 pipe 에 쓰기)는 자식 프로세스에서 돌리고 부모가
 * waitpid(WNOHANG) 로 지켜보다 시간이 지나면 SIGKILL 한다 → 틀린 코드는 멈추지 않고 FAIL.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAXSIG   32           /* macOS NSIG = 32. 시그널 번호 1..31 */
#define MAX_KIDS 16

/* ---------- 하네스 공용: 테스트 핸들러와 기록 ---------- */
static volatile sig_atomic_t g_cnt[MAXSIG];        /* 시그널별 핸들러 실행 횟수 */
static volatile sig_atomic_t g_fallback[MAXSIG];   /* 하네스 기본 핸들러 실행 횟수 */
static void h_count(int sig)    { g_cnt[sig] = g_cnt[sig] + 1; }
static void h_fallback(int sig) { g_fallback[sig] = g_fallback[sig] + 1; }

/* 하네스 전용 설치 함수 — 학생 install() 과 독립 (각 Part 의 실패가 섞이지 않게) */
static void hx_sigaction(int sig, void (*fn)(int), int flags) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = fn;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = flags;
    sigaction(sig, &sa, NULL);
}

/* ======================= Part A — sigaction, 합쳐짐, EINTR ======================= */

/* sig 에 fn 을 설치한다. flags 는 sa_flags 그대로 (0, SA_RESTART, ...).
 * mask 가 NULL 이 아니면 핸들러가 도는 동안 그 시그널들도 막는다 (sa_mask). NULL 이면 빈 집합.
 * 성공 0, 실패 -1 (errno 는 sigaction 이 남긴 값). */
static int install(int sig, void (*fn)(int), int flags, const sigset_t *mask) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = fn;
    if (mask) sa.sa_mask = *mask;
    else      sigemptyset(&sa.sa_mask);
    sa.sa_flags = flags;
    return sigaction(sig, &sa, NULL);
}

/* read() 가 시그널 때문에 -1/EINTR 로 끝나면 다시 부른다. 그 외 결과는 그대로 돌려준다. */
static ssize_t read_eintr(int fd, void *buf, size_t n) {
    ssize_t r;
    do {
        r = read(fd, buf, n);
    } while (r < 0 && errno == EINTR);
    return r;
}

/* ======================= Part B — async-signal-safe 로그 ======================= */

/* v 를 10진 문자열로 dst 에 쓰고 길이를 돌려준다 (NUL 은 붙이지 않는다).
 * snprintf 금지 — 핸들러에서 부를 함수다. dst 는 최소 20바이트. */
static size_t fmt_ulong(char *dst, unsigned long v) {
    char tmp[20];
    size_t n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v != 0);
    for (size_t i = 0; i < n; i++) dst[i] = tmp[n - 1 - i];
    return n;
}

static int g_log_fd = -1;                  /* 로그를 쓸 fd (하네스가 정함) */
static volatile sig_atomic_t g_log_count;  /* 지금까지 남긴 로그 수 */

/* 핸들러: "signal <번호> #<순번>\n" 을 write() 한 번으로 g_log_fd 에 쓴다.
 * printf/malloc/strlen 이외의 비안전 함수 금지. 들어올 때의 errno 를 그대로 돌려놓고 나간다. */
static void log_handler(int sig) {
    int saved = errno;
    g_log_count = g_log_count + 1;
    char buf[64];
    size_t n = 0;
    const char *p1 = "signal ";
    while (*p1) buf[n++] = *p1++;
    n += fmt_ulong(buf + n, (unsigned long)sig);
    buf[n++] = ' ';
    buf[n++] = '#';
    n += fmt_ulong(buf + n, (unsigned long)g_log_count);
    buf[n++] = '\n';
    (void)!write(g_log_fd, buf, n);        /* 실패해도 핸들러에서 할 수 있는 일은 없다 */
    errno = saved;
}

/* ======================= Part C — 메인 루프로 이벤트 전달 ======================= */

/* (1) 플래그 방식 */
static volatile sig_atomic_t g_flag;
static void flag_handler(int sig) { (void)sig; g_flag = 1; }

static void (*g_race_hook)(void);           /* 하네스가 "검사 직후, 잠들기 직전" 에 끼워 넣는 코드 */
static void race_window(void) { if (g_race_hook) g_race_hook(); }

/* 깨진 버전 (하네스 제공): 검사와 pause() 사이에 시그널이 오면 영원히 잔다. */
static int wait_flag_pause(void) {
    while (!g_flag) {
        race_window();
        pause();
    }
    g_flag = 0;
    return 0;
}

/* g_flag 가 설 때까지 기다린 뒤 내리고 0 을 돌려준다. sig 는 g_flag 를 세우는 시그널.
 * 요구: 검사와 잠들기 사이(race_window() 위치)에 시그널이 와도 놓치지 않는다.
 *       돌아갈 때 시그널 마스크는 들어올 때와 같아야 한다.
 * 반드시 while 루프 안에서 g_flag 검사 직후 race_window() 를 한 번 부를 것 (테스트가 여기에 시그널을 넣는다). */
static int wait_flag(int sig) {
    sigset_t blk, old;
    sigemptyset(&blk);
    sigaddset(&blk, sig);
    if (sigprocmask(SIG_BLOCK, &blk, &old) != 0) return -1;
    while (!g_flag) {
        race_window();
        sigsuspend(&old);                   /* 마스크를 old 로 바꾸며 잠들기 = 원자적. 핸들러 후 복귀 */
    }
    g_flag = 0;
    sigprocmask(SIG_SETMASK, &old, NULL);
    return 0;
}

/* (2) self-pipe */
static int g_sp[2] = { -1, -1 };            /* [0] 읽기(메인 루프), [1] 쓰기(핸들러) */

/* 핸들러: errno 를 보존하며 시그널 번호 1바이트를 g_sp[1] 에 쓴다.
 * pipe 가 꽉 차서 실패(EAGAIN)해도 그냥 넘어간다 — 이미 읽지 않은 바이트가 있으니 깨어날 것이다. */
static void selfpipe_handler(int sig) {
    int saved = errno;
    unsigned char b = (unsigned char)sig;
    (void)!write(g_sp[1], &b, 1);
    errno = saved;
}

/* self-pipe 준비:
 *   1) pipe() 를 만들고 두 끝 모두 O_NONBLOCK + FD_CLOEXEC 를 켠다 → g_sp
 *   2) SIGINT, SIGTERM, SIGCHLD 에 selfpipe_handler 를 install() 로 설치한다.
 *      flags: 셋 다 SA_RESTART, SIGCHLD 는 추가로 SA_NOCLDSTOP.
 *      sa_mask: 세 시그널 모두 (핸들러끼리 중첩되지 않게).
 * 성공 0, 실패 -1. 이미 열려 있으면 닫고 새로 연다. */
static int selfpipe_setup(void) {
    if (g_sp[0] >= 0) { close(g_sp[0]); close(g_sp[1]); g_sp[0] = g_sp[1] = -1; }
    int p[2];
    if (pipe(p) != 0) return -1;
    for (int i = 0; i < 2; i++) {
        int fl = fcntl(p[i], F_GETFL);
        int fd = fcntl(p[i], F_GETFD);
        if (fl < 0 || fd < 0 ||
            fcntl(p[i], F_SETFL, fl | O_NONBLOCK) != 0 ||
            fcntl(p[i], F_SETFD, fd | FD_CLOEXEC) != 0) {
            close(p[0]); close(p[1]);
            return -1;
        }
    }
    g_sp[0] = p[0];
    g_sp[1] = p[1];
    sigset_t m;
    sigemptyset(&m);
    sigaddset(&m, SIGINT);
    sigaddset(&m, SIGTERM);
    sigaddset(&m, SIGCHLD);
    if (install(SIGINT,  selfpipe_handler, SA_RESTART, &m) != 0) return -1;
    if (install(SIGTERM, selfpipe_handler, SA_RESTART, &m) != 0) return -1;
    if (install(SIGCHLD, selfpipe_handler, SA_RESTART | SA_NOCLDSTOP, &m) != 0) return -1;
    return 0;
}

typedef struct {
    int   stop;                 /* SIGTERM/SIGINT 바이트를 받으면 그 시그널 번호 */
    int   nbytes;               /* 지금까지 self-pipe 에서 읽은 바이트 수 */
    int   seen[MAXSIG];         /* 시그널 번호별 바이트 수 */
    int   nreaped;              /* 회수한 자식 수 */
    pid_t pid[MAX_KIDS];        /* 회수한 자식 pid 와 waitpid status (회수한 순서) */
    int   status[MAX_KIDS];
} loop_t;

/* 이벤트 루프 한 바퀴:
 *   poll(g_sp[0], timeout_ms) — 시간 초과면 0. poll 이 EINTR 이면 0 (에러 아님, 다음 바퀴에 처리).
 *   읽을 게 있으면 EAGAIN 이 날 때까지 전부 읽고(drain), 바이트마다 nbytes/seen 을 올리고
 *     SIGTERM/SIGINT → L->stop = 그 번호
 *     SIGCHLD        → waitpid(-1, &st, WNOHANG) 를 0/-1 이 나올 때까지 반복해 전부 회수·기록
 *   이번 바퀴에 처리한 바이트 수를 돌려준다. 진짜 에러면 -1. */
static void reap_all(loop_t *L) {
    int st;
    pid_t p;
    while ((p = waitpid(-1, &st, WNOHANG)) > 0) {
        if (L->nreaped < MAX_KIDS) {
            L->pid[L->nreaped] = p;
            L->status[L->nreaped] = st;
        }
        L->nreaped++;
    }
}

static int loop_once(int timeout_ms, loop_t *L) {
    struct pollfd pf = { .fd = g_sp[0], .events = POLLIN, .revents = 0 };
    int r = poll(&pf, 1, timeout_ms);
    if (r < 0) return errno == EINTR ? 0 : -1;
    if (r == 0) return 0;
    int got = 0;
    for (;;) {
        unsigned char buf[64];
        ssize_t k = read(g_sp[0], buf, sizeof buf);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) break;                  /* EAGAIN = 다 읽음 */
        for (ssize_t i = 0; i < k; i++) {
            int s = buf[i];
            got++;
            L->nbytes++;
            if (s > 0 && s < MAXSIG) L->seen[s]++;
            if (s == SIGTERM || s == SIGINT) L->stop = s;
            if (s == SIGCHLD) reap_all(L);
        }
    }
    return got;
}

/* ============================== 테스트 하네스 ============================== */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static void sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) { }
}
static long now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
static void arm_timer_ms(int ms) {                     /* 1회용 SIGALRM */
    struct itimerval it;
    memset(&it, 0, sizeof it);
    it.it_value.tv_sec = ms / 1000;
    it.it_value.tv_usec = (ms % 1000) * 1000;
    setitimer(ITIMER_REAL, &it, NULL);
}
static void reset_signals_for_child(void) {
    int sigs[] = { SIGUSR1, SIGUSR2, SIGALRM, SIGTERM, SIGINT, SIGCHLD };
    for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++) hx_sigaction(sigs[i], SIG_DFL, 0);
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, NULL);
}

/* fn 을 자식 프로세스에서 돌린다. 반환: 자식 종료 코드(0..255), 시그널로 죽으면 1000+번호,
 * timeout_ms 안에 안 끝나면 SIGKILL 후 -2 (= 멈춤). */
#define HUNG (-2)
static int run_in_child(int (*fn)(void), int timeout_ms) {
    fflush(stdout);
    pid_t c = fork();
    if (c < 0) return -1;
    if (c == 0) { reset_signals_for_child(); _exit(fn() & 0xff); }
    long t0 = now_ms();
    int st;
    for (;;) {
        pid_t r = waitpid(c, &st, WNOHANG);
        if (r == c) break;
        if (r < 0 && errno != EINTR) return -1;
        if (now_ms() - t0 > timeout_ms) {
            kill(c, SIGKILL);
            while (waitpid(c, &st, 0) < 0 && errno == EINTR) { }
            return HUNG;
        }
        sleep_ms(2);
    }
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    return 1000 + WTERMSIG(st);
}
static const char *rc_name(int rc) {
    static char b[32];
    if (rc == HUNG) return "HUNG(watchdog)";
    if (rc == 0) return "OK";
    snprintf(b, sizeof b, rc >= 1000 ? "killed by sig %d" : "exit %d", rc >= 1000 ? rc - 1000 : rc);
    return b;
}

/* ---- Part A 도우미 ---- */
static volatile sig_atomic_t g_log[16], g_nlog;
static volatile sig_atomic_t g_raise_inner;
static void logc(int c) { if (g_nlog < 16) { g_log[g_nlog] = c; g_nlog = g_nlog + 1; } }
static void h_outer(int sig) { (void)sig; logc('<'); if (g_raise_inner) raise(SIGUSR2); logc('>'); }
static void h_inner(int sig) { (void)sig; logc('{'); logc('}'); }
static bool log_is(const char *s) {
    if ((size_t)g_nlog != strlen(s)) return false;
    for (int i = 0; i < g_nlog; i++) if (g_log[i] != s[i]) return false;
    return true;
}
static void log_print(char *out) { int i; for (i = 0; i < g_nlog; i++) out[i] = (char)g_log[i]; out[i] = 0; }

/* 200 ms 뒤 1바이트를 쓰는 자식을 만들고, 30 ms 뒤 SIGALRM 이 오게 한 다음 read 한다.
 * use_retry: read_eintr 사용 여부. 반환: read 결과, *err 에 errno. */
static ssize_t timed_read(bool use_retry, int *err, long *ms) {
    int p[2];
    if (pipe(p) != 0) return -100;
    fflush(stdout);
    pid_t c = fork();
    if (c == 0) { reset_signals_for_child(); close(p[0]); sleep_ms(200); (void)!write(p[1], "x", 1); _exit(0); }
    close(p[1]);
    char b = 0;
    long t0 = now_ms();
    arm_timer_ms(30);
    errno = 0;
    ssize_t r = use_retry ? read_eintr(p[0], &b, 1) : read(p[0], &b, 1);
    *err = errno;
    *ms = now_ms() - t0;
    arm_timer_ms(0);
    close(p[0]);
    while (waitpid(c, NULL, 0) < 0 && errno == EINTR) { }
    return r;
}

static void test_part_a(void) {
    for (int s = 1; s < MAXSIG; s++) g_cnt[s] = 0;

    /* A-1 설치하고 자신에게 보내기 */
    CHECK(install(SIGUSR1, h_count, 0, NULL) == 0);
    raise(SIGUSR1);
    CHECK(g_cnt[SIGUSR1] == 1);
    struct sigaction q;
    sigaction(SIGUSR1, NULL, &q);
    CHECK(q.sa_handler == h_count && (q.sa_flags & SA_RESTART) == 0);
    CHECK(install(SIGUSR1, h_count, SA_RESTART, NULL) == 0);
    sigaction(SIGUSR1, NULL, &q);
    CHECK(q.sa_handler == h_count && (q.sa_flags & SA_RESTART) != 0);

    /* A-2 막혀 있는 동안 같은 시그널 2번 → 펜딩 1개 → 핸들러 1번 */
    sigset_t blk, old, pend;
    sigemptyset(&blk);
    sigaddset(&blk, SIGUSR1);
    sigprocmask(SIG_BLOCK, &blk, &old);
    int before = g_cnt[SIGUSR1];
    raise(SIGUSR1);
    raise(SIGUSR1);
    sigpending(&pend);
    bool pending = sigismember(&pend, SIGUSR1) == 1;
    int while_blocked = g_cnt[SIGUSR1] - before;
    sigprocmask(SIG_SETMASK, &old, NULL);
    int delivered = g_cnt[SIGUSR1] - before;
    printf("A-2 coalescing: raised 2x while blocked -> pending=%d, handler ran %d time(s) after unblock\n",
           (int)pending, delivered);
    CHECK(pending && while_blocked == 0);
    CHECK(delivered == 1);

    /* A-3 sa_mask: USR1 핸들러 안에서 USR2 를 raise */
    install(SIGUSR2, h_inner, 0, NULL);
    g_raise_inner = 1;
    char seq[17];
    g_nlog = 0;
    install(SIGUSR1, h_outer, 0, NULL);               /* sa_mask 비어 있음 → 중첩 */
    raise(SIGUSR1);
    log_print(seq);
    printf("A-3 sa_mask={}     : %s\n", seq);
    CHECK(log_is("<{}>"));
    sigset_t m2;
    sigemptyset(&m2);
    sigaddset(&m2, SIGUSR2);
    g_nlog = 0;
    install(SIGUSR1, h_outer, 0, &m2);                /* sa_mask = {USR2} → USR1 핸들러가 끝난 뒤 */
    raise(SIGUSR1);
    log_print(seq);
    printf("A-3 sa_mask={USR2} : %s\n", seq);
    CHECK(log_is("<>{}"));
    g_raise_inner = 0;

    /* A-4 blocking read 도중 SIGALRM: SA_RESTART 없음 → EINTR, 있음 → 재시작 */
    int err;
    long ms;
    g_cnt[SIGALRM] = 0;
    install(SIGALRM, h_count, 0, NULL);
    ssize_t r = timed_read(false, &err, &ms);
    printf("A-4 read, no SA_RESTART : ret=%zd errno=%s after %ld ms\n", r, r < 0 ? strerror(err) : "-", ms);
    CHECK(r == -1 && err == EINTR);
    CHECK(g_cnt[SIGALRM] == 1);
    install(SIGALRM, h_count, SA_RESTART, NULL);
    r = timed_read(false, &err, &ms);
    printf("A-4 read, SA_RESTART    : ret=%zd after %ld ms\n", r, ms);
    CHECK(r == 1 && g_cnt[SIGALRM] == 2);
    install(SIGALRM, h_count, 0, NULL);
    r = timed_read(true, &err, &ms);
    printf("A-4 read_eintr, no SA_RESTART: ret=%zd after %ld ms\n", r, ms);
    CHECK(r == 1 && g_cnt[SIGALRM] == 3);
    hx_sigaction(SIGALRM, h_fallback, SA_RESTART);
}

static void test_part_b(void) {
    char b[24];
    size_t n;
    n = fmt_ulong(b, 0);           CHECK(n == 1 && memcmp(b, "0", 1) == 0);
    n = fmt_ulong(b, 7);           CHECK(n == 1 && memcmp(b, "7", 1) == 0);
    n = fmt_ulong(b, 1000);        CHECK(n == 4 && memcmp(b, "1000", 4) == 0);
    n = fmt_ulong(b, ULONG_MAX);   CHECK(n == 20 && memcmp(b, "18446744073709551615", 20) == 0);

    int p[2];
    if (pipe(p) != 0) { CHECK(!"pipe"); return; }
    fcntl(p[0], F_SETFL, fcntl(p[0], F_GETFL) | O_NONBLOCK);
    g_log_fd = p[1];
    g_log_count = 0;
    hx_sigaction(SIGUSR1, log_handler, 0);
    hx_sigaction(SIGUSR2, log_handler, 0);
    raise(SIGUSR1);
    raise(SIGUSR2);
    char got[128] = { 0 };
    ssize_t k = read(p[0], got, sizeof got - 1);
    char want[64];
    snprintf(want, sizeof want, "signal %d #1\nsignal %d #2\n", SIGUSR1, SIGUSR2);
    char shown[128];
    for (int i = 0; i < 128; i++) shown[i] = got[i] == '\n' ? '|' : got[i];
    printf("B log pipe got: \"%s\"\n", shown);
    CHECK(k > 0 && strcmp(got, want) == 0);

    /* 로그 fd 가 깨졌을 때(write 가 EBADF) 메인의 errno 를 망치면 안 된다 */
    close(p[1]);
    g_log_fd = p[1];               /* 닫힌 fd */
    errno = ERANGE;
    raise(SIGUSR1);
    CHECK(errno == ERANGE);
    CHECK(g_log_count == 3);
    close(p[0]);
    g_log_fd = -1;
    hx_sigaction(SIGUSR1, h_fallback, SA_RESTART);
    hx_sigaction(SIGUSR2, h_fallback, SA_RESTART);
}

/* ---- Part C 자식 함수들 (run_in_child 로 실행) ---- */
static void hook_raise_usr1(void) { g_race_hook = NULL; raise(SIGUSR1); }
static int child_pause_race(void) {                 /* 깨진 버전 + 경쟁 주입 → 멈춰야 정상 */
    hx_sigaction(SIGUSR1, flag_handler, 0);
    g_flag = 0;
    g_race_hook = hook_raise_usr1;
    return wait_flag_pause();
}
static int child_wait_race(void) {                  /* 내 버전 + 경쟁 주입 → 바로 돌아와야 */
    hx_sigaction(SIGUSR1, flag_handler, 0);
    g_flag = 0;
    g_race_hook = hook_raise_usr1;
    if (wait_flag(SIGUSR1) != 0) return 1;
    if (g_flag != 0) return 2;                      /* 내리고 돌아와야 한다 */
    sigset_t cur;
    sigprocmask(SIG_SETMASK, NULL, &cur);
    if (sigismember(&cur, SIGUSR1)) return 3;       /* 마스크 복원 */
    g_flag = 1;                                     /* 이미 와 있던 경우 → 기다리지 않음 */
    if (wait_flag(SIGUSR1) != 0 || g_flag != 0) return 4;
    return 0;
}
static int child_flood(void) {                      /* 아무도 안 읽는 self-pipe 에 10만 번 */
    if (selfpipe_setup() != 0) return 10;
    hx_sigaction(SIGUSR1, selfpipe_handler, SA_RESTART);
    for (int i = 0; i < 100000; i++) raise(SIGUSR1);
    long n = 0;
    unsigned char b[4096];
    ssize_t k;
    while ((k = read(g_sp[0], b, sizeof b)) > 0) n += k;
    if (n <= 0) return 11;                          /* 핸들러가 아무것도 안 씀 */
    if (n >= 100000) return 12;                     /* pipe 가 안 찼다? (용량이 10만 이상일 리 없음) */
    return 0;
}

static void drain_selfpipe(void) {
    unsigned char b[256];
    if (g_sp[0] < 0) return;
    while (read(g_sp[0], b, sizeof b) > 0) { }
}

static void test_part_c(void) {
    /* C-1 플래그 + 경쟁 주입 */
    int rc_broken = run_in_child(child_pause_race, 300);
    int rc_mine = run_in_child(child_wait_race, 2000);
    printf("C-1 flag race: pause() version=%s, wait_flag()=%s\n", rc_name(rc_broken), rc_name(rc_mine));
    CHECK(rc_broken == HUNG);                       /* 하네스의 깨진 버전은 정말 멈춘다 */
    CHECK(rc_mine == 0);

    /* C-2 self-pipe 준비 */
    CHECK(selfpipe_setup() == 0);
    bool nb = true, ce = true;
    for (int i = 0; i < 2; i++) {
        if (g_sp[i] < 0 || !(fcntl(g_sp[i], F_GETFL) & O_NONBLOCK)) nb = false;
        if (g_sp[i] < 0 || !(fcntl(g_sp[i], F_GETFD) & FD_CLOEXEC)) ce = false;
    }
    CHECK(nb);
    CHECK(ce);
    int ok_inst = 0;
    int sigs[3] = { SIGINT, SIGTERM, SIGCHLD };
    for (int i = 0; i < 3; i++) {
        struct sigaction q;
        sigaction(sigs[i], NULL, &q);
        bool flags_ok = (q.sa_flags & SA_RESTART) &&
                        (sigs[i] != SIGCHLD || (q.sa_flags & SA_NOCLDSTOP));
        bool mask_ok = sigismember(&q.sa_mask, SIGINT) && sigismember(&q.sa_mask, SIGTERM) &&
                       sigismember(&q.sa_mask, SIGCHLD);
        if (q.sa_handler == selfpipe_handler && flags_ok && mask_ok) ok_inst++;
    }
    CHECK(ok_inst == 3);
    if (g_sp[0] < 0 || g_sp[1] < 0 || !(fcntl(g_sp[0], F_GETFL) & O_NONBLOCK)) {
        printf("SKIP 8 self-pipe checks (Part C): pipe missing or read end blocking (a drain would hang)\n");
        g_total += 8;
        return;
    }

    /* C-3 핸들러가 1바이트 */
    hx_sigaction(SIGUSR1, selfpipe_handler, SA_RESTART);
    drain_selfpipe();
    raise(SIGUSR1);
    unsigned char byte = 0;
    CHECK(read(g_sp[0], &byte, 1) == 1 && byte == SIGUSR1);

    /* C-4 pipe 가 꽉 찼을 때 핸들러의 write 는 EAGAIN → errno 보존 */
    long filled = 0;
    if (fcntl(g_sp[1], F_GETFL) & O_NONBLOCK) {    /* 쓰기 끝이 blocking 이면 꽉 찬 pipe 에 핸들러가 영원히 막힌다 */
        while (write(g_sp[1], "x", 1) == 1) filled++;
        errno = ERANGE;
        raise(SIGUSR1);
        CHECK(errno == ERANGE);
        drain_selfpipe();
        printf("C-4 self-pipe capacity (1-byte non-blocking writes until EAGAIN): %ld\n", filled);
    } else {
        CHECK(!"C-4 skipped: write end is blocking");
    }

    /* C-5 아무도 안 읽는 동안 10만 번 → 핸들러가 막히지 않아야 (자식에서) */
    int rc_flood = run_in_child(child_flood, 3000);
    printf("C-5 flood 100000 raises with nobody reading: %s\n", rc_name(rc_flood));
    CHECK(rc_flood == 0);
    drain_selfpipe();                               /* 자식 종료 SIGCHLD 바이트 버리기 */

    /* C-6 SIGTERM → 루프가 종료 요청을 본다 */
    loop_t L;
    memset(&L, 0, sizeof L);
    kill(getpid(), SIGTERM);
    int r = loop_once(100, &L);
    CHECK(r >= 1 && L.stop == SIGTERM && L.seen[SIGTERM] == 1);

    /* C-7 SIGCHLD: 자식 3개가 모두 좀비가 될 때까지 SIGCHLD 를 막아 둔다 → 바이트 1개, 회수 3 */
    memset(&L, 0, sizeof L);
    drain_selfpipe();
    sigset_t blk, old;
    sigemptyset(&blk);
    sigaddset(&blk, SIGCHLD);
    sigprocmask(SIG_BLOCK, &blk, &old);
    pid_t kid[3];
    fflush(stdout);
    for (int i = 0; i < 3; i++) {
        kid[i] = fork();
        if (kid[i] == 0) _exit(i + 1);
    }
    for (int i = 0; i < 3; i++) {                   /* WNOWAIT: 좀비인지 보기만 하고 회수는 안 함 */
        for (int t = 0; t < 2000; t++) {
            siginfo_t si;
            memset(&si, 0, sizeof si);
            if (waitid(P_PID, (id_t)kid[i], &si, WEXITED | WNOWAIT | WNOHANG) == 0 && si.si_pid == kid[i]) break;
            sleep_ms(1);
        }
    }
    sigprocmask(SIG_SETMASK, &old, NULL);           /* 여기서 핸들러 1번 */
    long t0 = now_ms();
    while (L.nreaped < 3 && now_ms() - t0 < 2000) {
        if (loop_once(50, &L) < 0) break;
    }
    int match = 0;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < L.nreaped && j < MAX_KIDS; j++)
            if (L.pid[j] == kid[i] && WIFEXITED(L.status[j]) && WEXITSTATUS(L.status[j]) == i + 1) match++;
    printf("C-7 3 children exited while SIGCHLD blocked -> SIGCHLD bytes=%d, reaped=%d\n",
           L.seen[SIGCHLD], L.nreaped);
    CHECK(L.seen[SIGCHLD] == 1);                    /* 표준 시그널은 쌓이지 않는다 */
    CHECK(L.nreaped == 3 && match == 3);
    errno = 0;
    CHECK(waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD);   /* 좀비 0 */
    for (int i = 0; i < 3; i++) while (waitpid(kid[i], NULL, 0) < 0 && errno == EINTR) { }  /* 틀린 구현 뒷정리 */

    /* C-8 poll 도중 시그널: poll 은 SA_RESTART 여도 EINTR → 루프는 에러로 보면 안 된다 */
    memset(&L, 0, sizeof L);
    hx_sigaction(SIGALRM, selfpipe_handler, SA_RESTART);
    arm_timer_ms(30);
    int r1 = loop_once(500, &L);
    int r2 = loop_once(0, &L);
    printf("C-8 SIGALRM during poll: loop_once -> %d then %d, SIGALRM bytes=%d\n", r1, r2, L.seen[SIGALRM]);
    CHECK(r1 >= 0 && r2 >= 0 && L.seen[SIGALRM] == 1);
    hx_sigaction(SIGALRM, h_fallback, SA_RESTART);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    /* 하네스 기본 핸들러: 학생 코드가 설치에 실패해도 기본 동작(프로세스 종료)으로 죽지 않게 */
    int sigs[] = { SIGUSR1, SIGUSR2, SIGALRM, SIGTERM, SIGINT };
    for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++) hx_sigaction(sigs[i], h_fallback, SA_RESTART);

    test_part_a();
    test_part_b();
    test_part_c();

    if (g_sp[0] >= 0) { close(g_sp[0]); close(g_sp[1]); }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
