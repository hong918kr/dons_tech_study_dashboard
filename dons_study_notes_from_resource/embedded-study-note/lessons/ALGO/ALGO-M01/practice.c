/* ALGO-M01 추가 연습 — 한 번에 검증
 *
 * 연습 1: 비트 단위 API crc_bits (길이가 8의 배수가 아닌 메시지) + 카탈로그 20개 residue 를 비트 codeword 로
 * 연습 2: init 이 막는 것(앞쪽 0x00)과 xorout 이 막는 것(잔여값 검사에서 뒤쪽 0x00) — 예측하고 측정
 * 연습 3: G 의 주기와 "모든 2비트 오류를 잡는 최대 데이터 길이"
 * 연습 4: 미지의 CRC-16 파라미터 역추적 (RevEng 식: 같은 길이 두 프레임의 XOR 로 init·xorout 제거)
 * 연습 5: CRC 위조 — 2바이트를 붙여 원하는 CRC 만들기 (해가 정확히 1개인 이유)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *name;
    unsigned width;
    uint32_t poly, init;
    bool refin, refout;
    uint32_t xorout, check, residue;
} crc_params;

static uint32_t crc_mask(unsigned w) { return w >= 32 ? 0xFFFFFFFFu : (1u << w) - 1u; }
static uint32_t reflect(uint32_t x, unsigned n) { uint32_t r = 0; for (unsigned i = 0; i < n; i++) { r = (r << 1) | (x & 1u); x >>= 1; } return r; }

/* 레지스터 한 비트 전진 (direct 알고리즘) */
static uint32_t step(const crc_params *p, uint32_t reg, uint32_t in) {
    uint32_t top = ((reg >> (p->width - 1)) ^ in) & 1u;
    reg = (reg << 1) & crc_mask(p->width);
    return top ? reg ^ p->poly : reg;
}
/* 레지스터 상태에서 바이트들을 이어서 처리 (refout/xorout 전) */
static uint32_t crc_raw_update(const crc_params *p, uint32_t reg, const uint8_t *d, size_t n) {
    for (size_t i = 0; i < n; i++)
        for (unsigned k = 0; k < 8; k++)
            reg = step(p, reg, p->refin ? (d[i] >> k) & 1u : (d[i] >> (7 - k)) & 1u);
    return reg;
}
static uint32_t crc_finish(const crc_params *p, uint32_t reg) {
    if (p->refout) reg = reflect(reg, p->width);
    return (reg ^ p->xorout) & crc_mask(p->width);
}
static uint32_t crc_bitwise(const crc_params *p, const uint8_t *d, size_t n) {
    return crc_finish(p, crc_raw_update(p, p->init & crc_mask(p->width), d, n));
}
static size_t crc_append(const crc_params *p, uint8_t *buf, size_t n) {
    uint32_t c = crc_bitwise(p, buf, n); unsigned nb = p->width / 8u;
    for (unsigned i = 0; i < nb; i++) buf[n + i] = p->refout ? (uint8_t)(c >> (8u * i)) : (uint8_t)(c >> (8u * (nb - 1u - i)));
    return n + nb;
}
static bool crc_residue_ok(const crc_params *p, const uint8_t *f, size_t len) {
    return len >= p->width / 8u && (crc_bitwise(p, f, len) ^ p->xorout) == p->residue;
}

/* ---------- 연습 1: 비트 API ---------- */
/* bits[i] ∈ {0,1} 는 "읽는 순서" 그대로 (refin 은 이미 반영된 것으로 본다 → 여기서는 무시) */
static uint32_t crc_bits(const crc_params *p, const uint8_t *bits, size_t nbits) {
    uint32_t reg = p->init & crc_mask(p->width);
    for (size_t i = 0; i < nbits; i++) reg = step(p, reg, bits[i]);
    return crc_finish(p, reg);
}

/* ---------- 연습 3: 주기 ---------- */
/* x^k ≡ 1 (mod G) 인 최소 k ≥ 1. G 의 상수항이 0 이면(x | G) 주기가 없다 → 0 */
static uint32_t poly_period(unsigned w, uint32_t poly) {
    if (!(poly & 1u)) return 0;
    const crc_params p = { "", w, poly, 0, false, false, 0, 0, 0 };
    uint32_t r = 1;
    for (uint64_t k = 1; k <= (1ull << w); k++) {
        r = step(&p, r, 0);
        if (r == 1) return (uint32_t)k;
    }
    return 0;
}
/* codeword N = 8·n + w 비트에서 모든 2비트 오류를 잡으려면 N - 1 < period ⇔ 8n ≤ period - w */
static uint32_t max_bytes_all_2bit(unsigned w, uint32_t poly) {
    uint32_t per = poly_period(w, poly);
    return per > w ? (per - w) / 8u : 0;
}

/* ---------- 연습 4: 파라미터 역추적 ---------- */
typedef struct { const uint8_t *d; size_t n; uint16_t crc; } frame;

/* 같은 길이 프레임 쌍 (f[0],f[1]), (f[2],f[3]) 로 poly·반사 후보를 찾고, 길이가 다른 f[4] 까지 써서 init·xorout.
 * 가정: width 16, refin == refout. 찾은 해의 개수를 돌려준다 (out[0..3] 에 처음 4개). */
static int crc16_recover(const frame f[5], crc_params out[4]) {
    static uint8_t x[64];
    int found = 0;
    for (int refl = 0; refl < 2; refl++) {
        for (uint32_t poly = 1; poly < 0x10000u; poly += 2) {     /* 상수항 1 인 poly 만 */
            crc_params q = { "cand", 16, poly, 0, refl != 0, refl != 0, 0, 0, 0 };
            bool ok = true;
            for (int pr = 0; pr < 2 && ok; pr++) {                  /* crc(a)⊕crc(b) = crc0(a⊕b) */
                const frame *a = &f[2 * pr], *b = &f[2 * pr + 1];
                for (size_t i = 0; i < a->n; i++) x[i] = a->d[i] ^ b->d[i];
                ok = crc_bitwise(&q, x, a->n) == (uint32_t)(a->crc ^ b->crc);
            }
            if (!ok) continue;
            /* init 전수: xorout 은 f[0] 에서 결정, 나머지 프레임으로 검증 */
            for (uint32_t init = 0; init < 0x10000u; init++) {
                q.init = init; q.xorout = 0;
                uint32_t xo = crc_bitwise(&q, f[0].d, f[0].n) ^ f[0].crc;
                q.xorout = xo;
                bool all = true;
                for (int i = 1; i < 5 && all; i++) all = crc_bitwise(&q, f[i].d, f[i].n) == f[i].crc;
                if (all) { if (found < 4) out[found] = q; found++; }
            }
        }
    }
    return found;
}

/* ---------- 연습 5: 위조 ---------- */
/* msg 뒤에 2바이트 s 를 붙여 CRC-16 이 target 이 되게. 해의 개수를 돌려주고 첫 해를 s 에 */
static int crc16_forge_suffix(const crc_params *p, const uint8_t *msg, size_t n, uint16_t target, uint8_t s[2]) {
    const uint32_t reg0 = crc_raw_update(p, p->init & 0xFFFFu, msg, n);   /* 메시지 부분은 한 번만 */
    int cnt = 0;
    for (uint32_t v = 0; v < 0x10000u; v++) {
        uint8_t t[2] = { (uint8_t)(v >> 8), (uint8_t)v };
        if (crc_finish(p, crc_raw_update(p, reg0, t, 2)) == target) { if (!cnt) { s[0] = t[0]; s[1] = t[1]; } cnt++; }
    }
    return cnt;
}

/* ---------------- 테스트 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

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

/* 바이트 → 읽는 순서 비트 */
static size_t to_bits(const crc_params *p, const uint8_t *d, size_t n, uint8_t *bits) {
    for (size_t i = 0; i < n; i++)
        for (unsigned k = 0; k < 8; k++) bits[8 * i + k] = p->refin ? (d[i] >> k) & 1u : (d[i] >> (7 - k)) & 1u;
    return 8 * n;
}

int main(void) {
    static uint8_t buf[600], bits[8 * 600 + 64];
    uint32_t seed = 0xA160011u;
    const uint8_t *chk = (const uint8_t *)"123456789";

    /* ===== 연습 1 ===== */
    int bad = 0;
    for (size_t i = 0; i < NCAT; i++) {
        size_t nb = to_bits(&CAT[i], chk, 9, bits);
        bad += crc_bits(&CAT[i], bits, nb) != CAT[i].check;
    }
    CHECK(bad == 0);
    bad = 0;
    for (int t = 0; t < 2000; t++) {                     /* 바이트 API 와 같아야 */
        const crc_params *p = &CAT[xs32(&seed) % NCAT];
        size_t n = xs32(&seed) % 40u;
        for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)xs32(&seed);
        size_t nb = to_bits(p, buf, n, bits);
        bad += crc_bits(p, bits, nb) != crc_bitwise(p, buf, n);
    }
    CHECK(bad == 0);
    /* 비트 codeword 의 residue: 메시지 비트(임의 길이) + CRC 비트(refout 이면 LSB 먼저) → 레지스터(refout 반영) == residue */
    int badres = 0;
    for (size_t i = 0; i < NCAT; i++) {
        const crc_params *p = &CAT[i];
        for (int t = 0; t < 40; t++) {
            size_t nb = xs32(&seed) % 61u;                /* 0..60 비트: 8의 배수가 아닌 길이 포함 */
            for (size_t k = 0; k < nb; k++) bits[k] = (uint8_t)(xs32(&seed) & 1u);
            uint32_t c = crc_bits(p, bits, nb);
            for (unsigned k = 0; k < p->width; k++)
                bits[nb + k] = p->refout ? (c >> k) & 1u : (c >> (p->width - 1 - k)) & 1u;
            uint32_t reg_out = crc_bits(p, bits, nb + p->width) ^ p->xorout;   /* = 레지스터(refout 반영) */
            badres += reg_out != p->residue;
        }
    }
    printf("ex1: residue via bit codewords, %zu algorithms x 40 (0..60-bit msgs), bad=%d\n", NCAT, badres);
    CHECK(badres == 0);

    /* ===== 연습 2: 앞쪽 0 과 뒤쪽 0 ===== */
    const crc_params *XM = &CAT[11], *IBM = &CAT[9], *MB = &CAT[10], *SDLC = &CAT[16], *C32 = &CAT[17];
    int lead_xm = 0, lead_ibm = 0, trail_mb = 0, trail_sdlc = 0, trail_c32 = 0;
    for (int t = 0; t < 1000; t++) {
        size_t n = 1 + xs32(&seed) % 32u, z = 1 + xs32(&seed) % 4u;
        uint8_t m[64] = {0};
        for (size_t i = 0; i < n; i++) m[z + i] = (uint8_t)xs32(&seed);
        lead_xm += crc_bitwise(XM, m + z, n) == crc_bitwise(XM, m, n + z);          /* 앞에 0 z 개 */
        lead_ibm += crc_bitwise(IBM, m + z, n) == crc_bitwise(IBM, m, n + z);
        uint8_t f[80] = {0};
        memcpy(f, m + z, n);
        size_t len = crc_append(MB, f, n);   memset(f + len, 0, z); trail_mb += crc_residue_ok(MB, f, len + z);
        memcpy(f, m + z, n);
        len = crc_append(SDLC, f, n);        memset(f + len, 0, z); trail_sdlc += crc_residue_ok(SDLC, f, len + z);
        memcpy(f, m + z, n);
        len = crc_append(C32, f, n);         memset(f + len, 0, z); trail_c32 += crc_residue_ok(C32, f, len + z);
    }
    printf("ex2: leading zeros unchanged: XMODEM %d/1000, IBM-3740 %d/1000\n", lead_xm, lead_ibm);
    printf("ex2: trailing zeros pass residue check: MODBUS %d/1000, IBM-SDLC %d/1000, CRC-32 %d/1000\n",
           trail_mb, trail_sdlc, trail_c32);
    CHECK(lead_xm == 1000 && lead_ibm == 0);
    CHECK(trail_mb == 1000 && trail_sdlc == 0 && trail_c32 == 0);

    /* ===== 연습 3: 주기 ===== */
    CHECK(poly_period(8, 0x07) == 127 && poly_period(16, 0x1021) == 32767 && poly_period(16, 0x8005) == 32767);
    CHECK(poly_period(15, 0x4599) == 127 && poly_period(5, 0x05) == 31 && poly_period(3, 0x3) == 7);
    CHECK(poly_period(8, 0x06) == 0);                     /* x | G → 주기 없음 */
    CHECK(max_bytes_all_2bit(8, 0x07) == 14 && max_bytes_all_2bit(16, 0x1021) == 4093);
    {   /* 경계 확인 (엔진): CRC-8/SMBUS 14바이트 데이터 → 모든 2비트 오류 검출, 15바이트 → 놓치는 쌍 존재 */
        const crc_params *p = &CAT[3];
        long miss14 = 0, miss15 = 0;
        for (int L = 14; L <= 15; L++) {
            uint8_t cw[32], b2[32];
            for (int i = 0; i < L; i++) cw[i] = (uint8_t)xs32(&seed);
            size_t len = crc_append(p, cw, (size_t)L), N = 8 * len;
            for (size_t r = 0; r < N; r++)
                for (size_t q = r + 1; q < N; q++) {
                    memcpy(b2, cw, len);
                    b2[r / 8] ^= (uint8_t)(0x80u >> (r % 8)); b2[q / 8] ^= (uint8_t)(0x80u >> (q % 8));
                    long m = crc_residue_ok(p, b2, len);
                    if (L == 14) miss14 += m; else miss15 += m;
                }
        }
        printf("ex3: CRC-8/SMBUS all 2-bit errors: 14-byte data missed %ld, 15-byte data missed %ld\n", miss14, miss15);
        CHECK(miss14 == 0 && miss15 == 1);
    }

    /* ===== 연습 4: 역추적 ===== */
    const crc_params HIDDEN = { "hidden", 16, 0x3D65, 0x1234, true, true, 0xFFFF, 0, 0 };
    static uint8_t fd[5][16];
    const size_t flen[5] = { 8, 8, 8, 8, 13 };
    frame fr[5];
    for (int i = 0; i < 5; i++) {
        for (size_t k = 0; k < flen[i]; k++) fd[i][k] = (uint8_t)xs32(&seed);
        fr[i].d = fd[i]; fr[i].n = flen[i]; fr[i].crc = (uint16_t)crc_bitwise(&HIDDEN, fd[i], flen[i]);
    }
    crc_params got[4];
    memset(got, 0, sizeof got);
    int nsol = crc16_recover(fr, got);
    for (int i = 0; i < nsol && i < 4; i++)
        printf("ex4: solution %d: poly=0x%04X init=0x%04X refin=refout=%d xorout=0x%04X\n",
               i + 1, got[i].poly, got[i].init, got[i].refin, got[i].xorout);
    /* G = (x+1)·P 이면 (init ⊕ P·?, xorout ⊕ ?) 쌍이 하나 더 생긴다 — 모든 메시지에서 같은 CRC 인지 확인 */
    int hit = -1, agree = 0;
    for (int i = 0; i < nsol && i < 4; i++)
        if (got[i].poly == HIDDEN.poly && got[i].init == HIDDEN.init && got[i].refin && got[i].xorout == HIDDEN.xorout) hit = i;
    if (nsol == 2) {
        const crc_params *other = &got[hit == 0 ? 1 : 0];
        for (int t = 0; t < 1000; t++) {
            size_t n = xs32(&seed) % 100u;
            for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)xs32(&seed);
            agree += crc_bitwise(other, buf, n) == crc_bitwise(&HIDDEN, buf, n);
        }
        printf("ex4: the other solution agrees with hidden on %d/1000 random messages (len 0..99)\n", agree);
    }
    CHECK(nsol == 2 && hit >= 0 && agree == 1000);

    /* ===== 연습 5: 위조 ===== */
    int badf = 0;
    for (int t = 0; t < 8; t++) {
        const crc_params *p = (t & 1) ? MB : IBM;
        uint8_t m[16];
        for (int i = 0; i < 10; i++) m[i] = (uint8_t)xs32(&seed);
        uint16_t target = (uint16_t)xs32(&seed);
        uint8_t s[2] = { 0, 0 };
        int cnt = crc16_forge_suffix(p, m, 10, target, s);
        m[10] = s[0]; m[11] = s[1];
        badf += cnt != 1 || crc_bitwise(p, m, 12) != target;
    }
    printf("ex5: forged 8 messages (2-byte suffix), bad=%d\n", badf);
    CHECK(badf == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
