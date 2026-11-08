/* C-A04 demo: 비동기 시그널 핸들러(= ISR 모델)에서 malloc 을 부르면? (관찰용, 의도적 UB)
 *   cc -std=c11 -Wall -Wextra -Werror -O2 demo_malloc_signal.c -o msig && ./msig
 * 자식 프로세스: main 은 malloc/free 를 계속 돌리고, 100 µs 주기 SIGALRM 핸들러도 malloc/free 한다.
 * 부모: 자식을 최대 3초 기다려 결과(정상 종료 / 시그널로 죽음 / 멈춤 → 강제 종료)를 보고한다.
 * malloc 은 POSIX async-signal-safe 목록에 없다 → 이 프로그램의 동작은 정의되지 않는다. */
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static atomic_long g_handler_runs;                         /* lock-free → 핸들러에서 써도 된다 */
static void *volatile g_sink_main, *volatile g_sink_isr;   /* volatile: malloc/free 쌍 제거 최적화 방지 */

static void on_alarm(int sig) {
    (void)sig;
    g_sink_isr = malloc(4000);     /* 금지된 일: 힙 락/자료구조를 main 과 공유 */
    free(g_sink_isr);
    atomic_fetch_add_explicit(&g_handler_runs, 1, memory_order_relaxed);
}

static void child(size_t sz) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_alarm;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);
    struct itimerval tv = { { 0, 100 }, { 0, 100 } };       /* 100 µs 주기 */
    setitimer(ITIMER_REAL, &tv, NULL);
    for (long i = 0; i < 5000000L; i++) {                   /* 수백 ms~수 초 분량 */
        g_sink_main = malloc(sz + (size_t)(i & 63));
        free(g_sink_main);
    }
    struct itimerval off = { { 0, 0 }, { 0, 0 } };
    setitimer(ITIMER_REAL, &off, NULL);
    printf("    (child: handler ran %ld times)\n", atomic_load(&g_handler_runs));
    fflush(stdout);
    _exit(0);
}

int main(void) {
    static const size_t sizes[] = { 32, 4000, 200000 };    /* 작은/중간/큰 할당 경로 */
    for (int r = 0; r < 3; r++) {
        for (size_t k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
            pid_t pid = fork();
            if (pid == 0) child(sizes[k]);
            int st = 0, waited = 0;
            while (waitpid(pid, &st, WNOHANG) == 0 && waited < 300) {
                struct timespec ts = { 0, 10 * 1000 * 1000 };   /* 10 ms */
                nanosleep(&ts, NULL);
                waited++;
            }
            if (waited >= 300) {
                kill(pid, SIGKILL);
                waitpid(pid, &st, 0);
                printf("run %d main malloc(%6zu): HUNG > 3 s (deadlock) -> killed\n", r, sizes[k]);
            } else if (WIFSIGNALED(st)) {
                printf("run %d main malloc(%6zu): died by signal %d after %d0 ms\n", r, sizes[k], WTERMSIG(st), waited);
            } else {
                printf("run %d main malloc(%6zu): exited %d after %d0 ms\n", r, sizes[k], WEXITSTATUS(st), waited);
            }
            fflush(stdout);
        }
    }
    return 0;
}
