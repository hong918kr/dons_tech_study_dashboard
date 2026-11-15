/* COMM-A02 demo_window.c — 처리량 곡선과 seq 공간 실험 (긴 실행, solution.c 의 모델을 그대로 복사)
 *
 * 1. goodput vs 창 크기 W (1..64) × 손실률 p (0, 1, 5, 10, 20 %), RTT 20 틱, RTO 30: GBN vs SR + 이론값
 * 2. RTT 4 / 80 틱에서 같은 곡선 (p = 0, 5, 20 %)
 * 3. 재정렬(jitter) 링크에서 seq 비트 k 별 실패 실행 수 — FIFO 공식 vs seq_bits_needed
 * 4. 텔레메트리 무선 (57600 baud, 72 B 프레임, RTT 200 ms, 손실 5 %): W 별 B/s, RAM
 * goodput = 앱이 받은 메시지 / 링크가 1 틱에 보낼 수 있는 프레임 (1 = 링크 100 %)
 * 빌드: cc -std=c11 -Wall -Wextra -O2 demo_window.c -o demo_window -lm && ./demo_window
 */
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static uint32_t msg_payload(uint32_t i) { return i * 2654435761u + 0x5A17u; }   /* 메시지 i 의 내용 (앱이 검사) */
static bool g_trace;                                                          /* true 면 이벤트를 한 줄씩 찍는다 */

/* ---------------- 링크 시뮬레이터 (한 방향 = channel, 도착 시각 최소 힙) ---------------- */
#define CHAN_CAP 512
enum { PKT_DATA = 1, PKT_ACK = 2 };
typedef struct { uint8_t type; uint16_t seq; uint32_t payload, due, order; } pkt;
typedef struct { uint32_t idx; uint8_t drop; uint32_t extra; } link_script;   /* idx = 이 채널의 몇 번째 패킷 (0부터) */
typedef struct { uint32_t delay, jitter, loss_ppm; } chan_cfg;
typedef struct {
    chan_cfg cfg; uint32_t rng;
    pkt h[CHAN_CAP]; int n; uint32_t order;
    const link_script *script; size_t nscript;
    uint32_t sent, lost, delivered, overflow;
} channel;

static void chan_init(channel *c, const chan_cfg *cfg, uint32_t seed) {
    memset(c, 0, sizeof *c); c->cfg = *cfg; c->rng = seed ? seed : 1u;
}
static bool pkt_before(const pkt *a, const pkt *b) { return a->due < b->due || (a->due == b->due && a->order < b->order); }
static void chan_send(channel *c, uint32_t now, uint8_t type, uint16_t seq, uint32_t payload) {
    uint32_t idx = c->sent++, extra = 0;
    for (size_t i = 0; i < c->nscript; i++)
        if (c->script[i].idx == idx) {
            if (c->script[i].drop) { c->lost++; return; }
            extra = c->script[i].extra;
        }
    if (c->cfg.loss_ppm && xs32(&c->rng) % 1000000u < c->cfg.loss_ppm) { c->lost++; return; }
    uint32_t d = c->cfg.delay + extra;
    if (c->cfg.jitter) d += xs32(&c->rng) % (c->cfg.jitter + 1u);
    if (d == 0) d = 1;
    if (c->n >= CHAN_CAP) { c->overflow++; return; }
    pkt p = { type, seq, payload, now + d, c->order++ };
    int i = c->n++;
    while (i > 0 && pkt_before(&p, &c->h[(i - 1) / 2])) { c->h[i] = c->h[(i - 1) / 2]; i = (i - 1) / 2; }
    c->h[i] = p;
}
static bool chan_recv(channel *c, uint32_t now, pkt *out) {
    if (c->n == 0 || c->h[0].due > now) return false;
    *out = c->h[0];
    pkt last = c->h[--c->n];
    int i = 0;
    for (;;) {
        int l = 2 * i + 1, r = l + 1, m = i;
        const pkt *best = &last;
        if (l < c->n && pkt_before(&c->h[l], best)) { m = l; best = &c->h[l]; }
        if (r < c->n && pkt_before(&c->h[r], best)) { m = r; }
        if (m == i) break;
        c->h[i] = c->h[m]; i = m;
    }
    if (c->n > 0) c->h[i] = last;
    c->delivered++;
    return true;
}

/* ---------------- 프로토콜 설정 / 상태 ---------------- */
#define WMAX 64                     /* 창 크기 상한 = 송신 버퍼 / SR 수신 버퍼 칸 수 */
typedef enum { PROTO_GBN, PROTO_SR } proto_t;
typedef struct {
    proto_t proto;
    uint8_t  k;                     /* seq 비트 수 1..15 → seq 공간 2^k */
    uint16_t W;                     /* 창 크기 1..min(WMAX, 2^k) */
    uint32_t rto;                   /* 재전송 타임아웃 (틱) */
    uint8_t  max_tx;                /* 한 프레임 최대 전송 횟수 → 넘으면 FAIL */
} win_cfg;

typedef struct {
    win_cfg cfg; uint32_t mask; channel *link;
    uint32_t N;                     /* 보낼 메시지 수 (앱) */
    uint32_t base, next, hi;        /* 절대 번호: base = 가장 오래된 미확인, next = 다음에 보낼 것 (GBN 은 되감음), hi = 보낸 최대 + 1 */
    uint32_t buf[WMAX];             /* 미확인 메시지 사본 (재전송용) — idx % WMAX */
    uint8_t  txcount[WMAX];
    bool     acked[WMAX], need_retx[WMAX];   /* SR */
    uint32_t deadline[WMAX];                 /* SR: 프레임별 타이머 */
    bool     timer_on; uint32_t timer_deadline;  /* GBN: 타이머 하나 */
    bool     failed; uint32_t fail_idx, t_fail;
    uint32_t frames, retx, timeouts, acks_new, acks_dup, acks_ignored;
    uint32_t sent_acked;            /* 이미 확인된(< base) 프레임을 보낸 횟수 — 항상 0 이어야 한다 */
} win_tx;

typedef struct {
    win_cfg cfg; uint32_t mask; channel *link;
    uint32_t expected;              /* GBN: 다음 기대 메시지 번호, SR: rcv_base (절대 번호) */
    uint32_t buf[WMAX]; bool have[WMAX]; uint32_t buffered, max_buffered;   /* SR 수신 버퍼 */
    uint32_t delivered, wrong;      /* 앱: 받은 개수, 내용이 순서와 안 맞은 개수 (= 잘못 받아들인 옛 메시지) */
    uint32_t discarded, dup, old, ignored, acks_sent;
} win_rx;

static bool cfg_valid(const win_cfg *c) {
    return c->k >= 1 && c->k <= 15 && c->W >= 1 && c->W <= WMAX && c->W <= (1u << c->k) && c->rto >= 1 && c->max_tx >= 1;
}

/* ---------------- 공통 동작 (주어짐) ---------------- */
static void rx_deliver(win_rx *r, uint32_t now, uint32_t payload) {
    if (payload != msg_payload(r->delivered)) r->wrong++;
    if (g_trace) printf("  t=%4u  RX deliver    #%u%s\n", now, r->delivered, payload != msg_payload(r->delivered) ? "  <-- 내용 불일치!" : "");
    r->delivered++;
}
static void rx_send_ack(win_rx *r, uint32_t now, uint16_t seq) {
    r->acks_sent++;
    chan_send(r->link, now, PKT_ACK, seq, 0);
}
static void tx_frame(win_tx *t, uint32_t now, uint32_t idx, bool retx) {
    uint32_t slot = idx % WMAX;
    if (!retx) { t->buf[slot] = msg_payload(idx); t->txcount[slot] = 0; t->acked[slot] = false; t->need_retx[slot] = false; }
    t->txcount[slot]++;
    t->deadline[slot] = now + t->cfg.rto;
    t->frames++;
    if (retx) t->retx++;
    if (idx < t->base) t->sent_acked++;
    if (g_trace) printf("  t=%4u  TX %s  #%u seq=%u\n", now, retx ? "RETX" : "    ", idx, idx & t->mask);
    chan_send(t->link, now, PKT_DATA, (uint16_t)(idx & t->mask), t->buf[slot]);
}
static void tx_fail(win_tx *t, uint32_t now, uint32_t idx) {
    t->failed = true; t->fail_idx = idx; t->t_fail = now;
    if (g_trace) printf("  t=%4u  FAIL          #%u (tx %u 번)\n", now, idx, t->txcount[idx % WMAX]);
}

/* ======================= Go-Back-N ======================= */
/* A1: seq == expected 면 앱에 넘기고 expected++. 받든 버리든 ACK(expected) — 누적 ACK */
static void gbn_rx_on_data(win_rx *r, uint32_t now, uint16_t seq, uint32_t payload) {
    if (seq == (r->expected & r->mask)) {
        rx_deliver(r, now, payload);
        r->expected++;
    } else {
        r->discarded++;                                     /* 순서가 아니면 버린다 (버퍼 없음) */
    }
    rx_send_ack(r, now, (uint16_t)(r->expected & r->mask)); /* 지금까지 순서대로 받은 것의 다음 seq */
}

/* A2: d = (ack − base) mod 2^k. 1 ≤ d ≤ hi − base 면 base += d (되감은 뒤에도 hi 기준!), 아니면 무시 */
static void gbn_tx_on_ack(win_tx *t, uint32_t now, uint16_t ack) {
    uint32_t d = ((uint32_t)ack - t->base) & t->mask;
    if (d == 0 || d > t->hi - t->base) { t->acks_ignored++; return; }   /* 중복 ACK 또는 범위 밖 */
    t->base += d;
    t->acks_new++;
    if (t->next < t->base) t->next = t->base;              /* 되감기 전에 보낸 프레임까지 확인됨 → 그 앞은 다시 안 보냄 */
    if (t->base == t->hi) t->timer_on = false;             /* 미확인 없음 → 타이머 정지 */
    else { t->timer_on = true; t->timer_deadline = now + t->cfg.rto; }   /* base 전진 → 재시작 */
}

/* A3: 타이머 만료 처리. base 를 max_tx 번 보냈으면 FAIL, 아니면 next = base 로 되감고 타이머 재시작 */
static void gbn_tx_timeout(win_tx *t, uint32_t now) {
    t->timeouts++;
    if (t->txcount[t->base % WMAX] >= t->cfg.max_tx) { tx_fail(t, now, t->base); return; }
    if (g_trace) printf("  t=%4u  TIMEOUT       go back to #%u (%u 개 재전송 예정)\n", now, t->base, t->hi - t->base);
    t->next = t->base;                                      /* Go back N */
    t->timer_deadline = now + t->cfg.rto;
}

static void gbn_tx_poll(win_tx *t, uint32_t now) {
    if (t->timer_on && (int32_t)(now - t->timer_deadline) >= 0) {
        gbn_tx_timeout(t, now);
        if (t->failed) return;
    }
    bool sent = false;
    if (t->next < t->hi) { tx_frame(t, now, t->next, true); t->next++; sent = true; }        /* 되감은 구간 재전송 */
    else if (t->next < t->N && t->next < t->base + t->cfg.W) {
        tx_frame(t, now, t->next, false); t->next++; t->hi = t->next; sent = true;            /* 새 프레임 */
    }
    if (sent && !t->timer_on) { t->timer_on = true; t->timer_deadline = now + t->cfg.rto; }
}

/* ======================= Selective Repeat ======================= */
/* A4: d = (seq − rcv_base) mod 2^k. d < W → 창 안: 버퍼(중복이면 dup), ACK(seq), 순서대로 넘김.
 *     d ≥ 2^k − W → 바로 앞 창(옛 것): ACK(seq) 만. 그 외 → 무시 (ACK 없음) */
static void sr_rx_on_data(win_rx *r, uint32_t now, uint16_t seq, uint32_t payload) {
    uint32_t d = ((uint32_t)seq - r->expected) & r->mask;
    if (d < r->cfg.W) {
        uint32_t slot = (r->expected + d) % WMAX;
        if (!r->have[slot]) {
            r->have[slot] = true; r->buf[slot] = payload;
            if (++r->buffered > r->max_buffered) r->max_buffered = r->buffered;
        } else {
            r->dup++;
        }
        rx_send_ack(r, now, seq);
        while (r->have[r->expected % WMAX]) {               /* rcv_base 부터 연속된 것을 앱으로 */
            uint32_t s = r->expected % WMAX;
            r->have[s] = false; r->buffered--;
            rx_deliver(r, now, r->buf[s]);
            r->expected++;
        }
    } else if (d >= r->mask + 1u - r->cfg.W) {
        r->old++;                                           /* 이미 넘긴 것 — 내 ACK 가 사라졌다는 뜻 */
        rx_send_ack(r, now, seq);
    } else {
        r->ignored++;
    }
}

/* A5: d = (seq − base) mod 2^k, d < hi − base 면 그 프레임 확인 (이미 확인이면 dup). base 를 확인된 것 너머로 민다 */
static void sr_tx_on_ack(win_tx *t, uint32_t now, uint16_t seq) {
    (void)now;
    uint32_t d = ((uint32_t)seq - t->base) & t->mask;
    if (d >= t->hi - t->base) { t->acks_ignored++; return; }
    uint32_t slot = (t->base + d) % WMAX;
    if (t->acked[slot]) { t->acks_dup++; return; }
    t->acked[slot] = true; t->need_retx[slot] = false;
    t->acks_new++;
    while (t->base < t->hi && t->acked[t->base % WMAX]) { t->acked[t->base % WMAX] = false; t->base++; }
}

/* A6: [base, hi) 의 미확인·재전송 대기 아닌 프레임 중 타이머 만료된 것: max_tx 번 보냈으면 FAIL, 아니면 need_retx */
static void sr_tx_timeouts(win_tx *t, uint32_t now) {
    for (uint32_t i = t->base; i < t->hi; i++) {
        uint32_t s = i % WMAX;
        if (t->acked[s] || t->need_retx[s] || (int32_t)(now - t->deadline[s]) < 0) continue;
        t->timeouts++;
        if (t->txcount[s] >= t->cfg.max_tx) { tx_fail(t, now, i); return; }
        if (g_trace) printf("  t=%4u  TIMEOUT       #%u 만 재전송 예정\n", now, i);
        t->need_retx[s] = true;
    }
}

static void sr_tx_poll(win_tx *t, uint32_t now) {
    sr_tx_timeouts(t, now);
    if (t->failed) return;
    for (uint32_t i = t->base; i < t->hi; i++)              /* 재전송이 새 프레임보다 먼저, 오래된 것부터 */
        if (t->need_retx[i % WMAX]) { t->need_retx[i % WMAX] = false; tx_frame(t, now, i, true); return; }
    if (t->hi < t->N && t->hi < t->base + t->cfg.W) { tx_frame(t, now, t->hi, false); t->hi++; t->next = t->hi; }
}

/* ======================= 시뮬레이션 실행 ======================= */
typedef struct {
    bool ok;                        /* 끝까지 확인 + 앱이 N 개를 순서·내용 그대로 받음 */
    bool finished, failed, stalled;
    uint32_t ticks, delivered, wrong, frames, retx, timeouts;
    uint32_t data_lost, ack_lost, rx_discarded, rx_dup, rx_old, rx_ignored, max_buffered, overflow;
    uint32_t acks_ignored, fail_idx, sent_acked;
} run_result;

typedef struct { win_cfg cfg; channel fwd, rev; win_tx tx; win_rx rx; } world;
static world g_w;

static run_result run_sim(const win_cfg *c, const chan_cfg *fc, const chan_cfg *rc, uint32_t N, uint32_t seed,
                          const link_script *fs, size_t nfs, const link_script *rs, size_t nrs, uint32_t tick_cap) {
    run_result R; memset(&R, 0, sizeof R);
    if (!cfg_valid(c)) { R.stalled = true; return R; }
    world *w = &g_w;
    memset(w, 0, sizeof *w);
    w->cfg = *c;
    chan_init(&w->fwd, fc, seed * 2u + 1u); chan_init(&w->rev, rc, seed * 2u + 7u);
    w->fwd.script = fs; w->fwd.nscript = nfs; w->rev.script = rs; w->rev.nscript = nrs;
    w->tx.cfg = *c; w->tx.mask = (1u << c->k) - 1u; w->tx.link = &w->fwd; w->tx.N = N;
    w->rx.cfg = *c; w->rx.mask = w->tx.mask; w->rx.link = &w->rev;
    uint32_t t = 0;
    for (; t < tick_cap; t++) {
        pkt p;
        while (chan_recv(&w->fwd, t, &p)) {
            if (c->proto == PROTO_GBN) gbn_rx_on_data(&w->rx, t, p.seq, p.payload);
            else sr_rx_on_data(&w->rx, t, p.seq, p.payload);
        }
        while (chan_recv(&w->rev, t, &p)) {
            if (c->proto == PROTO_GBN) gbn_tx_on_ack(&w->tx, t, p.seq);
            else sr_tx_on_ack(&w->tx, t, p.seq);
        }
        if (w->tx.base >= N) { R.finished = true; break; }
        if (c->proto == PROTO_GBN) gbn_tx_poll(&w->tx, t); else sr_tx_poll(&w->tx, t);
        if (w->tx.failed) { R.failed = true; break; }
    }
    if (!R.finished && !R.failed) R.stalled = true;
    R.ticks = t; R.delivered = w->rx.delivered; R.wrong = w->rx.wrong;
    R.frames = w->tx.frames; R.retx = w->tx.retx; R.timeouts = w->tx.timeouts;
    R.data_lost = w->fwd.lost; R.ack_lost = w->rev.lost; R.overflow = w->fwd.overflow + w->rev.overflow;
    R.rx_discarded = w->rx.discarded; R.rx_dup = w->rx.dup; R.rx_old = w->rx.old; R.rx_ignored = w->rx.ignored;
    R.max_buffered = w->rx.max_buffered; R.acks_ignored = w->tx.acks_ignored; R.fail_idx = w->tx.fail_idx; R.sent_acked = w->tx.sent_acked;
    R.ok = R.finished && R.delivered == N && R.wrong == 0;
    return R;
}

/* ======================= Part B: seq 공간 조건 ======================= */
/* B1: 정확성에 충분한 최소 k (2^k ≥ need). Rmax = 왕복 최대 지연 (틱 = 프레임 시간), Jf = DATA 지연 편차.
 *   FIFO (jitter 둘 다 0): GBN need = W + 1, SR need = 2W  (교과서 조건, 이 레슨의 증명)
 *   재정렬 있음: GBN need = max(W + 1 + Jf, W + Rmax + 1), SR need = max(2W + Jf, W + Rmax)  (속도 상한으로 증명한 충분조건) */
static uint8_t seq_bits_needed(proto_t p, uint32_t W, const chan_cfg *f, const chan_cfg *r) {
    uint32_t need;
    if (f->jitter == 0 && r->jitter == 0) {
        need = (p == PROTO_GBN) ? W + 1u : 2u * W;
    } else {
        uint32_t Rmax = f->delay + f->jitter + r->delay + r->jitter, Jf = f->jitter;
        uint32_t a = (p == PROTO_GBN) ? W + 1u + Jf : 2u * W + Jf;
        uint32_t b = (p == PROTO_GBN) ? W + Rmax + 1u : W + Rmax;
        need = a > b ? a : b;
    }
    uint8_t k = 1;
    while ((1u << k) < need) k++;
    return k;
}

/* ======================= Part C: 텔레메트리 무선 링크 설계 ======================= */
typedef struct {
    uint32_t baud, bits_per_byte;   /* 8N1 = 10 비트/바이트 */
    uint32_t frame_bytes, payload_bytes;
    uint32_t rtt_us;                /* 프레임 송신 시간을 뺀 왕복 지연 */
    uint32_t loss_ppm;
} radio_link;
typedef struct {
    uint32_t frame_us;              /* 1 틱 = 프레임 하나 송신 시간 */
    uint32_t R;                     /* 왕복 지연 (틱, 올림) = 대역폭-지연 곱 (프레임) */
    uint32_t rto;                   /* R + ⌈R/2⌉ */
    uint32_t W;                     /* SR 창: R + rto (손실 하나 복구하는 동안에도 계속 보내려면) */
    uint32_t k;                     /* SR: 2^k ≥ 2W */
    uint32_t ram_tx, ram_rx_sr, ram_rx_gbn;   /* 바이트: 송신 사본 W·payload, SR 수신 버퍼 W·payload, GBN 수신 0 */
    uint32_t line_Bps;              /* 손실 없을 때 payload 바이트/초 상한 */
} link_plan_t;

/* C1: 위 규칙대로 계산 (올림 나눗셈) */
static link_plan_t link_plan(const radio_link *L) {
    link_plan_t P; memset(&P, 0, sizeof P);
    uint64_t bits = (uint64_t)L->frame_bytes * L->bits_per_byte;
    P.frame_us = (uint32_t)((bits * 1000000u + L->baud - 1u) / L->baud);
    P.R = (L->rtt_us + P.frame_us - 1u) / P.frame_us;
    P.rto = P.R + (P.R + 1u) / 2u;
    P.W = P.R + P.rto;
    P.k = 1;
    while ((1u << P.k) < 2u * P.W) P.k++;
    P.ram_tx = P.W * L->payload_bytes;
    P.ram_rx_sr = P.W * L->payload_bytes;
    P.ram_rx_gbn = 0;
    P.line_Bps = (uint32_t)((uint64_t)L->payload_bytes * 1000000u / P.frame_us);
    return P;
}

/* ======================= 데모 ======================= */
#define SEEDS 4u
static double gp(const run_result *r, uint32_t N) { return r->ok ? (double)N / (double)r->ticks : 0.0; }

/* SR 이론 (1) 낙관적 상한 min(q, W/R)  (2) 비관적 라운드 모델: W 개를 한꺼번에 보내고 전부 확인될 때까지 다음 W 개를
 *   안 보낸다고 보면 라운드 시간 = max(R, W) + RTO·E[max_{i≤W} G_i], G_i = 실패한 시도 수 ~ 기하분포(실패확률 1−q).
 *   재전송이 링크 칸을 먹는 것은 안 세므로 q 로 자른다: min(q, W / 라운드 시간) */
static double e_max_geom(double f, uint32_t W) {
    double s = 0;
    for (int m = 1; m < 400; m++) s += 1.0 - pow(1.0 - pow(f, m), W);
    return s;
}
static double sr_upper(double p, uint32_t W, uint32_t R) { double q = (1 - p) * (1 - p); double w = (double)W / R; return q < w ? q : w; }
static double sr_round(double p, uint32_t W, uint32_t R, uint32_t rto) {
    double q = (1 - p) * (1 - p);
    double T = (W > R ? W : R) + rto * e_max_geom(1 - q, W);
    double v = W / T;
    return v < q ? v : q;
}
/* GBN 이론: 프레임 x 의 첫 손실 = (R − 1 + RTO) 틱 밀림 (x−1 의 ACK 로 base 가 x 가 될 때 타이머가 재시작되므로),
 *   되감아 다시 보낸 x 가 또 손실 = RTO 밀림 (되감는 틱에 타이머 재시작 + x 송신).
 *   프레임당 시간 = max(1, R/W) + p·(R − 1 + RTO) + p²/(1−p)·RTO.  W = 1 이면 stop-and-wait: R + (1/q − 1)·RTO (COMM-M14) */
static double gbn_theory(double p, uint32_t W, uint32_t R, uint32_t rto) {
    double q = (1 - p) * (1 - p);
    if (W == 1) return 1.0 / (R + (1.0 / q - 1.0) * rto);
    double base = (double)R / W; if (base < 1) base = 1;
    return 1.0 / (base + p * (R - 1.0 + rto) + p * p / (1 - p) * rto);
}

static void curve(uint32_t R, uint32_t rto, const double *ps, int nps, uint32_t N) {
    static const uint16_t Ws[] = { 1, 2, 4, 8, 12, 16, 20, 24, 32, 48, 64 };
    for (int ip = 0; ip < nps; ip++) {
        double p = ps[ip];
        printf("\n RTT %u, RTO %u, loss %.0f %% each way (N=%u x %u seeds, goodput in %% of link)\n", R, rto, p * 100, N, SEEDS);
        printf("   W | GBN meas  theory | SR meas  upper  round | GBN frames/msg  SR frames/msg\n");
        for (size_t i = 0; i < sizeof Ws / sizeof Ws[0]; i++) {
            uint16_t W = Ws[i];
            chan_cfg L = { R / 2u, 0, (uint32_t)(p * 1e6 + 0.5) };
            win_cfg cg = { PROTO_GBN, 8, W, rto, 100 }, cs = { PROTO_SR, 8, W, rto, 100 };
            double gg = 0, ss = 0, gf = 0, sf = 0; bool ok = true;
            for (uint32_t seed = 1; seed <= SEEDS; seed++) {                /* 시드 4 개 평균 (표본 오차 줄이기) */
                run_result g = run_sim(&cg, &L, &L, N, seed, NULL, 0, NULL, 0, 50000000);
                run_result s = run_sim(&cs, &L, &L, N, seed, NULL, 0, NULL, 0, 50000000);
                gg += gp(&g, N); ss += gp(&s, N); gf += (double)g.frames / N; sf += (double)s.frames / N;
                ok = ok && g.ok && s.ok;
            }
            printf("  %2u | %6.1f  %6.1f | %6.1f  %5.1f  %5.1f | %6.2f  %6.2f%s\n", W,
                   100 * gg / SEEDS, 100 * gbn_theory(p, W, R, rto),
                   100 * ss / SEEDS, 100 * sr_upper(p, W, R), 100 * sr_round(p, W, R, rto),
                   gf / SEEDS, sf / SEEDS, ok ? "" : "  (FAIL!)");
        }
    }
}

int main(void) {
    const double ps[] = { 0.0, 0.01, 0.05, 0.10, 0.20 };
    printf("=== 1. goodput vs W, RTT 20 ===");
    curve(20, 30, ps, 5, 20000);
    const double ps2[] = { 0.0, 0.05, 0.20 };
    printf("\n=== 2a. RTT 4 (short link) ===");
    curve(4, 6, ps2, 3, 20000);
    printf("\n=== 2b. RTT 80 (long link) ===");
    curve(80, 120, ps2, 3, 20000);

    printf("\n=== 3. seq space under reordering: failed runs out of 20 seeds (N=1500, loss 10 %%) ===\n");
    static const uint32_t Js[] = { 0, 2, 6, 12 };
    for (int p = 0; p < 2; p++) {
        uint16_t W = 8;
        for (int ij = 0; ij < 4; ij++) {
            chan_cfg L = { 5, Js[ij], 100000 };
            uint8_t kn = seq_bits_needed((proto_t)p, W, &L, &L);
            for (int rt = 0; rt < 2; rt++) {
                uint32_t rto = rt ? (10u + 2u * Js[ij] + 2u) : 8u;      /* 짧은 RTO (쓸데없는 재전송) / RTO > RTTmax */
                printf("  %s W=8 delay 5..%-2u RTO %2u (needed k=%u): ", p ? "SR " : "GBN", 5 + Js[ij], rto, kn);
                for (uint8_t k = 3; k <= 7; k++) {
                    uint32_t fails = 0;
                    for (uint32_t seed = 1; seed <= 20; seed++) {
                        win_cfg c = { (proto_t)p, k, W, rto, 200 };
                        if (!cfg_valid(&c)) { fails = 99; break; }
                        run_result r = run_sim(&c, &L, &L, 1500, seed, NULL, 0, NULL, 0, 50000000);
                        if (!r.ok) fails++;
                    }
                    printf(" k=%u:%-2u", k, fails);
                }
                printf("\n");
            }
        }
    }

    printf("\n=== 4. telemetry radio 57600 baud, 72 B frame (64 B payload), RTT 200 ms, loss 5 %% ===\n");
    radio_link RL = { 57600, 10, 72, 64, 200000, 50000 };
    link_plan_t P = link_plan(&RL);
    printf("  plan: frame %u us, R=%u, RTO=%u, W=%u, k=%u, line %u B/s, q*line %.0f B/s\n",
           P.frame_us, P.R, P.rto, P.W, P.k, P.line_Bps, 0.95 * 0.95 * P.line_Bps);
    static const uint16_t Wt[] = { 4, 8, 16, 24, 32, 40, 48, 64 };
    printf("   W |  GBN B/s   SR B/s | SR RAM (tx+rx)\n");
    for (size_t i = 0; i < sizeof Wt / sizeof Wt[0]; i++) {
        chan_cfg L = { P.R / 2u, 0, RL.loss_ppm };
        double b[2];
        for (int p = 0; p < 2; p++) {
            win_cfg c = { (proto_t)p, 8, Wt[i], P.rto, 100 };
            double acc = 0;
            for (uint32_t seed = 1; seed <= 3; seed++) {
                run_result r = run_sim(&c, &L, &L, 10000, seed, NULL, 0, NULL, 0, 50000000);
                acc += 10000.0 * RL.payload_bytes / (r.ticks * (P.frame_us / 1e6));
            }
            b[p] = acc / 3;
        }
        printf("  %2u | %7.0f  %7.0f | %u B\n", Wt[i], b[0], b[1], 2u * Wt[i] * RL.payload_bytes);
    }
    return 0;
}
