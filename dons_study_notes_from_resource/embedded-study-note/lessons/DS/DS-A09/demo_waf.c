/* demo_waf.c — solution.c 의 FTL 을 그대로 가져와 WAF 와 작업량을 측정한다 (시간 측정 없음, 전부 정수 연산이라 결정적).
 *
 * build: cc -std=c11 -O2 demo_waf.c -lm -o dw && ./dw        (t1..t4 를 전부 출력, 이 맥에서 약 3 분)
 *        ./dw t3     처럼 표 하나만 고를 수 있다.
 *
 * 기하: 1024 블록 × 128 페이지. 프리컨디셔닝 = 순차 채우기 + 40 drive-writes(DW), 측정 = 그 다음 40 DW.
 * WAF = (측정 구간 host 프로그램 + GC 복사 프로그램) / host 프로그램.
 */
#define PG_PER_BLK 128u
#define MAX_BLK 1024u
#define FTL_NO_MAIN
#include "solution.c"
#include <math.h>
#include <stdlib.h>

static uint64_t rng_s;
static uint32_t rng32(void) {
    uint64_t z = (rng_s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return (uint32_t)((z ^ (z >> 31)) >> 32);
}
/* hot_pct % 의 쓰기가 주소 공간의 앞 space_pct % 로 간다 (100/100 = 균등) */
static uint32_t pick_lpn(uint32_t hot_pct, uint32_t space_pct) {
    uint32_t hot = (uint32_t)((uint64_t)F.nlpn * space_pct / 100u);
    if (hot == 0 || hot >= F.nlpn) return rng32() % F.nlpn;
    if (rng32() % 100u < hot_pct) return rng32() % hot;
    return hot + rng32() % (F.nlpn - hot);
}

typedef struct {
    policy_t pol; age_mode_t age; uint32_t streams, epoch, hot, space;
    double op; uint64_t seed; bool paced; uint32_t wm_start, wm_stop;
} cfg_t;
typedef struct { double waf, avg_free, miss_pct, regret_pct; uint64_t host, gc; uint32_t max_copies; uint64_t rebuilds, picks, all_picks; } res_t;

/* 얼린 시계의 선택을 "지금 시계" 기준 정확한 선택과 비교 (CB 일 때만) */
static uint64_t live_score(uint32_t b) {
    uint32_t v = F.vcnt[b];
    if (v == 0) return UINT64_MAX;
    uint64_t age = F.host_clk > F.stamp[b] ? F.host_clk - F.stamp[b] : 0;
    return cb_score(v, age);
}
static uint64_t g_picks, g_miss; static double g_regret;
static void observe_pick(uint32_t chosen) {
    uint64_t best = 0;
    for (uint32_t b = 0; b < F.nblk; b++) if (F.state[b] == B_CLOSED || b == chosen) { uint64_t s = live_score(b); if (s > best) best = s; }
    uint64_t got = live_score(chosen);
    g_picks++;
    if (got != best) { g_miss++; if (best != UINT64_MAX && best > 0) g_regret += 1.0 - (double)got / (double)best; }
}

static bool write_fg_observed(uint32_t lpn, uint32_t v, bool observe) {
    uint64_t before = F.gc_wr;
    while (F.victim != NONE || (F.host_blk == NONE && F.free_n <= GC_RESERVE)) {
        bool need_pick = F.victim == NONE;
        if (!gc_step()) return false;
        if (need_pick && observe && F.victim != NONE) observe_pick(F.victim);
    }
    uint32_t n = (uint32_t)(F.gc_wr - before);
    if (n > F.gc_max_per_host) F.gc_max_per_host = n;
    return host_program(lpn, v);
}

static res_t run(cfg_t c) {
    uint32_t nblk = MAX_BLK, nlpn = (uint32_t)((double)nblk * PG_PER_BLK / (1.0 + c.op));
    if (!ftl_init(nblk, nlpn, c.pol)) { puts("bad config"); exit(1); }
    F.age_mode = c.age; F.streams = c.streams; F.epoch_len = c.epoch;
    if (c.wm_start) { F.wm_start = c.wm_start; F.wm_stop = c.wm_stop; }
    rng_s = c.seed; g_picks = g_miss = 0; g_regret = 0;
    uint32_t ver = 0;
    bool observe = c.pol == POL_CB && c.epoch > 0 && !c.paced;
    for (uint32_t i = 0; i < nlpn; i++) if (!write_fg_observed(i, ++ver, false)) { puts("write failed"); exit(1); }
    uint64_t pre = (uint64_t)nlpn * 40u, mes = (uint64_t)nlpn * 40u;
    uint64_t h0 = 0, g0 = 0; double free_sum = 0;
    for (uint64_t i = 0; i < pre + mes; i++) {
        if (i == pre) { h0 = F.host_clk; g0 = F.gc_wr; g_picks = g_miss = 0; g_regret = 0; F.gc_max_per_host = 0; }
        uint32_t lpn = pick_lpn(c.hot, c.space);
        bool ok = c.paced ? ftl_write(lpn, ++ver) : write_fg_observed(lpn, ++ver, observe && i >= pre);
        if (!ok) { puts("write failed"); exit(1); }
        if (i >= pre) free_sum += F.free_n;
    }
    res_t r = {0};
    r.host = F.host_clk - h0; r.gc = F.gc_wr - g0;
    r.waf = (double)(r.host + r.gc) / (double)r.host;
    r.avg_free = free_sum / (double)mes;
    r.miss_pct = g_picks ? 100.0 * (double)g_miss / (double)g_picks : 0;
    r.regret_pct = g_picks ? 100.0 * g_regret / (double)g_picks : 0;
    r.max_copies = F.gc_max_per_host; r.rebuilds = F.rebuilds; r.picks = g_picks; r.all_picks = F.erases;
    if (!ftl_check()) { printf("ftl_check failed: %s\n", g_why); exit(1); }
    return r;
}

/* ---------- 해석식 (전부 W 는 주가지) ---------- */
static double lambert_w0(double y) {         /* y in (-1/e, 0): w in (-1, 0), Newton */
    double w = -0.5;
    for (int i = 0; i < 80; i++) {
        double e = exp(w), f = w * e - y;
        w -= f / (e * (w + 1.0));
        if (w <= -1.0) w = -0.9999999;
    }
    return w;
}
static double desnoyers_lru(double a) {      /* Desnoyers 2012 Eq.4: A = a / (a + W(-a e^-a)) */
    return a / (a + lambert_w0(-a * exp(-a)));
}
static double desnoyers_greedy(double a, double np) {  /* Eq.11/12 */
    double c = (1.0 + 1.0 / (2.0 * np)) * a;
    double x0 = 0.5 - (np / a) * lambert_w0(-c * exp(-c));
    return np / (np - (x0 - 1.0));
}
static double agarwal(double rho) { return (1.0 + rho) / (2.0 * rho); } /* Agarwal & Marrow 2010, 재인용: Xiang & Kurkoski 2012 */

static const double OPS[] = { 0.07, 0.15, 0.28, 0.50 };
static const struct { uint32_t h, s; const char *n; } WL[] = { {100, 100, "uniform"}, {80, 20, "80/20"}, {90, 10, "90/10"}, {95, 5, "95/5"} };

static void t1(void) {
    puts("### T1: WAF vs OP, policy, workload (3 seeds: mean [min-max]). 2 streams, AGE_CLOSE");
    for (size_t w = 0; w < 4; w++) {
        printf("\n%s:\n| OP | FIFO | greedy | cost-benefit R=1 | cost-benefit R=32 |", WL[w].n);
        if (w == 0) printf(" Eq.4 LRU | Eq.12 greedy | Eq.4(T-3) | Eq.12(T-3) | Agarwal |");
        puts("");
        for (size_t o = 0; o < 4; o++) {
            printf("| %2.0f%% |", OPS[o] * 100);
            struct { policy_t p; uint32_t ep; } P[4] = { {POL_FIFO, 32}, {POL_GREEDY, 32}, {POL_CB, 1}, {POL_CB, 32} };
            for (int pi = 0; pi < 4; pi++) {
                double sum = 0, lo = 1e9, hi = 0;
                for (uint64_t seed = 1; seed <= 3; seed++) {
                    cfg_t c = { P[pi].p, AGE_CLOSE, 2, P[pi].ep, WL[w].h, WL[w].s, OPS[o], seed * 1009u, false, 0, 0 };
                    double v = run(c).waf; sum += v; if (v < lo) lo = v; if (v > hi) hi = v;
                }
                printf(" %.3f [%.3f-%.3f] |", sum / 3, lo, hi);
            }
            if (w == 0) {
                /* T-3: 예약 2 블록 + 프런티어 2개가 평균 반씩 비어 있는 몫(약 1 블록)을 뺀 유효 용량 (계산에 쓴 근사) */
                double a = 1.0 + OPS[o], a2 = a * (MAX_BLK - 3.0) / MAX_BLK;
                printf(" %.3f | %.3f | %.3f | %.3f | %.3f |", desnoyers_lru(a), desnoyers_greedy(a, PG_PER_BLK),
                       desnoyers_lru(a2), desnoyers_greedy(a2, PG_PER_BLK), agarwal(OPS[o]));
            }
            puts(""); fflush(stdout);
        }
    }
}

static void t2(void) {
    puts("\n### T2: age definition and stream separation (OP 7%, 28%; seed 1009)");
    for (int op = 0; op < 2; op++) for (size_t w = 1; w < 3; w++) {
        double o = op ? 0.28 : 0.07;
        printf("\n%s OP %.0f%%:\n| config | WAF |\n", WL[w].n, o * 100);
        cfg_t base = { POL_GREEDY, AGE_CLOSE, 2, 32, WL[w].h, WL[w].s, o, 1009u, false, 0, 0 };
        cfg_t c = base; printf("| greedy 2-stream | %.3f |\n", run(c).waf);
        c = base; c.streams = 1; printf("| greedy 1-stream | %.3f |\n", run(c).waf);
        c = base; c.pol = POL_CB; c.epoch = 1; printf("| CB AGE_CLOSE 2-stream | %.3f |\n", run(c).waf);
        c.age = AGE_CARRY; printf("| CB AGE_CARRY 2-stream | %.3f |\n", run(c).waf);
        c.age = AGE_LAST_INV; printf("| CB AGE_LAST_INV 2-stream | %.3f |\n", run(c).waf);
        c.age = AGE_CLOSE; c.streams = 1; printf("| CB AGE_CLOSE 1-stream | %.3f |\n", run(c).waf);
        fflush(stdout);
    }
}

static void t3(void) {
    static const struct { uint32_t h, s; double op; const char *n; } C[] = {
        {90, 10, 0.07, "90/10 OP 7%"}, {90, 10, 0.28, "90/10 OP 28%"}, {80, 20, 0.07, "80/20 OP 7%"}, {100, 100, 0.07, "uniform OP 7%"} };
    static const uint32_t Rs[] = { 1, 4, 16, 32, 64, 256, 1024, 4096 };
    for (size_t k = 0; k < 4; k++) {
        printf("\n### T3: frozen-clock epoch length R (CB, AGE_CLOSE, %s, seed 1009; rebuilds and key evals are per whole run)\n", C[k].n);
        puts("| R (picks) | WAF | rebuilds | key evals/pick | miss % vs live clock (measure window) | mean regret of missed picks % |");
        for (size_t i = 0; i < 8; i++) {
            cfg_t c = { POL_CB, AGE_CLOSE, 2, Rs[i], C[k].h, C[k].s, C[k].op, 1009u, false, 0, 0 };
            res_t r = run(c);
            printf("| %4u | %.3f | %" PRIu64 " | %.1f | %.2f | %.2f |\n", Rs[i], r.waf, r.rebuilds,
                   r.all_picks ? (double)F.key_evals / (double)r.all_picks : 0.0, r.miss_pct, r.regret_pct);
            fflush(stdout);
        }
    }
}

static void t3b(void) {
    puts("\n### T3b: heap work on the invalidate path (1024 closed-block heap, measurement of whole run)");
    puts("| policy | workload | heap swaps per invalidate | max swaps in one refresh | invalidates per host write |");
    for (int p = 0; p < 2; p++) for (size_t w = 0; w < 3; w += 2) {
        cfg_t c = { p ? POL_CB : POL_GREEDY, AGE_CLOSE, 2, 32, WL[w].h, WL[w].s, 0.07, 1009u, false, 0, 0 };
        (void)run(c);
        printf("| %s | %s | %.3f | %u | %.3f |\n", p ? "CB R=32" : "greedy", WL[w].n,
               (double)F.heap_swaps / (double)F.invalidates, F.refresh_swaps_max, (double)F.invalidates / (double)F.host_clk);
    }
}

static void t4(void) {
    puts("\n### T4: paced GC watermarks vs WAF (greedy, uniform, OP 28%, seed 1009)");
    puts("| wm_start/wm_stop (free blocks) | WAF | avg free | max copies per host write |");
    static const uint32_t W[][2] = { {4, 6}, {8, 16}, {32, 64}, {64, 128}, {128, 256} };
    for (size_t i = 0; i < 5; i++) {
        cfg_t c = { POL_GREEDY, AGE_CLOSE, 2, 32, 100, 100, 0.28, 1009u, true, W[i][0], W[i][1] };
        res_t r = run(c);
        printf("| %u / %u | %.3f | %.1f | %u |\n", W[i][0], W[i][1], r.waf, r.avg_free, r.max_copies);
        fflush(stdout);
    }
    cfg_t c = { POL_GREEDY, AGE_CLOSE, 2, 32, 100, 100, 0.28, 1009u, false, 0, 0 };
    res_t r = run(c);
    printf("| (foreground only) | %.3f | %.1f | %u |\n", r.waf, r.avg_free, r.max_copies);
}

int main(int argc, char **argv) {
    const char *a = argc > 1 ? argv[1] : "all";
    if (!strcmp(a, "all") || !strcmp(a, "t1")) t1();
    if (!strcmp(a, "all") || !strcmp(a, "t2")) t2();
    if (!strcmp(a, "all") || !strcmp(a, "t3")) { t3(); t3b(); }
    if (!strcmp(a, "all") || !strcmp(a, "t4")) t4();
    return 0;
}
