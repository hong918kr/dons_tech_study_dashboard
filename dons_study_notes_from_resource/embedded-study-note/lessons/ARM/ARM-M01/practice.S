/* ARM-M01 추가 연습 답 — AArch64 asm (Apple Mach-O + Linux ELF 겸용)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_violators.S -o p && ./p
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
    .macro FUNC name
    .globl  SYM(\name)
    FTYPE(SYM(\name))
    .p2align 2
SYM(\name):
    .endm
    .text

/* 연습 1: rgb12 make_rgb(int32_t r, int32_t g, int32_t b) — 12 B 구조체 → x0 = {r,g}, x1 = {b, 미정} */
FUNC make_rgb
    mov     w9, w0                      // r (윗 32비트 0 으로 정리)
    orr     x0, x9, x1, lsl #32         // x0 = g:r  (메모리 +0 이 하위 → r 이 아래)
    mov     w1, w2                      // x1 = b (+8 ~ +11)
    ret
    FSIZE(SYM(make_rgb))

/* 연습 2: f5 f5_scale(f5 v, float k) — 인자도 포인터(x0), 결과도 포인터(x8), k 는 s0 (NSRN 은 따로 센다) */
FUNC f5_scale
    mov     x9, #0                      // i
1:  ldr     s1, [x0, x9, lsl #2]        // v.v[i]
    fmul    s1, s1, s0                  // * k
    str     s1, [x8, x9, lsl #2]        // result.v[i]
    add     x9, x9, #1
    cmp     x9, #5
    b.lo    1b
    ret
    FSIZE(SYM(f5_scale))

/* 연습 3: quat quat_add3(quat a, quat b, quat c) — a = s0..s3, b = s4..s7, c 는 레지스터 부족 → 통째로 [sp..sp+15] */
FUNC quat_add3
    ldp     s16, s17, [sp]              // c.w, c.x
    ldp     s18, s19, [sp, #8]          // c.y, c.z
    fadd    s0, s0, s4                  // (a + b) + c  — C 와 같은 순서
    fadd    s1, s1, s5
    fadd    s2, s2, s6
    fadd    s3, s3, s7
    fadd    s0, s0, s16
    fadd    s1, s1, s17
    fadd    s2, s2, s18
    fadd    s3, s3, s19
    ret
    FSIZE(SYM(quat_add3))

/* 연습 4: long mixed_iv(long a0..a7, double d, long a8) — d 는 d0 (스택 아님!), a8 은 [sp] */
FUNC mixed_iv
    ldr     x9, [sp]                    // a8: 정수 레지스터가 다 찼다
    fcvtzs  x10, d0                     // (long)d  — 0 쪽으로 자름
    add     x0, x0, x9
    add     x0, x0, x10                 // a0 + a8 + (long)d
    ret
    FSIZE(SYM(mixed_iv))

/* 연습 5: long vsum(long n, ...) — 가변 인자 n 개(전부 long) 의 합
 * Apple: 가변 인자는 처음부터 전부 스택 8 B 슬롯 → k 번째 = [sp + 8k]
 * 표준:  가변 인자도 x1..x7 → 그 뒤 스택. x0..x7 을 sp 바로 아래 64 B 에 저장하면 [sp+8] 부터 한 줄로 이어진다.
 */
FUNC vsum
#ifdef __APPLE__
    mov     x9, sp                      // p = 첫 가변 인자
#else
    stp     x0, x1, [sp, #-64]!         // 레지스터 저장 영역 = 들어온 스택 인자 바로 아래
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    add     x9, sp, #8                  // p = x1 이 저장된 곳 (그 뒤로 x2.., 이어서 [old sp]..)
#endif
    mov     x10, x0                     // 남은 개수
    mov     x0, #0
    cbz     x10, 2f
1:  ldr     x11, [x9], #8               // *p++
    add     x0, x0, x11
    subs    x10, x10, #1
    b.ne    1b
2:
#ifndef __APPLE__
    add     sp, sp, #64
#endif
    ret
    FSIZE(SYM(vsum))
