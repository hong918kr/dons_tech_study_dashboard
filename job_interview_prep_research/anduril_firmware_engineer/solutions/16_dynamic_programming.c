// 16_dynamic_programming.c  —  REFERENCE SOLUTION
// 동적 계획법 (Dynamic Programming, bottom-up 1D/2D 테이블)  —  Q1~Q9
// ---------------------------------------------------------------------------
// 빌드: cc -std=c11 -Wall -Wextra 16_dynamic_programming.c -o /tmp/andb_16 && /tmp/andb_16
//
// DP 사고 4단계 (매 문제 이 순서로 생각하라):
//   1) 상태(state)      : dp[i] / dp[i][j] 가 "무엇의 답"인지 한 문장으로 정의.
//   2) 전이(transition) : dp[i] 를 더 작은 dp[...] 로 표현하는 점화식(recurrence).
//   3) 기저(base case)  : 가장 작은 입력(n=0,1, 빈 문자열)의 값을 직접 채운다.
//   4) 순서(order)      : 의존하는 값이 먼저 계산되도록 루프 방향을 정한다.
//
// 임베디드 관점:
//   - DP = "자원/전력/대역폭 최적 배분"의 수학적 도구 (knapsack = 링크 대역 배분).
//   - edit distance = 명령어 퍼지 매칭/오타 보정, LCS = 펌웨어 diff/델타 업데이트.
//   - bottom-up + 고정 크기 테이블 => 재귀 스택 폭주 없이 결정론적 메모리/지연.
//   - 1D 롤링(rolling) 최적화로 O(n) 메모리를 O(1)/O(W) 로 줄이는 게 핵심 트릭.
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>

// ===========================================================================
// 테스트 하네스 (PASS/FAIL)
// ===========================================================================
static int g_pass = 0;
static int g_fail = 0;
#define T(label, cond) do {                                   \
    if (cond) { printf("  [PASS] %s\n", (label)); g_pass++; } \
    else      { printf("  [FAIL] %s\n", (label)); g_fail++; } \
} while (0)

static int imax(int a, int b) { return a > b ? a : b; }
static int imin(int a, int b) { return a < b ? a : b; }
static int imax3(int a, int b, int c) { return imax(a, imax(b, c)); }

// ===========================================================================
// Q1. 계단 오르기 (Climbing Stairs)
// ---------------------------------------------------------------------------
// 상태 : dp[i] = i 계단을 오르는 방법의 수 (한 번에 1 또는 2칸).
// 전이 : dp[i] = dp[i-1] + dp[i-2]          (마지막 한 걸음이 1칸 or 2칸)
// 기저 : dp[0] = 1 (아무 것도 안 함이 1가지), dp[1] = 1
// => 사실상 피보나치. 롤링 변수 2개로 O(1) 공간.
// 예: n=2 -> 2([1,1],[2]), n=3 -> 3, n=5 -> 8
// ===========================================================================
int climb_stairs(int n) {
    if (n < 0) return 0;
    if (n <= 1) return 1;               // 기저: dp[0]=dp[1]=1
    int prev2 = 1, prev1 = 1;           // dp[0], dp[1]
    for (int i = 2; i <= n; ++i) {
        int cur = prev1 + prev2;        // dp[i] = dp[i-1] + dp[i-2]
        prev2 = prev1;
        prev1 = cur;
    }
    return prev1;
}

// ===========================================================================
// Q2. 도둑질 (House Robber)
// ---------------------------------------------------------------------------
// 상태 : dp[i] = 0..i 집까지 고려했을 때 훔칠 수 있는 최대 금액 (인접 집 동시 금지).
// 전이 : dp[i] = max(dp[i-1],            // i 집을 안 털면 이전 최적 그대로
//                    dp[i-2] + nums[i])  // i 집을 털면 i-1 은 못 털므로 dp[i-2]
// 기저 : dp[-1]=0(가상), dp[0]=nums[0]
// => 롤링 변수 2개로 O(1) 공간.  n=0 -> 0.
// 예: [2,7,9,3,1] -> 12 (2+9+1),  [2,1,1,2] -> 4 (2+2)
// ===========================================================================
int house_robber(const int *nums, int n) {
    if (n <= 0) return 0;               // 기저: 집 없음
    int prev2 = 0;                      // dp[i-2] (가상 dp[-1]=0)
    int prev1 = 0;                      // dp[i-1]
    for (int i = 0; i < n; ++i) {
        int cur = imax(prev1, prev2 + nums[i]);   // 점화식
        prev2 = prev1;
        prev1 = cur;
    }
    return prev1;
}

// ===========================================================================
// Q3. 동전 교환 - 최소 개수 (Coin Change, fewest coins)
// ---------------------------------------------------------------------------
// 상태 : dp[a] = 금액 a 를 만드는 데 필요한 최소 동전 수 (불가능하면 무한대).
// 전이 : dp[a] = min over coin { dp[a - coin] + 1 }  (a-coin >= 0 일 때)
// 기저 : dp[0] = 0 (0원은 동전 0개)
// 순서 : a 를 1..amount 오름차순 (dp[a-coin] 이 먼저 계산됨).
// 반환 : 만들 수 없으면 -1.  amount=0 -> 0.
// 예: coins=[1,2,5], amount=11 -> 3 (5+5+1),  coins=[2], amount=3 -> -1
// ===========================================================================
int coin_change_min(const int *coins, int nc, int amount) {
    if (amount < 0) return -1;
    const int INF = INT_MAX - 1;                 // +1 오버플로 방지용 여유
    int *dp = malloc((size_t)(amount + 1) * sizeof *dp);
    if (!dp) return -1;
    dp[0] = 0;                                    // 기저
    for (int a = 1; a <= amount; ++a) {
        dp[a] = INF;
        for (int c = 0; c < nc; ++c) {
            if (coins[c] > 0 && a - coins[c] >= 0 && dp[a - coins[c]] + 1 < dp[a])
                dp[a] = dp[a - coins[c]] + 1;     // 점화식
        }
    }
    int ans = (dp[amount] >= INF) ? -1 : dp[amount];
    free(dp);
    return ans;
}

// ===========================================================================
// Q4. 최장 증가 부분수열 (Longest Increasing Subsequence, LIS)
// ---------------------------------------------------------------------------
// 상태 : dp[i] = nums[i] 로 "끝나는" 증가 부분수열의 최대 길이.
// 전이 : dp[i] = 1 + max{ dp[j] : j < i, nums[j] < nums[i] }  (없으면 1)
// 기저 : 모든 dp[i] = 1 (자기 자신만).
// 답  : max(dp[0..n-1]).  이 구현은 O(n^2).
// (참고) 참을성(patience sorting) + 이진탐색으로 O(n log n) 가능: tails[] 유지.
// 예: [10,9,2,5,3,7,101,18] -> 4 ([2,3,7,101] 또는 [2,5,7,18])
// ===========================================================================
int longest_increasing_subsequence(const int *nums, int n) {
    if (n <= 0) return 0;
    int *dp = malloc((size_t)n * sizeof *dp);
    if (!dp) return 0;
    int best = 1;
    for (int i = 0; i < n; ++i) {
        dp[i] = 1;                                // 기저
        for (int j = 0; j < i; ++j) {
            if (nums[j] < nums[i] && dp[j] + 1 > dp[i])
                dp[i] = dp[j] + 1;                // 점화식
        }
        if (dp[i] > best) best = dp[i];
    }
    free(dp);
    return best;
}

// ===========================================================================
// Q5. 0/1 배낭 (0/1 Knapsack)
// ---------------------------------------------------------------------------
// 상태 : dp[c] = 용량 c 이하에서 얻을 수 있는 최대 가치 (각 아이템 0 또는 1회).
// 전이 : 아이템 i 를 훑으며  dp[c] = max(dp[c], dp[c - w[i]] + v[i])
// 순서 : c 를 cap..w[i] 로 "내림차순" (같은 아이템 중복 사용 방지 = 0/1 보장).
//        (오름차순으로 돌면 unbounded knapsack = 무제한 사용이 됨)
// 기저 : dp[*] = 0 (아무 것도 안 담음).
// 예: w=[1,3,4,5], v=[1,4,5,7], cap=7 -> 9 (w3+w4 = 4+5? -> v5+v4=7+... ) => 9
// ===========================================================================
int knapsack_01(const int *w, const int *v, int n, int cap) {
    if (cap <= 0 || n <= 0) return 0;
    int *dp = calloc((size_t)(cap + 1), sizeof *dp);   // dp[*] = 0 기저
    if (!dp) return 0;
    for (int i = 0; i < n; ++i) {
        for (int c = cap; c >= w[i]; --c) {            // 내림차순 = 0/1
            int cand = dp[c - w[i]] + v[i];
            if (cand > dp[c]) dp[c] = cand;            // 점화식
        }
    }
    int ans = dp[cap];
    free(dp);
    return ans;
}

// ===========================================================================
// Q6. 편집 거리 (Edit Distance / Levenshtein)
// ---------------------------------------------------------------------------
// 상태 : dp[i][j] = a[0..i) 를 b[0..j) 로 바꾸는 최소 연산 수(삽입/삭제/치환).
// 전이 : a[i-1]==b[j-1] 이면  dp[i][j] = dp[i-1][j-1]
//        아니면 dp[i][j] = 1 + min(dp[i-1][j],   // 삭제 (a 문자 버림)
//                                   dp[i][j-1],   // 삽입 (b 문자 추가)
//                                   dp[i-1][j-1]) // 치환
// 기저 : dp[i][0] = i (i개 삭제), dp[0][j] = j (j개 삽입)
// 예: "kitten"->"sitting" = 3,  ""->"abc" = 3
// ===========================================================================
int edit_distance(const char *a, const char *b) {
    int la = (int)strlen(a), lb = (int)strlen(b);
    // (la+1) x (lb+1) 테이블을 1차원으로 flatten.  idx(i,j) = i*(lb+1)+j
    int *dp = malloc((size_t)(la + 1) * (size_t)(lb + 1) * sizeof *dp);
    if (!dp) return -1;
    int W = lb + 1;
    for (int i = 0; i <= la; ++i) dp[i * W + 0] = i;    // 기저: 왼쪽 열
    for (int j = 0; j <= lb; ++j) dp[0 * W + j] = j;    // 기저: 윗 행
    for (int i = 1; i <= la; ++i) {
        for (int j = 1; j <= lb; ++j) {
            if (a[i - 1] == b[j - 1]) {
                dp[i * W + j] = dp[(i - 1) * W + (j - 1)];   // 문자 일치: 비용 0
            } else {
                int del = dp[(i - 1) * W + j];
                int ins = dp[i * W + (j - 1)];
                int sub = dp[(i - 1) * W + (j - 1)];
                dp[i * W + j] = 1 + imin(del, imin(ins, sub));
            }
        }
    }
    int ans = dp[la * W + lb];
    free(dp);
    return ans;
}

// ===========================================================================
// Q7. 최장 공통 부분수열 (Longest Common Subsequence, LCS)
// ---------------------------------------------------------------------------
// 상태 : dp[i][j] = a[0..i) 와 b[0..j) 의 LCS 길이.
// 전이 : a[i-1]==b[j-1] 이면  dp[i][j] = dp[i-1][j-1] + 1
//        아니면 dp[i][j] = max(dp[i-1][j], dp[i][j-1])
// 기저 : dp[0][*] = dp[*][0] = 0 (한쪽이 빈 문자열)
// 예: "abcde"&"ace" -> 3 ("ace"),  "abc"&"def" -> 0
// ===========================================================================
int longest_common_subsequence(const char *a, const char *b) {
    int la = (int)strlen(a), lb = (int)strlen(b);
    int W = lb + 1;
    int *dp = calloc((size_t)(la + 1) * (size_t)W, sizeof *dp);   // 기저 0
    if (!dp) return 0;
    for (int i = 1; i <= la; ++i) {
        for (int j = 1; j <= lb; ++j) {
            if (a[i - 1] == b[j - 1])
                dp[i * W + j] = dp[(i - 1) * W + (j - 1)] + 1;
            else
                dp[i * W + j] = imax(dp[(i - 1) * W + j], dp[i * W + (j - 1)]);
        }
    }
    int ans = dp[la * W + lb];
    free(dp);
    return ans;
}

// ===========================================================================
// Q8. 격자 경로 수 (Unique Paths)
// ---------------------------------------------------------------------------
// m x n 격자에서 좌상단(0,0)->우하단(m-1,n-1), 오른쪽/아래로만 이동.
// 상태 : dp[i][j] = (0,0) 에서 (i,j) 까지 오는 경로 수.
// 전이 : dp[i][j] = dp[i-1][j] + dp[i][j-1]   (위 or 왼쪽에서 옴)
// 기저 : 첫 행/첫 열은 모두 1 (한 방향으로만 옴).
// => 1D 롤링: 한 행(row[n])만 유지하며 갱신. O(n) 공간.
// 예: 3x7 -> 28,  1xN -> 1
// ===========================================================================
int unique_paths(int m, int n) {
    if (m <= 0 || n <= 0) return 0;
    int *row = malloc((size_t)n * sizeof *row);
    if (!row) return 0;
    for (int j = 0; j < n; ++j) row[j] = 1;        // 첫 행 = 1 (기저)
    for (int i = 1; i < m; ++i) {
        // row[0] 은 첫 열이라 항상 1로 유지됨.
        for (int j = 1; j < n; ++j) {
            row[j] = row[j] + row[j - 1];          // 위(옛 row[j]) + 왼쪽(row[j-1])
        }
    }
    int ans = row[n - 1];
    free(row);
    return ans;
}

// ===========================================================================
// Q9. 최대 곱 부분배열 (Maximum Product Subarray)
// ---------------------------------------------------------------------------
// 연속 부분배열의 곱 중 최댓값.  음수 * 음수 = 양수 이므로 최솟값도 추적해야 함!
// 상태 : maxp = 현재 원소로 "끝나는" 부분배열의 최대 곱,
//        minp = 같은 조건의 최소 곱 (음수 대비).
// 전이 : cand = {nums[i], maxp*nums[i], minp*nums[i]}
//        maxp = max(cand),  minp = min(cand)
// 기저 : maxp = minp = nums[0], 답 = nums[0].
// 예: [2,3,-2,4] -> 6 ([2,3]),  [-2,0,-1] -> 0,  [-2,3,-4] -> 24 (전체)
// ===========================================================================
int max_product_subarray(const int *nums, int n) {
    if (n <= 0) return 0;
    int maxp = nums[0], minp = nums[0], ans = nums[0];
    for (int i = 1; i < n; ++i) {
        int x = nums[i];
        // x 가 음수면 max 와 min 이 뒤집히므로 세 후보를 함께 평가.
        int a = maxp * x, b = minp * x;
        int nmax = imax3(x, a, b);
        int nmin = imin(x, imin(a, b));
        maxp = nmax;
        minp = nmin;
        if (maxp > ans) ans = maxp;
    }
    return ans;
}

// ===========================================================================
// main : 모든 케이스 PASS/FAIL
// ===========================================================================
int main(void) {
    // -------- Q1 climb_stairs --------
    printf("== Q1. Climbing Stairs ==\n");
    T("climb(0) == 1", climb_stairs(0) == 1);
    T("climb(1) == 1", climb_stairs(1) == 1);
    T("climb(2) == 2", climb_stairs(2) == 2);
    T("climb(3) == 3", climb_stairs(3) == 3);
    T("climb(5) == 8", climb_stairs(5) == 8);
    T("climb(10) == 89", climb_stairs(10) == 89);

    // -------- Q2 house_robber --------
    printf("== Q2. House Robber ==\n");
    T("rob [] == 0", house_robber((int[]){0}, 0) == 0);
    T("rob [5] == 5", house_robber((int[]){5}, 1) == 5);
    T("rob [2,1] == 2", house_robber((int[]){2,1}, 2) == 2);
    T("rob [1,2,3,1] == 4", house_robber((int[]){1,2,3,1}, 4) == 4);
    T("rob [2,7,9,3,1] == 12", house_robber((int[]){2,7,9,3,1}, 5) == 12);
    T("rob [2,1,1,2] == 4", house_robber((int[]){2,1,1,2}, 4) == 4);

    // -------- Q3 coin_change_min --------
    printf("== Q3. Coin Change (min coins) ==\n");
    T("coins[1,2,5] amt 11 == 3", coin_change_min((int[]){1,2,5}, 3, 11) == 3);
    T("coins[2] amt 3 == -1", coin_change_min((int[]){2}, 1, 3) == -1);
    T("coins[1] amt 0 == 0", coin_change_min((int[]){1}, 1, 0) == 0);
    T("coins[186,419,83,408] amt 6249 == 20",
      coin_change_min((int[]){186,419,83,408}, 4, 6249) == 20);
    T("coins[2,5,10,1] amt 27 == 4", coin_change_min((int[]){2,5,10,1}, 4, 27) == 4);

    // -------- Q4 LIS --------
    printf("== Q4. Longest Increasing Subsequence ==\n");
    T("LIS [] == 0", longest_increasing_subsequence((int[]){0}, 0) == 0);
    T("LIS [7] == 1", longest_increasing_subsequence((int[]){7}, 1) == 1);
    T("LIS [10,9,2,5,3,7,101,18] == 4",
      longest_increasing_subsequence((int[]){10,9,2,5,3,7,101,18}, 8) == 4);
    T("LIS [0,1,0,3,2,3] == 4",
      longest_increasing_subsequence((int[]){0,1,0,3,2,3}, 6) == 4);
    T("LIS [7,7,7,7] == 1", longest_increasing_subsequence((int[]){7,7,7,7}, 4) == 1);

    // -------- Q5 knapsack_01 --------
    printf("== Q5. 0/1 Knapsack ==\n");
    T("knap w[1,3,4,5] v[1,4,5,7] cap7 == 9",
      knapsack_01((int[]){1,3,4,5}, (int[]){1,4,5,7}, 4, 7) == 9);
    T("knap cap 0 == 0", knapsack_01((int[]){1,2}, (int[]){10,20}, 2, 0) == 0);
    T("knap single fits == 15",
      knapsack_01((int[]){3}, (int[]){15}, 1, 5) == 15);
    T("knap single too heavy == 0",
      knapsack_01((int[]){6}, (int[]){15}, 1, 5) == 0);
    T("knap w[2,3,4,5] v[3,4,5,6] cap5 == 7",
      knapsack_01((int[]){2,3,4,5}, (int[]){3,4,5,6}, 4, 5) == 7);

    // -------- Q6 edit_distance --------
    printf("== Q6. Edit Distance (Levenshtein) ==\n");
    T("edit kitten->sitting == 3", edit_distance("kitten", "sitting") == 3);
    T("edit horse->ros == 3", edit_distance("horse", "ros") == 3);
    T("edit ''->'' == 0", edit_distance("", "") == 0);
    T("edit ''->abc == 3", edit_distance("", "abc") == 3);
    T("edit abc->'' == 3", edit_distance("abc", "") == 3);
    T("edit same == 0", edit_distance("firmware", "firmware") == 0);

    // -------- Q7 LCS --------
    printf("== Q7. Longest Common Subsequence ==\n");
    T("LCS abcde,ace == 3", longest_common_subsequence("abcde", "ace") == 3);
    T("LCS abc,abc == 3", longest_common_subsequence("abc", "abc") == 3);
    T("LCS abc,def == 0", longest_common_subsequence("abc", "def") == 0);
    T("LCS '' , abc == 0", longest_common_subsequence("", "abc") == 0);
    T("LCS AGGTAB,GXTXAYB == 4",
      longest_common_subsequence("AGGTAB", "GXTXAYB") == 4);

    // -------- Q8 unique_paths --------
    printf("== Q8. Unique Paths ==\n");
    T("paths 3x7 == 28", unique_paths(3, 7) == 28);
    T("paths 3x2 == 3", unique_paths(3, 2) == 3);
    T("paths 1x10 == 1", unique_paths(1, 10) == 1);
    T("paths 10x1 == 1", unique_paths(10, 1) == 1);
    T("paths 3x3 == 6", unique_paths(3, 3) == 6);

    // -------- Q9 max_product_subarray --------
    printf("== Q9. Maximum Product Subarray ==\n");
    T("maxprod [2,3,-2,4] == 6", max_product_subarray((int[]){2,3,-2,4}, 4) == 6);
    T("maxprod [-2,0,-1] == 0", max_product_subarray((int[]){-2,0,-1}, 3) == 0);
    T("maxprod [-2,3,-4] == 24", max_product_subarray((int[]){-2,3,-4}, 3) == 24);
    T("maxprod [2,-5,-2,-4,3] == 24",
      max_product_subarray((int[]){2,-5,-2,-4,3}, 5) == 24);
    T("maxprod [-3] == -3", max_product_subarray((int[]){-3}, 1) == -3);
    T("maxprod [0,2] == 2", max_product_subarray((int[]){0,2}, 2) == 2);

    // -------- 결과 --------
    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
