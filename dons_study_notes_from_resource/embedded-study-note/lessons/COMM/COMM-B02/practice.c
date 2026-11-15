/* COMM-B02 추가 연습 — 한 번에 검증
 * 연습 1 overrun 덮어쓰기 정책 (STM32 OVRDIS 비슷) · 연습 2 8x 오버샘플링 (샘플 4/5/6)
 * 연습 3 LIN 식 break 검출 (low 길이 ≥ 11 비트) · 연습 4 IDLE line 검출 (문자 끝 뒤 1문자 시간 high)
 * 연습 5 재무장 없는 수신기의 "삼켜진 start edge" 재현
 * 수신기는 본문 solution.c 를 OVS·샘플 위치·overrun 정책·재무장을 바꿀 수 있게 일반화한 축약판.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef enum { PAR_NONE = 0, PAR_EVEN, PAR_ODD } parity_t;
typedef struct { uint8_t data_bits; parity_t parity; uint8_t stop_bits; } uart_cfg_t;
enum { SR_RXNE = 1u << 0, SR_ORE = 1u << 1, SR_NF = 1u << 2, SR_FE = 1u << 3, SR_PE = 1u << 4, SR_BRK = 1u << 5 };
typedef enum { RX_IDLE = 0, RX_START, RX_DATA, RX_PARITY, RX_STOP } rx_state_t;

static int parity_of(uint16_t v) { int p = 0; while (v) { p ^= v & 1u; v >>= 1; } return p; }

/* ---------------- 일반화한 수신기 (본문 모델 축약판) ---------------- */
#define LOG_CAP 512
typedef struct { uint16_t data; uint8_t flags; uint8_t false_start; } ev_t;
typedef struct {
    uart_cfg_t cfg;
    uint8_t ovs, sa;          /* 비트당 샘플 수, 다수결 첫 샘플 번호 (sa, sa+1, sa+2)          */
    bool overwrite;           /* 연습 1: true 면 RXNE 중에 온 새 문자가 RDR 를 덮어씀 (ORE 없음) */
    bool naive;               /* 연습 5: true 면 false start 뒤 재무장(prev=1) 을 하지 않음        */
    rx_state_t st;
    uint8_t prev, ph, smp, nbit, noisy, par;
    uint16_t shift;
    uint32_t t;
    uint8_t rdr, sr;
    uint32_t n_overrun;
    ev_t log[LOG_CAP];
    uint32_t nlog;
    /* 연습 4: IDLE line */
    bool idle_armed;
    uint32_t high_run, n_idle, t_idle;
} rx_t;

static void rx_init(rx_t *r, uart_cfg_t c, uint8_t ovs, uint8_t sa, bool overwrite) {
    memset(r, 0, sizeof *r);
    r->cfg = c; r->ovs = ovs; r->sa = sa; r->overwrite = overwrite; r->prev = 1;
}
static void rx_log(rx_t *r, uint16_t d, uint8_t f, uint8_t fs) {
    if (r->nlog < LOG_CAP) { r->log[r->nlog].data = d; r->log[r->nlog].flags = f; r->log[r->nlog].false_start = fs; r->nlog++; }
}
static uint8_t frame_flags(const uart_cfg_t *c, uint16_t data, uint8_t par, uint8_t stop, uint8_t noisy) {
    uint8_t f = noisy ? SR_NF : 0;
    if (c->parity != PAR_NONE && par != (uint8_t)(parity_of(data) ^ (c->parity == PAR_ODD))) f |= SR_PE;
    if (!stop) { f |= SR_FE; if (data == 0 && (c->parity == PAR_NONE || par == 0)) f |= SR_BRK; }
    return f;
}
/* 연습 1 의 핵심: overrun 정책 두 가지 */
static void rx_deliver(rx_t *r, uint8_t data, uint8_t flags) {
    if (r->sr & SR_RXNE) {
        r->n_overrun++;
        if (!r->overwrite) { r->sr |= SR_ORE; return; }   /* 본문: 옛 것 유지 + ORE            */
        r->rdr = data;                                     /* OVRDIS 식: 새 것으로 덮고 ORE 없음 */
        r->sr = (uint8_t)(SR_RXNE | flags);
        return;
    }
    r->rdr = data;
    r->sr |= (uint8_t)(SR_RXNE | flags);
}
static uint8_t uart_read(rx_t *r, uint8_t *d) { uint8_t s = r->sr; if (s & SR_RXNE) *d = r->rdr; r->sr = 0; return s; }

static uint32_t frame_bits(const uart_cfg_t *c) { return 1u + c->data_bits + (c->parity != PAR_NONE) + 1u; }

static void rx_tick(rx_t *r, int sample) {
    uint8_t s = sample ? 1 : 0;
    r->t++;
    /* 연습 4: 문자 완료 뒤부터 high 샘플을 센다. 0 이 오면 리셋. 1문자 시간(frame_bits × ovs) 이면 IDLE 1회 */
    if (s) r->high_run++; else r->high_run = 0;
    if (r->idle_armed && r->high_run >= frame_bits(&r->cfg) * r->ovs) { r->n_idle++; r->t_idle = r->t; r->idle_armed = false; }

    if (r->st == RX_IDLE) {
        if (r->prev == 1 && s == 0) { r->st = RX_START; r->ph = 1; r->smp = r->nbit = r->noisy = r->par = 0; r->shift = 0; }
        r->prev = s;
        return;
    }
    r->ph++;
    if (r->ph >= r->sa && r->ph <= r->sa + 2) r->smp |= (uint8_t)(s << (r->ph - r->sa));
    if (r->ph == r->sa + 2) {
        int a = r->smp & 1, b = (r->smp >> 1) & 1, c = (r->smp >> 2) & 1, v = a + b + c >= 2;
        r->noisy |= (uint8_t)!(a == b && b == c);
        r->smp = 0;
        if (r->st == RX_START && v) {
            r->st = RX_IDLE; rx_log(r, 0, 0, 1);
            if (!r->naive) { r->prev = 1; return; }      /* 본문 solution 과 같은 재무장 */
        }
        else if (r->st == RX_DATA) { r->shift |= (uint16_t)(v << r->nbit); r->nbit++; }
        else if (r->st == RX_PARITY) r->par = (uint8_t)v;
        else if (r->st == RX_STOP) {
            uint8_t f = frame_flags(&r->cfg, r->shift, r->par, (uint8_t)v, r->noisy);
            rx_log(r, r->shift, f, 0);
            rx_deliver(r, (uint8_t)r->shift, f);
            r->st = RX_IDLE;
            r->idle_armed = true; r->high_run = 0;     /* 이 샘플 다음부터 센다 */
        }
    }
    if (r->ph == r->ovs && r->st != RX_IDLE) {
        r->ph = 0;
        if (r->st == RX_START) r->st = RX_DATA;
        else if (r->st == RX_DATA && r->nbit == r->cfg.data_bits) r->st = r->cfg.parity ? RX_PARITY : RX_STOP;
        else if (r->st == RX_PARITY) r->st = RX_STOP;
    }
    r->prev = s;
}

/* ---------------- 송신 파형 (COMM-B01 모델 축약판) ---------------- */
#define LINE_CAP (1u << 16)
static uint8_t g_line[LINE_CAP];
static size_t g_n;
static void put_level(int lv, size_t n) { for (size_t i = 0; i < n && g_n < LINE_CAP; i++) g_line[g_n++] = (uint8_t)lv; }
static void put_char(uint16_t byte, const uart_cfg_t *c, unsigned ovs, unsigned gap_bits) {
    uint16_t d = (uint16_t)(byte & ((1u << c->data_bits) - 1u));
    put_level(0, ovs);
    for (int i = 0; i < c->data_bits; i++) put_level((d >> i) & 1u, ovs);
    if (c->parity != PAR_NONE) put_level(parity_of(d) ^ (c->parity == PAR_ODD), ovs);
    put_level(1, (size_t)ovs * (c->stop_bits + gap_bits));
}

/* ---------------- 연습 3: LIN 식 break 검출기 ---------------- */
/* low 가 연속 thr_bits 비트 시간 이상이면 그 순간 1회 보고. high 가 오면 재무장 */
typedef struct { uint32_t low_run, thr; bool fired; uint32_t n_break; } brk_det_t;
static void brk_init(brk_det_t *b, unsigned ovs, unsigned thr_bits) { memset(b, 0, sizeof *b); b->thr = ovs * thr_bits; }
static void brk_tick(brk_det_t *b, int s) {
    if (s) { b->low_run = 0; b->fired = false; return; }
    if (++b->low_run >= b->thr && !b->fired) { b->fired = true; b->n_break++; }
}

/* ---------------- 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t g_rng = 0xC0FFEE11u;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static const uart_cfg_t C8N1 = {8, PAR_NONE, 1}, C8E1 = {8, PAR_EVEN, 1};
static rx_t g_rx;

/* 본문 Part C 와 같은 ISR 지연 모델 (링 대신 바로 out[] 에) */
static size_t run_latency(const uint8_t *tx, size_t n, uint32_t lat, bool overwrite, uint8_t *out, uint32_t *ore_seen) {
    g_n = 0; put_level(1, 40);
    for (size_t i = 0; i < n; i++) put_char(tx[i], &C8N1, 16, 0);
    put_level(1, 2000);
    rx_init(&g_rx, C8N1, 16, 8, overwrite);
    bool pend = false; uint32_t due = 0; size_t k = 0; *ore_seen = 0;
    for (size_t i = 0; i < g_n; i++) {
        rx_tick(&g_rx, g_line[i]);
        if (!pend && (g_rx.sr & SR_RXNE)) { pend = true; due = g_rx.t + lat; }
        if (pend && g_rx.t >= due) {
            uint8_t d = 0, s = uart_read(&g_rx, &d);
            if (s & SR_ORE) (*ore_seen)++;
            if (s & SR_RXNE) out[k++] = d;
            pend = false;
        }
    }
    return k;
}

int main(void) {
    uint8_t tx[64], out[64];
    for (int i = 0; i < 64; i++) tx[i] = (uint8_t)xs32();

    /* 연습 1: 덮어쓰기 정책 — 지연 200 틱이면 홀수 번째만 남고, ORE 는 한 번도 안 보인다 */
    {
        uint32_t ore;
        size_t n = run_latency(tx, 64, 200, true, out, &ore);
        int ok = (n == 32 && ore == 0 && g_rx.n_overrun == 32);
        for (size_t i = 0; i < n && i < 32; i++) ok &= (out[i] == tx[2 * i + 1]);
        CHECK(ok);
        n = run_latency(tx, 64, 200, false, out, &ore);      /* 본문 정책과 대조: 짝수 번째 + ORE 32번 */
        ok = (n == 32 && ore == 32);
        for (size_t i = 0; i < n && i < 32; i++) ok &= (out[i] == tx[2 * i]);
        CHECK(ok);
        n = run_latency(tx, 64, 159, true, out, &ore);       /* 지연이 1문자 시간 안이면 정책과 무관 */
        CHECK(n == 64 && memcmp(out, tx, 64) == 0 && g_rx.n_overrun == 0);
        printf("ex1: latency 200 -> overwrite keeps odd indices (no ORE), default keeps even indices (ORE x32)\n");
    }

    /* 연습 2: 8x 오버샘플링 (샘플 4/5/6) — 전수 왕복 + 글리치 경계가 절반으로 */
    {
        const uart_cfg_t *cs[2] = {&C8N1, &C8E1};
        int bad = 0;
        for (int k = 0; k < 2; k++) {
            g_n = 0; put_level(1, 9);
            for (int b = 0; b < 256; b++) put_char((uint16_t)b, cs[k], 8, (unsigned)(b % 3));
            rx_init(&g_rx, *cs[k], 8, 4, false);
            for (size_t i = 0; i < g_n; i++) rx_tick(&g_rx, g_line[i]);
            if (g_rx.nlog != 256) { bad += 256; continue; }
            for (int b = 0; b < 256; b++) bad += !(g_rx.log[b].data == b && g_rx.log[b].flags == 0 && !g_rx.log[b].false_start);
        }
        CHECK(bad == 0);
        int ok = 1;
        printf("ex2: OVER8 glitch L:");
        for (int L = 1; L <= 8; L++) {
            g_n = 0; put_level(1, 16); put_level(0, (size_t)L); put_level(1, 8 * 14);
            rx_init(&g_rx, C8N1, 8, 4, false);
            for (size_t i = 0; i < g_n; i++) rx_tick(&g_rx, g_line[i]);
            const ev_t *e = &g_rx.log[0];
            bool fs = (g_rx.nlog == 1 && e->false_start);
            bool nf = (g_rx.nlog == 1 && !e->false_start && e->data == 0xFF && e->flags == SR_NF);
            bool cl = (g_rx.nlog == 1 && !e->false_start && e->data == 0xFF && e->flags == 0);
            printf(" %d=%s", L, fs ? "rej" : nf ? "FF+NF" : cl ? "FF" : "?");
            ok &= (L <= 4) ? fs : (L == 5) ? nf : cl;
        }
        printf("\n");
        CHECK(ok);
    }

    /* 연습 3: break 검출 — LIN break(13비트 low)는 1번, 0x00(8N1 = 9비트 low)·0x00(8E1 = 10비트 low)는 0번 */
    {
        brk_det_t b11, b10;
        g_n = 0; put_level(1, 32);
        put_char(0x00, &C8N1, 16, 2);                 /* start + 8 data = 9 비트 low  */
        put_char(0x00, &C8E1, 16, 2);                 /* start + 8 data + parity 0 = 10 비트 low */
        put_level(0, 13 * 16); put_level(1, 16);      /* LIN break 13 비트 + delimiter 1 비트 */
        put_char(0x55, &C8N1, 16, 2);                 /* LIN sync 0x55 */
        brk_init(&b11, 16, 11); brk_init(&b10, 16, 10);
        for (size_t i = 0; i < g_n; i++) { brk_tick(&b11, g_line[i]); brk_tick(&b10, g_line[i]); }
        CHECK(b11.n_break == 1);                      /* 11비트 문턱: break 만 */
        CHECK(b10.n_break == 2);                      /* 10비트 문턱: 8E1 의 0x00 까지 break 로 오인 */
        g_n = 0; put_level(1, 32); put_level(0, 100 * 16); put_level(1, 32);
        brk_init(&b11, 16, 11);
        for (size_t i = 0; i < g_n; i++) brk_tick(&b11, g_line[i]);
        CHECK(b11.n_break == 1);                      /* 아주 긴 low 도 한 번만 */
    }

    /* 연습 4: IDLE line — 문자 완료(stop 샘플 10) 뒤 high 가 1문자 시간(8N1: 160 샘플) 이어지면 1회 */
    {
        const char *msg = "HELLO";
        g_n = 0; put_level(1, 20);
        for (int i = 0; i < 5; i++) put_char((uint8_t)msg[i], &C8N1, 16, 0);
        put_level(1, 3 * 160);
        rx_init(&g_rx, C8N1, 16, 8, false);
        for (size_t i = 0; i < g_n; i++) rx_tick(&g_rx, g_line[i]);
        /* 마지막 문자 'O' 의 완료 시각 = 20 + 4*160 + 1 + 9*16 + 9 = 814 → IDLE = 814 + 160 = 974 */
        CHECK(g_rx.nlog == 5 && g_rx.n_idle == 1 && g_rx.t_idle == 974);
        /* gap 9 비트: 6 + 144 = 150 < 160 → 사이에 IDLE 없음 / gap 10 비트: 166 ≥ 160 → 문자마다 IDLE */
        for (unsigned gap = 9; gap <= 10; gap++) {
            g_n = 0; put_level(1, 20);
            for (int i = 0; i < 5; i++) put_char(0xFF, &C8N1, 16, gap);   /* 0xFF: high 가 가장 긴 문자 */
            put_level(1, 3 * 160);
            rx_init(&g_rx, C8N1, 16, 8, false);
            for (size_t i = 0; i < g_n; i++) rx_tick(&g_rx, g_line[i]);
            CHECK(g_rx.nlog == 5 && g_rx.n_idle == (gap == 9 ? 1u : 5u));
        }
    }

    /* 연습 5: 진짜 edge 보다 k 샘플 앞의 1샘플 글리치. 재무장 없으면 k=9 에서 'A' 가 조용히 0xD0 이 된다 */
    {
        int bad_naive = 0, bad_fixed = 0, k_bad = -1;
        uint16_t got9 = 0; uint8_t f9 = 0xFF;
        for (int k = 1; k <= 18; k++) {
            for (int nv = 0; nv < 2; nv++) {
                g_n = 0; put_level(1, 40);
                size_t e = g_n; put_char(0x41, &C8N1, 16, 4); put_char(0x42, &C8N1, 16, 4);
                g_line[e - (size_t)k] = 0;
                rx_init(&g_rx, C8N1, 16, 8, false); g_rx.naive = (nv == 1);
                for (size_t i = 0; i < g_n; i++) rx_tick(&g_rx, g_line[i]);
                uint16_t fr[4]; uint8_t ff[4]; int m = 0;
                for (uint32_t i = 0; i < g_rx.nlog && m < 4; i++)
                    if (!g_rx.log[i].false_start) { fr[m] = g_rx.log[i].data; ff[m] = g_rx.log[i].flags; m++; }
                bool good = (m == 2 && fr[0] == 0x41 && fr[1] == 0x42 && (ff[0] & ~SR_NF) == 0 && ff[1] == 0);
                if (nv) { bad_naive += !good; if (!good) { k_bad = k; got9 = m ? fr[0] : 0; f9 = m ? ff[0] : 0xFF; } }
                else bad_fixed += !good;
            }
        }
        printf("ex5: naive bad k=%d ('A' -> 0x%02X flags 0x%02X), fixed bad=%d\n", k_bad, got9, f9, bad_fixed);
        CHECK(bad_naive == 1 && k_bad == 9 && got9 == 0xD0 && f9 == 0);
        CHECK(bad_fixed == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
