// C28_peterson.c — 책 ASIDE 의 Peterson 알고리즘이 "현대 하드웨어에선 안 된다"는 말을
// Apple M2 (ARM, 약한 메모리 모델)에서 직접 확인한다.
//   plain  : volatile int 로만 구현 (컴파일러 재배치는 막지만 CPU 재배치는 못 막음)
//   seqcst : 같은 코드를 C11 atomic (memory_order_seq_cst) 로 구현 → arm64 에서 stlr/ldar 로 컴파일되어 store→load 재배치 금지
// build: cc -Wall -Wextra -O0 -pthread code/C28_peterson.c -o .work/bin/C28_peterson
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

#ifndef LOOPS
#define LOOPS 2000000
#endif

// ---- plain 버전
static volatile int flag_v[2];
static volatile int turn_v;
// ---- seq_cst 버전
static volatile atomic_int flag_a[2];   // volatile: -O2 에서 clang 이 turn_a 접근을 통째로 지워 무한 대기한 것을 관찰해서 붙임
static volatile atomic_int turn_a;

static long counter;                    // 보호 대상 (일부러 평범한 변수)
static int use_atomic;

static void lock(int self) {
    int other = 1 - self;
    if (use_atomic) {
        atomic_store(&flag_a[self], 1);
        atomic_store(&turn_a, other);
        while (atomic_load(&flag_a[other]) == 1 && atomic_load(&turn_a) == other)
            ;
    } else {
        flag_v[self] = 1;               // (1) store
        turn_v = other;                 // (2) store
        while (flag_v[other] == 1 && turn_v == other)   // (3) load — (1)보다 먼저 보일 수 있다!
            ;
    }
}

static void unlock(int self) {
    if (use_atomic) atomic_store(&flag_a[self], 0);
    else flag_v[self] = 0;
}

static void *worker(void *arg) {
    int self = (int)(long)arg;
    for (int i = 0; i < LOOPS; i++) {
        lock(self);
        counter = counter + 1;
        unlock(self);
    }
    return NULL;
}

static void run(int atomic_mode) {
    pthread_t a, b;
    use_atomic = atomic_mode;
    counter = 0;
    pthread_create(&a, NULL, worker, (void *)0L);
    pthread_create(&b, NULL, worker, (void *)1L);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    printf("%-7s counter = %ld (expected %d, lost %ld)\n",
           atomic_mode ? "seqcst" : "plain", counter, 2 * LOOPS, 2L * LOOPS - counter);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    for (int r = 0; r < 3; r++) run(0);
    for (int r = 0; r < 3; r++) run(1);
    return 0;
}
