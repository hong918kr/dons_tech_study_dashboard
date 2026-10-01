// C04_states.c — 프로세스 상태를 실제 ps 로 관찰한다.
//  child1: 무한 루프       → Running/Ready  (ps STAT 'R')
//  child2: sleep()        → Blocked        (ps STAT 'S')
//  child3: SIGSTOP 받음    → Stopped(suspend)(ps STAT 'T')
//  child4: 바로 exit()     → wait() 전까지 Zombie (ps STAT 'Z')
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static pid_t spawn(int kind) {
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(1); }
    if (pid == 0) {
        switch (kind) {
        case 1: for (volatile unsigned long i = 0;; i++) ; // CPU만 씀
        case 2: sleep(100); break;                          // 이벤트(타이머) 대기
        case 3: for (;;) pause();                           // 곧 SIGSTOP 받음
        case 4: _exit(7);                                   // 즉시 종료 → 좀비
        }
        _exit(0);
    }
    return pid;
}

int main(void) {
    pid_t p[5];
    for (int k = 1; k <= 4; k++) p[k] = spawn(k);
    kill(p[3], SIGSTOP);
    usleep(300 * 1000);  // 자식들이 자리 잡을 시간

    char cmd[256];
    snprintf(cmd, sizeof cmd, "ps -o pid,ppid,stat,command -p %d,%d,%d,%d,%d",
             (int)getpid(), (int)p[1], (int)p[2], (int)p[3], (int)p[4]);
    printf("parent=%d  spin=%d  sleep=%d  stopped=%d  zombie=%d\n",
           (int)getpid(), (int)p[1], (int)p[2], (int)p[3], (int)p[4]);
    fflush(stdout);
    system(cmd);

    // 좀비 수거: wait 하면 종료 코드(7)를 받아 오고 PCB가 사라진다
    int status;
    pid_t w = waitpid(p[4], &status, 0);
    printf("reaped %d, exit status = %d\n", (int)w, WEXITSTATUS(status));
    fflush(stdout);
    snprintf(cmd, sizeof cmd, "ps -o pid,stat -p %d || echo '(pid %d is gone)'", (int)p[4], (int)p[4]);
    system(cmd);

    // 정리: 나머지 자식 죽이고 수거
    for (int k = 1; k <= 3; k++) { kill(p[k], SIGKILL); waitpid(p[k], NULL, 0); }
    return 0;
}
