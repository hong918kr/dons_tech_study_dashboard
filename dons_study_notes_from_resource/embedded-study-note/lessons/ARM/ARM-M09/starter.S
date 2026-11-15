/* ENV: qemu */
/* ARM-M09 starter.S — Cortex-M3 (ARMv7-M) 예외 진입 스텁 + 테스트용 SVC 헬퍼, Thumb-2.
 * 하드웨어가 예외 진입 때 r0,r1,r2,r3,r12,lr,pc,xPSR 8 워드(FPU 사용 중이면 26 워드)를
 * "끊긴 쪽" 스택(MSP 또는 PSP)에 자동으로 쌓고, lr 에 EXC_RETURN(0xFFFFFFxx)을 넣는다.
 * 스텁의 일: 프레임이 어느 스택에 있는지 EXC_RETURN bit2 로 고르고, C 로 b (bl 아님!) 한다.
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

/* ======================= TODO: 예외 진입 스텁 ======================= */

// SVC_Handler: r0 = 프레임 주소 (MSP 또는 PSP), r1 = EXC_RETURN → svc_handler_c(frame, exc_return)
    .global SVC_Handler
    .type   SVC_Handler, %function
    .thumb_func
SVC_Handler:
    // TODO: EXC_RETURN(lr) bit2 로 MSP / PSP 고르기 (IT 블록)
    //       r0 = 프레임 주소
    //
    //
    //       r1 = EXC_RETURN
    bx      lr                  // TODO: svc_handler_c 로 b (지금은 아무것도 안 하고 바로 복귀)
    .size   SVC_Handler, . - SVC_Handler

// SysTick_Handler: 같은 모양 → systick_c(frame, exc_return)
    .global SysTick_Handler
    .type   SysTick_Handler, %function
    .thumb_func
SysTick_Handler:
    // TODO: SVC_Handler 와 같은 모양
    //
    //
    //
    //
    bx      lr                  // TODO: systick_c 로 b
    .size   SysTick_Handler, . - SysTick_Handler

/* ======================= 주어진 코드 (TODO 아님) ======================= */

// HardFault_Handler: 이 레슨의 HardFault 는 Part B 의 "승격(escalation)" 실험에서만 나온다.
// 그 두 경우 모두 끊긴 코드가 MSP 위였으므로 MSP 만 본다 (범용 판은 위와 같은 bit2 검사 — ARM-M12).
    .global HardFault_Handler
    .type   HardFault_Handler, %function
    .thumb_func
HardFault_Handler:
    mrs     r0, msp
    mov     r1, lr
    b       hardfault_c
    .size   HardFault_Handler, . - HardFault_Handler

// 시스템 콜 래퍼: 인자는 이미 r0..r3 에 있다 → svc → 핸들러가 스택된 r0 을 바꿔 두면 그게 반환값
    .global sys_add
    .type   sys_add, %function
    .thumb_func
sys_add:                        // uint32_t sys_add(uint32_t a, uint32_t b)
    svc     #0
    bx      lr
    .size   sys_add, . - sys_add

    .global sys_xor4
    .type   sys_xor4, %function
    .thumb_func
sys_xor4:                       // uint32_t sys_xor4(a, b, c, d)
    svc     #1
    bx      lr
    .size   sys_xor4, . - sys_xor4

    .global sys_ticks
    .type   sys_ticks, %function
    .thumb_func
sys_ticks:                      // uint32_t sys_ticks(void)
    svc     #2
    bx      lr
    .size   sys_ticks, . - sys_ticks

    .global sys_wait_tick
    .type   sys_wait_tick, %function
    .thumb_func
sys_wait_tick:                  // uint32_t sys_wait_tick(uint32_t spin_limit) — 핸들러 안에서 틱을 기다린다
    svc     #3
    bx      lr
    .size   sys_wait_tick, . - sys_wait_tick

    .global sys_bad
    .type   sys_bad, %function
    .thumb_func
sys_bad:                        // 없는 번호 → 0xFFFFFFFF (ENOSYS)
    svc     #0x42
    bx      lr
    .size   sys_bad, . - sys_bad

    .global sys_priv
    .type   sys_priv, %function
    .thumb_func
sys_priv:                       // 테스트 전용: 비특권 태스크를 특권으로 되돌린다 (harness 가 처리)
    svc     #0x7F
    bx      lr
    .size   sys_priv, . - sys_priv

// uint32_t svc_sentinel(uint32_t misalign)
//   r0..r3, r12, lr 에 알아보기 쉬운 값을 넣고 svc #1 (XOR4). misalign != 0 이면 sp 를 4 바이트
//   내려서 "4 mod 8" 인 sp 로 예외를 받는다 (STKALIGN 패딩 관찰). svc 직전 sp 를 g_sp_before_svc 에.
    .global svc_sentinel
    .type   svc_sentinel, %function
    .thumb_func
svc_sentinel:
    push    {r4, r5, r6, lr}    // 16 B → 8 정렬 유지
    mov     r6, r0              // misalign 플래그 보관 (r6 = callee-saved)
    cbz     r6, 1f
    sub     sp, sp, #4          // 일부러 sp ≡ 4 (mod 8)
1:  ldr     r4, =g_sp_before_svc
    mov     r5, sp
    str     r5, [r4]            // svc 직전 sp 기록
    ldr     r0, =0xA0A0A0A0
    ldr     r1, =0xA1A1A1A1
    ldr     r2, =0xA2A2A2A2
    ldr     r3, =0xA3A3A3A3
    ldr     r12, =0xC12C12C1
    ldr     lr, =0x1EE71EE7     // 스택된 LR 로 보일 값 (진짜 복귀 주소는 위에서 push 함)
    .global svc_sentinel_svc
svc_sentinel_svc:
    svc     #1                  // 인코딩 0xDF01 (상위 바이트 0xDF, 하위 바이트 = 번호)
    .global svc_sentinel_ret
svc_sentinel_ret:               // 스택된 PC 는 여기를 가리켜야 한다
    cbz     r6, 2f
    add     sp, sp, #4
2:  pop     {r4, r5, r6, pc}
    .size   svc_sentinel, . - svc_sentinel
    .ltorg

// uint32_t svc_under_primask(uint32_t a, uint32_t b) — PRIMASK=1 상태에서 svc → HardFault 로 승격
    .global svc_under_primask
    .type   svc_under_primask, %function
    .thumb_func
svc_under_primask:
    cpsid   i                   // 실행 우선순위 = 0 → SVCall(우선순위 0) 이 선점 못 한다
    svc     #0
    .global svc_primask_ret
svc_primask_ret:
    cpsie   i
    bx      lr
    .size   svc_under_primask, . - svc_under_primask

// void spin_until_change(volatile uint32_t *p, uint32_t old, uint32_t limit)
//   leaf, push 없음 → 루프 동안 sp 가 그대로 (g_sp_spin 에 기록). SysTick 이 이 루프를 끊는다.
    .global spin_until_change
    .type   spin_until_change, %function
    .thumb_func
spin_until_change:
    ldr     r3, =g_sp_spin
    mov     r12, sp
    str     r12, [r3]
    .global spin_loop_begin
spin_loop_begin:
    ldr     r3, [r0]            // *p 다시 읽기
    cmp     r3, r1
    bne     spin_loop_end       // 바뀌었으면 끝
    subs    r2, r2, #1
    bne     spin_loop_begin     // limit 소진 전까지
    .global spin_loop_end
spin_loop_end:
    bx      lr
    .size   spin_until_change, . - spin_until_change
    .ltorg

// 특수 레지스터 헬퍼 (ARM-B14 축약판)
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

    .global set_control
    .type   set_control, %function
    .thumb_func
set_control:
    msr     control, r0
    isb
    bx      lr
    .size   set_control, . - set_control

    .global get_ipsr
    .type   get_ipsr, %function
    .thumb_func
get_ipsr:
    mrs     r0, ipsr
    bx      lr
    .size   get_ipsr, . - get_ipsr

// uint32_t run_on_psp(uint32_t *psp_top, uint32_t (*fn)(uint32_t), uint32_t arg) — ARM-B14 그대로
    .global run_on_psp
    .type   run_on_psp, %function
    .thumb_func
run_on_psp:
    push    {r4, lr}
    msr     psp, r0
    mrs     r4, control
    orr     r4, r4, #2          // SPSEL = 1
    msr     control, r4
    isb
    mov     r0, r2
    blx     r1                  // fn(arg) 를 PSP 위에서
    mrs     r4, control
    bic     r4, r4, #2          // SPSEL = 0 (특권일 때만 먹힌다)
    msr     control, r4
    isb
    pop     {r4, pc}
    .size   run_on_psp, . - run_on_psp

#ifdef HAS_FPU
/* ---------------- Cortex-M4F 전용 (mps2-an386): FPU 프레임 실험 ---------------- */

// uint32_t fp_svc_probe(uint32_t mode)
//   mode 0: CONTROL.FPCA = 0 으로 지우고 svc #1 → 기본 8 워드 프레임이어야 한다
//   mode 1: sp 아래 112 B 를 0x5A5A5A5A 로 칠하고, s0..s15 = 1.0..16.0, FPSCR = 0x02C00000 → svc #1
//           (FP 를 썼으니 FPCA = 1 → 26 워드 확장 프레임). 반환: svc 뒤 s0 의 비트 (복원 확인용)
    .global fp_svc_probe
    .type   fp_svc_probe, %function
    .thumb_func
fp_svc_probe:
    push    {r4, r5, r6, lr}
    ldr     r4, =g_sp_before_svc
    cbnz    r0, 1f
    mrs     r5, control
    bic     r5, r5, #4          // FPCA = 0: "아직 FP 문맥 없음"
    msr     control, r5
    isb
    mov     r5, sp
    str     r5, [r4]
    svc     #1
    movs    r0, #0
    pop     {r4, r5, r6, pc}
1:  ldr     r1, =0x5A5A5A5A     // 프레임이 놓일 자리를 미리 칠한다 (lazy stacking 관찰)
    mov     r2, sp
    sub     r3, r2, #112
3:  str     r1, [r3], #4
    cmp     r3, r2
    bne     3b
    ldr     r1, =fp_pattern
    vldmia  r1, {s0-s15}        // 첫 FP 명령 → CONTROL.FPCA = 1
    ldr     r1, =0x02C00000     // FPSCR: DN=1, RMode=RZ (알아보기 쉬운 값)
    vmsr    fpscr, r1
    mov     r5, sp
    str     r5, [r4]
    svc     #1
    ldr     r1, =g_fp_after     // svc 뒤 s0..s15 를 저장 → 복원됐는지 C 가 확인
    vstmia  r1, {s0-s15}
    vmov    r0, s0
    pop     {r4, r5, r6, pc}
    .size   fp_svc_probe, . - fp_svc_probe
    .ltorg

// void touch_fpu(void) — 핸들러 안에서 FP 명령 하나 (lazy 저장을 일으키고 s0 을 망가뜨린다)
    .global touch_fpu
    .type   touch_fpu, %function
    .thumb_func
touch_fpu:
    ldr     r0, =0xDEADBEEF
    vmov    s0, r0
    bx      lr
    .size   touch_fpu, . - touch_fpu
    .ltorg

    .section .rodata
    .p2align 2
fp_pattern:                     // 1.0f .. 16.0f
    .word   0x3F800000, 0x40000000, 0x40400000, 0x40800000, 0x40A00000, 0x40C00000, 0x40E00000, 0x41000000
    .word   0x41100000, 0x41200000, 0x41300000, 0x41400000, 0x41500000, 0x41600000, 0x41700000, 0x41800000
#endif
