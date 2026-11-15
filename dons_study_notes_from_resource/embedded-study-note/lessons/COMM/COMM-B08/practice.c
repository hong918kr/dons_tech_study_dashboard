/* COMM-B08 추가 연습 — 한 번에 검증
 *
 * 연습 1: init = 0 인 CRC 와 체크섬은 앞에 붙은 0x00 바이트를 못 본다 (길이 필드가 필요한 이유)
 * 연습 2: 다항식 주기 → "2비트 오류를 전부 잡는 최대 길이" (CRC-8 0x07: 119, CRC-16 0x1021: 32751 데이터 비트)
 * 연습 3: 2비트 미검출 개수 A_2 의 닫힌 식 vs 신드롬 전수 (CRC-8, 32바이트 → 147개, 0.4234 %)
 * 연습 4: 비트 순서 불일치 시 가장 짧은 미검출 와이어 버스트 (8 / 17 / 31) + CRC-32 를 MSB-first 와이어에 쓸 때의 실제 패턴
 * 연습 5: CRC-16/MODBUS (반사형 0x8005) 는 UART 와 짝이 맞는다 — check 0x4B37, LSB 와이어 버스트 <= 16 전수 0
 *         (연습 4 의 함수로: MSB-first 와이어에 쓰면 최단 미검출 버스트 11)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* ---- solution.c 의 CRC 엔진 축약판 ---- */
typedef struct { unsigned width; uint32_t poly, init; bool refin, refout; uint32_t xorout; } crc_param;
static const crc_param CRC8_SMBUS        = {  8, 0x07u,       0x00u,       false, false, 0x00u };
static const crc_param CRC16_CCITT_FALSE = { 16, 0x1021u,     0xFFFFu,     false, false, 0x0000u };
static const crc_param CRC16_MODBUS      = { 16, 0x8005u,     0xFFFFu,     true,  true,  0x0000u };
static const crc_param CRC32_ISO_HDLC    = { 32, 0x04C11DB7u, 0xFFFFFFFFu, true,  true,  0xFFFFFFFFu };

static uint32_t reflect(uint32_t v, unsigned w) {
    uint32_t r = 0;
    for (unsigned i = 0; i < w; i++) { r = (r << 1) | (v & 1u); v >>= 1; }
    return r;
}
static uint32_t crc_compute(const crc_param *p, const uint8_t *d, size_t n) {
    uint32_t top = 1u << (p->width - 1), mask = p->width == 32 ? 0xFFFFFFFFu : (1u << p->width) - 1u, c = p->init;
    for (size_t i = 0; i < n; i++) {
        c ^= (p->refin ? reflect(d[i], 8) : d[i]) << (p->width - 8);
        for (int k = 0; k < 8; k++) c = (c & top) ? (c << 1) ^ p->poly : c << 1;
        c &= mask;
    }
    if (p->refout) c = reflect(c, p->width);
    return (c ^ p->xorout) & mask;
}
static uint32_t sum8(const uint8_t *p, size_t n) { uint32_t s = 0; for (size_t i = 0; i < n; i++) s += p[i]; return s & 0xFFu; }
static uint32_t fletcher16(const uint8_t *p, size_t n) {
    uint32_t s1 = 0, s2 = 0;
    for (size_t i = 0; i < n; i++) { s1 = (s1 + p[i]) % 255u; s2 = (s2 + s1) % 255u; }
    return s2 << 8 | s1;
}

/* 코드워드 = data || crc (반사형은 LE, 아니면 BE) — 와이어 비트 순서 o: 0 = MSB-first, 1 = LSB-first */
#define MAXCW 72
static size_t encode(const crc_param *p, const uint8_t *d, size_t n, uint8_t *cw) {
    uint32_t v = crc_compute(p, d, n);
    size_t cb = p->width / 8;
    memcpy(cw, d, n);
    for (size_t i = 0; i < cb; i++) cw[n + i] = (uint8_t)(v >> (p->refout ? 8 * i : 8 * (cb - 1 - i)));
    return n + cb;
}
static uint32_t residual(const crc_param *p, const uint8_t *cw, size_t len) {
    size_t cb = p->width / 8, n = len - cb;
    uint32_t rx = 0;
    for (size_t i = 0; i < cb; i++) rx |= (uint32_t)cw[n + i] << (p->refout ? 8 * i : 8 * (cb - 1 - i));
    return crc_compute(p, cw, n) ^ rx;
}
static void flip_wire(uint8_t *b, unsigned i, int lsb) { b[i >> 3] ^= (uint8_t)(lsb ? 1u << (i & 7) : 0x80u >> (i & 7)); }
static int syndromes(const crc_param *p, size_t ndata, int lsb, uint32_t *syn) {
    uint8_t z[MAXCW] = {0}, cw[MAXCW];
    size_t len = encode(p, z, ndata, cw);
    for (unsigned i = 0; i < 8 * len; i++) { flip_wire(cw, i, lsb); syn[i] = residual(p, cw, len); flip_wire(cw, i, lsb); }
    return (int)(8 * len);
}
static uint64_t bursts(const uint32_t *syn, int nb, int L) {        /* solution.c Part C 와 같은 그레이 코드 전수 */
    uint64_t miss = 0;
    for (int s = 0; s + L <= nb; s++) {
        uint32_t x = syn[s] ^ (L >= 2 ? syn[s + L - 1] : 0u), m = L >= 2 ? (uint32_t)L - 2u : 0u;
        miss += x == 0;
        for (uint32_t g = 1; g < (1u << m); g++) { x ^= syn[s + 1 + __builtin_ctz(g)]; miss += x == 0; }
    }
    return miss;
}

/* ---- 연습 2: 주기 ---- */
/* x^n ≡ 1 (mod g) 인 가장 작은 n >= 1 (g 의 상수항 1 가정). limit 안에 없으면 0. */
static uint32_t crc_period(uint32_t poly, unsigned r, uint32_t limit) {
    uint32_t top = 1u << (r - 1), mask = r == 32 ? 0xFFFFFFFFu : (1u << r) - 1u, s = 1;
    for (uint32_t n = 1; n <= limit; n++) {
        s = (s & top) ? ((s << 1) ^ poly) & mask : (s << 1) & mask;
        if (s == 1u) return n;
    }
    return 0;
}

/* ---- 연습 3: A_2 ---- */
static uint32_t count_equal_pairs(const uint32_t *syn, int nb) {
    uint32_t c = 0;
    for (int i = 0; i < nb; i++) for (int j = i + 1; j < nb; j++) c += syn[i] == syn[j];
    return c;
}
static uint32_t a2_closed_form(uint32_t N, uint32_t period) {     /* 거리가 주기의 배수인 쌍: Σ_m (N − m·T) */
    uint32_t c = 0;
    for (uint32_t d = period; d < N; d += period) c += N - d;
    return c;
}

/* ---- 연습 4: 순서 불일치 ---- */
static int wire_pos(int p) { return 8 * (p / 8) + 7 - p % 8; }
/* e = q·g (q(0)=1, deg q <= 13) 를 8가지 바이트 정렬로 놓아 와이어 길이 최솟값.
 * 와이어 길이 <= r 인 미검출은 다항식 길이 <= r+14 → deg q <= 13 이라 빠짐없이 찾는다. */
static int min_mismatched_burst(uint32_t poly, int r, uint64_t *best_e, int *best_off) {
    uint64_t g = (1ull << r) | poly;
    int best = 1 << 30;
    for (uint32_t q = 1; q < (1u << 14); q += 2) {
        uint64_t e = 0;
        for (int j = 0; j < 14; j++) if (q >> j & 1u) e ^= g << j;
        int d = 63;
        while (!(e >> d & 1u)) d--;
        for (int o = 0; o < 8; o++) {
            int mn = 1 << 30, mx = -1;
            for (int j = 0; j <= d; j++)
                if (e >> j & 1u) { int w = wire_pos(o + d - j); if (w < mn) mn = w; if (w > mx) mx = w; }
            if (mx - mn + 1 < best) { best = mx - mn + 1; *best_e = e; *best_off = o; }
        }
    }
    return best;
}

int main(void) {
    uint32_t seed = 0x5B08u;
    static const uint8_t s9[] = "123456789";
    static uint32_t syn[MAXCW * 8];

    /* 연습 1: 앞에 0x00 세 바이트 */
    {
        uint8_t m[40] = {0}, z[43] = {0};
        int blind8 = 0, blind16 = 0, blind_sum = 0, blind_fl = 0;
        for (int t = 0; t < 1000; t++) {
            for (int i = 0; i < 40; i++) m[i] = (uint8_t)xs32(&seed);
            memcpy(z + 3, m, 40);
            blind8    += crc_compute(&CRC8_SMBUS, m, 40) == crc_compute(&CRC8_SMBUS, z, 43);
            blind16   += crc_compute(&CRC16_CCITT_FALSE, m, 40) == crc_compute(&CRC16_CCITT_FALSE, z, 43);
            blind_sum += sum8(m, 40) == sum8(z, 43);
            blind_fl  += fletcher16(m, 40) == fletcher16(z, 43);
        }
        printf("ex1 prepend 00 00 00: same check value in 1000 msgs: crc8(init 0)=%d crc16(init FFFF)=%d sum8=%d fletcher16=%d\n",
               blind8, blind16, blind_sum, blind_fl);
        CHECK(blind8 == 1000 && blind_sum == 1000 && blind_fl == 1000);
        CHECK(blind16 < 5);
    }
    /* 연습 2: 주기 → HD>=3 길이 한계 */
    {
        uint32_t t8 = crc_period(0x07u, 8, 1000), t16 = crc_period(0x1021u, 16, 70000);
        printf("ex2 period: crc8 0x07 = %u (data <= %u bits), crc16 0x1021 = %u (data <= %u bits)\n", t8, t8 - 8, t16, t16 - 16);
        CHECK(t8 == 127 && t8 - 8 == 119);
        CHECK(t16 == 32767 && t16 - 16 == 32751);
        CHECK(crc_period(0x8005u, 16, 70000) == 32767);
        /* 주기 경계 직접 확인: 데이터 15바이트 (코드워드 128비트) 에서 비트 0 과 127 을 뒤집으면 미검출 */
        uint8_t d[15], cw[16];
        for (int i = 0; i < 15; i++) d[i] = (uint8_t)xs32(&seed);
        size_t len = encode(&CRC8_SMBUS, d, 15, cw);
        flip_wire(cw, 0, 0); flip_wire(cw, 127, 0);
        CHECK(len == 16 && residual(&CRC8_SMBUS, cw, len) == 0);
    }
    /* 연습 3: A_2 */
    {
        int nb = syndromes(&CRC8_SMBUS, 32, 0, syn);
        uint32_t a2 = count_equal_pairs(syn, nb), cf = a2_closed_form((uint32_t)nb, 127);
        double rate = 100.0 * a2 / ((double)nb * (nb - 1) / 2);
        printf("ex3 crc8 32B: N=%d A_2 = %u (closed form %u), 2-bit undetected rate = %.4f %%\n", nb, a2, cf, rate);
        CHECK(nb == 264 && a2 == 147 && cf == 147);
        nb = syndromes(&CRC16_CCITT_FALSE, 64, 0, syn);
        CHECK(count_equal_pairs(syn, nb) == 0);
    }
    /* 연습 4: 순서 불일치 */
    {
        uint64_t e; int off;
        int l8 = min_mismatched_burst(0x07u, 8, &e, &off);
        int l16 = min_mismatched_burst(0x1021u, 16, &e, &off);
        int lmb = min_mismatched_burst(0x8005u, 16, &e, &off);
        int l32 = min_mismatched_burst(0x04C11DB7u, 32, &e, &off);
        printf("ex4 shortest undetected wire burst, mismatched order: crc8=%d crc16/0x1021=%d crc16/0x8005=%d crc32=%d\n", l8, l16, lmb, l32);
        CHECK(l8 == 8 && l16 == 17 && l32 == 31);
        CHECK(lmb == 11);                          /* Modbus CRC 를 MSB-first(SPI) 에 쓰면 11비트 버스트도 놓친다 */
        /* CRC-32 패턴을 실제 바이트로: 다항식 위치 p (LSB-first = 반사형의 자연 순서) → 바이트 p/8 의 bit p%8 */
        uint8_t pat[8] = {0};
        int d = 63;
        while (!(e >> d & 1u)) d--;
        for (int j = 0; j <= d; j++) if (e >> j & 1u) { int p = off + d - j; pat[p / 8] |= (uint8_t)(1u << (p % 8)); }
        printf("    crc32 pattern bytes (XOR at a byte boundary):");
        for (int i = 0; i < 8; i++) printf(" %02X", pat[i]);
        int lo = 1 << 30, hi = -1;                 /* MSB-first 와이어에서의 길이 */
        for (int i = 0; i < 64; i++) if (pat[i >> 3] & (0x80u >> (i & 7))) { if (i < lo) lo = i; if (i > hi) hi = i; }
        printf("  -> MSB-first wire span %d bits\n", hi - lo + 1);
        CHECK(hi - lo + 1 == 31);
        int undetected = 0;
        for (int t = 0; t < 1000; t++) {               /* 랜덤 메시지 어느 바이트 경계에 XOR 해도 미검출 */
            uint8_t m[48], cw[52];
            for (int i = 0; i < 48; i++) m[i] = (uint8_t)xs32(&seed);
            size_t len = encode(&CRC32_ISO_HDLC, m, 48, cw);
            unsigned at = xs32(&seed) % (unsigned)(len - 8 + 1);
            for (int i = 0; i < 8; i++) cw[at + i] ^= pat[i];
            undetected += residual(&CRC32_ISO_HDLC, cw, len) == 0;
        }
        CHECK(undetected == 1000);
    }
    /* 연습 5: CRC-16/MODBUS 와 UART */
    {
        CHECK(crc_compute(&CRC16_MODBUS, s9, 9) == 0x4B37u);
        int nb = syndromes(&CRC16_MODBUS, 8, 1, syn);
        uint64_t lsb = 0, b17;
        for (int L = 1; L <= 16; L++) lsb += bursts(syn, nb, L);
        b17 = bursts(syn, nb, 17);
        printf("ex5 crc16/modbus on LSB-first wire, 8B: bursts L<=16 undetected=%llu, L=17 undetected=%llu\n",
               (unsigned long long)lsb, (unsigned long long)b17);
        CHECK(lsb == 0 && b17 == (uint64_t)(nb - 17 + 1));
        /* 바이트 순서: Modbus 는 CRC 하위 바이트 먼저 → 이 encode 의 LE 배치와 같다 */
        uint8_t cw[11];
        encode(&CRC16_MODBUS, s9, 9, cw);
        CHECK(cw[9] == 0x37 && cw[10] == 0x4B);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
