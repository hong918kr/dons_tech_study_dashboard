---
id: DS-B06
time: 50분
updated: 2026-11-07
---
# DS-B06 이중 연결 리스트와 Sentinel 노드

## 학습 목표

- **원형 이중 연결 리스트 + sentinel(더미 head)** 로 빈 리스트·맨 앞·맨 뒤 경계 조건을 *코드에서 없앤다* — `if` 없는 O(1) 삽입·삭제.
- 모든 삽입을 `insert_between(n, prev, next)` 하나로 표현하고, 포인터 4개를 고치는 순서를 설명한다.
- 두 리스트를 **O(1) splice** 로 이어 붙인다.
- `prev/next` 일관성 **불변식 검사 함수**를 쓰고, 고정 시드 랜덤 테스트 + 모델 비교로 수만 번의 연산을 검증한다.
- Linux `list_head`, FreeRTOS `List_t` 가 왜 이 구조인지 연결해서 말할 수 있다.

## 1. 문제

RTOS의 ready/delayed 리스트, Linux 커널의 거의 모든 객체 목록, LRU 캐시가 **이중 연결 리스트**다. 면접에서는 "O(1)로 임의 노드를 지울 수 있게 하라" 가 단골이고, 정답의 핵심은 **sentinel 노드** — 리스트가 비어 있어도 항상 존재하는 가짜 노드다.

sentinel 이 있으면 리스트는 언제나 **고리**다. 빈 리스트는 sentinel 혼자 자기 자신을 가리키는 고리, 노드가 있으면 `sentinel → 첫 노드 → … → 마지막 노드 → sentinel`. 그러면 "앞 노드가 없으면?", "뒤 노드가 없으면?", "head가 바뀌면?" 같은 질문이 전부 사라진다 — 어떤 노드든 앞뒤 이웃이 **반드시** 있기 때문이다.

```svg
<svg viewBox="0 0 720 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">sentinel 이 있는 원형 이중 연결 리스트</text>
  <text x="20" y="55" fill="currentColor">빈 리스트</text>
  <rect x="40" y="75" width="70" height="40" class="f-mut" stroke="currentColor"/>
  <text x="75" y="100" fill="currentColor" text-anchor="middle">S</text>
  <g stroke="currentColor" fill="none">
    <path d="M110 85 C150 60, 150 130, 110 105"/><path d="M110 105 l9 0 M110 105 l5 -7"/>
  </g>
  <text x="20" y="150" fill="currentColor">next = prev = &amp;S</text>
  <text x="230" y="55" fill="currentColor">노드 3개</text>
  <rect x="240" y="75" width="70" height="40" class="f-mut" stroke="currentColor"/>
  <g stroke="currentColor" fill="none">
    <rect x="360" y="75" width="70" height="40"/><rect x="480" y="75" width="70" height="40"/>
    <rect x="600" y="75" width="70" height="40"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="275" y="100">S</text><text x="395" y="100" font-family="monospace">A</text>
    <text x="515" y="100" font-family="monospace">B</text><text x="635" y="100" font-family="monospace">C</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M310 87 L355 87"/><path d="M355 87 l-8 -4 M355 87 l-8 4"/>
    <path d="M360 103 L315 103"/><path d="M315 103 l8 -4 M315 103 l8 4"/>
    <path d="M430 87 L475 87"/><path d="M475 87 l-8 -4 M475 87 l-8 4"/>
    <path d="M480 103 L435 103"/><path d="M435 103 l8 -4 M435 103 l8 4"/>
    <path d="M550 87 L595 87"/><path d="M595 87 l-8 -4 M595 87 l-8 4"/>
    <path d="M600 103 L555 103"/><path d="M555 103 l8 -4 M555 103 l8 4"/>
    <path d="M635 115 L635 160 L275 160 L275 120"/><path d="M275 120 l-4 8 M275 120 l4 8"/>
    <path d="M275 75 L275 50 L635 50 L635 70"/><path d="M635 70 l-4 -8 M635 70 l4 -8"/>
  </g>
  <g fill="currentColor">
    <text x="420" y="45">S.prev = C (마지막)</text>
    <text x="420" y="178">C.next = S</text>
    <text x="20" y="205">S.next 가 첫 노드, S.prev 가 마지막 노드. 모든 노드에 앞·뒤가 반드시 있다 → NULL 검사가 0개.</text>
    <text x="20" y="230">add_head = S 와 S.next 사이, add_tail = S.prev 와 S 사이, remove = 앞뒤 이웃끼리 잇기.</text>
  </g>
</svg>
```

```c
typedef struct DNode { struct DNode *prev, *next; int val; } DNode;
typedef struct { DNode head; size_t len; } DList;   /* head 가 sentinel (값 없음) */
```

### Part A — 초기화와 삽입

- `dl_init(l)` — 빈 리스트 = sentinel 자기 고리, `len = 0`.
- `dl_empty(l)` — `head.next == &head`.
- `insert_between(n, prev, next)` — 이웃한 두 노드 사이에 n을 끼운다 (모든 삽입의 핵심).
- `dl_add_head(l, n)`, `dl_add_tail(l, n)` — 각각 한 줄 + `len++`.

### Part B — 삭제와 splice

- `dl_remove(l, n)` — O(1). 맨 앞·가운데·맨 뒤·마지막 하나 모두 **같은 두 줄**. 떼어낸 노드의 `prev/next` 는 `NULL` 로.
- `dl_splice_tail(dst, src)` — src의 노드 전부를 dst 꼬리에 **O(1)** 로 옮기고 src는 빈 리스트로. 빈 src, 빈 dst 도 처리.

### Part C — 불변식 검사와 랜덤 테스트

- `dl_check(l)` — 모든 링크 `p → p->next` 에 대해 `p->next->prev == p`, NULL 없음, sentinel 로 돌아올 때까지 센 노드 수 == `len`. **망가진 리스트(중간에서 도는 고리)에서도 반드시 멈춰야** 한다.
- 하네스의 `random_test(20000)` 은 노드 32개 풀과 리스트 2개로 add_head/add_tail/remove/splice 를 xorshift32(시드 `0xC0FFEE`)로 섞어 실행하고, 매 연산 뒤 `dl_check` + 배열 모델 비교를 한다. 당신의 `dl_check` 가 이 테스트의 심판이다.

## 2. Problem (English)

> **Circular doubly linked list with a sentinel node.**
>
> Using `struct DNode { DNode *prev, *next; int val; }` and `struct DList { DNode head; size_t len; }` where `head` is a sentinel, implement in C11 with no dynamic allocation:
>
> 1. `dl_init`, `dl_empty`, `dl_add_head`, `dl_add_tail` — all O(1), with no `NULL` checks and no special cases for an empty list.
> 2. `void dl_remove(DList *l, DNode *n)` in O(1) given only the node; set the removed node's links to `NULL`. `void dl_splice_tail(DList *dst, DList *src)` that moves every node of `src` to the tail of `dst` in O(1) and leaves `src` empty.
> 3. `bool dl_check(const DList *l)` that verifies `p->next->prev == p` for every link, no `NULL` links, and that the forward walk returns to the sentinel after exactly `len` nodes. It must terminate even if the list is corrupted into a loop that bypasses the sentinel.
>
> Then drive 20,000 seeded random operations across two lists, checking the invariant and a shadow array model after every operation.
>
> Follow-ups: how does Linux's `CONFIG_DEBUG_LIST` use the same invariant? Why does `list_del` write poison values instead of `NULL`?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c && ./a.out
# 처음엔 "5/20 passed" — random_test 는 step 0 에서 바로 멈춘다. TODO 를 채워 20/20 을 만든다
```

## 4. 힌트

<details>
<summary>힌트 1 — 빈 리스트가 "고리"다</summary>

`l->head.next = l->head.prev = &l->head;`. 이 한 줄 덕분에 빈 리스트에 `add_head` 할 때도 "prev = sentinel, next = sentinel" 인 평범한 끼우기가 된다.

</details>

<details>
<summary>힌트 2 — insert_between 은 포인터 4개</summary>

새 노드의 두 팔(`n->prev`, `n->next`)을 먼저 세팅하고, 이웃 둘(`prev->next`, `next->prev`)이 n을 가리키게 한다. add_head = `insert_between(n, &l->head, l->head.next)`, add_tail = `insert_between(n, l->head.prev, &l->head)`.

</details>

<details>
<summary>힌트 3 — remove 는 리스트를 몰라도 된다</summary>

`n->prev->next = n->next; n->next->prev = n->prev;` — 노드 자신이 이웃을 알고 있다. `l` 은 `len--` 에만 쓴다. sentinel 덕분에 `n->prev` 와 `n->next` 는 절대 NULL 이 아니다.

</details>

<details>
<summary>힌트 4 — splice 는 "고리 두 개를 끊고 하나로 잇기"</summary>

src의 첫 노드 `first = src->head.next`, 마지막 `last = src->head.prev`, dst의 마지막 `at = dst->head.prev`. 연결은 `at ⇄ first`, `last ⇄ dst sentinel` 두 쌍(포인터 4개). 그리고 src 를 `dl_init`. **src가 비어 있으면** first/last 가 src의 sentinel 자신이므로 반드시 먼저 걸러야 한다.

</details>

<details>
<summary>힌트 5 — 검사기가 멈추는 법</summary>

sentinel 에서 출발해 `next` 로 걸으며 링크마다 `nx->prev == p` 를 확인한다. 센 노드가 `len` 을 넘으면 그 순간 false — 고리가 sentinel 을 비켜 가도 len+1 칸 안에 끝난다.

</details>

## 5. 풀이

### Part A — insert_between 하나로 모든 삽입

```svg
<svg viewBox="0 0 720 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">insert_between(n, P, Q) — 고치는 포인터 4개</text>
  <g stroke="currentColor" fill="none">
    <rect x="60" y="60" width="90" height="44"/><rect x="530" y="60" width="90" height="44"/>
  </g>
  <rect x="295" y="140" width="90" height="44" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="105" y="87">P (prev)</text><text x="575" y="87">Q (next)</text><text x="340" y="167">n</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-dasharray="4 3">
    <path d="M150 72 L525 72"/><path d="M530 94 L155 94"/>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M295 152 L155 100"/><path d="M155 100 l9 0 M155 100 l6 7"/>
    <path d="M385 152 L525 100"/><path d="M525 100 l-9 0 M525 100 l-6 7"/>
    <path d="M150 104 L290 165"/><path d="M290 165 l-9 0 M290 165 l-5 -7"/>
    <path d="M530 104 L390 165"/><path d="M390 165 l9 0 M390 165 l5 -7"/>
  </g>
  <g fill="currentColor">
    <text x="190" y="140">① n-&gt;prev = P</text><text x="430" y="140">② n-&gt;next = Q</text>
    <text x="160" y="200">③ P-&gt;next = n</text><text x="440" y="200">④ Q-&gt;prev = n</text>
    <text x="250" y="55">점선 = 끊어질 옛 링크 P ⇄ Q</text>
    <text x="10" y="222">P, Q 를 인자로 먼저 받아 두므로 4줄 순서가 안전하다. 인라인으로 쓰면 S.next 를 읽기 전에 덮어쓰지 말 것.</text>
  </g>
</svg>
```

| 연산 | 호출 | 빈 리스트일 때 P, Q |
|---|---|---|
| add_head | `insert_between(n, &S, S.next)` | P = Q = S |
| add_tail | `insert_between(n, S.prev, &S)` | P = Q = S |
| (연습 1) move_to_head | 떼고 → `insert_between(n, &S, S.next)` | — |

빈 리스트에서 P와 Q가 *같은* sentinel 이어도 ③ `S.next = n`, ④ `S.prev = n` 으로 정확히 "노드 1개짜리 고리"가 된다. 이것이 sentinel 의 힘이다 — 특수 경우가 일반 경우의 한 예가 된다.

### Part B — remove 와 splice

**remove** 는 두 줄이다. 맨 앞 노드면 `n->prev` 가 sentinel, 맨 뒤면 `n->next` 가 sentinel 일 뿐 코드는 같다.

| 지우는 노드 | n->prev | n->next | 실행 후 |
|---|---|---|---|
| 가운데 (2 in 0,1,2,3) | 1 | 3 | 1 ⇄ 3 |
| 맨 앞 (0) | S | 1 | S.next = 1 |
| 맨 뒤 (3) | 1 | S | S.prev = 1 |
| 마지막 하나 (1) | S | S | S ⇄ S (빈 고리) |

떼어낸 노드의 링크를 `NULL` 로 지우는 이유: 같은 노드를 두 번 `remove` 하거나, 지운 노드를 따라 걷는 버그가 **즉시 NULL 역참조로 터지게** 하려는 것이다. Linux `list_del` 은 NULL 대신 `LIST_POISON1/2` (`0x100`, `0x122` + 오프셋)라는 *매핑 안 된 특이한 주소*를 넣어서, oops 메시지만 보고도 "삭제된 노드를 썼구나" 를 알게 한다.

**splice** 는 고리 두 개를 끊어 하나로 잇는다.

```svg
<svg viewBox="0 0 720 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">dl_splice_tail(dst, src): 포인터 4개 + src 초기화</text>
  <g fill="currentColor"><text x="20" y="55">전</text><text x="20" y="165">후</text></g>
  <g stroke="currentColor" fill="none">
    <rect x="60" y="38" width="50" height="28"/><rect x="130" y="38" width="50" height="28"/><rect x="200" y="38" width="50" height="28"/>
    <rect x="360" y="38" width="50" height="28"/><rect x="430" y="38" width="50" height="28"/><rect x="500" y="38" width="50" height="28"/><rect x="570" y="38" width="50" height="28"/>
  </g>
  <rect x="60" y="38" width="50" height="28" class="f-mut"/><rect x="360" y="38" width="50" height="28" class="f-mut"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="85" y="57">Sd</text><text x="155" y="57">4</text><text x="225" y="57">5</text>
    <text x="385" y="57">Ss</text><text x="455" y="57">6</text><text x="525" y="57">7</text><text x="595" y="57">8</text>
    <text x="225" y="88">at</text><text x="455" y="88">first</text><text x="595" y="88">last</text>
  </g>
  <g stroke="currentColor" fill="none">
    <rect x="60" y="148" width="50" height="28"/><rect x="130" y="148" width="50" height="28"/><rect x="200" y="148" width="50" height="28"/>
    <rect x="270" y="148" width="50" height="28"/><rect x="340" y="148" width="50" height="28"/><rect x="410" y="148" width="50" height="28"/>
    <rect x="560" y="148" width="50" height="28"/>
  </g>
  <rect x="60" y="148" width="50" height="28" class="f-mut"/><rect x="560" y="148" width="50" height="28" class="f-mut"/>
  <rect x="200" y="148" width="120" height="28" class="f-ok" opacity=".5"/>
  <rect x="410" y="148" width="50" height="28" class="f-ok" opacity=".5"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="85" y="167">Sd</text><text x="155" y="167">4</text><text x="225" y="167">5</text><text x="295" y="167">6</text>
    <text x="365" y="167">7</text><text x="435" y="167">8</text><text x="585" y="167">Ss</text>
  </g>
  <g fill="currentColor">
    <text x="470" y="167">→ Sd 로 복귀</text>
    <text x="620" y="167">자기 고리</text>
    <text x="20" y="210">at ⇄ first : at-&gt;next = first; first-&gt;prev = at;     last ⇄ Sd : last-&gt;next = &amp;Sd; Sd.prev = last;</text>
    <text x="20" y="235">초록 칸 = 새로 이어진 두 이음매. 노드 수와 무관하게 포인터 4개 → O(1). len 은 더하기 한 번.</text>
  </g>
</svg>
```

빈 src 를 거르지 않으면 `first = last = &Ss` 가 되어 **src 의 sentinel 이 dst 에 끼어 들어간다** — 그 뒤 `dl_init(src)` 가 Ss 를 자기 고리로 돌려놓으면서 dst 의 고리가 끊어진다. 테스트 `dl_splice_tail(&a, &e)` (빈 src) 가 이것을 잡는다.

### Part C — 불변식 검사기와 랜덤 테스트

불변식(invariant)은 "어떤 연산 뒤에도 항상 참이어야 하는 성질"이다. 이중 연결 리스트의 불변식:

1. 모든 노드 p 에 대해 `p->next->prev == p` (그리고 `p->prev->next == p`)
2. 링크에 NULL 없음
3. sentinel 에서 next 로 걸으면 정확히 `len` 개를 지나 sentinel 로 돌아옴

검사기는 sentinel 을 포함한 고리의 *모든 링크* 를 한 번씩 확인하므로 1번의 `p->next->prev == p` 만 보면 충분하다 (forward 고리의 모든 간선에서 역방향이 맞으면 prev 체인도 같은 고리다). 3번의 "len 넘으면 즉시 false" 가 종료를 보장한다.

| 망가뜨린 방법 (테스트) | 걸리는 지점 |
|---|---|
| `n6.prev = &S` (5 ← 6 링크 오류) | p = 5, nx = 6: `6.prev != 5` |
| `len = 5` (실제 4개) | 마지막에 `count(4) != len(5)` |
| `n7.next = &n5` (S 를 비켜 가는 고리) | p = 7, nx = 5: `5.prev(=4) != 7` → 무한 루프 전에 false |
| `n5.next = NULL` | `!nx` |

**랜덤 테스트 설계**: 노드 32개 풀, 리스트 A·B 두 개, 각 노드가 어디 있는지(`where[]`)와 각 리스트의 순서(`model[][]`, 그냥 int 배열)를 *따로* 들고 있다. 매 연산 뒤 ① `dl_check(A)`, `dl_check(B)` ② 리스트를 앞에서 걸으며 모델 배열과 노드 주소를 하나하나 비교. 모델은 배열이라 틀리기 어려우므로 "느리지만 확실한 정답" 역할을 한다 (shadow model / differential testing). 시드가 고정이라 실패하면 같은 step 에서 다시 재현된다.

실제 실행 결과 (`xorshift32`, 시드 `0xC0FFEE`, 20,000 step, `r % 8` 로 연산 선택):

| 연산 | 실행 횟수 |
|---|---|
| add_head | 2,578 |
| add_tail | 2,561 |
| remove | 5,107 |
| splice | 2,471 |
| 최대 길이 | 32 (풀 전체가 한 리스트에) |

나머지 7,283 step 은 "풀이 꽉 차서 add 불가" 또는 "고른 리스트가 비어서 remove 불가" 로 건너뛰었다. 덕분에 **빈 리스트·꽉 찬 풀·빈 리스트끼리 splice** 같은 경계가 수천 번씩 섞였다. 펌웨어 실무에서는 이 `dl_check` 를 `#ifdef DEBUG` 빌드의 모든 리스트 연산 뒤에 `assert` 로 넣어 두고, 릴리스에선 뺀다 — Linux 의 `CONFIG_DEBUG_LIST` 가 정확히 이것이다 (`__list_add_valid()` 가 삽입 전에 `next->prev == prev` 를 확인).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
# random: add_head 2578, add_tail 2561, remove 5107, splice 2471, max len 32
# 20/20 passed
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined solution.c && ./a.out   # 같은 결과, 리포트 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| insert 에서 `P->next = n` 을 `n->next = P->next` 보다 먼저 | Q 로 가는 유일한 링크를 덮어써 Q 를 잃음 | n 의 두 팔 먼저, 이웃 나중 (`insert_between`) |
| splice 에서 빈 src 검사 누락 | src sentinel 이 dst 에 끼어 들어가고, `dl_init(src)` 가 dst 고리를 끊음 | `if (dl_empty(src)) return;` |
| splice 뒤 `dl_init(src)` 누락 | src sentinel 이 여전히 옮겨간 노드를 가리킴 → 두 리스트가 노드를 공유 | 마지막에 `dl_init(src)` |
| remove 한 노드의 링크를 그대로 둠 | 두 번 remove 하면 이웃을 엉뚱하게 다시 이어 리스트 손상 (조용히) | `n->next = n->prev = NULL;` (Linux 는 poison) |
| 순회하면서 현재 노드 remove 후 `p = p->next` | p 의 링크는 방금 NULL 이 됨 → 크래시 | 다음 노드를 미리 저장 (`list_for_each_safe`, 연습 3) |
| 검사기가 `while (p != &l->head)` 만으로 걷기 | sentinel 을 비켜 가는 고리에서 무한 루프 → 테스트가 멈춤 | `++count > len` 이면 즉시 false |
| `DNode n[8];` 의 `val` 만 채우고 `n[2].next == NULL` 을 검사 | **미초기화 값 읽기** — 이 레슨 작성 중 실제로 clang `-O2` 가 해당 경로를 UB로 보고 `brk #1` 을 심어 starter 가 printf 도 없이 exit 133 으로 죽었다 | `DNode n[8] = {0};` |

## 8. 면접 Q&A

<details>
<summary>Q1. sentinel 을 쓰면 무엇이 좋아지고, 비용은 무엇인가요?</summary>

빈 리스트·맨 앞·맨 뒤 분기가 사라져 삽입/삭제가 무조건 같은 2~4줄 → 분기 예측·코드 크기·버그 모두 줄어든다. 비용은 리스트마다 노드 하나(여기선 prev/next 두 포인터 = 32비트 MCU 8 B) 와, 순회 종료 조건이 `NULL` 이 아니라 `p != &l->head` 라는 점. Linux `list_head` 는 sentinel 이 *값 없는* `{next, prev}` 두 포인터뿐이라 비용이 최소다.

</details>

<details>
<summary>Q2. 노드 포인터만 주어졌을 때 O(1) 삭제가 되는 이유는? 단일 연결 리스트라면?</summary>

이중 리스트는 노드가 앞 이웃을 알고 있어 `prev->next` 를 바로 고칠 수 있다. 단일 리스트는 앞 노드를 찾으려면 head 부터 O(n) 순회해야 한다. (트릭: 다음 노드의 값을 복사하고 다음 노드를 지우는 방법이 있지만 꼬리 노드에선 불가능하고, 외부에서 노드 주소를 들고 있으면 깨진다.) RTOS 가 "타임아웃된 태스크를 대기 리스트에서 빼기" 를 O(1) 로 하려고 이중 리스트를 쓰는 이유.

</details>

<details>
<summary>Q3. Linux list_del 은 왜 NULL 대신 LIST_POISON 을 쓰나요? list_del_init 은 언제 쓰나요?</summary>

NULL 역참조는 원인을 구분하기 어렵지만, `0x100`/`0x122` 근처 주소에서 oops 가 나면 "삭제된 리스트 노드 사용" 이라고 바로 알 수 있다 (그 주소들은 매핑되지 않아 반드시 fault). `list_del_init` 은 지운 노드를 자기 고리로 초기화해서 `list_empty(&node)` 로 "지금 어떤 리스트에도 없음" 을 검사할 수 있게 한다 — 노드를 여러 번 넣었다 뺐다 하는 경우(타이머 재등록)에 쓴다.

</details>

<details>
<summary>Q4. len 필드를 두는 것의 장단점은?</summary>

장점: 크기 질의 O(1), 불변식 검사에 쓸 수 있음. 단점: 모든 연산이 len 을 갱신해야 하고, **remove 가 리스트 포인터를 알아야** 한다(노드만으론 부족). 리스트 일부를 잘라 옮기는 split 은 옮긴 개수를 세느라 O(k) 가 된다 (연습 4). Linux `list_head` 는 len 이 없어서 `list_del(node)` 만으로 지우고 cut/splice 가 모두 O(1). FreeRTOS `List_t` 는 `uxNumberOfItems` 를 두고, 대신 각 `ListItem_t` 가 자기가 속한 리스트를 `pxContainer` 로 기억한다.

</details>

<details>
<summary>Q5. ISR 과 태스크가 같은 이중 리스트를 건드리면 어떻게 보호하나요?</summary>

remove 는 두 포인터를 고치는 사이에 리스트가 일시적으로 불일치한다 (4개 쓰기가 원자적이지 않음). 단일 코어 MCU 에선 짧은 임계 구역(`taskENTER_CRITICAL()` = BASEPRI 올리기, 또는 PRIMASK)으로 감싼다 — FreeRTOS 의 `uxListRemove` 호출부가 그렇다. 임계 구역 안의 작업이 O(1) 이라 인터럽트 지연이 상수로 묶이는 것이 이 구조의 또 다른 장점. 멀티코어는 스핀락, 또는 Linux 의 RCU 리스트(`list_add_rcu`)로 읽기 쪽을 락 없이.

</details>

<details>
<summary>Q6. 불변식 검사기와 랜덤 테스트를 어떻게 설계했나요? 왜 모델이 필요한가요?</summary>

검사기만으로는 "구조는 멀쩡하지만 내용이 틀린" 버그(예: add_head 가 tail 에 넣음)를 못 잡는다. 그래서 단순한 배열 모델을 나란히 돌리고 매 연산 뒤 순서까지 비교한다. 시드 고정으로 실패 step 이 재현되고, 실패 시 첫 step 에서 멈춰 로그가 짧다. 꼬리질문 "더 강하게 하려면?" → 연산 확률을 바꿔 여러 시드로 돌리기, libFuzzer 로 바이트열을 연산열로 해석하는 퍼징, 실패 케이스 축소(shrinking).

</details>

## 9. 추가 연습

### 연습 1 — LRU touch: 노드를 맨 앞으로

`dl_move_to_head(l, n)` — 이미 리스트에 있는 n 을 맨 앞으로. `0,1,2,3` 에서 2 → `2,0,1,3`.

<details>
<summary>답 보기</summary>

떼기(이웃끼리 잇기) + `insert_between(n, &S, S.next)`. len 은 그대로. 이미 맨 앞이면 바로 반환(안 그러면 떼고 나서 `S.next` 가 바뀐 상태로 다시 끼워도 결과는 같지만 쓸데없는 쓰기). LRU 캐시(DS-M05)에서 "hit 이면 맨 앞으로" 가 정확히 이 함수다.

</details>

### 연습 2 — 제자리 뒤집기

`dl_reverse(l)` — 노드를 옮기지 않고 리스트를 뒤집어라. 빈 리스트도 안전해야 한다.

<details>
<summary>답 보기</summary>

sentinel 을 **포함한** 모든 노드에서 `prev` 와 `next` 를 맞바꾼다. 다음으로 갈 때는 바꾸기 전의 next(= 바꾼 뒤의 prev)로 간다. 고리 전체가 반대 방향이 되므로 S.next 가 옛 마지막 노드가 된다. 빈 리스트는 S 하나만 자기 자신과 교환 — 그대로.

</details>

### 연습 3 — 순회 중 삭제 (safe iteration)

`dl_remove_even(l)` — 값이 짝수인 노드를 모두 지우고 개수 반환. `0..7` → `1,3,5,7`, 4개.

<details>
<summary>답 보기</summary>

`for (p = S.next, tmp = p->next; p != &S; p = tmp, tmp = p->next)` — 다음 노드를 **지우기 전에** `tmp` 에 저장. Linux `list_for_each_safe` 와 같은 모양. `dl_remove` 가 p 의 링크를 NULL 로 지우므로 저장 안 하면 바로 크래시한다 (실수 표 5번).

</details>

### 연습 4 — at 뒤를 잘라 다른 리스트로 (split)

`dl_split_after(src, at, dst)` — at 다음부터 끝까지를 빈 dst 로 옮긴다. `1,3,5,7` 을 3 뒤에서 → `1,3` / `5,7`.

<details>
<summary>답 보기</summary>

`first = at->next`, `last = src->head.prev` 를 dst sentinel 에 고리로 걸고, `at ⇄ src sentinel` 을 잇는다 — 포인터 6개, O(1). 하지만 **len 을 유지하려면 옮긴 노드를 세야 해서 O(k)**. len 필드의 숨은 비용(Q4). at 이 마지막 노드면 옮길 것이 없다.

</details>

### 연습 5 — 회전: 노드 대신 sentinel 을 옮긴다

`dl_rotate_left(l, k)` — `0,1,2,3,4` 를 2 → `2,3,4,0,1`. k == len 이면 그대로.

<details>
<summary>답 보기</summary>

원형 리스트에서 "시작" 은 sentinel 위치일 뿐이다. sentinel 을 고리에서 떼어(remove 와 같은 두 줄) k 번째 노드 뒤에 다시 끼우면(insert_between) 회전 끝 — 노드는 하나도 안 움직인다. 탐색 O(k), 재연결 O(1). `k %= len` 으로 큰 k 도 처리.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out   # 9/9 passed
```

## 10. 핵심 정리

- sentinel = 항상 있는 가짜 노드. 리스트는 언제나 고리 → NULL 검사 0개, 빈/앞/뒤 분기 0개.
- 빈 리스트: `S.next = S.prev = &S`. 첫 노드 `S.next`, 마지막 `S.prev`.
- 모든 삽입 = `insert_between(n, P, Q)`: n의 두 팔 먼저, 이웃 둘 나중 (포인터 4개).
- 삭제 = `n->prev->next = n->next; n->next->prev = n->prev;` + 링크 지우기(NULL/poison).
- splice = 이음매 두 개(포인터 4개) + src 재초기화. **빈 src 는 먼저 거른다.**
- 불변식: 모든 링크 `p->next->prev == p`, NULL 없음, 걸음 수 == len. len 초과 시 즉시 false → 망가진 고리에서도 멈춤.
- 랜덤 테스트 = 고정 시드 PRNG + 배열 모델 비교 + 매 연산 뒤 검사. 실무에선 DEBUG 빌드 assert (Linux `CONFIG_DEBUG_LIST`).

## 참고자료

- [Linux kernel include/linux/list.h — list_add, list_del, list_splice, list_for_each_safe](https://github.com/torvalds/linux/blob/master/include/linux/list.h)
- [Linux kernel lib/list_debug.c — CONFIG_DEBUG_LIST 의 __list_add_valid](https://github.com/torvalds/linux/blob/master/lib/list_debug.c)
- [Linux kernel include/linux/poison.h — LIST_POISON1/2](https://github.com/torvalds/linux/blob/master/include/linux/poison.h)
- [FreeRTOS-Kernel list.c / include/list.h — xListEnd 마커와 uxListRemove](https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/list.c)
- CLRS *Introduction to Algorithms* §10.2 — Linked lists, sentinels
