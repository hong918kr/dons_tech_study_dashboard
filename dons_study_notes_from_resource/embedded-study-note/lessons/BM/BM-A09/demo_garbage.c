/* BM-A09 demo_garbage.c — 호스트 C. "헤더(magic/version/size)는 맞고 나머지는 쓰레기" 인 덤프 2^20 건에
 * 대해, 검사값을 CRC-32 의 하위 8 / 16 / 24 / 32 비트만 쓸 때 몇 건이 통과(오수용)하는지 센다.
 * 기대값 = N / 2^bits. 확률 법칙이 실제로 맞는지 눈으로 확인하는 데모 (CHECK 없음, 고정 시드).
 * cc -std=c11 -Wall -Wextra -Werror -O2 demo_garbage.c -o /tmp/BM-A09_dg && /tmp/BM-A09_dg
 */
#include <stdint.h>
#include <stdio.h>

static uint32_t g_rng = 0x2026A09u;
static uint32_t xs32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }
static uint32_t crc32(const uint8_t *p, uint32_t len) {      /* CRC-32/ISO-HDLC (BM-A09 solution.c 와 같은 값) */
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

int main(void) {
    enum { N = 1 << 20 };
    static const int bits[4] = { 8, 16, 24, 32 };
    uint32_t accepted[4] = { 0 };
    union { uint32_t w[51]; uint8_t b[204]; } d;
    for (uint32_t i = 0; i < N; ++i) {
        for (int j = 0; j < 51; ++j) d.w[j] = xs32();
        d.w[0] = 0x504D5544u; d.w[1] = 2u; d.w[2] = 204u;      /* 헤더만 정상 */
        uint32_t c = crc32(d.b + 4, 196u);
        for (int k = 0; k < 4; ++k) {
            uint32_t m = bits[k] == 32 ? 0xFFFFFFFFu : ((1u << bits[k]) - 1u);
            if ((c & m) == (d.w[50] & m)) accepted[k]++;
        }
    }
    printf("header-valid garbage dumps: N = %d (2^20), fixed seed 0x2026A09\n", N);
    printf("check bits   false accepts   expected N/2^bits\n");
    for (int k = 0; k < 4; ++k) {
        double e = (double)N;
        for (int b = 0; b < bits[k]; ++b) e /= 2.0;
        printf("%10d   %13u   %17.4f\n", bits[k], accepted[k], e);
    }
    return 0;
}
