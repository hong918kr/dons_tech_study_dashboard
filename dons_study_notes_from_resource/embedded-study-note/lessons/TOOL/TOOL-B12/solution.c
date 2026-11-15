/* TOOL-B12 starter — firmware helpers that "work on the bench" but contain UB.
 * Every TODO body below has undefined behaviour for some legal input.
 * Rewrite each body UB-free (see fwub.h for the exact contract).
 * Rules: freestanding (no libc), no compiler builtins, keep the signatures.
 *   bash check.sh starter   -> builds driver.c + this file with
 *   -fsanitize=undefined -fno-sanitize-recover=all and runs every case. */
#include "fwub.h"

/* ---- Part A: arithmetic ------------------------------------------------ */
int32_t adc_scale(int32_t raw, int32_t num, int32_t den)
{
    if (den == 0) return 0;                  /* contract: no divide by zero  */
    int64_t q = (int64_t)raw * num / den;    /* |raw*num| <= 2^62: fits      */
    if (q > INT32_MAX) return INT32_MAX;     /* INT32_MIN / -1 lands here    */
    if (q < INT32_MIN) return INT32_MIN;
    return (int32_t)q;
}

int32_t accum_add(int32_t acc, int32_t x)
{
    int64_t s = (int64_t)acc + x;            /* cannot overflow in 64 bits   */
    if (s > INT32_MAX) return INT32_MAX;
    return s < INT32_MIN ? INT32_MIN : (int32_t)s;
}

uint32_t sample_mag(int32_t x)
{
    /* negate in unsigned arithmetic: wraps mod 2^32, defined */
    return x < 0 ? 0u - (uint32_t)x : (uint32_t)x;
}

uint32_t reg_mask(unsigned lsb, unsigned width)
{
    uint32_t ones = width >= 32u ? 0xFFFFFFFFu : (1u << width) - 1u;
    return ones << lsb;                      /* unsigned, count <= 31        */
}

uint16_t sample_to_u16(uint32_t raw, int adc_bits)
{
    if (adc_bits <= 16) return (uint16_t)(raw << (16 - adc_bits));
    return (uint16_t)(raw >> (adc_bits - 16));
}

/* ---- Part B: memory and representation -------------------------------- */
int pkt_parse(const uint8_t *buf, size_t n, struct pkt_hdr *out)
{
    if (n < PKT_HDR_WIRE)
        return -1;
    out->type = buf[0];
    if (buf[5] > 1u) return -2;              /* validate before it is a bool */
    out->seq = (uint32_t)buf[1] | (uint32_t)buf[2] << 8 |
               (uint32_t)buf[3] << 16 | (uint32_t)buf[4] << 24;
    out->ack = buf[5] != 0u;
    out->len = (uint16_t)(buf[6] | buf[7] << 8);
    return 0;
}

static const char *const k_mode_names[3] = { "RUN", "SLEEP", "STOP" };

const char *pwr_mode_name(uint8_t wire)
{
    if (wire > PWR_STOP)                     /* range-check the raw byte     */
        return "?";
    return k_mode_names[wire];
}

int32_t cal_apply(const struct cal *c, int32_t x)
{
    if (c == NULL) return x;                 /* not loaded yet: identity     */
    return accum_add(x, c->offset);          /* saturating, no overflow      */
}

static const uint16_t k_therm[16] = {
    3950, 3700, 3420, 3150, 2890, 2640, 2400, 2170,
    1950, 1740, 1540, 1350, 1170, 1000,  840,  690,
};

uint16_t therm_lookup(uint16_t adc12)
{
    unsigned i = adc12 >> 8u;
    if (i > 15u) i = 15u;                    /* clamp glitches to last entry */
    return k_therm[i];
}

/* ---- Part C: float -> int ---------------------------------------------- */
int16_t temp_to_centi(float deg_c)
{
    float v = deg_c * 100.0f;
    if (v != v) return 0;                    /* NaN (no <math.h> here)       */
    if (v >= 32767.0f) return INT16_MAX;     /* range check BEFORE the cast  */
    if (v <= -32768.0f) return INT16_MIN;
    return (int16_t)v;                       /* now in range: truncation     */
}

uint16_t duty_to_ccr(float duty, uint16_t arr)
{
    if (!(duty > 0.0f)) return 0;            /* negative, zero and NaN       */
    if (duty >= 1.0f) return arr;
    return (uint16_t)(duty * (float)arr);    /* 0 < value < arr: fits        */
}
