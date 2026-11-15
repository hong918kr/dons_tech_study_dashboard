/* ARM-B06 비트 연산 명령: BIC, UBFX, BFI, CLZ, RBIT — 테스트 하네스 (starter.S / solution.S 공용)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 harness.c {starter|solution}.S shared_bugs.S -o /tmp/ARM-B06_v && /tmp/ARM-B06_v
 * 기준(reference)은 C-B04 스타일 C 매크로/함수 — asm 결과를 이것과 대조한다.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- asm 함수 원형 (solution.S / starter.S) ---------------- */
/* 가짜 NAND 컨트롤러 채널 상태 워드 NFC_STAT (32비트)
 *   [0] READY  [1] ECC_FAIL  [3:2] rsvd  [7:4] STATE  [15:8] ERRCNT  [23:16] TEMP(signed °C)  [27:24] rsvd  [31:28] VER */
/* Part A — 고정 위치 필드 + 가변 위치 필드 */
uint32_t stat_get_state(uint32_t stat);                      /* UBFX  bits 7:4                 */
int32_t  stat_get_temp(uint32_t stat);                       /* SBFX  bits 23:16 (부호 확장)    */
uint32_t stat_prep_state(uint32_t state);                    /* UBFIZ (state & 0xF) << 4        */
uint32_t stat_set_state(uint32_t stat, uint32_t state);      /* BFI   bits 7:4 ← state[3:0]     */
uint64_t field_get_var(uint64_t reg, unsigned lsb, unsigned width); /* 1<=width<=64, lsb+width<=64 */
/* Part B — 비트 순서 명령 */
uint32_t rbit32(uint32_t x);                                 /* RBIT w                           */
uint64_t rbit64(uint64_t x);                                 /* RBIT x                           */
int      hsb32(uint32_t x);                                  /* 31 - CLZ, hsb32(0) == -1         */
int      hsb64(uint64_t x);                                  /* 63 - CLZ, hsb64(0) == -1         */
uint32_t nibrev32(uint32_t x);                               /* 0x12345678 -> 0x87654321         */
uint32_t bswap32(uint32_t x);                                /* REV                              */
uint16_t bswap16(uint16_t x);                                /* REV + LSR #16                    */
/* Part C — 펌웨어: 상태 디코드 / 패치 / 빅 엔디언 SCSI CDB */
typedef struct {
    uint8_t ready, ecc_fail, state, errcnt;
    int8_t  temp;
    uint8_t ver;
} nfc_stat_t;
_Static_assert(offsetof(nfc_stat_t, temp) == 4 && offsetof(nfc_stat_t, ver) == 5 && sizeof(nfc_stat_t) == 6,
               "asm stat_decode assumes byte offsets 0..5");
void     stat_decode(uint32_t stat, nfc_stat_t *out);
uint32_t reg_clrset(uint32_t reg, uint32_t clr, uint32_t set);   /* (reg & ~clr) | set  : BIC + ORR */
uint32_t stat_patch(uint32_t stat, uint32_t new_state);          /* STATE=new, ERRCNT=0, ECC_FAIL=0 */
uint32_t cdb10_lba(const uint8_t *cdb);                          /* bytes 2..5 big-endian           */
uint16_t cdb10_len(const uint8_t *cdb);                          /* bytes 7..8 big-endian           */
void     cdb10_build(uint8_t *cdb, uint32_t lba, uint16_t len);  /* READ(10) = 0x28, 나머지 0        */
/* 일부러 틀린 버전 (shared_bugs.S) — 테스트가 잡아내는지 본다 */
uint64_t field_get_var_naive(uint64_t reg, unsigned lsb, unsigned width); /* mask = (1<<w)-1  */
uint32_t nibrev_rbit(uint32_t x);                                         /* RBIT 만 씀        */
int      hsb32_eor(uint32_t x);                                           /* clz ^ 31 (컴파일러식) */

/* ---------------- C 기준 구현 (C-B04 FIELD_* 매크로 압축본) ---------------- */
#define FIELD_LSB(mask)               ((uint32_t)(mask) & (~(uint32_t)(mask) + 1u))
#define FIELD_GET(mask, reg)          (((uint32_t)(reg) & (uint32_t)(mask)) / FIELD_LSB(mask))
#define FIELD_PREP(mask, val)         (((uint32_t)(val) * FIELD_LSB(mask)) & (uint32_t)(mask))
#define FIELD_REPLACE(mask, reg, val) (((uint32_t)(reg) & ~(uint32_t)(mask)) | FIELD_PREP(mask, val))

#define STAT_READY_Msk    0x00000001u
#define STAT_ECCFAIL_Msk  0x00000002u
#define STAT_STATE_Msk    0x000000F0u
#define STAT_ERRCNT_Msk   0x0000FF00u
#define STAT_TEMP_Msk     0x00FF0000u
#define STAT_VER_Msk      0xF0000000u

/* 8비트 2의 보수 → int32 (C 변환 규칙의 구현 정의 동작 없이: (v ^ 0x80) - 0x80) */
static int32_t sext8(uint32_t v) { return (int32_t)((v & 0xFFu) ^ 0x80u) - 0x80; }

static uint64_t ref_get64(uint64_t reg, unsigned lsb, unsigned width) {
    uint64_t m = (width >= 64) ? ~UINT64_C(0) : ((UINT64_C(1) << width) - 1u);  /* 1<<64 은 UB → 분기 */
    return (reg >> lsb) & m;
}
static uint64_t ref_rbit(uint64_t x, unsigned bits) {
    uint64_t r = 0;
    for (unsigned i = 0; i < bits; i++) r |= ((x >> i) & 1u) << (bits - 1u - i);
    return r;
}
static int ref_hsb(uint64_t x) { int h = -1; for (int i = 0; i < 64; i++) if ((x >> i) & 1u) h = i; return h; }
static uint32_t ref_nibrev32(uint32_t x) {
    uint32_t r = 0;
    for (unsigned i = 0; i < 8; i++) r |= ((x >> (4u * i)) & 0xFu) << (4u * (7u - i));
    return r;
}
static uint32_t ref_bswap32(uint32_t x) {
    return (x >> 24) | ((x >> 8) & 0xFF00u) | ((x << 8) & 0xFF0000u) | (x << 24);
}
static uint32_t ref_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;               /* 고정 시드 xorshift64 */
static uint64_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }
#define NRAND 200000

static bool random_partA(void) {          /* C-B04 매크로와 대조 */
    for (int i = 0; i < NRAND; i++) {
        uint32_t r = (uint32_t)rnd(), v = (uint32_t)rnd();
        if (stat_get_state(r) != FIELD_GET(STAT_STATE_Msk, r)) return false;
        if (stat_get_temp(r) != sext8(FIELD_GET(STAT_TEMP_Msk, r))) return false;
        if (stat_prep_state(v) != FIELD_PREP(STAT_STATE_Msk, v)) return false;
        if (stat_set_state(r, v) != FIELD_REPLACE(STAT_STATE_Msk, r, v)) return false;
    }
    return true;
}
static bool exhaustive_get_var(void) {    /* (lsb, width) 2080 쌍 전부 × 값 8개 */
    for (unsigned lsb = 0; lsb < 64; lsb++)
        for (unsigned w = 1; lsb + w <= 64; w++)
            for (int k = 0; k < 8; k++) {
                uint64_t r = (k == 0) ? ~UINT64_C(0) : rnd();
                if (field_get_var(r, lsb, w) != ref_get64(r, lsb, w)) {
                    printf("  lsb=%u w=%u r=%016llx asm=%016llx ref=%016llx\n", lsb, w,
                           (unsigned long long)r, (unsigned long long)field_get_var(r, lsb, w),
                           (unsigned long long)ref_get64(r, lsb, w));
                    return false;
                }
            }
    return true;
}
static bool random_partB(void) {
    for (int i = 0; i < NRAND; i++) {
        uint64_t x = rnd() >> (rnd() & 63);                    /* 다양한 최상위 비트 위치 */
        uint32_t y = (uint32_t)x;
        if (rbit32(y) != (uint32_t)ref_rbit(y, 32) || rbit64(x) != ref_rbit(x, 64)) return false;
        if (hsb32(y) != ref_hsb(y) || hsb64(x) != ref_hsb(x)) return false;
        if (nibrev32(y) != ref_nibrev32(y) || bswap32(y) != ref_bswap32(y)) return false;
        if (bswap16((uint16_t)y) != (uint16_t)(ref_bswap32(y & 0xFFFFu) >> 16)) return false;
    }
    for (int k = 0; k < 64; k++) {                             /* 2^k, 2^k-1, 2^(k+1)-1 경계 */
        uint64_t p = UINT64_C(1) << k;
        if (hsb64(p) != k || hsb64(p | (p - 1u)) != k || hsb64(p - 1u) != k - 1) return false;
        if (k < 32 && (hsb32((uint32_t)p) != k || hsb32((uint32_t)(p - 1u)) != k - 1)) return false;
    }
    return true;
}
static bool naive_agrees_below_64(void) { /* 버그 버전도 width < 64 에선 맞는다 → 경계값이 꼭 필요 */
    for (int i = 0; i < NRAND; i++) {
        unsigned w = 1u + (unsigned)(rnd() % 63u), lsb = (unsigned)(rnd() % (65u - w));
        uint64_t r = rnd();
        if (field_get_var_naive(r, lsb, w) != ref_get64(r, lsb, w)) return false;
    }
    return true;
}
static bool rbit_fools_palindromic_nibbles(void) { /* 니블이 {0,6,9,F} 뿐이면 RBIT == 니블 역순 */
    static const uint32_t pal[4] = { 0x0u, 0x6u, 0x9u, 0xFu };
    for (int i = 0; i < 10000; i++) {
        uint32_t x = 0, r = (uint32_t)rnd();
        for (int j = 0; j < 8; j++) x |= pal[(r >> (2 * j)) & 3u] << (4 * j);
        if (nibrev_rbit(x) != ref_nibrev32(x)) return false;
    }
    return true;
}
static bool decode_matches(uint32_t st) {
    nfc_stat_t s;
    memset(&s, 0xEE, sizeof s);
    stat_decode(st, &s);
    return s.ready == FIELD_GET(STAT_READY_Msk, st) && s.ecc_fail == FIELD_GET(STAT_ECCFAIL_Msk, st) &&
           s.state == FIELD_GET(STAT_STATE_Msk, st) && s.errcnt == FIELD_GET(STAT_ERRCNT_Msk, st) &&
           s.temp == sext8(FIELD_GET(STAT_TEMP_Msk, st)) && s.ver == FIELD_GET(STAT_VER_Msk, st);
}
static bool random_partC(void) {
    uint8_t buf[32];
    for (int i = 0; i < NRAND / 4; i++) {
        uint32_t st = (uint32_t)rnd(), a = (uint32_t)rnd(), b = (uint32_t)rnd(), ns = (uint32_t)rnd();
        if (!decode_matches(st)) return false;
        if (reg_clrset(st, a, b) != ((st & ~a) | b)) return false;
        uint32_t want = FIELD_REPLACE(STAT_STATE_Msk, st, ns);
        want = FIELD_REPLACE(STAT_ERRCNT_Msk, want, 0u) & ~STAT_ECCFAIL_Msk;
        if (stat_patch(st, ns) != want) return false;
        /* CDB: 정렬 안 된 주소(오프셋 0..15)에서 빌드 → 앞뒤 가드 바이트 보존 + 왕복 */
        unsigned off = (unsigned)(rnd() & 15u);
        uint16_t len = (uint16_t)rnd();
        memset(buf, 0xA5, sizeof buf);
        cdb10_build(buf + off, a, len);
        if (buf[off] != 0x28 || buf[off + 1] != 0 || buf[off + 6] != 0 || buf[off + 9] != 0) return false;
        if (ref_be32(buf + off + 2) != a || (uint16_t)((buf[off + 7] << 8) | buf[off + 8]) != len) return false;
        if (cdb10_lba(buf + off) != a || cdb10_len(buf + off) != len) return false;
        for (unsigned j = 0; j < sizeof buf; j++)
            if ((j < off || j >= off + 10) && buf[j] != 0xA5) return false;
    }
    return true;
}

int main(void) {
    /* Part A — 고정 필드 (예: NFC_STAT = 0x30E507A1: VER=3, TEMP=0xE5=-27, ERRCNT=7, STATE=0xA, READY=1) */
    CHECK(stat_get_state(0x30E507A1u) == 0xAu);
    CHECK(stat_get_temp(0x30E507A1u) == -27);
    CHECK(stat_get_temp(0x007F0000u) == 127 && stat_get_temp(0xFF80FFFFu) == -128);
    CHECK(stat_prep_state(0x5u) == 0x50u);
    CHECK(stat_prep_state(0x1Fu) == 0xF0u);                    /* 넘친 비트는 잘린다 */
    CHECK(stat_set_state(0x30E507A1u, 0x3u) == 0x30E50731u);
    CHECK(stat_set_state(0xFFFFFFFFu, 0x0u) == 0xFFFFFF0Fu);
    CHECK(random_partA());
    /* Part A — 가변 위치 필드 */
    CHECK(field_get_var(0xFEDCBA9876543210ull, 8, 8) == 0x32u);
    CHECK(field_get_var(0xFEDCBA9876543210ull, 60, 4) == 0xFu);
    CHECK(field_get_var(0xFEDCBA9876543210ull, 0, 64) == 0xFEDCBA9876543210ull);
    CHECK(field_get_var(0x8000000000000000ull, 63, 1) == 1u);
    CHECK(exhaustive_get_var());

    /* Part B — RBIT / CLZ / REV */
    CHECK(rbit32(0x00000001u) == 0x80000000u && rbit32(0x12345678u) == 0x1E6A2C48u);
    CHECK(rbit64(1u) == 0x8000000000000000ull);
    CHECK(hsb32(0u) == -1 && hsb32(1u) == 0 && hsb32(0x80000000u) == 31);
    CHECK(hsb64(0u) == -1 && hsb64(0x10u) == 4 && hsb64(UINT64_MAX) == 63);
    CHECK(nibrev32(0x12345678u) == 0x87654321u);
    CHECK(nibrev32(0xA0000000u) == 0x0000000Au && nibrev32(0x0000FF00u) == 0x00FF0000u);
    CHECK(bswap32(0x11223344u) == 0x44332211u);
    CHECK(bswap16(0xBEEFu) == 0xEFBEu);
    CHECK(random_partB());
    /* Part B — 일부러 틀린 버전을 테스트가 잡는가 */
    CHECK(field_get_var_naive(0xFEDCBA9876543210ull, 0, 64) != 0xFEDCBA9876543210ull); /* width=64 에서 0 */
    CHECK(naive_agrees_below_64());                            /* 무작위 width 1..63 으론 못 잡는다 */
    CHECK(nibrev_rbit(0x12345678u) != 0x87654321u);            /* RBIT ≠ 니블 역순 */
    CHECK(rbit_fools_palindromic_nibbles());                   /* 테스트 값을 잘못 고르면 통과해 버린다 */
    CHECK(hsb32_eor(0u) == 63 && hsb32_eor(0x80u) == 7);       /* x=0 에서만 틀림 */

    /* Part C — 펌웨어: 상태 디코드 / 패치 / 빅 엔디언 CDB */
    nfc_stat_t s;
    memset(&s, 0xEE, sizeof s);
    stat_decode(0x30E507A1u, &s);
    CHECK(s.ready == 1 && s.ecc_fail == 0 && s.state == 0xA && s.errcnt == 7 && s.temp == -27 && s.ver == 3);
    CHECK(decode_matches(0xFFFFFFFFu) && decode_matches(0u));
    CHECK(reg_clrset(0xFFFF0000u, 0x00F00000u, 0x00000005u) == 0xFF0F0005u);
    CHECK(stat_patch(0x30E507A3u, 0x4u) == 0x30E50041u);      /* STATE=4, ERRCNT=0, ECC_FAIL=0 */
    static const uint8_t cdb[10] = { 0x28, 0x00, 0x12, 0x34, 0x56, 0x78, 0x00, 0x00, 0x08, 0x00 };
    CHECK(cdb10_lba(cdb) == 0x12345678u && cdb10_len(cdb) == 8u);
    uint8_t out[10];
    memset(out, 0xEE, sizeof out);
    cdb10_build(out, 0x12345678u, 8u);
    CHECK(memcmp(out, cdb, sizeof cdb) == 0);
    CHECK(random_partC());

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
