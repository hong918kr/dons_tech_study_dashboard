/* ARM-A06 기준 구현 (소프트웨어 CRC) — 모든 빌드(starter/solution/practice/demo)에 함께 링크된다.
 *  - ref_bitwise : 한 비트씩 (ALGO-M01 비트 단위 CRC 축약판, 반사형만)
 *  - ref_table   : 256 엔트리 바이트 테이블 (ALGO-M02 Sarwate 축약판)
 *  - ref_slice8  : slicing-by-8 (ALGO-M02 축약판, 8 KiB 테이블)
 *  - gf2_mulmod / xpow_mod : GF(2)[x]/P 곱셈과 x^e mod P (ALGO-M02 combine 축약판, 다항식을 인자로 일반화)
 *  - ref_mpeg2_bitwise : 비반사(MSB-first) CRC-32/MPEG-2 — STM32 CRC 유닛 기본 설정과 같은 계산 (Part C 기준)
 * API 는 zlib 모양: crc = f(crc, p, n), 처음 crc = 0. 반사형 다항식 상수 poly_r 를 인자로 받는다.
 *   CRC-32 (IEEE, zlib/Ethernet) : poly_r = 0xEDB88320 (= 0x04C11DB7 비트 반전)
 *   CRC-32C (Castagnoli, iSCSI)  : poly_r = 0x82F63B78 (= 0x1EDC6F41 비트 반전)
 * 반사 표현: 32비트 값의 비트 k 가 x^(31-k) 의 계수다 (비트 31 = x^0, 비트 0 = x^31).
 */
#include <stddef.h>
#include <stdint.h>

/* ---------------- 비트 단위 (ALGO-M01 축약판) ---------------- */
uint32_t ref_bitwise(uint32_t crc, const void *buf, size_t n, uint32_t poly_r) {
    const uint8_t *p = buf;
    uint32_t c = ~crc;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (poly_r & (0u - (c & 1u)));   /* c&1 이면 poly XOR, 분기 없음 */
    }
    return ~c;
}

/* ---------------- 테이블 / slicing-by-8 (ALGO-M02 축약판) ---------------- */
static uint32_t T_ieee[8][256], T_c[8][256];

static void make_tables(uint32_t t[8][256], uint32_t poly_r) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (poly_r & (0u - (c & 1u)));
        t[0][i] = c;
    }
    for (int k = 1; k < 8; k++)
        for (int i = 0; i < 256; i++) t[k][i] = (t[k - 1][i] >> 8) ^ t[0][t[k - 1][i] & 0xFFu];
}

void ref_init(void) {
    make_tables(T_ieee, 0xEDB88320u);
    make_tables(T_c, 0x82F63B78u);
}

static uint32_t (*pick(uint32_t poly_r))[256] { return poly_r == 0x82F63B78u ? T_c : T_ieee; }

uint32_t ref_table(uint32_t crc, const void *buf, size_t n, uint32_t poly_r) {
    uint32_t (*T)[256] = pick(poly_r);
    const uint8_t *p = buf;
    uint32_t c = ~crc;
    while (n--) c = T[0][(c ^ *p++) & 0xFFu] ^ (c >> 8);
    return ~c;
}

static inline uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

uint32_t ref_slice8(uint32_t crc, const void *buf, size_t n, uint32_t poly_r) {
    uint32_t (*T)[256] = pick(poly_r);
    const uint8_t *p = buf;
    uint32_t c = ~crc;
    for (; n >= 8; p += 8, n -= 8) {
        uint32_t lo = c ^ le32(p), hi = le32(p + 4);
        c = T[7][lo & 0xFFu] ^ T[6][(lo >> 8) & 0xFFu] ^ T[5][(lo >> 16) & 0xFFu] ^ T[4][lo >> 24]
          ^ T[3][hi & 0xFFu] ^ T[2][(hi >> 8) & 0xFFu] ^ T[1][(hi >> 16) & 0xFFu] ^ T[0][hi >> 24];
    }
    while (n--) c = T[0][(c ^ *p++) & 0xFFu] ^ (c >> 8);
    return ~c;
}

/* ---------------- GF(2)[x]/P 산술 (ALGO-M02 combine 축약판) ---------------- */
/* a·b mod P, 반사 표현. a 의 x^0, x^1, ... 항(비트 31, 30, ...)을 차례로 보며 b 를 x 배씩 키운다. */
uint32_t gf2_mulmod(uint32_t a, uint32_t b, uint32_t poly_r) {
    uint32_t r = 0;
    for (uint32_t m = 1u << 31; m; m >>= 1) {
        if (a & m) r ^= b;
        b = (b >> 1) ^ (poly_r & (0u - (b & 1u)));      /* b ← b·x mod P */
    }
    return r;
}

/* x^e mod P (제곱-곱셈, O(log e) 번의 gf2_mulmod) */
uint32_t xpow_mod(uint64_t e, uint32_t poly_r) {
    uint32_t r = 1u << 31;      /* x^0 */
    uint32_t sq = 1u << 30;     /* x^1 */
    while (e) {
        if (e & 1u) r = gf2_mulmod(r, sq, poly_r);
        sq = gf2_mulmod(sq, sq, poly_r);
        e >>= 1;
    }
    return r;
}

/* ---------------- 비반사 CRC-32/MPEG-2 (STM32 CRC 유닛 기본값) ---------------- */
/* poly 0x04C11DB7 (normal), init 0xFFFFFFFF, MSB 먼저, 출력 반사·xorout 없음. check("123456789") = 0x0376E6E7 */
uint32_t ref_mpeg2_bitwise(uint32_t state, const void *buf, size_t n) {
    const uint8_t *p = buf;
    uint32_t c = state;
    while (n--) {
        c ^= (uint32_t)*p++ << 24;
        for (int k = 0; k < 8; k++)
            c = (c << 1) ^ (0x04C11DB7u & (0u - (c >> 31)));
    }
    return c;
}
