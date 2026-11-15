/* OS-B01 demo — 좀비와 고아를 진짜 ps 로 본다 (macOS / Linux 공통 코드)
 *
 *   좀비: 자식이 _exit 했지만 부모가 아직 wait 안 함 → ps STAT 에 Z
 *   고아: 부모(C)가 먼저 끝난 손자(G) → PPID 가 1 (macOS: launchd) 로 바뀜
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_zombie_orphan.c -o dzo && ./dzo
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) { }
}

static void ps(pid_t pid) {                     /* ps -o pid,ppid,stat,comm -p <pid> 를 실행하고 그 자식만 기다린다 */
    char p[16];
    snprintf(p, sizeof p, "%d", (int)pid);
    fflush(stdout);
    pid_t c = fork();
    if (c == 0) {
        execlp("ps", "ps", "-o", "pid,ppid,stat,comm", "-p", p, (char *)NULL);
        _exit(127);
    }
    waitpid(c, NULL, 0);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("me = %d\n", (int)getpid());

    printf("== zombie: child exits, parent does not wait yet ==\n");
    pid_t z = fork();
    if (z == 0) _exit(7);
    sleep_ms(100);
    ps(z);
    int st;
    waitpid(z, &st, 0);
    printf("== after waitpid (exit code %d): the pid is gone ==\n", WEXITSTATUS(st));
    ps(z);

    printf("== orphan: C forks G, C exits first ==\n");
    int fd[2];
    if (pipe(fd) != 0) return 1;
    pid_t c = fork();
    if (c == 0) {
        pid_t g = fork();
        if (g == 0) {
            close(fd[0]);
            pid_t me = getpid();
            if (write(fd[1], &me, sizeof me) != (ssize_t)sizeof me) _exit(1);
            close(fd[1]);
            execlp("sleep", "sleep", "2", (char *)NULL);
            _exit(127);
        }
        sleep_ms(300);                          /* 잠깐 살아 있다가 G 를 기다리지 않고 끝난다 */
        _exit(0);
    }
    close(fd[1]);
    pid_t g = 0;
    if (read(fd[0], &g, sizeof g) != (ssize_t)sizeof g) return 1;
    close(fd[0]);
    sleep_ms(100);
    printf("-- while C (%d) is alive --\n", (int)c);
    ps(g);
    waitpid(c, NULL, 0);
    sleep_ms(50);
    printf("-- after C exited --\n");
    ps(g);
    ps(1);
    kill(g, SIGTERM);                           /* 내 자식이 아니므로 wait 할 수 없다 — 회수는 새 부모(pid 1)의 몫 */
    sleep_ms(100);
    printf("-- after kill(G): reaped by pid 1, not by me --\n");
    ps(g);
    return 0;
}
