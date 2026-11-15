/* COMM-B09 하드웨어 CRC 유닛과 소프트웨어 CRC 맞추기
 *
 * Part A: 소프트웨어 기준 CRC-32 두 가지 — ISO-HDLC(zlib·Ethernet, 반사형)와 MPEG-2(비반사, STM32 기본) —
 *         그리고 STM32 CRC 주변장치 mock (DR 32/16/8비트 쓰기, CRC_INIT, CRC_POL/POLYSIZE, REV_IN, REV_OUT)
 * Part B: 설정 탐색 — 바이트 순서 × REV_IN × REV_OUT × INIT × 최종 XOR 64가지 중 SW 와 같은 것 찾기,
 *         결과값만 보고 빠뜨린 설정 추리(crc_diagnose), 구형(F1/F2/F4) 유닛에서 4의 배수가 아닌 꼬리
 * Part C: 신형 유닛 드라이버(begin/feed/end, 임의 청크·비정렬 버퍼) + 펌웨어 이미지 CRC 검증
 *
 * mock 의 레지스터 의미는 STM32 레퍼런스 매뉴얼(RM)을 축약한 것이고, 이 파일의 정의가 테스트의 기준이다.
 * 실제 칩의 세부(좁은 쓰기 + REV_IN, POLYSIZE < 32 의 REV_OUT 등)는 해당 RM 확인.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
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

/* 하위 n 비트 마스크 (n = 1..32). n == 32 에서 1u << 32 는 UB 라 따로 */
static uint32_t mask_n(unsigned n) { return n >= 32 ? 0xFFFFFFFFu : (1u << n) - 1u; }

/* ---------------- Part A: 비트 반사와 소프트웨어 CRC-32 ---------------- */

/* x 의 하위 nbits 비트를 좌우로 뒤집는다 (bit 0 ↔ bit nbits-1). nbits = 1..32.
 * Cortex-M3/M4/M7 에서는 32비트 반사가 RBIT 명령 하나 (__RBIT). */
static uint32_t reflect(uint32_t x, unsigned nbits) {
    uint32_t r = 0;
    for (unsigned i = 0; i < nbits; i++) { r = (r << 1) | (x & 1u); x >>= 1; }
    return r;
}

/* MSB-first (비반사) CRC-32, 다항식 0x04C11DB7. 초기값·최종 XOR 없이 "상태"만 갱신한다.
 * 바이트의 bit7 이 먼저 들어간다. */
static uint32_t crc32_msb_update(uint32_t crc, const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint32_t)p[i] << 24;
        for (int k = 0; k < 8; k++)
            crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04C11DB7u : crc << 1;
    }
    return crc;
}

/* LSB-first (반사) CRC-32, 반사 다항식 0xEDB88320 = reflect(0x04C11DB7, 32). 바이트의 bit0 이 먼저. */
static uint32_t crc32_lsb_update(uint32_t crc, const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++)
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    return crc;
}

/* RevEng 카탈로그 매개변수 — 두 함수 모두 width 32, poly 0x04C11DB7, init 0xFFFFFFFF
 *   CRC-32/ISO-HDLC : refin = refout = true,  xorout 0xFFFFFFFF, check 0xCBF43926 (zlib, Ethernet FCS)
 *   CRC-32/MPEG-2   : refin = refout = false, xorout 0,          check 0x0376E6E7 (STM32 CRC 유닛 기본값) */
static uint32_t crc32_iso_hdlc(const uint8_t *p, size_t n) { return ~crc32_lsb_update(0xFFFFFFFFu, p, n); }
static uint32_t crc32_mpeg2(const uint8_t *p, size_t n)    { return crc32_msb_update(0xFFFFFFFFu, p, n); }

/* ---------- STM32 CRC 유닛 mock ----------
 * legacy = true : F1/F2/F4 형. 다항식 0x04C11DB7·초기값 0xFFFFFFFF 고정, 반사 없음, DR 은 32비트 쓰기만.
 * legacy = false: F0/F3/F7/L4/G0/G4/H7 형. CRC_INIT, CRC_POL, POLYSIZE, REV_IN, REV_OUT, 8/16/32비트 DR 쓰기.
 * 공통: 쓴 데이터(REV_IN 적용 후)를 MSB 먼저 한 비트씩 LFSR 에 넣는다. 최종 XOR 레지스터는 없다(소프트웨어 몫). */
enum { REV_NONE = 0, REV_BYTE = 1, REV_HALF = 2, REV_WORD = 3 };   /* CRC_CR.REV_IN 인코딩 (대표값) */

typedef struct {
    bool     legacy;
    uint8_t  polysize;   /* 32 / 16 / 8 / 7 (CRC_CR.POLYSIZE) */
    uint8_t  rev_in;     /* REV_NONE..REV_WORD */
    bool     rev_out;    /* 읽을 때 polysize 비트 전체를 반사 */
    uint32_t init;       /* CRC_INIT: RESET 때 상태로 들어갈 값 */
    uint32_t pol;        /* CRC_POL */
    uint32_t state;      /* 내부 CRC 레지스터 (REV_OUT 적용 전) */
    unsigned writes[3];  /* 8 / 16 / 32비트 DR 쓰기 횟수 */
    unsigned bad_access; /* legacy 유닛에 8/16비트 쓰기 (mock 정책: 무시하고 센다) */
} crc_unit;

static void hw_init(crc_unit *u, bool legacy) {           /* 전원 투입 직후 기본값 */
    memset(u, 0, sizeof *u);
    u->legacy = legacy; u->polysize = 32; u->pol = 0x04C11DB7u; u->init = 0xFFFFFFFFu;
    u->state = 0xFFFFFFFFu;
}
static uint32_t hw_mask(const crc_unit *u) { return mask_n(u->polysize); }
/* CRC_CR.RESET = 1: 상태 ← INIT (legacy 는 항상 0xFFFFFFFF) */
static void hw_reset(crc_unit *u) { u->state = u->legacy ? 0xFFFFFFFFu : (u->init & hw_mask(u)); }

/* REV_IN: 쓰기 데이터(width 비트)를 unit(8/16/32) 단위로 각각 반사.
 * mock 정의: unit 이 쓰기 폭보다 크면 쓰기 폭 단위로 반사 (실제 칩의 좁은 쓰기 동작은 RM 확인). */
static uint32_t rev_in_apply(unsigned rev, uint32_t data, unsigned width) {
    unsigned unit = rev == REV_BYTE ? 8u : rev == REV_HALF ? 16u : rev == REV_WORD ? 32u : 0u;
    if (unit == 0) return data;
    if (unit > width) unit = width;
    uint32_t out = 0;
    for (unsigned s = 0; s < width; s += unit)
        out |= reflect((data >> s) & mask_n(unit), unit) << s;
    return out;
}

/* DR 에 width(8/16/32) 비트 쓰기 = CPU 의 STRB/STRH/STR 또는 DMA 한 번 */
static void hw_write(crc_unit *u, uint32_t data, unsigned width) {
    u->writes[width == 8 ? 0 : width == 16 ? 1 : 2]++;
    if (u->legacy && width != 32) { u->bad_access++; return; }
    data &= mask_n(width);
    if (!u->legacy) data = rev_in_apply(u->rev_in, data, width);
    uint32_t m = hw_mask(u), top = 1u << (u->polysize - 1u);
    for (int i = (int)width - 1; i >= 0; i--) {             /* MSB 먼저 */
        uint32_t fb = ((u->state & top) ? 1u : 0u) ^ ((data >> i) & 1u);
        u->state = (u->state << 1) & m;
        if (fb) u->state ^= u->pol & m;
    }
}

/* DR 읽기: REV_OUT 이면 polysize 비트 전체를 반사해서 보여준다 (내부 상태는 그대로) */
static uint32_t hw_read(const crc_unit *u) {
    if (!u->legacy && u->rev_out) return reflect(u->state, u->polysize);
    return u->state;
}

/* ---------------- Part B: 설정 탐색, 진단, 구형 유닛의 꼬리 ---------------- */

/* 바이트 버퍼에서 32비트 워드 읽기. 리틀 엔디언 Cortex-M 의 LDR 은 load_le32 와 같다.
 * load_be32 = LDR + REV (__REV). 바이트 단위로 조립하므로 정렬(alignment)과 무관. */
static uint32_t load_le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint32_t load_be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

typedef struct { bool be_load; uint8_t rev_in; bool rev_out; uint32_t init, xorout; } hw_cfg;

/* 신형 유닛을 cfg 로 설정하고 n(4의 배수) 바이트를 32비트 워드로 DR 에 써서 결과를 낸다.
 * 반환 = DR 읽기 ^ cfg->xorout (최종 XOR 는 소프트웨어 몫) */
static uint32_t hw_run(crc_unit *u, const hw_cfg *c, const uint8_t *p, size_t n) {
    hw_init(u, false);
    u->rev_in = c->rev_in; u->rev_out = c->rev_out; u->init = c->init;
    hw_reset(u);
    for (size_t i = 0; i + 4 <= n; i += 4)
        hw_write(u, c->be_load ? load_be32(p + i) : load_le32(p + i), 32);
    return hw_read(u) ^ c->xorout;
}

/* 브링업 진단: HW 결과와 SW 기대값만 보고 "무엇을 빠뜨렸나" 추리 */
enum { DIAG_OK, DIAG_NO_XOROUT, DIAG_NO_REVOUT, DIAG_NO_REVOUT_NO_XOROUT, DIAG_OTHER };
static int crc_diagnose(uint32_t hw, uint32_t sw) {
    if (hw == sw) return DIAG_OK;
    if ((hw ^ 0xFFFFFFFFu) == sw) return DIAG_NO_XOROUT;
    if (reflect(hw, 32) == sw) return DIAG_NO_REVOUT;
    if ((reflect(hw, 32) ^ 0xFFFFFFFFu) == sw) return DIAG_NO_REVOUT_NO_XOROUT;
    return DIAG_OTHER;      /* 바이트 순서 · REV_IN · INIT · 다항식 → 데이터로 탐색해야 한다 */
}

/* 구형(F4) 유닛으로 CRC-32/MPEG-2: 완전한 워드는 HW, 꼬리 0~3 바이트는 SW 로 이어서 */
static uint32_t f4_crc32_mpeg2(crc_unit *u, const uint8_t *p, size_t n) {
    hw_reset(u);
    size_t i = 0;
    for (; i + 4 <= n; i += 4) hw_write(u, load_be32(p + i), 32);
    return crc32_msb_update(hw_read(u), p + i, n - i);
}

/* 구형(F4) 유닛으로 CRC-32/ISO-HDLC (zlib): 워드를 RBIT 해서 넣고, 결과도 RBIT → SW 꼬리 → 반전 */
static uint32_t f4_crc32_iso(crc_unit *u, const uint8_t *p, size_t n) {
    hw_reset(u);
    size_t i = 0;
    for (; i + 4 <= n; i += 4) hw_write(u, reflect(load_le32(p + i), 32), 32);
    return ~crc32_lsb_update(reflect(hw_read(u), 32), p + i, n - i);
}

/* ---------------- Part C: 신형 유닛 드라이버와 이미지 검증 ---------------- */

/* CRC-32/ISO-HDLC 설정: REV_IN = BYTE + 빅 엔디언 묶음(폭과 무관하게 맞는다), REV_OUT = 1, INIT = 0xFFFFFFFF */
static void crc32_begin(crc_unit *u) {
    u->polysize = 32; u->pol = 0x04C11DB7u; u->init = 0xFFFFFFFFu;
    u->rev_in = REV_BYTE; u->rev_out = true;
    hw_reset(u);
}
/* 임의 길이 청크: 4바이트씩 32비트 쓰기, 남은 2바이트는 16비트 쓰기, 1바이트는 8비트 쓰기 */
static void crc32_feed(crc_unit *u, const uint8_t *p, size_t n) {
    size_t i = 0;
    for (; i + 4 <= n; i += 4) hw_write(u, load_be32(p + i), 32);
    if (n - i >= 2) { hw_write(u, (uint32_t)p[i] << 8 | p[i + 1], 16); i += 2; }
    if (n - i == 1) hw_write(u, p[i], 8);
}
static uint32_t crc32_end(const crc_unit *u) { return ~hw_read(u); }

/* 플래시 이미지 = [magic "CB09" LE][len LE][payload len 바이트][CRC-32/ISO-HDLC LE]
 * CRC 는 magic 부터 payload 끝까지 (빌드 스크립트의 zlib.crc32 와 같은 값). 플래시는 64바이트씩 읽는다. */
#define IMG_MAGIC 0x39304243u   /* "CB09" 를 LE 로 읽은 값 */
static bool image_ok(crc_unit *u, const uint8_t *flash, size_t flash_size) {
    if (flash_size < 12) return false;
    if (load_le32(flash) != IMG_MAGIC) return false;
    uint32_t len = load_le32(flash + 4);
    if (len > flash_size - 12) return false;             /* 8 + len + 4 <= flash_size, 넘침 없는 형태 */
    size_t covered = 8 + (size_t)len;
    crc32_begin(u);
    for (size_t off = 0; off < covered; off += 64)
        crc32_feed(u, flash + off, covered - off < 64 ? covered - off : 64);
    return crc32_end(u) == load_le32(flash + covered);
}

/* ======================= 테스트 ======================= */

static const char *REV_NAME[4] = { "NONE", "BYTE", "HALF", "WORD" };

int main(void) {
    uint32_t seed = 0xC0B09u;
    static uint8_t buf[2048];
    crc_unit u;

    /* ===== Part A: 반사 ===== */
    CHECK(reflect(0x01u, 8) == 0x80u && reflect(0x0Fu, 8) == 0xF0u && reflect(0x1Au, 8) == 0x58u);
    CHECK(reflect(1u, 32) == 0x80000000u && reflect(0x04C11DB7u, 32) == 0xEDB88320u);
    CHECK(reflect(0x8005u, 16) == 0xA001u);
    {   int bad = 0;
        for (uint32_t x = 0; x < 256; x++) {               /* 8비트 전수: 비트 i ↔ 비트 7-i */
            uint32_t r = reflect(x, 8);
            for (unsigned i = 0; i < 8; i++) if (((x >> i) & 1u) != ((r >> (7 - i)) & 1u)) bad++;
        }
        for (int t = 0; t < 10000; t++) { uint32_t x = xs32(&seed); if (reflect(reflect(x, 32), 32) != x) bad++; }
        CHECK(bad == 0);
    }
    /* RM 의 REV_IN 예시: 0x1A2B3C4D → byte 0x58D43CB2, half-word 0xD458B23C, word 0xB23CD458 */
    CHECK(rev_in_apply(REV_BYTE, 0x1A2B3C4Du, 32) == 0x58D43CB2u);
    CHECK(rev_in_apply(REV_HALF, 0x1A2B3C4Du, 32) == 0xD458B23Cu);
    CHECK(rev_in_apply(REV_WORD, 0x1A2B3C4Du, 32) == 0xB23CD458u);
    CHECK(rev_in_apply(REV_NONE, 0x1A2B3C4Du, 32) == 0x1A2B3C4Du);

    /* ===== Part A: 소프트웨어 CRC-32 check 값 ===== */
    const uint8_t *chk = (const uint8_t *)"123456789";
    CHECK(crc32_iso_hdlc(chk, 9) == 0xCBF43926u);
    CHECK(crc32_mpeg2(chk, 9) == 0x0376E6E7u);
    CHECK((crc32_mpeg2(chk, 9) ^ 0xFFFFFFFFu) == 0xFC891918u);         /* CRC-32/BZIP2 = MPEG-2 + xorout */
    CHECK(crc32_iso_hdlc((const uint8_t *)"a", 1) == 0xE8B7BE43u);
    CHECK(crc32_iso_hdlc((const uint8_t *)"The quick brown fox jumps over the lazy dog", 43) == 0x414FA339u);
    CHECK(crc32_iso_hdlc(chk, 0) == 0x00000000u && crc32_mpeg2(chk, 0) == 0xFFFFFFFFu);
    {   /* 두 구현을 서로 묶는 성질: 반사형 CRC(data) = reflect( 비반사형 CRC(바이트마다 비트 반사한 data) ) */
        int bad = 0;
        for (int t = 0; t < 1000; t++) {
            size_t n = xs32(&seed) % 200u;
            uint8_t a[200], r[200];
            for (size_t i = 0; i < n; i++) { a[i] = (uint8_t)xs32(&seed); r[i] = (uint8_t)reflect(a[i], 8); }
            if (crc32_lsb_update(0xFFFFFFFFu, a, n) != reflect(crc32_msb_update(0xFFFFFFFFu, r, n), 32)) bad++;
        }
        printf("lsb(data) vs reflect(msb(bit-reversed bytes)): 1000 msgs, mismatches=%d\n", bad);
        CHECK(bad == 0);
    }

    /* ===== Part A: mock 유닛 기본 동작 ===== */
    hw_init(&u, true);                                    /* F4 형 */
    hw_reset(&u); hw_write(&u, 0x12345678u, 32);
    CHECK(hw_read(&u) == 0xDF8A8A2Bu);                    /* 리셋 후 워드 0x12345678 → 0xDF8A8A2B */
    CHECK(hw_read(&u) == crc32_mpeg2((const uint8_t *)"\x12\x34\x56\x78", 4));
    hw_write(&u, 0xAB, 8);                                /* legacy 에 바이트 쓰기: 무시 + 기록 */
    CHECK(u.bad_access == 1 && hw_read(&u) == 0xDF8A8A2Bu);
    {   int bad_be = 0, bad_le = 0;
        for (int t = 0; t < 1000; t++) {
            size_t n = 4u * (xs32(&seed) % 64u);
            for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)xs32(&seed);
            hw_reset(&u);
            for (size_t i = 0; i < n; i += 4) hw_write(&u, load_be32(buf + i), 32);
            if (hw_read(&u) != crc32_mpeg2(buf, n)) bad_be++;
            hw_reset(&u);
            for (size_t i = 0; i < n; i += 4) hw_write(&u, load_le32(buf + i), 32);
            if (hw_read(&u) != crc32_mpeg2(buf, n)) bad_le++;
        }
        printf("legacy unit vs CRC-32/MPEG-2, 1000 msgs (n%%4==0): BE-load mismatches=%d, LE-load mismatches=%d\n",
               bad_be, bad_le);
        CHECK(bad_be == 0);
        CHECK(bad_le > 900);                              /* LE 로 읽으면 (빈 메시지 빼고) 거의 다 틀린다 */
    }
    hw_init(&u, false);                                   /* 신형, 기본 설정 = 구형과 같은 CRC */
    {   int bad = 0;
        for (size_t n = 0; n <= 64; n++) {                /* 바이트 쓰기만으로도 MPEG-2 */
            for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)(n * 31u + i * 7u);
            hw_reset(&u);
            for (size_t i = 0; i < n; i++) hw_write(&u, buf[i], 8);
            if (hw_read(&u) != crc32_mpeg2(buf, n)) bad++;
        }
        CHECK(bad == 0 && u.bad_access == 0);
    }

    /* ===== Part B: 64가지 설정 탐색 ===== */
    {
        static const uint32_t INITS[2] = { 0x00000000u, 0xFFFFFFFFu };
        int iso_hits = 0, mpeg_hits = 0, iso_le_word = 0, iso_be_byte = 0, mpeg_be_none = 0;
        printf("config search (load, REV_IN, REV_OUT, INIT, XOROUT) over 8 random 64-byte msgs:\n");
        for (int be = 0; be < 2; be++)
        for (int ri = 0; ri < 4; ri++)
        for (int ro = 0; ro < 2; ro++)
        for (int ii = 0; ii < 2; ii++)
        for (int xo = 0; xo < 2; xo++) {
            hw_cfg c = { be != 0, (uint8_t)ri, ro != 0, INITS[ii], xo ? 0xFFFFFFFFu : 0u };
            bool all_iso = true, all_mpeg = true;
            uint32_t s2 = 0xB09u;
            for (int t = 0; t < 8; t++) {
                for (int i = 0; i < 64; i++) buf[i] = (uint8_t)xs32(&s2);
                uint32_t h = hw_run(&u, &c, buf, 64);
                if (h != crc32_iso_hdlc(buf, 64)) all_iso = false;
                if (h != crc32_mpeg2(buf, 64)) all_mpeg = false;
            }
            if ((all_iso || all_mpeg) && iso_hits + mpeg_hits < 6)        /* 앞의 6개까지만 출력 */
                printf("  %s-load REV_IN=%s REV_OUT=%d INIT=%08X XOROUT=%08X  == %s\n", be ? "BE" : "LE",
                       REV_NAME[ri], ro, (unsigned)c.init, (unsigned)c.xorout, all_iso ? "CRC-32/ISO-HDLC" : "CRC-32/MPEG-2");
            iso_hits += all_iso; mpeg_hits += all_mpeg;
            if (all_iso && !be && ri == REV_WORD && ro && ii && xo) iso_le_word = 1;
            if (all_iso && be && ri == REV_BYTE && ro && ii && xo) iso_be_byte = 1;
            if (all_mpeg && be && ri == REV_NONE && !ro && ii && !xo) mpeg_be_none = 1;
        }
        printf("  -> %d configs == ISO-HDLC, %d configs == MPEG-2 (of 64)\n", iso_hits, mpeg_hits);
        CHECK(iso_hits == 2 && iso_le_word && iso_be_byte);
        CHECK(mpeg_hits == 1 && mpeg_be_none);
    }

    /* ===== Part B: 결과값으로 진단 ===== */
    {
        memcpy(buf, "12345678", 8);
        uint32_t sw = crc32_iso_hdlc(buf, 8);
        hw_cfg good   = { false, REV_WORD, true,  0xFFFFFFFFu, 0xFFFFFFFFu };
        hw_cfg no_x   = { false, REV_WORD, true,  0xFFFFFFFFu, 0u };
        hw_cfg no_r   = { false, REV_WORD, false, 0xFFFFFFFFu, 0xFFFFFFFFu };
        hw_cfg no_rx  = { false, REV_WORD, false, 0xFFFFFFFFu, 0u };
        hw_cfg bad_ri = { false, REV_BYTE, true,  0xFFFFFFFFu, 0xFFFFFFFFu };
        hw_cfg bad_in = { false, REV_WORD, true,  0x00000000u, 0xFFFFFFFFu };
        CHECK(crc_diagnose(hw_run(&u, &good, buf, 8), sw) == DIAG_OK);
        CHECK(crc_diagnose(hw_run(&u, &no_x, buf, 8), sw) == DIAG_NO_XOROUT);
        CHECK(crc_diagnose(hw_run(&u, &no_r, buf, 8), sw) == DIAG_NO_REVOUT);
        CHECK(crc_diagnose(hw_run(&u, &no_rx, buf, 8), sw) == DIAG_NO_REVOUT_NO_XOROUT);
        CHECK(crc_diagnose(hw_run(&u, &bad_ri, buf, 8), sw) == DIAG_OTHER);
        CHECK(crc_diagnose(hw_run(&u, &bad_in, buf, 8), sw) == DIAG_OTHER);
    }

    /* ===== Part B: 구형 유닛, 4의 배수가 아닌 길이 ===== */
    hw_init(&u, true);
    {
        int bad_m = 0, bad_i = 0;
        for (size_t n = 0; n <= 64; n++) {                /* 길이 0..64 전부 (꼬리 0~3 모두) */
            for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)(0xA5u ^ (n * 13u + i));
            if (f4_crc32_mpeg2(&u, buf, n) != crc32_mpeg2(buf, n)) bad_m++;
            if (f4_crc32_iso(&u, buf, n) != crc32_iso_hdlc(buf, n)) bad_i++;
        }
        for (int t = 0; t < 2000; t++) {                  /* 랜덤 길이 0..300, 시작 주소 오프셋 0..3 */
            size_t n = xs32(&seed) % 301u, off = xs32(&seed) % 4u;
            for (size_t i = 0; i < n; i++) buf[off + i] = (uint8_t)xs32(&seed);
            if (f4_crc32_mpeg2(&u, buf + off, n) != crc32_mpeg2(buf + off, n)) bad_m++;
            if (f4_crc32_iso(&u, buf + off, n) != crc32_iso_hdlc(buf + off, n)) bad_i++;
        }
        printf("legacy + SW tail: 2065 msgs, mismatches mpeg2=%d iso=%d, narrow writes=%u\n", bad_m, bad_i, u.bad_access);
        CHECK(bad_m == 0);
        CHECK(bad_i == 0);
        CHECK(u.bad_access == 0);
        CHECK(f4_crc32_iso(&u, chk, 9) == 0xCBF43926u && f4_crc32_mpeg2(&u, chk, 9) == 0x0376E6E7u);
    }
    {   /* 함정: 꼬리를 0 으로 채워 워드로 넣으면 "다른 CRC" 가 된다 */
        int same = 0, tested = 0;
        for (int t = 0; t < 1000; t++) {
            size_t n = 1 + xs32(&seed) % 200u;
            if (n % 4 == 0) n++;
            uint8_t pad[208] = { 0 };
            for (size_t i = 0; i < n; i++) pad[i] = (uint8_t)xs32(&seed);
            hw_reset(&u);
            for (size_t i = 0; i < n; i += 4) hw_write(&u, load_be32(pad + i), 32);
            same += hw_read(&u) == crc32_mpeg2(pad, n);
            tested++;
        }
        printf("zero-padded tail: %d/%d equal to CRC-32/MPEG-2\n", same, tested);
        CHECK(same == 0);
    }

    /* ===== Part C: 신형 유닛 드라이버 ===== */
    hw_init(&u, false);
    crc32_begin(&u); crc32_feed(&u, chk, 9);
    CHECK(crc32_end(&u) == 0xCBF43926u);
    CHECK(u.writes[2] == 2 && u.writes[1] == 0 && u.writes[0] == 1);   /* 워드 2 + 바이트 1 */
    for (size_t i = 0; i < 1027; i++) buf[i] = (uint8_t)xs32(&seed);
    hw_init(&u, false); crc32_begin(&u); crc32_feed(&u, buf, 1027);
    CHECK(crc32_end(&u) == crc32_iso_hdlc(buf, 1027));
    CHECK(u.writes[2] == 256 && u.writes[1] == 1 && u.writes[0] == 1); /* 1027 = 4·256 + 2 + 1 */
    {   /* 임의 청크(0..37 바이트)로 쪼개 넣어도 한 번에 계산한 SW 와 같다 + 비정렬 시작 */
        int bad = 0;
        for (int t = 0; t < 2000; t++) {
            size_t n = xs32(&seed) % 601u, off = xs32(&seed) % 4u;
            for (size_t i = 0; i < n; i++) buf[off + i] = (uint8_t)xs32(&seed);
            crc32_begin(&u);
            for (size_t i = 0; i < n; ) {
                size_t k = xs32(&seed) % 38u;
                if (k > n - i) k = n - i;
                crc32_feed(&u, buf + off + i, k);
                i += k;
            }
            if (crc32_end(&u) != crc32_iso_hdlc(buf + off, n)) bad++;
        }
        printf("modern driver, 2000 msgs in random chunks: mismatches=%d\n", bad);
        CHECK(bad == 0);
    }
    {   /* 장애 주입: 신형 드라이버를 구형 유닛에서 돌리면 (REV 설정 무시 + 좁은 쓰기 무시) 틀린다 */
        crc_unit old; hw_init(&old, true);
        crc32_begin(&old); crc32_feed(&old, chk, 9);
        CHECK(crc32_end(&old) != 0xCBF43926u && old.bad_access == 1);
    }

    /* ===== Part C: 플래시 이미지 검증 ===== */
    {
        static uint8_t flash[1024];
        uint32_t len = 777;
        memset(flash, 0xFF, sizeof flash);                /* 지운 플래시 = 0xFF */
        flash[0] = 'C'; flash[1] = 'B'; flash[2] = '0'; flash[3] = '9';
        flash[4] = (uint8_t)len; flash[5] = (uint8_t)(len >> 8); flash[6] = 0; flash[7] = 0;
        for (uint32_t i = 0; i < len; i++) flash[8 + i] = (uint8_t)xs32(&seed);
        uint32_t c = crc32_iso_hdlc(flash, 8 + len);      /* 빌드 PC 쪽: zlib.crc32 */
        for (int k = 0; k < 4; k++) flash[8 + len + (uint32_t)k] = (uint8_t)(c >> (8 * k));
        CHECK(load_le32(flash) == IMG_MAGIC);
        CHECK(image_ok(&u, flash, sizeof flash));
        flash[100] ^= 0x10;                               /* payload 1비트 */
        CHECK(!image_ok(&u, flash, sizeof flash));
        flash[100] ^= 0x10;
        flash[8 + len] ^= 0x01;                           /* 저장된 CRC 1비트 */
        CHECK(!image_ok(&u, flash, sizeof flash));
        flash[8 + len] ^= 0x01;
        for (int k = 0; k < 4; k++) flash[8 + len + (uint32_t)k] = (uint8_t)(c >> (24 - 8 * k));   /* BE 로 저장 */
        CHECK(!image_ok(&u, flash, sizeof flash));
        for (int k = 0; k < 4; k++) flash[8 + len + (uint32_t)k] = (uint8_t)(c >> (8 * k));
        CHECK(image_ok(&u, flash, sizeof flash));
        flash[4] = 0xF8; flash[5] = 0xFF; flash[6] = 0xFF; flash[7] = 0xFF;                 /* len = 0xFFFFFFF8 */
        CHECK(!image_ok(&u, flash, sizeof flash));
        flash[4] = (uint8_t)len; flash[5] = (uint8_t)(len >> 8); flash[6] = 0; flash[7] = 0;
        flash[0] = 'X';
        CHECK(!image_ok(&u, flash, sizeof flash));
        CHECK(!image_ok(&u, flash, 11));
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
