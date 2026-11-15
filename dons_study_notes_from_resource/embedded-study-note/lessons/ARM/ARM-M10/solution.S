/* ENV: qemu */
/* ARM-M10 solution.S — PRIMASK / BASEPRI 크리티컬 섹션 프리미티브 (Cortex-M3, Thumb-2).
 * AAPCS: 인자 r0..r1, 반환 r0. 모두 leaf 함수 (스택 안 씀).
 * 빌드: run_qemu.sh solution  →  harness.c + solution.S + shared_rmw.S + shared_startup.c
 */
    .syntax unified
    .cpu    cortex-m3
    .thumb
    .text

// uint32_t crit_enter(void) — 옛 PRIMASK 를 돌려주고 인터럽트를 끈다 (중첩 가능)
    .global crit_enter
    .type   crit_enter, %function
    .thumb_func
crit_enter:
    mrs     r0, primask         // 옛 상태 (0 = 켜짐, 1 = 꺼짐)
    cpsid   i                   // PRIMASK = 1
    bx      lr
    .size   crit_enter, . - crit_enter

// void crit_exit(uint32_t saved) — 들어올 때 상태로 "복원" (무조건 켜지 않는다)
    .global crit_exit
    .type   crit_exit, %function
    .thumb_func
crit_exit:
    msr     primask, r0         // saved = 1 이면 계속 꺼진 채, 0 이면 켜짐
    bx      lr
    .size   crit_exit, . - crit_exit

// uint32_t basepri_get(void)
    .global basepri_get
    .type   basepri_get, %function
    .thumb_func
basepri_get:
    mrs     r0, basepri
    bx      lr
    .size   basepri_get, . - basepri_get

// void basepri_set(uint32_t v) — 무조건 쓰기 (낮출 수도 있다: 0 = 마스크 없음)
    .global basepri_set
    .type   basepri_set, %function
    .thumb_func
basepri_set:
    msr     basepri, r0
    bx      lr
    .size   basepri_set, . - basepri_set

// void basepri_raise(uint32_t v) — BASEPRI_MAX: 더 급한 쪽으로만 바뀐다 (v=0 은 무시)
    .global basepri_raise
    .type   basepri_raise, %function
    .thumb_func
basepri_raise:
    msr     basepri_max, r0
    bx      lr
    .size   basepri_raise, . - basepri_raise

// uint32_t basepri_enter(uint32_t level) — 옛 BASEPRI 반환 + level 까지 "올리기만"
    .global basepri_enter
    .type   basepri_enter, %function
    .thumb_func
basepri_enter:
    mrs     r1, basepri         // 옛 값
    msr     basepri_max, r0     // 조건부 상승 (이미 더 급하면 그대로)
    mov     r0, r1
    bx      lr
    .size   basepri_enter, . - basepri_enter

// void basepri_exit(uint32_t old)
    .global basepri_exit
    .type   basepri_exit, %function
    .thumb_func
basepri_exit:
    msr     basepri, r0         // 정확히 들어오기 전 값으로
    bx      lr
    .size   basepri_exit, . - basepri_exit

// uint32_t prio_encode(uint32_t logical, uint32_t bits) — CMSIS 식: (logical << (8-bits)) & 0xFF
    .global prio_encode
    .type   prio_encode, %function
    .thumb_func
prio_encode:
    rsb     r1, r1, #8          // shift = 8 - bits
    lsls    r0, r0, r1          // 구현된 비트는 바이트의 위쪽에 있다
    uxtb    r0, r0              // 넘친 비트는 버린다 (CMSIS 와 같음 — 7. 실수 참고)
    bx      lr
    .size   prio_encode, . - prio_encode
