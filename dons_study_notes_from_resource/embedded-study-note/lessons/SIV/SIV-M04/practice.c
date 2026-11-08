/* SIV-M04 추가 연습 — 한 번에 검증
 *   P1  확장 capability(0x100~0xFFF) 체인 걷기 + 루프/범위 방어, ID 로 찾기 (AER 0x0001, Secondary PCIe 0x0019,
 *       Physical Layer 16.0 GT/s 0x0026)
 *   P2  기대 링크 = min(EP cap, RP cap, 슬롯 배선 폭)
 *   P3  retrain N 회 무실패 → 실패 확률 상한 (rule of three) 과 필요한 N
 *   P4  "RL 을 쓴 직후 LT 가 1 이어야 한다" 스펙 준수 검사 — LT 가 늦게 서는 결함 mock
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---------------- P1: 확장 capability ---------------- */
typedef struct { uint8_t b[4096]; size_t len; } cfg_t;
static uint32_t rd32(const cfg_t *c, size_t off) {
    return (uint32_t)c->b[off] | (uint32_t)c->b[off + 1] << 8 | (uint32_t)c->b[off + 2] << 16 | (uint32_t)c->b[off + 3] << 24;
}
static void wr32(cfg_t *c, size_t off, uint32_t v) { for (int i = 0; i < 4; i++) c->b[off + (size_t)i] = (uint8_t)(v >> (8 * i)); }
static uint32_t ext_hdr(uint16_t id, uint8_t ver, uint16_t next) { return (uint32_t)id | (uint32_t)ver << 16 | (uint32_t)next << 20; }

typedef enum { EX_FOUND, EX_NOT_FOUND, EX_NO_EXT, EX_BAD_PTR, EX_LOOP } ex_status_t;
/* 헤더: [15:0] ID, [19:16] version, [31:20] next. 0x100 부터. 헤더 0 또는 all-ones = 확장 cap 없음/접근 불가. */
static ex_status_t ext_find(const cfg_t *c, uint16_t id, uint16_t *off_out, int *visited_out) {
    static uint8_t seen[1024];                       /* dword 단위 방문 표시 (4096/4) */
    memset(seen, 0, sizeof seen);
    *off_out = 0; *visited_out = 0;
    if (c->len < 4096) return EX_NO_EXT;             /* lspci -xxx(256B) 덤프에는 확장 영역이 없다 */
    uint16_t pos = 0x100;
    uint32_t h = rd32(c, pos);
    if (h == 0 || h == 0xFFFFFFFFu) return EX_NO_EXT;
    for (;;) {
        if (seen[pos >> 2]) { *off_out = pos; return EX_LOOP; }
        seen[pos >> 2] = 1; (*visited_out)++;
        h = rd32(c, pos);
        if ((h & 0xFFFFu) == id) { *off_out = pos; return EX_FOUND; }
        uint16_t next = (uint16_t)((h >> 20) & 0xFFCu);        /* 하위 2비트 reserved */
        if (next == 0) return EX_NOT_FOUND;
        if (next < 0x100) { *off_out = next; return EX_BAD_PTR; }
        pos = next;
    }
}

/* ---------------- P2: 기대 링크 ---------------- */
typedef struct { uint8_t gen, width; } link_t;
static link_t expected_link(link_t ep, link_t rp, uint8_t slot_width) {
    link_t e;
    e.gen = ep.gen < rp.gen ? ep.gen : rp.gen;
    e.width = ep.width < rp.width ? ep.width : rp.width;
    if (slot_width < e.width) e.width = slot_width;
    return e;
}

/* ---------------- P3: 무실패 N 회의 신뢰 상한 ---------------- */
static double pow_n(double q, int n) { double r = 1.0; for (int i = 0; i < n; i++) r *= q; return r; }
/* 0 실패 / N 회 → (1-p)^N = 1-CL 을 만족하는 p (이분 탐색, libm 없이) */
static double p_upper(int n, double cl) {
    double lo = 0.0, hi = 1.0;
    for (int it = 0; it < 100; it++) { double mid = (lo + hi) / 2; if (pow_n(1.0 - mid, n) > 1.0 - cl) lo = mid; else hi = mid; }
    return hi;
}
/* p 미만을 CL 로 주장하려면 무실패 몇 회? (1-p)^N ≤ 1-CL 인 최소 N */
static int n_needed(double p, double cl) { int n = 0; double q = 1.0; while (q > 1.0 - cl) { q *= 1.0 - p; n++; } return n; }

/* ---------------- P4: LT 즉시 세트 검사 ---------------- */
#define LNKSTA_LT 0x0800u
typedef struct { uint32_t now, t_rl, lt_delay, train_us; bool rl; } lt_mock_t;
static uint16_t lt_read(lt_mock_t *m) {
    m->now += 1;
    uint16_t v = 0x0044;                                         /* Gen4 x4 */
    if (m->rl) {
        uint32_t dt = m->now - m->t_rl;
        if (dt >= m->lt_delay && dt < m->lt_delay + m->train_us) v |= LNKSTA_LT;   /* 결함: lt_delay 동안 LT=0 */
    }
    return v;
}
static void lt_write_rl(lt_mock_t *m) { m->now += 1; m->rl = true; m->t_rl = m->now; }
/* 스펙: LT 는 "Configuration/Recovery 중 이거나, RL 에 1 을 썼지만 training 이 아직 시작 안 됨" 을 뜻한다 →
 * RL 쓰기 직후 첫 읽기에서 LT 가 1 이어야 한다. */
static bool check_lt_immediate(lt_mock_t *m) { lt_write_rl(m); return (lt_read(m) & LNKSTA_LT) != 0; }
/* 순진한 완료 대기: LT==0 이 될 때까지 — 반환값 = 읽은 횟수 */
static int naive_wait(lt_mock_t *m) { int polls = 0; lt_write_rl(m); do { polls++; } while (lt_read(m) & LNKSTA_LT); return polls; }

int main(void) {
    /* P1 */
    static cfg_t c;
    memset(&c, 0, sizeof c); c.len = 4096;
    wr32(&c, 0x100, ext_hdr(0x0001, 2, 0x148));   /* AER */
    wr32(&c, 0x148, ext_hdr(0x0019, 1, 0x170));   /* Secondary PCIe */
    wr32(&c, 0x170, ext_hdr(0x0026, 1, 0x000));   /* Physical Layer 16.0 GT/s */
    uint16_t off; int nv;
    CHECK(ext_find(&c, 0x0026, &off, &nv) == EX_FOUND && off == 0x170 && nv == 3);
    CHECK(ext_find(&c, 0x0001, &off, &nv) == EX_FOUND && off == 0x100 && nv == 1);
    CHECK(ext_find(&c, 0x0027, &off, &nv) == EX_NOT_FOUND && nv == 3);       /* Lane Margining 없음 */
    wr32(&c, 0x170, ext_hdr(0x0026, 1, 0x148));                             /* 0x170 → 0x148 루프 */
    CHECK(ext_find(&c, 0x0027, &off, &nv) == EX_LOOP && off == 0x148 && nv == 3);
    wr32(&c, 0x170, ext_hdr(0x0026, 1, 0x0C0));                             /* 표준 영역을 가리킴 */
    CHECK(ext_find(&c, 0x0027, &off, &nv) == EX_BAD_PTR && off == 0x0C0);
    wr32(&c, 0x100, 0xFFFFFFFFu);
    CHECK(ext_find(&c, 0x0001, &off, &nv) == EX_NO_EXT);
    c.len = 256;
    CHECK(ext_find(&c, 0x0001, &off, &nv) == EX_NO_EXT);

    /* P2 */
    static const struct { link_t ep, rp; uint8_t slot; link_t want; } ET[] = {
        { { 4, 4 }, { 5, 16 }, 8, { 4, 4 } },
        { { 5, 16 }, { 4, 8 }, 16, { 4, 8 } },
        { { 4, 16 }, { 4, 16 }, 8, { 4, 8 } },     /* x16 커넥터에 x8 만 배선된 슬롯 */
        { { 3, 1 }, { 5, 4 }, 4, { 3, 1 } },
    };
    for (size_t i = 0; i < sizeof ET / sizeof ET[0]; i++) {
        link_t e = expected_link(ET[i].ep, ET[i].rp, ET[i].slot);
        CHECK(e.gen == ET[i].want.gen && e.width == ET[i].want.width);
    }

    /* P3 */
    double p200 = p_upper(200, 0.95), p1000 = p_upper(1000, 0.95);
    int need = n_needed(0.001, 0.95), need99 = n_needed(0.001, 0.99);
    CHECK(p200 > 0.0148 && p200 < 0.0149);         /* 정확값 ≈ 1.487%, 근사 3/200 = 1.5% */
    CHECK(p1000 > 0.00299 && p1000 < 0.00300);
    CHECK(need == 2995 && need99 == 4603);
    printf("P3 p_upper(200)=%.4f%% p_upper(1000)=%.4f%% n_needed(0.1%%,95%%)=%d (99%%: %d)\n",
           p200 * 100, p1000 * 100, need, need99);

    /* P4 */
    lt_mock_t good = { .lt_delay = 0, .train_us = 40 }, bad = { .lt_delay = 5, .train_us = 40 };
    CHECK(check_lt_immediate(&good) == true);
    CHECK(check_lt_immediate(&bad) == false);
    lt_mock_t g2 = { .lt_delay = 0, .train_us = 40 }, b2 = { .lt_delay = 5, .train_us = 40 };
    int pg = naive_wait(&g2), pb = naive_wait(&b2);
    CHECK(pg > 30 && pb == 1);                      /* 결함 칩에서 순진한 대기는 첫 읽기에 "완료" */
    printf("P4 naive_wait polls: good=%d lt-late=%d\n", pg, pb);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
