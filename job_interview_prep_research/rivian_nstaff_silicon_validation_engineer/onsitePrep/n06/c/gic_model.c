/*
 * gic_model.c -- N06C C6 practice: a tiny GIC-like interrupt controller model + directed tests
 *                + a toy interrupt-latency Monte Carlo (why you report p99/max, not mean).
 *
 * Modelled after the GICv3 concepts in Arm "Learn the architecture - GIC v3 and v4, Overview"
 * (198123, v3.2) -- simplified, single PE, one group, EOImode=0 (EOI = priority drop + deactivate):
 *   - INTID 0-15 SGI (always edge; config writes ignored), 16-31 PPI, 32+ SPI. 1023 = spurious.
 *   - Per-INTID: enable, priority (8-bit, 0x00 highest), edge/level, pending, active.
 *   - Level: pending follows the input line; Edge: a rising edge latches pending.
 *   - Signalled iff enabled && pending && !active && prio < PMR && group-prio < running group-prio.
 *   - IAR: returns highest-priority candidate (tie -> lowest INTID in this model), makes it active,
 *     edge clears pending, running priority := its priority. EOI: pop running priority, deactivate.
 * Register names are architectural concepts; this is NOT a register-accurate model.
 *
 * Build: cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined gic_model.c
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#define NINT 64
#define SPURIOUS 1023
#define IDLE_PRIO 0xFF

typedef struct { bool en, edge, line, pend, act; uint8_t prio; } irq_t;
typedef struct {
    irq_t q[NINT];
    uint8_t pmr;          /* priority mask: only prio < pmr is signalled */
    int sub_bits;         /* binary point: low sub_bits are sub-priority (ignored for preemption) */
    uint8_t rp_stack[16]; int rp_id[16]; int depth;
} gic_t;

static void gic_reset(gic_t *g)
{
    memset(g, 0, sizeof *g);
    for (int i = 0; i < NINT; i++) { g->q[i].prio = 0xA0; g->q[i].edge = (i < 16); }
    g->pmr = 0xF0; g->sub_bits = 0;
}
static void cfg(gic_t *g, int id, bool en, bool edge, uint8_t prio)
{
    g->q[id].en = en; g->q[id].prio = prio;
    if (id >= 16) g->q[id].edge = edge;     /* SGI config is RAO/WI: always edge */
}
static uint8_t running(const gic_t *g) { return g->depth ? g->rp_stack[g->depth - 1] : IDLE_PRIO; }
static uint8_t grp(const gic_t *g, uint8_t p) { return (uint8_t)(p & (0xFF << g->sub_bits)); }

static void line(gic_t *g, int id, bool v)
{
    irq_t *q = &g->q[id];
    if (q->edge) { if (v && !q->line) q->pend = true; }
    else q->pend = v;                       /* level: pending tracks the input */
    q->line = v;
}
static void pulse(gic_t *g, int id) { line(g, id, true); line(g, id, false); }

static int hppi(const gic_t *g)
{
    int best = SPURIOUS; uint8_t bp = 0xFF;
    for (int i = 0; i < NINT; i++) {
        const irq_t *q = &g->q[i];
        if (!q->en || !q->pend || q->act) continue;
        if (q->prio < bp) { bp = q->prio; best = i; }   /* strict < : tie keeps lowest INTID */
    }
    if (best == SPURIOUS) return SPURIOUS;
    if (!(bp < g->pmr)) return SPURIOUS;
    if (!(grp(g, bp) < grp(g, running(g)))) return SPURIOUS;  /* not enough to preempt */
    return best;
}
static bool irq_asserted(const gic_t *g) { return hppi(g) != SPURIOUS; }

static int iar(gic_t *g)
{
    int id = hppi(g);
    if (id == SPURIOUS) return SPURIOUS;    /* read of spurious does not acknowledge anything */
    irq_t *q = &g->q[id];
    q->act = true;
    if (q->edge) q->pend = false;           /* level stays pending while line is high */
    g->rp_stack[g->depth] = q->prio; g->rp_id[g->depth] = id; g->depth++;
    return id;
}
static int eoi(gic_t *g, int id)
{
    if (g->depth == 0 || g->rp_id[g->depth - 1] != id) return -1;  /* out-of-order EOI: SW bug */
    g->depth--; g->q[id].act = false;
    if (!g->q[id].edge) g->q[id].pend = g->q[id].line;
    return 0;
}

static int fails, total;
#define CHECK(name, cond) do { total++; bool _ok = (cond); \
    printf("RESULT %-58s -> %s\n", name, _ok ? "PASS" : "FAIL"); if (!_ok) fails++; } while (0)

static void directed_tests(void)
{
    gic_t g; int id, n;

    gic_reset(&g);
    CHECK("C6-R01 after reset: nothing enabled, IAR reads 1023", iar(&g) == SPURIOUS);

    gic_reset(&g); cfg(&g, 40, true, true, 0x80);
    pulse(&g, 40); id = iar(&g); pulse(&g, 40);          /* 2nd edge while active */
    bool ap = g.q[40].act && g.q[40].pend;
    eoi(&g, 40); int id2 = iar(&g); eoi(&g, 40);
    CHECK("C6-E01 edge: re-assert while active -> active+pending -> 2nd delivery", id == 40 && ap && id2 == 40);

    gic_reset(&g); cfg(&g, 41, true, true, 0x80);
    pulse(&g, 41); pulse(&g, 41); pulse(&g, 41);         /* 3 edges before ack */
    n = 0; while ((id = iar(&g)) != SPURIOUS) { n++; eoi(&g, id); }
    CHECK("C6-E02 edge: 3 edges before ack coalesce into 1 delivery (count in device!)", n == 1);

    gic_reset(&g); cfg(&g, 50, true, false, 0x80);
    line(&g, 50, true); id = iar(&g);
    bool still = g.q[50].pend;                            /* active+pending while line high */
    eoi(&g, 50);                                          /* handler forgot to clear device */
    int again = iar(&g);
    CHECK("C6-L01 level: EOI without clearing device -> immediate re-delivery (storm)", id == 50 && still && again == 50);
    line(&g, 50, false); eoi(&g, 50);
    CHECK("C6-L02 level: clear device then EOI -> no re-delivery", iar(&g) == SPURIOUS);

    gic_reset(&g); cfg(&g, 51, true, false, 0x80);
    line(&g, 51, true); bool asserted = irq_asserted(&g);
    line(&g, 51, false);                                  /* glitch: dropped before ack */
    CHECK("C6-L03 level: deassert before ack -> IAR 1023 (handler must tolerate spurious)", asserted && iar(&g) == SPURIOUS);

    gic_reset(&g); cfg(&g, 33, true, true, 0x90); cfg(&g, 34, true, true, 0x40); cfg(&g, 35, true, true, 0x40);
    pulse(&g, 33); pulse(&g, 34); pulse(&g, 35);
    int o1 = iar(&g); eoi(&g, o1); int o2 = iar(&g); eoi(&g, o2); int o3 = iar(&g); eoi(&g, o3);
    printf("        order taken: %d %d %d (prio 34=0x40 35=0x40 33=0x90)\n", o1, o2, o3);
    CHECK("C6-P01 priority: lowest value first, tie -> lowest INTID (model rule)", o1 == 34 && o2 == 35 && o3 == 33);

    gic_reset(&g); cfg(&g, 36, true, true, 0xF0); g.pmr = 0xF0;
    pulse(&g, 36);
    bool masked = (iar(&g) == SPURIOUS);
    g.pmr = 0xF8; id = iar(&g);
    CHECK("C6-P02 PMR: prio 0xF0 masked at PMR 0xF0, delivered at PMR 0xF8", masked && id == 36);

    /* Arm guide example: A=0x10, B=0x20, C=0x21, binary point so that low 4 bits are sub-priority */
    gic_reset(&g); g.sub_bits = 4;
    cfg(&g, 37, true, true, 0x10); cfg(&g, 38, true, true, 0x20); cfg(&g, 39, true, true, 0x21);
    pulse(&g, 39); int c = iar(&g);                       /* handling C */
    pulse(&g, 38); bool b_blocked = (iar(&g) == SPURIOUS);/* B same group prio -> no preempt */
    pulse(&g, 37); int a = iar(&g);                       /* A preempts */
    eoi(&g, a); eoi(&g, c); int b = iar(&g); eoi(&g, b);
    CHECK("C6-P03 preemption: A(0x10) preempts C(0x21); B(0x20) waits (group prio)", c == 39 && b_blocked && a == 37 && b == 38);

    gic_reset(&g); cfg(&g, 45, false, true, 0x80);
    pulse(&g, 45); bool held = g.q[45].pend && iar(&g) == SPURIOUS;
    g.q[45].en = true;
    CHECK("C6-N01 disabled INTID latches pending, delivered after enable", held && iar(&g) == 45);

    gic_reset(&g); cfg(&g, 5, true, false /* try level */, 0x80);
    pulse(&g, 5); id = iar(&g);
    CHECK("C6-S01 SGI config write ignored: stays edge (RAO/WI)", g.q[5].edge && id == 5);

    gic_reset(&g); cfg(&g, 46, true, true, 0x80); cfg(&g, 47, true, true, 0x40);
    g.sub_bits = 0; pulse(&g, 46); int x = iar(&g); pulse(&g, 47); int y = iar(&g);
    CHECK("C6-N02 out-of-order EOI (EOI outer before inner) is rejected by model", eoi(&g, x) == -1 && eoi(&g, y) == 0 && eoi(&g, x) == 0);
}

/* ---------- toy latency Monte Carlo ---------- */
static uint32_t rng = 12345u;
static double urand(void) { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0; }
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

static void latency_mc(const char *name, bool preempt, double lo_isr_us, double lo_duty, double crit_us, double crit_duty)
{
    enum { N = 200000 };
    static double s[N];
    const double entry_us = 0.4;   /* exception entry + IAR read + dispatch [assumed] */
    for (int i = 0; i < N; i++) {
        double lat = entry_us;
        if (!preempt && urand() < lo_duty) lat += urand() * lo_isr_us;    /* wait for low ISR */
        if (urand() < crit_duty)          lat += urand() * crit_us;       /* IRQs masked section */
        s[i] = lat;
    }
    qsort(s, N, sizeof s[0], cmpd);
    double sum = 0; for (int i = 0; i < N; i++) sum += s[i];
    printf("LAT %-34s mean=%5.2f p50=%5.2f p99=%5.2f p99.9=%5.2f max=%5.2f us (N=%d)\n",
           name, sum / N, s[N / 2], s[(int)(N * 0.99)], s[(int)(N * 0.999)], s[N - 1], N);
}

int main(void)
{
    directed_tests();
    printf("SUMMARY %d/%d PASS\n\n", total - fails, total);
    /* low-prio ISR 20 us busy 10% of the time; 30 us IRQ-masked section 1% of the time */
    latency_mc("no preemption, 20us low ISR, 30us crit", false, 20.0, 0.10, 30.0, 0.01);
    latency_mc("preemption on, same load",               true,  20.0, 0.10, 30.0, 0.01);
    latency_mc("preemption on, crit section cut to 2us", true,  20.0, 0.10,  2.0, 0.01);
    return fails ? 1 : 0;
}
