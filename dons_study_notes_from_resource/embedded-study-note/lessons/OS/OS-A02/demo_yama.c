/* ENV: linux */
/* OS-A02 demo — Yama ptrace_scope: 누가 누구에게 PTRACE_ATTACH 할 수 있나
 *
 *   T1, T2 = 그냥 잠자는 자식 (T2 는 prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY) 로 허락)
 *   S      = T1/T2 의 형제 (조상이 아님) → scope 1 에서 T1 attach 는 EPERM, T2 는 허락됨
 *   부모   = T1 의 조상 → scope 1 에서 attach 가능
 * 모든 자식은 alarm(5) 안전장치 + 마지막에 SIGKILL/waitpid.
 *
 * build (Lima VM elinux): gcc -std=c11 -Wall -Wextra -Werror -O2 demo_yama.c -o yama && ./yama
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>

static pid_t sleeper(int allow_any, const int pfd[2]) {
    pid_t p = fork();
    if (p == 0) {
        alarm(5);
        if (allow_any) prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
        if (write(pfd[1], "r", 1) != 1) _exit(1);
        for (;;) pause();
    }
    char c;
    if (read(pfd[0], &c, 1) != 1) return -1; /* prctl 이 끝난 뒤에 시험한다 */
    return p;
}

static const char *try_attach(pid_t t) { /* attach → 정지 확인 → detach. 결과 문자열 */
    static char buf[64];
    if (ptrace(PTRACE_ATTACH, t, NULL, NULL) != 0) {
        snprintf(buf, sizeof buf, "denied, errno=%d (%s)", errno, strerror(errno));
        return buf;
    }
    int st;
    waitpid(t, &st, 0);
    ptrace(PTRACE_DETACH, t, NULL, NULL);
    snprintf(buf, sizeof buf, "OK (stopped sig %d, detached)", WIFSTOPPED(st) ? WSTOPSIG(st) : -1);
    return buf;
}

int main(void) {
    FILE *f = fopen("/proc/sys/kernel/yama/ptrace_scope", "r");
    int scope = -1;
    if (f) { if (fscanf(f, "%d", &scope) != 1) scope = -1; fclose(f); }
    printf("kernel.yama.ptrace_scope = %d, euid = %d\n", scope, (int)geteuid());

    int pfd[2];
    if (pipe(pfd) != 0) return 1;
    pid_t t1 = sleeper(0, pfd);
    pid_t t2 = sleeper(1, pfd);
    if (t1 < 0 || t2 < 0) return 1;

    fflush(stdout);
    pid_t s = fork();
    if (s == 0) { /* 형제 */
        printf("sibling  → T1 (no PR_SET_PTRACER)      : %s\n", try_attach(t1));
        printf("sibling  → T2 (PR_SET_PTRACER_ANY)     : %s\n", try_attach(t2));
        fflush(stdout);
        _exit(0);
    }
    waitpid(s, NULL, 0);
    printf("parent   → T1 (ancestor)               : %s\n", try_attach(t1));

    kill(t1, SIGKILL);
    kill(t2, SIGKILL);
    waitpid(t1, NULL, 0);
    waitpid(t2, NULL, 0);
    return 0;
}
