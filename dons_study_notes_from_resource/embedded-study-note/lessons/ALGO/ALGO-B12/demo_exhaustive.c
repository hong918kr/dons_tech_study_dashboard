/* ALGO-B12 demo: 2^32 전 범위 대조 (solution.c 에 넣기엔 느려서 따로 둔다)
 *
 * 모든 32비트 x (4,294,967,296 개)에 대해 정답 코드의 함수를 기준(__builtin_* 또는 64비트 무식한 계산)과 대조한다.
 * 주의: -O2 에서는 clang 이 pc_swar32 와 pc_kernighan 을 둘 다 popcount 명령(arm64 CNT)으로 바꿔 버린다
 *       → "C 소스의 산술"을 검사하려면 -O0 으로도 돌린다 (함수 하나씩 고를 수 있게 argv 로 선택).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_exhaustive.c -o demo_exhaustive && ./demo_exhaustive [all|swar|kern|...]
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static unsigned pc_kernighan(uint32_t x) { unsigned c = 0; while (x) { x &= x - 1u; c++; } return c; }
static unsigned pc_swar32(uint32_t x) {
    x = x - ((x >> 1) & 0x55555555u);
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
    x = (x + (x >> 4)) & 0x0F0F0F0Fu;
    return (x * 0x01010101u) >> 24;
}
static uint32_t lowbit(uint32_t x)       { return x & (0u - x); }
static uint32_t clear_lowbit(uint32_t x) { return x & (x - 1u); }
static bool     is_pow2(uint32_t x)      { return x != 0 && (x & (x - 1u)) == 0; }
static unsigned ctz32(uint32_t x)        { return pc_swar32(lowbit(x) - 1u); }
static unsigned clz32(uint32_t x) {
    x |= x >> 1; x |= x >> 2; x |= x >> 4; x |= x >> 8; x |= x >> 16;
    return 32u - pc_swar32(x);
}
static uint32_t next_pow2(uint32_t x) {
    if (x == 0) return 1u;
    if (x > 0x80000000u) return 0u;
    x--;
    x |= x >> 1; x |= x >> 2; x |= x >> 4; x |= x >> 8; x |= x >> 16;
    return x + 1u;
}
static bool align_up32(uint32_t x, uint32_t a, uint32_t *out) {
    uint32_t m = a - 1u;
    if (x > UINT32_MAX - m) return false;
    *out = (x + m) & ~m;
    return true;
}

/* 기준 */
static unsigned ref_pc(uint32_t x)       { return (unsigned)__builtin_popcount(x); }
static uint32_t ref_lowbit(uint32_t x)   { return x ? 1u << __builtin_ctz(x) : 0u; }
static unsigned ref_ctz(uint32_t x)      { return x ? (unsigned)__builtin_ctz(x) : 32u; }
static unsigned ref_clz(uint32_t x)      { return x ? (unsigned)__builtin_clz(x) : 32u; }
static uint32_t ref_next_pow2(uint32_t x) {             /* clz 로 (기준은 builtin 을 써도 된다) */
    if (x <= 1) return 1u;
    if (x > 0x80000000u) return 0u;
    return 1u << (32 - __builtin_clz(x - 1u));           /* x ≥ 2 → x-1 ≥ 1, 시프트 양 ≤ 31 */
}

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

#define SWEEP(name, cond) do {                                                    \
    if (strcmp(which, "all") == 0 || strcmp(which, name) == 0) {                  \
        double t0 = now(); uint64_t bad = 0;                                      \
        uint32_t x = 0;                                                           \
        do { if (!(cond)) bad++; } while (++x != 0);                              \
        printf("%-8s 2^32 values  bad=%llu  %.2f s\n", name,                      \
               (unsigned long long)bad, now() - t0);                              \
        fflush(stdout); total_bad += bad;                                         \
    } } while (0)

int main(int argc, char **argv) {
    const char *which = argc > 1 ? argv[1] : "all";
    uint64_t total_bad = 0;
    SWEEP("swar",   pc_swar32(x) == ref_pc(x));
    SWEEP("kern",   pc_kernighan(x) == ref_pc(x));
    SWEEP("lowbit", lowbit(x) == ref_lowbit(x));
    SWEEP("clear",  clear_lowbit(x) == (x ^ ref_lowbit(x)));
    SWEEP("pow2",   is_pow2(x) == (ref_pc(x) == 1));
    SWEEP("ctz",    ctz32(x) == ref_ctz(x));
    SWEEP("clz",    clz32(x) == ref_clz(x));
    SWEEP("next",   next_pow2(x) == ref_next_pow2(x));
    /* align_up32: 정렬 64 하나로 전 범위 (2^32 이상으로 올라가는 x 는 정확히 63 개여야 한다) */
    if (strcmp(which, "all") == 0 || strcmp(which, "align") == 0) {
        double t0 = now(); uint64_t bad = 0, fails = 0; uint32_t x = 0, r;
        do {
            uint64_t want = ((uint64_t)x + 63u) & ~(uint64_t)63u;
            bool ok = align_up32(x, 64, &r);
            if (!ok) fails++;
            if (ok != (want <= UINT32_MAX) || (ok && r != want)) bad++;
        } while (++x != 0);
        printf("%-8s 2^32 values  bad=%llu  overflow-rejects=%llu  %.2f s\n", "align64",
               (unsigned long long)bad, (unsigned long long)fails, now() - t0);
        total_bad += bad;
    }
    printf("total bad = %llu\n", (unsigned long long)total_bad);
    return total_bad ? 1 : 0;
}
