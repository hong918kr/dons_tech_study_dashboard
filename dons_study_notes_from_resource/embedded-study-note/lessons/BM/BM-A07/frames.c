/* ENV: qemu */
/* BM-A07 "프레임 크기를 아는" 함수들 — starter/solution/practice 와 함께 링크된다 (같은 레슨 안의 파일).
 *
 * 이 파일만 따로 -fstack-usage 로 컴파일해 frames.su 를 얻고, run_qemu.sh 가 그것을 su_gen.h
 * (#define SU_path_leaf 72 …) 로 바꿔 solution.c 에 넣는다 → 펌웨어 안의 CHECK 가 "컴파일러가 말한 프레임"
 * 과 "스택 페인팅으로 잰 사용량" 을 직접 비교한다. 이 파일은 su_gen.h 를 include 하지 않으므로 코드가 안 바뀐다.
 *
 * 모든 지역 배열은 volatile 이고 "전부" 쓴다 → 프레임의 가장 낮은 워드까지 실제로 쓰인다 (측정 = 정적값이 되는 조건).
 * 예외: big_sparse 는 256 B 를 예약하고 맨 위 한 워드만 쓴다 (Part B: 가드 건너뛰기).
 */
#include <stdint.h>

#define REG32(a)    (*(volatile uint32_t *)(uintptr_t)(a))
#define NVIC_ISPR0  REG32(0xE000E200u)
#define IRQ_LOW     14u                /* mps2-an385 에서 장치가 안 붙은 줄 (BM-M02 에서 확인) */
#define IRQ_HIGH    15u

#define MODE_PEND_LOW   1u             /* path_leaf 가 IRQ_LOW 를 건다 */
#define MODE_NEST_HIGH  2u             /* IRQ_LOW 핸들러가 IRQ_HIGH 를 건다 (중첩) */
#define MODE_MISALIGN   4u             /* SP 를 4 B 어긋난 상태에서 IRQ 를 받는다 (정렬 패딩 실험) */

volatile uint32_t g_frames_mode;
volatile uint32_t g_xpsr_low, g_xpsr_high;       /* ISR 이 본 "쌓인 xPSR" — bit9 = 진입 시 4 B 정렬 패딩 */
volatile uint32_t g_isr_low_runs, g_isr_high_runs;
volatile uint32_t g_min_sp;                      /* 재귀 맨 아래 프레임의 SP */
volatile uint32_t g_bottom_ret;                  /* at_bottom 의 반환값 (깊이와 섞지 않게 따로) */

static inline uint32_t read_sp(void) {
    uint32_t sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    return sp;
}

__attribute__((noinline)) uint32_t path_leaf(uint32_t x) {
    volatile uint32_t buf[16];                   /* 64 B */
    for (uint32_t i = 0; i < 16u; ++i) buf[i] = x + i;
    if (g_frames_mode & MODE_PEND_LOW) {
        if (g_frames_mode & MODE_MISALIGN)       /* SP%8 == 4 인 순간에 IRQ 가 걸리게: AAPCS 는 공개 경계에서만 8 B 정렬 */
            __asm__ volatile("sub sp, sp, #4\n\tstr %1, [%0]\n\tdsb\n\tisb\n\tadd sp, sp, #4"
                             :: "r"(&NVIC_ISPR0), "r"(1u << IRQ_LOW) : "memory");
        else {
            NVIC_ISPR0 = 1u << IRQ_LOW;
            __asm__ volatile("dsb\n\tisb" ::: "memory");   /* 여기서 바로 예외 진입 */
        }
    }
    uint32_t s = 0;
    for (uint32_t i = 0; i < 16u; ++i) s += buf[i];
    return s;
}

__attribute__((noinline)) uint32_t path_mid(uint32_t x) {
    volatile uint32_t buf[32];                   /* 128 B */
    for (uint32_t i = 0; i < 32u; ++i) buf[i] = x ^ i;
    return path_leaf(buf[31]) + buf[0];
}

__attribute__((noinline)) uint32_t path_top(uint32_t x) {
    volatile uint32_t buf[64];                   /* 256 B */
    for (uint32_t i = 0; i < 64u; ++i) buf[i] = x + 3u * i;
    return path_mid(buf[63]) + buf[0];
}

/* ---- ISR: naked 입구 (프레임 0) 가 예외 프레임 주소를 r0 로 넘기고 C 본문으로 "b" (LR = EXC_RETURN 유지) ---- */
__attribute__((used, noinline)) void irq_low_body(const uint32_t *frame) {
    volatile uint32_t buf[12];                   /* 48 B */
    for (uint32_t i = 0; i < 12u; ++i) buf[i] = i;
    g_xpsr_low = frame[7];
    g_isr_low_runs++;
    if (g_frames_mode & MODE_NEST_HIGH) {
        NVIC_ISPR0 = 1u << IRQ_HIGH;             /* 더 급한 IRQ → 여기서 선점 (중첩) */
        __asm__ volatile("dsb\n\tisb" ::: "memory");
    }
    (void)buf[0];
}
__attribute__((used, noinline)) void irq_high_body(const uint32_t *frame) {
    volatile uint32_t buf[20];                   /* 80 B */
    for (uint32_t i = 0; i < 20u; ++i) buf[i] = i;
    g_xpsr_high = frame[7];
    g_isr_high_runs++;
    (void)buf[0];
}
__attribute__((naked)) void Irq14_Handler(void) { __asm__ volatile("mov r0, sp\n\tb irq_low_body"); }
__attribute__((naked)) void Irq15_Handler(void) { __asm__ volatile("mov r0, sp\n\tb irq_high_body"); }

/* ---- 재귀: SP 가 stop_sp 이하가 될 때까지 내려가고, 맨 아래에서 at_bottom 을 부른다 ---- */
__attribute__((noinline)) uint32_t recurse_until(uint32_t stop_sp, uint32_t (*at_bottom)(uint32_t)) {
    volatile uint32_t pad[2];                    /* 프레임 맨 아래 워드까지 쓴다 → 넘친 만큼 정확히 덮어쓴다 */
    uint32_t sp = read_sp();
    for (uint32_t i = 0; i < 2u; ++i) pad[i] = sp ^ (0x5000u + i);
    uint32_t r;
    if (sp > stop_sp) r = recurse_until(stop_sp, at_bottom) + 1u;   /* 꼬리 호출이 안 되게 +1 */
    else { g_min_sp = sp; r = 0u; if (at_bottom) g_bottom_ret = at_bottom(sp); }
    return r + (pad[1] == (sp ^ 0x5001u) ? 0u : 0x10000u);
}

/* ---- 가드 건너뛰기: 256 B 를 예약하지만 맨 위 한 워드만 쓰고, 그 아래로 leaf 를 부른다 ---- */
__attribute__((noinline)) uint32_t big_sparse(uint32_t call_leaf) {
    volatile uint32_t big[64];                   /* 256 B 예약 */
    big[63] = call_leaf;                         /* 맨 위 한 워드만 */
    return call_leaf ? path_leaf(big[63]) + big[63] : big[63];   /* 호출 뒤에 big 을 읽음 → 꼬리 호출 금지 */
}

/* ---- -fstack-protector: 이 함수 하나만 (frames.c 는 -fstack-protector-explicit 로 컴파일) ---- */
__attribute__((noinline, stack_protect)) uint32_t ssp_copy(const uint8_t *src, uint32_t n) {
    uint8_t buf[16];
    for (uint32_t i = 0; i < n; ++i) buf[i] = src[i];       /* 버그: n > 16 이면 buf 뒤를 덮는다 */
    uint32_t s = 0;
    for (uint32_t i = 0; i < 16u; ++i) s += buf[i];
    return s;
}
