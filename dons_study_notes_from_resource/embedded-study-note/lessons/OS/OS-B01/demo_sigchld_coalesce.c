/* OS-B01 demo — SIGCHLD 는 자식 수만큼 오지 않는다 (막지 않은 상태에서 실측)
 *
 *   N 개의 자식을 한꺼번에 fork → 각자 바로 _exit. 핸들러 실행 횟수와 waitpid 로 회수한 수를 비교.
 *   naive : 핸들러 한 번에 waitpid 한 번  → 회수 못 한 좀비가 남는다
 *   loop  : 핸들러 한 번에 WNOHANG 루프   → 전부 회수
 *   (회수는 메인 루프에서 한다 — 핸들러는 플래그만. 이유는 OS-B07)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_sigchld_coalesce.c -o dsc && ./dsc 50 5
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_flag, g_count;
static void on_chld(int s) { (void)s; g_flag = 1; g_count++; }

static void sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) { }
}

static void run(int n, int loop) {
    g_flag = 0; g_count = 0;
    for (int i = 0; i < n; i++) {
        pid_t p = fork();
        if (p == 0) _exit(0);
    }
    int reaped = 0;
    for (int t = 0; t < 300; t++) {              /* 300 ms 동안 메인 루프 */
        if (g_flag) {
            g_flag = 0;
            if (loop) { while (waitpid(-1, NULL, WNOHANG) > 0) reaped++; }
            else if (waitpid(-1, NULL, WNOHANG) > 0) reaped++;
        }
        sleep_ms(1);
    }
    int left = 0;
    while (waitpid(-1, NULL, WNOHANG) > 0) left++; /* 남은 좀비 청소 (그리고 세기) */
    printf("%-5s n=%d  handler ran %2d times  reaped %2d  zombies left %2d\n",
           loop ? "loop" : "naive", n, (int)g_count, reaped, left);
}

int main(int argc, char **argv) {
    int n = argc > 1 ? atoi(argv[1]) : 50, runs = argc > 2 ? atoi(argv[2]) : 5;
    setvbuf(stdout, NULL, _IOLBF, 0);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_chld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);
    for (int r = 0; r < runs; r++) { run(n, 0); run(n, 1); }
    return 0;
}
