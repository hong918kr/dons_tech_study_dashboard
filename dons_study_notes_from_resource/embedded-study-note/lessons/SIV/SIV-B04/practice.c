/* SIV-B04 추가 연습 — 한 번에 검증
 *   연습 1: W0C(rc_w0) / W1T(toggle) 기대값 확장 + W1T 를 W1S 로 만든 결함은 어떤 패턴 순서에서 잡히나
 *   연습 2: 패턴 집합의 품질 — 비트별 0/1 양극성, 인접 비트 차이
 *   연습 3: 테스트 후 복원값 (W1C/WO 를 되쓰지 않기)
 *   연습 4: R/W 테스트 전 블록 생존 확인 (clock-gated → 0xDEADBEEF / 0xFFFFFFFF, ID 불일치)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef enum { AT_RW, AT_RO, AT_WO, AT_W1C, AT_W1S, AT_RC, AT_W0C, AT_W1T } access_t;
typedef struct { uint8_t lsb, width; access_t acc; } field_t;
typedef struct { uint32_t off; int nf; field_t f[4]; } reg_desc_t;

static uint32_t field_mask(const field_t *f) {
    uint32_t ones = (f->width >= 32) ? 0xFFFFFFFFu : ((1u << f->width) - 1u);
    return ones << f->lsb;
}

/* ---- 연습 1: 한 필드(mask m)의 write 후 기대 read 값 ---- */
static uint32_t expect_field(access_t a, uint32_t before, uint32_t wr, uint32_t m) {
    switch (a) {
    case AT_RW:  return wr & m;
    case AT_RO:  return before & m;
    case AT_W1C: return before & ~wr & m;
    case AT_W1S: return (before | wr) & m;
    case AT_W0C: return before & wr & m;         /* 0 쓴 자리만 0, 1 쓰기는 무효 */
    case AT_W1T: return (before ^ wr) & m;       /* 1 쓴 자리 반전 */
    case AT_WO: case AT_RC: default: return 0;
    }
}
/* 비트 하나짜리 W1T 를 W1S 로 잘못 만든 장치: 패턴 순서 pats 를 리셋(0)부터 돌려 잡히는 첫 인덱스, 없으면 -1 */
static int first_detect_w1t_as_w1s(const uint32_t *pats, int n) {
    uint32_t dut = 0;                              /* 결함 장치 (W1S 동작) */
    for (int i = 0; i < n; i++) {
        uint32_t before = dut;
        dut = (dut | pats[i]) & 1u;
        if (dut != expect_field(AT_W1T, before, pats[i], 1u)) return i;
    }
    return -1;
}

/* ---- 연습 2: 패턴 집합의 품질 ---- */
static uint32_t polarity_cover(const uint32_t *p, int n) {   /* 0 과 1 을 둘 다 받아 본 비트 */
    uint32_t saw1 = 0, saw0 = 0;
    for (int i = 0; i < n; i++) { saw1 |= p[i]; saw0 |= ~p[i]; }
    return saw1 & saw0;
}
static uint32_t adjacent_differ(const uint32_t *p, int n) {  /* bit i 와 i+1 이 다른 값을 가져 본 i */
    uint32_t d = 0;
    for (int i = 0; i < n; i++) d |= (p[i] ^ (p[i] >> 1)) & 0x7FFFFFFFu;
    return d;
}

/* ---- 연습 3: 테스트 후 복원값 — RW 필드만 원래 값, 나머지는 무해한 0 ---- */
static uint32_t restore_value(const reg_desc_t *r, uint32_t orig) {
    uint32_t rw = 0;
    for (int i = 0; i < r->nf; i++) if (r->f[i].acc == AT_RW) rw |= field_mask(&r->f[i]);
    return orig & rw;
}

/* ---- 연습 4: 블록 생존 확인 ---- */
typedef enum { PRE_OK, PRE_BUS_ERR, PRE_ID_MISMATCH } pre_t;
static pre_t precheck(uint32_t (*rd)(uint32_t off), uint32_t id_off, uint32_t id_exp) {
    uint32_t a = rd(id_off), b = rd(id_off);
    if (a == 0xDEADBEEFu || a == 0xFFFFFFFFu || a != b) return PRE_BUS_ERR;
    return a == id_exp ? PRE_OK : PRE_ID_MISMATCH;
}
static uint32_t rd_alive(uint32_t off)  { return off == 0 ? 0x0D3A0102u : 0; }
static uint32_t rd_gated(uint32_t off)  { (void)off; return 0xDEADBEEFu; }
static uint32_t rd_nopwr(uint32_t off)  { (void)off; return 0xFFFFFFFFu; }
static uint32_t rd_oldrev(uint32_t off) { return off == 0 ? 0x0D3A0101u : 0; }

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* 연습 1 */
    CHECK(expect_field(AT_W0C, 0xFu, 0xAu, 0xFu) == 0xAu);        /* 0 쓴 bit0, bit2 만 지움 */
    CHECK(expect_field(AT_W0C, 0x5u, 0xFu, 0xFu) == 0x5u);        /* 전부 1 쓰기 = 무효 */
    CHECK(expect_field(AT_W1T, 0x5u, 0x3u, 0xFu) == 0x6u);
    CHECK(expect_field(AT_W1T, 0x5u, 0x0u, 0xFu) == 0x5u);
    { static const uint32_t seq1[] = { 0x0u, 0xFFFFFFFFu, 0x55555555u, 0xAAAAAAAAu };
      static const uint32_t seq2[] = { 0xFFFFFFFFu, 0xFFFFFFFFu };
      static const uint32_t seq3[] = { 0x0u, 0xAAAAAAAAu };
      CHECK(first_detect_w1t_as_w1s(seq1, 4) == 2);              /* F 로 1, 다음 5 에서 0 이어야 함 */
      CHECK(first_detect_w1t_as_w1s(seq2, 2) == 1);
      CHECK(first_detect_w1t_as_w1s(seq3, 2) == -1); }           /* bit0 에 1 을 두 번 못 씀 */

    /* 연습 2 */
    { static const uint32_t z_f[] = { 0x0u, 0xFFFFFFFFu }, cb[] = { 0x55555555u, 0xAAAAAAAAu },
                            one[] = { 0x55555555u },
                            all4[] = { 0x0u, 0xFFFFFFFFu, 0x55555555u, 0xAAAAAAAAu };
      CHECK(polarity_cover(z_f, 2) == 0xFFFFFFFFu && adjacent_differ(z_f, 2) == 0u);
      CHECK(polarity_cover(cb, 2) == 0xFFFFFFFFu && adjacent_differ(cb, 2) == 0x7FFFFFFFu);
      CHECK(polarity_cover(one, 1) == 0u);
      CHECK(polarity_cover(all4, 4) == 0xFFFFFFFFu && adjacent_differ(all4, 4) == 0x7FFFFFFFu); }

    /* 연습 3 */
    { reg_desc_t ctrl = { 0x004, 2, { { 0, 1, AT_RW }, { 8, 4, AT_RW } } };
      reg_desc_t ints = { 0x00C, 1, { { 0, 3, AT_W1C } } };
      reg_desc_t cmd  = { 0x01C, 2, { { 0, 1, AT_WO }, { 8, 8, AT_WO } } };
      reg_desc_t mix  = { 0x030, 3, { { 0, 4, AT_RW }, { 4, 4, AT_W1C }, { 8, 4, AT_W1S } } };
      CHECK(restore_value(&ctrl, 0xFFFFF5A1u) == 0x00000501u);   /* reserved 는 버림 */
      CHECK(restore_value(&ints, 0x00000007u) == 0u);            /* pending 을 지우지 않는다 */
      CHECK(restore_value(&cmd, 0x0000AB01u) == 0u);             /* 명령은 절대 재생하지 않는다 */
      CHECK(restore_value(&mix, 0x00000FF9u) == 0x00000009u); }

    /* 연습 4 */
    CHECK(precheck(rd_alive, 0, 0x0D3A0102u) == PRE_OK);
    CHECK(precheck(rd_gated, 0, 0x0D3A0102u) == PRE_BUS_ERR);
    CHECK(precheck(rd_nopwr, 0, 0x0D3A0102u) == PRE_BUS_ERR);
    CHECK(precheck(rd_oldrev, 0, 0x0D3A0102u) == PRE_ID_MISMATCH);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
