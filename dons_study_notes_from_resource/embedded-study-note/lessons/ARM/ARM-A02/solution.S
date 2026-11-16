/* ENV: qemu */
/* ARM-A02 solution.S — AArch64 EL1 벡터 테이블 + 전체 레지스터 프레임 저장(SAVE_ALL/RESTORE_ALL) + 도우미.
 * QEMU virt (gic-version=2, cortex-a53), MMU off, EL1h. ELF/GNU as 문법. 주석은 // 만.
 * 프레임 = struct trap_frame (solution.c): x0..x30, SP_EL0, ELR_EL1, SPSR_EL1, ESR_EL1, FAR_EL1 = 36 × 8 = 288 B.
 */
#define TF_X30    240
#define TF_SP_EL0 248
#define TF_ELR    256
#define TF_SPSR   264
#define TF_ESR    272
#define TF_FAR    280
#ifdef BUG_FRAME_ALIGN
#define TF_SIZE   280               // 7. 실수 4: 16 의 배수가 아닌 프레임 → SP 정렬 폴트
#else
#define TF_SIZE   288               // 36 워드 = 16 × 18 → SP 가 16 바이트 정렬을 유지한다
#endif

// ================= TODO(Part A): 프레임 저장 / 복원 =================
// 진입 직후: PSTATE.DAIF = 1111 (하드웨어가 마스크), sp = SP_EL1. 범용 레지스터는 하나도 저장되지 않았다.
    .macro  SAVE_ALL
    sub     sp, sp, #TF_SIZE
    stp     x0, x1, [sp, #16 * 0]
    stp     x2, x3, [sp, #16 * 1]
    stp     x4, x5, [sp, #16 * 2]
    stp     x6, x7, [sp, #16 * 3]
    stp     x8, x9, [sp, #16 * 4]
    stp     x10, x11, [sp, #16 * 5]
    stp     x12, x13, [sp, #16 * 6]
    stp     x14, x15, [sp, #16 * 7]
    stp     x16, x17, [sp, #16 * 8]
    stp     x18, x19, [sp, #16 * 9]
    stp     x20, x21, [sp, #16 * 10]
    stp     x22, x23, [sp, #16 * 11]
    stp     x24, x25, [sp, #16 * 12]
    stp     x26, x27, [sp, #16 * 13]
    stp     x28, x29, [sp, #16 * 14]
#ifndef BUG_NO_LR
    str     x30, [sp, #TF_X30]      // bl exc_c 가 x30 을 덮기 전에
#endif
    mrs     x0, sp_el0              // 여기부터 x0..x4 는 이미 저장됐으니 스크래치
    mrs     x1, elr_el1             // 돌아갈 주소 — 다음 예외(중첩 IRQ, C 안의 sync)가 덮어쓴다
    mrs     x2, spsr_el1            // 예외 직전 PSTATE (NZCV, DAIF, M) — 역시 덮인다
    mrs     x3, esr_el1
    mrs     x4, far_el1
    stp     x0, x1, [sp, #TF_SP_EL0]
    stp     x2, x3, [sp, #TF_SPSR]
    str     x4, [sp, #TF_FAR]
    .endm

// 복귀: 프레임 → 시스템 레지스터 → 범용 레지스터 → ERET. 이 구간 내내 IRQ 는 마스크돼 있어야 한다.
    .macro  RESTORE_ALL
    ldp     x0, x1, [sp, #TF_SP_EL0]
    ldr     x2, [sp, #TF_SPSR]
    msr     sp_el0, x0
#ifndef BUG_NO_ELR
    msr     elr_el1, x1             // C 가 고친 값일 수도 있다 (컨텍스트 스위치라면 다른 태스크의 PC)
    msr     spsr_el1, x2
#endif
    ldp     x2, x3, [sp, #16 * 1]
    ldp     x4, x5, [sp, #16 * 2]
    ldp     x6, x7, [sp, #16 * 3]
    ldp     x8, x9, [sp, #16 * 4]
    ldp     x10, x11, [sp, #16 * 5]
    ldp     x12, x13, [sp, #16 * 6]
    ldp     x14, x15, [sp, #16 * 7]
    ldp     x16, x17, [sp, #16 * 8]
    ldp     x18, x19, [sp, #16 * 9]
    ldp     x20, x21, [sp, #16 * 10]
    ldp     x22, x23, [sp, #16 * 11]
    ldp     x24, x25, [sp, #16 * 12]
    ldp     x26, x27, [sp, #16 * 13]
    ldp     x28, x29, [sp, #16 * 14]
#ifndef BUG_NO_LR
    ldr     x30, [sp, #TF_X30]
#endif
    ldp     x0, x1, [sp, #16 * 0]   // 스크래치로 쓴 x0, x1 은 마지막에
    add     sp, sp, #TF_SIZE
    eret                            // PC ← ELR_EL1, PSTATE ← SPSR_EL1 (DAIF 도 예외 전 값으로)
    .endm

// ================= 벡터 테이블 (주어짐) =================
// 엔트리 = SAVE_ALL (25 명령) + 4 명령 = 29 ≤ 32 → 0x80 안에 들어간다. 넘치면 .org 가 "뒤로 갈 수 없다" 에러.
    .macro  VENTRY kind
    .p2align 7
.Lvent\kind:
    SAVE_ALL
    mov     x0, sp                  // x0 = struct trap_frame *
    mov     x1, #\kind              // x1 = 엔트리 번호 0..15 (= 오프셋 / 0x80)
    bl      exc_c                   // C 디스패처 (solution.c)
    b       ret_from_exc
    .org    .Lvent\kind + 0x80      // 리눅스 kernel_ventry 와 같은 넘침 검사 (빈칸은 0 = udf #0)
    .endm

    .text
    .p2align 11                     // VBAR_EL1[10:0] = 0
    .global vectors_el1, vectors_el1_end
    .type   vectors_el1, %function
vectors_el1:
    VENTRY  0                       // 0x000 현재 EL, SP_EL0: Sync
    VENTRY  1                       // 0x080                  IRQ
    VENTRY  2                       // 0x100                  FIQ
    VENTRY  3                       // 0x180                  SError
    VENTRY  4                       // 0x200 현재 EL, SP_ELx: Sync   ← svc #0x77 (프레임 왕복 테스트)
    VENTRY  5                       // 0x280                  IRQ    ← 타이머 / SGI
    VENTRY  6                       // 0x300                  FIQ
    VENTRY  7                       // 0x380                  SError
    VENTRY  8                       // 0x400 하위 EL AArch64: Sync
    VENTRY  9                       // 0x480                  IRQ
    VENTRY  10                      // 0x500                  FIQ
    VENTRY  11                      // 0x580                  SError
    VENTRY  12                      // 0x600 하위 EL AArch32: Sync
    VENTRY  13                      // 0x680                  IRQ
    VENTRY  14                      // 0x700                  FIQ
    VENTRY  15                      // 0x780                  SError
vectors_el1_end:
    .size   vectors_el1, . - vectors_el1

    .global ret_from_exc
    .type   ret_from_exc, %function
ret_from_exc:
    RESTORE_ALL
    .size   ret_from_exc, . - ret_from_exc

// ================= 레지스터 보존 검사기 (주어짐) =================
// void regs_hold(const uint64_t in[32], uint64_t out[32]) — x0..x30 = in[0..30], NZCV = in[31] 로 채운 채
//   IRQ 를 열고 wfi (+ nop 16 개) → IRQ 가 이 창 안에 떨어진다 → 다시 마스크하고 x0..x30, NZCV 를 out 에 찍는다.
// void regs_svc(const uint64_t in[32], uint64_t out[32]) — 같은 채우기 후 svc #0x77 (동기 예외 경로).
    .macro  PROBE_ENTER name
    .global \name
    .type   \name, %function
\name:
    sub     sp, sp, #368            // [0..96] 저장 영역, [112..360] 스냅샷 (16 정렬 유지)
    stp     x29, x30, [sp, #0]
    stp     x19, x20, [sp, #16]
    stp     x21, x22, [sp, #32]
    stp     x23, x24, [sp, #48]
    stp     x25, x26, [sp, #64]
    stp     x27, x28, [sp, #80]
    str     x1, [sp, #96]           // out
    ldr     x2, [x0, #248]          // in[31] = NZCV 패턴
    msr     nzcv, x2
    mov     x30, x0                 // 베이스를 x30 에 두고 x0..x29 를 채운 뒤 x30 을 마지막에
    ldp     x0, x1, [x30, #0]
    ldp     x2, x3, [x30, #16]
    ldp     x4, x5, [x30, #32]
    ldp     x6, x7, [x30, #48]
    ldp     x8, x9, [x30, #64]
    ldp     x10, x11, [x30, #80]
    ldp     x12, x13, [x30, #96]
    ldp     x14, x15, [x30, #112]
    ldp     x16, x17, [x30, #128]
    ldp     x18, x19, [x30, #144]
    ldp     x20, x21, [x30, #160]
    ldp     x22, x23, [x30, #176]
    ldp     x24, x25, [x30, #192]
    ldp     x26, x27, [x30, #208]
    ldp     x28, x29, [x30, #224]
    ldr     x30, [x30, #240]
    .endm

    .macro  PROBE_LEAVE name
    stp     x0, x1, [sp, #112]      // 스냅샷: 베이스가 sp 라 범용 레지스터를 하나도 안 건드린다
    stp     x2, x3, [sp, #128]
    stp     x4, x5, [sp, #144]
    stp     x6, x7, [sp, #160]
    stp     x8, x9, [sp, #176]
    stp     x10, x11, [sp, #192]
    stp     x12, x13, [sp, #208]
    stp     x14, x15, [sp, #224]
    stp     x16, x17, [sp, #240]
    stp     x18, x19, [sp, #256]
    stp     x20, x21, [sp, #272]
    stp     x22, x23, [sp, #288]
    stp     x24, x25, [sp, #304]
    stp     x26, x27, [sp, #320]
    stp     x28, x29, [sp, #336]
    str     x30, [sp, #352]
    mrs     x0, nzcv
    str     x0, [sp, #360]
    ldr     x1, [sp, #96]
    add     x2, sp, #112
    mov     x3, #32
1:  ldr     x4, [x2], #8            // 32 워드 복사 → out
    str     x4, [x1], #8
    subs    x3, x3, #1
    b.ne    1b
    ldp     x19, x20, [sp, #16]
    ldp     x21, x22, [sp, #32]
    ldp     x23, x24, [sp, #48]
    ldp     x25, x26, [sp, #64]
    ldp     x27, x28, [sp, #80]
    ldp     x29, x30, [sp, #0]
    add     sp, sp, #368
    ret
    .size   \name, . - \name
    .endm

    PROBE_ENTER regs_hold
    .global hold_win_start, hold_win_end
hold_win_start:
    msr     daifclr, #2             // IRQ 열기 (PSTATE.I = 0)
    wfi                             // 다음 틱까지 잠든다 → IRQ 는 여기(또는 nop 사이)에 떨어진다
    .rept   16
    nop
    .endr
    msr     daifset, #2             // 다시 마스크
hold_win_end:
    PROBE_LEAVE regs_hold

    PROBE_ENTER regs_svc
    .global svc77_insn
svc77_insn:
    svc     #0x77                   // 동기 예외 → 0x200 → 같은 SAVE_ALL/RESTORE_ALL
    PROBE_LEAVE regs_svc

// ================= 시스템 레지스터 도우미 (주어짐) =================
    .macro  FN name
    .global \name
    .type   \name, %function
\name:
    .endm

    FN set_vbar_el1
    msr     vbar_el1, x0
    isb
    ret
    FN read_vbar_el1
    mrs     x0, vbar_el1
    ret
    FN read_cntfrq
    mrs     x0, cntfrq_el0          // 카운터 주파수 (펌웨어가 써 넣는 값, QEMU 는 고정)
    ret
    FN read_cntvct
    isb                             // 앞선 명령보다 먼저 읽히지 않게
    mrs     x0, cntvct_el0
    ret
    FN write_cntv_tval
    msr     cntv_tval_el0, x0       // CVAL = 지금 + x0 (부호 있는 32 비트 다운카운터 보기)
    isb
    ret
    FN write_cntv_cval
    msr     cntv_cval_el0, x0
    isb
    ret
    FN read_cntv_cval
    mrs     x0, cntv_cval_el0
    ret
    FN write_cntv_ctl
    msr     cntv_ctl_el0, x0        // bit0 ENABLE, bit1 IMASK, bit2 ISTATUS(RO)
    isb
    ret
    FN read_cntv_ctl
    mrs     x0, cntv_ctl_el0
    ret
    FN irq_enable
    msr     daifclr, #2
    ret
    FN irq_disable
    msr     daifset, #2
    ret
    FN read_daif
    mrs     x0, daif
    ret
    FN wfi_once
    wfi
    ret
    FN read_elr_el1
    mrs     x0, elr_el1
    ret
    FN read_sctlr_el1
    mrs     x0, sctlr_el1
    ret
    FN read_current_el
    mrs     x0, CurrentEL
    ubfx    x0, x0, #2, #2
    ret
