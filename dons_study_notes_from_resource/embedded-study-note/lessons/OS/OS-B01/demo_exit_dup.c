/* OS-B01 demo — exec 실패 후 자식이 exit() 를 부르면 부모의 stdio 버퍼가 두 번 나간다
 *
 *   ./dex        자식이 _exit(127)  → "hello" 한 번
 *   ./dex exit   자식이 exit(127)   → "hello" 두 번 (fork 가 복사한 버퍼를 자식도 flush)
 *   출력이 파이프/파일이면 완전 버퍼링이라 더 잘 보인다:  ./dex exit | cat
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_exit_dup.c -o dex
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char **argv) {
    int use_exit = argc > 1 && strcmp(argv[1], "exit") == 0;
    printf("hello from parent %d (still in the stdio buffer) | ", (int)getpid());   /* 개행 없음 → 버퍼에 남음 */
    pid_t pid = fork();                          /* 자식은 이 버퍼까지 복사해 간다 */
    if (pid == 0) {
        execlp("no_such_cmd_demo", "no_such_cmd_demo", (char *)NULL);
        if (use_exit) exit(127);                 /* atexit + stdio flush → 부모 몫의 글자가 또 나감 */
        _exit(127);                              /* 커널로 바로 — 버퍼는 버려진다 */
    }
    int st;
    waitpid(pid, &st, 0);
    printf("child exit code %d (%s)\n", WEXITSTATUS(st), use_exit ? "exit" : "_exit");
    return 0;
}
