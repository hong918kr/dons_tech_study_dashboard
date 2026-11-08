/* C-M06 demo — 직접 호출(인라인) vs 직접 호출(인라인 금지) vs ops 표 간접 호출: 시간 측정
 *
 *   cc -std=c11 -O2 demo_bench.c -o /tmp/bench && /tmp/bench
 *
 * 숫자는 머신·클럭·부하에 따라 바뀐다. 테스트(CHECK)가 아니라 관찰용이다.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define N    4096
#define REPS 50000                                      /* 총 호출 수 = N * REPS ≈ 2억 */

struct ops { uint32_t (*xform)(uint32_t); };

static inline uint32_t twice_inline(uint32_t x) { return x * 2u + 1u; }
__attribute__((noinline)) static uint32_t twice_noinline(uint32_t x) { return x * 2u + 1u; }
static const struct ops k_ops = { .xform = twice_noinline };

static uint32_t g_a[N];

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

__attribute__((noinline)) static uint32_t run_inline(void) {
    uint32_t s = 0;
    for (int r = 0; r < REPS; r++)
        for (int i = 0; i < N; i++) s += twice_inline(g_a[i]);
    return s;
}
__attribute__((noinline)) static uint32_t run_noinline(void) {
    uint32_t s = 0;
    for (int r = 0; r < REPS; r++)
        for (int i = 0; i < N; i++) s += twice_noinline(g_a[i]);
    return s;
}
__attribute__((noinline)) static uint32_t run_indirect(const struct ops *o) {
    uint32_t s = 0;
    for (int r = 0; r < REPS; r++)
        for (int i = 0; i < N; i++) s += o->xform(g_a[i]);
    return s;
}

int main(void) {
    for (int i = 0; i < N; i++) g_a[i] = (uint32_t)i * 2654435761u;
    const struct ops *volatile op_ptr = &k_ops;         /* 컴파일러가 표 내용을 못 보게 (탈가상화 방지) */
    const double calls = (double)N * REPS;

    for (int round = 0; round < 3; round++) {
        double t0 = now_s(); uint32_t a = run_inline();
        double t1 = now_s(); uint32_t b = run_noinline();
        double t2 = now_s(); uint32_t c = run_indirect(op_ptr);
        double t3 = now_s();
        printf("round %d: inline %.3f ns/elem | direct call %.3f ns/call | indirect (ops) %.3f ns/call  [%s]\n",
               round, (t1 - t0) / calls * 1e9, (t2 - t1) / calls * 1e9, (t3 - t2) / calls * 1e9,
               (a == b && b == c) ? "sums equal" : "MISMATCH");
    }
    return 0;
}
