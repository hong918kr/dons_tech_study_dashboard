/*
 * E1  I2C Temperature Sensor Driver (mock HAL)
 *
 * Write a small driver for a TMP102-like I2C temperature sensor on top of the HAL below.
 * The HAL is implemented at the bottom of this file as a FAKE bus (register array +
 * fault injection). Do not edit the fake; only use the HAL functions.
 *
 *   int i2c_write(addr7, buf, len)                       START addr+W buf[0..len) STOP
 *   int i2c_write_read(addr7, wbuf, wlen, rbuf, rlen)    START addr+W wbuf Sr addr+R rbuf STOP
 *       -> 0 on success, -EIO on NACK, -ETIMEDOUT if the bus is stuck busy
 *   uint32_t millis(void)   free-running ms counter (wraps at 2^32)
 *   void delay_ms(ms)       busy-wait (advances the fake clock)
 *
 * Register map (device address 0x48, register pointer = first written byte):
 *   0x00 TEMP     2 B  RO  12-bit two's complement, left-justified, MSB first, 0.0625 degC/LSB
 *   0x01 CONFIG   1 B  RW  b7 OS (one-shot, self-clearing) | b6 reserved | b5 POL | b4 TM |
 *                          b3 reserved | b2:1 RATE | b0 SD (shutdown)
 *   0x02 T_LOW    2 B  RW  same format as TEMP
 *   0x03 T_HIGH   2 B  RW  same format as TEMP
 *   0x04 STATUS   1 B  RO  b0 DATA_READY (set when a conversion finishes, cleared by reading TEMP)
 *   0x0F WHO_AM_I 1 B  RO  always 0xA1
 *
 * Implement:
 *   tmp_init      check WHO_AM_I (-ENODEV if wrong, no retry); read-modify-write CONFIG:
 *                 set RATE = rate and SD = 1, keep every other bit; cache CONFIG in d->cfg.
 *                 rate > 3 -> -EINVAL.
 *   tmp_read_c100 one-shot: write CONFIG with OS=1, poll STATUS.DATA_READY every TMP_POLL_MS
 *                 until it is set or TMP_TIMEOUT_MS has elapsed (-ETIMEDOUT; wrap-safe),
 *                 then burst-read TEMP (2 bytes, one transaction) and convert to 0.01 degC.
 *   tmp_set_alert encode thresholds (0.01 degC -> register, round half away from zero),
 *                 require low < high (-EINVAL), each must fit 12 bits (-ERANGE).
 *   Retry policy: every bus transfer is retried up to TMP_MAX_RETRIES times on -EIO only.
 *                 -ETIMEDOUT from the bus is returned immediately. Return 0 or -errno.
 *
 * Build & run:  ./run.sh E1
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

/* ---------------- HAL (provided) ---------------- */
int i2c_write(uint8_t addr7, const uint8_t *buf, size_t len);
int i2c_write_read(uint8_t addr7, const uint8_t *wbuf, size_t wlen, uint8_t *rbuf, size_t rlen);
uint32_t millis(void);
void delay_ms(uint32_t ms);

/* ---------------- device definitions (from the datasheet) ---------------- */
#define TMP_ADDR_DEFAULT  0x48u
#define REG_TEMP          0x00u
#define REG_CONFIG        0x01u
#define REG_T_LOW         0x02u
#define REG_T_HIGH        0x03u
#define REG_STATUS        0x04u
#define REG_WHO_AM_I      0x0Fu
#define WHO_AM_I_VAL      0xA1u

#define CFG_SD            (1u << 0)
#define CFG_RATE_SHIFT    1
#define CFG_RATE_MASK     (3u << CFG_RATE_SHIFT)
#define CFG_POL           (1u << 5)
#define CFG_OS            (1u << 7)
#define STATUS_DRDY       (1u << 0)

#define TMP_MAX_RETRIES   3      /* retries after the first attempt, -EIO only */
#define TMP_POLL_MS       5
#define TMP_TIMEOUT_MS    100

typedef struct {
    uint8_t addr;     /* 7-bit address */
    uint8_t cfg;      /* shadow copy of CONFIG (OS bit always 0 here) */
    bool    ready;    /* tmp_init succeeded */
} TmpDev;

/* Provided helper (same as T1-5): two TEMP-format bytes -> 0.01 degC, half away from 0. */
static int16_t tmp_raw_to_c100(uint8_t msb, uint8_t lsb)
{
    int16_t s16 = (int16_t)(uint16_t)(((uint16_t)msb << 8) | (lsb & 0xF0u));
    int32_t num = (int32_t)(s16 / 16) * 625;
    return (int16_t)((num >= 0) ? (num + 50) / 100 : (num - 50) / 100);
}

/* ---------------- driver: your code ---------------- */

int tmp_init(TmpDev *d, uint8_t addr, uint8_t rate)
{
    /* TODO: check WHO_AM_I, read-modify-write CONFIG (RATE, SD), cache it in d->cfg */
    (void)d; (void)addr; (void)rate;
    return -ENOSYS;
}

int tmp_read_c100(TmpDev *d, int16_t *out)
{
    /* TODO: one-shot, poll DATA_READY with a wrap-safe timeout, burst-read TEMP, convert */
    (void)d; (void)out;
    return -ENOSYS;
}

int tmp_set_alert(TmpDev *d, int16_t high_c100, int16_t low_c100)
{
    /* TODO: validate, encode both thresholds, then write T_HIGH and T_LOW */
    (void)d; (void)high_c100; (void)low_c100;
    return -ENOSYS;
}

/* =====================================================================
 * Test harness: fake I2C bus + fake clock (do not edit)
 * ===================================================================== */
static struct {
    uint16_t reg[16];        /* register file; 1-byte regs use the low byte */
    uint32_t now;            /* fake millis() */
    uint32_t xfers;          /* transfers attempted (1-based index of the last one) */
    uint32_t nack_at, nack_len;  /* transfers nack_at .. nack_at+nack_len-1 NACK */
    bool     busy;           /* bus stuck: every transfer -> -ETIMEDOUT */
    bool     drdy_never;     /* conversion never completes */
    uint32_t conv_ms;        /* conversion time */
    bool     conv_pending, drdy;
    uint32_t conv_start;
} F;

static size_t reg_width(uint8_t r) { return (r == REG_TEMP || r == REG_T_LOW || r == REG_T_HIGH) ? 2 : 1; }

static void fake_reset(uint32_t now, uint8_t config)
{
    memset(&F, 0, sizeof F);
    F.now = now;
    F.conv_ms = 30;
    F.reg[REG_CONFIG] = config;
    F.reg[REG_WHO_AM_I] = WHO_AM_I_VAL;
    F.reg[REG_TEMP] = 0x1900;            /* 25.0 degC */
}

static int fake_begin(uint8_t addr7)
{
    F.xfers++;
    if (F.busy) return -ETIMEDOUT;
    if (F.nack_len && F.xfers >= F.nack_at && F.xfers - F.nack_at < F.nack_len) return -EIO;
    if (addr7 != TMP_ADDR_DEFAULT) return -EIO;
    return 0;
}

uint32_t millis(void) { return F.now; }
void delay_ms(uint32_t ms) { F.now += ms; }

int i2c_write(uint8_t addr7, const uint8_t *buf, size_t len)
{
    int rc = fake_begin(addr7);
    if (rc) return rc;
    if (len < 1 || buf[0] > 0x0F) return -EIO;
    uint8_t r = buf[0];
    if (len == 1) return 0;                          /* pointer-only write */
    if (len - 1 != reg_width(r)) return -EIO;        /* device NACKs a wrong length */
    uint16_t v = (reg_width(r) == 2) ? (uint16_t)((buf[1] << 8) | buf[2]) : buf[1];
    if (r == REG_CONFIG) {
        if (v & CFG_OS) { F.conv_pending = true; F.conv_start = F.now; F.drdy = false; }
        F.reg[r] = (uint16_t)(v & ~CFG_OS);          /* OS self-clears */
    } else if (r == REG_T_LOW || r == REG_T_HIGH) {
        F.reg[r] = v;
    }                                                /* RO registers ignore writes */
    return 0;
}

int i2c_write_read(uint8_t addr7, const uint8_t *wbuf, size_t wlen, uint8_t *rbuf, size_t rlen)
{
    int rc = fake_begin(addr7);
    if (rc) return rc;
    if (wlen != 1 || wbuf[0] > 0x0F || rlen < 1 || rlen > reg_width(wbuf[0])) return -EIO;
    uint8_t r = wbuf[0];
    if (r == REG_STATUS) {
        if (F.conv_pending && !F.drdy_never && F.now - F.conv_start >= F.conv_ms) {
            F.conv_pending = false; F.drdy = true;
        }
        F.reg[REG_STATUS] = F.drdy ? STATUS_DRDY : 0;
    }
    uint16_t v = F.reg[r];
    if (reg_width(r) == 2) { rbuf[0] = (uint8_t)(v >> 8); if (rlen > 1) rbuf[1] = (uint8_t)v; }
    else rbuf[0] = (uint8_t)v;
    if (r == REG_TEMP) F.drdy = false;               /* reading TEMP clears DATA_READY */
    return 0;
}

int main(void)
{
    TmpDev d = {0};
    int16_t t = 0;

    /* 1. normal init + one-shot read (25.0 degC); count transfers and elapsed time */
    fake_reset(1000, 0x00);
    CHECK(tmp_init(&d, 0x48, 2) == 0);
    CHECK(F.xfers == 3);                               /* WHO_AM_I, CONFIG read, CONFIG write */
    CHECK(F.reg[REG_CONFIG] == 0x05);                  /* RATE=2, SD=1 */
    F.xfers = 0;
    CHECK(tmp_read_c100(&d, &t) == 0 && t == 2500);
    CHECK(F.xfers == 9 && F.now == 1030);              /* 1 write + 7 STATUS polls + 1 TEMP */
    CHECK(F.reg[REG_CONFIG] == 0x05);                  /* OS bit self-cleared */

    /* 2. negative temperatures */
    F.reg[REG_TEMP] = 0xE700;                          /* -25.0 degC */
    CHECK(tmp_read_c100(&d, &t) == 0 && t == -2500);
    F.reg[REG_TEMP] = 0xFFF0;                          /* -0.0625 degC */
    CHECK(tmp_read_c100(&d, &t) == 0 && t == -6);

    /* 3. read-modify-write keeps POL, TM and reserved bits */
    fake_reset(0, 0x7A);                               /* POL|TM|res|res, RATE=1, SD=0 */
    CHECK(tmp_init(&d, 0x48, 2) == 0);
    CHECK(F.reg[REG_CONFIG] == 0x7D && d.cfg == 0x7D);
    CHECK(tmp_init(&d, 0x48, 4) == -EINVAL);

    /* 4. NACK once -> retried, read still succeeds */
    fake_reset(0, 0x00);
    CHECK(tmp_init(&d, 0x48, 2) == 0);
    F.xfers = 0; F.nack_at = 1; F.nack_len = 1;        /* the one-shot write NACKs once */
    CHECK(tmp_read_c100(&d, &t) == 0 && t == 2500);
    CHECK(F.xfers == 10);

    /* 5. NACK 3 times -> still OK (3 retries); NACK forever -> -EIO after 1 + 3 attempts */
    fake_reset(0, 0x00); F.nack_at = 1; F.nack_len = 3;
    CHECK(tmp_init(&d, 0x48, 2) == 0 && F.xfers == 6);
    fake_reset(0, 0x00); F.nack_at = 1; F.nack_len = 1000;
    CHECK(tmp_init(&d, 0x48, 2) == -EIO && F.xfers == 4);

    /* 6. bus stuck busy -> -ETIMEDOUT immediately, no retry */
    fake_reset(0, 0x00); F.busy = true;
    CHECK(tmp_init(&d, 0x48, 2) == -ETIMEDOUT && F.xfers == 1);

    /* 7. wrong WHO_AM_I -> -ENODEV, no retry, CONFIG untouched */
    fake_reset(0, 0x00); F.reg[REG_WHO_AM_I] = 0x75;
    CHECK(tmp_init(&d, 0x48, 2) == -ENODEV && F.xfers == 1);
    CHECK(tmp_read_c100(&d, &t) == -ENODEV);

    /* 8. DATA_READY never set -> -ETIMEDOUT after exactly TMP_TIMEOUT_MS */
    fake_reset(5000, 0x00);
    CHECK(tmp_init(&d, 0x48, 2) == 0);
    F.drdy_never = true; F.xfers = 0;
    CHECK(tmp_read_c100(&d, &t) == -ETIMEDOUT);
    CHECK(F.now == 5100 && F.xfers == 22);             /* 1 write + 21 STATUS polls */

    /* 9. millis() wraps during the poll: must not time out early ... */
    fake_reset(0xFFFFFFF0u, 0x00);
    CHECK(tmp_init(&d, 0x48, 2) == 0);
    F.reg[REG_TEMP] = 0x5000;                          /* 80.0 degC */
    CHECK(tmp_read_c100(&d, &t) == 0 && t == 8000);
    CHECK(F.now == 0x0000000Eu);                       /* 0xFFFFFFF0 + 30 */
    /* ... and must still time out after 100 ms across the wrap */
    fake_reset(0xFFFFFFC0u, 0x00);
    CHECK(tmp_init(&d, 0x48, 2) == 0);
    F.drdy_never = true;
    CHECK(tmp_read_c100(&d, &t) == -ETIMEDOUT && F.now == 0x00000024u);

    /* 10. alert thresholds: encode, round-trip, validation */
    fake_reset(0, 0x00);
    CHECK(tmp_init(&d, 0x48, 2) == 0);
    CHECK(tmp_set_alert(&d, 8000, 7500) == 0);
    CHECK(F.reg[REG_T_HIGH] == 0x5000 && F.reg[REG_T_LOW] == 0x4B00);
    CHECK(tmp_set_alert(&d, 8010, -1005) == 0);
    CHECK(F.reg[REG_T_HIGH] == 0x5020 && F.reg[REG_T_LOW] == 0xF5F0);
    CHECK(tmp_raw_to_c100(0x50, 0x20) == 8013 && tmp_raw_to_c100(0xF5, 0xF0) == -1006);
    F.xfers = 0;
    CHECK(tmp_set_alert(&d, 7500, 7500) == -EINVAL);   /* low must be < high */
    CHECK(tmp_set_alert(&d, 13000, 0) == -ERANGE);     /* 130 degC > 127.9375 */
    CHECK(F.xfers == 0);                               /* nothing written on bad input */

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
