#define _DEFAULT_SOURCE 1
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdatomic.h>
#include <unistd.h>

static volatile sig_atomic_t stop_flag;          /* 핸들러가 건드려도 되는 유일한 종류 */
static atomic_int worker_loops;

static void on_sigint(int sig) { (void)sig; stop_flag = 1; }   /* 여기서 printf 금지 */

static void *worker(void *a) {
    (void)a;
    while (!stop_flag) { atomic_fetch_add(&worker_loops, 1); usleep(1000); }
    return NULL;
}
int main(void) {
    struct sigaction sa = {0};
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, NULL);

    pthread_t t; pthread_create(&t, NULL, worker, NULL);
    usleep(100000);
    printf("SIGINT 를 나 자신에게 보낸다 (pid %d)\n", (int)getpid());
    kill(getpid(), SIGINT);
    pthread_join(t, NULL);                       /* 워커가 스스로 빠져나온다 */
    printf("워커가 %d 번 돌고 정상 종료했다, stop_flag=%d\n",
           atomic_load(&worker_loops), (int)stop_flag);
    return 0;
}
