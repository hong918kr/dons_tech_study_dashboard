/* COMM-M01 추가 연습 — 한 번에 검증
 *
 * 본문 solution.c 의 모델·마스터·드라이버(그대로 복사) 위에:
 *   연습 1: 트랜잭션 누적 스트레칭 예산 (SMBus 식 누적 clock-low 한도 흉내) → I2C_ERR_TIMEOUT_CUM
 *   연습 2: ACK 앞 스트레칭 (슬레이브가 8번째 SCL↓ 에서 붙잡고 끝날 때 ACK/NACK 결정) — 정답/naive 마스터 비교
 *   연습 3: 트랜잭션 길이 공식 = 시뮬레이션 (t_low, t_high, nw, nr, 스트레칭 랜덤)
 *   연습 4: 로직 애널라이저 검사기 — runt high 펄스와 "바이트당 9클럭" 위반을 캡처에서 찾기
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 버스 모델 (주어짐) ---------------- */
enum { MAX_NODES = 4, TRACE_CAP = 2048 };
#define STRETCH_FOREVER UINT32_MAX

enum { I2C_OK = 0, I2C_ERR_ARG = -1, I2C_ERR_BUS_BUSY = -2, I2C_ERR_NACK_ADDR = -3,
       I2C_ERR_NACK_DATA = -4, I2C_ERR_TIMEOUT = -5 };

typedef struct Slave Slave;
typedef struct Bus Bus;

struct Bus {
    uint8_t  sda_drv[MAX_NODES], scl_drv[MAX_NODES]; /* 1 = 놓음, 0 = LOW 로 당김. node 0 = 마스터 */
    int      nnodes;
    uint8_t  sda, scl;                 /* 해석된 선 레벨 */
    Slave   *slaves[MAX_NODES];
    int      nslaves;
    uint32_t ticks;                    /* 1 틱 = 1 µs */
    /* 마스터 설정 */
    uint32_t t_low, t_high;            /* SCL low / high 반주기 (틱). t_low >= 2, t_high >= 1 */
    uint32_t max_stretch;              /* SCL 을 놓은 뒤 high 를 기다리는 최대 틱 */
    int    (*scl_up)(Bus *b);          /* SCL 올리기: m_scl_high (정답) 또는 scl_up_naive */
    /* 마스터 통계 */
    uint32_t last_wait;                /* 마지막 scl_up 에서 기다린 틱 */
    uint32_t stretch_events, stretch_ticks, stretch_max;
    /* 로직 애널라이저 관찰: SCL high/low 펄스 폭 */
    uint32_t hi_run, lo_run, min_high, min_low, max_low;
    uint32_t sda_moves_scl_high;
    char     tr_scl[TRACE_CAP + 1], tr_sda[TRACE_CAP + 1];
    uint32_t tr_len;
};

typedef enum { S_IDLE, S_ADDR, S_ADDR_ACK, S_RX, S_RX_ACK, S_TX, S_TX_ACK } SState;
enum { STR_ADDR_W = 1, STR_RX = 2, STR_TX = 4, STR_PRE_ACK = 8 }; /* 스트레칭 위치 (STR_PRE_ACK = 연습 2) */

struct Slave {
    int      node;
    uint8_t  addr;
    SState   st;
    uint8_t  prev_scl, prev_sda, sh;
    int      nbits;
    bool     rw, master_ack;
    uint8_t  rx[16];
    size_t   rx_len, rx_cap;
    uint8_t  tx[16];
    size_t   tx_len, tx_pos;
    uint32_t starts, stops, addr_matches;
    /* 클럭 스트레칭 */
    uint8_t  stretch_where;            /* STR_* 비트마스크 */
    uint32_t stretch_n;                /* 스트레칭 길이 N (틱). STRETCH_FOREVER = 놓지 않음 */
    const uint32_t *stretch_seq;       /* 있으면 k 번째 스트레칭 = stretch_seq[k] (끝나면 stretch_n) */
    size_t   stretch_seq_len;
    uint32_t nstretch;                 /* 지금까지의 스트레칭 이벤트 수 (N == 0 포함) */
    uint32_t hold;                     /* 남은 붙잡기 틱 (0 = 안 붙잡음) */
    bool     pending_load;             /* 읽기: 붙잡기가 끝날 때 다음 바이트 MSB 를 낸다 */
    bool     pending_ack;              /* 연습 2: 붙잡기가 끝날 때 ACK(0)를 낸다 */
};

static void slave_on_tick(Slave *s, Bus *b);

static void bus_resolve(Bus *b) {
    uint8_t sda = 1, scl = 1;
    for (int i = 0; i < b->nnodes; i++) { sda &= b->sda_drv[i]; scl &= b->scl_drv[i]; }
    b->sda = sda;
    b->scl = scl;
}

static void slave_stretch_tick(Slave *s, Bus *b);

/* 1 틱: 스트레칭 카운트다운(놓기는 이번 틱 스냅샷에 바로 보인다) → AND → 슬레이브 반응 → AND → 기록 */
static void bus_tick(Bus *b) {
    uint8_t psda = b->sda, pscl = b->scl;
    for (int i = 0; i < b->nslaves; i++) slave_stretch_tick(b->slaves[i], b);
    bus_resolve(b);
    for (int i = 0; i < b->nslaves; i++) slave_on_tick(b->slaves[i], b);
    bus_resolve(b);
    b->ticks++;
    if (pscl && b->scl && psda != b->sda) b->sda_moves_scl_high++;
    if (b->scl) {                                  /* 펄스 폭 측정 (LA 처럼) */
        if (!pscl) {
            if (b->lo_run < b->min_low) b->min_low = b->lo_run;
            if (b->lo_run > b->max_low) b->max_low = b->lo_run;
            b->hi_run = 0;
        }
        b->hi_run++;
    } else {
        if (pscl) { if (b->hi_run < b->min_high) b->min_high = b->hi_run; b->lo_run = 0; }
        b->lo_run++;
    }
    if (b->tr_len < TRACE_CAP) {
        b->tr_scl[b->tr_len] = (char)('0' + b->scl);
        b->tr_sda[b->tr_len] = (char)('0' + b->sda);
        b->tr_len++;
        b->tr_scl[b->tr_len] = b->tr_sda[b->tr_len] = '\0';
    }
}

static void set_sda(Bus *b, int v) { b->sda_drv[0] = (uint8_t)(v != 0); bus_tick(b); }
static void set_scl(Bus *b, int v) { b->scl_drv[0] = (uint8_t)(v != 0); bus_tick(b); }
static void idle_ticks(Bus *b, uint32_t n) { while (n--) bus_tick(b); }

/* ---------------- Part A: 스트레칭 슬레이브 ---------------- */

static uint8_t tx_next(Slave *s) { return s->tx_pos < s->tx_len ? s->tx[s->tx_pos++] : 0xFF; }

/* 읽기: 다음 바이트를 꺼내 MSB 를 SDA 에 (SCL low 일 때만 불린다) */
static void slave_load(Slave *s, Bus *b) {
    s->sh = tx_next(s);
    s->nbits = 0;
    b->sda_drv[s->node] = (uint8_t)(s->sh >> 7);
    s->pending_load = false;
}
/* 붙잡기 마지막 틱에 할 일: 미뤄 둔 MSB 또는 ACK (연습 2) */
static void slave_pending(Slave *s, Bus *b) {
    if (s->pending_load) slave_load(s, b);
    if (s->pending_ack) { b->sda_drv[s->node] = 0; s->pending_ack = false; }
}

/* 스트레칭 시작 (9번째 클럭이 끝나는 SCL↓ 틱에 불림). kind 가 stretch_where 에 없으면 false.
 * 이번 길이 N = stretch_seq[nstretch] (없으면 stretch_n), nstretch++. N == 0 이면 false.
 * N > 0 이면 hold = N, 자기 SCL 드라이버를 당기고 true. N == 1 이고 pending_load 면 지금 MSB 를 낸다. */
static bool slave_stretch_start(Slave *s, Bus *b, uint8_t kind) {
    if (!(s->stretch_where & kind)) return false;
    uint32_t n = (s->stretch_seq && s->nstretch < s->stretch_seq_len) ? s->stretch_seq[s->nstretch] : s->stretch_n;
    s->nstretch++;
    if (n == 0) return false;
    s->hold = n;
    b->scl_drv[s->node] = 0;
    if (s->hold == 1) slave_pending(s, b);
    return true;
}

/* 매 틱 맨 앞(bus_tick 이 AND 계산 전에)에 불림. 붙잡는 중이면 hold--: 1 이 되면 미뤄 둔 MSB 를 내고(데이터 준비 완료),
 * 0 이 되면 SCL 을 놓는다. STRETCH_FOREVER 는 줄지 않는다. */
static void slave_stretch_tick(Slave *s, Bus *b) {
    if (s->hold == 0 || s->hold == STRETCH_FOREVER) return;
    s->hold--;
    if (s->hold == 1) slave_pending(s, b);
    if (s->hold == 0) b->scl_drv[s->node] = 1;
}

/* COMM-B06 슬레이브 상태 머신 + 스트레칭 훅 (주어짐) */
static void slave_on_tick(Slave *s, Bus *b) {
    uint8_t scl = b->scl, sda = b->sda;          /* 이번 틱 시작의 스냅샷 */
    uint8_t *out = &b->sda_drv[s->node];
    if (s->prev_scl && scl && s->prev_sda != sda) {                /* SCL high 중 SDA 변화 */
        if (!sda) { s->starts++; s->st = S_ADDR; s->nbits = 0; s->sh = 0; s->pending_load = s->pending_ack = false; }
        else      { s->stops++;  s->st = S_IDLE; }
        *out = 1;
    } else if (!s->prev_scl && scl) {                               /* SCL ↑ : 샘플 */
        if (s->st == S_ADDR || s->st == S_RX) { s->sh = (uint8_t)(s->sh << 1 | sda); s->nbits++; }
        else if (s->st == S_TX) s->nbits++;
        else if (s->st == S_TX_ACK) s->master_ack = (sda == 0);
    } else if (s->prev_scl && !scl) {                               /* SCL ↓ : SDA 바꿀 차례 */
        switch (s->st) {
        case S_ADDR:
            if (s->nbits == 8) {
                if ((s->sh >> 1) == s->addr) { s->rw = s->sh & 1; s->addr_matches++; *out = 0; s->st = S_ADDR_ACK; }
                else s->st = S_IDLE;
            }
            break;
        case S_RX:
            if (s->nbits == 8) {
                bool take = s->rx_len < s->rx_cap;
                if (take) s->rx[s->rx_len++] = s->sh;
                s->st = S_RX_ACK;
                s->pending_ack = take;                              /* 연습 2: ACK 앞 스트레칭이면 나중에 */
                if (!slave_stretch_start(s, b, STR_PRE_ACK)) { if (take) *out = 0; s->pending_ack = false; }
            }
            break;
        case S_ADDR_ACK:
        case S_RX_ACK:                                              /* 9번째 클럭 끝: ACK 놓기 */
            s->nbits = 0; s->sh = 0; *out = 1;
            if (s->st == S_ADDR_ACK && s->rw) {                     /* 읽기: 첫 바이트 준비 */
                s->st = S_TX; s->pending_load = true;
                if (!slave_stretch_start(s, b, STR_TX)) slave_load(s, b);
            } else {
                slave_stretch_start(s, b, s->st == S_ADDR_ACK ? STR_ADDR_W : STR_RX);
                s->st = S_RX;
            }
            break;
        case S_TX:
            if (s->nbits < 8) *out = (uint8_t)((s->sh >> (7 - s->nbits)) & 1);
            else { *out = 1; s->st = S_TX_ACK; }
            break;
        case S_TX_ACK:
            *out = 1;
            if (s->master_ack) {                                    /* 마스터 ACK: 다음 바이트 준비 */
                s->st = S_TX; s->pending_load = true;
                if (!slave_stretch_start(s, b, STR_TX)) slave_load(s, b);
            } else s->st = S_IDLE;
            break;
        default:
            break;
        }
    }
    s->prev_scl = scl;
    s->prev_sda = sda;
}

/* ---------------- Part A: 스트레칭을 아는 마스터 ---------------- */

/* SCL 을 놓고(1 틱), 다시 읽어 low 면 high 가 될 때까지 1 틱씩 기다린다.
 * 기다린 틱 w 가 max_stretch 에 도달했는데도 low 면 I2C_ERR_TIMEOUT (w == max_stretch 에 올라오면 OK).
 * last_wait = w. w > 0 이면 stretch_events++, stretch_ticks += w, stretch_max 갱신. */
static int m_scl_high(Bus *b) {
    set_scl(b, 1);
    uint32_t w = 0;
    while (!b->scl) {
        if (w >= b->max_stretch) { b->last_wait = w; return I2C_ERR_TIMEOUT; }
        bus_tick(b);
        w++;
    }
    b->last_wait = w;
    if (w > 0) {
        b->stretch_events++;
        b->stretch_ticks += w;
        if (w > b->stretch_max) b->stretch_max = w;
    }
    return I2C_OK;
}

/* naive 마스터 (주어짐): SCL 을 놓기만 하고 high 라고 가정한다 */
static int scl_up_naive(Bus *b) { set_scl(b, 1); b->last_wait = 0; return I2C_OK; }

/* 비트 쓰기 (SCL low 에서 시작): SDA = bit → low 반주기 채우기(t_low-2) → scl_up → high 반주기(t_high-1) → SCL↓.
 * low 펄스 = SCL↓ 틱 + SDA 틱 + (t_low-2) = t_low, high 펄스 = 확인된 high 부터 t_high. */
static int m_write_bit(Bus *b, int bit) {
    set_sda(b, bit);
    idle_ticks(b, b->t_low - 2);
    int r = b->scl_up(b);
    if (r) return r;
    idle_ticks(b, b->t_high - 1);
    set_scl(b, 0);
    return I2C_OK;
}

/* 비트 읽기: SDA 놓기 → low 반주기 → scl_up → (high 확인 직후) SDA 읽기 → high 반주기 → SCL↓ */
static int m_read_bit(Bus *b, int *bit) {
    set_sda(b, 1);
    idle_ticks(b, b->t_low - 2);
    int r = b->scl_up(b);
    if (r) return r;
    *bit = b->sda;
    idle_ticks(b, b->t_high - 1);
    set_scl(b, 0);
    return I2C_OK;
}

/* 바이트 (주어짐): 어느 비트에서든 에러(TIMEOUT)면 바로 전파 */
static int m_write_byte(Bus *b, uint8_t byte, bool *ack) {
    int r, bit = 1;
    *ack = false;
    for (int i = 7; i >= 0; i--) if ((r = m_write_bit(b, (byte >> i) & 1)) != 0) return r;
    if ((r = m_read_bit(b, &bit)) != 0) return r;
    *ack = (bit == 0);
    return I2C_OK;
}
static int m_read_byte(Bus *b, bool ack, uint8_t *out) {
    int r, bit = 1;
    uint8_t v = 0;
    for (int i = 0; i < 8; i++) { if ((r = m_read_bit(b, &bit)) != 0) return r; v = (uint8_t)(v << 1 | bit); }
    *out = v;
    return m_write_bit(b, ack ? 0 : 1);
}

/* START (주어짐). SCL low 에서 불리면 Sr: SDA 놓기 → SCL 올리기(스트레칭 가능!) → tSU;STA → SDA↓ → tHD;STA → SCL↓ */
static int m_start(Bus *b) {
    if (!b->scl) {
        set_sda(b, 1);
        idle_ticks(b, b->t_low - 2);
        int r = b->scl_up(b);
        if (r) return r;
        idle_ticks(b, b->t_high - 1);
    }
    set_sda(b, 0);
    idle_ticks(b, b->t_high - 1);
    set_scl(b, 0);
    return I2C_OK;
}

/* ---------------- Part A/B: STOP (스트레칭 대기) 과 타임아웃 뒤 정리 ---------------- */

/* STOP (SCL low 에서): SDA 0 → low 반주기 → scl_up (마지막 ACK 뒤 스트레칭을 기다림) → tSU;STO(t_high-1) → SDA 1 */
static int m_stop(Bus *b) {
    set_sda(b, 0);
    idle_ticks(b, b->t_low - 2);
    int r = b->scl_up(b);
    if (r) return r;
    idle_ticks(b, b->t_high - 1);
    set_sda(b, 1);
    return I2C_OK;
}

/* 타임아웃 뒤: 마스터는 SDA·SCL 드라이버를 모두 놓는다(= 1). 클럭을 더 내지 않는다. 1 틱. */
static void m_abort(Bus *b) {
    b->sda_drv[0] = 1;
    b->scl_drv[0] = 1;
    bus_tick(b);
}

/* ---------------- Part C: 드라이버 API + 타이밍 로그 ---------------- */
typedef enum { EV_START, EV_RSTART, EV_ADDR, EV_TX, EV_RX, EV_STOP, EV_TIMEOUT } EvKind;
typedef struct {
    uint32_t t0, t1;          /* 시작/끝 틱 (µs) */
    EvKind   kind, phase;     /* phase: TIMEOUT 이면 어느 단계에서 났는지, 아니면 kind 와 같음 */
    uint8_t  byte;
    bool     ack;
    uint32_t stretch;         /* 이 단계에서 기다린 스트레칭 틱 */
} I2cEvent;
typedef struct { I2cEvent *ev; size_t cap, n; bool overflow; } I2cLog;

/* log == NULL 이면 무시. 꽉 찼으면 overflow = true 로 표시하고 버린다. */
static void log_add(I2cLog *log, const I2cEvent *e) {
    if (!log) return;
    if (log->n < log->cap) log->ev[log->n++] = *e;
    else log->overflow = true;
}

static void log_ev(I2cLog *log, const Bus *b, EvKind k, EvKind ph, uint32_t t0, uint32_t s0, uint8_t byte, bool ack) {
    I2cEvent e = { t0, b->ticks, k, ph, byte, ack, b->stretch_ticks - s0 };
    log_add(log, &e);
}

/* [S addr+W wr[0..nw) ] [Sr|S addr+R rd[0..nr) (마지막만 NACK)] P
 * 반환: OK / ARG (addr > 0x7F, nw == nr == 0, NULL 버퍼) / BUS_BUSY (START 전 SDA 나 SCL 이 low, 클럭 0개)
 *       NACK_ADDR / NACK_DATA (→ STOP 내고 반환) / TIMEOUT (→ m_abort, STOP 없음).
 * 모든 단계를 log 에 남긴다 (t0, t1, 바이트, ACK, 그 단계의 스트레칭). */
static int i2c_write_read(Bus *b, uint8_t addr7, const uint8_t *wr, size_t nw, uint8_t *rd, size_t nr, I2cLog *log) {
    if (addr7 > 0x7F || (nw == 0 && nr == 0) || (nw && !wr) || (nr && !rd)) return I2C_ERR_ARG;
    if (!b->sda || !b->scl) return I2C_ERR_BUS_BUSY;
    int r, ret = I2C_OK;
    bool ack = false;
    EvKind ph = EV_START;
    uint32_t t0 = b->ticks, s0 = b->stretch_ticks;
    if ((r = m_start(b)) != 0) goto timeout;
    log_ev(log, b, EV_START, EV_START, t0, s0, 0, false);
    if (nw > 0) {
        ph = EV_ADDR; t0 = b->ticks; s0 = b->stretch_ticks;
        if ((r = m_write_byte(b, (uint8_t)(addr7 << 1), &ack)) != 0) goto timeout;
        log_ev(log, b, EV_ADDR, EV_ADDR, t0, s0, (uint8_t)(addr7 << 1), ack);
        if (!ack) { ret = I2C_ERR_NACK_ADDR; goto stop; }
        for (size_t i = 0; i < nw; i++) {
            ph = EV_TX; t0 = b->ticks; s0 = b->stretch_ticks;
            if ((r = m_write_byte(b, wr[i], &ack)) != 0) goto timeout;
            log_ev(log, b, EV_TX, EV_TX, t0, s0, wr[i], ack);
            if (!ack) { ret = I2C_ERR_NACK_DATA; goto stop; }
        }
    }
    if (nr > 0) {
        if (nw > 0) {
            ph = EV_RSTART; t0 = b->ticks; s0 = b->stretch_ticks;
            if ((r = m_start(b)) != 0) goto timeout;
            log_ev(log, b, EV_RSTART, EV_RSTART, t0, s0, 0, false);
        }
        ph = EV_ADDR; t0 = b->ticks; s0 = b->stretch_ticks;
        if ((r = m_write_byte(b, (uint8_t)(addr7 << 1 | 1), &ack)) != 0) goto timeout;
        log_ev(log, b, EV_ADDR, EV_ADDR, t0, s0, (uint8_t)(addr7 << 1 | 1), ack);
        if (!ack) { ret = I2C_ERR_NACK_ADDR; goto stop; }
        for (size_t i = 0; i < nr; i++) {
            ph = EV_RX; t0 = b->ticks; s0 = b->stretch_ticks;
            if ((r = m_read_byte(b, i + 1 < nr, &rd[i])) != 0) goto timeout;
            log_ev(log, b, EV_RX, EV_RX, t0, s0, rd[i], i + 1 < nr);
        }
    }
stop:
    ph = EV_STOP; t0 = b->ticks; s0 = b->stretch_ticks;
    if ((r = m_stop(b)) != 0) goto timeout;
    log_ev(log, b, EV_STOP, EV_STOP, t0, s0, 0, false);
    return ret;
timeout:                                         /* 프리미티브 에러 (= SCL 타임아웃) */
    m_abort(b);
    log_ev(log, b, EV_TIMEOUT, ph, t0, s0, 0, false);
    return r;
}

/* ---------------- 테스트 하네스 (주어짐) ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static void stats_reset(Bus *b) {
    b->stretch_events = b->stretch_ticks = b->stretch_max = b->last_wait = 0;
    b->hi_run = b->lo_run = 0;
    b->min_high = b->min_low = UINT32_MAX;
    b->max_low = 0;
    b->sda_moves_scl_high = 0;
    b->tr_len = 1;                                   /* 0번 칸 = 지금 레벨 */
    b->tr_scl[0] = (char)('0' + b->scl); b->tr_sda[0] = (char)('0' + b->sda);
    b->tr_scl[1] = b->tr_sda[1] = '\0';
}
static void bus_init(Bus *b, uint32_t t_low, uint32_t t_high, uint32_t max_stretch) {
    memset(b, 0, sizeof *b);
    b->nnodes = 1;
    b->sda_drv[0] = b->scl_drv[0] = 1;
    b->sda = b->scl = 1;
    b->t_low = t_low; b->t_high = t_high; b->max_stretch = max_stretch;
    b->scl_up = m_scl_high;
    stats_reset(b);
}
static void bus_add_slave(Bus *b, Slave *s, uint8_t addr, size_t rx_cap) {
    memset(s, 0, sizeof *s);
    s->node = b->nnodes++;
    b->sda_drv[s->node] = b->scl_drv[s->node] = 1;
    s->addr = addr;
    s->rx_cap = rx_cap < sizeof s->rx ? rx_cap : sizeof s->rx;
    s->prev_scl = b->scl; s->prev_sda = b->sda;
    b->slaves[b->nslaves++] = s;
}
static void slave_set_tx(Slave *s, const uint8_t *d, size_t n) { memcpy(s->tx, d, n); s->tx_len = n; s->tx_pos = 0; }
/* 슬레이브 전원 리셋 흉내: 상태 IDLE, 두 선 놓기 */
static void slave_reset(Slave *s, Bus *b) {
    s->st = S_IDLE; s->hold = 0; s->pending_load = s->pending_ack = false;
    b->sda_drv[s->node] = b->scl_drv[s->node] = 1;
    bus_tick(b);
}
/* 버스가 idle(SDA = SCL = 1)이 될 때까지 최대 limit 틱 진행하고, idle 이면 버스 free 시간(tBUF ≈ t_low 틱)을
 * 더 기다린다 — SCL 이 방금 올라온 틱에 START 를 내면 슬레이브는 그것을 START 가 아닌 SCL↑ 로 본다.
 * 반환 = idle 이면 true */
static bool bus_run_until_idle(Bus *b, uint32_t limit) {
    while (!(b->sda && b->scl) && limit--) bus_tick(b);
    if (!(b->sda && b->scl)) return false;
    idle_ticks(b, b->t_low);
    return b->sda && b->scl;
}
static uint32_t xs_state = 0x2545F491u;
static uint32_t xorshift32(void) {
    uint32_t x = xs_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return xs_state = x;
}
static uint32_t pos_wait(uint32_t n, uint32_t t_low) { return n > t_low ? n - t_low : 0; }

/* 하네스용 최소 트랜잭션 (Part A/B 테스트용 — 드라이버 정책 없음): S addr+W d[0..n) P.
 * NACK 이어도 끝까지 보낸다. 반환 = I2C_OK 또는 프리미티브 에러(TIMEOUT 이면 m_abort 후). *all_ack = 전부 ACK 였나 */
static int h_write(Bus *b, uint8_t addr7, const uint8_t *d, size_t n, bool *all_ack) {
    bool ack = false, all = true;
    int r = m_start(b);
    if (!r) { r = m_write_byte(b, (uint8_t)(addr7 << 1), &ack); all = all && ack; }
    for (size_t i = 0; !r && i < n; i++) { r = m_write_byte(b, d[i], &ack); all = all && ack; }
    if (!r) r = m_stop(b);
    if (r) m_abort(b);
    if (all_ack) *all_ack = all && !r;
    return r;
}
/* S addr+R d[0..n) (마지막만 NACK) P */
static int h_read(Bus *b, uint8_t addr7, uint8_t *d, size_t n) {
    bool ack = false;
    int r = m_start(b);
    if (!r) r = m_write_byte(b, (uint8_t)(addr7 << 1 | 1), &ack);
    for (size_t i = 0; !r && i < n; i++) r = m_read_byte(b, i + 1 < n, &d[i]);
    if (!r) r = m_stop(b);
    if (r) m_abort(b);
    return r;
}

/* ================= 연습 1: 트랜잭션 누적 스트레칭 예산 ================= */
enum { I2C_ERR_TIMEOUT_CUM = -6 };
static uint32_t g_budget, g_spent;            /* 트랜잭션마다 g_spent = 0 으로 시작 */

/* 한 번의 기다림 한도 = min(max_stretch, 남은 예산). 예산 때문에 끊겼으면 TIMEOUT_CUM */
static int m_scl_high_budget(Bus *b) {
    uint32_t left = g_budget - g_spent, save = b->max_stretch;
    bool cum = left < save;
    if (cum) b->max_stretch = left;
    int r = m_scl_high(b);
    b->max_stretch = save;
    if (r == I2C_ERR_TIMEOUT && cum) return I2C_ERR_TIMEOUT_CUM;
    if (r == I2C_OK) g_spent += b->last_wait;
    return r;
}

/* ================= 연습 3: 트랜잭션 길이 공식 ================= */
/* START(t_high+1) + 주소·데이터 바이트 9(t_low+t_high) 씩 + Sr(t_low+2·t_high) + STOP(t_low+t_high) + Σ 기다림 */
static uint32_t xfer_ticks(uint32_t tl, uint32_t th, size_t nw, size_t nr, uint32_t waits) {
    uint32_t T = tl + th, t = th + 1;
    if (nw) t += 9 * T * (uint32_t)(1 + nw);
    if (nw && nr) t += tl + 2 * th;
    if (nr) t += 9 * T * (uint32_t)(1 + nr);
    return t + T + waits;
}

/* ================= 연습 4: 로직 애널라이저 검사기 ================= */
typedef struct {
    uint32_t frames;       /* START/Sr 로 시작해 다음 Sr/STOP 으로 끝난 구간 수 */
    uint32_t bad_frames;   /* 그 구간의 완전한 클럭 펄스 수가 9 의 배수가 아님 */
    uint32_t pulses;       /* 완전한 SCL 펄스(↑ 다음 ↓) 총수 */
    uint32_t runts;        /* high 폭 < t_high_min 인 펄스 수 */
    uint32_t min_high;
} LaReport;

static void la_check(const char *scl, const char *sda, uint32_t n, uint32_t t_high_min, LaReport *rep) {
    memset(rep, 0, sizeof *rep);
    rep->min_high = UINT32_MAX;
    bool in_frame = false, hi = false;
    uint32_t seg = 0, width = 0;
    for (uint32_t i = 1; i < n; i++) {
        int c0 = scl[i - 1] == '1', c1 = scl[i] == '1', d0 = sda[i - 1] == '1', d1 = sda[i] == '1';
        if (c0 && c1 && d0 != d1) {                     /* START/Sr (↓) 또는 STOP (↑) */
            if (in_frame) { rep->frames++; if (seg % 9) rep->bad_frames++; }
            in_frame = !d1; seg = 0; hi = false;
        } else if (!c0 && c1) { hi = true; width = 1; }
        else if (c0 && c1) width++;
        else if (c0 && !c1 && hi) {
            hi = false; seg++; rep->pulses++;
            if (width < rep->min_high) rep->min_high = width;
            if (width < t_high_min) rep->runts++;
        }
    }
}

/* ================= 연습 4 보조: 트레이스를 처음부터 기록 ================= */
static void fresh(Bus *b, Slave *s, uint32_t tl, uint32_t th, uint32_t maxs) {
    bus_init(b, tl, th, maxs);
    bus_add_slave(b, s, 0x48, 16);
}

int main(void) {
    Bus b; Slave s;
    uint8_t rd[8] = {0};
    const uint8_t a5 = 0xA5;
    int r, bad;
    bool all_ack;

    /* ===== 연습 1: 누적 예산 150, 한 번 한도 100, t_low 5 ===== */
    fresh(&b, &s, 5, 5, 100);
    b.scl_up = m_scl_high_budget; g_budget = 150;
    static const uint32_t s60[4] = { 60, 60, 60, 60 }, s200[1] = { 200 };
    s.stretch_where = STR_ADDR_W | STR_RX; s.stretch_seq = s60; s.stretch_seq_len = 4;
    const uint8_t w3[3] = { 1, 2, 3 };
    g_spent = 0; s.nstretch = 0;
    CHECK(i2c_write_read(&b, 0x48, w3, 1, NULL, 0, NULL) == I2C_OK && g_spent == 110);   /* 이벤트 2개 × 55 */
    slave_reset(&s, &b);
    g_spent = 0; s.nstretch = 0;
    r = i2c_write_read(&b, 0x48, w3, 2, NULL, 0, NULL);                                  /* 3번째에서 40 만 남음 */
    CHECK(r == I2C_ERR_TIMEOUT_CUM && g_spent == 110 && b.last_wait == 40);
    slave_reset(&s, &b);
    s.stretch_seq = s200; s.stretch_seq_len = 1; g_spent = 0; s.nstretch = 0;
    CHECK(i2c_write_read(&b, 0x48, w3, 1, NULL, 0, NULL) == I2C_ERR_TIMEOUT && b.last_wait == 100); /* 한 번 한도 먼저 */
    slave_reset(&s, &b);
    bad = 0;
    for (int it = 0; it < 500; it++) {                                                   /* 랜덤: 예측과 비교 */
        uint32_t sq[4], spent = 0; int want = I2C_OK;
        for (int i = 0; i < 4; i++) sq[i] = xorshift32() % 90;
        for (int i = 0; i < 4 && want == I2C_OK; i++) {
            uint32_t w = pos_wait(sq[i], 5);
            if (w > 100 || spent + w > 150) want = (spent + w > 150 && 150 - spent < 100) ? I2C_ERR_TIMEOUT_CUM : I2C_ERR_TIMEOUT;
            else spent += w;
        }
        s.stretch_seq = sq; s.stretch_seq_len = 4; s.nstretch = 0; g_spent = 0; s.rx_len = 0;
        r = i2c_write_read(&b, 0x48, w3, 3, NULL, 0, NULL);
        if (r != want || (r == I2C_OK && (g_spent != spent || s.rx_len != 3))) bad++;
        if (r != I2C_OK) slave_reset(&s, &b);
    }
    CHECK(bad == 0);

    /* ===== 연습 2: ACK 앞 스트레칭 (슬레이브가 8번째 SCL↓ 에서 붙잡고 끝날 때 ACK 결정) ===== */
    fresh(&b, &s, 5, 5, 100);
    s.stretch_where = STR_PRE_ACK; s.stretch_n = 30;
    CHECK(h_write(&b, 0x48, w3, 3, &all_ack) == I2C_OK && all_ack && s.rx_len == 3);
    CHECK(b.stretch_events == 3 && b.stretch_ticks == 3 * 25);           /* 9번째 클럭에서 25 씩 기다림 */
    s.rx_len = 0; s.rx_cap = 1;                                          /* 버퍼 1 → 둘째 바이트는 늦은 NACK */
    CHECK(h_write(&b, 0x48, w3, 2, &all_ack) == I2C_OK && !all_ack && s.rx_len == 1);
    fresh(&b, &s, 5, 5, 100); b.scl_up = scl_up_naive;
    s.stretch_where = STR_PRE_ACK; s.stretch_n = 30;
    r = h_write(&b, 0x48, &a5, 1, &all_ack);
    bool idle_after = bus_run_until_idle(&b, 200);
    printf("naive + pre-ACK stretch: ret=%d all_ack=%d slave rx_len=%zu, bus idle after=%d (SCL=%d SDA=%d)\n",
           r, (int)all_ack, s.rx_len, (int)idle_after, b.scl, b.sda);
    CHECK(r == I2C_OK && !all_ack && s.rx_len == 1);                     /* 받았는데 NACK 로 보임 → 재시도 = 중복 쓰기 */
    CHECK(!idle_after && b.scl == 1 && b.sda == 0);                      /* 늦은 ACK 가 SDA 를 쥔 채 남음 */

    /* ===== 연습 3: 트랜잭션 길이 공식 = 시뮬레이션 (t_low, t_high, nw, nr, 스트레칭 랜덤) ===== */
    static const uint32_t TL[4] = { 2, 3, 5, 8 }, TH[3] = { 1, 2, 5 };
    bad = 0;
    int ncase = 0;
    for (int a = 0; a < 4; a++) for (int c = 0; c < 3; c++)
    for (size_t nw = 0; nw <= 3; nw++) for (size_t nr = 0; nr <= 3; nr++) {
        if (!nw && !nr) continue;
        for (int rep = 0; rep < 8; rep++) {
            uint32_t sq[8];
            for (int i = 0; i < 8; i++) sq[i] = xorshift32() % 30;
            fresh(&b, &s, TL[a], TH[c], 1000);
            s.stretch_where = (uint8_t)(xorshift32() & 7); s.stretch_seq = sq; s.stretch_seq_len = 8;
            slave_set_tx(&s, w3, 3);
            uint32_t t0 = b.ticks;
            r = i2c_write_read(&b, 0x48, w3, nw, rd, nr, NULL);
            if (r != I2C_OK || b.ticks - t0 != xfer_ticks(TL[a], TH[c], nw, nr, b.stretch_ticks)) bad++;
            ncase++;
        }
    }
    printf("formula vs sim: %d cases, mismatches=%d; 100 kHz reg read (1 W + 2 R) = %u us, 6-byte burst read = %u us\n",
           ncase, bad, (unsigned)xfer_ticks(5, 5, 1, 2, 0), (unsigned)xfer_ticks(5, 5, 1, 6, 0));
    CHECK(bad == 0 && xfer_ticks(5, 5, 1, 2, 0) == 481);

    /* ===== 연습 4: LA 검사기 — 정답 마스터 vs naive 마스터 캡처 ===== */
    LaReport la;
    fresh(&b, &s, 3, 2, 100);
    s.stretch_where = STR_ADDR_W; s.stretch_n = 7;
    CHECK(h_write(&b, 0x48, &a5, 1, NULL) == I2C_OK);
    la_check(b.tr_scl, b.tr_sda, b.tr_len, 2, &la);
    CHECK(la.frames == 1 && la.bad_frames == 0 && la.pulses == 18 && la.runts == 0 && la.min_high == 2);
    fresh(&b, &s, 5, 5, 100); b.scl_up = scl_up_naive;
    s.stretch_where = STR_ADDR_W; s.stretch_n = 7;
    h_write(&b, 0x48, &a5, 1, NULL);
    la_check(b.tr_scl, b.tr_sda, b.tr_len, 4, &la);                     /* Sm tHIGH 4.0 µs = 4 틱 */
    printf("LA naive N=7 : frames=%u bad=%u pulses=%u runts=%u min_high=%u\n",
           (unsigned)la.frames, (unsigned)la.bad_frames, (unsigned)la.pulses, (unsigned)la.runts, (unsigned)la.min_high);
    CHECK(la.runts == 1 && la.min_high == 3 && la.bad_frames == 0);
    fresh(&b, &s, 5, 5, 100); b.scl_up = scl_up_naive;
    s.stretch_where = STR_ADDR_W; s.stretch_n = 25;
    h_write(&b, 0x48, &a5, 1, NULL);
    la_check(b.tr_scl, b.tr_sda, b.tr_len, 4, &la);
    printf("LA naive N=25: frames=%u bad=%u pulses=%u runts=%u min_high=%u\n",
           (unsigned)la.frames, (unsigned)la.bad_frames, (unsigned)la.pulses, (unsigned)la.runts, (unsigned)la.min_high);
    CHECK(la.frames == 1 && la.bad_frames == 1 && la.pulses == 16);    /* 18 클럭 중 2 개가 삼켜짐 */
    fresh(&b, &s, 5, 5, 100);                                          /* 레지스터 읽기 (Sr) + 랜덤 스트레칭 */
    static const uint32_t sq5[5] = { 9, 0, 33, 12, 50 };
    s.stretch_where = STR_ADDR_W | STR_RX | STR_TX; s.stretch_seq = sq5; s.stretch_seq_len = 5;
    slave_set_tx(&s, w3, 3);
    CHECK(i2c_write_read(&b, 0x48, w3, 1, rd, 3, NULL) == I2C_OK);
    la_check(b.tr_scl, b.tr_sda, b.tr_len, 5, &la);
    CHECK(la.frames == 2 && la.bad_frames == 0 && la.pulses == 18 + 36 && la.runts == 0);
    fresh(&b, &s, 5, 5, 100);                                          /* 읽기만: 바이트마다 40 틱 스트레칭 */
    s.stretch_where = STR_TX; s.stretch_n = 40;
    slave_set_tx(&s, w3, 3);
    CHECK(h_read(&b, 0x48, rd, 2) == I2C_OK && rd[0] == 1 && rd[1] == 2);
    la_check(b.tr_scl, b.tr_sda, b.tr_len, 5, &la);
    CHECK(la.frames == 1 && la.bad_frames == 0 && la.pulses == 27 && la.runts == 0 && la.min_high == 5);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
