// C05_exec.c — OSTEP Figure 5.3 (p3.c). 자식이 execvp()로 'wc'가 된다.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    const char *target = argc > 1 ? argv[1] : "code/C05_exec.c";
    printf("hello world (pid:%d)\n", (int)getpid());
    fflush(stdout);
    int rc = fork();
    if (rc < 0) {
        fprintf(stderr, "fork failed\n");
        exit(1);
    } else if (rc == 0) {
        printf("hello, I am child (pid:%d)\n", (int)getpid());
        fflush(stdout);
        char *myargs[3];
        myargs[0] = strdup("wc");          // 프로그램: wc
        myargs[1] = strdup(target);        // 인자: 셀 파일
        myargs[2] = NULL;                  // argv 끝 표시
        execvp(myargs[0], myargs);         // 성공하면 절대 돌아오지 않는다
        printf("this shouldn't print out\n");
    } else {
        int wc = wait(NULL);
        printf("hello, I am parent of %d (wc:%d) (pid:%d)\n", rc, wc, (int)getpid());
    }
    return 0;
}
