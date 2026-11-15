/* ENV: qemu */
/* ARM-B14 starter.S — Cortex-M3 (ARMv7-M) 특수 레지스터 헬퍼, Thumb-2.
 * C 에서 부르는 함수들: AAPCS (r0..r3 인자, r0 반환, r4..r11 callee-saved).
 * 특수 레지스터는 일반 ldr/str 로 못 만진다 → MRS (특수→범용) / MSR (범용→특수) 만 가능.
 */
    .syntax unified
    .cpu    cortex-m3
    .thumb
    .text

/* ---------------- 이미 주어진 것 (TODO 아님) ---------------- */

// uint32_t get_sp(void) — 지금 이 순간 쓰고 있는 SP (MSP 든 PSP 든)
    .global get_sp
    .type   get_sp, %function
    .thumb_func
get_sp:
    mov     r0, sp              // leaf 라 push 없음 → 호출한 쪽의 sp 와 같다
    bx      lr
    .size   get_sp, . - get_sp

// void svc_call(uint32_t cmd) — 명령 번호를 전역에 두고 SVC 예외를 일으킨다
    .global svc_call
    .type   svc_call, %function
    .thumb_func
svc_call:
    ldr     r1, =g_svc_cmd
    str     r0, [r1]            // g_svc_cmd = cmd (핸들러가 읽는다)
    ldr     r1, =g_sp_before_svc
    mov     r2, sp
    str     r2, [r1]            // svc 직전 sp 기록 (핸들러 안 PSP 와 비교용)
    svc     #0                  // → SVCall 예외 (번호 11)
    bx      lr
    .size   svc_call, . - svc_call
    .ltorg

/* ---------------- Part A: MRS / MSR 헬퍼 ---------------- */

// uint32_t get_msp(void)
    .global get_msp
    .type   get_msp, %function
    .thumb_func
get_msp:
    movs    r0, #0              // TODO: r0 = SP_main (MRS)
    bx      lr
    .size   get_msp, . - get_msp

// uint32_t get_psp(void)
    .global get_psp
    .type   get_psp, %function
    .thumb_func
get_psp:
    movs    r0, #0              // TODO: r0 = SP_process (MRS)
    bx      lr
    .size   get_psp, . - get_psp

// void set_psp(uint32_t top)
    .global set_psp
    .type   set_psp, %function
    .thumb_func
set_psp:
    nop                         // TODO: SP_process = r0 (MSR)
    bx      lr
    .size   set_psp, . - set_psp

// uint32_t get_control(void) — bit0 nPRIV, bit1 SPSEL
    .global get_control
    .type   get_control, %function
    .thumb_func
get_control:
    movs    r0, #0              // TODO: r0 = CONTROL
    bx      lr
    .size   get_control, . - get_control

// void set_control(uint32_t v) — nPRIV 용. SPSEL 을 C 함수 한가운데서 바꾸면 sp 가 바뀐다!
    .global set_control
    .type   set_control, %function
    .thumb_func
set_control:
    nop                         // TODO: CONTROL = r0 (MSR)
    nop                         // TODO: 배리어 (왜 필요한가?)
    bx      lr
    .size   set_control, . - set_control

// uint32_t get_xpsr(void) — APSR | IPSR | EPSR 합친 보기 (EPSR 비트는 MRS 로 0 으로 읽힌다)
    .global get_xpsr
    .type   get_xpsr, %function
    .thumb_func
get_xpsr:
    movs    r0, #0              // TODO: r0 = xPSR
    bx      lr
    .size   get_xpsr, . - get_xpsr

// uint32_t get_ipsr(void) — 지금 처리 중인 예외 번호 (Thread = 0)
    .global get_ipsr
    .type   get_ipsr, %function
    .thumb_func
get_ipsr:
    movs    r0, #0              // TODO: r0 = IPSR
    bx      lr
    .size   get_ipsr, . - get_ipsr

// uint32_t get_primask(void) — 읽기만 (크리티컬 섹션은 ARM-M10)
    .global get_primask
    .type   get_primask, %function
    .thumb_func
get_primask:
    movs    r0, #0              // TODO: r0 = PRIMASK
    bx      lr
    .size   get_primask, . - get_primask

/* ---------------- Part B: Thread 모드를 PSP 로 ---------------- */

// uint32_t run_on_psp(uint32_t *psp_top, uint32_t (*fn)(uint32_t), uint32_t arg)
//   PSP = psp_top, CONTROL.SPSEL = 1 → fn(arg) 를 PSP 위에서 실행 → SPSEL = 0 → fn 의 반환값
    .global run_on_psp
    .type   run_on_psp, %function
    .thumb_func
run_on_psp:
    push    {r4, lr}            // MSP 에 저장 (8 바이트 → 8 정렬 유지)
    // TODO: PSP = r0
    // TODO: CONTROL.SPSEL(bit1) = 1  (r4 로 읽고-고치고-쓰기)
    // TODO: 배리어
    .global psp_switched
psp_switched:                   // gdb 가 여기 멈춰 $sp == $psp 를 본다
    mov     r0, r2              // fn 의 인자
    blx     r1                  // fn(arg) — r1 의 bit0 = 1 (Thumb) 이어야 한다
    // TODO: CONTROL.SPSEL = 0 → 다시 MSP, 배리어
    pop     {r4, pc}            // MSP 에서 꺼낸다 → 호출자로 복귀
    .size   run_on_psp, . - run_on_psp

/* ---------------- Part C: 예외 핸들러 트램펄린 ---------------- */

// void exc_observe(uint32_t exc_return, uint32_t sp, uint32_t msp, uint32_t psp) — harness.c
// b (bl 아님) 로 넘기므로 lr = EXC_RETURN 이 그대로 → exc_observe 의 return 이 곧 예외 복귀.
    .global SVC_Handler
    .type   SVC_Handler, %function
    .thumb_func
SVC_Handler:
    // TODO: r0 = EXC_RETURN, r1 = sp, r2 = MSP, r3 = PSP 를 담고 exc_observe 로 (bl 아님!)
    bx      lr                  // 지금은 아무것도 안 하고 예외 복귀
    .size   SVC_Handler, . - SVC_Handler

    .global PendSV_Handler
    .type   PendSV_Handler, %function
    .thumb_func
PendSV_Handler:
    // TODO: SVC_Handler 와 같은 트램펄린
    bx      lr
    .size   PendSV_Handler, . - PendSV_Handler
