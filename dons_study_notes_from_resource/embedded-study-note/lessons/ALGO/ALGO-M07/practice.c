/* ALGO-M07 추가 연습 — 한 번에 검증
 *
 *   연습 1  인접 금지 최대합 (House Robber): 연속 두 슬롯에 고출력 송신 금지 → 우선순위 합 최대. O(1) 공간
 *   연습 2  Decode Ways: '1'..'26' → 'A'..'Z' 로 읽는 방법 수 ('0' 처리, 포화 덧셈). O(1) 공간
 *   연습 3  비감소 LIS (같은 값 허용) → upper_bound 한 글자 차이. 정렬되게 만드는 최소 삭제 수 = n - LNDS
 *   연습 4  LIS 개수 (Number of LIS): len[i], cnt[i] 두 배열. 2^(n/2) 로 커지는 입력에서 uint64 포화
 *   연습 5  최소 동전 + 실제 사용한 동전 목록 (DMA 버스트 계획): choice[] 로 복원
 *
 * 모든 함수는 열거 브루트포스와 대조 (xorshift32 고정 시드) + LeetCode 예제.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint64_t sat_add_u64(uint64_t a, uint64_t b) { return (a > UINT64_MAX - b) ? UINT64_MAX : a + b; }

/* ---------- 연습 1: 인접 금지 최대합 ----------
 * best(i) = max(best(i-1), best(i-2) + v[i]).  두 변수만 굴린다. */
uint64_t rob_max(const uint32_t *v, size_t n) {
    uint64_t p2 = 0, p1 = 0;                 /* best(i-2), best(i-1) */
    for (size_t i = 0; i < n; i++) {
        uint64_t take = p2 + v[i];
        uint64_t cur = take > p1 ? take : p1;
        p2 = p1;
        p1 = cur;
    }
    return p1;
}

/* ---------- 연습 2: Decode Ways ----------
 * d(i) = s[0..i) 을 읽는 방법 수.  d(0) = 1.
 * d(i) = [s[i-1] != '0'] d(i-1) + [10 ≤ s[i-2..i) ≤ 26] d(i-2) */
uint64_t decode_ways(const char *s) {
    uint64_t p2 = 0, p1 = 1;                 /* d(i-2), d(i-1); 시작은 d(-1) 대신 0, d(0) = 1 */
    for (size_t i = 0; s[i]; i++) {
        uint64_t cur = 0;
        if (s[i] < '0' || s[i] > '9') return 0;
        if (s[i] != '0') cur = p1;
        if (i > 0) {
            int two = (s[i - 1] - '0') * 10 + (s[i] - '0');
            if (s[i - 1] != '0' && two <= 26) cur = sat_add_u64(cur, p2);
        }
        p2 = p1;
        p1 = cur;
    }
    return p1;
}

/* ---------- 연습 3: 비감소 LIS ---------- tails 에서 "첫 > x" (upper_bound) */
size_t lnds_len(const int32_t *a, size_t n, int32_t *tails) {
    size_t len = 0;
    for (size_t i = 0; i < n; i++) {
        size_t lo = 0, hi = len;
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (tails[mid] <= a[i]) lo = mid + 1;
            else                    hi = mid;
        }
        tails[lo] = a[i];
        if (lo == len) len++;
    }
    return len;
}

/* ---------- 연습 4: LIS 개수 ----------
 * len[i] = a[i] 로 끝나는 LIS 길이, cnt[i] = 그 길이를 내는 부분수열 수.
 * j < i, a[j] < a[i]:  len[j]+1 > len[i] → (len, cnt) = (len[j]+1, cnt[j]);  == → cnt[i] += cnt[j] */
#define LC_MAX 1024
uint64_t lis_count(const int32_t *a, size_t n) {
    static size_t len[LC_MAX];
    static uint64_t cnt[LC_MAX];
    if (n > LC_MAX) return 0;
    size_t best = 0;
    uint64_t total = 0;
    for (size_t i = 0; i < n; i++) {
        len[i] = 1;
        cnt[i] = 1;
        for (size_t j = 0; j < i; j++) {
            if (a[j] >= a[i]) continue;
            if (len[j] + 1 > len[i])       { len[i] = len[j] + 1; cnt[i] = cnt[j]; }
            else if (len[j] + 1 == len[i]) cnt[i] = sat_add_u64(cnt[i], cnt[j]);
        }
        if (len[i] > best)       { best = len[i]; total = cnt[i]; }
        else if (len[i] == best) total = sat_add_u64(total, cnt[i]);
    }
    return total;
}

/* ---------- 연습 5: 최소 동전 + 사용한 동전 목록 ----------
 * dp[a] 와 함께 choice[a] = 최적해의 "마지막 동전" 인덱스를 저장 → a 에서 coins[choice[a]] 를 빼며 거꾸로 걷는다.
 * 반환: 개수 (불가능 -1, out 용량 부족 -2). out 에는 사용한 동전 값. */
#define CP_MAX_AMT 4096
int32_t coin_plan(const uint32_t *coins, size_t k, uint32_t amount, uint32_t *out, size_t cap) {
    static uint16_t dp[CP_MAX_AMT + 1];
    static uint8_t choice[CP_MAX_AMT + 1];
    if (amount > CP_MAX_AMT || k > 255) return -1;
    dp[0] = 0;
    for (uint32_t a = 1; a <= amount; a++) {
        dp[a] = UINT16_MAX;
        for (size_t j = 0; j < k; j++) {
            uint32_t c = coins[j];
            if (c >= 1 && c <= a && dp[a - c] != UINT16_MAX && dp[a - c] + 1 < dp[a]) {
                dp[a] = (uint16_t)(dp[a - c] + 1);
                choice[a] = (uint8_t)j;
            }
        }
    }
    if (dp[amount] == UINT16_MAX) return -1;
    if (dp[amount] > cap) return -2;
    size_t p = 0;
    for (uint32_t a = amount; a > 0; a -= coins[choice[a]]) out[p++] = coins[choice[a]];
    return (int32_t)dp[amount];
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

static uint64_t brute_rob(const uint32_t *v, size_t n) {           /* 인접 비트가 없는 모든 마스크 */
    uint64_t best = 0;
    for (uint32_t m = 0; m < (1u << n); m++) {
        if (m & (m >> 1)) continue;
        uint64_t s = 0;
        for (size_t i = 0; i < n; i++) if ((m >> i) & 1u) s += v[i];
        if (s > best) best = s;
    }
    return best;
}
static uint64_t brute_decode(const char *s) {                       /* 한 글자 / 두 글자로 자르는 모든 방법 */
    if (!*s) return 1;
    uint64_t w = 0;
    if (*s != '0') w += brute_decode(s + 1);
    if (s[1] && *s != '0' && (*s - '0') * 10 + (s[1] - '0') <= 26) w += brute_decode(s + 2);
    return w;
}
/* 부분집합 열거: strict = 1 이면 순증가, 0 이면 비감소. 최대 길이와 그 길이를 내는 부분집합 수 */
static void brute_inc(const int32_t *a, size_t n, int strict, size_t *best_len, uint64_t *best_cnt) {
    *best_len = 0; *best_cnt = 1;      /* 빈 부분수열 1개 (n == 0 일 때의 규약은 테스트에서 따로) */
    for (uint32_t m = 1; m < (1u << n); m++) {
        size_t c = 0;
        bool ok = true, have = false;
        int32_t last = 0;
        for (size_t i = 0; i < n && ok; i++) {
            if (!((m >> i) & 1u)) continue;
            if (have && (strict ? a[i] <= last : a[i] < last)) ok = false;
            last = a[i]; have = true; c++;
        }
        if (!ok) continue;
        if (c > *best_len) { *best_len = c; *best_cnt = 1; }
        else if (c == *best_len) (*best_cnt)++;
    }
}
static int32_t bm_min;
static void brute_min(const uint32_t *coins, size_t k, size_t i, uint32_t left, int32_t used) {
    if (left == 0) { if (bm_min < 0 || used < bm_min) bm_min = used; return; }
    for (size_t j = i; j < k; j++) if (coins[j] <= left) brute_min(coins, k, j, left - coins[j], used + 1);
}

int main(void) {
    /* ---- 연습 1 ---- */
    {
        const uint32_t e1[] = { 1, 2, 3, 1 }, e2[] = { 2, 7, 9, 3, 1 };
        CHECK(rob_max(e1, 4) == 4 && rob_max(e2, 5) == 12);           /* LeetCode 198 예제 */
        CHECK(rob_max(NULL, 0) == 0 && rob_max(e2, 1) == 2);
        const uint32_t big[] = { UINT32_MAX, 0, UINT32_MAX, 0, UINT32_MAX };
        CHECK(rob_max(big, 5) == 3ull * UINT32_MAX);                  /* uint32 합은 uint64 로 */
        uint32_t v[18];
        unsigned long bad = 0;
        for (int t = 0; t < 2000; t++) {
            size_t n = xorshift32() % 19;
            for (size_t i = 0; i < n; i++) v[i] = xorshift32() % 100;
            bad += rob_max(v, n) != brute_rob(v, n);
        }
        printf("연습 1: 2000 random arrays (n<=18) vs mask brute force, mismatches=%lu\n", bad);
        CHECK(bad == 0);
    }
    /* ---- 연습 2 ---- */
    {
        CHECK(decode_ways("12") == 2 && decode_ways("226") == 3 && decode_ways("06") == 0);   /* LeetCode 91 예제 */
        CHECK(decode_ways("") == 1 && decode_ways("10") == 1 && decode_ways("100") == 0 && decode_ways("27") == 1
              && decode_ways("1x") == 0);
        char s[24];
        unsigned long bad = 0;
        for (int t = 0; t < 5000; t++) {
            size_t n = xorshift32() % 24;
            for (size_t i = 0; i < n; i++) {
                uint32_t r = xorshift32() % 10;
                s[i] = (char)('0' + (r < 4 ? 1 + r % 2 : xorshift32() % 10));   /* 1·2 가 많게 */
            }
            s[n] = '\0';
            bad += decode_ways(s) != brute_decode(s);
        }
        printf("연습 2: 5000 random digit strings (len<=23) vs split brute force, mismatches=%lu\n", bad);
        CHECK(bad == 0);
        /* "1111…1" (n 개) → F(n+1).  92 개 → F(93), 93 개 → 포화 */
        char ones[94];
        memset(ones, '1', 93); ones[92] = '\0';
        CHECK(decode_ways(ones) == 12200160415121876738ull);
        ones[92] = '1'; ones[93] = '\0';
        CHECK(decode_ways(ones) == UINT64_MAX);
    }
    /* ---- 연습 3 · 4 ---- */
    {
        int32_t tails[16];
        const int32_t e1[] = { 1, 3, 5, 4, 7 }, e2[] = { 2, 2, 2, 2, 2 };
        CHECK(lis_count(e1, 5) == 2 && lis_count(e2, 5) == 5);         /* LeetCode 673 예제 */
        CHECK(lnds_len(e2, 5, tails) == 5 && lnds_len(e1, 5, tails) == 4);
        const int32_t sens[] = { 3, 3, 4, 2, 4, 5, 5, 1 };               /* 같은 값 허용 → 3,3,4,4,5,5 */
        CHECK(lnds_len(sens, 8, tails) == 6 && 8 - lnds_len(sens, 8, tails) == 2);
        int32_t a[16];
        unsigned long bad3 = 0, bad4 = 0;
        for (int t = 0; t < 3000; t++) {
            size_t n = 1 + xorshift32() % 15;
            uint32_t range = 1 + xorshift32() % 8;
            for (size_t i = 0; i < n; i++) a[i] = (int32_t)(xorshift32() % range);
            size_t bl; uint64_t bc;
            brute_inc(a, n, 0, &bl, &bc);
            bad3 += lnds_len(a, n, tails) != bl;
            brute_inc(a, n, 1, &bl, &bc);
            bad4 += lis_count(a, n) != bc;
        }
        printf("연습 3/4: 3000 random arrays (n<=15) vs subset brute force, mismatches lnds=%lu count=%lu\n", bad3, bad4);
        CHECK(bad3 == 0);
        CHECK(bad4 == 0);
        /* (1,0),(3,2),(5,4),… m 쌍 → LIS 길이 m, 개수 2^m.  m = 63 은 정확, m = 64 는 포화 */
        static int32_t pairs[128];
        for (int32_t i = 0; i < 64; i++) { pairs[2 * i] = 2 * i + 1; pairs[2 * i + 1] = 2 * i; }
        CHECK(lis_count(pairs, 126) == (1ull << 63) && lis_count(pairs, 128) == UINT64_MAX);
    }
    /* ---- 연습 5 ---- */
    {
        uint32_t out[64];
        const uint32_t bursts[] = { 1, 4, 6, 9 };                        /* 탐욕이 틀리는 버스트 집합: 12 = 6+6 */
        int32_t c = coin_plan(bursts, 4, 12, out, 64);
        CHECK(c == 2 && out[0] + out[1] == 12);
        CHECK(coin_plan(bursts, 4, 12, out, 1) == -2);
        const uint32_t even[] = { 2, 4 };
        CHECK(coin_plan(even, 2, 7, out, 64) == -1 && coin_plan(even, 2, 0, out, 64) == 0);
        uint32_t coins[5];
        unsigned long bad = 0;
        for (int t = 0; t < 1500; t++) {
            size_t k = 1 + xorshift32() % 5;
            for (size_t j = 0; j < k; j++) coins[j] = 1 + xorshift32() % 12;   /* 중복 동전도 허용: 결과는 같아야 */
            uint32_t amt = xorshift32() % 40;
            bm_min = -1;
            brute_min(coins, k, 0, amt, 0);
            c = coin_plan(coins, k, amt, out, 64);
            if (c != bm_min) { bad++; continue; }
            uint32_t sum = 0;
            for (int32_t p = 0; p < c; p++) {
                bool in = false;
                for (size_t j = 0; j < k; j++) in |= out[p] == coins[j];
                bad += !in;
                sum += out[p];
            }
            bad += c >= 0 && sum != amt;
        }
        printf("연습 5: 1500 random (coins<=12, amount<40) vs multiset brute force, mismatches=%lu\n", bad);
        CHECK(bad == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
