/* STOR-M08 추가 연습 — 한 번에 검증
 *
 *   연습 1  wait-die / wound-wait (타임스탬프로 "예방"): 결정 함수 + 한 걸음씩 도는 X 락 시뮬레이터.
 *           무작위 스크립트 300 개에서 대기 간선이 항상 한 방향(늙은→젊은 / 젊은→늙은)이라 사이클 0,
 *           모두 커밋(같은 ts 재시작). 같은 스크립트를 예방 없이(그냥 대기) 돌리면 사이클이 생긴다.
 *   연습 2  타임아웃 감지의 트레이드오프 (논리 시계): 짧은 T = 헛 abort, 긴 T = 데드락이 오래 방치.
 *   연습 3  다중 단위 잠금(multi-granularity) 의도 락 IS/IX/S/SIX/X 호환성 행렬과 부모 모드.
 *   연습 4  U(update) 락: S→X 업그레이드 데드락을 읽는 시점의 대기로 바꾼다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice
 */
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static uint32_t xorshift32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* ============ 작은 step 모드 lock table (모드는 int, 호환성은 함수 포인터) ============ */
enum { NT = 8, NO = 8 };
enum { M_NONE = 0, M_S = 1, M_U = 2, M_X = 3 };
enum { P_WAIT = 0, P_WAIT_DIE = 1, P_WOUND_WAIT = 2 };
enum { R_OK = 0, R_WAIT = 1, R_DIE = 2, R_WOUND = 3 };
typedef bool (*compat_fn)(int held, int want);
typedef struct { int tx, held, want; } ent_t;
typedef struct {
    ent_t q[NO][NT]; int n[NO];
    compat_fn compat; int policy;
    uint64_t ts[NT]; bool wounded[NT];
    long aborts, wounds;
} plm_t;

static bool compat_sx(int h, int w) { return h == M_NONE || w == M_NONE || (h == M_S && w == M_S); }

/* 연습 1: 충돌 상대(blocker)가 있을 때 요청자가 할 일.
 *   wait-die  : 요청자가 더 늙었으면(ts 작음) 기다린다, 젊으면 죽는다(자기 abort).
 *   wound-wait: 요청자가 더 늙었으면 상대를 다치게 한다(상대 abort), 젊으면 기다린다. */
static int ww_decide(int policy, uint64_t req_ts, uint64_t blk_ts) {
    if (policy == P_WAIT_DIE) return req_ts < blk_ts ? R_WAIT : R_DIE;
    if (policy == P_WOUND_WAIT) return req_ts < blk_ts ? R_WOUND : R_WAIT;
    return R_WAIT;
}

static ent_t *pfind(plm_t *lm, int o, int tx) {
    for (int i = 0; i < lm->n[o]; i++) if (lm->q[o][i].tx == tx) return &lm->q[o][i];
    return NULL;
}
static void pgrant(plm_t *lm, int o) {               /* 업그레이드 먼저, 그다음 FIFO */
    for (int i = 0; i < lm->n[o]; i++) {
        ent_t *e = &lm->q[o][i];
        if (e->held == M_NONE || e->want == M_NONE) continue;
        bool ok = true;
        for (int j = 0; j < lm->n[o]; j++) if (j != i && !lm->compat(lm->q[o][j].held, e->want)) ok = false;
        if (!ok) return;
        e->held = e->want; e->want = M_NONE;
    }
    for (int i = 0; i < lm->n[o]; i++) {
        ent_t *e = &lm->q[o][i];
        if (e->held != M_NONE || e->want == M_NONE) continue;
        for (int j = 0; j < lm->n[o]; j++) if (j != i && !lm->compat(lm->q[o][j].held, e->want)) return;
        e->held = e->want; e->want = M_NONE;
    }
}
static void prelease_all(plm_t *lm, int tx) {
    for (int o = 0; o < NO; o++) {
        ent_t *e = pfind(lm, o, tx);
        if (!e) continue;
        int i = (int)(e - lm->q[o]);
        memmove(&lm->q[o][i], &lm->q[o][i + 1], (size_t)(lm->n[o] - i - 1) * sizeof(ent_t));
        lm->n[o]--;
        pgrant(lm, o);
    }
}
/* blocker = 비호환 보유자 + (일반 대기자라면) 업그레이드 대기자와 앞선 대기자 전부 (FIFO 라 호환돼도 못 지나감) */
static uint32_t blockers(plm_t *lm, int o, int tx) {
    uint32_t b = 0;
    int me = -1;
    for (int i = 0; i < lm->n[o]; i++) if (lm->q[o][i].tx == tx) me = i;
    if (me < 0) return 0;
    const ent_t *w = &lm->q[o][me];
    for (int j = 0; j < lm->n[o]; j++) {
        const ent_t *h = &lm->q[o][j];
        if (j == me) continue;
        if (!lm->compat(h->held, w->want)) b |= 1u << h->tx;
        if (w->held == M_NONE && h->want != M_NONE && (h->held != M_NONE || j < me)) b |= 1u << h->tx;
    }
    return b;
}
/* 요청 / 재폴링. 다친(wounded) 트랜잭션은 다음 요청에서 R_DIE 를 받는다. */
static int prequest(plm_t *lm, int tx, int o, int mode) {
    if (lm->wounded[tx]) return R_DIE;
    ent_t *e = pfind(lm, o, tx);
    if (e && e->want == M_NONE && e->held >= mode) return R_OK;
    if (!e) { e = &lm->q[o][lm->n[o]++]; *e = (ent_t){ tx, M_NONE, mode }; }
    else if (e->want == M_NONE) e->want = mode;
    if (e->held == M_NONE && e->want != M_NONE && lm->n[o] == 1) { e->held = mode; e->want = M_NONE; return R_OK; }
    pgrant(lm, o);
    if (e->want == M_NONE) return R_OK;
    for (;;) {                                        /* 정책 적용 */
        uint32_t b = blockers(lm, o, tx);
        if (!b) { pgrant(lm, o); return e->want == M_NONE ? R_OK : R_WAIT; }
        bool wounded_any = false;
        for (int k = 0; k < NT; k++) {
            if (!(b >> k & 1u)) continue;
            int d = ww_decide(lm->policy, lm->ts[tx], lm->ts[k]);
            if (d == R_DIE) { lm->aborts++; return R_DIE; }   /* 하네스가 release_all + 재시작 */
            if (d == R_WOUND) {                               /* 상대를 abort: 락 즉시 회수 (데이터 없음 모델) */
                lm->wounded[k] = true; lm->wounds++; lm->aborts++;
                prelease_all(lm, k);
                wounded_any = true;
            }
        }
        if (!wounded_any) return R_WAIT;
        e = pfind(lm, o, tx);
    }
}
/* 대기 간선 a→b 전체가 정책이 허락하는 방향인지, 그리고 사이클이 있는지 */
static uint32_t wait_edges(plm_t *lm, int a) {
    for (int o = 0; o < NO; o++) {
        ent_t *e = pfind(lm, o, a);
        if (e && e->want != M_NONE) return blockers(lm, o, a);
    }
    return 0;
}
static bool reach(plm_t *lm, int u, int target, uint32_t *seen) {
    uint32_t b = wait_edges(lm, u);
    for (int v = 0; v < NT; v++) {
        if (!(b >> v & 1u)) continue;
        if (v == target) return true;
        if (*seen >> v & 1u) continue;
        *seen |= 1u << v;
        if (reach(lm, v, target, seen)) return true;
    }
    return false;
}
static bool has_cycle(plm_t *lm) {
    for (int t = 0; t < NT; t++) { uint32_t s = 1u << t; if (reach(lm, t, t, &s)) return true; }
    return false;
}
static bool edges_ok(plm_t *lm) {
    for (int a = 0; a < NT; a++) {
        uint32_t b = wait_edges(lm, a);
        for (int k = 0; k < NT; k++) {
            if (!(b >> k & 1u)) continue;
            if (lm->policy == P_WAIT_DIE && !(lm->ts[a] < lm->ts[k])) return false;   /* 늙은 → 젊은 만 */
            if (lm->policy == P_WOUND_WAIT && !(lm->ts[a] > lm->ts[k])) return false; /* 젊은 → 늙은 만 */
        }
    }
    return true;
}

/* 스크립트 sim: 트랜잭션 nt 개, 각자 X 락 3 개를 차례로 → commit. 반환: 0 = 모두 커밋, 1 = 사이클(멈춤), 2 = 방향 위반 */
typedef struct { int obj[3]; int pc; bool done; int restarts; } ptx_t;
static int run_sim(int policy, uint32_t seed, int nt, long *aborts) {
    plm_t lm;
    memset(&lm, 0, sizeof lm);
    lm.compat = compat_sx; lm.policy = policy;
    ptx_t x[NT];
    for (int t = 0; t < nt; t++) {
        x[t] = (ptx_t){ { 0 }, 0, false, 0 };
        lm.ts[t] = (uint64_t)(t + 1);
        for (int k = 0; k < 3; k++) {
            int o;
            bool dup;
            do { o = (int)(xorshift32(&seed) % 4u); dup = false; for (int j = 0; j < k; j++) dup |= x[t].obj[j] == o; } while (dup);
            x[t].obj[k] = o;
        }
    }
    for (int step = 0; step < 5000; step++) {
        int t = (int)(xorshift32(&seed) % (uint32_t)nt);          /* 무작위 스케줄 */
        if (x[t].done) {
            bool all = true;
            for (int i = 0; i < nt; i++) all &= x[i].done;
            if (all) { *aborts = lm.aborts; return 0; }
            continue;
        }
        int r = lm.wounded[t] ? R_DIE : x[t].pc == 3 ? R_OK : prequest(&lm, t, x[t].obj[x[t].pc], M_X);
        if (r == R_DIE) { prelease_all(&lm, t); lm.wounded[t] = false; x[t].pc = 0; x[t].restarts++; }  /* 같은 ts 로 재시작 */
        else if (x[t].pc == 3) { prelease_all(&lm, t); x[t].done = true; }                             /* commit */
        else if (r == R_OK) x[t].pc++;
        if (!edges_ok(&lm)) return 2;
        if (has_cycle(&lm)) { *aborts = lm.aborts; return 1; }
    }
    *aborts = lm.aborts;
    return 3;
}

/* ============ 연습 2: 타임아웃 (논리 시계) ============
 * 시나리오 L: T1 이 A 를 hold_ticks 동안 쥐고 정상 커밋. T2 는 tick 1 부터 A 를 기다림 (데드락 아님).
 * 시나리오 D: tick 2 에 ABBA 데드락. 먼저 시간이 다한 쪽이 abort.
 * 반환: L → T2 가 abort 됐는가(헛 abort), D → 데드락이 풀린 tick. */
static bool timeout_false_abort(int T, int hold_ticks) {
    for (int tick = 1; tick <= 1000; tick++) {
        if (tick >= hold_ticks) return false;          /* T1 커밋 → T2 획득 */
        if (tick - 1 > T) return true;                 /* T2 가 T 보다 오래 기다림 → abort */
    }
    return false;
}
static int timeout_resolve_tick(int T) {
    int start = 2;
    for (int tick = start; tick <= 100000; tick++) if (tick - start > T) return tick;
    return -1;
}

/* ============ 연습 3: 의도 락 ============ */
enum { IS = 0, IX = 1, S = 2, SIX = 3, X = 4 };
static bool mg_compat(int held, int want) {
    static const uint8_t ok[5] = {         /* 비트 j = want j 와 호환. 행 = held */
        /* IS  */ 0x0F,                    /* IS IX S SIX */
        /* IX  */ 0x03,                    /* IS IX */
        /* S   */ 0x05,                    /* IS S */
        /* SIX */ 0x01,                    /* IS */
        /* X   */ 0x00,
    };
    return ok[held] >> want & 1u;
}
static int mg_parent(int child) { return (child == S || child == IS) ? IS : IX; }   /* SIX/X/IX → IX */

/* ============ 연습 4: U 락 ============ */
static bool compat_sux(int h, int w) {
    if (h == M_NONE || w == M_NONE) return true;
    if (h == M_X || w == M_X) return false;
    if (h == M_U && w == M_U) return false;   /* U 는 한 명만 */
    return true;                              /* S-S, S-U, U-S (시스템마다 U-S 를 막기도 한다) */
}

int main(void) {
    /* 연습 1 */
    CHECK(ww_decide(P_WAIT_DIE, 1, 2) == R_WAIT && ww_decide(P_WAIT_DIE, 2, 1) == R_DIE);
    CHECK(ww_decide(P_WOUND_WAIT, 1, 2) == R_WOUND && ww_decide(P_WOUND_WAIT, 2, 1) == R_WAIT);
    {   /* 데드락이 아닌데도 wait-die 는 젊은 쪽을 죽인다 (예방의 대가) */
        plm_t lm; memset(&lm, 0, sizeof lm);
        lm.compat = compat_sx; lm.policy = P_WAIT_DIE; lm.ts[1] = 1; lm.ts[2] = 2;
        CHECK(prequest(&lm, 1, 0, M_X) == R_OK && prequest(&lm, 2, 0, M_X) == R_DIE && lm.aborts == 1);
        lm.policy = P_WAIT;
        CHECK(prequest(&lm, 2, 0, M_X) == R_WAIT);                      /* 감지 방식이면 그냥 기다릴 상황 */
    }
    {   /* ABBA: wound-wait 은 늙은 T1 이 젊은 T2 를 다치게 하고 B 를 가져간다 */
        plm_t lm; memset(&lm, 0, sizeof lm);
        lm.compat = compat_sx; lm.policy = P_WOUND_WAIT; lm.ts[1] = 1; lm.ts[2] = 2;
        CHECK(prequest(&lm, 1, 0, M_X) == R_OK && prequest(&lm, 2, 1, M_X) == R_OK);
        CHECK(prequest(&lm, 1, 1, M_X) == R_OK && lm.wounded[2] && lm.wounds == 1);
        CHECK(prequest(&lm, 2, 0, M_X) == R_DIE);                       /* 다친 T2 는 다음 요청에서 abort 를 안다 */
    }
    {
        long ab[3] = { 0, 0, 0 }; int cyc[3] = { 0, 0, 0 }, bad[3] = { 0, 0, 0 }, done[3] = { 0, 0, 0 };
        for (int p = 0; p < 3; p++)
            for (uint32_t s = 1; s <= 300; s++) {
                long a = 0;
                int r = run_sim(p, s * 2654435761u, 5, &a);
                ab[p] += a; cyc[p] += r == 1; bad[p] += r >= 2; done[p] += r == 0;
            }
        printf("  300 scripts x 5 txns: wait     -> cycles %d, done %d\n", cyc[0], done[0]);
        printf("                        wait-die -> cycles %d, done %d, aborts %ld\n", cyc[1], done[1], ab[1]);
        printf("                      wound-wait -> cycles %d, done %d, aborts %ld\n", cyc[2], done[2], ab[2]);
        CHECK(cyc[0] > 0);                                              /* 예방 없으면 데드락이 생긴다 */
        CHECK(cyc[1] == 0 && bad[1] == 0 && done[1] == 300);
        CHECK(cyc[2] == 0 && bad[2] == 0 && done[2] == 300);
    }

    /* 연습 2 */
    CHECK(timeout_false_abort(5, 12) == true);       /* 짧은 T: 데드락도 아닌데 abort */
    CHECK(timeout_false_abort(20, 12) == false);     /* 긴 T: 기다려서 받음 */
    CHECK(timeout_resolve_tick(5) == 8 && timeout_resolve_tick(20) == 23);   /* 긴 T: 진짜 데드락이 오래 남음 */

    /* 연습 3 */
    {
        static const char *exp[5] = { "11110", "11000", "10100", "10000", "00000" };   /* 표준 행렬 (Gray) */
        bool ok = true;
        for (int h = 0; h < 5; h++) for (int w = 0; w < 5; w++) ok &= mg_compat(h, w) == (exp[h][w] == '1');
        CHECK(ok);
        bool sym = true;
        for (int h = 0; h < 5; h++) for (int w = 0; w < 5; w++) sym &= mg_compat(h, w) == mg_compat(w, h);
        CHECK(sym);
        CHECK(mg_parent(S) == IS && mg_parent(X) == IX && mg_parent(SIX) == IX);
        CHECK(mg_compat(IX, mg_parent(S)) == true);   /* 다른 행 수정 중(IX) + 행 읽기(IS): 같이 됨 */
        CHECK(mg_compat(S, IX) == false);             /* 테이블 전체 읽기(S) 중 행 수정(IX) 은 기다림 */
    }

    /* 연습 4 */
    {   /* S 로 읽고 X 로 올리기: 두 트랜잭션이면 사이클 */
        plm_t lm; memset(&lm, 0, sizeof lm);
        lm.compat = compat_sux; lm.policy = P_WAIT; lm.ts[1] = 1; lm.ts[2] = 2;
        CHECK(prequest(&lm, 1, 0, M_S) == R_OK && prequest(&lm, 2, 0, M_S) == R_OK);
        CHECK(prequest(&lm, 1, 0, M_X) == R_WAIT && prequest(&lm, 2, 0, M_X) == R_WAIT && has_cycle(&lm));
    }
    {   /* U 로 읽기: 두 번째 U 가 읽는 시점에 기다림 → 사이클 없음, 둘 다 커밋 */
        plm_t lm; memset(&lm, 0, sizeof lm);
        lm.compat = compat_sux; lm.policy = P_WAIT; lm.ts[1] = 1; lm.ts[2] = 2; lm.ts[3] = 3;
        CHECK(prequest(&lm, 1, 0, M_U) == R_OK && prequest(&lm, 2, 0, M_U) == R_WAIT);
        CHECK(prequest(&lm, 3, 0, M_S) == R_WAIT);                      /* FIFO: 앞선 U 대기자 뒤 (U-S 는 호환이지만 순서) */
        CHECK(prequest(&lm, 1, 0, M_X) == R_OK && !has_cycle(&lm));     /* T1 은 혼자 U → X */
        prelease_all(&lm, 1);
        CHECK(prequest(&lm, 2, 0, M_U) == R_OK && prequest(&lm, 3, 0, M_S) == R_OK);
        CHECK(prequest(&lm, 2, 0, M_X) == R_WAIT);                      /* 읽는 T3 가 끝날 때까지 */
        prelease_all(&lm, 3);
        CHECK(prequest(&lm, 2, 0, M_X) == R_OK);
    }

    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
