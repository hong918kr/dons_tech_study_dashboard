// C05_wait.c — OSTEP Figure 5.2 (p2.c) + 종료 상태(exit status) 해석.
// wait()로 순서를 결정적으로 만들고, 자식이 정상 종료/시그널로 죽은 경우를 구분한다.
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static void report(pid_t pid, int status) {
    if (WIFEXITED(status))
        printf("  child %d exited normally, exit code = %d\n", (int)pid, WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        printf("  child %d killed by signal %d (%s)\n", (int)pid, WTERMSIG(status),
               WTERMSIG(status) == SIGSEGV ? "SIGSEGV" : "other");
}

int main(void) {
    printf("hello world (pid:%d)\n", (int)getpid());
    fflush(stdout);
    int rc = fork();
    if (rc < 0) { fprintf(stderr, "fork failed\n"); exit(1); }
    if (rc == 0) {
        usleep(100 * 1000);   // 일부러 늦게 출력해도 부모는 기다린다
        printf("hello, I am child (pid:%d)\n", (int)getpid());
        exit(3);              // 종료 코드 3
    }
    int status;
    int wc = wait(&status);
    printf("hello, I am parent of %d (wc:%d) (pid:%d)\n", rc, wc, (int)getpid());
    report(wc, status);

    // 두 번째 자식: NULL 포인터에 쓰기 → 커널이 SIGSEGV로 죽인다
    rc = fork();
    if (rc == 0) {
        volatile int *bad = NULL;
        *bad = 1;
        _exit(0);
    }
    wc = waitpid(rc, &status, 0);
    report(wc, status);

    // 기다릴 자식이 없을 때 wait()는?
    wc = wait(NULL);
    printf("  wait() with no children returns %d\n", wc);
    return 0;
}
