/* ALGO-A07 demo — 2^32 전 범위 확인 (solution.c 에 넣기엔 길다)
 *   1) Gray: 모든 32비트 x 에 대해 dec(enc(x)) == x, enc(x) 와 enc(x+1) 은 정확히 1비트 차이 (x+1 은 2^32 에서 0 으로)
 *   2) haszero: "0 이 아님 ⇔ 0 바이트 있음", 첫 0 바이트 = ctz/8 (little-endian),
 *      바이트별 플래그가 정확한 마스크와 다른 워드 수 (유도값 3·65536 − 1 = 196607 과 비교)
 *   3) sub = (sub-1) & 0xFFFFFFFF 순회 횟수 = 2^32
 *   4) Gosper 사슬 33개(k = 0..32)가 모든 32비트 값을 정확히 한 번씩 방문, 사슬 길이 = C(32,k)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_exhaustive.c -o demo_exhaustive && ./demo_exhaustive
 */
#include <stdint.h>
#include <stdio.h>
#include <time.h>

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

static uint32_t gray_enc(uint32_t b) { return b ^ (b >> 1); }
static uint32_t gray_dec(uint32_t g) { g ^= g >> 16; g ^= g >> 8; g ^= g >> 4; g ^= g >> 2; g ^= g >> 1; return g; }
static uint32_t haszero32(uint32_t v) { return (v - 0x01010101u) & ~v & 0x80808080u; }
static uint32_t zero_exact(uint32_t v) { return ~(((v & 0x7F7F7F7Fu) + 0x7F7F7F7Fu) | v | 0x7F7F7F7Fu); }
static uint32_t next_same_popcount(uint32_t x) {
    if (x == 0) return 0;
    uint32_t c = x & (0u - x), r = x + c;
    if (r == 0) return 0;
    return (((r ^ x) >> 2) / c) | r;
}

int main(void) {
    double t0 = now();
    uint64_t bad_rt = 0, bad_adj = 0;
    uint32_t x = 0;
    do {
        if (gray_dec(gray_enc(x)) != x) bad_rt++;
        if (__builtin_popcount(gray_enc(x) ^ gray_enc(x + 1u)) != 1) bad_adj++;
    } while (++x != 0);
    printf("gray   : 2^32 values, roundtrip bad=%llu, adjacent-not-1-bit=%llu  (%.2f s)\n",
           (unsigned long long)bad_rt, (unsigned long long)bad_adj, now() - t0);

    t0 = now();
    uint64_t bad_iff = 0, bad_first = 0, flag_diff = 0;
    x = 0;
    do {
        uint32_t m = 0;                                     /* 바이트 하나씩 만든 정답 마스크 */
        for (int i = 0; i < 4; i++) if (((x >> (8 * i)) & 0xFFu) == 0) m |= 0x80u << (8 * i);
        uint32_t h = haszero32(x);
        if ((h != 0) != (m != 0)) bad_iff++;
        if ((h ? __builtin_ctz(h) : 32) != (m ? __builtin_ctz(m) : 32)) bad_first++;
        if (h != m) flag_diff++;
        if (zero_exact(x) != m) bad_iff += 1000000000ull;   /* 정확한 마스크도 함께 확인 */
    } while (++x != 0);
    printf("haszero: 2^32 words, iff bad=%llu, first-zero(ctz) bad=%llu, per-byte flags differ in %llu words "
           "(derived 3*65536-1=196607)  (%.2f s)\n",
           (unsigned long long)bad_iff, (unsigned long long)bad_first, (unsigned long long)flag_diff, now() - t0);

    t0 = now();
    uint64_t n = 0;
    uint32_t sub = 0xFFFFFFFFu, acc = 0;
    for (;;) { n++; acc ^= sub; if (sub == 0) break; sub = (sub - 1u) & 0xFFFFFFFFu; }
    printf("submask: mask=0xFFFFFFFF visited %llu (2^32=%llu), xor=%08X  (%.2f s)\n",
           (unsigned long long)n, 1ull << 32, acc, now() - t0);

    t0 = now();
    uint64_t total = 0, binom = 1, bad_len = 0;             /* C(32,k) 를 차례로 */
    acc = 0;
    for (int k = 0; k <= 32; k++) {
        uint32_t v = k == 32 ? 0xFFFFFFFFu : (1u << k) - 1u;
        uint64_t len = 0;
        for (;;) {
            len++; acc ^= v;
            if ((unsigned)__builtin_popcount(v) != (unsigned)k) bad_len++;
            uint32_t nx = next_same_popcount(v);
            if (nx == 0) break;
            if (nx <= v) { bad_len++; break; }
            v = nx;
        }
        if (len != binom) bad_len++;
        if (k == 16) printf("gosper : k=16 chain length %llu (C(32,16)=%llu), last=0x%08X\n",
                            (unsigned long long)len, (unsigned long long)binom, v);
        total += len;
        binom = binom * (uint64_t)(32 - k) / (uint64_t)(k + 1);
    }
    printf("gosper : 33 chains, total visits %llu (2^32=%llu), bad=%llu, xor of all=%08X  (%.2f s)\n",
           (unsigned long long)total, 1ull << 32, (unsigned long long)bad_len, acc, now() - t0);
    return 0;
}
