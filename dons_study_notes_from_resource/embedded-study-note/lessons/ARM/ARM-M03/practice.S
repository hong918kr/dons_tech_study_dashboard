/* ARM-M03 추가 연습 — asm 커널 2개 (연습 4, 5)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S -o /tmp/ARM-M03_practice
 */
#ifdef __APPLE__
.macro FUNC name
    .globl _\name
    .p2align 4
_\name:
.endm
#else
.macro FUNC name
    .globl \name
    .type \name, %function
    .p2align 4
\name:
.endm
#endif

    .text

/* 연습 4: uint64_t lat_ldr_idx(uint64_t iters, uint64_t i0, const uint64_t *tab)
 * 인덱스 추적: i ← tab[i] 를 16번/반복. 주소 = tab + i*8 (scaled register offset) */
    FUNC lat_ldr_idx
    cbz     x0, 2f
1:  .rept 16
    ldr     x1, [x2, x1, lsl #3]    // 주소 계산(shift+add)이 load 사슬 안에 들어간다
    .endr
    subs    x0, x0, #1
    b.ne    1b
2:  mov     x0, x1
    ret

/* 연습 5: double tput_fadd4(uint64_t iters, double x, double y)
 * 사슬 4개(d16–d19)에 fadd 4개씩 = 반복당 16개. 결과 = d16+d17+d18+d19 (순서대로) */
    FUNC tput_fadd4
    fmov    d16, d0
    fmov    d17, d0
    fmov    d18, d0
    fmov    d19, d0
    cbz     x0, 2f
1:  .rept 4
    fadd    d16, d16, d1            // 사슬 4개뿐: 지연(≈2.5–3 사이클)을 4개로 나눈 것보다 빨라질 수 없다
    fadd    d17, d17, d1
    fadd    d18, d18, d1
    fadd    d19, d19, d1
    .endr
    subs    x0, x0, #1
    b.ne    1b
2:  fadd    d0, d16, d17
    fadd    d0, d0, d18
    fadd    d0, d0, d19
    ret
