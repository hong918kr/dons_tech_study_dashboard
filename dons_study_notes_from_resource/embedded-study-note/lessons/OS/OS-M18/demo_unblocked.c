/* ENV: linux */
/* BUILD: -pthread */
/* OS-M18 demo_unblocked — signalfd 를 열었지만 시그널을 "막지 않았을 때" 무슨 일이 생기나 (각 실험은 자식 프로세스에서)
 *   1) SIGUSR1: 안 막음 → 기본 동작(종료)이 먼저 → 프로세스가 죽는다
 *   2) SIGCHLD: 안 막음 → 기본 동작이 "무시" → 생성 시점에 버려져 signalfd 는 영원히 조용
 *   3) 스레드를 먼저 만들고 메인만 막음 → 그 스레드가 SIGTERM 을 받아 프로세스가 죽는다.
 *      단, 스레드가 막 생성된 직후엔 glibc 가 새 스레드를 "전부 막힌" 마스크로 시작하므로 결과가 갈린다 (경쟁).
 * build: gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_unblocked.c -o unblocked && ./unblocked
 */
#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <unistd.h>

static int sfd_for(int sig, int block) {
    sigset_t s;
    sigemptyset(&s);
    sigaddset(&s, sig);
    if (block) pthread_sigmask(SIG_BLOCK, &s, NULL);
    return signalfd(-1, &s, SFD_NONBLOCK | SFD_CLOEXEC);
}
static int readable(int fd, int ms) { struct pollfd p = { fd, POLLIN, 0 }; return poll(&p, 1, ms) == 1; }
static const char *desc(int st, char *buf, size_t n) {
    if (WIFEXITED(st)) snprintf(buf, n, "exit %d", WEXITSTATUS(st));
    else snprintf(buf, n, "killed by %s", strsignal(WTERMSIG(st)));
    return buf;
}

static void case1(int block) {               /* exit 0 = signalfd 로 받음 */
    pid_t c = fork();
    if (c == 0) {
        alarm(3);
        int fd = sfd_for(SIGUSR1, block);
        kill(getpid(), SIGUSR1);
        _exit(readable(fd, 300) ? 0 : 1);
    }
    int st;
    char b[64];
    waitpid(c, &st, 0);
    printf("  1) SIGUSR1, %-7s → %s\n", block ? "막음" : "안 막음", desc(st, b, sizeof b));
}
static void case2(int block) {               /* exit 0 = SIGCHLD 레코드가 왔다, 1 = 안 왔다 */
    pid_t c = fork();
    if (c == 0) {
        alarm(3);
        int fd = sfd_for(SIGCHLD, block);
        pid_t g = fork();
        if (g == 0) _exit(0);
        waitpid(g, NULL, 0);                /* 손자는 이미 죽어 회수됨 — SIGCHLD 는 그 전에 생성됐다 */
        _exit(readable(fd, 300) ? 0 : 1);
    }
    int st;
    char b[64];
    waitpid(c, &st, 0);
    printf("  2) SIGCHLD, %-7s → %s (%s)\n", block ? "막음" : "안 막음", desc(st, b, sizeof b),
           WIFEXITED(st) && WEXITSTATUS(st) == 0 ? "signalfd 에 레코드 있음" : "signalfd 조용 — 시그널이 버려졌다");
}

static int g_ready[2], g_hold[2];
static void *idle(void *a) {
    char ch = 'r';
    ssize_t r = a ? write(g_ready[1], &ch, 1) : 0;
    r = read(g_hold[0], &ch, 1);
    (void)r;
    return NULL;
}
static int case3_once(int handshake) {       /* 0 = signalfd 로 받음, 1 = SIGTERM 에 죽음, 2 = 기타 */
    pid_t c = fork();
    if (c == 0) {
        alarm(3);
        if (pipe(g_ready) != 0 || pipe(g_hold) != 0) _exit(9);
        pthread_t t;
        pthread_create(&t, NULL, idle, handshake ? &t : NULL);
        char ch;
        if (handshake && read(g_ready[0], &ch, 1) != 1) _exit(9);
        int fd = sfd_for(SIGTERM, 1);       /* 메인만 막는다 (버그) */
        kill(getpid(), SIGTERM);
        _exit(readable(fd, 300) ? 0 : 3);
    }
    int st;
    waitpid(c, &st, 0);
    if (WIFEXITED(st) && WEXITSTATUS(st) == 0) return 0;
    if (WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM) return 1;
    return 2;
}
static void case3(int handshake, int trials) {
    int cnt[3] = { 0, 0, 0 };
    for (int i = 0; i < trials; i++) cnt[case3_once(handshake)]++;
    printf("  3) 스레드 먼저 → 메인만 막음, %s: %d 번 중 SIGTERM 에 죽음 %d, signalfd 로 받음 %d, 기타 %d\n",
           handshake ? "스레드가 돌기 시작한 뒤 kill" : "pthread_create 직후 바로 kill", trials, cnt[1], cnt[0], cnt[2]);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("signalfd 는 시그널을 \"훔치지\" 않는다 — 막혀서 pending 에 남은 것만 읽는다\n");
    case1(0); case1(1);
    case2(0); case2(1);
    case3(0, 200);
    case3(1, 200);
    return 0;
}
