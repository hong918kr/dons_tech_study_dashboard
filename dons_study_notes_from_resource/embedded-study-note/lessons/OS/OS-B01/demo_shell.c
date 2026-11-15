/* OS-B01 demo — solution.c 의 부품으로 만든 대화형 미니 셸 (REPL)
 *
 *   한 줄 읽기 → (SIGCHLD 플래그가 서 있으면 reap_jobs) → sh_line → 반복.  exit 이나 EOF 로 끝.
 *   파이프 입력이면 읽은 줄을 프롬프트와 함께 다시 찍어 준다 (스크립트 실행 기록용).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_shell.c -o msh && ./msh
 *        (solution.c 를 #include 한다 — 같은 디렉터리에서 빌드)
 */
#define main os_b01_tests_main                  /* solution.c 의 테스트 main 은 이름만 바꿔 둔다 */
#include "solution.c"
#undef main

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    shell_t sh;
    shell_init(&sh);
    if (install_sigchld() != 0) { perror("sigaction"); return 1; }
    bool tty = isatty(STDIN_FILENO);
    char line[LINE_MAX_];
    for (;;) {
        if (g_sigchld_flag) reap_jobs(&sh);     /* 끝난 백그라운드 작업 알림 ("[1] Done ...") */
        if (tty) { printf("msh$ "); fflush(stdout); }
        if (fgets(line, sizeof line, stdin) == NULL) break;
        line[strcspn(line, "\n")] = '\0';
        if (!tty) printf("msh$ %s\n", line);
        int code = sh_line(&sh, line);
        if (line[strspn(line, " \t")] != '\0') printf("  ($? = %d)\n", code);
        if (sh.exit_requested) break;
    }
    for (int i = 0; i < MAX_JOBS; i++)          /* 끝내기 전에 남은 작업을 기다려 회수 (좀비를 남기지 않는다) */
        if (sh.jobs[i].state == JOB_RUNNING) {
            sh.jobs[i].status = wait_status(sh.jobs[i].pid);
            sh.jobs[i].state = JOB_DONE;
            printf("[%d] reaped at exit, $? = %d\n", sh.jobs[i].jid, status_code(sh.jobs[i].status));
        }
    return sh.last_code;
}
