/* COMM-B04 추가 연습 — 한 번에 검증
 *
 * 연습 1: GPIO 비트뱅 루프(Linux spi-bitbang 모양) — 엣지 "뒤에" MISO 를 읽는 마스터. 4모드 전수 + 늦게 읽어서 생기는 차이
 * 연습 2: MOSI–MISO 루프백 자가 시험 — 4모드 모두 통과한다 = 모드 설정은 검증 못 한다
 * 연습 3: mode 0/3 자동 인식 슬레이브 (CS 하강 때 SCLK 레벨로 CPOL 판정) — mode 1/2 마스터와는?
 * 연습 4: 데이지 체인 (8비트 시프트 레지스터 두 개) — 첫 바이트가 먼 쪽 장치로 간다
 * 연습 5: SCLK 최대 주파수 예산과 프리스케일러 선택, race 조합의 hold 여유
 *
 * 모델은 solution.c 축약판 (슬레이브 2개 체인·루프백·자동 모드 옵션 추가). self-contained.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LINE_RELEASED 1u
#define SPI_FILL      0xFFu

typedef struct { uint8_t cpol, cpha, lsb_first; } spi_cfg_t;
static spi_cfg_t spi_mode(unsigned mode) { spi_cfg_t c = { (uint8_t)(mode >> 1 & 1u), (uint8_t)(mode & 1u), 0 }; return c; }

/* ---- 시프트 엔진 (solution.c 와 같음, MSB first 만) ---- */
typedef struct {
    spi_cfg_t cfg;
    uint8_t tx_sr, rx_sr, out_cnt, in_cnt, out;
    const uint8_t *tx; size_t tx_n, tx_i;
    uint8_t *rx; size_t rx_cap, rx_n;
    uint8_t echo, last_rx;
    unsigned partial;
} spi_eng_t;

static int spi_is_sample_edge(const spi_cfg_t *c, uint8_t new_sclk) { return (new_sclk != c->cpol) ^ c->cpha; }
static uint8_t eng_next_byte(spi_eng_t *e) {
    if (e->echo) return e->last_rx;
    return e->tx_i < e->tx_n ? e->tx[e->tx_i++] : (uint8_t)SPI_FILL;
}
static void eng_select(spi_eng_t *e) {
    e->in_cnt = 0; e->rx_sr = 0; e->tx_sr = eng_next_byte(e); e->out_cnt = 0;
    if (e->cfg.cpha == 0) { e->out = e->tx_sr >> 7 & 1u; e->out_cnt = 1; } else e->out = LINE_RELEASED;
}
static void eng_sample(spi_eng_t *e, uint8_t level) {
    e->rx_sr = (uint8_t)(e->rx_sr << 1 | level);
    if (++e->in_cnt == 8) {
        e->last_rx = e->rx_sr;
        if (e->rx_n < e->rx_cap) e->rx[e->rx_n++] = e->rx_sr;
        e->in_cnt = 0; e->rx_sr = 0;
    }
}
static void eng_drive(spi_eng_t *e) {
    if (e->out_cnt == 8) { e->tx_sr = eng_next_byte(e); e->out_cnt = 0; }
    e->out = (uint8_t)(e->tx_sr >> (7u - e->out_cnt) & 1u);
    e->out_cnt++;
}
static void eng_deselect(spi_eng_t *e) { if (e->in_cnt) { e->partial++; e->in_cnt = 0; e->rx_sr = 0; } }

/* ---- 버스: 마스터 → s1 (→ s2 체인이면) → 마스터. 옵션: 루프백, s1 자동 mode 0/3 ---- */
typedef struct {
    uint8_t cs_n, sclk, mosi, mid, miso;   /* mid = s1 의 출력(체인에서 s2 의 입력) */
    spi_eng_t m, s1, s2;
    uint8_t m_active, chain, loopback, auto03;
} sim_t;

static void sim_step(sim_t *s, uint8_t cs_n, uint8_t sclk) {
    int was = s->cs_n == 0, now = cs_n == 0;
    if (was && now && sclk != s->sclk) {
        int ms = s->m_active && spi_is_sample_edge(&s->m.cfg, sclk);
        int a = spi_is_sample_edge(&s->s1.cfg, sclk), b = s->chain && spi_is_sample_edge(&s->s2.cfg, sclk);
        if (ms) eng_sample(&s->m, s->miso);           /* 1단계: 샘플 (엣지 직전 레벨) */
        if (a) eng_sample(&s->s1, s->mosi);
        if (b) eng_sample(&s->s2, s->mid);
        if (s->m_active && !ms) eng_drive(&s->m);     /* 2단계: 드라이브 */
        if (!a) eng_drive(&s->s1);
        if (s->chain && !b) eng_drive(&s->s2);
    }
    if (!was && now) {
        if (s->auto03) { s->s1.cfg.cpol = sclk; s->s1.cfg.cpha = sclk; }   /* idle 높음 → mode 3, 낮음 → mode 0 */
        eng_select(&s->s1);
        if (s->chain) eng_select(&s->s2);
    }
    if (was && !now) { eng_deselect(&s->s1); if (s->chain) eng_deselect(&s->s2); }
    s->cs_n = cs_n; s->sclk = sclk;
    s->mosi = s->m.out;
    s->mid = now ? s->s1.out : (uint8_t)LINE_RELEASED;
    if (s->loopback) s->miso = s->mosi;
    else s->miso = !now ? (uint8_t)LINE_RELEASED : s->chain ? s->s2.out : s->s1.out;
}

static void sim_init(sim_t *s, spi_cfg_t mc) {
    memset(s, 0, sizeof *s);
    s->cs_n = 1; s->sclk = mc.cpol; s->m.cfg = mc;
    s->m.out = s->s1.out = s->s2.out = LINE_RELEASED;
    s->m.last_rx = s->s1.last_rx = s->s2.last_rx = SPI_FILL;
    s->mosi = s->mid = s->miso = LINE_RELEASED;
}
static void eng_setup(spi_eng_t *e, spi_cfg_t c, const uint8_t *tx, size_t tx_n, uint8_t *rx, size_t cap) {
    e->cfg = c; e->tx = tx; e->tx_n = tx_n; e->tx_i = 0; e->rx = rx; e->rx_cap = rx ? cap : 0; e->rx_n = 0; e->partial = 0;
}

/* 하드웨어식 마스터 (solution.c 의 spi_xfer): 엣지에서 샘플 */
static void spi_xfer(sim_t *s, spi_cfg_t c, const uint8_t *tx, uint8_t *rx, size_t n) {
    eng_setup(&s->m, c, tx, n, rx, n);
    sim_step(s, 1, c.cpol); sim_step(s, 0, c.cpol);
    eng_select(&s->m); s->m_active = 1; sim_step(s, 0, c.cpol);
    for (size_t i = 0; i < 16 * n; i++) sim_step(s, 0, (uint8_t)(s->sclk ^ 1u));
    s->m_active = 0; sim_step(s, 1, c.cpol);
}

/* 비트열 지연 (solution.c 와 같음) */
static void delayed_bytes(const uint8_t *src, size_t n, int delay, uint8_t *out) {
    for (size_t i = 0; i < n; i++) {
        uint8_t v = 0;
        for (unsigned k = 0; k < 8; k++) {
            long j = (long)(i * 8 + k) - delay, bit;
            if (j < 0) bit = LINE_RELEASED;
            else if ((size_t)j >= 8 * n) bit = SPI_FILL >> 7 & 1u;
            else bit = src[j / 8] >> (7 - j % 8) & 1;
            v = (uint8_t)(v << 1 | bit);
        }
        out[i] = v;
    }
}

/* ===== 연습 1: GPIO 비트뱅 마스터 (마스터 엔진을 쓰지 않는다) ===== */
static void gpio_sclk(sim_t *s, uint8_t v) { sim_step(s, s->cs_n, v); }
static void gpio_mosi(sim_t *s, uint8_t v) { s->m.out = v; sim_step(s, s->cs_n, s->sclk); }   /* 엣지 없는 step */
static uint8_t gpio_miso(const sim_t *s) { return s->miso; }

static uint8_t bb_txrx(sim_t *s, spi_cfg_t c, uint8_t out) {
    uint8_t in = 0;
    for (int i = 7; i >= 0; i--) {
        uint8_t bit = out >> i & 1u;
        if (c.cpha == 0) {                      /* 데이터 먼저, leading 엣지 뒤에 읽기 */
            gpio_mosi(s, bit);
            gpio_sclk(s, (uint8_t)!c.cpol);
            in = (uint8_t)(in << 1 | gpio_miso(s));
            gpio_sclk(s, c.cpol);
        } else {                                /* leading 엣지 뒤 데이터, trailing 엣지 뒤에 읽기 */
            gpio_sclk(s, (uint8_t)!c.cpol);
            gpio_mosi(s, bit);
            gpio_sclk(s, c.cpol);
            in = (uint8_t)(in << 1 | gpio_miso(s));
        }
    }
    return in;
}
static void bb_xfer(sim_t *s, spi_cfg_t c, const uint8_t *tx, uint8_t *rx, size_t n) {
    sim_step(s, 1, c.cpol);                     /* idle 먼저, 그다음 CS */
    sim_step(s, 0, c.cpol);
    for (size_t i = 0; i < n; i++) rx[i] = bb_txrx(s, c, tx[i]);
    sim_step(s, 1, c.cpol);
}

/* ===== 연습 5: 타이밍 예산 ===== */
/* 반주기 ≥ 2·tpd(왕복 배선) + tv(슬레이브 clock→MISO 유효) + tsu(마스터 setup).  ns 정수 → Hz (내림) */
static uint32_t spi_fmax_hz(uint32_t tv_ns, uint32_t tsu_ns, uint32_t tpd_ns) {
    uint32_t half = 2 * tpd_ns + tv_ns + tsu_ns;
    return (uint32_t)(1000000000ull / (2ull * half));
}
/* 같은 엣지에서 샘플+변경(race 조합): 새 값이 마스터에 도착하는 최소 시간 − 마스터 hold 요구 */
static int32_t hold_margin_ns(uint32_t tv_min_ns, uint32_t th_ns, uint32_t tpd_ns) {
    return (int32_t)(2 * tpd_ns + tv_min_ns) - (int32_t)th_ns;
}
/* fPCLK / 2^k (k = 1..8) 중 fmax 이하인 가장 빠른 분주비. 없으면 0  (STM32 SPI BR 필드 모양 — RM 확인 권장) */
static uint32_t spi_pick_div(uint32_t fpclk_hz, uint32_t fmax_hz) {
    for (uint32_t d = 2; d <= 256; d <<= 1)
        if ((uint64_t)fpclk_hz <= (uint64_t)fmax_hz * d) return d;   /* fpclk/d ≤ fmax 를 나눗셈 없이 정확히 */
    return 0;
}

/* ---------------- 테스트 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static sim_t g_sim;

int main(void) {
    sim_t *s = &g_sim;

    /* 연습 1: 비트뱅 마스터 4모드 × 256 × 256 — 짝이 맞는 슬레이브와는 하드웨어식 마스터와 같은 결과 */
    for (unsigned mode = 0; mode < 4; mode++) {
        unsigned bad = 0;
        for (unsigned a = 0; a < 256; a++)
            for (unsigned b = 0; b < 256; b++) {
                uint8_t mtx = (uint8_t)a, stx = (uint8_t)b, mrx = 0, srx = 0;
                sim_init(s, spi_mode(mode));
                eng_setup(&s->s1, spi_mode(mode), &stx, 1, &srx, 1);
                bb_xfer(s, spi_mode(mode), &mtx, &mrx, 1);
                if (mrx != stx || srx != mtx) bad++;
            }
        CHECK(bad == 0);
    }
    /* 엣지 뒤에 읽는 비트뱅 mode 0 은 mode 1 슬레이브와도 "우연히" 맞는다. 하드웨어식(엣지에서 샘플)은 +1 밀림 */
    {
        uint8_t mtx[2] = {0x12, 0x34}, stx[2] = {0x56, 0x78}, mrx[2] = {0}, srx[2] = {0}, e[2];
        sim_init(s, spi_mode(0)); eng_setup(&s->s1, spi_mode(1), stx, 2, srx, 2);
        bb_xfer(s, spi_mode(0), mtx, mrx, 2);
        CHECK(mrx[0] == 0x56 && mrx[1] == 0x78 && srx[0] == 0x12 && srx[1] == 0x34);
        sim_init(s, spi_mode(0)); eng_setup(&s->s1, spi_mode(1), stx, 2, srx, 2);
        spi_xfer(s, spi_mode(0), mtx, mrx, 2);
        delayed_bytes(stx, 2, +1, e);
        CHECK(memcmp(mrx, e, 2) == 0 && mrx[0] == 0xAB);       /* 1|0101011 */
    }

    /* 연습 2: 루프백 — 4모드 모두 rx == tx (그래서 모드 오류는 못 잡는다) */
    for (unsigned mode = 0; mode < 4; mode++) {
        unsigned bad = 0;
        for (unsigned a = 0; a < 256; a++) {
            uint8_t tx[3] = {(uint8_t)a, (uint8_t)~a, 0x5A}, rx[3] = {0};
            sim_init(s, spi_mode(mode)); s->loopback = 1;
            eng_setup(&s->s1, spi_mode(mode), NULL, 0, NULL, 0);
            spi_xfer(s, spi_mode(mode), tx, rx, 3);
            if (memcmp(tx, rx, 3)) bad++;
        }
        CHECK(bad == 0);
    }

    /* 연습 3: 자동 mode 0/3 슬레이브 */
    for (unsigned mode = 0; mode < 4; mode++) {
        unsigned ok = 0;
        uint8_t mtx[4] = {0x81, 0x42, 0x24, 0x18}, stx[4] = {0xF0, 0x0F, 0xCC, 0x33}, mrx[4] = {0}, srx[4] = {0};
        uint8_t em[4], es[4];
        sim_init(s, spi_mode(mode)); s->auto03 = 1;
        eng_setup(&s->s1, spi_mode(0), stx, 4, srx, 4);       /* cfg 는 CS 하강 때 덮어쓴다 */
        spi_xfer(s, spi_mode(mode), mtx, mrx, 4);
        if (mode == 0 || mode == 3) ok = !memcmp(mrx, stx, 4) && !memcmp(srx, mtx, 4);
        else if (mode == 1) {                                   /* 슬레이브는 mode 0 으로 판단 → (1,0) 칸: MOSI +1 */
            delayed_bytes(mtx, 4, +1, es);
            ok = !memcmp(mrx, stx, 4) && !memcmp(srx, es, 4);
        } else {                                                /* mode 2: idle 높음 → mode 3 으로 판단 → (2,3) 칸: MISO +1 */
            delayed_bytes(stx, 4, +1, em);
            ok = !memcmp(mrx, em, 4) && !memcmp(srx, mtx, 4);
        }
        CHECK(ok);
    }

    /* 연습 4: 데이지 체인 — 마스터 → s1 → s2 → 마스터, 둘 다 mode 0 에코(8비트 시프트 레지스터) */
    {
        uint8_t f1[2] = {0xA1, 0xB2}, f2[2] = {0xC3, 0xD4}, rx[2] = {0};
        sim_init(s, spi_mode(0)); s->chain = 1;
        eng_setup(&s->s1, spi_mode(0), NULL, 0, NULL, 0); s->s1.echo = 1;
        eng_setup(&s->s2, spi_mode(0), NULL, 0, NULL, 0); s->s2.echo = 1;
        spi_xfer(s, spi_mode(0), f1, rx, 2);
        CHECK(s->s2.last_rx == 0xA1 && s->s1.last_rx == 0xB2);   /* 첫 바이트가 먼 쪽(s2)으로 */
        CHECK(rx[0] == SPI_FILL && rx[1] == SPI_FILL);
        spi_xfer(s, spi_mode(0), f2, rx, 2);
        CHECK(rx[0] == 0xA1 && rx[1] == 0xB2);                   /* 이전 내용이 밀려 나온다 */
        CHECK(s->s2.last_rx == 0xC3 && s->s1.last_rx == 0xD4);
    }

    /* 연습 5: 타이밍 예산 (숫자는 예제용 대표값) */
    CHECK(spi_fmax_hz(12, 4, 2) == 25000000u);                  /* 반주기 20 ns */
    CHECK(spi_fmax_hz(30, 5, 3) == 12195121u);                  /* 반주기 41 ns → 12.195 MHz */
    CHECK(spi_pick_div(84000000u, 25000000u) == 4);             /* 21 MHz */
    CHECK(spi_pick_div(72000000u, 10000000u) == 8);             /* 9 MHz */
    CHECK(spi_pick_div(80000000u, 40000000u) == 2);             /* 정확히 40 MHz 도 허용 */
    CHECK(spi_pick_div(80000000u, 100000u) == 0);               /* /256 = 312.5 kHz 도 너무 빠름 */
    CHECK(hold_margin_ns(2, 3, 1) == 1);                        /* 1 ns — 될 수도, 온도·보드에 따라 안 될 수도 */
    CHECK(hold_margin_ns(0, 5, 1) == -3);                       /* 음수 = hold 위반 → 가끔 틀린 비트 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
