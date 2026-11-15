/* ALGO-A04 추가 연습 — 한 번에 검증
 *   연습 1  m ≤ 8 이면 시프트 레지스터 하나: (w << 8 | b) & mask == pat  + "창이 찼나" 카운터
 *   연습 2  강한 실패 함수(KMP 의 next): 바이트당 최악 비교 수를 전수로 재기 (MP vs KMP)
 *   연습 3  Aho–Corasick: 동기 워드 여러 개를 한 번에 (256-폭 goto 표 = DFA)
 *   연습 4  Penney / Conway: KMP 자동자로 "패턴이 처음 나올 때까지 기대 바이트 수" = Σ A^k (경계 k)
 *   연습 5  애너그램 창: 패턴의 바이트 다중집합과 같은 창을 O(1)/바이트로 (AlgoMonster M03 §15)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr -lm && ./pr
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(c) do { g_total++; if (c) g_pass++; else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } while (0)
static uint32_t g_rng = 88172645u;
static uint32_t xs32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

#define MAXM 64
#define TN 20000
static uint8_t g_t[TN];

static int naive_find(const uint8_t *t, int n, const uint8_t *p, int m, uint32_t *ends, int cap) {
    int ne = 0;
    for (int i = 0; i + m <= n; i++)
        if (memcmp(t + i, p, (size_t)m) == 0 && ne < cap) ends[ne++] = (uint32_t)(i + m - 1);
    return ne;
}
static bool same_ends(const uint32_t *a, int na, const uint32_t *b, int nb) {
    return na == nb && (na == 0 || memcmp(a, b, (size_t)na * sizeof *a) == 0);
}
static void kmp_build(const uint8_t *p, int m, uint8_t *fail) {
    fail[0] = 0;
    for (int i = 1, k = 0; i < m; i++) {
        while (k > 0 && p[i] != p[k]) k = fail[k - 1];
        if (p[i] == p[k]) k++;
        fail[i] = (uint8_t)k;
    }
}

/* ---------------- 연습 1: 시프트 레지스터 ---------------- */
typedef struct { uint64_t w, mask, pat; uint8_t m, filled; } sr_t;
static void sr_init(sr_t *s, const uint8_t *p, int m) {      /* 1 ≤ m ≤ 8 */
    memset(s, 0, sizeof *s);
    s->m = (uint8_t)m;
    s->mask = m == 8 ? ~0ull : ((1ull << (8 * m)) - 1);
    for (int i = 0; i < m; i++) s->pat = (s->pat << 8) | p[i];
}
static bool sr_push(sr_t *s, uint8_t b, bool use_filled) {
    s->w = (s->w << 8) | b;
    if (s->filled < s->m) s->filled++;
    return (!use_filled || s->filled == s->m) && (s->w & s->mask) == s->pat;
}

/* ---------------- 연습 2: MP(fail) vs KMP(strong) 바이트당 비교 수 ---------------- */
/* strong[q] (q<m): p[q] 에서 불일치했을 때 다음 비교 위치. p[fail]==p[q] 이면 같은 비교를 또 할 이유가 없다 → 건너뜀.
 * -1 = 이 바이트로는 어느 접두사도 못 늘린다 (q=0 으로 가고 바이트 소비). */
static void strong_build(const uint8_t *p, int m, const uint8_t *fail, int16_t *strong) {
    strong[0] = -1;
    for (int q = 1; q < m; q++) {
        int r = fail[q - 1];
        strong[q] = (p[r] == p[q]) ? strong[r] : (int16_t)r;
    }
}
/* 상태 q(0..m-1)에서 바이트 c 를 처리: 새 상태와 비교 횟수 */
static int mp_step(const uint8_t *p, const uint8_t *fail, int q, uint8_t c, int *cmp) {
    *cmp = 0;
    for (;;) {
        (*cmp)++;
        if (p[q] == c) return q + 1;
        if (q == 0) return 0;
        q = fail[q - 1];
    }
}
static int kmp_step(const uint8_t *p, const int16_t *strong, int q, uint8_t c, int *cmp) {
    *cmp = 0;
    int x = q;
    while (x >= 0) { (*cmp)++; if (p[x] == c) return x + 1; x = strong[x]; }
    return 0;
}
static int run_strong(const uint8_t *p, int m, const uint8_t *fail, const int16_t *strong,
                      const uint8_t *t, int n, uint32_t *ends, int cap) {
    int q = 0, ne = 0, cmp;
    for (int i = 0; i < n; i++) {
        if (q == m) q = fail[m - 1];
        q = kmp_step(p, strong, q, t[i], &cmp);
        if (q == m && ne < cap) ends[ne++] = (uint32_t)i;
    }
    return ne;
}
/* 패턴 하나의 바이트당 최악 비교 수 (모든 상태 × 모든 바이트 종류) */
static void worst_delay(const uint8_t *p, int m, int *mp_max, int *kmp_max) {
    uint8_t fail[MAXM]; int16_t strong[MAXM];
    kmp_build(p, m, fail); strong_build(p, m, fail, strong);
    *mp_max = *kmp_max = 0;
    for (int q = 0; q < m; q++)
        for (int c = 0; c < 256; c++) {
            int a, b;
            mp_step(p, fail, q, (uint8_t)c, &a); kmp_step(p, strong, q, (uint8_t)c, &b);
            if (a > *mp_max) *mp_max = a;
            if (b > *kmp_max) *kmp_max = b;
        }
}

/* ---------------- 연습 3: Aho–Corasick (DFA 형태) ---------------- */
#define AC_MAXS 128
typedef struct { uint8_t go[AC_MAXS][256]; uint8_t link[AC_MAXS]; uint32_t out[AC_MAXS]; int ns; } ac_t;
static ac_t g_ac;
static int ac_build(ac_t *a, const uint8_t *const *pats, const int *lens, int k) {
    memset(a, 0, sizeof *a);
    static int16_t tr[AC_MAXS][256];
    memset(tr, -1, sizeof tr);
    a->ns = 1;
    for (int i = 0; i < k; i++) {                       /* 1) 트라이 */
        int v = 0;
        for (int j = 0; j < lens[i]; j++) {
            if (tr[v][pats[i][j]] < 0) { if (a->ns == AC_MAXS) return -1; tr[v][pats[i][j]] = (int16_t)a->ns++; }
            v = tr[v][pats[i][j]];
        }
        a->out[v] |= 1u << i;
    }
    int qu[AC_MAXS], qh = 0, qt = 0;                    /* 2) BFS: 실패 링크 + goto 완성 (KMP DFA 를 트리로) */
    for (int c = 0; c < 256; c++) {
        if (tr[0][c] >= 0) { a->go[0][c] = (uint8_t)tr[0][c]; a->link[tr[0][c]] = 0; qu[qt++] = tr[0][c]; }
        else a->go[0][c] = 0;
    }
    while (qh < qt) {
        int v = qu[qh++];
        a->out[v] |= a->out[a->link[v]];                /* 접미사로 끝나는 짧은 패턴도 함께 보고 */
        for (int c = 0; c < 256; c++) {
            if (tr[v][c] >= 0) { int u = tr[v][c]; a->link[u] = a->go[a->link[v]][c]; a->go[v][c] = (uint8_t)u; qu[qt++] = u; }
            else a->go[v][c] = a->go[a->link[v]][c];
        }
    }
    return 0;
}

/* ---------------- 연습 4: 기대 대기 시간 ---------------- */
/* E_q = 1 + (1/A) Σ_c E_{δ(q,c)},  E_m = 0.  알파벳 A 글자('a'..). 가우스 소거 (m ≤ 12). */
static double expected_wait_markov(const uint8_t *p, int m, int A) {
    uint8_t fail[MAXM]; kmp_build(p, m, fail);
    double M[MAXM][MAXM + 1];
    memset(M, 0, sizeof M);
    for (int q = 0; q < m; q++) {
        M[q][q] += 1.0; M[q][m] = 1.0;
        for (int c = 0; c < A; c++) {
            int cmp, x = mp_step(p, fail, q, (uint8_t)('a' + c), &cmp);
            if (x < m) M[q][x] -= 1.0 / A;
        }
    }
    for (int i = 0; i < m; i++) {                        /* 부분 피벗 */
        int piv = i;
        for (int r = i + 1; r < m; r++) if (fabs(M[r][i]) > fabs(M[piv][i])) piv = r;
        for (int c = 0; c <= m; c++) { double t = M[i][c]; M[i][c] = M[piv][c]; M[piv][c] = t; }
        for (int r = 0; r < m; r++) if (r != i) {
            double f = M[r][i] / M[i][i];
            for (int c = i; c <= m; c++) M[r][c] -= f * M[i][c];
        }
    }
    return M[0][m] / M[0][0];
}
static double expected_wait_borders(const uint8_t *p, int m, double A) {   /* Σ_{k: p[0..k)=p[m-k..m)} A^k */
    double s = 0;
    for (int k = 1; k <= m; k++) if (memcmp(p, p + m - k, (size_t)k) == 0) s += pow(A, k);
    return s;
}

/* ---------------- 연습 5: 애너그램 창 ---------------- */
static int anagram_find(const uint8_t *t, int n, const uint8_t *p, int m, uint32_t *ends, int cap) {
    int diff[256] = {0}, nz = 0, ne = 0;             /* diff[c] = 창의 c 개수 − 패턴의 c 개수, nz = 0 아닌 칸 수 */
    for (int i = 0; i < m; i++) { if (diff[p[i]] == 0) nz++; diff[p[i]]--; if (diff[p[i]] == 0) nz--; }
    for (int i = 0; i < n; i++) {
        uint8_t c = t[i];
        if (diff[c] == 0) nz++; diff[c]++; if (diff[c] == 0) nz--;
        if (i >= m) { uint8_t o = t[i - m]; if (diff[o] == 0) nz++; diff[o]--; if (diff[o] == 0) nz--; }
        if (i >= m - 1 && nz == 0 && ne < cap) ends[ne++] = (uint32_t)i;
    }
    return ne;
}
static int anagram_brute(const uint8_t *t, int n, const uint8_t *p, int m, uint32_t *ends, int cap) {
    int cp[256] = {0}, ne = 0;
    for (int i = 0; i < m; i++) cp[p[i]]++;
    for (int s = 0; s + m <= n; s++) {
        int cw[256] = {0};
        for (int i = 0; i < m; i++) cw[t[s + i]]++;
        if (memcmp(cw, cp, sizeof cp) == 0 && ne < cap) ends[ne++] = (uint32_t)(s + m - 1);
    }
    return ne;
}

static uint32_t e0[TN], e1[TN];

int main(void) {
    /* ---- 연습 1 ---- */
    {
        int bad = 0, cases = 0;
        for (int c = 0; c < 3000; c++) {
            int m = 1 + (int)(xs32() % 8), n = (int)(xs32() % 2000), A = (c & 1) ? 2 : 256;
            uint8_t p[8];
            for (int i = 0; i < m; i++) p[i] = A == 2 ? (uint8_t)(xs32() & 1) : (uint8_t)xs32();   /* 0x00 포함 */
            for (int i = 0; i < n; i++) g_t[i] = A == 2 ? (uint8_t)(xs32() & 1) : (uint8_t)xs32();
            int n0 = naive_find(g_t, n, p, m, e0, TN), n1 = 0;
            sr_t s; sr_init(&s, p, m);
            for (int i = 0; i < n; i++) if (sr_push(&s, g_t[i], true)) e1[n1++] = (uint32_t)i;
            if (!same_ends(e0, n0, e1, n1)) bad++;
            cases++;
        }
        const uint8_t p[3] = {0x00, 0x00, 0x7E}, t[2] = {0x7E, 0x01};
        sr_t s; sr_init(&s, p, 3); bool a = sr_push(&s, t[0], false);   /* 카운터 없이: 시작 직후 가짜 매칭 */
        sr_init(&s, p, 3); bool b = sr_push(&s, t[0], true);
        printf("ex1 shift-register: %d cases vs naive, %d mismatches | 00 00 7E on first byte 7E: without fill=%d with fill=%d\n",
               cases, bad, a, b);
        CHECK(bad == 0); CHECK(a && !b);
    }
    /* ---- 연습 2 ---- */
    {
        int bad = 0, worst_mp = 0, worst_kmp = 0, over_bound = 0, npat = 0;
        uint8_t p[MAXM], fail[MAXM]; int16_t strong[MAXM];
        for (int m = 1; m <= 12; m++)
            for (uint32_t bits = 0; bits < (1u << m); bits++) {
                for (int i = 0; i < m; i++) p[i] = (uint8_t)('a' + ((bits >> i) & 1));
                int a, b; worst_delay(p, m, &a, &b); npat++;
                if (a > worst_mp) worst_mp = a;
                if (b > worst_kmp) worst_kmp = b;
                if (b > 1 + log((double)m) / log((1 + sqrt(5.0)) / 2) + 1e-9) over_bound++;
                if (m <= 8 && (bits & 7) == 0) {                         /* 매칭 결과는 naive 와 같아야 */
                    kmp_build(p, m, fail); strong_build(p, m, fail, strong);
                    int n = 300; for (int i = 0; i < n; i++) g_t[i] = (uint8_t)('a' + (xs32() & 1));
                    int n0 = naive_find(g_t, n, p, m, e0, TN), n1 = run_strong(p, m, fail, strong, g_t, n, e1, TN);
                    if (!same_ends(e0, n0, e1, n1)) bad++;
                }
            }
        printf("ex2 binary patterns len 1..12 (%d): worst comparisons/byte MP=%d KMP(strong)=%d, over 1+log_phi(m): %d, match mismatches %d\n",
               npat, worst_mp, worst_kmp, over_bound, bad);
        CHECK(bad == 0); CHECK(worst_mp == 12); CHECK(over_bound == 0);
        /* 피보나치 단어 f1="b", f2="a", f_{k}=f_{k-1}f_{k-2}: strong 의 최악 사례로 알려진 것 */
        char fa[MAXM * 2] = "a", fb[MAXM * 2] = "b", tmp[MAXM * 2];
        printf("    fibonacci words: ");
        int fib_ok = 1;
        while (strlen(fa) <= MAXM) {
            int m = (int)strlen(fa), a, b;
            worst_delay((const uint8_t *)fa, m, &a, &b);
            printf("m=%d MP=%d KMP=%d (1+log_phi m=%.2f)  ", m, a, b, 1 + log((double)m) / log((1 + sqrt(5.0)) / 2));
            fib_ok &= b <= 1 + log((double)m) / log((1 + sqrt(5.0)) / 2) + 1e-9;
            snprintf(tmp, sizeof tmp, "%s%s", fa, fb); strcpy(fb, fa); strcpy(fa, tmp);
        }
        printf("\n");
        memset(p, 'a', 32); int a, b; worst_delay(p, 32, &a, &b);
        printf("    a^32: MP=%d KMP=%d\n", a, b);
        CHECK(fib_ok); CHECK(a == 32 && b == 1);
    }
    /* ---- 연습 3 ---- */
    {
        const uint8_t s1[] = {0x55, 0x55, 0x7E}, s2[] = {0xAA, 0x7E}, s3[] = {0x7E, 0x7E, 0x7E, 0x55}, s4[] = {0x7E};
        const uint8_t *pats[4] = {s1, s2, s3, s4}; const int lens[4] = {3, 2, 4, 1};
        int bad = 0, total = 0;
        for (int c = 0; c < 400; c++) {
            const uint8_t *P[6]; int L[6], k; uint8_t rp[6][6];
            if (c == 0) { k = 4; for (int i = 0; i < 4; i++) { P[i] = pats[i]; L[i] = lens[i]; } }
            else {
                k = 1 + (int)(xs32() % 6);
                for (int i = 0; i < k; i++) { L[i] = 1 + (int)(xs32() % 6); for (int j = 0; j < L[i]; j++) rp[i][j] = (uint8_t)("UZ~"[xs32() % 3]); P[i] = rp[i]; }
            }
            if (ac_build(&g_ac, P, L, k) != 0) { bad++; continue; }
            int n = 2000;
            for (int i = 0; i < n; i++) { uint32_t r = xs32() % 5; g_t[i] = c == 0 ? (r == 0 ? 0x55 : r == 1 ? 0x7E : r == 2 ? 0xAA : (uint8_t)xs32()) : (uint8_t)("UZ~"[r % 3]); }
            int v = 0;
            uint32_t got[6] = {0}, exp[6] = {0};
            for (int i = 0; i < n; i++) {
                v = g_ac.go[v][g_t[i]];
                for (int z = 0; z < k; z++) {
                    bool hit = (g_ac.out[v] >> z) & 1;
                    bool want = i + 1 >= L[z] && memcmp(g_t + i + 1 - L[z], P[z], (size_t)L[z]) == 0;
                    if (hit != want) bad++;
                    got[z] += hit; exp[z] += want;
                }
            }
            for (int z = 0; z < k; z++) total += (int)got[z];
            if (c == 0) printf("ex3 aho-corasick sync words {55 55 7E, AA 7E, 7E 7E 7E 55, 7E}: states=%d hits=%u/%u/%u/%u\n",
                               g_ac.ns, got[0], got[1], got[2], got[3]);
        }
        printf("    400 pattern sets x 2000 bytes: %d total hits, %d (byte,pattern) disagreements with naive\n", total, bad);
        CHECK(bad == 0 && total > 0);
    }
    /* ---- 연습 4 ---- */
    {
        int bad = 0, npat = 0;
        uint8_t p[12];
        for (int m = 1; m <= 10; m++)
            for (uint32_t bits = 0; bits < (1u << m); bits++) {
                for (int i = 0; i < m; i++) p[i] = (uint8_t)('a' + ((bits >> i) & 1));
                double x = expected_wait_markov(p, m, 2), y = expected_wait_borders(p, m, 2);
                if (fabs(x - y) > 1e-6 * y) bad++;
                npat++;
            }
        const uint8_t hth[] = "aba", hhh[] = "aaa", hht[] = "aab";
        double e_hth = expected_wait_markov(hth, 3, 2), e_hhh = expected_wait_markov(hhh, 3, 2), e_hht = expected_wait_markov(hht, 3, 2);
        const uint8_t pre[] = {0x55, 0x55, 0x7E}, pre3[] = {0x55, 0x55, 0x55};
        double w1 = expected_wait_borders(pre, 3, 256), w2 = expected_wait_borders(pre3, 3, 256);
        printf("ex4 Markov(KMP automaton) vs border sum: %d binary patterns, %d mismatches | HTH=%.1f HHH=%.1f HHT=%.1f\n"
               "    uniform random bytes: 55 55 7E first after %.0f bytes on average (%.0f s at 115200 8N1), 55 55 55 after %.0f\n",
               npat, bad, e_hth, e_hhh, e_hht, w1, w1 / 11520.0, w2);
        CHECK(bad == 0);
        CHECK(fabs(e_hth - 10) < 1e-9 && fabs(e_hhh - 14) < 1e-9 && fabs(e_hht - 8) < 1e-9);
        CHECK(w1 == 16777216.0 && w2 == 16777216.0 + 65536.0 + 256.0);
    }
    /* ---- 연습 5 ---- */
    {
        int bad = 0, hits = 0;
        for (int c = 0; c < 2000; c++) {
            int m = 1 + (int)(xs32() % 6), n = (int)(xs32() % 400), A = 2 + (int)(xs32() % 3);
            uint8_t p[8];
            for (int i = 0; i < m; i++) p[i] = (uint8_t)(xs32() % (uint32_t)A);
            for (int i = 0; i < n; i++) g_t[i] = (uint8_t)(xs32() % (uint32_t)A);
            int n0 = anagram_brute(g_t, n, p, m, e0, TN), n1 = anagram_find(g_t, n, p, m, e1, TN);
            if (!same_ends(e0, n0, e1, n1)) bad++;
            hits += n0;
        }
        printf("ex5 anagram windows: 2000 cases, %d windows, %d mismatches\n", hits, bad);
        CHECK(bad == 0 && hits > 0);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
