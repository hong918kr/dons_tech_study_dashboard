# Module 9 — Dynamic Programming (동적 계획법)

> 출처: AlgoMonster Coding Patterns, Module 9 (레슨 50개) · 한국어 개념 정리 노트
> 목표: "이거 DP네"를 **키워드로 즉시 인식**하고, **상태 정의 → 점화식 → 기저 → 채우는 순서 → 공간 최적화** 5단계를 패턴별(상수 전이 / 그리드 / 두 시퀀스 / 비상수 전이 / 배낭 / 구간 / 위상 / 트리 / 비트마스크)로 손에 익히기.


## 한눈에 보기 (5분 복습용)

| 패턴/문제 | 키워드 | 핵심 아이디어 | 복잡도 |
|---|---|---|---|
| DP Intro | 최대/최소, 경우의 수, 가능 여부 | 겹치는 부분문제 + optimal substructure. 점화식 찾기가 핵심, greedy 반례 먼저 확인 | 상태 수 × 전이 비용 |
| Climbing Stairs | 1 또는 2칸, distinct ways | `ways(n)=ways(n-1)+ways(n-2)`, 변수 2개로 rolling | O(n) / O(1) |
| N-th Tribonacci | 이전 3항의 합 | 화살표 3개인 상수 전이, 변수 3개 | O(n) / O(1) |
| Constant Transition Intro | 1D 입력, adjacent, 고정 step | `dp[i]`가 고정 개수의 이전 상태에 의존 | O(n) / O(1) |
| House Robber | 인접한 것 동시 선택 불가, 최대 합 | `max(dp[i-1], dp[i-2]+x)`: 인접 금지를 화살표 길이로 표현 | O(n) / O(1) |
| Min Cost Climbing Stairs | 1/2칸, 최소 비용, 꼭대기 | `min(dp[i-1]+c[i-1], dp[i-2]+c[i-2])`, 답은 n(한 칸 너머) | O(n) / O(1) |
| Minimum Cost For Tickets | 1/7/30일 패스 | 날짜로 인덱싱, 여행 안 하는 날은 복사, 고정 오프셋 min | O(365) |
| Grid DP Intro | 격자, right/down만 이동 | `dp[r][c]` ← 위/왼쪽, 순서는 행 우선. 사방 이동이면 BFS | O(mn) / O(n) |
| Unique Paths | 경로 수, right/down | 위 + 왼쪽. 재계산 횟수 자체가 답 | O(mn) / O(n) |
| Unique Paths II | 장애물, 경로 수 | 장애물 = 0이 자동 전파. 1행 rolling은 왼→오 방향 | O(mn) / O(n) |
| Minimum Path Sum | 경로 합 최소 | `min(위, 왼)+grid`. greedy 실패 | O(mn) / O(n) |
| Maximal Square | 1로만 된 최대 정사각형 | `min(위, 왼, 대각)+1`, 답 = max²(대각선이 L자 모양 배제) | O(mn) |
| Triangle | 삼각형, 아래 인접 두 칸 | 바닥→위, `dp[c]=t+min(dp[c], dp[c+1])` | O(n²) / O(n) |
| Dungeon Game | 최소 초기 체력, 0 이하 금지 | 역방향 "필요 체력" `max(min(아래, 오른쪽)-v, 1)` | O(mn) |
| Dual-Sequence Intro | 두 시퀀스, n·m ≤ ~10⁶ | prefix 쌍 `dp[i][j]`, 대각/위/왼 세 이웃, 연산자만 다름 | O(nm) |
| LCS | 공통 부분수열 | 매치면 대각+1, 아니면 max(위, 왼). 역추적으로 복원 | O(nm) |
| Edit Distance | insert/delete/replace 최소 연산 | 매치면 대각, 아니면 1+min(3). 기저 i, j | O(nm) / O(min) |
| Delete String | 양쪽 삭제로 같게, 글자별 비용 | 가중 LCS. 0행/0열 = 누적 삭제 비용 | O(nm) |
| Distinct Subsequences | s의 부분수열 중 t와 같은 개수 | 매치면 위+대각(합). 1행 압축 시 역방향 루프 | O(mn) / O(n) |
| Shortest Common Supersequence | 둘 다 포함하는 최단 문자열 | LCS 표 + 역추적, 길이 = m+n−LCS | O(mn) |
| Non-constant Transition Intro | subsequence, 가변 선행자 | `dp[i]=best(dp[j]+…) for j<i` | O(n²) / O(nk) |
| LIS | increasing subsequence | i에서 끝나는 길이, max(dp). tails + 이진 탐색 | O(n²) → O(n log n) |
| Partition Array for Max Sum | 길이 ≤ k 조각으로 분할 | 마지막 조각 길이 L 시도 + running max | O(nk) |
| Largest Divisible Subset | 모든 쌍이 나누어떨어짐 | 정렬 + LIS (조건 `%==0`), 추이성 활용 | O(n²) |
| Divisor Game | 두 플레이어, 최선, 움직일 수 없으면 패배 | L로 보내는 수가 있으면 W. 짝수 = W | O(n²) → O(1) |
| Knapsack DP Intro | 아이템 선택 + 누적 제약(합/용량) | dp[i][j]=combine(skip, include); combine=or/+/max | O(n·C) |
| Weight-Only Knapsack (Subset Sum) | 가능한 모든 합 | bool dp[합], 0/1이므로 역순 갱신 (bitset 가능) | O(n·S) |
| Partition Equal Subset Sum | 두 그룹 합 같게 | total 홀수→False, total/2 subset-sum feasibility | O(n·S/2) |
| Target Sum | 각 원소 +/− 부호 | P=(total+target)/2 부분집합 개수 counting | O(n·P) |
| Unbounded Knapsack Intro | 무제한 재사용 | include 분기가 같은 행 dp[i][j-w]; 1D 정순 | O(n·C) |
| Coin Change II | 조합의 수, 동전 무제한 | coin 바깥 루프 + amount 정순 dp[j]+=dp[j-c] | O(k·A) |
| Coin Change (min) | 최소 동전 수, 그리디 반례 | dp[j]=min(dp[j], dp[j-c]+1), INF→−1 | O(k·A) |
| Perfect Squares | 최소 제곱수 개수 | 제곱수를 동전으로 둔 coin change min | O(n√n) |
| 0/1 Knapsack Intro | 각 아이템 1번, 가치 최대 | dp[j]=max(dp[j], dp[j-w]+v), 역순 | O(n·W) |
| 0/1 Knapsack Practice | 무게 제한·가치 최대 | 1D 역순 템플릿; "정확히 W"면 −INF 초기화 | O(n·W) |
| Bounded Knapsack Intro | 아이템별 재고 q | 직접 x개 시도 or 이진 분해 → 0/1 | O(n·C·log q) |
| Bounded Knapsack | 수량 제한 | 1,2,4,…,나머지 덩어리로 0/1 갱신 | O(C·Σlog q) |
| Interval DP Intro | 부분 구간 여러 개, n≤수백 | dp[l][r], shrink형 O(n²)/split형 O(n³), 길이순 fill | O(n²)~O(n³) |
| Palindromic Substrings | 팰린드롬 부분 문자열 개수 | 중심 확장(2n−1 중심) 또는 dp[l][r] | O(n²) / O(1) 공간 |
| Coin Game | 양 끝에서 가져가기, 최적 플레이 | 점수 차 d[l][r]=max(c[l]−d[l+1][r], c[r]−d[l][r−1]) | O(n²) |
| Longest Palindromic Subsequence | 팰린드롬 부분 수열 | 같으면 inner+2, 다르면 max(한쪽 버림) | O(n²) |
| Topological Sort DP Intro | DAG / 엄격 증가 제약 | 위상 순서(=memo DFS)로 dp[v]=f(선행자) | O(V+E) |
| Longest Increasing Path in Matrix | 엄격 증가 경로 in grid | 암묵적 DAG, memo DFS 또는 값 정렬 DP | O(RC) / O(RC log RC) |
| Longest String Chain | 한 글자 삽입 체인 | 길이순 정렬, 한 글자 삭제한 선행자 해시 조회 | O(n·L²) |
| Tree DP Intro | 서브트리 답이 자식에 의존 | post-order DFS, aggregate/selection, 튜플 반환 | O(n) |
| House Robber III | 트리에서 인접 노드 동시 선택 불가 | 노드별 (rob, skip) 반환 | O(n) |
| Bitmask Intro | 집합 상태, n≤20 | 비트 검사/세팅/클리어/토글, 부분집합 열거 | O(2ⁿ·n) 열거 |
| Bitmask DP | 전부 방문/배정/분할 | dp[mask][extra]; 할당은 popcount로 extra 생략; submask O(3ⁿ) | O(2ⁿ·n) |
| Min Cost to Visit Every Node | 모든 노드 정확히 한 번 (open TSP) | dp(mask,pos) 최소 추가 비용, 시작 mask=1 | O(2ⁿ·n²) |
| DP Practice List | 복습 목록 | 상태 모양별 9그룹 분류 (prefix/grid/dual/interval/knapsack/DAG/tree…) | — |

**DP 5단계:** 상태 정의 → 점화식 → 기저 → 채우는 순서 → 공간 최적화. 막히면 top-down memo로 먼저 맞추고 bottom-up으로 옮긴다.
**상태 모양으로 분류:** 1D 상수 전이 / 그리드 / 두 시퀀스 `dp[i][j]` / 비상수 전이 `j<i` / 배낭 `dp[용량]` / 구간 `dp[l][r]` / DAG·트리(post-order) / 비트마스크 `dp[mask]`.
**1D 배낭 루프 방향:** 0/1이면 용량 **역순**, unbounded면 **정순** — 방향이 곧 "재사용 허용 여부".

---

## 1. Dynamic Programming Intro

> 섹션: Introduction

### 핵심 개념
- DP = 큰 문제를 **겹치는 작은 부분 문제**로 쪼개고, 그 답을 **저장해 재사용**해서 원래 답을 조립하는 최적화 기법.
- 이름의 유래: Bellman(1950s)의 "programming"은 코딩이 아니라 **계획/의사결정**. 사실상 "다단계 계획" = 메모이제이션.
- **DP == DFS + memoization + pruning.** Top-down(재귀+메모)과 Bottom-up(표 채우기)은 같은 점화식의 두 구현일 뿐.

### 면접에서 DP 푸는 2단계
1. **DP 문제임을 알아채기** — 질문이 ① 최대/최소(가장 긴·짧은·싼) ② 경우의 수("how many ways") ③ 가능 여부(boolean) 중 하나.
   - 경우의 수·가능 여부는 키워드만으로 거의 확정. **최대/최소는 greedy 반례를 먼저 찾아볼 것.**
   - 예: 동전 {1,3,4}로 6 만들기 최소 개수 → greedy 4+1+1=3개, 정답 3+3=2개. greedy가 깨지면 DP.
2. **점화식(recurrence) 찾기** — 부분 문제 정의 + 기저 + 작은 답들로 큰 답 표현. 예: `fib(i) = fib(i-1) + fib(i-2)`, `fib(0)=0, fib(1)=1`. 나머지(메모, 표, 방향)는 기계적 작업.

### DP가 성립하는 두 성질
| 성질 | 의미 | 예 / 반례 |
|---|---|---|
| **Optimal substructure** | 최적해 = 부분 문제 최적해의 조합 → 점화식이 유효 | SF→SD 최단경로 = SF→LA + LA→SD ✅ / 경유 항공권 최저가는 구간 최저가의 합이 아님 ❌ |
| **Overlapping sub-problems** | 같은 부분 문제가 반복 등장 → 저장할 가치가 있음 | Fibonacci 재귀 트리 |
- Divide & Conquer(merge sort)도 쪼개지만 **부분 문제가 안 겹침** → 저장 불필요. 겹치면 DP.

### 템플릿 코드
```python
# Top-down: DFS + memo
def fib(n, memo={}):
    if n in memo: return memo[n]
    if n < 2: return n
    memo[n] = fib(n - 1, memo) + fib(n - 2, memo)
    return memo[n]

# Bottom-up: 표를 작은 것부터 채움 (순서가 중요)
def fib_bu(n):
    if n < 2: return n
    dp = [0] * (n + 1); dp[1] = 1
    for i in range(2, n + 1):
        dp[i] = dp[i - 1] + dp[i - 2]
    return dp[n]
```

### Top-down vs Bottom-up
| | Top-down | Bottom-up |
|---|---|---|
| 장점 | 계산 순서 신경 안 씀, 분할형("몇 가지 방법으로 나누나") 문제에 자연스러움 | 복잡도 분석 쉬움(표 크기 × 전이), 재귀 스택 오버플로 없음, 공간 최적화 쉬움 |
| 추천 | 기본 출발점 | 채우는 순서가 명확할 때(그리드 DP 등) |

### DP 패턴 지도 (이 모듈의 목차)
| 패턴 | 상태 | 대표 문제 |
|---|---|---|
| Constant Transition | `dp[i]`가 고정 개수 이전 상태에 의존 → O(n) | Climbing Stairs, House Robber |
| Grid | `dp[i][j]` = 셀 (i,j)까지의 최적/경우의 수 | Unique Paths, Min Path Sum, Maximal Square, Dungeon(역방향) |
| Dual-Sequence | `dp[i][j]` = 두 시퀀스의 prefix 쌍 → O(nm) | LCS, Edit Distance |
| Interval | `dp[i][j]` = 부분배열 [i..j] → O(n²) 상태 | Longest Palindromic Subseq, Coin Game, Burst Balloons |
| Knapsack | (아이템 index, 현재 무게) 2변수 상태 | Subset Sum, Partition, Target Sum, 0-1 Knapsack |
| Topological Sort DP | DAG의 위상 순서로 계산 | Longest Increasing Path in Matrix |
| Tree DP | 노드별 상태, post-order로 자식→부모 | House Robber III |
| Non-constant Transition | `dp[i] = max(dp[j]) for j<i` → 전이 O(n) | LIS, 주식 K회 거래 |
| Bitmask | 부분집합을 비트로 인코딩, n! → 2ⁿ | TSP류 |

- 🐛 흔한 실수: greedy로 되는지 안 따져보고 DP부터 짜기 / 반대로 greedy 반례 없이 greedy로 끝내기.
- 🎤 "DP 문제는 어떻게 접근하나?" → 키워드로 DP 인식 → greedy 반례 확인 → 상태·점화식·기저 정의 → top-down으로 시작, 필요 시 bottom-up + 공간 최적화.

---

## 2. Climbing Stairs

> 섹션: Warmup

**문제:** 계단 n칸, 한 번에 1칸 또는 2칸. 꼭대기까지 가는 서로 다른 방법(순서 구분) 수. 예: `n=5 → 8`.

**핵심 관찰:** 경로를 나열하면 답 크기만큼 비용(n=50이면 120억+). **"마지막 한 걸음은 어디서 왔나?"** — n-1에서 1칸, 또는 n-2에서 2칸. 두 그룹은 겹치지 않으므로 합하면 됨.

**풀이**
1. 상태: `ways(j)` = j번째 칸에 도달하는 방법 수.
2. 점화식: `ways(n) = ways(n-1) + ways(n-2)` (= Fibonacci 한 칸 밀린 것).
3. 기저: `ways(1)=1`, `ways(2)=2`.
4. 이전 두 값만 필요 → 변수 2개로 슬라이딩.

```python
def climbing_stairs(n: int) -> int:
    if n <= 2:
        return n
    a, b = 1, 2              # ways(i-2), ways(i-1)
    for _ in range(3, n + 1):
        a, b = b, a + b
    return b
```

**복잡도:** 시간 O(n), 공간 O(1).

- 🐛 함정: 순수 재귀는 O(2ⁿ). 기저를 `ways(0)=1`로 둘지 `ways(1)=1, ways(2)=2`로 둘지 일관성 유지.
- ✅ 이 모듈 모든 상수 전이 문제의 원형: **+를 max로** 바꾸면 House Robber, **화살표 3개**면 Tribonacci, **화살표에 비용**을 붙이면 Min Cost Climbing Stairs.
- 🔑 패턴 인식: "몇 가지 방법(distinct ways)", "1 또는 2 스텝", "마지막 동작이 몇 가지뿐" → 상수 전이 DP.

---

## 3. N-th Tribonacci Number

**문제:** `T(0)=0, T(1)=1, T(2)=1`, `T(n)=T(n-1)+T(n-2)+T(n-3)`. n(≤37)이 주어질 때 T(n). 예: `n=4 → 4` (0,1,1,2,4).

**핵심 관찰:** Climbing Stairs와 **화살표 개수만 다름**(2개 → 3개). 점화식이 문제에 그대로 주어져 있으니 계산 순서와 공간만 신경 쓰면 됨.

**풀이**
1. 기저 3개를 먼저 처리(n=0 → 0, n≤2 → 1).
2. 최근 세 값만 유지하며 앞으로 밀기(rolling window).

```python
def nth_tribonacci_number(n: int) -> int:
    if n == 0:
        return 0
    if n <= 2:
        return 1
    a, b, c = 0, 1, 1
    for _ in range(3, n + 1):
        a, b, c = b, c, a + b + c   # 튜플 동시 대입 → temp 불필요
    return c
```

**복잡도:** 시간 O(n), 공간 O(1).

| 수열 | 점화식 | 필요한 변수 |
|---|---|---|
| Fibonacci | 앞 2개 합 | 2 |
| Tribonacci | 앞 3개 합 | 3 |
| k-bonacci | 앞 k개 합 | k (또는 합을 슬라이딩 윈도우로 유지) |

- 🐛 함정: 기저가 3개 — n=0,1,2를 다 처리 안 하면 인덱스 오류. 순수 재귀는 O(3ⁿ).
- ✅ "한 상태가 읽는 이전 상태 개수"는 독립적인 다이얼. 이후엔 그 개수가 **데이터에 따라 변하는** 비상수 전이(LIS 등)로 확장.
- 🔑 패턴 인식: "이전 k개 항으로 정의되는 수열" → 고정 오프셋 상수 전이, 변수 k개 rolling.

---

## 4. Constant Transition DP Introduction

> 섹션: Constant Transition

### 핵심 개념
- 입력이 **1차원 배열/문자열/시퀀스**이고, `dp[i]`가 **고정된 개수**의 이전 상태(`dp[i-1]`, `dp[i-2]` …)에만 의존하는 DP.
- 상태 하나 계산이 O(1) → 전체 **O(n)**.

### 언제 쓰나 (식별 신호)
- 1차원 입력 + prefix/suffix 구조(i의 답이 앞쪽 답들로 결정) + 최적화/카운팅/가능 여부.
- 문제에 **"adjacent(인접)"**, 고정 step 크기가 등장. i에서의 결정이 **다음 1–2칸에만** 영향.
- 예: Climbing Stairs, House Robber, Decode Ways.
- 이전 원소 **아무거나** 선행자가 될 수 있으면(LIS) → 비상수 전이(레슨 21).

### 예: House Robber
`dp[i] = max(dp[i-1], dp[i-2] + money[i])` — i를 건너뛰거나, i를 털고 i-2까지의 최적과 합치거나.

### 상태 정의 선택지
| 상태 | 의미 |
|---|---|
| `dp[i]` | index i에서 **끝나는** prefix의 답 |
| `dp[i]` | index i에서 **시작하는** suffix의 답 |
| `dp[i]` | 원소 i를 **반드시 포함**할 때의 최적 |
| `dp[i]` | 앞 i개 원소에 대한 최적 (i 포함 여부 무관) |
- 점화식이 뒤(작은 index)를 보면 왼→오, 앞(큰 index)을 보면 오→왼으로 채움.

### 템플릿 (공간 O(1))
```python
prev2, prev1 = base0, base1
for i in range(2, n):
    cur = f(prev1, prev2, arr[i])
    prev2, prev1 = prev1, cur
return prev1
```

- 🐛 흔한 실수: "i 포함 강제" 상태와 "앞 i개 최적" 상태를 섞어 써서 답을 `dp[-1]` vs `max(dp)` 중 잘못 고름.
- 🎤 "공간 줄일 수 있나?" → 전이가 고정 k개면 변수 k개로 O(1).

---

## 5. House Robber

**문제:** 집마다 돈 `nums[i]`, **인접한 두 집은 같이 못 턴다**. 최대 금액. 예: `[2,7,9,3,1] → 12` (2+9+1).

**핵심 관찰:** 규칙은 국소적(인접 금지)이지만 결과는 전역적 — 큰 집을 먼저 집으면(greedy) 양옆 합이 더 큰 경우를 놓침(7+3=10 < 12). 부분집합 전수는 지수.
- 집 i는 **턴다 / 안 턴다** 둘 중 하나.
  - 안 턴다 → 앞 i-1개의 최적 `dp[i-1]`.
  - 턴다 → `nums[i] + dp[i-2]` (i-1은 금지).
- **인접 금지 조건을 if문으로 검사하지 않는다** — "턴다" 화살표가 2칸 뒤를 가리키는 것 자체가 제약을 구조적으로 보장.

**풀이**
1. 상태: `dp[i]` = 집 0..i에서 얻을 수 있는 최대.
2. 점화식: `dp[i] = max(dp[i-1], dp[i-2] + nums[i])`.
3. 기저: `dp[0]=nums[0]`, `dp[1]=max(nums[0], nums[1])`. (또는 빈 prefix `dp[0]=0`을 두는 1-indexed 버전)

```python
def rob(nums: list[int]) -> int:
    prev2, prev1 = 0, 0          # dp[i-2], dp[i-1]  (빈 prefix = 0)
    for x in nums:
        prev2, prev1 = prev1, max(prev1, prev2 + x)
    return prev1
```

**복잡도:** 시간 O(n), 공간 O(1).

- 🐛 함정: n=1 처리, greedy(큰 값부터/홀짝 합) 반례. "i를 반드시 턴다" 상태로 정의하면 답이 `max(dp)`가 되어 헷갈림 → "앞 i개 최적"으로 정의.
- ✅ Climbing Stairs에서 **+ → max**, 두 화살표의 **길이가 다름**(1칸/2칸)이 유일한 차이. 변형: 원형 배열(House Robber II → 첫 집 제외/마지막 집 제외 두 번), 트리(House Robber III → Tree DP).
- 🔑 패턴 인식: "인접한 것을 같이 고를 수 없다", "최대 합" → take/skip 상수 전이 DP. **금지 조건은 전이의 도달 거리로 표현**.

---

## 6. Min Cost Climbing Stairs

**문제:** `cost[i]` = i번 계단을 **떠날 때** 내는 비용, 한 번에 1 또는 2칸. index 0 또는 1에서 공짜로 시작. 꼭대기(**마지막 index 한 칸 너머**)까지 최소 비용. 예: `[10,15,20] → 15` (1번에서 시작, 15 내고 2칸).

**핵심 관찰:** Climbing Stairs의 두 화살표 그대로, 각 화살표에 **출발 계단의 가격**을 달고 연산을 `+ → min`으로.

**풀이**
1. 상태: `dp[i]` = i번 위치에 서기 위한 최소 비용 (i = 0..n, **n = 꼭대기**).
2. 점화식: `dp[i] = min(dp[i-1] + cost[i-1], dp[i-2] + cost[i-2])`.
3. 기저: `dp[0] = dp[1] = 0` (시작 공짜).
4. 답: `dp[n]` → 표 크기는 `len(cost)+1`.

```python
def min_cost_climbing_stairs(cost: list[int]) -> int:
    prev2, prev1 = 0, 0                    # dp[0], dp[1]
    for i in range(2, len(cost) + 1):
        prev2, prev1 = prev1, min(prev1 + cost[i - 1], prev2 + cost[i - 2])
    return prev1
```

**복잡도:** 시간 O(n), 공간 O(1).

- 🐛 함정: **off-by-one** — 답은 마지막 계단이 아니라 그 **한 칸 위**. 비용은 "도착"이 아니라 "떠날 때" 지불. `dp[n-1]`을 반환하면 틀림.
- ✅ 카운팅(+) → 비용(min)으로 바꿔도 구조는 동일.
- 🔑 패턴 인식: "1 또는 2칸", "최소 비용", "시작 위치 선택 가능" → 가중치 붙은 상수 전이.

---

## 7. Minimum Cost For Tickets

**문제:** 여행일 `days`(정렬, 1..365), 1일/7일/30일 패스 가격 `costs`. 모든 여행일을 덮는 최소 비용. 예: `days=[1,4,6,7,8,20], costs=[2,7,15] → 11` (1일권@1 + 7일권@4 + 1일권@20).

**핵심 관찰:** 거꾸로 생각 — 날 d를 덮는 마지막 패스는 ① d에 산 1일권 ② d-6에 산 7일권 ③ d-29에 산 30일권. 각 패스는 **고정 길이(1, 7, 30)의 화살표**. 어떤 패스가 "활성"인지 추적할 필요 없음.

**풀이**
1. 상태: `dp[d]` = 1..d일까지의 여행일을 모두 덮는 최소 비용 (**달력 날짜로 인덱싱**).
2. 여행 안 하는 날: `dp[d] = dp[d-1]` (비용 없음).
3. 여행일: `dp[d] = min(dp[d-1]+c1, dp[max(0,d-7)]+c7, dp[max(0,d-30)]+c30)`.
4. 답: `dp[last_day]`.

```python
def minimum_cost_for_tickets(days: list[int], costs: list[int]) -> int:
    travel, last = set(days), days[-1]
    dp = [0] * (last + 1)
    for d in range(1, last + 1):
        if d not in travel:
            dp[d] = dp[d - 1]
        else:
            dp[d] = min(dp[d - 1] + costs[0],
                        dp[max(0, d - 7)] + costs[1],
                        dp[max(0, d - 30)] + costs[2])
    return dp[last]
```

**복잡도:** 시간 O(last_day) ≤ O(365), 공간 O(last_day) (최근 30일만 유지하면 O(30)).

- 🐛 함정: `d-7`, `d-30`이 음수 → `max(0, …)`로 클램프. 여행일 index(trip 번호)로 dp를 잡으면 "7일 전"을 찾기 번거로움 → **날짜 인덱싱**이 간단. 1일권이 7일권보다 항상 싸다고 가정하지 말 것(가격이 역전될 수 있음 — min이 알아서 처리).
- ✅ 화살표 3개의 길이가 제각각이어도 **고정 오프셋이면 상수 전이**. Coin Change는 동전마다 화살표 하나.
- 🔑 패턴 인식: "1/7/30일 패스", "여러 기간 옵션 중 최소 비용으로 모두 커버" → 고정 오프셋 min DP.

---

## 8. Grid DP Introduction

> 섹션: Grid

### 핵심 개념
- 2차원 행렬 위에서 **이동 방향이 제한된**(보통 오른쪽/아래) 문제. 상태 `dp[r][c]` = 셀 (r,c)에서의 부분 문제 답. 상태 수 O(m·n).

### 언제 쓰나
- 입력이 grid/matrix + **이동 제한**(right/down only, 또는 제한된 인접 방향).
- 질문이 최소 비용/최대 값/경로 수/도달 가능 여부.
- ⚠️ **4방향 자유 이동**이면 DAG가 아님(사이클) → BFS/Dijkstra 같은 그래프 알고리즘.

### 전이 패턴
| 이동 | 의존 셀 | 결합 연산 |
|---|---|---|
| 오른쪽/아래 | `(r-1,c)`, `(r,c-1)` | 카운팅 → `+`, 최적화 → `min/max`, 가능 여부 → `or` |
| + 대각선 | 추가로 `(r-1,c-1)` | 동일 |

### 템플릿 (Unique Paths)
```python
def grid_dp(m, n):
    dp = [[0] * n for _ in range(m)]
    for r in range(m):
        for c in range(n):
            if r == 0 or c == 0:
                dp[r][c] = 1                     # 첫 행/열: 경로 1개
            else:
                dp[r][c] = dp[r-1][c] + dp[r][c-1]
    return dp[m-1][n-1]
```

### 처리 순서 · 장애물 · 공간
- **순서:** 위→아래 행, 각 행은 왼→오. 그래야 위/왼쪽이 먼저 계산됨. (역방향 의존이면 오른쪽 아래부터 — Dungeon Game)
- **장애물:** 카운팅이면 `dp=0`, min/max면 `±inf`.
- **공간 최적화:** 이전 행만 필요 → O(n) 1차원 배열 한 줄을 제자리 갱신 (`dp[c] += dp[c-1]`).

- 🐛 흔한 실수: 첫 행/열 기저 누락, 장애물 뒤 첫 행/열 셀까지 1로 채움(막히면 그 뒤로 0이어야 함).
- 🎤 "그리드에서 DP vs BFS?" → 이동이 단조(right/down)라 DAG면 DP, 사방 이동이면 BFS/Dijkstra.

---

## 9. Unique Paths

**문제:** m×n 그리드, 로봇은 좌상단에서 **오른쪽/아래로만** 이동. 우하단까지 서로 다른 경로 수. 예: `m=2, n=3 → 3`, `m=5, n=3 → 15`.

**핵심 관찰:** 브루트포스 재귀는 같은 칸을 계속 재계산 — 5×3에서 호출 55번 vs 칸 15개, 10×10이면 184,755번 vs 100칸. 놀라운 점: **칸 (r,c)가 재계산되는 횟수 = 그 칸까지의 경로 수** (그 칸에 도착하는 모든 경로가 호출하므로). 즉 낭비된 일이 곧 답.
- **여정이 아니라 도착을 센다:** 모든 경로는 위 칸 또는 왼쪽 칸 중 **정확히 하나**를 거쳐 들어옴 → 두 집합은 겹치지 않으니 더하면 됨.

**풀이**
1. 상태: `dp[r][c]` = (r,c)까지 경로 수.
2. 점화식: `dp[r][c] = dp[r-1][c] + dp[r][c-1]`.
3. 기저: 첫 행, 첫 열 모두 1 (오른쪽만 / 아래만).
4. 순서: 행 위→아래, 행 안 왼→오. 순서가 뻔하니 재귀 없이 표로.

```python
def unique_paths(m: int, n: int) -> int:
    dp = [1] * n                      # 첫 행
    for _ in range(1, m):
        for c in range(1, n):
            dp[c] += dp[c - 1]        # 위(dp[c]) + 왼쪽(dp[c-1])
    return dp[-1]
```

**복잡도:** 시간 O(m·n), 공간 O(n) (1행 배열). 조합식 `C(m+n-2, m-1)`으로 O(min(m,n))도 가능.

- 🐛 함정: 답이 매우 빠르게 커짐 — 18×18이면 32-bit signed overflow(Python은 무관, C/Java는 주의). 1×1 그리드 → 1.
- ✅ 같은 그리드, 연산자만 바꾸면: 장애물 → 0 (Unique Paths II), `min + 셀 비용` (Minimum Path Sum), 역방향 읽기 (Dungeon Game).
- 🔑 패턴 인식: "오른쪽/아래로만 이동", "경로 수" → 그리드 카운팅 DP. **브루트포스의 중복 호출 횟수 자체가 답이면 표를 만들라는 신호.**

---

## 10. Unique Paths with Obstacles

**문제:** Unique Paths + 장애물(`1`) 칸은 못 밟음. 좌상단→우하단 경로 수. 예: `[[0,0,0],[0,1,0],[0,0,0]] → 2`.

**핵심 관찰:** 새 규칙이 아니라 **장애물 = 0**. 점화식은 그대로(위 + 왼쪽). 장애물 칸이 0이 되면, 그 칸을 통해서만 올 수 있던 칸들도 자동으로 0을 더하게 되어 **차단 효과가 저절로 전파**.

**풀이**
1. 시작 칸이 장애물이면 즉시 0.
2. `dp[j]` 1차원 배열, `dp[0]=1`.
3. 각 행 왼→오: 장애물이면 `dp[j]=0`, 아니면 `dp[j] += dp[j-1]` (j>0).
   - 갱신 전 `dp[j]` = **위 칸**(이전 행 값), `dp[j-1]` = 이미 갱신된 **왼쪽 칸**.

```python
def unique_paths_ii(grid: list[list[int]]) -> int:
    if not grid or grid[0][0] == 1:
        return 0
    n = len(grid[0])
    dp = [0] * n
    dp[0] = 1
    for row in grid:
        for j in range(n):
            if row[j] == 1:
                dp[j] = 0
            elif j > 0:
                dp[j] += dp[j - 1]
    return dp[-1]
```

**복잡도:** 시간 O(m·n), 공간 O(n).

- 🐛 함정: 첫 행/열을 무조건 1로 초기화하면 틀림 — 가장자리 장애물 **이후** 칸은 도달 불가(0). 시작/도착 칸이 장애물인 경우. **루프 방향을 뒤집으면** `dp[j-1]`이 왼쪽이 아니라 이전 행 값이 되어 그럴듯하지만 틀린 답.
- ✅ Rolling array 일반 원칙: 행이 "윗행 + 현재 행 왼쪽"만 읽으면 1행으로 축소 가능 — 단 **순회 방향이 의미를 결정**.
- 🔑 패턴 인식: "장애물/막힌 칸", "경로 수" → 막힌 칸을 0으로 두는 그리드 카운팅 DP.

---

## 11. Minimum Path Sum

**문제:** 음이 아닌 정수 그리드, 오른쪽/아래만 이동. 좌상단→우하단 경로의 셀 값 합 최소(시작·끝 포함). 예: `[[1,3,1],[1,5,1],[4,2,1]] → 7` (1→3→1→1→1).

**핵심 관찰:** **Greedy(다음 칸 싼 쪽) 실패** — 첫 갈림길에서 1을 고르면 아래 4를 피할 수 없어 9. 앞에서 3을 내고 돌아가는 게 정답. 브루트포스의 셀별 재계산 횟수 = Unique Paths 답(경로 수만큼 폭발).
- 셀로 들어오는 경로는 위 또는 왼쪽 **중 하나**. 같은 셀에 같은 비용으로 도착한 경로는 이후 구분 불필요 → 셀당 숫자 하나면 충분.

**풀이**
1. 상태: `dp[r][c]` = (r,c)까지 최소 누적 비용.
2. 점화식: `dp[r][c] = min(dp[r-1][c], dp[r][c-1]) + grid[r][c]`.
3. 기저: 첫 행/열은 이웃이 하나 → 단순 누적합. 시작은 `grid[0][0]`.

```python
def min_path_sum(grid: list[list[int]]) -> int:
    m, n = len(grid), len(grid[0])
    INF = float('inf')
    dp = [INF] * n
    dp[0] = 0
    for r in range(m):
        for c in range(n):
            left = dp[c - 1] if c > 0 else INF
            dp[c] = min(dp[c], left) + grid[r][c]   # dp[c] = 위 칸
    return dp[-1]
```

**복잡도:** 시간 O(m·n), 공간 O(n) (1행 rolling; 2D 표면 O(m·n)).

- 🐛 함정: greedy 반례. 1D 최적화에서 첫 행 처리 시 위 칸이 없음 → `dp` 초기값을 inf로(시작만 0). 음수 셀이 있어도 DP는 유효(이동이 단조이므로), 다만 이 문제는 음이 아님.
- ✅ Unique Paths와 같은 그리드·같은 두 이웃, **`+` → `min` + 자기 셀 비용**. 경로 세기 → 경로 하나 고르기.
- 🔑 패턴 인식: "오른쪽/아래만", "경로 합 최소/최대" → 그리드 min DP. "greedy 반례가 쉽게 나오면" DP.

---

## 12. Maximal Square

**문제:** 0/1 행렬에서 1로만 이루어진 가장 큰 정사각형의 **넓이**. 예: 4×5 예시 행렬 → `4` (2×2).

**핵심 관찰:** 브루트포스(각 셀을 우하단 꼭짓점으로 1×1, 2×2, … 확인)는 이미 확인한 작은 정사각형을 **계속 다시 읽음** — 전부 1인 40×40이면 380만 번 검사. 대신 **셀마다 "여기서 끝나는 최대 정사각형 한 변"을 저장**.
- 한 변 k 정사각형이 (r,c)에서 끝나려면 **위, 왼쪽, 대각선 왼쪽 위** 세 곳에서 각각 k-1 정사각형이 끝나야 함 (세 개가 자기 셀 제외 전체 영역을 덮음). → 가장 약한 것 + 1.
- **대각선이 필요한 이유:** 위·왼쪽만 보면 모서리에 구멍 난 L자 모양도 통과시킴.

**풀이**
1. 상태: `dp[r][c]` = (r,c)를 우하단으로 하는 최대 all-1 정사각형 변 길이. 0 셀은 0.
2. 점화식: `dp[r][c] = min(dp[r-1][c], dp[r][c-1], dp[r-1][c-1]) + 1` (matrix[r][c]==1일 때).
3. 기저: 첫 행/열은 자기 값.
4. 답: **표 전체 최댓값의 제곱** (코너 셀이 아님!).

```python
def maximal_square(matrix: list[list[int]]) -> int:
    if not matrix:
        return 0
    m, n = len(matrix), len(matrix[0])
    dp = [[0] * (n + 1) for _ in range(m + 1)]   # 1-padding → 경계 처리 불필요
    best = 0
    for r in range(1, m + 1):
        for c in range(1, n + 1):
            if matrix[r - 1][c - 1] == 1:
                dp[r][c] = min(dp[r-1][c], dp[r][c-1], dp[r-1][c-1]) + 1
                best = max(best, dp[r][c])
    return best * best
```

**복잡도:** 시간 O(m·n), 공간 O(m·n) (2행 또는 1행 + 대각 임시 변수로 O(n)).

- 🐛 함정: 넓이가 아니라 **변 길이**를 반환 / 마지막 셀 값을 답으로 반환 / 입력이 문자 `'1'`인 변형(LeetCode)에서 정수 비교.
- ✅ 경로(2이웃)가 아니라 **2차원 영역**을 덮을 때는 대각선을 포함한 3이웃.
- 🔑 패턴 인식: "all 1s 최대 정사각형", "여기서 끝나는 최대 X" → 셀을 끝점으로 하는 상태 + 3이웃 min.

---

## 13. Triangle

**문제:** 삼각형 배열(행 r에 r+1개). 꼭대기에서 바닥까지, (r,c)에서 (r+1,c) 또는 (r+1,c+1)로 이동하는 경로의 최소 합. 예: `[[2],[3,4],[6,5,7],[4,1,8,3]] → 11` (2→3→5→1). 음수 가능 — `[[-1],[2,3],[1,-1,-3],[5,2,4,1]] → 0` (greedy는 2).

**핵심 관찰:**
- 행을 왼쪽 정렬하면 인덱싱이 명확: 아래 두 칸 = `[r+1][c]`, `[r+1][c+1]`.
- 브루트포스 재귀는 슬롯별 재계산 횟수가 **파스칼의 삼각형**, 총 호출 **2ⁿ − 1** (20행이면 100만 호출 vs 210칸).
- **질문 방향 뒤집기:** 위에서 내려오면 "어떤 경로로 왔나"에 의존하는 것처럼 보이지만, **"여기서 바닥까지 최소 비용"**은 슬롯에만 의존 → 슬롯당 숫자 하나.

**풀이**
1. 상태: `dp[r][c]` = (r,c)에서 바닥까지의 최소 합.
2. 점화식: `dp[r][c] = triangle[r][c] + min(dp[r+1][c], dp[r+1][c+1])`.
3. 기저: 바닥 행 = 자기 값. 답: `dp[0][0]`.
4. 순서: **아래에서 위로**. (Top-down 메모 `dfs(r,c)`도 자연스러움 — 삼각형 설명 방향과 일치)

```python
def minimum_total(triangle: list[list[int]]) -> int:
    dp = triangle[-1][:]                          # 바닥 행 복사
    for r in range(len(triangle) - 2, -1, -1):
        for c in range(r + 1):                    # 왼→오: dp[c], dp[c+1] 둘 다 아직 '아래 행' 값
            dp[c] = triangle[r][c] + min(dp[c], dp[c + 1])
    return dp[0]
```

**복잡도:** 시간 O(n²) (슬롯 n(n+1)/2개), 공간 O(n) (1행 in-place). 2D 표면 O(n²), top-down은 + 재귀 스택 O(n).

- 🐛 함정: 음수 값이 있으므로 greedy 불가. 위→아래 DP로 하면 마지막 행 전체의 min을 따로 구해야 하고 경계(c=0, c=r) 처리가 번거로움 → 아래→위가 깔끔. 입력을 in-place로 수정하면 부작용.
- ✅ **Rolling array 정확성 = 읽을 값이 이번 패스에서 이미 덮어써졌는지**로 판단. 여기서는 두 읽기(`c`, `c+1`)가 쓰기(`c`)보다 앞(아직 안 쓴 칸)이라 정방향 OK. 방향 자체가 답을 정하지 않음.
- 🔑 패턴 인식: "삼각형", "아래 인접 두 칸으로 이동", "위→아래 최소 합" → 바닥에서 올라오는 그리드 DP.

---

## 14. Dungeon Game

**문제:** 기사가 좌상단→우하단(공주), 오른쪽/아래만. 셀 값만큼 체력 증감(시작·끝 셀 포함). 체력이 **0 이하가 되면 사망**. 필요한 최소 초기 체력. 예: `[[-2,-3,3],[-5,-10,1],[10,30,-5]] → 7`.

**핵심 관찰:**
- 정방향 질문 "여기 도착했을 때 최대 체력?"은 **실패** — `[[0,-10,30],[-1,-1,-1]]`에서 30을 먹는 윗길은 끝 체력이 많지만 -10을 버티려면 시작 11 필요, 아랫길은 4. **끝이 부유한 경로 ≠ 시작이 싼 경로.** 도착 상태가 과거 경로(최저점)에 의존.
- **질문 뒤집기:** "이 방에 **들어갈 때 최소 얼마가 있어야** 끝까지 사는가?" → 앞으로 남은 방에만 의존 → DP 상태로 적합.
- 방향도 뒤집힘: **출구에서 입구로** 채움.

**풀이**
1. 상태: `need[r][c]` = (r,c) 진입 시 필요한 최소 체력.
2. 점화식: `need[r][c] = max(min(need[r+1][c], need[r][c+1]) - dungeon[r][c], 1)`.
3. 기저: 공주 방 `max(1 - dungeon[m-1][n-1], 1)`. 격자 밖은 inf (단, 공주 방의 "다음"은 1로 취급).
4. 순서: 아래→위, 오른쪽→왼쪽. 답: `need[0][0]` (왼쪽 위!).

```python
def dungeon_game(dungeon: list[list[int]]) -> int:
    m, n = len(dungeon), len(dungeon[0])
    INF = float('inf')
    need = [[INF] * (n + 1) for _ in range(m + 1)]
    need[m][n - 1] = need[m - 1][n] = 1          # 공주 방 다음에 필요한 체력 = 1
    for r in range(m - 1, -1, -1):
        for c in range(n - 1, -1, -1):
            nxt = min(need[r + 1][c], need[r][c + 1])
            need[r][c] = max(nxt - dungeon[r][c], 1)
    return need[0][0]
```

**복잡도:** 시간 O(m·n), 공간 O(m·n) (1행 rolling으로 O(n)).

- 🐛 함정: **`max(..., 1)` 누락** — 큰 회복 방 앞에서 필요 체력이 0/음수로 계산되지만, 체력은 항상 **>0**이어야 하므로 최소 1. 정방향 DP나 "최대 경로 합"으로 풀려는 시도(반례 위). 이진 탐색 + 시뮬레이션도 가능하지만 O(mn log) 이고 불필요.
- ✅ 정방향 상태가 안 되면 **"과거에 의존하는 질문인가?"** 확인 → 미래만 보는 질문으로 뒤집고, 채우는 방향도 뒤집는다 (Triangle과 같은 이유).
- 🔑 패턴 인식: "최소 초기값", "어느 시점에도 0 이하 금지", "경로 중 최저점 제약" → **역방향 그리드 DP**.

---

## 15. Dual-Sequence DP Introduction

> 섹션: Dual-Sequence

### 핵심 개념
- **두 문자열/배열의 관계**(공통 부분, 변환 비용, 포함 횟수, 병합)를 묻는 DP. 이 섹션 모든 문제가 **같은 상태 + 같은 세 이웃**, 연산자만 다름.
- 상태: `dp[i][j]` = s1의 앞 i글자, s2의 앞 j글자에 대한 답. 표 크기 **(n+1)×(m+1)** — 0행/0열 = 빈 prefix = 기저.

### 언제 쓰나
- 입력이 **시퀀스 두 개** + 둘 사이 관계를 묻는 질문.
- 길이 제한이 **~1,000–3,000** → O(n·m) 신호.

### 세 이웃 (모든 문제 공통)
| 이웃 | 의미 | 용도 |
|---|---|---|
| `dp[i-1][j-1]` (대각) | 두 글자를 동시에 처리 | 매치, 치환 |
| `dp[i-1][j]` (위) | s1에서 한 글자 버림 | 삭제 / 한쪽 skip |
| `dp[i][j-1]` (왼) | s2에서 한 글자 버림 | 삽입 / 다른 쪽 skip |

| 문제 | `dp[i][j]` | 매치 시 | 불일치 시 |
|---|---|---|---|
| LCS | 최장 공통 부분수열 길이 | 대각 + 1 | max(위, 왼) |
| Edit Distance | 최소 편집 수 | 대각 (공짜) | 1 + min(세 이웃) |
| Delete String | 같게 만드는 최소 삭제 비용 | 대각 (공짜) | min(위+비용, 왼+비용) |
| Distinct Subsequences | s2가 s1에 들어가는 방법 수 | 위 + 대각 (합) | 위 |
| Shortest Common Supersequence | 둘 다 포함하는 최단 문자열 | 한 글자만 유지 | 짧은 쪽 선택 |

### 같은 점화식의 두 인덱싱
- **표(prefix, 소비한 글자 수):** `dp[i][j]`, 0행/0열에 기저가 공짜로.
- **재귀(suffix, 남은 글자 수):** `dfs(i, j)` = `s1[i:]`, `s2[j:]`의 답. 기저 = 한쪽 소진. 매치 → `dfs(i+1,j+1)`, 불일치 → `dfs(i+1,j)`, `dfs(i,j+1)` 조합.
- 같은 셀을 반대 모서리에서 접근한 것일 뿐.

```python
# 표 템플릿 (LCS 형태)
def dual_seq(a, b):
    n, m = len(a), len(b)
    dp = [[0] * (m + 1) for _ in range(n + 1)]   # 0행/0열 = 빈 prefix 기저
    for i in range(1, n + 1):
        for j in range(1, m + 1):
            if a[i-1] == b[j-1]:
                dp[i][j] = dp[i-1][j-1] + 1
            else:
                dp[i][j] = max(dp[i-1][j], dp[i][j-1])
    return dp[n][m]
```

### 순서 · 공간
- 모든 의존이 위/왼쪽 → **행 위→아래, 행 안 왼→오**면 충분 (구간 DP처럼 길이순 채우기 불필요).
- 공간: 2행 또는 1행으로 O(min(n,m)). **단, 실제 부분수열/편집 스크립트를 복원하려면 전체 표 필요** → 답이 숫자 하나일 때만 공간 최적화.

- 🐛 흔한 실수: `a[i]` vs `a[i-1]` 인덱스 혼동(표는 1-based 길이, 문자열은 0-based). 0행/0열 기저를 문제별로 다르게 채워야 함(Edit Distance는 `i`, `j`, Distinct Subseq는 `dp[i][0]=1`).
- 🎤 "두 문자열 DP의 공통 구조?" → prefix 쌍 상태, 대각/위/왼 세 이웃, 연산자만 문제마다 다름.

---

## 16. Longest Common Subsequence

**문제:** 두 문자열의 최장 공통 **부분수열**(연속 아님, 순서 유지) 길이. 예: `"abcde","ace" → 3`, `"abcd","dcba" → 1`.

**핵심 관찰:**
- Greedy 불가 — 먼저 보이는 공통 문자를 잡으면 다른 짝을 전부 막을 수 있음.
- 브루트포스 재귀(두 포인터)는 같은 `(i,j)` 쌍을 반복 — 버린 순서가 달라도 남은 문자열은 같음. 공통 문자 없는 12+12자면 540만 호출 vs 상태 169개.
- **부분수열을 고르지 말고 앞 글자만 비교하라:**
  - 같으면 → 무조건 LCS에 넣고 둘 다 전진 (공짜 매치를 건너뛸 이유 없음 — exchange argument).
  - 다르면 → 둘 중 적어도 하나는 안 쓰임 → 각각 버려보고 max.

**풀이**
1. Top-down: `dfs(i,j)` = `word1[i:]`, `word2[j:]`의 LCS. 기저: 한쪽 소진 → 0.
2. Bottom-up: `dp[i][j]` = 앞 i, 앞 j글자의 LCS. 매치 → `dp[i-1][j-1]+1`, 불일치 → `max(dp[i-1][j], dp[i][j-1])`. 0행/0열 = 0.

```python
def longest_common_subsequence(a: str, b: str) -> int:
    n, m = len(a), len(b)
    dp = [[0] * (m + 1) for _ in range(n + 1)]
    for i in range(1, n + 1):
        for j in range(1, m + 1):
            if a[i - 1] == b[j - 1]:
                dp[i][j] = dp[i - 1][j - 1] + 1
            else:
                dp[i][j] = max(dp[i - 1][j], dp[i][j - 1])
    return dp[n][m]

def recover_lcs(a, b, dp):                 # 실제 문자열 복원 (전체 표 필요)
    i, j, out = len(a), len(b), []
    while i and j:
        if a[i - 1] == b[j - 1]:
            out.append(a[i - 1]); i -= 1; j -= 1
        elif dp[i - 1][j] >= dp[i][j - 1]:
            i -= 1
        else:
            j -= 1
    return ''.join(reversed(out))
```

**복잡도:** 시간 O(n·m), 공간 O(n·m) (길이만 필요하면 2행 → O(min(n,m))). Top-down은 재귀 깊이 O(n+m).

- 🐛 함정: **subsequence(부분수열) vs substring(부분문자열)** 혼동 — substring이면 불일치 시 `0`으로 리셋하고 답은 표 최댓값. 복원 시 2행 최적화를 쓰면 불가.
- ✅ 두 시퀀스 DP 가족의 **기본 템플릿**: Edit Distance(min+비용), Distinct Subsequences(카운팅), SCS(n+m−LCS).
- 🔑 패턴 인식: "두 문자열", "공통 subsequence", "순서 유지하며 일치" → LCS 2D DP. diff 도구, DNA 정렬.

---

## 17. Edit Distance

**문제:** word1 → word2 변환 최소 연산 수 (삽입/삭제/치환 각 1). 예: `"horse","ros" → 3`, `"intention","execution" → 5`.

**핵심 관찰:**
- 위치별 불일치 개수는 답이 아님 — `"abcde"→"bcdef"`는 전 위치가 다르지만 2번(a 삭제, f 삽입). 삽입/삭제가 뒤를 모두 밀어버림.
- **모든 편집을 앞에서부터(왼→오) 처리하도록 재배열 가능** → 포인터 두 개가 가리키는 글자만 보면 됨. 중간 문자열을 만들 필요 없음.
- 같은 `(i,j)`에 여러 편집 경로로 도달 → 상태 = index 쌍. 9자 vs 9자: 36만 호출 vs 100 상태.

**풀이 (연산 ↔ 이웃)**
| 연산 | 전진 | 표에서 읽는 칸 |
|---|---|---|
| 매치 (공짜) | i, j 둘 다 | `dp[i-1][j-1]` |
| 치환 word1[i]→word2[j] | 둘 다 | `dp[i-1][j-1] + 1` |
| 삭제 word1[i] | i만 | `dp[i-1][j] + 1` |
| 삽입 word2[j] | j만 | `dp[i][j-1] + 1` |
- 기저: `dp[0][j] = j` (j번 삽입), `dp[i][0] = i` (i번 삭제). 답 `dp[n][m]`.

```python
def min_distance(a: str, b: str) -> int:
    n, m = len(a), len(b)
    prev = list(range(m + 1))                 # dp[0][*]
    for i in range(1, n + 1):
        cur = [i] + [0] * m                   # dp[i][0] = i
        for j in range(1, m + 1):
            if a[i - 1] == b[j - 1]:
                cur[j] = prev[j - 1]
            else:
                cur[j] = 1 + min(prev[j - 1], prev[j], cur[j - 1])
        prev = cur
    return prev[m]
```

**복잡도:** 시간 O(n·m), 공간 O(min(n,m)) (2행). 2D 표/메모는 O(n·m), top-down 재귀 깊이 O(n+m).

- 🐛 함정: 기저를 0으로 둠(빈 문자열 대비 비용은 길이). 매치일 때도 +1 하는 실수. 빈 문자열 입력(길이 0 허용).
- ✅ **LCS와의 관계:** 치환이 없으면(삽입/삭제만) `거리 = n + m − 2·LCS`. 치환은 삭제+삽입 두 번을 한 번에 → 이 등식이 깨짐.
- 🔑 패턴 인식: "최소 연산으로 A를 B로", "insert/delete/replace", 오타 교정·유사도 → Levenshtein 2D DP.

---

## 18. Delete String

**문제:** 두 문자열 s1, s2와 글자별 삭제 비용 `costs[0..25]`. 양쪽에서 글자를 삭제해 두 문자열을 같게 만드는 최소 총비용. 예: `costs a=1,b=2,c=3`, `"abc","cba" → 6` (c만 남기고 a,b를 양쪽에서 삭제).

**핵심 관찰:**
- 브루트포스는 2ⁿ·2ᵐ 조합. 포인터 재귀도 삭제 순서만 다른 경로가 같은 `(i,j)`로 모임 → 메모.
- **삭제 비용 최소화 = 살아남는 것의 가치 최대화.** 남는 글자들은 반드시 **공통 부분수열**이고, 각 생존자는 양쪽에서 한 번씩 비용을 아껴줌:
  `총비용 = cost(s1) + cost(s2) − 2 × (가중 LCS 값)` → **글자 가중치 LCS**.

**풀이**
1. 상태: `dp[i][j]` = s1 앞 i, s2 앞 j글자를 같게 만드는 최소 비용.
2. 매치: `dp[i][j] = dp[i-1][j-1]` (공짜로 둘 다 유지 — 매치를 거부할 이유 없음).
3. 불일치: `min(dp[i-1][j] + cost(s1[i-1]), dp[i][j-1] + cost(s2[j-1]))`.
4. 기저: **0이 아님!** `dp[i][0]` = s1 앞 i글자 전부 삭제 비용(누적합), `dp[0][j]`도 동일. `dp[0][0]=0`.

```python
def delete_string(costs: list[int], s1: str, s2: str) -> int:
    c = lambda ch: costs[ord(ch) - 97]
    n, m = len(s1), len(s2)
    dp = [[0] * (m + 1) for _ in range(n + 1)]
    for i in range(1, n + 1):
        dp[i][0] = dp[i - 1][0] + c(s1[i - 1])
    for j in range(1, m + 1):
        dp[0][j] = dp[0][j - 1] + c(s2[j - 1])
    for i in range(1, n + 1):
        for j in range(1, m + 1):
            if s1[i - 1] == s2[j - 1]:
                dp[i][j] = dp[i - 1][j - 1]
            else:
                dp[i][j] = min(dp[i - 1][j] + c(s1[i - 1]),
                               dp[i][j - 1] + c(s2[j - 1]))
    return dp[n][m]
```

**복잡도:** 시간 O(n·m), 공간 O(n·m) (2행으로 O(min(n,m))).

- 🐛 함정: 0행/0열을 0으로 두는 실수(빈 prefix와 맞추려면 전부 삭제해야 함). Top-down 기저에서 매번 suffix 합을 계산하면 느림 → suffix 합 전처리. 상태는 **문자가 아니라 위치 쌍**.
- ✅ LeetCode 583(삭제 횟수, 모든 비용 1), 712(ASCII 합 삭제)와 동일 구조.
- 🔑 패턴 인식: "양쪽에서 삭제해 같게", "삭제 비용 최소" → 가중 LCS / 두 시퀀스 min DP.

---

## 19. Distinct Subsequences

**문제:** s의 부분수열 중 t와 같은 것의 개수 (**위치 집합이 다르면 다른 것**). 예: `"rabbbit","rabbit" → 3`, `"babgbag","bag" → 5`.

**핵심 관찰:**
- 글자별 후보 위치 수를 곱하면 과대계산(위치가 증가해야 함). 전부 생성 후 필터링은 지수.
- **s의 각 글자마다 예/아니오 하나:** "이 글자를 t가 다음에 필요로 하는 글자로 쓸까?" → 모든 부분수열이 이 답 시퀀스 하나와 1:1 대응 → 중복·누락 없음.
- skip 가지와 use 가지는 **서로소 집합** → max/min이 아니라 **합**.

**풀이**
1. Top-down: `count(i,j)` = `s[i:]`로 `t[j:]`를 만드는 방법 수. 기저: `j==len(t)` → 1 (**먼저 검사**), `i==len(s)` → 0. 전이: `count(i+1,j)` + (매치면) `count(i+1,j+1)`.
2. Bottom-up: `dp[i][j]` = s 앞 i로 t 앞 j 만드는 수. 매치: `dp[i-1][j] + dp[i-1][j-1]`, 불일치: `dp[i-1][j]`. **`dp[i][0]=1`**(빈 target은 1가지), `dp[0][j>0]=0`.
3. 1행 압축: j를 **오른쪽→왼쪽**으로 돌아야 `dp[j-1]`이 아직 이전 행(대각) 값.

```python
def distinct_subsequences(s: str, t: str) -> int:
    m, n = len(s), len(t)
    if n > m:
        return 0
    dp = [0] * (n + 1)
    dp[0] = 1
    for i in range(1, m + 1):
        for j in range(min(i, n), 0, -1):   # 역방향! + j>i는 항상 0이라 생략
            if s[i - 1] == t[j - 1]:
                dp[j] += dp[j - 1]
    return dp[n]
```

**복잡도:** 시간 O(m·n), 공간 O(n) (1행). 2D/메모는 O(m·n).

- 🐛 함정: 1행 최적화에서 **정방향 루프 → 대각 값이 이미 덮어써져 오답** (0/1 knapsack과 같은 이유). 기저 순서(`j==len(t)` 먼저). 결과 값이 빠르게 커짐 → 고정폭 정수 언어에서 overflow 주의.
- ✅ 같은 상태, 다른 결합: **최장 → max, 최소 연산 → min, 몇 가지 → sum**. 카운팅이면 기저가 0이 아니라 **1**(아무것도 안 하는 한 가지 방법).
- 🔑 패턴 인식: "s의 부분수열 중 t와 같은 것의 개수", "몇 가지 방법으로 embed" → 카운팅 두 시퀀스 DP.

---

## 20. Shortest Common Supersequence

**문제:** str1, str2를 모두 부분수열로 포함하는 **가장 짧은 문자열** 자체를 반환. 예: `"abac","cab" → "cabac"`, `"abcde","ace" → "abcde"`.

**핵심 관찰:**
- 이어붙이기(`"abaccab"`, 길이 7)는 공유 가능한 글자를 두 번 씀. Greedy 공유도 실패(한 곳 공유 선택이 다른 공유를 막음).
- **마지막 글자를 누가 쓰나?** 끝 글자가 같으면 한 번만 써서 둘 다 닫음. 다르면 답의 마지막 글자는 둘 중 **한쪽 것** → 2지선다.
- 길이 공식: **`|SCS| = m + n − LCS`** (LCS를 따라 공유되는 글자만 한 번씩).
- 문자열을 상태에 저장하면 상태당 O(m+n) 복사 → **LCS 길이 표를 채운 뒤 역추적(backtrack)으로 문자열 복원**.

**풀이**
1. LCS 표 `dp[i][j]` 채우기.
2. `(m,n)`에서 `(0,0)`으로 역추적:
   - 글자 같음 → 한 번 쓰고 대각 이동.
   - `dp[i-1][j] > dp[i][j-1]` → `str1[i-1]` 쓰고 위로.
   - 그 외 → `str2[j-1]` 쓰고 왼쪽으로.
3. 한쪽 index가 0이 되면 남은 쪽 글자 전부 추가. 모은 글자를 뒤집기.

```python
def shortest_common_supersequence(a: str, b: str) -> str:
    m, n = len(a), len(b)
    dp = [[0] * (n + 1) for _ in range(m + 1)]
    for i in range(1, m + 1):
        for j in range(1, n + 1):
            dp[i][j] = dp[i-1][j-1] + 1 if a[i-1] == b[j-1] else max(dp[i-1][j], dp[i][j-1])
    out, i, j = [], m, n
    while i and j:
        if a[i-1] == b[j-1]:
            out.append(a[i-1]); i -= 1; j -= 1
        elif dp[i-1][j] > dp[i][j-1]:
            out.append(a[i-1]); i -= 1
        else:
            out.append(b[j-1]); j -= 1
    out.extend(reversed(a[:i]))          # 남은 prefix (역순으로 쌓는 중이므로 reversed)
    out.extend(reversed(b[:j]))
    return ''.join(reversed(out))
```

**복잡도:** 시간 O(m·n) (표) + O(m+n) (역추적), 공간 O(m·n) — **역추적에 전체 표가 필요해 2행 최적화 불가**. 문자열 메모 top-down은 O(m·n·(m+n)).

- 🐛 함정: 동점(tie) 처리 방향에 따라 답 문자열이 달라질 수 있음(길이는 같음). 남은 prefix를 추가할 때 순서(역순 누적 후 전체 reverse). 문자열 자체를 DP 값으로 저장하면 메모리 폭발.
- ✅ **상태에 답 자체를 저장 vs 답을 복원할 숫자만 저장** — 후자가 표준(LCS 복원과 같은 역추적).
- 🔑 패턴 인식: "둘 다 subsequence로 포함하는 최단 문자열" → LCS 표 + 역추적. 길이만 물으면 `m+n−LCS`.

---

## 21. Non-constant Transition DP Introduction

> 섹션: Non-constant Transition

### 핵심 개념
- `dp[i]`가 **가변 개수**의 이전 상태에 의존 — 고정 오프셋(`i-1`, `i-2`)이 아니라 조건을 만족하는 **모든 j < i**를 훑어야 함.
- 일반형: `dp[i] = best(dp[j] + something) for all valid j < i` → 상태당 O(n), 전체 **O(n²)** (lookback이 k로 제한되면 O(n·k)).

### 언제 쓰나
- **부분수열(subsequence)** 문제(순서 유지, 비연속).
- 조건에 따라 **이전 원소 아무거나** 선행자가 될 수 있음.
- 고정 step 없음 ("항상 1–2칸 뒤" 같은 패턴이 아님). 또는 "길이 ≤ k 구간으로 분할".

### 예 1: LIS
`dp[i]` = i에서 **끝나는** 최장 증가 부분수열 길이. `nums[j] < nums[i]`인 모든 j에 대해 `dp[i] = max(dp[i], dp[j]+1)`. 답 = `max(dp)`.

### 예 2: Partition Array for Maximum Sum
`dp[i]` = 앞 i개 원소의 최대 합. 마지막 조각 길이 1..k를 모두 시도: `dp[i] = max(dp[i-L] + max(arr[i-L:i]) * L)`.

```python
# 템플릿: 모든 이전 상태 스캔
def non_constant(arr):
    n = len(arr)
    dp = [base] * n
    for i in range(n):
        for j in range(i):              # 또는 range(max(0, i-k), i)
            if valid(j, i):
                dp[i] = best(dp[i], dp[j] + gain(j, i))
    return answer_from(dp)              # dp[-1] 또는 max(dp)
```

### 상수 vs 비상수 전이
| | 상수 전이 | 비상수 전이 |
|---|---|---|
| Lookback | 고정 1–2개 | 가변(최대 전부) |
| 상태당 시간 | O(1) | O(n) 또는 O(k) |
| 전체 | O(n) | O(n²) / O(n·k) |
| 공간 최적화 | 보통 O(1) | 보통 O(n) 필요 |
| 예 | House Robber | LIS |

### 최적화 기법 (문제별)
- **이진 탐색:** LIS → O(n log n) (patience sorting / tails 배열).
- **단조 deque/stack:** 특정 전이를 분할상환 O(1).
- **Segment tree / BIT:** 범위 max/min 질의를 O(log n).

- 🐛 흔한 실수: "i에서 끝나는" 상태인데 `dp[-1]`을 답으로 반환(→ `max(dp)`). 초기값(LIS는 1).
- 🎤 "O(n²)보다 빠르게?" → 전이의 best 질의를 자료구조(이진 탐색/단조 큐/세그트리)로 가속.

---

## 22. Longest Increasing Subsequence

**문제:** 정수 배열에서 **엄격히 증가**하는 가장 긴 부분수열의 길이. 예: `[10,9,2,5,3,7,101,18] → 4` (2,3,7,101).

**핵심 관찰:**
- Greedy 실패: `[3,10,2,1,20]`에서 가장 작은 1부터 → [1,20] 길이 2, 정답 [3,10,20] 길이 3.
- 브루트포스 2ⁿ 부분수열.
- **체인을 마지막 원소에 고정:** "여기서 시작하는 최장"이 아니라 **"여기서 끝나는 최장"**. 체인을 연장하는 데 필요한 건 마지막 원소뿐.
- 처음으로 **읽는 이웃이 위치가 아니라 값에 의해 결정**되는 점화식(더 작은 값을 가진 모든 이전 칸).

**풀이 (O(n²))**
1. 상태: `dp[i]` = `nums[i]`로 끝나는 LIS 길이. 초기값 1 (자기 혼자).
2. 전이: `nums[j] < nums[i]`인 모든 j<i → `dp[i] = max(dp[i], dp[j]+1)`.
3. 답: **`max(dp)`** (최장 체인은 어디서든 끝날 수 있음).

**최적화 (O(n log n), patience sorting)**
- `tails[k]` = 길이 k+1인 증가 부분수열들의 **가장 작은 끝값**. (같은 길이면 끝이 작을수록 연장 여지가 큼)
- `tails`는 항상 정렬 → 새 원소 x의 자리를 **이진 탐색**(strict 증가이므로 `bisect_left`: x 이상인 첫 위치)으로 찾아 교체 또는 끝에 추가.

```python
from bisect import bisect_left

def lis_n2(nums: list[int]) -> int:
    if not nums:
        return 0
    dp = [1] * len(nums)
    for i in range(len(nums)):
        for j in range(i):
            if nums[j] < nums[i]:
                dp[i] = max(dp[i], dp[j] + 1)
    return max(dp)

def lis_nlogn(nums: list[int]) -> int:
    tails = []
    for x in nums:
        k = bisect_left(tails, x)       # strict: 같은 값은 교체 (non-decreasing이면 bisect_right)
        if k == len(tails):
            tails.append(x)
        else:
            tails[k] = x
    return len(tails)
```

**복잡도:** O(n²) DP: 시간 O(n²), 공간 O(n). 이진 탐색: 시간 **O(n log n)**, 공간 O(n).

| 방법 | 시간 | 핵심 |
|---|---|---|
| 브루트포스 | O(n·2ⁿ) | 모든 부분수열 검사 |
| DP (끝나는 위치) | O(n²) | `dp[i]` = i에서 끝나는 LIS |
| DP + 이진 탐색 | O(n log n) | `tails[k]` = 길이 k+1 최소 끝값 |

- 🐛 함정: 답을 `dp[-1]`로 반환. 빈 배열(길이 0 허용). `tails`는 **실제 LIS가 아님**(길이만 맞음) — 복원하려면 parent 포인터 필요. strict vs non-decreasing에 따라 `bisect_left`/`bisect_right`.
- ✅ "여기서 어디로 갈 수 있나"가 아니라 **"어디서 왔나"**로 생각. Russian Doll Envelopes, Largest Divisible Subset, Longest String Chain이 같은 뼈대.
- 🔑 패턴 인식: "increasing subsequence", "순서 유지하며 체인 최대 길이" → LIS (n≤2500이면 O(n²), 10⁵면 O(n log n)).

---

## 23. Partition Array for Maximum Sum

**문제:** 배열을 길이 ≤ k인 연속 부분배열들로 분할, 각 조각의 모든 원소를 그 조각 최댓값으로 바꿈. 가능한 최대 합. 예: `arr=[1,15,7,9,2,5], k=3 → 72` ([1,15,7]|[9,2,5] → 45+27).

**핵심 관찰:**
- Greedy(큰 값끼리 묶기) 실패: [15,7,9]를 묶으면 56. 큰 값은 **작은 이웃 옆에서** 더 많이 끌어올림. 조각 크기도 균등하면 안 됨.
- **마지막 조각만 미정:** 앞부분은 더 작은 부분 문제가 이미 해결. i에서의 선택은 "마지막 조각 길이 L ∈ [1, min(k,i)]" 하나뿐.
- "모든 조각 ≤ k" 제약을 **상태 정의에 포함** → `dp[i-L]`은 이미 합법 분할이므로 각 단계는 마지막 조각 하나만 검사하면 됨.

**풀이**
1. 상태: `dp[i]` = 앞 i개 원소의 최대 합 (모든 조각 ≤ k). `dp[0]=0`.
2. 점화식: `dp[i] = max_{L=1..min(k,i)} dp[i-L] + max(arr[i-L:i]) * L`.
3. L을 늘리며 뒤로 확장할 때 **running max**를 유지 → 내부 루프 O(1).
4. 답: `dp[n]`. 실제 분할은 각 i에서 이긴 L을 역추적.

```python
def partition_array_for_maximum_sum(arr: list[int], k: int) -> int:
    n = len(arr)
    dp = [0] * (n + 1)
    for i in range(1, n + 1):
        cur_max = 0
        for L in range(1, min(k, i) + 1):
            cur_max = max(cur_max, arr[i - L])          # 뒤로 한 칸씩 확장
            dp[i] = max(dp[i], dp[i - L] + cur_max * L)
    return dp[n]
```

**복잡도:** 시간 O(n·k), 공간 O(n) (최근 k+1개만 두면 O(k)).

- 🐛 함정: L 상한을 `k`로만 두고 `i`를 잊음(음수 인덱스). 매 L마다 `max(arr[i-L:i])`를 새로 계산하면 O(n·k²).
- ✅ 상수 전이(고정 화살표)와 데이터 의존 전이(LIS) 사이: **화살표 수가 k로 제한**되지만 2개보다 많고, 각 화살표 값이 창(window)에 의존.
- 🔑 패턴 인식: "길이 ≤ k 연속 조각으로 분할", "분할 후 점수 최대/최소" → `dp[i] = best(dp[i-L] + cost(마지막 조각))` 분할 DP.

---

## 24. Largest Divisible Subset

**문제:** 서로 다른 양의 정수 집합에서, **모든 쌍**이 한쪽이 다른 쪽을 나누는 최대 부분집합의 크기. 예: `[8,9,4,2,12,1,3] → 4` ({1,2,4,8} 또는 {1,2,4,12}).

**핵심 관찰:**
- 브루트포스: 2ⁿ 부분집합 × 쌍 검사.
- **나눗셈은 추이적(transitive):** a|b, b|c → a|c. 그래서 유효한 부분집합 = **배수 체인**.
- **정렬하면** 체인이 "각 원소가 다음 원소를 나누는" 부분수열이 됨 → 새 원소는 **체인의 마지막 원소만** 검사하면 충분 (나머지는 추이성이 보장).
- = **LIS에서 `<`를 `%==0`으로 바꾼 것.**

**풀이**
1. `nums.sort()`.
2. 상태: `dp[i]` = 정렬된 `nums[i]`로 끝나는 최대 체인 크기. 초기 1.
3. 전이: `nums[i] % nums[j] == 0`인 모든 j<i → `dp[i] = max(dp[i], dp[j]+1)`.
4. 답: `max(dp)`. (부분집합 자체가 필요하면 `parent[i]=j` 기록 후 역추적 — LeetCode 368)

```python
def find_largest_subset(nums: list[int]) -> int:
    nums.sort()
    n = len(nums)
    dp = [1] * n
    for i in range(n):
        for j in range(i):
            if nums[i] % nums[j] == 0:
                dp[i] = max(dp[i], dp[j] + 1)
    return max(dp)
```

**복잡도:** 시간 O(n log n + n²) = O(n²), 공간 O(n).

| | LIS | Largest Divisible Subset |
|---|---|---|
| 조건 | `nums[j] < nums[i]` | `nums[i] % nums[j] == 0` |
| 정렬 필요 | 아니오 (원래 순서가 의미) | **예** (추이성 활용, 부분집합이므로 순서 자유) |
| 상태/전이 | i에서 끝나는 길이, `max(dp[j]+1)` | 동일 |

- 🐛 함정: **정렬 안 함** → 큰 수가 앞에 오면 체인을 놓침. 나눗셈 방향(`nums[i] % nums[j]`, 큰 것 % 작은 것). 답을 `dp[-1]`로.
- ✅ 쌍(pairwise) 조건이 추이적이면 → 정렬 후 **체인의 끝만 비교**하는 LIS 형태로 축소.
- 🔑 패턴 인식: "모든 쌍이 나누어떨어지는 최대 부분집합", "체인" → 정렬 + LIS 변형.

---

## 25. Divisor Game

**문제:** 칠판에 n. 차례마다 `0 < x < n`이고 `n % x == 0`인 x를 골라 n을 `n − x`로. 움직일 수 없는 사람이 패배. 선공(Player 1)이 최선을 다할 때 이기는가? 예: `n=2 → true`, `n=3 → false`.

**핵심 관찰:**
- 앞으로 시뮬레이션하면 게임 트리가 지수적으로 커짐.
- **종료 상태에서 거꾸로:** 모든 위치는 **W(승)** 또는 **L(패)**.
  - 이동할 수 없으면 L. (`n=1` → L)
  - 상대를 **L로 보내는 수가 하나라도** 있으면 W.
  - 모든 수가 상대에게 W를 주면 L.
- 화살표(가능한 수) 수 = n의 진약수 개수 → **소인수분해에 따라 가변** (비상수 전이).

**풀이**
1. 상태: `dp[i]` = 칠판이 i일 때 둘 차례인 사람이 이기는가.
2. 기저: `dp[1] = False`.
3. 전이: `dp[i] = any(not dp[i-x] for x in 진약수(i))`.
4. 표를 채워 보면 **짝수 = W, 홀수 = L** 패턴:
   - 홀수의 약수는 모두 홀수 → 홀−홀 = 짝수를 상대에게 줌.
   - 짝수는 1을 빼서 항상 홀수(L)를 상대에게 줄 수 있음.

```python
def divisor_game(n: int) -> bool:
    dp = [False] * (n + 1)                 # dp[1] = False
    for i in range(2, n + 1):
        for x in range(1, i):
            if i % x == 0 and not dp[i - x]:
                dp[i] = True
                break                       # 이기는 수 하나면 충분
    return dp[n]

def divisor_game_o1(n: int) -> bool:
    return n % 2 == 0
```

**복잡도:** DP 시간 O(n²) (약수를 √i까지만 보면 O(n√n)), 공간 O(n). 패리티 공식 O(1).

| n | 진약수 | 도달 상태 | 결과 |
|---|---|---|---|
| 1 | 없음 | — | L |
| 2 | {1} | 1:L | W |
| 3 | {1} | 2:W | L |
| 4 | {1,2} | 3:L, 2:W | W |
| 6 | {1,2,3} | 5:L, 4:W, 3:L | W |

- 🐛 함정: x < n 조건(자기 자신 제외). W/L 정의를 뒤집음("하나라도 L로 보내면 W" vs "모두").
- ✅ **게임 DP 템플릿:** 상태 = 현재 위치(차례는 대칭이라 생략), `win[s] = any(not win[t] for t in moves(s))`. 면접에선 표로 먼저 증명하고 패턴(짝/홀)으로 O(1) 도출하는 흐름이 설득력 있음.
- 🔑 패턴 인식: "두 플레이어가 최선을 다할 때 선공이 이기나", "움직일 수 없으면 패배" → Win/Lose 게임 DP (Nim·Sprague-Grundy 계열).

---

## 26. Knapsack DP Introduction
> 섹션: Knapsack, Weight-Only

**핵심 개념:** 일반 DP는 상태가 1차원(시퀀스 위치, 그리드 칸)이지만 knapsack은 **자원 제약**이라는 두 번째 차원을 더한다 → 상태 = (item index, 남은/누적 자원). "아이템을 고르되 누적 제약(합·용량)을 지킨다"면 거의 knapsack 틀이다.

### 핵심 점화식 — 아이템마다 take / skip
```
dp[i][j] = combine( dp[i-1][j],        # skip
                    dp[i-1][j - w_i] ) # include (j >= w_i일 때)
```
- `combine`만 바뀐다: **feasibility → or**, **counting → +**, **optimization → max/min**.
- Base: `dp[0][0] = True`(아이템 0개로 합 0 가능), `dp[0][j>0] = False`. 행(아이템) 단위로 위→아래, 각 칸은 **윗행만** 참조.

### 질문 유형 4가지
| 유형 | 질문 | 대표 문제 |
|---|---|---|
| Feasibility | T를 만들 수 있나? | Partition Equal Subset Sum |
| Counting | 몇 가지 방법? | Coin Change II, Target Sum |
| Optimization | 최대 가치 / 최소 개수 | 0/1 Knapsack, Coin Change |
| Enumeration | 가능한 합 전부 | Subset Sum |

### 두 개의 분류 축
- **Weight-only vs Weight+Value:** 무게 자체가 기여(subset sum, coin) vs 무게와 가치가 별개(고전 knapsack).
- **0/1 vs Unbounded:** 0/1은 include 시 `dp[i-1][...]`(아이템 소모), unbounded는 `dp[i][...]`(같은 행 = 재사용 가능).
- 1D 압축 시 이 차이가 **순회 방향**으로 바뀐다: **0/1 → 오른쪽→왼쪽**, **unbounded → 왼쪽→오른쪽**.

- ✅ 체크포인트: [3,2,5]로 7 만들기 → 2+5. dp 테이블 행 = 아이템, 열 = 합.
- 🎤 "knapsack 문제인지 어떻게 알아보나?" → 아이템 집합에서 선택 + 누적 제약(합/용량/예산) + 각 아이템 take/skip 결정.

---

## 27. Weight-Only knapsack

**문제:** 아이템 무게 리스트가 주어질 때, 부분집합으로 만들 수 있는 **모든 합**을 반환 (Subset Sum, enumeration). 예: `[1,3,3,5]` → `[0,1,3,4,5,6,7,8,9,11,12]` (2와 10은 불가).

**핵심 관찰:** 최대 합 `S = sum(weights)` ≤ 100·100 = 10⁴ → 합을 인덱스로 하는 boolean 테이블이 충분히 작다. 2ⁿ 부분집합 열거 대신 "도달 가능한 합" 집합을 아이템마다 갱신.

**풀이**
1. `dp[j]` = 지금까지의 아이템으로 합 j를 만들 수 있나. `dp[0] = True`.
2. 아이템 w마다 j를 **S → w (내림차순)** 으로: `dp[j] |= dp[j-w]`. (0/1이므로 역순 — 같은 아이템 두 번 사용 방지)
3. `dp[j]`가 True인 j 전부 반환.

```python
def knapsack_weight_only(weights: list[int]) -> list[int]:
    S = sum(weights)
    dp = [False] * (S + 1)
    dp[0] = True
    for w in weights:
        for j in range(S, w - 1, -1):   # 0/1 → 오른쪽에서 왼쪽
            if dp[j - w]:
                dp[j] = True
    return [j for j in range(S + 1) if dp[j]]
```
- 파이썬 트릭: 비트셋 `bits |= bits << w` (bits=1 시작) 한 줄로도 가능 — 64비트 워드 단위 병렬.

**복잡도:** 시간 O(n·S), 공간 O(S) (2D면 O(n·S)).

- 🐛 함정: j를 **오름차순**으로 돌리면 같은 아이템을 여러 번 쓰게 되어 unbounded가 됨 (`[3]` → 6, 9도 생겨버림). 합 0을 결과에 포함할 것.
- 🔑 키워드: "가능한 모든 합", "subset sum", 값 범위가 작음(≤10⁴~10⁵) → 합 인덱스 boolean DP / bitset.

---

## 28. Partition Equal Subset Sum

**문제:** 음이 아닌 정수 배열을 합이 같은 두 부분집합으로 나눌 수 있나? 예: `[3,4,7]` → True (3+4 | 7), `[1,2,3,5]` → False (합 11 홀수).

**핵심 관찰:** 두 쪽 합이 같다 = 한쪽 합이 `total/2`. → "합 total/2를 만드는 부분집합이 있나?"라는 **subset sum feasibility**로 환원.

**풀이**
1. total이 홀수면 즉시 False.
2. `target = total // 2`, `dp[0] = True`.
3. 각 num에 대해 j를 target → num 역순: `dp[j] = dp[j] or dp[j-num]`.
4. `dp[target]` 반환 (조기 종료 가능).

```python
def can_partition(nums: list[int]) -> bool:
    total = sum(nums)
    if total % 2:
        return False
    target = total // 2
    dp = [False] * (target + 1)
    dp[0] = True
    for x in nums:
        for j in range(target, x - 1, -1):
            dp[j] = dp[j] or dp[j - x]
        if dp[target]:
            return True
    return dp[target]
```

**복잡도:** 시간 O(n·total/2) ≤ 200·10⁴, 공간 O(total/2).

- 🐛 함정: 홀수 체크 누락, 역순 순회 누락(원소 재사용 버그), 0 원소는 어느 쪽에 넣어도 무관 (x=0이면 range가 j=target..0으로 dp[j] or dp[j] — 무해).
- 🔑 키워드: "두 그룹으로 나눠 합을 같게/차이 최소" → 한쪽 합 = 목표로 바꾸는 subset sum. (차이 최소 버전 = Last Stone Weight II)

---

## 29. Target Sum

**문제:** 각 원소(≥0)에 +/−를 붙여 합이 target이 되는 방법 수. 예: `[1,1,1,1,1], target=3` → 5 (−1을 하나만 고르는 5가지). 위치가 다르면 다른 방법.

**핵심 관찰 (대수 변환):** + 쪽 합 P, − 쪽 합 N이면 `P − N = target`, `P + N = total` → **`P = (total + target) / 2`**. → "합이 P인 부분집합 개수"라는 **0/1 subset-sum counting**.

**풀이**
1. `total + target`이 홀수이거나 `|target| > total`이면 0.
2. `P = (total+target)//2`, `dp[0] = 1`.
3. 각 x에 대해 j를 P → x 역순: `dp[j] += dp[j-x]`.

```python
def target_sum(nums: list[int], target: int) -> int:
    total = sum(nums)
    if abs(target) > total or (total + target) % 2:
        return 0
    P = (total + target) // 2
    dp = [0] * (P + 1)
    dp[0] = 1
    for x in nums:
        for j in range(P, x - 1, -1):
            dp[j] += dp[j - x]
    return dp[P]
```
- 대안: 합에 offset(total)을 더해 인덱스를 [0, 2·total]로 옮긴 2D DP `dp[i][s]` — 직관적이지만 공간 2배.

**복잡도:** 시간 O(n·P), 공간 O(P).

- 🐛 함정: **0 원소** — +0과 −0은 서로 다른 방법으로 세야 한다. 위 코드는 x=0일 때 `dp[j] += dp[j]`로 자동 2배가 되어 맞게 처리됨. 음수 target 경계(`abs` 체크) 누락 주의.
- 🔑 키워드: "각 원소에 +/− 부호", "두 그룹 차이가 target" → P = (total+target)/2 변환 후 counting knapsack.

---

## 30. Unbounded Knapsack Introduction

**핵심 개념:** 0/1은 아이템 1회 사용, **unbounded는 아이템 "종류"를 무제한 사용** (동전 액면, 카테고리). "몇 개 쓸까"를 추적하지 않고 여전히 **이진 결정**으로 본다: ① 이 종류를 아예 건너뛰기(i−1로) ② **하나 쓰고 같은 종류에 머무르기(i 그대로)**.

### 0/1과의 유일한 차이 = 한 글자
```
0/1       : dp[i][j] = dp[i-1][j] + dp[i-1][j - w]   # 쓰면 이전 행
unbounded : dp[i][j] = dp[i-1][j] + dp[i][j - w]     # 쓰면 같은 행 (재사용)
```
- `dp[i][j-w]`는 이미 i번째 종류를 여러 번 쓴 경우까지 포함 → 거기에 하나 더 얹으면 무제한 재사용이 자연스럽게 된다.

### Top-down 템플릿
```python
from functools import cache
def count_ways(coins, amount):
    @cache
    def f(i, a):                     # 앞 i개 종류로 a 만들기
        if a == 0: return 1
        if i == 0 or a < 0: return 0
        return f(i - 1, a) + f(i, a - coins[i - 1])   # skip | use & stay
    return f(len(coins), amount)
```

### 1D 압축 = 순회 방향
| | 0/1 | Unbounded |
|---|---|---|
| 사용 횟수 | ≤ 1 | 무제한 |
| use 분기 | `i-1` | `i` (stay) |
| 1D 순회 | **오른쪽→왼쪽** (이전 행 값 읽기) | **왼쪽→오른쪽** (현재 행 값 읽기) |
| 예 | subset sum | coin change |

- ✅ 체크포인트: coins [2,3], amount 5 → {2,3} 1가지.
- 🎤 "0/1과 unbounded를 코드에서 어떻게 구분?" → include 분기가 같은 인덱스를 재참조하느냐, 1D에선 내부 루프 방향.
- 🐛 흔한 실수: 1D에서 방향을 헷갈려 0/1 문제에 재사용을 허용하거나 그 반대.

---

## 31. Coin Change II

**문제:** 동전 액면 리스트(무제한 사용)로 amount를 만드는 **조합** 수. 예: `coins=[1,2,5], amount=5` → 4 (5 / 2+2+1 / 2+1+1+1 / 1×5). `[2], 3` → 0.

**핵심 관찰:** unbounded counting knapsack. **조합**(순서 무시)이므로 동전을 바깥 루프로 두어 "동전 종류 순서대로만 사용"하게 강제 → 같은 조합의 순열 중복 제거.

**풀이**
1. `dp[0] = 1` (아무것도 안 쓰는 1가지).
2. 바깥: 각 coin. 안: j = coin → amount **오름차순** `dp[j] += dp[j-coin]` (같은 행 참조 = 재사용 허용).

```python
def coin_change_ii(coins: list[int], amount: int) -> int:
    dp = [0] * (amount + 1)
    dp[0] = 1
    for c in coins:                       # 바깥 = 아이템 → 조합
        for j in range(c, amount + 1):    # 오름차순 → unbounded
            dp[j] += dp[j - c]
    return dp[amount]
```

**복잡도:** 시간 O(len(coins)·amount), 공간 O(amount).

- 🐛 함정: 루프를 뒤집어(바깥 amount, 안 coin) 쓰면 **순열 수**(1+2와 2+1을 다르게)가 나온다 — 그건 Combination Sum IV. amount=0이면 1.
- 🔑 키워드: "무제한 사용", "조합의 수", "방법의 수" → unbounded counting, coin 바깥 루프.

---

## 32. Coin Change, Optimization

**문제:** 동전(무제한)으로 amount를 만드는 **최소 동전 수**, 불가능하면 −1. 예: `[1,2,5], 11` → 3 (5+5+1). `[1,3,4], 6` → 2 (3+3; 그리디 4+1+1=3개는 틀림). `[3], 1` → −1.

**핵심 관찰:** unbounded knapsack의 **optimization(min)** 버전. combine이 `+` 대신 `min(…, dp[j-c] + 1)`. 그리디는 임의 액면에서 실패하므로 DP 필요.

**풀이**
1. `dp[j]` = j를 만드는 최소 동전 수, `dp[0]=0`, 나머지 `INF`.
2. 각 coin c, j = c → amount 오름차순: `dp[j] = min(dp[j], dp[j-c] + 1)`.
3. `dp[amount]`가 INF면 −1.

```python
def coin_change(coins: list[int], amount: int) -> int:
    INF = float('inf')
    dp = [0] + [INF] * amount
    for c in coins:
        for j in range(c, amount + 1):
            if dp[j - c] + 1 < dp[j]:
                dp[j] = dp[j - c] + 1
    return dp[amount] if dp[amount] != INF else -1
```
- 대안 관점: 0..amount를 노드, 동전을 간선으로 본 **BFS 최단거리** (가중치 1) — 같은 복잡도.

**복잡도:** 시간 O(len(coins)·amount), 공간 O(amount). (최소값 문제라 루프 순서는 바꿔도 결과 동일)

- 🐛 함정: coin이 2³¹−1처럼 amount보다 크면 range가 비어 자연히 무시됨. amount=0 → 0. INF를 `amount+1`로 써도 됨(최대 동전 수 ≤ amount).
- 🔑 키워드: "최소 개수로 만들기", "무제한 사용", "그리디 반례" → unbounded min knapsack.

---

## 33. Perfect Squares

**문제:** n을 완전제곱수(같은 수 반복 가능)의 합으로 쓸 때 최소 개수. 예: 12 → 3 (4+4+4), 13 → 2 (4+9; 그리디 9+1+1+1=4개는 오답).

**핵심 관찰:** "동전" = 1, 4, 9, …, ≤ n 인 제곱수 (√n개). → **Coin Change(min)와 완전히 동일한 unbounded knapsack**. 1이 항상 있으므로 −1 케이스 없음.

**풀이**
1. `dp[0]=0`, `dp[j]=INF`.
2. 각 제곱수 s = k², j = s → n 오름차순: `dp[j] = min(dp[j], dp[j-s]+1)`.

```python
def perfect_squares(n: int) -> int:
    dp = [0] + [n] * n            # 상한: 1을 n번
    k = 1
    while k * k <= n:
        s = k * k
        for j in range(s, n + 1):
            dp[j] = min(dp[j], dp[j - s] + 1)
        k += 1
    return dp[n]
```

**복잡도:** 시간 O(n·√n) ≈ 10⁶, 공간 O(n).

- 🐛 함정: 그리디(가장 큰 제곱부터) 금지. Python에서 n=10⁴이면 10⁶ 연산 — OK.
- 💡 참고: 라그랑주 4제곱 정리로 답은 항상 ≤ 4 (수학 O(√n) 풀이 존재) — 면접에선 DP를 먼저 제시.
- 🔑 키워드: "최소 개수의 X로 합 n 만들기, 재사용 가능" → 아이템 집합을 만들고 coin change min.

---

## 34. 0/1 Knapsack Introduction
> 섹션: Knapsack, Weight+Value

**핵심 개념:** 각 아이템을 **최대 1번** 고를 수 있는 가장 흔한 knapsack. 이제 아이템에 무게(비용)와 **가치**가 따로 있다. `dp[i][j]` = 앞 i개 아이템, 용량 j로 얻는 최대 가치.

### 언제 쓰나
- 배열 원소 하나하나가 서로 다른 아이템, "각 아이템 최대 한 번", 부분집합 선택/분할 문제.

### 점화식
```
dp[i][j] = max( dp[i-1][j],                       # skip
                dp[i-1][j - w_i] + v_i )          # take (j >= w_i)
```
- 두 분기 모두 **이전 행(i−1)** 참조 → 고른 뒤 다음 행으로 넘어가므로 재사용 불가 = 0/1의 정의.

### 1D 공간 최적화 템플릿
```python
def knapsack_01(weights, values, cap):
    dp = [0] * (cap + 1)
    for w, v in zip(weights, values):
        for j in range(cap, w - 1, -1):     # 반드시 오른쪽 → 왼쪽
            dp[j] = max(dp[j], dp[j - w] + v)
    return dp[cap]
```
- 역순이라 `dp[j-w]`는 아직 이번 아이템으로 갱신되지 않은 **이전 행 값**. 정순이면 같은 아이템이 여러 번 들어가 unbounded가 된다.

### combine만 바꾸면 되는 변형
| 질의 | 갱신식 |
|---|---|
| 최적화 | `dp[j] = max(dp[j], dp[j-w] + v)` |
| 경우의 수 | `dp[j] += dp[j-w]` |
| 가능 여부 | `dp[j] = dp[j] or dp[j-w]` |

- **복잡도:** O(n·cap) 시간, O(cap) 공간. (cap이 크면 pseudo-polynomial이라 느려짐 — cap 값 크기에 비례)
- 🎤 "왜 pseudo-polynomial?" → 입력 크기가 아니라 용량 **값**에 비례. cap이 10⁹이면 불가 → 가치 기준 DP나 meet-in-the-middle 고려.

---

## 35. 0/1 Knapsack Practice Problem

**문제:** n개 아이템(weights[i], values[i]), 용량 max_weight 이하로 각 아이템 최대 1번 골라 **최대 가치**. 예: `w=[3,4,7], v=[4,5,8], cap=7` → 9 (3+4 무게로 4+5).

**핵심 관찰:** 교과서 0/1 knapsack. 상태 (아이템 i, 사용 용량 j), take/skip. 무게 7짜리 단독(8)보다 작은 둘의 합(9)이 낫다 → 가치/무게 비율 그리디는 틀린다.

**풀이**
1. Top-down 사고: `f(i, rem)` = i번째부터 남은 용량 rem으로 얻는 최대 가치 = `max(f(i+1, rem), v[i] + f(i+1, rem-w[i]))` (w[i] ≤ rem일 때).
2. Bottom-up 1D: `dp[j]`를 cap → w 역순으로 갱신.

```python
def knapsack(weights: list[int], values: list[int], max_weight: int) -> int:
    dp = [0] * (max_weight + 1)          # dp[j] = 용량 j 이하에서 최대 가치
    for w, v in zip(weights, values):
        for j in range(max_weight, w - 1, -1):
            dp[j] = max(dp[j], dp[j - w] + v)
    return dp[max_weight]
```

**복잡도:** 시간 O(n·W) = 100·1000, 공간 O(W).

- 🐛 함정: dp를 0으로 초기화하면 "용량 **이하**" 의미(정답은 dp[W]). "정확히 W를 채워야" 한다면 `dp[0]=0, 나머지 -INF`로 초기화해야 함. 순회 방향 역순 필수.
- 🔑 키워드: "무게 제한", "가치 최대", "각 물건 한 번" → 0/1 knapsack (fractional이면 그리디).

---

## 36. Bounded Knapsack Introduction

**핵심 개념:** 0/1(≤1회)과 unbounded(무제한) 사이. 아이템 종류 i마다 **재고 q[i]** 가 있어 0..q[i]번 사용 가능.

| 변형 | 사용 횟수 |
|---|---|
| 0/1 | ≤ 1 |
| Bounded | ≤ q[i] |
| Unbounded | 무제한 |

- 예: cap 7, A(w2,v3,q3), B(w3,v4,q2) → A×2 + B×1 = 무게 7, 가치 10. 0/1은 "A 두 번"을 표현 못 하고, unbounded는 "B 세 번"을 허용해버린다.

### 방법 1 — 직접 전이 (개수 x를 전부 시도)
```
dp[i][j] = max_{0 ≤ x ≤ q, x·w ≤ j} ( dp[i-1][j - x·w] + x·v )
```
- 항상 **이전 행**을 읽으므로 재고 초과 불가. 시간 O(n·C·q).

### 방법 2 — Binary decomposition (실전 템플릿)
- q를 **1, 2, 4, …, 나머지** 덩어리로 쪼개면 0..q의 모든 개수를 덩어리 부분집합으로 표현 가능 (예: 13 = 1,2,4,6).
- 각 덩어리를 (k·w, k·v)인 **0/1 아이템**으로 바꿔 평범한 0/1 knapsack 실행.

```python
def bounded_knapsack(items, capacity):          # items: (w, v, q)
    expanded = []
    for w, v, q in items:
        k = 1
        while k <= q:
            expanded.append((k * w, k * v)); q -= k; k *= 2
        if q:
            expanded.append((q * w, q * v))
    dp = [0] * (capacity + 1)
    for w, v in expanded:
        for j in range(capacity, w - 1, -1):    # 0/1 → 역순
            dp[j] = max(dp[j], dp[j - w] + v)
    return dp[capacity]
```

- **복잡도:** 직접 O(n·C·q) → 분해 후 **O(n·C·log q)**, 공간 O(C). (더 나아가 monotonic deque로 O(n·C)도 가능 — 고급)
- 🐛 흔한 실수: 덩어리 합이 q를 넘지 않게 "나머지" 덩어리 처리 누락; 분해 후 순회 방향을 정순으로 하면 덩어리 재사용.
- 🎤 "재고 제한 있는 knapsack?" → 이진 분해로 0/1 환원, O(C·Σlog q).

---

## 37. Bounded Knapsack

**문제:** 아이템 i는 무게 weights[i], 가치 values[i], **최대 quantities[i]개**. 용량 capacity 내 최대 가치. 예: `w=[2,3], v=[5,8], q=[2,1], cap=5` → 13 (각 1개). `w=[2,3,4], v=[3,4,5], q=[3,2,1], cap=10` → 14 (item0×2 + item1×2).

**핵심 관찰:** 제약 n≤100, q≤1000, C≤10⁴ → 직접 전이 O(n·C·q)=10⁹ 불가. **이진 분해**로 아이템당 ~log₂q ≈ 10개 → 약 1000개 0/1 아이템 × 10⁴ = 10⁷.

**풀이**
1. 각 아이템의 q를 1,2,4,…,나머지 덩어리로 쪼개 (k·w, k·v) 0/1 아이템 생성.
2. 1D 0/1 knapsack (역순) 실행.

```python
def bounded_knapsack(weights, values, quantities, capacity):
    dp = [0] * (capacity + 1)
    for w, v, q in zip(weights, values, quantities):
        q = min(q, capacity // w)          # 들어갈 수 있는 개수로 잘라내기
        k = 1
        while q > 0:
            take = min(k, q)
            W, V = take * w, take * v
            for j in range(capacity, W - 1, -1):
                dp[j] = max(dp[j], dp[j - W] + V)
            q -= take
            k <<= 1
    return dp[capacity]
```

**복잡도:** 시간 O(C·Σ log qᵢ), 공간 O(C).

- 🐛 함정: `q = min(q, capacity//w)` 프루닝으로 불필요한 덩어리 제거. 덩어리를 만들며 바로 0/1 갱신하면 expanded 리스트 불필요. `q*w > capacity`면 사실상 unbounded처럼 취급 가능.
- 🔑 키워드: "각 아이템 최대 k개", "재고/수량 제한" → bounded knapsack → binary splitting.

---

## 38. Interval DP Intro
> 섹션: Interval

**핵심 개념:** "i까지의 답"이 아니라 **"구간 [l, r]의 답"** 을 상태로 둔다: `dp[l][r]`. 큰 구간이 그 안의 작은 구간에 의존하므로 작은 구간 결과를 재사용.

### 언제 쓰나 (신호)
- 입력이 선형 시퀀스(배열/문자열)이고 **여러 부분 문자열/부분 배열**을 따져야 함.
- 대표: 팰린드롬, 배열 양끝에서 가져가는 게임, 병합 비용(풍선 터뜨리기, 행렬 곱 순서).
- n ≤ 수백 → O(n²)/O(n³) 기대.

### 두 가지 전이 모양
1. **양끝 제거(shrink):** `dp[l][r]`가 `dp[l+1][r]`, `dp[l][r-1]`, `dp[l+1][r-1]`에서 옴. 예: `pal[l][r] = s[l]==s[r] and pal[l+1][r-1]`. 상태당 O(1) → 총 O(n²).
2. **분할점(split):** `dp[l][r] = best_m (dp[l][m] + dp[m+1][r] + cost(l,m,r))`. 상태당 O(n) → 총 **O(n³)**.

### 템플릿 A — Top-down (의존 순서를 고민할 필요 없음 → 면접 첫 구현으로 추천)
```python
from functools import cache
def count_palindromes(s):
    @cache
    def pal(l, r):
        return l >= r or (s[l] == s[r] and pal(l + 1, r - 1))
    n = len(s)
    return sum(pal(l, r) for l in range(n) for r in range(l, n))
```

### 템플릿 B — Bottom-up: **길이 순서로 채우기**
```python
def count_palindromes(s):
    n = len(s); dp = [[False] * n for _ in range(n)]; cnt = 0
    for length in range(1, n + 1):
        for l in range(n - length + 1):      # 가장 큰 l = n - length
            r = l + length - 1
            dp[l][r] = s[l] == s[r] and (length <= 2 or dp[l + 1][r - 1])
            cnt += dp[l][r]
    return cnt
```
- 대안 fill 순서: `l`을 n−1 → 0 (아래→위), `r`을 l → n−1 (왼→오).
- **복잡도:** 공간 O(n²) (상삼각 테이블), 시간 O(n²) 또는 O(n³).
- 🐛 흔한 실수: bottom-up에서 l, r을 단순 0→n으로 돌려 **아직 안 채운 내부 구간**을 읽음. length ≤ 2 경계(l+1 > r−1) 처리 누락.
- 🎤 "Interval DP fill order?" → 길이 오름차순, 또는 l 역순·r 정순. 핵심은 `[l+1, r-1]`이 먼저 계산되는 것.

---

## 39. Palindromic Substrings

**문제:** 문자열 s의 **팰린드롬 부분 문자열(연속)** 개수. 위치가 다르면 따로 센다. 예: `"aaa"` → 6, `"abcba"` → 7.

**핵심 관찰:** `s[l..r]`이 팰린드롬 ⇔ `s[l]==s[r]` 그리고 `s[l+1..r-1]`이 팰린드롬 → shrink형 interval DP. 또는 같은 성질을 역으로 쓴 **중심 확장(expand around center)** 으로 O(1) 공간.

**풀이 (중심 확장 — 면접 추천)**
1. 중심은 2n−1개: 문자 i(홀수 길이), 사이 (i, i+1)(짝수 길이).
2. 각 중심에서 양쪽이 같은 동안 확장하며 카운트 +1.

```python
def palindromic_substrings(s: str) -> int:
    n, cnt = len(s), 0
    for c in range(2 * n - 1):
        l, r = c // 2, c // 2 + c % 2      # 홀수/짝수 중심 통합
        while l >= 0 and r < n and s[l] == s[r]:
            cnt += 1
            l -= 1; r += 1
    return cnt
```
- Interval DP 버전: 38번 템플릿 B 그대로 (`dp[l][r] = s[l]==s[r] and (len<=2 or dp[l+1][r-1])`).

**복잡도:** 둘 다 시간 O(n²). 공간: DP O(n²), 중심 확장 **O(1)**. (Manacher면 O(n))

- 🐛 함정: 짝수 길이 중심 누락("aa"), substring(연속) vs subsequence(비연속) 혼동.
- 🔑 키워드: "팰린드롬 부분 문자열 개수/최장" → 중심 확장 or `dp[l][r]` 불리언 테이블.

---

## 40. Coin Game

**문제:** 동전 한 줄에서 두 사람이 번갈아 **왼쪽 끝 또는 오른쪽 끝** 동전을 가져감. 내가 선공, 둘 다 최적 플레이일 때 내가 보장받는 최대 점수. 예: `[8,15,3,7]` → 22 (7을 먼저! 큰 8을 먼저 잡으면 15만 얻음). `[2,2,2,2]` → 4.

**핵심 관찰:** 게임 상태 = 남은 구간 [l, r]. 현재 차례인 사람 입장에서 구간의 최대 점수 `f(l, r)`. 상대도 같은 함수로 최적 플레이 → **"구간 합 − 상대가 남은 구간에서 얻는 최대"**.
- `f(l, r) = sum(l..r) − min(f(l+1, r), f(l, r−1))` 또는 동치로 **점수 차이** `d(l,r) = max(c[l] − d(l+1,r), c[r] − d(l,r−1))`, 답 = (total + d(0,n−1)) / 2.

**풀이 (차이 DP, 1D 압축)**
1. `d[l][l] = c[l]`.
2. 길이 순(또는 l 역순, r 정순)으로 `d[l][r] = max(c[l] - d[l+1][r], c[r] - d[l][r-1])`.

```python
def coin_game(coins: list[int]) -> int:
    n = len(coins)
    d = coins[:]                      # d[r] ≡ d[l][r] (현재 l 행)
    for l in range(n - 2, -1, -1):
        for r in range(l + 1, n):
            # d[r]   = 아직 이전 행 값 = d[l+1][r]
            # d[r-1] = 이미 갱신된 값 = d[l][r-1]
            d[r] = max(coins[l] - d[r], coins[r] - d[r - 1])
    return (sum(coins) + d[n - 1]) // 2
```

**복잡도:** 시간 O(n²) = 10⁶, 공간 O(n) (2D면 O(n²)).

- 🐛 함정: "더 큰 쪽을 가져가는" 그리디는 반례 존재(예시 1). 1D 압축 시 d[r]과 d[r−1] 의미(이전 행/현재 행) 헷갈리지 말 것. 합 기반 공식은 prefix sum 필요.
- 🔑 키워드: "양 끝에서만 가져감", "두 플레이어 최적(minimax)" → interval DP + 점수 차이(zero-sum) 트릭. (LeetCode Predict the Winner / Stone Game)

---

## 41. Longest Palindromic Subsequence

**문제:** 문자열 s에서 **최장 팰린드롬 부분 수열**(문자 건너뛰기 가능, 순서 유지) 길이. 예: `"bbbab"` → 4 ("bbbb"), `"abacb"` → 3 ("aba").

**핵심 관찰:** `dp[l][r]` = s[l..r] 안의 LPS 길이.
- 양끝이 같으면 둘 다 쓰고 안쪽: `dp[l+1][r-1] + 2`.
- 다르면 한쪽을 버림: `max(dp[l+1][r], dp[l][r-1])`.
- (동치: s와 reverse(s)의 **LCS**)

**풀이**
1. `dp[i][i] = 1`.
2. l을 n−1 → 0, r을 l+1 → n−1 순으로 채움 (내부·아래·왼쪽 셀이 먼저 준비됨).

```python
def longest_palindromic_subsequence(s: str) -> int:
    n = len(s)
    dp = [0] * n                 # dp[r] ≡ dp[l+1][r] (이전 행)
    for l in range(n - 1, -1, -1):
        new = [0] * n
        new[l] = 1
        for r in range(l + 1, n):
            if s[l] == s[r]:
                new[r] = dp[r - 1] + 2          # dp[l+1][r-1] (+ l+1>r-1이면 0)
            else:
                new[r] = max(dp[r], new[r - 1]) # dp[l+1][r], dp[l][r-1]
        dp = new
    return dp[n - 1]
```

**복잡도:** 시간 O(n²) = 10⁶, 공간 O(n) (2D면 O(n²)).

- 🐛 함정: substring(39번, 연속)과 혼동 금지 — 여기선 불일치 시 **max(한쪽 버림)**. 길이 2 구간에서 `dp[l+1][r-1]`이 빈 구간 → 0이어야 함 (위 코드에서 이전 행 `dp[l]`은 0이라 OK).
- 🔑 키워드: "부분 수열 + 팰린드롬", "최소 삭제로 팰린드롬 만들기"(= n − LPS) → `dp[l][r]` shrink형 interval DP.

---

## 42. Topological Sort DP Introduction
> 섹션: Topological Sort

**핵심 개념:** 상태 의존 관계가 **DAG**(사이클 없는 방향 그래프)이면, 위상 순서대로 처리할 때 각 노드에 도달하는 순간 그 노드가 의존하는 값이 이미 다 계산돼 있다 → DP 가능. 사실 **모든 DP는 상태 DAG 위의 계산**이고, 이 패턴은 그 DAG가 그래프/행렬로 드러나 있는 경우.

### 언제 쓰나
- 명시적 DAG가 주어짐 (선후 관계, 작업 의존성).
- **암묵적 DAG:** "엄격히 증가/감소하는 값으로만 이동", "문자 하나를 추가해서만 다음 단어" 등 **순서 제약이 사이클을 원천 차단**.
- 각 노드의 값이 선행/후행 노드 값의 조합으로 정해짐.

### 템플릿 — DAG 최장 경로
```
for v in topological_order:
    dp[v] = max(dp[u] + 1 for u in preds(v)), 선행자 없으면 0
answer = max(dp)
```

### 두 가지 구현
| | Memoized DFS (top-down) | 명시적 위상 정렬 (Kahn, bottom-up) |
|---|---|---|
| 적합 | 암묵적 그래프(행렬, 문자열) | 명시적 그래프, 모든 노드 처리 |
| 순서 | 재귀가 자동으로 해결 | indegree 0부터 큐 |
| 장점 | 구현 간단, 도달 가능한 상태만 | 재귀 깊이 문제 없음, 캐시 친화 |

```python
from functools import cache
def longest_increasing_path(M):
    R, C = len(M), len(M[0])
    @cache
    def dfs(r, c):                         # (r,c)에서 시작하는 최장 경로
        best = 1
        for nr, nc in ((r+1,c),(r-1,c),(r,c+1),(r,c-1)):
            if 0 <= nr < R and 0 <= nc < C and M[nr][nc] > M[r][c]:
                best = max(best, 1 + dfs(nr, nc))
        return best
    return max(dfs(r, c) for r in range(R) for c in range(C))
```
- **복잡도:** O(V + E). 행렬이면 O(R·C).
- 🐛 흔한 실수: 사이클이 있는 그래프(비엄격 ≥ 조건)에 memo DFS → 무한 재귀/오답. visited 체크를 일반 DFS처럼 넣으면 memo와 충돌.
- 🎤 "왜 memo가 맞나?" → 엄격한 순서 제약이 DAG를 보장 → 하위 문제 답이 경로에 무관하게 고정.

---

## 43. Longest Increasing Path in a Matrix

**문제:** m×n 행렬에서 상하좌우로만 이동하며 **엄격히 증가**하는 최장 경로의 셀 수. 예: `[[9,9,4],[6,6,8],[2,1,1]]` → 4 (1→2→6→9).

**핵심 관찰:** "작은 값 → 큰 값" 간선만 존재 → 엄격 증가라 **사이클 불가 = 암묵적 DAG**. 셀 값 오름차순 자체가 위상 순서.

**풀이 A (memo DFS):** 42번 템플릿 그대로 — `dfs(r,c)` = (r,c)에서 시작하는 최장 경로 = 1 + max(더 큰 이웃의 dfs).

**풀이 B (값 정렬 bottom-up — 재귀 깊이 걱정 없음)**
1. 모든 셀을 값 **내림차순**으로 정렬.
2. 순서대로 `dp[r][c] = 1 + max(dp[이웃] for 이웃 값 > 현재)` — 더 큰 이웃은 이미 계산됨.

```python
def longest_increasing_path_in_a_matrix(matrix: list[list[int]]) -> int:
    R, C = len(matrix), len(matrix[0])
    dp = [[1] * C for _ in range(R)]
    cells = sorted(((matrix[r][c], r, c) for r in range(R) for c in range(C)), reverse=True)
    for v, r, c in cells:
        for nr, nc in ((r+1,c),(r-1,c),(r,c+1),(r,c-1)):
            if 0 <= nr < R and 0 <= nc < C and matrix[nr][nc] > v:
                dp[r][c] = max(dp[r][c], dp[nr][nc] + 1)
    return max(map(max, dp))
```

**복잡도:** memo DFS O(R·C), 정렬 버전 O(R·C·log(R·C)). 공간 O(R·C).

- 🐛 함정: 같은 값으로 이동 금지(`>` 엄격). Python memo DFS는 최악 경로 길이 40000 → `sys.setrecursionlimit` 필요 → 정렬/Kahn(indegree = 더 작은 이웃 수, BFS 층 수 = 답) 방식이 안전. visited 배열 불필요.
- 🔑 키워드: "엄격히 증가하는 경로", "그리드에서 최장 경로" → 암묵적 DAG + memo DFS / 값 정렬 DP.

---

## 44. Longest String Chain

**문제:** 단어 목록에서 각 단어가 이전 단어에 **글자 하나를 아무 위치에 삽입**해 만들어지는 최장 체인 길이. 예: `["a","b","ba","bca","bda","bdca"]` → 4 (a→ba→bda→bdca).

**핵심 관찰:** 간선 A→B는 len(B) = len(A)+1일 때만 → 길이가 엄격히 증가 = **DAG**, 위상 순서 = **길이 오름차순**. 후속자를 찾는 것(26×17 삽입)보다 **선행자를 찾는 것(한 글자 삭제, ≤16가지)** 이 싸다.

**풀이**
1. 단어를 길이순 정렬.
2. 각 단어 w에 대해 모든 i에서 `w[:i] + w[i+1:]`(한 글자 삭제)를 만들고, 해시맵 dp에 있으면 `dp[w] = max(dp[w], dp[pred] + 1)`.
3. 최대 dp 반환.

```python
def longest_string_chain(words: list[str]) -> int:
    dp = {}
    best = 0
    for w in sorted(set(words), key=len):
        cur = 1
        for i in range(len(w)):
            pred = w[:i] + w[i + 1:]
            if pred in dp:
                cur = max(cur, dp[pred] + 1)
        dp[w] = cur
        best = max(best, cur)
    return best
```

**복잡도:** 시간 O(n log n + n·L²) (L ≤ 16, 슬라이싱 O(L)), 공간 O(n·L).

- 🐛 함정: 입력 순서는 무의미 → 반드시 길이 정렬. 모든 쌍 비교(O(n²·L))도 n=1000이면 통과하지만 삭제 기반 해시가 깔끔. 중복 단어는 set으로.
- 🔑 키워드: "한 글자 추가/삭제로 이어지는 체인", "단어 사다리형 최장" → 길이 = 위상 순서, 해시맵 DP (LIS on DAG).

---

## 45. Tree DP Introduction
> 섹션: Trees

**핵심 개념:** 인덱스 대신 **노드가 상태**. `dp[v]` = v를 루트로 하는 **서브트리만** 고려한 답. 상태 n개, 각 노드 한 번 방문 → 보통 O(n).

### 언제 쓰나
- 입력이 트리(연결 + 사이클 없음).
- 경로/서브트리/노드 값에 대한 최적화·카운팅이고, **노드의 답이 자식들의 답에서 결정**됨.
- 대표: 최장 경로, 트리 지름, 서브트리 합, House Robber III, 최대 독립 집합.

### 처리 순서 = Post-order DFS
- 자식을 먼저 계산 → 부모에서 합침. 재귀 구조가 자동으로 순서를 보장 (재귀 반환 = 자식 완료).
- Base: 리프 (예: 리프에서 아래로 가는 최장 경로 = 0).

### 템플릿 (인접 리스트 트리, parent로 역방향 차단)
```python
def longest_down_path(graph, node, parent=-1):
    best = 0
    for child in graph[node]:
        if child != parent:                       # 무방향 트리에서 부모로 돌아가기 방지
            best = max(best, longest_down_path(graph, child, node) + 1)
    return best                                   # dp[v] = max(dp[child]) + 1
```

### 전이 유형
| 유형 | 설명 | 예 |
|---|---|---|
| Aggregate | 모든 자식 정보를 합침 | 서브트리 크기(Σ+1), 서브트리 합, 최대 깊이 |
| Selection | 최적의 자식/자식 부분집합 선택 | 독립 집합(나를 고르면 자식 불가), 매칭 |

### 다중 값 DP (노드당 튜플 반환)
- **지름:** (v에서 아래로 최장 경로, v를 지나는 최장 경로 = 가장 긴 자식 두 개의 합). 전역 최대값 갱신.
- **독립 집합:** (v 포함 시 최선, v 제외 시 최선).
- 🐛 흔한 실수: 무방향 인접 리스트에서 parent 체크 누락 → 무한 재귀. 반환값(부모에게 올려줄 값)과 전역 답(여기서 끝나는 경로)을 혼동.
- 🎤 "트리 DP 복잡도?" → 노드마다 자식 수만큼 일 → Σdeg = O(n). Python은 깊은 트리에서 재귀 한도 주의(반복 post-order로 전환).

---

## 46. House Robber III

**문제:** 집이 이진 트리 구조. 부모-자식(직접 연결)을 같은 밤에 둘 다 털 수 없음. 최대 금액. 예: `[3,2,3,null,3,null,1]` → 7 (루트 3 + 손자 3+1). `[3,4,5,1,3,null,1]` → 9 (루트 건너뛰고 4+5).

**핵심 관찰:** 트리 위의 **최대 가중 독립 집합**. 노드마다 두 값을 올려보냄: `(rob, skip)`.
- `rob(v) = v.val + skip(L) + skip(R)` (나를 털면 자식은 못 텀)
- `skip(v) = max(rob(L), skip(L)) + max(rob(R), skip(R))` (나를 건너뛰면 자식은 자유)

**풀이**
1. post-order DFS로 각 노드가 `(rob, skip)` 튜플 반환. None → (0, 0).
2. 답 = `max(dfs(root))`.

```python
def house_robber_iii(root) -> int:
    def dfs(node):
        if node is None:
            return 0, 0                         # (rob, skip)
        lr, ls = dfs(node.left)
        rr, rs = dfs(node.right)
        rob = node.val + ls + rs
        skip = max(lr, ls) + max(rr, rs)
        return rob, skip
    return max(dfs(root))
```

**복잡도:** 시간 O(n), 공간 O(h) (재귀 스택; 편향 트리면 O(n) → 10⁴ 깊이는 Python 기본 한도 1000 초과 가능 → `sys.setrecursionlimit` 또는 반복 post-order).

- 🐛 함정: "루트를 털면 손자들까지" 식의 naive 재귀는 memo 없으면 지수 시간. 레벨별 합을 번갈아 고르는 그리디(짝수 레벨 vs 홀수 레벨)는 오답 — 예시 2처럼 서브트리마다 선택이 다름. skip일 때 자식은 **반드시 털어야 하는 게 아니라 max**.
- 🔑 키워드: "트리에서 인접한(부모-자식) 노드 동시 선택 불가" → 노드별 (take, skip) 쌍 반환 Tree DP.

---

## 47. Bitmask Introduction
> 섹션: Bitmask

**핵심 개념:** 정수 하나로 **불리언 집합**을 표현 — 비트 i가 1이면 원소 i 포함. `[T,F,T,F]` → `0b0101 = 5`. 백트래킹의 `used[]` 배열을 memo 키로 쓰려면 문자열/튜플 변환이 필요하지만, 비트마스크는 **그 자체가 해시 가능한 int** → DP 상태로 딱.
- 면접 우선순위: **낮음**(고급 기법). 하지만 n ≤ 20 + "모든 원소 방문/배정"이면 바로 떠올려야 함. (펌웨어에서 레지스터 비트 조작과 동일한 연산이라 Don에겐 익숙한 도구)

### 비트 연산 치트시트
| 목적 | 코드 |
|---|---|
| i번 비트 검사 | `mask & (1 << i)` (0이 아니면 포함) |
| i번 세팅(추가) | `mask \| (1 << i)` |
| i번 클리어(제거) | `mask & ~(1 << i)` |
| i번 토글 | `mask ^ (1 << i)` |
| 원소 개수 | `mask.bit_count()` (3.10+) / `bin(mask).count('1')` |
| 전체 집합 | `(1 << n) - 1` |
| 최하위 1비트 | `mask & -mask` |

### 모든 부분집합 열거
```python
nums = [1, 2, 3]; n = len(nums)
for mask in range(1 << n):                 # 0 .. 2^n - 1
    subset = [nums[i] for i in range(n) if mask >> i & 1]
# 부분집합의 부분집합: sub = mask; while sub: ...; sub = (sub - 1) & mask   → 총 O(3^n)
```

### 언제 bitmask DP?
- 상태에 **"지금까지 고른/방문한 원소 집합"** 이 필요.
- **n ≤ ~20** (2²⁰ ≈ 10⁶ 상태).
- 유형: TSP(모든 도시 방문 최단), 할당 문제(n 작업 ↔ n 사람), 순서가 중요한 부분집합.
- 🐛 흔한 실수: 연산자 우선순위 — Python에서 `mask & 1 << i`는 `mask & (1 << i)`로 해석되지만 `==`와 섞이면 틀리기 쉬움 → 괄호 필수. `~`는 Python에서 음수 무한 비트 → 클리어 용도로만.
- 🎤 "bitmask 상태 수?" → 2ⁿ × (추가 차원). n=20이면 2ⁿ·n = 2·10⁷.

---

## 48. Bitmask DP

**핵심 개념:** 상태의 일부가 비트마스크인 DP. `dp[mask][extra]` = mask에 있는 원소들을 이미 선택/방문했고, 부가 상태가 extra일 때의 답.
- **왜 압축이 되나:** 같은 집합을 방문하고 같은 곳에 서 있는 두 경로는 **남은 선택이 완전히 동일** → 비용이 더 좋은 쪽만 기억하면 됨. 방문 **순서 이력**을 버리고 **집합 + 작은 부가 변수**만 유지.
- n=15: 순열 15! ≈ 1.3·10¹² vs 상태 2¹⁵·15 ≈ 5·10⁵.

### 흔한 상태 모양
| 문제 유형 | 상태 | 비고 |
|---|---|---|
| 경로(TSP 계열) | `dp[mask][pos]` | pos = 현재 노드 |
| 할당/매칭 | `dp[mask]` | 다음 사람 = `popcount(mask)`로 유추 → extra 생략 |
| 그룹 분할 | `dp[mask]` | 서브마스크 순회 |

### 예: 최소 비용 할당 (n 작업자 ↔ n 작업)
```python
from functools import cache
def min_cost_assignment(cost):
    n = len(cost); FULL = (1 << n) - 1
    @cache
    def dp(mask):                          # mask = 이미 배정된 작업들
        if mask == FULL:
            return 0
        worker = mask.bit_count()          # 다음 작업자 인덱스
        return min(cost[worker][t] + dp(mask | 1 << t)
                   for t in range(n) if not mask >> t & 1)
    return dp(0)
```
- **복잡도:** 상태 2ⁿ × 전이 n → **O(2ⁿ·n)** 시간, O(2ⁿ) 공간. (`dp[mask][pos]`형은 O(2ⁿ·n²))

### 서브마스크 순회 (그룹 분할용)
```python
sub = mask
while sub:
    comp = mask ^ sub          # (sub, comp)로 분할 처리
    sub = (sub - 1) & mask
```
- 모든 mask에 대해 합치면 **O(3ⁿ)** (각 원소: mask 밖 / sub 안 / comp 안).

- 🔑 신호: "모든 도시를 한 번씩 방문", "각 작업자에게 작업 하나씩", "집합을 유효한 그룹으로 분할" + **n ≤ 20**.
- 🐛 흔한 실수: extra 차원을 빠뜨려 서로 다른 미래를 가진 상태를 합쳐버림 (TSP에서 pos 누락). mask == FULL 기저 조건과 시작 mask(보통 0 또는 시작 노드 비트) 혼동.

---

## 49. Minimum Cost to Visit Every Node

**문제:** 방향 가중 그래프 인접 행렬(`0` = 간선 없음). 노드 0에서 출발해 **모든 노드를 정확히 한 번씩** 방문하는 최소 비용 (복귀 불필요, 해밀턴 경로). 불가능하면 −1. 예: `[[0,0,1],[0,0,0],[0,2,0]]` → 3 (0→2→1). n ≤ 15.

**핵심 관찰:** 경로형 bitmask DP (open TSP). 상태 = (방문 집합 mask, 현재 노드 pos). 미래 비용은 이 둘에만 의존. 매 단계 가장 싼 간선 그리디는 실패(예시 2).

**풀이 (top-down)**
1. `dp(mask, pos)` = mask를 방문했고 pos에 있을 때, **나머지를 모두 방문하는 최소 추가 비용**.
2. mask == FULL → 0.
3. 방문 안 한 v 중 `graph[pos][v] > 0`인 것에 대해 `graph[pos][v] + dp(mask | 1<<v, v)`의 최소.
4. 답 = `dp(1, 0)`; INF면 −1.

```python
from functools import cache
def min_cost_to_visit_every_node(graph: list[list[int]]) -> int:
    n = len(graph); FULL = (1 << n) - 1; INF = float('inf')
    @cache
    def dp(mask, pos):
        if mask == FULL:
            return 0
        best = INF
        for v in range(n):
            w = graph[pos][v]
            if w and not mask >> v & 1:
                best = min(best, w + dp(mask | 1 << v, v))
        return best
    ans = dp(1, 0)                      # 노드 0 방문 상태로 시작
    return -1 if ans == INF else ans
```

**복잡도:** 상태 2ⁿ·n, 전이 n → **O(2ⁿ·n²)** ≈ 32768·225 ≈ 7·10⁶, 공간 O(2ⁿ·n).

- 🐛 함정: `0`은 "간선 없음"(가중치 0 간선 아님). 시작 mask는 0이 아니라 `1`(노드 0 방문). n=1이면 0 반환. 도달 불가 시 INF 전파 → −1. 무방향 "재방문 허용" 버전(LeetCode 847)은 BFS over (mask, pos)로 다름.
- 🔑 키워드: "모든 노드를 정확히 한 번 방문", "최단 해밀턴 경로/TSP", n ≤ 15~20 → `dp[mask][pos]`.

---

## 50. DP Practice List
> 섹션: Additional Practice

**핵심:** "The Monster DP List" — DP 문제를 **상태 모양(state shape)** 별 9개 그룹 + 기타로 묶은 연습 목록. 그룹 이름 자체가 "이 문제는 dp를 무엇 위에 정의하나"라는 분류 체계다.

| 그룹 | dp가 정의되는 대상 | 연습 문제 |
|---|---|---|
| 1. Warmup | 1D 기초 | Climbing Stairs, N-th Tribonacci, Perfect Squares |
| 2. Constant Transition | 모든 prefix, 전이 O(1) | Min Cost Climbing Stairs, House Robber, Decode Ways, Min Cost For Tickets, Solving Questions With Brainpower |
| 3. Grid | 그리드와 같은 크기 테이블, 부분 그리드 | Unique Paths I/II, Minimum Path Sum, Count Square Submatrices, Maximal Square, Dungeon Game |
| 4. Dual-sequence | `dp[i][j]` = 두 시퀀스 prefix 쌍 | LCS, Uncrossed Lines, Min ASCII Delete Sum, Edit Distance, Distinct Subsequences, Shortest Common Supersequence |
| 5. Interval | 모든 구간 [l, r] | Longest Palindromic Subseq, Stone Game VII, Palindromic Substrings, Min Cost Tree From Leaf Values, Burst Balloons, Strange Printer |
| 6. Non-constant Transition | prefix지만 j < i 전부 훑는 O(n) 전이 | Count Number of Teams, LIS, Partition Array for Max Sum, Largest Sum of Averages, Filling Bookcase Shelves |
| 7. Knapsack | (아이템, 용량/합) | Partition Equal Subset Sum, Dice Rolls With Target Sum, Combination Sum IV, Ones and Zeroes, Coin Change I/II, Target Sum, Last Stone Weight II, Profitable Schemes |
| 8. Topological Sort (선택) | DAG 노드 | Longest Increasing Path in Matrix, Longest String Chain, Course Schedule III |
| 9. Trees (선택) | 서브트리 | House Robber III, Binary Tree Cameras |
| 기타 | 위 분류 밖 | 2 Keys Keyboard, Word Break, Min Removals to Make Mountain Array, Out of Boundary Paths |

### 복습 전략
- ✅ 그룹 2·3·4·7이 면접 빈도 최상위 → 먼저 끝내기. 8·9는 advanced/optional, bitmask는 여유 있을 때.
- ✅ 문제를 풀기 전에 **"dp[?]의 의미를 한 문장으로"** 쓰고, 어느 그룹 모양인지 먼저 분류하는 습관.
- 🔑 Knapsack 그룹 체크: Combination Sum IV = **순열** 카운팅(바깥 루프 amount), Ones and Zeroes = **2차원 용량** 0/1 knapsack, Last Stone Weight II = Partition의 **차이 최소** 변형, Profitable Schemes = 인원 용량 + 이익 하한 3D knapsack.
- 🎤 "DP 문제를 처음 보면?" → ① 상태 모양 분류(prefix/grid/두 시퀀스/구간/용량/DAG/트리/마스크) ② dp 의미 정의 ③ 전이·기저 ④ 채우는 순서 ⑤ 공간 압축.

---

## 용어·패턴 사전

| 용어 | 뜻 |
|---|---|
| Optimal substructure | 최적해가 부분 문제 최적해의 조합으로 만들어지는 성질 → 점화식이 유효 |
| Overlapping sub-problems | 같은 부분 문제가 반복 등장하는 성질 → 저장(memo)할 가치 |
| Recurrence relation (점화식) | 문제의 답을 더 작은 같은 문제의 답으로 표현한 식 |
| Top-down / Bottom-up | 재귀 + memo(순서 자동) / 표를 작은 것부터 채움(순서 명시, 공간 최적화 쉬움) |
| Rolling array | 필요한 이전 행/값만 유지해 공간을 O(n)·O(1)로 줄이는 기법. 루프 방향이 정확성을 결정 |
| Constant vs Non-constant transition | 고정 개수 이전 상태 참조(O(1)/상태) vs 조건 맞는 모든 j<i 참조(O(n)/상태) |
| Backtracking reconstruction (역추적) | 완성된 DP 표를 끝에서 거꾸로 따라가 실제 해(문자열/분할)를 복원. 전체 표 필요 |
| Subsequence vs Substring | 순서만 유지(비연속) vs 연속 구간 |
| Patience sorting (tails) | LIS용 `tails[k]` = 길이 k+1의 최소 끝값, 정렬 유지 → 이진 탐색 O(n log n) |
| Win/Lose state (게임 DP) | 상대를 L로 보내는 수가 하나라도 있으면 W, 모든 수가 W로 가면 L |
| 0/1 Knapsack | 각 아이템 최대 1회 선택; 1D 압축 시 용량 역순 순회 |
| Unbounded Knapsack | 아이템 종류 무제한 사용; include가 같은 행 참조, 1D 정순 순회 |
| Bounded Knapsack | 아이템별 최대 q개; 이진 분해로 0/1 환원 |
| Binary decomposition | q를 1,2,4,…,나머지로 쪼개 0..q 모든 개수를 부분집합으로 표현 |
| Pseudo-polynomial | 입력 크기가 아니라 용량 "값"에 비례하는 시간 (O(n·C)) |
| Interval DP | dp[l][r]로 구간 답을 정의, 짧은 구간→긴 구간 순으로 채움 |
| Expand around center | 2n−1개 중심에서 양쪽 확장해 팰린드롬 탐색, O(1) 공간 |
| Implicit DAG | 엄격한 순서 제약(값 증가, 길이 증가)으로 사이클이 없는 암묵적 그래프 |
| Post-order DFS | 자식 먼저 계산 후 부모에서 합치는 순회 = Tree DP의 fill 순서 |
| Bitmask / Submask enumeration | 정수 비트로 집합 표현; `sub=(sub-1)&mask`로 부분마스크 순회, 전체 O(3ⁿ) |
