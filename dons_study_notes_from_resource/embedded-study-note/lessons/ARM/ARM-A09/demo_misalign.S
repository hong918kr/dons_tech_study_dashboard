/* ARM-A09 demo_misalign.S (ELF) — 8 B LDR 의 :lo12: 대상이 4 B 정렬뿐인 경우. ld.bfd / ld.lld 는 에러. */
    .text
    .globl  _start
_start:
    adrp    x8, odd
    ldr     x0, [x8, :lo12:odd]             // LDST64_ABS_LO12_NC: 오프셋을 8 로 나눠 넣어야 하는데 나머지 4
    b       .
    .data
    .p2align 3
    .byte   1, 2, 3, 4
    .globl  odd
odd:    .quad 5
