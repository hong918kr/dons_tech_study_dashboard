/* ALGO-M02 추가 연습 — 한 번에 검증
 *
 * 연습 1: CRC-32C (Castagnoli, iSCSI/ext4/SCTP) 바이트 테이블 — check 0xE3069283 (+ 이 맥의 ARMv8 CRC32C 명령과 대조)
 * 연습 2: 니블(4비트) 테이블 CRC-32 — 64 바이트 테이블로 플래시 절약
 * 연습 3: 비반사(MSB-first) CRC-32/MPEG-2 · BZIP2 테이블 — check 0x0376E6E7 / 0xFC891918, 32비트 워드 공급
 * 연습 4: 지워진 플래시(0xFF × n) 의 CRC 를 combine 배가(doubling)로 O(log² n)
 * 연습 5: CRC 위조 — 메시지 뒤 4바이트를 골라 원하는 CRC-32 를 만든다 (역 테이블 스텝)
 *
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   (zlib 불필요)
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#if defined(__ARM_FEATURE_CRC32)
#include <arm_acle.h>
#endif

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* ---- 공통: 반사형 CRC-32 기준(비트 단위, poly 를 인자로) ---- */
static uint32_t crc_reflect_bitwise(uint32_t poly_r, uint32_t crc, const uint8_t *p, size_t n) {
    uint32_t c = ~crc;
    while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (poly_r & (0u - (c & 1u))); }
    return ~c;
}
static void make_table_r(uint32_t poly_r, uint32_t t[256]) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (poly_r & (0u - (c & 1u)));
        t[i] = c;
    }
}
#define POLY_R   0xEDB88320u   /* CRC-32/ISO-HDLC */
#define POLY_C_R 0x82F63B78u   /* CRC-32C (Castagnoli 0x1EDC6F41 의 반사) */
static uint32_t T32[256];
static uint32_t crc32_tab(uint32_t crc, const uint8_t *p, size_t n) {
    uint32_t c = ~crc;
    while (n--) c = T32[(c ^ *p++) & 0xFFu] ^ (c >> 8);
    return ~c;
}

/* ===== 연습 1: CRC-32C 테이블 ===== */
static uint32_t TC[256];
static uint32_t crc32c_tab(uint32_t crc, const uint8_t *p, size_t n) {
    uint32_t c = ~crc;
    while (n--) c = TC[(c ^ *p++) & 0xFFu] ^ (c >> 8);
    return ~c;
}

/* ===== 연습 2: 니블 테이블 (16 엔트리 = 64 바이트) ===== */
static uint32_t TN[16];
static void make_nibble_table(void) {
    for (uint32_t i = 0; i < 16; i++) {
        uint32_t c = i;
        for (int k = 0; k < 4; k++) c = (c >> 1) ^ (POLY_R & (0u - (c & 1u)));
        TN[i] = c;
    }
}
static uint32_t crc32_nibble(uint32_t crc, const uint8_t *p, size_t n) {
    uint32_t c = ~crc;
    while (n--) {
        c ^= *p++;
        c = TN[c & 0xFu] ^ (c >> 4);        /* 하위 니블 먼저 (반사형 = LSB 먼저) */
        c = TN[c & 0xFu] ^ (c >> 4);
    }
    return ~c;
}

/* ===== 연습 3: 비반사 CRC-32 (MPEG-2: init ~0, xorout 0 / BZIP2: xorout ~0) ===== */
#define POLY_N 0x04C11DB7u
static uint32_t TM[256];
static void make_table_msb(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i << 24;
        for (int k = 0; k < 8; k++) c = (c & 0x80000000u) ? (c << 1) ^ POLY_N : c << 1;
        TM[i] = c;
    }
}
static uint32_t crc_msb_bitwise(uint32_t c, const uint8_t *p, size_t n) {   /* 레지스터 그대로 (조건화 없음) */
    while (n--) {
        c ^= (uint32_t)*p++ << 24;
        for (int k = 0; k < 8; k++) c = (c & 0x80000000u) ? (c << 1) ^ POLY_N : c << 1;
    }
    return c;
}
static uint32_t crc_msb_tab(uint32_t c, const uint8_t *p, size_t n) {
    while (n--) c = (c << 8) ^ TM[(c >> 24) ^ *p++];
    return c;
}
static uint32_t crc32_mpeg2(const uint8_t *p, size_t n) { return crc_msb_tab(0xFFFFFFFFu, p, n); }
static uint32_t crc32_bzip2(const uint8_t *p, size_t n) { return ~crc_msb_tab(0xFFFFFFFFu, p, n); }
/* STM32 식 하드웨어 CRC 유닛을 흉내: 32비트 워드를 MSB 부터 넣는다 = 워드의 BE 바이트 순서 (확인 권장: 칩별 설정) */
static uint32_t crc_words_msb(const uint32_t *w, size_t nw) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < nw; i++) {
        c ^= w[i];
        for (int k = 0; k < 4; k++) c = (c << 8) ^ TM[c >> 24];   /* 0 바이트 4번 = 워드 한 번 */
    }
    return c;
}

/* ===== 연습 4: 0xFF × n 의 CRC-32 를 O(log² n) ===== */
static uint32_t gf2_mulmod(uint32_t a, uint32_t b) {
    uint32_t r = 0;
    for (uint32_t m = 1u << 31; m; m >>= 1) {
        if (a & m) r ^= b;
        b = (b >> 1) ^ (POLY_R & (0u - (b & 1u)));
    }
    return r;
}
static uint32_t x_pow_8n(uint64_t n) {
    uint32_t r = 1u << 31, sq = 1u << 23;
    while (n) { if (n & 1u) r = gf2_mulmod(r, sq); sq = gf2_mulmod(sq, sq); n >>= 1; }
    return r;
}
static uint32_t combine(uint32_t c1, uint32_t c2, uint64_t len2) { return gf2_mulmod(x_pow_8n(len2), c1) ^ c2; }
static uint32_t crc32_ff_run(uint64_t n) {
    const uint8_t ff = 0xFF;
    uint32_t acc = 0, piece = crc32_tab(0, &ff, 1);   /* piece = crc(FF × len) */
    uint64_t len = 1;
    while (n) {
        if (n & 1u) acc = combine(acc, piece, len);
        piece = combine(piece, piece, len);
        len *= 2;
        n >>= 1;
    }
    return acc;
}

/* ===== 연습 5: 4바이트 덧붙여 CRC 를 target 으로 ===== */
static uint8_t TOPIDX[256];   /* T32[i] >> 24 → i (전단사임을 테스트로 확인) */
static bool make_top_index(void) {
    bool seen[256] = { false };
    for (int i = 0; i < 256; i++) {
        uint8_t top = (uint8_t)(T32[i] >> 24);
        if (seen[top]) return false;
        seen[top] = true;
        TOPIDX[top] = (uint8_t)i;
    }
    return true;
}
/* 0 바이트 테이블 스텝 R' → S = T[R' & 0xFF] ^ (R' >> 8) 의 역: S 의 최상위 바이트가 인덱스를 알려 준다 */
static uint32_t unstep(uint32_t s) {
    uint8_t i = TOPIDX[s >> 24];
    return ((s ^ T32[i]) << 8) | i;
}
static void crc32_forge4(const uint8_t *msg, size_t n, uint32_t target, uint8_t out[4]) {
    uint32_t r = ~crc32_tab(0, msg, n);        /* M 뒤의 레지스터 */
    uint32_t s = ~target;                      /* 4바이트 뒤 원하는 레지스터 */
    for (int k = 0; k < 4; k++) s = unstep(s); /* G^{-1}(s): 0 바이트 스텝 4번 되돌리기 */
    uint32_t w = r ^ s;                        /* G(r ^ w) = 원하는 레지스터 */
    out[0] = (uint8_t)w; out[1] = (uint8_t)(w >> 8); out[2] = (uint8_t)(w >> 16); out[3] = (uint8_t)(w >> 24);
}

int main(void) {
    static uint8_t buf[1 << 20];
    uint32_t seed = 0x5EED0002u;
    const uint8_t *chk = (const uint8_t *)"123456789";
    make_table_r(POLY_R, T32);

    /* ---- 연습 1 ---- */
    make_table_r(POLY_C_R, TC);
    CHECK(crc32c_tab(0, chk, 9) == 0xE3069283u);                  /* RevEng CRC-32/ISCSI check */
    CHECK(TC[1] == 0xF26B8303u);
    int c_bad = 0, hw_bad = 0, hw_n = 0;
    for (int t = 0; t < 1000; t++) {
        size_t n = xs32(&seed) % 600u;
        for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)xs32(&seed);
        uint32_t ref = crc_reflect_bitwise(POLY_C_R, 0, buf, n);
        c_bad += crc32c_tab(0, buf, n) != ref;
#if defined(__ARM_FEATURE_CRC32)
        uint32_t h = 0xFFFFFFFFu;                                   /* 명령은 조건화(~) 없이 레지스터만 갱신 */
        for (size_t i = 0; i < n; i++) h = __crc32cb(h, buf[i]);
        hw_bad += ~h != ref; hw_n++;
#endif
    }
    printf("ex1 crc32c: 1000 random vs bitwise mismatches=%d; ARMv8 __crc32cb checked=%d mismatches=%d\n", c_bad, hw_n, hw_bad);
    CHECK(c_bad == 0 && hw_bad == 0);

    /* ---- 연습 2 ---- */
    make_nibble_table();
    CHECK(crc32_nibble(0, chk, 9) == 0xCBF43926u);
    int nb_bad = 0;
    for (int t = 0; t < 1000; t++) {
        size_t n = xs32(&seed) % 600u;
        for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)xs32(&seed);
        nb_bad += crc32_nibble(0, buf, n) != crc_reflect_bitwise(POLY_R, 0, buf, n);
    }
    printf("ex2 nibble table (%zu bytes): 1000 random mismatches=%d\n", sizeof TN, nb_bad);
    CHECK(nb_bad == 0 && sizeof TN == 64);

    /* ---- 연습 3 ---- */
    make_table_msb();
    CHECK(crc32_mpeg2(chk, 9) == 0x0376E6E7u);                      /* RevEng CRC-32/MPEG-2 check */
    CHECK(crc32_bzip2(chk, 9) == 0xFC891918u);                      /* RevEng CRC-32/BZIP2 check */
    CHECK(TM[1] == POLY_N);
    int m_bad = 0, w_bad = 0;
    for (int t = 0; t < 1000; t++) {
        size_t nw = xs32(&seed) % 150u, n = 4 * nw;
        uint32_t w[150];
        for (size_t i = 0; i < nw; i++) {
            w[i] = xs32(&seed);
            buf[4 * i] = (uint8_t)(w[i] >> 24); buf[4 * i + 1] = (uint8_t)(w[i] >> 16);   /* BE 바이트로 펼침 */
            buf[4 * i + 2] = (uint8_t)(w[i] >> 8); buf[4 * i + 3] = (uint8_t)w[i];
        }
        m_bad += crc_msb_tab(0xFFFFFFFFu, buf, n) != crc_msb_bitwise(0xFFFFFFFFu, buf, n);
        w_bad += crc_words_msb(w, nw) != crc32_mpeg2(buf, n);
    }
    printf("ex3 msb-first: table vs bitwise mismatches=%d, word-feed vs BE bytes mismatches=%d\n", m_bad, w_bad);
    CHECK(m_bad == 0 && w_bad == 0);

    /* ---- 연습 4 ---- */
    memset(buf, 0xFF, sizeof buf);
    int ff_bad = 0;
    for (uint64_t n = 0; n <= 3000; n++) ff_bad += crc32_ff_run(n) != crc32_tab(0, buf, (size_t)n);
    const uint64_t big[] = { 4096, 65535, 65536, 131072 + 7, (uint64_t)1 << 20 };
    for (size_t j = 0; j < sizeof big / sizeof big[0]; j++)
        ff_bad += crc32_ff_run(big[j]) != crc32_tab(0, buf, (size_t)big[j]);
    printf("ex4 crc of 0xFF x n: n=0..3000 and 5 big sizes up to 1 MiB, mismatches=%d\n", ff_bad);
    CHECK(ff_bad == 0);
    /* 이미지 + 지워진 패딩: crc(img || FF^pad) = combine(crc(img), crc(FF^pad), pad) */
    for (size_t i = 0; i < 1000; i++) buf[i] = (uint8_t)xs32(&seed);
    CHECK(combine(crc32_tab(0, buf, 1000), crc32_ff_run(8192 - 1000), 8192 - 1000) == crc32_tab(0, buf, 8192));

    /* ---- 연습 5 ---- */
    CHECK(make_top_index());
    int forge_bad = 0;
    for (int t = 0; t < 2000; t++) {
        size_t n = xs32(&seed) % 300u;
        for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)xs32(&seed);
        uint32_t target = (t == 0) ? 0xCBF43926u : (t == 1) ? 0u : xs32(&seed);
        crc32_forge4(buf, n, target, buf + n);
        forge_bad += crc_reflect_bitwise(POLY_R, 0, buf, n + 4) != target;
    }
    printf("ex5 forge 4 bytes: 2000 random (msg, target) pairs, misses=%d\n", forge_bad);
    CHECK(forge_bad == 0);
    {   /* 예: "123456789" 뒤에 4바이트를 붙여 CRC = 0xDEADBEEF */
        uint8_t m[13];
        memcpy(m, chk, 9);
        crc32_forge4(m, 9, 0xDEADBEEFu, m + 9);
        printf("ex5 example: \"123456789\" + %02X %02X %02X %02X -> crc32 = %08X\n",
               m[9], m[10], m[11], m[12], crc32_tab(0, m, 13));
        CHECK(crc32_tab(0, m, 13) == 0xDEADBEEFu);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
