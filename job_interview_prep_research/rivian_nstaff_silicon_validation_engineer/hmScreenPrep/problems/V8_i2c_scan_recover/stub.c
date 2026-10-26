/*
 * V8  I2C Bus Scan + Stuck-Bus Recovery on a Bit-Banged Controller
 *
 * Board bring-up: the SoC's I2C pins are muxed as GPIOs and you drive the bus by hand.
 * Both lines are open-drain with pull-ups:
 *   gpio_scl_set(0) / gpio_sda_set(0)   drive the line LOW
 *   gpio_scl_set(1) / gpio_sda_set(1)   RELEASE the line (it goes high unless someone holds it low)
 *   gpio_scl_get() / gpio_sda_get()     read the real line level (1 = high)
 *   delay_us(us)                        busy-wait; advances the fake clock
 * The bus model at the bottom simulates slaves that ACK their address, a slave that
 * stretches the clock (holds SCL low during the ACK clock), and a slave stuck mid-byte
 * holding SDA low that lets go after it sees enough SCL falling edges.
 *
 * Implement:
 *   i2c_bus_recover()  If SDA is already high: return 0 (no clocks). Otherwise pulse SCL
 *                      (low, then release/high) at most 9 times, checking SDA after each pulse;
 *                      once SDA is high, generate a STOP (SCL low, SDA low, SCL high, SDA high)
 *                      and return the number of pulses used. Still low after 9 pulses -> -EIO
 *                      (no STOP). SCL that cannot go high -> -ETIMEDOUT.
 *   i2c_probe(addr)    -EINVAL for reserved addresses (0x00-0x07, 0x78-0x7F, > 0x7F), nothing on
 *                      the bus. -EBUSY if SCL or SDA is low before START. Otherwise:
 *                      START, send (addr << 1) | 0 MSB first, read the ACK bit, STOP.
 *                      0 on ACK, -ENXIO on NACK.
 *                      Every time you release SCL, wait until it is really high (clock stretching);
 *                      if it stays low for I2C_STRETCH_TIMEOUT_US -> release both lines, -ETIMEDOUT.
 *   i2c_scan(found, max) probe 0x08..0x77 in order; write up to max ACKing addresses into found,
 *                      return the total count. If a probe fails with anything other than
 *                      -ENXIO, stop and return that error.
 *   Timing: I2C_HALF_US between line changes (100 kHz).
 *
 * Build & run:  ./run.sh V8
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

/* ---------------- GPIO HAL (provided, implemented by the bus model below) ---------------- */
void gpio_scl_set(int release);
void gpio_sda_set(int release);
int  gpio_scl_get(void);
int  gpio_sda_get(void);
void delay_us(uint32_t us);

#define I2C_HALF_US             5u
#define I2C_STRETCH_TIMEOUT_US  200u
#define I2C_RECOVER_MAX_PULSES  9

/* ---------------- your code ---------------- */

int i2c_bus_recover(void)
{
    /* TODO: if SDA low, pulse SCL up to 9 times until SDA is high, then STOP; return pulses */
    return -ENOSYS;
}

int i2c_probe(uint8_t addr)
{
    /* TODO: reject reserved, check idle, START, addr+W MSB first, ACK bit, STOP.
     *       Wait for SCL to really go high every time you release it (stretch timeout). */
    (void)addr;
    return -ENOSYS;
}

int i2c_scan(uint8_t *found, size_t max)
{
    /* TODO: probe 0x08..0x77, keep ACKing addresses, return total count or first hard error */
    (void)found; (void)max;
    return -ENOSYS;
}

/* =====================================================================
 * Open-drain I2C bus model + slaves + fault injection (do not edit)
 * ===================================================================== */
#define MAX_DEV 4
#define FOREVER 0xFFFFFFFFu
enum { D_IDLE, D_ADDR, D_ACK, D_IGNORE };

typedef struct {
    uint8_t  addr;
    uint32_t stretch_us;          /* hold SCL low this long on the ACK clock (FOREVER = never let go) */
    int      st;
    uint8_t  bits, shift;
    bool     sda_low;             /* driving SDA low */
    bool     holding;             /* holding SCL low */
    uint32_t hold_until;
} Dev;

static struct {
    bool m_scl, m_sda;            /* master side: true = released */
    bool scl, sda;                /* last effective line levels */
    uint32_t now;                 /* microseconds */
    Dev dev[MAX_DEV]; int ndev;
    bool stuck; uint32_t stuck_left; bool stuck_dead;   /* stuck slave holding SDA low */
    bool sniff; uint8_t sniff_bits, sniff_shift;
    uint32_t starts, stops, m_falls, addr_bytes;
    uint8_t addr_min, addr_max;
} B;

static bool line_scl(void)
{
    if (!B.m_scl) return false;
    for (int i = 0; i < B.ndev; i++) if (B.dev[i].holding) return false;
    return true;
}

static bool line_sda(void)
{
    if (!B.m_sda || B.stuck) return false;
    for (int i = 0; i < B.ndev; i++) if (B.dev[i].sda_low) return false;
    return true;
}

static void ev_start(void)
{
    B.starts++;
    B.sniff = true; B.sniff_bits = 0; B.sniff_shift = 0;
    for (int i = 0; i < B.ndev; i++) { Dev *d = &B.dev[i]; d->st = D_ADDR; d->bits = 0; d->shift = 0; d->sda_low = false; }
}

static void ev_stop(void)
{
    B.stops++;
    B.sniff = false;
    for (int i = 0; i < B.ndev; i++) { B.dev[i].st = D_IDLE; B.dev[i].sda_low = false; }
}

static void ev_rise(bool sda)
{
    if (B.sniff && ++B.sniff_bits <= 8) {
        B.sniff_shift = (uint8_t)((B.sniff_shift << 1) | sda);
        if (B.sniff_bits == 8) {
            uint8_t a = B.sniff_shift >> 1;
            if (B.addr_bytes == 0 || a < B.addr_min) B.addr_min = a;
            if (B.addr_bytes == 0 || a > B.addr_max) B.addr_max = a;
            B.addr_bytes++;
        }
    }
    for (int i = 0; i < B.ndev; i++) {
        Dev *d = &B.dev[i];
        if (d->st == D_ADDR && d->bits < 8) { d->shift = (uint8_t)((d->shift << 1) | sda); d->bits++; }
    }
}

static void ev_fall(void)
{
    if (B.stuck && !B.stuck_dead && --B.stuck_left == 0) B.stuck = false;
    for (int i = 0; i < B.ndev; i++) {
        Dev *d = &B.dev[i];
        if (d->st == D_ADDR && d->bits == 8) {
            if ((d->shift >> 1) == d->addr && !(d->shift & 1u)) { d->st = D_ACK; d->sda_low = true; }
            else d->st = D_IGNORE;
        } else if (d->st == D_ACK) {
            d->st = D_IGNORE; d->sda_low = false;
        }
    }
}

static void bus_eval(void)
{
    bool scl = line_scl(), sda = line_sda();
    if (scl != B.scl) {
        if (scl) ev_rise(sda); else ev_fall();
    } else if (scl && sda != B.sda) {
        if (sda) ev_stop(); else ev_start();
    }
    B.scl = line_scl();
    B.sda = line_sda();
}

void gpio_scl_set(int release)
{
    bool r = release != 0;
    if (!r && B.m_scl) B.m_falls++;
    if (r && !B.m_scl) {                       /* master lets SCL go: stretching slaves grab it */
        for (int i = 0; i < B.ndev; i++) {
            Dev *d = &B.dev[i];
            if (d->st == D_ACK && d->stretch_us && !d->holding) {
                d->holding = true;
                d->hold_until = (d->stretch_us == FOREVER) ? FOREVER : B.now + d->stretch_us;
            }
        }
    }
    B.m_scl = r;
    bus_eval();
}

void gpio_sda_set(int release) { B.m_sda = release != 0; bus_eval(); }
int  gpio_scl_get(void) { return line_scl(); }
int  gpio_sda_get(void) { return line_sda(); }

void delay_us(uint32_t us)
{
    B.now += us;
    if (B.now > 10000000u) { printf("FAIL: bus time ran away (missing timeout?)\n"); exit(1); }
    for (int i = 0; i < B.ndev; i++) {
        Dev *d = &B.dev[i];
        if (d->holding && d->hold_until != FOREVER && B.now >= d->hold_until) d->holding = false;
    }
    bus_eval();
}

static void bus_reset(void)
{
    memset(&B, 0, sizeof B);
    B.m_scl = B.m_sda = true;
    B.scl = B.sda = true;
}

static void add_dev(uint8_t addr, uint32_t stretch_us)
{
    B.dev[B.ndev++] = (Dev){ .addr = addr, .stretch_us = stretch_us };
}

static void inject_stuck_sda(uint32_t falls_to_release)   /* FOREVER = shorted */
{
    B.stuck = true;
    B.stuck_left = falls_to_release;
    B.stuck_dead = (falls_to_release == FOREVER);
    B.sda = false;
}

static bool bus_idle(void) { return gpio_scl_get() && gpio_sda_get(); }

int main(void)
{
    uint8_t found[8];

    /* 1. scan finds exactly the populated addresses; reserved addresses never hit the bus */
    bus_reset();
    add_dev(0x1D, 0); add_dev(0x48, 0); add_dev(0x50, 0);
    memset(found, 0xEE, sizeof found);
    CHECK(i2c_scan(found, 8) == 3);
    CHECK(found[0] == 0x1D && found[1] == 0x48 && found[2] == 0x50 && found[3] == 0xEE);
    CHECK(B.addr_bytes == 112 && B.addr_min == 0x08 && B.addr_max == 0x77);
    CHECK(B.starts == 112 && B.stops == 112);              /* every probe ends with STOP */
    CHECK(bus_idle());

    /* 2. found[] smaller than the result: count is exact, nothing written past max */
    bus_reset();
    add_dev(0x1D, 0); add_dev(0x48, 0); add_dev(0x50, 0);
    memset(found, 0xEE, sizeof found);
    CHECK(i2c_scan(found, 2) == 3);
    CHECK(found[0] == 0x1D && found[1] == 0x48 && found[2] == 0xEE);

    /* 3. single probes: ACK, NACK, reserved address (no bus activity) */
    bus_reset();
    add_dev(0x48, 0);
    CHECK(i2c_probe(0x48) == 0);
    CHECK(i2c_probe(0x49) == -ENXIO);
    uint32_t s = B.starts;
    CHECK(i2c_probe(0x03) == -EINVAL && i2c_probe(0x78) == -EINVAL && i2c_probe(0x80) == -EINVAL);
    CHECK(B.starts == s && bus_idle());

    /* 4. slave stuck mid-byte releases SDA after 5 more clocks: recover, then the bus works */
    bus_reset();
    add_dev(0x48, 0);
    inject_stuck_sda(5);
    CHECK(i2c_probe(0x48) == -EBUSY && B.starts == 0);
    CHECK(i2c_bus_recover() == 5);
    CHECK(B.stops == 1 && bus_idle());
    CHECK(i2c_probe(0x48) == 0);
    CHECK(i2c_bus_recover() == 0);                          /* healthy bus: nothing to do */

    /* 5. worst recoverable case needs all 9 clocks */
    bus_reset();
    inject_stuck_sda(9);
    CHECK(i2c_bus_recover() == 9 && bus_idle());

    /* 6. SDA shorted low: give up after exactly 9 pulses, no STOP; scan refuses to start */
    bus_reset();
    add_dev(0x48, 0);
    inject_stuck_sda(FOREVER);
    CHECK(i2c_bus_recover() == -EIO);
    CHECK(B.m_falls == 9 && B.stops == 0);
    CHECK(i2c_scan(found, 8) == -EBUSY && B.starts == 0);

    /* 7. clock stretching: 40 us is waited out; a slave that never lets go times out */
    bus_reset();
    add_dev(0x50, 40);
    uint32_t t0 = B.now;
    CHECK(i2c_probe(0x50) == 0);
    CHECK(B.now - t0 >= 40 + 9 * 2 * I2C_HALF_US);         /* really waited for SCL */
    CHECK(B.stops == 1 && bus_idle());
    bus_reset();
    add_dev(0x50, FOREVER);
    t0 = B.now;
    CHECK(i2c_probe(0x50) == -ETIMEDOUT);
    CHECK(B.now - t0 >= I2C_STRETCH_TIMEOUT_US && B.now - t0 < I2C_STRETCH_TIMEOUT_US + 200);
    CHECK(B.m_scl && B.m_sda);                              /* master released both lines */

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
