---
id: ALGO-M08
time: 90분
updated: 2026-11-11
---
# ALGO-M08 동적 계획법 2D: 편집 거리·LCS·0/1 배낭

## 학습 목표

- 2차원 DP 세 가지(편집 거리, LCS, 0/1 배낭)를 **상태 → 전이 → 기저 → 계산 순서**로 말하고, 각 점화식이 왜 맞는지 증명(마지막 열/마지막 아이템 논증)까지 한다.
- 표를 거꾸로 따라가는 **역추적(traceback)** 으로 답의 "값"뿐 아니라 "내용"(편집 스크립트, LCS 문자열, 선택한 모듈 목록)을 출력한다.
- 공간을 O(n·m) → O(min(n,m)), O(n·W) → O(W) 로 줄이고, 1D 배낭에서 **w 를 내려가며 도는 이유**(올라가면 무제한 배낭이 됨)를 불변식으로 설명한다. 1D 인데도 역추적하려면 결정 비트셋(n·W 비트)을 쓴다.
- 펌웨어 상황에 적용한다: 키를 아는 설정 파일 diff, 섹터 정렬된 Flash 예산 안의 모듈 선택. Cortex-M 에서 표 크기·스택 사용량을 숫자로 말한다.

## 1. 문제

"두 설정 파일이 얼마나 다른가 / 어떤 기능 모듈을 이미지에 넣을까" — 2D DP 는 펌웨어 코딩 면접에서 **표를 그리고, 점화식을 말하고, 역추적까지 짜고, 메모리를 줄이는** 네 단계를 45분 안에 보여 주는 단골 문제다.

1D DP(ALGO-M07)는 상태가 숫자 하나(i)였다. 이 레슨은 상태가 **두 개**다: 편집 거리·LCS 는 (a 의 앞 i 글자, b 의 앞 j 글자), 배낭은 (앞 i 개 아이템, 용량 w). 표의 한 칸은 "더 작은 부분 문제 몇 개"에만 의존하고, 그 의존 방향이 계산 순서와 공간 축소 방법을 정한다.

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m8a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">한 칸이 어디에 의존하나 (화살표 = "이 값을 읽는다")</text>
  <text x="20" y="50" fill="currentColor">편집 거리 D[i][j]</text>
  <g stroke="currentColor">
    <rect x="40" y="70" width="60" height="40" class="f-mut"/><rect x="100" y="70" width="60" height="40" class="f-mut"/>
    <rect x="40" y="110" width="60" height="40" class="f-mut"/><rect x="100" y="110" width="60" height="40" class="f-acc"/>
  </g>
  <text x="50" y="94" fill="currentColor">i-1,j-1</text><text x="115" y="94" fill="currentColor">i-1,j</text>
  <text x="55" y="134" fill="currentColor">i,j-1</text><text x="122" y="134" fill="currentColor">i,j</text>
  <g stroke="currentColor" fill="none" marker-end="url(#m8a)">
    <path d="M130 120 L130 102"/><path d="M110 130 L92 130"/><path d="M110 118 L92 104"/>
  </g>
  <text x="20" y="175" fill="currentColor">min(대각 + [다름],</text>
  <text x="20" y="193" fill="currentColor">    위 + 1, 왼쪽 + 1)</text>
  <text x="20" y="215" fill="currentColor">위 = 삭제, 왼쪽 = 삽입</text>
  <text x="270" y="50" fill="currentColor">LCS L[i][j]</text>
  <g stroke="currentColor">
    <rect x="290" y="70" width="60" height="40" class="f-mut"/><rect x="350" y="70" width="60" height="40" class="f-mut"/>
    <rect x="290" y="110" width="60" height="40" class="f-mut"/><rect x="350" y="110" width="60" height="40" class="f-ok"/>
  </g>
  <text x="300" y="94" fill="currentColor">i-1,j-1</text><text x="365" y="94" fill="currentColor">i-1,j</text>
  <text x="305" y="134" fill="currentColor">i,j-1</text><text x="372" y="134" fill="currentColor">i,j</text>
  <g stroke="currentColor" fill="none" marker-end="url(#m8a)">
    <path d="M380 120 L380 102"/><path d="M360 130 L342 130"/><path d="M360 118 L342 104"/>
  </g>
  <text x="270" y="175" fill="currentColor">같으면 대각 + 1</text>
  <text x="270" y="193" fill="currentColor">다르면 max(위, 왼쪽)</text>
  <text x="270" y="215" fill="currentColor">값은 클수록 좋다</text>
  <text x="500" y="50" fill="currentColor">0/1 배낭 K[i][w]</text>
  <g stroke="currentColor">
    <rect x="500" y="70" width="40" height="40" class="f-mut"/><rect x="540" y="70" width="40" height="40" class="f-mut"/>
    <rect x="580" y="70" width="40" height="40" class="f-mut"/><rect x="620" y="70" width="40" height="40" class="f-mut"/>
    <rect x="660" y="70" width="40" height="40" class="f-mut"/>
    <rect x="500" y="110" width="40" height="40" class="f-mut"/><rect x="540" y="110" width="40" height="40" class="f-mut"/>
    <rect x="580" y="110" width="40" height="40" class="f-mut"/><rect x="620" y="110" width="40" height="40" class="f-mut"/>
    <rect x="660" y="110" width="40" height="40" class="f-warn"/>
  </g>
  <text x="508" y="94" fill="currentColor">w-s</text><text x="668" y="94" fill="currentColor">w</text>
  <text x="474" y="94" fill="currentColor">i-1</text><text x="486" y="134" fill="currentColor">i</text>
  <g stroke="currentColor" fill="none" marker-end="url(#m8a)">
    <path d="M680 120 L680 102"/><path d="M670 118 L530 102"/>
  </g>
  <text x="500" y="175" fill="currentColor">max(위 = 안 넣음,</text>
  <text x="500" y="193" fill="currentColor">    위 줄 w−s + v = 넣음)</text>
  <text x="500" y="215" fill="currentColor">같은 줄은 읽지 않는다!</text>
  <text x="10" y="250" fill="currentColor">→ 편집 거리·LCS 는 같은 줄 왼쪽도 읽으므로 한 줄로 줄이면 대각값을 따로 보관해야 하고,</text>
  <text x="10" y="266" fill="currentColor">  배낭은 위 줄만 읽으므로 한 줄로 줄일 때 w 를 내려가며 돌면 된다 (Part B).</text>
</svg>
```

### Part A — 2D 표와 역추적: 편집 거리, LCS

- `ed_table(a, n, b, m)` — Levenshtein 거리(삽입·삭제·치환 각 비용 1) 표 `g_ed[i][j]` 를 채우고 `D[n][m]` 을 돌려준다.
- `ed_script(a, n, b, m, ops)` — 표를 (n, m) 에서 (0, 0) 으로 거슬러 **편집 스크립트**(`=` 같음, `~` 치환, `-` 삭제, `+` 삽입)를 앞에서부터의 순서로 만든다. 동점이면 대각 → 삭제 → 삽입 순.
- `lcs_table`, `lcs_string` — 최장 공통 부분수열(LCS, longest common subsequence) 길이 표와, LCS 문자열 하나.
- 테스트: kitten/sitting = 3, LeetCode 72 예(horse/ros = 3, intention/execution = 5), LeetCode 1143 예, CLRS 예(ABCBDAB/BDCABA → 4). {a, b} 위 길이 ≤ 5 문자열 **모든 쌍**(63 × 63)을 문자열 그래프 BFS(편집 연산의 정의 그대로)와 대조, 정렬 정의 재귀(표 없음)·부분수열 2^n 전수와 랜덤 대조, 스크립트는 "적용하면 정말 b 가 되고 비용 = 거리" 인지 검사.

### Part B — 공간 축소와 0/1 배낭

- `ed_1row` — 배열 한 줄(짧은 쪽 길이 + 1) + 대각 임시 변수 하나로 거리만 구한다.
- `knap_2d(n, size, value, W, chosen)` — K[i][w] 표 + 선택 역추적. `knap_1d` — dp[w] 한 줄, **w 를 내려가며**.
- 성질 검사: 대칭, 삼각 부등식, 그리고 편집 거리와 LCS 를 잇는 부등식 max(n,m) − L ≤ d ≤ min(max(n,m), n + m − 2L) (5. 풀이에서 증명).
- 배낭 테스트: 고전 예(60/100/120 → 220), Rosetta Code 22개 아이템(W = 400 → 1030), n = 3 인 작은 인스턴스 **32768개 전수**, 랜덤 n ≤ 15 를 부분집합 2^n 전수와, n = 32 를 2D ↔ 1D 끼리.

### Part C — 설정 diff 와 Flash 예산

- `cfg_diff(old, n, new, m, key_aware, ops, &nops)` — `key=value` 줄 단위 편집 거리. key_aware 면 **같은 키끼리만 치환**(값 변경)할 수 있고, 다른 키는 삭제 + 삽입. 키를 모르는 단위 비용 diff 와 출력 비교.
- `flash_select(mods, n, budget, gran, chosen, &used)` — 128 KiB 여유 Flash 에 모듈을 골라 넣는다. 모듈은 gran 바이트(지우기 섹터) 경계에 정렬 → 크기를 ceil(size/gran) 단위로. 1D dp + **결정 비트셋**으로 역추적. 섹터 4096 / 256 / 1 바이트에서 결과 비교, 부분집합 전수와 대조.
- 측정(`demo_bench.c`): 정의 재귀 호출 수, 2D 표 vs 한 줄 ns/칸·메모리, 배낭 2D vs 1D vs 1D+비트. Cortex-M4/M0 스택 사용량과 안쪽 루프(`demo_target.c`).

## 2. Problem (English)

```c
int      ed_table  (const char *a, int n, const char *b, int m);           /* Levenshtein distance, fills g_ed */
int      ed_script (const char *a, int n, const char *b, int m, Op *ops);  /* returns #ops; ops in forward order */
int      lcs_string(const char *a, int n, const char *b, int m, char *out);/* returns |LCS|, writes one LCS */
int      ed_1row   (const char *a, int n, const char *b, int m);           /* O(min(n,m)) extra memory */
uint32_t knap_2d(int n, const uint32_t *size, const uint32_t *value, uint32_t W, uint8_t *chosen);
uint32_t knap_1d(int n, const uint32_t *size, const uint32_t *value, uint32_t W);   /* O(W) memory */
int      cfg_diff(const char *const *old, int n, const char *const *nw, int m, bool key_aware, Op *ops, int *nops);
uint32_t flash_select(const Module *mods, int n, uint32_t budget, uint32_t gran, uint8_t *chosen, uint32_t *used);
```

Constraints: strings ≤ 64 chars, ≤ 32 config lines, ≤ 32 modules, W ≤ 2048 for the 2-D table and ≤ 131072 units for the 1-D version. No malloc — static tables sized by `MAX_*`. Edit costs are 1 (insert/delete/substitute); in `cfg_diff` with `key_aware`, substitution is allowed only between lines with the same key.

```text
ed("horse", "ros") = 3        script: ~h→r  =o  -r  =s  -e
lcs("abcde", "ace") = 3, "ace"
knap(size {10,20,30}, value {60,100,120}, W = 50) = 220, chosen {1, 2}
cfg_diff(uart.baud=9600 → uart.baud=115200, ...) = "~ uart.baud=9600 -> uart.baud=115200"
```

Follow-ups: Prove the recurrence. Reconstruct the answer in O(W) memory. Why must the 1-D knapsack loop run downwards? Is O(n·W) polynomial? How much RAM does this need on a Cortex-M4 with 64 KB?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 아홉 군데(Part A 4, Part B 3, Part C 2). 기준 구현(BFS, 정의 재귀, 부분수열·부분집합 전수), 스크립트 검증기, 출력 함수는 주어진다. 스텁은 0 이나 빈 결과를 돌려주므로 멈추거나 죽지 않는다. 처음 실행 (실제 출력, 중간 생략):

```text
FAIL starter.c:326  ed_table("kitten", 6, "sitting", 7) == 3
…
exhaustive {a,b} len<=5: 3969 pairs vs BFS, bad ed=3906 script=3968 lcs=3794 lcs_str=0 1row=3906
…
knapsack exhaustive n=3: 32768 instances, bad 2d=28908 pick=28908 1d=28908
…
flash_select random: 300 instances (n<=12, gran 1/64/256/4096) vs brute bad=256
FAIL starter.c:557  bad == 0
10/51 passed
```

(exit 1. 통과한 10개는 스텁의 0 이 우연히 맞는 경우다 — 빈 LCS 는 언제나 공통 부분수열이고(`lcs_str=0`), 스텁끼리 0 = 0 으로 맞는 비교(한 줄 vs 표, 2D vs 1D, 삼각 부등식), 답이 0 인 배낭 경계(용량 0, 아이템 없음, 전부 너무 큼), 기준 구현만 쓰는 Rosetta 전수 검사.)

## 4. 힌트

<details><summary>힌트 1 — 상태를 "접두사(prefix)"로 잡는다</summary>

D[i][j] = a 의 앞 i 글자를 b 의 앞 j 글자로 바꾸는 최소 비용. 표 크기는 (n+1) × (m+1) — 빈 접두사(0행·0열)가 기저다. D[i][0] = i (전부 삭제), D[0][j] = j (전부 삽입). 0 으로 두면 틀린다 (7. 실수 2).

</details>

<details><summary>힌트 2 — 정렬의 "마지막 열"을 본다</summary>

a[0..i) 와 b[0..j) 를 위아래로 맞춰 쓴 정렬(alignment)의 마지막 열은 셋 중 하나다: (a[i-1], b[j-1]) 맞춤, (a[i-1], 빈칸) 삭제, (빈칸, b[j-1]) 삽입. 마지막 열을 떼면 더 짧은 접두사의 정렬이 남는다.

</details>

<details><summary>힌트 3 — 역추적은 "이 칸 값이 어느 이웃에서 왔나"를 다시 계산</summary>

선택을 따로 저장할 필요 없다. (i, j) 에서 `D[i][j] == D[i-1][j-1] + [다름]` 이면 대각, 아니면 `== D[i-1][j] + 1` 이면 위(삭제), 둘 다 아니면 왼쪽(삽입). 거꾸로 모은 뒤 뒤집는다. 배낭은 `K[i][w] != K[i-1][w]` 이면 아이템 i-1 을 넣은 것.

</details>

<details><summary>힌트 4 — 1D 배낭: dp[w − s] 는 "아이템 i 를 넣기 전" 값이어야 한다</summary>

dp[w] 를 갱신할 때 읽는 dp[w − s] 는 w 보다 왼쪽이다. w 를 **큰 쪽부터** 돌면 왼쪽은 아직 이전 줄 값이다. 작은 쪽부터 돌면? 아이템 크기 2, 가치 3, W = 6 으로 손으로 돌려 보라.

</details>

<details><summary>힌트 5 — 1D 인데 무엇을 골랐는지 알려면</summary>

값 표 전체(n·W 개 uint32)는 버리고, "아이템 i 가 용량 w 에서 값을 올렸나" 한 비트만 n·W 개 저장한다. 끝에서 w = W 부터 i 를 거꾸로: 비트가 1 이면 선택, w −= s_i.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 이 문제에서 말할 것 |
|---|---|
| 1. 질문으로 범위 확인 | 비용은 모두 1인가(치환 = 2 인 변형도 있음), 문자열 길이 상한(표 크기), 답이 값만인지 스크립트도인지, 배낭에서 같은 아이템 여러 번 가능한지(0/1 vs 무제한), W 의 단위(바이트? 섹터?) |
| 2. 단순 해법 | 편집 거리: 정렬 정의 재귀 — 호출 수가 지수(아래 실측 n = 12 에서 3.8억 번). 배낭: 부분집합 2^n |
| 3. 개선 + 불변식 | 같은 부분 문제 (i, j) 가 반복된다 → 표. **상태·전이·기저·순서**를 한 문장씩. 정확성은 "마지막 열 / 마지막 아이템" 논증 |
| 4. 코드 | 표 채우기 → 역추적 → (질문 오면) 공간 축소 |
| 5. 소리 내어 말할 테스트 | 빈 문자열 양쪽, 같은 문자열(거리 0), 완전히 다른 문자열(거리 max), 용량 0, 아이템이 전부 너무 큼, 딱 맞는 용량, 크기 0 아이템 |
| 6. 복잡도 | 시간 O(n·m) / O(n·W). 메모리 2D O(n·m), 축소 O(min(n,m)) / O(W). 재귀 없음 → 스택 상수. W 는 **입력 길이가 아니라 값** → 의사 다항(pseudo-polynomial) |

### Part A — 편집 거리: 상태·전이·기저·순서

- **상태**: D[i][j] = a[0..i) → b[0..j) 최소 편집 수.
- **전이**: D[i][j] = min( D[i-1][j-1] + [a[i-1] ≠ b[j-1]], D[i-1][j] + 1, D[i][j-1] + 1 ).
- **기저**: D[i][0] = i, D[0][j] = j.
- **순서**: i 증가, 그 안에서 j 증가 (위·왼쪽·대각이 먼저 계산됨).

**정확성.** 두 단계로 나눈다.

1. *편집 연산 최소 횟수 = 정렬 최소 비용.* 정렬(위 줄 = a, 아래 줄 = b, 빈칸 허용, 빈칸끼리 열은 없음)의 비용 = "글자가 다른 열 + 빈칸 있는 열" 수. 정렬은 그대로 같은 비용의 편집 순서가 된다(≥ 방향). 반대로 어떤 편집 순서든 비용이 늘지 않는 정렬(Wagner–Fischer 의 trace)로 바꿀 수 있다 — 단위 비용이 삼각 부등식을 만족하기 때문이다(같은 위치를 두 번 고치는 것은 한 번 고치는 것보다 싸지 않다). 이 1단계는 테스트에서 **문자열 그래프 BFS**(정점 = 문자열, 간선 = 한 글자 삽입/삭제/치환)로 따로 확인했다: {a,b} 길이 ≤ 5 인 3969쌍 전부 BFS 거리 = 표 값.
2. *표 = 정렬 최소 비용* (i + j 에 대한 귀납). 최적 정렬의 마지막 열은 맞춤/삭제/삽입 중 하나이고, 떼어낸 나머지는 짧은 접두사들의 정렬이다 — 그 나머지가 최적이 아니면 더 싼 것으로 바꿔 전체도 싸지므로 모순. 따라서 D[i][j] ≥ 세 후보의 최솟값. 반대로 세 후보 각각은 (짧은 접두사의 최적 정렬 + 마지막 열 하나)로 실제 만들어지므로 ≤. 기저: 빈 문자열과의 정렬은 빈칸 열뿐.

**트레이스 — horse → ros** (LeetCode 72 예 1). 칸 = D[i][j], 진하게 칠한 칸이 역추적 경로:

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">D[i][j] (행 = horse 의 앞 i 글자, 열 = ros 의 앞 j 글자)</text>
  <g fill="currentColor" font-family="monospace">
    <text x="128" y="50">ε</text><text x="178" y="50">r</text><text x="228" y="50">o</text><text x="278" y="50">s</text>
    <text x="80" y="76">ε</text><text x="80" y="106">h</text><text x="80" y="136">o</text>
    <text x="80" y="166">r</text><text x="80" y="196">s</text><text x="80" y="226">e</text>
  </g>
  <g stroke="currentColor">
    <rect x="110" y="58" width="50" height="30" class="f-acc"/><rect x="160" y="58" width="50" height="30" class="f-mut"/><rect x="210" y="58" width="50" height="30" class="f-mut"/><rect x="260" y="58" width="50" height="30" class="f-mut"/>
    <rect x="110" y="88" width="50" height="30" class="f-mut"/><rect x="160" y="88" width="50" height="30" class="f-acc"/><rect x="210" y="88" width="50" height="30" class="f-mut"/><rect x="260" y="88" width="50" height="30" class="f-mut"/>
    <rect x="110" y="118" width="50" height="30" class="f-mut"/><rect x="160" y="118" width="50" height="30" class="f-mut"/><rect x="210" y="118" width="50" height="30" class="f-acc"/><rect x="260" y="118" width="50" height="30" class="f-mut"/>
    <rect x="110" y="148" width="50" height="30" class="f-mut"/><rect x="160" y="148" width="50" height="30" class="f-mut"/><rect x="210" y="148" width="50" height="30" class="f-acc"/><rect x="260" y="148" width="50" height="30" class="f-mut"/>
    <rect x="110" y="178" width="50" height="30" class="f-mut"/><rect x="160" y="178" width="50" height="30" class="f-mut"/><rect x="210" y="178" width="50" height="30" class="f-mut"/><rect x="260" y="178" width="50" height="30" class="f-acc"/>
    <rect x="110" y="208" width="50" height="30" class="f-mut"/><rect x="160" y="208" width="50" height="30" class="f-mut"/><rect x="210" y="208" width="50" height="30" class="f-mut"/><rect x="260" y="208" width="50" height="30" class="f-acc"/>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="130" y="78">0</text><text x="180" y="78">1</text><text x="230" y="78">2</text><text x="280" y="78">3</text>
    <text x="130" y="108">1</text><text x="180" y="108">1</text><text x="230" y="108">2</text><text x="280" y="108">3</text>
    <text x="130" y="138">2</text><text x="180" y="138">2</text><text x="230" y="138">1</text><text x="280" y="138">2</text>
    <text x="130" y="168">3</text><text x="180" y="168">2</text><text x="230" y="168">2</text><text x="280" y="168">2</text>
    <text x="130" y="198">4</text><text x="180" y="198">3</text><text x="230" y="198">3</text><text x="280" y="198">2</text>
    <text x="130" y="228">5</text><text x="180" y="228">4</text><text x="230" y="228">4</text><text x="280" y="228">3</text>
  </g>
  <text x="326" y="78" fill="currentColor">(5,3)=3 → 위(4,3)=2 : 2+1 = 3 → '-' e 삭제</text>
  <text x="326" y="104" fill="currentColor">  (대각 (4,2)=3, e≠s → 4 ≠ 3 이라 대각 아님)</text>
  <text x="326" y="132" fill="currentColor">(4,3)=2 → 대각(3,2)=2, s=s → '=' s</text>
  <text x="326" y="160" fill="currentColor">(3,2)=2 → 대각 2+1 ≠ 2, 위(2,2)=1+1 → '-' r 삭제</text>
  <text x="326" y="188" fill="currentColor">(2,2)=1 → 대각(1,1)=1, o=o → '=' o</text>
  <text x="326" y="216" fill="currentColor">(1,1)=1 → 대각(0,0)=0+1, h≠r → '~' h→r</text>
  <text x="10" y="266" fill="currentColor">뒤집으면 스크립트 ~ = - = - : h 를 r 로 바꾸고, r 을 지우고, e 를 지운다 (비용 3).</text>
  <text x="10" y="288" fill="currentColor">LeetCode 72 의 설명(horse → rorse → rose → ros)과 같은 편집이다.</text>
</svg>
```

솔루션의 실제 출력(정렬을 세 줄로; `|` 같음, `S` 치환, `D` 삭제, `I` 삽입):

```text
kitten -> sitting, distance 3, script:
  a  : kitten-
  b  : sitting
  op : S|||S|I
intention -> execution, distance 5, script:
  a  : intention
  b  : execution
  op : SSSSS||||
```

intention → execution 은 LeetCode 의 설명(삭제 1 + 치환 3 + 삽입 1)과 다른 스크립트지만 같은 비용 5 다 — 최적 스크립트는 하나가 아니다. 어느 것을 낼지는 역추적의 **동점 우선순위**가 정한다(대각 → 삭제 → 삽입). 테스트는 스크립트를 특정 문자열과 비교하지 않고 "적용하면 b 가 되고 비용 = 거리"를 검사한다(`script_ok`).

### Part A — LCS: 같으면 대각 + 1 이 왜 항상 옳은가

L[i][j] = a[0..i), b[0..j) 의 LCS 길이. 기저 L[i][0] = L[0][j] = 0.

- **a[i-1] = b[j-1] = c 이면 L[i][j] = L[i-1][j-1] + 1.** ≥ 는 자명(L[i-1][j-1] 짜리 공통 부분수열 뒤에 c). ≤: 최적 Z(길이 k)의 마지막 글자가 c 가 아니면 Z 뒤에 c 를 붙여 더 긴 공통 부분수열이 되므로 모순 → 마지막 글자 = c. 그 c 를 a[i-1], b[j-1] 에 다시 맞춰도 순서가 유지되므로(마지막 위치니까) Z 의 앞 k − 1 글자는 a[0..i-1), b[0..j-1) 의 공통 부분수열 → k − 1 ≤ L[i-1][j-1].
- **다르면 L[i][j] = max(L[i-1][j], L[i][j-1]).** 최적 Z 에서 a[i-1] 과 b[j-1] 이 **둘 다** 쓰일 수는 없다: 서로 맞춰질 수 없고(글자가 다름), a[i-1] 이 b[q] (q < j-1) 와 맞춰졌다면 b[j-1] 은 a 의 i-1 뒤 위치와 맞춰야 하는데 없다. 그러니 한쪽 끝을 버린 문제의 답과 같다.

"abcde" / "ace" 표 (L[i][j]):

| | ε | a | c | e |
|---|---|---|---|---|
| ε | 0 | 0 | 0 | 0 |
| a | 0 | **1** | 1 | 1 |
| b | 0 | 1 | 1 | 1 |
| c | 0 | 1 | **2** | 2 |
| d | 0 | 1 | 2 | 2 |
| e | 0 | 1 | 2 | **3** |

역추적: (5,3) e=e → (4,2) d≠c, 위 L[3][2]=2 ≥ 왼쪽 L[4][1]=1 → (3,2) c=c → (2,1) b≠a, 위 1 ≥ 왼쪽 0 → (1,1) a=a. 거꾸로 모은 "ace". CLRS 예(ABCBDAB, BDCABA)는 "위 우선"이면 BCBA, "왼쪽 우선"이면 BDAB — 둘 다 길이 4 의 LCS 다 (`demo_bugs.c` [5]).

**편집 거리와 LCS 를 잇는 부등식.** 길이 n, m, LCS 길이 L, 편집 거리 d.

- d ≤ n + m − 2L: LCS 글자만 남기고 a 의 나머지 n − L 개 삭제, b 의 나머지 m − L 개 삽입. 이 "삽입·삭제만"의 거리는 정확히 n + m − 2L 이다 (테스트: 치환 간선 없는 BFS 거리 = n + m − 2L, 3969쌍 전부).
- d ≤ max(n, m): 앞 min(n,m) 글자를 치환하고 나머지 삽입/삭제.
- d ≥ max(n, m) − L: 최적 정렬의 열을 맞춤 M, 치환 S, 삭제 X, 삽입 Y 개라 하면 n = M + S + X, m = M + S + Y, d = S + X + Y ≥ S + max(X, Y) = max(n, m) − M, 그리고 맞춤 열들은 공통 부분수열이라 M ≤ L.

테스트는 길이 0~64 랜덤 3000쌍에서 이 세 부등식, 대칭 d(a,b) = d(b,a), 삼각 부등식을 확인한다 (bad=0).

### Part B — 한 줄로 줄이기

**편집 거리.** 칸 (i, j) 는 위·왼쪽·대각만 읽는다. 배열 `row[0..m]` 하나에 왼쪽에서 오른쪽으로 덮어쓰면, `row[j]` 를 덮기 직전에 `row[j]` = 위(이전 줄), `row[j-1]` = 왼쪽(이번 줄, 이미 덮음)이다. 대각(이전 줄의 j-1)은 이미 덮였으므로 **덮기 전에 `diag` 변수에 옮겨 둔다**: `up = row[j]; ... row[j] = best; diag = up;`. 짧은 쪽을 열로 두면(d(a,b) = d(b,a)) 메모리 O(min(n, m)). 대각 보관을 빼먹으면 kitten/sitting 이 9 가 된다(7. 실수 3).

**0/1 배낭.**

- **상태**: K[i][w] = 아이템 0..i-1 중 크기 합 ≤ w 로 고른 가치 합 최댓값.
- **전이**: K[i][w] = max( K[i-1][w], K[i-1][w − s] + v ) (두 번째는 s ≤ w 일 때). 최적 부분집합은 아이템 i-1 을 안 넣거나(나머지는 아이템 0..i-2, 용량 w 의 최적) 넣는다(나머지는 용량 w − s 의 최적) — 나머지가 최적이 아니면 바꿔서 더 좋아지므로.
- **기저**: K[0][w] = 0. **순서**: i 증가, w 는 아무 순서(위 줄만 읽음).
- **역추적**: K[i][w] ≠ K[i-1][w] 이면 넣는 쪽이 엄격히 좋았던 것 → 선택, w −= s. 같으면 안 넣어도 최적.

고전 예(크기 10/20/30, 가치 60/100/120, W = 50; 표는 10 단위):

| K[i][w] | 0 | 10 | 20 | 30 | 40 | 50 |
|---|---|---|---|---|---|---|
| 아이템 없음 | 0 | 0 | 0 | 0 | 0 | 0 |
| +(10, 60) | 0 | 60 | 60 | 60 | 60 | 60 |
| +(20, 100) | 0 | 60 | 100 | 160 | 160 | **160** |
| +(30, 120) | 0 | 60 | 100 | 160 | 180 | **220** |

역추적: K[3][50] = 220 ≠ 160 → 아이템 2 선택, w = 20. K[2][20] = 100 ≠ K[1][20] = 60 → 아이템 1 선택, w = 0. K[1][0] = 0 = K[0][0] → 아이템 0 안 넣음. 가치/크기 비율 그리디는 10(6/단위) → 20(5/단위) → 30 은 안 들어감 → **160** 으로 틀린다.

**1D 배낭과 역방향 루프.** dp[w] 하나에 줄 i 를 덮어쓴다. 불변식: 아이템 i 를 처리하며 w 를 **W 에서 s 까지 내려가는** 동안, dp[w] 를 계산하는 순간 dp[0..w] 는 아직 K[i][·] 가 아니라 K[i-1][·], dp[w+1..W] 는 이미 K[i][·]. 읽는 dp[w − s] 는 w − s ≤ w 이므로 K[i-1][w − s] — 2D 전이와 정확히 같다(s = 0 이면 dp[w] 자신을 덮기 전에 읽으므로 역시 옛 값).

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m8b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">아이템 하나 (크기 2, 가치 3), W = 6 — dp[w] = max(dp[w], dp[w−2] + 3)</text>
  <text x="10" y="58" fill="currentColor">내려가며 (w = 6 → 2)</text>
  <g stroke="currentColor">
    <rect x="200" y="40" width="60" height="30" class="f-mut"/><rect x="260" y="40" width="60" height="30" class="f-mut"/>
    <rect x="320" y="40" width="60" height="30" class="f-ok"/><rect x="380" y="40" width="60" height="30" class="f-ok"/>
    <rect x="440" y="40" width="60" height="30" class="f-ok"/><rect x="500" y="40" width="60" height="30" class="f-ok"/>
    <rect x="560" y="40" width="60" height="30" class="f-ok"/>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="226" y="60">0</text><text x="286" y="60">0</text><text x="346" y="60">3</text><text x="406" y="60">3</text>
    <text x="466" y="60">3</text><text x="526" y="60">3</text><text x="586" y="60">3</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#m8b)">
    <path d="M470 40 C 470 26, 590 26, 590 38"/><path d="M350 72 C 350 88, 470 88, 470 72"/>
  </g>
  <text x="630" y="60" fill="currentColor">= 3 (정답)</text>
  <text x="60" y="104" fill="currentColor">dp[6] 이 읽는 dp[4] 는 아직 0 (옛 줄) → 아이템은 한 번만</text>
  <text x="10" y="158" fill="currentColor">올라가며 (w = 2 → 6)</text>
  <g stroke="currentColor">
    <rect x="200" y="140" width="60" height="30" class="f-mut"/><rect x="260" y="140" width="60" height="30" class="f-mut"/>
    <rect x="320" y="140" width="60" height="30" class="f-warn"/><rect x="380" y="140" width="60" height="30" class="f-warn"/>
    <rect x="440" y="140" width="60" height="30" class="f-warn"/><rect x="500" y="140" width="60" height="30" class="f-warn"/>
    <rect x="560" y="140" width="60" height="30" class="f-warn"/>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="226" y="160">0</text><text x="286" y="160">0</text><text x="346" y="160">3</text><text x="406" y="160">3</text>
    <text x="466" y="160">6</text><text x="526" y="160">6</text><text x="586" y="160">9</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#m8b)">
    <path d="M350 140 C 350 126, 470 126, 470 138"/><path d="M470 172 C 470 188, 590 188, 590 172"/>
  </g>
  <text x="630" y="160" fill="currentColor">= 9 (틀림)</text>
  <text x="60" y="210" fill="currentColor">dp[4] 가 방금 갱신된 dp[2] (이미 아이템 포함) 를 읽는다 → 같은 아이템 3번</text>
  <text x="60" y="232" fill="currentColor">= K[i][w] = max(K[i−1][w], K[i][w−s] + v) — 무제한(unbounded) 배낭의 점화식</text>
</svg>
```

`demo_bugs.c` 실측: 랜덤 10000개 중 앞으로 도는 버전은 **8993개** 틀렸고, 10000개 **전부** 무제한 배낭의 2D 정의(같은 줄 i 를 읽는 점화식)와 값이 같았다. 즉 루프 방향 하나가 문제를 바꾼다 — 동전 교환 경우의 수(ALGO-M07)의 루프 순서 함정과 같은 뿌리다.

### Part C — 키를 아는 설정 diff

장치 설정 파일 두 버전의 차이를 줄 단위로 보여 주는 도구(OTA 전에 무엇이 바뀌는지 로그로 남기기 등). 줄을 "글자"로 보고 편집 거리를 쓰되, 치환 비용을 가중치로 바꾼다: 같은 줄 0, **같은 키**(`=` 앞부분)면 1(값 변경), 다른 키면 금지(CD_INF = 1000 — 삭제 + 삽입 = 2 보다 크므로 절대 고르지 않음). 점화식은 그대로 min(대각 + cost, 위 + 1, 왼쪽 + 1) 이고 위의 정확성 증명도 그대로 성립한다(마지막 열 논증은 열 비용이 무엇이든 맞다). 실제 출력:

```text
config diff (key-aware), cost 5, ops ~=~-~=+:
  ~ uart.baud=9600  ->  uart.baud=115200
    uart.parity=none
  ~ imu.rate_hz=100  ->  imu.rate_hz=200
  - imu.range_g=4
  ~ log.level=info  ->  log.level=debug
    radio.channel=11
  + radio.tx_power=8
config diff (unit cost, key-blind), cost 5, ops ~=~~~~:
  ~ uart.baud=9600  ->  uart.baud=115200
    uart.parity=none
  ~ imu.rate_hz=100  ->  imu.rate_hz=200
  ~ imu.range_g=4  ->  log.level=debug
  ~ log.level=info  ->  radio.channel=11
  ~ radio.channel=11  ->  radio.tx_power=8
```

두 diff 의 **비용은 같은 5** 인데 키를 모르는 쪽은 "imu.range_g 를 log.level 로 바꿨다" 같은 의미 없는 diff 를 낸다 — 단위 비용에서는 동점이 많고, 역추적이 대각을 먼저 고르기 때문이다. 비용 모델이 곧 diff 의 품질이다. 테스트: 줄 풀 7개(`a=1`, `c`(= 없음), `d=1=2` 포함)로 만든 6줄 이하 랜덤 1500쌍을 정의 재귀와, 32줄 이하 2500쌍의 스크립트를 검증기와 대조 (bad=0). 실제 diff 도구는 LCS(삽입·삭제만) 계열인 Myers 알고리즘을 쓴다 (Q5).

### Part C — Flash 예산 모듈 선택과 결정 비트셋

128 KiB 여유 공간에 기능 모듈을 고른다. 독립적으로 OTA 갱신하려면 모듈마다 **지우기 섹터(erase sector) 경계**에서 시작해야 한다고 하자 — 그러면 모듈의 실제 비용은 ceil(size / 4096) 섹터다. 이것은 근사가 아니라 정확한 모델이고, 덤으로 W 가 131072 → 32 로 줄어든다. 실제 출력:

```text
flash budget 131072 B, gran 4096 B: value 365 (brute 365), used 126976 B, modules: ble_stack ota_dfu sensor_fusion crypto_aes usb_cdc power_mgr shell_cli
flash budget 131072 B, gran  256 B: value 395 (brute 395), used 128512 B, modules: ble_stack ota_dfu sensor_fusion crypto_aes usb_cdc lfs_logging power_mgr shell_cli
flash budget 131072 B, gran    1 B: value 395 (brute 395), used 127512 B, modules: ble_stack ota_dfu sensor_fusion crypto_aes usb_cdc lfs_logging power_mgr shell_cli
```

섹터 정렬 때문에 lfs_logging(14200 B → 4 섹터)이 빠지고 가치가 395 → 365 로 준다: 고른 7개가 31 섹터, 남은 1 섹터에 4 섹터짜리는 안 들어간다. 바이트로 꽉 채우면 127512 B 에 8개가 들어간다. "정렬 비용"이 선택을 바꾸는 실제 예다.

**결정 비트셋 역추적.** 1D dp 만으로는 무엇을 골랐는지 모른다(dp 가 덮어써짐). 아이템 i 처리 중 dp[w] 가 **엄격히 올라갈 때만** 비트 take[i][w] = 1. 그러면 take[i][w] = 1 ⇔ K[i+1][w] > K[i][w] (2D 의 "≠" 판정과 같은 정보). 역추적은 2D 와 똑같이 w = W, i = n−1 → 0 으로 비트를 보며 w −= s_i. 메모리: dp (W+1)·4 B + 비트 n·(W/8+1) B. 이 예(gran 4096)에서는 dp 132 B + 비트 10 × 5 = 50 B. gran 1 이면 dp 512 KiB + 비트 160 KiB — 호스트 빌드 도구에서나 가능한 크기.

### 측정 — `demo_bench.c` (이 맥, Apple Silicon M 시리즈, Apple clang 21, -O2, warm-up + 7회 중앙값)

```text
[1] edit distance by definition recursion (no table), strings of distinct letters
  n=m   calls          DP cells (n+1)^2   ms
    4   481            25                 0.00
    6   13483          49                 0.01
    8   398593         81                 0.45
   10   12146179       121                19.28
   12   377393953      169                625.53
```

호출 수는 정확히 (3·D(n,n) − 1)/2 다. D(n,n) 은 중앙 Delannoy 수(OEIS A001850: 321, 8989, 265729, 8097453, 251595969 for n = 4, 6, 8, 10, 12). 증명: 호출 수 f 와 g = (3D − 1)/2 가 같은 점화식 f(n,m) = 1 + f(n−1,m) + f(n,m−1) + f(n−1,m−1) 과 기저 f(0,m) = f(n,0) = 1 을 만족한다 (D 가 D(n,m) = D(n−1,m) + D(n,m−1) + D(n−1,m−1), D(0,m) = 1 이므로 대입하면 +1 이 정확히 맞는다). D(n,n) 은 n 마다 점근적으로 3 + 2√2 ≈ 5.83 배 — 실측 비율 31.07 = 5.57² 는 그 근처다. 표는 169칸이면 된다.

```text
[2] edit distance, random |S|=4 strings, median of 7
  n=m    2D ms    1-row ms   2D ns/cell  1-row ns/cell  2D table bytes  1-row bytes  same
  1000     0.94      1.00       0.939         0.996         2004002         2002  yes (d=529)
  2000     5.05      4.41       1.264         1.103         8008002         4002  yes (d=1053)
  4000    18.88     21.21       1.180         1.326        32016002         8002  yes (d=2083)

[3] 0/1 knapsack, n=64 random items (size 1..W/8, value 1..1000), median of 7
  W       2D ms    1D ms   1D+bits ms   2D bytes    1D bytes  1D+bits bytes  same optimum
  4096      0.18     0.16        0.31     1065220      16388          49220  yes (17275)
  16384     0.55     0.63        0.92     4260100      65540         196676  yes (19502)
  65536     1.98     1.59        4.94    17039620     262148         786500  yes (22207)
```

비율을 볼 것. 이 맥에서는 2D 와 한 줄의 **속도가 거의 같다**(칸당 약 1 ns) — 2D 표도 한 줄씩 순차로 쓰고 바로 위 줄만 읽어서 캐시에 잘 맞기 때문이다. 공간 축소의 이득은 속도가 아니라 **메모리**(32 MB → 8 KB, 17 MB → 256 KB)다. 1D+비트는 비트 쓰기와 줄마다 memset 때문에 1D 보다 2~3배 느리지만, 역추적을 2D 표의 약 1/22 메모리로 한다.

**Cortex-M 에서** (`demo_target.c`, 이 맥에서 `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -c -fstack-usage` 실제 결과): `ed_1row`(row[65] 를 스택에) **204 B**, `knap_1d` **56 B** (Cortex-M0: 196 B / 80 B). 1D 배낭의 안쪽 루프는 4번 펼쳐졌고 칸마다 다음 6개 명령이다 — 곱셈·나눗셈 없음:

```text
	ldr	r4, [r0, #4]          @ dp[w - s]
	ldr.w	r5, [lr, #4]          @ dp[w]
	add	r4, r1                @ + v
	cmp	r4, r5
	it	hi
	strhi.w	r4, [lr, #4]          @ 더 크면 저장
```

M0(ARMv6-M)에는 IT 블록이 없어 같은 자리가 `cmp` + `bls` 분기로 바뀐다(실제 출력에서 확인). RAM 예산으로 말하면: 2D 편집 거리 표 65 × 65 × uint16 = 8450 B 는 64 KB SRAM MCU 에 들어가지만 1000자 로그 비교(2 MB)는 안 된다 → 한 줄(2 KB) 또는 띠(연습 6).

### 복잡도

| 함수 | 시간 | 추가 메모리 | 메모 |
|---|---|---|---|
| ed_table / lcs_table | O(n·m) | (n+1)(m+1) 칸 | 재귀 없음, 스택 상수 |
| ed_script / lcs_string | + O(n+m) | 스크립트 ≤ n+m 칸 | 역추적은 표가 필요 |
| ed_1row | O(n·m) | min(n,m)+1 칸 | 거리만. 스크립트까지 O(n+m) 메모리로는 Hirschberg (Q3) |
| knap_2d | O(n·W) | (n+1)(W+1) 칸 | W 는 값 → 의사 다항 |
| knap_1d | O(n·W) | W+1 칸 | 역방향 루프 |
| flash_select | O(n·W) | W+1 칸 + n·W 비트 | W = budget/gran |
| cfg_diff | O(n·m·L) | (n+1)(m+1) | L = 줄 길이 (strcmp) |
| 정의 재귀 (참고) | (3·D(n,m) − 1)/2 호출 | 스택 깊이 n+m | 지수 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 51/51 passed, exit 0, 약 0.2 초
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 오류 없음
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench   # 약 1.3 초
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bugs.c -o demo_bugs && ./demo_bugs      # 약 0.1 초
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -c -fstack-usage demo_target.c -o t.o && cat t.su
```

솔루션의 나머지 실제 출력:

```text
LCS(ABCBDAB, BDCABA) = BCBA
exhaustive {a,b} len<=5: 3969 pairs vs BFS, bad ed=0 script=0 lcs=0 lcs_str=0 1row=0
random: ed vs definition recursion 1500 (len<=7, |S|=3) bad=0; lcs vs 2^n brute 600 (len<=12, |S|=4) bad=0
random len 0..64 x3000: bad script=0 lcs_str=0 1row=0 bounds/symmetry=0 triangle=0
Rosetta 0-1 knapsack: value 1030, weight 396, optimal subsets 1 (2^22 brute force)
knapsack exhaustive n=3: 32768 instances, bad 2d=0 pick=0 1d=0
knapsack random: 400 (n<=15) vs brute bad 2d=0 pick=0 1d=0; 200 (n=32) 2d vs 1d bad=0
config diff random: 1500 (<=6 lines) vs recursion bad=0; 2500 scripts (<=32 lines) bad=0
flash_select random: 300 instances (n<=12, gran 1/64/256/4096) vs brute bad=0
51/51 passed
```

Rosetta Code 값(1030, 무게 396)은 기억한 아이템 목록으로 재현했고, 같은 실행에서 2^22 부분집합 전수가 1030 과 **유일한** 최적 부분집합을 확인했다.

## 7. 자주 하는 실수

모두 `demo_bugs.c` 실제 출력이다.

**1. 1D 배낭을 앞으로 돈다.** `for (w = s; w <= W; w++)` 는 같은 아이템을 여러 번 넣는다. 크기 2·가치 3·W = 6 → 9 (정답 3). 랜덤 10000개 중 8993개 틀림, 그리고 10000개 전부 무제한 배낭 값과 같다 (Part B 그림). 고친 코드: `for (uint32_t w = W + 1; w-- > s; )` — 부호 없는 `w >= s` 조건은 s = 0 일 때 영원히 참이라 이 관용구를 쓴다.

**2. 편집 거리 기저를 0 으로 둔다.** `D[i][0] = D[0][j] = 0` 이면 "빈 문자열로 바꾸는 데 0" 이 되어 ed("abc", "") = 0, ed("xabcx", "abc") = 1 (정답 3, 2) — 앞뒤 글자를 공짜로 버리는 근사 부분문자열 매칭(semi-global)이 되어 버린다. 랜덤 10000개 중 9808개 틀림.

**3. 한 줄로 줄이면서 대각값을 안 남긴다.** `row[j-1]` 은 이미 이번 줄 값이다. 대각 자리에 그것을 쓰면 kitten/sitting = 9, 랜덤 10000개 중 9181개 틀림. `diag = up` 을 덮기 전에 저장.

**4. 가치/크기 비율 그리디.** 분수 배낭(fractional)에서만 최적이다. 0/1 에서는 60/100/120 예에서 160 (최적 220), 랜덤 10000개 중 1455개 최적 아님, 최악 비율 **0.165**. max(그리디, 가장 비싼 단일 아이템) 으로 고치면 최악 0.550 — 이론상 ≥ 1/2 보장(분수 최적 ≤ 그리디 접두사 + 처음 못 넣은 아이템)과 맞는다. 정답이 필요하면 DP.

**5. 역추적 동점 처리를 "버그"로 착각한다.** 위 우선 / 왼쪽 우선은 LCS 문자열이 다르다 (랜덤 10000개 중 5040개 다름, 길이는 10000개 전부 같음). 테스트에서 특정 문자열을 기대하면 안 되고 "공통 부분수열인가 + 길이"를 검사한다. 출력이 결정적이어야 하면(로그 비교, 재현 가능한 빌드) 우선순위를 문서에 적는다.

**6. 표 크기·셀 타입을 안 따진다.** 편집 거리 ≤ max(n,m) 이라 n ≤ 255 면 uint8_t 칸으로도 충분하고, 배낭 가치 합은 n·max(v) 를 계산해 타입을 고른다(이 레슨: 32 × 1000 ≪ 2^32). 2D 표를 함수 지역 배열로 두면 Cortex-M 스택(보통 1~8 KB)이 바로 넘친다 — 정적 RAM(`static`)이나 호출자 버퍼로.

**7. 역추적 조건을 `>=` 로 섞어 쓴다.** 배낭 역추적에서 "K[i][w] == K[i-1][w-s] + v 이면 넣음" 을 먼저 보면 s ≤ w 확인을 빼먹기 쉽고(w − s 언더플로), 값이 같은 동점에서 다른 답이 나온다. 이 레슨은 "≠ 이면 넣음" 하나로 통일했다 — 그 경우 넣는 쪽이 엄격히 좋았으므로 s ≤ w 가 보장된다.

## 8. 면접 Q&A

<details><summary>Q1. 0/1 배낭이 O(n·W) 인데 왜 NP-hard 라고 하나요?</summary>

입력 크기는 W 의 **비트 수**(log W)이지 W 자체가 아니다. O(n·W) = O(n·2^{log W}) 이라 입력 길이에 대해 지수 — 의사 다항(pseudo-polynomial) 시간이다. 0/1 배낭(결정 문제)은 NP-complete 이지만 이런 알고리즘이 있어 "약한(weakly) NP-hard" 라 부른다. 실무 대응: W 의 단위를 키운다(이 레슨의 섹터 단위 — 모델이 정확하면 손실 없음), 가치가 작으면 "가치 v 를 만드는 최소 크기" 로 축을 바꿔 O(n·Σv), n ≤ 40 정도면 반으로 나눈 부분집합 열거(meet-in-the-middle, O(2^{n/2}·n)), 근사가 괜찮으면 가치를 반올림하는 FPTAS.

</details>

<details><summary>Q2. 1D 배낭에서 w 를 왜 내려가며 도나요? 꼬리: 올라가면 무엇을 푸는 건가요?</summary>

dp[w] 를 갱신할 때 dp[w − s] 가 아직 "아이템 i 를 고려하기 전" 값이어야 같은 아이템을 한 번만 쓴다. 내려가면 왼쪽 칸은 아직 안 건드렸으니 옛 값이다(불변식: dp[0..w] = 이전 줄, dp[w+1..W] = 새 줄). 올라가면 dp[w − s] 가 이미 아이템 i 를 포함할 수 있어 K[i][w] = max(K[i−1][w], K[i][w−s] + v) — 무제한(unbounded) 배낭이 된다. 실측: 앞으로 도는 버전이 랜덤 10000개 전부에서 무제한 배낭과 같은 값. 그래서 무제한 배낭/동전 교환 최소 개수는 일부러 올라가며 돈다.

</details>

<details><summary>Q3. 공간을 줄이면 역추적은 어떻게 하나요?</summary>

세 가지. (1) **결정 비트셋**: 값은 한 줄, "이 칸에서 넣었나" 1비트만 n·W 개 — 2D uint32 표의 1/32 (Part C, 실측 17 MB → 786 KB 중 비트 512 KB). (2) **Hirschberg** (편집 거리·LCS): 중간 행에서 앞쪽 DP 와 뒤쪽(뒤집은 문자열) DP 를 각각 한 줄로 계산해 최적 경로가 지나는 열을 찾고 두 반쪽을 재귀 — 시간 O(n·m) (상수 약 2배), 메모리 O(n + m), 재귀 깊이 O(log n). (3) 체크포인트: √n 줄마다 저장해 두고 구간별로 다시 계산. 편집 거리 "값만" 이면 한 줄로 충분하다.

</details>

<details><summary>Q4. 하향식 메모이제이션(top-down memo)으로 짜면 무엇이 다른가요? MCU 에서는?</summary>

같은 점화식에 캐시를 붙인 것이라 결과는 같다. 차이: (1) 실제로 도달하는 상태만 계산한다 — 배낭에서 크기가 모두 큰 배수면 n·W 칸 중 일부만 닿는다. (2) 재귀 깊이가 n + m (편집 거리) 또는 n (배낭) — Cortex-M 의 1~8 KB 스택에서 프레임당 수십 바이트 × 깊이 128 은 위험하다(ALGO-B11). (3) 캐시를 "미계산" 표시로 초기화해야 한다. 그래서 펌웨어에서는 바닥부터(bottom-up) 반복 + 정적 표가 기본이고, 메모는 상태 공간이 아주 성길 때(해시맵 캐시) 쓴다. 메모 없는 정의 재귀는 실측대로 (3·D(n,n) − 1)/2 번 호출 — n = 12 에서 3.8억.

</details>

<details><summary>Q5. diff 도구(git diff)는 편집 거리 DP 를 쓰나요?</summary>

아니다. 줄 diff 는 치환 없이 삽입·삭제만 보므로 LCS 문제이고(삭제 + 삽입 = n + m − 2L), git 의 기본은 Myers 의 O((n+m)·D) 알고리즘(D = 편집 거리)이다 — 비슷한 파일이면 D 가 작아 거의 선형. 결과 품질을 위해 patience/histogram 같은 변형도 고를 수 있다. 이 레슨의 키 인식 diff 처럼 "어떤 짝을 맞출 것인가" 가 사람이 읽는 diff 품질을 정한다. 펌웨어 쪽 연결: 델타 OTA(bsdiff 등, INFRA-A04)는 바이너리 차이를 다른 방법(접미사 배열 매칭)으로 찾는다.

</details>

<details><summary>Q6. 편집 거리가 k 이하인지만 알면 될 때 더 빨리 할 수 있나요?</summary>

있다. |i − j| > k 인 칸은 거리가 k 를 넘으므로(그 칸까지 이미 |i − j| 번의 삽입/삭제가 필요, 경로를 따라 D 는 줄지 않음) 띠 |i − j| ≤ k 만 계산하면 된다 — O(k·n) 시간 (Ukkonen). 결과는 min(d, k + 1). UART CLI 의 "did you mean" 처럼 k = 2 면 명령 하나당 몇십 칸이다 (연습 6, 랜덤 4000개에서 전체 DP 와 일치). |n − m| > k 면 계산 없이 바로 거절.

</details>

<details><summary>Q7. 두 문자열 길이가 1000 이고 RAM 이 32 KB 인 MCU 입니다. 편집 거리와 실제 차이 위치가 필요해요.</summary>

2D uint16 표는 1001² × 2 ≈ 2 MB 라 불가. 거리만이면 한 줄 2 KB. 차이 위치까지면: (1) 거리 상한 k 를 알면 띠 표 (2k + 1) × 1001 칸 — k = 8 이면 uint8 로 약 17 KB, (2) Hirschberg 로 O(n + m) 메모리, (3) 장치에서는 거리만 계산하고 스크립트는 호스트로 올려 계산. 셀 타입은 거리 ≤ 1000 이라 uint16, 띠 + k ≤ 254 면 uint8. 표는 정적 RAM 이나 링커 스크립트로 잡은 영역에 — 스택에 두지 않는다.

</details>

<details><summary>Q8. 최적해가 여러 개면 어떤 것을 내나요? 테스트는 어떻게 쓰죠?</summary>

역추적의 동점 우선순위가 정한다(이 레슨: 대각 → 삭제 → 삽입, 배낭은 "같으면 안 넣음"). 면접에서는 "최적 중 하나" 라고 말하고, 결정적인 규칙을 명시한다. 테스트는 특정 답 대신 **성질**을 검사한다: 스크립트를 적용하면 b 가 되는가 + 비용 = 거리, LCS 문자열이 양쪽의 부분수열인가 + 길이, 고른 아이템의 크기 합 ≤ W + 가치 합 = 최적값. 기대값 자체는 독립 구현(BFS, 정의 재귀, 부분집합 전수)에서 온다.

</details>

## 9. 추가 연습

AlgoMonster 매핑 노트가 "Grid DP 갭"과 "Knapsack 변형 갭"으로 표시한 부분을 채운다.

### 연습 1 — 점유 격자 경로 수 (LeetCode 63 Unique Paths II)

로봇이 R × C 점유 격자(1 = 장애물)에서 오른쪽/아래로만 움직여 (0,0) → (R−1,C−1) 로 가는 경로 수를 `uint64_t` 로. 장애물 없는 34 × 34 까지 이항계수와 대조하고, uint64 가 넘치는 크기를 말하라.

<details><summary>답 보기</summary>

dp[c] 한 줄: 장애물이면 0, 아니면 `dp[c] += dp[c-1]` (위 = 옛 dp[c], 왼쪽 = 방금 계산한 dp[c-1] — 여기서는 같은 줄 왼쪽을 읽는 게 맞으므로 **올라가며**). 시작 칸이 장애물이면 0. 장애물 없으면 C(R+C−2, R−1): 34 × 34 = C(66, 33) = 7219428434016265740 < 2^64, 35 × 35 = C(68, 34) ≈ 2.85 × 10^19 > 2^64 − 1 ≈ 1.84 × 10^19 이라 넘친다(Python 으로 확인). 테스트: 1..34 × 1..34 전부 파스칼 삼각형과 대조, 장애물 25% 랜덤 7 × 7 이하 2000개를 경로 하나씩 세는 재귀와 대조.

</details>

### 연습 2 — 최소 비용 경로 + 경로 복원 (LeetCode 64 Minimum Path Sum)

칸마다 이동 에너지 비용이 있는 격자에서 최소 합 경로와 그 이동 문자열("RRDD")을 출력하라.

<details><summary>답 보기</summary>

S[r][c] = cost[r][c] + min(S[r−1][c], S[r][c−1]) (첫 행/열은 한쪽만). 역추적: (R−1, C−1) 에서 위가 왼쪽보다 작거나 같으면 'D' 를, 아니면 'R' 을 뒤에서부터 채운다. 경로 복원 때문에 2D 표를 쓴다(값만이면 한 줄). LeetCode 예 [[1,3,1],[1,5,1],[4,2,1]] → 7, 이동 RRDD. 테스트: 6 × 6 이하 랜덤 2000개를 모든 경로 재귀와 대조 + 출력한 경로를 실제로 걸어 합 = 최솟값 확인.

</details>

### 연습 3 — 두 전원 레일에 부하 균등 분할 (LeetCode 416 Partition Equal Subset Sum)

부하 전류 x[i] 를 두 레일에 합이 정확히 같게 나눌 수 있는가? 나눌 수 있으면 배정(side[i])까지, 메모리 O(Σx/2) 로.

<details><summary>답 보기</summary>

부분집합 합 = 가치 없는 0/1 배낭. reach[s] 를 **내려가며** 갱신(같은 부하 두 번 금지). 배정 복원은 "s 에 처음 도달시킨 아이템" parent[s] 하나로 충분하다: reach[s − x_i] 는 아이템 i 이전에 도달했으므로 parent[s − x_i] < i — 사슬을 따라가면 서로 다른 아이템을 인덱스가 엄격히 줄며 고른다. 비트셋 없이 O(S) 로 역추적되는 이유가 이것(가부만 묻는 DP 라서). 예 [1,5,11,5] → 가능, [1,2,3,5] → 불가. 테스트: n ≤ 16 랜덤 1500개를 2^n 전수와 대조, 가능한 456개는 두 쪽 합이 같은지 확인.

</details>

### 연습 4 — 최단 공통 상위수열 (LeetCode 1092 Shortest Common Supersequence)

a 와 b 를 둘 다 부분수열로 포함하는 가장 짧은 문자열을 만들어라. (두 센서 이벤트 로그를 순서를 지키며 합치기.)

<details><summary>답 보기</summary>

길이 = n + m − LCS. LCS 표를 역추적하며 같은 글자는 한 번, LCS 에 안 쓰이는 글자는 그대로 내보내고, 한쪽이 끝나면 나머지를 붙인다. "abac", "cab" → "cabac" (길이 5). 테스트: 길이 ≤ 12 랜덤 800쌍에서 길이 = n + m − (부분수열 전수 LCS), 두 입력이 모두 출력의 부분수열인지 확인.

</details>

### 연습 5 — 최장 공통 부분문자열 (연속; LeetCode 718 Maximum Length of Repeated Subarray 의 문자열판)

부분수열이 아니라 **연속** 구간. 두 펌웨어 로그에서 가장 긴 공통 구간을 찾고 위치를 돌려라.

<details><summary>답 보기</summary>

E[i][j] = a[i−1] = b[j−1] 이면 E[i−1][j−1] + 1, 아니면 **0** (끊기면 리셋 — LCS 와의 차이). 답은 표 전체의 최댓값. 한 줄로 줄일 때 대각만 읽으므로 j 를 **내려가며** 돌면 row[j−1] 이 아직 이전 줄 값이다 (배낭과 같은 이유). 테스트: 길이 ≤ 64 랜덤 2000쌍을 O(n·m·L) 직접 비교와 대조 + 돌려준 위치의 구간이 b 에 실제로 있는지.

</details>

### 연습 6 — 띠 편집 거리와 UART CLI "did you mean"

`ed_band(a, n, b, m, k)` = min(d, k + 1) 을 O(k·n) 으로. 명령 목록 {reboot, reset, status, stats, log, flash, freq} 에서 오타 입력에 거리 ≤ 2 인 가장 가까운 명령을 제안하라.

<details><summary>답 보기</summary>

|i − j| ≤ k 칸만 계산하고, 띠 바로 바깥 칸(다음 줄이 읽는 cur[lo−1], cur[hi+1])을 k + 1 로 둔다. 정당성: 정렬 경로를 따라 D 는 줄지 않고, 띠 밖 칸의 D ≥ |i − j| > k 이므로 띠 밖을 지나는 경로는 결과를 k 이하로 만들 수 없다 → 띠 안 값 = min(D, k + 1). 실제 출력: 'staus' → status (d=1), 'rset' → reset (d=1), 'flsah' → flash (d=2). 주의: 'staus' 는 stats 와도 거리 1 — 동점은 목록 순서로 정해졌다(먼저 나온 status). 'flsah' 는 인접 글자 교환인데 Levenshtein 은 2 로 센다; 교환을 1 로 세려면 Damerau(OSA) 전이 D[i−2][j−2] + 1 을 추가한다. 테스트: 랜덤 4000개(k 0..9, 절반은 몇 글자만 바꾼 쌍)에서 전체 DP 의 min(d, k + 1) 과 일치.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 15/15 passed, 약 0.2 초
```

## 10. 핵심 정리

- 2D DP 는 상태(i, j) · 전이 · 기저(빈 접두사 행/열) · 순서(의존 칸이 먼저)를 한 문장씩 말한다. 정확성은 "최적해의 마지막 열/마지막 아이템" 논증.
- 편집 거리: min(대각 + [다름], 위 + 1, 왼쪽 + 1), 기저 i / j. LCS: 같으면 대각 + 1, 다르면 max(위, 왼쪽). max(n,m) − L ≤ d ≤ min(max(n,m), n + m − 2L).
- 역추적 = "이 값이 어느 이웃에서 왔나" 를 다시 계산. 동점 우선순위가 답을 고른다 → 테스트는 성질로.
- 공간 축소: 편집 거리는 한 줄 + `diag` 변수, 0/1 배낭은 w 를 **내려가며**(올라가면 무제한 배낭 — 실측 10000/10000). 1D 로 역추적하려면 결정 비트셋 n·W 비트.
- 배낭 O(n·W) 는 의사 다항. W 단위를 섹터로 잡으면 정확하면서 표가 작아진다(131072 → 32). 섹터 정렬이 선택을 바꾼다(395 → 365).
- 이 맥 실측: 2D 와 한 줄은 칸당 약 1 ns 로 속도 비슷, 메모리는 32 MB vs 8 KB. 정의 재귀는 (3·D(n,n) − 1)/2 호출 — n = 12 에서 3.8억.
- Cortex-M4: ed_1row 스택 204 B, 1D 배낭 안쪽 루프 칸당 ldr·ldr·add·cmp·it·str 6개. 큰 표는 스택이 아니라 정적 RAM.

## 참고자료

- R. A. Wagner, M. J. Fischer, "The String-to-String Correction Problem", *J. ACM* 21(1), 1974 — 편집 거리 DP 와 trace(정렬) = 편집 순서 정리.
- V. I. Levenshtein, "Binary codes capable of correcting deletions, insertions, and reversals", *Soviet Physics Doklady* 10(8), 1966 (러시아어 원문 1965).
- Cormen, Leiserson, Rivest, Stein, *Introduction to Algorithms* 3판 §15.4 LCS (예: ABCBDAB / BDCABA, 정리 15.1 최적 부분 구조), 연습 16.2-2 (0/1 배낭 DP). 4판에서는 장 번호가 다르다 — 확인 권장.
- D. S. Hirschberg, "A linear space algorithm for computing maximal common subsequences", *Commun. ACM* 18(6), 1975.
- E. W. Myers, "An O(ND) Difference Algorithm and Its Variations", *Algorithmica* 1, 1986 — git diff 기본 알고리즘.
- E. Ukkonen, "Algorithms for approximate string matching", *Information and Control* 64, 1985 — 띠(band) 계산 (연습 6).
- H. Kellerer, U. Pferschy, D. Pisinger, *Knapsack Problems*, Springer 2004 — 의사 다항 DP, 그리디 1/2 근사, FPTAS.
- Rosetta Code, "Knapsack problem/0-1" — 22개 아이템, W = 400 → 가치 1030 / 무게 396 (이 레슨에서 DP 와 2^22 전수로 재현).
- OEIS A001850 (중앙 Delannoy 수) — 정의 재귀 호출 수 (3·D(n,n) − 1)/2 와 대조.
- LeetCode 72 Edit Distance, 1143 Longest Common Subsequence, 583 Delete Operation for Two Strings (n + m − 2L), 416 Partition Equal Subset Sum, 62/63 Unique Paths (II), 64 Minimum Path Sum, 1092 Shortest Common Supersequence, 718 Maximum Length of Repeated Subarray.
- AlgoMonster 노트 M09 §15–20 (Dual-Sequence DP: LCS · Edit Distance · SCS), §26–37 (Knapsack), §8–14 (Grid DP, 연습 1·2) (Don 정리본).
- 관련 레슨: ALGO-M07 (1D DP, 동전 교환 루프 순서), ALGO-M09 (그리디가 맞는 경우·교환 논증), ALGO-A03 (비트마스크 DP), ALGO-A04 (KMP — 정확 매칭), ALGO-B11 (재귀 깊이), ALGO-M04 (격자 BFS), ALGO-M13 (2D 누적합), INFRA-A04 (델타 업데이트 bsdiff), SYSD-M01 (A/B 부트로더·파티션), TOOL-M01 (링커 스크립트 MEMORY — Flash 예산), TOOL-B07 (조건부 컴파일·빌드 변형).
