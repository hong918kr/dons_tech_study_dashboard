/* C-A03 demo_relaxed.c — "relaxed 로도 돌아가던데요?" 를 TSan 과 스트레스로 반박한다.
 *
 * Part B 의 1칸 채널과 Part A 의 스핀락을 memory_order 만 바꿔 빌드한다 (컴파일 타임 상수).
 *   (기본)                          올바른 release/acquire          → TSan 보고 없음
 *   -DPUB=memory_order_relaxed      게시(FULL 저장)를 relaxed        → 생산자 쓰기 vs 소비자 읽기 레이스
 *   -DRET=memory_order_relaxed      반납(EMPTY 저장)을 relaxed       → 소비자 읽기 vs 생산자 다음 쓰기 레이스
 *   -DLOCK_MO=memory_order_relaxed  test_and_set/clear 를 relaxed  → 락이 보호하던 일반 카운터 레이스
 *   -DVOL (LOCK_MO 와 함께)          카운터를 volatile 로: 컴파일러 이동을 막고 CPU 재배치만 남긴다
 * TSan : cc -std=c11 -g -O1 -fsanitize=thread -pthread [-D...] demo_relaxed.c -o dr && ./dr
 * 실기 : cc -std=c11 -O2 -pthread -DN=2000000u [-D...] demo_relaxed.c -o dr_n && ./dr_n
 * (relaxed 버전은 C11 상 데이터 레이스 = UB. 관찰용으로만 돌린다.)
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

#ifndef PUB
#define PUB memory_order_release
#endif
#ifndef RET
#define RET memory_order_release
#endif
#ifndef LOCK_MO                        /* 지정하면 acquire/release 둘 다 이 값 */
#define LOCK_ACQ memory_order_acquire
#define LOCK_REL memory_order_release
#else
#define LOCK_ACQ LOCK_MO
#define LOCK_REL LOCK_MO
#endif
#ifndef N
#define N 2000u
#endif

typedef struct { uint32_t seq, data[7]; } msg_t;
static atomic_uint full;              /* 0 EMPTY, 1 FULL */
static msg_t slot;                    /* 일반 데이터 */

static void *producer(void *arg) {
    (void)arg;
    for (uint32_t s = 0; s < N; s++) {
        while (atomic_load_explicit(&full, memory_order_acquire) != 0u) { }
        slot.seq = s;                                   /* 쓰기 */
        for (int i = 0; i < 7; i++) slot.data[i] = s + (uint32_t)i;
        atomic_store_explicit(&full, 1u, PUB);          /* 게시 */
    }
    return NULL;
}
static uint32_t consume_all(void) {
    uint32_t bad = 0;
    for (uint32_t s = 0; s < N; s++) {
        while (atomic_load_explicit(&full, memory_order_acquire) != 1u) { }
        msg_t m = slot;                                 /* 읽기 */
        if (m.seq != s || m.data[6] != s + 6u) bad++;
        atomic_store_explicit(&full, 0u, RET);          /* 반납 */
    }
    return bad;
}

static atomic_flag lk = ATOMIC_FLAG_INIT;
#ifdef VOL                                              /* -DVOL: 컴파일러가 루프 밖으로 못 빼게 */
static volatile unsigned long counter;
#else
static unsigned long counter;                           /* 락으로 보호한다고 "믿는" 일반 변수 */
#endif
#define LOCK_ITERS (N * 50u)
static void *locker(void *arg) {
    (void)arg;
    for (uint32_t i = 0; i < LOCK_ITERS; i++) {
        while (atomic_flag_test_and_set_explicit(&lk, LOCK_ACQ)) { }
        counter++;
        atomic_flag_clear_explicit(&lk, LOCK_REL);
    }
    return NULL;
}

int main(void) {
    pthread_t a, b;
    pthread_create(&a, NULL, producer, NULL);
    uint32_t bad = consume_all();
    pthread_join(a, NULL);
    printf("chan: messages=%u bad=%u\n", (unsigned)N, bad);
    pthread_create(&a, NULL, locker, NULL);
    pthread_create(&b, NULL, locker, NULL);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    printf("lock: counter=%lu (expect %lu)\n", counter, 2ul * LOCK_ITERS);
    return 0;
}
