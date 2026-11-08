---
id: DS-M04
time: 90분
updated: 2026-11-07
---
# DS-M04 버디 할당기: 분할과 병합

## 학습 목표

- 2의 거듭제곱 블록을 **반으로 쪼개고(split)** 다시 **짝(buddy)과 합치는(merge)** 버디 할당기를 malloc 없이 64 KB 정적 아레나 위에 구현한다.
- 버디 주소가 왜 **`오프셋 XOR 블록 크기`** 한 줄로 나오는지 정렬(alignment) 성질로 증명하고, 트리 노드 번호(`node ^ 1`, `node >> 1`)와 같은 식임을 안다.
- order 별 free list 배열 + 비트맵(free 상태, 할당 머리)으로 "버디가 **같은 order 로** free 인가"를 O(1)에 판정하고, `free(ptr)` 만으로 블록 크기를 찾는다.
- 불변식 검사기 + 참조 모델(unit 소유 배열) 대조 랜덤 테스트로 검증하고, 모두 해제하면 **order 10 블록 하나로 완전 복원**됨을 확인한다.
- 외부 단편화 지표 `1 − largest_free / total_free` 와 내부 단편화를 실제 워크로드에서 재고, "총량은 충분한데 할당 실패"를 직접 만들어 본다.

## 1. 문제

Linux 커널의 물리 페이지 할당기(page allocator)가 버디 할당기이고, 그 위에 slab(DS-M03)이 올라간다 — "`alloc_pages(order)` 는 내부에서 어떻게 동작하나요?", "free 할 때 이웃을 어떻게 찾나요?" 는 커널·펌웨어 면접의 단골 꼬리질문이다. 임베디드에서는 DMA 버퍼처럼 **크고, 크기가 제각각이고, 정렬이 필요한** 메모리를 결정적인 시간 안에 나눠 줄 때 쓴다.

**버디 시스템(buddy system)**: 메모리를 2^k 크기 블록으로만 나눈다. 크기 2^(k+1) 블록을 반으로 자르면 나오는 두 조각이 서로의 **버디**다. 할당은 "필요하면 반으로 계속 자르기", 해제는 "버디도 비어 있으면 다시 붙이기"다. 이 레슨의 설정:

| 항목 | 값 |
|---|---|
| order 0 (최소 블록, unit) | 64 B |
| order 10 (최대 블록) | 64 KB = 아레나 전체 = 1024 unit |
| free list | `head[0..10]` — order 별 sentinel 원형 이중 리스트 (DS-B06). 링크는 **free 블록 자신의 앞 16 B 에 내장** |
| 비트맵 | `freemap` (블록이 free list 에 있음), `allocmap` (할당된 블록의 머리) — 각 2048 비트 = 256 B (DS-B08) |
| `avail` | bit k = free list k 가 비어 있지 않음 → `ctz` 로 O(1) 탐색 |

비트 번호는 **완전 이진 트리 노드 번호**다. 블록 하나 = 트리 노드 하나라는 걸 그림으로 보면 버디·부모 계산이 공짜로 나온다 (DS-B12 힙의 1-based 인덱스와 같다).

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">버디 트리 = 비트 번호. order k, unit u 블록의 노드 n = (1024 &gt;&gt; k) + (u &gt;&gt; k)</text>
  <g stroke="currentColor" fill="none">
    <path d="M370 70 L190 100"/><path d="M370 70 L550 100"/>
    <path d="M190 130 L95 160"/><path d="M190 130 L275 160"/>
    <path d="M550 130 L455 160"/><path d="M550 130 L635 160"/>
  </g>
  <rect x="230" y="40" width="280" height="30" rx="5" class="f-acc" stroke="currentColor"/>
  <g class="f-mut" stroke="currentColor">
    <rect x="80" y="100" width="220" height="30" rx="5"/><rect x="440" y="100" width="220" height="30" rx="5"/>
    <rect x="20" y="160" width="150" height="30" rx="5"/><rect x="200" y="160" width="150" height="30" rx="5"/>
    <rect x="380" y="160" width="150" height="30" rx="5"/><rect x="560" y="160" width="150" height="30" rx="5"/>
    <rect x="20" y="225" width="60" height="28" rx="4"/><rect x="80" y="225" width="60" height="28" rx="4"/>
    <rect x="140" y="225" width="60" height="28" rx="4"/><rect x="200" y="225" width="60" height="28" rx="4"/>
    <rect x="560" y="225" width="60" height="28" rx="4"/><rect x="620" y="225" width="60" height="28" rx="4"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="370" y="60">n=1  order 10  u 0–1023</text>
    <text x="190" y="120">n=2  order 9  u 0–511</text><text x="550" y="120">n=3  order 9  u 512–1023</text>
    <text x="95" y="180">n=4  u 0–255</text><text x="275" y="180">n=5  u 256–511</text>
    <text x="455" y="180">n=6  u 512–767</text><text x="635" y="180">n=7  u 768–1023</text>
    <text x="50" y="244">1024</text><text x="110" y="244">1025</text><text x="170" y="244">1026</text><text x="230" y="244">1027</text>
    <text x="590" y="244">2046</text><text x="650" y="244">2047</text>
    <text x="400" y="244">… order 0: n = 1024 + u …</text>
    <text x="370" y="212">⋮</text>
  </g>
  <g fill="currentColor">
    <text x="10" y="282">버디 노드 = n ^ 1   ⇔   버디 unit = u ^ (1 &lt;&lt; k)   (바이트로는 offset ^ 블록 크기)</text>
    <text x="10" y="304">부모 노드 = n &gt;&gt; 1  ⇔  합친 시작 = u &amp; ~(1 &lt;&lt; k).  노드 1..2047 → 비트맵 256 B</text>
    <text x="10" y="324">예: unit 3 (order 0) = n 1027, 버디 1026 = unit 2. 합치면 부모 513 = order 1, unit 2</text>
  </g>
</svg>
```

API (모두 `buddy_t *b` 를 받는다, 전역 malloc 없음):

```c
int   size_to_order(size_t size);                  /* 0 또는 > 64 KB → -1 */
void *buddy_alloc_order(buddy_t *b, uint32_t order);
void *buddy_alloc(buddy_t *b, size_t size);        /* = alloc_order(size_to_order(size)) — 주어짐 */
bool  buddy_free(buddy_t *b, void *p);             /* 크기 인자 없음. 잘못된 포인터면 false */
void  buddy_stats(const buddy_t *b, bstats_t *st); /* free_bytes, largest_free, nblocks, frag */
```

### Part A — size → order, 할당과 분할

- `size_to_order(size)` — 요청을 담는 가장 작은 order. `units = ceil(size/64)`, `order = ceil(log2(units))`. 0 바이트·64 KB 초과는 -1.
- `buddy_alloc_order(b, order)` — `avail` 에서 order 이상인 가장 작은 비어 있지 않은 order k 를 `ctz` 로 찾고, 블록을 꺼내 k > order 인 동안 **반으로 쪼개 위쪽 절반을 free list 로** 돌려준다. `allocmap` 에 머리 표시.
- 테스트: 크기 표 12개(0, 1, 64, 65, …, `SIZE_MAX`), 빈 아레나에서 64 B 하나 → order 0..9 에 블록이 하나씩, 이어서 100 B / 64 B / 1000 B 가 unit 2 / 1 / 16 에 오는지, 64 KB 통째 할당, 64 B × 1024 로 꽉 채운 뒤 1025번째 거절.

### Part B — 해제와 버디 병합, 잘못된 해제 거절

- `buddy_free(b, p)` — ① NULL·아레나 밖·64 B 경계 아님 → false. ② `allocmap` 에서 이 unit 에서 시작하는 할당 블록의 order 를 찾는다 (없으면 이중 해제나 블록 내부 포인터 → false). ③ 버디 `u ^ (1<<k)` 가 **같은 order 로** free 면 리스트에서 빼고 합쳐 한 order 위로, 반복. ④ 최종 블록을 push.
- 테스트: 64 B 네 개를 1, 2, 0, 3 순서로 풀며 병합 단계 추적, NULL·스택 변수·`p+32`·블록 내부·미할당 unit·이중 해제 거절(상태 불변), 참조 모델 대조 랜덤 30000회 + 남은 블록 무작위 순서 전부 해제 → **order 10 블록 하나**.

### Part C — 단편화 지표와 펌웨어 워크로드

- `buddy_stats(b, st)` — free 바이트 합, 가장 큰 free 블록, free 블록 수, **외부 단편화 `frag = 1 − largest_free / free_bytes`** (free 가 0 이면 0).
- 워크로드 (네트워크 MCU 가정): 매 step 패킷 버퍼(64~1518 B, 수명 1~48 step)를 하나 할당하고, 0.6% 확률로 DMA 프레임(4~16 KB, 수명 100~399 step). 2500 step 마다 내부/외부 단편화와 "**총 free 는 블록 크기 이상인데 실패**" 횟수를 출력. 호출당 free list 연산 수 최댓값도 잰다(결정성).
- 체커보드: 64 B × 1024 를 할당하고 짝수 번째만 해제 → 32 KB 가 비어 있어도 128 B 를 못 준다.

## 2. Problem (English)

> **Binary buddy allocator over a static 64 KB arena.**
>
> The minimum block is 64 bytes (order 0) and the maximum is the whole arena (order 10). Keep one free list per order and bitmaps for block state; no dynamic allocation. Free blocks store their list links inside themselves.
>
> 1. `int size_to_order(size_t size)` — smallest order whose block holds `size`; `-1` for `0` or `size > 65536`.
> 2. `void *buddy_alloc_order(buddy_t *b, uint32_t order)` — take the smallest non-empty free list of order `>= order`, split down, return the block (or `NULL`). Must be `O(MAX_ORDER)`.
> 3. `bool buddy_free(buddy_t *b, void *p)` — no size argument. Reject `NULL`, out-of-arena, misaligned, interior, and double-freed pointers by returning `false` without changing state. Otherwise coalesce with the buddy (`offset XOR block_size`) while it is free at the same order.
> 4. `void buddy_stats(const buddy_t *b, bstats_t *st)` — total free bytes, largest free block, number of free blocks, and external fragmentation `1 - largest/total`.
> 5. Prove (by test) that after any sequence of operations, freeing every live block restores a single order-10 block.
>
> Example: fresh arena, `alloc(64)` → unit 0, free-list counts per order `[1,1,1,1,1,1,1,1,1,1,0]`. Then `alloc(100)` → unit 2, `alloc(64)` → unit 1, `alloc(1000)` → unit 16.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "5/22 passed", exit 1 — TODO 4곳을 채워 22/22 를 만든다
```

TODO: `size_to_order`, `buddy_alloc_order` (Part A) / `buddy_free` (Part B) / `buddy_stats` (Part C). 주어진 것: 노드 번호 `node_of()`, 비트 연산 `bm_*`, free list `fl_push/fl_remove/fl_pop` (리스트·`nfree`·`avail`·`freemap` 을 함께 갱신하고 `g_listops` 를 센다), 불변식 검사기 `buddy_check()`, 참조 모델 `g_owner[]` / `ref_can_alloc()`, 하네스.

## 4. 힌트

<details>
<summary>힌트 1 — size → order 는 "올림 log2"</summary>

`units = (size + 63) >> 6` 으로 64 B 단위 올림. `units` 가 2의 거듭제곱이면 그 지수, 아니면 한 칸 위. `ceil(log2(x)) = 32 − clz(x − 1)` (x ≥ 2). x = 1 이면 `clz(0)` 이 되는데 **`__builtin_clz(0)` 은 정의되지 않은 동작(UB)** 이다 — 따로 0 을 돌려준다. 그리고 `size + 63` 이 넘치지 않게 `size > ARENA_SIZE` 검사를 먼저. (Cortex-M3 이상은 `CLZ` 명령이 있고 CMSIS `__CLZ(0)` 은 32 를 돌려주지만, 이식 가능한 C 에서는 0 을 피한다.)

</details>

<details>
<summary>힌트 2 — 어느 free list 에서 꺼내나: avail + ctz</summary>

`avail & ~((1u << order) − 1u)` 는 "order 이상이면서 비어 있지 않은 리스트들". 0 이면 할당 불가. 아니면 `ctz` 가 그중 **가장 작은** order — 큰 블록을 아껴 두는 best-fit 이다. 루프로 order 를 하나씩 올려 봐도 최대 11번이라 결과는 같다.

</details>

<details>
<summary>힌트 3 — 분할은 "아래 절반을 계속 쥐고, 위 절반을 반납"</summary>

order k 블록 `u` 를 반으로 자르면 `[u, u+2^(k−1))` 와 `[u+2^(k−1), u+2^k)`. 아래 절반의 시작이 `u` 그대로라 변수를 바꿀 필요가 없다. `k−−` 후 `fl_push(b, k, u + (1u << k))` 를 k == order 가 될 때까지.

</details>

<details>
<summary>힌트 4 — 버디는 XOR, 그리고 "같은 order 로" free 여야 한다</summary>

order k 블록은 2^k 의 배수에서 시작하고, 부모(order k+1)는 2^(k+1) 의 배수에서 시작한다. 그러니 두 자식의 시작은 **비트 k 하나만** 다르다 → `buddy = u ^ (1u << k)`. 버디의 unit 하나가 비어 있다고 합치면 안 된다 — 버디 영역이 더 작게 쪼개져 일부만 비어 있을 수 있다. `freemap` 의 `node_of(k, buddy)` 비트, 즉 "그 블록이 **order k 그대로** free list 에 있나"를 본다.

</details>

<details>
<summary>힌트 5 — free(ptr) 가 order 를 찾는 법</summary>

할당할 때 `allocmap` 의 `node_of(order, u)` 를 켰다. 해제할 때는 k = 0, 1, 2… 로 올라가며 그 비트를 찾는다. 단 `node_of` 는 `u >> k` 로 아래 비트를 잘라 버리므로, **u 가 order k 로 정렬되지 않았으면** 그 위 order 는 볼 필요도 없이 실패다 (블록 내부 포인터).

</details>

## 5. 풀이

### 불변식 — 무엇이 항상 참이어야 하나

`buddy_check()` 가 매 연산 후 확인하는 것:

| # | 불변식 | 깨지면 생기는 일 |
|---|---|---|
| ① | 각 리스트의 `next->prev == self`, `prev->next == self` | 리스트 손상 → 이중 할당 |
| ② | order k free 블록의 시작 unit 은 2^k 의 배수 | 버디 계산(XOR)이 틀어짐 |
| ③ | `freemap` 비트 == 리스트 소속, 비트 수 == Σ nfree | 병합 판정 오류 |
| ④ | **같은 order 의 free 버디 쌍이 없다** (병합이 끝까지 됨) | 복원 실패, 큰 블록을 못 만듦 |
| ⑤ | `nfree[k]` == 리스트 길이, `avail` 비트 == (nfree[k] > 0) | ctz 탐색이 빈 리스트를 고름 |
| ⑥ | free 블록 ∪ 할당 블록이 1024 unit 을 **정확히 한 번씩** 덮는다 | 겹침(이중 할당) 또는 누수 |

④ 가 버디 시스템의 핵심이다. ④ 와 ⑥ 이 성립하면 "**모든 블록을 해제하면 order 10 하나**"는 정리(theorem)가 된다: 전부 free 인데 order 10 이 아니라면, 가장 작은 order 의 free 블록 하나를 보자 — 그 버디 영역도 전부 free 이고 그 안의 블록들은 그보다 작을 수 없으니 버디 자체가 같은 order 의 free 블록이어야 하고, 그러면 ④ 위반이다.

참조 모델도 같은 성질을 쓴다: **order k 할당이 성공 ⇔ 2^k 로 정렬된 2^k 칸이 모두 비어 있는 곳이 있다.** (⇐: 그런 칸은 트리 노드 하나이고, ④ 때문에 그 안의 free 조각은 이미 그 노드 크기 이상으로 합쳐져 있다.) 하네스의 `ref_can_alloc()` 은 unit 소유 배열 `g_owner[]` 만 보고 이걸 계산하므로 할당기 내부와 독립적이다.

### Part A — 분할: 64 B 하나를 위해 10번 자른다

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">빈 아레나에서 alloc(64 B) — 폭은 비례 아님 (unit 범위로 표시)</text>
  <text x="15" y="40" fill="currentColor" font-size="12">before: free_area[10] → u0,  avail = 0x400</text>
  <rect x="15" y="46" width="710" height="32" class="f-ok" stroke="currentColor"/>
  <text x="370" y="67" fill="currentColor" text-anchor="middle" font-family="monospace">order 10 · u 0–1023 · 64 KB</text>
  <text x="15" y="110" fill="currentColor" font-size="12">after: 위쪽 절반을 order 9, 8, …, 0 리스트에 하나씩 반납, 아래 끝 u0 을 반환</text>
  <rect x="15" y="116" width="40" height="32" class="f-warn" stroke="currentColor"/>
  <g class="f-ok" stroke="currentColor">
    <rect x="55" y="116" width="40" height="32"/><rect x="95" y="116" width="50" height="32"/>
    <rect x="145" y="116" width="55" height="32"/><rect x="200" y="116" width="60" height="32"/>
    <rect x="260" y="116" width="65" height="32"/><rect x="325" y="116" width="70" height="32"/>
    <rect x="395" y="116" width="75" height="32"/><rect x="470" y="116" width="80" height="32"/>
    <rect x="550" y="116" width="85" height="32"/><rect x="635" y="116" width="90" height="32"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="35" y="137">u0</text><text x="75" y="137">u1</text><text x="120" y="137">2–3</text>
    <text x="172" y="137">4–7</text><text x="230" y="137">8–15</text><text x="292" y="137">16–31</text>
    <text x="360" y="137">32–63</text><text x="432" y="137">64–127</text><text x="510" y="137">128–255</text>
    <text x="592" y="137">256–511</text><text x="680" y="137">512–1023</text>
    <text x="35" y="166">할당</text><text x="75" y="166">o0</text><text x="120" y="166">o1</text>
    <text x="172" y="166">o2</text><text x="230" y="166">o3</text><text x="292" y="166">o4</text>
    <text x="360" y="166">o5</text><text x="432" y="166">o6</text><text x="510" y="166">o7</text>
    <text x="592" y="166">o8</text><text x="680" y="166">o9</text>
  </g>
  <g fill="currentColor">
    <text x="15" y="200">① avail &amp; ~0 = 0x400 → ctz = 10 → pop u0 (order 10)</text>
    <text x="15" y="222">② 분할 루프: k=9 push(9, u0+512), k=8 push(8, u0+256), …, k=0 push(0, u0+1)</text>
    <text x="15" y="244">③ allocmap[node_of(0, 0) = 1024] = 1,  avail = 0x3FF,  nfree = 1 1 1 1 1 1 1 1 1 1 0</text>
    <text x="15" y="266">free list 연산 = pop 1 + push 10 = 11 → 이게 할당의 최악 (MAX_ORDER + 1)</text>
    <text x="15" y="288">다음 alloc(100 B = order 1): ctz(0x3FF &amp; ~1) = 1 → u2 를 쪼갤 필요 없이 바로 반환</text>
  </g>
</svg>
```

실제 출력 (solution.c):

```text
[A] init        nfree[0..10] = 0 0 0 0 0 0 0 0 0 0 1
[A] alloc 64B   nfree[0..10] = 1 1 1 1 1 1 1 1 1 1 0
[A] alloc 100B/64B/1000B -> unit 2 / 1 / 16
[A] after 4     nfree[0..10] = 0 0 1 1 0 1 1 1 1 1 0
```

단계별 추적 (64 B 다음의 세 요청):

| 요청 | order | `avail & ~((1<<o)−1)` 의 ctz | 꺼낸 블록 | 분할 | 반환 | 바뀐 nfree |
|---|---|---|---|---|---|---|
| 100 B (2 unit) | 1 | 1 | o1 u2 | 없음 | u2 | o1: 1 → 0 |
| 64 B | 0 | 0 | o0 u1 | 없음 | u1 | o0: 1 → 0 |
| 1000 B (16 unit) | 4 | 4 | o4 u16 | 없음 | u16 | o4: 1 → 0 |

`size_to_order(1000)`: units = ⌈1000/64⌉ = 16 → `32 − clz(15) = 32 − 28 = 4`. 1500 B 는 24 unit → `32 − clz(23) = 5` (32 unit = 2 KB 블록, 548 B 낭비). 이 반올림이 **내부 단편화**이고 Part C 에서 잰다.

**왜 아래 절반을 쥐고 위 절반을 반납하나**: 둘 중 어느 쪽이든 맞지만, 아래 절반은 시작 주소가 `u` 그대로라 계산이 하나 줄고, 연속 할당이 낮은 주소부터 채워져 높은 주소에 큰 덩어리가 남는다. Linux `expand()` 도 같은 방향(아래를 쓰고 위 절반들을 order 별 리스트에 넣음)이다.

### Part B — 병합: XOR 로 버디를 찾고 위로 올라간다

64 B 네 개(unit 0..3)를 1, 2, 0, 3 순서로 풀 때의 실제 출력:

```text
[B] free unit 1: list ops 1, nfree[0..3] = 1 0 1 1, top order 9
[B] free unit 2: list ops 1, nfree[0..3] = 2 0 1 1, top order 9
[B] free unit 0: list ops 2, nfree[0..3] = 1 1 1 1, top order 9
[B] free unit 3: list ops 11, nfree[0..3] = 0 0 0 0, top order 10
```

| 해제 | k | 버디 `u ^ (1<<k)` | 버디가 order k 로 free? | 동작 | list 연산 |
|---|---|---|---|---|---|
| u1 | 0 | 0 | 아니오 (할당) | push(0, u1) | 1 |
| u2 | 0 | 3 | 아니오 (할당) | push(0, u2) | 1 |
| u0 | 0 | 1 | 예 | remove u1, u=0, k=1 | |
| | 1 | 2 | **아니오** — u2 는 order 0 으로만 free, u3 은 할당 | push(1, u0) | 2 |
| u3 | 0 | 2 | 예 | remove u2, u=2, k=1 | |
| | 1 | 0 | 예 (o1 u0) | remove, u=0, k=2 | |
| | 2…9 | 4, 8, …, 512 | 예 (처음 분할 때 반납한 것들) | remove, k++ | |
| | 10 | — | k == MAX_ORDER | push(10, u0) | 10 + 1 = 11 |

```svg
<svg viewBox="0 0 740 310" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">free(u3): 버디가 같은 order 로 free 인 동안 합치며 올라간다 (unit 0–15)</text>
  <g stroke="currentColor">
    <rect x="50" y="40" width="80" height="32" class="f-ok"/><rect x="130" y="40" width="40" height="32" class="f-ok"/>
    <rect x="170" y="40" width="40" height="32" class="f-warn"/><rect x="210" y="40" width="160" height="32" class="f-ok"/>
    <rect x="370" y="40" width="320" height="32" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="90" y="61">u0–1 o1</text><text x="150" y="61">u2</text><text x="190" y="61">u3</text>
    <text x="290" y="61">u4–7 o2</text><text x="530" y="61">u8–15 o3   (u16… 은 o4~o9)</text>
  </g>
  <text x="10" y="61" fill="currentColor" font-size="12">전</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="100">k=0  buddy = 3 ^ 1   = 2    o0 free  → remove(0,2),  u = 3 &amp; ~1 = 2,  k=1</text>
    <text x="20" y="122">k=1  buddy = 2 ^ 2   = 0    o1 free  → remove(1,0),  u = 2 &amp; ~2 = 0,  k=2</text>
    <text x="20" y="144">k=2  buddy = 0 ^ 4   = 4    o2 free  → remove(2,4),  k=3</text>
    <text x="20" y="166">k=3  buddy = 0 ^ 8   = 8    o3 free  → remove(3,8),  k=4   … k=9: buddy 512</text>
    <text x="20" y="188">k=10 = MAX_ORDER → push(10, u0).  list ops = remove 10 + push 1 = 11</text>
    <text x="20" y="214">노드로 보면: 1027 ^ 1 = 1026 → 부모 513, ^1 = 512 → 부모 256, ^1 = 257 → … → 1</text>
  </g>
  <rect x="50" y="240" width="640" height="32" class="f-ok" stroke="currentColor"/>
  <text x="370" y="261" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">order 10 · u 0–1023 · 하나로 복원</text>
  <text x="10" y="261" fill="currentColor" font-size="12">후</text>
  <text x="20" y="298" fill="currentColor">u0 해제 때 k=1 에서 멈춘 이유: 버디 u2–3 이 "o1 블록"으로 free 가 아님 (u3 이 할당 중)</text>
</svg>
```

**XOR 증명 (한 줄 더 자세히)**: order k 블록 시작 `u` 는 2^k 의 배수 (불변식 ②). 부모 시작 `p` 는 2^(k+1) 의 배수라서 비트 k 가 0. 두 자식은 `p` 와 `p + 2^k` — 비트 k 만 다르다. 그러니 어느 자식에서든 비트 k 를 뒤집으면 다른 자식: `u ^ (1 << k)`. 부모 시작은 비트 k 를 끈 값 `u & ~(1 << k)`. 바이트 오프셋으로는 `off ^ (64 << k)` = "오프셋 XOR 블록 크기". 자주 하는 오답 `u + (1 << k)` 는 왼쪽 자식에서만 맞는다.

**`buddy_free` 의 검증 단계** — 펌웨어에서 잘못된 free 는 조용한 메모리 손상이 되므로 할당기가 먼저 막는다:

| 입력 | 걸리는 곳 | 결과 |
|---|---|---|
| `NULL`, 스택 변수 주소 | `uintptr_t` 범위 검사 | false |
| `p + 32` | 64 B 정렬 검사 | false |
| order 1 블록(u0–1)의 `&mem[1]` | k=0: allocmap(1024+1) 없음 → k=1: u=1 이 2 로 안 나눠짐 → 중단 | false |
| 할당 안 된 `&mem[3]` | 모든 k 에서 allocmap 없음 (k=0: 1027, k=1: u=3 홀수 → 중단) | false |
| 같은 포인터 두 번째 | 첫 free 가 allocmap 비트를 지웠음 | false, 상태 불변 |

범위 검사를 `p < b->mem` 처럼 포인터 관계 비교로 하면 안 된다: C11 6.5.8 은 같은 배열(객체) 안을 가리키는 포인터끼리의 `<` 만 정의한다. `uintptr_t` 로 바꿔 정수로 비교한다 (변환 결과는 구현 정의지만 평탄 주소 공간인 Cortex-M·arm64 에선 주소 그대로다).

**랜덤 테스트**: 30000 연산 (55% 할당: order 0~2 가 60%, 3~5 가 30%, 6~10 이 10% / 45% 무작위 해제). 매 연산마다 ① 할당 성공 여부 == `ref_can_alloc(k)` ② 돌려받은 블록이 자기 크기로 정렬되고 다른 살아 있는 블록과 안 겹침 ③ 할당 때 블록 전체를 태그 바이트로 `memset`, 해제 직전에 그대로인지 확인 (겹침 버그는 ASan 이 못 잡는다 — 전부 같은 아레나 안이라서) ④ `buddy_check()`. 실제 출력:

```text
[B] random 30000 ops: match (allocs 13484, refused 3236, live at end 204)
```

거절 3236 번이 모두 참조 모델의 "불가"와 일치했다 — 꽉 찬 경계와 큰 order 실패가 충분히 시험된 것이다. 이어서 남은 204 블록을 무작위 순서로 해제한 뒤 `only_max_block()` 이 참 → **완전 복원**.

### Part C — 단편화 측정

**두 종류의 단편화**:

- **내부 단편화(internal)**: 블록 안의 낭비 = `1 − 요청 합 / 할당 블록 합`. 버디는 2의 거듭제곱 반올림이라 최악 거의 50% (예: 2 KB + 1 B → 4 KB).
- **외부 단편화(external)**: free 메모리가 흩어져 큰 요청을 못 받는 정도. 이 레슨의 지표 `frag = 1 − largest_free / free_bytes` — free 가 한 덩어리면 0, 잘게 흩어질수록 1 에 가깝다.

`buddy_stats()` 는 리스트를 돌 필요가 없다: `free_bytes = Σ nfree[k] · (64 << k)`, `largest_free = 64 << (31 − clz(avail))` (avail 의 최상위 비트 = 비어 있지 않은 가장 큰 order). O(MAX_ORDER).

패킷 + DMA 워크로드 실제 출력 (seed 20261107, 20000 step):

```text
[C]  step | live | used B | req B | int.frag | free B | largest | ext.frag | fail (free>=blk)
[C]  2500 |   28 |  38272 | 28118 |    26.5% |  27264 |   16384 |    39.9% | 7 (7)
[C]  5000 |   24 |  45440 | 28221 |    37.9% |  20096 |    8192 |    59.2% | 14 (14)
[C]  7500 |   26 |  32000 | 21992 |    31.3% |  33536 |   16384 |    51.1% | 21 (19)
[C] 10000 |   23 |  35968 | 23392 |    35.0% |  29568 |    8192 |    72.3% | 28 (26)
[C] 12500 |   25 |  24448 | 17732 |    27.5% |  41088 |   16384 |    60.1% | 36 (34)
[C] 15000 |   21 |  29568 | 22427 |    24.2% |  35968 |   16384 |    54.4% | 47 (41)
[C] 17500 |   28 |  57600 | 40788 |    29.2% |   7936 |    4096 |    48.4% | 61 (51)
[C] 20000 |   26 |  53760 | 33436 |    37.8% |  11776 |    4096 |    65.2% | 68 (56)
[C] worst list ops per call: alloc 6, free 5 (bound 11)
```

| 관찰 | 숫자 | 해석 |
|---|---|---|
| 내부 단편화 | 24~38% | 패킷 64~1518 B 가 64 B~2 KB 블록으로 반올림. 1518 B 이더넷 프레임 하나가 2 KB 를 먹는다 |
| 외부 단편화 | 40~72% | 수명이 짧은 패킷 버퍼가 큰 블록 한가운데를 쪼개 놓는다. 10000 step: 29.5 KB free 인데 최대 8 KB |
| 실패 68 번 중 56 번 | "free 합 ≥ 블록 크기" | **총량은 충분한데 연속 블록이 없어서** 실패 — 외부 단편화가 실제 장애로 나타난 것 |
| 호출당 list 연산 | alloc ≤ 6, free ≤ 5 | 이론 상한 11 (Part A·B 추적에서 실제로 11 을 봤다). 시간 상한이 아레나 크기와 무관한 상수 |

각 줄을 출력할 때 하네스가 `buddy_check()`, `free_bytes + used == 65536`, `largest_free == ref_largest_free()` (unit 배열에서 독립 계산) 를 확인한다. 끝에 살아 있는 버퍼를 모두 풀면 `frag == 0`, `largest == free == 65536`, free 블록 1개.

**체커보드 — 지표의 극단**:

```text
[C] checkerboard: free 32768 B in 512 blocks, largest 64 B, ext.frag 0.998047, alloc(128) NULL
```

```svg
<svg viewBox="0 0 740 210" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">체커보드: 64 B × 1024 할당 후 짝수 번째만 해제 (앞 32 unit 만 그림)</text>
  <g stroke="currentColor">
    <rect x="50" y="40" width="20" height="30" class="f-ok"/><rect x="70" y="40" width="20" height="30" class="f-warn"/>
    <rect x="90" y="40" width="20" height="30" class="f-ok"/><rect x="110" y="40" width="20" height="30" class="f-warn"/>
    <rect x="130" y="40" width="20" height="30" class="f-ok"/><rect x="150" y="40" width="20" height="30" class="f-warn"/>
    <rect x="170" y="40" width="20" height="30" class="f-ok"/><rect x="190" y="40" width="20" height="30" class="f-warn"/>
    <rect x="210" y="40" width="20" height="30" class="f-ok"/><rect x="230" y="40" width="20" height="30" class="f-warn"/>
    <rect x="250" y="40" width="20" height="30" class="f-ok"/><rect x="270" y="40" width="20" height="30" class="f-warn"/>
    <rect x="290" y="40" width="20" height="30" class="f-ok"/><rect x="310" y="40" width="20" height="30" class="f-warn"/>
    <rect x="330" y="40" width="20" height="30" class="f-ok"/><rect x="350" y="40" width="20" height="30" class="f-warn"/>
    <rect x="370" y="40" width="20" height="30" class="f-ok"/><rect x="390" y="40" width="20" height="30" class="f-warn"/>
    <rect x="410" y="40" width="20" height="30" class="f-ok"/><rect x="430" y="40" width="20" height="30" class="f-warn"/>
    <rect x="450" y="40" width="20" height="30" class="f-ok"/><rect x="470" y="40" width="20" height="30" class="f-warn"/>
    <rect x="490" y="40" width="20" height="30" class="f-ok"/><rect x="510" y="40" width="20" height="30" class="f-warn"/>
    <rect x="530" y="40" width="20" height="30" class="f-ok"/><rect x="550" y="40" width="20" height="30" class="f-warn"/>
    <rect x="570" y="40" width="20" height="30" class="f-ok"/><rect x="590" y="40" width="20" height="30" class="f-warn"/>
    <rect x="610" y="40" width="20" height="30" class="f-ok"/><rect x="630" y="40" width="20" height="30" class="f-warn"/>
    <rect x="650" y="40" width="20" height="30" class="f-ok"/><rect x="670" y="40" width="20" height="30" class="f-warn"/>
  </g>
  <text x="700" y="60" fill="currentColor">…</text>
  <g fill="currentColor">
    <text x="50" y="96">free 칸(ok) = order 0 블록 512개. 모든 free 블록의 버디가 할당 중 → 못 합침</text>
    <text x="50" y="122">free_bytes = 32768,  largest_free = 64  →  frag = 1 − 64/32768 = 0.998046875</text>
    <text x="50" y="148">alloc(128) = NULL — 메모리 절반이 비어 있는데 128 B 를 못 준다</text>
    <text x="50" y="174">같은 32 KB 가 한 덩어리였다면 largest = 32768 → frag = 0</text>
    <text x="50" y="198">홀수 번째까지 풀면 연쇄 병합으로 order 10 하나 — frag 0 으로 복원 (테스트 마지막 CHECK)</text>
  </g>
</svg>
```

수명이 긴 작은 할당 하나가 큰 블록 한가운데에 박히면 그 위 모든 order 의 병합을 막는다 — 이게 버디 외부 단편화의 본질이다. 펌웨어에서 쓰는 대책:

1. **수명으로 분리**: 부팅 때 한 번 잡고 안 놓는 것(드라이버 디스크립터, 큐)은 버디 밖 정적 할당 또는 별도 영역. Linux 는 migratetype(움직일 수 있는/없는 페이지)으로 pageblock 을 나눠 같은 원리를 쓴다.
2. **작은 객체는 위층에서**: 64 B~2 KB 패킷 버퍼는 고정 크기 풀(DS-M02)이나 slab(DS-M03) 으로 — 버디에서는 큰 페이지만 받아 오고 그 안을 잘게 쓴다. 내부 단편화도 함께 준다.
3. **큰 DMA 버퍼는 부팅 직후 미리 확보**: 단편화가 쌓이기 전에 (Linux CMA 와 같은 동기).

**메모리 오버헤드**: 메타데이터 `sizeof(buddy_t) − 64 KB = 736 B` (sentinel 11 × 16 B, nfree 44 B, avail 4 B, 비트맵 2 × 256 B) = 아레나의 1.1%. free list 링크는 free 블록 안에 살므로 추가 비용 0 — 그래서 최소 블록은 링크 2개(arm64 16 B, Cortex-M 8 B)보다 커야 한다 (`_Static_assert`). 할당된 블록에는 헤더가 없다: `free(p)` 의 크기 정보는 `allocmap` 에 있다. 헤더를 블록 앞에 두면 4 KB 요청이 4 KB + 헤더 → 8 KB 블록이 되고 정렬도 깨진다.

**복잡도 정리**:

| 연산 | 시간 | 최악 list 연산 | 비고 |
|---|---|---|---|
| alloc | O(MAX_ORDER) | 1 pop + 10 push | order 탐색은 `ctz` 로 O(1) |
| free | O(MAX_ORDER) | 10 remove + 1 push | order 찾기 최대 11 비트 검사 |
| stats | O(MAX_ORDER) | 0 | 리스트 순회 없음 |
| check (테스트용) | O(NUNITS + NNODES) | — | 실전 빌드에선 끔 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 22/22 passed
# 메모리 오류·UB 확인
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o solution_asan && ./solution_asan
```

**엄격한 별칭 규칙(strict aliasing) 메모**: free 링크를 `uint8_t mem[]` 에 `(fblk_t *)` 캐스팅으로 쓰면 C11 6.5p7 위반이다 (선언된 타입이 `uint8_t` 인 객체를 `fblk_t` 로 접근). 그래서 아레나를 `union { fblk_t link; uint8_t bytes[64]; max_align_t align; } mem[1024]` 로 선언해 링크 접근을 union 멤버 접근으로 만들었다. 테스트가 사용자 데이터를 쓰는 방법은 `memset` (문자 타입 접근, 항상 허용)이다. 사용자가 이 메모리에 `uint32_t` 등을 쓰는 것은 정적 배열 위 할당기 전반(DS-M02 풀 포함)의 회색 지대라, Linux 커널은 `-fno-strict-aliasing` 으로 빌드한다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 버디를 `u + (1 << k)` 로 계산 | 오른쪽 자식의 버디는 왼쪽(`u − 2^k`). 오른쪽에서 해제하면 엉뚱한 이웃과 합침 | `bu = u ^ (1u << k)` |
| 버디의 첫 unit 이 free 인지만 보고 병합 | 버디 영역이 더 작게 쪼개져 일부만 free 일 수 있다 → 할당 중인 메모리를 free 블록에 포함 | "order k 로 free list 에 있나": `bm_test(freemap, node_of(k, bu))` |
| 병합 후 시작을 `u` 그대로 둠 | 오른쪽 자식에서 시작하면 합친 블록이 정렬 안 됨 → 이후 XOR 전부 틀림 | `u &= ~(1u << k)` |
| 병합할 때 버디를 free list 에서 안 뺌 | 합쳐진 큰 블록과 그 조각이 둘 다 리스트에 → 같은 메모리 이중 할당 | `fl_remove(b, k, bu)` 후 k++ |
| free 에서 order 를 찾을 때 정렬 검사 생략 | `node_of(k, u)` 는 `u >> k` 로 잘라서, 블록 내부 포인터 `&mem[1]` 이 order 1 블록 머리로 인정됨 | `if (u & ((1u << k) − 1u))` 면 중단 |
| `__builtin_clz(units − 1)` 을 units == 1 에서 호출 | `clz(0)` 은 UB (-O2 에서 아무 값) | `if (units == 1) return 0;` |
| `size + 63` 을 범위 검사 전에 | `SIZE_MAX` 근처에서 넘쳐 작은 값 → 엉뚱한 order 성공 | `size > ARENA_SIZE` 를 먼저 |
| 아레나 범위를 `p >= b->mem && p < end` 로 검사 | 다른 객체를 가리키는 포인터의 관계 비교는 UB → 컴파일러가 검사를 지울 수 있음 | `uintptr_t` 로 정수 비교 |
| 할당 블록 앞에 크기 헤더 저장 | 2^k 요청이 2^(k+1) 블록을 먹고(내부 단편화 2배), 반환 주소가 블록 크기로 정렬되지 않음 (DMA 정렬 깨짐) | 크기는 옆 비트맵(allocmap)이나 호출자가 order 전달 (Linux `free_pages(addr, order)`) |

## 8. 면접 Q&A

<details>
<summary>Q1. 버디 주소를 왜 XOR 로 구하나요? 덧셈/뺄셈으로는 안 되나요?</summary>

order k 블록은 2^k 정렬, 부모는 2^(k+1) 정렬이라 두 형제의 시작 주소는 비트 k 하나만 다르다. 내가 왼쪽이면 `+2^k`, 오른쪽이면 `−2^k` 인데, 비트 k 를 보고 분기하는 대신 XOR 하나로 둘 다 처리한다 — 분기 없음, 명령 하나. 전제는 **아레나 기준 오프셋**이 정렬돼 있다는 것: 절대 주소로 XOR 하려면 아레나 시작이 64 KB 정렬이어야 하므로 보통 `addr − base` 오프셋(Linux 는 pfn)으로 계산한다. Linux: `mm/internal.h` 의 `__find_buddy_pfn()` 이 `page_pfn ^ (1 << order)` 다.

</details>

<details>
<summary>Q2. 시간 복잡도와 최악 시간은? 실시간 시스템에 써도 되나요?</summary>

alloc 은 order 탐색(`ctz` O(1) 또는 최대 11칸 루프) + 분할 최대 MAX_ORDER 번, free 는 order 찾기 + 병합 최대 MAX_ORDER 번. 둘 다 **블록 수와 무관한 상수 상한** (여기선 list 연산 11) — first-fit 리스트(FreeRTOS heap_4 처럼 free 블록을 주소순으로 훑는 방식)는 free 블록 수에 비례한다. 실측에서 호출당 alloc ≤ 6, free ≤ 5, 이론 최악 11 은 Part A·B 추적에서 실제로 나왔다. 그래서 실시간에 쓸 수 있지만 내부 단편화(최악 ~50%)가 크다. 상수 시간이면서 크기 클래스를 더 촘촘히 나누는 게 TLSF (DS-A06).

</details>

<details>
<summary>Q3. 내부 단편화와 외부 단편화를 설명하고, 버디는 각각 어떤가요?</summary>

내부 = 할당 블록 안의 안 쓰는 부분. 버디는 2의 거듭제곱 반올림 때문에 크다 — 워크로드 실측 24~38%, 최악은 블록 크기 + 1 바이트 요청에서 거의 50% (2 KB + 1 B → 4 KB). 외부 = free 총량은 있는데 연속 블록이 없는 상태. 버디는 "합칠 수 있는 짝"이 고정돼 있어 짝이 아닌 이웃 free 블록끼리는 못 합친다 (u1 과 u2 가 둘 다 free 여도 버디가 아니라서 안 합쳐짐). 실측: 실패 68회 중 56회가 free 합 ≥ 블록 크기인데 실패. 대책은 수명별 분리, 작은 객체는 slab/풀로, 큰 버퍼는 조기 확보, 리눅스는 migratetype 분리와 compaction(페이지 이동).

</details>

<details>
<summary>Q4. Linux 페이지 할당기는 이 구조와 무엇이 같고 다른가요?</summary>

같은 점: zone 마다 `free_area[order]` 배열 (order 0..10 이 기본 — 4 KB 페이지면 최대 4 MB 블록), 각 `free_area` 에 리스트와 `nr_free`. 할당은 `__rmqueue_smallest()` 가 요청 order 부터 위로 올라가며 비어 있지 않은 리스트를 찾고 `expand()` 로 위 절반들을 아래 order 리스트에 넣는다. 해제는 `__free_one_page()` 가 `__find_buddy_pfn()` 으로 버디를 찾아 병합. `/proc/buddyinfo` 가 order 별 free 블록 수 — 이 레슨의 `nfree[0..10]` 출력과 같은 모양이다.
다른 점: ① 링크가 free 메모리 안이 아니라 `struct page` 배열(옆 메타데이터)에 있다 (연습 1·2 가 이 방식). ② "버디가 free 이고 order 가 k" 판정은 비트맵이 아니라 버디 페이지의 `PageBuddy` 표시 + 저장된 order (`buddy_order()`) — 옛 2.4 커널은 쌍마다 1비트 XOR 비트맵을 썼다 (연습 1). ③ free 할 때 호출자가 order 를 넘긴다 (`__free_pages(page, order)`). ④ 리스트가 migratetype 별로 나뉘고, 작은 order 는 CPU 별 캐시(pcp list)를 먼저 거쳐 zone 락을 덜 잡는다.

</details>

<details>
<summary>Q5. ISR 이나 멀티코어에서 이 할당기를 쓰면?</summary>

alloc/free 는 리스트 여러 개와 비트맵을 함께 바꾸는 다단계 연산이라 중간에 끼어들면 불변식이 깨진다. 단일 코어 MCU: 인터럽트를 끄는 임계 구역(`__disable_irq()` / `taskENTER_CRITICAL()`)으로 감싸는데, 연산이 최대 list 연산 11번이라 **임계 구역 길이에 상수 상한**이 있다는 게 장점. 그래도 ISR 에서 직접 할당하기보다 미리 할당한 버퍼 풀(DS-M02)을 쓰는 게 정석. 멀티코어: 할당기 전체에 스핀락 (Linux `zone->lock`), 경합을 줄이려 코어별 캐시를 앞에 둔다 (pcp list, slab 의 per-CPU 캐시).

</details>

<details>
<summary>Q6. free(ptr) 는 크기를 어떻게 알죠? 방법별 장단점은?</summary>

① 호출자가 order 를 넘김 (Linux `free_pages`): 메타데이터 0, 대신 API 가 불편하고 틀린 order 를 못 막는다. ② 블록 앞 헤더: 간단하지만 2^k 요청이 다음 order 로 밀리고 정렬이 깨진다. ③ 이 레슨의 `allocmap` (트리 노드당 1비트, 256 B): free 시 order 0 부터 정렬이 허용하는 order 까지 최대 11비트를 본다 — 이중 해제·내부 포인터 검출까지 공짜. ④ unit 마다 order 바이트 배열 (1024 B): O(1) 조회, 메모리가 4배. 64 KB 아레나라면 ③이 균형이 좋다.

</details>

<details>
<summary>Q7. 외부 단편화 지표 1 − largest/free 의 한계는? 더 나은 지표는?</summary>

"가장 큰 블록 하나"만 보니까, 요청이 전부 작으면 frag 가 높아도 문제없고 반대로 frag 가 낮아도 그보다 큰 요청은 실패한다. 요청 크기를 넣은 지표가 낫다: order j 요청 기준 **쓸 수 없는 free 비율** = `1 − (Σ_{k≥j} nfree[k]·2^k) / (Σ_k nfree[k]·2^k)`. 이 레슨의 지표는 가장 큰 블록 **하나**만 세므로, 가장 큰 free order 의 블록이 하나뿐일 때 j = 그 order 의 값과 같다. Linux 는 debugfs `extfrag/unusable_index` 로 이 order 별 값을 보여 주고, compaction 결정에는 `extfrag_index` 를 쓴다. 펌웨어에서는 "부팅 후 N 시간 뒤에도 가장 큰 DMA 프레임(예: 16 KB)이 할당 가능한가"를 장시간 테스트로 보는 게 실용적이다.

</details>

## 9. 추가 연습

`practice.c` 는 Linux 부팅 초기처럼 **unit 번호(pfn)만 관리하는 메타데이터 전용 버디**다: 링크는 옆 배열 `next[]/prev[]` (Linux `struct page` 의 리스트처럼), 메모리 내용은 건드리지 않는다. 랜덤 테스트는 본문과 같은 참조 모델(정렬된 빈 칸 존재 ⇔ 할당 성공) 대조 + 매 연산 불변식 + 전부 해제 후 **초기 free 블록 집합과 정확히 같은지** 확인한다.

### 연습 1 — 쌍(pair) XOR 비트맵: 비트맵을 절반으로

버디 쌍마다 비트 1개만 두고 병합 판정을 하라. 비트 수는?

<details>
<summary>답 보기</summary>

쌍 비트 = `isfree_k(A) XOR isfree_k(B)` 로 정의하고, **free list k 에 넣거나 뺄 때마다 그 쌍 비트를 토글**하면 정의가 저절로 유지된다 (최상위 order 는 짝이 없어 토글 안 함). 해제할 블록 자신은 아직 리스트에 없으니 그 순간 쌍 비트 = "버디가 free" — 비트 하나로 판정 끝:

```c
while (k < P_MAX && pair_test(pb, k, u)) { pb_remove(pb, k, u ^ (1u << k)); u &= ~(1u << k); k++; }
pb_push(pb, k, u);
```

쌍 비트 번호는 **부모 노드 번호** `(1024 >> (k+1)) + (u >> (k+1))` 라 1..1023 — 1023 비트(128 B). 본문의 `freemap` 2047 비트의 절반. 대신 "이 블록이 free 인가"를 단독으로 물을 수 없어 이중 해제 검출이 안 되고, 호출자가 order 를 넘겨야 한다 (옛 Linux 2.4 `__free_pages_ok()` 가 `__test_and_change_bit()` 로 이 방식을 썼다). 검사기는 모든 쌍에 대해 비트 == XOR 이고 둘 다 free 인 쌍이 없음을 확인한다.

```text
[1] pair-XOR bitmap (1023 bits used): random 20000 ops ok, allocs 9161
```

</details>

### 연습 2 — 2의 거듭제곱이 아닌 아레나 (1000 unit)

RAM 이 62.5 KB(1000 unit)뿐이다. 초기화와 병합을 어떻게 바꾸나?

<details>
<summary>답 보기</summary>

초기화: 왼쪽부터 "현재 위치로 정렬되고 범위를 넘지 않는 가장 큰 블록"으로 잘라 `pb_free` 로 넣는다 → 1000 = 512 + 256 + 128 + 64 + 32 + 8. 병합은 **바꿀 게 없다**: 범위 밖 블록은 리스트에 들어간 적이 없으니 쌍 비트가 "버디 free" 를 말할 일이 없다. 그래서 order 10 블록은 영원히 안 생기고 `pb_alloc(10)` 은 실패한다.

```text
[2] 1000 units init       nfree[0..10] = 0 0 0 1 0 1 1 1 1 1 0
[2] after stress+free all nfree[0..10] = 0 0 0 1 0 1 1 1 1 1 0
```

랜덤 20000 연산 후 전부 해제하면 처음 쪼갠 모양 그대로 돌아온다 — "완전 복원"의 일반형은 "order 10 하나"가 아니라 "초기 free 집합"이다.

</details>

### 연습 3 — 예약 구멍이 있는 부팅 초기화

unit [100, 130) 은 부트로더가 남긴 DMA 디스크립터 영역이라 절대 나눠 주면 안 된다. 관리 범위 1000 unit.

<details>
<summary>답 보기</summary>

`pb_init` 이 free 범위 목록 `{[0,100), [130,1000)}` 을 받아 각 범위를 연습 2처럼 잘라 `pb_free` 한다. 구멍 경계에서는 정렬 때문에 블록이 잘게 쪼개진다: 96–99 (o2), 130–131 (o1), 132–135 (o2), 136–143 (o3)… 구멍 쪽 버디는 영원히 free 가 아니라서 그 위로 합쳐지지 않는다. Linux 부팅도 같은 발상이다 — memblock 이 예약 영역을 뺀 범위를 가장 큰 정렬 블록 단위로 버디에 넘긴다.

```text
[3] hole [100,130) init   nfree[0..10] = 0 1 2 2 1 3 3 1 2 0 0
```

참조 모델은 구멍 unit 을 "사용 불가(2)"로 표시하므로 랜덤 테스트가 구멍을 침범하면 바로 걸린다.

</details>

### 연습 4 — 제자리 축소 (shrink)

1 KB (order 4) 를 받았는데 실제로 128 B (order 1) 만 필요해졌다. 데이터를 옮기지 않고 나머지를 돌려줘라.

<details>
<summary>답 보기</summary>

위쪽 절반을 큰 것부터 반납: order 3 (u8–15), order 2 (u4–7), order 1 (u2–3). 각 조각의 버디는 아직 쥐고 있는 아래쪽이라 병합이 일어나지 않는다 — `pb_free` 를 그대로 불러도 된다. 앞 128 B 의 내용과 주소는 그대로.

```text
[4] shrink order 4 -> 1   nfree[0..10] = 0 1 1 1 1 1 1 1 1 1 0
```

이어서 `pb_free(0, 1)` 하면 연쇄 병합으로 order 10 하나. 응용: 2의 거듭제곱이 아닌 큰 요청(예: 5 KB)은 8 KB 를 받아 shrink 로 꼬리를 돌려주면 내부 단편화가 줄지만, 그 조각들은 따로 해제해야 한다 (Linux `alloc_pages_exact()` 가 이 방식).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 9/9 passed
```

## 10. 핵심 정리

- 버디 = 2^k 블록만. 할당은 가장 작은 충분한 order 에서 꺼내 **반으로 쪼개 위 절반 반납**, 해제는 **버디가 같은 order 로 free 인 동안 합치기**.
- 버디 = `u ^ (1 << k)` (오프셋 XOR 블록 크기), 합친 시작 = `u & ~(1 << k)`. 트리 노드로는 `n ^ 1`, `n >> 1`.
- 핵심 불변식: 같은 order 의 free 버디 쌍이 없다 + free ∪ 할당이 아레나를 정확히 한 번 덮는다 → 전부 해제하면 최대 블록 하나로 복원.
- `avail` 비트마스크 + `ctz` 로 order 탐색 O(1), alloc/free 최악 list 연산 MAX_ORDER + 1 = 11 — 블록 수와 무관한 상수 상한.
- free(ptr) 의 크기는 allocmap(노드 비트) 에서, 단 order k 로 정렬된 경우만 → 내부 포인터·이중 해제 검출.
- 내부 단편화 실측 24~38% (최악 ~50%), 외부 단편화 `1 − largest/free` 실측 40~72%, 실패의 82%가 "총량은 충분". 체커보드는 50% free 인데 0.998.
- 작은 객체는 풀/slab 을 위에, 오래 사는 것은 분리, 큰 DMA 버퍼는 일찍 — Linux 는 같은 구조(free_area, `__find_buddy_pfn`, `/proc/buddyinfo`).

## 참고자료

- Kenneth C. Knowlton, "A Fast Storage Allocator", *Communications of the ACM* 8(10), 1965 — 버디 시스템 원 논문
- Donald E. Knuth, *The Art of Computer Programming* Vol. 1, §2.5 Dynamic Storage Allocation (buddy system 알고리즘과 연습문제)
- Remzi & Andrea Arpaci-Dusseau, *Operating Systems: Three Easy Pieces*, Ch. 17 Free-Space Management (Buddy Allocation 절)
- [Linux `mm/page_alloc.c` — `__rmqueue_smallest()`, `expand()`, `__free_one_page()`](https://github.com/torvalds/linux/blob/master/mm/page_alloc.c)
- [Linux `mm/internal.h` — `__find_buddy_pfn()`, `buddy_order()`](https://github.com/torvalds/linux/blob/master/mm/internal.h)
- [Linux `Documentation/filesystems/proc.rst` — `/proc/buddyinfo`](https://github.com/torvalds/linux/blob/master/Documentation/filesystems/proc.rst)
- Mel Gorman, *Understanding the Linux Virtual Memory Manager* (2004), Ch. 6 Physical Page Allocation — 2.4 의 쌍 비트맵 방식 설명
- 관련 레슨: DS-B06 (sentinel 리스트), DS-B08 (비트맵), DS-B12 (트리 인덱스), DS-M02 (고정 크기 풀), DS-M03 (slab), DS-A06 (TLSF)
