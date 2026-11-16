/* ENV: qemu */
/* ARM-A01 shared_tasks.S — 주어진 asm (TODO 없음): starter / solution / practice 모두에 링크된다.
 *  - os_start      : main 의 r4–r11 에 센티널을 넣고 svc #0. main 으로 돌아오면 바뀐 레지스터 비트마스크 반환.
 *  - task_regcheck : 레지스터 보존 검사 태스크. r0–r12, lr 에 이번 라운드 패턴을 넣고 한참 돈 뒤 전부 대조.
 *  - task_fpcheck  : 위 + s0–s31 (HAS_FPU, Cortex-M4F)
 *  - IRQ6_Handler  : "바쁜 IRQ" — r4–r11 패턴을 들고 오래 돈다 (PendSV 우선순위 실험, Part B)
 *  - task_exit_trap: 태스크 함수가 return 하면 오는 곳 (초기 프레임의 LR)
 *  - get_msp / get_psp / get_control / get_ipsr
 * 검사 창(window) 안에서는 모든 레지스터가 "알려진 값" 이라, 스위치가 그 안에 떨어지면 저장/복원이 진짜로 검증된다.
 * 창 밖의 C 호출(rc_prepare / rc_verify)은 cpsid i 로 감싼다 → 스위치는 asm 창 안(또는 경계)에만 떨어진다.
 */
    .syntax unified
#ifdef HAS_FPU
    .cpu    cortex-m4
    .fpu    fpv4-sp-d16
#else
    .cpu    cortex-m3
#endif
    .thumb
    .text

// uint32_t os_start(void)
//   r4–r11 = 0x44444444 … 0xBBBBBBBB → svc #0 (SVC_Handler 가 main 문맥을 MSP 에 저장하고 첫 태스크로)
//   → 태스크들이 돈다 → PendSV 가 main 문맥을 복원하며 svc 다음 명령으로 돌려보낸다
//   → r4–r11 이 그대로인지 검사, 바뀐 레지스터 n 마다 bit n 을 세운 마스크 반환 (0 = 정상)
    .global os_start
    .type   os_start, %function
    .thumb_func
os_start:
    push    {r3-r11, lr}        // 10 워드 = 40 B → sp 8 정렬 유지
    ldr     r4, =0x44444444
    ldr     r5, =0x55555555
    ldr     r6, =0x66666666
    ldr     r7, =0x77777777
    ldr     r8, =0x88888888
    ldr     r9, =0x99999999
    ldr     r10, =0xAAAAAAAA
    ldr     r11, =0xBBBBBBBB
    .global os_start_svc
os_start_svc:
    svc     #0
    .global os_start_ret
os_start_ret:                   // main 으로 돌아올 때 스택된 PC 가 가리키는 곳
    movs    r0, #0
    ldr     r1, =0x44444444
    cmp     r4, r1
    it      ne
    orrne   r0, r0, #(1 << 4)
    ldr     r1, =0x55555555
    cmp     r5, r1
    it      ne
    orrne   r0, r0, #(1 << 5)
    ldr     r1, =0x66666666
    cmp     r6, r1
    it      ne
    orrne   r0, r0, #(1 << 6)
    ldr     r1, =0x77777777
    cmp     r7, r1
    it      ne
    orrne   r0, r0, #(1 << 7)
    ldr     r1, =0x88888888
    cmp     r8, r1
    it      ne
    orrne   r0, r0, #(1 << 8)
    ldr     r1, =0x99999999
    cmp     r9, r1
    it      ne
    orrne   r0, r0, #(1 << 9)
    ldr     r1, =0xAAAAAAAA
    cmp     r10, r1
    it      ne
    orrne   r0, r0, #(1 << 10)
    ldr     r1, =0xBBBBBBBB
    cmp     r11, r1
    it      ne
    orrne   r0, r0, #(1 << 11)
    pop     {r3-r11, pc}
    .size   os_start, . - os_start
    .ltorg

// void task_regcheck(struct rc *st)  — Thread 모드, PSP. 돌아오지 않는다.
//   [sp] = st, [sp+4] = 스핀 카운터 (태스크 자기 스택 위)
    .global task_regcheck
    .type   task_regcheck, %function
    .thumb_func
task_regcheck:
    push    {r0, r1}
1:  cpsid   i                   // C 부분은 스위치 없이
    ldr     r0, [sp]
    bl      rc_prepare          // st->want[] 채움, r0 = 스핀 횟수
    str     r0, [sp, #4]
    ldr     r0, [sp]            // &st->want[0] (오프셋 0)
    cpsie   i                   // ---- 창 시작: 여기부터 SysTick/PendSV 가 끼어들 수 있다 ----
    ldmia   r0, {r0-r12, lr}    // 14 개 패턴 (베이스 r0 도 덮어씀 — writeback 없으니 허용)
    .global rc_spin
rc_spin:
2:  push    {r0}                // r0 패턴을 잠시 스택에
    ldr     r0, [sp, #8]        // 카운터
    subs    r0, r0, #1
    str     r0, [sp, #8]
    pop     {r0}                // r0 패턴 복귀 (pop 은 플래그를 안 바꾼다)
    bne     2b
    .global rc_spin_end
rc_spin_end:
    push    {r0-r12, lr}        // ---- 창 끝: 14 개를 그대로 스택에 (got[]) ----
    cpsid   i
    ldr     r0, [sp, #56]       // st
    mov     r1, sp              // got
    movs    r2, #0              // fgot = NULL
    bl      rc_verify
    add     sp, sp, #56
    cpsie   i                   // rc_verify 가 pend 한 yield/IRQ6 는 여기서 받는다
    b       1b
    .size   task_regcheck, . - task_regcheck

#ifdef HAS_FPU
// void task_fpcheck(struct rc *st) — 위와 같고 s0–s31 도 (fwant[] → s0..s31, 끝에서 vpush 로 fgot[])
    .global task_fpcheck
    .type   task_fpcheck, %function
    .thumb_func
task_fpcheck:
    push    {r0, r1}
1:  cpsid   i
    ldr     r0, [sp]
    bl      rc_prepare
    str     r0, [sp, #4]
    ldr     r0, [sp]
    add     r1, r0, #56         // &st->fwant[0]
    vldmia  r1, {s0-s31}        // FP 명령 → CONTROL.FPCA = 1 (이 태스크는 이제 "FP 문맥 있음")
    cpsie   i
    ldmia   r0, {r0-r12, lr}
2:  push    {r0}
    ldr     r0, [sp, #8]
    subs    r0, r0, #1
    str     r0, [sp, #8]
    pop     {r0}
    bne     2b
    vpush   {s0-s31}            // fgot[] (128 B)
    push    {r0-r12, lr}        // got[]  (56 B)
    cpsid   i
    ldr     r0, [sp, #184]      // st (56 + 128 위)
    mov     r1, sp
    add     r2, sp, #56
    bl      rc_verify
    add     sp, sp, #184
    cpsie   i
    b       1b
    .size   task_fpcheck, . - task_fpcheck
#endif

// IRQ6_Handler — 바쁜 IRQ: r4–r11 패턴을 들고 60000 번 돈다. 그동안 더 높은 SysTick 이 여러 번 끼어든다.
//   PendSV 가 최저 우선순위면 이 핸들러 안에서는 절대 스위치가 안 일어나야 한다.
    .global IRQ6_Handler
    .type   IRQ6_Handler, %function
    .thumb_func
IRQ6_Handler:
    push    {r3-r11, lr}        // AAPCS: r4–r11 은 callee-saved → 핸들러도 지켜야 한다 (10 워드, 8 정렬)
    sub     sp, sp, #8          // [sp] = 카운터
    bl      irq_prepare         // g_irq_want[0..7] 채움, r0 = 스핀 횟수
    str     r0, [sp]
    ldr     r0, =g_irq_want
    ldmia   r0, {r4-r11}
3:  ldr     r0, [sp]
    subs    r0, r0, #1
    str     r0, [sp]
    bne     3b
    push    {r4-r11}            // got[0..7]
    mov     r0, sp
    bl      irq_verify
    add     sp, sp, #40         // 32 (got) + 8 (카운터)
    pop     {r3-r11, pc}        // pc ← EXC_RETURN → 예외 복귀
    .size   IRQ6_Handler, . - IRQ6_Handler
    .ltorg

// task_exit_trap — 초기 프레임 LR 칸의 값. 태스크 함수의 반환값(r0)을 들고 C 로.
    .global task_exit_trap
    .type   task_exit_trap, %function
    .thumb_func
task_exit_trap:
    b       task_exit_c
    .size   task_exit_trap, . - task_exit_trap

    .global get_msp
    .type   get_msp, %function
    .thumb_func
get_msp:
    mrs     r0, msp
    bx      lr
    .size   get_msp, . - get_msp

    .global get_psp
    .type   get_psp, %function
    .thumb_func
get_psp:
    mrs     r0, psp
    bx      lr
    .size   get_psp, . - get_psp

    .global get_control
    .type   get_control, %function
    .thumb_func
get_control:
    mrs     r0, control
    bx      lr
    .size   get_control, . - get_control

    .global set_basepri
    .type   set_basepri, %function
    .thumb_func
set_basepri:
    msr     basepri, r0
    isb
    bx      lr
    .size   set_basepri, . - set_basepri

    .global get_ipsr
    .type   get_ipsr, %function
    .thumb_func
get_ipsr:
    mrs     r0, ipsr
    bx      lr
    .size   get_ipsr, . - get_ipsr
