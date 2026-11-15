/* ENV: qemu */
/* ARM-M13 practice.c — 추가 연습 답. 본문 테스트(solution.c 의 main)를 먼저 돌리고 연습 1–4 를 더한다.
 * 빌드: run_qemu.sh practice → practice.c(+solution.c 포함) + practice.S(+solution.S 포함) + shared_*
 * 부팅 4 가지: virt / virt,virtualization=on / virt,secure=on / virt,secure=on,virtualization=on
 */
#define main   solution_main
#define finish solution_finish
#include "solution.c"
#undef main
#undef finish
int finish(void);
int solution_finish(void) { sh_puts("  -- (solution tests above, practice below) --\n"); return 0; }

void     svc_on_sp0(void *sp0_top);
uint64_t read_midr(void);
uint64_t read_mpidr(void);
uint64_t read_pfr0(void);
uint64_t do_smc(uint64_t fid);
extern const uint32_t svc50_insn[], smc_insn[];
volatile uint64_t g_scr_el3;                               /* el3_to_lower 가 기록 */

/* 연습 3: SPSR_ELx 해석 */
struct spsr_info { uint32_t el, sp_h, daif, nzcv, aarch32; };
static void decode_spsr(uint64_t spsr, struct spsr_info *o) {
    o->aarch32 = (uint32_t)(spsr >> 4) & 1u;                /* M[4] = 1 이면 AArch32 에서 온 예외 */
    o->el   = (uint32_t)(spsr >> 2) & 3u;                   /* M[3:2] */
    o->sp_h = (uint32_t)spsr & 1u;                          /* M[0]: 1 = SP_ELx (h), 0 = SP_EL0 (t) */
    o->daif = (uint32_t)(spsr >> 6) & 0xFu;                 /* [9:6] D A I F */
    o->nzcv = (uint32_t)(spsr >> 28) & 0xFu;                /* [31:28] */
}

static uint64_t g_sp0_stack[64] __attribute__((aligned(16)));

int main(void) {
    solution_main();                                        /* 본문 테스트 (CHECK 집계는 이어진다) */
    uint64_t el = get_current_el();
    if (el != 1u || read_vbar_el1() != ADDR(vectors_el1)) { CHECK(0); return finish(); }

    /* 연습 1: EL1t 에서 예외 → 벡터 0x000 */
    svc_on_sp0(&g_sp0_stack[64]);
    const volatile struct exc_rec *s = last();
    dump("  [1] EL1t svc vector = ", s->kind * 0x80u);
    dump("  [1] EL1t svc SPSR   = ", s->spsr);
    CHECK(s->kind == 0u && s->esr == 0x56000050u && s->elr == ADDR(svc50_insn) + 4u);
    CHECK((s->spsr & 0xFu) == 0x4u);                        /* M = EL1t */
    CHECK(s->sp_el0 == ADDR(&g_sp0_stack[64]));             /* 예외 때 SP_EL0 = 우리가 준 스택 (아무것도 안 쌓였다) */

    /* 연습 2: ID 레지스터 */
    uint64_t midr = read_midr(), mpidr = read_mpidr(), pfr0 = read_pfr0();
    dump("  [2] MIDR_EL1         = ", midr);
    dump("  [2] MPIDR_EL1        = ", mpidr);
    dump("  [2] ID_AA64PFR0_EL1  = ", pfr0);
    uint32_t f_el2 = (uint32_t)(pfr0 >> 8) & 0xFu, f_el3 = (uint32_t)(pfr0 >> 12) & 0xFu;
    CHECK(((midr >> 24) & 0xFFu) == 0x41u && ((midr >> 4) & 0xFFFu) == 0xD03u);   /* Arm, Cortex-A53 */
    CHECK(((mpidr >> 31) & 1u) == 1u && (mpidr & 0xFFu) == 0u);                     /* RES1, Aff0 = 0 (코어 0) */
    CHECK((pfr0 & 0xFu) != 0u && ((pfr0 >> 4) & 0xFu) != 0u);                       /* EL0, EL1 있음 */
    CHECK((g_boot_el == 3u) == (f_el3 != 0u));                                       /* EL3 있음 ⇔ EL3 로 부팅 */
    CHECK(g_boot_el != 2u || f_el2 != 0u);                                           /* EL2 로 부팅 ⇒ EL2 있음 */

    /* 연습 3: SPSR 해석 — 본문에서 기록한 실제 값 */
    struct spsr_info si;
    decode_spsr(0x600003c5u, &si);                          /* svc #0x42 때 실측 SPSR */
    CHECK(si.el == 1u && si.sp_h == 1u && si.daif == 0xFu && si.nzcv == 0x6u && si.aarch32 == 0u);
    decode_spsr(0x0u, &si);                                 /* EL0 의 svc */
    CHECK(si.el == 0u && si.sp_h == 0u && si.daif == 0u);
    decode_spsr(s->spsr, &si);                              /* 연습 1 의 EL1t */
    CHECK(si.el == 1u && si.sp_h == 0u);

    /* 연습 4: 부팅 EL 에 따른 HVC/SMC 결과 표 (안전한 조합만 실행) */
    dump("  [4] boot EL = ", g_boot_el);
    dump("  [4] EL2 field = ", f_el2);
    dump("  [4] EL3 field = ", f_el3);
    if (g_boot_el == 3u) {
        dump("  [4] SCR_EL3 written = ", g_scr_el3);
        CHECK(g_scr_el3 == 0x401u && el == 1u);
    }
    unsigned n = g_exc_count;
    if (f_el3 == 0u) {                                      /* EL3 가 없으면 smc 는 EL3 로 갈 곳이 없다 */
        uint64_t r = do_smc(0x84000000u);
        dump("  [4] smc PSCI_VERSION -> x0 = ", r);
        dump("  [4] smc exceptions to EL1  = ", g_exc_count - n);
        if (f_el2 == 0u) {                                  /* virt: PSCI 는 hvc 로 → smc 는 미정의 */
            CHECK(g_exc_count - n == 1u && last()->esr == 0x02000000u && last()->elr == ADDR(smc_insn));
        } else {                                            /* virt,virtualization=on: PSCI 가 smc 로 */
            CHECK(g_exc_count == n && r == 0x10001u);
        }
    }
    if (f_el3 != 0u) {                                      /* secure=on: QEMU PSCI 없음, SCR_EL3.HCE = 0 → hvc 미정의 */
        n = g_exc_count;
        uint64_t r = do_hvc(0x84000000u);
        dump("  [4] hvc -> x0 = ", r);
        dump("  [4] hvc ESR   = ", last()->esr);
        CHECK(g_exc_count - n == 1u && last()->esr == 0x02000000u && last()->elr == ADDR(hvc_insn));
    }
    return finish();
}
