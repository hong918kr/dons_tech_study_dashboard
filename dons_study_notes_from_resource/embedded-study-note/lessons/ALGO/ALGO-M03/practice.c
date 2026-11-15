/* ALGO-M03 추가 연습 — 한 번에 검증
 *   연습 1  Combination Sum II (중복 후보, 각 1회, 고유 조합 사전순) vs 마스크 전수 + 정렬·중복 제거
 *   연습 2  N-Queens 반복판 (명시적 스택, 재귀 없음) — n = 0..12 를 OEIS A000170 과 대조
 *   연습 3  N-Queens 좌우 대칭으로 절반만 탐색 — 개수 같음, 노드 수 비교
 *   연습 4  올바른 괄호 생성 — 개수 = Catalan 수 (OEIS A000108), n ≤ 6 은 2^(2n) 전수와 대조
 *   연습 5  회문 분할 개수 (AlgoMonster M05 §5) — 2^(n-1) 자르기 마스크 전수와 대조
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x9E3779B9u;
static uint32_t xorshift32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

static void isort_i32(int32_t *a, size_t n) {
    for (size_t i = 1; i < n; i++) { int32_t v = a[i]; size_t j = i; while (j > 0 && a[j - 1] > v) { a[j] = a[j - 1]; j--; } a[j] = v; }
}

/* ======================= 연습 1: Combination Sum II ======================= */
#define CS_MAXN 16
#define CS_MAXOUT 4096
typedef struct {
    int32_t a[CS_MAXN]; size_t n; int32_t target;
    int32_t path[CS_MAXN]; size_t len;
    int32_t out[CS_MAXOUT][CS_MAXN + 1];     /* out[k][0] = 길이, 그 뒤 값 */
    size_t count;
} cs_ctx_t;

static void cs_rec(cs_ctx_t *c, size_t start, int32_t remain) {
    if (remain == 0) {
        if (c->count < CS_MAXOUT) { c->out[c->count][0] = (int32_t)c->len; memcpy(&c->out[c->count][1], c->path, c->len * sizeof c->path[0]); }
        c->count++;
        return;
    }
    for (size_t i = start; i < c->n; i++) {
        if (i > start && c->a[i] == c->a[i - 1]) continue;   /* 같은 깊이에서 같은 값은 한 번만 시작 */
        if (c->a[i] > remain) break;                           /* 정렬됨: 뒤는 더 크다 */
        c->path[c->len++] = c->a[i];                           /* choose */
        cs_rec(c, i + 1, remain - c->a[i]);                    /* explore (i+1: 각 원소 1회) */
        c->len--;                                              /* unchoose */
    }
}
static cs_ctx_t g_cs, g_csref;

/* 기준: 모든 마스크 → 정렬된 값 열 → 사전순 정렬 → 중복 제거 */
static int cmp_combo(const int32_t *x, const int32_t *y) {
    int32_t lx = x[0], ly = y[0];
    for (int32_t i = 1; i <= lx && i <= ly; i++) if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return (lx > ly) - (lx < ly);
}
static size_t cs_ref(const int32_t *a, size_t n, int32_t target) {
    size_t k = 0;
    for (uint32_t m = 1; m < (1u << n); m++) {
        int64_t s = 0; int32_t v[CS_MAXN]; size_t len = 0;
        for (size_t i = 0; i < n; i++) if (m >> i & 1u) { s += a[i]; v[len++] = a[i]; }
        if (s != target) continue;
        isort_i32(v, len);
        g_csref.out[k][0] = (int32_t)len; memcpy(&g_csref.out[k][1], v, len * sizeof v[0]); k++;
    }
    for (size_t i = 1; i < k; i++) {                          /* 삽입 정렬 (k 작음) */
        int32_t tmp[CS_MAXN + 1]; memcpy(tmp, g_csref.out[i], sizeof tmp); size_t j = i;
        while (j > 0 && cmp_combo(g_csref.out[j - 1], tmp) > 0) { memcpy(g_csref.out[j], g_csref.out[j - 1], sizeof tmp); j--; }
        memcpy(g_csref.out[j], tmp, sizeof tmp);
    }
    size_t u = 0;
    for (size_t i = 0; i < k; i++) if (u == 0 || cmp_combo(g_csref.out[u - 1], g_csref.out[i]) != 0) memcpy(g_csref.out[u++], g_csref.out[i], sizeof g_csref.out[0]);
    return u;
}
static size_t combo_sum2(const int32_t *a, size_t n, int32_t target) {
    memset(&g_cs, 0, sizeof g_cs);
    g_cs.n = n; g_cs.target = target;
    memcpy(g_cs.a, a, n * sizeof a[0]); isort_i32(g_cs.a, n);
    cs_rec(&g_cs, 0, target);
    return g_cs.count;
}

/* ======================= 연습 2·3: N-Queens 반복판 / 대칭 ======================= */
/* 반복판: 스택에 (cols, d1, d2, avail) 를 행마다 한 칸. 깊이 = 행 번호 */
static uint64_t nq_iter(unsigned n, uint64_t *nodes) {
    struct { uint32_t cols, d1, d2, avail; } st[16];
    if (n == 0) { if (nodes) *nodes = 1; return 1; }
    uint32_t full = (1u << n) - 1;
    uint64_t cnt = 0, nd = 1;
    int top = 0;
    st[0].cols = st[0].d1 = st[0].d2 = 0; st[0].avail = full;
    while (top >= 0) {
        if (st[top].avail == 0) { top--; continue; }           /* 이 행의 후보 소진 → 되돌아가기 (unchoose) */
        uint32_t bit = st[top].avail & (0u - st[top].avail);
        st[top].avail ^= bit;                                  /* choose */
        uint32_t c = st[top].cols | bit, a = (st[top].d1 | bit) << 1, b = (st[top].d2 | bit) >> 1;
        nd++;
        if (c == full) { cnt++; continue; }                    /* 마지막 행까지 놓음 */
        top++;                                                 /* explore: 다음 행 프레임 push */
        st[top].cols = c; st[top].d1 = a; st[top].d2 = b; st[top].avail = full & ~(c | a | b);
    }
    if (nodes) *nodes = nd;
    return cnt;
}
static uint64_t nq_rec(uint32_t full, uint32_t cols, uint32_t d1, uint32_t d2, uint64_t *nodes) {
    (*nodes)++;
    if (cols == full) return 1;
    uint64_t cnt = 0;
    for (uint32_t avail = full & ~(cols | d1 | d2); avail; avail &= avail - 1) {
        uint32_t bit = avail & (0u - avail);
        cnt += nq_rec(full, cols | bit, (d1 | bit) << 1, (d2 | bit) >> 1, nodes);
    }
    return cnt;
}
/* 대칭판: 첫 행은 왼쪽 절반 열만 → ×2. n 이 홀수면 가운데 열을 따로 (그 해들은 거울상이 서로 다른
 * 가운데-시작 해이므로 ×2 하지 않는다). */
static uint64_t nq_sym(unsigned n, uint64_t *nodes) {
    if (n == 0) { *nodes = 1; return 1; }
    uint32_t full = (1u << n) - 1;
    uint64_t cnt = 0, nd = 1;
    for (unsigned c = 0; c < n / 2; c++) { uint32_t bit = 1u << c; cnt += 2 * nq_rec(full, bit, bit << 1, bit >> 1, &nd); }
    if (n & 1u) { uint32_t bit = 1u << (n / 2); cnt += nq_rec(full, bit, bit << 1, bit >> 1, &nd); }
    *nodes = nd;
    return cnt;
}

/* ======================= 연습 4: 올바른 괄호 ======================= */
static char  pa_buf[32];
static size_t pa_count; static int pa_bad; static char pa_prev[32];
static void paren_rec(size_t pos, unsigned open, unsigned close, unsigned n) {
    if (pos == 2 * n) {
        pa_buf[pos] = '\0';
        if (pa_count > 0 && strcmp(pa_prev, pa_buf) >= 0) pa_bad = 1;   /* 사전순·중복 없음 확인 */
        memcpy(pa_prev, pa_buf, pos + 1);
        pa_count++;
        return;
    }
    if (open < n)     { pa_buf[pos] = '('; paren_rec(pos + 1, open + 1, close, n); }   /* '(' 를 먼저: 사전순 */
    if (close < open) { pa_buf[pos] = ')'; paren_rec(pos + 1, open, close + 1, n); }
}
static size_t paren_brute(unsigned n) {
    size_t k = 0;
    for (uint32_t m = 0; m < (1u << (2 * n)); m++) {        /* 비트 1 = '(' */
        int bal = 0, ok = 1;
        for (unsigned i = 0; i < 2 * n && ok; i++) { bal += (m >> i & 1u) ? 1 : -1; if (bal < 0) ok = 0; }
        k += (size_t)(ok && bal == 0);
    }
    return k;
}

/* ======================= 연습 5: 회문 분할 개수 ======================= */
static int is_pal(const char *s, size_t i, size_t j) {        /* s[i..j) */
    while (i + 1 < j) { if (s[i] != s[j - 1]) return 0; i++; j--; }
    return 1;
}
static uint64_t pal_rec(const char *s, size_t n, size_t start) {
    if (start == n) return 1;
    uint64_t cnt = 0;
    for (size_t end = start + 1; end <= n; end++)
        if (is_pal(s, start, end)) cnt += pal_rec(s, n, end);  /* 회문이 아닌 조각에서 바로 가지치기 */
    return cnt;
}
static uint64_t pal_brute(const char *s, size_t n) {
    if (n == 0) return 1;
    uint64_t k = 0;
    for (uint32_t cut = 0; cut < (1u << (n - 1)); cut++) {     /* 비트 i = s[i] 와 s[i+1] 사이를 자름 */
        size_t st = 0; int ok = 1;
        for (size_t i = 0; i < n && ok; i++)
            if (i == n - 1 || (cut >> i & 1u)) { if (!is_pal(s, st, i + 1)) ok = 0; st = i + 1; }
        k += (uint64_t)ok;
    }
    return k;
}

int main(void) {
    /* ---- 연습 1 ---- */
    {
        const int32_t ex1[7] = { 10, 1, 2, 7, 6, 1, 5 };       /* LeetCode 40 예: target 8 → [1,1,6],[1,2,5],[1,7],[2,6] */
        size_t k = combo_sum2(ex1, 7, 8);
        const int32_t w0[4] = { 3, 1, 1, 6 }, w1[4] = { 3, 1, 2, 5 }, w2[3] = { 2, 1, 7 }, w3[3] = { 2, 2, 6 };
        CHECK(k == 4 && !memcmp(g_cs.out[0], w0, sizeof w0) && !memcmp(g_cs.out[1], w1, sizeof w1)
                     && !memcmp(g_cs.out[2], w2, sizeof w2) && !memcmp(g_cs.out[3], w3, sizeof w3));
        const int32_t ones[6] = { 1, 1, 1, 1, 1, 1 };
        CHECK(combo_sum2(ones, 6, 3) == 1);                     /* 마스크로는 C(6,3) = 20 개, 고유 조합은 1 개 */
        unsigned long bad = 0, sols = 0;
        int32_t a[12];
        for (int it = 0; it < 300; it++) {
            size_t n = 1 + xorshift32() % 12;
            for (size_t i = 0; i < n; i++) a[i] = 1 + (int32_t)(xorshift32() % 6);
            int32_t t = 1 + (int32_t)(xorshift32() % 20);
            size_t kr = cs_ref(a, n, t), kg = combo_sum2(a, n, t);
            sols += kr;
            if (kg != kr) { bad++; continue; }
            for (size_t i = 0; i < kg; i++) if (cmp_combo(g_cs.out[i], g_csref.out[i]) != 0) { bad++; break; }
        }
        printf("연습 1: 300 random instances, %lu unique combinations, mismatches=%lu\n", sols, bad);
        CHECK(bad == 0);
    }
    /* ---- 연습 2·3 ---- */
    {
        static const uint64_t a000170[13] = { 1, 1, 0, 0, 2, 10, 4, 40, 92, 352, 724, 2680, 14200 };
        int ok_it = 1, ok_sym = 1;
        uint64_t nd_rec12 = 0, nd_it12 = 0, nd_sym12 = 0;
        for (unsigned n = 0; n <= 12; n++) {
            uint64_t ni, ns, nr = 0;
            if (nq_iter(n, &ni) != a000170[n]) ok_it = 0;
            if (nq_sym(n, &ns) != a000170[n]) ok_sym = 0;
            uint64_t cr = nq_rec(n ? (1u << n) - 1 : 0, 0, 0, 0, &nr);
            if (cr != a000170[n] || ni != nr) ok_it = 0;          /* 반복판 노드 수 = 재귀판 호출 수 */
            if (n == 12) { nd_rec12 = nr; nd_it12 = ni; nd_sym12 = ns; }
        }
        printf("연습 2: iterative N-Queens n=0..12 %s, nodes n=12: recursive=%llu iterative=%llu, stack = 12 x %zu B\n",
               ok_it ? "== A000170" : "MISMATCH", (unsigned long long)nd_rec12, (unsigned long long)nd_it12, (size_t)16);
        printf("연습 3: mirror-halved N-Queens n=0..12 %s, nodes n=12: full=%llu halved=%llu\n",
               ok_sym ? "== A000170" : "MISMATCH", (unsigned long long)nd_rec12, (unsigned long long)nd_sym12);
        CHECK(ok_it);
        CHECK(ok_sym);
        CHECK(nd_sym12 < nd_rec12);
    }
    /* ---- 연습 4 ---- */
    {
        static const uint64_t catalan[13] = { 1, 1, 2, 5, 14, 42, 132, 429, 1430, 4862, 16796, 58786, 208012 };
        int ok = 1;
        for (unsigned n = 0; n <= 12; n++) {
            pa_count = 0; pa_bad = 0;
            paren_rec(0, 0, 0, n);
            if (pa_count != catalan[n] || pa_bad) ok = 0;
            if (n <= 6 && paren_brute(n) != catalan[n]) ok = 0;
        }
        pa_count = 0; paren_rec(0, 0, 0, 3);
        printf("연습 4: parentheses n=0..12 == Catalan (A000108): %s; n=3 last = %s\n", ok ? "yes" : "NO", pa_prev);
        CHECK(ok);
        CHECK(strcmp(pa_prev, "()()()") == 0);
    }
    /* ---- 연습 5 ---- */
    {
        CHECK(pal_rec("aab", 3, 0) == 2);                       /* a|a|b, aa|b */
        CHECK(pal_rec("", 0, 0) == 1);
        CHECK(pal_rec("aaaa", 4, 0) == 8);                      /* 같은 글자 n 개: 2^(n-1) */
        char s[17];
        unsigned long bad = 0;
        for (int it = 0; it < 400; it++) {
            size_t n = xorshift32() % 17;
            for (size_t i = 0; i < n; i++) s[i] = (char)('a' + xorshift32() % 2);
            s[n] = '\0';
            if (pal_rec(s, n, 0) != pal_brute(s, n)) bad++;
        }
        printf("연습 5: 400 random strings over {a,b}, len<=16: mismatches=%lu\n", bad);
        CHECK(bad == 0);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
