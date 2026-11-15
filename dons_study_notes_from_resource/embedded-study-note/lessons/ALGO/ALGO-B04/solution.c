/* ALGO-B04 답을 이분 탐색: Parametric Search
 *
 * "최솟값을 직접 구하기 어렵다 → '이 값으로 되나?'(판정 함수)를 묻고, 그 답이 단조이면 답 공간을 이분 탐색"
 *   Part A  패킷 n개(순서 고정, 쪼갤 수 없음)를 D개 프레임에 담는 최소 프레임 용량
 *           frames_needed() = 그리디 판정,  min_frame_capacity() = [max, sum] 에서 first-true 이분 탐색
 *   Part B  정수 제곱근 floor(sqrt(x)): isqrt32 (나눗셈 비교, 64비트 없음) / isqrt64 (hi 클램프 + 64비트 곱)
 *           mid*mid 오버플로, last-true 탐색의 위쪽 mid, x = 0/1/k^2-1/UINT_MAX 경계
 *   Part C  RS-485 TDMA 로거: 기록 n개를 D 슬롯 안에 → 최소 슬롯 페이로드(= DMA 버퍼 크기) → 최소 보드레이트,
 *           표준 보드레이트 표에서 고르기 (판정 함수 + 이분 탐색을 한 번 더)
 *
 * 테스트: 손 계산 예제 + 고정 시드 xorshift32 랜덤을 브루트포스(자르는 위치 전수 조사 / O(D n^2) DP /
 *         선형 증가 isqrt / s*s <= x < (s+1)^2 직접 확인 / 닫힌 식)와 대조.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ======================= Part A — 최소 프레임 용량 ======================= */
/* 용량 cap 인 프레임에 패킷을 순서대로 "넣을 수 있을 만큼 넣고, 넘치면 새 프레임" (그리디)으로 담을 때
 * 필요한 프레임 수. 패킷 하나가 cap 보다 크면 어떤 방법으로도 불가능 → SIZE_MAX. n == 0 이면 0. */
static size_t frames_needed(const uint32_t *sz, size_t n, uint64_t cap) {
    size_t frames = 0;
    uint64_t used = 0;                     /* 지금 프레임에 담긴 바이트 */
    for (size_t i = 0; i < n; i++) {
        if (sz[i] > cap) return SIZE_MAX;  /* 하나도 못 담는 패킷 */
        if (frames == 0 || used + sz[i] > cap) { frames++; used = 0; }
        used += sz[i];
    }
    return frames;
}

/* 판정 함수 (주어짐): "용량 cap 이면 D 프레임 안에 되나?" — 호출 횟수를 세어 O(log) 를 확인한다 */
static unsigned long g_pred_calls;
static bool fits(const uint32_t *sz, size_t n, size_t D, uint64_t cap) {
    g_pred_calls++;
    return frames_needed(sz, n, cap) <= D;
}

/* 패킷 n개를 순서대로 D 프레임 안에 보내기 위한 최소 프레임 용량.
 * n == 0 → 0 (보낼 게 없음),  n > 0 && D == 0 → UINT64_MAX (불가능).
 * 답 공간 [max(sz), sum(sz)] 에서 fits() 가 처음 true 가 되는 곳 (first true). 합은 uint64 로. */
static uint64_t min_frame_capacity(const uint32_t *sz, size_t n, size_t D) {
    if (n == 0) return 0;
    if (D == 0) return UINT64_MAX;
    uint64_t lo = 0, hi = 0;
    for (size_t i = 0; i < n; i++) {
        if (sz[i] > lo) lo = sz[i];
        hi += sz[i];
    }
    /* 불변식: 답 ∈ [lo, hi].  fits(hi) == true (D >= 1 이면 한 프레임에 전부),  lo 미만은 전부 false */
    while (lo < hi) {
        uint64_t mid = lo + (hi - lo) / 2;  /* 아래쪽 mid: lo <= mid < hi */
        if (fits(sz, n, D, mid)) hi = mid;  /* mid 가 답이거나 답은 더 왼쪽 */
        else lo = mid + 1;                  /* mid 이하는 전부 false (단조성) */
    }
    return lo;
}

/* ======================= Part B — 정수 제곱근 ======================= */
/* floor(sqrt(x)) = s*s <= x 인 가장 큰 s  (last true).  64비트 연산 없이: mid*mid <= x  ⇔  mid <= x / mid (mid >= 1) */
static uint32_t isqrt32(uint32_t x) {
    uint32_t lo = 0, hi = x < 65535u ? x : 65535u;   /* 65536^2 = 2^32 > UINT32_MAX → 답 <= 65535 */
    /* 불변식: lo*lo <= x (lo 는 true),  답 <= hi */
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo + 1) / 2;       /* 위쪽 mid: lo < mid <= hi, 그래서 mid >= 1 */
        if (mid <= x / mid) lo = mid;                /* mid*mid <= x */
        else hi = mid - 1;
    }
    return lo;
}

/* 64비트판: 답 <= 2^32 - 1 이므로 hi 를 거기로 자르면 mid*mid <= (2^32-1)^2 < 2^64 — 곱이 넘치지 않는다 */
static uint64_t isqrt64(uint64_t x) {
    uint64_t lo = 0, hi = x < 0xFFFFFFFFu ? x : 0xFFFFFFFFu;
    while (lo < hi) {
        uint64_t mid = lo + (hi - lo + 1) / 2;
        if (mid * mid <= x) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

/* ======================= Part C — TDMA 로거의 버퍼·보드레이트 ======================= */
/* UART 8N1 = 바이트당 선로 비트 10개 (start + 8 data + stop).  slot_us 마이크로초 슬롯에 들어가는 바이트 수 (주어짐) */
static uint64_t slot_bytes(uint32_t baud, uint32_t slot_us) {
    return (uint64_t)baud * slot_us / 10000000u;      /* floor(baud/10 * slot_us/1e6) */
}

/* slot_bytes(b, slot_us) >= need 인 가장 작은 정수 보드레이트 b ∈ [1, baud_max]. 없으면 0. need == 0 이면 1. */
static uint32_t min_baud_exact(uint64_t need, uint32_t slot_us, uint32_t baud_max) {
    if (baud_max == 0 || slot_bytes(baud_max, slot_us) < need) return 0;
    uint32_t lo = 1, hi = baud_max;                   /* 불변식: 답 ∈ [lo, hi], hi 는 true */
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (slot_bytes(mid, slot_us) >= need) hi = mid;
        else lo = mid + 1;
    }
    return lo;
}

/* 오름차순 표준 보드레이트 표에서 slot_bytes >= need 인 첫 항목의 인덱스, 없으면 -1 (표 위의 first true) */
static int pick_std_baud(const uint32_t *tbl, size_t nb, uint64_t need, uint32_t slot_us) {
    size_t lo = 0, hi = nb;                           /* [lo, hi): 답 인덱스 후보, hi == nb 는 "없음" */
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (slot_bytes(tbl[mid], slot_us) >= need) hi = mid;
        else lo = mid + 1;
    }
    return lo < nb ? (int)lo : -1;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 2463534242u;                  /* xorshift32, 고정 시드 */
static uint32_t xs32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }
static uint64_t xs64(void) { uint64_t h = xs32(); return (h << 32) | xs32(); }

/* --- 브루트포스 1: 자르는 위치 전수 조사 (n <= 14). 그리디와 무관한 독립 기준 --- */
static uint64_t brute_cuts(const uint32_t *sz, size_t n, size_t D) {
    if (n == 0) return 0;
    if (D == 0) return UINT64_MAX;
    uint64_t best = UINT64_MAX;
    for (uint32_t mask = 0; mask < (1u << (n - 1)); mask++) {   /* 비트 i = 패킷 i 와 i+1 사이를 자름 */
        size_t cuts = 0;
        for (uint32_t m = mask; m; m &= m - 1) cuts++;
        if (cuts + 1 > D) continue;
        uint64_t cur = 0, worst = 0;
        for (size_t i = 0; i < n; i++) {
            cur += sz[i];
            if (i == n - 1 || (mask >> i) & 1u) { if (cur > worst) worst = cur; cur = 0; }
        }
        if (worst < best) best = worst;
    }
    return best;
}

/* --- 브루트포스 2: DP. dp[k][i] = 앞 i개를 정확히 k 프레임(빈 프레임 허용 안 함)에 담을 때 최대 프레임 합의 최솟값 --- */
#define DPN 48
static uint64_t brute_dp(const uint32_t *sz, size_t n, size_t D) {
    static uint64_t dp[DPN + 1][DPN + 1];
    uint64_t pre[DPN + 1] = {0};
    if (n == 0) return 0;
    if (D == 0) return UINT64_MAX;
    if (D > n) D = n;                                  /* 프레임이 남으면 비워 두면 된다 */
    for (size_t i = 0; i < n; i++) pre[i + 1] = pre[i] + sz[i];
    for (size_t k = 0; k <= D; k++) for (size_t i = 0; i <= n; i++) dp[k][i] = UINT64_MAX;
    dp[0][0] = 0;
    for (size_t k = 1; k <= D; k++)
        for (size_t i = 1; i <= n; i++)
            for (size_t j = k - 1; j < i; j++) {       /* 마지막 프레임 = 패킷 j..i-1 */
                if (dp[k - 1][j] == UINT64_MAX) continue;
                uint64_t last = pre[i] - pre[j];
                uint64_t v = dp[k - 1][j] > last ? dp[k - 1][j] : last;
                if (v < dp[k][i]) dp[k][i] = v;
            }
    uint64_t best = UINT64_MAX;
    for (size_t k = 1; k <= D; k++) if (dp[k][n] < best) best = dp[k][n];
    return best;
}

/* --- 브루트포스 3: isqrt 정답 확인. s*s <= x < (s+1)^2 를 넘침 없이 --- */
static bool isqrt_ok64(uint64_t x, uint64_t s) {
    if (s > 0xFFFFFFFFu || s * s > x) return false;
    if (s == 0xFFFFFFFFu) return true;                 /* (s+1)^2 = 2^64 > 어떤 x 든 */
    return (s + 1) * (s + 1) > x;                      /* s+1 <= 2^32-1 → 곱이 2^64 미만 */
}

static uint32_t log2_ceil_u64(uint64_t v) { uint32_t k = 0; while (k < 64 && (1ull << k) < v) k++; return k; }

int main(void) {
    /* ---------- Part A: 손 계산 ---------- */
    const uint32_t w1[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    CHECK(frames_needed(w1, 10, 15) == 5);             /* [1..5] [6,7] [8] [9] [10] */
    CHECK(frames_needed(w1, 10, 14) == 6);
    CHECK(frames_needed(w1, 10, 9) == SIZE_MAX);       /* 10 바이트 패킷이 안 들어감 */
    CHECK(frames_needed(w1, 0, 1) == 0);
    CHECK(min_frame_capacity(w1, 10, 5) == 15);
    const uint32_t w2[] = {3, 2, 2, 4, 1, 4};
    CHECK(min_frame_capacity(w2, 6, 3) == 6);          /* [3,2] [2,4] [1,4] */
    const uint32_t w3[] = {1, 2, 3, 1, 1};
    CHECK(min_frame_capacity(w3, 5, 4) == 3);          /* [1,2] [3] [1,1] (+빈 프레임 하나) */
    CHECK(min_frame_capacity(w1, 10, 1) == 55);        /* D = 1 → 합 */
    CHECK(min_frame_capacity(w1, 10, 10) == 10);       /* D >= n → 최댓값 */
    CHECK(min_frame_capacity(w1, 10, 1000) == 10);
    const uint32_t one[] = {42};
    CHECK(min_frame_capacity(one, 1, 1) == 42);
    CHECK(min_frame_capacity(w1, 0, 3) == 0);          /* 보낼 게 없음 */
    CHECK(min_frame_capacity(w1, 10, 0) == UINT64_MAX);
    const uint32_t eq[] = {7, 7, 7, 7, 7, 7, 7};
    CHECK(min_frame_capacity(eq, 7, 3) == 21);         /* ceil(7/3) = 3 개씩 */
    const uint32_t zeros[] = {0, 0, 5, 0, 0};
    CHECK(min_frame_capacity(zeros, 5, 2) == 5);
    const uint32_t big[] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    CHECK(min_frame_capacity(big, 4, 2) == 2ull * 0xFFFFFFFFu);   /* 합이 32비트를 넘는다 */
    CHECK(min_frame_capacity(big, 4, 1) == 4ull * 0xFFFFFFFFu);

    /* ---------- Part A: 랜덤 대조 ---------- */
    int bad_cuts = 0, bad_dp = 0, bad_mono = 0, bad_calls = 0;
    for (int t = 0; t < 4000; t++) {                   /* n <= 12, 전수 조사 */
        uint32_t a[14];
        size_t n = xs32() % 13, D = xs32() % 14;
        uint32_t maxv = (t & 1) ? 10 : 1000;
        for (size_t i = 0; i < n; i++) a[i] = xs32() % (maxv + 1);
        if (min_frame_capacity(a, n, D) != brute_cuts(a, n, D)) bad_cuts++;
    }
    for (int t = 0; t < 1500; t++) {                   /* n <= 48, DP (값은 가끔 32비트 전체) */
        uint32_t a[DPN];
        size_t n = 1 + xs32() % DPN, D = 1 + xs32() % (n + 2);
        for (size_t i = 0; i < n; i++) a[i] = (t % 3 == 0) ? xs32() : xs32() % 200;
        uint64_t sum = 0, mx = 0;
        for (size_t i = 0; i < n; i++) { sum += a[i]; if (a[i] > mx) mx = a[i]; }
        g_pred_calls = 0;
        if (min_frame_capacity(a, n, D) != brute_dp(a, n, D)) bad_dp++;
        if (g_pred_calls > log2_ceil_u64(sum - mx + 1)) bad_calls++;   /* first true: ceil(log2(구간 크기)) 회 이하 */
    }
    for (int t = 0; t < 300; t++) {                    /* 판정 함수의 단조성 자체를 확인: cap 이 커지면 프레임 수는 안 늘어난다 */
        uint32_t a[20];
        size_t n = 1 + xs32() % 20;
        uint64_t sum = 0, mx = 0;
        for (size_t i = 0; i < n; i++) { a[i] = xs32() % 30; sum += a[i]; if (a[i] > mx) mx = a[i]; }
        size_t prev = SIZE_MAX;
        for (uint64_t c = mx; c <= sum; c++) { size_t f = frames_needed(a, n, c); if (f > prev || f == 0) bad_mono++; prev = f; }
    }
    printf("Part A random: cuts-brute mismatches=%d  dp-brute mismatches=%d  monotonic violations=%d  too-many-probes=%d\n",
           bad_cuts, bad_dp, bad_mono, bad_calls);
    CHECK(bad_cuts == 0);
    CHECK(bad_dp == 0);
    CHECK(bad_mono == 0);
    CHECK(bad_calls == 0);

    /* ---------- Part B: 정수 제곱근 ---------- */
    CHECK(isqrt32(0) == 0);
    CHECK(isqrt32(1) == 1);
    CHECK(isqrt32(2) == 1 && isqrt32(3) == 1 && isqrt32(4) == 2);
    CHECK(isqrt32(8) == 2 && isqrt32(9) == 3);
    CHECK(isqrt32(4294836224u) == 65534);              /* 65535^2 - 1 */
    CHECK(isqrt32(4294836225u) == 65535);              /* 65535^2 */
    CHECK(isqrt32(UINT32_MAX) == 65535);
    CHECK(isqrt64(0) == 0 && isqrt64(1) == 1);
    CHECK(isqrt64(0xFFFFFFFE00000001ull) == 0xFFFFFFFFull);       /* (2^32-1)^2 */
    CHECK(isqrt64(0xFFFFFFFE00000000ull) == 0xFFFFFFFEull);       /* (2^32-1)^2 - 1 */
    CHECK(isqrt64(UINT64_MAX) == 0xFFFFFFFFull);
    CHECK(isqrt64(1ull << 62) == 1ull << 31);
    CHECK(isqrt64((1ull << 62) - 1) == (1ull << 31) - 1);
    CHECK(isqrt64(18014398509481983ull) == 134217727ull);        /* 2^54 - 1: double 로는 틀리는 값 (5. 풀이) */

    int bad32 = 0, bad64 = 0, badsq = 0, badcross = 0;
    {   /* 0..2^20 전수: 선형으로 키우는 브루트포스와 대조 */
        uint32_t s = 0;
        for (uint32_t x = 0; x <= (1u << 20); x++) {
            while ((uint64_t)(s + 1) * (s + 1) <= x) s++;
            if (isqrt32(x) != s) bad32++;
            if (isqrt64(x) != s) bad64++;
        }
    }
    for (uint32_t k = 1; k <= 65535; k++) {            /* 모든 완전제곱 k^2 과 그 양옆 (32비트 전 범위) */
        uint32_t sq = k * k;                           /* k <= 65535 → 넘치지 않음 */
        if (isqrt32(sq) != k || isqrt32(sq - 1) != k - 1 || isqrt32(sq + 1) != k) badsq++;
    }
    for (int t = 0; t < 300000; t++) {                 /* 랜덤 32비트: 64비트판과 교차, 64비트 곱으로 직접 확인 */
        uint32_t x = xs32() >> (xs32() % 32);
        uint32_t s = isqrt32(x);
        if (!isqrt_ok64(x, s)) bad32++;
        if (isqrt64(x) != s) badcross++;
    }
    for (int t = 0; t < 300000; t++) {                 /* 랜덤 64비트 (비트 길이도 랜덤) */
        uint64_t x = xs64() >> (xs32() % 64);
        if (!isqrt_ok64(x, isqrt64(x))) bad64++;
    }
    for (int t = 0; t < 100000; t++) {                 /* 랜덤 k 의 k^2-1, k^2, k^2+1 (64비트) */
        uint64_t k = 1 + xs32() % 0xFFFFFFFFu;         /* 1 .. 2^32-1 */
        uint64_t sq = k * k;
        if (isqrt64(sq) != k || isqrt64(sq - 1) != k - 1) badsq++;
        if (k < 0xFFFFFFFFu && isqrt64(sq + 1) != k) badsq++;
    }
    printf("Part B: isqrt32 bad=%d  isqrt64 bad=%d  around-squares bad=%d  32/64 cross bad=%d\n",
           bad32, bad64, badsq, badcross);
    CHECK(bad32 == 0);
    CHECK(bad64 == 0);
    CHECK(badsq == 0);
    CHECK(badcross == 0);

    /* ---------- Part C: TDMA 로거 ---------- */
    CHECK(slot_bytes(115200, 1000) == 11);             /* 11520 B/s × 1 ms = 11.52 → 11 */
    CHECK(min_baud_exact(46, 500, 5000000) == 920000); /* 46 B / 0.5 ms × 10 bit */
    CHECK(min_baud_exact(0, 500, 5000000) == 1);
    CHECK(min_baud_exact(1000, 500, 5000000) == 0);    /* 5 Mbaud 로도 0.5 ms 에 250 B 뿐 */
    int bad_baud = 0;
    for (int t = 0; t < 20000; t++) {                  /* 닫힌 식 ceil(need * 1e7 / slot_us) 과 대조 */
        uint64_t need = xs32() % 4096;
        uint32_t slot_us = 50 + xs32() % 20000, bmax = 10000000;
        uint64_t closed = need == 0 ? 1 : (need * 10000000u + slot_us - 1) / slot_us;
        uint32_t got = min_baud_exact(need, slot_us, bmax);
        if (closed <= bmax ? got != closed : got != 0) bad_baud++;
    }
    CHECK(bad_baud == 0);

    static const uint32_t std_baud[] = {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600, 1000000, 2000000, 3000000};
    const size_t nb = sizeof std_baud / sizeof std_baud[0];
    int bad_pick = 0;
    for (int t = 0; t < 20000; t++) {                  /* 표준 표에서 고르기: 선형 탐색과 대조 */
        uint64_t need = xs32() % 800;
        uint32_t slot_us = 100 + xs32() % 5000;
        int lin = -1;
        for (size_t i = 0; i < nb; i++) if (slot_bytes(std_baud[i], slot_us) >= need) { lin = (int)i; break; }
        if (pick_std_baud(std_baud, nb, need, slot_us) != lin) bad_pick++;
    }
    CHECK(bad_pick == 0);

    /* 시나리오: 20 ms 로깅 창에 내 노드 슬롯 D = 8 개 (각 1 ms). 기록 = IMU 28 B, 기압 12 B, GPS NMEA, 상태 16 B, 이벤트 */
    static const uint32_t rec[] = {28, 12, 28, 72, 28, 16, 28, 12, 28, 64, 28, 16,
                                   28, 12, 28, 40, 28, 16, 28, 12, 28, 72, 28, 16};
    const size_t nrec = sizeof rec / sizeof rec[0];
    const size_t D = 8;
    const uint32_t slot_us = 1000;
    uint64_t cap = min_frame_capacity(rec, nrec, D);
    uint32_t need_baud = min_baud_exact(cap, slot_us, 10000000);
    int pick = pick_std_baud(std_baud, nb, cap, slot_us);
    printf("Part C: %zu records, %zu slots x %u us -> min slot payload (= DMA TX buffer) %llu B, frames used %zu,"
           " min baud %u, pick %u (slot holds %llu B)\n",
           nrec, D, slot_us, (unsigned long long)cap, frames_needed(rec, nrec, cap), need_baud,
           pick >= 0 ? std_baud[pick] : 0u, pick >= 0 ? (unsigned long long)slot_bytes(std_baud[pick], slot_us) : 0ull);
    CHECK(cap == brute_dp(rec, nrec, D));
    CHECK(pick >= 0 && slot_bytes(std_baud[pick], slot_us) >= cap &&
          (pick == 0 || slot_bytes(std_baud[pick - 1], slot_us) < cap));
    CHECK(fits(rec, nrec, D, cap) && !fits(rec, nrec, D, cap - 1));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
