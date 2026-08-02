# 13. 트리 & 이진탐색트리 (Trees & BST) — Q1~Q9 🌳

> DSA 트랙의 관문. **재귀 순회**, **명시적 스택/큐로의 반복 변환**, 그리고
> **BST 불변식**이 핵심이다. 임베디드에선 파싱 트리·룩업 테이블·상태 계층 탐색으로
> 곧장 이어진다.

---

## 왜 이 주제가 중요한가 (임베디드/Anduril 관점)

- **트리 = 파싱/표현식 구조**: 명령 프로토콜 파서, 설정(config) AST, 표현식 평가기가
  전부 트리다. pre/in/post 순회는 이들을 직렬화/평가하는 기본기.
- **BST = 정렬된 인덱스/룩업**: "키로 O(log n) 조회 + 정렬 순회"가 필요한 곳(주소↔핸들
  매핑, 시간순 이벤트 인덱스). inorder 가 곧 정렬 출력.
- **BFS(큐) = 상태/그래프 계층 탐색**: FSM 도달성, 토폴로지 계층, 최단 홉 탐색.
- **재귀 -> 반복 변환**: MCU/ISR 은 콜스택이 얕다. 깊은 재귀는 스택 오버플로 위험 →
  **명시적 스택**으로 반복화하는 기술이 실전에서 요구된다.

---

## 핵심 개념 치트시트

| 개념 | 한 줄 요약 |
|------|-----------|
| 순회 3형제 | 값 기록 **시점**만 다름: pre(루트먼저)/in(중간)/post(마지막). |
| BST inorder | 오름차순 정렬 결과 == 트리가 BST 인지의 힌트. |
| 반복 inorder | 노드 포인터 **명시적 스택**: 왼쪽 끝까지 push → pop 기록 → 오른쪽. |
| 레벨오더 BFS | 배열 **FIFO 큐**: dequeue → 기록 → 좌·우 자식 enqueue. |
| max depth | `1 + max(왼, 오)`, 빈 트리 0. |
| invert | 각 노드 좌/우 자식 swap (재귀). |
| 균형 판정 | 높이 반환하되 불균형이면 **-1 조기 전파** → O(n) (naive 는 O(n²)). |
| BST 불변식 | 왼 서브트리 **전체** < 노드 < 오 서브트리 **전체**. |
| validate BST | 국소 비교론 부족 → **(lo, hi) 경계** 좁히기. |
| LCA on BST | 값이 둘 다 작으면 왼, 둘 다 크면 오, **갈라지면 현재**. O(h). |

---

## 노드 정의 & 순회 (Q1)

```c
typedef struct tnode { int val; struct tnode *left, *right; } tnode_t;

void tree_inorder(const tnode_t *n, int *out, size_t *k) {
    if (!n) return;
    tree_inorder(n->left,  out, k);
    out[(*k)++] = n->val;          // 왼쪽 다 본 뒤 루트  (pre 는 이 줄을 맨 위로)
    tree_inorder(n->right, out, k);
}
```

- **암기 포인트**: 세 순회는 재귀 구조가 같고 `out[(*k)++]=n->val;` 한 줄의 **위치**만
  다르다. pre=맨 위, in=가운데, post=맨 아래.
- 복잡도: O(n) 시간, O(h) 콜스택(h=높이). 균형이면 h≈log n, 편향이면 h=n.

---

## 반복 inorder = 명시적 스택 (Q2)

```c
size_t tree_inorder_iter(const tnode_t *root, int *out) {
    size_t n = tree_count(root);
    const tnode_t **stk = malloc(n * sizeof *stk);   // 힙 대신 정적 배열도 가능
    size_t top = 0, k = 0;
    const tnode_t *cur = root;
    while (cur || top > 0) {
        while (cur) { stk[top++] = cur; cur = cur->left; }  // 왼쪽 끝까지 push
        cur = stk[--top];                                   // pop
        out[k++] = cur->val;                                // 방문
        cur = cur->right;                                   // 오른쪽으로
    }
    free(stk);
    return k;
}
```

- **왜?** 재귀의 콜스택을 **자기 자료구조로 대체** → 깊이를 우리가 통제. ISR/베어메탈에서
  스택 오버플로를 피하는 정석. 스택 용량은 트리 높이 상한(=노드 수)으로 잡으면 안전.

---

## 레벨오더 BFS = 큐 (Q3)

```c
size_t tree_level_order(const tnode_t *root, int *out) {
    if (!root) return 0;
    size_t n = tree_count(root);
    const tnode_t **q = malloc(n * sizeof *q);
    size_t head = 0, tail = 0, k = 0;
    q[tail++] = root;                       // enqueue
    while (head < tail) {                    // 큐가 빌 때까지
        const tnode_t *cur = q[head++];      // dequeue
        out[k++] = cur->val;
        if (cur->left)  q[tail++] = cur->left;
        if (cur->right) q[tail++] = cur->right;
    }
    free(q); return k;
}
```

- **배열 FIFO**: `head/tail` 인덱스만 전진(링버퍼 아님, 재사용 안 함). 용량 = 노드 수라
  래핑 불필요. 레벨 경계가 필요하면 dequeue 전에 `tail-head`(현재 레벨 크기)를 스냅샷.

---

## 최대 깊이 · 뒤집기 (Q4, Q5)

```c
int tree_max_depth(const tnode_t *r) {
    if (!r) return 0;
    int l = tree_max_depth(r->left), rr = tree_max_depth(r->right);
    return (l > rr ? l : rr) + 1;
}

tnode_t *tree_invert(tnode_t *r) {          // mirror
    if (!r) return NULL;
    tnode_t *l = tree_invert(r->left), *rt = tree_invert(r->right);
    r->left = rt; r->right = l;              // 좌우 교환
    return r;
}
```

---

## 높이 균형 (Q6) — -1 조기 전파

```c
static int balanced_height(const tnode_t *r) {
    if (!r) return 0;
    int l = balanced_height(r->left);   if (l < 0) return -1;   // 이미 불균형
    int rr = balanced_height(r->right); if (rr < 0) return -1;
    if ((l > rr ? l - rr : rr - l) > 1) return -1;             // 이 노드 불균형
    return (l > rr ? l : rr) + 1;
}
bool tree_is_balanced(const tnode_t *r) { return balanced_height(r) >= 0; }
```

- **핵심**: 높이 계산과 균형 검사를 **한 번의 후위 순회**로 합친다. 각 노드에서 높이를
  따로 다시 재는 naive 구현은 O(n²). -1 을 "불균형 신호"로 위로 전파해 O(n).

---

## BST: 삽입/탐색 · 유효성 · LCA (Q7~Q9)

```c
tnode_t *bst_insert(tnode_t *r, int v) {
    if (!r) return node_new(v);
    if (v < r->val)      r->left  = bst_insert(r->left,  v);
    else if (v > r->val) r->right = bst_insert(r->right, v);
    return r;                                 // v == r->val : 중복 무시
}
bool bst_search(const tnode_t *r, int v) {
    while (r) { if (v == r->val) return true; r = v < r->val ? r->left : r->right; }
    return false;
}
```

### validate BST — 경계 좁히기 (흔한 함정)

```c
static bool valid(const tnode_t *r, long lo, long hi) {   // long: INT 경계 오버플로 회피
    if (!r) return true;
    if (r->val <= lo || r->val >= hi) return false;
    return valid(r->left, lo, r->val) && valid(r->right, r->val, hi);
}
bool bst_is_valid(const tnode_t *r) { return valid(r, LONG_MIN, LONG_MAX); }
```

- **함정**: "노드 > 왼자식 && 노드 < 오른자식"만 보는 국소 검사는 **틀린다**.
  `[5,1,4,_,_,3,6]` 은 국소적으론 통과하지만 4 가 5 의 오른쪽에 있어 위반 → 조상 경계를
  전달해야 한다. 대안: **inorder 가 순증가**인지 검사(이전 값 하나만 기억).

### LCA on BST — O(h)

```c
tnode_t *bst_lca(tnode_t *r, int a, int b) {
    while (r) {
        if (a < r->val && b < r->val)      r = r->left;
        else if (a > r->val && b > r->val) r = r->right;
        else return r;                        // 분기점(또는 하나가 현재와 일치) = LCA
    }
    return NULL;
}
```

- 일반 이진트리 LCA 는 O(n) 재귀지만, **BST 는 값 비교만으로 O(h)**. BST 성질을 쓰는
  대표 문제.

---

## 인터뷰 팔로업 & 임베디드 함정

- **Q. 재귀 순회를 왜 반복으로 바꾸나?** → MCU 콜스택은 수 KB. 깊이 큰(편향) 트리에서
  재귀는 스택 오버플로 → 명시적 스택으로 깊이를 통제.
- **Q. BFS 큐를 힙 없이?** → `POOL_SIZE` 정적 배열 + head/tail 인덱스. 용량은 트리
  노드 수 상한으로 컴파일 타임 확보.
- **Q. validate BST 국소 검사의 반례?** → `[5,1,4,_,_,3,6]`. 경계 전달 또는 inorder
  단조성으로 해결.
- **함정: validate 에서 INT_MIN/INT_MAX 노드** → int 경계로 비교하면 오버플로/등호
  문제. `long` 경계 + 열린구간(`<=`, `>=`)으로 안전하게.
- **함정: 편향 트리에서 h=n** → BST 라도 정렬 삽입하면 링크드리스트가 됨. 실무는
  self-balancing(AVL/RB)이 필요하지만 인터뷰 기본은 단순 BST.
- **함정: 순회 버퍼 크기** → out 배열은 노드 수 이상 확보. `tree_count` 로 사전 산정.
- **함정: 사이클(손상 포인터)** → 진짜 트리는 사이클이 없지만, 손상 시 순회가 무한
  루프. 방어적 코드에선 방문 상한/깊이 상한을 둔다.

---

## 복잡도 요약

| 연산 | 시간 | 공간 |
|------|------|------|
| pre/in/post 순회 | O(n) | O(h) 스택 |
| 반복 inorder | O(n) | O(h) 명시적 스택 |
| 레벨오더 BFS | O(n) | O(w) 큐 (w=최대 폭) |
| max depth / invert | O(n) | O(h) |
| is balanced | O(n) | O(h) |
| BST insert / search | O(h) | O(h) 재귀 / O(1) 반복 |
| validate BST | O(n) | O(h) |
| LCA on BST | O(h) | O(1) |

> h = 높이 (균형 시 log n, 편향 시 n), w = 최대 레벨 폭, n = 노드 수.
