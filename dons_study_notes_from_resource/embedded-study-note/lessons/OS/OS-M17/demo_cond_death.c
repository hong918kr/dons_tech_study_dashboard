/* ENV: linux */
/* BUILD: -pthread */
/* OS-M17 데모: 조건변수는 robust 가 아니다 — "조건변수 안에서 잠든 채 죽은 프로세스" 가 남기는 것 (glibc 관찰)
 *
 *   ① 대기자 A 가 pthread_cond_wait 안에서 SIGKILL 로 죽는다 (뮤텍스는 안 쥔 상태 → EOWNERDEAD 도 없다).
 *      그다음 살아 있는 대기자 B 가 잠들고, 부모가 pthread_cond_signal 을 한 번 → B 는 깨어나나?
 *      같은 실험을 broadcast 로도.
 *   ② 그 조건변수를 pthread_cond_destroy 하면? (자식에서 alarm(2) watchdog 을 걸고 시도)
 *   결과는 glibc 구현 세부 — POSIX 는 "대기 중에 죽은 프로세스" 에 대해 아무것도 약속하지 않는다.
 *
 * build (VM): gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_cond_death.c -o cd && ./cd
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int waiting; /* cond 에서 자는 프로세스 수 (뮤텍스 아래에서만 바꾼다) */
} shared_t;

static void sleep_ms(int ms) { struct timespec t = { 0, ms * 1000000L }; nanosleep(&t, NULL); }
static shared_t *fresh(void) {
    shared_t *s = mmap(NULL, sizeof *s, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (s == MAP_FAILED) exit(1);
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_setpshared(&a, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&a, PTHREAD_MUTEX_ROBUST);
    pthread_mutex_init(&s->mu, &a);
    pthread_condattr_t c;
    pthread_condattr_init(&c);
    pthread_condattr_setpshared(&c, PTHREAD_PROCESS_SHARED);
    pthread_condattr_setclock(&c, CLOCK_MONOTONIC);
    pthread_cond_init(&s->cv, &c);
    return s;
}
/* 대기자 프로세스: timeout_ms (0 = 무한) 동안 cond 대기. exit code 0 = signal 로 깨어남, 1 = ETIMEDOUT */
static pid_t waiter(shared_t *s, int timeout_ms) {
    fflush(stdout);
    pid_t p = fork();
    if (p != 0) return p;
    alarm(5);
    pthread_mutex_lock(&s->mu);
    s->waiting++;
    int e;
    if (timeout_ms) {
        struct timespec dl;
        clock_gettime(CLOCK_MONOTONIC, &dl);
        dl.tv_nsec += (long)timeout_ms * 1000000L;
        while (dl.tv_nsec >= 1000000000L) { dl.tv_sec++; dl.tv_nsec -= 1000000000L; }
        e = pthread_cond_timedwait(&s->cv, &s->mu, &dl);
    } else {
        e = pthread_cond_wait(&s->cv, &s->mu);
    }
    s->waiting--;
    pthread_mutex_unlock(&s->mu);
    _exit(e == 0 ? 0 : 1);
}
static void until_waiting(shared_t *s, int n) { /* 뮤텍스를 잡았을 때 waiting==n 이면 그들은 cond 안에서 잔다 */
    for (int t = 0; t < 2000; t++) {
        pthread_mutex_lock(&s->mu);
        int w = s->waiting;
        pthread_mutex_unlock(&s->mu);
        if (w >= n) return;
        sleep_ms(1);
    }
}
static const char *how(int st) {
    if (WIFEXITED(st)) return WEXITSTATUS(st) == 0 ? "깨어남(0)" : "ETIMEDOUT";
    if (WIFSIGNALED(st)) return WTERMSIG(st) == SIGALRM ? "멈춤 → alarm(2) 으로 죽음" : "시그널로 죽음";
    return "?";
}

static int trial(int dead_waiters, int broadcast) {
    shared_t *s = fresh();
    pid_t dead[4];
    for (int i = 0; i < dead_waiters; i++) {
        dead[i] = waiter(s, 0);
        until_waiting(s, i + 1);
        kill(dead[i], SIGKILL);              /* cond_wait 안에서 즉사 */
        waitpid(dead[i], NULL, 0);
    }
    pid_t b = waiter(s, 400);
    until_waiting(s, dead_waiters + 1);      /* 죽은 대기자는 waiting-- 를 못 했다 → +1 */
    pthread_mutex_lock(&s->mu);
    if (broadcast) pthread_cond_broadcast(&s->cv); else pthread_cond_signal(&s->cv);
    pthread_mutex_unlock(&s->mu);
    int st;
    waitpid(b, &st, 0);
    munmap(s, sizeof *s);                    /* destroy 는 ② 에서 따로 */
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

int main(void) {
    printf("== ① 죽은 대기자 k 명 뒤의 signal/broadcast 한 번 → 살아 있는 대기자 B (10 회씩) ==\n");
    for (int k = 0; k <= 2; k++)
        for (int bc = 0; bc <= 1; bc++) {
            int woke = 0;
            for (int i = 0; i < 10; i++) woke += trial(k, bc);
            printf("  죽은 대기자 %d, %-9s → B 깨어남 %2d/10\n", k, bc ? "broadcast" : "signal", woke);
        }

    printf("== ② 죽은 대기자가 있던 조건변수에 pthread_cond_destroy ==\n");
    static const char *what[] = { "죽은 대기자 0 명", "죽은 대기자 1 명, 깨움 없음", "죽은 대기자 1 명 + signal 1 번",
                                  "죽은 대기자 1 명 + broadcast 1 번" };
    for (int k = 0; k < 4; k++) {
        shared_t *s = fresh();
        if (k) {
            pid_t a = waiter(s, 0);
            until_waiting(s, 1);
            kill(a, SIGKILL);
            waitpid(a, NULL, 0);
            pthread_mutex_lock(&s->mu);
            if (k == 2) pthread_cond_signal(&s->cv);       /* 죽은 대기자에게 깨움을 보낸 뒤 */
            if (k == 3) pthread_cond_broadcast(&s->cv);
            pthread_mutex_unlock(&s->mu);
        }
        fflush(stdout);
        pid_t d = fork();
        if (d == 0) { alarm(2); _exit(pthread_cond_destroy(&s->cv)); }
        int st;
        waitpid(d, &st, 0);
        printf("  %-34s: destroy → %s\n", what[k],
               WIFEXITED(st) ? (WEXITSTATUS(st) == 0 ? "0 (정상)" : strerror(WEXITSTATUS(st))) : how(st));
        munmap(s, sizeof *s);
    }
    return 0;
}
