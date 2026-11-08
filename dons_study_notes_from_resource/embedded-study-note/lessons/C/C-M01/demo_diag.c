/* C-M01 demo_diag.c — 컴파일러가 잡아 주는 레지스터 정의 실수들. 실행 파일이 아니다(main 없음).
 * 하나씩 켜서 진단 메시지를 본다:
 *   clang -std=c11 -fsyntax-only -DDIAG=1 demo_diag.c     # 읽기 전용 레지스터에 쓰기
 *   clang -std=c11 -fsyntax-only -DDIAG=2 demo_diag.c     # reserved 를 빠뜨린 정의
 *   clang -std=c11 -fsyntax-only -DDIAG=3 demo_diag.c     # 64비트 호스트: 주소를 uint32_t 에 담기
 *   clang --target=thumbv7em-none-eabi -std=c11 -fsyntax-only -DDIAG=3 demo_diag.c   # 타깃에선 조용
 *   clang -std=c11 -fsyntax-only -DDIAG=4 demo_diag.c     # const 멤버가 있는 구조체 통째 대입 */
#include <stddef.h>
#include <stdint.h>

typedef struct {
    volatile uint32_t SR, DR, BRR, CR1;
#if DIAG != 2
    uint32_t RESERVED0[4];                    /* DIAG=2 에서는 빠뜨린다 */
#endif
    volatile const uint32_t ID;               /* CMSIS __I */
} VUART_TypeDef;
_Static_assert(offsetof(VUART_TypeDef, ID) == 0x20, "VUART ID must be at 0x20 (datasheet)");

void diag(VUART_TypeDef *u, VUART_TypeDef *v) {
    (void)u; (void)v;
#if DIAG == 1
    u->ID = 0;                                /* 읽기 전용 */
#elif DIAG == 3
    uint32_t base = 0x40011000u;                              /* 주소를 uint32_t 변수에 보관 */
    volatile uint32_t *dr = (volatile uint32_t *)(base + 4u); /* 32비트 정수 → 포인터 */
    uint32_t back = (uint32_t)&u->DR;                         /* 포인터 → 32비트 정수 */
    (void)dr; (void)back;
#elif DIAG == 4
    *u = *v;                                  /* 레지스터 블록 통째 복사 */
#endif
}
