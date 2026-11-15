/* ENV: qemu */
/* ARM-M10 starter.S — PRIMASK / BASEPRI 크리티컬 섹션 프리미티브 (Cortex-M3, Thumb-2).
 * AAPCS: 인자 r0..r1, 반환 r0. 모두 leaf 함수 (스택 안 씀).
 * 빌드: run_qemu.sh starter   →  harness.c + starter.S + shared_rmw.S + shared_startup.c
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
    movs    r0, #0              // TODO: mrs  — 옛 PRIMASK 읽기
    nop                         // TODO: 인터럽트 끄기
    bx      lr
    .size   crit_enter, . - crit_enter

// void crit_exit(uint32_t saved) — 들어올 때 상태로 "복원" (무조건 켜지 않는다)
    .global crit_exit
    .type   crit_exit, %function
    .thumb_func
crit_exit:
    nop                         // TODO: 저장값으로 PRIMASK 복원
    bx      lr
    .size   crit_exit, . - crit_exit

// uint32_t basepri_get(void)
    .global basepri_get
    .type   basepri_get, %function
    .thumb_func
basepri_get:
    movs    r0, #0              // TODO: BASEPRI 읽기
    bx      lr
    .size   basepri_get, . - basepri_get

// void basepri_set(uint32_t v) — 무조건 쓰기 (낮출 수도 있다: 0 = 마스크 없음)
    .global basepri_set
    .type   basepri_set, %function
    .thumb_func
basepri_set:
    nop                         // TODO: BASEPRI 쓰기
    bx      lr
    .size   basepri_set, . - basepri_set

// void basepri_raise(uint32_t v) — BASEPRI_MAX: 더 급한 쪽으로만 바뀐다 (v=0 은 무시)
    .global basepri_raise
    .type   basepri_raise, %function
    .thumb_func
basepri_raise:
    nop                         // TODO: BASEPRI_MAX 쓰기
    bx      lr
    .size   basepri_raise, . - basepri_raise

// uint32_t basepri_enter(uint32_t level) — 옛 BASEPRI 반환 + level 까지 "올리기만"
    .global basepri_enter
    .type   basepri_enter, %function
    .thumb_func
basepri_enter:
    movs    r1, #0              // TODO: 옛 BASEPRI
    nop                         // TODO: 조건부 상승
    mov     r0, r1
    bx      lr
    .size   basepri_enter, . - basepri_enter

// void basepri_exit(uint32_t old)
    .global basepri_exit
    .type   basepri_exit, %function
    .thumb_func
basepri_exit:
    nop                         // TODO: 복원
    bx      lr
    .size   basepri_exit, . - basepri_exit

// uint32_t prio_encode(uint32_t logical, uint32_t bits) — CMSIS 식: (logical << (8-bits)) & 0xFF
    .global prio_encode
    .type   prio_encode, %function
    .thumb_func
prio_encode:
    movs    r1, #0              // TODO: shift = 8 - bits
    nop                         // TODO: 왼쪽으로 shift
    nop                         // TODO: 0xFF 로 자르기
    bx      lr
    .size   prio_encode, . - prio_encode
