/* OS-B02 demo_hang — "닫지 않은 쓰기 끝 → EOF 안 옴 → hang" 을 실제로 재현하고, 감시(watchdog)로 끊는다.
 *
 *   V1 부모가 쓰기 끝을 안 닫음          printf 'b\na\n' | sort         → sort 가 영원히 EOF 대기
 *   V2 읽는 자식이 자기 쓰기 끝을 안 닫음  (같은 파이프라인)               → sort 가 "자기 자신" 을 기다림
 *   V3 형제 자식이 쓰기 끝을 물려받아 쥠  printf 'b\na\n' | sort | cat   → cat 이 pipe0 쓰기 끝을 쥠
 *   V4 다 쓰고 나서 읽기 (popen 양방향)   1 MB → cat → 부모              → 양쪽 다 write 에서 막힘
 *   FIX 전부 닫음                        printf 'b\na\n' | sort | cat
 *
 * 감시: V1–V3/FIX 는 waitpid(WNOHANG) 폴링 1000 ms 뒤 SIGKILL. V4 는 alarm(1) + SA_RESTART 없는 핸들러로
 * 막힌 write() 를 EINTR 로 깨운다. 인자 "lsof" 를 주면 막힌 순간 lsof 로 누가 파이프를 쥐었는지 찍는다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_hang.c -o demo_hang && ./demo_hang [lsof]
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum { BUG_NONE = 0, BUG_PARENT = 1, BUG_SELF = 2, BUG_SIBLING = 4 };
static bool g_lsof;

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static pid_t spawn(char *const argv[], int in, int out, const int *drop, int ndrop) {
    pid_t pid = fork();
    if (pid == 0) {
        if (in != 0)  { dup2(in, 0);  close(in); }
        if (out != 1) { dup2(out, 1); close(out); }
        for (int i = 0; i < ndrop; i++) close(drop[i]);
        execvp(argv[0], argv);
        _exit(127);
    }
    return pid;
}

static void show_lsof(const pid_t *pids, int n) {
    char cmd[256];
    int k = snprintf(cmd, sizeof cmd, "lsof -n -a -d 0-9 -p %ld", (long)getpid());
    for (int i = 0; i < n; i++) k += snprintf(cmd + k, sizeof cmd - (size_t)k, ",%ld", (long)pids[i]);
    snprintf(cmd + k, sizeof cmd - (size_t)k, " 2>/dev/null | awk '{print $1, $2, $4, $5, $6, $8, $9}'");
    fflush(stdout);
    if (system(cmd) != 0) printf("  (lsof failed)\n");
}

/* 자식들을 timeout 안에 거두고, 못 거두면 죽인다. 끝난 시간(ms) 또는 -1(hang) 반환 */
static double reap(const pid_t *pids, int n, double t0, int timeout_ms) {
    int left = n;
    bool done[4] = { false };
    while (left > 0) {
        for (int i = 0; i < n; i++)
            if (!done[i] && waitpid(pids[i], NULL, WNOHANG) == pids[i]) { done[i] = true; left--; }
        if (left == 0) return now_ms() - t0;
        if (now_ms() - t0 > timeout_ms) {
            printf("  HANG: %d ms 지나도 %d 개 프로세스가 안 끝남:", timeout_ms, left);
            for (int i = 0; i < n; i++) if (!done[i]) printf(" pid %ld", (long)pids[i]);
            printf("\n");
            if (g_lsof) show_lsof(pids, n);
            for (int i = 0; i < n; i++) if (!done[i]) { kill(pids[i], SIGKILL); waitpid(pids[i], NULL, 0); }
            return -1;
        }
        struct timespec ts = { 0, 1000000L };
        nanosleep(&ts, NULL);
    }
    return now_ms() - t0;
}

/* printf 'b\na\n' | sort [| cat] — 버그 플래그에 따라 fd 하나를 안 닫는다 */
static void run(const char *name, int bug, bool three) {
    char *pf[] = { "printf", "b\\na\\n", NULL }, *so[] = { "sort", NULL }, *ca[] = { "cat", NULL };
    int p0[2], p1[2] = { -1, -1 };
    pid_t pids[3];
    int n = 0;
    double t0 = now_ms();
    printf("%s\n", name);
    fflush(stdout);
    if (pipe(p0) < 0) { perror("pipe"); exit(1); }
    if (three && pipe(p1) < 0) { perror("pipe"); exit(1); }
    /* 단계 0: printf → p0 */
    { int d[] = { p0[0], p1[0], p1[1] };
      pids[n++] = spawn(pf, 0, p0[1], d, three ? 3 : 1); }
    /* 단계 1: p0 → sort → (p1 또는 stdout) */
    { int d[4], nd = 0;
      if (!(bug & BUG_SELF)) d[nd++] = p0[1];               /* V2: 자기 쓰기 끝을 안 닫음 */
      if (three) { d[nd++] = p1[0]; }
      pids[n++] = spawn(so, p0[0], three ? p1[1] : 1, d, nd); }
    /* 단계 2: p1 → cat → stdout */
    if (three) {
        int d[3], nd = 0;
        d[nd++] = p1[1];
        if (!(bug & BUG_SIBLING)) { d[nd++] = p0[0]; d[nd++] = p0[1]; }   /* V3: pipe0 을 물려받은 채 */
        pids[n++] = spawn(ca, p1[0], 1, d, nd);
    }
    if (!(bug & BUG_PARENT)) close(p0[1]);                  /* V1: 부모가 쓰기 끝을 쥠 */
    close(p0[0]);
    if (three) { close(p1[0]); close(p1[1]); }
    double t = reap(pids, n, t0, 1000);
    if (bug & BUG_PARENT) close(p0[1]);
    if (t >= 0) printf("  OK: %.1f ms 에 끝남\n", t);
}

static volatile sig_atomic_t g_alarm;
static void on_alarm(int s) { (void)s; g_alarm = 1; }

/* V4: 1 MB 를 cat 에 다 쓰고 나서 읽으려 한다 */
static void run_write_all_then_read(void) {
    static char big[1 << 20];
    memset(big, 'q', sizeof big);
    int ip[2], op[2];
    printf("V4 다 쓰고 나서 읽기: 1 MB → cat → 부모\n");
    fflush(stdout);
    if (pipe(ip) < 0 || pipe(op) < 0) { perror("pipe"); exit(1); }
    char *ca[] = { "cat", NULL };
    int d[] = { ip[1], op[0] };
    pid_t pid = spawn(ca, ip[0], op[1], d, 2);
    close(ip[0]); close(op[1]);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_alarm;                               /* SA_RESTART 없음 → 막힌 write 가 EINTR */
    sigaction(SIGALRM, &sa, NULL);
    double t0 = now_ms();
    alarm(1);
    size_t put = 0;
    while (put < sizeof big && !g_alarm) {                 /* 시그널이 오면 write 는 부분 성공 또는 EINTR */
        ssize_t k = write(ip[1], big + put, sizeof big - put);
        if (k > 0) put += (size_t)k;
        else if (k < 0 && errno != EINTR) break;
    }
    alarm(0);
    if (g_alarm) {
        printf("  HANG: %.0f ms 동안 write 에서 막힘. 들어간 바이트 = %zu / %zu\n", now_ms() - t0, put, sizeof big);
        printf("        (부모는 입력 파이프가 꽉 차서, cat 은 출력 파이프가 꽉 차서 서로 기다림)\n");
        if (g_lsof) show_lsof(&pid, 1);
    }
    close(ip[1]);                                           /* 이제라도 정리: 읽어서 비우고 거둔다 */
    char sink[65536];
    size_t got = 0;
    ssize_t k;
    while ((k = read(op[0], sink, sizeof sink)) > 0) got += (size_t)k;
    close(op[0]);
    waitpid(pid, NULL, 0);
    printf("  정리: 쓰기 끝을 닫고 읽으니 cat 이 %zu 바이트를 내보내고 끝남\n", got);
}

int main(int argc, char **argv) {
    g_lsof = argc > 1 && strcmp(argv[1], "lsof") == 0;
    signal(SIGPIPE, SIG_IGN);
    run("V1 부모가 쓰기 끝을 안 닫음: printf | sort", BUG_PARENT, false);
    run("V2 sort 가 자기 쓰기 끝을 안 닫음: printf | sort", BUG_SELF, false);
    run("V3 cat 이 pipe0 을 물려받아 쥠: printf | sort | cat", BUG_SIBLING, true);
    run("FIX 전부 닫음: printf | sort | cat", BUG_NONE, true);
    run_write_all_then_read();
    return 0;
}
