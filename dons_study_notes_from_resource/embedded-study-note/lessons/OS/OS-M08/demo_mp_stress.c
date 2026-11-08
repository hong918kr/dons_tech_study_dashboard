/* OS-M08 demo_mp_stress.c — 메시지 패싱(MP)을 relaxed 플래그로 하면 실제로 깨지나? (Apple Silicon 실측)
 *
 * 모양: 메일박스 칸 NSLOT 개짜리 배열. 라운드 k 마다 writer 는 칸 i 마다
 *         payload 4필드(seq, a, b, c) 쓰기 → flag[i] = 라운드 값 (PUB)
 *       reader(main) 는 칸 i 마다 flag[i] == 라운드 값이 될 때까지 기다리고(ACQ) payload 를 읽어 분류한다.
 *         ok    : 4필드 모두 이번 라운드 값
 *         stale : 4필드가 서로 같지만 지난 라운드 값 (통째로 옛것)
 *         torn  : 필드끼리 다름 (일부만 새것)
 *       칸은 라운드마다 재사용되지만, writer 는 reader 가 라운드를 다 읽은 뒤에만 다음 라운드를 시작한다
 *       (go 카운터 release/acquire) → stale/torn 은 "flag 는 새것인데 payload 는 옛것" 인 진짜 MP 재배치다.
 *
 * 빌드 옵션
 *   -DPUB=memory_order_relaxed|memory_order_release   flag 저장 순서   (기본 relaxed)
 *   -DACQ=memory_order_relaxed|memory_order_acquire   flag 읽기 순서   (기본 relaxed)
 *   -DLAYOUT=0  payload 4필드가 각각 다른 캐시 라인, flag 는 또 다른 라인 (기본)
 *   -DLAYOUT=1  payload 4필드가 한 캐시 라인(16 B 구조체), flag 는 다른 라인  ← 보통의 메일박스 모양
 *   -DLAYOUT=2  payload 와 flag 가 같은 캐시 라인
 *   -DPLAIN     payload 를 일반(비원자) 필드로 — C11 데이터 레이스(UB), TSan 이 잡는 모양
 *               (기본은 payload 도 relaxed atomic: UB 는 아니지만 순서 보장이 없어 여전히 버그)
 *
 *   cc -std=c11 -O2 -pthread [-D...] demo_mp_stress.c -o mp && ./mp
 *   cc -std=c11 -g -O1 -fsanitize=thread -pthread -DPLAIN -DROUNDS=20 demo_mp_stress.c -o mp_t && ./mp_t
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef PUB
#define PUB memory_order_relaxed
#endif
#ifndef ACQ
#define ACQ memory_order_relaxed
#endif
#ifndef LAYOUT
#define LAYOUT 0
#endif
#ifndef NSLOT
#define NSLOT 4096u
#endif
#ifndef ROUNDS
#define ROUNDS 2000u
#endif

#ifdef PLAIN
typedef uint32_t field_t;
#define WR(x, v) ((x) = (v))
#define RD(x) (x)
#define FINIT(x) ((x) = 0u)
#else
typedef atomic_uint field_t;
#define WR(x, v) atomic_store_explicit(&(x), (v), memory_order_relaxed)
#define RD(x) atomic_load_explicit(&(x), memory_order_relaxed)
#define FINIT(x) atomic_init(&(x), 0u)
#endif

#if LAYOUT == 0
typedef struct { _Alignas(64) field_t seq; _Alignas(64) field_t a;      /* 필드마다 다른 64 B 라인 */
                 _Alignas(64) field_t b;   _Alignas(64) field_t c; } payload_t;
typedef struct { payload_t pl; _Alignas(64) atomic_uint flag; } slot_t;
#elif LAYOUT == 1
typedef struct { field_t seq, a, b, c; } payload_t;                      /* 16 B, 한 라인 */
typedef struct { _Alignas(64) payload_t pl; _Alignas(64) atomic_uint flag; } slot_t;
#else
typedef struct { field_t seq, a, b, c; } payload_t;                      /* payload + flag 같은 라인 */
typedef struct { _Alignas(64) payload_t pl; atomic_uint flag; } slot_t;
#endif
_Static_assert(sizeof(slot_t) % 64 == 0, "slot = whole cache lines");

static slot_t *S;
static atomic_uint go;            /* main → writer: "라운드 k 시작해도 됨" */
static atomic_uint wdone;         /* writer → main: "라운드 k 다 썼음" */

static void *writer(void *arg) {
    (void)arg;
    for (unsigned k = 1; k <= ROUNDS; k++) {
        while (atomic_load_explicit(&go, memory_order_acquire) != k) { }
        for (unsigned i = 0; i < NSLOT; i++) {
            unsigned v = (k - 1u) * NSLOT + i + 1u;
            WR(S[i].pl.seq, v); WR(S[i].pl.a, v); WR(S[i].pl.b, v); WR(S[i].pl.c, v);   /* 1) payload */
            atomic_store_explicit(&S[i].flag, v, PUB);                        /* 2) 게시 */
        }
        atomic_store_explicit(&wdone, k, memory_order_release);
    }
    return NULL;
}

int main(void) {
    S = aligned_alloc(64, sizeof(slot_t) * NSLOT);
    if (!S) return 1;
    for (unsigned i = 0; i < NSLOT; i++) {
        FINIT(S[i].pl.seq); FINIT(S[i].pl.a); FINIT(S[i].pl.b); FINIT(S[i].pl.c);
        atomic_init(&S[i].flag, 0u);
    }
    long ok = 0, stale = 0, torn = 0;
    pthread_t t;
    pthread_create(&t, NULL, writer, NULL);
    for (unsigned k = 1; k <= ROUNDS; k++) {
        atomic_store_explicit(&go, k, memory_order_release);
        for (unsigned i = 0; i < NSLOT; i++) {
            unsigned v = (k - 1u) * NSLOT + i + 1u;
            while (atomic_load_explicit(&S[i].flag, ACQ) != v) { }           /* 1) 플래그 확인 */
            unsigned s = RD(S[i].pl.seq), a = RD(S[i].pl.a), b = RD(S[i].pl.b), c = RD(S[i].pl.c);  /* 2) 읽기 */
            if (s == v && a == v && b == v && c == v) ok++;
            else if (s == a && a == b && b == c) stale++;
            else torn++;
        }
        while (atomic_load_explicit(&wdone, memory_order_acquire) != k) { }
    }
    pthread_join(t, NULL);
    printf("LAYOUT=%d %s slot=%zuB rounds=%ld ok=%ld stale=%ld torn=%ld  (bad %.4f%%)\n", LAYOUT,
#ifdef PLAIN
           "plain",
#else
           "relaxed-payload",
#endif
           sizeof(slot_t), (long)NSLOT * ROUNDS, ok, stale, torn, 100.0 * (double)(stale + torn) / ((double)NSLOT * ROUNDS));
    free(S);
    return 0;
}
