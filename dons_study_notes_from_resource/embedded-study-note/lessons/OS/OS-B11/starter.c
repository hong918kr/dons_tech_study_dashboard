/* OS-B11 페이지 교체 시뮬레이터: FIFO/LRU/Clock/OPT
 *
 *   Part A  pick_victim() + sim_access(): 참조 하나 처리 (hit / 빈 프레임 / 희생자 고르기)
 *           네 정책: FIFO(적재 시각), LRU(마지막 참조 시각), OPT(다음 참조 위치), Clock(use bit 1개 + 시계 바늘)
 *   Part B  fault_curve() / belady_find() / inclusion_ok(): 프레임 1..N 폴트 곡선, Belady 이상 찾기,
 *           스택 성질(f 프레임의 내용 ⊆ f+1 프레임의 내용, 모든 시각) 검사
 *   Part C  clock_lru_rank(): Clock 이 내쫓은 페이지가 그 순간 LRU 순서로 몇 번째였나 (0 = LRU 와 같은 선택)
 *
 * 검증 방법: 손으로 계산한 작은 예(OSTEP 22장 예제 포함), 정의로 계산한 기준값
 *   (LRU = 스택 거리, OPT = 가능한 모든 교체를 다 해 보는 DP), xorshift32 고정 시드 무작위 문자열.
 * 시간 측정 없음 → 매 실행 같은 결과. 시뮬레이터는 순수 C, 파일·프로세스 없음.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
 * demo_*.c 는 시뮬레이터만 쓰려고 #define OSB11_NO_MAIN 후 이 파일을 #include 한다.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FRAMES 128
#define MAX_PAGE   1024           /* 페이지 번호는 0..MAX_PAGE-1 */

typedef enum { POL_FIFO, POL_LRU, POL_CLOCK, POL_OPT, POL_COUNT } policy_t;
static const char *const pol_name[POL_COUNT] = { "FIFO", "LRU", "CLOCK", "OPT" };

typedef struct {
    policy_t pol;
    int  nframes, used;               /* 프레임 수, 채워진 프레임 수 (0..used-1 이 차 있다) */
    int  page[MAX_FRAMES];            /* 프레임 i 에 든 페이지 */
    long loaded[MAX_FRAMES];          /* 적재된 시각            — FIFO 가 쓴다 */
    long last[MAX_FRAMES];            /* 마지막으로 참조된 시각 — LRU 가 쓴다 */
    long nextuse[MAX_FRAMES];         /* 다음에 참조될 위치 (없으면 n) — OPT 가 쓴다 */
    unsigned char use[MAX_FRAMES];    /* use(reference) bit     — Clock 이 쓴다 */
    int  hand;                        /* Clock 바늘: 다음에 볼 프레임 */
    long hand_steps;                  /* Clock 바늘이 들여다본 프레임 수 (교체 비용) */
    const int  *refs;                 /* 참조 문자열 */
    const long *next;                 /* next[t] = t 이후 refs[t] 가 다시 나오는 위치, 없으면 n */
    long n, t;                        /* 길이, 지금 처리할 참조 번호 */
} sim_t;

typedef struct {                      /* sim_access() 의 결과 */
    bool hit;
    int  frame;                       /* 이 참조가 끝난 뒤 페이지가 있는 프레임 */
    int  evicted;                     /* 내쫓긴 페이지, 없으면 -1 */
    long evicted_last;                /* 내쫓긴 페이지의 마지막 참조 시각 (Part C 용) */
} acc_t;

typedef struct { long hits, misses, hand_steps; } stats_t;

/* ---------- 하네스 제공 ---------- */
static void build_next(const int *refs, long n, long *next) {
    static long seen[MAX_PAGE];
    for (int p = 0; p < MAX_PAGE; p++) seen[p] = n;
    for (long t = n - 1; t >= 0; t--) { next[t] = seen[refs[t]]; seen[refs[t]] = t; }
}
static void sim_init(sim_t *s, policy_t pol, int nframes, const int *refs, const long *next, long n) {
    memset(s, 0, sizeof *s);
    s->pol = pol; s->nframes = nframes; s->refs = refs; s->next = next; s->n = n;
}
static int sim_find(const sim_t *s, int page) {          /* 완전 연관(fully associative): 전부 뒤진다 */
    for (int f = 0; f < s->used; f++) if (s->page[f] == page) return f;
    return -1;
}

/* ======================= Part A — 희생자 고르기 + 참조 하나 처리 ======================= */
/* 프레임이 꽉 찼을 때 내쫓을 프레임 번호를 고른다.
 *   FIFO : loaded 가 가장 작은 프레임
 *   LRU  : last 가 가장 작은 프레임
 *   OPT  : nextuse 가 가장 큰 프레임. 동점(둘 다 다시 안 나옴 = n)이면 last 가 작은 쪽 (LRU 로 후퇴)
 *   CLOCK: 바늘 위치부터 돌며 use==1 이면 0 으로 지우고 다음 칸, use==0 이면 그 프레임이 희생자.
 *          들여다본 칸마다 hand_steps++, 고른 뒤 바늘은 희생자 다음 칸. */
static int pick_victim(sim_t *s) {
    /* TODO: switch (s->pol) — FIFO: loaded 최소, LRU: last 최소,
     *       OPT: nextuse 최대 (동점이면 last 최소), CLOCK: 바늘을 돌리며 use 를 지우다가 use==0 인 프레임.
     *       CLOCK 은 들여다본 칸마다 hand_steps++, 끝나면 hand = 희생자 + 1 (mod nframes). */
    (void)s;
    return 0;
}

/* refs[t] 하나를 처리하고 t++.
 *   hit  → 그 프레임의 last/nextuse 갱신, use=1
 *   miss → 빈 프레임(used < nframes)이 있으면 거기, 없으면 pick_victim() 의 프레임을 비우고 적재.
 *          적재한 프레임은 loaded=t, last=t, nextuse=next[t], use=1.
 *   내쫓았으면 evicted = 그 페이지, evicted_last = 그 페이지의 (덮어쓰기 전) last. */
static acc_t sim_access(sim_t *s) {
    /* TODO: t = s->t++ ; p = refs[t] ; sim_find → hit 이면 표시,
     *       miss 면 빈 프레임 또는 pick_victim() (evicted / evicted_last 기록 후 덮어쓰기, loaded = t).
     *       마지막으로 그 프레임의 last = t, nextuse = next[t], use = 1, a.frame = 프레임. */
    acc_t a = { false, -1, -1, -1 };
    (void)pick_victim; (void)sim_find;
    s->t++;
    return a;
}

/* 하네스: 문자열 전체를 돌린다. evicted[t] = t 에서 내쫓긴 페이지(-1), mask[t] = t 처리 뒤 상주 집합
 * (페이지 < 64 일 때만). 둘 다 NULL 가능. */
static void sim_run(policy_t pol, const int *refs, long n, int nframes,
                    stats_t *st, int *evicted, uint64_t *mask) {
    long *next = malloc((size_t)(n > 0 ? n : 1) * sizeof *next);
    if (!next) { fprintf(stderr, "out of memory\n"); exit(2); }
    build_next(refs, n, next);
    sim_t s;
    sim_init(&s, pol, nframes, refs, next, n);
    stats_t z = { 0, 0, 0 };
    for (long t = 0; t < n; t++) {
        acc_t a = sim_access(&s);
        if (a.hit) z.hits++; else z.misses++;
        if (evicted) evicted[t] = a.evicted;
        if (mask) {
            uint64_t m = 0;
            for (int f = 0; f < s.used; f++) m |= 1ULL << (s.page[f] & 63);
            mask[t] = m;
        }
    }
    z.hand_steps = s.hand_steps;
    if (st) *st = z;
    free(next);
}

/* ======================= Part B — 폴트 곡선, Belady, 스택 성질 ======================= */
/* faults[f] = f 프레임일 때 미스 수, f = 1..maxf (faults[0] 은 안 씀) */
static void fault_curve(policy_t pol, const int *refs, long n, int maxf, long *faults) {
    /* TODO: f = 1..maxf 마다 sim_run(pol, refs, n, f, ...) 의 misses */
    (void)pol; (void)refs; (void)n;
    for (int f = 0; f <= maxf; f++) faults[f] = 0;
}

/* 프레임을 늘렸는데 폴트가 늘어난 첫 f (faults[f+1] > faults[f]) — 없으면 0 */
static int belady_find(const long *faults, int maxf) {
    /* TODO: faults[f+1] > faults[f] 인 가장 작은 f, 없으면 0 */
    (void)faults; (void)maxf;
    return 0;
}

/* 모든 f(1..maxf-1)와 모든 시각 t 에서 상주집합(f) ⊆ 상주집합(f+1) 인가? (페이지 < 64)
 * 깨지는 첫 (f, t) 를 *bad_f, *bad_t 에 (안 깨지면 0, -1). */
static bool inclusion_ok(policy_t pol, const int *refs, long n, int maxf, int *bad_f, long *bad_t) {
    /* TODO: f = 1..maxf-1 마다 f 와 f+1 프레임으로 sim_run(..., mask) 를 돌려
     *       모든 t 에서 (mask_f[t] & ~mask_f+1[t]) == 0 인지. 처음 깨지는 (f, t) 를 기록. */
    (void)pol; (void)refs; (void)n; (void)maxf;
    *bad_f = 0; *bad_t = -1;
    return false;
}

/* ======================= Part C — Clock 은 LRU 를 얼마나 닮았나 ======================= */
/* Clock 을 돌리며 교체가 일어날 때마다 희생자의 "LRU 순위" 를 센다:
 *   rank = 남아 있는 다른 페이지 중 희생자보다 마지막 참조가 더 오래된 페이지 수 (0 = 진짜 LRU 페이지)
 * hist[0..nf-1] 에 누적 (호출자가 0 으로 채워 둔다), st 에 hits/misses/hand_steps. 반환: 교체 횟수. */
static long clock_lru_rank(const int *refs, long n, int nf, long *hist, stats_t *st) {
    /* TODO: POL_CLOCK 으로 sim_init 후 sim_access 를 n 번. 교체가 일어나면
     *       rank = (희생자 자리를 뺀) 다른 프레임 중 last < a.evicted_last 인 개수 → hist[rank]++.
     *       hits / misses / hand_steps 를 *st 에, 교체 횟수를 반환. next 배열은 build_next 로. */
    (void)refs; (void)n; (void)nf; (void)hist;
    if (st) { st->hits = 0; st->misses = 0; st->hand_steps = 0; }
    return 0;
}

/* ---------- 작업 부하(workload) 생성기 — 하네스 제공, demo 도 쓴다 ---------- */
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static void wl_uniform(int *r, long n, int pages, uint32_t seed) {          /* 지역성 없음 */
    for (long i = 0; i < n; i++) r[i] = (int)(xs32(&seed) % (uint32_t)pages);
}
static void wl_8020(int *r, long n, int pages, uint32_t seed) {             /* 80% 참조가 20% 페이지로 */
    int hot = pages / 5;
    for (long i = 0; i < n; i++) {
        if (xs32(&seed) % 100 < 80) r[i] = (int)(xs32(&seed) % (uint32_t)hot);
        else                        r[i] = hot + (int)(xs32(&seed) % (uint32_t)(pages - hot));
    }
}
static void wl_loop(int *r, long n, int loop) {                             /* 0,1,..,loop-1 반복 */
    for (long i = 0; i < n; i++) r[i] = (int)(i % loop);
}

#ifndef OSB11_NO_MAIN
/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* 기준값 1 — LRU 의 정의: f 프레임 LRU 의 내용 = 가장 최근에 참조된 서로 다른 f 페이지.
 * 그러니 t 의 참조가 hit ⇔ 직전 참조 이후 나온 서로 다른 페이지 수 < f (스택 거리 ≤ f). */
static long ref_lru_hits(const int *r, long n, int f) {
    long hits = 0;
    for (long t = 0; t < n; t++) {
        long prev = -1;
        for (long u = t - 1; u >= 0; u--) if (r[u] == r[t]) { prev = u; break; }
        if (prev < 0) continue;
        uint64_t seen = 0; int d = 0;
        for (long u = prev + 1; u < t; u++)
            if (!(seen & (1ULL << r[u]))) { seen |= 1ULL << r[u]; d++; }
        if (d < f) hits++;
    }
    return hits;
}
/* 기준값 2 — 최소 미스 수를 "모든 교체 선택을 다 해 보는" DP 로 (페이지 < 8, 상태 = 상주 비트마스크 256개) */
static long ref_min_misses(const int *r, long n, int f) {
    enum { NP = 8, NS = 1 << NP };
    long cur[NS], nxt[NS];
    const long INF = 1L << 40;
    for (int m = 0; m < NS; m++) cur[m] = INF;
    cur[0] = 0;
    for (long t = 0; t < n; t++) {
        for (int m = 0; m < NS; m++) nxt[m] = INF;
        int pb = 1 << r[t];
        for (int m = 0; m < NS; m++) {
            if (cur[m] == INF) continue;
            if (m & pb) { if (cur[m] < nxt[m]) nxt[m] = cur[m]; continue; }     /* hit */
            int cnt = 0;
            for (int q = 0; q < NP; q++) cnt += (m >> q) & 1;
            if (cnt < f) { int k = m | pb; if (cur[m] + 1 < nxt[k]) nxt[k] = cur[m] + 1; continue; }
            for (int q = 0; q < NP; q++) if (m & (1 << q)) {                   /* 누구를 내쫓든 다 해 본다 */
                int k = (m & ~(1 << q)) | pb;
                if (cur[m] + 1 < nxt[k]) nxt[k] = cur[m] + 1;
            }
        }
        memcpy(cur, nxt, sizeof cur);
    }
    long best = INF;
    for (int m = 0; m < NS; m++) if (cur[m] < best) best = cur[m];
    return best;
}
static bool same_ints(const int *a, const int *b, long n) { return memcmp(a, b, (size_t)n * sizeof *a) == 0; }
static int distinct(const int *r, long n) {
    uint64_t m = 0; int d = 0;
    for (long i = 0; i < n; i++) if (!(m & (1ULL << r[i]))) { m |= 1ULL << r[i]; d++; }
    return d;
}

int main(void) {
    /* ---------------- Part A ---------------- */
    {   /* OSTEP 22장 예제: 0 1 2 0 1 3 0 3 1 2 1, 프레임 3 */
        const int r[] = { 0, 1, 2, 0, 1, 3, 0, 3, 1, 2, 1 };
        const long n = 11;
        int ev[4][11]; stats_t st[4];
        for (int p = 0; p < POL_COUNT; p++) sim_run((policy_t)p, r, n, 3, &st[p], ev[p], NULL);
        const int e_fifo[11] = { -1, -1, -1, -1, -1, 0, 1, -1, 2, 3, -1 };   /* 그림 22.2 */
        const int e_lru[11]  = { -1, -1, -1, -1, -1, 2, -1, -1, -1, 0, -1 }; /* 그림 22.5 */
        const int e_opt[11]  = { -1, -1, -1, -1, -1, 2, -1, -1, -1, 0, -1 }; /* 그림 22.1 은 3, 0 도 정답 */
        CHECK(st[POL_FIFO].hits == 4 && same_ints(ev[POL_FIFO], e_fifo, n));
        CHECK(st[POL_LRU].hits == 6 && same_ints(ev[POL_LRU], e_lru, n));
        CHECK(st[POL_OPT].hits == 6 && same_ints(ev[POL_OPT], e_opt, n));
        CHECK(st[POL_CLOCK].hits == 4 && same_ints(ev[POL_CLOCK], e_fifo, n));   /* 첫 교체 때 use 가 전부 1 → FIFO 와 같아짐 */
        CHECK(st[POL_CLOCK].hand_steps == 4 + 1 + 1 + 4);
        printf("OSTEP ex (3 frames) hits: FIFO=%ld LRU=%ld OPT=%ld CLOCK=%ld\n",
               st[POL_FIFO].hits, st[POL_LRU].hits, st[POL_OPT].hits, st[POL_CLOCK].hits);
    }
    {   /* 두 번째 기회가 통하는 예: 1 2 3 4 2 5 2, 프레임 3 */
        const int r[] = { 1, 2, 3, 4, 2, 5, 2 };
        int ev[4][7]; stats_t st[4];
        for (int p = 0; p < POL_COUNT; p++) sim_run((policy_t)p, r, 7, 3, &st[p], ev[p], NULL);
        const int e_fifo[7] = { -1, -1, -1, 1, -1, 2, 3 };
        const int e_clk[7]  = { -1, -1, -1, 1, -1, 3, -1 };
        CHECK(st[POL_FIFO].hits == 1 && same_ints(ev[POL_FIFO], e_fifo, 7));
        CHECK(st[POL_CLOCK].hits == 2 && same_ints(ev[POL_CLOCK], e_clk, 7));
        CHECK(st[POL_LRU].hits == 2 && same_ints(ev[POL_LRU], e_clk, 7));
        CHECK(st[POL_CLOCK].hand_steps == 4 + 2);
    }
    {   /* 경계: 프레임 1개 → 바로 앞과 같을 때만 hit / 프레임 ≥ 서로 다른 페이지 수 → 미스 = 강제 미스뿐 */
        uint32_t seed = 0xB11u;
        int r[80];
        bool ok1 = true, ok2 = true;
        for (int trial = 0; trial < 50; trial++) {
            for (int i = 0; i < 80; i++) r[i] = (int)(xs32(&seed) % 4);
            long rep = 0;
            for (int i = 1; i < 80; i++) if (r[i] == r[i - 1]) rep++;
            for (int p = 0; p < POL_COUNT; p++) {
                stats_t st;
                sim_run((policy_t)p, r, 80, 1, &st, NULL, NULL);
                if (st.hits != rep) ok1 = false;
                sim_run((policy_t)p, r, 80, 4, &st, NULL, NULL);
                if (st.misses != distinct(r, 80)) ok2 = false;
            }
        }
        CHECK(ok1);
        CHECK(ok2);
    }
    {   /* LRU = 스택 거리 기준값, OPT = 전수 DP 기준값, OPT ≤ 다른 모든 정책 */
        uint32_t seed = 12345u;
        int r[64];
        int lru_bad = 0, opt_bad = 0, opt_worse = 0, cases = 0;
        for (int trial = 0; trial < 300; trial++) {
            int pages = 3 + (int)(xs32(&seed) % 4);                 /* 3..6 */
            long n = 8 + (long)(xs32(&seed) % 9);                   /* 8..16 */
            for (long i = 0; i < n; i++) r[i] = (int)(xs32(&seed) % (uint32_t)pages);
            for (int f = 1; f <= 4; f++) {
                stats_t s[4];
                for (int p = 0; p < POL_COUNT; p++) sim_run((policy_t)p, r, n, f, &s[p], NULL, NULL);
                if (s[POL_LRU].hits != ref_lru_hits(r, n, f)) lru_bad++;
                if (s[POL_OPT].misses != ref_min_misses(r, n, f)) opt_bad++;
                for (int p = 0; p < POL_COUNT; p++) if (s[p].misses < s[POL_OPT].misses) opt_worse++;
                cases++;
            }
        }
        CHECK(lru_bad == 0);
        CHECK(opt_bad == 0);
        CHECK(opt_worse == 0);
        printf("random small strings: %d cases, LRU vs stack-distance mismatches=%d, OPT vs brute-force mismatches=%d\n",
               cases, lru_bad, opt_bad);
    }

    /* ---------------- Part B ---------------- */
    const int bel[] = { 1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5 };
    {
        long ff[8], fl[8], fo[8], fc[8];
        fault_curve(POL_FIFO, bel, 12, 7, ff);
        fault_curve(POL_LRU, bel, 12, 7, fl);
        fault_curve(POL_OPT, bel, 12, 7, fo);
        fault_curve(POL_CLOCK, bel, 12, 7, fc);
        printf("Belady string faults, frames 1..7\n");
        const char *nm[4] = { "FIFO", "LRU", "OPT", "CLOCK" };
        long *cv[4] = { ff, fl, fo, fc };
        for (int k = 0; k < 4; k++) {
            printf("  %-5s", nm[k]);
            for (int f = 1; f <= 7; f++) printf(" %3ld", cv[k][f]);
            printf("\n");
        }
        CHECK(ff[3] == 9 && ff[4] == 10);                           /* 3 프레임 9, 4 프레임 10 */
        CHECK(belady_find(ff, 7) == 3);
        CHECK(belady_find(fl, 7) == 0 && belady_find(fo, 7) == 0);
        CHECK(fl[3] == 10 && fl[4] == 8 && fo[3] == 7 && fo[4] == 6);
        int bf; long bt;
        CHECK(!inclusion_ok(POL_FIFO, bel, 12, 7, &bf, &bt) && bf == 3);
        printf("  FIFO inclusion breaks first at f=%d, t=%ld\n", bf, bt);
        CHECK(inclusion_ok(POL_LRU, bel, 12, 7, &bf, &bt) && inclusion_ok(POL_OPT, bel, 12, 7, &bf, &bt));
    }
    {   /* 무작위 문자열: LRU/OPT 는 언제나 포함 관계 유지, FIFO 는 Belady 사례가 나오고, 나오면 포함 관계가 깨져 있다 */
        uint32_t seed = 2026u;
        int r[30];
        int lru_viol = 0, opt_viol = 0, fifo_anom = 0, fifo_anom_but_incl = 0, mono_bad = 0;
        const int TRIALS = 5000;
        for (int trial = 0; trial < TRIALS; trial++) {
            for (int i = 0; i < 30; i++) r[i] = (int)(xs32(&seed) % 7);
            int bf; long bt;
            if (!inclusion_ok(POL_LRU, r, 30, 7, &bf, &bt)) lru_viol++;
            if (!inclusion_ok(POL_OPT, r, 30, 7, &bf, &bt)) opt_viol++;
            long ff[8], fl[8], fo[8];
            fault_curve(POL_FIFO, r, 30, 7, ff);
            fault_curve(POL_LRU, r, 30, 7, fl);
            fault_curve(POL_OPT, r, 30, 7, fo);
            if (belady_find(fl, 7) != 0 || belady_find(fo, 7) != 0) mono_bad++;
            if (belady_find(ff, 7) != 0) {
                fifo_anom++;
                if (inclusion_ok(POL_FIFO, r, 30, 7, &bf, &bt)) fifo_anom_but_incl++;
            }
        }
        CHECK(lru_viol == 0 && opt_viol == 0 && mono_bad == 0);
        CHECK(fifo_anom > 0);
        CHECK(fifo_anom_but_incl == 0);
        printf("random 30-ref strings (7 pages): %d trials, FIFO Belady cases=%d, LRU/OPT inclusion violations=%d/%d\n",
               TRIALS, fifo_anom, lru_viol, opt_viol);
    }

    /* ---------------- Part C ---------------- */
    {   /* 손 계산: 1 2 3 4 2 3 5, 프레임 3 → Clock 희생자 1(rank 0), 2(rank 1). LRU 라면 1, 4 */
        const int r[] = { 1, 2, 3, 4, 2, 3, 5 };
        long hist[3] = { 0, 0, 0 }; stats_t st = { 0, 0, 0 };
        long ev = clock_lru_rank(r, 7, 3, hist, &st);
        CHECK(ev == 2 && hist[0] == 1 && hist[1] == 1 && hist[2] == 0);
        CHECK(st.hits == 2 && st.misses == 5 && st.hand_steps == 4 + 4);
        int el[7];
        sim_run(POL_LRU, r, 7, 3, NULL, el, NULL);
        CHECK(el[3] == 1 && el[6] == 4);
    }
    static int w[10000];
    const long N = 10000;
    {   /* 반복(looping) 50 페이지: 프레임 < 50 이면 LRU·FIFO·Clock 모두 hit 0. Clock 은 매번 한 바퀴를 돈다 */
        wl_loop(w, N, 50);
        bool zero = true, exact = true, sweep = true, opt_pos = true;
        for (int f = 5; f < 50; f += 5) {
            stats_t s[4];
            for (int p = 0; p < POL_COUNT; p++) sim_run((policy_t)p, w, N, f, &s[p], NULL, NULL);
            if (s[POL_LRU].hits || s[POL_FIFO].hits || s[POL_CLOCK].hits) zero = false;
            if (s[POL_OPT].hits <= 0) opt_pos = false;
            long hist[MAX_FRAMES] = { 0 }; stats_t cs;
            long ev = clock_lru_rank(w, N, f, hist, &cs);
            if (hist[0] != ev) exact = false;                       /* 매번 진짜 LRU 페이지를 고른다 */
            /* f 번 교체마다 한 번은 한 바퀴(f+1칸), 나머지는 1칸 → 평균 2칸 */
            if (cs.hand_steps < 2 * ev || cs.hand_steps > 2 * ev + f) sweep = false;
        }
        CHECK(zero);
        CHECK(opt_pos);
        CHECK(exact && sweep);
    }
    {   /* 지역성 없음(균등 100 페이지): 미래를 모르는 정책은 무엇이든 hit 확률 = f/100 → 셋 다 f%±3%p */
        wl_uniform(w, N, 100, 99u);
        bool near = true;
        printf("uniform workload (100 pages, 10000 refs) hit rate %%\n  frames");
        for (int p = POL_COUNT - 1; p >= 0; p--) printf(" %5s", pol_name[p]);
        printf("\n");
        for (int f = 10; f <= 90; f += 20) {
            stats_t s[4];
            printf("  %6d", f);
            for (int p = POL_COUNT - 1; p >= 0; p--) {
                sim_run((policy_t)p, w, N, f, &s[p], NULL, NULL);
                printf(" %5.1f", 100.0 * (double)s[p].hits / (double)N);
            }
            printf("\n");
            for (int p = 0; p < POL_OPT; p++) {
                double d = 100.0 * (double)s[p].hits / (double)N - f;
                if (d > 3.0 || d < -3.0) near = false;
            }
        }
        CHECK(near);
    }
    {   /* 80-20 (100 페이지, 1만 참조): OPT ≥ 나머지, Clock 은 LRU 에 가깝고 FIFO 보다 낫다 */
        wl_8020(w, N, 100, 7u);
        bool opt_best = true, between = true;
        printf("80-20 workload (100 pages, 10000 refs) hit rate %%\n"
               "  frames   OPT   LRU CLOCK  FIFO  gap-closed%%  victim=LRU%%  mean-rank/f\n");
        for (int f = 10; f <= 90; f += 20) {
            stats_t s[4];
            for (int p = 0; p < POL_COUNT; p++) sim_run((policy_t)p, w, N, f, &s[p], NULL, NULL);
            for (int p = 0; p < POL_COUNT; p++) if (s[p].hits > s[POL_OPT].hits) opt_best = false;
            double hl = 100.0 * (double)s[POL_LRU].hits / (double)N, hc = 100.0 * (double)s[POL_CLOCK].hits / (double)N;
            double hf = 100.0 * (double)s[POL_FIFO].hits / (double)N;
            if (!(hf <= hc && hl - hc < 4.0)) between = false;
            long hist[MAX_FRAMES] = { 0 }; stats_t cs;
            long ev = clock_lru_rank(w, N, f, hist, &cs);
            double rsum = 0;
            for (int k = 0; k < f; k++) rsum += (double)k * (double)hist[k];
            printf("  %6d %5.1f %5.1f %5.1f %5.1f  %10.0f  %10.1f  %10.3f\n", f,
                   100.0 * (double)s[POL_OPT].hits / (double)N, hl, hc, hf,
                   hl > hf ? 100.0 * (hc - hf) / (hl - hf) : 100.0,
                   ev ? 100.0 * (double)hist[0] / (double)ev : 0.0, ev ? rsum / (double)ev / f : 0.0);
        }
        CHECK(opt_best);
        CHECK(between);                          /* 고정 시드 7 에서: FIFO ≤ Clock, LRU - Clock < 4%p (모든 f) */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#else
/* demo_*.c 가 include 할 때: 쓰지 않는 static 함수 경고(-Wunused-function)를 막는다 */
static inline void osb11_keep(void) {
    (void)fault_curve; (void)belady_find; (void)inclusion_ok; (void)clock_lru_rank;
    (void)wl_uniform; (void)wl_8020; (void)wl_loop; (void)pol_name; (void)sim_run;
}
#endif
