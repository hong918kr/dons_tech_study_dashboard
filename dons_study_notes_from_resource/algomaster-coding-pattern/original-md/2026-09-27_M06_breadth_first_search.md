# Module 6 — Breadth First Search (너비 우선 탐색)

> 출처: AlgoMonster Coding Patterns, Module 6 (레슨 5개) · 한국어 개념 정리 노트
> 목표: 트리에서 **queue로 레벨 단위 순회**를 자유자재로 짜고, "레벨/가장 가까운/최소 깊이"류 문제를 보면 BFS를 즉시 떠올리기.

## 한눈에 보기 (5분 복습용)

| 패턴/문제 | 언제 쓰나(키워드) | 핵심 아이디어 | 복잡도 |
|---|---|---|---|
| BFS 기본 | 가까운 노드 먼저, 조기 종료 | `deque` + popleft 후 자식 append | O(n) / O(W) |
| Level Order | "level by level", 레벨별 리스트 | 루프마다 `len(queue)` 스냅샷 → n개만 꺼내기 | O(n) / O(n) |
| ZigZag | 레벨마다 방향 교대 | enqueue 순서 고정, **출력만** `appendleft`/reverse + flag | O(n) |
| Right Side View | 레벨당 하나(첫/끝) | 오른쪽 자식 먼저 넣고 `queue[0]` 기록 | O(n) |
| Min Depth | shallowest leaf, nearest | 레벨 BFS + depth, 첫 leaf에서 즉시 return | O(n) 최악 |

**BFS = queue(FIFO).** queue엔 최대 두 레벨만 공존 → 레벨 시작 시 `len(queue)` = 그 레벨 크기.

**BFS vs DFS:** 답이 루트 **가까이** → BFS(찾자마자 종료), 답이 **멀리/경로 전체** → DFS(메모리 O(h)).

**Python에선 반드시 `collections.deque`** — `list.pop(0)`은 O(n).

---

## 1. BFS Intro

> 섹션: Introduction

### 핵심 개념
- **DFS**: 자식(깊이) 먼저 끝까지 → 재귀/stack.
- **BFS**: 같은 레벨(너비)을 전부 방문한 뒤 다음 레벨 → **queue (FIFO)**. 노드를 꺼낼 때 그 자식들을 넣는다.

### 왜/언제
| DFS가 유리 | BFS가 유리 |
|---|---|
| 루트에서 **먼** 노드 찾기, 경로 전체 탐색, 백트래킹 | 루트에서 **가까운(가장 가까운)** 노드 찾기, 레벨 단위 처리, 최단 거리(무가중치) |
- BFS는 가까운 순서로 방문하므로 **처음 조건을 만족한 노드 = 가장 얕은 노드** → 조기 종료 가능.

### 템플릿
```python
from collections import deque

def bfs(root):
    queue = deque([root])            # 시작 원소가 최소 하나 있어야 함
    while queue:
        node = queue.popleft()       # dequeue
        if OK(node):                 # 조건 만족 시 조기 반환
            return node
        for child in node.children:  # 꺼낸 직후 자식 enqueue
            queue.append(child)
    return None
```

### 흔한 실수
- 🐛 Python에서 `list.pop(0)` 사용 → O(n). 반드시 `collections.deque.popleft()` (O(1)).
- 🐛 root가 `None`인 경우 queue에 None을 넣고 시작 → 자식 접근 시 에러.
- 🎤 "BFS vs DFS 공간 복잡도?" → BFS는 가장 넓은 레벨 폭 W만큼(완전 이진 트리면 ~n/2), DFS는 높이 h만큼.

---

## 2. Binary Tree Level Order Traversal

> 섹션: BFS on Tree

**문제:** 이진 트리의 **레벨별 값 목록**을 반환 (i번째 리스트 = 깊이 i의 노드들, 왼→오). 예: 루트 1, 자식 2·3, 손자 4·5 → `[[1],[2,3],[4,5]]`.

**핵심 관찰:** queue의 노드들은 자기 레벨을 모른다. 하지만
- FIFO 특성상 queue에는 **최대 두 레벨**만 공존한다 (레벨 k를 다 꺼내기 전엔 k+2가 들어올 수 없음).
- 한 레벨의 첫 노드를 꺼내기 직전엔 queue에 **정확히 그 레벨만** 있다 → 그 순간 `n = len(queue)`를 저장하고 **n개만** 꺼내면 한 레벨.

**풀이**
1. root 없으면 `[]`.
2. 루프마다 `n = len(queue)` 스냅샷 → n번 popleft하며 값 수집 + 자식 enqueue.
3. 수집한 레벨을 결과에 추가.

```python
from collections import deque

def level_order_traversal(root) -> list[list[int]]:
    if root is None:
        return []
    res, queue = [], deque([root])
    while queue:
        level = []
        for _ in range(len(queue)):       # 이번 레벨 노드 수만큼만
            node = queue.popleft()
            level.append(node.val)
            for child in (node.left, node.right):
                if child:
                    queue.append(child)
        res.append(level)
    return res
```

**복잡도:** 시간 O(n) (노드·간선 각 1회), 공간 O(n) (queue 최대 폭).

- 🐛 `for _ in range(len(queue))`는 루프 시작 시 한 번만 평가되므로 안전. 반면 `while` 안에서 매번 `len(queue)`를 다시 보면 다음 레벨이 섞인다.
- ✅ DFS + depth 인자로도 가능(`res[depth].append`)하지만 BFS가 자연스럽다.
- 🔑 "level by level", "각 레벨의 ~", "레벨 평균/최댓값" → **레벨 스냅샷 BFS**. 이 모듈 나머지 문제는 전부 이 템플릿의 변형.

---

## 3. Binary Tree ZigZag Level Order Traversal

**문제:** 레벨 순회를 하되 **레벨마다 방향을 번갈아** (0레벨 왼→오, 1레벨 오→왼, …). 예: `1 / 2 3 / 4 5 6 7` → `[[1],[3,2],[4,5,6,7]]`.

**핵심 관찰:** queue에 넣는 순서(왼 자식 → 오른 자식)는 **절대 바꾸지 않는다**. 바꾸는 건 **결과 리스트에 담는 방향**뿐. → 레벨 순회 + 방향 flag 하나.

**풀이**
1. 레벨 스냅샷 BFS 그대로.
2. 레벨 값을 `deque`에 담되 flag가 True면 `append`, False면 `appendleft` (또는 리스트 후 `reverse()`).
3. 레벨이 끝날 때마다 flag 뒤집기.

```python
from collections import deque

def zig_zag_traversal(root) -> list[list[int]]:
    if root is None:
        return []
    res, queue, left_to_right = [], deque([root]), True
    while queue:
        level = deque()
        for _ in range(len(queue)):
            node = queue.popleft()
            if left_to_right:
                level.append(node.val)
            else:
                level.appendleft(node.val)
            if node.left:
                queue.append(node.left)
            if node.right:
                queue.append(node.right)
        res.append(list(level))
        left_to_right = not left_to_right   # 레벨마다 방향 전환
    return res
```

**복잡도:** 시간 O(n), 공간 O(n).

- 🐛 자식 enqueue 순서까지 뒤집으면 다음 레벨 순서가 꼬인다 — 방향은 **출력에만** 적용.
- 🐛 root가 None일 때 방어 (원본 코드처럼 None을 queue에 넣으면 `.val` 접근 에러).
- ✅ `appendleft`는 O(1); 리스트 `insert(0, x)`는 O(k)라 피한다. `reverse()`도 레벨당 O(k)라 총 O(n)으로 괜찮음.
- 🔑 "zigzag", "alternating direction", "spiral level order" → 레벨 BFS + flag.

---

## 4. Binary Tree Right Side View

**문제:** 트리를 오른쪽에서 봤을 때 보이는 노드, 즉 **각 레벨의 가장 오른쪽 노드** 값을 위에서부터 반환. 예: `1 / 2 3 / 4 . . .`(4는 2의 왼자식) → `[1, 3, 4]`.

**핵심 관찰:** 레벨 순회에서 각 레벨의 **하나만** 뽑으면 된다.
- 방법 A (강의): 자식을 **오른쪽 먼저** enqueue → 레벨 시작 시 `queue[0]`이 가장 오른쪽.
- 방법 B: 평소대로 왼→오 enqueue, 레벨의 **마지막**으로 꺼낸 노드를 기록.

**풀이 (A)**
1. 레벨 시작 때 `queue[0].val`을 결과에 추가.
2. 레벨의 n개를 꺼내며 오른쪽 → 왼쪽 순서로 자식 enqueue.

```python
from collections import deque

def binary_tree_right_side_view(root) -> list[int]:
    if root is None:
        return []
    res, queue = [], deque([root])
    while queue:
        res.append(queue[0].val)            # 오른쪽부터 넣었으니 맨 앞이 rightmost
        for _ in range(len(queue)):
            node = queue.popleft()
            if node.right:
                queue.append(node.right)
            if node.left:
                queue.append(node.left)
    return res
```

**복잡도:** 시간 O(n), 공간 O(n).

- 🐛 "오른쪽 자식만 따라 내려가기"는 **틀림** — 오른쪽 서브트리가 얕으면 더 깊은 레벨의 rightmost는 왼쪽 서브트리에 있다 (위 예시의 4).
- ✅ DFS 대안: 오른쪽 먼저 방문하며 `depth == len(res)`일 때 추가 (각 깊이 첫 방문 = rightmost).
- 🔑 "right side view", "각 레벨의 첫/마지막 노드", "left side view" → 레벨 BFS에서 한 개만 뽑기.

---

## 5. Binary Tree Min Depth

**문제:** 이진 트리에서 **가장 얕은 leaf의 깊이** (루트 깊이 = 0 기준). 예: `1 / 2 3 / 4 5`(4,5는 2의 자식) → leaf 3이 깊이 1 → `1`.

**핵심 관찰:** DFS는 트리 전체를 돌며 최소를 갱신해야 하지만, BFS는 레벨 순으로 보므로 **처음 만난 leaf가 곧 최소 깊이** → 즉시 반환. (BFS가 DFS보다 확실히 유리한 대표 사례)

**풀이**
1. 레벨 스냅샷 BFS + `depth` 카운터 (레벨 시작마다 +1, 루트가 0이 되도록 −1에서 시작).
2. 꺼낸 노드가 leaf(`left`, `right` 모두 None)면 그 자리에서 `depth` 반환.

```python
from collections import deque

def binary_tree_min_depth(root) -> int:
    if root is None:
        return 0
    queue, depth = deque([root]), -1
    while queue:
        depth += 1
        for _ in range(len(queue)):
            node = queue.popleft()
            if not node.left and not node.right:   # 첫 leaf = 최소 깊이
                return depth
            if node.left:
                queue.append(node.left)
            if node.right:
                queue.append(node.right)
    return depth
```

**복잡도:** 시간 O(n) 최악(보통은 조기 종료로 더 빠름), 공간 O(n).

- 🐛 자식이 하나뿐인 노드는 **leaf가 아니다**. DFS로 `1 + min(left, right)`를 쓰면 None 쪽 0을 골라 틀림 — BFS는 leaf 판정이 명확해 이 함정이 없다.
- 🐛 깊이 정의 확인: 이 강의는 루트 = 0, LeetCode 111은 루트 = 1 (노드 수). 면접에서 먼저 물어보기.
- 🎤 "BFS가 DFS보다 나은 경우?" → 답이 **루트 가까이** 있을 때: 최소 깊이, 무가중치 최단 경로 — 찾자마자 멈출 수 있다.
- 🔑 "shallowest", "minimum depth", "nearest", "fewest steps" → BFS + 조기 종료.

---

## 용어·패턴 사전

| 용어 | 뜻 |
|---|---|
| BFS (Breadth First Search) | 같은 깊이의 노드를 모두 방문한 뒤 다음 깊이로 가는 순회 |
| DFS (Depth First Search) | 한 경로를 끝까지 내려간 뒤 되돌아오는 순회 (재귀/stack) |
| queue / FIFO | 먼저 넣은 것이 먼저 나오는 자료구조. BFS의 핵심 |
| deque | 양끝 O(1) 삽입/삭제 큐. `popleft`, `appendleft` |
| level (depth) | 루트로부터의 간선 수. 루트 = 0 (문제마다 정의 확인) |
| level snapshot | 레벨 시작 시 `len(queue)`를 고정해 그 레벨만 처리하는 기법 |
| leaf | 자식이 하나도 없는 노드 (자식 1개면 leaf 아님) |
| early return | 조건을 처음 만족하는 순간 반환. BFS에선 그게 최소 깊이 |
| right side view | 각 레벨의 가장 오른쪽 노드 모음 |
| zigzag traversal | 레벨마다 좌→우 / 우→좌를 번갈아 출력하는 순회 |
