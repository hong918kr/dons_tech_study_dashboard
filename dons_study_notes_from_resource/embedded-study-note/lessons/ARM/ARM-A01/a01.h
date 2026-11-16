/* ENV: qemu */
/* ARM-A01 공용 선언 — 모든 .c 가 include 한다 (TCB 레이아웃, 레지스터 주소, 태스크 통계 구조체).
 * asm(.S) 쪽은 같은 오프셋을 .equ 로 따로 적는다 → 아래 _Static_assert 가 둘이 어긋나지 않게 지킨다.
 */
#ifndef ARM_A01_H
#define ARM_A01_H
#include <stdint.h>
#include <stddef.h>

/* ---------------- 시스템 레지스터 (ARMv7-M SCB / SysTick / NVIC / FPU) ---------------- */
#define REG32(a)     (*(volatile uint32_t *)(uintptr_t)(a))
#define REG8(a)      (*(volatile uint8_t *)(uintptr_t)(a))
#define SCB_ICSR     REG32(0xE000ED04u)   /* bit28 PENDSVSET, bit27 PENDSVCLR, bit26 PENDSTSET, bit25 PENDSTCLR */
#define SCB_VTOR     REG32(0xE000ED08u)
#define SCB_SHPR2    REG32(0xE000ED1Cu)   /* [31:24] SVCall */
#define SCB_SHPR3    REG32(0xE000ED20u)   /* [31:24] SysTick, [23:16] PendSV */
#define SCB_CFSR     REG32(0xE000ED28u)
#define SCB_HFSR     REG32(0xE000ED2Cu)
#define SYST_CSR     REG32(0xE000E010u)
#define SYST_RVR     REG32(0xE000E014u)
#define SYST_CVR     REG32(0xE000E018u)
#define NVIC_ISER0   REG32(0xE000E100u)
#define NVIC_ICER0   REG32(0xE000E180u)
#define NVIC_ISPR0   REG32(0xE000E200u)
#define NVIC_ICPR0   REG32(0xE000E280u)
#define NVIC_IPR(n)  REG8(0xE000E400u + (n))
#define FPCCR        REG32(0xE000EF34u)   /* bit31 ASPEN, bit30 LSPEN, bit0 LSPACT */
#define ICSR_PENDSVSET (1u << 28)
#define ICSR_PENDSVCLR (1u << 27)
#define ICSR_PENDSTCLR (1u << 25)

#define IRQ_BUSY     6u                   /* QEMU an385/an386 에서 비어 있는 IRQ 줄 (BM-M02 측정) */
#define TICK_RELOAD  1000u                /* SysTick: CPU 클럭 1000 사이클마다 (QEMU mps2 = 25 MHz → 40 µs) */
#define CANARY       0xC0DEFACEu
#define EXC_RETURN_THREAD_PSP 0xFFFFFFFDu
#define XPSR_INIT    0x01000000u          /* T 비트(24)만 1 */

/* ---------------- TCB: asm 이 쓰는 칸은 앞쪽 고정 오프셋 ---------------- */
struct tcb {
    uint32_t *sp;            /* +0  PendSV 가 저장/복원하는 PSP (r4–r11 [+s16–s31] 까지 쌓은 뒤의 값) */
    uint32_t  id;            /* +4  0,1,2… (추적용) */
    uint32_t  exc_return;    /* +8  이 태스크로 돌아갈 때 쓸 EXC_RETURN (FPU 판 PendSV 가 저장/복원) */
    uint32_t *stack_lo;      /* +12 스택 배열 시작 (낮은 주소, 카나리 2 워드) */
    uint32_t  stack_words;   /* +16 스택 크기 (워드) */
    uint32_t  alive;         /* +20 0 = 끝났거나 오버플로로 죽음 → 스케줄 안 함 */
    char      name;          /* 'A', 'B', … */
};
_Static_assert(offsetof(struct tcb, sp) == 0, "asm TCB_SP");
_Static_assert(offsetof(struct tcb, exc_return) == 8, "asm TCB_EXCRET");

extern struct tcb *volatile g_cur;       /* asm 이 읽는다: 지금 도는 태스크 */
extern volatile uint32_t g_main_msp;     /* SVC 가 main 문맥을 MSP 에 쌓은 뒤의 sp */

/* ---------------- 커널 상태 (shared_os.c) ---------------- */
#define MAX_TASKS 4
#define TRACE_LEN 512
struct os {
    struct tcb *task[MAX_TASKS];
    uint32_t ntasks, ticks, tick_limit, stop;
    uint32_t switches;        /* 다른 태스크로 바꾼 횟수 */
    uint32_t pendsv_calls;    /* os_schedule 호출 횟수 (= PendSV 진입 횟수, 마지막 main 복귀 포함) */
    uint32_t from_handler;    /* PendSV 가 Handler 모드를 끊고 들어온 횟수 (EXC_RETURN bit3 == 0) */
    uint32_t from_main;
    uint32_t rounds_no_tick;  /* 틱 0 인 채로 돈 검사 라운드 수 (BASEPRI 를 안 풀었을 때의 워치독) */       /* PendSV 가 Thread+MSP(= main) 를 끊고 들어온 횟수 (bit3 = 1, bit2 = 0) */
    uint32_t last_exc_return; /* 마지막 PendSV 진입 lr */
    uint32_t overflow_id;     /* 스택 검사에 걸린 태스크 id + 1 (0 = 없음) */
    uint32_t overflow_sp;
    uint32_t exited_id;       /* exit trap 에 들어온 태스크 id + 1 */
    uint32_t exited_r0;
    uint32_t ntrace;
    char     trace[TRACE_LEN];/* 스위치마다 다음 태스크 이름 */
};
extern volatile struct os g_os;

/* 레지스터 검사 태스크 1개의 상태 (asm 태스크가 want[] 를 ldm 으로 읽는다) */
struct rc {
    uint32_t want[14];        /* +0  r0..r12, lr 에 넣을 패턴 (이번 라운드) */
    uint32_t fwant[32];       /* +56 s0..s31 패턴 (FPU 판 task_fpcheck) */
    uint32_t rng, spin_min, spin_mask;
    uint32_t rounds, mismatches, mism_mask, fmism_mask;
    uint32_t sw_at_prepare, windows_switched;
    uint32_t yield_every, yields, irq_every, irqs;
    uint32_t printed;
    char     name;
};
_Static_assert(offsetof(struct rc, want) == 0, "asm RC_WANT");
_Static_assert(offsetof(struct rc, fwant) == 56, "asm RC_FWANT");

/* 바쁜 IRQ(IRQ6) 의 r4–r11 검사 상태 */
struct irqstat {
    uint32_t want[8];
    uint32_t rng, runs, mismatches, mism_mask, ticks_in_window, windows_with_tick, tick_at_prepare;
};
extern volatile struct irqstat g_irq;

/* ---------------- TODO (starter/solution/practice 의 .c) ---------------- */
uint32_t *init_stack(struct tcb *t, void (*entry)(void *), void *arg);
void      os_tick(void);

/* ---------------- shared_os.c ---------------- */
struct tcb *os_schedule(uint32_t exc_return);    /* PendSV 가 r4–r11 저장 뒤 bl. NULL = main 으로 */
uint32_t os_run(struct tcb **list, uint32_t n, uint32_t tick_limit);
void     os_yield(void);
void     task_exit_c(uint32_t r0);              /* task_exit_trap(asm) 가 b 로 넘어온다 */
void     tcb_setup(struct tcb *t, uint32_t id, char name, uint32_t *stack_lo, uint32_t words);
void     rc_setup(struct rc *st, char name, uint32_t seed);
void     irq_setup(uint32_t seed);
extern uint32_t g_wd_ticks;
extern uint32_t g_os_msp_before, g_os_msp_after, g_os_control_after;

/* ---------------- shared_tasks.S ---------------- */
uint32_t os_start(void);                 /* main r4–r11 = 센티널 → svc #0 → … → 바뀐 레지스터 비트마스크 */
void     task_regcheck(void *st);        /* r0–r12, lr 패턴 검사 무한 루프 */
void     task_fpcheck(void *st);         /* + s0–s31 (HAS_FPU) */
void     task_exit_trap(void);           /* 초기 프레임 LR 칸: 태스크 함수가 return 하면 여기로 */
uint32_t get_msp(void), get_psp(void), get_control(void), get_ipsr(void);
void     set_basepri(uint32_t v);

/* ---------------- shared_startup.c ---------------- */
void sh_puts(const char *s);
void sh_putc(char c);
void sh_hex(uint32_t v);
void sh_dec(uint32_t v);
__attribute__((noreturn)) void sh_exit(int code);
void check(int ok, const char *expr, int line);
extern unsigned g_pass, g_total;
#define CHECK(e) check((e) ? 1 : 0, #e, __LINE__)
extern void (*const g_vectors[48])(void);

#endif
