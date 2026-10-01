// C05_minishell.c — fork/exec/wait + dup2 + pipe 로 만든 75줄짜리 셸.
// 지원: "cmd args", "cmd args > file", "cmd1 args | cmd2 args", "exit"
// stdin에서 한 줄씩 읽는다 (스크립트를 파이프로 넣어 테스트 가능).
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAXARGS 16

// "a b > f" → argv={"a","b",NULL}, *outfile="f"
static void parse(char *s, char **argv, char **outfile) {
    int n = 0;
    *outfile = NULL;
    for (char *tok = strtok(s, " \t\n"); tok && n < MAXARGS - 1; tok = strtok(NULL, " \t\n")) {
        if (strcmp(tok, ">") == 0) { *outfile = strtok(NULL, " \t\n"); break; }
        argv[n++] = tok;
    }
    argv[n] = NULL;
}

// 자식 안에서만 호출: (필요하면) fd 재배선 후 exec
static void exec_child(char **argv, char *outfile, int in_fd, int out_fd) {
    if (in_fd != STDIN_FILENO)  { dup2(in_fd, STDIN_FILENO);   close(in_fd); }
    if (out_fd != STDOUT_FILENO){ dup2(out_fd, STDOUT_FILENO); close(out_fd); }
    if (outfile) {
        int fd = open(outfile, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd < 0) { perror(outfile); _exit(1); }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
    execvp(argv[0], argv);
    fprintf(stderr, "minish: %s: command not found\n", argv[0]);
    _exit(127);
}

int main(void) {
    char line[512];
    for (;;) {
        printf("minish> ");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) break;
        printf("%s", line);                       // 스크립트 입력을 화면에 에코
        fflush(stdout);
        char *bar = strchr(line, '|');
        char *a1[MAXARGS], *a2[MAXARGS], *o1, *o2;
        if (!bar) {
            parse(line, a1, &o1);
            if (!a1[0]) continue;
            if (strcmp(a1[0], "exit") == 0) break;
            pid_t pid = fork();
            if (pid == 0) exec_child(a1, o1, STDIN_FILENO, STDOUT_FILENO);
            int st;
            waitpid(pid, &st, 0);
            if (WEXITSTATUS(st)) printf("[exit %d]\n", WEXITSTATUS(st));
        } else {
            *bar = '\0';
            parse(line, a1, &o1);
            parse(bar + 1, a2, &o2);
            int p[2];
            pipe(p);
            pid_t l = fork();
            if (l == 0) { close(p[0]); exec_child(a1, NULL, STDIN_FILENO, p[1]); }
            pid_t r = fork();
            if (r == 0) { close(p[1]); exec_child(a2, o2, p[0], STDOUT_FILENO); }
            close(p[0]); close(p[1]);
            waitpid(l, NULL, 0);
            waitpid(r, NULL, 0);
        }
    }
    printf("\nbye\n");
    return 0;
}
