/* ENV: cross — ARM-B12 .n/.w 폭 관찰용. 실행하지 않는다: clang --target=thumbv7em-none-eabi -c demo_width.S */
    .syntax unified
    .thumb
    .text
    .globl width_demo
width_demo:
    adds    r0, r0, #1      // 16-bit 가능 (Rd,Rn low, imm3)
    adds.w  r0, r0, #1      // 같은 연산, 32-bit 강제
    add     r0, r0, #1      // IT 밖: 16-bit ADD(imm) 는 플래그를 쓰므로 → 32-bit
    add     r8, r8, r1      // high register: 16-bit ADD (register) T2
    adds    r0, r0, #300    // imm 이 8비트 초과 → 32-bit 자동 선택
    ldr     r0, [r1, #124]  // imm5*4 ≤ 124 → 16-bit
    ldr     r0, [r1, #128]  // 범위 밖 → ldr.w
    ldrb    r3, [r1], #1    // post-index 는 32-bit 만 존재
    cmp     r0, #0
    it      eq
    addeq   r0, r0, #1      // IT 안: 16-bit ADD 는 플래그를 안 쓴다 → 16-bit
    bx      lr
