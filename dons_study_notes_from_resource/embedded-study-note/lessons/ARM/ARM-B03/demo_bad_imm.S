/* ARM-B03 demo_bad_imm.S — 어셈블러가 거절하는 즉시값들 (일부러 에러). 빌드하지 말고 에러만 본다:
 *   clang -c demo_bad_imm.S -o /dev/null          (맥)
 *   gcc   -c demo_bad_imm.S -o /dev/null          (리눅스 VM, GNU as)
 */
    .text
    .p2align 2
bad:
    and  x0, x0, #0x1234              // 1 이 한 덩어리가 아님 -> 논리 즉시값 불가
    and  w0, w0, #0                   // 0 은 논리 즉시값 불가 (s = 0)
    orr  x0, x0, #0xffffffffffffffff  // ~0 도 불가 (s = e)
    and  w0, w0, #0x5555555555555555  // 32비트 명령에 64비트 값
    movz x0, #0x12345                 // imm16 범위 초과
    movk x0, #1, lsl #8               // hw 시프트는 0/16/32/48 만
    movz w0, #1, lsl #32              // w 레지스터는 lsl 0/16 만
    mov  x0, #0x12345678              // mov 별칭: 명령 1개로 안 되는 값
    mov  x0, #0xffffffff12345678      // MOVN+MOVK 2개가 필요 -> mov 한 줄로는 불가
    ret
