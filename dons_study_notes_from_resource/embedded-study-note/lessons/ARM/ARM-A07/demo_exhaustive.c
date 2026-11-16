/* ARM-A07 demo_exhaustive — 2^32 개 (a, b) 쌍 전부: NEON SQDMULH / SQRDMULH / SQDMULL 이 C 명세와 같은가, 몇 쌍이 포화하나
 *   cc -std=c11 -O2 demo_exhaustive.c -o /tmp/ARM-A07_ex && time /tmp/ARM-A07_ex
 * 테스트(< 2 s 예산)에는 "모든 a × 경계 b + 2^20 무작위"만 넣고, 전수 검사는 여기서 한 번 돌려 결과를 붙여 둔다.
 */
#include <arm_neon.h>
#include <stdint.h>
#include <stdio.h>

static int16_t sat16(int64_t v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }
static int64_t fdiv(int64_t v, int64_t d) { return v >= 0 ? v / d : -((-v + d - 1) / d); }

int main(void) {
    uint64_t bad_mul = 0, bad_mulr = 0, bad_lmult = 0, sat_mul = 0, sat_mulr = 0, sat_lmult = 0;
    int16_t bv[8];
    for (int32_t a = -32768; a <= 32767; a++) {
        int16x8_t av = vdupq_n_s16((int16_t)a);
        for (int32_t b0 = -32768; b0 <= 32767; b0 += 8) {
            for (int j = 0; j < 8; j++) bv[j] = (int16_t)(b0 + j);
            int16x8_t b = vld1q_s16(bv);
            int16_t gm[8], gr[8]; int32_t gl[8];
            vst1q_s16(gm, vqdmulhq_s16(av, b));                                  /* SQDMULH  */
            vst1q_s16(gr, vqrdmulhq_s16(av, b));                                 /* SQRDMULH */
            vst1q_s32(gl, vqdmull_s16(vget_low_s16(av), vget_low_s16(b)));       /* SQDMULL  */
            vst1q_s32(gl + 4, vqdmull_high_s16(av, b));                          /* SQDMULL2 */
            for (int j = 0; j < 8; j++) {
                int64_t p2 = 2 * (int64_t)a * bv[j];
                int16_t em = sat16(fdiv(p2, 65536)), er = sat16(fdiv(p2 + 0x8000, 65536));
                int64_t el = p2 > INT32_MAX ? INT32_MAX : p2;
                bad_mul += gm[j] != em; bad_mulr += gr[j] != er; bad_lmult += gl[j] != el;
                sat_mul += fdiv(p2, 65536) > 32767; sat_mulr += fdiv(p2 + 0x8000, 65536) > 32767; sat_lmult += p2 > INT32_MAX;
            }
        }
    }
    printf("pairs checked: %llu\n", 65536ull * 65536ull);
    printf("SQDMULH  mismatches %llu, saturating pairs %llu\n", (unsigned long long)bad_mul, (unsigned long long)sat_mul);
    printf("SQRDMULH mismatches %llu, saturating pairs %llu\n", (unsigned long long)bad_mulr, (unsigned long long)sat_mulr);
    printf("SQDMULL  mismatches %llu, saturating pairs %llu\n", (unsigned long long)bad_lmult, (unsigned long long)sat_lmult);
    return (bad_mul | bad_mulr | bad_lmult) ? 1 : 0;
}
