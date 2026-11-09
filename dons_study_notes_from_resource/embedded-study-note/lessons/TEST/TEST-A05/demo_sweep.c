/* demo_sweep.c — solution.c 코어를 그대로 include 해서 큰 sweep을 돌린다.
 * 벽시계는 이 demo에만 있다 (시뮬 코어에는 없다).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_sweep.c -o demo_sweep
 */
#define _POSIX_C_SOURCE 200809L
#define main solution_main
#include "solution.c"
#undef main
#include <time.h>

static double wall_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* 여러 seed의 trace hash를 다시 해시한 값: 빌드가 달라도 같아야 한다 */
static uint64_t digest(uint32_t fixes, uint32_t n)
{
    uint64_t d = FNV_BASIS;
    for (uint32_t i = 1; i <= n; i++) d = hash_u64(d, sim_run_seed(i, fixes).hash);
    return d;
}

static void table(uint32_t n)
{
    static const struct { const char *name; uint32_t fixes; } rows[] = {
        { "none                ", FW_F_NONE },
        { "CLAIM               ", FW_F_CLAIM },
        { "CLAIM+MATCH         ", FW_F_CLAIM | FW_F_MATCH_ID },
        { "CLAIM+MATCH+STOP    ", FW_F_ALL },
    };
    printf("fixes                 runs   fails  double   stale    hang  first_seed  first_viol\n");
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        sweep_t s;
        sweep(1, n, rows[i].fixes, &s);
        printf("%s %6u %7u %7u %7u %7u %11llu  %s\n", rows[i].name, s.runs, s.fails,
               s.by_viol[V_DOUBLE], s.by_viol[V_STALE], s.by_viol[V_HANG],
               s.found ? (unsigned long long)s.first_seed : 0ull,
               s.found ? VIOL_NAME[s.first_viol] : "-");
    }
}

/* 요청마다 check/commit 표식의 순서를 모은다 (RX·타이머 둘 다 표식을 남긴 요청만). */
typedef struct { char pat[6]; uint32_t n; } pat_t;

static void tally(pat_t *p, int *np, const char *s)
{
    for (int i = 0; i < *np; i++)
        if (strcmp(p[i].pat, s) == 0) { p[i].n++; return; }
    if (*np < 12) { strcpy(p[*np].pat, s); p[*np].n = 1; (*np)++; }
}

static void orderings(const char *title, uint32_t n)
{
    pat_t pats[12];
    int np = 0;
    uint32_t contested = 0;
    for (uint32_t seed = 1; seed <= n; seed++) {
        (void)sim_run_seed(seed, FW_F_NONE);
        char seqs[MAX_REQ][6];
        memset(seqs, 0, sizeof seqs);
        uint32_t cnt = g.n_trace < MAX_TRACE ? g.n_trace : MAX_TRACE;
        for (uint32_t i = 0; i < cnt; i++) {
            int k = g_trace[i].kind;
            uint32_t id = g_trace[i].arg;
            if (k < TR_MARK0 || k >= TR_VIOL || id >= MAX_REQ) continue;
            size_t len = strlen(seqs[id]);
            if (len < 5) seqs[id][len] = "RrTt"[k - TR_MARK0];
        }
        for (int id = 0; id < MAX_REQ; id++)
            if (strchr(seqs[id], 'R') && strchr(seqs[id], 'T')) { contested++; tally(pats, &np, seqs[id]); }
    }
    printf("%s: contested=%u, distinct orderings=%d\n", title, contested, np);
    for (int i = 0; i < np; i++) printf("   %-6s %u\n", pats[i].pat, pats[i].n);
}

int main(void)
{
    uint32_t n = 20000;
    printf("digest(none, 2000 seeds) = %016llx\n", (unsigned long long)digest(FW_F_NONE, 2000));
    printf("digest(all,  2000 seeds) = %016llx\n", (unsigned long long)digest(FW_F_ALL, 2000));

    puts("\n[1] seed sweep by fix set");
    table(n);

    puts("\n[2] virtual vs wall");
    uint64_t virt = 0;
    double t0 = wall_sec();
    for (uint32_t i = 1; i <= n; i++) virt += sim_run_seed(i, FW_F_ALL).end_us;
    double el = wall_sec() - t0;
    printf("%u seeds, virtual %.3f s, wall %.3f s, speedup %.0fx, %.2f us/seed\n",
           n, (double)virt * 1e-6, el, (double)virt * 1e-6 / el, el * 1e6 / n);

    puts("\n[3] blind spot: window_max=0 (no execution-time model, buggy fw)");
    g_cfg.window_max = 0;
    sweep_t s;
    sweep(1, n, FW_F_NONE, &s);
    printf("window_max=0: runs=%u fails=%u double=%u stale=%u hang=%u\n",
           s.runs, s.fails, s.by_viol[V_DOUBLE], s.by_viol[V_STALE], s.by_viol[V_HANG]);
    g_cfg.window_max = 4;

    puts("\n[4] orderings the single-core model can reach (buggy fw)");
    orderings("TMR > RX  ", n);
    g_cfg.prio_rx = 2; g_cfg.prio_tmr = 1;
    orderings("RX  > TMR ", n);
    g_cfg.prio_rx = 1; g_cfg.prio_tmr = 2;
    return 0;
}
