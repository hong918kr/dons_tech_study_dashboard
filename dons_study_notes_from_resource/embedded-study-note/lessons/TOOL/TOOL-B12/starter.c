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
    /* TODO: signed multiply overflow, den == 0, INT32_MIN / -1 */
    return raw * num / den;



}

int32_t accum_add(int32_t acc, int32_t x)
{
    /* TODO: saturate instead of overflowing */
    return acc + x;

}

uint32_t sample_mag(int32_t x)
{
    /* TODO: -INT32_MIN does not exist in int32_t */
    return (uint32_t)(x < 0 ? -x : x);
}

uint32_t reg_mask(unsigned lsb, unsigned width)
{
    /* TODO: 1 << 31 (into the sign bit), 1 << 32 (>= width), int not uint32_t */
    return (uint32_t)(((1 << width) - 1) << lsb);
}

uint16_t sample_to_u16(uint32_t raw, int adc_bits)
{
    /* TODO: adc_bits > 16 makes the shift count negative */
    return (uint16_t)(raw << (16 - adc_bits));
}

/* ---- Part B: memory and representation -------------------------------- */
int pkt_parse(const uint8_t *buf, size_t n, struct pkt_hdr *out)
{
    if (n < PKT_HDR_WIRE)
        return -1;
    out->type = buf[0];
    /* TODO: misaligned uint32_t load, wire byte read as bool, misaligned u16 */
    out->seq = *(const uint32_t *)(buf + 1);
    out->ack = *(const bool *)(buf + 5);

    out->len = *(const uint16_t *)(buf + 6);
    return 0;
}

static const char *const k_mode_names[3] = { "RUN", "SLEEP", "STOP" };

const char *pwr_mode_name(uint8_t wire)
{
    /* TODO: a wire byte is not a valid enum value just because you cast it */
    enum pwr_mode m = (enum pwr_mode)wire;
    return k_mode_names[m];
}

int32_t cal_apply(const struct cal *c, int32_t x)
{
    /* TODO: c may be NULL before calibration is loaded; offset may overflow */
    return x + c->offset;
}

static const uint16_t k_therm[16] = {
    3950, 3700, 3420, 3150, 2890, 2640, 2400, 2170,
    1950, 1740, 1540, 1350, 1170, 1000,  840,  690,
};

uint16_t therm_lookup(uint16_t adc12)
{
    /* TODO: a glitchy 0x1000 reading indexes k_therm[16] */
    return k_therm[adc12 >> 8];

}

/* ---- Part C: float -> int ---------------------------------------------- */
int16_t temp_to_centi(float deg_c)
{
    /* TODO: 400.0 C -> 40000 does not fit int16_t; NaN */
    return (int16_t)(deg_c * 100.0f);



}

uint16_t duty_to_ccr(float duty, uint16_t arr)
{
    /* TODO: negative duty is UB in the cast; duty > 1 is a plain logic bug */
    return (uint16_t)(duty * (float)arr);

}
