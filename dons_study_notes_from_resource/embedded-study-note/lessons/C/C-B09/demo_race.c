/* C-B09 demo_race.c — volatile 카운터는 원자적이지 않다 (두 스레드 테스트)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_race.c -o demo_race && ./demo_race
 * 주의: volatile 버전은 일부러 "데이터 경쟁(data race)"을 만든다 → C11 기준 UB.
 *       관찰용 실험이지 이렇게 쓰라는 코드가 아니다. 결과는 실행마다 다르다. */
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

#define N 1000000

static volatile unsigned long v_count;   /* volatile 만 붙인 카운터 */
static _Atomic unsigned long  a_count;   /* C11 원자 카운터          */

static void *inc_volatile(void *arg) {
    (void)arg;
    for (int i = 0; i < N; i++)
        v_count = v_count + 1;           /* LDR → ADD → STR 3단계 (v_count++ 와 같음) */
    return NULL;
}

static void *inc_atomic(void *arg) {
    (void)arg;
    for (int i = 0; i < N; i++)
        atomic_fetch_add_explicit(&a_count, 1, memory_order_relaxed);  /* 한 번에 읽고-더하고-쓰기 */
    return NULL;
}

static unsigned long run(void *(*fn)(void *)) {
    pthread_t t1, t2;
    pthread_create(&t1, NULL, fn, NULL);
    pthread_create(&t2, NULL, fn, NULL);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    return fn == inc_volatile ? v_count : atomic_load(&a_count);
}

int main(void) {
    unsigned long v = run(inc_volatile);
    unsigned long a = run(inc_atomic);
    printf("expected          : %d\n", 2 * N);
    printf("volatile counter  : %lu  (lost %lu)\n", v, 2UL * N - v);
    printf("_Atomic  counter  : %lu  (lost %lu)\n", a, 2UL * N - a);
    return 0;
}
