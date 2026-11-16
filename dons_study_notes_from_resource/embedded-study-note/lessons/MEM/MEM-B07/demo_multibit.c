/* MEM-B07 demo — k비트 오류에 대한 SECDED(72,64) 판정 분포
 *   k = 1..4 : 전수 (C(72,4) = 1,028,790 까지)
 *   k = 5..8 : 무작위 1,000,000회 (고정 시드)
 * 코덱은 solution.c 의 위치형 SECDED 축약판. 선형 코드라 결과는 데이터와 무관 → 오류 패턴만으로 판정한다.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_multibit.c -o demo_multibit && ./demo_multibit
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned parity64(uint64_t x) {
    x ^= x >> 32; x ^= x >> 16; x ^= x >> 8; x ^= x >> 4; x ^= x >> 2; x ^= x >> 1;
    return (unsigned)(x & 1u);
}
static bool is_pow2(unsigned x) { return x && !(x & (x - 1)); }
static uint64_t xs64(uint64_t *s) { uint64_t x = *s; x ^= x << 13; x ^= x >> 7; x ^= x << 17; return *s = x; }

static uint8_t POS[64]; static uint64_t CMASK[7]; static uint8_t SYN_TAB[256];
static void secded_init(void) {
    int i = 0;
    for (unsigned p = 1; p < 72; p++) if (!is_pow2(p)) POS[i++] = (uint8_t)p;
    for (int j = 0; j < 7; j++) { CMASK[j] = 0; for (int k = 0; k < 64; k++) if (POS[k] >> j & 1u) CMASK[j] |= 1ull << k; }
    memset(SYN_TAB, 0xFE, sizeof SYN_TAB);
    SYN_TAB[0] = 0xFF; SYN_TAB[0x80] = 71;
    for (int j = 0; j < 7; j++) SYN_TAB[0x80 | 1u << j] = (uint8_t)(64 + j);
    for (int k = 0; k < 64; k++) SYN_TAB[0x80 | POS[k]] = (uint8_t)k;
}
enum { OUT_CORR, OUT_UE, OUT_MIS, OUT_SILENT };
static int classify_err(uint64_t ed, uint8_t ec) {
    unsigned s = 0;
    for (int j = 0; j < 7; j++) s |= parity64(ed & CMASK[j]) << j;
    s ^= ec & 0x7Fu;
    uint8_t syn = (uint8_t)(s | (parity64(ed) ^ parity64(ec)) << 7);
    unsigned e = SYN_TAB[syn];
    if (e == 0xFF) return OUT_SILENT;
    if (e == 0xFE) return OUT_UE;
    if (e < 64) ed ^= 1ull << e; else ec ^= (uint8_t)(1u << (e - 64));
    return (ed == 0 && ec == 0) ? OUT_CORR : OUT_MIS;
}
static void set_bit(uint64_t *ed, uint8_t *ec, unsigned b) {
    if (b < 64) *ed ^= 1ull << b; else *ec ^= (uint8_t)(1u << (b - 64));
}

static uint64_t cnt[4];
static void rec(unsigned start, unsigned left, uint64_t ed, uint8_t ec) {   /* 조합 전수 */
    if (!left) { cnt[classify_err(ed, ec)]++; return; }
    for (unsigned b = start; b + left <= 72; b++) {
        uint64_t d = ed; uint8_t c = ec;
        set_bit(&d, &c, b);
        rec(b + 1, left - 1, d, c);
    }
}

int main(void) {
    secded_init();
    printf("k  mode           trials    corrected          UE                 miscorrected       silent(SDC)\n");
    for (unsigned k = 1; k <= 8; k++) {
        memset(cnt, 0, sizeof cnt);
        uint64_t n;
        const char *mode;
        if (k <= 4) { rec(0, k, 0, 0); mode = "exhaustive"; }
        else {
            uint64_t rng = 0xC0FFEE00ull + k;
            for (uint32_t t = 0; t < 1000000u; t++) {
                uint64_t ed = 0; uint8_t ec = 0; unsigned picked = 0;
                while (picked < k) {                            /* 서로 다른 k 비트 */
                    unsigned b = (unsigned)(xs64(&rng) % 72u);
                    bool on = b < 64 ? (ed >> b & 1u) : (ec >> (b - 64) & 1u);
                    if (!on) { set_bit(&ed, &ec, b); picked++; }
                }
                cnt[classify_err(ed, ec)]++;
            }
            mode = "random 1M";
        }
        n = cnt[0] + cnt[1] + cnt[2] + cnt[3];
        printf("%u  %-11s %9llu", k, mode, (unsigned long long)n);
        for (int o = 0; o < 4; o++)
            printf("  %8llu (%5.1f%%)", (unsigned long long)cnt[o], 100.0 * (double)cnt[o] / (double)n);
        printf("\n");
    }
    return 0;
}
