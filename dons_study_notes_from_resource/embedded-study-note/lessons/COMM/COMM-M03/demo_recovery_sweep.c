/* COMM-M03 demo_recovery_sweep.c — 복구 알고리즘 4종 비교 (실측)
 *
 * 모델·드라이버·Part A 는 solution.c 와 같은 코드 (복사본, 자기완결).
 * 전수: {쓰기 3B, 읽기 3B, 레지스터 읽기(Sr) 2B} × 데이터 256 패턴 {v, ~v, 난수} × 리셋 위치(마스터 동작 k 번째) 전부.
 * 알고리즘:
 *   fold   = 펄스마다 STOP 시도 (SDA 가 SCL 을 반 박자 늦게 따라감, Linux i2c_generic_scl_recovery 방식) = solution.c
 *   naive  = SDA 놓고 SCL 토글, SDA=1 보이면 SCL↓ 후 STOP — 재시도 없음
 *   naive+ = naive 인데 STOP 뒤 SDA 가 다시 0 이면 클럭을 더 낸다 (총 40 클럭 예산)
 *   S+P    = SDA 놓고 SCL 토글, SDA=1 보이면 SCL=1 인 채로 START(SDA↓) 후 STOP(SDA↑)
 * 빌드: cc -std=c11 -Wall -Wextra -O2 demo_recovery_sweep.c -o demo && ./demo
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 버스 모델 (주어짐, COMM-B06 축약판 + 고장 주입) ---------------- */
enum { MAX_NODES = 5, TRACE_CAP = 4096, MASTER = 0, FAULT = 1 };

typedef struct Slave Slave;

typedef struct {
    uint8_t  sda_drv[MAX_NODES], scl_drv[MAX_NODES]; /* 1 = 놓음, 0 = LOW 로 당김 */
    int      nnodes;              /* node 0 = 마스터, node 1 = 고장 노드, 2.. = 슬레이브 */
    uint8_t  sda, scl;            /* 해석된 선 레벨 */
    Slave   *slaves[MAX_NODES];
    int      nslaves;
    uint32_t ticks;
    uint32_t sda_moves_scl_high;  /* SCL=1 동안 SDA 변화 = START + STOP 수 */
    uint32_t scl_falls;           /* SCL 하강 엣지 수 = 클럭 수 */
    /* 마스터 리셋 주입 */
    bool     reset_armed, reset_fired, master_dead;
    uint32_t arm_tick, reset_after; /* arm 이후 마스터 동작 reset_after 개 뒤에 리셋 */
    /* 고장 노드 */
    bool     fault_sda_low;
    uint32_t fault_scl_from, fault_scl_until;
    /* 트레이스 */
    bool     trace_on;
    char     tr_scl[TRACE_CAP + 1], tr_sda[TRACE_CAP + 1];
    uint32_t tr_len;
} Bus;

typedef enum { S_IDLE, S_ADDR, S_ADDR_ACK, S_RX, S_RX_ACK, S_TX, S_TX_ACK } SState;

struct Slave {
    int      node;
    uint8_t  addr;
    SState   st;
    uint8_t  prev_scl, prev_sda;
    uint8_t  sh;
    int      nbits;               /* 이번 바이트에서 본 SCL↑ 수 */
    bool     rw, master_ack;
    bool     glitch_ack_once;     /* 다음 마스터 NACK 을 ACK 로 잘못 본다 (ACK 슬롯 글리치 1회) */
    uint8_t  rx[16];
    size_t   rx_len, rx_cap;
    uint8_t  tx[16];
    size_t   tx_len, tx_pos;
    uint32_t starts, stops, addr_matches;
};

static void bus_resolve(Bus *b) {
    uint8_t sda = 1, scl = 1;
    for (int i = 0; i < b->nnodes; i++) { sda &= b->sda_drv[i]; scl &= b->scl_drv[i]; }
    b->sda = sda;
    b->scl = scl;
}

static void slave_on_tick(Slave *s, Bus *b) {
    uint8_t scl = b->scl, sda = b->sda;
    uint8_t *out = &b->sda_drv[s->node];
    if (s->prev_scl && scl && s->prev_sda != sda) {           /* START / STOP */
        if (!sda) { s->starts++; s->st = S_ADDR; s->nbits = 0; s->sh = 0; }
        else      { s->stops++;  s->st = S_IDLE; }
        *out = 1;
    } else if (!s->prev_scl && scl) {                          /* SCL↑ : 샘플 */
        if (s->st == S_ADDR || s->st == S_RX) { s->sh = (uint8_t)(s->sh << 1 | sda); s->nbits++; }
        else if (s->st == S_TX) s->nbits++;
        else if (s->st == S_TX_ACK) {
            s->master_ack = (sda == 0);
            if (s->glitch_ack_once && !s->master_ack) { s->master_ack = true; s->glitch_ack_once = false; }
        }
    } else if (s->prev_scl && !scl) {                          /* SCL↓ : SDA 바꿀 차례 */
        switch (s->st) {
        case S_ADDR:
            if (s->nbits == 8) {
                if ((s->sh >> 1) == s->addr) { s->rw = s->sh & 1; s->addr_matches++; *out = 0; s->st = S_ADDR_ACK; }
                else s->st = S_IDLE;
            }
            break;
        case S_RX:
            if (s->nbits == 8) {
                if (s->rx_len < s->rx_cap) { s->rx[s->rx_len++] = s->sh; *out = 0; }
                s->st = S_RX_ACK;
            }
            break;
        case S_ADDR_ACK:
        case S_RX_ACK:
            s->nbits = 0; s->sh = 0;
            if (s->st == S_ADDR_ACK && s->rw) {
                s->sh = s->tx_pos < s->tx_len ? s->tx[s->tx_pos++] : 0xFF;
                *out = (uint8_t)(s->sh >> 7); s->st = S_TX;
            } else { *out = 1; s->st = S_RX; }
            break;
        case S_TX:
            if (s->nbits < 8) *out = (uint8_t)((s->sh >> (7 - s->nbits)) & 1);
            else { *out = 1; s->st = S_TX_ACK; }
            break;
        case S_TX_ACK:
            if (s->master_ack) {
                s->sh = s->tx_pos < s->tx_len ? s->tx[s->tx_pos++] : 0xFF;
                s->nbits = 0; *out = (uint8_t)(s->sh >> 7); s->st = S_TX;
            } else { *out = 1; s->st = S_IDLE; }
            break;
        default:
            break;
        }
    }
    s->prev_scl = scl;
    s->prev_sda = sda;
}

static void bus_tick(Bus *b) {
    uint8_t psda = b->sda, pscl = b->scl;
    b->sda_drv[FAULT] = b->fault_sda_low ? 0 : 1;
    b->scl_drv[FAULT] = (b->ticks >= b->fault_scl_from && b->ticks < b->fault_scl_until) ? 0 : 1;
    bus_resolve(b);
    for (int i = 0; i < b->nslaves; i++) slave_on_tick(b->slaves[i], b);
    bus_resolve(b);
    b->ticks++;
    if (pscl && b->scl && psda != b->sda) b->sda_moves_scl_high++;
    if (pscl && !b->scl) b->scl_falls++;
    if (b->trace_on && b->tr_len < TRACE_CAP) {
        b->tr_scl[b->tr_len] = (char)('0' + b->scl);
        b->tr_sda[b->tr_len] = (char)('0' + b->sda);
        b->tr_len++;
        b->tr_scl[b->tr_len] = b->tr_sda[b->tr_len] = '\0';
    }
}

static void bus_idle_tick(Bus *b) { bus_tick(b); }   /* 마스터는 아무것도 안 바꾸고 시간만 흐름 */

/* 마스터(node 0)가 선 하나를 바꾼다 = 1 틱. 리셋이 걸리면 이 동작 대신 두 핀을 놓고 죽는다. */
static void master_drive(Bus *b, uint8_t *drv, int v) {
    if (b->master_dead) return;                              /* 리셋 중: 핀 = 입력(Hi-Z) */
    if (b->reset_armed && b->ticks - b->arm_tick >= b->reset_after) {
        b->master_dead = b->reset_fired = true;
        b->sda_drv[MASTER] = b->scl_drv[MASTER] = 1;
        bus_tick(b);
        return;
    }
    *drv = (uint8_t)(v != 0);
    bus_tick(b);
}
static void set_sda(Bus *b, int v) { master_drive(b, &b->sda_drv[MASTER], v); }
static void set_scl(Bus *b, int v) { master_drive(b, &b->scl_drv[MASTER], v); }

static void m_start(Bus *b) {                 /* SCL=0 에서 부르면 Sr (COMM-B07) */
    if (!b->scl) { set_sda(b, 1); set_scl(b, 1); }
    set_sda(b, 0);
    set_scl(b, 0);
}
static void m_stop(Bus *b)              { set_sda(b, 0); set_scl(b, 1); set_sda(b, 1); }
static void m_write_bit(Bus *b, int v)  { set_sda(b, v); set_scl(b, 1); set_scl(b, 0); }
static int  m_read_bit(Bus *b)          { set_sda(b, 1); set_scl(b, 1); int v = b->sda; set_scl(b, 0); return v; }
static bool m_write_byte(Bus *b, uint8_t x) {
    for (int i = 7; i >= 0; i--) m_write_bit(b, (x >> i) & 1);
    return m_read_bit(b) == 0;
}
static uint8_t m_read_byte(Bus *b, bool ack) {
    uint8_t v = 0;
    for (int i = 0; i < 8; i++) v = (uint8_t)(v << 1 | m_read_bit(b));
    m_write_bit(b, ack ? 0 : 1);
    return v;
}

/* ---------------- 드라이버 (주어짐, COMM-B06/B07 축약판 + STOP 확인) ---------------- */
enum { I2C_OK = 0, I2C_ERR_ARG = -1, I2C_ERR_BUS_BUSY = -2, I2C_ERR_NACK_ADDR = -3,
       I2C_ERR_NACK_DATA = -4, I2C_ERR_STOP_FAILED = -5, I2C_ERR_BUS_STUCK = -6 };

static int m_stop_checked(Bus *b) {           /* STOP 뒤 두 선이 정말 1 인가 */
    m_stop(b);
    return (b->sda && b->scl) ? I2C_OK : I2C_ERR_STOP_FAILED;
}
static int i2c_write(Bus *b, uint8_t addr7, const uint8_t *data, size_t n) {
    if (addr7 > 0x7F || (n && !data)) return I2C_ERR_ARG;
    if (!b->sda || !b->scl) return I2C_ERR_BUS_BUSY;
    int e = I2C_OK;
    m_start(b);
    if (!m_write_byte(b, (uint8_t)(addr7 << 1))) e = I2C_ERR_NACK_ADDR;
    else for (size_t i = 0; i < n; i++) if (!m_write_byte(b, data[i])) { e = I2C_ERR_NACK_DATA; break; }
    int s = m_stop_checked(b);
    return e ? e : s;
}
static int i2c_read(Bus *b, uint8_t addr7, uint8_t *buf, size_t n) {
    if (addr7 > 0x7F || !n || !buf) return I2C_ERR_ARG;
    if (!b->sda || !b->scl) return I2C_ERR_BUS_BUSY;
    int e = I2C_OK;
    m_start(b);
    if (!m_write_byte(b, (uint8_t)(addr7 << 1 | 1))) e = I2C_ERR_NACK_ADDR;
    else for (size_t i = 0; i < n; i++) buf[i] = m_read_byte(b, i + 1 < n);   /* 마지막만 NACK */
    int s = m_stop_checked(b);
    return e ? e : s;
}
/* S addr+W reg Sr addr+R d0..dn-1 P */
static int i2c_write_read(Bus *b, uint8_t addr7, uint8_t reg, uint8_t *buf, size_t n) {
    if (addr7 > 0x7F || !n || !buf) return I2C_ERR_ARG;
    if (!b->sda || !b->scl) return I2C_ERR_BUS_BUSY;
    int e = I2C_OK;
    m_start(b);
    if (!m_write_byte(b, (uint8_t)(addr7 << 1))) e = I2C_ERR_NACK_ADDR;
    else if (!m_write_byte(b, reg)) e = I2C_ERR_NACK_DATA;
    else {
        m_start(b);                                             /* Sr */
        if (!m_write_byte(b, (uint8_t)(addr7 << 1 | 1))) e = I2C_ERR_NACK_ADDR;
        else for (size_t i = 0; i < n; i++) buf[i] = m_read_byte(b, i + 1 < n);
    }
    int s = m_stop_checked(b);
    return e ? e : s;
}

/* ---------------- Part A: 버스 상태 + 9클럭 복구 ---------------- */
typedef enum { BUS_IDLE = 0, BUS_SDA_LOW = 1, BUS_SCL_LOW = 2, BUS_BOTH_LOW = 3 } BusState;
enum { REC_OK = 0, REC_ERR_SDA_STUCK = -10, REC_ERR_SCL_STUCK = -11 };
enum { REC_MAX_PULSES = 9, SCL_WAIT_MAX = 16 };

typedef struct {
    BusState before, after;
    int      pulses;                      /* 실제로 낸 클럭(SCL↓) 수 */
    uint8_t  sda_after[REC_MAX_PULSES];   /* 펄스마다 SDA 를 놓은 뒤 읽은 값 */
    int      scl_wait_ticks;              /* SCL 이 1 이 되기를 기다린 총 틱 (스트레칭/고착) */
} RecReport;

/* 지금 선 레벨로 버스 상태 분류: bit0 = SDA 가 LOW, bit1 = SCL 이 LOW */
static BusState bus_state(const Bus *b) {
    return (BusState)((b->sda ? 0 : 1) | (b->scl ? 0 : 2));
}

/* SCL 을 놓고 정말 1 이 될 때까지 최대 SCL_WAIT_MAX 틱 기다린다. 반환 = 기다린 틱, 실패면 -1 */
static int wait_scl_high(Bus *b) {
    int w = 0;
    while (!b->scl && w < SCL_WAIT_MAX) { bus_idle_tick(b); w++; }
    return b->scl ? w : -1;
}

/* 복구 펄스 1개 (SDA 가 SCL 을 반 박자 늦게 따라감 = 펄스마다 STOP 시도):
 *   SCL↓ → SDA 당김(SCL=0 이라 조건 아님) → SCL 놓음 + 1 될 때까지 대기 → SDA 놓음.
 *   슬레이브가 이번 SCL↓ 에서 SDA 를 놓았다면 마지막 동작이 SCL=1 중 SDA↑ = STOP.
 * 반환: 1 = 놓은 뒤 SDA=1 (STOP 생성됨), 0 = 아직 누가 SDA 를 잡음, REC_ERR_SCL_STUCK. *waited += 대기 틱 */
static int rec_pulse(Bus *b, int *waited) {
    set_scl(b, 0);
    set_sda(b, 0);
    set_scl(b, 1);
    int w = wait_scl_high(b);
    if (w < 0) { set_sda(b, 1); *waited += SCL_WAIT_MAX; return REC_ERR_SCL_STUCK; }
    *waited += w;
    set_sda(b, 1);
    return b->sda ? 1 : 0;
}

/* UM10204 §3.1.16 bus clear + STOP. 핀은 이미 GPIO 오픈드레인이라고 가정 (모델의 node 0).
 * 순서: 두 선 놓기 → SCL 확인(LOW 면 기다렸다가 SCL_STUCK) → SDA 이미 1 이면 0 펄스로 OK
 *       → 최대 9 펄스, 펄스마다 STOP 시도, SDA=1 이 되면 끝 → 9 번 뒤에도 0 이면 SDA_STUCK. */
static int i2c_bus_recover(Bus *b, RecReport *rep) {
    memset(rep, 0, sizeof *rep);
    rep->before = bus_state(b);
    set_sda(b, 1);
    set_scl(b, 1);
    int w = wait_scl_high(b);
    if (w < 0) { rep->scl_wait_ticks = SCL_WAIT_MAX; rep->after = bus_state(b); return REC_ERR_SCL_STUCK; }
    rep->scl_wait_ticks = w;
    if (b->sda) { rep->after = bus_state(b); return REC_OK; }
    for (int p = 1; p <= REC_MAX_PULSES; p++) {
        int r = rec_pulse(b, &rep->scl_wait_ticks);
        rep->pulses = p;
        if (r < 0) { rep->after = bus_state(b); return REC_ERR_SCL_STUCK; }
        rep->sda_after[p - 1] = b->sda;
        if (r == 1) { rep->after = bus_state(b); return rep->after == BUS_IDLE ? REC_OK : REC_ERR_SDA_STUCK; }
    }
    rep->after = bus_state(b);
    return REC_ERR_SDA_STUCK;
}

/* ---- 하네스 조각 (solution.c 와 같음) ---- */
static void bus_init(Bus *b) {
    memset(b, 0, sizeof *b);
    b->nnodes = 2;                                     /* 마스터 + 고장 노드 */
    for (int i = 0; i < MAX_NODES; i++) b->sda_drv[i] = b->scl_drv[i] = 1;
    b->sda = b->scl = 1;
}
static void bus_add_slave(Bus *b, Slave *s, uint8_t addr) {
    memset(s, 0, sizeof *s);
    s->node = b->nnodes++;
    s->addr = addr;
    s->rx_cap = sizeof s->rx;
    s->prev_scl = b->scl; s->prev_sda = b->sda;
    b->slaves[b->nslaves++] = s;
}
static void arm_reset(Bus *b, uint32_t after) { b->reset_armed = true; b->reset_fired = false; b->arm_tick = b->ticks; b->reset_after = after; }
static void master_reboot(Bus *b, int boot_ticks) {   /* 리셋 해제 → 부트 → GPIO 초기화 (선은 놓은 채) */
    b->reset_armed = false;
    b->master_dead = false;
    for (int i = 0; i < boot_ticks; i++) bus_idle_tick(b);
}
static uint32_t xs_state = 0x2545F491u;
static uint32_t xorshift32(void) {
    uint32_t x = xs_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return xs_state = x;
}

typedef enum { TX_WRITE, TX_READ, TX_WREAD, TX_KINDS } TxKind;
static const char *KIND_NAME[TX_KINDS] = {"write 3B", "read 3B", "reg read (Sr) 2B"};

static void load_slave(Slave *s, const uint8_t d[3]) {
    s->rx_len = 0;
    memcpy(s->tx, d, 3); s->tx_len = 3; s->tx_pos = 0;
}
static void run_tx(Bus *b, TxKind k, const uint8_t d[3]) {
    uint8_t got[3];
    if (k == TX_WRITE) (void)i2c_write(b, 0x50, d, 3);
    else if (k == TX_READ) (void)i2c_read(b, 0x50, got, 3);
    else (void)i2c_write_read(b, 0x50, d[0], got, 2);
}
/* 복구 뒤 정상 트랜잭션 2개 (0x50 쓰기, 0x68 읽기) 가 되는가 */
static bool verify_bus(Bus *b, Slave *s1, Slave *s2) {
    const uint8_t w[2] = {0xA5, 0x5A};
    uint8_t r = 0;
    s1->rx_len = 0;
    s2->tx[0] = 0xC3; s2->tx_len = 1; s2->tx_pos = 0;
    bool ok = i2c_write(b, 0x50, w, 2) == I2C_OK && s1->rx_len == 2 && s1->rx[0] == 0xA5 && s1->rx[1] == 0x5A;
    ok = ok && i2c_read(b, 0x68, &r, 1) == I2C_OK && r == 0xC3;
    return ok && bus_state(b) == BUS_IDLE;
}

typedef int (*RecFn)(Bus *b);

static int alg_fold(Bus *b) { RecReport r; return i2c_bus_recover(b, &r); }

static int alg_naive(Bus *b) {
    set_sda(b, 1); set_scl(b, 1);
    if (b->sda) return REC_OK;
    for (int p = 1; p <= REC_MAX_PULSES && !b->sda; p++) { set_scl(b, 0); set_scl(b, 1); }
    if (!b->sda) return REC_ERR_SDA_STUCK;
    set_scl(b, 0);
    m_stop(b);
    return (b->sda && b->scl) ? REC_OK : REC_ERR_SDA_STUCK;
}

static int alg_naive_retry(Bus *b) {
    set_sda(b, 1); set_scl(b, 1);
    if (b->sda) return REC_OK;
    uint32_t f0 = b->scl_falls;
    while (b->scl_falls - f0 < 40) {
        if (!b->sda) { set_scl(b, 0); set_scl(b, 1); continue; }
        set_scl(b, 0);
        m_stop(b);
        if (b->sda && b->scl) return REC_OK;
    }
    return REC_ERR_SDA_STUCK;
}

static int alg_start_stop(Bus *b) {
    set_sda(b, 1); set_scl(b, 1);
    if (b->sda) return REC_OK;
    for (int p = 1; p <= REC_MAX_PULSES; p++) {
        set_scl(b, 0); set_scl(b, 1);
        if (b->sda) { set_sda(b, 0); set_sda(b, 1); return (b->sda && b->scl) ? REC_OK : REC_ERR_SDA_STUCK; }
    }
    return REC_ERR_SDA_STUCK;
}

enum { NALG = 4 };
static const char *ALG_NAME[NALG] = {"fold (solution)", "naive", "naive+retry", "START+STOP"};
static const RecFn ALG[NALG] = {alg_fold, alg_naive, alg_naive_retry, alg_start_stop};

typedef struct { long scen, hung, ok, over9, void_msgs, stops_seen; long clk_sum; int clk_max; long hist[41]; } Stat;

int main(void) {
    static Stat st[NALG][TX_KINDS];
    memset(st, 0, sizeof st);
    for (int a = 0; a < NALG; a++) {
        xs_state = 0x2545F491u;                              /* 알고리즘마다 같은 데이터 */
        Bus b; Slave s1, s2;
        bus_init(&b); bus_add_slave(&b, &s1, 0x50); bus_add_slave(&b, &s2, 0x68);
        for (int kind = 0; kind < TX_KINDS; kind++) {
            for (int v = 0; v < 256; v++) {
                uint8_t d[3] = {(uint8_t)v, (uint8_t)~v, (uint8_t)xorshift32()};
                for (uint32_t k = 0;; k++) {
                    Stat *S = &st[a][kind];
                    load_slave(&s1, d);
                    arm_reset(&b, k);
                    run_tx(&b, (TxKind)kind, d);
                    bool fired = b.reset_fired;
                    master_reboot(&b, 3);
                    bool hung = !b.sda;
                    uint32_t f0 = b.scl_falls, st2 = s2.starts, sp2 = s2.stops;
                    int rc = ALG[a](&b);
                    int clk = (int)(b.scl_falls - f0);
                    bool ok = rc == REC_OK && b.sda && b.scl;
                    /* 복구 중 무고한 슬레이브가 본 START 수 (주소 없이 바로 STOP 이 오면 void message) */
                    S->void_msgs += s2.starts - st2;
                    S->stops_seen += s2.stops - sp2;
                    if (!ok) {                                   /* 실패: 다음 시나리오를 위해 제대로 된 복구로 정리 */
                        RecReport r; (void)i2c_bus_recover(&b, &r);
                    }
                    ok = ok && verify_bus(&b, &s1, &s2);
                    if (!ok) { RecReport r; (void)i2c_bus_recover(&b, &r); (void)verify_bus(&b, &s1, &s2); }
                    S->scen++;
                    if (hung) {
                        S->hung++;
                        S->ok += ok;
                        S->clk_sum += clk;
                        if (clk > S->clk_max) S->clk_max = clk;
                        if (clk > 9) S->over9++;
                        S->hist[clk > 40 ? 40 : clk]++;
                    }
                    if (!fired) break;
                }
            }
        }
    }
    printf("hung scenarios only (SDA low after reboot). ok = recovered + 2 normal transactions pass\n");
    printf("%-16s %-17s %6s %6s %7s %6s %6s %6s\n", "algorithm", "transaction", "hung", "ok", "ok%", "maxclk", "avgclk", ">9clk");
    for (int a = 0; a < NALG; a++)
        for (int kind = 0; kind < TX_KINDS; kind++) {
            const Stat *S = &st[a][kind];
            printf("%-16s %-17s %6ld %6ld %6.2f%% %6d %6.2f %6ld\n", ALG_NAME[a], KIND_NAME[kind], S->hung, S->ok,
                   100.0 * (double)S->ok / (double)S->hung, S->clk_max, (double)S->clk_sum / (double)S->hung, S->over9);
        }
    for (int a = 0; a < NALG; a++) {
        long vm = 0, sp = 0, hg = 0;
        for (int kind = 0; kind < TX_KINDS; kind++) { vm += st[a][kind].void_msgs; sp += st[a][kind].stops_seen; hg += st[a][kind].hung; }
        printf("innocent slave 0x68 during recovery, %-16s: STARTs %ld, STOPs %ld (hung scenarios %ld)\n", ALG_NAME[a], vm, sp, hg);
    }
    printf("clock histogram (all transactions, hung only):\n");
    for (int a = 0; a < NALG; a++) {
        printf("  %-16s", ALG_NAME[a]);
        int top = 0;
        for (int c = 0; c <= 40; c++) for (int kind = 0; kind < TX_KINDS; kind++) if (st[a][kind].hist[c]) top = c;
        for (int c = 1; c <= top; c++) {
            long n = 0;
            for (int kind = 0; kind < TX_KINDS; kind++) n += st[a][kind].hist[c];
            printf(" %d:%ld", c, n);
        }
        putchar('\n');
    }
    /* 실제 시간 하한: 클럭 W 개 × T_SCL + tSU;STO + tBUF (UM10204 Rev.7 Table 10 의 최소값) */
    const struct { const char *name; double f_hz, tsu_sto_us, tbuf_us; } mode[3] = {
        {"Standard 100 kHz", 100e3, 4.0, 4.7}, {"Fast 400 kHz", 400e3, 0.6, 1.3}, {"Fast+ 1 MHz", 1e6, 0.26, 0.5}};
    printf("worst-case recovery time (lower bound) for W clocks:\n");
    int nr_max = 0;
    for (int kind = 0; kind < TX_KINDS; kind++) if (st[2][kind].clk_max > nr_max) nr_max = st[2][kind].clk_max;
    int W3[2] = {9, nr_max};
    for (int m = 0; m < 3; m++) {
        printf("  %-17s", mode[m].name);
        for (int i = 0; i < 2; i++)
            printf("  W=%2d: %7.2f us", W3[i], W3[i] * 1e6 / mode[m].f_hz + mode[m].tsu_sto_us + mode[m].tbuf_us);
        putchar('\n');
    }
    return 0;
}
