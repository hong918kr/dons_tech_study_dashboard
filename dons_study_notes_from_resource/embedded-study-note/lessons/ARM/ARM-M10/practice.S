/* ENV: qemu */
/* ARM-M10 practice.S — 추가 연습 답 (Cortex-M3, Thumb-2).
 * 빌드: run_qemu.sh practice → practice.c + practice.S + shared_rmw.S + shared_startup.c
 * 본문 프리미티브(crit_enter/exit, basepri_enter/exit)도 여기에 다시 둔다 (변형마다 자기 .S 하나).
 */
    .syntax unified
    .cpu    cortex-m3
    .thumb
    .text

    .macro FUNC name
    .global \name
    .type   \name, %function
    .thumb_func
\name:
    .endm

FUNC crit_enter
    mrs     r0, primask
    cpsid   i
    bx      lr
    .size   crit_enter, . - crit_enter

FUNC crit_exit
    msr     primask, r0
    bx      lr
    .size   crit_exit, . - crit_exit

FUNC basepri_enter
    mrs     r1, basepri
    msr     basepri_max, r0
    mov     r0, r1
    bx      lr
    .size   basepri_enter, . - basepri_enter

FUNC basepri_exit
    msr     basepri, r0
    bx      lr
    .size   basepri_exit, . - basepri_exit

// 연습 1: uint32_t prio_bits_probe(volatile uint8_t *ipr)
//   0xFF 를 써 보고 다시 읽어 위쪽 1 의 개수 = 구현된 우선순위 비트 수. 원래 값은 복원.
FUNC prio_bits_probe
    ldrb    r2, [r0]            // 원래 값 보관
    movs    r1, #0xFF
    strb    r1, [r0]            // 구현 안 된 비트는 RAZ/WI → 0 으로 남는다
    ldrb    r1, [r0]            // 예: 3비트 칩이면 0xE0, QEMU 는 0xFF
    strb    r2, [r0]            // 복원
    mvn     r1, r1, lsl #24     // ~(v << 24): 위쪽 1 들이 위쪽 0 들로
    clz     r0, r1              // 위쪽 0 의 개수 = 구현 비트 수
    bx      lr
    .size   prio_bits_probe, . - prio_bits_probe

// 연습 2: uint32_t fault_enter(void) / void fault_exit(uint32_t) — FAULTMASK (실행 우선순위 -1)
FUNC fault_enter
    mrs     r0, faultmask
    cpsid   f                   // NMI 말고는 전부 (HardFault 까지) 막는다
    bx      lr
    .size   fault_enter, . - fault_enter

FUNC fault_exit
    msr     faultmask, r0
    bx      lr
    .size   fault_exit, . - fault_exit

// 연습 3: void svc_call(void) — SVC #7. PRIMASK=1 이면 받을 수 없어 HardFault 로 승격
    .global svc_insn
FUNC svc_call
svc_insn:
    svc     #7
    bx      lr
    .size   svc_call, . - svc_call
