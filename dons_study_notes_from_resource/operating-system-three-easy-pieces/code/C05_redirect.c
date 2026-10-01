// C05_redirect.c — OSTEP Figure 5.4 (p4.c): "wc file > out" 을 셸처럼 구현.
// 방법 A: close(1) 후 open() → 가장 낮은 빈 fd(=1)가 재사용된다 (책의 방법)
// 방법 B: open() 후 dup2(fd, 1) → 실제 셸이 쓰는 방법 (경쟁/가정 없음)
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void run_wc_into(const char *src, const char *out, int use_dup2) {
    int rc = fork();
    if (rc < 0) { perror("fork"); exit(1); }
    if (rc == 0) {
        if (!use_dup2) {
            close(STDOUT_FILENO);                                   // fd 1 비우기
            int fd = open(out, O_CREAT | O_WRONLY | O_TRUNC, S_IRWXU); // → fd 1 받음
            fprintf(stderr, "  [child A] open() returned fd %d\n", fd);
        } else {
            int fd = open(out, O_CREAT | O_WRONLY | O_TRUNC, S_IRWXU);
            fprintf(stderr, "  [child B] open() returned fd %d, dup2(%d, 1)\n", fd, fd);
            dup2(fd, STDOUT_FILENO);                                // fd 1 ← fd
            close(fd);
        }
        execlp("wc", "wc", src, (char *)NULL);
        perror("exec");
        _exit(127);
    }
    waitpid(rc, NULL, 0);
}

int main(void) {
    run_wc_into("code/C05_redirect.c", "/tmp/ostep_p4_A.output", 0);
    run_wc_into("code/C05_redirect.c", "/tmp/ostep_p4_B.output", 1);
    printf("(parent) nothing from wc appeared on my terminal. Files:\n");
    fflush(stdout);
    system("cat /tmp/ostep_p4_A.output /tmp/ostep_p4_B.output");
    return 0;
}
