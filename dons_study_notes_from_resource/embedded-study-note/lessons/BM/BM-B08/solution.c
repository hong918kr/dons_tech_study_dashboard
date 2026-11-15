/* ENV: qemu */
/* BM-B08 C로 쓴 startup으로 첫 부팅: QEMU에서 main까지
 *
 * 펌웨어 한 파일 = startup (벡터 테이블 + weak 기본 핸들러 + Reset_Handler + reset_init) + 자기 검사 main.
 * handlers.c 가 SysTick_Handler / HardFault_Handler 를 강한(strong) 심볼로 덮어쓴다.
 * 대상: qemu-system-arm -M mps2-an385 (Cortex-M3), 링커 스크립트 bm_b08.ld, 출력은 세미호스팅.
 * 빌드/실행 (Lima VM elinux 안): bash run_qemu.sh solution|starter|gdb|faults|map|practice
 *   arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -std=c11 -O2 -g -Wall -Wextra -Werror \
 *     -ffreestanding -nostdlib -fno-tree-loop-distribute-patterns -T bm_b08.ld <this>.c handlers.c -lgcc
 */
#include <stddef.h>
#include <stdint.h>

/* ---------------- 링커 스크립트가 만든 심볼: "값" 이 아니라 "주소" 가 정보다 ---------------- */
extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[];
typedef void (*init_fn)(void);
extern init_fn __init_array_start[], __init_array_end[];

/* ---------------- 세미호스팅 (BKPT 0xAB → QEMU 가 호스트에서 처리) ---------------- */
static int semi(int op, const void *arg) {
    register int r0 __asm__("r0") = op;
    register const void *r1 __asm__("r1") = arg;
    __asm__ volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}
void sh_puts(const char *s) { semi(0x04, s); }                       /* SYS_WRITE0 */
void sh_hex(uint32_t v) {                                            /* "0x%08x" */
    char b[11];
    b[0] = '0'; b[1] = 'x'; b[10] = 0;
    for (int i = 9; i >= 2; --i, v >>= 4) b[i] = "0123456789abcdef"[v & 0xFu];
    sh_puts(b);
}
void sh_dec(uint32_t v) {
    char b[11];
    int i = 10;
    b[i] = 0;
    do { b[--i] = (char)('0' + v % 10u); v /= 10u; } while (v);
    sh_puts(&b[i]);
}
__attribute__((noreturn)) void sh_exit(int code) {
    uint32_t blk[2] = { 0x20026u, (uint32_t)code };                  /* ADP_Stopped_ApplicationExit, 종료 코드 */
    semi(0x20, blk);                                                 /* SYS_EXIT_EXTENDED → qemu exit = code */
    semi(0x18, (const void *)(uintptr_t)(code ? 0x20024u : 0x20026u)); /* 구형 호스트용 SYS_EXIT */
    for (;;) { }
}

/* ---------------- Part A: 벡터 테이블 (C 배열) + weak alias 기본 핸들러 ---------------- */
void Reset_Handler(void);
void Default_Handler(void);
#define WEAK_DEFAULT __attribute__((weak, alias("Default_Handler")))
void NMI_Handler(void) WEAK_DEFAULT;
void HardFault_Handler(void) WEAK_DEFAULT;
void MemManage_Handler(void) WEAK_DEFAULT;
void BusFault_Handler(void) WEAK_DEFAULT;
void UsageFault_Handler(void) WEAK_DEFAULT;
void SVC_Handler(void) WEAK_DEFAULT;
void DebugMon_Handler(void) WEAK_DEFAULT;
void PendSV_Handler(void) WEAK_DEFAULT;
void SysTick_Handler(void) WEAK_DEFAULT;
void IRQ0_Handler(void) WEAK_DEFAULT;   /* mps2-an385 의 외부 IRQ 는 32 개 — 이름은 일반화 */
void IRQ7_Handler(void) WEAK_DEFAULT;   /* (실제 이름은 보드 문서 AN385 / QEMU hw/arm/mps2.c 확인) */

typedef void (*vector_t)(void);
#define D Default_Handler
#ifdef FAULT_THUMB   /* 결함 주입: .type %function 없는 asm 라벨 → 링커가 bit0(Thumb) 을 안 붙인다 (ARM-M08 의 실수) */
__asm__(".section .text.reset_nothumb,\"ax\",%progbits\n .thumb\n .global Reset_NoThumb\n"
        "Reset_NoThumb:\n b Reset_Handler\n .text\n");
extern char Reset_NoThumb[];
#define RESET_VECTOR ((vector_t)Reset_NoThumb)
#else
#define RESET_VECTOR Reset_Handler
#endif

__attribute__((section(".isr_vector"), used))
const vector_t g_vectors[16 + 32] = {
    (vector_t)_estack,      /* [0]  초기 MSP — 코드 주소가 아니라 RAM 끝 주소 */
    RESET_VECTOR,           /* [1]  Reset: 링커가 bit0=1 (Thumb) 을 넣는다 */
    NMI_Handler,            /* [2]  */
    HardFault_Handler,      /* [3]  */
    MemManage_Handler,      /* [4]  */
    BusFault_Handler,       /* [5]  */
    UsageFault_Handler,     /* [6]  */
    0, 0, 0, 0,             /* [7..10] 예약 */
    SVC_Handler,            /* [11] */
    DebugMon_Handler,       /* [12] */
    0,                      /* [13] 예약 */
    PendSV_Handler,         /* [14] */
    SysTick_Handler,        /* [15] */
    IRQ0_Handler, D, D, D, D, D, D, IRQ7_Handler,          /* IRQ0..7  */
    D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D,        /* IRQ8..23 */
    D, D, D, D, D, D, D, D,                                /* IRQ24..31 */
};

#define REG32(a)   (*(volatile uint32_t *)(uintptr_t)(a))
#define SCB_VTOR   REG32(0xE000ED08u)
#define NVIC_ISER0 REG32(0xE000E100u)
#define NVIC_ICER0 REG32(0xE000E180u)
#define NVIC_ISPR0 REG32(0xE000E200u)
#define SYST_CSR   REG32(0xE000E010u)
#define SYST_RVR   REG32(0xE000E014u)
#define SYST_CVR   REG32(0xE000E018u)

volatile uint32_t g_default_ipsr;            /* Default_Handler 가 마지막으로 본 IPSR */

/* 주인 없는 예외가 모두 여기로 온다. 진짜 펌웨어는 보통 무한 루프(디버거로 IPSR 확인).
 * 이 레슨은 검사용으로: 외부 IRQ 면 기록 + 그 IRQ 를 끄고 복귀, 시스템 예외면 보고 후 종료. */
void Default_Handler(void) {
    uint32_t ipsr;
    __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
    g_default_ipsr = ipsr;
    if (ipsr >= 16u && ipsr < 48u) {
        NVIC_ICER0 = 1u << (ipsr - 16u);
        return;
    }
    sh_puts("Default_Handler: unexpected exception, IPSR=");
    sh_dec(ipsr);
    sh_puts("\n");
    sh_exit(2);
}

/* ---------------- TODO: reset_init (BM-B07 의 리셋 초기화를 실제 링커 심볼로) ---------------- */
/* 1) .data: FLASH 의 _sidata 에서 RAM 의 [_sdata, _edata) 로 워드 복사
 * 2) .bss : [_sbss, _ebss) 를 0 으로
 * 3) __init_array_start..end 의 함수 포인터를 순서대로 호출
 * 주의: 이 함수가 끝나기 전에는 .data/.bss 전역 변수를 읽지도 쓰지도 말 것. */
__attribute__((noinline)) void reset_init(void) {
    const uint32_t *src = _sidata;
    for (uint32_t *dst = _sdata; dst < _edata;) *dst++ = *src++;
    for (uint32_t *dst = _sbss; dst < _ebss;) *dst++ = 0u;
    for (init_fn *f = __init_array_start; f < __init_array_end; ++f) (*f)();
}

/* ---------------- 하네스: 리셋 직후 RAM 은 쓰레기 (QEMU 는 0 으로 시작하므로 일부러 더럽힌다) ---------------- */
__attribute__((noinline)) static void ram_poison(void) {
#ifndef NO_POISON
    for (uint32_t *p = _sdata; p < _ebss; ++p) *p = 0xA5A5A5A5u;
#endif
}

int main(void);
__attribute__((noreturn)) void Reset_Handler(void) {
    ram_poison();
    reset_init();
    sh_exit(main());        /* crt0 의 exit(main()) 과 같은 모양: main 의 반환값이 QEMU 종료 코드 */
}

/* ---------------- 자기 검사용 전역들 ---------------- */
uint32_t g_magic = 0xC0FFEE42u;                 /* .data (워드)           */
int16_t g_neg = -1234;                          /* .data (하프워드, 음수)  */
char g_msg[] = "hello, .data";                  /* .data (13 바이트)       */
uint8_t g_odd[3] = { 0x11, 0x22, 0x33 };        /* .data (홀수 크기)       */
const uint32_t g_table[4] = { 1u, 10u, 100u, 1000u }; /* .rodata: FLASH 에만 */
uint32_t g_zero[8];                             /* .bss                    */
static volatile uint32_t s_counter;             /* .bss (static, volatile: 안 쓰여도 안 지워지게) */
uint32_t g_explicit_zero = 0;                   /* = 0 도 .bss 로 간다      */
volatile uint32_t g_ticks;                      /* handlers.c 의 SysTick_Handler 가 올린다 */
uint32_t g_ctor_n;                              /* 생성자 기록 (.bss → 생성자보다 먼저 0 이어야) */
uint32_t g_ctor_log[4];
uint32_t g_ctor_saw_magic;

__attribute__((constructor(101))) static void ctor_first(void) {
    g_ctor_saw_magic = g_magic;                 /* .data 복사가 생성자보다 먼저여야 0xC0FFEE42 */
    if (g_ctor_n < 4u) g_ctor_log[g_ctor_n++] = 1u;
}
__attribute__((constructor)) static void ctor_second(void) {
    if (g_ctor_n < 4u) g_ctor_log[g_ctor_n++] = 2u;
}

/* ---------------- 테스트 하네스 (카운터는 지역 변수: .bss 가 틀려도 셈은 맞게) ---------------- */
#define CHECK(expr) do { total++; if (expr) { pass++; } else { \
    sh_puts("FAIL " __FILE__ ":"); sh_dec(__LINE__); sh_puts("  " #expr "\n"); } } while (0)

static int str_eq(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static uint32_t sp_now(void) {
    uint32_t sp;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    return sp;
}

int main(void) {
    int pass = 0, total = 0;
    sh_puts("BM-B08 boot: main reached\n");
    sh_puts("  _sidata="); sh_hex((uint32_t)(uintptr_t)_sidata);
    sh_puts(" _sdata="); sh_hex((uint32_t)(uintptr_t)_sdata);
    sh_puts(" _edata="); sh_hex((uint32_t)(uintptr_t)_edata);
    sh_puts("\n  _sbss="); sh_hex((uint32_t)(uintptr_t)_sbss);
    sh_puts(" _ebss="); sh_hex((uint32_t)(uintptr_t)_ebss);
    sh_puts(" _estack="); sh_hex((uint32_t)(uintptr_t)_estack);
    sh_puts(" sp="); sh_hex(sp_now());
    sh_puts("\n  g_magic="); sh_hex(g_magic);
    sh_puts(" g_zero[0]="); sh_hex(g_zero[0]);
    sh_puts(" ctor_n="); sh_dec(g_ctor_n);
    sh_puts("\n");

    /* Part A: .data 초기값 / .bss 0 / 주소 배치 */
    CHECK(g_magic == 0xC0FFEE42u);
    CHECK(g_neg == -1234);
    CHECK(str_eq(g_msg, "hello, .data"));
    CHECK(g_odd[0] == 0x11u && g_odd[1] == 0x22u && g_odd[2] == 0x33u);
    uint32_t nz = 0;
    for (size_t i = 0; i < 8; ++i) nz |= g_zero[i];
    CHECK(nz == 0u && s_counter == 0u && g_explicit_zero == 0u);
    CHECK((uintptr_t)&g_magic >= 0x20000000u && (uintptr_t)g_table < 0x00040000u && g_table[3] == 1000u);
    CHECK((uintptr_t)_sidata < 0x00040000u && (uintptr_t)_sdata == 0x20000000u);

    /* Part B: 생성자 (init_array) — 순서, 그리고 .data 를 본 시점 */
    CHECK(g_ctor_n == 2u && g_ctor_log[0] == 1u && g_ctor_log[1] == 2u);
    CHECK(g_ctor_saw_magic == 0xC0FFEE42u);

    /* Part C: 벡터 테이블과 예외 연결 */
    CHECK(SCB_VTOR == 0u);                                   /* 리셋 후 VTOR = 0 */
    CHECK(SCB_VTOR == (uintptr_t)g_vectors);                 /* = 우리 표가 주소 0 에 있다 (7. 실수 4 참고) */
    CHECK((uintptr_t)g_vectors[0] == (uintptr_t)_estack);
    CHECK(((uintptr_t)g_vectors[1] & 1u) == 1u && ((uintptr_t)g_vectors[15] & 1u) == 1u);
    uint32_t sp = sp_now();
    CHECK(sp > (uintptr_t)_ebss && sp <= (uintptr_t)_estack);
    CHECK((uintptr_t)NMI_Handler == (uintptr_t)Default_Handler);       /* weak alias 그대로 */
    CHECK((uintptr_t)SysTick_Handler != (uintptr_t)Default_Handler);   /* handlers.c 가 덮어씀 */

    uint32_t t0 = g_ticks;                    /* SysTick: 1000 사이클마다, 강한 핸들러가 g_ticks++ */
    SYST_RVR = 1000u - 1u;
    SYST_CVR = 0u;
    SYST_CSR = 0x7u;                          /* ENABLE | TICKINT | CLKSOURCE(core) */
    for (uint32_t spin = 0; spin < 2000000u && (uint32_t)(g_ticks - t0) < 3u; ++spin) { }
    SYST_CSR = 0u;
    CHECK((uint32_t)(g_ticks - t0) >= 3u);

    g_default_ipsr = 0u;                      /* 핸들러 없는 IRQ7 을 일부러 펜딩 → Default_Handler */
    NVIC_ISER0 = 1u << 7;
    NVIC_ISPR0 = 1u << 7;
    __asm__ volatile("dsb\n isb" ::: "memory");
    CHECK(g_default_ipsr == 16u + 7u);

    sh_dec((uint32_t)pass); sh_puts("/"); sh_dec((uint32_t)total); sh_puts(" passed\n");
    return pass == total ? 0 : 1;
}
