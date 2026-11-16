/* ENV: qemu */
/* ARM-A02 practice.c — 추가 연습 답. 본문 테스트(solution.c 의 main)를 먼저 돌리고 연습 1–4 를 더한다.
 * 빌드: run_qemu.sh practice → practice.c(+solution.c 포함) + practice.S(+solution.S 포함) + shared_*
 */
#define main   solution_main
#define finish solution_finish
#include "solution.c"
#undef main
#undef finish
int finish(void);
int solution_finish(void) { sh_puts("  -- (solution tests above, practice below) --\n"); return 0; }

void     write_cntp_tval(uint64_t v);
void     write_cntp_cval(uint64_t v);
uint64_t read_cntp_cval(void);
void     write_cntp_ctl(uint64_t v);
uint64_t read_cntp_ctl(void);

#define INTID_PTIMER 30u                 /* PPI 14 = EL1 물리 타이머 (dtb: <1 0x0e 0x104>) */

/* ---- 연습 1·3 이 쓰는 훅 (on_irq 가 IAR 을 읽은 직후 부른다) ---- */
static volatile uint32_t p_sgi_iar[16], p_sgi_cnt[16];
static volatile uint64_t p_pt_cnt, p_pt_target, p_pt_first_cval, p_pt_last_cval, p_pt_period;
static volatile uint32_t p_pt_cval_mode;
static void practice_hook(uint32_t id, uint32_t iar) {
    if (id < 16u) { p_sgi_iar[id] = iar; p_sgi_cnt[id]++; return; }
    if (id == INTID_PTIMER) {
        uint64_t cval = read_cntp_cval();               /* 이번 마감 */
        if (p_pt_cnt == 0u) p_pt_first_cval = cval;
        p_pt_last_cval = cval;
        if (++p_pt_cnt >= p_pt_target) { write_cntp_ctl(0u); return; }   /* 목표 도달: 핸들러 안에서 끈다 */
        if (p_pt_cval_mode) write_cntp_cval(cval + p_pt_period);   /* 마감 기준: 드리프트 0 */
        else                write_cntp_tval(p_pt_period);          /* 지금 기준: 지연만큼 밀린다 */
    }
}

/* ---- 연습 4: GICD_TYPER 해석 ---- */
struct typer_info { uint32_t it_lines, num_irqs, cpus, security_ext, lspi; };
static void decode_typer(uint32_t v, struct typer_info *o) {
    o->it_lines = v & 0x1Fu;                            /* ITLinesNumber [4:0] */
    o->num_irqs = 32u * (o->it_lines + 1u);             /* 최대 1020 까지만 유효 (1020–1023 은 특수) */
    if (o->num_irqs > 1020u) o->num_irqs = 1020u;
    o->cpus = ((v >> 5) & 7u) + 1u;                     /* CPUNumber [7:5] + 1 */
    o->security_ext = (v >> 10) & 1u;                   /* SecurityExtn [10] */
    o->lspi = (v >> 11) & 0x1Fu;                        /* LSPI [15:11] (보안 확장 있을 때만 의미) */
}

static uint64_t pt_run(uint32_t cval_mode, uint64_t n) {   /* CNTP n 틱 → (마지막 − 처음) − (n−1)·P */
    p_pt_cnt = 0;
    p_pt_target = n;                                    /* 루프 종료 ~ irq_disable 사이의 n+1 번째 IRQ 방지 */
    p_pt_cval_mode = cval_mode;
    write_cntp_tval(p_pt_period);
    write_cntp_ctl(CTL_ENABLE);
    int ok = wait_until(&p_pt_cnt, n, read_cntfrq());       /* 한도 1 s */
    write_cntp_ctl(0u);
    if (!ok) return ~0ull;
    return (p_pt_last_cval - p_pt_first_cval) - (n - 1u) * p_pt_period;
}

int main(void) {
    solution_main();
    if (g_ticks < 101u) { CHECK(0); return finish(); }     /* 본문이 IRQ 까지 못 갔으면 연습도 못 한다 */
    uint64_t frq = read_cntfrq();
    g_irq_hook = practice_hook;

    /* 연습 1: SGI 0..15 를 자기 자신에게 → IAR 의 INTID/CPUID, 한 번씩 */
    unsigned ok_sgi = 0;
    for (uint32_t n = 0; n < 16u; n++) {
        gic_enable_irq(n, 0x90u);
        uint64_t before = p_sgi_cnt[n];
        gic_send_sgi_self(n);
        volatile uint64_t c = 0;                           /* p_sgi_cnt 는 32 비트 → 64 비트 거울로 기다린다 */
        uint64_t end = read_cntvct() + frq / 100u;
        irq_enable();
        while (p_sgi_cnt[n] == before && read_cntvct() < end) c++;
        irq_disable();
        uint32_t cpu = 9;
        ok_sgi += p_sgi_cnt[n] == before + 1u && decode_iar(p_sgi_iar[n], &cpu) == n && cpu == 0u;
    }
    dump("  ex1: IAR of SGI 15 = ", p_sgi_iar[15]);
    CHECK(ok_sgi == 16u);
    CHECK(rd32(GICC(GICC_RPR)) == 0xFFu);                  /* 전부 EOI 됐다 */

    /* 연습 2: PMR 경계 — 우선순위 0xA0 은 PMR 0xA0 에서 막히고 0xB0 에서 통과 */
    write_cntv_tval(g_period);
    write_cntv_ctl(CTL_ENABLE);
    wr32(GICC(GICC_PMR), PRIO_TIMER);
    uint64_t t0 = g_ticks;
    spin_for(10u * g_period);
    uint32_t hp = rd32(GICC(GICC_HPPIR)), pend = rd32(GICD(GICD_ISPENDR));
    dump("  ex2: PMR=0xa0 HPPIR = ", hp);
    dump("  ex2: PMR=0xa0 ISPENDR0 = ", pend);
    CHECK(g_ticks == t0 && ((pend >> INTID_VTIMER) & 1u));  /* pending 인데 CPU 로 안 온다 */
    wr32(GICC(GICC_PMR), 0xB0u);
    CHECK(wait_until(&g_ticks, t0 + 1u, frq / 5u));
    wr32(GICC(GICC_PMR), 0xF0u);

    /* 연습 3: 두 번째 소스 = 물리 타이머 (INTID 30), 그리고 TVAL vs CVAL 재무장 드리프트 */
    gic_enable_irq(INTID_PTIMER, 0x90u);
    p_pt_period = ticks_per_period(frq, 300u);
    uint64_t v0 = g_ticks;
    uint64_t drift_cval = pt_run(1u, 30u);
    uint64_t v1 = g_ticks;
    uint64_t drift_tval = pt_run(0u, 30u);
    write_cntv_ctl(0u);
    dumpd("  ex3: CNTP period (ticks)   = ", p_pt_period);
    dumpd("  ex3: CNTV ticks meanwhile  = ", v1 - v0);
    dumpd("  ex3: drift CVAL+=P (ticks) = ", drift_cval);
    dumpd("  ex3: drift TVAL=P  (ticks) = ", drift_tval);
    CHECK(p_pt_period == 208333u);
    CHECK(v1 - v0 > 0u && g_other >= 60u);                 /* 두 소스가 같이 돌았다, 30 은 on_irq 의 "기타" */
    CHECK(drift_cval == 0u);                               /* 마감 기준 재무장: 정확히 29·P */
    CHECK(drift_tval != ~0ull && drift_tval > 0u);         /* 지금 기준 재무장: 매번 지연만큼 늦어진다 */

    /* 연습 4: GICD_TYPER */
    struct typer_info ti;
    decode_typer(rd32(GICD(GICD_TYPER)), &ti);
    dumpd("  ex4: GIC interrupts = ", ti.num_irqs);
    dumpd("  ex4: GIC CPUs       = ", ti.cpus);
    CHECK(ti.num_irqs == 288u && ti.cpus == 1u && ti.security_ext == 0u);
    decode_typer(0x0000FCFFu, &ti);                        /* 손으로 만든 값: 31 → 1020 상한, 8 CPU, 보안 확장, LSPI 31 */
    CHECK(ti.num_irqs == 1020u && ti.cpus == 8u && ti.security_ext == 1u && ti.lspi == 31u);
    decode_typer(0x0000000Fu, &ti);
    CHECK(ti.num_irqs == 512u && ti.cpus == 1u && ti.security_ext == 0u);

    g_irq_hook = 0;
    return finish();
}
