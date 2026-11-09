/* ARCH-A04 추가 연습 — 한 번에 검증
 *
 * solution.c 의 시뮬레이터를 그대로 가져와(main 만 이름을 바꿔 포함) 4 개 연습의 답을 검사한다.
 *   연습 1  표준 패턴 a/b/b'/c 의 트랜잭션 수 공식 → k = 1..8, 라운드 1..6, 3 프로토콜 전부에서 시뮬레이터와 일치
 *   연습 2  false sharing 을 없애는 최소 간격(stride) 을 시뮬레이터로 찾기 → LINE_WORDS
 *   연습 3  MOESI 전용 돌연변이 2 개를 잡는 directed 트레이스 + "등가 돌연변이"(MESI 에선 절대 안 걸림)
 *   연습 4  브로드캐스트 스누프 vs 디렉터리: 꼭 필요한 메시지만 세면 패턴 a 는 코어 수와 무관
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr   (solution.c 와 같은 폴더에서)
 */
#define main solution_main
#include "solution.c"
#undef main

/* ---------- 연습 1: 손으로 유도한 공식 (엄격한 순환 c0, c1, ..., c(k-1), c0, ...) ---------- */
typedef struct { long rd, rdx, upgr, c2c, memrd, memwb, inval, hits; } expect_t;

static expect_t predict(proto_t p, pat_t pat, int k, int rounds) {
    long n = (long)k * rounds;
    expect_t e = {0, 0, 0, 0, 0, 0, 0, 0};
    switch (pat) {
    case PAT_A: case PAT_BFS:              /* 같은 라인을 모두가 쓴다: 첫 연산만 메모리, 나머지는 직전 소유자에게서 */
        if (k == 1) { e.rdx = 1; e.memrd = 1; e.hits = n - 1; break; }
        e.rdx = n; e.memrd = 1; e.c2c = n - 1; e.inval = n - 1;
        e.memwb = (p == PR_MOESI) ? 0 : n - 1;   /* MSI/MESI 의 Flush 는 메모리도 갱신, MOESI 는 직행만 */
        break;
    case PAT_B:                            /* 코어마다 자기 라인: 첫 접근만 미스 */
        e.rdx = k; e.memrd = k; e.hits = n - k;
        break;
    case PAT_C:                            /* 읽기만: 첫 읽기만 미스, 전부 메모리에서 (S·E 는 공급 안 함) */
        e.rd = k; e.memrd = k; e.hits = n - k;
        break;
    }
    return e;
}

static void ex1(void) {
    printf("== 연습 1: 패턴 공식 vs 시뮬레이터 ==\n");
    int cases = 0, ok = 0;
    for (int p = 0; p < 3; p++)
        for (int pat = 0; pat < 4; pat++)
            for (int k = 1; k <= (pat == PAT_BFS ? LINE_WORDS : MAX_CORE); k++)
                for (int R = 1; R <= 6; R++) {
                    sim_reset(S, (proto_t)p, MAX_CORE, 4, 1, 0);
                    gen_pattern(S, (pat_t)pat, k, R, true);
                    inv_t v = sim_run(S);
                    expect_t e = predict((proto_t)p, (pat_t)pat, k, R);
                    const stats_t *t = &S->st;
                    bool same = v == INV_OK && t->bus[BUS_RD] == e.rd && t->bus[BUS_RDX] == e.rdx &&
                                t->bus[BUS_UPGR] == e.upgr && t->c2c == e.c2c && t->memrd == e.memrd &&
                                t->memwb == e.memwb && t->inval == e.inval && t->hits == e.hits;
                    cases++; ok += same;
                    if (!same) printf("  mismatch %s %s k=%d R=%d\n", PR_NAME[p], PAT_NAME[pat], k, R);
                }
    printf("  %d/%d cases match\n", ok, cases);
    CHECK(ok == cases && cases == 3 * 6 * (8 + 8 + 4 + 8));
}

/* ---------- 연습 2: 최소 패딩 간격 ---------- */
static long run_stride(int k, int rounds, int stride) {
    sim_reset(S, PR_MESI, k, 4, 1, 0);
    for (int r = 0; r < rounds; r++)
        for (int c = 0; c < k; c++) { add_op(S, c, OP_A, (uint32_t)(c * stride), 1); add_sync(S); }
    if (sim_run(S) != INV_OK) return -1;
    return S->st.c2c;
}
static void ex2(void) {
    printf("== 연습 2: 코어별 카운터 간격(워드) vs cache-to-cache 전송 (k=4, 20 라운드) ==\n");
    int min_ok = -1;
    for (int stride = 1; stride <= 8; stride++) {
        long c2c = run_stride(4, 20, stride);
        printf("  stride %d words: c2c = %ld\n", stride, c2c);
        if (c2c == 0 && min_ok < 0) min_ok = stride;
    }
    CHECK(min_ok == LINE_WORDS);
    CHECK(run_stride(4, 20, 2) == 2 * (2 * 20 - 1));     /* 2 개씩 같은 라인: 라인마다 2 코어 핑퐁 */
}

/* ---------- 연습 3: MOESI 돌연변이용 directed 트레이스 ---------- */
static const char *const TR_O_NO_SUPPLY =               /* O 를 만든 뒤 제3 의 코어가 읽게 한다 */
    "c0 W 0 1\nsync\nc1 R 0\nsync\nc2 R 0\n";
static const char *const TR_O_EVICT =                   /* O 를 만든 뒤 같은 set 의 다른 라인으로 밀어낸다 */
    "c0 W 0 1\nsync\nc1 R 0\nsync\nc0 R 64\n";

static void ex3(void) {
    printf("== 연습 3: MOESI 전용 돌연변이 ==\n");
    CHECK(run_text(PR_MOESI, 4, 1, 0, TR_O_NO_SUPPLY) == INV_OK);
    inv_t a = run_text(PR_MOESI, 4, 1, MUT_O_NO_SUPPLY, TR_O_NO_SUPPLY);
    printf("  o_no_supply    : %s cycle %ld  %s\n", INV_NAME[a], S->viol_cycle, S->viol_msg);
    CHECK(a == INV_DATA && S->viol_cycle == 3);
    CHECK(run_text(PR_MOESI, 4, 1, 0, TR_O_EVICT) == INV_OK && S->st.bus[BUS_WB] == 1);
    inv_t b = run_text(PR_MOESI, 4, 1, MUT_O_EVICT_SILENT, TR_O_EVICT);
    printf("  o_evict_silent : %s cycle %ld  %s\n", INV_NAME[b], S->viol_cycle, S->viol_msg);
    CHECK(b == INV_MEM && S->viol_cycle == 3);
    /* 등가 돌연변이: MESI 에는 O 가 없으니 같은 버그가 절대 실행되지 않는다 → 랜덤으로도 안 걸린다 */
    long viol = 0;
    for (uint32_t seed = 1; seed <= 20; seed++) {
        sim_reset(S, PR_MESI, 4, 2, 1, MUT_O_NO_SUPPLY | MUT_O_EVICT_SILENT);
        gen_random(S, seed, 2000, 6);
        if (sim_run(S) != INV_OK) viol++;
    }
    printf("  same mutants on MESI, 20 random seeds: %ld violations (equivalent mutants)\n", viol);
    CHECK(viol == 0);
}

/* ---------- 연습 4: 브로드캐스트 vs 디렉터리 메시지 수 ---------- */
static void ex4(void) {
    printf("== 연습 4: 스누프 조회 수 vs 반응해야 했던 캐시 수 (10 라운드) ==\n");
    printf("  pattern               n  txns  broadcast-lookups  needed  needed/lookup\n");
    for (int pat = 0; pat < 4; pat += 3)                  /* a 와 c */
        for (int n = 2; n <= 8; n *= 2) {
            sim_reset(S, PR_MESI, n, 4, 1, 0);
            gen_pattern(S, (pat_t)pat, n, 10, true);
            CHECK(sim_run(S) == INV_OK);
            const stats_t *t = &S->st;
            long txns = t->bus[BUS_RD] + t->bus[BUS_RDX] + t->bus[BUS_UPGR];
            long lookups = txns * (n - 1);                /* 버스: 모든 트랜잭션을 나머지 n-1 개가 태그 조회 */
            long needed = t->snoop_act;                   /* 디렉터리: 상태가 바뀌거나 데이터를 낼 캐시에만 보냄 */
            printf("  %-20s %2d %5ld  %17ld  %6ld  %13.3f\n", PAT_NAME[pat], n, txns, lookups, needed,
                   (double)needed / (double)lookups);
            if (pat == PAT_A) CHECK(lookups == 10L * n * (n - 1) && needed == 10L * n - 1);   /* 연산당 1 (직전 소유자) */
            if (pat == PAT_C) CHECK(lookups == (long)n * (n - 1) && needed == 1);              /* 첫 E→S 한 번뿐 */
        }
}

int main(void) {
    S = calloc(1, sizeof *S);
    if (!S) return 2;
    ex1();
    ex2();
    ex3();
    ex4();
    free(S);
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
