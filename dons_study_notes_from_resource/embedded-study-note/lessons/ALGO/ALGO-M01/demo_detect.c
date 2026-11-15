/* ALGO-M01 demo_detect.c — CRC 오류 검출 능력 전수 측정 (CRC-8/SMBUS, CRC-16/IBM-3740, CRC-16/MODBUS)
 *
 * 선형성: 길이가 같으면 (codeword ⊕ E) 의 레지스터 = codeword 의 레지스터 ⊕ E(x)·x^w mod G.
 * gcd(x^w, G) = 1 이므로 "오류 E 를 놓친다" ⇔ G(x) | E(x) ⇔ Σ_{i∈E} x^i ≡ 0 (mod G).
 * 그래서 비트 i(지수 i, 0 = 마지막에 읽힌 비트)의 신드롬 S_i = x^i mod G 를 미리 구해 두고 XOR 로 센다.
 *   [1] 8바이트 데이터 codeword: 무게 1·2·3·4 오류 전수 (신드롬) + 무게 1·2 는 실제 엔진으로도 전수 대조
 *   [2] 버스트 길이 b = 1..w+4 전수: 놓친 비율 vs 이론 2^-(w-1) (b = w+1), 2^-w (b > w+1)
 *   [3] 2비트 오류 검출 한계 = G 의 주기(x^d ≡ 1 인 최소 d): 데이터 길이별 놓친 2비트 오류 개수 + 엔진 확인
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_detect.c -o demo_detect && ./demo_detect
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct { const char *name; unsigned width; uint32_t poly, init; bool refin, refout; uint32_t xorout, residue; } crc_params;

static uint32_t crc_mask(unsigned w) { return w >= 32 ? 0xFFFFFFFFu : (1u << w) - 1u; }
static uint32_t reflect(uint32_t x, unsigned n) { uint32_t r = 0; for (unsigned i = 0; i < n; i++) { r = (r << 1) | (x & 1u); x >>= 1; } return r; }
static uint32_t crc_bitwise(const crc_params *p, const uint8_t *d, size_t n) {
    const unsigned w = p->width; const uint32_t mask = crc_mask(w);
    uint32_t crc = p->init & mask;
    for (size_t i = 0; i < n; i++) {
        uint8_t b = p->refin ? (uint8_t)reflect(d[i], 8) : d[i];
        for (int k = 7; k >= 0; k--) {
            uint32_t top = ((crc >> (w - 1)) ^ ((uint32_t)b >> k)) & 1u;
            crc = (crc << 1) & mask;
            if (top) crc ^= p->poly;
        }
    }
    if (p->refout) crc = reflect(crc, w);
    return (crc ^ p->xorout) & mask;
}
static size_t crc_append(const crc_params *p, uint8_t *buf, size_t n) {
    uint32_t c = crc_bitwise(p, buf, n); unsigned nb = p->width / 8u;
    for (unsigned i = 0; i < nb; i++) buf[n + i] = p->refout ? (uint8_t)(c >> (8u * i)) : (uint8_t)(c >> (8u * (nb - 1u - i)));
    return n + nb;
}
static bool residue_ok(const crc_params *p, const uint8_t *f, size_t len) { return (crc_bitwise(p, f, len) ^ p->xorout) == p->residue; }

/* 읽는 순서 r 번째 비트(0 = 처음)를 뒤집는다: refin 이면 바이트 안 LSB 먼저 */
static void flip_read_bit(const crc_params *p, uint8_t *f, size_t r) {
    f[r / 8] ^= (uint8_t)(p->refin ? 1u << (r % 8) : 0x80u >> (r % 8));
}

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

#define MAXBITS 70000
static uint32_t S[MAXBITS];        /* S[i] = x^i mod G */
static void syndromes(const crc_params *p, size_t nbits) {
    const unsigned w = p->width; uint32_t r = 1;
    for (size_t i = 0; i < nbits; i++) {
        S[i] = r;
        uint32_t top = (r >> (w - 1)) & 1u;
        r = (r << 1) & crc_mask(w);
        if (top) r ^= p->poly;
    }
}
static uint32_t period(const crc_params *p) {
    const unsigned w = p->width; uint32_t r = 1;
    for (uint32_t k = 1; k <= (1u << w); k++) {
        uint32_t top = (r >> (w - 1)) & 1u;
        r = (r << 1) & crc_mask(w);
        if (top) r ^= p->poly;
        if (r == 1) return k;
    }
    return 0;
}

int main(void) {
    const crc_params P[3] = {
        { "CRC-8/SMBUS",     8, 0x07,   0x00,   false, false, 0x00, 0x00 },
        { "CRC-16/IBM-3740", 16, 0x1021, 0xFFFF, false, false, 0x00, 0x00 },
        { "CRC-16/MODBUS",   16, 0x8005, 0xFFFF, true,  true,  0x00, 0x00 },
    };
    clock_t t0 = clock();
    uint32_t seed = 0xDE7EC7u;
    static uint8_t cw[8192], bad[8192];

    printf("[1] 8-byte data: undetected errors by weight (exhaustive)\n");
    printf("%-16s %5s %12s %12s %12s %14s   engine check (w1,w2)\n", "crc", "bits", "w=1", "w=2", "w=3", "w=4");
    for (int a = 0; a < 3; a++) {
        const crc_params *p = &P[a];
        for (int i = 0; i < 8; i++) cw[i] = (uint8_t)xs32(&seed);
        size_t len = crc_append(p, cw, 8), N = 8 * len;
        syndromes(p, N);
        long m1 = 0, m2 = 0, m3 = 0, m4 = 0, t4 = 0;
        for (size_t i = 0; i < N; i++) {
            m1 += S[i] == 0;
            for (size_t j = i + 1; j < N; j++) {
                uint32_t sij = S[i] ^ S[j];
                m2 += sij == 0;
                for (size_t k = j + 1; k < N; k++) {
                    uint32_t sijk = sij ^ S[k];
                    m3 += sijk == 0;
                    for (size_t l = k + 1; l < N; l++) { t4++; m4 += sijk == S[l]; }
                }
            }
        }
        /* 엔진으로 무게 1·2 전수: 읽는 순서 r 은 지수 N-1-r */
        long e1 = 0, e2 = 0;
        for (size_t r = 0; r < N; r++) {
            memcpy(bad, cw, len); flip_read_bit(p, bad, r); e1 += residue_ok(p, bad, len);
            for (size_t q = r + 1; q < N; q++) {
                memcpy(bad, cw, len); flip_read_bit(p, bad, r); flip_read_bit(p, bad, q);
                e2 += residue_ok(p, bad, len);
            }
        }
        printf("%-16s %5zu %5ld/%-6zu %5ld/%-6zu %5ld/%-6zu %6ld/%-7ld   %ld, %ld\n", p->name, N,
               m1, N, m2, N * (N - 1) / 2, m3, N * (N - 1) * (N - 2) / 6, m4, t4, e1, e2);
    }

    printf("\n[2] bursts in the CRC's bit order, 8-byte data codeword (exhaustive)\n");
    printf("%-16s %3s %10s %8s %12s %12s\n", "crc", "b", "patterns", "missed", "missed frac", "theory");
    for (int a = 0; a < 3; a++) {
        const crc_params *p = &P[a];
        const unsigned w = p->width;
        size_t N = 8 * (8 + w / 8);
        syndromes(p, N);
        for (unsigned b = 1; b <= w + 4; b++) {
            if (b < w - 1 && b != 1 && b != w / 2) continue;      /* 표를 짧게: 1, w/2, w-1 .. w+4 */
            long tried = 0, missed = 0;
            for (size_t s = 0; s + b <= N; s++) {
                /* 비트 s 와 s+b-1 은 1, 사이 b-2 비트는 Gray 코드로 하나씩 바꾸며 신드롬 갱신 */
                uint32_t syn = S[s] ^ (b > 1 ? S[s + b - 1] : 0u);
                uint32_t cnt = b >= 2 ? 1u << (b - 2) : 1u;
                for (uint32_t g = 0; g < cnt; g++) {
                    if (g) { unsigned bit = (unsigned)__builtin_ctz(g); syn ^= S[s + 1 + bit]; }
                    tried++; missed += syn == 0;
                }
            }
            double th = b <= w ? 0.0 : (b == w + 1 ? 1.0 / (double)(1u << (w - 1)) : 1.0 / (double)(1u << w));
            printf("%-16s %3u %10ld %8ld %12.3e %12.3e\n", p->name, b, tried, missed, (double)missed / (double)tried, th);
        }
    }

    printf("\n[3] 2-bit errors vs data length (exhaustive, syndromes) and polynomial period\n");
    for (int a = 0; a < 3; a++) {
        const crc_params *p = &P[a];
        uint32_t per = period(p);
        printf("%-16s period(x mod G) = %u bits\n", p->name, per);
        const size_t lens8[] = { 8, 15, 16, 32, 64 };
        const size_t lens16[] = { 64, 4000, 4094, 4095, 8000 };
        for (int t = 0; t < 5; t++) {
            size_t dlen = p->width == 8 ? lens8[t] : lens16[t];
            size_t N = 8 * (dlen + p->width / 8);
            syndromes(p, N);
            long missed = 0;
            for (size_t d = 1; d < N; d++) if (S[d] == 1) missed += (long)(N - d);   /* x^i(x^d + 1): S_d == 1 */
            printf("   data %5zu B (%6zu-bit codeword): undetected 2-bit errors = %ld\n", dlen, N, missed);
        }
        /* 엔진 확인: 거리 = 주기인 두 비트는 놓치고, 거리 = 주기 - 1 은 잡는다 */
        size_t dlen = p->width == 8 ? 16 : 4095, len;
        for (size_t i = 0; i < dlen; i++) cw[i] = (uint8_t)xs32(&seed);
        len = crc_append(p, cw, dlen);
        memcpy(bad, cw, len); flip_read_bit(p, bad, 3); flip_read_bit(p, bad, 3 + per);
        bool miss_per = residue_ok(p, bad, len);
        memcpy(bad, cw, len); flip_read_bit(p, bad, 3); flip_read_bit(p, bad, 3 + per - 1);
        bool miss_per1 = residue_ok(p, bad, len);
        printf("   engine, %zu-byte data: flip bits 3 and 3+%u -> %s; 3 and 3+%u -> %s\n", dlen, per,
               miss_per ? "UNDETECTED" : "detected", per - 1, miss_per1 ? "UNDETECTED" : "detected");
    }
    printf("\ntotal %.2f s\n", (double)(clock() - t0) / CLOCKS_PER_SEC);
    return 0;
}
