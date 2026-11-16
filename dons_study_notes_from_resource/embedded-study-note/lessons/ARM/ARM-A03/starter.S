/* ENV: qemu */
/* ARM-A03 starter.S / solution.S — MMU 켜기 · AT 주소 변환 조회 · TLBI · 예외 벡터 (QEMU virt, cortex-a53, EL1).
 * ELF/GNU as 문법. 벡터 테이블 + exc_common 은 ARM-M13 solution.S 축약판 (x0–x18, x29, x30 + ESR/ELR/SPSR/FAR).
 * 전체 레지스터 저장·IRQ 는 ARM-A02 몫. 이 파일의 TODO = mmu_enable, at_s1e1r/at_s1e1w.
 */
#define FRAME   208         /* sizeof(struct exc_frame) = 26 × 8 = 208 (이미 16 의 배수) */
#define F_ELR   168
#define F_SPSR  176
#define F_ESR   184
#define F_FAR   192
#define F_KIND  200

    .text

/* ================= 주어진 것: 시스템 레지스터 읽기 ================= */
    .global get_current_el, read_sctlr, read_tcr, read_mair, read_ttbr0, read_mmfr0, read_mmfr1
    .type   get_current_el, %function
get_current_el:
    mrs     x0, CurrentEL
    ubfx    x0, x0, #2, #2
    ret
    .size   get_current_el, . - get_current_el
    .type   read_sctlr, %function
read_sctlr:
    mrs     x0, sctlr_el1
    ret
    .size   read_sctlr, . - read_sctlr
    .type   read_tcr, %function
read_tcr:
    mrs     x0, tcr_el1
    ret
    .size   read_tcr, . - read_tcr
    .type   read_mair, %function
read_mair:
    mrs     x0, mair_el1
    ret
    .size   read_mair, . - read_mair
    .type   read_ttbr0, %function
read_ttbr0:
    mrs     x0, ttbr0_el1
    ret
    .size   read_ttbr0, . - read_ttbr0
    .type   read_mmfr0, %function
read_mmfr0:
    mrs     x0, id_aa64mmfr0_el1    // [3:0] PARange, [31:28] TGran4 ...
    ret
    .size   read_mmfr0, . - read_mmfr0
    .type   read_mmfr1, %function
read_mmfr1:
    mrs     x0, id_aa64mmfr1_el1    // [3:0] HAFDBS: 0 = 하드웨어 AF/Dirty 갱신 없음
    ret
    .size   read_mmfr1, . - read_mmfr1

// void set_vbar_el1(const void *table)
    .global set_vbar_el1
    .type   set_vbar_el1, %function
set_vbar_el1:
    msr     vbar_el1, x0
    isb
    ret
    .size   set_vbar_el1, . - set_vbar_el1

/* ================= 주어진 것: TLB · 캐시 유지보수 ================= */
// void tlbi_va(uint64_t va) — 이 VA 의 (모든 ASID) 엔트리를 Inner Shareable 도메인에서 무효화
    .global tlbi_va, tlbi_all, dsb_ishst, isb_sync
    .type   tlbi_va, %function
tlbi_va:
    dsb     ishst               // 앞의 descriptor 쓰기가 walker 에게 보이게
    lsr     x0, x0, #12         // 피연산자 = VA[55:12] (ASID 필드 [63:48] = 0)
    tlbi    vaae1is, x0         // all-ASID 버전 (이 레슨은 nG = 0, ASID 안 씀)
    dsb     ish                 // TLBI 완료 대기
    isb
    ret
    .size   tlbi_va, . - tlbi_va
    .type   tlbi_all, %function
tlbi_all:
    dsb     ishst
    tlbi    vmalle1is
    dsb     ish
    isb
    ret
    .size   tlbi_all, . - tlbi_all
    .type   dsb_ishst, %function
dsb_ishst:
    dsb     ishst
    ret
    .size   dsb_ishst, . - dsb_ishst
    .type   isb_sync, %function
isb_sync:
    isb
    ret
    .size   isb_sync, . - isb_sync

// void sync_code(void *p) — p 의 캐시 라인 하나에 새로 쓴 명령을 실행 가능하게 (D 정리 → I 무효화 → ISB)
    .global sync_code
    .type   sync_code, %function
sync_code:
    dc      cvau, x0            // D-cache 를 PoU 까지 정리
    dsb     ish
    ic      ivau, x0            // 그 주소의 I-cache 무효화
    dsb     ish
    isb
    ret
    .size   sync_code, . - sync_code

/* ================= 주어진 것: 폴트를 일으킬 접근 (라벨 = 테스트의 ELR 기댓값) ================= */
    .global load64, load64_insn, store64, store64_insn, load32, load32_insn, store32
    .type   load64, %function
load64:
load64_insn:
    ldr     x0, [x0]
    ret
    .size   load64, . - load64
    .type   store64, %function
store64:
store64_insn:
    str     x1, [x0]
    ret
    .size   store64, . - store64
    .type   load32, %function
load32:
load32_insn:
    ldr     w0, [x0]
    ret
    .size   load32, . - load32
    .type   store32, %function
store32:
    str     w1, [x0]
    ret
    .size   store32, . - store32

/* ================= Part A: MMU 켜기 ================= */
// void mmu_enable(uint64_t ttbr0, uint64_t tcr, uint64_t mair)
// 순서: MAIR → TCR → TTBR0 → DSB/ISB → TLBI VMALLE1 → DSB/ISB → SCTLR.M|C|I → ISB
    .global mmu_enable
    .type   mmu_enable, %function
mmu_enable:
    // TODO(Part A): MAIR → TCR → TTBR0 → DSB/ISB → TLBI VMALLE1 → DSB/ISB → SCTLR.M|C|I → ISB
    ret                         // 스텁: MMU 를 켜지 않는다
    .size   mmu_enable, . - mmu_enable

/* ================= Part A: AT — 하드웨어 page walker 에게 물어보기 ================= */
// uint64_t at_s1e1r(uint64_t va) / at_s1e1w(uint64_t va) → PAR_EL1
    .global at_s1e1r, at_s1e1w
    .type   at_s1e1r, %function
at_s1e1r:
    // TODO(Part A): AT S1E1R → ISB → PAR_EL1
    mov     x0, #1              // 스텁: F = 1 (폴트), FST = 0
    ret
    .size   at_s1e1r, . - at_s1e1r
    .type   at_s1e1w, %function
at_s1e1w:
    // TODO(Part A): AT S1E1W → ISB → PAR_EL1
    mov     x0, #1
    ret
    .size   at_s1e1w, . - at_s1e1w

/* ================= 주어진 것: 벡터 테이블 (ARM-M13 축약판) ================= */
    .macro  VENTRY kind
    .p2align 7
    sub     sp, sp, #FRAME
    stp     x0, x1, [sp, #0]
    mov     x0, #\kind
    b       exc_common
    .endm

    .p2align 11
    .global vectors_el1
    .type   vectors_el1, %function
vectors_el1:
    VENTRY  0
    VENTRY  1
    VENTRY  2
    VENTRY  3
    VENTRY  4                   // 0x200 현재 EL, SP_ELx, Sync ← 이 레슨의 모든 폴트
    VENTRY  5
    VENTRY  6
    VENTRY  7
    VENTRY  8
    VENTRY  9
    VENTRY  10
    VENTRY  11
    VENTRY  12
    VENTRY  13
    VENTRY  14
    VENTRY  15
    .size   vectors_el1, . - vectors_el1

    .type   exc_common, %function
exc_common:
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x9, [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x29, [sp, #144]
    str     x30, [sp, #160]
    str     x0, [sp, #F_KIND]
    mrs     x1, esr_el1
    str     x1, [sp, #F_ESR]
    mrs     x1, elr_el1
    str     x1, [sp, #F_ELR]
    mrs     x1, spsr_el1
    str     x1, [sp, #F_SPSR]
    mrs     x1, far_el1
    str     x1, [sp, #F_FAR]
    mov     x0, sp
    bl      exc_handler             // C: ELR/x0 를 고칠 수 있다
    ldr     x1, [sp, #F_ELR]
    msr     elr_el1, x1
    ldr     x1, [sp, #F_SPSR]
    msr     spsr_el1, x1
    ldp     x2, x3, [sp, #16]
    ldp     x4, x5, [sp, #32]
    ldp     x6, x7, [sp, #48]
    ldp     x8, x9, [sp, #64]
    ldp     x10, x11, [sp, #80]
    ldp     x12, x13, [sp, #96]
    ldp     x14, x15, [sp, #112]
    ldp     x16, x17, [sp, #128]
    ldp     x18, x29, [sp, #144]
    ldr     x30, [sp, #160]
    ldp     x0, x1, [sp, #0]
    add     sp, sp, #FRAME
    eret
    .size   exc_common, . - exc_common
