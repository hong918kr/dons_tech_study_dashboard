/* ARM-A09 demo_misalign_mac.S (Mach-O) — 8 B LDR 의 @PAGEOFF 대상이 4 B 정렬뿐. 새 ld 는 조용히 링크한다(!). */
    .text
    .globl  _f2
    .p2align 2
_f2:
    adrp    x8, _odd@PAGE
    ldr     x0, [x8, _odd@PAGEOFF]          // PAGEOFF12: (addr & 0xfff) / 8 이어야 하는데 나머지 4
    ret
    .data
    .p2align 3
    .byte   1, 2, 3, 4
    .globl  _odd
_odd:   .quad 5
