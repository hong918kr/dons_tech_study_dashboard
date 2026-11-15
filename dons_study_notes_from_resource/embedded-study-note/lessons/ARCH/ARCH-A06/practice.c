/* ARCH-A06 추가 연습 — 한 번에 검증
 *
 * solution.c 의 시뮬레이터를 그대로 가져와(main 만 이름을 바꿔 포함) 4 개 연습의 답을 검사한다.
 *   연습 1  Little 의 법칙: 평균 ROB 점유 = IPC × 평균 체류(commit − issue) 가 정확히 성립 + 최소 ROB 크기
 *   연습 2  손으로 타임라인 예측 (WAR 이 있는 짧은 시퀀스) → Tomasulo · 스코어보드 사이클
 *   연습 3  flush 로 버려지는 일: 예외(= 분기 미스 예측과 같은 복구 경로)를 commit 에서 받을 때의 비용
 *   연습 4  메모리 의존: store-to-load forwarding 과 "ST 주소를 늦게 아는" 이 모델의 보수성
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr   (solution.c 와 같은 폴더에서)
 */
#define main solution_main
#include "solution.c"
#undef main

static insn_t P[MAXPROG];

/* ---------- 연습 1: Little 의 법칙 ---------- */
static void ex1(void) {
    printf("== 연습 1: Little 의 법칙 (점유 = IPC x 체류) 와 최소 ROB ==\n");
    arch_t init; arch_init(&init, NULL);
    bool exact = true;
    for (int k = 0; k < NK; k++) {
        int n = make_kernel((kern_t)k, P);
        for (int rob = 4; rob <= 64; rob *= 2) {
            cfg_t c = cfg_default(); c.rob = rob; for (int f = 0; f < NFU; f++) c.nrs[f] = 16;
            sim_init(&g_sim, &c, P, n, &init);
            int cy = sim_run(&g_sim);
            long resid = 0;
            for (int i = 0; i < n; i++) resid += g_sim.tl[i].cmt - g_sim.tl[i].iss;
            if (resid != g_sim.st.occ_sum) exact = false;     /* Σ체류 == Σ(사이클마다 점유) — 항등식 */
            if (rob == 4 || rob == 64)
                printf("  %-26s ROB=%2d  IPC=%.3f  avg occ=%.2f  avg resid=%.2f  IPC*resid=%.2f\n", K_NAME[k], rob,
                       (double)n / cy, (double)g_sim.st.occ_sum / cy, (double)resid / n,
                       (double)n / cy * ((double)resid / n));
        }
    }
    CHECK(exact);
    /* 최소 ROB: IPC 가 ROB=64 와 같아지는 가장 작은 크기 */
    int minrob[NK];
    for (int k = 0; k < NK; k++) {
        double full = ipc_of((kern_t)k, 64, 16, true, NULL);
        minrob[k] = 0;
        for (int r = 1; r <= 64 && !minrob[k]; r++)
            if (ipc_of((kern_t)k, r, 16, true, NULL) >= full - 1e-12) minrob[k] = r;
        printf("  min ROB for full IPC: %-26s %d\n", K_NAME[k], minrob[k]);
    }
    CHECK(minrob[K_INDEP] == 9);          /* 체류 8 (issue t → commit t+8) + 해제 슬롯은 다음 사이클 → 9 */
    CHECK(minrob[K_CHAIN] <= 3);
    CHECK(minrob[K_DOT] > minrob[K_INDEP]);
}

/* ---------- 연습 2: 손 예측 ---------- */
static void ex2(void) {
    printf("== 연습 2: 손으로 타임라인 예측 ==\n");
    arch_t init; arch_init(&init, NULL);
    int n = assemble("MUL r1, r2, r3\nADD r2, r4, r5\nADD r6, r1, r2\nST r6, 0(r0)\n", P, MAXPROG);
    cfg_t c = cfg_default();
    sim_init(&g_sim, &c, P, n, &init); int cy = sim_run(&g_sim);
    print_timeline(&g_sim);
    const int want[4][5] = {{1, 2, 7, 8, 9}, {2, 3, 4, 5, 10}, {3, 9, 10, 11, 12}, {4, 12, 12, 13, 14}};
    bool ok = true;
    for (int i = 0; i < n; i++) {
        const tl_t *x = &g_sim.tl[i];
        if (x->iss != want[i][0] || x->xs != want[i][1] || x->xe != want[i][2] || x->wr != want[i][3] ||
            x->cmt != want[i][4]) ok = false;
    }
    int sbc = sb_run(&g_sb, &c, P, n, &init);
    print_sb(&g_sb);
    printf("  Tomasulo %d cycles, scoreboard %d cycles, mem[0]=%u\n", cy, sbc, g_sim.a.mem[0]);
    CHECK(ok && cy == 14);
    CHECK(sbc == 16);
    CHECK(g_sim.a.mem[0] == 20u * 30u + 40u + 50u && g_sb.a.mem[0] == g_sim.a.mem[0]);  /* r1*... : MUL 은 옛 r2 */
}

/* ---------- 연습 3: flush 비용 ---------- */
static void ex3(void) {
    printf("== 연습 3: 예외를 commit 에서 받을 때 버려지는 명령 수 ==\n");
    arch_t init; arch_init(&init, NULL);
    int n = 0;
    P[n++] = (insn_t){OP_DIV, 1, 2, 0, 0};                       /* DIV0, x.s=2 에서 이미 안다 */
    for (int i = 0; i < 40; i++) P[n++] = (insn_t){OP_ADDI, (uint8_t)(3 + i % 4), 7, 0, i};
    bool ok = true;
    printf("  ROB  flushed  detect(x.s)  taken  delay\n");
    for (int rob = 4; rob <= 32; rob *= 2) {
        cfg_t c = cfg_default(); c.rob = rob; c.nrs[FU_ADD] = 16;   /* RS 는 넉넉히: ROB 효과만 본다 */
        sim_init(&g_sim, &c, P, n, &init); sim_run(&g_sim);
        int det = g_sim.tl[0].xs, taken = g_sim.cyc;
        printf("  %3d  %7ld  %11d  %5d  %5d\n", rob, g_sim.st.flushed, det, taken, taken - det);
        int expect = rob - 1 < 13 ? rob - 1 : 13;                 /* issue 는 2..14 사이클에 최대 13 개 */
        if (g_sim.st.flushed != expect || taken != 15 || g_sim.exc_idx != 0) ok = false;
    }
    CHECK(ok);
}

/* ---------- 연습 4: 메모리 의존 ---------- */
static void ex4(void) {
    printf("== 연습 4: store-to-load forwarding 과 보수적 메모리 순서 ==\n");
    arch_t init; arch_init(&init, NULL);
    init.r[2] = 100; init.r[3] = 7;
    cfg_t c = cfg_default();
    int n = assemble("DIV r1, r2, r3\nST r1, 3(r0)\nLD r4, 3(r0)\nLD r5, 4(r0)\n", P, MAXPROG);
    sim_init(&g_sim, &c, P, n, &init); sim_run(&g_sim);
    print_timeline(&g_sim);
    int ld_same = g_sim.tl[2].xs, ld_other = g_sim.tl[3].xs, st_done = g_sim.tl[1].wr;
    CHECK(g_sim.a.r[4] == 14 && g_sim.a.r[5] == 1004 && ld_same > st_done && ld_other > st_done);
    /* ST 의 데이터가 일찍 준비되면 (r6) 두 LD 모두 일찍 간다 */
    n = assemble("DIV r1, r2, r3\nST r6, 3(r0)\nLD r4, 3(r0)\nLD r5, 4(r0)\n", P, MAXPROG);
    sim_init(&g_sim2, &c, P, n, &init); sim_run(&g_sim2);
    printf("  ST data from DIV : LD(3) x.s=%d, LD(4) x.s=%d | ST data ready: LD(3) x.s=%d, LD(4) x.s=%d\n",
           ld_same, ld_other, g_sim2.tl[2].xs, g_sim2.tl[3].xs);
    CHECK(g_sim2.a.r[4] == 60 && g_sim2.tl[2].xs < 10 && g_sim2.tl[3].xs < 10);
}

int main(void) {
    ex1(); ex2(); ex3(); ex4();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
