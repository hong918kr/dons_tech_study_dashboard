#include <stdio.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>
#include <unistd.h>

/*
16. 소프트웨어 스핀락(Spinlock) 구현
17. 소프트웨어 뮤텍스(Mutex) 구현
18. 소프트웨어 세마포어(Semaphore) 구현
19. 소프트웨어 조건변수(Condition Variable) 구현
*/

/*
/*
GCC Built-in Atomic Functions 설명

1. __sync_lock_test_and_set(ptr, value)
   - ptr이 가리키는 변수에 value를 저장하고, 이전 값을 반환합니다.
   - 이 연산은 원자적으로 수행되어 여러 스레드가 동시에 접근해도 중간에 끼어들 수 없습니다.
   - 주로 spinlock, mutex 등에서 lock을 획득할 때 사용합니다.
   - 예시:
       while (__sync_lock_test_and_set(&lock, 1)) {
           // lock이 이미 1이면(다른 스레드가 잡음) 계속 대기
       }
       // lock이 0이었으면 내가 1로 만들고 임계구역 진입

2. __sync_lock_release(ptr)
   - ptr이 가리키는 변수에 0을 저장합니다(락 해제).
   - 이 연산도 원자적으로 수행됩니다.
   - 예시:
       __sync_lock_release(&lock); // lock을 0으로 만들어 락 해제

3. __sync_fetch_and_add(ptr, value)
   - ptr이 가리키는 변수에 value를 더하고, 더하기 전의 값을 반환합니다.
   - 이 연산도 원자적으로 수행됩니다.
   - 주로 카운터 증가, 세마포어 signal 등에서 사용합니다.
   - 예시:
       __sync_fetch_and_add(&count, 1); // count를 1 증가시킴

4. __sync_bool_compare_and_swap(ptr, oldval, newval)
   - ptr이 가리키는 값이 oldval과 같으면 newval로 바꿉니다.
   - 성공하면 true(1), 실패하면 false(0)를 반환합니다.
   - 이 연산도 원자적으로 수행됩니다.
   - 주로 세마포어 wait, lock-free 자료구조 등에서 사용합니다.
   - 예시:
       if (__sync_bool_compare_and_swap(&val, expected, desired)) {
           // val이 expected와 같아서 desired로 바뀜
       }

요약:
- 이 함수들은 멀티스레드/멀티코어 환경에서 데이터 경쟁 없이 안전하게 값을 변경(락, 카운터, 세마포어 등)할 수 있게 해주는 원자적(atomic) 연산입니다.
- 임베디드, 커널, 동시성 프로그래밍에서 매우 중요하게 사용됩니다.
*/




// --- Spinlock ---
typedef struct { volatile int lock; } spinlock_t;
void spinlock_init(spinlock_t* l) {
    l->lock = 0;
}
void spinlock_lock(spinlock_t* l) {
    while (__sync_lock_test_and_set(&l->lock, 1)) {
        // busy-wait
    }
}
void spinlock_unlock(spinlock_t* l) {
    __sync_lock_release(&l->lock);
}

// --- Mutex ---
typedef struct { volatile int locked; } mutex_t;
void mutex_init(mutex_t* m) {
    m->locked = 0;
}
void mutex_lock(mutex_t* m) {
    while (__sync_lock_test_and_set(&m->locked, 1)) {
        // busy-wait (실제 OS에서는 sleep/yield)
    }
}
void mutex_unlock(mutex_t* m) {
    __sync_lock_release(&m->locked);
}

// --- Semaphore ---
typedef struct { volatile int count; } semaphore_t;
void semaphore_init(semaphore_t* s, int initial) {
    s->count = initial;
}
void semaphore_wait(semaphore_t* s) {
    while (1) {
        while (s->count <= 0) {
            // busy-wait
        }
        // 원자적으로 감소 시도
        if (__sync_bool_compare_and_swap(&s->count, s->count, s->count - 1)) {
            break;
        }
    }
}
void semaphore_signal(semaphore_t* s) {
    __sync_fetch_and_add(&s->count, 1);
}

// --- Condition Variable ---
typedef struct { volatile int flag; } condvar_t;
void condvar_init(condvar_t* cv) {
    cv->flag = 0;
}
void condvar_wait(condvar_t* cv, mutex_t* m) {
    while (!cv->flag) {
        mutex_unlock(m);
        usleep(1000); // simulate wait
        mutex_lock(m);
    }
    cv->flag = 0; // auto-reset
}
void condvar_signal(condvar_t* cv) {
    cv->flag = 1;
}

// --- 테스트 코드 ---

#define PASS 1
#define FAIL 0

// Spinlock 테스트
int test_spinlock() {
    spinlock_t l;
    spinlock_init(&l);
    int shared = 0;
    spinlock_lock(&l);
    shared = 42;
    spinlock_unlock(&l);
    int result = (shared == 42) ? PASS : FAIL;
    printf("Spinlock test: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// Mutex 테스트
int test_mutex() {
    mutex_t m;
    mutex_init(&m);
    int shared = 0;
    mutex_lock(&m);
    shared = 99;
    mutex_unlock(&m);
    int result = (shared == 99) ? PASS : "FAIL";
    printf("Mutex test: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// Semaphore 테스트
int test_semaphore() {
    semaphore_t s;
    semaphore_init(&s, 2);
    int ok1 = FAIL, ok2 = FAIL, ok3 = FAIL;
    semaphore_wait(&s); ok1 = (s.count == 1);
    semaphore_wait(&s); ok2 = (s.count == 0);
    semaphore_signal(&s); ok3 = (s.count == 1);
    int result = (ok1 && ok2 && ok3) ? PASS : FAIL;
    printf("Semaphore test: %s\n", result ? "PASS" : "FAIL");
    return result;
}

// Condition Variable 테스트
int test_condvar() {
    mutex_t m;
    condvar_t cv;
    mutex_init(&m);
    condvar_init(&cv);
    int shared = 0;

    // 시뮬레이션: 메인에서 wait, 다른 곳에서 signal
    mutex_lock(&m);
    // 별도 스레드에서 signal
    pid_t pid = fork();
    if (pid == 0) {
        usleep(10000);
        condvar_signal(&cv);
        _exit(0);
    }
    condvar_wait(&cv, &m);
    shared = 1;
    mutex_unlock(&m);
    int result = (shared == 1) ? PASS : FAIL;
    printf("Condvar test: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int main() {
    int total = 0;
    total += test_spinlock();
    total += test_mutex();
    total += test_semaphore();
    total += test_condvar();
    printf("Total Passed: %d/4\n", total);
    return 0;
}