---
id: DS-B13
time: 50분
updated: 2026-11-07
---
# DS-B13 이진 탐색 트리: 삽입·삭제·순회

## 학습 목표

- 정적 노드 풀 위에서 BST의 **insert / find / delete** 를 `malloc` 없이 구현한다.
- 삭제의 세 경우(자식 **0개 / 1개 / 2개**)를 **포인터의 포인터(`Node **link`)** 하나로 깔끔하게 처리한다.
- **inorder(재귀) / preorder(정적 스택, 반복) / level-order(정적 큐)** 순회를 쓰고, 각각 어떤 순서가 나오는지 손으로 예측한다.
- "부모와만 비교"하는 틀린 검사 대신 **(min, max) 범위**를 내려보내는 유효성 검사를 쓴다.
- 고정 시드 랜덤 삽입·삭제를 **참조 모델**과 비교해 불변식(정렬 순서·개수·풀 누수)을 매 연산마다 확인하고, 삽입 순서가 높이를 어떻게 망가뜨리는지 측정한다.

## 1. 문제

BST는 면접 화이트보드의 단골이고(특히 **삭제**와 **유효성 검사**), 펌웨어에서는 "정렬된 상태를 유지하며 넣고 빼는" 곳 — 타이머 목록, 소프트웨어 스케줄러, SSD FTL의 extent 맵(LBA 구간 → 물리 위치) — 에 쓰인다. 리눅스 커널의 `rbtree`, `hrtimer`, CFS 스케줄러도 전부 *균형 잡힌* BST다. 균형을 배우기 전에 기본 BST를 정확히 다루는 것이 먼저다.

**BST 불변식**: 모든 노드 `n` 에 대해 *왼쪽 서브트리 전체* `< n.key <` *오른쪽 서브트리 전체*. 그래서 inorder(왼쪽 → 나 → 오른쪽)로 돌면 키가 **정렬된 순서**로 나온다. 이 레슨의 모든 테스트는 결국 이 한 문장을 확인한다.

```svg
<svg viewBox="0 0 720 370" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">삽입 순서 50 30 70 20 40 60 80 35 45 65 로 만든 BST</text>
  <g stroke="currentColor" fill="none">
    <line x1="360" y1="60" x2="200" y2="120"/><line x1="360" y1="60" x2="520" y2="120"/>
    <line x1="200" y1="120" x2="120" y2="180"/><line x1="200" y1="120" x2="280" y2="180"/>
    <line x1="520" y1="120" x2="440" y2="180"/><line x1="520" y1="120" x2="600" y2="180"/>
    <line x1="280" y1="180" x2="240" y2="240"/><line x1="280" y1="180" x2="320" y2="240"/>
    <line x1="440" y1="180" x2="480" y2="240"/>
  </g>
  <g stroke="currentColor">
    <circle cx="360" cy="60" r="17" class="f-acc"/>
    <circle cx="200" cy="120" r="17" class="f-mut"/><circle cx="520" cy="120" r="17" class="f-mut"/>
    <circle cx="120" cy="180" r="17" class="f-mut"/><circle cx="280" cy="180" r="17" class="f-mut"/>
    <circle cx="440" cy="180" r="17" class="f-mut"/><circle cx="600" cy="180" r="17" class="f-mut"/>
    <circle cx="240" cy="240" r="17" class="f-mut"/><circle cx="320" cy="240" r="17" class="f-mut"/>
    <circle cx="480" cy="240" r="17" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="360" y="65">50</text><text x="200" y="125">30</text><text x="520" y="125">70</text>
    <text x="120" y="185">20</text><text x="280" y="185">40</text><text x="440" y="185">60</text>
    <text x="600" y="185">80</text><text x="240" y="245">35</text><text x="320" y="245">45</text>
    <text x="480" y="245">65</text>
  </g>
  <text x="640" y="65" fill="currentColor" font-size="12">level 0</text>
  <text x="640" y="125" fill="currentColor" font-size="12">level 1</text>
  <text x="640" y="185" fill="currentColor" font-size="12">level 2</text>
  <text x="640" y="245" fill="currentColor" font-size="12">level 3</text>
  <g fill="currentColor" font-family="monospace">
    <text x="10" y="290">inorder    (L→나→R) : 20 30 35 40 45 50 60 65 70 80   ← 정렬!</text>
    <text x="10" y="315">preorder   (나→L→R) : 50 30 20 40 35 45 70 60 65 80   ← 복제·직렬화</text>
    <text x="10" y="340">level-order (BFS)   : 50 30 70 20 40 60 80 35 45 65   ← 층별</text>
  </g>
  <text x="10" y="362" fill="currentColor" font-size="12">높이(노드 수 기준) = 4: 50 → 30 → 40 → 35. 탐색 비용은 높이에 비례한다.</text>
</svg>
```

노드는 `malloc` 대신 **정적 노드 풀** `g_pool[128]` 에서 꺼낸다 (`node_alloc` / `node_free`, 시작 코드에 주어짐). 빈 노드는 `left` 포인터로 엮인 free list에 있다 — DS-B07 메모리 풀과 같은 구조다. `g_used` 로 누수를 잡는다.

### Part A — find / insert / delete

- `bst_find(t, key)` — 노드 포인터 또는 `NULL`.
- `bst_insert(t, key)` — 새 키면 `true`. **중복 키는 거부**(`false`, 트리 불변). 풀이 비면 `false`.
- `bst_delete(t, key)` — 지웠으면 `true`, 없으면 `false`. 세 경우를 모두 처리:
  1. 자식 0개(리프) → 그냥 떼어낸다.
  2. 자식 1개 → 그 자식을 부모에 바로 이어붙인다.
  3. 자식 2개 → **후계자(inorder successor = 오른쪽 서브트리의 최솟값)** 의 키를 복사해 오고, 후계자 노드를 대신 지운다.
- 지운 노드는 반드시 `node_free` 로 풀에 돌려준다 (`t->count == g_used` 검사).

### Part B — 순회 3종

- `bst_inorder` — **재귀**. 결과는 정렬 순서.
- `bst_preorder` — **반복**. 크기 `POOL_N` 의 정적 배열을 스택으로 쓴다 (재귀 금지).
- `bst_levelorder` — 정적 배열 **큐**로 BFS.
- 모두 `out[]` 에 키를 채우고 개수를 돌려준다.

### Part C — 유효성 검사 + 랜덤 스트레스

- `valid_rec(n, lo, hi)` — 모든 노드가 열린 구간 `(lo, hi)` 안에 있는지. 경계는 `int64_t` 로 받아 키가 `INT_MIN`/`INT_MAX` 여도 안전하게.
- 하네스가 xorshift32(고정 시드)로 5000번 랜덤 삽입·삭제를 하고, **매 연산 후** `bool ref[100]` 참조 모델과 비교한다: 반환값, inorder 결과 = 참조의 정렬 목록, `count == g_used`, `bst_valid`.
- 마지막으로 정렬된 키 vs 셔플한 키를 넣었을 때의 **높이**를 표로 출력한다.

## 2. Problem (English)

> **Binary search tree without dynamic allocation.**
>
> Nodes come from a fixed pool of 128 (`node_alloc` / `node_free` provided). Keys are distinct `int`s.
>
> 1. `Node *bst_find(const Bst *t, int key)`, `bool bst_insert(Bst *t, int key)` (reject duplicates), and `bool bst_delete(Bst *t, int key)` handling leaf, one-child and two-children nodes. Return every freed node to the pool.
> 2. `int bst_inorder(const Bst *t, int *out)` (recursive), `int bst_preorder(...)` (iterative, explicit stack, no recursion), `int bst_levelorder(...)` (BFS with an array-backed queue). Each returns the number of keys written.
> 3. `bool bst_valid(const Bst *t)` — verify the BST property for the *whole* tree (not just parent/child pairs). Must accept `INT_MIN` and `INT_MAX` as keys.
>
> Example: inserting `50 30 70 20 40 60 80 35 45 65`, then `bst_delete(30)` gives preorder `50 35 20 40 45 70 60 65 80`. The tree `10(5(-,12),15)` is **invalid**.
>
> Follow-ups: What is the worst-case height and how do you get it? How deep can the recursion go on a 2 KB MCU stack?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 FAIL 이 쭉 나오고 "5/25 passed" — TODO 7곳을 채워 25/25 를 만든다
```

메모리 오류까지 보려면 (권장):

```bash
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined solution.c -o sol_asan && ./sol_asan
```

## 4. 힌트

<details>
<summary>힌트 1 — find 는 반복문 세 줄</summary>

`while (n && n->key != key) n = key < n->key ? n->left : n->right; return n;` — BST 탐색은 재귀가 필요 없다. 한 방향으로만 내려가기 때문이다.

</details>

<details>
<summary>힌트 2 — "부모 포인터" 대신 "나를 가리키는 링크의 주소"</summary>

`Node **link = &t->root;` 로 시작해서 `link = &(*link)->left` 처럼 내려간다. 빈 자리(`*link == NULL`)에 도착하면 `*link = new_node;` 한 줄로 끝. 루트인지 왼쪽 자식인지 오른쪽 자식인지 분기할 필요가 없다 — 리눅스 `rb_link_node` 와 같은 관용구.

</details>

<details>
<summary>힌트 3 — 삭제: 자식 2개를 자식 ≤1개 문제로 바꾼다</summary>

후계자 = 오른쪽으로 한 번, 그다음 왼쪽 끝까지. 후계자는 **왼쪽 자식이 없다**(있으면 그게 더 작은 후계자일 테니). 그러니 키를 복사해 온 뒤 "후계자 노드 삭제"는 경우 1·2로 끝난다. `link` 를 후계자를 가리키는 링크로 바꿔주면 마지막 한 줄 `*link = n->left ? n->left : n->right;` 가 세 경우 모두를 처리한다.

</details>

<details>
<summary>힌트 4 — 반복 preorder 는 오른쪽을 먼저 push</summary>

스택은 LIFO. pop → 방문 → `right` push → `left` push 순서여야 `left` 가 먼저 나온다. 각 노드는 정확히 한 번 push 되므로 스택 크기 `POOL_N` 이면 절대 안 넘친다.

</details>

<details>
<summary>힌트 5 — 유효성: 부모가 아니라 조상 전체가 경계를 만든다</summary>

왼쪽으로 내려갈 때 상한이 `n->key` 로 줄고, 오른쪽으로 내려갈 때 하한이 `n->key` 로 올라간다. `valid_rec(n->left, lo, n->key) && valid_rec(n->right, n->key, hi)`. 시작은 `(INT_MIN-1, INT_MAX+1)` — `int` 로는 표현이 안 되니 `int64_t`.

</details>

## 5. 풀이

### Part A — `Node **link` 하나로 insert와 delete를 통일

`Node *parent` 를 들고 다니면 "부모의 왼쪽이었나 오른쪽이었나, 아니면 루트였나"를 매번 분기해야 한다. 대신 **"현재 노드를 가리키고 있는 포인터 변수의 주소"** 를 들고 다니면, 그 변수에 쓰는 것만으로 부모(또는 루트)와의 연결이 바뀐다.

```svg
<svg viewBox="0 0 720 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">insert(42): link 가 내려가는 경로</text>
  <g stroke="currentColor" fill="none">
    <rect x="20" y="50" width="110" height="34"/><rect x="190" y="50" width="130" height="34"/>
    <rect x="380" y="50" width="140" height="34"/><rect x="580" y="50" width="120" height="34"/>
  </g>
  <rect x="580" y="50" width="120" height="34" class="f-acc"/>
  <g fill="currentColor" font-family="monospace">
    <text x="30" y="72">&amp;t-&gt;root</text><text x="200" y="72">&amp;n50-&gt;left</text>
    <text x="390" y="72">&amp;n30-&gt;right</text><text x="590" y="72">&amp;n45-&gt;left</text>
  </g>
  <g stroke="currentColor"><line x1="130" y1="67" x2="186" y2="67"/><line x1="320" y1="67" x2="376" y2="67"/><line x1="520" y1="67" x2="576" y2="67"/></g>
  <g fill="currentColor" font-size="12">
    <text x="135" y="105">42 &lt; 50</text><text x="325" y="105">42 &gt; 30</text>
    <text x="465" y="105" font-size="12">42&gt;40, 42&lt;45</text>
  </g>
  <text x="10" y="145" fill="currentColor">*link == NULL 에 도착 → *link = node_alloc(42);  루트든 왼쪽이든 오른쪽이든 같은 한 줄.</text>
  <text x="10" y="172" fill="currentColor">delete 도 같은 링크를 쓴다: *link = (남은 자식 또는 NULL);  부모 쪽 분기 코드가 사라진다.</text>
</svg>
```

삭제의 세 경우를 그림으로:

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">delete — 자식 0개 / 1개 / 2개</text>
  <rect x="10" y="40" width="220" height="190" rx="8" class="f-mut" stroke="currentColor"/>
  <rect x="255" y="40" width="220" height="190" rx="8" class="f-mut" stroke="currentColor"/>
  <rect x="500" y="40" width="230" height="190" rx="8" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" font-weight="bold"><text x="20" y="62">① 리프 20</text><text x="265" y="62">② 자식 1개 60</text><text x="510" y="62">③ 자식 2개 30</text></g>
  <g stroke="currentColor" fill="none">
    <line x1="120" y1="95" x2="70" y2="145"/>
    <line x1="365" y1="95" x2="320" y2="145"/><line x1="320" y1="145" x2="360" y2="195"/>
    <line x1="615" y1="95" x2="560" y2="140"/><line x1="615" y1="95" x2="670" y2="140"/><line x1="670" y1="140" x2="640" y2="190"/>
  </g>
  <path d="M340 180 C 300 150, 300 120, 345 100" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <path d="M640 175 C 610 140, 590 100, 605 90" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <g stroke="currentColor">
    <circle cx="120" cy="95" r="16" class="f-mut"/><circle cx="70" cy="145" r="16" class="f-warn"/>
    <circle cx="365" cy="95" r="16" class="f-mut"/><circle cx="320" cy="145" r="16" class="f-warn"/><circle cx="360" cy="195" r="16" class="f-ok"/>
    <circle cx="615" cy="95" r="16" class="f-warn"/><circle cx="560" cy="140" r="16" class="f-mut"/><circle cx="670" cy="140" r="16" class="f-mut"/><circle cx="640" cy="190" r="16" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="120" y="100">30</text><text x="70" y="150">20</text>
    <text x="365" y="100">70</text><text x="320" y="150">60</text><text x="360" y="200">65</text>
    <text x="615" y="100">30</text><text x="560" y="145">20</text><text x="670" y="145">40</text><text x="640" y="195">35</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="250">*link = NULL</text>
    <text x="265" y="250">*link = 65 (유일한 자식)</text>
    <text x="510" y="250">키 35 복사 → 35 노드를 ②/①로 삭제</text>
    <text x="510" y="270">후계자 = 오른쪽 한 번, 왼쪽 끝까지</text>
  </g>
</svg>
```

테스트의 삭제 순서를 단계별로 추적하면 (preorder로 모양 확인):

| 단계 | 연산 | 경우 | 무슨 일이 일어나나 | preorder |
|---|---|---|---|---|
| 0 | (초기) | — | — | `50 30 20 40 35 45 70 60 65 80` |
| 1 | `delete(30)` | 자식 2개 | 후계자 = 40의 왼쪽 끝 = 35. 30 자리에 35를 쓰고 35 노드를 지움 | `50 35 20 40 45 70 60 65 80` |
| 2 | `delete(20)` | 리프 | 35의 left = NULL | `50 35 40 45 70 60 65 80` |
| 3 | `delete(60)` | 자식 1개 | 70의 left = 65 | `50 35 40 45 70 65 80` |
| 4 | `delete(50)` | 루트, 자식 2개 | 후계자 = 70의 왼쪽 끝 = 65 | `65 35 40 45 70 80` |
| 5 | `insert(68)`, `delete(70)` | 자식 2개 | 후계자 = 80 = **바로 오른쪽 자식** (`s == &n->right`) | `65 35 40 45 80 68` |

5단계가 함정이다. 부모 포인터 방식으로 짜면 "후계자의 부모가 곧 지울 노드"인 경우를 따로 처리해야 해서 버그가 잘 난다. `Node **s` 방식은 `s` 가 `&n->right` 를 가리킬 뿐이라 같은 코드로 처리된다.

**키 복사 vs 노드 이동**: 본문은 키만 복사한다(값이 `int` 하나라 충분). 노드에 큰 페이로드가 있거나, 노드 주소를 바깥에서 핸들로 들고 있다면(타이머 핸들, 침입형 구조) 키 복사는 **다른 객체의 내용을 바꿔버리는** 버그가 된다. 그때는 후계자 노드 자체를 떼어 옮겨 단다 → 추가 연습 4.

### Part B — 재귀 · 스택 · 큐, 세 가지 방문 순서

| 순회 | 구현 | 보조 메모리 | 최악 깊이/크기 | 쓰임새 |
|---|---|---|---|---|
| inorder | 재귀 | 호출 스택 (프레임 × 높이) | 높이 h (편향 트리면 N) | 정렬 출력, 범위 질의 |
| preorder | 반복 + `stack[POOL_N]` | 포인터 배열 | 각 노드 1회 push → ≤ N | 트리 복제·직렬화 (다시 넣으면 같은 모양) |
| level-order | 반복 + `q[POOL_N]` | 포인터 배열 | 각 노드 1회 enqueue → ≤ N | 층별 출력, 최소 깊이, 모양 디버깅 |

preorder 스택의 동작 (초기 트리):

| pop | 방문 | push (right, left) | 스택 (bottom → top) |
|---|---|---|---|
| — | — | 50 | `50` |
| 50 | 50 | 70, 30 | `70 30` |
| 30 | 30 | 40, 20 | `70 40 20` |
| 20 | 20 | — | `70 40` |
| 40 | 40 | 45, 35 | `70 45 35` |
| 35 | 35 | — | `70 45` |
| 45 | 45 | — | `70` |
| 70 | 70 | 80, 60 | `80 60` |

level-order 큐는 `head` 와 `tail` 이 둘 다 앞으로만 간다. 각 노드가 딱 한 번 들어가므로 길이 `POOL_N` 의 **선형 배열**이면 충분하다(링 버퍼 불필요). 메모리를 더 줄이고 싶다면 큐의 최대 동시 길이는 "가장 넓은 층의 폭"이므로, 폭 상한 W를 알면 크기 W의 링 버퍼(DS-B01)로 바꿀 수 있다.

**펌웨어 관점 — 재귀는 숨은 스택 사용량이다.** Cortex-M 의 태스크 스택은 흔히 512B~2KB. `inorder_rec` 한 프레임이 (리턴 주소 + 저장 레지스터 + 인자) 대략 16~32바이트라면 편향 트리 1000노드에서 16~32KB — 바로 HardFault. 그래서 MISRA C:2012 Rule 17.2 는 재귀를 금지한다. 재귀를 쓰려면 **높이 상한이 보장될 때만**(균형 트리: 2·log₂N) 쓰고, 아니면 정적 스택 버전으로 바꾼다 (추가 연습 1).

### Part C — 유효성은 "구간"으로 검사한다

```svg
<svg viewBox="0 0 720 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">부모-자식만 비교하면 통과하지만 BST가 아닌 트리</text>
  <g stroke="currentColor" fill="none">
    <line x1="150" y1="65" x2="90" y2="125"/><line x1="150" y1="65" x2="210" y2="125"/><line x1="90" y1="125" x2="130" y2="185"/>
  </g>
  <g stroke="currentColor">
    <circle cx="150" cy="65" r="17" class="f-mut"/><circle cx="90" cy="125" r="17" class="f-mut"/>
    <circle cx="210" cy="125" r="17" class="f-mut"/><circle cx="130" cy="185" r="17" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="150" y="70">10</text><text x="90" y="130">5</text><text x="210" y="130">15</text><text x="130" y="190">12</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="190" y="62">(-inf, +inf)</text>
    <text x="10" y="112">(-inf, 10)</text>
    <text x="235" y="122">(10, +inf)</text>
    <text x="155" y="215">(5, 10)  12 ∉ → 실패</text>
  </g>
  <g fill="currentColor">
    <text x="360" y="70">부모 비교: 5 &lt; 12 ✓ (12는 5의 오른쪽)</text>
    <text x="360" y="95">10 &gt; 5 ✓, 10 &lt; 15 ✓ → "유효"라고 착각</text>
    <text x="360" y="130">그러나 12 는 10 의 왼쪽 서브트리에 있다.</text>
    <text x="360" y="155">find(12) 는 10 에서 오른쪽(15)으로 가서 못 찾는다.</text>
    <text x="360" y="190">구간 검사: 왼쪽으로 가면 hi = 부모 키,</text>
    <text x="360" y="215">오른쪽으로 가면 lo = 부모 키 — 조상 전체가 경계.</text>
  </g>
</svg>
```

- 경계는 **열린 구간** `(lo, hi)`: 중복 키를 허용하지 않으므로 `key <= lo || key >= hi` 면 실패.
- `int` 경계(`INT_MIN`, `INT_MAX`)를 센티넬로 쓰면 키 자체가 `INT_MIN` 일 때 `INT_MIN <= INT_MIN` 으로 잘못 실패한다. `int64_t` 로 한 칸 넓힌다 (테스트의 `edge` 트리). 대안은 `const Node *lo_node, *hi_node` 포인터(NULL = 무한대)로 경계를 전달하는 방법.
- 동치 검사: "inorder 결과가 순증가"도 유효성과 같다 (중복 없음 가정). 랜덤 테스트는 두 가지를 다 확인한다.

**랜덤 스트레스와 참조 모델.** 키 범위를 0~99로 좁혀야 같은 키에 삽입/삭제가 자주 부딪혀 중복 삽입·없는 키 삭제 경로까지 밟는다. 참조는 `bool ref[100]` 하나 — "정답이 뻔한 느린 구현"과 매 연산 비교하는 것이 자료구조 테스트의 기본기다. 실제 실행 결과:

```text
  random ops=5000  max_size=64  final_size=55  bad_ops=0
```

**측정 — 삽입 순서가 높이를 정한다** (실제 출력, 셔플 200회 평균):

| N | 정렬 순서로 삽입 | 랜덤 순서 (평균) | 최소 가능 ⌈log₂(N+1)⌉ |
|---|---|---|---|
| 15 | 15 | 6.9 | 4 |
| 63 | 63 | 11.6 | 6 |
| 127 | 127 | 14.1 | 7 |

- 정렬된 키를 넣으면 매번 오른쪽에만 붙어 **연결 리스트**가 된다 → 높이 N, 탐색 O(N). 센서 타임스탬프·시퀀스 번호처럼 **단조 증가하는 키**가 펌웨어에서 흔하다는 게 함정이다.
- 랜덤 순서면 평균 높이는 최소치의 약 2배로 괜찮다 (N=127에서 14.1 vs 7). 이론적으로 기대 높이는 점근적으로 ≈ 4.31·ln N (Reed, 2003) 이고, 작은 N에선 그보다 낮게 나온다. 하지만 "평균"은 실시간 시스템의 보증이 아니다 → 최악을 막으려면 균형 트리(AVL/red-black, DS-M 레벨)를 쓴다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 25/25 passed
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined solution.c -o sol_asan && ./sol_asan   # ASan/UBSan 깨끗
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 유효성 검사를 `n->left->key < n->key && n->right->key > n->key` 로만 | 손자 이하가 조상의 경계를 넘는 경우(위 그림의 12)를 못 잡는다 | `valid_rec(n, lo, hi)` 로 구간을 내려보냄 |
| 경계 센티넬로 `INT_MIN`/`INT_MAX` 사용 | 키가 정확히 `INT_MIN` 이면 잘못 실패 | `int64_t` 경계 또는 `const Node *` 경계 |
| 자식 2개 삭제에서 **선행자/후계자 노드를 안 지움** | 키만 복사하고 끝내면 같은 키가 두 번 존재 | 키 복사 후 `link = s; n = succ;` 로 후계자를 삭제 대상으로 |
| 후계자가 `n->right` 자신인 경우를 별도 처리 안 함 (부모 포인터 방식) | `succ_parent->left = succ->right` 가 엉뚱한 곳을 덮음 | `Node **s` 링크 방식이면 자동으로 해결 |
| 삭제 후 `node_free` 누락 | 풀 누수 → 몇 시간 뒤 `node_alloc` 이 NULL (malloc 누수와 같은 증상) | 삭제 경로 끝에서 반드시 `node_free(n)` + `count--` |
| 반복 preorder에서 `left` 를 먼저 push | LIFO라 오른쪽이 먼저 방문됨 | `right` push → `left` push |
| 중복 키 삽입 시 오른쪽(또는 왼쪽)에 그냥 붙임 | 정책이 섞이면 find/delete가 어느 쪽을 볼지 모호 | 거부(`false`)하거나 노드에 카운트를 둔다 |
| 재귀 순회를 ISR 이나 작은 태스크 스택에서 호출 | 편향 트리에서 스택 오버플로 → HardFault | 정적 스택 반복 순회 (추가 연습 1) |

## 8. 면접 Q&A

<details>
<summary>Q1. BST에서 자식이 둘인 노드는 어떻게 삭제하나요? 후계자 대신 선행자를 써도 되나요?</summary>

오른쪽 서브트리의 최솟값(후계자) 또는 왼쪽 서브트리의 최댓값(선행자)으로 바꾼다. 둘 다 자식이 최대 1개라 원래 문제가 쉬운 경우로 줄어든다. 어느 쪽이든 정확하다. 꼬리질문 "항상 후계자만 쓰면?" → 삭제가 많으면 트리가 왼쪽으로 기우는 경향이 있다는 연구(Hibbard 삭제의 비대칭)가 있어 번갈아 쓰기도 하지만, 실무에선 어차피 균형 트리를 쓴다.

</details>

<details>
<summary>Q2. BST가 유효한지 어떻게 검사하나요? 시간·공간 복잡도는?</summary>

(min, max) 구간을 내려보내는 재귀, 또는 inorder 결과가 순증가인지 확인(직전 값 하나만 기억하면 되므로 추가 배열 불필요). 둘 다 O(N) 시간, O(h) 스택. 흔한 오답은 부모-자식만 비교하는 것. 꼬리: `INT_MIN` 키 처리 → 넓은 타입 또는 포인터 경계.

</details>

<details>
<summary>Q3. 정렬된 데이터를 BST에 넣으면 어떻게 되나요? 펌웨어에서 실제로 일어나나요?</summary>

매번 한쪽에만 붙어 높이 N의 리스트가 된다 (위 측정: N=127 → 높이 127). 펌웨어에서는 타임스탬프, 증가하는 LBA, 시퀀스 번호처럼 단조 키가 흔해서 **실제로 자주 일어난다**. 대책: 균형 트리(rbtree — 리눅스 hrtimer, CFS), 키 분포가 고정이면 정렬 배열 + 이진 탐색, 타이머라면 힙(DS-B12)이나 타이머 휠.

</details>

<details>
<summary>Q4. 재귀 없이 inorder 순회를 할 수 있나요? 메모리를 O(1)로 할 수도 있나요?</summary>

명시적 스택: 왼쪽 끝까지 push → pop 해서 방문 → 오른쪽으로 이동 (추가 연습 1). O(1) 추가 메모리로는 **Morris traversal**: 왼쪽 서브트리의 최댓값 노드의 빈 `right` 를 잠깐 현재 노드로 이어(스레드) 돌아올 길을 만들고, 돌아오면 원상복구. 트리를 일시적으로 수정하므로 ISR이 동시에 읽는다면 쓰면 안 된다. 또는 노드에 parent 포인터를 두면 스택 없이 후계자를 따라갈 수 있다 (rbtree의 `rb_next`).

</details>

<details>
<summary>Q5. malloc 없이 BST를 구현할 때 노드 풀 설계에서 신경 쓸 점은?</summary>

① 최대 노드 수를 요구사항에서 정해 정적 배열 크기로 박는다 ② 풀 고갈 시 insert가 실패를 돌려주고 호출자가 처리 (조용히 무시 금지) ③ 삭제 경로에서 반드시 반환 — `used == count` 를 불변식으로 테스트 ④ 포인터 대신 `uint8_t`/`uint16_t` 인덱스를 쓰면 32비트 MCU에서 노드가 12바이트(int + 포인터 2개) → 8바이트(int + `uint8_t` 2개 + 패딩, 키가 `uint16_t` 면 4바이트)로 줄고, 플래시에 그대로 저장하거나 DMA로 옮겨도 의미가 유지된다 ⑤ 동시 접근이 있으면 alloc/free와 트리 수정을 같은 임계 구역에.

</details>

<details>
<summary>Q6. 같은 키가 여러 번 들어와야 한다면 (예: 같은 만료 시각의 타이머 여러 개)?</summary>

(a) 키를 `(만료시각, 고유 id)` 의 복합 키로 만들어 전순서를 강제 — 리눅스 hrtimer는 같은 시각이면 오른쪽으로 보내 FIFO를 유지한다. (b) 노드에 카운트나 리스트 헤드를 둬서 같은 키를 한 노드에 묶는다. 어느 쪽이든 "같으면 어디로 가는가"를 insert/find/delete가 똑같이 따라야 한다.

</details>

<details>
<summary>Q7. BST와 해시 테이블, 정렬 배열 중 무엇을 고르나요?</summary>

해시: 평균 O(1) 정확 일치만, 순서 질의 불가. 정렬 배열: 탐색 O(log N), 삽입·삭제 O(N) 이동, 캐시·플래시 친화(상수 테이블은 `const` 로 ROM에). BST(균형): 탐색·삽입·삭제 모두 O(log N) + **순서 질의**(floor/ceil, 범위, 다음 원소). "LBA x를 포함하는 extent 찾기"처럼 floor가 필요하면 BST 계열이 자연스럽다 (추가 연습 3).

</details>

## 9. 추가 연습

### 연습 1 — 재귀 없는 inorder

정적 스택만으로 inorder를 구현하고, 정렬 삽입으로 만든 높이 128 트리에서도 정렬 결과가 나오는지 확인하라.

<details>
<summary>답 보기</summary>

`while (n || sp) { while (n) { push(n); n = n->left; } n = pop(); visit(n); n = n->right; }`. 각 노드가 한 번 push 되므로 스택 크기 N이면 충분하다. 편향 트리에서 재귀 버전은 프레임 128개, 이 버전은 포인터 128개(512B/1KB) — 그리고 **크기가 컴파일 타임에 고정**되어 스택 분석(`-fstack-usage`)에 잡힌다.

</details>

### 연습 2 — 범위 질의 [lo, hi]

`bst_range(t, lo, hi, out)` — 구간 안 키를 정렬 순서로. 구간과 무관한 서브트리는 방문하지 않음을 방문 횟수로 보여라.

<details>
<summary>답 보기</summary>

inorder에 가지치기: `lo < key` 일 때만 왼쪽, `key < hi` 일 때만 오른쪽으로 내려간다. [33, 52] 질의는 10개 중 7개 노드만 방문한다. 복잡도 O(h + k) (k = 결과 개수). "지난 100ms 동안의 이벤트" 같은 시간 창 질의에 쓴다.

</details>

### 연습 3 — floor: x 이하 최대 키 (extent 맵)

extent 시작 LBA `{0, 100, 250, 400, 700}` 이 BST에 있을 때 `floor(260) == 250`. 더 작은 키가 없으면 `false`.

<details>
<summary>답 보기</summary>

내려가면서 `key < x` 인 노드를 "후보"로 기록하고 오른쪽으로(더 큰 후보를 찾아), `key > x` 면 왼쪽으로. 같으면 즉시 반환. O(h). SSD FTL의 extent/range 매핑, 메모리 맵 영역 조회("주소 A는 어느 MPU 영역?")가 정확히 이 연산이다.

</details>

### 연습 4 — 키 복사 없는 삭제 (노드 이동)

노드 주소를 외부 핸들로 들고 있을 때를 위해, 자식 2개 삭제에서 후계자 **노드**를 떼어 지울 노드 자리에 옮겨 달아라. 삭제 후 키 35·80 노드의 주소가 그대로인지 확인.

<details>
<summary>답 보기</summary>

① `*s = succ->right` 로 후계자를 원래 자리에서 먼저 떼어내고 ② `succ->left = n->left; succ->right = n->right;` ③ `*link = succ`. 순서가 중요하다: ①을 먼저 해야 후계자가 `n->right` 자신인 경우에도 `n->right` 가 이미 `succ->right` 로 갱신되어 ②가 맞게 동작한다. 리눅스 `rb_erase` 가 노드를 옮기는 이유도 같다 — 노드는 다른 구조체 안에 박혀 있다(침입형).

</details>

### 연습 5 — 정렬 배열로 높이 최소 BST 만들기

정렬된 127개 키로 높이 7의 트리를 만들어라.

<details>
<summary>답 보기</summary>

가운데 원소를 루트로, 왼쪽 절반·오른쪽 절반을 재귀로. 재귀 깊이는 log₂N 이라 안전. 부팅 시 `const` 정렬 테이블(플래시)에서 균형 트리를 한 번 만들어 두고, 런타임에 소수의 삽입·삭제만 일어나는 경우에 쓸 만하다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 17/17 passed
```

## 10. 핵심 정리

- 불변식: 왼쪽 서브트리 전체 `<` 키 `<` 오른쪽 서브트리 전체 → inorder = 정렬.
- `Node **link` 로 내려가면 insert는 `*link = new`, delete는 `*link = child` 한 줄. 루트 분기 없음.
- 삭제 자식 2개 = 후계자(오른쪽 한 번, 왼쪽 끝)로 바꿔 자식 ≤1개 문제로 축소. 후계자가 바로 오른쪽 자식인 경우를 테스트하라.
- 순회: inorder(정렬), preorder(복제, 스택은 right 먼저 push), level-order(큐). 각 노드 1회 → 정적 배열 크기 N 충분.
- 유효성은 (lo, hi) 구간을 내려보내 검사. 경계는 한 칸 넓은 타입으로.
- 정렬된 키 삽입 → 높이 N (측정: 127 → 127, 랜덤 평균 14.1). 실시간 보증이 필요하면 균형 트리.
- 펌웨어: 재귀 깊이 = 숨은 스택. 정적 풀 + `used == count` 누수 검사 + 참조 모델 랜덤 테스트.

## 참고자료

- CLRS, *Introduction to Algorithms* 3판 12장 Binary Search Trees (TREE-DELETE, TRANSPLANT)
- [Linux kernel docs — Red-black Trees (rbtree)](https://docs.kernel.org/core-api/rbtree.html) — `rb_link_node` 의 `struct rb_node **link` 관용구
- [Linux kernel include/linux/rbtree.h](https://github.com/torvalds/linux/blob/master/include/linux/rbtree.h)
- Robert Sedgewick & Kevin Wayne, *Algorithms* 4판 3.2절 Binary Search Trees (Hibbard deletion)
- MISRA C:2012 Rule 17.2 — 재귀 금지 근거
- [GCC `-fstack-usage` 문서](https://gcc.gnu.org/onlinedocs/gcc/Developer-Options.html) — 함수별 스택 사용량 분석
