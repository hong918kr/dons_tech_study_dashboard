/* ENV: qemu */
/* ARM-M12 shared_triggers.S — fault 를 "일부러" 내는 leaf 함수들 + 호출 틀 (TODO 없음, 모든 변형에 링크).
 * 규칙: 각 트리거는 fault 명령 앞에 전역 라벨 <이름>_insn 을 둔다 → 테스트가 "쌓인 PC == 라벨" 을 확인.
 *       fault 명령 바로 뒤는 항상 `adds r0, r0, #1` → 정확히 건너뛰었으면 그 줄이 실행돼 반환값이 +1.
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
    .macro LABEL name                // 데이터 라벨처럼 쓰는 전역 코드 주소 (Thumb 비트 없음 = 짝수)
    .global \name
\name:
    .endm

// uint32_t run_trigger(fn, a, b) — fn(a, b) 를 부른다. fn 안에서 fault 가 나면 쌓인 LR = trigger_ret | 1
FUNC run_trigger
    push    {r4, lr}
    mov     r4, r0              // r4 = fn (bit0 = Thumb 비트. 0 이면 INVSTATE — 일부러 쓰는 경우 있음)
    mov     r0, r1              // a
    mov     r1, r2              // b
    blx     r4                  // LR = trigger_ret | 1
LABEL trigger_ret
    pop     {r4, pc}
    .size   run_trigger, . - run_trigger

// uint32_t fault_div(int32_t a, int32_t b) — SDIV (32 비트 명령). CCR.DIV_0_TRP=0 이면 a/0 = 0, fault 없음
FUNC fault_div
LABEL fault_div_insn
    sdiv    r0, r0, r1
    adds    r0, r0, #1
    bx      lr
    .size   fault_div, . - fault_div

// uint32_t fault_ldr(uint32_t addr) — LDR (16 비트). 홀수 주소 → UNALIGNED(트랩 시), 빈 주소 → BusFault
FUNC fault_ldr
LABEL fault_ldr_insn
    ldr     r0, [r0]
    adds    r0, r0, #1
    bx      lr
    .size   fault_ldr, . - fault_ldr

// uint32_t fault_ldrd(uint32_t addr) — LDRD (32 비트). 4 정렬이 아니면 트랩 설정과 무관하게 UNALIGNED
FUNC fault_ldrd
LABEL fault_ldrd_insn
    ldrd    r0, r1, [r0]
    adds    r0, r0, #1
    bx      lr
    .size   fault_ldrd, . - fault_ldrd

// uint32_t fault_udf16(void) — UDF #0 (16 비트 0xDE00): 항상 UNDEFINSTR. 정확히 건너뛰면 11
FUNC fault_udf16
    movs    r0, #10
LABEL fault_udf16_insn
    udf     #0
    adds    r0, r0, #1
    bx      lr
    .size   fault_udf16, . - fault_udf16

// uint32_t fault_udf32(void) — UDF.W #1 (32 비트 0xF7F0 0xA001). 정확히 건너뛰면 21 (+2 만 하면 망한다)
FUNC fault_udf32
    movs    r0, #20
LABEL fault_udf32_insn
    udf.w   #1
    adds    r0, r0, #1
    bx      lr
    .size   fault_udf32, . - fault_udf32

// uint32_t fault_tbit(uint32_t a) — 정상 함수. 일부러 Thumb 비트 없이 (짝수 주소로) blx 하면 INVSTATE.
//   4 바이트 정렬해 둔다: QEMU 10.2 는 "짝수지만 4 의 배수가 아닌" 목표에서 INVSTATE 대신 UNALIGNED 를 낸다 (9. 연습 3)
    .p2align 2
FUNC fault_tbit
    adds    r0, r0, #1
    bx      lr
    .size   fault_tbit, . - fault_tbit

// uint32_t run_on_psp(uint32_t *psp_top, uint32_t (*fn)(uint32_t), uint32_t arg)  (ARM-B14 축약판)
//   PSP = psp_top, CONTROL.SPSEL = 1 → fn(arg) 를 PSP 위에서 실행 ("태스크") → SPSEL = 0
FUNC run_on_psp
    push    {r4, lr}            // MSP 에 저장
    msr     psp, r0
    mrs     r4, control
    orr     r4, r4, #2          // SPSEL = 1
    msr     control, r4
    isb
    mov     r0, r2
    blx     r1                  // fn(arg) — 여기서 fault 가 나면 프레임은 PSP 에 쌓인다
    mrs     r4, control
    bic     r4, r4, #2          // SPSEL = 0
    msr     control, r4
    isb
    pop     {r4, pc}
    .size   run_on_psp, . - run_on_psp

// 특수 레지스터 읽기 (ARM-B14)
FUNC get_ipsr
    mrs     r0, ipsr
    bx      lr
    .size   get_ipsr, . - get_ipsr
FUNC get_control
    mrs     r0, control
    bx      lr
    .size   get_control, . - get_control
