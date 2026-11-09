/* STOR-M08 demo_bench — 은행 이체 스트레스로 비교: 희생자 정책 · 재시도 ts 유지 여부 · 접근 패턴 · 타임아웃 감지
 *
 * solution.c 의 lock manager 를 그대로 쓴다 (#include). 설정마다 1.2 초 동안 8 개 이체 스레드 + 감사 스레드 1 개.
 *   패턴 upgrade : S(from) S(to) → X(from) X(to)   (SELECT 후 UPDATE — 업그레이드 데드락이 잦다)
 *   패턴 x-direct: X(from) → X(to)                (SELECT … FOR UPDATE — 순서가 무작위라 ABBA 가능)
 *   패턴 ordered : X(min) → X(max)                (전역 순서 — 이체끼리는 순환 대기가 생길 수 없다)
 *   detect=0    : WFG 검사 끔. 시간 제한(tmo)이 지나면 abort. 그 순간 WFG 에 사이클이 있었는지(진짜) 없었는지(헛) 센다.
 * 숫자는 이 맥의 다른 작업과 경쟁하며 나온 것 — 비율만 믿을 것.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_bench.c -o demo_bench && ./demo_bench
 */
#define main stor_m08_selftest
#include "solution.c"
#undef main

enum { PAT_UPGRADE, PAT_XDIRECT, PAT_ORDERED };
enum { NACCT = 16, NW = 8 };
typedef struct { const char *name; vpolicy_t pol; bool keep_ts; int pat; bool detect; double tmo; } cfg_t;

static atomic_bool g_run;
static atomic_long g_tmo_real, g_tmo_false;

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* lm_lock 와 같되, 시간이 다하면 그 순간 WFG 에 나를 지나는 사이클이 있었는지 분류한다 */
static lmres_t lock_tmo(lockmgr_t *lm, int tx, int obj, lkmode_t mode, double tmo) {
    bool blocked_now;
    pthread_mutex_lock(&lm->mu);
    lmres_t r = acquire_locked(lm, tx, obj, mode, &blocked_now);
    if (r == LM_WAIT) {
        struct timespec dl;
        clock_gettime(CLOCK_REALTIME, &dl);
        long long ns = dl.tv_nsec + (long long)(tmo * 1e9);
        dl.tv_sec += (time_t)(ns / 1000000000LL); dl.tv_nsec = (long)(ns % 1000000000LL);
        while (lm->tx[tx].ws == W_WAITING) {
            int rc = pthread_cond_timedwait(&lm->tx[tx].cv, &lm->mu, &dl);
            if (rc == ETIMEDOUT && lm->tx[tx].ws == W_WAITING) {
                uint64_t adj[MAXTX]; int cyc[MAXTX], n;
                build_wfg(lm, adj);
                atomic_fetch_add(find_cycle(adj, tx, cyc, &n) ? &g_tmo_real : &g_tmo_false, 1);
                cancel_wait_locked(lm, tx);
                r = LM_TIMEOUT;
                break;
            }
        }
        if (r != LM_TIMEOUT) r = lm->tx[tx].ws == W_ABORTED ? LM_ABORTED : LM_GRANTED;
    }
    pthread_mutex_unlock(&lm->mu);
    return r;
}

typedef struct {
    db_t *db; const cfg_t *c; int id; uint32_t seed; bool audit; int64_t total;
    long commits, aborts, wasted, retries_ge5, bad; int max_retries;
} wk_t;

static lmres_t d_lock(wk_t *w, txn_t *t, int obj, lkmode_t m) {
    return w->c->detect ? lm_lock(w->db->lm, t->id, obj, m, 2.0) : lock_tmo(w->db->lm, t->id, obj, m, w->c->tmo);
}

/* 한 번 시도. 실패하면 되돌리고(wasted += 되돌린 쓰기 수) 결과를 돌려준다. */
static lmres_t attempt(wk_t *w, txn_t *t, int from, int to, int64_t amt) {
    db_t *db = w->db;
    int64_t a = 0, b = 0, v = 0, sum = 0;
    lmres_t r = LM_GRANTED;
    if (w->audit) {
        for (int k = 0; k < NACCT && r == LM_GRANTED; k++)
            if ((r = d_lock(w, t, k, LK_S)) == LM_GRANTED && (r = tx_read(db, t, k, &v)) == LM_GRANTED) sum += v;
        if (r == LM_GRANTED) { if (sum != w->total) w->bad++; tx_commit(db, t); return r; }
        goto fail;
    }
    if (w->c->pat == PAT_ORDERED) {                          /* 둘 다 잡은 뒤 쓴다 */
        int lo = from < to ? from : to, hi = from < to ? to : from;
        if ((r = d_lock(w, t, lo, LK_X)) != LM_GRANTED) goto fail;
        if ((r = d_lock(w, t, hi, LK_X)) != LM_GRANTED) goto fail;
        tx_read(db, t, from, &a); tx_read(db, t, to, &b);     /* 이미 보유 → 즉시 */
        if (a >= amt) { tx_write(db, t, from, a - amt); tx_write(db, t, to, b + amt); }
    } else {                                                 /* from 을 먼저 쓰고 나서 to 를 잡는다 → 되돌릴 일이 생김 */
        lkmode_t m = w->c->pat == PAT_UPGRADE ? LK_S : LK_X;
        if ((r = d_lock(w, t, from, m)) != LM_GRANTED || (r = tx_read(db, t, from, &a)) != LM_GRANTED) goto fail;
        if (w->c->pat == PAT_UPGRADE && (r = d_lock(w, t, to, LK_S)) != LM_GRANTED) goto fail;
        bool ok = a >= amt;
        if (ok && (r = d_lock(w, t, from, LK_X)) != LM_GRANTED) goto fail;
        if (ok) tx_write(db, t, from, a - amt);
        if ((r = d_lock(w, t, to, LK_X)) != LM_GRANTED) goto fail;
        tx_read(db, t, to, &b);
        if (ok) tx_write(db, t, to, b + amt);
    }
    tx_commit(db, t);
    return LM_GRANTED;
fail:
    w->wasted += t->nundo;
    tx_abort(db, t);
    return r;
}

static void *wk_main(void *arg) {
    wk_t *w = arg;
    txn_t t;
    while (atomic_load(&g_run)) {
        int from = (int)(xorshift32(&w->seed) % NACCT), to = (int)(xorshift32(&w->seed) % (NACCT - 1));
        if (to >= from) to++;
        int64_t amt = 1 + (int64_t)(xorshift32(&w->seed) % 10u);
        uint64_t ts = atomic_fetch_add(&g_ts, 1);
        for (int retry = 0;; retry++) {
            if (retry > 0 && !w->c->keep_ts) ts = atomic_fetch_add(&g_ts, 1);   /* 재시작 = 새 ts (나쁜 선택) */
            tx_begin(w->db, &t, w->id, ts);
            lmres_t r = attempt(w, &t, from, to, amt);
            if (r == LM_GRANTED) {
                w->commits++;
                if (retry > w->max_retries) w->max_retries = retry;
                if (retry >= 5) w->retries_ge5++;
                break;
            }
            w->aborts++;
            if (!atomic_load(&g_run)) break;
        }
    }
    return NULL;
}

int main(void) {
    static const cfg_t cfgs[] = {
        { "youngest  keep-ts upgrade ", V_YOUNGEST,     true,  PAT_UPGRADE, true,  0 },
        { "youngest  new-ts  upgrade ", V_YOUNGEST,     false, PAT_UPGRADE, true,  0 },
        { "fewest    keep-ts upgrade ", V_FEWEST_LOCKS, true,  PAT_UPGRADE, true,  0 },
        { "leastwork keep-ts upgrade ", V_LEAST_WORK,   true,  PAT_UPGRADE, true,  0 },
        { "youngest  keep-ts x-direct", V_YOUNGEST,     true,  PAT_XDIRECT, true,  0 },
        { "youngest  keep-ts ordered ", V_YOUNGEST,     true,  PAT_ORDERED, true,  0 },
        { "timeout 1ms       x-direct", V_YOUNGEST,     true,  PAT_XDIRECT, false, 0.001 },
        { "timeout 10ms      x-direct", V_YOUNGEST,     true,  PAT_XDIRECT, false, 0.010 },
        { "timeout 1ms       ordered ", V_YOUNGEST,     true,  PAT_ORDERED, false, 0.001 },
    };
    const double dur = 1.2;
    printf("%d transfer threads + 1 auditor, %d accounts, %.1f s per config\n", NW, NACCT, dur);
    printf("%-27s %9s %9s %8s %8s %7s %6s %7s %7s %9s\n", "config", "xfer/s", "deadlock", "abort/c", "waste/c",
           "maxret", ">=5", "audits", "aud-max", "tmo r/f");
    for (size_t c = 0; c < sizeof cfgs / sizeof cfgs[0]; c++) {
        static lockmgr_t lm;
        lm_init(&lm, cfgs[c].pol);
        lm.detect = cfgs[c].detect;
        db_t db = { .lm = &lm, .strict = true, .blocking = true, .timeout_s = 2.0 };
        int64_t total = 0;
        for (int a = 0; a < NACCT; a++) { db.val[a] = 1000; total += 1000; }
        atomic_store(&g_tmo_real, 0); atomic_store(&g_tmo_false, 0);
        wk_t w[NW + 1];
        pthread_t th[NW + 1];
        atomic_store(&g_run, true);
        for (int i = 0; i <= NW; i++) {
            w[i] = (wk_t){ .db = &db, .c = &cfgs[c], .id = i, .seed = 0x2545F491u * (uint32_t)(i + 1) | 1u,
                           .audit = i == NW, .total = total };
            pthread_create(&th[i], NULL, wk_main, &w[i]);
        }
        double t0 = now_s();
        struct timespec nap = { 0, (long)(dur * 1e9) % 1000000000L };
        nap.tv_sec = (time_t)dur;
        nanosleep(&nap, NULL);
        atomic_store(&g_run, false);
        long commits = 0, aborts = 0, wasted = 0, ge5 = 0, bad = 0; int maxr = 0;
        for (int i = 0; i <= NW; i++) {
            pthread_join(th[i], NULL);
            aborts += w[i].aborts; wasted += w[i].wasted; bad += w[i].bad;
            if (i < NW) { commits += w[i].commits; ge5 += w[i].retries_ge5; if (w[i].max_retries > maxr) maxr = w[i].max_retries; }
        }
        double el = now_s() - t0;
        int64_t sum = 0;
        for (int a = 0; a < NACCT; a++) sum += db.val[a];
        long allc = commits + w[NW].commits;
        printf("%-27s %9.0f %9ld %8.2f %8.3f %7d %6ld %7ld %7d %4ld/%-4ld%s\n", cfgs[c].name, (double)commits / el,
               lm.deadlocks, (double)aborts / (double)(allc ? allc : 1), (double)wasted / (double)(allc ? allc : 1), maxr, ge5,
               w[NW].commits, w[NW].max_retries, atomic_load(&g_tmo_real), atomic_load(&g_tmo_false),
               (sum != total || bad) ? "  INVARIANT BROKEN" : "");
        lm_destroy(&lm);
    }
    return 0;
}
