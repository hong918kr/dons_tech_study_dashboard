# Module 4 — Depth First Search (깊이 우선 탐색·트리·BST)

> 출처: AlgoMonster Coding Patterns, Module 4 (레슨 16개) · 한국어 개념 정리 노트
> 목표: 재귀 → 트리 → DFS 템플릿을 몸에 익히고, "노드에서 무엇을 반환하고 무엇을 아래로 내려보내나"만 정하면 트리/BST 문제가 풀리는 상태 만들기.

## 한눈에 보기 (5분 복습용)

| 패턴/문제 | 언제 쓰나 (키워드) | 핵심 아이디어 | 복잡도 |
|---|---|---|---|
| 재귀 기본 | 자기 유사 구조, "작은 같은 문제" | base case + 크기가 줄어드는 재귀 호출, 내부는 call stack | 깊이만큼 스택 |
| DFS on Tree 템플릿 | 트리 전부 방문, 서브트리 값 합성 | **반환값 = 위로**, **state 인자 = 아래로** | O(n) / O(h) |
| Max Depth | height, longest root-to-leaf | postorder `max(l, r) + 1` | O(n) / O(h) |
| Visible Tree Node | root→노드 경로 조건 | 경로 최댓값을 state로 전달, `>=` | O(n) / O(h) |
| Balanced Tree | 모든 노드 좌우 높이 차 | (balanced, height) 튜플 postorder, null 높이 −1 | O(n) / O(h) |
| Subtree of Another | 트리 동일성, 포함 여부 | `same(a,b)` 동시 DFS × 모든 노드 | O(m·n) |
| Invert Tree | mirror, flip | 왼↔오 바꾼 새 서브트리 반환 | O(n) / O(h) |
| BST 검색/삽입 | 정렬 유지 + 동적 삽입 | 값 비교로 한쪽만 내려감, None 자리에 삽입 | O(h) |
| Valid BST | BST 검증 | (lo, hi) 범위를 state로 | O(n) / O(h) |
| LCA on BST | BST + 공통 조상 | 둘 다 작으면 왼, 둘 다 크면 오, 아니면 현재 | O(h) |
| Pre+In 복원 | traversal로 트리 재구성 | preorder=root, inorder=분할, 값→인덱스 dict | O(n) |
| Serialize | 트리 ↔ 문자열 | preorder + null 마커, iterator로 역직렬화 | O(n) |
| LCA (일반 트리) | 부모 포인터 없는 LCA | postorder, 양쪽 non-null이면 현재 노드 | O(n) / O(h) |

**트리 문제는 "노드 하나 입장에서" 푼다:** 반환값(자식→부모)과 state(부모→자식)를 먼저 정하고 base case(None)를 정하면 코드가 거의 자동으로 나온다.

**자식 결과가 필요하면 postorder, 조상 정보가 필요하면 state 인자(preorder).** Balanced/Max Depth/LCA는 전자, Visible Node/Valid BST는 후자.

**BST면 한쪽만 내려가라 → O(h).** 전체 순회가 필요한 일반 트리 문제(O(n))와 구분하는 게 면접 포인트.

---

## 1. Recursion Intro

> 섹션: Introduction

### 핵심 개념
- **재귀 = 함수가 자기 자신을 (더 작은 입력으로) 호출.** 점심 약속 비유: A가 B에게 전화 → B는 C를 먼저 불러야 해서 A를 보류하고 C에게 전화 → … 마지막 사람이 "OK"하면 역순으로 차례차례 답이 돌아온다.
- 올바른 재귀의 두 요소:
  1. **Base case (종료 조건)** — 더 이상 호출하지 않고 바로 답을 내는 경우.
  2. **Recursive call** — 인자를 바꿔 자기 자신 호출 (문제 크기가 줄어야 함).

```python
def factorial(n):
    if n <= 1:              # base case
        return 1
    return n * factorial(n - 1)   # recursive call
```

### 재귀와 스택
- 컴퓨터는 내부적으로 **call stack**을 쓴다. 호출마다 **stack frame**(함수 + 인자 + 지역변수)을 push, 반환 시 pop.
- 그래서 재귀는 명시적 스택 + 루프로 항상 바꿀 수 있다:

```python
def factorial_stack(n):
    stack = []
    while n > 0:            # 호출을 push (top = base case 쪽)
        stack.append(n); n -= 1
    res = 1
    while stack:            # 반환값을 쓰며 pop
        res *= stack.pop()
    return res
```

### 흔한 실수
- 🐛 base case 누락/도달 불가 → 무한 재귀 → `RecursionError`(Python 기본 한도 ~1000).
- 🐛 재귀 깊이 = 스택 메모리. 깊이 O(n)이면 공간도 O(n) — 긴 연결 리스트 같은 편향 트리에서 터질 수 있음.
- 🎤 "재귀를 반복문으로 바꿀 수 있나?" → 가능. call stack을 명시적 stack으로 흉내 내면 된다(임베디드에서 스택 크기 제한 시 유용).

---

## 2. Trees

### 핵심 개념
- **트리 = 사이클 없는 연결 그래프.** 성질: ① acyclic ② root에서 모든 노드로 경로 존재 ③ 간선 수 = N−1 ④ root 제외 모든 노드는 부모가 정확히 1개.
- 용어: **internal node**(자식 ≥1), **leaf**(자식 0), **ancestor**(root→나 경로 위의 노드, 자기 자신 제외), **descendant**(아래로 도달 가능한 노드, 자기 제외), **level**(조상 수).

### 이진 트리 분류
| 종류 | 정의 | 메모 |
|---|---|---|
| Full | 모든 노드 자식 0 또는 2 | |
| Complete | 마지막 레벨 빼고 꽉 참, 마지막 레벨은 왼쪽부터 | **heap**의 모양 |
| Perfect | 모든 internal 자식 2, 모든 leaf 같은 레벨 | 노드 수 = 2^h − 1, internal = leaf − 1 → 전체·leaf 둘 다 O(2^h) |
| Balanced | 모든 노드에서 좌우 높이 차 ≤ 1 | 높이 O(log n). **BST와 결합해야** 탐색 O(log n) |

- Perfect 트리 공식은 **조합 탐색(백트래킹)의 시간복잡도 추정**에 자주 쓰인다.
- Red-black / AVL은 알아두기만 — 면접 구현 출제는 거의 없음.

### 순회 (현재 노드를 언제 방문하나)
```python
class Node:
    def __init__(self, val, left=None, right=None):
        self.val, self.left, self.right = val, left, right

def inorder(root):          # 왼 → 나 → 오   (BST면 정렬 순서!)
    if root:
        inorder(root.left); print(root.val); inorder(root.right)

def preorder(root):         # 나 → 왼 → 오   (복사/직렬화)
    if root:
        print(root.val); preorder(root.left); preorder(root.right)

def postorder(root):        # 왼 → 오 → 나   (자식 결과로 내 값 계산: 높이, 삭제)
    if root:
        postorder(root.left); postorder(root.right); print(root.val)
```

### AlgoMonster 트리 인코딩 (테스트 입력 만들 때)
- 이진 트리: **preorder로 값 나열, null은 `x`**. 예: `5 4 3 x x 8 x x 6 x x`.
- N-ary 트리: preorder로 `값 자식수` 쌍 나열. 예: `7 3 2 1 5 0 3 0 4 0` (7은 자식 3개, 2는 자식 1개 …).
- ✅ 체크포인트: "internal = leaf − 1"은 perfect binary tree에서만 성립하는 성질.

---

## 3. DFS Intro

### 핵심 개념
- **DFS = 갈 수 있는 만큼 깊이 내려가고, 더 볼 게 없으면 되돌아와(backtrack) 다른 가지를 탐색.** 트리에서는 사실상 **preorder 순회**.
- 예: 이진 트리에서 값이 `target`인 노드 찾기.

```python
def dfs(root, target):
    if root is None:            # base case 1: 빈 노드
        return None
    if root.val == target:      # base case 2: 찾음
        return root
    # 왼쪽에서 찾으면 그걸 반환, 아니면 오른쪽 결과를 그대로 반환
    return dfs(root.left, target) or dfs(root.right, target)
```

### 같이 나오는 개념 두 개
- **Backtracking:** 깊이 들어갔다가 되돌아오는 동작 자체. DFS와 사실상 같은 것 — "backtracking은 개념, DFS는 그걸 구현한 알고리즘". 교과서에선 주로 **조합 탐색** 문맥에서 backtracking이라 부름.
- **Divide & Conquer:** 왼/오 두 재귀 호출(같은 종류의 하위 문제)로 쪼개고, 결과를 합쳐서 반환 → 분할 정복.

### 언제 DFS를 쓰나
| 대상 | 전형적 질문 |
|---|---|
| 트리 | 노드 찾기/생성/수정/삭제, 반환값 있는 순회(최대 서브트리, 균형 판정) |
| 조합 문제 | "몇 가지 방법?", "모든 조합을 구하라", "퍼즐의 모든 해" — 결국 **탐색 트리** 위의 DFS |
| 그래프 | A→B 경로, 연결 요소, 사이클 탐지 — **visited 기록 필수**(사이클 때문에) |

- 🐛 그래프에서 visited 없이 DFS → 무한 루프.
- 🎤 "DFS vs backtracking 차이?" → 본질적으로 같다. backtracking은 '되돌아옴' 개념, 보통 조합 탐색에서 상태를 되돌리는 DFS를 가리킨다.

---

## 4. DFS on Tree — Intro

> 섹션: DFS on Tree

### 핵심 개념: "노드처럼 생각하라"
- 트리 전체를 보지 말고 **한 노드 입장**에서 생각. 노드가 아는 건 ① 내 값 ② 자식으로 가는 링크, 이 두 개뿐. 현재 노드에서 할 일만 정하고 나머지는 재귀에 맡긴다.

### 템플릿
```python
def dfs(node, state):
    if node is None:
        return BASE           # 빈 트리의 답
    left = dfs(node.left, next_state)
    right = dfs(node.right, next_state)
    return combine(node.val, left, right)
```

### 함수 설계 시 결정할 두 가지
| 방향 | 수단 | 예 |
|---|---|---|
| **위로 (자식 → 부모)** | **반환값** | max depth면 "내 서브트리 깊이", 탐색이면 "찾은 노드 or None" |
| **아래로 (부모 → 자식)** | **state = 함수 인자** | 부모 값, 지금까지의 최대값, 들여쓰기 레벨 |

- 예 (state만 내려보내기): 디렉터리 트리 pretty-print — `dfs(node, indent)`에서 자식에게 `indent + ' '`를 넘기고 각 호출은 한 줄 출력, 반환값 없음.

### 반환값 vs 전역 변수 (트리 최댓값 예)
```python
# ① 반환값 (divide & conquer)
def tree_max(node):
    if node is None:
        return float('-inf')
    return max(node.val, tree_max(node.left), tree_max(node.right))

# ② 전역(바깥) 변수 — fire-and-forget
def get_max(root):
    best = float('-inf')
    def dfs(node):
        nonlocal best
        if node is None: return
        best = max(best, node.val)
        dfs(node.left); dfs(node.right)
    dfs(root)
    return best
```
- 취향 문제지만, **merge 단계가 복잡하면 전역 변수 방식이 더 간단**해질 수 있다(예: 경로 최대합처럼 반환값과 답이 다를 때).
- 🐛 Python에서 내부 함수가 바깥 변수를 **재할당**하려면 `nonlocal` 필요 (리스트 `[0]` 트릭도 가능).
- 🎤 "트리 문제 접근법?" → 반환값(위로 올릴 정보)과 state(아래로 내릴 정보)를 먼저 정의하고 base case를 정한다.

---

## 5. Max Depth of A Tree

**문제:** 이진 트리의 최대 깊이 = root→leaf 최장 경로의 **간선(edge) 수**. 예: `5 4 3 x x 8 x x 6 x x` (root 5, 자식 4·6, 4의 자식 3·8) → `2`.

**핵심 관찰**
- 가장 깊은 노드가 어디 있는지 모르니 **전체 순회** 필요 → DFS.
- 반환값 = "내 서브트리에서 root→leaf 최장 경로의 **노드 수**". 필요한 state 없음(자식 결과만으로 계산) → **postorder**.
- 노드 수에서 1을 빼면 간선 수.

**풀이**
1. `dfs(None) = 0`.
2. `dfs(node) = max(dfs(left), dfs(right)) + 1`.
3. 답 = `dfs(root) - 1` (빈 트리면 0).

```python
def tree_max_depth(root):
    def dfs(node):
        if not node:
            return 0
        return max(dfs(node.left), dfs(node.right)) + 1
    return dfs(root) - 1 if root else 0
```

**복잡도:** 시간 O(n) (노드 n개·간선 n−1개 한 번씩), 공간 O(h) — 편향 트리면 O(n).

- 🐛 **깊이 정의 확인!** 노드 수 기준(LeetCode 104)인지 간선 수 기준(여기)인지에 따라 −1이 붙는다.
- 🐛 엣지: 빈 트리, 노드 1개(→0), 한쪽으로 치우친 트리(재귀 깊이 n).
- 🔑 "maximum depth / height / longest root-to-leaf" → 자식 결과를 합치는 **postorder DFS**.

---

## 6. Visible Tree Node

**문제:** root→노드 경로 위에 그 노드보다 **엄격히 큰** 값이 없으면 "visible". visible 노드 개수를 세라 (root는 항상 visible). 예: `5 4 3 x x 8 x x 6 x x` → `3` (5, 8, 6).

**핵심 관찰**
- visible ⇔ `node.val >= (root→부모 경로의 최댓값)`.
- 그 최댓값은 **부모가 알려줘야** 하는 정보 → **state로 아래로 전달**.
- 반환값 = 내 서브트리의 visible 수 → 위로 합산.

**풀이**
1. `dfs(node, max_so_far)`; 빈 노드 → 0.
2. `node.val >= max_so_far`면 1 카운트.
3. 자식에게 `max(max_so_far, node.val)` 전달, 왼·오 결과 합산.
4. 시작 state = `-inf` (root는 무조건 visible).

```python
from math import inf

def visible_tree_node(root):
    def dfs(node, max_so_far):
        if not node:
            return 0
        cnt = 1 if node.val >= max_so_far else 0
        new_max = max(max_so_far, node.val)
        return cnt + dfs(node.left, new_max) + dfs(node.right, new_max)
    return dfs(root, -inf)
```

**복잡도:** 시간 O(n), 공간 O(h).

- 🐛 `>`가 아니라 `>=` — 같은 값은 visible (8→8 경로에서도 보임).
- 🐛 초기값을 0으로 두면 음수 트리에서 틀림 → `-inf`.
- 🔑 "root에서 이 노드까지의 경로에 대해 …" → **state(경로 최대/합/부모값)를 인자로 내려보내는 preorder DFS**. (LeetCode 1448 Count Good Nodes)

---

## 7. Balanced Binary Tree

**문제:** **모든 노드**에서 좌·우 서브트리 높이(간선 수) 차이가 ≤ 1이면 balanced. 판정하라. 빈 트리는 balanced. 예: 3의 왼쪽이 비고 오른쪽 높이가 2 → 차이 3 → `false`.

**핵심 관찰**
- 각 노드가 답해야 할 질문이 두 개: "내 서브트리 **높이**는?" + "내 서브트리가 **balanced**인가?" → 자식을 먼저 봐야 하므로 **postorder**.
- 간선 기준 높이에서 **null의 높이 = −1** (leaf = max(−1,−1)+1 = 0이 맞게 떨어짐).
- 높이와 실패 신호를 한 int에 섞는 sentinel(−1) 방식은 null 높이 −1과 충돌 → **(balanced, height) 튜플**로 반환하는 게 깔끔.

**풀이**
1. `check(None) = (True, -1)`.
2. 왼·오 결과를 받아 `ok = l_ok and r_ok and |lh − rh| ≤ 1`.
3. `(ok, max(lh, rh) + 1)` 반환.

```python
def is_balanced(tree):
    def check(node):
        if node is None:
            return True, -1
        l_ok, lh = check(node.left)
        r_ok, rh = check(node.right)
        return (l_ok and r_ok and abs(lh - rh) <= 1), max(lh, rh) + 1
    return check(tree)[0]
```

**복잡도:** 시간 O(n) (각 노드 1회), 공간 O(h).

- 🐛 root에서만 좌우 높이를 비교하면 틀림 — **모든 노드**에서 조건 필요.
- 🐛 노드마다 `height()`를 따로 호출하는 top-down 방식은 O(n²) (편향 트리). 한 번의 postorder로 높이와 판정을 같이 올려야 O(n).
- 🔑 "for every node, left/right subtree …" + 높이 → **postorder에서 (판정, 값) 두 개를 같이 반환**.

---

## 8. Subtree of Another Tree

**문제:** 두 이진 트리 `root`, `sub_root`. `sub_root`가 `root`의 **subtree**(어떤 노드 + 그 모든 자손)와 완전히 같은가? 빈 트리는 어떤 트리의 subtree. 예: root=`3 4 1 x x 2 x x 5 x x`, sub=`4 1 x x 2 x x` → `true`.

**핵심 관찰**
- 두 개의 재귀를 조합:
  1. `same(a, b)`: 두 트리를 **동시에** 내려가며 구조·값 비교.
  2. `root`의 모든 노드를 후보 root로 보고 `same(node, sub_root)` 시도.

**풀이**
1. `sub_root`가 None → True. `root`가 None → False.
2. `same(root, sub_root)`면 True.
3. 아니면 `is_subtree(root.left, sub)` or `is_subtree(root.right, sub)`.

```python
def is_same(a, b):
    if a is None or b is None:
        return a is b                  # 둘 다 None일 때만 True
    return a.val == b.val and is_same(a.left, b.left) and is_same(a.right, b.right)

def subtree_of_another_tree(root, sub_root):
    if sub_root is None:
        return True
    if root is None:
        return False
    return (is_same(root, sub_root)
            or subtree_of_another_tree(root.left, sub_root)
            or subtree_of_another_tree(root.right, sub_root))
```

**복잡도:** 시간 O(m·n) (최악: 모든 노드에서 전체 비교), 공간 O(m + n) 재귀 깊이.

- 🐛 값만 같고 구조가 다른 경우(한쪽에만 자식이 있음) → `same`에서 None 체크가 핵심.
- 🐛 "subtree"는 **자손 전부 포함** — 부분 구조 일치(prefix)가 아님.
- ✅ 심화: 두 트리를 null 마커 포함 preorder로 직렬화 후 문자열 매칭(KMP)하면 O(m+n).
- 🔑 "두 트리가 같은가 / 한 트리가 다른 트리 안에 있나" → **두 포인터 동시 DFS** + 모든 노드에서 시도.

---

## 9. Invert Binary Tree

**문제:** 이진 트리를 좌우 반전(거울상)해서 반환. 각 노드에서 왼·오 서브트리를 바꾸고 둘 다 재귀적으로 반전. 예: `1 2 4 x x 5 x x 3 x x` → `1 3 x x 2 5 x x 4 x x`.

**핵심 관찰**
- 반환값 = "반전된 현재 서브트리". state 없음.
- 새 노드 = (내 값, **반전된 오른쪽**을 왼쪽에, **반전된 왼쪽**을 오른쪽에).

```python
def invert_binary_tree(tree):
    if tree is None:
        return None
    return Node(tree.val,
                invert_binary_tree(tree.right),
                invert_binary_tree(tree.left))

# in-place 버전
def invert_inplace(node):
    if node:
        node.left, node.right = invert_inplace(node.right), invert_inplace(node.left)
    return node
```

**복잡도:** 시간 O(n), 공간 O(h) (최악 O(n)).

- 🐛 in-place로 `node.left = invert(node.right)` 후 `node.right = invert(node.left)`처럼 **순차 대입하면 이미 덮어쓴 값을 씀** → 튜플 동시 대입 또는 임시 변수.
- 🔑 "mirror / invert / flip tree" → 반환값으로 새 서브트리를 조립하는 DFS (Max Howell 밈 문제 😅).

---

## 10. Binary Search Tree Intro

> 섹션: Binary Search Tree

### 핵심 개념
- **BST:** 모든 노드에 대해 **왼쪽 서브트리 전체 < 노드 < 오른쪽 서브트리 전체**. 빈 트리도 BST. 값은 비교 가능하기만 하면 됨(정수, 문자열 …).
- inorder 순회 = **정렬 순서**.

### 연산 (모두 O(h))
| 연산 | 방법 |
|---|---|
| 검색 | 현재 값과 비교 → 같으면 발견, 작으면 오른쪽, 크면 왼쪽, None이면 없음 (이진 탐색과 동일 원리) |
| 삽입 | 검색하듯 내려가다 **빈 자리(None)에 새 노드**. 정렬 리스트처럼 원소를 밀 필요 없음 |
| 삭제(선택) | 찾은 뒤: 오른쪽이 비면 왼쪽 서브트리를 끌어올림, 아니면 **오른쪽 서브트리의 최소(leftmost)**로 대체 |

```python
def find(tree, val):
    while tree:
        if tree.val == val: return True
        tree = tree.right if tree.val < val else tree.left
    return False

def insert(tree, val):
    if tree is None:
        return Node(val)
    if val > tree.val:
        tree.right = insert(tree.right, val)
    elif val < tree.val:
        tree.left = insert(tree.left, val)
    return tree            # 중복은 무시
```

### 균형 문제
- 정렬된 입력을 순서대로 넣으면 **연결 리스트로 퇴화** → O(n).
- **Self-balancing (AVL, red-black)**: 회전(rotation)으로 높이 O(log n) 유지. 면접에선 "존재와 O(log n) 보장"만 알면 됨, 구현은 안 나옴.

### 왜/언제 BST (vs 해시 테이블)
- 해시가 보통 평균 O(1)이라 존재 여부만 볼 땐 해시가 우선.
- BST가 이기는 경우: ① **정렬 순서 유지**하며 동적 삽입 ② "x보다 큰 첫 원소"(lower/upper bound) ③ **k번째 작은/큰 원소** ④ 메모리 낭비 적음(해시는 빈 bucket 여유).
- 삽입 없이 조회만이면 **정렬 + 이진 탐색**으로 충분.
- 🎤 C++ `std::map`/`set` = red-black tree. Python엔 내장 BST 없음 → `bisect` + 리스트 또는 `sortedcontainers`.

---

## 11. Valid Binary Search Tree

**문제:** 이진 트리가 BST인지 판정 (모든 노드: 왼쪽 서브트리 전부 < 노드 < 오른쪽 서브트리 전부, **엄격한** 부등호). 예: `6 4 3 x x 5 x x 8 x x` → `true`; 오른쪽 서브트리 깊숙이 root보다 작은 값이 있으면 `false`.

**핵심 관찰**
- 부모-자식만 비교하면 부족 — 손자 이하 노드도 **조상들이 만든 범위** 안에 있어야 한다.
- state = 현재 노드가 가질 수 있는 **열린 구간 (lo, hi)**. 왼쪽으로 가면 hi = 내 값, 오른쪽으로 가면 lo = 내 값.
- 반환값 = 서브트리가 유효한가.

```python
from math import inf

def valid_bst(root):
    def dfs(node, lo, hi):
        if not node:
            return True
        if not (lo < node.val < hi):
            return False
        return dfs(node.left, lo, node.val) and dfs(node.right, node.val, hi)
    return dfs(root, -inf, inf)
```

**복잡도:** 시간 O(n), 공간 O(h).

- 🐛 **대표적 오답:** `node.left.val < node.val < node.right.val`만 확인. 반례: `5 (4) (6 (3) (7))` — 3은 6의 왼쪽이라 로컬로는 OK지만 5보다 작음.
- 🐛 중복 허용 여부 확인 (여기선 strict).
- ✅ 대안: **inorder 순회가 strictly increasing**인지 확인 (이전 값 하나만 기억 → O(1) 추가 공간).
- 🔑 "validate BST / 모든 조상 조건" → **범위(min, max)를 state로 내려보내는 DFS**.

---

## 12. Insert Into BST

**문제:** 유효한 BST의 root와 값 `val`. **새 leaf를 추가**하는 방식으로 삽입하고 root 반환. 이미 있으면 그대로. 기존 구조 변경 금지. 예: root 8, 왼쪽 6(왼쪽 자식 3)에 7 삽입 → 8의 왼쪽 → 6의 오른쪽 빈 자리에 leaf 7.

**핵심 관찰**
- 검색 경로를 따라 내려가다 **처음 만나는 None 자리**가 삽입 위치.
- 재귀가 "갱신된 서브트리 root"를 반환하게 하면 `node.left = insert(node.left, val)` 한 줄로 부모 링크가 자연스럽게 연결됨.

```python
def insert_bst(bst, val):
    if bst is None:
        return Node(val)              # 빈 자리 → 새 leaf
    if val > bst.val:
        bst.right = insert_bst(bst.right, val)
    elif val < bst.val:
        bst.left = insert_bst(bst.left, val)
    return bst                        # 같으면 아무것도 안 함
```

**복잡도:** 시간 O(h), 공간 O(h) 재귀 (반복문으로 쓰면 O(1)).

- 🐛 빈 트리 입력 → 새 노드가 root가 되어야 하므로 **반환값을 반드시 받아야** 한다.
- 🐛 중복 처리 누락 시 같은 값이 들어가 BST 불변식 깨짐.
- 🔑 "insert/delete in BST, 구조 수정" → **서브트리 root를 반환해서 부모가 재연결**하는 재귀 패턴.

---

## 13. Lowest Common Ancestor of a Binary Search Tree

**문제:** BST와 두 값 p, q. **LCA** = p와 q를 모두 자손으로 갖는 가장 깊은 노드(자기 자신도 자손으로 인정). 예: `[6,2,8,0,4,7,9,x,x,3,5]`, p=2, q=8 → `6`.

**핵심 관찰**
- BST 순서 덕분에 **한 경로만** 내려가면 됨 (일반 트리처럼 전체 탐색 불필요).
- 현재 노드 값 v 기준 3가지 경우:
  1. p, q 둘 다 < v → LCA는 왼쪽에.
  2. 둘 다 > v → 오른쪽에.
  3. 그 외 (**갈라지는 지점** 또는 p==v / q==v) → 현재 노드가 LCA.

```python
def lca_on_bst(bst, p, q):
    node = bst
    while node:
        if p < node.val and q < node.val:
            node = node.left
        elif p > node.val and q > node.val:
            node = node.right
        else:
            return node.val       # split 또는 equality
```

**복잡도:** 시간 O(h), 공간 O(1) 반복 (재귀면 O(h)).

- 🐛 equality 케이스(p가 q의 조상) 빠뜨리지 않기 — "자기 자신도 자손" 정의 때문에 else에 포함.
- 🔑 "LCA" + "BST" → 값 비교로 **분기점(split point)** 찾기. BST가 아니면 → 16번(일반 LCA)의 postorder 방식.

---

## 14. Reconstruct Binary Tree from Preorder and Inorder Traversal

> 섹션: Advanced

**문제:** 값이 모두 다른 이진 트리의 preorder·inorder 배열로 원래 트리 복원. 예: pre=`[3,9,20,15,7]`, in=`[9,3,15,20,7]` → root 3, 왼쪽 9, 오른쪽 20(자식 15, 7).

**핵심 관찰**
- **preorder → root를 알려줌** (구간의 첫 원소가 root).
- **inorder → 좌/우 분할을 알려줌** (root 위치 왼쪽 = 왼쪽 서브트리, 오른쪽 = 오른쪽 서브트리).
- inorder에서 얻은 **왼쪽 서브트리 크기**로 preorder도 똑같이 잘라낼 수 있다 → 두 개의 더 작은 동일 문제 → 재귀.

**풀이**
1. inorder 값→인덱스 **dict 미리 생성** (root 찾기 O(1)).
2. 슬라이싱 대신 인덱스 3개로 구간 표현: `pre_i`(root 위치), `in_start`, `size`.
3. `left_size = in_root - in_start`, `right_size = size - 1 - left_size`.
4. 왼쪽: `(pre_i + 1, in_start, left_size)`; 오른쪽: `(pre_i + 1 + left_size, in_root + 1, right_size)`.

```python
def construct_binary_tree(preorder, inorder):
    pos = {v: i for i, v in enumerate(inorder)}

    def build(pre_i, in_start, size):
        if size <= 0:
            return None
        root_val = preorder[pre_i]
        in_root = pos[root_val]
        left_size = in_root - in_start
        left = build(pre_i + 1, in_start, left_size)
        right = build(pre_i + 1 + left_size, in_root + 1, size - 1 - left_size)
        return Node(root_val, left, right)

    return build(0, 0, len(preorder))
```

**복잡도:** 시간 O(n), 공간 O(n) (dict + 재귀 깊이 O(h)).

- 🐛 **off-by-one이 제일 흔한 버그** — 크래시가 아니라 "조용히 틀린 트리"가 나온다. size 기반 표현이 인덱스 실수를 줄여준다.
- 🐛 매번 `inorder.index()` 선형 탐색 → O(n²). 배열 슬라이싱도 복사 비용으로 O(n²).
- 🐛 값 중복이 있으면 유일하게 복원 불가 (문제 전제: unique).
- 🔑 "construct/rebuild tree from traversals" → **preorder(또는 postorder)로 root, inorder로 분할**. (preorder+postorder만으로는 일반적으로 유일하지 않음)

---

## 15. Serializing and Deserializing Binary Tree

**문제:** 이진 트리 ↔ 문자열 변환 함수 쌍 `serialize`, `deserialize` 작성. 형식은 자유, 왕복하면 원래 트리가 나와야 함. 예: root 1(왼 2(4,5), 오 3) → `"1 2 4 x x 5 x x 3 x x"` → 같은 트리.

**핵심 관찰**
- **preorder + null 마커(`x`)** 를 쓰면 트리가 유일하게 결정된다 (null 표시가 leaf/비-leaf 경계를 알려줌 → inorder 불필요).
- 역직렬화는 **같은 preorder 순서로 토큰을 하나씩 소비**하는 DFS. 공유 iterator가 "다음 토큰 위치" 상태를 들고 다님.

```python
def serialize(root):
    out = []
    def dfs(node):
        if not node:
            out.append("x"); return
        out.append(str(node.val))
        dfs(node.left); dfs(node.right)
    dfs(root)
    return " ".join(out)

def deserialize(s):
    tokens = iter(s.split())
    def dfs():
        val = next(tokens)
        if val == "x":
            return None
        node = Node(int(val))
        node.left = dfs()          # 순서 중요: 왼쪽 먼저 소비
        node.right = dfs()
        return node
    return dfs()
```

**복잡도:** 시간 O(n). 공간: serialize O(h) 스택, deserialize O(n) (노드 생성) + O(h) 스택.

- 🐛 null 마커를 빼면 복원 불가 (preorder만으로는 모양이 안 정해짐).
- 🐛 구분자 없이 이어 붙이면 `12`가 `1`,`2`인지 모름 → 구분자 필수. 음수 값 파싱도 확인.
- ✅ BFS(레벨 순서)로 직렬화하는 LeetCode 스타일도 가능 — 면접에선 설명하기 쉬운 쪽 선택.
- 🔑 "serialize / encode tree to string / 저장 후 복원" → **preorder + null 마커 + iterator DFS**.

---

## 16. Lowest Common Ancestor

**문제:** 일반 이진 트리(BST 아님)에서 두 노드 node1, node2의 LCA. 값 유일, 두 노드 모두 존재 보장, 노드는 자기 자신의 자손. 예: root 5, 왼쪽 서브트리에 1, 오른쪽에 8 → `5`.

**핵심 관찰**
- 부모 포인터가 없으니 아래→위로 정보를 올리려면 **postorder DFS**.
- 반환값 의미: "이 서브트리에서 찾은 target(또는 이미 확정된 LCA), 없으면 None".
- 경우 분석:
  1. node가 None → None.
  2. node가 node1 또는 node2 → **바로 자신 반환** (다른 target이 아래에 있어도 LCA는 나 → 더 볼 필요 없음; 존재 보장 덕분).
  3. 그 외: 왼·오 결과가 **둘 다 non-null → 내가 LCA**, 하나만 non-null → 그걸 그대로 위로 전달, 둘 다 null → None.

```python
def lca(root, node1, node2):
    if not root:
        return None
    if root is node1 or root is node2:
        return root
    left = lca(root.left, node1, node2)
    right = lca(root.right, node1, node2)
    if left and right:
        return root          # 양쪽에서 하나씩 → 분기점
    return left or right     # 한쪽 결과를 올려보냄 (둘 다 None이면 None)
```

**복잡도:** 시간 O(n), 공간 O(h) (최악 O(n)).

- 🐛 case 2의 조기 반환은 "**둘 다 존재**" 가정이 있어야 정답. 존재 보장이 없으면 찾은 개수를 따로 세야 함 (LeetCode 1644 변형).
- 🐛 값 비교(`root.val == node1.val`) vs 노드 동일성(`is`) — 문제 입력 형식 확인.
- 🔑 "LCA in binary tree" (BST 아님) → **postorder, 양쪽 non-null이면 현재 노드**. BST면 13번의 값 비교로 O(h).

---

## 용어·패턴 사전

| 용어 | 뜻 |
|---|---|
| Base case | 재귀가 더 호출하지 않고 바로 답을 내는 종료 조건 |
| Call stack / stack frame | 함수 호출마다 인자·지역변수를 쌓는 스택 / 그 한 칸 |
| DFS | 깊이 먼저 내려갔다 되돌아오는 탐색. 트리에선 preorder와 같음 |
| Backtracking | 탐색 후 되돌아오는 동작. 보통 조합 탐색 DFS를 지칭 |
| Divide & Conquer | 같은 종류의 하위 문제로 나눠 풀고 결과를 합침 |
| Preorder / Inorder / Postorder | 현재 노드를 자식 전 / 사이 / 후에 방문 |
| 반환값 (return value) | 자식 → 부모로 올리는 정보 (높이, 개수, 찾은 노드) |
| State (함수 인자) | 부모 → 자식으로 내리는 정보 (경로 최대값, 허용 범위) |
| Height / Depth | 서브트리 root→가장 먼 leaf 간선 수 / 트리 root→해당 노드 거리 |
| Full / Complete / Perfect | 자식 0or2 / 마지막 레벨 빼고 꽉+왼쪽 정렬(heap) / 전부 꽉 참(2^h−1) |
| Balanced tree | 모든 노드에서 좌우 높이 차 ≤ 1 → 높이 O(log n) |
| BST | 왼쪽 전부 < 노드 < 오른쪽 전부. inorder = 정렬 |
| AVL / Red-black | 회전으로 균형을 유지하는 self-balancing BST |
| LCA | 두 노드를 모두 자손으로 갖는 가장 깊은 노드 (자기 자신 포함) |
| Split point | BST LCA에서 p, q가 좌우로 갈라지는 노드 |
| Serialize / Deserialize | 트리 ↔ 문자열. preorder + null 마커(`x`) |
| Skewed tree | 한쪽으로 치우쳐 연결 리스트처럼 된 트리 → 높이 O(n) |
