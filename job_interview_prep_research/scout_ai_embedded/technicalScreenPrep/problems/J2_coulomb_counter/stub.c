/*
 * J2  Battery State of Charge by Coulomb Counting
 *
 * A shunt amplifier feeds a 12-bit ADC. Convert each reading to current and integrate
 * it over time to track the remaining charge of a battery pack.
 *
 *   current_mA = ((int32_t)raw - ZERO_RAW) * MA_PER_COUNT_NUM / MA_PER_COUNT_DEN
 *   ZERO_RAW = 2048, 10 mA per count.  Positive = discharge, negative = charging.
 *   ADC codes 0 and 4095 are saturated: do not integrate, set s->fault, count it.
 *
 * Rules (agreed with the interviewer):
 *   - dt = (uint32_t)(now_ms - last_ms)  (wrap-safe). Every call updates last_ms.
 *   - The current measured at now_ms is used for the whole interval (last, now].
 *   - dt > MAX_GAP_MS is a gap (missed samples): do not integrate, count it.
 *   - Accumulate charge in mA*ms (int64) so slow currents are never lost to rounding.
 *   - Remaining charge is clamped to [0, capacity].
 *   - soc_permille() = remaining * 1000 / capacity, rounded down, 0..1000.
 *   - Low-battery alert with hysteresis: ON at <= 150 permille, OFF at >= 200 permille.
 *   - Part C: ocv_to_permille() maps a rest voltage through a 7-point table with
 *     integer linear interpolation (clamped at both ends); soc_rest_calibrate()
 *     resets the remaining charge from it.
 *   - 1 <= capacity_mAh <= 1000000. No malloc, no floating point.
 *
 * Build & run:  ./run.sh J2
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

#define ADC_MAX          4095u
#define ZERO_RAW         2048
#define MA_PER_COUNT_NUM 10
#define MA_PER_COUNT_DEN 1
#define MAX_GAP_MS       1000u
#define MAMS_PER_MAH     3600000LL   /* 1 mAh = 3600 s * 1000 ms */
#define LOW_ON_PERMILLE  150u
#define LOW_OFF_PERMILLE 200u

typedef struct {
    int64_t  cap_mAms;    /* capacity in mA*ms */
    int64_t  rem_mAms;    /* remaining charge in mA*ms, 0..cap_mAms */
    uint32_t last_ms;     /* time of the previous call */
    uint32_t gaps;        /* intervals skipped because dt > MAX_GAP_MS */
    uint32_t faults;      /* saturated ADC samples seen */
    bool     fault;       /* the most recent sample was saturated */
    bool     low_alert;
} Soc;

int32_t current_mA(uint16_t raw)
{
    /* TODO: convert an ADC code to mA (positive = discharge) */
    (void)raw;
    return 0;
}

uint16_t soc_permille(const Soc *s)
{
    /* TODO: remaining / capacity in 0..1000 */
    (void)s;
    return 0;
}

void soc_init(Soc *s, uint32_t capacity_mAh, uint16_t soc_pm, uint32_t now_ms)
{
    /* TODO: your code here */
    (void)s; (void)capacity_mAh; (void)soc_pm; (void)now_ms;
}

void soc_update(Soc *s, uint16_t adc_raw, uint32_t now_ms)
{
    /* TODO: wrap-safe dt, fault/gap checks, integrate in mA*ms, clamp, hysteresis */
    (void)s; (void)adc_raw; (void)now_ms;
}

bool soc_low_alert(const Soc *s)
{
    /* TODO */
    (void)s;
    return false;
}

/* ---- Part C: open-circuit-voltage recalibration ---- */
typedef struct { uint16_t mv, pm; } OcvPoint;
static const OcvPoint OCV[] = {
    {3300, 0}, {3500, 50}, {3600, 150}, {3700, 400}, {3800, 600}, {4000, 850}, {4200, 1000},
};
#define OCV_N (sizeof OCV / sizeof OCV[0])

uint16_t ocv_to_permille(uint16_t mv)
{
    /* TODO: clamp below/above the table, linear interpolation in between */
    (void)mv; (void)OCV;
    return 0;
}

void soc_rest_calibrate(Soc *s, uint16_t cell_mv)
{
    /* TODO: reset remaining charge from ocv_to_permille(cell_mv) */
    (void)s; (void)cell_mv;
}

int main(void)
{
    Soc s = {0};

    /* 1. normal discharge: 1000 mA for 3.6 s = 1 mAh = 10% of a 10 mAh pack */
    CHECK(current_mA(2148) == 1000 && current_mA(1948) == -1000 && current_mA(2048) == 0);
    soc_init(&s, 10, 1000, 0);
    CHECK(soc_permille(&s) == 1000 && !soc_low_alert(&s));
    for (uint32_t t = 100; t <= 3600; t += 100) soc_update(&s, 2148, t);
    CHECK(soc_permille(&s) == 900);
    CHECK(s.rem_mAms == 9 * MAMS_PER_MAH);
    soc_update(&s, 2048, 3700);                       /* zero current: no change */
    CHECK(soc_permille(&s) == 900 && s.gaps == 0 && s.faults == 0);

    /* 2. tiny current, many steps: 10 mA * 10 ms = 100 mA*ms per step.
     *    Dividing to mAh every step gives 0 each time; the accumulator must keep it. */
    soc_init(&s, 1, 1000, 0);
    for (uint32_t i = 1; i <= 18000; i++) soc_update(&s, 2049, i * 10);   /* 180 s */
    CHECK(soc_permille(&s) == 500);
    CHECK(s.rem_mAms == MAMS_PER_MAH / 2);

    /* 3. charging clamps at capacity (stored value, not just the display) */
    soc_init(&s, 10, 990, 0);
    soc_update(&s, 1688, 1000);                       /* -3600 mA for 1 s = +1 mAh */
    soc_update(&s, 1688, 2000);
    CHECK(soc_permille(&s) == 1000 && s.rem_mAms == s.cap_mAms);
    soc_update(&s, 2084, 3000);                       /* +360 mA for 1 s = -0.1 mAh */
    CHECK(soc_permille(&s) == 990);

    /* 4. deep discharge clamps at 0, then charging starts from 0 */
    soc_init(&s, 10, 50, 0);
    CHECK(soc_low_alert(&s));                         /* starts low -> alert on */
    soc_update(&s, 2408, 1000);                       /* 3600 mA for 1 s */
    soc_update(&s, 2408, 2000);
    CHECK(soc_permille(&s) == 0 && s.rem_mAms == 0);
    soc_update(&s, 2012, 3000);                       /* -360 mA for 1 s */
    CHECK(soc_permille(&s) == 10);

    /* 5. timer wrap: last = 2^32 - 500, now = 500 -> dt = 1000 */
    soc_init(&s, 10, 1000, 0xFFFFFE0Cu);
    soc_update(&s, 2084, 500u);
    CHECK(s.gaps == 0 && soc_permille(&s) == 990);

    /* 6. gap > MAX_GAP_MS: skip the interval, but resync last_ms */
    soc_init(&s, 10, 1000, 0);
    soc_update(&s, 2408, 1001);                       /* dt = 1001 -> gap */
    CHECK(s.gaps == 1 && soc_permille(&s) == 1000);
    soc_update(&s, 2408, 2001);                       /* dt = 1000 -> integrate */
    CHECK(s.gaps == 1 && soc_permille(&s) == 900);

    /* 7. saturated ADC codes are faults: no integration */
    soc_init(&s, 10, 1000, 0);
    soc_update(&s, 4095, 1000);
    CHECK(s.fault && s.faults == 1 && soc_permille(&s) == 1000);
    soc_update(&s, 0, 2000);
    CHECK(s.fault && s.faults == 2 && soc_permille(&s) == 1000);
    soc_update(&s, 2084, 3000);                       /* good sample clears the flag */
    CHECK(!s.fault && s.faults == 2 && soc_permille(&s) == 990);

    /* 8. hysteresis: 10 permille per 1 s step (360 mA on 10 mAh) */
    soc_init(&s, 10, 300, 0);
    uint32_t t = 0;
    int on_edges = 0, off_edges = 0;
    bool prev = soc_low_alert(&s);
    for (int i = 0; i < 25; i++) {
        uint16_t raw = (i < 16) ? 2084 : (i < 22) ? 2012 : 2084;  /* down, up, down */
        t += 1000;
        soc_update(&s, raw, t);
        bool now = soc_low_alert(&s);
        if (now && !prev) on_edges++;
        if (!now && prev) off_edges++;
        prev = now;
        if (t == 14000) CHECK(soc_permille(&s) == 160 && !now);
        if (t == 15000) CHECK(soc_permille(&s) == 150 && now);   /* ON at <= 150 */
        if (t == 16000) CHECK(soc_permille(&s) == 140 && now);
        if (t == 21000) CHECK(soc_permille(&s) == 190 && now);   /* still ON in band */
        if (t == 22000) CHECK(soc_permille(&s) == 200 && !now);  /* OFF at >= 200 */
        if (t == 25000) CHECK(soc_permille(&s) == 170 && !now);  /* no re-trigger */
    }
    CHECK(on_edges == 1 && off_edges == 1);

    /* 9. OCV table: clamps, exact points, interpolation (rounded down) */
    CHECK(ocv_to_permille(3000) == 0 && ocv_to_permille(4300) == 1000);
    CHECK(ocv_to_permille(3300) == 0 && ocv_to_permille(3700) == 400 && ocv_to_permille(4200) == 1000);
    CHECK(ocv_to_permille(3650) == 275);
    CHECK(ocv_to_permille(3900) == 725);
    CHECK(ocv_to_permille(3333) == 8);                /* 33 * 50 / 200 = 8.25 -> 8 */

    /* 10. rest calibration overwrites the counted charge and re-evaluates the alert */
    soc_init(&s, 10, 900, 0);
    soc_rest_calibrate(&s, 3650);
    CHECK(soc_permille(&s) == 275 && !soc_low_alert(&s));
    soc_rest_calibrate(&s, 3500);
    CHECK(soc_permille(&s) == 50 && soc_low_alert(&s));

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
