/* ENV: qemu */
/* BM-A09 크래시 덤프: .noinit RAM 에 fault 기록, 리셋 후 보고
 *
 * 한 파일, 두 빌드:
 *   (1) 펌웨어  — qemu-system-arm -M mps2-an385 (Cortex-M3), 링커 스크립트 a09.ld.
 *       부팅 1: 콜드 → 계획(.noinit) 초기화 → 결함 0 주입 → fault → 진입 스텁(r4–r11 저장, fault 스택으로 전환)
 *               → dump_capture (1 단계 레지스터 봉인 → 2 단계 스택 워드 봉인) → SYSRESETREQ
 *       부팅 2: dump_valid → UART0 에 @@ 줄 블록으로 보고 → 소비 표시 → 결함 1 주입 → ...
 *       부팅 9: 요약 + 검사 → 세미호스팅 SYS_EXIT 0/1.  보고는 전부 CMSDK UART0 (QEMU -serial file:).
 *   (2) 호스트  — cc -DHOST_TEST: 같은 select_frame / dump_seal / dump_valid 를 맥에서 시험
 *       (콜드 부팅 쓰레기 1e6 건, 헤더만 맞는 쓰레기 1e6 건, 1비트 뒤집힘 전수, 낡은 버전, 찢어진 커밋).
 * 실행: bash run_qemu.sh solution|starter (VM),  cc -DHOST_TEST ... solution.c (맥)
 */
#include <stddef.h>
#include <stdint.h>
#ifdef HOST_TEST
#include <stdio.h>
#define dsb() __asm__ volatile("" ::: "memory")
#else
#define dsb() __asm__ volatile("dsb" ::: "memory")
#define isb() __asm__ volatile("isb" ::: "memory")
#endif

/* ======================= 공통: 덤프 형식 v2 (204 B, 리틀엔디언) ======================= */
#define DUMP_MAGIC     0x504D5544u   /* 메모리 바이트 'D','U','M','P' */
#define DUMP_CONSUMED  0x44414552u   /* 'R','E','A','D': 보고 끝난 덤프 */
#define DUMP_VERSION   2u
#define DUMP_NSTACK    16u
#define FRAME_BAD      0xFFFFFFFFu   /* 프레임을 못 읽었을 때 r0–r3/r12/lr/pc/xpsr 자리 */
#define RR_FAULT       1u            /* reset_reason: fault 정책 리셋 */

typedef struct {
    uint32_t magic;        /*  0 마지막에 쓴다 = 커밋 표시 (CRC 범위 밖) */
    uint32_t version;      /*  4 = 2                        ┐                     */
    uint32_t size;         /*  8 = sizeof = 204             │                     */
    uint32_t boot_count;   /* 12 크래시가 난 부팅 번호       │                     */
    uint32_t reset_reason; /* 16 RR_FAULT                   │                     */
    uint32_t exc;          /* 20 IPSR (3 Hard 4 Mem 5 Bus 6 Usage)                 */
    uint32_t exc_return;   /* 24 진입 때 LR                 │ CRC-32 범위          */
    uint32_t flags;        /* 28 F_*                        │ [4, 200) = 196 B     */
    uint32_t stage;        /* 32 1 = 레지스터만, 2 = 스택 워드까지                 */
    uint32_t sp;           /* 36 프레임이 있던(있어야 할) SP │                     */
    uint32_t msp, psp;     /* 40 44 진입 순간의 두 SP       │                     */
    uint32_t r[13];        /* 48 r0–r3,r12 = 쌓인 값, r4–r11 = 스텁이 저장           */
    uint32_t lr, pc, xpsr; /* 100 104 108 쌓인 값            │                     */
    uint32_t cfsr, hfsr, mmfar, bfar, shcsr;   /* 112..128   │                     */
    uint32_t nstack;       /* 132 유효한 스택 워드 수         │                     */
    uint32_t stack[DUMP_NSTACK]; /* 136 프레임 바로 위 16 워드 ┘                    */
    uint32_t crc;          /* 200 CRC-32/ISO-HDLC (zlib) of [4, 200) */
} crash_dump_t;
_Static_assert(sizeof(crash_dump_t) == 204, "dump v2 must be 204 bytes");
_Static_assert(offsetof(crash_dump_t, r) == 48 && offsetof(crash_dump_t, pc) == 104, "layout");
_Static_assert(offsetof(crash_dump_t, stack) == 136 && offsetof(crash_dump_t, crc) == 200, "layout");
#define DUMP_CRC_LEN ((uint32_t)(offsetof(crash_dump_t, crc) - 4u))   /* 196 */

typedef struct {           /* 예전 펌웨어의 v1 형식 (32 B) — "낡은 덤프" 시험용 */
    uint32_t magic, version, size, exc, pc, lr, cfsr, crc;   /* crc = CRC-32 of [4, 28) */
} crash_dump_v1_t;

/* flags */
#define F_PSP         (1u << 0)   /* EXC_RETURN[2]=1: 프레임은 PSP 에 */
#define F_FRAME_OK    (1u << 1)   /* 프레임을 읽어도 안전하다고 판정 */
#define F_HANDLER     (1u << 2)   /* EXC_RETURN[3:0]=0001: fault 가 핸들러 모드(ISR 안)에서 */
#define F_FPFRAME     (1u << 3)   /* EXC_RETURN[4]=0: 확장(FP) 프레임 104 B */
#define F_STKERR      (1u << 4)   /* CFSR MSTKERR/STKERR: 하드웨어 쌓기 실패 → 프레임 없음 */
#define F_BAD_EXCRET  (1u << 5)   /* LR 이 EXC_RETURN 모양이 아님 */
#define F_ALIGNPAD    (1u << 6)   /* 쌓인 xPSR[9]: 정렬 패딩 4 B */
#define F_STACK_TRUNC (1u << 7)   /* 스택 워드가 RAM 끝/가드에서 잘림 */

/* CFSR 비트 (ARMv7-M ARM B3.2.15 — 확인 권장) */
#define CFSR_DACCVIOL   (1u << 1)
#define CFSR_MSTKERR    (1u << 4)
#define CFSR_MMARVALID  (1u << 7)
#define CFSR_PRECISERR  (1u << 9)
#define CFSR_STKERR     (1u << 12)
#define CFSR_BFARVALID  (1u << 15)
#define CFSR_UNDEFINSTR (1u << 16)
#define CFSR_DIVBYZERO  (1u << 25)
#define HFSR_FORCED     (1u << 30)

/* dump_valid 결과 */
enum { DV_OK = 0, DV_NO_MAGIC, DV_CONSUMED, DV_BAD_VERSION, DV_BAD_SIZE, DV_BAD_CRC, DV_BAD_FIELD, DV_N };
static const char *const k_dv_name[DV_N] = {
    "OK", "NO_MAGIC", "CONSUMED", "BAD_VERSION", "BAD_SIZE", "BAD_CRC", "BAD_FIELD"
};

typedef struct { uint32_t ram_lo, ram_hi, guard_lo, guard_hi; } stack_bounds_t;
typedef struct { uint32_t sp, frame, fsize, flags; } frame_sel_t;

/* CRC-32/ISO-HDLC (zlib): 반사, 다항식 0xEDB88320, init/xorout 0xFFFFFFFF, 니블 표 16 개 (64 B) */
static const uint32_t k_crc_nib[16] = {
    0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
    0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu,
};
static uint32_t crc32(const void *buf, uint32_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t c = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; ++i) {
        c ^= p[i];
        c = (c >> 4) ^ k_crc_nib[c & 0xFu];
        c = (c >> 4) ^ k_crc_nib[c & 0xFu];
    }
    return ~c;
}

/* v7-M 의 합법 EXC_RETURN: 0xFFFFFFF1/F9/FD (+ FP 확장 프레임이면 E1/E9/ED) */
static int excret_legal(uint32_t e) {
    uint32_t lo = e & 0xFFu;
    if ((e >> 8) != 0x00FFFFFFu || (lo & 0xE0u) != 0xE0u) return 0;
    lo &= 0x0Fu;
    return lo == 0x1u || lo == 0x9u || lo == 0xDu;
}

/* ================= TODO 1: 어느 스택의 프레임을 읽을까, 읽어도 되나 =================
 * - excret 가 합법이 아니면: flags = F_BAD_EXCRET, sp = msp, frame = 0.
 * - EXC_RETURN[2] → PSP (F_PSP) 아니면 MSP. [3:0]==1 → F_HANDLER. [4]==0 → F_FPFRAME, fsize 104 (아니면 32).
 * - CFSR 에 MSTKERR/STKERR 가 있으면 하드웨어가 프레임을 못 쌓은 것 → F_STKERR, frame = 0.
 * - sp 가 4-정렬이고, [sp, sp+fsize) 가 [ram_lo, ram_hi) 안이며 가드 [guard_lo, guard_hi) 와 겹치지 않을 때만
 *   frame = sp, F_FRAME_OK. 덧셈 오버플로에 주의 (sp 가 0xFFFFFFF0 이어도 안전하게). */
static frame_sel_t select_frame(uint32_t excret, uint32_t msp, uint32_t psp, uint32_t cfsr,
                                const stack_bounds_t *b) {
    frame_sel_t s = { msp, 0u, 32u, 0u };
    if (!excret_legal(excret)) { s.flags = F_BAD_EXCRET; return s; }
    if (excret & 4u) { s.flags |= F_PSP; s.sp = psp; }
    if ((excret & 0xFu) == 1u) s.flags |= F_HANDLER;
    if (!(excret & 0x10u)) { s.flags |= F_FPFRAME; s.fsize = 104u; }
    if (cfsr & (CFSR_MSTKERR | CFSR_STKERR)) { s.flags |= F_STKERR; return s; }
    if ((s.sp & 3u) != 0u || s.sp < b->ram_lo || s.sp > b->ram_hi || b->ram_hi - s.sp < s.fsize) return s;
    if (s.sp < b->guard_hi && s.sp + s.fsize > b->guard_lo) return s;
    s.frame = s.sp;
    s.flags |= F_FRAME_OK;
    return s;
}

/* ================= TODO 2: 봉인 (commit) =================
 * magic = 0 → (DSB) → crc = CRC-32([4, 200)) → (DSB) → magic = DUMP_MAGIC → (DSB).
 * 내용 필드는 호출자가 이미 채웠다. magic 은 CRC 범위 밖이라 마지막에 써도 CRC 가 안 바뀐다. */
static void dump_seal(crash_dump_t *d) {
    d->magic = 0u;
    dsb();
    d->crc = crc32((const uint8_t *)d + 4, DUMP_CRC_LEN);
    dsb();
    d->magic = DUMP_MAGIC;
    dsb();
}

/* ================= TODO 3: 검증 — 이유별로 다른 코드 =================
 * 순서가 중요하다: magic → (CONSUMED?) → version → size → CRC → 필드 상식 검사.
 * version 을 CRC 보다 먼저 보는 이유: 낡은 형식은 크기·CRC 범위가 다르다 (v1 은 32 B).
 * CRC 가 맞아도 stage ∉ {1,2}, nstack > 16, stage 1 인데 nstack ≠ 0 이면 DV_BAD_FIELD
 * (보고 루프가 nstack 을 믿고 배열을 넘어 읽지 않게). */
static int dump_valid(const crash_dump_t *d) {
    if (d->magic == DUMP_CONSUMED) return DV_CONSUMED;
    if (d->magic != DUMP_MAGIC) return DV_NO_MAGIC;
    if (d->version != DUMP_VERSION) return DV_BAD_VERSION;
    if (d->size != (uint32_t)sizeof(crash_dump_t)) return DV_BAD_SIZE;
    if (d->crc != crc32((const uint8_t *)d + 4, DUMP_CRC_LEN)) return DV_BAD_CRC;
    if ((d->stage != 1u && d->stage != 2u) || d->nstack > DUMP_NSTACK || (d->stage == 1u && d->nstack != 0u))
        return DV_BAD_FIELD;
    return DV_OK;
}

#ifndef HOST_TEST
/* ============================== 펌웨어 (QEMU mps2-an385) ============================== */
extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[], _snoinit[], _enoinit[];
extern uint32_t _sstack_guard[], _estack_guard[], _sfault_stack[], _fault_stack_top[], _stext[], _etext[];

#define REG32(a)    (*(volatile uint32_t *)(uintptr_t)(a))
#define REG8(a)     (*(volatile uint8_t *)(uintptr_t)(a))
#define SCB_AIRCR   REG32(0xE000ED0Cu)
#define SCB_CCR     REG32(0xE000ED14u)
#define SCB_SHCSR   REG32(0xE000ED24u)
#define SCB_CFSR    REG32(0xE000ED28u)
#define SCB_HFSR    REG32(0xE000ED2Cu)
#define SCB_MMFAR   REG32(0xE000ED34u)
#define SCB_BFAR    REG32(0xE000ED38u)
#define MPU_CTRL    REG32(0xE000ED94u)
#define MPU_RNR     REG32(0xE000ED98u)
#define MPU_RBAR    REG32(0xE000ED9Cu)
#define MPU_RASR    REG32(0xE000EDA0u)
#define NVIC_ISER0  REG32(0xE000E100u)
#define NVIC_ISPR0  REG32(0xE000E200u)
#define NVIC_IPR(n) REG8(0xE000E400u + (n))
#define UART0       0x40004000u           /* CMSDK APB UART0 = QEMU 첫 번째 -serial (BM-B09) */
#define UART_DATA   REG32(UART0 + 0x00u)
#define UART_STATE  REG32(UART0 + 0x04u)  /* bit0 TXFULL */
#define UART_CTRL   REG32(UART0 + 0x08u)  /* bit0 TX_EN */
#define UART_BAUDDIV REG32(UART0 + 0x10u)
#define BUS_HOLE    0x30000000u           /* QEMU info mtree: 아무것도 없음 → 정밀 BusFault (BM-M13 probe) */
#define RAM_LO      0x20000000u           /* a09.ld RAM */
#define RAM_HI      0x2000F000u           /* = ORIGIN(NOINIT) */

/* ---------- 출력: UART0 (보고), 세미호스팅은 종료 코드에만 ---------- */
static void uart_init(void) { UART_BAUDDIV = 16u; UART_CTRL = 1u; }
static void uart_putc(char c) {
    for (uint32_t n = 0; (UART_STATE & 1u) && n < 100000u; ++n) { }
    UART_DATA = (uint8_t)c;
}
static void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
static void uart_pad(const char *s, uint32_t w) {
    uint32_t n = 0;
    for (; s[n]; ++n) uart_putc(s[n]);
    for (; n < w; ++n) uart_putc(' ');
}
static void uart_hex(uint32_t v) {
    uart_puts("0x");
    for (int s = 28; s >= 0; s -= 4) uart_putc("0123456789abcdef"[(v >> s) & 0xFu]);
}
static void uart_dec(uint32_t v) {
    char b[11];
    int i = 10;
    b[i] = 0;
    do { b[--i] = (char)('0' + v % 10u); v /= 10u; } while (v);
    uart_puts(&b[i]);
}
static int semi(int op, const void *arg) {
    register int r0 __asm__("r0") = op;
    register const void *r1 __asm__("r1") = arg;
    __asm__ volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}
__attribute__((noreturn)) static void sh_exit(int code) {
    uint32_t blk[2] = { 0x20026u, (uint32_t)code };          /* SYS_EXIT_EXTENDED, ADP_Stopped_ApplicationExit */
    semi(0x20, blk);
    for (;;) { }
}
__attribute__((noreturn)) static void system_reset(void) {  /* CMSIS NVIC_SystemReset 모양 (BM-M13) */
    dsb();
    SCB_AIRCR = (0x05FAu << 16) | (SCB_AIRCR & (7u << 8)) | (1u << 2);
    dsb();
    for (;;) { __asm__ volatile("nop"); }
}

/* ---------- 리셋을 건너는 RAM (.noinit) ---------- */
#define NOINIT __attribute__((section(".noinit")))
#define NESTED_MAGIC 0x4E455354u      /* "NEST" */
#define PLAN_MAGIC   0x504C414Eu      /* "PLAN" */
#define NSTEPS       9u
#define MAX_BOOTS    16u
typedef union { crash_dump_t cur; crash_dump_v1_t v1; uint32_t w[sizeof(crash_dump_t) / 4u]; } dump_area_t;
typedef struct { uint32_t magic, exc, excret, cfsr, hfsr, pc, check; } nested_rec_t;
typedef struct { uint32_t count, inv; } bootctr_t;
typedef struct { uint32_t rc, nested_ok; crash_dump_t d; nested_rec_t n; } step_res_t;
typedef struct {
    uint32_t magic, next, nres, cold_rc, cold_raw;
    step_res_t res[NSTEPS];
    uint32_t check;
} plan_t;
NOINIT dump_area_t g_dump;
NOINIT nested_rec_t g_nested;
NOINIT bootctr_t g_bootctr;
NOINIT plan_t g_plan;

/* ---------- 이번 부팅에만 (.bss: 매 부팅 0) ---------- */
__attribute__((used)) uint32_t g_fault_active;      /* 진입 스텁이 읽는다: 1 이면 중첩 fault */
__attribute__((used)) uint32_t g_fault_r4_r11[8];   /* 진입 스텁이 r4–r11 을 여기에 (스택 안 씀) */
static uint32_t g_scratch[DUMP_NSTACK];              /* 2 단계의 위험한 읽기는 먼저 여기로 */
volatile uint32_t g_hook_bad_stack_read;             /* 시험 주입: 2 단계가 BUS_HOLE 을 읽게 (NESTED 단계) */
static uint32_t g_task_stack[256] __attribute__((aligned(8)));   /* PSP "태스크" 스택 1 KB */

static stack_bounds_t bounds(void) {
    stack_bounds_t b = { RAM_LO, RAM_HI, (uint32_t)(uintptr_t)_sstack_guard, (uint32_t)(uintptr_t)_estack_guard };
    return b;
}
static void copy_words(uint32_t *dst, const uint32_t *src, uint32_t n) { for (uint32_t i = 0; i < n; ++i) dst[i] = src[i]; }

/* ================= TODO 4: 캡처 — 2 단계 커밋 =================
 * 1 단계 (안전한 것만): select_frame → 헤더·플래그·SP 들 → 프레임이 OK 면 r0–r3,r12,lr,pc,xpsr (아니면 FRAME_BAD)
 *   → r4–r11 = g_fault_r4_r11 → CFSR/HFSR/MMFAR/BFAR/SHCSR → stage 1, nstack 0, stack[] 0 → dump_seal.
 * 2 단계 (위험한 읽기): 프레임이 OK 일 때만. 시작 = frame + fsize (+4 if xPSR[9] → F_ALIGNPAD),
 *   개수 = 16 을 RAM_HI 와 가드 아래쪽 경계에서 자름 (F_STACK_TRUNC). g_hook_bad_stack_read 면 출처를 BUS_HOLE 로.
 *   먼저 g_scratch 로 다 읽고, 그 다음에야 magic=0 → stack[]/nstack/stage 2/flags → dump_seal.
 *   (읽다가 중첩 fault 가 나도 1 단계 덤프는 그대로 유효해야 한다.) */
static void dump_capture(crash_dump_t *d, uint32_t excret, uint32_t msp, uint32_t psp, uint32_t ipsr) {
    const stack_bounds_t b = bounds();
    const uint32_t cfsr = SCB_CFSR;
    const frame_sel_t s = select_frame(excret, msp, psp, cfsr, &b);
    const volatile uint32_t *f = (const volatile uint32_t *)(uintptr_t)s.frame;
    const int ok = (s.flags & F_FRAME_OK) != 0u;
    /* ---- 1 단계 ---- */
    d->magic = 0u;
    dsb();
    d->version = DUMP_VERSION; d->size = (uint32_t)sizeof *d; d->boot_count = g_bootctr.count;
    d->reset_reason = RR_FAULT; d->exc = ipsr & 0x1FFu; d->exc_return = excret; d->flags = s.flags;
    d->stage = 1u; d->sp = s.sp; d->msp = msp; d->psp = psp;
    for (uint32_t i = 0; i < 4u; ++i) d->r[i] = ok ? f[i] : FRAME_BAD;
    for (uint32_t i = 0; i < 8u; ++i) d->r[4u + i] = g_fault_r4_r11[i];
    d->r[12] = ok ? f[4] : FRAME_BAD;
    d->lr = ok ? f[5] : FRAME_BAD; d->pc = ok ? f[6] : FRAME_BAD; d->xpsr = ok ? f[7] : FRAME_BAD;
    d->cfsr = cfsr; d->hfsr = SCB_HFSR; d->mmfar = SCB_MMFAR; d->bfar = SCB_BFAR; d->shcsr = SCB_SHCSR;
    d->nstack = 0u;
    for (uint32_t i = 0; i < DUMP_NSTACK; ++i) d->stack[i] = 0u;
    dump_seal(d);
    /* ---- 2 단계 ---- */
    if (!ok) return;
    uint32_t flags = s.flags, n = DUMP_NSTACK;
    uint32_t above = s.frame + s.fsize;
    if (d->xpsr & (1u << 9)) { above += 4u; flags |= F_ALIGNPAD; }
    if (above >= b.ram_hi) n = 0u;
    else if ((b.ram_hi - above) / 4u < n) { n = (b.ram_hi - above) / 4u; flags |= F_STACK_TRUNC; }
    if (above < b.guard_lo && (b.guard_lo - above) / 4u < n) { n = (b.guard_lo - above) / 4u; flags |= F_STACK_TRUNC; }
    const volatile uint32_t *src = (const volatile uint32_t *)(uintptr_t)(g_hook_bad_stack_read ? BUS_HOLE : above);
    for (uint32_t i = 0; i < n; ++i) g_scratch[i] = src[i];       /* 여기서 fault 나도 1 단계 덤프는 살아 있다 */
    d->magic = 0u;
    dsb();
    d->flags = flags; d->nstack = n;
    for (uint32_t i = 0; i < n; ++i) d->stack[i] = g_scratch[i];
    d->stage = 2u;
    dump_seal(d);
}

/* ---------- fault 진입: 첫 fault → fault_capture, 중첩 fault → fault_nested ---------- */
__attribute__((used, noreturn)) void fault_capture(uint32_t excret, uint32_t msp, uint32_t psp, uint32_t ipsr) {
    g_fault_active = 1u;
    dump_capture(&g_dump.cur, excret, msp, psp, ipsr);
    uart_puts("  [fault] exc="); uart_dec(ipsr & 0x1FFu); uart_puts(" msp="); uart_hex(msp);
    uart_puts(" -> dump stage="); uart_dec(g_dump.cur.stage); uart_puts(" sealed, SYSRESETREQ\n");
    system_reset();
}
__attribute__((used, noreturn)) void fault_nested(uint32_t excret, uint32_t msp, uint32_t ipsr) {
    nested_rec_t *n = &g_nested;                         /* 최소한만: 기록 몇 워드 + 즉시 리셋 */
    const uint32_t lo = (uint32_t)(uintptr_t)_sfault_stack, hi = (uint32_t)(uintptr_t)_fault_stack_top;
    n->magic = 0u;
    n->exc = ipsr & 0x1FFu; n->excret = excret; n->cfsr = SCB_CFSR; n->hfsr = SCB_HFSR;
    n->pc = (msp >= lo && msp <= hi - 32u && (msp & 3u) == 0u) ? ((const uint32_t *)(uintptr_t)msp)[6] : FRAME_BAD;
    n->check = crc32(&n->exc, 5u * 4u);
    dsb();
    n->magic = NESTED_MAGIC;
    system_reset();
}
#ifdef NAIVE_ENTRY   /* 결함 주입 (run_qemu.sh naive): 스택 전환 없이 현재 MSP 로 바로 C 를 부른다 */
#define SWITCH_STACK ""
#else
#define SWITCH_STACK "movw r12, #:lower16:_fault_stack_top\n movt r12, #:upper16:_fault_stack_top\n msr msp, r12\n"
#endif
__attribute__((naked)) void Fault_Entry(void) {
    __asm__ volatile(
        "movw  r12, #:lower16:g_fault_active \n"
        "movt  r12, #:upper16:g_fault_active \n"
        "ldr   r0, [r12]                     \n"
        "cbnz  r0, 1f                        \n"   /* 이미 덤프 중 → 중첩 fault */
        "movw  r12, #:lower16:g_fault_r4_r11 \n"
        "movt  r12, #:upper16:g_fault_r4_r11 \n"
        "stmia r12, {r4-r11}                 \n"   /* 콜리 저장 레지스터: 스택 없이 고정 주소로 */
        "mov   r0, lr                        \n"   /* EXC_RETURN */
        "mrs   r1, msp                       \n"
        "mrs   r2, psp                       \n"
        "mrs   r3, ipsr                      \n"
        SWITCH_STACK                               /* 여기까지 push 0 번. 이제부터 C 가 push 해도 안전 */
        "b     fault_capture                 \n"
        "1:                                  \n"
        "mov   r0, lr                        \n"
        "mrs   r1, msp                       \n"
        "mrs   r2, ipsr                      \n"
        "b     fault_nested                  \n");
}
void HardFault_Handler(void)  __attribute__((alias("Fault_Entry")));
void MemManage_Handler(void)  __attribute__((alias("Fault_Entry")));
void BusFault_Handler(void)   __attribute__((alias("Fault_Entry")));
void UsageFault_Handler(void) __attribute__((alias("Fault_Entry")));

/* ---------- startup (BM-B08 축약판) ---------- */
void Reset_Handler(void);
void Irq7_Handler(void);
void Default_Handler(void) {
    uint32_t ipsr;
    __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
    uart_puts("Default_Handler: unexpected exception "); uart_dec(ipsr); uart_puts("\n");
    sh_exit(2);
}
typedef void (*vector_t)(void);
#define D Default_Handler
__attribute__((section(".isr_vector"), used))
const vector_t g_vectors[16 + 32] = {
    (vector_t)_estack, Reset_Handler, D, HardFault_Handler, MemManage_Handler, BusFault_Handler,
    UsageFault_Handler, 0, 0, 0, 0, D, D, 0, D, D,
    D, D, D, D, D, D, D, Irq7_Handler, D, D, D, D, D, D, D, D,
    D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D,
};

/* ---------- 결함 주입 (한 부팅에 하나). 각 fault 명령에 전역 라벨 → 쌓인 PC 와 정확히 비교 ---------- */
extern const char pc_udf[], pc_bus[], pc_div0[], pc_irq[], pc_badsp[], pc_nest[];
__attribute__((naked, noinline)) static void inject_udf_regs(void) {   /* rN = 0xA00N000N 후 UDF */
    __asm__ volatile(
        "movw r0,#0\n movt r0,#0xA000\n movw r1,#1\n movt r1,#0xA001\n movw r2,#2\n movt r2,#0xA002\n"
        "movw r3,#3\n movt r3,#0xA003\n movw r4,#4\n movt r4,#0xA004\n movw r5,#5\n movt r5,#0xA005\n"
        "movw r6,#6\n movt r6,#0xA006\n movw r7,#7\n movt r7,#0xA007\n movw r8,#8\n movt r8,#0xA008\n"
        "movw r9,#9\n movt r9,#0xA009\n movw r10,#10\n movt r10,#0xA00A\n movw r11,#11\n movt r11,#0xA00B\n"
        "movw r12,#12\n movt r12,#0xA00C\n"
        ".global pc_udf\npc_udf: udf #1\n b .\n");
}
__attribute__((naked, noinline)) static void inject_bus_regs(void) {   /* rN = 0xB00N000N, r1 = BUS_HOLE, LDR */
    __asm__ volatile(
        "movw r0,#0\n movt r0,#0xB000\n movw r1,#0\n movt r1,#0x3000\n movw r2,#2\n movt r2,#0xB002\n"
        "movw r3,#3\n movt r3,#0xB003\n movw r4,#4\n movt r4,#0xB004\n movw r5,#5\n movt r5,#0xB005\n"
        "movw r6,#6\n movt r6,#0xB006\n movw r7,#7\n movt r7,#0xB007\n movw r8,#8\n movt r8,#0xB008\n"
        "movw r9,#9\n movt r9,#0xB009\n movw r10,#10\n movt r10,#0xB00A\n movw r11,#11\n movt r11,#0xB00B\n"
        "movw r12,#12\n movt r12,#0xB00C\n"
        ".global pc_bus\npc_bus: ldr r0, [r1]\n b .\n");
}
__attribute__((naked, noinline)) static void task_div0(void) {       /* PSP 위의 "태스크": 표식 2 워드 push 후 0 나눗셈 */
    __asm__ volatile(
        "movw r4,#0x0001\n movt r4,#0x7A5C\n movw r5,#0x0002\n movt r5,#0x7A5C\n push {r4, r5}\n"
        "movs r0, #7\n movs r1, #0\n"
        ".global pc_div0\npc_div0: udiv r2, r0, r1\n b .\n");
}
__attribute__((naked, noinline)) static void run_on_psp(__attribute__((unused)) uint32_t top, __attribute__((unused)) void (*fn)(void)) {
    __asm__ volatile("msr psp, r0\n movs r2, #2\n msr control, r2\n isb\n blx r1\n b .\n");
}
void Irq7_Handler(void) {                                 /* IRQ7 (우선순위 0x80) 안의 UDF → UsageFault(0) 가 선점 */
    __asm__ volatile(".global pc_irq\npc_irq: udf #3");
}
__attribute__((noinline, noclone)) static uint32_t recurse(uint32_t n) {   /* 프레임 ~64 B 씩 스택을 먹는다 */
    volatile uint32_t pad[12];
    pad[0] = n; pad[11] = n ^ 0x5A5A5A5Au;
    if (n == 0u) return pad[0];
    return recurse(n - 1u) + pad[11];
}
__attribute__((naked, noinline)) static void inject_bad_sp(void) {     /* 망가진 SP (잘못된 문맥 복원 모델) 후 UDF */
    __asm__ volatile("movw r0, #0x0100\n movt r0, #0x3000\n msr msp, r0\n isb\n"
                     ".global pc_badsp\npc_badsp: udf #4\n b .\n");
}
__attribute__((naked, noinline)) static void inject_udf_plain(void) {
    __asm__ volatile(".global pc_nest\npc_nest: udf #5\n b .\n");
}

static const char *const k_step_name[NSTEPS] = {
    "UDF_MSP", "BUS_PRECISE", "DIV0_PSP", "IRQ_UDF", "STACK_OVF", "BAD_SP", "NESTED", "STALE_V1", "BAD_CRC"
};
volatile uint32_t g_sink;
static void inject(uint32_t k) {
    switch (k) {
    case 0: inject_udf_regs(); break;
    case 1: inject_bus_regs(); break;
    case 2: run_on_psp((uint32_t)(uintptr_t)&g_task_stack[256], task_div0); break;
    case 3: NVIC_IPR(7) = 0x80u; NVIC_ISER0 = 1u << 7; NVIC_ISPR0 = 1u << 7; dsb(); isb(); break;
    case 4: g_sink = recurse(4096u); break;
    case 5: inject_bad_sp(); break;
    case 6: g_hook_bad_stack_read = 1u; inject_udf_plain(); break;
    case 7: {                                          /* 예전 펌웨어(v1)가 남긴, CRC 까지 맞는 덤프 */
        crash_dump_v1_t *v = &g_dump.v1;
        v->magic = 0u; v->version = 1u; v->size = (uint32_t)sizeof *v; v->exc = 6u;
        v->pc = 0x00001234u; v->lr = 0x00001001u; v->cfsr = CFSR_UNDEFINSTR;
        v->crc = crc32(&v->version, 24u);
        dsb(); v->magic = DUMP_MAGIC; dsb();
        system_reset();
    }
    case 8: {                                          /* 정상 봉인 후 1 비트 뒤집기 (메모리 손상 모델) */
        crash_dump_t *d = &g_dump.cur;
        for (uint32_t i = 0; i < sizeof(crash_dump_t) / 4u; ++i) g_dump.w[i] = 0u;
        d->version = DUMP_VERSION; d->size = (uint32_t)sizeof *d; d->boot_count = g_bootctr.count;
        d->reset_reason = RR_FAULT; d->exc = 6u; d->exc_return = 0xFFFFFFF9u; d->stage = 1u; d->pc = 0x00000100u;
        dump_seal(d);
        d->pc ^= 1u << 4;
        dsb();
        system_reset();
    }
    default: break;
    }
}

/* ---------- 계획 (시험 진행 상태, .noinit) ---------- */
#define PLAN_BYTES ((uint32_t)(offsetof(plan_t, check) - offsetof(plan_t, next)))
static int plan_valid(void) {
    return g_plan.magic == PLAN_MAGIC && g_plan.check == crc32(&g_plan.next, PLAN_BYTES) &&
           g_plan.next <= NSTEPS && g_plan.nres <= NSTEPS;
}
static void plan_seal(void) { g_plan.check = crc32(&g_plan.next, PLAN_BYTES); g_plan.magic = PLAN_MAGIC; dsb(); }
static int nested_valid(const nested_rec_t *n) {
    return n->magic == NESTED_MAGIC && n->check == crc32(&n->exc, 5u * 4u);
}

/* ---------- 보고: 기계용 @@ 줄 블록 (decode_dump.py 가 파싱) + 사람용 한 줄 ---------- */
static void report_dump(const crash_dump_t *d, const char *tag) {
    const uint8_t *p = (const uint8_t *)d;
    uart_puts("@@DUMP-BEGIN tag="); uart_puts(tag); uart_puts(" v="); uart_dec(d->version);
    uart_puts(" size="); uart_dec(d->size); uart_puts(" boot="); uart_dec(d->boot_count);
    uart_puts(" exc="); uart_dec(d->exc); uart_puts(" crc="); uart_hex(d->crc); uart_puts("\n");
    for (uint32_t off = 0; off < d->size; off += 32u) {
        uart_puts("@@HEX ");
        for (uint32_t i = off; i < off + 32u && i < d->size; ++i) {
            uart_putc("0123456789abcdef"[p[i] >> 4]); uart_putc("0123456789abcdef"[p[i] & 0xFu]);
        }
        uart_puts("\n");
    }
    uart_puts("@@DUMP-END\n");
    uart_puts("  crash in boot #"); uart_dec(d->boot_count); uart_puts(": exc "); uart_dec(d->exc);
    uart_puts(" CFSR="); uart_hex(d->cfsr); uart_puts(" HFSR="); uart_hex(d->hfsr);
    uart_puts(" pc="); uart_hex(d->pc); uart_puts(" lr="); uart_hex(d->lr);
    uart_puts(" sp="); uart_hex(d->sp); uart_puts((d->flags & F_PSP) ? " PSP" : " MSP");
    uart_puts((d->flags & F_HANDLER) ? "/handler" : "/thread"); uart_puts(" stage="); uart_dec(d->stage);
    uart_puts(" nstack="); uart_dec(d->nstack); uart_puts("\n");
}
static void report_nested(const nested_rec_t *n) {
    uart_puts("@@NESTED exc="); uart_dec(n->exc); uart_puts(" excret="); uart_hex(n->excret);
    uart_puts(" cfsr="); uart_hex(n->cfsr); uart_puts(" hfsr="); uart_hex(n->hfsr);
    uart_puts(" pc="); uart_hex(n->pc); uart_puts("\n");
}

/* ---------- 최종 검사 (부팅 9) ---------- */
#define CHECK(expr) do { total++; if (expr) { pass++; } else { \
    uart_puts("FAIL " __FILE__ ":"); uart_dec(__LINE__); uart_puts("  " #expr "  [step "); uart_dec(k); uart_puts("]\n"); } } while (0)
#define A(n) (0xA0000000u | ((uint32_t)(n) << 16) | (uint32_t)(n))
#define B(n) (0xB0000000u | ((uint32_t)(n) << 16) | (uint32_t)(n))
#define ADDR(sym) ((uint32_t)(uintptr_t)(sym))

static int final_report(void) {
    int pass = 0, total = 0;
    uint32_t k = NSTEPS;
    const uint32_t glo = ADDR(_sstack_guard), ghi = ADDR(_estack_guard);
    uart_puts("== post-mortem summary (QEMU mps2-an385) ==\n");
    uart_puts("step tag         rc          exc excret     flags      stage nstk CFSR       pc\n");
    for (k = 0; k < g_plan.nres; ++k) {
        const step_res_t *r = &g_plan.res[k];
        uart_dec(k); uart_puts("    "); uart_pad(k_step_name[k], 13u);
        uart_pad(r->rc < DV_N ? k_dv_name[r->rc] : "?", 12u);
        if (r->rc != DV_OK) { uart_puts("(no dump)\n"); continue; }
        uart_dec(r->d.exc); uart_puts(" "); uart_hex(r->d.exc_return); uart_puts(" "); uart_hex(r->d.flags);
        uart_puts(" "); uart_dec(r->d.stage); uart_puts(" "); uart_dec(r->d.nstack); uart_puts(" ");
        uart_hex(r->d.cfsr); uart_puts(" "); uart_hex(r->d.pc); uart_puts(r->nested_ok ? " +nested" : "");
        uart_puts("\n");
    }
    k = NSTEPS;
    CHECK(g_plan.cold_rc == DV_NO_MAGIC);                /* QEMU 콜드 부팅: RAM 0 → magic 없음 */
    CHECK(g_plan.cold_raw == 0u);
    CHECK(g_plan.nres == NSTEPS);
    CHECK(g_bootctr.count == NSTEPS + 1u);
    for (k = 0; k < NSTEPS; ++k) {
        const step_res_t *r = &g_plan.res[k];
        const crash_dump_t *d = &r->d;
        if (k < 7u) {                                    /* 진짜 fault 일곱 개: 공통 */
            CHECK(r->rc == DV_OK);
            CHECK(d->boot_count == k + 1u);
            CHECK(d->reset_reason == RR_FAULT);
            CHECK(dump_valid(d) == DV_OK);               /* 복사본도 CRC 가 맞는다 */
        }
        switch (k) {
        case 0:
            CHECK(d->exc == 6u && d->cfsr == CFSR_UNDEFINSTR && d->exc_return == 0xFFFFFFF9u);
            CHECK(d->flags == F_FRAME_OK && d->stage == 2u && d->nstack == DUMP_NSTACK);
            CHECK(d->pc == ADDR(pc_udf) && d->sp == d->msp);
            for (uint32_t i = 0; i < 13u; ++i) CHECK(d->r[i] == A(i));
            CHECK((d->lr & 1u) && d->lr > ADDR(_stext) && d->lr < ADDR(_etext));
            break;
        case 1:
            CHECK(d->exc == 5u && d->cfsr == (CFSR_PRECISERR | CFSR_BFARVALID) && d->bfar == BUS_HOLE);
            CHECK(d->pc == ADDR(pc_bus) && d->r[1] == BUS_HOLE && d->r[0] == B(0));
            for (uint32_t i = 2; i < 13u; ++i) CHECK(d->r[i] == B(i));
            break;
        case 2:
            CHECK(d->exc == 6u && d->cfsr == CFSR_DIVBYZERO && d->exc_return == 0xFFFFFFFDu);
            CHECK(d->flags == (F_PSP | F_FRAME_OK) && d->sp == d->psp && d->pc == ADDR(pc_div0));
            CHECK(d->psp == ADDR(&g_task_stack[256]) - 8u - 32u);
            CHECK(d->stack[0] == 0x7A5C0001u && d->stack[1] == 0x7A5C0002u);
            CHECK(d->r[0] == 7u && d->r[1] == 0u);
            break;
        case 3:
            CHECK(d->exc == 6u && d->cfsr == CFSR_UNDEFINSTR && d->exc_return == 0xFFFFFFF1u);
            CHECK(d->flags == (F_HANDLER | F_FRAME_OK) && d->pc == ADDR(pc_irq));
            CHECK((d->xpsr & 0x1FFu) == 16u + 7u);       /* 쌓인 xPSR 의 IPSR = IRQ7 */
            break;
        case 4:
            CHECK(d->exc == 4u && (d->cfsr & CFSR_MSTKERR) && (d->flags & F_STKERR) && !(d->flags & F_FRAME_OK));
            CHECK(d->pc == FRAME_BAD && d->nstack == 0u && d->stage == 1u);
            CHECK(d->msp >= glo && d->msp < ghi);        /* 진입 순간 MSP 는 가드 안 → 여기서 C 를 돌리면 안 된다 */
            CHECK(d->mmfar >= glo && d->mmfar < ghi && (d->cfsr & CFSR_DACCVIOL));
            break;
        case 5:
            CHECK(d->exc == 5u && d->cfsr == (CFSR_UNDEFINSTR | CFSR_STKERR) && d->flags == F_STKERR);
            CHECK(d->msp == 0x30000100u - 32u && d->pc == FRAME_BAD && d->stage == 1u);   /* 쌓기 실패해도 SP 는 −32 */
            break;
        case 6:
            CHECK(d->exc == 6u && d->cfsr == CFSR_UNDEFINSTR && d->pc == ADDR(pc_nest));
            CHECK(d->stage == 1u && d->nstack == 0u);    /* 2 단계는 못 끝냈지만 1 단계 덤프는 살았다 */
            CHECK(r->nested_ok && r->n.exc == 3u && (r->n.hfsr & HFSR_FORCED));
            CHECK((r->n.cfsr & (CFSR_PRECISERR | CFSR_BFARVALID)) == (CFSR_PRECISERR | CFSR_BFARVALID));
            CHECK(r->n.excret == 0xFFFFFFF1u && r->n.pc > ADDR(_stext) && r->n.pc < ADDR(_etext));
            break;
        case 7: CHECK(r->rc == DV_BAD_VERSION); break;
        case 8: CHECK(r->rc == DV_BAD_CRC); break;
        default: break;
        }
        if (k != 6u) CHECK(!r->nested_ok);
    }
    k = NSTEPS;
    CHECK(dump_valid(&g_dump.cur) == DV_NO_MAGIC);       /* 마지막 보고 후 깨끗이 지웠다 */
    uart_dec((uint32_t)pass); uart_puts("/"); uart_dec((uint32_t)total); uart_puts(" passed\n");
    return pass == total ? 0 : 1;
}

#ifndef FIRST_STEP                 /* 데모 빌드(run_qemu.sh naive/lockup)는 일부 단계만 돌린다 */
#define FIRST_STEP 0u
#endif
#ifndef LAST_STEP
#define LAST_STEP NSTEPS
#endif
int main(void) {
    uart_init();
#ifndef NO_FAULT_ENABLE                                   /* (run_qemu.sh lockup 은 일부러 끈다) */
    SCB_SHCSR |= (1u << 16) | (1u << 17) | (1u << 18);  /* MemManage/BusFault/UsageFault (BM-M13) */
#endif
    SCB_CCR |= 1u << 4;                                   /* DIV_0_TRP */
    uint32_t gsz_log2 = 0;                                /* 가드 크기 (a09.ld, 기본 64 B) → RASR.SIZE = log2 − 1 */
    while ((1u << gsz_log2) < ADDR(_estack_guard) - ADDR(_sstack_guard)) gsz_log2++;
    MPU_RNR = 0u;                                         /* 주 스택 가드: 접근 금지 영역 (자세히는 BM-A08) */
    MPU_RBAR = ADDR(_sstack_guard);
    MPU_RASR = (1u << 28) | (0u << 24) | ((gsz_log2 - 1u) << 1) | 1u;  /* XN, AP=000 (특권도 금지), SIZE, ENABLE */
    MPU_CTRL = (1u << 2) | 1u;                            /* PRIVDEFENA | ENABLE (HFNMIENA=0) */
    dsb(); isb();

    if (g_bootctr.inv != ~g_bootctr.count) g_bootctr.count = 0u;   /* 콜드: 쓰레기 → 0 */
    g_bootctr.count++; g_bootctr.inv = ~g_bootctr.count;
    uart_puts("== boot #"); uart_dec(g_bootctr.count); uart_puts(" ==\n");
    if (g_bootctr.count > MAX_BOOTS) { uart_puts("boot-loop guard\n"); return 3; }

    const int rc = dump_valid(&g_dump.cur);
    const int nok = nested_valid(&g_nested);
    if (!plan_valid()) {                                  /* 콜드 부팅 */
        uart_puts("  cold boot: .noinit @"); uart_hex(ADDR(&g_dump)); uart_puts(" raw[0..3]=");
        for (uint32_t i = 0; i < 4u; ++i) { uart_hex(g_dump.w[i]); uart_puts(" "); }
        uart_puts("\n");
        for (uint32_t i = 0; i < sizeof g_plan / 4u; ++i) ((uint32_t *)&g_plan)[i] = 0u;
        g_plan.next = FIRST_STEP; g_plan.nres = FIRST_STEP;
        g_plan.cold_rc = (uint32_t)rc; g_plan.cold_raw = g_dump.w[0] | g_dump.w[1] | g_nested.magic;
    } else if (g_plan.nres < g_plan.next) {              /* 직전 부팅이 주입한 단계의 결과 */
        step_res_t *r = &g_plan.res[g_plan.nres++];
        r->rc = (uint32_t)rc; r->nested_ok = (uint32_t)nok;
        copy_words((uint32_t *)&r->d, g_dump.w, sizeof(crash_dump_t) / 4u);
        copy_words((uint32_t *)&r->n, (const uint32_t *)&g_nested, sizeof(nested_rec_t) / 4u);
    }
    if (rc == DV_OK) report_dump(&g_dump.cur, g_plan.nres ? k_step_name[g_plan.nres - 1u] : "?");
    else { uart_puts("@@NODUMP rc="); uart_puts(k_dv_name[rc]); uart_puts(" magic="); uart_hex(g_dump.w[0]);
           uart_puts(" ver="); uart_dec(g_dump.w[1]); uart_puts("\n"); }
    if (nok) report_nested(&g_nested);
    g_dump.cur.magic = (rc == DV_OK) ? DUMP_CONSUMED : 0u;   /* 소비 표시 / 쓰레기 정리 */
    g_nested.magic = 0u;
    plan_seal();

    if (g_plan.next < LAST_STEP) {
        const uint32_t k = g_plan.next++;
        plan_seal();                                      /* 주입 "전에" 진행을 봉인 */
        uart_puts("  inject "); uart_dec(k); uart_puts(": "); uart_puts(k_step_name[k]); uart_puts("\n");
        inject(k);
        uart_puts("  no fault happened! reset\n");
        system_reset();
    }
    if (LAST_STEP < NSTEPS) { uart_puts("demo build: done\n"); return 0; }
    return final_report();
}

__attribute__((noreturn)) void Reset_Handler(void) {
    const uint32_t *src = _sidata;
    for (uint32_t *dst = _sdata; dst < _edata;) *dst++ = *src++;
    for (uint32_t *dst = _sbss; dst < _ebss;) *dst++ = 0u;      /* .noinit 은 건드리지 않는다 */
    sh_exit(main());
}

#else /* HOST_TEST */
/* ============================== 호스트 시험 (같은 검증 코드) ============================== */
static int pass, total;
#define CHECK(expr) do { total++; if (expr) pass++; else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t g_rng = 0x2026A09u;
static uint32_t xs32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }
typedef union { crash_dump_t d; crash_dump_v1_t v1; uint32_t w[51]; uint8_t b[204]; } area_t;

static void make_valid(area_t *a) {
    for (uint32_t i = 0; i < 51u; ++i) a->w[i] = 0x01010101u * (i + 1u);
    a->d.version = DUMP_VERSION; a->d.size = 204u; a->d.stage = 2u; a->d.nstack = 16u;
    dump_seal(&a->d);
}
static int expected_code_for_flip(uint32_t byte) {   /* 1 비트 뒤집힘이 어느 필드냐에 따라 */
    if (byte < 4u) return DV_NO_MAGIC;
    if (byte < 8u) return DV_BAD_VERSION;
    if (byte < 12u) return DV_BAD_SIZE;
    return DV_BAD_CRC;
}

int main(void) {
    const stack_bounds_t b = { 0x20000000u, 0x2000F000u, 0x20001000u, 0x20001020u };
    /* H1: CRC-32 확인값 */
    CHECK(crc32("123456789", 9u) == 0xCBF43926u);
    CHECK(crc32("", 0u) == 0u);
    /* H2: select_frame 이름 붙은 경우 */
    frame_sel_t s;
    s = select_frame(0xFFFFFFF9u, 0x20002000u, 0x20003000u, 0u, &b);
    CHECK(s.frame == 0x20002000u && s.flags == F_FRAME_OK && s.fsize == 32u);
    s = select_frame(0xFFFFFFFDu, 0x20002000u, 0x20003000u, 0u, &b);
    CHECK(s.frame == 0x20003000u && s.flags == (F_PSP | F_FRAME_OK));
    s = select_frame(0xFFFFFFF1u, 0x20002000u, 0x20003000u, 0u, &b);
    CHECK(s.frame == 0x20002000u && s.flags == (F_HANDLER | F_FRAME_OK));
    s = select_frame(0xFFFFFFE9u, 0x20002000u, 0u, 0u, &b);
    CHECK(s.frame == 0x20002000u && s.flags == (F_FPFRAME | F_FRAME_OK) && s.fsize == 104u);
    s = select_frame(0xFFFFFFF9u, 0x20002000u, 0u, CFSR_MSTKERR | CFSR_DACCVIOL, &b);
    CHECK(s.frame == 0u && s.flags == F_STKERR && s.sp == 0x20002000u);
    s = select_frame(0xFFFFFFFDu, 0x20002000u, 0x30000000u, CFSR_STKERR, &b);
    CHECK(s.frame == 0u && s.flags == (F_PSP | F_STKERR) && s.sp == 0x30000000u);
    CHECK(select_frame(0xFFFFFFFDu, 0, 0x30000000u, 0u, &b).frame == 0u);            /* RAM 밖 PSP */
    CHECK(select_frame(0xFFFFFFF9u, 0x20002002u, 0, 0u, &b).frame == 0u);            /* 비정렬 */
    CHECK(select_frame(0xFFFFFFF9u, 0x20001010u, 0, 0u, &b).frame == 0u);            /* 가드 안 */
    CHECK(select_frame(0xFFFFFFF9u, 0x20000FF0u, 0, 0u, &b).frame == 0u);            /* 가드에 걸침 */
    CHECK(select_frame(0xFFFFFFF9u, 0x20000FE0u, 0, 0u, &b).frame == 0x20000FE0u);   /* 가드 바로 아래 끝 */
    CHECK(select_frame(0xFFFFFFF9u, 0x2000EFE0u, 0, 0u, &b).frame == 0x2000EFE0u);   /* RAM 끝에 딱 */
    CHECK(select_frame(0xFFFFFFF9u, 0x2000EFE4u, 0, 0u, &b).frame == 0u);            /* RAM 끝 넘음 */
    CHECK(select_frame(0xFFFFFFF9u, 0xFFFFFFF0u, 0, 0u, &b).frame == 0u);            /* 오버플로 */
    CHECK(select_frame(0xFFFFFFE9u, 0x2000EFA0u, 0, 0u, &b).frame == 0u);            /* FP 프레임 104 B 넘음 */
    s = select_frame(0x0800012Bu, 0x20002000u, 0, 0u, &b);                           /* LR 이 코드 주소 */
    CHECK(s.frame == 0u && s.flags == F_BAD_EXCRET);
    CHECK(select_frame(0xFFFFFFF5u, 0x20002000u, 0, 0u, &b).flags == F_BAD_EXCRET);
    CHECK(select_frame(0xFFFFFF79u, 0x20002000u, 0, 0u, &b).flags == F_BAD_EXCRET);
    /* H3: 성질 검사 — 무작위 20 만 건: 프레임을 골랐다면 RAM 안·가드 밖·정렬 */
    uint32_t viol = 0, chosen = 0;
    static const uint32_t legal[6] = { 0xFFFFFFF1u, 0xFFFFFFF9u, 0xFFFFFFFDu, 0xFFFFFFE1u, 0xFFFFFFE9u, 0xFFFFFFEDu };
    for (uint32_t i = 0; i < 200000u; ++i) {
        uint32_t e = (i & 7u) < 6u ? legal[i & 7u] : xs32();
        uint32_t sp = (i & 1u) ? 0x20000000u + (xs32() & 0xFFFFu) : xs32();
        uint32_t cf = (xs32() & 7u) == 0u ? CFSR_STKERR : 0u;
        s = select_frame(e, sp, sp, cf, &b);
        if (s.frame) {
            chosen++;
            if (s.frame & 3u || s.frame < b.ram_lo || (uint64_t)s.frame + s.fsize > b.ram_hi ||
                (s.frame < b.guard_hi && s.frame + s.fsize > b.guard_lo) || cf || !excret_legal(e)) viol++;
        }
    }
    printf("  H3 select_frame: 200000 random cases, frame chosen %u, violations %u\n", chosen, viol);
    CHECK(viol == 0u && chosen > 10000u);
    /* H4: 봉인 → 유효, 1 비트 뒤집힘 전수 (204 B × 8 = 1632) → 필드별 정확한 코드 */
    area_t a;
    make_valid(&a);
    CHECK(dump_valid(&a.d) == DV_OK);
    uint32_t flips = 0, wrong = 0;
    for (uint32_t byte = 0; byte < 204u; ++byte)
        for (uint32_t bit = 0; bit < 8u; ++bit) {
            area_t c = a;
            c.b[byte] ^= (uint8_t)(1u << bit);
            flips++;
            if (dump_valid(&c.d) != expected_code_for_flip(byte)) wrong++;
        }
    printf("  H4 single-bit flips: %u, wrong code %u\n", flips, wrong);
    CHECK(flips == 1632u && wrong == 0u);
    /* H5: 이름 붙은 경우 */
    area_t c = a; c.d.magic = 0u;                         CHECK(dump_valid(&c.d) == DV_NO_MAGIC);   /* 찢어진 커밋 */
    c = a; c.d.magic = DUMP_CONSUMED;                     CHECK(dump_valid(&c.d) == DV_CONSUMED);
    c = a; c.d.version = 3u; dump_seal(&c.d);             CHECK(dump_valid(&c.d) == DV_BAD_VERSION);
    c = a; c.d.size = 200u; dump_seal(&c.d);              CHECK(dump_valid(&c.d) == DV_BAD_SIZE);
    c = a; c.d.nstack = 17u; dump_seal(&c.d);             CHECK(dump_valid(&c.d) == DV_BAD_FIELD);
    c = a; c.d.stage = 0u; dump_seal(&c.d);               CHECK(dump_valid(&c.d) == DV_BAD_FIELD);
    c = a; c.d.stage = 1u; dump_seal(&c.d);               CHECK(dump_valid(&c.d) == DV_BAD_FIELD);  /* stage1 + nstack16 */
    c = a; c.d.stage = 1u; c.d.nstack = 0u; dump_seal(&c.d); CHECK(dump_valid(&c.d) == DV_OK);
    c = a; c.d.pc ^= 0x10u; dump_seal(&c.d);              CHECK(dump_valid(&c.d) == DV_OK);         /* 다시 봉인하면 유효 */
    for (uint32_t i = 0; i < 51u; ++i) { c.w[i] = 0u; }
    CHECK(dump_valid(&c.d) == DV_NO_MAGIC);                                                       /* QEMU 콜드 RAM */
    for (uint32_t i = 0; i < 51u; ++i) { c.w[i] = ~0u; }
    CHECK(dump_valid(&c.d) == DV_NO_MAGIC);                                                       /* 0xFF 채움 */
    for (uint32_t i = 0; i < 51u; ++i) { c.w[i] = 0u; }                                           /* 낡은 v1, v1 CRC 정상 */
    c.v1.version = 1u; c.v1.size = 32u; c.v1.exc = 6u; c.v1.pc = 0x1234u; c.v1.crc = crc32(&c.v1.version, 24u);
    c.v1.magic = DUMP_MAGIC;                              CHECK(dump_valid(&c.d) == DV_BAD_VERSION);
    /* H6: 콜드 부팅 쓰레기 1e6 건 (모든 바이트 무작위) */
    uint32_t hist[DV_N] = { 0 };
    for (uint32_t i = 0; i < 1000000u; ++i) {
        for (uint32_t j = 0; j < 51u; ++j) c.w[j] = xs32();
        hist[dump_valid(&c.d)]++;
    }
    printf("  H6 1e6 random fills:");
    for (uint32_t i = 0; i < DV_N; ++i) printf(" %s %u", k_dv_name[i], hist[i]);
    printf("\n");
    CHECK(hist[DV_OK] == 0u);
    /* H7: 헤더(magic, version, size)는 맞고 나머지 무작위 1e6 건 → CRC 만이 거른다 */
    for (uint32_t i = 0; i < DV_N; ++i) hist[i] = 0u;
    for (uint32_t i = 0; i < 1000000u; ++i) {
        for (uint32_t j = 0; j < 51u; ++j) c.w[j] = xs32();
        c.d.magic = DUMP_MAGIC; c.d.version = DUMP_VERSION; c.d.size = 204u;
        hist[dump_valid(&c.d)]++;
    }
    printf("  H7 1e6 header-valid fills: OK %u BAD_CRC %u BAD_FIELD %u (expected false accepts 1e6/2^32 = %.6f)\n",
           hist[DV_OK], hist[DV_BAD_CRC], hist[DV_BAD_FIELD], 1e6 / 4294967296.0);
    CHECK(hist[DV_OK] == 0u && hist[DV_BAD_FIELD] == 0u && hist[DV_BAD_CRC] == 1000000u);
    printf("%d/%d passed\n", pass, total);
    return pass == total ? 0 : 1;
}
#endif /* HOST_TEST */
