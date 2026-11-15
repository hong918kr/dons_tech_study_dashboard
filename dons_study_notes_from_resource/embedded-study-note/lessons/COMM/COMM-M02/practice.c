/* COMM-M02 추가 연습 — 한 번에 검증
 * (모델은 solution.c 와 같다. 연습 3 을 위해 Master.gap 필드 + m_step 의 free 조건 한 줄만 다르다.)
 * 연습 1: arb_predict() — 시뮬레이션 없이 바이트만 보고 승자와 패배 위치 예측, 랜덤 쌍으로 시뮬레이션과 대조
 * 연습 2: SCL 동기화 공식 — k 마스터의 버스 주기 = max(t_low) + min(t_high), 실제 ns 계산과 Sm/Fm 규격 위반 판정
 * 연습 3: 양보 간격(gap) — 낮은 주소 마스터가 연달아 보낼 때 높은 주소 마스터의 굶주림과 해결
 * 연습 4: 승자에겐 중재가 "무손실" — 충돌한 트레이스를 디코드하면 승자 혼자 보낸 것과 똑같다
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 상수 · 결과 코드 (주어짐) ---------------- */
enum { MAX_NODES = 8, MAX_MASTERS = 3, MAX_SLAVES = 4, MAX_MSG = 6, QCAP = 8,
       TRACE_CAP = 1024, LOG_CAP = 128, DONE_CAP = 64, RX_CAP = 256 };

enum { I2C_PENDING = 1, I2C_OK = 0, I2C_ERR_ARG = -1, I2C_ERR_NACK_ADDR = -2, I2C_ERR_NACK_DATA = -3,
       I2C_ERR_ARB_LOST = -4, I2C_ERR_BUS_BUSY = -5, I2C_ERR_TIMEOUT = -6 };

/* Linux errno 값 (asm-generic/errno-base.h, errno.h). 맥의 <errno.h> 값과 달라서 직접 둔다. */
enum { LNX_EIO = 5, LNX_ENXIO = 6, LNX_EAGAIN = 11, LNX_EBUSY = 16, LNX_EINVAL = 22, LNX_ETIMEDOUT = 110 };

/* ---------------- 이벤트 로그 · 트레이스 · 완료 기록 (주어짐) ---------------- */
typedef enum { EV_START, EV_ARB_LOST, EV_RETRY, EV_DONE, EV_S_START, EV_S_ADDR, EV_S_RX, EV_S_TX, EV_S_STOP } EvKind;
typedef struct { uint32_t t; char who; uint8_t k; int a, b, c; } Ev;

typedef struct {
    char     scl[TRACE_CAP + 1], sda[TRACE_CAP + 1];          /* 버스 레벨 '0'/'1' */
    char     dscl[MAX_NODES][TRACE_CAP + 1], dsda[MAX_NODES][TRACE_CAP + 1]; /* 노드별 드라이버 */
    uint32_t n;
} Trace;

typedef struct { uint8_t addr7; bool rd; uint8_t n; uint8_t w[MAX_MSG]; uint32_t at; } Req;

typedef struct {
    char     who;
    Req      req;
    uint8_t  r[MAX_MSG];
    int      status, attempts, n_lost, lost_byte, lost_bit;   /* lost_* = 마지막 패배 위치, 없으면 -1 */
    uint32_t start_t, done_t, wait_ticks;                      /* start_t = 마지막 시도의 START 틱 */
} Done;

typedef struct Master Master;
typedef struct Slave Slave;

typedef struct {
    uint8_t  sda_drv[MAX_NODES], scl_drv[MAX_NODES];   /* 1 = 놓음, 0 = 당김 */
    int      nnodes;
    uint8_t  sda, scl, psda, pscl;                      /* L(t), L(t-1) */
    uint32_t t;
    uint32_t starts, stops;                             /* 관찰자: 버스 위 START / STOP 수 */
    Master  *m[MAX_MASTERS]; int nm;
    Slave   *s[MAX_SLAVES];  int ns;
    int      fault_node;                                /* -1 = 없음. 고장 노드: 지정 구간에 선을 붙잡음 */
    uint32_t f_sda_from, f_sda_to, f_scl_from, f_scl_to;
    Trace   *tr;                                        /* NULL = 기록 안 함 */
    Ev      *log;  int nlog, log_cap;
    Done    *done; int ndone, done_cap;
} Bus;

static void log_ev(Bus *b, char who, EvKind k, int a, int bb, int c) {
    if (!b->log || b->nlog >= b->log_cap) return;
    Ev *e = &b->log[b->nlog++];
    e->t = b->t; e->who = who; e->k = (uint8_t)k; e->a = a; e->b = bb; e->c = c;
}

/* ---------------- 마스터 ---------------- */
typedef enum { M_IDLE, M_WAIT_FREE, M_START_HOLD, M_LOW, M_WAIT_HIGH, M_HIGH, M_STOP_HIGH } MPhase;

typedef struct { bool busy; uint32_t idle; } Mon;    /* 버스 모니터: START~STOP = busy, idle = 연속 idle 틱 */

struct Master {
    char     name; int node;
    int      t_low, t_high, t_hd_sta, t_su_sto, t_buf;   /* 틱 단위 타이밍 (t_low >= 2) */
    int      max_retries;                                /* 패배 후 재시도 횟수 상한 */
    bool     keep_clock;                                 /* 정책: 진 뒤 그 바이트 끝(9번째 클럭)까지 클럭을 계속 낼까 */
    uint32_t max_wait;                                   /* 버스 free 를 기다리는 최대 틱 (시도마다) */
    Mon      mon;
    Req      q[QCAP]; int qh, qn;                        /* 요청 큐 (q[qh] = 현재 요청) */
    MPhase   ph; int cnt; uint32_t wait, wait_total;
    int      byte_i, bit_i;                              /* byte 0 = 주소 바이트, bit 0..7 = 데이터, 8 = ACK 슬롯 */
    uint8_t  sh;                                         /* 수신 시프트 레지스터 */
    bool     lost, stopping;
    int      result, attempts, n_lost, lost_byte, lost_bit;
    uint32_t start_t;
    uint8_t  rbuf[MAX_MSG];
    uint32_t n_finished;
    Done     last;
    int      gap;                                        /* 연습 3: 내 다음 메시지 전 추가로 쉬는 틱 (양보) */
};

static uint8_t m_cur_byte(const Master *m) {            /* 지금 바이트: 주소+R/W 또는 쓰기 데이터 */
    const Req *r = &m->q[m->qh];
    return m->byte_i == 0 ? (uint8_t)(r->addr7 << 1 | (r->rd ? 1 : 0)) : r->w[m->byte_i - 1];
}

/* 요청 하나를 끝낸다: 선을 놓고, 기록하고, 큐에서 뺀다 */
static void m_finish(Master *m, Bus *b, int status) {
    b->sda_drv[m->node] = 1;
    b->scl_drv[m->node] = 1;
    Done d;
    memset(&d, 0, sizeof d);
    d.who = m->name; d.req = m->q[m->qh]; d.status = status; d.attempts = m->attempts;
    d.n_lost = m->n_lost; d.lost_byte = m->lost_byte; d.lost_bit = m->lost_bit;
    d.start_t = m->start_t; d.done_t = b->t + 1; d.wait_ticks = m->wait_total;
    memcpy(d.r, m->rbuf, sizeof d.r);
    m->last = d;
    if (b->done && b->ndone < b->done_cap) b->done[b->ndone++] = d;
    log_ev(b, m->name, EV_DONE, status, m->attempts, 0);
    m->qh = (m->qh + 1) % QCAP; m->qn--;
    m->ph = M_IDLE; m->n_finished++;
}

/* 시도 하나 시작: 버스가 free 라고 판단한 틱에 SDA 를 당긴다 (SCL=1 중 SDA↓ = START) */
static void m_begin_attempt(Master *m, Bus *b) {
    m->attempts++;
    m->byte_i = 0; m->bit_i = 0; m->sh = 0; m->lost = false; m->stopping = false;
    m->result = I2C_PENDING; m->cnt = 0; m->start_t = b->t + 1;
    b->sda_drv[m->node] = 0;
    m->ph = M_START_HOLD;
    log_ev(b, m->name, EV_START, m->attempts, 0, 0);
}

/* ===== Part B-1: 버스 모니터 (TODO) =====
 * 매 틱 L(t-1)→L(t) 를 보고: SCL 1→1 인데 SDA 가 바뀌면 ↓ = START → busy, ↑ = STOP → free.
 * idle = busy 가 아니고 SDA=SCL=1 인 연속 틱 수 (아니면 0). free = !busy && idle >= t_buf. */
static void mon_step(Mon *mo, const Bus *b) {
    if (b->pscl && b->scl && b->psda != b->sda) mo->busy = (b->sda == 0);
    if (!mo->busy && b->sda && b->scl) mo->idle++;
    else mo->idle = 0;
}
static bool mon_free(const Mon *mo, int t_buf) {
    return !mo->busy && mo->idle >= (uint32_t)t_buf;
}

/* ===== Part A-2: 이번 클럭에 SDA 로 무엇을 낼까 (TODO) — SCL low 첫 틱에 한 번 불린다 =====
 * STOP 준비면 0, 중재에 진 뒤면 1(놓음), 데이터 비트면 보내는 바이트의 비트 (받는 바이트면 1),
 * 9번째(ACK) 클럭이면 보내는 쪽은 1(놓고 ACK 읽기), 받는 쪽은 더 받을 거면 0(ACK) 마지막이면 1(NACK). */
static void m_setup_bit(Master *m, Bus *b) {
    const Req *r = &m->q[m->qh];
    bool tx = (m->byte_i == 0) || !r->rd;               /* 이 바이트를 마스터가 보내나 */
    uint8_t v;
    if (m->stopping) v = 0;
    else if (m->lost) v = 1;
    else if (m->bit_i < 8) v = tx ? (uint8_t)((m_cur_byte(m) >> (7 - m->bit_i)) & 1) : 1;
    else v = tx ? 1 : (uint8_t)(m->byte_i < r->n ? 0 : 1);
    b->sda_drv[m->node] = v;
}

static void m_arb_lost(Master *m, Bus *b);
static void m_give_up(Master *m, Bus *b);

/* ===== Part A-3: SCL 상승을 본 틱의 처리 (TODO) =====
 * 진 뒤 클럭만 내는 중이면 비트만 세고 9번째에서 m_give_up.
 * 이 클럭의 SDA 를 내가 내는데(보내는 바이트의 데이터 비트, 받는 바이트의 ACK 비트) 1 을 냈는데 0 을 읽으면 m_arb_lost.
 * 데이터 비트: 받는 바이트면 시프트. ACK 슬롯: 보내는 쪽 NACK → result = NACK_ADDR/NACK_DATA, STOP 으로;
 * ACK 이고 마지막 바이트면 result = OK, STOP 으로. 받는 쪽은 rbuf 에 저장. 그 다음 바이트로. */
static void m_sample(Master *m, Bus *b) {
    const Req *r = &m->q[m->qh];
    bool tx = (m->byte_i == 0) || !r->rd;
    if (m->lost) {                                      /* 정책 keep_clock: 클럭만 마저 낸다 */
        if (m->bit_i++ == 8) m_give_up(m, b);
        return;
    }
    bool mine = (m->bit_i < 8) == tx;                   /* 데이터 비트는 송신자, ACK 비트는 수신자가 낸다 */
    if (mine && b->sda_drv[m->node] == 1 && b->sda == 0) { m_arb_lost(m, b); return; }
    if (m->bit_i < 8) {
        if (!tx) m->sh = (uint8_t)(m->sh << 1 | b->sda);
        m->bit_i++;
        return;
    }
    if (tx && b->sda) {                                 /* NACK */
        m->result = m->byte_i == 0 ? I2C_ERR_NACK_ADDR : I2C_ERR_NACK_DATA;
        m->stopping = true;
    } else {
        if (!tx) m->rbuf[m->byte_i - 1] = m->sh;
        if (m->byte_i == r->n) { m->result = I2C_OK; m->stopping = true; }
    }
    m->byte_i++; m->bit_i = 0; m->sh = 0;
}

/* ===== Part A-1: SCL 생성 + 클럭 동기화 (TODO) =====
 * M_START_HOLD / M_HIGH : SCL=1 을 t_hd_sta / t_high 틱 센 뒤 SCL 을 당긴다(→ M_LOW, cnt=0).
 *                        세는 도중 SCL 이 0 이면 = 다른 마스터가 먼저 당김 → 곧장 low 단계 (이 틱이 low 1틱째).
 * M_LOW       : SCL 을 당긴 채 cnt++. cnt==1 (low 첫 틱) 에 m_setup_bit. cnt >= t_low 면 SCL 을 놓고 M_WAIT_HIGH.
 * M_WAIT_HIGH : SCL 이 실제로 1 이 될 때까지 기다림. 1 을 본 틱 = 상승: stopping 이면 M_STOP_HIGH,
 *               아니면 M_HIGH + m_sample (패배 등으로 단계가 바뀌면 끝). 이 틱이 high 1틱째.
 * M_STOP_HIGH : SCL=1 을 t_su_sto 틱 센 뒤 SDA 를 놓고(SCL=1 중 SDA↑ = STOP) m_finish(result). */
static void m_clock(Master *m, Bus *b) {
    uint8_t *scl_out = &b->scl_drv[m->node];
    if (m->ph == M_WAIT_HIGH) {
        if (!b->scl) return;                            /* 놓았는데 아직 0 = 남이 잡고 있다 → 기다림 */
        m->cnt = 0;
        if (m->stopping) m->ph = M_STOP_HIGH;
        else {
            m->ph = M_HIGH;
            m_sample(m, b);                             /* 상승 엣지 = 샘플 + 중재 비교 */
            if (m->ph != M_HIGH) return;
        }
    }
    switch (m->ph) {
    case M_START_HOLD:
    case M_HIGH:
        if (!b->scl) { m->ph = M_LOW; m->cnt = 0; break; }     /* 동기화: 누가 high 를 먼저 끝냄 */
        if (++m->cnt >= (m->ph == M_HIGH ? m->t_high : m->t_hd_sta)) {
            *scl_out = 0; m->ph = M_LOW; m->cnt = 0;
        }
        return;
    case M_STOP_HIGH:
        if (++m->cnt >= m->t_su_sto) { b->sda_drv[m->node] = 1; m_finish(m, b, m->result); }
        return;
    case M_LOW:
        break;
    default:
        return;
    }
    *scl_out = 0;                                       /* low 단계: 내 몫의 low 를 센다 */
    if (++m->cnt == 1) m_setup_bit(m, b);               /* SDA 는 SCL low 첫 틱에만 바꾼다 */
    if (m->cnt >= m->t_low) { *scl_out = 1; m->ph = M_WAIT_HIGH; }
}

/* ===== Part B-2: 중재 패배 처리 + 재시도 (TODO) =====
 * m_arb_lost: 패배 기록(n_lost++, lost_byte/bit = 지금 위치), 로그 EV_ARB_LOST(byte, bit), SDA 즉시 놓기.
 *             keep_clock 이고 아직 데이터 비트(bit_i < 8)면 lost=true, bit_i++ 하고 클럭만 계속.
 *             아니면 m_give_up.
 * m_give_up : SDA·SCL 놓고 lost=false. attempts > max_retries 면 m_finish(ARB_LOST),
 *             아니면 M_WAIT_FREE (wait=0) 로 가서 STOP + t_buf 뒤 다시 START. 로그 EV_RETRY(attempts). */
static void m_arb_lost(Master *m, Bus *b) {
    m->n_lost++;
    m->lost_byte = m->byte_i; m->lost_bit = m->bit_i;
    log_ev(b, m->name, EV_ARB_LOST, m->byte_i, m->bit_i, 0);
    b->sda_drv[m->node] = 1;
    if (m->keep_clock && m->bit_i < 8) { m->lost = true; m->bit_i++; return; }
    m_give_up(m, b);
}
static void m_give_up(Master *m, Bus *b) {
    b->sda_drv[m->node] = 1;
    b->scl_drv[m->node] = 1;
    m->lost = false;
    if (m->attempts > m->max_retries) { m_finish(m, b, I2C_ERR_ARB_LOST); return; }
    m->ph = M_WAIT_FREE; m->wait = 0;
    log_ev(b, m->name, EV_RETRY, m->attempts, m->max_retries, 0);
}

/* 마스터 한 틱 (주어짐) */
static void m_step(Master *m, Bus *b) {
    mon_step(&m->mon, b);
    switch (m->ph) {
    case M_IDLE:
        if (m->qn == 0 || b->t < m->q[m->qh].at) break;
        m->ph = M_WAIT_FREE; m->wait = 0; m->wait_total = 0;
        m->attempts = 0; m->n_lost = 0; m->lost_byte = m->lost_bit = -1;
        memset(m->rbuf, 0, sizeof m->rbuf);
        /* fallthrough */
    case M_WAIT_FREE:
        if (mon_free(&m->mon, m->t_buf + (m->attempts == 0 ? m->gap : 0))) { m_begin_attempt(m, b); break; }  /* 연습 3 */
        m->wait++; m->wait_total++;
        if (m->wait > m->max_wait) m_finish(m, b, I2C_ERR_BUS_BUSY);   /* 버스가 끝내 free 가 안 됨 */
        break;
    default:
        m_clock(m, b);
        break;
    }
}

/* ---------------- 슬레이브 (COMM-B06 모델 축약판, 주어짐) ---------------- */
typedef enum { S_IDLE, S_ADDR, S_ADDR_ACK, S_RX, S_RX_ACK, S_TX, S_TX_ACK } SState;

struct Slave {
    int      node; uint8_t addr;
    SState   st; uint8_t sh; int nbits; bool rw, master_ack;
    uint8_t  rx[RX_CAP]; size_t rx_len, rx_limit;       /* rx_limit 개 넘게 오면 NACK */
    uint8_t  tx_seed; uint32_t tx_count;                /* 읽기 응답 = tx_seed + tx_count++ */
    uint32_t starts, stops, addr_matches;
};

/* 슬레이브 한 틱: SCL high 중 SDA 변화 = START/STOP, SCL↑ = 샘플, SCL↓ = 자기 SDA 변경 */
static void sl_step(Slave *s, Bus *b) {
    uint8_t *out = &b->sda_drv[s->node];
    uint8_t scl = b->scl, sda = b->sda;
    if (b->pscl && scl && b->psda != sda) {
        if (!sda) { s->starts++; s->st = S_ADDR; s->nbits = 0; s->sh = 0; }
        else { s->stops++; s->st = S_IDLE; }
        *out = 1;
    } else if (!b->pscl && scl) {
        if (s->st == S_ADDR || s->st == S_RX) { s->sh = (uint8_t)(s->sh << 1 | sda); s->nbits++; }
        else if (s->st == S_TX) s->nbits++;
        else if (s->st == S_TX_ACK) s->master_ack = (sda == 0);
    } else if (b->pscl && !scl) {
        switch (s->st) {
        case S_ADDR:
            if (s->nbits == 8) {
                bool match = (s->sh >> 1) == s->addr;
                log_ev(b, 's', EV_S_ADDR, s->sh, match, s->addr);
                if (match) { s->rw = s->sh & 1; s->addr_matches++; *out = 0; s->st = S_ADDR_ACK; }
                else s->st = S_IDLE;
            }
            break;
        case S_RX:
            if (s->nbits == 8) {
                bool ack = s->rx_len < s->rx_limit && s->rx_len < RX_CAP;
                if (ack) { s->rx[s->rx_len++] = s->sh; *out = 0; }
                log_ev(b, 's', EV_S_RX, s->sh, ack, s->addr);
                s->st = S_RX_ACK;
            }
            break;
        case S_ADDR_ACK:
        case S_RX_ACK:
            s->nbits = 0; s->sh = 0;
            if (s->st == S_ADDR_ACK && s->rw) {
                s->sh = (uint8_t)(s->tx_seed + s->tx_count++);
                log_ev(b, 's', EV_S_TX, s->sh, 0, s->addr);
                *out = (uint8_t)(s->sh >> 7); s->st = S_TX;
            } else { *out = 1; s->st = S_RX; }
            break;
        case S_TX:
            if (s->nbits < 8) *out = (uint8_t)((s->sh >> (7 - s->nbits)) & 1);
            else { *out = 1; s->st = S_TX_ACK; }
            break;
        case S_TX_ACK:
            if (s->master_ack) {
                s->sh = (uint8_t)(s->tx_seed + s->tx_count++);
                log_ev(b, 's', EV_S_TX, s->sh, 0, s->addr);
                s->nbits = 0; *out = (uint8_t)(s->sh >> 7); s->st = S_TX;
            } else { *out = 1; s->st = S_IDLE; }
            break;
        default:
            break;
        }
    }
}

/* ---------------- 버스 한 틱 (주어짐) ---------------- */
static void bus_resolve(Bus *b) {
    uint8_t sda = 1, scl = 1;
    for (int i = 0; i < b->nnodes; i++) { sda &= b->sda_drv[i]; scl &= b->scl_drv[i]; }
    b->sda = sda; b->scl = scl;
}
static void trace_rec(Bus *b) {
    Trace *tr = b->tr;
    if (!tr || tr->n >= TRACE_CAP) return;
    tr->scl[tr->n] = (char)('0' + b->scl); tr->sda[tr->n] = (char)('0' + b->sda);
    for (int i = 0; i < MAX_NODES; i++) {               /* 없는 노드 = 놓음(1) */
        tr->dscl[i][tr->n] = (char)('0' + b->scl_drv[i]); tr->dsda[i][tr->n] = (char)('0' + b->sda_drv[i]);
        tr->dscl[i][tr->n + 1] = tr->dsda[i][tr->n + 1] = '\0';
    }
    tr->n++;
    tr->scl[tr->n] = tr->sda[tr->n] = '\0';
}
static void bus_step(Bus *b) {
    for (int i = 0; i < b->nm; i++) m_step(b->m[i], b);
    for (int i = 0; i < b->ns; i++) sl_step(b->s[i], b);
    if (b->fault_node >= 0) {
        int f = b->fault_node;
        uint32_t nt = b->t + 1;
        b->sda_drv[f] = (uint8_t)!(nt >= b->f_sda_from && nt < b->f_sda_to);
        b->scl_drv[f] = (uint8_t)!(nt >= b->f_scl_from && nt < b->f_scl_to);
    }
    b->psda = b->sda; b->pscl = b->scl;
    bus_resolve(b);
    b->t++;
    if (b->pscl && b->scl && b->psda != b->sda) {         /* 관찰자 (로직 애널라이저) */
        if (b->sda) { b->stops++; log_ev(b, '*', EV_S_STOP, 0, 0, 0); }
        else        { b->starts++; log_ev(b, '*', EV_S_START, 0, 0, 0); }
    }
    trace_rec(b);
}
static bool all_idle(const Bus *b) {
    for (int i = 0; i < b->nm; i++) if (b->m[i]->ph != M_IDLE || b->m[i]->qn) return false;
    return b->sda && b->scl && b->psda && b->pscl;      /* 모든 노드가 STOP 뒤 idle 을 한 번은 봤다 */
}
static bool run_until_idle(Bus *b, uint32_t cap) {     /* 상한 cap 틱 — 스텁이어도 반드시 끝난다 */
    for (uint32_t i = 0; i < cap; i++) { if (all_idle(b)) return true; bus_step(b); }
    return all_idle(b);
}

static int m_submit(Master *m, uint8_t addr7, bool rd, const uint8_t *w, size_t n, uint32_t at) {
    if (addr7 > 0x7F || n > MAX_MSG || (rd && n == 0) || (!rd && n > 0 && !w) || m->qn == QCAP) return I2C_ERR_ARG;
    Req *r = &m->q[(m->qh + m->qn) % QCAP];
    memset(r, 0, sizeof *r);
    r->addr7 = addr7; r->rd = rd; r->n = (uint8_t)n; r->at = at;
    if (!rd && n) memcpy(r->w, w, n);
    m->qn++;
    return 0;
}

/* ===== Part C: 블로킹 드라이버 API (TODO) =====
 * 마스터 m 에 요청을 넣고(지금 틱), m 이 그 요청을 끝낼 때까지 bus_step (다른 마스터·슬레이브도 같이 돈다).
 * 상한: (max_retries+1) * (max_wait + 2000) 틱 — 넘으면 m_finish(TIMEOUT) 로 선을 놓고 -ETIMEDOUT.
 * 반환 (Linux i2c fault-codes 식): 0 / -EINVAL(인자) / -ENXIO(주소 NACK) / -EIO(데이터 NACK)
 *       / -EAGAIN(중재 패배, 재시도 소진) / -EBUSY(버스가 free 가 안 됨) / -ETIMEDOUT.
 * info (NULL 가능): attempts, n_lost, lost_byte, lost_bit, wait_ticks, ticks(걸린 틱). 읽기면 rd 에 n 바이트 복사. */
typedef struct { int attempts, n_lost, lost_byte, lost_bit; uint32_t wait_ticks, ticks; } XferInfo;

static int i2c_mm_xfer(Bus *b, Master *m, uint8_t addr7, bool rd, const uint8_t *w, uint8_t *r, size_t n,
                       XferInfo *info) {
    if (rd && !r) return -LNX_EINVAL;
    if (m_submit(m, addr7, rd, w, n, b->t) != 0) return -LNX_EINVAL;
    uint32_t done0 = m->n_finished, t0 = b->t;
    uint32_t cap = (uint32_t)(m->max_retries + 1) * (m->max_wait + 2000u);
    while (m->n_finished == done0 && b->t - t0 < cap) bus_step(b);
    if (m->n_finished == done0) m_finish(m, b, I2C_ERR_TIMEOUT);   /* 큐 맨 앞 = 이 요청 (큐가 비어 있었다고 가정) */
    const Done *d = &m->last;
    if (info) {
        info->attempts = d->attempts; info->n_lost = d->n_lost; info->lost_byte = d->lost_byte;
        info->lost_bit = d->lost_bit; info->wait_ticks = d->wait_ticks; info->ticks = b->t - t0;
    }
    if (rd && d->status == I2C_OK) memcpy(r, d->r, n);
    switch (d->status) {
    case I2C_OK:            return 0;
    case I2C_ERR_NACK_ADDR: return -LNX_ENXIO;
    case I2C_ERR_NACK_DATA: return -LNX_EIO;
    case I2C_ERR_ARB_LOST:  return -LNX_EAGAIN;
    case I2C_ERR_BUS_BUSY:  return -LNX_EBUSY;
    case I2C_ERR_TIMEOUT:   return -LNX_ETIMEDOUT;
    default:                return -LNX_EINVAL;
    }
}

/* ---------------- 테스트 하네스 (주어짐) ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static Trace g_tr;
static Ev    g_log[LOG_CAP];
static Done  g_done[DONE_CAP];

static void bus_init(Bus *b, bool trace) {
    memset(b, 0, sizeof *b);
    b->sda = b->scl = b->psda = b->pscl = 1;
    memset(b->sda_drv, 1, sizeof b->sda_drv); memset(b->scl_drv, 1, sizeof b->scl_drv);
    b->fault_node = -1;
    b->log = g_log; b->log_cap = LOG_CAP;
    b->done = g_done; b->done_cap = DONE_CAP;
    if (trace) { b->tr = &g_tr; g_tr.n = 0; trace_rec(b); }   /* trace[0] = L(0) */
}
static Master *bus_add_master(Bus *b, Master *m, char name, int t_low, int t_high) {
    memset(m, 0, sizeof *m);
    m->name = name; m->node = b->nnodes++;
    b->sda_drv[m->node] = b->scl_drv[m->node] = 1;
    m->t_low = t_low; m->t_high = t_high; m->t_hd_sta = 2; m->t_su_sto = 2; m->t_buf = 4;
    m->max_retries = 3; m->keep_clock = false; m->max_wait = 2000;
    m->mon.idle = 1000;                                  /* 전원 직후 버스는 free 로 가정 */
    m->lost_byte = m->lost_bit = -1;
    b->m[b->nm++] = m;
    return m;
}
static Slave *bus_add_slave(Bus *b, Slave *s, uint8_t addr) {
    memset(s, 0, sizeof *s);
    s->node = b->nnodes++; s->addr = addr; s->rx_limit = RX_CAP; s->tx_seed = (uint8_t)(addr ^ 0x5A);
    b->sda_drv[s->node] = b->scl_drv[s->node] = 1;
    b->s[b->ns++] = s;
    return s;
}

/* 트레이스의 [from, to) 에서 양쪽이 반대 레벨로 막힌 SCL low/high 구간 길이를 순서대로 모은다 */
static void scl_runs(const Trace *tr, uint32_t from, uint32_t to, int *lows, int *nl, int *highs, int *nh, int cap) {
    *nl = *nh = 0;
    if (to > tr->n) to = tr->n;
    uint32_t i = from;
    while (i < to && (i == from || tr->scl[i] == tr->scl[i - 1])) i++;       /* 첫 엣지까지 건너뜀 */
    while (i < to) {
        uint32_t j = i;
        while (j < to && tr->scl[j] == tr->scl[i]) j++;
        if (j >= to) break;                                                    /* 끝이 막히지 않은 구간은 버림 */
        int len = (int)(j - i);
        if (tr->scl[i] == '0') { if (*nl < cap) lows[(*nl)++] = len; }
        else { if (*nh < cap) highs[(*nh)++] = len; }
        i = j;
    }
}
static uint32_t xs_state = 0x2545F491u;
static uint32_t xorshift32(void) {
    uint32_t x = xs_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return xs_state = x;
}
static int first_diff_bit(uint8_t a, uint8_t c) {      /* MSB 부터 0,1,2.. 번째 — 처음 다른 비트 */
    for (int i = 0; i < 8; i++) if (((a ^ c) >> (7 - i)) & 1) return i;
    return -1;
}


/* ======================= 연습 1: 중재 결과 예측 ======================= */
/* 두 마스터가 같은 틱에 START 하고 슬레이브가 모든 바이트를 ACK 한다고 가정.
 * 반환: 0 = X 가 짐, 1 = Y 가 짐, 2 = 아무도 안 짐(완전히 같은 메시지), -1 = 규격상 미정(STOP vs 데이터 비트).
 * *byte, *bit = 패배 위치 (byte 0 = 주소, bit 8 = ACK 슬롯). */
typedef struct { uint8_t addr7; bool rd; uint8_t n; uint8_t w[MAX_MSG]; } Msg;

static int drive_bit(const Msg *m, int byte, int bit) {   /* 이 위치에서 마스터가 내는 값, 안 내면 -1 */
    bool tx = byte == 0 || !m->rd;
    if (bit < 8) {
        if (!tx) return -1;
        uint8_t v = byte == 0 ? (uint8_t)(m->addr7 << 1 | (m->rd ? 1 : 0)) : m->w[byte - 1];
        return (v >> (7 - bit)) & 1;
    }
    if (tx) return -1;                                     /* ACK 은 슬레이브가 낸다 */
    return byte < m->n ? 0 : 1;                            /* 마스터 수신자: 마지막만 NACK */
}
static int arb_predict(const Msg *x, const Msg *y, int *byte, int *bit) {
    int nb = 1 + (x->n > y->n ? x->n : y->n);
    for (int by = 0; by < nb; by++) {
        if (by > x->n || by > y->n) return -1;             /* 한쪽은 STOP 하려 하고 다른 쪽은 계속 → 규격상 금지 */
        for (int bi = 0; bi <= 8; bi++) {
            int dx = drive_bit(x, by, bi), dy = drive_bit(y, by, bi);
            if (dx < 0 || dy < 0 || dx == dy) continue;
            *byte = by; *bit = bi;
            return dx == 1 ? 0 : 1;                        /* 1 을 낸 쪽이 진다 */
        }
    }
    return 2;
}

/* ======================= 연습 2: SCL 동기화 공식 ======================= */
static int sync_low(const int *lo, int k)  { int v = lo[0]; for (int i = 1; i < k; i++) if (lo[i] > v) v = lo[i]; return v; }
static int sync_high(const int *hi, int k) { int v = hi[0]; for (int i = 1; i < k; i++) if (hi[i] < v) v = hi[i]; return v; }

/* ======================= 연습 4: 트레이스 디코더 ======================= */
/* 버스 트레이스 [from,to) → "S A0 A 11 A 22 A P" 형식. 용량 부족 -1, 아니면 길이 */
static int decode_trace(const Trace *tr, uint32_t from, uint32_t to, char *out, size_t cap) {
    size_t k = 0; int nb = -1; uint8_t sh = 0;
    if (cap == 0) return -1;
    out[0] = '\0';
    for (uint32_t i = from + 1; i < to && i < tr->n; i++) {
        char tok[8] = "";
        if (tr->scl[i - 1] == '1' && tr->scl[i] == '1' && tr->sda[i - 1] != tr->sda[i]) {
            if (tr->sda[i] == '0') { snprintf(tok, sizeof tok, "S"); nb = 0; sh = 0; }
            else { snprintf(tok, sizeof tok, "P"); nb = -1; }
        } else if (tr->scl[i - 1] == '0' && tr->scl[i] == '1' && nb >= 0) {
            if (nb < 8) { sh = (uint8_t)(sh << 1 | (tr->sda[i] == '1')); nb++; }
            else { snprintf(tok, sizeof tok, "%02X %c", sh, tr->sda[i] == '0' ? 'A' : 'N'); nb = 0; sh = 0; }
        }
        if (tok[0]) {
            size_t l = strlen(tok) + (k ? 1 : 0);
            if (k + l + 1 > cap) return -1;
            k += (size_t)snprintf(out + k, cap - k, k ? " %s" : "%s", tok);
        }
    }
    return (int)k;
}

int main(void) {
    static Bus b;
    static Master X, Y, Z;
    static Slave s1, s2, s3, s4;

    /* ----- 연습 1: 예측 vs 시뮬레이션 ----- */
    {
        Msg a = {0x50, false, 2, {0x11, 0x22}}, c = {0x68, false, 1, {0x33}};
        int by = -9, bi = -9;
        CHECK(arb_predict(&a, &c, &by, &bi) == 1 && by == 0 && bi == first_diff_bit(0x50 << 1, 0x68 << 1));
        Msg r1 = {0x48, true, 1, {0}}, r2 = {0x48, true, 2, {0}};
        CHECK(arb_predict(&r1, &r2, &by, &bi) == 0 && by == 1 && bi == 8);       /* 짧은 읽기가 ACK 비트에서 진다 */
        Msg p1 = {0x50, false, 1, {0x11}}, p2 = {0x50, false, 2, {0x11, 0x00}};
        CHECK(arb_predict(&p1, &p2, &by, &bi) == -1);                            /* 접두사 관계 = 규격상 미정 */
        CHECK(arb_predict(&a, &a, &by, &bi) == 2);
        int bad = 0, ncase = 0, nund = 0;
        for (int it = 0; it < 3000; it++) {
            Msg m[2];
            uint32_t r = xorshift32();
            uint8_t addr = (uint8_t)(0x20 + (r & 3));          /* 주소가 자주 겹치게 */
            for (int q = 0; q < 2; q++) {
                memset(&m[q], 0, sizeof m[q]);
                m[q].addr7 = (r >> (8 + q)) & 1 ? addr : (uint8_t)(0x20 + (xorshift32() & 3));
                m[q].rd = (xorshift32() % 3) == 0;
                m[q].n = (uint8_t)(1 + xorshift32() % 3);
                for (int j = 0; j < m[q].n; j++) m[q].w[j] = (uint8_t)(xorshift32() & 0x0F);   /* 작은 값 = 접두사 겹침 많게 */
            }
            int pb = -1, pi = -1;
            int want = arb_predict(&m[0], &m[1], &pb, &pi);
            if (want == -1) { nund++; continue; }              /* 규격상 미정 — 시뮬레이션하지 않음 */
            ncase++;
            bus_init(&b, false); b.log = NULL; b.done = NULL;
            bus_add_master(&b, &X, 'X', 2, 2); bus_add_master(&b, &Y, 'Y', 2, 2);
            Slave *sl[4] = {&s1, &s2, &s3, &s4};
            for (int q = 0; q < 4; q++) bus_add_slave(&b, sl[q], (uint8_t)(0x20 + q));
            X.max_retries = Y.max_retries = 0;
            m_submit(&X, m[0].addr7, m[0].rd, m[0].w, m[0].n, 0);
            m_submit(&Y, m[1].addr7, m[1].rd, m[1].w, m[1].n, 0);
            if (!run_until_idle(&b, 5000)) { bad++; continue; }
            int got = X.last.n_lost ? 0 : Y.last.n_lost ? 1 : 2;
            const Done *lo = got == 0 ? &X.last : &Y.last;
            if (got != want || (got < 2 && (lo->lost_byte != pb || lo->lost_bit != pi))) bad++;
        }
        printf("ex1: %d simulated pairs agree with arb_predict (bad=%d), %d undefined prefix pairs skipped\n", ncase, bad, nund);
        CHECK(bad == 0 && ncase > 2000 && nund > 0);
    }

    /* ----- 연습 2: k 마스터 동기화 공식 + ns 계산 ----- */
    {
        int bad = 0;
        for (int it = 0; it < 300; it++) {
            int k = 2 + (int)(xorshift32() % 2), lo[3], hi[3];
            bus_init(&b, true); b.log = NULL; b.done = NULL;
            Master *ms[3] = {&X, &Y, &Z};
            for (int i = 0; i < k; i++) {
                lo[i] = 2 + (int)(xorshift32() % 7); hi[i] = 1 + (int)(xorshift32() % 7);
                bus_add_master(&b, ms[i], (char)('X' + i), lo[i], hi[i]);
            }
            bus_add_slave(&b, &s1, 0x20);
            const uint8_t d[1] = {0x5A};
            for (int i = 0; i < k; i++) m_submit(ms[i], 0x20, false, d, 1, 0);
            if (!run_until_idle(&b, 5000)) { bad++; continue; }
            int L[64] = {0}, H[64] = {0}, nl, nh;
            scl_runs(&g_tr, 0, g_tr.n, L, &nl, H, &nh, 64);
            for (int i = 0; i < nl; i++) bad += L[i] != sync_low(lo, k);
            for (int i = 0; i < nh; i++) bad += H[i] != sync_high(hi, k);
            bad += nl != 19 || nh != 18;
        }
        CHECK(bad == 0);
        /* 실제 시간: 400 kHz 마스터 (tLOW 1300 ns, tHIGH 1200 ns) + 100 kHz 마스터 (tLOW 5000, tHIGH 5000) */
        int lo_ns[2] = {1300, 5000}, hi_ns[2] = {1200, 5000};
        int L = sync_low(lo_ns, 2), H = sync_high(hi_ns, 2);
        int f_hz = 1000000000 / (L + H);
        printf("ex2: 400k master + 100k master -> SCL low %d ns, high %d ns, period %d ns = %d Hz; "
               "Sm tHIGH>=4000 ns? %s\n", L, H, L + H, f_hz, H >= 4000 ? "yes" : "NO (violates Standard-mode)");
        CHECK(L == 5000 && H == 1200 && f_hz == 161290 && H < 4000 && H >= 600);
    }

    /* ----- 연습 3: 양보 간격 gap ----- */
    {
        int att[3] = {0, 0, 0};
        uint32_t lat[3] = {0, 0, 0};
        const int gaps[3] = {0, 1, 4};
        for (int g = 0; g < 3; g++) {
            bus_init(&b, false); b.log = NULL; b.done = NULL;
            bus_add_master(&b, &X, 'X', 2, 2); bus_add_master(&b, &Y, 'Y', 2, 2);
            bus_add_slave(&b, &s1, 0x10); bus_add_slave(&b, &s2, 0x68);
            X.gap = gaps[g]; Y.max_retries = 10;
            const uint8_t d[1] = {0x5A};
            for (int k = 0; k < 5; k++) m_submit(&X, 0x10, false, d, 1, 0);
            XferInfo info;
            int rc = i2c_mm_xfer(&b, &Y, 0x68, false, d, NULL, 1, &info);   /* Y 는 블로킹 API 로 */
            CHECK(run_until_idle(&b, 20000));
            att[g] = info.attempts; lat[g] = info.ticks;
            CHECK(rc == 0 && s1.rx_len == 5 && s2.rx_len == 1);
        }
        printf("ex3: Y attempts / latency with X gap 0,1,4 = %d/%u, %d/%u, %d/%u ticks\n",
               att[0], (unsigned)lat[0], att[1], (unsigned)lat[1], att[2], (unsigned)lat[2]);
        CHECK(att[0] == 6 && att[1] == 2 && att[2] == 2 && lat[1] < lat[0]);
    }

    /* ----- 연습 4: 승자에게 중재는 무손실 ----- */
    {
        char solo[128], both[128], both_slow[128];
        const uint8_t d[2] = {0x11, 0x22};
        /* (a) X 혼자 */
        bus_init(&b, true); b.log = NULL; b.done = NULL;
        bus_add_master(&b, &X, 'X', 2, 2); bus_add_slave(&b, &s1, 0x50); bus_add_slave(&b, &s2, 0x68);
        m_submit(&X, 0x50, false, d, 2, 0); run_until_idle(&b, 5000);
        uint32_t end_solo = X.last.done_t + 1;
        char scl_solo[TRACE_CAP + 1], sda_solo[TRACE_CAP + 1];
        memcpy(scl_solo, g_tr.scl, end_solo); memcpy(sda_solo, g_tr.sda, end_solo);
        CHECK(decode_trace(&g_tr, 0, end_solo, solo, sizeof solo) > 0);
        /* (b) 같은 속도의 Y 가 0x68 로 충돌 (진 뒤 바이트 끝까지 클럭) */
        bus_init(&b, true); b.log = NULL; b.done = NULL;
        bus_add_master(&b, &X, 'X', 2, 2); bus_add_master(&b, &Y, 'Y', 2, 2);
        bus_add_slave(&b, &s1, 0x50); bus_add_slave(&b, &s2, 0x68);
        Y.keep_clock = true; Y.max_retries = 0;
        m_submit(&X, 0x50, false, d, 2, 0); m_submit(&Y, 0x68, false, d, 2, 0); run_until_idle(&b, 5000);
        CHECK(Y.last.status == I2C_ERR_ARB_LOST && X.last.done_t + 1 == end_solo);
        CHECK(memcmp(g_tr.scl, scl_solo, end_solo) == 0 && memcmp(g_tr.sda, sda_solo, end_solo) == 0);  /* 틱 단위로 같다 */
        decode_trace(&g_tr, 0, end_solo, both, sizeof both);
        /* (c) 느린 Y (12, 2) — 타이밍은 달라도 디코드 결과는 같다 */
        bus_init(&b, true); b.log = NULL; b.done = NULL;
        bus_add_master(&b, &X, 'X', 2, 2); bus_add_master(&b, &Y, 'Y', 12, 2);
        bus_add_slave(&b, &s1, 0x50); bus_add_slave(&b, &s2, 0x68);
        Y.keep_clock = true; Y.max_retries = 0;
        m_submit(&X, 0x50, false, d, 2, 0); m_submit(&Y, 0x68, false, d, 2, 0); run_until_idle(&b, 5000);
        decode_trace(&g_tr, 0, X.last.done_t + 1, both_slow, sizeof both_slow);
        printf("ex4: solo  : %s\n     collide: %s\n     slow Y : %s (A STOP at tick %u vs %u solo)\n",
               solo, both, both_slow, (unsigned)X.last.done_t, (unsigned)(end_solo - 1));
        CHECK(strcmp(solo, "S A0 A 11 A 22 A P") == 0 && strcmp(both, solo) == 0 && strcmp(both_slow, solo) == 0);
        CHECK(X.last.done_t + 1 > end_solo);
        char tiny[8];
        CHECK(decode_trace(&g_tr, 0, X.last.done_t + 1, tiny, sizeof tiny) == -1);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
