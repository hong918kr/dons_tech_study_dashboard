/* ENV: qemu */
/* ARM-M13 practice.S — 추가 연습 답. 본문(solution.S)을 그대로 포함하고 아래만 더한다. */
#include "solution.S"

// 연습 1: void svc_on_sp0(void *sp0_top) — EL1t (SPSel = 0, sp = SP_EL0) 상태에서 svc #0x50
    .global svc_on_sp0, svc50_insn
    .type   svc_on_sp0, %function
svc_on_sp0:
    msr     sp_el0, x0          // SP_EL0 을 먼저 정한다
    msr     spsel, #0           // 이제 sp = SP_EL0 (EL1t). 이 사이에 스택을 쓰면 안 된다
svc50_insn:
    svc     #0x50               // → 벡터 0x000 (현재 EL, SP_EL0). 진입하면 SPSel = 1 (SP_EL1) 로 바뀐다
    msr     spsel, #1           // ERET 은 SPSR.M = EL1t 로 돌려놓았다 → 다시 EL1h 로
    ret
    .size   svc_on_sp0, . - svc_on_sp0

// 연습 2: ID 레지스터 읽기 (EL1 에서 읽을 수 있다)
    .global read_midr, read_mpidr, read_pfr0
    .type   read_midr, %function
read_midr:
    mrs     x0, midr_el1        // EL2 가 있으면 EL1 은 VPIDR_EL2 값을 본다
    ret
    .size   read_midr, . - read_midr
    .type   read_mpidr, %function
read_mpidr:
    mrs     x0, mpidr_el1
    ret
    .size   read_mpidr, . - read_mpidr
    .type   read_pfr0, %function
read_pfr0:
    mrs     x0, id_aa64pfr0_el1 // [3:0] EL0, [7:4] EL1, [11:8] EL2, [15:12] EL3 — 0 = 없음
    ret
    .size   read_pfr0, . - read_pfr0

// 연습 4: uint64_t do_smc(uint64_t fid) — SMC 는 EL3 로 가는 "펌웨어 호출"
    .global do_smc, smc_insn
    .type   do_smc, %function
do_smc:
smc_insn:
    smc     #0
    ret
    .size   do_smc, . - do_smc

// 연습 4: void el3_to_lower(void) — shared_start.S 의 weak 판을 덮어쓴다. EL3 에서 불려 EL2 (있으면) 또는 EL1 에서 돌아온다
    .global el3_to_lower
    .type   el3_to_lower, %function
el3_to_lower:
    mov     x1, #0x401          // SCR_EL3: RW (bit 10) = 1 → 아래 EL 은 AArch64, NS (bit 0) = 1 → 비보안. HCE = 0
    msr     scr_el3, x1
    adrp    x2, g_scr_el3
    str     x1, [x2, :lo12:g_scr_el3]
    mrs     x0, id_aa64pfr0_el1
    ubfx    x0, x0, #8, #4      // EL2 필드
    mov     x2, sp
    cbz     x0, 1f
    msr     sp_el2, x2          // EL2 가 있다 → EL2h 로 (그다음 _start 가 el2_to_el1 을 부른다)
    mov     x3, #0x3c9          // DAIF = 1111, M = 1001 (EL2h)
    b       2f
1:  msr     sp_el1, x2          // EL2 가 없다 → 바로 EL1h 로
    mov     x3, #0x3c5          // M = 0101 (EL1h)
2:  msr     spsr_el3, x3
    msr     elr_el3, x30        // 나를 부른 _start 로, 낮은 EL 에서
    eret
    .size   el3_to_lower, . - el3_to_lower
