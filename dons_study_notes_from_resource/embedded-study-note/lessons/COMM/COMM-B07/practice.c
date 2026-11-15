/* COMM-B07 추가 연습 — I2C 레지스터 읽기: repeated START와 센서 드라이버
 *
 * 모델: I2C 버스의 "트랜잭션 층" — START / Sr / STOP / 바이트 + 9번째 클럭의 ACK·NACK.
 *       SDA·SCL 비트 수준(wired-AND, 엣지 규칙)은 COMM-B06 에서 했다. 여기서는 그 위의 이벤트만
 *       남긴 축약판이다 (COMM-B06 모델 축약판). 버스의 모든 이벤트는 로직 애널라이저 디코더처럼
 *       bus->log 에 찍힌다:  "S 90 A 0F A Sr 91 A A7 N P"
 *       (90/91 = 7비트 주소 0x48 << 1 | R/W, A = ACK, N = NACK, Sr = repeated START, P = STOP)
 * Part A: mock 온도 센서 슬레이브의 레지스터 파일(포인터 + 자동 증가),
 *         마스터 i2c_write / i2c_read / i2c_write_read(repeated START), raw → m°C 변환
 * Part B: 주소 NACK · 데이터 NACK · 인자 오류, 그리고 STOP+START 와 Sr 비교
 *         (다른 마스터가 끼어듦 / STOP 에서 포인터를 리셋하는 디바이스 / MSB·LSB 가 찢어진 읽기)
 * Part C: 드라이버 — ID 확인 → 설정 + 되읽기 → DRDY 폴링 연속 읽기
 *
 * practice.c: 위 정답 코드(그대로) + 9. 추가 연습 1~4 의 답과 테스트.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
 */
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ================= mock 온도 센서 (이 레슨의 가상 부품, 레지스터 맵은 자체 정의) ================= */
enum {
    REG_TEMP_MSB = 0x00, REG_TEMP_LSB = 0x01,   /* 읽기 전용, int16 2의 보수, 1 LSB = 1/128 °C */
    REG_CONFIG = 0x02,                           /* bit0 EN, bit2:1 AVG, bit7 DRDY(읽기 전용) */
    REG_THIGH_MSB = 0x03, REG_THIGH_LSB = 0x04,  /* 경보 상한 (저장만) */
    REG_TLOW_MSB = 0x05, REG_TLOW_LSB = 0x06,    /* 경보 하한 (저장만) */
    REG_WHO_AM_I = 0x0F,                         /* 읽기 전용, 0xA7 */
    REG_COUNT = 0x10                             /* 포인터 범위 0x00..0x0F, 자동 증가는 0x0F → 0x00 */
};
#define CFG_EN       0x01u  /* 1 = 연속 변환 */
#define CFG_AVG_MASK 0x06u  /* 평균 횟수 코드 (저장만, 동작 없음) */
#define CFG_DRDY     0x80u  /* 새 변환 완료. TEMP_MSB 를 읽으면 0 */
#define CFG_RW_MASK  (CFG_EN | CFG_AVG_MASK)
#define TMP_ID       0xA7u
#define TMP_ADDR     0x48u
#define HELPER       static __attribute__((unused))   /* TODO 에서 쓸 도우미: starter 에서 아직 안 써도 경고 없게 */

typedef enum { DS_IDLE, DS_ADDR, DS_PTR, DS_WDATA, DS_RDATA } DevState;

typedef struct {
    uint8_t addr7;
    uint8_t id;              /* WHO_AM_I 값 (다른 부품 흉내를 위해 바꿀 수 있다) */
    int16_t temp;            /* 출력 레지스터 TEMP_MSB:TEMP_LSB */
    uint8_t config;
    uint16_t thigh, tlow;
    uint8_t ptr;             /* 레지스터 포인터 */
    DevState st;
    /* 변환 모델: EN=1 이면 STOP 을 conv_every 번 볼 때마다 script 의 다음 값으로 TEMP 갱신 + DRDY=1.
     * 트랜잭션 도중(START ~ STOP)에는 갱신하지 않는다 (= 읽는 동안 출력 레지스터를 잠그는 섀도). */
    const int16_t *script;
    size_t script_len, script_pos;
    unsigned conv_every, stop_seen;
    /* 변종·고장 옵션 */
    bool ptr_reset_on_stop;  /* STOP 에서 포인터를 0 으로 되돌리는 디바이스 */
    bool cfg_stuck;          /* CONFIG 쓰기가 무시됨 (쓰기 보호 / 다른 부품) */
} TmpSensor;

static void dev_init(TmpSensor *d, uint8_t addr7) {
    memset(d, 0, sizeof *d);
    d->addr7 = addr7;
    d->id = TMP_ID;
    d->thigh = 0x2800;       /* 80 °C = 10240 × 1/128 */
    d->tlow = 0x0A00;        /* 20 °C */
    d->st = DS_IDLE;
}

HELPER uint8_t dev_reg_get(const TmpSensor *d, uint8_t r) {
    switch (r) {
    case REG_TEMP_MSB:  return (uint8_t)((uint16_t)d->temp >> 8);
    case REG_TEMP_LSB:  return (uint8_t)((uint16_t)d->temp & 0xFFu);
    case REG_CONFIG:    return d->config;
    case REG_THIGH_MSB: return (uint8_t)(d->thigh >> 8);
    case REG_THIGH_LSB: return (uint8_t)(d->thigh & 0xFFu);
    case REG_TLOW_MSB:  return (uint8_t)(d->tlow >> 8);
    case REG_TLOW_LSB:  return (uint8_t)(d->tlow & 0xFFu);
    case REG_WHO_AM_I:  return d->id;
    default:            return 0x00;   /* 예약 레지스터 */
    }
}

HELPER void dev_reg_put(TmpSensor *d, uint8_t r, uint8_t v) {
    switch (r) {
    case REG_CONFIG:
        if (!d->cfg_stuck) d->config = (uint8_t)((d->config & ~CFG_RW_MASK) | (v & CFG_RW_MASK));
        break;
    case REG_THIGH_MSB: d->thigh = (uint16_t)((d->thigh & 0x00FFu) | (unsigned)v << 8); break;
    case REG_THIGH_LSB: d->thigh = (uint16_t)((d->thigh & 0xFF00u) | v); break;
    case REG_TLOW_MSB:  d->tlow = (uint16_t)((d->tlow & 0x00FFu) | (unsigned)v << 8); break;
    case REG_TLOW_LSB:  d->tlow = (uint16_t)((d->tlow & 0xFF00u) | v); break;
    default: break;                    /* 읽기 전용·예약: ACK 는 하되 무시 */
    }
}

/* ---- Part A TODO 1: 슬레이브가 받은 데이터 바이트 (SLA+W 다음).  반환 = ACK 하면 true ----
 * st == DS_PTR  : 첫 바이트 = 레지스터 포인터. 0x10 이상이면 NACK. 받으면 DS_WDATA 로.
 * st == DS_WDATA: ptr 위치에 쓰기(dev_reg_put) 후 ptr 자동 증가 (0x0F 다음은 0x00). */
static bool dev_on_write(TmpSensor *d, uint8_t b) {
    if (d->st == DS_PTR) {
        if (b >= REG_COUNT) return false;
        d->ptr = b;
        d->st = DS_WDATA;
        return true;
    }
    dev_reg_put(d, d->ptr, b);
    d->ptr = (uint8_t)((d->ptr + 1u) & 0x0Fu);
    return true;
}

/* ---- Part A TODO 2: 슬레이브가 내보낼 바이트 (SLA+R 다음, 마스터의 클럭마다 1바이트) ----
 * ptr 위치 값을 돌려주고 ptr 자동 증가. TEMP_MSB 를 내보내면 CONFIG 의 DRDY 를 지운다. */
static uint8_t dev_on_read(TmpSensor *d) {
    uint8_t v = dev_reg_get(d, d->ptr);
    if (d->ptr == REG_TEMP_MSB) d->config = (uint8_t)(d->config & ~CFG_DRDY);
    d->ptr = (uint8_t)((d->ptr + 1u) & 0x0Fu);
    return v;
}

static void dev_start(TmpSensor *d) { d->st = DS_ADDR; }   /* START 와 Sr 둘 다: 주소부터 다시 */

static bool dev_addr(TmpSensor *d, uint8_t sla) {
    if (d->st != DS_ADDR) return false;
    if ((sla >> 1) != d->addr7) { d->st = DS_IDLE; return false; }
    d->st = (sla & 1u) ? DS_RDATA : DS_PTR;
    return true;
}

static void dev_stop(TmpSensor *d) {
    d->st = DS_IDLE;
    if (d->ptr_reset_on_stop) d->ptr = 0;
    if ((d->config & CFG_EN) && d->conv_every && ++d->stop_seen >= d->conv_every) {
        d->stop_seen = 0;
        if (d->script_pos < d->script_len) d->temp = d->script[d->script_pos++];
        d->config |= CFG_DRDY;
    }
}

/* ================= 버스 (트랜잭션 층, COMM-B06 모델 축약판) ================= */
#define LOG_CAP 1024
#define MAX_DEV 3

typedef struct I2cBus I2cBus;
typedef void (*BusFreeHook)(I2cBus *b, void *ctx);

struct I2cBus {
    TmpSensor *dev[MAX_DEV];
    size_t ndev;
    TmpSensor *sel;          /* 이번 주소 단계에서 ACK 한 디바이스 */
    bool busy;               /* START ~ STOP = 버스 점유. 다른 마스터는 STOP 을 봐야 시작할 수 있다 */
    bool addr_phase;         /* START/Sr 직후 첫 바이트 = 주소 */
    const char *master;      /* START 를 낸 마스터 이름 ("" = 우리 MCU) */
    unsigned n_start, n_rstart, n_stop;
    BusFreeHook on_free;     /* STOP 직후 1회 호출: 버스가 비기를 기다리던 다른 마스터 */
    void *on_free_ctx;
    char log[LOG_CAP];
    size_t loglen;
};

static void bus_init(I2cBus *b) { memset(b, 0, sizeof *b); b->master = ""; }
static void bus_attach(I2cBus *b, TmpSensor *d) { if (b->ndev < MAX_DEV) b->dev[b->ndev++] = d; }
static void bus_log_clear(I2cBus *b) { b->loglen = 0; b->log[0] = '\0'; }

static void blog(I2cBus *b, const char *tok) {
    if (b->loglen + 1 >= LOG_CAP) return;
    int k = snprintf(b->log + b->loglen, LOG_CAP - b->loglen, "%s%s", b->loglen ? " " : "", tok);
    if (k < 0) return;
    b->loglen = (size_t)k < LOG_CAP - b->loglen ? b->loglen + (size_t)k : LOG_CAP - 1;
}

/* START. 버스를 이미 잡고 있으면 repeated START(Sr) — STOP 없이 방향·주소를 바꾼다 */
static void bus_start(I2cBus *b) {
    char tok[16];
    bool rep = b->busy;
    if (rep) { b->n_rstart++; snprintf(tok, sizeof tok, "Sr"); }
    else if (b->master[0]) { b->n_start++; snprintf(tok, sizeof tok, "S(%s)", b->master); }
    else { b->n_start++; snprintf(tok, sizeof tok, "S"); }
    blog(b, tok);
    b->busy = true;
    b->addr_phase = true;
    b->sel = NULL;
    for (size_t i = 0; i < b->ndev; i++) dev_start(b->dev[i]);
}

static void bus_stop(I2cBus *b) {
    blog(b, "P");
    b->n_stop++;
    b->busy = false;
    b->addr_phase = false;
    b->sel = NULL;
    for (size_t i = 0; i < b->ndev; i++) dev_stop(b->dev[i]);
    if (b->on_free) {                       /* 버스가 비었다 → 기다리던 다른 마스터가 잡는다 */
        BusFreeHook h = b->on_free;
        b->on_free = NULL;                  /* 1회만 (자기 STOP 에서 다시 불리지 않게) */
        h(b, b->on_free_ctx);
    }
}

/* 마스터가 1바이트를 쓰고 9번째 클럭에서 ACK 를 읽는다. 반환 = ACK? */
static bool bus_tx(I2cBus *b, uint8_t byte) {
    bool ack = false;
    char tok[8];
    if (b->addr_phase) {
        b->addr_phase = false;
        for (size_t i = 0; i < b->ndev; i++)
            if (dev_addr(b->dev[i], byte)) { ack = true; b->sel = b->dev[i]; }   /* wired-AND: 누구든 당기면 0 */
    } else if (b->sel && (b->sel->st == DS_PTR || b->sel->st == DS_WDATA)) {
        ack = dev_on_write(b->sel, byte);
        if (!ack) { b->sel->st = DS_IDLE; b->sel = NULL; }   /* NACK 한 슬레이브는 다음 START/STOP 까지 쉰다 */
    }
    snprintf(tok, sizeof tok, "%02X", byte);
    blog(b, tok);
    blog(b, ack ? "A" : "N");
    return ack;
}

/* 마스터가 1바이트를 읽고 9번째 클럭에 ACK(더 읽음) 또는 NACK(마지막) 를 보낸다 */
static uint8_t bus_rx(I2cBus *b, bool ack) {
    uint8_t v = 0xFF;                       /* 아무도 SDA 를 당기지 않으면 풀업 → 1 */
    char tok[8];
    if (!b->addr_phase && b->sel && b->sel->st == DS_RDATA) {
        v = dev_on_read(b->sel);
        if (!ack) { b->sel->st = DS_IDLE; b->sel = NULL; }  /* NACK = "그만" → 슬레이브는 SDA 를 놓는다 */
    }
    snprintf(tok, sizeof tok, "%02X", v);
    blog(b, tok);
    blog(b, ack ? "A" : "N");
    return v;
}

/* ================= 마스터 API ================= */
enum { I2C_OK = 0, I2C_ENACK_ADDR = -1, I2C_ENACK_DATA = -2, I2C_EARG = -3 };

static uint8_t sla_w(uint8_t a7) { return (uint8_t)(a7 << 1); }
static uint8_t sla_r(uint8_t a7) { return (uint8_t)(a7 << 1 | 1u); }

/* ---- Part A TODO 3: S SLA+W w[0..wn-1] P.  wn = 0 이면 주소만 보내는 probe ----
 * 주소 NACK → STOP 후 I2C_ENACK_ADDR, 데이터 NACK → STOP 후 I2C_ENACK_DATA.
 * 인자 오류(a7 > 0x7F, wn > 0 인데 w == NULL) → 버스를 건드리지 않고 I2C_EARG. */
static int i2c_write(I2cBus *b, uint8_t a7, const uint8_t *w, size_t wn) {
    if (a7 > 0x7Fu || (wn && !w)) return I2C_EARG;
    bus_start(b);
    if (!bus_tx(b, sla_w(a7))) { bus_stop(b); return I2C_ENACK_ADDR; }
    for (size_t i = 0; i < wn; i++)
        if (!bus_tx(b, w[i])) { bus_stop(b); return I2C_ENACK_DATA; }
    bus_stop(b);
    return I2C_OK;
}

/* ---- Part A TODO 4: S SLA+R r[0..rn-1] P.  마지막 바이트만 NACK, 나머지는 ACK ----
 * rn == 0 또는 r == NULL 또는 a7 > 0x7F → I2C_EARG. 주소 NACK → STOP 후 I2C_ENACK_ADDR. */
static int i2c_read(I2cBus *b, uint8_t a7, uint8_t *r, size_t rn) {
    if (a7 > 0x7Fu || !r || rn == 0) return I2C_EARG;
    bus_start(b);
    if (!bus_tx(b, sla_r(a7))) { bus_stop(b); return I2C_ENACK_ADDR; }
    for (size_t i = 0; i < rn; i++) r[i] = bus_rx(b, i + 1 < rn);
    bus_stop(b);
    return I2C_OK;
}

/* ---- Part A TODO 5: S SLA+W w.. Sr SLA+R r.. P  (STOP 없이 방향 전환 = repeated START) ----
 * wn ≥ 1, rn ≥ 1, 포인터 NULL 아님, a7 ≤ 0x7F 가 아니면 I2C_EARG.
 * 어느 단계든 NACK 이면 즉시 STOP 하고 에러 (SLA+W/SLA+R NACK → ENACK_ADDR, w NACK → ENACK_DATA). */
static int i2c_write_read(I2cBus *b, uint8_t a7, const uint8_t *w, size_t wn, uint8_t *r, size_t rn) {
    if (a7 > 0x7Fu || !w || wn == 0 || !r || rn == 0) return I2C_EARG;
    bus_start(b);
    if (!bus_tx(b, sla_w(a7))) { bus_stop(b); return I2C_ENACK_ADDR; }
    for (size_t i = 0; i < wn; i++)
        if (!bus_tx(b, w[i])) { bus_stop(b); return I2C_ENACK_DATA; }
    bus_start(b);                                   /* busy 이므로 Sr 로 찍힌다 */
    if (!bus_tx(b, sla_r(a7))) { bus_stop(b); return I2C_ENACK_ADDR; }
    for (size_t i = 0; i < rn; i++) r[i] = bus_rx(b, i + 1 < rn);
    bus_stop(b);
    return I2C_OK;
}

/* 비교용 (주어짐): 같은 일을 STOP + START 로 — 두 트랜잭션 사이에 버스가 빈다 */
static int i2c_write_then_read(I2cBus *b, uint8_t a7, const uint8_t *w, size_t wn, uint8_t *r, size_t rn) {
    int rc = i2c_write(b, a7, w, wn);
    return rc ? rc : i2c_read(b, a7, r, rn);
}

/* ================= 드라이버 ================= */
enum { TMP_OK = 0, TMP_EID = -10, TMP_ECFG = -11, TMP_ETIMEOUT = -12 };
#define TMP_CFG_DEFAULT (CFG_EN | 0x04u)   /* EN + AVG 코드 2 */
#define TMP_MAX_POLL    8u                 /* DRDY 를 이만큼 읽어도 0 이면 포기 */

typedef struct { I2cBus *bus; uint8_t addr7; } TmpDrv;

static int16_t s16_from_be(const uint8_t *p) {
    int32_t v = (int32_t)p[0] << 8 | p[1];
    return (int16_t)(v >= 0x8000 ? v - 0x10000 : v);
}
static int tmp_reg_read(TmpDrv *d, uint8_t reg, uint8_t *buf, size_t n) {
    return i2c_write_read(d->bus, d->addr7, &reg, 1, buf, n);
}
HELPER int tmp_reg_write1(TmpDrv *d, uint8_t reg, uint8_t v) {
    uint8_t w[2] = { reg, v };
    return i2c_write(d->bus, d->addr7, w, 2);
}

/* ---- Part A TODO 6: raw(1 LSB = 1/128 °C) → m°C.  raw × 1000 / 128 = raw × 125 / 16 을
 * 바닥(floor, −∞ 쪽) 나눗셈으로. 예: 1 → 7, −1 → −8, 3200 → 25000. int32 안에서 넘치지 않는다. */
static int32_t tmp_raw_to_mC(int16_t raw) {
    int32_t x = (int32_t)raw * 125;
    return x >= 0 ? x / 16 : -((-x + 15) / 16);
}

/* ---- Part C TODO 7: 초기화. WHO_AM_I 읽기(Sr) → 다르면 TMP_EID (CONFIG 는 건드리지 않음)
 * → CONFIG = TMP_CFG_DEFAULT 쓰기 → 되읽어 쓰기 가능 비트(CFG_RW_MASK)만 비교, 다르면 TMP_ECFG.
 * 버스 에러는 그대로 전달 (I2C_E*). */
static int tmp_init(TmpDrv *d) {
    uint8_t id = 0, cfg = 0;
    int rc = tmp_reg_read(d, REG_WHO_AM_I, &id, 1);
    if (rc) return rc;
    if (id != TMP_ID) return TMP_EID;
    rc = tmp_reg_write1(d, REG_CONFIG, TMP_CFG_DEFAULT);
    if (rc) return rc;
    rc = tmp_reg_read(d, REG_CONFIG, &cfg, 1);
    if (rc) return rc;
    if ((cfg & CFG_RW_MASK) != TMP_CFG_DEFAULT) return TMP_ECFG;
    return TMP_OK;
}

/* ---- Part C TODO 8: TEMP_MSB, TEMP_LSB 를 한 트랜잭션(포인터 0x00 + Sr + 2바이트 burst)으로 읽어 m°C ---- */
static int tmp_read_mC(TmpDrv *d, int32_t *mC) {
    uint8_t t[2];
    int rc = tmp_reg_read(d, REG_TEMP_MSB, t, 2);
    if (rc) return rc;
    *mC = tmp_raw_to_mC(s16_from_be(t));
    return TMP_OK;
}

/* ---- Part C TODO 9: 연속 읽기. 샘플마다 CONFIG 를 읽어 DRDY 가 1 이 될 때까지 폴링
 * (DRDY=0 인 읽기가 TMP_MAX_POLL 번이면 TMP_ETIMEOUT), 그 다음 tmp_read_mC.
 * *got = 성공한 샘플 수 (에러로 끝나도 그때까지의 개수). */
static int tmp_read_samples(TmpDrv *d, int32_t *out, size_t n, size_t *got) {
    *got = 0;
    for (size_t i = 0; i < n; i++) {
        uint8_t cfg = 0;
        unsigned polls = 0;
        for (;;) {
            int rc = tmp_reg_read(d, REG_CONFIG, &cfg, 1);
            if (rc) return rc;
            if (cfg & CFG_DRDY) break;
            if (++polls >= TMP_MAX_POLL) return TMP_ETIMEOUT;
        }
        int rc = tmp_read_mC(d, &out[i]);
        if (rc) return rc;
        *got = i + 1;
    }
    return TMP_OK;
}

/* ================= 테스트 하네스 ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define CHECK_LOG(b, want) do { g_total++; if (strcmp((b)->log, (want)) == 0) { g_pass++; } \
    else printf("FAIL %s:%d  log\n   got : %s\n   want: %s\n", __FILE__, __LINE__, (b)->log, (want)); } while (0)

static uint32_t g_rng = 0xC0FFEEu;
static uint32_t xorshift32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

/* 다른 마스터 M2: 버스가 비면 같은 센서의 reg 를 1바이트 읽는다 (자기 나름 정상인 트랜잭션) */
typedef struct { uint8_t reg, got; } Intruder;
static void intruder_run(I2cBus *b, void *ctx) {
    Intruder *in = ctx;
    const char *save = b->master;
    b->master = "M2";
    bus_start(b);
    if (bus_tx(b, sla_w(TMP_ADDR)) && bus_tx(b, in->reg)) {
        bus_start(b);
        if (bus_tx(b, sla_r(TMP_ADDR))) in->got = bus_rx(b, false);
    }
    bus_stop(b);
    b->master = save;
}

static void setup(I2cBus *b, TmpSensor *d) { bus_init(b); dev_init(d, TMP_ADDR); bus_attach(b, d); }

/* ======================= 추가 연습 (9. 추가 연습의 답) ======================= */
enum { TMP_EARG = -13 };

/* 연습 1: m°C → raw (1/128 °C) 가장 가까운 정수로 반올림 + int16 범위로 자르기, 그리고 경보 한계 쓰기.
 * raw = mC × 16 / 125. 125 가 홀수라 정확히 .5 인 경우는 없다 → 반올림 방향 논쟁 없음. */
static int16_t tmp_mC_to_raw(int32_t mC) {
    int64_t num = (int64_t)mC * 16;
    int64_t q = num >= 0 ? (num + 62) / 125 : -((-num + 62) / 125);
    if (q > INT16_MAX) q = INT16_MAX;
    if (q < INT16_MIN) q = INT16_MIN;
    return (int16_t)q;
}

static int tmp_set_limits(TmpDrv *d, int32_t low_mC, int32_t high_mC) {
    if (low_mC > high_mC) return TMP_EARG;
    uint16_t hi = (uint16_t)tmp_mC_to_raw(high_mC), lo = (uint16_t)tmp_mC_to_raw(low_mC);
    uint8_t w[5] = { REG_THIGH_MSB, (uint8_t)(hi >> 8), (uint8_t)hi, (uint8_t)(lo >> 8), (uint8_t)lo };
    uint8_t rb[4];
    int rc = i2c_write(d->bus, d->addr7, w, 5);            /* 한 트랜잭션, 자동 증가 03..06 */
    if (rc) return rc;
    rc = tmp_reg_read(d, REG_THIGH_MSB, rb, 4);            /* 되읽어 확인 */
    if (rc) return rc;
    return memcmp(rb, w + 1, 4) == 0 ? TMP_OK : TMP_ECFG;
}

/* 연습 2: Linux i2c_transfer() 흉내. 메시지 사이 = Sr, 끝에 P 한 번. 성공 시 메시지 수 반환. */
#define I2C_M_RD 0x0001u
typedef struct { uint8_t addr; uint16_t flags; uint16_t len; uint8_t *buf; } I2cMsg;

static int i2c_transfer(I2cBus *b, I2cMsg *msgs, int n) {
    if (!msgs || n <= 0) return I2C_EARG;
    for (int i = 0; i < n; i++) {                           /* 버스를 건드리기 전에 전부 검사 */
        bool rd = msgs[i].flags & I2C_M_RD;
        if (msgs[i].addr > 0x7Fu || (msgs[i].len && !msgs[i].buf) || (rd && msgs[i].len == 0)) return I2C_EARG;
    }
    for (int i = 0; i < n; i++) {
        I2cMsg *m = &msgs[i];
        bool rd = m->flags & I2C_M_RD;
        bus_start(b);                                       /* i > 0 이면 busy → Sr */
        if (!bus_tx(b, rd ? sla_r(m->addr) : sla_w(m->addr))) { bus_stop(b); return I2C_ENACK_ADDR; }
        for (uint16_t k = 0; k < m->len; k++) {
            if (rd) m->buf[k] = bus_rx(b, k + 1u < m->len);
            else if (!bus_tx(b, m->buf[k])) { bus_stop(b); return I2C_ENACK_DATA; }
        }
    }
    bus_stop(b);
    return n;
}

/* 연습 3: read-modify-write. 보통판(STOP 두 번)과 버스를 놓지 않는 판(Sr 로 이어 쓰기). 반환 = 에러 또는 0 */
static int tmp_update_bits(TmpDrv *d, uint8_t reg, uint8_t mask, uint8_t val) {
    uint8_t old = 0;
    int rc = tmp_reg_read(d, reg, &old, 1);
    if (rc) return rc;
    uint8_t nv = (uint8_t)((old & ~mask) | (val & mask));
    return nv == old ? TMP_OK : tmp_reg_write1(d, reg, nv);
}

static int tmp_update_bits_locked(TmpDrv *d, uint8_t reg, uint8_t mask, uint8_t val) {
    I2cBus *b = d->bus;
    int rc = I2C_OK;
    bus_start(b);
    if (!bus_tx(b, sla_w(d->addr7))) { rc = I2C_ENACK_ADDR; goto out; }
    if (!bus_tx(b, reg)) { rc = I2C_ENACK_DATA; goto out; }
    bus_start(b);                                          /* Sr */
    if (!bus_tx(b, sla_r(d->addr7))) { rc = I2C_ENACK_ADDR; goto out; }
    uint8_t old = bus_rx(b, false);
    uint8_t nv = (uint8_t)((old & ~mask) | (val & mask));
    if (nv != old) {
        bus_start(b);                                      /* Sr 다시 — 버스를 계속 쥔 채 쓰기 */
        if (!bus_tx(b, sla_w(d->addr7))) { rc = I2C_ENACK_ADDR; goto out; }
        if (!bus_tx(b, reg) || !bus_tx(b, nv)) { rc = I2C_ENACK_DATA; goto out; }
    }
out:
    bus_stop(b);
    return rc;
}

/* 다른 마스터 M2 가 버스가 비면 CONFIG 의 AVG 비트(0x06)를 자기도 원자적으로 켠다 */
static void m2_set_avg(I2cBus *b, void *ctx) {
    (void)ctx;
    TmpDrv m2 = { b, TMP_ADDR };
    const char *save = b->master;
    b->master = "M2";
    tmp_update_bits_locked(&m2, REG_CONFIG, CFG_AVG_MASK, CFG_AVG_MASK);
    b->master = save;
}

/* 연습 4: i2cdetect 처럼 0x08..0x77 에 주소만 보내 ACK 하는 주소를 찾는다.
 * 반환 = 찾은 총 개수 (cap 보다 많으면 앞의 cap 개만 저장, 반환은 총 개수 — snprintf 식) */
static size_t i2c_scan(I2cBus *b, uint8_t *found, size_t cap) {
    size_t cnt = 0;
    for (uint8_t a = 0x08; a <= 0x77; a++) {
        if (i2c_write(b, a, NULL, 0) == I2C_OK) {
            if (cnt < cap) found[cnt] = a;
            cnt++;
        }
    }
    return cnt;
}

int main(void) {
    I2cBus bus;
    TmpSensor dev;
    TmpDrv drv = { &bus, TMP_ADDR };
    int rc;

    /* ---- 본문 코드 재확인 (practice.c 는 혼자 돈다) ---- */
    {
        static const int16_t script[] = { 3200, -64 };
        int32_t out[2] = { 0 };
        size_t got = 0;
        Intruder in = { REG_CONFIG, 0 };
        uint8_t reg = REG_WHO_AM_I, id = 0;
        setup(&bus, &dev);
        dev.conv_every = 2; dev.script = script; dev.script_len = 2;
        CHECK(tmp_init(&drv) == TMP_OK);
        CHECK(tmp_read_samples(&drv, out, 2, &got) == TMP_OK && got == 2 && out[0] == 25000 && out[1] == -500);
        setup(&bus, &dev);
        bus.on_free = intruder_run; bus.on_free_ctx = &in;
        CHECK(i2c_write_then_read(&bus, TMP_ADDR, &reg, 1, &id, 1) == I2C_OK && id == 0x28);
    }

    /* ---- 연습 1 ---- */
    CHECK(tmp_mC_to_raw(25000) == 3200 && tmp_mC_to_raw(-500) == -64);
    CHECK(tmp_mC_to_raw(3) == 0 && tmp_mC_to_raw(4) == 1 && tmp_mC_to_raw(-4) == -1 && tmp_mC_to_raw(-3) == 0);
    CHECK(tmp_mC_to_raw(300000) == INT16_MAX && tmp_mC_to_raw(-300000) == INT16_MIN);
    {
        long bad = 0;                                        /* raw → m°C(바닥) → raw 가 항상 제자리 */
        for (int32_t raw = INT16_MIN; raw <= INT16_MAX; raw++)
            if (tmp_mC_to_raw(tmp_raw_to_mC((int16_t)raw)) != raw) bad++;
        CHECK(bad == 0);
        int32_t lo = 0, hi = 0;                              /* m°C → raw → m°C 오차는 [-4, +3] */
        for (int i = 0; i < 20000; i++) {
            int32_t mC = (int32_t)(xorshift32() % 512001u) - 256000;
            int32_t e = tmp_raw_to_mC(tmp_mC_to_raw(mC)) - mC;
            if (e < lo) lo = e;
            if (e > hi) hi = e;
        }
        CHECK(lo >= -4 && hi <= 3);
        printf("--- 연습 1: m°C → raw → m°C 오차 범위 (20000 랜덤) = [%ld, %ld] ---\n", (long)lo, (long)hi);
    }
    setup(&bus, &dev);
    rc = tmp_set_limits(&drv, -1000, 30000);
    CHECK(rc == TMP_OK && dev.thigh == 0x0F00 && dev.tlow == 0xFF80);
    CHECK_LOG(&bus, "S 90 A 03 A 0F A 00 A FF A 80 A P S 90 A 03 A Sr 91 A 0F A 00 A FF A 80 N P");
    bus_log_clear(&bus);
    CHECK(tmp_set_limits(&drv, 30000, -1000) == TMP_EARG && bus.loglen == 0);

    /* ---- 연습 2 ---- */
    {
        uint8_t reg = REG_WHO_AM_I, id = 0, ptr3 = REG_THIGH_MSB, two[2] = { 0 }, cfgw[2] = { REG_CONFIG, 0x05 };
        I2cMsg m[3] = { { TMP_ADDR, 0, 1, &reg }, { TMP_ADDR, I2C_M_RD, 1, &id }, { 0, 0, 0, NULL } };
        setup(&bus, &dev);
        rc = i2c_transfer(&bus, m, 2);
        CHECK(rc == 2 && id == TMP_ID);
        CHECK_LOG(&bus, "S 90 A 0F A Sr 91 A A7 N P");      /* = i2c_write_read 와 같은 파형 */

        I2cMsg m3[3] = { { TMP_ADDR, 0, 1, &ptr3 }, { TMP_ADDR, I2C_M_RD, 2, two }, { TMP_ADDR, 0, 2, cfgw } };
        bus_log_clear(&bus);
        rc = i2c_transfer(&bus, m3, 3);
        CHECK(rc == 3 && two[0] == 0x28 && two[1] == 0x00 && dev.config == 0x05);
        CHECK_LOG(&bus, "S 90 A 03 A Sr 91 A 28 A 00 N Sr 90 A 02 A 05 A P");

        m[1].addr = 0x49;                                    /* 두 번째 메시지 주소 NACK */
        bus_log_clear(&bus);
        CHECK(i2c_transfer(&bus, m, 2) == I2C_ENACK_ADDR);
        CHECK_LOG(&bus, "S 90 A 0F A Sr 93 N P");

        m[1].addr = TMP_ADDR; m[1].len = 0;                  /* 0바이트 읽기는 거부, 버스 무변화 */
        bus_log_clear(&bus);
        CHECK(i2c_transfer(&bus, m, 2) == I2C_EARG && bus.loglen == 0);
    }

    /* ---- 연습 3 ---- */
    setup(&bus, &dev);                                       /* 바뀌는 게 없으면 쓰지 않는다 */
    dev.config = 0x01;
    CHECK(tmp_update_bits(&drv, REG_CONFIG, CFG_EN, CFG_EN) == TMP_OK && bus.n_stop == 1);

    printf("--- CONFIG 의 EN 켜기, 그 사이 M2 가 AVG 비트를 켜려 함 ---\n");
    setup(&bus, &dev);
    bus.on_free = m2_set_avg;
    rc = tmp_update_bits(&drv, REG_CONFIG, CFG_EN, CFG_EN);
    printf("  보통 RMW  : %s  -> CONFIG 0x%02X\n", bus.log, dev.config);
    CHECK(rc == TMP_OK && dev.config == 0x01);               /* M2 의 0x06 이 사라졌다 (lost update) */
    CHECK_LOG(&bus, "S 90 A 02 A Sr 91 A 00 N P S(M2) 90 A 02 A Sr 91 A 00 N Sr 90 A 02 A 06 A P S 90 A 02 A 01 A P");

    setup(&bus, &dev);
    bus.on_free = m2_set_avg;
    rc = tmp_update_bits_locked(&drv, REG_CONFIG, CFG_EN, CFG_EN);
    printf("  Sr 로 쥠  : %s  -> CONFIG 0x%02X\n", bus.log, dev.config);
    CHECK(rc == TMP_OK && dev.config == 0x07);
    CHECK_LOG(&bus, "S 90 A 02 A Sr 91 A 00 N Sr 90 A 02 A 01 A P S(M2) 90 A 02 A Sr 91 A 01 N Sr 90 A 02 A 07 A P");

    setup(&bus, &dev);
    drv.addr7 = 0x49;
    CHECK(tmp_update_bits_locked(&drv, REG_CONFIG, 1, 1) == I2C_ENACK_ADDR);
    CHECK_LOG(&bus, "S 92 N P");
    drv.addr7 = TMP_ADDR;

    /* ---- 연습 4 ---- */
    {
        TmpSensor dev2;
        uint8_t found[4] = { 0 };
        setup(&bus, &dev);
        dev_init(&dev2, 0x4A);
        bus_attach(&bus, &dev2);
        size_t cnt = i2c_scan(&bus, found, 4);
        CHECK(cnt == 2 && found[0] == 0x48 && found[1] == 0x4A);
        CHECK(bus.n_start == 112 && bus.n_stop == 112);
        memset(found, 0, sizeof found);
        cnt = i2c_scan(&bus, found, 1);
        CHECK(cnt == 2 && found[0] == 0x48 && found[1] == 0x00);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
