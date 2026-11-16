/* ENV: qemu */
/* ARM-A01 practice.S — 추가 연습 답 (Cortex-M3, mps2-an385).
 *  SVC_Handler / PendSV_Handler : solution.S 의 M3 판 + 연습 3 (태스크를 비특권으로: CONTROL.nPRIV)
 *  os_start_direct              : 연습 1 — SVC 없이 첫 태스크 시작 (Thread 모드에서 PSP 로 바꾸고 프레임을 손으로 pop)
 *  task_unpriv_probe            : 연습 3 — 비특권 태스크에서 CONTROL / cpsid 효과 관찰
 */
    .syntax unified
    .cpu    cortex-m3
    .thumb
    .text
    .equ    TCB_SP, 0

// ---- SVC_Handler: solution 과 같고, 복귀 직전 CONTROL.nPRIV = g_task_npriv ----
    .global SVC_Handler
    .type   SVC_Handler, %function
    .thumb_func
SVC_Handler:
    push    {r3-r11, lr}
    ldr     r0, =g_main_msp
    mov     r1, sp
    str     r1, [r0]
    ldr     r0, =g_cur
    ldr     r0, [r0]
    ldr     r1, [r0, #TCB_SP]
    ldmia   r1!, {r4-r11}
    msr     psp, r1
    ldr     r2, =g_task_npriv   // 연습 3: 1 이면 태스크는 비특권 Thread 로 돈다
    ldr     r2, [r2]
    msr     control, r2         // Handler 모드는 항상 특권 → 여기서 nPRIV 를 바꿔도 지금은 영향 없음
    movs    r2, #0
    msr     basepri, r2
    isb
    ldr     lr, =0xFFFFFFFD
    bx      lr
    .size   SVC_Handler, . - SVC_Handler
    .ltorg

// ---- PendSV_Handler: solution 과 같고, main 으로 돌아갈 때 CONTROL = 0 (특권 + MSP) ----
    .global PendSV_Handler
    .type   PendSV_Handler, %function
    .thumb_func
PendSV_Handler:
    mrs     r0, psp
    stmdb   r0!, {r4-r11}
    ldr     r3, =g_cur
    ldr     r2, [r3]
    str     r0, [r2, #TCB_SP]
    mov     r0, lr
    push    {r3, lr}
    bl      os_schedule
    pop     {r3, lr}
    cbz     r0, 1f
    ldr     r1, [r0, #TCB_SP]
    ldmia   r1!, {r4-r11}
    msr     psp, r1
    isb
    bx      lr
1:  movs    r0, #0
    msr     control, r0         // 연습 3: main 은 다시 특권 (nPRIV = 0)
    isb
    ldr     r0, =g_main_msp
    ldr     r0, [r0]
    mov     sp, r0
    pop     {r3-r11, lr}
    bx      lr
    .size   PendSV_Handler, . - PendSV_Handler
    .ltorg

// ---- 연습 1: uint32_t os_start_direct(void) — SVC 없이 시작 ----
//   (1) main 이 나중에 "예외 복귀" 로 돌아올 수 있게 MSP 에 가짜 8 워드 프레임(PC = 복귀 라벨, xPSR = T) 을 만들고
//       그 아래에 PendSV 의 main 복귀 경로가 기대하는 {r3-r11, lr=0xFFFFFFF9} 를 쌓는다 → g_main_msp
//   (2) PSP = 첫 태스크 sp, CONTROL.SPSEL = 1, isb → 이제 Thread 의 sp 는 PSP
//   (3) 태스크 프레임을 손으로 푼다: r4–r11, r0(arg), lr(exit trap), pc. xPSR 칸은 버린다.
    .global os_start_direct
    .type   os_start_direct, %function
    .thumb_func
os_start_direct:
    push    {r3-r11, lr}        // 진짜 호출자(C)에게 돌아갈 때 쓸 레지스터
    ldr     r4, =0x44444444     // os_start 와 같은 센티널
    ldr     r5, =0x55555555
    ldr     r6, =0x66666666
    ldr     r7, =0x77777777
    ldr     r8, =0x88888888
    ldr     r9, =0x99999999
    ldr     r10, =0xAAAAAAAA
    ldr     r11, =0xBBBBBBBB
    cpsid   i                   // 프레임을 꾸미는 동안 SysTick 금지
    ldr     r0, =0x01000000     // (1) 가짜 하드웨어 프레임: xPSR
    adr     r1, 2f              //     PC = 아래 라벨 (adr 은 Thumb 비트 없는 주소)
    sub     sp, sp, #32
    str     r1, [sp, #24]
    str     r0, [sp, #28]
    ldr     lr, =0xFFFFFFF9     //     main 의 EXC_RETURN (Thread, MSP, 기본 프레임). 진짜 lr 은 맨 위에서 push 했다
    push    {r3-r11, lr}        //     SVC_Handler 가 쌓는 것과 똑같은 모양
    ldr     r0, =g_main_msp
    mov     r1, sp
    str     r1, [r0]
    ldr     r0, =g_cur          // (2) PSP 로 전환
    ldr     r0, [r0]
    ldr     r1, [r0, #TCB_SP]
    msr     psp, r1
    movs    r2, #2              //     CONTROL.SPSEL = 1 (특권 유지)
    msr     control, r2
    isb                         //     이후 명령이 새 sp 를 쓰도록
    ldmia   sp!, {r4-r11}       // (3) 소프트웨어 부분
    ldr     r0, [sp, #0]        //     r0 = arg
    ldr     lr, [sp, #20]       //     lr = exit trap
    ldr     r3, [sp, #24]       //     pc (bit0 = 0 으로 저장돼 있다)
    add     sp, sp, #32         //     xPSR 칸까지 버림 → PSP = 스택 꼭대기
    orr     r3, r3, #1          //     bx 에는 Thumb 비트가 필요
    movs    r2, #0
    msr     basepri, r2         //     main 이 올려 둔 BASEPRI 해제
    cpsie   i
    bx      r3
2:  // ---- PendSV 의 main 복귀 경로가 예외 복귀로 여기 도착 (MSP, CONTROL = 0) ----
    movs    r0, #0
    ldr     r1, =0x44444444
    cmp     r4, r1
    it      ne
    orrne   r0, r0, #(1 << 4)
    ldr     r1, =0xBBBBBBBB
    cmp     r11, r1
    it      ne
    orrne   r0, r0, #(1 << 11)
    pop     {r3-r11, pc}
    .size   os_start_direct, . - os_start_direct
    .ltorg

// ---- 연습 3: void task_unpriv_probe(volatile uint32_t out[3]) ----
//   out[0] = CONTROL, out[1] = cpsid i 직후 PRIMASK (비특권이면 cpsid 는 무시 → 0), out[2] = 루프 카운터
    .global task_unpriv_probe
    .type   task_unpriv_probe, %function
    .thumb_func
task_unpriv_probe:
    mrs     r1, control
    str     r1, [r0, #0]
    cpsid   i                   // 비특권에서는 NOP 처럼 무시된다 (ARMv7-M)
    mrs     r1, primask
    str     r1, [r0, #4]
    cpsie   i
3:  ldr     r1, [r0, #8]
    adds    r1, r1, #1
    str     r1, [r0, #8]
    b       3b
    .size   task_unpriv_probe, . - task_unpriv_probe
