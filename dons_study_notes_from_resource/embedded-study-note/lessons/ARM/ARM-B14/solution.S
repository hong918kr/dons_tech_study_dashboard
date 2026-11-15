/* ENV: qemu */
/* ARM-B14 solution.S — Cortex-M3 (ARMv7-M) 특수 레지스터 헬퍼, Thumb-2.
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
    mrs     r0, msp             // r0 = SP_main
    bx      lr
    .size   get_msp, . - get_msp

// uint32_t get_psp(void)
    .global get_psp
    .type   get_psp, %function
    .thumb_func
get_psp:
    mrs     r0, psp             // r0 = SP_process
    bx      lr
    .size   get_psp, . - get_psp

// void set_psp(uint32_t top)
    .global set_psp
    .type   set_psp, %function
    .thumb_func
set_psp:
    msr     psp, r0             // SP_process = r0 (Thread 가 MSP 를 쓰는 중이면 sp 는 그대로)
    bx      lr
    .size   set_psp, . - set_psp

// uint32_t get_control(void) — bit0 nPRIV, bit1 SPSEL
    .global get_control
    .type   get_control, %function
    .thumb_func
get_control:
    mrs     r0, control
    bx      lr
    .size   get_control, . - get_control

// void set_control(uint32_t v) — nPRIV 용. SPSEL 을 C 함수 한가운데서 바꾸면 sp 가 바뀐다!
    .global set_control
    .type   set_control, %function
    .thumb_func
set_control:
    msr     control, r0         // 비특권(Thread, nPRIV=1)이면 무시된다
    isb                         // 다음 명령부터 새 CONTROL 값이 보이게 (파이프라인 비우기)
    bx      lr
    .size   set_control, . - set_control

// uint32_t get_xpsr(void) — APSR | IPSR | EPSR 합친 보기 (EPSR 비트는 MRS 로 0 으로 읽힌다)
    .global get_xpsr
    .type   get_xpsr, %function
    .thumb_func
get_xpsr:
    mrs     r0, xpsr
    bx      lr
    .size   get_xpsr, . - get_xpsr

// uint32_t get_ipsr(void) — 지금 처리 중인 예외 번호 (Thread = 0)
    .global get_ipsr
    .type   get_ipsr, %function
    .thumb_func
get_ipsr:
    mrs     r0, ipsr
    bx      lr
    .size   get_ipsr, . - get_ipsr

// uint32_t get_primask(void) — 읽기만 (크리티컬 섹션은 ARM-M10)
    .global get_primask
    .type   get_primask, %function
    .thumb_func
get_primask:
    mrs     r0, primask
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
    msr     psp, r0             // PSP = 새 스택 꼭대기 (8 정렬은 호출자 책임)
    mrs     r4, control
    orr     r4, r4, #2          // SPSEL = 1
    msr     control, r4         // 이 순간부터 sp 는 PSP
#ifndef NO_ISB
    isb                         // 뒤 명령이 옛 SP(MSP)로 실행되지 않게
#endif
    .global psp_switched
psp_switched:                   // gdb 가 여기 멈춰 $sp == $psp 를 본다
    mov     r0, r2              // fn 의 인자
    blx     r1                  // fn(arg) — r1 의 bit0 = 1 (Thumb) 이어야 한다
    mrs     r4, control         // fn 이 nPRIV 를 바꿨을 수도 있으니 다시 읽는다
    bic     r4, r4, #2          // SPSEL = 0
    msr     control, r4         // 다시 MSP (특권일 때만 먹힌다!)
    isb
    pop     {r4, pc}            // MSP 에서 꺼낸다 → 호출자로 복귀
    .size   run_on_psp, . - run_on_psp

/* ---------------- Part C: 예외 핸들러 트램펄린 ---------------- */

// void exc_observe(uint32_t exc_return, uint32_t sp, uint32_t msp, uint32_t psp) — harness.c
// b (bl 아님) 로 넘기므로 lr = EXC_RETURN 이 그대로 → exc_observe 의 return 이 곧 예외 복귀.
    .global SVC_Handler
    .type   SVC_Handler, %function
    .thumb_func
SVC_Handler:
    mov     r0, lr              // EXC_RETURN (0xFFFFFFxx)
    mov     r1, sp              // 핸들러 안의 sp
    mrs     r2, msp
    mrs     r3, psp
    b       exc_observe
    .size   SVC_Handler, . - SVC_Handler

    .global PendSV_Handler
    .type   PendSV_Handler, %function
    .thumb_func
PendSV_Handler:
    mov     r0, lr
    mov     r1, sp
    mrs     r2, msp
    mrs     r3, psp
    b       exc_observe
    .size   PendSV_Handler, . - PendSV_Handler
