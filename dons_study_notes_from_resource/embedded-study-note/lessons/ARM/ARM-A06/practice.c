/* ARM-A06 추가 연습 — 한 번에 검증
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c shared_ref.c shared_bugs.c -lz -o pr && ./pr
 * 연습 1: CRC-32(IEEE) 3-way — zlib crc32() 와 대조 (상수만 IEEE 다항식으로)
 * 연습 2: "fold-ahead" 3-way — 세 체인을 모두 0 에서 시작, 이어받은 상태는 x^(24B) 로 한 번에 민다
 * 연습 3: STM32 CRC 유닛을 crc32x 로 — 워드 두 개를 rbit64 한 번으로
 * 연습 4: CRC-32C 잔여값(residue) — 수신 측 검사를 상수 비교 하나로
 * 연습 5: 시프트를 명령만으로 (0 을 n 바이트 먹이기, O(n)) vs PMULL (O(log n)) — 같은 값인지
 * (solution.c 의 함수는 축약 복사본으로 다시 넣었다: 레슨 파일은 서로 include 하지 않는다)
 */
#include <arm_acle.h>
#include <arm_neon.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

#define POLY_IEEE_R 0xEDB88320u
#define POLY_C_R    0x82F63B78u
#define HW __attribute__((target("+crc,+aes")))

void     ref_init(void);
uint32_t ref_slice8(uint32_t crc, const void *p, size_t n, uint32_t poly_r);
uint32_t xpow_mod(uint64_t e, uint32_t poly_r);
uint32_t ref_mpeg2_bitwise(uint32_t state, const void *p, size_t n);

static inline uint64_t ld64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }

/* (solution.c 축약판) 1-way 조건화 없는 갱신: 정렬 머리 대신 바이트 단위로 8 B 정렬까지 */
HW static uint32_t raw1(uint32_t c, const uint8_t *p, size_t n, int cas) {
    while (n && ((uintptr_t)p & 7)) { c = cas ? __crc32cb(c, *p) : __crc32b(c, *p); p++; n--; }
    for (; n >= 8; p += 8, n -= 8) c = cas ? __crc32cd(c, ld64(p)) : __crc32d(c, ld64(p));
    while (n--) { c = cas ? __crc32cb(c, *p) : __crc32b(c, *p); p++; }
    return c;
}
/* (solution.c 축약판) c·k·x^33 mod P */
HW static uint32_t mulk(uint32_t c, uint32_t k, int cas) {
    uint64_t prod = vgetq_lane_u64(vreinterpretq_u64_p128(vmull_p64((poly64_t)c, (poly64_t)k)), 0);
    return cas ? __crc32cd(0, prod) : __crc32d(0, prod);
}
HW static uint32_t shift_pmull(uint32_t c, uint64_t n, int cas) {
    if (n < 5) { while (n--) c = cas ? __crc32cb(c, 0) : __crc32b(c, 0); return c; }
    return mulk(c, xpow_mod(8 * n - 33, cas ? POLY_C_R : POLY_IEEE_R), cas);
}

/* ---------- 연습 1: CRC-32 (IEEE) 3-way ---------- */
static const struct { size_t blk; uint32_t k1, k2; } K3I[2] = {
    { 1024, 0xBBF2F6D6u, 0x7B4AA8B7u },        /* x^(8B−33), x^(16B−33) mod P_ieee */
    {  128, 0x910EEEC1u, 0xE95C1271u },
};
HW uint32_t crc32_3way(uint32_t crc, const void *buf, size_t n) {
    const uint8_t *p = buf;
    uint32_t c = ~crc;
    size_t head = (size_t)(-(uintptr_t)p & 7);
    if (head > n) head = n;
    c = raw1(c, p, head, 0); p += head; n -= head;
    for (int t = 0; t < 2; t++) {
        size_t B = K3I[t].blk;
        while (n >= 3 * B) {
            uint32_t c0 = c, c1 = 0, c2 = 0;
            for (size_t i = 0; i < B; i += 8) {
                c0 = __crc32d(c0, ld64(p + i));
                c1 = __crc32d(c1, ld64(p + B + i));
                c2 = __crc32d(c2, ld64(p + 2 * B + i));
            }
            c = mulk(c0, K3I[t].k2, 0) ^ mulk(c1, K3I[t].k1, 0) ^ c2;
            p += 3 * B; n -= 3 * B;
        }
    }
    return ~raw1(c, p, n, 0);
}

/* ---------- 연습 2: fold-ahead 3-way (CRC-32C) ---------- */
static const struct { size_t blk; uint32_t k1, k2, k3; } KFA[2] = {
    { 1024, 0x170076FAu, 0xA51B6135u, 0x359674F7u },   /* k3 = x^(24B−33) */
    {  128, 0x0D3B6092u, 0xB9E02B86u, 0xD270F1A2u },
};
HW uint32_t crc32c_3way_fa(uint32_t crc, const void *buf, size_t n) {
    const uint8_t *p = buf;
    uint32_t c = ~crc;
    size_t head = (size_t)(-(uintptr_t)p & 7);
    if (head > n) head = n;
    c = raw1(c, p, head, 1); p += head; n -= head;
    for (int t = 0; t < 2; t++) {
        size_t B = KFA[t].blk;
        while (n >= 3 * B) {
            uint32_t c0 = 0, c1 = 0, c2 = 0;            /* 세 체인 모두 0 에서 → 앞 덩어리의 combine 을 기다리지 않는다 */
            for (size_t i = 0; i < B; i += 8) {
                c0 = __crc32cd(c0, ld64(p + i));
                c1 = __crc32cd(c1, ld64(p + B + i));
                c2 = __crc32cd(c2, ld64(p + 2 * B + i));
            }
            c = mulk(c, KFA[t].k3, 1) ^ mulk(c0, KFA[t].k2, 1) ^ mulk(c1, KFA[t].k1, 1) ^ c2;
            p += 3 * B; n -= 3 * B;
        }
    }
    return ~raw1(c, p, n, 1);
}

/* ---------- 연습 3: STM32 CRC 유닛, 워드 2개씩 ---------- */
HW uint32_t stm32_crc_hw64(const uint32_t *w, size_t nw) {
    uint32_t s = 0xFFFFFFFFu;
    size_t i = 0;
    for (; i + 2 <= nw; i += 2)                         /* w[i] 의 비트 31 이 맨 먼저 → rbit64(w[i]:w[i+1]) */
        s = __crc32d(s, __rbitll(((uint64_t)w[i] << 32) | w[i + 1]));
    if (i < nw) s = __crc32w(s, __rbit(w[i]));
    return __rbit(s);
}
HW static uint32_t stm32_crc_hw32(const uint32_t *w, size_t nw) {     /* solution.c 와 같은 워드 단위판 */
    uint32_t s = 0xFFFFFFFFu;
    for (size_t i = 0; i < nw; i++) s = __crc32w(s, __rbit(w[i]));
    return __rbit(s);
}

/* ---------- 연습 4: CRC-32C 잔여값 ---------- */
static uint32_t crc32c_sw(uint32_t crc, const void *p, size_t n) { return ref_slice8(crc, p, n, POLY_C_R); }
/* 메시지 뒤에 CRC 를 리틀 엔디언으로 붙인다 (iSCSI 데이터 다이제스트·이더넷 FCS 와 같은 순서) */
static void seal_le(uint8_t *m, size_t n) {
    uint32_t c = crc32c_sw(0, m, n);
    m[n] = (uint8_t)c; m[n + 1] = (uint8_t)(c >> 8); m[n + 2] = (uint8_t)(c >> 16); m[n + 3] = (uint8_t)(c >> 24);
}
#define CRC32C_RESIDUE 0x48674BC7u   /* = ~0xB798B438 (RevEng CRC-32/ISCSI residue, xorout 전) — 아래 코드가 확인 */

/* ---------- 연습 5: 명령만으로 시프트 (O(n)) ---------- */
HW uint32_t shift_linear(uint32_t c, uint64_t n, int cas) {
    for (; n >= 8; n -= 8) c = cas ? __crc32cd(c, 0) : __crc32d(c, 0);   /* 0 8 바이트 = x^64 곱 */
    while (n--) c = cas ? __crc32cb(c, 0) : __crc32b(c, 0);
    return c;
}

/* ---------- 하네스 ---------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) g_pass++; \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }
static uint8_t g_buf[200 * 1024 + 64];

int main(void) {
    ref_init();
    for (size_t i = 0; i < sizeof g_buf; i++) g_buf[i] = (uint8_t)rnd();

    /* 연습 1 */
    for (int t = 0; t < 2; t++) {
        CHECK(K3I[t].k1 == xpow_mod(8 * K3I[t].blk - 33, POLY_IEEE_R));
        CHECK(K3I[t].k2 == xpow_mod(16 * K3I[t].blk - 33, POLY_IEEE_R));
    }
    long b1 = 0;
    for (size_t n = 0; n <= 4096; n++) {
        size_t a = rnd() & 15;
        uint32_t c0 = (uint32_t)rnd();
        b1 += crc32_3way(c0, g_buf + a, n) != (uint32_t)crc32(c0, g_buf + a, (uInt)n);
    }
    for (int t = 0; t < 100; t++) {
        size_t n = rnd() % (200 * 1024), a = rnd() & 63;
        b1 += crc32_3way(0, g_buf + a, n) != (uint32_t)crc32(0, g_buf + a, (uInt)n);
    }
    CHECK(b1 == 0);
    CHECK(crc32_3way(0, "123456789", 9) == 0xCBF43926u);

    /* 연습 2 */
    for (int t = 0; t < 2; t++) CHECK(KFA[t].k3 == xpow_mod(24 * KFA[t].blk - 33, POLY_C_R));
    long b2 = 0;
    for (size_t n = 0; n <= 4096; n++) {
        size_t a = rnd() & 15;
        uint32_t c0 = (uint32_t)rnd();
        b2 += crc32c_3way_fa(c0, g_buf + a, n) != crc32c_sw(c0, g_buf + a, n);
    }
    for (int t = 0; t < 100; t++) {
        size_t n = rnd() % (200 * 1024), a = rnd() & 63;
        b2 += crc32c_3way_fa(0, g_buf + a, n) != crc32c_sw(0, g_buf + a, n);
    }
    CHECK(b2 == 0);

    /* 연습 3 */
    long b3 = 0;
    for (int t = 0; t < 1000; t++) {
        uint32_t w[33];
        uint8_t be[132];
        size_t nw = rnd() % 34;
        for (size_t i = 0; i < nw; i++) {
            w[i] = (uint32_t)rnd();
            be[4 * i] = (uint8_t)(w[i] >> 24); be[4 * i + 1] = (uint8_t)(w[i] >> 16);
            be[4 * i + 2] = (uint8_t)(w[i] >> 8); be[4 * i + 3] = (uint8_t)w[i];
        }
        uint32_t want = ref_mpeg2_bitwise(0xFFFFFFFFu, be, 4 * nw);
        b3 += (stm32_crc_hw64(w, nw) != want) + (stm32_crc_hw32(w, nw) != want);
    }
    CHECK(b3 == 0);

    /* 연습 4 */
    uint8_t m[2100];
    long b4 = 0, flips_missed = 0;
    for (int t = 0; t < 500; t++) {
        size_t n = rnd() % 2000;
        for (size_t i = 0; i < n; i++) m[i] = (uint8_t)rnd();
        seal_le(m, n);
        b4 += crc32c_sw(0, m, n + 4) != CRC32C_RESIDUE;
        size_t bit = rnd() % (8 * (n + 4));               /* 1 비트 오류 (CRC 필드 포함 어디든) */
        m[bit / 8] ^= (uint8_t)(1u << (bit % 8));
        flips_missed += crc32c_sw(0, m, n + 4) == CRC32C_RESIDUE;
    }
    printf("residue of msg||crc_le: 0x%08X (want 0x%08X), single-bit errors missed: %ld/500\n",
           crc32c_sw(0, "123456789\x83\x92\x06\xE3", 13), CRC32C_RESIDUE, flips_missed);
    CHECK(b4 == 0);
    CHECK(flips_missed == 0);

    /* 연습 5 */
    long b5 = 0;
    for (uint64_t n = 0; n <= 2000; n++)
        for (int cas = 0; cas < 2; cas++) {
            uint32_t c = (uint32_t)rnd();
            b5 += shift_linear(c, n, cas) != shift_pmull(c, n, cas);
        }
    CHECK(b5 == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
