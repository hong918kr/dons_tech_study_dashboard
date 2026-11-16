/* ARM-A09 demo_loh.S (Mach-O) — 링커 최적화 힌트(.loh)를 붙여도 ld 가 ADRP+ADD 를 ADR 로 바꾸는지 관찰 */
    .text
    .globl  _get_bias_loh
    .p2align 2
_get_bias_loh:
Lloh0:
    adrp    x0, _local_bias@PAGE
Lloh1:
    add     x0, x0, _local_bias@PAGEOFF
    ret
    .loh AdrpAdd Lloh0, Lloh1                // "이 두 명령은 한 쌍" 이라는 힌트 → LC_LINKER_OPTIMIZATION_HINT
    .data
    .p2align 3
    .globl  _local_bias
_local_bias: .quad 100
