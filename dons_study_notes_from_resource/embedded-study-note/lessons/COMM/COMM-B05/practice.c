/* COMM-B05 추가 연습 — 본문 solution.c 의 mock 칩·버스·드라이버를 담고 (COMM-B05 본문 축약판:
 *  버그 재현 함수·imu_read_accel_mg 는 뺐다)
 * 연습용 기능을 더했다:
 *   연습 1: LIS3DH 식 명령 바이트 (bit6 MS = 자동 증가) — Imu.ms_style
 *   연습 2: 샘플 찢김(torn read)과 BDU 흉내 — Imu.bdu (CS 프레임 동안 새 샘플 보류)
 *   연습 3: 세그먼트 메시지 + cs_change (Linux spi_message 흉내), write_then_read
 *   연습 4: imu_update_bits — 필드만 바꾸는 RMW, 같으면 쓰기 생략
 *   연습 5: 트랜잭션 시간 계산 vs 모델 측정 (burst 1번 vs 단일 읽기 6번)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- MIMU-6 레지스터 맵 (이 레슨의 정의 — 실제 칩은 데이터시트 확인) ---------------- */
enum {
    REG_CFG0     = 0x00,  /* 0x00..0x07: 인터럽트/FIFO 설정 영역 (RW, 기본 0) */
    REG_WHO_AM_I = 0x0F,  /* RO, 항상 0xB5 */
    REG_CTRL1    = 0x10,  /* RW: [7:4] ODR, [3:2] FS (0:±2g 1:±4g 2:±8g 3:±16g) */
    REG_CTRL2    = 0x11,  /* RW (0x10..0x17 이 RW 제어 영역) */
    REG_CTRL3    = 0x12,  /* RW: bit2 IF_INC (기본 1), bit0 SW_RESET (쓰는 즉시 전체 리셋, 읽으면 0) */
    REG_STATUS   = 0x27,  /* RO: bit0 XLDA = 새 샘플 있음, OUT_Z_H(0x2D) 를 읽으면 0 */
    REG_OUT_X_L  = 0x28   /* RO: 0x28..0x2D = X_L X_H Y_L Y_H Z_L Z_H (little-endian int16) */
};
#define WHO_AM_I_VAL   0xB5u
#define CTRL3_IF_INC   0x04u
#define CTRL3_SW_RESET 0x01u
#define STATUS_XLDA    0x01u
#define IMU_T_CSW_NS   100u     /* mock 이 요구하는 CS high 최소 시간 (ns) */

/* ---------------- mock 칩: 상태와 주어진 도우미 ---------------- */
enum { ST_IDLE, ST_CMD, ST_DUMMY, ST_READ, ST_WRITE };
typedef struct { int st; uint8_t addr; uint8_t dummy_left; bool inc; } ImuFrame;   /* 한 CS 프레임 동안의 상태 */
typedef struct {
    uint8_t  regs[128];
    uint8_t  n_dummy;        /* 읽기 명령 뒤 더미 바이트 수 (0 = ST/InvenSense 류, 1 = BMI088 가속도계 류) */
    bool     write_protect;  /* 장애 주입: 모든 쓰기 무시 */
    uint32_t t_csw_min_ns;
    ImuFrame fr;
    uint8_t  junk;           /* "쓰레기" MISO 값을 만드는 LCG 상태 */
    uint32_t writes_ignored; /* RO/예약 주소에 쓰기 시도 횟수 */
    uint32_t cs_glitches;    /* 너무 짧은 CS high 를 못 보고 프레임을 이어간 횟수 */
    bool     ms_style;       /* 연습 1: 명령 bit6 = MS(자동 증가), 주소 6비트 */
    bool     bdu;            /* 연습 2: 선택된 동안 온 새 샘플은 CS↑ 까지 보류 */
    bool     pend;
    int16_t  pend_xyz[3];
} Imu;

static bool imu_reg_writable(uint8_t a) { return a <= 0x07u || (a >= 0x10u && a <= 0x17u); }
static void imu_reset_regs(Imu *d) {
    memset(d->regs, 0, sizeof d->regs);
    d->regs[REG_WHO_AM_I] = WHO_AM_I_VAL;
    d->regs[REG_CTRL3] = CTRL3_IF_INC;
}
static void imu_new(Imu *d, uint8_t n_dummy) {
    memset(d, 0, sizeof *d);
    imu_reset_regs(d);
    d->n_dummy = n_dummy; d->t_csw_min_ns = IMU_T_CSW_NS; d->junk = 0x5Au; d->fr.st = ST_IDLE;
}
/* 명령/더미/쓰기 바이트 동안 칩이 MISO 로 내보내는 의미 없는 값 (x*5+0x3B mod 256, 주기 256) */
static uint8_t imu_junk(Imu *d) { d->junk = (uint8_t)(d->junk * 5u + 0x3Bu); return d->junk; }
static uint8_t imu_reg_read(Imu *d, uint8_t a) {      /* 읽기 부수효과: OUT_Z_H 를 읽으면 XLDA 해제 */
    uint8_t v = d->regs[a];
    if (a == REG_OUT_X_L + 5) d->regs[REG_STATUS] &= (uint8_t)~STATUS_XLDA;
    return v;
}
static void imu_reg_write(Imu *d, uint8_t a, uint8_t v) {
    if (d->write_protect || !imu_reg_writable(a)) { d->writes_ignored++; return; }
    if (a == REG_CTRL3 && (v & CTRL3_SW_RESET)) { imu_reset_regs(d); return; }
    d->regs[a] = v;
}
static void imu_set_sample(Imu *d, int16_t x, int16_t y, int16_t z) {   /* "센서가 새 값을 측정함" */
    const int16_t v[3] = { x, y, z };
    if (d->bdu && d->fr.st != ST_IDLE) {                /* 연습 2: 읽는 중이면 보류 */
        d->pend = true; memcpy(d->pend_xyz, v, sizeof v);
        return;
    }
    for (int k = 0; k < 3; k++) {
        uint16_t u = (uint16_t)v[k];
        d->regs[REG_OUT_X_L + 2 * k] = (uint8_t)(u & 0xFFu);
        d->regs[REG_OUT_X_L + 2 * k + 1] = (uint8_t)(u >> 8);
    }
    d->regs[REG_STATUS] |= STATUS_XLDA;
}

/* ---------------- Part A: 명령 바이트와 슬레이브 상태 기계 ---------------- */

/* 명령 바이트: bit7 = 1 이면 읽기, bit6..0 = 주소 (주소의 bit7 은 버린다) */
static uint8_t imu_cmd(bool rd, uint8_t addr) {
    return (uint8_t)((rd ? 0x80u : 0x00u) | (addr & 0x7Fu));
}

/* CS 엣지: level 0 (CS↓) = 새 프레임 시작 → 명령 바이트 대기. level 1 (CS↑) = 프레임 끝 → 상태 리셋 */
static void imu_cs_edge(Imu *d, int level) {
    d->fr.st = (level == 0) ? ST_CMD : ST_IDLE;
    if (level == 1 && d->pend) {                        /* 연습 2: 프레임이 끝나면 보류한 샘플 반영 */
        d->pend = false;
        imu_set_sample(d, d->pend_xyz[0], d->pend_xyz[1], d->pend_xyz[2]);
    }
}

/* 선택된 동안 바이트 하나가 오갈 때 호출. 반환값 = 이 바이트 동안 칩이 MISO 로 내보낸 값.
 * 전이중(full-duplex): MISO 는 이번 MOSI 를 다 받기 "전에" 정해진다 → 명령 바이트 동안은 쓰레기. */
static uint8_t imu_byte(Imu *d, uint8_t mosi) {
    ImuFrame *f = &d->fr;
    uint8_t miso;
    switch (f->st) {
    case ST_CMD:
        f->addr = d->ms_style ? (uint8_t)(mosi & 0x3Fu) : (uint8_t)(mosi & 0x7Fu);
        f->inc = d->ms_style ? (mosi & 0x40u) != 0 : true;
        if (mosi & 0x80u) { f->dummy_left = d->n_dummy; f->st = d->n_dummy ? ST_DUMMY : ST_READ; }
        else f->st = ST_WRITE;
        return imu_junk(d);
    case ST_DUMMY:
        if (--f->dummy_left == 0) f->st = ST_READ;
        return imu_junk(d);
    case ST_READ:
        miso = imu_reg_read(d, f->addr);
        break;
    case ST_WRITE:
        imu_reg_write(d, f->addr, mosi);
        miso = imu_junk(d);
        break;
    default:
        return 0xFFu;                                    /* 선택 안 됨: 아무것도 안 함 */
    }
    if (d->ms_style) { if (f->inc) f->addr = (uint8_t)((f->addr + 1u) & 0x3Fu); }
    else if (d->regs[REG_CTRL3] & CTRL3_IF_INC) f->addr = (uint8_t)((f->addr + 1u) & 0x7Fu);
    return miso;
}

/* ---------------- 버스 모델 (주어짐): CS 라인 + 바이트 전송 + 시간 + 이벤트 로그 ---------------- */
enum { BUS_OK, BUS_MISO_FLOAT, BUS_MISO_LOW, BUS_LATE_1BIT };
typedef struct {
    Imu      *dev;           /* NULL = 이 CS 에 칩이 없음 */
    int       cs;            /* CS 라인 레벨 (active-low, 1 = 해제) */
    int       fault;         /* 장애 주입: MISO 끊김(풀업 → 0xFF) / 0 고정 / 1비트 늦은 샘플 */
    uint32_t  sclk_hz;
    uint64_t  t_ns, t_rise;  /* 모델 시각, 마지막 CS↑ 시각 */
    ImuFrame  saved;         /* CS↑ 직전 칩 상태 (짧은 CS high 를 칩이 못 본 경우 복원) */
    uint8_t   miso_last;
    uint32_t  frames;        /* 선에서 본 CS↓ 횟수 */
    size_t    cur_len, last_len;
    bool      log_on;
    size_t    log_len;
    char      log[1024];
} Bus;

static void bus_log(Bus *b, const char *s) {
    size_t n = strlen(s);
    if (!b->log_on || b->log_len + n + 1 > sizeof b->log) return;
    memcpy(b->log + b->log_len, s, n + 1);
    b->log_len += n;
}
static void bus_log_start(Bus *b) { b->log_on = true; b->log_len = 0; b->log[0] = '\0'; }
static void bus_init(Bus *b, Imu *dev) {
    memset(b, 0, sizeof *b);
    b->dev = dev; b->cs = 1; b->sclk_hz = 8000000u; b->t_ns = 1000000u;
}
static void bus_delay_ns(Bus *b, uint32_t ns) { b->t_ns += ns; }
static void bus_cs(Bus *b, int level) {
    char s[40];
    if (level == b->cs) return;
    b->cs = level;
    if (level == 0) {
        uint64_t gap = b->t_ns - b->t_rise;
        if (b->log_len > 0 && gap < 10000u) { snprintf(s, sizeof s, " (+%lluns) [", (unsigned long long)gap); bus_log(b, s); }
        else bus_log(b, b->log_len > 0 ? " [" : "[");
        b->frames++; b->cur_len = 0; b->miso_last = 1;
        if (!b->dev) return;
        if (gap < b->dev->t_csw_min_ns) { b->dev->fr = b->saved; b->dev->cs_glitches++; }  /* 칩은 CS↑ 를 못 봤다 */
        else imu_cs_edge(b->dev, 0);
    } else {
        bus_log(b, " ]");
        b->last_len = b->cur_len; b->t_rise = b->t_ns;
        if (!b->dev) return;
        b->saved = b->dev->fr;
        imu_cs_edge(b->dev, 1);
    }
}
/* n 바이트 전이중 교환. CS 는 건드리지 않는다. tx == NULL 이면 0x00 을, rx == NULL 이면 버린다. */
static void spi_xfer(Bus *b, const uint8_t *tx, uint8_t *rx, size_t n) {
    char s[16];
    for (size_t i = 0; i < n; i++) {
        uint8_t mosi = tx ? tx[i] : 0x00u, miso = 0xFFu;    /* 선택 안 됨/칩 없음: MISO hi-Z → 풀업 = 0xFF */
        if (b->cs == 0 && b->dev) miso = imu_byte(b->dev, mosi);
        if (b->fault == BUS_MISO_FLOAT) miso = 0xFFu;
        else if (b->fault == BUS_MISO_LOW) miso = 0x00u;
        else if (b->fault == BUS_LATE_1BIT) {               /* 마스터가 1비트 늦게 샘플: 직전 비트가 MSB 로 */
            uint8_t m = miso;
            miso = (uint8_t)((b->miso_last << 7) | (m >> 1));
            b->miso_last = m & 1u;
        }
        if (rx) rx[i] = miso;
        b->t_ns += 8000000000ull / b->sclk_hz;
        if (b->cs == 0) b->cur_len++;
        snprintf(s, sizeof s, " %02X/%02X", mosi, miso);
        bus_log(b, s);
    }
}

/* ---------------- Part B: 드라이버 (한 트랜잭션 = 한 CS 프레임) ---------------- */
enum { IMU_OK = 0, IMU_E_ARG = -1, IMU_E_NODEV = -2, IMU_E_ID = -3, IMU_E_VERIFY = -4, IMU_E_NOTREADY = -5 };
#define IMU_MAX_BURST 32u
#define IMU_MAX_DUMMY 1u
typedef struct { Bus *bus; uint8_t n_dummy; uint8_t fs; uint8_t last_id; } ImuDrv;

/* CS↓ → len 바이트 교환 → CS↑ → t_CSW 대기. 트랜잭션 하나는 반드시 이 함수 한 번으로. */
static int imu_frame(ImuDrv *d, const uint8_t *tx, uint8_t *rx, size_t len) {
    if (!d || !d->bus || !tx || len == 0) return IMU_E_ARG;
    bus_cs(d->bus, 0);
    spi_xfer(d->bus, tx, rx, len);
    bus_cs(d->bus, 1);
    bus_delay_ns(d->bus, IMU_T_CSW_NS);
    return IMU_OK;
}

/* addr 부터 n 바이트 읽기: [명령][더미 × n_dummy][데이터 × n] 을 한 프레임으로. 주소가 0x7F 를 넘으면 거부. */
static int imu_read(ImuDrv *d, uint8_t addr, uint8_t *buf, size_t n) {
    if (!d || !buf || n == 0 || n > IMU_MAX_BURST || addr > 0x7Fu || addr + n > 0x80u || d->n_dummy > IMU_MAX_DUMMY)
        return IMU_E_ARG;
    uint8_t tx[1 + IMU_MAX_DUMMY + IMU_MAX_BURST] = { 0 }, rx[sizeof tx];
    size_t hdr = 1u + d->n_dummy;
    tx[0] = imu_cmd(true, addr);
    int rc = imu_frame(d, tx, rx, hdr + n);
    if (rc != IMU_OK) return rc;
    memcpy(buf, rx + hdr, n);
    return IMU_OK;
}

/* addr 부터 n 바이트 쓰기: [명령][데이터 × n] 한 프레임 (쓰기에는 더미 바이트 없음) */
static int imu_write(ImuDrv *d, uint8_t addr, const uint8_t *buf, size_t n) {
    if (!d || !buf || n == 0 || n > IMU_MAX_BURST || addr > 0x7Fu || addr + n > 0x80u) return IMU_E_ARG;
    uint8_t tx[1 + IMU_MAX_BURST];
    tx[0] = imu_cmd(false, addr);
    memcpy(tx + 1, buf, n);
    return imu_frame(d, tx, NULL, 1 + n);
}

static int imu_read_reg(ImuDrv *d, uint8_t a, uint8_t *v) { return imu_read(d, a, v, 1); }
static int imu_write_reg(ImuDrv *d, uint8_t a, uint8_t v) { return imu_write(d, a, &v, 1); }

/* ---------------- Part C: 초기화와 가속도 읽기 ---------------- */

/* WHO_AM_I(0x00/0xFF → NODEV, 그 외 불일치 → ID) → CTRL3.SW_RESET → 리셋 끝날 때까지 폴링(최대 10번)
 * → CTRL3 = IF_INC, CTRL1 = odr<<4 | fs<<2 → CTRL1..CTRL3 을 burst 로 읽어 확인 (불일치 → VERIFY) */
static int imu_init(ImuDrv *d, Bus *b, uint8_t n_dummy, uint8_t fs, uint8_t odr) {
    if (!d || !b || n_dummy > IMU_MAX_DUMMY || fs > 3u || odr > 15u) return IMU_E_ARG;
    d->bus = b; d->n_dummy = n_dummy; d->fs = fs; d->last_id = 0;
    uint8_t id = 0, c3 = CTRL3_SW_RESET, got[3];
    int rc = imu_read_reg(d, REG_WHO_AM_I, &id);
    if (rc != IMU_OK) return rc;
    d->last_id = id;
    if (id == 0x00u || id == 0xFFu) return IMU_E_NODEV;
    if (id != WHO_AM_I_VAL) return IMU_E_ID;
    if ((rc = imu_write_reg(d, REG_CTRL3, CTRL3_SW_RESET)) != IMU_OK) return rc;
    for (int i = 0; i < 10 && (c3 & CTRL3_SW_RESET); i++)
        if ((rc = imu_read_reg(d, REG_CTRL3, &c3)) != IMU_OK) return rc;
    if (c3 & CTRL3_SW_RESET) return IMU_E_VERIFY;
    const uint8_t c1 = (uint8_t)(odr << 4 | fs << 2);
    if ((rc = imu_write_reg(d, REG_CTRL3, CTRL3_IF_INC)) != IMU_OK) return rc;
    if ((rc = imu_write_reg(d, REG_CTRL1, c1)) != IMU_OK) return rc;
    if ((rc = imu_read(d, REG_CTRL1, got, 3)) != IMU_OK) return rc;
    if (got[0] != c1 || got[2] != CTRL3_IF_INC) return IMU_E_VERIFY;
    return IMU_OK;
}


/* ================= 연습 1: LIS3DH 식 명령 바이트 ================= */
/* bit7 = R/W, bit6 = MS (1 이면 주소 자동 증가), bit5..0 = 주소 (6비트) */
static uint8_t lis_cmd(bool rd, bool ms, uint8_t addr) {
    return (uint8_t)((rd ? 0x80u : 0x00u) | (ms ? 0x40u : 0x00u) | (addr & 0x3Fu));
}

/* ================= 연습 3: 세그먼트 메시지 (Linux spi_message / spi_transfer 흉내) ================= */
typedef struct { const uint8_t *tx; uint8_t *rx; size_t len; bool cs_change; } SpiSeg;
/* CS 를 한 번 내리고 세그먼트를 차례로. cs_change 인 세그먼트 뒤에서만 CS 를 올렸다(t_CSW) 다시 내린다. */
static int spi_message_run(Bus *b, const SpiSeg *seg, size_t nseg) {
    if (!b || !seg || nseg == 0) return IMU_E_ARG;
    bus_cs(b, 0);
    for (size_t i = 0; i < nseg; i++) {
        spi_xfer(b, seg[i].tx, seg[i].rx, seg[i].len);
        if (seg[i].cs_change && i + 1 < nseg) { bus_cs(b, 1); bus_delay_ns(b, IMU_T_CSW_NS); bus_cs(b, 0); }
    }
    bus_cs(b, 1);
    bus_delay_ns(b, IMU_T_CSW_NS);
    return IMU_OK;
}
/* 명령 ntx 바이트를 보내고 이어서 nrx 바이트를 받는다 (받는 동안 MOSI = 0x00). bug = 첫 세그먼트 뒤 CS 해제 */
static int spi_write_then_read(Bus *b, const uint8_t *txb, size_t ntx, uint8_t *rxb, size_t nrx, bool bug) {
    const SpiSeg s[2] = { { txb, NULL, ntx, bug }, { NULL, rxb, nrx, false } };
    return spi_message_run(b, s, 2);
}

/* ================= 연습 4: 필드만 바꾸는 RMW ================= */
static int imu_update_bits(ImuDrv *d, uint8_t reg, uint8_t mask, uint8_t val, bool *changed) {
    uint8_t old = 0;
    int rc = imu_read_reg(d, reg, &old);
    if (rc != IMU_OK) return rc;
    uint8_t nv = (uint8_t)((old & ~mask) | (val & mask));
    if (changed) *changed = (nv != old);
    if (nv == old) return IMU_OK;                     /* 같으면 버스를 한 번 덜 쓴다 */
    return imu_write_reg(d, reg, nv);
}

/* ================= 연습 5: 트랜잭션 시간 ================= */
/* n 바이트 프레임 하나 + 뒤 CS high 대기 gap_ns. (t_CSS/t_CSH·소프트웨어 오버헤드는 모델에 없음) */
static uint64_t xfer_time_ns(size_t nbytes, uint32_t sclk_hz, uint32_t gap_ns) {
    return (uint64_t)nbytes * 8000000000ull / sclk_hz + gap_ns;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static void raw_frame(Bus *b, const uint8_t *tx, uint8_t *rx, size_t n) {
    bus_cs(b, 0); spi_xfer(b, tx, rx, n); bus_cs(b, 1); bus_delay_ns(b, 1000u);
}
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

int main(void) {
    static Imu dev;
    static Bus bus;
    ImuDrv drv = { 0 };
    uint8_t tx[64] = { 0 }, rx[64] = { 0 }, buf[64] = { 0 };

    /* ===== 연습 1: LIS3DH 식 MS 비트 ===== */
    CHECK(lis_cmd(true, true, 0x28) == 0xE8);
    CHECK(lis_cmd(true, false, 0x0F) == 0x8F);
    CHECK(lis_cmd(false, true, 0x20) == 0x60);
    CHECK(lis_cmd(true, true, 0x7F) == 0xFF);           /* 주소는 6비트로 잘린다 → 0x3F */
    imu_new(&dev, 0); bus_init(&bus, &dev); dev.ms_style = true;
    imu_set_sample(&dev, 0x0201, 0x0403, 0x0605);
    memset(tx, 0, sizeof tx); tx[0] = lis_cmd(true, false, REG_OUT_X_L);
    raw_frame(&bus, tx, rx, 7);
    CHECK(rx[1] == 0x01 && rx[2] == 0x01 && rx[6] == 0x01);              /* MS = 0: 같은 주소 반복 */
    tx[0] = lis_cmd(true, true, REG_OUT_X_L);
    raw_frame(&bus, tx, rx, 7);
    CHECK(rx[1] == 0x01 && rx[2] == 0x02 && rx[6] == 0x06);              /* MS = 1: 증가 */
    dev.regs[0x3F] = 0x3F; dev.regs[0x00] = 0x77;
    tx[0] = lis_cmd(true, true, 0x3F);
    raw_frame(&bus, tx, rx, 3);
    CHECK(rx[1] == 0x3F && rx[2] == 0x77);                                /* 6비트 주소 랩 0x3F → 0x00 */
    tx[0] = lis_cmd(false, true, 0x10); tx[1] = 0x11; tx[2] = 0x22;
    raw_frame(&bus, tx, rx, 3);
    CHECK(dev.regs[0x10] == 0x11 && dev.regs[0x11] == 0x22);
    tx[0] = lis_cmd(false, false, 0x10); tx[1] = 0x33; tx[2] = 0x44;
    raw_frame(&bus, tx, rx, 3);
    CHECK(dev.regs[0x10] == 0x44 && dev.regs[0x11] == 0x22);              /* MS = 0 쓰기: 같은 칸에 덮어씀 */

    /* ===== 연습 2: 찢어진 샘플과 BDU ===== */
    for (int bdu = 0; bdu <= 1; bdu++) {
        uint16_t split, burst, after;
        imu_new(&dev, 0); bus_init(&bus, &dev); dev.bdu = bdu;
        drv.bus = &bus; drv.n_dummy = 0;
        /* (a) 단일 읽기 두 번 사이에 새 샘플 */
        imu_set_sample(&dev, 0x00FF, 0, 0);
        imu_read_reg(&drv, REG_OUT_X_L, &buf[0]);
        imu_set_sample(&dev, 0x0100, 0, 0);
        imu_read_reg(&drv, REG_OUT_X_L + 1, &buf[1]);
        split = le16(buf);
        /* (b) 한 프레임 burst 인데, X_L 과 X_H 사이에 새 샘플 */
        imu_set_sample(&dev, 0x00FF, 0, 0);
        memset(tx, 0, sizeof tx); tx[0] = 0x80 | REG_OUT_X_L;
        bus_cs(&bus, 0);
        spi_xfer(&bus, tx, rx, 2);
        imu_set_sample(&dev, 0x0100, 0, 0);
        spi_xfer(&bus, tx + 2, rx + 2, 1);
        bus_cs(&bus, 1); bus_delay_ns(&bus, IMU_T_CSW_NS);
        burst = le16(rx + 1);
        imu_read(&drv, REG_OUT_X_L, buf, 2);
        after = le16(buf);
        printf("bdu=%d: split singles=0x%04X  burst(mid-frame update)=0x%04X  next read=0x%04X\n", bdu, split, burst, after);
        CHECK(split == 0x01FF);                         /* BDU 가 프레임 기준이면 단일 읽기 둘은 여전히 찢어진다 */
        CHECK(burst == (bdu ? 0x00FF : 0x01FF));
        CHECK(after == 0x0100);
    }

    /* ===== 연습 3: 세그먼트 메시지 ===== */
    imu_new(&dev, 0); bus_init(&bus, &dev);
    for (int i = 0; i < 8; i++) dev.regs[i] = (uint8_t)(0xA0 + i);
    imu_set_sample(&dev, 1000, -1000, 16384);
    uint8_t c = 0x80 | REG_WHO_AM_I;
    CHECK(spi_write_then_read(&bus, &c, 1, buf, 1, false) == IMU_OK && buf[0] == WHO_AM_I_VAL && bus.frames == 1);
    c = 0x80 | REG_OUT_X_L;
    CHECK(spi_write_then_read(&bus, &c, 1, buf, 6, false) == IMU_OK && memcmp(buf, dev.regs + REG_OUT_X_L, 6) == 0);
    CHECK(bus.frames == 2 && bus.last_len == 7);
    bus_log_start(&bus);
    CHECK(spi_write_then_read(&bus, &c, 1, buf, 6, true) == IMU_OK);       /* cs_change 버그 */
    printf("cs_change bug      : %s\n", bus.log);
    CHECK(bus.frames == 4 && dev.regs[0] == 0x00 && dev.regs[4] == 0x00 && dev.regs[5] == 0xA5);
    CHECK(spi_message_run(&bus, NULL, 1) == IMU_E_ARG);

    /* ===== 연습 4: imu_update_bits ===== */
    imu_new(&dev, 0); bus_init(&bus, &dev);
    CHECK(imu_init(&drv, &bus, 0, 0, 4) == IMU_OK && dev.regs[REG_CTRL1] == 0x40);
    {
        bool ch = false;
        uint32_t f0 = bus.frames;
        CHECK(imu_update_bits(&drv, REG_CTRL1, 0x0C, 2u << 2, &ch) == IMU_OK && ch && dev.regs[REG_CTRL1] == 0x48);
        CHECK(bus.frames == f0 + 2);
        f0 = bus.frames;
        CHECK(imu_update_bits(&drv, REG_CTRL1, 0x0C, 2u << 2, &ch) == IMU_OK && !ch && bus.frames == f0 + 1);
        CHECK(imu_update_bits(&drv, REG_CTRL1, 0xF0, 0xF3, &ch) == IMU_OK && dev.regs[REG_CTRL1] == 0xF8);  /* 마스크 밖 비트 무시 */
    }

    /* ===== 연습 5: 시간 — 계산 vs 모델 측정 ===== */
    {
        const uint32_t clk[3] = { 1000000u, 8000000u, 10000000u };
        printf("SCLK      | burst 6 (1 frame) | 6 x single | dummy=1 burst 6\n");
        for (int i = 0; i < 3; i++) {
            uint64_t t0, tb, ts, td;
            imu_new(&dev, 0); bus_init(&bus, &dev); bus.sclk_hz = clk[i];
            drv.bus = &bus; drv.n_dummy = 0;
            t0 = bus.t_ns; imu_read(&drv, REG_OUT_X_L, buf, 6); tb = bus.t_ns - t0;
            t0 = bus.t_ns; for (int k = 0; k < 6; k++) imu_read_reg(&drv, (uint8_t)(REG_OUT_X_L + k), buf + k); ts = bus.t_ns - t0;
            imu_new(&dev, 1); drv.n_dummy = 1;
            t0 = bus.t_ns; imu_read(&drv, REG_OUT_X_L, buf, 6); td = bus.t_ns - t0;
            printf("%5u kHz | %10llu ns     | %7llu ns | %8llu ns\n", (unsigned)(clk[i] / 1000u),
                   (unsigned long long)tb, (unsigned long long)ts, (unsigned long long)td);
            CHECK(tb == xfer_time_ns(7, clk[i], IMU_T_CSW_NS));
            CHECK(ts == 6 * xfer_time_ns(2, clk[i], IMU_T_CSW_NS));
            CHECK(td == xfer_time_ns(8, clk[i], IMU_T_CSW_NS));
        }
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
