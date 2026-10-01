// C27_thread_api.c — OSTEP Ch.27 Thread API 를 한 파일에서 전부 써 본다.
//  1) 구조체로 인자 넘기고, malloc 한 구조체로 결과 돌려받기 (Fig 27.2)
//  2) 정수 하나는 intptr_t 캐스팅으로 넘기기 (Fig 27.3 의 안전한 버전)
//  3) 스레드 속성(attr): 스택 크기 확인/변경
//  4) mutex: 초기화 · trylock(EBUSY) · ERRORCHECK 타입으로 "남의 락 unlock" 잡기(EPERM)
//  5) condition variable: ready 플래그를 while 로 기다리기
// build: cc -Wall -Wextra -O0 -pthread code/C27_thread_api.c -o .work/bin/C27_thread_api
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// ---------- 에러 체크 래퍼 (Fig 27.4 스타일) ----------
static void Pthread_create(pthread_t *t, const pthread_attr_t *a, void *(*f)(void *), void *arg) {
    int rc = pthread_create(t, a, f, arg);
    if (rc != 0) { fprintf(stderr, "pthread_create: %s\n", strerror(rc)); exit(1); }
}
static void Pthread_join(pthread_t t, void **ret) {
    int rc = pthread_join(t, ret);
    if (rc != 0) { fprintf(stderr, "pthread_join: %s\n", strerror(rc)); exit(1); }
}
static void Pthread_mutex_lock(pthread_mutex_t *m)   { int rc = pthread_mutex_lock(m);   assert(rc == 0); (void)rc; }
static void Pthread_mutex_unlock(pthread_mutex_t *m) { int rc = pthread_mutex_unlock(m); assert(rc == 0); (void)rc; }

// ---------- 1) 구조체 인자 / 구조체 반환 ----------
typedef struct { int a; int b; } myarg_t;
typedef struct { int x; int y; } myret_t;

static void *sum_thread(void *arg) {
    myarg_t *m = (myarg_t *)arg;
    myret_t *r = malloc(sizeof(*r));          // 힙에! 스택 변수 주소를 돌려주면 안 된다
    assert(r != NULL);
    r->x = m->a + m->b;
    r->y = m->a * m->b;
    return r;
}

// ---------- 2) 정수 하나 넘기기 ----------
static void *plus_one(void *arg) {
    intptr_t v = (intptr_t)arg;               // 포인터 크기 정수로 왕복 캐스팅
    return (void *)(v + 1);
}

// ---------- 3) 스택 크기 ----------
static void *stack_probe(void *arg) {
    (void)arg;
    size_t sz = pthread_get_stacksize_np(pthread_self());   // macOS 전용 확장
    return (void *)(uintptr_t)sz;
}

// ---------- 4) mutex 동작 ----------
static pthread_mutex_t m_err;                  // ERRORCHECK 타입 (동적 초기화)
static void *unlock_someone_elses(void *arg) {
    (void)arg;
    int rc = pthread_mutex_unlock(&m_err);     // main 이 잡고 있는 락을 남이 풀려 하면?
    return (void *)(intptr_t)rc;
}

// ---------- 5) condition variable ----------
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static int ready = 0;

static void *waiter(void *arg) {
    (void)arg;
    int wakeups = 0;
    Pthread_mutex_lock(&lock);
    while (ready == 0) {                        // if 가 아니라 while!
        pthread_cond_wait(&cond, &lock);        // 잠들면서 lock 을 놓고, 깨면 다시 잡는다
        wakeups++;
    }
    Pthread_mutex_unlock(&lock);
    return (void *)(intptr_t)wakeups;
}

int main(void) {
    pthread_t p;

    // 1)
    myarg_t args = { 10, 20 };
    myret_t *res;
    Pthread_create(&p, NULL, sum_thread, &args);
    Pthread_join(p, (void **)&res);
    printf("[1] struct arg  -> returned x=%d y=%d\n", res->x, res->y);
    free(res);

    // 2)
    void *rv;
    Pthread_create(&p, NULL, plus_one, (void *)(intptr_t)100);
    Pthread_join(p, &rv);
    printf("[2] int arg 100 -> returned %ld\n", (long)(intptr_t)rv);

    // 3)
    printf("[3] main thread stack       = %zu KB\n", pthread_get_stacksize_np(pthread_self()) / 1024);
    Pthread_create(&p, NULL, stack_probe, NULL);
    Pthread_join(p, &rv);
    printf("    default new-thread stack = %zu KB\n", (size_t)(uintptr_t)rv / 1024);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 4 * 1024 * 1024);
    Pthread_create(&p, &attr, stack_probe, NULL);
    Pthread_join(p, &rv);
    pthread_attr_destroy(&attr);
    printf("    with attr setstacksize   = %zu KB\n", (size_t)(uintptr_t)rv / 1024);

    // 4)
    pthread_mutexattr_t ma;
    pthread_mutexattr_init(&ma);
    pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_ERRORCHECK);
    int rc = pthread_mutex_init(&m_err, &ma);
    assert(rc == 0);
    pthread_mutexattr_destroy(&ma);
    Pthread_mutex_lock(&m_err);
    rc = pthread_mutex_trylock(&m_err);
    printf("[4] trylock on held lock      -> %d (%s)\n", rc, strerror(rc));
    rc = pthread_mutex_lock(&m_err);
    printf("    relock by owner (ERRCHECK) -> %d (%s)\n", rc, strerror(rc));
    Pthread_create(&p, NULL, unlock_someone_elses, NULL);
    Pthread_join(p, &rv);
    rc = (int)(intptr_t)rv;
    printf("    unlock by non-owner        -> %d (%s)\n", rc, strerror(rc));
    Pthread_mutex_unlock(&m_err);
    pthread_mutex_destroy(&m_err);

    // 5)
    Pthread_create(&p, NULL, waiter, NULL);
    usleep(100 * 1000);                         // waiter 가 먼저 잠들도록 잠깐 기다림
    Pthread_mutex_lock(&lock);
    ready = 1;
    pthread_cond_signal(&cond);
    Pthread_mutex_unlock(&lock);
    Pthread_join(p, &rv);
    printf("[5] cond: waiter woke up after %ld wakeup(s), ready=%d\n", (long)(intptr_t)rv, ready);
    return 0;
}
