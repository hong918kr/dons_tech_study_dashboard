/* COMM-M05 CAN 중재 시뮬레이션: 낮은 ID가 이기는 이유
 *
 * 모델: 1 스텝 = 1 비트 시간. dominant = 0, recessive = 1, 버스 = 모든 노드 드라이버의 AND.
 *   한 스텝: ① 놀고 있는(IDLE) 노드가 큐에 메시지가 있으면 SOF 시작 → ② 노드마다 이번 비트에 낼 값(node_drive)
 *            → ③ 버스 = AND → ④ 모든 노드가 같은 버스 비트를 샘플(송신기 비교 + 수신기 파서).
 *   프레임 인코더(CRC-15 + 비트 스터핑)와 비트 스트리밍 수신기는 COMM-M04 모델 축약판 (주어짐).
 *   모든 노드는 error-active 로만 동작한다 (TEC/REC·passive·bus-off 는 COMM-M07).
 * Part A: wired-AND, 노드가 낼 비트, 송신기의 "보낸 비트 vs 읽은 비트" 판정 (중재 필드에서 rec 보냈는데 dom → 패배)
 * Part B: 판정 처리 — 패배 → 즉시 수신기로 전환해 이긴 프레임을 받고 다음 bus idle 에 재시도,
 *         bit / stuff / ACK 에러 → 에러 플래그 + 메시지당 에러 재전송 상한 (모델 정의)
 * Part C: 노드 송신 큐 정책 FIFO vs 우선순위(가장 낮은 중재 키) — 우선순위 역전, 높은 ID 굶주림, 대기 시간 기록
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ================= 프레임 인코더 (COMM-M04 모델 축약판, 주어짐) ================= */
typedef struct { uint32_t id; bool ext, rtr; uint8_t dlc; uint8_t data[8]; } CanFrame;

enum { MAXU = 128, MAXBITS = 168 };   /* 확장 8바이트: 비스터프 118 + 스터프 ≤ 29 + 꼬리 10 = 157 */

static int frame_nbytes(const CanFrame *f) { return f->rtr ? 0 : (f->dlc > 8 ? 8 : f->dlc); }

/* CRC-15/CAN: poly 0x4599, init 0, 반사 없음, 입력 = 비스터프 비트 SOF..DATA 끝 */
static uint16_t crc15(const uint8_t *bits, int n) {
    uint16_t crc = 0;
    for (int i = 0; i < n; i++) {
        unsigned fb = ((crc >> 14) & 1u) ^ bits[i];
        crc = (uint16_t)((crc << 1) & 0x7FFFu);
        if (fb) crc ^= 0x4599u;
    }
    return crc;
}

static int put_bits(uint8_t *u, int n, uint32_t v, int w) {
    for (int i = w - 1; i >= 0; i--) u[n++] = (uint8_t)((v >> i) & 1u);
    return n;
}

/* 비스터프 비트열 SOF..CRC 끝. 반환 = 비트 수 (표준 19 + 8n + 15, 확장 39 + 8n + 15) */
static int frame_unstuffed(const CanFrame *f, uint8_t *u) {
    int n = 0;
    u[n++] = 0;                                              /* SOF (dominant) */
    if (!f->ext) {
        n = put_bits(u, n, f->id & 0x7FFu, 11);              /* uidx 1..11 */
        u[n++] = f->rtr; u[n++] = 0; u[n++] = 0;             /* RTR(12) IDE(13)=dom r0(14) */
    } else {
        n = put_bits(u, n, (f->id >> 18) & 0x7FFu, 11);      /* ID-A, uidx 1..11 */
        u[n++] = 1; u[n++] = 1;                              /* SRR(12)=rec IDE(13)=rec */
        n = put_bits(u, n, f->id & 0x3FFFFu, 18);            /* ID-B, uidx 14..31 */
        u[n++] = f->rtr; u[n++] = 0; u[n++] = 0;             /* RTR(32) r1 r0 */
    }
    n = put_bits(u, n, f->dlc & 0xFu, 4);
    for (int i = 0; i < frame_nbytes(f); i++) n = put_bits(u, n, f->data[i], 8);
    uint16_t c = crc15(u, n);
    return put_bits(u, n, c, 15);
}

/* 송신 이미지: 스터프된 SOF..CRC + 고정 꼬리(CRC delim, ACK slot, ACK delim, EOF×7) 전부 recessive */
typedef struct {
    uint8_t bit[MAXBITS];
    int16_t uidx[MAXBITS];   /* 비스터프 인덱스(0 = SOF), -1 = 스터프 비트, -2 = 고정 꼬리 */
    int     len;             /* SOF..EOF 끝 */
    int     stuffed_len;     /* SOF..CRC 끝 (스터프 포함) */
    int     ack_pos;         /* ACK 슬롯 위치 = stuffed_len + 1 */
    int     arb_last;        /* 중재 필드 마지막 비스터프 인덱스: 표준 12 (RTR), 확장 32 (RTR) */
} TxImage;

static void can_encode(const CanFrame *f, TxImage *img) {
    uint8_t u[MAXU];
    int nu = frame_unstuffed(f, u), n = 0, run = 0;
    uint8_t last = 2;
    for (int i = 0; i < nu; i++) {
        img->bit[n] = u[i]; img->uidx[n++] = (int16_t)i;
        if (u[i] == last) run++; else { last = u[i]; run = 1; }
        if (run == 5) { last ^= 1u; img->bit[n] = last; img->uidx[n++] = -1; run = 1; }  /* 스터프 비트도 다음 런에 센다 */
    }
    img->stuffed_len = n;
    for (int k = 0; k < 10; k++) { img->bit[n] = 1; img->uidx[n++] = -2; }
    img->len = n;
    img->ack_pos = img->stuffed_len + 1;
    img->arb_last = f->ext ? 32 : 12;
}

/* ================= 노드 · 버스 모델 (주어짐) ================= */
enum { MAXN = 8, QCAP = 8, RXCAP = 64, DONECAP = 64, NSTAT = 16, TRACE_CAP = 256, LOGCAP = 4096 };

typedef enum { ST_IDLE, ST_FRAME, ST_CRCDEL, ST_ACK, ST_ACKDEL, ST_EOF, ST_IFS,
               ST_ERRFLAG, ST_ERRWAIT, ST_ERRDEL } NState;
typedef enum { TXV_OK, TXV_LOST, TXV_BIT_ERR, TXV_STUFF_ERR, TXV_ACK_ERR } TxVerdict;
typedef enum { Q_FIFO, Q_PRIO } QPolicy;

typedef struct { CanFrame f; uint32_t t_enq; int errs, losses; } Msg;
typedef struct { CanFrame f; uint32_t t; } RxEntry;
typedef struct { CanFrame f; int node; bool ok; int errs, losses; uint32_t t_enq, t_done; } Done;
typedef struct { uint32_t id; int sent, aborted, dropped; uint64_t sum_wait; uint32_t max_wait; } IdStat;

typedef struct {
    int      idx;
    NState   st;
    int      cnt;                 /* EOF / IFS / 에러 플래그 / 딜리미터 카운터 */
    int      bitpos;              /* 이번 프레임에서 SOF = 0 부터 센 버스 비트 번호 (스터프 포함) */
    /* 수신기 — 모든 노드가 (송신 중에도) 항상 돌린다 */
    uint8_t  ub[MAXU]; int un, total;
    uint8_t  last; int run;
    bool     crc_bad;
    bool     listen_only;         /* ACK 를 내지 않는 노드 (고장 주입용) */
    /* 송신기 */
    bool     tx_active;
    TxImage  img;
    int      cur;                 /* q[] 에서 지금 보내는 메시지 */
    uint8_t  sent;                /* 이번 비트에 낸 값 */
    QPolicy  policy;
    Msg      q[QCAP]; int qn;     /* q[0] = 가장 오래된 것 (도착 순서 유지) */
    /* 결과 · 통계 */
    RxEntry  rx[RXCAP]; int nrx;
    int      arb_lost, bit_err, stuff_err, ack_err, rx_stuff_err, form_err, crc_err;
    int      first_lost_uidx, last_err_uidx;
    TxVerdict last_err;
} Node;

typedef struct {
    Node     node[MAXN]; int n;
    uint32_t t;
    uint8_t  level;
    int      max_err_retx;        /* 메시지당 에러 재전송 상한, -1 = 무한 (모델 정의) */
    int64_t  force_dom_at;        /* 이 비트 시간에 버스를 dominant 로 강제 (노이즈 주입), -1 = 없음 */
    Done     done[DONECAP]; int ndone;
    IdStat   stat[NSTAT]; int nstat;
    char     tr_bus[TRACE_CAP + 1], tr_drv[MAXN][TRACE_CAP + 1]; uint32_t tr_len;
    char     log[LOGCAP]; size_t loglen;
} Sim;

__attribute__((format(printf, 2, 3)))
static void logf_(Sim *s, const char *fmt, ...) {
    if (s->loglen + 1 >= LOGCAP) return;
    va_list ap; va_start(ap, fmt);
    int k = vsnprintf(s->log + s->loglen, LOGCAP - s->loglen, fmt, ap);
    va_end(ap);
    if (k > 0) s->loglen += (size_t)k < LOGCAP - s->loglen ? (size_t)k : LOGCAP - s->loglen - 1;
}

static const char *verdict_name(TxVerdict v) {
    static const char *nm[] = { "OK", "LOST", "BIT_ERR", "STUFF_ERR", "ACK_ERR" };
    return nm[v];
}

static IdStat *stat_of(Sim *s, uint32_t id) {
    for (int i = 0; i < s->nstat; i++) if (s->stat[i].id == id) return &s->stat[i];
    if (s->nstat == NSTAT) return NULL;
    s->stat[s->nstat] = (IdStat){ .id = id };
    return &s->stat[s->nstat++];
}

static void start_error(Node *nd) { nd->st = ST_ERRFLAG; nd->cnt = 0; nd->tx_active = false; }

static void q_remove(Node *nd, int i) {
    memmove(&nd->q[i], &nd->q[i + 1], (size_t)(nd->qn - i - 1) * sizeof nd->q[0]);
    nd->qn--;
}

/* 메시지 끝: 성공(ok) 또는 에러 재전송 상한 초과로 포기. 대기 시간 = t_done - t_enq (비트) */
static void finish_msg(Sim *s, Node *nd, bool ok) {
    Msg *m = &nd->q[nd->cur];
    if (s->ndone < DONECAP)
        s->done[s->ndone++] = (Done){ m->f, nd->idx, ok, m->errs, m->losses, m->t_enq, s->t };
    IdStat *st = stat_of(s, m->f.id);
    if (st) {
        if (ok) {
            uint32_t w = s->t - m->t_enq;
            st->sent++; st->sum_wait += w; if (w > st->max_wait) st->max_wait = w;
        } else st->aborted++;
    }
    logf_(s, "t=%-5u node%d TX %-7s id=0x%03X%s errs=%d losses=%d wait=%u\n", s->t, nd->idx,
          ok ? "OK" : "ABORTED", (unsigned)m->f.id, m->f.ext ? "x" : " ", m->errs, m->losses, s->t - m->t_enq);
    q_remove(nd, nd->cur);
    nd->tx_active = false; nd->cur = -1;
}

/* 드라이버 API: 큐에 넣기. 꽉 차면 -1 (버리고 통계에 dropped) */
static int can_send(Sim *s, int node, const CanFrame *f) {
    Node *nd = &s->node[node];
    if (nd->qn >= QCAP) { IdStat *st = stat_of(s, f->id); if (st) st->dropped++; return -1; }
    nd->q[nd->qn++] = (Msg){ *f, s->t, 0, 0 };
    return 0;
}

/* ================= Part A: wired-AND, 구동 비트, 송신기 판정 ================= */

/* 버스 레벨 = 모든 드라이버의 AND. 아무도 dominant(0) 를 안 내면 recessive(1). */
static uint8_t bus_resolve(const uint8_t *drv, int n) {
    uint8_t b = 1;
    for (int i = 0; i < n; i++) b &= drv[i];
    return b;
}

/* 이 노드가 이번 비트에 내는 값 */
static uint8_t node_drive(const Node *nd) {
    if (nd->st == ST_ERRFLAG) return 0;                         /* 능동 에러 플래그: dominant 6개 */
    if (nd->tx_active) return nd->img.bit[nd->bitpos];         /* 송신 이미지 (ACK 슬롯은 recessive) */
    if (nd->st == ST_ACK && !nd->crc_bad && !nd->listen_only)   /* CRC 맞게 받은 수신기 → ACK */
        return 0;
    return 1;
}

/* 송신기가 pos 번째 비트에서 sent 를 냈고 버스에서 bus 를 읽었다 → 판정 */
static TxVerdict tx_check(const TxImage *img, int pos, uint8_t sent, uint8_t bus) {
    if (pos == img->ack_pos)                       /* ACK 슬롯: 내가 rec, 누군가 dom 이어야 정상 */
        return bus == 0 ? TXV_OK : TXV_ACK_ERR;
    if (sent == bus) return TXV_OK;
    int u = img->uidx[pos];
    if (u == -1) return TXV_STUFF_ERR;             /* 스터프 비트가 뒤집힘 — 중재 중이라도 패배가 아니다 */
    if (sent == 1 && u >= 1 && u <= img->arb_last)
        return TXV_LOST;                           /* 중재 필드에서 rec 냈는데 dom → 졌다 */
    return TXV_BIT_ERR;                            /* 그 밖의 불일치 (중재 밖, 또는 dom 냈는데 rec) */
}

/* ================= Part B: 판정 처리 ================= */

static void tx_on_verdict(Sim *s, Node *nd, TxVerdict v) {
    Msg *m = &nd->q[nd->cur];
    switch (v) {
    case TXV_OK: return;
    case TXV_LOST:
        nd->tx_active = false;                     /* 즉시 놓는다 → 이 비트부터 그냥 수신기 */
        if (nd->arb_lost == 0) nd->first_lost_uidx = nd->img.uidx[nd->bitpos];
        nd->arb_lost++; m->losses++;
        logf_(s, "t=%-5u node%d LOST    id=0x%03X%s at uidx %d -> receiver, retry at next idle\n",
              s->t, nd->idx, (unsigned)m->f.id, m->f.ext ? "x" : " ", nd->img.uidx[nd->bitpos]);
        return;                                    /* 메시지는 큐에 그대로 — 에러가 아니므로 errs 안 늘림 */
    case TXV_BIT_ERR:   nd->bit_err++;   break;
    case TXV_STUFF_ERR: nd->stuff_err++; break;
    case TXV_ACK_ERR:   nd->ack_err++;   break;
    }
    nd->last_err = v; nd->last_err_uidx = nd->img.uidx[nd->bitpos];
    m->errs++;
    logf_(s, "t=%-5u node%d %-9s id=0x%03X%s at pos %d (uidx %d) -> error flag\n", s->t, nd->idx,
          verdict_name(v), (unsigned)m->f.id, m->f.ext ? "x" : " ", nd->bitpos, nd->img.uidx[nd->bitpos]);
    start_error(nd);                               /* 다음 비트부터 dominant 6개 */
    if (s->max_err_retx >= 0 && m->errs > s->max_err_retx)
        finish_msg(s, nd, false);                  /* 상한 초과 → 포기하고 앱에 실패 보고 */
}

/* ================= Part C: 송신 큐 정책 ================= */

/* 중재 비트열을 먼저 나가는 비트가 상위 비트가 되게 32비트로 묶는다. 작을수록 이긴다.
 * 표준: ID[10:0] RTR IDE(=0) 뒤는 0.  확장: ID-A[10:0] SRR(=1) IDE(=1) ID-B[17:0] RTR */
static uint32_t arb_key(const CanFrame *f) {
    if (!f->ext) return ((f->id & 0x7FFu) << 21) | ((uint32_t)f->rtr << 20);
    return (((f->id >> 18) & 0x7FFu) << 21) | (1u << 20) | (1u << 19) | ((f->id & 0x3FFFFu) << 1) | f->rtr;
}

/* 다음에 보낼 메시지 인덱스. FIFO = 가장 오래된 것, PRIO = 중재 키 최소 (같으면 오래된 것) */
static int txq_pick(const Node *nd) {
    if (nd->policy == Q_FIFO) return 0;
    int best = 0;
    for (int i = 1; i < nd->qn; i++)
        if (arb_key(&nd->q[i].f) < arb_key(&nd->q[best].f)) best = i;
    return best;
}

/* ================= 시뮬레이터 (주어짐) ================= */

static void deliver(Sim *s, Node *nd) {
    CanFrame f = { 0 };
    bool ext = nd->ub[13];
    int hdr = ext ? 39 : 19, v = 0;
    for (int i = 1; i <= 11; i++) v = (v << 1) | nd->ub[i];
    if (ext) for (int i = 14; i <= 31; i++) v = (v << 1) | nd->ub[i];
    f.id = (uint32_t)v; f.ext = ext; f.rtr = nd->ub[ext ? 32 : 12];
    for (int i = hdr - 4; i < hdr; i++) f.dlc = (uint8_t)((f.dlc << 1) | nd->ub[i]);
    for (int k = 0; k < frame_nbytes(&f); k++)
        for (int i = 0; i < 8; i++) f.data[k] = (uint8_t)((f.data[k] << 1) | nd->ub[hdr + 8 * k + i]);
    if (nd->nrx < RXCAP) nd->rx[nd->nrx++] = (RxEntry){ f, s->t };
}

static void rx_error(Sim *s, Node *nd, int *ctr, const char *what) {
    (*ctr)++;
    logf_(s, "t=%-5u node%d RX %-6s at frame bit %d -> error flag\n", s->t, nd->idx, what, nd->bitpos);
    start_error(nd);
}

/* 비트 스트리밍 수신기: 디스터핑 + 필드 파싱 + 고정 형식 검사 (COMM-M04 디코더의 스트리밍판) */
static void rx_bit(Sim *s, Node *nd, uint8_t b) {
    switch (nd->st) {
    case ST_IDLE:
        if (b == 0) {                                    /* SOF */
            nd->st = ST_FRAME; nd->un = 0; nd->ub[nd->un++] = 0;
            nd->last = 0; nd->run = 1; nd->total = -1; nd->crc_bad = false; nd->bitpos = 0;
        }
        return;
    case ST_FRAME:
        if (nd->run == 5) {                              /* 이번 비트는 스터프 비트여야 한다 */
            if (b == nd->last) { rx_error(s, nd, &nd->rx_stuff_err, "STUFF"); return; }
            nd->last = b; nd->run = 1;
            if (nd->un == nd->total) nd->st = ST_CRCDEL;
            return;
        }
        if (b == nd->last) nd->run++; else { nd->last = b; nd->run = 1; }
        if (nd->un >= MAXU) { rx_error(s, nd, &nd->form_err, "FORM"); return; }
        nd->ub[nd->un++] = b;
        if (nd->total < 0 && nd->un >= 14) {
            bool ext = nd->ub[13];
            int hdr = ext ? 39 : 19;
            if (nd->un == hdr) {
                CanFrame t = { .rtr = nd->ub[ext ? 32 : 12] };
                for (int i = hdr - 4; i < hdr; i++) t.dlc = (uint8_t)((t.dlc << 1) | nd->ub[i]);
                nd->total = hdr + 8 * frame_nbytes(&t) + 15;
            }
        }
        if (nd->un == nd->total) {
            int nd_ = nd->total - 15; uint16_t c = 0;
            for (int i = 0; i < 15; i++) c = (uint16_t)((c << 1) | nd->ub[nd_ + i]);
            nd->crc_bad = crc15(nd->ub, nd_) != c;
            if (nd->run != 5) nd->st = ST_CRCDEL;        /* run == 5 면 스터프 비트 하나 더 */
        }
        return;
    case ST_CRCDEL: if (b == 0) rx_error(s, nd, &nd->form_err, "FORM"); else nd->st = ST_ACK; return;
    case ST_ACK:    nd->st = ST_ACKDEL; return;          /* ACK 판정은 송신기 몫 */
    case ST_ACKDEL:
        if (b == 0) rx_error(s, nd, &nd->form_err, "FORM");
        else if (nd->crc_bad) rx_error(s, nd, &nd->crc_err, "CRC"); /* CRC 에러 플래그는 ACK delim 다음 비트부터 */
        else { nd->st = ST_EOF; nd->cnt = 0; }
        return;
    case ST_EOF:
        if (b == 0) { rx_error(s, nd, &nd->form_err, "FORM"); return; }
        if (++nd->cnt == 7) {
            if (!nd->tx_active) deliver(s, nd);          /* 모델: EOF 마지막 비트에서 수신 확정 */
            nd->st = ST_IFS; nd->cnt = 0;
        }
        return;
    case ST_IFS:     if (++nd->cnt == 3) nd->st = ST_IDLE; return;
    case ST_ERRFLAG: if (++nd->cnt == 6) nd->st = ST_ERRWAIT; return;
    case ST_ERRWAIT: if (b == 1) { nd->st = ST_ERRDEL; nd->cnt = 1; } return;
    case ST_ERRDEL:
        if (b == 0) { nd->st = ST_ERRWAIT; return; }
        if (++nd->cnt == 8) { nd->st = ST_IFS; nd->cnt = 0; }
        return;
    }
}

static void maybe_start_tx(Node *nd) {
    if (nd->st != ST_IDLE || nd->tx_active || nd->qn == 0) return;
    int i = txq_pick(nd);
    if (i < 0 || i >= nd->qn) i = 0;
    nd->cur = i;
    can_encode(&nd->q[i].f, &nd->img);
    nd->tx_active = true;
    nd->bitpos = 0;
}

static void node_sample(Sim *s, Node *nd, uint8_t b) {
    int pos = nd->bitpos;
    if (nd->tx_active) {
        tx_on_verdict(s, nd, tx_check(&nd->img, pos, nd->sent, b));
        if (nd->st == ST_ERRFLAG && nd->cnt == 0) { nd->bitpos++; return; }   /* 방금 에러 → 수신기 건너뜀 */
    }
    rx_bit(s, nd, b);
    if (nd->tx_active && pos == nd->img.len - 1) finish_msg(s, nd, true);  /* EOF 끝까지 무사 → 송신 성공 */
    nd->bitpos++;
}

static void sim_init(Sim *s, int n) {
    memset(s, 0, sizeof *s);
    s->n = n; s->max_err_retx = 3; s->force_dom_at = -1;
    for (int i = 0; i < n; i++) {
        s->node[i].idx = i; s->node[i].cur = -1;
        s->node[i].first_lost_uidx = -1; s->node[i].last_err_uidx = -9;
    }
}

static void sim_step(Sim *s) {
    uint8_t drv[MAXN];
    for (int i = 0; i < s->n; i++) maybe_start_tx(&s->node[i]);
    for (int i = 0; i < s->n; i++) { drv[i] = node_drive(&s->node[i]); s->node[i].sent = drv[i]; }
    uint8_t b = bus_resolve(drv, s->n);
    if ((int64_t)s->t == s->force_dom_at) b = 0;
    s->level = b;
    if (s->tr_len < TRACE_CAP) {
        s->tr_bus[s->tr_len] = (char)('0' + b);
        for (int i = 0; i < s->n; i++) s->tr_drv[i][s->tr_len] = (char)('0' + drv[i]);
        s->tr_len++;
    }
    for (int i = 0; i < s->n; i++) node_sample(s, &s->node[i], b);
    s->t++;
}

static bool sim_busy(const Sim *s) {
    for (int i = 0; i < s->n; i++) if (s->node[i].qn > 0 || s->node[i].st != ST_IDLE) return true;
    return false;
}

static uint32_t sim_run(Sim *s, uint32_t max_bits) {
    uint32_t t0 = s->t;
    while (sim_busy(s) && s->t - t0 < max_bits) sim_step(s);
    return s->t - t0;
}

/* ================= 테스트 하네스 ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs_state = 0x2545F491u;
static uint32_t xorshift32(void) {
    uint32_t x = xs_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return xs_state = x;
}

static CanFrame mk(uint32_t id, bool ext, bool rtr, uint8_t dlc, uint32_t seed) {
    CanFrame f = { .id = id, .ext = ext, .rtr = rtr, .dlc = dlc };
    for (int i = 0; i < 8; i++) f.data[i] = (uint8_t)(seed * 131u + (uint32_t)i * 29u);
    return f;
}

static bool same_frame(const CanFrame *a, const CanFrame *b) {
    if (a->id != b->id || a->ext != b->ext || a->rtr != b->rtr || a->dlc != b->dlc) return false;
    return memcmp(a->data, b->data, (size_t)frame_nbytes(a)) == 0;
}

/* 오라클 (학생 코드와 독립): 비스터프 비트를 앞에서부터 비교해 처음 다른 위치. 같으면 0.
 * 표준이 끼면 IDE(13)까지, 둘 다 확장이면 RTR(32)까지가 중재 구간. *winner = 0 이면 a, 1 이면 b */
static int oracle_first_diff(const CanFrame *a, const CanFrame *b, int *winner) {
    uint8_t ua[MAXU], ub[MAXU];
    frame_unstuffed(a, ua); frame_unstuffed(b, ub);
    int lim = (a->ext && b->ext) ? 32 : 13;
    for (int i = 1; i <= lim; i++)
        if (ua[i] != ub[i]) { *winner = ua[i] == 0 ? 0 : 1; return i; }
    return 0;
}

static int frame_len(const CanFrame *f) { TxImage im; can_encode(f, &im); return im.len; }

/* 2노드 경주: a(node0) vs b(node1). 기대 승자가 먼저 끝나고, 패자는 기대 위치에서 졌고, 서로의 프레임을 받았나 */
static bool race2(const CanFrame *a, const CanFrame *b) {
    static Sim s;
    sim_init(&s, 2);
    can_send(&s, 0, a); can_send(&s, 1, b);
    uint32_t bits = sim_run(&s, 2000);
    int w, d = oracle_first_diff(a, b, &w);
    if (d == 0) return false;
    const Node *lo = &s.node[1 - w];
    bool ok = s.ndone == 2 && s.done[0].ok && s.done[1].ok && s.done[0].node == w
              && lo->arb_lost == 1 && lo->first_lost_uidx == d && s.node[w].arb_lost == 0
              && s.node[0].nrx == 1 && same_frame(&s.node[0].rx[0].f, b)
              && s.node[1].nrx == 1 && same_frame(&s.node[1].rx[0].f, a)
              && bits == (uint32_t)(frame_len(a) + 3 + frame_len(b) + 3);
    return ok;
}

static void print_trace(const Sim *s, uint32_t n, const char *const *names) {
    for (int i = 0; i < s->n; i++) printf("  %-10s %.*s\n", names[i], (int)n, s->tr_drv[i]);
    printf("  %-10s %.*s\n", "BUS", (int)n, s->tr_bus);
}

int main(void) {
    static Sim s;

    /* ---- 0. 주어진 모델 자체 검사 (CRC-15 check value, 스터프 규칙) ---- */
    {
        uint8_t bits[72]; int n = 0;
        const char *msg = "123456789";
        for (int i = 0; i < 9; i++) n = put_bits(bits, n, (uint8_t)msg[i], 8);
        CHECK(crc15(bits, n) == 0x059E);                    /* RevEng CRC-15/CAN check */
        TxImage im; CanFrame f = mk(0x000, false, false, 0, 0);
        can_encode(&f, &im);
        CHECK(im.uidx[5] == -1 && im.bit[5] == 1);          /* SOF + ID 0000 → 5 dom → 스터프 rec */
    }

    /* ---- Part A: wired-AND, 구동, 판정 ---- */
    {
        uint8_t d[3];
        int good = 0;
        for (int m = 0; m < 8; m++) {                       /* 3노드 × dom/rec 전수 */
            for (int i = 0; i < 3; i++) d[i] = (uint8_t)((m >> i) & 1);
            good += bus_resolve(d, 3) == (m == 7 ? 1 : 0);
        }
        CHECK(good == 8);
        CHECK(bus_resolve(d, 0) == 1);                      /* 노드 없음 = 풀업 쪽 recessive */

        TxImage im; CanFrame f = mk(0x123, false, false, 1, 1);
        can_encode(&f, &im);
        CHECK(tx_check(&im, 3, 1, 0) == TXV_LOST);          /* uidx 3 = ID[8], rec 냈는데 dom */
        CHECK(tx_check(&im, 3, 1, 1) == TXV_OK);
        CHECK(tx_check(&im, 3, 0, 1) == TXV_BIT_ERR);       /* dom 냈는데 rec → 패배가 아니라 bit error */
        int rtr_pos = -1, ide_pos = -1, dlc_pos = -1;
        for (int p = 0; p < im.stuffed_len; p++) {
            if (im.uidx[p] == 12) rtr_pos = p;
            if (im.uidx[p] == 13) ide_pos = p;
            if (im.uidx[p] == 18) dlc_pos = p;
        }
        CHECK(tx_check(&im, rtr_pos, 1, 0) == TXV_LOST);    /* RTR 는 중재 필드 */
        CHECK(tx_check(&im, ide_pos, 1, 0) == TXV_BIT_ERR); /* 표준의 IDE 는 중재 밖 */
        CHECK(tx_check(&im, dlc_pos, 1, 0) == TXV_BIT_ERR);
        CHECK(tx_check(&im, im.ack_pos, 1, 0) == TXV_OK);   /* ACK 받음 */
        CHECK(tx_check(&im, im.ack_pos, 1, 1) == TXV_ACK_ERR);
        CHECK(tx_check(&im, im.ack_pos - 1, 1, 0) == TXV_BIT_ERR);  /* CRC delim 에 dom */
        TxImage ix; CanFrame fx = mk(0x123u << 18, true, false, 0, 0);
        can_encode(&fx, &ix);
        int srr = -1, ide = -1, idb = -1, rtrx = -1;
        for (int p = 0; p < ix.stuffed_len; p++) {
            if (ix.uidx[p] == 12) srr = p;
            if (ix.uidx[p] == 13) ide = p;
            if (ix.uidx[p] == 31) idb = p;
            if (ix.uidx[p] == 32) rtrx = p;
        }
        CHECK(tx_check(&ix, srr, 1, 0) == TXV_LOST && tx_check(&ix, ide, 1, 0) == TXV_LOST);
        CHECK(ix.bit[idb] == 0 && tx_check(&ix, rtrx, 0, 1) == TXV_BIT_ERR);
        TxImage iz; CanFrame fz = mk(0x000, false, false, 0, 0);
        can_encode(&fz, &iz);
        CHECK(tx_check(&iz, 5, 1, 0) == TXV_STUFF_ERR);     /* 중재 중 스터프 비트 뒤집힘 ≠ 패배 */

        Node nd; memset(&nd, 0, sizeof nd);
        nd.st = ST_ERRFLAG; CHECK(node_drive(&nd) == 0);
        nd.st = ST_ACK; CHECK(node_drive(&nd) == 0);
        nd.crc_bad = true; CHECK(node_drive(&nd) == 1);     /* CRC 틀리면 ACK 안 함 */
        nd.crc_bad = false; nd.listen_only = true; CHECK(node_drive(&nd) == 1);
        nd.listen_only = false; nd.tx_active = true; nd.img = im; nd.bitpos = im.ack_pos;
        CHECK(node_drive(&nd) == 1);                        /* 송신기는 자기 ACK 슬롯에 rec */
        nd.bitpos = 0; CHECK(node_drive(&nd) == 0);         /* SOF */
    }

    /* ---- 그림용 3노드 경주: 0x065 / 0x063 / 0x070 (DLC 0) ---- */
    {
        sim_init(&s, 3);
        CanFrame a = mk(0x065, false, false, 0, 0), b = mk(0x063, false, false, 0, 0), c = mk(0x070, false, false, 0, 0);
        can_send(&s, 0, &a); can_send(&s, 1, &b); can_send(&s, 2, &c);
        sim_run(&s, 5000);
        const char *nm[] = { "node0 065", "node1 063", "node2 070" };
        printf("3-node race, first 16 bits (SOF..RTR..IDE):\n");
        print_trace(&s, 16, nm);
        printf("%.*s", (int)s.loglen, s.log);
        CHECK(s.ndone == 3 && s.done[0].f.id == 0x063 && s.done[1].f.id == 0x065 && s.done[2].f.id == 0x070);
        CHECK(s.node[2].first_lost_uidx == 7 && s.node[0].first_lost_uidx == 9 && s.node[1].arb_lost == 0);
        CHECK(s.node[2].arb_lost == 2 && s.node[0].arb_lost == 1);   /* 0x070 은 두 번 진다 */
        CHECK(memcmp(s.tr_drv[0], s.tr_drv[1], 6) == 0 && s.tr_drv[0][5] == '1');  /* 스터프 비트(5) 모두 같음 */
        CHECK(s.node[0].nrx == 2 && s.node[1].nrx == 2 && s.node[2].nrx == 2);
        CHECK(s.node[2].rx_stuff_err + s.node[2].bit_err + s.node[2].stuff_err == 0);
    }

    /* ---- Part A/B: 필수 비교 (낮은 ID, data vs remote, 표준 vs 확장) ---- */
    {
        CanFrame sd = mk(0x123, false, false, 2, 1), sr = mk(0x123, false, true, 2, 1);
        CanFrame xd = mk((0x123u << 18) | 0x00001, true, false, 2, 1), xr = mk((0x123u << 18) | 0x00001, true, true, 2, 1);
        CanFrame lo = mk(0x122, false, false, 8, 5), hi = mk(0x7FF, false, false, 8, 6);
        int w;
        CHECK(race2(&lo, &hi) && oracle_first_diff(&lo, &hi, &w) == 1 && w == 0);
        CHECK(race2(&sd, &sr) && oracle_first_diff(&sd, &sr, &w) == 12);  /* data 가 RTR 에서 이김 */
        CHECK(race2(&xd, &sd) && oracle_first_diff(&xd, &sd, &w) == 12 && w == 1);  /* 표준 data: RTR(dom) vs SRR(rec) */
        CHECK(race2(&sr, &xd) && oracle_first_diff(&sr, &xd, &w) == 13 && w == 0);  /* 표준 remote: RTR=SRR rec 동률 → IDE */
        CHECK(race2(&xr, &xd) && oracle_first_diff(&xd, &xr, &w) == 32);  /* 확장 data vs remote: RTR(32) */
        CanFrame x2 = mk((0x122u << 18) | 0x3FFFF, true, false, 0, 2);
        CHECK(race2(&x2, &sd));                             /* 확장이라도 base ID 가 낮으면 이긴다 */
    }

    /* ---- 전수: 표준 ID 2048개 × (a vs a^1, a vs a^0x400), 같은 base 표준 vs 확장 ---- */
    {
        int bad = 0, runs = 0;
        for (uint32_t a = 0; a < 2048; a++) {
            CanFrame fa = mk(a, false, false, 1, a);
            CanFrame f1 = mk(a ^ 1u, false, false, 1, a + 7), f2 = mk(a ^ 0x400u, false, false, 1, a + 9);
            bad += !race2(&fa, &f1); bad += !race2(&f2, &fa); runs += 2;
            CanFrame fx = mk((a << 18) | (xorshift32() & 0x3FFFF), true, (a & 1) != 0, 1, a);
            CanFrame fr = mk(a, false, true, 1, a);
            bad += !race2(&fx, &fa); bad += !race2(&fr, &fx); runs += 2;
        }
        printf("exhaustive std races: %d runs, %d bad\n", runs, bad);
        CHECK(bad == 0);
    }

    /* ---- 랜덤: K = 2..6 노드가 같은 비트에 SOF, 중재 키 모두 다름 ---- */
    {
        int bad_order = 0, bad_rx = 0, bad_lost = 0, bad_bits = 0, errs = 0, trials = 1500;
        for (int tr = 0; tr < trials; tr++) {
            int k = 2 + (int)(xorshift32() % 5);
            CanFrame fr[6];
            for (int i = 0; i < k; i++) {
                for (;;) {
                    uint32_t r = xorshift32();
                    bool ext = (r & 3) == 0;
                    uint32_t id = ext ? (xorshift32() & 0x1FFFFFFF) : (xorshift32() & 0x7FF);
                    if ((r & 4) && i > 0) {                 /* 같은 11비트 base 를 일부러 겹치게 */
                        uint32_t base0 = (fr[0].ext ? fr[0].id >> 18 : fr[0].id) & 0x7FF;
                        id = ext ? (base0 << 18) | (id & 0x3FFFF) : base0;
                    }
                    fr[i] = mk(id, ext, (r & 8) != 0, (uint8_t)((r >> 4) % 9), r);
                    /* 키 대신 오라클로 충돌 판정 (학생 arb_key 에 의존하지 않음) */
                    bool clash = false; int w;
                    for (int j = 0; j < i; j++) if (oracle_first_diff(&fr[i], &fr[j], &w) == 0) clash = true;
                    if (!clash) break;
                }
            }
            /* 기대 순서: 오라클 비교로 정렬 (삽입 정렬) */
            int ord[6];
            for (int i = 0; i < k; i++) ord[i] = i;
            for (int i = 1; i < k; i++)
                for (int j = i; j > 0; j--) {
                    int w; oracle_first_diff(&fr[ord[j - 1]], &fr[ord[j]], &w);
                    if (w == 1) { int t = ord[j]; ord[j] = ord[j - 1]; ord[j - 1] = t; } else break;
                }
            sim_init(&s, k);
            uint32_t expect_bits = 0;
            for (int i = 0; i < k; i++) { can_send(&s, i, &fr[i]); expect_bits += (uint32_t)frame_len(&fr[i]) + 3; }
            uint32_t bits = sim_run(&s, 20000);
            bad_bits += bits != expect_bits;
            bool okord = s.ndone == k;
            for (int i = 0; okord && i < k; i++) okord = s.done[i].ok && s.done[i].node == ord[i];
            bad_order += !okord;
            for (int nidx = 0; nidx < k; nidx++) {
                const Node *nd = &s.node[nidx];
                int e = 0; bool okrx = nd->nrx == k - 1;
                for (int i = 0; okrx && i < k; i++) {
                    if (ord[i] == nidx) continue;
                    okrx = same_frame(&nd->rx[e++].f, &fr[ord[i]]);
                }
                bad_rx += !okrx;
                int rank = 0; while (ord[rank] != nidx) rank++;
                int w, d = oracle_first_diff(&fr[nidx], &fr[ord[0]], &w);
                bad_lost += nd->arb_lost != rank || (rank > 0 && nd->first_lost_uidx != d);
                errs += nd->bit_err + nd->stuff_err + nd->ack_err + nd->rx_stuff_err + nd->form_err + nd->crc_err;
            }
        }
        printf("random races: %d trials, bad order=%d rx=%d lost=%d bits=%d, errors=%d\n",
               trials, bad_order, bad_rx, bad_lost, bad_bits, errs);
        CHECK(bad_order == 0); CHECK(bad_rx == 0); CHECK(bad_lost == 0); CHECK(bad_bits == 0);
        CHECK(errs == 0);                                   /* 정상 중재는 에러를 하나도 만들지 않는다 */
    }

    /* ---- Part B: 고장 · 경계 ---- */
    {   /* 1) 완전히 같은 프레임 두 개 + 수신기 → 둘 다 성공, 버스엔 프레임 하나 */
        sim_init(&s, 3);
        CanFrame f = mk(0x2A0, false, false, 4, 3);
        can_send(&s, 0, &f); can_send(&s, 1, &f);
        uint32_t bits = sim_run(&s, 2000);
        CHECK(s.ndone == 2 && s.done[0].ok && s.done[1].ok && s.node[0].arb_lost + s.node[1].arb_lost == 0);
        CHECK(s.node[2].nrx == 1 && bits == (uint32_t)frame_len(&f) + 3);
    }
    {   /* 2) 같은 ID, 같은 DLC, 다른 데이터 → 중재 패배 없음, 데이터 필드에서 bit error, 재전송 상한 후 포기 */
        sim_init(&s, 3);
        CanFrame a = mk(0x100, false, false, 2, 0), b = a;
        a.data[0] = 0x5A; a.data[1] = 0x00; b.data[0] = 0x52; b.data[1] = 0x00;   /* 0x5A vs 0x52: bit3 이 다름 */
        can_send(&s, 0, &a); can_send(&s, 1, &b);
        sim_run(&s, 5000);
        printf("--- same ID, different data ---\n%.*s", (int)s.loglen, s.log);
        printf("node0 last=%s, node1 last=%s\n", verdict_name(s.node[0].last_err), verdict_name(s.node[1].last_err));
        CHECK(s.node[0].arb_lost == 0 && s.node[1].arb_lost == 0);
        CHECK(s.node[0].last_err == TXV_BIT_ERR && s.node[0].last_err_uidx == 19 + 4);  /* data[0] bit3 = uidx 23 */
        CHECK(s.node[1].bit_err + s.node[1].stuff_err >= 1);   /* 상대도 에러 플래그를 보고 에러 */
        CHECK(s.node[2].nrx == 0 && s.node[2].rx_stuff_err >= 4);
        CHECK(s.ndone == 2 && !s.done[0].ok && !s.done[1].ok && s.done[0].errs == 4 && s.done[1].errs == 4);
    }
    {   /* 3) 같은 ID, 다른 DLC → 제어 필드(DLC)에서 bit error */
        sim_init(&s, 3);
        CanFrame a = mk(0x0F0, false, false, 1, 0), b = mk(0x0F0, false, false, 3, 0);  /* DLC 0001 vs 0011 */
        can_send(&s, 0, &a); can_send(&s, 1, &b);
        sim_run(&s, 5000);
        CHECK(s.node[1].last_err == TXV_BIT_ERR && s.node[1].last_err_uidx == 17 && s.node[1].arb_lost == 0);
    }
    {   /* 4) 혼자인 노드 / listen-only 수신기만 있음 → ACK 에러, 상한 후 포기 */
        sim_init(&s, 2);
        s.node[1].listen_only = true;
        CanFrame f = mk(0x321, false, false, 1, 0);
        can_send(&s, 0, &f);
        sim_run(&s, 5000);
        CHECK(s.node[0].ack_err == 4 && s.ndone == 1 && !s.done[0].ok && s.node[1].nrx == 0);
        sim_init(&s, 2);
        s.max_err_retx = 0;                                 /* 재전송 없음 (one-shot 같은 정책) */
        can_send(&s, 0, &f); s.node[1].listen_only = false;
        sim_run(&s, 5000);
        CHECK(s.node[0].ack_err == 0 && s.done[0].ok && s.node[1].nrx == 1);
    }
    {   /* 5) 노이즈가 중재 중 rec 비트를 dom 으로 → 송신기는 "졌다"고 믿고 놓음 → 수신기 스터프 에러 → 재시도 성공 */
        sim_init(&s, 2);
        CanFrame f = mk(0x555, false, false, 1, 0);         /* 1010101 0101: 스터프 없음 */
        can_send(&s, 0, &f);
        s.force_dom_at = 1;                                 /* t=1 = ID[10] (rec) */
        sim_run(&s, 5000);
        printf("--- noise on arbitration bit ---\n%.*s", (int)s.loglen, s.log);
        CHECK(s.node[0].arb_lost == 1 && s.node[0].first_lost_uidx == 1);
        CHECK(s.node[1].rx_stuff_err == 1 && s.node[1].nrx == 1 && same_frame(&s.node[1].rx[0].f, &f));
        CHECK(s.ndone == 1 && s.done[0].ok && s.done[0].losses == 1 && s.done[0].errs == 0);
    }
    {   /* 6) 노이즈가 중재 중 스터프 비트(rec)를 dom 으로 → 패배가 아니라 stuff error */
        sim_init(&s, 2);
        CanFrame f = mk(0x000, false, false, 0, 0);
        can_send(&s, 0, &f);
        s.force_dom_at = 5;
        sim_run(&s, 5000);
        CHECK(s.node[0].stuff_err == 1 && s.node[0].arb_lost == 0 && s.done[0].ok && s.done[0].errs == 1);
    }

    /* ---- Part C: arb_key 순서 = 실제 중재 결과 (오라클 대조, 랜덤 20000쌍 + 필수 비교) ---- */
    {
        int bad = 0;
        for (int i = 0; i < 20000; i++) {
            uint32_t r = xorshift32();
            CanFrame a = mk((r & 1) ? xorshift32() & 0x1FFFFFFF : xorshift32() & 0x7FF, r & 1, (r & 2) != 0, 0, 0);
            CanFrame b = mk((r & 4) ? xorshift32() & 0x1FFFFFFF : xorshift32() & 0x7FF, (r & 4) != 0, (r & 8) != 0, 0, 0);
            if (r & 16) b.id = b.ext ? (((a.ext ? a.id >> 18 : a.id) & 0x7FF) << 18) | (b.id & 0x3FFFF)
                                     : ((a.ext ? a.id >> 18 : a.id) & 0x7FF);
            int w, d = oracle_first_diff(&a, &b, &w);
            uint32_t ka = arb_key(&a), kb = arb_key(&b);
            bad += d == 0 ? ka != kb : (w == 0) != (ka < kb);
        }
        CHECK(bad == 0);
        CanFrame sd = mk(0x123, false, false, 0, 0), sr = mk(0x123, false, true, 0, 0), xd = mk(0x123u << 18, true, false, 0, 0);
        CHECK(arb_key(&sd) < arb_key(&sr) && arb_key(&sr) < arb_key(&xd));
    }

    /* ---- Part C: 큐 정책 — 우선순위 역전 ---- */
    {
        CanFrame f400 = mk(0x400, false, false, 8, 1), f010 = mk(0x010, false, false, 8, 2);
        CanFrame f200[3] = { mk(0x200, false, false, 8, 3), mk(0x200, false, false, 8, 4), mk(0x200, false, false, 8, 5) };
        uint32_t want_fifo[5] = { 0x200, 0x200, 0x200, 0x400, 0x010 }, want_prio[5] = { 0x010, 0x200, 0x200, 0x200, 0x400 };
        for (int pol = 0; pol < 2; pol++) {
            sim_init(&s, 3);
            s.node[0].policy = pol ? Q_PRIO : Q_FIFO;
            can_send(&s, 0, &f400); can_send(&s, 0, &f010);
            for (int i = 0; i < 3; i++) can_send(&s, 1, &f200[i]);
            sim_run(&s, 10000);
            if (pol == 0) printf("--- node0 FIFO [0x400, 0x010] vs node1 3x 0x200 ---\n%.*s", (int)s.loglen, s.log);
            bool order = s.ndone == 5;
            for (int i = 0; order && i < 5; i++) order = s.done[i].ok && s.done[i].f.id == (pol ? want_prio[i] : want_fifo[i]);
            CHECK(order);
            /* 0x010 의 완료 시각 = 앞선 프레임들의 (len + 3) 합 + 자기 len − 1 */
            uint32_t t = 0; int i = 0;
            for (; i < s.ndone && s.done[i].f.id != 0x010; i++) t += (uint32_t)frame_len(&s.done[i].f) + 3;
            CHECK(i < s.ndone && s.done[i].t_done == t + (uint32_t)frame_len(&f010) - 1);
            CHECK(s.node[2].nrx == 5);
        }
    }

    /* ---- Part C: 과부하에서 높은 ID 굶주림, 최고 우선순위의 대기 상한 ---- */
    {
        const uint32_t ids[4] = { 0x100, 0x200, 0x300, 0x700 };
        int lmax = 0, l100;
        CanFrame fr[4];
        for (int i = 0; i < 4; i++) { fr[i] = mk(ids[i], false, false, 8, ids[i]); if (frame_len(&fr[i]) > lmax) lmax = frame_len(&fr[i]); }
        l100 = frame_len(&fr[0]);
        const uint32_t period[2] = { 1000, 300 };           /* 가벼운 부하 / 과부하 (0x100~0x300 각각 이 주기) */
        for (int h = 0; h < 2; h++) {
            sim_init(&s, 5);                                 /* node4 = 수신 전용 */
            for (int i = 0; i < 4; i++) s.node[i].policy = Q_PRIO;
            for (uint32_t t = 0; t < 30000; t++) {
                for (int i = 0; i < 3; i++) if (t % period[h] == (uint32_t)(i * 7)) can_send(&s, i, &fr[i]);
                if (t % 2000 == 11) can_send(&s, 3, &fr[3]);
                sim_step(&s);
            }
            IdStat *s1 = stat_of(&s, 0x100), *s7 = stat_of(&s, 0x700);
            printf("load %s: 0x100 sent=%d max_wait=%u | 0x700 sent=%d dropped=%d max_wait=%u (Lmax=%d)\n",
                   h ? "heavy" : "light", s1->sent, s1->max_wait, s7->sent, s7->dropped, s7->max_wait, lmax);
            CHECK(s1->max_wait <= (uint32_t)(lmax + 3 + l100));  /* 최고 우선순위: 블로킹 1프레임 + 자기 프레임 */
            if (h == 0) CHECK(s7->sent == 15 && s7->dropped == 0);
            else CHECK(s7->sent == 0 && s7->dropped == 7);       /* 과부하: 0x700 은 한 번도 못 나감, 큐 넘침 */
        }
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
