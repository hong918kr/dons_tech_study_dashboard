/* ENV: qemu */
/* RTOS-A01 Cortex-M 미니 RTOS 완성: 우선순위 스케줄러 + 동기화
 *
 * 펌웨어 한 파일 = startup(벡터 테이블, Reset_Handler) + 포트 계층(초기 스택 프레임, SVC 첫 태스크 시작,
 * PendSV 컨텍스트 스위치, BASEPRI 크리티컬 섹션) + 커널(비트맵 스케줄러, delay, 세마포어, 상속 뮤텍스)
 * + 자기 검사(Phase A–G). 대상: qemu-system-arm -M mps2-an385 (Cortex-M3), 출력은 세미호스팅.
 * 빌드/실행 (Lima VM elinux 안, run_qemu.sh 가 /tmp/RTOS-A01_work 로 복사해서 한다):
 *   arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -std=c11 -O2 -g -Wall -Wextra -Werror \
 *     -ffreestanding -nostdlib -fno-tree-loop-distribute-patterns -T rtos_a01.ld <this>.c -lgcc
 *   qemu-system-arm -M mps2-an385 -nographic -monitor none -serial none \
 *     -semihosting-config enable=on,target=native -icount shift=3,align=off,sleep=off -kernel <elf>
 * 종료 코드: 0 = 전부 PASS, 1 = FAIL / HardFault / 워치독.
 * 우선순위: 태스크는 "숫자 클수록 급함"(FreeRTOS 방식), NVIC 는 "숫자 작을수록 급함"(하드웨어).
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[];

/* ======================= 세미호스팅 (BM-B08 과 같은 것) ======================= */
static int semi(int op, const void *arg) {
    register int r0 __asm__("r0") = op;
    register const void *r1 __asm__("r1") = arg;
    __asm__ volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}
static void sh_puts(const char *s) { semi(0x04, s); }                 /* SYS_WRITE0 */
static void sh_hex(uint32_t v) {
    char b[11];
    b[0] = '0'; b[1] = 'x'; b[10] = 0;
    for (int i = 9; i >= 2; --i, v >>= 4) b[i] = "0123456789abcdef"[v & 0xFu];
    sh_puts(b);
}
static void sh_dec(uint32_t v) {
    char b[11];
    int i = 10;
    b[i] = 0;
    do { b[--i] = (char)('0' + v % 10u); v /= 10u; } while (v);
    sh_puts(&b[i]);
}
__attribute__((noreturn)) static void sh_exit(int code) {
    uint32_t blk[2] = { 0x20026u, (uint32_t)code };                  /* ADP_Stopped_ApplicationExit */
    semi(0x20, blk);                                                 /* SYS_EXIT_EXTENDED → qemu exit = code */
    semi(0x18, (const void *)(uintptr_t)(code ? 0x20024u : 0x20026u));
    for (;;) { }
}

/* ======================= 하드웨어 레지스터 ======================= */
#define REG32(a)        (*(volatile uint32_t *)(uintptr_t)(a))
#define SCB_ICSR        REG32(0xE000ED04u)
#define SCB_VTOR        REG32(0xE000ED08u)
#define SCB_SHPR2       REG32(0xE000ED1Cu)      /* [31:24] SVCall 우선순위 */
#define SCB_SHPR3       REG32(0xE000ED20u)      /* [23:16] PendSV, [31:24] SysTick */
#define SCB_CFSR        REG32(0xE000ED28u)
#define SCB_HFSR        REG32(0xE000ED2Cu)
#define SYST_CSR        REG32(0xE000E010u)
#define SYST_RVR        REG32(0xE000E014u)
#define SYST_CVR        REG32(0xE000E018u)
#define NVIC_ISER0      REG32(0xE000E100u)
#define NVIC_ISPR0      REG32(0xE000E200u)
#define NVIC_IPR(n)     (*(volatile uint8_t *)(uintptr_t)(0xE000E400u + (n)))
#define NVIC_STIR       REG32(0xE000EF00u)      /* 소프트웨어로 IRQ n 펜딩: STIR = n */
#define ICSR_PENDSVSET  (1u << 28)
#define ICSR_PENDSTSET  (1u << 26)
#define CSR_COUNTFLAG   (1u << 16)

/* ======================= 포트 설정 ======================= */
#define TICK_RELOAD       (25000u - 1u) /* mps2-an385 SYSCLK 25 MHz → 1 ms tick */
#define KERNEL_BASEPRI    0x40u         /* = configMAX_SYSCALL_INTERRUPT_PRIORITY. 이 값 이상(덜 급한) IRQ 를 막는다 */
#define PRIO_IRQ_HIGH     0x20u         /* IRQ31 "모터 과전류": 문턱 위 → 크리티컬 섹션에서도 실행, 커널 호출 금지 */
#define PRIO_IRQ_SYSCALL  0x80u         /* IRQ30 "UART RX": 문턱 아래 → 크리티컬 섹션 동안 대기, *_from_isr 허용 */
#define PRIO_LOWEST       0xFFu         /* PendSV · SysTick (FreeRTOS portMIN_INTERRUPT_PRIORITY = 255) */
#define IRQ_SYSCALL       30u           /* mps2-an385 에서 장치가 안 쓰는 줄 (아래 Phase A 에서 0 회 확인) */
#define IRQ_HIGH          31u
#define WATCHDOG_TICKS    400u          /* 이 tick 이 지나도 안 끝나면 FAIL 로 종료 (시작 코드가 매달리지 않게) */
#define WAIT_FOREVER      0xFFFFFFFFu
#define MAX_PRIO          32u
#define MAX_TASKS         16u
#define STACK_WORDS       256u          /* 1 KB / 태스크 */
#define STACK_FILL        0xA5A5A5A5u   /* 하이 워터 마크 측정용 칠하기 (RTOS-M08/M09) */
#define GUARD_WORDS       4u            /* 바닥 16 바이트가 칠한 값 그대로인지 매 스위치마다 검사 */

static inline void basepri_set(uint32_t v) { __asm__ volatile("msr basepri, %0\n isb\n dsb" :: "r"(v) : "memory"); }
static inline uint32_t basepri_get(void) { uint32_t v; __asm__ volatile("mrs %0, basepri" : "=r"(v)); return v; }
static inline uint32_t psp_get(void) { uint32_t v; __asm__ volatile("mrs %0, psp" : "=r"(v)); return v; }
static inline uint32_t msp_get(void) { uint32_t v; __asm__ volatile("mrs %0, msp" : "=r"(v)); return v; }
static inline uint32_t control_get(void) { uint32_t v; __asm__ volatile("mrs %0, control" : "=r"(v)); return v; }
static inline uint32_t ipsr_get(void) { uint32_t v; __asm__ volatile("mrs %0, ipsr" : "=r"(v)); return v; }
static inline void barrier(void) { __asm__ volatile("dsb\n isb" ::: "memory"); }

/* ======================= 커널 자료구조 ======================= */
typedef enum { TASK_READY, TASK_RUNNING, TASK_BLOCKED, TASK_SUSPENDED } task_state_t;
struct waitq;
typedef struct tcb {
    uint32_t *sp;               /* [0] 반드시 첫 멤버: SVC/PendSV 어셈블리가 [tcb+0] 을 읽고 쓴다 */
    const char *name;
    uint8_t prio, base_prio;    /* prio = 지금 유효(상속 포함), base_prio = 만들 때 준 값 */
    uint8_t held;               /* 쥐고 있는 뮤텍스 수 (FreeRTOS uxMutexesHeld) */
    task_state_t state;
    bool has_timeout;
    uint32_t wake_tick;         /* BLOCKED + 타임아웃일 때 깨어날 tick (랩 안전 비교) */
    struct waitq *wait_obj;     /* 기다리는 세마포어/뮤텍스 (delay 면 NULL) */
    struct tcb *next;           /* ready FIFO 또는 wait list 링크 — 한 번에 한 곳에만 */
    bool wait_ok;               /* 깨어난 이유: true = 받음, false = 타임아웃 */
    volatile uint32_t run_ticks;/* SysTick 순간 실행 중이던 태스크에 1 씩 (burn 의 시계) */
    uint32_t *stack_lo, *stack_hi;
} tcb_t;

typedef struct waitq { tcb_t *head; struct mutex *mtx; } waitq_t;   /* 우선순위 순(같으면 FIFO) */
typedef struct { waitq_t wq; uint32_t count, max; } sem_t;
typedef struct mutex { waitq_t wq; tcb_t *owner; bool inherit; } mutex_t;

tcb_t *volatile g_cur;                       /* = pxCurrentTCB. asm 이 이름으로 참조하므로 static 아님 */
static uint32_t g_ready_bits;                /* bit p = 우선순위 p 에 READY/RUNNING 태스크 있음 */
static tcb_t *g_ready_head[MAX_PRIO], *g_ready_tail[MAX_PRIO];
static tcb_t *g_tasks[MAX_TASKS];
static uint32_t g_ntasks;
static volatile uint32_t g_tick;
static volatile uint32_t g_crit_nesting;
static volatile uint32_t g_switches, g_stack_errors, g_api_errors;

/* 테스트 집계 (전역: 여러 태스크가 CHECK 를 부른다. 단일 코어 + 한 번에 한 태스크만 검사) */
static volatile uint32_t g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } else { \
    sh_puts("FAIL " __FILE__ ":"); sh_dec(__LINE__); sh_puts("  " #expr "\n"); } } while (0)
__attribute__((noreturn)) static void report_and_exit(void) {
    sh_dec(g_pass); sh_puts("/"); sh_dec(g_total); sh_puts(" passed\n");
    sh_exit(g_pass == g_total ? 0 : 1);
}

/* ---------------- 트레이스: tick 마다 "방금 끝난 1 tick 동안" 누가 돌았나 ---------------- */
#define TRACE_LEN 16u
static tcb_t *g_trace_task[3];
static char g_trace_st[3][TRACE_LEN];
static uint8_t g_trace_prio[3][TRACE_LEN];
static uint32_t g_trace_t0;
static volatile bool g_trace_on;

static void trace_record(void) {             /* kernel_tick 안 (BASEPRI 올린 상태) */
    uint32_t i = g_tick - 1u - g_trace_t0;   /* 칸 i = 구간 [t0+i, t0+i+1) */
    if (!g_trace_on || i >= TRACE_LEN) return;
    for (int k = 0; k < 3; ++k) {
        tcb_t *t = g_trace_task[k];
        if (!t) continue;
        char c = (t == g_cur) ? 'R' : t->state == TASK_READY ? 'r'
               : t->state == TASK_SUSPENDED ? '.' : t->wait_obj ? 'B' : 'D';
        g_trace_st[k][i] = c;
        g_trace_prio[k][i] = t->prio;
    }
}

/* ======================= 비트맵 ready list (RTOS-B04) ======================= */
static inline uint32_t clz32(uint32_t x) { uint32_t r; __asm__("clz %0, %1" : "=r"(r) : "r"(x)); return r; }

static void ready_push(tcb_t *t) {           /* 그 우선순위 FIFO 꼬리에 */
    t->next = NULL;
    if (g_ready_tail[t->prio]) g_ready_tail[t->prio]->next = t; else g_ready_head[t->prio] = t;
    g_ready_tail[t->prio] = t;
    g_ready_bits |= 1u << t->prio;
}
static void ready_remove(tcb_t *t) {
    tcb_t **pp = &g_ready_head[t->prio], *prev = NULL;
    while (*pp && *pp != t) { prev = *pp; pp = &(*pp)->next; }
    if (!*pp) return;
    *pp = t->next;
    if (g_ready_tail[t->prio] == t) g_ready_tail[t->prio] = prev;
    if (!g_ready_head[t->prio]) g_ready_bits &= ~(1u << t->prio);
    t->next = NULL;
}
static uint32_t sched_top_prio(void) { return 31u - clz32(g_ready_bits); }   /* IDLE(0) 이 늘 READY → bits ≠ 0 */
static tcb_t *sched_pick_next(void) { return g_ready_head[sched_top_prio()]; }

static void yield_request(void) { SCB_ICSR = ICSR_PENDSVSET; barrier(); }   /* = portYIELD() */
static void maybe_yield(void) { if (g_cur && sched_top_prio() > g_cur->prio) yield_request(); }

static void task_set_prio(tcb_t *t, uint8_t p) {   /* READY/RUNNING 이면 새 우선순위 FIFO 로 옮긴다 */
    if (t->prio == p) return;
    bool queued = (t->state == TASK_READY || t->state == TASK_RUNNING);
    if (queued) ready_remove(t);
    t->prio = p;
    if (queued) ready_push(t);
}

/* ======================= TODO 4: BASEPRI 크리티컬 섹션 ======================= */
/* 태스크용: 중첩 카운트 + BASEPRI = KERNEL_BASEPRI. 0 으로 돌아올 때만 BASEPRI = 0.
 * ISR 용: 이전 BASEPRI 를 돌려주고(ulPortRaiseBASEPRI), 끝에서 그 값으로 복원 — ISR 은 중첩될 수 있으니까. */
void crit_enter(void) {
    g_crit_nesting++;                        /* TODO 4: BASEPRI 올리기 */
}
void crit_exit(void) {
    g_crit_nesting--;                        /* TODO 4: 0 이 되면 BASEPRI = 0 */
}
static uint32_t isr_mask(void) {
    return 0u;                               /* TODO 4: 이전 BASEPRI 저장 + 올리기 */
}
static void isr_unmask(uint32_t old) { (void)old; /* TODO 4 */ }

/* ======================= wait list (우선순위 순, 같으면 FIFO) ======================= */
static void waitq_insert(waitq_t *q, tcb_t *t) {
    tcb_t **pp = &q->head;
    while (*pp && (*pp)->prio >= t->prio) pp = &(*pp)->next;
    t->next = *pp;
    *pp = t;
}
static void waitq_remove(waitq_t *q, tcb_t *t) {
    for (tcb_t **pp = &q->head; *pp; pp = &(*pp)->next)
        if (*pp == t) { *pp = t->next; t->next = NULL; return; }
}
static tcb_t *waitq_pop(waitq_t *q) {
    tcb_t *t = q->head;
    if (t) { q->head = t->next; t->next = NULL; }
    return t;
}
static void wake(tcb_t *t, bool ok) {        /* BLOCKED → READY (wait list 에서는 이미 빠진 상태) */
    t->wait_obj = NULL;
    t->has_timeout = false;
    t->wait_ok = ok;
    t->state = TASK_READY;
    ready_push(t);
}
static void block_current(waitq_t *q, uint32_t timeout) {   /* crit 안에서. 스위치는 crit_exit 때 */
    tcb_t *me = g_cur;
    ready_remove(me);
    me->state = TASK_BLOCKED;
    me->wait_obj = q;
    me->wait_ok = false;
    me->has_timeout = (timeout != WAIT_FOREVER);
    me->wake_tick = g_tick + timeout;
    if (q) waitq_insert(q, me);
    yield_request();                         /* PendSV 펜딩 — BASEPRI 가 내려가는 순간 실행 */
}
static void mutex_recompute_owner(mutex_t *m) {   /* 대기자가 타임아웃으로 빠졌을 때 (상속 되돌리기) */
    tcb_t *o = m->owner;
    if (!o || !m->inherit || o->held != 1u) return;   /* 여러 개 쥐면 RTOS-M04 Part B 정책 — 여기선 생략 */
    uint8_t p = o->base_prio;
    if (m->wq.head && m->wq.head->prio > p) p = m->wq.head->prio;
    task_set_prio(o, p);
}

/* ======================= tick (SysTick 이 부른다) ======================= */
static void task_suspend_locked(tcb_t *t) { ready_remove(t); t->state = TASK_SUSPENDED; }

static bool kernel_tick(void) {
    g_tick++;
    if (g_cur) g_cur->run_ticks++;
    trace_record();
    for (uint32_t i = 0; i < g_ntasks; ++i) {
        tcb_t *t = g_tasks[i];
        if (t->state != TASK_BLOCKED || !t->has_timeout || (int32_t)(g_tick - t->wake_tick) < 0) continue;
        waitq_t *q = t->wait_obj;
        if (q) waitq_remove(q, t);
        wake(t, false);
        if (q && q->mtx) mutex_recompute_owner(q->mtx);
    }
    if (g_tick >= WATCHDOG_TICKS) {
        sh_puts("WATCHDOG: tick "); sh_dec(g_tick); sh_puts(" — 테스트가 끝나지 않았다\n");
        g_total++;
        report_and_exit();
    }
    return g_cur && sched_top_prio() > g_cur->prio;
}

void SysTick_Handler(void) {
    uint32_t m = isr_mask();                 /* xPortSysTickHandler 와 같은 모양 */
    if (kernel_tick()) SCB_ICSR = ICSR_PENDSVSET;
    isr_unmask(m);
}

/* PendSV 가 부른다 (BASEPRI 올린 상태). 다음 태스크를 고르고 g_cur 를 바꾼다 = vTaskSwitchContext */
void kernel_switch_context(void) {
    tcb_t *prev = g_cur;
    if (prev->sp < prev->stack_lo + GUARD_WORDS || prev->sp > prev->stack_hi) g_stack_errors++;
    for (uint32_t i = 0; i < GUARD_WORDS; ++i)
        if (prev->stack_lo[i] != STACK_FILL) { g_stack_errors++; break; }
    tcb_t *next = sched_pick_next();
    if (prev->state == TASK_RUNNING) prev->state = TASK_READY;
    next->state = TASK_RUNNING;
    if (next != prev) g_switches++;
    g_cur = next;
}

/* ======================= TODO 2: PendSV 컨텍스트 스위치 ======================= */
/* 진입 시 하드웨어가 이미 PSP 에 r0-r3, r12, lr, pc, xPSR 을 쌓았다. 나머지 r4-r11 을 소프트웨어가.
 * 1) r0 = PSP, r4-r11 을 그 아래에 저장, 새 꼭대기를 g_cur->sp 에
 * 2) BASEPRI 올리고 kernel_switch_context() 호출 (lr 은 EXC_RETURN — 호출 전에 MSP 에 보관)
 * 3) 새 g_cur->sp 에서 r4-r11 복원, PSP 설정, bx lr (EXC_RETURN 0xFFFFFFFD → 하드웨어가 나머지 복원) */
__attribute__((naked)) void PendSV_Handler(void) {
    __asm__ volatile(
        "   bx lr                    \n"   /* TODO 2: 저장 → kernel_switch_context → 복원 */
        :: "i"(KERNEL_BASEPRI));
}

/* ======================= TODO 3: SVC 로 첫 태스크 시작 ======================= */
/* g_cur->sp 는 task_stack_init 이 만든 "가짜로 스위치아웃된" 프레임을 가리킨다.
 * r4-r11 을 꺼내고 PSP 를 맞춘 뒤, BASEPRI = 0, EXC_RETURN = 0xFFFFFFFD (Thread 모드 + PSP) 로 복귀.
 * 예외 복귀가 하드웨어 언스택(r0-r3, r12, lr, pc, xPSR)을 해 주므로 첫 태스크도 "평소 재개" 와 같은 길로 들어간다. */
__attribute__((naked)) void SVC_Handler(void) {
    __asm__ volatile(
        "   bx lr                    \n"); /* TODO 3: g_cur->sp 에서 r4-r11, PSP, BASEPRI=0, EXC_RETURN 0xFFFFFFFD */
}

/* MSP 를 벡터 [0] 값으로 되돌리고 (main 의 스택은 이제 ISR 전용), 인터럽트를 켜고 svc 0.
 * SVC_Handler 가 태스크로 넘어가지 못하면 다음 줄 start_failed 로 떨어진다 (시작 코드용 안전망). */
__attribute__((noreturn, used)) static void start_failed(void) {
    sh_puts("FAIL: svc 0 이 돌아왔다 — SVC_Handler 가 첫 태스크로 넘어가지 않았다\n");
    g_total++;
    report_and_exit();
}
__attribute__((naked, noreturn)) static void start_first_task(void) {
    __asm__ volatile(
        "   ldr r0, =0xE000ED08      \n"   /* VTOR */
        "   ldr r0, [r0]             \n"
        "   ldr r0, [r0]             \n"   /* 벡터 [0] = _estack */
        "   msr msp, r0              \n"
        "   cpsie i                  \n"
        "   cpsie f                  \n"
        "   dsb                      \n"
        "   isb                      \n"
        "   svc 0                    \n"
        "   b start_failed           \n"
        "   .ltorg                   \n");
}

/* ======================= TODO 1: 태스크 초기 스택 프레임 ======================= */
/* top = 스택 배열 끝(8 바이트 정렬). PendSV 가 막 저장한 것처럼 16 워드를 쌓고 새 sp 를 돌려준다.
 *   top-1 xPSR = 0x01000000 (T 비트)   top-2 PC = fn & ~1   top-3 LR = task_exit_trap
 *   top-4..top-7 r12, r3, r2, r1        top-8 r0 = arg       top-9..top-16 r11..r4  → return top-16 */
void task_exit_trap(void);
#define FRAME_FILL(r) (0x01010101u * (r))     /* gdb 에서 알아보기 쉽게: r4 = 0x04040404 ... */
uint32_t *task_stack_init(uint32_t *top, void (*fn)(void *), void *arg) {
    (void)fn; (void)arg;                                  /* TODO 1: 16 워드 프레임을 쌓는다 */
    return top;
}

/* ======================= 태스크 API ======================= */
static void task_create(tcb_t *t, const char *name, void (*fn)(void *), void *arg, uint8_t prio,
                        uint32_t *stack, uint32_t words) {
    crit_enter();
    bool known = false;
    for (uint32_t i = 0; i < g_ntasks; ++i) known |= (g_tasks[i] == t);
    if (!known && g_ntasks < MAX_TASKS) g_tasks[g_ntasks++] = t;
    for (uint32_t i = 0; i < words; ++i) stack[i] = STACK_FILL;
    t->name = name;
    t->prio = t->base_prio = prio;
    t->held = 0;
    t->has_timeout = false;
    t->wait_obj = NULL;
    t->wait_ok = false;
    t->run_ticks = 0;
    t->stack_lo = stack;
    t->stack_hi = stack + words;
    t->sp = task_stack_init(stack + words, fn, arg);
    t->state = TASK_READY;
    ready_push(t);
    maybe_yield();
    crit_exit();
}
static void task_delay(uint32_t ticks) {
    if (ticks == 0u) return;
    crit_enter();
    block_current(NULL, ticks);
    crit_exit();
}
static void delay_to(uint32_t abs_tick) {
    int32_t d = (int32_t)(abs_tick - g_tick);
    if (d > 0) task_delay((uint32_t)d);
}
static void task_suspend_self(void) {
    crit_enter();
    task_suspend_locked(g_cur);
    yield_request();
    crit_exit();
}
static volatile uint32_t g_exit_trap_hits;
static tcb_t *volatile g_exit_trap_task;
void task_exit_trap(void) {                  /* = prvTaskExitError. 여기선 기록 후 스스로 멈춘다 */
    g_exit_trap_hits++;
    g_exit_trap_task = g_cur;
    task_suspend_self();
    for (;;) { }
}
static void burn(uint32_t n) {               /* "CPU 를 n tick 쓴다" — 선점당한 동안은 안 센다 */
    tcb_t *me = g_cur;
    uint32_t target = me->run_ticks + n;
    while ((int32_t)(me->run_ticks - target) < 0) { __asm__ volatile("nop"); }
}

/* ---------------- 세마포어 (카운팅, 깨울 때 토큰을 바로 넘긴다) ---------------- */
static void sem_init(sem_t *s, uint32_t count, uint32_t max) { s->wq.head = NULL; s->wq.mtx = NULL; s->count = count; s->max = max; }
static bool sem_give_locked(sem_t *s) {
    tcb_t *w = waitq_pop(&s->wq);
    if (w) { wake(w, true); return g_cur && w->prio > g_cur->prio; }
    if (s->count < s->max) s->count++;
    return false;
}
static bool sem_take(sem_t *s, uint32_t timeout) {
    crit_enter();
    if (s->count > 0u) { s->count--; crit_exit(); return true; }
    if (timeout == 0u) { crit_exit(); return false; }
    block_current(&s->wq, timeout);
    crit_exit();                             /* ← 여기서 PendSV. 받거나 타임아웃되면 이 다음 줄부터 */
    return g_cur->wait_ok;
}
static void sem_give(sem_t *s) {
    crit_enter();
    if (sem_give_locked(s)) yield_request();
    crit_exit();
}
static bool sem_give_from_isr(sem_t *s) {    /* 돌려준 값 = *pxHigherPriorityTaskWoken */
    uint32_t m = isr_mask();
    bool woken = sem_give_locked(s);
    isr_unmask(m);
    return woken;
}

/* ---------------- 뮤텍스 (소유자 + 선택적 우선순위 상속) ---------------- */
static void mutex_init(mutex_t *m, bool inherit) { m->wq.head = NULL; m->wq.mtx = m; m->owner = NULL; m->inherit = inherit; }
static bool mutex_lock(mutex_t *m, uint32_t timeout) {
    crit_enter();
    tcb_t *me = g_cur;
    if (m->owner == NULL) { m->owner = me; me->held++; crit_exit(); return true; }
    if (timeout == 0u) { crit_exit(); return false; }
    /* TODO 5a: 상속 — 소유자가 나보다 낮으면 소유자를 내 우선순위로 (xTaskPriorityInherit) */
    block_current(&m->wq, timeout);
    crit_exit();
    return me->wait_ok;                      /* true 면 unlock 이 소유권을 넘겨준 상태 */
}
static void mutex_unlock(mutex_t *m) {
    crit_enter();
    tcb_t *me = g_cur;
    if (m->owner != me) { g_api_errors++; crit_exit(); return; }
    me->held--;
    /* TODO 5b: 되돌리기 — 더 쥔 뮤텍스가 없으면 base_prio 로 (xTaskPriorityDisinherit) */
    tcb_t *w = waitq_pop(&m->wq);
    m->owner = w;
    if (w) { w->held++; wake(w, true); }
    maybe_yield();
    crit_exit();
}

/* ======================= ISR 두 개 ======================= */
static sem_t g_isr_sem;                      /* IRQ30 → 태스크 (RTOS-B08 의 바이너리 세마포어 자리) */
static volatile uint32_t g_irq_sys_count, g_irq_high_count, g_irq_high_saw_basepri;

void IRQ30_Handler(void) {                   /* "UART RX": 문턱 아래 — 커널 호출 허용 */
    g_irq_sys_count++;
    if (sem_give_from_isr(&g_isr_sem)) SCB_ICSR = ICSR_PENDSVSET;   /* portYIELD_FROM_ISR */
}
void IRQ31_Handler(void) {                   /* "모터 과전류": 문턱 위 — 커널 호출 금지, 즉시 처리만 */
    g_irq_high_count++;
    g_irq_high_saw_basepri = basepri_get();
}

/* ======================= HardFault: 보고하고 exit 1 (매달리지 않게) ======================= */
__attribute__((used, noreturn)) static void hardfault_report(const uint32_t *frame, uint32_t exc_return) {
    sh_puts("HardFault! CFSR="); sh_hex(SCB_CFSR);
    sh_puts(" HFSR="); sh_hex(SCB_HFSR);
    sh_puts(" EXC_RETURN="); sh_hex(exc_return);
    sh_puts("\n  stacked pc="); sh_hex(frame[6]);
    sh_puts(" lr="); sh_hex(frame[5]);
    sh_puts(" xpsr="); sh_hex(frame[7]);
    sh_puts("\n");
    g_total++;
    report_and_exit();
}
__attribute__((naked)) void HardFault_Handler(void) {
    __asm__ volatile(
        "   tst lr, #4            \n"
        "   ite eq                \n"
        "   mrseq r0, msp         \n"
        "   mrsne r0, psp         \n"
        "   mov r1, lr            \n"
        "   b hardfault_report    \n");
}

/* ======================= 테스트 시나리오 ======================= */
static uint32_t stk_ctrl[STACK_WORDS] __attribute__((aligned(8)));
static uint32_t stk_idle[STACK_WORDS] __attribute__((aligned(8)));
static uint32_t stk_t[6][STACK_WORDS] __attribute__((aligned(8)));
static tcb_t t_ctrl, t_idle, t_h, t_m, t_l, t_w, t_c;

static char g_log[32];
static volatile uint32_t g_log_n;
static void log_ch(char c) { crit_enter(); if (g_log_n < sizeof g_log - 1u) g_log[g_log_n++] = c; crit_exit(); }
static void log_reset(void) { crit_enter(); g_log_n = 0; for (uint32_t i = 0; i < sizeof g_log; ++i) g_log[i] = 0; crit_exit(); }
static bool str_eq(const char *a, const char *b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }

static void idle_fn(void *arg) { (void)arg; for (;;) __asm__ volatile("wfi"); }

/* Phase C: 선점 + delay. 세 태스크 모두 t0 에 풀려난다 */
static uint32_t g_t0, g_ev_tick[3];
static void phc_h(void *a) { (void)a; delay_to(g_t0); log_ch('H'); task_delay(2); g_ev_tick[0] = g_tick; log_ch('h'); task_suspend_self(); }
static void phc_m(void *a) { (void)a; delay_to(g_t0); log_ch('M'); task_delay(4); g_ev_tick[1] = g_tick; log_ch('m'); task_suspend_self(); }
static void phc_l(void *a) { (void)a; delay_to(g_t0); log_ch('L'); burn(8); g_ev_tick[2] = g_tick; log_ch('l'); task_suspend_self(); }

/* Phase D: ISR → 세마포어 → 높은 태스크가 ISR 끝에서 바로 */
static void phd_w(void *a) { (void)a; for (;;) { if (sem_take(&g_isr_sem, WAIT_FOREVER)) log_ch('X'); } }
static sem_t g_slots;
static volatile uint32_t g_c_got, g_c_to_ok, g_c_to_dt, g_c_wake_tick;
static void phd_c(void *a) {
    (void)a;
    for (int i = 0; i < 3; ++i) g_c_got += sem_take(&g_slots, 0) ? 1u : 0u;
    uint32_t s = g_tick;
    g_c_to_ok = sem_take(&g_slots, 3) ? 1u : 0u;      /* 비었으니 3 tick 뒤 타임아웃 */
    g_c_to_dt = g_tick - s;
    if (sem_take(&g_slots, WAIT_FOREVER)) g_c_wake_tick = g_tick;
    task_suspend_self();
}

/* Phase E: 우선순위 역전 (RTOS-M03/M04 의 H/M/L 숫자 그대로) */
static mutex_t g_res;
static volatile uint32_t g_done[3];          /* H, M, L 끝난 tick - t0 */
static void phe_l(void *a) { (void)a; delay_to(g_t0); mutex_lock(&g_res, WAIT_FOREVER); burn(4); mutex_unlock(&g_res); burn(1); g_done[2] = g_tick - g_t0; task_suspend_self(); }
static void phe_h(void *a) { (void)a; delay_to(g_t0 + 1u); mutex_lock(&g_res, WAIT_FOREVER); burn(1); mutex_unlock(&g_res); g_done[0] = g_tick - g_t0; task_suspend_self(); }
__attribute__((noinline)) void gdb_probe(void) { __asm__ volatile("" ::: "memory"); }   /* gdb 브레이크포인트 자리 */
static volatile bool g_probe_armed;
static void phe_m(void *a) { (void)a; delay_to(g_t0 + 2u); if (g_probe_armed) gdb_probe(); burn(6); g_done[1] = g_tick - g_t0; task_suspend_self(); }

/* Phase E': 상속 중 대기자가 타임아웃 → 소유자 우선순위 되돌림 */
static volatile uint32_t g_to_ok, g_to_tick;
static void phf_l(void *a) { (void)a; delay_to(g_t0); mutex_lock(&g_res, WAIT_FOREVER); burn(6); mutex_unlock(&g_res); g_done[2] = g_tick - g_t0; task_suspend_self(); }
static void phf_h(void *a) { (void)a; delay_to(g_t0 + 1u); g_to_ok = mutex_lock(&g_res, 2) ? 1u : 0u; g_to_tick = g_tick - g_t0; task_suspend_self(); }

/* Phase G: return 하는 태스크 → LR 칸의 task_exit_trap */
static volatile uint32_t g_r_arg;
static void phg_r(void *a) { g_r_arg = (uint32_t)(uintptr_t)a; }

static void trace_print(const char *title) {
    char line[16 + 2 * TRACE_LEN];
    sh_puts(title);
    sh_puts("\n  tick   :");
    for (uint32_t i = 0; i < TRACE_LEN; ++i) { line[2 * i] = ' '; line[2 * i + 1] = (char)('0' + i % 10u); }
    line[2 * TRACE_LEN] = 0; sh_puts(line); sh_puts("\n");
    for (int k = 0; k < 3; ++k) {
        tcb_t *t = g_trace_task[k];
        sh_puts("  "); sh_puts(t->name); sh_puts("(");
        sh_dec(t->base_prio); sh_puts(")   :");
        bool released = false;
        for (uint32_t i = 0; i < TRACE_LEN; ++i) {
            char c = g_trace_st[k][i];
            if (c == 'R' || c == 'r') released = true;
            if (c == 'D') c = released ? 'B' : '.';
            line[2 * i] = ' '; line[2 * i + 1] = c ? c : '.';
        }
        sh_puts(line); sh_puts("\n");
    }
    sh_puts("  L prio :");
    for (uint32_t i = 0; i < TRACE_LEN; ++i) { line[2 * i] = ' '; line[2 * i + 1] = g_trace_st[2][i] ? (char)('0' + g_trace_prio[2][i]) : '.'; }
    sh_puts(line); sh_puts("\n");
}
static void trace_start(tcb_t *a, tcb_t *b, tcb_t *c, uint32_t t0) {
    crit_enter();
    g_trace_task[0] = a; g_trace_task[1] = b; g_trace_task[2] = c;
    for (int k = 0; k < 3; ++k) for (uint32_t i = 0; i < TRACE_LEN; ++i) { g_trace_st[k][i] = 0; g_trace_prio[k][i] = 0; }
    g_trace_t0 = t0;
    g_trace_on = true;
    crit_exit();
}

static void run_inversion(bool inherit) {
    mutex_init(&g_res, inherit);
    g_done[0] = g_done[1] = g_done[2] = 0;
    g_t0 = g_tick + 2u;
    trace_start(&t_h, &t_m, &t_l, g_t0);
    task_create(&t_l, "L", phe_l, NULL, 1, stk_t[2], STACK_WORDS);
    task_create(&t_m, "M", phe_m, NULL, 2, stk_t[1], STACK_WORDS);
    task_create(&t_h, "H", phe_h, NULL, 3, stk_t[0], STACK_WORDS);
    delay_to(g_t0 + 16u);
    g_trace_on = false;
    trace_print(inherit ? "[E] 우선순위 역전 — 상속 ON (mutex)" : "[E] 우선순위 역전 — 상속 OFF (binary semaphore 처럼)");
}

static uint32_t stack_used_bytes(const tcb_t *t) {
    uint32_t n = 0;
    while (t->stack_lo + n < t->stack_hi && t->stack_lo[n] == STACK_FILL) n++;
    return (uint32_t)(t->stack_hi - t->stack_lo - n) * 4u;
}

static void ctrl_fn(void *arg) {
    /* ---- Phase B: 첫 태스크가 SVC 로 시작됐다: 레지스터 상태 ---- */
    uint32_t ctrl = control_get(), psp = psp_get(), msp = msp_get();
    sh_puts("[B] first task: CONTROL="); sh_hex(ctrl); sh_puts(" PSP="); sh_hex(psp);
    sh_puts(" MSP="); sh_hex(msp); sh_puts(" BASEPRI="); sh_hex(basepri_get());
    sh_puts(" arg="); sh_hex((uint32_t)(uintptr_t)arg); sh_puts("\n");
    CHECK((uint32_t)(uintptr_t)arg == 0xC0DE0001u);                 /* r0 칸 → 첫 인자 */
    CHECK(ctrl == 0x2u);                                             /* SPSEL=1 (PSP), nPRIV=0 (특권) */
    CHECK(ipsr_get() == 0u);                                         /* Thread 모드 */
    CHECK(psp > (uint32_t)(uintptr_t)stk_ctrl && psp <= (uint32_t)(uintptr_t)(stk_ctrl + STACK_WORDS));
    CHECK(msp == (uint32_t)(uintptr_t)_estack - 32u);                /* main 스택 반납 + SVC 진입 프레임 32 B 가 남는다 */
    CHECK(basepri_get() == 0u);

    /* ---- Phase C: 비트맵 스케줄러 + delay + SysTick 선점 ---- */
    log_reset();
    g_t0 = g_tick + 2u;
    task_create(&t_l, "L", phc_l, NULL, 1, stk_t[2], STACK_WORDS);
    task_create(&t_m, "M", phc_m, NULL, 2, stk_t[1], STACK_WORDS);
    task_create(&t_h, "H", phc_h, NULL, 3, stk_t[0], STACK_WORDS);
    delay_to(g_t0 + 12u);
    sh_puts("[C] log="); sh_puts(g_log); sh_puts(" h@+"); sh_dec(g_ev_tick[0] - g_t0);
    sh_puts(" m@+"); sh_dec(g_ev_tick[1] - g_t0); sh_puts(" l@+"); sh_dec(g_ev_tick[2] - g_t0); sh_puts("\n");
    CHECK(str_eq(g_log, "HMLhml"));
    CHECK(g_ev_tick[0] - g_t0 == 2u && g_ev_tick[1] - g_t0 == 4u && g_ev_tick[2] - g_t0 == 8u);

    /* ---- Phase D: IRQ30 → sem_give_from_isr → 더 높은 W 가 ISR 끝에서 바로 ---- */
    sem_init(&g_isr_sem, 0, 1);
    log_reset();
    task_create(&t_w, "W", phd_w, NULL, 12, stk_t[3], STACK_WORDS);   /* CTRL(10) 보다 높다 → 바로 돌고 막힘 */
    log_ch('a');
    NVIC_STIR = IRQ_SYSCALL;                                         /* 소프트웨어 트리거 */
    barrier();
    log_ch('b');
    sh_puts("[D] isr→task log="); sh_puts(g_log); sh_puts("\n");
    CHECK(str_eq(g_log, "aXb"));                                     /* W 가 a 와 b 사이에 끼어들었다 */
    sem_init(&g_slots, 3, 3);
    task_create(&t_c, "C", phd_c, NULL, 5, stk_t[4], STACK_WORDS);
    uint32_t s0 = g_tick;
    task_delay(5);                                                   /* C: 3 개 받고, 4 번째는 3 tick 타임아웃 */
    uint32_t give_tick = g_tick;
    sem_give(&g_slots);                                              /* C(5) < CTRL(10): 선점 없음 */
    CHECK(g_c_wake_tick == 0u);
    task_delay(1);
    sh_puts("[D] counting: got="); sh_dec(g_c_got); sh_puts(" timeout_ok="); sh_dec(g_c_to_ok);
    sh_puts(" dt="); sh_dec(g_c_to_dt); sh_puts(" woke@+"); sh_dec(g_c_wake_tick - give_tick); sh_puts("\n");
    CHECK(g_c_got == 3u && g_c_to_ok == 0u && g_c_to_dt == 3u);
    CHECK(g_c_wake_tick == give_tick && give_tick - s0 == 5u);

    /* ---- Phase E: 우선순위 역전, 상속 OFF vs ON ---- */
    g_probe_armed = true;
    run_inversion(false);
    g_probe_armed = false;
    uint32_t off_h = g_done[0], off_m = g_done[1], off_l = g_done[2];
    uint8_t off_lmax = 0;
    for (uint32_t i = 0; i < TRACE_LEN; ++i) if (g_trace_prio[2][i] > off_lmax) off_lmax = g_trace_prio[2][i];
    uint32_t inv_ticks = 0;                                          /* H 가 막혀 있는데 M 이 돈 tick 수 */
    for (uint32_t i = 0; i < TRACE_LEN; ++i) inv_ticks += (g_trace_st[0][i] == 'B' && g_trace_st[1][i] == 'R');
    run_inversion(true);
    uint32_t on_h = g_done[0], on_m = g_done[1], on_l = g_done[2];
    uint8_t on_lmax = 0;
    uint32_t inv_on = 0;
    for (uint32_t i = 0; i < TRACE_LEN; ++i) {
        if (g_trace_prio[2][i] > on_lmax) on_lmax = g_trace_prio[2][i];
        inv_on += (g_trace_st[0][i] == 'B' && g_trace_st[1][i] == 'R');
    }
    sh_puts("[E] done(H,M,L) off=("); sh_dec(off_h); sh_puts(","); sh_dec(off_m); sh_puts(","); sh_dec(off_l);
    sh_puts(") on=("); sh_dec(on_h); sh_puts(","); sh_dec(on_m); sh_puts(","); sh_dec(on_l);
    sh_puts(") inversion ticks off="); sh_dec(inv_ticks); sh_puts(" on="); sh_dec(inv_on);
    sh_puts(" Lmax off="); sh_dec(off_lmax); sh_puts(" on="); sh_dec(on_lmax); sh_puts("\n");
    CHECK(off_h == 11u && off_m == 8u && off_l == 12u);              /* H 는 M 의 6 tick 만큼 더 기다렸다 */
    CHECK(on_h == 5u && on_m == 11u && on_l == 12u);                 /* H 의 막힘 = L 의 남은 임계 구역 3 tick */
    CHECK(inv_ticks == 6u && inv_on == 0u);
    CHECK(off_lmax == 1u && on_lmax == 3u && t_l.prio == 1u && t_l.held == 0u);

    /* ---- Phase E': 상속 중 대기자 타임아웃 → 소유자 우선순위 복원 ---- */
    mutex_init(&g_res, true);
    g_t0 = g_tick + 2u;
    trace_start(&t_h, &t_m, &t_l, g_t0);
    task_create(&t_l, "L", phf_l, NULL, 1, stk_t[2], STACK_WORDS);
    task_create(&t_h, "H", phf_h, NULL, 3, stk_t[0], STACK_WORDS);
    task_create(&t_m, "M", phg_r, (void *)0x5EEDu, 2, stk_t[1], STACK_WORDS);   /* M 은 바로 return (Phase G 미리보기) */
    delay_to(g_t0 + 10u);
    g_trace_on = false;
    sh_puts("[E'] timeout: ok="); sh_dec(g_to_ok); sh_puts(" at+"); sh_dec(g_to_tick);
    sh_puts(" L prio slots1..4="); for (uint32_t i = 1; i <= 4; ++i) sh_dec(g_trace_prio[2][i]);
    sh_puts(" L done+"); sh_dec(g_done[2]); sh_puts("\n");
    CHECK(g_to_ok == 0u && g_to_tick == 3u);
    CHECK(g_trace_prio[2][1] == 3u && g_trace_prio[2][2] == 3u && g_trace_prio[2][3] == 1u);
    CHECK(g_done[2] == 6u && t_l.prio == 1u);

    /* ---- Phase F: BASEPRI 크리티컬 섹션 (스케줄러가 도는 중, 태스크 문맥) ---- */
    uint32_t sys0 = g_irq_sys_count, hi0 = g_irq_high_count, x0 = 0;
    log_reset();
    crit_enter();
    CHECK(basepri_get() == KERNEL_BASEPRI);
    NVIC_STIR = IRQ_SYSCALL; NVIC_STIR = IRQ_SYSCALL; NVIC_STIR = IRQ_SYSCALL;   /* 3 번 펜딩 */
    NVIC_ISPR0 = 1u << IRQ_HIGH;
    barrier();
    CHECK(g_irq_high_count == hi0 + 1u && g_irq_high_saw_basepri == KERNEL_BASEPRI);  /* 문턱 위: 즉시 */
    CHECK(g_irq_sys_count == sys0 && (NVIC_ISPR0 & (1u << IRQ_SYSCALL)));             /* 문턱 아래: 펜딩만 */
    uint32_t tick_in = g_tick, wraps = 0;
    (void)SYST_CSR;                                                  /* 읽으면 COUNTFLAG 가 지워진다 */
    while (wraps < 3u) if (SYST_CSR & CSR_COUNTFLAG) wraps++;        /* SysTick 카운터 3 바퀴 = 3 ms */
    bool st_pending = (SCB_ICSR & ICSR_PENDSTSET) != 0u;
    crit_enter();                                                    /* 중첩 */
    crit_exit();
    CHECK(g_irq_sys_count == sys0 && g_tick == tick_in && st_pending);   /* 아직 막혀 있다 */
    crit_exit();                                                     /* 여기서 IRQ30, SysTick, (W 깨움) */
    barrier();
    x0 = g_tick - tick_in;
    sh_puts("[F] crit: hi ran inside (BASEPRI seen="); sh_hex(g_irq_high_saw_basepri);
    sh_puts("), sys ISR runs after exit="); sh_dec(g_irq_sys_count - sys0);
    sh_puts(" (3 pends merged), tick advance after 3 ms masked="); sh_dec(x0); sh_puts(" log="); sh_puts(g_log); sh_puts("\n");
    CHECK(g_irq_sys_count == sys0 + 1u);                             /* 펜딩 비트는 1 개 → 3 번이 1 번으로 */
    CHECK(x0 == 1u);                                                 /* SysTick 2 번 잃었다 */
    CHECK(str_eq(g_log, "X"));                                       /* IRQ30 이 W 를 깨웠다 (crit_exit 직후) */

    /* ---- Phase G: 태스크가 return → task_exit_trap, 스택 건강 ---- */
    sh_puts("[G] exit trap hits="); sh_dec(g_exit_trap_hits);
    sh_puts(" task="); sh_puts(g_exit_trap_task ? g_exit_trap_task->name : "-");
    sh_puts(" arg="); sh_hex(g_r_arg); sh_puts("\n");
    CHECK(g_exit_trap_hits == 1u && g_exit_trap_task == &t_m && t_m.state == TASK_SUSPENDED && g_r_arg == 0x5EEDu);
    sh_puts("[G] switches="); sh_dec(g_switches); sh_puts(" stack_errors="); sh_dec(g_stack_errors);
    sh_puts(" api_errors="); sh_dec(g_api_errors); sh_puts(" idle_ticks="); sh_dec(t_idle.run_ticks);
    sh_puts(" tick="); sh_dec(g_tick); sh_puts("\n[G] stack used (bytes):");
    for (uint32_t i = 0; i < g_ntasks; ++i) { sh_puts(" "); sh_puts(g_tasks[i]->name); sh_puts("="); sh_dec(stack_used_bytes(g_tasks[i])); }
    sh_puts("\n");
    CHECK(g_stack_errors == 0u && g_api_errors == 0u && g_switches > 20u && t_idle.run_ticks > 0u);
    report_and_exit();
}

/* ======================= main: Phase A (스케줄러 시작 전) → kernel_start ======================= */
static void kernel_start(void) {
    task_create(&t_idle, "IDLE", idle_fn, NULL, 0, stk_idle, STACK_WORDS);
    basepri_set(KERNEL_BASEPRI);             /* vTaskStartScheduler 처럼: SVC 가 0 으로 풀 때까지 SysTick/PendSV 막음 */
    SCB_SHPR3 |= (PRIO_LOWEST << 16) | (PRIO_LOWEST << 24);          /* PendSV, SysTick = 최저 */
    SCB_SHPR2 = 0u;                                                  /* SVCall = 최고 */
    g_cur = sched_pick_next();
    g_cur->state = TASK_RUNNING;
    SYST_RVR = TICK_RELOAD;
    SYST_CVR = 0u;
    SYST_CSR = 0x7u;                         /* ENABLE | TICKINT | CLKSOURCE=core */
    g_crit_nesting = 0u;
    start_first_task();
}

int main(void) {
    sh_puts("RTOS-A01 mini RTOS on mps2-an385\n");
    /* NVIC 설정: 두 IRQ 우선순위 + enable */
    NVIC_IPR(IRQ_SYSCALL) = (uint8_t)PRIO_IRQ_SYSCALL;
    NVIC_IPR(IRQ_HIGH) = (uint8_t)PRIO_IRQ_HIGH;
    NVIC_ISER0 = (1u << IRQ_SYSCALL) | (1u << IRQ_HIGH);
    barrier();
    CHECK(g_irq_sys_count == 0u && g_irq_high_count == 0u);         /* 장치가 이 줄을 안 쓴다 */

    /* ---- Phase A1: 초기 스택 프레임 (TODO 1) ---- */
    static uint32_t probe[32] __attribute__((aligned(8)));
    uint32_t *top = probe + 32;
    uint32_t *sp = task_stack_init(top, idle_fn, (void *)0x1234u);
    CHECK(sp == top - 16);                                           /* 아래 sp[] 검사는 이게 맞을 때만 읽는다 */
    CHECK(top[-1] == 0x01000000u);
    CHECK(top[-2] == ((uint32_t)(uintptr_t)idle_fn & ~1u) && (top[-2] & 1u) == 0u);
    CHECK(top[-3] == (uint32_t)(uintptr_t)task_exit_trap && (top[-3] & 1u) == 1u);
    CHECK(top[-8] == 0x1234u);
    CHECK(sp == top - 16 && sp[0] == 0x04040404u && sp[7] == 0x0B0B0B0Bu);   /* r4 이 가장 낮은 주소 */
    CHECK(((uintptr_t)(top - 8) & 7u) == 0u);                        /* 하드웨어 프레임 시작이 8 정렬 */

    /* ---- Phase A2: BASEPRI 크리티컬 섹션, 스케줄러 전 (TODO 4) ---- */
    sem_init(&g_isr_sem, 0, 1);
    crit_enter();
    NVIC_STIR = IRQ_SYSCALL;
    NVIC_ISPR0 = 1u << IRQ_HIGH;
    barrier();
    uint32_t hi_in = g_irq_high_count, sys_in = g_irq_sys_count, bp_in = basepri_get();
    crit_exit();
    barrier();
    sh_puts("[A] in crit: high="); sh_dec(hi_in); sh_puts(" sys="); sh_dec(sys_in);
    sh_puts(" BASEPRI="); sh_hex(bp_in); sh_puts(" | after exit: sys="); sh_dec(g_irq_sys_count); sh_puts("\n");
    CHECK(hi_in == 1u && sys_in == 0u && bp_in == KERNEL_BASEPRI);
    CHECK(g_irq_sys_count == 1u && basepri_get() == 0u && g_isr_sem.count == 1u);

    /* ---- 스케줄러 시작: CTRL(10) 이 첫 태스크 ---- */
    task_create(&t_ctrl, "CTRL", ctrl_fn, (void *)0xC0DE0001u, 10, stk_ctrl, STACK_WORDS);
    kernel_start();                          /* 돌아오지 않는다 */
    return 1;
}

/* ======================= startup (BM-B08 축약: 생성자 없음) ======================= */
void Reset_Handler(void);
void Default_Handler(void);
#define WEAK_DEFAULT __attribute__((weak, alias("Default_Handler")))
void NMI_Handler(void) WEAK_DEFAULT;
void MemManage_Handler(void) WEAK_DEFAULT;
void BusFault_Handler(void) WEAK_DEFAULT;
void UsageFault_Handler(void) WEAK_DEFAULT;
void DebugMon_Handler(void) WEAK_DEFAULT;

void Default_Handler(void) {
    sh_puts("Default_Handler: unexpected exception IPSR="); sh_dec(ipsr_get()); sh_puts("\n");
    g_total++;
    report_and_exit();
}

typedef void (*vector_t)(void);
#define D Default_Handler
__attribute__((section(".isr_vector"), used))
const vector_t g_vectors[16 + 32] = {
    (vector_t)_estack, Reset_Handler, NMI_Handler, HardFault_Handler,
    MemManage_Handler, BusFault_Handler, UsageFault_Handler, 0, 0, 0, 0,
    SVC_Handler, DebugMon_Handler, 0, PendSV_Handler, SysTick_Handler,
    D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D,                 /* IRQ0..15  */
    D, D, D, D, D, D, D, D, D, D, D, D, D, D, IRQ30_Handler, IRQ31_Handler,
};

__attribute__((noreturn)) void Reset_Handler(void) {
    const uint32_t *src = _sidata;
    for (uint32_t *dst = _sdata; dst < _edata;) *dst++ = *src++;
    for (uint32_t *dst = _sbss; dst < _ebss;) *dst++ = 0u;
    sh_exit(main());
}
