/*
 * E3  Data-Ready ISR -> Task Overheat Pipeline
 *
 * A temperature ADC raises a DATA-READY interrupt every 10 ms. The ISR must be tiny:
 * it captures (millis(), ADC_DATA) and pushes it into an SPSC ring buffer. A main-loop
 * task (temp_task_poll) drains the ring, converts ADC codes to 0.1 degC, runs a
 * hysteresis detector and switches the fan. Lost samples must be COUNTED, never silent.
 *
 * Definitions (agreed with the interviewer):
 *   - Ring: RB_SIZE (power of two) RawSample slots, all usable. head/tail are free-running
 *     uint32_t C11 atomics; ISR writes only head, task writes only tail.
 *     Full ring -> drop the NEW sample and increment g_overrun_count (ISR side).
 *   - adc_to_c10(code): use the low 12 bits only. mV = code * 3300 / 4096 (truncate),
 *     c10 = mV - 500   (TMP36-style: 500 mV at 0 degC, 10 mV/degC).
 *   - temp_task_poll(), in this order:
 *       1) read-and-clear g_overrun_count with IRQs masked (irq_disable/irq_enable),
 *          add it to g_task.overruns_total. Simulation rule: call SIM_PREEMPT() between
 *          the read and the clear -- it is where a real IRQ could arrive.
 *       2) pop every sample. If a previous sample exists and (t - last_t) > STALE_MS
 *          (wrap-safe), reset the detector streak first. Then det_update().
 *          EV_OVERHEAT -> fan_set(true), EV_RECOVERED -> fan_set(false), and log
 *          {sample t_ms, c10, event} (write at most LOG_CAP, always count n_log).
 *   - No malloc, no real threads: the harness calls the ISR at scripted points.
 *
 * Build & run:  ./run.sh E3
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdatomic.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

#define RB_SIZE   8u                 /* must be a power of two */
#define RB_MASK   (RB_SIZE - 1u)
#define ADC_MASK  0x0FFFu            /* 12-bit converter; bits 15..12 are status junk */
#define VREF_MV   3300u
#define ADC_FULL  4096u
#define STALE_MS  50u                /* a gap larger than this restarts confirmation */
#define LOG_CAP   8u
#define PERIOD_MS 10u                /* harness: DRDY period */

/* ================= provided: fake hardware + platform ================= */
volatile uint16_t ADC_DATA;          /* fake data register (reading it acks DRDY) */
volatile uint32_t g_now_ms;
uint32_t millis(void) { return g_now_ms; }

bool g_fan_on;
int  g_fan_calls;
void fan_set(bool on) { g_fan_on = on; g_fan_calls++; }

void TEMP_DRDY_IRQHandler(void);

/* Simulated NVIC: one pending latch per IRQ line. A second edge while pending is lost. */
static bool g_irq_masked, g_irq_pending;
static uint32_t g_irq_raised, g_irq_coalesced;
void irq_disable(void) { g_irq_masked = true; }
void irq_enable(void)
{
    g_irq_masked = false;
    if (g_irq_pending) { g_irq_pending = false; TEMP_DRDY_IRQHandler(); }  /* pended IRQ fires now */
}
static void raise_irq(void)
{
    g_irq_raised++;
    if (!g_irq_masked) { TEMP_DRDY_IRQHandler(); return; }
    if (g_irq_pending) g_irq_coalesced++;
    g_irq_pending = true;
}
/* harness: one scripted DRDY edge, then time advances by one period */
static void irq(uint16_t code) { ADC_DATA = code; raise_irq(); g_now_ms += PERIOD_MS; }

static int      g_preempt_irqs;      /* how many IRQs the harness injects at SIM_PREEMPT() */
static uint16_t g_preempt_code;
void SIM_PREEMPT(void) { while (g_preempt_irqs > 0) { g_preempt_irqs--; irq(g_preempt_code); } }

/* ================= provided: hysteresis detector (from T1-2) ================= */
typedef enum { EV_NONE, EV_OVERHEAT, EV_RECOVERED } Event;
typedef struct { int16_t high, low; uint8_t n_confirm, streak; bool overheated; } Detector;

void det_init(Detector *d, int16_t high, int16_t low, uint8_t n_confirm)
{
    *d = (Detector){ high, low, n_confirm, 0, false };
}
Event det_update(Detector *d, int16_t c10)
{
    bool toward = d->overheated ? (c10 <= d->low) : (c10 >= d->high);
    if (!toward) { d->streak = 0; return EV_NONE; }
    if (++d->streak < d->n_confirm) return EV_NONE;
    d->streak = 0;
    d->overheated = !d->overheated;
    return d->overheated ? EV_OVERHEAT : EV_RECOVERED;
}

/* ================= shared state ================= */
typedef struct { uint32_t t_ms; uint16_t code; } RawSample;
typedef struct {
    RawSample buf[RB_SIZE];
    _Atomic uint32_t head;           /* written only by the ISR (producer)  */
    _Atomic uint32_t tail;           /* written only by the task (consumer) */
} SampleRing;

typedef struct { uint32_t t_ms; int16_t c10; Event ev; } LogEntry;
typedef struct {
    Detector det;
    bool     has_last;
    uint32_t last_t_ms;              /* capture time of the last processed sample */
    uint32_t processed;
    uint32_t overruns_total;
    LogEntry log[LOG_CAP];
    uint32_t n_log;
} TempTask;

SampleRing        g_ring;
volatile uint32_t g_overrun_count;   /* ISR increments, task reads-and-clears */
TempTask          g_task;

void temp_task_init(int16_t high, int16_t low, uint8_t n_confirm)
{
    atomic_store(&g_ring.head, 0u);
    atomic_store(&g_ring.tail, 0u);
    g_overrun_count = 0;
    memset(&g_task, 0, sizeof g_task);
    det_init(&g_task.det, high, low, n_confirm);
}

/* ================= your code ================= */
int16_t adc_to_c10(uint16_t code)
{
    /* TODO: 12-bit code -> 0.1 degC */
    (void)code;
    return 0;
}

bool ring_push(SampleRing *r, RawSample s)            /* ISR side */
{
    /* TODO: SPSC push, drop newest when full */
    (void)r; (void)s;
    return false;
}

bool ring_pop(SampleRing *r, RawSample *out)          /* task side */
{
    /* TODO: SPSC pop */
    (void)r; (void)out;
    return false;
}

void TEMP_DRDY_IRQHandler(void)
{
    /* TODO: capture (millis(), ADC_DATA), push, count overruns. Keep it tiny. */
}

void temp_task_poll(void)
{
    /* TODO: 1) read-and-clear g_overrun_count safely (call SIM_PREEMPT() between read and clear)
             2) drain the ring: stale check, convert, det_update, fan_set, log */
}

/* ================= tests ================= */
/* ADC codes -> c10 (computed with adc_to_c10) */
enum { C760 = 1564, C770 = 1577, C780 = 1589, C790 = 1602, C800 = 1614, C820 = 1639,
       C830 = 1651, C840 = 1664, C852 = 1679, C860 = 1689, C870 = 1701, C880 = 1713,
       C700 = 1490, C900 = 1738 };

static void reset(uint32_t t0)
{
    g_now_ms = t0;
    g_irq_masked = g_irq_pending = false;
    g_irq_raised = g_irq_coalesced = 0;
    g_preempt_irqs = 0;
    g_fan_on = false; g_fan_calls = 0;
    temp_task_init(850, 800, 3);            /* on at >= 85.0 C x3, off at <= 80.0 C x3 */
}
static uint32_t ring_count(void) { return atomic_load(&g_ring.head) - atomic_load(&g_ring.tail); }
/* every raised IRQ must be processed, still queued, counted as overrun, or coalesced */
static bool conserved(void)
{
    return g_irq_raised == g_task.processed + ring_count() + g_task.overruns_total
                           + g_overrun_count + g_irq_coalesced;
}

int main(void)
{
    /* 1. normal: ramp up -> fan on at sample 4 (t=1040), cool down -> fan off (t=1090).
          Task polls after every 2 IRQs, so events are reported with CAPTURE time. */
    reset(1000);
    const uint16_t up[] = { C820, C840, C852, C860, C870, C880 };
    for (int i = 0; i < 6; i++) {
        irq(up[i]);
        if (i % 2 == 1) temp_task_poll();
        if (i == 3) CHECK(!g_fan_on && g_fan_calls == 0);
    }
    CHECK(g_fan_on && g_fan_calls == 1 && g_task.n_log == 1);
    CHECK(g_task.log[0].ev == EV_OVERHEAT && g_task.log[0].t_ms == 1040 && g_task.log[0].c10 == 870);
    const uint16_t down[] = { C830, C790, C780, C770, C760, C760 };   /* 830 is in the band */
    for (int i = 0; i < 6; i++) { irq(down[i]); if (i % 2 == 1) temp_task_poll(); }
    CHECK(!g_fan_on && g_fan_calls == 2 && g_task.n_log == 2);
    CHECK(g_task.log[1].ev == EV_RECOVERED && g_task.log[1].t_ms == 1090 && g_task.log[1].c10 == 770);
    CHECK(g_task.processed == 12 && g_task.overruns_total == 0 && conserved());

    /* 2. conversion: endpoints, truncation below 0 C, status bits ignored */
    CHECK(adc_to_c10(0) == -500);
    CHECK(adc_to_c10(4095) == 2799);
    CHECK(adc_to_c10(620) == -1);
    CHECK(adc_to_c10(1676) == 850 && adc_to_c10(0xF000u | 1676u) == 850);

    /* 3. burst: 20 IRQs while the task is blocked, ring holds 8 -> 12 overruns, oldest kept */
    reset(0);
    for (int i = 0; i < 20; i++) irq(C800);
    CHECK(ring_count() == RB_SIZE && g_overrun_count == 12);
    temp_task_poll();
    CHECK(g_task.processed == 8 && g_task.overruns_total == 12 && g_overrun_count == 0);
    CHECK(ring_count() == 0 && g_task.last_t_ms == 70);    /* samples 0..70 ms survived */
    irq(C800); temp_task_poll();                            /* t = 200: pipeline still sane */
    CHECK(g_task.processed == 9 && g_task.last_t_ms == 200 && g_task.overruns_total == 12);
    CHECK(g_fan_calls == 0 && conserved());

    /* 4. race: an IRQ arrives between read and clear of g_overrun_count (ring is full) */
    reset(0);
    for (int i = 0; i < 10; i++) irq(C800);                 /* 8 queued, 2 overruns */
    g_preempt_irqs = 1; g_preempt_code = C800;
    temp_task_poll();
    CHECK(g_preempt_irqs == 0 && g_task.processed == 8 && conserved());
    temp_task_poll();                                       /* collects anything left over */
    CHECK(g_task.overruns_total == 3 && g_overrun_count == 0 && g_irq_raised == 11 && conserved());

    /* 5. spikes: never 3 hot samples in a row -> fan never touched */
    reset(0);
    const uint16_t sp[] = { C900, C700, C900, C700, C900, C900, C700 };
    for (int i = 0; i < 7; i++) { irq(sp[i]); temp_task_poll(); }
    CHECK(g_fan_calls == 0 && !g_fan_on && g_task.n_log == 0);

    /* 6. millis() wraps during confirmation: t = 4294967281, 4294967291, 5 -> event at t=5 */
    reset(UINT32_MAX - 14u);
    for (int i = 0; i < 3; i++) irq(C870);
    temp_task_poll();
    CHECK(g_fan_on && g_task.n_log == 1 && g_task.log[0].t_ms == 5);

    /* 7. stale gap: 0, 10, (gap 60 ms) 70, 80, 90 -> streak restarts at 70, event at 90 */
    reset(0);
    irq(C870); irq(C870);
    g_now_ms = 70;
    irq(C870); irq(C870); temp_task_poll();
    CHECK(!g_fan_on && g_fan_calls == 0);
    irq(C870); temp_task_poll();
    CHECK(g_fan_on && g_task.n_log == 1 && g_task.log[0].t_ms == 90);

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
