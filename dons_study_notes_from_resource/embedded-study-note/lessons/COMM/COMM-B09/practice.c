/* COMM-B09 추가 연습 — 한 번에 검증
 *
 * 연습 1: 신형 CRC 유닛(POLYSIZE/POL/INIT/REV_IN/REV_OUT) + 소프트웨어 XOR 로 RevEng 카탈로그 CRC 7종 재현
 * 연습 2: 잔여값(residue) — 이미지 뒤에 CRC 를 붙이면 전체 CRC 는 데이터와 무관한 상수
 * 연습 3: 구형(F4) 유닛 상태 복원 — INIT 레지스터가 없을 때 "이 상태로 만드는 워드" 역산 (공유 유닛 문맥 전환)
 * 연습 4: 호스트 PC 에서 장치의 "네이티브" HW CRC (LE 워드 + 0 채움) 재현
 * 연습 5: 신형 유닛 상태 저장/복원 (REV_OUT 이 켜져 있을 때 INIT 에 무엇을 써야 하나)
 *
 * 이 파일은 단독으로 컴파일된다: solution.c 의 mock 과 SW CRC 를 축약해 다시 담았다 (COMM-B09 mock 축약판).
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
static uint32_t mask_n(unsigned n) { return n >= 32 ? 0xFFFFFFFFu : (1u << n) - 1u; }
static uint32_t reflect(uint32_t x, unsigned nbits) {
    uint32_t r = 0;
    for (unsigned i = 0; i < nbits; i++) { r = (r << 1) | (x & 1u); x >>= 1; }
    return r;
}
static uint32_t load_le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint32_t load_be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}
static uint32_t crc32_msb_update(uint32_t crc, const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint32_t)p[i] << 24;
        for (int k = 0; k < 8; k++) crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04C11DB7u : crc << 1;
    }
    return crc;
}
static uint32_t crc32_iso_hdlc(const uint8_t *p, size_t n) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    return ~crc;
}

/* 일반 Rocksoft/RevEng 모델 (width, poly, init, refin, refout, xorout) — 비트 단위, width 1..32 */
typedef struct { const char *name; unsigned width; uint32_t poly, init; bool refin, refout; uint32_t xorout, check; } crc_spec;
static uint32_t crc_generic(const crc_spec *s, const uint8_t *p, size_t n) {
    uint32_t m = mask_n(s->width), top = 1u << (s->width - 1u), crc = s->init & m;
    for (size_t i = 0; i < n; i++) {
        uint32_t b = s->refin ? reflect(p[i], 8) : p[i];
        for (int k = 7; k >= 0; k--) {
            uint32_t fb = ((crc & top) ? 1u : 0u) ^ ((b >> k) & 1u);
            crc = (crc << 1) & m;
            if (fb) crc ^= s->poly;
        }
    }
    if (s->refout) crc = reflect(crc, s->width);
    return (crc ^ s->xorout) & m;
}

/* ---------- STM32 CRC 유닛 mock (COMM-B09 mock 축약판) ---------- */
enum { REV_NONE = 0, REV_BYTE = 1, REV_HALF = 2, REV_WORD = 3 };
typedef struct {
    bool legacy; uint8_t polysize, rev_in; bool rev_out;
    uint32_t init, pol, state; unsigned bad_access;
} crc_unit;
static void hw_init(crc_unit *u, bool legacy) {
    memset(u, 0, sizeof *u);
    u->legacy = legacy; u->polysize = 32; u->pol = 0x04C11DB7u; u->init = 0xFFFFFFFFu; u->state = 0xFFFFFFFFu;
}
static void hw_reset(crc_unit *u) { u->state = u->legacy ? 0xFFFFFFFFu : (u->init & mask_n(u->polysize)); }
static uint32_t rev_in_apply(unsigned rev, uint32_t data, unsigned width) {
    unsigned unit = rev == REV_BYTE ? 8u : rev == REV_HALF ? 16u : rev == REV_WORD ? 32u : 0u;
    if (unit == 0) return data;
    if (unit > width) unit = width;
    uint32_t out = 0;
    for (unsigned s = 0; s < width; s += unit) out |= reflect((data >> s) & mask_n(unit), unit) << s;
    return out;
}
static void hw_write(crc_unit *u, uint32_t data, unsigned width) {
    if (u->legacy && width != 32) { u->bad_access++; return; }
    data &= mask_n(width);
    if (!u->legacy) data = rev_in_apply(u->rev_in, data, width);
    uint32_t m = mask_n(u->polysize), top = 1u << (u->polysize - 1u);
    for (int i = (int)width - 1; i >= 0; i--) {
        uint32_t fb = ((u->state & top) ? 1u : 0u) ^ ((data >> i) & 1u);
        u->state = (u->state << 1) & m;
        if (fb) u->state ^= u->pol & m;
    }
}
static uint32_t hw_read(const crc_unit *u) {
    return (!u->legacy && u->rev_out) ? reflect(u->state, u->polysize) : u->state;
}

/* ===== 연습 1: 카탈로그 CRC → 유닛 설정 ===== */
typedef struct { uint8_t polysize; uint32_t pol, init; uint8_t rev_in; bool rev_out; uint32_t sw_xor; } unit_cfg;

/* RevEng 매개변수를 유닛 설정으로: refin → REV_IN=BYTE (바이트 쓰기 기준), refout → REV_OUT, xorout 은 SW */
static unit_cfg cfg_from_spec(const crc_spec *s) {
    unit_cfg c = { (uint8_t)s->width, s->poly, s->init, s->refin ? REV_BYTE : REV_NONE, s->refout, s->xorout };
    return c;
}
static uint32_t unit_crc(crc_unit *u, const unit_cfg *c, const uint8_t *p, size_t n) {
    hw_init(u, false);
    u->polysize = c->polysize; u->pol = c->pol; u->init = c->init; u->rev_in = c->rev_in; u->rev_out = c->rev_out;
    hw_reset(u);
    for (size_t i = 0; i < n; i++) hw_write(u, p[i], 8);
    return (hw_read(u) ^ c->sw_xor) & mask_n(c->polysize);
}

/* ===== 연습 3: 구형 유닛 — RESET 직후 한 워드로 원하는 상태 만들기 =====
 * 32비트 워드 w 를 쓰면 state' = G(state ^ w), G = "입력 0 으로 32번 시프트". G 는 역함수가 있다(P 의 x^0 항 = 1). */
static uint32_t unshift32(uint32_t s) {                  /* G^{-1}: 한 비트씩 32번 되돌리기 */
    for (int k = 0; k < 32; k++)
        s = (s & 1u) ? ((s ^ 0x04C11DB7u) >> 1) | 0x80000000u : s >> 1;
    return s;
}
static uint32_t f4_word_for_state(uint32_t target) { return 0xFFFFFFFFu ^ unshift32(target); }
static void f4_restore(crc_unit *u, uint32_t saved) { hw_reset(u); hw_write(u, f4_word_for_state(saved), 32); }

/* ===== 연습 4: 장치의 "네이티브" CRC 를 PC 에서 (구형 유닛, LE 워드 로드, 꼬리 0 채움) ===== */
static uint32_t stm32_native_crc(const uint8_t *p, size_t n) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i += 4) {
        uint8_t w[4] = { 0, 0, 0, 0 };
        for (size_t k = 0; k < 4 && i + k < n; k++) w[k] = p[i + k];
        uint8_t fed[4] = { w[3], w[2], w[1], w[0] };      /* LE 로 읽은 워드의 MSB = 바이트 3 이 먼저 */
        crc = crc32_msb_update(crc, fed, 4);
    }
    return crc;
}
static uint32_t device_native_crc(crc_unit *u, const uint8_t *p, size_t n) {   /* 장치 쪽 (mock) */
    hw_reset(u);
    for (size_t i = 0; i < n; i += 4) {
        uint8_t w[4] = { 0, 0, 0, 0 };
        memcpy(w, p + i, n - i < 4 ? n - i : 4);
        hw_write(u, load_le32(w), 32);
    }
    return hw_read(u);
}

/* ===== 연습 5: 신형 유닛 상태 저장/복원 ===== */
typedef struct { uint32_t dr, init; } crc_ctx;
static crc_ctx modern_save(const crc_unit *u) { crc_ctx c = { hw_read(u), u->init }; return c; }
static void modern_restore(crc_unit *u, const crc_ctx *c) {
    u->init = u->rev_out ? reflect(c->dr, u->polysize) : c->dr;   /* DR 은 반사되어 보이므로 되돌려서 */
    hw_reset(u);
    u->init = c->init;                                            /* 다음 계산을 위해 원래 INIT 복구 */
}

int main(void) {
    uint32_t seed = 0xB09B09u;
    static uint8_t buf[1100];
    crc_unit u;
    const uint8_t *chk = (const uint8_t *)"123456789";

    /* ===== 연습 1 ===== */
    static const crc_spec SPECS[] = {
        { "CRC-32/ISO-HDLC", 32, 0x04C11DB7u, 0xFFFFFFFFu, true,  true,  0xFFFFFFFFu, 0xCBF43926u },
        { "CRC-32/MPEG-2",   32, 0x04C11DB7u, 0xFFFFFFFFu, false, false, 0x00000000u, 0x0376E6E7u },
        { "CRC-32/BZIP2",    32, 0x04C11DB7u, 0xFFFFFFFFu, false, false, 0xFFFFFFFFu, 0xFC891918u },
        { "CRC-32/ISCSI",    32, 0x1EDC6F41u, 0xFFFFFFFFu, true,  true,  0xFFFFFFFFu, 0xE3069283u },
        { "CRC-16/MODBUS",   16, 0x8005u,     0xFFFFu,     true,  true,  0x0000u,     0x4B37u },
        { "CRC-16/IBM-3740", 16, 0x1021u,     0xFFFFu,     false, false, 0x0000u,     0x29B1u },
        { "CRC-16/XMODEM",   16, 0x1021u,     0x0000u,     false, false, 0x0000u,     0x31C3u },
        { "CRC-8/SMBUS",      8, 0x07u,       0x00u,       false, false, 0x00u,       0xF4u },
        { "CRC-7/MMC",        7, 0x09u,       0x00u,       false, false, 0x00u,       0x75u },
    };
    for (size_t s = 0; s < sizeof SPECS / sizeof SPECS[0]; s++) {
        const crc_spec *sp = &SPECS[s];
        unit_cfg c = cfg_from_spec(sp);
        uint32_t g = crc_generic(sp, chk, 9), h = unit_crc(&u, &c, chk, 9);
        int bad = 0;
        for (int t = 0; t < 300; t++) {
            size_t n = xs32(&seed) % 100u;
            for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)xs32(&seed);
            if (unit_crc(&u, &c, buf, n) != crc_generic(sp, buf, n)) bad++;
        }
        printf("%-16s POLYSIZE=%2u POL=0x%08X INIT=0x%08X REV_IN=%s REV_OUT=%d swXOR=0x%08X  check=0x%08X\n",
               sp->name, c.polysize, (unsigned)c.pol, (unsigned)c.init, c.rev_in ? "BYTE" : "NONE", c.rev_out,
               (unsigned)c.sw_xor, (unsigned)h);
        CHECK(g == sp->check && h == sp->check && bad == 0);
    }

    /* ===== 연습 2: 잔여값 ===== */
    {
        int bad_res = 0, bad_raw = 0;
        for (int t = 0; t < 1000; t++) {
            size_t n = xs32(&seed) % 1000u;
            for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)xs32(&seed);
            uint32_t c = crc32_iso_hdlc(buf, n);
            for (int k = 0; k < 4; k++) buf[n + (size_t)k] = (uint8_t)(c >> (8 * k));   /* CRC 를 LE 로 붙임 */
            if (crc32_iso_hdlc(buf, n + 4) != 0x2144DF1Cu) bad_res++;
            /* 같은 일을 HW 로: REV_IN=BYTE, REV_OUT=1, INIT=FFFFFFFF, 최종 XOR 없이 DR 을 그대로 비교 */
            hw_init(&u, false); u.rev_in = REV_BYTE; u.rev_out = true; hw_reset(&u);
            for (size_t i = 0; i < n + 4; i++) hw_write(&u, buf[i], 8);
            if (hw_read(&u) != 0xDEBB20E3u) bad_raw++;
        }
        printf("residue: 1000 msgs, crc(msg||crc_le) != 0x2144DF1C: %d, raw DR != 0xDEBB20E3: %d\n", bad_res, bad_raw);
        CHECK(bad_res == 0);
        CHECK(bad_raw == 0);
        CHECK((0x2144DF1Cu ^ 0xFFFFFFFFu) == 0xDEBB20E3u);
    }

    /* ===== 연습 3: 구형 유닛 상태 복원 ===== */
    hw_init(&u, true);
    {
        int bad = 0;
        for (int t = 0; t < 1000; t++) {
            uint32_t target = xs32(&seed);
            f4_restore(&u, target);
            if (hw_read(&u) != target) bad++;
        }
        f4_restore(&u, 0xFFFFFFFFu);                      /* 리셋 상태로 되돌리는 워드는 0x00000000 이 아니다 */
        CHECK(hw_read(&u) == 0xFFFFFFFFu && f4_word_for_state(0xFFFFFFFFu) != 0u);
        CHECK(bad == 0);
        /* 문맥 전환: 긴 계산 A 를 하다가 ISR 이 B 를 통째로 계산, A 를 이어서 끝낸다 */
        for (int i = 0; i < 1024; i++) buf[i] = (uint8_t)xs32(&seed);
        hw_reset(&u);
        for (int i = 0; i < 512; i += 4) hw_write(&u, load_be32(buf + i), 32);
        uint32_t saved = hw_read(&u);                     /* 선점 직전 저장 */
        hw_reset(&u);                                     /* ISR: 다른 데이터 B (뒤쪽 64바이트) */
        for (int i = 960; i < 1024; i += 4) hw_write(&u, load_be32(buf + i), 32);
        uint32_t b = hw_read(&u);
        f4_restore(&u, saved);                            /* 복원 후 A 계속 */
        for (int i = 512; i < 1024; i += 4) hw_write(&u, load_be32(buf + i), 32);
        uint32_t ref_a = crc32_msb_update(0xFFFFFFFFu, buf, 1024);
        CHECK(hw_read(&u) == ref_a);
        CHECK(b == crc32_msb_update(0xFFFFFFFFu, buf + 960, 64));
    }

    /* ===== 연습 4: 네이티브 CRC 재현 ===== */
    hw_init(&u, true);
    {
        int bad = 0;
        for (int t = 0; t < 1000; t++) {
            size_t n = xs32(&seed) % 300u;
            for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)xs32(&seed);
            if (stm32_native_crc(buf, n) != device_native_crc(&u, buf, n)) bad++;
        }
        uint32_t nat = stm32_native_crc(chk, 9);
        printf("native (LE words, zero pad) \"123456789\" = 0x%08X, 1000 random mismatches=%d\n", (unsigned)nat, bad);
        CHECK(bad == 0);
        CHECK(nat == 0xAFF19057u && nat != 0x0376E6E7u && nat != 0xCBF43926u);
    }

    /* ===== 연습 5: 신형 유닛 상태 저장/복원 (REV_OUT 켜짐) ===== */
    {
        int bad = 0;
        for (int t = 0; t < 500; t++) {
            size_t n = 2 + xs32(&seed) % 500u, cut = 1 + xs32(&seed) % (n - 1);
            for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)xs32(&seed);
            hw_init(&u, false); u.rev_in = REV_BYTE; u.rev_out = true; hw_reset(&u);
            for (size_t i = 0; i < cut; i++) hw_write(&u, buf[i], 8);
            crc_ctx ctx = modern_save(&u);
            hw_reset(&u);                                 /* 다른 태스크가 유닛을 씀 */
            for (size_t i = 0; i < 37; i++) hw_write(&u, (uint8_t)(i * 11u), 8);
            modern_restore(&u, &ctx);
            for (size_t i = cut; i < n; i++) hw_write(&u, buf[i], 8);
            if ((hw_read(&u) ^ 0xFFFFFFFFu) != crc32_iso_hdlc(buf, n)) bad++;
            hw_reset(&u);                                 /* INIT 이 원래 값으로 돌아왔는지 */
            if (hw_read(&u) != 0xFFFFFFFFu) bad++;
        }
        printf("modern save/restore with REV_OUT: 500 runs, bad=%d\n", bad);
        CHECK(bad == 0);
        /* 함정: 반사된 DR 을 그대로 INIT 에 쓰면 틀린다 */
        memcpy(buf, chk, 9);
        hw_init(&u, false); u.rev_in = REV_BYTE; u.rev_out = true; hw_reset(&u);
        for (int i = 0; i < 4; i++) hw_write(&u, buf[i], 8);
        uint32_t dr = hw_read(&u);
        u.init = dr; hw_reset(&u); u.init = 0xFFFFFFFFu;  /* 잘못된 복원 */
        for (int i = 4; i < 9; i++) hw_write(&u, buf[i], 8);
        CHECK((hw_read(&u) ^ 0xFFFFFFFFu) != 0xCBF43926u);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
