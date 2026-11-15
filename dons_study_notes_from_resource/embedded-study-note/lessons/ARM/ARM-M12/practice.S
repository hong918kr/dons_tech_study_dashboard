/* ENV: qemu */
/* ARM-M12 practice.S — 추가 연습용 진입부와 트리거 (Fault_Entry 는 solution.S 와 같다). */
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
    .macro LABEL name
    .global \name
\name:
    .endm

FUNC Fault_Entry
    tst     lr, #4              // EXC_RETURN bit2: 0 = MSP, 1 = PSP
    ite     eq
    mrseq   r0, msp
    mrsne   r0, psp
    mov     r1, lr
    b       hardfault_c
    .size   Fault_Entry, . - Fault_Entry

// 연습 1: SVC 핸들러 안에서 UDF — "핸들러 안의 fault". leaf 라 LR = SVC 의 EXC_RETURN 그대로
FUNC SVC_Handler
    movs    r0, #40
LABEL svc_udf_insn
    udf     #2
    adds    r0, r0, #1
    bx      lr                  // 예외 복귀 (스레드로)
    .size   SVC_Handler, . - SVC_Handler

// uint32_t do_svc(void) — SVC 를 부른다 (SVC 핸들러가 무엇을 하든 반환값은 의미 없음)
FUNC do_svc
LABEL do_svc_insn
    svc     #0
    bx      lr
    .size   do_svc, . - do_svc

// 연습 2: uint32_t fault_nocp(uint32_t a) — VMOV s0, r0 (FPU 명령). Cortex-M3 에는 FPU 가 없다
//   어셈블러가 cortex-m3 에서 VFP 를 거부하므로 인코딩을 직접 넣는다: VMOV S0, R0 = 0xEE00 0x0A10
FUNC fault_nocp
LABEL fault_nocp_insn
    .inst.w 0xee000a10
    adds    r0, r0, #1
    bx      lr
    .size   fault_nocp, . - fault_nocp

// 연습 3: uint32_t fault_tbit2(uint32_t a) — 4 의 배수가 아닌 짝수 주소에 놓인 목표 (QEMU 특이 동작)
    .p2align 2
    nop                         // 2 바이트 밀어서 시작 주소 ≡ 2 (mod 4)
FUNC fault_tbit2
    adds    r0, r0, #1
    bx      lr
    .size   fault_tbit2, . - fault_tbit2

// 연습 4: uint32_t fault_it_div(int32_t a, int32_t b, uint32_t flag) — IT 블록 안의 fault
//   flag == 0 이면 SDIVEQ 가 실행된다 (b = 0 → DIVBYZERO). 그다음 MOVNE 는 "NE" 라 실행되면 안 된다.
FUNC fault_it_div
    cmp     r2, #0
    ite     eq
LABEL fault_it_div_insn
    sdiveq  r0, r0, r1          // IT 블록 첫 명령 (조건 EQ) — 여기서 fault
    movne   r0, #7              // IT 블록 둘째 (조건 NE)
    adds    r0, r0, #1
    bx      lr
    .size   fault_it_div, . - fault_it_div

// 연습 5: uint32_t fault_str(uint32_t addr, uint32_t v) — 저장 fault (실칩이면 대개 부정확 BusFault)
FUNC fault_str
LABEL fault_str_insn
    str     r1, [r0]
    adds    r0, r0, #1
    bx      lr
    .size   fault_str, . - fault_str
