---
id: DS-M01
time: 90분
updated: 2026-11-07
---
# DS-M01 침입형 리스트: Linux list_head 스타일

## 학습 목표

- **침입형 리스트(intrusive list)** — 링크(`struct list_head`)를 객체 *안에* 박아 두는 방식 — 와 "노드가 객체를 가리키는" 비침입형 리스트의 차이를 메모리·할당·수명 관점에서 설명한다.
- `container_of` 를 `offsetof` 로 직접 만들고, **왜 합법인지**(C11 조항), Linux 의 `typeof` 타입 검사판과 **포터블 C11 판**이 어떻게 다른지 말할 수 있다.
- `list_add/add_tail/del/del_init/move/move_tail/splice(_init)` 과 `list_for_each_entry(_safe)` 를 Linux 와 같은 의미로 구현한다.
- 하나의 task 객체가 **all 리스트와 ready/wait 리스트에 동시에** 속하는 미니 스케줄러를 만들고, 순회 중 삭제·이동을 `_safe` 로 올바르게 처리한다.
- container_of 를 쓰지 않는 독립 심판(raw 링크 검사 + 배열 모델)으로 20,000번 랜덤 연산을 검증한다.

## 1. 문제

Linux 커널, FreeRTOS, Zephyr 의 스케줄러·타이머·드라이버 큐는 거의 전부 **침입형 리스트**다. "malloc 없이 한 객체를 여러 리스트에 O(1) 로 넣고 빼라", "`container_of` 를 설명해 보라", "순회하면서 지우면 왜 터지나" 는 펌웨어/커널 면접의 단골 꼬리질문이다.

DS-B06 에서는 노드 안에 값(`int val`)이 있었다. 침입형은 반대로 **객체 안에 노드가 있다**. 노드는 `next/prev` 두 포인터뿐이라 어떤 타입의 객체에도 넣을 수 있고, 리스트 코드는 객체 타입을 전혀 모른다. 대신 리스트를 걷다가 만난 노드 주소에서 "이 노드를 품고 있는 객체" 를 되찾는 계산 — `container_of` — 이 필요하다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">task 하나에 링크 두 개 — all_node: all, run_node: ready 또는 wait (next 만 표시)</text>
  <g stroke="currentColor" fill="none">
    <rect x="170" y="70" width="130" height="120"/><rect x="360" y="70" width="130" height="120"/><rect x="550" y="70" width="130" height="120"/>
  </g>
  <g stroke="currentColor">
    <rect x="170" y="110" width="130" height="40" class="f-ok" opacity=".45"/><rect x="360" y="110" width="130" height="40" class="f-ok" opacity=".45"/><rect x="550" y="110" width="130" height="40" class="f-ok" opacity=".45"/>
    <rect x="170" y="150" width="130" height="40" class="f-acc" opacity=".45"/><rect x="360" y="150" width="130" height="40" class="f-acc" opacity=".45"/><rect x="550" y="150" width="130" height="40" class="f-acc" opacity=".45"/>
    <rect x="20" y="112" width="90" height="36" class="f-mut"/><rect x="20" y="152" width="90" height="36" class="f-mut"/><rect x="20" y="246" width="90" height="36" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="235" y="95">T0 id=0 READY</text><text x="425" y="95">T1 id=1 BLOCKED</text><text x="615" y="95">T2 id=2 READY</text>
    <text x="235" y="135" font-family="monospace">all_node</text><text x="425" y="135" font-family="monospace">all_node</text><text x="615" y="135" font-family="monospace">all_node</text>
    <text x="235" y="175" font-family="monospace">run_node</text><text x="425" y="175" font-family="monospace">run_node</text><text x="615" y="175" font-family="monospace">run_node</text>
    <text x="65" y="135" font-family="monospace">all</text><text x="65" y="175" font-family="monospace">ready</text><text x="65" y="269" font-family="monospace">wait</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M110 130 L165 130"/><path d="M165 130 l-8 -4 M165 130 l-8 4"/>
    <path d="M300 130 L355 130"/><path d="M355 130 l-8 -4 M355 130 l-8 4"/>
    <path d="M490 130 L545 130"/><path d="M545 130 l-8 -4 M545 130 l-8 4"/>
    <path d="M680 130 L720 130 L720 50 L65 50 L65 107"/><path d="M65 107 l-4 -8 M65 107 l4 -8"/>
    <path d="M110 170 L165 170"/><path d="M165 170 l-8 -4 M165 170 l-8 4"/>
    <path d="M300 172 L330 172 L330 208 L520 208 L520 172 L545 172"/><path d="M545 172 l-8 -4 M545 172 l-8 4"/>
    <path d="M680 175 L705 175 L705 225 L80 225 L80 193"/><path d="M80 193 l-4 8 M80 193 l4 8"/>
    <path d="M110 264 L420 264 L420 195"/><path d="M420 195 l-4 8 M420 195 l4 8"/>
    <path d="M465 190 L465 296 L50 296 L50 287"/><path d="M50 287 l-4 8 M50 287 l4 8"/>
  </g>
  <text x="10" y="320" fill="currentColor">리스트 코드는 list_head 만 안다. T1 은 all 과 wait 에 동시에 — 추가 할당 0회.</text>
</svg>
```

```c
struct list_head { struct list_head *next, *prev; };     /* 값 없음 — 링크뿐 */
struct task {
    int id, state;  bool kill;
    struct list_head all_node;   /* sched.all 에 걸림 (생성 순서) */
    struct list_head run_node;   /* sched.ready 또는 sched.wait 에 걸림 */
};
```

### Part A — container_of, 기본 삽입, 순회

- `container_of(ptr, type, member)` — `ptr` 이 `type` 객체 안 `member` 의 주소일 때 객체 주소를 돌려준다. `offsetof` 로 만들고, **잘못된 타입의 포인터를 넘기면 컴파일 진단**이 나오게 한다 (실행 비용 0).
- `INIT_LIST_HEAD`, `list_empty`, `list_add_between`(Linux `__list_add`), `list_add`(맨 앞, 스택), `list_add_tail`(맨 뒤, 큐).
- `list_for_each_entry(pos, head, member)` — `pos` 는 **객체 포인터**. `list_first_entry/next_entry` 는 주어진다.

### Part B — 삭제·이동·splice, 순회 중 삭제

- `list_del`(떼고 링크에 **poison** 주소), `list_del_init`(떼고 자기 고리 → `list_empty(node)` 로 "어디에도 없음" 확인), `list_move/move_tail`(지금 어느 리스트에 있든 떼어서 다른/같은 리스트로).
- `list_splice`(낡은 head 를 그대로 둠), `list_splice_init`, `list_splice_tail_init` — 빈 src 처리 포함.
- `list_for_each_entry_safe(pos, n, head, member)` — 순회 중 `pos` 를 지우거나 다른 리스트로 옮겨도 안전.

### Part C — 두 리스트에 동시에 속하는 task: 미니 스케줄러

- `task_create`(all 꼬리 + ready 꼬리), `sched_pick`(라운드 로빈: ready 맨 앞을 꼬리로 돌리고 반환), `task_block`/`task_wake`(ready ⇄ wait), `sched_wake_all`(wait 전체를 ready 꼬리로 **O(1) splice**), `sched_block_if(pred)`(ready 를 돌며 조건 맞는 task 를 wait 로 — 순회 중 이동), `sched_reap`(all 을 돌며 `kill` 표시된 task 를 **두 리스트 모두**에서 제거 — 순회 중 삭제).
- 하네스의 `random_test(20000)` 이 16개 task 슬롯으로 위 연산을 섞고, 매 연산 뒤 세 리스트의 링크 일관성·순서·상태를 배열 모델과 비교한다.

## 2. Problem (English)

> **Intrusive doubly linked list, Linux `list_head` style.**
>
> Implement in C11 (GCC/Clang `__typeof__` allowed), with no dynamic allocation:
>
> 1. `container_of(ptr, type, member)` using `offsetof`, with a zero-cost compile-time check that `ptr` has the type of `&((type *)0)->member`. Then `INIT_LIST_HEAD`, `list_empty`, `list_add`, `list_add_tail`, and `list_for_each_entry(pos, head, member)` where `pos` is a pointer to the containing object.
> 2. `list_del` (poison the links), `list_del_init`, `list_move`, `list_move_tail`, `list_splice`, `list_splice_init`, `list_splice_tail_init` — all O(1) — and `list_for_each_entry_safe(pos, n, head, member)` that tolerates removing or moving `pos` inside the loop body.
> 3. A tiny scheduler where every `struct task` is simultaneously on `all` (creation order) and on exactly one of `ready` / `wait`: `task_create`, `sched_pick` (round robin), `task_block`, `task_wake`, `sched_wake_all` (O(1) splice), `sched_block_if(pred)` (move while iterating), `sched_reap` (delete killed tasks from both lists while iterating `all`).
>
> Example: create 0..4 → `pick` ×6 returns 0,1,2,3,4,0. Block 2 and 0 → ready `1 3 4`, wait `2 0`, all still `0 1 2 3 4`.
>
> Follow-ups: Is `container_of` defined behavior? What does `pos` point to after `list_for_each_entry` finishes? Is `_safe` safe if the body deletes the *next* element, or if an ISR deletes it?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 이 Mac: 27.2 SDK 링크가 깨져 있음
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/DS-M01_st && /tmp/DS-M01_st
# 실제 출력(끝부분):
#   random_test: step 1 op 13 에서 불일치
#   FAIL starter.c:420  random_test(20000)
#   random: create 0, pick 0, block 0, wake 0, wake_all 0, block_if 1 (moved 0), reap 0 (killed 0), max alive 0
#   4/35 passed        (exit 1)
```

스텁은 경고 0개로 컴파일되고 크래시 없이 "틀린 값" 만 낸다: `container_of` 는 `NULL`, 순회 매크로는 0회 반복, 함수는 아무것도 안 함. 하네스는 `container_of` 를 **쓰지 않고** raw 링크 주소만 비교하므로(심판이 피고의 매크로에 의존하지 않음), 여러분 매크로가 틀려도 하네스는 정확히 판정한다.

## 4. 힌트

<details>
<summary>힌트 1 — container_of 는 뺄셈 한 번</summary>

멤버 주소 = 객체 주소 + `offsetof(type, member)`. 그러니 객체 주소 = 멤버 주소 − offset. 단, `ptr - 32` 를 `struct list_head *` 로 하면 **32 × sizeof(struct list_head)** 바이트를 빼게 된다. 바이트 단위로 빼려면 먼저 `(char *)` 로 바꾼다.

</details>

<details>
<summary>힌트 2 — 실행 비용 0 인 타입 검사</summary>

`sizeof` 의 피연산자는 (VLA 가 아니면) **평가되지 않는다**(C11 6.5.3.4p2). 그 안에서 `(ptr) == &((type *)0)->member` 를 써 두면 널 포인터를 실제로 역참조하지 않으면서, 타입이 다른 포인터끼리 비교하는 제약 위반(6.5.9p2) 진단을 받을 수 있다. 콤마 연산자로 `(void)sizeof(...), 진짜계산`.

</details>

<details>
<summary>힌트 3 — 순회의 끝 조건</summary>

`pos = list_first_entry(head, __typeof__(*pos), member)` 로 시작해 `pos = list_next_entry(pos, member)` 로 간다. 끝은 `&pos->member == head` — 즉 sentinel 에서 container_of 로 "계산만 한" 가짜 객체에 도착했을 때. 그 가짜 `pos` 는 **절대 역참조하지 않는다** (`&pos->member` 는 주소 계산일 뿐).

</details>

<details>
<summary>힌트 4 — move 와 splice 는 B06 의 재조립</summary>

`list_move(e, h)` = 링크만 떼기(`list_del_entry`, poison 없이) + `list_add(e, h)`. 노드가 *어느 리스트에 있었는지 몰라도* 된다 — 노드가 이웃을 알기 때문. splice 는 `first = list->next`, `last = list->prev` 를 `prev`, `next` 사이에 끼우는 포인터 4개, 빈 src 는 먼저 거른다 (DS-B06 연습과 같은 함정).

</details>

<details>
<summary>힌트 5 — _safe 와 Part C</summary>

`n = list_next_entry(pos, member)` 를 **본문 실행 전에** 계산해 두면, 본문이 `pos` 를 지우든 다른 리스트로 옮기든 다음 차례는 `n` 이다. `sched_reap` 은 all 을 순회(all_node 기준)하며 `list_del(&t->all_node); list_del(&t->run_node);` — run_node 가 ready 에 있든 wait 에 있든 같은 한 줄이다. `sched_wake_all` 은 상태 갱신용 순회(삭제 없음 → 일반 순회) 후 `list_splice_tail_init(&s->wait, &s->ready)`.

</details>

## 5. 풀이

### Part A — container_of: 주소 계산, 합법성, 타입 검사

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">struct task 메모리 (이 Mac, 64-bit) — container_of(ptr, struct task, run_node)</text>
  <g stroke="currentColor">
    <rect x="100" y="60" width="48" height="40" class="f-mut"/><rect x="148" y="60" width="48" height="40" class="f-mut"/>
    <rect x="196" y="60" width="12" height="40" class="f-mut"/><rect x="208" y="60" width="84" height="40" fill="none" stroke-dasharray="3 3"/>
    <rect x="292" y="60" width="192" height="40" class="f-ok" opacity=".45"/><rect x="484" y="60" width="192" height="40" class="f-acc" opacity=".45"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="124" y="85">id</text><text x="172" y="85">state</text><text x="250" y="85">pad</text>
    <text x="388" y="85">all_node</text><text x="580" y="85">run_node</text>
    <text x="100" y="118">0</text><text x="148" y="118">4</text><text x="202" y="118">8</text><text x="292" y="118">16</text>
    <text x="484" y="118">32</text><text x="676" y="118">48</text>
  </g>
  <text x="210" y="50" fill="currentColor" font-family="monospace">kill(8)</text>
  <g stroke="currentColor" fill="none">
    <path d="M484 165 L484 105"/><path d="M484 105 l-4 8 M484 105 l4 8"/>
    <path d="M484 150 L104 150"/><path d="M104 150 l8 -4 M104 150 l8 4"/>
    <path d="M100 165 L100 105"/><path d="M100 105 l-4 8 M100 105 l4 8"/>
  </g>
  <g fill="currentColor">
    <text x="494" y="160" font-family="monospace">ptr = &amp;t.run_node</text>
    <text x="200" y="143">− offsetof(struct task, run_node) = − 32 바이트</text>
    <text x="10" y="185" font-family="monospace">(struct task *)((char *)ptr - 32)  → &amp;t   (char* 라서 바이트 단위)</text>
    <text x="10" y="207" font-family="monospace">(struct task *)ptr               → &amp;t + 32: t-&gt;id 를 run_node.next 자리에서 읽는다</text>
    <text x="10" y="235">Cortex-M (4 B 포인터): all_node @12, run_node @20, sizeof 28 — offset 은 컴파일 타임 상수.</text>
  </g>
</svg>
```

**주소 계산.** `offsetof(type, member)` 는 `<stddef.h>` 의 표준 매크로로, 구조체 시작에서 멤버까지의 **바이트 수**를 정수 상수식(`size_t`)으로 준다(C11 7.19p3). 멤버 주소에서 그만큼 빼면 객체 주소다. 포인터 산술은 가리키는 타입 크기 단위이므로 반드시 `char *` 로 바꿔서 뺀다. 결과를 `(type *)(void *)` 로 바꾸는 `void *` 경유는 일부 컴파일러의 `-Wcast-align`(char* → 더 큰 정렬 타입 캐스트) 경고를 피하는 관용구다.

실제 측정 (`demo_container_of.c`, 이 Mac):

```text
sizeof(struct list_head) = 16, sizeof(struct task) = 48
offsetof(task, kill) = 8, all_node = 16, run_node = 32
&t           = 0x1008d4000
&t.all_node  = 0x1008d4010  (&t + 16)
&t.run_node  = 0x1008d4020  (&t + 32)
container_of(all_node) = 0x1008d4000, container_of(run_node) = 0x1008d4000, gnu = 0x1008d4000, id = 7
naive cast (struct task *)&t.run_node = 0x1008d4020  -> 32 B 어긋남
```

어느 링크에서 출발하든 **같은 객체**로 돌아온다 — 이것이 "한 객체, 여러 리스트" 의 열쇠다. 같은 파일을 `clang --target=thumbv7em-none-eabi -ffreestanding -c` 로 컴파일하면 32-bit 분기의 `_Static_assert`(all_node @12, run_node @20, sizeof 28)가 통과한다 — Cortex-M4 레이아웃을 Mac 에서 확인한 것.

**왜 합법인가 (C11 근거).**

| 단계 | 근거 |
|---|---|
| 멤버는 선언 순서대로 증가하는 주소에, 맨 앞엔 패딩 없음 | 6.7.2.1p15 |
| `offsetof` 값은 컴파일러가 아는 실제 바이트 offset | 7.19p3 (`&(t.member)` 가 주소 상수가 되는 member-designator 면 OK: `q.link[1]` 같은 중첩·상수 인덱스도 됨 — 연습 4) |
| 객체 포인터를 `char *` 로 바꾸면 그 객체의 첫 바이트, 증가시키며 나머지 바이트에 접근 가능 | 6.3.2.3p7 |
| 결과 `struct task *` 로 읽고 쓰기 | 그 주소엔 실제로 `struct task` 객체가 있으므로 유효 타입(effective type) 규칙 6.5p7 위반 없음 |
| `sizeof` 안의 `((type *)0)->member` | 피연산자는 평가되지 않음(6.5.3.4p2) → 널 역참조 없음 |

솔직한 단서: "멤버를 가리키던 `char *` 를 멤버 *밖*(앞쪽)으로 옮기는 것" 이 표준 문구만으로 깔끔하게 보장되는지는 포인터 출처(provenance) 관점에서 논쟁이 있다. 하지만 이 관용구는 Linux(`container_of`), Windows(`CONTAINING_RECORD`), Zephyr(`CONTAINER_OF`) 등이 수십 년 의존해 왔고, 주요 컴파일러가 모두 지원한다. **진짜 UB 는 따로 있다**: 링크가 실제로는 그 타입 객체 안에 있지 않을 때(예: 독립 변수인 sentinel `head` 에 container_of 를 적용해 *역참조*), 또는 같은 타입의 **다른 멤버 이름**을 줄 때(`container_of(&t->all_node, struct task, run_node)` → 16 B 어긋난 주소; 둘 다 `struct list_head` 라 타입 검사도 못 잡는다).

**첫 멤버라면 캐스트로 충분하지 않나?** 6.7.2.1p15 덕분에 offset 0 인 첫 멤버는 `(type *)ptr` 로 되돌릴 수 있다 (C "상속" 관용구). 그러나 링크가 두 개 이상이면 하나만 첫 멤버일 수 있다 — 일반해가 container_of 다.

**타입 검사: Linux 판 vs 포터블 C11 판.**

```c
/* Linux include/linux/container_of.h (GNU C: 문장 표현식 ({ }), typeof, __builtin_types_compatible_p) */
#define container_of(ptr, type, member) ({                              \
        void *__mptr = (void *)(ptr);                                   \
        static_assert(__same_type(*(ptr), ((type *)0)->member) ||       \
                      __same_type(*(ptr), void),                        \
                      "pointer type mismatch in container_of()");       \
        ((type *)(__mptr - offsetof(type, member))); })
/* 예전 커널: const typeof(((type *)0)->member) *__mptr = (ptr); → 타입이 다르면 대입 경고 */

/* 이 레슨 (C11 + 확장 없음) */
#define container_of(ptr, type, member)                     \
    ((void)sizeof((ptr) == &((type *)0)->member),           \
     (type *)(void *)((char *)(ptr) - offsetof(type, member)))
```

| | Linux (GNU C) | 포터블 C11 |
|---|---|---|
| 검사 수단 | `typeof` + `__builtin_types_compatible_p` → `static_assert` | 서로 다른 포인터 타입 비교 = 제약 위반(6.5.9p2) → 진단 의무 |
| 실패 시 | 항상 **에러** | clang/GCC 는 **경고**(`-Wcompare-distinct-pointer-types`) → `-Werror` 여야 빌드 실패 |
| `void *` 허용 | 명시적으로 허용 | `void *` 와의 비교는 합법이라 자연히 허용 |
| `const` 포인터 | 결과는 non-const (const 보존판 `container_of_const` 가 따로 있음, `_Generic` 사용) | 비교는 한정자 무시로 통과, 결과 캐스트에서 const 가 떨어짐 |
| `ptr` 평가 횟수 | 1회 (`__mptr` 에 저장) | 1회 (`sizeof` 안의 `ptr` 은 평가 안 됨) |
| `void *` 산술 `__mptr - off` | GNU 확장 | 표준대로 `char *` 산술 |

`int *` 를 잘못 넘기면 두 판 모두 컴파일이 멈춘다 (실제 출력):

```text
$ cc -std=c11 -Wall -Wextra -Werror -O2 -DBAD_TYPE demo_container_of.c
demo_container_of.c:87:11: error: comparison of distinct pointer types ('int *' and 'struct list_head *') [-Werror,-Wcompare-distinct-pointer-types]
$ cc -std=c11 -Wall -Wextra -Werror -O2 -DBAD_TYPE_GNU demo_container_of.c
demo_container_of.c:91:11: error: static assertion failed due to requirement '__builtin_types_compatible_p(int, struct list_head) || __builtin_types_compatible_p(int, void)': pointer type mismatch in container_of()
```

`typeof` 는 C23 에서 표준 키워드(`typeof`, `typeof_unqual`)가 되었다. C11 에선 `__typeof__` 가 GCC/Clang(armclang 포함) 확장이고 `-std=c11 -Wall -Wextra` 에서 경고 없이 쓰인다. 이 레슨은 container_of 는 순수 C11 로, `list_next_entry`/순회 매크로만 `__typeof__(*(pos))` 를 쓴다 (확장 없이 하려면 순회 매크로가 타입 인자를 하나 더 받으면 된다: `list_for_each_entry_t(pos, head, struct task, run_node)`).

**기본 연산과 순회.** 삽입은 DS-B06 의 `insert_between` 그대로(`list_add_between`). `list_add` = head 바로 뒤(스택: 4 2 0), `list_add_tail` = head 바로 앞(큐: 0 1 2 3 4). 순회 매크로의 진행을 표로 보면:

| 반복 | `pos` | `&pos->member` | 조건 `!= head` |
|---|---|---|---|
| 시작 | container_of(head->next) = T4 | &T4.run_node | 참 → 본문 |
| 2 | container_of(T4.run_node.next) = T2 | &T2.run_node | 참 |
| 3 | T0 | &T0.run_node | 참 |
| 4 | container_of(T0.run_node.next = **head**) = 가짜 | **head** | 거짓 → 끝 |

마지막 `pos` 는 `(char *)&ready − 32` — `ready` 앞 32 바이트를 "task 라고 우기는" 주소다. 역참조만 안 하면 비교는 맞다. 이 때문에 **루프가 끝난 뒤 `pos` 를 쓰면 안 된다**: "못 찾았는데 `pos->id` 를 읽는" 버그가 실제 Linux 에서 반복돼, 2022년 반복자 변수를 루프 안에 가두자는 논의 끝에 커널이 `-std=gnu89` → `gnu11` 로 올라갔다(v5.18, `for` 안 선언을 쓰려고). 단, 연습 2 처럼 `&pos->member == head` 라는 성질만 의도적으로 쓰는 건 Linux 에서도 쓰는 관용구다.

### Part B — 삭제·이동·splice 와 "순회 중 삭제"

| 연산 | 하는 일 | poison? | 노드가 어느 리스트에 있었는지 알아야? |
|---|---|---|---|
| `list_del(e)` | 이웃끼리 잇고 `e->next = 0x100`, `e->prev = 0x122` | O | X |
| `list_del_init(e)` | 이웃끼리 잇고 `e` 를 자기 고리로 | X | X |
| `list_move(e, h)` | `list_del_entry(e)` + `list_add(e, h)` | X | X |
| `list_move_tail(e, h)` | `list_del_entry(e)` + `list_add_tail(e, h)` — 같은 리스트면 **회전** | X | X |
| `list_splice(l, h)` | l 의 노드들을 h 맨 앞에, **l 은 갱신 안 함** | — | — |
| `list_splice_init` / `_tail_init` | 붙이고 l 을 빈 리스트로 | — | — |

마지막 열이 침입형의 힘이다. `sched_reap` 은 task 의 run_node 가 ready 에 있는지 wait 에 있는지 **묻지 않고** `list_del(&t->run_node)` 한다. 대신 리스트에 `len` 이 없다 (B06 Q4: len 이 있으면 remove 가 리스트를 알아야 한다). 길이가 필요하면 Part C 의 `nr` 처럼 바깥에서 센다.

`list_splice`(init 없음) 뒤의 src head 는 테스트가 보여주듯 `tmp.next == &T[8].run_node` — **옮겨 간 노드를 아직 가리키는 낡은 포인터**다. `list_empty(&tmp)` 가 거짓이라 거기에 다시 add 하면 dst 리스트를 망가뜨린다. Linux 도 같은 동작이며, src 를 다시 쓸 거면 `_init` 판을 쓴다.

**순회 중 이동이 왜 무한 루프가 되나.** `sched_block_if` 를 일반 `list_for_each_entry` 로 쓰면:

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">일반 순회 중 pos(A) 를 wait 로 옮기면 — 다음 걸음이 다른 리스트로 샌다</text>
  <g fill="currentColor"><text x="10" y="70">전</text><text x="10" y="165">후</text></g>
  <g stroke="currentColor" fill="none">
    <rect x="130" y="50" width="56" height="30"/><rect x="220" y="50" width="56" height="30"/>
    <rect x="490" y="50" width="56" height="30"/>
    <rect x="130" y="145" width="56" height="30"/>
    <rect x="490" y="145" width="56" height="30"/>
  </g>
  <g stroke="currentColor">
    <rect x="40" y="50" width="56" height="30" class="f-mut"/><rect x="400" y="50" width="56" height="30" class="f-mut"/>
    <rect x="40" y="145" width="56" height="30" class="f-mut"/><rect x="400" y="145" width="56" height="30" class="f-mut"/>
    <rect x="580" y="145" width="56" height="30" class="f-warn" opacity=".6"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="68" y="70">ready</text><text x="158" y="70">A</text><text x="248" y="70">B</text>
    <text x="428" y="70">wait</text><text x="518" y="70">C</text>
    <text x="68" y="165">ready</text><text x="158" y="165">B</text>
    <text x="428" y="165">wait</text><text x="518" y="165">C</text><text x="608" y="165">A</text>
    <text x="158" y="45">pos</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M96 65 L125 65"/><path d="M125 65 l-7 -4 M125 65 l-7 4"/>
    <path d="M186 65 L215 65"/><path d="M215 65 l-7 -4 M215 65 l-7 4"/>
    <path d="M456 65 L485 65"/><path d="M485 65 l-7 -4 M485 65 l-7 4"/>
    <path d="M96 160 L125 160"/><path d="M125 160 l-7 -4 M125 160 l-7 4"/>
    <path d="M456 160 L485 160"/><path d="M485 160 l-7 -4 M485 160 l-7 4"/>
    <path d="M546 160 L575 160"/><path d="M575 160 l-7 -4 M575 160 l-7 4"/>
  </g>
  <g stroke="currentColor" fill="none" stroke-dasharray="5 3">
    <path d="M608 175 L608 200 L428 200 L428 180"/><path d="M428 180 l-4 8 M428 180 l4 8"/>
  </g>
  <g fill="currentColor">
    <text x="455" y="218">A.next = &amp;wait</text>
    <text x="10" y="240">일반: 다음 pos = container_of(&amp;wait) = 가짜 객체, &amp;pos-&gt;member = &amp;wait ≠ &amp;ready</text>
    <text x="10" y="262">→ C → A → 가짜 → C … ready 는 다시 오지 않음 (무한 루프 + 가짜 객체 읽기)</text>
    <text x="10" y="284">_safe: 본문 전에 n = B 를 저장 → 다음 pos = B → &amp;ready 에서 끝</text>
  </g>
</svg>
```

실제로 정답 코드를 이렇게 바꿔 돌려 보았다 (ASan/UBSan 빌드):

| 변형 | 관찰 |
|---|---|
| `sched_block_if` 를 일반 순회로 | 5초 안에 끝나지 않아 강제 종료 — 무한 루프 |
| `sched_reap` 을 일반 순회로 | `AddressSanitizer: SEGV on unknown address 0x0000000000f8 ... in sched_reap` |
| `list_splice_tail_init` 에서 `INIT_LIST_HEAD(list)` 누락 | 32/35 — 이름 붙은 테스트 2개 + `random_test` FAIL |
| `list_splice_init` 에서 빈 src 검사 누락 | 30/35 |
| container_of 에서 `- offsetof` 누락 (단순 캐스트) | UBSan `member access within null pointer of type 'struct task'` 연속 |

reap 의 `0xf8` 이 poison 의 효과를 그대로 보여준다: 지운 노드의 `all_node.next` 는 `0x100`, 일반 순회가 그걸 따라가 `container_of(0x100, struct task, all_node)` = `0x100 − 16 = 0xf0`, 본문이 `t->kill`(offset 8)을 읽어 **0xf8** 에서 fault (demo 출력: `poison 0x100 에서 container_of(.., all_node) -> 0xf0, 그 ->kill 읽기 주소 0xf8`). oops 주소가 0x100 근처면 "지운 리스트 노드를 썼다" 를 바로 안다. macOS 64-bit 는 하위 4 GiB 가 `__PAGEZERO` 라 반드시 fault 한다. **Cortex-M 은 다르다** — 0x0 부근은 보통 플래시/벡터 테이블(STM32 는 부트 메모리 별칭)이라 *읽기가 성공해 버린다*. 그래서 MCU 에선 poison 이 조용히 쓰레기를 읽을 수 있고, MPU 로 0 번지 근처를 접근 금지 영역으로 막는 기법(Zephyr `CONFIG_NULL_POINTER_EXCEPTION_DETECTION_MPU`)을 함께 쓴다.

**`_safe` 의 한계.** `_safe` 는 "**본문이 `pos` 를** 지우거나 옮겨도 된다" 까지만 보장한다. 본문이 `n`(다음 원소)을 지우면 `n` 은 poison 을 품은 죽은 노드라 다음 걸음에서 터진다. 다른 스레드/ISR 이 지우는 것은 `_safe` 와 무관한 동시성 문제다 (Q4).

### Part C — 한 task, 두 리스트: 미니 스케줄러

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">run_node 의 상태 전이 (all_node 는 create 부터 reap 까지 all 에 고정)</text>
  <g stroke="currentColor">
    <rect x="60" y="70" width="140" height="50" class="f-ok" opacity=".45"/>
    <rect x="380" y="70" width="140" height="50" class="f-warn" opacity=".45"/>
    <rect x="600" y="150" width="120" height="44" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="130" y="100" font-family="monospace">ready</text><text x="450" y="100" font-family="monospace">wait</text>
    <text x="660" y="170">DEAD</text><text x="660" y="186" font-family="monospace">0x100/0x122</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M200 82 L375 82"/><path d="M375 82 l-8 -4 M375 82 l-8 4"/>
    <path d="M380 110 L205 110"/><path d="M205 110 l8 -4 M205 110 l8 4"/>
    <path d="M90 70 C90 35, 170 35, 170 66"/><path d="M170 66 l-4 -8 M170 66 l4 -8"/>
    <path d="M20 95 L55 95"/><path d="M55 95 l-8 -4 M55 95 l-8 4"/>
    <path d="M130 120 L130 172 L595 172"/><path d="M595 172 l-8 -4 M595 172 l-8 4"/>
    <path d="M450 120 L450 172"/>
  </g>
  <g fill="currentColor">
    <text x="215" y="75">block · block_if (list_move_tail)</text>
    <text x="215" y="135">wake · wake_all (splice_tail_init)</text>
    <text x="180" y="42">pick: 맨 앞 → 맨 뒤 (회전)</text>
    <text x="5" y="140">create</text>
    <text x="220" y="190">reap: list_del(all_node) + list_del(run_node)</text>
    <text x="10" y="220">wake_all 은 노드 수와 무관하게 포인터 4개 + head 초기화. 상태 갱신용 순회만 O(k).</text>
  </g>
</svg>
```

**불변식** (하네스 `sched_matches` 가 매 연산 뒤 검사):

1. 세 리스트 각각 고리 일관성: 모든 링크 `p->next->prev == p`, 모르는 주소(NULL·poison 포함) 없음, 유한 걸음에 head 로 복귀 (DS-B06 검사기와 같은 원리).
2. all 의 노드는 전부 어떤 task 의 `all_node`, ready/wait 의 노드는 전부 `run_node` — 링크 종류가 섞이지 않는다.
3. `nr == |all| == |ready| + |wait|` — 살아 있는 task 는 run 리스트 중 **정확히 하나**에.
4. ready 의 task 는 `T_READY`, wait 의 task 는 `T_BLOCKED`. 죽은 슬롯은 `T_DEAD` 이고 링크가 poison.
5. 세 리스트의 **순서**가 배열 모델과 같다.

이름 붙은 테스트의 추적:

| 연산 | all | ready | wait |
|---|---|---|---|
| create 0..4 | 0 1 2 3 4 | 0 1 2 3 4 | — |
| pick ×6 → 0,1,2,3,4,0 | 0 1 2 3 4 | 1 2 3 4 0 | — |
| block 2, block 0 | 0 1 2 3 4 | 1 3 4 | 2 0 |
| block_if(odd) → 2개 | 0 1 2 3 4 | 4 | 2 0 1 3 |
| wake 0 | 0 1 2 3 4 | 4 0 | 2 1 3 |
| kill 1(wait), 4(ready); reap → 2 | 0 2 3 | 0 | 2 3 |
| wake_all → 2 | 0 2 3 | 0 2 3 | — |

`block_if` 행: ready `1 3 4` 를 `_safe` 로 돌며 1 → wait, 3 → wait, 4 는 남는다. 옮겨진 순서대로 wait 꼬리에 붙어 `2 0 1 3`. reap 은 **all 을** 돌기 때문에 순회 중 지우는 것은 all_node(→ `_safe` 필요)이고, run_node 삭제는 다른 리스트라 순회에 영향이 없다.

**랜덤 테스트.** 슬롯 16개, xorshift32(시드 `0x1BADB002`), 20,000 step. `r % 16` 으로 create 4/16, pick 3/16, block 3/16, wake 2/16, wake_all·block_if 각 1/16, kill+reap 2/16. 모델은 슬롯 번호 배열 세 개(`m_all`, `m_rdy`, `m_wt`)로, 각 연산을 배열 삽입·삭제로 흉내 낸다. 실제 출력:

| 연산 | 실행 횟수 |
|---|---|
| create | 4,411 |
| pick | 3,540 |
| block / wake | 3,518 / 1,695 |
| wake_all | 1,271 |
| block_if (옮긴 task 수) | 1,280 (3,196) |
| reap (죽인 task 수) | 2,456 (4,396) |
| 최대 동시 생존 | 16 (슬롯 전부) |

나머지 step 은 "슬롯이 꽉 참 / ready 나 wait 가 빔" 으로 건너뛴 것이다 — 덕분에 빈 리스트 splice, 빈 ready 에서 pick(NULL), 꽉 찬 풀이 수천 번 섞인다. 전체 실행은 이 Mac 에서 0.01 초 미만.

**복잡도와 메모리.**

| 연산 | 시간 | 비고 |
|---|---|---|
| add / del / move / splice / pick / block / wake | O(1), 분기 없음 | 최악 = 평균 → 임계 구역 길이가 상수 |
| wake_all | splice O(1) + 상태 갱신 O(k) | 상태를 리스트 소속으로만 표현하면 O(1) |
| for_each, block_if, reap | O(n) | |
| 메모리 | 소속 가능한 리스트 수 × 포인터 2개 | Cortex-M: 8 B/리스트, 64-bit: 16 B/리스트 |

비침입형과 비교 (demo 실제 출력):

```text
intrusive: 링크 2 x 16 = 32 B (task 안), 추가 할당 0회
external : ext_node 24 B 요청 -> malloc_size 32 B, x2 = 64 B + 할당 2회 + 포인터 한 번 더 따라가기
```

비침입형(`std::list<task*>` 식)은 리스트에 넣을 때마다 노드를 **할당**해야 하고(ISR 에서 불가, 실패 가능, 단편화), "이 task 를 ready 에서 빼라" 를 O(1) 로 하려면 task 가 자기 노드 포인터를 따로 들고 있어야 한다. 침입형은 객체가 곧 노드라 할당 0, 삭제 O(1), 객체 하나를 읽으면 링크도 같은 캐시 라인 근처에 있다. 대가: 객체가 들어갈 수 있는 리스트 **종류와 개수를 컴파일 타임에** 정해야 하고(task 당 링크 2개면 동시에 최대 2개 리스트), 객체를 해제하기 전에 반드시 모든 리스트에서 빼야 한다 (수명 결합).

**실제 시스템에서는.**
- **Linux**: `task_struct` 에 `struct list_head tasks`(전체 프로세스, `for_each_process`), `children`, `sibling` 등이 박혀 있다 — 이 레슨의 all_node 와 같은 패턴.
- **FreeRTOS**: TCB 에 `ListItem_t xStateListItem`(ready/delayed/suspended)과 `xEventListItem`(큐·세마포어 대기) 두 개. 단 container_of 대신 각 `ListItem_t` 가 **`pvOwner` 역포인터**(와 소속 리스트 `pxContainer`)를 갖는다 — 아이템당 포인터 2개를 더 쓰는 대신 매크로 트릭이 없다 (`listGET_LIST_ITEM_OWNER`).
- **Zephyr**: `sys_dlist_t`/`sys_dnode_t` (`include/zephyr/sys/dlist.h`) + `CONTAINER_OF`, 순회는 `SYS_DLIST_FOR_EACH_CONTAINER(_SAFE)`.
- **BSD `sys/queue.h`**: `TAILQ_ENTRY(type)` 로 링크를 객체에 박지만 링크가 `struct type *` 로 타입이 정해져 있어 container_of 가 필요 없다 (대신 리스트마다 필드 이름이 매크로 인자).
- SSD FTL 에서도 같은 모양: 블록 디스크립터 하나가 "free 블록 리스트" 와 "GC 후보 리스트" 를 오가고, 실행 중인 NAND 명령 디스크립터가 "die 별 큐" 와 "완료 큐" 에 걸린다 — 할당 없이, ISR 완료 핸들러에서 O(1) 로.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/DS-M01_so && /tmp/DS-M01_so
# random: create 4411, pick 3540, block 3518, wake 1695, wake_all 1271, block_if 1280 (moved 3196), reap 2456 (killed 4396), max alive 16
# 35/35 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o /tmp/DS-M01_as && /tmp/DS-M01_as   # 같은 출력, 리포트 없음

# 관찰용
cc -std=c11 -Wall -Wextra -Werror -O2 demo_container_of.c -o /tmp/DS-M01_demo && /tmp/DS-M01_demo
cc -std=c11 -Wall -Wextra -Werror -O2 -DBAD_TYPE demo_container_of.c       # 의도된 컴파일 에러
clang --target=thumbv7em-none-eabi -ffreestanding -std=c11 -c demo_container_of.c -o /tmp/DS-M01_m4.o   # 32-bit 레이아웃 assert
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `(type *)((struct list_head *)ptr - offsetof(...))` | 포인터 산술이 16 B 단위 → offset × 16 바이트를 뺌 | `(char *)(ptr) - offsetof(type, member)` |
| `container_of(&t->all_node, struct task, run_node)` | 멤버 이름 착각. 타입이 같아 **어떤 타입 검사도 못 잡음** → 16 B 어긋난 객체 | 링크 이름을 리스트 이름과 맞춤(`all` ↔ `all_node`), 리스트별 접근 함수로 감싸기 |
| 루프 끝난 뒤 `pos->id` 사용 ("못 찾음" 처리) | `pos` 는 head 에서 계산한 가짜 객체 → 쓰레기 읽기 | 찾았는지 별도 변수/포인터(`found = NULL`)로 기록 |
| 일반 순회 중 `list_del(&pos->...)` | 다음 걸음이 poison(0x100)을 따라감 → 실측 SEGV @0xf8 | `list_for_each_entry_safe` |
| 일반 순회 중 `list_move` | 다음 걸음이 *다른 리스트*로 새어 나감 → 실측 무한 루프 | `_safe` |
| `_safe` 루프에서 `n` 을 지움 | 저장해 둔 다음 노드가 죽은 노드 | 지운 뒤 `n` 을 다시 계산하거나 루프를 재시작 |
| `list_splice` 후 src 재사용 | src head 가 옮겨 간 노드를 가리키는 낡은 포인터 | `list_splice_init` / `_tail_init` |
| 빈 src 로 splice | `first = last = src head` → sentinel 이 dst 에 끼어듦 | `if (!list_empty(list))` |
| task 를 all 에서만 빼고 free/재사용 | run_node 가 여전히 ready 에 걸려 있음 → 재생성 시 리스트 손상 | 해제 전 **모든** 링크 `list_del` (reap 처럼) |
| 같은 노드를 두 리스트에 `list_add` | 링크 하나 = 리스트 하나. 두 번째 add 가 첫 리스트를 끊음 | 리스트마다 별도 `list_head` 멤버 |
| `INIT_LIST_HEAD` 없이 `{0}` 만 | `next == NULL` → `list_empty` 거짓, add 시 NULL 역참조 | 정적 초기화 `LIST_HEAD_INIT(name) { &(name), &(name) }` 또는 INIT 호출 |
| 내부 헬퍼를 `__list_add` 로 명명 | `__` 로 시작하는 이름은 구현 예약(C11 7.1.3). 커널은 "구현" 이라 괜찮지만 사용자 코드는 UB | `list_add_between` 같은 일반 이름 |

## 8. 면접 Q&A

<details>
<summary>Q1. 침입형 리스트와 std::list&lt;T*&gt; 같은 비침입형의 트레이드오프는?</summary>

침입형: 삽입 시 할당 0 (ISR·결정성 OK), 객체 포인터만으로 O(1) 삭제, 한 객체를 여러 리스트에 동시에, 링크가 객체 안에 있어 캐시 지역성 좋음, 링크 메모리 2포인터/리스트. 단점: 소속 가능한 리스트를 구조체 정의에 박아야 함(일반 컨테이너 라이브러리처럼 아무 객체나 못 넣음), 수명 결합(해제 전 모든 리스트에서 unlink — 안 하면 use-after-free), 같은 링크로 같은 리스트에 두 번 못 들어감, 타입 안전성이 매크로에 의존. 비침입형은 반대 — 아무 객체나 넣지만 노드마다 할당(이 Mac 에서 24 B 요청 → 32 B 블록)과 포인터 한 번 더 따라가기.

</details>

<details>
<summary>Q2. container_of 는 정의된 동작인가요? 꼬리질문: offsetof 를 직접 &((T*)0)->m 로 만들면?</summary>

`offsetof` 는 표준 매크로(7.19)이고, `char *` 로 바꿔 객체 바이트 안에서 이동하는 것(6.3.2.3p7), 결과 포인터가 실제 `type` 객체를 가리키는 것(6.5p7)으로 실무상 정의된 관용구다 — 멤버 포인터에서 바깥으로 나가는 산술이 표준 문구로 완전히 명시됐는지는 provenance 논쟁이 있지만 모든 주요 컴파일러가 지원하고 Linux·Windows·Zephyr 가 의존한다. 진짜 UB 는 링크가 그 타입 객체 안에 없을 때다. `((size_t)&((T *)0)->m)` 는 옛 `offsetof` 구현인데 널 포인터 산술이라 표준상 UB 다 — 그래서 컴파일러는 `__builtin_offsetof` 로 구현하고, 사용자는 `<stddef.h>` 의 것을 쓴다. 반면 `sizeof` 안의 `&((T *)0)->m` 은 평가되지 않으므로 타입 검사용으로는 안전하다.

</details>

<details>
<summary>Q3. list_for_each_entry 가 끝난 뒤 pos 는 무엇을 가리키나요?</summary>

head(sentinel) 에 container_of 를 적용해 *계산만 한* 가짜 객체 — `&pos->member == head` 다. 필드를 읽으면 head 주변 메모리를 읽는다. "검색 실패 후 pos 를 쓰는" 버그가 Linux 에서 반복되어 2022년 반복자를 루프 범위로 가두는 논의가 있었고, `for` 안 변수 선언을 쓰기 위해 커널이 v5.18 에서 `gnu11` 로 전환했다. 반면 "끝까지 돌면 `&pos->member` 가 head 다" 를 이용해 `list_add_tail(new, &pos->member)` 로 정렬 삽입하는 것은 의도된 관용구(연습 2).

</details>

<details>
<summary>Q4. _safe 면 ISR 이나 다른 스레드가 지워도 안전한가요?</summary>

아니다. `_safe` 는 **본문 자신이 pos 를** 지우는 경우만 다룬다. 다른 실행 흐름이 `n` 을 지우면 저장해 둔 `n` 이 죽은 노드가 된다. 리스트 연산 자체도 포인터 4개 쓰기라 원자적이지 않다. 단일 코어 MCU 는 순회 *전체*를 임계 구역(`taskENTER_CRITICAL`/PRIMASK·BASEPRI)으로 감싸거나 — 순회가 O(n) 이면 인터럽트 지연이 n 에 비례하므로 피하고 — ISR 은 별도 리스트에만 넣게 한다. FreeRTOS 가 스케줄러가 suspend 된 동안 ISR 이 깨운 task 를 ready 리스트가 아닌 `xPendingReadyList` 에 넣었다가 resume 때 옮기는 것이 그 예. Linux 는 spinlock(`spin_lock_irqsave`) 또는 읽기 위주면 RCU(`list_for_each_entry_rcu`, `list_del_rcu` + grace period 후 해제).

</details>

<details>
<summary>Q5. list_del 은 왜 NULL 이 아니라 0x100/0x122 를 쓰나요? MCU 에서도 효과가 있나요?</summary>

oops 의 fault 주소만 보고 "삭제된 리스트 노드를 따라갔다" 를 알 수 있다 — 이 레슨에서 일반 순회 reap 이 정확히 `0xf8`(= 0x100 − offsetof(all_node) 16 + offsetof(kill) 8)에서 죽었다. NULL 이면 다른 널 버그와 구분이 안 된다. 커널은 `POISON_POINTER_DELTA` 를 더해 사용자 공간이 매핑할 수 없는 영역으로 옮기기도 한다. Cortex-M 에선 0x0 부근이 플래시/벡터 테이블이라 읽기가 성공할 수 있어 효과가 약하다 → MPU 로 0 번지 근처 접근 금지(Zephyr 의 MPU 기반 널 포인터 검출), 또는 디버그 빌드에서 `CONFIG_DEBUG_LIST` 식 검사(B06).

</details>

<details>
<summary>Q6. FreeRTOS 는 왜 container_of 대신 pvOwner 를 쓰나요? 어느 쪽이 낫나요?</summary>

`ListItem_t` 에 `pvOwner`(소유 객체)와 `pxContainer`(소속 리스트) 포인터를 두면 매크로 트릭·컴파일러 확장 없이 어떤 C89 컴파일러에서도 동작하고, `pxContainer` 로 "지금 어느 리스트에 있나" 를 O(1) 로 물을 수 있다(리스트 길이 `uxNumberOfItems` 유지에도 필요). 대가는 아이템당 포인터 2개(Cortex-M 8 B) 추가. 또 FreeRTOS 리스트 아이템엔 정렬 키 `xItemValue` 가 있어 `vListInsert` 가 키 순서로 삽입한다(같은 키는 뒤에 — 연습 2 와 같은 안정성). Linux 식은 메모리 최소·일반성 최대, FreeRTOS 식은 이식성·자기 기술성. 면접에선 "둘 다 침입형이고, 링크 → 객체 복원 방법이 다르다" 가 핵심.

</details>

<details>
<summary>Q7. 해시 테이블 버킷에도 list_head 를 쓰나요?</summary>

Linux 는 버킷용으로 `hlist_head`(포인터 **1개**: `first`)와 `hlist_node`(`next`, `pprev` — 앞 노드의 next 필드 주소)를 따로 둔다. 버킷이 수천 개면 head 가 16 B → 8 B 로 줄어드는 것이 크고, `pprev` 덕분에 노드만으로 O(1) 삭제는 유지된다. 대가는 꼬리 접근 O(n)(버킷엔 필요 없음). LRU(DS-M05)는 해시 버킷 + 이 레슨의 list_head 를 한 객체에 같이 박는 구조다.

</details>

<details>
<summary>Q8. splice 가 O(1) 인데 왜 Linux list_head 엔 길이 필드가 없나요?</summary>

길이를 두면 remove 가 "어느 리스트에서 빠지는지" 알아야 하고(노드만으로 삭제 불가 — 또는 FreeRTOS 처럼 노드에 소속 포인터 추가), `list_cut_position` 같은 부분 이동은 개수를 세느라 O(k) 가 된다. 길이가 필요한 쪽(이 레슨의 `nr`, 블록 계층 큐 깊이 등)이 바깥에서 센다. 이 레슨의 `sched_wake_all` 이 O(k) 인 것도 "상태 필드" 를 갱신하기 때문 — 상태를 소속 리스트로만 표현하면 O(1).

</details>

## 9. 추가 연습

### 연습 1 — list_cut_position: 앞부분 잘라 가기

`list_cut_position(list, head, entry)` — head 의 첫 노드부터 `entry`(포함)까지를 빈 `list` 로 옮긴다. `entry == head` 면 아무것도 안 옮긴다. `0 1 2 3 4` 를 2 에서 → cut `0 1 2`, head `3 4`. (완료 큐에서 "여기까지 처리 완료" 묶음을 떼어 가기)

<details>
<summary>답 보기</summary>

`new_first = entry->next` 를 먼저 저장. cut 쪽: `list->next = head->next; list->next->prev = list; list->prev = entry; entry->next = list;` 남는 쪽: `head->next = new_first; new_first->prev = head;` — 포인터 6개, O(1) (entry 를 *찾는* 것은 O(k)). entry 가 마지막 노드면 new_first 가 head 자신이 되어 head 는 자연스럽게 빈 고리가 된다. 빈 head·entry == head 는 `INIT_LIST_HEAD(list)` 후 반환. practice.c 는 길이 0~20 리스트를 무작위 위치에서 2,000번 잘라 배열 모델과 비교한다.

</details>

### 연습 2 — 우선순위 정렬 삽입 (순회 끝의 pos 활용)

`insert_by_prio(head, t)` — prio 가 작을수록 앞, 같은 prio 는 먼저 온 것이 앞(FIFO). prio `3 1 3 0 1 7` 을 id 0..5 순으로 넣으면 → id `3 1 4 0 2 5`.

<details>
<summary>답 보기</summary>

```c
list_for_each_entry(pos, head, node)
    if (pos->prio > t->prio) break;      /* '>' 라서 같은 prio 뒤로 → 안정 */
list_add_tail(&t->node, &pos->node);     /* pos 앞에. 끝까지 돌았으면 &pos->node == head → 꼬리 */
```

break 하지 않으면 pos 는 가짜지만 `&pos->node` 는 정확히 head 라 `list_add_tail(…, head)` = 꼬리 삽입이 된다 — 특수 경우 0개. `>=` 로 쓰면 같은 prio 가 역순(LIFO)이 되어 실측 2개 FAIL. FreeRTOS `vListInsert` 가 같은 키를 뒤에 넣는 이유도 같다 (같은 우선순위 task 끼리 공정하게). 삽입 O(n) — 많으면 DS-M09 비트맵 + 우선순위별 리스트 배열로 O(1).

</details>

### 연습 3 — 멱등 취소: list_del_init + list_empty

`tmr_cancel(t)` — 걸려 있으면 빼고 true, 이미 취소됐거나 한 번도 안 걸렸으면 false. 두 번 불러도 안전해야 한다.

<details>
<summary>답 보기</summary>

타이머의 링크를 생성 시 `INIT_LIST_HEAD(&t->node)`(자기 고리)로 두고, 취소는 `list_del_init`. 그러면 `list_empty(&t->node)` 가 곧 "어디에도 안 걸려 있음" 이다 — 별도 플래그 불필요. `list_del` 로 지우면 poison 이 남아 두 번째 취소가 0x100 을 따라간다. Linux `timer_pending()` 이 같은 발상(링크 상태로 소속 판단)이고, 타이머 휠(DS-M06)의 cancel 이 이 함수다. ISR 에서 만료 처리와 태스크의 cancel 이 겹치면 확인과 삭제를 하나의 임계 구역에 넣어야 한다.

</details>

### 연습 4 — 중첩·배열 멤버에 container_of

```c
struct desc { uint32_t addr, len; struct { uint8_t flags; struct list_head link[2]; } q; };
```

`link[1]`(완료 큐 링크)에서 desc 를 되찾고, 완료 큐를 `list_for_each_entry(d, &doneq, q.link[1])` 로 돌아라.

<details>
<summary>답 보기</summary>

`container_of(p, struct desc, q.link[1])` — offsetof 의 member-designator 는 `.` 과 상수 `[ ]` 를 포함할 수 있다(C11 7.19p3: `&(t.q.link[1])` 가 주소 상수이면 됨). 타입 검사 `&((struct desc *)0)->q.link[1]` 도 그대로 작동. 런타임 변수 인덱스는 표준이 보장하지 않는다(GCC/Clang 은 받아 줌). 순회 매크로도 `member` 자리에 `q.link[1]` 를 그대로 넣으면 된다 — 매크로가 `pos->member` 를 텍스트로 붙이기 때문. DMA 디스크립터가 "HW 제출 큐" 와 "완료 큐" 에 동시에 걸리는 드라이버 구조.

</details>

### 연습 5 — list_replace_init: 자리 넘겨주기

`list_replace_init(old, new)` — `new` 가 `old` 의 자리(앞뒤 이웃)를 그대로 차지하고 `old` 는 자기 고리. 머리·가운데·꼬리·원소 1개 모두.

<details>
<summary>답 보기</summary>

`new->next = old->next; new->next->prev = new; new->prev = old->prev; new->prev->next = new; INIT_LIST_HEAD(old);` — 원소 1개면 old 의 앞뒤가 모두 head 라 head 의 next/prev 가 둘 다 new 로 바뀐다. 순서 유지가 중요한 큐에서 객체를 교체할 때(예: 재전송 버퍼를 새 버퍼로 바꿔 끼우기, FTL 에서 GC 로 옮긴 블록 디스크립터 교체) 떼고-넣기보다 한 번에, 순서 그대로.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/DS-M01_pr && /tmp/DS-M01_pr   # 19/19 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o /tmp/DS-M01_pas && /tmp/DS-M01_pas   # 19/19, 리포트 없음
```

## 10. 핵심 정리

- 침입형 = 객체 안에 `list_head{next, prev}`. 리스트 코드는 타입을 모르고, 할당 0, 노드 주소만으로 O(1) 삭제, 링크 수만큼 여러 리스트에 동시 소속.
- `container_of(p, T, m) = (T *)((char *)p - offsetof(T, m))` — `char *` 로 바이트 단위 뺄셈. 포터블 타입 검사는 `(void)sizeof((p) == &((T *)0)->m)` (평가 안 됨, 1회 평가 유지).
- Linux 판은 `typeof` + `__builtin_types_compatible_p` + `static_assert` 로 항상 에러. 같은 타입의 **다른 멤버 이름**은 어느 쪽도 못 잡는다.
- `list_for_each_entry` 의 끝 = `&pos->member == head`. 루프 뒤 `pos` 는 가짜 — 필드 읽기 금지 (→ Linux v5.18 gnu11).
- 순회 중 pos 삭제/이동 = `_safe`. 아니면 poison 을 따라 SEGV(@0xf8), 또는 다른 리스트로 새어 무한 루프 — 둘 다 실측.
- `list_del` = poison(재사용 버그를 시끄럽게), `list_del_init` = 자기 고리(`list_empty(node)` 로 소속 검사, 멱등 취소).
- `list_splice` 는 src 를 낡은 채 둔다 → 재사용하면 `_init`. 빈 src 는 먼저 거른다.
- 검증 = container_of 를 쓰지 않는 raw 링크 심판 + 배열 모델 + 고정 시드 랜덤 20,000 step.

## 참고자료

- [Linux include/linux/list.h — list_add, list_del, list_move, list_splice_init, list_cut_position, list_for_each_entry_safe](https://github.com/torvalds/linux/blob/master/include/linux/list.h)
- [Linux include/linux/container_of.h — container_of, container_of_const](https://github.com/torvalds/linux/blob/master/include/linux/container_of.h)
- [Linux include/linux/poison.h — LIST_POISON1/2](https://github.com/torvalds/linux/blob/master/include/linux/poison.h)
- LWN.net, "Moving the kernel to modern C" (2022-02) — list 반복자 오용 문제와 gnu11 전환 논의
- [FreeRTOS-Kernel include/list.h — ListItem_t, pvOwner, listGET_LIST_ITEM_OWNER](https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/include/list.h)
- [Zephyr include/zephyr/sys/dlist.h — sys_dlist, SYS_DLIST_FOR_EACH_CONTAINER](https://github.com/zephyrproject-rtos/zephyr/blob/main/include/zephyr/sys/dlist.h)
- ISO/IEC 9899:2011 (C11) — 6.3.2.3p7, 6.5p7, 6.5.3.4p2, 6.5.9p2, 6.7.2.1p15, 7.1.3, 7.19p3 (N1570 초안)
- DS-B06 (sentinel 이중 리스트, 불변식 검사기), 이후 DS-M05 (LRU), DS-M06 (타이머 휠)이 이 레슨의 list_head 를 쓴다
