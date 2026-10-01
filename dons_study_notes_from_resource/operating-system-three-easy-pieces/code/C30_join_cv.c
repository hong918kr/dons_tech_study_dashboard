// C30_join_cv.c — Ch.30 Fig 30.3 의 thr_join()/thr_exit() 를 그대로 돌리고,
// "상태 변수(done) 없이 signal/wait 만 쓰는" 잘못된 버전이 왜 영원히 잠드는지(lost wakeup)
// 를 재현한다. 영원히 멈추면 데모가 안 끝나므로, 잘못된 버전의 wait 는 1초 timedwait 로 감지만 한다.
//
// build: cc -Wall -Wextra -O0 -pthread code/C30_join_cv.c -o .work/bin/C30_join_cv
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  c = PTHREAD_COND_INITIALIZER;
static int done = 0;                      // 상태 변수: "자식이 끝났나?"

// ---------- 올바른 버전 (Fig 30.3) ----------
static void thr_exit(void) {
    pthread_mutex_lock(&m);
    done = 1;                             // 1) 상태를 바꾸고
    pthread_cond_signal(&c);              // 2) 기다리는 사람을 깨운다 (락 잡은 채로)
    pthread_mutex_unlock(&m);
}
static void thr_join(void) {
    pthread_mutex_lock(&m);
    while (done == 0)                     // 상태를 보고, 아니면 잔다 (while!)
        pthread_cond_wait(&c, &m);        // 락을 풀고 잠듦 -> 깨면 락 다시 잡고 리턴
    pthread_mutex_unlock(&m);
}
static void *child_good(void *arg) {
    long delay_ms = (long)arg;
    usleep((useconds_t)(delay_ms * 1000));
    printf("  child (delay %ldms)\n", delay_ms);
    thr_exit();
    return NULL;
}

// ---------- 잘못된 버전: 상태 변수 없음 ----------
static void thr_exit_nostate(void) {
    pthread_mutex_lock(&m);
    pthread_cond_signal(&c);              // 아무도 안 자고 있으면 이 signal 은 그냥 사라진다
    pthread_mutex_unlock(&m);
}
static int thr_join_nostate(void) {       // 0 = 깨어남, ETIMEDOUT = 영원히 잘 뻔함
    struct timeval now; struct timespec dl;
    gettimeofday(&now, NULL);
    dl.tv_sec = now.tv_sec + 1; dl.tv_nsec = now.tv_usec * 1000;
    pthread_mutex_lock(&m);
    int rc = pthread_cond_timedwait(&c, &m, &dl);  // 원래 코드는 그냥 cond_wait (무한 대기)
    pthread_mutex_unlock(&m);
    return rc;
}
static void *child_bad(void *arg) {
    (void)arg;
    printf("  child runs first and signals (nobody is waiting yet)\n");
    thr_exit_nostate();
    return NULL;
}

int main(void) {
    pthread_t p;

    printf("[1] good join, parent waits first (child sleeps 50ms):\n");
    printf("  parent: begin\n");
    done = 0;
    pthread_create(&p, NULL, child_good, (void *)50L);
    thr_join();
    printf("  parent: end\n");
    pthread_join(p, NULL);

    printf("[2] good join, child finishes first (parent sleeps 50ms before join):\n");
    printf("  parent: begin\n");
    done = 0;
    pthread_create(&p, NULL, child_good, (void *)0L);
    usleep(50 * 1000);
    thr_join();                           // done==1 이므로 wait 없이 바로 통과
    printf("  parent: end\n");
    pthread_join(p, NULL);

    printf("[3] BROKEN join without state variable, child signals first:\n");
    printf("  parent: begin\n");
    pthread_create(&p, NULL, child_bad, NULL);
    usleep(50 * 1000);                    // 자식이 먼저 signal 하도록 일부러 늦게 wait
    int rc = thr_join_nostate();
    if (rc == ETIMEDOUT)
        printf("  parent: still asleep after 1s -> LOST WAKEUP (real code would hang forever)\n");
    else
        printf("  parent: woke up (rc=%d)\n", rc);
    pthread_join(p, NULL);
    return 0;
}
