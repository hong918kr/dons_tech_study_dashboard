/* DS-A01 demo_relaxed.c — SPSC 링의 memory_order 를 relaxed 로 약하게 만들면 실제로 깨지나? (관찰용, 체크 아님)
 *
 *   PUB_ST / PUB_LD : 게시 방향  — 생산자의 tail store / 소비자의 tail load   (정답: release / acquire)
 *   RET_ST / RET_LD : 반납 방향  — 소비자의 head store / 생산자의 head load   (정답: release / acquire)
 *
 * 메시지 = {seq, payload = mix(seq)}. 소비자는 seq 가 0,1,2,... 인지, payload 가 맞는지 센다.
 *   stale : seq 가 기대값보다 작다 (= 지난 바퀴의 옛 내용을 읽었다, 게시 방향이 깨짐)
 *   ahead : seq 가 기대값보다 크다 (= 생산자가 소비자가 읽기 전에 칸을 덮어씀, 반납 방향이 깨짐)
 *   torn  : payload != mix(seq) (= 16 B 칸의 반은 옛 값, 반은 새 값)
 * 고의로 데이터 레이스를 만드는 프로그램이다 (C11 5.1.2.4 기준 UB). 결과는 실행마다 다르다.
 *
 * build (정답 순서):      cc -std=c11 -O2 -pthread demo_relaxed.c -o dr && ./dr
 * build (전부 relaxed):   cc -std=c11 -O2 -pthread -DALL_RELAXED demo_relaxed.c -o drr && ./drr
 * build (한 방향만):      ... -DPUB_ST=memory_order_relaxed -DPUB_LD=memory_order_relaxed   (또는 RET_*)
 * TSan:                  cc -std=c11 -g -O1 -fsanitize=thread -pthread -DALL_RELAXED -DN=100000 demo_relaxed.c
 * 인자: ./dr [runs] (기본 5)   컴파일 상수: -DN=메시지 수(기본 2e7) -DCAP=용량(기본 4)
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef ALL_RELAXED
#  define PUB_ST memory_order_relaxed
#  define PUB_LD memory_order_relaxed
#  define RET_ST memory_order_relaxed
#  define RET_LD memory_order_relaxed
#endif
#ifndef PUB_ST
#  define PUB_ST memory_order_release
#endif
#ifndef PUB_LD
#  define PUB_LD memory_order_acquire
#endif
#ifndef RET_ST
#  define RET_ST memory_order_release
#endif
#ifndef RET_LD
#  define RET_LD memory_order_acquire
#endif
#ifndef N
#  define N 20000000u
#endif
#ifndef CAP
#  define CAP 4u
#endif

typedef struct { uint64_t seq; uint64_t payload; } msg_t;
static inline uint64_t mix(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

static struct {
    _Alignas(128) atomic_uint tail;
    _Alignas(128) atomic_uint head;
    _Alignas(128) msg_t buf[CAP];
} q;

static void *producer(void *arg) {
    (void)arg;
    for (uint64_t i = 0; i < N; i++) {
        uint32_t t = atomic_load_explicit(&q.tail, memory_order_relaxed);
        while (t - atomic_load_explicit(&q.head, RET_LD) == CAP) { }
        q.buf[t & (CAP - 1u)].seq = i;
        q.buf[t & (CAP - 1u)].payload = mix(i);
        atomic_store_explicit(&q.tail, t + 1u, PUB_ST);
    }
    return NULL;
}

int main(int argc, char **argv) {
    int runs = argc > 1 ? atoi(argv[1]) : 5;
    printf("orders: tail st=%d ld=%d | head st=%d ld=%d (0=relaxed 2=acquire 3=release)  N=%u CAP=%u\n",
           (int)PUB_ST, (int)PUB_LD, (int)RET_ST, (int)RET_LD, (unsigned)N, (unsigned)CAP);
    unsigned long long tot_bad = 0;
    for (int r = 0; r < runs; r++) {
        atomic_store(&q.tail, 0u);
        atomic_store(&q.head, 0u);
        pthread_t p;
        pthread_create(&p, NULL, producer, NULL);
        unsigned long long stale = 0, ahead = 0, torn = 0;
        for (uint64_t expect = 0; expect < N; expect++) {
            uint32_t h = atomic_load_explicit(&q.head, memory_order_relaxed);
            while (atomic_load_explicit(&q.tail, PUB_LD) == h) { }
            msg_t m = q.buf[h & (CAP - 1u)];
            atomic_store_explicit(&q.head, h + 1u, RET_ST);
            if (m.seq < expect) stale++;
            else if (m.seq > expect) ahead++;
            if (m.payload != mix(m.seq)) torn++;
        }
        pthread_join(p, NULL);
        printf("run %d: stale %llu  ahead %llu  torn %llu  (of %u)\n", r, stale, ahead, torn, (unsigned)N);
        tot_bad += stale + ahead + torn;
    }
    printf("total bad: %llu\n", tot_bad);
    return 0;
}
