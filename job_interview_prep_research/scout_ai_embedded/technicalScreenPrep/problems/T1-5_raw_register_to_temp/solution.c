/*
 * T1-5  Raw Register to Temperature
 *
 * A TMP102-style sensor returns its temperature register as two bytes, MSB first.
 * The temperature is a 12-bit two's complement number, LEFT-justified in the 16-bit
 * register (bits 15..4). Bits 3..0 are not part of the reading. 1 LSB = 0.0625 degC.
 *
 *   raw_to_c100(msb, lsb)  -> temperature in 0.01 degC, rounded to nearest,
 *                             ties away from zero (-6.25 -> -6, 12.5 -> 13, -12.5 -> -13).
 *   is_overheat(msb, lsb, thresh_c100) -> true when the converted value >= thresh_c100.
 *
 * Constraints: integer math only (no float), no reliance on implementation-defined
 * right shift of negative numbers. Ignore bits 3..0 of lsb.
 *
 * Reference solution.  Build & run:  ./run.sh T1-5 sol
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

int16_t raw_to_c100(uint8_t msb, uint8_t lsb)
{
    /* 1. assemble the 16-bit register; drop the 4 unused low bits */
    uint16_t reg = (uint16_t)(((uint16_t)msb << 8) | (lsb & 0xF0u));

    /* 2. reinterpret as signed. Value-preserving on every two's complement
     *    compiler (gcc/clang/armcc define it); fully portable alternative:
     *    int32_t v = reg; if (v & 0x8000) v -= 0x10000;                     */
    int16_t s16 = (int16_t)reg;

    /* 3. counts of 0.0625 degC. Low 4 bits are 0, so /16 is EXACT for negatives
     *    too -- no rounding question, and no implementation-defined >> needed. */
    int32_t counts = s16 / 16;                 /* -2048 .. 2047 */

    /* 4. 0.0625 degC = 6.25 c100 = 625/100. Multiply first, then divide.     */
    int32_t num = counts * 625;                /* fits easily in int32 */
    int32_t c100 = (num >= 0) ? (num + 50) / 100 : (num - 50) / 100;  /* half away from 0 */
    return (int16_t)c100;                      /* -12800 .. 12794 */
}

bool is_overheat(uint8_t msb, uint8_t lsb, int16_t thresh_c100)
{
    return raw_to_c100(msb, lsb) >= thresh_c100;
}

int main(void)
{
    /* 1. basic positive values */
    CHECK(raw_to_c100(0x19, 0x00) == 2500);      /*  25.0     degC */
    CHECK(raw_to_c100(0x7F, 0xF0) == 12794);     /* 127.9375  degC (max) */

    /* 2. negative values need sign extension */
    CHECK(raw_to_c100(0xFF, 0xF0) == -6);        /*  -0.0625  degC */
    CHECK(raw_to_c100(0xE7, 0x00) == -2500);     /* -25.0     degC */
    CHECK(raw_to_c100(0x80, 0x00) == -12800);    /* -128.0    degC (min) */

    /* 3. zero */
    CHECK(raw_to_c100(0x00, 0x00) == 0);

    /* 4. exact .5 ties round away from zero, symmetric for negatives */
    CHECK(raw_to_c100(0x00, 0x20) == 13);        /*  0.125 degC = 12.5 c100 */
    CHECK(raw_to_c100(0xFF, 0xE0) == -13);       /* -0.125 degC */

    /* 5. garbage in the 4 unused low bits must be ignored */
    CHECK(raw_to_c100(0x19, 0x0F) == 2500);
    CHECK(raw_to_c100(0xFF, 0xFF) == -6);

    /* 6. overheat decision at and just below the threshold */
    CHECK(is_overheat(0x19, 0x00, 2500) == true);    /* 25.00 >= 25.00 */
    CHECK(is_overheat(0x18, 0xF0, 2500) == false);   /* 24.9375 -> 2494 */
    CHECK(is_overheat(0xE7, 0x00, -3000) == true);   /* -25.00 >= -30.00 */

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
