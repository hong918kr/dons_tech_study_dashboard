/* ARM-A04 추가 연습용 골든 워드 — clang 이 어셈블한 진짜 인코딩 (실행하지 않고 읽기만) */
#ifdef __APPLE__
#define SYM(x) _##x
#else
#define SYM(x) x
#endif
    .text
    .p2align 2
    .globl SYM(prac_ref)
SYM(prac_ref):
    b.eq . + 0x20                  // 0  연습 1: B.cond  cond=0000
    b.ne . - 4                     // 1  cond=0001
    b.hi . + 0xffffc               // 2  cond=1000, 최대 +1 MB − 4
    b.lt . - 0x100000              // 3  cond=1011, 최소 −1 MB
    cbnz x5, . + 0x40              // 4  CBNZ
    cbnz x30, . - 0x100000         // 5
    br   x16                       // 6  연습 2: veneer 의 끝
    brk  #0                        // 7  연습 4: CMODX 안전 집합
    brk  #0xf000
    svc  #0x80
    hvc  #0
    smc  #0
    nop
    isb
    b    . + 8                     // 14
    bl   . - 8                     // 15
    add  x0, x0, #1                // 16 안전 집합 밖
    movz x0, #1                    // 17
    ret                            // 18
    dsb  ish                       // 19 (ISB 와 같은 그룹이지만 다른 명령)
    .globl SYM(prac_ref_end)
SYM(prac_ref_end):
