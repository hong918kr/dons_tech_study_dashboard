/* ENV: qemu */
/* ARM-A02 solution.c — EL1 IRQ 처리: GICv2 드라이버 + 제네릭 타이머 틱 + 예외 디스패처 + 펌웨어 테스트.
 * 빌드: run_qemu.sh [starter|solution]  →  <v>.c + <v>.S + shared_start.S + shared_rt.c (+ arm_a02.ld)
 * 실행: qemu-system-aarch64 -M virt,gic-version=2 -cpu cortex-a53 (EL1 부팅, MMU off)
 * 출력: PASS/FAIL 줄, "N/M passed", SYS_EXIT 코드 (0 통과, 1 실패, 3 예외 폭주, 4 예상 밖 예외).
 */
#include <stddef.h>
#include <stdint.h>

/* ---- shared_rt.c / shared_start.S ---- */
void sh_puts(const char *s);
void sh_hex(uint64_t v);
void sh_dec(uint64_t v);
void sh_exit(uint32_t code) __attribute__((noreturn));
void check(int ok, const char *expr, int line);
int  finish(void);
extern volatile uint64_t g_boot_el;
#define CHECK(e) check((e) ? 1 : 0, #e, __LINE__)

/* ---- <v>.S ---- */
extern const uint32_t vectors_el1[], vectors_el1_end[], hold_win_start[], hold_win_end[], svc77_insn[];
void     set_vbar_el1(const void *table);
uint64_t read_vbar_el1(void);
uint64_t read_cntfrq(void);
uint64_t read_cntvct(void);
void     write_cntv_tval(uint64_t v);
void     write_cntv_cval(uint64_t v);
uint64_t read_cntv_cval(void);
void     write_cntv_ctl(uint64_t v);
uint64_t read_cntv_ctl(void);
void     irq_enable(void);
void     irq_disable(void);
uint64_t read_daif(void);
void     wfi_once(void);
uint64_t read_elr_el1(void);
uint64_t read_sctlr_el1(void);
uint64_t read_current_el(void);
void     regs_hold(const uint64_t in[32], uint64_t out[32]);
void     regs_svc(const uint64_t in[32], uint64_t out[32]);

#define ADDR(p) ((uint64_t)(uintptr_t)(p))

/* ---- 예외 프레임: SAVE_ALL(.S) 이 스택에 만드는 배치와 바이트 단위로 같아야 한다 ---- */
struct trap_frame {
    uint64_t x[31];                    /* x0..x30          @0..240 */
    uint64_t sp_el0;                   /*                  @248 */
    uint64_t elr, spsr;                /* ELR_EL1, SPSR_EL1 @256, @264 */
    uint64_t esr, far;                 /* ESR_EL1, FAR_EL1  @272, @280 */
};
_Static_assert(sizeof(struct trap_frame) == 288, "TF_SIZE in .S");
_Static_assert(sizeof(struct trap_frame) % 16 == 0, "SP must stay 16-byte aligned");
_Static_assert(offsetof(struct trap_frame, x[30]) == 240, "TF_X30");
_Static_assert(offsetof(struct trap_frame, sp_el0) == 248, "TF_SP_EL0");
_Static_assert(offsetof(struct trap_frame, elr) == 256 && offsetof(struct trap_frame, spsr) == 264, "TF_ELR/SPSR");
_Static_assert(offsetof(struct trap_frame, esr) == 272 && offsetof(struct trap_frame, far) == 280, "TF_ESR/FAR");

/* ---- GICv2 on QEMU virt (dtb_excerpt.txt: reg = <0x8000000 0x10000 0x8010000 0x10000>) ---- */
#define GICD_BASE        0x08000000u
#define GICC_BASE        0x08010000u
#define GICD_CTLR        0x000u
#define GICD_TYPER       0x004u
#define GICD_IIDR        0x008u
#define GICD_ISENABLER   0x100u
#define GICD_ICENABLER   0x180u
#define GICD_ISPENDR     0x200u
#define GICD_ISACTIVER   0x300u
#define GICD_IPRIORITYR  0x400u
#define GICD_ITARGETSR   0x800u
#define GICD_ICFGR       0xC00u
#define GICD_SGIR        0xF00u
#define GICC_CTLR        0x00u
#define GICC_PMR         0x04u
#define GICC_BPR         0x08u
#define GICC_IAR         0x0Cu
#define GICC_EOIR        0x10u
#define GICC_RPR         0x14u
#define GICC_HPPIR       0x18u
#define GICC_IIDR        0xFCu

#define INTID_VTIMER     27u           /* PPI 11 = EL1 virtual timer (dtb: <1 0x0b 0x104>) */
#define INTID_SGI_NEST   1u            /* Part C: 중첩용 소프트웨어 인터럽트 */
#define INTID_SPURIOUS   1023u
#define PRIO_TIMER       0xA0u
#define PRIO_SGI         0x80u         /* 숫자가 작을수록 높은 우선순위 */
#define TICK_HZ          1000u
#define CTL_ENABLE       1u
#define CTL_IMASK        2u
#define CTL_ISTATUS      4u

static inline uint32_t rd32(uint64_t a) { return *(volatile uint32_t *)(uintptr_t)a; }
static inline void wr32(uint64_t a, uint32_t v) { *(volatile uint32_t *)(uintptr_t)a = v; }
#define GICD(off) (GICD_BASE + (off))
#define GICC(off) (GICC_BASE + (off))

/* ================= TODO(Part A): GIC 레지스터 주소 계산 (순수 C) ================= */
/* intid 하나가 레지스터 배열에서 bits 비트를 차지할 때(ISENABLER 1, ICFGR 2, IPRIORITYR/ITARGETSR 8)
 * 그 필드가 들어 있는 32 비트 워드의 주소와 워드 안의 비트 위치. */
uint64_t gicd_reg_addr(uint32_t reg_off, uint32_t intid, uint32_t bits) {
    uint32_t per_word = 32u / bits;
    return GICD_BASE + reg_off + 4u * (intid / per_word);
}
uint32_t gicd_reg_shift(uint32_t intid, uint32_t bits) {
    return bits * (intid % (32u / bits));
}

/* GICC_IAR: [9:0] INTID, [12:10] CPUID (SGI 를 보낸 코어), 나머지 예약 */
uint32_t decode_iar(uint32_t iar, uint32_t *cpuid) {
    *cpuid = (iar >> 10) & 7u;
    return iar & 0x3FFu;
}

/* 주기 hz 의 틱에 필요한 카운터 틱 수 (반올림). TVAL 은 부호 있는 32 비트 → 범위 밖이면 0 */
uint64_t ticks_per_period(uint64_t cntfrq, uint32_t hz) {
    if (hz == 0u) return 0u;
    uint64_t t = (cntfrq + hz / 2u) / hz;
    return (t == 0u || t > 0x7FFFFFFFu) ? 0u : t;
}

/* ================= TODO(Part A): GIC 초기화 ================= */
void gic_init(void) {
    wr32(GICD(GICD_CTLR), 0u);                 /* 설정하는 동안 분배기 끔 */
    wr32(GICC(GICC_PMR), 0xF0u);               /* 우선순위 < 0xF0 (숫자) 인 것만 CPU 로 */
    wr32(GICC(GICC_BPR), 2u);                  /* 그룹 우선순위 = bit[7:3] → 0x80 이 0xA0 을 선점할 수 있다 */
    wr32(GICC(GICC_CTLR), 1u);                 /* CPU 인터페이스 켬 (그룹 0 → IRQ) */
    wr32(GICD(GICD_CTLR), 1u);                 /* 분배기 켬 */
}

void gic_enable_irq(uint32_t intid, uint32_t prio) {
    uint64_t pa = gicd_reg_addr(GICD_IPRIORITYR, intid, 8u);
    uint32_t ps = gicd_reg_shift(intid, 8u);
    wr32(pa, (rd32(pa) & ~(0xFFu << ps)) | ((prio & 0xFFu) << ps));     /* 바이트 필드 RMW */
    if (intid >= 32u) {                        /* SPI 만: 대상 CPU 0, PPI/SGI 의 ITARGETSR 는 읽기 전용 */
        uint64_t ta = gicd_reg_addr(GICD_ITARGETSR, intid, 8u);
        uint32_t ts = gicd_reg_shift(intid, 8u);
        wr32(ta, (rd32(ta) & ~(0xFFu << ts)) | (1u << ts));
    }
    wr32(gicd_reg_addr(GICD_ISENABLER, intid, 1u), 1u << gicd_reg_shift(intid, 1u));   /* W1S: RMW 금지 */
}

void gic_send_sgi_self(uint32_t intid) {      /* TargetListFilter = 0b10: 요청한 CPU 자신 */
    wr32(GICD(GICD_SGIR), (2u << 24) | (intid & 0xFu));
}

/* ---- 테스트 모드 (Part B/C 의 버그 재현·중첩) ---- */
enum { MODE_NORMAL, MODE_NO_EOI, MODE_STORM, MODE_SPURIOUS, MODE_LR_LOST, MODE_NEST };
#define STORM_N 2000u
static volatile uint32_t g_mode;
static uint64_t g_period;
static volatile uint64_t g_ticks, g_sgis, g_spurious, g_other, g_irq_total, g_exc_total, g_hold_hits;
static volatile uint32_t g_last_intid, g_depth, g_max_depth, g_noeoi_iar;
static volatile uint64_t g_storm, g_storm_istatus_lo, g_nest_seen, g_elr_clobbered;
static volatile uint32_t g_rpr_in_sgi, g_rpr_in_timer;
static volatile uint64_t g_lat_min = ~0ull, g_lat_max;
static volatile uint64_t g_svc_in[32], g_svc_new[32];
static volatile uint32_t g_svc_seen_mask;
static volatile uint64_t g_svc_elr, g_svc_spsr;

/* ================= TODO(Part A): 타이머 재무장 ================= */
/* 레벨 트리거: CVAL 이 지난 상태(ISTATUS = 1)가 계속되는 동안 선이 계속 높다 → 다음 마감으로 옮겨야 내려간다 */
void vtimer_rearm(void) {
    write_cntv_tval(g_period);
}

void (*volatile g_irq_hook)(uint32_t id, uint32_t iar);   /* 9. 추가 연습이 쓴다 (본문에선 NULL) */

/* ---- 주어짐: 인터럽트 하나를 처리 (EOI 여부를 돌려준다) ---- */
int on_irq(struct trap_frame *f, uint32_t id, uint32_t iar) {
    int eoi = 1;
    g_irq_total++;
    g_last_intid = id;
    if (g_irq_hook) g_irq_hook(id, iar);
    if (f->elr >= ADDR(hold_win_start) && f->elr <= ADDR(hold_win_end)) g_hold_hits++;
    if (++g_depth > g_max_depth) g_max_depth = g_depth;
    if (id == INTID_VTIMER) {
        g_ticks++;
        if (g_mode == MODE_NORMAL) {            /* 마감 → C 진입 지연 (QEMU 에선 참고용일 뿐) */
            uint64_t lat = read_cntvct() - read_cntv_cval();
            if (lat < g_lat_min) g_lat_min = lat;
            if (lat > g_lat_max) g_lat_max = lat;
        }
        switch (g_mode) {
        case MODE_NO_EOI:                       /* 버그 1: 재무장은 했지만 EOI 를 안 한다 */
            vtimer_rearm();
            g_noeoi_iar = iar;
            eoi = 0;
            break;
        case MODE_STORM:                        /* 버그 3: EOI 는 하지만 재무장도 마스크도 안 한다 */
            if (!(read_cntv_ctl() & CTL_ISTATUS)) g_storm_istatus_lo++;
            if (++g_storm >= STORM_N) write_cntv_ctl(CTL_ENABLE | CTL_IMASK);  /* 폭주를 끝내는 안전장치 */
            break;
        case MODE_NEST: {                       /* Part C: 높은 우선순위 SGI 를 받아들이는 중첩 */
            uint64_t s0 = g_sgis;
            g_rpr_in_timer = rd32(GICC(GICC_RPR));
            gic_send_sgi_self(INTID_SGI_NEST);
            irq_enable();                       /* ELR/SPSR 는 이미 프레임에 있다 → 열어도 된다 */
            for (unsigned i = 0; i < 100000u && g_sgis == s0; i++) { }
            irq_disable();
            if (read_elr_el1() != f->elr) g_elr_clobbered++;       /* 중첩 IRQ 가 ELR_EL1 을 덮었다 */
            vtimer_rearm();
            break;
        }
        default:
            vtimer_rearm();
        }
    } else if (id == INTID_SGI_NEST) {
        g_sgis++;
        g_rpr_in_sgi = rd32(GICC(GICC_RPR));
        if (g_depth == 2u) g_nest_seen++;
    } else {
        g_other++;
    }
    g_depth--;
    return eoi;
}

/* ================= TODO(Part A): IRQ 핸들러 — IAR → 처리 → EOIR ================= */
void irq_c(struct trap_frame *f) {
    uint32_t iar = rd32(GICC(GICC_IAR));        /* 읽기 = acknowledge: pending → active */
    uint32_t cpu;
    uint32_t id = decode_iar(iar, &cpu);
    if (id >= 1020u) {                          /* 1023 = spurious: 처리할 것 없음, EOI 도 하지 않는다 */
        g_spurious++;
        return;
    }
    if (on_irq(f, id, iar))
        wr32(GICC(GICC_EOIR), iar);             /* 같은 IAR 값 그대로 (SGI 면 CPUID 포함) */
}

/* ---- 주어짐: 동기 예외 (svc #0x77 = 프레임 왕복 테스트) ---- */
static void sync_c(struct trap_frame *f) {
    uint32_t ec = (uint32_t)(f->esr >> 26) & 0x3Fu;
    if (ec == 0x15u && (f->esr & 0xFFFFu) == 0x77u) {
        uint32_t bad = 0;
        for (unsigned i = 0; i < 31u; i++)
            if (f->x[i] != g_svc_in[i]) bad |= 1u << i;          /* SAVE 가 맞는 자리에 저장했나 */
        g_svc_seen_mask = bad;
        g_svc_elr = f->elr;
        g_svc_spsr = f->spsr;
        for (unsigned i = 0; i < 31u; i++) f->x[i] = g_svc_new[i];  /* RESTORE 가 프레임에서 복원하나 */
        f->spsr = (f->spsr & ~(0xFull << 28)) | (g_svc_new[31] & (0xFull << 28));   /* NZCV 바꾸기 */
        return;
    }
    sh_puts("unexpected sync: ESR="); sh_hex(f->esr); sh_puts(" ELR="); sh_hex(f->elr);
    sh_puts(" FAR="); sh_hex(f->far); sh_puts("\n");
    sh_exit(4);
}

/* ---- 주어짐: 모든 벡터 엔트리가 부르는 C 디스패처 ---- */
__attribute__((noinline)) void exc_c(struct trap_frame *f, uint64_t kind) {
    if (++g_exc_total > 50000u) {
        sh_puts("exception storm: ELR="); sh_hex(f->elr); sh_puts(" ESR="); sh_hex(f->esr); sh_puts("\n");
        sh_exit(3);
    }
    if (kind == 5u) {                           /* 0x280: 현재 EL, SP_ELx, IRQ */
        if (g_mode == MODE_SPURIOUS) write_cntv_ctl(0u);           /* IAR 읽기 전에 장치가 선을 내린 상황 */
        irq_c(f);
        if (g_mode == MODE_LR_LOST)             /* 버그 2 시뮬레이션: x30 을 저장 안 했다면 남는 값 */
            f->x[30] = ADDR(__builtin_return_address(0));
        return;
    }
    if (kind == 4u) { sync_c(f); return; }      /* 0x200: 현재 EL, SP_ELx, Sync */
    sh_puts("unexpected vector 0x"); sh_hex(kind * 0x80u); sh_puts(" ESR="); sh_hex(f->esr);
    sh_puts(" ELR="); sh_hex(f->elr); sh_puts("\n");
    sh_exit(4);
}

/* ---- 테스트 도우미 ---- */
static void dump(const char *name, uint64_t v) { sh_puts(name); sh_hex(v); sh_puts("\n"); }
static void dumpd(const char *name, uint64_t v) { sh_puts(name); sh_dec(v); sh_puts("\n"); }

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint64_t xorshift64(void) {
    uint64_t x = g_rng;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    return g_rng = x;
}

/* IRQ 를 열고 *ctr >= target 이 되거나 카운터 기준 max_ticks 가 지날 때까지 돈다 (wfi 없음 → 안 멈춘다) */
static int wait_until(volatile uint64_t *ctr, uint64_t target, uint64_t max_ticks) {
    uint64_t end = read_cntvct() + max_ticks;
    irq_enable();
    while (*ctr < target && read_cntvct() < end) { }
    irq_disable();
    return *ctr >= target;
}
static void spin_for(uint64_t ticks) {
    uint64_t end = read_cntvct() + ticks;
    irq_enable();
    while (read_cntvct() < end) { }
    irq_disable();
}

/* regs_hold/regs_svc 입력: x0..x30 센티널 + NZCV 패턴 */
static void make_sentinels(uint64_t in[32], unsigned k) {
    static const uint64_t nzcv[4] = { 0x0ull, 0xF0000000ull, 0x60000000ull, 0x90000000ull };
    for (unsigned i = 0; i < 31u; i++) in[i] = xorshift64() ^ ((uint64_t)i << 56);
    in[31] = nzcv[k & 3u];
}
static uint32_t diff_mask(const uint64_t a[32], const uint64_t b[32]) {
    uint32_t m = 0;
    for (unsigned i = 0; i < 32u; i++) if (a[i] != b[i]) m |= 1u << i;   /* bit 31 = NZCV */
    return m;
}

int main(void) {
    sh_puts("ARM-A02 AArch64 vectors + IRQ context save (QEMU virt, GICv2, cortex-a53)\n");
    uint64_t frq = read_cntfrq();
    dump("  CurrentEL     = ", read_current_el());
    dump("  CNTFRQ_EL0    = ", frq);
    dump("  SCTLR_EL1     = ", read_sctlr_el1());
    dump("  GICD_TYPER    = ", rd32(GICD(GICD_TYPER)));
    dump("  GICD_IIDR     = ", rd32(GICD(GICD_IIDR)));
    dump("  GICC_IIDR     = ", rd32(GICC(GICC_IIDR)));
    CHECK(g_boot_el == 1u && read_current_el() == 1u);

    /* ---------- Part A-1: 순수 C 도우미 ---------- */
    static const uint32_t offs[3] = { GICD_ISENABLER, GICD_ICFGR, GICD_IPRIORITYR };
    static const uint32_t bits[3] = { 1u, 2u, 8u };
    unsigned bad = 0;
    for (unsigned r = 0; r < 3u; r++)
        for (uint32_t id = 0; id < 1020u; id++) {          /* 전수: 비트 인덱스로 다시 계산한 값과 비교 */
            uint32_t bitidx = id * bits[r];
            bad += gicd_reg_addr(offs[r], id, bits[r]) != GICD_BASE + offs[r] + 4u * (bitidx / 32u);
            bad += gicd_reg_shift(id, bits[r]) != bitidx % 32u;
        }
    CHECK(bad == 0u);
    CHECK(gicd_reg_addr(GICD_ISENABLER, 27u, 1u) == 0x08000100u && gicd_reg_shift(27u, 1u) == 27u);
    CHECK(gicd_reg_addr(GICD_IPRIORITYR, 27u, 8u) == 0x08000418u && gicd_reg_shift(27u, 8u) == 24u);
    CHECK(gicd_reg_addr(GICD_ICFGR, 27u, 2u) == 0x08000C04u && gicd_reg_shift(27u, 2u) == 22u);
    CHECK(gicd_reg_addr(GICD_ITARGETSR, 33u, 8u) == 0x08000820u && gicd_reg_shift(33u, 8u) == 8u);
    CHECK(gicd_reg_addr(GICD_ISENABLER, 32u, 1u) == 0x08000104u && gicd_reg_shift(32u, 1u) == 0u);
    unsigned bad_iar = 0;
    for (uint32_t cpu = 0; cpu < 8u; cpu++)
        for (uint32_t id = 0; id < 1024u; id++) {
            uint32_t iar = (uint32_t)(xorshift64() << 13) | (cpu << 10) | id, c = 99;
            bad_iar += decode_iar(iar, &c) != id || c != cpu;
        }
    CHECK(bad_iar == 0u);
    CHECK(ticks_per_period(62500000u, 1000u) == 62500u && ticks_per_period(1000000000u, 1000u) == 1000000u);
    CHECK(ticks_per_period(24000000u, 7u) == 3428571u && ticks_per_period(24000000u, 0u) == 0u);
    CHECK(ticks_per_period(0xFFFFFFFFFFull, 1u) == 0u && ticks_per_period(100u, 1000u) == 0u);

    /* ---------- Part A-2: 벡터 설치 + 동기 예외로 프레임 왕복 ---------- */
    CHECK((ADDR(vectors_el1) & 0x7FFu) == 0u && ADDR(vectors_el1_end) - ADDR(vectors_el1) == 0x800u);
    set_vbar_el1(vectors_el1);
    dump("  VBAR_EL1      = ", read_vbar_el1());
    CHECK(read_vbar_el1() == ADDR(vectors_el1));
    uint64_t in[32], out[32];
    make_sentinels(in, 1u);
    for (unsigned i = 0; i < 32u; i++) g_svc_in[i] = in[i];
    for (unsigned i = 0; i < 31u; i++) g_svc_new[i] = ~in[i] ^ 0x5A5Au;
    g_svc_new[31] = 0x60000000u;                                   /* Z C */
    regs_svc(in, out);
    uint64_t want[32];
    for (unsigned i = 0; i < 32u; i++) want[i] = g_svc_new[i];
    dump("  SVC frame mismatch (SAVE)    = ", g_svc_seen_mask);
    dump("  SVC regs mismatch (RESTORE)  = ", diff_mask(out, want));
    dump("  SVC ELR_EL1   = ", g_svc_elr);
    dump("  SVC SPSR_EL1  = ", g_svc_spsr);
    CHECK(g_svc_seen_mask == 0u);                                  /* 31 개 전부 제자리에 저장 */
    CHECK(diff_mask(out, want) == 0u);                             /* C 가 쓴 31 개 + NZCV 가 그대로 복원 */
    CHECK(g_svc_elr == ADDR(svc77_insn) + 4u && (g_svc_spsr & 0xFu) == 0x5u);
    CHECK((g_svc_spsr >> 28) == 0xFu);                             /* svc 직전 NZCV = in[31] = 0xF */

    /* ---------- Part A-3: GIC + 타이머 ---------- */
    gic_init();
    gic_enable_irq(INTID_VTIMER, PRIO_TIMER);
    uint32_t pri = (rd32(GICD(GICD_IPRIORITYR + 24u)) >> 24) & 0xFFu;
    dump("  IPRIORITYR[27]= ", pri);
    dump("  ISENABLER0    = ", rd32(GICD(GICD_ISENABLER)));
    dump("  ICFGR1        = ", rd32(GICD(GICD_ICFGR + 4u)));
    dump("  GICC_BPR      = ", rd32(GICC(GICC_BPR)));
    CHECK(pri == PRIO_TIMER);
    CHECK((rd32(GICD(GICD_ISENABLER)) >> INTID_VTIMER) & 1u);
    CHECK(rd32(GICD(GICD_CTLR)) == 1u && rd32(GICC(GICC_CTLR)) == 1u && rd32(GICC(GICC_PMR)) == 0xF0u);
    uint32_t cpu = 9;
    uint32_t idle_iar = rd32(GICC(GICC_IAR));                      /* 아무것도 pending 아님 */
    dump("  IAR (idle)    = ", idle_iar);
    CHECK(decode_iar(idle_iar, &cpu) == INTID_SPURIOUS);

    g_period = ticks_per_period(frq, TICK_HZ);
    dump("  period (ticks)= ", g_period);
    write_cntv_tval(g_period);
    write_cntv_ctl(CTL_ENABLE);
    int ready = wait_until(&g_ticks, 1u, frq / 5u);                /* 첫 틱이 200 ms 안에 와야 */
    CHECK(ready);
    if (!ready) {
        write_cntv_ctl(0u);
        sh_puts("  (no timer IRQ: GIC/timer/vector TODO not done -> IRQ tests skipped)\n");
        return finish();
    }
    uint64_t end = read_cntvct() + 2u * frq;                       /* 안전 한도 2 s (카운터 기준) */
    irq_enable();
    while (g_ticks < 101u && read_cntvct() < end) wfi_once();       /* wfi 로 잠들어 100 틱 세기 */
    irq_disable();
    dumpd("  ticks         = ", g_ticks);
    dumpd("  tick latency min (counter ticks, QEMU: 참고용) = ", g_lat_min);
    dumpd("  tick latency max (counter ticks, QEMU: 참고용) = ", g_lat_max);
    CHECK(g_ticks >= 101u);
    CHECK(g_last_intid == INTID_VTIMER && g_other == 0u && g_spurious == 0u);
    CHECK(rd32(GICC(GICC_RPR)) == 0xFFu);                          /* 활성 인터럽트 없음 = idle */

    /* 프레임 무결성: x0..x30 + NZCV 센티널을 든 채 IRQ 를 맞는다 */
    uint32_t mask = 0;
    unsigned mism = 0;
    for (unsigned k = 0; k < 200u; k++) {
        make_sentinels(in, k);
        regs_hold(in, out);
        uint32_t m = diff_mask(in, out);
        mask |= m;
        mism += m != 0u;
    }
    dumpd("  hold: IRQs inside window = ", g_hold_hits);
    dump("  hold: mismatch mask      = ", mask);
    CHECK(mism == 0u && mask == 0u);
    CHECK(g_hold_hits >= 200u);                                    /* 매 호출마다 창 안에서 IRQ 를 맞았다 */

    /* ---------- Part B-1: EOI 를 빼먹으면 ---------- */
    uint64_t t0 = g_ticks;
    g_mode = MODE_NO_EOI;
    spin_for(30u * g_period);                                      /* 타이머는 30 번 만료된다 */
    uint32_t rpr = rd32(GICC(GICC_RPR)), hppir = rd32(GICC(GICC_HPPIR));
    uint32_t act = rd32(GICD(GICD_ISACTIVER)), pend = rd32(GICD(GICD_ISPENDR));
    dumpd("  no-EOI: IRQs in 30 periods = ", g_ticks - t0);
    dump("  no-EOI: GICC_RPR  = ", rpr);
    dump("  no-EOI: GICC_HPPIR= ", hppir);
    dump("  no-EOI: ISACTIVER0= ", act);
    dump("  no-EOI: ISPENDR0  = ", pend);
    CHECK(g_ticks - t0 == 1u);                                     /* 딱 한 번 받고 침묵 */
    CHECK(rpr == PRIO_TIMER);                                      /* 실행 우선순위가 0xA0 에 묶임 */
    CHECK((act >> INTID_VTIMER) & 1u);
    g_mode = MODE_NORMAL;
    wr32(GICC(GICC_EOIR), g_noeoi_iar);                            /* 늦은 EOI → 바로 다시 흐른다 */
    CHECK(wait_until(&g_ticks, t0 + 4u, frq / 5u));
    CHECK(rd32(GICC(GICC_RPR)) == 0xFFu);

    /* ---------- Part B-2: x30 을 저장하지 않은 엔트리 (exc_c 가 그 결과를 재현) ---------- */
    g_mode = MODE_LR_LOST;
    make_sentinels(in, 2u);
    regs_hold(in, out);
    g_mode = MODE_NORMAL;
    mask = diff_mask(in, out);
    dump("  lr-lost: mismatch mask = ", mask);
    dump("  lr-lost: x30 after IRQ = ", out[30]);
    dump("  lr-lost: = vectors_el1 + ", out[30] - ADDR(vectors_el1));
    CHECK(mask == (1u << 30));                                     /* 검사기가 x30 만 정확히 잡는다 */
    CHECK(out[30] - ADDR(vectors_el1) >= 0x280u && out[30] - ADDR(vectors_el1) < 0x300u);

    /* ---------- Part B-3: 레벨 트리거 타이머를 재무장/마스크 안 하면 폭주 ---------- */
    g_storm = 0;
    g_mode = MODE_STORM;
    uint64_t spins = 0, tick_before = g_ticks;
    end = read_cntvct() + frq;                                     /* 한도 1 s */
    irq_enable();
    while (g_storm < STORM_N && read_cntvct() < end) spins++;
    irq_disable();
    uint64_t storm_ticks = g_ticks - tick_before;
    dumpd("  storm: IRQs taken      = ", g_storm);
    dumpd("  storm: main loop spins = ", spins);
    dump("  storm: CNTV_CTL after  = ", read_cntv_ctl());
    CHECK(g_storm == STORM_N && storm_ticks == STORM_N);
    CHECK(g_storm_istatus_lo == 0u);                               /* 매번 ISTATUS = 1 인 채로 들어왔다 */
    CHECK(read_cntv_ctl() == (CTL_ENABLE | CTL_IMASK | CTL_ISTATUS));   /* 조건은 여전히 참, 마스크로만 멈춤 */
    spin_for(5u * g_period);
    CHECK(g_storm == STORM_N);                                     /* IMASK 뒤로는 조용 */
    g_mode = MODE_NORMAL;
    write_cntv_tval(g_period);
    write_cntv_ctl(CTL_ENABLE);

    /* ---------- Part B-4: spurious 1023 ---------- */
    uint64_t s0 = g_spurious;
    t0 = g_ticks;
    g_mode = MODE_SPURIOUS;
    int got = wait_until(&g_spurious, s0 + 1u, frq / 5u);
    g_mode = MODE_NORMAL;
    dumpd("  spurious: count = ", g_spurious - s0);
    CHECK(got && g_spurious - s0 == 1u && g_ticks == t0);          /* IAR = 1023, 틱 처리 안 함 */
    CHECK(rd32(GICC(GICC_RPR)) == 0xFFu && (read_cntv_ctl() & CTL_ENABLE) == 0u);
    write_cntv_tval(g_period);
    write_cntv_ctl(CTL_ENABLE);

    /* ---------- Part C: 중첩 IRQ (타이머 0xA0 안에서 SGI 0x80) ---------- */
    gic_enable_irq(INTID_SGI_NEST, PRIO_SGI);
    g_mode = MODE_NEST;
    g_max_depth = 0;
    uint64_t sg0 = g_sgis, nt0 = g_ticks, ns0 = g_nest_seen, ec0 = g_elr_clobbered;
    mask = 0; mism = 0;
    for (unsigned k = 0; k < 100u; k++) {
        make_sentinels(in, k);
        regs_hold(in, out);
        uint32_t m = diff_mask(in, out);
        mask |= m;
        mism += m != 0u;
    }
    g_mode = MODE_NORMAL;
    uint64_t nest_ticks = g_ticks - nt0;
    dumpd("  nest: timer IRQs        = ", nest_ticks);
    dumpd("  nest: SGIs (depth 2)    = ", g_nest_seen - ns0);
    dumpd("  nest: ELR_EL1 clobbered = ", g_elr_clobbered - ec0);
    dump("  nest: RPR in timer/SGI  = ", ((uint64_t)g_rpr_in_timer << 8) | g_rpr_in_sgi);
    dump("  nest: mismatch mask     = ", mask);
    CHECK(nest_ticks >= 100u && g_sgis - sg0 == nest_ticks && g_nest_seen - ns0 == nest_ticks);
    CHECK(g_max_depth == 2u);
    CHECK(g_elr_clobbered - ec0 == nest_ticks);                    /* 매번 덮였다 → 프레임 사본이 필요했다 */
    CHECK(g_rpr_in_timer == PRIO_TIMER && g_rpr_in_sgi == PRIO_SGI);
    CHECK(mism == 0u && mask == 0u);

    write_cntv_ctl(0u);
    dumpd("  IRQs total    = ", g_irq_total);
    dumpd("  exceptions    = ", g_exc_total);
    return finish();
}
