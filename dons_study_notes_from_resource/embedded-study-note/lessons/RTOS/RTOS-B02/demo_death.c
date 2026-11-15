/* RTOS-B02 데모 — death test: KERNEL_ASSERT 가 "진짜로" 멈추는가 (fork + waitpid, SIGABRT)
 *
 * solution.c 는 assert 훅을 카운터로 바꿔 끼워 위반을 센다 (멈추지 않으므로 25개 조합을 한 프로세스에서 검사).
 * 하지만 제품 빌드의 훅은 멈춰야 한다. 그걸 확인하는 방법이 death test:
 *   자식 프로세스(fork)에서 기본 훅(abort)으로 전이를 시도 → 부모가 waitpid 로 "SIGABRT 로 죽었나" 를 본다.
 *   (GoogleTest 의 EXPECT_DEATH 도 같은 생각: 죽을 코드를 다른 프로세스에서 돌린다)
 * 별도 파일인 이유: macOS 는 abort 로 죽은 프로세스마다 ~/Library/Logs/DiagnosticReports 에 .ips 크래시 리포트를
 * 남긴다 (실제로 확인함). 검증용으로 자주 돌리는 solution.c 에 넣으면 리포트가 쌓인다.
 *
 * 커널은 solution.c 에서 필요한 만큼만 줄여 왔다 (상태 표 + sched_run + block_on 의 핵심).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_death.c -o demo_death && ./demo_death
 */
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef enum { TASK_READY, TASK_RUNNING, TASK_BLOCKED, TASK_SUSPENDED, TASK_DELETED, TASK_NSTATES } task_state_t;
static const char *const ST_NAME[TASK_NSTATES] = { "READY", "RUNNING", "BLOCKED", "SUSPENDED", "DELETED" };
static const char LEGAL[TASK_NSTATES][TASK_NSTATES + 1] = { "01011", "10111", "10011", "10001", "00000" };

/* 제품용 KERNEL_ASSERT: 메시지 후 abort (FreeRTOSConfig.h 의 configASSERT 와 같은 모양) */
#define KERNEL_ASSERT(cond, from, to) do { if (!(cond)) { \
    fprintf(stderr, "  [child] KERNEL_ASSERT: illegal %s -> %s (%s:%d)\n", ST_NAME[from], ST_NAME[to], __FILE__, __LINE__); \
    abort(); } } while (0)

typedef struct { const char *name; task_state_t state; } tcb_t;
static tcb_t *g_current;

static void transition(tcb_t *t, task_state_t to) {   /* 모든 전이가 지나가는 관문 */
    KERNEL_ASSERT(LEGAL[t->state][to] == '1', t->state, to);
    if (to == TASK_RUNNING) g_current = t;
    if (t->state == TASK_RUNNING) g_current = NULL;
    t->state = to;
}

/* 자식에서 from 상태의 태스크를 만들고 to 로 보낸다. 반환: 종료 코드, 또는 128 + 시그널 번호 */
static int death_test(task_state_t from, task_state_t to) {
    fflush(NULL);                                     /* 부모 버퍼가 자식에서 두 번 나오지 않게 */
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return -1; }
    if (pid == 0) {
        tcb_t t = { "T", from };
        transition(&t, to);
        _exit(0);                                     /* 여기 오면 assert 가 안 걸린 것 */
    }
    int st = 0;
    if (waitpid(pid, &st, 0) != pid) { perror("waitpid"); return -1; }
    return WIFSIGNALED(st) ? 128 + WTERMSIG(st) : WEXITSTATUS(st);
}

int main(void) {
    static const struct { task_state_t from, to; } C[] = {
        { TASK_BLOCKED,   TASK_RUNNING },             /* 깨어나지도 않고 CPU 를 받음 */
        { TASK_SUSPENDED, TASK_RUNNING },             /* resume 없이 실행 */
        { TASK_RUNNING,   TASK_RUNNING },             /* 이중 dispatch */
        { TASK_DELETED,   TASK_READY },               /* use-after-delete */
        { TASK_READY,     TASK_RUNNING },             /* 합법: 살아서 exit 0 이어야 */
        { TASK_BLOCKED,   TASK_READY },               /* 합법 */
    };
    int fail = 0, n = (int)(sizeof C / sizeof C[0]);
    printf("SIGABRT = %d -> expected status for illegal = %d\n", SIGABRT, 128 + SIGABRT);
    for (int i = 0; i < n; i++) {
        bool legal = LEGAL[C[i].from][C[i].to] == '1';
        int st = death_test(C[i].from, C[i].to);
        bool ok = legal ? st == 0 : st == 128 + SIGABRT;
        printf("%-9s -> %-9s : child status %3d  (%s, expect %s) %s\n", ST_NAME[C[i].from], ST_NAME[C[i].to], st,
               st > 128 ? "killed by signal" : "exited", legal ? "exit 0" : "SIGABRT", ok ? "PASS" : "FAIL");
        fail += !ok;
    }
    printf("%d/%d passed\n", n - fail, n);
    return fail ? 1 : 0;
}
