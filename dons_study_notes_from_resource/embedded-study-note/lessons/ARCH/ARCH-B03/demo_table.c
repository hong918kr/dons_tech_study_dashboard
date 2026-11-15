/* ARCH-B03 demo: 면접에 나오는 대표 값들의 비트, 정확한 10진값, 하드웨어 FP16 변환 결과 표.
 * 빌드: cc -std=c11 -O2 demo_table.c -o /tmp/esn_ARCH-B03/tb && /tmp/esn_ARCH-B03/tb
 */
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t f2u(float f)    { uint32_t u; memcpy(&u, &f, 4); return u; }
static float    u2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static uint16_t hw_f16(float f) { _Float16 h = (_Float16)f; uint16_t u; memcpy(&u, &h, 2); return u; }
static float    hw_back(uint16_t u) { _Float16 h; memcpy(&h, &u, 2); return (float)h; }

static void row(const char *name, float x) {
    uint32_t b = f2u(x);
    uint16_t h = hw_f16(x);
    printf("%-22s %08X  s=%u e=%3u f=%06X  %-34.30g FP16 %04X = %.17g\n", name, b, b >> 31, (b >> 23) & 0xFF,
           b & 0x7FFFFF, (double)x, h, (double)hw_back(h));
}

int main(void) {
    row("0.1f", 0.1f);
    row("1/3", 1.0f / 3.0f);
    row("FLT_TRUE_MIN (2^-149)", FLT_TRUE_MIN);
    row("FLT_MIN (2^-126)", FLT_MIN);
    row("FP16 min sub 2^-24", 0x1p-24f);
    row("2^-25 (tie)", 0x1p-25f);
    row("1.5*2^-24 (tie)", 0x1.8p-24f);
    row("FP16 min normal 2^-14", 0x1p-14f);
    row("65504 (FP16 max)", 65504.0f);
    row("65519.996", u2f(0x477FEFFFu));
    row("65520 (tie)", 65520.0f);
    row("1+2^-11 (tie)", u2f(0x3F801000u));
    row("1+3*2^-11 (tie)", u2f(0x3F803000u));
    row("2049", 2049.0f);
    row("qNaN 7FC00000", u2f(0x7FC00000u));
    row("sNaN 7FA00000", u2f(0x7FA00000u));
    return 0;
}
