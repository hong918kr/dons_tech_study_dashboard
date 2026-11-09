/* DS-A04 demo_llsc.c — "진짜" LL/SC pop 은 ABA 에 걸리나? (arm64 전용 관찰 데모, 체크 아님)
 *
 * pop 을 손으로 쓴 LDAXR … STLXR 로 만든다: top 을 LDAXR 로 읽고(배타 모니터 설정), next 를 읽고,
 * window 에서 잠깐 멈췄다가, STLXR 로 top = next 를 시도한다. STLXR 은 "값이 같은가" 가 아니라
 * "그 사이 누가 그 granule 에 썼는가 (모니터가 지워졌나)" 로 성공/실패한다.
 *
 * window 에서 하는 일 (모드별 1000번 반복, STLXR 실패 횟수를 센다):
 *   empty  : 아무것도 안 함 (함수 호출만)                 → 기대: 성공
 *   aba    : 다른 스레드가 pop A, pop B, push A (값은 다시 A) → 기대: 실패 (값이 같아도)
 *   same   : 다른 스레드가 top 에 지금과 같은 값 A 를 store → 기대: 실패 (쓰기 자체가 모니터를 지움)
 *   sleep  : 아무도 안 쓰고 nanosleep(50us) — 문맥 전환/예외 복귀  → 관찰해 보자
 * 주의: 두 asm 문 사이에 C 코드(훅 호출)를 넣은 것은 관찰용 편법이다. 실제 LL/SC 루프는 한 asm 블록 안에
 * 다른 메모리 접근 없이 짧게 써야 한다 (Arm ARM: 그 사이 명시적 메모리 접근이 있으면 진행 보장 없음).
 *
 * build: cc -std=c11 -O2 -pthread demo_llsc.c -o dl && ./dl
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#if !defined(__aarch64__)
int main(void) { puts("arm64 only"); return 0; }
#else
#define NIL 0xFFFFFFFFu
static _Atomic uint32_t g_next[4];
static _Atomic uint32_t g_top;

static uint32_t ldaxr32(_Atomic uint32_t *p) {
    uint32_t v;
    __asm__ volatile("ldaxr %w0, [%1]" : "=r"(v) : "r"(p) : "memory");
    return v;
}
static uint32_t stlxr32(_Atomic uint32_t *p, uint32_t v) {     /* 0 = 성공, 1 = 실패 */
    uint32_t st;
    __asm__ volatile("stlxr %w0, %w2, [%1]" : "=&r"(st) : "r"(p), "r"(v) : "memory");
    return st;
}
/* main 쪽의 평범한 CAS pop/push (컴파일러 CAS) */
static uint32_t cas_pop(void) {
    uint32_t old = atomic_load_explicit(&g_top, memory_order_acquire);
    while (old != NIL && !atomic_compare_exchange_weak_explicit(&g_top, &old, atomic_load_explicit(&g_next[old], memory_order_relaxed),
                                                                memory_order_acquire, memory_order_acquire)) {}
    return old;
}
static void cas_push(uint32_t i) {
    uint32_t old = atomic_load_explicit(&g_top, memory_order_relaxed);
    do atomic_store_explicit(&g_next[i], old, memory_order_relaxed);
    while (!atomic_compare_exchange_weak_explicit(&g_top, &old, i, memory_order_release, memory_order_relaxed));
}
static void setup(void) {               /* top -> A(0) -> B(1) -> C(2) -> D(3) */
    for (uint32_t i = 0; i < 4; i++) atomic_store(&g_next[i], i + 1 < 4 ? i + 1 : NIL);
    atomic_store(&g_top, 0u);
}

enum { M_EMPTY, M_ABA, M_SAME, M_SLEEP };
static _Atomic int g_mode;
static atomic_int g_in_window, g_go;
static atomic_int g_quit;

static void __attribute__((noinline)) window(void) {
    if (g_mode == M_EMPTY) return;
    if (g_mode == M_SLEEP) { struct timespec t = { 0, 50000 }; nanosleep(&t, NULL); return; }
    atomic_store_explicit(&g_in_window, 1, memory_order_release);
    for (long k = 0; k < 200000000L && !atomic_load_explicit(&g_go, memory_order_acquire); k++) {}
}
/* 한 번 시도: STLXR 상태 반환. LDAXR 로 읽은 값이 A 였고 STLXR 할 때도 top 값은 A 다 (aba/same 모드). */
static uint32_t llsc_pop_once(uint32_t *got) {
    uint32_t old = ldaxr32(&g_top);
    uint32_t next = atomic_load_explicit(&g_next[old], memory_order_relaxed);
    window();
    *got = old;
    return stlxr32(&g_top, next);
}
static void *other(void *arg) {          /* 다른 코어: T1 이 window 에 들어오면 일을 하고 go */
    (void)arg;
    while (!atomic_load(&g_quit)) {
        if (!atomic_load_explicit(&g_in_window, memory_order_acquire)) continue;
        atomic_store(&g_in_window, 0);
        if (g_mode == M_ABA) { uint32_t a = cas_pop(); (void)cas_pop(); cas_push(a); }
        else atomic_store_explicit(&g_top, atomic_load(&g_top), memory_order_release);   /* 같은 값 다시 쓰기 */
        atomic_store_explicit(&g_go, 1, memory_order_release);
    }
    return NULL;
}
int main(void) {
    const char *names[] = { "empty", "aba", "same", "sleep" };
    pthread_t t;
    pthread_create(&t, NULL, other, NULL);
    for (int m = 0; m < 4; m++) {
        g_mode = m;
        unsigned fail = 0;
        const unsigned N = 1000;
        for (unsigned k = 0; k < N; k++) {
            setup();
            atomic_store(&g_go, 0);
            uint32_t got;
            uint32_t st = llsc_pop_once(&got);
            if (st) { fail++; __asm__ volatile("clrex" ::: "memory"); }
            (void)got;
        }
        printf("%-6s: STLXR failed %4u / %u   (top value at STLXR time was A again: %s)\n", names[m], fail, N,
               m == M_ABA || m == M_SAME ? "yes" : "n/a");
    }
    atomic_store(&g_quit, 1);
    pthread_join(t, NULL);
    return 0;
}
#endif
