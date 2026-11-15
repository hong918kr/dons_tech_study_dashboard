/* ALGO-A07 추가 연습 — 한 번에 검증
 *   연습 1  반사(reflect)로 만드는 n비트 Gray 수열 (LeetCode 89 Gray Code) = b ^ (b>>1)
 *   연습 2  64비트 SWAR 로 버퍼 속 '\n' 개수 세기 — 정확한 마스크 vs haszero 플래그(과다 계산 실측)
 *   연습 3  상위집합 순회 sup = (sup + 1) | mask, 부분집합 합(SOS) O(n·2^n) vs O(3^n)
 *   연습 4  부분집합 DP O(3^n): 플래시 쓰기 작업을 페이지 버퍼 묶음 최소 개수로 — 분할 브루트포스와 대조
 *   연습 5  k-조합의 colex 순위(rank)/역순위(unrank) — Gosper 순서와 같음을 확인
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t rng = 0x9E3779B9u;
static uint32_t xorshift32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static unsigned pc32(uint32_t x) { return (unsigned)__builtin_popcount(x); }
static unsigned pc64(uint64_t x) { return (unsigned)__builtin_popcountll(x); }

static uint32_t next_same_popcount(uint32_t x) {          /* 본문 Part A 와 같음 */
    if (x == 0) return 0;
    uint32_t c = x & (0u - x), r = x + c;
    if (r == 0) return 0;
    return (((r ^ x) >> 2) / c) | r;
}

/* ---------- 연습 1: 반사 Gray ---------- */
static void gray_reflect(int n, uint32_t *out) {
    out[0] = 0;
    for (int i = 0; i < n; i++) {
        uint32_t half = 1u << i;
        for (uint32_t j = 0; j < half; j++)                 /* 앞 절반을 거꾸로 + 새 최상위 비트 */
            out[half + j] = out[half - 1u - j] | half;
    }
}

/* ---------- 연습 2: 64비트 SWAR 바이트 세기 ---------- */
#define ONES64  0x0101010101010101ull
#define HIGHS64 0x8080808080808080ull
#define LOW7_64 0x7F7F7F7F7F7F7F7Full
static uint64_t haszero64(uint64_t v) { return (v - ONES64) & ~v & HIGHS64; }
static uint64_t zero_exact64(uint64_t v) { return ~(((v & LOW7_64) + LOW7_64) | v | LOW7_64); }

/* buf 는 8-정렬, len 은 아무 값. c 와 같은 바이트 수. exact=false 면 haszero 플래그를 그대로 센다(틀림) */
static size_t count_byte(const uint8_t *buf, size_t len, uint8_t c, bool exact) {
    size_t i = 0, n = 0;
    uint64_t rep = ONES64 * c;
    for (; i + 8 <= len; i += 8) {
        uint64_t w;
        memcpy(&w, buf + i, 8);
        w ^= rep;
        n += pc64(exact ? zero_exact64(w) : haszero64(w));
    }
    for (; i < len; i++) n += buf[i] == c;
    return n;
}

/* ---------- 연습 3: 상위집합 · SOS ---------- */
static uint64_t supermask_count(uint32_t mask, int n) {    /* mask ⊆ sup ⊆ [n] 인 sup 의 수 */
    uint64_t cnt = 0;
    uint32_t full = (1u << n) - 1u;
    for (uint32_t sup = mask; ; sup = (sup + 1u) | mask) {
        if (sup > full) break;
        cnt++;
        if (sup == full) break;
    }
    return cnt;
}

#define SOS_N 14
static uint32_t a_arr[1u << SOS_N], f_fast[1u << SOS_N], f_slow[1u << SOS_N];

static void sos_fast(int n) {                               /* 비트마다 한 번씩 "아래 칸을 더한다" */
    memcpy(f_fast, a_arr, sizeof(uint32_t) << n);
    for (int b = 0; b < n; b++)
        for (uint32_t m = 0; m < (1u << n); m++)
            if (m & (1u << b)) f_fast[m] += f_fast[m ^ (1u << b)];
}
static uint64_t sos_slow(int n) {                           /* 부분집합 순회, 반환 = 방문 수 */
    uint64_t visits = 0;
    for (uint32_t m = 0; m < (1u << n); m++) {
        uint32_t s = 0, sub = m;
        for (;;) { s += a_arr[sub]; visits++; if (!sub) break; sub = (sub - 1u) & m; }
        f_slow[m] = s;
    }
    return visits;
}

/* ---------- 연습 4: 최소 묶음 (부분집합 DP) ---------- */
#define MAXJ 12
static uint8_t dp[1u << MAXJ];
static uint32_t wsum[1u << MAXJ];

static int min_batches(const uint32_t *w, int n, uint32_t cap) {
    for (uint32_t m = 1; m < (1u << n); m++) {              /* 부분집합 무게 합 */
        uint32_t lb = m & (0u - m);
        wsum[m] = wsum[m ^ lb] + w[__builtin_ctz(lb)];
    }
    wsum[0] = 0; dp[0] = 0;
    for (uint32_t m = 1; m < (1u << n); m++) {
        uint32_t lb = m & (0u - m), rest = m ^ lb, best = 255;
        for (uint32_t sub = rest; ; sub = (sub - 1u) & rest) {   /* lb 를 반드시 포함 → 중복 없음 */
            uint32_t g = sub | lb;
            if (wsum[g] <= cap && dp[m ^ g] + 1u < best) best = dp[m ^ g] + 1u;
            if (!sub) break;
        }
        dp[m] = (uint8_t)best;
    }
    return dp[(1u << n) - 1u];
}

static int bf_best;
static void bf(const uint32_t *w, int n, uint32_t cap, int i, uint32_t *load, int groups) {
    if (groups >= bf_best) return;                          /* 가지치기 */
    if (i == n) { bf_best = groups; return; }
    for (int g = 0; g < groups; g++)
        if (load[g] + w[i] <= cap) { load[g] += w[i]; bf(w, n, cap, i + 1, load, groups); load[g] -= w[i]; }
    load[groups] = w[i];
    bf(w, n, cap, i + 1, load, groups + 1);
}

/* ---------- 연습 5: colex 순위 ---------- */
static uint64_t C[33][33];
static uint64_t colex_rank(uint32_t x) {                    /* Σ C(p_i, i+1), p_0 < p_1 < ... */
    uint64_t r = 0;
    int i = 0;
    while (x) { int p = __builtin_ctz(x); r += C[p][i + 1]; i++; x &= x - 1u; }
    return r;
}
static uint32_t colex_unrank(uint64_t r, int k) {           /* 위에서부터 가장 큰 p: C(p,k) ≤ r */
    uint32_t x = 0;
    for (int i = k; i >= 1; i--) {
        int p = i - 1;
        while (p + 1 < 32 && C[p + 1][i] <= r) p++;
        x |= 1u << p; r -= C[p][i];
    }
    return x;
}

static _Alignas(8) uint8_t text[4096];

int main(void) {
    /* 연습 1 */
    {
        static uint32_t seq[1u << 16];
        int ok = 1;
        for (int n = 1; n <= 16; n++) {
            gray_reflect(n, seq);
            for (uint32_t b = 0; b < (1u << n); b++) if (seq[b] != (b ^ (b >> 1))) ok = 0;
        }
        CHECK(ok);
        gray_reflect(3, seq);
        CHECK(seq[0] == 0 && seq[1] == 1 && seq[2] == 3 && seq[3] == 2 && seq[4] == 6 && seq[7] == 4);
    }

    /* 연습 2 */
    {
        size_t over = 0, cases = 0, bad = 0;
        for (int round = 0; round < 200; round++) {
            for (size_t i = 0; i < sizeof text; i++) {      /* 로그 텍스트 흉내: 글자 + 가끔 "\n\v" */
                uint32_t r = xorshift32() % 40u;
                text[i] = r == 0 ? '\n' : r == 1 ? 0x0B : (uint8_t)('a' + r % 26u);
            }
            size_t len = (size_t)(xorshift32() % sizeof text), ref = 0;
            for (size_t i = 0; i < len; i++) ref += text[i] == '\n';
            size_t ex = count_byte(text, len, '\n', true), hz = count_byte(text, len, '\n', false);
            cases++;
            if (ex != ref) bad++;
            if (hz != ref) over += hz - ref;
        }
        printf("ex2: %zu buffers, exact count bad=%zu, haszero-flag count overcounted by %zu in total\n",
               cases, bad, over);
        CHECK(bad == 0);
        CHECK(over > 0);
        CHECK(haszero64(0x0B0Aull ^ (ONES64 * '\n')) == 0x8080ull && zero_exact64(0x0B0Aull ^ (ONES64 * '\n')) == 0x80ull);
    }

    /* 연습 3 */
    {
        int ok = 1;
        for (uint32_t m = 0; m < (1u << 10); m++)
            if (supermask_count(m, 10) != (1ull << (10 - pc32(m)))) ok = 0;
        CHECK(ok && supermask_count(0, 0) == 1 && supermask_count(0x3FFu, 10) == 1);
        for (uint32_t m = 0; m < (1u << SOS_N); m++) a_arr[m] = xorshift32() & 0xFFFFu;
        sos_fast(SOS_N);
        uint64_t visits = sos_slow(SOS_N);
        CHECK(memcmp(f_fast, f_slow, sizeof(uint32_t) << SOS_N) == 0);
        printf("ex3: SOS n=%d: fast = n*2^(n-1) = %u adds, slow visits = %llu (3^14 = 4782969)\n",
               SOS_N, SOS_N << (SOS_N - 1), (unsigned long long)visits);
        CHECK(visits == 4782969u);
    }

    /* 연습 4 */
    {
        uint32_t w[MAXJ], load[MAXJ];
        int bad = 0, tried = 0;
        for (int t = 0; t < 400; t++) {
            int n = 1 + (int)(xorshift32() % 9u);
            uint32_t cap = 256u + xorshift32() % 769u;      /* 페이지 버퍼 256..1024 B */
            for (int i = 0; i < n; i++) w[i] = 1u + xorshift32() % cap;
            bf_best = n + 1;
            bf(w, n, cap, 0, load, 0);
            if (min_batches(w, n, cap) != bf_best) bad++;
            tried++;
        }
        CHECK(bad == 0);
        uint32_t w12[12] = {600, 500, 400, 300, 300, 200, 200, 120, 100, 80, 60, 40};
        int b12 = min_batches(w12, 12, 1024);              /* 합 2900 → 하한 ceil(2900/1024) = 3 */
        printf("ex4: %d random instances vs brute bad=%d, 12 jobs (sum 2900 B) into 1024 B pages: %d batches\n",
               tried, bad, b12);
        CHECK(b12 == 3);
    }

    /* 연습 5 */
    {
        for (int n = 0; n <= 32; n++) {
            C[n][0] = 1;
            for (int k = 1; k <= n; k++) C[n][k] = C[n - 1][k - 1] + (k <= n - 1 ? C[n - 1][k] : 0);
        }
        int ok = 1;
        const int n = 20;
        for (int k = 1; k <= n; k++) {
            uint32_t x = (1u << k) - 1u;
            uint64_t r = 0;
            for (;;) {
                if (colex_rank(x) != r || colex_unrank(r, k) != x) { ok = 0; break; }
                uint32_t nx = next_same_popcount(x);
                if (nx == 0 || nx >= (1u << n)) break;
                x = nx; r++;
            }
            if (r != C[n][k] - 1u) ok = 0;
        }
        CHECK(ok);
        CHECK(colex_rank(0xFFFF0000u) == C[32][16] - 1u && colex_unrank(C[32][16] - 1u, 16) == 0xFFFF0000u);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
