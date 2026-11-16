/* ENV: qemu */
/* ARM-A01 demo_bugs.S — Part B 결함 주입용 "잘못된 PendSV" 들 (run_qemu.sh bugs 에서만 링크).
 * harness.c (-DBUGS) 가 RAM 벡터 테이블의 [14] 를 이 함수로 바꿔 끼운다 (VTOR).
 * 각 함수는 solution.S 의 PendSV_Handler 와 딱 한 군데만 다르다 (// BUG 표시).
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
    .equ    TCB_SP, 0
    .equ    TCB_EXCRET, 8

#ifndef HAS_FPU
// HardFault 진입 스텁 (BUGSEL 2, 3 이 HardFault 로 끝난다): MSP, EXC_RETURN, PSP 를 C 로 (ARM-M09 스텁 변형)
    .global HardFault_Handler
    .type   HardFault_Handler, %function
    .thumb_func
HardFault_Handler:
    mrs     r0, msp             // 진입 순간의 MSP, PSP 둘 다 넘긴다 (어느 쪽에 프레임이 있는지가 관찰 대상)
    mov     r1, lr
    mrs     r2, psp
    b       hardfault_c
    .size   HardFault_Handler, . - HardFault_Handler

// BUG1a 용 SVC: 첫 태스크 진입도 {r4-r10} (init 프레임을 15 워드로 줄인 경우와 짝)
    .global SVC_bug_no_r11
    .type   SVC_bug_no_r11, %function
    .thumb_func
SVC_bug_no_r11:
    push    {r3-r10, lr}        // BUG: main 의 r11 도 안 쌓음 (9 워드 → 8 정렬 깨짐 → r3 하나 더)
    sub     sp, sp, #4
    ldr     r0, =g_main_msp
    mov     r1, sp
    str     r1, [r0]
    ldr     r0, =g_cur
    ldr     r0, [r0]
    ldr     r1, [r0, #TCB_SP]
    ldmia   r1!, {r4-r10}       // BUG: r11 빠짐
    msr     psp, r1
    movs    r2, #0
    msr     basepri, r2
    isb
    ldr     lr, =0xFFFFFFFD
    bx      lr
    .size   SVC_bug_no_r11, . - SVC_bug_no_r11
    .ltorg

// BUG1: PendSV 저장/복원 목록에서 r11 을 빼먹음 ({r4-r10}).
//   1a: SVC·init 프레임도 7 칸이면 모양은 일관적 → r11 만 태스크끼리 샌다 (조용한 손상)
//   1b: init 프레임·SVC 는 8 칸 그대로면 → PendSV 로 처음 들어가는 태스크의 프레임이 한 칸 어긋난다
    .global PendSV_bug_no_r11
    .type   PendSV_bug_no_r11, %function
    .thumb_func
PendSV_bug_no_r11:
    mrs     r0, psp
    stmdb   r0!, {r4-r10}       // BUG: r11 빠짐
    ldr     r3, =g_cur
    ldr     r2, [r3]
    str     r0, [r2, #TCB_SP]
    mov     r0, lr
    push    {r3, lr}
    bl      os_schedule
    pop     {r3, lr}
    cbz     r0, 1f
    ldr     r1, [r0, #TCB_SP]
    ldmia   r1!, {r4-r10}       // BUG: r11 빠짐
    msr     psp, r1
    isb
    bx      lr
1:  ldr     r0, =g_main_msp
    ldr     r0, [r0]
    mov     sp, r0
    add     sp, sp, #4          // SVC_bug_no_r11 의 패딩
    pop     {r3-r10, lr}        // BUG: main 의 r11 도 복원 안 함
    bx      lr
    .size   PendSV_bug_no_r11, . - PendSV_bug_no_r11
    .ltorg

// BUG3: msr psp 뒤 isb 생략
    .global PendSV_bug_no_isb
    .type   PendSV_bug_no_isb, %function
    .thumb_func
PendSV_bug_no_isb:
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
    msr     psp, r1             // BUG: 다음 줄 isb 없음
    bx      lr
1:  ldr     r0, =g_main_msp
    ldr     r0, [r0]
    mov     sp, r0
    pop     {r3-r11, lr}
    bx      lr
    .size   PendSV_bug_no_isb, . - PendSV_bug_no_isb
    .ltorg
#else
// BUG4 (M4F): EXC_RETURN bit4 를 무시하고 s16–s31 을 안 저장 (M3 용 스위처를 M4F 에 그대로 쓴 꼴).
//   EXC_RETURN 은 TCB 에 저장/복원 → 프레임 모양(26 워드 하드웨어 프레임)은 맞다. s16–s31 만 공유된다.
    .global PendSV_bug_no_fp
    .type   PendSV_bug_no_fp, %function
    .thumb_func
PendSV_bug_no_fp:
    mrs     r0, psp
    stmdb   r0!, {r4-r11}       // BUG: 앞의 vstmdbeq {s16-s31} 없음
    ldr     r3, =g_cur
    ldr     r2, [r3]
    str     r0, [r2, #TCB_SP]
    str     lr, [r2, #TCB_EXCRET]
    mov     r0, lr
    push    {r3, lr}
    bl      os_schedule
    pop     {r3, lr}
    cbz     r0, 1f
    ldr     lr, [r0, #TCB_EXCRET]
    ldr     r1, [r0, #TCB_SP]
    ldmia   r1!, {r4-r11}       // BUG: 뒤의 vldmiaeq {s16-s31} 없음
    msr     psp, r1
    isb
    bx      lr
1:  ldr     r0, =g_main_msp
    ldr     r0, [r0]
    mov     sp, r0
    pop     {r3-r11, lr}
    tst     lr, #0x10
    it      eq
    vpopeq  {s16-s31}
    bx      lr
    .size   PendSV_bug_no_fp, . - PendSV_bug_no_fp
    .ltorg
#endif
