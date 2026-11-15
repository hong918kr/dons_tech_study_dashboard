/* ENV: qemu */
/* ARM-M08 demo_thumbfunc.S — 어떤 지시어가 있어야 링커가 bit0 = 1 을 넣는가 (실행 안 함, 링크해서 심볼·워드만 본다)
 * run_qemu.sh thumbfunc → readelf -s 와 .data 의 실제 워드
 */
    .syntax unified
    .cpu cortex-m3
    .thumb
    .text
    .global lbl_plain, lbl_type, lbl_thumbfunc, lbl_both

    .p2align 2
lbl_plain:                          // 지시어 없음
    bx lr

    .p2align 2
    .type lbl_type, %function       // .type 만
lbl_type:
    bx lr

    .p2align 2
    .thumb_func                     // .thumb_func 만
lbl_thumbfunc:
    bx lr

    .p2align 2
    .type lbl_both, %function       // 둘 다 (이 레슨의 규칙)
    .thumb_func
lbl_both:
    bx lr

    .thumb_set lbl_tset, lbl_plain  // .thumb_set 별칭 (CMSIS startup 의 weak 기본 핸들러 방식)
    .set lbl_set, lbl_plain         // 그냥 .set 별칭

    .data
    .p2align 2
    .global tbl
tbl:                                // 벡터 테이블처럼 .word 로 적은 6 개
    .word lbl_plain, lbl_type, lbl_thumbfunc, lbl_both, lbl_tset, lbl_set
