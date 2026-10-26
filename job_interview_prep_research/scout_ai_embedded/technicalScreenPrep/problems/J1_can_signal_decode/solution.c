/*
 * J1  Decode CAN Signals and Monitor CANopen Heartbeats
 *
 * Part A - generic signal extraction from an 8-byte classic CAN payload.
 *   Intel / little-endian layout (DBC "@1"): bit k of the frame lives in
 *   data[k / 8], bit (k % 8). A signal of `len` bits starting at `start_bit`
 *   occupies bits start_bit .. start_bit + len - 1; start_bit is its LSB.
 *     can_get_bits()   -> raw unsigned value (len 1..32, start_bit + len <= 64)
 *     can_get_signed() -> same bits, two's-complement sign-extended
 *   Invalid arguments return 0. No undefined behaviour (careful with len == 32).
 *
 * Part B - decode a CANopen TPDO1 from a motor drive.
 *   COB-ID = 0x180 + node_id, node_id 1..127, standard 11-bit id. DLC must be >= 8.
 *   bits  0..15  speed_rpm    signed,   1 rpm
 *   bits 16..31  current      signed,   0.1 A   -> store current_ma (mA)
 *   bits 32..39  temp         unsigned, 1 degC, offset -40
 *   bits 40..47  status       bit0 = fault, bit1 = enabled
 *   bits 48..59  bus_voltage  unsigned, 0.1 V   -> store bus_mv (mV)
 *   bits 60..63  counter      unsigned rolling counter 0..15
 *   Return false (and leave *out untouched) on a wrong id, node 0 or short DLC.
 *   Integer math only.
 *
 * Part C - heartbeat monitor for nodes 1..8.
 *   Heartbeat COB-ID = 0x700 + node_id, data[0] = NMT state
 *   (0x00 boot-up, 0x04 stopped, 0x05 operational, 0x7F pre-operational).
 *   hb_check() returns a bitmask (bit n-1 = node n) of nodes that were seen at
 *   least once AND (are timed out: now - last > timeout, wrap-safe, OR whose last
 *   state is not operational). Ignore other ids / nodes and frames with DLC 0.
 *
 * Reference solution.  Build & run:  ./run.sh J1 sol
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

/* ---------- Part A ---------- */
uint32_t can_get_bits(const uint8_t data[8], uint8_t start_bit, uint8_t len)
{
    if (data == NULL || len == 0 || len > 32 || start_bit + len > 64) return 0;
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--)              /* little-endian assemble: data[0] = LSB */
        v = (v << 8) | data[i];
    uint64_t mask = ((uint64_t)1 << len) - 1; /* 64-bit shift: len 32 is safe */
    return (uint32_t)((v >> start_bit) & mask);
}

int32_t can_get_signed(const uint8_t data[8], uint8_t start_bit, uint8_t len)
{
    uint32_t raw = can_get_bits(data, start_bit, len);
    if (len == 0 || len > 32) return 0;
    if (raw & ((uint32_t)1 << (len - 1)))     /* sign bit set -> subtract 2^len */
        return (int32_t)((int64_t)raw - ((int64_t)1 << len));
    return (int32_t)raw;
}

/* ---------- Part B ---------- */
typedef struct {
    uint8_t  node_id;
    int16_t  speed_rpm;
    int32_t  current_ma;
    int16_t  temp_c;
    bool     fault;
    bool     enabled;
    uint32_t bus_mv;
    uint8_t  counter;
} MotorStatus;

#define COB_FUNC_MASK 0x780u
#define COB_NODE_MASK 0x07Fu
#define COB_TPDO1     0x180u
#define COB_HEARTBEAT 0x700u

bool decode_motor_status(uint32_t can_id, uint8_t dlc, const uint8_t data[8], MotorStatus *out)
{
    if (data == NULL || out == NULL) return false;
    if (can_id > 0x7FFu) return false;                       /* not an 11-bit id */
    if ((can_id & COB_FUNC_MASK) != COB_TPDO1) return false; /* not TPDO1 */
    uint8_t node = (uint8_t)(can_id & COB_NODE_MASK);
    if (node == 0 || dlc < 8) return false;

    MotorStatus m;                         /* decode fully, then publish once */
    m.node_id    = node;
    m.speed_rpm  = (int16_t)can_get_signed(data, 0, 16);
    m.current_ma = can_get_signed(data, 16, 16) * 100;       /* 0.1 A -> mA */
    m.temp_c     = (int16_t)((int32_t)can_get_bits(data, 32, 8) - 40);
    uint32_t st  = can_get_bits(data, 40, 8);
    m.fault      = (st & 0x01u) != 0;
    m.enabled    = (st & 0x02u) != 0;
    m.bus_mv     = can_get_bits(data, 48, 12) * 100u;        /* 0.1 V -> mV */
    m.counter    = (uint8_t)can_get_bits(data, 60, 4);
    *out = m;
    return true;
}

/* ---------- Part C ---------- */
#define HB_MAX_NODES 8
#define NMT_BOOTUP   0x00u
#define NMT_STOPPED  0x04u
#define NMT_OPER     0x05u
#define NMT_PREOP    0x7Fu

typedef struct {
    uint32_t timeout_ms;
    uint32_t last_ms[HB_MAX_NODES];
    uint8_t  state[HB_MAX_NODES];
    uint8_t  seen;                 /* bit n-1 = node n has sent at least one heartbeat */
} HbMon;

void hb_init(HbMon *m, uint32_t timeout_ms)
{
    memset(m, 0, sizeof *m);
    m->timeout_ms = timeout_ms;
}

void hb_on_frame(HbMon *m, uint32_t can_id, uint8_t dlc, const uint8_t *data, uint32_t now_ms)
{
    if (can_id > 0x7FFu || (can_id & COB_FUNC_MASK) != COB_HEARTBEAT) return;
    uint32_t node = can_id & COB_NODE_MASK;
    if (node < 1 || node > HB_MAX_NODES || dlc < 1 || data == NULL) return;
    m->last_ms[node - 1] = now_ms;
    m->state[node - 1]   = data[0] & 0x7Fu;   /* bit7 = toggle bit (node guarding) */
    m->seen |= (uint8_t)(1u << (node - 1));
}

uint8_t hb_check(HbMon *m, uint32_t now_ms)
{
    uint8_t bad = 0;
    for (uint32_t i = 0; i < HB_MAX_NODES; i++) {
        if (!(m->seen & (1u << i))) continue;
        bool timed_out = (uint32_t)(now_ms - m->last_ms[i]) > m->timeout_ms;  /* wrap-safe */
        if (timed_out || m->state[i] != NMT_OPER) bad |= (uint8_t)(1u << i);
    }
    return bad;
}

int main(void)
{
    /* ===== Part A ===== */
    /* 1. normal frame: DC 05 83 FF 69 02 E0 51 */
    const uint8_t f[8] = { 0xDC, 0x05, 0x83, 0xFF, 0x69, 0x02, 0xE0, 0x51 };
    CHECK(can_get_bits(f, 0, 16) == 1500);
    CHECK(can_get_signed(f, 16, 16) == -125);
    CHECK(can_get_bits(f, 16, 16) == 0xFF83);
    CHECK(can_get_bits(f, 48, 12) == 480);
    CHECK(can_get_bits(f, 60, 4) == 5);

    /* 2. field crossing a byte boundary: bits 12..19 = high nibble of data[1] + low nibble of data[2] */
    const uint8_t x[8] = { 0x00, 0xAB, 0xCD, 0x00, 0x00, 0x00, 0x00, 0x00 };
    CHECK(can_get_bits(x, 12, 8) == 0xDA);
    CHECK(can_get_signed(x, 12, 8) == -38);

    /* 3. len == 32 (no UB), INT32_MIN, and a 1-bit signal at bit 63 */
    const uint8_t w[8] = { 0x00, 0x00, 0x00, 0x80, 0xFF, 0xFF, 0xFF, 0xFF };
    CHECK(can_get_bits(w, 32, 32) == 0xFFFFFFFFu);
    CHECK(can_get_signed(w, 32, 32) == -1);
    CHECK(can_get_signed(w, 0, 32) == INT32_MIN);
    CHECK(can_get_bits(w, 63, 1) == 1 && can_get_signed(w, 63, 1) == -1);
    CHECK(can_get_signed(w, 28, 4) == -8);   /* 0x8 in 4 bits */

    /* 4. invalid arguments return 0 */
    CHECK(can_get_bits(w, 0, 0) == 0);
    CHECK(can_get_bits(w, 0, 33) == 0);
    CHECK(can_get_bits(w, 40, 25) == 0);     /* 40 + 25 > 64 */

    /* ===== Part B ===== */
    MotorStatus ms;
    /* 5. normal TPDO1 from node 3 */
    bool ok = decode_motor_status(0x183, 8, f, &ms);
    CHECK(ok);
    CHECK(ok && ms.node_id == 3 && ms.speed_rpm == 1500 && ms.current_ma == -12500);
    CHECK(ok && ms.temp_c == 65 && !ms.fault && ms.enabled);
    CHECK(ok && ms.bus_mv == 48000 && ms.counter == 5);

    /* 6. negative speed, temp raw 0 -> -40 C, fault set, max 12-bit voltage, counter 15 */
    const uint8_t g[8] = { 0x24, 0xFA, 0x64, 0x00, 0x00, 0x01, 0xFF, 0xFF };
    ok = decode_motor_status(0x1FF, 8, g, &ms);
    CHECK(ok && ms.node_id == 127 && ms.speed_rpm == -1500 && ms.current_ma == 10000);
    CHECK(ok && ms.temp_c == -40 && ms.fault && !ms.enabled);
    CHECK(ok && ms.bus_mv == 409500 && ms.counter == 15);

    /* 7. rejects: wrong function code, node 0, short DLC, 29-bit id; *out untouched */
    MotorStatus keep = { .node_id = 0xEE };
    CHECK(!decode_motor_status(0x283, 8, f, &keep));       /* TPDO2 */
    CHECK(!decode_motor_status(0x703, 8, f, &keep));       /* heartbeat */
    CHECK(!decode_motor_status(0x180, 8, f, &keep));       /* node 0 */
    CHECK(!decode_motor_status(0x183, 7, f, &keep));       /* short DLC */
    CHECK(!decode_motor_status(0x18FF0183u, 8, f, &keep)); /* extended id */
    CHECK(keep.node_id == 0xEE);

    /* ===== Part C ===== */
    HbMon hb;
    const uint8_t OP = 0x05, BOOT = 0x00, PREOP = 0x7F;
    hb_init(&hb, 1000);

    /* 8. nodes 1 and 2 operational at t=0; node 3 never seen -> not reported */
    hb_on_frame(&hb, 0x701, 1, &OP, 0);
    hb_on_frame(&hb, 0x702, 1, &OP, 0);
    CHECK(hb_check(&hb, 900) == 0x00);
    hb_on_frame(&hb, 0x701, 1, &OP, 1000);
    CHECK(hb_check(&hb, 1500) == 0x02);      /* node 2: 1500 ms > 1000 */

    /* 9. node 2 reboots (boot-up 0x00) -> fresh but NOT operational */
    hb_on_frame(&hb, 0x702, 1, &BOOT, 1600);
    CHECK(hb_check(&hb, 1700) == 0x02);
    hb_on_frame(&hb, 0x702, 1, &OP, 1800);
    CHECK(hb_check(&hb, 1900) == 0x00);

    /* 10. exactly timeout is still alive (strict >), one ms later is dead */
    CHECK(hb_check(&hb, 2000) == 0x00);      /* node 1: 2000 - 1000 = 1000 */
    CHECK(hb_check(&hb, 2001) == 0x01);

    /* 11. pre-op node 4, ignored ids: node 9, node 0, DLC 0, wrong function code */
    hb_on_frame(&hb, 0x704, 1, &PREOP, 2001);
    hb_on_frame(&hb, 0x709, 1, &OP, 2001);
    hb_on_frame(&hb, 0x700, 1, &OP, 2001);
    hb_on_frame(&hb, 0x703, 0, &OP, 2001);
    hb_on_frame(&hb, 0x183, 8, f, 2001);
    CHECK(hb_check(&hb, 2001) == 0x09);      /* node 1 timed out + node 4 pre-op */

    /* 12. millisecond timer wraps past 0xFFFFFFFF */
    hb_init(&hb, 500);
    hb_on_frame(&hb, 0x705, 1, &OP, 0xFFFFFF00u);
    CHECK(hb_check(&hb, 0xFFFFFF80u) == 0x00);  /* 128 ms: a naive now > last + timeout fails here */
    CHECK(hb_check(&hb, 0x00000064u) == 0x00);  /* 356 ms across the wrap */
    CHECK(hb_check(&hb, 0x00000200u) == 0x10);  /* 768 ms > 500 -> node 5 */

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
