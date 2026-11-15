/* COMM-B05 SPI 디바이스 접근: 명령/주소 포맷, burst, CS 타이밍
 *
 * 가상 칩 "MIMU-6"(이 레슨의 mock IMU)을 바이트 단위 SPI 버스 모델 위에 만들고 드라이버를 짠다.
 *   명령 바이트 = bit7 R/W (1 = 읽기) + bit6..0 레지스터 주소 (7비트).
 *   자동 증가(auto-increment)는 CTRL3.IF_INC 비트(리셋 기본값 1)가 켜고 끈다. 0x7F 다음은 0x00.
 *   읽기: 명령 바이트 동안 MISO = 쓰레기, 그 뒤 더미 바이트 n_dummy 개(0 또는 1), 그 다음부터 레지스터 값.
 *   CS↑ 는 칩의 상태 기계를 리셋한다. CS high 가 t_CSW(100 ns) 보다 짧으면 칩은 그 펄스를 못 본다.
 * Part A: 명령 바이트 + 슬레이브 상태 기계 (imu_cmd / imu_cs_edge / imu_byte)
 * Part B: 드라이버 imu_frame / imu_read / imu_write + 버그 재현
 *         (주소와 데이터 사이에 CS 해제, 더미 바이트 안 버림, CS high 시간 부족)
 * Part C: imu_init (WHO_AM_I → soft reset → 설정 → 읽어서 확인) + imu_read_accel_mg (STATUS+XYZ 7바이트 burst)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
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
typedef struct { int st; uint8_t addr; uint8_t dummy_left; } ImuFrame;   /* 한 CS 프레임 동안의 상태 */
typedef struct {
    uint8_t  regs[128];
    uint8_t  n_dummy;        /* 읽기 명령 뒤 더미 바이트 수 (0 = ST/InvenSense 류, 1 = BMI088 가속도계 류) */
    bool     write_protect;  /* 장애 주입: 모든 쓰기 무시 */
    uint32_t t_csw_min_ns;
    ImuFrame fr;
    uint8_t  junk;           /* "쓰레기" MISO 값을 만드는 LCG 상태 */
    uint32_t writes_ignored; /* RO/예약 주소에 쓰기 시도 횟수 */
    uint32_t cs_glitches;    /* 너무 짧은 CS high 를 못 보고 프레임을 이어간 횟수 */
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
    (void)rd; (void)addr;
    return 0; /* TODO: bit7 = R/W, bit6..0 = 주소 */
}

/* CS 엣지: level 0 (CS↓) = 새 프레임 시작 → 명령 바이트 대기. level 1 (CS↑) = 프레임 끝 → 상태 리셋 */
static void imu_cs_edge(Imu *d, int level) {
    (void)d; (void)level; /* TODO: CS↓ → 명령 대기(ST_CMD), CS↑ → ST_IDLE */
}

/* 선택된 동안 바이트 하나가 오갈 때 호출. 반환값 = 이 바이트 동안 칩이 MISO 로 내보낸 값.
 * 전이중(full-duplex): MISO 는 이번 MOSI 를 다 받기 "전에" 정해진다 → 명령 바이트 동안은 쓰레기. */
static uint8_t imu_byte(Imu *d, uint8_t mosi) {
    (void)d; (void)mosi;
    return 0xFFu; /* TODO: 상태별 처리(명령 해석 → 더미 → 읽기/쓰기) + IF_INC 면 주소 증가 */
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
    (void)d; (void)tx; (void)rx; (void)len;
    return IMU_E_ARG; /* TODO: 인자 검사 → CS↓ → spi_xfer → CS↑ → t_CSW 대기 */
}

/* addr 부터 n 바이트 읽기: [명령][더미 × n_dummy][데이터 × n] 을 한 프레임으로. 주소가 0x7F 를 넘으면 거부. */
static int imu_read(ImuDrv *d, uint8_t addr, uint8_t *buf, size_t n) {
    (void)d; (void)addr; (void)buf; (void)n;
    return IMU_E_ARG; /* TODO: 인자 검사 → [명령][더미][데이터] 한 프레임 → 데이터만 복사 */
}

/* addr 부터 n 바이트 쓰기: [명령][데이터 × n] 한 프레임 (쓰기에는 더미 바이트 없음) */
static int imu_write(ImuDrv *d, uint8_t addr, const uint8_t *buf, size_t n) {
    (void)d; (void)addr; (void)buf; (void)n;
    return IMU_E_ARG; /* TODO: 인자 검사 → [명령][데이터] 한 프레임 */
}

static int imu_read_reg(ImuDrv *d, uint8_t a, uint8_t *v) { return imu_read(d, a, v, 1); }
static int imu_write_reg(ImuDrv *d, uint8_t a, uint8_t v) { return imu_write(d, a, &v, 1); }

/* ---------------- 버그 재현 (주어짐) ---------------- */
/* 버그 1: 명령과 데이터를 서로 다른 CS 프레임으로 보낸다
 *        (예: CS 를 자동으로 올리는 "전송 함수"를 명령용·데이터용으로 두 번 호출) */
static void bug_read_split_cs(ImuDrv *d, uint8_t addr, uint8_t *buf, size_t n, uint8_t fill) {
    uint8_t cmd = imu_cmd(true, addr), junk = 0, tx[IMU_MAX_BURST];
    memset(tx, fill, sizeof tx);
    imu_frame(d, &cmd, &junk, 1);
    imu_frame(d, tx, buf, n);
}
/* 버그 2: 더미 바이트가 있는 칩에서 더미를 안 버린다 */
static void bug_read_no_dummy(ImuDrv *d, uint8_t addr, uint8_t *buf, size_t n) {
    uint8_t tx[1 + IMU_MAX_BURST] = { 0 }, rx[sizeof tx] = { 0 };
    tx[0] = imu_cmd(true, addr);
    imu_frame(d, tx, rx, 1 + n);
    memcpy(buf, rx + 1, n);
}
/* 버그 3: 쓰기 두 번 사이 CS high 시간 0 ns (t_CSW 위반) */
static void bug_write2_no_gap(ImuDrv *d, uint8_t a1, uint8_t v1, uint8_t a2, uint8_t v2) {
    uint8_t t1[2] = { imu_cmd(false, a1), v1 }, t2[2] = { imu_cmd(false, a2), v2 };
    bus_cs(d->bus, 0); spi_xfer(d->bus, t1, NULL, 2); bus_cs(d->bus, 1);
    bus_cs(d->bus, 0); spi_xfer(d->bus, t2, NULL, 2); bus_cs(d->bus, 1);
    bus_delay_ns(d->bus, IMU_T_CSW_NS);
}

/* ---------------- Part C: 초기화와 가속도 읽기 ---------------- */

/* WHO_AM_I(0x00/0xFF → NODEV, 그 외 불일치 → ID) → CTRL3.SW_RESET → 리셋 끝날 때까지 폴링(최대 10번)
 * → CTRL3 = IF_INC, CTRL1 = odr<<4 | fs<<2 → CTRL1..CTRL3 을 burst 로 읽어 확인 (불일치 → VERIFY) */
static int imu_init(ImuDrv *d, Bus *b, uint8_t n_dummy, uint8_t fs, uint8_t odr) {
    (void)d; (void)b; (void)n_dummy; (void)fs; (void)odr;
    return IMU_E_ARG; /* TODO: WHO_AM_I → SW_RESET + 폴링 → CTRL3/CTRL1 쓰기 → burst 로 읽어 확인 */
}

/* STATUS(0x27) + OUT_X_L..OUT_Z_H 를 7바이트 burst 한 번으로. XLDA = 0 이면 NOTREADY.
 * mg = raw × (2000 << fs) / 32768, C 정수 나눗셈(0 쪽으로 버림) */
static int imu_read_accel_mg(ImuDrv *d, int32_t mg[3]) {
    (void)d; (void)mg;
    return IMU_E_ARG; /* TODO: STATUS..OUT_Z_H 7바이트 burst → XLDA 확인 → LE int16 → mg */
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* Part A 테스트용: 드라이버 없이 버스를 직접 쓰는 한 프레임 */
static void raw_frame(Bus *b, const uint8_t *tx, uint8_t *rx, size_t n) {
    bus_cs(b, 0); spi_xfer(b, tx, rx, n); bus_cs(b, 1); bus_delay_ns(b, 1000u);
}
static void fill_random_regs(Imu *d, uint32_t *seed) {   /* RW 영역과 OUT/STATUS 를 랜덤으로 (CTRL3 제외) */
    for (int a = 0; a < 128; a++)
        if ((imu_reg_writable((uint8_t)a) && a != REG_CTRL3) || (a >= REG_STATUS && a <= REG_OUT_X_L + 5))
            d->regs[a] = (uint8_t)xs32(seed);
}
static int32_t ref_mg(int16_t raw, uint8_t fs) {        /* 기준: 64비트로 곱하고 부호를 떼서 나눔 */
    int64_t p = (int64_t)raw * (2000LL << fs);
    return (int32_t)(p < 0 ? -((-p) / 32768) : p / 32768);
}

int main(void) {
    static Imu dev;
    static Bus bus;
    uint32_t seed = 0xC0B05u;
    uint8_t tx[64] = { 0 }, rx[64] = { 0 }, buf[64] = { 0 }, exp[64] = { 0 };
    ImuDrv drv;

    /* ===== mock 자체 점검 (주어진 도우미 — TODO 와 무관하게 통과해야 함) ===== */
    imu_new(&dev, 0);
    {
        bool seen[256] = { false };
        int distinct = 0;
        for (int i = 0; i < 256; i++) { uint8_t j = imu_junk(&dev); if (!seen[j]) { seen[j] = true; distinct++; } }
        CHECK(distinct == 256);                        /* 쓰레기 값 LCG 는 주기 256 */
    }
    imu_reg_write(&dev, REG_WHO_AM_I, 0x00);
    imu_set_sample(&dev, 1, 2, 3);
    CHECK(imu_reg_read(&dev, REG_WHO_AM_I) == WHO_AM_I_VAL && dev.writes_ignored == 1);
    CHECK(imu_reg_read(&dev, REG_STATUS) == STATUS_XLDA && imu_reg_read(&dev, 0x2D) == 0 && dev.regs[REG_STATUS] == 0);

    /* ===== Part A: 명령 바이트 ===== */
    CHECK(imu_cmd(true, 0x0F) == 0x8F);
    CHECK(imu_cmd(false, 0x10) == 0x10);
    CHECK(imu_cmd(true, 0xFF) == 0xFF);
    CHECK(imu_cmd(false, 0x80) == 0x00);

    /* A: WHO_AM_I 단일 읽기 (버스 직접) */
    imu_new(&dev, 0); bus_init(&bus, &dev);
    bus_log_start(&bus);
    tx[0] = 0x8F; tx[1] = 0x00;
    raw_frame(&bus, tx, rx, 2);
    printf("WHO_AM_I read      : %s\n", bus.log);
    CHECK(rx[1] == WHO_AM_I_VAL);
    CHECK(bus.last_len == 2);

    /* A: 6바이트 burst (IF_INC = 1) */
    imu_set_sample(&dev, 1, -2, 300);
    bus_log_start(&bus);
    memset(tx, 0, sizeof tx); tx[0] = 0x80 | REG_OUT_X_L;
    raw_frame(&bus, tx, rx, 7);
    printf("burst 6 from 0x28  : %s\n", bus.log);
    const uint8_t xyz[6] = { 0x01, 0x00, 0xFE, 0xFF, 0x2C, 0x01 };
    CHECK(memcmp(rx + 1, xyz, 6) == 0);

    /* A: IF_INC = 0 → 같은 레지스터 반복 */
    tx[0] = REG_CTRL3; tx[1] = 0x00;
    raw_frame(&bus, tx, rx, 2);
    CHECK(dev.regs[REG_CTRL3] == 0x00);
    memset(tx, 0, sizeof tx); tx[0] = 0x80 | REG_OUT_X_L;
    raw_frame(&bus, tx, rx, 7);
    CHECK(rx[1] == 0x01 && rx[2] == 0x01 && rx[6] == 0x01);
    tx[0] = REG_CTRL3; tx[1] = CTRL3_IF_INC;
    raw_frame(&bus, tx, rx, 2);

    /* A: 단일 쓰기, burst 쓰기, RO 쓰기 무시 */
    tx[0] = REG_CTRL1; tx[1] = 0x5C;
    raw_frame(&bus, tx, rx, 2);
    CHECK(dev.regs[REG_CTRL1] == 0x5C);
    tx[0] = REG_CFG0;
    for (int i = 0; i < 8; i++) tx[1 + i] = (uint8_t)(0xA0 + i);
    raw_frame(&bus, tx, rx, 9);
    CHECK(dev.regs[0x00] == 0xA0 && dev.regs[0x07] == 0xA7);
    uint32_t ign = dev.writes_ignored;
    tx[0] = REG_WHO_AM_I; tx[1] = 0x00;
    raw_frame(&bus, tx, rx, 2);
    CHECK(dev.regs[REG_WHO_AM_I] == WHO_AM_I_VAL && dev.writes_ignored == ign + 1);

    /* A: 0x7F → 0x00 랩어라운드 */
    memset(tx, 0, sizeof tx); tx[0] = 0x80 | 0x7E;
    raw_frame(&bus, tx, rx, 5);
    CHECK(rx[1] == 0x00 && rx[2] == 0x00 && rx[3] == 0xA0 && rx[4] == 0xA1);

    /* A: 더미 바이트 1개짜리 칩 */
    imu_new(&dev, 1); bus_init(&bus, &dev);
    bus_log_start(&bus);
    memset(tx, 0, sizeof tx); tx[0] = 0x8F;
    raw_frame(&bus, tx, rx, 3);
    printf("dummy=1 WHO_AM_I   : %s\n", bus.log);
    CHECK(rx[2] == WHO_AM_I_VAL);

    /* A: CS high 동안 오간 바이트는 무시, MISO = 0xFF */
    imu_new(&dev, 0); bus_init(&bus, &dev);
    tx[0] = REG_CTRL1; tx[1] = 0xAA;
    spi_xfer(&bus, tx, rx, 2);
    CHECK(dev.regs[REG_CTRL1] == 0x00 && rx[0] == 0xFF && rx[1] == 0xFF);

    /* A: CS↑ 는 상태 기계를 리셋 — 명령만 보내고 끝난 쓰기 프레임 다음의 읽기 */
    tx[0] = REG_CTRL1;
    raw_frame(&bus, tx, rx, 1);
    tx[0] = 0x8F; tx[1] = 0x00;
    raw_frame(&bus, tx, rx, 2);
    CHECK(rx[1] == WHO_AM_I_VAL && dev.regs[REG_CTRL1] == 0x00);

    /* A: SW_RESET */
    tx[0] = REG_CTRL1; tx[1] = 0x5C;
    raw_frame(&bus, tx, rx, 2);
    tx[0] = REG_CTRL3; tx[1] = CTRL3_SW_RESET;
    raw_frame(&bus, tx, rx, 2);
    CHECK(dev.regs[REG_CTRL1] == 0x00 && dev.regs[REG_CTRL3] == CTRL3_IF_INC);

    /* A: 전수 — 주소 128개(CTRL3 제외) × 값 256개, 쓰고 다시 읽기 */
    {
        int bad = 0;
        imu_new(&dev, 0); bus_init(&bus, &dev);
        for (int a = 0; a < 128; a++) {
            if (a == REG_CTRL3) continue;
            for (int v = 0; v < 256; v++) {
                uint8_t before = dev.regs[a];
                tx[0] = (uint8_t)a; tx[1] = (uint8_t)v;
                raw_frame(&bus, tx, rx, 2);
                tx[0] = (uint8_t)(0x80 | a); tx[1] = 0x00;
                raw_frame(&bus, tx, rx, 2);
                uint8_t want = imu_reg_writable((uint8_t)a) ? (uint8_t)v : before;
                if (rx[1] != want) bad++;
            }
        }
        printf("exhaustive write/read 127 addr x 256 values: bad=%d\n", bad);
        CHECK(bad == 0);
    }

    /* A: 랜덤 burst 읽기 3000개 — 더미 0/1, IF_INC 0/1, 주소·길이 랜덤, 랩어라운드 포함 */
    {
        int bad = 0;
        for (int t = 0; t < 3000; t++) {
            uint8_t nd = (uint8_t)(xs32(&seed) & 1u), inc = (xs32(&seed) & 3u) != 0;
            imu_new(&dev, nd); bus_init(&bus, &dev);
            fill_random_regs(&dev, &seed);
            dev.regs[REG_CTRL3] = inc ? CTRL3_IF_INC : 0;
            uint8_t a = (uint8_t)(xs32(&seed) & 0x7Fu);
            size_t n = 1 + xs32(&seed) % 20u;
            uint8_t snap[128];
            memcpy(snap, dev.regs, sizeof snap);
            memset(tx, 0, sizeof tx); tx[0] = (uint8_t)(0x80 | a);
            raw_frame(&bus, tx, rx, 1 + nd + n);
            for (size_t k = 0; k < n; k++)
                if (rx[1 + nd + k] != snap[inc ? (a + k) & 0x7Fu : a]) { bad++; break; }
        }
        CHECK(bad == 0);
    }

    /* A: 랜덤 burst 쓰기 1000개 — 기대값 모델과 비교 */
    {
        int bad = 0;
        imu_new(&dev, 0); bus_init(&bus, &dev);
        for (int t = 0; t < 1000; t++) {
            uint8_t model[128];
            memcpy(model, dev.regs, sizeof model);
            uint8_t a = (uint8_t)(xs32(&seed) & 0x7Fu);
            size_t n = 1 + xs32(&seed) % 16u;
            tx[0] = a;
            for (size_t k = 0; k < n; k++) {
                uint8_t ad = (uint8_t)((a + k) & 0x7Fu), v = (uint8_t)xs32(&seed);
                if (ad == REG_CTRL3) v = (uint8_t)((v & ~CTRL3_SW_RESET) | CTRL3_IF_INC);  /* 리셋·자동증가 끄기 방지 */
                tx[1 + k] = v;
                if (imu_reg_writable(ad)) model[ad] = v;
            }
            raw_frame(&bus, tx, rx, 1 + n);
            if (memcmp(model, dev.regs, sizeof model) != 0) bad++;
        }
        CHECK(bad == 0);
    }

    /* ===== Part B: 드라이버 ===== */
    imu_new(&dev, 0); bus_init(&bus, &dev);
    drv.bus = &bus; drv.n_dummy = 0; drv.fs = 0; drv.last_id = 0;
    {
        uint32_t f0 = bus.frames;
        CHECK(imu_read(&drv, 0x10, buf, 0) == IMU_E_ARG);
        CHECK(imu_read(&drv, 0x10, buf, IMU_MAX_BURST + 1) == IMU_E_ARG);
        CHECK(imu_read(&drv, 0x80, buf, 1) == IMU_E_ARG);
        CHECK(imu_read(&drv, 0x7F, buf, 2) == IMU_E_ARG);
        CHECK(imu_read(&drv, 0x10, NULL, 1) == IMU_E_ARG);
        CHECK(imu_write(&drv, 0x7E, buf, 3) == IMU_E_ARG);
        CHECK(imu_write(&drv, 0x00, NULL, 1) == IMU_E_ARG);
        CHECK(imu_frame(&drv, tx, rx, 0) == IMU_E_ARG);
        CHECK(bus.frames == f0);                       /* 인자 오류면 버스를 건드리지 않는다 */
    }
    CHECK(imu_read_reg(&drv, REG_WHO_AM_I, buf) == IMU_OK && buf[0] == WHO_AM_I_VAL && bus.last_len == 2);
    CHECK(imu_read(&drv, 0x7F, buf, 1) == IMU_OK);    /* 경계: 마지막 주소 1바이트는 허용 */

    /* B: 랜덤 읽기 2000개 (더미 0/1) — 값 + "한 호출 = 한 프레임, 길이 1+dummy+n" */
    {
        int bad = 0;
        for (int t = 0; t < 2000; t++) {
            uint8_t nd = (uint8_t)(xs32(&seed) & 1u);
            imu_new(&dev, nd); bus_init(&bus, &dev);
            drv.bus = &bus; drv.n_dummy = nd;
            fill_random_regs(&dev, &seed);
            size_t n = 1 + xs32(&seed) % IMU_MAX_BURST;
            uint8_t a = (uint8_t)(xs32(&seed) % (0x81u - n));
            uint8_t snap[128];
            memcpy(snap, dev.regs, sizeof snap);
            uint32_t f0 = bus.frames;
            if (imu_read(&drv, a, buf, n) != IMU_OK || memcmp(buf, snap + a, n) != 0 ||
                bus.frames != f0 + 1 || bus.last_len != 1 + nd + n) bad++;
        }
        CHECK(bad == 0);
    }
    /* B: 랜덤 쓰기 1000개 */
    {
        int bad = 0;
        imu_new(&dev, 0); bus_init(&bus, &dev);
        drv.bus = &bus; drv.n_dummy = 0;
        for (int t = 0; t < 1000; t++) {
            uint8_t model[128];
            memcpy(model, dev.regs, sizeof model);
            size_t n = 1 + xs32(&seed) % 16u;
            uint8_t a = (uint8_t)(xs32(&seed) % (0x81u - n));
            for (size_t k = 0; k < n; k++) {
                uint8_t ad = (uint8_t)(a + k), v = (uint8_t)xs32(&seed);
                if (ad == REG_CTRL3) v = (uint8_t)((v & ~CTRL3_SW_RESET) | CTRL3_IF_INC);
                buf[k] = v;
                if (imu_reg_writable(ad)) model[ad] = v;
            }
            uint32_t f0 = bus.frames;
            if (imu_write(&drv, a, buf, n) != IMU_OK || memcmp(model, dev.regs, sizeof model) != 0 ||
                bus.frames != f0 + 1 || bus.last_len != 1 + n) bad++;
        }
        CHECK(bad == 0);
    }

    /* B: 버그 1 재현 — 명령과 데이터 사이에 CS↑ (MOSI 채움 0x00) */
    imu_new(&dev, 0); bus_init(&bus, &dev);
    drv.bus = &bus; drv.n_dummy = 0;
    for (int i = 0; i < 8; i++) dev.regs[i] = (uint8_t)(0xA0 + i);
    imu_set_sample(&dev, 1000, -1000, 16384);
    memcpy(exp, dev.regs + REG_OUT_X_L, 6);
    bus_log_start(&bus);
    bug_read_split_cs(&drv, REG_OUT_X_L, buf, 6, 0x00);
    printf("BUG split CS, 0x00 : %s\n", bus.log);
    CHECK(memcmp(buf, exp, 6) != 0);                  /* 읽은 값이 틀리고 */
    CHECK(dev.regs[0] == 0 && dev.regs[4] == 0);      /* 0x00..0x04 설정이 0 으로 덮였다 */
    CHECK(dev.regs[5] == 0xA5);
    CHECK(bus.frames == 2);
    /* 같은 버그, MOSI 채움 0xFF → 0x7F 부터 읽기 명령이 되어 엉뚱한 레지스터 */
    for (int i = 0; i < 8; i++) dev.regs[i] = (uint8_t)(0xA0 + i);
    bug_read_split_cs(&drv, REG_OUT_X_L, buf, 6, 0xFF);
    CHECK(buf[1] == dev.regs[0x7F] && buf[2] == 0xA0 && buf[5] == 0xA3);
    CHECK(dev.regs[0] == 0xA0);
    /* 수정: 한 프레임 */
    bus_log_start(&bus);
    CHECK(imu_read(&drv, REG_OUT_X_L, buf, 6) == IMU_OK && memcmp(buf, exp, 6) == 0);
    printf("FIX one CS frame   : %s\n", bus.log);
    CHECK(dev.regs[0] == 0xA0 && dev.regs[4] == 0xA4);

    /* B: 버그 2 재현 — 더미 1개 칩에서 더미를 안 버림 → 한 바이트 밀림 */
    imu_new(&dev, 1); bus_init(&bus, &dev);
    drv.bus = &bus; drv.n_dummy = 1;
    imu_set_sample(&dev, 0x1234, 0x5678, -2);
    memcpy(exp, dev.regs + REG_OUT_X_L, 6);
    bug_read_no_dummy(&drv, REG_OUT_X_L, buf, 6);
    CHECK(memcmp(buf + 1, exp, 5) == 0 && buf[0] != exp[0]);
    imu_set_sample(&dev, 0x1234, 0x5678, -2);
    CHECK(imu_read(&drv, REG_OUT_X_L, buf, 6) == IMU_OK && memcmp(buf, exp, 6) == 0);

    /* B: 버그 3 재현 — CS high 0 ns → 칩이 두 쓰기를 한 프레임으로 본다 */
    imu_new(&dev, 0); bus_init(&bus, &dev);
    drv.bus = &bus; drv.n_dummy = 0;
    bus_log_start(&bus);
    bug_write2_no_gap(&drv, 0x01, 0x03, REG_CTRL1, 0x5C);
    printf("BUG CS high 0 ns   : %s\n", bus.log);
    CHECK(dev.cs_glitches == 1);
    CHECK(dev.regs[0x01] == 0x03 && dev.regs[0x02] == 0x10 && dev.regs[0x03] == 0x5C);  /* 이웃이 덮임 */
    CHECK(dev.regs[REG_CTRL1] == 0x00);                                                   /* 원래 쓰기는 사라짐 */
    imu_new(&dev, 0); bus_init(&bus, &dev);
    bus_log_start(&bus);
    CHECK(imu_write_reg(&drv, 0x01, 0x03) == IMU_OK && imu_write_reg(&drv, REG_CTRL1, 0x5C) == IMU_OK);
    printf("FIX t_CSW kept     : %s\n", bus.log);
    CHECK(dev.cs_glitches == 0 && dev.regs[0x01] == 0x03 && dev.regs[0x02] == 0x00 && dev.regs[REG_CTRL1] == 0x5C);

    /* ===== Part C: 초기화 ===== */
    imu_new(&dev, 0); bus_init(&bus, &dev);
    bus_log_start(&bus);
    CHECK(imu_init(&drv, &bus, 0, 1, 4) == IMU_OK);
    printf("imu_init log       : %s\n", bus.log);
    CHECK(dev.regs[REG_CTRL1] == 0x44 && dev.regs[REG_CTRL3] == CTRL3_IF_INC);
    /* 누가 IF_INC 를 끄고 설정을 어질러 놓은 칩 → soft reset 이 정리 */
    imu_new(&dev, 0); bus_init(&bus, &dev);
    dev.regs[REG_CTRL3] = 0x00; dev.regs[REG_CTRL2] = 0x77; dev.regs[0x03] = 0x99;
    CHECK(imu_init(&drv, &bus, 0, 3, 9) == IMU_OK);
    CHECK(dev.regs[REG_CTRL1] == 0x9C && dev.regs[REG_CTRL2] == 0 && dev.regs[0x03] == 0);
    imu_new(&dev, 1); bus_init(&bus, &dev);
    CHECK(imu_init(&drv, &bus, 1, 2, 1) == IMU_OK && dev.regs[REG_CTRL1] == 0x18);
    CHECK(imu_init(&drv, &bus, 0, 4, 1) == IMU_E_ARG);
    /* 장애 주입 */
    bus_init(&bus, NULL);
    CHECK(imu_init(&drv, &bus, 0, 0, 1) == IMU_E_NODEV && drv.last_id == 0xFF);
    imu_new(&dev, 0); bus_init(&bus, &dev); bus.fault = BUS_MISO_FLOAT;
    CHECK(imu_init(&drv, &bus, 0, 0, 1) == IMU_E_NODEV);
    imu_new(&dev, 0); bus_init(&bus, &dev); bus.fault = BUS_MISO_LOW;
    CHECK(imu_init(&drv, &bus, 0, 0, 1) == IMU_E_NODEV && drv.last_id == 0x00);
    imu_new(&dev, 0); bus_init(&bus, &dev); bus.fault = BUS_LATE_1BIT;
    CHECK(imu_init(&drv, &bus, 0, 0, 1) == IMU_E_ID && (drv.last_id & 0x7F) == 0x5A);
    printf("late-1bit WHO_AM_I : 0x%02X (expected 0xB5)\n", drv.last_id);
    imu_new(&dev, 0); bus_init(&bus, &dev); dev.regs[REG_WHO_AM_I] = 0x33;
    CHECK(imu_init(&drv, &bus, 0, 0, 1) == IMU_E_ID);
    imu_new(&dev, 0); bus_init(&bus, &dev); dev.write_protect = true;
    CHECK(imu_init(&drv, &bus, 0, 1, 4) == IMU_E_VERIFY);
    imu_new(&dev, 1); bus_init(&bus, &dev);           /* 칩은 더미 1, 드라이버는 0 으로 설정 */
    CHECK(imu_init(&drv, &bus, 0, 0, 1) < 0);
    printf("dummy cfg 0 on dummy-1 chip: id read 0x%02X\n", drv.last_id);
    imu_new(&dev, 0); bus_init(&bus, &dev);           /* 칩은 더미 0, 드라이버는 1 로 설정 */
    CHECK(imu_init(&drv, &bus, 1, 0, 1) == IMU_E_NODEV && drv.last_id == 0x00);

    /* C: 가속도 읽기 */
    imu_new(&dev, 0); bus_init(&bus, &dev);
    CHECK(imu_init(&drv, &bus, 0, 0, 4) == IMU_OK);
    int32_t mg[3] = { 0, 0, 0 };
    CHECK(imu_read_accel_mg(&drv, mg) == IMU_E_NOTREADY);
    imu_set_sample(&dev, 16384, -16384, -1);
    CHECK(imu_read_accel_mg(&drv, mg) == IMU_OK && mg[0] == 1000 && mg[1] == -1000 && mg[2] == 0);
    CHECK(bus.last_len == 8);
    CHECK(imu_read_accel_mg(&drv, mg) == IMU_E_NOTREADY);            /* 같은 샘플을 두 번 받지 않는다 */
    imu_new(&dev, 1); bus_init(&bus, &dev);
    CHECK(imu_init(&drv, &bus, 1, 3, 4) == IMU_OK);
    imu_set_sample(&dev, -32768, 32767, 2);
    CHECK(imu_read_accel_mg(&drv, mg) == IMU_OK && mg[0] == -16000 && mg[1] == 15999 && mg[2] == 0);
    CHECK(bus.last_len == 9);
    {
        int bad = 0;
        for (int t = 0; t < 2000; t++) {
            uint8_t nd = (uint8_t)(xs32(&seed) & 1u), fs = (uint8_t)(xs32(&seed) & 3u);
            imu_new(&dev, nd); bus_init(&bus, &dev);
            if (imu_init(&drv, &bus, nd, fs, 4) != IMU_OK) { bad++; continue; }
            int16_t v[3];
            for (int k = 0; k < 3; k++) v[k] = (int16_t)((int32_t)(xs32(&seed) & 0xFFFFu) - 32768);
            imu_set_sample(&dev, v[0], v[1], v[2]);
            if (imu_read_accel_mg(&drv, mg) != IMU_OK) { bad++; continue; }
            for (int k = 0; k < 3; k++) if (mg[k] != ref_mg(v[k], fs)) { bad++; break; }
        }
        CHECK(bad == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
