/* ENV: linux */
/* BUILD: -pthread */
/* OS-M17 추가 연습 — 한 번에 검증 (Lima VM elinux, glibc)
 *
 *   연습 1  스레드가 robust 뮤텍스를 쥔 채 pthread_exit → EOWNERDEAD (프로세스 사망만이 아니다).
 *           같은 상황의 일반 뮤텍스 → timedlock ETIMEDOUT.
 *   연습 2  ENOTRECOVERABLE 뒤의 유일한 길: destroy + 같은 자리에 다시 init. 덤으로 consistent 오용 → EINVAL,
 *           robust 뮤텍스를 주인 아닌 쪽이 unlock → EPERM.
 *   연습 3  두 개를 쥔 채 죽으면 둘 다 EOWNERDEAD. 이미 잠들어 기다리던 프로세스도 EOWNERDEAD 로 깨어난다.
 *   연습 4  robust mutex 가 없는 OS(macOS)의 대용품: 주인 pid 를 적는 락 + kill(pid, 0) 생존 확인.
 *           좀비(아직 wait 안 한 죽은 자식)는 "살아 있음" 으로 보인다 — 그 함정을 테스트로.
 *
 * build (VM): gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s  (line %d)\n", #expr, __LINE__); } } while (0)

static struct timespec rt_after(int ms) {
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    t.tv_sec += ms / 1000;
    t.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (t.tv_nsec >= 1000000000L) { t.tv_sec++; t.tv_nsec -= 1000000000L; }
    return t;
}
static int timedlock_ms(pthread_mutex_t *m, int ms) { struct timespec d = rt_after(ms); return pthread_mutex_timedlock(m, &d); }
static void sleep_ms(int ms) {
    struct timespec t = { ms / 1000, (long)(ms % 1000) * 1000000L };
    while (nanosleep(&t, &t) != 0 && errno == EINTR) {}
}
static int wait_child(pid_t pid, int timeout_ms) {
    int st = 0;
    for (int t = 0; t < timeout_ms; t++) {
        if (waitpid(pid, &st, WNOHANG) == pid) return st;
        sleep_ms(1);
    }
    kill(pid, SIGKILL);
    waitpid(pid, &st, 0);
    return -1;
}
static pid_t fork_child(void) { fflush(stdout); pid_t p = fork(); if (p == 0) alarm(5); return p; }
static int killed9(int st) { return st != -1 && WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL; }

/* 공유 익명 매핑(fork 로 상속) 위에 pshared 뮤텍스 하나 init */
static int init_mutex(pthread_mutex_t *m, int pshared, int robust) {
    pthread_mutexattr_t a;
    int e = pthread_mutexattr_init(&a);
    if (!e && pshared) e = pthread_mutexattr_setpshared(&a, PTHREAD_PROCESS_SHARED);
    if (!e && robust) e = pthread_mutexattr_setrobust(&a, PTHREAD_MUTEX_ROBUST);
    if (!e) e = pthread_mutex_init(m, &a);
    pthread_mutexattr_destroy(&a);
    return e;
}
static void *shared_page(void) {
    void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}

/* ---------------- 연습 1: 스레드 사망 ---------------- */
static void *lock_and_exit(void *arg) {
    pthread_mutex_lock(arg);
    pthread_exit(NULL);                      /* unlock 없이 스레드 종료 */
}
/* 스레드가 쥔 채 끝난 뒤 main 의 timedlock 결과 */
static int lock_after_thread_death(int robust) {
    pthread_mutex_t m;
    if (init_mutex(&m, 0, robust)) return -1;
    pthread_t t;
    pthread_create(&t, NULL, lock_and_exit, &m);
    pthread_join(t, NULL);
    int e = timedlock_ms(&m, 100);
    if (e == EOWNERDEAD) { pthread_mutex_consistent(&m); pthread_mutex_unlock(&m); }
    if (e == 0) pthread_mutex_unlock(&m);
    if (!robust) return e;                    /* 일반 뮤텍스는 잠긴 채 버린다 (destroy 하면 EBUSY) */
    pthread_mutex_destroy(&m);
    return e;
}

/* ---------------- 연습 2: ENOTRECOVERABLE → 다시 init ---------------- */
static int reinit_after_notrecoverable(pthread_mutex_t *m) {
    int e = pthread_mutex_destroy(m);        /* POSIX: 이 상태에서 허용되는 유일한 연산 */
    if (e) return e;
    return init_mutex(m, 1, 1);              /* 같은 주소에 새 뮤텍스 — 다른 프로세스도 그대로 쓴다 */
}
static void *unlock_foreign(void *arg) { return (void *)(long)pthread_mutex_unlock(arg); }

/* ---------------- 연습 4: pid 락 (robust 대용품) ---------------- */
typedef struct { atomic_int owner; } pidlock_t;   /* 0 = 풀림, 그 밖 = 주인 pid */
static int pid_gone(int pid) { return kill(pid, 0) == -1 && errno == ESRCH; }
/* 0 = 획득, EOWNERDEAD = 죽은 주인에게서 빼앗음, ETIMEDOUT */
static int pidlock_lock(pidlock_t *l, int timeout_ms) {
    int me = (int)getpid();
    for (int t = 0; t <= timeout_ms; t++) {
        int cur = 0;
        if (atomic_compare_exchange_strong(&l->owner, &cur, me)) return 0;
        if (pid_gone(cur) && atomic_compare_exchange_strong(&l->owner, &cur, me)) return EOWNERDEAD;
        sleep_ms(1);
    }
    return ETIMEDOUT;
}
static void pidlock_unlock(pidlock_t *l) { atomic_store(&l->owner, 0); }

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);

    printf("== 연습 1: 스레드가 쥔 채 종료 ==\n");
    CHECK(lock_after_thread_death(1) == EOWNERDEAD);
    CHECK(lock_after_thread_death(0) == ETIMEDOUT);

    printf("== 연습 2: ENOTRECOVERABLE 뒤 다시 init ==\n");
    pthread_mutex_t *m = shared_page();
    CHECK(m != NULL && init_mutex(m, 1, 1) == 0);
    if (m) {
        CHECK(pthread_mutex_lock(m) == 0);
        CHECK(pthread_mutex_consistent(m) == EINVAL);     /* 일관된 뮤텍스에 consistent */
        pthread_t t;
        void *ret = NULL;
        pthread_create(&t, NULL, unlock_foreign, m);
        pthread_join(t, &ret);
        CHECK((long)ret == EPERM);                         /* robust 는 주인 검사를 한다 */
        pthread_mutex_unlock(m);
        pid_t p = fork_child();
        if (p == 0) { pthread_mutex_lock(m); raise(SIGKILL); }
        CHECK(killed9(wait_child(p, 3000)));
        CHECK(timedlock_ms(m, 200) == EOWNERDEAD);
        pthread_mutex_unlock(m);                           /* consistent 없이 → 영구 불능 */
        CHECK(timedlock_ms(m, 50) == ENOTRECOVERABLE);
        CHECK(reinit_after_notrecoverable(m) == 0);
        p = fork_child();
        if (p == 0) _exit(timedlock_ms(m, 200) == 0 && pthread_mutex_unlock(m) == 0 ? 0 : 1);
        int st = wait_child(p, 3000);
        CHECK(st != -1 && WIFEXITED(st) && WEXITSTATUS(st) == 0);
        pthread_mutex_destroy(m);
        munmap(m, 4096);
    }

    printf("== 연습 3: 두 개 쥔 채 사망, 이미 기다리던 쪽 ==\n");
    pthread_mutex_t *ab = shared_page();
    CHECK(ab != NULL && init_mutex(&ab[0], 1, 1) == 0 && init_mutex(&ab[1], 1, 1) == 0);
    if (ab) {
        int pfd[2];
        CHECK(pipe(pfd) == 0);
        pid_t p = fork_child();
        if (p == 0) {
            pthread_mutex_lock(&ab[0]);
            pthread_mutex_lock(&ab[1]);
            char c = 'x';
            if (write(pfd[1], &c, 1) != 1) _exit(2);
            for (;;) pause();                            /* 부모가 죽인다 */
        }
        char c;
        CHECK(read(pfd[0], &c, 1) == 1);
        /* waiter 프로세스가 ab[0] 에서 3 초 timedlock 으로 잠든 뒤 주인을 죽인다 */
        int e0 = -1;
        pid_t w = fork_child();
        if (w == 0) {
            int e = timedlock_ms(&ab[0], 3000);
            if (e == EOWNERDEAD) { pthread_mutex_consistent(&ab[0]); pthread_mutex_unlock(&ab[0]); }
            _exit(e == EOWNERDEAD ? 0 : 1);
        }
        sleep_ms(50);                                    /* waiter 가 잠들 시간 (결과는 잠들었든 아니든 같다) */
        kill(p, SIGKILL);
        CHECK(killed9(wait_child(p, 3000)));
        int sw = wait_child(w, 4000);
        CHECK(sw != -1 && WIFEXITED(sw) && WEXITSTATUS(sw) == 0);   /* waiter 가 EOWNERDEAD 를 받았다 */
        CHECK(timedlock_ms(&ab[0], 200) == 0);           /* waiter 가 consistent 했으니 평범 */
        pthread_mutex_unlock(&ab[0]);
        e0 = timedlock_ms(&ab[1], 200);
        CHECK(e0 == EOWNERDEAD);                         /* 두 번째 락도 표시됐다 (robust list) */
        if (e0 == EOWNERDEAD) { pthread_mutex_consistent(&ab[1]); pthread_mutex_unlock(&ab[1]); }
        close(pfd[0]); close(pfd[1]);
        pthread_mutex_destroy(&ab[0]); pthread_mutex_destroy(&ab[1]);
        munmap(ab, 4096);
    }

    printf("== 연습 4: pid 락 + kill(pid, 0) — 좀비 함정 ==\n");
    pidlock_t *pl = shared_page();
    CHECK(pl != NULL);
    if (pl) {
        int pfd[2];
        CHECK(pipe(pfd) == 0);
        pid_t p = fork_child();
        if (p == 0) {
            if (pidlock_lock(pl, 100) != 0) _exit(2);
            char c = 'x';
            if (write(pfd[1], &c, 1) != 1) _exit(2);
            raise(SIGKILL);
        }
        char c;
        CHECK(read(pfd[0], &c, 1) == 1 && atomic_load(&pl->owner) == (int)p);
        CHECK(pidlock_lock(pl, 30) == ETIMEDOUT);         /* 죽었지만 아직 좀비: kill(pid,0) == 0 */
        CHECK(killed9(wait_child(p, 3000)));             /* 수거하면 */
        CHECK(pidlock_lock(pl, 30) == EOWNERDEAD);        /* 그제야 ESRCH → 빼앗는다 */
        CHECK(atomic_load(&pl->owner) == (int)getpid());
        pidlock_unlock(pl);
        CHECK(pidlock_lock(pl, 30) == 0);
        pidlock_unlock(pl);
        close(pfd[0]); close(pfd[1]);
        munmap(pl, 4096);
    }

    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
