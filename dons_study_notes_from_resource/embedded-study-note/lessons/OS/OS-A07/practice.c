/* OS-A07 추가 연습 — 한 번에 검증 (엔진 = 같은 폴더의 solution.c, 쓰기 캐시 창만 6 으로)
 *   연습 1  async commit(barrier 없음 + checksum)은 data 모드에선 안전, ordered 모드에선 옛 데이터를 노출한다
 *           → TxE 전에 데이터만 flush(dflush) 하면 고쳐진다. (ext4 는 이 조합의 마운트를 거부한다)
 *   연습 2  그룹 커밋: create K 개를 한 트랜잭션으로 → 전원 차단 어디서든 0 개 또는 K 개, 쓰기 수 감소
 *   연습 3  복구 도중 크래시: 모든 (N1, N2) 쌍 → 다시 마운트하면 fsck 0, 상태 old-or-new (replay 는 멱등)
 *   연습 4  revoke 문제: ordered + lazy checkpoint 에서 해제된 indirect 블록을 데이터로 재사용 → replay 가 덮어씀
 *           → "체크포인트 안 된 저널에 옛 버전이 있는 블록은 재할당 금지" 로 고친다
 *   연습 5  복구 비용: fsck 읽기 수는 파일 수에 비례, 저널 replay 읽기 수는 로그 크기에 비례
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr   (solution.c 와 같은 폴더에서)
 */
#define A07_NO_MAIN
#define A07_WC_MAX 6
#include "solution.c"

static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static void setmode(int mode, int barrier, int csum, int lazy, int wc) {
    J.mode = mode; J.barrier = barrier; J.csum = csum; J.lazy = lazy; D.wc_on = wc; J.dflush = 0; J.veto = NULL;
}
static int fsck_total(void) { fsck_t r; return fsck(&r); }

/* ---- 연습 1 ---- */
static void ex1(void) {
    printf("-- ex1: window %d, NO barrier + csum (async commit)\n", WC_MAX);
    int bad_data_mode = 0, bad_ord = 0, bad_ord_fixed = 0, meta_ord = 0;
    for (int k = 0; k < 2; k++) {
        sweep_t r;
        setmode(M_DATA, 0, 1, 0, 1); sweep(&WL[k], &r);
        bad_data_mode += r.fsck_bad + r.meta_bad + r.data_bad;
        printf("  data     %-17s runs=%4d data_bad=%d crc-rejected=%ld\n", WL[k].name, r.runs, r.data_bad, r.crc_bad);
        setmode(M_ORDERED, 0, 1, 0, 1); sweep(&WL[k], &r);
        bad_ord += r.data_bad; meta_ord += r.fsck_bad + r.meta_bad;
        printf("  ordered  %-17s runs=%4d data_bad=%d (N=%d mask=0x%x first)\n", WL[k].name, r.runs, r.data_bad,
               r.nbad ? r.bad_n[0] : -1, r.nbad ? r.bad_mask[0] : 0);
        setmode(M_ORDERED, 0, 1, 0, 1); J.dflush = 1; sweep(&WL[k], &r);
        bad_ord_fixed += r.fsck_bad + r.meta_bad + r.data_bad;
        printf("  ordered+dflush %-11s runs=%4d data_bad=%d\n", WL[k].name, r.runs, r.data_bad);
    }
    setmode(M_ORDERED, 1, 1, 0, 0);
    CHECK(bad_data_mode == 0);          /* 데이터도 checksum 안에 있으니 안전 */
    CHECK(meta_ord == 0 && bad_ord > 0);/* 메타데이터는 안전, ordered 데이터 약속은 깨진다 */
    CHECK(bad_ord_fixed == 0);          /* 커밋 전에 데이터만 내리면 된다 */
}

/* ---- 연습 2 ---- */
enum { K = 4 };
static void group_ops(void) {
    char nm[16];
    op_enter();
    for (int i = 0; i < K; i++) { snprintf(nm, sizeof nm, "/g%d", i); create_node(nm, T_FILE); }
    op_exit(0);
}
static int count_g(void) {
    int n = 0; char nm[16];
    for (int i = 0; i < K; i++) { snprintf(nm, sizeof nm, "/g%d", i); n += fs_namei(nm, NULL) >= 0; }
    return n;
}
static void ex2(void) {
    printf("-- ex2: group commit of %d creates\n", K);
    setmode(M_ORDERED, 1, 1, 0, 0);
    dev_init(); fs_mkfs(); fs_mount(); fs_sync();
    memcpy(SNAP, D.media, sizeof SNAP);
    restore_and_mount();
    group_ops();
    long W = D.nwrite, tx = J.commits;
    restore_and_mount();
    char nm[16];
    for (int i = 0; i < K; i++) { snprintf(nm, sizeof nm, "/g%d", i); fs_create(nm); }
    long Wsep = D.nwrite;
    printf("  writes: grouped %ld (1 tx) vs one-by-one %ld (%d tx)\n", W, Wsep, K);
    int runs = 0, bad = 0, seen0 = 0, seenK = 0;
    for (long N = 0; N <= W; N++) {
        restore_and_mount();
        D.crash_at = N;
        group_ops();
        power_cut(); dev_reboot(); fs_mount();
        int c = count_g();
        runs++;
        if ((c != 0 && c != K) || fsck_total() != 0) bad++;
        seen0 += c == 0; seenK += c == K;
    }
    printf("  crash sweep: runs=%d, all-or-nothing violations=%d (0 files: %d runs, %d files: %d runs)\n",
           runs, bad, seen0, K, seenK);
    CHECK(tx >= 1 && W < Wsep);
    CHECK(runs == W + 1 && bad == 0 && seen0 > 0 && seenK > 0);
}

/* ---- 연습 3 ---- */
static void ex3(void) {
    printf("-- ex3: crash during recovery (ordered, lazy checkpoint)\n");
    int runs = 0, bad = 0;
    for (int w = 0; w < 2; w++) {
        setmode(M_ORDERED, 1, 1, 1, 0);
        long W = clean_run(&WL[w]);
        int wr = 0, wb = 0;
        for (long N1 = 0; N1 <= W; N1++) {
            restore_and_mount(); D.crash_at = N1;
            run_ops(WL[w].ops, WL[w].nops); power_cut(); dev_reboot();
            io_reset(); fs_mount();
            long R = D.nwrite;                                     /* 복구가 하는 쓰기 수 */
            for (long N2 = 0; N2 < R; N2++) {
                restore_and_mount(); D.crash_at = N1;
                int j = run_ops(WL[w].ops, WL[w].nops); power_cut(); dev_reboot();
                io_reset(); D.crash_at = N2; fs_mount(); power_cut(); dev_reboot();  /* 복구 중 전원 차단 */
                fs_mount();                                                         /* 두 번째 복구 */
                uint64_t m, f;
                int ok = fsck_total() == 0 && digest(&m, &f) == 0;
                int hit = 0;
                for (int k = j - 1; ok && k <= (j > WL[w].nops ? WL[w].nops : j); k++) hit |= ST_FULL[k] == f;
                wr++; runs++;
                if (!ok || !hit) { bad++; wb++; }
            }
        }
        printf("  %-17s W=%ld  (N1,N2) pairs=%d  bad=%d\n", WL[w].name, W, wr, wb);
    }
    setmode(M_ORDERED, 1, 1, 0, 0);
    CHECK(runs > 50 && bad == 0);
}

/* ---- 연습 4 ---- */
static const workload_t REUSE_IND = { "reuse-indirect", 2, { { OP_CREATE, "/p", 0, 0, 0, 0 }, { OP_WRITE, "/p", 0, 0, 1024, 81 } }, 4,
    { { OP_WRITE, "/p", 0, 6144, 1024, 82 }, { OP_UNLINK, "/p", 0, 0, 0, 0 }, { OP_CREATE, "/c", 0, 0, 0, 0 },
      { OP_WRITE, "/c", 0, 0, 2048, 83 } } };
static int veto_pending(uint32_t b) {   /* 체크포인트 안 된 커밋(또는 열린 트랜잭션)에 메타데이터 버전이 있다 */
    return find(J.ck_blk, J.nck, b) >= 0 || find(J.tx_blk, J.ntx, b) >= 0;
}
static void ex4(void) {
    printf("-- ex4: freed indirect block reused as data (ordered)\n");
    sweep_t r0, r1, r2, r3;
    setmode(M_ORDERED, 1, 1, 0, 0); sweep(&REUSE_IND, &r0);
    setmode(M_ORDERED, 1, 1, 1, 0); sweep(&REUSE_IND, &r1);
    setmode(M_ORDERED, 1, 1, 1, 0); J.veto = veto_pending; sweep(&REUSE_IND, &r2);
    setmode(M_DATA, 1, 1, 1, 0); sweep(&REUSE_IND, &r3);
    printf("  ordered, ckpt each     : runs=%d fsck_bad=%d data_bad=%d\n", r0.runs, r0.fsck_bad, r0.data_bad);
    printf("  ordered, lazy          : runs=%d fsck_bad=%d data_bad=%d  N=", r1.runs, r1.fsck_bad, r1.data_bad);
    for (int i = 0; i < r1.nbad; i++) printf("%s%d", i ? "," : "", r1.bad_n[i]);
    printf("\n  ordered, lazy + veto   : runs=%d fsck_bad=%d data_bad=%d\n", r2.runs, r2.fsck_bad, r2.data_bad);
    printf("  data,    lazy          : runs=%d fsck_bad=%d data_bad=%d\n", r3.runs, r3.fsck_bad, r3.data_bad);
    /* 첫 위반을 재현해서 /c 의 두 번째 블록이 무엇으로 덮였는지 본다 */
    uint32_t w[4] = { 0 };
    if (r1.nbad) {
        setmode(M_ORDERED, 1, 1, 1, 0);
        prepare(&REUSE_IND); restore_and_mount(); D.crash_at = r1.bad_n[0];
        run_ops(REUSE_IND.ops, REUSE_IND.nops); power_cut(); dev_reboot(); fs_mount();
        uint8_t got[2048];
        int ci = fs_namei("/c", NULL);
        if (ci > 0 && fs_read((uint32_t)ci, 0, got, sizeof got) == 2048) memcpy(w, got + 1024, sizeof w);
        printf("  N=%d: /c bytes 1024..1039 as u32 = %u %u %u %u (an indirect block: entry0 = block number)\n",
               r1.bad_n[0], w[0], w[1], w[2], w[3]);
    }
    setmode(M_ORDERED, 1, 1, 0, 0);
    CHECK(r0.data_bad == 0 && r0.fsck_bad == 0);
    CHECK(r1.data_bad > 0 && r1.fsck_bad == 0 && blk_ok(w[0]) && w[1] == 0);
    CHECK(r2.data_bad == 0 && r2.fsck_bad == 0 && r3.data_bad == 0);
}

/* ---- 연습 5 ---- */
static void ex5(void) {
    printf("-- ex5: recovery cost — fsck reads vs journal replay reads\n");
    static uint8_t buf[7 * 1024];
    long fr[2], rr[2];
    const int nfiles[2] = { 4, 16 };
    for (int t = 0; t < 2; t++) {
        setmode(M_ORDERED, 1, 1, 0, 0);
        dev_init(); fs_mkfs(); fs_mount();
        char nm[16];
        for (int i = 0; i < nfiles[t]; i++) {
            snprintf(nm, sizeof nm, "/f%d", i);
            int fi = fs_create(nm);
            pattern(buf, sizeof buf, (uint32_t)i + 1);
            fs_write((uint32_t)fi, 0, buf, sizeof buf);           /* 7 KB → indirect 블록 하나씩 */
        }
        J.lazy = 1; fs_create("/last");                           /* 커밋만 되고 체크포인트 전 */
        power_cut(); dev_reboot();
        io_reset(); fs_mount(); rr[t] = D.nread;
        io_reset(); int e = fsck_total(); fr[t] = D.nread;
        printf("  %2d files: replay reads=%ld (replayed %s), fsck reads=%ld, fsck findings=%d\n",
               nfiles[t], rr[t], fs_namei("/last", NULL) > 0 ? "/last" : "nothing", fr[t], e);
    }
    setmode(M_ORDERED, 1, 1, 0, 0);
    CHECK(rr[1] <= rr[0] + 2 && fr[1] > fr[0] && fr[0] > 5 * rr[0]);   /* replay ≈ 상수, fsck 는 커진다 */
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
