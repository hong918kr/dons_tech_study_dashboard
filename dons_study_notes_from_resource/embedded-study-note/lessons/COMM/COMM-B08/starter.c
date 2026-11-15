/* COMM-B08 오류 검출 능력 실험: 체크섬 vs CRC
 *
 * Part A: 파라미터형 비트 단위 CRC 엔진 (CRC-8/SMBUS, CRC-16/CCITT-FALSE, CRC-32/ISO-HDLC)
 *         + sum8 · Fletcher-16 (ALGO-B14 축약판), 코드워드 = data || check 인코더(용량 검사)와 수신 측 잔차(residual)
 * Part B: 와이어 비트 순서대로 k비트 · 버스트 오류 주입 → 미검출 횟수 측정,
 *         CRC 신드롬 표 + 2·3비트 전수 탐색으로 경험적 해밍 거리(HD)
 * Part C: 버스 비트 순서(UART LSB-first vs SPI/I2C MSB-first)와 CRC 반사의 궁합 — 버스트 전수 검사,
 *         요구사항(프레임 길이, HD, 최대 버스트) → 가장 짧은 검출 코드 고르기
 *
 * 수백만 회 실험은 demo_detect.c, HD-길이 경계는 demo_hd.c. 여기는 빠른 성질 검사만 (< 2 s).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 공통: 알고리즘 목록 ---------------- */
typedef enum { ALG_SUM8, ALG_FL16, ALG_CRC8, ALG_CRC16, ALG_CRC32, ALG_COUNT } alg_t;
typedef enum { ORD_MSB, ORD_LSB } order_t;   /* 와이어에 비트가 나가는 순서 (바이트 안) */

static const char *ALG_NAME[ALG_COUNT] = { "sum8", "fletcher16", "crc8", "crc16", "crc32" };
static const unsigned CHECK_BYTES[ALG_COUNT] = { 1, 2, 1, 2, 4 };

/* Rocksoft/RevEng 모델: width, poly(정규형, MSB-first), init, refin, refout, xorout */
typedef struct { unsigned width; uint32_t poly, init; bool refin, refout; uint32_t xorout; } crc_param;
static const crc_param CRC8_SMBUS        = {  8, 0x07u,       0x00u,       false, false, 0x00u };       /* check 0xF4 */
static const crc_param CRC16_CCITT_FALSE = { 16, 0x1021u,     0xFFFFu,     false, false, 0x0000u };     /* check 0x29B1 */
static const crc_param CRC32_ISO_HDLC    = { 32, 0x04C11DB7u, 0xFFFFFFFFu, true,  true,  0xFFFFFFFFu }; /* check 0xCBF43926 */

static uint32_t reflect(uint32_t v, unsigned w) {          /* 하위 w 비트를 뒤집는다 */
    uint32_t r = 0;
    for (unsigned i = 0; i < w; i++) { r = (r << 1) | (v & 1u); v >>= 1; }
    return r;
}

/* ALGO-B14 축약판: 바이트 합 mod 256, Fletcher-16 (s2 << 8 | s1, mod 255) */
static uint32_t sum8(const uint8_t *p, size_t n) {
    uint32_t s = 0;
    for (size_t i = 0; i < n; i++) s += p[i];
    return s & 0xFFu;
}
static uint32_t fletcher16(const uint8_t *p, size_t n) {
    uint32_t s1 = 0, s2 = 0;
    for (size_t i = 0; i < n; i++) { s1 = (s1 + p[i]) % 255u; s2 = (s2 + s1) % 255u; }
    return s2 << 8 | s1;
}

/* ---------------- Part A: CRC 엔진 · 코드워드 ---------------- */

/* 비트 단위 CRC (자세한 원리는 ALGO-M01). width 는 8..32. */
static uint32_t crc_compute(const crc_param *p, const uint8_t *d, size_t n) {
    (void)p; (void)d; (void)n;
    return 0; /* TODO: init → 바이트마다 (refin 이면 reflect) 상위에 XOR → 8번 shift/XOR poly → refout → xorout */
}

static uint32_t check_value(alg_t a, const uint8_t *d, size_t n) {
    switch (a) {
    case ALG_SUM8:  return sum8(d, n);
    case ALG_FL16:  return fletcher16(d, n);
    case ALG_CRC8:  return crc_compute(&CRC8_SMBUS, d, n);
    case ALG_CRC16: return crc_compute(&CRC16_CCITT_FALSE, d, n);
    default:        return crc_compute(&CRC32_ISO_HDLC, d, n);
    }
}

/* 검사값을 붙이는 바이트 순서: 반사형(CRC-32)은 리틀 엔디언, 나머지는 빅 엔디언.
 * → 다항식의 비트가 와이어에서 끊김 없이 이어진다 (Part C 에서 다시). */
static bool check_little_endian(alg_t a) { return a == ALG_CRC32; }

/* data[0..n) || check 를 out 에 쓴다. 반환: 코드워드 길이, 용량 부족이면 -1 (out 는 건드리지 않음).
 * out == data 여도 된다 (제자리 인코딩). */
static long cw_encode(alg_t a, const uint8_t *data, size_t n, uint8_t *out, size_t cap) {
    (void)a; (void)data; (void)n; (void)out; (void)cap; (void)check_little_endian;
    return -1; /* TODO: 용량 검사 → memmove → check_value 를 CHECK_BYTES[a] 바이트로 (CRC-32 만 LE) */
}

/* 수신 측: (데이터로 다시 계산한 검사값) XOR (받은 검사값). 0 이면 "정상"으로 판정.
 * len < 검사 바이트 수면 0xFFFFFFFF (항상 실패). */
static uint32_t cw_residual(alg_t a, const uint8_t *cw, size_t len) {
    (void)a; (void)cw; (void)len;
    return 0; /* TODO: 받은 검사값을 같은 엔디언으로 읽어 check_value(data) 와 XOR (len < 검사 바이트면 0xFFFFFFFF) */
}

static bool cw_verify(alg_t a, const uint8_t *cw, size_t len) { return cw_residual(a, cw, len) == 0; }

/* ---------------- Part B: 오류 주입 · 측정 · 신드롬 ---------------- */
#define MAX_DATA 64
#define MAX_CW   (MAX_DATA + 4)
#define MAX_BITS (MAX_CW * 8)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* 와이어 비트 i (0 = 가장 먼저 나가는 비트) 를 뒤집는다. MSB-first: 바이트의 bit7 먼저 (SPI, I2C).
 * LSB-first: bit0 먼저 (UART, Ethernet). */
static void flip_wire(uint8_t *buf, unsigned i, order_t o) {
    unsigned k = i & 7u;
    buf[i >> 3] ^= (uint8_t)(o == ORD_MSB ? 0x80u >> k : 1u << k);
}

/* 서로 다른 와이어 비트 k 개를 뒤집는다 (1 <= k <= 8, k <= nbits). */
static void inject_bits(uint8_t *buf, unsigned nbits, unsigned k, order_t o, uint32_t *rng) {
    (void)buf; (void)nbits; (void)k; (void)o; (void)rng;
    /* TODO: 중복 없는 위치 k 개를 뽑아 flip_wire */
}

/* 길이 L 버스트: 시작 s 는 [0, nbits-L] 균등, 첫 비트와 마지막 비트는 반드시 뒤집고 사이 L-2 비트는 동전 던지기.
 * L = 1 이면 1비트 오류. (1 <= L <= nbits) */
static void inject_burst(uint8_t *buf, unsigned nbits, unsigned L, order_t o, uint32_t *rng) {
    (void)buf; (void)nbits; (void)L; (void)o; (void)rng;
    /* TODO: 시작 s ∈ [0, nbits-L], 양 끝 비트 반드시, 사이는 xs32 & 1 */
}

typedef enum { ERR_BITS, ERR_BURST } err_kind;

/* trials 번: 랜덤 데이터(ndata 바이트) → 인코드 → 오류 주입 → 검증. "통과했는데 바뀐" 횟수를 센다. */
static uint32_t count_undetected(alg_t a, size_t ndata, err_kind kind, unsigned param, order_t o,
                                 uint32_t trials, uint32_t seed) {
    (void)a; (void)ndata; (void)kind; (void)param; (void)o; (void)seed;
    return trials; /* TODO: 랜덤 데이터 → cw_encode → 복사본에 주입 → 바뀌었는데 cw_verify 통과면 miss++ */
}

/* CRC 전용: 와이어 비트 i 하나만 뒤집었을 때의 잔차 = 신드롬. CRC 는 GF(2) 위에서 선형이라
 * "오류 e 가 미검출" ⇔ "e 가 뒤집은 비트들의 신드롬 XOR == 0" (데이터와 무관!).
 * 반환: 코드워드 비트 수, 실패 시 -1 (체크섬이거나 용량 부족). */
static int crc_syndromes(alg_t a, size_t ndata, order_t o, uint32_t *syn, size_t cap) {
    (void)a; (void)ndata; (void)o; (void)syn; (void)cap;
    return -1; /* TODO: 0 데이터 코드워드에서 비트 i 하나씩 뒤집어 cw_residual 을 syn[i] 에 */
}

/* 가장 가벼운 미검출 오류의 비트 수(1..maxk)를 전수 탐색, 없으면 maxk+1.  maxk <= 3.
 * = 경험적 해밍 거리 (HD > maxk 이면 maxk+1 을 돌려준다). */
static int min_undetected_weight(const uint32_t *syn, int nbits, int maxk) {
    (void)syn; (void)nbits; (void)maxk;
    return 0; /* TODO: 1비트(syn==0) → 2비트(같은 값) → 3비트(syn[i]^syn[j]==syn[k]) 순서로 전수 */
}

/* ---------------- Part C: 비트 순서 · 버스트 전수 · 코드 선택 ---------------- */

/* 길이 L 버스트 (양 끝 1, 사이 L-2 비트 모든 조합) 를 모든 시작 위치에서 전수: 미검출 개수.
 * 그레이 코드로 사이 비트를 하나씩 토글 → 패턴당 XOR 1번. 비용 = (nbits-L+1) · 2^(L-2). L <= 24. */
static uint64_t burst_undetected_exhaustive(const uint32_t *syn, int nbits, int L) {
    (void)syn; (void)nbits; (void)L;
    return UINT64_MAX; /* TODO: 시작 위치마다 양 끝 신드롬 XOR, 사이 L-2 비트는 그레이 코드로 토글하며 0 세기 */
}

/* 요구사항을 만족하는 가장 짧은 CRC: ndata 바이트 프레임에서 (need_hd-1) 비트 이하 오류 전부,
 * 와이어 길이 burst_max 이하 버스트 전부 검출. 2 <= need_hd <= 4, 1 <= burst_max <= 20.
 * 체크섬은 보장이 데이터에 따라 달라 신드롬으로 증명할 수 없으므로 후보에서 뺀다. 없으면 ALG_COUNT. */
static alg_t choose_check(size_t ndata, int need_hd, int burst_max, order_t o) {
    (void)ndata; (void)need_hd; (void)burst_max; (void)o;
    return ALG_COUNT; /* TODO: CRC8 → CRC16 → CRC32 순서로 신드롬 → HD 전수 → 버스트 전수, 처음 통과한 것 */
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* 기준 구현: 다항식을 "비트 배열"로 두고 긴 나눗셈 — 레지스터 트릭 없이 정의 그대로 (느리지만 다른 길) */
static uint32_t ref_crc(const crc_param *p, const uint8_t *d, size_t n) {
    uint8_t bits[(MAX_DATA + 8) * 8];
    size_t nb = 0;
    for (size_t i = 0; i < n; i++)
        for (int k = 0; k < 8; k++) bits[nb++] = p->refin ? (d[i] >> k) & 1u : (d[i] >> (7 - k)) & 1u;
    for (unsigned k = 0; k < p->width; k++) bits[nb++] = 0;            /* × x^width */
    for (unsigned k = 0; k < p->width && k < n * 8; k++)               /* init 은 첫 width 비트에 XOR */
        bits[k] ^= (uint8_t)((p->init >> (p->width - 1 - k)) & 1u);
    for (size_t i = 0; i + p->width < nb; i++)
        if (bits[i]) {
            bits[i] = 0;
            for (unsigned k = 0; k < p->width; k++) bits[i + 1 + k] ^= (uint8_t)((p->poly >> (p->width - 1 - k)) & 1u);
        }
    uint32_t r = 0;
    for (unsigned k = 0; k < p->width; k++) r = (r << 1) | bits[nb - p->width + k];
    if (p->refout) r = reflect(r, p->width);
    return r ^ p->xorout;
}

int main(void) {
    static const uint8_t s9[] = "123456789";
    static const uint8_t abcde[] = "abcde";
    uint32_t seed = 0xB08u;

    /* ===== Part A: 표준 check 값 (RevEng 카탈로그) ===== */
    CHECK(crc_compute(&CRC8_SMBUS, s9, 9) == 0xF4u);
    CHECK(crc_compute(&CRC16_CCITT_FALSE, s9, 9) == 0x29B1u);
    CHECK(crc_compute(&CRC32_ISO_HDLC, s9, 9) == 0xCBF43926u);
    CHECK(check_value(ALG_SUM8, s9, 9) == 0xDDu);                      /* 477 = 0x1DD */
    CHECK(check_value(ALG_FL16, abcde, 5) == 0xC8F0u);                 /* ALGO-B14 */
    CHECK(crc_compute(&CRC16_CCITT_FALSE, s9, 0) == 0xFFFFu);          /* 빈 입력 = init */
    CHECK(crc_compute(&CRC32_ISO_HDLC, s9, 0) == 0x00000000u);         /* init ^ xorout */
    {   /* 다른 길(비트 배열 긴 나눗셈)과 랜덤 대조, n >= 4 (init 이 데이터 비트에 걸치도록) */
        uint8_t buf[MAX_DATA];
        int bad = 0;
        for (int t = 0; t < 300; t++) {
            size_t n = 4 + xs32(&seed) % (MAX_DATA - 3);
            for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)xs32(&seed);
            bad += crc_compute(&CRC8_SMBUS, buf, n) != ref_crc(&CRC8_SMBUS, buf, n);
            bad += crc_compute(&CRC16_CCITT_FALSE, buf, n) != ref_crc(&CRC16_CCITT_FALSE, buf, n);
            bad += crc_compute(&CRC32_ISO_HDLC, buf, n) != ref_crc(&CRC32_ISO_HDLC, buf, n);
        }
        printf("crc_compute vs long division: 900 cases, mismatches=%d\n", bad);
        CHECK(bad == 0);
    }
    {   /* 코드워드 바이트 배치 */
        uint8_t cw[16];
        CHECK(cw_encode(ALG_CRC16, s9, 9, cw, sizeof cw) == 11 && cw[9] == 0x29 && cw[10] == 0xB1);
        CHECK(cw_encode(ALG_CRC32, s9, 9, cw, sizeof cw) == 13 &&
              cw[9] == 0x26 && cw[10] == 0x39 && cw[11] == 0xF4 && cw[12] == 0xCB);
        CHECK(cw_encode(ALG_FL16, abcde, 5, cw, sizeof cw) == 7 && cw[5] == 0xC8 && cw[6] == 0xF0);
        CHECK(cw_encode(ALG_CRC32, s9, 9, cw, 12) == -1);              /* 용량 1 부족 */
        CHECK(cw_encode(ALG_SUM8, s9, 9, cw, 9) == -1);
        CHECK(cw_encode(ALG_CRC8, s9, 0, cw, 1) == 1 && cw[0] == 0x00); /* 빈 데이터 */
        uint8_t inplace[12];
        memcpy(inplace, s9, 9);
        CHECK(cw_encode(ALG_CRC8, inplace, 9, inplace, sizeof inplace) == 10 && inplace[9] == 0xF4);
        CHECK(cw_residual(ALG_CRC8, inplace, 0) == 0xFFFFFFFFu && !cw_verify(ALG_CRC32, inplace, 3));
    }
    {   /* 모든 알고리즘 × 길이 0..40: 인코드 → 검증 통과, 1비트 바꾸면 실패 */
        uint8_t data[48], cw[MAX_CW];
        int bad = 0;
        for (alg_t a = 0; a < ALG_COUNT; a++)
            for (size_t n = 0; n <= 40; n++) {
                for (size_t i = 0; i < n; i++) data[i] = (uint8_t)xs32(&seed);
                long len = cw_encode(a, data, n, cw, sizeof cw);
                if (len != (long)(n + CHECK_BYTES[a])) { bad++; continue; }
                bad += !cw_verify(a, cw, (size_t)len);
                cw[xs32(&seed) % (uint32_t)len] ^= (uint8_t)(1u << (xs32(&seed) % 8));
                bad += cw_verify(a, cw, (size_t)len);
            }
        CHECK(bad == 0);
    }

    /* ===== Part B: 주입기 · 측정 ===== */
    {   /* 주입기가 정확히 k 비트 / 길이 L 버스트를 만드는지 */
        uint8_t z[MAX_CW];
        int bad = 0;
        for (int t = 0; t < 2000; t++) {
            unsigned k = 1 + (unsigned)t % 4, L = 1 + (unsigned)t % 40;
            order_t o = (t & 1) ? ORD_LSB : ORD_MSB;
            memset(z, 0, sizeof z);
            inject_bits(z, 36 * 8, k, o, &seed);
            unsigned w = 0;
            for (int i = 0; i < 36; i++) w += (unsigned)__builtin_popcount(z[i]);
            bad += w != k;
            memset(z, 0, sizeof z);
            inject_burst(z, 36 * 8, L, o, &seed);
            int first = -1, last = -1;
            for (int i = 0; i < 36 * 8; i++) {
                unsigned bit = o == ORD_MSB ? (z[i >> 3] >> (7 - (i & 7))) & 1u : (z[i >> 3] >> (i & 7)) & 1u;
                if (bit) { if (first < 0) first = i; last = i; }
            }
            bad += first < 0 || (unsigned)(last - first + 1) != L;
        }
        CHECK(bad == 0);
    }
    uint32_t m[ALG_COUNT][5];
    for (alg_t a = 0; a < ALG_COUNT; a++)
        for (unsigned k = 1; k <= 4; k++)
            m[a][k] = count_undetected(a, 32, ERR_BITS, k, a == ALG_CRC32 ? ORD_LSB : ORD_MSB, 20000, 0xB08u + k);
    printf("32B data, 20000 trials each:  k=1..4 undetected\n");
    for (alg_t a = 0; a < ALG_COUNT; a++)
        printf("  %-10s %6u %6u %6u %6u\n", ALG_NAME[a], m[a][1], m[a][2], m[a][3], m[a][4]);
    {
        bool all1 = true;
        for (alg_t a = 0; a < ALG_COUNT; a++) all1 &= m[a][1] == 0;
        CHECK(all1);                                                    /* 1비트는 모두 잡는다 */
    }
    CHECK(m[ALG_SUM8][2] > 0);                                          /* sum8 은 2비트를 놓친다 */
    CHECK(m[ALG_CRC8][3] == 0 && m[ALG_CRC16][3] == 0);                 /* (x+1) 인수 → 홀수 비트 오류 전부 */
    CHECK(m[ALG_CRC32][2] == 0 && m[ALG_CRC32][3] == 0 && m[ALG_CRC32][4] == 0);
    CHECK(m[ALG_CRC8][4] > 0);                                          /* 8비트 검사값의 한계 */
    {   /* 길이 <= r 버스트: CRC 는 데이터와 무관하게 0 (짝맞는 비트 순서에서) */
        uint32_t c8 = 0, c16 = 0, c32 = 0, s8b = 0;
        for (unsigned L = 1; L <= 8; L++)  c8  += count_undetected(ALG_CRC8,  32, ERR_BURST, L, ORD_MSB, 3000, L);
        for (unsigned L = 1; L <= 16; L++) c16 += count_undetected(ALG_CRC16, 32, ERR_BURST, L, ORD_MSB, 3000, L);
        for (unsigned L = 1; L <= 32; L++) c32 += count_undetected(ALG_CRC32, 32, ERR_BURST, L, ORD_LSB, 3000, L);
        for (unsigned L = 9; L <= 16; L++) s8b += count_undetected(ALG_SUM8,  32, ERR_BURST, L, ORD_MSB, 3000, L);
        CHECK(c8 == 0 && c16 == 0 && c32 == 0);
        CHECK(s8b > 0);                                                 /* 체크섬은 짧은 버스트도 놓친다 */
        uint32_t c8_9 = count_undetected(ALG_CRC8, 32, ERR_BURST, 9, ORD_MSB, 40000, 9);
        printf("crc8 burst L=9: %u / 40000 undetected (theory 2^-7 -> 312.5)\n", c8_9);
        CHECK(c8_9 > 200 && c8_9 < 430);                                /* ±6σ 근처 */
    }
    {   /* 신드롬 = 선형성 확인: 임의 오류의 잔차 == 신드롬 XOR (데이터 무관) */
        static uint32_t syn[MAX_BITS];
        int nb = crc_syndromes(ALG_CRC16, 20, ORD_MSB, syn, MAX_BITS);
        CHECK(nb == 22 * 8);
        CHECK(crc_syndromes(ALG_SUM8, 20, ORD_MSB, syn, MAX_BITS) == -1);
        CHECK(crc_syndromes(ALG_CRC16, 20, ORD_MSB, syn, 100) == -1);
        nb = crc_syndromes(ALG_CRC16, 20, ORD_MSB, syn, MAX_BITS);
        int bad = 0;
        uint8_t data[20], cw[24];
        for (int t = 0; t < 500 && nb > 0; t++) {
            for (int i = 0; i < 20; i++) data[i] = (uint8_t)xs32(&seed);
            long len = cw_encode(ALG_CRC16, data, 20, cw, sizeof cw);
            uint32_t x = 0;
            for (int f = 0; f < 5; f++) { unsigned b = xs32(&seed) % (unsigned)nb; flip_wire(cw, b, ORD_MSB); x ^= syn[b]; }
            bad += cw_residual(ALG_CRC16, cw, (size_t)len) != x;
        }
        CHECK(bad == 0);
        /* 체크 바이트 안의 비트의 신드롬은 그 비트 자신 (CRC-16 MSB: 마지막 와이어 비트 = 값의 bit0) */
        CHECK(nb > 16 && syn[nb - 1] == 0x0001u && syn[nb - 16] == 0x8000u);
    }
    {   /* 경험적 HD (전수 2·3비트): CRC-8 0x07 은 주기 127 → 코드워드 128비트부터 2비트 미검출 */
        static uint32_t syn[MAX_BITS];
        int nb;
        nb = crc_syndromes(ALG_CRC8, 14, ORD_MSB, syn, MAX_BITS);   /* 120 bits */
        CHECK(min_undetected_weight(syn, nb, 3) == 4);               /* HD >= 4 */
        nb = crc_syndromes(ALG_CRC8, 15, ORD_MSB, syn, MAX_BITS);   /* 128 bits */
        CHECK(min_undetected_weight(syn, nb, 3) == 2);
        nb = crc_syndromes(ALG_CRC16, 32, ORD_MSB, syn, MAX_BITS);
        CHECK(min_undetected_weight(syn, nb, 3) == 4);
        nb = crc_syndromes(ALG_CRC32, 32, ORD_LSB, syn, MAX_BITS);
        CHECK(min_undetected_weight(syn, nb, 3) == 4);               /* 3비트까지 0 (실제 HD 는 더 큼: demo_hd) */
    }

    /* ===== Part C: 비트 순서 · 버스트 전수 · 선택 ===== */
    {
        static uint32_t syn[MAX_BITS];
        int nb;
        uint64_t msb8 = 0, lsb8 = 0, msb16 = 0, lsb16 = 0, lsb32 = 0, msb32 = 0, b9, b17;
        nb = crc_syndromes(ALG_CRC8, 8, ORD_MSB, syn, MAX_BITS);
        for (int L = 1; L <= 8; L++) msb8 += burst_undetected_exhaustive(syn, nb, L);
        b9 = burst_undetected_exhaustive(syn, nb, 9);
        nb = crc_syndromes(ALG_CRC8, 8, ORD_LSB, syn, MAX_BITS);
        for (int L = 1; L <= 8; L++) lsb8 += burst_undetected_exhaustive(syn, nb, L);
        nb = crc_syndromes(ALG_CRC16, 8, ORD_MSB, syn, MAX_BITS);
        for (int L = 1; L <= 16; L++) msb16 += burst_undetected_exhaustive(syn, nb, L);
        b17 = burst_undetected_exhaustive(syn, nb, 17);
        nb = crc_syndromes(ALG_CRC16, 8, ORD_LSB, syn, MAX_BITS);
        for (int L = 1; L <= 16; L++) lsb16 += burst_undetected_exhaustive(syn, nb, L);
        nb = crc_syndromes(ALG_CRC32, 4, ORD_LSB, syn, MAX_BITS);
        for (int L = 1; L <= 20; L++) lsb32 += burst_undetected_exhaustive(syn, nb, L);
        nb = crc_syndromes(ALG_CRC32, 4, ORD_MSB, syn, MAX_BITS);
        for (int L = 1; L <= 20; L++) msb32 += burst_undetected_exhaustive(syn, nb, L);
        printf("exhaustive bursts (8B data): crc8 L<=8 MSB=%llu LSB=%llu, L=9 MSB=%llu | crc16 L<=16 MSB=%llu LSB=%llu, L=17 MSB=%llu"
               " | crc32 (4B) L<=20 LSB=%llu MSB=%llu\n",
               (unsigned long long)msb8, (unsigned long long)lsb8, (unsigned long long)b9,
               (unsigned long long)msb16, (unsigned long long)lsb16, (unsigned long long)b17,
               (unsigned long long)lsb32, (unsigned long long)msb32);
        CHECK(msb8 == 0 && msb16 == 0 && lsb32 == 0);   /* 순서가 맞으면 길이 <= r 버스트 전부 검출 */
        CHECK(b9 == 72 - 9 + 1 && b17 == 80 - 17 + 1);  /* L = r+1: 시작 위치마다 정확히 1개 (오류 = g 자체) */
        CHECK(lsb8 > 0);                                /* UART(LSB-first) 위 CRC-8/SMBUS: 8비트 버스트도 놓친다 */
        CHECK(lsb16 == 0 && msb32 == 0);                /* 이 범위에선 버팀 (최단 미검출: 17, 31 비트 — demo_hd) */
        static const uint8_t e8[2] = { 0xD8, 0x06 };    /* 와이어(LSB-first) 비트 3..10 = 길이 8 버스트 */
        CHECK(crc_compute(&CRC8_SMBUS, e8, 2) == 0x00); /* g 의 배수 → 어느 메시지에 XOR 해도 미검출 */
    }
    CHECK(choose_check(8, 4, 8, ORD_MSB) == ALG_CRC8);       /* SPI 센서 8바이트 */
    CHECK(choose_check(8, 4, 8, ORD_LSB) == ALG_CRC16);      /* 같은 요구, UART: CRC-8 탈락 */
    CHECK(choose_check(32, 3, 8, ORD_MSB) == ALG_CRC16);     /* 32B: CRC-8 은 2비트도 놓침 (주기 127) */
    CHECK(choose_check(8, 4, 17, ORD_MSB) == ALG_CRC32);     /* 17비트 버스트 → 16비트 검사값으론 부족 */
    CHECK(choose_check(MAX_DATA + 1, 2, 1, ORD_MSB) == ALG_COUNT);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
