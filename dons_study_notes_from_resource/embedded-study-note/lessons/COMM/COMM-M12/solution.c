/* COMM-M12 드라이버 계층화: 버스 추상화로 I2C/SPI 겸용 센서 드라이버
 *
 * 계층 (위 → 아래):
 *   app      : app_step()            — 언제 재초기화/버스 복구/포기할지 (정책). 버스 종류를 모른다.
 *   driver   : imu_*()               — MIMU-6D 레지스터 의미 (ID, reset, 설정, mg 변환). bus_t 만 안다.
 *   bus_ops  : i2c_/spi_read_regs()  — "레지스터 n개 읽기/쓰기"를 버스 프로토콜로 번역 + 에러 코드 번역.
 *   HAL      : i2c_hal_xfer() / spi_hal_cs() + spi_hal_xfer() — 버스 고유 상태 코드 (mock 컨트롤러).
 *   mock chip: ImuCore — I2C 와 SPI 를 둘 다 지원하는 가상 IMU "MIMU-6D" (이 레슨의 정의).
 * 에러 코드 도메인을 일부러 겹치지 않게 했다: HAL = 양수(0x10.., 0x20..), BUS = -1..-6, IMU = -101..-108.
 * 규칙: 아래 계층의 코드는 위로 새지 않는다(번역 함수가 전사적), 재시도는 imu_xfer() 한 곳에서만 (BUS_EAGAIN 만).
 * Part A: bus_ops 두 개 (I2C: 포인터 쓰기 + Sr + 읽기 / SPI: R/W+MS 명령 바이트) + 버스 독립 IMU 드라이버
 * Part B: 계층별 에러 번역, 재시도 정책, 장애 주입 (addr NACK, data NACK, arbitration lost, timeout,
 *         SPI 칩 없음 = MISO 0xFF, MISO 0x00, SPI timeout), 중간에 끊긴 burst 는 결과를 남기지 않는다
 * Part C: imu_init (probe → soft reset 폴링 → 설정 → 되읽기) + app_step (재시도/복구/재초기화 정책 + 로그)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define GIVEN __attribute__((unused))   /* 주어진 도우미 — starter 의 스텁 상태에서는 아직 안 불릴 수 있음 */

/* ================= 0. 로그 버퍼 (malloc 없음, 용량 넘으면 overflow 표시) ================= */
typedef struct { char s[8192]; size_t len; bool overflow; } Log;
static void log_clear(Log *l) { if (l) { l->len = 0; l->s[0] = '\0'; l->overflow = false; } }
__attribute__((format(printf, 2, 3)))
static void log_printf(Log *l, const char *fmt, ...) {
    if (!l) return;
    size_t room = sizeof l->s - l->len;
    va_list ap; va_start(ap, fmt);
    int k = vsnprintf(l->s + l->len, room, fmt, ap);
    va_end(ap);
    if (k < 0) return;
    if ((size_t)k >= room) { l->len = sizeof l->s - 1; l->overflow = true; } else l->len += (size_t)k;
}

/* ================= 1. mock 칩 MIMU-6D (I2C + SPI 듀얼 인터페이스, 레지스터 6비트 주소) ================= */
enum {
    R_CFG0   = 0x00,  /* 0x00..0x07 RW 설정 영역 (FIFO/INT 설정 자리, 기본 0) */
    R_WHO    = 0x0F,  /* RO, 0xB5 */
    R_CTRL1  = 0x10,  /* RW [7:4] ODR (0..10 유효), [3:2] FS (0 ±2g .. 3 ±16g) */
    R_CTRL2  = 0x11,  /* RW */
    R_CTRL3  = 0x12,  /* RW bit0 SW_RESET: 쓰면 리셋 시작, 끝나면 0 으로 읽힘 */
    R_STATUS = 0x27,  /* RO bit0 XLDA (새 샘플), bit7..1 예약 = 0. OUT_Z_H 를 읽으면 XLDA = 0 */
    R_OUTX_L = 0x28   /* RO 0x28..0x2D X_L X_H Y_L Y_H Z_L Z_H (little-endian int16) */
};
#define IMU_ID        0xB5u
#define CTRL3_SW_RST  0x01u
#define STATUS_XLDA   0x01u
#define IMU_I2C_ADDR  0x6Au   /* 7비트 주소 (SA0 = 0 이라 가정, 이 mock 의 정의) */

typedef struct {
    uint8_t  regs[64];
    uint8_t  who;            /* 장애 주입: 다른 칩 (예 0x33) */
    bool     write_protect;  /* 장애 주입: 쓰기는 ACK 되지만 무시됨 */
    unsigned reset_polls;    /* SW_RESET 후 CTRL3 를 이만큼 읽는 동안 bit0 = 1 (리셋 진행 중) */
    unsigned reset_left;
    unsigned resets, ignored;
} ImuCore;

static bool core_writable(uint8_t a) { return a <= 0x07u || (a >= R_CTRL1 && a <= R_CTRL3); }
static void core_defaults(ImuCore *c) { memset(c->regs, 0, sizeof c->regs); c->regs[R_WHO] = c->who; }
static void core_init(ImuCore *c) { memset(c, 0, sizeof *c); c->who = IMU_ID; core_defaults(c); }
static uint8_t core_read(ImuCore *c, uint8_t a) {
    a &= 0x3Fu;
    uint8_t v = c->regs[a];
    if (a == R_CTRL3 && c->reset_left && --c->reset_left == 0) c->regs[R_CTRL3] &= (uint8_t)~CTRL3_SW_RST;
    if (a == R_OUTX_L + 5) c->regs[R_STATUS] &= (uint8_t)~STATUS_XLDA;
    return v;
}
static void core_write(ImuCore *c, uint8_t a, uint8_t v) {
    a &= 0x3Fu;
    if (c->write_protect || !core_writable(a)) { c->ignored++; return; }
    if (a == R_CTRL3 && (v & CTRL3_SW_RST)) {          /* soft reset: 전 레지스터 기본값, 잠시 bit0 = 1 */
        core_defaults(c);
        c->regs[R_CTRL3] = c->reset_polls ? CTRL3_SW_RST : 0u;
        c->reset_left = c->reset_polls;
        c->resets++;
        return;
    }
    c->regs[a] = v;
}
static void core_set_sample(ImuCore *c, int16_t x, int16_t y, int16_t z) {
    int16_t v[3] = { x, y, z };
    for (int k = 0; k < 3; k++) {
        uint16_t u = (uint16_t)v[k];
        c->regs[R_OUTX_L + 2 * k] = (uint8_t)(u & 0xFFu);
        c->regs[R_OUTX_L + 2 * k + 1] = (uint8_t)(u >> 8);
    }
    c->regs[R_STATUS] |= STATUS_XLDA;
}

/* ================= 2. HAL: mock I2C 컨트롤러 (COMM-B07 바이트 수준 모델 축약판) ================= */
enum { I2C_HAL_OK = 0, I2C_HAL_NACK_ADDR = 0x10, I2C_HAL_NACK_DATA = 0x11, I2C_HAL_ARLO = 0x12, I2C_HAL_TIMEOUT = 0x13 };
enum { F_NONE = 0, F_NACK_ADDR, F_NACK_DATA, F_ARLO, F_TIMEOUT, F_MISO_FLOAT, F_MISO_LOW };
typedef struct { int kind; int at; int count; } Fault;   /* at = 트랜잭션 안의 데이터 바이트 번호, count = 남은 발동 횟수 (-1 = 계속) */

typedef struct {
    ImuCore *dev;       /* NULL = 버스에 아무도 없음 */
    uint8_t  dev_addr;
    uint8_t  ptr;       /* 칩의 레지스터 포인터 (I2C 는 항상 자동 증가, 0x3F 다음 0x00) */
    Fault    f;
    bool     stuck;     /* SDA 가 low 에 붙음: 복구(COMM-M03) 전까지 모든 전송 TIMEOUT */
    bool     in_txn;    /* START 와 STOP 사이 — HAL 호출이 끝나면 항상 false 여야 한다 */
    Log     *log;
    unsigned txns, recoveries;
    int      last_status;
} I2cHal;

static bool fault_fires(Fault *f, int kind, int at) {
    if (f->kind != kind || f->count == 0 || (at >= 0 && f->at != at)) return false;
    if (f->count > 0) f->count--;
    return true;
}
static int i2c_end(I2cHal *h, int st) { log_printf(h->log, " P\n"); h->in_txn = false; h->last_status = st; return st; }
/* S addr+W [w0..] (Sr addr+R [r0..]) P. wn == 0 이면 바로 읽기. 반환 = I2C_HAL_* */
static int i2c_hal_xfer(I2cHal *h, uint8_t a7, const uint8_t *w, size_t wn, uint8_t *r, size_t rn) {
    h->txns++;
    if (h->stuck) { log_printf(h->log, "S? SDA stuck low -> TIMEOUT\n"); h->last_status = I2C_HAL_TIMEOUT; return I2C_HAL_TIMEOUT; }
    h->in_txn = true;
    log_printf(h->log, "S %02X", (unsigned)((a7 << 1) | (wn ? 0u : 1u)));
    if (fault_fires(&h->f, F_ARLO, -1)) {              /* 다른 마스터가 이김: SDA 놓고 물러남 (COMM-M02) */
        log_printf(h->log, " ARLO\n"); h->in_txn = false; h->last_status = I2C_HAL_ARLO; return I2C_HAL_ARLO;
    }
    bool ack = h->dev && a7 == h->dev_addr && !fault_fires(&h->f, F_NACK_ADDR, -1);
    if (!ack) { log_printf(h->log, " N"); return i2c_end(h, I2C_HAL_NACK_ADDR); }
    log_printf(h->log, " A");
    int idx = 0;
    for (size_t i = 0; i < wn; i++, idx++) {
        if (fault_fires(&h->f, F_TIMEOUT, idx)) { log_printf(h->log, " %02X T", w[i]); return i2c_end(h, I2C_HAL_TIMEOUT); }
        log_printf(h->log, " %02X", w[i]);
        if (fault_fires(&h->f, F_NACK_DATA, idx)) { log_printf(h->log, " N"); return i2c_end(h, I2C_HAL_NACK_DATA); }
        if (i == 0) h->ptr = (uint8_t)(w[0] & 0x3Fu);
        else { core_write(h->dev, h->ptr, w[i]); h->ptr = (uint8_t)((h->ptr + 1u) & 0x3Fu); }
        log_printf(h->log, " A");
    }
    if (rn) {
        if (wn) log_printf(h->log, " Sr %02X A", (unsigned)((a7 << 1) | 1u));
        for (size_t i = 0; i < rn; i++, idx++) {
            if (fault_fires(&h->f, F_TIMEOUT, idx)) { log_printf(h->log, " T"); return i2c_end(h, I2C_HAL_TIMEOUT); }
            r[i] = core_read(h->dev, h->ptr);
            h->ptr = (uint8_t)((h->ptr + 1u) & 0x3Fu);
            log_printf(h->log, " %02X %c", r[i], i + 1 < rn ? 'A' : 'N');
        }
    }
    return i2c_end(h, I2C_HAL_OK);
}
static int i2c_hal_recover(I2cHal *h) {                  /* 9 클럭 + STOP (COMM-M03) — 여기선 결과만 모델링 */
    log_printf(h->log, "RECOVER 9xSCL + P\n");
    h->stuck = false; h->in_txn = false; h->recoveries++;
    return I2C_HAL_OK;
}

/* ================= 3. HAL: mock SPI 컨트롤러 + MIMU-6D SPI 쪽 (COMM-B05 모델 축약판) ================= */
enum { SPI_HAL_OK = 0, SPI_HAL_TIMEOUT = 0x20 };
enum { SPI_ST_CMD, SPI_ST_READ, SPI_ST_WRITE };
/* 명령 바이트 (이 mock 의 정의, ST LIS3DH 식): bit7 R/W (1 = 읽기), bit6 MS (1 = 자동 증가), bit5..0 주소. 더미 없음. */
typedef struct {
    ImuCore *dev;       /* NULL = 이 CS 에 칩 없음 → MISO 는 풀업으로 0xFF */
    bool     cs_low;
    int      st; uint8_t addr; bool inc; int fbyte;
    uint8_t  junk;      /* 명령 바이트 동안 MISO 쓰레기 (LCG) */
    Fault    f;         /* F_TIMEOUT (at = 프레임 안 바이트 번호), F_MISO_FLOAT / F_MISO_LOW (count 와 무관하게 유지) */
    Log     *log;
    unsigned frames, bytes;
    int      last_status;
} SpiHal;

static void spi_hal_cs(SpiHal *h, int level) {           /* level 0 = CS 내림 (active-low) */
    if (level == 0 && !h->cs_low) { h->cs_low = true; h->st = SPI_ST_CMD; h->fbyte = 0; h->frames++; log_printf(h->log, "["); }
    else if (level != 0 && h->cs_low) { h->cs_low = false; log_printf(h->log, " ]\n"); }
}
static uint8_t spi_dev_byte(SpiHal *h, uint8_t mosi) {   /* 칩이 바이트 하나를 주고받음. 반환 = MISO */
    ImuCore *c = h->dev;
    uint8_t miso = 0xFFu;
    if (!c) return 0xFFu;
    switch (h->st) {
    case SPI_ST_CMD:
        h->junk = (uint8_t)(h->junk * 5u + 0x3Bu); miso = h->junk;
        h->addr = (uint8_t)(mosi & 0x3Fu); h->inc = (mosi & 0x40u) != 0;
        h->st = (mosi & 0x80u) ? SPI_ST_READ : SPI_ST_WRITE;
        break;
    case SPI_ST_READ:
        miso = core_read(c, h->addr);
        if (h->inc) h->addr = (uint8_t)((h->addr + 1u) & 0x3Fu);
        break;
    default:
        h->junk = (uint8_t)(h->junk * 5u + 0x3Bu); miso = h->junk;
        core_write(c, h->addr, mosi);
        if (h->inc) h->addr = (uint8_t)((h->addr + 1u) & 0x3Fu);
        break;
    }
    return miso;
}
static int spi_hal_xfer(SpiHal *h, const uint8_t *tx, uint8_t *rx, size_t n) {   /* 전이중, CS 는 건드리지 않음 */
    for (size_t i = 0; i < n; i++) {
        if (fault_fires(&h->f, F_TIMEOUT, h->fbyte)) { log_printf(h->log, " T!"); h->last_status = SPI_HAL_TIMEOUT; return SPI_HAL_TIMEOUT; }
        uint8_t miso = h->cs_low ? spi_dev_byte(h, tx[i]) : 0xFFu;
        if (h->f.kind == F_MISO_FLOAT) miso = 0xFFu;     /* MISO 끊김: 칩은 MOSI 를 받지만 마스터는 0xFF */
        if (h->f.kind == F_MISO_LOW) miso = 0x00u;
        rx[i] = miso; h->fbyte++; h->bytes++;
        log_printf(h->log, " %02X/%02X", tx[i], miso);
    }
    h->last_status = SPI_HAL_OK;
    return SPI_HAL_OK;
}

/* ================= 4. 공통 버스 인터페이스 (bus_ops) ================= */
enum { BUS_OK = 0, BUS_EINVAL = -1, BUS_ENODEV = -2, BUS_EIO = -3, BUS_ETIMEDOUT = -4, BUS_EAGAIN = -5 };
#define BUS_MAX_XFER 32u
/* 계약: 0 또는 BUS_E* 만 반환. n 은 1..BUS_MAX_XFER. 잘못된 인자 → BUS_EINVAL, 선은 건드리지 않음.
 *       실패 시 buf 내용은 "정의되지 않음" (일부만 채워졌을 수 있음) → 호출자는 실패한 버퍼를 쓰면 안 된다.
 *       재시도하지 않는다 (한 번 호출 = 버스 트랜잭션 최대 1개). recover 는 선택 (NULL = 지원 안 함). */
typedef struct bus_ops {
    const char *name;
    int (*read_regs)(void *ctx, uint8_t reg, uint8_t *buf, size_t n);
    int (*write_regs)(void *ctx, uint8_t reg, const uint8_t *buf, size_t n);
    int (*recover)(void *ctx);
} bus_ops;
typedef struct { const bus_ops *ops; void *ctx; } bus_t;

typedef struct { I2cHal *hal; uint8_t addr7; } I2cDev;                     /* I2C 어댑터의 ctx */
typedef struct { SpiHal *hal; uint8_t read_flag, inc_flag; } SpiDev;       /* SPI 어댑터의 ctx (regmap 의 read_flag_mask 같은 것) */

static bool is_bus_code(int rc) { return rc <= BUS_OK && rc >= BUS_EAGAIN; }
static const char *bus_strerror(int rc) {
    switch (rc) {
    case BUS_OK: return "OK"; case BUS_EINVAL: return "BUS_EINVAL"; case BUS_ENODEV: return "BUS_ENODEV";
    case BUS_EIO: return "BUS_EIO"; case BUS_ETIMEDOUT: return "BUS_ETIMEDOUT"; case BUS_EAGAIN: return "BUS_EAGAIN";
    default: return "BUS_?";
    }
}
static const char *hal_strerror(int st) {
    switch (st) {
    case 0: return "HAL_OK"; case I2C_HAL_NACK_ADDR: return "I2C_NACK_ADDR"; case I2C_HAL_NACK_DATA: return "I2C_NACK_DATA";
    case I2C_HAL_ARLO: return "I2C_ARLO"; case I2C_HAL_TIMEOUT: return "I2C_TIMEOUT"; case SPI_HAL_TIMEOUT: return "SPI_TIMEOUT";
    default: return "HAL_?";
    }
}

/* ---- Part B: HAL 코드 → 공통 버스 코드 (전사적: 모르는 코드도 BUS_EIO 로) ---- */
static int i2c_map_err(int st) {
    switch (st) {
    case I2C_HAL_OK:        return BUS_OK;
    case I2C_HAL_NACK_ADDR: return BUS_ENODEV;     /* 주소에 아무도 대답 안 함 (Linux: -ENXIO) */
    case I2C_HAL_NACK_DATA: return BUS_EIO;        /* 칩은 있는데 바이트를 거부 */
    case I2C_HAL_ARLO:      return BUS_EAGAIN;     /* 다른 마스터가 이김 — 우리 트랜잭션은 일어나지 않음 */
    case I2C_HAL_TIMEOUT:   return BUS_ETIMEDOUT;  /* SCL/SDA 붙잡힘, 스트레칭 한계 초과 */
    default:                return BUS_EIO;
    }
}
static int spi_map_err(int st) {
    switch (st) {
    case SPI_HAL_OK:      return BUS_OK;
    case SPI_HAL_TIMEOUT: return BUS_ETIMEDOUT;
    default:              return BUS_EIO;
    }
}

/* ---- Part A: I2C 어댑터 — 읽기 = [S addr+W reg Sr addr+R d0..dn-1 P], 쓰기 = [S addr+W reg d0.. P] ---- */
static int i2c_read_regs(void *ctx, uint8_t reg, uint8_t *buf, size_t n) {
    I2cDev *d = (I2cDev *)ctx;
    if (!d || !buf || n == 0 || n > BUS_MAX_XFER) return BUS_EINVAL;
    return i2c_map_err(i2c_hal_xfer(d->hal, d->addr7, &reg, 1, buf, n));
}
static int i2c_write_regs(void *ctx, uint8_t reg, const uint8_t *buf, size_t n) {
    I2cDev *d = (I2cDev *)ctx;
    if (!d || !buf || n == 0 || n > BUS_MAX_XFER) return BUS_EINVAL;
    uint8_t tmp[1 + BUS_MAX_XFER];
    tmp[0] = reg;
    memcpy(tmp + 1, buf, n);
    return i2c_map_err(i2c_hal_xfer(d->hal, d->addr7, tmp, 1 + n, NULL, 0));
}
static int i2c_recover(void *ctx) { return i2c_map_err(i2c_hal_recover(((I2cDev *)ctx)->hal)); }

/* ---- Part A: SPI 어댑터 — [CS↓ cmd d0..dn-1 CS↑], cmd = read_flag | (n > 1 ? inc_flag : 0) | reg ---- */
static int spi_read_regs(void *ctx, uint8_t reg, uint8_t *buf, size_t n) {
    SpiDev *d = (SpiDev *)ctx;
    if (!d || !buf || n == 0 || n > BUS_MAX_XFER || reg > 0x3Fu || reg + n > 0x40u) return BUS_EINVAL;
    uint8_t tx[1 + BUS_MAX_XFER] = { 0 }, rx[1 + BUS_MAX_XFER];
    tx[0] = (uint8_t)(d->read_flag | (n > 1 ? d->inc_flag : 0u) | reg);
    spi_hal_cs(d->hal, 0);
    int st = spi_hal_xfer(d->hal, tx, rx, 1 + n);
    spi_hal_cs(d->hal, 1);                         /* 에러여도 CS 는 반드시 올린다 */
    if (st != SPI_HAL_OK) return spi_map_err(st);
    memcpy(buf, rx + 1, n);                        /* 명령 바이트 동안의 MISO 는 버림 */
    return BUS_OK;
}
static int spi_write_regs(void *ctx, uint8_t reg, const uint8_t *buf, size_t n) {
    SpiDev *d = (SpiDev *)ctx;
    if (!d || !buf || n == 0 || n > BUS_MAX_XFER || reg > 0x3Fu || reg + n > 0x40u) return BUS_EINVAL;
    uint8_t tx[1 + BUS_MAX_XFER], rx[1 + BUS_MAX_XFER];
    tx[0] = (uint8_t)((n > 1 ? d->inc_flag : 0u) | reg);
    memcpy(tx + 1, buf, n);
    spi_hal_cs(d->hal, 0);
    int st = spi_hal_xfer(d->hal, tx, rx, 1 + n);
    spi_hal_cs(d->hal, 1);
    return spi_map_err(st);
}

static const bus_ops I2C_BUS_OPS = { "i2c", i2c_read_regs, i2c_write_regs, i2c_recover };
static const bus_ops SPI_BUS_OPS = { "spi", spi_read_regs, spi_write_regs, NULL };

/* ================= 5. 디바이스 드라이버 (버스를 모른다: bus_t 만 쓴다) ================= */
enum { IMU_OK = 0, IMU_EINVAL = -101, IMU_ENOTFOUND = -102, IMU_EBADID = -103, IMU_EIO = -104,
       IMU_ETIMEDOUT = -105, IMU_EBUSY = -106, IMU_ENOTREADY = -107, IMU_ENOTSUP = -108 };
#define IMU_MAX_ATTEMPTS 3u   /* BUS_EAGAIN 일 때만, 첫 시도 포함 최대 3번 */
#define IMU_RESET_POLLS  5u   /* SW_RESET 완료 확인 읽기 최대 횟수 (실제 칩은 사이에 ms 지연) */

typedef struct {
    bus_t    bus;
    bool     ready;
    uint8_t  odr, fs;
    unsigned attempts, retries;   /* 통계: 버스 호출 수, 그중 재시도 수 */
    int      last_bus_err;        /* 마지막 bus_ops 반환값 (진단용, 위로 전달하지는 않음) */
} imu_t;

static bool is_imu_code(int rc) { return rc == IMU_OK || (rc <= IMU_EINVAL && rc >= IMU_ENOTSUP); }
static const char *imu_strerror(int rc) {
    switch (rc) {
    case IMU_OK: return "OK"; case IMU_EINVAL: return "IMU_EINVAL"; case IMU_ENOTFOUND: return "IMU_ENOTFOUND";
    case IMU_EBADID: return "IMU_EBADID"; case IMU_EIO: return "IMU_EIO"; case IMU_ETIMEDOUT: return "IMU_ETIMEDOUT";
    case IMU_EBUSY: return "IMU_EBUSY"; case IMU_ENOTREADY: return "IMU_ENOTREADY"; case IMU_ENOTSUP: return "IMU_ENOTSUP";
    default: return "IMU_?";
    }
}

/* ---- Part B: 공통 버스 코드 → 드라이버 코드 ---- */
static int imu_map_err(int bus_rc) {
    switch (bus_rc) {
    case BUS_OK:        return IMU_OK;
    case BUS_EINVAL:    return IMU_EINVAL;     /* 드라이버 버그 (잘못된 길이/주소) */
    case BUS_ENODEV:    return IMU_ENOTFOUND;
    case BUS_ETIMEDOUT: return IMU_ETIMEDOUT;
    case BUS_EAGAIN:    return IMU_EBUSY;      /* 재시도를 다 써도 버스를 못 얻음 */
    case BUS_EIO:
    default:            return IMU_EIO;
    }
}

/* ---- Part B: 드라이버의 유일한 버스 출입구. 재시도 정책은 여기 한 곳 ----
 * wbuf != NULL 이면 쓰기, 아니면 rbuf 로 읽기. BUS_EAGAIN 만 최대 IMU_MAX_ATTEMPTS 번까지 다시 시도한다. */
static int imu_xfer(imu_t *imu, uint8_t reg, uint8_t *rbuf, const uint8_t *wbuf, size_t n) {
    int rc = BUS_EIO;
    for (unsigned a = 0; a < IMU_MAX_ATTEMPTS; a++) {
        if (a) imu->retries++;
        imu->attempts++;
        rc = wbuf ? imu->bus.ops->write_regs(imu->bus.ctx, reg, wbuf, n)
                  : imu->bus.ops->read_regs(imu->bus.ctx, reg, rbuf, n);
        if (rc != BUS_EAGAIN) break;
    }
    imu->last_bus_err = rc;
    return imu_map_err(rc);
}
GIVEN static int imu_rd(imu_t *imu, uint8_t reg, uint8_t *buf, size_t n) { return imu_xfer(imu, reg, buf, NULL, n); }
GIVEN static int imu_wr(imu_t *imu, uint8_t reg, const uint8_t *buf, size_t n) { return imu_xfer(imu, reg, NULL, buf, n); }

/* ---- Part A: WHO_AM_I. 0x00/0xFF = 아무도 MISO/SDA 를 안 움직임 → NOTFOUND, 그 외 불일치 → BADID ---- */
static int imu_probe(imu_t *imu) {
    uint8_t id = 0;
    int rc = imu_rd(imu, R_WHO, &id, 1);
    if (rc != IMU_OK) return rc;
    if (id == 0x00u || id == 0xFFu) return IMU_ENOTFOUND;
    if (id != IMU_ID) return IMU_EBADID;
    return IMU_OK;
}

/* ---- Part A: CTRL1 = odr<<4 | fs<<2 쓰고 되읽어 확인. 인자 오류는 버스를 건드리지 않음 ---- */
static int imu_config(imu_t *imu, uint8_t odr, uint8_t fs) {
    if (odr > 10u || fs > 3u) return IMU_EINVAL;
    uint8_t v = (uint8_t)(odr << 4 | fs << 2), back = 0;
    int rc = imu_wr(imu, R_CTRL1, &v, 1);
    if (rc != IMU_OK) return rc;
    rc = imu_rd(imu, R_CTRL1, &back, 1);
    if (rc != IMU_OK) return rc;
    if (back != v) return IMU_EIO;
    imu->odr = odr; imu->fs = fs;
    return IMU_OK;
}

/* ---- Part A/B: STATUS + XYZ 7바이트 burst 한 번 → mg. 실패하면 mg 를 건드리지 않는다 ---- */
static int imu_read_accel_mg(imu_t *imu, int32_t mg[3]) {
    if (!imu || !mg || !imu->ready) return IMU_EINVAL;
    uint8_t b[7];
    int rc = imu_rd(imu, R_STATUS, b, sizeof b);
    if (rc != IMU_OK) return rc;                       /* b 는 일부만 채워졌을 수 있음 → 버린다 */
    if (b[0] == 0xFFu) return IMU_ENOTFOUND;           /* 예약 비트까지 1: SPI MISO 가 떠 있다는 신호 */
    if (!(b[0] & STATUS_XLDA)) return IMU_ENOTREADY;
    int32_t out[3];
    for (int k = 0; k < 3; k++) {
        uint32_t u = (uint32_t)b[1 + 2 * k] | (uint32_t)b[2 + 2 * k] << 8;
        int32_t raw = (int32_t)u - ((u & 0x8000u) ? 65536 : 0);
        out[k] = raw * (int32_t)(2000 << imu->fs) / 32768;
    }
    memcpy(mg, out, sizeof out);                       /* 세 축을 한꺼번에 커밋 */
    return IMU_OK;
}

/* ---- Part C: probe → SW_RESET → 완료 폴링 → 설정 + 되읽기. 성공해야만 ready ---- */
static int imu_init(imu_t *imu, bus_t bus, uint8_t odr, uint8_t fs) {
    imu->bus = bus;
    imu->ready = false;
    int rc = imu_probe(imu);
    if (rc != IMU_OK) return rc;
    uint8_t v = CTRL3_SW_RST;
    rc = imu_wr(imu, R_CTRL3, &v, 1);
    if (rc != IMU_OK) return rc;
    unsigned i;
    for (i = 0; i < IMU_RESET_POLLS; i++) {
        rc = imu_rd(imu, R_CTRL3, &v, 1);
        if (rc != IMU_OK) return rc;
        if (!(v & CTRL3_SW_RST)) break;
    }
    if (i == IMU_RESET_POLLS) return IMU_ETIMEDOUT;
    rc = imu_config(imu, odr, fs);
    if (rc != IMU_OK) return rc;
    imu->ready = true;
    return IMU_OK;
}

static int imu_recover_bus(imu_t *imu) {                /* 주어짐: 버스가 recover 를 지원할 때만 */
    if (!imu->bus.ops->recover) return IMU_ENOTSUP;
    return imu_map_err(imu->bus.ops->recover(imu->bus.ctx));
}

/* ================= 6. 애플리케이션 (정책: 언제 복구/재초기화/포기) ================= */
enum { APP_INIT, APP_RUN, APP_ABSENT };
#define APP_FAIL_LIMIT   3u   /* 연속 실패 3번 → 재초기화 */
#define APP_PROBE_PERIOD 4u   /* 칩 없음 → 4 스텝마다 다시 probe */
typedef struct {
    imu_t    imu;
    bus_t    bus;
    uint8_t  odr, fs;
    int      state;
    unsigned step, next_try, fails;
    unsigned samples, notready, recoveries, reinits;
    int32_t  mg[3];
    Log     *log;
} app_t;

GIVEN static void app_try_recover(app_t *a) {                 /* 주어짐 */
    int rc = imu_recover_bus(&a->imu);
    if (rc == IMU_ENOTSUP) { log_printf(a->log, "      (%s: no bus recovery op)\n", a->bus.ops->name); return; }
    a->recoveries++;
    log_printf(a->log, "      bus recover -> %s\n", imu_strerror(rc));
}

/* ---- Part C: 1 스텝 (예: 1 ms 주기 메인 루프에서 호출) ---- */
static void app_step(app_t *a) {
    unsigned t = a->step++;
    if (a->state == APP_ABSENT) {
        if (t < a->next_try) return;
        a->state = APP_INIT;
    }
    if (a->state == APP_INIT) {
        int rc = imu_init(&a->imu, a->bus, a->odr, a->fs);
        if (rc == IMU_OK) {
            a->state = APP_RUN; a->fails = 0;
            log_printf(a->log, "t=%-3u init OK on %s\n", t, a->bus.ops->name);
            return;
        }
        a->fails++;
        if (rc == IMU_ENOTFOUND || rc == IMU_EBADID || a->fails >= APP_FAIL_LIMIT) {
            a->state = APP_ABSENT; a->next_try = t + APP_PROBE_PERIOD; a->fails = 0;
            log_printf(a->log, "t=%-3u init %s -> absent, retry at t=%u\n", t, imu_strerror(rc), a->next_try);
            return;
        }
        log_printf(a->log, "t=%-3u init %s\n", t, imu_strerror(rc));
        if (rc == IMU_ETIMEDOUT) app_try_recover(a);
        return;
    }
    int rc = imu_read_accel_mg(&a->imu, a->mg);
    if (rc == IMU_OK) { a->samples++; a->fails = 0; return; }
    if (rc == IMU_ENOTREADY) { a->notready++; return; }
    if (rc == IMU_ENOTFOUND) {
        a->state = APP_ABSENT; a->next_try = t + APP_PROBE_PERIOD; a->fails = 0;
        log_printf(a->log, "t=%-3u read %s -> absent, retry at t=%u\n", t, imu_strerror(rc), a->next_try);
        return;
    }
    a->fails++;
    log_printf(a->log, "t=%-3u read %s (%u/%u)\n", t, imu_strerror(rc), a->fails, APP_FAIL_LIMIT);
    if (rc == IMU_ETIMEDOUT) app_try_recover(a);
    if (a->fails >= APP_FAIL_LIMIT) {
        a->state = APP_INIT; a->reinits++; a->fails = 0;
        log_printf(a->log, "t=%-3u -> reinit\n", t);
    }
}

/* ================= 테스트 하네스 ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* 시험대: 같은 칩 모델 두 개 + 각 버스 HAL + 어댑터 ctx */
static Log     g_wire;
static ImuCore g_core_i, g_core_s;
static I2cHal  g_i2c;
static SpiHal  g_spi;
static I2cDev  g_i2c_dev;
static SpiDev  g_spi_dev;
static void rig_reset(void) {
    core_init(&g_core_i); core_init(&g_core_s);
    memset(&g_i2c, 0, sizeof g_i2c); g_i2c.dev = &g_core_i; g_i2c.dev_addr = IMU_I2C_ADDR; g_i2c.log = &g_wire;
    memset(&g_spi, 0, sizeof g_spi); g_spi.dev = &g_core_s; g_spi.junk = 0x5Au; g_spi.log = &g_wire;
    g_i2c_dev.hal = &g_i2c; g_i2c_dev.addr7 = IMU_I2C_ADDR;
    g_spi_dev.hal = &g_spi; g_spi_dev.read_flag = 0x80u; g_spi_dev.inc_flag = 0x40u;
    log_clear(&g_wire);
}
static bus_t bus_i2c(void) { bus_t b = { &I2C_BUS_OPS, &g_i2c_dev }; return b; }
static bus_t bus_spi(void) { bus_t b = { &SPI_BUS_OPS, &g_spi_dev }; return b; }
static ImuCore *core_of(int spi) { return spi ? &g_core_s : &g_core_i; }
static bool lines_idle(void) { return !g_spi.cs_low && !g_i2c.in_txn; }
static void show_wire(const char *label) { printf("%s%s", label, g_wire.len ? g_wire.s : "(no bus activity)\n"); }
static int32_t ref_mg(int16_t raw, uint8_t fs) {
    int64_t p = (int64_t)raw * (2000LL << fs);
    return (int32_t)(p < 0 ? -((-p) / 32768) : p / 32768);
}

/* 같은 대본을 버스만 바꿔 돌리고, 드라이버가 내놓는 결과를 한 줄씩 기록 */
static void run_session(bus_t bus, ImuCore *core, Log *out) {
    static imu_t imu;
    memset(&imu, 0, sizeof imu);
    int32_t mg[3] = { 0, 0, 0 };
    static const int16_t S[5][3] = { { 16384, -16384, 0 }, { 1, -1, 32767 }, { 0, 0, 0 }, { -32768, 12345, -2 }, { 100, 200, 300 } };
    log_printf(out, "init=%s\n", imu_strerror(imu_init(&imu, bus, 4, 0)));
    for (int i = 0; i < 5; i++) {
        if (i != 2) core_set_sample(core, S[i][0], S[i][1], S[i][2]);
        int rc = imu_read_accel_mg(&imu, mg);
        log_printf(out, "accel=%s %d %d %d\n", imu_strerror(rc), (int)mg[0], (int)mg[1], (int)mg[2]);
    }
    log_printf(out, "config(4,3)=%s\n", imu_strerror(imu_config(&imu, 4, 3)));
    core_set_sample(core, 32767, -32768, 1);
    int rc = imu_read_accel_mg(&imu, mg);
    log_printf(out, "accel=%s %d %d %d\n", imu_strerror(rc), (int)mg[0], (int)mg[1], (int)mg[2]);
    log_printf(out, "config(11,0)=%s probe=%s\n", imu_strerror(imu_config(&imu, 11, 0)), imu_strerror(imu_probe(&imu)));
}

/* Part B 장애 표: 한 줄 = (버스, 장애, 동작) → 기대하는 HAL / BUS / IMU 코드와 버스 호출 수 */
enum { OP_PROBE, OP_CONFIG, OP_ACCEL, OP_INIT };
enum { DV_OK, DV_ABSENT, DV_WRONG_ID, DV_WP, DV_RESET_STUCK, DV_STUCK_BUS };
typedef struct {
    const char *name; int spi; int fkind, at, count; int dv; int op;
    int exp_hal, exp_bus, exp_drv; unsigned exp_att;
} FaultCase;
static const FaultCase FC[] = {
    { "i2c addr NACK (1x)",     0, F_NACK_ADDR, -1, 1, DV_OK,          OP_PROBE,  I2C_HAL_NACK_ADDR, BUS_ENODEV,    IMU_ENOTFOUND, 1 },
    { "i2c no device",          0, F_NONE,       0, 0, DV_ABSENT,      OP_PROBE,  I2C_HAL_NACK_ADDR, BUS_ENODEV,    IMU_ENOTFOUND, 1 },
    { "i2c data NACK @1",       0, F_NACK_DATA,  1, 1, DV_OK,          OP_CONFIG, I2C_HAL_NACK_DATA, BUS_EIO,       IMU_EIO,       1 },
    { "i2c ARLO (1x)",          0, F_ARLO,      -1, 1, DV_OK,          OP_ACCEL,  I2C_HAL_OK,        BUS_OK,        IMU_OK,        2 },
    { "i2c ARLO (always)",      0, F_ARLO,      -1,-1, DV_OK,          OP_ACCEL,  I2C_HAL_ARLO,      BUS_EAGAIN,    IMU_EBUSY,     3 },
    { "i2c timeout mid-burst",  0, F_TIMEOUT,    4, 1, DV_OK,          OP_ACCEL,  I2C_HAL_TIMEOUT,   BUS_ETIMEDOUT, IMU_ETIMEDOUT, 1 },
    { "i2c SDA stuck",          0, F_NONE,       0, 0, DV_STUCK_BUS,   OP_ACCEL,  I2C_HAL_TIMEOUT,   BUS_ETIMEDOUT, IMU_ETIMEDOUT, 1 },
    { "i2c wrong chip 0x33",    0, F_NONE,       0, 0, DV_WRONG_ID,    OP_PROBE,  I2C_HAL_OK,        BUS_OK,        IMU_EBADID,    1 },
    { "i2c write-protected",    0, F_NONE,       0, 0, DV_WP,          OP_INIT,   I2C_HAL_OK,        BUS_OK,        IMU_EIO,       5 },
    { "i2c reset never ends",   0, F_NONE,       0, 0, DV_RESET_STUCK, OP_INIT,   I2C_HAL_OK,        BUS_OK,        IMU_ETIMEDOUT, 7 },
    { "spi no device (0xFF)",   1, F_NONE,       0, 0, DV_ABSENT,      OP_PROBE,  SPI_HAL_OK,        BUS_OK,        IMU_ENOTFOUND, 1 },
    { "spi MISO stuck low",     1, F_MISO_LOW,   0, 0, DV_OK,          OP_PROBE,  SPI_HAL_OK,        BUS_OK,        IMU_ENOTFOUND, 1 },
    { "spi MISO open (run)",    1, F_MISO_FLOAT, 0, 0, DV_OK,          OP_ACCEL,  SPI_HAL_OK,        BUS_OK,        IMU_ENOTFOUND, 1 },
    { "spi timeout mid-burst",  1, F_TIMEOUT,    3, 1, DV_OK,          OP_ACCEL,  SPI_HAL_TIMEOUT,   BUS_ETIMEDOUT, IMU_ETIMEDOUT, 1 },
    { "spi timeout on write",   1, F_TIMEOUT,    1, 1, DV_OK,          OP_CONFIG, SPI_HAL_TIMEOUT,   BUS_ETIMEDOUT, IMU_ETIMEDOUT, 1 },
    { "spi wrong chip 0x33",    1, F_NONE,       0, 0, DV_WRONG_ID,    OP_PROBE,  SPI_HAL_OK,        BUS_OK,        IMU_EBADID,    1 },
    { "spi write-protected",    1, F_NONE,       0, 0, DV_WP,          OP_INIT,   SPI_HAL_OK,        BUS_OK,        IMU_EIO,       5 },
    { "spi reset never ends",   1, F_NONE,       0, 0, DV_RESET_STUCK, OP_INIT,   SPI_HAL_OK,        BUS_OK,        IMU_ETIMEDOUT, 7 },
};
static int run_op(imu_t *imu, bus_t bus, int op, int32_t mg[3]) {
    switch (op) {
    case OP_PROBE:  imu->bus = bus; return imu_probe(imu);
    case OP_CONFIG: return imu_config(imu, 5, 1);
    case OP_ACCEL:  return imu_read_accel_mg(imu, mg);
    default:        return imu_init(imu, bus, 4, 0);
    }
}

int main(void) {
    uint32_t seed = 0xC0FFEE12u;
    static imu_t imu;
    uint8_t buf[40];

    /* ---------- 0. 주어진 mock 자체 점검 (HAL 직접) ---------- */
    rig_reset();
    {
        uint8_t reg = R_WHO, id = 0, tx[2] = { 0x8F, 0x00 }, rx[2];
        CHECK(i2c_hal_xfer(&g_i2c, IMU_I2C_ADDR, &reg, 1, &id, 1) == I2C_HAL_OK && id == IMU_ID);
        CHECK(strcmp(g_wire.s, "S D4 A 0F A Sr D5 A B5 N P\n") == 0);
        spi_hal_cs(&g_spi, 0); spi_hal_xfer(&g_spi, tx, rx, 2); spi_hal_cs(&g_spi, 1);
        CHECK(rx[1] == IMU_ID && !g_spi.cs_low);
        CHECK(i2c_hal_xfer(&g_i2c, 0x50, &reg, 1, &id, 1) == I2C_HAL_NACK_ADDR && !g_i2c.in_txn);
    }

    /* ---------- Part A: 어댑터 — 와이어 바이트와 읽기/쓰기 ---------- */
    rig_reset();
    CHECK(i2c_read_regs(&g_i2c_dev, R_WHO, buf, 1) == BUS_OK && buf[0] == IMU_ID);
    CHECK(strcmp(g_wire.s, "S D4 A 0F A Sr D5 A B5 N P\n") == 0);
    show_wire("i2c read WHO_AM_I : ");
    log_clear(&g_wire);
    CHECK(spi_read_regs(&g_spi_dev, R_WHO, buf, 1) == BUS_OK && buf[0] == IMU_ID);
    CHECK(strncmp(g_wire.s, "[ 8F/", 5) == 0 && strstr(g_wire.s, " 00/B5 ]") != NULL);
    show_wire("spi read WHO_AM_I : ");
    core_set_sample(&g_core_i, 1, -2, 300); core_set_sample(&g_core_s, 1, -2, 300);
    log_clear(&g_wire);
    CHECK(i2c_read_regs(&g_i2c_dev, R_OUTX_L, buf, 6) == BUS_OK && buf[0] == 0x01 && buf[3] == 0xFF && buf[4] == 0x2C);
    show_wire("i2c read 6 @0x28  : ");
    log_clear(&g_wire);
    CHECK(spi_read_regs(&g_spi_dev, R_OUTX_L, buf, 6) == BUS_OK && buf[0] == 0x01 && buf[3] == 0xFF && buf[4] == 0x2C);
    CHECK(strncmp(g_wire.s, "[ E8/", 5) == 0);                 /* 0x80 | 0x40 (MS) | 0x28 */
    show_wire("spi read 6 @0x28  : ");
    {
        uint8_t w[3] = { 0x11, 0x22, 0x33 };
        log_clear(&g_wire);
        CHECK(i2c_write_regs(&g_i2c_dev, 0x02, w, 3) == BUS_OK && g_core_i.regs[2] == 0x11 && g_core_i.regs[4] == 0x33);
        CHECK(strcmp(g_wire.s, "S D4 A 02 A 11 A 22 A 33 A P\n") == 0);
        show_wire("i2c write 3 @0x02 : ");
        log_clear(&g_wire);
        CHECK(spi_write_regs(&g_spi_dev, 0x02, w, 3) == BUS_OK && g_core_s.regs[2] == 0x11 && g_core_s.regs[4] == 0x33);
        CHECK(strncmp(g_wire.s, "[ 42/", 5) == 0);             /* 쓰기: bit7 = 0, MS = 1 */
        show_wire("spi write 3 @0x02 : ");
        log_clear(&g_wire);
        CHECK(spi_write_regs(&g_spi_dev, R_CTRL2, w, 1) == BUS_OK && strncmp(g_wire.s, "[ 11/", 5) == 0);
    }
    /* 인자 오류: BUS_EINVAL, 선은 안 건드림 */
    {
        unsigned t0 = g_i2c.txns, f0 = g_spi.frames;
        CHECK(i2c_read_regs(&g_i2c_dev, 0, buf, 0) == BUS_EINVAL);
        CHECK(i2c_read_regs(&g_i2c_dev, 0, buf, 33) == BUS_EINVAL);
        CHECK(i2c_write_regs(&g_i2c_dev, 0, NULL, 1) == BUS_EINVAL);
        CHECK(spi_read_regs(&g_spi_dev, 0x40, buf, 1) == BUS_EINVAL);
        CHECK(spi_read_regs(&g_spi_dev, 0x3E, buf, 3) == BUS_EINVAL);
        CHECK(spi_write_regs(&g_spi_dev, 0x00, buf, 33) == BUS_EINVAL);
        CHECK(g_i2c.txns == t0 && g_spi.frames == f0);
    }
    /* 전수: 쓰기 가능한 레지스터 11개 × 값 256 × 버스 2 — 쓰고 되읽기 */
    {
        int bad = 0;
        static const uint8_t RW[] = { 0, 1, 2, 3, 4, 5, 6, 7, R_CTRL1, R_CTRL2 };
        for (int spi = 0; spi < 2; spi++) {
            rig_reset();
            bus_t b = spi ? bus_spi() : bus_i2c();
            for (size_t r = 0; r < sizeof RW; r++)
                for (int v = 0; v < 256; v++) {
                    uint8_t x = (uint8_t)v, y = 0;
                    log_clear(&g_wire);
                    if (b.ops->write_regs(b.ctx, RW[r], &x, 1) != BUS_OK) { bad++; continue; }
                    if (b.ops->read_regs(b.ctx, RW[r], &y, 1) != BUS_OK || y != x || core_of(spi)->regs[RW[r]] != x) bad++;
                }
        }
        CHECK(bad == 0);
    }
    /* 랜덤 burst: 3000 × 버스 2 — 읽은 바이트 == 읽기 직전 레지스터 스냅샷 */
    {
        int bad = 0;
        for (int spi = 0; spi < 2; spi++) {
            rig_reset();
            bus_t b = spi ? bus_spi() : bus_i2c();
            ImuCore *c = core_of(spi);
            for (int t = 0; t < 3000; t++) {
                for (int a = 0; a < 8; a++) c->regs[a] = (uint8_t)xs32(&seed);
                for (int a = R_STATUS; a < R_OUTX_L + 6; a++) c->regs[a] = (uint8_t)xs32(&seed);
                size_t n = 1 + xs32(&seed) % BUS_MAX_XFER;
                uint8_t reg = (uint8_t)(xs32(&seed) % (0x41u - n));
                uint8_t snap[64];
                memcpy(snap, c->regs, sizeof snap);
                log_clear(&g_wire);
                if (b.ops->read_regs(b.ctx, reg, buf, n) != BUS_OK || memcmp(buf, snap + reg, n) != 0) bad++;
            }
        }
        CHECK(bad == 0);
    }

    /* ---------- Part A: 같은 드라이버, 같은 대본 → 두 버스에서 같은 결과 ---------- */
    {
        static Log out_i, out_s;
        log_clear(&out_i); log_clear(&out_s);
        rig_reset(); run_session(bus_i2c(), &g_core_i, &out_i);
        size_t wire_i = g_wire.len;
        rig_reset(); run_session(bus_spi(), &g_core_s, &out_s);
        CHECK(out_i.len > 0 && strcmp(out_i.s, out_s.s) == 0);
        CHECK(strstr(out_i.s, "init=OK\naccel=OK 1000 -1000 0\n") != NULL);
        CHECK(strstr(out_i.s, "accel=IMU_ENOTREADY 0 0 1999\n") != NULL);  /* 새 샘플 없음 → 이전 값 유지 */
        CHECK(strstr(out_i.s, "config(11,0)=IMU_EINVAL probe=OK") != NULL);
        CHECK(wire_i != g_wire.len);                                        /* 와이어는 다르다 */
        printf("driver output (identical on i2c and spi):\n%s", out_i.s);
    }
    /* mg 변환: 랜덤 2000 샘플 × fs 4개 × 버스 2 */
    {
        int bad = 0;
        for (int spi = 0; spi < 2; spi++)
            for (uint8_t fs = 0; fs < 4; fs++) {
                rig_reset();
                memset(&imu, 0, sizeof imu);
                if (imu_init(&imu, spi ? bus_spi() : bus_i2c(), 4, fs) != IMU_OK) { bad++; continue; }
                for (int t = 0; t < 500; t++) {
                    int16_t v[3]; int32_t mg[3];
                    for (int k = 0; k < 3; k++) v[k] = (int16_t)((int32_t)(xs32(&seed) & 0xFFFFu) - 32768);
                    core_set_sample(core_of(spi), v[0], v[1], v[2]);
                    log_clear(&g_wire);
                    if (imu_read_accel_mg(&imu, mg) != IMU_OK) { bad++; continue; }
                    for (int k = 0; k < 3; k++) if (mg[k] != ref_mg(v[k], fs)) bad++;
                }
            }
        CHECK(bad == 0);
    }

    /* ---------- Part B: 번역 함수 — 전사성과 도메인 ---------- */
    {
        int bad = 0;
        for (int st = 0; st < 256; st++) {
            if (!is_bus_code(i2c_map_err(st)) || !is_bus_code(spi_map_err(st))) bad++;
            if (st != 0 && (i2c_map_err(st) == BUS_OK || spi_map_err(st) == BUS_OK)) bad++;   /* 모르는 실패를 성공으로 바꾸면 안 됨 */
        }
        for (int rc = -10; rc <= 0; rc++) if (!is_imu_code(imu_map_err(rc))) bad++;
        CHECK(bad == 0);
        CHECK(i2c_map_err(I2C_HAL_NACK_ADDR) == BUS_ENODEV && i2c_map_err(I2C_HAL_NACK_DATA) == BUS_EIO);
        CHECK(i2c_map_err(I2C_HAL_ARLO) == BUS_EAGAIN && i2c_map_err(I2C_HAL_TIMEOUT) == BUS_ETIMEDOUT);
        CHECK(spi_map_err(SPI_HAL_TIMEOUT) == BUS_ETIMEDOUT && spi_map_err(0x7F) == BUS_EIO);
        CHECK(imu_map_err(BUS_ENODEV) == IMU_ENOTFOUND && imu_map_err(BUS_EAGAIN) == IMU_EBUSY);
        CHECK(imu_map_err(BUS_ETIMEDOUT) == IMU_ETIMEDOUT && imu_map_err(BUS_EINVAL) == IMU_EINVAL && imu_map_err(-77) == IMU_EIO);
    }

    /* ---------- Part B: 장애 표 — HAL → BUS → IMU 가 계층마다 문서대로 번역되는가 ---------- */
    printf("%-24s %-15s %-14s %-14s att\n", "fault", "HAL", "bus", "driver");
    for (size_t i = 0; i < sizeof FC / sizeof FC[0]; i++) {
        const FaultCase *fc = &FC[i];
        rig_reset();
        memset(&imu, 0, sizeof imu);
        bus_t b = fc->spi ? bus_spi() : bus_i2c();
        ImuCore *c = core_of(fc->spi);
        int32_t mg[3] = { 777, 777, 777 };
        if (fc->op == OP_CONFIG || fc->op == OP_ACCEL) {       /* 정상 초기화 뒤에 장애 */
            CHECK(imu_init(&imu, b, 4, 0) == IMU_OK);
            core_set_sample(c, 1000, 2000, 3000);
        }
        if (fc->dv == DV_ABSENT) { if (fc->spi) g_spi.dev = NULL; else g_i2c.dev = NULL; }
        if (fc->dv == DV_WRONG_ID) { c->who = 0x33; core_defaults(c); }
        if (fc->dv == DV_WP) c->write_protect = true;
        if (fc->dv == DV_RESET_STUCK) c->reset_polls = 99;
        if (fc->dv == DV_STUCK_BUS) g_i2c.stuck = true;
        Fault f = { fc->fkind, fc->at, fc->count };
        if (fc->spi) g_spi.f = f; else g_i2c.f = f;
        unsigned a0 = imu.attempts;
        log_clear(&g_wire);
        int rc = run_op(&imu, b, fc->op, mg);
        int hal = fc->spi ? g_spi.last_status : g_i2c.last_status;
        printf("%-24s %-15s %-14s %-14s %u\n", fc->name, hal_strerror(hal), bus_strerror(imu.last_bus_err), imu_strerror(rc), imu.attempts - a0);
        CHECK(hal == fc->exp_hal);
        CHECK(imu.last_bus_err == fc->exp_bus);
        CHECK(rc == fc->exp_drv);
        CHECK(imu.attempts - a0 == fc->exp_att);
        CHECK(lines_idle());                                     /* CS 올라가 있고 I2C 는 STOP 뒤 */
        if (fc->op == OP_ACCEL && rc != IMU_OK) CHECK(mg[0] == 777 && mg[1] == 777 && mg[2] == 777);
        if (fc->op == OP_ACCEL && rc == IMU_OK) CHECK(mg[0] == 61 && mg[1] == 122 && mg[2] == 183);
        if (fc->op == OP_INIT) CHECK(!imu.ready);
        if (i == 5 || i == 13) show_wire("    wire: ");
    }
    /* 일시적 ARLO 2번 → 세 번째 시도에 성공, 앱은 모른다 */
    {
        rig_reset(); memset(&imu, 0, sizeof imu);
        CHECK(imu_init(&imu, bus_i2c(), 4, 0) == IMU_OK);
        core_set_sample(&g_core_i, 5, 6, 7);
        g_i2c.f = (Fault){ F_ARLO, -1, 2 };
        unsigned a0 = imu.attempts, r0 = imu.retries;
        int32_t mg[3];
        CHECK(imu_read_accel_mg(&imu, mg) == IMU_OK && imu.attempts - a0 == 3 && imu.retries - r0 == 2);
        /* 타임아웃은 재시도하지 않는다 (정책): 두 번 연속 걸리게 해도 시도는 1 */
        g_i2c.f = (Fault){ F_TIMEOUT, 0, 2 };
        a0 = imu.attempts;
        CHECK(imu_read_accel_mg(&imu, mg) == IMU_ETIMEDOUT && imu.attempts - a0 == 1);
    }
    /* 랜덤 장애 퍼징: 4000번 — 코드 도메인, 선 상태, 실패 시 mg 불변, 성공 시 값 정확 */
    {
        int bad_dom = 0, bad_idle = 0, bad_mg = 0, bad_val = 0, bad_att = 0;
        for (int t = 0; t < 4000; t++) {
            int spi = (int)(xs32(&seed) & 1u);
            rig_reset(); memset(&imu, 0, sizeof imu);
            bus_t b = spi ? bus_spi() : bus_i2c();
            ImuCore *c = core_of(spi);
            int16_t v[3];
            for (int k = 0; k < 3; k++) v[k] = (int16_t)((int32_t)(xs32(&seed) & 0xFFFFu) - 32768);
            if (imu_init(&imu, b, 4, (uint8_t)(xs32(&seed) & 3u)) != IMU_OK) { bad_val++; continue; }
            core_set_sample(c, v[0], v[1], v[2]);
            static const int KI[] = { F_NONE, F_NACK_ADDR, F_NACK_DATA, F_ARLO, F_TIMEOUT };
            static const int KS[] = { F_NONE, F_TIMEOUT, F_MISO_FLOAT, F_MISO_LOW };
            Fault f = { spi ? KS[xs32(&seed) % 4] : KI[xs32(&seed) % 5], (int)(xs32(&seed) % 9), (int)(xs32(&seed) % 4) };
            if (spi) g_spi.f = f; else g_i2c.f = f;
            unsigned a0 = imu.attempts;
            int32_t mg[3] = { 777, 777, 777 };
            int op = (int)(xs32(&seed) % 3);
            int rc;
            if (op == 0) rc = imu_read_accel_mg(&imu, mg);
            else if (op == 1) rc = imu_config(&imu, (uint8_t)(xs32(&seed) % 12), (uint8_t)(xs32(&seed) % 4));
            else {
                size_t n = xs32(&seed) % 34;
                int brc = b.ops->read_regs(b.ctx, (uint8_t)(xs32(&seed) % 0x48), buf, n);
                if (!is_bus_code(brc)) bad_dom++;
                rc = IMU_OK;
            }
            if (!is_imu_code(rc)) bad_dom++;
            if (!lines_idle()) bad_idle++;
            if (imu.attempts - a0 > 2 * IMU_MAX_ATTEMPTS) bad_att++;
            if (op == 0 && rc != IMU_OK && (mg[0] != 777 || mg[1] != 777 || mg[2] != 777)) bad_mg++;
            if (op == 0 && rc == IMU_OK)
                for (int k = 0; k < 3; k++) if (mg[k] != ref_mg(v[k], imu.fs)) bad_val++;
        }
        CHECK(bad_dom == 0); CHECK(bad_idle == 0); CHECK(bad_mg == 0); CHECK(bad_val == 0); CHECK(bad_att == 0);
    }
    /* 드라이버 인자 오류: 버스를 안 건드림 */
    {
        rig_reset(); memset(&imu, 0, sizeof imu);
        int32_t mg[3];
        imu.bus = bus_i2c();
        CHECK(imu_read_accel_mg(&imu, mg) == IMU_EINVAL);        /* init 전 */
        CHECK(imu_config(&imu, 11, 0) == IMU_EINVAL && imu_config(&imu, 0, 4) == IMU_EINVAL);
        CHECK(g_i2c.txns == 0 && imu.attempts == 0);
        CHECK(imu_init(&imu, bus_i2c(), 4, 0) == IMU_OK && imu_read_accel_mg(&imu, NULL) == IMU_EINVAL);
    }

    /* ---------- Part C: init 와이어 로그 + 리셋 폴링 경계 ---------- */
    rig_reset(); memset(&imu, 0, sizeof imu);
    g_core_i.reset_polls = 2;
    CHECK(imu_init(&imu, bus_i2c(), 4, 1) == IMU_OK && imu.ready && g_core_i.regs[R_CTRL1] == 0x44 && g_core_i.resets == 1);
    printf("i2c imu_init wire log:\n%s", g_wire.s);
    rig_reset(); memset(&imu, 0, sizeof imu);
    g_core_s.reset_polls = 2;
    CHECK(imu_init(&imu, bus_spi(), 4, 1) == IMU_OK && imu.ready && g_core_s.regs[R_CTRL1] == 0x44);
    printf("spi imu_init wire log:\n%s", g_wire.s);
    for (unsigned rp = 0; rp <= 6; rp++) {
        rig_reset(); memset(&imu, 0, sizeof imu);
        g_core_s.reset_polls = rp;
        int rc = imu_init(&imu, bus_spi(), 4, 0);
        CHECK(rc == (rp <= IMU_RESET_POLLS - 1 ? IMU_OK : IMU_ETIMEDOUT));
    }
    /* 설정 쓰기 도중 data NACK → init 실패, ready 아님 */
    rig_reset(); memset(&imu, 0, sizeof imu);
    {
        g_i2c.f = (Fault){ F_NACK_DATA, 1, 1 };   /* 첫 데이터 바이트 쓰기 = SW_RESET 쓰기 */
        CHECK(imu_init(&imu, bus_i2c(), 4, 0) == IMU_EIO && !imu.ready && g_core_i.resets == 0);
    }

    /* ---------- Part C: 앱 정책 시나리오 (I2C) ---------- */
    {
        static Log alog;
        static app_t app;
        log_clear(&alog);
        rig_reset();
        memset(&app, 0, sizeof app);
        app.bus = bus_i2c(); app.odr = 4; app.fs = 0; app.log = &alog;
        g_i2c.dev = NULL;                                  /* 부팅 때 센서 아직 없음 */
        for (unsigned t = 0; t < 40; t++) {
            if (t == 6) g_i2c.dev = &g_core_i;             /* 센서 전원 들어옴 */
            if (t >= 8 && t != 20) core_set_sample(&g_core_i, 1000, 0, -1000);
            if (t == 14) g_i2c.stuck = true;               /* SDA stuck → 타임아웃 → 버스 복구 */
            if (t == 17) g_i2c.f = (Fault){ F_ARLO, -1, 2 };          /* 앱에는 안 보여야 함 */
            if (t == 22) g_i2c.f = (Fault){ F_TIMEOUT, 2, 3 };        /* 3번 연속 타임아웃 → 재초기화 */
            log_clear(&g_wire);
            app_step(&app);
        }
        printf("app log (i2c):\n%s", alog.s);
        CHECK(strstr(alog.s, "t=0   init IMU_ENOTFOUND -> absent, retry at t=4\n") != NULL);
        CHECK(strstr(alog.s, "t=8   init OK on i2c\n") != NULL);
        CHECK(app.recoveries == 4 && g_i2c.recoveries == 4 && app.reinits == 1);
        CHECK(app.state == APP_RUN && app.notready == 1);
        CHECK(app.imu.retries == 2);                      /* ARLO 2번은 드라이버 안에서 끝남 */
        CHECK(strstr(alog.s, "ARLO") == NULL && strstr(alog.s, "BUSY") == NULL);
        CHECK(app.mg[0] == 61 && app.mg[2] == -61);
        CHECK(app.samples == 40 - 9 - 1 - 1 - 3 - 1);
    }
    /* ---------- Part C: 앱 정책 시나리오 (SPI: 복구 op 없음, MISO 끊김) ---------- */
    {
        static Log alog;
        static app_t app;
        log_clear(&alog);
        rig_reset();
        memset(&app, 0, sizeof app);
        app.bus = bus_spi(); app.odr = 4; app.fs = 1; app.log = &alog;
        for (unsigned t = 0; t < 30; t++) {
            if (t != 5) core_set_sample(&g_core_s, 1000, 0, -1000);
            if (t == 3) g_spi.f = (Fault){ F_TIMEOUT, 2, 3 };
            if (t == 12) g_spi.f = (Fault){ F_MISO_FLOAT, 0, 0 };   /* 커넥터 빠짐 */
            if (t == 20) g_spi.f = (Fault){ F_NONE, 0, 0 };          /* 다시 꽂힘 */
            log_clear(&g_wire);
            app_step(&app);
        }
        printf("app log (spi):\n%s", alog.s);
        CHECK(strstr(alog.s, "no bus recovery op") != NULL && app.recoveries == 0);
        CHECK(app.reinits == 1 && app.state == APP_RUN);
        CHECK(strstr(alog.s, "t=12  read IMU_ENOTFOUND -> absent") != NULL);
        CHECK(app.mg[0] == 122 && app.mg[2] == -122);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
