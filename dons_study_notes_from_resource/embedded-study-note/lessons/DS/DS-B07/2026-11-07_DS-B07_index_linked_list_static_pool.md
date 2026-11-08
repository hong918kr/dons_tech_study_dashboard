---
id: DS-B07
time: 50분
updated: 2026-11-07
---
# DS-B07 인덱스 링크 리스트: 정적 노드 풀

## 학습 목표

- 노드 64개짜리 **정적 배열**을 풀(pool)로 쓰고, **free list** 로 O(1) 할당/해제를 구현한다 — `malloc` 없이.
- "다음 노드"를 포인터 대신 **`uint8_t` 배열 인덱스**로 연결하고, `NIL = 0xFF` 를 끝 표시로 쓴다.
- 하나의 풀을 여러 리스트가 공유할 때 **"모든 노드는 정확히 한 곳에 있다"** 는 불변식을 검사 함수로 확인한다 (사이클·누수·len/tail 불일치 탐지).
- `sizeof` 로 포인터 버전과 RAM 사용량을 직접 비교하고, 그 차이가 어디서 오는지(포인터 폭, 패딩) 설명한다.

## 1. 문제

펌웨어에서 "리스트가 필요하다"는 순간 `malloc` 을 쓸 수는 없다 — 대부분의 코딩 표준(MISRA C 21.3)이 금지하고, 힙은 단편화되고, 실패 시점을 예측할 수 없기 때문이다. 그래서 **노드를 미리 정적 배열로 만들어두고 빌려 쓰는** 노드 풀이 표준 해법이고, 면접에서도 "malloc 없이 링크드 리스트를 짜보라"는 단골 질문이다.

한 단계 더 나가면 **포인터 대신 인덱스**로 연결한다. 노드가 64개뿐이면 다음 노드 번호는 0~63 이라 1바이트면 충분한데, 포인터는 Cortex-M 에서 4바이트, 이 맥(64비트)에서는 8바이트다. 인덱스는 위치에 독립적이라 **그대로 플래시에 저장하거나 DMA 버퍼·공유 메모리에 놓아도** 의미가 바뀌지 않는다는 덤도 있다. SSD FTL 에서 수백만 개의 블록/캐시 엔트리를 `uint32_t` 인덱스로 엮는 것도 같은 발상이다.

```svg
<svg viewBox="0 0 740 272" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">pool.node[] 앞 8칸 — 리스트 A 와 free list 가 같은 배열을 나눠 쓴다</text>
  <g stroke="currentColor" fill="none">
    <rect x="20" y="44" width="80" height="60"/><rect x="108" y="44" width="80" height="60"/>
    <rect x="196" y="44" width="80" height="60"/><rect x="284" y="44" width="80" height="60"/>
    <rect x="372" y="44" width="80" height="60"/><rect x="460" y="44" width="80" height="60"/>
    <rect x="548" y="44" width="80" height="60"/><rect x="636" y="44" width="80" height="60"/>
  </g>
  <rect x="108" y="44" width="80" height="60" class="f-acc" stroke="currentColor"/>
  <rect x="284" y="44" width="80" height="60" class="f-acc" stroke="currentColor"/>
  <rect x="548" y="44" width="80" height="60" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="60" y="62" font-size="12">[0]</text><text x="148" y="62" font-size="12">[1]</text>
    <text x="236" y="62" font-size="12">[2]</text><text x="324" y="62" font-size="12">[3]</text>
    <text x="412" y="62" font-size="12">[4]</text><text x="500" y="62" font-size="12">[5]</text>
    <text x="588" y="62" font-size="12">[6]</text><text x="676" y="62" font-size="12">[7]</text>
    <text x="60" y="94">nx=2</text><text x="148" y="80">v=30</text><text x="148" y="96">nx=FF</text>
    <text x="236" y="94">nx=4</text><text x="324" y="80">v=10</text><text x="324" y="96">nx=6</text>
    <text x="412" y="94">nx=5</text><text x="500" y="94">nx=7</text>
    <text x="588" y="80">v=20</text><text x="588" y="96">nx=1</text><text x="676" y="94">nx=8</text>
  </g>
  <defs><marker id="b07a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <path d="M324 106 Q456 150 586 108" stroke="currentColor" fill="none" marker-end="url(#b07a)"/>
  <path d="M588 106 Q368 196 150 108" stroke="currentColor" fill="none" marker-end="url(#b07a)"/>
  <g fill="currentColor">
    <text x="10" y="190">리스트 A: head=3 → [3] 10 → [6] 20 → [1] 30 → NIL(0xFF)   (색칠한 칸, 곡선 화살표)</text>
    <text x="10" y="212">free list: free_head=0 → [0] → [2] → [4] → [5] → [7] → [8] → … → [63] → NIL</text>
    <text x="10" y="234">free 노드는 val 이 의미 없고 next 만 쓴다 — 같은 next 필드가 "빈 칸 목록"의 고리가 된다.</text>
    <text x="10" y="256">노드 = val(2B) + next(1B) + 패딩(1B) = 4B.  포인터 버전은 64비트에서 16B, 32비트에서 8B.</text>
  </g>
</svg>
```

자료형은 시작 코드에 정해져 있다.

```c
#define POOL_N 64u
#define NIL    0xFFu
typedef struct { uint16_t val; uint8_t next; } inode_t;
typedef struct { inode_t node[POOL_N]; uint8_t free_head, free_cnt; } pool_t;
typedef struct { uint8_t head, tail, len; } ilist_t;
```

### Part A — 정적 노드 풀과 free list

- `pool_init(p)` — 모든 노드를 `0 → 1 → … → 63 → NIL` 로 엮어 free list 를 만든다. `free_head = 0`, `free_cnt = 64`.
- `node_alloc(p)` — free list 의 **머리에서 하나 꺼낸다** (pop). 비었으면 `NIL`.
- `node_free(p, i)` — 노드 `i` 를 free list **머리에 다시 넣는다** (push). 그래서 마지막에 반납한 노드가 다음에 먼저 나온다 (LIFO).

### Part B — 리스트 연산 (풀 하나, 리스트 여러 개)

`ilist_t` 는 `head`, `tail`, `len` 3바이트뿐이다. 노드는 모두 같은 풀에서 빌린다.

- `list_push_front` / `list_push_back` — 풀이 비면 `false`. `tail` 덕분에 push_back 도 O(1).
- `list_pop_front(p, l, &out)` — 맨 앞 값을 꺼내고 노드를 풀에 반납.
- `list_remove(p, l, v)` — 값이 `v` 인 **첫** 노드를 떼어 반납. head / 중간 / tail 세 경우 모두 처리.
- `list_to_array(p, l, out, max)` — `head` 에서 `next` 를 따라 `NIL` 까지 순회하며 값을 복사.

### Part C — 불변식 검사와 RAM 비교

- `pool_check(p, lists, n)` — free list 와 모든 리스트를 걸으며 `seen[]` 에 표시한다. 다음 중 하나라도 있으면 `false`: 범위 밖 인덱스, 이미 본 노드 다시 만남(사이클 또는 두 리스트가 노드 공유), 걸은 개수 ≠ `len`/`free_cnt`, 마지막 노드 ≠ `tail`, 전체 개수 ≠ 64(누수). **고장 난 리스트에서도 무한 루프 없이** 끝나야 한다.
- 테스트는 리스트 3개에 대해 랜덤 연산 20,000번을 하며 평범한 배열로 만든 참조 모델과 매번 비교하고, 매번 `pool_check` 를 돌린다 (고정 시드 xorshift32).
- `ram_report()` (주어짐) 가 `sizeof` 로 인덱스 버전과 포인터 버전의 노드·풀·헤더 크기를 출력한다. 이 숫자를 읽고 차이를 설명하라.

## 2. Problem (English)

> **Index-linked list on a static node pool (no malloc).**
>
> You have a pool of 64 nodes `{ uint16_t val; uint8_t next; }` where `next` is an array index and `0xFF` means "none".
>
> 1. Implement `pool_init`, `uint8_t node_alloc(pool_t*)` (returns `0xFF` when exhausted) and `void node_free(pool_t*, uint8_t)` in O(1) using an intrusive free list threaded through the `next` field.
> 2. Implement a singly linked list `{ uint8_t head, tail, len; }` on top of the pool: `push_front`, `push_back` (O(1)), `pop_front`, `remove(value)` (first match), and `to_array` (traversal). Several lists share one pool.
> 3. Write `bool pool_check(...)` that verifies every node is in exactly one place (free list or one list), `len`/`tail`/`free_cnt` are consistent, and terminates on a corrupted (cyclic) list.
> 4. Compare the RAM footprint against a pointer-based list with `sizeof`, on a 64-bit host and for a 32-bit MCU.
>
> Example: after `push_back 1,2,3; push_front 0; remove 2` the list is `[0,1,3]`; removing `3` must update `tail` so that a later `push_back 9` gives `[0,1,9]`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 FAIL 줄이 쭉 나오고 "7/27 passed" (false/NIL 을 기대하는 검사와 RAM 크기 비교만 우연히 통과) — TODO를 채워 27/27 을 만든다
```

> 맥 SDK 27.2에서 `ld: tapi error` 가 나면 `export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk` 후 다시 빌드.

## 4. 힌트

<details>
<summary>힌트 1 — free list 는 그냥 "스택"이다</summary>

free 노드의 `next` 필드를 그대로 빈 칸 목록의 고리로 쓴다(침입형, intrusive). alloc = 스택 pop (`i = free_head; free_head = node[i].next`), free = 스택 push (`node[i].next = free_head; free_head = i`). 둘 다 분기 하나, O(1).

</details>

<details>
<summary>힌트 2 — 빈 리스트일 때 tail</summary>

빈 리스트는 `head == tail == NIL`. push_front 로 첫 노드를 넣을 때 tail 도 그 노드가 되어야 하고, push_back 은 `tail == NIL` 이면 head 를 바꾸고 아니면 `node[tail].next` 를 바꾼다. pop 으로 마지막 노드를 꺼내면 tail 을 다시 `NIL` 로.

</details>

<details>
<summary>힌트 3 — 단일 연결 리스트에서 삭제하려면 prev 가 필요하다</summary>

`for (i = head; i != NIL; prev = i, i = node[i].next)` 로 `prev` 를 같이 들고 간다. `prev == NIL` 이면 head 를 지우는 것이고, 지우는 노드가 `tail` 이면 `tail = prev`.

</details>

<details>
<summary>힌트 4 — pool_check 가 무한 루프에 안 빠지려면</summary>

`uint8_t seen[64]` 을 0으로 시작해서, 노드를 방문할 때마다 "이미 1인가?"를 먼저 본다. 이미 봤으면 사이클이거나 두 곳에 걸린 노드 → 즉시 `false`. 방문할 수 있는 노드는 64개뿐이라 루프는 최대 65번 돈다. 인덱스가 `>= 64` 인지도 **배열 접근 전에** 검사.

</details>

## 5. 풀이

### Part A — free list = next 필드로 엮은 스택

`pool_init` 는 `node[i].next = i + 1` (마지막만 `NIL`) 로 끝난다. 이후 alloc/free 는 free list 머리만 만진다.

| 단계 | 연산 | free_head | free list | 돌려준 값 |
|---|---|---|---|---|
| 0 | `pool_init` | 0 | 0→1→2→…→63 | — |
| 1 | `node_alloc` | 1 | 1→2→…→63 | 0 |
| 2 | `node_alloc` | 2 | 2→…→63 | 1 |
| 3 | `node_free(0)` | 0 | 0→2→…→63 | — |
| 4 | `node_alloc` | 2 | 2→…→63 | 0 (방금 반납한 것) |

- 64번 할당 후 `free_head == NIL` → 65번째는 `NIL`. 테스트의 `node_free(10); node_free(20);` 다음 alloc 이 20, 10 순서로 나오는 것도 LIFO 이기 때문이다.
- LIFO 는 **캐시에 유리**하다: 방금 반납한 노드는 아직 캐시에 있을 확률이 높다 (Linux slab, FreeRTOS 의 고정 블록 풀도 같은 방식).
- alloc 에서 `node[i].next = NIL` 로 지워두는 건 필수는 아니지만, 꺼낸 노드가 free list 꼬리를 가리키는 "유령 링크"를 남기지 않게 해서 디버깅을 쉽게 한다.

### Part B — 리스트 연산

인덱스 버전은 포인터 버전과 **코드 모양이 똑같다.** `n->next` 가 `p->node[i].next` 로, `NULL` 이 `NIL` 로 바뀔 뿐이다.

| 포인터 버전 | 인덱스 버전 |
|---|---|
| `struct node *n = malloc(...)` | `uint8_t i = node_alloc(p)` |
| `n->next = head; head = n;` | `p->node[i].next = l->head; l->head = i;` |
| `for (n = head; n; n = n->next)` | `for (i = l->head; i != NIL; i = p->node[i].next)` |
| `free(n)` | `node_free(p, i)` |

삭제는 head / 중간 / tail 세 경우가 있다. 아래 그림은 리스트 `[0,1,3]`(인덱스 0→1→3 이라 하자) 에서 tail 인 3을 지우는 경우다.

```svg
<svg viewBox="0 0 720 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">list_remove — prev 를 들고 가다가 i 를 건너뛰게 연결</text>
  <defs><marker id="b07b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <g stroke="currentColor" fill="none">
    <rect x="40" y="50" width="100" height="40"/><rect x="220" y="50" width="100" height="40"/>
    <rect x="400" y="50" width="100" height="40"/><rect x="580" y="50" width="100" height="40"/>
  </g>
  <rect x="400" y="50" width="100" height="40" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="90" y="75">head: [0] 0</text><text x="270" y="75">prev: [1] 1</text>
    <text x="450" y="75">i: [3] 3</text><text x="630" y="75">NIL</text>
  </g>
  <path d="M140 70 L216 70" stroke="currentColor" marker-end="url(#b07b)"/>
  <path d="M320 70 L396 70" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#b07b)"/>
  <path d="M500 70 L576 70" stroke="currentColor" marker-end="url(#b07b)"/>
  <path d="M270 92 Q450 150 628 94" stroke="currentColor" fill="none" marker-end="url(#b07b)"/>
  <g fill="currentColor">
    <text x="300" y="140">① node[prev].next = node[i].next  (= NIL)</text>
    <text x="10" y="170">② i 가 tail 이었으니 tail = prev(=1). 빼먹으면 tail 이 반납된 노드 3을 계속 가리킨다.</text>
    <text x="10" y="192">③ len--, node_free(p, 3) — 노드 3은 free list 머리로.</text>
    <text x="10" y="214">prev == NIL 이었다면(지울 노드가 head) ① 대신 head = node[i].next.</text>
  </g>
</svg>
```

②를 빼먹으면 생기는 일이 이 레슨의 핵심 버그다. 테스트의 `remove(3)` 다음 `push_back(9)` 는 `node[tail].next = 새 노드` 를 하는데, tail 이 이미 free list 에 들어간 노드 3을 가리키고 있으면 **free list 를 리스트에 이어 붙여버린다**. 결과는 `[0,1]` 뒤에 9가 안 보이거나, 나중에 같은 노드가 두 번 할당되는 오염이다. 랜덤 스트레스 + `pool_check` 는 이런 버그를 수천 단계 안에 잡아낸다.

`list_to_array` 의 `n < max` 조건은 "out 버퍼 넘침"을 막는 동시에, 리스트가 오염되어 사이클이 생겨도 순회가 끝나게 해주는 안전장치다.

### Part C — 불변식: "모든 노드는 정확히 한 곳에"

풀을 공유하는 자료구조의 정합성은 한 문장으로 요약된다: **64개 노드 각각이 free list 또는 리스트 하나에 정확히 한 번 나타난다.** `pool_check` 는 이를 그대로 검사한다.

| 검사 | 잡아내는 버그 |
|---|---|
| `i >= POOL_N` | 초기화 안 된 next, `NIL` 대신 64 로 끝낸 free list |
| `seen[i]` 이미 1 | 사이클(무한 루프 원인), double free, 두 리스트가 노드 공유 |
| 걸은 개수 ≠ `len` / `free_cnt` | 카운터 갱신 누락 |
| 마지막 노드 ≠ `tail` | Part B 의 tail 갱신 누락 |
| 합계 ≠ 64 | 누수 (alloc 하고 어디에도 안 붙인 노드) |

테스트는 일부러 사이클을 만들고(`node[tail].next = head`), 일부러 노드를 하나 새게 해서 `false` 가 나오는지도 본다. 펌웨어에서는 이 함수를 디버그 빌드의 **assert 훅**이나 유휴 태스크에서 주기적으로 돌린다.

랜덤 스트레스는 참조 모델(그냥 `uint16_t ref[3][64]` 배열에 shift 로 넣고 빼는 느린 구현)과 매 단계 비교한다. 느리지만 틀릴 수 없는 구현을 정답지로 쓰는 것 — **차분 테스트(differential testing)** 다.

### Part C — RAM 비교: 실제 sizeof

`ram_report()` 를 이 맥(arm64, 포인터 8B)에서 실행한 실제 출력이다. "32-bit model" 은 포인터 자리에 `uint32_t` 를 넣은 구조체로 Cortex-M 의 레이아웃을 흉내낸 것이다.

```text
== RAM 비교 (POOL_N=64, payload uint16_t) ==
layout                          node B   pool B  head B
index  (uint8_t next)                4      256       3
pointer (this host)                 16     1024      24
pointer (32-bit model)               8      512      12
sizeof(pool_t) = 258 (노드 + free_head + free_cnt)
```

| 레이아웃 | 노드 | 64개 풀 | 리스트 헤더 | 인덱스 대비 |
|---|---|---|---|---|
| 인덱스 (`uint8_t next`) | 4 B | 256 B | 3 B | 1× |
| 포인터, 32비트 MCU (Cortex-M) | 8 B | 512 B | 12 B | 2× |
| 포인터, 64비트 호스트 (이 맥) | 16 B | 1024 B | 24 B | 4× |
| 인덱스 SoA (`val[64]` + `next[64]`, 추가 연습 3) | 3 B | 192 B | 3 B | 0.75× |

```svg
<svg viewBox="0 0 720 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">64노드 풀의 RAM (막대 길이 = 바이트, 0.6 px/B)</text>
  <g fill="currentColor">
    <text x="10" y="58">index SoA</text><text x="10" y="90">index AoS</text>
    <text x="10" y="122">ptr 32-bit</text><text x="10" y="154">ptr 64-bit</text>
  </g>
  <rect x="100" y="42" width="115" height="22" class="f-ok"/>
  <rect x="100" y="74" width="154" height="22" class="f-acc"/>
  <rect x="100" y="106" width="307" height="22" class="f-warn"/>
  <rect x="100" y="138" width="614" height="22" class="f-mut"/>
  <g fill="currentColor" font-family="monospace">
    <text x="222" y="58">192</text><text x="261" y="90">256</text>
    <text x="414" y="122">512</text><text x="620" y="154">1024</text>
  </g>
  <text x="10" y="190" fill="currentColor">AoS 노드 4B 중 1B 는 패딩 — uint16_t 정렬(2B) 때문에 3B 구조체가 4B 로 커진다.</text>
</svg>
```

차이의 원인은 두 가지다.

1. **링크 폭**: 1B 인덱스 vs 4B/8B 포인터.
2. **패딩**: 구조체 크기는 가장 큰 멤버 정렬의 배수다. `{uint16_t; uint8_t}` 는 3B 지만 2의 배수로 맞춰 4B, `{uint16_t; 포인터 8B}` 는 10B 지만 8의 배수로 맞춰 16B. 포인터 버전은 패딩도 같이 커진다.

STM32G0 (RAM 8~36 KB) 에서 512B vs 256B 는 작아 보여도, 이런 풀이 UART 명령 큐·타이머·이벤트마다 하나씩 있으면 수 KB 차이가 된다. 대가는 ① 노드 수 상한(uint8_t 면 255, `NIL` 하나 빼고) ② 접근할 때 `base + i*4` 주소 계산 한 번(Cortex-M 에서 `LDRB` + `ADD … LSL #2`, 사실상 공짜) ③ 풀 포인터 `p` 를 늘 같이 넘겨야 한다는 점.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 27/27 passed
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g solution.c -o sol_asan && ./sol_asan   # 깨끗해야 함
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `pool_init` 에서 `next = i + 1` 만 하고 끝 | 마지막 노드 next 가 64 → 범위 밖 노드를 할당 | 마지막만 `NIL`: `(i + 1 < POOL_N) ? i + 1 : NIL` |
| tail 노드를 지우고 `tail` 갱신 안 함 | 다음 push_back 이 **반납된 노드**에 연결 → free list 와 리스트가 엉킴 | `if (l->tail == i) l->tail = prev;` |
| 마지막 노드를 pop 하고 `tail` 그대로 | 빈 리스트인데 tail ≠ NIL → 같은 오염 | `if (l->head == NIL) l->tail = NIL;` |
| `node_free` 후 그 인덱스를 계속 사용 | 정적 배열 안이라 **ASan 도 못 잡는** use-after-free. 이미 다른 리스트 노드일 수 있다 | free 직후 지역 변수에 `NIL` 대입, 디버그 빌드에서 `val = 0xDEAD` 포이즌 |
| `for (uint8_t i = 0; i < 256; i++)` | `uint8_t` 는 256 이 될 수 없어 무한 루프 (clang/gcc 가 대개 경고) | 루프 변수는 `unsigned`, 노드 번호만 `uint8_t` |
| `POOL_N` 을 256 으로 늘림 | 255 번 노드가 `NIL` 과 같은 값 | 255 개까지만, 그 이상은 `uint16_t` 인덱스 + `NIL = 0xFFFF` |
| 순회에 상한 없음 | 오염된 리스트에서 영원히 돈다 (워치독 리셋) | `n < max` 또는 `seen[]` 검사 |

## 8. 면접 Q&A

<details>
<summary>Q1. malloc 대신 정적 노드 풀을 쓰는 이유를 말해보세요.</summary>

① **결정성**: alloc/free 가 분기 하나짜리 O(1) — 최악 실행 시간이 고정 (RTOS 데드라인 분석 가능). ② **단편화 없음**: 노드 크기가 하나라 외부 단편화가 생길 수 없다. ③ **RAM 이 링크 시점에 확정**: `.bss` 에 잡히므로 map 파일로 총량을 안다. ④ 코딩 표준 (MISRA C:2012 Rule 21.3) 준수. 꼬리질문 "풀이 바닥나면?" → 에러 반환 + 카운터 증가, 최저 잔량(high-water mark) 기록으로 크기를 실측해서 정한다. 절대 조용히 실패하지 않는다.

</details>

<details>
<summary>Q2. 포인터 대신 인덱스로 연결하면 어떤 장단점이 있나요?</summary>

장점: 링크 폭이 1~2B 라 RAM 절약(이 레슨 기준 32비트 MCU 에서 절반), **위치 독립적**이라 풀 전체를 플래시에 저장/복원하거나 다른 주소에 매핑된 공유 메모리·DMA 영역에 놓아도 링크가 유효, 범위 검사(`i < POOL_N`)로 손상 탐지가 쉬움. 단점: 노드 수 상한, 풀 base 를 항상 알아야 함, 서로 다른 풀의 노드를 한 리스트에 섞을 수 없음. FTL 매핑 테이블·LRU 캐시처럼 엔트리가 수백만 개인 곳에서는 64비트 포인터 대신 `uint32_t` 인덱스가 DRAM 을 절반으로 줄인다.

</details>

<details>
<summary>Q3. ISR 과 메인 루프가 같은 풀에서 alloc/free 하면 무엇이 문제이고 어떻게 막나요?</summary>

`node_alloc` 은 `free_head` 읽기 → `next` 읽기 → `free_head` 쓰기 의 RMW 라, 중간에 ISR 이 alloc 하면 같은 노드가 두 번 나간다. 가장 간단한 해법은 짧은 임계 구역 (`primask = __get_PRIMASK(); __disable_irq(); … __set_PRIMASK(primask);`) — 몇 사이클이라 인터럽트 지연이 미미하다. lock-free 로 하려면 CAS 스택(Treiber stack)인데 **ABA 문제**가 생긴다. 인덱스의 장점이 여기서 나온다: `{uint8_t idx; uint8_t tag;}` 를 16비트로 묶어 tag 를 매번 증가시키면 `LDREXH/STREXH` 한 번으로 ABA 를 막을 수 있다 (포인터+태그는 64비트 CAS 가 필요).

</details>

<details>
<summary>Q4. 리스트가 오염됐는지 런타임에 어떻게 검사하나요? 사이클은요?</summary>

이 레슨의 `pool_check` 처럼 방문 표시 배열로 "모든 노드가 정확히 한 번" 을 확인한다 (노드 64개면 64B 스택). 표시 배열을 쓸 수 없으면 Floyd 의 토끼와 거북이(두 인덱스, 하나는 2칸씩)로 O(1) 메모리 사이클 검출. 실무에서는 디버그 빌드 assert, 유휴 태스크의 주기 검사, 그리고 노드에 magic 값/canary 를 두는 방법을 함께 쓴다.

</details>

<details>
<summary>Q5. 구조체 {uint16_t val; uint8_t next;} 의 sizeof 는? 왜 3이 아닌가요?</summary>

4. 구조체는 배열로 놓였을 때 모든 원소의 `val` 이 2바이트 정렬되도록 크기를 가장 큰 멤버 정렬(2)의 배수로 올린다 → 꼬리에 1B 패딩. 이를 없애려면 ① SoA (`val[64]`, `next[64]` 분리 → 192B) ② `__attribute__((packed))` (비정렬 접근이 생겨 Cortex-M0 에서는 HardFault 위험, M3+ 에서는 느려짐) ③ 멤버를 큰 것부터 배치 (여기선 효과 없음). 면접 꼬리질문 단골: `{char; int; char;}` 는 12, `{int; char; char;}` 는 8.

</details>

<details>
<summary>Q6. 단일 연결 리스트에서 노드 삭제가 O(n) 인데, O(1) 로 하려면?</summary>

삭제할 노드의 앞 노드를 알아야 하기 때문에 O(n). 해결: ① 이중 연결(인덱스라면 `uint8_t prev` 1B 추가 — 포인터였다면 8B) ② 호출자가 순회 중이라 prev 를 이미 알고 있으면 `remove_after(prev)` API 제공. Linux `list_head` 가 이중 원형 리스트인 이유가 이것이다 (DS-B06 sentinel 리스트 참고).

</details>

## 9. 추가 연습

### 연습 1 — 제자리 뒤집기

`list_reverse(p, l)` 을 노드를 새로 할당하지 않고 `next` 인덱스만 바꿔서 구현하라. `tail` 도 맞아야 뒤집은 뒤 push_back 이 동작한다.

<details>
<summary>답 보기</summary>

`prev = NIL, cur = head` 에서 시작해 `nxt = next[cur]; next[cur] = prev; prev = cur; cur = nxt;` 반복. 시작 전에 `tail = head` (옛 head 가 새 tail), 끝나면 `head = prev`. 빈 리스트는 루프가 안 돌고 head/tail 모두 NIL 유지.

</details>

### 연습 2 — double free 탐지

풀에 `uint64_t used` 비트맵(노드당 1비트)을 두고, `node_free_safe(p, i)` 가 범위 밖·이미 free 된 노드·한 번도 할당 안 된 노드면 `false` 를 돌려주게 하라.

<details>
<summary>답 보기</summary>

alloc 에서 `used |= 1ull << i`, free 에서 먼저 `if (i >= POOL_N || !(used & (1ull << i))) return false;` 검사 후 비트를 지운다. 노드 64개라 `uint64_t` 하나로 딱 맞는다 — 64B 짜리 `bool[64]` 대신 8B. (비트맵은 DS-B08 에서 본격적으로.) double free 를 그냥 두면 같은 노드가 free list 에 두 번 들어가 **두 번 할당**된다.

</details>

### 연습 3 — SoA 풀로 패딩 없애기

노드를 `uint16_t val[64]; uint8_t next[64];` 두 배열로 나눈 풀(`soa_pool_t`)에 alloc/free 를 구현하고 크기를 비교하라.

<details>
<summary>답 보기</summary>

코드는 `p->node[i].next` 가 `p->next[i]` 로 바뀌는 것뿐. 실행 결과: `AoS inode_t[64] = 256 B, SoA val[]+next[] = 192 B (sizeof(soa_pool_t)=194)`. 패딩 64B 가 사라진다. 순회는 `next[]` 64B 만 읽으면 되므로 캐시에도 더 유리하다 (링크만 따라가고 값은 마지막에 볼 때).

</details>

### 연습 4 — 정렬 삽입 (타이머 리스트)

`list_insert_sorted(p, l, v)` — 값(마감 시각) 오름차순을 유지하며 삽입. 같은 값이면 **먼저 넣은 것이 앞**(안정, FIFO)이어야 한다.

<details>
<summary>답 보기</summary>

`while (cur != NIL && val[cur] <= v)` 로 진행 — `<` 가 아니라 `<=` 여야 같은 값 뒤에 들어가서 안정적이다. 끼울 자리에서 `prev == NIL` 이면 head 갱신, `cur == NIL` 이면 tail 갱신. RTOS 소프트웨어 타이머 리스트가 정확히 이 구조이고(삽입 O(n), 만료 처리 O(1) — 맨 앞만 보면 됨), 타이머가 많아지면 DS 트랙 뒤쪽의 타이머 휠로 간다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 16/16 passed
```

## 10. 핵심 정리

- 노드 풀 = 정적 배열 + free list. free list 는 free 노드의 `next` 로 엮은 **스택** → alloc/free 모두 O(1), malloc 없음.
- 링크를 `uint8_t` 인덱스로, 끝은 `NIL = 0xFF`. 노드 최대 255개.
- 빈 리스트는 `head == tail == NIL`. tail 을 지우거나 마지막 노드를 pop 할 때 **tail 갱신**을 잊지 말 것.
- 불변식: 모든 노드는 free list 또는 리스트 하나에 **정확히 한 번**. `seen[]` 로 사이클·공유·누수·카운터 오류를 한 번에 잡는다.
- 실측 (64노드, uint16_t 값): 인덱스 256B / 포인터 32비트 512B / 포인터 64비트 1024B. 패딩 때문에 SoA 로 나누면 192B.
- 인덱스는 위치 독립적 → 플래시 저장, 공유 메모리, 16비트 tag 를 붙인 lock-free CAS 에 유리.
- 정적 풀 안의 use-after-free 는 ASan 이 못 잡는다 → 불변식 검사·포이즌 값으로 직접 지킨다.

## 참고자료

- [Linux kernel include/linux/list.h](https://github.com/torvalds/linux/blob/master/include/linux/list.h) — 침입형 리스트의 원형
- [FreeRTOS — Memory Management (heap_1 ~ heap_5, 정적 할당)](https://www.freertos.org/a00111.html)
- MISRA C:2012 Rule 21.3 — `malloc`/`free` 사용 금지
- Jeff Bonwick, *The Slab Allocator: An Object-Caching Kernel Memory Allocator* (USENIX 1994) — LIFO free list 와 캐시
- R. Kent Treiber, *Systems Programming: Coping with Parallelism* (IBM RJ 5118, 1986) — lock-free 스택과 ABA
- Introduction to Algorithms (CLRS) 10.3 — "Implementing pointers and objects" (배열로 구현한 연결 리스트와 free list)
