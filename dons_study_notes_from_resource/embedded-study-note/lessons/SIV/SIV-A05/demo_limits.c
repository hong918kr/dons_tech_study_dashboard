/* demo_limits.c — solution.c 를 포함해 체크 없이 측정 표만 찍는다.
 *   cc -std=c11 -Wall -Wextra -Werror -O2 demo_limits.c -o /tmp/SIV-A05_demo && /tmp/SIV-A05_demo
 * 1) 칩×errata 판정 매트릭스  2) WA on 만 보면 왜 안 되는가  3) 간헐 버그 놓침 확률  4) 부트 로그 */
#define main solution_main
#include "solution.c"
#undef main

static void print_matrix(void) {
    static const int ALL[] = { -40, 25, 105 };
    audit_opts_t o = { ALL, 3, 1024, 0x51u, F_ALL };
    const chip_model_t *const ms[] = { &M_A0, &M_A1, &M_B0, &M_B0_ECO, &M_B0_BRANCH };
    printf("[1] audit matrix (corners -40/25/105C, 1024 trials each)\n");
    printf("%-7s", "");
    for (size_t j = 0; j < sizeof ms / sizeof ms[0]; j++) printf("%-14s", ms[j]->name);
    printf("\n");
    for (int i = 0; i < TABLE_N; i++) {
        printf("E%-6u", (unsigned)TABLE[i].id);
        for (size_t j = 0; j < sizeof ms / sizeof ms[0]; j++) {
            audit_t a;
            audit_one(TABLE, TABLE_N, &TABLE[i], ms[j], &o, &a);
            char cell[20];
            snprintf(cell, sizeof cell, "%s%s", V_NAME[a.v], a.masked ? "*" : "");
            printf("%-14s", cell);
        }
        printf("\n");
    }
    printf("(* = WA 가 트리거를 없애서 on-probe 는 exercised=0)\n");
}

static void print_on_only(void) {
    static const int ALL[] = { -40, 25, 105 };
    audit_opts_t o = { ALL, 3, 1024, 0x51u, F_ALL };
    const erratum_t *e = tbl_find(TABLE, TABLE_N, 1004);
    const chip_model_t *const ms[] = { &M_A0, &M_B0, &M_B0_ECO };
    printf("\n[2] E1004 probe: WA on vs WA off\n");
    printf("%-8s %-22s %-22s %s\n", "chip", "WA on (ex/fail)", "WA off (ex/fail)", "verdict");
    for (size_t j = 0; j < sizeof ms / sizeof ms[0]; j++) {
        audit_t a;
        audit_one(TABLE, TABLE_N, e, ms[j], &o, &a);
        char on[32], off[32];
        snprintf(on, sizeof on, "%d/%d", a.on_ex, a.on_fail);
        snprintf(off, sizeof off, "%d/%d", a.off_ex, a.off_fail);
        printf("%-8s %-22s %-22s %s\n", ms[j]->name, on, off, V_NAME[a.v]);
    }
}

static void print_miss(void) {
    printf("\n[3] intermittent bug: P(0 failures in N trials) measured over 5000 seeds (WA off)\n");
    printf("%-8s %-8s %-12s %-12s\n", "1/p", "N", "measured", "(1-p)^N");
    static const uint32_t dens[] = { 64, 1024 };
    static const int ns[] = { 1, 16, 64, 256, 1024, 4096 };
    for (size_t d = 0; d < sizeof dens / sizeof dens[0]; d++)
        for (size_t k = 0; k < sizeof ns / sizeof ns[0]; k++) {
            int miss = 0;
            for (int i = 0; i < 5000; i++) {
                soc_t s;
                soc_init(&s, model_raw(&M_A0), F_ALL, M_A0.bugs, 25, 0x7000u + (uint32_t)i);
                s.l2_den = dens[d];
                miss += probe_l2arb(&s, ns[k]).failures == 0;
            }
            double q = 1.0;
            for (int i = 0; i < ns[k]; i++) q *= 1.0 - 1.0 / dens[d];
            printf("%-8u %-8d %-12.4f %-12.4f\n", (unsigned)dens[d], ns[k], miss / 5000.0, q);
        }
}

static void print_boots(void) {
    printf("\n[4] boot logs\n");
    static const struct { const char *tag; unsigned maj, min; policy_t pol; } b[] = {
        { "A0/HALT", 0, 0, POLICY_HALT }, { "A1/HALT", 0, 1, POLICY_HALT }, { "B0/HALT", 1, 0, POLICY_HALT },
        { "C0/HALT", 2, 0, POLICY_HALT }, { "C0/LAB ", 2, 0, POLICY_LAB },  { "A2/LAB ", 0, 2, POLICY_LAB },
    };
    for (size_t i = 0; i < sizeof b / sizeof b[0]; i++) {
        soc_t s;
        errata_ctx_t c;
        char line[160];
        soc_init(&s, chip_raw_id(b[i].maj, b[i].min, 0x11), F_ALL, 0, 25, 1);
        int rc = errata_boot(TABLE, TABLE_N, &s, KNOWN, KNOWN_N, b[i].pol, &c);
        ctx_format(line, sizeof line, &c);
        printf("%s rc=%-3d clk_fast=%d %s\n", b[i].tag, rc, s.clk_fast, line);
    }
}

int main(void) {
    (void)solution_main;
    print_matrix();
    print_on_only();
    print_miss();
    print_boots();
    return 0;
}
