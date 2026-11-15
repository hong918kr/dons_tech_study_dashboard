/* COMM-M05 practice.c — 추가 연습 한 번에 검증
 *
 * 모델 = solution.c 의 인코더 + 버스 시뮬레이터 (정답 채운 복사본).
 * 연습 1: 패배 위치(비스터프 인덱스) → 필드 이름 "ID[4]" / "SRR" / "IDE" / "RTR" + 로그 한 줄
 * 연습 2: 같은 ID 리모트 프레임 둘 — DLC 같으면 둘 다 성공(버스엔 한 프레임), 다르면 DLC 에서 bit error
 * 연습 3: K 프레임 동시 시작 → 각 프레임 완료 시각을 닫힌 식으로 예측하고 시뮬레이션과 대조
 * 연습 4: 주기가 짧을수록 낮은 ID (deadline-monotonic) — 잘못 배정 vs 바르게 배정한 제어 메시지의 최악 대기
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
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

static uint32_t xs_state = 0x1234567u;
static uint32_t xorshift32(void) {
    uint32_t x = xs_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return xs_state = x;
}

static CanFrame mk(uint32_t id, bool ext, bool rtr, uint8_t dlc, uint32_t seed) {
    CanFrame f = { .id = id, .ext = ext, .rtr = rtr, .dlc = dlc };
    for (int i = 0; i < 8; i++) f.data[i] = (uint8_t)(seed * 131u + (uint32_t)i * 29u);
    return f;
}
static int frame_len(const CanFrame *f) { TxImage im; can_encode(f, &im); return im.len; }

/* ---- 연습 1: 패배 위치 → 필드 이름 ---- */
static int lost_field_name(bool ext, int uidx, char *out, size_t cap) {
    int k;
    if (!ext && uidx >= 1 && uidx <= 11)       k = snprintf(out, cap, "ID[%d]", 11 - uidx);
    else if (!ext && uidx == 12)               k = snprintf(out, cap, "RTR");
    else if (ext && uidx >= 1 && uidx <= 11)   k = snprintf(out, cap, "ID[%d]", 29 - uidx);   /* ID-A = ID[28:18] */
    else if (ext && uidx == 12)                k = snprintf(out, cap, "SRR");
    else if (ext && uidx == 13)                k = snprintf(out, cap, "IDE");
    else if (ext && uidx >= 14 && uidx <= 31)  k = snprintf(out, cap, "ID[%d]", 31 - uidx);   /* ID-B = ID[17:0] */
    else if (ext && uidx == 32)                k = snprintf(out, cap, "RTR");
    else return -1;                                                                       /* 중재 필드 밖 */
    return (k < 0 || (size_t)k >= cap) ? -1 : k;
}

/* 2노드 경주 후 패자의 필드 이름 */
static const char *race_lost_name(const CanFrame *a, const CanFrame *b, char *buf, size_t cap) {
    static Sim s;
    sim_init(&s, 2);
    can_send(&s, 0, a); can_send(&s, 1, b);
    sim_run(&s, 3000);
    for (int i = 0; i < 2; i++)
        if (s.node[i].arb_lost == 1) {
            const CanFrame *f = i == 0 ? a : b;
            if (lost_field_name(f->ext, s.node[i].first_lost_uidx, buf, cap) < 0) return "?";
            return buf;
        }
    return "none";
}

/* ---- 연습 3: 동시 시작한 K 프레임의 완료 시각 예측 (arb_key 오름차순으로 줄 선다) ---- */
static void predict_done(const CanFrame *fr, int k, uint32_t *done_t, int *order) {
    for (int i = 0; i < k; i++) order[i] = i;
    for (int i = 1; i < k; i++)
        for (int j = i; j > 0 && arb_key(&fr[order[j]]) < arb_key(&fr[order[j - 1]]); j--) {
            int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
        }
    uint32_t t = 0;
    for (int r = 0; r < k; r++) {
        int L = frame_len(&fr[order[r]]);
        done_t[order[r]] = t + (uint32_t)L - 1;           /* SOF 가 t 에 시작해 L 비트 → 마지막 EOF 비트 */
        t += (uint32_t)L + 3;                             /* + IFS 3 → 다음 SOF */
    }
}

/* ---- 연습 4: deadline-monotonic ID 배정 — 주기가 짧을수록 낮은 ID ---- */
static void assign_ids_dm(const uint32_t *period, int n, uint32_t base, uint32_t step, uint32_t *ids) {
    for (int i = 0; i < n; i++) {
        int rank = 0;
        for (int j = 0; j < n; j++)
            if (period[j] < period[i] || (period[j] == period[i] && j < i)) rank++;
        ids[i] = base + (uint32_t)rank * step;
    }
}

/* 4노드가 각자 주기 메시지 1개, t=0 에 동시 출발(critical instant). 노드 0 의 최대 대기 */
static uint32_t worst_wait_node0(const uint32_t *period, const uint32_t *ids, uint32_t T) {
    static Sim s;
    sim_init(&s, 5);
    CanFrame f[4];
    for (int i = 0; i < 4; i++) { f[i] = mk(ids[i], false, false, 8, ids[i]); s.node[i].policy = Q_PRIO; }
    for (uint32_t t = 0; t < T; t++) {
        for (int i = 0; i < 4; i++) if (t % period[i] == 0) can_send(&s, i, &f[i]);
        sim_step(&s);
    }
    return stat_of(&s, ids[0])->max_wait;
}

int main(void) {
    char buf[16];

    /* 연습 1 */
    {
        CanFrame s065 = mk(0x065, false, false, 0, 0), s070 = mk(0x070, false, false, 0, 0);
        CanFrame sd = mk(0x123, false, false, 1, 0), sr = mk(0x123, false, true, 1, 0);
        CanFrame xd = mk(0x123u << 18, true, false, 1, 0), xr = mk(0x123u << 18, true, true, 1, 0);
        CanFrame xb = mk((0x123u << 18) | 0x20, true, false, 1, 0);   /* ID-B bit5 만 다름 */
        CHECK(strcmp(race_lost_name(&s065, &s070, buf, sizeof buf), "ID[4]") == 0);
        CHECK(strcmp(race_lost_name(&sd, &xd, buf, sizeof buf), "SRR") == 0);
        CHECK(strcmp(race_lost_name(&sr, &xd, buf, sizeof buf), "IDE") == 0);
        CHECK(strcmp(race_lost_name(&xd, &xb, buf, sizeof buf), "ID[5]") == 0);
        CHECK(strcmp(race_lost_name(&sd, &sr, buf, sizeof buf), "RTR") == 0);
        CHECK(strcmp(race_lost_name(&xd, &xr, buf, sizeof buf), "RTR") == 0);
        CHECK(lost_field_name(false, 13, buf, sizeof buf) == -1 && lost_field_name(true, 1, buf, sizeof buf) == 6
              && strcmp(buf, "ID[28]") == 0 && lost_field_name(true, 14, buf, 3) == -1);
        printf("ex1: std 0x065 vs 0x070 -> loser lost at %s\n", race_lost_name(&s065, &s070, buf, sizeof buf));
    }

    /* 연습 2 */
    {
        static Sim s;
        CanFrame r2 = mk(0x3A0, false, true, 2, 0), r4 = mk(0x3A0, false, true, 4, 0);
        sim_init(&s, 3);
        can_send(&s, 0, &r2); can_send(&s, 1, &r2);
        uint32_t bits = sim_run(&s, 3000);
        CHECK(s.ndone == 2 && s.done[0].ok && s.done[1].ok && s.node[2].nrx == 1 && bits == (uint32_t)frame_len(&r2) + 3);
        sim_init(&s, 3);
        can_send(&s, 0, &r2); can_send(&s, 1, &r4);         /* DLC 0010 vs 0100: uidx 16 에서 갈림 */
        sim_run(&s, 3000);
        CHECK(s.node[0].arb_lost == 0 && s.node[1].arb_lost == 0);
        CHECK(s.node[1].last_err == TXV_BIT_ERR && s.node[1].last_err_uidx == 16);
        CHECK(s.node[2].nrx == 0 && s.ndone == 2 && !s.done[0].ok && !s.done[1].ok);
        printf("ex2: remote DLC2 vs DLC4 -> node1 %s at uidx %d, receiver got %d frames\n",
               verdict_name(s.node[1].last_err), s.node[1].last_err_uidx, s.node[2].nrx);
    }

    /* 연습 3 */
    {
        static Sim s;
        int bad = 0;
        for (int tr = 0; tr < 400; tr++) {
            int k = 2 + (int)(xorshift32() % 6);
            CanFrame fr[7];
            for (int i = 0; i < k; i++) {
                for (;;) {
                    uint32_t r = xorshift32();
                    bool ext = (r & 1) != 0;
                    fr[i] = mk(ext ? xorshift32() & 0x1FFFFFFF : xorshift32() & 0x7FF, ext, (r & 2) != 0, (uint8_t)((r >> 3) % 9), r);
                    bool dup = false;
                    for (int j = 0; j < i; j++) dup |= arb_key(&fr[j]) == arb_key(&fr[i]);
                    if (!dup) break;
                }
            }
            uint32_t pred[7]; int ord[7];
            predict_done(fr, k, pred, ord);
            sim_init(&s, k);
            for (int i = 0; i < k; i++) can_send(&s, i, &fr[i]);
            sim_run(&s, 20000);
            bool ok = s.ndone == k;
            for (int i = 0; ok && i < k; i++) ok = s.done[i].node == ord[i] && s.done[i].t_done == pred[ord[i]];
            bad += !ok;
        }
        CHECK(bad == 0);
        CanFrame ex[3] = { mk(0x065, false, false, 8, 1), mk(0x063, false, false, 8, 2), mk(0x070, false, false, 8, 3) };
        uint32_t pred[3]; int ord[3];
        predict_done(ex, 3, pred, ord);
        printf("ex3: 3x DLC8 (len %d/%d/%d): 0x063 done bit %u, 0x065 %u, 0x070 %u -> last at 500k = %u us\n",
               frame_len(&ex[0]), frame_len(&ex[1]), frame_len(&ex[2]), pred[1], pred[0], pred[2], (pred[2] + 1) * 2);
        CHECK(ord[0] == 1 && ord[1] == 0 && ord[2] == 2);
    }

    /* 연습 4 */
    {
        const uint32_t period[4] = { 500, 1000, 2000, 5000 };   /* 노드 0 = 500비트(1 kHz @500k) 제어 루프 */
        uint32_t right[4], wrong[4] = { 0x400, 0x100, 0x200, 0x300 };
        assign_ids_dm(period, 4, 0x100, 0x100, right);
        CHECK(right[0] == 0x100 && right[1] == 0x200 && right[2] == 0x300 && right[3] == 0x400);
        uint32_t ww = worst_wait_node0(period, wrong, 20000), wr = worst_wait_node0(period, right, 20000);
        int lmax = 0;
        for (int i = 0; i < 4; i++) { CanFrame f = mk(right[i], false, false, 8, right[i]); if (frame_len(&f) > lmax) lmax = frame_len(&f); }
        CanFrame f0 = mk(0x100, false, false, 8, 0x100);
        printf("ex4: control msg worst wait: wrong IDs %u bits, deadline-monotonic %u bits (bound %d)\n",
               ww, wr, lmax + 3 + frame_len(&f0));
        CHECK(wr <= (uint32_t)(lmax + 3 + frame_len(&f0)));
        CHECK(ww > 250 && wr < 250);                        /* 마감 = 주기의 절반 250비트 */
        uint32_t tie[3] = { 100, 50, 100 }, tid[3];
        assign_ids_dm(tie, 3, 0x10, 1, tid);
        CHECK(tid[1] == 0x10 && tid[0] == 0x11 && tid[2] == 0x12);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
