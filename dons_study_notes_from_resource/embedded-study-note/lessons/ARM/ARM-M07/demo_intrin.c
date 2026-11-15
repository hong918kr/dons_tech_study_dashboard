/* ARM-M07 demo_intrin: the same nibble-mask idea with <arm_neon.h> intrinsics
 *   clang -std=c11 -O2 -S -o - demo_intrin.c | grep -v '^\s*[.;]'      (look for cmeq / shrn / rbit / clz)
 */
#include <arm_neon.h>
#include <stdint.h>
/* index of the first byte == c in one 16-byte chunk, or 16 if none */
int first_match16(const uint8_t *p, uint8_t c) {
    uint8x16_t eq   = vceqq_u8(vld1q_u8(p), vdupq_n_u8(c));               /* CMEQ  */
    uint8x8_t  nib  = vshrn_n_u16(vreinterpretq_u16_u8(eq), 4);           /* SHRN #4 */
    uint64_t   mask = vget_lane_u64(vreinterpret_u64_u8(nib), 0);         /* FMOV x, d */
    return mask ? __builtin_ctzll(mask) >> 2 : 16;                        /* RBIT + CLZ */
}
/* byte sum of 16 bytes with the widening pairwise adds */
uint32_t sum16(const uint8_t *p) {
    uint16x8_t s16 = vpaddlq_u8(vld1q_u8(p));                             /* UADDLP .8h */
    return vaddlvq_u16(s16);                                              /* UADDLV s */
}
