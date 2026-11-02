/*
 * BV10 - I2C stuck-bus recovery on a simulated open-drain bus.
 *
 * Model
 *   - Two wires, SCL and SDA, each = wired-AND of every driver (open drain):
 *       line = pull-up(1) AND master_drive AND slave_drive AND !fault
 *     A driver either releases (1) or pulls low (0). Nobody can drive "1".
 *   - One slave (7-bit addr 0x48) with a small register file and a real
 *     bit-level state machine: samples SDA on SCL rising edges, changes its
 *     SDA output on SCL falling edges, detects START/STOP (SDA edge while
 *     SCL high), and optionally stretches SCL after an address ACK.
 *   - A bit-bang master (GPIO-style primitives) that can be "reset" in the
 *     middle of a transfer: it simply releases both lines, while the slave
 *     keeps its state -> classic stuck bus (SCL high, SDA held low).
 *
 * Recovery strategies
 *   - naive : pulse SCL until SDA reads high, then one STOP.
 *   - robust: each pulse is SCL low -> SDA low -> SCL high -> SDA release,
 *             i.e. a STOP attempt on every pulse (same idea as the Linux
 *             i2c_generic_scl_recovery()), max 9 pulses, then verify idle.
 *
 * Build/run (macOS):
 *   cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk \
 *      -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
 *      i2c_bus_recovery_sim.c -o /tmp/i2c_rec && /tmp/i2c_rec
 */
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ bus -- */
typedef enum { S_IDLE, S_ADDR, S_ADDR_ACK, S_RX, S_RX_ACK, S_TX, S_TX_ACK,
               S_WAIT_STOP } sstate_t;
static const char *sname[] = { "IDLE", "ADDR", "ADDR_ACK", "RX", "RX_ACK",
                               "TX", "TX_ACK", "WAIT_STOP" };

typedef struct {
    /* drivers: 1 = released, 0 = pulling low */
    int m_scl, m_sda, s_scl, s_sda;
    int fault_sda_gnd, fault_scl_gnd;   /* board faults (solder short) */
    int scl, sda;                       /* resolved line levels */
    long tick;
    /* waveform trace */
    int tracing;
    char tr_scl[512], tr_sda[512];
    int ntr;
} bus_t;

typedef struct {
    unsigned char addr;        /* 7-bit */
    unsigned char regs[256];
    unsigned char ptr;         /* register pointer */
    sstate_t st;
    int bitcnt;
    unsigned char sr;          /* shift register */
    int rw;                    /* 1 = read */
    int first_rx;              /* first written byte = register pointer */
    unsigned char txbyte;
    int stretch_ticks;         /* configured stretch after read addr ACK */
    int stretch_left;
    int stretch_seen;          /* statistics */
    int starts, stops;
} slave_t;

static bus_t B;
static slave_t S;

static void resolve(int *scl, int *sda)
{
    *scl = B.m_scl & B.s_scl & !B.fault_scl_gnd;
    *sda = B.m_sda & B.s_sda & !B.fault_sda_gnd;
}

static void trace_point(void)
{
    if (!B.tracing || B.ntr >= (int)sizeof(B.tr_scl) - 1) return;
    B.tr_scl[B.ntr] = B.scl ? '-' : '_';
    B.tr_sda[B.ntr] = B.sda ? '-' : '_';
    B.ntr++;
    B.tr_scl[B.ntr] = B.tr_sda[B.ntr] = '\0';
}

/* ------------------------------------------------------------- slave FSM -- */
static void slave_drive_bit(int bit) { B.s_sda = bit ? 1 : 0; }

static void slave_on_scl_rise(int sda)
{
    switch (S.st) {
    case S_ADDR:
    case S_RX:
        S.sr = (unsigned char)((S.sr << 1) | (sda & 1));
        S.bitcnt++;
        break;
    case S_TX_ACK:
        /* master's ACK bit: SDA low = ACK (send more), high = NACK (done) */
        S.rw = sda ? -1 : 1;  /* reuse rw: -1 marks NACK received */
        break;
    default:
        break;   /* ADDR_ACK, RX_ACK, TX: slave is the one driving SDA */
    }
}

static void slave_on_scl_fall(void)
{
    switch (S.st) {
    case S_ADDR:
        if (S.bitcnt == 8) {
            if ((S.sr >> 1) == S.addr) {
                S.rw = S.sr & 1;
                S.st = S_ADDR_ACK;
                slave_drive_bit(0);             /* ACK */
            } else {
                S.st = S_WAIT_STOP;             /* not for us */
            }
        }
        break;
    case S_ADDR_ACK:
        slave_drive_bit(1);                     /* end of ACK slot */
        if (S.rw == 1) {
            S.st = S_TX;
            S.bitcnt = 0;
            S.txbyte = S.regs[S.ptr++];
            slave_drive_bit((S.txbyte >> 7) & 1);
            if (S.stretch_ticks > 0) {          /* "fetching data..." */
                B.s_scl = 0;
                S.stretch_left = S.stretch_ticks;
            }
        } else {
            S.st = S_RX;
            S.bitcnt = 0;
            S.sr = 0;
            S.first_rx = 1;
        }
        break;
    case S_RX:
        if (S.bitcnt == 8) {
            if (S.first_rx) { S.ptr = S.sr; S.first_rx = 0; }
            else            { S.regs[S.ptr++] = S.sr; }  /* commit write */
            S.st = S_RX_ACK;
            slave_drive_bit(0);
        }
        break;
    case S_RX_ACK:
        slave_drive_bit(1);
        S.st = S_RX;
        S.bitcnt = 0;
        S.sr = 0;
        break;
    case S_TX:
        S.bitcnt++;
        if (S.bitcnt == 8) {
            slave_drive_bit(1);                 /* release for master ACK */
            S.st = S_TX_ACK;
        } else {
            slave_drive_bit((S.txbyte >> (7 - S.bitcnt)) & 1);
        }
        break;
    case S_TX_ACK:
        if (S.rw == -1) {                       /* NACK -> done */
            S.st = S_WAIT_STOP;
            slave_drive_bit(1);
        } else {
            S.st = S_TX;
            S.bitcnt = 0;
            S.txbyte = S.regs[S.ptr++];
            slave_drive_bit((S.txbyte >> 7) & 1);
        }
        break;
    default:
        break;
    }
}

static void slave_on_start(void)
{
    S.starts++;
    S.st = S_ADDR;
    S.bitcnt = 0;
    S.sr = 0;
    slave_drive_bit(1);
}

static void slave_on_stop(void)
{
    S.stops++;
    S.st = S_IDLE;
    slave_drive_bit(1);
}

/* Re-evaluate the wires until stable; deliver edges to the slave. */
static void settle(void)
{
    for (int iter = 0; iter < 8; iter++) {
        int scl, sda;
        resolve(&scl, &sda);
        if (scl == B.scl && sda == B.sda) return;
        int oscl = B.scl, osda = B.sda;
        B.scl = scl;
        B.sda = sda;
        if (scl != oscl) {
            if (scl) slave_on_scl_rise(sda);
            else     slave_on_scl_fall();
        } else if (scl && sda != osda) {
            if (sda) slave_on_stop();
            else     slave_on_start();
        }
    }
}

/* One unit of simulated time (think: a quarter SCL period). */
static void tick(void)
{
    B.tick++;
    if (S.stretch_left > 0 && --S.stretch_left == 0) {
        B.s_scl = 1;
        settle();
    }
}

/* ---------------------------------------------------- master primitives -- */
#define SCL_STRETCH_TIMEOUT 50  /* ticks */

static void scl_lo(void)  { B.m_scl = 0; settle(); tick(); trace_point(); }
static void sda_lo(void)  { B.m_sda = 0; settle(); tick(); trace_point(); }
static void sda_rel(void) { B.m_sda = 1; settle(); tick(); trace_point(); }
static void sda_set(int b){ if (b) sda_rel(); else sda_lo(); }

/* Release SCL and wait while a slave stretches it. Returns ticks waited or
 * -1 on timeout (SCL stuck low). */
static int scl_hi(void)
{
    B.m_scl = 1;
    settle();
    int waited = 0;
    while (!B.scl && waited < SCL_STRETCH_TIMEOUT) { tick(); waited++; }
    tick();
    trace_point();
    if (!B.scl) return -1;
    if (waited) S.stretch_seen += waited;
    return waited;
}

static int bus_idle(void) { return B.scl && B.sda; }

static int i2c_start(void)
{
    if (!bus_idle()) return -1;     /* bus busy / stuck: refuse to START */
    sda_lo();
    scl_lo();
    return 0;
}
static void i2c_rstart(void) { sda_rel(); scl_hi(); sda_lo(); scl_lo(); }
static void i2c_stop(void)   { sda_lo(); scl_hi(); sda_rel(); }

/* returns 1 if ACKed */
static int i2c_write_byte(unsigned char b)
{
    for (int i = 7; i >= 0; i--) {
        sda_set((b >> i) & 1);
        scl_hi();
        scl_lo();
    }
    sda_rel();
    scl_hi();
    int ack = !B.sda;
    scl_lo();
    return ack;
}

static unsigned char i2c_read_byte(int ack)
{
    unsigned char b = 0;
    sda_rel();
    for (int i = 0; i < 8; i++) {
        scl_hi();
        b = (unsigned char)((b << 1) | B.sda);
        scl_lo();
    }
    sda_set(!ack);       /* ACK = drive low, NACK = release */
    scl_hi();
    scl_lo();
    sda_rel();
    return b;
}

/* "MCU reset / watchdog in the middle of a transfer": the master's pins
 * go back to inputs -> both lines released. The slave keeps its state. */
static void master_reset(void)
{
    B.m_scl = 1;
    B.m_sda = 1;
    settle();
    tick();
}

/* --------------------------------------------------------- recovery ---- */
typedef enum { REC_OK = 0, REC_SDA_STUCK, REC_SCL_STUCK, REC_STOP_FAILED } rec_t;
static const char *recname[] = { "OK", "SDA_STUCK_LOW", "SCL_STUCK_LOW",
                                 "STOP_FAILED" };

static rec_t recover_naive(int *pulses)
{
    *pulses = 0;
    if (scl_hi() < 0) return REC_SCL_STUCK;
    while (!B.sda && *pulses < 9) {
        scl_lo();
        if (scl_hi() < 0) return REC_SCL_STUCK;
        (*pulses)++;
        printf("    naive pulse %d: SDA=%d  (slave %s, bit %d)\n", *pulses,
               B.sda, sname[S.st], S.bitcnt);
    }
    if (!B.sda) return REC_SDA_STUCK;
    /* single STOP: SCL low, SDA low, SCL high, SDA release */
    scl_lo();
    sda_lo();
    scl_hi();
    sda_rel();
    printf("    naive STOP   : SCL=%d SDA=%d  (slave %s, bit %d)\n", B.scl,
           B.sda, sname[S.st], S.bitcnt);
    return bus_idle() ? REC_OK : REC_STOP_FAILED;
}

static rec_t recover_robust(int *pulses)
{
    *pulses = 0;
    if (scl_hi() < 0) return REC_SCL_STUCK;   /* nothing we can clock */
    if (B.sda) return REC_OK;                 /* already idle */
    for (int i = 1; i <= 9; i++) {
        scl_lo();
        sda_lo();                  /* prepare a STOP on every pulse */
        if (scl_hi() < 0) return REC_SCL_STUCK;
        sda_rel();                 /* SDA rising while SCL high = STOP */
        *pulses = i;
        printf("    pulse %d: SCL=%d SDA=%d  slave=%-9s bit=%d\n", i, B.scl,
               B.sda, sname[S.st], S.bitcnt);
        if (bus_idle()) return REC_OK;
    }
    return REC_SDA_STUCK;
}

/* ---------------------------------------------------------- harness ---- */
#define SLAVE_ADDR   0x48
#define REG_WHO_AM_I 0x0F
#define WHO_AM_I_VAL 0xB5   /* simulated part, not a real datasheet value */
#define REG_DATA     0x20

static void sim_reset(void)
{
    memset(&B, 0, sizeof B);
    memset(&S, 0, sizeof S);
    B.m_scl = B.m_sda = B.s_scl = B.s_sda = 1;
    B.scl = B.sda = 1;
    S.addr = SLAVE_ADDR;
    S.regs[REG_WHO_AM_I] = WHO_AM_I_VAL;
    S.stretch_ticks = 3;
}

/* Send the 8 address bits, then the ACK clock's rising edge only. */
static void send_addr_then_hang_in_ack(unsigned char addr_rw)
{
    for (int i = 7; i >= 0; i--) {
        sda_set((addr_rw >> i) & 1);
        scl_hi();
        scl_lo();
    }
    sda_rel();
    scl_hi();          /* slave is driving ACK (SDA low) right now */
}

/* WHO_AM_I read: START, addr+W, reg, rSTART, addr+R, data(NACK), STOP */
static int read_who_am_i(unsigned char *val)
{
    if (i2c_start() < 0) return -1;
    if (!i2c_write_byte((SLAVE_ADDR << 1) | 0)) { i2c_stop(); return -2; }
    if (!i2c_write_byte(REG_WHO_AM_I))          { i2c_stop(); return -3; }
    i2c_rstart();
    if (!i2c_write_byte((SLAVE_ADDR << 1) | 1)) { i2c_stop(); return -4; }
    *val = i2c_read_byte(0);
    i2c_stop();
    return 0;
}

typedef struct {
    const char *id, *desc, *method, *expect;
    char actual[64];
    int pulses;
    int pass;
} row_t;

static void print_state(const char *tag)
{
    printf("  %-22s SCL=%d SDA=%d  slave=%s bit=%d\n", tag, B.scl, B.sda,
           sname[S.st], S.bitcnt);
}

/* Build the stuck condition for scenario `sc`. */
static void make_stuck(int sc)
{
    switch (sc) {
    case 1: /* write: master dies while slave ACKs its address */
        i2c_start();
        send_addr_then_hang_in_ack((SLAVE_ADDR << 1) | 0);
        master_reset();
        break;
    case 2: /* read of 0x00: master dies in the addr ACK -> ACK + 8 zeros */
        S.regs[REG_DATA] = 0x00;
        i2c_start();
        i2c_write_byte((SLAVE_ADDR << 1) | 0);
        i2c_write_byte(REG_DATA);
        i2c_rstart();
        send_addr_then_hang_in_ack((SLAVE_ADDR << 1) | 1);
        master_reset();
        break;
    case 3: /* read of 0x10: master dies after sampling bit7 */
        S.regs[REG_DATA] = 0x10;
        i2c_start();
        i2c_write_byte((SLAVE_ADDR << 1) | 0);
        i2c_write_byte(REG_DATA);
        i2c_rstart();
        i2c_write_byte((SLAVE_ADDR << 1) | 1);  /* ACK done, slave on bit7 */
        sda_rel();
        scl_hi();                               /* master samples bit7 */
        master_reset();
        break;
    case 4: /* hardware: SDA shorted to GND */
        B.fault_sda_gnd = 1;
        settle();
        break;
    case 5: /* hardware: SCL shorted to GND (or slave stretching forever) */
        B.fault_scl_gnd = 1;
        settle();
        break;
    }
}

static int run(int sc, int robust, row_t *r, int show_wave)
{
    sim_reset();
    make_stuck(sc);
    printf("\n[%s] %s  (method: %s)\n", r->id, r->desc, r->method);
    print_state("stuck state:");
    int start_rc = i2c_start();
    printf("  START attempt before recovery -> %s\n",
           start_rc < 0 ? "refused (bus not idle)" : "ok");
    if (start_rc == 0) { i2c_stop(); }

    B.tracing = show_wave;
    int pulses = 0;
    rec_t rc = robust ? recover_robust(&pulses) : recover_naive(&pulses);
    B.tracing = 0;
    r->pulses = pulses;
    printf("  recovery result: %s after %d pulse(s); stretch waited %d tick(s)\n",
           recname[rc], pulses, S.stretch_seen);
    print_state("after recovery:");
    if (show_wave) {
        printf("  waveform during recovery ('-' high, '_' low):\n");
        printf("    SCL %s\n    SDA %s\n", B.tr_scl, B.tr_sda);
    }

    if (rc == REC_OK) {
        unsigned char v = 0;
        int t = read_who_am_i(&v);
        if (t == 0) {
            snprintf(r->actual, sizeof r->actual, "recovered, WHO_AM_I=0x%02X", v);
            printf("  verify: WHO_AM_I(0x%02X) = 0x%02X  %s\n", REG_WHO_AM_I, v,
                   v == WHO_AM_I_VAL ? "match" : "MISMATCH");
            if (v != WHO_AM_I_VAL) rc = REC_STOP_FAILED;
        } else {
            snprintf(r->actual, sizeof r->actual, "recovered?, xfer err %d", t);
            rc = REC_STOP_FAILED;
        }
    } else {
        snprintf(r->actual, sizeof r->actual, "%s", recname[rc]);
    }
    printf("  slave saw START x%d, STOP x%d\n", S.starts, S.stops);
    return rc;
}

int main(void)
{
    row_t rows[] = {
        { "S1", "write, slave stuck driving ACK after address", "robust",
          "OK", "", 0, 0 },
        { "S2", "read 0x00, stuck in addr ACK (worst case: ACK + 8 zeros)",
          "robust", "OK", "", 0, 0 },
        { "S3a", "read 0x10, stuck at bit7", "naive", "STOP_FAILED", "", 0, 0 },
        { "S3b", "read 0x10, stuck at bit7", "robust", "OK", "", 0, 0 },
        { "S4", "SDA shorted to GND (board fault)", "robust", "SDA_STUCK_LOW",
          "", 0, 0 },
        { "S5", "SCL shorted to GND / endless stretch", "robust",
          "SCL_STUCK_LOW", "", 0, 0 },
    };
    const int sc_of[] = { 1, 2, 3, 3, 4, 5 };
    const int n = (int)(sizeof rows / sizeof rows[0]);

    printf("I2C stuck-bus recovery simulation (open-drain, slave addr 0x%02X)\n",
           SLAVE_ADDR);
    int all_as_expected = 1;
    for (int i = 0; i < n; i++) {
        int robust = strcmp(rows[i].method, "robust") == 0;
        int rc = run(sc_of[i], robust, &rows[i], sc_of[i] == 2);
        rows[i].pass = strcmp(recname[rc], rows[i].expect) == 0;
        if (!rows[i].pass) all_as_expected = 0;
    }

    printf("\n%-4s %-7s %-14s %-6s %-28s %s\n", "ID", "method", "expected",
           "pulses", "actual", "verdict");
    for (int i = 0; i < n; i++)
        printf("%-4s %-7s %-14s %-6d %-28s %s\n", rows[i].id, rows[i].method,
               rows[i].expect, rows[i].pulses, rows[i].actual,
               rows[i].pass ? "PASS" : "FAIL");
    printf("\nsuite: %s\n", all_as_expected ? "ALL AS EXPECTED" : "UNEXPECTED RESULT");
    return all_as_expected ? 0 : 1;
}
