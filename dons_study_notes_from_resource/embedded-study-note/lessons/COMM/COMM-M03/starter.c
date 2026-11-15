/* COMM-M03 I2C 버스 복구: SDA stuck-low 에서 9클럭 + STOP
 *
 * 모델 (COMM-B06 모델 축약판): SDA/SCL = 모든 노드 드라이버의 wired-AND (1 = 놓음, 풀업 → 1).
 *   마스터가 선 하나를 바꿀 때마다 bus_tick() 1번. 슬레이브는 SCL↑ 에서 샘플, SCL↓ 에서만 SDA 변경,
 *   START(SCL=1 중 SDA↓) / STOP(SCL=1 중 SDA↑) 은 어느 상태에서든 최우선.
 * 고장 주입: 마스터 리셋(트랜잭션 도중 k 번째 동작에서 핀이 입력으로 → 두 선 놓음, 슬레이브는 그대로),
 *   고장 노드(SDA 영구 LOW, SCL 을 [from, until) 틱 동안 LOW), 슬레이브 ACK 슬롯 글리치.
 * Part A: bus_state / rec_pulse / i2c_bus_recover — 펄스마다 STOP 을 시도하는 9클럭 복구
 * Part B: predict_pulses — 슬레이브 상태로 필요한 펄스 수 예측 (왜 9 면 충분한가) + 고장별 에러 코드
 * Part C: dev_ensure_idle / i2c_xfer_robust — busy·STOP 실패 감지 → 복구 → 전원 재인가 → 재시도 + 로그
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
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
    /* TODO(A1): bit0 = SDA 가 LOW, bit1 = SCL 이 LOW */
    (void)b;
    return BUS_IDLE;
}

/* SCL 을 놓고 정말 1 이 될 때까지 최대 SCL_WAIT_MAX 틱 기다린다. 반환 = 기다린 틱, 실패면 -1 */
static int wait_scl_high(Bus *b) {
    /* TODO(A2): SCL 이 0 인 동안 bus_idle_tick() 하며 최대 SCL_WAIT_MAX 틱 기다린다 (스트레칭 존중) */
    (void)b;
    return 0;
}

/* 복구 펄스 1개 (SDA 가 SCL 을 반 박자 늦게 따라감 = 펄스마다 STOP 시도):
 *   SCL↓ → SDA 당김(SCL=0 이라 조건 아님) → SCL 놓음 + 1 될 때까지 대기 → SDA 놓음.
 *   슬레이브가 이번 SCL↓ 에서 SDA 를 놓았다면 마지막 동작이 SCL=1 중 SDA↑ = STOP.
 * 반환: 1 = 놓은 뒤 SDA=1 (STOP 생성됨), 0 = 아직 누가 SDA 를 잡음, REC_ERR_SCL_STUCK. *waited += 대기 틱 */
static int rec_pulse(Bus *b, int *waited) {
    /* TODO(A3): SCL↓ → SDA↓ → SCL 놓고 wait_scl_high → SDA 놓기. 놓은 뒤 SDA 를 읽어 반환 */
    (void)b; (void)waited;
    return 0;
}

/* UM10204 §3.1.16 bus clear + STOP. 핀은 이미 GPIO 오픈드레인이라고 가정 (모델의 node 0).
 * 순서: 두 선 놓기 → SCL 확인(LOW 면 기다렸다가 SCL_STUCK) → SDA 이미 1 이면 0 펄스로 OK
 *       → 최대 9 펄스, 펄스마다 STOP 시도, SDA=1 이 되면 끝 → 9 번 뒤에도 0 이면 SDA_STUCK. */
static int i2c_bus_recover(Bus *b, RecReport *rep) {
    /* TODO(A4): 두 선 놓기 → SCL 확인 → SDA 이미 1 이면 끝 → 최대 9 펄스 → 결과 코드 */
    memset(rep, 0, sizeof *rep);
    (void)b;
    return REC_OK;
}

/* ---------------- Part B: 몇 펄스면 풀리나 (왜 9 면 충분한가) ---------------- */

/* SCL=1 로 멈춘 버스에서 슬레이브 s 를 풀어 주는 데 rec_pulse 가 몇 번 필요한가.
 * 0 = 이 슬레이브는 SDA 를 안 잡고 있음, -1 = SDA 를 잡을 상태가 아닌데 잡고 있음(모델 밖). */
static int predict_pulses(const Slave *s, const Bus *b) {
    /* TODO(B1): 안 당기면 0 / 쓰기 ACK 1 / 읽기 주소 ACK 1 + (다음 바이트의 앞쪽 0 개수) / S_TX 는 지금 비트 c = 8 - nbits 부터 */
    (void)s; (void)b;
    return 0;
}

/* ---------------- Part C: 드라이버 경로의 복구 ---------------- */
typedef struct I2cDev I2cDev;
struct I2cDev {
    Bus     *b;
    char    *log;
    size_t   log_cap, log_len;
    bool     log_trunc;
    unsigned n_recover, n_recover_fail, n_power_cycle, n_retry;
    int      last_pulses;
    void   (*power_cycle)(Bus *b, void *ctx);   /* 보드의 센서 전원 스위치 / 리셋 핀 (없으면 NULL) */
    void    *pc_ctx;
};
typedef struct { uint8_t addr7; bool read; uint8_t *buf; size_t n; } I2cOp;

__attribute__((format(printf, 2, 3)))
static void dlog(I2cDev *d, const char *fmt, ...) {        /* "[t=  123] i2c0: ...\n", 용량 넘으면 자르고 표시 */
    if (!d->log || d->log_cap == 0 || d->log_trunc) return;
    size_t room = d->log_cap - d->log_len;
    int n = snprintf(d->log + d->log_len, room, "[t=%5u] i2c0: ", (unsigned)d->b->ticks);
    if (n < 0 || (size_t)n >= room) { d->log_len = d->log_cap - 1; d->log_trunc = true; return; }
    d->log_len += (size_t)n; room -= (size_t)n;
    va_list ap;
    va_start(ap, fmt);
    n = vsnprintf(d->log + d->log_len, room, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n + 1 >= room) { d->log_len = d->log_cap - 1; d->log[d->log_len] = '\0'; d->log_trunc = true; return; }
    d->log_len += (size_t)n;
    d->log[d->log_len++] = '\n';
    d->log[d->log_len] = '\0';
}

/* START 전에 부른다. 버스가 idle 이 아니면: 복구 → (실패 + 전원 훅 있으면) 전원 재인가 → 복구 다시.
 * 반환: I2C_OK 또는 I2C_ERR_BUS_STUCK. 카운터·로그를 남긴다. */
static int dev_ensure_idle(I2cDev *d) {
    /* TODO(C1): idle 이면 OK. 아니면 로그 → i2c_bus_recover → 실패 + 훅이면 power_cycle → 다시 복구 → 아니면 BUS_STUCK */
    (void)d;
    return I2C_OK;
}

/* 버스 수준 고장만 여기서 재시도한다 (NACK 재시도 정책은 상위 계층 몫).
 * 시도마다 dev_ensure_idle → 전송. STOP_FAILED 면 1번 더. 마지막에도 실패하면 버스를 정리하고 에러. */
static int i2c_xfer_robust(I2cDev *d, const I2cOp *op) {
    /* TODO(C2): 최대 2번: dev_ensure_idle → 전송. STOP_FAILED 만 재시도, 끝까지 실패면 버스 정리 후 에러 */
    (void)d; (void)op;
    return I2C_ERR_ARG;
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
static void trace_start(Bus *b) {
    b->trace_on = true; b->tr_len = 1;
    b->tr_scl[0] = (char)('0' + b->scl); b->tr_sda[0] = (char)('0' + b->sda);
    b->tr_scl[1] = b->tr_sda[1] = '\0';
}
static void trace_print(const Bus *b, const char *title) {
    printf("%s (%u ticks)\n  SCL ", title, (unsigned)b->tr_len);
    for (uint32_t i = 0; i < b->tr_len; i++) putchar(b->tr_scl[i] == '1' ? '-' : '_');
    printf("\n  SDA ");
    for (uint32_t i = 0; i < b->tr_len; i++) putchar(b->tr_sda[i] == '1' ? '-' : '_');
    printf("\n      ");
    for (uint32_t i = 0; i < b->tr_len; i++) {
        char c = ' ';
        if (i > 0 && b->tr_scl[i - 1] == '1' && b->tr_scl[i] == '1' && b->tr_sda[i - 1] != b->tr_sda[i])
            c = b->tr_sda[i] == '0' ? 'S' : 'P';
        else if (i > 0 && b->tr_scl[i - 1] == '1' && b->tr_scl[i] == '0') c = 'v';
        putchar(c);
    }
    putchar('\n');
}
static uint32_t xs_state = 0x2545F491u;
static uint32_t xorshift32(void) {
    uint32_t x = xs_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return xs_state = x;
}

/* 흔한 잘못된 복구 (비교용): SDA 놓고 SCL 토글 → SDA=1 보이면 그때 STOP(SCL↓, SDA↓, SCL↑, SDA↑).
 * 문제: STOP 을 만들려고 내린 SCL↓ 이 슬레이브에게는 "다음 비트 내라" 클럭이다. */
static int recover_naive(Bus *b) {
    set_sda(b, 1); set_scl(b, 1);
    for (int p = 1; p <= REC_MAX_PULSES && !b->sda; p++) { set_scl(b, 0); set_scl(b, 1); }
    if (!b->sda) return REC_ERR_SDA_STUCK;
    set_scl(b, 0);                                   /* SCL=1 에서 SDA 를 내리면 START 이므로 먼저 SCL↓ */
    m_stop(b);                                       /* SDA↓(SCL=0) → SCL↑ → SDA↑ */
    return (b->sda && b->scl) ? REC_OK : REC_ERR_SDA_STUCK;
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

typedef struct { bool fired; int pred, pred2, rc, pulses; bool idle_after, one_stop, s2_quiet, prefix_ok, verify_ok; } Scen;

/* 트랜잭션을 k 번째 마스터 동작에서 리셋 → 부트 3틱 → 예측 → 복구 → 검증 */
static Scen scenario(Bus *b, Slave *s1, Slave *s2, TxKind kind, const uint8_t d[3], uint32_t k) {
    Scen r; memset(&r, 0, sizeof r);
    load_slave(s1, d);
    arm_reset(b, k);
    run_tx(b, kind, d);
    r.fired = b->reset_fired;
    master_reboot(b, 3);
    r.pred = predict_pulses(s1, b);
    r.pred2 = predict_pulses(s2, b);
    uint32_t mv0 = b->sda_moves_scl_high, st2 = s2->starts, am2 = s2->addr_matches;
    RecReport rep;
    r.rc = i2c_bus_recover(b, &rep);
    r.pulses = rep.pulses;
    r.idle_after = bus_state(b) == BUS_IDLE;
    r.one_stop = b->sda_moves_scl_high - mv0 == (rep.pulses ? 1u : 0u);   /* 복구는 STOP 하나만 만든다 */
    r.s2_quiet = s2->starts == st2 && s2->addr_matches == am2 && b->sda_drv[s2->node] == 1;
    size_t pre = s1->rx_len;
    const uint8_t *want = d;                                              /* 쓰기: 끝난 바이트만 저장됐나 */
    r.prefix_ok = pre <= 3 && (kind == TX_READ ? pre == 0 : memcmp(s1->rx, want, pre) == 0);
    if (kind == TX_WREAD) r.prefix_ok = pre <= 1 && (pre == 0 || s1->rx[0] == d[0]);
    r.verify_ok = verify_bus(b, s1, s2);
    return r;
}

/* 데이터 0 비트를 내는 중(S_TX)에 리셋되는 첫 k 를 찾는다: 읽기 첫 바이트의 비트 7 (nbits==1) */
static uint32_t find_reset_in_tx(Bus *b, Slave *s1, const uint8_t d[3], int nbits) {
    for (uint32_t k = 0; k < 200; k++) {
        Bus save = *b; Slave s1s = *s1;
        load_slave(s1, d);
        arm_reset(b, k);
        run_tx(b, TX_READ, d);
        master_reboot(b, 3);
        bool hit = s1->st == S_TX && s1->nbits == nbits && s1->tx_pos == 1;
        *b = save; *s1 = s1s;                       /* 버스·슬레이브 상태 되돌림 (슬레이브 포인터는 그대로) */
        if (hit) return k;
    }
    return 0;
}
static uint32_t find_reset_in_addr_ack(Bus *b, Slave *s1, const uint8_t d[3]) {
    for (uint32_t k = 0; k < 200; k++) {
        Bus save = *b; Slave s1s = *s1;
        load_slave(s1, d);
        arm_reset(b, k);
        run_tx(b, TX_READ, d);
        master_reboot(b, 3);
        bool hit = s1->st == S_ADDR_ACK && s1->rw;
        *b = save; *s1 = s1s;
        if (hit) return k;
    }
    return 0;
}

static void power_cycle_hook(Bus *b, void *ctx) {    /* 센서 전원 재인가: 고장 해제 + 슬레이브 POR */
    Slave *s = (Slave *)ctx;
    b->fault_sda_low = false;
    b->sda_drv[s->node] = 1;
    s->st = S_IDLE; s->nbits = 0;
    bus_idle_tick(b);
}

int main(void) {
    Bus b; Slave s1, s2;
    RecReport rep;
    const uint8_t d12[3] = {0x12, 0x34, 0x56};

    /* ===== Part A: bus_state ===== */
    bus_init(&b);
    CHECK(bus_state(&b) == BUS_IDLE);
    b.fault_sda_low = true; bus_idle_tick(&b);
    CHECK(bus_state(&b) == BUS_SDA_LOW);
    b.fault_scl_from = 0; b.fault_scl_until = UINT32_MAX; bus_idle_tick(&b);
    CHECK(bus_state(&b) == BUS_BOTH_LOW);
    b.fault_sda_low = false; bus_idle_tick(&b);
    CHECK(bus_state(&b) == BUS_SCL_LOW);

    /* wait_scl_high: SCL 을 놓은 뒤 정말 1 이 될 때까지 다시 읽으며 기다림 (최대 SCL_WAIT_MAX 틱) */
    bus_init(&b);
    b.fault_scl_from = 0; b.fault_scl_until = 5; bus_idle_tick(&b);
    CHECK(wait_scl_high(&b) == 5 && b.scl == 1);                 /* [0,5) 동안 LOW: 기다린 5번째 틱에 1 */
    b.fault_scl_from = b.ticks; b.fault_scl_until = UINT32_MAX; bus_idle_tick(&b);
    uint32_t tw = b.ticks;
    CHECK(wait_scl_high(&b) == -1 && b.ticks - tw == SCL_WAIT_MAX);   /* 영원히 LOW → 유한 시간 후 포기 */

    /* ===== Part A: 과제 시나리오 — 읽기 도중 마스터 리셋, 슬레이브가 데이터 0 비트(0x12 의 MSB)를 내는 중 ===== */
    bus_init(&b); bus_add_slave(&b, &s1, 0x50); bus_add_slave(&b, &s2, 0x68);
    uint32_t k = find_reset_in_tx(&b, &s1, d12, 1);
    CHECK(k > 0);
    load_slave(&s1, d12);
    arm_reset(&b, k);
    run_tx(&b, TX_READ, d12);
    CHECK(b.reset_fired);
    master_reboot(&b, 3);
    CHECK(bus_state(&b) == BUS_SDA_LOW);                         /* SCL=1, SDA=0: hang */
    CHECK(s1.st == S_TX && s1.nbits == 1);
    CHECK(i2c_write(&b, 0x68, d12, 1) == I2C_ERR_BUS_BUSY);      /* 그냥 쓰면 START 를 못 만든다 */
    CHECK(predict_pulses(&s1, &b) == 3 && predict_pulses(&s2, &b) == 0);   /* 0x12 = 0 0 0 1 ...: 비트6=0, 비트5=0, 비트4=1 */
    trace_start(&b);
    uint32_t f0 = b.scl_falls, mv0 = b.sda_moves_scl_high;
    CHECK(i2c_bus_recover(&b, &rep) == REC_OK);
    CHECK(rep.before == BUS_SDA_LOW && rep.after == BUS_IDLE && rep.pulses == 3);
    CHECK(rep.sda_after[0] == 0 && rep.sda_after[1] == 0 && rep.sda_after[2] == 1);
    CHECK(b.scl_falls - f0 == 3);                                 /* 클럭은 정확히 3개 */
    CHECK(b.sda_moves_scl_high - mv0 == 1 && s1.stops >= 1 && s1.st == S_IDLE);   /* STOP 하나 */
    trace_print(&b, "recovery: slave was sending 0x12, reset at MSB");
    b.trace_on = false;
    CHECK(verify_bus(&b, &s1, &s2));

    /* rec_pulse 단독: 한 펄스 = SCL↓, SDA↓, SCL↑, SDA↑ = 4틱 */
    k = find_reset_in_tx(&b, &s1, d12, 1);
    load_slave(&s1, d12); arm_reset(&b, k); run_tx(&b, TX_READ, d12); master_reboot(&b, 3);
    int waited = 0;
    uint32_t t0 = b.ticks;
    CHECK(rec_pulse(&b, &waited) == 0 && b.ticks - t0 == 4 && waited == 0);   /* 비트 6 = 0: 아직 잡힘 */
    CHECK(rec_pulse(&b, &waited) == 0);                                       /* 비트 5 = 0 */
    CHECK(rec_pulse(&b, &waited) == 1 && bus_state(&b) == BUS_IDLE && s1.st == S_IDLE);  /* 비트 4 = 1 → STOP */
    CHECK(verify_bus(&b, &s1, &s2));

    /* ===== Part B: 최악 = 읽기 주소 ACK 중 리셋 + 첫 데이터 0x00 → 9 펄스 ===== */
    const uint8_t d00[3] = {0x00, 0x00, 0x00};
    k = find_reset_in_addr_ack(&b, &s1, d00);
    load_slave(&s1, d00); arm_reset(&b, k); run_tx(&b, TX_READ, d00); master_reboot(&b, 3);
    CHECK(s1.st == S_ADDR_ACK && predict_pulses(&s1, &b) == 9);
    f0 = b.scl_falls;
    CHECK(i2c_bus_recover(&b, &rep) == REC_OK && rep.pulses == 9 && b.scl_falls - f0 == 9);
    int zeros = 0;
    for (int i = 0; i < 8; i++) zeros += rep.sda_after[i] == 0;
    CHECK(zeros == 8 && rep.sda_after[8] == 1);                   /* ACK 슬롯에서야 놓음 */
    CHECK(verify_bus(&b, &s1, &s2));

    /* 같은 자리, 데이터 0x40: 펄스마다 STOP 시도 → 2 펄스. 잘못된 복구는 실패 */
    const uint8_t d40[3] = {0x40, 0x00, 0x00};
    Bus bsave = b; Slave s1save = s1, s2save = s2;
    load_slave(&s1, d40); arm_reset(&b, k); run_tx(&b, TX_READ, d40); master_reboot(&b, 3);
    CHECK(predict_pulses(&s1, &b) == 2);
    CHECK(i2c_bus_recover(&b, &rep) == REC_OK && rep.pulses == 2);
    b = bsave; s1 = s1save; s2 = s2save;
    load_slave(&s1, d40); arm_reset(&b, k); run_tx(&b, TX_READ, d40); master_reboot(&b, 3);
    CHECK(recover_naive(&b) == REC_ERR_SDA_STUCK);                /* STOP 의 SCL↓ 에 슬레이브가 비트 5(=0)를 냄 */
    CHECK(bus_state(&b) == BUS_SDA_LOW && s1.st == S_TX);
    CHECK(i2c_bus_recover(&b, &rep) == REC_OK);                   /* 제대로 된 복구로 마무리 */
    CHECK(verify_bus(&b, &s1, &s2));

    /* 쓰기 데이터 ACK 중 리셋 → 1 펄스, 끝난 바이트만 저장 (복구 클럭이 바이트를 만들지 않음) */
    {
        uint32_t kk = 0; bool found = false;
        for (kk = 0; kk < 200 && !found; kk++) {
            Bus sv = b; Slave ss = s1;
            load_slave(&s1, d12); arm_reset(&b, kk); run_tx(&b, TX_WRITE, d12); master_reboot(&b, 3);
            found = s1.st == S_RX_ACK && s1.rx_len == 2 && bus_state(&b) == BUS_SDA_LOW;
            if (!found) { b = sv; s1 = ss; }
        }
        CHECK(found);
        CHECK(predict_pulses(&s1, &b) == 1);
        CHECK(i2c_bus_recover(&b, &rep) == REC_OK && rep.pulses == 1);
        CHECK(s1.rx_len == 2 && s1.rx[0] == 0x12 && s1.rx[1] == 0x34 && s1.st == S_IDLE);
        CHECK(verify_bus(&b, &s1, &s2));
    }

    /* 이미 idle 인 버스: 아무 클럭도 내지 않는다 */
    f0 = b.scl_falls; mv0 = b.sda_moves_scl_high;
    CHECK(i2c_bus_recover(&b, &rep) == REC_OK && rep.pulses == 0 && b.scl_falls == f0 && b.sda_moves_scl_high == mv0);

    /* 고장: SDA 를 영원히 잡는 노드 → 9 클럭 후 SDA_STUCK */
    b.fault_sda_low = true; bus_idle_tick(&b);
    f0 = b.scl_falls;
    CHECK(i2c_bus_recover(&b, &rep) == REC_ERR_SDA_STUCK);
    CHECK(rep.pulses == 9 && b.scl_falls - f0 == 9 && rep.after == BUS_SDA_LOW);
    b.fault_sda_low = false; bus_idle_tick(&b);
    CHECK(verify_bus(&b, &s1, &s2));

    /* 고장: SCL 이 LOW 에 붙음 → 클럭을 낼 수 없다 → SCL_STUCK, 펄스 0 */
    b.fault_scl_from = b.ticks; b.fault_scl_until = UINT32_MAX; bus_idle_tick(&b);
    f0 = b.scl_falls;
    CHECK(i2c_bus_recover(&b, &rep) == REC_ERR_SCL_STUCK);
    CHECK(rep.pulses == 0 && b.scl_falls == f0 && rep.before == BUS_SCL_LOW && rep.scl_wait_ticks == SCL_WAIT_MAX);
    b.fault_scl_until = 0; bus_idle_tick(&b);
    CHECK(verify_bus(&b, &s1, &s2));

    /* SCL 이 잠깐(5틱) 붙잡힘(스트레칭) → 읽어 보고 기다리므로 그대로 복구 */
    k = find_reset_in_tx(&b, &s1, d12, 1);
    load_slave(&s1, d12); arm_reset(&b, k); run_tx(&b, TX_READ, d12); master_reboot(&b, 3);
    b.fault_scl_from = b.ticks + 3; b.fault_scl_until = b.fault_scl_from + 5;   /* 첫 펄스의 SCL 놓기를 5틱 붙잡음 */
    CHECK(i2c_bus_recover(&b, &rep) == REC_OK && rep.pulses == 3 && rep.scl_wait_ticks > 0);
    CHECK(verify_bus(&b, &s1, &s2));

    /* 복구 도중(2번째 펄스부터) SCL 이 영원히 붙잡힘 → SCL_STUCK, pulses = 2 */
    load_slave(&s1, d12); arm_reset(&b, k); run_tx(&b, TX_READ, d12); master_reboot(&b, 3);
    b.fault_scl_from = b.ticks + 2 + 4 + 1; b.fault_scl_until = UINT32_MAX;
    CHECK(i2c_bus_recover(&b, &rep) == REC_ERR_SCL_STUCK && rep.pulses == 2);
    b.fault_scl_until = 0; bus_idle_tick(&b);
    CHECK(i2c_bus_recover(&b, &rep) == REC_OK);                    /* 풀리면 다시 복구 */
    CHECK(verify_bus(&b, &s1, &s2));

    /* ===== Part B: 전수 — 모든 마스터 동작 위치 k × 256 데이터 패턴 × 쓰기/읽기/레지스터 읽기 ===== */
    bus_init(&b); bus_add_slave(&b, &s1, 0x50); bus_add_slave(&b, &s2, 0x68);
    long hist[TX_KINDS][REC_MAX_PULSES + 1];
    memset(hist, 0, sizeof hist);
    long nscen = 0, nhung = 0, bad_pred = 0, bad_rc = 0, bad_inv = 0, bad_verify = 0;
    int maxp = 0;
    for (int kind = 0; kind < TX_KINDS; kind++) {
        for (int v = 0; v < 256; v++) {
            uint8_t d[3] = {(uint8_t)v, (uint8_t)~v, (uint8_t)xorshift32()};
            for (uint32_t kk = 0;; kk++) {
                Scen r = scenario(&b, &s1, &s2, (TxKind)kind, d, kk);
                nscen++;
                if (r.pred < 0 || r.pred > REC_MAX_PULSES || r.pred2 != 0 || r.pulses != r.pred) bad_pred++;
                if (r.rc != REC_OK || !r.idle_after) bad_rc++;
                if (!r.one_stop || !r.s2_quiet || !r.prefix_ok) bad_inv++;
                if (!r.verify_ok) bad_verify++;
                if (r.pulses >= 0 && r.pulses <= REC_MAX_PULSES) hist[kind][r.pulses]++;
                if (r.pulses > 0) nhung++;
                if (r.pulses > maxp) maxp = r.pulses;
                if (!r.fired) break;                     /* 리셋 전에 트랜잭션이 끝남 = 모든 위치를 돌았다 */
            }
        }
    }
    printf("exhaustive: %ld scenarios, %ld hung (SDA low after reboot), max pulses %d\n", nscen, nhung, maxp);
    for (int kind = 0; kind < TX_KINDS; kind++) {
        printf("  %-17s pulses 0..9:", KIND_NAME[kind]);
        for (int p = 0; p <= REC_MAX_PULSES; p++) printf(" %ld", hist[kind][p]);
        putchar('\n');
    }
    printf("  bad_pred=%ld bad_rc=%ld bad_invariant=%ld bad_verify=%ld\n", bad_pred, bad_rc, bad_inv, bad_verify);
    CHECK(nscen > 50000 && nhung > 0);
    CHECK(bad_pred == 0);
    CHECK(bad_rc == 0);
    CHECK(bad_inv == 0);
    CHECK(bad_verify == 0);
    CHECK(maxp == 9 && hist[TX_READ][9] > 0 && hist[TX_WRITE][9] == 0);
    CHECK(hist[TX_WRITE][1] > 0);

    /* ===== Part C: 드라이버 경로 (새 버스: 로그 시각이 작게 보이도록) ===== */
    bus_init(&b); bus_add_slave(&b, &s1, 0x50); bus_add_slave(&b, &s2, 0x68);
    static char logbuf[2048];
    I2cDev dev; memset(&dev, 0, sizeof dev);
    dev.b = &b; dev.log = logbuf; dev.log_cap = sizeof logbuf;
    uint8_t rb[3] = {0};
    I2cOp rd = {0x50, true, rb, 3};

    /* C1. 워치독 리셋 → 부트 → 첫 센서 읽기: busy 감지 → 복구 → 읽기 성공 */
    k = find_reset_in_addr_ack(&b, &s1, d00);
    load_slave(&s1, d00);
    const uint8_t seq[4] = {0x00, 0x12, 0x34, 0x56};             /* 센서 FIFO: 0x00 다음에 12 34 56 */
    memcpy(s1.tx, seq, 4); s1.tx_len = 4;
    arm_reset(&b, k); run_tx(&b, TX_READ, d00); master_reboot(&b, 3);
    dlog(&dev, "boot after watchdog reset");
    CHECK(i2c_xfer_robust(&dev, &rd) == I2C_OK && memcmp(rb, d12, 3) == 0);
    CHECK(dev.n_recover == 1 && dev.last_pulses == 9 && dev.n_recover_fail == 0);
    CHECK(strstr(logbuf, "recovered after 9 pulse(s)") != NULL);

    /* C2. ACK 슬롯 글리치: 슬레이브가 마지막 NACK 을 ACK 로 봄 → 다음 바이트 MSB(0) 로 SDA 잡음 → STOP 실패
     *     → 복구 → 재시도 성공 */
    const uint8_t d3[3] = {0x11, 0x22, 0x05};                     /* 세 번째 바이트 0x05: MSB=0 */
    load_slave(&s1, d3);
    s1.glitch_ack_once = true;
    I2cOp rd2 = {0x50, true, rb, 2};
    unsigned nr0 = dev.n_recover;
    int rr = i2c_xfer_robust(&dev, &rd2);
    CHECK(rr == I2C_OK && dev.n_retry == 1 && dev.n_recover == nr0 + 1);
    CHECK(bus_state(&b) == BUS_IDLE && strstr(logbuf, "STOP failed on read 0x50") != NULL);

    /* C3. 죽은 디바이스(SDA 영구 LOW) + 전원 스위치 훅 → 전원 재인가 후 성공 */
    b.fault_sda_low = true; bus_idle_tick(&b);
    dev.power_cycle = power_cycle_hook; dev.pc_ctx = &s1;
    load_slave(&s1, d12);
    CHECK(i2c_xfer_robust(&dev, &rd) == I2C_OK && memcmp(rb, d12, 3) == 0);
    CHECK(dev.n_power_cycle == 1 && dev.n_recover_fail == 0);

    /* C4. 같은 고장, 훅 없음 → BUS_STUCK, 트랜잭션은 시도조차 안 함 */
    dev.power_cycle = NULL;
    b.fault_sda_low = true; bus_idle_tick(&b);
    uint32_t am = s1.addr_matches;
    CHECK(i2c_xfer_robust(&dev, &rd) == I2C_ERR_BUS_STUCK && dev.n_recover_fail == 1 && s1.addr_matches == am);
    b.fault_sda_low = false; bus_idle_tick(&b);

    /* C5. SCL 고착 → BUS_STUCK, 로그에 SCL 원인 */
    b.fault_scl_from = b.ticks; b.fault_scl_until = UINT32_MAX; bus_idle_tick(&b);
    CHECK(i2c_xfer_robust(&dev, &rd) == I2C_ERR_BUS_STUCK && strstr(logbuf, "SCL stuck low") != NULL);
    b.fault_scl_until = 0; bus_idle_tick(&b);
    CHECK(i2c_xfer_robust(&dev, &rd) == I2C_OK);
    printf("--- bring-up log ---\n%s--- end log (%zu bytes) ---\n", logbuf, dev.log_len);
    CHECK(!dev.log_trunc && dev.log_len == strlen(logbuf));

    /* C6. 로그 용량: 작은 버퍼면 자르고 표시, 넘치지 않음 */
    char small[40];
    I2cDev d2; memset(&d2, 0, sizeof d2);
    d2.b = &b; d2.log = small; d2.log_cap = sizeof small;
    dlog(&d2, "first line");
    dlog(&d2, "second line that does not fit");
    CHECK(d2.log_trunc && d2.log_len < sizeof small && strlen(small) == d2.log_len);

    /* C7. 랜덤 300 라운드: 랜덤 동작(읽기/쓰기, 0x50/0x68), 40% 확률로 랜덤 위치 리셋 → 부트 → robust 동작 */
    I2cDev d3v; static char lb3[64];
    memset(&d3v, 0, sizeof d3v); d3v.b = &b; d3v.log = lb3; d3v.log_cap = sizeof lb3;
    int bad = 0, nres = 0;
    for (int it = 0; it < 300; it++) {
        uint32_t r = xorshift32();
        uint8_t d[3] = {(uint8_t)xorshift32(), (uint8_t)xorshift32(), (uint8_t)xorshift32()};
        if (r % 10 < 4) {                                    /* 이전 동작이 리셋으로 끊겼다 */
            load_slave(&s1, d); arm_reset(&b, xorshift32() % 120);
            run_tx(&b, (TxKind)((r >> 8) % TX_KINDS), d); master_reboot(&b, 3);
            nres++;
        }
        if (dev_ensure_idle(&d3v) != I2C_OK) bad++;           /* 부트 시 init: 버스 점검·복구 */
        bool to68 = (r >> 12) & 1, isrd = (r >> 13) & 1;
        Slave *sl = to68 ? &s2 : &s1;
        uint8_t buf[3];
        load_slave(sl, d);
        I2cOp op = {to68 ? 0x68 : 0x50, isrd, isrd ? buf : d, 3};
        int e = i2c_xfer_robust(&d3v, &op);
        if (e != I2C_OK || bus_state(&b) != BUS_IDLE || d3v.last_pulses > 9) bad++;
        else if (isrd ? memcmp(buf, d, 3) != 0 : (sl->rx_len != 3 || memcmp(sl->rx, d, 3) != 0)) bad++;
    }
    printf("random robust: 300 ops, %d preceded by reset, %u recoveries, bad=%d\n", nres, d3v.n_recover, bad);
    CHECK(bad == 0 && d3v.n_recover > 0 && d3v.n_recover_fail == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
