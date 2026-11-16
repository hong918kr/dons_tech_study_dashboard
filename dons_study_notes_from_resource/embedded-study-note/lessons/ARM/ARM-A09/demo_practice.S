/* ARM-A09 demo_practice.S (ELF) — 추가 연습의 정답 워드를 실제 ld.bfd 로 만든다 (demo_practice.sh) */
    .section .text.near, "ax"
    .globl  _start
_start:
    b.ne    far1                            // R_AARCH64_CONDBR19  (±1 MB)
    cbz     x0, far1                        // R_AARCH64_CONDBR19
    tbz     x1, #3, mid1                    // R_AARCH64_TSTBR14   (±32 KB)
    tbnz    w1, #31, back1                  // R_AARCH64_TSTBR14, 뒤로
    adr     x4, mid1                        // R_AARCH64_ADR_PREL_LO21 (±1 MB, 바이트 단위)
    movz    x2, #:abs_g3:far_data           // R_AARCH64_MOVW_UABS_G3
    movk    x2, #:abs_g2_nc:far_data        // R_AARCH64_MOVW_UABS_G2_NC
    movk    x2, #:abs_g1_nc:far_data        // R_AARCH64_MOVW_UABS_G1_NC
    movk    x2, #:abs_g0_nc:far_data        // R_AARCH64_MOVW_UABS_G0_NC
    b       .
    .section .text.mid, "ax"
mid1:
    ret
    .section .text.far, "ax"
far1:
    ret
    .section .text.back, "ax"
back1:
    ret
    .section .data.far, "aw"
    .globl  far_data
far_data:
    .quad   1
