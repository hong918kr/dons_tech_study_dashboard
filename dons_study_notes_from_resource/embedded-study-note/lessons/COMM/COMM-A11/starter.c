/* COMM-A11 종합: 센서 허브 통신 스택 + 장애 주입
 *
 * 결정적 시뮬레이터 하나 (1 tick = 1 ms, 협력형 스케줄러, 스레드 없음, malloc 없음):
 *   하드웨어 모델 (주어짐)
 *     I2C 버스 + 센서 2개 tmp0(0x48) / hum1(0x40)  (COMM-M01/M03 축약판: 스트레칭 타임아웃, SDA 붙잡기, 9클럭 복구용 GPIO)
 *     SPI NOR 16 KB                                (COMM-A06 축약판: WREN/RDSR/PP(페이지 안에서 랩)/SE/READ, WIP 중 명령 무시)
 *     UART 링크 두 방향                            (115200 bps ≈ 11 B/tick, 지연 1 tick, 바이트 손실·비트 손상·다운)
 *     지상국                                       (COBS 스트림 → CRC → seq 중복 제거 → ACK, COMM-B12/M14 축약판)
 *   허브 소프트웨어 (TODO 는 전부 여기)
 *     버스 드라이버 → 센서 서비스(주기 샘플링, staleness) → 로거(NOR 레코드) / 텔레메트리(COBS+CRC16+seq/ACK)
 *     에러 분류 하나(hub_err, COMM-M12 처럼 계층마다 번역), 컴포넌트별 통계 + 건강 상태 OK/DEGRADED/FAULT,
 *     복구 행동: 버스 복구, 백오프 재초기화, stale 표시, 재전송, 링크 probe, drop-oldest.
 *
 * Part A: A1 i2c_map (HAL → hub_err), A2 logger_task (페이지 분할 + WIP 폴링 + 되읽기 검증), A3 tlm_task (stop-and-wait)
 * Part B: B1 q_push_drop_oldest, B2 health_update, B3 i2c_bus_recover, B4 센서 복구 정책 (bus_try_recover/sensor_fail/sensor_sample)
 * Part C: C1 hub_report (bring-up 보고서) + 시나리오별 장애 주입 + 무작위 soak (매 tick 불변식)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define GIVEN __attribute__((unused))   /* 주어진 도우미 — starter 에서는 아직 안 불릴 수 있음 */
#define TODO_FN __attribute__((unused)) /* TODO 함수끼리만 부르는 것 — starter 에서는 안 불릴 수 있음 */

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static uint32_t umin(uint32_t a, uint32_t b) { return a < b ? a : b; }
static void put_le32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t get_le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, 반사 없음, xorout 0, check("123456789") = 0x29B1 (COMM-B10 축약판) */
static uint16_t crc16(const uint8_t *p, size_t n) {
    uint16_t c = 0xFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint16_t)(p[i] << 8);
        for (int k = 0; k < 8; k++) c = (c & 0x8000u) ? (uint16_t)((c << 1) ^ 0x1021u) : (uint16_t)(c << 1);
    }
    return c;
}

/* ================= 1. 에러 분류 하나 + 이벤트 로그 ================= */
typedef enum {
    HUB_OK = 0, HUB_EAGAIN = -1, HUB_ENODEV = -2, HUB_EIO = -3, HUB_ETIMEDOUT = -4, HUB_EBUSY = -5,
    HUB_ECRC = -6, HUB_ENOSPC = -7, HUB_ELINK = -8, HUB_EBADID = -9, HUB_EINVAL = -10
} hub_err;
enum { HUB_NERR = 11 };
static const char *const ERR_NAME[HUB_NERR] = { "OK", "EAGAIN", "ENODEV", "EIO", "ETIMEDOUT", "EBUSY",
                                                "ECRC", "ENOSPC", "ELINK", "EBADID", "EINVAL" };
static int err_index(int e) { return (e <= 0 && e > -HUB_NERR) ? -e : -HUB_EINVAL; }

enum { C_I2C, C_S0, C_S1, C_NOR, C_LINK, NCOMP };
static const char *const COMP_NAME[NCOMP] = { "i2c0", "tmp0", "hum1", "nor0", "uart0" };

typedef enum { EV_STATE, EV_REC_OK, EV_REC_FAIL, EV_INIT, EV_STALE, EV_FRESH, EV_NOR_TO, EV_BURN,
               EV_LOG_DROP, EV_TLM_FAIL, EV_TLM_DROP, EV_PROBE, EV_LINK_UP, EV_KINDS } EvKind;
static const char *const EV_NAME[EV_KINDS] = { "state", "recover-ok", "recover-FAIL", "init", "stale", "fresh",
    "nor-timeout", "slot-burned", "log-drop-oldest", "tlm-fail", "tlm-drop-oldest", "link-probing", "link-up" };
typedef struct { uint32_t t; uint8_t comp, kind; int32_t arg; } Ev;
#define EVLOG_CAP 256u
typedef struct { Ev e[EVLOG_CAP]; uint32_t n; } EvLog;     /* 링: 최근 EVLOG_CAP 개, n = 지금까지 총 개수 */
static void ev(EvLog *L, uint32_t t, int comp, EvKind k, int32_t arg) {
    Ev *e = &L->e[L->n % EVLOG_CAP];
    e->t = t; e->comp = (uint8_t)comp; e->kind = (uint8_t)k; e->arg = arg;
    L->n++;
}

/* ================= 2. I2C 버스 + 센서 모델 (주어짐, COMM-M01/M03 축약판, 트랜잭션 수준) ================= */
#define I2C_STRETCH_LIMIT_US   1000u  /* 컨트롤러의 스트레칭 한도 (이 모델의 정의; SMBus t_TIMEOUT 25–35 ms 는 대표값) */
#define I2C_HOLD_AFTER_TIMEOUT 3u     /* 타임아웃으로 버려진 슬레이브가 SDA 를 잡고 있는 클럭 수 (모델 정의, ≤ 9) */
#define SENS_REG_DATA 0x00u           /* 2바이트 big-endian, 센티 단위 */
#define SENS_REG_CFG  0x02u           /* bit0 EN. 전원이 다시 들어오면 0 */
#define SENS_REG_WHO  0x0Fu
#define SENS_CFG_EN   0x01u
#define SENS_UNCONF   0x8000u         /* EN=0 일 때 DATA 값 (센서가 리셋됐다는 신호) */
enum { I2C_HAL_OK = 0, I2C_HAL_NACK_ADDR = 0x10, I2C_HAL_NACK_DATA = 0x11, I2C_HAL_TIMEOUT = 0x12,
       I2C_HAL_BUSY = 0x13, I2C_HAL_ARG = 0x14 };

typedef struct {
    uint8_t  idx, addr7, who, cfg, ptr;
    uint32_t stretch_us;          /* 평소 변환 스트레칭 */
    bool     off;                 /* 장애 knob: 전원 꺼짐/브라운아웃 → 주소 NACK. 다시 켜지면 cfg = 0 */
    uint32_t stretch_fault_us;    /* 장애 knob: 0 이 아니면 읽기마다 이만큼 스트레칭 */
    uint8_t  hold_clocks;         /* > 0: 바이트 중간에 멈춘 슬레이브가 SDA 를 LOW 로 잡음. SCL 펄스마다 1 감소 */
    uint32_t reads, boots;
} I2cDevM;
typedef struct {
    I2cDevM  dev[2];
    uint32_t now;
    bool     sda_short;           /* 장애 knob: 외부 단락으로 SDA LOW — 클럭으로 안 풀린다 */
    uint32_t xfers, pulses, stops;
} I2cBusM;

static int16_t phys_value(int idx, uint32_t t) {         /* "물리량": 지상국이 같은 식으로 값을 검증한다 */
    if (idx == 0) return (int16_t)(2150 + (int32_t)((t / 7u) % 400u));        /* 21.50 .. 25.49 °C */
    return (int16_t)(4000 + (int32_t)((t * 3u / 11u) % 2500u));               /* 40.00 .. 64.99 %RH */
}
static void dev_set_power(I2cDevM *d, bool on) {
    if (on && d->off) { d->cfg = 0; d->boots++; }        /* 재부팅: 설정이 날아간다 */
    if (!on) d->hold_clocks = 0;                          /* 전원이 꺼지면 SDA 도 놓는다 */
    d->off = !on;
}
static bool i2c_sda_level(const I2cBusM *b) {
    if (b->sda_short) return false;
    for (int k = 0; k < 2; k++) if (b->dev[k].hold_clocks && !b->dev[k].off) return false;
    return true;
}
static uint8_t dev_reg_read(const I2cDevM *d, uint8_t reg, uint32_t now) {
    uint16_t raw = (d->cfg & SENS_CFG_EN) ? (uint16_t)phys_value(d->idx, now) : (uint16_t)SENS_UNCONF;
    switch (reg) {
    case SENS_REG_DATA:     return (uint8_t)(raw >> 8);
    case SENS_REG_DATA + 1: return (uint8_t)raw;
    case SENS_REG_CFG:      return d->cfg;
    case SENS_REG_WHO:      return d->who;
    default:                return 0;
    }
}
/* S addr+W w[0..wn) [Sr addr+R r[0..rn)] P. w[0] = 레지스터 포인터. CFG 만 쓰기 가능 (다른 곳은 데이터 NACK). */
static int i2c_hal_xfer(I2cBusM *b, uint8_t addr7, const uint8_t *w, size_t wn, uint8_t *r, size_t rn) {
    if (addr7 > 0x7Fu || wn > 4 || rn > 8 || (wn && !w) || (rn && !r) || wn + rn == 0) return I2C_HAL_ARG;
    b->xfers++;
    if (!i2c_sda_level(b)) return I2C_HAL_BUSY;                 /* START 를 못 만든다 */
    I2cDevM *d = NULL;
    for (int k = 0; k < 2; k++) if (b->dev[k].addr7 == addr7 && !b->dev[k].off) d = &b->dev[k];
    if (!d) { b->stops++; return I2C_HAL_NACK_ADDR; }
    if (wn) {
        d->ptr = w[0];
        for (size_t i = 1; i < wn; i++, d->ptr++) {
            if (d->ptr != SENS_REG_CFG) { b->stops++; return I2C_HAL_NACK_DATA; }
            d->cfg = w[i];
        }
    }
    if (rn) {
        uint32_t st = d->stretch_fault_us ? d->stretch_fault_us : d->stretch_us;
        if (st > I2C_STRETCH_LIMIT_US) {                        /* 마스터는 포기하고 선을 놓지만 */
            d->hold_clocks = I2C_HOLD_AFTER_TIMEOUT;            /* 슬레이브는 바이트 중간: 0 비트로 SDA 를 잡고 있다 */
            return I2C_HAL_TIMEOUT;
        }
        for (size_t i = 0; i < rn; i++) r[i] = dev_reg_read(d, (uint8_t)(d->ptr + i), b->now);
        d->ptr = (uint8_t)(d->ptr + rn);
        d->reads++;
    }
    b->stops++;
    return I2C_HAL_OK;
}
/* 복구용 GPIO (핀을 오픈드레인 GPIO 로 바꿨다고 가정, COMM-M03) */
GIVEN static bool i2c_gpio_sda(const I2cBusM *b) { return i2c_sda_level(b); }
GIVEN static void i2c_gpio_scl_pulse(I2cBusM *b) {
    b->pulses++;
    for (int k = 0; k < 2; k++) if (b->dev[k].hold_clocks) b->dev[k].hold_clocks--;
}
GIVEN static void i2c_gpio_stop(I2cBusM *b) { b->stops++; }

/* ================= 3. SPI NOR 모델 (주어짐, COMM-A06 축약판) ================= */
#define NOR_PAGE   256u
#define NOR_SECTOR 4096u
#define NOR_NSECT  4u
#define NOR_SIZE   (NOR_SECTOR * NOR_NSECT)     /* 16 KB — 회전이 soak 안에서 일어나게 작게 (모델 크기) */
#define NOR_T_PP   2u                           /* tick. 실제 t_PP / t_SE 는 데이터시트 확인 권장 */
#define NOR_T_SE   25u
enum { NOR_PP = 0x02, NOR_READ = 0x03, NOR_RDSR = 0x05, NOR_WREN = 0x06, NOR_SE = 0x20, NOR_JEDEC = 0x9F };
#define SR_WIP 0x01u
#define SR_WEL 0x02u
typedef struct {
    uint8_t  mem[NOR_SIZE];
    uint8_t  wel;
    uint32_t busy_until, now;
    bool     stuck_busy;      /* 장애 knob: 칩 hang — WIP 가 계속 1 */
    bool     bad_program;     /* 장애 knob: PP 가 앞 절반만 프로그램 (부분 프로그램) */
    bool     se_pending;      /* 지우기는 끝나는 tick 에 반영된다 (모델) — 그 전에 hang 이 오면 절반만 지워진 채 중단 */
    uint32_t se_addr;
    uint32_t ign_busy, ign_nowel, wraps, programs, erases, erase_aborts;
} NorM;
static bool nor_wip(const NorM *f) { return f->stuck_busy || f->now < f->busy_until; }
/* CS↓ tx/rx n 바이트 CS↑ 한 프레임. 명령은 CS↑ 에서 실행. WIP 중엔 RDSR 말고 전부 무시. */
static void nor_frame(NorM *f, const uint8_t *tx, uint8_t *rx, size_t n) {
    if (!n) return;
    if (rx) memset(rx, 0xFF, n);
    uint8_t cmd = tx[0];
    if (nor_wip(f) && cmd != NOR_RDSR) { f->ign_busy++; return; }
    uint32_t a = n >= 4 ? (((uint32_t)tx[1] << 16) | ((uint32_t)tx[2] << 8) | tx[3]) % NOR_SIZE : 0u;
    switch (cmd) {
    case NOR_JEDEC: if (rx && n >= 4) { rx[1] = 0xEF; rx[2] = 0x40; rx[3] = 0x14; } break;  /* W25Q80 계열 값 (모델) */
    case NOR_RDSR:
        if (rx) for (size_t i = 1; i < n; i++) rx[i] = (uint8_t)((nor_wip(f) ? SR_WIP : 0u) | (f->wel ? SR_WEL : 0u));
        break;
    case NOR_WREN: f->wel = 1; break;
    case NOR_READ: if (rx) for (size_t i = 4; i < n; i++) rx[i] = f->mem[(a + i - 4) % NOR_SIZE]; break;
    case NOR_PP:
        if (!f->wel) { f->ign_nowel++; return; }
        if (n > 4) {
            size_t len = n - 4, keep = f->bad_program ? len / 2 : len;
            if ((a % NOR_PAGE) + len > NOR_PAGE) f->wraps++;          /* 고전 버그: 페이지 안에서 감긴다 */
            for (size_t i = 0; i < keep; i++)
                f->mem[(a & ~(NOR_PAGE - 1u)) | ((a + (uint32_t)i) & (NOR_PAGE - 1u))] &= tx[4 + i];
        }
        f->programs++; f->wel = 0; f->busy_until = f->now + NOR_T_PP;
        break;
    case NOR_SE:
        if (!f->wel) { f->ign_nowel++; return; }
        f->se_pending = true; f->se_addr = a & ~(NOR_SECTOR - 1u);
        f->erases++; f->wel = 0; f->busy_until = f->now + NOR_T_SE;
        break;
    default: break;
    }
}
static void nor_model_tick(NorM *f) {          /* world_tick 이 매 tick 부른다 */
    if (!f->se_pending) return;
    if (f->stuck_busy) {                        /* 지우는 중에 hang → 섹터 앞 절반만 지워진 채 중단 */
        memset(&f->mem[f->se_addr], 0xFF, NOR_SECTOR / 2);
        f->se_pending = false; f->erase_aborts++;
    } else if (f->now >= f->busy_until) {
        memset(&f->mem[f->se_addr], 0xFF, NOR_SECTOR);
        f->se_pending = false;
    }
}
static uint8_t nor_rdsr(NorM *f) { uint8_t tx[2] = { NOR_RDSR, 0 }, rx[2]; nor_frame(f, tx, rx, 2); return rx[1]; }
GIVEN static void nor_wren(NorM *f) { uint8_t tx = NOR_WREN; nor_frame(f, &tx, NULL, 1); }
static void nor_addr(uint8_t *tx, uint8_t cmd, uint32_t a) { tx[0] = cmd; tx[1] = (uint8_t)(a >> 16); tx[2] = (uint8_t)(a >> 8); tx[3] = (uint8_t)a; }
GIVEN static void nor_pp(NorM *f, uint32_t a, const uint8_t *p, size_t n) {
    uint8_t tx[4 + NOR_PAGE];
    if (n > NOR_PAGE) n = NOR_PAGE;
    nor_addr(tx, NOR_PP, a); memcpy(tx + 4, p, n);
    nor_frame(f, tx, NULL, 4 + n);
}
GIVEN static void nor_se(NorM *f, uint32_t a) { uint8_t tx[4]; nor_addr(tx, NOR_SE, a); nor_frame(f, tx, NULL, 4); }
GIVEN static void nor_read(NorM *f, uint32_t a, uint8_t *p, size_t n) {
    uint8_t tx[4 + 64] = { 0 }, rx[4 + 64];
    if (n > 64) n = 64;
    nor_addr(tx, NOR_READ, a);
    nor_frame(f, tx, rx, 4 + n);
    memcpy(p, rx + 4, n);
}

/* ================= 4. UART 링크 + COBS + 프레임 (주어짐, COMM-B12 / COMM-M14 축약판) ================= */
#define LINK_BPT   11u      /* 115200 8N1 = 11 520 B/s ≈ 11 B/ms */
#define LINK_DELAY 1u       /* 전파 + 수신 ISR 지연 (모델값) */
#define CH_CAP     512u
typedef struct { uint8_t b; uint32_t due; } ChB;
typedef struct {
    ChB      q[CH_CAP];
    uint32_t head, len, slot_t, slot_used, seed;
    bool     down;                       /* 장애 knob */
    uint32_t loss_ppm, corrupt_ppm;      /* 장애 knob: 바이트당 확률 (백만분율) */
    uint32_t sent, lost, corrupted, overflow;
} Chan;
static void ch_send(Chan *c, uint32_t now, const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (c->slot_t < now) { c->slot_t = now; c->slot_used = 0; }
        if (c->slot_used == LINK_BPT) { c->slot_t++; c->slot_used = 0; }
        c->slot_used++; c->sent++;
        uint8_t b = p[i];
        if (c->down) { c->lost++; continue; }
        if (c->loss_ppm && xs32(&c->seed) % 1000000u < c->loss_ppm) { c->lost++; continue; }
        if (c->corrupt_ppm && xs32(&c->seed) % 1000000u < c->corrupt_ppm) { b ^= (uint8_t)(1u << (xs32(&c->seed) & 7u)); c->corrupted++; }
        if (c->len == CH_CAP) { c->overflow++; continue; }
        ChB *s = &c->q[(c->head + c->len++) % CH_CAP];
        s->b = b; s->due = c->slot_t + LINK_DELAY;
    }
}
static bool ch_recv(Chan *c, uint32_t now, uint8_t *b) {
    if (!c->len || c->q[c->head].due > now) return false;
    *b = c->q[c->head].b; c->head = (c->head + 1) % CH_CAP; c->len--;
    return true;
}

static int cobs_encode(const uint8_t *src, size_t n, uint8_t *dst, size_t cap) {   /* COMM-B12 그대로 */
    if (cap < 1) return -1;
    size_t code_at = 0, o = 1;
    uint8_t code = 1;
    for (size_t i = 0; i < n; i++) {
        if (src[i] != 0) {
            if (o >= cap) return -1;
            dst[o++] = src[i];
            if (++code != 0xFF) continue;
            if (i + 1 == n) { dst[code_at] = 0xFF; return (int)o; }
        }
        dst[code_at] = code;
        if (o >= cap) return -1;
        code_at = o++;
        code = 1;
    }
    dst[code_at] = code;
    return (int)o;
}
/* 스트리밍 수신기 (COMM-B12 cobs_rx_feed 축약판): 바이트마다, 0x00 에서 판정 + 재동기화 */
#define RX_MAX 32u
enum { RX_NONE = 0, RX_FRAME = 1, RX_ERR = -1 };
typedef struct {
    uint8_t  buf[RX_MAX];
    size_t   len, frame_len;
    uint8_t  remain;
    bool     zero_pending, started, overflow;
    uint32_t ok, bad;
} CobsRx;
static void rx_put(CobsRx *rx, uint8_t b) { if (rx->len >= RX_MAX) rx->overflow = true; else rx->buf[rx->len++] = b; }
static int cobs_rx_feed(CobsRx *rx, uint8_t b) {
    if (b == 0x00) {
        int r = RX_NONE;
        if (rx->started) {
            if (rx->overflow || rx->remain || rx->len < 2 ||
                crc16(rx->buf, rx->len - 2) != (uint16_t)(rx->buf[rx->len - 2] << 8 | rx->buf[rx->len - 1])) { rx->bad++; r = RX_ERR; }
            else { rx->frame_len = rx->len - 2; rx->ok++; r = RX_FRAME; }
        }
        rx->len = 0; rx->remain = 0; rx->zero_pending = false; rx->started = false; rx->overflow = false;
        return r;
    }
    rx->started = true;
    if (rx->overflow) return RX_NONE;
    if (rx->remain == 0) {
        if (rx->zero_pending) rx_put(rx, 0x00);
        rx->remain = (uint8_t)(b - 1);
        rx->zero_pending = (b != 0xFF);
    } else { rx_put(rx, b); rx->remain--; }
    return RX_NONE;
}

/* 메시지 (CRC 앞 평문, 이 레슨의 정의):
 *   DATA = [01][seq][id LE32][sensor][flags][t LE32][value LE16]  (14 B)
 *   ACK  = [02][seq]   PING = [03][seq]                            (2 B)
 * 와이어 = COBS(평문 ‖ CRC16 상위 ‖ 하위) ‖ 0x00 */
enum { FT_DATA = 1, FT_ACK = 2, FT_PING = 3 };
#define MSG_DATA_LEN 14u
#define FRAME_CAP    24u
typedef struct { uint8_t type, seq, sensor, flags; uint32_t id, t; int16_t value; } Msg;
static int frame_encode(const Msg *m, uint8_t *out, size_t cap) {
    uint8_t p[MSG_DATA_LEN + 2];
    size_t n = 2;
    p[0] = m->type; p[1] = m->seq;
    if (m->type == FT_DATA) {
        put_le32(p + 2, m->id); p[6] = m->sensor; p[7] = m->flags; put_le32(p + 8, m->t);
        p[12] = (uint8_t)(uint16_t)m->value; p[13] = (uint8_t)((uint16_t)m->value >> 8);
        n = MSG_DATA_LEN;
    }
    uint16_t c = crc16(p, n);
    p[n] = (uint8_t)(c >> 8); p[n + 1] = (uint8_t)c;
    int k = cobs_encode(p, n + 2, out, cap);
    if (k < 0 || (size_t)k >= cap) return -1;
    out[k] = 0x00;
    return k + 1;
}
static hub_err msg_parse(const uint8_t *p, size_t n, Msg *m) {          /* CRC 는 cobs_rx_feed 가 이미 확인 */
    memset(m, 0, sizeof *m);
    if (n < 2) return HUB_EIO;
    m->type = p[0]; m->seq = p[1];
    if (m->type == FT_ACK || m->type == FT_PING) return n == 2 ? HUB_OK : HUB_EIO;
    if (m->type != FT_DATA || n != MSG_DATA_LEN) return HUB_EIO;
    m->id = get_le32(p + 2); m->sensor = p[6]; m->flags = p[7]; m->t = get_le32(p + 8);
    m->value = (int16_t)(uint16_t)(p[12] | p[13] << 8);
    return HUB_OK;
}
static bool seq_is_new(uint8_t seq, uint8_t last) {                      /* COMM-M14: 8비트, 반 창 */
    uint8_t d = (uint8_t)(seq - last);
    return d >= 1u && d <= 128u;
}

/* ================= 5. 허브 구조 ================= */
#define S0_PERIOD        20u     /* tick: tmp0 50 Hz */
#define S1_PERIOD        50u     /* tick: hum1 20 Hz */
#define S1_PHASE         5u
#define STALE_PERIODS    3u      /* 마지막 좋은 샘플이 3 주기보다 오래되면 stale */
#define SENS_BACKOFF_CAP 8u      /* 재초기화 백오프 상한 = 8 주기 */
#define BUS_BACKOFF_MIN  10u
#define BUS_BACKOFF_MAX  160u
#define HEALTH_OK_RUN    2u      /* OK 로 돌아가려면 연속 성공 2번 */
#define HEALTH_FAULT_RUN 3u      /* 연속 실패 3번이면 FAULT */
#define QCAP             16u
#define REC_SIZE         20u     /* 256 의 약수가 아니라서 몇몇 레코드는 페이지 경계를 넘는다 */
#define REC_MAGIC        0xA5u
#define SLOTS_PER_SECTOR (NOR_SECTOR / REC_SIZE)    /* 204 (끝 16 B 미사용) */
#define LG_TO_PROG       10u     /* tick: PP 대기 한도 (typ 2 의 5배, 모델 정의) */
#define LG_TO_ERASE      100u    /* tick: SE 대기 한도 (typ 25 의 4배) */
#define LG_BACKOFF_MIN   4u
#define LG_BACKOFF_MAX   64u
#define TLM_RTO_INIT     10u     /* RTT ≈ 3 tick 의 3배 남짓 */
#define TLM_RTO_MAX      80u
#define TLM_MAX_TX       4u      /* 첫 전송 + 재전송 3 */
#define LINK_FAULT_RUN   2u      /* 연속 실패 메시지 2개면 링크를 의심하고 probe 모드 */
#define PROBE_PERIOD     100u
#define MAX_SAMPLES      8192u

typedef struct { uint32_t id, t; int16_t value; uint8_t sensor, flags; } Sample;
typedef struct { Sample s[QCAP]; uint32_t head, len; } SampleQ;
GIVEN static bool q_pop(SampleQ *q, Sample *out) {
    if (!q->len) return false;
    *out = q->s[q->head]; q->head = (q->head + 1) % QCAP; q->len--;
    return true;
}

typedef enum { H_OK = 0, H_DEGRADED = 1, H_FAULT = 2 } HState;
static const char *const H_NAME[3] = { "OK", "DEGRADED", "FAULT" };
typedef struct {
    HState   st;
    uint8_t  err_run, ok_run;
    uint32_t ok_ev, err_ev, trans;
    uint32_t err[HUB_NERR];
    uint32_t t_left_ok, last_rec, rec_max, t_back_ok;
    uint32_t ticks_in[3];
} Health;

typedef struct { bool need_recover; uint32_t next_recover, backoff, rec_try, rec_ok, last_pulses; } BusSvc;
typedef struct {
    uint8_t  idx, addr7, who, comp;
    uint32_t period;
    bool     inited, stale;
    uint32_t next_sample, next_init, backoff, last_good;
    uint32_t slots, samples, misses, init_try, init_ok, stale_events;
} SensorSvc;
typedef enum { LG_IDLE, LG_WAIT } LgState;
typedef enum { OP_NONE, OP_ERASE, OP_PROG } LgOp;
typedef struct {
    SampleQ  q;
    LgState  st;
    LgOp     op;
    bool     have_cur, suspect;
    Sample   cur;
    uint8_t  rec[REC_SIZE];
    uint32_t off, chunk;                          /* 레코드 안에서 다음에 쓸 위치, 지금 쓰는 조각 길이 */
    uint32_t sector, slot;
    bool     erased[NOR_NSECT];
    uint32_t recs_in[NOR_NSECT];                  /* 섹터마다 커밋된 레코드 수 (지울 때 overwritten 으로) */
    uint32_t wait_start, retry_at, backoff;
    uint32_t logged, dropped, burned, overwritten, timeouts, splits;
} Logger;
typedef struct {
    SampleQ  q;
    bool     busy, is_ping, probing;
    Sample   cur;
    uint8_t  seq, fail_run;
    uint32_t deadline, rto, tx_count, next_probe;
    CobsRx   rx;
    uint32_t sent, retx, acked, failed, dropped, stale_acks, pings;
} Tlm;

enum { FATE_NONE = 0, FATE_LOGGED, FATE_LOG_DROPPED, FATE_ACKED, FATE_FAILED, FATE_TLM_DROPPED };
typedef void (*fate_fn)(void *ctx, uint32_t id, int fate);

typedef struct Hub {
    I2cBusM  *bus; NorM *nor; Chan *up, *down;     /* 하드웨어 (HAL 핸들) */
    BusSvc    bsv;
    SensorSvc sens[2];
    Logger    lg;
    Tlm       tl;
    Health    hl[NCOMP];
    bool      sampling;
    uint32_t  produced;
    EvLog     log;
    fate_fn   fate; void *fate_ctx;                 /* 테스트용 관찰 훅: 샘플 하나의 최종 운명 */
} Hub;

static void fate(Hub *h, uint32_t id, int f) { if (h->fate) h->fate(h->fate_ctx, id, f); }
static HState hub_overall(const Hub *h) {
    HState w = H_OK;
    for (int c = 0; c < NCOMP; c++) if (h->hl[c].st > w) w = h->hl[c].st;
    return w;
}

/* 센서 드라이버 (주어짐, COMM-M12 의 bus_ops 를 I2C 하나로 줄인 판) */
static hub_err i2c_map(int hal);
GIVEN static hub_err sens_init(Hub *h, const SensorSvc *s) {
    uint8_t reg = SENS_REG_WHO, who = 0;
    hub_err e = i2c_map(i2c_hal_xfer(h->bus, s->addr7, &reg, 1, &who, 1));
    if (e != HUB_OK) return e;
    if (who != s->who) return HUB_EBADID;
    uint8_t w[2] = { SENS_REG_CFG, SENS_CFG_EN };
    return i2c_map(i2c_hal_xfer(h->bus, s->addr7, w, 2, NULL, 0));
}
GIVEN static hub_err sens_read(Hub *h, const SensorSvc *s, int16_t *v) {
    uint8_t reg = SENS_REG_DATA, r[2] = { 0, 0 };
    hub_err e = i2c_map(i2c_hal_xfer(h->bus, s->addr7, &reg, 1, r, 2));
    if (e != HUB_OK) return e;
    uint16_t raw = (uint16_t)(r[0] << 8 | r[1]);
    if (raw == SENS_UNCONF) return HUB_EIO;      /* 센서가 리셋되어 설정이 날아감 → 재초기화가 필요 */
    *v = (int16_t)raw;
    return HUB_OK;
}
/* 로그 레코드 20 B: [A5][sensor][id LE32][t LE32][value LE16][flags][FF×5][CRC16 BE (0..17)] */
GIVEN static void rec_build(uint8_t *r, const Sample *s) {
    memset(r, 0xFF, REC_SIZE);
    r[0] = REC_MAGIC; r[1] = s->sensor; put_le32(r + 2, s->id); put_le32(r + 6, s->t);
    r[10] = (uint8_t)(uint16_t)s->value; r[11] = (uint8_t)((uint16_t)s->value >> 8); r[12] = s->flags;
    uint16_t c = crc16(r, REC_SIZE - 2);
    r[18] = (uint8_t)(c >> 8); r[19] = (uint8_t)c;
}
static bool rec_parse(const uint8_t *r, Sample *s) {
    if (r[0] != REC_MAGIC || crc16(r, REC_SIZE - 2) != (uint16_t)(r[18] << 8 | r[19])) return false;
    s->sensor = r[1]; s->id = get_le32(r + 2); s->t = get_le32(r + 6);
    s->value = (int16_t)(uint16_t)(r[10] | r[11] << 8); s->flags = r[12];
    return true;
}
GIVEN static uint32_t lg_slot_addr(const Logger *L) { return L->sector * NOR_SECTOR + L->slot * REC_SIZE; }
GIVEN static void tlm_send(Hub *h, uint32_t now) {               /* 지금 메시지(DATA 또는 PING)를 링크로 */
    Tlm *T = &h->tl;
    Msg m = { (uint8_t)(T->is_ping ? FT_PING : FT_DATA), T->seq, T->cur.sensor, T->cur.flags, T->cur.id, T->cur.t, T->cur.value };
    uint8_t f[FRAME_CAP];
    int n = frame_encode(&m, f, sizeof f);
    if (n > 0) ch_send(h->up, now, f, (size_t)n);
    T->sent++; T->tx_count++;
}

static bool q_push_drop_oldest(SampleQ *q, const Sample *s, Sample *dropped);
static void health_update(Hub *h, int comp, hub_err e, uint32_t now);

/* 샘플 하나 생산 = 두 싱크(로거, 텔레메트리)의 큐에 넣는다. 넘치면 가장 오래된 것을 버리고 센다 (주어짐). */
static void hub_produce(Hub *h, uint8_t sensor, uint32_t t, int16_t v) {
    Sample s = { h->produced++, t, v, sensor, (uint8_t)hub_overall(h) };
    Sample d;
    if (q_push_drop_oldest(&h->lg.q, &s, &d)) {
        h->lg.dropped++; h->hl[C_NOR].err[err_index(HUB_ENOSPC)]++;
        fate(h, d.id, FATE_LOG_DROPPED); ev(&h->log, t, C_NOR, EV_LOG_DROP, (int32_t)d.id);
    }
    if (q_push_drop_oldest(&h->tl.q, &s, &d)) {
        h->tl.dropped++; h->hl[C_LINK].err[err_index(HUB_ENOSPC)]++;
        fate(h, d.id, FATE_TLM_DROPPED); ev(&h->log, t, C_LINK, EV_TLM_DROP, (int32_t)d.id);
    }
}

/* ================= 6. TODO — 허브 소프트웨어 ================= */

/* TODO A1: I2C HAL 상태 코드 → hub_err. 전사적(total): 모르는 코드도 반드시 hub 도메인으로, 실패를 OK 로 바꾸지 않는다.
 *   OK→OK, NACK_ADDR→ENODEV, NACK_DATA→EIO, TIMEOUT→ETIMEDOUT, BUSY→EBUSY, ARG→EINVAL, 그 외→EIO */
static hub_err i2c_map(int hal) {
    (void)hal;
    return HUB_EIO;   /* TODO A1 */
}

/* TODO B1: 링 큐에 넣는다. 가득 차 있으면 가장 오래된 것을 *dropped 로 꺼내 버리고 넣는다 (true 반환). */
static bool q_push_drop_oldest(SampleQ *q, const Sample *s, Sample *dropped) {
    (void)q; (void)s; (void)dropped;
    return false;     /* TODO B1: 지금은 넣지도 버리지도 않는다 */
}

/* TODO B2: 건강 상태 기계 (이 레슨의 정의)
 *   e == OK : ok_ev++, err_run = 0. OK 가 아니면 ok_run++ 하고 HEALTH_OK_RUN 에 닿으면 OK.
 *   e != OK : err_ev++, err[err_index(e)]++, ok_run = 0, err_run++.
 *             목표 = err_run ≥ HEALTH_FAULT_RUN ? FAULT : DEGRADED. 에러로는 나빠지기만 한다 (목표 > 지금일 때만 바꿈).
 *   상태가 바뀌면: trans++, EV_STATE 로그. OK 를 떠날 때 t_left_ok = now,
 *   OK 로 돌아올 때 last_rec = now − t_left_ok, rec_max 갱신, t_back_ok = now. */
TODO_FN static void health_update(Hub *h, int comp, hub_err e, uint32_t now) {
    (void)h; (void)comp; (void)e; (void)now;   /* TODO B2 */
}

/* TODO B3: I2C 버스 복구 (COMM-M03 축약판). SDA 가 이미 1 이면 STOP 만 내고 OK (0 펄스).
 * 아니면 SCL 펄스를 최대 9번, 펄스마다 SDA 확인 → 1 이 되면 STOP, OK. 9번 뒤에도 0 이면 HUB_EBUSY.
 * *pulses = 실제로 낸 펄스 수. */
static hub_err i2c_bus_recover(I2cBusM *b, uint8_t *pulses) {
    (void)b;
    *pulses = 0;
    return HUB_EBUSY;  /* TODO B3 */
}

/* TODO B4 (1/3): 버스 복구 한 번 시도 + 기록.
 *   rec_try++. 성공: need_recover = false, rec_ok++, backoff = MIN, EV_REC_OK(pulses), 버스 건강 OK.
 *   실패: next_recover = now + backoff, backoff = min(2·backoff, MAX), EV_REC_FAIL, 버스 건강 EBUSY. */
TODO_FN static void bus_try_recover(Hub *h, uint32_t now) {
    (void)h; (void)now;   /* TODO B4 */
}

/* TODO B4 (2/3): 센서 동작이 실패했을 때 — "누구 탓인가"를 정하고 복구 행동을 고른다.
 *   ENODEV / EBADID / EIO : 버스는 응답했다(버스 OK). 센서 에러. inited = false,
 *                           next_init = now + backoff, backoff = min(2·backoff, SENS_BACKOFF_CAP·period).
 *   ETIMEDOUT             : 센서 에러 + 버스 에러 (슬레이브가 SDA 를 잡고 있을 수 있다) → need_recover, 지금 복구.
 *   EBUSY (그 외)         : 시작도 못 함 — 센서 탓이 아니다. 버스 에러만 → need_recover, 지금 복구. */
TODO_FN static void sensor_fail(Hub *h, SensorSvc *s, hub_err e, uint32_t now) {
    (void)h; (void)s; (void)e; (void)now;   /* TODO B4 */
}

/* TODO B4 (3/3): 샘플 슬롯 하나. 반환 OK = 샘플 생산, 그 외 = 이 슬롯은 miss.
 *   1) 버스가 복구를 기다리는 중이면: 아직 시간이 안 됐으면 EAGAIN, 됐으면 bus_try_recover, 여전히 막혀 있으면 EBUSY.
 *   2) 초기화 안 됐으면: next_init 전이면 EAGAIN. init_try++, EV_INIT(결과 코드), sens_init.
 *      실패 → sensor_fail, 그 코드 반환. 성공 → inited, init_ok++, backoff = period.
 *   3) sens_read. 실패 → sensor_fail. 성공 → 버스·센서 건강 OK, stale 이었으면 해제 + EV_FRESH,
 *      last_good = now, hub_produce. */
static hub_err sensor_sample(Hub *h, SensorSvc *s, uint32_t now) {
    (void)h; (void)s; (void)now;
    return HUB_EAGAIN; /* TODO B4: 지금은 모든 슬롯이 miss */
}

/* TODO A2: 로거 (tick 마다 한 번, 막히지 않는다). 레코드 = 20 B 고정 슬롯, 섹터당 204 개, 섹터 4개 순환.
 *   now < retry_at 이면 아무것도 안 한다 (타임아웃 뒤 백오프).
 *   LG_WAIT: RDSR. WIP=1 이면 now − wait_start ≥ 한도(ERASE 100 / 그 외 10) 일 때 타임아웃:
 *            timeouts++, suspect = true, 건강 ETIMEDOUT, EV_NOR_TO, retry_at = now + backoff,
 *            wait_start = retry_at, backoff 2배(상한). 어떤 명령도 더 보내지 않고 return.
 *            WIP=0 이면 끝난 동작 마무리 → LG_IDLE:
 *              ERASE: suspect 가 아니면 erased[sector] = true + 건강 OK (suspect 면 다시 지운다 — 지우기는 멱등)
 *              PROG : off += chunk. 레코드 끝이면 슬롯을 되읽어 rec 와 비교:
 *                     같으면 커밋 (logged++, recs_in++, slot++, have_cur = false, FATE_LOGGED, 건강 OK, backoff = MIN)
 *                     다르면 슬롯 태움 (burned++, slot++, off = 0, retry_at = now + backoff, backoff 2배,
 *                     건강 EIO, EV_BURN) — 같은 샘플을 다음 슬롯에
 *              suspect = false.
 *   LG_IDLE: 현재 레코드가 없으면 큐에서 꺼내 rec_build, off = 0 (큐가 비면 return).
 *            RDSR 이 WIP=1 이면 (타임아웃 난 동작이 아직 진행 중) 아무것도 보내지 말고 WAIT(OP_NONE).
 *            off == 0 이고 slot == SLOTS_PER_SECTOR 이면 다음 섹터로 (slot = 0, erased[새 섹터] = false).
 *            off == 0, slot == 0, !erased → overwritten += recs_in, recs_in = 0, WREN + SE → WAIT(ERASE).
 *            아니면 조각 = min(남은 바이트, 페이지 끝까지) (첫 조각이 레코드보다 짧으면 splits++),
 *            WREN + PP → WAIT(PROG). 기다림 시작 = now. */
static void logger_task(Hub *h, uint32_t now) {
    (void)h; (void)now;   /* TODO A2 */
}

/* TODO A3: 텔레메트리 stop-and-wait (COMM-M14 축약판) + 링크 probe.
 *   1) 하향 링크 바이트를 전부 cobs_rx_feed. RX_ERR → 링크 err[ECRC]++ (상태는 안 바꿈, 재전송이 처리).
 *      RX_FRAME → msg_parse. ACK 이고 busy 이고 seq 가 같으면 완료: busy = false, fail_run = 0, seq++,
 *      PING 이었으면 probing = false + EV_LINK_UP, DATA 였으면 acked++ + FATE_ACKED. 건강 OK.
 *      그 외 ACK 는 stale_acks++.
 *   2) busy 이고 now ≥ deadline:
 *      tx_count < 한도(PING 1, DATA TLM_MAX_TX) → rto = min(2·rto, MAX), deadline = now + rto, retx++, tlm_send.
 *      PING 이면 포기: busy = false, next_probe = now + PROBE_PERIOD, 건강 ELINK (seq 는 그대로 — 데이터 없는 핑).
 *      DATA 면 포기: busy = false, failed++, FATE_FAILED, EV_TLM_FAIL, seq++ (실은 도착했을 수 있다!),
 *                   건강 ETIMEDOUT, ++fail_run ≥ LINK_FAULT_RUN 이면 probing = true, next_probe = now, EV_PROBE.
 *   3) busy 가 아니면: probing 이면 now ≥ next_probe 일 때 PING 시작 (pings++), 아니면 큐에서 DATA 하나.
 *      시작 = busy, tx_count = 0, rto = RTO_INIT, deadline = now + rto, tlm_send. */
static void tlm_task(Hub *h, uint32_t now) {
    (void)h; (void)now;   /* TODO A3 */
}

/* TODO C1: bring-up 보고서. snprintf 처럼 동작: buf 에 최대 cap−1 글자 + NUL, 반환 = 잘리지 않았다면 쓸 길이.
 * 형식 (테스트가 파싱한다):
 *   "== sensor hub report t=%u overall=%s ==\n"
 *   "comp  state     ok_ev err_ev trans rec_max fault_t  errors\n"
 *   컴포넌트마다 "%-5s %-8s %6u %6u %5u %7u %7u " + 0 이 아닌 에러들 " NAME=n" (OK 칸 제외) + "\n"
 *   "bus: recover %u/%u last_pulses=%u\n"                                            (rec_ok/rec_try)
 *   센서마다 "%s: slots=%u samples=%u misses=%u init=%u/%u stale_events=%u%s\n"     (init_ok/init_try, stale 이면 " STALE")
 *   "log: produced=%u logged=%u dropped=%u burned=%u overwritten=%u timeouts=%u splits=%u pending=%u\n"
 *   "tlm: acked=%u failed=%u dropped=%u retx=%u pings=%u pending=%u\n"
 *   (pending = 큐 + 처리 중 1개) */
static size_t rp(char *buf, size_t cap, size_t pos, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
GIVEN static size_t rp(char *buf, size_t cap, size_t pos, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(pos < cap ? buf + pos : NULL, pos < cap ? cap - pos : 0, fmt, ap);
    va_end(ap);
    return k > 0 ? pos + (size_t)k : pos;
}
static size_t hub_report(const Hub *h, uint32_t now, char *buf, size_t cap) {
    (void)h; (void)now;
    if (cap) buf[0] = '\0';
    return 0;          /* TODO C1 */
}

/* ================= 7. 스케줄러 (주어짐): 1 tick = 센서 → 로거 → 텔레메트리, 각자 짧게 하고 돌려준다 ================= */
static void sensor_task(Hub *h, uint32_t now) {
    for (int k = 0; k < 2; k++) {
        SensorSvc *s = &h->sens[k];
        if (!s->stale && now - s->last_good > STALE_PERIODS * s->period) {
            s->stale = true; s->stale_events++;
            ev(&h->log, now, s->comp, EV_STALE, (int32_t)(now - s->last_good));
        }
        if (!h->sampling || now < s->next_sample) continue;
        s->next_sample += s->period;
        s->slots++;
        if (sensor_sample(h, s, now) == HUB_OK) s->samples++; else s->misses++;   /* 슬롯 = 샘플 + miss */
    }
}
static void hub_tick(Hub *h, uint32_t now) {
    sensor_task(h, now);
    logger_task(h, now);
    tlm_task(h, now);
    for (int c = 0; c < NCOMP; c++) h->hl[c].ticks_in[h->hl[c].st]++;
}
static void hub_init(Hub *h, I2cBusM *bus, NorM *nor, Chan *up, Chan *down) {
    memset(h, 0, sizeof *h);
    h->bus = bus; h->nor = nor; h->up = up; h->down = down;
    const uint8_t addr[2] = { 0x48, 0x40 }, who[2] = { 0xA1, 0xB2 };
    const uint32_t per[2] = { S0_PERIOD, S1_PERIOD }, ph[2] = { 0, S1_PHASE };
    for (int k = 0; k < 2; k++) {
        SensorSvc *s = &h->sens[k];
        s->idx = (uint8_t)k; s->addr7 = addr[k]; s->who = who[k]; s->comp = (uint8_t)(C_S0 + k);
        s->period = per[k]; s->next_sample = ph[k]; s->backoff = per[k];
    }
    h->bsv.backoff = BUS_BACKOFF_MIN;
    h->lg.backoff = LG_BACKOFF_MIN;
    h->tl.rto = TLM_RTO_INIT;
    h->sampling = true;
}

/* ================= 8. 시험대 (주어짐): World = 하드웨어 + 허브 + 지상국 + 장애 주입기 ================= */
typedef struct {
    Chan    *up, *down;
    CobsRx   rx;
    bool     have_last;
    uint8_t  last;
    uint8_t  got[MAX_SAMPLES];
    uint32_t unique, dup_suppressed, dup_delivered, value_err, pings, bad_frames, id_overflow;
} Ground;
static void ground_tick(Ground *g, uint32_t now) {
    uint8_t b;
    while (ch_recv(g->up, now, &b)) {
        if (cobs_rx_feed(&g->rx, b) != RX_FRAME) continue;
        Msg m;
        if (msg_parse(g->rx.buf, g->rx.frame_len, &m) != HUB_OK || (m.type != FT_DATA && m.type != FT_PING)) { g->bad_frames++; continue; }
        if (!g->have_last || seq_is_new(m.seq, g->last)) {
            g->have_last = true; g->last = m.seq;
            if (m.type == FT_PING) g->pings++;
            else if (m.id >= MAX_SAMPLES) g->id_overflow++;
            else if (g->got[m.id]) g->dup_delivered++;                         /* 같은 샘플이 두 번 = 버그 */
            else {
                g->got[m.id] = 1; g->unique++;
                if (m.sensor > 1 || m.value != phys_value(m.sensor, m.t)) g->value_err++;   /* 거짓 수락 감시 */
            }
        } else g->dup_suppressed++;
        Msg a = { FT_ACK, m.seq, 0, 0, 0, 0, 0 };
        uint8_t f[FRAME_CAP];
        int n = frame_encode(&a, f, sizeof f);
        if (n > 0) ch_send(g->down, now, f, (size_t)n);
    }
}

enum { F_SDA_HOLD, F_SDA_SHORT, F_SENSOR_OFF, F_STRETCH, F_NOR_BUSY, F_NOR_BADPROG, F_LINK_CORRUPT, F_LINK_LOSS, F_LINK_DOWN, F_KINDS };
static const char *const F_NAME[F_KINDS] = { "sda-hold", "sda-short", "sensor-off", "stretch", "nor-busy",
                                             "nor-badprog", "link-corrupt", "link-loss", "link-down" };
typedef struct { uint32_t from, until; uint8_t kind, target; uint32_t arg; } Fault;  /* target: 센서 0/1, 링크 0 둘다 1 상향 2 하향 */
#define FAULT_CAP 256u
typedef struct {
    I2cBusM  bus;
    NorM     nor;
    Chan     up, down;
    Ground   gnd;
    Hub      hub;
    Fault    f[FAULT_CAP];
    uint32_t nf, now;
} World;

static uint8_t  g_fate[2][MAX_SAMPLES];     /* [0] 로그 운명, [1] 텔레메트리 운명 */
static uint32_t g_fate_double, g_fate_overflow;
static void fate_record(void *ctx, uint32_t id, int f) {
    (void)ctx;
    if (id >= MAX_SAMPLES) { g_fate_overflow++; return; }
    int sink = (f == FATE_LOGGED || f == FATE_LOG_DROPPED) ? 0 : 1;
    if (g_fate[sink][id]) g_fate_double++;
    g_fate[sink][id] = (uint8_t)f;
}

static void fault_add(World *w, int kind, int target, uint32_t from, uint32_t until, uint32_t arg) {
    if (w->nf >= FAULT_CAP) return;
    Fault *f = &w->f[w->nf++];
    f->kind = (uint8_t)kind; f->target = (uint8_t)target; f->from = from; f->until = until; f->arg = arg;
}
static void faults_apply(World *w) {
    uint32_t now = w->now;
    bool off[2] = { false, false };
    uint32_t st_us[2] = { 0, 0 };
    Chan *ch[2] = { &w->up, &w->down };
    w->bus.sda_short = false; w->nor.stuck_busy = false; w->nor.bad_program = false;
    for (int c = 0; c < 2; c++) { ch[c]->down = false; ch[c]->loss_ppm = 0; ch[c]->corrupt_ppm = 0; }
    for (uint32_t i = 0; i < w->nf; i++) {
        const Fault *f = &w->f[i];
        int t = f->target & 1;
        if (f->kind == F_SDA_HOLD) {                       /* 순간 사건: 그 tick 에 슬레이브가 SDA 를 잡는다 */
            if (now == f->from && !w->bus.dev[t].off) w->bus.dev[t].hold_clocks = (uint8_t)f->arg;
            continue;
        }
        if (now < f->from || now >= f->until) continue;
        switch (f->kind) {
        case F_SDA_SHORT:   w->bus.sda_short = true; break;
        case F_SENSOR_OFF:  off[t] = true; break;
        case F_STRETCH:     st_us[t] = f->arg; break;
        case F_NOR_BUSY:    w->nor.stuck_busy = true; break;
        case F_NOR_BADPROG: w->nor.bad_program = true; break;
        default:
            for (int c = 0; c < 2; c++) {
                if (f->target != 0 && f->target != c + 1) continue;
                if (f->kind == F_LINK_DOWN) ch[c]->down = true;
                if (f->kind == F_LINK_LOSS && f->arg > ch[c]->loss_ppm) ch[c]->loss_ppm = f->arg;
                if (f->kind == F_LINK_CORRUPT && f->arg > ch[c]->corrupt_ppm) ch[c]->corrupt_ppm = f->arg;
            }
            break;
        }
    }
    for (int k = 0; k < 2; k++) { dev_set_power(&w->bus.dev[k], !off[k]); w->bus.dev[k].stretch_fault_us = st_us[k]; }
}
/* 무작위 장애 (고정 시드): [t0, t1) 안에서 평균 mean_gap tick 마다 하나 */
static void faults_random(World *w, uint32_t seed, uint32_t t0, uint32_t t1, uint32_t mean_gap) {
    uint32_t s = seed ? seed : 1u, t = t0;
    while (w->nf < FAULT_CAP) {
        t += 1u + xs32(&s) % (2u * mean_gap);
        if (t >= t1) break;
        int kind = (int)(xs32(&s) % F_KINDS);
        uint32_t r = xs32(&s), dur = 0, arg = 0;
        int target = (int)(r & 1u);
        switch (kind) {
        case F_SDA_HOLD:     arg = 1u + r % 9u; dur = 1; break;
        case F_SDA_SHORT:    dur = 20u + r % 380u; break;
        case F_SENSOR_OFF:   dur = 5u + r % 1500u; break;
        case F_STRETCH:      dur = 50u + r % 350u; arg = 1500u + r % 3500u; break;
        case F_NOR_BUSY:     dur = 20u + r % 780u; break;
        case F_NOR_BADPROG:  dur = 10u + r % 190u; break;
        case F_LINK_DOWN:    dur = 50u + r % 1450u; target = (int)(r % 3u); break;
        default:             dur = 50u + r % 750u; arg = 5000u + r % 25000u; target = (int)(r % 3u); break;
        }
        fault_add(w, kind, target, t, t + dur, arg);
    }
}
static void world_init(World *w, uint32_t seed) {
    memset(w, 0, sizeof *w);
    const uint8_t addr[2] = { 0x48, 0x40 }, who[2] = { 0xA1, 0xB2 };
    const uint32_t stretch[2] = { 0, 400 };          /* hum1 은 평소 400 µs 변환 스트레칭 (한도 1000 µs 안) */
    for (int k = 0; k < 2; k++) {
        I2cDevM *d = &w->bus.dev[k];
        d->idx = (uint8_t)k; d->addr7 = addr[k]; d->who = who[k]; d->stretch_us = stretch[k];
    }
    memset(w->nor.mem, 0xFF, NOR_SIZE);
    w->up.seed = (seed ^ 0x9E3779B9u) | 1u;
    w->down.seed = (seed ^ 0x7F4A7C15u) | 1u;
    w->gnd.up = &w->up; w->gnd.down = &w->down;
    hub_init(&w->hub, &w->bus, &w->nor, &w->up, &w->down);
    w->hub.fate = fate_record;
    memset(g_fate, 0, sizeof g_fate);
    g_fate_double = g_fate_overflow = 0;
}
static void world_tick(World *w) {
    faults_apply(w);
    w->bus.now = w->nor.now = w->now;
    nor_model_tick(&w->nor);
    hub_tick(&w->hub, w->now);
    ground_tick(&w->gnd, w->now);
    w->now++;
}

/* 불변식 (매 tick): NULL = 정상, 아니면 깨진 규칙 이름 */
static const char *inv_check(const World *w) {
    const Hub *h = &w->hub;
    const Logger *L = &h->lg;
    const Tlm *T = &h->tl;
    uint32_t inflight = (T->busy && !T->is_ping) ? 1u : 0u;
    if (h->produced != L->logged + L->dropped + L->q.len + (L->have_cur ? 1u : 0u)) return "log accounting";
    if (h->produced != T->acked + T->failed + T->dropped + T->q.len + inflight) return "tlm accounting";
    for (int k = 0; k < 2; k++) if (h->sens[k].slots != h->sens[k].samples + h->sens[k].misses) return "slot accounting";
    if (h->sens[0].samples + h->sens[1].samples != h->produced) return "samples != produced";
    if (w->gnd.dup_delivered || w->gnd.value_err || w->gnd.id_overflow) return "ground duplicate/value";
    if (w->gnd.unique < T->acked || w->gnd.unique > T->acked + T->failed + inflight) return "ground set bounds";
    if (w->nor.ign_busy || w->nor.ign_nowel || w->nor.wraps) return "nor command discipline";
    if (L->q.len > QCAP || T->q.len > QCAP) return "queue capacity";
    for (int c = 0; c < NCOMP; c++) {
        if (h->hl[c].st > H_FAULT) return "health range";
        if (h->hl[c].st == H_OK && h->hl[c].err_run) return "health OK with err_run";
    }
    if (g_fate_double || g_fate_overflow) return "fate assigned twice";
    return NULL;
}
static bool hub_idle(const Hub *h) {
    return !h->lg.q.len && !h->lg.have_cur && h->lg.st == LG_IDLE && !h->tl.q.len && !h->tl.busy;
}
typedef struct { uint32_t inv_fail, first_fail_t; const char *why; } RunStat;
static void run_ticks(World *w, uint32_t n, RunStat *rs) {
    for (uint32_t i = 0; i < n; i++) {
        world_tick(w);
        const char *why = inv_check(w);
        if (why && rs->inv_fail++ == 0) { rs->first_fail_t = w->now - 1; rs->why = why; }
    }
}
/* 샘플링을 멈추고 큐가 빌 때까지 (최대 max tick) */
static bool drain(World *w, uint32_t max, RunStat *rs) {
    w->hub.sampling = false;
    for (uint32_t i = 0; i < max && !hub_idle(&w->hub); i++) run_ticks(w, 1, rs);
    return hub_idle(&w->hub);
}
/* NOR 전체를 부팅 때처럼 훑는다 (테스트 오라클: 모델 메모리를 직접 읽음) */
typedef struct { uint32_t valid, dup, bad_value, not_logged; } ScanStat;
static ScanStat nor_scan(const World *w, uint8_t *seen) {
    ScanStat st = { 0, 0, 0, 0 };
    memset(seen, 0, MAX_SAMPLES);
    for (uint32_t sec = 0; sec < NOR_NSECT; sec++)
        for (uint32_t sl = 0; sl < SLOTS_PER_SECTOR; sl++) {
            Sample s;
            if (!rec_parse(&w->nor.mem[sec * NOR_SECTOR + sl * REC_SIZE], &s)) continue;
            st.valid++;
            if (s.id >= MAX_SAMPLES || s.sensor > 1) { st.bad_value++; continue; }
            if (seen[s.id]) st.dup++;
            seen[s.id] = 1;
            if (s.value != phys_value(s.sensor, s.t)) st.bad_value++;
            if (g_fate[0][s.id] != FATE_LOGGED) st.not_logged++;
        }
    return st;
}
/* 끝난 뒤 샘플 하나하나의 운명 확인. 0 = 정상 */
static uint32_t final_check(const World *w, bool verbose) {
    static uint8_t seen[MAX_SAMPLES];
    const Hub *h = &w->hub;
    uint32_t bad = 0;
    ScanStat sc = nor_scan(w, seen);
    if (sc.dup || sc.bad_value || sc.not_logged) bad++;
    if (sc.valid != h->lg.logged - h->lg.overwritten) bad++;
    for (uint32_t id = 0; id < h->produced && id < MAX_SAMPLES; id++) {
        uint8_t fl = g_fate[0][id], ft = g_fate[1][id], got = w->gnd.got[id];
        if (fl != FATE_LOGGED && fl != FATE_LOG_DROPPED) bad++;
        if (ft != FATE_ACKED && ft != FATE_FAILED && ft != FATE_TLM_DROPPED) bad++;
        if (ft == FATE_ACKED && !got) bad++;                      /* ACK 받았는데 지상국에 없음 */
        if (got && ft != FATE_ACKED && ft != FATE_FAILED) bad++;  /* 버렸다는데 지상국에 있음 */
    }
    if (verbose || bad)
        printf("  final: produced=%u nor_valid=%u (logged %u - overwritten %u) ground_unique=%u acked=%u failed=%u -> %s\n",
               h->produced, sc.valid, h->lg.logged, h->lg.overwritten, w->gnd.unique, h->tl.acked, h->tl.failed,
               bad ? "MISMATCH" : "every sample accounted");
    return bad;
}
static void print_events(const Hub *h, uint32_t max_lines) {
    uint32_t start = h->log.n > EVLOG_CAP ? h->log.n - EVLOG_CAP : 0, printed = 0;
    for (uint32_t i = start; i < h->log.n && printed < max_lines; i++, printed++) {
        const Ev *e = &h->log.e[i % EVLOG_CAP];
        printf("  [t=%5u] %-5s %-15s ", e->t, COMP_NAME[e->comp], EV_NAME[e->kind]);
        if (e->kind == EV_STATE) printf("-> %s\n", H_NAME[e->arg]);
        else if (e->kind == EV_INIT) printf("%s\n", ERR_NAME[err_index(e->arg)]);
        else if (e->kind == EV_REC_OK || e->kind == EV_REC_FAIL) printf("pulses=%d\n", e->arg);
        else printf("%d\n", e->arg);
    }
}

/* ================= 테스트 하네스 ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static World g_w;

static void test_models(void) {
    printf("-- models (given) --\n");
    CHECK(crc16((const uint8_t *)"123456789", 9) == 0x29B1);
    const uint8_t src[4] = { 0x11, 0x22, 0x00, 0x33 }, want[5] = { 0x03, 0x11, 0x22, 0x02, 0x33 };
    uint8_t enc[8];
    CHECK(cobs_encode(src, 4, enc, sizeof enc) == 5 && memcmp(enc, want, 5) == 0);
    /* 프레임 왕복: DATA 하나를 인코딩 → 스트림 디코더 → 파싱 */
    Msg m = { FT_DATA, 7, 1, 0, 0x01020304u, 1234, -2 }, back;
    uint8_t f[FRAME_CAP];
    int n = frame_encode(&m, f, sizeof f);
    CobsRx rx; memset(&rx, 0, sizeof rx);
    int r = RX_NONE;
    for (int i = 0; i < n; i++) r = cobs_rx_feed(&rx, f[i]);
    CHECK(n == 18 && r == RX_FRAME && msg_parse(rx.buf, rx.frame_len, &back) == HUB_OK &&
          back.id == m.id && back.t == m.t && back.value == -2 && back.seq == 7);
    printf("DATA frame (seq 7, id 0x01020304, hum1, t=1234, value -2): ");
    for (int i = 0; i < n; i++) printf("%02X ", f[i]);
    printf("(%d B)\n", n);
    /* NOR: 페이지 경계를 넘는 PP 는 같은 페이지 앞으로 감긴다 (고전 버그 재현) */
    world_init(&g_w, 1);
    NorM *nor = &g_w.nor;
    uint8_t rec[REC_SIZE];
    Sample s = { 42, 100, 2222, 0, 0 };
    rec_build(rec, &s);
    nor_wren(nor); nor_pp(nor, 0x0F0, rec, REC_SIZE);            /* 0x0F0 + 20 = 0x104 > 0x100 */
    CHECK(nor->wraps == 1 && nor->mem[0x100] == 0xFF && nor->mem[0x000] == rec[16] && nor->mem[0x0FF] == rec[15]);
    nor_wren(nor);
    CHECK(nor->ign_busy == 1 && (nor_rdsr(nor) & SR_WIP));       /* 바쁜 동안 WREN 무시, RDSR 만 된다 */
    nor->now = 10;
    nor_pp(nor, 0x200, rec, 4);
    CHECK(nor->ign_nowel == 1 && nor->mem[0x200] == 0xFF);       /* WREN 없는 PP 는 무시 */
    uint8_t tx[4] = { NOR_JEDEC, 0, 0, 0 }, idr[4];
    nor_frame(nor, tx, idr, 4);
    CHECK(idr[1] == 0xEF && idr[2] == 0x40 && idr[3] == 0x14);
    /* I2C: 정상 읽기, 없는 주소, 스트레칭 초과 → 슬레이브가 SDA 를 잡음 → 다음 START 불가 */
    I2cBusM *b = &g_w.bus;
    uint8_t reg = SENS_REG_WHO, who = 0;
    CHECK(i2c_hal_xfer(b, 0x48, &reg, 1, &who, 1) == I2C_HAL_OK && who == 0xA1);
    CHECK(i2c_hal_xfer(b, 0x50, &reg, 1, &who, 1) == I2C_HAL_NACK_ADDR);
    b->dev[1].stretch_fault_us = 5000;
    CHECK(i2c_hal_xfer(b, 0x40, &reg, 1, &who, 1) == I2C_HAL_TIMEOUT && b->dev[1].hold_clocks == I2C_HOLD_AFTER_TIMEOUT);
    CHECK(i2c_hal_xfer(b, 0x48, &reg, 1, &who, 1) == I2C_HAL_BUSY);
}

static void test_a1_map(void) {
    printf("-- A1 error translation --\n");
    CHECK(i2c_map(I2C_HAL_OK) == HUB_OK);
    CHECK(i2c_map(I2C_HAL_NACK_ADDR) == HUB_ENODEV);
    CHECK(i2c_map(I2C_HAL_NACK_DATA) == HUB_EIO);
    CHECK(i2c_map(I2C_HAL_TIMEOUT) == HUB_ETIMEDOUT);
    CHECK(i2c_map(I2C_HAL_BUSY) == HUB_EBUSY);
    CHECK(i2c_map(I2C_HAL_ARG) == HUB_EINVAL);
    int bad = 0;
    for (int c = 0; c <= 255; c++) {
        hub_err e = i2c_map(c);
        if (e > 0 || e <= -HUB_NERR || (c != 0 && e == HUB_OK)) bad++;
    }
    CHECK(bad == 0);                                              /* 전사적, 실패를 OK 로 안 바꿈 */
}

static void test_a2_logger(void) {
    printf("-- A2 logger: page split, WIP polling, read-back verify, sector rotation --\n");
    RunStat rs = { 0, 0, NULL };
    world_init(&g_w, 2);
    g_w.hub.sampling = false;
    const uint32_t N = 300;                                       /* 300 > 204 슬롯 → 두 번째 섹터도 쓴다 */
    for (uint32_t i = 0; i < N; i++) {
        hub_produce(&g_w.hub, (uint8_t)(i & 1), g_w.now, phys_value((int)(i & 1), g_w.now));
        g_w.hub.sens[i & 1].samples++; g_w.hub.sens[i & 1].slots++;   /* 직접 생산했으므로 장부 맞추기 */
        run_ticks(&g_w, 8, &rs);
    }
    CHECK(drain(&g_w, 2000, &rs));
    const Logger *L = &g_w.hub.lg;
    printf("logged=%u splits=%u erases=%u programs=%u timeouts=%u burned=%u\n",
           L->logged, L->splits, g_w.nor.erases, g_w.nor.programs, L->timeouts, L->burned);
    CHECK(L->logged == N && L->dropped == 0 && L->burned == 0 && L->timeouts == 0);
    CHECK(L->splits > 0 && g_w.nor.programs == N + L->splits);    /* 경계를 넘는 레코드는 PP 두 번 */
    CHECK(g_w.nor.erases == 2 && L->sector == 1);
    CHECK(rs.inv_fail == 0);                                      /* 랩 0, 바쁠 때 명령 0, WREN 없는 PP 0 */
    static uint8_t seen[MAX_SAMPLES];
    ScanStat sc = nor_scan(&g_w, seen);
    uint32_t missing = 0;
    for (uint32_t i = 0; i < N; i++) if (!seen[i]) missing++;
    CHECK(sc.valid == N && sc.dup == 0 && sc.bad_value == 0 && missing == 0);
    /* 경계를 넘는 슬롯 12 (0x0F0..0x103) 가 온전한가 */
    Sample s;
    CHECK(rec_parse(&g_w.nor.mem[12 * REC_SIZE], &s) && s.id == 12);
}

static void test_a3_tlm(void) {
    printf("-- A3 telemetry stop-and-wait on a clean link --\n");
    RunStat rs = { 0, 0, NULL };
    world_init(&g_w, 3);
    g_w.hub.sampling = false;
    for (uint32_t i = 0; i < 50; i++) {
        hub_produce(&g_w.hub, 0, g_w.now, phys_value(0, g_w.now));
        g_w.hub.sens[0].samples++; g_w.hub.sens[0].slots++;
        run_ticks(&g_w, 10, &rs);
    }
    CHECK(drain(&g_w, 1000, &rs));
    const Tlm *T = &g_w.hub.tl;
    printf("acked=%u sent=%u retx=%u seq=%u ground_unique=%u\n", T->acked, T->sent, T->retx, T->seq, g_w.gnd.unique);
    CHECK(T->acked == 50 && T->retx == 0 && T->failed == 0 && T->sent == 50 && T->seq == 50);
    CHECK(g_w.gnd.unique == 50 && g_w.gnd.dup_suppressed == 0 && g_w.gnd.value_err == 0);
    CHECK(rs.inv_fail == 0 && final_check(&g_w, false) == 0);
}

static uint32_t total_trans(const Hub *h) { uint32_t t = 0; for (int c = 0; c < NCOMP; c++) t += h->hl[c].trans; return t; }

static void test_a4_clean(void) {
    printf("-- A4 whole hub, no faults, 2000 ticks --\n");
    RunStat rs = { 0, 0, NULL };
    world_init(&g_w, 4);
    run_ticks(&g_w, 2000, &rs);
    const Hub *h = &g_w.hub;
    CHECK(h->produced == 2000 / S0_PERIOD + 2000 / S1_PERIOD);
    CHECK(h->sens[0].misses == 0 && h->sens[1].misses == 0 && h->sens[0].init_try == 1 && h->sens[1].init_try == 1);
    CHECK(total_trans(h) == 0 && hub_overall(h) == H_OK);         /* 고장 없으면 상태 전이 0 */
    CHECK(drain(&g_w, 1000, &rs));
    CHECK(h->lg.logged == h->produced && h->tl.acked == h->produced && h->tl.retx == 0);
    CHECK(rs.inv_fail == 0 && final_check(&g_w, true) == 0);
}

/* ---- Part B: 장애 시나리오 ---- */
static void scenario_begin(uint32_t seed) { world_init(&g_w, seed); }
static void scenario_end(RunStat *rs, uint32_t settle) {
    run_ticks(&g_w, settle, rs);
    CHECK(drain(&g_w, 4000, rs));
    CHECK(rs->inv_fail == 0);
    if (rs->inv_fail) printf("  invariant '%s' broke at t=%u (%u ticks)\n", rs->why, rs->first_fail_t, rs->inv_fail);
    CHECK(final_check(&g_w, false) == 0);
}

static void test_b3_recover_unit(void) {
    printf("-- B3 i2c_bus_recover unit --\n");
    int bad = 0;
    for (uint8_t hold = 0; hold <= 9; hold++) {
        world_init(&g_w, 5);
        g_w.bus.dev[0].hold_clocks = hold;
        uint8_t p = 99;
        hub_err e = i2c_bus_recover(&g_w.bus, &p);
        if (e != HUB_OK || p != hold || g_w.bus.stops != 1 || !i2c_gpio_sda(&g_w.bus)) bad++;
    }
    CHECK(bad == 0);                                              /* k 클럭 잡힘 → 정확히 k 펄스 + STOP */
    world_init(&g_w, 5);
    g_w.bus.dev[1].hold_clocks = 12;                              /* 9 로는 안 풀리는 슬레이브 (모델 밖의 고장) */
    uint8_t p = 0;
    CHECK(i2c_bus_recover(&g_w.bus, &p) == HUB_EBUSY && p == 9 && g_w.bus.stops == 0);
    world_init(&g_w, 5);
    g_w.bus.sda_short = true;
    CHECK(i2c_bus_recover(&g_w.bus, &p) == HUB_EBUSY && p == 9);
}

static void test_b2_health_unit(void) {
    printf("-- B2 health state machine unit --\n");
    world_init(&g_w, 20);
    Hub *h = &g_w.hub;
    Health *x = &h->hl[C_NOR];
    const struct { hub_err e; HState want; } seq[] = {
        { HUB_OK, H_OK }, { HUB_EIO, H_DEGRADED }, { HUB_OK, H_DEGRADED }, { HUB_OK, H_OK },          /* OK 는 연속 2번 */
        { HUB_ETIMEDOUT, H_DEGRADED }, { HUB_ETIMEDOUT, H_DEGRADED }, { HUB_ETIMEDOUT, H_FAULT },   /* 3번째에 FAULT */
        { HUB_OK, H_FAULT }, { HUB_EIO, H_FAULT },                                                  /* 에러로 좋아지지 않는다 */
        { HUB_OK, H_FAULT }, { HUB_OK, H_OK },
    };
    int bad = 0;
    for (uint32_t i = 0; i < sizeof seq / sizeof seq[0]; i++) {
        health_update(h, C_NOR, seq[i].e, 100 + 10 * i);
        if (x->st != seq[i].want) { bad++; printf("  step %u: got %s want %s\n", i, H_NAME[x->st], H_NAME[seq[i].want]); }
    }
    CHECK(bad == 0);
    CHECK(x->trans == 5 && x->ok_ev == 6 && x->err_ev == 5);
    CHECK(x->err[err_index(HUB_ETIMEDOUT)] == 3 && x->err[err_index(HUB_EIO)] == 2);
    CHECK(x->rec_max == 60 && x->last_rec == 60 && x->t_back_ok == 200);   /* t=140 에 OK 를 떠나 t=200 에 복귀 */
}

static void test_b_sda_hold(void) {
    printf("-- B: tmp0 stops mid-byte holding SDA for 5 clocks at t=400 --\n");
    RunStat rs = { 0, 0, NULL };
    scenario_begin(6);
    fault_add(&g_w, F_SDA_HOLD, 0, 400, 401, 5);
    run_ticks(&g_w, 1000, &rs);
    const Hub *h = &g_w.hub;
    printf("rec %u/%u last_pulses=%u bus EBUSY=%u misses tmp0=%u hum1=%u bus rec_max=%u\n", h->bsv.rec_ok, h->bsv.rec_try,
           h->bsv.last_pulses, h->hl[C_I2C].err[err_index(HUB_EBUSY)], h->sens[0].misses, h->sens[1].misses, h->hl[C_I2C].rec_max);
    CHECK(h->bsv.rec_try == 1 && h->bsv.rec_ok == 1 && h->bsv.last_pulses == 5);
    CHECK(h->hl[C_I2C].err[err_index(HUB_EBUSY)] == 1 && h->sens[0].misses + h->sens[1].misses == 1);
    CHECK(h->hl[C_S0].trans == 0 && h->hl[C_S1].trans == 0);      /* 버스 탓 → 센서는 안 나무란다 */
    CHECK(h->hl[C_I2C].st == H_OK && h->hl[C_I2C].rec_max <= S0_PERIOD);
    scenario_end(&rs, 200);
}

static void test_b_sda_short(void) {
    printf("-- B: SDA shorted low t=[1000,1300) --\n");
    RunStat rs = { 0, 0, NULL };
    scenario_begin(7);
    fault_add(&g_w, F_SDA_SHORT, 0, 1000, 1300, 0);
    uint32_t worst = H_OK;
    for (uint32_t i = 0; i < 2500; i++) { run_ticks(&g_w, 1, &rs); if (g_w.hub.hl[C_I2C].st > worst) worst = g_w.hub.hl[C_I2C].st; }
    const Hub *h = &g_w.hub;
    uint32_t back = h->hl[C_I2C].t_back_ok;
    printf("bus worst=%s rec %u/%u back_ok=t%u (fault end +%u) stale_events tmp0=%u hum1=%u misses %u/%u\n", H_NAME[worst],
           h->bsv.rec_ok, h->bsv.rec_try, back, back - 1300, h->sens[0].stale_events, h->sens[1].stale_events,
           h->sens[0].misses, h->sens[1].misses);
    CHECK(worst == H_FAULT && h->hl[C_I2C].st == H_OK);
    CHECK(back >= 1300 && back - 1300 <= BUS_BACKOFF_MAX + 2 * S0_PERIOD);   /* 회복 시간 상한 */
    CHECK(h->bsv.rec_ok == 1 && h->bsv.rec_try >= 4 && h->bsv.rec_try <= 10); /* 백오프: 매 슬롯 두드리지 않는다 */
    CHECK(h->sens[0].stale_events == 1 && h->sens[1].stale_events == 1);
    CHECK(h->hl[C_S0].trans == 0 && h->hl[C_S1].trans == 0);
    CHECK(h->sens[0].init_try == 1);                              /* 버스 고장으로 센서 재초기화를 하지 않는다 */
    scenario_end(&rs, 200);
}

static void test_b_sensor_off(void) {
    printf("-- B: hum1 NACKs t=[300,2300) (powered off), then comes back reset --\n");
    RunStat rs = { 0, 0, NULL };
    scenario_begin(8);
    fault_add(&g_w, F_SENSOR_OFF, 1, 300, 2300, 0);
    uint32_t worst = H_OK;
    for (uint32_t i = 0; i < 3500; i++) { run_ticks(&g_w, 1, &rs); if (g_w.hub.hl[C_S1].st > worst) worst = g_w.hub.hl[C_S1].st; }
    const Hub *h = &g_w.hub;
    uint32_t at[16], n = 0;
    uint32_t start = h->log.n > EVLOG_CAP ? h->log.n - EVLOG_CAP : 0;
    for (uint32_t i = start; i < h->log.n && n < 16; i++) {
        const Ev *e = &h->log.e[i % EVLOG_CAP];
        if (e->comp == C_S1 && e->kind == EV_INIT && e->t >= 300) at[n++] = e->t;
    }
    printf("hum1 init attempts after t=300:");
    for (uint32_t i = 0; i < n; i++) printf(" %u", at[i]);
    printf("  (back OK at t=%u)\n", h->hl[C_S1].t_back_ok);
    int gaps_ok = n >= 8;
    for (uint32_t i = 1; i < n && i < 8; i++) if (at[i] - at[i - 1] != umin(100u << (i - 1), SENS_BACKOFF_CAP * S1_PERIOD)) gaps_ok = 0;
    CHECK(gaps_ok);                                               /* 100, 200, 400, 400 ... (상한 8 주기) */
    CHECK(worst == H_FAULT && h->hl[C_S1].st == H_OK);
    CHECK(h->hl[C_S1].t_back_ok - 2300 <= SENS_BACKOFF_CAP * S1_PERIOD + 2 * S1_PERIOD);
    CHECK(h->hl[C_S1].err[err_index(HUB_ENODEV)] == n && g_w.bus.dev[1].boots == 1);   /* 읽기 1 + 실패한 init n−1 */
    CHECK(h->sens[0].misses == 0 && h->hl[C_S0].trans == 0 && h->hl[C_I2C].trans == 0);   /* 이웃은 멀쩡 */
    scenario_end(&rs, 300);
}

static void test_b_brownout(void) {
    printf("-- B: hum1 brown-out t=[312,322) between samples -> config lost --\n");
    RunStat rs = { 0, 0, NULL };
    scenario_begin(9);
    fault_add(&g_w, F_SENSOR_OFF, 1, 312, 322, 0);
    run_ticks(&g_w, 1000, &rs);
    const Hub *h = &g_w.hub;
    printf("hum1 EIO=%u ENODEV=%u init %u/%u misses=%u\n", h->hl[C_S1].err[err_index(HUB_EIO)],
           h->hl[C_S1].err[err_index(HUB_ENODEV)], h->sens[1].init_ok, h->sens[1].init_try, h->sens[1].misses);
    CHECK(h->hl[C_S1].err[err_index(HUB_EIO)] == 1 && h->hl[C_S1].err[err_index(HUB_ENODEV)] == 0);
    CHECK(h->sens[1].init_try == 2 && h->sens[1].init_ok == 2 && h->sens[1].misses == 1);
    CHECK(h->hl[C_S1].st == H_OK);
    scenario_end(&rs, 100);                                       /* 0x8000 이 샘플로 나갔다면 value_err 로 잡힌다 */
}

static void test_b_absent_at_boot(void) {
    printf("-- B: tmp0 never answers (absent at boot) --\n");
    RunStat rs = { 0, 0, NULL };
    scenario_begin(10);
    fault_add(&g_w, F_SENSOR_OFF, 0, 0, 100000, 0);
    run_ticks(&g_w, 3000, &rs);
    const Hub *h = &g_w.hub;
    printf("tmp0 state=%s init_try=%u samples=%u | hum1 samples=%u state=%s\n", H_NAME[h->hl[C_S0].st], h->sens[0].init_try,
           h->sens[0].samples, h->sens[1].samples, H_NAME[h->hl[C_S1].st]);
    CHECK(h->hl[C_S0].st == H_FAULT && h->sens[0].samples == 0);
    CHECK(h->sens[0].init_try <= 4 + 3000 / (SENS_BACKOFF_CAP * S0_PERIOD) + 1);   /* 백오프로 시도 수 제한 */
    CHECK(h->sens[1].samples == 3000 / S1_PERIOD && h->hl[C_S1].st == H_OK && hub_overall(h) == H_FAULT);
    g_w.nf = 0;                                                   /* 장애 제거 (나중에 꽂음) → 결국 회복 */
    scenario_end(&rs, 600);
    CHECK(g_w.hub.hl[C_S0].st == H_OK);
}

static void test_b_stretch(void) {
    printf("-- B: hum1 stretches 3000 us (> 1000 us limit) t=[600,800) --\n");
    RunStat rs = { 0, 0, NULL };
    scenario_begin(11);
    fault_add(&g_w, F_STRETCH, 1, 600, 800, 3000);
    run_ticks(&g_w, 1500, &rs);
    const Hub *h = &g_w.hub;
    printf("hum1 ETIMEDOUT=%u state=%s | bus ETIMEDOUT=%u EBUSY=%u rec %u/%u pulses=%u | tmp0 misses=%u\n",
           h->hl[C_S1].err[err_index(HUB_ETIMEDOUT)], H_NAME[h->hl[C_S1].st], h->hl[C_I2C].err[err_index(HUB_ETIMEDOUT)],
           h->hl[C_I2C].err[err_index(HUB_EBUSY)], h->bsv.rec_ok, h->bsv.rec_try, h->bsv.last_pulses, h->sens[0].misses);
    CHECK(h->hl[C_S1].err[err_index(HUB_ETIMEDOUT)] == 4 && h->sens[1].misses == 4);   /* 605, 655, 705, 755 */
    CHECK(h->bsv.rec_ok == 4 && h->bsv.rec_try == 4 && h->bsv.last_pulses == I2C_HOLD_AFTER_TIMEOUT);
    CHECK(h->hl[C_I2C].err[err_index(HUB_EBUSY)] == 0 && h->sens[0].misses == 0);    /* 즉시 복구 → 이웃 영향 0 */
    CHECK(h->hl[C_S1].st == H_OK && h->hl[C_S1].trans >= 2);
    scenario_end(&rs, 100);
}

static void test_b_nor_busy(void) {
    printf("-- B: NOR hangs busy t=[500,1500) --\n");
    RunStat rs = { 0, 0, NULL };
    scenario_begin(12);
    fault_add(&g_w, F_NOR_BUSY, 0, 500, 1500, 0);
    uint32_t worst = H_OK;
    for (uint32_t i = 0; i < 2500; i++) { run_ticks(&g_w, 1, &rs); if (g_w.hub.hl[C_NOR].st > worst) worst = g_w.hub.hl[C_NOR].st; }
    const Hub *h = &g_w.hub;
    printf("nor worst=%s timeouts=%u log_dropped=%u back_ok=t%u | tlm dropped=%u failed=%u | ignored-while-busy=%u\n",
           H_NAME[worst], h->lg.timeouts, h->lg.dropped, h->hl[C_NOR].t_back_ok, h->tl.dropped, h->tl.failed, g_w.nor.ign_busy);
    CHECK(worst == H_FAULT && h->hl[C_NOR].st == H_OK);
    CHECK(h->lg.timeouts >= 3 && h->lg.dropped > 0 && g_w.nor.ign_busy == 0);
    CHECK(h->hl[C_NOR].t_back_ok - 1500 <= LG_BACKOFF_MAX + LG_TO_PROG + 10);
    CHECK(h->tl.dropped == 0 && h->tl.failed == 0);               /* 텔레메트리는 영향 없음 */
    scenario_end(&rs, 200);
}

static void test_b_nor_badprog(void) {
    printf("-- B: NOR partial programs t=[300,360) -> read-back verify burns slots --\n");
    RunStat rs = { 0, 0, NULL };
    scenario_begin(13);
    fault_add(&g_w, F_NOR_BADPROG, 0, 300, 360, 0);
    run_ticks(&g_w, 1200, &rs);
    printf("burned=%u logged=%u\n", g_w.hub.lg.burned, g_w.hub.lg.logged);
    CHECK(g_w.hub.lg.burned >= 1 && g_w.hub.lg.dropped == 0);
    scenario_end(&rs, 100);                                       /* final_check: NOR 에 중복·깨진 레코드 0 */
}

static void test_b_nor_split_timeout(void) {
    printf("-- B: NOR hangs right after the first half of a page-split record --\n");
    RunStat rs = { 0, 0, NULL };
    scenario_begin(14);
    const Logger *L = &g_w.hub.lg;
    uint32_t guard = 0;
    while (!(L->st == LG_WAIT && L->op == OP_PROG && L->off == 0 && L->chunk < REC_SIZE) && guard++ < 5000) run_ticks(&g_w, 1, &rs);
    uint32_t t0 = g_w.now, split_id = L->cur.id;
    fault_add(&g_w, F_NOR_BUSY, 0, t0, t0 + 120, 0);
    run_ticks(&g_w, 800, &rs);
    printf("split record id=%u at slot %u: hang at t=%u, timeouts=%u burned=%u\n", split_id, L->slot, t0, L->timeouts, L->burned);
    CHECK(guard < 5000 && L->timeouts >= 1 && L->burned == 0);   /* 두 번째 조각을 마저 쓰고 검증 통과 */
    CHECK(g_fate[0][split_id] == FATE_LOGGED);
    scenario_end(&rs, 100);
}

static void test_b_nor_erase_hang(void) {
    printf("-- B: chip full of old data (0x00), NOR hangs t=[3,200) during the first sector erase --\n");
    RunStat rs = { 0, 0, NULL };
    scenario_begin(22);
    memset(g_w.nor.mem, 0x00, NOR_SIZE);
    fault_add(&g_w, F_NOR_BUSY, 0, 3, 200, 0);
    run_ticks(&g_w, 3500, &rs);
    const Logger *L = &g_w.hub.lg;
    printf("erase_aborts=%u erases=%u timeouts=%u burned=%u logged=%u\n", g_w.nor.erase_aborts, g_w.nor.erases, L->timeouts,
           L->burned, L->logged);
    CHECK(g_w.nor.erase_aborts == 1 && g_w.nor.erases >= 2 && L->timeouts >= 1);   /* 타임아웃 난 지우기는 다시 한다 */
    CHECK(L->burned == 0 && L->logged > SLOTS_PER_SECTOR);          /* 섹터 0 을 끝까지 썼는데 태운 슬롯 0 */
    scenario_end(&rs, 100);
}

static void test_b_link_corrupt(void) {
    printf("-- B: uplink+downlink corrupt 1%% of bytes t=[200,1700) --\n");
    RunStat rs = { 0, 0, NULL };
    scenario_begin(15);
    fault_add(&g_w, F_LINK_CORRUPT, 0, 200, 1700, 10000);
    run_ticks(&g_w, 2000, &rs);
    const Hub *h = &g_w.hub;
    printf("corrupted bytes up=%u down=%u | ground bad frames=%u dup_suppressed=%u | hub retx=%u ECRC=%u failed=%u acked=%u\n",
           g_w.up.corrupted, g_w.down.corrupted, g_w.gnd.rx.bad + g_w.gnd.bad_frames, g_w.gnd.dup_suppressed, h->tl.retx,
           h->hl[C_LINK].err[err_index(HUB_ECRC)], h->tl.failed, h->tl.acked);
    CHECK(g_w.up.corrupted > 0 && g_w.gnd.rx.bad > 0 && h->tl.retx > 0);
    CHECK(g_w.gnd.value_err == 0 && g_w.gnd.dup_delivered == 0);  /* 거짓 수락 0, 중복 전달 0 */
    scenario_end(&rs, 100);
}

static void test_b_link_loss(void) {
    printf("-- B: 2%% byte loss both ways t=[200,1700) --\n");
    RunStat rs = { 0, 0, NULL };
    scenario_begin(16);
    fault_add(&g_w, F_LINK_LOSS, 0, 200, 1700, 20000);
    run_ticks(&g_w, 2000, &rs);
    const Hub *h = &g_w.hub;
    printf("lost bytes up=%u down=%u | retx=%u failed=%u acked=%u stale_acks=%u dup_suppressed=%u\n", g_w.up.lost,
           g_w.down.lost, h->tl.retx, h->tl.failed, h->tl.acked, h->tl.stale_acks, g_w.gnd.dup_suppressed);
    CHECK(g_w.up.lost > 0 && h->tl.retx > 0 && g_w.gnd.dup_suppressed > 0);   /* ACK 손실 → 재전송 → 중복 억제 */
    scenario_end(&rs, 100);
}

static void test_b_link_down(void) {
    printf("-- B: link down both ways t=[1000,2000) --\n");
    RunStat rs = { 0, 0, NULL };
    scenario_begin(17);
    fault_add(&g_w, F_LINK_DOWN, 0, 1000, 2000, 0);
    uint32_t worst = H_OK;
    for (uint32_t i = 0; i < 2600; i++) { run_ticks(&g_w, 1, &rs); if (g_w.hub.hl[C_LINK].st > worst) worst = g_w.hub.hl[C_LINK].st; }
    const Hub *h = &g_w.hub;
    printf("link worst=%s failed=%u dropped=%u pings=%u back_ok=t%u (+%u) | log dropped=%u\n", H_NAME[worst], h->tl.failed,
           h->tl.dropped, h->tl.pings, h->hl[C_LINK].t_back_ok, h->hl[C_LINK].t_back_ok - 2000, h->lg.dropped);
    CHECK(worst == H_FAULT && h->hl[C_LINK].st == H_OK);
    CHECK(h->tl.failed == LINK_FAULT_RUN && h->tl.dropped > 0 && h->tl.pings >= 5);   /* 2개 실패 뒤엔 핑만 */
    CHECK(h->hl[C_LINK].t_back_ok - 2000 <= PROBE_PERIOD + TLM_RTO_INIT + 10);
    CHECK(h->lg.dropped == 0);                                    /* 로그는 영향 없음 */
    scenario_end(&rs, 100);
}

static void test_b_ack_path_down(void) {
    printf("-- B: only the downlink (ACKs) is down t=[1000,1400) -> 'failed' but delivered --\n");
    RunStat rs = { 0, 0, NULL };
    scenario_begin(18);
    fault_add(&g_w, F_LINK_DOWN, 2, 1000, 1400, 0);
    run_ticks(&g_w, 2000, &rs);
    const Hub *h = &g_w.hub;
    printf("failed=%u acked=%u ground_unique=%u dup_suppressed=%u\n", h->tl.failed, h->tl.acked, g_w.gnd.unique, g_w.gnd.dup_suppressed);
    CHECK(h->tl.failed == LINK_FAULT_RUN && g_w.gnd.unique > h->tl.acked);   /* 실패 = "모름" 이지 "안 감" 이 아니다 */
    CHECK(g_w.gnd.dup_delivered == 0);
    scenario_end(&rs, 100);
}

static void test_b_long_outage(void) {
    printf("-- B: link down both ways for 20000 ticks (> 128 probe periods) --\n");
    RunStat rs = { 0, 0, NULL };
    scenario_begin(21);
    fault_add(&g_w, F_LINK_DOWN, 0, 500, 20500, 0);
    run_ticks(&g_w, 21500, &rs);
    const Hub *h = &g_w.hub;
    printf("pings=%u failed=%u seq=%u ground_last=%u acked=%u ground_unique=%u link=%s\n", h->tl.pings, h->tl.failed,
           h->tl.seq, g_w.gnd.last, h->tl.acked, g_w.gnd.unique, H_NAME[h->hl[C_LINK].st]);
    CHECK(h->tl.pings > 128 && h->tl.failed == LINK_FAULT_RUN);   /* 실패한 핑은 seq 를 쓰지 않는다 → 간격 ≤ 3 */
    CHECK(h->hl[C_LINK].st == H_OK);
    scenario_end(&rs, 100);
}

/* ---- Part C: 보고서, 복합 장애, soak ---- */
static void test_c_report(void) {
    printf("-- C: combined faults + bring-up report --\n");
    RunStat rs = { 0, 0, NULL };
    scenario_begin(19);
    fault_add(&g_w, F_SDA_SHORT, 0, 400, 700, 0);
    fault_add(&g_w, F_SENSOR_OFF, 1, 900, 1600, 0);
    fault_add(&g_w, F_NOR_BUSY, 0, 1000, 1400, 0);
    fault_add(&g_w, F_LINK_DOWN, 0, 1200, 1900, 0);
    fault_add(&g_w, F_STRETCH, 0, 2100, 2160, 2500);
    run_ticks(&g_w, 3000, &rs);
    printf("event log (first 24 of %u):\n", g_w.hub.log.n);
    print_events(&g_w.hub, 24);
    static char buf[4096];
    size_t need = hub_report(&g_w.hub, g_w.now, buf, sizeof buf);
    fputs(buf, stdout);
    const Hub *h = &g_w.hub;
    CHECK(need > 0 && need < sizeof buf && strlen(buf) == need);
    char line[128];
    snprintf(line, sizeof line, "\nhum1  OK       %6u %6u %5u %7u %7u ", h->hl[C_S1].ok_ev, h->hl[C_S1].err_ev, h->hl[C_S1].trans,
             h->hl[C_S1].rec_max, h->hl[C_S1].ticks_in[H_FAULT]);
    CHECK(strstr(buf, line) != NULL);
    snprintf(line, sizeof line, "ENODEV=%u", h->hl[C_S1].err[err_index(HUB_ENODEV)]);
    CHECK(strstr(buf, line) != NULL && h->hl[C_S1].err[err_index(HUB_ENODEV)] > 0);
    snprintf(line, sizeof line, "bus: recover %u/%u ", h->bsv.rec_ok, h->bsv.rec_try);
    CHECK(strstr(buf, line) != NULL);
    snprintf(line, sizeof line, "tlm: acked=%u failed=%u dropped=%u ", h->tl.acked, h->tl.failed, h->tl.dropped);
    CHECK(strstr(buf, line) != NULL);
    CHECK(strstr(buf, "overall=OK") != NULL);
    char small[64];
    size_t need2 = hub_report(h, g_w.now, small, sizeof small);
    CHECK(need2 == need && strlen(small) == sizeof small - 1);    /* 잘려도 NUL, 반환은 필요한 길이 */
    scenario_end(&rs, 100);
}

static void test_c_soak(void) {
    printf("-- C: random fault soak (3 seeds x 20000 ticks, invariants every tick) --\n");
    const uint32_t seeds[3] = { 0xC0FFEEu, 0xBADC0DEu, 0x5EED1234u };
    for (int i = 0; i < 3; i++) {
        RunStat rs = { 0, 0, NULL };
        world_init(&g_w, seeds[i]);
        faults_random(&g_w, seeds[i], 100, 20000, 300);
        uint32_t kinds[F_KINDS] = { 0 };
        for (uint32_t k = 0; k < g_w.nf; k++) kinds[g_w.f[k].kind]++;
        run_ticks(&g_w, 23000, &rs);                              /* 20000 장애 구간 + 3000 조용한 구간 */
        const Hub *h = &g_w.hub;
        bool all_ok = hub_overall(h) == H_OK;
        CHECK(drain(&g_w, 6000, &rs));
        printf("seed %08X: faults=%u (", seeds[i], g_w.nf);
        for (int k = 0; k < F_KINDS; k++) printf("%s%s %u", k ? ", " : "", F_NAME[k], kinds[k]);
        printf(")\n  produced=%u logged=%u log_dropped=%u burned=%u | acked=%u failed=%u tlm_dropped=%u retx=%u | rec %u/%u | inv_fail=%u\n",
               h->produced, h->lg.logged, h->lg.dropped, h->lg.burned, h->tl.acked, h->tl.failed, h->tl.dropped, h->tl.retx,
               h->bsv.rec_ok, h->bsv.rec_try, rs.inv_fail);
        if (rs.inv_fail) printf("  invariant '%s' broke first at t=%u\n", rs.why, rs.first_fail_t);
        CHECK(rs.inv_fail == 0);
        CHECK(all_ok);                                            /* 조용한 3000 tick 뒤 전부 OK 로 돌아옴 */
        CHECK(final_check(&g_w, true) == 0);
        CHECK(h->lg.overwritten > 0 && h->bsv.rec_ok > 0 && h->tl.retx > 0);   /* soak 가 실제로 경로를 밟았다 */
    }
}

int main(void) {
    test_models();
    test_a1_map();
    test_a2_logger();
    test_a3_tlm();
    test_a4_clean();
    test_b2_health_unit();
    test_b3_recover_unit();
    test_b_sda_hold();
    test_b_sda_short();
    test_b_sensor_off();
    test_b_brownout();
    test_b_absent_at_boot();
    test_b_stretch();
    test_b_nor_busy();
    test_b_nor_badprog();
    test_b_nor_split_timeout();
    test_b_nor_erase_hang();
    test_b_link_corrupt();
    test_b_link_loss();
    test_b_link_down();
    test_b_ack_path_down();
    test_b_long_outage();
    test_c_report();
    test_c_soak();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
