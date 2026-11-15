/* ENV: qemu */
/* BM-M13 Fault 처리 정책: 세분 fault 활성화와 안전 상태 진입
 *
 * 펌웨어 한 파일 = startup (BM-B08 축약판) + fault 정책 + "한 부팅에 fault 하나" 주입 시험.
 *   부팅 1: 콜드 부팅 → 계획(.noinit) 초기화 → fault 0 주입 → 핸들러: 출력 안전 → 기록 → SYSRESETREQ
 *   부팅 2: 기록 확인·보고 → fault 1 주입 → ...                 부팅 8: 요약 표 + 검사 → SYS_EXIT 0/1
 * 대상: qemu-system-arm -M mps2-an385 (Cortex-M3), 링커 스크립트 m13.ld, 출력은 세미호스팅.
 * 빌드/실행 (Lima VM elinux 안): bash run_qemu.sh solution|starter|probe|noreboot|bssbug|practice
 */
#include <stddef.h>
#include <stdint.h>

extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[], _snoinit[], _enoinit[];

/* ---------------- 세미호스팅 (BM-B08 축약판) ---------------- */
static int semi(int op, const void *arg) {
    register int r0 __asm__("r0") = op;
    register const void *r1 __asm__("r1") = arg;
    __asm__ volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}
static void sh_puts(const char *s) { semi(0x04, s); }                        /* SYS_WRITE0 */
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
    uint32_t blk[2] = { 0x20026u, (uint32_t)code };                          /* SYS_EXIT_EXTENDED */
    semi(0x20, blk);
    for (;;) { }
}

/* ---------------- 레지스터 (ARMv7-M ARM 기준 — 확인 권장) ---------------- */
#define REG32(a)        (*(volatile uint32_t *)(uintptr_t)(a))
#define SCB_AIRCR       REG32(0xE000ED0Cu)
#define SCB_CCR         REG32(0xE000ED14u)
#define SCB_SHCSR       REG32(0xE000ED24u)
#define SCB_CFSR        REG32(0xE000ED28u)
#define SCB_HFSR        REG32(0xE000ED2Cu)
#define SCB_MMFAR       REG32(0xE000ED34u)
#define SCB_BFAR        REG32(0xE000ED38u)
#define MPU_CTRL        REG32(0xE000ED94u)
#define MPU_RNR         REG32(0xE000ED98u)
#define MPU_RBAR        REG32(0xE000ED9Cu)
#define MPU_RASR        REG32(0xE000EDA0u)
#define NVIC_ISER0      REG32(0xE000E100u)
#define NVIC_ISPR0      REG32(0xE000E200u)
#define FPGAIO_LED0     REG32(0x40028000u)   /* mps2 FPGAIO LED0 (QEMU: 2 비트만 남음 — probe 로 확인) */

#define SHCSR_MEMFAULTENA (1u << 16)
#define SHCSR_BUSFAULTENA (1u << 17)
#define SHCSR_USGFAULTENA (1u << 18)
#define CCR_UNALIGN_TRP   (1u << 3)
#define CCR_DIV_0_TRP     (1u << 4)
#define CFSR_DACCVIOL     (1u << 1)
#define CFSR_MMARVALID    (1u << 7)
#define CFSR_PRECISERR    (1u << 9)
#define CFSR_BFARVALID    (1u << 15)
#define CFSR_UNDEFINSTR   (1u << 16)
#define CFSR_UNALIGNED    (1u << 24)
#define CFSR_DIVBYZERO    (1u << 25)
#define HFSR_FORCED       (1u << 30)
#define AIRCR_VECTKEY     (0x05FAu << 16)
#define AIRCR_PRIGROUP_Msk (7u << 8)
#define AIRCR_SYSRESETREQ (1u << 2)

/* "출력 핀" = LED0 비트: bit0 MOTOR_EN, bit1 HEATER_EN. 안전 상태 = 둘 다 끔. */
#define OUT_ACTIVE 0x3u
#define OUT_SAFE   0x0u

#define dsb() __asm__ volatile("dsb" ::: "memory")
#define isb() __asm__ volatile("isb" ::: "memory")

/* ---------------- 리셋을 건너 살아남는 기록 (.noinit, m13.ld) ---------------- */
#define CRASH_MAGIC 0xFA17C0DEu
#define PLAN_MAGIC  0x504C414Eu   /* "PLAN" */
#define NSTEPS      7u
#define MAX_BOOTS   12u
#define FRAME_BAD   0xFFFFFFFFu

typedef struct {
    uint32_t magic;     /* 마지막에 쓴다 = "기록 완료" 표시 */
    uint32_t reason;    /* 들어온 예외 번호 (3 Hard, 4 MemManage, 5 Bus, 6 Usage), 0 = fault 없음 */
    uint32_t cfsr, hfsr, bfar, mmfar;
    uint32_t pc, lr, xpsr;   /* 하드웨어가 쌓은 프레임 */
    uint32_t outputs;   /* 안전 상태로 만든 뒤 출력 레지스터를 다시 읽은 값 */
    uint32_t check;     /* FNV-1a(reason … outputs, 9 워드) */
} crash_rec_t;

typedef struct {
    uint32_t magic, boots, next, nres;
    uint32_t boot_dirty;          /* 부팅 직후 CFSR/HFSR/LED 가 0 이 아니었던 부팅의 비트마스크 */
    crash_rec_t res[NSTEPS];
    uint32_t check;
} plan_t;

#ifdef PERSIST_IN_BSS            /* 결함 주입 (bssbug): .noinit 대신 .bss 에 → reset_init 이 매번 0 으로 */
#define NOINIT
#else
#define NOINIT __attribute__((section(".noinit")))
#endif
NOINIT crash_rec_t g_crash;
NOINIT plan_t g_plan;

static uint32_t fnv1a_words(const uint32_t *w, uint32_t n) {   /* 바이트 단위 FNV-1a, 리틀엔디언 */
    uint32_t h = 0x811C9DC5u;
    for (uint32_t i = 0; i < n; ++i)
        for (uint32_t s = 0; s < 32u; s += 8u) { h ^= (w[i] >> s) & 0xFFu; h *= 0x01000193u; }
    return h;
}
#define CRASH_WORDS ((uint32_t)((offsetof(crash_rec_t, check) - offsetof(crash_rec_t, reason)) / 4u))
#define PLAN_WORDS  ((uint32_t)((offsetof(plan_t, check) - offsetof(plan_t, boots)) / 4u))
static int crash_valid(const crash_rec_t *r) {
    return r->magic == CRASH_MAGIC && r->check == fnv1a_words(&r->reason, CRASH_WORDS);
}
static int plan_valid(void) {
    return g_plan.magic == PLAN_MAGIC && g_plan.check == fnv1a_words(&g_plan.boots, PLAN_WORDS) &&
           g_plan.next <= NSTEPS && g_plan.nres <= NSTEPS;
}
static void plan_seal(void) { g_plan.check = fnv1a_words(&g_plan.boots, PLAN_WORDS); dsb(); }

/* ---------------- 리셋: AIRCR.SYSRESETREQ (CMSIS NVIC_SystemReset 모양) ---------------- */
__attribute__((noreturn)) static void system_reset(void) {
    dsb();                                              /* 앞선 쓰기(기록)를 먼저 끝낸다 */
    SCB_AIRCR = AIRCR_VECTKEY | (SCB_AIRCR & AIRCR_PRIGROUP_Msk) | AIRCR_SYSRESETREQ;
    dsb();
    for (;;) { __asm__ volatile("nop"); }               /* 리셋이 실제로 걸릴 때까지 */
}

/* ================= TODO 1: fault 세분 활성화 =================
 * SHCSR 의 MEMFAULTENA / BUSFAULTENA / USGFAULTENA 를 켜고 (나머지 비트는 보존),
 * CCR 의 DIV_0_TRP / UNALIGN_TRP 를 켠다. 그 다음 DSB + ISB. */
static void fault_policy_enable(void) {
    /* TODO: SHCSR 3 비트, CCR 2 비트, DSB/ISB */
}

/* ================= TODO 2: 출력을 안전 상태로 =================
 * LED0 (= MOTOR_EN | HEATER_EN) 에 OUT_SAFE 를 쓰고, 버스 쓰기가 끝났음을 보장(DSB)한 뒤
 * 다시 읽어 확인한다. 반환값 = 다시 읽은 값. 이 함수는 fault 핸들러 안에서 "가장 먼저" 불린다. */
static uint32_t safe_outputs(void) {
    /* TODO: OUT_SAFE 쓰기 → DSB → 다시 읽기 */
    return FPGAIO_LED0;
}

/* ================= TODO 3: fault 주소는 VALID 비트가 있을 때만 =================
 * BFAR 은 CFSR.BFARVALID, MMFAR 은 CFSR.MMARVALID 가 1 일 때만 의미가 있다 (아니면 0 을 돌려준다). */
static void fault_address(uint32_t cfsr, uint32_t *bfar, uint32_t *mmfar) {
    (void)cfsr;                                          /* TODO: VALID 비트 확인 후 BFAR / MMFAR */
    *bfar = 0u;
    *mmfar = 0u;
}

/* ---------------- 기록 작성: 내용 → 체크값 → (DSB) → magic 순서 ---------------- */
static int frame_in_ram(const uint32_t *frame) {
    uintptr_t a = (uintptr_t)frame;
    return a >= 0x20000000u && a + 32u <= (uintptr_t)_estack && (a & 3u) == 0u;
}
static void crash_commit(uint32_t reason, const uint32_t *frame, uint32_t outputs) {
    crash_rec_t *r = &g_crash;
    r->magic = 0u;                                       /* 쓰는 중에는 무효 */
    r->reason = reason;
    r->cfsr = SCB_CFSR;
    r->hfsr = SCB_HFSR;
    fault_address(r->cfsr, &r->bfar, &r->mmfar);
    if (frame && frame_in_ram(frame)) { r->lr = frame[5]; r->pc = frame[6]; r->xpsr = frame[7]; }
    else { r->lr = FRAME_BAD; r->pc = FRAME_BAD; r->xpsr = FRAME_BAD; }
    r->outputs = outputs;
    r->check = fnv1a_words(&r->reason, CRASH_WORDS);     /* 체크값: magic 뒤의 내용 9 워드 */
    dsb();                                               /* 내용·체크값이 먼저 RAM 에 … */
    r->magic = CRASH_MAGIC;                              /* … magic 은 맨 마지막 ("커밋") */
    dsb();
}

static const char *exc_name(uint32_t e) {
    switch (e) {
    case 0: return "none      ";
    case 3: return "HardFault ";
    case 4: return "MemManage ";
    case 5: return "BusFault  ";
    case 6: return "UsageFault";
    default: return "other     ";
    }
}

/* 네 개의 fault 벡터가 모두 여기로: 어느 스택(MSP/PSP)에 프레임이 있는지 고르고, IPSR 과 함께 C 로 */
__attribute__((used, noreturn)) void fault_policy(const uint32_t *frame, uint32_t ipsr) {
    uint32_t out = safe_outputs();                       /* 1) 출력 안전 상태 — 무엇보다 먼저 */
    crash_commit(ipsr & 0x1FFu, frame, out);             /* 2) 기록 (리셋을 건너 살아남는 RAM) */
    sh_puts("  [fault] "); sh_puts(exc_name(ipsr & 0x1FFu));  /* 3) 기록이 끝난 뒤에야 로그 (아래 Q&A) */
    sh_puts(" CFSR="); sh_hex(g_crash.cfsr); sh_puts(" -> outputs="); sh_hex(out);
    sh_puts(", record sealed, SYSRESETREQ\n");
    system_reset();                                      /* 4) 리셋 */
}
__attribute__((naked)) void Fault_Entry(void) {
    __asm__ volatile(
        "tst   lr, #4      \n"
        "ite   eq          \n"
        "mrseq r0, msp     \n"
        "mrsne r0, psp     \n"
        "mrs   r1, ipsr    \n"
        "b     fault_policy\n");
}
void HardFault_Handler(void)  __attribute__((alias("Fault_Entry")));
void MemManage_Handler(void)  __attribute__((alias("Fault_Entry")));
void BusFault_Handler(void)   __attribute__((alias("Fault_Entry")));
void UsageFault_Handler(void) __attribute__((alias("Fault_Entry")));

/* ---------------- startup (BM-B08 축약판) ---------------- */
void Reset_Handler(void);
void Irq7_Handler(void);
void Default_Handler(void) {
    uint32_t ipsr;
    __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
    sh_puts("Default_Handler: unexpected exception "); sh_dec(ipsr); sh_puts("\n");
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

/* ---------------- fault 주입: 각 명령에 전역 라벨 → 쌓인 PC 와 정확히 비교 ---------------- */
extern const char pc_div0[], pc_unal[], pc_bus[], pc_udf[], pc_mpu[], pc_esc[], pc_isr[];
#define BUS_HOLE 0x30000000u          /* QEMU info mtree 에서 아무것도 매핑 안 된 주소 (probe 로 확인) */
static uint32_t g_guard[8] __attribute__((aligned(32)));    /* MPU 로 읽기 전용이 될 32 B "스택 가드" */
static uint32_t g_words[2] = { 0x33221100u, 0x77665544u };
volatile uint32_t g_zero;             /* 0 — 컴파일러가 상수로 접지 못하게 volatile */
volatile uint32_t g_sink;

__attribute__((noinline, noclone)) static uint32_t inject_div0(uint32_t a, uint32_t b) {
    uint32_t q = 0;
    __asm__ volatile(".global pc_div0\npc_div0: udiv %0, %1, %2" : "=r"(q) : "r"(a), "r"(b));
    return q;
}
__attribute__((noinline, noclone)) static uint32_t inject_load(uint32_t addr, int which) {
    uint32_t v = 0;
    if (which == 0) __asm__ volatile(".global pc_unal\npc_unal: ldr %0, [%1]" : "=r"(v) : "r"(addr) : "memory");
    else            __asm__ volatile(".global pc_bus\npc_bus: ldr %0, [%1]" : "=r"(v) : "r"(addr) : "memory");
    return v;
}
__attribute__((noinline, noclone)) static void inject_udf(void) {
    __asm__ volatile(".global pc_udf\npc_udf: udf #1");
}
__attribute__((noinline, noclone)) static void inject_mpu_store(uint32_t *p) {
    MPU_RNR = 0u;                                        /* 영역 0: g_guard 32 B, 읽기 전용, 실행 금지 */
    MPU_RBAR = (uint32_t)(uintptr_t)p;                   /* 32 B 정렬된 주소, VALID=0 → RNR 사용 */
    MPU_RASR = (1u << 28) | (6u << 24) | (1u << 17) | (4u << 1) | 1u;  /* XN, AP=110(RO), C, SIZE=4(32 B), EN */
    MPU_CTRL = (1u << 2) | 1u;                           /* PRIVDEFENA | ENABLE */
    dsb(); isb();
    __asm__ volatile(".global pc_mpu\npc_mpu: str %1, [%0]" :: "r"(p), "r"(0xBADu) : "memory");
}
__attribute__((noinline, noclone)) static void inject_escalate(void) {
    SCB_SHCSR &= ~SHCSR_USGFAULTENA;                     /* UsageFault 를 끈 채로 */
    dsb(); isb();
    __asm__ volatile(".global pc_esc\npc_esc: udf #2");
}
void Irq7_Handler(void) {                                /* 우선순위 0 IRQ 안에서 UDF: UsageFault 도 0 → 승격 */
    __asm__ volatile(".global pc_isr\npc_isr: udf #3");
}
static void inject_in_isr(void) {
    NVIC_ISER0 = 1u << 7;
    NVIC_ISPR0 = 1u << 7;
    dsb(); isb();
}

static const char *const k_step_name[NSTEPS] = {
    "DIV0     ", "UNALIGNED", "BUSFAULT ", "UNDEF    ", "MPU_GUARD", "ESC_OFF  ", "ESC_PRIO "
};
static void inject(uint32_t k) {
    switch (k) {
    case 0: g_sink = inject_div0(7u, g_zero); break;
    case 1: g_sink = inject_load((uint32_t)(uintptr_t)g_words + 1u, 0); break;
    case 2: g_sink = inject_load(BUS_HOLE, 1); break;
    case 3: inject_udf(); break;
    case 4: inject_mpu_store(g_guard); break;
    case 5: inject_escalate(); break;
    case 6: inject_in_isr(); break;
    default: break;
    }
}

/* 단계별 기대값 (QEMU 10.2.1 mps2-an385 에서 확인, ARMv7-M ARM 규칙과 일치) */
typedef struct { uint32_t exc, cfsr, hfsr, bfar, mmfar, pc; } expect_t;
static expect_t expected(uint32_t k) {
    expect_t e = { 0u, 0u, 0u, 0u, 0u, 0u };
    switch (k) {
    case 0: e.exc = 6u; e.cfsr = CFSR_DIVBYZERO;  e.pc = (uint32_t)(uintptr_t)pc_div0; break;
    case 1: e.exc = 6u; e.cfsr = CFSR_UNALIGNED;  e.pc = (uint32_t)(uintptr_t)pc_unal; break;
    case 2: e.exc = 5u; e.cfsr = CFSR_PRECISERR | CFSR_BFARVALID; e.bfar = BUS_HOLE;
            e.pc = (uint32_t)(uintptr_t)pc_bus; break;
    case 3: e.exc = 6u; e.cfsr = CFSR_UNDEFINSTR; e.pc = (uint32_t)(uintptr_t)pc_udf; break;
    case 4: e.exc = 4u; e.cfsr = CFSR_DACCVIOL | CFSR_MMARVALID; e.mmfar = (uint32_t)(uintptr_t)g_guard;
            e.pc = (uint32_t)(uintptr_t)pc_mpu; break;
    case 5: e.exc = 3u; e.cfsr = CFSR_UNDEFINSTR; e.hfsr = HFSR_FORCED; e.pc = (uint32_t)(uintptr_t)pc_esc; break;
    case 6: e.exc = 3u; e.cfsr = CFSR_UNDEFINSTR; e.hfsr = HFSR_FORCED; e.pc = (uint32_t)(uintptr_t)pc_isr; break;
    default: break;
    }
    return e;
}

/* ---------------- 리셋 사유 보고 (다음 부팅에서) ---------------- */
static void cfsr_names(uint32_t c) {
    static const struct { uint32_t bit; const char *name; } k[] = {
        { 1u << 0, "IACCVIOL" }, { CFSR_DACCVIOL, "DACCVIOL" }, { CFSR_MMARVALID, "MMARVALID" },
        { CFSR_PRECISERR, "PRECISERR" }, { 1u << 10, "IMPRECISERR" }, { CFSR_BFARVALID, "BFARVALID" },
        { CFSR_UNDEFINSTR, "UNDEFINSTR" }, { 1u << 17, "INVSTATE" }, { CFSR_UNALIGNED, "UNALIGNED" },
        { CFSR_DIVBYZERO, "DIVBYZERO" },
    };
    uint32_t known = 0;
    sh_puts("[");
    for (size_t i = 0; i < sizeof k / sizeof k[0]; ++i) {
        known |= k[i].bit;
        if (c & k[i].bit) { sh_puts(" "); sh_puts(k[i].name); }
    }
    if (c & ~known) { sh_puts(" +"); sh_hex(c & ~known); }   /* 나머지 비트 해독은 ARM-M12 */
    sh_puts(" ]");
}
static void report_reset_cause(const crash_rec_t *r) {
    if (r->reason == 0u) sh_puts("  reset cause: SOFTWARE (injected fault did not trap) ");
    else { sh_puts("  reset cause: FAULT "); sh_puts(exc_name(r->reason)); }
    if (r->hfsr & HFSR_FORCED) sh_puts("(FORCED)");
    sh_puts(" CFSR="); sh_hex(r->cfsr); sh_puts(" "); cfsr_names(r->cfsr);
    sh_puts("\n    pc="); sh_hex(r->pc); sh_puts(" lr="); sh_hex(r->lr);
    if (r->cfsr & CFSR_BFARVALID) { sh_puts(" BFAR="); sh_hex(r->bfar); }
    if (r->cfsr & CFSR_MMARVALID) { sh_puts(" MMFAR="); sh_hex(r->mmfar); }
    sh_puts(" outputs="); sh_hex(r->outputs); sh_puts("\n");
}

/* ---------------- 시험 하네스 (카운터는 지역 변수) ---------------- */
#define CHECK(expr) do { total++; if (expr) { pass++; } else { \
    sh_puts("FAIL " __FILE__ ":"); sh_dec(__LINE__); sh_puts("  " #expr "  [step "); sh_dec(k); sh_puts("]\n"); } } while (0)

static int final_report(void) {
    int pass = 0, total = 0;
    uint32_t k = 0;
    sh_puts("== reset-cause summary (QEMU mps2-an385) ==\n");
    sh_puts("step fault     handler    HFSR       CFSR       fault-addr stacked-pc expect-pc  out\n");
    for (k = 0; k < g_plan.nres; ++k) {
        const crash_rec_t *r = &g_plan.res[k];
        expect_t e = expected(k);
        sh_dec(k); sh_puts("    "); sh_puts(k_step_name[k]); sh_puts(" "); sh_puts(exc_name(r->reason));
        sh_puts(" "); sh_hex(r->hfsr); sh_puts(" "); sh_hex(r->cfsr); sh_puts(" ");
        sh_hex(r->bfar | r->mmfar); sh_puts(" "); sh_hex(r->pc); sh_puts(" "); sh_hex(e.pc);
        sh_puts(" "); sh_hex(r->outputs); sh_puts("\n");
    }
    for (k = 0; k < NSTEPS; ++k) {
        const crash_rec_t *r = &g_plan.res[k];
        expect_t e = expected(k);
        CHECK(crash_valid(r));
        CHECK(r->reason == e.exc);
        CHECK(r->cfsr == e.cfsr);
        CHECK(r->hfsr == e.hfsr);
        CHECK(r->bfar == e.bfar);
        CHECK(r->mmfar == e.mmfar);
        CHECK(r->pc == e.pc);
        CHECK(r->outputs == OUT_SAFE);
    }
    k = NSTEPS;
    CHECK(g_plan.nres == NSTEPS);
    CHECK(g_plan.boots == NSTEPS + 1u);
    CHECK(g_plan.boot_dirty == 0u);               /* 매 부팅: 리셋이 CFSR/HFSR/LED0/SHCSR 를 지웠다 */
    CHECK((SCB_SHCSR & (7u << 16)) == (7u << 16));
    CHECK((SCB_CCR & (CCR_DIV_0_TRP | CCR_UNALIGN_TRP)) == (CCR_DIV_0_TRP | CCR_UNALIGN_TRP));
    CHECK((uintptr_t)&g_plan >= (uintptr_t)_snoinit && (uintptr_t)(&g_plan + 1) <= (uintptr_t)_enoinit);
    sh_dec((uint32_t)pass); sh_puts("/"); sh_dec((uint32_t)total); sh_puts(" passed\n");
    return pass == total ? 0 : 1;
}

int main(void) {
    uint32_t cfsr0 = SCB_CFSR, hfsr0 = SCB_HFSR, led0 = FPGAIO_LED0, shcsr0 = SCB_SHCSR;   /* 리셋 직후 값 */
    fault_policy_enable();
    if (!plan_valid()) {                                 /* 콜드 부팅 (또는 기록이 깨짐) */
        sh_puts("cold boot: no valid plan in .noinit @"); sh_hex((uint32_t)(uintptr_t)&g_plan); sh_puts("\n");
        g_plan.magic = PLAN_MAGIC; g_plan.boots = 0u; g_plan.next = 0u; g_plan.nres = 0u; g_plan.boot_dirty = 0u;
        g_crash.magic = 0u;
    }
    g_plan.boots++;
    sh_puts("== boot #"); sh_dec(g_plan.boots); sh_puts(" ==  CFSR="); sh_hex(cfsr0); sh_puts(" HFSR=");
    sh_hex(hfsr0); sh_puts(" LED0="); sh_hex(led0); sh_puts(" SHCSR="); sh_hex(shcsr0);
    sh_puts(" -> "); sh_hex(SCB_SHCSR); sh_puts("\n");
    if ((cfsr0 | hfsr0 | led0 | shcsr0) != 0u) g_plan.boot_dirty |= 1u << (g_plan.boots - 1u);
    if (g_plan.boots > MAX_BOOTS) { plan_seal(); sh_puts("boot-loop guard: too many resets\n"); return 2; }
    if (crash_valid(&g_crash)) {
        report_reset_cause(&g_crash);
        if (g_plan.nres < NSTEPS) g_plan.res[g_plan.nres++] = g_crash;
        g_crash.magic = 0u;                              /* 소비했으면 무효화: 다음 리셋이 같은 기록을 또 보지 않게 */
    } else if (g_plan.boots > 1u) {
        sh_puts("  reset cause: unknown (no valid crash record)\n");
    }
    plan_seal();
    if (g_plan.next < NSTEPS) {
        uint32_t k = g_plan.next++;
        plan_seal();                                     /* 주입 "전에" 진행을 기록 — 기록을 잃어도 같은 fault 를 반복하지 않게 */
        FPGAIO_LED0 = OUT_ACTIVE;
        sh_puts("  inject "); sh_dec(k); sh_puts(": "); sh_puts(k_step_name[k]);
        sh_puts(" (outputs="); sh_hex(FPGAIO_LED0); sh_puts(")\n");
        inject(k);
        sh_puts("  no fault happened! (trap not enabled?) -> record reason=none, reset\n");
        crash_commit(0u, 0, FPGAIO_LED0);
        system_reset();
    }
    return final_report();
}

__attribute__((noreturn)) void Reset_Handler(void) {
    const uint32_t *src = _sidata;
    for (uint32_t *dst = _sdata; dst < _edata;) *dst++ = *src++;
    for (uint32_t *dst = _sbss; dst < _ebss;) *dst++ = 0u;      /* .noinit 은 건드리지 않는다 */
    sh_exit(main());
}
