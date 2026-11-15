/* ARM-M03 시스템 카운터 마이크로벤치 — 카운터 읽기 + 측정 커널 (AArch64, Apple Mach-O + Linux ELF)
 *
 * build (Mac): cc -std=c11 -Wall -Wextra -Werror -O2 {starter|solution}.c {starter|solution}.S -o /tmp/ARM-M03_v
 * 모든 커널: uint64_t k(uint64_t iters, uint64_t x, uint64_t y)  (FP 커널은 x, y 가 d0, d1)
 *   한 반복(iteration) = 측정 대상 명령 정확히 16개 + 루프 꼬리 2개 (subs/b.ne).
 *   lat_*  : 16개가 한 사슬 (앞 결과가 다음 입력)      → 1개당 시간 = 지연(latency)
 *   tput_* : 16개가 서로 다른 16개 사슬에 한 개씩     → 1개당 시간 = 처리량의 역수(reciprocal throughput)
 *   반환값은 사슬의 최종 값 → C 참조 모델과 비교 가능 + 결과 싱크(sink).
 * leaf 함수: 스택 안 씀. 정수 사슬 레지스터 x1, x3–x17 (caller-saved, x18 은 Apple 예약이라 안 씀),
 *            FP 사슬 d16–d31 (caller-saved. d8–d15 는 callee-saved 라 안 씀).
 */
#ifdef __APPLE__
/* Mach-O: C 이름 앞에 '_', .type/.size 없음 */
.macro FUNC name
    .globl _\name
    .p2align 4
_\name:
.endm
.macro ENDF name
.endm
#else
/* ELF: 이름 그대로 + 함수 타입/크기 (gdb, perf 가 함수 경계를 안다) */
.macro FUNC name
    .globl \name
    .type \name, %function
    .p2align 4
\name:
.endm
.macro ENDF name
    .size \name, . - \name
.endm
#endif

/* 정수 16사슬 / FP 16사슬 레지스터 목록 */
#define ICHAINS x1, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
#define FCHAINS d16, d17, d18, d19, d20, d21, d22, d23, d24, d25, d26, d27, d28, d29, d30, d31

    .text

/* ===================== Part A: 카운터 읽기 ===================== */

/* uint64_t cnt_read(void) — ISB 로 앞 명령이 전부 끝난 뒤 가상 카운터를 읽는다 */
    FUNC cnt_read
    isb                             // 파이프라인 비우기: 앞 명령 완료 후에 mrs 실행
    mrs     x0, cntvct_el0          // 가상 카운트 (EL0 에서 읽기 허용: CNTKCTL_EL1.EL0VCTEN=1)
    ret
    ENDF cnt_read

/* uint64_t cnt_freq(void) — 카운터 주파수 (Hz). 펌웨어가 부팅 때 써 넣은 값을 읽기만 한다 */
    FUNC cnt_freq
    mrs     x0, cntfrq_el0
    ret
    ENDF cnt_freq

/* uint64_t cnt_read_noisb(void) — 비교용: ISB 없이 읽기 (데모 전용) */
    FUNC cnt_read_noisb
    mrs     x0, cntvct_el0
    ret
    ENDF cnt_read_noisb

/* ===================== 커널 공통 틀 =====================
 * 진입: x0 = iters. iters == 0 이면 본문을 건너뛴다 (cbz).
 */

/* uint64_t k_empty(iters, x, y) — 오버헤드 보정용: 루프 꼬리만 돈다, x 그대로 반환 */
    FUNC k_empty
    cbz     x0, 2f
1:  subs    x0, x0, #1
    b.ne    1b
2:  mov     x0, x1
    ret
    ENDF k_empty

/* uint64_t lat_add(iters, x, y) — add 16개 의존 사슬. 결과 = x + 16*iters (mod 2^64) */
    FUNC lat_add
    cbz     x0, 2f
1:  .rept 16
    add     x1, x1, #1              // x1 ← x1 + 1 : 다음 add 가 이 결과를 기다린다
    .endr
    subs    x0, x0, #1
    b.ne    1b
2:  mov     x0, x1
    ret
    ENDF lat_add

/* uint64_t tput_add(iters, x, y) — 16사슬 × add 1개. 사슬 초기값 모두 x. 결과 = 16 사슬의 합 */
    FUNC tput_add
    .irp r, ICHAINS
    mov     \r, x1                  // 16개 사슬 모두 x 로 시작 (x1 자신 포함)
    .endr
    cbz     x0, 2f
1:  .irp r, ICHAINS
    add     \r, \r, #1              // 서로 독립: 매 사이클 ALU 수만큼 동시에 실행 가능
    .endr
    subs    x0, x0, #1
    b.ne    1b
2:  mov     x0, xzr
    .irp r, ICHAINS
    add     x0, x0, \r              // 합 = 16x + 16*iters
    .endr
    ret
    ENDF tput_add

/* uint64_t lat_mul(iters, x, y) — x ← x*y 16번/반복 */
    FUNC lat_mul
    cbz     x0, 2f
1:  .rept 16
    mul     x1, x1, x2
    .endr
    subs    x0, x0, #1
    b.ne    1b
2:  mov     x0, x1
    ret
    ENDF lat_mul

/* uint64_t tput_mul(iters, x, y) — 16사슬, 사슬 i 초기값 x+i, 결과 = 사슬들의 XOR */
    FUNC tput_mul
    .irp r, ICHAINS
    mov     \r, x1                  // 16개 사슬 모두 x 로 채운 뒤
    .endr
    // 사슬 i 초기값 = x + i (x1 이 i=0)
    add     x3, x3, #1
    add     x4, x4, #2
    add     x5, x5, #3
    add     x6, x6, #4
    add     x7, x7, #5
    add     x8, x8, #6
    add     x9, x9, #7
    add     x10, x10, #8
    add     x11, x11, #9
    add     x12, x12, #10
    add     x13, x13, #11
    add     x14, x14, #12
    add     x15, x15, #13
    add     x16, x16, #14
    add     x17, x17, #15
    cbz     x0, 2f
1:  .irp r, ICHAINS
    mul     \r, \r, x2
    .endr
    subs    x0, x0, #1
    b.ne    1b
2:  mov     x0, xzr
    .irp r, ICHAINS
    eor     x0, x0, \r
    .endr
    ret
    ENDF tput_mul

/* uint64_t lat_madd(iters, x, y) — x ← x*y + y (곱하는 쪽 Rn 으로 사슬) */
    FUNC lat_madd
    cbz     x0, 2f
1:  .rept 16
    madd    x1, x1, x2, x2
    .endr
    subs    x0, x0, #1
    b.ne    1b
2:  mov     x0, x1
    ret
    ENDF lat_madd

/* uint64_t lat_madd_acc(iters, x, y) — x ← y*y + x (더하는 쪽 Ra 로 사슬: 누산기 전달) */
    FUNC lat_madd_acc
    cbz     x0, 2f
1:  .rept 16
    madd    x1, x2, x2, x1
    .endr
    subs    x0, x0, #1
    b.ne    1b
2:  mov     x0, x1
    ret
    ENDF lat_madd_acc

/* uint64_t lat_sdiv(iters, x, y) — x ← x / y (signed). y=1 이면 값이 안 변해 사슬이 계속 같은 몫 크기 */
    FUNC lat_sdiv
    cbz     x0, 2f
1:  .rept 16
    sdiv    x1, x1, x2
    .endr
    subs    x0, x0, #1
    b.ne    1b
2:  mov     x0, x1
    ret
    ENDF lat_sdiv

/* uint64_t tput_sdiv(iters, x, y) — 16사슬 × sdiv, 초기값 모두 x, 결과 = 16사슬의 합 (XOR 면 짝수 개 같은 값 → 0) */
    FUNC tput_sdiv
    .irp r, ICHAINS
    mov     \r, x1
    .endr
    cbz     x0, 2f
1:  .irp r, ICHAINS
    sdiv    \r, \r, x2
    .endr
    subs    x0, x0, #1
    b.ne    1b
2:  mov     x0, xzr
    .irp r, ICHAINS
    add     x0, x0, \r
    .endr
    ret
    ENDF tput_sdiv

/* uint64_t lat_udiv(iters, x, y) — x ← x / y (unsigned) */
    FUNC lat_udiv
    cbz     x0, 2f
1:  .rept 16
    udiv    x1, x1, x2
    .endr
    subs    x0, x0, #1
    b.ne    1b
2:  mov     x0, x1
    ret
    ENDF lat_udiv

/* uint64_t lat_ldr(iters, p, y) — 포인터 추적: p ← *p 16번/반복. *p == p 면 같은 L1 줄만 계속 읽는다 */
    FUNC lat_ldr
    cbz     x0, 2f
1:  .rept 16
    ldr     x1, [x1]                // 주소가 직전 load 의 결과 → load-to-use 지연이 그대로 보인다
    .endr
    subs    x0, x0, #1
    b.ne    1b
2:  mov     x0, x1
    ret
    ENDF lat_ldr

/* uint64_t tput_ldr(iters, p, y) — 16개 독립 load: 사슬 i 는 매번 p[i] 를 읽는다 (주소가 결과에 의존 안 함).
 * 결과 = 마지막에 읽은 16개 값의 합 */
    FUNC tput_ldr
    mov     x2, x1                  // x2 = 베이스 주소 (x1 은 사슬로 쓴다)
    mov     x1, xzr
    .irp r, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12, x13, x14, x15, x16, x17
    mov     \r, xzr
    .endr
    cbz     x0, 2f
1:  ldr     x1, [x2, #0]            // 16개 load 의 주소는 x2 + 상수 → 서로 독립
    ldr     x3, [x2, #8]
    ldr     x4, [x2, #16]
    ldr     x5, [x2, #24]
    ldr     x6, [x2, #32]
    ldr     x7, [x2, #40]
    ldr     x8, [x2, #48]
    ldr     x9, [x2, #56]
    ldr     x10, [x2, #64]
    ldr     x11, [x2, #72]
    ldr     x12, [x2, #80]
    ldr     x13, [x2, #88]
    ldr     x14, [x2, #96]
    ldr     x15, [x2, #104]
    ldr     x16, [x2, #112]
    ldr     x17, [x2, #120]
    subs    x0, x0, #1
    b.ne    1b
2:  mov     x0, xzr
    .irp r, ICHAINS
    add     x0, x0, \r
    .endr
    ret
    ENDF tput_ldr

/* ---------- FP (double) : double k(uint64_t iters, double x, double y), x=d0 y=d1 ---------- */

/* double lat_fadd(iters, x, y) — x ← x + y 16번/반복 */
    FUNC lat_fadd
    cbz     x0, 2f
1:  .rept 16
    fadd    d0, d0, d1
    .endr
    subs    x0, x0, #1
    b.ne    1b
2:  ret
    ENDF lat_fadd

/* double tput_fadd(iters, x, y) — 16사슬(d16–d31) × fadd. 결과 = d16+d17+...+d31 (왼쪽부터 순서대로) */
    FUNC tput_fadd
    .irp r, FCHAINS
    fmov    \r, d0
    .endr
    cbz     x0, 2f
1:  .irp r, FCHAINS
    fadd    \r, \r, d1
    .endr
    subs    x0, x0, #1
    b.ne    1b
2:  fmov    d0, d16
    .irp r, d17, d18, d19, d20, d21, d22, d23, d24, d25, d26, d27, d28, d29, d30, d31
    fadd    d0, d0, \r
    .endr
    ret
    ENDF tput_fadd

/* double lat_fmul(iters, x, y) — x ← x * y */
    FUNC lat_fmul
    cbz     x0, 2f
1:  .rept 16
    fmul    d0, d0, d1
    .endr
    subs    x0, x0, #1
    b.ne    1b
2:  ret
    ENDF lat_fmul

/* double tput_fmul(iters, x, y) — 16사슬 × fmul, 결과 = 순서대로 합 */
    FUNC tput_fmul
    .irp r, FCHAINS
    fmov    \r, d0
    .endr
    cbz     x0, 2f
1:  .irp r, FCHAINS
    fmul    \r, \r, d1
    .endr
    subs    x0, x0, #1
    b.ne    1b
2:  fmov    d0, d16
    .irp r, d17, d18, d19, d20, d21, d22, d23, d24, d25, d26, d27, d28, d29, d30, d31
    fadd    d0, d0, \r
    .endr
    ret
    ENDF tput_fmul

/* double lat_fmadd(iters, x, y) — x ← x*y + y (한 번 반올림: fma(x, y, y)) */
    FUNC lat_fmadd
    cbz     x0, 2f
1:  .rept 16
    fmadd   d0, d0, d1, d1
    .endr
    subs    x0, x0, #1
    b.ne    1b
2:  ret
    ENDF lat_fmadd

/* double tput_fmadd(iters, x, y) — 16사슬 × fmadd, 결과 = 순서대로 합 */
    FUNC tput_fmadd
    .irp r, FCHAINS
    fmov    \r, d0
    .endr
    cbz     x0, 2f
1:  .irp r, FCHAINS
    fmadd   \r, \r, d1, d1
    .endr
    subs    x0, x0, #1
    b.ne    1b
2:  fmov    d0, d16
    .irp r, d17, d18, d19, d20, d21, d22, d23, d24, d25, d26, d27, d28, d29, d30, d31
    fadd    d0, d0, \r
    .endr
    ret
    ENDF tput_fmadd
