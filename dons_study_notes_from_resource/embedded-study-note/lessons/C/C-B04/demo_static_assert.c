/* C-B04 데모: 잘못된 마스크/값/레지스터 맵을 "컴파일 에러"로 잡는다.
 *   cc -std=c11 -c demo_static_assert.c                 # 통과 (에러 없음)
 *   cc -std=c11 -c -DBAD_MASK    demo_static_assert.c   # 구멍 난 마스크
 *   cc -std=c11 -c -DBAD_VAL     demo_static_assert.c   # 필드에 안 들어가는 값
 *   cc -std=c11 -c -DBAD_OVERLAP demo_static_assert.c   # 필드끼리 겹치는 레지스터 맵
 */
#include <stdint.h>

#define FIELD_LSB(mask)       ((uint32_t)(mask) & (~(uint32_t)(mask) + 1u))
#define FIELD_PREP(mask, val) (((uint32_t)(val) * FIELD_LSB(mask)) & (uint32_t)(mask))
#define FIELD_MAX(mask)       ((uint32_t)(mask) / FIELD_LSB(mask))
#define FIELD_MASK_OK(mask)   ((uint32_t)(mask) != 0u && \
                               (((uint32_t)(mask) + FIELD_LSB(mask)) & (uint32_t)(mask)) == 0u)
#define FIELD_FIT(mask, val)  ((uint32_t)(val) <= FIELD_MAX(mask))
#define FIELD_PREP_CONST(mask, val) ((uint32_t)(FIELD_PREP(mask, val) + 0u * sizeof(struct { \
    _Static_assert(FIELD_MASK_OK(mask), "FIELD_PREP_CONST: mask is zero or not contiguous");  \
    _Static_assert(!FIELD_MASK_OK(mask) || FIELD_FIT(mask, val),                              \
                   "FIELD_PREP_CONST: value too large for field");                            \
    char dummy; })))

#define CTRL_MODE_Msk   0x0000000Eu     /* bits 3:1 */
#define CTRL_PRESC_Msk  0x000000F0u     /* bits 7:4 */

uint32_t ok_val = FIELD_PREP_CONST(CTRL_PRESC_Msk, 15u);           /* 0xF0 — 통과 */

#ifdef BAD_MASK
uint32_t bad_mask = FIELD_PREP_CONST(0x000000D0u, 1u);             /* 1101_0000: 구멍 */
#endif
#ifdef BAD_VAL
uint32_t bad_val = FIELD_PREP_CONST(CTRL_MODE_Msk, 8u);            /* 3비트에 8 */
#endif
#ifdef BAD_OVERLAP
#define CTRL_IRQEN_Msk  0x00000010u     /* 실수: bit 8 이어야 하는데 bit 4 (PRESC와 겹침) */
_Static_assert((uint64_t)CTRL_MODE_Msk + CTRL_PRESC_Msk + CTRL_IRQEN_Msk ==
               (CTRL_MODE_Msk | CTRL_PRESC_Msk | CTRL_IRQEN_Msk), "CTRL fields overlap");
#endif
