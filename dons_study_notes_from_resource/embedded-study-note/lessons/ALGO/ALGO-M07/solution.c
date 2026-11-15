/* ALGO-M07 동적 계획법 1D: 계단·동전 교환·LIS
 *
 * 세 문제 모두 같은 순서로 푼다: 상태 정의 → 점화식 → 하향식 메모(memoization) / 상향식 테이블(tabulation)
 * → 공간 최적화.
 *   Part A  계단: 보폭 집합 steps(1..7), 막힌 칸 blocked. 0 → n 으로 가는 순서 있는 방법 수
 *           stairs_memo / stairs_tab / stairs_roll(링 8칸)
 *   Part B  동전 교환: 최소 개수 coin_min_memo / coin_min_tab / coin_min_ring(링 64칸)
 *           경우의 수: 조합(순서 무관) coin_ways_combo_memo(상태 2개) / coin_ways_combo(1D)
 *                      순열(순서 중요) coin_ways_perm  ← 두 루프의 순서만 다르다 (함정)
 *           경우의 수는 uint64_t 포화 덧셈: 참값이 2^64-1 이상이면 UINT64_MAX
 *   Part C  LIS(최장 증가 부분수열, 순증가): 뒤섞인 시퀀스 번호 로그 → 남길 패킷 = LIS, 버릴 개수 = n - LIS
 *           lis_memo O(n²) 하향식 / lis_n2 O(n²) 상향식 + 복원 / lis_nlogn tails + 복원 / lis_push 스트리밍 O(L)
 *
 * 테스트: 열거 브루트포스(분할 마스크, 다중집합·수열 DFS, 부분집합 2^n)와 대조, xorshift32 무작위,
 *         공개 값(OEIS A000073·A001399, LeetCode 예제, SICP 292, Project Euler 31 = 73682, F(93)).
 *         테스트 쪽 정확값은 unsigned __int128 (clang/gcc 확장). 알고리즘 쪽은 동적 할당 없음.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 포화 덧셈: a, b 가 각각 min(참값, MAX) 이면 결과도 min(참합, MAX). 음이 아닌 수의 합에서만 성립 */
static uint64_t sat_add_u64(uint64_t a, uint64_t b) { return (a > UINT64_MAX - b) ? UINT64_MAX : a + b; }

/* ======================= Part A: 계단 ======================= */
#define STAIRS_MAX_N    256
#define STAIRS_MAX_STEP 7
#define STAIRS_RING     8          /* MAX_STEP 보다 큰 2의 거듭제곱 → 인덱스는 & (RING - 1), 나눗셈 없음 */

/* steps: 서로 다른 1..7 (k 개). blocked: 길이 n+1 배열 또는 NULL(막힌 칸 없음) */
static bool stairs_args_ok(uint32_t n, const uint8_t *steps, size_t k) {
    if (n > STAIRS_MAX_N || k == 0 || k > STAIRS_MAX_STEP) return false;
    for (size_t j = 0; j < k; j++) {
        if (steps[j] < 1 || steps[j] > STAIRS_MAX_STEP) return false;
        for (size_t t = 0; t < j; t++) if (steps[t] == steps[j]) return false;
    }
    return true;
}
static bool is_blocked(const uint8_t *blocked, uint32_t i) { return blocked != NULL && blocked[i] != 0; }

/* 상태 f(i) = 0 → i 로 가는 방법 수.  f(i) = 0 (i 가 막힘),  f(0) = 1,  f(i) = Σ_{s∈steps, s≤i} f(i-s) */
typedef struct { const uint8_t *steps; size_t k; const uint8_t *blocked; } stairs_ctx_t;
static uint64_t s_memo[STAIRS_MAX_N + 1];
static bool     s_have[STAIRS_MAX_N + 1];

static uint64_t stairs_f(const stairs_ctx_t *c, uint32_t i) {
    if (is_blocked(c->blocked, i)) return 0;
    if (i == 0) return 1;
    if (s_have[i]) return s_memo[i];
    uint64_t w = 0;
    for (size_t j = 0; j < c->k; j++)
        if (c->steps[j] <= i) w = sat_add_u64(w, stairs_f(c, i - c->steps[j]));
    s_have[i] = true;
    s_memo[i] = w;
    return w;
}

/* 하향식 (재귀 깊이 최대 n+1) */
uint64_t stairs_memo(uint32_t n, const uint8_t *steps, size_t k, const uint8_t *blocked) {
    if (!stairs_args_ok(n, steps, k)) return 0;
    memset(s_have, 0, sizeof s_have);
    stairs_ctx_t c = { steps, k, blocked };
    return stairs_f(&c, n);
}

/* 상향식: dp[0..n] (8 B × (n+1)) */
static uint64_t s_dp[STAIRS_MAX_N + 1];
uint64_t stairs_tab(uint32_t n, const uint8_t *steps, size_t k, const uint8_t *blocked) {
    if (!stairs_args_ok(n, steps, k)) return 0;
    s_dp[0] = is_blocked(blocked, 0) ? 0 : 1;
    for (uint32_t i = 1; i <= n; i++) {
        uint64_t w = 0;
        if (!is_blocked(blocked, i))
            for (size_t j = 0; j < k; j++)
                if (steps[j] <= i) w = sat_add_u64(w, s_dp[i - steps[j]]);
        s_dp[i] = w;
    }
    return s_dp[n];
}

/* 공간 최적화: 최근 8칸만 (64 B, n 과 무관) */
uint64_t stairs_roll(uint32_t n, const uint8_t *steps, size_t k, const uint8_t *blocked) {
    if (!stairs_args_ok(n, steps, k)) return 0;
    uint64_t r[STAIRS_RING] = { 0 };
    r[0] = is_blocked(blocked, 0) ? 0 : 1;
    for (uint32_t i = 1; i <= n; i++) {
        uint64_t w = 0;
        if (!is_blocked(blocked, i))
            for (size_t j = 0; j < k; j++)
                if (steps[j] <= i) w = sat_add_u64(w, r[(i - steps[j]) & (STAIRS_RING - 1)]);
        r[i & (STAIRS_RING - 1)] = w;   /* i-8 칸을 덮어쓴다: 아무도 다시 읽지 않는다 (s ≤ 7) */
    }
    return r[n & (STAIRS_RING - 1)];
}

/* ======================= Part B: 동전 교환 ======================= */
#define COIN_MAX_AMT  10000
#define COIN_MAX_K    16
#define COIN_MEMO_AMT 2000         /* 하향식은 재귀 깊이 ≈ amount → 2000 까지만 받는다 */
#define COIN_RING     64           /* coin_min_ring: 모든 동전 < 64 */
#define COIN_INF      UINT16_MAX   /* 만들 수 없음 (개수 ≤ 10000 < 65535) */

/* 동전은 서로 다른 양수, k ≤ 16, amount ≤ max_amt. k == 0 허용 (amount 0 만 가능) */
static bool coins_ok(const uint32_t *coins, size_t k, uint32_t amount, uint32_t max_amt) {
    if (amount > max_amt || k > COIN_MAX_K) return false;
    for (size_t j = 0; j < k; j++) {
        if (coins[j] == 0) return false;
        for (size_t t = 0; t < j; t++) if (coins[t] == coins[j]) return false;
    }
    return true;
}
typedef struct { const uint32_t *coins; size_t k; } coin_ctx_t;

/* g(a) = a 를 만드는 최소 동전 수 (불가능 -1).  g(0) = 0,  g(a) = 1 + min_{c≤a, g(a-c)≥0} g(a-c) */
static int32_t m_memo[COIN_MEMO_AMT + 1];   /* -2 = 아직 모름 */
static int32_t coin_min_f(const coin_ctx_t *c, uint32_t a) {
    if (a == 0) return 0;
    if (m_memo[a] != -2) return m_memo[a];
    int32_t best = -1;
    for (size_t j = 0; j < c->k; j++) {
        if (c->coins[j] > a) continue;
        int32_t sub = coin_min_f(c, a - c->coins[j]);
        if (sub >= 0 && (best < 0 || sub + 1 < best)) best = sub + 1;
    }
    m_memo[a] = best;
    return best;
}

/* 하향식. 입력이 규칙 밖이어도 -1 */
int32_t coin_min_memo(const uint32_t *coins, size_t k, uint32_t amount) {
    if (!coins_ok(coins, k, amount, COIN_MEMO_AMT)) return -1;
    for (uint32_t a = 0; a <= amount; a++) m_memo[a] = -2;
    coin_ctx_t c = { coins, k };
    return coin_min_f(&c, amount);
}

/* 상향식: dp[a] 는 uint16_t (2 B × (amount+1)) */
static uint16_t m_dp[COIN_MAX_AMT + 1];
int32_t coin_min_tab(const uint32_t *coins, size_t k, uint32_t amount) {
    if (!coins_ok(coins, k, amount, COIN_MAX_AMT)) return -1;
    m_dp[0] = 0;
    for (uint32_t a = 1; a <= amount; a++) {
        uint16_t best = COIN_INF;
        for (size_t j = 0; j < k; j++) {
            uint32_t c = coins[j];
            if (c <= a && m_dp[a - c] != COIN_INF && m_dp[a - c] + 1 < best) best = (uint16_t)(m_dp[a - c] + 1);
        }
        m_dp[a] = best;
    }
    return m_dp[amount] == COIN_INF ? -1 : (int32_t)m_dp[amount];
}

/* 공간 최적화: dp[a] 는 dp[a-c] (c < 64) 만 본다 → 링 64칸 (256 B). amount 는 UINT32_MAX 미만 아무 값 */
int32_t coin_min_ring(const uint32_t *coins, size_t k, uint32_t amount) {
    if (!coins_ok(coins, k, 0, 0) || amount == UINT32_MAX) return -1;
    for (size_t j = 0; j < k; j++) if (coins[j] >= COIN_RING) return -1;
    uint32_t r[COIN_RING];
    r[0] = 0;
    for (uint32_t a = 1; a <= amount; a++) {
        uint32_t best = UINT32_MAX;                      /* UINT32_MAX = 불가능 */
        for (size_t j = 0; j < k; j++) {
            uint32_t c = coins[j];
            if (c <= a) {
                uint32_t sub = r[(a - c) & (COIN_RING - 1)];
                if (sub != UINT32_MAX && sub + 1 < best) best = sub + 1;
            }
        }
        r[a & (COIN_RING - 1)] = best;
    }
    uint32_t v = r[amount & (COIN_RING - 1)];
    return v == UINT32_MAX ? -1 : (int32_t)v;
}

/* h(i, a) = coins[i..k) 만으로 a 를 만드는 다중집합 수 (순서 무관).
 * h(i, 0) = 1,  h(k, a>0) = 0,  h(i, a) = h(i+1, a) + [c_i ≤ a] h(i, a - c_i)   ← "동전 i 를 그만 쓴다 / 하나 더 쓴다" */
static uint64_t w_memo[COIN_MAX_K][COIN_MEMO_AMT + 1];
static uint8_t  w_have[COIN_MAX_K][COIN_MEMO_AMT + 1];
static uint64_t combo_f(const coin_ctx_t *c, size_t i, uint32_t a) {
    if (a == 0) return 1;
    if (i == c->k) return 0;
    if (w_have[i][a]) return w_memo[i][a];
    uint64_t w = combo_f(c, i + 1, a);
    if (c->coins[i] <= a) w = sat_add_u64(w, combo_f(c, i, a - c->coins[i]));
    w_have[i][a] = 1;
    w_memo[i][a] = w;
    return w;
}

/* 하향식 조합 수: 상태가 (i, a) 두 개라 메모가 2D (k × (amount+1)) */
uint64_t coin_ways_combo_memo(const uint32_t *coins, size_t k, uint32_t amount) {
    if (!coins_ok(coins, k, amount, COIN_MEMO_AMT)) return 0;
    for (size_t i = 0; i < k; i++) memset(w_have[i], 0, amount + 1);
    coin_ctx_t c = { coins, k };
    return combo_f(&c, 0, amount);
}

static uint64_t w_dp[COIN_MAX_AMT + 1];
/* 상향식 조합 수 (순서 무관): 바깥 = 동전, 안 = 금액 오름차순. 2D 표의 행을 한 줄에 덮어쓴 것 */
uint64_t coin_ways_combo(const uint32_t *coins, size_t k, uint32_t amount) {
    if (!coins_ok(coins, k, amount, COIN_MAX_AMT)) return 0;
    w_dp[0] = 1;
    for (uint32_t a = 1; a <= amount; a++) w_dp[a] = 0;
    for (size_t j = 0; j < k; j++)
        for (uint32_t a = coins[j]; a <= amount; a++)
            w_dp[a] = sat_add_u64(w_dp[a], w_dp[a - coins[j]]);
    return w_dp[amount];
}

/* 상향식 순열 수 (순서 중요, = 보폭이 동전인 계단): 바깥 = 금액, 안 = 동전 */
uint64_t coin_ways_perm(const uint32_t *coins, size_t k, uint32_t amount) {
    if (!coins_ok(coins, k, amount, COIN_MAX_AMT)) return 0;
    w_dp[0] = 1;
    for (uint32_t a = 1; a <= amount; a++) {
        uint64_t w = 0;
        for (size_t j = 0; j < k; j++)
            if (coins[j] <= a) w = sat_add_u64(w, w_dp[a - coins[j]]);
        w_dp[a] = w;
    }
    return w_dp[amount];
}

/* ======================= Part C: LIS (순증가) ======================= */
#define LIS_MAX_N 4096
#define LIS_NONE  SIZE_MAX

/* L(i) = a[i] 로 끝나는 LIS 길이.  L(i) = 1 + max_{j<i, a[j]<a[i]} L(j)  (없으면 1).  답 = max_i L(i) */
static size_t l_memo[LIS_MAX_N];    /* 0 = 아직 모름 (참값은 ≥ 1) */
static size_t lis_end_f(const int32_t *a, size_t i) {
    if (l_memo[i]) return l_memo[i];
    size_t best = 1;
    for (size_t j = 0; j < i; j++)
        if (a[j] < a[i]) {
            size_t t = lis_end_f(a, j) + 1;
            if (t > best) best = t;
        }
    l_memo[i] = best;
    return best;
}

/* 하향식 O(n²), 길이만 */
size_t lis_memo(const int32_t *a, size_t n) {
    if (n > LIS_MAX_N) return 0;
    memset(l_memo, 0, n * sizeof l_memo[0]);
    size_t best = 0;
    for (size_t i = n; i-- > 0;) {                     /* 위에서부터 묻는다 (진짜 하향식) */
        size_t t = lis_end_f(a, i);
        if (t > best) best = t;
    }
    return best;
}

static size_t l_len[LIS_MAX_N], l_prev[LIS_MAX_N], l_tail[LIS_MAX_N];

/* 상향식 O(n²) + 복원: out_idx (NULL 가능) 에 LIS 인덱스를 오름차순으로 */
size_t lis_n2(const int32_t *a, size_t n, size_t *out_idx) {
    if (n > LIS_MAX_N) return 0;
    size_t best = 0, end = 0;
    for (size_t i = 0; i < n; i++) {
        l_len[i] = 1;
        l_prev[i] = LIS_NONE;
        for (size_t j = 0; j < i; j++)
            if (a[j] < a[i] && l_len[j] + 1 > l_len[i]) { l_len[i] = l_len[j] + 1; l_prev[i] = j; }
        if (l_len[i] > best) { best = l_len[i]; end = i; }
    }
    if (out_idx)
        for (size_t p = best, i = end; p > 0; p--, i = l_prev[i]) out_idx[p - 1] = i;
    return best;
}

/* O(n log n) patience: tail[m] = 길이 m+1 증가 부분수열들 중 끝값이 가장 작은 것의 끝 인덱스.
 * 불변식: a[tail[0]] < a[tail[1]] < ... < a[tail[len-1]].  복원은 prev[] 체인 */
size_t lis_nlogn(const int32_t *a, size_t n, size_t *out_idx) {
    if (n > LIS_MAX_N) return 0;
    size_t len = 0;
    for (size_t i = 0; i < n; i++) {
        size_t lo = 0, hi = len;                     /* 첫 m 으로 a[tail[m]] >= a[i]  (lower_bound, ALGO-B02) */
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (a[l_tail[mid]] < a[i]) lo = mid + 1;
            else                       hi = mid;
        }
        l_prev[i] = lo ? l_tail[lo - 1] : LIS_NONE;
        l_tail[lo] = i;
        if (lo == len) len++;
    }
    if (out_idx && len)
        for (size_t p = len, i = l_tail[len - 1]; p > 0; p--, i = l_prev[i]) out_idx[p - 1] = i;
    return len;
}

/* 스트리밍 길이 전용: tails 값만 (caller 버퍼 cap 칸, 4 B × L). 넘치면 false (길이는 그대로) */
typedef struct { int32_t *tails; size_t cap; size_t len; } lis_stream_t;
bool lis_push(lis_stream_t *s, int32_t x) {
    size_t lo = 0, hi = s->len;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (s->tails[mid] < x) lo = mid + 1;
        else                   hi = mid;
    }
    if (lo == s->len) {
        if (s->len == s->cap) return false;
        s->len++;
    }
    s->tails[lo] = x;
    return true;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x2545F491u;
static uint32_t xorshift32(void) {
    uint32_t x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g_rng = x;
}

typedef unsigned __int128 u128;
static const u128 TWO64 = (u128)1 << 64;
static uint64_t sat64(u128 v) { return v > UINT64_MAX ? UINT64_MAX : (uint64_t)v; }

/* --- 브루트포스 1: 계단 = n 의 합성(composition). 자를 위치 n-1 곳의 부분집합 2^(n-1) 개를 전부 본다 --- */
static uint64_t brute_stairs(uint32_t n, const uint8_t *steps, size_t k, const uint8_t *blocked) {
    if (blocked && (blocked[0] || blocked[n])) return 0;
    if (n == 0) return 1;
    uint64_t cnt = 0;
    for (uint32_t mask = 0; mask < (1u << (n - 1)); mask++) {
        uint32_t last = 0;
        bool ok = true;
        for (uint32_t p = 1; p <= n && ok; p++) {
            if (p < n && !((mask >> (p - 1)) & 1u)) continue;   /* p 에서 자르지 않음 */
            uint32_t part = p - last;
            bool in = false;
            for (size_t j = 0; j < k; j++) in |= (steps[j] == part);
            if (!in || (blocked && blocked[p])) ok = false;
            last = p;
        }
        cnt += ok;
    }
    return cnt;
}

/* 정확값 참조 (128비트, 2^64 에서 잘라 둠): 포화와 같은 의미 */
static uint64_t ref_stairs_exact(uint32_t n, const uint8_t *steps, size_t k, const uint8_t *blocked) {
    static u128 d[STAIRS_MAX_N + 1];
    for (uint32_t i = 0; i <= n; i++) {
        u128 w = (i == 0) ? 1 : 0;
        for (size_t j = 0; j < k && i; j++) if (steps[j] <= i) w += d[i - steps[j]];
        if (blocked && blocked[i]) w = 0;
        d[i] = w > TWO64 ? TWO64 : w;
    }
    return sat64(d[n]);
}

/* --- 브루트포스 2: 동전 다중집합 열거 (인덱스 비감소) → 최소 개수와 조합 수 --- */
static uint64_t bc_ways;
static int32_t  bc_min;
static void brute_multiset(const uint32_t *coins, size_t k, size_t i, uint32_t left, int32_t used) {
    if (left == 0) { bc_ways++; if (bc_min < 0 || used < bc_min) bc_min = used; return; }
    for (size_t j = i; j < k; j++)
        if (coins[j] <= left) brute_multiset(coins, k, j, left - coins[j], used + 1);
}
/* --- 브루트포스 3: 순서 있는 수열 열거 --- */
static uint64_t brute_perm(const uint32_t *coins, size_t k, uint32_t left) {
    if (left == 0) return 1;
    uint64_t s = 0;
    for (size_t j = 0; j < k; j++) if (coins[j] <= left) s += brute_perm(coins, k, left - coins[j]);
    return s;
}
static int32_t greedy_coins(const uint32_t *desc, size_t k, uint32_t amount) {   /* 내림차순 동전 */
    int32_t cnt = 0;
    for (size_t j = 0; j < k; j++) { cnt += (int32_t)(amount / desc[j]); amount %= desc[j]; }
    return amount ? -1 : cnt;
}

/* --- 브루트포스 4: LIS = 모든 부분집합 중 순증가인 것의 최대 크기 --- */
static size_t brute_lis(const int32_t *a, size_t n) {
    size_t best = 0;
    for (uint32_t mask = 0; mask < (1u << n); mask++) {
        size_t cnt = 0;
        bool ok = true, have = false;
        int32_t last = 0;
        for (size_t i = 0; i < n && ok; i++) {
            if (!((mask >> i) & 1u)) continue;
            if (have && a[i] <= last) ok = false;
            last = a[i]; have = true; cnt++;
        }
        if (ok && cnt > best) best = cnt;
    }
    return best;
}
/* 복원 결과 검증: 인덱스 순증가 + 값 순증가 + 길이 */
static bool lis_valid(const int32_t *a, size_t n, const size_t *idx, size_t len) {
    for (size_t p = 0; p < len; p++) {
        if (idx[p] >= n) return false;
        if (p && (idx[p] <= idx[p - 1] || a[idx[p]] <= a[idx[p - 1]])) return false;
    }
    return true;
}
static size_t stream_len(const int32_t *a, size_t n) {
    static int32_t buf[LIS_MAX_N];
    lis_stream_t s = { buf, LIS_MAX_N, 0 };
    for (size_t i = 0; i < n; i++) (void)lis_push(&s, a[i]);
    return s.len;
}

int main(void) {
    /* ======== Part A: 계단 ======== */
    const uint8_t st12[] = { 1, 2 }, st123[] = { 1, 2, 3 };
    {
        CHECK(stairs_memo(0, st12, 2, NULL) == 1 && stairs_tab(0, st12, 2, NULL) == 1 && stairs_roll(0, st12, 2, NULL) == 1);
        CHECK(stairs_memo(10, st12, 2, NULL) == 89 && stairs_tab(10, st12, 2, NULL) == 89 && stairs_roll(10, st12, 2, NULL) == 89);
        /* {1,2}: ways(n) = F(n+1).  F(93) = 12200160415121876738 은 uint64 에 들어가는 마지막 피보나치 수 */
        CHECK(stairs_tab(92, st12, 2, NULL) == 12200160415121876738ull && stairs_roll(92, st12, 2, NULL) == 12200160415121876738ull);
        CHECK(stairs_memo(93, st12, 2, NULL) == UINT64_MAX && stairs_tab(93, st12, 2, NULL) == UINT64_MAX);
        /* {1,2,3}: ways(n) = A000073(n+2) (OEIS, tribonacci 0,0,1,1,2,4,7,...) */
        static const uint64_t trib[] = { 0, 0, 1, 1, 2, 4, 7, 13, 24, 44, 81, 149, 274, 504, 927, 1705, 3136, 5768, 10609, 19513, 35890 };
        int bad = 0;
        for (uint32_t n = 0; n + 2 < sizeof trib / sizeof trib[0]; n++)
            bad += stairs_memo(n, st123, 3, NULL) != trib[n + 2] || stairs_tab(n, st123, 3, NULL) != trib[n + 2]
                || stairs_roll(n, st123, 3, NULL) != trib[n + 2];
        CHECK(bad == 0);
        /* 손 계산 예: n = 7, {1,2,3}, 칸 2·5 막힘 → 8 (막힘 없으면 44) */
        const uint8_t blk7[8] = { 0, 0, 1, 0, 0, 1, 0, 0 };
        CHECK(stairs_memo(7, st123, 3, blk7) == 8 && stairs_tab(7, st123, 3, blk7) == 8 && stairs_roll(7, st123, 3, blk7) == 8);
        /* 경계: 출발/도착이 막힘, 짝수 보폭만, 규칙 밖 입력 */
        const uint8_t blk_start[4] = { 1, 0, 0, 0 }, blk_end[4] = { 0, 0, 0, 1 };
        const uint8_t st2[] = { 2 }, bad0[] = { 0, 1 }, bad8[] = { 8 }, dup[] = { 1, 1 };
        CHECK(stairs_tab(3, st123, 3, blk_start) == 0 && stairs_roll(3, st123, 3, blk_end) == 0 && stairs_memo(3, st123, 3, blk_end) == 0);
        CHECK(stairs_tab(7, st2, 1, NULL) == 0 && stairs_memo(8, st2, 1, NULL) == 1 && stairs_roll(8, st2, 1, NULL) == 1);
        CHECK(stairs_memo(5, bad0, 2, NULL) == 0 && stairs_tab(5, bad8, 1, NULL) == 0 && stairs_roll(5, dup, 2, NULL) == 0
              && stairs_tab(STAIRS_MAX_N + 1, st12, 2, NULL) == 0);
    }
    {   /* 전수: n = 0..10, 칸 0..n 의 모든 막힘 패턴, 보폭 집합 5개 → 분할 마스크 브루트포스와 대조 */
        static const uint8_t sets[][3] = { { 1, 2, 0 }, { 1, 2, 3 }, { 2, 3, 0 }, { 1, 3, 5 }, { 7, 0, 0 } };
        static const size_t ks[] = { 2, 3, 2, 3, 1 };
        unsigned long cases = 0, bad = 0;
        uint8_t blk[11];
        for (size_t s = 0; s < 5; s++)
            for (uint32_t n = 0; n <= 10; n++)
                for (uint32_t m = 0; m < (1u << (n + 1)); m++) {
                    for (uint32_t i = 0; i <= n; i++) blk[i] = (uint8_t)((m >> i) & 1u);
                    uint64_t want = brute_stairs(n, sets[s], ks[s], blk);
                    cases++;
                    bad += stairs_memo(n, sets[s], ks[s], blk) != want || stairs_tab(n, sets[s], ks[s], blk) != want
                        || stairs_roll(n, sets[s], ks[s], blk) != want;
                }
        printf("Part A exhaustive: %lu (steps, n, blocked) cases vs composition brute force, mismatches=%lu\n", cases, bad);
        CHECK(bad == 0);
    }
    {   /* 무작위: n ≤ 256, 보폭 부분집합, 막힘 확률 0~30% → 세 구현 == 128비트 참조 (포화 포함) */
        unsigned long bad = 0, sat = 0;
        uint8_t blk[STAIRS_MAX_N + 1], steps[STAIRS_MAX_STEP];
        for (int t = 0; t < 3000; t++) {
            uint32_t n = xorshift32() % (STAIRS_MAX_N + 1);
            uint32_t sm = 1 + xorshift32() % 127;           /* 1..7 의 비지 않은 부분집합 */
            size_t k = 0;
            for (uint8_t v = 1; v <= 7; v++) if ((sm >> (v - 1)) & 1u) steps[k++] = v;
            uint32_t pct = xorshift32() % 31;
            for (uint32_t i = 0; i <= n; i++) blk[i] = (uint8_t)(xorshift32() % 100 < pct);
            blk[0] = blk[n] = 0;
            uint64_t want = ref_stairs_exact(n, steps, k, blk);
            sat += (want == UINT64_MAX);
            bad += stairs_memo(n, steps, k, blk) != want || stairs_tab(n, steps, k, blk) != want
                || stairs_roll(n, steps, k, blk) != want;
        }
        printf("Part A random: 3000 cases (n<=256), saturated=%lu, mismatches=%lu\n", sat, bad);
        CHECK(bad == 0);
        CHECK(sat > 0);
    }
    {   /* uint64 한계: 처음으로 넘치는 n */
        uint32_t n12 = 0, n123 = 0;
        while (ref_stairs_exact(n12 + 1, st12, 2, NULL) != UINT64_MAX) n12++;
        while (ref_stairs_exact(n123 + 1, st123, 3, NULL) != UINT64_MAX) n123++;
        printf("Part A uint64 limit: steps{1,2} last exact n=%u, steps{1,2,3} last exact n=%u\n", n12, n123);
        CHECK(n12 == 92);
        CHECK(stairs_tab(n123, st123, 3, NULL) == ref_stairs_exact(n123, st123, 3, NULL)
              && stairs_tab(n123, st123, 3, NULL) != UINT64_MAX && stairs_roll(n123 + 1, st123, 3, NULL) == UINT64_MAX);
    }

    /* ======== Part B: 동전 교환 ======== */
    {
        const uint32_t c125[] = { 1, 2, 5 }, c2[] = { 2 }, c1[] = { 1 }, c10[] = { 10 }, c9[] = { 9 }, c123[] = { 1, 2, 3 };
        const uint32_t c134[] = { 1, 3, 4 }, c431[] = { 4, 3, 1 };
        /* LeetCode 322 예제 */
        CHECK(coin_min_memo(c125, 3, 11) == 3 && coin_min_tab(c125, 3, 11) == 3 && coin_min_ring(c125, 3, 11) == 3);
        CHECK(coin_min_memo(c2, 1, 3) == -1 && coin_min_tab(c2, 1, 3) == -1 && coin_min_ring(c2, 1, 3) == -1);
        CHECK(coin_min_memo(c1, 1, 0) == 0 && coin_min_tab(c1, 1, 0) == 0 && coin_min_ring(c1, 1, 0) == 0);
        /* 탐욕이 틀리는 집합: {1,3,4}, 6 → 3+3 = 2개 (탐욕 4+1+1 = 3개) */
        CHECK(coin_min_tab(c134, 3, 6) == 2 && coin_min_memo(c134, 3, 6) == 2 && greedy_coins(c431, 3, 6) == 3);
        /* LeetCode 518 예제 (조합) / LeetCode 377 예제 (순열) */
        CHECK(coin_ways_combo(c125, 3, 5) == 4 && coin_ways_combo_memo(c125, 3, 5) == 4);
        CHECK(coin_ways_combo(c2, 1, 3) == 0 && coin_ways_combo(c10, 1, 10) == 1 && coin_ways_combo_memo(c10, 1, 10) == 1);
        CHECK(coin_ways_perm(c123, 3, 4) == 7 && coin_ways_perm(c9, 1, 3) == 0);
        CHECK(coin_ways_combo(c123, 3, 4) == 4);                 /* 같은 입력, 루프 순서만 다르면 4 vs 7 */
        /* SICP 1.2.2: 100센트를 {50,25,10,5,1} 로 → 292,  Project Euler 31: 200p 를 영국 동전 8종으로 → 73682 */
        const uint32_t us5[] = { 1, 5, 10, 25, 50 }, uk8[] = { 1, 2, 5, 10, 20, 50, 100, 200 };
        CHECK(coin_ways_combo(us5, 5, 100) == 292 && coin_ways_combo_memo(us5, 5, 100) == 292);
        CHECK(coin_ways_combo(uk8, 8, 200) == 73682 && coin_ways_combo_memo(uk8, 8, 200) == 73682);
        /* OEIS A001399: {1,2,3} 조합 수 = round((n+3)²/12) = floor(((n+3)² + 6) / 12) */
        int bad = 0;
        for (uint32_t n = 0; n <= COIN_MAX_AMT; n += (n < 2000 ? 1 : 97)) {
            uint64_t want = ((uint64_t)(n + 3) * (n + 3) + 6) / 12;
            bad += coin_ways_combo(c123, 3, n) != want;
            if (n <= COIN_MEMO_AMT) bad += coin_ways_combo_memo(c123, 3, n) != want;
        }
        CHECK(bad == 0);
        /* 순열 수 = 보폭이 동전인 계단 */
        bad = 0;
        for (uint32_t n = 0; n <= STAIRS_MAX_N; n++) bad += coin_ways_perm(c123, 3, n) != stairs_tab(n, st123, 3, NULL);
        CHECK(bad == 0);
        const uint32_t c12[] = { 1, 2 };
        CHECK(coin_ways_perm(c1, 1, 10000) == 1 && coin_ways_perm(c12, 2, 92) == 12200160415121876738ull
              && coin_ways_perm(c12, 2, 93) == UINT64_MAX);
        /* 큰 금액: 미국 동전(정준 체계 → 탐욕이 최적)과 대조. 링 버전은 금액 크기와 무관한 256 B */
        const uint32_t us5d[] = { 50, 25, 10, 5, 1 };
        CHECK(coin_min_tab(us5, 5, COIN_MAX_AMT) == greedy_coins(us5d, 5, COIN_MAX_AMT));
        CHECK(coin_min_ring(us5, 5, 999999) == greedy_coins(us5d, 5, 999999));
        /* 규칙 밖 입력: 0 동전, 중복 동전, 금액 초과, 링 범위 밖 동전 */
        const uint32_t z[] = { 0, 1 }, d[] = { 3, 3 }, big[] = { 1, 64 };
        CHECK(coin_min_tab(z, 2, 5) == -1 && coin_ways_combo(d, 2, 6) == 0 && coin_ways_perm(z, 2, 5) == 0
              && coin_min_memo(c1, 1, COIN_MEMO_AMT + 1) == -1 && coin_min_ring(big, 2, 100) == -1);
        CHECK(coin_min_tab(NULL, 0, 0) == 0 && coin_min_tab(NULL, 0, 7) == -1 && coin_ways_combo(NULL, 0, 0) == 1
              && coin_ways_perm(NULL, 0, 7) == 0);
    }
    {   /* 전수: {1..8} 의 모든 비지 않은 부분집합 × 금액 0..24 (순열은 0..16) → 열거 브루트포스와 대조 */
        unsigned long cases = 0, bad_min = 0, bad_combo = 0, bad_perm = 0;
        uint32_t coins[8];
        for (uint32_t m = 1; m < 256; m++) {
            size_t k = 0;
            for (uint32_t v = 1; v <= 8; v++) if ((m >> (v - 1)) & 1u) coins[k++] = v;
            for (uint32_t amt = 0; amt <= 24; amt++) {
                bc_ways = 0; bc_min = -1;
                brute_multiset(coins, k, 0, amt, 0);
                cases++;
                bad_min += coin_min_memo(coins, k, amt) != bc_min || coin_min_tab(coins, k, amt) != bc_min
                        || coin_min_ring(coins, k, amt) != bc_min;
                bad_combo += coin_ways_combo(coins, k, amt) != bc_ways || coin_ways_combo_memo(coins, k, amt) != bc_ways;
                if (amt <= 16) bad_perm += coin_ways_perm(coins, k, amt) != brute_perm(coins, k, amt);
            }
        }
        printf("Part B exhaustive: %lu (coin set, amount) cases, mismatches min=%lu combo=%lu perm=%lu\n",
               cases, bad_min, bad_combo, bad_perm);
        CHECK(bad_min == 0);
        CHECK(bad_combo == 0);
        CHECK(bad_perm == 0);
    }
    {   /* 무작위: 동전 1..63 서로 다른 k = 1..6 개, 금액 ≤ 2000 → memo == tab == ring, combo memo == combo 1D */
        unsigned long bad = 0, impossible = 0;
        uint32_t coins[6];
        for (int t = 0; t < 400; t++) {
            size_t k = 1 + xorshift32() % 6;
            for (size_t j = 0; j < k; j++) {
                uint32_t v;
                bool again;
                do { v = 2 + xorshift32() % 62; again = false; for (size_t q = 0; q < j; q++) again |= coins[q] == v; } while (again);
                coins[j] = v;                                       /* 1 은 빼서 불가능한 금액도 나오게 */
            }
            uint32_t amt = xorshift32() % (COIN_MEMO_AMT + 1);
            int32_t want = coin_min_tab(coins, k, amt);
            impossible += want < 0;
            bad += coin_min_memo(coins, k, amt) != want || coin_min_ring(coins, k, amt) != want
                || coin_ways_combo_memo(coins, k, amt) != coin_ways_combo(coins, k, amt);
        }
        printf("Part B random: 400 cases (amount<=2000), impossible=%lu, mismatches=%lu\n", impossible, bad);
        CHECK(bad == 0);
    }

    /* ======== Part C: LIS ======== */
    {
        size_t idx[16];
        const int32_t e1[] = { 10, 9, 2, 5, 3, 7, 101, 18 }, e2[] = { 0, 1, 0, 3, 2, 3 }, e3[] = { 7, 7, 7, 7, 7, 7, 7 };
        /* LeetCode 300 예제 */
        CHECK(lis_memo(e1, 8) == 4 && lis_n2(e1, 8, idx) == 4 && lis_valid(e1, 8, idx, 4) && lis_nlogn(e1, 8, idx) == 4
              && lis_valid(e1, 8, idx, 4) && stream_len(e1, 8) == 4);
        CHECK(lis_memo(e2, 6) == 4 && lis_n2(e2, 6, NULL) == 4 && lis_nlogn(e2, 6, NULL) == 4 && stream_len(e2, 6) == 4);
        CHECK(lis_memo(e3, 7) == 1 && lis_n2(e3, 7, NULL) == 1 && lis_nlogn(e3, 7, NULL) == 1 && stream_len(e3, 7) == 1);
        CHECK(lis_memo(NULL, 0) == 0 && lis_n2(NULL, 0, idx) == 0 && lis_nlogn(NULL, 0, idx) == 0 && stream_len(NULL, 0) == 0);
        const int32_t ext[] = { INT32_MAX, INT32_MIN, 0, INT32_MAX, INT32_MIN };
        CHECK(lis_memo(ext, 5) == 3 && lis_nlogn(ext, 5, idx) == 3 && lis_valid(ext, 5, idx, 3) && idx[0] == 1 && idx[2] == 3);
        /* 시퀀스 번호 로그: 105 가 일찍 도착, 90 은 다른 세션의 잔재 → 남길 것 8개, 버릴 것 2개 */
        const int32_t seq[] = { 100, 101, 102, 105, 103, 104, 106, 90, 107, 108 };
        size_t kept = lis_nlogn(seq, 10, idx);
        CHECK(kept == 8 && lis_valid(seq, 10, idx, kept) && 10 - kept == 2);
        /* 스트리밍 버퍼가 작으면 넘침을 알린다 */
        int32_t small[3];
        lis_stream_t s = { small, 3, 0 };
        bool ok = true;
        for (int32_t v = 1; v <= 3; v++) ok &= lis_push(&s, v);
        CHECK(ok && s.len == 3 && !lis_push(&s, 4) && s.len == 3 && lis_push(&s, 0) && s.len == 3);
    }
    {   /* 전수: 길이 0..7, 값 {0,1,2,3} 의 모든 배열 (중복 포함) → 부분집합 브루트포스와 대조 + 복원 검증 */
        unsigned long arrays = 0, bad = 0, bad_rec = 0;
        int32_t a[7];
        size_t idx[7];
        for (size_t n = 0; n <= 7; n++) {
            uint32_t total = 1u << (2 * n);
            for (uint32_t code = 0; code < total; code++) {
                for (size_t i = 0; i < n; i++) a[i] = (int32_t)((code >> (2 * i)) & 3u);
                size_t want = brute_lis(a, n);
                arrays++;
                bad += lis_memo(a, n) != want || stream_len(a, n) != want;
                bad_rec += lis_n2(a, n, idx) != want || !lis_valid(a, n, idx, want);
                bad_rec += lis_nlogn(a, n, idx) != want || !lis_valid(a, n, idx, want);
            }
        }
        printf("Part C exhaustive: %lu arrays (n<=7, values 0..3), mismatches len=%lu reconstruct=%lu\n", arrays, bad, bad_rec);
        CHECK(bad == 0);
        CHECK(bad_rec == 0);
    }
    {   /* 무작위: n ≤ 16 은 브루트포스, n ≤ 600 은 네 구현끼리 + 복원 검증 */
        static int32_t a[600];
        static size_t idx[600];
        unsigned long bad = 0;
        for (int t = 0; t < 1500; t++) {
            size_t n = xorshift32() % 17;
            uint32_t range = 1 + xorshift32() % 40;
            for (size_t i = 0; i < n; i++) a[i] = (int32_t)(xorshift32() % range) - 20;
            size_t want = brute_lis(a, n);
            bad += lis_memo(a, n) != want || lis_nlogn(a, n, idx) != want || !lis_valid(a, n, idx, want);
        }
        unsigned long bad_big = 0;
        for (int t = 0; t < 60; t++) {
            size_t n = xorshift32() % 601;
            for (size_t i = 0; i < n; i++) a[i] = (int32_t)xorshift32();      /* 전 범위 (음수 포함) */
            if (t % 3 == 0) for (size_t i = 0; i < n; i++) a[i] = (int32_t)(i / 2) + (int32_t)(xorshift32() % 7);  /* 거의 증가 */
            size_t want = lis_n2(a, n, idx);
            bad_big += !lis_valid(a, n, idx, want) || lis_memo(a, n) != want || stream_len(a, n) != want;
            bad_big += lis_nlogn(a, n, idx) != want || !lis_valid(a, n, idx, want);
        }
        printf("Part C random: 1500 small (vs brute) mismatches=%lu, 60 large n<=600 mismatches=%lu\n", bad, bad_big);
        CHECK(bad == 0);
        CHECK(bad_big == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
