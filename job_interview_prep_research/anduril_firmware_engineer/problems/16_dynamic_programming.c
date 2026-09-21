// 16_dynamic_programming.c  —  PRACTICE STUB (여기 빈칸을 채우세요)
// 동적 계획법 (Dynamic Programming, bottom-up 1D/2D 테이블)  —  Q1~Q9
// ---------------------------------------------------------------------------
// 빌드: cc -std=c11 -Wall -Wextra 16_dynamic_programming.c -o /tmp/andb_16 && /tmp/andb_16
// 각 함수의 // TODO 를 구현하고 다시 빌드/실행해 FAIL -> PASS 로 바꾸세요.
// 지금 상태로도 컴파일/실행은 되며(placeholder 반환), 대부분 [FAIL] 로 나옵니다.
//
// DP 사고 4단계 (매 문제 이 순서로!):
//   1) 상태(state)      : dp[i] / dp[i][j] 가 "무엇의 답"인지 한 문장으로.
//   2) 전이(transition) : 더 작은 dp 로 표현하는 점화식(recurrence).
//   3) 기저(base case)  : 가장 작은 입력(n=0,1, 빈 문자열)을 직접 채운다.
//   4) 순서(order)      : 의존값이 먼저 계산되도록 루프 방향 결정.
//
// 각 함수 위에 점화식이 이미 적혀 있으니 그대로 코드로 옮기면 됩니다.
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

// Q1. 계단 오르기.  dp[i] = dp[i-1] + dp[i-2],  dp[0]=dp[1]=1 (피보나치).
//   예: n=2 -> 2, n=5 -> 8
int climb_stairs(int n) {
    (void)n;
    // TODO: 롤링 변수 2개(prev1, prev2)로 dp[n] 계산
    return 0;   // placeholder
}

// Q2. 도둑질 (인접 집 금지).  dp[i] = max(dp[i-1], dp[i-2] + nums[i]).
//   예: [2,7,9,3,1] -> 12
int house_robber(const int *nums, int n) {
    (void)nums; (void)n;
    // TODO: prev1/prev2 롤링. n<=0 이면 0.
    return 0;   // placeholder
}

// Q3. 동전 최소 개수.  dp[a] = min over coin { dp[a-coin] + 1 }, dp[0]=0.
//   만들 수 없으면 -1.  예: [1,2,5], 11 -> 3
int coin_change_min(const int *coins, int nc, int amount) {
    (void)coins; (void)nc; (void)amount;
    // TODO: 크기 amount+1 dp 테이블 malloc, INF 초기화, 마지막에 free.
    return -1;  // placeholder
}

// Q4. 최장 증가 부분수열 (O(n^2)).
//   dp[i] = 1 + max{ dp[j] : j<i, nums[j]<nums[i] }, 없으면 1.  답 = max(dp).
//   예: [10,9,2,5,3,7,101,18] -> 4
int longest_increasing_subsequence(const int *nums, int n) {
    (void)nums; (void)n;
    // TODO: dp[i] 채우고 최댓값 추적. (O(n log n) 은 tails[]+이진탐색)
    return 0;   // placeholder
}

// Q5. 0/1 배낭.  아이템마다 c 를 cap..w[i] "내림차순":
//   dp[c] = max(dp[c], dp[c-w[i]] + v[i]), dp[*]=0.
//   예: w=[1,3,4,5] v=[1,4,5,7] cap=7 -> 9
int knapsack_01(const int *w, const int *v, int n, int cap) {
    (void)w; (void)v; (void)n; (void)cap;
    // TODO: dp = calloc(cap+1). 내림차순 루프가 0/1(중복 방지)의 핵심.
    return 0;   // placeholder
}

// Q6. 편집 거리(Levenshtein).  같으면 dp[i][j]=dp[i-1][j-1],
//   아니면 1 + min(dp[i-1][j], dp[i][j-1], dp[i-1][j-1]).
//   기저: dp[i][0]=i, dp[0][j]=j.  예: kitten->sitting = 3
int edit_distance(const char *a, const char *b) {
    (void)a; (void)b;
    // TODO: (la+1)x(lb+1) 테이블(1D flatten 권장), 기저 채우고 점화식, free.
    return -1;  // placeholder
}

// Q7. 최장 공통 부분수열(LCS).  같으면 dp[i][j]=dp[i-1][j-1]+1,
//   아니면 max(dp[i-1][j], dp[i][j-1]).  기저 0.  예: abcde,ace -> 3
int longest_common_subsequence(const char *a, const char *b) {
    (void)a; (void)b;
    // TODO: (la+1)x(lb+1) 테이블 calloc, 점화식, free.
    return 0;   // placeholder
}

// Q8. 격자 경로 수.  dp[i][j] = dp[i-1][j] + dp[i][j-1], 첫 행/열 = 1.
//   예: 3x7 -> 28
int unique_paths(int m, int n) {
    (void)m; (void)n;
    // TODO: 1D 롤링 row[n] 로 O(n) 공간. row 전부 1로 시작.
    return 0;   // placeholder
}

// Q9. 최대 곱 부분배열.  음수 대비 최소 곱도 함께 추적:
//   maxp=max(x, maxp*x, minp*x), minp=min(x, maxp*x, minp*x).
//   예: [2,3,-2,4] -> 6, [-2,3,-4] -> 24
int max_product_subarray(const int *nums, int n) {
    (void)nums; (void)n;
    // TODO: maxp/minp/ans 를 nums[0] 로 초기화 후 갱신.
    return 0;   // placeholder
}

// ===========================================================================
// main : 모든 케이스 PASS/FAIL
// ===========================================================================
int main(void) {
    printf("== Q1. Climbing Stairs ==\n");
    T("climb(0) == 1", climb_stairs(0) == 1);
    T("climb(1) == 1", climb_stairs(1) == 1);
    T("climb(2) == 2", climb_stairs(2) == 2);
    T("climb(3) == 3", climb_stairs(3) == 3);
    T("climb(5) == 8", climb_stairs(5) == 8);
    T("climb(10) == 89", climb_stairs(10) == 89);

    printf("== Q2. House Robber ==\n");
    T("rob [] == 0", house_robber((int[]){0}, 0) == 0);
    T("rob [5] == 5", house_robber((int[]){5}, 1) == 5);
    T("rob [2,1] == 2", house_robber((int[]){2,1}, 2) == 2);
    T("rob [1,2,3,1] == 4", house_robber((int[]){1,2,3,1}, 4) == 4);
    T("rob [2,7,9,3,1] == 12", house_robber((int[]){2,7,9,3,1}, 5) == 12);
    T("rob [2,1,1,2] == 4", house_robber((int[]){2,1,1,2}, 4) == 4);

    printf("== Q3. Coin Change (min coins) ==\n");
    T("coins[1,2,5] amt 11 == 3", coin_change_min((int[]){1,2,5}, 3, 11) == 3);
    T("coins[2] amt 3 == -1", coin_change_min((int[]){2}, 1, 3) == -1);
    T("coins[1] amt 0 == 0", coin_change_min((int[]){1}, 1, 0) == 0);
    T("coins[186,419,83,408] amt 6249 == 20",
      coin_change_min((int[]){186,419,83,408}, 4, 6249) == 20);
    T("coins[2,5,10,1] amt 27 == 4", coin_change_min((int[]){2,5,10,1}, 4, 27) == 4);

    printf("== Q4. Longest Increasing Subsequence ==\n");
    T("LIS [] == 0", longest_increasing_subsequence((int[]){0}, 0) == 0);
    T("LIS [7] == 1", longest_increasing_subsequence((int[]){7}, 1) == 1);
    T("LIS [10,9,2,5,3,7,101,18] == 4",
      longest_increasing_subsequence((int[]){10,9,2,5,3,7,101,18}, 8) == 4);
    T("LIS [0,1,0,3,2,3] == 4",
      longest_increasing_subsequence((int[]){0,1,0,3,2,3}, 6) == 4);
    T("LIS [7,7,7,7] == 1", longest_increasing_subsequence((int[]){7,7,7,7}, 4) == 1);

    printf("== Q5. 0/1 Knapsack ==\n");
    T("knap w[1,3,4,5] v[1,4,5,7] cap7 == 9",
      knapsack_01((int[]){1,3,4,5}, (int[]){1,4,5,7}, 4, 7) == 9);
    T("knap cap 0 == 0", knapsack_01((int[]){1,2}, (int[]){10,20}, 2, 0) == 0);
    T("knap single fits == 15", knapsack_01((int[]){3}, (int[]){15}, 1, 5) == 15);
    T("knap single too heavy == 0", knapsack_01((int[]){6}, (int[]){15}, 1, 5) == 0);
    T("knap w[2,3,4,5] v[3,4,5,6] cap5 == 7",
      knapsack_01((int[]){2,3,4,5}, (int[]){3,4,5,6}, 4, 5) == 7);

    printf("== Q6. Edit Distance (Levenshtein) ==\n");
    T("edit kitten->sitting == 3", edit_distance("kitten", "sitting") == 3);
    T("edit horse->ros == 3", edit_distance("horse", "ros") == 3);
    T("edit ''->'' == 0", edit_distance("", "") == 0);
    T("edit ''->abc == 3", edit_distance("", "abc") == 3);
    T("edit abc->'' == 3", edit_distance("abc", "") == 3);
    T("edit same == 0", edit_distance("firmware", "firmware") == 0);

    printf("== Q7. Longest Common Subsequence ==\n");
    T("LCS abcde,ace == 3", longest_common_subsequence("abcde", "ace") == 3);
    T("LCS abc,abc == 3", longest_common_subsequence("abc", "abc") == 3);
    T("LCS abc,def == 0", longest_common_subsequence("abc", "def") == 0);
    T("LCS '' , abc == 0", longest_common_subsequence("", "abc") == 0);
    T("LCS AGGTAB,GXTXAYB == 4",
      longest_common_subsequence("AGGTAB", "GXTXAYB") == 4);

    printf("== Q8. Unique Paths ==\n");
    T("paths 3x7 == 28", unique_paths(3, 7) == 28);
    T("paths 3x2 == 3", unique_paths(3, 2) == 3);
    T("paths 1x10 == 1", unique_paths(1, 10) == 1);
    T("paths 10x1 == 1", unique_paths(10, 1) == 1);
    T("paths 3x3 == 6", unique_paths(3, 3) == 6);

    printf("== Q9. Maximum Product Subarray ==\n");
    T("maxprod [2,3,-2,4] == 6", max_product_subarray((int[]){2,3,-2,4}, 4) == 6);
    T("maxprod [-2,0,-1] == 0", max_product_subarray((int[]){-2,0,-1}, 3) == 0);
    T("maxprod [-2,3,-4] == 24", max_product_subarray((int[]){-2,3,-4}, 3) == 24);
    T("maxprod [2,-5,-2,-4,3] == 24",
      max_product_subarray((int[]){2,-5,-2,-4,3}, 5) == 24);
    T("maxprod [-3] == -3", max_product_subarray((int[]){-3}, 1) == -3);
    T("maxprod [0,2] == 2", max_product_subarray((int[]){0,2}, 2) == 2);

    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
