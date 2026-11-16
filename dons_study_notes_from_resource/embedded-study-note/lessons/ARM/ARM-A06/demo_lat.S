/* ARM-A06 demo: crc32cx / pmull 지연(latency)·처리량(throughput) 마이크로벤치 커널 (맥 전용, Mach-O 심볼)
 * 각 함수: x0 = 반복 횟수. 반복마다 명령 8개 (chains3 만 9개). 결과를 돌려줘서 지워지지 않게 한다.
 */
    .arch_extension crc
    .text
// 의존 사슬 기준: add 8개 (= 1 사이클/명령 가정으로 클럭 추정)
    .globl _lat_add
    .p2align 4
_lat_add:
    mov     x1, #0
1:  add     x1, x1, #1
    add     x1, x1, #1
    add     x1, x1, #1
    add     x1, x1, #1
    add     x1, x1, #1
    add     x1, x1, #1
    add     x1, x1, #1
    add     x1, x1, #1
    subs    x0, x0, #1
    b.ne    1b
    mov     x0, x1
    ret
// crc32cx 사슬 1개 (각 명령이 앞 결과를 기다림) = 지연
    .globl _lat_crc
    .p2align 4
_lat_crc:
    mov     w1, #0
    mov     x2, #0x1234
1:
    .rept 8
    crc32cx w1, w1, x2
    .endr
    subs    x0, x0, #1
    b.ne    1b
    mov     w0, w1
    ret
// pmull 사슬 1개
    .globl _lat_pmull
    .p2align 4
_lat_pmull:
    movi    v0.16b, #1
    movi    v1.16b, #3
1:
    .rept 8
    pmull   v0.1q, v0.1d, v1.1d
    .endr
    subs    x0, x0, #1
    b.ne    1b
    fmov    x0, d0
    ret
// 독립 사슬 2개 / 3개 / 4개 / 8개 (총 명령 수는 8, 3개짜리만 9)
    .globl _tp_crc2
    .p2align 4
_tp_crc2:
    mov     w1, #0
    mov     w3, #0
    mov     x2, #0x1234
1:
    .rept 4
    crc32cx w1, w1, x2
    crc32cx w3, w3, x2
    .endr
    subs    x0, x0, #1
    b.ne    1b
    eor     w0, w1, w3
    ret
    .globl _tp_crc3
    .p2align 4
_tp_crc3:
    mov     w1, #0
    mov     w3, #0
    mov     w4, #0
    mov     x2, #0x1234
1:
    .rept 3
    crc32cx w1, w1, x2
    crc32cx w3, w3, x2
    crc32cx w4, w4, x2
    .endr
    subs    x0, x0, #1
    b.ne    1b
    eor     w0, w1, w3
    eor     w0, w0, w4
    ret
    .globl _tp_crc4
    .p2align 4
_tp_crc4:
    mov     w1, #0
    mov     w3, #0
    mov     w4, #0
    mov     w5, #0
    mov     x2, #0x1234
1:
    .rept 2
    crc32cx w1, w1, x2
    crc32cx w3, w3, x2
    crc32cx w4, w4, x2
    crc32cx w5, w5, x2
    .endr
    subs    x0, x0, #1
    b.ne    1b
    eor     w0, w1, w3
    eor     w0, w0, w4
    eor     w0, w0, w5
    ret
    .globl _tp_crc8
    .p2align 4
_tp_crc8:
    mov     w1, #0
    mov     w3, #0
    mov     w4, #0
    mov     w5, #0
    mov     w6, #0
    mov     w7, #0
    mov     w8, #0
    mov     w9, #0
    mov     x2, #0x1234
1:  crc32cx w1, w1, x2
    crc32cx w3, w3, x2
    crc32cx w4, w4, x2
    crc32cx w5, w5, x2
    crc32cx w6, w6, x2
    crc32cx w7, w7, x2
    crc32cx w8, w8, x2
    crc32cx w9, w9, x2
    subs    x0, x0, #1
    b.ne    1b
    eor     w0, w1, w3
    eor     w0, w0, w9
    ret
// 독립 pmull 4개 (처리량 참고)
    .globl _tp_pmull4
    .p2align 4
_tp_pmull4:
    movi    v0.16b, #1
    movi    v1.16b, #3
    movi    v2.16b, #5
    movi    v3.16b, #7
    movi    v4.16b, #9
1:
    .rept 2
    pmull   v0.1q, v0.1d, v4.1d
    pmull   v1.1q, v1.1d, v4.1d
    pmull   v2.1q, v2.1d, v4.1d
    pmull   v3.1q, v3.1d, v4.1d
    .endr
    subs    x0, x0, #1
    b.ne    1b
    fmov    x0, d0
    ret
