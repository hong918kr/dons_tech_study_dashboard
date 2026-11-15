/* ENV: qemu */
/* BM-A01 SystemInit 순서: FPU 활성화, VTOR 재배치, 클럭 전환
 *
 * 펌웨어 한 파일 = startup (BM-B08 축약판) + Reset_Handler 의 초기화 순서 + 자체 시험.
 *   Reset_Handler: (1) FPU 켜기 (CPACR) → (2) .data 복사 / .bss 0 / 생성자 → (3) 벡터 테이블 RAM 복사 + VTOR
 *   main:          Part A 확인 → Part B 결함 주입 (NOCP, 생성자 순서, VTOR 정렬) → Part C 클럭 전환 모델
 * 대상: qemu-system-arm -M mps2-an386 (Cortex-M4F), 링커 스크립트 a01.ld, 출력은 세미호스팅.
 * 빌드/실행 (Lima VM elinux 안): bash run_qemu.sh solution|starter|probe|order|disasm|practice
 */
#include <stddef.h>
#include <stdint.h>

extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[];
typedef void (*vector_t)(void);
extern vector_t __init_array_start[], __init_array_end[];

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
#define REG32(a)      (*(volatile uint32_t *)(uintptr_t)(a))
#define NVIC_ISER0    REG32(0xE000E100u)
#define NVIC_ICER0    REG32(0xE000E180u)
#define NVIC_ISPR0    REG32(0xE000E200u)
#define NVIC_ICPR0    REG32(0xE000E280u)
#define SCB_ICSR      REG32(0xE000ED04u)
#define SCB_VTOR      REG32(0xE000ED08u)
#define SCB_SHCSR     REG32(0xE000ED24u)
#define SCB_CFSR      REG32(0xE000ED28u)
#define SCB_HFSR      REG32(0xE000ED2Cu)
#define SCB_CPACR     REG32(0xE000ED88u)
#define FPU_FPCCR     REG32(0xE000EF34u)
#define CPACR_CP10_CP11_FULL (0xFu << 20)       /* CP10 [21:20] = 11, CP11 [23:22] = 11 */
#define SHCSR_USGFAULTENA    (1u << 18)
#define CFSR_NOCP            (1u << 19)
#define HFSR_FORCED          (1u << 30)
#define ICSR_PENDSTSET       (1u << 26)
#define ICSR_PENDSTCLR       (1u << 25)
#define dsb() __asm__ volatile("dsb" ::: "memory")
#define isb() __asm__ volatile("isb" ::: "memory")

/* ---------------- fault 기록: 이 레슨의 진단 정책 = NOCP 면 FPU 를 켜고 같은 명령을 다시 ----------------
 * (제품이라면 NOCP 는 버그 → 기록 + 리셋 (BM-M13). 여기선 한 부팅에서 여러 시험을 하려고 "고치고 재시도".)
 * 주의: 이 핸들러 자체는 FP 명령을 쓰면 안 된다 (CPACR=0 인 채로 들어올 수 있으므로) — disasm 으로 확인. */
typedef struct { uint32_t ipsr, cfsr, hfsr, pc, exc_return, insn; } frec_t;
#define FLOG_N 8u
static volatile frec_t g_flog[FLOG_N];
static volatile uint32_t g_nfault;

__attribute__((used)) void fault_c(uint32_t *frame, uint32_t exc_return) {
    uint32_t ipsr;
    __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
    uint32_t cfsr = SCB_CFSR, hfsr = SCB_HFSR, pc = frame[6];
    const volatile uint16_t *ip = (const volatile uint16_t *)(uintptr_t)pc;
    if (g_nfault < FLOG_N) {
        volatile frec_t *r = &g_flog[g_nfault];
        r->ipsr = ipsr & 0x1FFu; r->cfsr = cfsr; r->hfsr = hfsr; r->pc = pc;
        r->exc_return = exc_return; r->insn = ((uint32_t)ip[0] << 16) | ip[1];
    }
    g_nfault++;
    SCB_CFSR = cfsr;                                   /* W1C: 다음 fault 를 깨끗하게 */
    SCB_HFSR = hfsr;
    if ((cfsr & CFSR_NOCP) && g_nfault < 16u) {
        SCB_CPACR |= CPACR_CP10_CP11_FULL;             /* 고치고, 같은 명령으로 복귀 (PC 그대로) */
        dsb(); isb();
        return;
    }
    sh_puts("unexpected fault IPSR="); sh_dec(ipsr & 0x1FFu); sh_puts(" CFSR="); sh_hex(cfsr);
    sh_puts(" pc="); sh_hex(pc); sh_puts("\n");
    sh_exit(3);
}
__attribute__((naked)) void Fault_Entry(void) {
    __asm__ volatile(
        "tst   lr, #4      \n"
        "ite   eq          \n"
        "mrseq r0, msp     \n"
        "mrsne r0, psp     \n"
        "mov   r1, lr      \n"
        "push  {r4, lr}    \n"
        "bl    fault_c     \n"
        "pop   {r4, pc}    \n");
}
static int is_vfp_insn(uint32_t insn) {      /* T32 코프로세서 공간 + coproc 10/11 (101x) */
    return ((insn >> 16) & 0xEC00u) == 0xEC00u && ((insn >> 9) & 7u) == 5u;
}

/* ---------------- 예외 핸들러 (플래시 테이블 기본값 + 교체용) ---------------- */
static volatile uint32_t g_hit_systick_old, g_hit_systick_new, g_hit_irq6_new, g_hit_irq31, g_hit_wrong;
static volatile uint32_t g_last_excret;
void SysTick_Handler(void) { g_hit_systick_old++; }
void SysTick_New(void)     { g_hit_systick_new++; }
void Irq6_New(void)        { g_hit_irq6_new++; NVIC_ICER0 = 1u << 6; }
void Irq31_Handler(void)   { g_hit_irq31++; NVIC_ICER0 = 1u << 31; }
void Wrong_Handler(void) {                       /* "엉뚱한 칸" 에서 불리면 여기로 */
    g_hit_wrong++;
    NVIC_ICER0 = 0xFFFFFFFFu;                    /* 외부 IRQ 였다면 다시 안 오게 */
    NVIC_ICPR0 = 0xFFFFFFFFu;
}
__attribute__((used)) void excret_c(uint32_t lr) { g_last_excret = lr; g_hit_systick_new++; }
__attribute__((naked)) void SysTick_ExcRet(void) {     /* EXC_RETURN 을 기록하는 SysTick (Part B) */
    __asm__ volatile("mov r0, lr\n push {r4, lr}\n bl excret_c\n pop {r4, pc}\n");
}

/* ---------------- 벡터 테이블 (플래시) ---------------- */
void Reset_Handler(void);
void Default_Handler(void) {
    uint32_t ipsr;
    __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
    sh_puts("Default_Handler: unexpected exception "); sh_dec(ipsr & 0x1FFu); sh_puts("\n");
    sh_exit(2);
}
#define N_EXT_IRQ   32u                          /* mps2-an386: num-irq 32 (QEMU mps2.c, probe P8) */
#define N_VECTORS   (16u + N_EXT_IRQ)            /* 48 칸 = 192 B */
#define VTOR_ALIGN  256u                         /* 2 의 거듭제곱 ≥ 192 B, 최소 128 B */
_Static_assert(VTOR_ALIGN >= N_VECTORS * 4u && VTOR_ALIGN >= 128u, "VTOR alignment too small");
_Static_assert((VTOR_ALIGN & (VTOR_ALIGN - 1u)) == 0u, "VTOR alignment must be a power of two");
#define D Default_Handler
__attribute__((section(".isr_vector"), used))
const vector_t g_vectors[N_VECTORS] = {
    (vector_t)_estack, Reset_Handler, D, Fault_Entry, Fault_Entry, Fault_Entry, Fault_Entry,
    0, 0, 0, 0, D, D, 0, D, SysTick_Handler,
    D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D,                 /* IRQ 0–15 */
    D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, Irq31_Handler,     /* IRQ 16–31 */
};
vector_t g_ram_vectors[N_VECTORS] __attribute__((aligned(VTOR_ALIGN)));   /* .bss */
static uint32_t g_vtor_before;

/* ---------------- "생성자": .data 의 float 를 읽어 표를 만든다 (C++ static ctor 와 같은 자리) ---------------- */
volatile float g_ctor_scale = 0.5f;              /* .data — 0x3F000000 */
float g_gain[8];
__attribute__((constructor, noinline)) void ctor_gain_table(void) {
    for (int i = 0; i < 8; ++i) g_gain[i] = (float)i * g_ctor_scale + 0.25f;   /* VFP: vldr, vcvt, vmla … */
}

/* ================= TODO 1: FPU 켜기 =================
 * CPACR (0xE000ED88) 의 CP10 [21:20], CP11 [23:22] 를 둘 다 11 (full access) 로. 다른 비트는 보존.
 * 그 다음 DSB (쓰기 완료) + ISB (파이프라인 비우기) — 이 뒤의 첫 FP 명령부터 확실히 적용되게.
 * .data/.bss 초기화 전에 불리므로 전역 변수를 쓰면 안 된다. */
static inline void fpu_enable(void) {
    SCB_CPACR |= CPACR_CP10_CP11_FULL;
    dsb();
    isb();
}

/* ================= TODO 2: 벡터 테이블을 RAM 으로 + VTOR =================
 * g_vectors (플래시, N_VECTORS 칸) → g_ram_vectors (RAM, VTOR_ALIGN 정렬) 복사, DSB (복사가 메모리에 닿게),
 * 옛 VTOR 을 g_vtor_before 에 남기고 VTOR = &g_ram_vectors, DSB + ISB. .bss 0 채우기 "뒤에" 불려야 한다. */
static void vtor_relocate(void) {
    for (uint32_t i = 0; i < N_VECTORS; ++i) g_ram_vectors[i] = g_vectors[i];
    dsb();
    g_vtor_before = SCB_VTOR;
    SCB_VTOR = (uint32_t)(uintptr_t)g_ram_vectors;
    dsb();
    isb();
}

/* ================= TODO 3: 런타임 핸들러 교체 =================
 * 예외 번호 exc (SysTick = 15, IRQn = 16 + n) 의 RAM 테이블 칸을 fn 으로 바꾸고 이전 값을 돌려준다.
 * 거부 (0 반환, 아무것도 안 씀): VTOR 이 RAM 테이블을 가리키지 않을 때 (써 봐야 효과 없음),
 * exc < 2 (SP 초기값·Reset 칸) 또는 exc >= N_VECTORS, fn == 0. 쓴 뒤 DSB — 다음 예외가 새 값을 읽게. */
static vector_t vtor_set_handler(uint32_t exc, vector_t fn) {
    if (SCB_VTOR != (uint32_t)(uintptr_t)g_ram_vectors || exc < 2u || exc >= N_VECTORS || fn == 0)
        return 0;
    vector_t old = g_ram_vectors[exc];
    g_ram_vectors[exc] = fn;
    dsb();
    return old;
}

/* ================= Part C: STM32F4 RCC/FLASH 모델 (BM-B02 mock 축약판) =================
 * mps2 에는 RCC/PLL 이 없다 → 레지스터 접근을 REG_READ/REG_WRITE 로 모델에 보낸다 (호스트 시험과 같은 seam).
 * 주소·비트: RM0090 기준 대표값 — RM 확인 권장. 모델의 시간 = 레지스터 접근 횟수. */
#define RCC_CR       0x40023800u   /* HSION 0, HSIRDY 1, HSEON 16, HSERDY 17, PLLON 24, PLLRDY 25 */
#define RCC_PLLCFGR  0x40023804u   /* PLLM [5:0], PLLN [14:6], PLLP [17:16] (P/2-1), PLLSRC 22, PLLQ [27:24] */
#define RCC_CFGR     0x40023808u   /* SW [1:0], SWS [3:2], HPRE [7:4], PPRE1 [12:10], PPRE2 [15:13] */
#define FLASH_ACR    0x40023C00u   /* LATENCY [2:0], PRFTEN 8, ICEN 9, DCEN 10 */
#define CR_HSEON  (1u << 16)
#define CR_HSERDY (1u << 17)
#define CR_PLLON  (1u << 24)
#define CR_PLLRDY (1u << 25)
#define SW_HSI 0u
#define SW_HSE 1u
#define SW_PLL 2u
#define HSI_HZ        16000000u
#define WS_STEP_HZ    30000000u    /* 2.7–3.6 V: 대기 상태 1 개당 30 MHz (RM0090 표 — 확인 권장) */
#define APB1_MAX_HZ   42000000u
#define APB2_MAX_HZ   84000000u
#define HSE_STARTUP   20u          /* 모델: HSEON 뒤 20 접근 후 HSERDY */
#define PLL_LOCK      10u
#define SW_DELAY      3u
#define POLL_MAX      200u         /* 드라이버의 모든 대기 루프 상한 */

typedef struct {
    uint32_t cr, pllcfgr, cfgr, acr;
    uint32_t lat_eff, acr_pending;     /* LATENCY 는 쓴 다음 접근부터 유효 */
    uint32_t t, hse_on_t, pll_on_t, sw_t;
    uint32_t hse_hz, hse_broken, pll_broken;
    uint32_t garbage, apb_viol, unknown, cfg_while_on;
    uint32_t t_lat_up, t_sw_pll;       /* 첫 "LATENCY≥5 쓰기" / "SW=PLL 쓰기" 시각 (0 = 없음) */
} clk_mock_t;
static clk_mock_t g_m;

static uint32_t m_div_ahb(uint32_t hpre) {             /* HPRE 0xxx = /1, 1000.. = /2 /4 /8 /16 /64 /128 /256 /512 */
    static const uint16_t k[8] = { 2, 4, 8, 16, 64, 128, 256, 512 };
    return (hpre & 8u) ? k[hpre & 7u] : 1u;
}
static uint32_t m_div_apb(uint32_t ppre) { return (ppre & 4u) ? (2u << (ppre & 3u)) : 1u; }
static int m_pll_valid(uint32_t pllcfgr, uint32_t hse) {
    uint32_t m = pllcfgr & 63u, n = (pllcfgr >> 6) & 511u;
    if (m < 2u || n < 50u || n > 432u || !(pllcfgr & (1u << 22))) return 0;
    uint64_t vin = hse / m, vco = (uint64_t)hse * n / m;
    return vin >= 1000000u && vin <= 2000000u && vco >= 100000000u && vco <= 432000000u;
}
static uint32_t m_src_hz(uint32_t src) {
    if (src == SW_HSI) return HSI_HZ;
    if (src == SW_HSE) return g_m.hse_hz;
    uint32_t p = 2u * (((g_m.pllcfgr >> 16) & 3u) + 1u);
    return (uint32_t)((uint64_t)g_m.hse_hz * ((g_m.pllcfgr >> 6) & 511u) / (g_m.pllcfgr & 63u) / p);
}
static int m_src_ready(uint32_t src) {
    return src == SW_HSI || (src == SW_HSE && (g_m.cr & CR_HSERDY)) || (src == SW_PLL && (g_m.cr & CR_PLLRDY));
}
static uint32_t m_hclk(void) { return m_src_hz((g_m.cfgr >> 2) & 3u) / m_div_ahb((g_m.cfgr >> 4) & 15u); }
static int m_too_fast(void) { return m_hclk() > (g_m.lat_eff + 1u) * WS_STEP_HZ; }
static void m_tick(void) {
    g_m.t++;
    if (g_m.acr_pending) { g_m.lat_eff = g_m.acr & 7u; g_m.acr_pending = 0u; }
    if ((g_m.cr & CR_HSEON) && !g_m.hse_broken && g_m.t - g_m.hse_on_t >= HSE_STARTUP) g_m.cr |= CR_HSERDY;
    if (!(g_m.cr & CR_HSEON)) g_m.cr &= ~CR_HSERDY;
    if ((g_m.cr & CR_PLLON) && (g_m.cr & CR_HSERDY) && !g_m.pll_broken && m_pll_valid(g_m.pllcfgr, g_m.hse_hz) &&
        g_m.t - g_m.pll_on_t >= PLL_LOCK) g_m.cr |= CR_PLLRDY;
    if (!(g_m.cr & CR_PLLON)) g_m.cr &= ~CR_PLLRDY;
    uint32_t sw = g_m.cfgr & 3u, sws = (g_m.cfgr >> 2) & 3u;
    if (sw != sws && g_m.t - g_m.sw_t >= SW_DELAY && m_src_ready(sw)) g_m.cfgr = (g_m.cfgr & ~0xCu) | (sw << 2);
    if (m_too_fast()) g_m.garbage++;                                   /* 플래시가 못 따라옴 */
    uint32_t h = m_hclk();
    if (h / m_div_apb((g_m.cfgr >> 10) & 7u) > APB1_MAX_HZ) g_m.apb_viol++;
    if (h / m_div_apb((g_m.cfgr >> 13) & 7u) > APB2_MAX_HZ) g_m.apb_viol++;
}
static uint32_t mock_read32(uint32_t a) {
    m_tick();
    uint32_t v;
    switch (a) {
    case RCC_CR: v = g_m.cr; break;
    case RCC_PLLCFGR: v = g_m.pllcfgr; break;
    case RCC_CFGR: v = g_m.cfgr; break;
    case FLASH_ACR: v = g_m.acr; break;
    default: g_m.unknown++; v = 0u; break;
    }
    return m_too_fast() ? v ^ 0x5A5A5A5Au : v;                         /* 대기 상태 부족 = 쓰레기 읽기 */
}
static void mock_write32(uint32_t a, uint32_t v) {
    m_tick();
    switch (a) {
    case RCC_CR:
        if ((v & CR_HSEON) && !(g_m.cr & CR_HSEON)) g_m.hse_on_t = g_m.t;
        if ((v & CR_PLLON) && !(g_m.cr & CR_PLLON)) g_m.pll_on_t = g_m.t;
        g_m.cr = (g_m.cr & (CR_HSERDY | CR_PLLRDY | 2u)) | (v & (CR_HSEON | CR_PLLON | 1u));
        break;
    case RCC_PLLCFGR:
        if (g_m.cr & CR_PLLON) g_m.cfg_while_on++; else g_m.pllcfgr = v;   /* PLL 켜진 채 설정 = 무시 */
        break;
    case RCC_CFGR:
        if ((v & 3u) != (g_m.cfgr & 3u)) g_m.sw_t = g_m.t;
        if ((v & 3u) == SW_PLL && !g_m.t_sw_pll) g_m.t_sw_pll = g_m.t;
        g_m.cfgr = (g_m.cfgr & 0xCu) | (v & ~0xCu);                    /* SWS 는 읽기 전용 */
        break;
    case FLASH_ACR:
        if ((v & 7u) >= 5u && !g_m.t_lat_up) g_m.t_lat_up = g_m.t;
        g_m.acr = v; g_m.acr_pending = 1u;
        break;
    default: g_m.unknown++; break;
    }
}
static void mock_reset(uint32_t hse_hz, int hse_broken, int pll_broken) {
    uint32_t *w = (uint32_t *)&g_m;                     /* 구조체 = uint32_t 만 → 워드 루프 (memset 호출 없이) */
    for (uint32_t i = 0; i < sizeof g_m / 4u; ++i) w[i] = 0u;
    g_m.cr = 0x00000083u; g_m.pllcfgr = 0x24003010u; g_m.cfgr = 0u; g_m.acr = 0u;   /* RM0090 리셋값 (확인 권장) */
    g_m.hse_hz = hse_hz; g_m.hse_broken = (uint32_t)hse_broken; g_m.pll_broken = (uint32_t)pll_broken;
}
#define REG_READ(a)     mock_read32(a)
#define REG_WRITE(a, v) mock_write32((a), (v))

/* ---------------- 드라이버 부품 (주어짐) ---------------- */
typedef enum { CLK_OK = 0, CLK_ERR_NO_PLL, CLK_ERR_HSE, CLK_ERR_PLL, CLK_ERR_FLASH, CLK_ERR_SWITCH, CLK_ERR_TODO } clk_rc_t;
typedef struct { uint32_t m, n, p, q; } pll_cfg_t;
static uint32_t g_core_hz = HSI_HZ;               /* CMSIS 의 SystemCoreClock 자리 */

/* 정확히 target_hz 를 내는 (M,N,P) 전부 세고, 가장 좋은 것을 고른다 (BM-B10 계산기 축약판).
 * 규칙: VCO 입력 1–2 MHz, VCO 출력 100–432 MHz, M 2..63, N 50..432, P ∈ {2,4,6,8}.
 * 순위: USB 48 MHz 를 정확히 내는 Q 가 있음 > VCO 입력이 큼 (M 작음, 지터↓) > P 작음. */
static uint32_t pll_search(uint32_t hse, uint32_t target, pll_cfg_t *best) {
    uint32_t count = 0, best_score = 0;
    for (uint32_t m = 2; m <= 63u; ++m) {
        uint64_t vin_num = hse;                                         /* VCO 입력 = hse / m */
        if (vin_num < 1000000ull * m || vin_num > 2000000ull * m) continue;
        for (uint32_t p = 2; p <= 8u; p += 2u) {
            uint64_t num = (uint64_t)target * p * m;                    /* N = target·P·M / hse */
            if (num % hse) continue;
            uint64_t n = num / hse, vco = (uint64_t)target * p;
            if (n < 50u || n > 432u || vco < 100000000u || vco > 432000000u) continue;
            count++;
            uint32_t q = (uint32_t)(vco / 48000000u), q_exact = (vco % 48000000u == 0u && q >= 2u && q <= 15u);
            if (q < 2u) q = 2u;
            if (q > 15u) q = 15u;
            uint32_t score = (q_exact << 16) | ((64u - m) << 4) | (8u - p);
            if (score > best_score) { best_score = score; best->m = m; best->n = (uint32_t)n; best->p = p; best->q = q; }
        }
    }
    return count;
}
static uint32_t flash_ws_for(uint32_t hclk) { return hclk ? (hclk - 1u) / WS_STEP_HZ : 0u; }
static uint32_t ppre_bits_for(uint32_t hclk, uint32_t max_hz) {   /* 가장 작은 분주 1/2/4/8/16 */
    if (hclk <= max_hz) return 0u;
    for (uint32_t b = 4u; b <= 7u; ++b) if (hclk / (2u << (b & 3u)) <= max_hz) return b;
    return 7u;
}
static int wait_bits(uint32_t addr, uint32_t mask, uint32_t want) {
    for (uint32_t i = 0; i < POLL_MAX; ++i) if ((REG_READ(addr) & mask) == want) return 1;
    return 0;                                                           /* 상한 → 실패 (매달리지 않는다) */
}
static clk_rc_t st_hse_on(void) {
    REG_WRITE(RCC_CR, REG_READ(RCC_CR) | CR_HSEON);
    if (wait_bits(RCC_CR, CR_HSERDY, CR_HSERDY)) return CLK_OK;
    REG_WRITE(RCC_CR, REG_READ(RCC_CR) & ~CR_HSEON);                    /* 실패: 켠 것 되돌리기 */
    return CLK_ERR_HSE;
}
static clk_rc_t st_pll_config(const pll_cfg_t *c) {
    if (REG_READ(RCC_CR) & CR_PLLON) return CLK_ERR_PLL;                /* 켜진 PLL 은 설정 불가 */
    REG_WRITE(RCC_PLLCFGR, c->m | (c->n << 6) | (((c->p >> 1) - 1u) << 16) | (1u << 22) | (c->q << 24));
    return CLK_OK;
}
static clk_rc_t st_pll_on(void) {
    REG_WRITE(RCC_CR, REG_READ(RCC_CR) | CR_PLLON);
    if (wait_bits(RCC_CR, CR_PLLRDY, CR_PLLRDY)) return CLK_OK;
    REG_WRITE(RCC_CR, REG_READ(RCC_CR) & ~CR_PLLON);
    return CLK_ERR_PLL;
}
static void st_bus_prescalers(uint32_t hclk) {                          /* AHB /1, APB1 ≤ 42, APB2 ≤ 84 */
    uint32_t v = REG_READ(RCC_CFGR) & ~((15u << 4) | (7u << 10) | (7u << 13));
    REG_WRITE(RCC_CFGR, v | (ppre_bits_for(hclk, APB1_MAX_HZ) << 10) | (ppre_bits_for(hclk, APB2_MAX_HZ) << 13));
}
static clk_rc_t st_flash_latency(uint32_t ws) {
    REG_WRITE(FLASH_ACR, ws | (7u << 8));                               /* LATENCY + PRFTEN/ICEN/DCEN */
    return wait_bits(FLASH_ACR, 7u, ws) ? CLK_OK : CLK_ERR_FLASH;       /* RM: 다시 읽어 적용 확인 */
}
static clk_rc_t st_switch(uint32_t sw) {
    REG_WRITE(RCC_CFGR, (REG_READ(RCC_CFGR) & ~3u) | sw);
    return wait_bits(RCC_CFGR, 0xCu, sw << 2) ? CLK_OK : CLK_ERR_SWITCH;
}

/* ================= TODO 4: HSI 16 MHz → HSE + PLL target_hz =================
 * 부품만으로: PLL 값 찾기 (없으면 CLK_ERR_NO_PLL, 레지스터 손대지 않음) → HSE 켜기 → PLL 설정 → PLL 켜기 →
 * 버스 분주 (목표 주파수 기준) → 플래시 LATENCY 올리기 → SW=PLL. 오류면 그 rc 를 즉시 반환.
 * 성공하면 g_core_hz = target_hz, *out = 고른 PLL 값. 핵심: "빨라지기 전에" 대기 상태와 분주를 먼저. */
static clk_rc_t clock_up(uint32_t hse_hz, uint32_t target_hz, pll_cfg_t *out) {
    pll_cfg_t c = { 0, 0, 0, 0 };
    clk_rc_t rc;
    if (pll_search(hse_hz, target_hz, &c) == 0u) return CLK_ERR_NO_PLL;
    if ((rc = st_hse_on()) != CLK_OK) return rc;
    if ((rc = st_pll_config(&c)) != CLK_OK) return rc;
    if ((rc = st_pll_on()) != CLK_OK) return rc;
    st_bus_prescalers(target_hz);
    if ((rc = st_flash_latency(flash_ws_for(target_hz))) != CLK_OK) return rc;
    if ((rc = st_switch(SW_PLL)) != CLK_OK) return rc;
    g_core_hz = target_hz;
    *out = c;
    return CLK_OK;
}

/* ================= TODO 5: PLL → HSI 16 MHz 로 내려오기 =================
 * 순서가 거꾸로: SW=HSI (SWS 확인) → 버스 분주 (HSI 기준) → LATENCY 내리기 → PLL 끄기 → HSE 끄기.
 * 성공하면 g_core_hz = HSI_HZ. */
static clk_rc_t clock_down(void) {
    clk_rc_t rc;
    if ((rc = st_switch(SW_HSI)) != CLK_OK) return rc;
    st_bus_prescalers(HSI_HZ);
    if ((rc = st_flash_latency(flash_ws_for(HSI_HZ))) != CLK_OK) return rc;
    REG_WRITE(RCC_CR, REG_READ(RCC_CR) & ~CR_PLLON);
    REG_WRITE(RCC_CR, REG_READ(RCC_CR) & ~CR_HSEON);
    g_core_hz = HSI_HZ;
    return CLK_OK;
}

/* ---------------- 결함 주입판 (순서만 틀린 시퀀스) ---------------- */
static void bug_up_latency_after_switch(uint32_t hse, uint32_t target) {
    pll_cfg_t c = { 0, 0, 0, 0 };
    (void)pll_search(hse, target, &c);
    (void)st_hse_on(); (void)st_pll_config(&c); (void)st_pll_on(); st_bus_prescalers(target);
    (void)st_switch(SW_PLL);                                            /* 먼저 빨라지고 … */
    (void)st_flash_latency(flash_ws_for(target));                       /* … 나중에 대기 상태 */
}
static void bug_up_prescaler_after_switch(uint32_t hse, uint32_t target) {
    pll_cfg_t c = { 0, 0, 0, 0 };
    (void)pll_search(hse, target, &c);
    (void)st_hse_on(); (void)st_pll_config(&c); (void)st_pll_on();
    (void)st_flash_latency(flash_ws_for(target));
    (void)st_switch(SW_PLL);
    st_bus_prescalers(target);                                          /* APB1 이 잠깐 168 MHz */
}
static void bug_down_latency_first(void) {
    (void)st_flash_latency(0u);                                         /* 아직 168 MHz 인데 0 WS */
    (void)st_switch(SW_HSI);
    st_bus_prescalers(HSI_HZ);
}

/* ---------------- 시험 하네스 ---------------- */
static uint32_t g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } else { \
    sh_puts("FAIL " __FILE__ ":"); sh_dec(__LINE__); sh_puts("  " #expr "\n"); } } while (0)

extern const char pc_b1[], pc_b2[];
__attribute__((noinline)) static uint32_t vfp_add_self(uint32_t bits, int which) {   /* 1.5f+1.5f 등 */
    uint32_t r;
    if (which == 1)
        __asm__ volatile(".global pc_b1\npc_b1: vmov s0, %1\n vadd.f32 s0, s0, s0\n vmov %0, s0" : "=r"(r) : "r"(bits) : "s0", "memory");
    else
        __asm__ volatile(".global pc_b2\npc_b2: vmov s0, %1\n vadd.f32 s0, s0, s0\n vmov %0, s0" : "=r"(r) : "r"(bits) : "s0", "memory");
    return r;
}
static void pend_systick(void) { SCB_ICSR = ICSR_PENDSTSET; dsb(); isb(); }
static void pend_irq(uint32_t n) { NVIC_ISER0 = 1u << n; NVIC_ISPR0 = 1u << n; dsb(); isb(); }
static void fpu_off(void) { SCB_CPACR &= ~CPACR_CP10_CP11_FULL; dsb(); isb(); }
static int gain_ok(void) {
    for (int i = 0; i < 8; ++i) if (g_gain[i] != (float)i * 0.5f + 0.25f) return 0;
    return 1;
}
static const char *const k_rc[] = { "OK", "NO_PLL", "HSE", "PLL", "FLASH", "SWITCH", "TODO" };

static void part_a(uint32_t boot_faults) {
    sh_puts("== Part A: Reset_Handler order ==\n");
    sh_puts("[boot] (1) FPU    CPACR="); sh_hex(SCB_CPACR); sh_puts(" FPCCR="); sh_hex(FPU_FPCCR);
    sh_puts(" faults during boot="); sh_dec(boot_faults); sh_puts("\n");
    sh_puts("[boot] (2) .data "); sh_dec((uint32_t)((uintptr_t)_edata - (uintptr_t)_sdata)); sh_puts(" B, .bss ");
    sh_dec((uint32_t)((uintptr_t)_ebss - (uintptr_t)_sbss)); sh_puts(" B, ctors ");
    sh_dec((uint32_t)(__init_array_end - __init_array_start)); sh_puts(", g_gain[7]*4=");
    sh_dec((uint32_t)(g_gain[7] * 4.0f)); sh_puts("\n");
    sh_puts("[boot] (3) VTOR   "); sh_hex(g_vtor_before); sh_puts(" -> "); sh_hex(SCB_VTOR);
    sh_puts(" (&g_ram_vectors="); sh_hex((uint32_t)(uintptr_t)g_ram_vectors); sh_puts(", align ");
    sh_dec(VTOR_ALIGN); sh_puts(", "); sh_dec(N_VECTORS); sh_puts(" vectors)\n");
    for (uint32_t i = 0; i < boot_faults && i < FLOG_N; ++i) {
        sh_puts("  boot fault: IPSR="); sh_dec(g_flog[i].ipsr); sh_puts(" CFSR="); sh_hex(g_flog[i].cfsr);
        sh_puts(" HFSR="); sh_hex(g_flog[i].hfsr); sh_puts(" pc="); sh_hex(g_flog[i].pc);
        sh_puts(" insn="); sh_hex(g_flog[i].insn); sh_puts(" EXC_RETURN="); sh_hex(g_flog[i].exc_return); sh_puts("\n");
    }
    CHECK(boot_faults == 0u);                                           /* 생성자의 FP 가 fault 없이 */
    CHECK((SCB_CPACR & CPACR_CP10_CP11_FULL) == CPACR_CP10_CP11_FULL);
    CHECK(gain_ok());
    volatile float a = 1.5f, b = 2.25f, c = 3.0f;                       /* 진짜 VFP: vmul/vadd/vdiv */
    float prod = a * b + 0.5f, q = 1.0f / c;
    union { float f; uint32_t u; } uq = { q };
    sh_puts("  1.5*2.25+0.5 = "); sh_dec((uint32_t)(prod * 1000.0f)); sh_puts("/1000, 1/3 bits = "); sh_hex(uq.u); sh_puts("\n");
    CHECK(prod == 3.875f);
    CHECK(uq.u == 0x3EAAAAABu);
    CHECK(SCB_VTOR == (uint32_t)(uintptr_t)g_ram_vectors);
    CHECK(((uintptr_t)g_ram_vectors & (VTOR_ALIGN - 1u)) == 0u);
    int same = 1;
    for (uint32_t i = 0; i < N_VECTORS; ++i) if (g_ram_vectors[i] != g_vectors[i]) same = 0;
    CHECK(same);
    CHECK(g_vtor_before == 0u);
    /* 런타임 핸들러 교체: SysTick */
    vector_t old = vtor_set_handler(15u, SysTick_New);
    CHECK(old == SysTick_Handler);
    pend_systick();
    sh_puts("  swap SysTick: old hits="); sh_dec(g_hit_systick_old); sh_puts(" new hits="); sh_dec(g_hit_systick_new); sh_puts("\n");
    CHECK(g_hit_systick_new == 1u && g_hit_systick_old == 0u);
    CHECK(vtor_set_handler(15u, SysTick_Handler) == SysTick_New);     /* 되돌리기 */
    pend_systick();
    CHECK(g_hit_systick_old == 1u && g_hit_systick_new == 1u);
    /* 런타임 핸들러 교체: 빈 IRQ 6 (mps2 에 장치 없음) */
    CHECK(vtor_set_handler(16u + 6u, Irq6_New) == Default_Handler);
    pend_irq(6u);
    sh_puts("  swap IRQ6: Irq6_New hits="); sh_dec(g_hit_irq6_new); sh_puts("\n");
    CHECK(g_hit_irq6_new == 1u);
    CHECK(vtor_set_handler(0u, Irq6_New) == 0 && vtor_set_handler(1u, Irq6_New) == 0);   /* SP/Reset 칸 거부 */
    CHECK(vtor_set_handler(N_VECTORS, Irq6_New) == 0 && vtor_set_handler(15u, 0) == 0);
}

static vector_t g_vt_scratch[128] __attribute__((aligned(VTOR_ALIGN)));   /* 512 B 실험장 */
static void part_b(void) {
    sh_puts("== Part B: wrong order / missing FPU / misaligned VTOR ==\n");
    /* B1: UsageFault 켜고 CPACR=0 → UsageFault NOCP */
    uint32_t n0 = g_nfault;
    SCB_SHCSR |= SHCSR_USGFAULTENA;
    fpu_off();
    uint32_t r = vfp_add_self(0x3FC00000u, 1);
    volatile frec_t *f = &g_flog[n0 < FLOG_N ? n0 : 0];
    sh_puts("  B1 USGFAULTENA=1: IPSR="); sh_dec(f->ipsr); sh_puts(" CFSR="); sh_hex(f->cfsr); sh_puts(" HFSR="); sh_hex(f->hfsr);
    sh_puts(" pc="); sh_hex(f->pc); sh_puts(" EXC_RETURN="); sh_hex(f->exc_return); sh_puts(" result="); sh_hex(r); sh_puts("\n");
    CHECK(g_nfault == n0 + 1u && f->ipsr == 6u && f->cfsr == CFSR_NOCP && f->hfsr == 0u);
    CHECK(f->pc == (uint32_t)(uintptr_t)pc_b1 && is_vfp_insn(f->insn));
    CHECK(f->exc_return == 0xFFFFFFE9u);                                /* FP 를 쓴 뒤 → 확장 프레임 */
    CHECK(r == 0x40400000u);                                            /* 고친 뒤 재실행 = 3.0f */
    /* B2: UsageFault 끈 채 (리셋 기본값) → HardFault FORCED, CFSR 에는 원래 원인 NOCP */
    n0 = g_nfault;
    SCB_SHCSR &= ~SHCSR_USGFAULTENA;
    fpu_off();
    r = vfp_add_self(0x3FC00000u, 2);
    f = &g_flog[n0 < FLOG_N ? n0 : 0];
    sh_puts("  B2 USGFAULTENA=0: IPSR="); sh_dec(f->ipsr); sh_puts(" CFSR="); sh_hex(f->cfsr); sh_puts(" HFSR="); sh_hex(f->hfsr);
    sh_puts(" pc="); sh_hex(f->pc); sh_puts("\n");
    CHECK(g_nfault == n0 + 1u && f->ipsr == 3u && f->cfsr == CFSR_NOCP && f->hfsr == HFSR_FORCED);
    CHECK(f->pc == (uint32_t)(uintptr_t)pc_b2 && r == 0x40400000u);
    /* B3: 순서 결함 재현 — FPU 가 꺼진 채 생성자 (.data 의 float 를 읽는 코드) */
    n0 = g_nfault;
    for (int i = 0; i < 8; ++i) g_gain[i] = 0.0f;
    fpu_off();
    ctor_gain_table();
    f = &g_flog[n0 < FLOG_N ? n0 : 0];
    uint32_t off = f->pc - ((uint32_t)(uintptr_t)ctor_gain_table & ~1u);
    sh_puts("  B3 ctor with FPU off: faults="); sh_dec(g_nfault - n0); sh_puts(" pc=ctor_gain_table+"); sh_dec(off);
    sh_puts(" insn="); sh_hex(f->insn); sh_puts(" table ok after fix-up="); sh_dec((uint32_t)gain_ok()); sh_puts("\n");
    CHECK(g_nfault == n0 + 1u && f->ipsr == 3u && (f->cfsr & CFSR_NOCP));
    CHECK(off < 64u && is_vfp_insn(f->insn));
    CHECK(gain_ok());
    /* B4: VTOR 정렬 위반 — 0x40 어긋난 주소를 쓰면 하위 7비트가 버려진다 → 엉뚱한 칸 */
    vector_t saved_systick = g_ram_vectors[15];
    for (uint32_t i = 0; i < 128u; ++i) g_vt_scratch[i] = Wrong_Handler;
    for (uint32_t i = 0; i < N_VECTORS; ++i) g_vt_scratch[16u + i] = g_ram_vectors[i];
    dsb();
    uint32_t want = (uint32_t)(uintptr_t)&g_vt_scratch[16];
    SCB_VTOR = want; dsb(); isb();
    uint32_t got = SCB_VTOR;
    uint32_t w0 = g_hit_wrong, s0 = g_hit_systick_old;
    pend_systick();
    sh_puts("  B4 VTOR write "); sh_hex(want); sh_puts(" -> reads "); sh_hex(got); sh_puts(", SysTick -> ");
    sh_puts(g_hit_wrong == w0 + 1u ? "Wrong_Handler (slot 15 of the masked base)\n" : "?\n");
    CHECK(got == (want & ~0x7Fu) && got != want);
    CHECK(g_hit_wrong == w0 + 1u && g_hit_systick_old == s0);
    /* B5: 128 B 정렬이지만 256 B (필요한 정렬) 는 아님 — QEMU 는 VTOR + 4·exc 덧셈이라 "동작해 보인다" */
    for (uint32_t i = 0; i < 128u; ++i) g_vt_scratch[i] = Wrong_Handler;
    for (uint32_t i = 0; i < N_VECTORS; ++i) g_vt_scratch[32u + i] = g_ram_vectors[i];
    dsb();
    want = (uint32_t)(uintptr_t)&g_vt_scratch[32];
    SCB_VTOR = want; dsb(); isb();
    got = SCB_VTOR;
    w0 = g_hit_wrong;
    pend_irq(31u);                                                      /* 예외 47: +0xBC */
    sh_puts("  B5 VTOR "); sh_hex(got); sh_puts(" (128-aligned, needs 256): IRQ31 -> ");
    uint32_t slot = (((want & ~(VTOR_ALIGN - 1u)) | (47u * 4u)) - (uint32_t)(uintptr_t)g_vt_scratch) / 4u;
    sh_puts(g_hit_irq31 == 1u ? "Irq31_Handler (QEMU adds)" : "?");
    sh_puts("; OR-concatenating HW would fetch slot "); sh_dec(slot); sh_puts(" = table entry "); sh_dec(slot - 32u);
    sh_puts(" (SysTick)\n");
    CHECK(got == want && g_hit_irq31 == 1u && g_hit_wrong == w0);       /* QEMU 관찰 (실칩 규칙 위반) */
    SCB_VTOR = (uint32_t)(uintptr_t)g_ram_vectors; dsb(); isb();
    g_ram_vectors[15] = SysTick_ExcRet; dsb();
    pend_systick();
    sh_puts("  restored VTOR, SysTick EXC_RETURN="); sh_hex(g_last_excret); sh_puts("\n");
    CHECK(g_last_excret == 0xFFFFFFE9u);
    g_ram_vectors[15] = saved_systick; dsb();
}

static void show_clk(const char *tag, clk_rc_t rc) {
    sh_puts(tag); sh_puts(" rc="); sh_puts(k_rc[rc]); sh_puts(" SWS="); sh_dec((g_m.cfgr >> 2) & 3u);
    sh_puts(" HCLK="); sh_dec(m_hclk() / 1000000u); sh_puts("MHz LAT="); sh_dec(g_m.lat_eff);
    sh_puts(" garbage="); sh_dec(g_m.garbage); sh_puts(" apb_viol="); sh_dec(g_m.apb_viol);
    sh_puts(" t="); sh_dec(g_m.t); sh_puts("\n");
}
static void part_c(void) {
    sh_puts("== Part C: STM32F4 clock switch (RCC/FLASH model) ==\n");
    pll_cfg_t c = { 0, 0, 0, 0 };
    uint32_t n = pll_search(8000000u, 168000000u, &c);
    sh_puts("  pll_search(8 MHz -> 168 MHz): "); sh_dec(n); sh_puts(" solutions, best M="); sh_dec(c.m);
    sh_puts(" N="); sh_dec(c.n); sh_puts(" P="); sh_dec(c.p); sh_puts(" Q="); sh_dec(c.q); sh_puts("\n");
    CHECK(n == 5u && c.m == 4u && c.n == 168u && c.p == 2u && c.q == 7u);
    pll_cfg_t c25 = { 0, 0, 0, 0 };
    uint32_t n25 = pll_search(25000000u, 168000000u, &c25);
    sh_puts("  pll_search(25 MHz -> 168 MHz): "); sh_dec(n25); sh_puts(" solutions, best M="); sh_dec(c25.m);
    sh_puts(" N="); sh_dec(c25.n); sh_puts(" P="); sh_dec(c25.p); sh_puts(" Q="); sh_dec(c25.q); sh_puts("\n");
    CHECK(n25 >= 1u && (uint64_t)25000000u * c25.n / c25.m / c25.p == 168000000u);
    CHECK(flash_ws_for(168000000u) == 5u && flash_ws_for(30000000u) == 0u && flash_ws_for(30000001u) == 1u);

    /* C1: 올바른 순서 */
    mock_reset(8000000u, 0, 0);
    pll_cfg_t got = { 0, 0, 0, 0 };
    g_core_hz = HSI_HZ;
    clk_rc_t rc = clock_up(8000000u, 168000000u, &got);
    show_clk("  C1 clock_up correct order      ", rc);
    CHECK(rc == CLK_OK && ((g_m.cfgr >> 2) & 3u) == SW_PLL && m_hclk() == 168000000u && g_core_hz == 168000000u);
    CHECK(g_m.lat_eff == 5u && ((g_m.cfgr >> 10) & 7u) == 5u && ((g_m.cfgr >> 13) & 7u) == 4u);
    CHECK(g_m.garbage == 0u && g_m.apb_viol == 0u && g_m.unknown == 0u && g_m.cfg_while_on == 0u);
    CHECK(g_m.t_lat_up != 0u && g_m.t_sw_pll != 0u && g_m.t_lat_up < g_m.t_sw_pll);
    CHECK(got.m == 4u && got.n == 168u && got.p == 2u && got.q == 7u);
    uint32_t up_t = g_m.t;
    rc = clock_down();
    show_clk("  C2 clock_down correct order    ", rc);
    CHECK(rc == CLK_OK && ((g_m.cfgr >> 2) & 3u) == SW_HSI && g_m.lat_eff == 0u && g_core_hz == HSI_HZ);
    CHECK(g_m.garbage == 0u && g_m.apb_viol == 0u && !(g_m.cr & (CR_PLLON | CR_HSEON)));
    /* C3–C5: 결함 주입 — 최종 상태는 같아 보여도 "창" 동안 모델이 잡는다 */
    mock_reset(8000000u, 0, 0);
    bug_up_latency_after_switch(8000000u, 168000000u);
    show_clk("  C3 inject: LATENCY after SW    ", CLK_OK);
    CHECK(g_m.garbage > 0u && g_m.t_sw_pll < g_m.t_lat_up);
    CHECK(((g_m.cfgr >> 2) & 3u) == SW_PLL && g_m.lat_eff == 5u);          /* 끝 상태만 보면 정상 */
    mock_reset(8000000u, 0, 0);
    bug_up_prescaler_after_switch(8000000u, 168000000u);
    show_clk("  C4 inject: APB prescaler after ", CLK_OK);
    CHECK(g_m.apb_viol > 0u && g_m.garbage == 0u);
    mock_reset(8000000u, 0, 0);
    (void)clock_up(8000000u, 168000000u, &got);
    g_m.garbage = 0u;
    bug_down_latency_first();
    show_clk("  C5 inject: LATENCY down first  ", CLK_OK);
    CHECK(g_m.garbage > 0u);
    /* C6: HSE 가 안 뜬다 → 상한 있는 대기, HSI 에 머무름, 아무것도 올리지 않음 */
    mock_reset(8000000u, 1, 0);
    g_core_hz = HSI_HZ;
    rc = clock_up(8000000u, 168000000u, &got);
    show_clk("  C6 HSE broken                  ", rc);
    CHECK(rc == CLK_ERR_HSE && ((g_m.cfgr >> 2) & 3u) == SW_HSI && !(g_m.cr & CR_HSEON));
    CHECK(g_m.lat_eff == 0u && g_core_hz == HSI_HZ && g_m.t <= POLL_MAX + 10u && g_m.garbage == 0u);
    /* C7: PLL 이 잠기지 않는다 */
    mock_reset(8000000u, 0, 1);
    rc = clock_up(8000000u, 168000000u, &got);
    show_clk("  C7 PLL never locks             ", rc);
    CHECK(rc == CLK_ERR_PLL && ((g_m.cfgr >> 2) & 3u) == SW_HSI && g_m.garbage == 0u && g_core_hz == HSI_HZ);
    /* C8: 불가능한 목표 → 레지스터를 건드리지 않는다 */
    mock_reset(8000000u, 0, 0);
    rc = clock_up(8000000u, 167000001u, &got);
    CHECK(rc == CLK_ERR_NO_PLL && g_m.t == 0u);
    /* C9: HSE 4..26 MHz × 목표 6 개 — 올라갔다 내려오기, 위반 0 */
    static const uint32_t tg[6] = { 48000000u, 84000000u, 100000000u, 120000000u, 144000000u, 168000000u };
    uint32_t runs = 0, ok = 0, nopll = 0, bad = 0;
    for (uint32_t hse = 4000000u; hse <= 26000000u; hse += 1000000u)
        for (uint32_t k = 0; k < 6u; ++k) {
            mock_reset(hse, 0, 0);
            runs++;
            clk_rc_t r1 = clock_up(hse, tg[k], &got);
            if (r1 == CLK_ERR_NO_PLL) { nopll++; if (g_m.t != 0u) bad++; continue; }
            int good = r1 == CLK_OK && m_hclk() == tg[k] && g_m.lat_eff == flash_ws_for(tg[k]);
            good = good && clock_down() == CLK_OK && m_hclk() == HSI_HZ && g_m.lat_eff == 0u;
            good = good && g_m.garbage == 0u && g_m.apb_viol == 0u && g_m.unknown == 0u;
            if (good) ok++; else bad++;
        }
    sh_puts("  C9 sweep HSE 4..26 MHz x 6 targets: runs="); sh_dec(runs); sh_puts(" ok="); sh_dec(ok);
    sh_puts(" no_pll="); sh_dec(nopll); sh_puts(" bad="); sh_dec(bad); sh_puts("\n");
    CHECK(bad == 0u && ok + nopll == runs && ok > 0u);
    sh_puts("[boot] (4) clock  HSI 16 MHz -> PLL 168 MHz (M=4 N=168 P=2 Q=7), LATENCY 0->5 before SW, ");
    sh_puts("APB1 /4 = 42 MHz, APB2 /2 = 84 MHz, "); sh_dec(up_t); sh_puts(" model accesses\n");
}

__attribute__((noinline)) int main(void) {
    uint32_t boot_faults = g_nfault;
    part_a(boot_faults);
    part_b();
    part_c();
    sh_dec(g_pass); sh_puts("/"); sh_dec(g_total); sh_puts(" passed\n");
    return g_pass == g_total ? 0 : 1;
}

/* ---------------- startup: 이 순서가 이 레슨의 주인공 (BM-B08 startup 축약판) ---------------- */
__attribute__((noreturn)) void Reset_Handler(void) {
#ifndef INJECT_FPU_LATE
    fpu_enable();                                              /* (1) 어떤 C 코드보다 먼저 */
#endif
#ifdef INJECT_VTOR_EARLY
    vtor_relocate();                                           /* 결함: .bss 0 채우기 전에 → 테이블이 지워진다 */
#endif
    const uint32_t *src = _sidata;
    for (uint32_t *dst = _sdata; dst < _edata;) *dst++ = *src++;   /* (2) .data 복사 */
    for (uint32_t *dst = _sbss; dst < _ebss;) *dst++ = 0u;         /*     .bss 0 */
    for (vector_t *p = __init_array_start; p < __init_array_end; ++p) (*p)();   /* 생성자 */
#ifdef INJECT_FPU_LATE
    fpu_enable();                                              /* 결함: 생성자가 FP 를 쓴 "뒤에" */
#endif
#ifndef INJECT_VTOR_EARLY
    vtor_relocate();                                           /* (3) RAM 벡터 테이블 + VTOR */
#endif
    sh_exit(main());                                           /* (4) 클럭은 main 의 Part C (모델) */
}
