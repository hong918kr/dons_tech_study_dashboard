/* ENV: qemu */
/* BM-A08 MPU 설정: NULL 트랩, 스택 가드, 실행 금지 RAM
 *
 * 펌웨어 한 파일 = startup (BM-B08 축약판) + PMSAv7 MPU 인코더/로더 + MemManage 기록·복구 + 시험 13 개.
 * BM-M13 은 fault 하나마다 리셋했다. 여기서는 핸들러가 기록한 뒤 "복구"(명령 건너뛰기 / 호출자로 돌아가기 /
 * 태스크 되감기) 하고 같은 부팅에서 다음 시험으로 넘어간다.
 * 대상: qemu-system-arm -M mps2-an385 (Cortex-M3), 링커 스크립트 a08.ld, 출력은 세미호스팅.
 * 빌드/실행 (Lima VM elinux 안): bash run_qemu.sh solution|starter|probe|lockup|syms|practice
 */
#include <stddef.h>
#include <stdint.h>

extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[], _stext[];

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

/* ---------------- 레지스터 (ARMv7-M ARM B3.2 / B3.5 기준 — 확인 권장) ---------------- */
#define REG32(a)        (*(volatile uint32_t *)(uintptr_t)(a))
#define SCB_CPUID       REG32(0xE000ED00u)
#define SCB_VTOR        REG32(0xE000ED08u)
#define SCB_SHCSR       REG32(0xE000ED24u)
#define SCB_CFSR        REG32(0xE000ED28u)
#define SCB_HFSR        REG32(0xE000ED2Cu)
#define SCB_MMFAR       REG32(0xE000ED34u)
#define MPU_TYPE        REG32(0xE000ED90u)
#define MPU_CTRL        REG32(0xE000ED94u)
#define MPU_RNR         REG32(0xE000ED98u)
#define MPU_RBAR        REG32(0xE000ED9Cu)
#define MPU_RASR        REG32(0xE000EDA0u)
#define NVIC_ISER0      REG32(0xE000E100u)
#define NVIC_ISPR0      REG32(0xE000E200u)
#define NVIC_ICPR0      REG32(0xE000E280u)
#define NVIC_IPR1       REG32(0xE000E404u)   /* IRQ4..7, 한 바이트씩 */
#define FPGAIO_LED0     0x40028000u          /* mps2 FPGAIO (BM-M13 에서 info mtree 로 확인) */

#define SHCSR_MEMFAULTENA (1u << 16)
#define CFSR_IACCVIOL   (1u << 0)
#define CFSR_DACCVIOL   (1u << 1)
#define CFSR_MUNSTKERR  (1u << 3)
#define CFSR_MSTKERR    (1u << 4)
#define CFSR_MMARVALID  (1u << 7)
#define HFSR_FORCED     (1u << 30)

#define MPU_CTRL_ENABLE     (1u << 0)
#define MPU_CTRL_HFNMIENA   (1u << 1)
#define MPU_CTRL_PRIVDEFENA (1u << 2)
#define MPU_RBAR_VALID      (1u << 4)
#define MPU_NREGIONS        8u

#define dsb() __asm__ volatile("dsb" ::: "memory")
#define isb() __asm__ volatile("isb" ::: "memory")

/* ================= PMSAv7 영역 인코딩 =================
 * RBAR = ADDR[31:5] | VALID(4) | REGION[3:0]
 * RASR = XN(28) | AP[26:24] | TEX[21:19] S(18) C(17) B(16) | SRD[15:8] | SIZE[5:1] | ENABLE(0)
 * 크기 = 2^(SIZE+1), 32 B ≤ 크기 ≤ 4 GB, base 는 크기의 배수, SRD 는 크기 ≥ 256 B 일 때만 (8 조각 = 크기/8). */
typedef struct { uint32_t rbar, rasr; } mpu_region_t;
typedef enum { MPU_OK = 0, MPU_ERR_REGION, MPU_ERR_SIZE, MPU_ERR_ALIGN, MPU_ERR_SRD, MPU_ERR_AP,
               MPU_ERR_ATTR } mpu_rc_t;

/* AP (특권 / 비특권): 000 없음/없음, 001 RW/없음, 010 RW/RO, 011 RW/RW, 100 예약, 101 RO/없음, 110 RO/RO, 111 RO/RO */
#define AP_NONE   0u
#define AP_PRW    1u
#define AP_FULL   3u
#define AP_PRO    5u
#define AP_RO     6u
/* attr = TEX[2:0]<<3 | S<<2 | C<<1 | B  (RASR[21:16] 그대로) */
#define ATTR_SO     0x00u   /* TEX000 C0 B0: strongly-ordered */
#define ATTR_FLASH  0x02u   /* TEX000 C1 B0: normal, write-through (내부 플래시 관례) */
#define ATTR_SRAM   0x03u   /* TEX000 C1 B1: normal, write-back, 비공유 */

/* ARMv7-M ARM Table B3-13 (TEX/C/B) 의 예약 조합 — 기억 기반, 확인 권장 */
static int attr_valid(uint32_t attr) {
    uint32_t tex = attr >> 3, cb = attr & 3u;
    if (attr > 0x3Fu) return 0;
    if (tex == 1u && cb == 1u) return 0;          /* 001 0 1 예약 */
    if (tex == 2u && cb != 0u) return 0;          /* 010 은 0 0 (비공유 device) 만 */
    if (tex == 3u) return 0;                      /* 011 x x 예약 */
    return 1;
}

/* ================= TODO 1: mpu_encode =================
 * 검사 순서(반환 코드): region ≥ 8 → REGION / size 가 2 의 거듭제곱이 아니거나 32 B 미만 또는 4 GB 초과 → SIZE /
 * base 가 size 의 배수가 아님 → ALIGN / srd > 0xFF 또는 (size < 256 이고 srd ≠ 0) → SRD /
 * ap > 7 또는 ap == 4 → AP / xn > 1 또는 attr 예약 조합 → ATTR.
 * 통과하면 out->rbar, out->rasr (ENABLE=1, RBAR.VALID=1) 를 채우고 MPU_OK. 실패하면 *out 은 건드리지 않는다. */
static mpu_rc_t mpu_encode(uint32_t region, uint32_t base, uint64_t size, uint32_t ap, uint32_t xn,
                           uint32_t attr, uint32_t srd, mpu_region_t *out) {
    if (region >= MPU_NREGIONS) return MPU_ERR_REGION;
    if (size < 32u || size > (1ull << 32) || (size & (size - 1u)) != 0u) return MPU_ERR_SIZE;
    if (((uint64_t)base & (size - 1u)) != 0u) return MPU_ERR_ALIGN;
    if (srd > 0xFFu || (size < 256u && srd != 0u)) return MPU_ERR_SRD;
    if (ap > 7u || ap == 4u) return MPU_ERR_AP;
    if (xn > 1u || !attr_valid(attr)) return MPU_ERR_ATTR;
    uint32_t log2 = 0;
    while ((1ull << log2) < size) log2++;
    out->rbar = base | MPU_RBAR_VALID | region;
    out->rasr = (xn << 28) | (ap << 24) | (attr << 16) | (srd << 8) | ((log2 - 1u) << 1) | 1u;
    return MPU_OK;
}

/* ================= TODO 2: mpu_load =================
 * 1) MPU 끄기 (CTRL=0) + DSB/ISB   2) 영역 0..7 전부 비활성 (RNR=i, RASR=0)
 * 3) 표의 각 항목: RBAR(VALID 포함 → RNR 이 그 영역으로 바뀜) 쓰고 RASR 쓰기
 * 4) CTRL = ctrl, 그리고 DSB + ISB (이후 접근이 새 설정을 보도록). */
static void mpu_load(const mpu_region_t *tab, uint32_t n, uint32_t ctrl) {
    MPU_CTRL = 0u;
    dsb(); isb();
    for (uint32_t i = 0; i < MPU_NREGIONS; ++i) { MPU_RNR = i; MPU_RASR = 0u; }
    for (uint32_t i = 0; i < n; ++i) { MPU_RBAR = tab[i].rbar; MPU_RASR = tab[i].rasr; }
    MPU_CTRL = ctrl;
    dsb(); isb();
}

/* 런타임에 영역 하나만 바꾸기 (주어짐): 비활성화한 뒤 바꾸고 다시 켠다 */
static void mpu_region_write(const mpu_region_t *r) {
    MPU_RBAR = r->rbar;           /* VALID → RNR = 영역 번호 */
    MPU_RASR = 0u;
    MPU_RBAR = r->rbar;
    MPU_RASR = r->rasr;
    dsb(); isb();
}
static void mpu_region_off(uint32_t region) { MPU_RNR = region; MPU_RASR = 0u; dsb(); isb(); }

/* ---------------- 메모리 배치: 태스크(PSP) 스택과 그 아래 ---------------- */
typedef struct {
    uint32_t victim[56];   /* +0   224 B: 가드가 없거나 건너뛰면 조용히 망가지는 "다른 데이터" */
    uint32_t guard[8];     /* +224  32 B: MPU no-access (스택 가드) */
    uint32_t stack[256];   /* +256 1 KB: 태스크 스택 (꼭대기 = +1280) */
} task_area_t;
_Static_assert(offsetof(task_area_t, guard) == 224, "guard offset");
_Static_assert(offsetof(task_area_t, stack) == 256, "stack offset");
static task_area_t g_ta __attribute__((aligned(512)));
static uint32_t g_sub[64] __attribute__((aligned(256)));        /* 서브리전 시험용 256 B */
static uint16_t g_ram_code[16] __attribute__((aligned(32)));    /* RAM 에 복사한 Thumb 코드 32 B */
#define NULL_TRAP_SIZE 256u

/* 영역 번호 = 우선순위 (번호가 클수록 겹칠 때 이긴다) */
enum { R_FLASH = 0, R_SRAM = 1, R_NULL = 2, R_GUARD = 3, R_RAMX = 4, R_SUB = 5 };

static uint32_t g_enc_err;     /* 표를 만들다 실패한 횟수 */
static mpu_region_t region(uint32_t n, uint32_t base, uint64_t size, uint32_t ap, uint32_t xn, uint32_t attr,
                           uint32_t srd) {
    mpu_region_t r = { (n & 0xFu) | MPU_RBAR_VALID, 0u };   /* 실패 시: 비활성 영역 (RASR=0) */
    if (mpu_encode(n, base, size, ap, xn, attr, srd, &r) != MPU_OK) g_enc_err++;
    return r;
}

/* ================= TODO 3: boot_layout =================
 * 부팅 배치 4 개 영역을 tab[0..3] 에 채우고 개수(4)를 돌려준다. 겹치는 곳은 "번호가 큰 영역이 이긴다".
 *   R_FLASH(0): 0x00000000, 256 KB, AP_RO, 실행 허용, ATTR_FLASH
 *   R_SRAM (1): 0x20000000,  64 KB, AP_FULL, XN, ATTR_SRAM          ← RAM 은 실행 금지
 *   R_NULL (2): 0x00000000, NULL_TRAP_SIZE, AP_NONE, XN, ATTR_FLASH  ← FLASH 보다 번호가 커야 이긴다
 *   R_GUARD(3): g_ta.guard, 32 B, AP_NONE, XN, ATTR_SRAM            ← SRAM 보다 번호가 커야 이긴다
 * region() 도우미를 쓴다 (인코딩 실패 → g_enc_err++ 와 비활성 영역). */
static uint32_t boot_layout(mpu_region_t tab[MPU_NREGIONS]) {
    tab[0] = region(R_FLASH, 0x00000000u, 256u * 1024u, AP_RO, 0u, ATTR_FLASH, 0u);
    tab[1] = region(R_SRAM, 0x20000000u, 64u * 1024u, AP_FULL, 1u, ATTR_SRAM, 0u);
    tab[2] = region(R_NULL, 0x00000000u, NULL_TRAP_SIZE, AP_NONE, 1u, ATTR_FLASH, 0u);
    tab[3] = region(R_GUARD, (uint32_t)(uintptr_t)g_ta.guard, 32u, AP_NONE, 1u, ATTR_SRAM, 0u);
    return 4u;
}

/* ================= TODO 4: Thumb 명령 길이 =================
 * 첫 halfword 의 [15:11] 이 0b11101, 0b11110, 0b11111 이면 32 비트 명령(4 B), 아니면 16 비트(2 B). */
static uint32_t thumb_insn_len(uint16_t hw) {
    uint32_t top5 = (uint32_t)hw >> 11;
    return (top5 == 0x1Du || top5 == 0x1Eu || top5 == 0x1Fu) ? 4u : 2u;
}

/* 주소 0 을 "진짜로" 읽기. C 로 *(volatile uint32_t *)0 이라 쓰면 GCC 는 NULL 역참조(UB)로 보고
 * udf #255 (트랩) 를 심는다 (-fdelete-null-pointer-checks, 실측 — 7. 자주 하는 실수). 그래서 asm 으로. */
static inline uint32_t raw_load(uint32_t addr) {
    uint32_t v;
    __asm__ volatile("ldr %0, [%1]" : "=r"(v) : "r"(addr) : "memory");
    return v;
}

/* ---------------- fault 기록 + 복구 정책 (주어짐) ---------------- */
typedef enum { REC_NONE = 0, REC_SKIP, REC_RETURN, REC_ABORT_TASK } recover_t;
#define NOADDR        0xFFFFFFFFu
#define RET_SENTINEL  0xFA11FA11u   /* REC_RETURN 이 r0 로 돌려주는 값 */
#define TASK_ABORTED  0xDEAD7A5Cu   /* 되감긴 태스크의 반환값 */
#define MAX_FAULTS    64u

typedef struct {
    uint32_t n;            /* 이번 시험에서 들어온 fault 수 */
    uint32_t exc;          /* IPSR: 3 HardFault, 4 MemManage */
    uint32_t cfsr, hfsr;
    uint32_t mmfar;        /* MMARVALID 일 때만, 아니면 NOADDR */
    uint32_t mmfar_raw;    /* VALID 와 무관하게 읽은 원시 값 */
    uint32_t pc, lr;       /* 쌓인 프레임 (MSTKERR 면 NOADDR — 프레임이 없다) */
    uint32_t exc_return;
    uint32_t hf_probe;     /* HardFault 안에서 읽어 본 NULL 의 값 (HFNMIENA 시험) */
    uint32_t resume;       /* 복구 뒤 돌아갈 PC (REC_SKIP / REC_RETURN) */
} fault_log_t;
static volatile fault_log_t g_f;
static volatile recover_t g_policy;
static volatile uint32_t g_hf_probe_on;
static volatile uint32_t g_total_faults;
static uint32_t *volatile g_task_top;
extern const char task_abort_landing[];

static void fault_skip(uint32_t *frame) {
    uint32_t pc = frame[6];
    frame[6] = pc + thumb_insn_len(*(const volatile uint16_t *)(uintptr_t)pc);
}
static uint32_t task_rewind(void) {     /* 태스크 스택 꼭대기에 새 프레임: PC = 착지점, r0 = TASK_ABORTED */
    uint32_t *f = g_task_top - 8;
    f[0] = TASK_ABORTED; f[1] = 0u; f[2] = 0u; f[3] = 0u; f[4] = 0u;
    f[5] = NOADDR;
    f[6] = (uint32_t)(uintptr_t)task_abort_landing & ~1u;
    f[7] = 0x01000000u;                                  /* xPSR.T */
    __asm__ volatile("msr psp, %0" :: "r"(f) : "memory");
    return 0xFFFFFFFDu;                                  /* 스레드 모드, PSP, 기본 프레임 */
}

__attribute__((used)) uint32_t fault_c(uint32_t *frame, uint32_t exc_return, uint32_t ipsr) {
    uint32_t exc = ipsr & 0x1FFu;
    if (exc == 3u && g_hf_probe_on)                      /* MPU 를 끄기 "전에": HardFault 에서 MPU 가 보이나? */
        g_f.hf_probe = raw_load(0x0u);
    uint32_t ctrl = MPU_CTRL;
    MPU_CTRL = 0u; dsb(); isb();                         /* 기록·복구 동안은 MPU 끔 (프레임이 가드 안일 수도) */
    uint32_t cfsr = SCB_CFSR, hfsr = SCB_HFSR;
    g_total_faults++;
    g_f.n++;
    g_f.exc = exc; g_f.cfsr = cfsr; g_f.hfsr = hfsr; g_f.exc_return = exc_return;
    g_f.mmfar_raw = SCB_MMFAR;
    g_f.mmfar = (cfsr & CFSR_MMARVALID) ? g_f.mmfar_raw : NOADDR;
    int frame_ok = (cfsr & CFSR_MSTKERR) == 0u;          /* 쌓기 실패 → 프레임을 믿을 수 없다 */
    g_f.pc = frame_ok ? frame[6] : NOADDR;
    g_f.lr = frame_ok ? frame[5] : NOADDR;
    SCB_CFSR = cfsr;                                     /* W1C: 다음 시험이 깨끗하게 */
    SCB_HFSR = hfsr;
    if (g_f.n > MAX_FAULTS) { sh_puts("fault storm: recovery did not advance -> exit 3\n"); sh_exit(3); }
    uint32_t ret = exc_return;
    switch (g_policy) {
    case REC_SKIP:
        if (!frame_ok) { sh_puts("REC_SKIP without a frame -> exit 4\n"); sh_exit(4); }
        fault_skip(frame);
        break;
    case REC_RETURN:                                     /* 호출된 함수가 즉시 돌아간 것처럼 */
        if (!frame_ok) { sh_puts("REC_RETURN without a frame -> exit 4\n"); sh_exit(4); }
        frame[6] = frame[5] & ~1u;
        frame[0] = RET_SENTINEL;
        break;
    case REC_ABORT_TASK:
        if (exc_return != 0xFFFFFFFDu) { sh_puts("REC_ABORT_TASK but not from PSP thread -> exit 4\n"); sh_exit(4); }
        ret = task_rewind();
        break;
    default:
        sh_puts("unexpected fault: exc="); sh_dec(exc); sh_puts(" CFSR="); sh_hex(cfsr);
        sh_puts(" pc="); sh_hex(g_f.pc); sh_puts(" -> exit 5\n");
        sh_exit(5);
    }
    g_f.resume = (frame_ok && g_policy != REC_ABORT_TASK) ? frame[6] : NOADDR;
    MPU_CTRL = ctrl; dsb(); isb();
    return ret;
}

/* MemManage + HardFault 진입: 어느 스택에 프레임이 있나 고르고 C 로. C 가 돌려준 EXC_RETURN 으로 복귀. */
__attribute__((naked)) void Fault_Entry(void) {
    __asm__ volatile(
        "tst   lr, #4      \n"
        "ite   eq          \n"
        "mrseq r0, msp     \n"
        "mrsne r0, psp     \n"
        "mov   r1, lr      \n"
        "mrs   r2, ipsr    \n"
        "push  {r4, lr}    \n"
        "bl    fault_c     \n"
        "pop   {r4, lr}    \n"
        "bx    r0          \n");
}
void HardFault_Handler(void) __attribute__((alias("Fault_Entry")));
void MemManage_Handler(void) __attribute__((alias("Fault_Entry")));

/* 함수 fn(arg) 를 PSP 위의 "태스크" 로 실행. 되감기면 task_abort_landing 에 r0=TASK_ABORTED 로 착지. */
__attribute__((naked, noinline)) uint32_t run_on_psp(uint32_t (*fn)(uint32_t) __attribute__((unused)), uint32_t arg __attribute__((unused)),
                                                     uint32_t *psp_top __attribute__((unused))) {
    __asm__ volatile(
        "push  {r4, lr}            \n"
        "msr   psp, r2             \n"
        "mrs   r3, control         \n"
        "orr   r3, r3, #2          \n"     /* SPSEL=1: 스레드가 PSP 사용 */
        "msr   control, r3         \n"
        "isb                       \n"
        "mov   r4, r0              \n"
        "mov   r0, r1              \n"
        "blx   r4                  \n"
        ".global task_abort_landing\n"
        ".thumb_func               \n"
        "task_abort_landing:       \n"
        "mrs   r3, control         \n"
        "bic   r3, r3, #2          \n"     /* MSP 로 복귀 */
        "msr   control, r3         \n"
        "isb                       \n"
        "pop   {r4, pc}            \n");
}

/* ---------------- startup (BM-B08 축약판) ---------------- */
void Reset_Handler(void);
void Irq7_Handler(void);
void Default_Handler(void) {
    uint32_t ipsr;
    __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
    sh_puts("Default_Handler: unexpected exception "); sh_dec(ipsr); sh_puts(" CFSR="); sh_hex(SCB_CFSR);
    sh_puts("\n");
    sh_exit(2);
}
typedef void (*vector_t)(void);
#define D Default_Handler
__attribute__((section(".isr_vector"), used))
const vector_t g_vectors[16 + 32] = {
    (vector_t)_estack, Reset_Handler, D, HardFault_Handler, MemManage_Handler, D,
    D, 0, 0, 0, 0, D, D, 0, D, D,
    D, D, D, D, D, D, D, Irq7_Handler, D, D, D, D, D, D, D, D,
    D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D,
};

static volatile uint32_t g_irq7_runs;
void Irq7_Handler(void) { g_irq7_runs++; }

/* ---------------- 주입 함수: 명령마다 전역 라벨 → 쌓인 PC 와 정확히 비교 ---------------- */
extern const char pc_ldr[], pc_str[], pc_big[];
#define SKIPPED 0xEEEEEEEEu
__attribute__((noinline, noclone)) static uint32_t inject_ldr(uint32_t addr) {
    uint32_t v = SKIPPED;                                /* 건너뛰면 이 값이 남는다 */
    __asm__ volatile(".global pc_ldr\npc_ldr: ldr %0, [%1]" : "+l"(v) : "l"(addr) : "memory");
    return v;
}
__attribute__((noinline, noclone)) static void inject_str(uint32_t addr, uint32_t val) {
    __asm__ volatile(".global pc_str\npc_str: str.w %1, [%0]" :: "r"(addr), "r"(val) : "memory");
}
/* 프레임이 가드(32 B)보다 큰 함수: SP 를 160 B 내리고 맨 아래에 쓴다 */
__attribute__((naked, noinline)) static uint32_t task_big_frame(uint32_t v __attribute__((unused))) {
    __asm__ volatile(
        "sub   sp, sp, #160 \n"
        ".global pc_big     \n"
        "pc_big: str r0, [sp]\n"
        "add   sp, sp, #160 \n"
        "bx    lr           \n");
}

static volatile uint32_t g_depth;
static uintptr_t g_floor;          /* 가드가 없을 때의 비상 탈출: 이 주소 아래로는 내려가지 않는다 */
__attribute__((noinline, noclone)) static uint32_t recurse(uint32_t n) {
    volatile uint32_t pad[4];
    pad[0] = n; pad[3] = n ^ 0x5A5Au;
    g_depth = n;
    if ((uintptr_t)&pad[0] < g_floor) return 0u;
    return recurse(n + 1u) + pad[3];
}
static uint32_t task_recurse(uint32_t arg) { (void)arg; return recurse(1u); }
static uint32_t task_pend_irq7(uint32_t arg) {
    (void)arg;
    NVIC_ISPR0 = 1u << 7;              /* 예외 진입 = PSP 에 32 B 쌓기 → 가드에 닿는다 */
    dsb(); isb();
    return g_irq7_runs;
}

/* ---------------- 시험 하네스 ---------------- */
static int g_pass, g_total;
static const char *g_test = "";
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } else { \
    sh_puts("FAIL " __FILE__ ":"); sh_dec(__LINE__); sh_puts("  " #expr "  [" ); sh_puts(g_test); sh_puts("]\n"); } } while (0)

static void arm_test(const char *name, recover_t pol) {
    g_test = name;
    g_policy = pol;
    g_f.n = 0u; g_f.exc = 0u; g_f.cfsr = 0u; g_f.hfsr = 0u; g_f.mmfar = NOADDR; g_f.mmfar_raw = 0u;
    g_f.pc = 0u; g_f.lr = 0u; g_f.exc_return = 0u; g_f.hf_probe = 0u; g_f.resume = 0u;
}
static void report(uint32_t result) {      /* 한 줄 요약 (풀이 표의 원본) */
    sh_puts(g_test); sh_puts(" faults="); sh_dec(g_f.n); sh_puts(" exc="); sh_dec(g_f.exc);
    sh_puts(" CFSR="); sh_hex(g_f.cfsr); sh_puts(" MMFAR=");
    if (g_f.mmfar == NOADDR) { sh_puts("(invalid, raw "); sh_hex(g_f.mmfar_raw); sh_puts(")"); }
    else sh_hex(g_f.mmfar);
    sh_puts(" pc="); sh_hex(g_f.pc); sh_puts(" result="); sh_hex(result); sh_puts("\n");
}
static void victim_fill(void) { for (uint32_t i = 0; i < 56u; ++i) g_ta.victim[i] = 0x5A5A5A5Au; }
static uint32_t victim_damage(void) {
    uint32_t bad = 0;
    for (uint32_t i = 0; i < 56u; ++i) bad += (g_ta.victim[i] != 0x5A5A5A5Au);
    return bad;
}
static uint32_t peek_nompu(uint32_t addr) {   /* MPU 를 잠깐 끄고 읽기 (검증용) */
    uint32_t c = MPU_CTRL;
    MPU_CTRL = 0u; dsb(); isb();
    uint32_t v = raw_load(addr);
    MPU_CTRL = c; dsb(); isb();
    return v;
}

int main(void) {
    static mpu_region_t tab[MPU_NREGIONS];            /* .bss → 0 (스택 배열 초기화는 memset 호출이 된다) */
    const uint32_t ta = (uint32_t)(uintptr_t)&g_ta;
    const uint32_t estack = (uint32_t)(uintptr_t)_estack;
    const uint32_t boot_ctrl = MPU_CTRL_PRIVDEFENA | MPU_CTRL_ENABLE;

    sh_puts("BM-A08 MPU on QEMU mps2-an385  CPUID="); sh_hex(SCB_CPUID);
    sh_puts(" VTOR="); sh_hex(SCB_VTOR); sh_puts(" &g_ta="); sh_hex(ta);
    sh_puts(" g_sub="); sh_hex((uint32_t)(uintptr_t)g_sub); sh_puts(" g_ram_code=");
    sh_hex((uint32_t)(uintptr_t)g_ram_code); sh_puts("\n");

    /* T0 MPU 존재 확인 (리셋 값) */
    g_test = "T0 MPU_TYPE";
    sh_puts("T0 MPU_TYPE="); sh_hex(MPU_TYPE); sh_puts(" CTRL="); sh_hex(MPU_CTRL); sh_puts("\n");
    CHECK(((MPU_TYPE >> 8) & 0xFFu) == 8u);              /* DREGION = 8 */
    CHECK((MPU_TYPE & 1u) == 0u);                        /* SEPARATE = 0 (통합 MPU) */
    CHECK(MPU_CTRL == 0u);

    /* T1 인코더: 이름 붙은 값과 거부 */
    g_test = "T1 encode";
    {
        mpu_region_t r = { 0u, 0u };
        CHECK(mpu_encode(2, 0x0u, 256u, AP_NONE, 1u, ATTR_FLASH, 0u, &r) == MPU_OK);
        CHECK(r.rbar == 0x00000012u && r.rasr == 0x1002000Fu);
        CHECK(mpu_encode(0, 0x0u, 256u * 1024u, AP_RO, 0u, ATTR_FLASH, 0u, &r) == MPU_OK);
        CHECK(r.rbar == 0x00000010u && r.rasr == 0x06020023u);
        CHECK(mpu_encode(1, 0x20000000u, 65536u, AP_FULL, 1u, ATTR_SRAM, 0u, &r) == MPU_OK);
        CHECK(r.rbar == 0x20000011u && r.rasr == 0x1303001Fu);
        CHECK(mpu_encode(5, 0x20000300u, 256u, AP_NONE, 1u, ATTR_SRAM, 0x0Fu, &r) == MPU_OK);
        CHECK(r.rbar == 0x20000315u && r.rasr == 0x10030F0Fu);
        CHECK(mpu_encode(7, 0x0u, 1ull << 32, AP_FULL, 0u, ATTR_SO, 0u, &r) == MPU_OK);
        CHECK(r.rasr == 0x0300003Fu);                    /* SIZE=31 → 4 GB */
        mpu_region_t keep = r;
        CHECK(mpu_encode(1, 0x20000000u, 48u, AP_FULL, 1u, ATTR_SRAM, 0u, &r) == MPU_ERR_SIZE);
        CHECK(mpu_encode(1, 0x20000000u, 16u, AP_FULL, 1u, ATTR_SRAM, 0u, &r) == MPU_ERR_SIZE);
        CHECK(mpu_encode(1, 0x20000000u, 0u, AP_FULL, 1u, ATTR_SRAM, 0u, &r) == MPU_ERR_SIZE);
        CHECK(mpu_encode(1, 0x0u, 1ull << 33, AP_FULL, 1u, ATTR_SRAM, 0u, &r) == MPU_ERR_SIZE);
        CHECK(mpu_encode(1, 0x20000100u, 1024u, AP_FULL, 1u, ATTR_SRAM, 0u, &r) == MPU_ERR_ALIGN);
        CHECK(mpu_encode(1, 0x20000000u, 128u, AP_FULL, 1u, ATTR_SRAM, 0x0Fu, &r) == MPU_ERR_SRD);
        CHECK(mpu_encode(1, 0x20000000u, 256u, AP_FULL, 1u, ATTR_SRAM, 0x100u, &r) == MPU_ERR_SRD);
        CHECK(mpu_encode(1, 0x20000000u, 256u, 4u, 1u, ATTR_SRAM, 0u, &r) == MPU_ERR_AP);
        CHECK(mpu_encode(1, 0x20000000u, 256u, 8u, 1u, ATTR_SRAM, 0u, &r) == MPU_ERR_AP);
        CHECK(mpu_encode(1, 0x20000000u, 256u, AP_FULL, 2u, ATTR_SRAM, 0u, &r) == MPU_ERR_ATTR);
        CHECK(mpu_encode(1, 0x20000000u, 256u, AP_FULL, 1u, 0x18u, 0u, &r) == MPU_ERR_ATTR);  /* TEX=011 */
        CHECK(mpu_encode(8, 0x20000000u, 256u, AP_FULL, 1u, ATTR_SRAM, 0u, &r) == MPU_ERR_REGION);
        CHECK(r.rbar == keep.rbar && r.rasr == keep.rasr);   /* 실패는 *out 을 건드리지 않는다 */
    }
    g_test = "T1 insn len";
    CHECK(thumb_insn_len(0x6810u) == 2u && thumb_insn_len(0xE7FEu) == 2u && thumb_insn_len(0xBF00u) == 2u);
    CHECK(thumb_insn_len(0xF8C0u) == 4u && thumb_insn_len(0xE92Du) == 4u && thumb_insn_len(0xF000u) == 4u);
    CHECK(thumb_insn_len(0xE800u) == 4u && thumb_insn_len(0xE000u) == 2u);   /* 11101 vs 11100 (B 16 비트) */

    /* 부팅 배치 적재 + 되읽기 */
    g_test = "T1 load";
    g_enc_err = 0u;
    uint32_t nb = boot_layout(tab);
    mpu_load(tab, nb, boot_ctrl);
    CHECK(nb == 4u && g_enc_err == 0u);
    CHECK(MPU_CTRL == boot_ctrl);
    for (uint32_t i = 0; i < nb; ++i) {
        MPU_RNR = i;
        CHECK(MPU_RBAR == (tab[i].rbar & ~MPU_RBAR_VALID));   /* 되읽기: VALID 는 0, REGION = 현재 RNR */
        CHECK(MPU_RASR == tab[i].rasr);
    }
    {   /* 다시 적재하면 예전 영역(여기선 6 번)이 남아 있으면 안 된다 */
        mpu_region_t stale = { (uint32_t)(uintptr_t)g_sub | MPU_RBAR_VALID | 6u, (1u << 28) | (AP_FULL << 24) | (7u << 1) | 1u };
        mpu_region_write(&stale);
        mpu_load(tab, nb, boot_ctrl);
        MPU_RNR = 6u;
        CHECK(MPU_RASR == 0u);
    }
    sh_puts("boot layout loaded: CTRL="); sh_hex(MPU_CTRL);
    for (uint32_t i = 0; i < nb; ++i) { sh_puts(" R"); sh_dec(i); sh_puts("="); sh_hex(tab[i].rbar); sh_puts("/"); sh_hex(tab[i].rasr); }
    sh_puts("\n");

    /* T2 NULL 읽기 */
    uint32_t v;
    arm_test("T2 NULL read  ", REC_SKIP);
    v = inject_ldr(0x0u); report(v);
    CHECK(g_f.n == 1u && g_f.exc == 4u);
    CHECK(g_f.cfsr == (CFSR_DACCVIOL | CFSR_MMARVALID));
    CHECK(g_f.mmfar == 0x0u);
    CHECK(g_f.pc == (uint32_t)(uintptr_t)pc_ldr);
    CHECK(g_f.resume == (uint32_t)(uintptr_t)pc_ldr + 2u);   /* 16 비트 ldr 다음 */
    CHECK(v == SKIPPED);

    /* T3 NULL 구조체 멤버 (p->field, offset 0x10) */
    arm_test("T3 NULL->0x10 ", REC_SKIP);
    v = inject_ldr(0x10u); report(v);
    CHECK(g_f.n == 1u && g_f.exc == 4u && g_f.mmfar == 0x10u);

    /* T4 NULL 쓰기: 막지 않으면 QEMU 에선 벡터[0] 이 조용히 바뀐다 (코드 메모리가 RAM) */
    arm_test("T4 NULL write ", REC_SKIP);
    inject_str(0x0u, 0xBADu); report(0u);
    CHECK(g_f.n == 1u && g_f.exc == 4u && g_f.cfsr == (CFSR_DACCVIOL | CFSR_MMARVALID));
    CHECK(g_f.mmfar == 0x0u && g_f.pc == (uint32_t)(uintptr_t)pc_str);
    CHECK(g_f.resume == (uint32_t)(uintptr_t)pc_str + 4u);   /* 32 비트 str.w 다음 */
    CHECK(peek_nompu(0x0u) == estack);                    /* 벡터[0] (초기 SP) 그대로 */

    /* T5 충돌: 벡터 표도 0x0-0xFF 안. 소프트웨어 읽기는 막히고, 예외 진입의 벡터 읽기는 통과 */
    arm_test("T5 read vec[1]", REC_SKIP);
    v = inject_ldr(0x4u); report(v);
    CHECK(g_f.n == 1u && g_f.mmfar == 0x4u && v == SKIPPED);
    arm_test("T5 IRQ7 vector", REC_NONE);
    NVIC_IPR1 = 0x80u << 24;                              /* IRQ7 우선순위 0x80 (fault 0 보다 낮음) */
    NVIC_ISER0 = 1u << 7;
    g_irq7_runs = 0u;
    NVIC_ISPR0 = 1u << 7; dsb(); isb();                   /* 벡터 0x5C 는 NULL 영역 안 */
    report(g_irq7_runs);
    CHECK(g_irq7_runs == 1u && g_f.n == 0u);

    /* T6 영역 우선순위: NULL 을 0 번, FLASH 를 2 번으로 바꾸면 FLASH 가 이겨서 NULL 이 통과 */
    arm_test("T6 NULL is R0 ", REC_SKIP);
    {
        mpu_region_t sw[4] = { tab[2], tab[1], tab[0], tab[3] };
        sw[0].rbar = (sw[0].rbar & ~0xFu) | 0u;           /* 영역 번호만 맞바꾼다 */
        sw[2].rbar = (sw[2].rbar & ~0xFu) | 2u;
        mpu_load(sw, 4u, boot_ctrl);
        v = inject_ldr(0x0u); report(v);
        CHECK(g_f.n == 0u && v == estack);                /* 막히지 않았다: 번호가 큰 쪽이 이긴다 */
        mpu_load(tab, nb, boot_ctrl);
    }

    /* T7 스택 가드: PSP 태스크의 무한 재귀 (32 B 가드 → 잡히지만 예외 프레임이 가드 아래를 건드린다) */
    arm_test("T7 recursion32", REC_ABORT_TASK);
    victim_fill();
    g_floor = (uintptr_t)&g_ta.victim[8];
    g_task_top = &g_ta.stack[256];
    g_depth = 0u;
    v = run_on_psp(task_recurse, 0u, g_task_top); report(v);
    sh_puts("   depth reached="); sh_dec(g_depth); sh_puts(" victim damaged words="); sh_dec(victim_damage());
    sh_puts(" victim[54..55]="); sh_hex(g_ta.victim[54]); sh_puts(" "); sh_hex(g_ta.victim[55]); sh_puts("\n");
    CHECK(v == TASK_ABORTED);
    CHECK(g_f.n == 1u && g_f.exc == 4u && g_f.exc_return == 0xFFFFFFFDu);
    CHECK(g_f.cfsr == (CFSR_DACCVIOL | CFSR_MMARVALID | CFSR_MSTKERR));   /* 접근 위반 + 쌓기 실패 */
    CHECK(g_f.mmfar >= ta + 224u && g_f.mmfar < ta + 256u);   /* 가드 안 */
    CHECK(g_depth > 30u);
    CHECK(victim_damage() == 2u && g_ta.victim[54] == g_depth + 1u);   /* 쌓인 r0(=n), r1 이 가드 아래에 */
    arm_test("T7 recursion64", REC_ABORT_TASK);          /* 가드 = 프레임(24 B) + 예외 프레임(32 B) 이상 → 64 B */
    victim_fill();
    {
        mpu_region_t g64 = region(R_GUARD, ta + 192u, 64u, AP_NONE, 1u, ATTR_SRAM, 0u);
        mpu_region_write(&g64);
        v = run_on_psp(task_recurse, 0u, g_task_top); report(v);
        mpu_region_write(&tab[3]);
        CHECK(v == TASK_ABORTED && g_f.n == 1u && g_f.exc == 4u);
        CHECK(g_f.mmfar >= ta + 192u && g_f.mmfar < ta + 256u);
        CHECK(victim_damage() == 0u);
    }

    /* T8 MSTKERR: 예외 진입의 "쌓기" 자체가 가드에 닿는다 */
    arm_test("T8 MSTKERR    ", REC_ABORT_TASK);
    victim_fill();
    g_irq7_runs = 0u;
    v = run_on_psp(task_pend_irq7, 0u, &g_ta.stack[4]);  /* PSP = 가드 위 16 B */
    report(v);
    sh_puts("   irq7 runs="); sh_dec(g_irq7_runs); sh_puts("\n");
    CHECK(v == TASK_ABORTED);
    CHECK(g_f.n == 1u && g_f.exc == 4u);
    CHECK(g_f.cfsr == CFSR_MSTKERR);                      /* MMARVALID 없음 */
    CHECK(g_f.mmfar == NOADDR && g_f.pc == NOADDR);
    CHECK(g_irq7_runs == 1u);                             /* 원래 IRQ 는 되감은 뒤 정상 실행 */
    CHECK(victim_damage() == 0u);

    /* T9 가드를 건너뛰는 큰 프레임: 32 B 가드는 160 B 내림을 못 본다 */
    arm_test("T9 big frame32", REC_ABORT_TASK);
    victim_fill();
    v = run_on_psp(task_big_frame, 0xB16F0000u, &g_ta.stack[8]);   /* PSP = 가드 위 32 B */
    report(v);
    CHECK(g_f.n == 0u && v == 0xB16F0000u);
    CHECK(g_ta.victim[32] == 0xB16F0000u && victim_damage() == 1u);   /* 조용한 손상 (+128) */
    arm_test("T9 guard 256 B", REC_ABORT_TASK);
    victim_fill();
    {
        mpu_region_t big = region(R_GUARD, ta, 256u, AP_NONE, 1u, ATTR_SRAM, 0u);
        mpu_region_write(&big);
        v = run_on_psp(task_big_frame, 0xB16F0000u, &g_ta.stack[8]);
        report(v);
        CHECK(v == TASK_ABORTED && g_f.n == 1u && g_f.exc == 4u);
        CHECK((g_f.cfsr & (CFSR_DACCVIOL | CFSR_MMARVALID)) == (CFSR_DACCVIOL | CFSR_MMARVALID));
        mpu_region_write(&tab[3]);                        /* 먼저 되돌린다: victim 을 읽어야 하니까 */
        CHECK(g_f.mmfar == ta + 128u);
        CHECK(victim_damage() == 0u);
    }

    /* T10 실행 금지 RAM: RAM 으로 복사한 함수 호출 → IACCVIOL, 다른 영역으로 실행 허용 → 42 */
    arm_test("T10 XN RAM    ", REC_RETURN);
    g_ram_code[0] = 0x202Au;                              /* movs r0, #42 */
    g_ram_code[1] = 0x4770u;                              /* bx lr        */
    dsb(); isb();
    uint32_t (*volatile ramfn)(void) = (uint32_t (*)(void))((uintptr_t)g_ram_code | 1u);
    v = ramfn(); report(v);
    CHECK(g_f.n == 1u && g_f.exc == 4u);
    CHECK(g_f.cfsr == CFSR_IACCVIOL);                     /* MMARVALID 없음 */
    CHECK(g_f.mmfar == NOADDR);
    CHECK(g_f.pc == (uint32_t)(uintptr_t)g_ram_code);
    CHECK(v == RET_SENTINEL);
    arm_test("T10 RAMX region", REC_SKIP);
    {
        mpu_region_t rx = region(R_RAMX, (uint32_t)(uintptr_t)g_ram_code, 32u, AP_RO, 0u, ATTR_SRAM, 0u);
        mpu_region_write(&rx);                            /* R4 > R1: 이 32 B 만 실행 허용, 대신 읽기 전용 (W^X) */
        v = ramfn(); report(v);
        CHECK(g_f.n == 0u && v == 42u);
        arm_test("T10 W^X write ", REC_SKIP);
        inject_str((uint32_t)(uintptr_t)g_ram_code, 0xBF00BF00u); report(0u);
        CHECK(g_f.n == 1u && g_f.mmfar == (uint32_t)(uintptr_t)g_ram_code);
        CHECK(g_ram_code[0] == 0x202Au);
        mpu_region_off(R_RAMX);
    }

    /* T11 서브리전: 256 B no-access 영역의 앞 4 조각(0..127)은 SRD 로 끔 → 아래 영역(SRAM RW)으로 */
    arm_test("T11 SRD sub0  ", REC_SKIP);
    {
        const uint32_t sb = (uint32_t)(uintptr_t)g_sub;
        mpu_region_t s = region(R_SUB, sb, 256u, AP_NONE, 1u, ATTR_SRAM, 0x0Fu);
        mpu_region_write(&s);
        inject_str(sb + 0u, 0x11u); report(0u);
        CHECK(g_f.n == 0u && peek_nompu(sb) == 0x11u);
        arm_test("T11 SRD sub3  ", REC_SKIP);
        inject_str(sb + 124u, 0x33u); report(0u);
        CHECK(g_f.n == 0u && peek_nompu(sb + 124u) == 0x33u);
        arm_test("T11 SRD sub4  ", REC_SKIP);
        inject_str(sb + 128u, 0x44u); report(0u);
        CHECK(g_f.n == 1u && g_f.mmfar == sb + 128u && peek_nompu(sb + 128u) == 0u);
        arm_test("T11 SRD sub7  ", REC_SKIP);
        inject_str(sb + 252u, 0x77u); report(0u);
        CHECK(g_f.n == 1u && g_f.mmfar == sb + 252u && peek_nompu(sb + 252u) == 0u);
        mpu_region_off(R_SUB);
    }

    /* T12 PRIVDEFENA=0: 영역 밖(주변장치)은 특권이어도 fault, PPB 는 항상 통과 */
    arm_test("T12 noPRIVDEF ", REC_SKIP);
    mpu_load(tab, nb, MPU_CTRL_ENABLE);
    v = inject_ldr(FPGAIO_LED0); report(v);
    CHECK(g_f.n == 1u && g_f.exc == 4u && g_f.mmfar == FPGAIO_LED0 && v == SKIPPED);
    arm_test("T12 PPB CPUID ", REC_SKIP);
    v = inject_ldr(0xE000ED00u); report(v);
    CHECK(g_f.n == 0u && v == SCB_CPUID);
    mpu_load(tab, nb, boot_ctrl);
    arm_test("T12 PRIVDEF=1 ", REC_SKIP);
    v = inject_ldr(FPGAIO_LED0); report(v);
    CHECK(g_f.n == 0u && v != SKIPPED);

    /* T13 HFNMIENA=0: HardFault 안에서는 MPU 가 꺼진 것처럼 → NULL 이 읽힌다 */
    arm_test("T13 HF probe  ", REC_SKIP);
#ifdef HFNMI_LOCKUP
    mpu_load(tab, nb, boot_ctrl | MPU_CTRL_HFNMIENA);     /* run_qemu.sh lockup: HardFault 안의 fault → Lockup */
#endif
    SCB_SHCSR &= ~SHCSR_MEMFAULTENA; dsb(); isb();        /* MemManage 끔 → HardFault 로 승격 */
    g_hf_probe_on = 1u;
    v = inject_ldr(0x0u); report(v);
    g_hf_probe_on = 0u;
    SCB_SHCSR |= SHCSR_MEMFAULTENA; dsb(); isb();
    sh_puts("   HardFault read *0 = "); sh_hex(g_f.hf_probe); sh_puts(" HFSR="); sh_hex(g_f.hfsr); sh_puts("\n");
    CHECK(g_f.n == 1u && g_f.exc == 3u && g_f.hfsr == HFSR_FORCED);
    CHECK(g_f.cfsr == (CFSR_DACCVIOL | CFSR_MMARVALID) && g_f.mmfar == 0x0u);
    CHECK(g_f.hf_probe == estack);

    g_test = "end";
    CHECK(g_total_faults == 14u);
    CHECK(SCB_CFSR == 0u && MPU_CTRL == boot_ctrl);
    sh_puts("total faults handled in ONE boot: "); sh_dec(g_total_faults); sh_puts("\n");
    sh_dec((uint32_t)g_pass); sh_puts("/"); sh_dec((uint32_t)g_total); sh_puts(" passed\n");
    return g_pass == g_total ? 0 : 1;
}

__attribute__((noreturn)) void Reset_Handler(void) {
    SCB_SHCSR |= SHCSR_MEMFAULTENA;                      /* MemManage 를 HardFault 와 분리 (BM-M13) */
    const uint32_t *src = _sidata;
    for (uint32_t *dst = _sdata; dst < _edata;) *dst++ = *src++;
    for (uint32_t *dst = _sbss; dst < _ebss;) *dst++ = 0u;
    sh_exit(main());
}
