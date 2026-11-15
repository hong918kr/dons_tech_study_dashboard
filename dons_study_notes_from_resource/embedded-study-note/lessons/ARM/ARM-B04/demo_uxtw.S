/* ARM-B04 demo_uxtw.S — sxtw 를 uxtw 로 잘못 쓴 gather (맥 전용 심볼) */
    .text
// int64_t gather_one_uxtw(const int32_t *base, int32_t k)    BUG: k 를 부호 없는 수로 확장
    .globl _gather_one_uxtw
    .p2align 2
_gather_one_uxtw:
    ldrsw   x0, [x0, w1, uxtw #2]   // 주소 = base + zext(k)*4  -> k = -1 이면 base + 0x3fffffffc
    ret
// int64_t gather_one_sxtw(const int32_t *base, int32_t k)    정답
    .globl _gather_one_sxtw
    .p2align 2
_gather_one_sxtw:
    ldrsw   x0, [x0, w1, sxtw #2]   // 주소 = base + sext(k)*4  -> k = -1 이면 base - 4
    ret
