---
id: DS-B05
time: 50분
updated: 2026-11-07
---
# DS-B05 단일 연결 리스트: 삽입·삭제·뒤집기·사이클 검출

## 학습 목표

- **이중 포인터(pointer-to-pointer)** `Node **pp` 로 head 삽입·정렬 삽입·값 삭제를 *head 특별 처리 없이* 한 가지 코드로 쓴다.
- 리스트 뒤집기를 **반복(prev/cur/next 세 포인터)** 과 **재귀** 두 방식으로 쓰고, 재귀의 스택 비용 O(n)이 MCU에서 왜 위험한지 설명한다.
- **Floyd 사이클 검출(토끼와 거북이)** 로 사이클 유무와 *시작 노드*를 O(1) 메모리로 찾고, 그 수학적 이유를 말할 수 있다.
- 같은 slow/fast 기법으로 **중간 노드**를 한 번 순회에 찾는다.
- malloc 없이 **정적 노드 풀**로 리스트를 만들고, ASan(AddressSanitizer)으로 메모리 오류 없이 도는지 확인한다.

## 1. 문제

연결 리스트는 "포인터를 정확히 다룰 줄 아는가"를 5분 만에 보는 면접 단골이다. 펌웨어에서도 타이머 대기열, 드라이버의 요청 큐, RTOS의 ready list가 모두 연결 리스트다 — 그리고 **ISR 안에서 포인터 하나를 잘못 고치면** 리스트가 고리(cycle)로 변해 시스템이 멈춘다.

단일 연결 리스트(singly linked list)의 노드는 값과 "다음 노드 주소" 하나만 가진다. 이 레슨의 핵심 도구는 `Node **pp` — **"고쳐 쓸 링크(포인터 변수)의 주소"** 다. head 변수도, 각 노드의 `next` 칸도 모두 `Node *` 타입 변수이므로, 그 주소를 들고 다니면 "head를 바꾸는 경우"와 "중간 노드의 next를 바꾸는 경우"가 똑같아진다.

```svg
<svg viewBox="0 0 720 240" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">Node **pp — "고쳐 쓸 링크"를 가리킨다</text>
  <g stroke="currentColor" fill="none">
    <rect x="20" y="80" width="70" height="36"/>
    <rect x="170" y="80" width="50" height="36"/><rect x="220" y="80" width="50" height="36"/>
    <rect x="350" y="80" width="50" height="36"/><rect x="400" y="80" width="50" height="36"/>
    <rect x="530" y="80" width="50" height="36"/><rect x="580" y="80" width="50" height="36"/>
  </g>
  <rect x="220" y="80" width="50" height="36" class="f-acc"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="55" y="103">head</text>
    <text x="195" y="103" font-family="monospace">1</text><text x="245" y="103">next</text>
    <text x="375" y="103" font-family="monospace">3</text><text x="425" y="103">next</text>
    <text x="555" y="103" font-family="monospace">7</text><text x="605" y="103" font-family="monospace">NULL</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M90 98 L165 98"/><path d="M165 98 l-8 -5 M165 98 l-8 5"/>
    <path d="M270 98 L345 98"/><path d="M345 98 l-8 -5 M345 98 l-8 5"/>
    <path d="M450 98 L525 98"/><path d="M525 98 l-8 -5 M525 98 l-8 5"/>
    <path d="M245 165 L245 122"/><path d="M245 122 l-5 8 M245 122 l5 8"/>
  </g>
  <text x="200" y="185" fill="currentColor">pp = &amp;n1-&gt;next</text>
  <text x="200" y="205" fill="currentColor">*pp == n3 (지금 가리키는 노드)</text>
  <g fill="currentColor">
    <text x="20" y="60">pp = &amp;head 로 시작하면 head 도 "그냥 링크 하나"</text>
    <text x="440" y="175">값 5를 삽입: n-&gt;next = *pp; *pp = n;</text>
    <text x="440" y="198">값 3을 삭제: *pp = (*pp)-&gt;next;</text>
    <text x="440" y="221">head 이든 중간이든 같은 두 줄</text>
  </g>
</svg>
```

노드는 `static Node pool[64]` 에서 `node_new()` 로 꺼낸다 (malloc 금지 — 펌웨어 표준). 테스트 도우미 `list_eq` 는 최대 65칸만 걸어서, 사이클이 생겨도 무한 루프에 빠지지 않는다.

### Part A — head 삽입·정렬 삽입·값 삭제 (이중 포인터)

- `push_front(Node **head, Node *n)` — 맨 앞에 붙인다. head가 바뀌므로 `Node **`.
- `insert_sorted(Node **head, Node *n)` — 오름차순을 유지하며 끼운다. **같은 값이면 기존 노드 뒤**(안정 삽입, stable).
- `remove_value(Node **head, int v)` — 값이 v인 *첫* 노드를 떼어 반환(`next = NULL` 로 정리), 없으면 `NULL`. head 삭제·tail 삭제·마지막 노드 삭제가 모두 같은 코드여야 한다.

### Part B — 뒤집기: 반복과 재귀

- `reverse_iter(Node *head)` — 새 head를 반환. 추가 메모리 O(1).
- `reverse_rec(Node *head)` — 같은 일을 재귀로. 노드 64개 전체를 뒤집어도 맞아야 한다.

### Part C — Floyd 사이클 검출과 중간 노드

- `has_cycle(const Node *head)` — slow 1칸, fast 2칸. 만나면 사이클.
- `cycle_entry(Node *head)` — 사이클이 *시작되는* 노드(없으면 `NULL`). 자기 자신을 가리키는 1-노드 고리, 전체가 고리인 경우도.
- `middle_node(Node *head)` — 중간 노드. 짝수 길이면 **두 번째 중간** (`1,2,3,4 → 3`).

## 2. Problem (English)

> **Singly linked list fundamentals (no dynamic allocation).**
>
> Nodes are `struct Node { int val; struct Node *next; }` taken from a static pool. Implement in C11:
>
> 1. `void push_front(Node **head, Node *n)`, `void insert_sorted(Node **head, Node *n)` (ascending, stable for equal keys), and `Node *remove_value(Node **head, int v)` that unlinks and returns the first node with value `v` (or `NULL`). Use a pointer-to-pointer so the head is not a special case.
> 2. `Node *reverse_iter(Node *head)` in O(1) extra space and `Node *reverse_rec(Node *head)` recursively. Both return the new head.
> 3. `bool has_cycle(const Node *head)` and `Node *cycle_entry(Node *head)` using Floyd's tortoise-and-hare in O(1) extra space; `Node *middle_node(Node *head)` returning the second middle for even lengths.
>
> Example: `[1,5,9]` + `insert_sorted(5)` → `[1,5,5,9]` with the new 5 after the old one. List `1→2→…→10→4` (10 links back to 4): `has_cycle == true`, `cycle_entry == node 4`. `middle_node([1..6]) == node 4`.
>
> Follow-ups: what is the stack cost of the recursive reverse on a Cortex-M with a 1 KB task stack? Prove why resetting one pointer to the head finds the cycle entry.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c && ./a.out
# 처음엔 FAIL 이 쭉 나오고 "8/27 passed" — TODO 를 채워 27/27 을 만든다

# ASan + UBSan 으로도 돌린다 (포인터 실수는 여기서 바로 잡힌다)
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined starter.c && ./a.out
```

## 4. 힌트

<details>
<summary>힌트 1 — pp 는 "다음에 고칠 칸"이다</summary>

`Node **pp = head;` 로 시작하고, 넘어갈 때는 `pp = &(*pp)->next;`. 멈췄을 때 `*pp` 는 "지금 자리에 있는 노드"(또는 NULL), `pp` 는 "그 노드를 가리키는 링크". 삽입은 `n->next = *pp; *pp = n;`, 삭제는 `*pp = (*pp)->next;`.

</details>

<details>
<summary>힌트 2 — 정렬 삽입의 멈춤 조건과 안정성</summary>

`while (*pp && (*pp)->val <= n->val)` — `<=` 이므로 같은 값들을 지나쳐서 그 *뒤*에 들어간다 (안정). `<` 로 쓰면 같은 값의 앞에 들어간다.

</details>

<details>
<summary>힌트 3 — 반복 뒤집기는 "끊기 전에 저장"</summary>

`next = cur->next` 를 **먼저** 저장하고, `cur->next = prev` 로 방향을 바꾼 뒤, `prev = cur; cur = next;` 로 한 칸 전진. 끝나면 `prev` 가 새 head.

</details>

<details>
<summary>힌트 4 — 재귀 뒤집기는 "나머지를 믿는다"</summary>

`head->next` 이후를 재귀로 뒤집으면 그 결과의 *꼬리*는 여전히 `head->next` 노드다. 그러므로 `head->next->next = head; head->next = NULL;`. 기저: 빈 리스트 또는 노드 1개.

</details>

<details>
<summary>힌트 5 — Floyd 두 번째 단계</summary>

slow와 fast가 만난 뒤, slow를 head로 되돌리고 **둘 다 1칸씩** 움직이면 사이클 시작점에서 만난다. 이유는 풀이의 식 `a = kL − b` 참고.

</details>

## 5. 풀이

### Part A — 이중 포인터 하나로 세 연산

일반적인 "prev 포인터" 방식은 head인지 아닌지 분기가 필요하다:

```c
/* prev 방식: head 일 때와 아닐 때 코드가 다르다 */
if (head->val == v) head = head->next;
else prev->next = cur->next;
```

이중 포인터는 *head 변수의 주소*와 *각 노드 next 칸의 주소*를 같은 타입(`Node **`)으로 다루므로 분기가 사라진다. Linus Torvalds가 "good taste" 예시로 든 바로 그 코드다.

```svg
<svg viewBox="0 0 720 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">insert_sorted(&amp;head, 5) 에서 pp 가 움직이는 모습</text>
  <g fill="currentColor" font-family="monospace">
    <text x="20" y="60">단계  pp 가 가리키는 칸   *pp      (*pp)-&gt;val &lt;= 5 ?</text>
    <text x="20" y="88">①    &amp;head             n1(1)    1 &lt;= 5 → 전진</text>
    <text x="20" y="112">②    &amp;n1-&gt;next         n2(5)    5 &lt;= 5 → 전진 (안정)</text>
    <text x="20" y="136">③    &amp;n2-&gt;next         n3(9)    9 &lt;= 5 ? 아니오 → 멈춤</text>
  </g>
  <rect x="15" y="150" width="690" height="80" rx="8" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="30" y="175">멈춘 뒤:  n-&gt;next = *pp;  (새 노드가 n3 를 가리킴)</text>
    <text x="30" y="198">          *pp = n;        (n2-&gt;next 칸에 n 을 써 넣음)</text>
    <text x="30" y="221">빈 리스트면 ① 에서 *pp == NULL 로 바로 멈추고 head = n 이 된다 — 분기 없음.</text>
  </g>
</svg>
```

`remove_value` 도 같은 루프에서 조건만 `(*pp)->val == v` 로 바꾸고, 찾으면 `*pp = x->next` 한 줄로 떼어낸다. 떼어낸 노드의 `next` 를 `NULL` 로 정리하는 이유: 호출자가 그 노드를 다른 리스트에 넣거나 풀에 돌려줄 때, 옛 리스트를 가리키는 "유령 링크"가 남아 있으면 나중에 엉뚱한 리스트를 걷게 된다.

| 연산 | 시간 | 추가 메모리 | head 분기 |
|---|---|---|---|
| push_front | O(1) | 0 | 없음 (`n->next = *head; *head = n;`) |
| insert_sorted | O(n) | `pp` 1개 | 없음 |
| remove_value | O(n) | `pp` 1개 | 없음 |

### Part B — 반복 뒤집기: 화살표를 하나씩 돌린다

```svg
<svg viewBox="0 0 720 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">reverse_iter 중간 상태 (cur = 3 을 처리하기 직전)</text>
  <g stroke="currentColor" fill="none">
    <rect x="40" y="70" width="60" height="34"/><rect x="160" y="70" width="60" height="34"/>
    <rect x="280" y="70" width="60" height="34"/><rect x="400" y="70" width="60" height="34"/>
    <rect x="520" y="70" width="60" height="34"/>
  </g>
  <rect x="280" y="70" width="60" height="34" class="f-acc"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="70" y="92">1</text><text x="190" y="92">2</text><text x="310" y="92">3</text>
    <text x="430" y="92">4</text><text x="550" y="92">5</text><text x="660" y="92">NULL</text>
    <text x="70" y="40">NULL</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M70 70 L70 50"/><path d="M70 50 l-5 8 M70 50 l5 8"/>
    <path d="M160 87 L105 87"/><path d="M105 87 l8 -5 M105 87 l8 5"/>
    <path d="M340 87 L395 87"/><path d="M395 87 l-8 -5 M395 87 l-8 5"/>
    <path d="M460 87 L515 87"/><path d="M515 87 l-8 -5 M515 87 l-8 5"/>
    <path d="M580 87 L625 87"/><path d="M625 87 l-8 -5 M625 87 l-8 5"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="190" y="128">prev</text><text x="310" y="128">cur</text><text x="430" y="128">next (저장)</text>
  </g>
  <g fill="currentColor">
    <text x="20" y="165">이미 뒤집힌 부분: 2 → 1 → NULL (prev 가 그 head)   아직 남은 부분: 3 → 4 → 5 (cur 가 그 head)</text>
    <text x="20" y="190">한 단계 = next = cur-&gt;next;  cur-&gt;next = prev;  prev = cur;  cur = next;</text>
    <text x="20" y="215">불변식: "prev 리스트 + cur 리스트 = 원래 노드 전부, 끊긴 노드 없음". cur == NULL 이면 prev 가 답.</text>
  </g>
</svg>
```

**재귀 버전**은 코드는 짧지만 깊이가 리스트 길이와 같다. 스택 프레임 하나가 (복귀 주소 + 저장 레지스터) 16~32바이트라면 노드 1,000개 리스트는 16~32 KB 스택을 먹는다. FreeRTOS 태스크 스택이 보통 512 B ~ 2 KB인 Cortex-M에서는 **스택 오버플로 → HardFault** 다. 이 코드는 꼬리 재귀(tail recursion)가 아니라서(재귀 호출 *뒤에* `head->next->next = head` 를 해야 함) 컴파일러가 루프로 바꿔주지도 못한다. 결론: 면접에선 둘 다 쓸 줄 알되, 펌웨어 코드엔 반복 버전.

| 방식 | 시간 | 스택 | 펌웨어에서 |
|---|---|---|---|
| 반복 (prev/cur/next) | O(n) | O(1) | 권장 |
| 재귀 | O(n) | O(n) — 깊이 = 길이 | 길이 상한이 작고 확실할 때만 |

### Part C — Floyd: 왜 head로 되돌리면 시작점에서 만나는가

fast는 slow보다 매 단계 1칸씩 더 간다. 둘 다 고리 안에 들어오면 둘 사이 거리가 매 단계 1씩 줄어 **반드시** 만난다 (건너뛸 수 없다). 고리가 없으면 fast가 먼저 NULL에 닿는다.

```svg
<svg viewBox="0 0 720 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">ρ(로) 모양: 꼬리 a, 고리 길이 L, 만난 지점은 입구에서 b</text>
  <g stroke="currentColor" fill="none">
    <path d="M40 140 L230 140"/>
    <circle cx="320" cy="140" r="90"/>
  </g>
  <circle cx="40" cy="140" r="6" class="f-acc"/>
  <circle cx="230" cy="140" r="7" class="f-ok" stroke="currentColor"/>
  <circle cx="384" cy="76" r="7" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="25" y="125">head</text>
    <text x="120" y="160">a 칸</text>
    <text x="180" y="125">입구(entry)</text>
    <text x="300" y="45">b 칸</text>
    <text x="395" y="70">만난 곳</text>
    <text x="300" y="145">고리 L 칸</text>
  </g>
  <g fill="currentColor">
    <text x="440" y="110">slow 이동: a + b</text>
    <text x="440" y="134">fast 이동: a + b + kL  (k 바퀴 더)</text>
    <text x="440" y="158">fast = 2 × slow  ⇒  a + b = kL</text>
    <text x="440" y="182" font-weight="bold">⇒  a = kL − b</text>
    <text x="440" y="212">head 에서 a 칸 = 만난 곳에서 a 칸</text>
    <text x="440" y="234">(L − b 칸 + 몇 바퀴) → 둘 다 입구 도착</text>
  </g>
</svg>
```

그래서 만난 뒤 한 포인터를 head로 되돌리고 **둘 다 1칸씩** 가면, a칸 뒤 정확히 입구에서 만난다. 테스트 리스트 `1→…→10→4` 는 a = 3, L = 7 이다.

- 1-노드 자기 고리(`n->next = n`): a = 0, L = 1. 첫 단계에 만나고, head로 되돌린 slow가 이미 fast와 같으므로 head를 반환.
- 전체 고리(`10→1`): a = 0 이므로 입구 = head.
- 메모리 O(1) — "방문 표시용 해시셋" 이나 노드에 visited 비트를 다는 방법보다 RAM이 안 든다. 펌웨어에서 리스트 손상 검사(watchdog 전에 "ready list가 고리인가?")에 그대로 쓸 수 있다.

**중간 노드**도 같은 slow/fast다. fast가 끝에 닿으면 slow는 절반 지점. 루프 조건 `while (fast && fast->next)` 에서 짝수 길이면 slow가 두 번째 중간에서 멈춘다.

| 길이 | 리스트 | slow 최종 | 반환 |
|---|---|---|---|
| 1 | 1 | 1 | 1 |
| 2 | 1 2 | 2 | 2 (두 번째 중간) |
| 5 | 1 2 3 4 5 | 3 | 3 |
| 6 | 1 2 3 4 5 6 | 4 | 4 |

첫 번째 중간이 필요하면 조건을 `while (fast->next && fast->next->next)` 로 바꾼다 (추가 연습 4의 회문 검사가 이 형태).

### ASan 으로 확인하기

`-fsanitize=address,undefined` 로 solution.c / practice.c 를 돌리면 경고 없이 `27/27`, `13/13` 이 나온다. 단, **정적 풀은 ASan이 "해제"를 모른다** — 풀에서 꺼낸 노드를 다 쓰고 돌려줘도 ASan 입장에선 여전히 살아 있는 전역 배열이다. 그래서 추가 연습 5에서는 free list에 넣을 때 `ASAN_POISON_MEMORY_REGION` 으로 노드를 "독" 처리해서, 해제 후 사용(use-after-free)을 ASan이 잡게 만든다. NULL 역참조(빈 리스트에서 `head->next`)는 ASan이 SEGV 리포트와 함께 줄 번호를 알려준다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out          # 27/27 passed
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined solution.c && ./a.out   # 27/27, 리포트 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `void push_front(Node *head, Node *n) { n->next = head; head = n; }` | 매개변수 복사본만 바뀜 → 호출자의 head 그대로 | `Node **head` 로 받고 `*head = n` |
| 반복 뒤집기에서 `cur->next = prev;` 를 `next` 저장보다 먼저 | 나머지 리스트로 가는 유일한 링크를 덮어써서 잃어버림 | `Node *next = cur->next;` 를 맨 먼저 |
| 재귀 뒤집기에서 `head->next = NULL;` 누락 | 옛 head(새 tail)가 2번 노드를 계속 가리켜 1↔2 고리가 생김 | 꼬리 정리 한 줄 추가 |
| `while (fast->next && fast)` | 순서가 반대 — fast가 NULL이면 `fast->next` 에서 먼저 터짐 | `while (fast && fast->next)` (단락 평가 순서) |
| Floyd에서 `fast = fast->next->next` 전에 `fast->next` 검사 안 함 | 홀수 길이 리스트 끝에서 NULL 역참조 | 루프 조건에 `fast->next` 포함 |
| 삭제 루프에서 지운 뒤에도 `pp = &(*pp)->next;` | 연속된 같은 값 하나를 건너뜀 (`7,7` 중 하나만 지움) | 지웠으면 pp를 그대로 두기 (추가 연습 1) |
| 떼어낸 노드의 `next` 를 그대로 둠 | 재사용 시 옛 리스트를 가리키는 유령 링크 | `x->next = NULL;` |
| 테스트에서 고리 리스트를 출력 함수로 찍기 | 무한 루프 → 테스트가 멈춤 | 걷는 칸 수 상한(`k <= POOL_SIZE`)을 둔다 |

## 8. 면접 Q&A

<details>
<summary>Q1. 이중 포인터로 삭제하는 코드를 쓰고, prev 포인터 방식과 비교해 장점을 말해보세요.</summary>

`for (Node **pp = &head; *pp; pp = &(*pp)->next) if ((*pp)->val == v) { *pp = (*pp)->next; break; }`. head 변수와 next 칸이 같은 타입의 "링크"라서 head 삭제를 따로 처리하지 않는다 → 분기가 없어 버그 여지가 적다. 꼬리질문 "더미(dummy) 노드를 쓰면?" → 같은 효과지만 노드 하나만큼 메모리를 쓰고, 정적 풀에선 더미용 노드를 따로 마련해야 한다. 이중 포인터는 공짜.

</details>

<details>
<summary>Q2. 재귀 뒤집기의 공간 복잡도는? 임베디드에서 쓰면 안 되는 이유는?</summary>

O(n) 스택. 꼬리 재귀가 아니라(재귀 호출 뒤에 할 일이 있음) 최적화로 없어지지도 않는다. 태스크 스택이 1 KB인 Cortex-M에서 프레임 16 B면 64개만 넘어도 위험 구간이고, 넘치면 인접 메모리를 덮거나 MPU/스택 리밋(ARMv8-M `PSPLIM`)으로 HardFault. MISRA C:2012 Rule 17.2도 재귀를 금지한다.

</details>

<details>
<summary>Q3. Floyd에서 fast가 3칸씩 가면 어떻게 되나요? 왜 보통 2칸인가요?</summary>

상대 속도가 2가 되어 고리 길이 L이 짝수일 때 거리 차가 홀수면 영원히 건너뛸 수 있다 (예: L=2, 거리 1). 상대 속도 1이면 거리가 1씩 줄어 반드시 0이 된다. 또 입구를 찾는 `a = kL − b` 식이 2배 속도에서 성립하는 식이다. 꼬리질문 "고리 길이 L은?" → 만난 뒤 한 포인터를 고정하고 다른 쪽을 한 바퀴 돌려 센다.

</details>

<details>
<summary>Q4. 사이클 검출의 다른 방법과 트레이드오프는?</summary>

① 방문 노드 해시셋: O(n) 메모리 — 펌웨어에선 불가. ② 노드에 visited 비트: 노드를 수정해야 하고 다른 순회와 공유 불가. ③ Brent 알고리즘: 2의 거듭제곱 간격으로 기준점을 옮김, 평균적으로 Floyd보다 함수 호출(포인터 이동)이 적다. ④ 길이 상한 걸기: 정적 풀이면 "64칸 넘게 걸으면 고리" — 가장 싸고 실무에서 흔한 방어 코드.

</details>

<details>
<summary>Q5. 연결 리스트는 배열보다 언제 나쁜가요? 펌웨어에서 왜 그래도 쓰나요?</summary>

포인터 따라가기는 캐시 지역성이 나쁘고(노드가 흩어짐), 노드마다 포인터 4~8바이트가 붙고, 임의 접근이 O(n). Cortex-M0/M4처럼 캐시가 없는 MCU에선 지역성 손해가 작고, 대신 "중간 삽입/삭제 O(1)" 과 "고정 크기 노드를 정적 풀에서" 쓸 수 있다는 장점이 크다. 타이머 대기열, 대기 태스크 목록이 대표적. 포인터 대신 `uint8_t` 인덱스로 연결하면 RAM이 더 준다 (DS-B07).

</details>

<details>
<summary>Q6. ISR과 메인 루프가 같은 단일 연결 리스트에 push_front 하면 무엇이 깨지나요?</summary>

`n->next = head; head = n;` 두 줄 사이에 ISR이 들어와 head를 바꾸면, 메인이 낡은 head로 덮어써서 ISR이 넣은 노드를 잃는다 (lost update — C-B01의 RMW 경쟁과 같은 모양). 해결: 임계 구역(PRIMASK), 또는 LDREX/STREX(C11 `atomic_compare_exchange`)로 lock-free push (Treiber 스택). pop까지 lock-free로 하려면 ABA 문제가 생긴다 — Advanced 레벨 주제.

</details>

## 9. 추가 연습

### 연습 1 — 값 v를 모두 삭제

`int remove_all(Node **head, int v)` — 지운 개수 반환. `7,7,1,7,2,7` 에서 7 → `1,2`, 4개.

<details>
<summary>답 보기</summary>

같은 `pp` 루프인데, **지웠을 때는 pp를 전진시키지 않는다**. `*pp` 자리에 다음 노드가 당겨져 왔으니 그 자리를 다시 검사해야 연속된 7을 놓치지 않는다. `while (*pp) { if (match) *pp = (*pp)->next; else pp = &(*pp)->next; }`.

</details>

### 연습 2 — 끝에서 k번째 노드 삭제 (한 번 순회)

`Node *remove_nth_from_end(Node **head, int k)` — `1..5`, k=2 → 4 삭제. k가 길이보다 크면 NULL.

<details>
<summary>답 보기</summary>

`lead` 를 k칸 먼저 보내고(도중에 NULL이면 k > 길이), 그다음 `lead` 와 `pp` 를 함께 전진. `lead` 가 NULL이 되면 `*pp` 가 끝에서 k번째. pp를 쓰므로 k == 길이(= head 삭제)도 분기 없이 처리된다.

</details>

### 연습 3 — 정렬된 두 리스트 병합

`Node *merge_sorted(Node *a, Node *b)` — 새 노드 할당 없이 링크만 바꾼다. 같은 값이면 a 먼저(안정).

<details>
<summary>답 보기</summary>

더미 노드 대신 `Node *head = NULL, **tail = &head;` 로 "다음에 채울 링크"를 들고 다닌다. 작은 쪽을 `*tail` 에 걸고 `tail = &그노드->next`. 한쪽이 바닥나면 `*tail = 남은 쪽`. merge sort의 핵심 단계이고, 정렬된 타이머 리스트 두 개를 합칠 때도 쓴다.

</details>

### 연습 4 — O(1) 메모리 회문 검사 (원상 복구 포함)

`bool is_palindrome(Node *head)` — `1,2,3,2,1` 참, `1,2,3,1` 거짓. 끝나면 리스트가 원래대로여야 한다.

<details>
<summary>답 보기</summary>

① slow/fast로 *앞절반의 마지막* 노드를 찾고 (`while (fast->next && fast->next->next)`) ② 그 뒤를 뒤집어 ③ 앞에서부터 비교 ④ **다시 뒤집어 붙인다**. ④를 빼먹으면 호출자의 리스트가 망가진다 — 면접관이 꼭 묻는 부분.

</details>

### 연습 5 — free list 풀 + ASan poison 으로 use-after-free 잡기

노드 8개 풀을 free list(LIFO)로 관리하고, 해제한 노드는 `ASAN_POISON_MEMORY_REGION` 으로 독 처리하라. `-DDEMO_UAF` 로 빌드하면 해제된 노드를 읽는 줄에서 ASan이 멈춰야 한다.

<details>
<summary>답 보기</summary>

free list도 노드의 `next` 칸을 재사용한다 (추가 메모리 0). 할당: unpoison → `free_head = n->next` → 반환. 해제: `n->next = free_head; free_head = n;` → poison. ASan이 꺼진 빌드에선 매크로가 빈 식이 되도록 `__has_feature(address_sanitizer)` / `__SANITIZE_ADDRESS__` 로 감싼다. 실제 실행 결과: `ERROR: AddressSanitizer: use-after-poison ... READ of size 4 ... practice.c:188`. FreeRTOS `heap_x` 나 펌웨어 메모리 풀에 같은 기법을 넣으면 호스트 단위 테스트에서 해제 후 사용을 잡을 수 있다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out                                   # 13/13 passed
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined practice.c && ./a.out    # 13/13, 리포트 없음
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -DDEMO_UAF practice.c && ./a.out   # ASan 이 멈춤
```

## 10. 핵심 정리

- `Node **pp` = "고쳐 쓸 링크의 주소". 삽입 `n->next = *pp; *pp = n;`, 삭제 `*pp = (*pp)->next;` — head도 같은 코드.
- 전진은 `pp = &(*pp)->next`. 삭제 후엔 전진하지 않는다(연속 삭제).
- 반복 뒤집기: **저장 → 뒤집기 → prev 전진 → cur 전진**. 끝나면 prev가 head. 스택 O(1).
- 재귀 뒤집기는 스택 O(n), 꼬리 재귀 아님 → 펌웨어에선 피한다 (MISRA 17.2).
- Floyd: slow 1칸, fast 2칸, 만나면 고리. 한쪽을 head로 → 1칸씩 → 입구 (`a = kL − b`).
- 중간: `while (fast && fast->next)` → 두 번째 중간. 첫 번째 중간은 `fast->next && fast->next->next`.
- malloc 대신 정적 풀. ASan은 풀 "해제"를 모르니 poison API로 알려준다.

## 참고자료

- [Linus Torvalds — "good taste" 이중 포인터 삭제 (TED 2016, 해설: github.com/mkirchner/linked-list-good-taste)](https://github.com/mkirchner/linked-list-good-taste)
- Robert W. Floyd, *Nondeterministic Algorithms* (J. ACM, 1967) — 사이클 검출의 출처로 알려짐; Knuth TAOCP Vol. 2 §3.1 연습 6
- [Richard P. Brent, *An improved Monte Carlo factorization algorithm* (BIT, 1980) — Brent 사이클 검출](https://maths-people.anu.edu.au/~brent/pd/rpb051i.pdf)
- [Clang AddressSanitizer — Manual poisoning](https://clang.llvm.org/docs/AddressSanitizer.html#manual-poisoning)
- MISRA C:2012 Rule 17.2 — 함수는 직접·간접적으로 자기 자신을 호출하지 않는다
- [FreeRTOS list.c — 커널 ready/delayed list 구현](https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/list.c)
