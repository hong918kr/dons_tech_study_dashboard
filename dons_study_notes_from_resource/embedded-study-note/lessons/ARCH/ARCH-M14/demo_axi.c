/* ARCH-M14 demo — 정답 모델(solution.c)로 파형과 표를 찍는다. 체크 없음, 관찰용.
 *
 *   ./demo wave      5채널 텍스트 파형: 쓰기 2개 + 읽기 3개, 다른 ID 응답이 순서를 바꾸는 모습
 *   ./demo viol      버그 주입 → 체커가 처음 잡은 사이클 근처 파형 (B_BEFORE_WLAST, VALID_DROP)
 *   ./demo ot        outstanding K vs 총 사이클 (시뮬 vs 닫힌 식) · R 채널 사용률
 *   ./demo deadlock  "VALID 가 READY 를 기다리는" 매니저 × 서브 READY 정책 3가지
 *   ./demo kill      kill matrix: 버그 8종 × 시드 200개, 깨끗한 회귀 1000 시드
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_axi.c -o demo && ./demo wave
 */
#define main solution_main          /* solution.c 의 테스트 main 은 이름만 바꿔서 같이 링크 */
#include "solution.c"
#undef main

#define MAXW 400
static bus_t g_w[MAXW];
static long g_nw;
static void rec(long t, const bus_t *b) { if (t < MAXW) { g_w[t] = *b; g_nw = t + 1; } }

/* 셀: '.' 쉼, 'r' READY 만, 'w' VALID 인데 READY 없음(기다림), 그 외 handshake → 내용 */
static void cell(int ch, const chan_t *c) {
    if (!c->valid) { printf(c->ready ? "  r" : "  ."); return; }
    if (!c->ready) { printf("  w"); return; }
    switch (ch) {
    case CH_AW: case CH_AR: case CH_B: printf(" %u ", c->p.id); break;
    case CH_W: printf(c->p.last ? "  L" : "  d"); break;
    default:   printf(" %u%c", c->p.id, c->p.last ? 'L' : 'd'); break;
    }
}
static void wave(long from, long to) {
    if (to > g_nw) to = g_nw;
    printf("cycle "); for (long t = from; t < to; t++) printf("%3ld", t % 1000); printf("\n");
    for (int ch = 0; ch < NCH; ch++) {
        printf("%-5s ", CH_NAME[ch]);
        for (long t = from; t < to; t++) cell(ch, &g_w[t].ch[ch]);
        printf("\n");
    }
}

static void demo_wave(void) {
    static const txn_t t[] = {
        { true, 0, 1, 2, 0x0100 }, { true, 1, 0, 2, 0x0200 },
        { false, 0, 3, 2, 0xC000 }, { false, 2, 0, 2, 0x9000 }, { false, 0, 0, 2, 0xA000 },
    };
    cfg_t g = { .t = t, .nt = 5, .seed = 4, .m_delay = 1, .s_delay = 0, .s_rdy_mode = RDY_RAND, .s_rdy_pct = 70,
                .m_rdy_pct = 80, .b_lat = 1, .r_lat = 3, .slow_lat = 6, .max_ot = 4 };
    g_sim.tap = rec; g_nw = 0;
    long cy = sim_run(&g);
    printf("셀: . 쉼 | r READY만 | w VALID인데 READY 없음(대기) | handshake: AW/AR/B = ID, W = d/L(WLAST), R = ID + d/L\n");
    printf("txn: W0 id0 len1 @0x100 · W1 id1 len0 @0x200 · R2 id0 len3 @0xC000(느린 영역 +6) · R3 id2 len0 @0x9000 · R4 id0 len0 @0xA000\n\n");
    wave(0, cy);
    printf("\ncycles=%ld  correct=%d  violations=%u\n", cy, sim_correct(t, 5), chk_total(&g_sim.c));
    g_sim.tap = NULL;
}

static void show_fault(const char *title, unsigned fault, int ch, uint32_t seed) {
    g_sim.tap = rec; g_nw = 0;
    int r = run_fault(seed, fault, ch);
    g_sim.tap = NULL;
    printf("[%s] seed %u → 첫 위반 %s (%s 채널, cycle %ld), 위반 합계 %u\n", title, seed, r < 0 ? "없음" : V_NAME[r],
           CH_NAME[g_sim.c.first_ch], g_sim.c.first_t, chk_total(&g_sim.c));
    long f = g_sim.c.first_t;
    wave(f > 8 ? f - 8 : 0, f + 8);
    printf("\n");
}

static void demo_ot(void) {
    static txn_t rd[48];
    for (int i = 0; i < 48; i++) rd[i] = (txn_t){ false, (uint8_t)(i % 4), 7, 2, 0x8000u + (uint32_t)i * 32u };
    printf("읽기 48 버스트 × 8 beat, 서브 지연 lat=20, 지연 0 · READY 항상 1 → 왕복 = 20 + 8 + 1 = 29 사이클\n");
    printf("ot_needed(20, 8) = %u\n\n", ot_needed(20, 8));
    printf("  K   sim cycles   식(pred)   R beats/cycle\n");
    for (unsigned k = 1; k <= 8; k++) {
        if (48 % k) continue;
        cfg_t g = { .t = rd, .nt = 48, .seed = 1, .s_rdy_mode = RDY_ALWAYS, .m_rdy_pct = 100, .r_lat = 20, .max_ot = (int)k };
        long cy = sim_run(&g);
        printf("%3u   %10ld   %8ld   %.3f\n", k, cy, pred_read_cycles(48, 8, 20, k), 48.0 * 8 / (double)cy);
    }
    printf("\n지연별 필요한 outstanding (beats=4, 예: 128비트 버스 64 B 버스트):\n  lat:");
    const unsigned lats[] = { 0, 10, 50, 100, 200 };
    for (int i = 0; i < 5; i++) printf("%6u", lats[i]);
    printf("\n  K  :");
    for (int i = 0; i < 5; i++) printf("%6u", ot_needed(lats[i], 4));
    printf("\n");
}

static void demo_deadlock(void) {
    static txn_t rd[4];
    for (int i = 0; i < 4; i++) rd[i] = (txn_t){ false, 0, 3, 2, 0x8000u + (uint32_t)i * 16u };
    const struct { bool polite; int mode; const char *what; } c[] = {
        { true, RDY_LAZY, "polite(틀림) x LAZY(합법)" }, { true, RDY_ALWAYS, "polite(틀림) x ALWAYS(합법)" },
        { true, RDY_RAND, "polite(틀림) x RAND 50%(합법)" }, { false, RDY_LAZY, "정상 x LAZY(합법)" },
    };
    printf("매니저 AR 소스 × 서브 ARREADY 정책  →  결과\n");
    for (int i = 0; i < 4; i++) {
        cfg_t g = { .t = rd, .nt = 4, .seed = 9, .s_rdy_mode = c[i].mode, .s_rdy_pct = 50, .m_rdy_pct = 100, .r_lat = 2,
                    .polite_ar = c[i].polite };
        long cy = sim_run(&g);
        printf("  hung=%d cycles=%-4ld AR hs=%u R hs=%-2u violations=%u   <- %s\n", g_sim.hung, g_sim.cycles,
               g_sim.hs_count[CH_AR], g_sim.hs_count[CH_R], chk_total(&g_sim.c), c[i].what);
        (void)cy;
    }
}

static void demo_kill(void) {
    const unsigned faults[] = { F_DROP_VALID, F_CHG_PAYLOAD, F_WLAST, F_B_EARLY, F_B_NO_AW, F_RLAST, F_REORDER, 0x100u };
    const int expect[] = { V_VALID_DROP, V_PAYLOAD, V_WLAST, V_B_EARLY, V_B_NO_AW, V_RLAST, V_ID_ORDER, V_4K };
    int clean = 0; long cyc = 0;
    for (uint32_t seed = 1000; seed < 2000; seed++) {
        int n = gen_traffic(seed, 40); cfg_t g = cfg_rand(seed, n);
        long c = sim_run(&g);
        if (c > 0 && sim_correct(g_tx, n) && chk_total(&g_sim.c) == 0) clean++;
        cyc += c;
    }
    printf("깨끗한 회귀: 1000 시드 × 40 txn → 통과 %d/1000 (평균 %.0f 사이클)\n\n", clean, cyc / 1000.0);
    printf("주입한 버그 (W 채널)   첫 위반이 그 규칙   위반 0 (놓침)   다른 규칙이 먼저\n");
    for (int f = 0; f < 8; f++) {
        int hit = 0, miss = 0, other = 0;
        for (uint32_t seed = 1000; seed < 1200; seed++) {
            int r = run_fault(seed, faults[f], CH_W);
            if (r == expect[f]) hit++; else if (r < 0) miss++; else other++;
        }
        printf("  %-18s %8d/200 %14d %16d\n", V_NAME[expect[f]], hit, miss, other);
    }
}

int main(int argc, char **argv) {
    const char *m = argc > 1 ? argv[1] : "all";
    bool all = !strcmp(m, "all");
    if (all || !strcmp(m, "wave")) demo_wave();
    if (all || !strcmp(m, "viol")) { show_fault("B_EARLY 주입", F_B_EARLY, CH_W, 3); show_fault("VALID_DROP 주입 (AR)", F_DROP_VALID, CH_AR, 5); }
    if (all || !strcmp(m, "ot")) demo_ot();
    if (all || !strcmp(m, "deadlock")) demo_deadlock();
    if (all || !strcmp(m, "kill")) demo_kill();
    return 0;
}
