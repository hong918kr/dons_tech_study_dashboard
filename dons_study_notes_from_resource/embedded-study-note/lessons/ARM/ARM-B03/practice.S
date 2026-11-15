/* ARM-B03 practice.S — 추가 연습 4: MOVZ/MOVK 보다 짧은 길 (ORR 논리 즉시값 별칭)
 * `mov x0, #imm` 은 imm 이 MOVZ/MOVN 한 개로 안 되면 어셈블러가 ORR x0, xzr, #imm 으로 바꿔 준다
 * (논리 즉시값일 때만). 하네스가 첫 워드가 정말 ORR(Rn=xzr) 인지 기계어로 확인한다.
 */
#ifdef __APPLE__
#  define SYM(n)  _##n
#  define FUNC(n)
#  define ENDF(n)
#else
#  define SYM(n)  n
#  define FUNC(n) .type n, %function
#  define ENDF(n) .size n, .-n
#endif

    .text

/* uint64_t k_5555(void) — 0x5555555555555555: MOVZ/MOVK 로는 4개, ORR 로는 1개 */
    .globl SYM(k_5555)
    .p2align 2
    FUNC(k_5555)
SYM(k_5555):
    mov  x0, #0x5555555555555555      // = orr x0, xzr, #0x5555555555555555
    ret
    ENDF(k_5555)

/* uint64_t k_00ff_1234(void) — 0x00ff00ff00ff1234: ORR 패턴 + hw0 만 MOVK 로 교체 = 2개 */
    .globl SYM(k_00ff_1234)
    .p2align 2
    FUNC(k_00ff_1234)
SYM(k_00ff_1234):
    orr  x0, xzr, #0x00ff00ff00ff00ff // x0 = 0x00ff_00ff_00ff_00ff (논리 즉시값, 주기 16)
    movk x0, #0x1234                  // hw0 교체 -> 0x00ff_00ff_00ff_1234
    ret
    ENDF(k_00ff_1234)
