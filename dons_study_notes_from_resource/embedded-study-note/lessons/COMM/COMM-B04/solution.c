/* COMM-B04 SPI 4가지 모드(CPOL/CPHA): 비트뱅 마스터+슬레이브 시뮬레이터
 *
 * Part A: 모드 규칙(샘플 엣지 판정) + 시프트 엔진(select/sample/drive/deselect) + 마스터 클럭 생성기 spi_xfer
 *         → 4모드 × 256×256 전이중 교환 전수, LSB-first, CS 유지 다바이트 랜덤, mode 0 파형 문자열 대조
 * Part B: 모드 불일치 16조합 예측(spi_predict) vs 시뮬레이션, SCLK 글리치(비트 밀림이 CS 내내 지속),
 *         CS 를 SCLK idle 보다 먼저 내리는 드라이버 버그, CS 중간 해제(부분 바이트 폐기)
 * Part C: 에코 슬레이브(응답은 다음 바이트), 로직 애널라이저식 디코더 spi_decode
 *
 * 모델: 1 step = SCLK 반주기. SCLK 엣지에서 "모두 먼저 샘플(엣지 직전 레벨), 그다음 모두 드라이브"
 *       = hold time 이 지켜지는 동기 회로. 아무도 안 모는 MOSI/MISO 는 1 로 본다(LINE_RELEASED).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LINE_RELEASED 1u   /* 몰지 않는 데이터 선의 레벨 (풀업/hi-Z 를 1 로 본다 — 모델 선택) */
#define SPI_FILL      0xFFu /* 보낼 바이트가 떨어지면 내보내는 값 */
#define SPI_MAX_XFER  64u
#define TRACE_CAP     4096u

typedef struct { uint8_t cpol, cpha, lsb_first; } spi_cfg_t;

static spi_cfg_t spi_mode(unsigned mode) {             /* mode = CPOL<<1 | CPHA */
    spi_cfg_t c = { (uint8_t)(mode >> 1 & 1u), (uint8_t)(mode & 1u), 0 };
    return c;
}

/* 시프트 엔진: 마스터(MOSI 를 몰고 MISO 를 샘플)와 슬레이브(반대)가 같은 구조를 쓴다 */
typedef struct {
    spi_cfg_t cfg;
    uint8_t tx_sr, rx_sr;      /* 시프트 레지스터 */
    uint8_t out_cnt;           /* tx_sr 에서 이미 선에 올린 비트 수 0..8 */
    uint8_t in_cnt;            /* rx_sr 에 모은 비트 수 0..7 */
    uint8_t out;               /* 이 엔진이 모는 선의 레벨 */
    const uint8_t *tx; size_t tx_n, tx_i;   /* 보낼 바이트 (다 쓰면 SPI_FILL) */
    uint8_t *rx; size_t rx_cap, rx_n;       /* 받은 바이트 (넘치면 rx_drop++) */
    uint8_t echo, last_rx;     /* echo=1: 다음에 보낼 바이트 = 마지막으로 받은 바이트 (Part C) */
    unsigned rx_drop, partial; /* partial: 8비트를 못 채우고 CS 가 올라간 횟수 */
} spi_eng_t;

static unsigned bitpos(const spi_cfg_t *c, unsigned k) {   /* k 번째로 나가는 비트의 위치 */
    return c->lsb_first ? k : 7u - k;
}
static uint8_t eng_next_byte(spi_eng_t *e) {
    if (e->echo) return e->last_rx;
    return e->tx_i < e->tx_n ? e->tx[e->tx_i++] : (uint8_t)SPI_FILL;
}
static void eng_store(spi_eng_t *e, uint8_t b) {
    e->last_rx = b;
    if (e->rx_n < e->rx_cap) e->rx[e->rx_n++] = b; else e->rx_drop++;
}

/* ---------------- Part A: 모드 규칙과 시프트 엔진 ---------------- */

/* SCLK 가 new_sclk 로 바뀌는 엣지가 "샘플" 엣지인가 (아니면 "시프트(드라이브)" 엣지).
 * leading(첫 번째) 엣지 = idle(CPOL) 에서 벗어나는 엣지. CPHA=0 은 leading 에서, CPHA=1 은 trailing 에서 샘플. */
static int spi_is_sample_edge(const spi_cfg_t *c, uint8_t new_sclk) {
    int leading = new_sclk != c->cpol;
    return leading ^ c->cpha;
}

/* CS 하강: 카운터 초기화, 첫 바이트 적재. CPHA=0 이면 첫 엣지 전에 첫 비트를 선에 올려 둔다. */
static void eng_select(spi_eng_t *e) {
    e->in_cnt = 0; e->rx_sr = 0;
    e->tx_sr = eng_next_byte(e); e->out_cnt = 0;
    if (e->cfg.cpha == 0) { e->out = (uint8_t)(e->tx_sr >> bitpos(&e->cfg, 0) & 1u); e->out_cnt = 1; }
    else e->out = LINE_RELEASED;   /* CPHA=1: 첫 비트는 첫(leading) 엣지에서 나간다 */
}

/* 샘플 엣지: 한 비트 받아 넣고, 8비트가 차면 바이트 하나 완성 */
static void eng_sample(spi_eng_t *e, uint8_t level) {
    if (e->cfg.lsb_first) e->rx_sr = (uint8_t)(e->rx_sr >> 1 | level << 7);
    else                  e->rx_sr = (uint8_t)(e->rx_sr << 1 | level);
    if (++e->in_cnt == 8) { eng_store(e, e->rx_sr); e->in_cnt = 0; e->rx_sr = 0; }
}

/* 시프트 엣지: 다음 비트를 선에 올린다. 현재 바이트를 다 보냈으면 다음 바이트 적재 */
static void eng_drive(spi_eng_t *e) {
    if (e->out_cnt == 8) { e->tx_sr = eng_next_byte(e); e->out_cnt = 0; }
    e->out = (uint8_t)(e->tx_sr >> bitpos(&e->cfg, e->out_cnt) & 1u);
    e->out_cnt++;
}

/* CS 상승: 덜 찬 바이트는 버리고 센다 (CS 가 비트 카운터를 리셋 = SPI 의 프레임 동기) */
static void eng_deselect(spi_eng_t *e) {
    if (e->in_cnt != 0) { e->partial++; e->in_cnt = 0; e->rx_sr = 0; }
}

/* ---------------- 버스 시뮬레이터 (주어짐) ---------------- */
typedef struct { uint8_t cs_n, sclk, mosi, miso, ev; } spi_sample_t;   /* ev bit0 = 마스터 샘플, bit1 = 슬레이브 샘플 */

typedef struct {
    uint8_t cs_n, sclk, mosi, miso;   /* 선 레벨 */
    spi_eng_t m, s;                   /* 마스터 엔진, 슬레이브 엔진 */
    uint8_t m_active;                 /* 마스터가 지금 클럭을 세고 있나 */
    unsigned race_miso, race_mosi;    /* 같은 엣지에서 한쪽은 샘플, 다른 쪽은 그 선을 바꿈 (hold time 의존) */
    long t, glitch_at;                /* step 번호, SCLK 글리치를 넣을 step (-1 = 없음) */
    spi_sample_t tr[TRACE_CAP]; size_t tr_n; unsigned tr_drop;
} spi_sim_t;

static void sim_step(spi_sim_t *s, uint8_t cs_n, uint8_t sclk);

static void sim_glitch(spi_sim_t *s) {   /* SCLK 선에 짧은 펄스 하나 (노이즈·링잉) — 마스터 엔진은 모른다 */
    uint8_t a = s->m_active; s->m_active = 0;
    sim_step(s, s->cs_n, (uint8_t)(s->sclk ^ 1u));
    sim_step(s, s->cs_n, (uint8_t)(s->sclk ^ 1u));
    s->m_active = a;
}

static void sim_step(spi_sim_t *s, uint8_t cs_n, uint8_t sclk) {
    if (s->t == s->glitch_at) { s->glitch_at = -1; sim_glitch(s); }
    s->t++;
    uint8_t ev = 0;
    int was_sel = s->cs_n == 0, now_sel = cs_n == 0;
    if (was_sel && now_sel && sclk != s->sclk) {             /* 선택된 동안의 SCLK 엣지 */
        int ms = s->m_active && spi_is_sample_edge(&s->m.cfg, sclk);
        int md = s->m_active && !ms;
        int ss = spi_is_sample_edge(&s->s.cfg, sclk), sd = !ss;
        if (ms) { eng_sample(&s->m, s->miso); ev |= 1; }     /* 1단계: 엣지 직전 레벨로 샘플 */
        if (ss) { eng_sample(&s->s, s->mosi); ev |= 2; }
        if (ms && sd) s->race_miso++;
        if (ss && md) s->race_mosi++;
        if (md) eng_drive(&s->m);                            /* 2단계: 드라이브 */
        if (sd) eng_drive(&s->s);
    }
    if (!was_sel && now_sel) eng_select(&s->s);
    if (was_sel && !now_sel) eng_deselect(&s->s);
    s->cs_n = cs_n; s->sclk = sclk;
    s->mosi = s->m.out;                                      /* 마스터는 MOSI 를 항상 몬다 */
    s->miso = now_sel ? s->s.out : (uint8_t)LINE_RELEASED;   /* 선택 안 된 슬레이브는 MISO 를 놓는다 */
    if (s->tr_n < TRACE_CAP) {
        spi_sample_t x = { s->cs_n, s->sclk, s->mosi, s->miso, ev };
        s->tr[s->tr_n++] = x;
    } else s->tr_drop++;
}

static void sim_init(spi_sim_t *s, spi_cfg_t master_cfg) {
    memset(s, 0, sizeof *s);
    s->cs_n = 1; s->sclk = master_cfg.cpol;
    s->m.cfg = master_cfg; s->m.out = LINE_RELEASED; s->m.last_rx = SPI_FILL;
    s->s.out = LINE_RELEASED; s->s.last_rx = SPI_FILL;
    s->mosi = s->miso = LINE_RELEASED;
    s->glitch_at = -1;
}

/* 슬레이브 준비: 모드, 보낼 바이트, 받을 버퍼 */
static void slave_setup(spi_sim_t *s, spi_cfg_t cfg, const uint8_t *tx, size_t tx_n, uint8_t *rx, size_t rx_cap) {
    spi_eng_t *e = &s->s;
    e->cfg = cfg; e->tx = tx; e->tx_n = tx_n; e->tx_i = 0;
    e->rx = rx; e->rx_cap = rx ? rx_cap : 0; e->rx_n = 0;
    e->rx_drop = 0; e->partial = 0; e->echo = 0; e->last_rx = SPI_FILL;
}

/* ---------------- Part A: 비트뱅 마스터 (클럭 생성기) ---------------- */

/* CS 를 내리고 n 바이트를 전이중으로 주고받고 CS 를 올린다. rx 는 n 바이트 (NULL 이면 버림).
 * 순서: ① CS 높은 채로 SCLK 를 이 모드의 idle(CPOL) 로  ② CS 하강  ③ 반주기 대기(마스터 첫 비트 setup)
 *       ④ 16·n 개 엣지  ⑤ CS 상승.  반환 0, n 이 1..SPI_MAX_XFER 밖이면 -1. */
static int spi_xfer(spi_sim_t *s, spi_cfg_t cfg, const uint8_t *tx, uint8_t *rx, size_t n) {
    if (n == 0 || n > SPI_MAX_XFER) return -1;
    spi_eng_t *m = &s->m;
    m->cfg = cfg; m->tx = tx; m->tx_n = n; m->tx_i = 0;
    m->rx = rx; m->rx_cap = rx ? n : 0; m->rx_n = 0;
    sim_step(s, 1, cfg.cpol);                       /* ① idle 레벨부터 */
    sim_step(s, 0, cfg.cpol);                       /* ② CS 하강 → 슬레이브 select */
    eng_select(m); s->m_active = 1;
    sim_step(s, 0, cfg.cpol);                       /* ③ CPHA=0 이면 마스터 첫 비트가 MOSI 에 */
    for (size_t i = 0; i < 16 * n; i++)
        sim_step(s, 0, (uint8_t)(s->sclk ^ 1u));   /* ④ 한 바이트 = 8 클럭 = 16 엣지 */
    s->m_active = 0;
    sim_step(s, 1, cfg.cpol);                       /* ⑤ CS 상승 */
    return 0;
}

/* ---------------- Part B: 모드 불일치 예측 ---------------- */
/* delay: 받은 비트열 = 보낸 비트열을 delay 비트 늦춘 것. +1 = 맨 앞에 몰지 않은 비트(1)가 끼고 한 칸씩 밀림,
 *        -1 = 첫 비트를 놓치고 한 칸씩 당겨짐. race = 모든 샘플이 상대가 선을 바꾸는 바로 그 엣지에서 일어남. */
typedef struct { int8_t miso_delay, mosi_delay; uint8_t race; } spi_link_t;

/* 송신자(CPHA, 홀수 엣지에서 샘플하나)와 수신자(홀수 엣지에서 샘플하나)로 지연 계산.
 * 엣지 번호: CS 하강 = 0, 마스터 기준 e1(leading), e2, ... */
static int link_delay(int tx_cpha, int tx_odd, int rx_odd) {
    int first = tx_odd ? 2 : 1;                /* 송신자의 첫 드라이브 엣지 (샘플 안 하는 쪽 짝/홀) */
    int d0 = tx_cpha == 0 ? 0 : first;         /* 첫 비트가 선에 올라가는 엣지 */
    int d1 = tx_cpha == 0 ? first : first + 2; /* 둘째 비트가 올라가는 엣지 (= 첫 비트가 사라지는 엣지) */
    int e0 = rx_odd ? 1 : 2;                   /* 수신자의 첫 샘플 엣지 — 엣지 직전 레벨을 읽는다 */
    return e0 <= d0 ? +1 : e0 <= d1 ? 0 : -1;
}

static spi_link_t spi_predict(unsigned master_mode, unsigned slave_mode) {
    spi_cfg_t m = spi_mode(master_mode), sl = spi_mode(slave_mode);
    int m_odd = m.cpha == 0;                              /* 마스터: CPHA=0 → leading(홀수) 엣지에서 샘플 */
    int s_odd = (m.cpol ^ sl.cpol ^ sl.cpha) == 0;        /* 슬레이브: 홀수 엣지가 자기 샘플 엣지인가 */
    spi_link_t r;
    r.race = (uint8_t)(m_odd != s_odd);
    r.miso_delay = (int8_t)link_delay(sl.cpha, s_odd, m_odd);
    r.mosi_delay = (int8_t)link_delay(m.cpha, m_odd, s_odd);
    return r;
}

/* ---------------- Part C: 로직 애널라이저식 디코더 ---------------- */
typedef struct {
    uint8_t *mosi, *miso; size_t cap, n;   /* 디코드된 바이트 (MOSI/MISO 짝) */
    unsigned partial, overflow;
} spi_dec_t;

/* 캡처(step 마다 선 레벨)를 cfg 로 디코드. CS 하강 전의 엣지는 무시(캡처가 프레임 중간에서 시작한 경우).
 * 샘플 값 = 엣지 직전 샘플의 레벨. 반환 0, 버퍼가 모자라면 -1 (overflow 에 버린 바이트 수). */
static int spi_decode(const spi_sample_t *tr, size_t len, spi_cfg_t cfg, spi_dec_t *o) {
    o->n = 0; o->partial = 0; o->overflow = 0;
    int in_frame = 0; unsigned cnt = 0; uint8_t a = 0, b = 0;
    for (size_t i = 1; i < len; i++) {
        const spi_sample_t *p = &tr[i - 1], *c = &tr[i];
        if (p->cs_n && !c->cs_n) { in_frame = 1; cnt = 0; a = b = 0; continue; }
        if (!p->cs_n && c->cs_n) { if (in_frame && cnt) o->partial++; in_frame = 0; continue; }
        if (!in_frame || c->cs_n || p->sclk == c->sclk || !spi_is_sample_edge(&cfg, c->sclk)) continue;
        if (cfg.lsb_first) { a = (uint8_t)(a >> 1 | p->mosi << 7); b = (uint8_t)(b >> 1 | p->miso << 7); }
        else               { a = (uint8_t)(a << 1 | p->mosi);      b = (uint8_t)(b << 1 | p->miso); }
        if (++cnt == 8) {
            if (o->n < o->cap) { o->mosi[o->n] = a; o->miso[o->n] = b; o->n++; } else o->overflow++;
            cnt = 0; a = b = 0;
        }
    }
    return o->overflow ? -1 : 0;
}

/* ---------------- 테스트 도우미 (주어짐) ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t rng = 0xC0FFEEu;
static uint32_t xorshift32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static spi_sim_t g_sim;   /* 20 KB 트레이스 — 스택 대신 정적 */

/* 캡처 한 줄을 문자열로: 0=CS 1=SCLK 2=MOSI 3=MISO ('-' 높음, '_' 낮음), 4=샘플 표시 (M/S/B/.) */
static void trace_row(const spi_sample_t *tr, size_t n, int which, char *out, size_t cap) {
    size_t i = 0;
    for (; i < n && i + 1 < cap; i++) {
        const spi_sample_t *x = &tr[i];
        if (which == 4) out[i] = ".MSB"[x->ev & 3u];
        else {
            uint8_t v = which == 0 ? x->cs_n : which == 1 ? x->sclk : which == 2 ? x->mosi : x->miso;
            out[i] = v ? '-' : '_';
        }
    }
    out[i] = '\0';
}

/* 보낸 바이트열(뒤는 SPI_FILL)을 비트열로 보고 delay 만큼 늦춘 n 바이트 (MSB first). 앞에 끼는 비트 = LINE_RELEASED */
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

/* 마스터 모드 mm, 슬레이브 모드 sm 으로 한 번 교환 */
static void run_pair(spi_sim_t *s, unsigned mm, unsigned sm, const uint8_t *mtx, const uint8_t *stx,
                     uint8_t *mrx, uint8_t *srx, size_t n) {
    sim_init(s, spi_mode(mm));
    slave_setup(s, spi_mode(sm), stx, n, srx, n);
    spi_xfer(s, spi_mode(mm), mtx, mrx, n);
}

/* 버그 있는 드라이버: CS 를 먼저 내리고 나서 SCLK 를 새 모드의 idle 로 옮긴다 (주어짐 — 버그 재현용) */
static void spi_xfer_cs_first(spi_sim_t *s, spi_cfg_t cfg, const uint8_t *tx, uint8_t *rx, size_t n) {
    spi_eng_t *m = &s->m;
    m->cfg = cfg; m->tx = tx; m->tx_n = n; m->tx_i = 0; m->rx = rx; m->rx_cap = n; m->rx_n = 0;
    sim_step(s, 0, s->sclk);                        /* CS 먼저 (SCLK 는 이전 장치의 idle 그대로) */
    sim_step(s, 0, cfg.cpol);                       /* 그다음 idle 변경 → 선택된 슬레이브에겐 엣지! */
    eng_select(m); s->m_active = 1;
    sim_step(s, 0, cfg.cpol);
    for (size_t i = 0; i < 16 * n; i++) sim_step(s, 0, (uint8_t)(s->sclk ^ 1u));
    s->m_active = 0;
    sim_step(s, 1, cfg.cpol);
}

int main(void) {
    spi_sim_t *s = &g_sim;
    char row[128];

    /* ===== Part A ===== */
    /* 모드 규칙: mode 0/3 은 상승 엣지에서, mode 1/2 는 하강 엣지에서 샘플 */
    CHECK(spi_is_sample_edge(&(spi_cfg_t){0, 0, 0}, 1) == 1);   /* mode 0 상승 = 샘플 */
    CHECK(spi_is_sample_edge(&(spi_cfg_t){0, 0, 0}, 0) == 0);
    CHECK(spi_is_sample_edge(&(spi_cfg_t){0, 1, 0}, 1) == 0);   /* mode 1 상승 = 시프트 */
    CHECK(spi_is_sample_edge(&(spi_cfg_t){0, 1, 0}, 0) == 1);
    CHECK(spi_is_sample_edge(&(spi_cfg_t){1, 0, 0}, 0) == 1);   /* mode 2 하강(leading) = 샘플 */
    CHECK(spi_is_sample_edge(&(spi_cfg_t){1, 0, 0}, 1) == 0);
    CHECK(spi_is_sample_edge(&(spi_cfg_t){1, 1, 0}, 1) == 1);   /* mode 3 상승(trailing) = 샘플 */
    CHECK(spi_is_sample_edge(&(spi_cfg_t){1, 1, 0}, 0) == 0);

    /* mode 0, 마스터 0xA5 / 슬레이브 0x3C 한 바이트 — 파형을 글자로 대조 (5. 풀이의 표와 그림) */
    {
        uint8_t mtx = 0xA5, stx = 0x3C, mrx = 0, srx = 0;
        run_pair(s, 0, 0, &mtx, &stx, &mrx, &srx, 1);
        CHECK(mrx == 0x3C && srx == 0xA5);
        CHECK(s->tr_n == 20);                                  /* 16·1 + 4 step */
        trace_row(s->tr, s->tr_n, 0, row, sizeof row); CHECK(strcmp(row, "-__________________-") == 0);
        trace_row(s->tr, s->tr_n, 1, row, sizeof row); CHECK(strcmp(row, "___-_-_-_-_-_-_-_-__") == 0);
        trace_row(s->tr, s->tr_n, 2, row, sizeof row); CHECK(strcmp(row, "----__--____--__----") == 0);
        trace_row(s->tr, s->tr_n, 3, row, sizeof row); CHECK(strcmp(row, "-_____--------____--") == 0);
        trace_row(s->tr, s->tr_n, 4, row, sizeof row); CHECK(strcmp(row, "...B.B.B.B.B.B.B.B..") == 0);
    }

    /* 4모드 × 256 × 256 한 바이트 전이중 — 전수 */
    for (unsigned mode = 0; mode < 4; mode++) {
        unsigned bad = 0;
        for (unsigned a = 0; a < 256; a++)
            for (unsigned b = 0; b < 256; b++) {
                uint8_t mtx = (uint8_t)a, stx = (uint8_t)b, mrx = 0, srx = 0;
                run_pair(s, mode, mode, &mtx, &stx, &mrx, &srx, 1);
                if (mrx != stx || srx != mtx || s->race_miso || s->race_mosi || s->s.partial) bad++;
                if (s->tr[1].sclk != (mode >> 1) || s->tr[s->tr_n - 1].sclk != (mode >> 1)) bad++;  /* CS 경계에서 SCLK = idle */
            }
        CHECK(bad == 0);
    }

    /* LSB-first: 양쪽 LSB → 정상, 한쪽만 LSB → 비트 순서가 뒤집힌 바이트 */
    for (unsigned mode = 0; mode < 4; mode++) {
        unsigned bad = 0, rev_ok = 0;
        for (unsigned a = 0; a < 256; a++) {
            spi_cfg_t c = spi_mode(mode); c.lsb_first = 1;
            uint8_t mtx = (uint8_t)a, stx = (uint8_t)~a, mrx = 0, srx = 0;
            sim_init(s, c); slave_setup(s, c, &stx, 1, &srx, 1); spi_xfer(s, c, &mtx, &mrx, 1);
            if (mrx != stx || srx != mtx) bad++;
            uint8_t r = 0;                                   /* 슬레이브만 MSB first */
            sim_init(s, c); slave_setup(s, spi_mode(mode), &stx, 1, &srx, 1); spi_xfer(s, c, &mtx, &mrx, 1);
            for (unsigned k = 0; k < 8; k++) r = (uint8_t)(r << 1 | (a >> k & 1u));   /* r = bitrev(a) */
            if (srx == r) rev_ok++;
        }
        CHECK(bad == 0);
        CHECK(rev_ok == 256);
    }

    /* CS 유지 다바이트, 고정 시드 랜덤: 4모드 × 500회, n = 1..16 */
    for (unsigned mode = 0; mode < 4; mode++) {
        unsigned bad = 0;
        for (int it = 0; it < 500; it++) {
            uint8_t mtx[16], stx[16], mrx[16], srx[16];
            size_t n = 1 + xorshift32() % 16;
            for (size_t i = 0; i < n; i++) { mtx[i] = (uint8_t)xorshift32(); stx[i] = (uint8_t)xorshift32(); }
            memset(mrx, 0, sizeof mrx); memset(srx, 0, sizeof srx);
            run_pair(s, mode, mode, mtx, stx, mrx, srx, n);
            if (memcmp(mrx, stx, n) || memcmp(srx, mtx, n) || s->s.rx_n != n || s->tr_n != 16 * n + 4) bad++;
        }
        CHECK(bad == 0);
    }

    /* 경계: n = 0, n > MAX 는 거부, rx = NULL 이면 받은 바이트를 버리고 센다 */
    {
        uint8_t tx[SPI_MAX_XFER + 1] = {0};
        sim_init(s, spi_mode(0));
        CHECK(spi_xfer(s, spi_mode(0), tx, NULL, 0) == -1);
        CHECK(spi_xfer(s, spi_mode(0), tx, NULL, SPI_MAX_XFER + 1) == -1);
        CHECK(s->tr_n == 0);                                   /* 거부하면 버스를 건드리지 않는다 */
        slave_setup(s, spi_mode(0), NULL, 0, NULL, 0);
        CHECK(spi_xfer(s, spi_mode(0), tx, NULL, 3) == 0 && s->m.rx_drop == 3 && s->s.rx_drop == 3);
    }

    /* ===== Part B ===== */
    /* 예측 표의 대표 칸 (5. 풀이 표) */
    {
        spi_link_t p;
        p = spi_predict(0, 0); CHECK(p.miso_delay == 0 && p.mosi_delay == 0 && !p.race);
        p = spi_predict(0, 1); CHECK(p.miso_delay == 1 && p.mosi_delay == 0 && p.race);
        p = spi_predict(1, 0); CHECK(p.miso_delay == 0 && p.mosi_delay == 1 && p.race);
        p = spi_predict(0, 2); CHECK(p.miso_delay == 0 && p.mosi_delay == 0 && p.race);
        p = spi_predict(0, 3); CHECK(p.miso_delay == 1 && p.mosi_delay == 0 && !p.race);
        p = spi_predict(3, 0); CHECK(p.miso_delay == -1 && p.mosi_delay == 0 && !p.race);
        p = spi_predict(1, 3); CHECK(p.miso_delay == 1 && p.mosi_delay == 1 && p.race);
    }

    /* 16조합 전부: 예측 = 시뮬레이션 (랜덤 다바이트 200회씩) */
    for (unsigned mm = 0; mm < 4; mm++)
        for (unsigned sm = 0; sm < 4; sm++) {
            spi_link_t p = spi_predict(mm, sm);
            unsigned bad = 0;
            for (int it = 0; it < 200; it++) {
                uint8_t mtx[8], stx[8], mrx[8] = {0}, srx[8] = {0}, em[8], es[8];
                size_t n = 1 + xorshift32() % 8;
                for (size_t i = 0; i < n; i++) { mtx[i] = (uint8_t)xorshift32(); stx[i] = (uint8_t)xorshift32(); }
                run_pair(s, mm, sm, mtx, stx, mrx, srx, n);
                delayed_bytes(stx, n, p.miso_delay, em);
                delayed_bytes(mtx, n, p.mosi_delay, es);
                unsigned races = p.race ? (unsigned)(8 * n) : 0;
                if (memcmp(mrx, em, n) || memcmp(srx, es, n) || s->race_miso != races || s->race_mosi != races) bad++;
            }
            CHECK(bad == 0);
        }

    /* 1비트 밀림 재현: 마스터 mode 0, 슬레이브 mode 1 — 이전 바이트의 LSB 가 다음 바이트 MSB 로 */
    {
        uint8_t mtx[2] = {0x12, 0x34}, stx[2] = {0x12, 0x34}, mrx[2] = {0}, srx[2] = {0};
        run_pair(s, 0, 1, mtx, stx, mrx, srx, 2);
        CHECK(mrx[0] == 0x89 && mrx[1] == 0x1A);              /* 1|0001001, 0|0011010 */
        CHECK(srx[0] == 0x12 && srx[1] == 0x34);              /* MOSI 쪽은 맞게 받지만 race (hold time 의존) */
        CHECK(s->race_mosi == 16 && s->race_miso == 16);
    }

    /* SCLK 글리치: mode 0, 바이트 1 직전에 펄스 하나 → CS 가 올라갈 때까지 계속 밀린다 */
    {
        uint8_t mtx[3] = {0xA5, 0xA5, 0xA5}, stx[3] = {0x3C, 0x3C, 0x3C}, mrx[3] = {0}, srx[3] = {0};
        sim_init(s, spi_mode(0));
        slave_setup(s, spi_mode(0), stx, 3, srx, 3);
        s->glitch_at = 3 + 16;                                 /* ①②③ + 바이트 0 의 16 엣지 뒤 */
        spi_xfer(s, spi_mode(0), mtx, mrx, 3);
        CHECK(srx[0] == 0xA5 && srx[1] == 0xD2 && srx[2] == 0xD2);   /* 슬레이브는 한 비트를 더 받음 */
        CHECK(mrx[0] == 0x3C && mrx[1] == 0x78 && mrx[2] == 0x79);   /* 마스터는 한 비트를 놓침 */
        CHECK(s->s.partial == 1);                              /* CS 상승 때 1비트가 남아 있었다 → 검출 가능 */
        slave_setup(s, spi_mode(0), stx, 3, srx, 3);           /* CS 를 한 번 올렸다 내리면 복구 */
        spi_xfer(s, spi_mode(0), mtx, mrx, 3);
        CHECK(memcmp(srx, mtx, 3) == 0 && memcmp(mrx, stx, 3) == 0 && s->s.partial == 0);
    }

    /* CS 를 SCLK idle 보다 먼저 내리는 버그: mode 0 장치 다음에 mode 3 장치 */
    {
        uint8_t a = 0x55, b = 0x00, rx = 0, wr = 0x80, srx = 0, stx = 0x3C;
        sim_init(s, spi_mode(0));
        slave_setup(s, spi_mode(0), &b, 1, &rx, 1);
        spi_xfer(s, spi_mode(0), &a, &rx, 1);                  /* 이제 SCLK = 0, MOSI = 1 */
        slave_setup(s, spi_mode(3), &stx, 1, &srx, 1);
        spi_xfer_cs_first(s, spi_mode(3), &wr, &rx, 1);
        CHECK(srx == 0xC0);                                    /* 0x80 을 썼는데 0xC0 이 들어감 */
        CHECK(rx == 0x3C);                                     /* 읽기 방향은 멀쩡 — 그래서 늦게 발견된다 */
        CHECK(s->s.partial == 1);
        sim_init(s, spi_mode(0));                              /* 고친 순서(spi_xfer)로 같은 시나리오 */
        slave_setup(s, spi_mode(0), &b, 1, &rx, 1);
        spi_xfer(s, spi_mode(0), &a, &rx, 1);
        slave_setup(s, spi_mode(3), &stx, 1, &srx, 1);
        spi_xfer(s, spi_mode(3), &wr, &rx, 1);
        CHECK(srx == 0x80 && rx == 0x3C && s->s.partial == 0);
    }

    /* CS 중간 해제: 6비트만 보내고 CS 상승 → 슬레이브는 부분 바이트를 버린다. 다음 프레임은 정상 */
    {
        uint8_t mtx = 0xA5, stx = 0x3C, mrx = 0, srx = 0;
        sim_init(s, spi_mode(1));
        slave_setup(s, spi_mode(1), &stx, 1, &srx, 1);
        s->m.cfg = spi_mode(1); s->m.tx = &mtx; s->m.tx_n = 1; s->m.tx_i = 0; s->m.rx = &mrx; s->m.rx_cap = 1;
        sim_step(s, 0, 0); eng_select(&s->m); s->m_active = 1; sim_step(s, 0, 0);
        for (int i = 0; i < 12; i++) sim_step(s, 0, (uint8_t)(s->sclk ^ 1u));
        s->m_active = 0; sim_step(s, 1, 0);
        CHECK(s->s.partial == 1 && s->s.rx_n == 0);
        slave_setup(s, spi_mode(1), &stx, 1, &srx, 1);
        spi_xfer(s, spi_mode(1), &mtx, &mrx, 1);
        CHECK(srx == 0xA5 && mrx == 0x3C && s->s.partial == 0);
    }

    /* ===== Part C ===== */
    /* 에코 슬레이브: 응답은 한 바이트 늦게 온다 (전이중 = 같은 바이트 안에선 대답 못 함) */
    for (unsigned mode = 0; mode < 4; mode++) {
        uint8_t mtx[4] = {0x11, 0x22, 0x33, 0x44}, mrx[4] = {0}, srx[4] = {0};
        sim_init(s, spi_mode(mode));
        slave_setup(s, spi_mode(mode), NULL, 0, srx, 4);
        s->s.echo = 1;
        spi_xfer(s, spi_mode(mode), mtx, mrx, 4);
        CHECK(mrx[0] == SPI_FILL && mrx[1] == 0x11 && mrx[2] == 0x22 && mrx[3] == 0x33);
    }

    /* CPHA=0 엔진은 마지막 trailing 엣지에서 다음 바이트를 미리 꺼낸다 → CS 가 올라가면 그 바이트는 사라진다.
     * 큐 하나로 두 프레임을 보내면 mode 0/2 는 stx[2] 를 건너뛰고, mode 1/3 은 그대로 이어진다. */
    for (unsigned mode = 0; mode < 4; mode++) {
        uint8_t stx[6] = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5}, mtx[2] = {0, 0}, mrx[4] = {0};
        sim_init(s, spi_mode(mode));
        slave_setup(s, spi_mode(mode), stx, 6, NULL, 0);
        spi_xfer(s, spi_mode(mode), mtx, mrx, 2);
        spi_xfer(s, spi_mode(mode), mtx, mrx + 2, 2);
        uint8_t want2 = (mode & 1u) ? 0xA2 : 0xA3;
        CHECK(mrx[0] == 0xA0 && mrx[1] == 0xA1 && mrx[2] == want2 && mrx[3] == want2 + 1);
    }

    /* 디코더: 4모드 × 캡처 3프레임 → MOSI/MISO 바이트 복원 */
    for (unsigned mode = 0; mode < 4; mode++) {
        uint8_t mtx[12], stx[12], mrx[12] = {0}, srx[12] = {0}, dm[16] = {0}, ds[16] = {0};
        for (int i = 0; i < 12; i++) { mtx[i] = (uint8_t)xorshift32(); stx[i] = (uint8_t)xorshift32(); }
        sim_init(s, spi_mode(mode));                           /* 슬레이브는 프레임(CS)마다 TX 를 다시 준비 */
        slave_setup(s, spi_mode(mode), stx, 3, srx, 3);     spi_xfer(s, spi_mode(mode), mtx, mrx, 3);
        slave_setup(s, spi_mode(mode), stx + 3, 4, srx, 4); spi_xfer(s, spi_mode(mode), mtx + 3, mrx + 3, 4);
        slave_setup(s, spi_mode(mode), stx + 7, 5, srx, 5); spi_xfer(s, spi_mode(mode), mtx + 7, mrx + 7, 5);
        spi_dec_t d = { dm, ds, sizeof dm, 0, 0, 0 };
        CHECK(spi_decode(s->tr, s->tr_n, spi_mode(mode), &d) == 0 && d.n == 12 && d.partial == 0
              && memcmp(dm, mtx, 12) == 0 && memcmp(ds, stx, 12) == 0);
        spi_dec_t small = { dm, ds, 5, 0, 0, 0 };              /* 버퍼 5바이트 → -1, 7바이트 버림 */
        CHECK(spi_decode(s->tr, s->tr_n, spi_mode(mode), &small) == -1 && small.n == 5 && small.overflow == 7);
        /* 캡처가 첫 프레임 중간에서 시작 → 그 프레임은 건너뛰고 나머지 두 프레임만 */
        spi_dec_t mid = { dm, ds, sizeof dm, 0, 0, 0 };
        CHECK(spi_decode(s->tr + 10, s->tr_n - 10, spi_mode(mode), &mid) == 0 && mid.n == 9
              && memcmp(dm, mtx + 3, 9) == 0);
    }

    /* 디코더는 슬레이브가 본 것을 본다: 글리치 캡처를 디코드하면 슬레이브 수신값 + partial */
    {
        uint8_t mtx[3] = {0xA5, 0xA5, 0xA5}, stx[3] = {0x3C, 0x3C, 0x3C}, mrx[3] = {0}, srx[3] = {0}, dm[4] = {0}, ds[4] = {0};
        sim_init(s, spi_mode(0));
        slave_setup(s, spi_mode(0), stx, 3, srx, 3);
        s->glitch_at = 19;
        spi_xfer(s, spi_mode(0), mtx, mrx, 3);
        spi_dec_t d = { dm, ds, 4, 0, 0, 0 };
        spi_decode(s->tr, s->tr_n, spi_mode(0), &d);
        CHECK(d.n == 3 && memcmp(dm, srx, 3) == 0 && d.partial == 1);
    }

    /* 디코더 모드를 잘못 고르면: mode 1 캡처를 mode 0 으로 → MOSI 가 한 비트 늦게 보인다 */
    {
        uint8_t mtx[2] = {0x12, 0x34}, stx[2] = {0, 0}, mrx[2] = {0}, srx[2] = {0}, dm[2] = {0}, ds[2] = {0}, e[2];
        run_pair(s, 1, 1, mtx, stx, mrx, srx, 2);
        spi_dec_t d = { dm, ds, 2, 0, 0, 0 };
        spi_decode(s->tr, s->tr_n, spi_mode(0), &d);
        delayed_bytes(mtx, 2, +1, e);
        CHECK(d.n == 2 && memcmp(dm, e, 2) == 0 && dm[0] == 0x89);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
