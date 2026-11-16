/* ENV: qemu */
/* ARM-A03 practice.S — 추가 연습 답. 본문(solution.S)을 그대로 포함하고 아래만 더한다. */
#include "solution.S"

// 연습 3 관찰용: void set_ttbr0_only(uint64_t ttbr0) — TTBR0 만 바꾸고 TLBI 는 하지 않는다 (일부러 틀린 전환)
    .global set_ttbr0_only
    .type   set_ttbr0_only, %function
set_ttbr0_only:
    msr     ttbr0_el1, x0
    isb
    ret
    .size   set_ttbr0_only, . - set_ttbr0_only

// 연습 4: void mmu_disable(void) — SCTLR.M/C/I 를 끄고 TLB 를 비운다 (재설정 전 단계)
// 실칩에서는 D-cache 를 끄기 전에 dirty 라인을 clean 해야 한다 (set/way 또는 VA 범위) — QEMU 는 캐시를 모델링하지 않는다.
    .global mmu_disable
    .type   mmu_disable, %function
mmu_disable:
    mrs     x0, sctlr_el1
    bic     x0, x0, #(1 << 0)   // M
    bic     x0, x0, #(1 << 2)   // C
    bic     x0, x0, #(1 << 12)  // I
    msr     sctlr_el1, x0
    isb                         // 이 다음 명령부터 VA = PA (아이덴티티라 PC 는 그대로)
    tlbi    vmalle1
    dsb     nsh
    isb
    ret
    .size   mmu_disable, . - mmu_disable
