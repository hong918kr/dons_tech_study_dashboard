/* OS-A08 추가 연습 — 한 번에 검증
 *   연습 1  TRIM: 지운 LBA 를 클리너에게 알리면 WA 가 얼마나 내려가나 + TRIM 을 기록 안 하면 재구성 때 되살아난다
 *   연습 2  클리너 예약(reserve): 0 이면 공간 교착 → 쓰기가 -1 (멈추지 않고, 옛 데이터는 그대로), 1 이면 충분
 *   연습 3  호스트 스트림 힌트 (NVMe Streams/FDP 식): hot/cold 를 다른 헤드에 쓰면 greedy 도 좋아진다
 *   연습 4  victim 사용률 분포: cost-benefit 은 두 봉우리 (hot 은 낮은 u, cold 는 높은 u), greedy 는 한 봉우리
 *   연습 5  LFS 논문의 write cost = (읽기 + 쓰기) / 새 데이터 = 2/(1-ū) 를 측정값으로 확인
 * 엔진은 solution.c 를 그대로 쓴다 (LFS_NO_MAIN). 같은 폴더에서 빌드:
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#define LFS_NO_MAIN
#include "solution.c"

static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

/* ---------------- 연습 1: TRIM ---------------- */
static int lfs_trim(lfs_t *L, uint32_t lba) {
    if (lba >= L->c.nlba || L->map[lba] == NONE) return -1;
    L->live[seg_of(L, L->map[lba])]--;          /* 클리너가 더는 옮기지 않는다 */
    L->map[lba] = NONE;
    L->ver[lba]++;                               /* 다음 쓰기의 버전이 옛 summary 보다 크게 */
    return 0;
}

/* 앞 70 % LBA 만 균등 랜덤으로 덮어쓰고, 뒤 30 % 는 prefill 뒤 손대지 않는다 (trim=true 면 그 30 % 를 TRIM). */
static int count_resurrected(const lfs_t *L) {
    uint32_t *m = malloc(L->c.nlba * sizeof *m), *lv = malloc(L->c.nseg * sizeof *lv);
    int n = 0;
    if (m && lv && lfs_rebuild_map(L, m, lv) >= 0)
        for (uint32_t b = 0; b < L->c.nlba; b++) n += (m[b] != NONE && L->map[b] == NONE);
    free(m); free(lv);
    return n;
}

/* resurrected[0] = TRIM 직후 재구성에서 되살아난 수, [1] = 워크로드가 끝난 뒤 */
static double trim_run(bool trim, int resurrected[2], double *mean_free) {
    lfs_cfg_t c = { 256, 64, (uint32_t)(0.85 * 256 * 64), POL_GREEDY, 2, 3, 1, 1, true, false };
    lfs_t L;
    if (lfs_init(&L, &c) != 0) return -1;
    uint32_t hot = c.nlba * 7 / 10, rng = 21;
    for (uint32_t b = 0; b < c.nlba; b++) (void)lfs_write(&L, b);
    if (trim) for (uint32_t b = hot; b < c.nlba; b++) (void)lfs_trim(&L, b);
    resurrected[0] = count_resurrected(&L);
    double fs = 0;
    uint64_t n = 12ull * hot;
    for (uint64_t k = 0; k < n; k++) {
        if (k == 4ull * hot) { memset(&L.st, 0, sizeof L.st); fs = 0; }
        (void)lfs_write(&L, xs32(&rng) % hot);
        fs += L.nfree;
    }
    *mean_free = fs / (double)(8ull * hot);
    double w = stats_wa(&L.st);
    resurrected[1] = count_resurrected(&L);
    lfs_free(&L);
    return w;
}

static void ex1(void) {
    printf("-- ex1 TRIM\n");
    int res0[2], res1[2];
    double f0, f1;
    double w0 = trim_run(false, res0, &f0), w1 = trim_run(true, res1, &f1);
    uint32_t ntrim = (uint32_t)(0.85 * 256 * 64) - (uint32_t)(0.85 * 256 * 64) * 7 / 10;
    /* 모델: TRIM 없으면 뒤 30 % 는 꽉 찬 세그먼트로 굳어 있고, 앞 70 % 가 남은 공간을 쓴다. TRIM 하면 전체 공간을 쓴다. */
    double U = 0.85 * 256 * 64, hot = (double)((uint32_t)U * 7 / 10);
    double T0 = (256 - f0 - 1.0) * 64 - (U - hot), T1 = (256 - f1 - 1.0) * 64;
    double m0 = model_greedy(T0 / hot, 64), m1 = model_greedy(T1 / hot, 64);
    printf("  no TRIM: WA %.3f (model %.3f)   TRIM 30%%: WA %.3f (model %.3f)\n", w0, m0, w1, m1);
    printf("  summary-scan rebuild resurrects trimmed lbas: right after TRIM %d of %u, after the workload %d\n",
           res1[0], ntrim, res1[1]);
    CHECK(w1 < w0 * 0.8);
    CHECK(fabs(w0 - m0) <= 0.05 * m0 && fabs(w1 - m1) <= 0.05 * m1);
    CHECK(res0[0] == 0 && res0[1] == 0 && res1[0] == (int)ntrim);   /* TRIM 이 로그에 없으면 스캔이 되살린다 */
}

/* ---------------- 연습 2: 예약과 교착 ---------------- */
/* wm_low 는 reserve 보다 커야 한다 (아니면 호스트가 막혔는데 청소가 시작되지 않는다) → wm = reserve+1 / +2 */
static void reserve_run(uint32_t reserve, int *first_fail, int *bad) {
    lfs_cfg_t c = { 16, 8, (uint32_t)(0.75 * 16 * 8), POL_GREEDY, reserve + 1, reserve + 2, reserve, 1, true, false };
    lfs_t L;
    *first_fail = -1; *bad = 0;
    if (lfs_init(&L, &c) != 0) { *bad = 1; return; }
    uint32_t rng = 3, ref[96] = { 0 };
    for (uint32_t b = 0; b < c.nlba; b++) if (lfs_write(&L, b) == 0) ref[b]++;
    for (int k = 0; k < 20000; k++) {
        uint32_t b = xs32(&rng) % c.nlba;
        if (lfs_write(&L, b) == 0) ref[b]++;
        else if (*first_fail < 0) *first_fail = k;
    }
    uint64_t v;
    for (uint32_t b = 0; b < c.nlba; b++) *bad += lfs_read(&L, b, &v) != 0 || v != stamp(b, ref[b]);
    *bad += lfs_check(&L) != 0;
    lfs_free(&L);
}
static void ex2(void) {
    printf("-- ex2 cleaner reserve\n");
    int f0, b0, f1, b1;
    reserve_run(0, &f0, &b0);
    reserve_run(1, &f1, &b1);
    printf("  reserve 0: first failed write at #%d (data intact: %s)   reserve 1: first failure %d\n",
           f0, b0 ? "no" : "yes", f1);
    CHECK(f0 >= 0 && b0 == 0);          /* 교착은 오류로 드러나고, 실패한 쓰기는 아무것도 망가뜨리지 않는다 */
    CHECK(f1 == -1 && b1 == 0);
}

/* ---------------- 연습 3: 호스트 스트림 힌트 ---------------- */
static int write_hinted(lfs_t *L, uint32_t *cold_head, uint32_t lba, bool hot) {
    if (hot) return lfs_write(L, lba);
    uint32_t keep = L->hhead;
    L->hhead = *cold_head;                       /* cold 스트림 헤드를 잠깐 끼워 넣는다 */
    int r = lfs_write(L, lba);
    *cold_head = L->hhead;
    L->hhead = keep;
    return r;
}
static double stream_run(int pol, bool hint) {
    lfs_cfg_t c = { 256, 64, (uint32_t)(0.80 * 256 * 64), pol, 3, 4, 1, 1, true, false };
    lfs_t L;
    if (lfs_init(&L, &c) != 0) return -1;
    uint32_t rng = 5, cold = NONE, hotn = c.nlba / 10;
    int fails = 0;
    for (uint32_t b = 0; b < c.nlba; b++) fails += write_hinted(&L, &cold, b, !hint || b < hotn) != 0;
    for (uint64_t k = 0; k < 12ull * c.nlba; k++) {
        if (k == 4ull * c.nlba) memset(&L.st, 0, sizeof L.st);
        uint32_t b = next_lba(WL_HOTCOLD, c.nlba, &rng, k);
        fails += write_hinted(&L, &cold, b, !hint || b < hotn) != 0;
    }
    double w = fails || lfs_check(&L) ? -1 : stats_wa(&L.st);
    lfs_free(&L);
    return w;
}
static void ex3(void) {
    printf("-- ex3 host stream hint (hot/cold heads)\n");
    double g0 = stream_run(POL_GREEDY, false), g1 = stream_run(POL_GREEDY, true);
    double c0 = stream_run(POL_COSTBEN, false), c1 = stream_run(POL_COSTBEN, true);
    printf("  greedy: 1 stream %.3f -> 2 streams %.3f   cost-benefit: %.3f -> %.3f\n", g0, g1, c0, c1);
    CHECK(g0 > 0 && g1 > 0 && c0 > 0 && c1 > 0);
    CHECK(g1 < 0.97 * g0);                      /* greedy 는 힌트로 좋아진다 */
    CHECK(fabs(c1 - c0) <= 0.03 * c0);          /* cost-benefit 은 나이로 이미 거의 같은 정보를 얻고 있었다 */
}

/* ---------------- 연습 4: victim 사용률 분포 ---------------- */
static void ex4(void) {
    printf("-- ex4 bimodal victim utilization\n");
    lfs_cfg_t c = { 256, 64, (uint32_t)(0.75 * 256 * 64), POL_GREEDY, 2, 3, 1, 4, true, true };
    run_t g, b;
    CHECK(run_sim(&c, WL_HOTCOLD, 5, 4, 8, &g) == 0);
    c.policy = POL_COSTBEN;
    CHECK(run_sim(&c, WL_HOTCOLD, 5, 4, 8, &b) == 0);
    double gl = 0, gh = 0, bl = 0, bh = 0, gmax = 0;
    for (int i = 0; i < 10; i++) {
        double pg = (double)g.st.hist[i] / g.st.segs_cleaned, pb = (double)b.st.hist[i] / b.st.segs_cleaned;
        if (i < 3) { gl += pg; bl += pb; }
        if (i >= 6) { gh += pg; bh += pb; }
        if (pg > gmax) gmax = pg;
    }
    printf("  greedy: u<0.3 %.1f%%  u>=0.6 %.1f%%  top bucket %.1f%%  mean u %.3f | cost-benefit: u<0.3 %.1f%%  u>=0.6 %.1f%%  mean u %.3f\n",
           100 * gl, 100 * gh, 100 * gmax, g.mean_victim_u, 100 * bl, 100 * bh, b.mean_victim_u);
    CHECK(gmax > 0.8);                               /* greedy: 거의 한 구간 */
    CHECK(bl > 0.3 && bh > 0.2);                     /* cost-benefit: 두 봉우리 */
    CHECK(b.mean_victim_u < g.mean_victim_u);        /* 그래도 평균은 더 낮다 → WA 가 낮다 */
}

/* ---------------- 연습 5: LFS 논문의 write cost ---------------- */
static void ex5(void) {
    printf("-- ex5 LFS write cost = 2/(1-u)\n");
    const double us[] = { 0.60, 0.80 };
    for (int i = 0; i < 2; i++) {
        lfs_cfg_t c = { 256, 64, (uint32_t)(us[i] * 256 * 64), POL_GREEDY, 2, 3, 1, 1, true, false };
        run_t r;
        CHECK(run_sim(&c, WL_UNIFORM, 7, 4, 8, &r) == 0);
        double wc = (double)(r.st.segs_cleaned * c.S + r.st.clean_w + r.st.host_w) / (double)r.st.host_w;
        double ub = r.mean_victim_u, f = 2.0 / (1.0 - ub);
        printf("  disk u %.2f: victim u %.3f  write cost %.3f  2/(1-u) %.3f  (WA %.3f = 1/(1-u) %.3f)\n",
               us[i], ub, wc, f, r.wa, 1.0 / (1.0 - ub));
        CHECK(fabs(wc - f) <= 0.01 * f);
    }
}

int main(void) {
    ex1();
    ex2();
    ex3();
    ex4();
    ex5();
    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
