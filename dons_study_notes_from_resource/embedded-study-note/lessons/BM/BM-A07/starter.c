/* ENV: qemu */
/* BM-A07 스택 오버플로 탐지: 스택 페인팅과 high-water mark
 *
 * 펌웨어 한 부팅 = startup (BM-B08 축약판) + 스택 페인팅/수위(HWM)/카나리 + 시험 하네스.
 *   Part A: 프레임을 아는 함수(frames.c)를 깊이별로 불러 "칠한 뒤 잰 사용량" == ".su 정적값의 합" 을 확인,
 *           IRQ 중첩(32 B 예외 프레임 + 핸들러), SP 4 B 어긋남 → 정렬 패딩, 덜 쓴 프레임 → 과소 측정.
 *   Part B: 재귀로 스택 한계를 넘긴다 — 가드(카나리) 안에서 멈춤 / 가드를 지나 .bss 이웃까지 / 큰 프레임이
 *           가드를 "건너뛰기" / -fstack-protector 함수 카나리.
 *   Part C: 출시 체크리스트 (reserved / measured / margin).
 * 대상: qemu-system-arm -M mps2-an385 (Cortex-M3), a07.ld, frames.c 와 함께 링크, su_gen.h 는 run_qemu.sh 가 생성.
 * 빌드/실행 (Lima VM elinux 안): bash run_qemu.sh solution|starter|practice|su|paintbug
 */
#include <stddef.h>
#include <stdint.h>
#include "su_gen.h"   /* frames.su → #define SU_path_leaf 64 … (run_qemu.sh 가 만든다) */

extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[];
extern uint32_t _sneighbor[], _eneighbor[], _sguard[], _eguard[], _sstack[], _estack[];

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
#define REG8(a)         (*(volatile uint8_t *)(uintptr_t)(a))
#define SYST_CSR        REG32(0xE000E010u)   /* ENABLE 0, TICKINT 1, CLKSOURCE 2 */
#define SYST_RVR        REG32(0xE000E014u)
#define SYST_CVR        REG32(0xE000E018u)
#define NVIC_ISER0      REG32(0xE000E100u)
#define NVIC_IPR(n)     REG8(0xE000E400u + (n))
#define SCB_CCR         REG32(0xE000ED14u)   /* STKALIGN bit9 */
#define SCB_SHPR3_SYST  REG8(0xE000ED23u)    /* SysTick 우선순위 바이트 */
#define SCB_CFSR        REG32(0xE000ED28u)
#define SCB_HFSR        REG32(0xE000ED2Cu)
#define IRQ_LOW         14u
#define IRQ_HIGH        15u

/* ---------------- frames.c (프레임 크기를 아는 함수들) ---------------- */
typedef uint32_t (*fn_t)(uint32_t);
uint32_t path_leaf(uint32_t x);
uint32_t path_mid(uint32_t x);
uint32_t path_top(uint32_t x);
uint32_t recurse_until(uint32_t stop_sp, fn_t at_bottom);
uint32_t big_sparse(uint32_t call_leaf);
uint32_t ssp_copy(const uint8_t *src, uint32_t n);
void Irq14_Handler(void);
void Irq15_Handler(void);
extern volatile uint32_t g_frames_mode, g_xpsr_low, g_xpsr_high, g_isr_low_runs, g_isr_high_runs, g_min_sp;
#define MODE_PEND_LOW   1u
#define MODE_NEST_HIGH  2u
#define MODE_MISALIGN   4u

#define STACK_PAINT   0xDEADBEEFu   /* 칠하는 값: 주소·작은 정수·Thumb 코드 주소로 우연히 나오기 어려운 값 */
#define STACK_CANARY  0xC0FFEE00u   /* 가드 값: 페인트와 다르게 → "덮였다" 와 "아직 칠해져 있다" 를 구분 */
#define PAINT_MARGIN  64u           /* 칠하는 코드 자신의 프레임 + 예외 프레임 32 B 를 덮을 여유 */
#define EXC_FRAME     32u           /* FPU 없는 Cortex-M3: r0-r3, r12, lr, pc, xpsr */

static inline uint32_t read_sp(void) {
    uint32_t sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    return sp;
}
static inline uint32_t addr(const volatile void *p) { return (uint32_t)(uintptr_t)p; }

/* ================= TODO 1: 스택 칠하기 =================
 * [lo, hi) 의 모든 워드에 STACK_PAINT 를 쓴다 (hi 는 포함하지 않음, lo >= hi 이면 아무것도 안 함).
 * memset 을 부르면 안 된다 (-nostdlib, 그리고 -fno-tree-loop-distribute-patterns 로 루프가 memset 이 되는 것도 막음). */
__attribute__((noinline)) void stack_paint(volatile uint32_t *lo, volatile uint32_t *hi) {
    (void)lo; (void)hi;                          /* TODO */
}

/* ================= TODO 2: 최고 수위 (high-water mark) =================
 * 스택 한계(_sstack, 가장 낮은 주소)에서 위로 훑어 STACK_PAINT 가 아닌 첫 워드의 주소를 돌려준다.
 * 전부 칠해져 있으면 _estack. 반환값이 _sstack 이면 "맨 아래 워드까지 쓰였다" = 한계에 닿았거나 넘쳤다. */
__attribute__((noinline)) volatile uint32_t *stack_lowest_used(void) {
    return _sstack;                              /* TODO: 지금은 "전부 쓰였다" 고 답한다 */
}

/* ================= TODO 3: 가드 카나리 =================
 * 가드 구역 [_sguard, _eguard) (스택 한계 바로 아래 32 B) 의 모든 워드에 STACK_CANARY 를 쓰고(arm),
 * 모든 워드가 그대로인지 검사한다(ok: 1 = 온전, 0 = 하나라도 덮임). SysTick 핸들러와 main 루프가 부른다. */
void canary_arm(void) {
    /* TODO */
}
int canary_ok(void) {
    return 1;                                    /* TODO: 지금은 항상 "온전하다" */
}

static uint32_t stack_hwm_bytes(void) { return addr(_estack) - addr(stack_lowest_used()); }

/* ---------------- .bss 의 이웃: 스택 가드 바로 아래에 놓인 "모터 전류 제한표" ---------------- */
#define NB_WORDS 64u
__attribute__((section(".neighbor"))) volatile uint32_t g_neighbor[NB_WORDS];
static void neighbor_fill(void) { for (uint32_t i = 0; i < NB_WORDS; ++i) g_neighbor[i] = 1000u + i; }
static uint32_t neighbor_damage(uint32_t *first, uint32_t *last) {
    uint32_t n = 0;
    *first = NB_WORDS; *last = 0;
    for (uint32_t i = 0; i < NB_WORDS; ++i)
        if (g_neighbor[i] != 1000u + i) { if (!n) *first = i; *last = i; ++n; }
    return n;
}

/* ---------------- SysTick: 주기적으로 카나리 검사 ---------------- */
volatile uint32_t g_ticks, g_trip_tick;
void SysTick_Handler(void) {
    g_ticks++;
    if (g_trip_tick == 0u && !canary_ok()) g_trip_tick = g_ticks;   /* 처음 발견한 틱 */
}
static int wait_ticks(uint32_t n) {             /* 유한 루프: 스텁이어도 멈추지 않는다 */
    uint32_t t0 = g_ticks;
    for (uint32_t i = 0; i < 20000000u; ++i) if (g_ticks - t0 >= n) return 1;
    return 0;
}

/* ---------------- 측정: SP 아래를 다시 칠하고 → fn 호출 → 가장 낮게 쓰인 워드 ---------------- */
volatile uint32_t g_sink;
static uint32_t g_hwm_max;                       /* 정상 시나리오들의 절대 최고 수위 (Part C) */
__attribute__((noinline)) static uint32_t measure_call(fn_t fn, uint32_t arg) {
    uint32_t sp = read_sp();
    stack_paint(_sstack, (volatile uint32_t *)(sp - PAINT_MARGIN));   /* 내 프레임 + 도우미 프레임은 안 칠함 */
    g_sink = fn(arg);
    uint32_t low = addr(stack_lowest_used());
    if (addr(_estack) - low > g_hwm_max) g_hwm_max = addr(_estack) - low;
    return sp > low ? sp - low : 0u;
}

/* ---------------- 시험 하네스 ---------------- */
static uint32_t g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } else { \
    sh_puts("FAIL " __FILE__ ":"); sh_dec(__LINE__); sh_puts("  " #expr "\n"); } } while (0)
static void kv(const char *k, uint32_t v) { sh_puts(k); sh_dec(v); }

static int final_report(void) {
    sh_dec(g_pass); sh_puts("/"); sh_dec(g_total); sh_puts(" passed\n");
    return g_pass == g_total ? 0 : 1;
}

/* ---------------- -fstack-protector 지원 (freestanding: 직접 정의) ---------------- */
uint32_t __stack_chk_guard = 0xF00DA500u;       /* 맨 아래 바이트 0x00 = "terminator" 카나리. 실제론 부팅마다 난수 */
static uint32_t g_ssp_stage;
__attribute__((noreturn)) void __stack_chk_fail(void) {
    sh_puts("  __stack_chk_fail: ssp_copy frame canary overwritten (n=24) -> stop\n");
    CHECK(g_ssp_stage == 24u);                    /* n=24 에서만 걸려야 한다 */
    sh_exit(final_report());
}

/* ---------------- HardFault: 정보만 찍고 끝 (paintbug 데모용) ---------------- */
__attribute__((used)) void hardfault_report(const uint32_t *frame) {
    sh_puts("HardFault: CFSR="); sh_hex(SCB_CFSR); sh_puts(" HFSR="); sh_hex(SCB_HFSR);
    sh_puts(" stacked pc="); sh_hex(frame[6]); sh_puts(" lr="); sh_hex(frame[5]); sh_puts("\n");
    sh_exit(3);
}
__attribute__((naked)) void HardFault_Handler(void) {
    __asm__ volatile("tst lr, #4\n\tite eq\n\tmrseq r0, msp\n\tmrsne r0, psp\n\tb hardfault_report");
}

#ifdef PAINT_BUG   /* 결함 주입: "스택 전체" 를 main 안에서 칠한다 → 살아 있는 프레임(복귀 주소)까지 덮는다 */
__attribute__((noinline)) static void paint_whole_stack_wrong(void) {
    stack_paint(_sstack, _estack);
    sh_puts("painted [_sstack, _estack) from inside main ... returning\n");
}
#endif

/* ---------------- startup (BM-B08 축약판) ---------------- */
void Reset_Handler(void);
void Default_Handler(void) { sh_puts("Default_Handler: unexpected exception\n"); sh_exit(2); }
typedef void (*vector_t)(void);
#define D Default_Handler
__attribute__((section(".isr_vector"), used))
const vector_t g_vectors[16 + 32] = {
    (vector_t)_estack, Reset_Handler, D, HardFault_Handler, D, D, D, 0, 0, 0, 0, D, D, 0, D, SysTick_Handler,
    D, D, D, D, D, D, D, D, D, D, D, D, D, D, Irq14_Handler, Irq15_Handler,
    D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D,
};

static void print_layout(void) {
    sh_puts("layout: neighbor "); sh_hex(addr(_sneighbor)); sh_puts(".."); sh_hex(addr(_eneighbor));
    sh_puts("  guard "); sh_hex(addr(_sguard)); sh_puts(".."); sh_hex(addr(_eguard));
    sh_puts("  stack "); sh_hex(addr(_sstack)); sh_puts(".."); sh_hex(addr(_estack));
    sh_puts("  CCR="); sh_hex(SCB_CCR); sh_puts("\n");
}

int main(void) {
    /* ---- Part A ---- */
    uint32_t hwm_boot = stack_hwm_bytes();
    print_layout();
    sh_puts("A1 boot: HWM="); sh_dec(hwm_boot); sh_puts(" B of "); sh_dec(addr(_estack) - addr(_sstack));
    sh_puts(" (paint stopped "); sh_dec(PAINT_MARGIN); sh_puts(" B below Reset_Handler's SP)\n");
    CHECK(hwm_boot >= PAINT_MARGIN && hwm_boot <= 256u);
    CHECK(_sstack[0] == STACK_PAINT && _sstack[200] == STACK_PAINT);
    CHECK(canary_ok());
    CHECK(addr(_eguard) == addr(_sstack) && (addr(_sguard) & 31u) == 0u);
#ifdef PAINT_BUG
    paint_whole_stack_wrong();
    sh_puts("(not reached)\n");
#endif

    NVIC_IPR(IRQ_LOW) = 0x80u;                   /* 낮은 쪽 */
    NVIC_IPR(IRQ_HIGH) = 0x40u;                  /* 높은 쪽: IRQ_LOW 안에서 선점 */
    NVIC_ISER0 = (1u << IRQ_LOW) | (1u << IRQ_HIGH);

    static const struct { const char *name; fn_t fn; uint32_t su; } k_paths[] = {
        { "leaf          ", path_leaf, SU_path_leaf },
        { "mid>leaf      ", path_mid,  SU_path_mid + SU_path_leaf },
        { "top>mid>leaf  ", path_top,  SU_path_top + SU_path_mid + SU_path_leaf },
    };
    sh_puts("A2 path            static(.su)  measured\n");
    for (uint32_t i = 0; i < 3u; ++i) {
        g_frames_mode = 0u;
        uint32_t d = measure_call(k_paths[i].fn, 7u);
        sh_puts("   "); sh_puts(k_paths[i].name); kv("   ", k_paths[i].su); kv("        ", d); sh_puts("\n");
        CHECK(d == k_paths[i].su);
    }
    const uint32_t path3 = SU_path_top + SU_path_mid + SU_path_leaf;
    static const struct { const char *name; uint32_t mode, expect, pad_low; } k_isr[] = {
        { "top>mid>leaf + IRQ14             ", MODE_PEND_LOW,
          path3 + EXC_FRAME + SU_Irq14_Handler + SU_irq_low_body, 0u },
        { "top>mid>leaf + IRQ14 > IRQ15     ", MODE_PEND_LOW | MODE_NEST_HIGH,
          path3 + 2u * EXC_FRAME + SU_Irq14_Handler + SU_irq_low_body + SU_Irq15_Handler + SU_irq_high_body, 0u },
        { "same, SP%8==4 at IRQ14 (+4 +pad4)", MODE_PEND_LOW | MODE_NEST_HIGH | MODE_MISALIGN,
          path3 + 4u + 4u + 2u * EXC_FRAME + SU_Irq14_Handler + SU_irq_low_body + SU_Irq15_Handler + SU_irq_high_body, 1u },
    };
    sh_puts("A3 with interrupts                    expect  measured  xPSR[9] low/high\n");
    for (uint32_t i = 0; i < 3u; ++i) {
        uint32_t lo0 = g_isr_low_runs, hi0 = g_isr_high_runs;
        g_frames_mode = k_isr[i].mode;
        uint32_t d = measure_call(path_top, 7u);
        g_frames_mode = 0u;
        uint32_t pl = (g_xpsr_low >> 9) & 1u, ph = (g_xpsr_high >> 9) & 1u;
        sh_puts("   "); sh_puts(k_isr[i].name); kv("  ", k_isr[i].expect); kv("    ", d);
        kv("       ", pl); sh_puts("/"); sh_dec((k_isr[i].mode & MODE_NEST_HIGH) ? ph : 0u); sh_puts("\n");
        CHECK(d == k_isr[i].expect);
        CHECK(g_isr_low_runs == lo0 + 1u);
        CHECK(g_isr_high_runs == hi0 + ((k_isr[i].mode & MODE_NEST_HIGH) ? 1u : 0u));
        CHECK(pl == k_isr[i].pad_low);
    }
    uint32_t d_sparse = measure_call(big_sparse, 0u);
    sh_puts("A4 big_sparse alone: static "); sh_dec(SU_big_sparse); sh_puts(" B, measured "); sh_dec(d_sparse);
    sh_puts(" B (= PAINT_MARGIN floor: reserved but never written -> invisible to painting)\n");
    CHECK(d_sparse <= PAINT_MARGIN);
    uint32_t hwm_normal = g_hwm_max;
    sh_puts("A5 worst normal HWM (absolute, from _estack) = "); sh_dec(hwm_normal); sh_puts(" B\n");
    CHECK(hwm_normal > path3 && hwm_normal < addr(_estack) - addr(_sstack));

    /* ---- Part B: SysTick 1 ms 로 카나리 감시 시작 (측정 중엔 끼어들지 않게 Part A 뒤에 켠다) ---- */
    SCB_SHPR3_SYST = 0xE0u;
    SYST_RVR = 25000u - 1u;                      /* mps2 25 MHz → 1 ms (QEMU 는 호스트 시간 기준) */
    SYST_CVR = 0u;
    SYST_CSR = 7u;                               /* CLKSOURCE=core | TICKINT | ENABLE */
    CHECK(wait_ticks(2u));
    CHECK(g_trip_tick == 0u);

    static const struct { const char *name; fn_t bottom; } k_ovf[] = {
        { "B1 overflow into guard only   ", 0 },
        { "B2 overflow past guard        ", 0 },
        { "B3 big frame jumps over guard ", big_sparse },
    };
    for (uint32_t i = 0; i < 3u; ++i) {
        uint32_t stop = i == 0u ? addr(_sstack) - 1u : i == 1u ? addr(_sguard) - 64u : addr(_sstack) + 100u;
        neighbor_fill();
        canary_arm();
        stack_paint(_sstack, (volatile uint32_t *)(read_sp() - PAINT_MARGIN));
        g_trip_tick = 0u;
        __asm__ volatile("cpsid i" ::: "memory");   /* 맨 아래에서 SysTick 프레임 32 B 가 더 쌓이지 않게 (결정적) */
        uint32_t depth = recurse_until(stop, k_ovf[i].bottom);
        __asm__ volatile("cpsie i" ::: "memory");
        int waited = wait_ticks(2u);
        uint32_t first, last, dmg = neighbor_damage(&first, &last);
        uint32_t below = addr(_sstack) > g_min_sp ? addr(_sstack) - g_min_sp : 0u;
        sh_puts(k_ovf[i].name); kv("depth=", depth); sh_puts(" min_sp="); sh_hex(g_min_sp);
        kv(i == 2u ? " (above limit by " : " (below limit by ", i == 2u ? g_min_sp - addr(_sstack) : below);
        sh_puts(" B)\n     canary="); sh_puts(canary_ok() ? "intact" : "DESTROYED");
        sh_puts(g_trip_tick ? " systick=DETECTED" : " systick=silent"); kv(" HWM=", stack_hwm_bytes());
        kv(" neighbor_damaged_words=", dmg);
        if (dmg) {
            kv(" [", first); kv("..", last); sh_puts("] e.g. g_neighbor["); sh_dec(last); kv("]: ", 1000u + last);
            sh_puts(" -> "); sh_hex(g_neighbor[last]);
        }
        sh_puts("\n");
        CHECK(waited);
        if (i == 0u) {                           /* 가드 안에서 멈춤: 탐지 + 피해 없음 */
            CHECK(g_min_sp < addr(_sstack) && g_min_sp >= addr(_sguard));
            CHECK(!canary_ok());
            CHECK(g_trip_tick != 0u);
            CHECK(dmg == 0u);
            CHECK(stack_hwm_bytes() >= addr(_estack) - addr(_sstack) - 8u);  /* HWM 은 "거의 꽉 참" 이라고만 말한다 */
        } else if (i == 1u) {                    /* 가드를 지나침: 탐지는 되지만 이미 이웃이 망가짐 */
            CHECK(g_min_sp < addr(_sguard) && g_min_sp >= addr(_sneighbor));
            CHECK(!canary_ok());
            CHECK(g_trip_tick != 0u);
            CHECK(dmg > 0u);
        } else {                                 /* 건너뛰기: 카나리·HWM 둘 다 "괜찮다", 이웃은 망가짐 */
            CHECK(g_min_sp > addr(_sstack));
            CHECK(canary_ok());
            CHECK(g_trip_tick == 0u);
            CHECK(dmg > 0u);
            CHECK(stack_hwm_bytes() < addr(_estack) - addr(_sstack));
        }
    }
    neighbor_fill();
    canary_arm();

    /* ---- Part C: 출시 체크리스트 (정적 최악값은 practice.c 가 .su + 호출 그래프로 계산) ---- */
    uint32_t reserved = addr(_estack) - addr(_sstack);
    sh_puts("== MSP stack checklist ==\n");
    kv("reserved              ", reserved); sh_puts(" B\n");
    kv("measured HWM (normal) ", hwm_normal); sh_puts(" B\n");
    kv("margin                ", reserved - hwm_normal); kv(" B (", (reserved - hwm_normal) * 100u / reserved);
    sh_puts(" %)\n");
    kv("guard                 ", addr(_eguard) - addr(_sguard)); kv(" B, largest single frame ", SU_big_sparse);
    sh_puts(" B -> jump-over possible\n");
    CHECK(reserved - hwm_normal >= reserved / 4u);   /* 이 레슨의 정책: 실측 여유 25 % 이상 */

    /* ---- Part B 마지막: -fstack-protector (실패하면 돌아오지 않는다 → 그 안에서 보고) ---- */
    static const uint8_t src[32] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                                     17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32 };
    g_ssp_stage = 16u; CHECK(ssp_copy(src, 16u) == 136u);
    g_ssp_stage = 20u; CHECK(ssp_copy(src, 20u) == 136u);   /* buf 뒤 패딩만 덮음 → 못 잡는다 */
    sh_puts("B4 ssp_copy n=16 ok, n=20 (padding only) NOT detected, now n=24 ...\n");
    g_ssp_stage = 24u; g_sink = ssp_copy(src, 24u);
    sh_puts("  ssp_copy(n=24) returned: canary check did not fire\n");
    CHECK(0);
    return final_report();
}

__attribute__((noreturn)) void Reset_Handler(void) {
    uint32_t sp = read_sp();
    stack_paint(_sstack, (volatile uint32_t *)((sp - PAINT_MARGIN) & ~3u));   /* 가장 먼저: 아무것도 쓰기 전에 */
    canary_arm();
    const uint32_t *src = _sidata;
    for (uint32_t *dst = _sdata; dst < _edata;) *dst++ = *src++;
    for (uint32_t *dst = _sbss; dst < _ebss;) *dst++ = 0u;
    sh_exit(main());
}
