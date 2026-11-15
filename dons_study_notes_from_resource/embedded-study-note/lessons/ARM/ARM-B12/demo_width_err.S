/* ENV: cross — ARM-B12 지킬 수 없는 .n 접미사: GNU as 는 에러, LLVM(clang 21) 은 조용히 32비트로 */
    .syntax unified
    .thumb
    .text
    adds.n  r0, r0, #300    // imm8 초과
    add.n   r0, r0, #1      // IT 밖 16-bit ADD(imm) 는 ADDS(플래그 씀)뿐
    ldr.n   r0, [r1, #128]  // 16-bit LDR imm5*4 범위(0..124) 밖
    ldrb.n  r3, [r1], #1    // post-index 의 16-bit 인코딩은 없다
    adds.n  r0, r0, #1      // 이건 가능
