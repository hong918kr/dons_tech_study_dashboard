/* ARM-A10 demo_bti.c — 착지 패드 × 분기 종류 표를 실제 하드웨어로 채운다. 각 칸은 fork 한 자식에서 실행. */
#define _POSIX_C_SOURCE 200809L   /* strsignal, fork (-std=c11 에서도) */
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
extern int pad_none(void), pad_bti(void), pad_c(void), pad_j(void), pad_jc(void), pad_pac(void);
extern int via_blr(int (*)(void)), via_br16(int (*)(void)), via_br9(int (*)(void));
int main(void) {
    struct { const char *n; int (*f)(void); } pads[] = {
        {"none", pad_none}, {"bti", pad_bti}, {"bti c", pad_c}, {"bti j", pad_j}, {"bti jc", pad_jc}, {"paciasp", pad_pac}};
    struct { const char *n; int (*v)(int (*)(void)); } vias[] = {
        {"blr (10)", via_blr}, {"br x16 (01)", via_br16}, {"br x9 (11)", via_br9}};
    printf("%-9s", "pad\\via");
    for (int j = 0; j < 3; j++) printf(" | %-12s", vias[j].n);
    printf("\n");
    for (int i = 0; i < 6; i++) {
        printf("%-9s", pads[i].n);
        for (int j = 0; j < 3; j++) {
            fflush(stdout);
            pid_t pid = fork();
            if (pid == 0) _exit(vias[j].v(pads[i].f));
            int st = 0;
            waitpid(pid, &st, 0);
            if (WIFSIGNALED(st)) printf(" | %-12s", WTERMSIG(st) == SIGILL ? "SIGILL" : "SIG?");
            else printf(" | ok(%d)%-6s", WEXITSTATUS(st), "");
        }
        printf("\n");
    }
    return 0;
}
