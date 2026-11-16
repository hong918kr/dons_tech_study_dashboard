/* ARM-A04 I-cache 일관성 — 캐시 유지보수 명령 시퀀스 (AArch64, Apple Mach-O + Linux ELF)
 *
 * build (Mac): cc -std=c11 -Wall -Wextra -Werror -O2 {starter|solution}.c {starter|solution}.S shared_ref.S -o /tmp/ARM-A04_v
 *
 * uint64_t cache_sync_range(void *start, size_t len, size_t dline, size_t iline, unsigned flags)
 *   "방금 데이터로 쓴 [start, start+len) 를 명령으로 실행해도 된다" 를 보장하는 표준 순서:
 *     DC CVAU (D-line 마다)  →  DSB ISH  →  IC IVAU (I-line 마다)  →  DSB ISH  →  ISB
 *   dline/iline: 줄 크기(바이트, 2의 거듭제곱). flags bit0 = SKIP_DC (CTR_EL0.IDC=1), bit1 = SKIP_IC (CTR_EL0.DIC=1).
 *   반환값 = (DC 실행 횟수 << 32) | IC 실행 횟수  → 테스트가 "줄을 빠짐없이, 중복 없이" 돌았는지 C 모델과 비교.
 *   len == 0 이면 아무 명령도 실행하지 않고 0.
 * uint64_t read_ctr_el0(void)
 *   CTR_EL0 원값. 리눅스는 EL0 읽기 허용(또는 커널이 에뮬레이션), macOS 는 SIGILL (실측) → C 는 fork 한 자식에서만 부른다.
 * leaf 함수: 스택 안 씀, x0–x9 만 사용 (caller-saved).
 */
#ifdef __APPLE__
.macro FUNC name
    .globl _\name
    .p2align 4
_\name:
.endm
.macro ENDF name
.endm
#else
.macro FUNC name
    .globl \name
    .type \name, %function
    .p2align 4
\name:
.endm
.macro ENDF name
    .size \name, . - \name
.endm
#endif

    .text

FUNC read_ctr_el0
    mrs   x0, ctr_el0              // Cache Type Register (EL0 접근은 SCTLR_EL1.UCT 가 허락해야 함)
    ret
ENDF read_ctr_el0

FUNC cache_sync_range
    mov   x6, #0                   // x6 = DC 횟수
    mov   x7, #0                   // x7 = IC 횟수
    cbz   x1, 9f                   // len == 0 → 할 일 없음
    add   x5, x0, x1               // x5 = end (배타적)
    // ---- 1) D 쪽: 새 명령 바이트를 PoU(Point of Unification) 까지 밀어낸다 ----
    tbnz  w4, #0, 2f               // SKIP_DC: IDC=1 이면 D→PoU clean 불필요
    sub   x8, x2, #1               // x8 = dline - 1 (마스크)
    bic   x9, x0, x8               // 시작 주소를 D-line 경계로 내림 (안 내리면 마지막 줄을 놓친다)
1:  dc    cvau, x9                 // Clean by VA to PoU
    add   x6, x6, #1
    add   x9, x9, x2               // 다음 D-line
    cmp   x9, x5
    b.lo  1b                       // 부호 없는 비교 (주소)
2:  dsb   ish                      // clean 이 inner-shareable 전체에서 끝난 뒤에야 IC 를 시작
    // ---- 2) I 쪽: 그 주소의 옛 명령을 모든 코어의 I-cache 에서 버린다 ----
    tbnz  w4, #1, 4f               // SKIP_IC: DIC=1 이면 IC 무효화 불필요 (ISB 는 여전히 필요)
    sub   x8, x3, #1               // x8 = iline - 1
    bic   x9, x0, x8               // 시작을 I-line 경계로 내림
3:  ic    ivau, x9                 // Invalidate by VA to PoU (inner shareable 브로드캐스트)
    add   x7, x7, #1
    add   x9, x9, x3               // 다음 I-line
    cmp   x9, x5
    b.lo  3b
    dsb   ish                      // 무효화 완료 보장
4:  isb                            // 이 코어가 이미 가져온(prefetch) 명령을 버리고 다시 가져온다
9:  orr   x0, x7, x6, lsl #32      // 반환: (dc << 32) | ic
    ret
ENDF cache_sync_range
