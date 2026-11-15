---
id: RTOS-M10
time: 90분
updated: 2026-11-11
---
# RTOS-M10 RTOS 힙 전략: heap_1~heap_5 비교와 heap_4 coalescing

## 학습 목표

- FreeRTOS heap_4 의 자료구조(블록 앞 `BlockLink_t` 헤더, **주소순** free list, 아레나 안쪽 끝 표시 `pxEnd`, size 의 MSB = 할당 표시)를 그림으로 그리고, `pvPortMalloc` 의 first fit · 쪼개기 규칙(`남는 조각 > 2 × 헤더`)과 `prvInsertBlockIntoFreeList` 의 앞/뒤 병합을 직접 구현한다.
- heap_2 가 실제로 무엇을 하는지(**크기순** free list = best fit, 병합 없음)를 소스로 확인하고, 같은 8 KB 아레나·같은 요청 흐름에서 단편화 · 첫 할당 실패 시점 · 실패 횟수 · 최소 여유(min-ever)를 시드 8개 표로 비교한다.
- 내 모델이 진짜 FreeRTOS V11.3.2 (POSIX 포트, heap_4.c / heap_2.c)와 **연산 5000개 모두 바이트 단위로 같은** 결과를 내는 것을 데모로 확인한다.
- heap_1 / heap_3 / heap_5 를 결정성 · free 지원 · 병합 · 여러 영역 기준으로 비교하고, "바이트는 남았는데 할당 실패" 를 `vPortGetHeapStats` 로 진단하는 펌웨어 감시 코드를 짠다.

## 1. 문제

"FreeRTOS 힙 5개 중 무엇을 쓰고 왜죠? 메모리가 남았는데 malloc 이 실패하면?" — 결정성·단편화·실패 처리를 한 번에 묻는 펌웨어 면접 단골 질문이다. 정답은 "heap_4: 주소순 first fit + 이웃 병합, 그래도 O(블록 수)라 비결정적, 그래서 런타임 할당은 줄이고 감시한다" 를 소스 근거와 숫자로 말하는 것이다.

RTOS 는 태스크 TCB·스택·큐를 만들 때 `pvPortMalloc` 을 부른다 (스택 먼저, TCB 나중 — tasks.c:1640–1642, 1681, 1689 / 큐는 `Queue_t` + 저장소를 한 번에 — queue.c:529). 그 힙은 링커 힙이 아니라 보통 `ucHeap[configTOTAL_HEAP_SIZE]` 라는 **정적 배열 하나** 이고, 그 안을 어떻게 나눠 쓰느냐가 heap_1~heap_5 의 차이다.

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">heap_4: ucHeap 안의 블록과 주소순 free list (Cortex-M: 헤더 8 B)</text>
  <rect x="10" y="40" width="90" height="34" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="22" y="62" fill="currentColor" font-family="monospace">xStart</text>
  <text x="12" y="92" fill="currentColor" font-size="12">(아레나 밖 static)</text>
  <rect x="120" y="110" width="20" height="40" class="f-warn" stroke="currentColor"/>
  <rect x="140" y="110" width="80" height="40" class="f-acc" stroke="currentColor"/>
  <text x="150" y="135" fill="currentColor">할당 A</text>
  <rect x="220" y="110" width="20" height="40" class="f-warn" stroke="currentColor"/>
  <rect x="240" y="110" width="110" height="40" class="f-ok" stroke="currentColor"/>
  <text x="252" y="135" fill="currentColor">free 1</text>
  <rect x="350" y="110" width="20" height="40" class="f-warn" stroke="currentColor"/>
  <rect x="370" y="110" width="60" height="40" class="f-acc" stroke="currentColor"/>
  <text x="378" y="135" fill="currentColor">할당 B</text>
  <rect x="430" y="110" width="20" height="40" class="f-warn" stroke="currentColor"/>
  <rect x="450" y="110" width="190" height="40" class="f-ok" stroke="currentColor"/>
  <text x="470" y="135" fill="currentColor">free 2 (나머지)</text>
  <rect x="640" y="110" width="30" height="40" class="f-mut" stroke="currentColor"/>
  <text x="634" y="170" fill="currentColor" font-family="monospace">pxEnd</text>
  <text x="620" y="186" fill="currentColor" font-size="12">size 0, 아레나 안</text>
  <path d="M100 57 C 180 57, 230 80, 230 108" stroke="currentColor" fill="none"/>
  <path d="M232 108 C 300 70, 400 70, 440 108" stroke="currentColor" fill="none"/>
  <path d="M442 108 C 520 75, 600 75, 652 108" stroke="currentColor" fill="none"/>
  <text x="300" y="70" fill="currentColor" font-size="12">next (주소 오름차순)</text>
  <text x="120" y="172" fill="currentColor" font-size="12">헤더 = { next, size }</text>
  <text x="120" y="188" fill="currentColor" font-size="12">할당 블록: size MSB = 1, next = NULL</text>
  <rect x="10" y="204" width="720" height="58" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="224" fill="currentColor">malloc: xStart 부터 주소순으로 걸어 처음 맞는 블록 (first fit), 남는 조각 &gt; 16 B 면 쪼갬</text>
  <text x="22" y="244" fill="currentColor">free: 주소순 자리에 꽂으며 바로 앞·뒤 free 블록과 붙어 있으면 합침 → 이웃 free 둘이 붙어 있는 일이 없다</text>
</svg>
```

호스트에서는 `ucHeap` 대신 `uint8_t` 배열을 쓰고, 헤더를 **아레나 기준 오프셋** 으로 연결한 모델을 만든다. 포인터 대신 오프셋이라 헤더 크기 `hdr` 를 바꿔 Cortex-M(8 B)과 64비트 호스트(16 B) 두 기하를 같은 코드로 흉내 낼 수 있고, 헤더는 `memcpy` 로 읽고 써서 정렬·aliasing 문제가 없다(FreeRTOS 는 `uint8_t` 배열을 `BlockLink_t *` 로 캐스팅한다).

| FreeRTOS V11.3.2 | 모델 | 비고 |
|---|---|---|
| `xHeapStructSize` (heap_4.c:158) | `h->hdr` | `sizeof(BlockLink_t)` 를 8 로 올림: CM4 8 B, arm64 16 B (둘 다 컴파일러로 확인) |
| `heapMINIMUM_BLOCK_SIZE = xHeapStructSize << 1` (59) | `2u * h->hdr` | 쪼개기 기준 |
| `heapBLOCK_ALLOCATED_BITMASK` (80) | `ALLOC_BIT` | size 의 MSB |
| `prvHeapInit` (456–501) | `h4_init` | `pxEnd` 를 아레나 끝 **안쪽** 에 둠 → 8 KB 중 8184 B 사용 |
| `pvPortMalloc` (173–351) | `h4_malloc` (TODO 1) | first fit, 쪼개기, min-ever |
| `prvInsertBlockIntoFreeList` (504–569) | `h4_insert_free` (TODO 2) | 앞·뒤 병합 |
| heap_2.c `pvPortMalloc` / 삽입 매크로 (135–153) | `h2_malloc` / `h2_insert_free` (TODO 3) | 크기순 = best fit |
| `vPortGetHeapStats` (572–621) | `heap_get_stats` (TODO 4) | `HeapStats_t` 7개 값 (portable.h:156–165) |

### Part A — heap_4 를 다시 만든다

`h4_malloc` 과 `h4_insert_free` 를 채운다. 테스트는 1 KB 아레나에서 블록 오프셋·크기를 정확히 확인한다: `malloc(1)` 이 16 B 블록(헤더 8 + 1 → 8 의 배수), 남는 조각이 정확히 16 B 면 **쪼개지 않고** 통째로 주기, 앞만/뒤만/양쪽 병합, `xStart` 다음 블록, 정렬 안 된 아레나 시작, `xFreeBytesRemaining` 보다 큰 요청은 리스트를 걷지도 않고 거절.

### Part B — heap_2 와 비교: 단편화 · 첫 실패 · 결정성

`h2_malloc` / `h2_insert_free` 를 채운다. 같은 8 KB 아레나로:

- first fit vs best fit 이 다른 블록을 고르는 배치, 같은 크기 블록은 LIFO.
- **병적 패턴**: 살아 있는 블록은 늘 1개뿐인데 요청이 16, 32, 48 … B 로 커진다 → heap_2 는 512 B 에서 실패 (8184 B 전부 비어 있는데!), heap_4 는 8192 B 에서야 실패.
- xorshift 워크로드 (8–512 B, 오래 사는 객체 섞음, 5000 step, 시드 8개): 첫 실패 step, 실패 시 free·최대 블록·단편화, min-ever, 실패 횟수, free list 최대 방문 노드 수. **매 연산 뒤 불변식 검사**(주소순/크기순, heap_4 는 이웃 free 없음, 블록 크기 합 = 아레나, free 바이트 일치)와 블록마다 채운 태그 바이트 확인.

### Part C — LTE 센서 노드: "바이트는 있는데 OTA 버퍼가 안 잡힌다"

STM32F4 급 노드, `configTOTAL_HEAP_SIZE` 16 KB. 부팅 때 태스크 4개·큐 2개, 실행 중 모뎀 응답 버퍼 24개가 잠깐 쓰이고 풀린 뒤 OTA 가 8 KB 연속 버퍼를 요청한다. 1 KB 로그 버퍼를 "처음 로그를 찍을 때" 지연 할당하는 펌웨어와 부팅 때 할당하는 펌웨어를 비교한다. `heap_get_stats` (TODO 4)와 감시 함수 `heap_health` (TODO 5: 저수위 · 단편화 · 고갈 플래그)를 채운다.

## 2. Problem (English)

> **Implement FreeRTOS-style heap_4 and heap_2 allocators on a static arena and compare them.**
>
> The heap is a `uint8_t` arena. Every block starts with a header `{next, size}` (`hdr` bytes, 8 on Cortex-M). The MSB of `size` marks a block as allocated. A request of `n` bytes needs `wanted = roundup8(n + hdr)`.
>
> 1. `h4_malloc(h, n)`: free list in **address order** ending at an end marker of size 0 placed inside the arena. Reject immediately if `wanted > free_bytes`. First fit; if `size - wanted > 2*hdr` split and keep the tail in the list. Track `free_bytes`, `min_ever`, and the number of list nodes visited.
> 2. `h4_insert_free(h, b)`: insert by address and **coalesce** with the previous and/or next free block (never with the end marker).
> 3. `h2_malloc` / `h2_insert_free`: free list in **size order** (equal sizes: newest first) → best fit, no coalescing.
> 4. `heap_get_stats`: like `vPortGetHeapStats` (available, largest, smallest, count, min-ever, #alloc, #free).
> 5. `heap_health(stats, policy)`: return flags LOW_WATER (min-ever below a threshold), FRAGMENTED (enough bytes but no block large enough), EXHAUSTED.
>
> Example (`hdr = 8`, 1024-byte arena): usable 1016; `malloc(1), malloc(24), malloc(100), malloc(8)` return offsets 8, 24, 56, 168; freeing the 2nd, 4th, 3rd, 1st blocks yields 2, 2, 1, 1 free blocks with largest 840, 856, 1000, 1016.
>
> Follow-ups: Why address order? Is it O(1)? Can an ISR call it? Why does heap_2 fail with 8 KB free? How do you size `configTOTAL_HEAP_SIZE`?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/RTOS-M10_st && /tmp/RTOS-M10_st
# 처음엔 "16/41 passed", exit 1 — TODO 1~5 를 채워 41/41 을 만든다
```

초기화(`h4_init`, `h2_init`), 크기 계산(`wanted_size`), free 검증(`heap_free`), 불변식 검사(`heap_check`), 워크로드(`run_workload`), Part C 시나리오(`lte_node`)는 주어진 코드다. 워크로드는 할당기 인터페이스(`alloc_if_t`)로 할당기를 받는다 — 데모가 **같은 함수** 에 `pvPortMalloc` 을 끼운다.

진짜 FreeRTOS 로 확인하는 데모 (체크 대상 아님, 커널 소스 `~/.esn_scratch/FreeRTOS-Kernel` V11.3.2 필요). 레슨 폴더의 `FreeRTOSConfig.h` 는 템플릿에서 힙만 바꿨다 (`configTOTAL_HEAP_SIZE 8192`, `configAPPLICATION_ALLOCATED_HEAP 1`, 실패 훅 1, 타이머 0):

```bash
K=~/.esn_scratch/FreeRTOS-Kernel; P=$K/portable/ThirdParty/GCC/Posix
# ① heap_4.c 로 빌드 (템플릿의 heap_3.c 대신) — solution.c 를 #include 해서 같은 워크로드
cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_heap.c \
   $K/{tasks,list,queue}.c $K/portable/MemMang/heap_4.c $P/port.c $P/utils/wait_for_event.c -o /tmp/RTOS-M10_d4 && /tmp/RTOS-M10_d4
# ② heap_2.c 로: 같은 줄에 -DUSE_HEAP2, heap_4.c → heap_2.c, -o /tmp/RTOS-M10_d2
# ③ heap_5.c 영역 2개 (연습 2 와 같은 숫자)
cc -O2 -g -std=gnu11 -Wall -Wextra -pthread -I. -I$K/include -I$P -I$P/utils demo_freertos_heap5.c \
   $K/{tasks,list,queue}.c $K/portable/MemMang/heap_5.c $P/port.c $P/utils/wait_for_event.c -o /tmp/RTOS-M10_d5 && /tmp/RTOS-M10_d5
```

## 4. 힌트

<details>
<summary>힌트 1 — "앞 노드" 를 들고 다닌다</summary>

단일 연결 리스트에서 블록을 빼거나 그 자리에 쪼갠 조각을 꽂으려면 **앞 노드** 가 필요하다. FreeRTOS 는 `pxPreviousBlock = &xStart` 로 시작한다. 모델에서는 `prev = NIL` 이 `xStart` 를 뜻하고 `get_link(h, prev)` / `set_link(h, prev, v)` 가 그 차이를 숨긴다.

</details>

<details>
<summary>힌트 2 — 끝 표시가 걷기를 멈춘다</summary>

heap_4 의 끝 표시는 size 0, next NIL 이다. `while (size(b) < wanted && next(b) != NIL)` 은 맞는 블록을 찾거나 끝 표시에서 멈춘다. 멈춘 곳이 `h->span`(끝 표시)이면 실패. heap_2 의 `xEnd` 는 아레나 밖에 있고 size 가 아레나 전체라서 "모든 요청보다 크거나 같은" 보초 역할을 한다 (heap_2.c:385) — 모델에서는 NIL 검사로 대신한다.

</details>

<details>
<summary>힌트 3 — 쪼갤 때 순서</summary>

먼저 `prev → next(b)` 로 b 를 뺀다. 남는 조각 `nb = b + wanted` 의 size 를 쓰고, `nb->next = prev 의 현재 next`, `prev->next = nb` — 주소순이라 같은 자리다. 비교는 `>` (같으면 안 쪼갬). heap_2 는 크기순이라 `nb` 를 크기 자리에 다시 꽂는다.

</details>

<details>
<summary>힌트 4 — 병합은 두 번의 "끝 주소 == 시작 주소" 검사</summary>

`it` = 주소가 b 보다 작은 마지막 free 노드. ① `it + size(it) == b` 면 it 에 흡수하고 이후 b 는 it. ② `b + size(b) == next(it)` 면 뒤를 흡수 — 단 뒤가 끝 표시면 흡수하지 않고 `next(b) = 끝 표시`. ③ `it != b` 일 때만 `next(it) = b`: 앞과 합쳐졌다면 b == it 이라 자기 자신을 가리키게 된다 (heap_4.c:557–564 주석).

</details>

<details>
<summary>힌트 5 — 감시 플래그 순서</summary>

`available < need` 면 단편화를 따질 의미가 없다 (EXHAUSTED). `available >= need` 인데 `largest < need` 일 때만 FRAGMENTED. 저수위는 현재 값이 아니라 **min-ever** 로 본다 — 지금은 괜찮아도 부팅 이후 가장 위험했던 순간이 기준이다.

</details>

## 5. 풀이

### Part A — first fit, 쪼개기, 병합

`h4_malloc` 의 핵심은 FreeRTOS 와 같은 순서다:

```c
if (w == 0 || w > h->free_bytes) return NULL;          /* 빠른 거절 (heap_4.c:240) */
while (blk_size(h, b) < w && blk_next(h, b) != NIL) { prev = b; b = blk_next(h, b); }   /* 248–253 */
if (b == h->span) return NULL;                         /* 끝 표시 = 맞는 블록 없음 (257) */
set_link(h, prev, blk_next(h, b));                     /* 리스트에서 뺌 (266) */
if (blk_size(h, b) - w > 2u * h->hdr) { ... }          /* 쪼개기 (272–289) */
```

크기 규칙을 숫자로 (CM4 = 헤더 8 B, 호스트 = 16 B, 정렬 8):

| 요청 | CM4 블록 | 호스트 블록 | 비고 |
|---|---|---|---|
| `malloc(1)` | 16 | 24 | 헤더 + 1 → 8 의 배수. 데모: 첫 포인터 = `ucHeap + 16`, 남은 8152 = 8176 − 24 |
| `malloc(84)` (TCB) | 96 | 104 | |
| `malloc(1024)` (스택 256 워드) | 1032 | 1040 | |
| 남는 조각이 정확히 `2*hdr` | 안 쪼갬 | 안 쪼갬 | 테스트: `malloc(992)` 가 1016 B 통째 (`free_bytes == 0`) |
| 아레나 8192 B 사용 가능 | 8184 | 8176 | 끝 표시가 헤더 하나만큼 차지 |

Part A 테스트의 free 순서와 병합 결과 (1 KB 아레나, 블록 a[0,16) b[16,48) c[48,160) d[160,176), 나머지 [176,1016)):

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">prvInsertBlockIntoFreeList 의 네 경우 (Part A 테스트 그대로, 오프셋은 바이트)</text>
  <g fill="currentColor"><text x="10" y="58">처음</text><text x="10" y="108">free(b)</text><text x="10" y="158">free(d)</text><text x="10" y="208">free(c)</text><text x="10" y="258">free(a)</text></g>
  <rect x="110" y="40" width="40" height="28" class="f-acc" stroke="currentColor"/><text x="124" y="59" fill="currentColor">a</text>
  <rect x="150" y="40" width="60" height="28" class="f-acc" stroke="currentColor"/><text x="175" y="59" fill="currentColor">b</text>
  <rect x="210" y="40" width="120" height="28" class="f-acc" stroke="currentColor"/><text x="265" y="59" fill="currentColor">c</text>
  <rect x="330" y="40" width="40" height="28" class="f-acc" stroke="currentColor"/><text x="344" y="59" fill="currentColor">d</text>
  <rect x="370" y="40" width="300" height="28" class="f-ok" stroke="currentColor"/><text x="460" y="59" fill="currentColor">free 840</text>
  <rect x="110" y="90" width="40" height="28" class="f-acc" stroke="currentColor"/>
  <rect x="150" y="90" width="60" height="28" class="f-ok" stroke="currentColor"/><text x="160" y="109" fill="currentColor">32</text>
  <rect x="210" y="90" width="120" height="28" class="f-acc" stroke="currentColor"/>
  <rect x="330" y="90" width="40" height="28" class="f-acc" stroke="currentColor"/>
  <rect x="370" y="90" width="300" height="28" class="f-ok" stroke="currentColor"/><text x="460" y="109" fill="currentColor">840</text>
  <text x="680" y="109" fill="currentColor" font-size="12">병합 없음</text>
  <rect x="110" y="140" width="40" height="28" class="f-acc" stroke="currentColor"/>
  <rect x="150" y="140" width="60" height="28" class="f-ok" stroke="currentColor"/><text x="160" y="159" fill="currentColor">32</text>
  <rect x="210" y="140" width="120" height="28" class="f-acc" stroke="currentColor"/>
  <rect x="330" y="140" width="340" height="28" class="f-ok" stroke="currentColor"/><text x="460" y="159" fill="currentColor">856 (d+나머지)</text>
  <text x="680" y="159" fill="currentColor" font-size="12">뒤와</text>
  <rect x="110" y="190" width="40" height="28" class="f-acc" stroke="currentColor"/>
  <rect x="150" y="190" width="520" height="28" class="f-ok" stroke="currentColor"/><text x="330" y="209" fill="currentColor">1000 (b+c+d+나머지)</text>
  <text x="680" y="209" fill="currentColor" font-size="12">앞·뒤</text>
  <rect x="110" y="240" width="560" height="28" class="f-ok" stroke="currentColor"/><text x="330" y="259" fill="currentColor">1016 = 처음 상태</text>
  <text x="680" y="259" fill="currentColor" font-size="12">xStart 뒤</text>
  <text x="10" y="290" fill="currentColor" font-size="12">통계: 블록 2개(최대 840) → 2개(856) → 1개(1000) → 1개(1016). min-ever = 840, 할당 4 · 해제 4.</text>
</svg>
```

왜 **주소순** 리스트인가: 병합하려면 "바로 앞·뒤 블록이 free 인가" 를 알아야 한다. 주소순이면 삽입 위치를 찾는 순간 앞(it)과 뒤(next)가 손에 들어온다. 크기순 리스트(heap_2)에서는 물리적 이웃이 리스트 어디에 있는지 모른다 — 그래서 heap_2 는 병합을 아예 안 한다. glibc 같은 범용 할당기는 블록 끝에 크기를 하나 더 두는 boundary tag(Knuth, TAOCP Vol.1 §2.5)로 O(1) 이웃 찾기를 하지만, heap_4 는 헤더를 8 B 로 줄이는 대신 삽입을 O(free 블록 수)로 받아들였다.

### Part B — heap_2 는 정말 무엇을 하나

heap_2.c 주석과 코드가 그대로 말한다: "linked list structure … to link free blocks in **order of their size**" (heap_2.c:97–98), 삽입은 `pxIterator->pxNextFreeBlock->xBlockSize < xBlockSize` 동안 전진 (144) — 같은 크기면 기존 블록 **앞** 에 꽂히니 같은 크기끼리는 LIFO. 할당은 "traverse the list from the start (smallest) block until one of adequate size is found" (222–223) = 맞는 것 중 **가장 작은** 블록 = best fit. 그리고 free 는 `prvInsertBlockIntoFreeList` 만 부른다 (329) — 병합 코드가 없다. 대신 heap_2 에는 `xMinimumEverFreeBytesRemaining` 도 `vPortGetHeapStats` 도 없다 (xPortGetFreeHeapSize 만, 340) — 모델은 비교를 위해 min-ever 를 따로 센다.

**같은 아레나 크기**: CM4 기하에서 heap_4 는 끝 표시 8 B, heap_2 는 `configADJUSTED_HEAP_SIZE = TOTAL − portBYTE_ALIGNMENT` (60) 로 8 B 를 빼서 둘 다 8184 B 를 쓴다. 공정한 비교다.

**병적 패턴 — 살아 있는 블록 1개, 요청만 커진다** (실제 출력):

```text
growing request, 1 live block: heap_2 fails at k=32 (512 B), free=8184 in 31 blocks, largest=504 | heap_4 fails at k=512 (8192 B)
```

```svg
<svg viewBox="0 0 740 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">16·k B 요청 → 해제 반복, k = 1..31 뒤의 아레나 (8184 B, 비례 축척)</text>
  <text x="30" y="60" fill="currentColor">heap_2: 조각 31개 = 24, 40, 56 … 504 B (k 번째 요청의 블록 16k+8), 전부 free</text>
  <rect x="30.0" y="70" width="2.1" height="34" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
  <rect x="32.1" y="70" width="3.4" height="34" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <rect x="35.5" y="70" width="4.8" height="34" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
  <rect x="40.3" y="70" width="6.2" height="34" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <rect x="46.4" y="70" width="7.5" height="34" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
  <rect x="53.9" y="70" width="8.9" height="34" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <rect x="62.8" y="70" width="10.3" height="34" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
  <rect x="73.1" y="70" width="11.6" height="34" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <rect x="84.7" y="70" width="13.0" height="34" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
  <rect x="97.7" y="70" width="14.4" height="34" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <rect x="112.1" y="70" width="15.7" height="34" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
  <rect x="127.8" y="70" width="17.1" height="34" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <rect x="145.0" y="70" width="18.5" height="34" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
  <rect x="163.4" y="70" width="19.8" height="34" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <rect x="183.3" y="70" width="21.2" height="34" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
  <rect x="204.5" y="70" width="22.6" height="34" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <rect x="227.1" y="70" width="23.9" height="34" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
  <rect x="251.0" y="70" width="25.3" height="34" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <rect x="276.3" y="70" width="26.7" height="34" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
  <rect x="303.0" y="70" width="28.1" height="34" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <rect x="331.1" y="70" width="29.4" height="34" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
  <rect x="360.5" y="70" width="30.8" height="34" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <rect x="391.3" y="70" width="32.2" height="34" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
  <rect x="423.5" y="70" width="33.5" height="34" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <rect x="457.0" y="70" width="34.9" height="34" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
  <rect x="491.9" y="70" width="36.3" height="34" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <rect x="528.1" y="70" width="37.6" height="34" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
  <rect x="565.8" y="70" width="39.0" height="34" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <rect x="604.8" y="70" width="40.4" height="34" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
  <rect x="645.2" y="70" width="41.7" height="34" class="f-ok" stroke="currentColor" stroke-width="0.5"/>
  <rect x="686.9" y="70" width="43.1" height="34" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
  <text x="30" y="122" fill="currentColor" font-size="12">k=32: 520 B 블록 필요, 최대 조각 504 B → 실패. free 바이트 = 8184 (100 %)</text>
  <text x="30" y="150" fill="currentColor">heap_4: 해제할 때마다 나머지와 다시 합쳐져 늘 한 덩어리</text>
  <rect x="30" y="160" width="700" height="28" class="f-ok" stroke="currentColor"/>
  <text x="300" y="179" fill="currentColor">8184 B 한 블록 → k=512 (8200 B 블록) 에서야 실패</text>
</svg>
```

현실 판: 크기가 매번 다른 패킷·문자열 버퍼(AT 응답, JSON). 같은 크기만 반복하면 heap_2 도 괜찮다 — 연습 4 가 태스크 생성/삭제(스택 512 고정)로 2만 step 무실패를 보인다.

**무작위 워크로드** — 같은 8 KB, 같은 요청 흐름, 동시 최대 20개, 5000 step. solution.c 실제 출력 (CM4 기하, hdr 8):

```text
seed | heap_4 first_fail free@fail largest frag% min_ever fails maxwalk | heap_2 first_fail free@fail largest frag% min_ever fails maxwalk
   1 |    742      3136     376   88.1     1240    17      15 |    148      4264     472   89.0     3200  1412      96
   2 |    820      2488     472   81.1     1280    10      13 |     93      2288     368   84.0     1568  1355     105
   3 |    167      2264     448   80.3     1440     9      13 |     84      2488     368   85.3     2024  1413     118
   4 |    322      2528     384   84.9     1024    19      13 |    113      2936     416   85.9     1800  1373      99
   5 |    422      1944     408   79.1     1320     6      13 |    123      3312     416   87.5     1912  1507     116
   6 |    191      1712     392   77.2      920    30      13 |    146      3304     456   86.2     3112  1513     122
   7 |    836      1784     384   78.5     1160    21      14 |    169      4272     344   92.0     2656  1456     111
   8 |    831      2856     456   84.1     1200    11      12 |    103      1976     320   83.9     1816  1347     105
maxlive 16: heap_4 seeds with 0 failures = 8/8, heap_2 failed requests = 10986 (of 40000 steps)
```

읽는 법:

| 지표 | heap_4 | heap_2 | 해석 |
|---|---|---|---|
| 첫 실패 step | 167–836 | 84–169 | 8 시드 모두 heap_4 가 같거나 늦다 (테스트 `h4_later == 8`) |
| 실패 횟수 / 5000 | 6–30 | 1347–1513 | heap_2 는 한 번 쪼개지면 회복이 없다: 요청의 약 절반이 실패 |
| frag% (실패 순간) | 77–88 % | 84–92 % | **둘 다 높다** — 실패했다는 건 원래 "최대 블록 &lt; 요청" 이므로. 비교할 것은 얼마나 자주·일찍 그 상태가 되느냐 |
| min-ever | 920–1440 | 1568–3200 | heap_2 가 "더 여유" 처럼 보이는 건 실패가 많아 **덜 할당했기** 때문. min-ever 만 보고 할당기를 고르면 안 된다 |
| 최대 방문 노드 | 12–15 | 96–122 | 실행 시간의 상한 지표. heap_2 는 조각이 쌓여 리스트가 길다 |
| 동시 16개로 낮추면 | 실패 0 (8/8) | 10986 실패 | heap_4 는 부하 문제, heap_2 는 단편화 문제 |

frag% 정의: `1000 × (1 − largest / free)` 의 permille 을 % 로 출력. 불변식 위반 0, 태그 손상 0 이 모든 시드·연산에서 확인됐다 (`CHECK(inv_bad == 0 && corrupt == 0)`).

**결정성**: heap_4 도 O(1) 이 아니다. malloc 은 first fit 걷기, free 는 주소순 삽입 걷기 — 둘 다 free 블록 수에 비례한다. 걷는 동안 `vTaskSuspendAll()` 로 스케줄러만 멈추므로 (heap_4.c:221, 390) 인터럽트는 계속 들어오지만, 그 시간만큼 **모든 태스크 전환이 늦어진다**. 실시간 루프 안에서 할당하지 말아야 하는 이유다. O(1) 이 필요하면 고정 크기 풀(RTOS-B11) 또는 TLSF (Masmano, Ripoll, Crespo, Real 2004, "TLSF: a New Dynamic Memory Allocator for Real-Time Systems", ECRTS) 같은 분리 리스트 할당기를 쓴다.

### 진짜 FreeRTOS 와 비교 — `demo_freertos_heap.c`

데모는 solution.c 를 `#include` 해서 **같은 `run_workload`** 를 `pvPortMalloc`/`vPortFree` 에 돌리고, 모델은 호스트 기하(`hdr = 16`)로 돌린다. `ucHeap` 은 데모가 16 B 정렬로 정의(`configAPPLICATION_ALLOCATED_HEAP 1`)해서 정렬 손실이 0 이고, 시드마다 `vPortHeapResetState()` (heap_4.c:629, heap_2.c:401)로 힙을 처음으로 되돌린다. 비교 항목: 연산 5000개 각각의 (종류, 반환 오프셋, `xFreeBytesRemaining`), 끝의 `vPortGetHeapStats` 7개 값, 실패 훅 호출 수. 실제 출력:

```text
heap_4, configTOTAL_HEAP_SIZE=8192, sizeof(BlockLink-like header)=16, ucHeap % 16 = 0
first pvPortMalloc(1) - ucHeap = 16, free after = 8152
seed | first_fail  fails hook | ops equal (kind,off,avail) | end avail largest blocks min_ever n_alloc n_free | model
   1 |        650     17   17 |  5000/5000 |      2952    1408      6     1072    2105   2090 | SAME
   2 |        793     21   21 |  5000/5000 |      5048    2968      5      816    2132   2117 | SAME
   3 |        167     15   15 |  5000/5000 |      2392     952     11     1136    2140   2121 | SAME
   4 |        152     36   36 |  5000/5000 |      4704    4136      6      872    2150   2130 | SAME
   5 |        420     10   10 |  5000/5000 |      3456    1416      7     1272    2129   2111 | SAME
   6 |        191     32   32 |  5000/5000 |      2816    1600      8      952    2150   2131 | SAME
   7 |        299     30   30 |  5000/5000 |      4920    1648      8      856    2156   2142 | SAME
   8 |        724      5    5 |  5000/5000 |      2768    1384      8      840    2081   2062 | SAME
heap_4: model == real FreeRTOS on all 8 seeds
```

```text
heap_2, configTOTAL_HEAP_SIZE=8192, sizeof(BlockLink-like header)=16, ucHeap % 16 = 0
first pvPortMalloc(1) - ucHeap = 16, free after = 8160
seed | first_fail  fails hook | ops equal (kind,off,avail) | end avail largest blocks min_ever n_alloc n_free | model
   1 |        148   1361 1361 |  5000/5000 |      8024       0      0        0       0      0 | SAME
   2 |        114   1295 1295 |  5000/5000 |      7920       0      0        0       0      0 | SAME
   3 |         82   1344 1344 |  5000/5000 |      7352       0      0        0       0      0 | SAME
   4 |        114   1235 1235 |  5000/5000 |      7072       0      0        0       0      0 | SAME
   5 |        102   1137 1137 |  5000/5000 |      7656       0      0        0       0      0 | SAME
   6 |        146   1316 1316 |  5000/5000 |      7688       0      0        0       0      0 | SAME
   7 |        139   1243 1243 |  5000/5000 |      7792       0      0        0       0      0 | SAME
   8 |         97   1357 1357 |  5000/5000 |      6776       0      0        0       0      0 | SAME
heap_2: model == real FreeRTOS on all 8 seeds
```

(heap_2 줄의 largest/blocks/min_ever 등은 heap_2 에 `vPortGetHeapStats` 가 없어서 0 이다. 비교는 연산별 오프셋·free 바이트·실패 수로 했다.)

바이트 단위로 같으려면 맞춰야 했던 것: 헤더 크기(64비트에서 16, 그래서 첫 포인터가 `ucHeap + 16`), 쪼개기의 `>`(같으면 안 쪼갬), heap_4 끝 표시가 아레나 안 16 B 를 먹는 것(8192 → 8176), heap_2 는 정렬과 무관하게 8 B 를 빼는 것(8192 → 8184, 그래서 첫 `malloc(1)` 뒤 8160), 같은 크기 LIFO. 호스트 숫자가 CM4 표와 다른 것(시드 1 첫 실패 742 vs 650)은 헤더가 8 B 더 커서 블록마다 8 B 씩 더 먹기 때문이다 — **같은 펌웨어도 포트에 따라 힙 예산이 달라진다.**

### heap_1 ~ heap_5 한 표로 (V11.3.2 소스 기준)

| | heap_1 | heap_2 | heap_3 | heap_4 | heap_5 |
|---|---|---|---|---|---|
| 방식 | 범프 포인터 `xNextFreeByte` (75) | 크기순 free list, best fit | 표준 `malloc`/`free` 감싸기 (63–68, 87–92) | 주소순 free list, first fit | heap_4 + 여러 영역 |
| free | **불가**: `configASSERT( pv == NULL )` (151) | 가능 | 가능 | 가능 | 가능 |
| 병합 | — | **없음** ("does not combine adjacent free blocks", 31–32) | libc 구현에 따름 | 있음 (504–569) | 있음 |
| 결정성 | O(1), 단편화 0 | O(free 블록 수), 조각 누적 | libc 에 따름 (미검증) | O(free 블록 수) | O(free 블록 수) |
| 힙 위치 | `ucHeap` | `ucHeap` | 링커 힙 (`configTOTAL_HEAP_SIZE` 를 안 씀 — 파일에 등장 0회) | `ucHeap` | `vPortDefineHeapRegions` 배열 (주소 오름차순, 55, 614) |
| 통계 | `xPortGetFreeHeapSize` (162) | 같음 (340) | 없음 | + min-ever, `vPortGetHeapStats` (413–621) | 같음 (455–) |
| 동기화 | `vTaskSuspendAll` | 같음 | 같음 (libc 를 스케줄러 정지로 보호) | 같음 | 같음 |
| 언제 | 부팅 때만 만들고 안 지움 | 같은 크기만 반복 (레거시) | 이미 libc 힙을 쓰는 시스템 | **기본 선택** | 내부 SRAM + CCM/외부 SDRAM 처럼 떨어진 메모리 |

heap_1 의 함정 하나: 조건이 `( xNextFreeByte + xWantedSize ) < configADJUSTED_HEAP_SIZE` (118) 로 `<` 라서 남은 공간에 **딱 맞는** 마지막 요청은 실패한다 (연습 1: 8 B 남았다고 보고하는데 `malloc(8)` 실패). heap_5 는 영역 사이 끝 표시(size 0)가 free list 에 남아 `xNumberOfFreeBlocks` 에 세어진다 — 그래서 `vPortGetHeapStats` 가 최소 크기를 셀 때 0 을 일부러 건너뛴다 (heap_5.c:703–712 주석 "zero sized block at the end of each region"). 진짜 heap_5 데모 출력:

```text
after vPortDefineHeapRegions       free=1504 blocks=3 largest=1008 smallest= 496
pvPortMalloc(1200) -> NULL (영역 하나에 안 들어감)
malloc(900) at sram+16, malloc(400) at sram+2064
after 900 + 400                    free= 168 blocks=3 largest=  88 smallest=  80
after free both                    free=1504 blocks=3 largest=1008 smallest= 496
malloc(992) at sram+16
after whole-A alloc + free         free=1504 blocks=2 largest=1008 smallest= 496
```

마지막 줄: 영역 A 전체를 반납하면 뒤의 끝 표시(size 0)와 "병합" 되어 리스트에서 빠진다 (`pxEnd` 가 아니므로 heap_4 와 같은 병합 분기, size 0 을 더할 뿐). 블록 수만 3 → 2 로 바뀌고 해는 없다. 영역 A 끝과 영역 B 시작은 주소가 이어지지 않으니 영역을 넘는 병합은 없다.

<!-- code: demo_freertos_heap.c -->

### Part C — LTE 노드: 로그 버퍼 한 개의 위치

실제 출력:

```text
LTE node  lazy log : avail=11168 largest=8120 blocks=2 min_ever=6752 ota=FAIL health=2
LTE node  boot log : avail=11168 largest=11168 blocks=1 min_ever=6752 ota=ok health=0
```

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">응답 버퍼 24개를 모두 푼 뒤의 16 KB 힙 (축척: 16376 B = 700 px)</text>
  <text x="30" y="46" fill="currentColor">지연 할당: 로그 버퍼가 16번째 응답 뒤에 박힘</text>
  <rect x="30" y="54" width="178.5" height="32" class="f-mut" stroke="currentColor"/><text x="70" y="75" fill="currentColor">부팅 4176</text>
  <rect x="208.5" y="54" width="130.3" height="32" class="f-ok" stroke="currentColor"/><text x="230" y="75" fill="currentColor">free 3048</text>
  <rect x="338.8" y="54" width="44.1" height="32" class="f-warn" stroke="currentColor"/><text x="343" y="75" fill="currentColor">log</text>
  <rect x="382.9" y="54" width="347.1" height="32" class="f-ok" stroke="currentColor"/><text x="500" y="75" fill="currentColor">free 8120</text>
  <text x="30" y="116" fill="currentColor">부팅 때 할당: 로그 버퍼가 부팅 영역 바로 뒤</text>
  <rect x="30" y="124" width="178.5" height="32" class="f-mut" stroke="currentColor"/><text x="70" y="145" fill="currentColor">부팅 4176</text>
  <rect x="208.5" y="124" width="44.1" height="32" class="f-warn" stroke="currentColor"/><text x="213" y="145" fill="currentColor">log</text>
  <rect x="252.6" y="124" width="477.4" height="32" class="f-ok" stroke="currentColor"/><text x="430" y="145" fill="currentColor">free 11168</text>
  <rect x="379.5" y="168" width="350.5" height="22" class="f-acc" stroke="currentColor"/>
  <text x="440" y="184" fill="currentColor">OTA 가 필요한 블록 8200 B</text>
  <text x="30" y="214" fill="currentColor" font-size="12">free 는 둘 다 11168 B. 위: 최대 8120 &lt; 8200 → OTA 실패 (FRAGMENTED). 아래: 성공.</text>
</svg>
```

heap_4 의 병합은 **free 끼리만** 합친다. 오래 사는 블록 하나가 큰 free 영역 한가운데 있으면 그 양쪽은 영원히 합쳐질 수 없다. first fit 은 낮은 주소부터 채우므로, **오래 사는 것은 부팅 때 먼저** 할당하면 아레나 바닥에 모이고 위쪽이 한 덩어리로 남는다. 감시 함수는 그 차이를 바이트 수가 아니라 `largest` 로 잡는다:

| 플래그 | 조건 | 펌웨어 대응 |
|---|---|---|
| `HH_LOW_WATER` | `min_ever < low_water` (예: 10 %) | 텔레메트리 보고, 다음 빌드에서 `configTOTAL_HEAP_SIZE` 재산정 (연습 5) |
| `HH_FRAGMENTED` | `available >= need` 이고 `largest < need` | OTA 전 정리(로그 버퍼 반납 후 재할당), 큰 버퍼는 부팅 때 예약, 또는 heap_5 로 전용 영역 |
| `HH_EXHAUSTED` | `available < need` | 기능 거절 / 리셋 정책 |

실제 MCU 에서는 이 판단을 주기 태스크(예: 1 s 헬스 체크)에서 `vPortGetHeapStats` 로 하고, 실패 순간은 `vApplicationMallocFailedHook` (configUSE_MALLOC_FAILED_HOOK, heap_4.c:336–347)에서 잡는다.

### API 매핑

| 모델 (이 레슨) | FreeRTOS | Zephyr (문서 기준, 미검증) | CMSIS-RTOS2 (문서 기준, 미검증) |
|---|---|---|---|
| `heap_malloc` / `heap_free` | `pvPortMalloc` / `vPortFree` | `k_malloc` / `k_free` (시스템 힙), `k_heap_alloc(h, n, timeout)` | 커널 객체 메모리는 구현 몫 (RTX5 설정) |
| `heap_get_stats` | `vPortGetHeapStats` (heap_4/5) | `sys_heap_runtime_stats_get` | — |
| `min_ever` | `xPortGetMinimumEverFreeHeapSize` | 런타임 통계의 max allocated | — |
| `h5_define` (연습 2) | `vPortDefineHeapRegions` | `K_HEAP_DEFINE` 로 힙 여러 개 | — |
| 실패 처리 | `vApplicationMallocFailedHook` | NULL 반환 | NULL 반환 |
| 고정 크기 풀 (RTOS-B11) | 없음 (큐+블록 배열로 직접) | `k_mem_slab` | `osMemoryPoolNew/Alloc/Free` |
| 정적 생성 | `xTaskCreateStatic`, `configSUPPORT_DYNAMIC_ALLOCATION 0` | `K_THREAD_DEFINE` 등 | 속성 `cb_mem`/`stack_mem` |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/RTOS-M10_so && /tmp/RTOS-M10_so      # 41/41 passed (5회 동일, 0.06 s)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o /tmp/RTOS-M10_san && /tmp/RTOS-M10_san   # 41/41, 보고 없음
```

체크 대상 코드는 단일 스레드 결정적 모델이다 (TSan 대상 없음). 헤더를 `memcpy` 로 다뤄서 정렬 안 된 아레나(`g_arena + 3`) 테스트도 UBSan 에 걸리지 않는다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 쪼개기 조건을 `>=` 로 | FreeRTOS 는 `>` (heap_4.c:272). 바이트 단위 비교가 어긋난다 (16 B 남을 때 쪼개면 헤더만 있는 0 B 짜리 free 블록) | `if (size - w > 2*hdr)` |
| 앞과 병합한 뒤에도 `prev->next = b` | b == it 이라 자기 자신을 가리키는 순환 → 다음 malloc 무한 루프 | `if (it != b) set_link(h, it, b);` |
| 끝 표시와도 병합 | 끝 표시는 size 0 보초. 흡수하면 리스트 끝이 사라진다 | `if (nx != h->span) 병합 else next(b) = span` |
| 쪼갠 조각을 리스트 맨 앞에 꽂음 | 주소순이 깨져 다음 free 의 병합 판단이 틀린다 | 빼낸 자리(prev 뒤)에 꽂는다 |
| `xPortGetFreeHeapSize()` 만 보고 "충분" | 바이트 합계일 뿐, 단편화를 모른다 (Part C: 11168 B 있는데 8 KB 실패) | `vPortGetHeapStats().xSizeOfLargestFreeBlockInBytes` |
| min-ever 가 큰 할당기를 "여유 있다" 고 판단 | 실패가 잦아 덜 할당한 결과일 수 있다 (heap_2 표) | 실패 수와 함께 본다 |
| ISR 에서 `pvPortMalloc` | `vTaskSuspendAll` 은 스케줄러만 멈춘다. ISR 이 걷는 도중의 리스트를 건드릴 수 있고, ISR 은 블록 불가 | ISR 은 미리 할당한 링 버퍼/풀만 (C-M14) |
| 런타임에 오래 사는 버퍼를 지연 할당 | 큰 free 영역 한가운데 박혀 병합으로도 못 푼다 | 부팅 때 할당, 또는 정적 배열 |
| 64비트 호스트에서 잰 힙 사용량을 MCU 에 그대로 | 헤더 16 vs 8 B, 포인터 크기 차이로 블록마다 달라진다 | 타깃 기하로 계산 (`hdr = 8`), 보드에서 `vPortGetHeapStats` |

## 8. 면접 Q&A

<details>
<summary>Q1. FreeRTOS heap_1~heap_5 를 한 줄씩, 그리고 기본 선택은?</summary>

heap_1 = 범프 포인터, free 불가 (부팅 때만 만드는 시스템, O(1), 단편화 0). heap_2 = 크기순 리스트 best fit, **병합 없음** (같은 크기 반복일 때만 안전). heap_3 = libc `malloc` 을 `vTaskSuspendAll` 로 감쌈 (링커 힙, `configTOTAL_HEAP_SIZE` 무시). heap_4 = 주소순 first fit + 이웃 병합 + min-ever/통계 — 기본 선택. heap_5 = heap_4 + `vPortDefineHeapRegions` 로 떨어진 메모리 여러 개. 꼬리: "안전 인증 제품이면?" → 동적 할당을 아예 끄고(`configSUPPORT_DYNAMIC_ALLOCATION 0`) `...CreateStatic` 만, 또는 부팅 후 할당 금지 + heap_1. MISRA C:2012 Rule 21.3 은 `<stdlib.h>` 의 malloc/free 사용을 금지한다.

</details>

<details>
<summary>Q2. heap_4 의 free list 는 왜 주소순이고, malloc/free 는 몇 시간인가요?</summary>

병합하려면 물리적 이웃을 알아야 하는데, 주소순 리스트면 삽입 위치를 찾는 순간 앞(it)과 뒤(next)가 이웃 후보다: `it + size(it) == b`, `b + size(b) == next` 두 비교로 끝 (heap_4.c:525, 539). 대가로 malloc(first fit)과 free(삽입) 모두 **O(free 블록 수)**. 이 레슨 워크로드에서 최대 방문 노드는 heap_4 12–15, heap_2 96–122. 그 동안 스케줄러가 정지되므로 태스크 전환 지연의 상한에 들어간다. 꼬리: "O(1) 로 하려면?" → 고정 크기 풀, 또는 크기 클래스별 리스트 + 비트맵(TLSF, Masmano et al. 2004).

</details>

<details>
<summary>Q3. 힙에 8 KB 가 비어 있는데 512 B 할당이 실패했습니다. 어떻게 가능하죠?</summary>

외부 단편화 — 바이트는 있지만 한 덩어리가 없다. heap_2 는 병합을 안 하므로, 크기가 커지는 요청을 받았다 풀기만 해도 24, 40, … 504 B 조각 31개로 아레나 전체가 쪼개진다 (실측: free 8184 B, 최대 504 B, 520 B 블록 실패). heap_4 는 반납 때 다시 합치므로 8 KB 까지 된다. 진단은 `xPortGetFreeHeapSize` 가 아니라 `vPortGetHeapStats` 의 `xSizeOfLargestFreeBlockInBytes` 와 `xNumberOfFreeBlocks`. 꼬리: "heap_4 면 단편화가 없나?" → 아니다. 살아 있는 블록이 free 영역 사이에 끼면 병합으로도 못 붙인다 (Part C: 11168 B 비었지만 최대 8120 B).

</details>

<details>
<summary>Q4. pvPortMalloc 을 ISR 에서 불러도 되나요?</summary>

안 된다. heap_1~5 는 모두 `vTaskSuspendAll()` / `xTaskResumeAll()` 로 보호한다 (heap_4.c:221, 334, 390) — 태스크 전환만 막고 인터럽트는 막지 않는다. ISR 이 걷는 중인 리스트를 같이 고치면 깨진다. 게다가 실행 시간이 리스트 길이에 비례하고, 실패 시 훅을 부른다. FromISR 버전도 없다. ISR 에서 버퍼가 필요하면 미리 할당한 링 버퍼나 고정 크기 풀(락 프리 인덱스, C-M14/RTOS-B11)에서 꺼내고, 처리 태스크가 반납한다.

</details>

<details>
<summary>Q5. malloc(1) 은 Cortex-M4 heap_4 에서 실제로 몇 바이트를 쓰나요?</summary>

16 B. 헤더 `BlockLink_t` 8 B (포인터 4 + size_t 4, 컴파일러로 확인) + 1 → 8 의 배수. 게다가 남는 조각이 16 B 이하면 쪼개지 않으므로 최대 16 B 를 더 먹을 수 있다. 끝 표시가 아레나에서 8 B, 시작 정렬이 최대 7 B 를 가져간다. 64비트 호스트에선 헤더가 16 B 라 `malloc(1)` = 24 B (데모: `free after = 8152` = 8176 − 24). 그래서 작은 객체를 많이 동적 할당하면 오버헤드가 크다 — TCB 84 B → 96 B, 세마포어 72 B → 80 B (RTOS-M01 실측).

</details>

<details>
<summary>Q6. configTOTAL_HEAP_SIZE 는 어떻게 정하나요?</summary>

넉넉하게 잡고 돌린 뒤 `xPortGetMinimumEverFreeHeapSize()` (heap_4.c:419) 로 최악 순간의 사용량 `usable − min_ever` 를 잰다. 거기에 끝 표시·정렬과 **단편화 여유** 를 더한다. 연습 5 실측: 같은 크기만 만들고 지우는 부하는 여유 0 % 로 충분했지만(최대 사용 3696 B → 3840 B 로 줄여도 무실패), 스택 크기가 섞이면 0/5/10 % 는 실패, 15 % 부터 무실패였다. 시험 부하가 실제 최악을 덮는지가 진짜 문제이므로, 필드에서도 min-ever 를 텔레메트리로 보낸다. 꼬리: "min-ever 는 리셋되나?" → 부팅 이후 누적. `xPortResetHeapMinimumEverFreeHeapSize()` (425) 로 구간 측정도 된다.

</details>

<details>
<summary>Q7. 힙 오버플로나 이중 해제는 heap_4 가 잡아 주나요?</summary>

일부만. `vPortFree` 는 헤더의 할당 비트와 `next == NULL` 을 `configASSERT` 로 확인한다 (heap_4.c:369–370). 앞 블록이 넘쳐 다음 블록 헤더를 덮으면 보통 `next` 가 NULL 이 아니게 되어 그 블록을 free 할 때 걸리고, 0 으로 덮으면 할당 비트가 사라져 걸린다. 이중 해제는 할당 비트가 이미 0 이라 걸린다 — 단 그 사이에 같은 블록이 다시 할당됐다면 남의 블록을 푸는 것이라 못 잡는다. 헤더를 "그럴듯하게" 덮으면 못 잡는다 (연습 3). 추가 장치: `configENABLE_HEAP_PROTECTOR` (블록 포인터를 카나리와 XOR, `heapVALIDATE_BLOCK_POINTER` 로 범위 검사, 110–136), `configHEAP_CLEAR_MEMORY_ON_FREE`. 근본 대책은 MPU 영역·정적 분석·ASan 이 있는 호스트 테스트.

</details>

<details>
<summary>Q8. heap_5 는 언제, 무엇을 조심해서 쓰나요?</summary>

STM32F4 의 SRAM1/SRAM2 + CCM, 또는 내부 SRAM + 외부 SDRAM 처럼 떨어진 메모리를 한 힙으로 쓰고 싶을 때. 코드는 heap_4 와 같고 `vPortDefineHeapRegions` 가 영역마다 끝 표시를 만들어 이전 영역의 끝 표시를 다음 영역 첫 블록에 연결한다. 조심할 점: ① 첫 `pvPortMalloc` (= 첫 커널 객체 생성) **전에** 불러야 한다 (heap_5.c:40–43) ② 영역은 주소 오름차순 (614 assert) ③ 한 영역보다 큰 요청은 합계가 충분해도 실패 (데모: 1504 B 중 1200 B 실패) ④ CCM 처럼 DMA 가 못 닿는 영역에 DMA 버퍼가 잡힐 수 있다 — 그런 버퍼는 따로 정적 배치. ⑤ 통계의 블록 수에 영역 끝 표시가 섞인다.

</details>

## 9. 추가 연습

### 연습 1 — heap_1 범프 할당기

`h1_init`, `h1_malloc`, `h1_free`, `h1_free_bytes` 를 heap_1.c 그대로 작성하라. 1 KB 아레나에서 8 B 를 몇 번 할당할 수 있고, 그때 `xPortGetFreeHeapSize()` 는 몇을 돌려주나?

<details>
<summary>답 보기</summary>

헤더가 없어 `malloc(1)` 도 8 B (heap_4 는 16 B). 사용 가능 = `1024 − 8 = 1016`. 조건이 `next + w < 1016` 이라 126번(1008 B) 성공 후 남은 8 B 에 8 B 요청이 실패한다 — `xPortGetFreeHeapSize()` 는 8 을 돌려주는데. free 는 `configASSERT(pv == NULL)` 이라 NULL 외에는 거절 (모델은 −1). 실제 출력: `ex1 heap_1: 126 x 8 B allocations, then xPortGetFreeHeapSize()=8 but malloc(8) fails`.

</details>

### 연습 2 — heap_5: 영역 두 개

heap_4 코드는 그대로 두고 `h5_define(h, mem, regions, n, hdr)` 만 작성하라. 영역 A = [0, 1024), B = [2048, 2560), hdr 16 으로 진짜 heap_5 데모의 숫자(1504 / 3 블록 / 1200 실패 / 오프셋 16, 2064 / 영역 A 통째 반납 후 2 블록)를 재현하라.

<details>
<summary>답 보기</summary>

영역마다: 시작을 8 로 올리고, 끝 표시 = `(start + size − hdr) & ~7`, 첫 블록 = [start, end). 첫 영역이면 `xStart` 가, 아니면 **앞 영역의 끝 표시** 가 이 첫 블록을 가리킨다. 마지막 끝 표시만 `pxEnd`(= `h->span`). A: 1008, B: 496, 합 1504. 앞 영역 끝 표시(size 0)는 리스트에 남아 블록 수 3 — 통계에서 최소 크기는 0 을 건너뛴다(heap_5.c:706). malloc 은 size 0 < wanted 라 그냥 지나가고, 영역 A 전체를 반납하면 "뒤 블록과 병합" 분기로 흡수되어 2 블록. 주소 역순 영역은 −1 (`configASSERT(xAddress > pxEnd)`).

</details>

### 연습 3 — free 가 거절하는 이유: 힙 오버플로

`free_reason(h, p)` 를 작성하라: NULL / 범위 밖·정렬 불량 / 할당 표시 없음 / next ≠ NULL / OK. 24 B 블록 a 를 4 B, 8 B 넘치게 0xAA 로 쓰고, 0x00 으로 쓰고, "올바른 헤더" 로 덮은 뒤 b 의 판정을 확인하라.

<details>
<summary>답 보기</summary>

검사 순서는 FreeRTOS 와 같다: `heapVALIDATE_BLOCK_POINTER` → 할당 비트 (369) → `next == NULL` (370). 4 B 0xAA: next 만 덮임 → NEXT_NOT_NULL. 8 B 0xAA: size 도 0xAAAAAAAA 인데 MSB 가 1 이라 "할당됨" 으로 보이고 next 에서 걸림, `heap_check` 의 물리 걷기도 실패. 8 B 0x00: 할당 비트 0 → NOT_ALLOCATED (주의: 진짜 FreeRTOS 에서 NULL 은 0 이라 next 를 0 으로 덮는 4 B 넘침은 **못 잡는다** — 모델의 NIL 은 0xFFFFFFFF 라 다르다). 헤더를 원래 값으로 덮으면 OK — 탐지 불가. 이중 해제는 NOT_ALLOCATED. 모든 쓰기는 같은 아레나 배열 안이라 ASan 도 조용하다 (진짜 OOB 가 아님).

</details>

### 연습 4 — heap_2 가 괜찮은 경우

최대 6개 태스크를 무작위로 만들고 지운다 (스택 먼저 + TCB 84 B, 삭제는 역순). 스택이 늘 512 B 일 때와 256/512/768/1024 B 중 하나일 때, heap_2 와 heap_4 의 첫 실패와 free 블록 수 최대값을 비교하라 (8 KB, 2만 step).

<details>
<summary>답 보기</summary>

실제 출력:

```text
ex4 churn fixed : heap_2 first_fail=-1 max_blocks=13 | heap_4 first_fail=-1 max_blocks=4
ex4 churn varied: heap_2 first_fail=83 max_blocks=32 | heap_4 first_fail=-1 max_blocks=8
```

크기가 고정이면 heap_2 의 best fit 이 방금 반납된 같은 크기 블록을 정확히 다시 쓰므로 조각이 늘지 않는다 (블록 수 13 에서 멈춤). 크기가 섞이면 큰 블록이 작은 요청에 쪼개지고 다시 합쳐지지 않아 83번째 step 에서 실패. heap_4 는 둘 다 무실패. 그래서 heap_2 는 "같은 크기만 만들고 지우는" 레거시 설계에서만 쓴다.

</details>

### 연습 5 — min-ever 로 configTOTAL_HEAP_SIZE 정하기

`advise_total(usable, min_ever, hdr, margin_pct)` = 최대 사용량 + 여유 % + 끝 표시 + 정렬, 256 B 단위 올림. 연습 4 의 두 부하를 8 KB heap_4 에서 재고, 권장 크기로 줄여 다시 돌려 보라. 여유 몇 % 부터 실패가 없나?

<details>
<summary>답 보기</summary>

실제 출력: `ex5 fixed: peak 3696 -> total 3840 (margin 0) ok=1 | varied: peak 6512, first margin with no failure = 15% -> total 7680`. 고정 크기 부하는 단편화가 없어 여유 0 % 로도 무실패. 크기가 섞이면 같은 최대 사용량이라도 배치가 달라져 0/5/10 % 는 실패, 15 % 부터 무실패 (25 % 면 8192). 교훈: min-ever 는 바이트만 잰다. 여유는 측정으로 정하고, 측정 부하가 실제 최악을 덮어야 한다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/RTOS-M10_pr && /tmp/RTOS-M10_pr   # 24/24 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o /tmp/RTOS-M10_ps && /tmp/RTOS-M10_ps   # 보고 없음
```

<!-- code: demo_freertos_heap5.c -->

## 10. 핵심 정리

- RTOS 힙 = 정적 배열 `ucHeap` 하나를 나눠 쓰는 규칙. heap_1 범프(free 불가) · heap_2 크기순 best fit(병합 없음) · heap_3 libc 감싸기 · heap_4 주소순 first fit + 병합(기본) · heap_5 = heap_4 + 여러 영역.
- heap_4 블록 = 헤더 `{next, size|MSB}` + 데이터. wanted = (n + 헤더) 를 8 의 배수로, 남는 조각 &gt; 2×헤더일 때만 쪼갬. CM4 헤더 8 B → `malloc(1)` = 16 B, 64비트 호스트 16 B → 24 B.
- 병합 = 주소순 삽입 위치에서 "앞 끝 == 나", "나 끝 == 뒤" 두 비교. 끝 표시와는 안 합침, 앞과 합쳤으면 링크 갱신 생략.
- heap_2 는 같은 크기 반복엔 괜찮지만 크기가 섞이면 회복 불가: 살아 있는 블록 1개로도 8184 B 빈 힙에서 520 B 실패, 무작위 부하에서 요청의 절반가량 실패.
- heap_4 도 O(free 블록 수)이고 스케줄러를 멈춘 채 걷는다 → 실시간 루프·ISR 에서 할당 금지. O(1) 이 필요하면 고정 크기 풀.
- 진단은 `xPortGetFreeHeapSize` 가 아니라 `vPortGetHeapStats` 의 최대 블록·블록 수, 그리고 min-ever. 오래 사는 버퍼는 부팅 때 할당.
- 모델 = 진짜 heap_4/heap_2: 연산 5000개 × 시드 8개가 오프셋·free 바이트·통계까지 같다 (호스트 기하 hdr 16).

## 참고자료

- FreeRTOS-Kernel V11.3.2 — `portable/MemMang/heap_4.c` 59, 80, 158, 173–351 (`pvPortMalloc`), 221/334/390 (`vTaskSuspendAll`), 354–410 (`vPortFree`, 369–370 assert), 413–428, 456–501 (`prvHeapInit`), 504–569 (`prvInsertBlockIntoFreeList`), 572–621 (`vPortGetHeapStats`), 629 (`vPortHeapResetState`); `heap_2.c` 31–32, 60, 97–98, 135–153, 222–231, 245, 329, 340, 377–392; `heap_1.c` 54, 75, 118, 151, 162; `heap_3.c` 63–68, 87–92; `heap_5.c` 40–66, 570, 614, 703–712; `include/portable.h` 149–165, 198–202; `tasks.c` 1640–1700 (`prvCreateTask` 스택/TCB 순서); `queue.c` 529 — <https://github.com/FreeRTOS/FreeRTOS-Kernel/tree/V11.3.2>
- FreeRTOS — Memory Management (heap_1~5 설명) — <https://www.freertos.org/a00111.html>
- Paul R. Wilson, Mark S. Johnstone, Michael Neely, David Boles, 1995, "Dynamic Storage Allocation: A Survey and Critical Review" (IWMM) — first fit / best fit 단편화 분석
- Donald E. Knuth, *The Art of Computer Programming* Vol. 1, §2.5 Dynamic Storage Allocation — first fit, boundary tag, 병합
- M. Masmano, I. Ripoll, A. Crespo, J. Real, 2004, "TLSF: a New Dynamic Memory Allocator for Real-Time Systems" (ECRTS) — O(1) 실시간 할당기
- MISRA C:2012 Rule 21.3 — `<stdlib.h>` 메모리 할당 함수 사용 금지
- Zephyr — Memory Heaps (`k_heap`, `k_malloc`), Memory Slabs — <https://docs.zephyrproject.org/latest/kernel/memory_management/index.html>
- CMSIS-RTOS2 Memory Pool — <https://arm-software.github.io/CMSIS_6/latest/RTOS2/group__CMSIS__RTOS__PoolMgmt.html>
- 관련 레슨: RTOS-B11 (고정 블록 풀, 포인터 큐), RTOS-M01 (객체 크기 실측, heap_4 블록 규칙), RTOS-M08/M09 (스택 예산·오버플로), C-M14 (ISR 공유 데이터), OS 트랙 (범용 malloc 설계 — see:OS)
