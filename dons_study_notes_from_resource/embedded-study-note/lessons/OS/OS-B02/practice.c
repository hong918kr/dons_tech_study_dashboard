/* OS-B02 추가 연습 — 한 번에 검증
 *
 *   연습 1  dup / dup2 규칙: 가장 낮은 빈 번호, dup2(fd, fd), dup2 가 대상을 "조용히" 닫음, OSTEP p4 의 close(1)+open
 *   연습 2  열린 파일 항목(open file description) 공유: fork·dup 은 오프셋을 같이 쓰고, open 두 번은 따로 쓴다
 *   연습 3  pipefail 과 PIPESTATUS: 단계별 코드, SIGPIPE 무시가 exec 너머로 상속되는 함정
 *   연습 4  FD_CLOEXEC / F_DUPFD_CLOEXEC: exec 할 때 새는 fd 막기 (macOS 에는 pipe2 가 없다)
 *   연습 5  파이프 용량과 PIPE_BUF 원자성: O_NONBLOCK 으로 꽉 채워 보기
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef PIPE_BUF
#define PIPE_BUF 512                /* POSIX 최솟값 (_POSIX_PIPE_BUF) */
#endif
#define MAXSTAGE 4
#define TMO_MS   2000
#define HUNG     (-2)

static int g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) g_pass++; \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}
static void sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
static void set_cloexec(int fd) { fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC); }
static bool is_open(int fd)     { return fcntl(fd, F_GETFD) != -1; }
static bool is_cloexec(int fd)  { return (fcntl(fd, F_GETFD) & FD_CLOEXEC) != 0; }

static int shell_code(int st) {
    if (WIFEXITED(st))   return WEXITSTATUS(st);
    if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
    return -1;
}
/* WNOHANG 폴링 + 시간 초과 시 SIGKILL (본문 하네스와 같은 것) */
static int wait_all(const pid_t *pids, int n, int timeout_ms, int *codes) {
    bool done[MAXSTAGE] = { false };
    int left = n, rc = 0;
    double end = now_ms() + timeout_ms;
    for (int i = 0; i < n; i++) codes[i] = HUNG;
    while (left > 0) {
        for (int i = 0; i < n; i++) {
            int st;
            if (done[i]) continue;
            pid_t r = waitpid(pids[i], &st, WNOHANG);
            if (r == pids[i])                 { done[i] = true; left--; codes[i] = shell_code(st); }
            else if (r < 0 && errno != EINTR) { done[i] = true; left--; codes[i] = -1; }
        }
        if (left == 0) break;
        if (now_ms() >= end) {
            for (int i = 0; i < n; i++)
                if (!done[i]) { kill(pids[i], SIGKILL); waitpid(pids[i], NULL, 0); }
            rc = -1;
            break;
        }
        sleep_ms(1);
    }
    return rc;
}

/* 단계 n 개를 파이프로 잇고(stdin=/dev/null) 마지막 stdout 을 out 에 캡처한다.
 * reset_sigpipe: 자식에서 SIGPIPE 를 SIG_DFL 로 되돌릴지. 첫 단계의 stderr 는 /dev/null (yes 의 오류 메시지). */
static void run_pipe(char *const *argvs[], int n, bool reset_sigpipe, char *out, size_t cap, int *codes) {
    int cp[2], prev = open("/dev/null", O_RDONLY);
    pid_t pids[MAXSTAGE];
    size_t len = 0;
    if (prev < 0 || pipe(cp) < 0) { perror("practice"); exit(2); }
    set_cloexec(cp[0]);
    for (int i = 0; i < n; i++) {
        int p[2] = { -1, cp[1] };
        if (i < n - 1 && pipe(p) < 0) { perror("pipe"); exit(2); }
        pids[i] = fork();
        if (pids[i] < 0) { perror("fork"); exit(2); }
        if (pids[i] == 0) {
            dup2(prev, STDIN_FILENO);
            dup2(p[1], STDOUT_FILENO);
            if (i == 0) { int dn = open("/dev/null", O_WRONLY); dup2(dn, STDERR_FILENO); close(dn); }
            close(prev); close(p[1]);
            if (i < n - 1) { close(p[0]); close(cp[1]); }
            if (reset_sigpipe) signal(SIGPIPE, SIG_DFL);
            execvp(argvs[i][0], argvs[i]);
            _exit(127);
        }
        close(prev);
        if (i < n - 1) { close(p[1]); prev = p[0]; }
    }
    close(cp[1]);
    double deadline = now_ms() + TMO_MS;
    for (;;) {
        struct pollfd pf = { .fd = cp[0], .events = POLLIN };
        int left = (int)(deadline - now_ms());
        if (left <= 0 || poll(&pf, 1, left) <= 0) break;
        ssize_t k = read(cp[0], out + len, cap - 1 - len);
        if (k <= 0) break;
        len += (size_t)k;
    }
    out[len] = '\0';
    close(cp[0]);
    wait_all(pids, n, (int)(deadline - now_ms()) + 1, codes);
}

/* ---------- 연습 3 ---------- */
/* bash 의 set -o pipefail: 0 이 아닌 코드 중 "가장 오른쪽" 것, 모두 0 이면 0 */
static int pipefail_code(const int *codes, int n) {
    for (int i = n - 1; i >= 0; i--)
        if (codes[i] != 0) return codes[i];
    return 0;
}

/* ---------- 연습 4 ---------- */
#define PROBE_SH "n=; for f in 3 4 5 6 7 8 9; do if (: >&$f) 2>/dev/null; then n=\"$n $f\"; fi; done; echo \"open:$n\""

static void probe(char *out, size_t cap) {
    char *sh[] = { "sh", "-c", PROBE_SH, NULL };
    char *const *argvs[] = { sh };
    int codes[1];
    run_pipe(argvs, 1, true, out, cap, codes);
}

static char g_path[96];

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    alarm(30);
    snprintf(g_path, sizeof g_path, "/tmp/OS-B02_work_practice.%ld", (long)getpid());

    /* ---- 연습 1: dup / dup2 규칙 ---- */
    {
        int a = open("/dev/null", O_RDONLY), b = open("/dev/null", O_RDONLY);
        CHECK(a >= 3 && b > a);                                    /* 새 fd 는 가장 낮은 빈 번호 */
        close(a);
        int c = dup(b);
        CHECK(c == a);                                             /* dup 도 가장 낮은 빈 번호 → a 재사용 */
        CHECK(dup2(b, b) == b && is_open(b));                      /* 같은 번호: 아무것도 안 닫고 b 반환 */
        int p[2];
        if (pipe(p) < 0) { perror("pipe"); return 2; }
        CHECK(dup2(b, p[1]) == p[1]);                              /* p[1] 이 /dev/null 을 가리키게 됨 */
        char ch;
        CHECK(read(p[0], &ch, 1) == 0);                            /* 원래 쓰기 끝이 "조용히" 닫혀 EOF */
        close(p[0]); close(p[1]); close(b); close(c);

        pid_t pid = fork();                                        /* OSTEP 그림 5.4 (p4.c) 의 방식 */
        if (pid == 0) {
            close(STDOUT_FILENO);
            int fd = open(g_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);   /* 가장 낮은 빈 번호 = 1 */
            printf("fd=%d\n", fd);                                 /* stdout 이 이제 파일 */
            fflush(stdout);
            _exit(fd == 1 ? 0 : 1);
        }
        int st;
        waitpid(pid, &st, 0);
        char buf[32] = { 0 };
        int fd = open(g_path, O_RDONLY);
        ssize_t k = read(fd, buf, sizeof buf - 1);
        close(fd);
        CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0 && k == 5 && strcmp(buf, "fd=1\n") == 0);
    }

    /* ---- 연습 2: 오프셋 공유 ---- */
    {
        char buf[128];
        /* (a) fork: 같은 열린 파일 항목 → 자식이 쓴 만큼 부모 오프셋도 움직인다 */
        int fd = open(g_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        pid_t pid = fork();
        if (pid == 0) { ssize_t w = write(fd, "CCCC", 4); _exit(w == 4 ? 0 : 1); }
        waitpid(pid, NULL, 0);
        CHECK(lseek(fd, 0, SEEK_CUR) == 4);                        /* 부모는 안 썼는데 오프셋 4 */
        CHECK(write(fd, "PPPP", 4) == 4);
        close(fd);
        fd = open(g_path, O_RDONLY);
        ssize_t k = read(fd, buf, sizeof buf - 1);
        close(fd);
        buf[k > 0 ? k : 0] = '\0';
        CHECK(strcmp(buf, "CCCCPPPP") == 0);

        /* (b) 자식이 따로 open: 열린 파일 항목이 둘 → 오프셋이 따로 → 덮어씀 */
        fd = open(g_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        pid = fork();
        if (pid == 0) {
            int fd2 = open(g_path, O_WRONLY);
            ssize_t w = write(fd2, "CCCC", 4);
            _exit(w == 4 ? 0 : 1);
        }
        waitpid(pid, NULL, 0);
        CHECK(lseek(fd, 0, SEEK_CUR) == 0);
        CHECK(write(fd, "PP", 2) == 2);
        close(fd);
        fd = open(g_path, O_RDONLY);
        k = read(fd, buf, sizeof buf - 1);
        close(fd);
        buf[k > 0 ? k : 0] = '\0';
        CHECK(strcmp(buf, "PPCC") == 0);

        /* (c) dup: 한 프로세스 안에서도 같다 */
        fd = open(g_path, O_WRONLY | O_TRUNC);
        int d = dup(fd);
        CHECK(write(fd, "ab", 2) == 2 && write(d, "cd", 2) == 2 && lseek(fd, 0, SEEK_CUR) == 4);
        close(fd); close(d);
        unlink(g_path);
    }

    /* ---- 연습 3: pipefail, SIGPIPE 상속 ---- */
    {
        char out[64];
        int codes[MAXSTAGE];
        char *f[] = { "false", NULL }, *t[] = { "true", NULL };
        char *const *ft[] = { f, t };
        run_pipe(ft, 2, true, out, sizeof out, codes);
        CHECK(codes[0] == 1 && codes[1] == 0);
        CHECK(codes[1] == 0 && pipefail_code(codes, 2) == 1);     /* 기본: 마지막 0, pipefail: 1 */

        char *y[] = { "yes", NULL }, *h[] = { "head", "-n", "1", NULL };
        char *const *yh[] = { y, h };
        run_pipe(yh, 2, true, out, sizeof out, codes);             /* 자식에서 SIG_DFL 복원 */
        CHECK(strcmp(out, "y\n") == 0 && codes[0] == 128 + SIGPIPE && pipefail_code(codes, 2) == 141);
        run_pipe(yh, 2, false, out, sizeof out, codes);            /* 복원 안 함: SIG_IGN 이 exec 너머로 */
        printf("연습 3: SIGPIPE 무시 상속 시 yes 의 코드 = %d (기본값이면 141)\n", codes[0]);
        CHECK(strcmp(out, "y\n") == 0 && codes[0] != 128 + SIGPIPE && codes[0] != HUNG);
    }

    /* ---- 연습 4: FD_CLOEXEC ---- */
    {
        char out[64], want[64];
        int p[2];
        if (pipe(p) < 0) { perror("pipe"); return 2; }
        probe(out, sizeof out);                                    /* 플래그 없음: 자식에게 샌다 */
        snprintf(want, sizeof want, "open: %d %d\n", p[0], p[1]);
        printf("연습 4: 플래그 없이 = %s", out);
        CHECK(strcmp(out, want) == 0);
        set_cloexec(p[0]); set_cloexec(p[1]);
        probe(out, sizeof out);
        CHECK(strcmp(out, "open:\n") == 0 && is_open(p[0]));      /* 부모에는 그대로, exec 한 자식에는 없음 */
        int d = fcntl(p[0], F_DUPFD_CLOEXEC, 0);                   /* dup + CLOEXEC 를 한 번에 (원자적) */
        int e = dup(p[0]);                                         /* dup 은 플래그를 안 물려준다 */
        CHECK(d >= 0 && is_cloexec(d) && e >= 0 && !is_cloexec(e));
        probe(out, sizeof out);
        snprintf(want, sizeof want, "open: %d\n", e);
        CHECK(strcmp(out, want) == 0);                             /* e 만 보인다 */
        close(d); close(e); close(p[0]); close(p[1]);
    }

    /* ---- 연습 5: 파이프 용량, PIPE_BUF 원자성 ---- */
    {
        int p[2];
        if (pipe(p) < 0) { perror("pipe"); return 2; }
        fcntl(p[1], F_SETFL, fcntl(p[1], F_GETFL) | O_NONBLOCK);
        size_t cap1 = 0;
        while (write(p[1], "x", 1) == 1) cap1++;                   /* 1 바이트씩 꽉 찰 때까지 */
        int e1 = errno;
        CHECK(e1 == EAGAIN && cap1 >= PIPE_BUF);
        char sink[1024];
        CHECK(read(p[0], sink, 100) == 100);                       /* 100 바이트 비움 */
        static char chunk[PIPE_BUF];
        memset(chunk, 'z', sizeof chunk);
        errno = 0;
        ssize_t r1 = write(p[1], chunk, PIPE_BUF);                 /* PIPE_BUF 이하: 전부 아니면 0 */
        CHECK(r1 == -1 && errno == EAGAIN);
        close(p[0]); close(p[1]);

        if (pipe(p) < 0) { perror("pipe"); return 2; }
        fcntl(p[1], F_SETFL, fcntl(p[1], F_GETFL) | O_NONBLOCK);
        static char big[1 << 20];
        ssize_t r2 = write(p[1], big, sizeof big);                 /* PIPE_BUF 초과: 부분 쓰기 허용 */
        printf("연습 5: 1 B 씩 채운 용량 = %zu B, 1 MB 한 번에 쓰면 %zd B 들어감, PIPE_BUF = %d\n",
               cap1, r2, PIPE_BUF);
        CHECK(r2 > 0 && (size_t)r2 < sizeof big);
        close(p[0]); close(p[1]);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
