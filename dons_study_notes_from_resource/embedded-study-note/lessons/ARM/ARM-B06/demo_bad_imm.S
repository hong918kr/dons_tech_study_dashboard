/* ARM-B06 demo: 비트필드 명령의 즉시값 제약 — 일부러 어셈블 에러를 낸다
 * cc -c demo_bad_imm.S -o /tmp/ARM-B06_bad.o     (실패가 정상) */
    .text
    .p2align 2
bad_examples:
    ubfx    x0, x0, x1, #4          // lsb 를 레지스터로? → UBFX 는 즉시값만
    bfi     w0, w1, #30, #4         // lsb + width = 34 > 32
    ubfx    w0, w0, #4, #0          // width 0
    sbfx    x0, x0, #60, #8         // 60 + 8 > 64
    ret
