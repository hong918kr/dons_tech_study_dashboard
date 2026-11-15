/* ALGO-M02 CRC 가속: 테이블 방식과 CRC-32 Slicing
 *
 * CRC-32/ISO-HDLC (zlib, Ethernet, PNG, gzip): poly 0x04C11DB7, 반사(reflected) → 0xEDB88320,
 * init 0xFFFFFFFF, refin/refout = true, xorout 0xFFFFFFFF, check("123456789") = 0xCBF43926.
 * API 는 zlib 과 같은 모양: crc = f(crc, p, n), 처음엔 crc = 0, 이어서 부르면 스트리밍.
 *
 * Part A: 비트 단위(기준) → 256엔트리 테이블 생성기 + 바이트 단위 테이블 CRC
 * Part B: slicing-by-4 / slicing-by-8 테이블 생성 + 처리, GF(2) 곱셈으로 crc32_combine
 * Part C: 펌웨어 이미지 봉인/검사(잔여값 0x2144DF1C), 여러 DMA 블록 병렬 CRC → combine 으로 합치기
 *
 * 테스트(테스트 쪽에서만 zlib 사용): zlib crc32()/crc32_combine()/get_crc_table() 대조, 카탈로그 check 값,
 *        비트 단위 기준과 xorshift32 고정 시드 랜덤 대조, 작은 길이 × 정렬 오프셋 전수, 청크 분할 스트리밍.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -lz -o solution && ./solution
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

#define CRC32_POLY_R  0xEDB88320u   /* 0x04C11DB7 을 비트 순서 반전 (x^0 이 비트 31 쪽) */
#define CRC32_RESIDUE 0x2144DF1Cu   /* CRC 를 LE 로 붙인 메시지 전체의 crc32 (RevEng residue 0xDEBB20E3 의 ~) */

static uint32_t T[8][256];          /* T[0] = 바이트 테이블(1 KiB), T[1..7] = slicing 용 (총 8 KiB) */

/* ---------------- Part A: 비트 단위 → 바이트 테이블 ---------------- */

/* 기준 구현: 한 비트씩. 반사형이라 LSB 부터 들어가고 오른쪽으로 민다. */
static uint32_t crc32_bitwise(uint32_t crc, const uint8_t *p, size_t n) {
    (void)crc; (void)p; (void)n;
    return 0; /* TODO: c = ~crc; 바이트마다 c ^= b, 8번 (c >> 1) ^ (c&1 ? POLY_R : 0); return ~c */
}

/* 테이블 생성기: t[i] = 레지스터 하위 바이트가 i, 나머지 0 일 때 8비트 시프트 후의 값 */
static void crc32_make_table(uint32_t t[256]) {
    (void)t; /* TODO: t[i] = 하위 바이트 i 인 레지스터를 8비트 시프트(비트 단위 스텝 8번)한 값 */
}

/* 바이트 단위 테이블 CRC (Sarwate): 바이트당 XOR 2 + 시프트 1 + 로드 1 */
static uint32_t crc32_table(uint32_t crc, const uint8_t *p, size_t n) {
    (void)crc; (void)p; (void)n;
    return 0; /* TODO: 바이트마다 c = T[0][(c ^ b) & 0xFF] ^ (c >> 8) */
}

/* ---------------- Part B: slicing-by-4/8, combine ---------------- */

/* T[k][i] = 바이트 i 를 넣고 0 바이트 k 개를 더 넣은 효과. T[0] 이 이미 채워져 있어야 한다. */
static void crc32_make_slice_tables(uint32_t t[8][256]) {
    (void)t; /* TODO: t[k][i] = t[k-1][i] 에 0 바이트 한 번 더 (테이블 스텝 1번) */
}

/* 정렬·엔디언과 무관한 LE 32비트 읽기 (arm64 -O2 에서는 ldr 하나로 합쳐진다) */
static inline uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t crc32_slice4(uint32_t crc, const uint8_t *p, size_t n) {
    (void)crc; (void)p; (void)n;
    return 0; /* TODO: 4바이트씩 c ^= le32(p) → T[3..0] 4번 조회 XOR, 꼬리는 바이트 단위 */
}

static uint32_t crc32_slice8(uint32_t crc, const uint8_t *p, size_t n) {
    (void)crc; (void)p; (void)n; (void)le32;
    return 0; /* TODO: 8바이트씩 lo = c ^ le32(p), hi = le32(p+4) → T[7..0] 8번 조회 XOR, 꼬리는 바이트 단위 */
}

/* GF(2)[x] / P 에서 a·b (반사 표현: 비트 31 = x^0, 비트 0 = x^31) */
static uint32_t gf2_mulmod(uint32_t a, uint32_t b) {
    (void)a; (void)b;
    return 0; /* TODO: a 의 x^j 항(비트 31-j)마다 r ^= b·x^j; b ← b·x 는 비트 단위 CRC 스텝과 같다 */
}

/* x^(8n) mod P — 제곱-곱셈, O(log n) 번의 gf2_mulmod */
static uint32_t x_pow_8n(uint64_t n) {
    (void)n;
    return 1u << 31; /* TODO: r = 1, sq = x^8 (비트 23) 에서 제곱-곱셈 */
}

/* crc(A||B) = crc(A)·x^(8·|B|) ⊕ crc(B)   (init = xorout = ~0 이라 상수항이 상쇄된다) */
static uint32_t crc32_combine_m(uint32_t crc1, uint32_t crc2, uint64_t len2) {
    (void)crc1; (void)len2;
    return crc2; /* TODO: crc1·x^(8·len2) mod P ⊕ crc2 */
}

/* ---------------- Part C: 펌웨어 이미지 · 병렬 DMA 블록 ---------------- */

/* img[0..len) 의 CRC 를 img[len..len+4) 에 LE 로 붙인다 (버퍼는 len + 4 바이트 이상) */
static void fw_image_seal(uint8_t *img, size_t len) {
    (void)img; (void)len; /* TODO: crc 를 img[len..len+3] 에 LE 로 */
}

/* 봉인된 이미지(데이터 + LE CRC 4바이트, 총 n 바이트) 검사: 전체 CRC 가 잔여값이면 OK */
static bool fw_image_ok(const uint8_t *img, size_t n) {
    (void)img; (void)n;
    return false; /* TODO: 전체(데이터 + CRC) 의 crc 가 CRC32_RESIDUE 인가 (n < 4 는 거짓) */
}

/* n 바이트를 parts 개의 연속 블록으로 나눠 따로 CRC(= DMA 채널/코어별) → combine 으로 합친다 */
static uint32_t crc32_parallel(const uint8_t *p, size_t n, unsigned parts) {
    (void)p; (void)n; (void)parts;
    return 0; /* TODO: 블록 크기 ceil(n/parts), 블록마다 crc32_slice8(0, ...) → acc = combine(acc, c, len) */
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

static uint32_t zcrc(uint32_t crc, const uint8_t *p, size_t n) { return (uint32_t)crc32(crc, p, (uInt)n); }

/* 정의 그대로의 기준: x^k mod P 를 x 를 k 번 곱해 구한다 (제곱-곱셈과 다른 길) */
static uint32_t ref_x_pow(uint64_t k) {
    uint32_t r = 1u << 31;
    while (k--) r = (r >> 1) ^ (CRC32_POLY_R & (0u - (r & 1u)));
    return r;
}

typedef uint32_t (*crc_fn)(uint32_t, const uint8_t *, size_t);

int main(void) {
    static uint8_t buf[70000 + 16];
    uint32_t seed = 0xC0FFEE02u;
    const uint8_t *chk = (const uint8_t *)"123456789";
    const crc_fn F[4] = { crc32_bitwise, crc32_table, crc32_slice4, crc32_slice8 };
    const char *FN[4] = { "bitwise", "table", "slice4", "slice8" };

    /* ===== Part A: 기준 + 테이블 ===== */
    CHECK(crc32_bitwise(0, chk, 9) == 0xCBF43926u);          /* RevEng CRC-32/ISO-HDLC check */
    CHECK(crc32_bitwise(0, chk, 0) == 0u);                   /* 빈 입력: ~(~0) = 0 */
    CHECK(crc32_bitwise(0, (const uint8_t *)"a", 1) == 0xE8B7BE43u);
    CHECK(crc32_bitwise(0, (const uint8_t *)"The quick brown fox jumps over the lazy dog", 43) == 0x414FA339u);

    crc32_make_table(T[0]);
    const z_crc_t *zt = get_crc_table();
    int tab_bad = 0;
    for (int i = 0; i < 256; i++) tab_bad += T[0][i] != (uint32_t)zt[i];
    printf("table vs zlib get_crc_table(): %d/256 entries differ\n", tab_bad);
    CHECK(tab_bad == 0);
    CHECK(T[0][0] == 0u && T[0][1] == 0x77073096u && T[0][128] == CRC32_POLY_R && T[0][255] == 0x2D02EF8Du);

    crc32_make_slice_tables(T);
    /* T[k][i] 정의: 하위 바이트 i 인 레지스터에 바이트 스텝 1번 + 0 바이트 스텝 k 번 (비트 단위로 직접) */
    int slice_bad = 0;
    for (int k = 0; k < 8; k++)
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int s = 0; s < 8 * (k + 1); s++) c = (c >> 1) ^ (CRC32_POLY_R & (0u - (c & 1u)));
            slice_bad += T[k][i] != c;
        }
    printf("slice tables T[0..7] vs bit-by-bit definition: %d/2048 entries differ\n", slice_bad);
    CHECK(slice_bad == 0);

    for (int f = 0; f < 4; f++) CHECK(F[f](0, chk, 9) == 0xCBF43926u);

    /* 1바이트·2바이트 메시지 전수 (256 + 65536) : 비트 단위·테이블 vs zlib */
    int small_bad = 0;
    for (uint32_t v = 0; v < 65536; v++) {
        uint8_t m[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
        uint32_t z2 = zcrc(0, m, 2);
        small_bad += (crc32_table(0, m, 2) != z2) + (crc32_bitwise(0, m, 2) != z2);
        if (v < 256) small_bad += crc32_table(0, m, 1) != zcrc(0, m, 1);
    }
    printf("all 1- and 2-byte messages: bitwise/table vs zlib mismatches=%d\n", small_bad);
    CHECK(small_bad == 0);

    /* ===== Part B: slicing — 길이 0..72 × 시작 오프셋 0..7 전수, 이어서 랜덤 ===== */
    for (int i = 0; i < 128; i++) buf[i] = (uint8_t)xs32(&seed);
    int edge_bad[4] = { 0 };
    for (size_t off = 0; off < 8; off++)
        for (size_t n = 0; n <= 72; n++) {
            uint32_t ref = zcrc(0, buf + off, n);
            for (int f = 1; f < 4; f++) edge_bad[f] += F[f](0, buf + off, n) != ref;
        }
    printf("len 0..72 x offset 0..7 (584 cases): mismatches table=%d slice4=%d slice8=%d\n",
           edge_bad[1], edge_bad[2], edge_bad[3]);
    CHECK(edge_bad[1] == 0 && edge_bad[2] == 0 && edge_bad[3] == 0);

    int rnd_bad[4] = { 0 };
    for (int t = 0; t < 3000; t++) {
        size_t n = xs32(&seed) % 4097u, off = xs32(&seed) % 8u;
        uint32_t init = (t % 3 == 0) ? 0u : xs32(&seed);          /* 이어 부르기(임의 이전 crc)도 */
        for (size_t i = 0; i < n; i++) buf[off + i] = (uint8_t)xs32(&seed);
        if (t % 50 == 0) memset(buf + off, t % 100 ? 0xFF : 0x00, n);   /* 전부 0x00 / 0xFF */
        uint32_t ref = zcrc(init, buf + off, n);
        for (int f = 0; f < 4; f++) rnd_bad[f] += F[f](init, buf + off, n) != ref;
    }
    printf("random 3000 msgs (len 0..4096, offset 0..7) vs zlib crc32(): bitwise=%d table=%d slice4=%d slice8=%d\n",
           rnd_bad[0], rnd_bad[1], rnd_bad[2], rnd_bad[3]);
    for (int f = 0; f < 4; f++) { if (rnd_bad[f]) printf("  (%s)\n", FN[f]); CHECK(rnd_bad[f] == 0); }

    /* 큰 버퍼 1번 (70000 바이트) */
    for (size_t i = 0; i < 70000; i++) buf[i] = (uint8_t)xs32(&seed);
    CHECK(crc32_slice8(0, buf, 70000) == zcrc(0, buf, 70000));
    CHECK(crc32_slice4(0, buf, 70000) == zcrc(0, buf, 70000));

    /* 스트리밍: 임의 크기 청크로 이어 부른 결과 == 한 번에 */
    int stream_bad = 0;
    for (int t = 0; t < 500; t++) {
        size_t n = xs32(&seed) % 5000u, pos = 0;
        uint32_t c4 = 0, c8 = 0, ct = 0;
        while (pos < n) {
            size_t ch = 1 + xs32(&seed) % 300u;
            if (ch > n - pos) ch = n - pos;
            c4 = crc32_slice4(c4, buf + pos, ch); c8 = crc32_slice8(c8, buf + pos, ch);
            ct = crc32_table(ct, buf + pos, ch);
            pos += ch;
        }
        uint32_t one = zcrc(0, buf, n);
        stream_bad += (c4 != one) + (c8 != one) + (ct != one);
    }
    printf("streaming in random chunks (1..300 B), 500 msgs: mismatches=%d\n", stream_bad);
    CHECK(stream_bad == 0);

    /* x^(8n) mod P: 제곱-곱셈 vs x 를 8n 번 곱하기 */
    int pow_bad = 0;
    for (uint64_t n = 0; n <= 600; n++) pow_bad += x_pow_8n(n) != ref_x_pow(8 * n);
    CHECK(pow_bad == 0);
    CHECK(gf2_mulmod(1u << 31, 0x12345678u) == 0x12345678u);    /* 1·b = b */
    CHECK(gf2_mulmod(0x9ABCDEF0u, 0x12345678u) == gf2_mulmod(0x12345678u, 0x9ABCDEF0u));   /* 교환 */

    /* combine: zlib crc32_combine 및 한 번에 계산한 값과 대조 */
    int comb_bad = 0;
    for (int t = 0; t < 2000; t++) {
        size_t n = xs32(&seed) % 6000u, k = n ? xs32(&seed) % (n + 1) : 0;
        uint32_t c1 = zcrc(0, buf, k), c2 = zcrc(0, buf + k, n - k);
        uint32_t m = crc32_combine_m(c1, c2, n - k);
        comb_bad += (m != zcrc(0, buf, n)) + (m != (uint32_t)crc32_combine(c1, c2, (z_off_t)(n - k)));
    }
    printf("combine: 2000 random splits vs one-shot and zlib crc32_combine(): mismatches=%d\n", comb_bad);
    CHECK(comb_bad == 0);
    CHECK(crc32_combine_m(0xCBF43926u, 0u, 0) == 0xCBF43926u);           /* 빈 B */
    CHECK(crc32_combine_m(0u, zcrc(0, chk, 9), 9) == 0xCBF43926u);       /* 빈 A */

    /* ===== Part C: 펌웨어 이미지 봉인/검사, 병렬 블록 ===== */
    uint8_t img[64 + 4];
    memcpy(img, chk, 9);
    fw_image_seal(img, 9);
    CHECK(img[9] == 0x26 && img[10] == 0x39 && img[11] == 0xF4 && img[12] == 0xCB);   /* LE */
    CHECK(fw_image_ok(img, 13));
    img[3] ^= 0x10;
    CHECK(!fw_image_ok(img, 13));
    CHECK(!fw_image_ok(img, 3));                                          /* 너무 짧음 */
    fw_image_seal(img, 0);
    CHECK(fw_image_ok(img, 4));                                           /* 빈 페이로드 + CRC */

    int img_bad = 0, img_miss = 0;
    for (int t = 0; t < 1000; t++) {
        size_t n = xs32(&seed) % 4000u;
        for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)xs32(&seed);
        fw_image_seal(buf, n);
        img_bad += !fw_image_ok(buf, n + 4);
        size_t bit = xs32(&seed) % (8 * (n + 4));                       /* 1비트 오류는 반드시 검출 */
        buf[bit / 8] ^= (uint8_t)(1u << (bit % 8));
        img_miss += fw_image_ok(buf, n + 4);
    }
    printf("fw image: 1000 sealed images ok-fail=%d, single-bit flips missed=%d\n", img_bad, img_miss);
    CHECK(img_bad == 0 && img_miss == 0);

    for (size_t i = 0; i < 70000; i++) buf[i] = (uint8_t)xs32(&seed);
    int par_bad = 0;
    const unsigned P[] = { 0, 1, 2, 3, 4, 7, 8, 16 };
    for (int t = 0; t < 200; t++) {
        size_t n = (t < 8) ? (size_t)t : xs32(&seed) % 70001u;
        uint32_t one = zcrc(0, buf, n);
        for (size_t j = 0; j < sizeof P / sizeof P[0]; j++) par_bad += crc32_parallel(buf, n, P[j]) != one;
    }
    printf("parallel blocks (parts 0,1,2,3,4,7,8,16) x 200 lengths: mismatches=%d\n", par_bad);
    CHECK(par_bad == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
