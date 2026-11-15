/* OS-B01 추가 연습 — 한 번에 검증
 *
 *   연습 1  run_timeout(): 제한 시간 안에 안 끝나면 SIGKILL (watchdog / timeout(1) 의 핵심)
 *   연습 2  posix_spawnp(): fork+exec 대신 한 번에 — exec 실패가 "부모의 반환값" 으로 온다
 *   연습 3  fork 개수 세기: for (i<3) fork() 와 fork() && fork() || fork()
 *   연습 4  sh_list(): a && b || c — 종료 코드로 다음 명령 실행 여부 결정
 *   연습 5  SIGCHLD 를 SIG_IGN 으로: 좀비가 안 생기는 대신 wait 가 ECHILD
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static void sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) { }
}

/* ---- 본문(solution.c) 과 같은 부품 ---- */
#define MAX_ARGS 16
static bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\n'; }
static int tokenize(char *line, char *argv[], int max, bool *bg) {
    char *r = line, *w = line;
    int argc = 0;
    *bg = false;
    for (;;) {
        while (is_ws(*r)) r++;
        if (*r == '\0') break;
        if (*bg) return -1;
        if (*r == '&') { *bg = true; r++; continue; }
        if (argc >= max - 1) return -1;
        argv[argc++] = w;
        while (*r != '\0' && !is_ws(*r) && *r != '&') {
            if (*r == '\'' || *r == '"') {
                char q = *r++;
                while (*r != q) { if (*r == '\0') return -1; *w++ = *r++; }
                r++;
            } else {
                *w++ = *r++;
            }
        }
        char stop = *r;
        *w++ = '\0';
        if (stop == '&') *bg = true;
        if (stop != '\0') r++;
    }
    if (*bg && argc == 0) return -1;
    argv[argc] = NULL;
    return argc;
}
static pid_t spawn(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);
        execvp(argv[0], argv);
        int e = errno;
        dprintf(STDERR_FILENO, "msh: %s: %s\n", argv[0], e == ENOENT ? "command not found" : strerror(e));
        _exit(e == ENOENT || e == ENOTDIR ? 127 : 126);
    }
    return pid;
}
static int wait_status(pid_t pid) {
    int st;
    for (;;) {
        pid_t r = waitpid(pid, &st, 0);
        if (r == pid) return st;
        if (r < 0 && errno == EINTR) continue;
        return -1;
    }
}
static int status_code(int status) {
    if (status < 0) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

/* ---- 연습 1: 제한 시간 실행 ---- */
static int run_timeout(char *const argv[], int ms, bool *timed_out) {
    *timed_out = false;
    pid_t pid = spawn(argv);
    if (pid < 0) return -1;
    int st;
    for (int t = 0; t < ms; t++) {
        pid_t r = waitpid(pid, &st, WNOHANG);
        if (r == pid) return st;
        if (r < 0 && errno != EINTR) return -1;
        sleep_ms(1);
    }
    *timed_out = true;
    kill(pid, SIGKILL);              /* 먼저 TERM 을 주는 정책도 흔하다 (본문 Part C 의 sup_shutdown) */
    return wait_status(pid);         /* 죽인 뒤에도 반드시 회수 — 안 하면 좀비 */
}

/* ---- 연습 2: posix_spawnp — 성공하면 pid, 실패하면 -errno ---- */
static pid_t spawn_posix(char *const argv[]) {
    pid_t pid;
    int rc = posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ);
    return rc == 0 ? pid : -rc;      /* 에러는 errno 가 아니라 반환값 */
}

/* ---- 연습 3: fork 개수 세기 — 실험은 별도 자식 안에서, 각 프로세스가 파이프에 1바이트 ---- */
static int count_procs(int which, int n) {
    int fd[2];
    if (pipe(fd) != 0) return -1;
    pid_t top = fork();
    if (top < 0) return -1;
    if (top == 0) {
        close(fd[0]);
        if (which == 0) {
            for (int i = 0; i < n; i++) fork();
        } else {
            (void)((fork() && fork()) || fork());
        }
        ssize_t wr = write(fd[1], "x", 1);
        close(fd[1]);
        while (wait(NULL) > 0) { }   /* 내가 만든 자식을 전부 회수하고 끝난다 */
        _exit(wr == 1 ? 0 : 1);
    }
    close(fd[1]);
    int count = 0;
    for (;;) {                       /* 쓰기 끝이 모두 닫히면 EOF. 3초 워치독 */
        struct pollfd p = { .fd = fd[0], .events = POLLIN };
        if (poll(&p, 1, 3000) <= 0) break;
        char buf[64];
        ssize_t r = read(fd[0], buf, sizeof buf);
        if (r <= 0) break;
        count += (int)r;
    }
    close(fd[0]);
    wait_status(top);
    return count;
}

/* ---- 연습 4: && / || 목록. 연산자는 따옴표 밖에서만 인식 ---- */
static int run_simple(char *seg) {
    char *argv[MAX_ARGS];
    bool bg;
    int argc = tokenize(seg, argv, MAX_ARGS, &bg);
    if (argc <= 0 || bg) return 2;
    pid_t pid = spawn(argv);
    return pid < 0 ? 1 : status_code(wait_status(pid));
}
static int sh_list(const char *line, int *ran) {
    char buf[256];
    snprintf(buf, sizeof buf, "%s", line);
    char *seg = buf;
    int code = 0;
    bool run = true;                 /* 첫 명령은 무조건 실행 */
    *ran = 0;
    for (char *p = buf; ; p++) {
        if (*p == '\'' || *p == '"') {                      /* 따옴표 안은 건너뛴다 */
            char q = *p++;
            while (*p && *p != q) p++;
            if (!*p) return 2;
            continue;
        }
        bool end = (*p == '\0');
        bool op = !end && ((p[0] == '&' && p[1] == '&') || (p[0] == '|' && p[1] == '|'));
        if (!end && !op) continue;
        char kind = end ? 0 : p[0];
        *p = '\0';
        if (run) { code = run_simple(seg); (*ran)++; }
        if (end) break;
        run = (kind == '&') ? (code == 0) : (code != 0);   /* 직전 "결과 코드" 로 결정 */
        p++;                                                /* 두 번째 글자 */
        seg = p + 1;
    }
    return code;
}

/* ---- 연습 5: SIGCHLD = SIG_IGN 이면 자식이 자동 회수된다 ---- */
static bool ign_autoreap(pid_t *child, int *wait_errno) {
    struct sigaction ign, old;
    memset(&ign, 0, sizeof ign);
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    sigaction(SIGCHLD, &ign, &old);
    pid_t pid = fork();
    if (pid == 0) _exit(0);
    *child = pid;
    int st;
    errno = 0;
    pid_t r = waitpid(pid, &st, 0);   /* POSIX(XSI): 자식이 끝날 때까지 막혔다가 -1/ECHILD */
    *wait_errno = errno;
    sigaction(SIGCHLD, &old, NULL);
    return pid > 0 && r == -1;
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    bool to;
    int st;

    /* 연습 1 */
    st = run_timeout((char *const[]){ "sleep", "5", NULL }, 100, &to);
    CHECK(to && WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL && status_code(st) == 137);
    st = run_timeout((char *const[]){ "sh", "-c", "exit 4", NULL }, 2000, &to);
    CHECK(!to && status_code(st) == 4);
    CHECK(waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD);   /* 좀비 없음 */

    /* 연습 2 */
    pid_t p = spawn_posix((char *const[]){ "sh", "-c", "exit 6", NULL });
    CHECK(p > 0 && status_code(wait_status(p)) == 6);
    p = spawn_posix((char *const[]){ "no_such_cmd_os_b01", NULL });
    CHECK(p == -ENOENT);                                           /* macOS: 자식 127 이 아니라 부모 반환값 */
    p = spawn_posix((char *const[]){ "/etc/hosts", NULL });
    CHECK(p == -EACCES);

    /* 연습 3 */
    CHECK(count_procs(0, 3) == 8);
    CHECK(count_procs(0, 4) == 16);
    CHECK(count_procs(1, 0) == 5);

    /* 연습 4 */
    int ran;
    CHECK(sh_list("false || true", &ran) == 0 && ran == 2);
    CHECK(sh_list("true && false", &ran) == 1 && ran == 2);
    CHECK(sh_list("true || no_such_cmd_os_b01", &ran) == 0 && ran == 1);          /* 건너뜀 → 127 안 나옴 */
    CHECK(sh_list("false && no_such_cmd_os_b01 || sh -c 'exit 5'", &ran) == 5 && ran == 2);
    CHECK(sh_list("sh -c 'exit 0 && exit 9' && true", &ran) == 0 && ran == 2);    /* 따옴표 안 && 는 sh 몫 */

    /* 연습 5 */
    pid_t child = 0;
    int e = 0;
    CHECK(ign_autoreap(&child, &e) && e == ECHILD);
    errno = 0;
    CHECK(child > 0 && kill(child, 0) == -1 && errno == ESRCH);    /* 좀비로도 남지 않았다 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
