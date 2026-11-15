/* ENV: qemu */
/* ARM-M08 solution.S — 어셈블리 벡터 테이블 + Reset_Handler + VTOR 재배치 (Cortex-M3, QEMU mps2-an385).
 * 빌드: run_qemu.sh [starter|solution]  →  harness.c + <variant>.S + shared_startup.c  (-T arm_m08.ld)
 * Mac 에서 어셈블만: clang --target=thumbv7m-none-eabi -c solution.S
 * 주석은 // 또는 블록 주석만 (; 금지 — .S 는 C 전처리기를 거친다).
 */
    .syntax unified
    .cpu cortex-m3
    .thumb

/* ===================== 1. 벡터 테이블 (주소 0, 48 워드 = 16 시스템 + 32 IRQ) ===================== */
    .section .isr_vector, "a", %progbits     // "a" = 할당(메모리에 올라감), 실행 속성 없음 = 데이터
    .p2align 2
    .global g_vectors
    .type g_vectors, %object
g_vectors:
    .word _estack                   // [0]  초기 MSP — 코드 주소가 아니라 데이터 (bit0 의미 없음)
    .word Reset_Handler             // [1]  Reset            (주어짐)
    .word NMI_Handler               // [2]  NMI
    .word HardFault_Handler         // [3]  HardFault        (주어짐)
    .word MemManage_Handler         // [4]  MemManage
    .word BusFault_Handler          // [5]  BusFault
    .word UsageFault_Handler        // [6]  UsageFault
    .word 0, 0, 0, 0                // [7..10] reserved
    .word SVC_Handler               // [11] SVCall
    .word DebugMon_Handler          // [12] DebugMonitor
    .word 0                         // [13] reserved
    .word PendSV_Handler            // [14] PendSV
    .word SysTick_Handler           // [15] SysTick
    .rept 6
    .word Default_Handler           // [16..21] IRQ0..5
    .endr
    .word Irq6_Handler              // [22] IRQ6 (QEMU mps2-an385 에서 비어 있는 선 → 소프트웨어 pend 용)
    .rept 25
    .word Default_Handler           // [23..47] IRQ7..31
    .endr
    .size g_vectors, . - g_vectors

/* 기본 핸들러: 약한(weak) 별칭. 강한 정의(C 의 SVC_Handler 등)가 있으면 그쪽이 이긴다.
 * .thumb_set = .set + "이 심볼은 Thumb 함수" 표시 → 링커가 .word 에 bit0 = 1 을 넣는다. */
    .weak NMI_Handler
    .thumb_set NMI_Handler, Default_Handler
    .weak MemManage_Handler
    .thumb_set MemManage_Handler, Default_Handler
    .weak BusFault_Handler
    .thumb_set BusFault_Handler, Default_Handler
    .weak DebugMon_Handler
    .thumb_set DebugMon_Handler, Default_Handler
    .weak PendSV_Handler
    .thumb_set PendSV_Handler, Default_Handler
    .thumb_set UsageFault_Handler, HardFault_Handler   // 두 fault 가 같은 분석 코드를 쓴다
    .global UsageFault_Handler

/* ===================== 2. Reset_Handler: .data 복사, .bss 0, main, 종료 ===================== */
    .text
    .p2align 2
    .global Reset_Handler
    .type Reset_Handler, %function
    .thumb_func
Reset_Handler:                      // 하드웨어가 이미 MSP = [0], PC = [1] & ~1, T = [1] & 1 로 해 둠
    ldr r1, =_sidata                // .data 초기값 (FLASH 쪽 LMA)
    ldr r2, =_sdata                 // .data 시작 (RAM 쪽 VMA)
    ldr r3, =_edata
1:  cmp r2, r3                      // while (d < end)
    ittt lo                         // 다음 3 명령은 lo(unsigned <) 일 때만
    ldrlo r0, [r1], #4              //   r0 = *s++
    strlo r0, [r2], #4              //   *d++ = r0
    blo 1b
    ldr r2, =_sbss
    ldr r3, =_ebss
    movs r0, #0
2:  cmp r2, r3                      // while (d < end) *d++ = 0
    itt lo
    strlo r0, [r2], #4
    blo 2b
    bl main                         // r0 = main 의 반환값
    b sh_exit                       // = QEMU 종료 코드 (돌아오지 않음)
    .size Reset_Handler, . - Reset_Handler

/* ===================== 3. VTOR 재배치 ===================== */
// void vtor_relocate(uint32_t *dst, const uint32_t *src, uint32_t nwords)
//   src 의 테이블을 dst(RAM, 정렬된 배열)로 복사하고 SCB->VTOR = dst.
    .p2align 2
    .global vtor_relocate
    .type vtor_relocate, %function
    .thumb_func
vtor_relocate:
    mov r12, r0                     // dst 시작 주소 기억 (r12 = 스크래치)
    cbz r2, 2f                      // nwords == 0 이면 복사 생략
1:  ldr r3, [r1], #4                // *src++
    str r3, [r0], #4                // *dst++
    subs r2, r2, #1
    bne 1b
2:  dsb                             // 테이블 쓰기가 VTOR 변경보다 먼저 끝나게
    ldr r3, =0xE000ED08             // &SCB->VTOR
    str r12, [r3]                   // VTOR = dst
    dsb                             // VTOR 쓰기 완료
    isb                             // 뒤 명령·예외가 새 VTOR 을 보게
    bx lr
    .size vtor_relocate, . - vtor_relocate

// void vector_patch(uint32_t *table, uint32_t exc_num, uint32_t handler)
//   table[exc_num] = handler (bit0 은 호출자가 책임 — 일부러 짝수를 넣는 실험도 이 함수로)
    .p2align 2
    .global vector_patch
    .type vector_patch, %function
    .thumb_func
vector_patch:
    str r2, [r0, r1, lsl #2]        // table[exc_num] = handler
    dsb                             // 다음 예외의 벡터 fetch 전에 쓰기 완료
    bx lr
    .size vector_patch, . - vector_patch

/* ===================== 4. 주어진 코드 (TODO 없음) ===================== */
// Default_Handler: r0 = IPSR(예외 번호) → C 의 unexpected_exception (종료 코드 3)
    .p2align 2
    .global Default_Handler
    .type Default_Handler, %function
    .thumb_func
Default_Handler:
    mrs r0, ipsr
    b unexpected_exception
    .size Default_Handler, . - Default_Handler

// HardFault_Handler (= UsageFault_Handler): 프레임 포인터와 EXC_RETURN 을 C 로 넘긴다.
//   fault_record(frame, exc_return) 가 리턴하면 bx lr(EXC_RETURN) = 예외 복귀. 프레임 해부는 ARM-M09.
    .p2align 2
    .global HardFault_Handler
    .type HardFault_Handler, %function
    .thumb_func
HardFault_Handler:
    tst lr, #4                      // EXC_RETURN bit2: 0 = MSP 에 쌓임, 1 = PSP
    ite eq
    mrseq r0, msp
    mrsne r0, psp
    mov r1, lr
    b fault_record
    .size HardFault_Handler, . - HardFault_Handler

// uint32_t peek32(uint32_t addr) — 주소 0 읽기를 C 의 널 포인터 규칙 밖에서 (BM-B08 실수 4)
    .p2align 2
    .global peek32
    .type peek32, %function
    .thumb_func
peek32:
    ldr r0, [r0]
    bx lr
    .size peek32, . - peek32

// void call_blx(uint32_t target) — target 으로 blx. bit0 = 0 이면 T = 0 → 다음 명령에서 INVSTATE
    .p2align 2
    .global call_blx
    .type call_blx, %function
    .thumb_func
call_blx:
    push {r4, lr}
    blx r0
    .global call_blx_ret            // 타입 없는 라벨 → C 에서 보이는 주소는 짝수 그대로
call_blx_ret:
    pop {r4, pc}
    .size call_blx, . - call_blx

// void bx_target(void) — 4 바이트 정렬된 시험용 함수: g_bx_ran++
    .p2align 2
    .global bx_target
    .type bx_target, %function
    .thumb_func
bx_target:
    ldr r1, =g_bx_ran
    ldr r0, [r1]
    adds r0, r0, #1
    str r0, [r1]
    bx lr
    .size bx_target, . - bx_target
    .ltorg
