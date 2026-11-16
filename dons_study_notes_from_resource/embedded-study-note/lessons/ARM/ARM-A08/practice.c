/* ARM-A08 추가 연습 — 한 번에 검증
 *
 * solution.c 의 모델·해석·시뮬레이터를 그대로 가져와(main 만 이름을 바꿔 포함) 연습 4 개의 답을 검사한다.
 *   연습 1  구현된 우선순위 비트 수: 8 비트(QEMU) 로 짠 설계를 3 비트·2 비트 칩으로 옮기면?
 *   연습 2  이용률(utilization) 검사 vs 응답 시간 해석: U < 1 인데 데드라인을 놓치는 세트
 *   연습 3  FIFO 없는 UART: 최대 보레이트 = "지연 + DR 읽기 위치 ≤ 1 글자 시간"
 *   연습 4  출력 지터: ISR 끝에서 PWM 듀티를 쓰면 지터 = R_max − R_min
 * 빌드 (레슨 디렉터리에서): cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/ARM-A08_pr && /tmp/ARM-A08_pr
 */
#define main solution_main
#include "solution.c"
#undef main

static int p_pass, p_total;
#define PCHECK(cond, msg) do { ++p_total; if (cond) { ++p_pass; } \
    else printf("FAIL  %s  (line %d)\n", msg, __LINE__); } while (0)

static const variant_t *find_v(const variant_t *v, int nv, const char *prefix)
{
    for (int k = 0; k < nv; ++k) if (strncmp(v[k].name, prefix, strlen(prefix)) == 0) return &v[k];
    return NULL;
}

/* ---------- 연습 1: 구현 비트 ---------- */
/* 바이트의 아래 (8 − bits) 비트는 RAZ/WI → 써도 읽으면 0. */
static uint8_t prio_implemented(uint8_t byte, int bits) { return (uint8_t)(byte & (0xFFu << (8 - bits))); }

static void ex1(const variant_t *v9)
{
    printf("== ex1: V9 design on parts with fewer priority bits (PRIGROUP 4)\n");
    static const int BITS[3] = { 8, 3, 2 };
    uint32_t Rcur[3] = {0}, Benc[3] = {0};
    int ok[3] = {0}, same_cur_enc[3] = {0};
    for (int b = 0; b < 3; ++b) {
        sys_t s = v9->s;
        for (int i = 0; i < s.n; ++i) s.irq[i].prio = prio_implemented(s.irq[i].prio, BITS[b]);
        for (int k = 0; k < s.nsec; ++k) if (s.sec[k].kind == SEC_BASEPRI) s.sec[k].basepri = prio_implemented(s.sec[k].basepri, BITS[b]);
        ok[b] = sys_ok(&v9->p, &s);
        same_cur_enc[b] = grp(&s, M_CUR) == grp(&s, M_ENC);
        printf("   %d bits: prio", BITS[b]);
        for (int i = 0; i < s.n; ++i) printf(" %02X", s.irq[i].prio);
        printf(" | R(us)");
        uint32_t viol = 0;
        for (int i = 0; i < s.n; ++i) {
            uint32_t R = 0; char u[16];
            response_bound(&v9->p, &s, i, &R);
            cyc_us(u, sizeof u, R);
            printf(" %s%s", u, R > dl(&s, i) ? "!" : "");
            search_t sr;
            g_rng = 0x5EEDull + (uint64_t)b * 31u + (uint64_t)i;
            sim_search(&v9->p, &s, i, 300, 40000, R + 64, &sr);
            if (sr.resp > R || sr.fails) ++viol;
        }
        response_bound(&v9->p, &s, M_CUR, &Rcur[b]);
        Benc[b] = blocking_bound(&v9->p, &s, M_CUR);
        printf(" | sys_ok=%d, CUR/ENC same group=%d, B(CUR)=%u, sim>bound=%u\n", ok[b], same_cur_enc[b],
               (unsigned)Benc[b], (unsigned)viol);
        PCHECK(viol == 0, "ex1 sim <= bound on reduced-bit parts");
    }
    PCHECK(prio_implemented(0x50, 3) == 0x40 && prio_implemented(0x45, 8) == 0x45 && prio_implemented(0xFF, 2) == 0xC0,
           "ex1 RAZ low bits");
    PCHECK(!same_cur_enc[0] && !same_cur_enc[1] && same_cur_enc[2], "ex1 2 bits: CUR 0x10 and ENC 0x20 collapse into one group");
    PCHECK(Benc[2] == cost_E(&v9->p, &v9->s) - 1 + 150 && Rcur[2] > Rcur[0], "ex1 2 bits: ENC now blocks CUR (same group, after it)");
}

/* ---------- 연습 2: 이용률 ---------- */
/* U = Σ (C_j + O) / T_j, 천분율 (내림). 필요조건일 뿐 충분조건이 아니다. */
static uint32_t util_permille(const cm_params_t *p, const sys_t *s)
{
    uint64_t num = 0;
    for (int j = 0; j < s->n; ++j) num += (uint64_t)(s->irq[j].C + cost_O(p, s)) * 1000000u / s->irq[j].T;
    return (uint32_t)(num / 1000u);
}

static void ex2(const variant_t *v0, const variant_t *v9)
{
    uint32_t u0 = util_permille(&v0->p, &v0->s), u9 = util_permille(&v9->p, &v9->s);
    printf("== ex2: utilization V0 = %u permille (sys_ok=%d), V9 = %u permille (sys_ok=%d)\n",
           (unsigned)u0, sys_ok(&v0->p, &v0->s), (unsigned)u9, sys_ok(&v9->p, &v9->s));
    PCHECK(u0 < 1000 && !sys_ok(&v0->p, &v0->s), "ex2 U < 100% but baseline misses (blocking + priority order)");
    sys_t over = v0->s; over.irq[M_CAN].T = 2000;               /* CAN 이 4 배 자주 */
    PCHECK(util_permille(&v0->p, &over) > 1000 && !sys_ok(&v0->p, &over), "ex2 U > 100% always fails");
}

/* ---------- 연습 3: FIFO 없는 UART ---------- */
static const uint32_t BAUDS[] = { 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600, 1843200 };
#define NBAUD (sizeof BAUDS / sizeof BAUDS[0])
static uint32_t char_cycles(uint32_t baud) { return (uint32_t)(10ull * MHZ * 1000000ull / baud); }   /* 8N1 = 10 비트, 내림 */

/* 표준 보레이트 중 최대: UART T = 1 글자로 두고 L_uart + read_off ≤ T 이고 R_uart ≤ T. 하나도 안 되면 0.
 * (UART 가 가장 급한 쪽이 아니면 T 를 바꿔도 다른 IRQ 의 해석은 그대로 — UART 보다 뒤 순번만 영향) */
static uint32_t max_baud(const cm_params_t *p, const sys_t *s0, int uart, uint32_t read_off)
{
    uint32_t best = 0;
    for (size_t b = 0; b < NBAUD; ++b) {
        sys_t s = *s0;
        uint32_t T = char_cycles(BAUDS[b]), L, R;
        s.irq[uart].T = T; s.irq[uart].D = 0;
        if (latency_bound(p, &s, uart, &L) || response_bound(p, &s, uart, &R) || L + read_off > T || R > T) continue;
        best = BAUDS[b];
    }
    return best;
}

static void ex3(const variant_t *v0, const variant_t *v9)
{
    const uint32_t off = 20;                                   /* ISR 첫 명령 → DR 읽기 (가정) */
    uint32_t b0 = max_baud(&v0->p, &v0->s, M_UART, off), b9 = max_baud(&v9->p, &v9->s, M_UART, off);
    uint32_t L0, L9;
    latency_bound(&v0->p, &v0->s, M_UART, &L0); latency_bound(&v9->p, &v9->s, M_UART, &L9);
    printf("== ex3: UART L bound V0 = %u cyc -> max %u bps; V9 = %u cyc -> max %u bps (char = 10 bits)\n",
           (unsigned)L0, (unsigned)b0, (unsigned)L9, (unsigned)b9);
    PCHECK(b0 == 115200 && b9 == 460800, "ex3 max standard baud: V0 115200, V9 460800");
    /* 목격: V0 를 230400 으로 돌리면 시뮬이 overrun 위상을 찾는가 */
    sys_t s = v0->s; s.irq[M_UART].T = char_cycles(230400); s.irq[M_UART].D = 0;
    search_t sr;
    g_rng = 0xBAD0ull;
    sim_search(&v0->p, &s, M_UART, 2000, 40000, 20000, &sr);
    printf("   V0 @230400: char = %u cyc, sim max UART latency = %u (+%u read) -> %s\n", (unsigned)char_cycles(230400),
           (unsigned)sr.lat, (unsigned)off, sr.lat + off > char_cycles(230400) ? "OVERRUN witnessed" : "no overrun seen");
    PCHECK(sr.lat + off > char_cycles(230400) && sr.lat <= L0, "ex3 sim witnesses the overrun the bound predicts");
}

/* ---------- 연습 4: 출력 지터 ---------- */
/* 최선: 다른 ISR 에서 꼬리 물기로 들어오면 tail + fetch, 혼자면 E. R_min = 그 + C. 지터 = R_max − R_min. */
static uint32_t resp_min(const cm_params_t *p, const sys_t *s, int i)
{
    uint32_t start = (s->n > 1 && p->tail_chain) ? p->tail + p->fetch : cost_E(p, s);
    return start + s->irq[i].C;
}

static void ex4(const variant_t *v0, const variant_t *v9)
{
    const variant_t *V[2] = { v0, v9 };
    uint32_t jit[2] = {0};
    for (int k = 0; k < 2; ++k) {
        uint32_t R = 0; search_t sr; char a[16], b[16];
        response_bound(&V[k]->p, &V[k]->s, M_CUR, &R);
        uint32_t rmin = resp_min(&V[k]->p, &V[k]->s, M_CUR);
        jit[k] = R - rmin;
        g_rng = 0x7177ull + (uint64_t)k;
        sim_search(&V[k]->p, &V[k]->s, M_CUR, 3000, 40000, R + 64, &sr);
        cyc_us(a, sizeof a, jit[k]); cyc_us(b, sizeof b, sr.resp - sr.resp_min);
        printf("== ex4: %-22s CUR R in [%u, %u] -> jitter bound %u cyc = %s us; sim saw [%u, %u] = %s us\n",
               V[k]->name, (unsigned)rmin, (unsigned)R, (unsigned)jit[k], a, (unsigned)sr.resp_min, (unsigned)sr.resp, b);
        PCHECK(sr.resp_min >= rmin && sr.resp <= R, "ex4 simulated responses inside [R_min, R_max]");
    }
    PCHECK(jit[1] == CM4.entry - CM4.tail && jit[0] > 100 * jit[1], "ex4 V9 jitter = entry - tail = 6 cycles");
}

int main(void)
{
    variant_t v[12];
    int nv = motor_variants(v);
    const variant_t *v0 = find_v(v, nv, "V0"), *v9 = find_v(v, nv, "V9");
    PCHECK(v0 && v9, "variants found");
    if (!v0 || !v9) return 1;
    ex1(v9);
    ex2(v0, v9);
    ex3(v0, v9);
    ex4(v0, v9);
    printf("%d/%d passed\n", p_pass, p_total);
    return p_pass == p_total ? 0 : 1;
}
