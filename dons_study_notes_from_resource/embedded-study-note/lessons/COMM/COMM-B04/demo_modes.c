/* COMM-B04 demo_modes.c — 실험: 4모드 파형, 모드 불일치 16조합 전수 분류, 1비트 밀림 예
 *
 * solution.c 의 모델(엔진·시뮬레이터·spi_xfer·spi_predict)을 그대로 복사해 쓴다 (self-contained).
 * (1) 마스터 0xA5 / 슬레이브 0x3C 한 바이트의 4모드 파형 (step = SCLK 반주기, '-' 높음 '_' 낮음)
 * (2) 마스터 모드 × 슬레이브 모드 16조합 × 256×256 바이트 쌍(한 바이트) + 랜덤 다바이트:
 *     받은 값이 delay 0/+1/-1 중 어느 것과 항상 같은지, race(같은 엣지 샘플+변경) 수
 * (3) 다바이트에서 밀린 비트가 다음 바이트로 넘어가는 예
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_modes.c -o demo_modes && ./demo_modes
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


static const char *dname(int d, int ok) { return !ok ? " ?? " : d == 0 ? "  0 " : d > 0 ? " +1 " : " -1 "; }

int main(void) {
    spi_sim_t *s = &g_sim;
    char row[128];
    static const char *names[5] = {"CS_N", "SCLK", "MOSI", "MISO", "smpl"};

    printf("(1) master 0xA5 / slave 0x3C, 1 byte, 4 modes  (1 char = half SCLK period)\n");
    for (unsigned mode = 0; mode < 4; mode++) {
        uint8_t mtx = 0xA5, stx = 0x3C, mrx = 0, srx = 0;
        run_pair(s, mode, mode, &mtx, &stx, &mrx, &srx, 1);
        printf("mode %u (CPOL=%u CPHA=%u)  master got 0x%02X, slave got 0x%02X\n", mode, mode >> 1, mode & 1u, mrx, srx);
        for (int w = 0; w < 5; w++) { trace_row(s->tr, s->tr_n, w, row, sizeof row); printf("  %s %s\n", names[w], row); }
    }

    printf("\n(2) mode mismatch matrix: rows = master mode, cols = slave mode\n");
    printf("    cell = MISO delay / MOSI delay / race-per-byte  (measured on all 256x256 byte pairs + 2000 random multi-byte)\n");
    unsigned pred_ok = 0;
    for (unsigned mm = 0; mm < 4; mm++) {
        printf("  M%u |", mm);
        for (unsigned sm = 0; sm < 4; sm++) {
            int miso_d = 99, mosi_d = 99;
            for (int d = -1; d <= 1 && (miso_d == 99 || mosi_d == 99); d++) {
                int all_miso = 1, all_mosi = 1;
                for (unsigned a = 0; a < 256 && (all_miso || all_mosi); a++)
                    for (unsigned b = 0; b < 256; b++) {
                        uint8_t mtx = (uint8_t)a, stx = (uint8_t)b, mrx = 0, srx = 0, em, es;
                        run_pair(s, mm, sm, &mtx, &stx, &mrx, &srx, 1);
                        delayed_bytes(&stx, 1, d, &em); delayed_bytes(&mtx, 1, d, &es);
                        if (mrx != em) all_miso = 0;
                        if (srx != es) all_mosi = 0;
                    }
                for (int it = 0; it < 2000 && (all_miso || all_mosi); it++) {
                    uint8_t mtx[8], stx[8], mrx[8] = {0}, srx[8] = {0}, em[8], es[8];
                    size_t n = 1 + xorshift32() % 8;
                    for (size_t i = 0; i < n; i++) { mtx[i] = (uint8_t)xorshift32(); stx[i] = (uint8_t)xorshift32(); }
                    run_pair(s, mm, sm, mtx, stx, mrx, srx, n);
                    delayed_bytes(stx, n, d, em); delayed_bytes(mtx, n, d, es);
                    if (memcmp(mrx, em, n)) all_miso = 0;
                    if (memcmp(srx, es, n)) all_mosi = 0;
                }
                if (all_miso && miso_d == 99) miso_d = d;
                if (all_mosi && mosi_d == 99) mosi_d = d;
            }
            uint8_t mtx = 0, stx = 0, mrx, srx;
            run_pair(s, mm, sm, &mtx, &stx, &mrx, &srx, 1);
            spi_link_t p = spi_predict(mm, sm);
            if (p.miso_delay == miso_d && p.mosi_delay == mosi_d && (p.race ? 8u : 0u) == s->race_miso
                && s->race_miso == s->race_mosi) pred_ok++;
            printf(" %s/%s/%2u |", dname(miso_d, miso_d != 99), dname(mosi_d, mosi_d != 99), s->race_miso);
        }
        printf("\n");
    }
    printf("  spi_predict agrees with measurement in %u/16 cells\n", pred_ok);

    printf("\n(3) master mode 0, slave mode 1, both send 12 34 (CS held)\n");
    {
        uint8_t mtx[2] = {0x12, 0x34}, stx[2] = {0x12, 0x34}, mrx[2] = {0}, srx[2] = {0};
        run_pair(s, 0, 1, mtx, stx, mrx, srx, 2);
        printf("  master got %02X %02X   slave got %02X %02X   races MISO=%u MOSI=%u\n",
               mrx[0], mrx[1], srx[0], srx[1], s->race_miso, s->race_mosi);
        for (int w = 0; w < 5; w++) { trace_row(s->tr, s->tr_n, w, row, sizeof row); printf("  %s %s\n", names[w], row); }
    }
    printf("\n(4) mode 0, SCLK glitch before byte 1, master A5 A5 A5 / slave 3C 3C 3C\n");
    {
        uint8_t mtx[3] = {0xA5, 0xA5, 0xA5}, stx[3] = {0x3C, 0x3C, 0x3C}, mrx[3] = {0}, srx[3] = {0};
        sim_init(s, spi_mode(0)); slave_setup(s, spi_mode(0), stx, 3, srx, 3);
        s->glitch_at = 19;
        spi_xfer(s, spi_mode(0), mtx, mrx, 3);
        printf("  master got %02X %02X %02X   slave got %02X %02X %02X   slave partial=%u\n",
               mrx[0], mrx[1], mrx[2], srx[0], srx[1], srx[2], s->s.partial);
    }
    return 0;
}
