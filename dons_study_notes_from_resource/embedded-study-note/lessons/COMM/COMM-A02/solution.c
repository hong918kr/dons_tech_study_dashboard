/* COMM-A02 슬라이딩 윈도 신뢰 전송: Go-Back-N vs Selective Repeat
 *
 * 링크 시뮬레이터 (틱 단위, xorshift32 고정 시드), 방향 둘: fwd = DATA (송신→수신), rev = ACK (수신→송신).
 *   방향마다 지연 = delay + U[0, jitter] (jitter > 0 이면 재정렬), 손실 확률 loss_ppm (백만분율), 비행 중 패킷 상한
 *   CHAN_CAP (넘치면 버리고 overflow 카운트). link_script 로 "이 채널의 n 번째 패킷"을 정확히 버리거나 늦출 수 있다.
 *   링크 속도: 송신은 한 틱에 DATA 프레임 하나 (1 틱 = 프레임 하나의 송신 시간). ACK 는 작아서 속도 제한 없음.
 *   손상(CRC 실패)은 손실과 같다 (COMM-M14) — 여기서는 손실만 주입한다.
 * 프레임 (구조체로 모델링): DATA = {seq (k 비트), payload (메시지 내용 32 비트)}, ACK = {seq}.
 *   payload = msg_payload(메시지 번호) — 앱이 받은 순서대로 확인하므로 "잘못된 메시지를 새 것으로 받음" 이 바로 보인다.
 * 프로토콜 (이 레슨의 정의):
 *   GBN: 수신은 seq == expected 일 때만 받고, 받든 버리든 ACK(expected) — "다음에 기대하는 seq" 누적 ACK (TCP 식).
 *        송신은 타이머 하나 (가장 오래된 미확인 프레임용, base 가 전진하면 다시 시작). 타임아웃 → next = base 로 되감아
 *        미확인 프레임 전부 재전송. 정확성 조건 W ≤ 2^k − 1 (FIFO 링크).
 *   SR : 수신은 창 [rcv_base, rcv_base + W) 안의 프레임을 버퍼링하고 개별 ACK(seq), 순서대로 앱에 넘긴다.
 *        창 바로 뒤 [rcv_base − W, rcv_base) 는 옛 것 → ACK 만 다시. 송신은 프레임마다 타이머, 만료된 것만 재전송.
 *        정확성 조건 W ≤ 2^(k−1) (FIFO 링크).
 *   공통: 한 프레임을 max_tx 번 보내고도 확인을 못 받으면 FAIL (링크 끊김으로 보고), 송신은 멈춘다.
 *
 * 한 틱의 순서: (1) 도착한 DATA → 수신 (ACK 를 rev 로) (2) 도착한 ACK → 송신 (3) 송신 타이머 검사 (4) 프레임 ≤ 1 개 송신
 *
 * Part A: A1 gbn_rx_on_data, A2 gbn_tx_on_ack, A3 gbn_tx_timeout, A4 sr_rx_on_data, A5 sr_tx_on_ack, A6 sr_tx_timeouts
 * Part B: B1 seq_bits_needed (seq 공간 조건) + 결함 주입: 시퀀스 공간 전수 시나리오, 무작위 손실/재정렬, 전부 손실
 * Part C: C1 link_plan (57600 baud 텔레메트리 무선: W, k, RAM 선택) + 시뮬레이션으로 확인
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
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

/* ======================= 테스트 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static const chan_cfg LINK10 = { 10, 0, 0 };          /* 지연 10 틱/방향 → RTT 20 틱, 손실 없음, FIFO */
static win_cfg mk(proto_t p, uint8_t k, uint16_t W, uint32_t rto, uint8_t max_tx) {
    win_cfg c = { p, k, W, rto, max_tx }; return c;
}
/* 손실 없는 FIFO 링크에서 마지막 ACK 를 처리하는 틱 (W ≤ R: 라운드마다 W 개, W ≥ R: 매 틱 하나) */
static uint32_t clean_finish(uint32_t N, uint32_t W, uint32_t R) {
    if (W >= R) return N - 1u + R;
    return (N - 1u) / W * R + (N - 1u) % W + R;
}

int main(void) {
    const char *pn[2] = { "GBN", "SR " };
    run_result r, g, s;

    /* ---------- Part A: 손실 없는 링크 — 처리량 = min(1, W/RTT) ---------- */
    {
        int bad = 0;
        for (int p = 0; p < 2; p++)
            for (uint16_t W = 1; W <= 32; W++) {
                win_cfg c = mk((proto_t)p, 8, W, 22, 8);
                r = run_sim(&c, &LINK10, &LINK10, 200, 1, NULL, 0, NULL, 0, 100000);
                if (!r.ok || r.ticks != clean_finish(200, W, 20) || r.frames != 200 || r.retx != 0) bad++;
            }
        CHECK(bad == 0);                                   /* 64 구성 전부 정확한 완료 틱 */
        win_cfg c = mk(PROTO_SR, 3, 4, 22, 8);             /* seq 가 0..7 을 125 번 넘게 돈다 */
        r = run_sim(&c, &LINK10, &LINK10, 1000, 1, NULL, 0, NULL, 0, 100000);
        CHECK(r.ok && r.ticks == clean_finish(1000, 4, 20));
        c = mk(PROTO_GBN, 3, 7, 22, 8);
        r = run_sim(&c, &LINK10, &LINK10, 1000, 1, NULL, 0, NULL, 0, 100000);
        CHECK(r.ok && r.ticks == clean_finish(1000, 7, 20));
        printf("A  clean RTT 20: W=1 %u ticks, W=5 %u, W=20 %u, W=32 %u (200 msgs)\n",
               clean_finish(200, 1, 20), clean_finish(200, 5, 20), clean_finish(200, 20, 20), clean_finish(200, 32, 20));
    }

    /* ---------- Part A: DATA 하나 손실 / ACK 하나 손실 — GBN 은 창 전체, SR 은 하나 ---------- */
    {
        static const link_script drop3[] = { { 3, 1, 0 } };
        win_cfg cg = mk(PROTO_GBN, 8, 8, 22, 8), cs = mk(PROTO_SR, 8, 8, 22, 8);
        printf("A  trace: GBN W=8, RTT 20, RTO 22, DATA #3 lost (N=12)\n");
        g_trace = true;
        g = run_sim(&cg, &LINK10, &LINK10, 12, 1, drop3, 1, NULL, 0, 100000);
        printf("A  trace: SR W=8, same link\n");
        s = run_sim(&cs, &LINK10, &LINK10, 12, 1, drop3, 1, NULL, 0, 100000);
        g_trace = false;
        CHECK(g.ok && s.ok);
        CHECK(g.retx == 8 && g.rx_discarded == 7 && g.timeouts == 1);   /* #3..#10 다시 (창 W=8 전부), #4..#10 버려짐 */
        CHECK(s.retx == 1 && s.max_buffered == 8 && s.rx_discarded == 0); /* #3 만 다시, #4..#10 은 버퍼에서 기다림 (+#3 = W 칸 전부) */
        CHECK(s.ticks < g.ticks);
        printf("A  DATA#3 lost: GBN retx=%u discarded=%u ticks=%u | SR retx=%u max_buffered=%u ticks=%u\n",
               g.retx, g.rx_discarded, g.ticks, s.retx, s.max_buffered, s.ticks);

        static const link_script dropack3[] = { { 3, 1, 0 } };
        g = run_sim(&cg, &LINK10, &LINK10, 12, 1, NULL, 0, dropack3, 1, 100000);
        s = run_sim(&cs, &LINK10, &LINK10, 12, 1, NULL, 0, dropack3, 1, 100000);
        CHECK(g.ok && g.retx == 0 && g.timeouts == 0);    /* 누적 ACK: 다음 ACK 가 덮는다 */
        CHECK(s.ok && s.retx == 1 && s.rx_old == 1);       /* 개별 ACK: #3 타임아웃 → 재전송 → 수신은 "옛 것" → re-ACK */
        printf("A  ACK#3 lost: GBN retx=%u | SR retx=%u rx_old=%u\n", g.retx, s.retx, s.rx_old);

        /* 되감기 함정: ACK #0..#4 가 늦게(t=24 한꺼번에) 와서 t=22 에 되감은 뒤 next(=2) 를 앞지른다 → hi 기준으로 받아야 */
        static const link_script late[] = { { 0, 0, 4 }, { 1, 0, 3 }, { 2, 0, 2 }, { 3, 0, 1 } };
        printf("A  trace: GBN W=8 RTO 22, ACKs of #0..#3 delayed to t=24 (N=8)\n");
        g_trace = true;
        g = run_sim(&cg, &LINK10, &LINK10, 8, 1, NULL, 0, late, 4, 100000);
        g_trace = false;
        CHECK(g.ok && g.timeouts == 1 && g.retx == 5 && g.acks_ignored == 0 && g.sent_acked == 0);   /* #0 #1 그리고 #5 #6 #7 만 다시 */

        /* RTO < RTT: 쓸데없는 타임아웃이 반복돼도 정확성은 유지 (비효율만) */
        win_cfg cq = mk(PROTO_GBN, 8, 8, 15, 30);
        g = run_sim(&cq, &LINK10, &LINK10, 300, 1, NULL, 0, NULL, 0, 100000);
        CHECK(g.ok && g.timeouts > 0);
        printf("A  GBN RTO 15 < RTT 20 (spurious): ok=%d timeouts=%u retx=%u frames=%u ticks=%u\n",
               g.ok, g.timeouts, g.retx, g.frames, g.ticks);
    }

    /* ---------- Part B: 시퀀스 공간 — 첫 창의 ACK 전부 손실 시나리오, k = 1..6, W = 1..2^k 전수 ---------- */
    {
        static link_script acks[WMAX];
        for (uint32_t i = 0; i < WMAX; i++) { acks[i].idx = i; acks[i].drop = 1; acks[i].extra = 0; }
        int bad = 0, cases = 0, gbn_fail_at = 0, sr_fail_at = 0;
        for (uint8_t k = 1; k <= 6; k++)
            for (uint16_t W = 1; W <= (1u << k) && W <= WMAX; W++)
                for (int p = 0; p < 2; p++) {
                    win_cfg c = mk((proto_t)p, k, W, 2u * W + 22u, 8);
                    uint32_t N = 4u * (1u << k) + W;
                    r = run_sim(&c, &LINK10, &LINK10, N, 1, NULL, 0, acks, W, 1000000);
                    bool expect_ok = (p == PROTO_GBN) ? (W <= (1u << k) - 1u) : (2u * W <= (1u << k));
                    cases++;
                    if (r.ok != expect_ok) bad++;
                    if (!r.ok && r.wrong > 0 && W == (p == PROTO_GBN ? (1u << k) : (1u << (k - 1)) + 1u)) {
                        if (p == PROTO_GBN) gbn_fail_at++; else sr_fail_at++;
                    }
                }
        CHECK(bad == 0);
        CHECK(gbn_fail_at == 6 && sr_fail_at == 6);       /* 경계 + 1 에서 6 개 k 모두 "옛 메시지를 새 것으로" 받음 */
        printf("B  seq-space scenario: %d cases, mismatches=%d (expected: GBN ok iff W <= 2^k-1, SR ok iff W <= 2^(k-1))\n", cases, bad);

        win_cfg c = mk(PROTO_SR, 3, 5, 32, 8);
        printf("B  trace: SR k=3 W=5 (> 2^(k-1)), ACKs of #0..#4 lost (N=10)\n");
        g_trace = true;
        r = run_sim(&c, &LINK10, &LINK10, 10, 1, NULL, 0, acks, 5, 100000);
        g_trace = false;
        CHECK(!r.ok && r.wrong == 2);                      /* 재전송 seq 0, 1 이 새 창 {5,6,7,0,1} 에 들어감 → #8, #9 로 오인 */
        c.W = 4;
        r = run_sim(&c, &LINK10, &LINK10, 10, 1, NULL, 0, acks, 4, 100000);
        CHECK(r.ok && r.rx_old == 4);                      /* W = 4: 재전송 4 개 전부 "옛 것" → re-ACK */
    }

    /* ---------- Part B: 무작위 손실 (FIFO), 경계 W 에서 0 위반 — 짧은 RTO(쓸데없는 재전송)도 포함 ---------- */
    {
        uint32_t viol = 0, runs = 0, frames = 0, msgs = 0, above_viol = 0;
        for (uint8_t k = 2; k <= 5; k++)
            for (int p = 0; p < 2; p++)
                for (uint32_t seed = 1; seed <= 6; seed++) {
                    uint16_t W = (p == PROTO_GBN) ? (uint16_t)((1u << k) - 1u) : (uint16_t)(1u << (k - 1));
                    chan_cfg L = { 10, 0, 50000u * (seed % 3u + 1u) };    /* 5 / 10 / 15 % 각 방향 */
                    uint32_t rto = (seed & 1u) ? 12u : 30u;                /* 12 < RTT 20: 쓸데없는 재전송 많음 */
                    win_cfg c = mk((proto_t)p, k, W, rto, 40);
                    r = run_sim(&c, &L, &L, 600, seed, NULL, 0, NULL, 0, 2000000);
                    runs++; frames += r.frames; msgs += r.delivered;
                    if (!r.ok || r.overflow) viol++;
                    win_cfg c2 = c; c2.W = (uint16_t)(W + 1u);             /* 경계 + 1 */
                    run_result r2 = run_sim(&c2, &L, &L, 600, seed, NULL, 0, NULL, 0, 2000000);
                    if (!r2.ok) above_viol++;
                }
        CHECK(viol == 0);
        CHECK(above_viol > 0);
        printf("B  random FIFO at bound: runs=%u msgs=%u frames=%u violations=%u | bound+1: failed runs=%u/%u\n",
               runs, msgs, frames, viol, above_viol, runs);
    }

    /* ---------- Part B: 재정렬 (jitter) — FIFO 조건으로는 부족, seq_bits_needed 로 0 ---------- */
    {
        CHECK(seq_bits_needed(PROTO_SR, 4, &LINK10, &LINK10) == 3 && seq_bits_needed(PROTO_GBN, 7, &LINK10, &LINK10) == 3);
        CHECK(seq_bits_needed(PROTO_SR, 8, &LINK10, &LINK10) == 4 && seq_bits_needed(PROTO_GBN, 8, &LINK10, &LINK10) == 4);
        chan_cfg J = { 5, 10, 100000 };                    /* 지연 5..15/방향, 손실 10 % */
        CHECK(seq_bits_needed(PROTO_SR, 4, &J, &J) == 6);  /* max(8 + 10, 4 + 30) = 34 → 64 */
        CHECK(seq_bits_needed(PROTO_GBN, 4, &J, &J) == 6); /* max(4 + 1 + 10, 4 + 30 + 1) = 35 → 64 */
        CHECK(seq_bits_needed(PROTO_SR, 32, &J, &J) == 7); /* max(64 + 10, 62) = 74 → 128 */
        uint32_t v_fifo[2] = { 0, 0 }, v_need[2] = { 0, 0 }, runs = 0;
        for (int p = 0; p < 2; p++)
            for (uint32_t seed = 1; seed <= 8; seed++)
                for (uint16_t W = 2; W <= 8; W *= 2) {
                    uint8_t kf = seq_bits_needed((proto_t)p, W, &LINK10, &LINK10);   /* FIFO 공식 */
                    uint8_t kn = seq_bits_needed((proto_t)p, W, &J, &J);
                    win_cfg c = mk((proto_t)p, kf, W, 12, 60);                       /* 짧은 RTO: 쓸데없는 재전송 + 재정렬 */
                    r = run_sim(&c, &J, &J, 800, seed, NULL, 0, NULL, 0, 4000000);
                    if (!r.ok) v_fifo[p]++;
                    c.k = kn;
                    r = run_sim(&c, &J, &J, 800, seed, NULL, 0, NULL, 0, 4000000);
                    if (!r.ok) v_need[p]++;
                    runs++;
                }
        CHECK(v_need[0] == 0 && v_need[1] == 0);
        CHECK(v_fifo[0] + v_fifo[1] > 0);
        printf("B  jitter 5..15 + RTO 12: FIFO-formula k -> failed runs GBN %u SR %u | seq_bits_needed k -> GBN %u SR %u (of %u each)\n",
               v_fifo[0], v_fifo[1], v_need[0], v_need[1], runs / 2u);
    }

    /* ---------- Part B: 전부 손실 → 정확히 W·max_tx 프레임, t = max_tx·RTO 에 FAIL ---------- */
    {
        chan_cfg dead = { 10, 0, 1000000 };
        for (int p = 0; p < 2; p++) {
            win_cfg c = mk((proto_t)p, 8, 8, 30, 5);
            r = run_sim(&c, &dead, &LINK10, 100, 1, NULL, 0, NULL, 0, 100000);
            CHECK(r.failed && r.fail_idx == 0 && r.frames == 8u * 5u && r.ticks == 5u * 30u && r.delivered == 0);
            printf("B  %s all DATA lost: failed=%d frames=%u t_fail=%u delivered=%u\n", pn[p], r.failed, r.frames, r.ticks, r.delivered);
            r = run_sim(&c, &LINK10, &dead, 100, 1, NULL, 0, NULL, 0, 100000);
            CHECK(r.failed && r.delivered == 8);           /* ACK 만 죽음: FAIL 인데 수신은 첫 창 8 개를 이미 받음 (COMM-M14) */
            printf("B  %s all ACK lost : failed=%d frames=%u delivered=%u (FAIL != not delivered)\n", pn[p], r.failed, r.frames, r.delivered);
        }
    }

    /* ---------- Part B: 무작위 손실 + 재정렬 스트레스 (k = 8, 정확히 한 번·순서대로), 재전송 수 검사 ---------- */
    {
        uint32_t bad = 0, sr_exact = 0, runs = 0, msgs = 0;
        for (uint32_t seed = 1; seed <= 12; seed++) {
            chan_cfg L = { 10, (seed % 3u) * 4u, 20000u * (seed % 6u) };  /* 지터 0/4/8, 손실 0..10 % */
            for (int p = 0; p < 2; p++) {
                uint16_t W = (uint16_t)(4u + (seed * 5u) % 29u);
                uint32_t rto = 20u + 2u * L.jitter + 2u;              /* RTO > RTTmax: 쓸데없는 재전송 없음 */
                win_cfg c = mk((proto_t)p, 8, W, rto, 60);
                r = run_sim(&c, &L, &L, 3000, seed, NULL, 0, NULL, 0, 4000000);
                runs++; msgs += r.delivered;
                if (!r.ok || r.overflow || r.sent_acked) bad++;
                if (p == PROTO_SR && r.retx == r.data_lost + r.ack_lost) sr_exact++;
                if (p == PROTO_GBN && r.retx > (uint32_t)W * r.timeouts) bad++;
            }
        }
        CHECK(bad == 0);
        CHECK(sr_exact == 12);                             /* SR: 재전송 수 == 잃은 DATA + 잃은 ACK (RTO > RTTmax 일 때 정확히) */
        printf("B  stress k=8: runs=%u msgs=%u bad=%u, SR retx == lostDATA+lostACK in %u/12 runs\n", runs, msgs, bad, sr_exact);
    }

    /* ---------- Part C: 57600 baud 텔레메트리 무선, RTT 200 ms, 손실 5 % ---------- */
    {
        radio_link L = { 57600, 10, 72, 64, 200000, 50000 };
        link_plan_t P = link_plan(&L);
        CHECK(P.frame_us == 12500 && P.R == 16 && P.rto == 24 && P.W == 40 && P.k == 7);
        CHECK(P.ram_tx == 2560 && P.ram_rx_sr == 2560 && P.ram_rx_gbn == 0 && P.line_Bps == 5120);
        radio_link L2 = { 115200, 10, 40, 32, 50000, 0 };   /* 40 B 프레임 @115200 = 3473 µs (올림) */
        link_plan_t P2 = link_plan(&L2);
        CHECK(P2.frame_us == 3473 && P2.R == 15 && P2.rto == 23 && P2.W == 38 && P2.k == 7);
        printf("C  plan 57600: frame %u us, R=%u ticks (BDP %u frames), RTO=%u, SR W=%u, k=%u, RAM tx %u B + rx %u B, line %u B/s\n",
               P.frame_us, P.R, P.R, P.rto, P.W, P.k, P.ram_tx, P.ram_rx_sr, P.line_Bps);
        chan_cfg rl = { P.R / 2u, 0, L.loss_ppm };
        const uint16_t Ws[4] = { 8, 16, 32, 40 };
        double sr_gp[4] = { 0 }, gbn_gp[4] = { 0 };
        bool all_ok = true;
        for (int i = 0; i < 4; i++)
            for (int p = 0; p < 2; p++) {
                win_cfg c = mk((proto_t)p, (uint8_t)P.k, Ws[i], P.rto, 16);
                r = run_sim(&c, &rl, &rl, 4000, 7, NULL, 0, NULL, 0, 4000000);
                all_ok = all_ok && r.ok;
                double Bps = (r.ok && P.frame_us) ? 4000.0 * L.payload_bytes / ((double)r.ticks * P.frame_us / 1e6) : 0.0;
                if (p == PROTO_SR) sr_gp[i] = Bps; else gbn_gp[i] = Bps;
                printf("C  %s W=%2u: %u ticks = %.1f s, goodput %.0f B/s (%.1f %% of line), retx %u\n",
                       pn[p], Ws[i], r.ticks, r.ticks * P.frame_us / 1e6, Bps, P.line_Bps ? 100.0 * Bps / P.line_Bps : 0.0, r.retx);
            }
        double q = (1.0 - L.loss_ppm / 1e6) * (1.0 - L.loss_ppm / 1e6);
        CHECK(all_ok);
        CHECK(sr_gp[3] > 0.85 * q * P.line_Bps);           /* 계획한 W 의 SR 은 이론 상한 q·line 의 85 % 이상 */
        CHECK(sr_gp[3] > gbn_gp[3] && sr_gp[0] < 0.6 * sr_gp[3]);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
