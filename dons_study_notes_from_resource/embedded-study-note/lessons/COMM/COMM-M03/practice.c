/* COMM-M03 추가 연습 — 한 번에 검증
 *
 * 모델·드라이버·Part A/B 는 solution.c 와 같은 코드 (복사본, 자기완결).
 * 연습 1: START+STOP 변형 복구 — 클럭 수는 같고, 무고한 슬레이브는 void message(S 바로 P)를 본다
 * 연습 2: "SDA=1 보이면 SCL↓ 후 STOP" + 재시도 — 다 풀리지만 9 클럭 예산을 넘는 경우 (0x40 → 10)
 * 연습 3: SDA 를 잡을 수 있는 슬레이브 상태 전수 → 닫힌 식 = 시뮬레이션, 9 는 단 한 상태
 * 연습 4: 1 ms 주기 버스 감시(hang monitor) → 감지 후 복구
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
 */

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

/* ---------------- Part B: 몇 펄스면 풀리나 (왜 9 면 충분한가) ---------------- */

/* SCL=1 로 멈춘 버스에서 슬레이브 s 를 풀어 주는 데 rec_pulse 가 몇 번 필요한가.
 * 0 = 이 슬레이브는 SDA 를 안 잡고 있음, -1 = SDA 를 잡을 상태가 아닌데 잡고 있음(모델 밖). */
static int predict_pulses(const Slave *s, const Bus *b) {
    if (b->sda_drv[s->node]) return 0;
    switch (s->st) {
    case S_RX_ACK:
        return 1;                                        /* 쓰기 ACK: 다음 SCL↓ 에서 놓음 */
    case S_ADDR_ACK:
        if (!s->rw) return 1;
        {   /* 읽기 주소 ACK: 다음 SCL↓ 에서 데이터 MSB 를 낸다 → 첫 1 비트 또는 ACK 슬롯까지 */
            uint8_t nx = s->tx_pos < s->tx_len ? s->tx[s->tx_pos] : 0xFF;
            int lz = 0;
            while (lz < 8 && !((nx >> (7 - lz)) & 1)) lz++;
            return 1 + lz;                               /* 0x00 → 1 + 8 = 9 (최악) */
        }
    case S_TX: {
        int c = 8 - s->nbits;                            /* SCL=1 이므로 지금 내는 비트 번호 (7..0) */
        if (c < 0 || c > 7) return -1;
        for (int k = 1; k <= c; k++)
            if ((s->sh >> (c - k)) & 1) return k;        /* k 번째 SCL↓ 에서 1 비트 = 놓음 */
        return c + 1;                                    /* 남은 비트가 전부 0 → ACK 슬롯에서 놓음 */
    }
    default:
        return -1;
    }
}

/* ---------------- 테스트 하네스 (주어짐) ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

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


/* ================= 연습 답 ================= */

/* 연습 1: START+STOP 변형 — SDA 를 놓은 채 SCL 만 토글, SDA=1 이 보이면 SCL=1 인 채로 SDA↓(START) → SDA↑(STOP).
 * 반환 REC_OK / REC_ERR_SDA_STUCK, *clocks = 낸 SCL↓ 수 */
static int recover_sp(Bus *b, int *clocks) {
    uint32_t f0 = b->scl_falls;
    set_sda(b, 1); set_scl(b, 1);
    int r = REC_ERR_SDA_STUCK;
    if (b->sda) r = REC_OK;
    for (int p = 1; p <= REC_MAX_PULSES && r != REC_OK; p++) {
        set_scl(b, 0); set_scl(b, 1);
        if (b->sda) { set_sda(b, 0); set_sda(b, 1); r = (b->sda && b->scl) ? REC_OK : REC_ERR_SDA_STUCK; break; }
    }
    *clocks = (int)(b->scl_falls - f0);
    return r;
}

/* 연습 2: 흔한 방식 + 재시도 — SDA=1 이 보이면 SCL↓ 후 STOP, 실패하면 클럭을 더 낸다 (총 budget 클럭) */
static int recover_naive_retry(Bus *b, int budget, int *clocks) {
    uint32_t f0 = b->scl_falls;
    int r = REC_ERR_SDA_STUCK;
    set_sda(b, 1); set_scl(b, 1);
    if (b->sda) r = REC_OK;
    while (r != REC_OK && (int)(b->scl_falls - f0) < budget) {
        if (!b->sda) { set_scl(b, 0); set_scl(b, 1); continue; }
        set_scl(b, 0);                       /* STOP 을 만들려면 SCL 을 먼저 내려야 한다 = 슬레이브에게는 클럭 */
        m_stop(b);
        if (b->sda && b->scl) r = REC_OK;
    }
    *clocks = (int)(b->scl_falls - f0);
    return r;
}

/* 연습 3: 슬레이브 상태 공간 전수용 — 상태를 직접 만들어 놓고 닫힌 식과 비교 */
static int pulses_formula(SState st, bool rw, int nbits, uint8_t sh_or_next) {
    if (st == S_RX_ACK || (st == S_ADDR_ACK && !rw)) return 1;
    if (st == S_ADDR_ACK) {                   /* 다음 바이트 MSB 부터: 1 + 앞쪽 0 개수 */
        int lz = 0;
        while (lz < 8 && !((sh_or_next >> (7 - lz)) & 1)) lz++;
        return 1 + lz;
    }
    int c = 8 - nbits;                        /* S_TX: 지금 내는 비트 */
    for (int k = 1; k <= c; k++) if ((sh_or_next >> (c - k)) & 1) return k;
    return c + 1;
}
static void place_state(Bus *b, Slave *s, SState st, bool rw, int nbits, uint8_t v) {
    s->st = st; s->rw = rw; s->nbits = nbits; s->master_ack = false;
    if (st == S_TX) s->sh = v;
    s->tx[0] = v; s->tx_len = 1; s->tx_pos = 0;
    b->sda_drv[s->node] = 0;                  /* 이 상태들은 전부 SDA 를 잡고 있다 */
    bus_resolve(b);
    s->prev_scl = b->scl; s->prev_sda = b->sda;
}

/* 연습 4: 1 ms 주기 버스 감시 — 드라이버가 쉬는데 SDA LOW(SCL HIGH) 또는 SCL LOW 가 thresh 샘플 연속이면 한 번 보고 */
enum { HANG_NONE = 0, HANG_SDA = 1, HANG_SCL = 2 };
typedef struct { int thresh, n_sda, n_scl; } HangMon;
static int hang_mon_sample(HangMon *m, int sda, int scl, bool drv_busy) {
    if (drv_busy || (sda && scl)) { m->n_sda = m->n_scl = 0; return HANG_NONE; }
    if (!scl) { m->n_sda = 0; return ++m->n_scl == m->thresh ? HANG_SCL : HANG_NONE; }   /* SCL 우선: 클럭을 못 낸다 */
    m->n_scl = 0;
    return ++m->n_sda == m->thresh ? HANG_SDA : HANG_NONE;
}

int main(void) {
    Bus b; Slave s1, s2;

    /* ===== 연습 1 + 2: 리셋 위치 × 256 패턴 전수 (읽기 3B, 레지스터 읽기 Sr) ===== */
    long hung = 0, sp_bad = 0, sp_starts = 0, nr_fail = 0, nr_over9 = 0;
    int sp_max = 0, nr_max = 0;
    for (int alg = 0; alg < 2; alg++) {
        xs_state = 0x2545F491u;
        bus_init(&b); bus_add_slave(&b, &s1, 0x50); bus_add_slave(&b, &s2, 0x68);
        for (int kind = TX_READ; kind <= TX_WREAD; kind++)
            for (int v = 0; v < 256; v++) {
                uint8_t d[3] = {(uint8_t)v, (uint8_t)~v, (uint8_t)xorshift32()};
                for (uint32_t k = 0;; k++) {
                    load_slave(&s1, d); arm_reset(&b, k); run_tx(&b, (TxKind)kind, d);
                    bool fired = b.reset_fired;
                    master_reboot(&b, 3);
                    bool h = !b.sda;
                    int pred = predict_pulses(&s1, &b), clk = 0;
                    uint32_t st2 = s2.starts;
                    if (alg == 0) {
                        int r = recover_sp(&b, &clk);
                        if (h) { hung++; sp_starts += s2.starts - st2; }
                        if (r != REC_OK || clk != pred || !verify_bus(&b, &s1, &s2)) sp_bad++;
                        if (clk > sp_max) sp_max = clk;
                    } else {
                        int r = recover_naive_retry(&b, 40, &clk);
                        if (r != REC_OK || !verify_bus(&b, &s1, &s2)) nr_fail++;
                        if (clk > 9) nr_over9++;
                        if (clk > nr_max) nr_max = clk;
                    }
                    if (!fired) break;
                }
            }
    }
    printf("ex1 START+STOP: hung=%ld bad=%ld max clocks=%d, STARTs seen by innocent slave=%ld\n", hung, sp_bad, sp_max, sp_starts);
    printf("ex2 naive+retry: fail=%ld max clocks=%d, cases over 9 clocks=%ld\n", nr_fail, nr_max, nr_over9);
    CHECK(hung > 0 && sp_bad == 0 && sp_max == 9);
    CHECK(sp_starts == hung);                              /* 복구마다 주소 없는 START 하나 = void message */
    CHECK(nr_fail == 0 && nr_max == 10 && nr_over9 > 0);   /* 다 풀리긴 하지만 9 클럭 예산을 넘는다 */

    /* 연습 2 이름 붙은 경우: 읽기 주소 ACK 중 리셋 + 데이터 0x40 → 10 클럭 */
    bus_init(&b); bus_add_slave(&b, &s1, 0x50); bus_add_slave(&b, &s2, 0x68);
    const uint8_t d40[3] = {0x40, 0x00, 0x00};
    uint32_t k40 = 0;
    for (uint32_t k = 0; k < 200; k++) {
        Bus sv = b; Slave ss = s1, ss2 = s2;
        load_slave(&s1, d40); arm_reset(&b, k); run_tx(&b, TX_READ, d40); master_reboot(&b, 3);
        bool hit = s1.st == S_ADDR_ACK && s1.rw;
        b = sv; s1 = ss; s2 = ss2;
        if (hit) { k40 = k; break; }
    }
    load_slave(&s1, d40); arm_reset(&b, k40); run_tx(&b, TX_READ, d40); master_reboot(&b, 3);
    int clk = 0;
    CHECK(recover_naive_retry(&b, 40, &clk) == REC_OK && clk == 10);
    CHECK(verify_bus(&b, &s1, &s2));
    load_slave(&s1, d40); arm_reset(&b, k40); run_tx(&b, TX_READ, d40); master_reboot(&b, 3);
    CHECK(recover_naive_retry(&b, 9, &clk) == REC_ERR_SDA_STUCK && clk == 9);   /* 9 클럭 예산이면 실패 */
    RecReport rep;
    CHECK(i2c_bus_recover(&b, &rep) == REC_OK);

    /* ===== 연습 3: SDA 를 잡을 수 있는 슬레이브 상태 전수 → 닫힌 식 = 시뮬레이션, 최댓값 9 ===== */
    int nstates = 0, mism = 0, maxp = 0, n9 = 0;
    for (int kind = 0; kind < 4; kind++) {
        int nb_lo = kind == 3 ? 1 : 0, nb_hi = kind == 3 ? 8 : 0;
        for (int nb = nb_lo; nb <= nb_hi; nb++)
            for (int v = 0; v < 256; v++) {
                SState st = kind == 0 ? S_RX_ACK : (kind == 3 ? S_TX : S_ADDR_ACK);
                bool rw = kind == 2;
                if (kind < 2 && v > 0) continue;                       /* 쓰기 ACK 는 데이터 무관: 1 개씩 */
                if (kind == 3 && ((v >> (8 - nb)) & 1)) continue;      /* 지금 비트가 1 이면 SDA 를 안 잡음 */
                bus_init(&b); bus_add_slave(&b, &s1, 0x50);
                place_state(&b, &s1, st, rw, nb, (uint8_t)v);
                int want = pulses_formula(st, rw, nb, (uint8_t)v);
                int r = i2c_bus_recover(&b, &rep);
                nstates++;
                if (r != REC_OK || rep.pulses != want || predict_pulses(&s1, &b) != 0) mism++;
                if (rep.pulses > maxp) maxp = rep.pulses;
                if (rep.pulses == 9) n9++;
            }
    }
    printf("ex3 state space: %d states, mismatches=%d, max pulses=%d (count %d)\n", nstates, mism, maxp, n9);
    CHECK(nstates == 1 + 1 + 256 + 8 * 128);
    CHECK(mism == 0 && maxp == 9 && n9 == 1);                  /* 9 는 '읽기 주소 ACK + 다음 바이트 0x00' 하나뿐 */

    /* ===== 연습 4: 행 모니터 ===== */
    HangMon m = {3, 0, 0};
    CHECK(hang_mon_sample(&m, 1, 1, false) == HANG_NONE);
    CHECK(hang_mon_sample(&m, 0, 1, false) == HANG_NONE && hang_mon_sample(&m, 0, 1, false) == HANG_NONE);
    CHECK(hang_mon_sample(&m, 0, 1, false) == HANG_SDA);       /* 3 번째 연속 샘플에서 한 번 */
    CHECK(hang_mon_sample(&m, 0, 1, false) == HANG_NONE);      /* 중복 보고 안 함 */
    CHECK(hang_mon_sample(&m, 0, 1, true) == HANG_NONE && m.n_sda == 0);   /* 드라이버 전송 중 LOW 는 정상 */
    int seq_r = HANG_NONE;
    for (int i = 0; i < 3; i++) seq_r = hang_mon_sample(&m, 0, 0, false);
    CHECK(seq_r == HANG_SCL);                                  /* 둘 다 LOW → SCL 원인으로 분류 */
    /* 모델 버스에 붙여 보기: 리셋 hang 뒤 10 틱마다 샘플 → 감지 → 복구 */
    bus_init(&b); bus_add_slave(&b, &s1, 0x50); bus_add_slave(&b, &s2, 0x68);
    const uint8_t d00[3] = {0, 0, 0};
    load_slave(&s1, d00); arm_reset(&b, k40); run_tx(&b, TX_READ, d00); master_reboot(&b, 3);
    HangMon m2 = {3, 0, 0};
    int got = HANG_NONE, samples = 0;
    while (got == HANG_NONE && samples < 100) {
        for (int t = 0; t < 10; t++) bus_idle_tick(&b);
        got = hang_mon_sample(&m2, b.sda, b.scl, false);
        samples++;
    }
    CHECK(got == HANG_SDA && samples == 3);
    CHECK(i2c_bus_recover(&b, &rep) == REC_OK && rep.pulses == 9 && verify_bus(&b, &s1, &s2));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
