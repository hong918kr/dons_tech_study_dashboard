/* ARM-M01 demo_szero.S — 스칼라 FP 쓰기(fmov s16)가 v16 의 나머지 96비트를 0 으로 만드는지 실측 (Mac 전용 심볼)
 * build: cc -O2 demo_szero.c demo_szero.S -o /tmp/ARM-M01_szero && /tmp/ARM-M01_szero */
    .globl _zt
    .p2align 2
_zt:
    movz x9, #0xAAAA, lsl #48
    movk x9, #0xBBBB, lsl #16
    fmov d16, x9
    mov  v16.d[1], x9
    fmov s16, #1.0             // 32비트만 "쓴" 것처럼 보이지만…
    fmov x0, d16
    mov  x1, v16.d[1]
    stp  x0, x1, [x2]
    ret
