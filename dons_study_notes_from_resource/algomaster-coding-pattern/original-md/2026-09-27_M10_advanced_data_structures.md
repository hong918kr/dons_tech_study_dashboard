# Module 10 — Advanced Data Structures (고급 자료구조: Union-Find·Trie·LRU Cache·Segment Tree)

> 출처: AlgoMonster Coding Patterns, Module 10 (레슨 15개) · 한국어 개념 정리 노트
> 목표: 연결성은 **DSU**, 접두사는 **Trie**, O(1) 캐시는 **HashMap + 이중 연결 리스트**, 구간 질의+갱신은 **Segment Tree** — 문제 문구를 보고 바로 자료구조를 떠올리고 템플릿을 5분 안에 쓰기.

## 한눈에 보기 (5분 복습용)

| 패턴 | 언제 쓰나 (키워드) | 핵심 아이디어 | 복잡도 |
|---|---|---|---|
| Union-Find (DSU) | "같은 그룹?", 컴포넌트 수, 사이클 판정, 공유하면 병합 | parent 포인터 트리 숲, union = root 하나를 다른 root 밑에 | 압축+rank: amortized O(α(n)) |
| DSU + root 메타데이터 | 그룹 크기/합/최대 | size 등을 **root에만** 저장, union 때 합산 | 위와 동일 |
| DSU + 그룹핑 | 계정 병합, 동치류 출력 | 노드 키에 제약 섞기 `(name,email)`, 마지막에 root별 묶기 | O((E+U) log U) |
| 역순 DSU | 간선을 **제거**하며 매번 연결성 질의 | 시간 역행 → 제거가 추가가 됨, union 전 기록 후 뒤집기 | O(m log n) |
| Trie + freq | prefix로 시작하는 단어 수, autocomplete | 노드마다 지나간 단어 수, freq==1이면 유일 | 연산당 O(L) |
| Trie + wildcard DFS | `.` 패턴 검색 | 문자는 1갈래, `.`은 모든 자식 분기, `is_end` 확인 | O(L) ~ O(26^w) |
| Trie + grid backtracking | 보드에서 여러 단어 찾기 | 격자 DFS와 트라이 하강 동시 진행, 자식 없으면 prune | 트라이 가지치기로 대폭 감소 |
| LRU Cache | O(1) get/put + 가장 오래된 것 제거 | HashMap(key→node) + 이중 연결 리스트 + sentinel | O(1) / 공간 O(cap) |
| Segment Tree | 구간 합/최대/최소 + 점 갱신 섞임 | 중첩 구간 캐시, 밖/안/부분 3경우, 4n flat array | query·update O(log n) |

**연결성 = DSU, prefix = Trie, O(1) 캐시 = Hash + DLL, 구간 질의+갱신 = Segment Tree.** 키워드 → 자료구조 매핑을 먼저 떠올리기.

**DSU 두 줄 암기:** `find`에 path compression 한 줄, `union`은 root 두 개를 **맨 위에서 한 번** 계산 후 `rx == ry`면 early return.

**Segment Tree 일반화:** merge 연산과 "완전히 밖"의 항등원만 바꾸면 sum → max(−∞) → min(+∞) → gcd(0).

**삭제가 어려우면 시간을 뒤집어라** — DSU는 split이 없으므로 오프라인이면 역순 union.

---

> 섹션: Disjoint Set Union | Union Find

## 1. DSU/Union Find Fundamentals

### 핵심 개념
- 상황: n대의 컴퓨터에 케이블이 하나씩 깔리며 클러스터가 생긴다. 계속 묻는 질문 두 개 — ① x와 y가 같은 클러스터인가? ② 새 케이블이 깔리면 두 클러스터를 한 번에 합칠 수 있나?
- **hash set으로 클러스터 저장?** 조회는 빠르지만 merge가 O(n) (한쪽 원소를 전부 복사). merge가 많으면 복사가 전체 비용을 지배.
- **대표원소(representative):** 클러스터마다 원소 하나를 ID로. "같은 클러스터?" = "대표가 같은가?"
- 원소→대표 **직접 포인터**면 merge 때 또 전부 고쳐야 함 → 대신 **parent 포인터(간접 참조)**. 위로 따라가면 결국 대표(=자기 자신이 parent인 root)에 도착.
- 결과 구조 = **트리들의 숲(forest)**. 클러스터 하나 = 트리 하나, root = 대표. merge = root 하나를 다른 root 밑에 붙이는 **포인터 1개 변경**.

### 연산
- `parent[i] = i`로 초기화 (n개의 단일 노드 트리).
- `find(x)`: parent를 따라 올라가 root 반환. `find(x) == find(y)` ⇔ 같은 집합.
- `union(x, y)`: 두 root를 한 번씩만 계산해 `rx == ry`면 no-op, 아니면 `parent[rx] = ry`.
- 원소가 연속 정수가 아니면 배열 대신 **dict + add(x)** (처음 볼 때 `parent[x] = x`).

```python
class UnionFind:
    def __init__(self, n: int):
        self.parent = list(range(n))

    def find(self, x: int) -> int:
        while self.parent[x] != x:
            x = self.parent[x]
        return x

    def union(self, x: int, y: int) -> None:
        rx, ry = self.find(x), self.find(y)
        if rx != ry:
            self.parent[rx] = ry
```

- 추적 예: n=6, `union(3,1) union(1,0) union(5,4) union(2,0)` → `parent=[0,0,0,1,4,4]` → 트리 {0,1,2,3}(root 0), {4,5}(root 4).

### 언제 쓰나
- 연결성 질문: 같은 컴포넌트? 컴포넌트 개수? 이 간선이 사이클을 만드나? **Kruskal MST**의 핵심 부품.

### 흔한 실수 / 성능
- 🐛 최적화 없는 find는 트리 깊이만큼 걸림. `union(0,1), union(1,2), union(2,3)`처럼 체인이 되면 find가 **O(n)** → 다음 레슨의 최적화 필요.
- 🐛 union에서 root가 아니라 **x 자체의 parent**를 바꾸는 실수 (`parent[x] = y`) → 집합이 쪼개짐.
- 🎤 "왜 Union Find?" → merge를 포인터 1개로 만들기 위해 대표원소 + parent 포인터 간접 참조를 쓰는 트리 숲.

---

## 2. DSU Optimizations: Path Compression and Union by Rank

### 문제: 체인
- `union(0,1), union(1,2), union(2,3), union(3,4)` → 0→1→2→3→4 체인, root 4. `find(0)`이 4홉. n개면 find당 **O(n)**.

### Path compression (경로 압축) — find에 한 줄 추가
- 표현 규칙은 "parent 포인터가 같은 클러스터 안에 있고 결국 root로 간다"뿐 → **root를 직접 가리키는 게 가장 짧은 합법 형태**.
- 재귀가 root에서 돌아오면서 경로상 모든 노드의 parent를 root로 덮어씀. 체인에서 `find(0)` 한 번 → 0,1,2,3 모두 parent=4 (깊이 2 트리).
- 첫 호출은 비싸도 뒤따르는 호출이 모두 1홉 → **amortized O(log n)**.

### Union by rank (선택, 고급) — merge 시점에 균형
- 압축은 "사후 수리", rank는 "예방". `rank[r]` = r 트리 깊이의 상한.
- 낮은 rank의 root를 높은 쪽 밑에 → 깊이 안 늘어남. **동률일 때만** 한쪽에 붙이고 그 root의 rank += 1.
- 🐛 `rx, ry`를 맨 위에서 한 번 계산해 재사용. parent를 바꾼 뒤 `rank[find(x)]`를 읽으면 엉뚱한 칸을 읽는다.

```python
class UnionFind:
    def __init__(self, n: int):
        self.parent = list(range(n))
        self.rank = [0] * n

    def find(self, x: int) -> int:
        if self.parent[x] != x:
            self.parent[x] = self.find(self.parent[x])   # path compression
        return self.parent[x]

    def union(self, x: int, y: int) -> bool:
        rx, ry = self.find(x), self.find(y)
        if rx == ry:
            return False
        if self.rank[rx] < self.rank[ry]:
            rx, ry = ry, rx
        self.parent[ry] = rx                             # 낮은 쪽을 높은 쪽 밑에
        if self.rank[rx] == self.rank[ry]:
            self.rank[rx] += 1
        return True
```

### 복잡도
| 버전 | find/union |
|---|---|
| 최적화 없음 | 최악 O(n) |
| path compression만 | amortized O(log n) |
| 압축 + rank | amortized **O(α(n))** — 역 Ackermann, 현실의 모든 n에서 ≤ 4 → 사실상 상수 |

- α(n)은 코드에서 계산하는 값이 아니라 분석에서만 등장. 두 최적화가 **둘 다** 있어야 α(n).
- ✅ 체크포인트: 면접에선 **path compression 한 줄이면 거의 충분**. "가장 tight한 bound?"를 물으면 rank 추가.
- 🐛 Python 재귀 find는 깊은 체인에서 recursion limit 위험 → 반복문 버전(`while` + 2-pass 또는 path halving `parent[x] = parent[parent[x]]`)도 알아두기.
- 🎤 "DSU 복잡도?" → "path compression + union by rank면 연산당 amortized O(α(n)), 실질 상수."

---

## 3. DSU Introductory Problem

**문제:** 정수 라벨에 대한 연산 스트림 처리. `merge(x, y)`는 두 집합 합치기, `is_same(x, y)`는 같은 집합인지 반환. 라벨은 **아무 연산에서나 처음 등장**할 수 있음 (0..n-1 가정 불가).
- 예: `union 1 2`, `union 2 3`, `is_same 1 3 → true`, `is_same 2 4 → false`.

**핵심 관찰:** 원소 집합을 미리 모르므로 **parent를 배열 대신 dict**로. `find`에서 처음 보는 x는 자기 자신을 parent로 등록(lazy add).

**풀이**
1. `find(x)`: x가 dict에 없으면 `parent[x] = x`. 이후 path compression으로 root 반환.
2. `merge`: 두 root가 다르면 한쪽을 다른 쪽 밑에.
3. `is_same`: `find(x) == find(y)`.

```python
class SameSet:
    def __init__(self):
        self.parent = {}

    def find(self, x: int) -> int:
        p = self.parent.setdefault(x, x)
        if p != x:
            self.parent[x] = self.find(p)
        return self.parent[x]

    def merge(self, x: int, y: int) -> None:
        rx, ry = self.find(x), self.find(y)
        if rx != ry:
            self.parent[rx] = ry

    def is_same(self, x: int, y: int) -> bool:
        return self.find(x) == self.find(y)
```

**복잡도:** 연산당 amortized O(log n) (압축만) / O(α(n)) (rank 추가). 공간 O(등장한 라벨 수).

- 🐛 `is_same`에서 처음 보는 4 같은 라벨도 KeyError 없이 자기 자신 집합으로 처리해야 함 → `setdefault`.
- 🔑 "라벨이 임의 정수/문자열", "스트림으로 합치고 질의" → **dict 기반 DSU**.

---

## 4. Size of Connected Components

**문제:** `merge(x, y)`로 합치고 `count(x)`로 x가 속한 집합의 **크기**를 반환. 처음 보는 값은 크기 1인 단독 집합.
- 예: `union 1 2`, `union 2 3`, `count 3 → 3`, `count 4 → 1`.

**핵심 관찰:** 크기는 **root에만 저장**. root가 곧 클러스터 전체를 대표하므로 `sizes[r]`는 r이 root일 때만 의미 있음 (불변식).

**풀이**
1. `_ensure(x)`: 처음 보면 `parent[x]=x`, `sizes[x]=1`. `find` 시작 시 항상 호출.
2. `merge`: root가 같으면 끝. 다르면 `parent[rx]=ry`, `sizes[ry] += sizes[rx]`.
3. `count(x)` = `sizes[find(x)]`.
- path compression은 non-root의 parent만 바꾸므로 root의 size 불변식을 깨지 않음.

```python
class SetCounter:
    def __init__(self):
        self.parent, self.sizes = {}, {}

    def find(self, x: int) -> int:
        if x not in self.parent:
            self.parent[x], self.sizes[x] = x, 1
        if self.parent[x] != x:
            self.parent[x] = self.find(self.parent[x])
        return self.parent[x]

    def merge(self, x: int, y: int) -> None:
        rx, ry = self.find(x), self.find(y)
        if rx == ry:
            return
        if self.sizes[rx] > self.sizes[ry]:      # union by size (보너스)
            rx, ry = ry, rx
        self.parent[rx] = ry
        self.sizes[ry] += self.sizes[rx]

    def count(self, x: int) -> int:
        return self.sizes[self.find(x)]
```

**복잡도:** q개 연산 O(q log n) (압축만) / union by size·rank 추가 시 O(q·α(n)). 공간 O(n).

- 🐛 `rx == ry`일 때 size를 더하면 **두 배로 부풀어 오름** → 반드시 early return.
- 🐛 size를 non-root에서 읽으면 stale 값 → 항상 `find` 후 root에서 읽기.
- 💡 size는 "union by size" 기준으로도 그대로 재사용 가능 (rank 대체).
- 🔑 "가장 큰 그룹 크기", "x와 연결된 원소 수" → **root에 메타데이터(size/sum/max)를 저장하는 DSU**.

---

## 5. Merge User Accounts

**문제:** 각 행 = `[이름, email...]`. **이름이 같고** 이메일 공유로 (전이적으로) 연결되면 같은 사람 → 병합. 이름이 다르면 같은 이메일이어도 병합 X. 결과는 행 내 이메일 정렬, 행은 (이름, 첫 이메일) 순 정렬. (LeetCode 721 변형)
- 예: John[jsmith, j_ny], John[jsmith, j_work], Mary[mary], John[johnny] → John[j_ny, j_work, jsmith], John[johnny], Mary[mary].

**핵심 관찰**
- "이메일 공유" = 연결성 → DSU. 노드 = 이메일.
- 이름 조건 때문에 노드를 bare email이 아니라 **(name, email) 튜플**로 → 이름 다르면 자동 분리.
- 한 행의 이메일들은 **첫 번째를 anchor**로 두고 나머지를 anchor와 union (전이성 덕에 행당 k-1번이면 충분).

**풀이**
1. Phase 1: 모든 행에서 (name,email) 노드 등록 + anchor와 union.
2. Phase 2: 모든 노드를 `find` root별로 그룹핑.
3. Phase 3: 그룹마다 `[name] + sorted(emails)`, 전체를 `(name, first_email)`로 정렬.

```python
from collections import defaultdict

def merge_accounts(accounts: list[list[str]]) -> list[list[str]]:
    parent = {}
    def find(x):
        parent.setdefault(x, x)
        if parent[x] != x:
            parent[x] = find(parent[x])
        return parent[x]

    for name, *emails in accounts:
        anchor = (name, emails[0])
        find(anchor)                                  # 이메일 1개짜리 행도 등록
        for e in emails[1:]:
            parent[find((name, e))] = find(anchor)

    groups = defaultdict(list)
    for node in parent:
        groups[find(node)].append(node[1])
    res = [[root[0]] + sorted(es) for root, es in groups.items()]
    return sorted(res, key=lambda a: (a[0], a[1]))
```

**복잡도:** E = 이메일 총 개수, U = 서로 다른 (name,email) 수 → O((E + U) log U) 시간, O(U) 공간.

- 🐛 이메일만 노드로 쓰면 John/Mary가 같은 이메일로 잘못 합쳐짐 (이 문제의 함정).
- 🐛 이메일 1개짜리 행도 노드로 **등록**해야 출력에서 빠지지 않음.
- 🐛 그룹핑 전 `find`를 반드시 다시 호출 (parent 값이 root가 아닐 수 있음).
- 🔑 "공유하면 같은 그룹 (전이적)", "그룹으로 묶어서 출력" → **DSU + root별 그룹핑**. 노드 키에 제약 조건을 섞어 넣는 트릭.

---

## 6. Number of Connected Components

**문제:** 노드 0..n-1이 모두 떨어진 상태에서 간선을 하나씩 추가. **매 간선 후** 연결 컴포넌트 개수를 기록해 리스트로 반환.
- 예: n=5, `[[1,2],[2,3],[1,3],[0,4],[0,4]]` → `[4,3,3,2,2]`.

**핵심 관찰:** 간선 하나는 둘 중 하나 — ① 이미 같은 컴포넌트(중복/사이클) → 개수 그대로, ② 서로 다른 컴포넌트를 합침 → **정확히 1 감소**. "같은 root?"가 DSU의 find.

**풀이**
1. `components = n`.
2. 각 간선 (a,b): `find(a) != find(b)`면 union + `components -= 1`.
3. union 여부와 상관없이 매번 `components` 기록.

```python
def number_of_connected_components(n: int, connections: list[list[int]]) -> list[int]:
    parent = list(range(n))
    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]     # path halving
            x = parent[x]
        return x

    comps, res = n, []
    for a, b in connections:
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb
            comps -= 1
        res.append(comps)
    return res
```

**복잡도:** m개 간선 O(m log n) amortized (압축), 공간 O(n).

- 🐛 `[1,3]`처럼 사이클을 닫는 간선, 중복 간선 `[0,4]` 두 번 → 감소시키면 안 됨.
- 💡 같은 체크로 **"사이클을 만드는 첫 간선"**(Redundant Connection)도 바로 찾음: `ra == rb`인 간선이 그것.
- 🔑 "간선이 하나씩 추가되며 매번 컴포넌트 수", "온라인 연결성" → **DSU + 카운터** (DFS는 매번 재계산해야 해서 불리).

---

## 7. Umbristan | Reverse Union Find

**문제:** 도시 1..n이 `breaks`의 간선들로 연결된 상태에서 시작해, 간선을 **하나씩 제거**. 매 제거 후 남은 클러스터(연결 컴포넌트) 수를 반환. (전체 간선 = breaks 목록 전부)
- 예: n=4, breaks=`[[1,2],[2,3],[3,4],[1,4],[2,4]]` → `[1,1,2,3,4]`.

**핵심 관찰**
- DSU는 union은 싸지만 **split이 없다**. 어떤 간선이 클러스터를 지탱하는지 기록하지 않으므로, 제거 시 끊어지는지 알려면 남은 간선 전부를 봐야 함 → 제거당 O(n).
- **시간을 거꾸로 돌리기:** 모든 간선 제거 후 = n개의 고립 도시(자명). 역순으로 보면 제거가 **추가**가 되고 → 앞 레슨(Number of Connected Components) 그대로.

**풀이**
1. breaks를 뒤집고 `clusters = n`에서 시작.
2. 각 간선마다 **union 전에** 현재 clusters를 기록 (그 값이 정방향에서 이 간선을 제거한 **후**의 상태).
3. root가 다르면 union + `clusters -= 1`.
4. 기록 리스트를 뒤집어 반환.

```python
def umbristan(n: int, breaks: list[list[int]]) -> list[int]:
    parent = list(range(n + 1))               # 도시 1..n
    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    clusters, res = n, []
    for a, b in reversed(breaks):
        res.append(clusters)                  # union 전에 기록!
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb
            clusters -= 1
    return res[::-1]
```

- 추적: 역순 [2,4]→기록4, [1,4]→3, [3,4]→2, [2,3]→1(이미 연결), [1,2]→1 → `[4,3,2,1,1]` 뒤집어 `[1,1,2,3,4]`.

**복잡도:** O(m log n) amortized, 공간 O(n).

- 🐛 기록 시점: union **후**가 아니라 **전**. (정방향 i번째 제거 후 상태 = 역방향에서 그 간선을 넣기 직전 상태)
- 🐛 라벨이 1..n이면 parent 크기 n+1. 원본 간선 중 **절대 제거되지 않는 간선**이 있는 변형이면 그것부터 미리 union.
- 💡 진짜 삭제를 지원하는 구조(Link-Cut Tree)는 면접 범위 밖 → 역순 처리가 기대 답.
- 🔑 "간선/칸을 **제거**하면서 매번 연결 상태 질의", "offline으로 전체 연산을 미리 앎" → **역순 DSU (reverse union-find)**.

---

> 섹션: Trie

## 8. Trie Introduction

### 핵심 개념
- 상황: 단어 50만 개 사전. 사용자가 타이핑할 때마다 "이 prefix로 시작하는 단어 몇 개?" (중복 포함). prefix는 매 키 입력마다 바뀜.
- **hash set?** 전체 단어 조회는 O(L)이지만 "ca"는 키가 아님 → 전부 스캔 O(n·L).
- **정렬 + 이진 탐색?** "ca" 이상 첫 단어와 "cb" 이상 첫 단어의 인덱스 차 → O(L log n) 조회는 OK, 하지만 삽입이 O(n).
- 공통 약점: 단어를 통째 문자열로 보고 **공유 prefix를 매번 다시 비교**. → prefix를 **정확히 한 번만 저장**하는 구조 = **Trie**.
- Trie: root는 빈 노드, 간선 = 문자 1개, root에서의 경로 = prefix. ["cat","cap","dog"] → c→a→{t,p}, d→o→g. "ca"는 한 번만 저장되어 두 단어가 공유.

### prefix 개수 세기: `freq`
- 노드마다 `freq` = **이 노드를 지나간 삽입 단어 수**.
- insert: root에서 내려가며 없으면 자식 생성, 들어간 노드마다 `freq += 1` (root 제외).
- query: 같은 경로를 **생성 없이** 따라가다 끊기면 0, 끝까지 가면 그 노드의 freq.
- 합산이므로 **중복 삽입이 자동 처리**됨 ("dog" 두 번 → 경로 전부 freq 2).

```python
class Trie:
    def __init__(self):
        self.children = {}      # char -> Trie
        self.freq = 0           # 이 노드를 지나간 단어 수

    def insert(self, word: str) -> None:
        node = self
        for ch in word:
            node = node.children.setdefault(ch, Trie())
            node.freq += 1

    def query(self, prefix: str) -> int:
        node = self
        for ch in prefix:
            if ch not in node.children:
                return 0
            node = node.children[ch]
        return node.freq
```
- 추적: insert cat, cap, dog → `query("ca")=2`, `query("cat")=1`, `query("cab")=0`.

### 복잡도
| 구조 | prefix 질의 | 삽입 |
|---|---|---|
| hash set | O(n·L) | O(L) |
| 정렬 배열 + 이분 | O(L log n) | O(n) |
| **Trie** | **O(L)** | **O(L)** |
- 공간: 최악 O(C) (C = 전체 문자 수), prefix 공유가 많을수록 훨씬 적음.

### 흔한 실수 / 변형
- 🐛 query에서 `setdefault`를 써서 **노드를 만들어 버리는** 실수 → 질의가 트라이를 오염.
- 💡 정확한 단어 존재 여부가 필요하면 `is_end` 플래그 추가. 알파벳 26자 고정이면 dict 대신 크기 26 배열.
- 🔑 "prefix", "starts with", "dictionary of words", "autocomplete", "longest common prefix" → **Trie**.

---

## 9. Autocomplete

**문제:** 단어를 순서대로 사전에 추가. 각 단어를 **추가한 직후**, 타이핑한 prefix가 사전에서 **딱 한 단어**와만 일치할 때까지 몇 글자 쳐야 하는지 계산해 총합 반환. 끝까지 유일해지지 않으면(더 긴 단어의 prefix) 전체 길이.
- 예: `["hi","hello","bojack","hills","hill"]` → 1 + 2 + 1 + 3 + 4 = **11**.

**핵심 관찰:** Trie Intro의 `freq`가 그대로 답. prefix 노드의 **freq == 1이면 유일** → 거기서 자동완성 발동.

**풀이**
1. 단어를 먼저 insert (경로 노드 freq += 1).
2. root부터 한 글자씩 내려가며 stroke += 1, 도착 노드 freq가 1이면 멈춤. 단어 끝까지 가면 전체 길이.
3. 원문 구현은 root.freq까지 세고 "첫 단어" 보정을 위해 마지막에 +1 — 아래처럼 **자식 노드 도착 후 검사**하면 보정이 필요 없음.

```python
class Node:
    __slots__ = ("children", "freq")
    def __init__(self):
        self.children, self.freq = {}, 0

def autocomplete(words: list[str]) -> int:
    root, total = Node(), 0
    for w in words:
        node = root
        for ch in w:                                   # insert
            node = node.children.setdefault(ch, Node())
            node.freq += 1
        node = root
        for ch in w:                                   # 몇 타 필요?
            node = node.children[ch]
            total += 1
            if node.freq == 1:
                break
    return total
```
- 추적: "hills" 삽입 후 h(3) → i(2) → l(1) → 3타. "hill"은 모든 노드가 "hills"와 공유(freq 2) → 4타.

**복잡도:** c = 전체 문자 수 → 시간 O(c), 공간 O(c).

- 🐛 "삽입 **후** 질의" 순서 — 자기 자신이 freq에 포함돼야 1이 "유일"을 의미.
- 🐛 첫 단어도 최소 1타 (root 기준으로 검사하면 0이 나옴 → 원문은 +1로 보정).
- 🔑 "prefix가 유일해지는 최소 길이", "몇 글자 치면 구분 가능" → **Trie + freq==1**.

---

## 10. Prefix Count

**문제:** 단어 사전(중복 가능)과 prefix 질의 목록. 각 prefix로 시작하는 단어 수(중복 포함)를 반환.
- 예: words=`["forgot","for","algomonster","while"]`, prefixes=`["fo","forg","algo"]` → `[2,1,1]`.

**핵심 관찰:** 단순 스캔은 질의당 O(n·m) — 공유 prefix 비교를 반복. Trie Intro의 `freq` = "이 prefix를 지나간 단어 수"가 **정확히 질의의 답**.

**풀이**
1. 모든 단어 insert (경로 노드 freq += 1). 중복 단어는 같은 경로를 두 번 지나가므로 자동 반영.
2. 각 prefix: 따라가다 자식이 없으면 0, 끝까지 가면 그 노드 freq.

```python
def prefix_count(words: list[str], prefixes: list[str]) -> list[int]:
    root = {}                                  # dict-of-dicts trie, 카운트는 '#' 키에
    for w in words:
        node = root
        for ch in w:
            node = node.setdefault(ch, {})
            node['#'] = node.get('#', 0) + 1

    res = []
    for p in prefixes:
        node = root
        for ch in p:
            node = node.get(ch)
            if node is None:
                break
        res.append(node['#'] if node else 0)
    return res
```

**복잡도:** c = 입력 전체 문자 수 → 시간 O(c), 공간 O(c). 질의 비용에 사전 크기 n이 안 들어감.

- 🐛 빈 prefix 질의가 가능하다면 root에도 카운트가 필요 (여기선 root 미증가 → 별도 처리).
- 💡 dict-of-dicts는 면접에서 클래스 없이 빠르게 쓰는 트라이 관용구. 카운트 키('#')가 문자와 충돌하지 않게 주의.
- 🔑 "각 prefix로 시작하는 단어 개수", "다수 질의" → **Trie + freq** (build 1회, 질의당 O(L)).

---

## 11. Add and Search Words Data Structure

**문제:** `WordDictionary` 설계 — `addWord(word)`로 소문자 단어 추가, `search(pattern)`은 `.`이 임의의 한 글자와 매치되는 와일드카드 검색. (LeetCode 211)
- 예: add bad, dad, mad → `search pad=false`, `bad=true`, `.ad=true`, `b..=true`.

**핵심 관찰**
- 저장은 평범한 Trie + **`is_end` 플래그** (여기선 prefix가 아니라 **정확한 단어** 일치가 필요).
- 새로운 건 `.`뿐: 일반 문자는 자식 하나만 따라가면 되지만, `.`에서는 **모든 자식으로 분기** → search가 작은 **DFS/백트래킹**이 됨.
- 매치 = 패턴이 끝나는 순간 도착 노드가 `is_end`.

**풀이**
1. addWord: 표준 insert, 마지막 노드 `is_end = True`.
2. search: `dfs(i, node)` — i가 끝이면 `node.is_end`. 문자면 그 자식 하나로, `.`이면 자식 중 하나라도 True면 True.

```python
class WordDictionary:
    def __init__(self):
        self.root = {}

    def addWord(self, word: str) -> None:
        node = self.root
        for ch in word:
            node = node.setdefault(ch, {})
        node['$'] = True                     # end-of-word

    def search(self, word: str) -> bool:
        def dfs(i: int, node: dict) -> bool:
            if i == len(word):
                return '$' in node
            ch = word[i]
            if ch == '.':
                return any(dfs(i + 1, child) for k, child in node.items() if k != '$')
            return ch in node and dfs(i + 1, node[ch])
        return dfs(0, self.root)
```

**복잡도:** addWord O(L). search는 와일드카드 없으면 O(L), `.`이 w개면 최악 O(26^w · L). 공간 O(전체 문자 수).

- 🐛 `is_end` 없이 경로 존재만 보면 "ba"가 "bad"의 prefix라서 true가 됨 → 오답.
- 🐛 `.` 분기 시 end 마커 키('$')를 자식으로 착각하지 않기.
- 🔑 "와일드카드 `.` 검색", "패턴으로 단어 존재 여부" → **Trie + DFS 분기**.

---

## 12. Word Search II

**문제:** 소문자 2D 격자와 단어 목록. 상하좌우 인접 칸으로 이동하며(한 경로에서 칸 재사용 금지) 철자를 만들 수 있는 단어들을 **입력 순서대로** 반환. (LeetCode 212)
- 예: matrix=`["aab","aaa"]`, words=`["bb","aa","abaa"]` → `["aa","abaa"]` (b가 하나뿐이라 "bb" 불가).

**핵심 관찰**
- 단어마다 격자 DFS(Word Search I)를 돌리면 같은 칸을 반복해서 걸음 → 낭비.
- **Trie(모든 단어) + 격자 DFS 백트래킹**을 결합: 격자를 걷는 동시에 트라이를 내려감. 현재 칸 글자가 트라이 자식에 없으면 **어떤 단어도 이어질 수 없음 → 즉시 가지치기**. 한 번의 순회로 모든 단어를 동시에 검사.
- 단어 끝 노드에 **단어 자체를 저장**(`node['$'] = word`) → 도착 즉시 답 확보, 경로 재구성 불필요.

**풀이**
1. 모든 단어로 트라이 구축.
2. 모든 칸에서 `dfs(r, c, root)`: 칸 글자가 자식에 없으면 return. 있으면 자식으로 이동, `$`가 있으면 found에 추가.
3. 칸을 `#`로 마킹 → 4방향 재귀 → 복원(백트래킹).
4. 같은 단어가 여러 경로로 나올 수 있으니 set에 모으고, 입력 순서로 필터.

```python
def word_search_ii(matrix: list[str], words: list[str]) -> list[str]:
    trie = {}
    for w in words:
        node = trie
        for ch in w:
            node = node.setdefault(ch, {})
        node['$'] = w

    grid = [list(row) for row in matrix]
    R, C = len(grid), len(grid[0])
    found = set()

    def dfs(r, c, parent):
        ch = grid[r][c]
        node = parent.get(ch)
        if node is None:
            return                              # prune
        if '$' in node:
            found.add(node.pop('$'))            # 찾은 단어는 제거 (중복 방지 + 가지치기)
        grid[r][c] = '#'
        for nr, nc in ((r+1, c), (r-1, c), (r, c+1), (r, c-1)):
            if 0 <= nr < R and 0 <= nc < C and grid[nr][nc] != '#':
                dfs(nr, nc, node)
        grid[r][c] = ch                         # backtrack
        if not node:                            # (최적화) 다 찾은 가지 삭제
            parent.pop(ch)

    for r in range(R):
        for c in range(C):
            dfs(r, c, trie)
    return [w for w in words if w in found]
```

**복잡도:** 트라이 구축 O(전체 문자 수). 탐색 최악 O(R·C·4·3^(L-1)) (L = 최대 단어 길이), 실제로는 트라이 가지치기로 훨씬 작음.

- 🐛 복원(`grid[r][c] = ch`)을 빼먹으면 다른 시작점의 경로가 막힘.
- 🐛 중복 결과 → set 사용. 출력 순서는 입력 순서.
- 💡 찾은 단어는 `$`를 pop하고, 자식이 빈 노드는 부모에서 제거하면 대형 입력(Python TLE 케이스)에서 크게 빨라짐 (원문 풀이엔 없는 추가 최적화).
- 🔑 "격자에서 **여러 단어** 찾기", "단어 목록 + 보드" → **Trie + DFS 백트래킹** (단어별 DFS는 X).

---

> 섹션: Data Structure Design

## 13. LRU Cache

**문제:** 용량이 정해진 LRU 캐시 설계. `get(key)`는 값 또는 -1, `put(key, value)`는 삽입/갱신, 가득 차면 **가장 오래 안 쓴 항목**을 제거 후 삽입. 둘 다 **O(1)**. (LeetCode 146)
- 예: cap=2, put(1,1) put(2,2) get1→1 put(3,3)[2 제거] get2→-1 put(4,4)[1 제거] get1→-1 get3→3 get4→4 → `[1,-1,-1,3,4]`.

**핵심 관찰**
- **hashmap만:** get/put은 O(1)이지만 "가장 오래된 것"이 뭔지 모름 → 제거 때 O(n) 스캔.
- **recency 순 리스트:** 앞 = 최근, 뒤 = LRU. 제거는 항상 뒤 → O(1). 문제는 중간 원소를 앞으로 옮기기 — 찾기가 O(n).
- 해결: hashmap의 값으로 **리스트 노드 포인터**를 저장 → 찾기 O(1).
- **왜 이중 연결?** 단일 연결이면 unlink에 predecessor가 필요 → head부터 걸어야 함 O(n). `prev`가 있으면 `node.prev.next = node.next; node.next.prev = node.prev`로 O(1).
- **dummy head/tail sentinel:** 빈 리스트 첫 삽입, 마지막 원소 제거 같은 경계 케이스의 null 체크 제거. 실제 노드는 항상 둘 사이.

**풀이**
1. `get(k)`: 없으면 -1. 있으면 노드 unlink → head 바로 뒤로 splice → 값 반환.
2. `put(k, v)`: 있으면 값 갱신 + 앞으로. 없으면 새 노드를 앞에 삽입, 맵 등록. 용량 초과면 `tail.prev` 제거 + **맵에서도 삭제** (그래서 노드에 key 저장).

```python
class Node:
    __slots__ = ("key", "val", "prev", "next")
    def __init__(self, key=0, val=0):
        self.key, self.val, self.prev, self.next = key, val, None, None

class LRUCache:
    def __init__(self, capacity: int):
        self.cap, self.map = capacity, {}
        self.head, self.tail = Node(), Node()
        self.head.next, self.tail.prev = self.tail, self.head

    def _unlink(self, node):
        node.prev.next, node.next.prev = node.next, node.prev

    def _push_front(self, node):
        node.prev, node.next = self.head, self.head.next
        self.head.next.prev = node
        self.head.next = node

    def get(self, key: int) -> int:
        node = self.map.get(key)
        if not node:
            return -1
        self._unlink(node); self._push_front(node)
        return node.val

    def put(self, key: int, value: int) -> None:
        if key in self.map:
            node = self.map[key]
            node.val = value
            self._unlink(node); self._push_front(node)
            return
        if len(self.map) == self.cap:
            lru = self.tail.prev
            self._unlink(lru)
            del self.map[lru.key]
        node = Node(key, value)
        self.map[key] = node
        self._push_front(node)
```

**복잡도:** get/put 모두 O(1) (해시 연산 + 상수 개 포인터 쓰기). 공간 O(capacity).

- 🐛 제거할 때 **맵에서 key 삭제 누락** → 유령 엔트리. 노드에 key를 들고 있어야 하는 이유.
- 🐛 기존 key에 put할 때 크기를 늘리거나 제거를 트리거하면 안 됨.
- 💡 Python 치트: `collections.OrderedDict` + `move_to_end(key)` / `popitem(last=False)`. 면접에선 보통 직접 구현을 요구.
- 🎤 임베디드 관점: 이건 "hash + intrusive doubly linked list" — 리눅스 커널 `list_head`, 페이지 캐시/TLB 교체 정책과 같은 구조.
- 🔑 "O(1) get/put + 가장 오래된 것 제거", "최근 사용 순서 유지" → **HashMap + Doubly Linked List (+ sentinel)**.

---

> 섹션: Segment Tree

## 14. Segment Tree Intro

### 핵심 개념
- 두 연산이 **섞여서** 들어옴: `update(idx, val)` (임의 위치 점 갱신), `query(l, r)` (구간 [l, r] 합, 양끝 포함).
- **일반 배열:** update O(1), query O(n). **prefix sum:** query O(1), update O(n) (뒤쪽 prefix 전부 무효). → 둘 다 한쪽만 싸다.
- 아이디어: **중첩된 구간들의 합을 캐시**. 전체를 반으로, 또 반으로… 단일 원소까지. 각 노드 합 = 두 자식 합 → 이진 트리, 높이 ⌈log₂ n⌉.

### Range query — 노드마다 세 경우 중 하나
1. **완전히 밖**: 공유 인덱스 없음 → 0 반환 (합의 항등원), 더 안 내려감.
2. **완전히 안**: 캐시된 합 그대로 반환 → 서브트리 전체가 한 번 읽기로 끝 (이게 핵심 이득).
3. **부분 겹침**: 두 자식으로 재귀해 더함. 부분 겹침은 쿼리 양끝을 걸치는 노드에서만 → 레벨당 최대 2개 → **O(log n)** 노드 방문 (레벨당 최대 4개 방문).

### Point update
- root→leaf 경로 하나만: `idx <= mid`면 왼쪽, 아니면 오른쪽. leaf에 쓰고, **재귀가 풀리며** 부모들을 `tree[p] = tree[2p] + tree[2p+1]`로 재계산. 경로 밖 노드는 그대로 유효. O(log n).

### 구현: flat array (heap 인덱싱)
- root = 1, 왼쪽 자식 `2*cur`, 오른쪽 `2*cur+1`, 부모 `i//2` (1-based라 산술이 깔끔).
- `arr`는 0-indexed 값, `tree`는 1-indexed 캐시. 각 재귀 호출은 `(cur, cur_left, cur_right)`를 함께 들고 다님.
- **크기 4n:** 노드는 2n-1개지만 n이 2의 거듭제곱이 아니면 heap 인덱싱에 빈 슬롯이 생김. 최악 2·next_pow2(n) < 4n → 4n이면 항상 안전.
- build: update를 n번 → O(n log n). (leaf 먼저 채우고 bottom-up이면 O(n).)

```python
class SegmentTree:
    def __init__(self, arr):
        self.n = len(arr)
        self.tree = [0] * (4 * self.n)
        for i, v in enumerate(arr):
            self.update(i, v)

    def update(self, idx, val, cur=1, lo=0, hi=None):
        if hi is None: hi = self.n - 1
        if lo == hi:
            self.tree[cur] = val
            return
        mid = (lo + hi) // 2
        if idx <= mid:
            self.update(idx, val, 2 * cur, lo, mid)
        else:
            self.update(idx, val, 2 * cur + 1, mid + 1, hi)
        self.tree[cur] = self.tree[2 * cur] + self.tree[2 * cur + 1]

    def query(self, ql, qr, cur=1, lo=0, hi=None):
        if hi is None: hi = self.n - 1
        if hi < ql or qr < lo:          # 완전히 밖
            return 0
        if ql <= lo and hi <= qr:       # 완전히 안
            return self.tree[cur]
        mid = (lo + hi) // 2            # 부분 겹침
        return (self.query(ql, qr, 2 * cur, lo, mid)
                + self.query(ql, qr, 2 * cur + 1, mid + 1, hi))
```
- 추적: arr=[2,1,5,3] → `query(1,3)` = 0([0,0] 밖) + 1([1,1]) + 8([2,3] 캐시) = 9. `update(2,0)` 후 [2,3]=3, root=6 → `query(1,3)=4`.

### 복잡도 / 일반화
- query·update O(log n), 공간 O(n) (4n 할당).
- **결합법칙 + 항등원**이 있는 연산이면 다 됨: min(+∞), max(−∞), gcd(0), OR(0). merge 규칙과 "밖" 반환값만 교체.
- 🐛 off-by-one 지옥: `mid+1`, 양끝 포함, 밖 조건 `hi < ql or qr < lo`.
- 💡 구간 **갱신**까지 필요하면 lazy propagation (후속 주제). 합 + 점 갱신만이면 **Fenwick tree(BIT)**가 더 짧음.
- 🎤 면접에선 "언제 쓰는지 + query/update가 구간을 어떻게 분해하는지" 설명이 먼저, 코드는 그 다음.
- 🔑 "구간 합/최소/최대 + 매 업데이트 후", "갱신과 질의가 섞여 있음" → **Segment Tree**.

---

## 15. Range max

**문제:** 배열과 연산 목록. `[1, l, r]`은 구간 [l, r] 최댓값 질의, `[2, i, v]`는 `arr[i] = v` 갱신. 질의 결과 리스트 반환.
- 예: arr=`[1,2,3,4,5]`, ops=`[[1,0,4],[2,4,7],[1,1,4]]` → `[5,7]`.

**핵심 관찰:** Segment Tree Intro에서 **merge 연산만 `+` → `max`**로 교체. "완전히 밖" 반환값은 max의 항등원 **−∞** (값이 모두 ≥1이라 원문은 0을 써도 통과하지만 음수가 있으면 틀림).

**풀이**
1. arr로 max 세그먼트 트리 구축.
2. op==1이면 query, op==2면 update.

```python
def range_max(arr: list[int], operations: list[list[int]]) -> list[int]:
    n = len(arr)
    NEG = float('-inf')
    tree = [NEG] * (4 * n)

    def update(idx, val, cur=1, lo=0, hi=n - 1):
        if lo == hi:
            tree[cur] = val
            return
        mid = (lo + hi) // 2
        if idx <= mid: update(idx, val, 2 * cur, lo, mid)
        else:          update(idx, val, 2 * cur + 1, mid + 1, hi)
        tree[cur] = max(tree[2 * cur], tree[2 * cur + 1])

    def query(ql, qr, cur=1, lo=0, hi=n - 1):
        if hi < ql or qr < lo:
            return NEG
        if ql <= lo and hi <= qr:
            return tree[cur]
        mid = (lo + hi) // 2
        return max(query(ql, qr, 2 * cur, lo, mid),
                   query(ql, qr, 2 * cur + 1, mid + 1, hi))

    for i, v in enumerate(arr):
        update(i, v)
    res = []
    for op, a, b in operations:
        if op == 1: res.append(query(a, b))
        else:       update(a, b)
    return res
```

**복잡도:** 시간 O((n + q) log n), 공간 O(n).

- 🐛 항등원 실수: max는 −∞, min은 +∞. sum의 0을 그대로 복붙하면 음수 입력에서 오답.
- 💡 갱신이 없다면 sparse table (O(1) 질의) 또는 sliding window max(monotonic deque)도 후보.
- 🔑 "구간 최대/최소 + 점 갱신 섞임" → **Segment Tree (merge = max/min)**.

---

## 용어·패턴 사전

| 용어 | 뜻 |
|---|---|
| DSU / Union-Find | 서로소 집합들을 트리 숲으로 관리. `find`(대표 찾기) + `union`(합치기) |
| representative (root) | 집합의 ID 역할을 하는 원소. `parent[r] == r` |
| path compression | find 중 지나간 노드를 root에 직접 연결 → 이후 find O(1)에 가깝게 |
| path halving | 반복문 버전 압축: `parent[x] = parent[parent[x]]` |
| union by rank / size | 낮은(작은) 트리를 높은(큰) 트리 밑에 붙여 깊이 억제 |
| α(n) | 역 Ackermann 함수. 현실의 모든 n에서 ≤ 4 → 사실상 상수 |
| reverse union-find | 삭제 연산을 역순으로 처리해 추가(union)로 바꾸는 오프라인 트릭 |
| Trie (prefix tree) | 간선=문자, 경로=prefix. 공통 prefix를 한 번만 저장 |
| freq (prefix count) | 트라이 노드를 지나간 단어 수 → prefix로 시작하는 단어 수 |
| is_end / `$` 마커 | 이 노드에서 끝나는 단어가 있음을 표시 (정확 일치·단어 회수용) |
| wildcard DFS | `.`에서 모든 자식으로 분기하는 트라이 탐색 |
| Trie + grid backtracking | 격자 DFS와 트라이 하강을 동시에 → 여러 단어를 한 번에 탐색·가지치기 |
| LRU | Least Recently Used — 가장 오래 사용 안 한 항목을 제거하는 교체 정책 |
| sentinel (dummy) node | 경계 null 체크를 없애는 가짜 head/tail 노드 |
| Segment Tree | 구간 집계를 이진 트리에 캐시. 점 갱신·구간 질의 모두 O(log n) |
| heap indexing | root=1, 자식 2i / 2i+1, 부모 i//2 — 트리를 배열에 저장 |
| identity element (항등원) | "완전히 밖" 노드가 반환하는 중립값: sum 0, max −∞, min +∞ |
| lazy propagation | 구간 갱신을 미뤄 두는 세그먼트 트리 확장 (후속 주제) |
| Fenwick tree (BIT) | 합 + 점 갱신 전용의 더 짧은 대안 구조 |
