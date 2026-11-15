/* ARM-B05 demo: cmp 직후의 NZCV 를 읽어 오는 도우미 (mrs nzcv 는 EL0 에서 읽을 수 있다)
 * uint32_t nzcv_cmp64(int64_t a, int64_t b);   uint32_t nzcv_cmp32(uint32_t a, uint32_t b);
 * 반환값 bit3..0 = N Z C V
 */
#ifdef __APPLE__
#define FUNC(n) .globl _##n %% .p2align 2 %% _##n:
#define ENDF(n)
#else
#define FUNC(n) .global n ; .type n,%function ; .p2align 2 ; n:
#define ENDF(n) .size n, .-n
#endif
    .text
FUNC(nzcv_cmp64)
    cmp     x0, x1              // 플래그 ← a - b (64비트)
    mrs     x0, nzcv            // NZCV 는 bit31..28
    lsr     x0, x0, #28         // → bit3..0
    ret
ENDF(nzcv_cmp64)
FUNC(nzcv_cmp32)
    cmp     w0, w1              // 플래그 ← a - b (32비트)
    mrs     x0, nzcv
    lsr     x0, x0, #28
    ret
ENDF(nzcv_cmp32)
