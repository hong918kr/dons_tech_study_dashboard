/* ENV: qemu */
/* ARM-M13 solution.S — AArch64 예외 레벨 · VBAR_EL1 벡터 · ESR/ELR/SPSR · ERET (QEMU virt, cortex-a53).
 * ELF/GNU as 문법 (.global / .type / .size). MMU off, 인터럽트 없음 (DAIF 전부 마스크된 채로 부팅).
 * 프레임 = C 함수를 부르기 위해 필요한 것만: x0–x18, x29, x30 (+ ESR/ELR/SPSR/FAR/종류/SP_EL0).
 * 모든 레지스터 저장·컨텍스트 스위치는 ARM-A02 몫.
 */
#define FRAME   224         /* sizeof(struct exc_frame) = 216 → 16 바이트 정렬로 224 */
#define F_ELR   168
#define F_SPSR  176
#define F_ESR   184
#define F_FAR   192
#define F_KIND  200
#define F_SPEL0 208

    .text

/* ================= 주어진 것: 예외를 일으키는 헬퍼 (라벨 = 테스트의 기댓값) ================= */

// uint64_t do_svc42(uint64_t x) — svc 뒤의 add 가 꼭 실행돼야 x + 1 + 100 이 나온다
    .global do_svc42, svc42_insn
    .type   do_svc42, %function
do_svc42:
svc42_insn:
    svc     #0x42               // 동기 예외. ELR_EL1 = 다음 명령 (add)
    add     x0, x0, #100        // ELR 을 +4 해 버리는 버그면 이 줄이 건너뛰어진다
    ret
    .size   do_svc42, . - do_svc42

// uint64_t do_brk(void) — brk #7: ELR = brk 자신 → 핸들러가 +4 해야 한다
    .global do_brk, brk_insn
    .type   do_brk, %function
do_brk:
    mov     x0, #1
brk_insn:
    brk     #7
    ret
    .size   do_brk, . - do_brk

// uint64_t do_udf(void) — 영구 미정의 명령 → EC 0x00 (Unknown reason)
    .global do_udf, udf_insn
    .type   do_udf, %function
do_udf:
    mov     x0, #2
udf_insn:
    udf     #0x1234
    ret
    .size   do_udf, . - do_udf

// uint64_t do_hvc(uint64_t x0) — EL2 가 없는 virt 에서 HVC 는? (QEMU 는 여기서 PSCI 펌웨어를 흉내 낸다 — 5. 풀이)
    .global do_hvc, hvc_insn
    .type   do_hvc, %function
do_hvc:
hvc_insn:
    hvc     #0                  // x0 = PSCI 함수 ID (SMCCC 규약)
    ret
    .size   do_hvc, . - do_hvc

// uint64_t load64(const void *p) / void store64(void *p, uint64_t v) — 데이터 어보트 실험용
    .global load64, load64_insn, store64, store64_insn
    .type   load64, %function
load64:
load64_insn:
    ldr     x0, [x0]            // FAR_EL1 = x0, ISS.SRT = 0, WnR = 0
    ret
    .size   load64, . - load64
    .type   store64, %function
store64:
store64_insn:
    str     x1, [x0]            // WnR = 1, SRT = 1
    ret
    .size   store64, . - store64

// uint64_t read_vbar_el1(void), read_daif(void)
    .global read_vbar_el1, read_daif
    .type   read_vbar_el1, %function
read_vbar_el1:
    mrs     x0, vbar_el1
    ret
    .size   read_vbar_el1, . - read_vbar_el1
    .type   read_daif, %function
read_daif:
    mrs     x0, daif            // bit 9 D, 8 A, 7 I, 6 F
    ret
    .size   read_daif, . - read_daif

/* ---- EL0 에서 부르는 것 (run_el0 이 EL0 으로 내려 보낸 C 함수가 쓴다) ---- */
    .global el0_svc_mul10, el0_exit, el0_read_currentel, el0_currentel_insn
    .type   el0_svc_mul10, %function
el0_svc_mul10:
    svc     #1                  // "시스템 콜 1": 커널(EL1)이 x0 *= 10
    ret
    .size   el0_svc_mul10, . - el0_svc_mul10
    .type   el0_exit, %function
el0_exit:
    svc     #0                  // "시스템 콜 0" = exit(x0). 돌아오지 않는다
    b       el0_exit
    .size   el0_exit, . - el0_exit
    .type   el0_read_currentel, %function
el0_read_currentel:
el0_currentel_insn:
    mrs     x0, CurrentEL       // EL0 에서는 접근 불가 → EL1 로 동기 예외
    ret
    .size   el0_read_currentel, . - el0_read_currentel

/* ================= Part A: CurrentEL, VBAR_EL1 ================= */

// uint64_t get_current_el(void) — CurrentEL[3:2] 를 0..3 으로
    .global get_current_el
    .type   get_current_el, %function
get_current_el:
    mrs     x0, CurrentEL       // EL 은 bit[3:2] 에 있다 (나머지 RES0)
    ubfx    x0, x0, #2, #2      // x0 = (CurrentEL >> 2) & 3
    ret
    .size   get_current_el, . - get_current_el

// void set_vbar_el1(const void *table) — 2 KB 정렬된 벡터 테이블 설치
    .global set_vbar_el1
    .type   set_vbar_el1, %function
set_vbar_el1:
    msr     vbar_el1, x0        // 하위 11 비트는 RES0 → 테이블은 0x800 정렬이어야 한다
    isb                         // 다음 예외부터 새 VBAR 을 쓰게 (시스템 레지스터 쓰기 → ISB)
    ret
    .size   set_vbar_el1, . - set_vbar_el1

/* ================= Part A: 벡터 테이블 (16 엔트리 × 0x80) ================= */

    .macro  VENTRY kind
    .p2align 7                  // 엔트리마다 0x80 = 명령 32 개 자리
    sub     sp, sp, #FRAME      // 예외 진입 직후 sp = SP_EL1 (SPSel 이 1 로 바뀐다)
    stp     x0, x1, [sp, #0]
    mov     x0, #\kind          // 몇 번 엔트리로 들어왔나 = 오프셋 / 0x80
    b       exc_common
    .endm

    .p2align 11                 // VBAR_EL1[10:0] = 0 → 2 KB 정렬
    .global vectors_el1
    .type   vectors_el1, %function
vectors_el1:
    VENTRY  0                   // 0x000 현재 EL, SP_EL0 사용 (EL1t): Sync
    VENTRY  1                   // 0x080                          IRQ
    VENTRY  2                   // 0x100                          FIQ
    VENTRY  3                   // 0x180                          SError
    VENTRY  4                   // 0x200 현재 EL, SP_ELx 사용 (EL1h): Sync  ← 이 레슨의 svc/brk/udf/abort
    VENTRY  5                   // 0x280                          IRQ
    VENTRY  6                   // 0x300                          FIQ
    VENTRY  7                   // 0x380                          SError
    VENTRY  8                   // 0x400 하위 EL, AArch64 (EL0):   Sync  ← EL0 의 svc
    VENTRY  9                   // 0x480                          IRQ
    VENTRY  10                  // 0x500                          FIQ
    VENTRY  11                  // 0x580                          SError
    VENTRY  12                  // 0x600 하위 EL, AArch32:          Sync
    VENTRY  13                  // 0x680                          IRQ
    VENTRY  14                  // 0x700                          FIQ
    VENTRY  15                  // 0x780                          SError
    .size   vectors_el1, . - vectors_el1

// 공통 경로: 프레임 채우기 → exc_handler(frame) → 프레임에서 ELR/SPSR 되돌려 쓰기 → ERET
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
    stp     x18, x29, [sp, #144]    // x18 @144, fp @152
    str     x30, [sp, #160]         // lr @160 (bl 이 덮기 전에)
    str     x0, [sp, #F_KIND]
    mrs     x1, esr_el1             // 예외 원인 (EC/IL/ISS)
    str     x1, [sp, #F_ESR]
    mrs     x1, elr_el1             // 돌아갈 주소
    str     x1, [sp, #F_ELR]
    mrs     x1, spsr_el1            // 예외 직전 PSTATE (M[3:0] = 어느 EL/SP, DAIF, NZCV)
    str     x1, [sp, #F_SPSR]
    mrs     x1, far_el1             // 어보트 주소 (어보트가 아니면 UNKNOWN)
    str     x1, [sp, #F_FAR]
    mrs     x1, sp_el0              // EL0(또는 EL1t) 쪽 스택 포인터 — 핸들러는 SP_EL1 위에 있다
    str     x1, [sp, #F_SPEL0]
    mov     x0, sp                  // x0 = struct exc_frame *
    bl      exc_handler             // C: 0 = ERET 로 복귀, 1 = EL0 프로그램 종료(run_el0 로 돌아감)
    cbnz    w0, 7f
    ldr     x1, [sp, #F_ELR]
    msr     elr_el1, x1             // C 가 고친 ELR (BRK/UDF/abort 는 +4)
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
    ldp     x0, x1, [sp, #0]        // x0 = C 가 바꾼 반환값일 수 있다 (svc)
    add     sp, sp, #FRAME
    eret                            // PC ← ELR_EL1, PSTATE ← SPSR_EL1 (EL·SP·DAIF 까지 복원)
7:  ldr     x0, [sp, #0]            // EL0 의 exit 코드
    add     sp, sp, #FRAME          // SP_EL1 = run_el0 이 EL0 으로 내려가기 직전 값
    b       run_el0_ret
    .size   exc_common, . - exc_common

/* ================= Part C: EL2 → EL1, EL1 → EL0 ================= */

// void el2_to_el1(void) — EL2 에서 불려서 "EL1 에서" 돌아오는 함수 (_start 가 부른다)
    .global el2_to_el1
    .type   el2_to_el1, %function
el2_to_el1:
    mrs     x0, hcr_el2
    adrp    x1, g_hcr_el2_before
    str     x0, [x1, :lo12:g_hcr_el2_before]
#ifndef BUG_NO_RW                // 7. 실수 4 데모: run_qemu.sh bug_norw
    orr     x0, x0, #(1 << 31)  // HCR_EL2.RW = 1: EL1 은 AArch64
#endif
    msr     hcr_el2, x0
    adrp    x1, g_hcr_el2_after
    str     x0, [x1, :lo12:g_hcr_el2_after]
    mov     x0, sp
    msr     sp_el1, x0          // EL1h 의 스택 = 지금 쓰던 부트 스택
    mov     x0, #0x3c5          // SPSR_EL2: D A I F = 1111, M[3:0] = 0101 (EL1h)
    msr     spsr_el2, x0
    msr     elr_el2, x30        // "돌아갈 주소" = 나를 부른 곳 → ERET 이 거기로, EL1 로
    eret
    .size   el2_to_el1, . - el2_to_el1

// uint64_t run_el0(uint64_t (*fn)(uint64_t), uint64_t arg, void *el0_sp_top)
// fn(arg) 를 EL0 에서 실행. fn 은 return 대신 el0_exit(code) (svc #0) 로 끝낸다 → 그 code 를 돌려준다.
    .global run_el0, run_el0_ret
    .type   run_el0, %function
run_el0:
    stp     x29, x30, [sp, #-96]!   // EL1 쪽 상태를 SP_EL1 에 저장 (돌아올 때 꺼낸다)
    mov     x29, sp
    stp     x19, x20, [sp, #16]
    stp     x21, x22, [sp, #32]
    stp     x23, x24, [sp, #48]
    stp     x25, x26, [sp, #64]
    stp     x27, x28, [sp, #80]
    msr     sp_el0, x2          // EL0 이 쓸 스택
    msr     elr_el1, x0         // ERET 의 목적지 = fn
    msr     spsr_el1, xzr       // M[3:0] = 0000 (EL0t), DAIF = 0 (GIC 를 안 켜서 인터럽트는 안 온다)
    mov     x0, x1              // fn 의 첫 인자
    eret                        // → EL0. SP_EL1 은 위 저장 영역을 가리킨 채 남는다
run_el0_ret:                    // exc_common 이 svc #0 을 받으면 여기로 (x0 = exit code)
    ldp     x19, x20, [sp, #16]
    ldp     x21, x22, [sp, #32]
    ldp     x23, x24, [sp, #48]
    ldp     x25, x26, [sp, #64]
    ldp     x27, x28, [sp, #80]
    ldp     x29, x30, [sp], #96
    ret
    .size   run_el0, . - run_el0
