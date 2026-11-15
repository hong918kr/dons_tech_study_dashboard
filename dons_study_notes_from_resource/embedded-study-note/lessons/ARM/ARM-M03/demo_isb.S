/* ARM-M03 demo_isb.S — 측정 구간을 asm 한 함수 안에 넣고 ISB 유무만 바꾼다 (데모 전용)
 *   uint64_t region_isb(uint64_t x, uint64_t y)    : isb; mrs t0; [sdiv x0,x0,x1 × 256]; isb; mrs t1; return t1-t0
 *   uint64_t region_noisb(uint64_t x, uint64_t y)  : 같은 구간, ISB 없이
 *   uint64_t region_isb_dep(uint64_t x, uint64_t y): t1 앞에 사슬 결과에 의존하는 load 를 둔다 (ISB 대신 의존성? → 안 된다)
 *   uint64_t region_ss(uint64_t x, uint64_t y)     : ISB 없이 CNTVCTSS_EL0 (self-synchronized, FEAT_ECV)
 * 사슬 결과는 버린다 (x0 은 시간 차로 덮어씀). y = 1 이면 몫이 안 변해 사슬 길이가 그대로.
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
    FUNC region_isb
    isb
    mrs     x9, cntvct_el0          // t0: 앞 명령이 다 끝난 뒤
    .rept 256
    sdiv    x0, x0, x1              // 256 × 약 7 사이클 의존 사슬
    .endr
    isb                             // 사슬이 끝날 때까지 다음 명령을 시작하지 않는다
    mrs     x10, cntvct_el0         // t1
    sub     x0, x10, x9
    ret

    FUNC region_noisb
    mrs     x9, cntvct_el0          // t0: 언제 실행될지 모른다
    .rept 256
    sdiv    x0, x0, x1
    .endr
    mrs     x10, cntvct_el0         // t1: sdiv 결과에 의존하지 않음 → 사슬보다 먼저 실행될 수 있다
    sub     x0, x10, x9
    ret

    FUNC region_isb_dep
    isb
    mrs     x9, cntvct_el0
    .rept 256
    sdiv    x0, x0, x1
    .endr
    and     x11, x0, xzr            // x11 = 0 이지만 x0(사슬 끝)에 의존
    add     x12, sp, x11            // 주소가 사슬에 의존하는 load
    ldr     x12, [x12, #-16]        // (스택 레드존 안쪽 읽기만, 값은 안 씀)
    mrs     x10, cntvct_el0         // 이 mrs 는 x12 에 의존하지 않는다 → 의존성만으로는 순서가 안 묶인다
    sub     x0, x10, x9
    ret

/* FEAT_ECV (Armv8.6) 의 self-synchronized 카운터 CNTVCTSS_EL0 = S3_3_C14_C0_6 (이 M6: hw.optional.arm.FEAT_ECV=1).
 * 어셈블러가 이름을 모를 수 있어 일반 인코딩 이름으로 쓴다 (objdump 는 CNTVCTSS_EL0 로 보여준다). */
    FUNC region_ss
    mrs     x9, S3_3_C14_C0_6       // t0 = CNTVCTSS_EL0, ISB 없음
    .rept 256
    sdiv    x0, x0, x1
    .endr
    mrs     x10, S3_3_C14_C0_6      // t1 = CNTVCTSS_EL0, ISB 없음
    sub     x0, x10, x9
    ret
