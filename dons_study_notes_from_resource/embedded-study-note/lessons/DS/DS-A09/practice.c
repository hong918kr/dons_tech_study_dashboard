/* DS-A09 추가 연습 — FTL 주변 자료구조 (자체 포함: solution.c 를 include 하지 않는다)
 *
 * 작은 FTL 하나(16 블록 × 32 페이지, 프런티어 1개)를 돌리면서 다음을 확인한다.
 *   연습 1  valid-count 버킷 리스트: 무효화 O(1), greedy pick O(PG) — 선형 탐색과 일치
 *   연습 2  정확한 cost-benefit 을 O(PG) 에 — age 의 기준을 "마지막 무효화"로 잡으면 버킷 안이 항상 시간순이다
 *   연습 3  P2L + 시퀀스 번호로 L2P 를 다시 만들기 (그리고 TRIM 이 왜 여기서 안 보이는가)
 *   연습 4  비트맵 없이 유효 판정: valid(ppa) ⇔ L2P[P2L[ppa]] == ppa
 *   연습 5  해석식 재현: Desnoyers 2012 Eq.4 / Eq.11–12 를 libm 없이 계산해 논문 표와 대조
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) { g_pass++; printf("PASS %s\n", #e); } \
                      else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)

#define NB   16u
#define PG   32u
#define NPG  (NB * PG)
#define NLPN (NB * PG * 3u / 4u)
#define NONE UINT32_MAX

enum { ST_FREE, ST_OPEN, ST_CLOSED };
static uint32_t l2p[NLPN], p2l[NPG], seqno[NPG], seq_next;
static uint32_t bm[NB], vcnt[NB], wp[NB], st[NB];
static uint32_t fq[NB], fq_h, fq_n, cur;
static uint64_t now;

/* ===== 연습 1, 2 — valid-count 버킷 (침입형 이중 연결 리스트) ===== */
static uint32_t bk_head[PG + 1u], bk_tail[PG + 1u], bk_next[NB], bk_prev[NB], bk_in[NB];
static uint64_t last_inv[NB];                 /* 버킷에 들어온 시각 = close 또는 마지막 무효화 */

static void bk_reset(void) {
    memset(bk_head, 0xFF, sizeof bk_head); memset(bk_tail, 0xFF, sizeof bk_tail);
    memset(bk_in, 0xFF, sizeof bk_in);
}
static void bk_append(uint32_t b) {
    uint32_t v = vcnt[b];
    bk_in[b] = v; bk_prev[b] = bk_tail[v]; bk_next[b] = NONE;
    if (bk_tail[v] != NONE) bk_next[bk_tail[v]] = b; else bk_head[v] = b;
    bk_tail[v] = b;
    last_inv[b] = now;
}
static void bk_remove(uint32_t b) {
    uint32_t v = bk_in[b];
    if (bk_prev[b] != NONE) bk_next[bk_prev[b]] = bk_next[b]; else bk_head[v] = bk_next[b];
    if (bk_next[b] != NONE) bk_prev[bk_next[b]] = bk_prev[b]; else bk_tail[v] = bk_prev[b];
    bk_in[b] = NONE;
}
static uint64_t cb_score(uint32_t v, uint64_t age) { return (age + 1u) * (uint64_t)(PG - v) * 1024u / (PG + v); }

static uint32_t g_scan_max;
/* 연습 1: 가장 작은 valid 수 버킷의 머리. */
static uint32_t pick_greedy(void) {
    for (uint32_t v = 0; v <= PG; v++) if (bk_head[v] != NONE) return bk_head[v];
    return NONE;
}
/* 연습 2: 각 버킷의 머리만 본다 — 같은 v 안에서는 가장 오래된 것이 최고점. work ≤ PG+1 */
static uint32_t pick_cb_exact(void) {
    uint32_t best = NONE, work = 0;
    uint64_t best_score = 0;
    for (uint32_t v = 0; v <= PG; v++) {
        work++;
        uint32_t b = bk_head[v];
        if (b == NONE) continue;
        uint64_t age = now - last_inv[b];
        uint64_t s = (v == 0) ? UINT64_MAX : cb_score(v, age);
        if (best == NONE || s > best_score) { best = b; best_score = s; }
    }
    if (work > g_scan_max) g_scan_max = work;
    return best;
}
static uint64_t score_of(uint32_t b) {
    return vcnt[b] == 0 ? UINT64_MAX : cb_score(vcnt[b], now - last_inv[b]);
}
static bool bk_sorted(void) {                 /* 모든 버킷이 시각 오름차순이고 v 가 맞는가 */
    for (uint32_t v = 0; v <= PG; v++)
        for (uint32_t b = bk_head[v]; b != NONE; b = bk_next[b]) {
            if (vcnt[b] != v) return false;
            if (bk_next[b] != NONE && last_inv[bk_next[b]] < last_inv[b]) return false;
        }
    return true;
}

/* ===== 작은 FTL ===== */
static int g_pick_mode;                       /* 0 = 연습1 greedy, 1 = 연습2 exact CB */
static uint32_t g_pick_mismatch, g_bk_bad, g_validity_bad, g_picks;

static void ft_reset(int pick_mode) {
    memset(l2p, 0xFF, sizeof l2p); memset(p2l, 0xFF, sizeof p2l);
    memset(seqno, 0, sizeof seqno); memset(bm, 0, sizeof bm);
    memset(vcnt, 0, sizeof vcnt); memset(wp, 0, sizeof wp); memset(st, 0, sizeof st);
    bk_reset();
    fq_h = 0; fq_n = NB; for (uint32_t b = 0; b < NB; b++) fq[b] = b;
    cur = NONE; seq_next = 1u; now = 0;
    g_pick_mode = pick_mode; g_scan_max = 0;
    g_pick_mismatch = g_bk_bad = g_validity_bad = g_picks = 0;
}
static uint32_t fq_pop(void) { uint32_t b = fq[fq_h]; fq_h = (fq_h + 1u) % NB; fq_n--; return b; }
static void fq_push(uint32_t b) { fq[(fq_h + fq_n) % NB] = b; fq_n++; }

static uint32_t program(uint32_t b, uint32_t lpn) {
    uint32_t ppa = b * PG + wp[b]++;
    p2l[ppa] = lpn; seqno[ppa] = seq_next++;
    return ppa;
}
static void set_valid(uint32_t ppa) { uint32_t b = ppa / PG; bm[b] |= 1u << (ppa % PG); vcnt[b]++; }
static void clr_valid(uint32_t ppa) {
    uint32_t b = ppa / PG;
    if (st[b] == ST_CLOSED) bk_remove(b);
    bm[b] &= ~(1u << (ppa % PG)); vcnt[b]--;
    if (st[b] == ST_CLOSED) bk_append(b);     /* 연습 2: 맨 뒤에 붙이면 정렬이 유지된다 */
}
static void close_b(uint32_t b) { st[b] = ST_CLOSED; bk_append(b); }

/* 연습 4: 비트맵 없이 유효 판정 */
static bool valid_by_map(uint32_t ppa) {
    uint32_t b = ppa / PG;
    if (ppa % PG >= wp[b]) return false;
    uint32_t lpn = p2l[ppa];
    return lpn < NLPN && l2p[lpn] == ppa;
}
static bool valid_by_bit(uint32_t ppa) { return (bm[ppa / PG] >> (ppa % PG)) & 1u; }

static void gc_once(void) {
    g_picks++;
    if (!bk_sorted()) g_bk_bad++;
    /* 버킷 선택을 선형 탐색과 대조 */
    uint32_t brute_v = NONE; uint64_t brute_s = 0;
    for (uint32_t b = 0; b < NB; b++) if (st[b] == ST_CLOSED) {
        if (vcnt[b] < brute_v) brute_v = vcnt[b];
        if (score_of(b) > brute_s) brute_s = score_of(b);
    }
    uint32_t gv = pick_greedy(), cv = pick_cb_exact();
    if (gv == NONE || vcnt[gv] != brute_v) g_pick_mismatch++;
    if (cv == NONE || score_of(cv) != brute_s) g_pick_mismatch++;
    uint32_t victim = g_pick_mode ? cv : gv;
    bk_remove(victim);
    /* 연습 4: 매 GC 마다 비트맵 판정과 맵 판정을 전 페이지 대조 */
    for (uint32_t ppa = 0; ppa < NPG; ppa++) if (valid_by_bit(ppa) != valid_by_map(ppa)) g_validity_bad++;
    st[victim] = ST_OPEN;
    cur = fq_pop();
    st[cur] = ST_OPEN;
    for (uint32_t p = 0; p < PG; p++) {
        uint32_t ppa = victim * PG + p;
        if (!valid_by_bit(ppa)) continue;
        uint32_t lpn = p2l[ppa], n = program(cur, lpn);
        l2p[lpn] = n; set_valid(n); clr_valid(ppa);
    }
    memset(&p2l[victim * PG], 0xFF, PG * sizeof p2l[0]);
    memset(&seqno[victim * PG], 0, PG * sizeof seqno[0]);
    wp[victim] = 0; st[victim] = ST_FREE; fq_push(victim);
}
static void ensure_frontier(void) {
    if (cur != NONE && wp[cur] < PG) return;
    if (cur != NONE) { close_b(cur); cur = NONE; }
    if (fq_n > 1u) { cur = fq_pop(); st[cur] = ST_OPEN; return; }
    gc_once();
}
static void host_write(uint32_t lpn) {
    ensure_frontier();
    uint32_t old = l2p[lpn], n = program(cur, lpn);
    l2p[lpn] = n; set_valid(n);
    if (old != NONE) clr_valid(old);
    now++;
}

static uint64_t rng_state;
static uint32_t rng32(void) {
    uint64_t z = (rng_state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return (uint32_t)((z ^ (z >> 31)) >> 32);
}

/* 연습 3: P2L + 시퀀스 번호만으로 L2P 재구성 */
static uint32_t rebuilt[NLPN];
static void rebuild_l2p(void) {
    static uint32_t best_seq[NLPN];
    memset(best_seq, 0, sizeof best_seq);
    memset(rebuilt, 0xFF, sizeof rebuilt);
    for (uint32_t ppa = 0; ppa < NPG; ppa++) {
        uint32_t b = ppa / PG, lpn = p2l[ppa];
        if (ppa % PG >= wp[b] || lpn >= NLPN) continue;
        if (seqno[ppa] > best_seq[lpn]) { best_seq[lpn] = seqno[ppa]; rebuilt[lpn] = ppa; }
    }
}
static bool rebuilt_matches_live(void) { return memcmp(rebuilt, l2p, sizeof rebuilt) == 0; }

/* 연습 5: libm 없는 수치 계산 */
static double my_exp(double x) {          /* 축소 후 테일러, 제곱 복원 */
    int sq = 0;
    while (x > 0.5 || x < -0.5) { x /= 2.0; sq++; }
    double term = 1.0, sum = 1.0;
    for (int i = 1; i < 20; i++) { term *= x / i; sum += term; }
    while (sq-- > 0) sum *= sum;
    return sum;
}
/* W_0(y), y ∈ (-1/e, 0): w·e^w 는 [-1,0] 에서 단조 증가 → 이분법 */
static double lambert_w0(double y) {
    double lo = -1.0, hi = 0.0;
    for (int i = 0; i < 200; i++) {
        double m = (lo + hi) * 0.5;
        if (m * my_exp(m) < y) lo = m; else hi = m;
    }
    return (lo + hi) * 0.5;
}
static double eq4_lru(double a) { return a / (a + lambert_w0(-a * my_exp(-a))); }
static double eq12_greedy(double a, double np) {
    double c = (1.0 + 1.0 / (2.0 * np)) * a;
    double x0 = 0.5 - (np / a) * lambert_w0(-c * my_exp(-c));
    return np / (np - (x0 - 1.0));
}
static bool near(double got, double want, double tol) { double d = got - want; return (d < 0 ? -d : d) <= tol; }

static void run_workload(int mode, uint32_t writes) {
    ft_reset(mode);
    rng_state = 0xABCDu + (uint64_t)mode;
    for (uint32_t i = 0; i < NLPN; i++) host_write(i);
    for (uint32_t i = 0; i < writes; i++) host_write(rng32() % NLPN);
}

int main(void) {
    puts("== 연습 1/2/4: buckets vs brute force, bitmap-free validity");
    for (int mode = 0; mode < 2; mode++) {
        run_workload(mode, 60000u);
        CHECK(g_picks > 500u);
        CHECK(g_bk_bad == 0u);
        CHECK(g_pick_mismatch == 0u);
        CHECK(g_scan_max <= PG + 1u);
        CHECK(g_validity_bad == 0u);
        printf("mode %d: %u picks, max bucket scan %u (PG+1 = %u)\n", mode, g_picks, g_scan_max, PG + 1u);
    }

    puts("== 연습 3: rebuild L2P from P2L + seq");
    run_workload(0, 20000u);
    rebuild_l2p();
    CHECK(rebuilt_matches_live());
    uint32_t trimmed = l2p[17];
    CHECK(trimmed != NONE);
    clr_valid(trimmed);
    l2p[17] = NONE;                                  /* TRIM: 플래시에는 아무것도 안 쓴다 */
    rebuild_l2p();
    CHECK(!rebuilt_matches_live());                  /* TRIM 한 LPN 이 되살아난다 */
    CHECK(rebuilt[17] == trimmed);

    puts("== 연습 5: Desnoyers 2012 tables (paper values, not simulated here)");
    /* Table 1 (LRU, Eq.4): Sf = (T-U)/T, α = 1/(1-Sf) */
    CHECK(near(eq4_lru(1.0 / (1.0 - 0.03)), 16.837, 0.005));
    CHECK(near(eq4_lru(1.0 / (1.0 - 0.07)), 7.318, 0.005));
    CHECK(near(eq4_lru(1.0 / (1.0 - 0.23)), 2.371, 0.005));
    /* Eq.11–12 (greedy, Np = 64): Table 2 는 free-list 보정 포함 값이고, 본문이 "보정 없이 Sf=0.03 이면 13.393" 이라고 준다 */
    CHECK(near(eq12_greedy(1.0 / (1.0 - 0.03), 64.0), 13.393, 0.005));
    CHECK(near(eq12_greedy(1.0 / (1.0 - 0.17), 64.0), 3.002, 0.01));   /* Sf 이 크면 보정 영향이 작다 (Table 2 값) */
    printf("Eq.4 (Sf 0.07) = %.3f   Eq.12 (Sf 0.07, Np 64) = %.3f (Table 2 with free-list correction: 6.623)\n",
           eq4_lru(1.0 / 0.93), eq12_greedy(1.0 / 0.93, 64.0));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
