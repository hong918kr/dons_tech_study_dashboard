/* ARM-B06 — 일부러 틀린 구현 3개 (starter/solution 공통 링크). 하네스가 각 버그를 잡아내는지 본다.
 * 같은 FUNC/ENDF 매크로 (macOS Mach-O / Linux ELF 겸용).
 */
#ifdef __APPLE__
    .macro FUNC name
    .text
    .globl _\name
    .p2align 2
_\name:
    .endm
    .macro ENDF name
    .endm
#else
    .macro FUNC name
    .text
    .global \name
    .type \name, %function
    .p2align 2
\name:
    .endm
    .macro ENDF name
    .size \name, . - \name
    .endm
#endif

// 버그 1: mask = (1 << width) - 1.  LSLV 는 시프트 양을 mod 64 로 쓰므로 width=64 → 1<<0 = 1 → mask 0
FUNC field_get_var_naive
    lsr     x0, x0, x1
    mov     x3, #1
    lsl     x3, x3, x2              // width=64 → x3 = 1 (C 라면 UB, ISA 는 "mod 64")
    sub     x3, x3, #1              // → 0 : 필드 전체가 사라진다
    and     x0, x0, x3
    ret
ENDF field_get_var_naive

// 버그 2: "니블 역순 = 비트 역순" 착각. RBIT 는 니블 순서와 함께 니블 안의 비트 순서도 뒤집는다
FUNC nibrev_rbit
    rbit    w0, w0
    ret
ENDF nibrev_rbit

// 버그 3: 흔한 C 관용구 __builtin_clz(x) ^ 31 을 clang -O2 가 그대로 옮긴 모양 (clz + eor #0x1f, 실제 출력).
//        clz ∈ 0..31 에선 31-clz 와 같지만 x=0 → clz=32 → 32^31 = 63
FUNC hsb32_eor
    clz     w0, w0
    eor     w0, w0, #31
    ret
ENDF hsb32_eor
