/* OS-B07 demo_daemon.c — self-pipe 로 만든 미니 감시 데몬 (init / supervisor 의 뼈대)
 *
 *   cc -std=c11 -Wall -Wextra -Werror -O2 demo_daemon.c -o demo_daemon
 *   ./demo_daemon & sleep 1.5; kill -TERM $!; wait $!; echo "daemon exit=$?"
 *   (터미널에서 그냥 ./demo_daemon 을 돌리고 Ctrl-C 를 눌러도 된다)
 *
 * 워커 3개를 띄운다. 워커 i 는 (i+1)*150 ms 일하고 끝나는데, 워커 1 은 매번 실패(exit 3)한다.
 * 데몬은 SIGCHLD 바이트를 받으면 waitpid(WNOHANG) 루프로 전부 회수하고, 실패한 워커는 다시 띄운다(최대 3번)
 * (systemd 의 Restart=on-failure 와 같은 생각). SIGTERM/SIGINT 를 받으면 워커들에게 SIGTERM 을 전달하고
 * 다 회수한 뒤 종료한다. 핸들러는 1바이트 write 만 하고 나머지는 전부 메인 루프에서 한다.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define NW 3
static int sp[2];
static pid_t worker[NW];
static int restarts;
static struct timespec t0;

static void on_sig(int sig) {
    int saved = errno;
    unsigned char b = (unsigned char)sig;
    (void)!write(sp[1], &b, 1);
    errno = saved;
}
static long ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (t.tv_sec - t0.tv_sec) * 1000 + (t.tv_nsec - t0.tv_nsec) / 1000000;
}
static pid_t spawn(int i) {
    fflush(stdout);
    pid_t c = fork();
    if (c == 0) {
        struct sigaction dfl;                       /* 자식은 기본 동작으로 (핸들러·self-pipe 를 물려받지 않게) */
        memset(&dfl, 0, sizeof dfl);
        dfl.sa_handler = SIG_DFL;
        sigaction(SIGTERM, &dfl, NULL);
        sigaction(SIGINT, &dfl, NULL);
        sigaction(SIGCHLD, &dfl, NULL);
        struct timespec w = { 0, (long)(i + 1) * 150000000L };
        nanosleep(&w, NULL);
        if (i == 1) _exit(3);                       /* 실패하는 워커 (exit 3) */
        for (;;) pause();                           /* 나머지는 SIGTERM 을 기다리며 상주 */
    }
    printf("[%5ld ms] spawn worker %d pid %d\n", ms(), i, (int)c);
    return c;
}

int main(void) {
    clock_gettime(CLOCK_MONOTONIC, &t0);
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (pipe(sp) != 0) return 1;
    for (int i = 0; i < 2; i++) {
        fcntl(sp[i], F_SETFL, fcntl(sp[i], F_GETFL) | O_NONBLOCK);
        fcntl(sp[i], F_SETFD, FD_CLOEXEC);
    }
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sig;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGINT);
    sigaddset(&sa.sa_mask, SIGTERM);
    sigaddset(&sa.sa_mask, SIGCHLD);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    for (int i = 0; i < NW; i++) worker[i] = spawn(i);
    int stopping = 0, alive = NW;
    while (alive > 0) {
        struct pollfd pf = { sp[0], POLLIN, 0 };
        if (poll(&pf, 1, -1) < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            return 1;
        }
        unsigned char b[64];
        ssize_t k;
        int got_chld = 0, got_stop = 0;
        while ((k = read(sp[0], b, sizeof b)) > 0)
            for (ssize_t j = 0; j < k; j++) {
                if (b[j] == SIGCHLD) got_chld++;
                else got_stop = b[j];
            }
        if (got_stop && !stopping) {
            stopping = 1;
            printf("[%5ld ms] got signal %d (%s) -> forwarding SIGTERM to workers\n", ms(), got_stop, strsignal(got_stop));
            for (int i = 0; i < NW; i++) if (worker[i] > 0) kill(worker[i], SIGTERM);
        }
        if (got_chld) {
            int st, n = 0;
            pid_t p;
            while ((p = waitpid(-1, &st, WNOHANG)) > 0) {
                n++;
                int i = 0;
                while (i < NW && worker[i] != p) i++;
                if (i == NW) continue;
                worker[i] = -1;
                alive--;
                if (WIFEXITED(st)) printf("[%5ld ms] worker %d pid %d exited %d\n", ms(), i, (int)p, WEXITSTATUS(st));
                else printf("[%5ld ms] worker %d pid %d killed by signal %d (%s)\n", ms(), i, (int)p, WTERMSIG(st), strsignal(WTERMSIG(st)));
                if (!stopping && !(WIFEXITED(st) && WEXITSTATUS(st) == 0) && restarts < 3) {
                    restarts++;
                    worker[i] = spawn(i);
                    alive++;
                }
            }
            printf("[%5ld ms]   (SIGCHLD bytes this round=%d, reaped=%d)\n", ms(), got_chld, n);
        }
    }
    printf("[%5ld ms] all workers gone, restarts=%d, daemon exits 0\n", ms(), restarts);
    return 0;
}
