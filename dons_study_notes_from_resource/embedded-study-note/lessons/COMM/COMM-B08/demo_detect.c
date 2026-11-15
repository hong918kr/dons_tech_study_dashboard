/* COMM-B08 demo_detect: 수백만 회 오류 주입으로 미검출률 측정 + 이론값과 비교
 *
 * 메시지 = 32바이트 랜덤 데이터 || 검사값.  오류는 코드워드 전체(검사값 포함)에 주입.
 * 표 1: 서로 다른 랜덤 k 비트 (k = 1..4) 오류, 각 1,000,000 회.
 *       CRC 이론값 = A_k / C(N,k)  (A_k = 길이 N 코드의 가중치 k 코드워드 개수, 신드롬으로 정확히 셈)
 * 표 2: 길이 L 버스트 (첫·끝 비트 1, 사이 랜덤), L = 1..40, 각 250,000 회.
 *       CRC 이론값: L <= r → 0,  L = r+1 → 2^-(r-1),  L > r+1 → 2^-r
 * 비트 순서: CRC-32(반사형)는 LSB-first, 나머지는 MSB-first 와이어 (각 CRC 와 짝이 맞는 순서).
 * 속도를 위해 CRC 는 256칸 테이블 (ALGO-M02) — 시작할 때 check 값으로 자가 검증.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_detect.c -o demo_detect && ./demo_detect   (약 4 초)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NDATA 32
enum { SUM8, FL16, CRC8, CRC16, CRC32, NALG };
static const char *NAME[NALG] = { "sum8", "fletcher16", "crc8", "crc16", "crc32" };
static const int CB[NALG] = { 1, 2, 1, 2, 4 };
static const int RBITS[NALG] = { 8, 16, 8, 16, 32 };

static uint8_t  T8[256];
static uint16_t T16[256];
static uint32_t T32[256];

static void tables(void) {
    for (unsigned b = 0; b < 256; b++) {
        uint32_t c8 = b, c16 = b << 8, c32 = b;
        for (int k = 0; k < 8; k++) {
            c8  = (c8 & 0x80u) ? ((c8 << 1) ^ 0x07u) & 0xFFu : (c8 << 1) & 0xFFu;
            c16 = (c16 & 0x8000u) ? ((c16 << 1) ^ 0x1021u) & 0xFFFFu : (c16 << 1) & 0xFFFFu;
            c32 = (c32 & 1u) ? (c32 >> 1) ^ 0xEDB88320u : c32 >> 1;    /* 0x04C11DB7 반사 */
        }
        T8[b] = (uint8_t)c8; T16[b] = (uint16_t)c16; T32[b] = c32;
    }
}
static uint32_t check_value(int a, const uint8_t *p, size_t n) {
    uint32_t s1 = 0, s2 = 0, c;
    switch (a) {
    case SUM8:  for (size_t i = 0; i < n; i++) s1 += p[i]; return s1 & 0xFFu;
    case FL16:  for (size_t i = 0; i < n; i++) { s1 = (s1 + p[i]) % 255u; s2 = (s2 + s1) % 255u; } return s2 << 8 | s1;
    case CRC8:  c = 0;           for (size_t i = 0; i < n; i++) c = T8[c ^ p[i]]; return c;
    case CRC16: c = 0xFFFF;      for (size_t i = 0; i < n; i++) c = (uint16_t)(c << 8) ^ T16[(c >> 8) ^ p[i]]; return c & 0xFFFFu;
    default:    c = 0xFFFFFFFFu; for (size_t i = 0; i < n; i++) c = (c >> 8) ^ T32[(c ^ p[i]) & 0xFFu]; return ~c;
    }
}
static size_t encode(int a, const uint8_t *d, uint8_t *cw) {
    uint32_t v = check_value(a, d, NDATA);
    memcpy(cw, d, NDATA);
    for (int i = 0; i < CB[a]; i++) cw[NDATA + i] = (uint8_t)(v >> (a == CRC32 ? 8 * i : 8 * (CB[a] - 1 - i)));
    return NDATA + (size_t)CB[a];
}
static bool verify(int a, const uint8_t *cw, size_t len) {
    uint32_t rx = 0;
    for (int i = 0; i < CB[a]; i++) rx |= (uint32_t)cw[NDATA + i] << (a == CRC32 ? 8 * i : 8 * (CB[a] - 1 - i));
    (void)len;
    return check_value(a, cw, NDATA) == rx;
}

static uint32_t seed = 0xC0B08u;
static uint32_t xs32(void) { uint32_t x = seed; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return seed = x; }
static void flip(uint8_t *b, unsigned i, bool lsb) { b[i >> 3] ^= (uint8_t)(lsb ? 1u << (i & 7) : 0x80u >> (i & 7)); }

/* ---- 이론: 신드롬으로 가중치 분포 A_2, A_3, A_4 정확히 세기 ---- */
static uint32_t SYN[NDATA * 8 + 32];
static uint32_t PX[(NDATA * 8 + 32) * (NDATA * 8 + 32) / 2];
static int cmpu(const void *x, const void *y) { uint32_t a = *(const uint32_t *)x, b = *(const uint32_t *)y; return (a > b) - (a < b); }
static void weight_dist(int a, double A[5], int *Nout) {
    uint8_t zero[NDATA] = {0}, cw[NDATA + 4];
    size_t len = encode(a, zero, cw);
    int N = (int)len * 8;
    for (int i = 0; i < N; i++) {           /* 신드롬 = 비트 하나 뒤집은 코드워드의 (재계산 ^ 받은값) */
        flip(cw, (unsigned)i, a == CRC32);
        uint32_t rx = 0;
        for (int k = 0; k < CB[a]; k++) rx |= (uint32_t)cw[NDATA + k] << (a == CRC32 ? 8 * k : 8 * (CB[a] - 1 - k));
        SYN[i] = check_value(a, cw, NDATA) ^ rx;
        flip(cw, (unsigned)i, a == CRC32);
    }
    double a2 = 0, a3 = 0, eqpairs = 0;
    size_t np = 0;
    for (int i = 0; i < N; i++)
        for (int j = i + 1; j < N; j++) { a2 += SYN[i] == SYN[j]; PX[np++] = SYN[i] ^ SYN[j]; }
    for (int i = 0; i < N; i++)
        for (int j = i + 1; j < N; j++)
            for (int k = j + 1; k < N; k++) a3 += (SYN[i] ^ SYN[j]) == SYN[k];
    qsort(PX, np, sizeof PX[0], cmpu);
    for (size_t i = 0; i < np;) { size_t j = i; while (j < np && PX[j] == PX[i]) j++; double c = (double)(j - i); eqpairs += c * (c - 1) / 2; i = j; }
    A[2] = a2; A[3] = a3; A[4] = (eqpairs - a2 * (N - 2)) / 3.0;   /* 쌍-쌍 일치 중 인덱스 겹침(= 2비트 미검출) 제외 */
    *Nout = N;
}
static double choose(int n, int k) { double r = 1; for (int i = 0; i < k; i++) r = r * (n - i) / (i + 1); return r; }

int main(void) {
    tables();
    static const uint8_t s9[] = "123456789";
    if (check_value(CRC8, s9, 9) != 0xF4u || check_value(CRC16, s9, 9) != 0x29B1u || check_value(CRC32, s9, 9) != 0xCBF43926u) {
        printf("self-test failed\n"); return 1;
    }
    uint8_t d[NDATA], cw[NDATA + 4], rx[NDATA + 4];

    /* ---- 표 1: 랜덤 k 비트 ---- */
    const uint32_t TK = 1000000;
    printf("Table 1: %u random messages (%d B data) per cell, k distinct random bit flips over data+check\n", TK, NDATA);
    printf("%-11s %4s %10s %10s %10s %10s\n", "alg", "N", "k=1", "k=2", "k=3", "k=4");
    uint32_t meas[NALG][5];
    for (int a = 0; a < NALG; a++) {
        int N = (NDATA + CB[a]) * 8;
        for (int k = 1; k <= 4; k++) {
            uint32_t miss = 0;
            for (uint32_t t = 0; t < TK; t++) {
                for (int i = 0; i < NDATA; i++) d[i] = (uint8_t)xs32();
                size_t len = encode(a, d, cw);
                memcpy(rx, cw, len);
                unsigned pos[4];
                for (int j = 0; j < k; j++) {
                    bool dup;
                    do { pos[j] = xs32() % (unsigned)N; dup = false; for (int q = 0; q < j; q++) dup |= pos[q] == pos[j]; } while (dup);
                    flip(rx, pos[j], a == CRC32);
                }
                miss += verify(a, rx, len);
            }
            meas[a][k] = miss;
        }
        printf("%-11s %4d %10u %10u %10u %10u\n", NAME[a], N, meas[a][1], meas[a][2], meas[a][3], meas[a][4]);
    }
    printf("\nCRC theory (exact): expected count = 1e6 * A_k / C(N,k)\n");
    printf("%-11s %4s %12s %12s %12s | %10s %10s %10s\n", "alg", "N", "A_2", "A_3", "A_4", "E[k=2]", "E[k=3]", "E[k=4]");
    for (int a = CRC8; a <= CRC32; a++) {
        double A[5]; int N;
        weight_dist(a, A, &N);
        printf("%-11s %4d %12.0f %12.0f %12.0f | %10.1f %10.1f %10.1f\n", NAME[a], N, A[2], A[3], A[4],
               TK * A[2] / choose(N, 2), TK * A[3] / choose(N, 3), TK * A[4] / choose(N, 4));
    }

    /* ---- 표 2: 버스트 ---- */
    const uint32_t TB = 250000;
    printf("\nTable 2: bursts of length L (first+last bit flipped, interior random), %u trials per cell\n", TB);
    printf("%3s %10s %10s %10s %10s %10s | %9s %9s %9s\n", "L", "sum8", "fletcher16", "crc8", "crc16", "crc32", "th crc8", "th crc16", "th crc32");
    uint64_t agg16 = 0, agg32 = 0;
    for (unsigned L = 1; L <= 40; L++) {
        uint32_t miss[NALG];
        for (int a = 0; a < NALG; a++) {
            unsigned N = (unsigned)(NDATA + CB[a]) * 8;
            miss[a] = 0;
            for (uint32_t t = 0; t < TB; t++) {
                for (int i = 0; i < NDATA; i++) d[i] = (uint8_t)xs32();
                size_t len = encode(a, d, cw);
                memcpy(rx, cw, len);
                unsigned s = xs32() % (N - L + 1);
                flip(rx, s, a == CRC32);
                if (L >= 2) flip(rx, s + L - 1, a == CRC32);
                for (unsigned b = s + 1; b + 1 < s + L; b++) if (xs32() & 1u) flip(rx, b, a == CRC32);
                miss[a] += verify(a, rx, len);
            }
        }
        double th[3];
        for (int c = 0; c < 3; c++) {
            int r = RBITS[CRC8 + c];
            th[c] = (int)L <= r ? 0.0 : (int)L == r + 1 ? TB / (double)(1ull << (r - 1)) : TB / (double)(1ull << r);
        }
        if (L >= 18) agg16 += miss[CRC16];
        if (L >= 18) agg32 += miss[CRC32];
        printf("%3u %10u %10u %10u %10u %10u | %9.2f %9.2f %9.2g\n", L, miss[SUM8], miss[FL16], miss[CRC8], miss[CRC16], miss[CRC32], th[0], th[1], th[2]);
    }
    printf("\ncrc16, L=18..40 summed: %llu undetected of %u trials (theory 2^-16 -> %.1f)\n",
           (unsigned long long)agg16, 23 * TB, 23.0 * TB / 65536.0);
    printf("crc32, L=18..40 summed: %llu undetected of %u trials (theory 2^-32 -> %.4f)\n",
           (unsigned long long)agg32, 23 * TB, 23.0 * TB / 4294967296.0);
    return 0;
}
