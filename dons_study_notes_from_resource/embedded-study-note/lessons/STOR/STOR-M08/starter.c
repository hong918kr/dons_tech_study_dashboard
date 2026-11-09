/* STOR-M08 동시성 제어: 2PL & 데드락 감지
 *
 * 작은 lock manager + strict 2PL 트랜잭션 + wait-for 그래프 데드락 감지.
 *   Part A  S/X 호환성 행렬, 객체마다 FIFO 요청 큐, S→X 업그레이드(업그레이드 대기자가 우선),
 *           strict 2PL 트랜잭션(읽기 = S, 쓰기 = X, 쓰기 전 값을 undo 로그에, 해제는 commit/abort 에서만).
 *   Part B  큐에서 wait-for 그래프(WFG) 만들기, "막히는 순간" 사이클 검사, 희생자 선택(3가지 정책),
 *           스크립트로 한 걸음씩 돌리는 트랜잭션: T1 X(A)→X(B) / T2 X(B)→X(A) → 희생자 정확히 1명,
 *           업그레이드 데드락, non-strict 2PL 의 dirty read · 커밋된 쓰기 소실(강제 스케줄).
 *   Part C  은행 이체 스트레스: 계좌 N 개, 스레드 M 개, xorshift 시드, 감사(audit) 트랜잭션 —
 *           총액 불변식이 정확히 성립, commit+abort 수가 맞음, 멈춤 없음(모든 대기에 시간 제한).
 *
 * 두 가지 구동 방식, 같은 lock manager:
 *   step 모드   lm_acquire() 는 막지 않는다 (GRANTED / WAIT / ABORTED). 하네스가 트랜잭션을 한 걸음씩 돌린다.
 *   thread 모드 lm_lock() 은 트랜잭션별 condvar 에서 pthread_cond_timedwait 로 기다린다 (시간 제한 → LM_TIMEOUT).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

#define MAXTX   32          /* 트랜잭션 슬롯 = WFG 노드. 한 행 = uint64_t 비트마스크 */
#define MAXOBJ  64          /* 잠글 수 있는 객체(계좌, 페이지, 메타데이터 엔트리 …) */
#define MAXUNDO 64

typedef enum { LK_NONE = 0, LK_S = 1, LK_X = 2 } lkmode_t;
typedef enum { LM_GRANTED = 0, LM_WAIT = 1, LM_ABORTED = 2, LM_TIMEOUT = 3, LM_ERROR = 4 } lmres_t;
typedef enum { W_IDLE = 0, W_WAITING = 1, W_ABORTED = 2 } wstate_t;
typedef enum { V_YOUNGEST = 0, V_FEWEST_LOCKS = 1, V_LEAST_WORK = 2 } vpolicy_t;

/* 큐 엔트리 하나 = (트랜잭션, 객체). held = 부여된 모드, want = 기다리는 모드.
 *   held=S/X want=NONE : 보유자     held=NONE want=S/X : 일반 대기자     held=S want=X : 업그레이드 대기자 */
typedef struct { int tx; lkmode_t held; lkmode_t want; } lreq_t;
typedef struct { lreq_t q[MAXTX]; int n; } lqueue_t;     /* 도착 순서대로 (FIFO) */

typedef struct {
    uint64_t ts;            /* 시작 타임스탬프 — 작을수록 오래됨. 재시도해도 그대로 */
    wstate_t ws;
    int wait_obj;           /* 기다리는 객체 (-1 = 없음) */
    int nlocks;             /* 보유한 락 수 (V_FEWEST_LOCKS) */
    int work;               /* 지금까지 쓴 횟수 = 되돌릴 일의 양 (V_LEAST_WORK) */
    pthread_cond_t cv;
} txslot_t;

typedef struct {
    pthread_mutex_t mu;     /* lock table 자체를 지키는 내부 래치 (latch) — 2PL 의 "락" 과 다르다 */
    lqueue_t obj[MAXOBJ];
    txslot_t tx[MAXTX];
    vpolicy_t policy;
    bool detect;            /* false = WFG 검사 끔 (타임아웃만으로 해결하는 데모용) */
    bool verbose;           /* 사이클을 찾을 때마다 출력 */
    long deadlocks;         /* 찾은 사이클 수 = 고른 희생자 수 */
    int last_cycle[MAXTX], last_n, last_victim;
} lockmgr_t;

static const char *mode_name(lkmode_t m) { return m == LK_X ? "X" : m == LK_S ? "S" : "-"; }
static char obj_name(int obj) { return (char)(obj < 26 ? 'A' + obj : '#'); }

static void lm_init(lockmgr_t *lm, vpolicy_t policy) {
    memset(lm, 0, sizeof *lm);
    pthread_mutex_init(&lm->mu, NULL);
    for (int i = 0; i < MAXTX; i++) { pthread_cond_init(&lm->tx[i].cv, NULL); lm->tx[i].wait_obj = -1; }
    lm->policy = policy;
    lm->detect = true;
    lm->last_victim = -1;
}
static void lm_destroy(lockmgr_t *lm) {
    for (int i = 0; i < MAXTX; i++) pthread_cond_destroy(&lm->tx[i].cv);
    pthread_mutex_destroy(&lm->mu);
}
static void lm_begin(lockmgr_t *lm, int tx, uint64_t ts) {
    pthread_mutex_lock(&lm->mu);
    txslot_t *t = &lm->tx[tx];
    t->ts = ts; t->ws = W_IDLE; t->wait_obj = -1; t->nlocks = 0; t->work = 0;
    pthread_mutex_unlock(&lm->mu);
}
static void lm_add_work(lockmgr_t *lm, int tx) {
    pthread_mutex_lock(&lm->mu); lm->tx[tx].work++; pthread_mutex_unlock(&lm->mu);
}

/* ======================= Part A — 호환성, 큐, 부여 ======================= */

/* A1: 이미 부여된 held 와 새로 원하는 want 가 같이 있을 수 있나? (NONE 은 아무것도 안 막는다) */
static bool lk_compat(lkmode_t held, lkmode_t want) {
    (void)held; (void)want;
    return false; /* TODO A1 */
}

static lreq_t *q_find(lqueue_t *q, int tx) {
    for (int i = 0; i < q->n; i++) if (q->q[i].tx == tx) return &q->q[i];
    return NULL;
}
static void q_remove(lqueue_t *q, lreq_t *e) {
    int i = (int)(e - q->q);
    memmove(&q->q[i], &q->q[i + 1], (size_t)(q->n - i - 1) * sizeof q->q[0]);
    q->n--;
}
static int holders_except(const lqueue_t *q, int tx) {
    int n = 0;
    for (int i = 0; i < q->n; i++) if (q->q[i].tx != tx && q->q[i].held != LK_NONE) n++;
    return n;
}
static bool has_waiter(const lqueue_t *q) {
    for (int i = 0; i < q->n; i++) if (q->q[i].want != LK_NONE) return true;
    return false;
}
static bool compat_with_holders(const lqueue_t *q, int tx, lkmode_t want) {
    for (int i = 0; i < q->n; i++)
        if (q->q[i].tx != tx && !lk_compat(q->q[i].held, want)) return false;
    return true;
}
static void wake(lockmgr_t *lm, int tx) {
    lm->tx[tx].ws = W_IDLE; lm->tx[tx].wait_obj = -1;
    pthread_cond_signal(&lm->tx[tx].cv);
}

/* A2: 락이 풀렸거나 대기가 취소된 뒤, 이 객체에서 부여할 수 있는 요청을 부여한다.
 *   1) 업그레이드 대기자(held=S, want=X)가 유일한 보유자가 되었으면 X 로 올린다.
 *      업그레이드 대기자가 남아 있으면 일반 대기자는 아무도 못 지나간다 (업그레이드가 굶지 않게).
 *   2) 일반 대기자를 도착 순서대로: 보유자 전부와 호환되면 부여, 처음으로 안 되는 곳에서 멈춤 (FIFO, 새치기 없음). */
static void grant_waiters(lockmgr_t *lm, int obj) {
    (void)lm; (void)obj; (void)wake;
    /* TODO A2 */
}

/* A3: 요청 하나. 막지 않는다. *blocked_now = 이번 호출로 새로 대기를 시작했는가 (그때만 사이클 검사).
 *   이미 같은/더 강한 모드 보유 → GRANTED. 아직 기다리는 중(step 모드 재폴링) → WAIT.
 *   S 보유 + X 요청 = 업그레이드: 유일한 보유자면 즉시 X, 아니면 want=X 로 대기.
 *   새 요청: 대기자가 하나도 없고 보유자 전부와 호환될 때만 즉시 부여 (FIFO). 아니면 큐 끝에서 대기. */
static lmres_t acquire_locked(lockmgr_t *lm, int tx, int obj, lkmode_t mode, bool *blocked_now) {
    (void)lm; (void)tx; (void)obj; (void)mode; (void)holders_except; (void)compat_with_holders;
    *blocked_now = false;
    return LM_ERROR; /* TODO A3 */
}

static void release_locked(lockmgr_t *lm, int tx, int obj) {
    lqueue_t *q = &lm->obj[obj];
    lreq_t *e = q_find(q, tx);
    if (!e) return;
    if (e->held != LK_NONE) lm->tx[tx].nlocks--;
    q_remove(q, e);
    grant_waiters(lm, obj);
}
static void lm_release(lockmgr_t *lm, int tx, int obj) {
    pthread_mutex_lock(&lm->mu); release_locked(lm, tx, obj); pthread_mutex_unlock(&lm->mu);
}
static void lm_release_all(lockmgr_t *lm, int tx) {
    pthread_mutex_lock(&lm->mu);
    for (int o = 0; o < MAXOBJ; o++) release_locked(lm, tx, o);
    pthread_mutex_unlock(&lm->mu);
}

/* 대기 취소 (희생자 또는 타임아웃): 일반 대기 엔트리는 지우고, 업그레이드는 want 만 지운다 (S 는 계속 보유 —
 * 그 S 는 abort 의 release_all 이 푼다). 앞을 막던 엔트리가 사라졌으니 grant_waiters. */
static void cancel_wait_locked(lockmgr_t *lm, int tx) {
    txslot_t *t = &lm->tx[tx];
    int obj = t->wait_obj;
    if (obj >= 0) {
        lreq_t *e = q_find(&lm->obj[obj], tx);
        if (e) { if (e->held == LK_NONE) q_remove(&lm->obj[obj], e); else e->want = LK_NONE; }
    }
    t->ws = W_ABORTED; t->wait_obj = -1;
    pthread_cond_signal(&t->cv);
    if (obj >= 0) grant_waiters(lm, obj);
}

/* ======================= Part B — wait-for 그래프, 사이클, 희생자 ======================= */

/* B1: 큐에서 WFG 만들기. adj[a] 의 비트 b = "a 가 b 를 기다린다".
 *   대기 엔트리 w 는 다음 엔트리 h (h.tx != w.tx) 를 기다린다:
 *     - h 가 보유자이고 h.held 가 w.want 와 비호환
 *     - w 가 일반 대기자일 때: h 가 업그레이드 대기자 (항상 먼저 부여됨)
 *     - w 가 일반 대기자일 때: h 가 w 보다 앞에 선 일반 대기자이고 h.want 가 w.want 와 비호환 (FIFO) */
static void build_wfg(const lockmgr_t *lm, uint64_t adj[MAXTX]) {
    memset(adj, 0, MAXTX * sizeof adj[0]);
    (void)lm; /* TODO B1 */
}

static bool dfs_cycle(const uint64_t adj[MAXTX], int u, int start, uint64_t *seen, int *path, int *n) {
    (void)adj; (void)u; (void)start; (void)seen; (void)path; (void)n;
    return false; /* TODO B2 */
}
/* B2: start 를 지나는 사이클이 있으면 true, cyc[0..n-1] = start → … → (다시 start).
 * 사이클 검사는 "막히는 순간" 에 막힌 트랜잭션에서만 한다: 그 전까지 그래프에 사이클이 없었고, 새로 생긴 간선은
 * 전부 start 에서 나가므로, 새 사이클이 있다면 반드시 start 를 지난다. 노드를 한 번씩만 방문 → O(V+E). */
static bool find_cycle(const uint64_t adj[MAXTX], int start, int cyc[MAXTX], int *n) {
    uint64_t seen = 1ull << start;
    *n = 0;
    return dfs_cycle(adj, start, start, &seen, cyc, n);
}

/* B3: 사이클 구성원 중 희생자 하나. 동률이면 가장 젊은(ts 가 큰) 쪽.
 *   V_YOUNGEST     : ts 최대 (가장 늦게 시작) — 재시도 때 ts 를 유지하면 결국 가장 늙어서 굶지 않는다
 *   V_FEWEST_LOCKS : 보유 락 수 최소 (풀었을 때 깨어날 사람이 적다)
 *   V_LEAST_WORK   : 쓴 횟수 최소 (되돌릴 일이 적다) */
static int pick_victim(const lockmgr_t *lm, const int *cyc, int n) {
    (void)lm; (void)n;
    return cyc[0]; /* TODO B3 */
}

static void print_cycle(const lockmgr_t *lm, const int *cyc, int n, int victim) {
    printf("  DEADLOCK:");
    for (int k = 0; k < n; k++) {
        int a = cyc[k], b = cyc[(k + 1) % n], o = lm->tx[a].wait_obj;
        const lreq_t *e = q_find((lqueue_t *)&lm->obj[o], a);
        printf(" T%d wants %s(%c) held by T%d%s", a, mode_name(e ? e->want : LK_NONE), obj_name(o), b,
               k + 1 < n ? " ->" : "");
    }
    static const char *pn[] = { "youngest", "fewest-locks", "least-work" };
    printf("  => victim T%d (ts=%llu, %s)\n", victim, (unsigned long long)lm->tx[victim].ts, pn[lm->policy]);
}

/* 막혔을 때: 사이클이 남아 있는 동안 희생자를 하나씩 고른다. 요청자 자신이 희생자면 LM_ABORTED. */
static lmres_t detect_locked(lockmgr_t *lm, int tx) {
    if (!lm->detect) return LM_WAIT;
    uint64_t adj[MAXTX];
    int cyc[MAXTX], n;
    for (;;) {
        build_wfg(lm, adj);
        if (!find_cycle(adj, tx, cyc, &n)) return LM_WAIT;
        int v = pick_victim(lm, cyc, n);
        lm->deadlocks++;
        memcpy(lm->last_cycle, cyc, sizeof cyc[0] * (size_t)n);
        lm->last_n = n; lm->last_victim = v;
        if (lm->verbose) print_cycle(lm, cyc, n, v);
        cancel_wait_locked(lm, v);
        if (v == tx) return LM_ABORTED;
    }
}

/* step 모드: 막지 않는 요청 / 재폴링 */
static lmres_t lm_acquire(lockmgr_t *lm, int tx, int obj, lkmode_t mode) {
    bool blocked_now;
    pthread_mutex_lock(&lm->mu);
    lmres_t r = acquire_locked(lm, tx, obj, mode, &blocked_now);
    if (blocked_now) r = detect_locked(lm, tx);
    pthread_mutex_unlock(&lm->mu);
    return r;
}

/* thread 모드: 부여되거나 희생자로 뽑히거나 시간이 다할 때까지 기다린다. 맥에는 pthread_condattr_setclock 이
 * 없으므로 시한은 CLOCK_REALTIME 기준 (pthread_cond_timedwait 의 기본 시계). */
static lmres_t lm_lock(lockmgr_t *lm, int tx, int obj, lkmode_t mode, double timeout_s) {
    bool blocked_now;
    pthread_mutex_lock(&lm->mu);
    lmres_t r = acquire_locked(lm, tx, obj, mode, &blocked_now);
    if (blocked_now) r = detect_locked(lm, tx);
    if (r == LM_WAIT) {
        struct timespec dl;
        clock_gettime(CLOCK_REALTIME, &dl);
        long long ns = dl.tv_nsec + (long long)(timeout_s * 1e9);
        dl.tv_sec += (time_t)(ns / 1000000000LL); dl.tv_nsec = (long)(ns % 1000000000LL);
        while (lm->tx[tx].ws == W_WAITING) {
            int rc = pthread_cond_timedwait(&lm->tx[tx].cv, &lm->mu, &dl);
            if (rc == ETIMEDOUT && lm->tx[tx].ws == W_WAITING) { cancel_wait_locked(lm, tx); r = LM_TIMEOUT; break; }
        }
        if (r != LM_TIMEOUT) r = lm->tx[tx].ws == W_ABORTED ? LM_ABORTED : LM_GRANTED;
    }
    pthread_mutex_unlock(&lm->mu);
    return r;
}

/* ======================= 트랜잭션 층: strict 2PL + undo 로그 ======================= */
typedef struct { int obj; int64_t old; } undo_t;
typedef struct {
    int id; uint64_t ts; int retries;
    bool shrinking;                 /* non-strict 에서 하나라도 풀었으면 true → 이후 획득 금지 (2PL 규칙) */
    undo_t undo[MAXUNDO]; int nundo;
} txn_t;
typedef struct {
    lockmgr_t *lm;
    int64_t val[MAXOBJ];            /* "데이터베이스" — 2PL 락으로만 보호된다 */
    bool strict;                    /* true = 해제는 commit/abort 에서만 */
    bool blocking;                  /* true = lm_lock (스레드), false = lm_acquire (step) */
    double timeout_s;
} db_t;

static void tx_begin(db_t *db, txn_t *t, int id, uint64_t ts) {
    t->id = id; t->ts = ts; t->shrinking = false; t->nundo = 0;
    lm_begin(db->lm, id, ts);
}
static lmres_t tx_lock(db_t *db, txn_t *t, int obj, lkmode_t mode) {
    if (t->shrinking) return LM_ERROR;                    /* 2PL: 줄어드는 단계에서는 새로 못 잡는다 */
    return db->blocking ? lm_lock(db->lm, t->id, obj, mode, db->timeout_s)
                        : lm_acquire(db->lm, t->id, obj, mode);
}
static lmres_t tx_read(db_t *db, txn_t *t, int obj, int64_t *out) {
    lmres_t r = tx_lock(db, t, obj, LK_S);
    if (r == LM_GRANTED) *out = db->val[obj];
    return r;
}
static lmres_t tx_write(db_t *db, txn_t *t, int obj, int64_t v) {
    if (t->nundo == MAXUNDO) return LM_ERROR;
    lmres_t r = tx_lock(db, t, obj, LK_X);
    if (r != LM_GRANTED) return r;
    t->undo[t->nundo++] = (undo_t){ obj, db->val[obj] };  /* before-image 먼저, 그다음 쓰기 (WAL 순서) */
    db->val[obj] = v;
    lm_add_work(db->lm, t->id);
    return r;
}
static lmres_t tx_add(db_t *db, txn_t *t, int obj, int64_t delta) {
    lmres_t r = tx_lock(db, t, obj, LK_X);
    if (r != LM_GRANTED) return r;
    return tx_write(db, t, obj, db->val[obj] + delta);
}
static void tx_commit(db_t *db, txn_t *t) { lm_release_all(db->lm, t->id); }

/* A4: abort = undo 로그를 거꾸로 적용(가장 나중 쓰기부터) → 그다음에 락을 푼다.
 * 순서가 반대면 다른 트랜잭션이 되돌리기 전의 값(커밋 안 된 값)을 읽는다. */
static void tx_abort(db_t *db, txn_t *t) {
    (void)db; (void)t;
    /* TODO A4 */
}
/* non-strict 2PL 의 이른 해제. strict 이면 거절. */
static bool tx_unlock(db_t *db, txn_t *t, int obj) {
    if (db->strict) return false;
    t->shrinking = true;
    lm_release(db->lm, t->id, obj);
    return true;
}

/* ======================= Part C — 은행 이체 ======================= */

/* C1: from → to 로 amt 이체 하나 (한 번 시도). 읽기(S) 두 번 → 잔액 부족이면 아무것도 안 쓰고 commit →
 * 쓰기(X 로 업그레이드) 두 번 → commit. 중간에 GRANTED 가 아니면 tx_abort 하고 그 결과를 돌려준다. */
static lmres_t transfer_once(db_t *db, txn_t *t, int from, int to, int64_t amt) {
    (void)db; (void)t; (void)from; (void)to; (void)amt;
    return LM_ERROR; /* TODO C1 */
}

/* ======================= 하네스 ======================= */

static void lm_dump(lockmgr_t *lm) {                 /* 멈췄을 때 큐 상태 = 순환 대기를 눈으로 */
    pthread_mutex_lock(&lm->mu);
    for (int o = 0; o < MAXOBJ; o++) {
        const lqueue_t *q = &lm->obj[o];
        if (!has_waiter(q)) continue;
        printf("  obj %2d:", o);
        for (int i = 0; i < q->n; i++) {
            const lreq_t *e = &q->q[i];
            if (e->want == LK_NONE) printf(" T%d[%s]", e->tx, mode_name(e->held));
            else if (e->held == LK_NONE) printf(" T%d[wait %s]", e->tx, mode_name(e->want));
            else printf(" T%d[%s, wait %s]", e->tx, mode_name(e->held), mode_name(e->want));
        }
        printf("\n");
    }
    pthread_mutex_unlock(&lm->mu);
}
static bool lm_quiet(lockmgr_t *lm) {               /* 모든 큐가 비었고 보유 락 0 */
    bool ok = true;
    pthread_mutex_lock(&lm->mu);
    for (int o = 0; o < MAXOBJ; o++) if (lm->obj[o].n) ok = false;
    for (int i = 0; i < MAXTX; i++) if (lm->tx[i].nlocks || lm->tx[i].ws == W_WAITING) ok = false;
    pthread_mutex_unlock(&lm->mu);
    return ok;
}

/* --- step 모드 스크립트 실행기 --- */
typedef struct { char op; int obj; int64_t arg; } sop_t;   /* R 읽기, A 더하기(X), U 이른 해제, C commit, B 사용자 abort */
typedef struct {
    const sop_t *ops; int nops; int pc; bool done;
    txn_t t; int64_t seen[8]; int nseen; int victimized;
    int last_pc; lmres_t last_r;                           /* 같은 대기를 trace 에 반복해 찍지 않게 */
} stx_t;

static void tr_add(char *tr, size_t cap, const char *s) {
    size_t n = strlen(tr), m = strlen(s);
    if (n + m + 4 < cap) memcpy(tr + n, s, m + 1);
    else if (n + 4 <= cap && (n < 3 || strcmp(tr + n - 3, "...") != 0)) memcpy(tr + n, "...", 4);
}
/* 한 걸음: tx 의 다음 연산 하나 (WAIT 이면 같은 연산을 다음 차례에 다시 시도). */
static void step_tx(db_t *db, stx_t *x, char *tr, size_t cap) {
    if (x->done) return;
    const sop_t *op = &x->ops[x->pc];
    char buf[96];
    lmres_t r = LM_GRANTED;
    int64_t v = 0;
    switch (op->op) {
    case 'R': r = tx_read(db, &x->t, op->obj, &v); break;
    case 'A': r = tx_add(db, &x->t, op->obj, op->arg); break;
    case 'U': if (!tx_unlock(db, &x->t, op->obj)) r = LM_ERROR; break;
    case 'C': tx_commit(db, &x->t); x->done = true; break;
    case 'B': tx_abort(db, &x->t); x->done = true; break;
    }
    static const char *rn[] = { "ok", "wait", "DEADLOCK-victim", "timeout", "error" };
    bool repeat = (r == LM_WAIT || r == LM_ERROR) && x->last_pc == x->pc && x->last_r == r;
    x->last_pc = x->pc; x->last_r = r;
    if (repeat) buf[0] = '\0';
    else if (op->op == 'R' || op->op == 'A')
        snprintf(buf, sizeof buf, "T%d:%s(%c)=%s ", x->t.id, op->op == 'R' ? "S" : "X", obj_name(op->obj), rn[r]);
    else
        snprintf(buf, sizeof buf, "T%d:%s ", x->t.id, op->op == 'C' ? "commit" : op->op == 'B' ? "abort"
                 : r == LM_ERROR ? "unlock(refused)" : "unlock");
    if (op->op == 'U') r = LM_GRANTED;            /* 거절돼도 다음 연산으로 */
    tr_add(tr, cap, buf);
    if (r == LM_GRANTED) {
        if (op->op == 'R' && x->nseen < 8) x->seen[x->nseen++] = v;
        if (!x->done) x->pc++;
    } else if (r == LM_ABORTED) {                 /* 희생자: 되돌리고 같은 ts 로 처음부터 */
        tx_abort(db, &x->t);
        x->victimized++; x->t.retries++;
        tx_begin(db, &x->t, x->t.id, x->t.ts);
        x->pc = 0; x->nseen = 0;
        tr_add(tr, cap, "[rollback+retry] ");
    }
}
/* order[] 대로 한 걸음씩 강제한 뒤, 남은 것은 라운드 로빈. max_steps 안에 다 안 끝나면 false (멈춤 없음). */
static bool run_script(db_t *db, stx_t *xs, int n, const int *order, int norder, int max_steps, char *tr, size_t cap) {
    tr[0] = '\0';
    for (int k = 0; k < norder; k++) step_tx(db, &xs[order[k]], tr, cap);
    for (int s = 0; s < max_steps; s++) {
        bool all = true;
        for (int i = 0; i < n; i++) if (!xs[i].done) { all = false; step_tx(db, &xs[i], tr, cap); }
        if (all) return true;
    }
    for (int i = 0; i < n; i++) if (!xs[i].done) { printf("  STUCK after %d steps:\n", max_steps); lm_dump(db->lm); return false; }
    return true;
}
static void stx_init(db_t *db, stx_t *x, const sop_t *ops, int nops, int id, uint64_t ts) {
    memset(x, 0, sizeof *x);
    x->ops = ops; x->nops = nops; x->last_pc = -1;
    tx_begin(db, &x->t, id, ts);
}

/* --- thread 모드: 은행 --- */
static _Atomic uint64_t g_ts = 1;
static atomic_int g_stuck;
static uint32_t xorshift32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

typedef struct {
    db_t *db; int id; uint32_t seed; int n, naccts; bool audit; int64_t total;
    long commits, aborts, attempts, bad_audits; int max_retries;
} worker_t;

static void *bank_worker(void *arg) {
    worker_t *w = arg;
    txn_t t;
    for (int k = 0; k < w->n && !atomic_load(&g_stuck); k++) {
        int from = (int)(xorshift32(&w->seed) % (uint32_t)w->naccts);
        int to = (int)(xorshift32(&w->seed) % (uint32_t)(w->naccts - 1));
        if (to >= from) to++;
        int64_t amt = 1 + (int64_t)(xorshift32(&w->seed) % 10u);
        uint64_t ts = atomic_fetch_add(&g_ts, 1);          /* 처음 시작할 때만 새 ts */
        for (int retry = 0;; retry++) {
            w->attempts++;
            tx_begin(w->db, &t, w->id, ts);
            lmres_t r;
            if (!w->audit) {
                r = transfer_once(w->db, &t, from, to, amt);
            } else {                                        /* 감사: 전 계좌를 S 로 읽어 합 */
                int64_t sum = 0, v = 0;
                r = LM_GRANTED;
                for (int a = 0; a < w->naccts && r == LM_GRANTED; a++) { r = tx_read(w->db, &t, a, &v); sum += v; }
                if (r == LM_GRANTED) { if (sum != w->total) w->bad_audits++; tx_commit(w->db, &t); }
                else tx_abort(w->db, &t);
            }
            if (r == LM_GRANTED) { w->commits++; if (retry > w->max_retries) w->max_retries = retry; break; }
            if (r == LM_ABORTED) { w->aborts++; continue; }
            atomic_store(&g_stuck, 1);                      /* LM_TIMEOUT / LM_ERROR */
            printf("  STUCK: T%d result %d\n", w->id, (int)r);
            lm_dump(w->db->lm);
            break;
        }
    }
    return NULL;
}

int main(void) {
    static lockmgr_t lm;
    char tr[400];

    /* ---------- Part A ---------- */
    CHECK(lk_compat(LK_S, LK_S) == true && lk_compat(LK_S, LK_X) == false);
    CHECK(lk_compat(LK_X, LK_S) == false && lk_compat(LK_X, LK_X) == false);
    CHECK(lk_compat(LK_NONE, LK_X) == true);

    lm_init(&lm, V_YOUNGEST);
    for (int i = 0; i < 4; i++) lm_begin(&lm, i, (uint64_t)(i + 1));
    CHECK(lm_acquire(&lm, 0, 0, LK_S) == LM_GRANTED && lm_acquire(&lm, 1, 0, LK_S) == LM_GRANTED);  /* S+S */
    CHECK(lm_acquire(&lm, 2, 0, LK_X) == LM_WAIT);                    /* X 는 S 보유자 뒤에서 대기 */
    CHECK(lm_acquire(&lm, 3, 0, LK_S) == LM_WAIT);                    /* S 지만 X 대기자를 새치기 못 함 (FIFO) */
    lm_release(&lm, 0, 0);
    CHECK(lm_acquire(&lm, 2, 0, LK_X) == LM_WAIT);                    /* T1 이 아직 S */
    lm_release(&lm, 1, 0);
    CHECK(lm_acquire(&lm, 2, 0, LK_X) == LM_GRANTED && lm_acquire(&lm, 3, 0, LK_S) == LM_WAIT);
    lm_release(&lm, 2, 0);
    CHECK(lm_acquire(&lm, 3, 0, LK_S) == LM_GRANTED && lm.tx[3].nlocks == 1);
    CHECK(lm_acquire(&lm, 3, 0, LK_S) == LM_GRANTED && lm.obj[0].n == 1);   /* 재진입: 엔트리 그대로 */
    lm_release_all(&lm, 3);
    CHECK(lm_acquire(&lm, 0, 1, LK_S) == LM_GRANTED && lm_acquire(&lm, 0, 1, LK_X) == LM_GRANTED);  /* 혼자면 즉시 업그레이드 */
    CHECK(lm.obj[1].q[0].held == LK_X && lm_acquire(&lm, 0, 1, LK_S) == LM_GRANTED && lm.tx[0].nlocks == 1);
    CHECK(lm_acquire(&lm, 0, 2, LK_S) == LM_GRANTED && lm_acquire(&lm, 1, 2, LK_S) == LM_GRANTED);
    CHECK(lm_acquire(&lm, 0, 2, LK_X) == LM_WAIT);                    /* 업그레이드 대기 (T1 의 S) */
    CHECK(lm_acquire(&lm, 2, 2, LK_S) == LM_WAIT);                    /* 업그레이드 대기자 우선 → 새 S 도 대기 */
    lm_release(&lm, 1, 2);
    CHECK(lm.obj[2].q[0].held == LK_X && lm.tx[0].ws == W_IDLE && lm_acquire(&lm, 2, 2, LK_S) == LM_WAIT);
    lm_release_all(&lm, 0);
    CHECK(lm_acquire(&lm, 2, 2, LK_S) == LM_GRANTED);
    lm_release_all(&lm, 2);
    CHECK(lm_quiet(&lm) && lm.deadlocks == 0);
    lm_destroy(&lm);

    {   /* undo: 같은 객체 두 번 쓰고 abort → 처음 값. strict 는 이른 해제 거절 */
        lm_init(&lm, V_YOUNGEST);
        db_t db = { .lm = &lm, .strict = true };
        db.val[0] = 100; db.val[1] = 7;
        txn_t t;
        tx_begin(&db, &t, 1, 1);
        CHECK(tx_add(&db, &t, 0, 5) == LM_GRANTED && tx_add(&db, &t, 0, 7) == LM_GRANTED && tx_write(&db, &t, 1, 0) == LM_GRANTED);
        CHECK(db.val[0] == 112 && db.val[1] == 0 && t.nundo == 3 && tx_unlock(&db, &t, 0) == false);
        tx_abort(&db, &t);
        CHECK(db.val[0] == 100 && db.val[1] == 7 && lm_quiet(&lm));
        lm_destroy(&lm);
    }

    /* ---------- Part B ---------- */
    {   /* WFG: obj0 = T1[S] T2[S->X] T3[-->S] T4[-->X],  obj1 = T5[X] T1[-->S] */
        lm_init(&lm, V_YOUNGEST);
        for (int i = 1; i <= 5; i++) lm_begin(&lm, i, (uint64_t)i);
        lm.detect = false;                        /* 검사 없이 상태만 만든다 */
        lm_acquire(&lm, 1, 0, LK_S); lm_acquire(&lm, 2, 0, LK_S);
        lm_acquire(&lm, 2, 0, LK_X); lm_acquire(&lm, 3, 0, LK_S); lm_acquire(&lm, 4, 0, LK_X);
        lm_acquire(&lm, 5, 1, LK_X); lm_acquire(&lm, 1, 1, LK_S);
        uint64_t adj[MAXTX];
        build_wfg(&lm, adj);
        CHECK(adj[2] == (1ull << 1));                              /* 업그레이드: 다른 S 보유자 T1 */
        CHECK(adj[3] == (1ull << 2));                              /* 일반 S: 업그레이드 대기자 T2 (T1 의 S 와는 호환) */
        CHECK(adj[4] == ((1ull << 1) | (1ull << 2) | (1ull << 3)));/* X: 보유자 T1,T2 + 앞선 대기자 T3 */
        CHECK(adj[1] == (1ull << 5) && adj[5] == 0);
        int cyc[MAXTX], n = 0;
        CHECK(find_cycle(adj, 4, cyc, &n) == false);                /* T1 은 T5 를 기다리고, T5 는 아무도 안 기다림 */
        lm_acquire(&lm, 5, 0, LK_S);                                /* T5 가 obj0 을 기다리면 사이클 */
        build_wfg(&lm, adj);
        CHECK(find_cycle(adj, 5, cyc, &n) == true && n >= 2 && cyc[0] == 5);
        bool ok = true;
        for (int k = 0; k < n; k++) ok = ok && (adj[cyc[k]] >> cyc[(k + 1) % n] & 1u);
        CHECK(ok);                                                  /* 돌려준 경로가 진짜 사이클 */
        uint64_t self[MAXTX] = { 0 }; self[7] = 1ull << 7;
        CHECK(find_cycle(self, 7, cyc, &n) == true && n == 1);      /* self-loop */
        lm_destroy(&lm);
    }
    {   /* 희생자 정책: T0 holds A wants B, T1 holds B,F wants C, T2 holds C,D,E wants A (막히는 순간 사이클) */
        static const vpolicy_t pol[3] = { V_YOUNGEST, V_FEWEST_LOCKS, V_LEAST_WORK };
        static const int expect[3] = { 1, 0, 2 };
        for (int p = 0; p < 3; p++) {
            lm_init(&lm, pol[p]);
            lm_begin(&lm, 0, 10); lm_begin(&lm, 1, 30); lm_begin(&lm, 2, 20);
            lm_acquire(&lm, 0, 0, LK_X);
            lm_acquire(&lm, 1, 1, LK_X); lm_acquire(&lm, 1, 5, LK_X);
            lm_acquire(&lm, 2, 2, LK_X); lm_acquire(&lm, 2, 3, LK_X); lm_acquire(&lm, 2, 4, LK_X);
            for (int i = 0; i < 5; i++) lm_add_work(&lm, 0);
            lm_add_work(&lm, 1); lm_add_work(&lm, 1);
            lmres_t r0 = lm_acquire(&lm, 0, 1, LK_X), r1 = lm_acquire(&lm, 1, 2, LK_X), r2 = lm_acquire(&lm, 2, 0, LK_X);
            CHECK(r0 == LM_WAIT && r1 == LM_WAIT && lm.deadlocks == 1 && lm.last_n == 3 && lm.last_victim == expect[p]);
            CHECK(lm.tx[expect[p]].ws == W_ABORTED && (r2 == (expect[p] == 2 ? LM_ABORTED : LM_WAIT)));
            lm_destroy(&lm);
        }
    }
    {   /* 고전 ABBA: T1 A-10,B+10 / T2 B-20,A+20, 강제 순서 T1 T2 T1 T2 */
        lm_init(&lm, V_YOUNGEST); lm.verbose = true;
        db_t db = { .lm = &lm, .strict = true };
        db.val[0] = 100; db.val[1] = 100;
        static const sop_t s1[] = { {'A', 0, -10}, {'A', 1, 10}, {'C', 0, 0} };
        static const sop_t s2[] = { {'A', 1, -20}, {'A', 0, 20}, {'C', 0, 0} };
        stx_t xs[2];
        stx_init(&db, &xs[0], s1, 3, 1, 1); stx_init(&db, &xs[1], s2, 3, 2, 2);
        static const int order[] = { 0, 1, 0, 1 };
        bool fin = run_script(&db, xs, 2, order, 4, 50, tr, sizeof tr);
        printf("  trace: %s\n", tr);
        CHECK(fin && lm.deadlocks == 1 && lm.last_victim == 2 && lm.last_n == 2);
        CHECK(xs[0].victimized == 0 && xs[1].victimized == 1 && xs[1].t.retries == 1 && xs[1].t.ts == 2);
        CHECK(db.val[0] == 110 && db.val[1] == 90 && lm_quiet(&lm));
        lm_destroy(&lm);
    }
    {   /* 업그레이드 데드락: 둘 다 S(A) 읽고, 둘 다 X(A) 로 올리려 함 */
        lm_init(&lm, V_YOUNGEST); lm.verbose = true;
        db_t db = { .lm = &lm, .strict = true };
        db.val[0] = 100;
        static const sop_t s[] = { {'R', 0, 0}, {'A', 0, 1}, {'C', 0, 0} };
        stx_t xs[2];
        stx_init(&db, &xs[0], s, 3, 1, 1); stx_init(&db, &xs[1], s, 3, 2, 2);
        static const int order[] = { 0, 1, 0, 1 };
        bool fin = run_script(&db, xs, 2, order, 4, 50, tr, sizeof tr);
        printf("  trace: %s\n", tr);
        CHECK(fin && lm.deadlocks == 1 && lm.last_victim == 2 && db.val[0] == 102 && lm_quiet(&lm));
        lm_destroy(&lm);
    }
    {   /* non-strict 2PL: T1 A+50 후 A 를 일찍 풂 → T2 가 읽고(dirty) commit → T1 abort */
        static const sop_t s1[] = { {'A', 0, 50}, {'A', 1, 1}, {'U', 0, 0}, {'B', 0, 0} };
        static const sop_t s2[] = { {'R', 0, 0}, {'C', 0, 0} };
        static const sop_t s3[] = { {'A', 0, 10}, {'C', 0, 0} };
        static const int order[] = { 0, 0, 0, 1, 1, 0 };
        for (int strict = 0; strict <= 1; strict++) {
            lm_init(&lm, V_YOUNGEST);
            db_t db = { .lm = &lm, .strict = strict };
            db.val[0] = 100;
            stx_t xs[2];
            stx_init(&db, &xs[0], s1, 4, 1, 1); stx_init(&db, &xs[1], s2, 2, 2, 2);
            bool fin = run_script(&db, xs, 2, order, 6, 50, tr, sizeof tr);
            printf("  %s: %s\n", strict ? "strict    " : "non-strict", tr);
            if (!strict) CHECK(fin && xs[1].seen[0] == 150 && db.val[0] == 100);  /* T2 가 존재한 적 없는 150 을 읽고 커밋 */
            else         CHECK(fin && xs[1].seen[0] == 100 && db.val[0] == 100);  /* T2 는 T1 abort 까지 기다림 */
            lm_destroy(&lm);
            lm_init(&lm, V_YOUNGEST);                       /* 같은 상황에서 T2 가 쓰면: 커밋된 쓰기가 사라짐 */
            db.lm = &lm; db.val[0] = 100; db.val[1] = 0;
            stx_init(&db, &xs[0], s1, 4, 1, 1); stx_init(&db, &xs[1], s3, 2, 2, 2);
            fin = run_script(&db, xs, 2, order, 6, 50, tr, sizeof tr);
            printf("  %s: %s\n", strict ? "strict    " : "non-strict", tr);
            if (!strict) CHECK(fin && db.val[0] == 100);    /* T2 의 +10 (커밋됨) 이 T1 의 undo 에 덮임 */
            else         CHECK(fin && db.val[0] == 110);
            lm_destroy(&lm);
        }
        lm_init(&lm, V_YOUNGEST);
        db_t db = { .lm = &lm, .strict = false };
        txn_t t;
        tx_begin(&db, &t, 1, 1);
        CHECK(tx_add(&db, &t, 0, 1) == LM_GRANTED && tx_unlock(&db, &t, 0) && tx_add(&db, &t, 1, 1) == LM_ERROR);
        tx_abort(&db, &t);
        lm_destroy(&lm);
    }

    /* ---------- Part C ---------- */
    if (g_fail) {
        printf("SKIP 7 stress checks (Part C): unit checks failed\n");
    } else {
        enum { NACCT = 16, NW = 8, NTRANS = 3000, NAUDIT = 300 };
        lm_init(&lm, V_YOUNGEST);
        db_t db = { .lm = &lm, .strict = true, .blocking = true, .timeout_s = 2.0 };
        int64_t total = 0;
        for (int a = 0; a < NACCT; a++) { db.val[a] = 1000 + 37 * a; total += db.val[a]; }
        worker_t w[NW + 1];
        pthread_t th[NW + 1];
        for (int i = 0; i <= NW; i++) {
            w[i] = (worker_t){ .db = &db, .id = i, .seed = 0x9E3779B9u ^ (uint32_t)(i * 7919 + 1),
                               .n = i < NW ? NTRANS : NAUDIT, .naccts = NACCT, .audit = i == NW, .total = total };
            pthread_create(&th[i], NULL, bank_worker, &w[i]);
        }
        long commits = 0, aborts = 0, attempts = 0, bad = 0; int maxr = 0;
        for (int i = 0; i <= NW; i++) {
            pthread_join(th[i], NULL);
            commits += w[i].commits; aborts += w[i].aborts; attempts += w[i].attempts; bad += w[i].bad_audits;
            if (w[i].max_retries > maxr) maxr = w[i].max_retries;
        }
        int64_t sum = 0;
        for (int a = 0; a < NACCT; a++) sum += db.val[a];
        printf("  bank: %d threads x %d transfers + %d audits: commits=%ld aborts=%ld deadlocks=%ld max_retries=%d\n",
               NW, NTRANS, NAUDIT, commits, aborts, lm.deadlocks, maxr);
        CHECK(atomic_load(&g_stuck) == 0);
        CHECK(sum == total);
        CHECK(commits == (long)NW * NTRANS + NAUDIT);
        CHECK(attempts == commits + aborts);
        CHECK(aborts == lm.deadlocks);              /* 사이클 하나 = 희생자 하나 = abort 하나 */
        CHECK(bad == 0);                            /* 모든 감사가 정확한 총액을 봤다 (직렬화 가능) */
        CHECK(lm_quiet(&lm));
        lm_destroy(&lm);
    }

    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
