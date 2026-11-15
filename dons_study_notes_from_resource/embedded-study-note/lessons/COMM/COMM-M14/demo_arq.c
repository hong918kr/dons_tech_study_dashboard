/* COMM-M14 demo_arq.c — 측정: 손실률별 goodput·지연 (FIXED vs EXP, 이론 비교), seq 폭 스윕,
 * 링크 단절(outage) 동안의 실패 분류·복구 지연, 지터 링크에서 고정 RTO vs 적응형 RTO.
 * 프로토콜·링크 코드는 solution.c 와 같다 (그대로 복사). 고정 시드라 매번 같은 숫자가 나온다.
 * 빌드: cc -std=c11 -Wall -Wextra -O2 demo_arq.c -o demo_arq && ./demo_arq
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

/* ======================= 측정 ======================= */
#include <stdlib.h>
#include <math.h>

static int cmp_u32(const void *a, const void *b) { uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b; return (x > y) - (x < y); }
static uint32_t g_lat[MSG_MAX];

typedef struct { double goodput, frames, mean_lat; uint32_t p99, fails, n_deliv; } meas;
static meas measure(const world *w) {
    meas m; memset(&m, 0, sizeof m);
    verdict v = w_verdict(w);
    uint32_t k = 0; double sum = 0;
    for (uint32_t i = 0; i < w->n_msgs; i++)
        if (w->ndeliv[i]) { g_lat[k] = w->t_deliv[i] - w->t_start[i]; sum += g_lat[k]; k++; }
    qsort(g_lat, k, sizeof g_lat[0], cmp_u32);
    m.n_deliv = k;
    m.mean_lat = k ? sum / k : 0;
    m.p99 = k ? g_lat[(uint32_t)ceil(0.99 * k) - 1] : 0;
    m.goodput = 1000.0 * v.ok / w->ticks;
    m.frames = (double)w->tx.tx_frames / w->n_msgs;
    m.fails = v.fail_to + v.fail_ld;
    if (!v_clean(v)) printf("!! invariant violated\n");
    return m;
}

/* 이론: 손실 p (양방향 독립), 지연 d 고정, RTT = 2d, 타임아웃 T_k, 전송 최대 M 번 */
typedef struct { double goodput, frames, mean_lat; double p99; } theo;
static theo theory(double p, uint32_t d, const uint32_t *T, int M) {
    theo r; double q = (1 - p) * (1 - p), RTT = 2.0 * d;
    double S[64]; S[0] = 0; for (int k = 1; k <= M; k++) S[k] = S[k - 1] + T[k];
    double Ecyc = 0, Efr = 0, fail = pow(1 - q, M);
    for (int n = 1; n <= M; n++) { double P = q * pow(1 - q, n - 1); Ecyc += P * (S[n - 1] + RTT); Efr += P * n; }
    Ecyc += fail * S[M]; Efr += fail * M;
    r.goodput = 1000.0 * (1 - fail) / Ecyc; r.frames = Efr;
    double Pd = 1 - pow(p, M), mean = 0, cum = 0; r.p99 = -1;
    for (int j = 1; j <= M; j++) {
        double P = pow(p, j - 1) * (1 - p), lat = S[j - 1] + d;
        mean += P * lat; cum += P;
        if (r.p99 < 0 && cum / Pd >= 0.99 - 1e-12) r.p99 = lat;
    }
    r.mean_lat = mean / Pd;
    return r;
}

static void hook_outage(world *w, uint32_t now) { bool up = now < 5000 || now >= 8000; w->fwd.up = up; w->rev.up = up; }

int main(void) {
    (void)log_print;                                            /* 로그는 이 데모에서 안 쓴다 */
    const uint32_t N = 20000;
    /* ---------- 1. 손실률 스윕 ---------- */
    printf("== 1. loss sweep: delay 10 each way (RTT 20), N=%u msgs, max_retries 15 (M=16), loss p on DATA and ACK ==\n", N);
    printf("   FIXED T=30 | EXP T=30·2^(k-1) cap 240.  goodput = OK msgs per 1000 ticks (ideal 1000/RTT = 50)\n");
    printf("  p    | FIXED goodput meas/theory  frames/msg meas/theory | EXP goodput meas/theory  frames/msg | fails F/E\n");
    meas MF[11], ME[11]; theo TF[11], TE[11];
    for (int i = 0; i <= 10; i++) {
        double p = 0.05 * i;
        uint32_t Tf[64], Te[64];
        for (int k = 1; k <= 16; k++) { Tf[k] = 30; Te[k] = 30u << (k - 1 < 3 ? k - 1 : 3); }
        chan_cfg l = LINK_CLEAN; l.loss_pm = (uint16_t)(50 * i);
        arq_cfg c = cfg_default(); c.max_retries = 15; c.seed = 100u + (uint32_t)i;
        w_init(&g_w, &c, &l, &l, N, false); w_run(&g_w, 400000000u); MF[i] = measure(&g_w);
        c.bo = BO_EXP;
        w_init(&g_w, &c, &l, &l, N, false); w_run(&g_w, 400000000u); ME[i] = measure(&g_w);
        TF[i] = theory(p, 10, Tf, 16); TE[i] = theory(p, 10, Te, 16);
        printf("  %.2f | %6.2f / %6.2f   %5.2f / %5.2f              | %6.2f / %6.2f   %5.2f / %5.2f | %u / %u\n",
               p, MF[i].goodput, TF[i].goodput, MF[i].frames, TF[i].frames, ME[i].goodput, TE[i].goodput,
               ME[i].frames, TE[i].frames, MF[i].fails, ME[i].fails);
    }
    printf("\n  p    | FIXED latency(first TX->deliver) mean meas/theory  p99 meas/theory | EXP mean meas/theory  p99 meas/theory\n");
    for (int i = 0; i <= 10; i++)
        printf("  %.2f | %7.1f / %7.1f   %5u / %5.0f | %7.1f / %7.1f   %5u / %5.0f\n", 0.05 * i,
               MF[i].mean_lat, TF[i].mean_lat, MF[i].p99, TF[i].p99, ME[i].mean_lat, TE[i].mean_lat, ME[i].p99, TE[i].p99);

    /* ---------- 2. seq 폭 스윕 ---------- */
    printf("\n== 2. seq_bits sweep: no loss, RTO 25 fixed (many spurious retransmissions), N=%u ==\n", N);
    struct { const char *name; chan_cfg l; } L[] = {
        { "delay 10..40   ", { 0, 0, 0, 0, 10, 30, 0 } },
        { "delay 10..110  ", { 0, 0, 0, 0, 10, 100, 0 } },
        { "10..20 +2% +300", { 0, 0, 0, 20, 10, 10, 300 } },
    };
    for (size_t li = 0; li < sizeof L / sizeof L[0]; li++) {
        chan_cfg l = L[li].l;
        uint32_t dmax = l.delay + l.jitter + (l.late_pm ? l.late_extra : 0);
        printf("  %s required=%u :", L[li].name, seq_bits_required(l.delay, dmax, l.delay, dmax));
        for (uint8_t b = 1; b <= 6; b++) {
            arq_cfg c = cfg_default(); c.rto_base = 25; c.max_retries = 100; c.seq_bits = b; c.seed = 7;
            w_init(&g_w, &c, &l, &l, N, false); w_run(&g_w, 400000000u);
            verdict v = w_verdict(&g_w);
            printf("  b=%u:%u", b, v.silent_loss + v.dup_deliv + v.order_viol);
            if (g_w.fwd.overflow || g_w.rev.overflow) printf("(ovf)");
        }
        printf("   (violations = silent loss + dup delivery + reorder)\n");
    }

    /* ---------- 3. 링크 단절 ---------- */
    printf("\n== 3. outage: both directions down for t in [5000, 8000), 1000 msgs, delay 10, max_retries 6 ==\n");
    printf("  policy            | FAIL link-down  FAIL timeout | refused TX during outage | 1st delivery after up | total ticks\n");
    for (int pol = 0; pol < 3; pol++) {
        arq_cfg c = cfg_default(); c.max_retries = 6;
        if (pol >= 1) c.bo = BO_EXP;
        if (pol == 2) c.jitter_pct = 25;
        w_init(&g_w, &c, &LINK_CLEAN, &LINK_CLEAN, 1000, false); g_w.hook = hook_outage; w_run(&g_w, 10000000u);
        verdict v = w_verdict(&g_w);
        uint32_t first = UINT32_MAX;
        for (uint32_t i = 0; i < g_w.n_msgs; i++) if (g_w.ndeliv[i] && g_w.t_deliv[i] >= 8000 && g_w.t_deliv[i] < first) first = g_w.t_deliv[i];
        printf("  %-17s | %14u  %12u | %24u | %+20d | %u%s\n",
               pol == 0 ? "FIXED 30" : pol == 1 ? "EXP 30..240" : "EXP 30..240 +25%",
               v.fail_ld, v.fail_to, g_w.fwd.refused, (int)(first - 8000), g_w.ticks, v_clean(v) ? "" : " !!");
    }

    /* ---------- 4. 지터 링크: 고정 RTO vs 적응형 ---------- */
    printf("\n== 4. jittery link delay 20..80 each way (RTT 40..160), 5000 msgs, max_retries 20 ==\n");
    printf("  loss | policy        | goodput  frames/msg  rx dup DATA  mean lat  p99 lat\n");
    for (int li = 0; li < 3; li++) {
        uint16_t loss = li == 0 ? 0 : li == 1 ? 100 : 300;
        for (int pol = 0; pol < 3; pol++) {
            chan_cfg l = { loss, 0, 0, 0, 20, 60, 0 };
            arq_cfg c = cfg_default(); c.max_retries = 20; c.seed = 55;
            if (pol == 0) c.rto_base = 60;
            if (pol == 1) c.rto_base = 200;
            if (pol == 2) { c.bo = BO_ADAPTIVE; c.rto_base = 300; c.rto_min = 20; c.rto_cap = 4000; }
            w_init(&g_w, &c, &l, &l, 5000, false); w_run(&g_w, 400000000u);
            meas m = measure(&g_w);
            printf("  %3u%% | %-13s | %7.2f  %10.2f  %11u  %8.1f  %7u\n", loss / 10,
                   pol == 0 ? "FIXED 60" : pol == 1 ? "FIXED 200" : "ADAPTIVE", m.goodput, m.frames, g_w.rx.dup_data, m.mean_lat, m.p99);
        }
    }
    return 0;
}
