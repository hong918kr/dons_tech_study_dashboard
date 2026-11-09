/* OS-A05 demo_sb — take 의 seq_cst 펜스를 빼면 이 맥(Apple M6)에서 실제로 중복이 나나?
 *
 * 리트머스 한 판 (Part B 의 S3 와 같은 모양, 이번엔 진짜 하드웨어 경쟁):
 *   주인: deque 에 A, B 를 넣고 go → (무작위로 조금 쉬고) take
 *   도둑: go 를 보면 steal 두 번 (첫 번째 = A, 두 번째가 주인의 take 와 경쟁)
 *   중복 = 주인도 B, 도둑의 두 번째 steal 도 B.
 *   이건 SB(store buffering) 모양이다: 주인 [W bottom ; R top], 도둑 [W top(CAS) ; R bottom]. 둘 다 옛 값을 읽으면 중복.
 *
 * take 변형 3 개:
 *   fence  : store bottom(relaxed) ; atomic_thread_fence(seq_cst) ; load top(relaxed)    — 논문 형태
 *   none   : store bottom(relaxed) ; load top(relaxed)                                    — 펜스 없음 (고장)
 *   scops  : store bottom(seq_cst) ; load top(seq_cst)                                    — 펜스 대신 seq_cst 연산
 * 도둑의 steal 은 항상 올바른 형태 (seq_cst 펜스 포함).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_sb.c -o demo_sb && ./demo_sb [rounds] [delay_mask]
 *        (asm 확인: cc -std=c11 -O2 -S demo_sb.c -o - | grep -A30 '_take_none:')
 */
#define _DARWIN_C_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    _Alignas(128) _Atomic(int64_t) top;
    _Alignas(128) _Atomic(int64_t) bottom;
    _Alignas(128) _Atomic(intptr_t) buf[4];
} dq_t;
static dq_t q;
#define A 0xA
#define B 0xB
#define EMPTY 0

__attribute__((noinline)) intptr_t take_fence(dq_t *d) {
    int64_t b = atomic_load_explicit(&d->bottom, memory_order_relaxed) - 1;
    atomic_store_explicit(&d->bottom, b, memory_order_relaxed);
    atomic_thread_fence(memory_order_seq_cst);
    int64_t t = atomic_load_explicit(&d->top, memory_order_relaxed);
    intptr_t x = EMPTY;
    if (t <= b) {
        x = atomic_load_explicit(&d->buf[b & 3], memory_order_relaxed);
        if (t == b) {
            if (!atomic_compare_exchange_strong_explicit(&d->top, &t, t + 1, memory_order_seq_cst, memory_order_relaxed)) x = EMPTY;
            atomic_store_explicit(&d->bottom, b + 1, memory_order_relaxed);
        }
    } else atomic_store_explicit(&d->bottom, b + 1, memory_order_relaxed);
    return x;
}
__attribute__((noinline)) intptr_t take_none(dq_t *d) {
    int64_t b = atomic_load_explicit(&d->bottom, memory_order_relaxed) - 1;
    atomic_store_explicit(&d->bottom, b, memory_order_relaxed);
    int64_t t = atomic_load_explicit(&d->top, memory_order_relaxed);          /* 펜스 없음 */
    intptr_t x = EMPTY;
    if (t <= b) {
        x = atomic_load_explicit(&d->buf[b & 3], memory_order_relaxed);
        if (t == b) {
            if (!atomic_compare_exchange_strong_explicit(&d->top, &t, t + 1, memory_order_seq_cst, memory_order_relaxed)) x = EMPTY;
            atomic_store_explicit(&d->bottom, b + 1, memory_order_relaxed);
        }
    } else atomic_store_explicit(&d->bottom, b + 1, memory_order_relaxed);
    return x;
}
__attribute__((noinline)) intptr_t take_scops(dq_t *d) {
    int64_t b = atomic_load_explicit(&d->bottom, memory_order_relaxed) - 1;
    atomic_store_explicit(&d->bottom, b, memory_order_seq_cst);
    int64_t t = atomic_load_explicit(&d->top, memory_order_seq_cst);
    intptr_t x = EMPTY;
    if (t <= b) {
        x = atomic_load_explicit(&d->buf[b & 3], memory_order_relaxed);
        if (t == b) {
            if (!atomic_compare_exchange_strong_explicit(&d->top, &t, t + 1, memory_order_seq_cst, memory_order_relaxed)) x = EMPTY;
            atomic_store_explicit(&d->bottom, b + 1, memory_order_relaxed);
        }
    } else atomic_store_explicit(&d->bottom, b + 1, memory_order_relaxed);
    return x;
}
__attribute__((noinline)) intptr_t steal(dq_t *d) {
    int64_t t = atomic_load_explicit(&d->top, memory_order_acquire);
    atomic_thread_fence(memory_order_seq_cst);
    int64_t b = atomic_load_explicit(&d->bottom, memory_order_acquire);
    if (t >= b) return EMPTY;
    intptr_t x = atomic_load_explicit(&d->buf[t & 3], memory_order_relaxed);
    if (!atomic_compare_exchange_strong_explicit(&d->top, &t, t + 1, memory_order_seq_cst, memory_order_relaxed)) return -1;
    return x;
}

static _Alignas(128) atomic_long go;
static _Alignas(128) atomic_long done;
static intptr_t thief_r1, thief_r2;
static long rounds;

static void *thief(void *arg) {
    (void)arg;
    for (long r = 1; r <= rounds; r++) {
        while (atomic_load_explicit(&go, memory_order_acquire) != r) { }
        thief_r1 = steal(&q);
        thief_r2 = steal(&q);
        atomic_store_explicit(&done, r, memory_order_release);
    }
    return NULL;
}

static uint32_t xs(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

int main(int argc, char **argv) {
    rounds = argc > 1 ? atol(argv[1]) : 2000000;
    uint32_t mask = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 0) : 1023u;   /* 주인 쪽 지연 범위 (빈 루프 횟수) */
    struct { const char *name; intptr_t (*take)(dq_t *); } v[] = {
        { "fence", take_fence }, { "none", take_none }, { "scops", take_scops } };
    double la[3]; getloadavg(la, 3);
    printf("rounds per variant %ld, owner delay 0..%u loop iterations, load average %.2f %.2f %.2f\n", rounds, mask, la[0], la[1], la[2]);
    for (int k = 0; k < 3; k++) {
        atomic_store(&go, 0); atomic_store(&done, 0);
        pthread_t th;
        pthread_create(&th, NULL, thief, NULL);
        long dup = 0, owner_b = 0, thief_b = 0, aborts = 0;
        uint32_t seed = 1u + (uint32_t)k;
        for (long r = 1; r <= rounds; r++) {
            atomic_store_explicit(&q.top, 0, memory_order_relaxed);
            atomic_store_explicit(&q.bottom, 0, memory_order_relaxed);
            atomic_store_explicit(&q.buf[0], A, memory_order_relaxed);
            atomic_store_explicit(&q.buf[1], B, memory_order_relaxed);
            atomic_store_explicit(&q.bottom, 2, memory_order_relaxed);
            atomic_store_explicit(&go, r, memory_order_release);              /* 위 초기화가 도둑에게 보인다 */
            for (uint32_t d = xs(&seed) & mask; d; d--) __asm__ volatile("" ::: "memory");   /* 창을 훑는다 */
            intptr_t o = v[k].take(&q);
            while (atomic_load_explicit(&done, memory_order_acquire) != r) { }
            owner_b += o == B;
            thief_b += thief_r2 == B;
            aborts += thief_r1 == -1 || thief_r2 == -1;
            dup += o == B && thief_r2 == B;
        }
        pthread_join(th, NULL);
        printf("%-6s duplicates(B twice) %8ld   owner got B %8ld   thief 2nd steal got B %8ld   aborts %ld\n",
               v[k].name, dup, owner_b, thief_b, aborts);
    }
    return 0;
}
