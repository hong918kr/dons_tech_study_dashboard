/* ALGO-B13 추가 연습 — 한 번에 검증
 *   연습 1: perm_xor32 — 모든 재배열을 "비트 i → 비트 i^m" 하나로
 *   연습 2: 버퍼 니블 역순 (스크린 문제의 "input buffer data" 꼬리질문)
 *   연습 3: 반사(reflected) CRC = 입력·출력 비트 반전 (CRC-16/ARC vs CRC-16/UMTS, MODBUS)
 *   연습 4: FFT 비트 반전 재배열 (n 비트 반전)
 *   연습 5: 16-entry 니블 표로 32비트 반전 (16 B)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t rng = 0xC0FFEE11u;
static uint32_t xs32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* 참조: 비트 i → 비트 i^m (n 비트) */
static uint64_t ref_xor_perm(uint64_t x, unsigned n, unsigned m) {
    uint64_t r = 0;
    for (unsigned i = 0; i < n; i++)
        if ((x >> i) & 1u) r |= 1ull << (i ^ m);
    return r;
}

/* ---------- 연습 1: m 의 비트 k 가 1 이면 "거리 2^k 짝 바꾸기" 단계를 적용 ---------- */
static uint32_t perm_xor32(uint32_t x, unsigned m) {
    static const uint32_t lo[5] = { 0x55555555u, 0x33333333u, 0x0F0F0F0Fu, 0x00FF00FFu, 0x0000FFFFu };
    for (unsigned k = 0; k < 5; k++)
        if ((m >> k) & 1u) {
            unsigned s = 1u << k;
            x = ((x >> s) & lo[k]) | ((x & lo[k]) << s);
        }
    return x;
}

/* ---------- 연습 2: 바이트 배열 (buf[0] = 가장 높은 자리, 쓴 순서 그대로) 의 hex 자리 역순 ---------- */
static uint8_t swap_nib(uint8_t b) { return (uint8_t)((b << 4) | (b >> 4)); }
static void rev_nibbles_buf(uint8_t *buf, size_t n) {
    if (n == 0) return;
    size_t i = 0, j = n - 1;
    while (i < j) {
        uint8_t a = buf[i];
        buf[i++] = swap_nib(buf[j]);
        buf[j--] = swap_nib(a);
    }
    if (i == j) buf[i] = swap_nib(buf[i]);      /* 홀수 길이: 가운데 바이트는 자기 니블만 swap */
}
static uint64_t load_be64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | p[i];
    return v;
}

/* ---------- 연습 3: 같은 다항식 0x8005, MSB-first(비반사) 와 LSB-first(반사) ---------- */
static uint16_t crc16_msb(const uint8_t *d, size_t n, uint16_t init) {      /* refin=false, refout=false */
    uint16_t c = init;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint16_t)(d[i] << 8);
        for (int k = 0; k < 8; k++)
            c = (c & 0x8000u) ? (uint16_t)((c << 1) ^ 0x8005u) : (uint16_t)(c << 1);
    }
    return c;
}
static uint16_t crc16_lsb(const uint8_t *d, size_t n, uint16_t init) {      /* refin=true, refout=true */
    uint16_t c = init;                                                       /* 0xA001 = 0x8005 비트 반전 */
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int k = 0; k < 8; k++)
            c = (c & 1u) ? (uint16_t)((c >> 1) ^ 0xA001u) : (uint16_t)(c >> 1);
    }
    return c;
}
static uint16_t rev16(uint16_t x) { return (uint16_t)ref_xor_perm(x, 16, 15); }
static uint8_t rev8(uint8_t x) { return (uint8_t)ref_xor_perm(x, 8, 7); }

/* ---------- 연습 4: n 비트 반전 (1 ≤ n ≤ 32), FFT 재배열 ---------- */
static uint32_t rev32_dc(uint32_t x) {
    x = ((x >> 1) & 0x55555555u) | ((x & 0x55555555u) << 1);
    x = ((x >> 2) & 0x33333333u) | ((x & 0x33333333u) << 2);
    x = ((x >> 4) & 0x0F0F0F0Fu) | ((x & 0x0F0F0F0Fu) << 4);
    x = ((x >> 8) & 0x00FF00FFu) | ((x & 0x00FF00FFu) << 8);
    return (x >> 16) | (x << 16);
}
static uint32_t rev_n(uint32_t i, unsigned nbits) {      /* nbits == 0 이면 32-0 = 32 시프트(UB) → 따로 */
    return nbits == 0 ? 0u : rev32_dc(i) >> (32u - nbits);
}
static void bitrev_permute(int32_t *a, unsigned log2n) {
    uint32_t n = 1u << log2n;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t j = rev_n(i, log2n);
        if (j > i) { int32_t t = a[i]; a[i] = a[j]; a[j] = t; }   /* 짝마다 한 번만 */
    }
}

/* ---------- 연습 5: 16 B 니블 표 ---------- */
static const uint8_t REV4[16] = { 0x0, 0x8, 0x4, 0xC, 0x2, 0xA, 0x6, 0xE, 0x1, 0x9, 0x5, 0xD, 0x3, 0xB, 0x7, 0xF };
static uint32_t rev32_lut16(uint32_t x) {
    uint32_t r = 0;
    for (int i = 0; i < 8; i++) { r = (r << 4) | REV4[x & 0xFu]; x >>= 4; }
    return r;
}

int main(void) {
    /* 연습 1 */
    long bad = 0;
    const unsigned ms[6] = { 31, 1, 4, 28, 24, 7 };   /* 비트반전, 홀짝, 니블swap, 니블역순, bswap, 바이트마다 비트반전 */
    for (int i = 0; i < 100000; i++) {
        uint32_t x = xs32();
        unsigned a = xs32() & 31u, b = xs32() & 31u;
        if (perm_xor32(x, a) != (uint32_t)ref_xor_perm(x, 32, a)) bad++;
        if (perm_xor32(perm_xor32(x, a), b) != perm_xor32(x, a ^ b)) bad++;   /* 합성 = m 의 XOR */
        if (perm_xor32(x, ms[i % 6]) != (uint32_t)ref_xor_perm(x, 32, ms[i % 6])) bad++;
    }
    CHECK(bad == 0);
    CHECK(perm_xor32(0x12345678u, 24) == __builtin_bswap32(0x12345678u));
    CHECK(perm_xor32(0x12345678u, 28) == 0x87654321u);
    CHECK(perm_xor32(0x12345678u, 31) == 0x1E6A2C48u);

    /* 연습 2 */
    {
        uint8_t b8[8] = { 0x12, 0x34, 0x56, 0x78, 0x90, 0xAB, 0xCD, 0xEF };
        const uint8_t w8[8] = { 0xFE, 0xDC, 0xBA, 0x09, 0x87, 0x65, 0x43, 0x21 };
        rev_nibbles_buf(b8, 8);
        CHECK(memcmp(b8, w8, 8) == 0);
        CHECK(load_be64(b8) == 0xFEDCBA0987654321ull);
        uint8_t b3[3] = { 0x12, 0x34, 0x56 };
        rev_nibbles_buf(b3, 3);
        CHECK(b3[0] == 0x65 && b3[1] == 0x43 && b3[2] == 0x21);
        uint8_t b1[1] = { 0xAB };
        rev_nibbles_buf(b1, 1);
        CHECK(b1[0] == 0xBA);
        rev_nibbles_buf(NULL, 0);
        bad = 0;
        for (int t = 0; t < 20000; t++) {            /* 8바이트 버퍼 = BE 로 읽은 64비트의 니블 역순 */
            uint8_t b[8];
            for (int i = 0; i < 8; i++) b[i] = (uint8_t)xs32();
            uint64_t want = ref_xor_perm(load_be64(b), 64, 60);
            rev_nibbles_buf(b, 8);
            if (load_be64(b) != want) bad++;
        }
        CHECK(bad == 0);
    }

    /* 연습 3: 카탈로그 check 값 ("123456789") + 관계식 */
    {
        const uint8_t chk[9] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
        uint16_t umts = crc16_msb(chk, 9, 0x0000), arc = crc16_lsb(chk, 9, 0x0000), modbus = crc16_lsb(chk, 9, 0xFFFF);
        printf("CRC-16/UMTS=0x%04X  CRC-16/ARC=0x%04X  CRC-16/MODBUS=0x%04X\n", umts, arc, modbus);
        CHECK(umts == 0xFEE8u && arc == 0xBB3Du && modbus == 0x4B37u);
        bad = 0;
        for (int t = 0; t < 2000; t++) {
            uint8_t d[64], r[64];
            size_t n = xs32() % 65u;
            uint16_t init = (uint16_t)xs32();
            for (size_t i = 0; i < n; i++) { d[i] = (uint8_t)xs32(); r[i] = rev8(d[i]); }
            /* 반사 CRC(init) = rev16( 비반사 CRC(바이트마다 비트 반전한 입력, rev16(init)) ) */
            if (crc16_lsb(d, n, init) != rev16(crc16_msb(r, n, rev16(init)))) bad++;
        }
        CHECK(bad == 0);
    }

    /* 연습 4 */
    {
        int32_t a8[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
        const int32_t w8[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };
        bitrev_permute(a8, 3);
        CHECK(memcmp(a8, w8, sizeof a8) == 0);
        static int32_t a[1024];
        for (int i = 0; i < 1024; i++) a[i] = i;
        bitrev_permute(a, 10);
        bad = 0;
        for (uint32_t i = 0; i < 1024; i++) if ((uint32_t)a[i] != (uint32_t)ref_xor_perm(i, 32, 31) >> 22) bad++;
        bitrev_permute(a, 10);                        /* 두 번 = 제자리 */
        for (int i = 0; i < 1024; i++) if (a[i] != i) bad++;
        for (unsigned nb = 1; nb <= 32; nb++)
            for (int t = 0; t < 200; t++) {
                uint32_t x = nb == 32 ? xs32() : xs32() & ((1u << nb) - 1u);
                if (rev_n(x, nb) != (uint32_t)(ref_xor_perm(x, 32, 31) >> (32u - nb))) bad++;
            }
        CHECK(bad == 0);
        CHECK(rev_n(5u, 0) == 0u && rev_n(1u, 1) == 1u && rev_n(1u, 32) == 0x80000000u);
    }

    /* 연습 5 */
    {
        bad = 0;
        for (uint32_t v = 0; v < 65536u; v++) {
            if (rev32_lut16(v) != (uint32_t)ref_xor_perm(v, 32, 31)) bad++;
            if (rev32_lut16(v << 16) != (uint32_t)ref_xor_perm(v << 16, 32, 31)) bad++;
        }
        for (int i = 0; i < 100000; i++) { uint32_t x = xs32(); if (rev32_lut16(x) != rev32_dc(x)) bad++; }
        CHECK(bad == 0);
        CHECK(sizeof REV4 == 16);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
