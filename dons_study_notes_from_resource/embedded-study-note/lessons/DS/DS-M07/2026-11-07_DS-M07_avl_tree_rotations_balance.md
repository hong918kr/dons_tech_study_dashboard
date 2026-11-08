---
id: DS-M07
time: 90분
updated: 2026-11-07
---
# DS-M07 AVL 트리: 회전과 균형 유지

## 학습 목표

- AVL 불변식(invariant) "모든 노드에서 왼쪽·오른쪽 높이 차 ≤ 1"을 말로 정확히 쓰고, 높이·균형 인자(balance factor)·순서를 한 번에 검사하는 **검사기(checker)** 를 만든다.
- 회전 4종(LL/RR/LR/RL)을 그림 없이도 포인터 3개 교체로 구현하고, 회전이 inorder 순서를 왜 보존하는지 설명한다.
- 삭제에서만 나오는 "자식 bf = 0" 경우와 회전 연쇄(cascade)를 이해하고, 후계자 노드를 **복사가 아닌 재연결**로 옮긴다.
- 높이 상한 h < 1.4404·log₂(n+2) − 0.328 을 Fibonacci 점화식으로 증명하고, 정렬 삽입에서 AVL vs 일반 BST(DS-B13) 높이를 실측표로 비교한다.
- AVL과 레드블랙 트리(Linux `rbtree`)의 트레이드오프를 면접에서 근거와 함께 말한다.

## 1. 문제

펌웨어의 키는 대부분 **단조 증가**한다 — 타임스탬프, 시퀀스 번호, 순차 쓰기의 LBA. DS-B13의 일반 BST에 이런 키를 넣으면 연결 리스트가 되어(높이 = n) 조회가 O(n), 재귀 순회는 스택을 n 프레임 먹는다. 자가 균형 트리(self-balancing tree)는 **최악에도** 높이를 O(log n)으로 묶어 실시간 시스템이 요구하는 "상한"을 준다. 면접에서는 "회전을 손으로 짜 보라", "AVL과 red-black 중 뭘 쓰겠나"로 나온다.

**AVL 불변식**: 모든 노드 v에 대해 `bf(v) = h(v.left) − h(v.right)` 가 −1, 0, +1 중 하나. 높이 `h` 는 DS-B13과 같은 **노드 개수 기준**(빈 트리 0, 잎 1)이다. 노드마다 자기 높이를 `int8_t h` 로 들고 다니며, 연산 후 경로를 거슬러 오르며 고친다.

```svg
<svg viewBox="0 0 720 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">키 1..7 을 정렬 순서로 삽입</text>
  <text x="10" y="44" fill="currentColor" font-size="12">일반 BST (DS-B13): 오른쪽 사슬, 높이 7</text>
  <text x="390" y="44" fill="currentColor" font-size="12">AVL: 회전 4번 후 완전 이진 트리, 높이 3</text>
  <g stroke="currentColor" fill="none">
    <path d="M55 72 L97 104"/><path d="M97 104 L139 136"/><path d="M139 136 L181 168"/>
    <path d="M181 168 L223 200"/><path d="M223 200 L265 232"/><path d="M265 232 L307 264"/>
    <path d="M540 80 L450 140"/><path d="M540 80 L630 140"/>
    <path d="M450 150 L405 210"/><path d="M450 150 L495 210"/>
    <path d="M630 150 L585 210"/><path d="M630 150 L675 210"/>
  </g>
  <g class="f-warn" stroke="currentColor">
    <rect x="40" y="61" width="30" height="22" rx="5"/><rect x="82" y="93" width="30" height="22" rx="5"/>
    <rect x="124" y="125" width="30" height="22" rx="5"/><rect x="166" y="157" width="30" height="22" rx="5"/>
    <rect x="208" y="189" width="30" height="22" rx="5"/><rect x="250" y="221" width="30" height="22" rx="5"/>
    <rect x="292" y="253" width="30" height="22" rx="5"/>
  </g>
  <g class="f-ok" stroke="currentColor">
    <rect x="522" y="62" width="36" height="24" rx="5"/>
    <rect x="432" y="132" width="36" height="24" rx="5"/><rect x="612" y="132" width="36" height="24" rx="5"/>
    <rect x="387" y="202" width="36" height="24" rx="5"/><rect x="477" y="202" width="36" height="24" rx="5"/>
    <rect x="567" y="202" width="36" height="24" rx="5"/><rect x="657" y="202" width="36" height="24" rx="5"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="55" y="77">1</text><text x="97" y="109">2</text><text x="139" y="141">3</text><text x="181" y="173">4</text>
    <text x="223" y="205">5</text><text x="265" y="237">6</text><text x="307" y="269">7</text>
    <text x="540" y="79">4</text><text x="450" y="149">2</text><text x="630" y="149">6</text>
    <text x="405" y="219">1</text><text x="495" y="219">3</text><text x="585" y="219">5</text><text x="675" y="219">7</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="595" y="70">h=3 bf=0</text><text x="450" y="174">h=2 bf=0</text><text x="630" y="174">h=2 bf=0</text>
    <text x="405" y="244">h=1</text><text x="495" y="244">h=1</text><text x="585" y="244">h=1</text><text x="675" y="244">h=1</text>
  </g>
  <text x="150" y="292" fill="currentColor" font-size="12" text-anchor="middle">bf(1) = 0 − 6 = −6 … 조회 최악 7번 비교</text>
  <text x="540" y="282" fill="currentColor" font-size="12" text-anchor="middle">모든 노드 |bf| ≤ 1 → 조회 최악 3번 비교</text>
</svg>
```

노드는 `{ left, right, int32_t key, uint32_t val, int8_t h }`. 이 Mac(arm64)에서 `sizeof(Node)` = 32바이트(포인터 8×2 + 4 + 4 + 1 + 패딩 7), Cortex-M(포인터 4바이트)에서는 20바이트다(`clang --target=thumbv7m-none-eabi` 로 `_Static_assert(sizeof(Node) == 20)` 컴파일 확인). 저장소는 `Node g_pool[4096]` 정적 풀 + free list(DS-B13과 같음), malloc 없음.

### Part A — 회전 4종 + 삽입 + 검사기

- `rot_right(y)` / `rot_left(x)` — 새 서브트리 루트를 반환. 높이 갱신 포함. `g_rot++` 은 측정용으로 주어진다.
- `rebalance(n)` — 전제: n의 두 서브트리는 AVL이고 높이 차 ≤ 2. 높이를 고치고 `bf == ±2` 면 LL/LR/RR/RL 판정 후 회전.
- `insert_rec(n, key, val, &added)` — BST 삽입 재귀 + 돌아오는 길에 `rebalance`. 중복이나 풀 고갈이면 트리를 바꾸지 않는다.
- `check_rec(n, lo, hi)` — ① 순서 `(lo, hi)` ② 저장된 `h` == 실제 높이 ③ `|bf| ≤ 1` 을 모두 만족하면 실제 높이, 아니면 −1.
- 테스트: 키 3개로 4종 회전을 각각 만들고 결과 모양·회전 수 확인, 1..7 정렬 삽입 → 완전 이진 트리, 검사기가 "높이는 맞지만 기운 트리 / 높이 필드가 틀린 트리 / 순서가 틀린 트리"를 잡는지.

### Part B — 삭제

- `detach_min(n, &min)` — 서브트리의 최소 노드를 떼어 내고 균형 맞춘 새 루트를 반환.
- `delete_rec(n, key, &removed)` — 자식 0/1개면 자식이 자리를 잇고, 2개면 후계자(successor) **노드를 떼어 와 그 자리에 연결**(키 복사 X). 경로를 따라 `rebalance`.
- 테스트: 삭제에서만 생기는 "자식 bf = 0" 경우가 **단일** 회전으로 처리되는지, 자식 2개 삭제 후 후계자 노드의 **포인터가 그대로**인지, 전부 지우면 풀 누수 0. 랜덤 삽입·삭제 60000번을 bool 배열 참조 모델과 대조하며 매번 `avl_check()`.

### Part C — 순차 쓰기 익스텐트 맵 + 높이 측정

- SSD/플래시 펌웨어의 익스텐트 맵(extent map): key = 시작 LBA, val = 길이. 호스트가 순차로 쓰면 시작 LBA가 단조 증가 → **정렬 삽입**.
- `avl_floor(t, key)` (DS-B13 연습 3의 floor) 와 `extent_lookup(t, lba, &start)` — LBA를 덮는 익스텐트를 찾거나 틈(gap)이면 false.
- 익스텐트 3000개를 순차로 넣고, 전체 LBA 공간을 참조 배열과 대조, 무작위 TRIM(삭제) 1500개 후 다시 대조.
- 측정: n = 15, 127, 1023, 4095 에서 오름차순·내림차순·랜덤 삽입 높이를 AVL과 일반 BST로 비교하고 이론 최악 높이와 나란히 출력.

## 2. Problem (English)

> **AVL tree on a static node pool.**
>
> Nodes are `struct { Node *left, *right; int32_t key; uint32_t val; int8_t h; }` taken from a fixed pool of 4096; no dynamic allocation. `h` is the node-count height (leaf = 1, empty = 0).
>
> 1. Implement `rot_left`, `rot_right`, and `rebalance` (the LL/RR/LR/RL cases), then `avl_insert(t, key, val)` returning `false` on a duplicate key or an exhausted pool, in `O(log n)`.
> 2. Implement `avl_delete(t, key)` in `O(log n)`. When the node has two children, splice in its in-order successor node (do not copy keys), so pointers to other nodes stay valid.
> 3. Implement a checker that verifies BST order, the stored heights, and `|balance| ≤ 1` at every node. Run it after every operation in a randomized test against a reference model.
> 4. Use the tree as an LBA extent map: `extent_lookup(t, lba, &start)` returns the extent `[start, start+len)` containing `lba`, or `false` for a gap.
> 5. Measure the height after inserting `n` keys in ascending order and compare with an unbalanced BST.
>
> Example: inserting `1, 2, …, 7` yields preorder `4 2 1 3 6 5 7`, height 3, after 4 single rotations.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "6/30 passed", exit 1 — TODO 9곳을 채워 30/30 을 만든다
```

TODO: `rot_right`, `rot_left`, `rebalance`, `insert_rec`, `check_rec` (Part A) / `detach_min`, `delete_rec` (Part B) / `avl_floor`, `extent_lookup` (Part C). 정적 풀, `ht()`·`fix_height()`·`bf()` 도우미, 래퍼 `avl_insert/avl_delete/avl_check`, 비교용 일반 BST `bst_insert_plain`, 최악 높이 계산 `avl_worst_height`, 하네스는 주어진다.

## 4. 힌트

<details>
<summary>힌트 1 — 회전은 "포인터 2개 바꾸고 높이 2개 고치기"</summary>

`rot_right(y)`: `x = y->left` 가 올라온다. x의 오른쪽 서브트리 B는 x보다 크고 y보다 작으므로 y의 새 왼쪽이 된다. 바뀌는 링크는 `y->left` 와 `x->right` 두 개뿐(부모의 링크는 반환값으로 호출자가 바꾼다). 높이는 **아래로 내려간 y를 먼저**, 그 다음 x.

</details>

<details>
<summary>힌트 2 — 4가지 경우는 "어느 쪽이 무겁고, 그 자식은 어느 쪽으로 기울었나"</summary>

`bf(n) = +2` (왼쪽이 무거움): 왼쪽 자식이 같은 방향(bf ≥ 0)이면 LL → `rot_right(n)`. 반대 방향(bf < 0)이면 LR → 먼저 `n->left = rot_left(n->left)` 로 LL 모양을 만든 뒤 `rot_right(n)`. `−2` 는 거울상. "bf ≥ 0" 에 **0이 포함**되는 것이 핵심 — 삭제에서 나온다.

</details>

<details>
<summary>힌트 3 — 재귀 삽입은 "링크를 반환값으로 다시 쓴다"</summary>

`n->left = insert_rec(n->left, ...)` 형태로 쓰면 아래에서 회전이 일어나 서브트리 루트가 바뀌어도 부모 링크가 자동으로 갱신된다. 이중 포인터도, 부모 포인터도 필요 없다. 돌아오면서 `rebalance(n)` 을 부르면 경로 위의 모든 높이가 고쳐진다. 새 노드가 안 생겼으면(중복) 아무것도 바뀌지 않았으니 그냥 `n` 을 반환.

</details>

<details>
<summary>힌트 4 — 검사기는 높이를 "반환값"으로 올린다</summary>

`check_rec` 이 실제 높이를 반환하게 하면 한 번의 후위 순회로 순서·균형·저장된 높이를 모두 확인할 수 있다(O(n)). 저장된 `n->h` 를 믿고 bf를 계산하면 "높이 필드가 틀린" 버그를 못 잡는다 — 반드시 자식이 반환한 **실제** 높이로 비교하라. 순서는 DS-B13처럼 `int64_t` 열린 구간 (lo, hi).

</details>

<details>
<summary>힌트 5 — 삭제의 자식 2개 경우</summary>

오른쪽 서브트리에서 `detach_min` 으로 후계자 s를 떼어 낸다(s는 왼쪽 자식이 없으니 s의 오른쪽이 자리를 잇는다). 그리고 지울 노드 자리에 `s->left = l; s->right = 새 오른쪽;` 으로 s를 앉히고 `rebalance(s)`. `detach_min` 도 돌아오는 길에 `rebalance` 해야 한다 — 오른쪽 서브트리 안에서도 높이가 줄었기 때문.

</details>

## 5. 풀이

### Part A — 회전: inorder를 보존하는 링크 교체

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m07a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">LL 경우: rot_right(y) — 삽입으로 A 가 k+1 로 자라 bf(y) = +2</text>
  <g stroke="currentColor" fill="none">
    <path d="M160 70 L105 120"/><path d="M160 70 L230 135"/><path d="M95 140 L55 180"/><path d="M95 140 L145 180"/>
    <path d="M520 70 L460 130"/><path d="M520 70 L600 120"/><path d="M610 140 L560 180"/><path d="M610 140 L670 180"/>
  </g>
  <rect x="140" y="50" width="40" height="26" rx="6" class="f-warn" stroke="currentColor"/>
  <rect x="75" y="118" width="40" height="26" rx="6" class="f-acc" stroke="currentColor"/>
  <rect x="500" y="50" width="40" height="26" rx="6" class="f-acc" stroke="currentColor"/>
  <rect x="590" y="118" width="40" height="26" rx="6" class="f-ok" stroke="currentColor"/>
  <g class="f-mut" stroke="currentColor">
    <polygon points="230,120 200,175 260,175"/><polygon points="55,170 25,225 85,225"/><polygon points="145,170 115,215 175,215"/>
    <polygon points="460,120 430,175 490,175"/><polygon points="560,170 530,215 590,215"/><polygon points="670,170 640,215 700,215"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="160" y="68">y</text><text x="95" y="136">x</text><text x="520" y="68">x</text><text x="610" y="136">y</text>
    <text x="230" y="163">C</text><text x="55" y="213">A</text><text x="145" y="203">B</text>
    <text x="460" y="163">A</text><text x="560" y="203">B</text><text x="670" y="203">C</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="230" y="190">k</text><text x="55" y="240">k+1</text><text x="145" y="230">k</text>
    <text x="460" y="190">k+1</text><text x="560" y="230">k</text><text x="670" y="230">k</text>
    <text x="215" y="58">bf +2</text><text x="40" y="128">bf +1</text>
    <text x="595" y="58">h = k+2, bf 0</text><text x="665" y="128">h = k+1</text>
  </g>
  <path d="M300 130 L400 130" stroke="currentColor" stroke-width="2" fill="none" marker-end="url(#m07a)"/>
  <text x="350" y="120" fill="currentColor" text-anchor="middle" font-family="monospace">rot_right(y)</text>
  <text x="10" y="258" fill="currentColor" font-size="12">inorder: A &lt; x &lt; B &lt; y &lt; C 는 그대로. 바뀐 링크 y-&gt;left = B, x-&gt;right = y 두 개 + 부모 링크(반환값).</text>
</svg>
```

**왜 inorder가 보존되나**: B의 모든 키는 x의 오른쪽에 있었으니 x보다 크고, y의 왼쪽에 있었으니 y보다 작다. 회전 후 B는 y의 왼쪽 = "x보다 크고 y보다 작은 자리"에 그대로 있다. A와 C는 상대 위치가 바뀌지 않는다. 그래서 회전은 **BST 속성을 절대 깨지 않고 높이만 옮긴다** — 왼쪽 서브트리를 한 칸 올리고 오른쪽을 한 칸 내린다.

**높이 갱신 순서**: 회전 후 y는 x의 자식이다. x의 높이는 y의 새 높이에 의존하므로 y 먼저, x 나중. 순서를 바꾸면 x가 y의 **옛** 높이로 계산된다(7장 실수 2에서 실제로 깨지는 출력).

**4가지 경우** (`rebalance` 의 전부):

| 경우 | 조건 | 처리 | 단일 회전 수 |
|---|---|---|---|
| LL | `bf(n) = +2`, `bf(n->left) ≥ 0` | `rot_right(n)` | 1 |
| LR | `bf(n) = +2`, `bf(n->left) < 0` | `n->left = rot_left(n->left)` 후 `rot_right(n)` | 2 |
| RR | `bf(n) = −2`, `bf(n->right) ≤ 0` | `rot_left(n)` | 1 |
| RL | `bf(n) = −2`, `bf(n->right) > 0` | `n->right = rot_right(n->right)` 후 `rot_left(n)` | 2 |

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m07b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">LR 경우: 지그재그를 먼저 일자로 펴고(rot_left), 그 다음 LL 처럼(rot_right)</text>
  <g stroke="currentColor" fill="none">
    <path d="M160 52 L95 102"/><path d="M160 52 L215 110"/><path d="M90 120 L50 165"/><path d="M90 120 L135 152"/>
    <path d="M135 170 L108 205"/><path d="M135 170 L165 205"/>
    <path d="M415 52 L350 102"/><path d="M415 52 L470 110"/><path d="M345 120 L300 152"/><path d="M345 120 L390 162"/>
    <path d="M300 170 L272 205"/><path d="M300 170 L330 205"/>
    <path d="M615 52 L565 102"/><path d="M615 52 L675 102"/><path d="M565 120 L540 162"/><path d="M565 120 L592 162"/>
    <path d="M675 120 L650 162"/><path d="M675 120 L705 162"/>
  </g>
  <g stroke="currentColor">
    <rect x="142" y="36" width="36" height="24" rx="5" class="f-warn"/><rect x="72" y="104" width="36" height="24" rx="5" class="f-mut"/>
    <rect x="117" y="152" width="36" height="24" rx="5" class="f-acc"/>
    <rect x="397" y="36" width="36" height="24" rx="5" class="f-warn"/><rect x="327" y="104" width="36" height="24" rx="5" class="f-acc"/>
    <rect x="282" y="152" width="36" height="24" rx="5" class="f-mut"/>
    <rect x="597" y="36" width="36" height="24" rx="5" class="f-acc"/><rect x="547" y="104" width="36" height="24" rx="5" class="f-ok"/>
    <rect x="657" y="104" width="36" height="24" rx="5" class="f-ok"/>
  </g>
  <g class="f-mut" stroke="currentColor">
    <polygon points="215,100 195,140 235,140"/><polygon points="50,155 30,195 70,195"/>
    <polygon points="108,195 92,230 124,230"/><polygon points="165,195 149,230 181,230"/>
    <polygon points="470,100 450,140 490,140"/><polygon points="390,152 374,187 406,187"/>
    <polygon points="272,195 256,230 288,230"/><polygon points="330,195 314,230 346,230"/>
    <polygon points="540,152 524,187 556,187"/><polygon points="592,152 576,187 608,187"/>
    <polygon points="650,152 634,187 666,187"/><polygon points="705,152 689,187 721,187"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="160" y="53">z</text><text x="90" y="121">y</text><text x="135" y="169">x</text>
    <text x="215" y="134">D</text><text x="50" y="189">A</text><text x="108" y="224">B</text><text x="165" y="224">C</text>
    <text x="415" y="53">z</text><text x="345" y="121">x</text><text x="300" y="169">y</text>
    <text x="470" y="134">D</text><text x="390" y="181">C</text><text x="272" y="224">A</text><text x="330" y="224">B</text>
    <text x="615" y="53">x</text><text x="565" y="121">y</text><text x="675" y="121">z</text>
    <text x="540" y="181">A</text><text x="592" y="181">B</text><text x="650" y="181">C</text><text x="705" y="181">D</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="182" y="40">+2</text><text x="40" y="110">−1</text>
    <text x="230" y="70" text-anchor="middle">rot_left(y)</text><text x="490" y="70" text-anchor="middle">rot_right(z)</text>
  </g>
  <g stroke="currentColor" stroke-width="2" fill="none" marker-end="url(#m07b)">
    <path d="M200 80 L262 80"/><path d="M455 80 L517 80"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="256">높이: A = D = k, x = k+1 (B, C 는 k 와 k−1, 또는 x 가 새 잎이면 k = 0 으로 모두 빈 트리)</text>
    <text x="10" y="276">결과: y(A, B), z(C, D) 모두 높이 k+1, x 는 k+2 = 삽입 전 z 의 높이 → 위쪽 조상은 아무것도 모른다</text>
  </g>
</svg>
```

**삽입 trace** — 1..7 정렬 삽입 (실제 출력 `insert 1..7 -> rotations=4 height=3`):

| 삽입 | 처음 깨지는 노드 (bf) | 자식 bf | 경우 | 결과 (preorder) |
|---|---|---|---|---|
| 1, 2 | — | — | — | 1 2 |
| 3 | 1 (−2) | 2 (−1) | RR → `rot_left(1)` | 2 1 3 |
| 4 | — (bf(2) = −1) | — | — | 2 1 3 4 |
| 5 | 3 (−2) | 4 (−1) | RR → `rot_left(3)` | 2 1 4 3 5 |
| 6 | 2 (−2) | 4 (−1) | RR → `rot_left(2)` | 4 2 1 3 5 6 |
| 7 | 5 (−2) | 6 (−1) | RR → `rot_left(5)` | 4 2 1 3 6 5 7 |

키 3개 테스트의 실제 출력 — 네 경우 모두 `20(10, 30)` 높이 2로 끝나고, 이중 회전만 회전 수가 2다:

```text
  [A] LL  insert 30,20,10 -> rotations=1 height=2
  [A] RR  insert 10,20,30 -> rotations=1 height=2
  [A] LR  insert 30,10,20 -> rotations=2 height=2
  [A] RL  insert 10,30,20 -> rotations=2 height=2
```

**삽입은 재구성(restructure) 최대 1번**: 새 잎에서 루트로 오르다 처음 만나는 bf ±2 노드 z에서 회전하면, 위 그림 마지막 줄처럼 z 서브트리의 높이가 **삽입 전 높이로 돌아간다**. 그러면 그 위 조상들의 높이·bf는 삽입 전과 같으므로 더 고칠 게 없다. 그래서 삽입 1번당 단일 회전 ≤ 2 (랜덤 테스트 실측 `max_rot/insert=2`). 우리 재귀 코드는 그래도 루트까지 `rebalance` 를 부르지만(높이 재계산뿐, O(log n)), 반복 버전은 "높이가 안 바뀌면 멈춤"으로 조기 종료할 수 있다.

**검사기 설계** — `check_rec` 은 후위 순회 한 번(O(n))으로 세 가지를 본다:

| 검사 | 잡는 버그 |
|---|---|
| ① 키가 열린 구간 (lo, hi) 안 | 회전에서 B를 잘못된 쪽에 붙임 (지역적으로는 맞아 보이는 트리) |
| ② 저장된 `h` == 자식이 **반환한** 실제 높이 + 1 | 회전 후 높이 갱신 누락/순서 오류 → 다음 연산의 bf 판정이 틀어짐 |
| ③ 두 자식의 실제 높이 차 l − r 이 −1, 0, +1 중 하나 | 회전을 빠뜨리거나 잘못된 회전 종류 |

②가 특히 중요하다. 높이 필드가 틀려도 당장 트리 모양은 멀쩡할 수 있지만, 다음 삽입에서 bf를 잘못 계산해 엉뚱한 회전을 한다 — 버그가 원인 지점에서 멀리 떨어져 나타난다. 매 연산 후 검사기를 돌리면 **원인 연산 바로 그 자리**에서 잡힌다. 하네스는 손으로 만든 "높이는 정확하지만 bf = −2인 사슬", "모양은 맞지만 `h` 만 틀린 트리", "12가 10의 왼쪽에 있는 트리"를 넣어 검사기 자체도 검증한다.

**재귀 깊이는 상한이 있다**: `insert_rec` 의 재귀 깊이 = 트리 높이 ≤ 1.44·log₂(n+2). n = 4096이면 16 프레임 이하다. DS-B13에서 "재귀는 높이 상한이 보장될 때만"이라고 했는데, AVL이 바로 그 보장이다. 그래도 MISRA C:2012 Rule 17.2(재귀 금지)를 따르는 코드베이스라면 연습 3의 경로 스택 버전을 쓴다.

### Part B — 삭제: 자식 bf = 0 과 회전 연쇄

삭제의 뼈대는 DS-B13과 같다(자식 0/1/2개). 다른 점은 ① 돌아오는 길의 `rebalance` ② 후계자를 **노드째 옮긴다**는 것.

```text
삭제 70 전 (회전 없이 50,20,60,10,30,70,5,35 삽입)      삭제 후: 50 의 bf = +2, 왼쪽 자식 20 의 bf = 0
            50 (h4, bf+1)                                   올바른 처리 = 단일 rot_right(50)
          /      \                                                 20
        20        60                                             /    \
       /  \         \                                          10      50
     10    30        70   ← 삭제                               /      /  \
    /        \                                                5     30    60
   5          35                                                      \
                                                                      35
```

**자식 bf = 0은 삭제에서만 생긴다.** 삽입에서는 "방금 자란 쪽"의 자식이 bf 0일 수 없다(새 잎 자신이면 부모가 ±2가 될 수 없다). 삭제에서는 반대쪽이 줄어든 것이라 무거운 쪽 자식이 완벽하게 균형(bf 0)일 수 있다. 이때 **단일** 회전을 해야 한다. 위 예에서 이중 회전(LR)을 하면 30이 루트로 올라가고 20의 왼쪽에 10(5)이, 오른쪽에 빈 트리(30의 옛 왼쪽 = B)가 남아 bf(20) = +2 — 트리가 깨진다(7장 실수 1, 실제 출력). 실제 출력:

```text
  [B] delete 70 (child bf 0) -> rotations=1
```

**후계자는 복사하지 말고 재연결**: 흔한 교과서 코드는 후계자의 키·값을 지울 노드에 **복사**하고 후계자 노드를 지운다. 그러면 바깥에서 후계자 노드를 가리키던 포인터(예: 타이머 핸들, 캐시 엔트리)가 엉뚱한 노드나 해제된 노드를 가리키게 된다. 침입형(intrusive) 트리 — 노드가 다른 구조체 안에 박혀 있는 Linux `rb_node` 같은 경우 — 는 복사가 아예 불가능하다. 그래서 `detach_min` 으로 후계자 노드를 떼어 와 지운 노드 자리에 앉힌다. 테스트가 이를 확인한다: 20을 지운 뒤 `avl_find(30)` 이 지우기 전에 받아 둔 포인터 `p30` 과 같고, 그 노드가 새 루트다.

**삭제는 회전이 연쇄될 수 있다**: 회전 후 서브트리 높이가 **1 줄어들 수 있고**(자식 bf ≠ 0인 경우), 그러면 부모의 bf가 바뀌어 위에서 또 회전이 필요할 수 있다. 그래서 삭제 1번에 회전이 O(log n)번까지 일어난다. 랜덤 테스트 실제 출력:

```text
  [B] random ops=60000 max_size=177 max_height=9 (worst AVL for 177 = 10) max_rot/insert=2 max_rot/delete=6 bad_ops=0
```

삽입은 최대 2(이중 회전 1번), 삭제는 한 번에 단일 회전 6번까지 관측됐다. 연습 2에서 최소 AVL 트리(Fibonacci tree)로 최악을 직접 만들면 높이 16에서 7번이다. 60000 연산 모두에서 정렬 순서·크기·풀 사용량이 참조 모델과 같고 검사기를 통과했다(`bad_ops=0`).

### Part C — 순차 쓰기 익스텐트 맵과 높이 측정

`extent_lookup` 은 floor로 "lba 이하에서 가장 큰 시작 LBA"를 찾고, `lba < start + len` 이면 그 익스텐트, 아니면 틈이다(DS-B13 연습 3의 floor 그대로; 덧셈 넘침을 피하려고 `int64_t` 로 비교). 실제 출력:

```text
  [C] 3000 sequential extents (sorted keys): height=12 (min 12, worst AVL 16)
  [C] after TRIM of 1500 extents: count=1500 height=12 lookup_mismatch=0
```

단조 증가 키 3000개가 들어갔는데 높이는 이론 최소치(⌈log₂ 3001⌉ = 12)다. 일반 BST였다면 3000이고, 재귀 순회는 3000 프레임 — Cortex-M 태스크 스택 1~2KB는 즉시 넘친다(MPU 스택 가드가 없으면 조용한 메모리 오염).

**측정 — 정렬 삽입 높이, AVL vs 일반 BST** (solution.c 실제 출력, 랜덤은 셔플 20회 평균):

```text
  [C]     n | min | AVL worst | AVL asc | AVL desc | AVL rand(avg) | BST asc | BST rand(avg) | rotations(asc)
  [C]    15 |   4 |         5 |       4 |        4 |           5.0 |      15 |           6.6 | 11
  [C]   127 |   7 |         9 |       7 |        7 |           8.2 |     127 |          13.9 | 120
  [C]  1023 |  10 |        14 |      10 |       10 |          12.0 |    1023 |          21.9 | 1013
  [C]  4095 |  12 |        16 |      12 |       12 |          14.2 |    4095 |          27.6 | 4083
```

| n | 최소 ⌈log₂(n+1)⌉ | AVL 최악 (정확) | 1.4404·log₂(n+2) − 0.328 | AVL 오름차순 | AVL 랜덤 평균 | BST 오름차순 | BST 랜덤 평균 |
|---|---|---|---|---|---|---|---|
| 15 | 4 | 5 | 5.56 | 4 | 5.0 | **15** | 6.6 |
| 127 | 7 | 9 | 9.77 | 7 | 8.2 | **127** | 13.9 |
| 1023 | 10 | 14 | 14.08 | 10 | 12.0 | **1023** | 21.9 |
| 4095 | 12 | 16 | 16.96 | 12 | 14.2 | **4095** | 27.6 |

(공식 열만 python으로 계산한 값, 나머지는 프로그램 출력.) 읽는 법:

- **BST 오름차순 = n** — DS-B13에서 본 사슬. AVL은 같은 입력에서 **최소 높이**가 나온다. n = 2^k − 1개를 정렬 삽입하면 완전 이진 트리가 된다(4095 → 12, 테스트로 확인).
- AVL 최악(정확)은 "N(h) ≤ n 인 최대 h"를 점화식으로 계산한 값(`avl_worst_height`)이고, 공식 상한의 정수 부분과 4행 모두 일치한다 — 상한이 느슨하지 않다는 뜻.
- 정렬 삽입의 회전 수는 n − 높이(11 = 15 − 4, 120 = 127 − 7, 1013 = 1023 − 10, 4083 = 4095 − 12). 거의 **매 삽입마다** 회전 1번 — 공짜가 아니다. 그래도 회전은 포인터 2개 + 높이 2개라 비교 몇 번 수준의 비용이다.
- 랜덤 삽입에서 AVL 평균(14.2)도 최소(12)에 가깝고, BST 평균(27.6)은 약 2배다. 그러나 BST의 "평균"은 보증이 아니고, AVL의 16은 보증이다.

**높이 상한 증명 스케치** — N(h) = 높이 h인 AVL 트리가 가질 수 있는 **최소** 노드 수:

1. 높이 h인 트리는 루트 + 한쪽 서브트리 높이 h−1(그래야 전체가 h) + 다른 쪽은 AVL 조건상 최소 h−2. 노드를 최소로 하려면 둘 다 최소 트리를 쓴다: **N(h) = 1 + N(h−1) + N(h−2)**, N(0) = 0, N(1) = 1. → 0, 1, 2, 4, 7, 12, 20, 33, 54, …
2. M(h) = N(h) + 1 로 두면 M(h) = M(h−1) + M(h−2), M(0) = 1 = F(2), M(1) = 2 = F(3) 이므로 **N(h) = F(h+2) − 1** (F(1) = F(2) = 1 인 Fibonacci). 연습 1에서 h = 1..16 모두 실제 트리를 만들어 확인한다.
3. Binet 공식 F(k) = (φ^k − ψ^k)/√5, φ = (1+√5)/2 ≈ 1.618, |ψ| < 1 이라 F(k) > φ^k/√5 − 1/2.
4. 노드 n개인 AVL의 높이가 h면 n ≥ N(h) = F(h+2) − 1 > φ^(h+2)/√5 − 3/2. 따라서 φ^(h+2) < √5·(n + 3/2) < √5·(n+2).
5. 양변에 log_φ: h + 2 < log_φ √5 + log_φ(n+2) → **h < log_φ(n+2) + log_φ √5 − 2 = 1.4404·log₂(n+2) − 0.328** (1/log₂φ = 1.44042, log_φ √5 − 2 = −0.3277).

더 간단한 버전: F(k) ≥ φ^(k−2) (귀납법, φ² = φ + 1)에서 n ≥ φ^h − 1 → h ≤ log_φ(n+1) ≈ 1.44·log₂(n+1). 면접에서는 이 정도면 충분하다. 결론: **AVL의 최악 높이는 완전 이진 트리보다 최대 약 44% 높다**. 레드블랙은 2·log₂(n+1)(100%)까지 허용한다.

**복잡도와 메모리**:

| 연산 | 시간 (최악) | 회전 (최악) | 재귀/스택 깊이 |
|---|---|---|---|
| find / floor | O(log n), 비교 ≤ 1.44 log₂ n | 0 | 반복문, 0 |
| insert | O(log n) | 단일 2 (재구성 1) | 높이 h |
| delete | O(log n) | O(log n) (실측 7, h = 16) | 높이 h |
| check | O(n) | — | 높이 h |

노드당 오버헤드 = 포인터 2개 + 높이 1바이트(패딩으로 실제 +7/+3). 부모 포인터를 안 둔 이유: 재귀(또는 경로 스택)가 부모 역할을 하므로 Cortex-M 기준 노드당 4바이트를 아낀다. 대신 "노드 포인터 하나만 들고 다음 노드로(`rb_next`)" 같은 연산은 부모 포인터 없이는 루트에서 다시 내려와야 한다 — Linux `rb_node` 가 부모 포인터(+색 비트)를 두는 이유다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution            # 30/30 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o sol_asan && ./sol_asan  # 30/30, 경고 없음
```

## 7. 자주 하는 실수

**실수 1 — 자식 bf = 0 을 이중 회전으로 보냄.** `if (bf(n->left) <= 0)` 처럼 0을 LR 쪽에 넣으면 삽입 테스트는 다 통과한다(삽입에선 0이 안 나오니까). 삭제에서만 터진다. 실제로 조건만 바꿔 돌린 출력 (발췌, 경로 생략):

```text
FAIL m1.c:309  avl_delete(&t, 70) && avl_check(&t)
  [B] delete 70 (child bf 0) -> rotations=2
  [B] random ops=60000 max_size=177 max_height=9 (worst AVL for 177 = 10) max_rot/insert=4 max_rot/delete=6 bad_ops=9587
```

고친 코드: LR/RL은 **엄격히 반대로 기울었을 때만**.

```c
if (b > 1) { if (bf(n->left) < 0) n->left = rot_left(n->left); return rot_right(n); }
```

**실수 2 — 회전 후 높이 갱신 순서.** `rot_right` 에서 `fix_height(x)` 를 `fix_height(y)` 보다 먼저 하면 x가 y의 옛 높이로 계산된다. 실제 출력 (발췌, 경로 생략 — LL·LR 만 깨지고 RR·RL 은 통과: `rot_right` 만 순서를 바꿨기 때문):

```text
  [A] LL  insert 30,20,10 -> rotations=1 height=4
FAIL m2.c:275  ok && n == 3 && same(out, P3, 3) && g_rot == R4[c].rots && ht(t.root) == 2
  [B] random ops=60000 max_size=177 max_height=18 (worst AVL for 177 = 10) max_rot/insert=15 max_rot/delete=13 bad_ops=57168
```

노드 3개짜리 트리의 루트 높이가 4로 저장됐고, 틀린 높이로 bf를 계산하니 회전이 폭주해 높이 18까지 간다. 고침: 내려간 노드 먼저 — `fix_height(y); fix_height(x);`.

**실수 3 — 삽입 지점에서만 고치고 경로를 안 거슬러 오름.** `return *added ? rebalance(n) : n;` 대신 `return n;` 이면 조상들의 높이가 갱신되지 않아 bf가 영원히 0에 가깝게 보이고 회전이 일어나지 않는다. 실제 출력: `[A] LL  insert 30,20,10 -> rotations=0 height=1` — 세 노드 사슬인데 루트 높이가 1. 높이는 **경로 위 모든 노드**에서 바뀐다.

**실수 4 — 후계자 키를 복사.** 동작은 하지만(검사기 통과) 바깥 포인터가 깨진다. 테스트 `p30 && avl_find(&t, 30) == p30` 이 이것을 잡는다. 펌웨어에서 트리 노드는 보통 다른 객체(타이머, I/O 요청)의 일부이거나 핸들로 노출되므로 **노드 정체성(identity)을 유지하는 재연결**이 기본이다.

**실수 5 — `detach_min` 에서 `rebalance` 를 빠뜨림.** 후계자를 떼면 오른쪽 서브트리 안의 경로 높이도 줄어든다. 지울 노드 자리(`rebalance(s)`)만 고치면 그 아래 노드들의 높이 필드가 낡은 채 남는다 — 검사기 ②(저장된 높이)가 다음 연산에서 바로 잡는다.

**실수 6 — 높이 규약 섞기.** "잎 = 0" 규약(간선 수)과 "잎 = 1" 규약(노드 수)을 섞으면 `ht(NULL)` 이 −1이어야 하는지 0이어야 하는지 헷갈려 bf가 한쪽에서만 1씩 어긋난다. 한 규약을 정해 `ht()` 하나로만 읽는다. 이 레슨과 상한 공식 h < 1.4404·log₂(n+2) − 0.328 은 모두 노드 수 규약이다(간선 규약이면 1을 빼야 한다).

## 8. 면접 Q&A

<details>
<summary>Q1. AVL 트리의 높이가 O(log n)임을 증명해 보세요.</summary>

높이 h인 AVL의 최소 노드 수 N(h)는 루트 + 높이 h−1, h−2인 최소 서브트리: N(h) = 1 + N(h−1) + N(h−2). +1을 하면 Fibonacci 점화식이 되어 N(h) = F(h+2) − 1 ≥ φ^h − 1 (φ ≈ 1.618). 노드가 n개면 n ≥ N(h) 이므로 h ≤ log_φ(n+1) ≈ 1.44·log₂(n+1). Binet 공식으로 다듬으면 h < 1.4404·log₂(n+2) − 0.328 (Knuth의 형태). 꼬리질문 "최악 트리는 어떻게 생겼나?" → 모든 내부 노드가 같은 방향으로 bf = ±1인 Fibonacci tree (연습 1). 꼬리질문 "실제로 그 상한에 닿나?" → 측정에서 n = 15, 127, 1023, 4095의 정확한 최악 높이 5, 9, 14, 16이 공식 값 5.56, 9.77, 14.08, 16.96의 정수 부분과 같았다.

</details>

<details>
<summary>Q2. AVL과 레드블랙 트리 중 무엇을 쓰겠습니까? Linux는 왜 rbtree인가요?</summary>

| | AVL | 레드블랙 (Linux `lib/rbtree.c`) |
|---|---|---|
| 최악 높이 (노드 수 기준) | < 1.44·log₂(n+2) | ≤ 2·log₂(n+1) |
| 삽입 회전 | ≤ 2 (재구성 1번) | ≤ 2 |
| 삭제 회전 | O(log n) (실측: h = 16에서 7) | ≤ 3 |
| 노드 추가 정보 | 높이 1바이트 또는 bf 2비트 | 색 1비트 |
| 갱신 비용이 큰 경로 | 삭제 시 루트까지 높이 갱신·회전 연쇄 | 재색칠은 위로 전파될 수 있지만 회전은 상수 |

조회가 압도적으로 많으면(정적에 가까운 라우팅 테이블, 읽기 위주 인덱스) AVL이 트리가 더 낮아 유리하고, 삽입·삭제가 잦고 **연산당 구조 변경량의 상한**이 중요하면 레드블랙이 유리하다. Linux 문서(`Documentation/core-api/rbtree.rst`)가 바로 이 근거를 든다 — AVL과 비슷하지만 삽입/삭제의 회전이 최대 2/3번으로 제한되고 조회는 약간 느리다(그래도 O(log n)). 커널은 hrtimer의 `timerqueue`, 스케줄러의 런큐(`kernel/sched/fair.c`), epoll 등에서 rbtree를 쓰고, 색 비트는 부모 포인터의 하위 비트에 넣어(`__rb_parent_color`) 추가 메모리가 0이다. 반대로 OpenZFS(`module/avl/avl.c`)는 AVL을 쓴다. 실무 답: "둘 다 O(log n) 보증 — 갱신 비율과 회전 상한 요구로 고르고, 실제 워크로드로 측정한다"(Pfaff 2004가 시스템 소프트웨어 워크로드에서 이 비교를 했다).

</details>

<details>
<summary>Q3. 삽입은 회전이 한 번이면 끝나는데 삭제는 왜 여러 번 필요한가요?</summary>

삽입은 어떤 서브트리의 높이를 **1 올린다**. 가장 낮은 불균형 노드에서 회전하면 그 서브트리 높이가 삽입 **전** 값으로 돌아가므로 조상은 변화를 모른다. 삭제는 높이를 **1 내린다**. 회전이 높이를 되돌려 주는 건 무거운 쪽 자식의 bf가 0일 때뿐이고(그때는 회전 후 높이가 삭제 전과 같아 멈춘다), 그 외에는 회전 후에도 서브트리가 1 낮아진 상태라 부모에서 또 불균형이 생길 수 있다. 최악은 경로의 거의 모든 단계(대략 h/2번)에서 회전 — 연습 2의 Fibonacci tree에서 h = 4, 8, 12, 16일 때 1, 3, 5, 7번(= h/2 − 1)을 실측했다.

</details>

<details>
<summary>Q4. 이 트리를 ISR과 태스크가 같이 쓴다면?</summary>

회전은 여러 링크를 순서대로 바꾸는 다단계 연산이라, 중간에 ISR이 조회하면 노드가 일시적으로 두 부모에 걸쳐 있거나 어느 쪽에서도 안 보이는 상태를 볼 수 있다. 선택지: ① ISR은 트리를 만지지 않고 요청을 SPSC 링(DS-A01)에 넣고 태스크가 트리를 단독 소유 — 가장 깔끔. ② 임계 구역(인터럽트 off)으로 감싸기 — AVL은 연산 길이가 O(log n)으로 **상한이 있어서** 인터럽트 지연의 최악값을 계산할 수 있다(n = 4096이면 경로 ≤ 16단계). 일반 BST라면 n단계라 불가능. ③ 읽기가 대부분이면 Linux의 `include/linux/rbtree_latch.h` 처럼 트리 사본 두 개 + seqcount latch로 읽기 측을 lock-free로 만드는 방법도 있다(모듈 주소 조회 `mod_tree` 에서 사용).

</details>

<details>
<summary>Q5. 균형 트리 대신 정렬 배열이나 해시 테이블을 쓰면 안 되나요?</summary>

정렬 배열 + 이진 탐색: 조회 O(log n), 캐시 최고, 오버헤드 0 — 하지만 삽입/삭제가 O(n) 메모리 이동. 키 집합이 부팅 때 정해지는 테이블(레지스터 맵, 명령어 표)이면 정답. 해시 테이블(DS-B10/B11): 정확 일치 조회 평균 O(1)이지만 **순서가 없어** floor·범위 질의·"다음으로 큰 키"를 못 한다 — 익스텐트 맵, 타이머(가장 이른 것), 주소 범위 조회는 순서가 필요하다. 최악이 O(n)인 것도 실시간에서 문제. 균형 트리는 "순서 + 동적 갱신 + 최악 상한"을 동시에 주는 구조다. 노드가 수천 개 이상이고 블록 장치 위라면 B+ 트리(DS-A08)가 낫다.

</details>

<details>
<summary>Q6. 노드에 높이 대신 균형 인자만 저장할 수 있나요? 메모리를 더 줄이려면?</summary>

가능하다. bf ∈ {−1, 0, +1} 은 2비트라 노드 포인터가 4바이트 정렬이면 하위 2비트에 넣을 수 있다(Linux rbtree가 색 1비트를 부모 포인터에 넣는 것과 같은 기법). 대신 회전 시 bf를 높이로부터 재계산할 수 없어서, 회전 경우마다 새 bf를 표로 갱신해야 한다(예: LR 후 y, z의 bf는 x의 옛 bf에 따라 달라짐) — 코드가 훨씬 까다롭다. 높이 1바이트 방식은 단순하고 검사하기 쉽다. 더 줄이려면 포인터를 `uint16_t` 풀 인덱스로 바꾼다(DS-B07): 노드 = 키 4 + 값 4 + 인덱스 2×2 + 높이 1 → 13바이트(정렬 시 16), Cortex-M 포인터 버전 20바이트 대비 20% 절약, 노드 수는 65535개 이하로 제한.

</details>

<details>
<summary>Q7. 재귀가 금지된 코드베이스(MISRA)에서는 어떻게 구현하나요?</summary>

두 가지: ① 부모 포인터를 두고 위로 올라가며 고친다(Linux rbtree 방식, 노드당 포인터 1개 추가). ② 내려가면서 지나간 **링크의 주소**(`Node **`)를 고정 크기 스택에 쌓고, 삽입 후 역순으로 `*link = rebalance(*link)` (연습 3). 스택 크기는 높이 상한으로 정한다 — int32 키라 노드 ≤ 2^32 이면 높이 < 1.4404·log₂(2^32 + 2) − 0.328 ≈ 45.8, 즉 최대 45. 그래서 48칸이면 어떤 경우에도 넘치지 않는다. Zephyr의 `include/zephyr/sys/rb.h` 레드블랙 트리도 노드에 부모 포인터를 두지 않고 연산 중 경로 스택을 쓰는 설계다.

</details>

## 9. 추가 연습

### 연습 1 — 최소 AVL 트리(Fibonacci tree)를 직접 만들기

높이 h인 AVL 중 노드가 가장 적은 트리를 만들고, 노드 수가 F(h+2) − 1인지, 검사기를 통과하는지 h = 1..16에서 확인하라.

<details>
<summary>답 보기</summary>

`build_fib(h)` = 왼쪽 `build_fib(h−1)`, 오른쪽 `build_fib(h−2)`, 키는 inorder 순서로 1, 2, 3 … (먼저 왼쪽을 만들고 나서 자기 키를 받는다). 모든 내부 노드의 bf가 +1인 "가장 아슬아슬한" AVL이다. 실제 출력:

```text
[1]  h | N(h) min nodes | F(h+2)-1 | check
[1]  1 |              1 |        1 | ok
[1]  2 |              2 |        2 | ok
[1]  3 |              4 |        4 | ok
[1]  4 |              7 |        7 | ok
[1]  8 |             54 |       54 | ok
[1] 12 |            376 |      376 | ok
[1] 16 |           2583 |     2583 | ok
```

노드 2583개로 높이 16 — 같은 노드 수의 완전 이진 트리 높이는 12다. 이것이 상한 증명의 1~2단계를 눈으로 본 것이다.

</details>

### 연습 2 — 삭제 한 번의 회전 연쇄 측정

Fibonacci tree에서 키를 하나씩(매번 새로 만든 트리에서) 지워 보고, 삭제 한 번에 일어나는 단일 회전의 최대 수와 그 키를 h = 4, 8, 12, 16에서 출력하라.

<details>
<summary>답 보기</summary>

실제 출력:

```text
[2]  h | nodes | max rotations in one delete (key) | after: height
[2]  4 |     7 |        1 single rotations (key 3) | 3
[2]  8 |    54 |        3 single rotations (key 32) | 7
[2] 12 |   376 |        5 single rotations (key 231) | 11
[2] 16 |  2583 |        7 single rotations (key 1595) | 15
```

최악은 h/2 − 1번: 짧은 쪽(오른쪽) 서브트리 깊숙한 곳의 잎을 지우면 그 서브트리 높이가 줄고, 부모에서 회전 → 그 서브트리도 1 낮아짐 → 할아버지에서 또 회전 … 이 두 단계마다 반복된다. 삭제 후 전체 높이가 1 줄어든 것도 그 증거다. 삽입이었다면 최대 2(이중 회전 1번). 이것이 Q2의 "레드블랙은 삭제 회전 ≤ 3"과 대비되는 지점이다.

</details>

### 연습 3 — 재귀 없는 삽입 (링크 경로 스택)

`Node **path[48]` 에 내려가며 지나간 링크 주소를 쌓고, 새 노드를 단 뒤 역순으로 `*path[i] = rebalance(*path[i])` 하는 `avl_insert_iter` 를 작성하라. 랜덤 키 1000개를 재귀 버전과 반복 버전에 각각 넣어 preorder가 같은지 30회 확인하라.

<details>
<summary>답 보기</summary>

링크 주소(`&parent->left` 또는 `&t->root`)를 저장하는 게 요령이다 — `rebalance` 가 새 서브트리 루트를 돌려주면 그 링크에 바로 쓴다. 노드 포인터만 저장하면 "이 노드가 부모의 왼쪽이었나 오른쪽이었나"를 다시 판정해야 한다. 같은 회전 규칙을 같은 순서(아래→위)로 적용하므로 모양이 완전히 같아야 한다. 실제 출력:

```text
[3] iterative insert: 30 random trials same preorder as recursive=yes, 0..4094 sorted -> height 12
```

개선점: 어떤 노드에서 `rebalance` 전후 높이가 같고 회전도 없었다면 그 위는 바뀔 게 없으니 루프를 끝낼 수 있다(삽입 한정). 스택 48칸 × 8바이트 = 384바이트(Cortex-M이면 192바이트)가 최악 스택 사용량으로 **고정**된다.

</details>

### 연습 4 — 순위 통계: select(k) / rank(key)

노드에 서브트리 크기 `size` 를 추가하고 `avl_select(t, k)` (k번째로 작은 키, 0-based)와 `avl_rank(t, key)` (key보다 작은 키 개수)를 O(log n)으로 구현하라. 랜덤 삽입·삭제 20000번 중 50번마다 정렬 참조와 전수 대조하라.

<details>
<summary>답 보기</summary>

`size` 는 높이처럼 자식에서 계산되는 값이라 `fix_height` 에 `n->size = 1 + sz(left) + sz(right)` 한 줄만 넣으면 회전(높이 갱신을 이미 아래→위 순서로 하므로)과 삽입·삭제 경로 갱신이 모두 공짜로 따라온다. 이런 "증강(augmented) 트리"는 Linux의 `rbtree_augmented.h` 와 같은 아이디어다(구간 트리 등). select: 왼쪽 크기 `ls` 와 k를 비교해 왼쪽으로 / 여기서 끝 / `k −= ls + 1` 하고 오른쪽. rank: 오른쪽으로 갈 때마다 `ls + 1` 을 누적. 검사기에도 "저장된 size == 실제 노드 수" 검사를 추가한다. 실제 출력:

```text
[4] select/rank vs sorted reference: size=975 bad=0
```

펌웨어 응용: "현재 대기 중인 요청 중 중앙값 지연", "백분위(p99) 레이턴시 추적"을 O(log n)에 답한다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 4/4 passed
```

## 10. 핵심 정리

- AVL 불변식: 모든 노드 |h(L) − h(R)| ≤ 1. 노드마다 높이(노드 수 기준, 잎 1)를 저장하고 경로를 거슬러 오르며 고친다.
- 회전 = 링크 2개 + 부모 링크(반환값) + 높이 2개(**내려간 노드 먼저**). inorder 보존, 높이만 옮긴다.
- `bf = +2`: 자식 bf ≥ 0 → LL(단일), < 0 → LR(이중). 거울상 동일. **bf 0은 삭제에서만, 단일 회전으로.**
- 삽입 회전 ≤ 2(재구성 1번 후 높이 원상복구), 삭제는 O(log n) 연쇄(Fibonacci tree 실측 h/2 − 1).
- 높이 상한: N(h) = F(h+2) − 1 → h < 1.4404·log₂(n+2) − 0.328. 실측 4095개 최악 16, 정렬 삽입 12, 일반 BST 4095.
- 후계자는 복사 말고 재연결 — 노드 정체성 유지(침입형 트리의 필수 조건).
- 검증: 순서·저장된 높이·|bf| 를 한 번에 보는 O(n) 검사기를 매 연산 후 + 참조 모델 대조.
- AVL vs RB: AVL은 더 낮은 트리(조회 유리), RB는 갱신당 회전 상수(삽입 2, 삭제 3) — Linux는 rbtree.

## 참고자료

- G. M. Adelson-Velsky, E. M. Landis, "An algorithm for the organization of information", *Soviet Mathematics Doklady* 3, 1962 — 원 논문
- Donald E. Knuth, *The Art of Computer Programming* Vol. 3, §6.2.3 Balanced Trees — 높이 상한 1.4405·lg(N+2) − 0.3277, 삭제 분석
- CLRS *Introduction to Algorithms*, Ch. 13 Red-Black Trees (Lemma 13.1 높이 ≤ 2 lg(n+1)), Problem 13-3 AVL trees
- [Linux `Documentation/core-api/rbtree.rst` — AVL과의 비교, 사용법](https://www.kernel.org/doc/html/latest/core-api/rbtree.html)
- [Linux `lib/rbtree.c`, `include/linux/rbtree_augmented.h`, `include/linux/rbtree_latch.h`](https://github.com/torvalds/linux/tree/master/lib)
- [OpenZFS `module/avl/avl.c` — 실전 AVL 구현](https://github.com/openzfs/zfs/blob/master/module/avl/avl.c)
- [Zephyr `include/zephyr/sys/rb.h` — 부모 포인터 없는 레드블랙 트리](https://github.com/zephyrproject-rtos/zephyr/blob/main/include/zephyr/sys/rb.h)
- Ben Pfaff, "Performance Analysis of BSTs in System Software", *ACM SIGMETRICS* 2004 — AVL/RB/splay 실측 비교
