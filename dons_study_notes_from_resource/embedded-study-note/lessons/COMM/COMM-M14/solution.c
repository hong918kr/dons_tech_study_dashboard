/* COMM-M14 재시도·타임아웃·백오프: stop-and-wait ARQ
 *
 * 링크 시뮬레이터 (틱 단위, xorshift32 고정 시드), 방향 둘: fwd = DATA (송신→수신), rev = ACK (수신→송신).
 *   패킷마다 손실 / 중복 / 손상(비트 하나 뒤집기 → CRC 실패 → 버림) / 지연(고정 + U[0,jitter] + 드문 late_extra),
 *   링크 down (보내기 자체를 거부). link_script 로 "n 번째 패킷"에 결함을 정확히 넣을 수도 있다.
 * 프레임: DATA = [0x01][SEQ][LEN][PAYLOAD × LEN][CRC_HI][CRC_LO],  ACK = [0x02][SEQ][CRC_HI][CRC_LO]
 *   CRC-16/CCITT-FALSE (COMM-B10 축약판), TYPE 부터 PAYLOAD 끝까지. SEQ 는 seq_bits 비트 (1..8).
 * 프로토콜 (이 레슨의 정의):
 *   송신: 한 번에 메시지 하나. 보내고 타이머 → seq 가 같은 ACK 면 OK. 타임아웃이면 재전송 (FIXED / EXP+cap /
 *         ADAPTIVE(RFC 6298), + 지터). max_retries 번 재전송해도 없으면 FAIL: 모든 시도가 링크에 거부됐으면
 *         LINK_DOWN, 아니면 TIMEOUT_EXHAUSTED. OK 든 FAIL 이든 seq 는 다음 값 (실패한 seq 재사용 금지).
 *   수신: d = (seq − last) mod 2^b 가 1..2^(b−1) 이면 새 메시지 → 전달, last = seq. 아니면 중복 → 전달 안 함.
 *         둘 다 ACK(seq) 를 보낸다. 손상된 프레임은 ACK 없이 버린다.
 *
 * Part A: A1 seq_is_new, A2 송신의 ACK 처리, A3 백오프 타임아웃, A4 타임아웃 → 재전송/실패 분류, A5 수신 처리
 * Part B: B1 seq_bits_required (seq 폭 하한) + 결함 주입 테스트, 1비트 seq 의 고전 버그, 무작위 스트레스
 * Part C: C1 RTT 추정 (Jacobson/Karels 정수 스케일, RFC 6298) + Karn 규칙, 큐 API, 이벤트 로그
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* ---------------- CRC-16/CCITT-FALSE (COMM-B10 축약판) ---------------- */
static uint16_t crc16(const uint8_t *p, size_t n) {
    uint16_t c = 0xFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint16_t)(p[i] << 8);
        for (int k = 0; k < 8; k++) c = (c & 0x8000u) ? (uint16_t)((c << 1) ^ 0x1021u) : (uint16_t)(c << 1);
    }
    return c;
}

/* ---------------- 프레임 ---------------- */
#define FT_DATA          0x01u
#define FT_ACK           0x02u
#define ARQ_MAX_PAYLOAD  16u
#define FRAME_MAX        (5u + ARQ_MAX_PAYLOAD)          /* DATA 최대 21 바이트 */

static size_t frame_data(uint8_t *out, uint8_t seq, const uint8_t *pl, uint8_t len) {
    out[0] = FT_DATA; out[1] = seq; out[2] = len;
    if (len) memcpy(out + 3, pl, len);
    uint16_t c = crc16(out, 3u + len);
    out[3 + len] = (uint8_t)(c >> 8); out[4 + len] = (uint8_t)c;
    return 5u + len;
}
static size_t frame_ack(uint8_t *out, uint8_t seq) {
    out[0] = FT_ACK; out[1] = seq;
    uint16_t c = crc16(out, 2);
    out[2] = (uint8_t)(c >> 8); out[3] = (uint8_t)c;
    return 4;
}
typedef struct { uint8_t type, seq, len; const uint8_t *pl; } frame_view;
/* 0 = 정상, -1 = CRC 불일치 또는 형식 오류 (= "손상" 으로 버린다) */
static int frame_parse(const uint8_t *b, size_t n, frame_view *v) {
    if (n < 4 || crc16(b, n - 2) != (uint16_t)((b[n - 2] << 8) | b[n - 1])) return -1;
    v->type = b[0]; v->seq = b[1]; v->len = 0; v->pl = NULL;
    if (b[0] == FT_ACK) return n == 4 ? 0 : -1;
    if (b[0] == FT_DATA && n >= 5 && b[2] <= ARQ_MAX_PAYLOAD && b[2] == n - 5) { v->len = b[2]; v->pl = b + 3; return 0; }
    return -1;
}

/* ---------------- 링크 시뮬레이터 (한 방향 = channel) ---------------- */
#define LINK_QCAP 32
enum { ACT_NONE, ACT_DROP, ACT_DELAY, ACT_DUP, ACT_CORRUPT };
typedef struct { uint32_t idx; uint8_t act; uint32_t arg; } link_script;  /* idx = 이 채널로 보낸 몇 번째 패킷(0부터) */

typedef struct {
    uint16_t loss_pm, dup_pm, corrupt_pm, late_pm;   /* 천분율(‰) */
    uint32_t delay, jitter, late_extra;              /* 지연 = delay + U[0,jitter] (+ late_extra, 확률 late_pm) */
} chan_cfg;

typedef struct { uint8_t b[FRAME_MAX]; uint8_t n; uint32_t due, order; } pkt;

typedef struct {
    chan_cfg cfg;
    bool up;                                         /* false = 링크 down: 보내기 자체를 거부 */
    uint32_t rng;
    pkt q[LINK_QCAP]; int nq;
    uint32_t order;
    const link_script *script; size_t nscript;
    uint32_t sent, refused, lost, duplicated, corrupted, delivered, overflow;
} channel;

enum { LINK_OK = 0, LINK_EDOWN = -1 };

static void chan_init(channel *c, const chan_cfg *cfg, uint32_t seed) {
    memset(c, 0, sizeof *c); c->cfg = *cfg; c->up = true; c->rng = seed ? seed : 1u;
}
static uint32_t chan_delay(channel *c) {
    uint32_t d = c->cfg.delay;
    if (c->cfg.jitter) d += xs32(&c->rng) % (c->cfg.jitter + 1u);
    if (c->cfg.late_pm && xs32(&c->rng) % 1000u < c->cfg.late_pm) d += c->cfg.late_extra;
    return d ? d : 1u;
}
static void chan_enqueue(channel *c, uint32_t now, const uint8_t *b, size_t n, uint32_t delay) {
    if (c->nq >= LINK_QCAP) { c->overflow++; return; }
    pkt *p = &c->q[c->nq++];
    memcpy(p->b, b, n); p->n = (uint8_t)n; p->due = now + delay; p->order = c->order++;
}
/* 보낸 쪽이 아는 것은 "링크가 받아 줬나(LINK_OK) / down 이라 거부했나(LINK_EDOWN)" 뿐 — 손실은 모른다 */
static int chan_send(channel *c, uint32_t now, const uint8_t *b, size_t n) {
    if (!c->up) { c->refused++; return LINK_EDOWN; }
    uint32_t idx = c->sent++;
    uint8_t act = ACT_NONE; uint32_t arg = 0;
    for (size_t i = 0; i < c->nscript; i++)
        if (c->script[i].idx == idx) { act = c->script[i].act; arg = c->script[i].arg; }
    if (act == ACT_DROP || (c->cfg.loss_pm && xs32(&c->rng) % 1000u < c->cfg.loss_pm)) { c->lost++; return LINK_OK; }
    uint8_t buf[FRAME_MAX];
    memcpy(buf, b, n);
    if (act == ACT_CORRUPT || (c->cfg.corrupt_pm && xs32(&c->rng) % 1000u < c->cfg.corrupt_pm)) {
        uint32_t bit = (act == ACT_CORRUPT ? arg : xs32(&c->rng)) % (uint32_t)(n * 8u);
        buf[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        c->corrupted++;
    }
    chan_enqueue(c, now, buf, n, chan_delay(c) + (act == ACT_DELAY ? arg : 0u));
    if (act == ACT_DUP || (c->cfg.dup_pm && xs32(&c->rng) % 1000u < c->cfg.dup_pm)) {
        c->duplicated++;
        chan_enqueue(c, now, buf, n, chan_delay(c) + (act == ACT_DUP ? arg : 0u));
    }
    return LINK_OK;
}
/* now 까지 도착할 패킷 중 (due, order) 가 가장 이른 것 하나 */
static bool chan_recv(channel *c, uint32_t now, pkt *out) {
    int best = -1;
    for (int i = 0; i < c->nq; i++)
        if (c->q[i].due <= now && (best < 0 || c->q[i].due < c->q[best].due ||
                                   (c->q[i].due == c->q[best].due && c->q[i].order < c->q[best].order))) best = i;
    if (best < 0) return false;
    *out = c->q[best]; c->q[best] = c->q[--c->nq]; c->delivered++;
    return true;
}

/* ---------------- 이벤트 로그 (bring-up 엔지니어가 읽는 것) ---------------- */
typedef enum { EV_TX, EV_RETX, EV_TX_REFUSED, EV_TIMEOUT, EV_ACK_OK, EV_ACK_STALE, EV_ACK_CORRUPT,
               EV_DONE_OK, EV_FAIL_TIMEOUT, EV_FAIL_LINKDOWN, EV_RX_DELIVER, EV_RX_DUP, EV_RX_CORRUPT } ev_kind;
typedef struct { uint32_t t; uint8_t kind, seq, attempt; uint32_t val; } ev;
#define LOG_CAP 64
typedef struct { ev e[LOG_CAP]; uint32_t n; } evlog;          /* 가득 차면 n 만 센다 (malloc 없음) */
static void log_ev(evlog *L, uint32_t t, ev_kind k, uint8_t seq, uint8_t attempt, uint32_t val) {
    if (!L) return;
    if (L->n < LOG_CAP) { ev *e = &L->e[L->n]; e->t = t; e->kind = (uint8_t)k; e->seq = seq; e->attempt = attempt; e->val = val; }
    L->n++;
}

/* ---------------- 송신기 ---------------- */
typedef enum { BO_FIXED, BO_EXP, BO_ADAPTIVE } backoff_kind;
typedef struct {
    uint8_t  seq_bits;       /* 1..8 */
    uint8_t  max_retries;    /* 재전송 상한 → 전송은 최대 max_retries + 1 번 */
    backoff_kind bo;
    uint32_t rto_base;       /* FIXED/EXP: 첫 타임아웃. ADAPTIVE: 첫 RTT 샘플 전의 RTO */
    uint32_t rto_cap;        /* EXP 상한 = ADAPTIVE 의 RTO 상한 */
    uint32_t rto_min;        /* ADAPTIVE 의 RTO 하한 */
    uint8_t  jitter_pct;     /* 타임아웃 T 에 U[0, T·pct/100] 을 더한다 */
    uint32_t seed;
} arq_cfg;

typedef enum { ARQ_OK = 0, ARQ_FAIL_TIMEOUT, ARQ_FAIL_LINKDOWN } arq_status;
typedef struct {
    arq_status st;
    uint8_t  attempts, refused, corrupt_acks;   /* 전송 시도 / 그중 링크가 거부 / 손상돼 버린 ACK */
    uint32_t t_submit, t_start, t_done;
} arq_report;
typedef void (*arq_done_fn)(void *ctx, uint32_t tag, const arq_report *r);

enum { ARQ_EQUEUE_FULL = -1, ARQ_EMSGSIZE = -2 };
#define ARQ_TXQ 4
typedef struct { uint8_t pl[ARQ_MAX_PAYLOAD]; uint8_t len; uint32_t tag, t_submit; } txq_item;

typedef struct {
    arq_cfg cfg;
    channel *link; evlog *log;
    arq_done_fn done; void *ctx;
    txq_item q[ARQ_TXQ]; uint8_t qh, qn;        /* 앱 → ARQ 고정 큐 */
    bool busy;                                  /* 메시지 하나를 보내는 중 (stop-and-wait) */
    uint8_t seq;                                /* 지금 메시지의 seq */
    uint8_t frame[FRAME_MAX]; uint8_t frame_n;
    uint32_t tag;
    arq_report rep;
    uint32_t t_last_tx, deadline;
    int32_t  srtt8, rttvar4;                    /* Jacobson 스케일: 8·SRTT, 4·RTTVAR (틱) */
    bool     have_rtt;
    uint32_t rto, rtt_samples;
    uint32_t rng;
    uint32_t tx_frames, retx, stale_acks, corrupt_dropped, n_ok, n_fail_timeout, n_fail_linkdown;
} arq_tx;

static uint8_t seq_mask(uint8_t bits) { return (uint8_t)((1u << bits) - 1u); }

static void arq_tx_init(arq_tx *t, const arq_cfg *cfg, channel *link, arq_done_fn done, void *ctx, evlog *log) {
    memset(t, 0, sizeof *t);
    t->cfg = *cfg; t->link = link; t->done = done; t->ctx = ctx; t->log = log;
    t->rto = cfg->rto_base; t->rng = cfg->seed ? cfg->seed : 1u;
}

/* 앱 API: 0 = 큐에 넣음, ARQ_EQUEUE_FULL / ARQ_EMSGSIZE. 결과는 나중에 done 콜백으로 정확히 한 번. */
static int arq_send(arq_tx *t, uint32_t now, const uint8_t *pl, uint8_t len, uint32_t tag) {
    if (len > ARQ_MAX_PAYLOAD) return ARQ_EMSGSIZE;
    if (t->qn >= ARQ_TXQ) return ARQ_EQUEUE_FULL;
    txq_item *it = &t->q[(t->qh + t->qn) % ARQ_TXQ];
    if (len) memcpy(it->pl, pl, len);
    it->len = len; it->tag = tag; it->t_submit = now;
    t->qn++;
    return 0;
}

/* ---- Part A: 백오프 — attempt 번째(1부터) 전송 뒤 기다릴 틱 수 ---- */
static uint32_t arq_timeout(arq_tx *t, uint32_t attempt) {
    uint32_t T;
    if (t->cfg.bo == BO_FIXED) {
        T = t->cfg.rto_base;
    } else if (t->cfg.bo == BO_EXP) {
        T = t->cfg.rto_base;                                    /* base · 2^(attempt−1), cap 에서 멈춤 */
        for (uint32_t k = 1; k < attempt && T < t->cfg.rto_cap; k++) T *= 2u;
        if (T > t->cfg.rto_cap) T = t->cfg.rto_cap;
    } else {
        T = t->rto;                                             /* ADAPTIVE: 타임아웃 때 A4 가 rto 를 두 배로 올려 둔다 */
    }
    if (t->cfg.jitter_pct) T += xs32(&t->rng) % (T * t->cfg.jitter_pct / 100u + 1u);
    return T;
}

static void tx_transmit(arq_tx *t, uint32_t now) {
    t->rep.attempts++;
    if (t->rep.attempts > 1) t->retx++;
    t->t_last_tx = now;
    uint32_t T = arq_timeout(t, t->rep.attempts);
    t->deadline = now + T;
    if (chan_send(t->link, now, t->frame, t->frame_n) == LINK_EDOWN) {
        t->rep.refused++;
        log_ev(t->log, now, EV_TX_REFUSED, t->seq, t->rep.attempts, T);
    } else {
        t->tx_frames++;
        log_ev(t->log, now, t->rep.attempts > 1 ? EV_RETX : EV_TX, t->seq, t->rep.attempts, T);
    }
}

/* OK 든 FAIL 이든 seq 는 다음 값으로 넘어간다 (실패한 메시지의 seq 를 재사용하지 않는다 — 5. 풀이 참고) */
static void tx_finish(arq_tx *t, uint32_t now, arq_status st) {
    t->rep.st = st; t->rep.t_done = now; t->busy = false;
    if (st == ARQ_OK) t->n_ok++; else if (st == ARQ_FAIL_TIMEOUT) t->n_fail_timeout++; else t->n_fail_linkdown++;
    log_ev(t->log, now, st == ARQ_OK ? EV_DONE_OK : st == ARQ_FAIL_TIMEOUT ? EV_FAIL_TIMEOUT : EV_FAIL_LINKDOWN,
           t->seq, t->rep.attempts, t->rep.corrupt_acks);
    t->seq = (uint8_t)((t->seq + 1u) & seq_mask(t->cfg.seq_bits));
    arq_report r = t->rep;
    if (t->done) t->done(t->ctx, t->tag, &r);                  /* 콜백 안에서 arq_send 해도 된다 (큐) */
}

/* ---- Part C: RTT 샘플 → SRTT/RTTVAR/RTO (RFC 6298, Jacobson/Karels 정수 스케일) ---- */
static void rtt_sample(arq_tx *t, uint32_t r) {
    int32_t R = (int32_t)r;
    if (!t->have_rtt) {
        t->srtt8 = R << 3; t->rttvar4 = R << 1;                 /* SRTT = R, RTTVAR = R/2 */
        t->have_rtt = true;
    } else {
        int32_t err = R - (t->srtt8 >> 3);                      /* 옛 SRTT 기준 오차 */
        t->srtt8 += err;                                        /* SRTT += err/8      (α = 1/8) */
        if (err < 0) err = -err;
        t->rttvar4 += err - (t->rttvar4 >> 2);                  /* RTTVAR += (|err| − RTTVAR)/4 (β = 1/4) */
    }
    uint32_t rto = (uint32_t)(t->srtt8 >> 3) + (uint32_t)(t->rttvar4 > 1 ? t->rttvar4 : 1);  /* SRTT + max(G, 4·RTTVAR) */
    if (rto < t->cfg.rto_min) rto = t->cfg.rto_min;
    if (rto > t->cfg.rto_cap) rto = t->cfg.rto_cap;
    t->rto = rto;
    t->rtt_samples++;
}

/* ---- Part A: 송신 쪽이 받은 프레임 (ACK) ---- */
static void arq_tx_on_frame(arq_tx *t, uint32_t now, const uint8_t *b, size_t n) {
    frame_view v;
    if (frame_parse(b, n, &v) != 0) {                           /* 손상 → 버리고 센다. 재전송은 타이머가 한다 */
        t->corrupt_dropped++;
        if (t->busy) t->rep.corrupt_acks++;
        log_ev(t->log, now, EV_ACK_CORRUPT, t->seq, t->rep.attempts, 0);
        return;
    }
    if (v.type != FT_ACK) return;
    if (!t->busy || v.seq != t->seq) {                          /* 지난 메시지의 ACK (중복/지연) → 무시 */
        t->stale_acks++;
        log_ev(t->log, now, EV_ACK_STALE, v.seq, t->rep.attempts, 0);
        return;
    }
    if (t->rep.attempts == 1 && t->rep.refused == 0)            /* Karn: 재전송한 메시지의 RTT 는 모호 → 샘플 안 함 */
        rtt_sample(t, now - t->t_last_tx);
    log_ev(t->log, now, EV_ACK_OK, v.seq, t->rep.attempts, now - t->rep.t_start);
    tx_finish(t, now, ARQ_OK);
}

/* ---- Part A: 주기적으로 (매 틱) 부른다: 타이머 검사 + 다음 메시지 시작 ---- */
static void arq_tx_poll(arq_tx *t, uint32_t now) {
    if (t->busy && (int32_t)(now - t->deadline) >= 0) {
        log_ev(t->log, now, EV_TIMEOUT, t->seq, t->rep.attempts, 0);
        if (t->rep.attempts >= (uint32_t)t->cfg.max_retries + 1u) {
            tx_finish(t, now, t->rep.refused == t->rep.attempts ? ARQ_FAIL_LINKDOWN : ARQ_FAIL_TIMEOUT);
        } else {
            if (t->cfg.bo == BO_ADAPTIVE)                       /* RFC 6298 5.5: 타임아웃마다 RTO 두 배 (상한) */
                t->rto = t->rto > t->cfg.rto_cap / 2u ? t->cfg.rto_cap : t->rto * 2u;
            tx_transmit(t, now);
        }
    }
    if (!t->busy && t->qn) {
        txq_item *it = &t->q[t->qh];
        t->qh = (uint8_t)((t->qh + 1u) % ARQ_TXQ); t->qn--;
        t->frame_n = (uint8_t)frame_data(t->frame, t->seq, it->pl, it->len);
        t->tag = it->tag;
        memset(&t->rep, 0, sizeof t->rep);
        t->rep.t_submit = it->t_submit; t->rep.t_start = now;
        t->busy = true;
        tx_transmit(t, now);
    }
}

/* ---------------- 수신기 ---------------- */
typedef void (*arq_deliver_fn)(void *ctx, uint32_t now, const uint8_t *pl, uint8_t len);
typedef struct {
    uint8_t seq_bits, last;                     /* last = 마지막으로 전달한 seq */
    channel *link; evlog *log;
    arq_deliver_fn deliver; void *ctx;
    uint32_t delivered, dup_data, corrupt_dropped, acks_sent;
} arq_rx;

static void arq_rx_init(arq_rx *r, uint8_t seq_bits, channel *link, arq_deliver_fn fn, void *ctx, evlog *log) {
    memset(r, 0, sizeof *r);
    r->seq_bits = seq_bits; r->last = seq_mask(seq_bits);    /* "−1": 첫 seq 0 이 d = 1 */
    r->link = link; r->deliver = fn; r->ctx = ctx; r->log = log;
}

/* ---- Part A: 새 메시지인가? d = (seq − last) mod 2^b 가 1..2^(b−1) 이면 새 것 ---- */
static bool seq_is_new(uint8_t seq, uint8_t last, uint8_t bits) {
    uint8_t d = (uint8_t)((seq - last) & seq_mask(bits));
    return d >= 1u && d <= (1u << (bits - 1u));
}

/* ---- Part A: 수신 쪽이 받은 프레임 (DATA) ---- */
static void arq_rx_on_frame(arq_rx *r, uint32_t now, const uint8_t *b, size_t n) {
    frame_view v;
    if (frame_parse(b, n, &v) != 0 || v.seq > seq_mask(r->seq_bits)) {
        r->corrupt_dropped++;                                   /* 손상: ACK 도 안 보낸다 (누구 것인지 모름) */
        log_ev(r->log, now, EV_RX_CORRUPT, 0, 0, 0);
        return;
    }
    if (v.type != FT_DATA) return;
    if (seq_is_new(v.seq, r->last, r->seq_bits)) {              /* 새 메시지: 정확히 한 번 전달 */
        r->last = v.seq; r->delivered++;
        log_ev(r->log, now, EV_RX_DELIVER, v.seq, 0, v.len);
        if (r->deliver) r->deliver(r->ctx, now, v.pl, v.len);
    } else {                                                    /* 중복: 전달하지 않는다 */
        r->dup_data++;
        log_ev(r->log, now, EV_RX_DUP, v.seq, 0, 0);
    }
    uint8_t a[4];                                               /* 둘 다 ACK — 앞의 ACK 가 사라졌을 수 있다 */
    if (chan_send(r->link, now, a, frame_ack(a, v.seq)) == LINK_OK) r->acks_sent++;
}

/* ---- Part B: 링크 지연 범위 [dmin, dmax] (fwd/rev) 에서 안전한 최소 seq 비트 수 (8 로 부족하면 0) ----
 * RTTmin = dmin_f + dmin_r, RTO ≥ RTTmin 가정. 송신은 seq 하나를 끝내는 데 RTTmin 이상 걸린다.
 *   수신: 낡은 DATA 는 last 보다 최대 Kr = ceil((dmax_f − dmin_f)/RTTmin) 뒤 → Kr < 2^(b−1) 이어야 "중복" 판정
 *   송신: 낡은 ACK 는 지금 seq 보다 최대 Ks = ceil((dmax_f + dmax_r)/RTTmin) 뒤 → Ks < 2^b 이어야 겹치지 않음 */
static uint8_t seq_bits_required(uint32_t dmin_f, uint32_t dmax_f, uint32_t dmin_r, uint32_t dmax_r) {
    uint32_t rtt = dmin_f + dmin_r;
    if (rtt == 0) return 0;
    uint32_t kr = (dmax_f - dmin_f + rtt - 1u) / rtt;
    uint32_t ks = (dmax_f + dmax_r + rtt - 1u) / rtt;
    for (uint8_t b = 1; b <= 8; b++)
        if (kr < (1u << (b - 1u)) && ks < (1u << b)) return b;
    return 0;
}

/* ======================= 시뮬레이션 하네스 (주어짐) ======================= */
#define MSG_MAX 20000u
#define PL_LEN  8u
typedef struct world world;
typedef void (*tick_hook)(world *w, uint32_t now);
struct world {
    channel fwd, rev;                           /* fwd: DATA 송신→수신, rev: ACK 수신→송신 */
    arq_tx tx; arq_rx rx; evlog log;
    uint32_t n_msgs, submitted, completed, ticks;
    uint8_t  st[MSG_MAX], attempts[MSG_MAX], ndone[MSG_MAX], ndeliv[MSG_MAX];
    uint32_t t_start[MSG_MAX], t_deliv[MSG_MAX], t_done[MSG_MAX];
    int64_t  last_tag;
    uint32_t order_viol, bad_payload;
    tick_hook hook;
    void (*on_done)(world *w, uint32_t tag, const arq_report *r);         /* 선택: 완료 직후 */
    void (*rx_fn)(world *w, uint32_t now, const uint8_t *b, size_t n);    /* 선택: 수신 처리 교체 */
};
static world g_w;

static void make_payload(uint8_t *pl, uint32_t tag) {          /* [tag LE32][4바이트 패턴] — 전달 때 그대로 검사 */
    for (int i = 0; i < 4; i++) pl[i] = (uint8_t)(tag >> (8 * i));
    for (int i = 4; i < (int)PL_LEN; i++) pl[i] = (uint8_t)(tag * 31u + (uint32_t)i);
}
static void w_deliver(void *ctx, uint32_t now, const uint8_t *pl, uint8_t len) {
    world *w = ctx;
    uint32_t tag = (uint32_t)pl[0] | (uint32_t)pl[1] << 8 | (uint32_t)pl[2] << 16 | (uint32_t)pl[3] << 24;
    uint8_t ref[PL_LEN];
    make_payload(ref, tag);
    if (len != PL_LEN || tag >= w->n_msgs || memcmp(ref, pl, PL_LEN) != 0) { w->bad_payload++; return; }
    if (w->ndeliv[tag]++ == 0) w->t_deliv[tag] = now;
    if ((int64_t)tag <= w->last_tag) w->order_viol++; else w->last_tag = tag;
}
static void w_done(void *ctx, uint32_t tag, const arq_report *r) {
    world *w = ctx;
    if (tag >= w->n_msgs) return;
    w->ndone[tag]++; w->st[tag] = (uint8_t)(1 + r->st); w->attempts[tag] = r->attempts;
    w->t_start[tag] = r->t_start; w->t_done[tag] = r->t_done;
    w->completed++;
    if (w->on_done) w->on_done(w, tag, r);
}
static void w_init(world *w, const arq_cfg *cfg, const chan_cfg *f, const chan_cfg *r, uint32_t n_msgs, bool with_log) {
    memset(w, 0, sizeof *w);
    chan_init(&w->fwd, f, cfg->seed ^ 0x9E3779B9u);
    chan_init(&w->rev, r, cfg->seed ^ 0x7F4A7C15u);
    arq_tx_init(&w->tx, cfg, &w->fwd, w_done, w, with_log ? &w->log : NULL);
    arq_rx_init(&w->rx, cfg->seq_bits, &w->rev, w_deliver, w, with_log ? &w->log : NULL);
    w->n_msgs = n_msgs > MSG_MAX ? MSG_MAX : n_msgs;
    w->last_tag = -1;
}
/* 한 틱 = (1) 도착한 DATA → 수신기 (2) 도착한 ACK → 송신기 (3) 앱이 큐를 채움 (4) 송신기 타이머/시작 */
static uint32_t w_run(world *w, uint32_t max_ticks) {
    uint32_t now;
    for (now = 0; now < max_ticks; now++) {
        if (w->hook) w->hook(w, now);
        pkt p;
        while (chan_recv(&w->fwd, now, &p)) {
            if (w->rx_fn) w->rx_fn(w, now, p.b, p.n); else arq_rx_on_frame(&w->rx, now, p.b, p.n);
        }
        while (chan_recv(&w->rev, now, &p)) arq_tx_on_frame(&w->tx, now, p.b, p.n);
        while (w->submitted < w->n_msgs) {
            uint8_t pl[PL_LEN];
            make_payload(pl, w->submitted);
            if (arq_send(&w->tx, now, pl, PL_LEN, w->submitted) != 0) break;
            w->submitted++;
        }
        arq_tx_poll(&w->tx, now);
        if (w->completed == w->n_msgs && w->fwd.nq == 0 && w->rev.nq == 0) { now++; break; }  /* 링크가 빌 때까지 */
    }
    return w->ticks = now;
}

typedef struct { uint32_t ok, fail_to, fail_ld, silent_loss, dup_deliv, not_done, multi_done, fail_but_deliv, order_viol, bad_payload; } verdict;
static verdict w_verdict(const world *w) {
    verdict v; memset(&v, 0, sizeof v);
    for (uint32_t i = 0; i < w->n_msgs; i++) {
        if (w->ndone[i] == 0) v.not_done++;
        if (w->ndone[i] > 1) v.multi_done++;
        if (w->ndeliv[i] > 1) v.dup_deliv += w->ndeliv[i] - 1u;
        if (w->st[i] == 1 + ARQ_OK) { v.ok++; if (w->ndeliv[i] == 0) v.silent_loss++; }
        else if (w->st[i] == 1 + ARQ_FAIL_TIMEOUT) { v.fail_to++; if (w->ndeliv[i]) v.fail_but_deliv++; }
        else if (w->st[i] == 1 + ARQ_FAIL_LINKDOWN) { v.fail_ld++; if (w->ndeliv[i]) v.fail_but_deliv++; }
    }
    v.order_viol = w->order_viol; v.bad_payload = w->bad_payload;
    return v;
}
/* 신뢰성 불변식: OK 인데 안 받음 0, 두 번 받음 0, 순서 역전 0, 깨진 payload 0, 모든 메시지에 결과 정확히 한 번 */
static bool v_clean(verdict v) {
    return v.silent_loss == 0 && v.dup_deliv == 0 && v.order_viol == 0 && v.bad_payload == 0 &&
           v.not_done == 0 && v.multi_done == 0;
}
static const char *EVNAME[] = { "TX", "RETX", "TX refused", "TIMEOUT", "ACK ok", "ACK stale", "ACK corrupt",
                                "DONE ok", "FAIL timeout", "FAIL link-down", "RX deliver", "RX dup->reACK", "RX corrupt" };
static void log_print(const evlog *L) {
    for (uint32_t i = 0; i < L->n && i < LOG_CAP; i++) {
        const ev *e = &L->e[i];
        printf("  t=%4u  %-14s seq=%u", e->t, EVNAME[e->kind], e->seq);
        if (e->kind == EV_TX || e->kind == EV_RETX || e->kind == EV_TX_REFUSED) printf(" try=%u rto=%u", e->attempt, e->val);
        else if (e->kind == EV_ACK_OK) printf(" try=%u rtt_from_start=%u", e->attempt, e->val);
        else if (e->kind >= EV_DONE_OK && e->kind <= EV_FAIL_LINKDOWN) printf(" tries=%u corrupt_acks=%u", e->attempt, e->val);
        printf("\n");
    }
}

static const chan_cfg LINK_CLEAN = { 0, 0, 0, 0, 10, 0, 0 };   /* 지연 10 틱 고정, 결함 없음 */
static arq_cfg cfg_default(void) {
    arq_cfg c = { 8, 4, BO_FIXED, 30, 240, 20, 0, 1 };          /* 8비트 seq, 재전송 4, 고정 RTO 30 틱 */
    return c;
}

/* ======================= 테스트 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* 스크립트 결함 하나를 넣고 n 개 메시지를 돌린다 */
static verdict scen(const arq_cfg *cfg, uint32_t n, const link_script *fs, size_t nfs, const link_script *rs, size_t nrs) {
    w_init(&g_w, cfg, &LINK_CLEAN, &LINK_CLEAN, n, false);
    g_w.fwd.script = fs; g_w.fwd.nscript = nfs;
    g_w.rev.script = rs; g_w.rev.nscript = nrs;
    w_run(&g_w, 100000);
    return w_verdict(&g_w);
}
static void hook_up_at_45(world *w, uint32_t now) { w->fwd.up = now >= 45; }

int main(void) {
    arq_cfg C = cfg_default();
    verdict v;

    /* ---------- Part A: 프레임, 수신 규칙, 백오프, 깨끗한 링크 ---------- */
    CHECK(crc16((const uint8_t *)"123456789", 9) == 0x29B1);           /* CRC-16/CCITT-FALSE check 값 */
    {
        uint8_t f[FRAME_MAX]; frame_view fv;
        size_t n = frame_data(f, 5, (const uint8_t *)"hello", 5);
        CHECK(n == 10 && frame_parse(f, n, &fv) == 0 && fv.type == FT_DATA && fv.seq == 5 && fv.len == 5 && memcmp(fv.pl, "hello", 5) == 0);
        int undetected = 0;                                              /* 단일 비트 손상은 전부 잡힌다 */
        for (size_t bit = 0; bit < n * 8; bit++) { f[bit / 8] ^= (uint8_t)(1u << (bit % 8)); undetected += frame_parse(f, n, &fv) == 0; f[bit / 8] ^= (uint8_t)(1u << (bit % 8)); }
        CHECK(undetected == 0);
        n = frame_ack(f, 7);
        CHECK(n == 4 && frame_parse(f, n, &fv) == 0 && fv.type == FT_ACK && fv.seq == 7);
    }
    {
        uint32_t bad = 0;                                                /* A1 전수: 8가지 폭 × last × seq */
        for (uint8_t b = 1; b <= 8; b++)
            for (uint32_t last = 0; last <= seq_mask(b); last++)
                for (uint32_t s = 0; s <= seq_mask(b); s++) {
                    uint32_t d = (s - last) & seq_mask(b);
                    bad += seq_is_new((uint8_t)s, (uint8_t)last, b) != (d >= 1 && d <= (1u << (b - 1)));
                }
        CHECK(bad == 0);
        CHECK(seq_is_new(0, 1, 1) && !seq_is_new(1, 1, 1));            /* 1비트 = 교대 비트 */
        CHECK(seq_is_new(0, 255, 8) && !seq_is_new(3, 5, 8) && seq_is_new(133, 5, 8) && !seq_is_new(134, 5, 8));
    }
    {
        arq_tx t; arq_cfg c = C;
        arq_tx_init(&t, &c, NULL, NULL, NULL, NULL);
        CHECK(arq_timeout(&t, 1) == 30 && arq_timeout(&t, 5) == 30);
        c.bo = BO_EXP; arq_tx_init(&t, &c, NULL, NULL, NULL, NULL);
        CHECK(arq_timeout(&t, 1) == 30 && arq_timeout(&t, 2) == 60 && arq_timeout(&t, 3) == 120 &&
              arq_timeout(&t, 4) == 240 && arq_timeout(&t, 5) == 240 && arq_timeout(&t, 40) == 240);
        c.bo = BO_FIXED; c.jitter_pct = 20; arq_tx_init(&t, &c, NULL, NULL, NULL, NULL);
        uint32_t lo = 999, hi = 0;
        for (int i = 0; i < 1000; i++) { uint32_t T = arq_timeout(&t, 1); if (T < lo) lo = T; if (T > hi) hi = T; }
        CHECK(lo == 30 && hi == 36);                                     /* 30 + U[0, 6] */
        c.bo = BO_ADAPTIVE; c.jitter_pct = 0; arq_tx_init(&t, &c, NULL, NULL, NULL, NULL); t.rto = 77;
        CHECK(arq_timeout(&t, 3) == 77);
    }
    {
        w_init(&g_w, &C, &LINK_CLEAN, &LINK_CLEAN, 200, false);
        w_run(&g_w, 100000);
        v = w_verdict(&g_w);
        CHECK(v_clean(v) && v.ok == 200 && g_w.tx.tx_frames == 200 && g_w.tx.retx == 0 && g_w.rx.acks_sent == 200);
        CHECK(g_w.ticks == 4001);                                        /* 메시지당 RTT 20 틱, 빈틈 없음 */
    }

    /* ---------- Part B: 결함 하나씩 ---------- */
    {   /* B1 DATA 손실 → 재전송 1번, 중복 없음 */
        static const link_script f[] = { { 0, ACT_DROP, 0 } };
        v = scen(&C, 1, f, 1, NULL, 0);
        CHECK(v_clean(v) && v.ok == 1 && g_w.attempts[0] == 2 && g_w.rx.dup_data == 0 && g_w.t_deliv[0] == 40);
    }
    {   /* B2 ACK 손실 → 수신 쪽에 중복 DATA, 전달은 한 번, 다시 ACK */
        static const link_script r[] = { { 0, ACT_DROP, 0 } };
        v = scen(&C, 1, NULL, 0, r, 1);
        CHECK(v_clean(v) && v.ok == 1 && g_w.ndeliv[0] == 1 && g_w.rx.dup_data == 1 && g_w.rx.acks_sent == 2 && g_w.attempts[0] == 2);
    }
    {   /* B3 ACK 지연 > RTO → 쓸데없는 재전송; 늦은 ACK 가 먼저 끝내고, 재전송분의 ACK 는 stale */
        static const link_script r[] = { { 0, ACT_DELAY, 25 } };
        v = scen(&C, 2, NULL, 0, r, 1);
        CHECK(v_clean(v) && v.ok == 2 && g_w.tx.retx == 1 && g_w.rx.dup_data == 1 && g_w.tx.stale_acks == 1);
        CHECK(g_w.t_done[0] == 45 && g_w.tx.rtt_samples == 1);         /* Karn: 첫 메시지는 RTT 샘플 없음 */
    }
    {   /* B4 링크가 DATA 를 복제 → 중복 한 번, ACK 도 두 개 → 하나는 stale */
        static const link_script f[] = { { 0, ACT_DUP, 3 } };
        v = scen(&C, 2, f, 1, NULL, 0);
        CHECK(v_clean(v) && v.ok == 2 && g_w.rx.dup_data == 1 && g_w.tx.stale_acks == 1 && g_w.tx.retx == 0);
    }
    {   /* B5 손상 → CRC 실패 → 버림 (= 손실처럼 동작) + 카운터 */
        static const link_script f[] = { { 0, ACT_CORRUPT, 13 } };
        static const link_script r[] = { { 0, ACT_CORRUPT, 5 } };
        v = scen(&C, 1, f, 1, r, 1);
        CHECK(v_clean(v) && v.ok == 1 && g_w.rx.corrupt_dropped == 1 && g_w.tx.corrupt_dropped == 1 && g_w.attempts[0] == 3);
        CHECK(g_w.ndeliv[0] == 1 && g_w.rx.dup_data == 1);
    }
    {   /* B6 전부 손실 → 정확히 max_retries+1 = 5 번 보내고 TIMEOUT_EXHAUSTED */
        arq_cfg c = C;
        chan_cfg dead = LINK_CLEAN; dead.loss_pm = 1000;
        w_init(&g_w, &c, &dead, &LINK_CLEAN, 1, false); w_run(&g_w, 100000); v = w_verdict(&g_w);
        CHECK(v.fail_to == 1 && g_w.fwd.sent == 5 && g_w.attempts[0] == 5 && g_w.t_done[0] == 150 && g_w.ndeliv[0] == 0);
        c.bo = BO_EXP;                                                  /* 30+60+120+240+240 */
        w_init(&g_w, &c, &dead, &LINK_CLEAN, 1, false); w_run(&g_w, 100000); v = w_verdict(&g_w);
        CHECK(v.fail_to == 1 && g_w.fwd.sent == 5 && g_w.t_done[0] == 690);
    }
    {   /* B7 ACK 가 전부 손상 → FAIL 인데 수신 쪽은 이미 받았다 ("실패 ≠ 안 감") */
        chan_cfg bad = LINK_CLEAN; bad.corrupt_pm = 1000;
        w_init(&g_w, &C, &LINK_CLEAN, &bad, 1, false); w_run(&g_w, 100000); v = w_verdict(&g_w);
        CHECK(v.fail_to == 1 && v.fail_but_deliv == 1 && g_w.ndeliv[0] == 1 && g_w.rx.dup_data == 4);
        CHECK(g_w.tx.corrupt_dropped == 5 && g_w.tx.n_fail_timeout == 1);
    }
    {   /* B8 링크 down → 보낼 때마다 거부 → LINK_DOWN (링크에는 0 개) */
        w_init(&g_w, &C, &LINK_CLEAN, &LINK_CLEAN, 1, false); g_w.fwd.up = false; w_run(&g_w, 100000); v = w_verdict(&g_w);
        CHECK(v.fail_ld == 1 && g_w.fwd.sent == 0 && g_w.fwd.refused == 5 && g_w.t_done[0] == 150);
        /* down 이었다가 t=45 에 up → 세 번째 시도에서 성공 */
        w_init(&g_w, &C, &LINK_CLEAN, &LINK_CLEAN, 1, false); g_w.hook = hook_up_at_45; w_run(&g_w, 100000); v = w_verdict(&g_w);
        CHECK(v.ok == 1 && g_w.attempts[0] == 3 && g_w.fwd.refused == 2);
        /* 거부 2번 + 손실 3번 → 섞였으면 TIMEOUT_EXHAUSTED (링크가 살아 있던 적이 있다) */
        chan_cfg dead = LINK_CLEAN; dead.loss_pm = 1000;
        w_init(&g_w, &C, &dead, &LINK_CLEAN, 1, false); g_w.hook = hook_up_at_45; w_run(&g_w, 100000); v = w_verdict(&g_w);
        CHECK(v.fail_to == 1 && g_w.fwd.refused == 2 && g_w.fwd.sent == 3);
    }
    {   /* B9 FAIL 뒤 다음 메시지: seq 를 넘겨도 두 경우 모두 정확히 한 번 전달 */
        static const link_script f[] = { { 0, ACT_DROP, 0 }, { 1, ACT_DROP, 0 }, { 2, ACT_DROP, 0 }, { 3, ACT_DROP, 0 }, { 4, ACT_DROP, 0 } };
        v = scen(&C, 2, f, 5, NULL, 0);                                /* (a) DATA 경로가 죽음: msg0 못 받음 */
        CHECK(v_clean(v) && v.fail_to == 1 && v.ok == 1 && g_w.ndeliv[0] == 0 && g_w.ndeliv[1] == 1);
        v = scen(&C, 2, NULL, 0, f, 5);                                /* (b) ACK 경로가 죽음: msg0 이미 받음 */
        CHECK(v_clean(v) && v.fail_to == 1 && v.ok == 1 && g_w.ndeliv[0] == 1 && g_w.ndeliv[1] == 1);
    }
    {   /* B10 고전 버그: 1비트 seq + RTO 보다 오래 늦는 ACK → 옛 ACK 가 새 DATA 를 "확인" → 조용한 손실 */
        static const link_script f[] = { { 3, ACT_DROP, 0 } };          /* C 의 첫 DATA 손실 */
        static const link_script r[] = { { 0, ACT_DELAY, 70 } };        /* A 의 첫 ACK 가 t=90 에 도착 */
        arq_cfg c = C; c.seq_bits = 1;
        w_init(&g_w, &c, &LINK_CLEAN, &LINK_CLEAN, 5, true);          /* 로그 켜고 직접 실행 (그림 3 의 원본) */
        g_w.fwd.script = f; g_w.fwd.nscript = 1; g_w.rev.script = r; g_w.rev.nscript = 1;
        w_run(&g_w, 100000); v = w_verdict(&g_w);
        printf("B10 event log, 1-bit seq:\n"); log_print(&g_w.log);
        printf("B10 1-bit seq: ok=%u silent_loss=%u (msg2 delivered %u, msg3 delivered %u) dup_deliv=%u\n",
               v.ok, v.silent_loss, g_w.ndeliv[2], g_w.ndeliv[3], v.dup_deliv);
        CHECK(v.ok == 5 && v.silent_loss == 2 && g_w.ndeliv[2] == 0 && g_w.ndeliv[3] == 0 && g_w.ndeliv[4] == 1);
        c.seq_bits = 8;
        v = scen(&c, 5, f, 1, r, 1);
        CHECK(v_clean(v) && v.ok == 5 && g_w.tx.stale_acks >= 1);
    }
    {   /* B11 seq 폭 하한 */
        CHECK(seq_bits_required(10, 10, 10, 10) == 1);                  /* 고정 지연 = FIFO → 교대 비트로 충분 */
        CHECK(seq_bits_required(10, 40, 10, 40) == 3);
        CHECK(seq_bits_required(10, 160, 10, 160) == 5);
        CHECK(seq_bits_required(1, 1000, 1, 1000) == 0);                /* 8비트로 부족 */
        /* 실제로: 지연 10..40 (양방향), 손실 없음, RTO 25 (쓸데없는 재전송 많음) */
        chan_cfg jit = { 0, 0, 0, 0, 10, 30, 0 };
        arq_cfg c = C; c.rto_base = 25; c.max_retries = 30;
        uint32_t viol[9] = { 0 };
        for (uint8_t b = 1; b <= 3; b++) {
            c.seq_bits = b; c.seed = 7;
            w_init(&g_w, &c, &jit, &jit, 3000, false); w_run(&g_w, 2000000); v = w_verdict(&g_w);
            viol[b] = v.silent_loss + v.dup_deliv + v.order_viol;
            printf("B11 jitter 10..40, seq_bits=%u: ok=%u silent_loss=%u dup_deliv=%u order_viol=%u retx=%u\n",
                   b, v.ok, v.silent_loss, v.dup_deliv, v.order_viol, g_w.tx.retx);
        }
        CHECK(viol[1] > 0);
        CHECK(viol[3] == 0 && v.ok == 3000);                            /* seq_bits_required(...) == 3 → 깨끗 */
    }
    {   /* B12 무작위 결함 스트레스 (고정 시드): 손실·중복·손상·지터·늦은 패킷 + FAIL 섞임 */
        struct { chan_cfg f, r; backoff_kind bo; uint32_t base, cap; uint8_t jit, retries; uint32_t n; } S[] = {
            { { 100, 50, 50, 0, 10, 0, 0 },   { 100, 50, 50, 0, 10, 0, 0 },   BO_FIXED,    30, 240,  0,  6, 3000 },
            { { 300, 20, 20, 0, 10, 15, 0 },  { 300, 20, 20, 0, 10, 15, 0 },  BO_EXP,      40, 320, 25,  8, 3000 },
            { { 50, 10, 10, 20, 10, 10, 150 },{ 50, 10, 10, 20, 10, 10, 150 },BO_ADAPTIVE, 100, 2000, 0, 10, 3000 },
            { { 500, 0, 0, 0, 10, 0, 0 },     { 500, 0, 0, 0, 10, 0, 0 },     BO_FIXED,    30, 240,  0,  3, 3000 },
        };
        for (size_t i = 0; i < sizeof S / sizeof S[0]; i++) {
            arq_cfg c = C; c.bo = S[i].bo; c.rto_base = S[i].base; c.rto_cap = S[i].cap; c.jitter_pct = S[i].jit;
            c.max_retries = S[i].retries; c.seed = 1000u + (uint32_t)i;
            w_init(&g_w, &c, &S[i].f, &S[i].r, S[i].n, false); w_run(&g_w, 20000000); v = w_verdict(&g_w);
            printf("B12 stress %zu: ok=%u fail=%u (fail_but_delivered=%u) retx=%u rx_dup=%u corrupt rx/tx=%u/%u stale_acks=%u ticks=%u\n",
                   i, v.ok, v.fail_to + v.fail_ld, v.fail_but_deliv, g_w.tx.retx, g_w.rx.dup_data,
                   g_w.rx.corrupt_dropped, g_w.tx.corrupt_dropped, g_w.tx.stale_acks, g_w.ticks);
            CHECK(v_clean(v) && v.ok + v.fail_to + v.fail_ld == S[i].n);
        }
    }

    /* ---------- Part C: RTT 추정 + Karn, API, 로그 ---------- */
    {
        arq_tx t; arq_cfg c = C; c.bo = BO_ADAPTIVE; c.rto_base = 100; c.rto_min = 20; c.rto_cap = 2000;
        arq_tx_init(&t, &c, NULL, NULL, NULL, NULL);
        rtt_sample(&t, 50); CHECK(t.rto == 150);                        /* 50 + 4·25 */
        rtt_sample(&t, 50); CHECK(t.rto == 125);                        /* RTTVAR = 18.75 → 50 + 75 */
        rtt_sample(&t, 90); CHECK(t.srtt8 >> 3 == 55 && t.rto == 152); /* RFC 실수 계산 151.25, 정수 스케일 152 */
        arq_tx_init(&t, &c, NULL, NULL, NULL, NULL);
        rtt_sample(&t, 2); CHECK(t.rto == 20);                          /* 하한 rto_min */
    }
    {   /* Karn + 타임아웃 두 배: 첫 메시지(DATA 손실)는 샘플 없이 RTO 100→200, 다음 메시지 샘플 20 → RTO 60 */
        static const link_script f[] = { { 0, ACT_DROP, 0 } };
        arq_cfg c = C; c.bo = BO_ADAPTIVE; c.rto_base = 100; c.rto_min = 20; c.rto_cap = 2000;
        w_init(&g_w, &c, &LINK_CLEAN, &LINK_CLEAN, 1, false); g_w.fwd.script = f; g_w.fwd.nscript = 1;
        w_run(&g_w, 100000);
        CHECK(g_w.st[0] == 1 + ARQ_OK && g_w.tx.rtt_samples == 0 && g_w.tx.rto == 200 && g_w.t_done[0] == 120);
        w_init(&g_w, &c, &LINK_CLEAN, &LINK_CLEAN, 2, false); g_w.fwd.script = f; g_w.fwd.nscript = 1;
        w_run(&g_w, 100000);
        CHECK(g_w.tx.rtt_samples == 1 && g_w.tx.rto == 60);
        /* 고정 RTT 50 에서 수렴: 정수 RTTVAR 는 3 에서 멈춘다 → RTO 53 */
        chan_cfg l25 = LINK_CLEAN; l25.delay = 25;
        w_init(&g_w, &c, &l25, &l25, 60, false); w_run(&g_w, 100000);
        CHECK(g_w.tx.rtt_samples == 60 && (g_w.tx.srtt8 >> 3) == 50 && g_w.tx.rttvar4 == 3 && g_w.tx.rto == 53);
    }
    {   /* 지터 큰 링크 (RTT 40..160): 고정 RTO 60 vs 적응형 — 쓸데없는 재전송(= 수신 중복) 비교 */
        chan_cfg jit = { 0, 0, 0, 0, 20, 60, 0 };
        arq_cfg c = C; c.max_retries = 20; c.rto_base = 60;
        w_init(&g_w, &c, &jit, &jit, 2000, false); w_run(&g_w, 5000000);
        verdict vf = w_verdict(&g_w); uint32_t dup_fixed = g_w.rx.dup_data, t_fixed = g_w.ticks;
        c.bo = BO_ADAPTIVE; c.rto_base = 300; c.rto_min = 20; c.rto_cap = 4000;
        w_init(&g_w, &c, &jit, &jit, 2000, false); w_run(&g_w, 5000000);
        verdict va = w_verdict(&g_w); uint32_t dup_ad = g_w.rx.dup_data;
        printf("C  jitter RTT 40..160: fixed RTO 60 -> spurious(rx dup)=%u ticks=%u | adaptive -> spurious=%u ticks=%u final rto=%u srtt=%d\n",
               dup_fixed, t_fixed, dup_ad, g_w.ticks, g_w.tx.rto, g_w.tx.srtt8 >> 3);
        CHECK(v_clean(vf) && v_clean(va) && vf.ok == 2000 && va.ok == 2000);
        CHECK(dup_ad * 10 < dup_fixed);
    }
    {   /* API: 큐 4 칸, payload 16 바이트 */
        arq_tx t; arq_tx_init(&t, &C, NULL, NULL, NULL, NULL);
        uint8_t pl[ARQ_MAX_PAYLOAD + 1] = { 0 };
        int r[5]; for (int i = 0; i < 5; i++) r[i] = arq_send(&t, 0, pl, 8, (uint32_t)i);
        CHECK(r[0] == 0 && r[3] == 0 && r[4] == ARQ_EQUEUE_FULL);
        CHECK(arq_send(&t, 0, pl, ARQ_MAX_PAYLOAD + 1, 9) == ARQ_EMSGSIZE);
    }
    {   /* 이벤트 로그: ACK 하나 손실 */
        static const link_script r[] = { { 0, ACT_DROP, 0 } };
        w_init(&g_w, &C, &LINK_CLEAN, &LINK_CLEAN, 1, true); g_w.rev.script = r; g_w.rev.nscript = 1;
        w_run(&g_w, 100000);
        printf("C  event log, lost ACK:\n"); log_print(&g_w.log);
        static const uint8_t want[] = { EV_TX, EV_RX_DELIVER, EV_TIMEOUT, EV_RETX, EV_RX_DUP, EV_ACK_OK, EV_DONE_OK };
        bool same = g_w.log.n == sizeof want;
        for (uint32_t i = 0; same && i < g_w.log.n; i++) same = g_w.log.e[i].kind == want[i];
        CHECK(same);
        /* 링크 down 로그: 거부 5번 → FAIL link-down */
        w_init(&g_w, &C, &LINK_CLEAN, &LINK_CLEAN, 1, true); g_w.fwd.up = false; w_run(&g_w, 100000);
        printf("C  event log, link down:\n"); log_print(&g_w.log);
        CHECK(g_w.log.n == 11 && g_w.log.e[10].kind == EV_FAIL_LINKDOWN);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
