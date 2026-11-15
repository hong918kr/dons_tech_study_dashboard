/* COMM-M01 I2C 클럭 스트레칭 지원 마스터
 *
 * 모델 (COMM-B06 모델 축약판 + 시간): SDA/SCL = 모든 노드 드라이버의 AND (1 = 놓음 → 풀업이 1, 0 = 당김).
 *   1 틱 = 1 µs (이 모델의 정의). 마스터는 SCL 을 반주기 단위로 만든다: low t_low 틱, high t_high 틱
 *   (기본 5/5 = 100 kHz). 매 틱: 마스터 변경 반영 → 슬레이브들이 같은 스냅샷을 보고 반응 → 다시 AND → 기록.
 *   슬레이브는 9번째 클럭(ACK)이 끝나는 SCL↓ 에서 SCL 을 N 틱 붙잡을 수 있다 = 클럭 스트레칭:
 *     쓰기: 주소 ACK 뒤 / 데이터 바이트 ACK 뒤,  읽기: 보낼 바이트의 첫 비트(MSB) 앞 (데이터는 스트레칭 끝에 준비됨).
 * Part A: 스트레칭 슬레이브 + "SCL 을 놓고 다시 읽어 진짜 high 가 된 뒤 high 반주기를 센다" 마스터
 * Part B: 최대 스트레치 타임아웃 → I2C_ERR_TIMEOUT + 두 선을 놓기 / "high 라고 가정하는" naive 마스터의 고장 재현
 * Part C: i2c_write_read() — NACK 주소 / NACK 데이터 / TIMEOUT 구분 + 브링업용 타이밍 로그
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
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
enum { STR_ADDR_W = 1, STR_RX = 2, STR_TX = 4 };    /* 스트레칭 위치 비트마스크 */

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

/* 스트레칭 시작 (9번째 클럭이 끝나는 SCL↓ 틱에 불림). kind 가 stretch_where 에 없으면 false.
 * 이번 길이 N = stretch_seq[nstretch] (없으면 stretch_n), nstretch++. N == 0 이면 false.
 * N > 0 이면 hold = N, 자기 SCL 드라이버를 당기고 true. N == 1 이고 pending_load 면 지금 MSB 를 낸다. */
static bool slave_stretch_start(Slave *s, Bus *b, uint8_t kind) {
    (void)s; (void)b; (void)kind;
    return false; /* TODO A1: kind 확인 → N 고르기(seq/stretch_n), nstretch++ → N > 0 이면 hold = N, SCL 당기기 */
}

/* 매 틱 맨 앞(bus_tick 이 AND 계산 전에)에 불림. 붙잡는 중이면 hold--: 1 이 되면 미뤄 둔 MSB 를 내고(데이터 준비 완료),
 * 0 이 되면 SCL 을 놓는다. STRETCH_FOREVER 는 줄지 않는다. */
static void slave_stretch_tick(Slave *s, Bus *b) {
    (void)s; (void)b; /* TODO A1: hold-- → 1 이 되면 미뤄 둔 slave_load, 0 이 되면 SCL 놓기 (FOREVER 는 그대로) */
}

/* COMM-B06 슬레이브 상태 머신 + 스트레칭 훅 (주어짐) */
static void slave_on_tick(Slave *s, Bus *b) {
    uint8_t scl = b->scl, sda = b->sda;          /* 이번 틱 시작의 스냅샷 */
    uint8_t *out = &b->sda_drv[s->node];
    if (s->prev_scl && scl && s->prev_sda != sda) {                /* SCL high 중 SDA 변화 */
        if (!sda) { s->starts++; s->st = S_ADDR; s->nbits = 0; s->sh = 0; s->pending_load = false; }
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
                if (s->rx_len < s->rx_cap) { s->rx[s->rx_len++] = s->sh; *out = 0; }
                s->st = S_RX_ACK;
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
    b->last_wait = 0;
    return I2C_OK; /* TODO A2: 지금은 naive 와 같다. 다시 읽어 low 면 1 틱씩 기다리기, max_stretch 넘으면 TIMEOUT, 통계 */
}

/* naive 마스터 (주어짐): SCL 을 놓기만 하고 high 라고 가정한다 */
static int scl_up_naive(Bus *b) { set_scl(b, 1); b->last_wait = 0; return I2C_OK; }

/* 비트 쓰기 (SCL low 에서 시작): SDA = bit → low 반주기 채우기(t_low-2) → scl_up → high 반주기(t_high-1) → SCL↓.
 * low 펄스 = SCL↓ 틱 + SDA 틱 + (t_low-2) = t_low, high 펄스 = 확인된 high 부터 t_high. */
static int m_write_bit(Bus *b, int bit) {
    (void)b; (void)bit;
    return I2C_OK; /* TODO A3: SDA = bit → idle(t_low-2) → scl_up (에러 전파) → idle(t_high-1) → SCL 0 */
}

/* 비트 읽기: SDA 놓기 → low 반주기 → scl_up → (high 확인 직후) SDA 읽기 → high 반주기 → SCL↓ */
static int m_read_bit(Bus *b, int *bit) {
    (void)b;
    *bit = 1;
    return I2C_OK; /* TODO A3: SDA 놓기 → idle(t_low-2) → scl_up → SDA 읽기 → idle(t_high-1) → SCL 0 */
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
    (void)b;
    return I2C_OK; /* TODO A3: SDA 0 → idle(t_low-2) → scl_up (스트레칭 기다림) → idle(t_high-1) → SDA 1 */
}

/* 타임아웃 뒤: 마스터는 SDA·SCL 드라이버를 모두 놓는다(= 1). 클럭을 더 내지 않는다. 1 틱. */
static void m_abort(Bus *b) {
    (void)b; /* TODO B: 마스터의 SDA·SCL 드라이버를 놓고 1 틱 */
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
    (void)log; (void)e; /* TODO C1: NULL 이면 무시, 자리 있으면 추가, 없으면 overflow = true */
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
    (void)b; (void)addr7; (void)wr; (void)nw; (void)rd; (void)nr; (void)log; (void)log_ev;
    return I2C_ERR_ARG; /* TODO C2: 인자 → idle 검사 → S addr+W wr.. → (Sr) addr+R rd.. → P. NACK 은 STOP, TIMEOUT 은 abort, 단계마다 log_ev */
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
    s->st = S_IDLE; s->hold = 0; s->pending_load = false;
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
/* '-' = 1, '_' = 0 */
static void trace_print(const Bus *b, const char *title) {
    printf("%s (%u ticks)\n  SCL ", title, (unsigned)b->tr_len);
    for (uint32_t i = 0; i < b->tr_len; i++) putchar(b->tr_scl[i] == '1' ? '-' : '_');
    printf("\n  SDA ");
    for (uint32_t i = 0; i < b->tr_len; i++) putchar(b->tr_sda[i] == '1' ? '-' : '_');
    putchar('\n');
}
static const char *EV_NAME[] = { "START", "Sr", "ADDR", "TX", "RX", "STOP", "TIMEOUT" };
static void log_print(const I2cLog *log) {
    printf("  %7s %6s  %-8s %-5s %-5s %s\n", "t(us)", "dur", "event", "byte", "ack", "stretch(us)");
    for (size_t i = 0; i < log->n; i++) {
        const I2cEvent *e = &log->ev[i];
        printf("  %7u %6u  %-8s ", (unsigned)e->t0, (unsigned)(e->t1 - e->t0), EV_NAME[e->kind]);
        if (e->kind == EV_ADDR || e->kind == EV_TX || e->kind == EV_RX)
            printf("0x%02X  %-5s", e->byte, e->ack ? "ACK" : "NACK");
        else if (e->kind == EV_TIMEOUT) printf("in %-8s", EV_NAME[e->phase]);
        else printf("%-11s", "");
        printf(" %u\n", (unsigned)e->stretch);
    }
    if (log->overflow) printf("  (log overflow)\n");
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

/* t_low = 3, t_high = 2, 슬레이브 0x48 이 주소 ACK 뒤 N = 7 틱 스트레칭, 0xA5 쓰기 — 정답 마스터의 틱 단위 파형.
 * 조각: idle | START (3) | 주소 0x90 + ACK (9 비트 × 5) | 데이터 0xA5 + ACK (45 + 기다림 4) | STOP (5) */
static const char *WAVE_SCL =
    "1" "110" "001100011000110001100011000110001100011000110" "0000001100011000110001100011000110001100011000110" "00111";
static const char *WAVE_SDA =
    "1" "000" "111110000000000111110000000000000000000000001" "1111111110000011111000000000011111000001111000001" "00001";

int main(void) {
    Bus b; Slave s;
    I2cEvent evbuf[32];
    I2cLog log;
    uint8_t rd[8] = {0};
    const uint8_t a5 = 0xA5;
    int r, bad;

    /* ===== Part A: 스트레칭 없음 — 펄스 폭과 트랜잭션 길이 ===== */
    bus_init(&b, 5, 5, 100);
    bus_add_slave(&b, &s, 0x48, 16);
    uint32_t t0 = b.ticks;
    r = h_write(&b, 0x48, &a5, 1, NULL);
    CHECK(r == I2C_OK && s.rx_len == 1 && s.rx[0] == 0xA5);
    CHECK(b.min_high == 5 && b.min_low == 5 && b.max_low == 5);       /* 100 kHz: 5 µs / 5 µs */
    CHECK(b.ticks - t0 == 6 + 2 * 90 + 10);                            /* START 6 + 바이트 2 × 9 × 10 + STOP 10 */
    CHECK(b.stretch_events == 0 && b.sda == 1 && b.scl == 1);

    /* ===== Part A: 작은 파형 — 주소 ACK 뒤 N = 7 스트레칭, 마스터 low 반주기 3 → 4 틱 기다림 ===== */
    bus_init(&b, 3, 2, 100);
    bus_add_slave(&b, &s, 0x48, 16);
    s.stretch_where = STR_ADDR_W; s.stretch_n = 7;
    r = h_write(&b, 0x48, &a5, 1, NULL);
    CHECK(r == I2C_OK && s.rx_len == 1 && s.rx[0] == 0xA5);
    CHECK(b.stretch_events == 1 && b.stretch_ticks == 4 && b.max_low == 7);
    CHECK(b.min_high == 2 && b.min_low == 3);                          /* high 반주기는 줄지 않는다 */
    CHECK(strcmp(b.tr_scl, WAVE_SCL) == 0);
    CHECK(strcmp(b.tr_sda, WAVE_SDA) == 0);
    trace_print(&b, "aware master, addr-ACK stretch N=7 (t_low=3, t_high=2), write 0xA5");

    /* ===== Part A: N = 0..60 전수 — 쓰기 2바이트(주소·데이터 ACK 뒤) / 읽기 3바이트(각 바이트 앞) ===== */
    bad = 0;
    for (uint32_t n = 0; n <= 60; n++) {
        const uint8_t w2[2] = { (uint8_t)(0x3C + n), (uint8_t)~n };
        bus_init(&b, 5, 5, 100);
        bus_add_slave(&b, &s, 0x48, 16);
        s.stretch_where = STR_ADDR_W | STR_RX; s.stretch_n = n;
        r = h_write(&b, 0x48, w2, 2, NULL);
        uint32_t w = pos_wait(n, 5);
        if (r != I2C_OK || s.rx_len != 2 || memcmp(s.rx, w2, 2) != 0) bad++;
        if (b.stretch_ticks != 3 * w || b.stretch_events != (w ? 3u : 0u) || s.nstretch != 3) bad++;
        if (b.min_high != 5 || b.min_low != 5 || b.max_low != (n > 5 ? n : 5)) bad++;
        const uint8_t t3[3] = { (uint8_t)(n * 7), 0x00, 0xFF };
        bus_init(&b, 5, 5, 100);
        bus_add_slave(&b, &s, 0x48, 16);
        s.stretch_where = STR_TX; s.stretch_n = n;
        slave_set_tx(&s, t3, 3);
        r = h_read(&b, 0x48, rd, 3);
        if (r != I2C_OK || memcmp(rd, t3, 3) != 0 || s.tx_pos != 3) bad++;
        if (b.stretch_ticks != 3 * w || s.nstretch != 3 || b.min_high != 5) bad++;  /* 주소 R ACK 뒤 + 마스터 ACK 2번 */
    }
    CHECK(bad == 0);

    /* ===== Part B: 타임아웃 경계 (max_stretch = 20, t_low = 5) ===== */
    bus_init(&b, 5, 5, 20);
    bus_add_slave(&b, &s, 0x48, 16);
    s.stretch_where = STR_ADDR_W; s.stretch_n = 5 + 20;              /* 정확히 20 틱 기다림 → 통과 */
    CHECK(h_write(&b, 0x48, &a5, 1, NULL) == I2C_OK);
    CHECK(b.stretch_max == 20 && s.rx_len == 1);
    s.stretch_n = 5 + 21; s.rx_len = 0;                               /* 21 틱 필요 → 타임아웃 */
    CHECK(h_write(&b, 0x48, &a5, 1, NULL) == I2C_ERR_TIMEOUT);
    CHECK(b.last_wait == 20);                                          /* 기다린 건 정확히 max_stretch */
    CHECK(b.sda_drv[0] == 1 && b.scl_drv[0] == 1);                     /* 마스터는 두 선을 놓았다 */
    CHECK(bus_run_until_idle(&b, 100));                                /* 슬레이브가 놓으면 idle (쓰기 중이었음) */
    s.stretch_n = 5 + 60; s.rx_len = 0;                               /* 한참 더 붙잡는 슬레이브 */
    CHECK(h_write(&b, 0x48, &a5, 1, NULL) == I2C_ERR_TIMEOUT);
    CHECK(b.scl == 0 && s.hold > 0);                                   /* 슬레이브는 아직 붙잡는 중 */
    CHECK(!bus_run_until_idle(&b, 20));                                /* 20 틱 더 지나도 아직 busy */
    CHECK(bus_run_until_idle(&b, 100));                                /* 쓰기 중이었으면 SDA 도 1 → idle */
    s.stretch_n = 0;
    CHECK(h_write(&b, 0x48, &a5, 1, NULL) == I2C_OK && s.rx_len == 1 && s.rx[0] == 0xA5);
    /* STOP 직전(마지막 데이터 ACK 뒤) 타임아웃: 마스터는 SDA 를 0 으로 둔 채 기다리던 중 → m_abort 가 SDA 도 놓아야 */
    s.stretch_where = STR_RX; s.stretch_n = 5 + 60; s.rx_len = 0;
    CHECK(h_write(&b, 0x48, &a5, 1, NULL) == I2C_ERR_TIMEOUT && s.rx_len == 1);
    CHECK(b.sda_drv[0] == 1 && b.scl_drv[0] == 1);
    CHECK(bus_run_until_idle(&b, 100));                                /* SDA 를 놓았으니 슬레이브가 놓으면 idle */
    s.stretch_where = STR_ADDR_W;

    /* 영원히 붙잡는 슬레이브 → TIMEOUT, 계속 busy, 슬레이브 리셋 뒤 회복 */
    s.stretch_n = STRETCH_FOREVER; s.rx_len = 0;
    CHECK(h_write(&b, 0x48, &a5, 1, NULL) == I2C_ERR_TIMEOUT);
    CHECK(!bus_run_until_idle(&b, 10000) && b.scl == 0);              /* SCL stuck-low: 9클럭 복구도 불가 */
    slave_reset(&s, &b);
    s.stretch_n = 0;
    CHECK(h_write(&b, 0x48, &a5, 1, NULL) == I2C_OK && s.rx_len == 1);

    /* 읽기 중 타임아웃: 슬레이브가 늦게 놓으면서 다음 바이트 MSB 를 낸다 → MSB 0 이면 SDA stuck-low (COMM-M03) */
    const uint8_t t12[2] = { 0x12, 0x34 }, t92[2] = { 0x92, 0x34 };
    s.stretch_where = STR_TX; s.stretch_n = 5 + 30;
    slave_set_tx(&s, t12, 2);
    CHECK(h_read(&b, 0x48, rd, 2) == I2C_ERR_TIMEOUT);
    CHECK(!bus_run_until_idle(&b, 100) && b.scl == 1 && b.sda == 0);   /* SCL 은 돌아왔지만 SDA = 0 */
    slave_reset(&s, &b);
    slave_set_tx(&s, t92, 2);
    CHECK(h_read(&b, 0x48, rd, 2) == I2C_ERR_TIMEOUT);
    CHECK(bus_run_until_idle(&b, 100));                                /* MSB 1 → 우연히 idle 처럼 보임 */
    CHECK(s.st == S_TX);                                               /* 하지만 슬레이브는 바이트 중간 */
    s.stretch_n = 0; slave_set_tx(&s, t12, 2);
    CHECK(h_read(&b, 0x48, rd, 2) == I2C_OK && rd[0] == 0x12 && rd[1] == 0x34);

    /* SMBus 식 25 ms (= 25000 틱) 타임아웃 */
    bus_init(&b, 5, 5, 25000);
    bus_add_slave(&b, &s, 0x48, 16);
    s.stretch_where = STR_RX; s.stretch_n = 20000;
    CHECK(h_write(&b, 0x48, &a5, 1, NULL) == I2C_OK && b.stretch_max == 20000 - 5);
    s.stretch_n = 30000;
    CHECK(h_write(&b, 0x48, &a5, 1, NULL) == I2C_ERR_TIMEOUT && b.last_wait == 25000);

    /* 바이트마다 다른 스트레칭: N = 0, 3, 6, 40, 1, 17 (쓰기 5바이트 → 주소 + 5 = 6 이벤트) */
    static const uint32_t seq[6] = { 0, 3, 6, 40, 1, 17 };
    const uint8_t w5[5] = { 1, 2, 3, 4, 5 };
    bus_init(&b, 5, 5, 100);
    bus_add_slave(&b, &s, 0x48, 16);
    s.stretch_where = STR_ADDR_W | STR_RX; s.stretch_seq = seq; s.stretch_seq_len = 6;
    CHECK(h_write(&b, 0x48, w5, 5, NULL) == I2C_OK && s.rx_len == 5 && memcmp(s.rx, w5, 5) == 0);
    CHECK(b.stretch_ticks == 1 + 35 + 12 && b.stretch_events == 3 && b.stretch_max == 35);

    /* ===== Part B: naive 마스터 (SCL 을 놓고 high 라고 가정) ===== */
    /* (1) 짧은 스트레칭 N = 7: 데이터는 맞지만 high 펄스가 3 µs 로 줄어든다 (Sm tHIGH ≥ 4.0 µs 위반) */
    bus_init(&b, 5, 5, 100); b.scl_up = scl_up_naive;
    bus_add_slave(&b, &s, 0x48, 16);
    s.stretch_where = STR_ADDR_W; s.stretch_n = 7;
    CHECK(h_write(&b, 0x48, &a5, 1, NULL) == I2C_OK && s.rx_len == 1 && s.rx[0] == 0xA5);
    CHECK(b.min_high == 3);
    /* (2) 긴 스트레칭 N = 25 (> t_low + t_high): 클럭이 삼켜져 데이터가 밀린다 */
    s.stretch_n = 25; s.rx_len = 0;
    bool all_ack = true;
    r = h_write(&b, 0x48, &a5, 1, &all_ack);
    printf("naive write 0xA5, N=25: ret=%d all_ack=%d, slave rx_len=%zu, slave state=%d\n",
           r, (int)all_ack, s.rx_len, (int)s.st);
    CHECK(r == I2C_OK && !all_ack && s.rx_len == 0 && s.st == S_IDLE); /* 클럭 2개 삼켜짐 → 데이터 NACK */
    /* (3) 읽기 N = 25: 데이터가 준비되기 전에 샘플 → 틀린 값 */
    bus_init(&b, 5, 5, 100); b.scl_up = scl_up_naive;
    bus_add_slave(&b, &s, 0x48, 16);
    const uint8_t t5a[2] = { 0x5A, 0xC3 };
    s.stretch_where = STR_TX; s.stretch_n = 25;
    slave_set_tx(&s, t5a, 2);
    memset(rd, 0, sizeof rd);
    r = h_read(&b, 0x48, rd, 2);
    printf("naive read {5A C3}, N=25: ret=%d, got %02X %02X\n", r, rd[0], rd[1]);
    CHECK(r == I2C_OK && rd[0] == 0xD6 && rd[1] == 0x7F);              /* 마스터는 성공이라고 믿는다! */
    /* (4) 마지막 ACK 뒤 스트레칭 + naive STOP: SCL low 중 SDA↑ → STOP 이 아니다 */
    bus_init(&b, 5, 5, 100); b.scl_up = scl_up_naive;
    bus_add_slave(&b, &s, 0x48, 16);
    s.stretch_where = STR_RX; s.stretch_n = 15;
    CHECK(h_write(&b, 0x48, &a5, 1, NULL) == I2C_OK && s.rx_len == 1);
    bus_run_until_idle(&b, 100);
    CHECK(s.stops == 0 && s.st == S_RX);                               /* 슬레이브는 STOP 을 못 봤다 */

    /* ===== Part C: i2c_write_read + 타이밍 로그 ===== */
    bus_init(&b, 5, 5, 100);
    bus_add_slave(&b, &s, 0x48, 1);
    const uint8_t reg = 0x0F, acc[2] = { 0x6A, 0x01 };
    s.stretch_where = STR_TX; s.stretch_n = 50;
    slave_set_tx(&s, acc, 2);
    log = (I2cLog){ evbuf, 32, 0, false };
    r = i2c_write_read(&b, 0x48, &reg, 1, rd, 2, &log);
    CHECK(r == I2C_OK && s.rx[0] == 0x0F && rd[0] == 0x6A && rd[1] == 0x01);
    CHECK(log.n == 8 && log.ev[0].kind == EV_START && log.ev[3].kind == EV_RSTART && log.ev[7].kind == EV_STOP);
    uint32_t sum = 0;
    for (size_t i = 0; i < log.n; i++) sum += log.ev[i].stretch;
    CHECK(sum == b.stretch_ticks && sum == 2 * 45 && log.ev[5].stretch == 45 && log.ev[6].stretch == 45);
    CHECK(log.ev[0].t0 == 0 && log.ev[7].t1 == b.ticks);                /* 로그가 트랜잭션 전체를 덮는다 */
    printf("register read 0x48 reg 0x0F, 2 bytes, slave stretches 50 us before each byte:\n");
    log_print(&log);

    /* NACK 주소 / NACK 데이터 / TIMEOUT / 인자 / 로그 넘침 */
    log = (I2cLog){ evbuf, 32, 0, false };
    CHECK(i2c_write_read(&b, 0x3C, &reg, 1, rd, 2, &log) == I2C_ERR_NACK_ADDR);
    CHECK(log.n == 3 && log.ev[1].kind == EV_ADDR && !log.ev[1].ack && log.ev[2].kind == EV_STOP && b.sda && b.scl);
    const uint8_t w3[3] = { 0x10, 0x20, 0x30 };
    s.rx_len = 0;                                                      /* rx_cap = 1 → 두 번째 바이트 NACK */
    CHECK(i2c_write_read(&b, 0x48, w3, 3, NULL, 0, NULL) == I2C_ERR_NACK_DATA && s.rx_len == 1 && b.sda && b.scl);
    s.stretch_n = 5 + 130; slave_set_tx(&s, acc, 2);           /* 130 틱 기다려야 함, 한도 100 */
    log = (I2cLog){ evbuf, 32, 0, false };
    s.rx_len = 0;
    CHECK(i2c_write_read(&b, 0x48, &reg, 1, rd, 2, &log) == I2C_ERR_TIMEOUT);
    CHECK(log.n == 6 && log.ev[5].kind == EV_TIMEOUT && log.ev[5].phase == EV_RX);
    printf("timeout case (slave stretches 135 us before the first read byte, max_stretch 100 us):\n");
    log_print(&log);
    CHECK(b.sda_drv[0] == 1 && b.scl_drv[0] == 1 && b.scl == 0);       /* 마스터는 놓았고 슬레이브가 아직 붙잡음 */
    uint32_t tb = b.ticks;
    CHECK(i2c_write_read(&b, 0x48, &reg, 1, rd, 2, NULL) == I2C_ERR_BUS_BUSY && b.ticks == tb);  /* 클럭 0개 */
    slave_reset(&s, &b);
    CHECK(i2c_write_read(&b, 0x80, &reg, 1, rd, 1, NULL) == I2C_ERR_ARG);
    CHECK(i2c_write_read(&b, 0x48, NULL, 0, rd, 0, NULL) == I2C_ERR_ARG);
    CHECK(i2c_write_read(&b, 0x48, NULL, 1, rd, 0, NULL) == I2C_ERR_ARG);
    CHECK(i2c_write_read(&b, 0x48, &reg, 1, NULL, 1, NULL) == I2C_ERR_ARG);
    s.stretch_n = 0; slave_set_tx(&s, acc, 2); s.rx_len = 0;
    log = (I2cLog){ evbuf, 3, 0, false };
    CHECK(i2c_write_read(&b, 0x48, &reg, 1, rd, 2, &log) == I2C_OK && log.n == 3 && log.overflow);

    /* 랜덤 300 트랜잭션: 대상 0x48 / 0x3C(없음), nw 0..3, nr 0..3, 스트레칭 위치·길이 랜덤 (일부는 타임아웃) */
    bus_init(&b, 5, 5, 60);
    bus_add_slave(&b, &s, 0x48, 16);
    int n_ok = 0, n_to = 0, n_nak = 0;
    bad = 0;
    for (int it = 0; it < 300; it++) {
        uint32_t x = xorshift32();
        uint8_t tgt = (x & 7) == 0 ? 0x3C : 0x48;
        size_t nw = (x >> 3) & 3, nr = (x >> 5) & 3;
        if (nw == 0 && nr == 0) nr = 1;
        uint8_t wbuf[3], tbuf[3];
        for (int i = 0; i < 3; i++) { wbuf[i] = (uint8_t)xorshift32(); tbuf[i] = (uint8_t)xorshift32(); }
        uint32_t sq[8];
        for (int i = 0; i < 8; i++) {
            uint32_t y = xorshift32();
            sq[i] = (y & 15) == 0 ? 5 + 61 + (y >> 4) % 20 : (y >> 4) % 40;   /* 1/16 은 타임아웃 길이 */
        }
        s.stretch_where = (uint8_t)((x >> 7) & 7);
        s.stretch_seq = sq; s.stretch_seq_len = 8; s.nstretch = 0;
        s.rx_len = 0; slave_set_tx(&s, tbuf, 3);
        /* 기대값: 이 트랜잭션의 스트레칭 이벤트를 순서대로 */
        int want = I2C_OK;
        uint32_t ws = 0, k = 0;
        if (tgt == 0x3C) want = I2C_ERR_NACK_ADDR;
        else {
            uint8_t kinds[8]; uint32_t nk = 0;
            if (nw) { kinds[nk++] = STR_ADDR_W; for (size_t i = 0; i < nw; i++) kinds[nk++] = STR_RX; }
            if (nr) for (size_t i = 0; i < nr; i++) kinds[nk++] = STR_TX;   /* 주소 R ACK 뒤 + 마스터 ACK 뒤 */
            for (uint32_t i = 0; i < nk && want == I2C_OK; i++) {
                if (!(s.stretch_where & kinds[i])) continue;
                uint32_t w = pos_wait(sq[k++], 5);
                if (w > 60) want = I2C_ERR_TIMEOUT; else ws += w;
            }
        }
        uint32_t st0 = b.stretch_ticks;
        r = i2c_write_read(&b, tgt, wbuf, nw, rd, nr, NULL);
        if (r != want) bad++;
        else if (r == I2C_OK) {
            n_ok++;
            if (s.rx_len != nw || memcmp(s.rx, wbuf, nw) != 0 || memcmp(rd, tbuf, nr) != 0) bad++;
            if (b.stretch_ticks - st0 != ws) bad++;
        } else if (r == I2C_ERR_TIMEOUT) { n_to++; slave_reset(&s, &b); }
        else n_nak++;
        if (!(b.sda && b.scl)) bad++;
    }
    printf("random: ok=%d timeout=%d nack_addr=%d, stretch total=%u us, min SCL high=%u us, mismatches=%d\n",
           n_ok, n_to, n_nak, (unsigned)b.stretch_ticks, (unsigned)b.min_high, bad);
    CHECK(bad == 0 && n_to > 0 && n_nak > 0);
    CHECK(b.min_high == 5);                                            /* 어떤 스트레칭에서도 tHIGH 유지 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
