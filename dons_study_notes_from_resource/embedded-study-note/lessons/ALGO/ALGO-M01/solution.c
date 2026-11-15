/* ALGO-M01 CRC 원리: 비트 단위 CRC-8·CRC-16-CCITT
 *
 * Part A: 파라미터화된 비트 단위 CRC 엔진 (width 1..32, poly/init/refin/refout/xorout — Rocksoft 모델)
 *         + 교과서식 다항식 나눗셈(비트 배열 long division) 버전
 * Part B: 반사형(reflected) 알고리즘을 오른쪽 시프트 + 뒤집은 poly 로 (Modbus 코드의 정체),
 *         CRC 덧붙이기(바이트 순서)와 잔여값(residue) 검사, 짧은 메시지에서 오류 검출 전수 검사
 * Part C: 센서 버스 3종 — SMBus/PMBus Read Word PEC(CRC-8/SMBUS), Sensirion 워드 CRC(CRC-8/NRSC-5 파라미터),
 *         Modbus RTU 요청 프레임(CRC-16/MODBUS, 하위 바이트 먼저)
 *
 * 테스트: RevEng CRC 카탈로그 20개 알고리즘의 "123456789" check 값과 residue 값, Python binascii/zlib 로
 *         교차 확인한 값, 다른 길(바이트 단위 Horner + uint64 다항식 나머지)로 계산한 기준 구현과
 *         xorshift32 고정 시드 랜덤 대조(임의 width/poly/init/플래그), 1바이트 메시지 전수,
 *         4바이트 메시지의 1·2·3비트 오류와 길이 ≤ width 버스트 전수 검출.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
 * 테이블(LUT) 방식은 ALGO-M02. 이 파일은 일부러 비트 단위만 쓴다.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *name;
    unsigned width;      /* 1..32 */
    uint32_t poly;       /* normal 표기: x^width 항은 빼고, x^(width-1)..x^0 계수 */
    uint32_t init;       /* 첫 메시지 비트를 읽기 전 레지스터 값 (direct 알고리즘 기준) */
    bool refin;          /* true: 각 바이트를 LSB 먼저 읽는다 */
    bool refout;         /* true: 마지막 레지스터를 width 비트 반전 */
    uint32_t xorout;     /* 마지막에 XOR */
    uint32_t check;      /* 카탈로그: CRC("123456789") */
    uint32_t residue;    /* 카탈로그: 오류 없는 codeword 를 읽은 뒤 레지스터 (refout 반영, xorout 전) */
} crc_params;

/* ---------------- Part A: 엔진 ---------------- */

/* 하위 width 비트가 1 인 마스크. width = 32 에서 1u << 32 는 UB 라 따로 처리한다. */
static uint32_t crc_mask(unsigned width) {
    return width >= 32 ? 0xFFFFFFFFu : (1u << width) - 1u;
}

/* x 의 하위 nbits 비트를 좌우 반전 (bit 0 ↔ bit nbits-1). 나머지 상위 비트는 0. */
static uint32_t reflect(uint32_t x, unsigned nbits) {
    uint32_t r = 0;
    for (unsigned i = 0; i < nbits; i++) {
        r = (r << 1) | (x & 1u);
        x >>= 1;
    }
    return r;
}

/* direct(비증강) 비트 단위 알고리즘. 레지스터는 MSB(bit width-1) 쪽으로 밀려 나간다. */
static uint32_t crc_bitwise(const crc_params *p, const uint8_t *d, size_t n) {
    const unsigned w = p->width;
    const uint32_t mask = crc_mask(w);
    uint32_t crc = p->init & mask;
    for (size_t i = 0; i < n; i++) {
        uint8_t b = p->refin ? (uint8_t)reflect(d[i], 8) : d[i];
        for (int k = 7; k >= 0; k--) {
            uint32_t top = ((crc >> (w - 1)) ^ ((uint32_t)b >> k)) & 1u;   /* 나가는 비트 ⊕ 들어오는 비트 */
            crc = (crc << 1) & mask;
            if (top) crc ^= p->poly;
        }
    }
    if (p->refout) crc = reflect(crc, w);
    return (crc ^ p->xorout) & mask;
}

/* 교과서식: 비트 배열 위에서 (M(x)·x^w + Init(x)·x^L) mod G(x) 를 long division 으로.
 * 배열 칸 j 는 지수 (L + w - 1 - j). 메시지 n <= CRC_POLYDIV_MAX 바이트 (넘으면 0 반환). */
#define CRC_POLYDIV_MAX 256u
static uint32_t crc_polydiv(const crc_params *p, const uint8_t *d, size_t n) {
    static uint8_t bits[CRC_POLYDIV_MAX * 8u + 32u];
    if (n > CRC_POLYDIV_MAX) return 0;
    const unsigned w = p->width;
    const size_t L = 8u * n;
    memset(bits, 0, L + w);
    for (size_t i = 0; i < n; i++)                       /* 메시지 비트를 읽는 순서대로 */
        for (unsigned k = 0; k < 8; k++)
            bits[8 * i + k] = p->refin ? (d[i] >> k) & 1u : (d[i] >> (7 - k)) & 1u;
    for (unsigned k = 0; k < w; k++)                     /* Init·x^L: 맨 앞 w 칸에 XOR */
        bits[k] ^= (p->init >> (w - 1 - k)) & 1u;
    for (size_t j = 0; j < L; j++) {                     /* 맨 앞 1 마다 G 를 그 자리에 맞춰 XOR */
        if (!bits[j]) continue;
        bits[j] = 0;                                     /* G 의 x^w 항 */
        for (unsigned k = 0; k < w; k++)
            bits[j + 1 + k] ^= (p->poly >> (w - 1 - k)) & 1u;
    }
    uint32_t r = 0;                                      /* 나머지 = 마지막 w 칸 */
    for (unsigned k = 0; k < w; k++) r = (r << 1) | bits[L + k];
    if (p->refout) r = reflect(r, w);
    return (r ^ p->xorout) & crc_mask(w);
}

/* ---------------- Part B: 반사형, 덧붙이기, 잔여값 ---------------- */

/* refin = refout = true 전용: 레지스터를 처음부터 반전된 모양으로 두고 오른쪽으로 민다.
 * 바이트 반전·결과 반전이 필요 없다. poly 도 반전(0x8005 → 0xA001). */
static uint32_t crc_reflected(const crc_params *p, const uint8_t *d, size_t n) {
    const unsigned w = p->width;
    const uint32_t rpoly = reflect(p->poly, w);
    uint32_t crc = reflect(p->init, w);
    for (size_t i = 0; i < n; i++) {
        for (unsigned k = 0; k < 8; k++) {               /* LSB 먼저 */
            crc ^= ((uint32_t)d[i] >> k) & 1u;
            crc = (crc & 1u) ? (crc >> 1) ^ rpoly : crc >> 1;
        }
    }
    return (crc ^ p->xorout) & crc_mask(w);
}

/* buf[0..n) 뒤에 CRC 를 붙이고 새 길이를 돌려준다. width 는 8 의 배수만.
 * refout = true 면 하위 바이트 먼저(LSB-first 선로 순서 유지), false 면 상위 바이트 먼저. */
static size_t crc_append(const crc_params *p, uint8_t *buf, size_t n) {
    const uint32_t c = crc_bitwise(p, buf, n);
    const unsigned nb = p->width / 8u;
    for (unsigned i = 0; i < nb; i++)
        buf[n + i] = p->refout ? (uint8_t)(c >> (8u * i)) : (uint8_t)(c >> (8u * (nb - 1u - i)));
    return n + nb;
}

/* 수신 측: codeword 전체(데이터 + CRC)를 엔진에 통과시켜 레지스터가 카탈로그 residue 인지.
 * 엔진 결과 = 레지스터(refout 반영) ^ xorout 이므로 xorout 을 다시 XOR 해 비교한다. */
static bool crc_residue_ok(const crc_params *p, const uint8_t *frame, size_t len) {
    if (len < p->width / 8u) return false;
    return (crc_bitwise(p, frame, len) ^ p->xorout) == p->residue;
}

/* ---------------- Part C: 센서 버스 ---------------- */

static const crc_params CRC8_SMBUS  = { "CRC-8/SMBUS", 8, 0x07, 0x00, false, false, 0x00, 0xF4, 0x00 };
static const crc_params CRC8_NRSC5  = { "CRC-8/NRSC-5", 8, 0x31, 0xFF, false, false, 0x00, 0xF7, 0x00 };
static const crc_params CRC16_3740  = { "CRC-16/IBM-3740", 16, 0x1021, 0xFFFF, false, false, 0x0000, 0x29B1, 0x0000 };
static const crc_params CRC16_MODBUS = { "CRC-16/MODBUS", 16, 0x8005, 0xFFFF, true, true, 0x0000, 0x4B37, 0x0000 };

/* SMBus Read Word + PEC: 선로의 바이트 [addr<<1|W, cmd, addr<<1|R, lo, hi] 전부가 PEC(CRC-8/SMBUS) 대상.
 * rx = { lo, hi, pec }. 맞으면 *word = lo | hi << 8 (SMBus 워드는 하위 바이트 먼저). */
static bool smbus_read_word_ok(uint8_t addr7, uint8_t cmd, const uint8_t rx[3], uint16_t *word) {
    const uint8_t wire[5] = { (uint8_t)(addr7 << 1), cmd, (uint8_t)(addr7 << 1 | 1u), rx[0], rx[1] };
    if (crc_bitwise(&CRC8_SMBUS, wire, 5) != rx[2]) return false;
    *word = (uint16_t)(rx[0] | rx[1] << 8);
    return true;
}

/* Sensirion SHT3x 측정 프레임 6바이트: T_hi T_lo CRC RH_hi RH_lo CRC (워드마다 CRC-8, poly 0x31 init 0xFF).
 * T = -45 + 175·S/65535 [°C], RH = 100·S/65535 [%] 를 밀리 단위 정수로 (반올림).
 * 175000·65535 > 2^32 이므로 곱은 uint64_t 로 (7. 자주 하는 실수 6). */
static bool sht_parse(const uint8_t rx[6], int32_t *t_mC, int32_t *rh_mpct) {
    if (crc_bitwise(&CRC8_NRSC5, rx, 2) != rx[2] || crc_bitwise(&CRC8_NRSC5, rx + 3, 2) != rx[5]) return false;
    const uint32_t st = (uint32_t)rx[0] << 8 | rx[1], srh = (uint32_t)rx[3] << 8 | rx[4];
    *t_mC = -45000 + (int32_t)((175000ull * st + 32767u) / 65535u);
    *rh_mpct = (int32_t)((100000ull * srh + 32767u) / 65535u);
    return true;
}

/* Modbus RTU "Read Holding Registers"(0x03) 요청: slave, 0x03, start(BE), qty(BE), CRC(LE) = 8바이트 */
static size_t modbus_build_read(uint8_t slave, uint16_t start, uint16_t qty, uint8_t out[8]) {
    out[0] = slave; out[1] = 0x03;
    out[2] = (uint8_t)(start >> 8); out[3] = (uint8_t)start;
    out[4] = (uint8_t)(qty >> 8);   out[5] = (uint8_t)qty;
    return crc_append(&CRC16_MODBUS, out, 6);
}

/* 주소 + 함수 + CRC 2바이트 = 최소 4바이트. CRC 포함 전체를 돌려 잔여값 0 인지. */
static bool modbus_frame_ok(const uint8_t *f, size_t len) {
    return len >= 4 && crc_residue_ok(&CRC16_MODBUS, f, len);
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* 기준 구현 (다른 길): 바이트마다 R ← (R·x^8 + B·x^w) mod G 를 uint64 다항식 나머지로 직접 계산 */
static uint32_t ref_rev(uint32_t x, unsigned n) { uint32_t r = 0; for (unsigned i = 0; i < n; i++) if (x >> i & 1u) r |= 1u << (n - 1 - i); return r; }
static uint32_t ref_crc(const crc_params *p, const uint8_t *d, size_t n) {
    const unsigned w = p->width;
    const uint64_t G = (1ull << w) | p->poly, m = (1ull << w) - 1;
    uint64_t R = p->init & m;
    for (size_t i = 0; i < n; i++) {
        uint64_t B = p->refin ? ref_rev(d[i], 8) : d[i];
        uint64_t v = (R << 8) ^ (B << w);                /* 차수 < w + 8 */
        for (int e = (int)w + 7; e >= (int)w; e--)
            if (v >> e & 1u) v ^= G << (e - (int)w);
        R = v & m;
    }
    if (p->refout) R = ref_rev((uint32_t)R, w);
    return (uint32_t)((R ^ p->xorout) & m);
}

/* RevEng CRC 카탈로그 (reveng.sourceforge.io/crc-catalogue) 에서 옮긴 파라미터 · check · residue */
static const crc_params CAT[] = {
    { "CRC-3/GSM",          3, 0x3,        0x0,        false, false, 0x7,        0x4,        0x2 },
    { "CRC-4/G-704",        4, 0x3,        0x0,        true,  true,  0x0,        0x7,        0x0 },
    { "CRC-5/USB",          5, 0x05,       0x1F,       true,  true,  0x1F,       0x19,       0x06 },
    { "CRC-8/SMBUS",        8, 0x07,       0x00,       false, false, 0x00,       0xF4,       0x00 },
    { "CRC-8/I-432-1",      8, 0x07,       0x00,       false, false, 0x55,       0xA1,       0xAC },
    { "CRC-8/MAXIM-DOW",    8, 0x31,       0x00,       true,  true,  0x00,       0xA1,       0x00 },
    { "CRC-8/NRSC-5",       8, 0x31,       0xFF,       false, false, 0x00,       0xF7,       0x00 },
    { "CRC-8/AUTOSAR",      8, 0x2F,       0xFF,       false, false, 0xFF,       0xDF,       0x42 },
    { "CRC-15/CAN",        15, 0x4599,     0x0000,     false, false, 0x0000,     0x059E,     0x0000 },
    { "CRC-16/IBM-3740",   16, 0x1021,     0xFFFF,     false, false, 0x0000,     0x29B1,     0x0000 },
    { "CRC-16/MODBUS",     16, 0x8005,     0xFFFF,     true,  true,  0x0000,     0x4B37,     0x0000 },
    { "CRC-16/XMODEM",     16, 0x1021,     0x0000,     false, false, 0x0000,     0x31C3,     0x0000 },
    { "CRC-16/KERMIT",     16, 0x1021,     0x0000,     true,  true,  0x0000,     0x2189,     0x0000 },
    { "CRC-16/SPI-FUJITSU",16, 0x1021,     0x1D0F,     false, false, 0x0000,     0xE5CC,     0x0000 },
    { "CRC-16/UMTS",       16, 0x8005,     0x0000,     false, false, 0x0000,     0xFEE8,     0x0000 },
    { "CRC-16/ARC",        16, 0x8005,     0x0000,     true,  true,  0x0000,     0xBB3D,     0x0000 },
    { "CRC-16/IBM-SDLC",   16, 0x1021,     0xFFFF,     true,  true,  0xFFFF,     0x906E,     0xF0B8 },
    { "CRC-32/ISO-HDLC",   32, 0x04C11DB7, 0xFFFFFFFF, true,  true,  0xFFFFFFFF, 0xCBF43926, 0xDEBB20E3 },
    { "CRC-32/MPEG-2",     32, 0x04C11DB7, 0xFFFFFFFF, false, false, 0x00000000, 0x0376E6E7, 0x00000000 },
    { "CRC-32/ISCSI",      32, 0x1EDC6F41, 0xFFFFFFFF, true,  true,  0xFFFFFFFF, 0xE3069283, 0xB798B438 },
};
#define NCAT (sizeof CAT / sizeof CAT[0])

/* 오류 검출 전수: data 4바이트 + CRC 의 codeword 에서 오류 패턴을 XOR 하고 residue 검사가 놓친 개수.
 * 비트 위치 pos 는 CRC 가 읽는 순서(= 선로 순서): refin 이면 바이트 안에서 LSB 먼저. */
static uint8_t g_cw[8], g_bad[8];
static size_t g_cwlen;
static uint8_t bitmask_at(const crc_params *p, size_t pos) {
    return (uint8_t)(p->refin ? 1u << (pos % 8) : 0x80u >> (pos % 8));
}
static long missed_after_flip(const crc_params *p, const size_t *pos, int k) {
    memcpy(g_bad, g_cw, g_cwlen);
    for (int i = 0; i < k; i++) g_bad[pos[i] / 8] ^= bitmask_at(p, pos[i]);
    return crc_residue_ok(p, g_bad, g_cwlen) ? 1 : 0;
}

int main(void) {
    static uint8_t buf[600];
    uint32_t seed = 0xA160001u;
    const uint8_t *chk = (const uint8_t *)"123456789";

    /* ===== Part A: 보조 함수 ===== */
    CHECK(crc_mask(1) == 0x1u && crc_mask(8) == 0xFFu && crc_mask(15) == 0x7FFFu && crc_mask(32) == 0xFFFFFFFFu);
    CHECK(reflect(0x01, 8) == 0x80 && reflect(0x31, 8) == 0x8C && reflect(0x8005, 16) == 0xA001);
    CHECK(reflect(0x1021, 16) == 0x8408 && reflect(0x04C11DB7u, 32) == 0xEDB88320u && reflect(0x3, 3) == 0x6);

    /* ===== Part A: 세 주인공의 check 값 (+ Python binascii.crc_hqx(…, 0xFFFF) = 0x29B1 로도 확인) ===== */
    CHECK(crc_bitwise(&CRC8_SMBUS, chk, 9) == 0xF4);
    CHECK(crc_bitwise(&CRC16_3740, chk, 9) == 0x29B1);
    CHECK(crc_bitwise(&CRC16_MODBUS, chk, 9) == 0x4B37);
    CHECK(crc_polydiv(&CRC8_SMBUS, chk, 9) == 0xF4);
    CHECK(crc_polydiv(&CRC16_3740, chk, 9) == 0x29B1);
    CHECK(crc_polydiv(&CRC16_MODBUS, chk, 9) == 0x4B37);
    CHECK(crc_bitwise(&CRC8_SMBUS, chk, 1) == 0x97);            /* '1' 한 바이트 — 5. 풀이의 손 계산 */

    /* ===== Part A: 카탈로그 20개 check 값 — 엔진과 다항식 나눗셈 둘 다 ===== */
    int bad_eng = 0, bad_div = 0;
    for (size_t i = 0; i < NCAT; i++) {
        uint32_t a = crc_bitwise(&CAT[i], chk, 9), b = crc_polydiv(&CAT[i], chk, 9);
        if (a != CAT[i].check) { bad_eng++; printf("  engine %-18s got 0x%X want 0x%X\n", CAT[i].name, a, CAT[i].check); }
        if (b != CAT[i].check) { bad_div++; printf("  polydiv %-18s got 0x%X want 0x%X\n", CAT[i].name, b, CAT[i].check); }
    }
    printf("catalogue %zu algorithms: engine mismatches=%d, polydiv mismatches=%d\n", NCAT, bad_eng, bad_div);
    CHECK(bad_eng == 0);
    CHECK(bad_div == 0);

    /* 빈 메시지: 레지스터 = init 그대로 → (refout) ^ xorout */
    CHECK(crc_bitwise(&CRC16_3740, chk, 0) == 0xFFFF && crc_bitwise(&CRC8_SMBUS, chk, 0) == 0x00);
    CHECK(crc_bitwise(&CAT[17], chk, 0) == 0 && crc_polydiv(&CAT[17], chk, 0) == 0);   /* CRC-32: ~0 ^ ~0 */
    CHECK(crc_polydiv(&CAT[2], chk, 0) == crc_bitwise(&CAT[2], chk, 0));               /* CRC-5/USB: L < w */

    /* 1바이트 메시지 전수 (세 주인공 × 256) */
    int bad1 = 0;
    for (unsigned v = 0; v < 256; v++) {
        uint8_t b = (uint8_t)v;
        bad1 += crc_bitwise(&CRC8_SMBUS, &b, 1) != ref_crc(&CRC8_SMBUS, &b, 1);
        bad1 += crc_bitwise(&CRC16_3740, &b, 1) != ref_crc(&CRC16_3740, &b, 1);
        bad1 += crc_bitwise(&CRC16_MODBUS, &b, 1) != ref_crc(&CRC16_MODBUS, &b, 1);
        bad1 += crc_polydiv(&CRC16_MODBUS, &b, 1) != ref_crc(&CRC16_MODBUS, &b, 1);
    }
    CHECK(bad1 == 0);

    /* 임의 파라미터(width 1..32, poly/init/xorout/플래그 랜덤) × 랜덤 메시지(0..64B) 3000개 */
    int badr[3] = {0};
    for (int t = 0; t < 3000; t++) {
        crc_params p = { "rand", 1u + xs32(&seed) % 32u, 0, 0, false, false, 0, 0, 0 };
        uint32_t m = crc_mask(p.width);
        p.poly = xs32(&seed) & m; p.init = xs32(&seed) & m; p.xorout = (t % 3) ? xs32(&seed) & m : 0;
        p.refin = xs32(&seed) & 1u; p.refout = (t % 4 == 0) ? !p.refin : p.refin;   /* 가끔 refin ≠ refout */
        if (t % 5 == 0) p.width = 32, p.poly = xs32(&seed), p.init = xs32(&seed);   /* 32 비트를 자주 */
        size_t n = xs32(&seed) % 65u;
        for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)xs32(&seed);
        uint32_t r = ref_crc(&p, buf, n);
        badr[0] += crc_bitwise(&p, buf, n) != r;
        badr[1] += crc_polydiv(&p, buf, n) != r;
        if (p.refin && p.refout) badr[2] += crc_reflected(&p, buf, n) != r;
    }
    printf("random params x msgs 3000: mismatches bitwise=%d polydiv=%d reflected=%d\n", badr[0], badr[1], badr[2]);
    CHECK(badr[0] == 0);
    CHECK(badr[1] == 0);

    /* ===== Part B: 오른쪽 시프트 반사형 = 엔진 ===== */
    CHECK(badr[2] == 0);
    CHECK(crc_reflected(&CRC16_MODBUS, chk, 9) == 0x4B37);
    int badrf = 0;
    for (size_t i = 0; i < NCAT; i++)
        if (CAT[i].refin && CAT[i].refout) badrf += crc_reflected(&CAT[i], chk, 9) != CAT[i].check;
    CHECK(badrf == 0);

    /* ===== Part B: 덧붙이기 + 잔여값 — 카탈로그 residue (width 8/16/32 의 16개) ===== */
    int nres = 0, badres = 0;
    for (size_t i = 0; i < NCAT; i++) {
        if (CAT[i].width % 8u) continue;
        for (int t = 0; t < 50; t++) {
            size_t n = (size_t)t;                            /* 길이 0..49 */
            for (size_t j = 0; j < n; j++) buf[j] = (uint8_t)xs32(&seed);
            size_t len = crc_append(&CAT[i], buf, n);
            badres += len != n + CAT[i].width / 8u || !crc_residue_ok(&CAT[i], buf, len);
            if (len == 0) { badres++; continue; }            /* 스텁 대비: % 0 방지 */
            buf[xs32(&seed) % len] ^= (uint8_t)(1u << (xs32(&seed) % 8u));   /* 1비트 깨면 거부 */
            badres += crc_residue_ok(&CAT[i], buf, len);
        }
        nres++;
    }
    printf("residue check: %d algorithms x 50 frames (ok + 1-bit flipped), bad=%d\n", nres, badres);
    CHECK(nres == 16 && badres == 0);
    memcpy(buf, chk, 9);
    CHECK(crc_append(&CRC16_3740, buf, 9) == 11 && buf[9] == 0x29 && buf[10] == 0xB1);     /* BE */
    CHECK(crc_append(&CRC16_MODBUS, buf, 9) == 11 && buf[9] == 0x37 && buf[10] == 0x4B);   /* LE */

    /* ===== Part B: 오류 검출 전수 — data 4B 의 codeword, 1·2·3비트 + 버스트 길이 1..w ===== */
    const crc_params *main3[3] = { &CRC8_SMBUS, &CRC16_3740, &CRC16_MODBUS };
    for (int a = 0; a < 3; a++) {
        const crc_params *p = main3[a];
        for (int i = 0; i < 4; i++) g_cw[i] = (uint8_t)xs32(&seed);
        g_cwlen = crc_append(p, g_cw, 4);
        const size_t nbits = 8 * g_cwlen;
        long tried = 0, missed = 0;
        size_t pos[3];
        for (pos[0] = 0; pos[0] < nbits; pos[0]++) {
            tried++; missed += missed_after_flip(p, pos, 1);
            for (pos[1] = pos[0] + 1; pos[1] < nbits; pos[1]++) {
                tried++; missed += missed_after_flip(p, pos, 2);
                for (pos[2] = pos[1] + 1; pos[2] < nbits; pos[2]++) { tried++; missed += missed_after_flip(p, pos, 3); }
            }
        }
        long btried = 0, bmissed = 0;               /* 버스트: 첫 비트와 끝 비트가 1, 사이 b-2 비트 자유 */
        for (unsigned b = 1; b <= p->width; b++)
            for (size_t s = 0; s + b <= nbits; s++)
                for (uint32_t mid = 0; mid < (b >= 2 ? 1u << (b - 2) : 1u); mid++) {
                    memcpy(g_bad, g_cw, g_cwlen);
                    for (unsigned k = 0; k < b; k++) {
                        bool on = (k == 0 || k == b - 1) || ((mid >> (k - 1)) & 1u);
                        if (on) g_bad[(s + k) / 8] ^= bitmask_at(p, s + k);
                    }
                    btried++; bmissed += crc_residue_ok(p, g_bad, g_cwlen);
                }
        printf("%-16s %zu-bit codeword: 1..3-bit errors %ld tried, %ld missed; bursts <= %u: %ld tried, %ld missed\n",
               p->name, nbits, tried, missed, p->width, btried, bmissed);
        CHECK(g_cwlen == 4 + p->width / 8 && crc_residue_ok(p, g_cw, g_cwlen) && missed == 0 && bmissed == 0);
    }

    /* ===== Part C: SMBus PEC ===== */
    uint8_t rx[3];
    uint16_t word = 0;
    {   /* PMBus READ_VOUT(0x8B) @ 0x40, 응답 0x1234 — PEC 는 기준 구현으로 만든다 */
        const uint8_t wire[5] = { 0x80, 0x8B, 0x81, 0x34, 0x12 };
        rx[0] = 0x34; rx[1] = 0x12; rx[2] = (uint8_t)ref_crc(&CRC8_SMBUS, wire, 5);
    }
    CHECK(smbus_read_word_ok(0x40, 0x8B, rx, &word) && word == 0x1234);
    CHECK(!smbus_read_word_ok(0x41, 0x8B, rx, &word));      /* 다른 주소의 응답 → PEC 불일치 */
    CHECK(!smbus_read_word_ok(0x40, 0x8C, rx, &word));      /* 다른 명령 */
    int badsm = 0;
    for (int t = 0; t < 5000; t++) {
        uint8_t ad = (uint8_t)(xs32(&seed) & 0x7Fu), cm = (uint8_t)xs32(&seed);
        uint8_t wire[5] = { (uint8_t)(ad << 1), cm, (uint8_t)(ad << 1 | 1u), (uint8_t)xs32(&seed), (uint8_t)xs32(&seed) };
        uint8_t r3[3] = { wire[3], wire[4], (uint8_t)ref_crc(&CRC8_SMBUS, wire, 5) };
        badsm += !smbus_read_word_ok(ad, cm, r3, &word) || word != (uint16_t)(wire[3] | wire[4] << 8);
        r3[xs32(&seed) % 3u] ^= (uint8_t)(1u << (xs32(&seed) % 8u));
        badsm += smbus_read_word_ok(ad, cm, r3, &word);
    }
    CHECK(badsm == 0);

    /* ===== Part C: Sensirion 워드 CRC (데이터시트 예: CRC(0xBEEF) = 0x92) ===== */
    const uint8_t beef[2] = { 0xBE, 0xEF };
    CHECK(crc_bitwise(&CRC8_NRSC5, beef, 2) == 0x92);
    int32_t tm = 0, rh = 0;
    uint8_t sht[6] = { 0x66, 0x66, 0, 0x80, 0x00, 0 };
    sht[2] = (uint8_t)ref_crc(&CRC8_NRSC5, sht, 2); sht[5] = (uint8_t)ref_crc(&CRC8_NRSC5, sht + 3, 2);
    CHECK(sht_parse(sht, &tm, &rh) && tm == 25000 && rh == 50001);
    uint8_t sht0[6] = { 0, 0, 0, 0xFF, 0xFF, 0 };
    sht0[2] = (uint8_t)ref_crc(&CRC8_NRSC5, sht0, 2); sht0[5] = (uint8_t)ref_crc(&CRC8_NRSC5, sht0 + 3, 2);
    CHECK(sht_parse(sht0, &tm, &rh) && tm == -45000 && rh == 100000);
    sht[4] ^= 0x01;
    CHECK(!sht_parse(sht, &tm, &rh));                       /* RH 워드 1비트 → 거부 */

    /* ===== Part C: Modbus RTU (흔히 인용되는 예: 01 03 0000 000A → CRC C5 CD) ===== */
    uint8_t mb[8];
    CHECK(modbus_build_read(0x01, 0x0000, 0x000A, mb) == 8 && mb[6] == 0xC5 && mb[7] == 0xCD);
    CHECK(modbus_frame_ok(mb, 8));
    CHECK(!modbus_frame_ok(mb, 7) && !modbus_frame_ok(mb, 3));   /* 잘린 프레임 */
    mb[7] ^= 0x01;
    CHECK(!modbus_frame_ok(mb, 8));
    int badmb = 0;
    for (int t = 0; t < 5000; t++) {
        uint8_t f[8];
        modbus_build_read((uint8_t)(1 + xs32(&seed) % 247u), (uint16_t)xs32(&seed), (uint16_t)(1 + xs32(&seed) % 125u), f);
        uint16_t c = (uint16_t)ref_crc(&CRC16_MODBUS, f, 6);
        badmb += f[6] != (uint8_t)c || f[7] != (uint8_t)(c >> 8) || !modbus_frame_ok(f, 8);
        f[xs32(&seed) % 8u] ^= (uint8_t)(1u + xs32(&seed) % 255u);   /* 바이트 하나를 임의로 깨기 */
        badmb += modbus_frame_ok(f, 8);
    }
    CHECK(badmb == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
