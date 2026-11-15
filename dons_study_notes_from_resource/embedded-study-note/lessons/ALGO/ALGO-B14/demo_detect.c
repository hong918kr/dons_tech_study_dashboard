/* ALGO-B14 오류 주입 실험: 체크섬별 검출률
 *
 * 32바이트 랜덤 메시지에 오류 패턴을 하나 넣고 "체크섬 값이 바뀌었나"(= 검출)를 센다.
 * 각 패턴 200000회, xorshift32 고정 시드 → 매번 같은 표가 나온다.
 * 마지막 열 CRC-16/CCITT-FALSE(poly 0x1021, init 0xFFFF)는 비교용 (ALGO-M01 에서 자세히).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_detect.c -o demo_detect && ./demo_detect
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define N      32
#define TRIALS 200000

static uint32_t seed = 0x1B14u;
static uint32_t xs32(void) { uint32_t x = seed; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return seed = x; }

static uint32_t c_sum8(const uint8_t *p, size_t n) { uint8_t s = 0; for (size_t i = 0; i < n; i++) s = (uint8_t)(s + p[i]); return s; }
static uint32_t c_xor8(const uint8_t *p, size_t n) { uint8_t x = 0; for (size_t i = 0; i < n; i++) x ^= p[i]; return x; }
static uint32_t c_inet(const uint8_t *p, size_t n) {
    uint32_t s = 0;
    for (size_t i = 0; i + 1 < n; i += 2) s += (uint32_t)p[i] << 8 | p[i + 1];
    if (n & 1) s += (uint32_t)p[n - 1] << 8;
    while (s >> 16) s = (s & 0xFFFFu) + (s >> 16);
    return (uint16_t)~s;
}
static uint32_t c_f16(const uint8_t *p, size_t n) {
    uint32_t a = 0, b = 0;
    for (size_t i = 0; i < n; i++) { a = (a + p[i]) % 255u; b = (b + a) % 255u; }
    return b << 8 | a;
}
static uint32_t c_f32(const uint8_t *p, size_t n) {
    uint32_t a = 0, b = 0;
    for (size_t i = 0; i < n; i += 2) {
        uint32_t w = p[i] | (i + 1 < n ? (uint32_t)p[i + 1] << 8 : 0u);
        a = (a + w) % 65535u; b = (b + a) % 65535u;
    }
    return b << 16 | a;
}
static uint32_t c_crc16(const uint8_t *p, size_t n) {   /* CRC-16/CCITT-FALSE, check("123456789") = 0x29B1 */
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint16_t)(p[i] << 8);
        for (int k = 0; k < 8; k++) c = (c & 0x8000u) ? (uint16_t)((c << 1) ^ 0x1021u) : (uint16_t)(c << 1);
    }
    return c;
}

typedef uint32_t (*cks_fn)(const uint8_t *, size_t);
static const cks_fn FN[6] = { c_sum8, c_xor8, c_inet, c_f16, c_f32, c_crc16 };
static const char *NAME[6] = { "sum8", "xor8", "inet16", "fl16", "fl32", "crc16" };

static void flip(uint8_t *m, unsigned bit) { m[bit / 8] ^= (uint8_t)(1u << (bit % 8)); }

/* 오류 패턴. m 을 고치고 새 길이를 돌려준다 (원본 o 는 그대로). */
static size_t inject(int kind, const uint8_t *o, uint8_t *m) {
    memcpy(m, o, N);
    switch (kind) {
    case 0: flip(m, xs32() % (8 * N)); return N;                                /* 1비트 */
    case 1: { unsigned a = xs32() % (8 * N), b; do b = xs32() % (8 * N); while (b == a);
              flip(m, a); flip(m, b); return N; }                               /* 2비트, 아무 곳 */
    case 2: { unsigned k = xs32() % 8, i = xs32() % N, j; do j = xs32() % N; while (j == i);
              flip(m, 8 * i + k); flip(m, 8 * j + k); return N; }               /* 2비트, 같은 비트 위치 */
    case 3: { unsigned i = xs32() % (N - 1); uint8_t t = m[i]; m[i] = m[i + 1]; m[i + 1] = t; return N; } /* 인접 바이트 교환 */
    case 4: { unsigned w = xs32() % (N / 2 - 1);                                /* 인접한 정렬 16비트 워드 교환 */
              uint8_t t0 = m[2 * w], t1 = m[2 * w + 1];
              m[2 * w] = m[2 * w + 2]; m[2 * w + 1] = m[2 * w + 3]; m[2 * w + 2] = t0; m[2 * w + 3] = t1; return N; }
    case 5: return N;                       /* 0x00 → 0xFF 바이트: 원본에 0x00 을 심어 둔 뒤 main 에서 처리 */
    case 6: return N;                       /* 0x0000 → 0xFFFF 워드: 동일 */
    case 7: for (int k = 0; k < 4; k++) m[xs32() % N] = (uint8_t)xs32(); return N;  /* 랜덤 4바이트 덮어쓰기 */
    case 8: { unsigned L = 2 + xs32() % 15, s = xs32() % (8 * N - L + 1);       /* 버스트 2..16비트: 양 끝 비트 반드시 뒤집힘 */
              flip(m, s); flip(m, s + L - 1);
              for (unsigned b = s + 1; b < s + L - 1; b++) if (xs32() & 1u) flip(m, b);
              return N; }
    case 9: memmove(m + 2, o, N); m[0] = m[1] = 0x00; return N + 2;            /* 앞에 0x00 두 바이트 삽입 */
    }
    return N;
}

static const char *KIND[10] = {
    "1-bit flip", "2-bit flip (anywhere)", "2-bit, same bit pos", "adjacent byte swap",
    "adjacent 16-bit word swap", "one byte 0x00 -> 0xFF", "aligned word 0x0000 -> 0xFFFF",
    "4 random bytes overwritten", "burst 2..16 bits", "prepend two 0x00 bytes" };

int main(void) {
    const uint8_t chk[9] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    if (c_crc16(chk, 9) != 0x29B1 || c_f16((const uint8_t *)"abcde", 5) != 0xC8F0) { printf("self-check failed\n"); return 1; }

    printf("%-30s %7s", "error pattern (32-byte msg)", "trials");
    for (int c = 0; c < 6; c++) printf(" %8s", NAME[c]);
    printf("\n");
    for (int kind = 0; kind < 10; kind++) {
        long det[6] = {0}, trials = 0;
        while (trials < TRIALS) {
            uint8_t o[N], m[N + 2];
            for (int i = 0; i < N; i++) o[i] = (uint8_t)xs32();
            size_t mlen;
            if (kind == 5) { unsigned i = xs32() % N; o[i] = 0x00; mlen = inject(kind, o, m); m[i] = 0xFF; }
            else if (kind == 6) { unsigned w = xs32() % (N / 2); o[2 * w] = o[2 * w + 1] = 0x00;
                                  mlen = inject(kind, o, m); m[2 * w] = m[2 * w + 1] = 0xFF; }
            else mlen = inject(kind, o, m);
            if (mlen == N && memcmp(o, m, N) == 0) continue;   /* 실제로 안 바뀐 경우(같은 값 교환 등)는 빼고 센다 */
            trials++;
            for (int c = 0; c < 6; c++) det[c] += FN[c](o, N) != FN[c](m, mlen);
        }
        printf("%-30s %7ld", KIND[kind], trials);
        for (int c = 0; c < 6; c++) printf(" %7.3f%%", 100.0 * (double)det[c] / (double)trials);
        printf("\n");
    }
    return 0;
}
