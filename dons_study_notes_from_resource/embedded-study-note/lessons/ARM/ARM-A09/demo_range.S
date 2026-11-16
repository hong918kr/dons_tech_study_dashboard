/* ARM-A09 demo_range.S — ADRP ±4 GB / BL ±128 MB 경계를 링커 스크립트로 만든다 (demo_elf.sh 가 사용) */
    .section .text.near, "ax"
    .globl  _start
_start:
    adrp    x0, far_data                    // ADR_PREL_PG_HI21: 페이지 차이가 21비트(부호)에 들어가야 한다
    add     x0, x0, :lo12:far_data
    bl      far_func                        // CALL26: (S - P) 가 ±128 MB 안이어야 한다
    b       .
    .section .text.far, "ax"
    .globl  far_func
far_func:
    ret
    .section .data.far, "aw"
    .globl  far_data
far_data:
    .quad   1
