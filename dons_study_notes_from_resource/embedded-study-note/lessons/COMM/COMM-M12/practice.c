/* COMM-M12 추가 연습 — 한 번에 검증
 * 모델(MIMU-6D, HAL, bus_ops, 드라이버, 앱)은 solution.c 와 같다. 단 연습 4 를 위해 SPI 칩에 더미 바이트 옵션
 * (SpiHal.n_dummy) 과 SPI 어댑터 설정 (SpiDev.n_dummy) 을 더했다.
 *   연습 1: 재시도 증폭 — 계층마다 "실패하면 3번" 이면 트랜잭션이 몇 번? (27 vs 1)
 *   연습 2: 레지스터 캐시 데코레이터 (regmap 캐시 축약) — 적중, 쓰기 보호 칩에서 검증이 거짓 통과하는 함정, 리셋 뒤 낡은 캐시
 *   연습 3: 버스 수준 장애 주입 데코레이터 — imu_init 의 모든 버스 호출 × 모든 버스 에러 (35 경우)
 *   연습 4: 더미 바이트가 있는 SPI 칩 (BMI088 가속도계 식) — 어댑터 설정만 바꾸고 드라이버는 그대로
 *   연습 5: X-macro 로 에러 번역표 + 앱 행동을 코드에서 생성, 전사성 검사
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
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
enum { SPI_ST_CMD, SPI_ST_READ, SPI_ST_WRITE, SPI_ST_DUMMY };   /* 연습 4: DUMMY 추가 */
/* 명령 바이트 (이 mock 의 정의, ST LIS3DH 식): bit7 R/W (1 = 읽기), bit6 MS (1 = 자동 증가), bit5..0 주소.
 * 연습 4: n_dummy > 0 이면 읽기 명령 뒤 더미 바이트 n_dummy 개 (BMI088 가속도계 식). */
typedef struct {
    ImuCore *dev;       /* NULL = 이 CS 에 칩 없음 → MISO 는 풀업으로 0xFF */
    bool     cs_low;
    int      st; uint8_t addr; bool inc; int fbyte;
    uint8_t  n_dummy, dummy_left;   /* 연습 4 */
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
        if (h->st == SPI_ST_READ && h->n_dummy) { h->st = SPI_ST_DUMMY; h->dummy_left = h->n_dummy; }
        break;
    case SPI_ST_DUMMY:
        h->junk = (uint8_t)(h->junk * 5u + 0x3Bu); miso = h->junk;
        if (--h->dummy_left == 0) h->st = SPI_ST_READ;
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
typedef struct { SpiHal *hal; uint8_t read_flag, inc_flag, n_dummy; } SpiDev;       /* SPI 어댑터의 ctx (regmap 의 read_flag_mask 같은 것) */

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
    if (!d || !buf || n == 0 || n > BUS_MAX_XFER || reg > 0x3Fu || reg + n > 0x40u || d->n_dummy > 1u) return BUS_EINVAL;
    uint8_t tx[2 + BUS_MAX_XFER] = { 0 }, rx[2 + BUS_MAX_XFER];
    tx[0] = (uint8_t)(d->read_flag | (n > 1 ? d->inc_flag : 0u) | reg);
    spi_hal_cs(d->hal, 0);
    int st = spi_hal_xfer(d->hal, tx, rx, 1 + d->n_dummy + n);
    spi_hal_cs(d->hal, 1);                         /* 에러여도 CS 는 반드시 올린다 */
    if (st != SPI_HAL_OK) return spi_map_err(st);
    memcpy(buf, rx + 1 + d->n_dummy, n);           /* 연습 4: 명령 + 더미 동안의 MISO 는 버림 */                        /* 명령 바이트 동안의 MISO 는 버림 */
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

/* ================= 테스트 하네스 (solution.c 와 같은 시험대) ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

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
    g_spi_dev.hal = &g_spi; g_spi_dev.read_flag = 0x80u; g_spi_dev.inc_flag = 0x40u; g_spi_dev.n_dummy = 0;
    log_clear(&g_wire);
}
static bus_t bus_i2c(void) { bus_t b = { &I2C_BUS_OPS, &g_i2c_dev }; return b; }
static bus_t bus_spi(void) { bus_t b = { &SPI_BUS_OPS, &g_spi_dev }; return b; }

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
}

/* ================= 연습 1: 재시도 증폭 — 모든 계층이 "실패하면 3번" 이면? ================= */
typedef struct { bus_t inner; unsigned tries; } RetryBus;            /* 순진한 버스 계층: 모든 에러를 재시도 */
static int rb_read(void *ctx, uint8_t reg, uint8_t *buf, size_t n) {
    RetryBus *r = (RetryBus *)ctx;
    int rc = BUS_EIO;
    for (unsigned i = 0; i < r->tries; i++) {
        rc = r->inner.ops->read_regs(r->inner.ctx, reg, buf, n);
        if (rc == BUS_OK) break;
    }
    return rc;
}
static int rb_write(void *ctx, uint8_t reg, const uint8_t *buf, size_t n) {
    RetryBus *r = (RetryBus *)ctx;
    int rc = BUS_EIO;
    for (unsigned i = 0; i < r->tries; i++) {
        rc = r->inner.ops->write_regs(r->inner.ctx, reg, buf, n);
        if (rc == BUS_OK) break;
    }
    return rc;
}
static const bus_ops RETRY_OPS = { "retry", rb_read, rb_write, NULL };
static int naive_drv_read(bus_t b, uint8_t reg, uint8_t *buf, size_t n, unsigned tries) {   /* 순진한 드라이버 */
    int rc = BUS_EIO;
    for (unsigned i = 0; i < tries; i++) { rc = b.ops->read_regs(b.ctx, reg, buf, n); if (rc == BUS_OK) break; }
    return rc;
}
static int naive_app_read(bus_t b, uint8_t *buf, unsigned tries) {                          /* 순진한 앱 */
    int rc = BUS_EIO;
    for (unsigned i = 0; i < tries; i++) { rc = naive_drv_read(b, R_STATUS, buf, 7, tries); if (rc == BUS_OK) break; }
    return rc;
}

/* ================= 연습 2: 레지스터 캐시 데코레이터 (regmap 캐시 축약) ================= */
typedef struct { bus_t inner; uint8_t val[64]; bool valid[64]; bool bypass; unsigned wire_reads, hits; } CacheBus;
static bool cb_volatile(unsigned r) { return r == R_CTRL3 || r >= R_STATUS; }   /* 칩이 스스로 바꾸는 레지스터 */
static void cb_invalidate(CacheBus *c) { memset(c->valid, 0, sizeof c->valid); }
static int cb_read(void *ctx, uint8_t reg, uint8_t *buf, size_t n) {
    CacheBus *c = (CacheBus *)ctx;
    if (!buf || n == 0 || n > BUS_MAX_XFER || reg + n > 64u) return BUS_EINVAL;
    bool all = !c->bypass;
    for (size_t i = 0; all && i < n; i++) all = !cb_volatile(reg + i) && c->valid[reg + i];
    if (all) { memcpy(buf, c->val + reg, n); c->hits++; return BUS_OK; }
    int rc = c->inner.ops->read_regs(c->inner.ctx, reg, buf, n);
    c->wire_reads++;
    if (rc == BUS_OK)
        for (size_t i = 0; i < n; i++)
            if (!cb_volatile(reg + i)) { c->val[reg + i] = buf[i]; c->valid[reg + i] = true; }
    return rc;
}
static int cb_write(void *ctx, uint8_t reg, const uint8_t *buf, size_t n) {
    CacheBus *c = (CacheBus *)ctx;
    if (!buf || n == 0 || n > BUS_MAX_XFER || reg + n > 64u) return BUS_EINVAL;
    int rc = c->inner.ops->write_regs(c->inner.ctx, reg, buf, n);
    if (rc == BUS_OK)                                    /* write-through: 성공한 쓰기만 캐시에 반영 */
        for (size_t i = 0; i < n; i++)
            if (!cb_volatile(reg + i)) { c->val[reg + i] = buf[i]; c->valid[reg + i] = true; }
    return rc;
}
static int cb_recover(void *ctx) {
    CacheBus *c = (CacheBus *)ctx;
    return c->inner.ops->recover ? c->inner.ops->recover(c->inner.ctx) : BUS_EINVAL;
}
static const bus_ops CACHE_OPS = { "cache", cb_read, cb_write, cb_recover };

/* ================= 연습 3: 버스 수준 장애 주입 데코레이터 (테스트 이음새) ================= */
typedef struct { bus_t inner; unsigned calls, fail_at; int code; } FaultBus;
static int fb_read(void *ctx, uint8_t reg, uint8_t *buf, size_t n) {
    FaultBus *f = (FaultBus *)ctx;
    if (++f->calls == f->fail_at) return f->code;        /* 선은 건드리지 않고 실패 */
    return f->inner.ops->read_regs(f->inner.ctx, reg, buf, n);
}
static int fb_write(void *ctx, uint8_t reg, const uint8_t *buf, size_t n) {
    FaultBus *f = (FaultBus *)ctx;
    if (++f->calls == f->fail_at) return f->code;
    return f->inner.ops->write_regs(f->inner.ctx, reg, buf, n);
}
static const bus_ops FAULT_OPS = { "fault", fb_read, fb_write, NULL };

/* ================= 연습 5: X-macro 번역표 — 문서를 코드에서 생성 ================= */
#define IMU_ERR_TABLE(X) \
    X(IMU_OK,        "use the sample") \
    X(IMU_EINVAL,    "code bug: fix caller, do not retry") \
    X(IMU_ENOTFOUND, "absent: re-probe every N steps, check power/wiring/address") \
    X(IMU_EBADID,    "wrong part: check BOM / strap pins, stop") \
    X(IMU_EIO,       "count; reinit after 3 in a row") \
    X(IMU_ETIMEDOUT, "bus recovery (I2C 9 clocks), reinit after 3") \
    X(IMU_EBUSY,     "bus contended: try next cycle") \
    X(IMU_ENOTREADY, "not an error: no new sample yet") \
    X(IMU_ENOTSUP,   "feature missing: fall back")
typedef struct { int code; const char *name; const char *action; } ErrRow;
#define ROW(c, act) { c, #c, act },
static const ErrRow IMU_ERR_ROWS[] = { IMU_ERR_TABLE(ROW) };
#undef ROW
static const ErrRow *imu_err_row(int code) {
    for (size_t i = 0; i < sizeof IMU_ERR_ROWS / sizeof IMU_ERR_ROWS[0]; i++)
        if (IMU_ERR_ROWS[i].code == code) return &IMU_ERR_ROWS[i];
    return NULL;
}

int main(void) {
    static imu_t imu;
    uint8_t buf[40];

    /* ---------- 연습 1 ---------- */
    {
        rig_reset();
        static RetryBus rb;
        rb.inner = bus_i2c(); rb.tries = 3;
        bus_t naive = { &RETRY_OPS, &rb };
        g_i2c.stuck = true;
        unsigned t0 = g_i2c.txns;
        CHECK(naive_app_read(naive, buf, 3) == BUS_ETIMEDOUT);
        unsigned naive_stuck = g_i2c.txns - t0;
        CHECK(naive_stuck == 27);
        g_i2c.stuck = false;
        memset(&imu, 0, sizeof imu);
        CHECK(imu_init(&imu, bus_i2c(), 4, 0) == IMU_OK);
        g_i2c.stuck = true; t0 = g_i2c.txns;
        int32_t mg[3];
        CHECK(imu_read_accel_mg(&imu, mg) == IMU_ETIMEDOUT);
        unsigned ours_stuck = g_i2c.txns - t0;
        CHECK(ours_stuck == 1);
        g_i2c.stuck = false;
        g_i2c.f = (Fault){ F_ARLO, -1, -1 };
        t0 = g_i2c.txns;
        CHECK(naive_app_read(naive, buf, 3) == BUS_EAGAIN);
        unsigned naive_arlo = g_i2c.txns - t0;
        t0 = g_i2c.txns;
        CHECK(imu_read_accel_mg(&imu, mg) == IMU_EBUSY);
        unsigned ours_arlo = g_i2c.txns - t0;
        CHECK(naive_arlo == 27 && ours_arlo == 3);
        g_i2c.f = (Fault){ F_NONE, 0, 0 }; g_i2c.stuck = true;
        CHECK(imu_recover_bus(&imu) == IMU_OK && !g_i2c.stuck);   /* 복구는 재시도가 아니라 앱이 고르는 별도 행동 */
        imu.bus = bus_spi();
        CHECK(imu_recover_bus(&imu) == IMU_ENOTSUP);
        printf("ex1 retry amplification (stuck bus, 25 ms timeout each):\n");
        printf("    naive 3x3x3: %u transactions, %u ms blocked | ours: %u transaction, %u ms\n",
               naive_stuck, naive_stuck * 25u, ours_stuck, ours_stuck * 25u);
        printf("    arbitration always lost: naive %u, ours %u\n", naive_arlo, ours_arlo);
    }

    /* ---------- 연습 2 ---------- */
    {
        rig_reset();
        static CacheBus cb;
        memset(&cb, 0, sizeof cb);
        cb.inner = bus_i2c();
        bus_t cached = { &CACHE_OPS, &cb };
        memset(&imu, 0, sizeof imu);
        CHECK(imu_init(&imu, cached, 4, 1) == IMU_OK);
        unsigned t0 = g_i2c.txns;
        uint8_t v = 0;
        for (int i = 0; i < 10; i++) CHECK(cached.ops->read_regs(cached.ctx, R_CTRL1, &v, 1) == BUS_OK && v == 0x44);
        CHECK(g_i2c.txns == t0);                                /* 10번 모두 캐시 */
        core_set_sample(&g_core_i, 1, 2, 3);
        int32_t mg[3];
        CHECK(imu_read_accel_mg(&imu, mg) == IMU_OK && g_i2c.txns == t0 + 1);   /* STATUS/OUT 는 휘발성 → 항상 선 */
        /* (b) 함정: 쓰기 보호 칩 + 캐시 → 되읽기가 캐시에서 나와 검증 통과 */
        rig_reset(); memset(&cb, 0, sizeof cb); cb.inner = bus_i2c();
        g_core_i.write_protect = true;
        memset(&imu, 0, sizeof imu);
        int rc_bug = imu_init(&imu, cached, 4, 1);
        CHECK(rc_bug == IMU_OK && g_core_i.regs[R_CTRL1] == 0x00);    /* 버그 재현: OK 인데 칩은 0 */
        rig_reset(); memset(&cb, 0, sizeof cb); cb.inner = bus_i2c();
        g_core_i.write_protect = true;
        cb.bypass = true;                                       /* regcache_cache_bypass 같은 것 */
        memset(&imu, 0, sizeof imu);
        int rc_fix = imu_init(&imu, cached, 4, 1);
        CHECK(rc_fix == IMU_EIO);
        /* (c) 캐시 뒤에서 칩이 리셋되면 캐시는 낡는다 */
        rig_reset(); memset(&cb, 0, sizeof cb); cb.inner = bus_i2c();
        uint8_t w = 0x44;
        CHECK(cached.ops->write_regs(cached.ctx, R_CTRL1, &w, 1) == BUS_OK);
        w = CTRL3_SW_RST;
        CHECK(cached.ops->write_regs(cached.ctx, R_CTRL3, &w, 1) == BUS_OK);   /* 칩 레지스터 전부 기본값 */
        CHECK(cached.ops->read_regs(cached.ctx, R_CTRL1, &v, 1) == BUS_OK && v == 0x44 && g_core_i.regs[R_CTRL1] == 0x00);
        cb_invalidate(&cb);
        CHECK(cached.ops->read_regs(cached.ctx, R_CTRL1, &v, 1) == BUS_OK && v == 0x00);
        printf("ex2 cache: write-protected chip -> init without bypass=%s, with bypass=%s\n",
               imu_strerror(rc_bug), imu_strerror(rc_fix));
    }

    /* ---------- 연습 3: imu_init 의 모든 버스 호출 × 모든 버스 에러 ---------- */
    {
        static const int CODES[] = { BUS_EINVAL, BUS_ENODEV, BUS_EIO, BUS_ETIMEDOUT, BUS_EAGAIN };
        int bad = 0, cases = 0;
        static FaultBus fb;
        bus_t fbus = { &FAULT_OPS, &fb };
        rig_reset(); g_core_s.reset_polls = 2;
        memset(&fb, 0, sizeof fb); fb.inner = bus_spi();
        memset(&imu, 0, sizeof imu);
        CHECK(imu_init(&imu, fbus, 4, 0) == IMU_OK && fb.calls == 7);    /* probe, reset, 3 polls, cfg wr, cfg rd */
        for (unsigned k = 1; k <= 7; k++)
            for (size_t c = 0; c < sizeof CODES / sizeof CODES[0]; c++) {
                rig_reset(); g_core_s.reset_polls = 2;
                memset(&fb, 0, sizeof fb); fb.inner = bus_spi(); fb.fail_at = k; fb.code = CODES[c];
                memset(&imu, 0, sizeof imu);
                unsigned f0 = g_spi.frames;
                int rc = imu_init(&imu, fbus, 4, 0);
                cases++;
                if (CODES[c] == BUS_EAGAIN) {                   /* 한 번의 EAGAIN 은 드라이버가 삼킨다 */
                    if (rc != IMU_OK || fb.calls != 8 || !imu.ready) bad++;
                } else {
                    if (rc != imu_map_err(CODES[c]) || fb.calls != k || imu.ready) bad++;
                    if (g_spi.frames - f0 != k - 1) bad++;      /* 실패한 호출은 선을 안 건드림, 그 뒤는 멈춤 */
                }
            }
        CHECK(cases == 35 && bad == 0);
        printf("ex3 fault-injection seam: %d cases (7 calls x 5 bus errors), bad=%d\n", cases, bad);
    }

    /* ---------- 연습 4: 더미 바이트가 있는 칩 ---------- */
    {
        static Log out_i, out_s;
        rig_reset();
        g_spi.n_dummy = 1;
        memset(&imu, 0, sizeof imu);
        imu.bus = bus_spi();
        int rc0 = imu_probe(&imu);
        CHECK(rc0 != IMU_OK);
        printf("ex4 dummy chip, adapter n_dummy=0: probe=%s  wire %s", imu_strerror(rc0), g_wire.s);
        rig_reset(); g_spi.n_dummy = 1; g_spi_dev.n_dummy = 1;
        memset(&imu, 0, sizeof imu); imu.bus = bus_spi();
        CHECK(imu_probe(&imu) == IMU_OK);
        printf("ex4 dummy chip, adapter n_dummy=1: probe=OK  wire %s", g_wire.s);
        log_clear(&out_i); log_clear(&out_s);
        rig_reset(); run_session(bus_i2c(), &g_core_i, &out_i);
        rig_reset(); g_spi.n_dummy = 1; g_spi_dev.n_dummy = 1; run_session(bus_spi(), &g_core_s, &out_s);
        CHECK(out_i.len > 0 && strcmp(out_i.s, out_s.s) == 0);     /* 드라이버는 한 줄도 안 바뀜 */
        rig_reset(); g_spi_dev.n_dummy = 2;
        CHECK(spi_read_regs(&g_spi_dev, R_WHO, buf, 1) == BUS_EINVAL && g_spi.frames == 0);
    }

    /* ---------- 연습 5: 번역표 생성 + 전사성 ---------- */
    {
        int bad = 0;
        for (int st = 0; st < 256; st++) {
            if (!is_bus_code(i2c_map_err(st)) || !is_bus_code(spi_map_err(st))) bad++;
            if (!is_imu_code(imu_map_err(i2c_map_err(st))) || !imu_err_row(imu_map_err(i2c_map_err(st)))) bad++;
            if (!is_imu_code(imu_map_err(spi_map_err(st))) || !imu_err_row(imu_map_err(spi_map_err(st)))) bad++;
        }
        static const int DRV_ONLY[] = { IMU_EBADID, IMU_ENOTREADY, IMU_ENOTSUP };
        for (size_t i = 0; i < 3; i++) if (!imu_err_row(DRV_ONLY[i])) bad++;
        CHECK(bad == 0);
        CHECK(sizeof IMU_ERR_ROWS / sizeof IMU_ERR_ROWS[0] == 9);
        static const int HALS[] = { I2C_HAL_OK, I2C_HAL_NACK_ADDR, I2C_HAL_NACK_DATA, I2C_HAL_ARLO, I2C_HAL_TIMEOUT, SPI_HAL_TIMEOUT };
        printf("ex5 generated table:\n| HAL | bus | driver | app action |\n|---|---|---|---|\n");
        for (size_t i = 0; i < sizeof HALS / sizeof HALS[0]; i++) {
            int b = HALS[i] == SPI_HAL_TIMEOUT ? spi_map_err(HALS[i]) : i2c_map_err(HALS[i]);
            const ErrRow *r = imu_err_row(imu_map_err(b));
            printf("| %s | %s | %s | %s |\n", hal_strerror(HALS[i]), bus_strerror(b), r->name, r->action);
        }
        for (size_t i = 0; i < 3; i++) {
            const ErrRow *r = imu_err_row(DRV_ONLY[i]);
            printf("| (none) | (none) | %s | %s |\n", r->name, r->action);
        }
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
