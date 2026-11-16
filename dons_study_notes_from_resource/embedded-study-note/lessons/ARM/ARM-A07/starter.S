/* ARM-A07 포화·고정소수점 — 손으로 쓴 AArch64 NEON (맥 M-칩 네이티브, 리눅스 ELF 도 같은 파일)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c solution.S shared_bugs.S -o /tmp/ARM-A07_solution
 * 인자: x0..x4, int16_t 인자는 w3 의 하위 16비트. 반환 없음.
 * v0..v7, v16..v31 만 쓴다 (v8..v15 하위 64비트는 callee-saved).
 * 명세 (solution.c 머리말): acc = SQDMLAL 누적 (탭 순서), y = SQRSHRN(acc, #16).
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

    .text

// void q15_scale_asm(int16_t *y, const int16_t *x, size_t n, int16_t g)   y[i] = SQRDMULH(x[i], g)
    .globl SYM(q15_scale_asm)
    FTYPE(q15_scale_asm)
    .p2align 2
SYM(q15_scale_asm):
    // TODO: dup v1.8h, w3 → 8개씩 ld1 / sqrdmulh v.8h / st1 → 꼬리 ldr h / sqrdmulh h / str h
    ret                                 // 스텁: y 를 건드리지 않는다
    FSIZE(q15_scale_asm)

// void fir_q15_asm(const int16_t *x, size_t n, const int16_t *h, size_t T, int16_t *y)   (T >= 1)
//   y[i] = SQRSHRN( SQDMLAL 누적 k=0..T-1 of h[k]·x[i+T-1-k] , #16 )
//   lane j 가 출력 i+j 를 맡는다 → 탭 순서는 명세 그대로 (탭 방향으로 나누면 포화 때 틀린다)
    .globl SYM(fir_q15_asm)
    FTYPE(fir_q15_asm)
    .p2align 2
SYM(fir_q15_asm):
    // TODO: 8개 출력씩 — movi acc=0 ×2 → 탭 루프 (ld1 {v.8h} / ld1r h[k] / sqdmull / sqdmull2 / sqadd ×2)
    //       → sqrshrn / sqrshrn2 #16 → st1. 꼬리 출력은 스칼라 형태 (ldr h / sqdmull s,h,h / sqadd s / sqrshrn h,s)
    ret                                 // 스텁: y 를 건드리지 않는다
    FSIZE(fir_q15_asm)
