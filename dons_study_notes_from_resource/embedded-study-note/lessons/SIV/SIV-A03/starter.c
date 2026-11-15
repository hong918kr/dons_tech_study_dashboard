/* SIV-A03 전력 특성화 자동화 — C 세트: INA226 유사 전력 모니터 드라이버 (mock I2C)
 *
 *   Part A  레지스터 접근(포인터 + 빅엔디안)과 스케일링(shunt/bus/current/power), CAL 계산
 *   Part B  부호, 포화/오버플로, CAL 반올림 오차, NAK 재시도, 바이트 스왑 탐지
 *   Part C  보드 레일 전수 점검: 결함을 심은 레일을 원인별로 FAIL 보고
 *
 *   cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/SIV-A03_so && /tmp/SIV-A03_so
 *
 * INA226 사실(TI 데이터시트 SBOS547): 16비트 빅엔디안, 0x00 Config(POR 0x4127), 0x01 Shunt(부호, 2.5 uV),
 * 0x02 Bus(1.25 mV), 0x03 Power(25 x Current_LSB), 0x04 Current(부호), 0x05 Calibration, 0x06 Mask/Enable,
 * 0xFE Manufacturer 0x5449, 0xFF Die 0x2260. Current = Shunt x CAL / 2048, Power = Current x Bus / 20000.
 * mock의 '가정'(데이터시트가 정하지 않은 부분): 나눗셈은 0쪽 절사, 오버플로는 포화 + OVF 라이브 표시, Power는 |I| 사용.
 * 드라이버는 정수 연산만 쓴다(펌웨어 현실). double은 테스트의 기대값 계산에만 쓴다.
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- 하네스 ---------- */
static int g_total, g_pass;
static void check(int cond, const char *fmt, ...)
{
    va_list ap;
    g_total++;
    if (cond) g_pass++;
    printf("%s ", cond ? "PASS" : "FAIL");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

/* ---------- 레지스터 맵 ---------- */
#define REG_CONFIG 0x00u
#define REG_SHUNT 0x01u
#define REG_BUS 0x02u
#define REG_POWER 0x03u
#define REG_CURRENT 0x04u
#define REG_CAL 0x05u
#define REG_MASK 0x06u
#define REG_MFG 0xFEu
#define REG_DIE 0xFFu

#define INA_MFG_ID 0x5449u
#define INA_DIE_ID 0x2260u
#define INA_CONFIG_POR 0x4127u
#define INA_MASK_OVF (1u << 2)
#define SHUNT_LSB_NV 2500
#define BUS_LSB_UV 1250u
#define POWER_LSB_MULT 25u
#define CAL_MAX 0x7FFFu

/* ---------- I2C 추상화 ---------- */
enum { I2C_OK = 0, I2C_NAK_ADDR = -1, I2C_NAK_DATA = -2 };
typedef int (*i2c_xfer_fn)(void *ctx, uint8_t addr, const uint8_t *tx, size_t ntx, uint8_t *rx, size_t nrx);
typedef struct {
    i2c_xfer_fn xfer;
    void *ctx;
} i2c_bus_t;

/* ---------- mock INA226 (주어진 코드) ---------- */
typedef struct {
    uint8_t addr;
    /* 아날로그 입력: 테스트가 세팅 */
    int64_t vshunt_nv;
    uint32_t vbus_uv;
    /* 레지스터 상태 */
    uint16_t config, cal;
    uint8_t ptr;
    /* 결함 주입 */
    int nak_addr_n;  /* 앞으로 n 트랜잭션 주소 NAK */
    int nak_data_n;  /* 앞으로 n 트랜잭션 데이터 NAK */
    bool byte_swap;  /* 읽기 응답의 MSB/LSB 뒤바뀜 */
    bool cal_ignored;/* CAL 쓰기 무시 */
    uint16_t mfg_override; /* 0이 아니면 다른 소자: MFG 레지스터가 이 값 */
    int brownout_at; /* n번째 트랜잭션 직전에 POR(CAL=0) */
    /* 관측 */
    int xfer_count;
    uint8_t last_tx[4];
    size_t last_ntx;
} mock_ina_t;

typedef struct {
    mock_ina_t *dev;
    size_t n;
} mock_bus_t;

static void mock_por(mock_ina_t *d)
{
    d->config = INA_CONFIG_POR;
    d->cal = 0;
    d->ptr = 0;
}

static void mock_init(mock_ina_t *d, uint8_t addr, int64_t vshunt_nv, uint32_t vbus_uv)
{
    memset(d, 0, sizeof(*d));
    d->addr = addr;
    d->vshunt_nv = vshunt_nv;
    d->vbus_uv = vbus_uv;
    mock_por(d);
}

static int64_t clamp64(int64_t v, int64_t lo, int64_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* 현재 아날로그 입력과 CAL로 결과 레지스터를 계산 */
static void mock_compute(const mock_ina_t *d, uint16_t *shunt, uint16_t *bus, uint16_t *cur, uint16_t *pow, bool *ovf)
{
    int64_t rounded = (d->vshunt_nv >= 0 ? d->vshunt_nv + SHUNT_LSB_NV / 2 : d->vshunt_nv - SHUNT_LSB_NV / 2) / SHUNT_LSB_NV;
    int64_t sh = clamp64(rounded, -32768, 32767);
    int64_t bs = clamp64((int64_t)d->vbus_uv / BUS_LSB_UV, 0, 0x7FFF);
    int64_t i_raw = sh * d->cal / 2048;
    int64_t i = clamp64(i_raw, -32768, 32767);
    int64_t p_raw = (i < 0 ? -i : i) * bs / 20000;
    int64_t p = clamp64(p_raw, 0, 0xFFFF);
    *ovf = (i != i_raw) || (p != p_raw);
    *shunt = (uint16_t)(sh & 0xFFFF);
    *bus = (uint16_t)bs;
    *cur = (uint16_t)(i & 0xFFFF);
    *pow = (uint16_t)p;
}

static uint16_t mock_read_reg(const mock_ina_t *d, uint8_t reg)
{
    uint16_t sh, bs, cu, pw;
    bool ovf;
    mock_compute(d, &sh, &bs, &cu, &pw, &ovf);
    switch (reg) {
    case REG_CONFIG: return d->config;
    case REG_SHUNT: return sh;
    case REG_BUS: return bs;
    case REG_POWER: return pw;
    case REG_CURRENT: return cu;
    case REG_CAL: return d->cal;
    case REG_MASK: return ovf ? INA_MASK_OVF : 0;
    case REG_MFG: return d->mfg_override ? d->mfg_override : INA_MFG_ID;
    case REG_DIE: return INA_DIE_ID;
    default: return 0;
    }
}

static int mock_xfer(void *ctx, uint8_t addr, const uint8_t *tx, size_t ntx, uint8_t *rx, size_t nrx)
{
    mock_bus_t *b = (mock_bus_t *)ctx;
    mock_ina_t *d = NULL;
    for (size_t i = 0; i < b->n; i++)
        if (b->dev[i].addr == addr) d = &b->dev[i];
    if (!d) return I2C_NAK_ADDR;
    d->xfer_count++;
    if (d->brownout_at && d->xfer_count == d->brownout_at) mock_por(d);
    if (d->nak_addr_n > 0) { d->nak_addr_n--; return I2C_NAK_ADDR; }
    if (ntx > 0) {
        if (d->nak_data_n > 0) { d->nak_data_n--; return I2C_NAK_DATA; }
        if (ntx != 1 && ntx != 3) return I2C_NAK_DATA;
        d->ptr = tx[0];
        d->last_ntx = ntx;
        memcpy(d->last_tx, tx, ntx);
        if (ntx == 3) {
            uint16_t v = (uint16_t)((tx[1] << 8) | tx[2]);
            if (d->ptr == REG_CONFIG) {
                if (v & 0x8000u) mock_por(d); else d->config = v;
            } else if (d->ptr == REG_CAL && !d->cal_ignored) {
                d->cal = v & CAL_MAX; /* 비트 15는 예약 */
            }
        }
    }
    if (nrx) {
        uint16_t v = mock_read_reg(d, d->ptr);
        uint8_t hi = (uint8_t)(v >> 8), lo = (uint8_t)(v & 0xFF);
        rx[0] = d->byte_swap ? lo : hi;
        if (nrx > 1) rx[1] = d->byte_swap ? hi : lo;
    }
    return I2C_OK;
}

/* ---------- 드라이버 ---------- */
enum { INA_OK = 0, INA_ERR_NAK = -1, INA_ERR_RANGE = -2, INA_ERR_ID = -3, INA_ERR_ENDIAN = -4 };

#define INA_F_OVF 1u  /* Mask/Enable OVF: current/power 신뢰 불가 */
#define INA_F_SAT 2u  /* shunt 코드가 양/음 풀스케일 */

typedef struct {
    i2c_bus_t bus;
    uint8_t addr;
    uint32_t rshunt_uohm;
    uint32_t lsb_ua;
    uint16_t cal;
    int retries;    /* 재시도 횟수(총 시도 = retries+1) */
    int retry_used; /* 관측: 실제 쓴 재시도 */
} ina226_t;

typedef struct {
    uint16_t shunt_raw, bus_raw, cur_raw, pow_raw, mask_raw;
    int64_t shunt_nv;
    uint32_t bus_uv;
    int64_t current_ua;
    uint64_t power_uw;
    unsigned flags;
} ina_sample_t;

/* C11에서 (int16_t)u16 은 범위 밖이면 구현 정의 — 산술로 해석한다 */
static int32_t as_i16(uint16_t raw) { return raw >= 0x8000u ? (int32_t)raw - 65536 : (int32_t)raw; }

/* A1: 레지스터 읽기. 포인터 1바이트를 쓰고(repeated START) 2바이트를 읽어 MSB 먼저로 조립한다.
 * NAK이면 트랜잭션 전체를 다시 시도(최대 d->retries번). 재시도할 때마다 d->retry_used++.
 * 반환: INA_OK / INA_ERR_NAK */
static int ina_read_reg(ina226_t *d, uint8_t reg, uint16_t *out)
{
    /* TODO A1: 포인터 쓰기 + 2바이트 읽기, 빅엔디안 조립, NAK 재시도 */
    (void)d; (void)reg; (void)out;
    return -99;
}

/* A2: 레지스터 쓰기. {reg, MSB, LSB} 3바이트 한 트랜잭션. 재시도 규칙은 A1과 같다 */
static int ina_write_reg(ina226_t *d, uint8_t reg, uint16_t val)
{
    /* TODO A2: 3바이트 쓰기(MSB 먼저), NAK 재시도 */
    (void)d; (void)reg; (void)val;
    return -99;
}

/* A3: 코드 → 물리량. shunt nV(부호), bus uV, current uA(부호), power uW */
static int64_t ina_shunt_nv(uint16_t raw)
{
    /* TODO A3a: 부호 있는 코드 x 2500 nV */
    (void)as_i16; (void)raw;
    return 0;
}

static uint32_t ina_bus_uv(uint16_t raw)
{
    /* TODO A3b: 부호 없는 코드 x 1250 uV */
    (void)raw;
    return 0;
}

static int64_t ina_current_ua(uint16_t raw, uint32_t lsb_ua)
{
    /* TODO A3c: 부호 있는 코드 x Current_LSB */
    (void)raw; (void)lsb_ua;
    return 0;
}

static uint64_t ina_power_uw(uint16_t raw, uint32_t lsb_ua)
{
    /* TODO A3d: 부호 없는 코드 x 25 x Current_LSB (64비트로 — 32비트면 넘친다) */
    (void)raw; (void)lsb_ua;
    return 0;
}

/* A4: CAL = 0.00512 / (Current_LSB[A] x Rshunt[ohm]) = 5.12e9 / (lsb_ua x rshunt_uohm).
 * 가장 가까운 정수로 반올림. 분모 0, 결과 0, 결과 > 0x7FFF → INA_ERR_RANGE */
static int ina_compute_cal(uint32_t lsb_ua, uint32_t rshunt_uohm, uint16_t *cal)
{
    /* TODO A4: 64비트 정수로 반올림 나눗셈, 범위 검사 */
    (void)lsb_ua; (void)rshunt_uohm; (void)cal;
    return -99;
}

/* B1: CAL 반올림이 만드는 전류 이득 오차 [ppm] = (cal - exact) / exact x 1e6 (exact = 5.12e9/den). 테스트 전용 double */
static double ina_cal_error_ppm(uint32_t lsb_ua, uint32_t rshunt_uohm, uint16_t cal)
{
    /* TODO B1: 이득 오차 ppm 계산 */
    (void)lsb_ua; (void)rshunt_uohm; (void)cal;
    return 1e9;
}

/* B2: 프로브. MFG를 읽어 0x5449면 통과, 0x4954면(바이트 스왑) INA_ERR_ENDIAN, 그 외 INA_ERR_ID.
 * 이어서 DIE == 0x2260 확인(아니면 INA_ERR_ID). NAK은 INA_ERR_NAK */
static int ina_probe(ina226_t *d)
{
    /* TODO B2: MFG/DIE 읽어 ID와 엔디안 판정 */
    (void)d;
    return -99;
}

/* B3: CAL 계산 → 쓰기. dev->cal을 갱신. 오류는 그대로 전파 */
static int ina_calibrate(ina226_t *d)
{
    /* TODO B3: ina_compute_cal 후 REG_CAL에 쓰기 */
    (void)d;
    return -99;
}

/* B4: SHUNT, BUS, CURRENT, POWER, MASK 순서로 읽어 물리량과 flags를 채운다.
 * flags: MASK에 OVF 있으면 INA_F_OVF, shunt 코드가 0x7FFF 또는 0x8000이면 INA_F_SAT.
 * 레지스터 읽기 실패는 그 오류를 그대로 반환 */
static int ina_read_sample(ina226_t *d, ina_sample_t *s)
{
    /* TODO B4: 5개 레지스터 읽기, 변환, 플래그 판정 */
    (void)d; (void)s;
    return -99;
}

/* ---------- Part C: 레일 전수 점검 ---------- */
typedef enum { V_PASS, V_NAK, V_ID, V_ENDIAN, V_CAL_READBACK, V_RANGE, V_CAL_LOST, V_GAIN } verdict_t;
static const char *const verdict_name[] = {"PASS", "NAK", "ID", "ENDIAN", "CAL_READBACK", "RANGE", "CAL_LOST", "GAIN"};

typedef struct {
    const char *name;
    uint8_t addr;
    uint32_t rshunt_uohm; /* 도면 값 — 실장 값과 다를 수 있다 */
    uint32_t lsb_ua;
    int64_t dmm_ua;       /* 벤치 기준 전류(DMM/전류 프로브) */
} rail_cfg_t;

typedef struct {
    verdict_t verdict;
    int64_t ina_ua;
    double ratio; /* ina/dmm, GAIN 판정 때 */
    char line[160];
} rail_result_t;

/* GAIN 허용: 기준의 2% + 전류 3 LSB. 2%는 대표값(분류용 — 정밀 특성화 기준 아님) */
static bool gain_ok(int64_t ina_ua, int64_t dmm_ua, uint32_t lsb_ua)
{
    int64_t diff = ina_ua - dmm_ua;
    int64_t abs_dmm = dmm_ua < 0 ? -dmm_ua : dmm_ua;
    if (diff < 0) diff = -diff;
    return diff * 100 <= abs_dmm * 2 + (int64_t)lsb_ua * 300;
}

/* C1: 레일 하나를 점검해 판정을 낸다. 순서가 곧 진단이다:
 *   ina_probe → NAK / ENDIAN / ID
 *   ina_calibrate(NAK → V_NAK, 범위 오류 → V_RANGE) 후 CAL 되읽기, 다르면 V_CAL_READBACK
 *   ina_read_sample(실패 → V_NAK); OVF 또는 SAT → V_RANGE
 *   CAL을 다시 읽어 0이면(또는 기대값과 다르면) V_CAL_LOST — 측정 중 브라운아웃
 *   gain_ok 실패 → V_GAIN, r->ratio = ina/dmm
 * r->ina_ua 는 측정이 된 경우 채운다. r->line 은 다음 형식:
 *   "PASS rail=VDD_CPU addr=0x40 i_ma=2500"
 *   "FAIL rail=VDD_CPU addr=0x40 reason=GAIN i_ma=1250 dmm_ma=2500 ratio=0.500" (GAIN일 때만 i_ma/dmm_ma/ratio)
 *   "FAIL rail=VDD_CPU addr=0x40 reason=NAK" */
static void survey_rail(i2c_bus_t bus, const rail_cfg_t *c, rail_result_t *r)
{
    memset(r, 0, sizeof(*r));
    /* TODO C1: 진단 순서대로 판정하고 한 줄 보고를 만든다 */
    (void)bus; (void)c; (void)verdict_name; (void)gain_ok;
    r->verdict = V_ID;
    snprintf(r->line, sizeof r->line, "TODO");
}

/* ---------- 테스트 ---------- */
static uint32_t xs_state = 0x1234ABCDu;
static uint32_t xs32(void)
{
    uint32_t x = xs_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return xs_state = x;
}

static ina226_t mkdev(mock_bus_t *b, uint8_t addr, uint32_t r, uint32_t lsb)
{
    ina226_t d = {{mock_xfer, b}, addr, r, lsb, 0, 0, 0};
    return d;
}

static void test_part_a(void)
{
    puts("--- Part A: 레지스터 접근과 스케일링");
    mock_ina_t m;
    mock_bus_t b = {&m, 1};
    mock_init(&m, 0x40, 5000000, 12000000);
    ina226_t d = mkdev(&b, 0x40, 10000, 500);
    uint16_t v = 0;
    int rc = ina_read_reg(&d, REG_MFG, &v);
    check(rc == INA_OK && v == 0x5449, "read MFG = 0x5449 (act 0x%04X rc %d)", v, rc);
    rc = ina_read_reg(&d, REG_DIE, &v);
    check(rc == INA_OK && v == 0x2260, "read DIE = 0x2260 (act 0x%04X rc %d)", v, rc);
    rc = ina_read_reg(&d, REG_CONFIG, &v);
    check(rc == INA_OK && v == 0x4127, "POR Config = 0x4127 (act 0x%04X)", v);
    rc = ina_write_reg(&d, REG_CAL, 0x0A00);
    check(rc == INA_OK && m.last_ntx == 3 && m.last_tx[0] == 0x05 && m.last_tx[1] == 0x0A && m.last_tx[2] == 0x00,
          "write CAL = {05,0A,00} MSB first (act %02X %02X %02X)", m.last_tx[0], m.last_tx[1], m.last_tx[2]);
    check(m.cal == 0x0A00, "mock CAL = 0x0A00 (act 0x%04X)", m.cal);

    static const struct { uint16_t raw; int64_t nv; } sh[] = {
        {0x0000, 0}, {0x0001, 2500}, {0x7FFF, 81917500}, {0x8000, -81920000}, {0xFFFF, -2500}, {0xF830, -5000000}};
    for (size_t i = 0; i < sizeof sh / sizeof sh[0]; i++)
        check(ina_shunt_nv(sh[i].raw) == sh[i].nv, "shunt 0x%04X -> %" PRId64 " nV (act %" PRId64 ")", sh[i].raw, sh[i].nv, ina_shunt_nv(sh[i].raw));
    static const struct { uint16_t raw; uint32_t uv; } bs[] = {{0, 0}, {1, 1250}, {0x2580, 12000000}, {0x7FFF, 40958750}};
    for (size_t i = 0; i < sizeof bs / sizeof bs[0]; i++)
        check(ina_bus_uv(bs[i].raw) == bs[i].uv, "bus 0x%04X -> %u uV (act %u)", bs[i].raw, bs[i].uv, ina_bus_uv(bs[i].raw));
    check(ina_current_ua(0xFFFF, 500) == -500, "current -1 code x 500uA = -500 uA");
    check(ina_current_ua(0x7FFF, 1000) == 32767000, "current max x 1mA = 32767000 uA");
    check(ina_power_uw(1, 500) == 12500, "power LSB = 25 x 500uA = 12500 uW");
    check(ina_power_uw(0xFFFF, 1000000) == 1638375000000ULL, "power max with 1A LSB needs 64-bit");

    static const struct { uint32_t lsb, r; uint16_t cal; } cals[] = {
        {1000, 2000, 2560}, {500, 10000, 1024}, {100, 10000, 5120}, {1000, 100000, 51}, {457, 2000, 5602}};
    for (size_t i = 0; i < sizeof cals / sizeof cals[0]; i++) {
        uint16_t c = 0;
        rc = ina_compute_cal(cals[i].lsb, cals[i].r, &c);
        check(rc == INA_OK && c == cals[i].cal, "CAL(lsb=%uuA, R=%uuohm) = %u (act %u rc %d)", cals[i].lsb, cals[i].r, cals[i].cal, c, rc);
    }

    /* 왕복: 정규화 난수로 아날로그 입력 → 드라이버 읽기 → 기대값(double 독립 계산)과 비교.
     * 허용 오차: shunt ±1/2 LSB=1250 nV (코드화 양자화), bus ±1 LSB=1250 uV(mock 절사),
     * power는 P_LSB(12.5 mW) + I 양자화 x Vbus + V 양자화 x I 의 합 상계(25 mW). */
    int bad_sh = 0, bad_bus = 0, bad_p = 0;
    ina_compute_cal(500, 10000, &d.cal);
    ina_write_reg(&d, REG_CAL, d.cal);
    for (int i = 0; i < 300; i++) {
        int64_t nv = (int64_t)(xs32() % 100000000u) - 50000000; /* ±50 mV */
        uint32_t uv = xs32() % 12000001u;
        m.vshunt_nv = nv;
        m.vbus_uv = uv;
        ina_sample_t s;
        rc = ina_read_sample(&d, &s);
        double i_exact_a = (double)nv * 1e-9 / 0.01;
        double p_exact_w = (i_exact_a < 0 ? -i_exact_a : i_exact_a) * (double)uv * 1e-6;
        double p_got_w = (double)s.power_uw * 1e-6;
        if (rc != INA_OK || llabs(s.shunt_nv - nv) > 1250) bad_sh++;
        if (rc != INA_OK || (int64_t)uv - (int64_t)s.bus_uv > 1250 || (int64_t)uv < (int64_t)s.bus_uv) bad_bus++;
        double dp = p_got_w - p_exact_w;
        if (rc != INA_OK || dp > 0.025 || dp < -0.025) bad_p++;
    }
    check(bad_sh == 0, "300 random shunt reads within 1/2 LSB (bad %d)", bad_sh);
    check(bad_bus == 0, "300 random bus reads within 1 LSB (bad %d)", bad_bus);
    check(bad_p == 0, "300 random power reads within 25 mW bound (bad %d)", bad_p);
}

static void test_part_b(void)
{
    puts("--- Part B: 부호, 포화, CAL 반올림, NAK, 바이트 스왑");
    mock_ina_t m;
    mock_bus_t b = {&m, 1};
    mock_init(&m, 0x40, 0, 12000000);
    ina226_t d = mkdev(&b, 0x40, 10000, 500);
    int rc = ina_calibrate(&d);
    check(rc == INA_OK && d.cal == 1024 && m.cal == 1024, "ina_calibrate writes CAL 1024 (act dev %u mock %u rc %d)", d.cal, m.cal, rc);

    ina_sample_t s;
    m.vshunt_nv = -5000000; /* -5 mV → 역방향 -0.5 A */
    rc = ina_read_sample(&d, &s);
    check(rc == INA_OK && s.shunt_nv == -5000000, "negative shunt -5 mV decoded (act %" PRId64 ")", s.shunt_nv);
    check(rc == INA_OK && s.current_ua == -500000, "negative current -500 mA (act %" PRId64 ")", s.current_ua);
    check(rc == INA_OK && s.power_uw == 6000000, "power unsigned 6 W magnitude (act %" PRIu64 ")", s.power_uw);

    m.vshunt_nv = 100000000; /* 100 mV > 81.92 mV FS */
    rc = ina_read_sample(&d, &s);
    check(rc == INA_OK && (s.flags & INA_F_SAT) && s.shunt_raw == 0x7FFF, "shunt saturates at +FS, SAT flag (raw 0x%04X flags %u)", s.shunt_raw, s.flags);
    m.vshunt_nv = -100000000;
    rc = ina_read_sample(&d, &s);
    check(rc == INA_OK && (s.flags & INA_F_SAT) && s.shunt_raw == 0x8000, "shunt saturates at -FS, SAT flag (raw 0x%04X flags %u)", s.shunt_raw, s.flags);

    /* CAL을 크게 잡으면 shunt는 범위 안이어도 current 레지스터가 넘친다: 40 mV x 5120/2048 = 40000 > 32767 */
    d.lsb_ua = 100;
    d.rshunt_uohm = 10000;
    rc = ina_calibrate(&d);
    m.vshunt_nv = 40000000;
    int rc2 = ina_read_sample(&d, &s);
    check(rc == INA_OK && rc2 == INA_OK && (s.flags & INA_F_OVF) && !(s.flags & INA_F_SAT),
          "current overflow flagged while shunt in range (flags %u)", s.flags);
    m.vshunt_nv = 0;
    d.lsb_ua = 500;
    ina_calibrate(&d);

    /* CAL 범위 */
    uint16_t c = 0;
    check(ina_compute_cal(100, 1000, &c) == INA_ERR_RANGE, "CAL 51200 > 0x7FFF -> RANGE");
    check(ina_compute_cal(0, 1000, &c) == INA_ERR_RANGE, "lsb=0 -> RANGE");
    check(ina_compute_cal(1000000, 10000000, &c) == INA_ERR_RANGE, "CAL rounds to 0 -> RANGE");

    /* 반올림 오차 표: 큰 CAL은 0.5/CAL 이하로 작고, 작은 CAL은 커진다 */
    static const struct { uint32_t lsb, r; double max_abs_ppm; } tbl[] = {
        {1000, 2000, 1.0}, {457, 2000, 90.0}, {1000, 100000, 3907.0}, {1000000, 10000, 953126.0}};
    for (size_t i = 0; i < sizeof tbl / sizeof tbl[0]; i++) {
        rc = ina_compute_cal(tbl[i].lsb, tbl[i].r, &c);
        double err = rc == INA_OK ? ina_cal_error_ppm(tbl[i].lsb, tbl[i].r, c) : 1e12;
        double exact = 5.12e9 / ((double)tbl[i].lsb * (double)tbl[i].r);
        double bound = 500000.0 / exact; /* 반올림 상한: |cal-exact|<=0.5 → 0.5/exact */
        check(err >= -bound - 1e-6 && err <= bound + 1e-6 && (err < 0 ? -err : err) <= tbl[i].max_abs_ppm,
              "CAL err lsb=%u R=%u cal=%u err=%.1f ppm (bound %.1f)", tbl[i].lsb, tbl[i].r, c, err, bound);
    }

    /* NAK: 일시 → 재시도로 복구, 값이 맞아야 한다(포인터도 다시 써야 함) */
    uint16_t v = 0;
    d.retries = 2;
    d.retry_used = 0;
    m.nak_data_n = 1;
    rc = ina_read_reg(&d, REG_MFG, &v);
    check(rc == INA_OK && v == 0x5449 && d.retry_used == 1, "transient data NAK recovers (v 0x%04X retries %d)", v, d.retry_used);
    m.nak_addr_n = 2;
    d.retry_used = 0;
    rc = ina_read_reg(&d, REG_DIE, &v);
    check(rc == INA_OK && v == 0x2260 && d.retry_used == 2, "2 addr NAKs recover with retries=2 (retries %d)", d.retry_used);
    m.nak_addr_n = 3;
    d.retry_used = 0;
    rc = ina_read_reg(&d, REG_DIE, &v);
    check(rc == INA_ERR_NAK && d.retry_used == 2, "3 NAKs exhaust retries=2 (rc %d used %d)", rc, d.retry_used);
    m.nak_data_n = 5;
    rc = ina_write_reg(&d, REG_CONFIG, 0x4127);
    check(rc == INA_ERR_NAK, "persistent write NAK reported (rc %d)", rc);
    m.nak_data_n = 0;
    d.retries = 0;

    /* 바이트 스왑 */
    mock_init(&m, 0x40, 0, 12000000);
    rc = ina_probe(&d);
    check(rc == INA_OK, "probe good part OK (rc %d)", rc);
    m.byte_swap = true;
    rc = ina_probe(&d);
    check(rc == INA_ERR_ENDIAN, "byte-swapped bus -> ENDIAN (rc %d)", rc);

    /* 한계 측정: 값 범위 검사(|I| < 10 A)는 스왑을 놓친다. ID 검사는 고정 패턴이라 항상 잡는다 */
    int missed = 0, trials = 2000;
    for (int i = 0; i < trials; i++) {
        int32_t code = (int32_t)(xs32() % 8000u) + 100; /* 정상 0.05~4 A (500uA LSB) */
        uint16_t raw = (uint16_t)code;
        uint16_t swapped = (uint16_t)((raw >> 8) | (raw << 8));
        int64_t ua = ina_current_ua(swapped, 500);
        if (ua > -10000000 && ua < 10000000) missed++;
    }
    printf("  [limit] range-check missed %d/%d byte-swapped currents (10 A window, 500uA LSB)\n", missed, trials);
    check(missed > 0, "range check alone misses some byte swaps (missed %d/%d)", missed, trials);
    check(ina_probe(&d) == INA_ERR_ENDIAN, "ID check still catches the swap");

    /* 대조군: CAL 1024(이득 1/2)면 81 mV에서도 current는 범위 안이고 플래그 없음 */
    mock_init(&m, 0x40, 81000000, 12000000);
    d.lsb_ua = 500; d.rshunt_uohm = 10000; d.retries = 0;
    ina_calibrate(&d);
    rc = ina_read_sample(&d, &s);
    check(rc == INA_OK && s.flags == 0 && s.current_ua == 16200 * 500, "81 mV with CAL 1024 no flags (flags %u, %" PRId64 " uA)", s.flags, s.current_ua);
}

#define NR 5
static void setup_good(mock_ina_t m[NR], const rail_cfg_t cfg[NR])
{
    for (int i = 0; i < NR; i++) {
        /* 양호 보드: 실장 R == 도면 R, 기준 전류 그대로 */
        int64_t nv = cfg[i].dmm_ua * (int64_t)cfg[i].rshunt_uohm / 1000000 * 1000;
        mock_init(&m[i], cfg[i].addr, nv, 900000);
    }
}

static void test_part_c(void)
{
    puts("--- Part C: 레일 전수 점검 (mock 보드 SENSE-EVB rev B)");
    static const rail_cfg_t cfg[NR] = {
        {"VDD_CPU",  0x40, 10000, 500, 2500000},
        {"VDD_GPU",  0x41, 5000,  500, 3000000},
        {"VDD_DDR",  0x42, 10000, 200, 1200000},
        {"VDD_IO",   0x43, 20000, 100, 300000},
        {"VDD_AON",  0x44, 50000, 50,  40000},
    };
    mock_ina_t m[NR];
    mock_bus_t b = {m, NR};
    i2c_bus_t bus = {mock_xfer, &b};
    rail_result_t r;
    int ok = 1;

    setup_good(m, cfg);
    for (int i = 0; i < NR; i++) {
        survey_rail(bus, &cfg[i], &r);
        printf("  %s\n", r.line);
        if (r.verdict != V_PASS) ok = 0;
    }
    check(ok, "good board: all 5 rails PASS");

    /* 결함 1: 주소 NAK (납땜 불량 등 — 소자 없음) */
    setup_good(m, cfg);
    m[3].addr = 0x4F;
    survey_rail(bus, &cfg[3], &r);
    printf("  %s\n", r.line);
    check(r.verdict == V_NAK && strstr(r.line, "rail=VDD_IO"), "open/missing part: NAK on VDD_IO");

    /* 결함 2: 측정 경로 바이트 스왑(브리지 펌웨어) */
    setup_good(m, cfg);
    m[0].byte_swap = true;
    survey_rail(bus, &cfg[0], &r);
    printf("  %s\n", r.line);
    check(r.verdict == V_ENDIAN, "byte swap on VDD_CPU -> ENDIAN");

    /* 결함 3a: 같은 주소에 다른 소자(주소 충돌/BOM 치환) → ID 불일치 */
    setup_good(m, cfg);
    m[1].mfg_override = 0x1234;
    survey_rail(bus, &cfg[1], &r);
    printf("  %s\n", r.line);
    check(r.verdict == V_ID, "foreign device at address -> ID on VDD_GPU");

    /* 결함 3b: CAL 쓰기가 안 먹음 (mock 결함: 쓰기 무시) */
    setup_good(m, cfg);
    m[1].cal_ignored = true;
    survey_rail(bus, &cfg[1], &r);
    printf("  %s\n", r.line);
    check(r.verdict == V_CAL_READBACK, "CAL write ignored -> CAL_READBACK on VDD_GPU");

    /* 결함 4: shunt 범위 초과 (R 도면 오류로 전압이 FS 넘음) */
    setup_good(m, cfg);
    m[2].vshunt_nv = 90000000;
    survey_rail(bus, &cfg[2], &r);
    printf("  %s\n", r.line);
    check(r.verdict == V_RANGE, "shunt over FS -> RANGE on VDD_DDR");

    /* 결함 5: 측정 중 브라운아웃: CAL 되읽기 후 → CAL 0 → current 0 */
    setup_good(m, cfg);
    m[0].brownout_at = 7; /* 트랜잭션 1 MFG, 2 DIE, 3 CAL쓰기, 4 CAL읽기, 5 SHUNT, 6 BUS, 7 CURRENT 직전에 POR */
    survey_rail(bus, &cfg[0], &r);
    printf("  %s\n", r.line);
    check(r.verdict == V_CAL_LOST, "brownout mid-sample -> CAL_LOST on VDD_CPU");

    /* 결함 6: 실장 shunt가 도면의 절반 (10 mΩ 자리에 5 mΩ) → 전류가 절반으로 읽힘 */
    setup_good(m, cfg);
    m[4].vshunt_nv = cfg[4].dmm_ua * 25000 / 1000000 * 1000; /* 50 mΩ 자리에 25 mΩ */
    survey_rail(bus, &cfg[4], &r);
    printf("  %s\n", r.line);
    check(r.verdict == V_GAIN && r.ratio > 0.45 && r.ratio < 0.55, "half-value shunt -> GAIN ratio~0.5 (ratio %.3f)", r.ratio);

    /* 한계: 기준 전류가 2% 이내로 틀린 shunt(1% 저항 공차 뒤집힘)는 이 분류로 잡히지 않는다 */
    setup_good(m, cfg);
    m[0].vshunt_nv = cfg[0].dmm_ua * 10150 / 1000000 * 1000; /* +1.5% */
    survey_rail(bus, &cfg[0], &r);
    printf("  [limit] %s\n", r.line);
    check(r.verdict == V_PASS, "1.5%% shunt tolerance error passes the 2%% screen (limit, by design)");
}

int main(void)
{
    test_part_a();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
