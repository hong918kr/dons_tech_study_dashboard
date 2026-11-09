/* DS-A04 demo_aba_stress.c — 강제 스케줄 없이, 진짜 8 스레드 경합에서 ABA 가 나오나? (관찰용, 체크 아님)
 *
 * solution.c 의 stress_worker 와 같은 루프: pop a, pop b → 소유 표시 → value++ → 해제 → 순서 바꿔 push.
 * 라운드마다 "동시 소유(dup)", "sum(value) != pop 수", 무결성(걸은 노드 수, 사이클) 을 센다.
 *   MODE=0  인덱스만 (naive, ABA 취약)
 *   MODE=1  64비트 (태그, 인덱스)
 *   MODE=2  64비트 태그 + push 의 CAS 를 relaxed 로 (ABA 는 막지만 메모리 순서 버그) → TSan 으로 볼 것
 * build: cc -std=c11 -O2 -pthread -DMODE=0 [-DPOOL=16] [-DROUNDS=10] [-DITERS=200000] demo_aba_stress.c -o das && ./das
 *        cc -std=c11 -g -O1 -fsanitize=thread -pthread -DMODE=0|2 -DROUNDS=1 -DITERS=20000 demo_aba_stress.c -o das_t && ./das_t
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifndef MODE
#define MODE 0
#endif
#ifndef POOL
#define POOL 64u
#endif
#ifndef ROUNDS
#define ROUNDS 10
#endif
#ifndef ITERS
#define ITERS 200000u
#endif
#define NTH 8
#define NIL 0xFFFFFFFFu
#if MODE == 2
#define PUSH_OK memory_order_relaxed
#else
#define PUSH_OK memory_order_release
#endif

typedef struct { _Atomic uint32_t next; uint32_t value; } node_t;
static node_t g_pool[POOL];
static atomic_uint g_own[POOL];
#if MODE == 0
static _Atomic uint32_t g_top;
static void push(uint32_t i) {
    uint32_t old = atomic_load_explicit(&g_top, memory_order_relaxed);
    do atomic_store_explicit(&g_pool[i].next, old, memory_order_relaxed);
    while (!atomic_compare_exchange_weak_explicit(&g_top, &old, i, PUSH_OK, memory_order_relaxed));
}
static uint32_t pop(void) {
    uint32_t old = atomic_load_explicit(&g_top, memory_order_acquire);
    while (old != NIL) {
        uint32_t next = atomic_load_explicit(&g_pool[old].next, memory_order_relaxed);
        if (atomic_compare_exchange_weak_explicit(&g_top, &old, next, memory_order_acquire, memory_order_acquire)) return old;
    }
    return NIL;
}
static uint32_t top_idx(void) { return atomic_load(&g_top); }
static void reset(void) { atomic_store(&g_top, NIL); }
#else
static _Atomic uint64_t g_top;
static void push(uint32_t i) {
    uint64_t old = atomic_load_explicit(&g_top, memory_order_relaxed), nw;
    do {
        atomic_store_explicit(&g_pool[i].next, (uint32_t)old, memory_order_relaxed);
        nw = ((uint64_t)((uint32_t)(old >> 32) + 1u) << 32) | i;
    } while (!atomic_compare_exchange_weak_explicit(&g_top, &old, nw, PUSH_OK, memory_order_relaxed));
}
static uint32_t pop(void) {
    uint64_t old = atomic_load_explicit(&g_top, memory_order_acquire);
    while ((uint32_t)old != NIL) {
        uint32_t next = atomic_load_explicit(&g_pool[(uint32_t)old].next, memory_order_relaxed);
        uint64_t nw = ((uint64_t)((uint32_t)(old >> 32) + 1u) << 32) | next;
        if (atomic_compare_exchange_weak_explicit(&g_top, &old, nw, memory_order_acquire, memory_order_acquire)) return (uint32_t)old;
    }
    return NIL;
}
static uint32_t top_idx(void) { return (uint32_t)atomic_load(&g_top); }
static void reset(void) { atomic_store(&g_top, (uint64_t)NIL); }
#endif

typedef struct { unsigned long long pops; unsigned dup, empty; } res_t;
static void *worker(void *p) {
    res_t *r = p;
    for (unsigned k = 0; k < ITERS; k++) {
        uint32_t h[2] = { pop(), pop() };
        for (int j = 0; j < 2; j++) {
            if (h[j] == NIL) { r->empty++; continue; }
            if (atomic_exchange_explicit(&g_own[h[j]], 1u, memory_order_relaxed)) r->dup++;
            g_pool[h[j]].value++;
            r->pops++;
            atomic_store_explicit(&g_own[h[j]], 0u, memory_order_relaxed);
        }
        if (k & 1u) { uint32_t t = h[0]; h[0] = h[1]; h[1] = t; }
        for (int j = 0; j < 2; j++) if (h[j] != NIL) push(h[j]);
    }
    return NULL;
}
int main(void) {
    static const char *nm[] = { "naive idx", "tag64", "tag64+relaxed push" };
    unsigned bad_rounds = 0;
    printf("MODE=%d (%s), POOL=%u, %d threads x %u iters, %d rounds\n", MODE, nm[MODE], POOL, NTH, ITERS, ROUNDS);
    for (int round = 0; round < ROUNDS; round++) {
        reset();
        for (uint32_t i = 0; i < POOL; i++) { g_pool[i].value = 0; push(i); }
        pthread_t t[NTH];
        res_t r[NTH] = { { 0, 0, 0 } };
        for (int i = 0; i < NTH; i++) pthread_create(&t[i], NULL, worker, &r[i]);
        unsigned long long pops = 0, sum = 0;
        unsigned dup = 0, empty = 0, walked = 0;
        bool cycle = false;
        unsigned char seen[POOL] = { 0 };
        for (int i = 0; i < NTH; i++) { pthread_join(t[i], NULL); pops += r[i].pops; dup += r[i].dup; empty += r[i].empty; }
        for (uint32_t i = 0; i < POOL; i++) sum += g_pool[i].value;
        for (uint32_t i = top_idx(); i != NIL; i = atomic_load(&g_pool[i].next)) {
            if (seen[i]) { cycle = true; break; }
            seen[i] = 1;
            walked++;
        }
        bool bad = dup || empty || sum != pops || walked != POOL || cycle;
        bad_rounds += bad;
        if (bad || round < 3)
            printf("round %2d: pops %llu sum %llu dup %u empty %u | stack walked %u/%u cycle %s %s\n", round, pops,
                   sum, dup, empty, walked, POOL, cycle ? "YES" : "no", bad ? "<-- CORRUPT" : "");
    }
    printf("corrupted rounds: %u / %d\n", bad_rounds, ROUNDS);
    return 0;
}
