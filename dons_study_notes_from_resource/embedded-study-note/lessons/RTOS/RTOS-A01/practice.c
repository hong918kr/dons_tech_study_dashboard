/* RTOS-A01 추가 연습 — 호스트(Mac / VM) C. 포트 계층의 "숫자" 를 손으로 다루는 도구 다섯 개.
 * 펌웨어가 아니다: QEMU 없이 돈다. 표의 값은 solution.elf 를 QEMU + gdb 로 돌려 실제로 얻은 것이다
 * (run_qemu.sh gdb / faults 출력, 레슨 md 5. 풀이 참고).
 *   cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/RTOS-A01_pr && /tmp/RTOS-A01_pr
 *
 *   연습 1  frame_lint        스위치아웃된 태스크의 16 워드(r4..r11 | r0 r1 r2 r3 r12 lr pc xPSR) 검사
 *   연습 2  exc_return_decode EXC_RETURN 값 → 돌아갈 모드 / 스택 / 프레임 크기
 *   연습 3  prio_masked       BASEPRI 와 구현 비트 수(QEMU 8, STM32 4)로 "이 IRQ 가 막히나 / FromISR 불러도 되나"
 *   연습 4  tick_loss         크리티컬 섹션 길이 → SysTick 몇 번 잃나, IRQ 펜딩 몇 번이 1 번으로 합쳐지나
 *   연습 5  frame_init_cm4f   Cortex-M4F 포트의 17 워드 초기 프레임 (EXC_RETURN 칸 추가)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } else { \
    printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

/* ---------------------------------------------------------------- 연습 1 */
enum {
    FL_OK = 0,
    FL_NO_THUMB = 1u << 0,      /* xPSR.T (bit24) = 0 → 복귀하자마자 INVSTATE UsageFault */
    FL_PC_ODD = 1u << 1,        /* 스택된 PC bit0 = 1 → ARMv7-M 에서 UNPREDICTABLE (QEMU 는 경고만) */
    FL_PC_RANGE = 1u << 2,      /* PC 가 .text 밖 */
    FL_IPSR = 1u << 3,          /* 스레드 태스크 프레임인데 xPSR.IPSR ≠ 0 */
    FL_SP_RANGE = 1u << 4,      /* sp 가 [stack_lo + guard, stack_hi - 64] 밖 */
};
typedef struct { uint32_t text_lo, text_hi, stack_lo, stack_hi, guard_bytes; } lint_cfg_t;

static unsigned frame_lint(uint32_t sp, const uint32_t w[16], const lint_cfg_t *c) {
    unsigned r = FL_OK;
    uint32_t pc = w[14], xpsr = w[15];
    if (((xpsr >> 24) & 1u) == 0u) r |= FL_NO_THUMB;
    if (pc & 1u) r |= FL_PC_ODD;
    if (pc < c->text_lo || pc >= c->text_hi) r |= FL_PC_RANGE;
    if ((xpsr & 0x1FFu) != 0u) r |= FL_IPSR;
    if (sp < c->stack_lo + c->guard_bytes || sp + 64u > c->stack_hi) r |= FL_SP_RANGE;
    return r;
}

/* ---------------------------------------------------------------- 연습 2 */
typedef struct { bool valid, to_thread, use_psp; unsigned frame_words; } exc_ret_t;

static exc_ret_t exc_return_decode(uint32_t v) {
    exc_ret_t e = { false, false, false, 0 };
    if ((v & 0xFFFFFFE0u) != 0xFFFFFFE0u) return e;      /* 상위 27 비트는 1 */
    uint32_t low = v & 0x1Fu;                              /* bit4 = !FType, bit3 = Thread, bit2 = PSP */
    switch (low) {
    case 0x11: case 0x19: case 0x1D: case 0x01: case 0x09: case 0x0D: break;
    default: return e;                                     /* 그 밖의 값은 ARMv7-M 에서 정의되지 않음 */
    }
    e.valid = true;
    e.to_thread = (low & 0x8u) != 0u;
    e.use_psp = (low & 0x4u) != 0u;
    e.frame_words = (low & 0x10u) ? 8u : 26u;              /* FType=0 → s0-s15, FPSCR, 예약 1 워드 추가 */
    return e;
}

/* ---------------------------------------------------------------- 연습 3 */
static uint8_t prio_eff(uint8_t p, int bits) { return (uint8_t)(p & (uint8_t)(0xFFu << (8 - bits))); }
static bool prio_masked(uint8_t basepri, uint8_t prio, int bits) {
    uint8_t b = prio_eff(basepri, bits);
    if (b == 0u) return false;                             /* BASEPRI = 0 → 아무것도 안 막는다 */
    return prio_eff(prio, bits) >= b;
}
static bool can_call_from_isr(uint8_t prio, uint8_t max_syscall, int bits) {
    return prio_eff(prio, bits) >= prio_eff(max_syscall, bits);   /* = 크리티컬 섹션에 막히는 쪽이어야 안전 */
}

/* ---------------------------------------------------------------- 연습 4 */
/* SysTick 주기 P(사이클), 크리티컬 섹션 시작 시점의 위상 phase(지난 tick 이후 경과, 0 ≤ phase < P), 길이 L.
 * 막힌 동안 지나간 tick 경계 수 = floor((phase + L) / P). 펜딩 비트는 1 개라 풀리는 순간 1 번만 처리된다. */
typedef struct { uint32_t crossed, processed, lost; } tick_loss_t;
static tick_loss_t tick_loss(uint32_t period, uint32_t phase, uint32_t len) {
    tick_loss_t t;
    t.crossed = (phase + len) / period;
    t.processed = t.crossed ? 1u : 0u;
    t.lost = t.crossed - t.processed;
    return t;
}
static uint32_t isr_runs_after_mask(uint32_t pends_while_masked) { return pends_while_masked ? 1u : 0u; }

/* ---------------------------------------------------------------- 연습 5 */
/* FreeRTOS portable/GCC/ARM_CM4F/port.c:202-231 pxPortInitialiseStack 과 같은 모양:
 * xPSR, PC, LR, r12, r3, r2, r1, r0, EXC_RETURN(0xFFFFFFFD), r11..r4 → 17 워드 */
static uint32_t *frame_init_cm4f(uint32_t *top, uint32_t fn, uint32_t arg, uint32_t exit_trap) {
    uint32_t *sp = top;
    *--sp = 0x01000000u;
    *--sp = fn & ~1u;
    *--sp = exit_trap;
    sp -= 5;                                               /* r12, r3, r2, r1 (값 안 넣음) */
    *sp = arg;                                             /* r0 */
    *--sp = 0xFFFFFFFDu;                                   /* 태스크별 EXC_RETURN: FPU 를 썼는지 PendSV 가 기억 */
    sp -= 8;                                               /* r11..r4 */
    return sp;
}

int main(void) {
    /* ---- 연습 1: gdb 로 실제 덤프한 L(선점됨) / H(막힘) 프레임 + 망가진 프레임 ---- */
    const lint_cfg_t L_cfg = { 0x000000C0u, 0x00003300u, 0x20001438u, 0x20001838u, 16u };
    const uint32_t L_sp = 0x200017f0u;
    const uint32_t L_w[16] = { 0x20000000u, 0x05050505u, 0x06060606u, 0x07070707u,
                               0x08080808u, 0x09090909u, 0x0a0a0a0au, 0x0b0b0b0bu,
                               0x00000001u, 0x200002ccu, 0x00000004u, 0xfffffffdu,
                               0x20000004u, 0x00000adbu, 0x00000aecu, 0xa1000000u };
    CHECK(frame_lint(L_sp, L_w, &L_cfg) == FL_OK);
    const lint_cfg_t H_cfg = { 0x000000C0u, 0x00003300u, 0x20000c38u, 0x20001038u, 16u };
    const uint32_t H_sp = 0x20000fc8u;
    const uint32_t H_w[16] = { 0x20000000u, 0x200002ccu, 0x20000274u, 0x07070707u,
                               0x08080808u, 0x09090909u, 0x0a0a0a0au, 0x0b0b0b0bu,
                               0x20000274u, 0x20000014u, 0x10000000u, 0x00000000u,
                               0x2000000cu, 0x00000003u, 0x000008ccu, 0x61000000u };
    CHECK(frame_lint(H_sp, H_w, &H_cfg) == FL_OK);           /* lr = 3 이어도 정상: 그 순간 LR 레지스터 값일 뿐 */
    uint32_t bad[16];
    for (int i = 0; i < 16; ++i) bad[i] = L_w[i];
    bad[15] = 0u;                                            /* F1: xPSR = 0 */
    CHECK(frame_lint(L_sp, bad, &L_cfg) == FL_NO_THUMB);
    bad[15] = L_w[15]; bad[14] = 0x00000aedu;                /* F2: PC bit0 = 1 */
    CHECK(frame_lint(L_sp, bad, &L_cfg) == FL_PC_ODD);
    bad[14] = 0x20000000u; bad[15] = 0x0100000Fu;            /* PC 가 RAM, IPSR = 15 (SysTick 안에서 저장?) */
    CHECK(frame_lint(L_sp, bad, &L_cfg) == (FL_PC_RANGE | FL_IPSR));
    CHECK(frame_lint(0x20001440u, L_w, &L_cfg) == FL_SP_RANGE);   /* guard 16 B 안 → 오버플로 직전 */
    CHECK(frame_lint(0x20001800u, L_w, &L_cfg) == FL_SP_RANGE);   /* 프레임 64 B 가 스택 끝을 넘음 */

    /* ---- 연습 2: EXC_RETURN ---- */
    exc_ret_t e = exc_return_decode(0xFFFFFFF9u);            /* main(MSP) 에서 svc 0 했을 때 SVC 의 LR */
    CHECK(e.valid && e.to_thread && !e.use_psp && e.frame_words == 8u);
    e = exc_return_decode(0xFFFFFFF9u | 0xDu);               /* SVC_Handler 의 orr lr, #0xd */
    CHECK(e.valid && e.to_thread && e.use_psp && e.frame_words == 8u && (0xFFFFFFF9u | 0xDu) == 0xFFFFFFFDu);
    e = exc_return_decode(0xFFFFFFF1u);                      /* 중첩 ISR → ISR 로 복귀 (항상 MSP) */
    CHECK(e.valid && !e.to_thread && !e.use_psp);
    e = exc_return_decode(0xFFFFFFEDu);                      /* M4F: FPU 쓴 태스크 → 26 워드 */
    CHECK(e.valid && e.to_thread && e.use_psp && e.frame_words == 26u);
    CHECK(!exc_return_decode(0xFFFFFFF5u).valid && !exc_return_decode(0x00000aecu).valid);

    /* ---- 연습 3: BASEPRI 문턱 ---- */
    CHECK(prio_masked(0x40, 0x80, 8) && !prio_masked(0x40, 0x20, 8));      /* 이 레슨의 IRQ30 / IRQ31 */
    CHECK(prio_masked(0x40, 0xFF, 8) && prio_masked(0x40, 0x40, 8));      /* SysTick·PendSV / 문턱과 같으면 막힘 */
    CHECK(!prio_masked(0x00, 0xFF, 8));                                    /* BASEPRI=0 은 "끄기" 가 아니라 "안 막음" */
    CHECK(prio_masked(0x40, 0x45, 4) && !prio_masked(0x40, 0x3F, 4));     /* 4 비트 칩: 0x45→0x40, 0x3F→0x30 */
    CHECK(prio_masked(0x50, 0x45, 8) == false && prio_masked(0x50, 0x45, 4) == false);
    CHECK(can_call_from_isr(0x80, 0x40, 8) && !can_call_from_isr(0x20, 0x40, 8));
    CHECK(!can_call_from_isr(0x00, 0x50, 4));                              /* 리셋 기본 우선순위 0 = 가장 급함 → 금지 */

    /* ---- 연습 4: 잃어버린 tick / 합쳐진 IRQ (Phase F 실측: 3 바퀴 막힘 → 1 tick, IRQ30 3 번 → 1 번) ---- */
    tick_loss_t t = tick_loss(25000u, 24990u, 3u * 25000u);
    CHECK(t.crossed == 3u && t.processed == 1u && t.lost == 2u);
    t = tick_loss(25000u, 100u, 24000u);                     /* 1 tick 보다 짧고 경계도 안 넘음 */
    CHECK(t.crossed == 0u && t.lost == 0u);
    t = tick_loss(25000u, 24000u, 2000u);                    /* 짧아도 경계를 넘으면 1 번 늦게 처리 (지연, 손실 없음) */
    CHECK(t.crossed == 1u && t.processed == 1u && t.lost == 0u);
    CHECK(isr_runs_after_mask(3u) == 1u && isr_runs_after_mask(0u) == 0u);

    /* ---- 연습 5: M4F 초기 프레임 ---- */
    uint32_t stk[64] = { 0 };
    uint32_t *top = stk + 64;
    uint32_t *sp = frame_init_cm4f(top, 0x00000aedu, 0xC0DE0001u, 0x00000551u);
    CHECK(top - sp == 17);
    CHECK(top[-1] == 0x01000000u && top[-2] == 0x00000aecu && top[-3] == 0x00000551u);
    CHECK(top[-8] == 0xC0DE0001u && top[-9] == 0xFFFFFFFDu);
    CHECK(exc_return_decode(top[-9]).frame_words == 8u);     /* 처음엔 FPU 안 쓴 것으로 시작 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
