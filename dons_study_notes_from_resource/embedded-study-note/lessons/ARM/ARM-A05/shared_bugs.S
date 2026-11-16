/* ARM-A05 shared_bugs.S — 일부러 틀린 커널 2개 (TODO 아님). 테스트가 "잡는다"는 것을 단언한다.
 * starter / solution / practice 모두 함께 링크된다 (ARM 트랙 빌드 규칙: shared_*.S).
 */
#ifdef __APPLE__
#define SYM(x) _##x
#define FTYPE(x)
#define FSIZE(x)
#else
#define SYM(x) x
#define FTYPE(x) .type x, %function
#define FSIZE(x) .size x, .-x
#endif

    .arch_extension dotprod
    .arch_extension i8mm
    .text

/* 버그 1: SDOT 대신 UDOT — 바이트를 부호 없는 수로 곱한다. 입력이 0..127 이면 결과가 같아서 통과해 버린다.
 * (-1 = 0xFF 가 255 로 읽힘). 테스트 데이터에 음수가 없으면 못 잡는다. */
    .globl SYM(bug_udot_4x4_kernel)
    FTYPE(bug_udot_4x4_kernel)
    .p2align 4
SYM(bug_udot_4x4_kernel):
    movi    v0.16b, #0
    movi    v1.16b, #0
    movi    v2.16b, #0
    movi    v3.16b, #0
    cbz     x2, 2f
1:  ld1     {v4.16b}, [x0], #16
    ld1     {v5.16b}, [x1], #16
    udot    v0.4s, v5.16b, v4.4b[0]     // ← 버그: udot
    udot    v1.4s, v5.16b, v4.4b[1]
    udot    v2.4s, v5.16b, v4.4b[2]
    udot    v3.4s, v5.16b, v4.4b[3]
    subs    x2, x2, #1
    b.ne    1b
2:  lsl     x4, x4, #2
    st1     {v0.4s}, [x3], x4
    st1     {v1.4s}, [x3], x4
    st1     {v2.4s}, [x3], x4
    st1     {v3.4s}, [x3]
    ret
    FSIZE(bug_udot_4x4_kernel)

/* 버그 2: SMMLA 누산기를 "행" 이라고 착각해 zip 없이 그대로 저장. acc[p][q] 는 2x2 조각
 * [C(2p,2q) C(2p,2q+1) C(2p+1,2q) C(2p+1,2q+1)] 인데, 행 2p 자리에 acc[p][0], acc[p][1] 을 그대로 쓴다. */
    .globl SYM(bug_smmla_nozip_kernel)
    FTYPE(bug_smmla_nozip_kernel)
    .p2align 4
SYM(bug_smmla_nozip_kernel):
    movi    v16.16b, #0
    movi    v17.16b, #0
    movi    v18.16b, #0
    movi    v19.16b, #0
    movi    v20.16b, #0
    movi    v21.16b, #0
    movi    v22.16b, #0
    movi    v23.16b, #0
    movi    v24.16b, #0
    movi    v25.16b, #0
    movi    v26.16b, #0
    movi    v27.16b, #0
    movi    v28.16b, #0
    movi    v29.16b, #0
    movi    v30.16b, #0
    movi    v31.16b, #0
    cbz     x2, 2f
1:  ld1     {v0.16b-v3.16b}, [x0], #64
    ld1     {v4.16b-v7.16b}, [x1], #64
    smmla   v16.4s, v0.16b, v4.16b
    smmla   v17.4s, v0.16b, v5.16b
    smmla   v18.4s, v0.16b, v6.16b
    smmla   v19.4s, v0.16b, v7.16b
    smmla   v20.4s, v1.16b, v4.16b
    smmla   v21.4s, v1.16b, v5.16b
    smmla   v22.4s, v1.16b, v6.16b
    smmla   v23.4s, v1.16b, v7.16b
    smmla   v24.4s, v2.16b, v4.16b
    smmla   v25.4s, v2.16b, v5.16b
    smmla   v26.4s, v2.16b, v6.16b
    smmla   v27.4s, v2.16b, v7.16b
    smmla   v28.4s, v3.16b, v4.16b
    smmla   v29.4s, v3.16b, v5.16b
    smmla   v30.4s, v3.16b, v6.16b
    smmla   v31.4s, v3.16b, v7.16b
    subs    x2, x2, #1
    b.ne    1b
2:  lsl     x4, x4, #2
    st1     {v16.4s, v17.4s}, [x3], x4  // ← 버그: 2x2 조각을 행으로 착각 (zip 없음)
    st1     {v18.4s, v19.4s}, [x3], x4
    st1     {v20.4s, v21.4s}, [x3], x4
    st1     {v22.4s, v23.4s}, [x3], x4
    st1     {v24.4s, v25.4s}, [x3], x4
    st1     {v26.4s, v27.4s}, [x3], x4
    st1     {v28.4s, v29.4s}, [x3], x4
    st1     {v30.4s, v31.4s}, [x3]
    ret
    FSIZE(bug_smmla_nozip_kernel)
