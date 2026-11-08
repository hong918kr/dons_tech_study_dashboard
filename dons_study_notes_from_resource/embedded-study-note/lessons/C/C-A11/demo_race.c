/* C-A11 demo — 스니펫 3 의 "읽고 → 0 쓰기" 를 진짜 동시 실행으로 (pthread = ISR 역할 스레드).
 *
 *   cc -std=c11 -O2 -pthread demo_race.c -o demo_race && ./demo_race                         # 유실 수 세기
 *   cc -std=c11 -g -O1 -fsanitize=thread -pthread -DN=2000L demo_race.c -o demo_race_tsan && ./demo_race_tsan
 *   cc -std=c11 -O2 -pthread -DFIXED demo_race.c -o demo_race_fixed && ./demo_race_fixed       # 원자 연산 판
 *   cc -std=c11 -g -O1 -fsanitize=thread -pthread -DFIXED -DN=2000L demo_race.c -o drf_tsan && ./drf_tsan
 *
 * "ISR" 스레드는 이벤트 비트가 내려가 있을 때만 다시 세운다 → 매 set 이 서로 다른 사건이다.
 * main 은 take() 로 가져가며 센다. 읽기와 0 쓰기 사이에 set 이 끼면 그 사건은 사라진다.
 *
 * 주의: 단일 코어 MCU 에서는 ISR 가 main 을 "선점" 하고, 여기서는 두 스레드가 "동시에" 돈다.
 * 버그의 모양(읽기~쓰기 사이에 끼어든 갱신이 사라짐)은 같지만 고치는 도구는 다르다
 * (MCU: 임계 구역 또는 LDREX/STREX, 멀티스레드: 원자 연산·뮤텍스). 유실 수는 실행마다 다르다.
 */
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

#define EV_ADC_DONE 1u
#ifndef N
#define N 200000L                                            /* TSan 빌드는 -DN=2000L (느리다) */
#endif

#ifndef FIXED
static volatile uint32_t g_events;                           /* 스니펫 3 원본 */
static void     isr_set(void) { g_events |= EV_ADC_DONE; }
static uint32_t peek(void)    { return g_events; }
static uint32_t take(void)    { uint32_t ev = g_events; g_events = 0u; return ev; }
#else
static _Atomic uint32_t g_events;                            /* 원자 연산 판 (연습 2) */
static void     isr_set(void) { atomic_fetch_or_explicit(&g_events, EV_ADC_DONE, memory_order_release); }
static uint32_t peek(void)    { return atomic_load_explicit(&g_events, memory_order_acquire); }
static uint32_t take(void)    { return atomic_exchange_explicit(&g_events, 0u, memory_order_acquire); }
#endif

static atomic_int g_isr_done;

static void *isr_thread(void *arg) {
    (void)arg;
    for (long i = 0; i < N; i++) {
        while (peek() & EV_ADC_DONE) { }                     /* 이전 사건이 가져가질 때까지 */
        isr_set();
    }
    atomic_store(&g_isr_done, 1);
    return NULL;
}

int main(void) {
    pthread_t th;
    long seen = 0;
    pthread_create(&th, NULL, isr_thread, NULL);
    for (;;) {
        int done = atomic_load(&g_isr_done);                 /* 먼저 읽고 → 마지막 take 까지 */
        if (take() & EV_ADC_DONE) seen++;
        if (done) break;
    }
    pthread_join(th, NULL);
    printf("events sent=%ld  seen by main=%ld  lost=%ld\n", N, seen, N - seen);
    return 0;
}
