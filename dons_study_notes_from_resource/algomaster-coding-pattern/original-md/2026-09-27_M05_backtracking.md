# Module 5 — Backtracking (백트래킹·메모이제이션)

> 출처: AlgoMonster Coding Patterns, Module 5 (레슨 17개) · 한국어 개념 정리 노트
> 목표: "choose → recurse → undo" 백트래킹 템플릿 하나로 조합·순열·분할 문제를 풀고, pruning·추가 state·aggregation·memoization·dedup 변형을 문제 문구에서 바로 알아보기.

## 한눈에 보기 (5분 복습용)

| 패턴/문제 | 언제 쓰나 (키워드) | 핵심 아이디어 | 복잡도 |
|---|---|---|---|
| DFS with States | 경로를 들고 다니는 트리 탐색 | path push → 재귀 → pop (frame당 짝 맞추기) | O(n) + 출력 |
| BT1 템플릿 | "find/generate **all** …" | is_leaf / get_edges / path push·pop | O(b^h) |
| Letter/Phone Combinations | 자리마다 선택지, 모든 조합 | 레벨 = 자리, 간선 = 그 자리 문자 | O(4^n · n) |
| Pruning (v1.1) | 제약 조건 있는 생성 | `is_valid` 실패 시 continue, start_index += len(edge) | 최악 동일, 실제 ↓ |
| Palindrome Partitioning | 모든 조각이 X인 분할 | prefix를 간선으로, 조건 불만족 prune | O(2^n · n) |
| Additional States | 이력 의존 제약 | open/close 카운트, `used[]` 인자 + revert | — |
| Valid Parentheses | 올바른 괄호 전부 | `open < n`이면 `(`, `close < open`이면 `)` | O(4^n · n) 상한 |
| Permutations | 순서 중요, 각 원소 1회 | `used[i]` 체크·복원 | O(n · n!) |
| BT2 Aggregation | 가능? / 몇 가지? / 최소? | path 없이 반환값 OR / + / min 집계 | memo 크기 × 노드당 작업 |
| Memoization | 중복 서브트리 | `memo[start_index(, state)]` | fib O(n) |
| Word Break | 사전 단어로 분할 가능? | prefix 간선 + OR + memo[start] | O(n² · m) |
| Decode Ways | 해독 방법 수 | 1자리(≠0) + 2자리(10~26), 합 + memo | O(n) |
| Coin Change (min) | 최소 동전 수 | 합을 state, min 집계 + memo, 불가 inf | O(amount · n) |
| Dedup (3Sum) | "unique" 조합 | 정렬 + 같은 값 skip + i+1 이후만 | O(n²) |
| Combination Sum | 합 = target, 재사용 O | `dfs(i)`로 순서 강제, 정렬 후 초과 시 break | O(n^(t/min)) |
| Subsets | power set | 포함/제외 or 모든 노드 저장 + start_index | O(n · 2^n) |

**첫 질문: "전부 나열"인가, "값 하나(존재·개수·최적)"인가?** 전자는 BT1(path + push/pop), 후자는 BT2(반환값 집계) + 중복 서브트리 보이면 memo(= DP).

**트리를 먼저 그려라:** 작은 입력으로 state-space tree를 그리고 is_leaf·get_edges만 정하면 코드는 템플릿에 채워 넣기.

**중복 제거 3종 세트:** 정렬 → 같은 레벨에서 이전과 같은 값 skip → start_index로 뒤쪽만 선택 (조합은 `i`/`i+1`, 순열은 `used[]`).

**Mutable state는 반드시 revert, 정수 인자는 `x+1`로 넘기면 자동 복원.** 결과 저장은 항상 `path[:]` 복사본.

---

## 1. DFS with States

> 섹션: Combinatorial Search

### 핵심 개념
- **State = 부모 호출이 자식 호출에 넘겨주는 정보.** 여기선 "root에서 현재 노드까지의 **경로**".
- 경로 state를 제대로 들고 다닐 수 있으면 대부분의 백트래킹이 같은 틀: **choose → recurse → undo**.

### 예제: Ternary Tree Paths
**문제:** 자식이 최대 3개인 트리에서 모든 root→leaf 경로를 `"1->2->4"` 형태로 반환.

```python
# 버전 1: 매번 새 리스트 (간단, 복사 비용 있음)
def ternary_tree_paths(root):
    res = []
    def dfs(node, path):
        if not node.children:                          # leaf
            res.append("->".join(path + [str(node.val)]))
            return
        for child in node.children:
            dfs(child, path + [str(node.val)])
    if root: dfs(root, [])
    return res

# 버전 2: path 하나를 재사용 (push → recurse → pop)
def ternary_tree_paths_v2(root):
    res, path = [], []
    def dfs(node):
        path.append(str(node.val))                     # choose
        if not node.children:
            res.append("->".join(path))
        for child in node.children:
            dfs(child)                                 # recurse
        path.pop()                                     # undo
    if root: dfs(root)
    return res
```

### 왜 버전 2인가
- 버전 1은 매 단계 리스트 할당·복사 → 메모리/시간 낭비.
- 버전 2는 path가 **call stack과 항상 동기화**. 규칙: **같은 stack frame에서 push 1번 = pop 1번**. pop을 빼먹으면 형제 가지에 엉뚱한 노드가 섞임.

- 🐛 결과에 `path` 자체를 넣으면 이후 pop으로 내용이 바뀜 → 반드시 **복사본**(`path[:]`, `"".join`) 저장.
- 🎤 "백트래킹의 핵심?" → 상태를 변경하고, 재귀하고, 돌아오면 정확히 되돌린다.

---

## 2. Backtracking 1

### 핵심 개념
- **조합 탐색(combinatorial search)**: 조건을 만족하는 묶음·배치 찾기 — 순열, 조합, 부분집합, 스도쿠. 입력이 조금만 커져도 경우의 수가 폭증.
- **Backtracking = state-space tree 위의 DFS.** 가능한 모든 상태를 트리로 그린 것이 state-space tree, leaf가 해답. 너무 크면 가지를 잘라냄(pruning).
- 앞의 트리 DFS와 차이: 트리가 **주어지지 않는다** — 각 단계에서 간선(선택지)과 자식을 **직접 생성**한다.

### 구현 순서: "트리를 그려라, 그려라, 그려라!"
해답이 최소 1개 나오는 작은 입력으로 트리를 그리고 두 질문에 답한다:
1. **is_leaf** — 해답에 도달했는지 어떻게 아나?
2. **get_edges** — 어떻게 가지를 치나(자식 생성)?

```python
def dfs(start_index, path):
    if is_leaf(start_index):
        report(path[:])            # 복사본 저장
        return
    for edge in get_edges(start_index):
        path.append(edge)          # choose
        dfs(start_index + 1, path) # recurse (start_index = 트리 레벨)
        path.pop()                 # undo = "backtrack"
```
- `path + [edge]`로 새 리스트를 넘기면 pop이 필요 없지만, **append/pop 형태가 기본** — ① 이름의 유래인 undo가 명시적 ② 스도쿠·N-Queens처럼 공유 보드를 수정·복원하는 문제로 그대로 확장 ③ O(1) 변경 vs 매번 O(h) 복사.

### 복잡도
- 시간 ∝ state-space tree 노드 수 ≈ **O(b^h)** (b = 분기 수, h = 높이; 분기가 불균일하면 상한).
- 공간 = 보통 트리 높이 O(h) (재귀 스택) + 결과 저장.

### 예제: a/b로 만든 n글자 단어 전부
**문제:** n → `'a','b'`로 만든 길이 n 문자열 전부 사전순. 예: 2 → `["aa","ab","ba","bb"]`.
- is_leaf: `start_index == n`. get_edges: `"ab"`.

```python
def letter_combination(n):
    res, path = [], []
    def dfs(i):
        if i == n:
            res.append("".join(path)); return
        for ch in "ab":
            path.append(ch); dfs(i + 1); path.pop()
    dfs(0)
    return res
```
- **복잡도:** leaf 2^n개 × join O(n) → 시간·공간 O(2^n · n).
- ✅ 기억: **backtracking + memoization = dynamic programming**.
- 🔑 "find all / generate all / 모든 경우를 나열" → state-space tree + 백트래킹 템플릿.

---

## 3. Generate All Phone Number Combinations

**문제:** 2–9 숫자로 된 전화번호 → 전화 키패드 문자로 만들 수 있는 모든 조합. 예: `"56"` → `["jm","jn","jo","km","kn","ko","lm","ln","lo"]`.

**핵심 관찰**
- "all possible combinations" → 조합 탐색. 트리 레벨 = 숫자 위치, 각 노드의 자식 = 그 숫자가 나타내는 문자들.
- 템플릿 채우기: **is_leaf** = `start_index == len(digits)`, **get_edges** = `KEYBOARD[digits[start_index]]`.

```python
KEYBOARD = {"2": "abc", "3": "def", "4": "ghi", "5": "jkl",
            "6": "mno", "7": "pqrs", "8": "tuv", "9": "wxyz"}

def letter_combinations_of_phone_number(digits):
    res, path = [], []
    def dfs(i):
        if i == len(digits):
            res.append("".join(path)); return
        for ch in KEYBOARD[digits[i]]:
            path.append(ch); dfs(i + 1); path.pop()
    dfs(0)
    return res
```

**복잡도:** 최악(7·9만) 분기 4 → leaf 4^n개 × join O(n) = 시간·공간 **O(4^n · n)**, 재귀 깊이 O(n).

- 🐛 빈 입력 `""` → 이 코드는 `[""]` 반환. LeetCode 17은 `[]`를 기대 → 초기 체크 필요.
- ✅ `start_index`는 `len(path)`로 대체 가능하지만, 이후 문제(분할 등)에선 path 길이와 위치가 달라지므로 템플릿에 유지.
- 🔑 "각 자리마다 선택지가 있고 모든 조합" → 레벨 = 자리, 간선 = 그 자리 선택지.

---

## 4. Backtracking 1 - Pruning

> 섹션: Pruning

### 핵심 개념
- **Pruning(가지치기)** = state-space tree에서 가지를 잘라내는 것. 가지가 적을수록 빠르다.
- 언제? 그 가지로 가면 **유효한 최종 상태가 절대 안 나오는 게 확실할 때** — 보통 문제의 제약 조건을 위반하는 순간.

### 템플릿 v1.1
```python
def dfs(start_index, path):
    if is_leaf(start_index):
        report(path[:])
        return
    for edge in get_edges(start_index):
        if not is_valid(edge):          # ① pruning: 제약 위반이면 건너뜀
            continue
        path.append(edge)
        dfs(start_index + len(edge), path)   # ② 한 칸이 아니라 edge 크기만큼 전진
        path.pop()
```
- v1 대비 변화: ① `is_valid` 검사로 가지 자르기 ② `start_index`를 **가변 크기**만큼 증가 (문자열 분할처럼 한 번에 여러 글자를 소비하는 문제).

- ✅ 가지치기는 **일찍** 할수록 효과가 크다 — 자식을 만들기 전에 검사.
- 🎤 "백트래킹을 빠르게 하려면?" → 제약을 이용해 불가능한 가지를 조기에 잘라낸다. 최악 복잡도는 같아도 실제 탐색 노드 수가 크게 준다.

---

## 5. Partition a String Into Palindromes

**문제:** 문자열 s를 **모든 조각이 palindrome**이 되도록 나누는 모든 방법. 예: `"aab"` → `[["a","a","b"], ["aa","b"]]`.

**핵심 관찰**
- 왼→오로 진행. 현재 위치 `start`에서 가능한 **모든 prefix** (길이 1, 2, …)가 간선.
- prefix가 palindrome이 **아니면 그 가지 전체를 잘라냄** (pruning). 맞으면 path에 넣고 `end`부터 재귀.
- `start == n` → 모든 글자가 어떤 palindrome에 속함 → 완성된 분할.
- 여기서 `start_index`가 **edge 길이만큼** 전진 → 템플릿 v1.1이 그대로 들어맞음.

```python
def partition(s):
    n, res, path = len(s), [], []
    def dfs(start):
        if start == n:
            res.append(path[:]); return
        for end in range(start + 1, n + 1):
            piece = s[start:end]
            if piece != piece[::-1]:        # prune
                continue
            path.append(piece)
            dfs(end)
            path.pop()
    dfs(0)
    return res
```

**복잡도:** 각 글자 사이마다 "자른다/안 자른다" 2가지 → 분할 2^(n−1)개. 최악(`"aaaa"`: 전부 유효) **O(2^n · n)** (palindrome 검사 O(n)). 공간 O(n) 재귀.

- 🐛 `res.append(path)` (복사 안 함) → 결과가 전부 빈 리스트가 됨.
- ✅ 최적화: `is_pal[i][j]` DP 테이블을 미리 만들면 검사 O(1).
- 🔑 "partition string so that every substring is X, return all" → **prefix를 간선으로, 조건 불만족 prefix는 prune**.

---

## 6. Backtracking 1 - Additional States

> 섹션: Additional States

### 핵심 개념
- Palindrome Partitioning은 prefix만 보고 유효성 판단 가능했다. 하지만 어떤 제약은 **지금까지의 선택 이력**을 알아야 검사 가능 → **추가 state**를 dfs 인자로 들고 다닌다.
  - 예: 열린 괄호 수/닫힌 괄호 수, 이미 사용한 원소(`used[]`), 남은 합계.

### 백트래킹 1 템플릿 최종형
```python
ans = []
def dfs(start_index, path, *extra):          # 추가 state
    if is_leaf(start_index):
        ans.append(path[:])                   # path 복사본
        return
    for edge in get_edges(start_index, *extra):
        if not is_valid(edge, *extra):        # prune
            continue
        path.append(edge)
        update(extra)                         # state 갱신
        dfs(start_index + len(edge), path, *extra)
        revert(extra)                         # 필요하면 되돌림 (예: permutations의 used)
        path.pop()
```
- 핵심 차이: `start_index`를 갱신할 때 **추가 state도 같이 갱신**하고, 변경 가능한(mutable) state라면 **돌아올 때 되돌린다**.
- ✅ 정수·불변 state는 인자로 `x + 1`을 넘기면 자동으로 되돌려짐(각 frame이 자기 값을 가짐). 리스트·set 같은 공유 객체만 명시적 revert 필요.
- 🔑 "유효성 판단에 지금까지의 개수/사용 여부가 필요" → 추가 state 인자.

---

## 7. Generate All Valid Parentheses

**문제:** 정수 n → 괄호 쌍 n개로 만든 **올바른** 괄호 문자열 전부. 예: n=2 → `(())`, `()()`; n=3 → 5개.

**핵심 관찰**
- 빈 문자열에서 `(` 또는 `)`를 붙여 길이 2n까지 → 조합 탐색.
- 무효가 되는 순간 두 가지:
  1. `(`가 n개를 넘음.
  2. 짝이 될 `(` 없이 `)`를 붙임 (`close >= open`).
- 문자열만 봐서는 판정이 번거로움 → **추가 state `open`, `close` 카운트**로 O(1) pruning.

```python
def generate_parentheses(n):
    res, path = [], []
    def dfs(open_cnt, close_cnt):
        if len(path) == 2 * n:
            res.append("".join(path)); return
        if open_cnt < n:
            path.append("("); dfs(open_cnt + 1, close_cnt); path.pop()
        if close_cnt < open_cnt:
            path.append(")"); dfs(open_cnt, close_cnt + 1); path.pop()
    dfs(0, 0)
    return res
```

**복잡도:** 상한 O(4^n · n) (2^(2n) 문자열 × 길이 2n) — pruning 덕에 실제론 훨씬 적음(정확히는 카탈란 수 C_n ≈ 4^n / n^1.5). 공간 O(4^n · n) 결과 + O(n) 스택.

- ✅ 선택지가 2개뿐이고 state 갱신 방식이 서로 달라서 `for` 루프 대신 **if 두 개**가 더 깔끔. 템플릿은 구조 가이드일 뿐, 상황에 맞게 변형.
- ✅ open/close는 **정수를 인자로 +1해서 넘기므로 revert 불필요** (각 frame이 자기 복사본을 가짐).
- 🐛 `)` 조건을 `close < n`으로 쓰면 `)(` 같은 무효 문자열 생성.
- 🔑 "generate all valid/well-formed …" + 개수 제약 → **카운트를 추가 state로 들고 pruning**.

---

## 8. General All Permutations

**문제:** 서로 다른 글자로 된 문자열의 모든 순열. 예: `"abc"` → `abc, acb, bac, bca, cab, cba` (3! = 6).

**핵심 관찰**
- 조합(각 자리 독립 선택)과 달리 **같은 원소를 두 번 쓸 수 없음** → 가방에서 하나씩 꺼내는 모양: n × (n−1) × … × 1.
- 추가 state: **`used[i]`** — i번째 글자를 현재 path에서 이미 썼나.
- `used`는 공유 리스트(참조 전달) → path처럼 **재귀 후 반드시 되돌리기**(`used[i] = False`). 괄호 문제의 정수 카운트와 대비되는 지점.

**템플릿 매핑:** is_leaf = `len(path) == n`, get_edges = 모든 글자, is_valid = `not used[i]`, revert = `used[i] = False`.

```python
def permutations(letters):
    n, res, path = len(letters), [], []
    used = [False] * n
    def dfs():
        if len(path) == n:
            res.append("".join(path)); return
        for i, ch in enumerate(letters):
            if used[i]:
                continue
            path.append(ch); used[i] = True
            dfs()
            path.pop(); used[i] = False        # 두 state 모두 revert
    dfs()
    return res
```

**복잡도:** 시간·공간 **O(n · n!)** (n!개 × 길이 n).

- 🐛 `used[i] = False` 복원을 빠뜨리면 첫 순열 하나만 나옴.
- 🐛 입력에 중복 글자가 있으면 같은 순열이 중복 생성 → 정렬 + "앞의 같은 글자가 미사용이면 skip" 규칙 필요 (Permutations II).
- ✅ 대안: swap 기반 in-place 순열 (`nums[i], nums[k] = nums[k], nums[i]` 후 재귀, 다시 swap).
- 🔑 "all permutations / arrangements / 순서가 중요 + 각 원소 1회" → **used[] 배열 + revert**.

---

## 9. Backtracking 2 - Aggregation

> 섹션: Aggregation and Memoization

### 핵심 개념
- 지금까지는 "해를 **전부 생성**"하는 문제. 이제는 해를 만들 필요 없이 **값 하나로 집계**하는 문제:
  - "사전 단어들로 이 문자열을 만들 수 **있나**?" (Word Break)
  - "메시지를 해독하는 방법의 **수**" (Decode Ways)
  - "금액을 만드는 **최소** 동전 수" (Coin Change)
- 자식 호출의 **반환값을 부모로 올려 합치는** 방식 — Max Depth·Visible Tree Node와 같은 원리.

### 백트래킹 2 템플릿
```python
def dfs(start_index, *extra):
    if is_leaf(start_index):
        return BASE                 # 예: 1 (방법 1개) / True / 0
    ans = INITIAL
    for edge in get_edges(start_index, *extra):
        if not is_valid(edge):
            continue
        ans = aggregate(ans, dfs(start_index + len(edge), *extra))
    return ans
```
- v1과 차이: **path와 push/pop 없음** (해를 저장 안 함), 결과는 **반환값으로 집계**.

| 질문 형태 | 초기값 | aggregate |
|---|---|---|
| 가능한가? 존재하나? | `False` | `or` |
| 몇 가지 방법? | `0` | `+` |
| 최대/최소 값? | `-inf` / `inf` (또는 0) | `max` / `min` |

- ✅ 이런 문제는 같은 하위 문제(같은 `start_index`)가 반복됨 → 다음 레슨의 **memoization**과 짝.
- 🔑 "is it possible / number of ways / minimum·maximum" → 해 생성 X, **반환값 집계 DFS**.

---

## 10. Memoization

### 핵심 개념
- **Memoization** = 이전 함수 호출 결과를 dict("memo")에 적어두고, **똑같은 호출이 다시 오면 계산 없이 꺼내 쓰기**. (오타 아님 — "memo"에서 온 말)
- 고전 예: 피보나치. 순진한 재귀는 `fib(n-2)` 등이 여러 번 중복 계산 → O(2^n).

```python
def fib(n, memo={}):          # 실전에선 기본인자 대신 새 dict 전달 or lru_cache
    if n in memo:             # ① 있으면 바로 반환
        return memo[n]
    if n < 2:
        return n
    memo[n] = fib(n - 1, memo) + fib(n - 2, memo)   # ② 계산 후 저장
    return memo[n]

from functools import lru_cache
@lru_cache(maxsize=None)
def fib2(n):
    return n if n < 2 else fib2(n - 1) + fib2(n - 2)
```

### 언제 / 무엇을
- **언제:** state-space tree를 그렸을 때 **똑같은 서브트리가 반복**되면.
- **무엇을 key로:** 중복 서브트리들이 공유하는 속성 — 보통 `start_index` 또는 반복해서 등장하는 추가 state (예: 남은 금액).

### 복잡도
- 백트래킹 시간 ∝ 방문 노드 수 → memo 후에는 **∝ memo 크기 × 노드당 작업**.
- fib: memo 크기 O(n), 노드당 O(1) → 시간·공간 **O(n)**.

- 🐛 Python **mutable 기본 인자**(`memo={}`)는 호출 간 공유됨 — 의도치 않은 캐시 오염 가능. 함수 안에서 새로 만들거나 `lru_cache` 사용.
- 🐛 memo key에 **결과에 영향을 주는 state를 전부** 넣어야 함. 빠뜨리면 틀린 값 재사용.
- 🐛 path처럼 "지금까지의 경로"에 의존하는 문제는 memo 불가 — 결과가 **start_index(와 state)만의 함수**일 때만.
- 🎤 "memoization vs DP?" → memo = top-down(재귀 + 캐시), DP 테이블 = bottom-up(반복). 둘 다 중복 하위 문제 제거.

---

## 11. Word Break

**문제:** 문자열 `s`를 단어 목록의 단어들(재사용 가능)을 이어 붙여 만들 수 있나? 예: `"algomonster"`, `["algo","monster"]` → `true`; `"aab"`, `["a","c"]` → `false`.

**핵심 관찰**
- 현재 위치 `start`에서 **사전 단어 중 prefix로 맞는 것**이 간선. 맞으면 그 길이만큼 전진.
- 템플릿 2: is_leaf = `start == len(s)` → True, 초기값 False, aggregate = **OR**, 추가 state 없음.
- 순진한 버전은 `"aaa…ab"` + `["a","aa",…]`에서 폭발 (분기 10 × 깊이 140).
- **중복 서브트리:** "a"+"a"로 온 위치 2와 "aa"로 온 위치 2는 **남은 문자열이 같음** → 결과는 `start`만의 함수 → `memo[start]`.

```python
def word_break(s, words):
    memo = {}
    def dfs(start):
        if start == len(s):
            return True
        if start in memo:
            return memo[start]
        ans = False
        for w in words:
            if s.startswith(w, start) and dfs(start + len(w)):
                ans = True
                break                     # OR 집계: 하나라도 되면 조기 종료
        memo[start] = ans
        return ans
    return dfs(0)
```

**복잡도:** memo 없이 O(m^n). memo 있으면 상태 n개 × 단어 m개 × 비교 O(n) = **O(n² · m)**, 공간 O(n).

- 🐛 `s[start:].startswith(w)`는 매번 슬라이스 복사 → `s.startswith(w, start)`가 더 효율적.
- ✅ bottom-up DP: `dp[i] = any(dp[i+len(w)] for w if s.startswith(w,i))`, `dp[n]=True`.
- 🔑 "can be segmented / constructed from dictionary words" → **prefix 간선 + memo[start_index] + OR 집계**.

---

## 12. Num Ways to Decode a Message

**문제:** A→1, …, Z→26으로 인코딩된 숫자 문자열을 해독하는 방법의 수. 예: `"18"` → 2 (AH, R); `"123"` → 3 (ABC, LC, AW).

**핵심 관찰**
- 각 위치에서 간선은 0~2개:
  - 한 자리 해독: 현재 숫자가 `'0'`이 아니면 가능.
  - 두 자리 해독: 두 자리 수가 **10~26**이면 가능.
  - `'0'`으로 시작하면 **0가지** (0은 10·20의 일부로만 해독 가능).
- 템플릿 2: is_leaf → 1 반환, 초기값 0, aggregate = **+**.
- `"12|3"`처럼 "1,2"로 온 경우와 "12"로 온 경우의 남은 부분이 같음 → `memo[start]`.

```python
def decode_ways(digits):
    memo = {}
    def dfs(i):
        if i == len(digits):
            return 1
        if i in memo:
            return memo[i]
        if digits[i] == "0":
            return 0                          # 선행 0은 해독 불가
        ways = dfs(i + 1)                     # 한 자리
        if i + 1 < len(digits) and 10 <= int(digits[i:i + 2]) <= 26:
            ways += dfs(i + 2)                # 두 자리
        memo[i] = ways
        return ways
    return dfs(0)
```

**복잡도:** memo 없이 O(2^n). memo 있으면 상태 n개 × O(1) = **O(n)**, 공간 O(n).

- 🐛 `"06"`처럼 두 자리여도 선행 0이면 10~26 범위 밖 → 조건 `10 <=`가 이걸 걸러줌.
- 🐛 `"30"`, `"100"` → 0가지. 0 처리 테스트 필수.
- ✅ 사실상 피보나치 모양 → bottom-up으로 변수 2개(O(1) 공간)까지 최적화 가능.
- 🔑 "number of ways to decode/interpret" → **간선 1~2개 + 합 집계 + memo[start_index]**.

---

## 13. Min Coins to Make Change

**문제:** 동전 액면 목록(무한 사용)과 금액 → 금액을 만드는 **최소 동전 수**, 불가능하면 −1. 예: `[1,2,5]`, 11 → 3 (5+5+1); `[3]`, 1 → −1.

**핵심 관찰**
- 0에서 시작해 동전을 계속 더해가는 트리. 추가 state = **현재 합 `cur`**.
- leaf: `cur == amount` → 0개 더 필요, `cur > amount` → 불가능(`inf`).
- 템플릿 2: 초기값 `inf`, aggregate = `min(ans, child + 1)`.
- 결과가 `cur`에만 의존 (어떤 순서로 왔는지 무관) → `memo[cur]`.
- ⚠️ **Greedy 실패 예**: `[1,3,4]`, 6 → greedy 4+1+1=3개, 정답 3+3=2개. 그래서 전수 탐색 + memo.

```python
from math import inf

def coin_change(coins, amount):
    memo = {}
    def dfs(cur):
        if cur == amount:
            return 0
        if cur > amount:
            return inf
        if cur in memo:
            return memo[cur]
        best = inf
        for c in coins:
            best = min(best, dfs(cur + c) + 1)   # inf + 1 == inf
        memo[cur] = best
        return best
    res = dfs(0)
    return res if res != inf else -1
```

**복잡도:** 상태 amount개 × 동전 n개 → 시간 **O(amount · n)**, 공간 O(amount) (재귀 깊이 amount/min(coin)).

- 🐛 재귀 깊이가 amount에 비례 → amount가 크면 Python RecursionError. bottom-up DP (`dp[x] = min(dp[x-c]+1)`)가 안전.
- 🐛 불가능 표시를 −1로 섞어 min을 취하면 버그 → 내부에선 `inf`, 마지막에만 −1 변환.
- 🔑 "fewest/minimum number of … to make up amount" → **합을 state로, min 집계 + memo** (= 무한 배낭 DP).

---

## 14. Deduplication

> 섹션: Dedup

### 핵심 개념 (예제: 3Sum unique triplets)
**문제:** 배열 `nums`, `target`. 서로 다른 인덱스 i, j, k로 합이 target인 **고유한** 세 수 조합 전부. 값이 같아도 위치가 다르면 사용 가능. 예: `[1,1,2,3]`, 6 → `[[1,2,3]]` (한 번만).

### 중복이 생기는 두 원인과 처방
| 원인 | 증상 | 처방 |
|---|---|---|
| ① 같은 값이 여러 번 시작점이 됨 | 두 개의 1이 각각 `[1,2,3]` 생성 | **정렬** 후 `nums[i] == nums[i-1]`이면 skip |
| ② 같은 집합이 다른 순서로 나옴 | `[1,2,3]`, `[2,1,3]`, `[3,1,2]` | **검색 범위를 i+1 이후로 제한** → 인덱스 오름차순 강제 = 값도 오름차순 |

```python
def three_sum_unique_triplets(nums, target):
    nums.sort()
    res = []
    n = len(nums)
    for i in range(n - 2):
        if i > 0 and nums[i] == nums[i - 1]:      # ① 같은 시작값 skip
            continue
        lo, hi = i + 1, n - 1                     # ② 뒤쪽에서만 찾기
        while lo < hi:
            s = nums[i] + nums[lo] + nums[hi]
            if s == target:
                res.append([nums[i], nums[lo], nums[hi]])
                while lo < hi and nums[lo] == nums[lo + 1]: lo += 1   # 내부 dedup
                while lo < hi and nums[hi] == nums[hi - 1]: hi -= 1
                lo += 1; hi -= 1
            elif s < target:
                lo += 1
            else:
                hi -= 1
    return res
```

**복잡도:** 정렬 O(n log n) + 바깥 루프 × 투 포인터 O(n) = **O(n²)**, 추가 공간 O(1) (결과 제외).

### 일반 원칙 (백트래킹에도 그대로)
- **정렬해서 같은 값을 붙여 놓고**, 같은 레벨(같은 for 루프)에서 **이전과 같은 값이면 건너뛴다**.
- **start_index 이후만 고르게** 해서 순서 차이로 인한 중복(조합 vs 순열)을 막는다.
- 🐛 `i > 0 and nums[i] == nums[i-1]` 대신 `nums[i] == nums[i+1]`로 쓰면 유효한 `[1,1,x]`를 놓칠 수 있음 — "처음 나온 것은 쓰고 **이후 같은 값**을 skip".
- 🔑 "unique combinations / no duplicate triplets" → **정렬 + 같은 레벨 중복 skip + 인덱스 전진**.

---

## 15. Combination Sum

**문제:** 서로 다른 양의 정수 `candidates`, `target`. 합이 target인 **고유한 조합** 전부 (같은 수 무제한 사용, 순서 무관). 예: `[2,3,6,7]`, 7 → `[[2,2,3],[7]]`; `[2,3,5]`, 8 → `[[2,2,2,2],[2,3,3],[3,5]]`.

**핵심 관찰**
- 남은 값 `remaining`에서 후보를 빼가는 트리. `remaining == 0` → 해.
- 순진하게 매번 모든 후보를 시도하면 `(2,3)`과 `(3,2)`가 둘 다 나옴 → **중복**.
- **순서 강제로 dedup:** 3을 고른 뒤엔 2를 다시 보지 않는다 — 2를 고른 가지에서 이미 "2와 3" 조합은 다 탐색했기 때문. → 재귀에 `start_index` 전달, **`i`부터** 루프 (같은 수 재사용 허용이므로 `i+1`이 아니라 `i`).
- 정렬해두면 `remaining - num < 0`인 순간 뒤 후보는 전부 더 크므로 **break** (pruning).

```python
def combination_sum(candidates, target):
    candidates.sort()
    res, path = [], []
    def dfs(start, remaining):
        if remaining == 0:
            res.append(path[:]); return
        for i in range(start, len(candidates)):
            num = candidates[i]
            if num > remaining:
                break                      # 정렬 덕분에 이후 전부 불가
            path.append(num)
            dfs(i, remaining - num)        # i: 같은 수 재사용 허용
            path.pop()
    dfs(0, target)
    return res
```

**복잡도:** 시간 O(n^(target/min)) (분기 n, 깊이 ≤ target/min), 보조 공간 O(target/min) (+ 결과).

- 🐛 `dfs(i+1, …)`로 쓰면 재사용 불가 버전(Combination Sum II 계열)이 되어 `[2,2,3]`을 놓침.
- 🐛 `dfs(0, …)`로 쓰면 순열처럼 중복 조합 폭발.
- ✅ 입력에 중복 값이 있고 각 원소 1회만 쓰는 **Combination Sum II** → 정렬 + `dfs(i+1)` + `if i > start and nums[i] == nums[i-1]: continue`.
- 🔑 "unique combinations that sum to target, unlimited use" → **start_index로 순서 강제 + 정렬 후 break**.

---

## 16. Subsets

> 섹션: Additional Practices

**문제:** 서로 다른 정수 배열의 **모든 부분집합**(power set), 중복 없이. 예: `[1,2,3]` → `[], [1], [2], [3], [1,2], [1,3], [2,3], [1,2,3]` (2³ = 8개).

**핵심 관찰 — 트리를 그리는 두 가지 방법**
1. **포함/제외 이진 트리:** 레벨 i에서 `nums[i]`를 넣을지 말지 → leaf(깊이 n)에서만 결과 저장. leaf 2^n개.
2. **start_index 트리 (combination sum식):** 부분집합은 **모든 노드가 곧 해** → 노드에 들어오자마자 저장, `i`부터 뒤쪽 원소만 추가 (앞을 다시 보지 않아 dedup).

```python
# 방법 1: include / exclude
def subsets(nums):
    res, cur = [], []
    def dfs(i):
        if i == len(nums):
            res.append(cur[:]); return
        cur.append(nums[i]); dfs(i + 1); cur.pop()   # 포함
        dfs(i + 1)                                    # 제외
    dfs(0)
    return res

# 방법 2: 모든 노드가 해
def subsets_v2(nums):
    res, path = [], []
    def dfs(start):
        res.append(path[:])                 # leaf를 기다리지 않고 저장
        for j in range(start, len(nums)):
            path.append(nums[j]); dfs(j + 1); path.pop()
    dfs(0)
    return res
```

**복잡도:** 부분집합 2^n개 × 복사 O(n) → 시간·공간 **O(n · 2^n)**, 재귀 깊이 O(n).

- ✅ 방법 2는 노드 수가 정확히 2^n (방법 1은 2^(n+1)−1 노드) → 조금 더 빠름.
- ✅ 비트마스크 대안: `for mask in range(1 << n)` — 임베디드 감각에 잘 맞음.
- 🐛 중복 원소 있는 Subsets II → 정렬 + 방법 2에서 `if j > start and nums[j] == nums[j-1]: continue`.
- 🔑 "all subsets / power set / 각 원소 넣거나 빼거나" → **포함/제외 이진 트리** 또는 **모든 노드 저장 + start_index**.

---

## 17. Backtracking Speedrun

> 섹션: Speedrun

템플릿 적용 감각을 빠르게 점검하는 10문제 객관식 세트. 문제마다 **is_leaf / get_edges / is_valid / 추가 state**만 뽑아 정리.

| # | 문제 (LeetCode) | 템플릿 | is_leaf | get_edges / is_valid | 추가 state · 포인트 |
|---|---|---|---|---|---|
| 1 | Restore IP Addresses (93) | BT1 | `start == len(s)` **and** 조각 4개 | 길이 1~3 조각 (범위 체크) / 0~255, 선행 0 금지 (`"0"`은 OK) | 조각 5개 이상이면 즉시 return |
| 2 | Path Sum II (113) | BT1 (트리) | leaf 노드 **and** remaining == 0 | 자식 둘 / None이면 return | `remaining` 감소, path push/pop. **leaf 체크 필수** |
| 3 | Beautiful Arrangement (526) | BT2 (합) | `i == n+1` → 1 | 1..n 중 미사용 **and** (`num % i == 0` or `i % num == 0`) | `visited[]` 갱신·복원 |
| 4 | Word Break II (140) | BT1 | `start == len(s)` | `s[start:end+1]` 모든 end / 사전에 있나 | 결과는 `" ".join(path)` |
| 5 | Numbers With Same Consecutive Diff (967) | BT1 | 자릿수 == n | 다음 자리 `d−k`, `d+k` / 0~9 범위 | 첫 자리 1~9, **k == 0이면 중복 방지** |
| 6 | Matchsticks to Square (473) | BT2 (OR) | 모든 성냥 사용 & 네 변 동일 | 네 변 중 하나에 놓기 / `side + m <= target` | `sides[4]`, 합 % 4 ≠ 0이면 즉시 False, **내림차순 정렬**로 빠른 실패 |
| 7 | Permutations II (47) | BT1 | path 길이 == n | 미사용 원소 / **정렬 후 `nums[i]==nums[i-1] and not used[i-1]`면 skip** | `used[]` |
| 8 | Subsets II (90) | BT1 | 모든 노드가 해 (들어오자마자 저장) | `nums[start:]` / **`i > start and nums[i]==nums[i-1]`면 skip** | 정렬 |
| 9 | Combination Sum II (40) | BT1 | remaining == 0 | `candidates[start:]`, `dfs(i+1)` / 초과 시 **break**, 같은 레벨 중복 skip | 정렬 |
| 10 | Gray Code (89) | BT2 (OR, 첫 해에서 종료) | 2^n개 모두 사용 | `code ^ (1 << i)` (한 비트 뒤집기) / 미방문 | `visited[]`, 비트 연산 |

### 대표 코드 두 개
```python
# Restore IP Addresses — 가변 길이 간선 + pruning + 개수 제한
def restore_ip(s):
    res, path = [], []
    def ok(seg):
        return seg == "0" or (seg[0] != "0" and int(seg) <= 255)
    def dfs(start):
        if len(path) == 4:
            if start == len(s): res.append(".".join(path))
            return
        for end in range(start + 1, min(start + 3, len(s)) + 1):
            seg = s[start:end]
            if ok(seg):
                path.append(seg); dfs(end); path.pop()
    dfs(0)
    return res

# Matchsticks to Square — 추가 state(sides) + OR 집계 + 조기 종료
def makesquare(sticks):
    total = sum(sticks)
    if total % 4: return False
    side = total // 4
    sticks.sort(reverse=True)           # 큰 것부터 → 빨리 실패
    sides = [0] * 4
    def dfs(i):
        if i == len(sticks):
            return True                 # 전부 side 이하로 넣었고 합이 4*side → 네 변 모두 side
        for k in range(4):
            if sides[k] + sticks[i] <= side:
                sides[k] += sticks[i]
                if dfs(i + 1): return True
                sides[k] -= sticks[i]
        return False
    return dfs(0)
```

- ✅ **"전부 나열"이면 BT1(path + push/pop), "존재/개수/최소"면 BT2(반환값 집계)** — 첫 질문으로 템플릿을 고른다.
- ✅ 중복 입력 + "unique" → 항상 **정렬 + 같은 레벨 중복 skip** (순열은 `not used[i-1]` 조건, 조합/부분집합은 `i > start` 조건).
- 🐛 k == 0처럼 두 간선이 같은 자식을 만드는 경우 → 중복 결과. 간선 생성 시 동일성 체크.

---

## 용어·패턴 사전

| 용어 | 뜻 |
|---|---|
| State (DFS) | 부모 호출이 자식 호출에 넘기는 정보 (경로, 카운트, 남은 합) |
| Combinatorial search | 조건을 만족하는 묶음·배치(순열/조합/부분집합)를 찾는 문제 |
| State-space tree | 가능한 모든 상태를 노드로 그린 트리. leaf = 해 |
| Backtracking | state-space tree 위의 DFS. choose → recurse → undo |
| is_leaf / get_edges / is_valid | 해 도달 판정 / 가지(선택지) 생성 / 가지 유효성(pruning) |
| start_index | 현재 트리 레벨 또는 입력에서 다음에 처리할 위치 |
| Pruning | 유효한 해가 나올 수 없는 가지를 미리 잘라내기 |
| Additional state | 유효성 판단에 필요한 이력 (open/close 수, `used[]`, `sides[]`) |
| Revert | 공유(mutable) state를 재귀 복귀 후 원상태로 되돌리기 |
| BT1 템플릿 | 해를 전부 생성: path + push/pop, leaf에서 복사본 저장 |
| BT2 템플릿 (Aggregation) | 반환값 집계: 존재(OR), 개수(+), 최적(min/max) |
| Memoization | 같은 인자(start_index·state) 호출 결과를 캐시해 중복 서브트리 제거 |
| Top-down vs bottom-up | 재귀 + memo vs 반복문 DP 테이블 |
| Deduplication | 정렬 + 같은 레벨 중복 skip + 인덱스 전진으로 중복 해 제거 |
| Branching factor b / height h | 노드당 자식 수 / 트리 깊이 → 시간 O(b^h) |
| Power set | 모든 부분집합의 집합, 크기 2^n |
| Catalan number | 올바른 괄호 n쌍 개수 등, C_n ≈ 4^n / n^1.5 |
