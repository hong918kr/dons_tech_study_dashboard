/* ENV: qemu */
/* ARM-A07 M4 레인 — Thumb-2 DSP 명령으로 쓴 Q15 FIR 과 Q 플래그 도우미 (Cortex-M4 = ARMv7E-M)
 * 빌드: run_qemu.sh m4 (arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb). 맥 확인: clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -c m4_fir.S
 * AAPCS: r0–r3 인자, 5번째 인자는 [sp], r0 반환, r4–r11 callee-saved.
 * 명세는 맥 레인과 같다: acc = L_mac 누적 (탭 순서), y = (QADD(acc, 0x8000)) >> 16.
 */
    .syntax unified
    .cpu cortex-m4
    .thumb
    .text

// int32_t m4_lmult(int32_t a, int32_t b)   L_mult = QADD(p, p),  p = SMULBB(a, b)
    .global m4_lmult
    .type   m4_lmult, %function
    .thumb_func
m4_lmult:
    smulbb  r0, r0, r1                  // p = a[15:0]·b[15:0] (부호 있는 16×16→32, 넘치지 않는다)
    qadd    r0, r0, r0                  // sat32(p + p) — -32768² 일 때만 포화, 그때 Q = 1
    bx      lr
    .size   m4_lmult, .-m4_lmult

// int32_t m4_mac(int32_t acc, int32_t a, int32_t b)   L_mac = QDADD(acc, SMULBB(a, b))
    .global m4_mac
    .type   m4_mac, %function
    .thumb_func
m4_mac:
    smulbb  r1, r1, r2
    qdadd   r0, r0, r1                  // sat32(acc + sat32(2p)) — 포화 두 번, 어느 쪽이든 Q = 1
    bx      lr
    .size   m4_mac, .-m4_mac

// int32_t m4_round(int32_t L)   round = QADD(L, 0x8000) >> 16 (산술)
    .global m4_round
    .type   m4_round, %function
    .thumb_func
m4_round:
    mov     r1, #0x8000
    qadd    r0, r0, r1
    asrs    r0, r0, #16
    bx      lr
    .size   m4_round, .-m4_round

// int32_t m4_ssat16_asr15(int32_t v)   SSAT 의 시프트 피연산자: sat16(v >> 15) 를 한 명령으로
    .global m4_ssat16_asr15
    .type   m4_ssat16_asr15, %function
    .thumb_func
m4_ssat16_asr15:
    ssat    r0, #16, r0, asr #15        // 잘리면 Q = 1
    bx      lr
    .size   m4_ssat16_asr15, .-m4_ssat16_asr15

// int32_t m4_smlabb(int32_t a, int32_t b, int32_t acc)   acc + a[15:0]·b[15:0] — 감싸고(wrap), 넘치면 Q = 1
//   (ACLE __smlabb 는 gcc 가 상수 피연산자일 때 평범한 ADD 로 접어 버린다 → Q 를 보려면 asm 으로)
    .global m4_smlabb
    .type   m4_smlabb, %function
    .thumb_func
m4_smlabb:
    smlabb  r0, r0, r1, r2
    bx      lr
    .size   m4_smlabb, .-m4_smlabb

// void fir_m4_q31(const int16_t *x, size_t n, const int16_t *h, size_t T, int16_t *y)   (T >= 1)
//   한 탭 = LDRSH ×2 + SMULBB + QDADD. 명세와 같은 순서 (k = 0..T-1) → 비트 단위로 같다
    .global fir_m4_q31
    .type   fir_m4_q31, %function
    .thumb_func
fir_m4_q31:
    push    {r4-r11, lr}                // 9 워드 = 36 바이트 → 5번째 인자 y 는 [sp, #36]
    ldr     r12, [sp, #36]              // y
    cbz     r1, 9f                      // n == 0
    sub     r4, r3, #1
    add     r4, r0, r4, lsl #1          // r4 = &x[i+T-1] (i = 0)
    mov     r11, #0x8000                // 반올림 상수
1:  movs    r5, #0                      // acc (Q31)
    mov     r6, r4                      // px = &x[i+T-1-k]
    mov     r7, r2                      // ph = &h[k]
    mov     r8, r3                      // k 카운터
2:  ldrsh   r9, [r6], #-2               // x[i+T-1-k], 한 샘플 과거로
    ldrsh   r10, [r7], #2               // h[k]
    smulbb  r9, r9, r10                 // p = x·h
    qdadd   r5, r5, r9                  // acc = sat(acc + sat(2p))
    subs    r8, r8, #1
    bne     2b
    qadd    r5, r5, r11                 // acc + 0x8000 (포화)
    asrs    r5, r5, #16                 // Q31 → Q15
    strh    r5, [r12], #2
    adds    r4, r4, #2                  // i++
    subs    r1, r1, #1
    bne     1b
9:  pop     {r4-r11, pc}
    .size   fir_m4_q31, .-fir_m4_q31

// uint32_t q_get(void)   APSR.Q (bit 27) — 포화 명령이 세우고, 지울 때까지 남는다 (sticky)
    .global q_get
    .type   q_get, %function
    .thumb_func
q_get:
    mrs     r0, apsr
    ubfx    r0, r0, #27, #1
    bx      lr
    .size   q_get, .-q_get

// void q_clear(void)
    .global q_clear
    .type   q_clear, %function
    .thumb_func
q_clear:
    mrs     r0, apsr
    bic     r0, r0, #(1 << 27)
    msr     APSR_nzcvq, r0              // M-profile: N Z C V Q 를 쓴다
    bx      lr
    .size   q_clear, .-q_clear
