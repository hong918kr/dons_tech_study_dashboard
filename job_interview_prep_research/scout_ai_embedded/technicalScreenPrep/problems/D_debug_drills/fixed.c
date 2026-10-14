/*
 * D  Debug Drills (D1-D8)
 *
 * Eight short firmware functions. Each one compiles and "looks right", but each has
 * exactly one classic embedded bug (D1 has two). For every drill:
 *   1. read the "Intended behavior" comment,
 *   2. say the bug out loud in under a minute,
 *   3. fix it with a one-line (or few-line) change.
 *
 * The test main() exposes every bug WITHOUT crashing or hanging:
 *   - D1 n == 0 runs in a forked child, so a divide-by-zero trap cannot kill the run.
 *   - D3 checks the head index BEFORE the next write, so no out-of-bounds store happens.
 *   - D4 has a TEST GUARD (iteration cap + index check) so the loop cannot run forever.
 *   - D6 simulates an interrupt deterministically through a test hook.
 *
 * Fixed version (reference).  Build & run:  ./run.sh D fixed   -> ALL PASS
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

/* ------------------------------------------------------------------------------------
 * D1  Average of n samples.
 * Intended: return the integer mean (truncated toward zero) of x[0..n-1].
 *           Inputs can be large (e.g. 8500 = 85.00 degC in 0.01 degC units).
 *           If n == 0, return AVG_INVALID instead of dividing.
 * ---------------------------------------------------------------------------------- */
#define AVG_INVALID INT16_MIN

int16_t avg(const int16_t *x, uint8_t n)
{
    if (n == 0) return AVG_INVALID;                  /* FIX: no divide by zero */
    int32_t sum = 0;                                  /* FIX: 255 * 32767 fits in int32 */
    for (uint8_t i = 0; i < n; i++) sum += x[i];
    return (int16_t)(sum / n);                        /* mean of int16 values fits in int16 */
}

/* ------------------------------------------------------------------------------------
 * D2  Timeout check on a free-running 32-bit tick counter.
 * Intended: return true once at least `timeout` ticks have elapsed since `start`.
 *           The counter wraps from 0xFFFFFFFF to 0, and that must not matter.
 * ---------------------------------------------------------------------------------- */
bool timed_out(uint32_t now, uint32_t start, uint32_t timeout)
{
    return (uint32_t)(now - start) >= timeout;       /* FIX: elapsed = modular difference */
}

/* ------------------------------------------------------------------------------------
 * D3  Ring buffer push (one slot kept empty, capacity RB_N - 1).
 * Intended: store v at head and advance head, wrapping to 0 after RB_N - 1.
 *           Return false (and store nothing) when the buffer is full.
 * ---------------------------------------------------------------------------------- */
#define RB_N 8
static int rb_buf[RB_N];
static unsigned rb_head, rb_tail;

bool push(int v)
{
    if ((rb_head + 1) % RB_N == rb_tail) return false;
    rb_buf[rb_head] = v;
    rb_head = (rb_head + 1) % RB_N;                   /* FIX: wrap the index */
    return true;
}

bool pop(int *v)              /* correct; given for the test */
{
    if (rb_head == rb_tail) return false;
    *v = rb_buf[rb_tail];
    rb_tail = (rb_tail + 1) % RB_N;
    return true;
}

/* ------------------------------------------------------------------------------------
 * D4  Process an array from the last element to the first.
 * Intended: call process(x[i]) for i = n-1, n-2, ..., 0 exactly once each.
 *           n == 0 must do nothing.
 * ---------------------------------------------------------------------------------- */
static int16_t d4_seen[16];
static size_t d4_cnt;
static bool d4_bad_index;     /* set by the TEST GUARD when the loop goes wrong */
static void process(int16_t v) { if (d4_cnt < 16) d4_seen[d4_cnt] = v; d4_cnt++; }

void process_reverse(const int16_t *x, size_t n)
{
    size_t iters = 0;
    for (size_t i = n; i-- > 0; ) {                   /* FIX: test first, then decrement */
        /* TEST GUARD (not part of the drill): stop before an out-of-range read / endless loop */
        if (i >= n || ++iters > 1000) { d4_bad_index = true; break; }
        process(x[i]);
    }
}

/* ------------------------------------------------------------------------------------
 * D5  Decode a 12-bit two's-complement temperature register (TMP102-style).
 * Intended: msb holds bits 11..4, the upper nibble of lsb holds bits 3..0.
 *           Return the signed raw count; 1 count = 0.0625 degC.
 *           e.g. 0x190 -> +400 (+25 degC), 0xE70 -> -400 (-25 degC).
 * ---------------------------------------------------------------------------------- */
int16_t decode_temp12(uint8_t msb, uint8_t lsb)
{
    int16_t raw = (int16_t)((msb << 4) | (lsb >> 4)); /* 0..4095 */
    if (raw & 0x800) raw -= 0x1000;                   /* FIX: sign-extend bit 11 */
    return raw;
}

/* ------------------------------------------------------------------------------------
 * D6  Overheat event counter shared between an ISR and the main loop.
 * Intended: TEMP_IRQHandler() counts events; task() reports every event exactly once.
 *           No event may be lost even if the IRQ fires at any instruction of task().
 *
 * Simulation: irq_window() marks the point where a real interrupt could land.
 * The test arms g_irq_fire_once so that exactly one IRQ fires at that point.
 * irq_disable()/irq_enable() model __disable_irq()/__enable_irq(): an IRQ raised
 * while disabled stays pending and runs when interrupts are enabled again.
 * ---------------------------------------------------------------------------------- */
static volatile uint32_t overheat_count;        /* FIX: volatile - changed by the ISR */
static uint32_t g_reported;
static bool g_irq_fire_once, g_irq_masked, g_irq_pending;

void TEMP_IRQHandler(void) { overheat_count++; }
static void report(uint32_t c) { g_reported += c; }
static void irq_window(void)
{
    if (!g_irq_fire_once) return;
    g_irq_fire_once = false;
    if (g_irq_masked) g_irq_pending = true; else TEMP_IRQHandler();
}
static inline void irq_disable(void) { g_irq_masked = true; }
static inline void irq_enable(void)
{
    g_irq_masked = false;
    if (g_irq_pending) { g_irq_pending = false; TEMP_IRQHandler(); }
}

void task(void)
{
    irq_disable();                             /* FIX: read + clear as one critical section */
    uint32_t c = overheat_count;
    irq_window();                              /* the IRQ stays pending while masked */
    overheat_count = 0;
    irq_enable();                              /* pending IRQ runs here -> count = 1 */
    if (c > 0) report(c);                      /* slow work outside the critical section */
}

/* ------------------------------------------------------------------------------------
 * D7  Reset one sample record to all zeros.
 * Intended: every field of *s becomes 0.
 * ---------------------------------------------------------------------------------- */
typedef struct { uint32_t t_ms; int16_t temp_c10; uint16_t flags; uint32_t seq; uint32_t crc; } Sample;

void clear(Sample *s) { memset(s, 0, sizeof(*s)); /* FIX: size of the struct, not the pointer */ }

/* ------------------------------------------------------------------------------------
 * D8  Debounced overheat detection.
 * Intended: enter OVERHEAT only after 3 CONSECUTIVE readings >= HIGH.
 *           Any reading below HIGH restarts the count.
 * ---------------------------------------------------------------------------------- */
#define HIGH 800
typedef enum { NORMAL, OVERHEAT } State;
typedef struct { uint8_t hot_cnt; State state; } Debounce;

void deb_update(Debounce *d, int16_t temp)
{
    if (temp >= HIGH) {
        if (d->hot_cnt < 3) d->hot_cnt++;             /* saturate: no uint8_t wrap */
        if (d->hot_cnt >= 3) d->state = OVERHEAT;
    } else {
        d->hot_cnt = 0;                               /* FIX: a cool reading restarts the count */
    }
}

/* ================================== tests ======================================== */
static int g_sec_fail;
static void begin(void) { g_sec_fail = g_fail; }
static void end(const char *name) { printf("%-28s %s\n", name, g_fail == g_sec_fail ? "ok" : "<-- BUG"); }

/* run avg(x, 0) in a child process: a divide-by-zero trap kills only the child */
static bool avg_n0_returns_invalid(void)
{
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        const int16_t x[1] = { 5 };
        _exit(avg(x, 0) == AVG_INVALID ? 0 : 1);
    }
    int st = 0;
    if (pid < 0 || waitpid(pid, &st, 0) != pid) return false;
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

int main(void)
{
    /* D1 */
    begin();
    const int16_t small[3] = { 10, 20, 30 };
    CHECK(avg(small, 3) == 20);                            /* normal */
    const int16_t big[8] = { 8500, 8500, 8500, 8500, 8500, 8500, 8500, 8500 };
    CHECK(avg(big, 8) == 8500);                            /* sum 68000 > INT16_MAX */
    CHECK(avg_n0_returns_invalid());                       /* n == 0 */
    end("D1 avg");

    /* D2 */
    begin();
    CHECK(timed_out(1000, 100, 500) == true);              /* normal, 900 elapsed */
    CHECK(timed_out(300, 100, 500) == false);              /* normal, 200 elapsed */
    CHECK(timed_out(600, 100, 500) == true);               /* exactly timeout */
    CHECK(timed_out(0xFFFFFF10u, 0xFFFFFF00u, 512) == false);  /* 16 elapsed, start+timeout wraps */
    CHECK(timed_out(0x00000150u, 0xFFFFFF00u, 512) == true);   /* 592 elapsed across the wrap */
    end("D2 timed_out");

    /* D3 */
    begin();
    int v = 0;
    rb_head = rb_tail = 0;
    for (int i = 0; i < 7; i++) CHECK(push(i));            /* capacity is RB_N - 1 = 7 */
    CHECK(push(99) == false);                              /* full */
    for (int i = 0; i < 7; i++) CHECK(pop(&v) && v == i);
    CHECK(push(100));                                      /* writes slot 7, head must wrap to 0 */
    CHECK(rb_head < RB_N);                                 /* checked BEFORE the next write */
    if (rb_head < RB_N) {
        CHECK(push(101));
        CHECK(pop(&v) && v == 100);
        CHECK(pop(&v) && v == 101);
    }
    end("D3 ring push");

    /* D4 */
    begin();
    const int16_t r[4] = { 1, 2, 3, 4 };
    d4_cnt = 0; d4_bad_index = false;
    process_reverse(r, 4);
    CHECK(!d4_bad_index && d4_cnt == 4);
    CHECK(d4_seen[0] == 4 && d4_seen[3] == 1);
    d4_cnt = 0; d4_bad_index = false;
    process_reverse(r, 0);                                 /* n == 0 */
    CHECK(!d4_bad_index && d4_cnt == 0);
    end("D4 reverse loop");

    /* D5 */
    begin();
    CHECK(decode_temp12(0x19, 0x00) == 400);               /* +25 degC */
    CHECK(decode_temp12(0x7F, 0xF0) == 2047);              /* max +127.9375 degC */
    CHECK(decode_temp12(0xE7, 0x00) == -400);              /* -25 degC */
    CHECK(decode_temp12(0xFF, 0xF0) == -1);                /* -0.0625 degC */
    CHECK(decode_temp12(0x80, 0x00) == -2048);             /* min -128 degC */
    end("D5 sign extension");

    /* D6 */
    begin();
    overheat_count = 0; g_reported = 0;
    task();
    CHECK(g_reported == 0);                                /* nothing to report */
    for (int i = 0; i < 3; i++) TEMP_IRQHandler();         /* 3 events */
    g_irq_fire_once = true;                                /* 4th event lands inside task() */
    task();
    task();                                                /* next loop iteration */
    CHECK(g_reported + overheat_count == 4);               /* no event lost */
    CHECK(g_reported == 4);
    end("D6 ISR counter race");

    /* D7 */
    begin();
    Sample s = { 123456u, -55, 0xBEEF, 77u, 0xDEADBEEFu };
    clear(&s);
    CHECK(s.t_ms == 0 && s.temp_c10 == 0 && s.flags == 0);
    CHECK(s.seq == 0 && s.crc == 0);
    end("D7 memset sizeof");

    /* D8 */
    begin();
    Debounce d = { 0, NORMAL };
    const int16_t steady[3] = { 900, 900, 900 };
    for (int i = 0; i < 3; i++) deb_update(&d, steady[i]);
    CHECK(d.state == OVERHEAT);                            /* normal: 3 in a row */
    Debounce e = { 0, NORMAL };
    const int16_t spiky[5] = { 900, 700, 900, 700, 900 };
    for (int i = 0; i < 5; i++) deb_update(&e, spiky[i]);
    CHECK(e.state == NORMAL);                              /* 3 isolated spikes are not overheat */
    Debounce f = { 0, NORMAL };
    const int16_t edge[4] = { 900, 900, 799, 900 };
    for (int i = 0; i < 4; i++) deb_update(&f, edge[i]);
    CHECK(f.state == NORMAL);                              /* 799 < HIGH breaks the run */
    end("D8 debounce reset");

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
