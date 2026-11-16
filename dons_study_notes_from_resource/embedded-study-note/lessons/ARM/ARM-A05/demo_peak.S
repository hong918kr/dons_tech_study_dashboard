/* ARM-A05 demo_peak.S — "이론 피크" 를 남의 숫자 대신 직접 잰다: SDOT / SMMLA 의 지연(latency)과 처리량(throughput).
 * 모든 함수: void f(uint64_t iters)  — 루프 1회 = 해당 명령 16개 (+ subs/b.ne 2개).
 *   lat_add      : add x9,x9,#1 16개 사슬 (1 사이클/명령 가정 → 클럭 보정용, ARM-M03 방식)
 *   lat_sdot     : sdot 16개가 모두 v16 하나에 쌓임 → 명령 1개당 = 지연
 *   tput_sdot16  : 누산기 16개 (v16..v31) 독립 → 처리량
 *   tput_sdot4   : 누산기 4개 돌려쓰기 (본문 4x4 커널과 같은 사슬 수)
 *   tput_sdotel16: by-element 형식 (sdot v.4s, v.16b, v.4b[i]) 누산기 16개
 *   lat_smmla / tput_smmla16 / tput_smmla4 : SMMLA 같은 실험
 * v8..v15 는 안 쓴다 (callee-saved). 입력 v0, v1 은 0 이 아니어도 상관없다 (값은 버린다).
 */
#ifdef __APPLE__
#define SYM(x) _##x
#else
#define SYM(x) x
#endif
    .arch_extension dotprod
    .arch_extension i8mm
    .text

.macro FN name
    .globl SYM(\name)
    .p2align 4
SYM(\name):
.endm

FN lat_add
    mov     x9, #0
1:  .rept 16
    add     x9, x9, #1
    .endr
    subs    x0, x0, #1
    b.ne    1b
    ret

FN lat_sdot
1:  .rept 16
    sdot    v16.4s, v0.16b, v1.16b
    .endr
    subs    x0, x0, #1
    b.ne    1b
    ret

FN tput_sdot16
1:  .irp r, 16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    sdot    v\r\().4s, v0.16b, v1.16b
    .endr
    subs    x0, x0, #1
    b.ne    1b
    ret

FN tput_sdot4
1:  .rept 4
    sdot    v16.4s, v0.16b, v1.16b
    sdot    v17.4s, v0.16b, v1.16b
    sdot    v18.4s, v0.16b, v1.16b
    sdot    v19.4s, v0.16b, v1.16b
    .endr
    subs    x0, x0, #1
    b.ne    1b
    ret

FN tput_sdotel16
1:  .irp r, 16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    sdot    v\r\().4s, v0.16b, v1.4b[1]
    .endr
    subs    x0, x0, #1
    b.ne    1b
    ret

FN lat_smmla
1:  .rept 16
    smmla   v16.4s, v0.16b, v1.16b
    .endr
    subs    x0, x0, #1
    b.ne    1b
    ret

FN tput_smmla16
1:  .irp r, 16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    smmla   v\r\().4s, v0.16b, v1.16b
    .endr
    subs    x0, x0, #1
    b.ne    1b
    ret

FN tput_smmla4
1:  .rept 4
    smmla   v16.4s, v0.16b, v1.16b
    smmla   v17.4s, v0.16b, v1.16b
    smmla   v18.4s, v0.16b, v1.16b
    smmla   v19.4s, v0.16b, v1.16b
    .endr
    subs    x0, x0, #1
    b.ne    1b
    ret
