/* ARM-B03 starter.S — 64비트 상수를 MOVZ/MOVN + MOVK 로 만드는 함수들 (+ Part C 레지스터 패치)
 * 규칙: movz_movk_plan() 과 같은 순서로 쓴다 = 첫 명령은 MOVZ 또는 MOVN(가장 낮은 hw),
 *       그 다음 MOVK 를 hw 오름차순으로. 하네스가 이 함수들의 기계어를 읽어 plan 과 비교한다.
 * 맥(Mach-O): 심볼 앞에 _ 가 붙는다. 리눅스(ELF): .type/.size 가 필요. SYM/FUNC/ENDF 매크로로 둘 다 지원.
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

/* uint64_t k_deadbeef(void) — 0x00000000DEADBEEF : 0 인 halfword 2개 -> MOVZ 경로, 2개 명령 */
    .globl SYM(k_deadbeef)
    .p2align 2
    FUNC(k_deadbeef)
SYM(k_deadbeef):
    mov  x0, #0                       // TODO: MOVZ + MOVK 로 0xDEADBEEF
    ret
    ENDF(k_deadbeef)

/* uint64_t k_cafef00d_deadbeef(void) — 0 도 0xffff 도 없는 최악의 경우: 4개 명령 */
    .globl SYM(k_cafef00d_deadbeef)
    .p2align 2
    FUNC(k_cafef00d_deadbeef)
SYM(k_cafef00d_deadbeef):
    mov  x0, #0                       // TODO: 0xCAFEF00DDEADBEEF (4개)
    ret
    ENDF(k_cafef00d_deadbeef)

/* uint64_t k_ffffffff_12345678(void) — 0xffff halfword 2개 -> MOVN 경로, 2개 명령 */
    .globl SYM(k_ffffffff_12345678)
    .p2align 2
    FUNC(k_ffffffff_12345678)
SYM(k_ffffffff_12345678):
    mov  x0, #0                       // TODO: MOVN + MOVK
    ret
    ENDF(k_ffffffff_12345678)

/* int64_t k_minus2(void) — -2 = 0xffff_ffff_ffff_fffe = ~1 : MOVN 1개 */
    .globl SYM(k_minus2)
    .p2align 2
    FUNC(k_minus2)
SYM(k_minus2):
    mov  x0, #0                       // TODO: -2 를 명령 1개로
    ret
    ENDF(k_minus2)

/* uint64_t k_qemu_uart(void) — QEMU virt PL011 UART0 베이스 0x0900_0000 : MOVZ 1개 (hw=1) */
    .globl SYM(k_qemu_uart)
    .p2align 2
    FUNC(k_qemu_uart)
SYM(k_qemu_uart):
    mov  x0, #0                       // TODO: 0x09000000 을 명령 1개로
    ret
    ENDF(k_qemu_uart)

/* uint64_t k_tie(void) — 0xffff_0000_0000_ffff : 0 2개 = 0xffff 2개 (동점) -> MOVZ 경로 */
    .globl SYM(k_tie)
    .p2align 2
    FUNC(k_tie)
SYM(k_tie):
    mov  x0, #0                       // TODO: 0xFFFF00000000FFFF (동점 -> MOVZ)
    ret
    ENDF(k_tie)

/* Part C: uint32_t pllcfgr_set_plln168(uint32_t r)
 *   STM32F4 RCC_PLLCFGR 의 PLLN 필드 [14:6] 을 168 로 바꾸고 나머지 비트는 보존.
 *   clear 마스크 ~0x7fc0 = 0xffff803f 는 논리 즉시값(감싸는 1의 연속) -> AND 1개.
 *   168 << 6 = 0x2a00 은 1 이 연속이 아니라 논리 즉시값 불가 -> MOVZ + ORR(레지스터).
 */
    .globl SYM(pllcfgr_set_plln168)
    .p2align 2
    FUNC(pllcfgr_set_plln168)
SYM(pllcfgr_set_plln168):
    // TODO: and(논리 즉시값) 로 PLLN clear -> movz w8 -> orr
    ret
    ENDF(pllcfgr_set_plln168)
