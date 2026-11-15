/* COMM-M14 practice.c — 추가 연습 한 번에 검증
 *   연습 1: 이론식 (goodput, 전송 횟수, 평균 전달 지연) 계산기 vs 시뮬레이션
 *   연습 2: 앱 계층 멱등성 — FAIL 뒤 같은 op_id 로 다시 보내도 효과는 한 번 (DIST-B05)
 *   연습 3: 적응형 RTO 의 백오프를 "성공하면 리셋" — 랜덤 손실 링크에서 Karn 유지 정책과 비교
 *   연습 4: 수신기 재부팅 → seq 상태 상실 → 조용한 손실 56 개, "첫 프레임에 동기" 로 고치기 + 남는 중복 위험
 * 프로토콜·링크·하네스 코드는 solution.c 와 같다 (그대로 복사, 자체 완결).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
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

/* ======================= 추가 연습 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---------- 연습 1: 이론식 ----------
 * 손실 p (DATA·ACK 독립), 지연 d 고정 (RTT = 2d < T_1), 타임아웃 T[1..M], 전송 최대 M 번.
 * 시도 하나가 성공 = DATA 도착 ∧ ACK 도착 → q = (1−p)^2. S_k = T_1 + … + T_k.
 *   한 메시지의 주기 = n 번째 시도 성공: S_{n−1} + RTT,  전부 실패: S_M
 *   goodput = P(성공) / E[주기]  (renewal-reward),  E[전송] = Σ n·q(1−q)^(n−1) + M(1−q)^M
 *   첫 전송 → 전달 지연 = S_{j−1} + d, j = 처음 DATA 가 도착한 시도, P(j) = p^(j−1)(1−p)  (전달된 것만 조건부) */
typedef struct { double goodput, frames, mean_lat; } th;
static double powi(double x, int n) { double r = 1; while (n-- > 0) r *= x; return r; }
static th theory(double p, uint32_t d, const uint32_t *T, int M) {
    th r; double q = (1 - p) * (1 - p), S = 0, Ecyc = 0, Efr = 0, lat = 0;
    for (int n = 1; n <= M; n++) {
        double Pn = q * powi(1 - q, n - 1), Pj = powi(p, n - 1) * (1 - p);
        Ecyc += Pn * (S + 2.0 * d); Efr += Pn * n; lat += Pj * (S + d);
        S += T[n];
    }
    double fail = powi(1 - q, M);
    Ecyc += fail * S; Efr += fail * M;
    r.goodput = 1000.0 * (1 - fail) / Ecyc; r.frames = Efr; r.mean_lat = lat / (1 - powi(p, M));
    return r;
}
static double rel(double a, double b) { double e = (a - b) / b; return e < 0 ? -e : e; }

/* ---------- 연습 2: 앱 계층 멱등성 ---------- */
typedef struct { uint32_t recent[16]; uint8_t n, head; uint32_t arq_deliveries, applied, ignored; int32_t value; } app_rx;
static void app_deliver(void *ctx, uint32_t now, const uint8_t *pl, uint8_t len) {
    (void)now;
    app_rx *a = ctx;
    if (len != 5) return;
    uint32_t op = (uint32_t)pl[0] | (uint32_t)pl[1] << 8 | (uint32_t)pl[2] << 16 | (uint32_t)pl[3] << 24;
    a->arq_deliveries++;
    for (uint8_t i = 0; i < a->n; i++) if (a->recent[i] == op) { a->ignored++; return; }   /* 이미 적용한 op */
    a->value += pl[4]; a->applied++;
    a->recent[a->head] = op; a->head = (uint8_t)((a->head + 1u) % 16u); if (a->n < 16) a->n++;
}
typedef struct { arq_tx *tx; uint32_t resends; arq_status st[4]; uint8_t ndone[4]; } app_tx;
static void op_payload(uint8_t *pl, uint32_t op) { for (int i = 0; i < 4; i++) pl[i] = (uint8_t)(op >> (8 * i)); pl[4] = (uint8_t)(10 + op); }
static void app_done(void *ctx, uint32_t op, const arq_report *r) {
    app_tx *a = ctx;
    if (op < 4) { a->st[op] = r->st; a->ndone[op]++; }
    if (r->st != ARQ_OK && a->resends < 3) {                    /* FAIL = "모른다" → 같은 op_id 로 다시 */
        uint8_t pl[5]; op_payload(pl, op);
        if (arq_send(a->tx, r->t_done, pl, 5, op) == 0) a->resends++;
    }
}

/* ---------- 연습 3: 성공하면 백오프 리셋 ---------- */
static void reset_backoff(world *w, uint32_t tag, const arq_report *r) {
    (void)tag;
    arq_tx *t = &w->tx;
    if (r->st != ARQ_OK || !t->have_rtt) return;
    uint32_t rto = (uint32_t)(t->srtt8 >> 3) + (uint32_t)(t->rttvar4 > 1 ? t->rttvar4 : 1);
    if (rto < t->cfg.rto_min) rto = t->cfg.rto_min;
    if (rto > t->cfg.rto_cap) rto = t->cfg.rto_cap;
    t->rto = rto;                                               /* 두 배로 올린 값을 다음 메시지로 끌고 가지 않는다 */
}

/* ---------- 연습 4: 수신기 재부팅 ---------- */
static bool g_rebooted, g_synced = true, g_use_sync;
static uint32_t g_reboot_at_tick = UINT32_MAX;
static void rx_reboot(world *w) {
    w->rx.last = seq_mask(w->rx.seq_bits);                      /* RAM 초기화: 부팅 직후와 같은 상태 */
    g_rebooted = true; g_synced = !g_use_sync;
}
static void hook_reboot_at_seq200(world *w, uint32_t now) { (void)now; if (!g_rebooted && w->tx.seq == 200) rx_reboot(w); }
static void hook_reboot_at_tick(world *w, uint32_t now) { if (!g_rebooted && now == g_reboot_at_tick) rx_reboot(w); }
static void rx_sync_fn(world *w, uint32_t now, const uint8_t *b, size_t n) {
    frame_view v;
    if (!g_synced && frame_parse(b, n, &v) == 0 && v.type == FT_DATA) {
        w->rx.last = (uint8_t)((v.seq - 1u) & seq_mask(w->rx.seq_bits));   /* 첫 정상 DATA 를 무조건 "새 것" 으로 */
        g_synced = true;
    }
    arq_rx_on_frame(&w->rx, now, b, n);
}

int main(void) {
    /* ---- 연습 1 ---- */
    {
        uint32_t Tf[17], Te[17];
        for (int k = 1; k <= 16; k++) { Tf[k] = 30; Te[k] = 30u << (k - 1 < 3 ? k - 1 : 3); }
        th t0 = theory(0.0, 10, Tf, 16);
        CHECK(rel(t0.goodput, 50.0) < 1e-12 && rel(t0.frames, 1.0) < 1e-12 && rel(t0.mean_lat, 10.0) < 1e-12);
        const double ps[2] = { 0.2, 0.4 };
        for (int i = 0; i < 2; i++)
            for (int pol = 0; pol < 2; pol++) {
                chan_cfg l = LINK_CLEAN; l.loss_pm = (uint16_t)(ps[i] * 1000);
                arq_cfg c = cfg_default(); c.max_retries = 15; c.seed = 300u + (uint32_t)(i * 2 + pol);
                if (pol) c.bo = BO_EXP;
                w_init(&g_w, &c, &l, &l, 5000, false); w_run(&g_w, 100000000u);
                verdict v = w_verdict(&g_w);
                double gp = 1000.0 * v.ok / g_w.ticks, fr = (double)g_w.tx.tx_frames / 5000, lat = 0; uint32_t nd = 0;
                for (uint32_t m = 0; m < 5000; m++) if (g_w.ndeliv[m]) { lat += g_w.t_deliv[m] - g_w.t_start[m]; nd++; }
                lat /= nd;
                th t = theory(ps[i], 10, pol ? Te : Tf, 16);
                printf("ex1 p=%.1f %-5s goodput %.2f (theory %.2f)  frames/msg %.3f (%.3f)  mean latency %.2f (%.2f)\n",
                       ps[i], pol ? "EXP" : "FIXED", gp, t.goodput, fr, t.frames, lat, t.mean_lat);
                CHECK(v_clean(v) && rel(gp, t.goodput) < 0.03 && rel(fr, t.frames) < 0.03 && rel(lat, t.mean_lat) < 0.05);
            }
    }
    /* ---- 연습 2 ---- */
    {
        static const link_script ackdrop[] = { { 0, ACT_DROP, 0 }, { 1, ACT_DROP, 0 }, { 2, ACT_DROP, 0 }, { 3, ACT_DROP, 0 }, { 4, ACT_DROP, 0 } };
        channel f, r; arq_tx tx; arq_rx rx; app_rx ar; app_tx at;
        memset(&ar, 0, sizeof ar); memset(&at, 0, sizeof at);
        arq_cfg c = cfg_default();
        chan_init(&f, &LINK_CLEAN, 11); chan_init(&r, &LINK_CLEAN, 12); r.script = ackdrop; r.nscript = 5;
        arq_tx_init(&tx, &c, &f, app_done, &at, NULL); at.tx = &tx;
        arq_rx_init(&rx, c.seq_bits, &r, app_deliver, &ar, NULL);
        uint8_t pl[5];
        op_payload(pl, 0); arq_send(&tx, 0, pl, 5, 0);
        op_payload(pl, 1); arq_send(&tx, 0, pl, 5, 1);
        for (uint32_t now = 0; now < 5000; now++) {
            pkt p;
            while (chan_recv(&f, now, &p)) arq_rx_on_frame(&rx, now, p.b, p.n);
            while (chan_recv(&r, now, &p)) arq_tx_on_frame(&tx, now, p.b, p.n);
            arq_tx_poll(&tx, now);
        }
        printf("ex2 op0 status: first=FAIL, resends=%u, ARQ deliveries=%u, applied=%u, ignored(dup op)=%u, value=%d\n",
               at.resends, ar.arq_deliveries, ar.applied, ar.ignored, ar.value);
        CHECK(at.resends == 1 && at.st[0] == ARQ_OK && at.ndone[0] == 2 && at.st[1] == ARQ_OK);
        CHECK(ar.arq_deliveries == 3 && ar.applied == 2 && ar.ignored == 1 && ar.value == 10 + 11);
    }
    /* ---- 연습 3 ---- */
    {
        double gp[2][3]; uint32_t p99[2][3];
        for (int li = 0; li < 3; li++)
            for (int pol = 0; pol < 2; pol++) {
                chan_cfg l = { (uint16_t)(li == 0 ? 0 : li == 1 ? 100 : 300), 0, 0, 0, 20, 60, 0 };
                arq_cfg c = cfg_default(); c.max_retries = 20; c.seed = 55; c.bo = BO_ADAPTIVE;
                c.rto_base = 300; c.rto_min = 20; c.rto_cap = 4000;
                w_init(&g_w, &c, &l, &l, 3000, false);
                if (pol) g_w.on_done = reset_backoff;
                w_run(&g_w, 100000000u);
                verdict v = w_verdict(&g_w);
                static uint32_t lat[MSG_MAX]; uint32_t k = 0;
                for (uint32_t m = 0; m < 3000; m++) if (g_w.ndeliv[m]) lat[k++] = g_w.t_deliv[m] - g_w.t_start[m];
                for (uint32_t a = 1; a < k; a++) { uint32_t x = lat[a], b = a; while (b && lat[b - 1] > x) { lat[b] = lat[b - 1]; b--; } lat[b] = x; }
                gp[pol][li] = 1000.0 * v.ok / g_w.ticks; p99[pol][li] = lat[(k * 99 + 99) / 100 - 1];
                printf("ex3 loss %2d%% %-14s goodput %.2f  p99 latency %u  frames/msg %.2f  clean=%d\n", li == 0 ? 0 : li == 1 ? 10 : 30,
                       pol ? "reset-on-OK" : "Karn-retain", gp[pol][li], p99[pol][li], (double)g_w.tx.tx_frames / 3000, v_clean(v));
                CHECK(v_clean(v));
            }
        CHECK(rel(gp[1][0], gp[0][0]) < 0.02);                         /* 손실 0: 차이 없음 */
        CHECK(gp[1][2] > 2 * gp[0][2] && p99[1][2] * 3 < p99[0][2]);    /* 손실 30%: 리셋이 goodput 2배+, p99 1/3 이하 */
    }
    /* ---- 연습 4 ---- */
    {
        arq_cfg c = cfg_default();
        CHECK(seq_bits_required(10, 10, 10, 10) == 1);                 /* 이 링크는 1비트로도 충분 — 재부팅은 seq 폭 문제가 아니다 */
        for (int fix = 0; fix < 2; fix++) {
            g_rebooted = false; g_use_sync = fix; g_synced = true;
            w_init(&g_w, &c, &LINK_CLEAN, &LINK_CLEAN, 300, false);
            g_w.hook = hook_reboot_at_seq200; g_w.rx_fn = rx_sync_fn;
            w_run(&g_w, 1000000);
            verdict v = w_verdict(&g_w);
            printf("ex4 reboot at seq 200, %s: ok=%u silent_loss=%u dup_deliv=%u\n", fix ? "sync-on-first" : "no fix       ",
                   v.ok, v.silent_loss, v.dup_deliv);
            if (!fix) CHECK(g_rebooted && v.ok == 300 && v.silent_loss == 56 && g_w.ndeliv[199] == 1 && g_w.ndeliv[256] == 1);
            else      CHECK(g_rebooted && v_clean(v) && v.ok == 300);
        }
        /* 남는 위험: msg0 의 ACK 가 사라진 사이 재부팅 → 재전송분을 "새 것" 으로 받아 두 번 전달 */
        static const link_script r[] = { { 0, ACT_DROP, 0 } };
        g_rebooted = false; g_use_sync = true; g_synced = true; g_reboot_at_tick = 20;
        w_init(&g_w, &c, &LINK_CLEAN, &LINK_CLEAN, 3, true);
        g_w.rev.script = r; g_w.rev.nscript = 1; g_w.hook = hook_reboot_at_tick; g_w.rx_fn = rx_sync_fn;
        w_run(&g_w, 100000);
        verdict v = w_verdict(&g_w);
        log_print(&g_w.log);
        printf("ex4 reboot between delivery and lost ACK: ndeliv[0]=%u dup_deliv=%u silent_loss=%u\n", g_w.ndeliv[0], v.dup_deliv, v.silent_loss);
        CHECK(g_rebooted && g_w.ndeliv[0] == 2 && v.dup_deliv == 1 && v.silent_loss == 0 && v.ok == 3);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
