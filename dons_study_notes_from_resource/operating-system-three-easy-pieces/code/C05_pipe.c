// C05_pipe.c — Homework 8: 자식 두 개를 pipe()로 연결. 셸의 "grep -o fork FILE | wc -l".
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    const char *file = argc > 1 ? argv[1] : "code/C05_pipe.c";
    int fds[2];                       // fds[0] = 읽는 쪽, fds[1] = 쓰는 쪽
    if (pipe(fds) < 0) { perror("pipe"); exit(1); }
    printf("pipe fds: read=%d write=%d\n", fds[0], fds[1]);
    fflush(stdout);

    pid_t left = fork();
    if (left == 0) {                  // 왼쪽: stdout → 파이프 쓰기 끝
        dup2(fds[1], STDOUT_FILENO);
        close(fds[0]); close(fds[1]);
        execlp("grep", "grep", "-o", "fork", file, (char *)NULL);
        _exit(127);
    }
    pid_t right = fork();
    if (right == 0) {                 // 오른쪽: stdin ← 파이프 읽기 끝
        dup2(fds[0], STDIN_FILENO);
        close(fds[0]); close(fds[1]);
        execlp("wc", "wc", "-l", (char *)NULL);
        _exit(127);
    }
    // 부모는 양쪽 끝을 반드시 닫는다. 쓰기 끝을 안 닫으면 wc가 EOF를 영영 못 본다.
    close(fds[0]); close(fds[1]);
    int st;
    waitpid(left, &st, 0);
    printf("grep (pid %d) exit %d\n", (int)left, WEXITSTATUS(st));
    fflush(stdout);
    waitpid(right, &st, 0);
    printf("wc   (pid %d) exit %d\n", (int)right, WEXITSTATUS(st));
    return 0;
}
