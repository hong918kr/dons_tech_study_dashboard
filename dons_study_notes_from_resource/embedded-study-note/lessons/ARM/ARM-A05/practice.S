/* ARM-A05 추가 연습 — asm: 8x8 행렬곱 커널 하나를 .macro 로 찍어내서 SMMLA(s8·s8) / USMMLA(u8·s8) 두 개를 만든다.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_bugs.S -o /tmp/ARM-A05_p && /tmp/ARM-A05_p
 * 시그니처는 본문과 같다: void kern(const int8_t *Ap, const int8_t *Bp, size_t kb, int32_t *C, size_t ldc)
 * (USMMLA 판은 Ap 의 바이트를 부호 없는 수로 읽는다 — C 에서는 uint8_t* 를 그대로 넘겨도 바이트는 같다)
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

    .arch_extension i8mm
    .text

// 2x2 조각 두 개 (acc[p][0..1] / acc[p][2..3]) → 행 2p, 2p+1 (각 8열) 저장. x3 는 다음 행으로 이동.
.macro STORE_PAIR a0, a1, a2, a3
    zip1    v0.2d, \a0\().2d, \a1\().2d
    zip1    v1.2d, \a2\().2d, \a3\().2d
    zip2    v2.2d, \a0\().2d, \a1\().2d
    zip2    v3.2d, \a2\().2d, \a3\().2d
    st1     {v0.4s, v1.4s}, [x3], x4
    st1     {v2.4s, v3.4s}, [x3], x4
.endm

// 본문 smmla_8x8_kernel 과 같은 구조. \op = smmla 또는 usmmla.
.macro MMLA8x8 op
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
    \op     v16.4s, v0.16b, v4.16b
    \op     v17.4s, v0.16b, v5.16b
    \op     v18.4s, v0.16b, v6.16b
    \op     v19.4s, v0.16b, v7.16b
    \op     v20.4s, v1.16b, v4.16b
    \op     v21.4s, v1.16b, v5.16b
    \op     v22.4s, v1.16b, v6.16b
    \op     v23.4s, v1.16b, v7.16b
    \op     v24.4s, v2.16b, v4.16b
    \op     v25.4s, v2.16b, v5.16b
    \op     v26.4s, v2.16b, v6.16b
    \op     v27.4s, v2.16b, v7.16b
    \op     v28.4s, v3.16b, v4.16b
    \op     v29.4s, v3.16b, v5.16b
    \op     v30.4s, v3.16b, v6.16b
    \op     v31.4s, v3.16b, v7.16b
    subs    x2, x2, #1
    b.ne    1b
2:  lsl     x4, x4, #2
    STORE_PAIR v16, v17, v18, v19
    STORE_PAIR v20, v21, v22, v23
    STORE_PAIR v24, v25, v26, v27
    STORE_PAIR v28, v29, v30, v31
    ret
.endm

// 연습 2 용: 본문 smmla_8x8_kernel 과 같은 일 (s8 x s8)
    .globl SYM(p_smmla_8x8_kernel)
    FTYPE(p_smmla_8x8_kernel)
    .p2align 4
SYM(p_smmla_8x8_kernel):
    MMLA8x8 smmla
    FSIZE(p_smmla_8x8_kernel)

// 연습 3: A = uint8 (활성값, ReLU 뒤 0..255), B = int8 (가중치). USMMLA Vd.4s, Vn(u8).16b, Vm(s8).16b
    .globl SYM(usmmla_8x8_kernel)
    FTYPE(usmmla_8x8_kernel)
    .p2align 4
SYM(usmmla_8x8_kernel):
    MMLA8x8 usmmla
    FSIZE(usmmla_8x8_kernel)
