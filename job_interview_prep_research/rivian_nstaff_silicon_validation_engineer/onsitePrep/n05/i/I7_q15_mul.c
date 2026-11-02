/* I7 - Q15 fixed-point multiply with rounding + saturation.
 * Context: ISP gain / white-balance coefficient, sensor calibration on a core
 * without (or avoiding) FP. Build/run: ./run.sh
 */
//@@BEGIN I7
#include <stdint.h>

#define Q15_MAX   32767
#define Q15_MIN (-32768)

static int16_t sat16(int32_t x)
{
    if (x > Q15_MAX) return Q15_MAX;
    if (x < Q15_MIN) return Q15_MIN;
    return (int16_t)x;
}

/* a * b in Q15, round half up, saturate.
 * >> of a negative int is implementation-defined in C11; gcc/clang do an
 * arithmetic shift (floor), which this rounding relies on. */
int16_t q15_mul(int16_t a, int16_t b)
{
    int32_t p = (int32_t)a * b;   /* Q30, |p| <= 2^30: fits int32 */
    p += 1 << 14;                 /* + 0.5 LSB of the Q15 result  */
    return sat16(p >> 15);        /* only -1 * -1 needs the clamp */
}
//@@END I7

//@@BEGIN I7_add
/* Follow-up: saturating add and float -> Q15 (host side / table generation). */
int16_t q15_add(int16_t a, int16_t b) { return sat16((int32_t)a + b); }

int16_t q15_from_double(double x)
{
    double s = x * 32768.0;
    s += (s >= 0) ? 0.5 : -0.5;           /* round half away from zero */
    if (s >= 32767.0) return Q15_MAX;
    if (s <= -32768.0) return Q15_MIN;
    return (int16_t)s;                    /* truncation toward zero */
}
//@@END I7_add

//@@BEGIN I7_q31
/* Follow-up: Q31 (e.g. NPU requant multiplier) - same idea in 64-bit. */
int32_t q31_mul(int32_t a, int32_t b)
{
    int64_t p = (int64_t)a * b;      /* Q62 */
    p += (int64_t)1 << 30;           /* + 0.5 LSB */
    p >>= 31;
    if (p > INT32_MAX) return INT32_MAX;   /* only INT32_MIN * INT32_MIN */
    return (int32_t)p;
}
//@@END I7_q31

//@@BEGIN I7_asr
/* Follow-up: floor(p / 2^15) without relying on signed >> behaviour. */
static int32_t asr15_portable(int32_t p)
{
    if (p >= 0) return p >> 15;
    return -(int32_t)(((0u - (uint32_t)p) + 32767u) >> 15);   /* -ceil(-p/2^15) */
}
//@@END I7_asr

#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(q15_mul(0x4000, 0x4000) == 0x2000);            /* 0.5 * 0.5 = 0.25 */
    assert(q15_mul(Q15_MIN, Q15_MIN) == Q15_MAX);         /* -1 * -1 saturates */
    assert(q15_mul(Q15_MIN, 0x4000) == -16384);           /* -1 * 0.5 = -0.5 */
    assert(q15_mul(Q15_MAX, Q15_MAX) == 32766);           /* 0.99997^2 */
    assert(q15_mul(Q15_MIN, Q15_MAX) == -32767);
    assert(q15_mul(0, Q15_MIN) == 0);
    /* rounding edges */
    assert(q15_mul(1, 1) == 0);                           /* 2^-30 -> 0 */
    assert(q15_mul(1, 0x4000) == 1);                      /* exactly 0.5 LSB -> up */
    assert(q15_mul(-1, 0x4000) == 0);                     /* -0.5 LSB -> 0 (half up) */
    assert(q15_mul(-1, 0x4001) == -1);                    /* just past -0.5 LSB */
    /* commutative */
    assert(q15_mul(12345, -6789) == q15_mul(-6789, 12345));

    assert(q15_add(30000, 10000) == Q15_MAX && q15_add(-30000, -10000) == Q15_MIN);
    assert(q15_add(100, -50) == 50);
    assert(q15_from_double(0.5) == 0x4000 && q15_from_double(-1.0) == Q15_MIN);
    assert(q15_from_double(1.0) == Q15_MAX && q15_from_double(-0.25) == -8192);

    assert(q31_mul(INT32_MIN, INT32_MIN) == INT32_MAX);
    assert(q31_mul(0x40000000, 0x40000000) == 0x20000000);
    assert(q31_mul(INT32_MIN, 0x40000000) == -0x40000000);
    for (int32_t p = -(1 << 30) - (1 << 14); p <= (1 << 30) + (1 << 14); p += 9973)
        assert(asr15_portable(p) == (p >> 15));
    assert(asr15_portable(-1) == -1 && asr15_portable(-32768) == -1 && asr15_portable(-32769) == -2);

    /* platform check that the comment relies on */
    assert((-3 >> 1) == -2);
    printf("I7 PASS\n");
    return 0;
}
