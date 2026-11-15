/* ARM-B04 demo_modes.S — 모드별로 "읽은 값" 과 "명령 뒤의 base 레지스터" 를 돌려준다 (맥 전용 심볼) */
    .text
// uint64_t m_off(const uint64_t *p, const uint64_t **after)       ldr x, [x0, #8]
    .globl _m_off
    .p2align 2
_m_off:
    ldr     x2, [x0, #8]            // 주소 x0+8, x0 안 바뀜
    str     x0, [x1]                // *after = x0
    mov     x0, x2
    ret
// uint64_t m_pre(const uint64_t *p, const uint64_t **after)       ldr x, [x0, #8]!
    .globl _m_pre
    .p2align 2
_m_pre:
    ldr     x2, [x0, #8]!           // x0 += 8 먼저, 새 x0 에서 로드
    str     x0, [x1]
    mov     x0, x2
    ret
// uint64_t m_post(const uint64_t *p, const uint64_t **after)      ldr x, [x0], #8
    .globl _m_post
    .p2align 2
_m_post:
    ldr     x2, [x0], #8            // 옛 x0 에서 로드, 그 다음 x0 += 8
    str     x0, [x1]
    mov     x0, x2
    ret
// uint64_t m_reg(const uint64_t *p, const uint64_t **after, uint64_t i)   ldr x, [x0, x2, lsl #3]
    .globl _m_reg
    .p2align 2
_m_reg:
    ldr     x3, [x0, x2, lsl #3]    // 주소 x0 + i*8
    str     x0, [x1]
    mov     x0, x3
    ret
// uint64_t m_sxtw(const uint64_t *p, const uint64_t **after, int32_t k)   ldr x, [x0, w2, sxtw #3]
    .globl _m_sxtw
    .p2align 2
_m_sxtw:
    ldr     x3, [x0, w2, sxtw #3]   // 주소 x0 + sext(k)*8, k 음수 가능
    str     x0, [x1]
    mov     x0, x3
    ret
