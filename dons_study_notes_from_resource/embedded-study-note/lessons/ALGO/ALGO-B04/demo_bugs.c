/* ALGO-B04 demo: 7. 자주 하는 실수 를 실제로 돌려 본다 (각 버그 변형 vs 정답, 고정 시드)
 *   B1 isqrt32: hi = x 그대로 + uint32 mid*mid  → 곱이 2^32 에서 감김
 *   B2 isqrt64: hi = x 그대로 + uint64 mid*mid  → 곱이 2^64 에서 감김
 *   B3 last true 인데 아래쪽 mid              → lo = mid 가 제자리 → 무한 루프 (반복 횟수 상한으로 감지)
 *   B4 판정 함수가 "패킷 > cap" 을 안 걸러냄 + lo = 0 에서 시작 → max 보다 작은 답
 *   B5 합을 uint32 로                          → 큰 패킷에서 hi 가 감겨 답이 틀림
 *   B6 hi = sum - 1 / lo = max + 1 같은 경계 실수 → D == 1, D >= n 에서 틀림
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bugs.c -o demo_bugs && ./demo_bugs
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static uint32_t g_rng = 2463534242u;
static uint32_t xs32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }
static uint64_t xs64(void) { uint64_t h = xs32(); return (h << 32) | xs32(); }

static uint64_t isqrt64(uint64_t x) {
    uint64_t lo = 0, hi = x < 0xFFFFFFFFu ? x : 0xFFFFFFFFu;
    while (lo < hi) { uint64_t mid = lo + (hi - lo + 1) / 2; if (mid * mid <= x) lo = mid; else hi = mid - 1; }
    return lo;
}
/* B1 */
static uint32_t isqrt32_wrap(uint32_t x) {
    uint32_t lo = 0, hi = x;
    while (lo < hi) { uint32_t mid = lo + (hi - lo + 1) / 2; if (mid * mid <= x) lo = mid; else hi = mid - 1; }
    return lo;
}
/* B2 */
static uint64_t isqrt64_wrap(uint64_t x) {
    uint64_t lo = 0, hi = x;
    while (lo < hi) { uint64_t mid = lo + (hi - lo + 1) / 2; if (mid * mid <= x) lo = mid; else hi = mid - 1; }
    return lo;
}
/* B3: 아래쪽 mid. 반복 상한 200 을 넘으면 "멈추지 않음" */
static bool isqrt64_lowmid(uint64_t x, uint64_t *out) {
    uint64_t lo = 0, hi = x < 0xFFFFFFFFu ? x : 0xFFFFFFFFu;
    for (int it = 0; lo < hi; it++) {
        if (it > 200) return false;
        uint64_t mid = lo + (hi - lo) / 2;
        if (mid * mid <= x) lo = mid; else hi = mid - 1;
    }
    *out = lo;
    return true;
}

/* 프레임 용량 */
static size_t frames_ok(const uint32_t *sz, size_t n, uint64_t cap) {
    size_t f = 0; uint64_t used = 0;
    for (size_t i = 0; i < n; i++) {
        if (sz[i] > cap) return SIZE_MAX;
        if (f == 0 || used + sz[i] > cap) { f++; used = 0; }
        used += sz[i];
    }
    return f;
}
static size_t frames_nocheck(const uint32_t *sz, size_t n, uint64_t cap) {   /* B4: 큰 패킷도 "한 프레임" 으로 셈 */
    size_t f = 0; uint64_t used = 0;
    for (size_t i = 0; i < n; i++) {
        if (f == 0 || used + sz[i] > cap) { f++; used = 0; }
        used += sz[i];
    }
    return f;
}
typedef size_t (*frames_fn)(const uint32_t *, size_t, uint64_t);
static uint64_t search(const uint32_t *sz, size_t n, size_t D, frames_fn fr, int variant) {
    uint64_t lo = 0, hi = 0;
    uint32_t hi32 = 0;
    for (size_t i = 0; i < n; i++) { if (sz[i] > lo) lo = sz[i]; hi += sz[i]; hi32 += sz[i]; }
    if (variant == 4) lo = 0;                          /* B4: 하한을 0 으로 */
    if (variant == 5) hi = hi32;                       /* B5: uint32 합 (감김) */
    if (variant == 6) { hi = hi - 1; lo = lo + 1; }    /* B6: 양 끝을 답 후보에서 뺌 */
    while (lo < hi) {
        uint64_t mid = lo + (hi - lo) / 2;
        if (fr(sz, n, mid) <= D) hi = mid; else lo = mid + 1;
    }
    return lo;
}

int main(void) {
    /* B1 / B2 */
    int b1 = 0, b2 = 0; uint32_t ex1 = 0; uint64_t ex2 = 0;
    for (int t = 0; t < 1000000; t++) {
        uint32_t x = xs32();
        if (isqrt32_wrap(x) != isqrt64(x)) { if (!b1) ex1 = x; b1++; }
        uint64_t y = xs64() >> (xs32() % 64);
        if (isqrt64_wrap(y) != isqrt64(y)) { if (!b2) ex2 = y; b2++; }
    }
    printf("B1 isqrt32 hi=x, uint32 mid*mid: wrong %d / 1000000 random x  (e.g. x=%u -> %u, correct %llu)\n",
           b1, ex1, isqrt32_wrap(ex1), (unsigned long long)isqrt64(ex1));
    printf("B2 isqrt64 hi=x, uint64 mid*mid: wrong %d / 1000000 random x  (e.g. x=%llu -> %llu, correct %llu)\n",
           b2, (unsigned long long)ex2, (unsigned long long)isqrt64_wrap(ex2), (unsigned long long)isqrt64(ex2));
    uint32_t small_bad = 0;                            /* B1 은 작은 x 에서는 멀쩡하다: 처음 틀리는 x */
    for (uint32_t x = 0; x < 200000u; x++) if (isqrt32_wrap(x) != isqrt64(x)) { small_bad = x; break; }
    printf("   B1 first wrong x (scan from 0): %u\n", small_bad);

    /* B3 */
    int hang = 0; uint64_t first_hang = UINT64_MAX;
    for (uint64_t x = 0; x < 1000; x++) { uint64_t r; if (!isqrt64_lowmid(x, &r)) { hang++; if (first_hang == UINT64_MAX) first_hang = x; } }
    printf("B3 last-true with lower mid: %d of x in [0,1000) never terminate (first x = %llu)\n", hang, (unsigned long long)first_hang);

    /* B4 / B5 / B6 */
    int b4 = 0, b5 = 0, b6 = 0, cases = 20000;
    for (int t = 0; t < cases; t++) {
        uint32_t a[24]; size_t n = 1 + xs32() % 24, D = 1 + xs32() % (n + 1);
        for (size_t i = 0; i < n; i++) a[i] = (t % 4 == 0) ? xs32() : xs32() % 100;
        uint64_t good = search(a, n, D, frames_ok, 0);
        if (search(a, n, D, frames_nocheck, 4) != good) b4++;
        if (search(a, n, D, frames_ok, 5) != good) b5++;
        if (search(a, n, D, frames_ok, 6) != good) b6++;
    }
    printf("B4 no oversize check + lo=0: wrong %d / %d\n", b4, cases);
    printf("B5 uint32 sum for hi:        wrong %d / %d\n", b5, cases);
    printf("B6 hi=sum-1, lo=max+1:       wrong %d / %d\n", b6, cases);
    const uint32_t w[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    const uint32_t v[] = {10, 1, 1};
    printf("   example {10,1,1}, D=3: correct %llu, B4 %llu;  [1..10] D=1: correct %llu, B6 %llu;  D=10: correct %llu, B6 %llu\n",
           (unsigned long long)search(v, 3, 3, frames_ok, 0), (unsigned long long)search(v, 3, 3, frames_nocheck, 4),
           (unsigned long long)search(w, 10, 1, frames_ok, 0), (unsigned long long)search(w, 10, 1, frames_ok, 6),
           (unsigned long long)search(w, 10, 10, frames_ok, 0), (unsigned long long)search(w, 10, 10, frames_ok, 6));
    return 0;
}
