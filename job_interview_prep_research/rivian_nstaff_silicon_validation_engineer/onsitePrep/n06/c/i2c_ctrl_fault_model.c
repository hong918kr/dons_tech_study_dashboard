/*
 * i2c_ctrl_fault_model.c -- N06C C1 practice: table-driven NEGATIVE tests for an I2C controller.
 *
 * A behavioural "reference model" of a generic I2C controller (NOT any vendor's register map):
 *   STATUS bits are W1C; an IRQ fires on the 0 -> non-zero edge of (STATUS & INT_EN).
 *   TX FIFO depth 8. On any error the controller must: set exactly the right status bit,
 *   flush the TX FIFO, leave the bus in a defined state, raise ONE interrupt, and accept the
 *   next transaction (no sticky lock-up).
 * A fault injector plays the bus side: NACK on address, NACK on data byte k, arbitration lost
 * at bit b of the address byte, SCL stretched past the timeout, SDA stuck low before START.
 * Each test row = injected fault + expected observable result -> this IS the error-category
 * section of the test matrix, executable.
 *
 * Build: cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined i2c_ctrl_fault_model.c
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

enum {
    ST_DONE      = 1u << 0,
    ST_NACK_ADDR = 1u << 1,
    ST_NACK_DATA = 1u << 2,
    ST_ARB_LOST  = 1u << 3,
    ST_TIMEOUT   = 1u << 4,
    ST_TX_OVF    = 1u << 5,
    ST_BUS_STUCK = 1u << 6,
};
#define TXF_DEPTH 8
#define STRETCH_TIMEOUT_US 1000   /* controller's SCL-low timeout (programmable in real IPs) */

typedef enum { BUS_IDLE, BUS_HELD_BY_TARGET, BUS_OWNED_BY_OTHER } bus_state_t;

typedef struct {
    uint32_t status, int_en;
    uint8_t txf[TXF_DEPTH]; int txn;
    int irq_count;
    int bytes_acked;          /* data bytes the target ACKed */
    bool stop_sent;
    bus_state_t bus;
} i2c_t;

typedef struct {
    bool nack_addr;
    int  nack_data_at;        /* -1 none, else index of data byte NACKed */
    int  arb_lost_bit;        /* -1 none, else bit 0..7 of address byte where other master wins */
    int  stretch_us;          /* SCL held low by target after first ACK */
    bool sda_stuck;           /* SDA low before START (target stuck mid-byte) */
} fault_t;

static void set_status(i2c_t *c, uint32_t bits)
{
    bool was = (c->status & c->int_en) != 0;
    c->status |= bits;
    bool now = (c->status & c->int_en) != 0;
    if (!was && now) c->irq_count++;
}
static void w1c(i2c_t *c, uint32_t bits) { c->status &= ~bits; }

static void reset(i2c_t *c) { memset(c, 0, sizeof *c); c->int_en = 0x7F; c->bus = BUS_IDLE; }

static int txf_push(i2c_t *c, uint8_t b)
{
    if (c->txn == TXF_DEPTH) { set_status(c, ST_TX_OVF); return -1; } /* byte dropped, flagged */
    c->txf[c->txn++] = b; return 0;
}

/* Run one write transaction from TX FIFO. */
static void xfer_write(i2c_t *c, uint8_t addr7, const fault_t *f)
{
    c->bytes_acked = 0; c->stop_sent = false;
    (void)addr7;
    if (f->sda_stuck || c->bus != BUS_IDLE) {          /* cannot form START */
        if (f->sda_stuck) c->bus = BUS_HELD_BY_TARGET; /* needs 9-clock recovery (BV10) */
        set_status(c, ST_BUS_STUCK); c->txn = 0; return;
    }
    if (f->arb_lost_bit >= 0) {                        /* lost during address: stop driving, no STOP */
        c->bus = BUS_OWNED_BY_OTHER;
        set_status(c, ST_ARB_LOST); c->txn = 0; return;
    }
    if (f->nack_addr) { c->stop_sent = true; set_status(c, ST_NACK_ADDR); c->txn = 0; return; }
    if (f->stretch_us > STRETCH_TIMEOUT_US) {          /* SCL held low: cannot send STOP */
        c->bus = BUS_HELD_BY_TARGET;
        set_status(c, ST_TIMEOUT); c->txn = 0; return;
    }
    for (int i = 0; i < c->txn; i++) {
        if (i == f->nack_data_at) {
            c->stop_sent = true; set_status(c, ST_NACK_DATA); c->txn = 0; return;
        }
        c->bytes_acked++;
    }
    c->txn = 0; c->stop_sent = true;
    set_status(c, ST_DONE);
}

/* What the bus does after the error (the "release" that the test waits for). */
static void bus_settles(i2c_t *c) { c->bus = BUS_IDLE; }

typedef struct {
    const char *id, *desc;
    fault_t f;
    int nbytes;
    uint32_t exp_status;      /* exact STATUS value expected */
    int exp_acked;
    bool exp_stop;
    bus_state_t exp_bus;
} tcase_t;

static const char *bus_name(bus_state_t b)
{
    return b == BUS_IDLE ? "idle" : b == BUS_HELD_BY_TARGET ? "held_by_target" : "owned_by_other";
}

int main(void)
{
    const fault_t NONE = {false, -1, -1, 0, false};
    tcase_t t[] = {
        {"C1-F00", "normal 4-byte write",            NONE,                         4, ST_DONE,      4, true,  BUS_IDLE},
        {"C1-E01", "NACK on address (no device)",    {true, -1, -1, 0, false},     4, ST_NACK_ADDR, 0, true,  BUS_IDLE},
        {"C1-E02", "NACK on data byte 2",            {false, 2, -1, 0, false},     4, ST_NACK_DATA, 2, true,  BUS_IDLE},
        {"C1-E03", "NACK on data byte 0",            {false, 0, -1, 0, false},     4, ST_NACK_DATA, 0, true,  BUS_IDLE},
        {"C1-E04", "arbitration lost at addr bit 3", {false, -1, 3, 0, false},     4, ST_ARB_LOST,  0, false, BUS_OWNED_BY_OTHER},
        {"C1-E05", "SCL stretch 5 ms > 1 ms timeout",{false, -1, -1, 5000, false}, 4, ST_TIMEOUT,   0, false, BUS_HELD_BY_TARGET},
        {"C1-F06", "SCL stretch 300 us < timeout",   {false, -1, -1, 300, false},  4, ST_DONE,      4, true,  BUS_IDLE},
        {"C1-E07", "SDA stuck low before START",     {false, -1, -1, 0, true},     4, ST_BUS_STUCK, 0, false, BUS_HELD_BY_TARGET},
        {"C1-E08", "TX FIFO overflow (9 into 8)",    NONE,                         9, ST_DONE | ST_TX_OVF, 8, true, BUS_IDLE},
    };
    int n = (int)(sizeof t / sizeof t[0]), fails = 0;
    i2c_t c;

    for (int i = 0; i < n; i++) {
        reset(&c);
        for (int b = 0; b < t[i].nbytes; b++) (void)txf_push(&c, (uint8_t)(0xA0 + b));
        xfer_write(&c, 0x50, &t[i].f);

        bool ok = true; char why[160] = "";
        if (c.status != t[i].exp_status) { ok = false; snprintf(why, sizeof why, "status=0x%02X exp 0x%02X", (unsigned)c.status, (unsigned)t[i].exp_status); }
        else if (c.irq_count != 1)       { ok = false; snprintf(why, sizeof why, "irq_count=%d exp 1", c.irq_count); }
        else if (c.txn != 0)             { ok = false; snprintf(why, sizeof why, "TX FIFO not flushed (%d)", c.txn); }
        else if (c.bytes_acked != t[i].exp_acked) { ok = false; snprintf(why, sizeof why, "acked=%d exp %d", c.bytes_acked, t[i].exp_acked); }
        else if (c.stop_sent != t[i].exp_stop)    { ok = false; snprintf(why, sizeof why, "stop=%d exp %d", c.stop_sent, t[i].exp_stop); }
        else if (c.bus != t[i].exp_bus)           { ok = false; snprintf(why, sizeof why, "bus=%s exp %s", bus_name(c.bus), bus_name(t[i].exp_bus)); }

        /* recovery check: clear status (W1C), let the bus settle, next normal write must pass */
        w1c(&c, c.status);
        bus_settles(&c);
        int irq_before = c.irq_count;
        for (int b = 0; b < 2; b++) (void)txf_push(&c, 0x55);
        xfer_write(&c, 0x50, &NONE);
        if (ok && (c.status != ST_DONE || c.irq_count != irq_before + 1 || c.bytes_acked != 2)) {
            ok = false; snprintf(why, sizeof why, "recovery xfer failed status=0x%02X", (unsigned)c.status);
        }

        printf("RESULT %s %-34s status=0x%02X acked=%d stop=%d bus=%-14s irq=1 -> %s%s%s\n",
               t[i].id, t[i].desc, (unsigned)t[i].exp_status, t[i].exp_acked, t[i].exp_stop,
               bus_name(t[i].exp_bus), ok ? "PASS" : "FAIL", ok ? "" : " : ", why);
        if (!ok) fails++;
    }

    printf("SUMMARY %d/%d PASS\n", n - fails, n);
    return fails ? 1 : 0;
}
