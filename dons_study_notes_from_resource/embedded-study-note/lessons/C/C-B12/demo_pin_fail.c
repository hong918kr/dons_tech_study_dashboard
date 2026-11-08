/* C-B12 데모: reserved 를 빠뜨린 레지스터 구조체는 _Static_assert 가 컴파일 단계에서 잡는다
 *   cc -std=c11 -fsyntax-only demo_pin_fail.c      ← 일부러 실패하는 파일
 */
#include <stddef.h>
#include <stdint.h>

typedef struct {
    volatile uint32_t CR;        /* 0x000 */
    volatile uint32_t SR;        /* 0x004 */
    volatile uint16_t DR;        /* 0x008 */
    volatile uint32_t BRR;       /* 0x00C — 암묵적 패딩 덕에 우연히 맞음 */
    uint32_t RESERVED1[3];       /* 0x010–0x01F 인데 3워드만 적음 (실수) */
    volatile uint32_t CH0_CFG;   /* 데이터시트: 0x020 */
} SPIX_Partial;

_Static_assert(offsetof(SPIX_Partial, BRR) == 0x00C, "BRR must be at 0x00C");
_Static_assert(offsetof(SPIX_Partial, CH0_CFG) == 0x020, "CH0_CFG must be at 0x020");
