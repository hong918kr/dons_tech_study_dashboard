/* COMM-A02 추가 연습 — 한 번에 검증 (solution.c 의 모델 + 연습 답)
 *
 * 연습 1: GBN 타이머를 "base 프레임을 보낸 시각 + RTO" 로 (PROTO_GBN_ST) — 손실 비용 R−1+RTO → RTO
 * 연습 2: SR + 누적 ACK + SACK 비트맵 (PROTO_SR_SACK) — ACK 손실에 강해지고 작은 창에서도 GBN 이상
 * 연습 3: 재정렬 링크에서 RTO > RTTmax 면 SR 은 2^k ≥ 2W 로 충분 — 짧은 RTO 와 비교
 * 연습 4: 수신 RAM 1 KB 로 텔레메트리 링크 설계 — 64 B × 16 칸 vs 32 B × 32 칸 (비트 오류율로 프레임 손실 계산) → 큰 프레임 승
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
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

/* ======================= 연습 1: GBN — base 프레임 송신 시각 기준 타이머 ======================= */
/* deadline[] 은 tx_frame 이 프레임마다 기록해 둔다. base 가 바뀌어도 타이머를 다시 시작하지 않는다.
 * next == base 면 base 를 이번 틱에 (다시) 보낼 참이므로 타임아웃을 보지 않는다. */
static void gbn_st_poll(win_tx *t, uint32_t now) {
    if (t->base < t->hi && t->next > t->base && (int32_t)(now - t->deadline[t->base % WMAX]) >= 0) {
        gbn_tx_timeout(t, now);
        if (t->failed) return;
    }
    if (t->next < t->hi) { tx_frame(t, now, t->next, true); t->next++; }
    else if (t->next < t->N && t->next < t->base + t->cfg.W) { tx_frame(t, now, t->next, false); t->next++; t->hi = t->next; }
}

/* ======================= 연습 2: SR + 누적 ACK + SACK 비트맵 ======================= */
/* ACK = { seq: rcv_base (다음 기대, 누적), payload: bit i = rcv_base + 1 + i 를 버퍼에 가짐 (i < 32) } */
static void sack_send(win_rx *r, uint32_t now) {
    uint32_t bm = 0;
    for (uint32_t i = 0; i < 32u && i + 1u < r->cfg.W; i++)
        if (r->have[(r->expected + 1u + i) % WMAX]) bm |= 1u << i;
    r->acks_sent++;
    chan_send(r->link, now, PKT_ACK, (uint16_t)(r->expected & r->mask), bm);
}
static void sack_rx_on_data(win_rx *r, uint32_t now, uint16_t seq, uint32_t payload) {
    uint32_t d = ((uint32_t)seq - r->expected) & r->mask;
    if (d < r->cfg.W) {
        uint32_t slot = (r->expected + d) % WMAX;
        if (!r->have[slot]) {
            r->have[slot] = true; r->buf[slot] = payload;
            if (++r->buffered > r->max_buffered) r->max_buffered = r->buffered;
        } else {
            r->dup++;
        }
        while (r->have[r->expected % WMAX]) {
            uint32_t s = r->expected % WMAX;
            r->have[s] = false; r->buffered--;
            rx_deliver(r, now, r->buf[s]);
            r->expected++;
        }
    } else if (d >= r->mask + 1u - r->cfg.W) {
        r->old++;
    } else {
        r->ignored++;
        return;
    }
    sack_send(r, now);                                      /* 받은 뒤의 상태 전체를 알린다 */
}
static void sack_mark(win_tx *t, uint32_t idx) {
    uint32_t s = idx % WMAX;
    if (t->acked[s]) { t->acks_dup++; return; }
    t->acked[s] = true; t->need_retx[s] = false; t->acks_new++;
}
static void sack_tx_on_ack(win_tx *t, uint16_t cum, uint32_t bm) {
    uint32_t d = ((uint32_t)cum - t->base) & t->mask;
    if (d > t->hi - t->base) { t->acks_ignored++; return; }
    uint32_t cabs = t->base + d;
    for (uint32_t i = t->base; i < cabs; i++) sack_mark(t, i);
    for (uint32_t i = 0; i < 32u; i++)
        if ((bm >> i) & 1u) { uint32_t idx = cabs + 1u + i; if (idx < t->hi) sack_mark(t, idx); }
    while (t->base < t->hi && t->acked[t->base % WMAX]) { t->acked[t->base % WMAX] = false; t->base++; }
}

enum { PROTO_GBN_ST = 2, PROTO_SR_SACK = 3 };
static void data_in(world *w, uint32_t t, const pkt *p) {
    switch ((int)w->cfg.proto) {
    case PROTO_GBN: case PROTO_GBN_ST: gbn_rx_on_data(&w->rx, t, p->seq, p->payload); break;
    case PROTO_SR_SACK: sack_rx_on_data(&w->rx, t, p->seq, p->payload); break;
    default: sr_rx_on_data(&w->rx, t, p->seq, p->payload); break;
    }
}
static void ack_in(world *w, uint32_t t, const pkt *p) {
    switch ((int)w->cfg.proto) {
    case PROTO_GBN: case PROTO_GBN_ST: gbn_tx_on_ack(&w->tx, t, p->seq); break;
    case PROTO_SR_SACK: sack_tx_on_ack(&w->tx, p->seq, p->payload); break;
    default: sr_tx_on_ack(&w->tx, t, p->seq); break;
    }
}
static void poll_tx(world *w, uint32_t t) {
    switch ((int)w->cfg.proto) {
    case PROTO_GBN: gbn_tx_poll(&w->tx, t); break;
    case PROTO_GBN_ST: gbn_st_poll(&w->tx, t); break;
    default: sr_tx_poll(&w->tx, t); break;
    }
}

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
        while (chan_recv(&w->fwd, t, &p)) data_in(w, t, &p);     /* 연습 변형까지 포함한 디스패치 */
        while (chan_recv(&w->rev, t, &p)) ack_in(w, t, &p);
        if (w->tx.base >= N) { R.finished = true; break; }
        poll_tx(w, t);
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

/* ======================= 테스트 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static win_cfg mk(int p, uint8_t k, uint16_t W, uint32_t rto, uint8_t max_tx) {
    win_cfg c = { (proto_t)p, k, W, rto, max_tx }; return c;
}
static double avg_gp(int p, uint16_t W, const chan_cfg *f, const chan_cfg *r, uint32_t rto, uint32_t N, int seeds, bool *ok) {
    double acc = 0;
    for (int s = 1; s <= seeds; s++) {
        win_cfg c = mk(p, 8, W, rto, 100);
        run_result x = run_sim(&c, f, r, N, (uint32_t)s, NULL, 0, NULL, 0, 50000000);
        if (!x.ok) *ok = false;
        acc += (double)N / x.ticks;
    }
    return acc / seeds;
}
/* 비트 오류율 → 프레임 손실 (ppm): 1 − (1 − BER)^bits, libm 없이 */
static uint32_t frame_loss_ppm(double ber, uint32_t bits) {
    double ok = 1.0;
    for (uint32_t i = 0; i < bits; i++) ok *= 1.0 - ber;
    return (uint32_t)((1.0 - ok) * 1e6 + 0.5);
}

int main(void) {
    bool ok;
    /* ---- 연습 1: GBN 타이머 기준 바꾸기 ---- */
    {
        const chan_cfg L = { 10, 0, 50000 };               /* RTT 20, 손실 5 % 각 방향 */
        ok = true;
        double g0 = avg_gp(PROTO_GBN, 32, &L, &L, 30, 20000, 4, &ok);
        double g1 = avg_gp(PROTO_GBN_ST, 32, &L, &L, 30, 20000, 4, &ok);
        double p = 0.05, th0 = 1.0 / (1 + p * 49 + p * p / (1 - p) * 30), th1 = 1.0 / (1 + p / (1 - p) * 30);
        printf("ex1 GBN W=32 RTT 20 RTO 30 loss 5%%: restart-on-advance %.1f%% (theory %.1f) | send-time timer %.1f%% (theory %.1f)\n",
               100 * g0, 100 * th0, 100 * g1, 100 * th1);
        CHECK(ok);
        CHECK(g1 > 1.3 * g0);                              /* 손실 비용 49 → 30 틱 */
        CHECK(g1 > 0.95 * th1 && g1 < 1.05 * th1);
        uint32_t bad = 0;                                  /* 정확성: 무작위 손실·재정렬, 짧은 RTO 포함 */
        for (uint32_t seed = 1; seed <= 20; seed++) {
            chan_cfg J = { 5, seed % 4u * 3u, 30000u * (seed % 5u) };
            win_cfg c = mk(PROTO_GBN_ST, 8, (uint16_t)(2u + seed % 30u), 8u + seed % 25u, 100);
            run_result x = run_sim(&c, &J, &J, 2000, seed, NULL, 0, NULL, 0, 50000000);
            if (!x.ok) bad++;
        }
        CHECK(bad == 0);
    }

    /* ---- 연습 2: SR + SACK ---- */
    {
        const chan_cfg D0 = { 10, 0, 0 }, A20 = { 10, 0, 200000 };   /* DATA 손실 0, ACK 손실 20 % */
        win_cfg cs = mk(PROTO_SR, 8, 16, 30, 50), ck = mk(PROTO_SR_SACK, 8, 16, 30, 50);
        run_result s = run_sim(&cs, &D0, &A20, 5000, 3, NULL, 0, NULL, 0, 50000000);
        run_result k = run_sim(&ck, &D0, &A20, 5000, 3, NULL, 0, NULL, 0, 50000000);
        printf("ex2 ACK loss 20%%, W=16: SR retx=%u (lost ACKs %u) ticks=%u | SR+SACK retx=%u ticks=%u\n",
               s.retx, s.ack_lost, s.ticks, k.retx, k.ticks);
        CHECK(s.ok && k.ok);
        CHECK(s.retx == s.ack_lost);                       /* 개별 ACK: 잃은 ACK 하나 = 재전송 하나 */
        CHECK(k.retx * 20u < s.retx);                      /* 누적+SACK: 다음 ACK 가 덮는다 */
        const chan_cfg L = { 10, 0, 50000 };
        ok = true;
        double gg = avg_gp(PROTO_GBN, 2, &L, &L, 30, 10000, 4, &ok);
        double gs = avg_gp(PROTO_SR, 2, &L, &L, 30, 10000, 4, &ok);
        double gk = avg_gp(PROTO_SR_SACK, 2, &L, &L, 30, 10000, 4, &ok);
        double gk32 = avg_gp(PROTO_SR_SACK, 32, &L, &L, 30, 10000, 4, &ok);
        double gs32 = avg_gp(PROTO_SR, 32, &L, &L, 30, 10000, 4, &ok);
        printf("ex2 loss 5%%, W=2: GBN %.2f%%  SR %.2f%%  SR+SACK %.2f%% | W=32: SR %.1f%%  SR+SACK %.1f%%\n",
               100 * gg, 100 * gs, 100 * gk, 100 * gs32, 100 * gk32);
        CHECK(ok);
        CHECK(gs < gg && gk >= gg);                        /* 작은 창: SR < GBN, SACK 를 붙이면 ≥ GBN */
        CHECK(gk32 > gs32);
        uint32_t bad = 0;
        for (uint32_t seed = 1; seed <= 20; seed++) {      /* FIFO, 경계 W = 2^(k−1) 에서도 정확 */
            chan_cfg F = { 10, 0, 40000u * (seed % 5u) };
            win_cfg c = mk(PROTO_SR_SACK, (uint8_t)(3u + seed % 4u), 0, 12u + seed % 30u, 100);
            c.W = (uint16_t)(1u << (c.k - 1u));
            run_result x = run_sim(&c, &F, &F, 2000, seed, NULL, 0, NULL, 0, 50000000);
            if (!x.ok) bad++;
        }
        CHECK(bad == 0);
    }

    /* ---- 연습 3: 재정렬 + RTO > RTTmax → 2^k ≥ 2W 로 충분 ---- */
    {
        const chan_cfg J = { 5, 12, 100000 };              /* 지연 5..17/방향 → RTTmax 34 */
        uint32_t f_short = 0, f_long = 0;
        for (uint32_t seed = 1; seed <= 20; seed++)
            for (uint16_t W = 2; W <= 16; W *= 2) {
                uint8_t k = 1; while ((1u << k) < 2u * W) k++;
                win_cfg c = mk(PROTO_SR, k, W, 8, 200);
                if (!run_sim(&c, &J, &J, 1500, seed, NULL, 0, NULL, 0, 50000000).ok) f_short++;
                c.rto = 36;                                /* > RTTmax 34 */
                if (!run_sim(&c, &J, &J, 1500, seed, NULL, 0, NULL, 0, 50000000).ok) f_long++;
            }
        uint8_t kn = seq_bits_needed(PROTO_SR, 16, &J, &J);  /* RTO 를 모르는 충분조건: max(2W + Jf, W + Rmax) = 50 → 6 */
        printf("ex3 SR k=log2(2W), delay 5..17: RTO 8 -> failed %u/80, RTO 36 (> RTTmax 34) -> failed %u/80 | seq_bits_needed(W=16)=%u vs 5\n",
               f_short, f_long, kn);
        CHECK(f_short > 0 && f_long == 0);
        CHECK(kn == 6);
    }

    /* ---- 연습 4: 수신 RAM 1 KB — 큰 프레임 적게 vs 작은 프레임 많이 ---- */
    {
        const double ber = 7e-5;
        uint32_t p72 = frame_loss_ppm(ber, 720), p40 = frame_loss_ppm(ber, 400), pack = frame_loss_ppm(ber, 80);
        CHECK(p72 > 49000 && p72 < 49300 && p40 > 27600 && p40 < 27800 && pack > 5500 && pack < 5700);
        radio_link La = { 57600, 10, 72, 64, 200000, p72 }, Lb = { 57600, 10, 40, 32, 200000, p40 };
        link_plan_t Pa = link_plan(&La), Pb = link_plan(&Lb);
        uint16_t Wa = (uint16_t)(1024u / La.payload_bytes), Wb = (uint16_t)(1024u / Lb.payload_bytes);
        double Bps[2];
        ok = true;
        for (int i = 0; i < 2; i++) {
            const radio_link *L = i ? &Lb : &La; const link_plan_t *P = i ? &Pb : &Pa;
            chan_cfg fd = { P->R / 2u, 0, L->loss_ppm }, rv = { P->R - P->R / 2u, 0, pack };
            double msgs_per_tick = avg_gp(PROTO_SR, i ? Wb : Wa, &fd, &rv, P->rto, 6000, 3, &ok);
            Bps[i] = msgs_per_tick * L->payload_bytes * 1e6 / P->frame_us;
            printf("ex4 %u B payload (%u B frame): loss %.2f%%, frame %u us, R=%u, RTO=%u, W=%u (1 KB rx) -> %.0f B/s (line %u B/s)\n",
                   L->payload_bytes, L->frame_bytes, L->loss_ppm / 1e4, P->frame_us, P->R, P->rto, i ? Wb : Wa, Bps[i], P->line_Bps);
        }
        CHECK(ok);
        CHECK(Pa.R == 16 && Pb.R == 29 && Wa == 16 && Wb == 32);
        CHECK(Bps[0] > Bps[1]);                            /* 같은 RAM = 창이 덮는 시간이 거의 같다 → 헤더 비율이 작은 큰 프레임이 이긴다 (이 링크에서) */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
