/* ENV: linux */
/* BUILD: -pthread */
/* OS-M17 데모: 주인이 죽는 순간 기다리던 쪽은 언제, 무엇을 받나 + glibc 락 단어 들여다보기
 *
 *   부모가 pshared 뮤텍스에서 timedlock(1000 ms) 으로 잠들어 있는 동안, 락을 쥔 자식이 SIGKILL 로 죽는다.
 *   - 일반 뮤텍스: 아무도 깨워 주지 않는다 → 1000 ms 꽉 채우고 ETIMEDOUT
 *   - robust    : 커널이 exit 때 robust list 를 걷어 OWNER_DIED 비트를 세우고 대기자를 깨운다 → 곧바로 EOWNERDEAD
 *   락 단어(__data.__lock)와 __owner 는 glibc 내부 필드 — 관찰용으로만 읽는다 (이식성 없음).
 *
 * build (VM): gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_owner_death.c -o od && ./od
 */
#define _GNU_SOURCE /* gettid() */
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
    struct timespec died_at; /* 자식이 죽기 직전에 적는 CLOCK_MONOTONIC */
    int tid;
} shared_t;

static double ms_between(struct timespec a, struct timespec b) {
    return (double)(b.tv_sec - a.tv_sec) * 1e3 + (double)(b.tv_nsec - a.tv_nsec) / 1e6;
}
static int cmpd(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* 한 번 실행: 반환 = timedlock 결과, *lat = 자식 사망 → 부모 반환 (ms) */
static int one(shared_t *s, int robust, double *lat, int verbose) {
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_setpshared(&a, PTHREAD_PROCESS_SHARED);
    if (robust) pthread_mutexattr_setrobust(&a, PTHREAD_MUTEX_ROBUST);
    pthread_mutex_init(&s->mu, &a);
    pthread_mutexattr_destroy(&a);
    int pfd[2];
    if (pipe(pfd) != 0) return -1;
    fflush(stdout);
    pid_t p = fork();
    if (p == 0) {
        pthread_mutex_lock(&s->mu);
        s->tid = (int)gettid();
        char c = 'x';
        if (write(pfd[1], &c, 1) != 1) _exit(2);
        struct timespec t = { 0, 100 * 1000000L };
        nanosleep(&t, NULL);                 /* 부모가 timedlock 안에서 잠들 시간 */
        clock_gettime(CLOCK_MONOTONIC, &s->died_at);
        raise(SIGKILL);
    }
    char c;
    if (read(pfd[0], &c, 1) != 1) return -1;
    close(pfd[0]); close(pfd[1]);
    if (verbose)
        printf("  [%s] 자식 tid=%d 가 쥔 상태: __lock=0x%08x __owner=%d\n", robust ? "robust" : "normal", s->tid,
               (unsigned)s->mu.__data.__lock, s->mu.__data.__owner);
    struct timespec dl;
    clock_gettime(CLOCK_REALTIME, &dl);
    dl.tv_sec += 1;
    int e = pthread_mutex_timedlock(&s->mu, &dl);
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    *lat = ms_between(s->died_at, now);
    int st;
    waitpid(p, &st, 0);
    if (verbose)
        printf("  [%s] timedlock → %s, 사망 후 %.3f ms 만에 반환. 반환 직후 __lock=0x%08x __owner=%d (내 tid=%d)\n",
               robust ? "robust" : "normal", e == EOWNERDEAD ? "EOWNERDEAD" : strerror(e), *lat,
               (unsigned)s->mu.__data.__lock, s->mu.__data.__owner, (int)gettid());
    if (e == EOWNERDEAD) pthread_mutex_consistent(&s->mu);
    if (e == 0 || e == EOWNERDEAD) pthread_mutex_unlock(&s->mu);
    return e;
}

/* 아무도 기다리지 않는 상태에서 죽었을 때 커널이 남긴 단어 */
static void word_after_death(shared_t *s) {
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_setpshared(&a, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&a, PTHREAD_MUTEX_ROBUST);
    pthread_mutex_init(&s->mu, &a);
    pthread_mutexattr_destroy(&a);
    fflush(stdout);
    pid_t p = fork();
    if (p == 0) { pthread_mutex_lock(&s->mu); s->tid = (int)gettid(); raise(SIGKILL); }
    int st;
    waitpid(p, &st, 0);
    printf("  [robust, 대기자 없음] 죽은 tid=%d, 죽은 뒤 __lock=0x%08x (FUTEX_OWNER_DIED=0x40000000, TID 부분=%u)\n",
           s->tid, (unsigned)s->mu.__data.__lock, (unsigned)s->mu.__data.__lock & 0x3fffffffu);
    struct timespec dl;
    clock_gettime(CLOCK_REALTIME, &dl);
    dl.tv_sec += 1;
    int e = pthread_mutex_timedlock(&s->mu, &dl);
    printf("  [robust, 대기자 없음] 다음 lock → %s, __lock=0x%08x\n", e == EOWNERDEAD ? "EOWNERDEAD" : strerror(e),
           (unsigned)s->mu.__data.__lock);
    pthread_mutex_consistent(&s->mu);
    pthread_mutex_unlock(&s->mu);
}

static shared_t *fresh(void) {  /* 매번 새 공유 페이지: 죽은 주인이 남긴 뮤텍스를 다시 init 하지 않는다 */
    shared_t *s = mmap(NULL, sizeof *s, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (s == MAP_FAILED) exit(1);
    return s;
}

int main(void) {
    shared_t *s;
    double lat;
    printf("== 한 번씩 자세히 ==\n");
    one(s = fresh(), 0, &lat, 1);   /* 일반 뮤텍스는 잠긴 채 버린다 (destroy 하면 EBUSY) */
    munmap(s, sizeof *s);
    one(s = fresh(), 1, &lat, 1);
    munmap(s, sizeof *s);
    word_after_death(s = fresh());
    munmap(s, sizeof *s);

    enum { R = 21 };
    double v[R];
    int owner_dead = 0;
    for (int i = 0; i < R; i++) {
        owner_dead += one(s = fresh(), 1, &v[i], 0) == EOWNERDEAD;
        munmap(s, sizeof *s);
    }
    qsort(v, R, sizeof v[0], cmpd);
    printf("== robust %d 회: EOWNERDEAD %d 회, 사망→깨어남 min %.3f / median %.3f / max %.3f ms ==\n", R, owner_dead,
           v[0], v[R / 2], v[R - 1]);
    return 0;
}
