---
id: DS-A05
time: 120분
updated: 2026-11-08
---
# DS-A05 침입형 레드블랙 트리 (Linux rbtree 스타일)

## 학습 목표

- `struct rb_node`를 객체 안에 박아 넣고(intrusive), 색을 **부모 포인터의 비트 0**에 숨기는 인코딩을 구현한다. 어떤 정렬 가정 위에 서 있는지 `_Static_assert`로 적고, `packed`가 왜 이 가정을 깨는지 설명한다.
- 삽입 복구 3 케이스와 삭제 복구 4 케이스를 그림 없이 재현하고, "삽입 회전 ≤ 2, 삭제 회전 ≤ 3"이 **왜** 성립하는지 케이스 흐름으로 증명한다.
- 삭제에서 키를 복사하지 않고 **후계자 노드를 재연결**해야 하는 이유(객체 포인터의 안정성)를 말하고 구현한다.
- 불변식 검사기(루트 검정, 빨강 연속 금지, 검정 높이, 전역 BST 순서, 부모 링크, 개수)가 각 위반을 실제로 잡는지 음성 테스트로 확인하고, 키 6개 전수 순열 + 랜덤 100만 회로 검증한다.
- RB와 AVL을 **측정값**으로 비교하고, 펌웨어 타임아웃 큐를 `rb_root_cached`로 만든다. 언제 정렬 리스트가 더 나은지 수치로 판단한다.

## 1. 문제

"균형 트리를 하나 짜 보세요. AVL 말고 레드블랙으로요. 삭제까지요." 시니어 펌웨어 면접에서 레드블랙은 **구현력**과 **설계 판단**을 동시에 본다. 리눅스의 타이머(hrtimer), 스케줄러 런큐(see:OS-A04), I/O 스케줄러가 모두 이 구조 위에 있고, 펌웨어에서는 malloc 없이 객체 안에 노드를 박아 쓰는 형태로 그대로 가져올 수 있다.

레드블랙 트리는 다섯 규칙을 지키는 이진 탐색 트리다. 노드는 빨강이나 검정이고, 루트는 검정, NIL은 검정, 빨강의 자식은 검정(**빨강 연속 금지**), 어떤 노드에서든 아래 NIL까지 가는 모든 경로의 검정 수가 같다(**검정 높이 동일**). 뒤의 두 규칙에서 높이 상한이 나온다.

- 검정 높이 b인 서브트리에는 내부 노드가 최소 2^b − 1개 있다(귀납법, 빨강을 무시하면 완전 이진 트리다).
- 빨강 연속 금지 때문에 경로의 절반 이상이 검정이다. 즉 b ≥ h/2.
- 합치면 n ≥ 2^(h/2) − 1, 곧 **h ≤ 2·log₂(n+1)** (Guibas & Sedgewick 1978, CLRS 13장).

이 레슨의 "침입형(intrusive)"은 DS-M01의 `list_head`와 같은 뜻이다. 트리는 키도 모르고 메모리도 할당하지 않는다. 노드는 사용자 객체의 필드이고, `container_of`로 객체를 되찾는다.

```svg
<svg viewBox="0 0 740 240" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">item_t 안의 rb_node (64비트): 색은 부모 포인터의 비트 0 에 있다</text>
<rect x="10" y="38" width="150" height="34" class="f-mut" stroke="currentColor"/><text x="18" y="60" fill="currentColor" font-family="monospace">int32_t key</text><rect x="160" y="38" width="150" height="34" class="f-mut" stroke="currentColor"/><text x="168" y="60" fill="currentColor" font-family="monospace">uint32_t seq</text><rect x="310" y="38" width="170" height="34" class="f-acc" stroke="currentColor"/><text x="318" y="60" fill="currentColor" font-family="monospace">parent_color</text><rect x="480" y="38" width="120" height="34" class="f-mut" stroke="currentColor"/><text x="488" y="60" fill="currentColor" font-family="monospace">rb_right</text><rect x="600" y="38" width="120" height="34" class="f-mut" stroke="currentColor"/><text x="608" y="60" fill="currentColor" font-family="monospace">rb_left</text>
<path d="M10 80 L10 90 L310 90 L310 80" stroke="currentColor" fill="none"/><text x="100" y="106" fill="currentColor" font-size="13">사용자 데이터 (+8 B)</text>
<path d="M310 80 L310 90 L720 90 L720 80" stroke="currentColor" fill="none"/><text x="430" y="106" fill="currentColor" font-size="13">struct rb_node = 24 B (Cortex-M 은 12 B)</text>
<rect x="60" y="126" width="520" height="34" class="f-acc" stroke="currentColor"/><rect x="580" y="126" width="70" height="34" class="f-warn" stroke="currentColor"/>
<text x="74" y="148" fill="currentColor" font-family="monospace">부모 노드 주소 (비트 63..1) — 노드 정렬이 ≥ 2 라서 비트 0 은 항상 0</text>
<text x="592" y="148" fill="currentColor" font-family="monospace">color</text>
<path d="M395 76 L320 124" stroke="currentColor" fill="none" stroke-dasharray="3 3"/>
<text x="60" y="184" fill="currentColor" font-size="13">0 = 빨강, 1 = 검정. 새 노드는 빨강이므로 rb_link_node 는 parent 값을 그대로 저장한다.</text>
<text x="60" y="204" fill="currentColor" font-size="13">item = container_of(node_ptr, item_t, node) = (char *)node_ptr - offsetof(item_t, node)</text>
<text x="60" y="224" fill="currentColor" font-size="13">트리 밖 표시: parent_color == 자기 주소 (실제 트리에 나올 수 없는 값)</text>
</svg>
```

| 파트 | 구현 | 체크 (결정적) | 관찰 데모 |
|---|---|---|---|
| A | 인코딩, 회전, `rb_insert_color`, `rb_first/last/next/prev`, `rb_validate` | 정확한 모양·색 문자열 비교, 검사기 음성 테스트 | `demo_asm.c` (Cortex-M4 명령) |
| B | `rb_erase` + `rb_erase_color` | 케이스별 고정 트리, 전수 720×720, 랜덤 100만 회, 케이스 커버리지, 회전 상한 | `demo_rb_vs_avl.c` |
| C | `rb_root_cached`, 타임아웃 큐 | FIFO 동률, 랩어라운드, 취소, 주기 타이머, 처리량 제한, 모델 대조 10만 회 | `demo_timerq_vs_list.c` |

### Part A — 인코딩, 회전, 삽입, 순회, 검사기

- `parent_color` 한 워드에 부모 주소와 색을 함께 넣는다. `rb_parent`, `rb_color`, `rb_set_parent`, `rb_set_parent_color`를 구현한다.
- `rb_rotate_left/right`: 부모 링크까지 고치고 색은 건드리지 않는다.
- `rb_insert_color`: 호출자가 `rb_link_node`로 빨강 노드를 연결한 뒤 부른다. 3 케이스 + 거울상.
- `item_insert`: 사용자 쪽 코드다. 키를 비교하며 `struct rb_node **link`로 내려간다.
- `rb_first/last/next/prev`: 스택 없이 부모 포인터로 걷는다.
- `rb_validate`: 위반 종류를 enum으로 돌려준다. 사이클에도 끝나야 한다.

### Part B — 삭제와 대규모 검증

- `rb_erase`: 자식 0~1개면 노드 자체가 빠지고, 자식 2개면 **후계자 노드**가 빠져서 삭제 노드 자리로 옮겨 간다.
- `rb_erase_color`: 이중 검정 복구 4 케이스 + 거울상.
- 검증: 케이스별 고정 트리 17개, 키 6개의 삽입 순열 720 × 삭제 순열 720 전수, 랜덤 20만 회(n ≤ 32, 매 연산 전체 검사), **랜덤 100만 회**(n ≤ 8192, 1000회마다 전체 검사 + 끝에 전부 삭제).
- 모든 케이스가 양 방향으로 실제 실행됐는지, 연산당 회전이 삽입 ≤ 2, 삭제 ≤ 3인지 체크한다.

### Part C — 타임아웃 큐 (cached leftmost)

- `rb_insert_color_cached`, `rb_erase_cached`로 가장 이른 노드를 O(1)에 읽는다.
- `tq_add`, `tq_cancel`, `tq_next_deadline`, `tq_tick`을 만든다. 32비트 틱 랩어라운드, 같은 시각은 FIFO, 이미 등록된 타이머는 거절, 취소는 여러 번 불러도 안전, 주기 타이머 재등록, 콜백이 다른 타이머를 취소해도 안전, 한 번에 발화하는 수 제한(budget).

## 2. Problem (English)

> **Intrusive red-black tree (Linux rbtree style).**
>
> `struct rb_node { uintptr_t parent_color; struct rb_node *rb_right, *rb_left; };` is embedded in user objects. Bit 0 of `parent_color` is the colour (0 = red, 1 = black); the rest is the parent pointer. State the alignment assumption with `_Static_assert`.
>
> Implement, without malloc and without recursion in the tree code:
>
> 1. `rb_insert_color(node, root)`, called after `rb_link_node()` attached `node` as a red leaf; `rb_first/last/next/prev`.
> 2. `rb_erase(node, root)`. A node with two children is replaced by **relinking** its successor (object addresses must stay valid; do not copy keys).
> 3. `rb_validate(root, n, key_fn, ctx, allow_dup)` returning which invariant failed: root black, no red-red, equal black height, global BST order, parent links, node count.
> 4. A timeout queue on `struct rb_root_cached`: `tq_add`, `tq_cancel`, `tq_next_deadline` (O(1)), `tq_tick(q, now, budget, fire, arg)`. Ticks are 32-bit and wrap; equal deadlines fire in FIFO order.
>
> Test with exhaustive permutations of a small key set and 1,000,000 random operations against a reference model.
>
> Follow-ups: why are at most 2 / 3 rotations enough? Is "O(1) rotations" the same as O(1) worst-case work? RB or AVL for a lookup-heavy table? When is a sorted list better? What breaks with `__packed`? Can an ISR touch this tree?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행 결과(실제 출력, 앞뒤 발췌):

```text
sizeof(struct rb_node) = 24, offsetof(item_t, node) = 8
== Part A: encoding
FAIL starter.c:574  rb_parent(&c) == &p && rb_color(&c) == RB_BLACK
FAIL starter.c:576  rb_parent(&c) == &p && rb_color(&c) == RB_RED
PASS rb_parent(&c) == NULL && rb_color(&c) == RB_BLACK
== Part A: validator catches each violation
PASS fx_validate("((10r)20B(30r))", 3) == RB_OK
FAIL starter.c:584  fx_validate("((10B)20r(30B))", 3) == RB_ERR_ROOT_RED
...
PASS RB_EMPTY_NODE(&t[0].node) && RB_EMPTY_NODE(&t[1].node)
SKIP timer random: unit checks failed
12/72 passed
```

하네스 설계:

- **트리를 문자열로 쓴다.** `"((10r)20B(30r))"`는 "(왼쪽 키 색 오른쪽)" 형식이고, `fx_load`가 이것을 원시 필드만으로 트리로 만든다. 그래서 케이스마다 입력과 기대 결과를 **정확한 모양과 색**으로 적을 수 있다. 시작 트리도 검사기를 통과해야 쓰인다(`bad fixture`).
- **검사기 음성 테스트**는 위반을 정확히 하나만 넣은 트리로 enum 값까지 비교한다. `((10B(25r))20B(30B))`는 부모-자식끼리만 보면 맞는 순서라서 전역 순서를 보지 않는 검사기를 잡아낸다.
- **객체 동일성**: `t_erase`는 삭제 뒤 살아남은 키마다 "처음과 같은 객체"가 트리에 있는지 본다. 키만 복사하는 삭제는 모양이 맞아도 여기서 FAIL한다.
- 전수·랜덤 테스트는 단위 체크가 통과해야 돈다(`SKIP`). 그래서 스텁 때문에 끝나지 않거나 터지는 일이 없다.
- 랜덤 100만 회는 새니타이저 빌드에서도 줄이지 않는다. 이 맥에서 `-O2` 0.5초, ASan+UBSan 1.0초다.

관찰용 파일(체크와 별개):

```bash
cc -std=c11 -O2 demo_rb_vs_avl.c -o drv && ./drv                # 높이·회전·메타데이터 작업량 + 벌크 시간 (약 15초)
cc -std=c11 -O2 demo_timerq_vs_list.c -o dtl && ./dtl           # 정렬 리스트 vs RB 타이머 큐 (약 16초)
cc -std=c11 -O2 -S demo_asm.c -o -                              # arm64 asm
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S -o - demo_asm.c
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -Os -c demo_asm.c -o d.o && llvm-size d.o
```

## 4. 힌트

<details><summary>힌트 1 — 왜 빨강이 0인가</summary>

새 노드는 항상 빨강으로 들어간다(검정으로 넣으면 검정 높이가 깨진다. 빨강이면 깨질 수 있는 건 "빨강 연속" 하나뿐이다). `RB_RED == 0`이면 `rb_link_node`가 `node->parent_color = (uintptr_t)parent;` 한 줄로 끝난다.
</details>

<details><summary>힌트 2 — 삽입 복구는 "삼촌"만 본다</summary>

node와 부모가 둘 다 빨강이면 조부모는 반드시 검정이고 존재한다(부모가 빨강이니 루트가 아니다). 결정은 삼촌의 색으로 한다. 빨강이면 색만 뒤집고 두 칸 위로, 검정이면 회전으로 끝낸다. 꺾인 모양이면 먼저 부모에서 회전해 일직선을 만든다.
</details>

<details><summary>힌트 3 — rb_next는 두 갈래</summary>

오른쪽 자식이 있으면 그 서브트리의 왼쪽 끝이다. 없으면 "내가 왼쪽 자식인 첫 조상"까지 올라간다. 오른쪽 자식이었던 동안은 계속 올라간다는 뜻이다.
</details>

<details><summary>힌트 4 — 삭제에서 "빠지는 색"을 먼저 정한다</summary>

복구가 필요한지는 "트리에서 실제로 사라지는 위치의 색"으로 정해진다. 자식 2개면 그 위치는 후계자의 원래 자리다. 빠지는 색이 검정이고 그 자리를 잇는 자식이 있으면 그 자식은 빨강이므로(자식이 하나뿐인 노드는 검정 높이 때문에 빨강 잎만 가질 수 있다) 검정으로 칠하면 끝이다. 자식이 없을 때만 복구 루프에 들어간다.
</details>

<details><summary>힌트 5 — 이중 검정은 NULL에서 시작한다</summary>

첫 반복에서는 `node`가 NULL이다. 그러면 "왼쪽 자식인가?"를 어떻게 판단할까? `parent->rb_right != node`로 본다. 오른쪽이 NULL이고 node도 NULL이면 node는 오른쪽 자식이다. 왼쪽과 오른쪽이 둘 다 NULL일 수는 없다(검정 하나가 빠졌으니 형제 쪽에 노드가 있어야 검정 높이가 맞았다).
</details>

<details><summary>힌트 6 — Case 1의 다음은 항상 끝난다</summary>

Case 1을 지나면 부모는 빨강이고 형제는 검정이다. 이 상태에서 Case 2는 "부모가 빨강"이라 바로 끝나고, Case 3/4도 그 반복에서 끝난다. 그래서 Case 1은 삭제 한 번에 최대 한 번이다.
</details>

<details><summary>힌트 7 — 캐시를 고치는 순서</summary>

`rb_erase_cached`에서 캐시를 옮길 때 `rb_next(node)`는 `rb_erase` **전에** 부른다. 지운 뒤에 부르는 코드는 지운 노드의 남은 필드에 기대게 되고, 이는 구현이 필드를 지우지 않는다는 우연에 기대는 것이다(7장에서 실제로 실험한다).
</details>

## 5. 풀이

### Part A — 색 비트 인코딩: 무엇을 가정하는가

| 가정 | 깨지는 경우 | 우리 방어 |
|---|---|---|
| 노드 주소의 비트 0이 항상 0 | 정렬 1인 타입, `__packed` 컨테이너 | `_Static_assert(_Alignof(struct rb_node) >= 2)`, 컨테이너별 `RB_CONTAINER_OK` (연습 6) |
| 포인터 ↔ 정수 변환 뒤 비트를 조작해도 의미가 유지됨 | `void *` ↔ `uintptr_t` 왕복만 표준이 보장한다(C11 7.20.1.4). 변환 결과의 비트 의미는 구현 정의다(6.3.2.3) | 주소를 그대로 정수로 쓰는 gcc/clang + ARM/x86에서 동작. 표준만으로 보장되는 코드는 아니다 |
| 포인터 하위 비트를 지워도 같은 객체 | 포인터 인증이나 캐퍼빌리티를 쓰는 아키텍처 | 이 레슨의 범위 밖. 해당 플랫폼이면 색을 별도 필드로 |

리눅스는 `struct rb_node`에 `__attribute__((aligned(sizeof(long))))`를 달고, `include/linux/rbtree_augmented.h`의 `__rb_parent`에서 `~3`으로 하위 2비트를 지운다. 정렬이 4 이상임을 가정하는 셈이다(소스 주석에 "allegedly CRIS needs it"이라고 적혀 있다). 우리는 필요한 만큼만 지우려고 `~1`을 쓰고, 정렬 ≥ 2만 가정한다. Cortex-M4 타깃에서 `sizeof(struct rb_node)` = 12, `_Alignof` = 4다(`_Static_assert`로 thumbv7em 컴파일 확인).

실제 명령을 보자(`demo_asm.c`, `-O2`):

```text
arm64 (Apple clang)                    thumbv7em (cortex-m4)
_demo_parent:                          demo_parent:
    ldr  x8, [x0]                          ldr  r0, [r0]
    and  x0, x8, #0xfffffffffffffffe       bic  r0, r0, #1
    ret                                    bx   lr
_demo_is_red:                          demo_is_red:
    ldrb w8, [x0]                          ldrb r0, [r0]
    tst  w8, #0x1                          movs r1, #1
    cset w0, eq                            bic.w r0, r1, r0
    ret                                    bx   lr
_demo_set_parent:                      demo_set_parent:
    ldr  x8, [x0]                          ldr  r2, [r0]
    and  x8, x8, #0x1                      and  r2, r2, #1
    orr  x8, x8, x1                        orrs r1, r2
    str  x8, [x0]                          str  r1, [r0]
    ret                                    bx   lr
```

부모 포인터를 읽는 비용은 명령 하나(`bic`)가 더 붙는 것이 전부다. 색 읽기는 컴파일러가 `ldrb`로 줄였다. 리틀엔디언이라 워드의 비트 0이 첫 바이트에 있기 때문이다. 대신 `rb_set_parent`는 읽기-수정-쓰기(read-modify-write)다. 인터럽트 코드와 같은 노드를 만지면 이 세 명령 사이에서 깨진다(8장 Q6).

코드 크기(Cortex-M4, `llvm-size`, insert + erase + erase_color + next, 관찰용 래퍼 3개 포함): **`-Os` 920 B, `-O2` 1452 B**. 노드당 RAM은 12 B다. 16비트 인덱스 풀을 쓰면 더 줄지만 포인터 인코딩을 쓸 수 없어 설계가 달라진다.

### Part A — 회전과 삽입 복구

회전 자체는 DS-M07과 같다. inorder를 보존하고 높이를 옮긴다. 다른 점은 **부모 링크**를 함께 고쳐야 한다는 것이다.

```c
static void rb_rotate_left(struct rb_node *x, struct rb_root *root) {
    struct rb_node *y = x->rb_right, *b = y->rb_left, *p = rb_parent(x);
    x->rb_right = b;
    if (b) rb_set_parent(b, x);
    y->rb_left = x;
    rb_set_parent(y, p);            /* y 는 자기 색을 유지한 채 x 자리로 */
    rb_set_parent(x, y);
    rb_change_child(x, y, p, root);
}
```

링크 쓰기 6개 중 하나라도 빠지면 모양은 맞아 보이는데 나중에 `rb_next`나 다음 복구가 틀린다. 검사기의 부모 링크 확인이 이걸 잡는다.

```svg
<svg viewBox="0 0 740 418" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">삽입 복구 3 케이스 (왼쪽 경우, 오른쪽은 거울상)</text><circle cx="368" cy="14" r="7" class="f-warn" stroke="currentColor"/><text x="380" y="18" fill="currentColor" font-size="13">빨강</text><circle cx="460" cy="14" r="7" class="f-mut" stroke="currentColor"/><text x="472" y="18" fill="currentColor" font-size="13">검정</text><circle cx="552" cy="14" r="7" class="f-acc" stroke="currentColor"/><text x="564" y="18" fill="currentColor" font-size="13">색 모름</text><circle cx="644" cy="14" r="7" class="f-ok" stroke="currentColor"/><text x="656" y="18" fill="currentColor" font-size="13">이중 검정</text>
<text x="10" y="48" fill="currentColor" font-size="13" font-weight="bold">Case 1: 삼촌 빨강</text><line x1="330" y1="62" x2="284" y2="102" stroke="currentColor"/><line x1="284" y1="102" x2="258.7" y2="142" stroke="currentColor"/><circle cx="258.7" cy="142" r="18" class="f-warn" stroke="currentColor"/><text x="258.7" y="147" text-anchor="middle" fill="currentColor" font-size="13">nr</text><circle cx="284" cy="102" r="18" class="f-warn" stroke="currentColor"/><text x="284" y="107" text-anchor="middle" fill="currentColor" font-size="13">pr</text><line x1="330" y1="62" x2="376" y2="102" stroke="currentColor"/><circle cx="376" cy="102" r="18" class="f-warn" stroke="currentColor"/><text x="376" y="107" text-anchor="middle" fill="currentColor" font-size="13">ur</text><circle cx="330" cy="62" r="18" class="f-mut" stroke="currentColor"/><text x="330" y="67" text-anchor="middle" fill="currentColor" font-size="13">gB</text><line x1="570" y1="62" x2="524" y2="102" stroke="currentColor"/><line x1="524" y1="102" x2="498.7" y2="142" stroke="currentColor"/><circle cx="498.7" cy="142" r="18" class="f-warn" stroke="currentColor"/><text x="498.7" y="147" text-anchor="middle" fill="currentColor" font-size="13">nr</text><circle cx="524" cy="102" r="18" class="f-mut" stroke="currentColor"/><text x="524" y="107" text-anchor="middle" fill="currentColor" font-size="13">pB</text><line x1="570" y1="62" x2="616" y2="102" stroke="currentColor"/><circle cx="616" cy="102" r="18" class="f-mut" stroke="currentColor"/><text x="616" y="107" text-anchor="middle" fill="currentColor" font-size="13">uB</text><circle cx="570" cy="62" r="18" class="f-warn" stroke="currentColor"/><text x="570" y="67" text-anchor="middle" fill="currentColor" font-size="13">gr</text><path d="M430 84 L475 84 M468 79 L475 84 L468 89" stroke="currentColor" fill="none"/><text x="10" y="70" fill="currentColor" font-size="13">색만 뒤집는다.</text><text x="10" y="87" fill="currentColor" font-size="13">회전 0번.</text><text x="10" y="104" fill="currentColor" font-size="13">g 가 새 node →</text><text x="10" y="121" fill="currentColor" font-size="13">두 칸 위에서 반복.</text><text x="10" y="138" fill="currentColor" font-size="13">g 가 루트면 마지막에</text><text x="10" y="155" fill="currentColor" font-size="13">검정으로 칠한다.</text>
<text x="10" y="176" fill="currentColor" font-size="13" font-weight="bold">Case 2: 삼촌 검정, 꺾임</text><line x1="330" y1="190" x2="284" y2="230" stroke="currentColor"/><line x1="284" y1="230" x2="309.3" y2="270" stroke="currentColor"/><circle cx="309.3" cy="270" r="18" class="f-warn" stroke="currentColor"/><text x="309.3" y="275" text-anchor="middle" fill="currentColor" font-size="13">nr</text><circle cx="284" cy="230" r="18" class="f-warn" stroke="currentColor"/><text x="284" y="235" text-anchor="middle" fill="currentColor" font-size="13">pr</text><line x1="330" y1="190" x2="376" y2="230" stroke="currentColor"/><circle cx="376" cy="230" r="18" class="f-mut" stroke="currentColor"/><text x="376" y="235" text-anchor="middle" fill="currentColor" font-size="13">uB</text><circle cx="330" cy="190" r="18" class="f-mut" stroke="currentColor"/><text x="330" y="195" text-anchor="middle" fill="currentColor" font-size="13">gB</text><line x1="570" y1="190" x2="524" y2="230" stroke="currentColor"/><line x1="524" y1="230" x2="498.7" y2="270" stroke="currentColor"/><circle cx="498.7" cy="270" r="18" class="f-warn" stroke="currentColor"/><text x="498.7" y="275" text-anchor="middle" fill="currentColor" font-size="13">pr</text><circle cx="524" cy="230" r="18" class="f-warn" stroke="currentColor"/><text x="524" y="235" text-anchor="middle" fill="currentColor" font-size="13">nr</text><line x1="570" y1="190" x2="616" y2="230" stroke="currentColor"/><circle cx="616" cy="230" r="18" class="f-mut" stroke="currentColor"/><text x="616" y="235" text-anchor="middle" fill="currentColor" font-size="13">uB</text><circle cx="570" cy="190" r="18" class="f-mut" stroke="currentColor"/><text x="570" y="195" text-anchor="middle" fill="currentColor" font-size="13">gB</text><path d="M430 212 L475 212 M468 207 L475 212 L468 217" stroke="currentColor" fill="none"/><text x="10" y="198" fill="currentColor" font-size="13">p 에서 왼쪽 회전.</text><text x="10" y="215" fill="currentColor" font-size="13">n 과 p 를 바꿔 부르면</text><text x="10" y="232" fill="currentColor" font-size="13">그대로 Case 3 모양.</text><text x="10" y="249" fill="currentColor" font-size="13">회전 1번.</text>
<text x="10" y="304" fill="currentColor" font-size="13" font-weight="bold">Case 3: 삼촌 검정, 일직선</text><line x1="330" y1="318" x2="284" y2="358" stroke="currentColor"/><line x1="284" y1="358" x2="258.7" y2="398" stroke="currentColor"/><circle cx="258.7" cy="398" r="18" class="f-warn" stroke="currentColor"/><text x="258.7" y="403" text-anchor="middle" fill="currentColor" font-size="13">nr</text><circle cx="284" cy="358" r="18" class="f-warn" stroke="currentColor"/><text x="284" y="363" text-anchor="middle" fill="currentColor" font-size="13">pr</text><line x1="330" y1="318" x2="376" y2="358" stroke="currentColor"/><circle cx="376" cy="358" r="18" class="f-mut" stroke="currentColor"/><text x="376" y="363" text-anchor="middle" fill="currentColor" font-size="13">uB</text><circle cx="330" cy="318" r="18" class="f-mut" stroke="currentColor"/><text x="330" y="323" text-anchor="middle" fill="currentColor" font-size="13">gB</text><line x1="570" y1="318" x2="524" y2="358" stroke="currentColor"/><circle cx="524" cy="358" r="18" class="f-warn" stroke="currentColor"/><text x="524" y="363" text-anchor="middle" fill="currentColor" font-size="13">nr</text><line x1="570" y1="318" x2="616" y2="358" stroke="currentColor"/><line x1="616" y1="358" x2="641.3" y2="398" stroke="currentColor"/><circle cx="641.3" cy="398" r="18" class="f-mut" stroke="currentColor"/><text x="641.3" y="403" text-anchor="middle" fill="currentColor" font-size="13">uB</text><circle cx="616" cy="358" r="18" class="f-warn" stroke="currentColor"/><text x="616" y="363" text-anchor="middle" fill="currentColor" font-size="13">gr</text><circle cx="570" cy="318" r="18" class="f-mut" stroke="currentColor"/><text x="570" y="323" text-anchor="middle" fill="currentColor" font-size="13">pB</text><path d="M430 340 L475 340 M468 335 L475 340 L468 345" stroke="currentColor" fill="none"/><text x="10" y="326" fill="currentColor" font-size="13">g 에서 오른쪽 회전,</text><text x="10" y="343" fill="currentColor" font-size="13">p 와 g 의 색 교환.</text><text x="10" y="360" fill="currentColor" font-size="13">서브트리 루트가 다시</text><text x="10" y="377" fill="currentColor" font-size="13">검정 → 위는 안전. 끝.</text>
</svg>
```

**세 케이스의 흐름.** 진입 시 유일한 위반은 "node와 부모가 둘 다 빨강"이다.

| 케이스 | 조건 | 처리 | 회전 | 다음 |
|---|---|---|---|---|
| 1 | 삼촌 빨강 | 부모·삼촌 검정, 조부모 빨강 | 0 | node = 조부모, 반복 |
| 2 | 삼촌 검정, 꺾임 | 부모에서 회전 | 1 | Case 3 |
| 3 | 삼촌 검정, 일직선 | 부모 검정, 조부모 빨강, 조부모에서 회전 | 1 | 끝 |

**회전이 2번을 넘지 않는 이유.** 회전이 일어나는 경로는 Case 2 → Case 3 또는 Case 3뿐이고 Case 3 뒤에는 `break`다. Case 1은 회전이 없다. 그래서 회전 ≤ 2. **그러나 Case 1은 O(log n)번 반복될 수 있다.** 뒤의 측정표에서 삽입 한 번에 색 변경이 55번까지 나온다.

**왜 Case 3 뒤에 끝나는가.** 회전 후 서브트리의 루트가 검정이고, 이 서브트리의 검정 높이는 삽입 전과 같다. 위로 전달되는 변화가 없다.

리눅스 `lib/rbtree.c`의 `__rb_insert` 주석도 같은 번호를 쓴다("Case 1 - node's uncle is red (color flips)", "Case 2 - right rotate at parent", "Case 3 - left rotate at gparent"). 리눅스는 포인터 쓰기를 케이스 안에 직접 적고 `__rb_rotate_set_parents`만 헬퍼로 쓴다. 우리는 읽기 쉽게 회전 전체를 함수로 뺐다.

**검사기가 잡아야 하는 것.**

| 검사 | 놓치면 지나가는 버그 |
|---|---|
| 루트 검정 | `rb_insert_color` 끝의 `rb_set_black` 누락 |
| 빨강 연속 금지 | 잘못된 케이스 판정, 회전 뒤 색 처리 누락 |
| 검정 높이 동일 | Case 4 색 교환 누락, 빠뜨린 재칠 |
| **전역** BST 순서 (in-order 직전 키와 비교) | 회전에서 B를 잘못 붙임. 부모-자식 비교만으로는 못 잡는다 |
| 부모 링크 | 회전이나 삭제에서 부모 갱신 누락 (rb_next, 복구 경로가 틀어짐) |
| 노드 수 + 방문 상한 | 사이클, 누락 |

### Part B — 삭제: 위치와 색을 분리해서 본다

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">자식 2개 삭제: 키 복사가 아니라 후계자 노드를 node 자리로 재연결</text>
<line x1="190" y1="58" x2="110" y2="110" stroke="currentColor"/><circle cx="110" cy="110" r="18" class="f-mut" stroke="currentColor"/><text x="110" y="115" text-anchor="middle" fill="currentColor" font-size="13">10</text><line x1="190" y1="58" x2="270" y2="110" stroke="currentColor"/><line x1="270" y1="110" x2="226.0" y2="162" stroke="currentColor"/><line x1="226.0" y1="162" x2="250.2" y2="214" stroke="currentColor"/><circle cx="250.2" cy="214" r="18" class="f-warn" stroke="currentColor"/><text x="250.2" y="219" text-anchor="middle" fill="currentColor" font-size="13">35</text><circle cx="226.0" cy="162" r="18" class="f-mut" stroke="currentColor"/><text x="226.0" y="167" text-anchor="middle" fill="currentColor" font-size="13">30</text><line x1="270" y1="110" x2="314.0" y2="162" stroke="currentColor"/><circle cx="314.0" cy="162" r="18" class="f-mut" stroke="currentColor"/><text x="314.0" y="167" text-anchor="middle" fill="currentColor" font-size="13">50</text><circle cx="270" cy="110" r="18" class="f-warn" stroke="currentColor"/><text x="270" y="115" text-anchor="middle" fill="currentColor" font-size="13">40</text><circle cx="190" cy="58" r="18" class="f-mut" stroke="currentColor"/><text x="190" y="63" text-anchor="middle" fill="currentColor" font-size="13">20</text><line x1="540" y1="58" x2="460" y2="110" stroke="currentColor"/><circle cx="460" cy="110" r="18" class="f-mut" stroke="currentColor"/><text x="460" y="115" text-anchor="middle" fill="currentColor" font-size="13">10</text><line x1="540" y1="58" x2="620" y2="110" stroke="currentColor"/><line x1="620" y1="110" x2="576.0" y2="162" stroke="currentColor"/><circle cx="576.0" cy="162" r="18" class="f-mut" stroke="currentColor"/><text x="576.0" y="167" text-anchor="middle" fill="currentColor" font-size="13">35</text><line x1="620" y1="110" x2="664.0" y2="162" stroke="currentColor"/><circle cx="664.0" cy="162" r="18" class="f-mut" stroke="currentColor"/><text x="664.0" y="167" text-anchor="middle" fill="currentColor" font-size="13">50</text><circle cx="620" cy="110" r="18" class="f-warn" stroke="currentColor"/><text x="620" y="115" text-anchor="middle" fill="currentColor" font-size="13">40</text><circle cx="540" cy="58" r="18" class="f-mut" stroke="currentColor"/><text x="540" y="63" text-anchor="middle" fill="currentColor" font-size="13">30</text><path d="M345 110 L395 110 M388 105 L395 110 L388 115" stroke="currentColor" fill="none"/>
<text x="10" y="254" fill="currentColor" font-size="13">succ = 오른쪽 서브트리의 왼쪽 끝 (왼쪽 자식이 없다). child = succ 의 오른쪽 자식 35.</text>
<text x="10" y="273" fill="currentColor" font-size="13">빠지는 색 = succ 의 색(B). child 35 가 있으므로 그것을 검정으로 칠하면 끝, 복구 없음.</text>
<text x="10" y="292" fill="currentColor" font-size="13">succ 는 node 의 parent_color 워드를 통째로 물려받는다 → 부모와 색이 한 번에 옮겨진다.</text>
<text x="10" y="311" fill="currentColor" font-size="13">item_t 30 의 주소는 그대로. 다른 곳에서 들고 있던 포인터가 살아 있다 (키만 복사하면 깨진다).</text>
</svg>
```

삭제는 두 단계다.

**1단계: 실제로 사라지는 위치를 정한다.** 자식이 0~1개면 node의 자리다. 자식이 2개면 후계자(`succ`)의 원래 자리다. `succ`는 왼쪽 자식이 없으니 자식이 최대 하나다. 그리고 `succ`의 **색**은 빠지는 자리에서 사라지고, `succ` 노드 자체는 node의 `parent_color`를 물려받아 node의 색이 된다. 이 두 줄이 핵심이다.

```c
removed_black = rb_is_black(succ);          /* 사라지는 색은 후계자의 원래 색 */
...
succ->parent_color = node->parent_color;    /* 부모 + 색을 한 번에 복사 */
```

**왜 키를 복사하지 않나.** 교과서식 "후계자의 키를 node에 복사하고 후계자를 지운다"는 침입형에서 쓸 수 없다. 객체 자체가 `item_t`이고 노드는 그 안의 필드다. 키만 옮기면 다른 코드(DS-M01처럼 리스트에 같이 걸려 있거나 타이머 핸들을 쥔 코드)가 들고 있는 `item_t *`가 엉뚱한 키를 가리키게 된다. DS-M07에서 "복사가 아닌 재연결"이라고 한 것과 같은 이유다.

**2단계: 검정이 빠졌을 때만 복구한다.**

| 빠진 색 | 자리를 잇는 자식 | 처리 |
|---|---|---|
| 빨강 | 있거나 없음 | 복구 불필요 |
| 검정 | 있음 (반드시 빨강) | 자식을 검정으로 칠하고 끝 |
| 검정 | 없음 | `rb_erase_color(fix_parent)` |

리눅스의 `__rb_erase_augmented` 주석도 같은 사실을 쓴다("Note that if there is one child it must be red due to 5) and node must be black due to 4)"). "자식이 있으면 빨강"은 정리해 둘 만하다. 검정 자식 하나가 있다면 반대쪽 NIL 경로와 검정 수가 맞지 않기 때문이다.

```svg
<svg viewBox="0 0 740 634" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">삭제 복구 4 케이스 (x 가 왼쪽 자식인 경우)</text><circle cx="368" cy="14" r="7" class="f-warn" stroke="currentColor"/><text x="380" y="18" fill="currentColor" font-size="13">빨강</text><circle cx="460" cy="14" r="7" class="f-mut" stroke="currentColor"/><text x="472" y="18" fill="currentColor" font-size="13">검정</text><circle cx="552" cy="14" r="7" class="f-acc" stroke="currentColor"/><text x="564" y="18" fill="currentColor" font-size="13">색 모름</text><circle cx="644" cy="14" r="7" class="f-ok" stroke="currentColor"/><text x="656" y="18" fill="currentColor" font-size="13">이중 검정</text>
<text x="10" y="48" fill="currentColor" font-size="13" font-weight="bold">Case 1: 형제 빨강</text><line x1="330" y1="62" x2="284" y2="96" stroke="currentColor"/><circle cx="284" cy="96" r="18" class="f-ok" stroke="currentColor"/><text x="284" y="101" text-anchor="middle" fill="currentColor" font-size="13">x</text><line x1="330" y1="62" x2="376" y2="96" stroke="currentColor"/><line x1="376" y1="96" x2="350.7" y2="130" stroke="currentColor"/><circle cx="350.7" cy="130" r="18" class="f-mut" stroke="currentColor"/><text x="350.7" y="135" text-anchor="middle" fill="currentColor" font-size="13">cB</text><line x1="376" y1="96" x2="401.3" y2="130" stroke="currentColor"/><circle cx="401.3" cy="130" r="18" class="f-mut" stroke="currentColor"/><text x="401.3" y="135" text-anchor="middle" fill="currentColor" font-size="13">dB</text><circle cx="376" cy="96" r="18" class="f-warn" stroke="currentColor"/><text x="376" y="101" text-anchor="middle" fill="currentColor" font-size="13">sr</text><circle cx="330" cy="62" r="18" class="f-mut" stroke="currentColor"/><text x="330" y="67" text-anchor="middle" fill="currentColor" font-size="13">pB</text><line x1="570" y1="62" x2="524" y2="96" stroke="currentColor"/><line x1="524" y1="96" x2="498.7" y2="130" stroke="currentColor"/><circle cx="498.7" cy="130" r="18" class="f-ok" stroke="currentColor"/><text x="498.7" y="135" text-anchor="middle" fill="currentColor" font-size="13">x</text><line x1="524" y1="96" x2="549.3" y2="130" stroke="currentColor"/><circle cx="549.3" cy="130" r="18" class="f-mut" stroke="currentColor"/><text x="549.3" y="135" text-anchor="middle" fill="currentColor" font-size="13">cB</text><circle cx="524" cy="96" r="18" class="f-warn" stroke="currentColor"/><text x="524" y="101" text-anchor="middle" fill="currentColor" font-size="13">pr</text><line x1="570" y1="62" x2="616" y2="96" stroke="currentColor"/><circle cx="616" cy="96" r="18" class="f-mut" stroke="currentColor"/><text x="616" y="101" text-anchor="middle" fill="currentColor" font-size="13">dB</text><circle cx="570" cy="62" r="18" class="f-mut" stroke="currentColor"/><text x="570" y="67" text-anchor="middle" fill="currentColor" font-size="13">sB</text><path d="M430 84 L475 84 M468 79 L475 84 L468 89" stroke="currentColor" fill="none"/><text x="10" y="70" fill="currentColor" font-size="13">p 에서 왼쪽 회전,</text><text x="10" y="87" fill="currentColor" font-size="13">색 교환. 새 형제 c 는</text><text x="10" y="104" fill="currentColor" font-size="13">검정 → Case 2/3/4 로.</text><text x="10" y="121" fill="currentColor" font-size="13">여기서 p 가 빨강이 돼</text><text x="10" y="138" fill="currentColor" font-size="13">루프는 곧 끝난다.</text>
<text x="10" y="198" fill="currentColor" font-size="13" font-weight="bold">Case 2: 조카 둘 다 검정</text><line x1="330" y1="212" x2="284" y2="246" stroke="currentColor"/><circle cx="284" cy="246" r="18" class="f-ok" stroke="currentColor"/><text x="284" y="251" text-anchor="middle" fill="currentColor" font-size="13">x</text><line x1="330" y1="212" x2="376" y2="246" stroke="currentColor"/><line x1="376" y1="246" x2="350.7" y2="280" stroke="currentColor"/><circle cx="350.7" cy="280" r="18" class="f-mut" stroke="currentColor"/><text x="350.7" y="285" text-anchor="middle" fill="currentColor" font-size="13">cB</text><line x1="376" y1="246" x2="401.3" y2="280" stroke="currentColor"/><circle cx="401.3" cy="280" r="18" class="f-mut" stroke="currentColor"/><text x="401.3" y="285" text-anchor="middle" fill="currentColor" font-size="13">dB</text><circle cx="376" cy="246" r="18" class="f-mut" stroke="currentColor"/><text x="376" y="251" text-anchor="middle" fill="currentColor" font-size="13">sB</text><circle cx="330" cy="212" r="18" class="f-acc" stroke="currentColor"/><text x="330" y="217" text-anchor="middle" fill="currentColor" font-size="13">p?</text><line x1="570" y1="212" x2="524" y2="246" stroke="currentColor"/><circle cx="524" cy="246" r="18" class="f-mut" stroke="currentColor"/><text x="524" y="251" text-anchor="middle" fill="currentColor" font-size="13">xB</text><line x1="570" y1="212" x2="616" y2="246" stroke="currentColor"/><line x1="616" y1="246" x2="590.7" y2="280" stroke="currentColor"/><circle cx="590.7" cy="280" r="18" class="f-mut" stroke="currentColor"/><text x="590.7" y="285" text-anchor="middle" fill="currentColor" font-size="13">cB</text><line x1="616" y1="246" x2="641.3" y2="280" stroke="currentColor"/><circle cx="641.3" cy="280" r="18" class="f-mut" stroke="currentColor"/><text x="641.3" y="285" text-anchor="middle" fill="currentColor" font-size="13">dB</text><circle cx="616" cy="246" r="18" class="f-warn" stroke="currentColor"/><text x="616" y="251" text-anchor="middle" fill="currentColor" font-size="13">sr</text><circle cx="570" cy="212" r="18" class="f-acc" stroke="currentColor"/><text x="570" y="217" text-anchor="middle" fill="currentColor" font-size="13">p+</text><path d="M430 234 L475 234 M468 229 L475 234 L468 239" stroke="currentColor" fill="none"/><text x="10" y="220" fill="currentColor" font-size="13">s 를 빨강으로: 양쪽 다</text><text x="10" y="237" fill="currentColor" font-size="13">검정 하나 모자람 → 부족을</text><text x="10" y="254" fill="currentColor" font-size="13">p 로 올린다 (p+ 표시).</text><text x="10" y="271" fill="currentColor" font-size="13">p 가 빨강이면 검정으로</text><text x="10" y="288" fill="currentColor" font-size="13">칠하고 끝. 검정이면</text><text x="10" y="305" fill="currentColor" font-size="13">p 가 새 이중 검정.</text>
<text x="10" y="348" fill="currentColor" font-size="13" font-weight="bold">Case 3: 먼 조카 검정</text><line x1="330" y1="362" x2="284" y2="396" stroke="currentColor"/><circle cx="284" cy="396" r="18" class="f-ok" stroke="currentColor"/><text x="284" y="401" text-anchor="middle" fill="currentColor" font-size="13">x</text><line x1="330" y1="362" x2="376" y2="396" stroke="currentColor"/><line x1="376" y1="396" x2="350.7" y2="430" stroke="currentColor"/><circle cx="350.7" cy="430" r="18" class="f-warn" stroke="currentColor"/><text x="350.7" y="435" text-anchor="middle" fill="currentColor" font-size="13">cr</text><line x1="376" y1="396" x2="401.3" y2="430" stroke="currentColor"/><circle cx="401.3" cy="430" r="18" class="f-mut" stroke="currentColor"/><text x="401.3" y="435" text-anchor="middle" fill="currentColor" font-size="13">dB</text><circle cx="376" cy="396" r="18" class="f-mut" stroke="currentColor"/><text x="376" y="401" text-anchor="middle" fill="currentColor" font-size="13">sB</text><circle cx="330" cy="362" r="18" class="f-acc" stroke="currentColor"/><text x="330" y="367" text-anchor="middle" fill="currentColor" font-size="13">p?</text><line x1="570" y1="362" x2="524" y2="396" stroke="currentColor"/><circle cx="524" cy="396" r="18" class="f-ok" stroke="currentColor"/><text x="524" y="401" text-anchor="middle" fill="currentColor" font-size="13">x</text><line x1="570" y1="362" x2="616" y2="396" stroke="currentColor"/><line x1="616" y1="396" x2="641.3" y2="430" stroke="currentColor"/><line x1="641.3" y1="430" x2="655.2149999999999" y2="464" stroke="currentColor"/><circle cx="655.2149999999999" cy="464" r="18" class="f-mut" stroke="currentColor"/><text x="655.2149999999999" y="469" text-anchor="middle" fill="currentColor" font-size="13">dB</text><circle cx="641.3" cy="430" r="18" class="f-warn" stroke="currentColor"/><text x="641.3" y="435" text-anchor="middle" fill="currentColor" font-size="13">sr</text><circle cx="616" cy="396" r="18" class="f-mut" stroke="currentColor"/><text x="616" y="401" text-anchor="middle" fill="currentColor" font-size="13">cB</text><circle cx="570" cy="362" r="18" class="f-acc" stroke="currentColor"/><text x="570" y="367" text-anchor="middle" fill="currentColor" font-size="13">p?</text><path d="M430 384 L475 384 M468 379 L475 384 L468 389" stroke="currentColor" fill="none"/><text x="10" y="370" fill="currentColor" font-size="13">s 에서 오른쪽 회전,</text><text x="10" y="387" fill="currentColor" font-size="13">c 검정 · s 빨강.</text><text x="10" y="404" fill="currentColor" font-size="13">새 형제 c 의 먼 조카가</text><text x="10" y="421" fill="currentColor" font-size="13">빨강 → Case 4.</text>
<text x="10" y="498" fill="currentColor" font-size="13" font-weight="bold">Case 4: 먼 조카 빨강</text><line x1="330" y1="512" x2="284" y2="546" stroke="currentColor"/><circle cx="284" cy="546" r="18" class="f-ok" stroke="currentColor"/><text x="284" y="551" text-anchor="middle" fill="currentColor" font-size="13">x</text><line x1="330" y1="512" x2="376" y2="546" stroke="currentColor"/><line x1="376" y1="546" x2="350.7" y2="580" stroke="currentColor"/><text x="350.7" y="585" text-anchor="middle" fill="currentColor" font-size="13">c</text><line x1="376" y1="546" x2="401.3" y2="580" stroke="currentColor"/><circle cx="401.3" cy="580" r="18" class="f-warn" stroke="currentColor"/><text x="401.3" y="585" text-anchor="middle" fill="currentColor" font-size="13">dr</text><circle cx="376" cy="546" r="18" class="f-mut" stroke="currentColor"/><text x="376" y="551" text-anchor="middle" fill="currentColor" font-size="13">sB</text><circle cx="330" cy="512" r="18" class="f-acc" stroke="currentColor"/><text x="330" y="517" text-anchor="middle" fill="currentColor" font-size="13">p?</text><line x1="570" y1="512" x2="524" y2="546" stroke="currentColor"/><line x1="524" y1="546" x2="498.7" y2="580" stroke="currentColor"/><circle cx="498.7" cy="580" r="18" class="f-mut" stroke="currentColor"/><text x="498.7" y="585" text-anchor="middle" fill="currentColor" font-size="13">xB</text><line x1="524" y1="546" x2="549.3" y2="580" stroke="currentColor"/><text x="549.3" y="585" text-anchor="middle" fill="currentColor" font-size="13">c</text><circle cx="524" cy="546" r="18" class="f-mut" stroke="currentColor"/><text x="524" y="551" text-anchor="middle" fill="currentColor" font-size="13">pB</text><line x1="570" y1="512" x2="616" y2="546" stroke="currentColor"/><circle cx="616" cy="546" r="18" class="f-mut" stroke="currentColor"/><text x="616" y="551" text-anchor="middle" fill="currentColor" font-size="13">dB</text><circle cx="570" cy="512" r="18" class="f-acc" stroke="currentColor"/><text x="570" y="517" text-anchor="middle" fill="currentColor" font-size="13">s?</text><path d="M430 534 L475 534 M468 529 L475 534 L468 539" stroke="currentColor" fill="none"/><text x="10" y="520" fill="currentColor" font-size="13">p 에서 왼쪽 회전.</text><text x="10" y="537" fill="currentColor" font-size="13">s 는 p 의 색을 물려받고,</text><text x="10" y="554" fill="currentColor" font-size="13">p 와 d 는 검정. 끝.</text>
</svg>
```

**네 케이스의 흐름.** "x 쪽은 검정이 하나 모자라다"(이중 검정)를 고치는 루프다.

| 케이스 | 조건 | 처리 | 회전 | 다음 |
|---|---|---|---|---|
| 1 | 형제 빨강 | 형제 검정, 부모 빨강, 부모에서 회전 | 1 | 새 형제(검정)로 Case 2/3/4 |
| 2 | 형제 검정, 조카 둘 다 검정 | 형제 빨강. 부모 빨강이면 검정으로 칠하고 끝. 아니면 부모가 새 이중 검정 | 0 | 끝 또는 위로 |
| 3 | 형제 검정, 먼 조카 검정 | 가까운 조카 검정, 형제 빨강, 형제에서 회전 | 1 | Case 4 |
| 4 | 형제 검정, 먼 조카 빨강 | 형제 = 부모 색, 부모 검정, 먼 조카 검정, 부모에서 회전 | 1 | 끝 |

**회전이 3번을 넘지 않는 이유.** Case 2만 위로 올라가며 회전이 없다. 회전은 Case 1, 3, 4에서만 일어나고, Case 1 뒤에는 부모가 빨강이므로 그 반복 안에서 끝난다(Case 2는 부모 빨강으로 멈춤, Case 3/4는 끝). 그래서 최대 경로는 Case 1 → Case 3 → Case 4로 회전 3번이다.

**NULL 판별**의 이중 의미(`node != sibling`)는 힌트 5와 같다.

```text
검증 결과 (solution.c, 실제 출력)
INFO insert cases L: 387145/194378/380927  R: 394345/191140/389870
INFO erase  cases L: 45073/288063/70601/143594  R: 70832/493427/97341/234553
INFO max rotations per op: insert 2, erase 3 (small n) / insert 2, erase 3 (1M ops)
INFO timer model: tick counter wrapped 1 time(s)
80/80 passed
```

모든 케이스가 좌우 양쪽으로 실행됐다. 전수 테스트(삽입 순열 720 × 삭제 순열 720 = 518,400 시퀀스)와 랜덤 테스트에서 연산당 회전 상한도 체크로 고정돼 있다. 삭제 카운트는 R 쪽이 L보다 많은데, 자식 2개 삭제가 항상 오른쪽 서브트리에서 후계자를 가져오기 때문으로 보인다(따로 실험하지는 않았다).

**검증이 정말 버그를 잡는가.** private 복사본에 버그를 넣어 돌렸다. 넣은 버그 19개 중 18개를 잡았다.

| 넣은 버그 | 결과 |
|---|---|
| 회전에서 B의 부모 갱신 누락 / Case 2 교환 누락 / 루트 재칠 누락 | 잡음 (FAIL 또는 ASan) |
| Case 3 색을 반대로 / Case 4 부모 색 미상속 | 잡음 |
| Case 2 부모 빨강에서 안 멈춤 / Case 3 조카 판정 반대 | 잡음 |
| fix_parent 잘못 / 후계자 색 미상속 / 자식 재칠 누락 | 잡음 |
| `rb_set_parent`가 색을 잃음 / `rb_next` 방향 반대 / 검사기를 부모-자식 비교로 | 잡음 |
| 캐시 미갱신 / `a < b` 시각 비교 / 동률 LIFO / `RB_CLEAR_NODE` 누락 | 잡음 |
| `rb_next`를 `rb_erase` **뒤에** 호출 (`RB_CLEAR_NODE` 없이) | **통과** — 7장 참고 |
| `rb_erase` 뒤 `RB_CLEAR_NODE` 후 `rb_next` | 잡음 |

### RB vs AVL: 측정

`demo_rb_vs_avl.c` 실제 출력이다. AVL은 DS-M07의 재귀 알고리즘이다(부모 포인터 없음, 노드에 height 저장). `meta`는 실제로 값이 바뀐 메타데이터 수다. RB는 색 변경, AVL은 height 변경이고, 조기 종료하는 반복 구현이 쓰게 될 양과 같다.

```text
sizeof: RB node+key = 32, AVL node = 24 (bytes, this 64-bit host)
tree pat           n | hgt  depth  bound | ins: rot/op max meta/op max | del: rot/op max meta/op max
RB  random     1000 |  12   9.24   19.9 | 0.574  2  2.264  16 | 0.352  3  1.691   7
AVL random     1000 |  12   9.18   14.0 | 0.669  2  3.087  11 | 0.363  4  1.198  11
RB  sorted     1000 |  17   9.41   19.9 | 0.983  1  4.909  25 | 0.492  1  2.937  10
AVL sorted     1000 |  10   8.99   14.0 | 0.990  1  3.964  11 | 0.491  1  1.465   8
RB  random  1000000 |  24  19.41   39.9 | 0.582  2  2.318  35 | 0.379  3  1.768  14
AVL random  1000000 |  24  19.40   28.4 | 0.699  2  3.184  23 | 0.373  8  1.241  24
RB  sorted  1000000 |  37  19.33   39.9 | 1.000  1  5.000  55 | 0.500  1  3.000  20
AVL sorted  1000000 |  20  18.95   28.4 | 1.000  1  4.000  21 | 0.500  1  1.500  18
```

읽는 법(10k, 100k 행도 같은 추세라 생략했다):

- **높이**: 랜덤에서는 RB와 AVL이 같다(24 vs 24). **정렬 입력에서 차이가 난다**. RB 37 vs AVL 20이고, RB는 상한 39.9에 가깝다. 타임스탬프처럼 단조 증가하는 키는 펌웨어에서 흔하다.
- **평균 깊이**는 정렬 입력에서도 거의 같다(19.33 vs 18.95). 높이를 만드는 긴 척추가 소수 경로이기 때문이다. 즉 **최악의 검색은 RB가 1.85배 깊지만 평균 검색은 거의 같다**.
- **회전 상한**: RB 삭제는 항상 ≤ 3이다. AVL 삭제는 관측 최대가 n과 함께 커진다(1000: 4, 1만: 6, 10만: 7, 100만: 8회). 이것이 RB의 진짜 장점이고, 리눅스 문서가 "faster real-time bounded worst case performance for insertion and deletion"이라고 쓴 이유다.
- **그러나 색 변경은 RB도 O(log n)이다.** 삽입 한 번의 색 변경 최대는 RB 55(정렬), AVL height 변경 최대는 21이다. "회전 O(1)"은 "작업량 O(1)"이 아니다.

벌크 시간(같은 프로그램, `-O2`, Apple M6, 5회 중앙값 [최소, 최대], 키마다 재지 않음. 타이머 눈금이 41.7 ns라서다):

```text
n=100000 (ns/op, median of 5 runs [min, max]):
  RB  insert                 86.2  [   66.0,   109.7]
  AVL insert                155.2  [  140.3,   197.3]
  RB  search                112.8  [   96.2,   118.0]
  AVL search                112.3  [   98.6,   130.5]
  RB  erase(ptr, no find)    27.7  [   22.9,    34.5]
  RB  find + erase          121.2  [  101.9,   145.5]
  AVL erase(by key)         138.0  [  133.1,   159.9]
n=1000000 (ns/op, median of 5 runs [min, max]):
  RB  insert                250.7  [  191.2,   279.3]
  AVL insert                330.6  [  309.9,   433.7]
  RB  search                369.3  [  340.6,   439.5]
  AVL search                362.1  [  325.2,   385.4]
  RB  erase(ptr, no find)    65.5  [   51.6,    69.2]
  RB  find + erase          351.5  [  301.8,   359.7]
  AVL erase(by key)         396.2  [  394.0,   421.4]
```

- **검색은 구별되지 않는다.** 범위가 겹친다. 평균 깊이가 같으니 그럴 만하다.
- **`RB erase(ptr)` 27.7 ns를 AVL 138 ns와 비교하면 안 된다.** RB 쪽은 검색이 없다. 공정한 비교는 `RB find + erase`다(121 vs 138 ns, 100k에서는 범위가 겹치고 100만에서는 안 겹친다). 27.7 ns는 **침입형의 이득**이다. 객체 포인터를 이미 쥐고 있으면 검색 비용이 사라진다.
- 삽입이 AVL보다 빠른 이유는 이 구현 차이가 섞여 있다. AVL 쪽은 재귀로 경로 전체의 height를 다시 계산한다. 이 표는 "RB가 AVL보다 빠르다"가 아니라 "이 두 구현에서는 이렇다"로 읽어야 한다. 알고리즘 비교는 위의 작업량 표가 더 믿을 만하다.
- 측정 중 다른 프로세스가 돌고 있었고 macOS에서는 스레드를 코어에 고정할 수 없다. 첫 실행에서는 100만 행의 범위가 2배 넘게 벌어지기도 했다. 표의 수치는 비율만 참고하자.

### Part C — 타임아웃 큐

```svg
<svg viewBox="0 0 740 244" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">타임아웃 큐: 트리는 정렬 상태를, 캐시는 "다음 만료" 를 맡는다</text>
<line x1="420" y1="66" x2="310" y2="118" stroke="currentColor"/><line x1="310" y1="118" x2="249.5" y2="170" stroke="currentColor"/><circle cx="249.5" cy="170" r="18" class="f-mut" stroke="currentColor"/><text x="249.5" y="175" text-anchor="middle" fill="currentColor" font-size="13">120</text><line x1="310" y1="118" x2="370.5" y2="170" stroke="currentColor"/><circle cx="370.5" cy="170" r="18" class="f-mut" stroke="currentColor"/><text x="370.5" y="175" text-anchor="middle" fill="currentColor" font-size="13">250</text><circle cx="310" cy="118" r="18" class="f-warn" stroke="currentColor"/><text x="310" y="123" text-anchor="middle" fill="currentColor" font-size="13">200</text><line x1="420" y1="66" x2="530" y2="118" stroke="currentColor"/><line x1="530" y1="118" x2="590.5" y2="170" stroke="currentColor"/><circle cx="590.5" cy="170" r="18" class="f-warn" stroke="currentColor"/><text x="590.5" y="175" text-anchor="middle" fill="currentColor" font-size="13">500</text><circle cx="530" cy="118" r="18" class="f-mut" stroke="currentColor"/><text x="530" y="123" text-anchor="middle" fill="currentColor" font-size="13">400</text><circle cx="420" cy="66" r="18" class="f-mut" stroke="currentColor"/><text x="420" y="71" text-anchor="middle" fill="currentColor" font-size="13">300</text>
<rect x="20" y="44" width="170" height="32" class="f-acc" stroke="currentColor"/><text x="30" y="65" fill="currentColor" font-family="monospace">rb_leftmost</text>
<path d="M190 60 C 232 72, 226 138, 235 158" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>




<text x="20" y="108" fill="currentColor" font-size="13">next_deadline: O(1)</text>
<text x="20" y="128" fill="currentColor" font-size="13">add / tick / cancel: O(log n)</text>
<text x="20" y="148" fill="currentColor" font-size="13">cancel 은 검색 없이</text>
<text x="20" y="168" fill="currentColor" font-size="13">노드 포인터로 바로 erase</text>
<text x="20" y="212" fill="currentColor" font-size="13">시각 비교: (int32_t)(a - b) &lt; 0. 활성 타이머끼리 거리가 2^31 미만이어야 전순서가 된다.</text>
<text x="20" y="232" fill="currentColor" font-size="13">그래서 tq_add 는 now 기준 ±2^30 안의 만료 시각만 받는다.</text>
</svg>
```

타이머는 항상 "가장 이른 것"을 물어보는 구조다. `rb_first`는 O(log n)이니 캐시를 둔다. 리눅스의 `struct rb_root_cached`가 정확히 이것이다(`rb_root`에 `rb_leftmost` 포인터 하나를 추가했다). 리눅스의 범용 타이머 큐 `lib/timerqueue.c`도 `rb_add_cached`와 `rb_erase_cached`로 만들어져 있고, 노드를 넣기 전에 `RB_EMPTY_NODE`로 이중 등록을 검사한다.

```c
static void rb_erase_cached(struct rb_node *node, struct rb_root_cached *root) {
    if (root->rb_leftmost == node) root->rb_leftmost = rb_next(node);   /* 지우기 전에 다음을 구한다 */
    rb_erase(node, &root->rb_root);
}
```

**leftmost 판단**은 삽입 경로에서 공짜로 얻는다. 내려가며 한 번이라도 오른쪽으로 갔으면 leftmost가 아니다. 추가 비교가 없다.

**동률 FIFO.** 같은 시각이면 오른쪽으로 내려간다. 그러면 in-order 순서가 등록 순서가 되고, 회전은 in-order를 보존하므로 항상 유지된다. 리스트 기반 RTOS에서 쓰는 동작과 같다(FreeRTOS `vListInsert`도 같은 값이면 뒤에 넣는다). 동률을 금지하는 구현도 있다. Zephyr rbtree 문서는 `lessthan_fn`에 "equal"을 허용하지 않는다고 적고 있으니, 거기서 타이머를 쓰려면 키에 일련번호를 붙여야 한다.

**랩어라운드.** `(int32_t)(a - b) < 0`은 a와 b의 거리가 2³¹ 미만일 때만 뜻이 있다. 거리가 그보다 크면 전순서(순서를 매기는 관계)가 아니게 되고, 트리 전체가 깨진다. 그래서 `tq_add`는 now 기준 ±2³⁰ 안의 만료 시각만 받는다. 활성 타이머 어느 쌍을 골라도 거리가 2³¹ 미만이다. 만료된 뒤 처리가 늦어지는 경우(backlog)를 위한 창도 여기 포함된다. 범위를 벗어난 값의 `(int32_t)` 변환은 C 표준상 구현 정의다(C11 6.3.1.3). gcc 문서는 2^N 모듈러 축소로 명시하고, 이 맥의 clang에서도 랩어라운드 체크가 통과한다.

**콜백 안전성.** `tq_tick`은 매 반복에서 `rb_leftmost`를 다시 읽는다. 콜백이 다른 타이머를 취소하거나 새 타이머를 넣어도 다음 반복의 전제가 깨지지 않는다. 주기 타이머는 **콜백 전에** 재등록한다. 그래야 콜백이 자기 자신을 cancel할 수 있다.

**budget.** 한 tick에서 발화할 수를 제한한다. 주기 타이머가 now보다 뒤처졌을 때 `tick` 하나가 끝없이 돌며 ISR이나 스케줄러를 붙잡는 것을 막는다. 남은 건 다음 tick이 처리한다(체크: `tq_tick(q, 95, 3, ...)` → 3회, 다음 호출 → 6회).

**펌웨어 관점에서 정렬 리스트와 비교.** 작은 RTOS의 지연 목록은 정렬 연결 리스트로 충분한 경우가 많다. FreeRTOS `vListInsert`는 정렬 삽입을 위해 리스트 앞에서부터 차례로 걷는다. 그러면 언제 트리가 이득일까? `demo_timerq_vs_list.c` 실제 출력(정상 상태 n개, "맨 앞 꺼내기 + 재등록" 1쌍, 만료 시각 균등 분포):

```text
sizeof: RB timer 32, list timer 24 bytes (64-bit)
    n | RB cmp/add  max | list cmp/add   max |  RB ns/pair [min,max]   | list ns/pair [min,max]
    4 |     2.0     2 |      2.5     3 |    16.1 [  13.9,  19.0] |      8.6 [    7.3,   14.2]
    8 |     3.2     4 |      5.3     7 |    21.7 [  17.6,  33.6] |     11.1 [   10.0,   16.7]
   16 |     4.3     6 |     10.8    15 |    32.9 [  28.5,  55.1] |     19.6 [   15.9,   31.0]
   32 |     5.4     7 |     21.5    31 |    48.1 [  40.1,  58.8] |     27.5 [   25.3,   31.1]
   64 |     6.5     8 |     42.9    63 |    49.3 [  47.6,  58.4] |     57.1 [   50.9,   68.1]
  128 |     7.5    10 |     85.6   127 |    56.8 [  45.1,  57.9] |     93.5 [   87.2,  102.9]
  256 |     8.6    11 |    170.9   255 |    66.5 [  59.3,  74.9] |    186.7 [  167.4,  220.6]
 1024 |    10.6    13 |    682.9  1023 |    67.3 [  59.2,  70.7] |    696.9 [  675.6,  720.5]
 4096 |    12.7    16 |   2729.5  4095 |    88.8 [  85.2, 211.1] |   6513.7 [ 5972.5, 7621.3]
```

- 비교 수만 보면 n=8에서 이미 트리가 적지만(3.2 vs 5.3), **시간은 n=32까지 리스트가 빠르다**. 트리는 비교 외에 재균형 비용을 낸다. n=64에서 범위가 겹치고, 128 이상에서 트리가 뚜렷이 앞선다.
- **이 교차점은 이 맥의 값이다.** 교차점은 캐시, 분기 예측, 코어에 따라 움직인다. D-cache가 없는 Cortex-M0/M3 계열에서는 포인터 추적 비용이 다르니 타깃에서 재야 한다.
- **비교 수의 최악**은 플랫폼과 무관한 숫자다. 리스트는 n−1번(인터럽트를 막은 임계 구역이면 그대로 지연이다), 트리는 높이 이하다.
- 입력 분포가 다르면 결론도 달라진다. 대부분 가까운 시각에 만료되는 타이머라면 리스트는 앞쪽에서 삽입 위치를 찾으니 측정값보다 유리하다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 80/80 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o san && ./san   # 80/80, 리포트 없음
```

## 7. 자주 하는 실수

1. **`rb_set_parent`가 색을 지운다.** `n->parent_color = (uintptr_t)p;` — 회전에서 쓰면 형제의 색이 빨강으로 바뀐다(빨강 = 0). 고친 코드는 `(uintptr_t)p | (n->parent_color & RB_COLOR_MASK)`. 실제로 넣어 보면 고정 트리 시험과 ASan이 함께 잡는다.
2. **Case 4에서 부모 색을 물려받지 않는다.** 형제는 `rb_color(parent)`를 받아야 한다. 무조건 검정으로 칠하면 p가 빨강이었던 경우 검정 높이가 깨진다. 고정 트리 `(((10B)20r(30B(40r)))50B(70B))`가 이 경우를 잡는다.
3. **Case 2 뒤에 n과 p를 안 바꾼다.** 회전으로 n이 위로 갔는데 그대로 Case 3을 부르면 p와 n의 색·회전 대상이 어긋난다.
4. **후계자 키를 복사한다.** 트리 모양은 맞는데 객체 동일성이 깨진다. `t_erase`의 동일성 검사가 잡는다. 실제 코드에서는 타이머 핸들이 다른 객체를 가리키게 되는 버그다.
5. **`fix_parent`를 잘못 잡는다.** 후계자가 node의 오른쪽 자식이면 복구가 시작될 위치의 부모는 node가 아니라 **후계자 자신**이다. node를 그대로 쓰면 이미 트리에서 빠진 노드를 따라 복구한다.
6. **순서 검사를 부모-자식 비교로 끝낸다.** `((10B(25r))20B(30B))`는 지나간다. in-order로 직전 키와 비교해야 한다.
7. **시각을 `a < b`로 비교한다.** 랩어라운드 시각에서 만료 순서가 뒤집힌다.
8. **캐시 갱신을 `rb_erase` 뒤에 한다.** 이 실수는 흥미롭다. 뮤턴트를 넣어 보니 **테스트를 통과했다**. `rb_erase`가 지워진 노드의 `rb_right`와 부모 필드를 건드리지 않아서 `rb_next`가 우연히 맞게 동작한다. 그런데 지운 뒤 `RB_CLEAR_NODE`를 먼저 부르면 NULL이 나오고 잡힌다. 지운 노드의 필드에 기대는 코드는 디버그 빌드에서 노드를 포이즈닝하는 순간 깨진다. "테스트가 통과했다"가 "맞다"는 아니다.
9. **컨테이너를 `__packed`로 만든다.** 노드 주소의 비트 0이 1일 수 있고, 그러면 `rb_parent`가 틀린 주소를 돌려준다. `RB_CONTAINER_OK`로 컴파일 시점에 거절한다(연습 6). 프로토콜 헤더와 같은 구조체에 트리 노드를 넣지 않는 것이 습관으로 더 낫다.
10. **`RB_EMPTY_NODE` 없이 취소를 짠다.** 한 번 더 취소하면 지워진 노드를 다시 지운다. `RB_CLEAR_NODE`로 표시하고 검사하면 여러 번 불러도 안전하다.

## 8. 면접 Q&A

<details><summary>Q1. 색을 포인터 비트에 넣는 대신 별도 필드를 두면 뭐가 나쁜가요?</summary>

필드 하나가 Cortex-M에서 패딩 포함 4바이트, 64비트에서 8바이트를 쓴다. 노드 12 B가 16 B로 33% 커진다. 트리가 큰 임베디드에서는 의미 있다. 대가는 ① 정렬 가정, ② 모든 부모 접근에 마스크, ③ 부모 쓰기가 read-modify-write가 된다는 것이다. ②는 실측한 대로 `bic` 한 명령이다. ③은 ISR과 공유하면 문제가 되지만, 트리는 어차피 임계 구역 안에서만 만져야 하므로 별도의 비용은 없다. 꼬리질문 "더 줄일 수 있나?": Zephyr rbtree 문서에 따르면 노드는 포인터 두 개뿐이고 부모 포인터가 없다. 내려가면서 스택에 경로를 쌓아 올라갈 길을 얻는다. 노드 RAM을 스택 사용(로그 크기)과 맞바꾼 설계다.
</details>

<details><summary>Q2. "삽입은 회전 2번, 삭제는 3번이니 O(1)"이라고 말해도 되나요?</summary>

**회전 수에 한해서만** 맞다. 위치를 찾는 데 O(log n), Case 1과 Case 2 반복이 O(log n)이다. 측정에서 삽입 한 번의 색 변경은 최대 55회였다(100만 개, 정렬 입력). 임계 구역 길이를 재는 사람에게 O(1)이라고 답하면 틀린다. 정확한 답은 "구조 변경은 O(1)이고 작업량은 O(log n)이며, 높이로 상한이 잡힌다"이다.
</details>

<details><summary>Q3. RB와 AVL 중 어떻게 고르나요?</summary>

측정 기준으로 답한다. 랜덤 입력의 높이와 평균 깊이는 같았고 검색 시간도 구별되지 않았다. 차이는 두 군데다. ① 정렬 입력에서 RB 높이가 AVL보다 1.85배 커서(37 vs 20) **최악의 검색 경로**가 길다. ② RB 삭제 회전은 ≤ 3으로 묶이지만 AVL은 O(log n)이고 이번 측정에서 최대 8회였다. 회전은 메모리 쓰기가 몰리는 작업이니, 쓰기가 많거나 동시 접근 비용이 크면 RB, 최악 검색 깊이가 중요한 읽기 위주 테이블이면 AVL이다. 그리고 노드 크기는 구현 선택이다. 우리 AVL은 부모 포인터가 없어서 더 작았다(24 vs 32 B). 알고리즘이 아닌 구현 차이다.
</details>

<details><summary>Q4. 왜 삭제에서 키를 복사하면 안 되나요?</summary>

침입형에서는 노드가 객체의 일부이기 때문이다. 복사는 "어느 객체가 어느 키를 가지는가"를 바꾼다. 다른 곳이 쥐고 있는 포인터(타이머 핸들, 리스트 연결, DMA 디스크립터의 back pointer)가 조용히 다른 데이터를 가리키게 된다. 재연결은 객체 주소를 그대로 둔다. 비용은 상수개 포인터 쓰기다.
</details>

<details><summary>Q5. 왜 취소가 "검색 없이" O(log n)인가요? 리스트 취소는 O(1)인데요?</summary>

객체 안에 노드가 있으니 `rb_erase`에 노드 포인터를 바로 넘긴다. 탐색 단계가 없다. 그래도 O(log n)인 이유는 복구가 위로 올라갈 수 있어서다(Case 2 반복). 이중 연결 리스트의 O(1) 제거보다 느리지만, 리스트는 정렬 삽입이 O(n)이다. 측정에서 `RB erase(ptr)`는 find+erase보다 4배 이상 빨랐다(27.7 vs 121 ns, n=10만).
</details>

<details><summary>Q6. ISR에서 이 타이머를 쓸 수 있나요?</summary>

그대로는 안 된다. 회전 한 번이 여러 필드를 고치고, 그 사이의 트리는 불변식이 깨져 있다. 태스크가 회전 중일 때 ISR이 들어와 같은 트리를 만지면 끝이다. 방법은 ① 트리 조작 전체를 인터럽트 금지 또는 스케줄러 잠금으로 감싼다(임계 구역 길이 = 높이에 비례. 그래서 n과 높이를 재어 둔다), ② ISR은 lock-free 링으로 요청만 넘기고(DS-A01, DS-A02) 태스크가 트리를 처리한다. 리눅스 `lib/rbtree.c` 주석에도 "tree rotations are not atomic so a lookup might"로 시작하는 경고가 있고, 락 없는 조회를 쓰려면 seqcount 같은 추가 장치가 필요하다. 리눅스 문서는 "Locking is also left up to the user"라고 명시한다.
</details>

<details><summary>Q7. 왜 리눅스는 "비교 콜백"이 아니라 사용자가 직접 삽입 루프를 쓰게 하나요?</summary>

문서에 따르면 "users are expected to write their own tree search and insert functions which call the provided rbtree functions"이다. 비교 콜백을 없애면 비교가 인라인되고 키 타입이 자유롭다. 우리 `item_insert`와 `tq_add`처럼 사용자 코드가 `struct rb_node **link`로 내려가고, 트리 코어(`rb_insert_color`)는 키를 아예 모른다. 그래서 라이브러리는 키 타입별로 복제되지 않는다. 대가는 호출마다 짧은 루프를 쓴다는 것이고, Zephyr는 반대로 `lessthan_fn` 콜백을 받는다. 어느 쪽이든 트레이드오프다.
</details>

<details><summary>Q8. 이 코드는 어떻게 믿나요? 테스트 전략을 설명해 보세요.</summary>

네 겹이다. ① 고정 트리 + 정확한 결과 문자열로 케이스별 동작 검증 ② 검사기 음성 테스트(위반을 넣어 enum 값까지 확인) ③ 전수 순열로 작은 n의 모든 경로 ④ 참조 모델 대조 랜덤 100만 회, 카운터로 "실제로 모든 케이스가 양 방향으로 실행됐는가"까지 체크. 마지막으로 뮤턴트로 테스트 자체를 시험했다. 19개 중 18개를 잡았고, 살아남은 하나(7장 8번)는 구현에 기댄 코드라는 걸 알려 주었다.
</details>

<details><summary>Q9. 펌웨어에서 이 구조를 안 쓰는 게 나은 경우는요?</summary>

n이 수십 이하이고 삽입 빈도가 낮으면 정렬 리스트가 더 빠르고 단순하다(이 맥 측정에서 n=32까지 리스트 승). 키 범위가 작고 고정이면 타이머 휠(DS-M06, 지연 리스트 비교는 RTOS-M07)이나 우선순위 비트맵(RTOS-B04)이 O(1)로 이긴다. 플래시 인덱스처럼 한 번에 페이지를 읽어야 한다면 B+ 트리(DS-A08)가 맞다. 그리고 RAM이 빠듯하면 포인터 3개/노드는 크다. Zephyr처럼 2포인터 구조나 인덱스 기반 풀로 줄인다.
</details>

## 9. 추가 연습

### 연습 1 — 가장 키 큰 RB 트리

검정 높이 bh에서 높이 2·bh를 만드는 최소 트리를 직접 구성하라. 노드 수는 얼마인가?

<details><summary>답</summary>

왼쪽 척추를 검정·빨강 교대로 두고, 나머지 자리에 최소 노드(완전 검정 트리)를 둔다. T(b) = T(b−1) + 2^b, T(0) = 0이므로 **n = 2^(b+1) − 2**, 높이 2b다. 즉 h = 2·log₂(n+2) − 2로 상한 2·log₂(n+1)에 거의 닿는다. 실제 출력: `bh=10 n=2046 height=20`. 검사기가 유효한 트리로 인정한다. 다만 삽입·삭제 순서로 이 트리에 도달할 수 있는지는 별개 문제이고, 여기서는 트리를 직접 구성했다.
</details>

### 연습 2 — `rb_replace_node`

같은 키의 새 객체로 노드를 교체하라. 재균형 없이 O(1)이어야 한다.

<details><summary>답</summary>

노드 3워드를 그대로 복사하고(`*nw = *victim`), 양쪽 자식의 부모와 부모의 자식 링크만 고친다. 트리 모양과 색이 그대로라 불변식은 건드릴 일이 없다. **키가 같아야 한다**는 전제를 지키지 않으면 순서가 깨진다. 객체 교체 없이 업데이트하는 패턴에 쓴다.
</details>

### 연습 3 — 후위 순회로 전체 해제

`rb_first_postorder` / `rb_next_postorder`를 구현하고, 방문 중 노드를 망가뜨려도(해제 흉내) 순회가 안전함을 보여라.

<details><summary>답</summary>

후위 순회는 자식을 먼저 방문하므로 재균형 없이 전부 해제할 수 있고, `rb_erase`를 n번 부르는 것보다 낫다. 현재 노드를 지우기 **전에** 다음 노드를 구해야 한다. 다음을 구할 때 읽는 것은 현재 노드의 부모 필드와 **아직 안 지운** 부모의 링크뿐이다. 리눅스에도 `rb_first_postorder`와 `rb_next_postorder`가 있다.
</details>

### 연습 4 — 한 객체, 두 트리

연결 객체 하나를 id 트리와 만료 시각 트리에 동시에 넣어라.

<details><summary>답</summary>

노드를 두 개(`by_id`, `by_deadline`) 넣고 `container_of`에서 멤버 이름만 다르게 쓴다. 침입형의 장점이 그대로 드러나는 예다. 두 트리는 서로의 링크를 건드리지 않는다. 만료 키에 동률이 있으면 id를 섞어 전순서를 만들어야 검사기가 통과한다.
</details>

### 연습 5 — lower_bound와 구간 순회

`key` 이상의 첫 노드를 찾고 `rb_next`로 구간을 걸어라. 전체 순회의 포인터 이동 수도 세어 보라.

<details><summary>답</summary>

내려가며 "key 이상인 후보"를 기억한다. 전체 in-order는 간선을 많아야 두 번 지나므로 이동 수 ≤ 2(n−1), 즉 `rb_next`는 **상각** O(1)이다(최악 한 번은 O(log n)). 실제 출력: `n=1805 full in-order walk: 3597 pointer hops, 2(n-1)=3608`.
</details>

### 연습 6 — packed 컨테이너 컴파일 시점 거절

노드의 정렬 가정이 깨지는 컨테이너를 컴파일 시점에 거절하는 매크로를 만들어라.

<details><summary>답</summary>

`RB_CONTAINER_OK(T, m)` = `_Alignof(T) >= _Alignof(struct rb_node) && offsetof(T, m) % _Alignof(struct rb_node) == 0`. 구조체의 정렬과 멤버의 오프셋 둘 다 확인해야 한다. `struct __attribute__((packed)) { char tag; struct rb_node node; }`은 오프셋 1, 정렬 1이라 거절된다. 런타임에 실제로 터뜨리는 것은 UB라서 일부러 시험하지 않았다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 16/16 passed
```

실제 출력 일부:

```text
bh= 1  n=    2  height= 2
bh= 2  n=    6  height= 4
bh= 4  n=   30  height= 8
bh= 7  n=  254  height=14
bh=10  n= 2046  height=20
n=1805 full in-order walk: 3597 pointer hops, 2(n-1)=3608
16/16 passed
```

## 10. 핵심 정리

- 색은 부모 포인터 비트 0에 넣는다. 노드 정렬 ≥ 2가 전제이고 `_Static_assert`로 적어 둔다. `packed` 컨테이너는 이 가정을 깬다. 빨강 = 0이라 `rb_link_node`가 한 줄이다.
- 삽입: 삼촌 빨강 → 색 뒤집고 위로, 삼촌 검정 → 회전으로 끝(꺾임은 먼저 펴기). 회전 ≤ 2.
- 삭제: "사라지는 색"이 검정이고 잇는 자식이 없을 때만 복구한다. 형제 빨강 → 회전, 조카 둘 다 검정 → 위로, 먼 조카 검정 → 형제 회전, 먼 조카 빨강 → 부모 회전 후 끝. 회전 ≤ 3.
- 자식 2개 삭제는 키 복사 금지. 후계자 노드를 재연결한다.
- 회전 O(1)은 작업량 O(1)이 아니다. 색 변경과 경로 탐색은 O(log n)이고, 측정 최대 색 변경은 55회였다.
- 측정: 랜덤에서 RB·AVL 높이와 검색은 같다. 정렬 입력에서 RB 높이 37 vs AVL 20. RB 삭제 회전 ≤ 3, AVL은 최대 8회 관측.
- 타이머: 캐시로 다음 만료 O(1), 같은 시각은 오른쪽으로 FIFO, 시각 비교는 `(int32_t)(a - b) < 0`이고 창은 ±2³⁰. 이 맥에서 n ≤ 32는 정렬 리스트가 더 빠르다.
- 검증은 고정 트리 + 음성 테스트 + 전수 + 모델 대조 + 뮤턴트까지 해야 "케이스를 다 지났다"고 말할 수 있다.

## 참고자료

- Linux Kernel, *Red-black Trees (rbtree) in Linux*, https://docs.kernel.org/core-api/rbtree.html — 사용자가 직접 쓰는 삽입 루프, cached rbtree, "real-time bounded worst case" 문구
- Linux 소스: `lib/rbtree.c` (`__rb_insert`, `____rb_erase_color` 케이스 주석), `include/linux/rbtree_types.h` (`struct rb_node`, `rb_root_cached`), `include/linux/rbtree_augmented.h` (`__rb_parent`, `__rb_erase_augmented`), `lib/timerqueue.c`
- Zephyr, *Balanced Red/Black Tree*, https://docs.zephyrproject.org/latest/kernel/data_structures/rbtree.html — 두 포인터 노드, 하강 중 스택, `lessthan_fn`
- FreeRTOS-Kernel `list.c` — `vListInsert` 정렬 삽입
- L. J. Guibas, R. Sedgewick, *A Dichromatic Framework for Balanced Trees*, FOCS 1978
- Cormen et al., *Introduction to Algorithms*, 13장 Red-Black Trees
- 관련 레슨: DS-M01(`container_of`), DS-M07(AVL), DS-A01/A02(ISR-태스크 전달), DS-A08(B+ 트리), OS-A04(CFS 런큐)
