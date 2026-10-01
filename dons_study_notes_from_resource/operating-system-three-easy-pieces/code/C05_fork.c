// C05_fork.c — OSTEP Figure 5.1 (p1.c). fork()는 한 번 불리고 두 번 리턴한다.
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void) {
    printf("hello world (pid:%d)\n", (int)getpid());
    fflush(stdout);                       // fork 전에 버퍼 비우기 (이유는 C05_fork_buffer.c)
    int rc = fork();
    if (rc < 0) {                         // fork 실패
        fprintf(stderr, "fork failed\n");
        exit(1);
    } else if (rc == 0) {                 // 자식: fork()가 0을 리턴
        printf("hello, I am child (pid:%d)\n", (int)getpid());
    } else {                              // 부모: fork()가 자식 PID를 리턴
        printf("hello, I am parent of %d (pid:%d)\n", rc, (int)getpid());
    }
    return 0;
}
