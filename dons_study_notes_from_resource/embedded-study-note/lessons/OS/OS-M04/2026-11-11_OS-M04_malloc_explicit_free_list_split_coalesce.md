---
id: OS-M04
time: 90분
updated: 2026-11-11
---
# OS-M04 malloc 구현 1: 암시적/명시적 free list, split·coalesce

## 학습 목표

- `mmap` 으로 받은 64 MiB 영역 위에 `mem_sbrk` 를 올리고(CS:APP 3e §9.9 memlib), 그 위에서 **boundary tag**(헤더 + 푸터 8 B씩) 블록 형식으로 `mm_malloc / mm_free / mm_realloc` 을 직접 만든다. 할당기 안에서는 진짜 `malloc` 을 한 번도 부르지 않는다.
- 기준선으로 **암시적 리스트 first fit**(모든 블록을 주소 순서로 걷기)을 만든다. 그다음 **명시적 이중 연결 free list** 로 바꾸고, 병합(coalescing) 4가지 경우, 최소 블록 32 B 분할(split), first / next / best fit 선택을 구현한다.
- `realloc` 을 네 갈래로 짠다. 제자리 축소 + 분할, 다음 free 블록 흡수, 힙 끝에서 모자란 만큼만 확장, 그 밖에는 이동이다. 실패하면 원본을 그대로 둔다는 약속도 지킨다.
- 힙 검사기 `mm_check` 를 만든다. 헤더 == 푸터, 붙어 있는 free 블록 없음, 리스트 노드가 힙 안에 있고 free 로 표시됨, 개수 일치, 순환 감지를 본다. 일부러 망가뜨린 힙 5종을 **크래시 없이** 잡는지 테스트한다.
- 공유 트레이스 T1~T4 로 정책별 **활용률 U = peak live payload / heap high-water** 와 처리량(ops/s)을 잰다. 실측에서 LIFO first fit 의 T3 활용률은 0.302 였고, 주소 순서 first fit 은 0.719 였다.

## 1. 문제

"malloc 을 직접 짜 보세요. free 한 블록은 어떻게 찾고, 이웃과는 어떻게 합치죠?" 펌웨어·시스템 면접에서 자주 나오는 질문이다. 화이트보드에서는 헤더 그림과 병합 4가지 경우를, 라이브 코딩에서는 포인터 산술과 경계 조건을 본다. 펌웨어에서는 이 지식이 바로 쓰인다. newlib 의 `_sbrk` 스텁, FreeRTOS `heap_4`(RTOS-M10), "몇 달 돌던 장비가 메모리가 남았는데 할당에 실패" 하는 단편화 버그가 모두 여기서 나온다.

OSTEP 17장(Free-Space Management)이 개념(분할·병합·헤더·free list·정책)을, CS:APP 3e §9.9 가 구현(묵시적 리스트 할당기, 명시적 리스트, malloc lab 트레이스)을 다룬다. 이 레슨은 그 둘을 C 코드와 측정으로 잇는다. 이 레슨은 OS-M05(segregated list, 단편화 계측), OS-M06(스레드 캐시)와 **같은 블록 형식과 같은 트레이스**를 쓰는 시리즈의 1편이다. 고정 크기 풀은 DS-M02, 슬랩은 DS-M03, 버디는 DS-M04, TLSF 는 DS-A06 에서 다룬다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
<defs><marker id="m4a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" fill="currentColor" font-weight="bold">힙 = mmap 64 MiB 안의 [lo, brk). 블록 = hdr 8 B + payload + ftr 8 B, 16 B 정렬</text>
<rect x="10" y="60" width="24" height="44" class="f-mut" stroke="currentColor"/>
<text x="12" y="122" fill="currentColor" font-size="12">pad</text>
<rect x="34" y="60" width="56" height="44" class="f-mut" stroke="currentColor"/>
<text x="38" y="86" fill="currentColor" font-size="12">16|1 16|1</text>
<text x="36" y="122" fill="currentColor" font-size="12">prologue</text>
<rect x="90" y="60" width="30" height="44" class="f-warn" stroke="currentColor"/>
<text x="93" y="86" fill="currentColor" font-size="12">128|1</text>
<rect x="120" y="60" width="130" height="44" class="f-acc" stroke="currentColor"/>
<text x="140" y="86" fill="currentColor">payload (사용자)</text>
<rect x="250" y="60" width="30" height="44" class="f-warn" stroke="currentColor"/>
<text x="253" y="86" fill="currentColor" font-size="12">128|1</text>
<text x="120" y="122" fill="currentColor" font-size="12">할당 블록: 요청 100 → 128 B</text>
<rect x="280" y="60" width="30" height="44" class="f-warn" stroke="currentColor"/>
<text x="283" y="86" fill="currentColor" font-size="12">96|0</text>
<rect x="310" y="60" width="40" height="44" class="f-ok" stroke="currentColor"/>
<text x="314" y="86" fill="currentColor" font-size="12">next</text>
<rect x="350" y="60" width="40" height="44" class="f-ok" stroke="currentColor"/>
<text x="354" y="86" fill="currentColor" font-size="12">prev</text>
<rect x="390" y="60" width="50" height="44" class="f-ok" stroke="currentColor"/>
<text x="400" y="86" fill="currentColor" font-size="12">(빈)</text>
<rect x="440" y="60" width="30" height="44" class="f-warn" stroke="currentColor"/>
<text x="443" y="86" fill="currentColor" font-size="12">96|0</text>
<text x="290" y="122" fill="currentColor" font-size="12">free 블록: payload 앞 16 B = 리스트 포인터</text>
<rect x="470" y="60" width="150" height="44" class="f-mut" stroke="currentColor"/>
<text x="500" y="86" fill="currentColor">… 다른 블록들 …</text>
<rect x="620" y="60" width="40" height="44" class="f-mut" stroke="currentColor"/>
<text x="626" y="86" fill="currentColor" font-size="12">0|1</text>
<text x="614" y="122" fill="currentColor" font-size="12">epilogue</text>
<line x1="662" y1="48" x2="662" y2="112" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="668" y="58" fill="currentColor" font-size="12">brk</text>
<rect x="20" y="170" width="90" height="34" class="f-mut" stroke="currentColor"/>
<text x="30" y="192" fill="currentColor" font-family="monospace">free_head</text>
<line x1="110" y1="187" x2="300" y2="110" stroke="currentColor" marker-end="url(#m4a)"/>
<path d="M330 104 C 400 170, 520 170, 560 106" fill="none" stroke="currentColor" marker-end="url(#m4a)"/>
<text x="400" y="178" fill="currentColor" font-size="12">next → 다음 free 블록 (주소 순서 아님, LIFO)</text>
<text x="20" y="232" fill="currentColor">암시적 리스트: hdr 의 size 로 다음 블록을 찾는다 → 할당 블록까지 모두 걷는다 O(전체 블록)</text>
<text x="20" y="254" fill="currentColor">명시적 리스트: free 블록끼리만 next/prev 로 잇는다 → O(free 블록). 대가: 최소 블록 32 B</text>
<text x="20" y="276" fill="currentColor">ftr(boundary tag) 덕분에 앞 블록 크기 = *(bp − 16) → 앞 이웃도 O(1) 로 찾아 병합한다</text>
</svg>
```

### Part A — 암시적 리스트 first fit (기준선)

- 주어진 것은 memlib(`mem_init`, `mem_sbrk`, `mem_heap_lo/hi`, `mem_heapsize`), 블록 도우미(`hdrp`, `ftrp`, `next_blkp`, `prev_blkp`, `set_block`, `adjust`), `mm_init`(패딩 + prologue + epilogue), `extend_heap`, `mm_malloc`, `mm_free` 이다.
- **TODO A1** `implicit_find_fit(asize)`: prologue 다음부터 epilogue(크기 0) 전까지 주소 순서로 걷는다. 처음 맞는 free 블록을 돌려주고, 본 블록마다 `g_st.scans++` 한다.
- **TODO A2** `place(bp, asize)`: 남는 부분이 `MIN_BLOCK`(32) 이상이면 쪼개서 뒤쪽을 새 free 블록으로 만들고, 아니면 통째로 준다.
- 테스트: 100 B 블록 4개 중 1번째·3번째를 free 하고 `malloc(50)` 을 부르면 1번째 자리(80 + 48 분할)가 나와야 하고 힙은 자라지 않아야 한다. `malloc(90)` 을 부르면 3번째 자리 128 B 전체가 나와야 한다(남는 16 < 32).

### Part B — 명시적 free list, 병합 4가지, fit 정책, 힙 검사기

- 리스트 연산 `fl_insert`(LIFO, 머리에 넣기)와 `fl_remove` 는 주어진다. 암시적 모드에서는 아무것도 하지 않으므로 Part A 코드가 그대로 돈다.
- **TODO B1** `coalesce(bp)`: 앞·뒤 이웃의 할당 비트로 4가지 경우를 나누고, free 이웃은 리스트에서 빼서 합친다. `g_st.coal[경우-1]++`.
- **TODO B2** `explicit_find_fit(asize)`: `FIT_FIRST`(머리부터 처음 맞는 것), `FIT_BEST`(가장 작은 것, 딱 맞으면 즉시 멈춤), `FIT_NEXT`(`rover` 부터 돌고 끝나면 머리로, 찾으면 `rover = 다음`).
- **TODO B3** `mm_check(verbose)`: 위반 개수를 돌려준다. 망가진 힙에서도 크래시나 무한 루프 없이 끝나야 한다.

### Part C — realloc, 트레이스로 활용률 측정, MCU 힙

- **TODO C1** `mm_realloc(ptr, size)`: `ptr == NULL` 이면 malloc, `size == 0` 이면 free 후 NULL(이 레슨의 선택). 축소는 제자리 + 분할로, 확장은 다음 free 블록 흡수 → 힙 끝 확장 → 이동 순서로 시도한다. 실패하면 NULL 을 돌려주고 원본은 그대로 둔다.
- 하네스의 `replay()` 는 공유 트레이스를 돌린다. alloc/realloc 때마다 id 로 만든 바이트 패턴을 채우고, realloc 뒤와 free 전에 검사하고, 50 연산마다 `mm_check` 를 부른다. 마지막에 U 를 계산한다.
- `mem_set_limit(16 KB)` 로 링커 스크립트의 `_heap_start.._heap_end` 같은 작은 고정 힙을 흉내 낸다. 할당이 정확히 15개에서 실패해야 하고, 실패한 realloc 은 원본을 지켜야 하며, 전부 free 하면 하나로 병합되어 15000 B 요청이 성공해야 한다.

**공유 트레이스** (OS-M05/M06 과 같은 생성기, `xorshift32`: `x ^= x<<13; x ^= x>>17; x ^= x<<5`):

| 트레이스 | seed | 만드는 법 | 연산 수(실측) |
|---|---|---|---|
| T1 small-uniform | 1 | 2000 스텝. live < 50 이면 alloc, 아니면 `rand%100 < 60` 이면 alloc 이고 그 외에는 임의 live 하나를 free. 크기 1..128. 끝에 전부 free | 2466 |
| T2 mixed | 2 | T1 과 같은 섞기. 크기는 70 % 1..256, 25 % 257..4096, 5 % 4097..65536 | 2438 |
| T3 realloc-grow | 3 | 버퍼 200개. 목표 1..32768, 첫 크기 1..512. 임의 버퍼를 +1..+512 씩 realloc 으로 키운다. 그때마다 25 % 확률로 작은 alloc(1..64), 20 % 확률로 작은 것 하나 free | 19198 |
| T4 binary-pattern | — | (64, 448) 500쌍 → 448 전부 free → 512 × 500 → 전부 free (CS:APP binary-bal 스타일) | 3000 |

## 2. Problem (English)

> Implement a general-purpose allocator inside a fixed 64 MiB region obtained with `mmap(MAP_PRIVATE|MAP_ANON)`. `mem_sbrk(intptr_t incr)` bumps a break inside that region and returns NULL past the limit. No calls to the system `malloc` from the allocator.
>
> Block format: 8-byte header and 8-byte footer holding `size | alloc` (bit 0), payloads 16-byte aligned, minimum block 32 bytes. Free blocks store `next`/`prev` pointers in the first 16 payload bytes.
>
> ```c
> int   mm_init(void);
> void *mm_malloc(size_t size);            // size 0 -> NULL
> void  mm_free(void *ptr);                // NULL -> no-op
> void *mm_realloc(void *ptr, size_t size); // NULL ptr -> malloc; size 0 -> free, return NULL
> int   mm_check(int verbose);             // number of invariant violations, never crashes
> ```
>
> 1. Baseline: implicit free list, first fit, split when the remainder is at least 32 bytes.
> 2. Explicit doubly linked free list with boundary-tag coalescing (all four cases), selectable first / next / best fit.
> 3. `realloc`: shrink in place (split off the tail), grow into a free successor, grow at the end of the heap by extending only the shortfall, otherwise move. On failure return NULL and leave the old block intact.
> 4. Replay four deterministic traces, verify payload integrity and `mm_check` every 50 ops, and report utilization `peak live payload / heap high-water` per policy. Throughput goes in a separate benchmark.
>
> Follow-ups: why does an explicit list need a 32-byte minimum block? Which fit policy wins on which trace, and why does LIFO ordering hurt? How would you catch a one-byte overflow inside your own heap? Would you ship this on a Cortex-M?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 여섯 곳(A1·A2·B1·B2·B3·C1)은 "틀렸지만 안전한" 값을 돌려준다. find_fit 은 항상 NULL 이라 매번 힙이 자라고, `place` 는 분할하지 않고, `coalesce` 는 병합하지 않고, `realloc` 은 NULL, `mm_check` 는 0 을 돌려준다. 그래서 힙은 망가지지 않고, 체크만 FAIL 이다. 실제 첫 실행 결과(0.1초, exit 1)의 일부:

```text
== Part A: implicit list, first fit ==
PASS mm_malloc(0) == NULL
PASS prev_blkp(b) == a && next_blkp(a) == b && rd(hdrp(a)) == rd(ftrp(a))
FAIL starter.c:417  e == a
FAIL starter.c:419  mem_heapsize() == hw
  [implicit first] T1 small-uniform   ops=2466   U=0.267 heap=107616 B scans/malloc=0.0
  [implicit first] T2 mixed           ops=2438   U=0.381 heap=2997568 B scans/malloc=0.0
  [implicit first] T4 binary-pattern  ops=3000   U=0.537 heap=536032 B scans/malloc=0.0
FAIL starter.c:431  ri[0].util > 0.65 && ri[1].util > 0.85
…
20/59 passed
```

재사용을 전혀 하지 않아도 T4 는 0.537 이다. 이 숫자는 §5 Part C 에서 다시 본다.

## 4. 힌트

<details><summary>힌트 1 — 블록 크기 계산과 정렬</summary>

payload 가 16 B 정렬이고 헤더가 8 B 이므로, 헤더는 항상 "16의 배수 − 8" 주소에 있다. 블록 크기 = `요청 + 16(hdr+ftr)` 을 16 배수로 올린 값이고, 최소 32 이다. `adjust(1) = 32`, `adjust(16) = 32`, `adjust(17) = 48`, `adjust(100) = 128` 이 된다. 블록 크기가 16의 배수라서 다음 payload 도 저절로 정렬된다. `mm_init` 이 처음에 8 B 패딩을 넣는 이유도 이것이다.
</details>

<details><summary>힌트 2 — prologue/epilogue 가 경계 검사를 없앤다</summary>

첫 블록의 "앞 블록"은 prologue(16|1)이고, 마지막 블록의 "다음 블록"은 epilogue(0|1)이다. 둘 다 **할당됨** 으로 표시되어 있으므로, `coalesce` 는 힙 끝인지 따로 묻지 않아도 된다. 암시적 걷기는 `blk_size(bp) > 0` 일 때까지만 돈다.
</details>

<details><summary>힌트 3 — coalesce 순서: 리스트에서 먼저 빼고, 크기는 나중에</summary>

이웃을 `fl_remove` 하기 **전에** 이웃 블록의 크기를 바꾸면 안 된다. 리스트 포인터는 payload 안에 있어서 헤더를 고쳐도 상관없어 보이지만, 앞 블록을 흡수할 때 `bp = prev` 로 옮긴 다음 `set_block(bp, 합친 크기, 0)` 을 **한 번만** 쓰는 것이 가장 안전하다. 푸터는 합친 블록의 끝(뒤 이웃의 옛 푸터 자리)에 쓰인다.
</details>

<details><summary>힌트 4 — next fit 의 rover 는 언제 무효가 되나</summary>

rover 가 가리키는 블록이 할당되거나 이웃에 흡수되면 리스트에서 빠진다. `fl_remove` 가 `rover == bp` 일 때 rover 를 다음 노드로 옮기므로 B2 는 신경 쓰지 않아도 된다. B2 에서 할 일은 찾은 뒤 `rover = fnext(bp)` 로 두는 것뿐이다. 두 번째 루프는 `bp != start` 에서 멈춰야 같은 노드를 두 번 보지 않는다.
</details>

<details><summary>힌트 5 — mm_check 가 크래시하지 않으려면</summary>

포인터를 따라가기 **전에** 범위를 먼저 검사한다. 힙 걷기에서는 크기가 16 배수이고 32 이상이며 끝을 넘지 않는지 보고, 아니면 멈춘다. 리스트 걷기에서는 노드 주소가 `[첫 블록, brk)` 안에 있고 16 정렬인지 보고, 아니면 멈춘다. 순환은 "힙 블록 수보다 많이 돌았다"로 잡는다. 주소 비교는 `uintptr_t` 로 한다. 힙 밖 포인터(예: 스택 주소)와 `<` 비교하는 것은 C 에서 정의되지 않는다.
</details>

<details><summary>힌트 6 — realloc 의 우선순위</summary>

비용이 싼 것부터 시도한다. ① 축소(복사 없음) ② 다음 블록 흡수(복사 없음) ③ 힙 끝이면 `mem_sbrk(부족분)`(복사 없음, 힙이 가장 적게 자람) ④ 이동(`memcpy`). ④에서 새 블록을 **먼저** 받은 다음 옛 블록을 free 해야 실패했을 때 원본이 살아 있다.
</details>

## 5. 풀이

### Part A — 블록 형식, 암시적 걷기, 분할

**`adjust` 표**. 할당 블록은 hdr 와 ftr 를 합쳐 16 B 의 오버헤드가 있다. free 블록은 포인터 16 B 를 더 담아야 하므로 최소 32 B 이다.

| 요청 | `n + 16` | 16 배수 | 블록 | payload 용량 | 낭비(내부 단편화) |
|---|---|---|---|---|---|
| 1 | 17 | 32 | 32 | 16 | 15 |
| 16 | 32 | 32 | 32 | 16 | 0 |
| 17 | 33 | 48 | 48 | 32 | 15 |
| 100 | 116 | 128 | 128 | 112 | 12 |
| 5000 | 5016 | 5024 | 5024 | 5008 | 8 |

**암시적 first fit** 은 `bp = next_blkp(heap_listp)` 에서 시작해서 `blk_size(bp) > 0` 인 동안 걷는다. 할당된 블록도 지나가므로 비용이 블록 **전체 수**에 비례한다. 실측 `scans/malloc` 이 이를 보여 준다. T2 에서 암시적 리스트는 147.2 블록, 명시적 LIFO first fit 은 3.9 블록을 봤다. T4 처럼 할당 블록이 수백 개 쌓인 뒤에는 749.5 블록이었다.

**분할** 규칙은 `csize − asize ≥ 32` 일 때 앞쪽을 할당하고 뒤쪽을 새 free 블록으로 만드는 것이다. 남는 부분이 32 보다 작으면 블록이 될 수 없으므로(포인터 두 개 + hdr/ftr) 통째로 준다. 그 16 B 는 내부 단편화가 된다. 테스트 `f == c && blk_size(f) == 128` 이 이 경우다(asize 112, 남는 16).

### Part B — 병합 4가지, 리스트, 정책, 검사기

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">free(bp) 의 병합 4가지 (테스트의 실제 순서: x1 → x4 → x3 → x5 → x2, 블록 80 B)</text>
<text x="10" y="58" fill="currentColor">경우 1</text>
<rect x="80" y="40" width="90" height="28" class="f-acc" stroke="currentColor"/><text x="100" y="59" fill="currentColor">할당</text>
<rect x="170" y="40" width="90" height="28" class="f-ok" stroke="currentColor"/><text x="190" y="59" fill="currentColor">bp</text>
<rect x="260" y="40" width="90" height="28" class="f-acc" stroke="currentColor"/><text x="280" y="59" fill="currentColor">할당</text>
<text x="370" y="59" fill="currentColor">→ 합칠 것 없음, bp 만 삽입 (x1: 80)</text>
<text x="10" y="118" fill="currentColor">경우 2</text>
<rect x="80" y="100" width="90" height="28" class="f-acc" stroke="currentColor"/><text x="100" y="119" fill="currentColor">할당</text>
<rect x="170" y="100" width="90" height="28" class="f-ok" stroke="currentColor"/><text x="190" y="119" fill="currentColor">bp</text>
<rect x="260" y="100" width="90" height="28" class="f-ok" stroke="currentColor" stroke-dasharray="4 3"/><text x="280" y="119" fill="currentColor">free</text>
<text x="370" y="119" fill="currentColor">→ 뒤를 리스트에서 빼고 bp 크기 += (x3: 160)</text>
<text x="10" y="178" fill="currentColor">경우 3</text>
<rect x="80" y="160" width="90" height="28" class="f-ok" stroke="currentColor" stroke-dasharray="4 3"/><text x="100" y="179" fill="currentColor">free</text>
<rect x="170" y="160" width="90" height="28" class="f-ok" stroke="currentColor"/><text x="190" y="179" fill="currentColor">bp</text>
<rect x="260" y="160" width="90" height="28" class="f-acc" stroke="currentColor"/><text x="280" y="179" fill="currentColor">할당</text>
<text x="370" y="179" fill="currentColor">→ 앞을 빼고 bp = 앞, 크기 += (x3: 240)</text>
<text x="10" y="238" fill="currentColor">경우 4</text>
<rect x="80" y="220" width="90" height="28" class="f-ok" stroke="currentColor" stroke-dasharray="4 3"/><text x="100" y="239" fill="currentColor">free</text>
<rect x="170" y="220" width="90" height="28" class="f-ok" stroke="currentColor"/><text x="190" y="239" fill="currentColor">bp</text>
<rect x="260" y="220" width="90" height="28" class="f-ok" stroke="currentColor" stroke-dasharray="4 3"/><text x="280" y="239" fill="currentColor">free</text>
<text x="370" y="239" fill="currentColor">→ 둘 다 빼고 bp = 앞, 셋을 합침 (x1: 400)</text>
<text x="10" y="284" fill="currentColor">앞 블록 찾기 = bp − (bp−16 의 ftr 크기): 푸터가 없으면 앞 이웃을 O(1)에 못 찾는다 (boundary tag, Knuth)</text>
</svg>
```

```c
static char *coalesce(char *bp) {            /* solution.c 발췌 */
    char *prev = prev_blkp(bp), *next = next_blkp(bp);
    int pa = blk_alloc(prev), na = blk_alloc(next);
    size_t size = blk_size(bp);
    if (pa && na) { g_st.coal[0]++; return bp; }
    if (pa && !na)       { fl_remove(next); size += blk_size(next); g_st.coal[1]++; }
    else if (!pa && na)  { fl_remove(prev); size += blk_size(prev); bp = prev; g_st.coal[2]++; }
    else { fl_remove(prev); fl_remove(next); size += blk_size(prev) + blk_size(next); bp = prev; g_st.coal[3]++; }
    set_block(bp, size, 0);
    return bp;                                /* 삽입은 호출자(mm_free, extend_heap)가 */
}
```

**즉시 병합(immediate coalescing)** 은 free 할 때마다 바로 합친다. 단점은 같은 크기를 free/malloc 반복할 때 합쳤다 쪼갰다를 되풀이하는 것이다. 대안은 지연 병합(deferred: 할당이 실패할 때 한꺼번에 병합)이다. 이 레슨은 즉시 병합으로 "인접 free 블록 없음"을 불변식으로 삼는다. 그래서 `mm_check` 가 그것을 검사할 수 있다.

**fit 정책 테스트** (LIFO 리스트, 가드 블록 사이에 f1 = 224, f2 = 128, f3 = 320 을 f3 → f2 → f1 순서로 free 해서 리스트 = f1 → f2 → f3 를 만들고, 112 B 요청을 두 번):

| 정책 | 1번째 | 2번째 | 이유 |
|---|---|---|---|
| first | f1 (분할, 남은 112 가 머리로) | f1 + 112 | LIFO 라 방금 생긴 나머지가 머리에 있다 |
| next | f1 (rover = f2) | f2 | rover 부터 찾는다 |
| best | f2 (128, 남는 16 → 통째로) | f1 | 남은 것 중 가장 작은 224 |

**mm_check 불변식**과 일부러 망가뜨린 5가지(실제 `mm_check(1)` 출력):

| 검사 | 주입한 고장 | 실제 보고 |
|---|---|---|
| hdr == ftr | ① p3 푸터를 `64\|0` 으로 | `block 0x1057201a0 header != footer` |
| 리스트 노드는 free 표시 + 개수 일치 | ② p3 를 할당으로 표시 (리스트에는 남음) | `list node … is marked allocated`, `free list has 2 nodes, heap has 1 free blocks` |
| 인접 free 없음 + 개수 일치 | ③ p2 를 free 표시만 (병합·삽입 안 함) | `block … and previous are both free (not coalesced)` ×2, `2 nodes, heap has 3` |
| 노드가 힙 안 | ④ p3→next = 스택 주소 | `list node 0x16f1fe0d0 outside heap` (따라가지 않고 멈춤) |
| 순환 없음 | ⑤ p1 ⇄ p3 | `prev link broken` ×3, `free list longer than heap — cycle?` (멈춤) |

그 밖에 prologue(16|1, hdr == ftr), payload 16 정렬, 크기 16 배수 ≥ 32, 블록이 brk 를 넘지 않는지, epilogue 가 정확히 brk 에 있는지도 본다. 마지막 검사 덕분에 `mem_sbrk` 를 하고 epilogue 를 다시 쓰지 않는 버그가 바로 잡힌다.

### Part C — realloc 과 트레이스 결과

**realloc 판단 순서** (테스트에서 각 갈래가 한 번씩 실행된다):

| 상황 | 동작 | 테스트에서의 실측 |
|---|---|---|
| `asize ≤ csize`, 남는 부분 ≥ 32 | 앞을 그대로 두고 뒤를 잘라 free (뒤 이웃과 병합) | 1024 → 128 + free 896, 같은 포인터 |
| `asize ≤ csize`, 남는 부분 < 32 | 아무것도 안 함 | 128 블록에 90 B → 그대로 |
| 다음 블록 free, 합치면 충분 | 흡수 → `place` 로 다시 분할 | 128 + 896 → 528 + free 496 |
| 다음 = epilogue (또는 다음이 free 이고 그다음이 epilogue) | 부족분만 `mem_sbrk` | 224 → 5024, 힙 +4800 B 정확히 |
| 그 밖 | `mm_malloc` → `memcpy` → `mm_free` | 새 주소, 앞 64 B 보존, 옛 블록 free |

**활용률 표** (solution.c 가 찍는 실측, 결정적이라 실행마다 같다):

| 트레이스 | 암시적 first (=주소순) | 명시적 first (LIFO) | 명시적 next | 명시적 best |
|---|---|---|---|---|
| T1 small-uniform | 0.685 | 0.665 | 0.665 | **0.698** |
| T2 mixed | **0.897** | 0.732 | 0.783 | 0.887 |
| T3 realloc-grow | **0.719** | 0.302 | 0.362 | 0.572 |
| T4 binary-pattern | 0.537 | 0.537 | 0.537 | 0.537 |

```svg
<svg viewBox="0 0 740 310" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">활용률 U (실측) — 높을수록 힙을 덜 낭비</text>
<rect x="10" y="30" width="14" height="12" class="f-acc" stroke="currentColor"/><text x="28" y="41" fill="currentColor">암시적 first(주소순)</text>
<rect x="170" y="30" width="14" height="12" class="f-warn" stroke="currentColor"/><text x="188" y="41" fill="currentColor">명시적 first(LIFO)</text>
<rect x="320" y="30" width="14" height="12" class="f-mut" stroke="currentColor"/><text x="338" y="41" fill="currentColor">명시적 next</text>
<rect x="440" y="30" width="14" height="12" class="f-ok" stroke="currentColor"/><text x="458" y="41" fill="currentColor">명시적 best</text>
<line x1="60" y1="260" x2="720" y2="260" stroke="currentColor"/>
<line x1="60" y1="60" x2="720" y2="60" stroke="currentColor" stroke-dasharray="3 4"/>
<text x="22" y="64" fill="currentColor">1.0</text>
<line x1="60" y1="160" x2="720" y2="160" stroke="currentColor" stroke-dasharray="3 4"/>
<text x="22" y="164" fill="currentColor">0.5</text>
<rect x="70" y="123" width="30" height="137" class="f-acc" stroke="currentColor"/><text x="70" y="118" fill="currentColor">.69</text>
<rect x="104" y="127" width="30" height="133" class="f-warn" stroke="currentColor"/>
<rect x="138" y="127" width="30" height="133" class="f-mut" stroke="currentColor"/>
<rect x="172" y="120" width="30" height="140" class="f-ok" stroke="currentColor"/><text x="172" y="115" fill="currentColor">.70</text>
<text x="100" y="278" fill="currentColor">T1</text>
<rect x="235" y="81" width="30" height="179" class="f-acc" stroke="currentColor"/><text x="235" y="76" fill="currentColor">.90</text>
<rect x="269" y="114" width="30" height="146" class="f-warn" stroke="currentColor"/><text x="269" y="109" fill="currentColor">.73</text>
<rect x="303" y="103" width="30" height="157" class="f-mut" stroke="currentColor"/><text x="303" y="98" fill="currentColor">.78</text>
<rect x="337" y="83" width="30" height="177" class="f-ok" stroke="currentColor"/><text x="337" y="78" fill="currentColor">.89</text>
<text x="265" y="278" fill="currentColor">T2</text>
<rect x="400" y="116" width="30" height="144" class="f-acc" stroke="currentColor"/><text x="400" y="111" fill="currentColor">.72</text>
<rect x="434" y="200" width="30" height="60" class="f-warn" stroke="currentColor"/><text x="434" y="195" fill="currentColor">.30</text>
<rect x="468" y="188" width="30" height="72" class="f-mut" stroke="currentColor"/><text x="468" y="183" fill="currentColor">.36</text>
<rect x="502" y="146" width="30" height="114" class="f-ok" stroke="currentColor"/><text x="502" y="141" fill="currentColor">.57</text>
<text x="430" y="278" fill="currentColor">T3</text>
<rect x="565" y="153" width="30" height="107" class="f-acc" stroke="currentColor"/><text x="565" y="148" fill="currentColor">.54</text>
<rect x="599" y="153" width="30" height="107" class="f-warn" stroke="currentColor"/>
<rect x="633" y="153" width="30" height="107" class="f-mut" stroke="currentColor"/>
<rect x="667" y="153" width="30" height="107" class="f-ok" stroke="currentColor"/>
<text x="595" y="278" fill="currentColor">T4 (모두 0.537)</text>
<text x="10" y="300" fill="currentColor">LIFO 순서가 주소순보다 나쁘다: T2 −0.17, T3 −0.42. T4 는 어떤 정책도 못 고친다 (배치 문제가 아니라 크기 섞임 문제)</text>
</svg>
```

표를 읽는 법:

1. **"암시적 first" 열은 사실상 주소 순서 first fit 이다.** 블록을 주소 순서로 걷기 때문이다. 연습 3에서 명시적 리스트를 주소 순서로 유지하면 네 트레이스 모두 high-water 가 **바이트 단위로 같다**는 것을 CHECK 로 확인했다(같은 블록을 고르니 당연하다). 그러니 이 표의 진짜 비교는 "주소순 vs LIFO" 이다.
2. **LIFO 가 T3 에서 크게 진다(0.302 vs 0.719).** 분석용으로 따로 돌려 본 카운터: LIFO 는 `extend_heap` 1192회, 이동 4792회. 주소순은 794회, 4141회. 해석(추정): realloc 이동으로 방금 free 된 큰 블록이 LIFO 머리에 오고, 그다음 작은 할당(1..64)들이 그 큰 블록부터 잘라 먹는다. 그래서 큰 구멍이 남지 않고, 자라는 버퍼는 힙 끝을 늘리게 된다. 주소순은 작은 할당이 낮은 주소의 작은 구멍을 먼저 메워서 큰 구멍이 오래 남는다. Wilson 외(1995) 서베이도 주소순 first fit 이 실제 프로그램에서 best fit 에 가깝고 LIFO 가 더 나쁘다고 보고한다(기억 기반 인용, 세부 미확인). 이 표의 숫자는 그 결론과 같은 방향이다.
3. **best fit 은 LIFO 리스트에서도 주소순에 가깝다**(T2 0.887). 대신 리스트 전체를 보므로 T2 에서 scans/malloc 이 23.0 이다(first 는 3.9). 처리량 표에서 그 대가를 본다.
4. **T4 = 0.537 은 정책과 무관하다.** 재사용을 전혀 안 하는 starter 도 같은 값이다. 448 B 구멍(블록 464) 500개가 64 B 블록 사이에 끼어 있어서 512 B(블록 528)가 하나도 못 들어간다. 계산하면 힙 = 32 + 500·(80 + 464) + 500·528 = 536032, peak live = 500·64 + 500·512 = 288000 이고, 288000 / 536032 = 0.537 이다. 고치는 방법은 배치 정책이 아니라 **크기별로 다른 영역에 두는 것**(segregated storage, 슬랩 — OS-M05, DS-M03)이나 이동(compaction)이다. C 에서는 포인터를 고칠 수 없으므로 compaction 은 불가능하다.

**작은 고정 힙(MCU 흉내)**. `mem_set_limit(16384)` 에서 1000 B 할당(블록 1024)은 32 + 15·1024 = 15392 ≤ 16384 라서 15개까지 성공하고, 16번째는 16416 > 16384 라 NULL 이다. 실측 `got == 15` 였다. Cortex-M 에서 newlib 를 쓰면 `malloc` 이 `_sbrk(incr)` 를 부르고, 보통 `syscalls.c`/`sysmem.c` 의 `_sbrk` 가 링커 심볼 `_end` 부터 스택 근처까지 break 를 올린다(STM32CubeIDE 가 생성하는 sysmem.c 가 이런 모양으로 알려져 있다 — 세부는 버전마다 다름, 미확인). 이 레슨의 `mem_sbrk` 가 바로 그 자리다. 실패를 NULL 로 깔끔히 돌려주고, 할당기가 NULL 을 끝까지 전달하는지가 펌웨어에서 가장 중요한 성질이다.

### 벤치마크 — demo_bench.c

<!-- code: demo_bench.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o bench && ./bench    # solution.c 를 include, 약 3 초
```

기계: Apple M6, Apple clang version 21.0.0 (clang-2100.0.123.102). **`uptime` load average 9.72**(다른 세션이 함께 돌던 공유 맥). 연산 루프만 쟀다(패턴 채우기 · mm_check 없음, 1바이트만 만짐). 한 측정 = 합계 20 ms 이상 반복 재생, 7번의 중앙값. 바로 이어서 한 번 더 돌린 결과도 순위가 같았고 값은 5~25 % 안에서 흔들렸다. **비율만 믿을 것.**

| 트레이스 | 연산 | 암시적 first | 명시적 first | 명시적 next | 명시적 best | libc malloc (macOS) |
|---|---|---|---|---|---|---|
| T1 small-uniform | 2466 | 6.9M | 160.3M | 141.9M | 204.2M | 192.5M |
| T2 mixed | 2438 | 8.0M | 192.6M | 190.3M | 98.0M | 118.9M |
| T3 realloc-grow | 19198 | 3.1M | 6.7M | 6.5M | 7.4M | 22.7M |
| T4 binary-pattern | 3000 | 1.2M | 12.5M | 13.2M | 13.1M | 106.9M |

(단위: ops/s)

- **암시적 → 명시적은 20~30배 빨라진다**(T1 6.9M → 160.3M). scans/malloc 166.6 → 10.1 과 같은 방향이다.
- **best fit 의 비용은 리스트 길이에 따라 다르다.** T2 에서는 first 의 절반(98.0M vs 192.6M)이다. T1 에서는 오히려 가장 빨랐다(204.2M). T1 은 크기가 작고 비슷해서 딱 맞는 블록이 자주 나와 일찍 멈추고, 분할이 적어 리스트도 짧다(scans/malloc best 5.0 vs first 10.1, 실측).
- **T4 에서는 모두 느리다(12~13M).** 512 B 요청마다 쓸 수 없는 464 B 구멍 500개를 다 보고 나서야 힙을 늘린다(scans/malloc 166.7). libc 는 107M 이다. 크기 클래스로 나눈 할당기는 이런 구멍을 아예 보지 않는다. 이것이 OS-M05 의 동기다.
- **T3 은 memcpy 가 지배한다.** 이동 4792회 × 평균 수 KB 를 복사한다. libc 가 3배 빠른 이유는 확인하지 않았다(큰 블록의 제자리 확장이나 페이지 단위 처리 등이 후보 — 추정).
- libc 와 비교한 숫자는 공정하지 않다. 우리 쪽은 스레드 안전성, 큰 블록 mmap, 반환(trim)이 없다. "같은 트레이스에서 무엇이 병목인가"를 보는 용도로만 쓴다.

### 데모 — ASan 은 내 힙 안의 오버플로를 못 본다 (demo_asan.c)

<!-- code: demo_asan.c -->

```bash
cc -std=c11 -g -O1 -fsanitize=address -fno-omit-frame-pointer demo_asan.c -o demo_asan
./demo_asan; ./demo_asan poison
```

실제 출력(일부):

```text
$ ./demo_asan
a=0x108088020 block=48 B (payload capacity 32), b=0x108088050
wrote 40 bytes into a 32-byte payload — ASan said nothing
  mm_check: block 0x108088020 header != footer
mm_check = 1
$ ./demo_asan poison            # exit 134 (abort)
==72828==ERROR: AddressSanitizer: use-after-poison on address 0x000107f98040 …
WRITE of size 40 at 0x000107f98040 thread T0
    #1 0x000100838ab8 in main demo_asan.c:37
=>0x000107f98000: 00 00 00 00 00 00 00 00[f7]f7 00 00 00 00 00 00
```

ASan 은 libc `malloc` 을 가로채서 블록 양옆에 redzone 을 둔다. 하지만 우리 힙은 `mmap` 한 덩어리라서 ASan 에게는 전부 "정상 메모리"이다. 8 B 넘침(a 의 푸터를 덮음)을 ASan 은 놓쳤고, `mm_check` 의 hdr == ftr 검사가 잡았다. 할당기가 헤더·푸터를 `ASAN_POISON_MEMORY_REGION` 으로 막아 두면(`f7` = 사용자가 poison 한 바이트) 넘치는 그 순간 멈춘다. 실무 할당기(예: 게임 엔진·임베디드 풀)가 ASan 빌드에서 이 인터페이스를 쓰는 이유다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 59/59 passed, 0.6 초
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_san && ./so_san   # 59/59, 보고 없음
```

5번 연속 실행해서 모두 exit 0 이었고, 다른 디렉터리(`/tmp`)에서 실행해도 59/59 였다(데이터 파일이 없다). 시작 코드와 다른 곳은 TODO 여섯 함수의 본문뿐이다. 스레드를 쓰지 않으므로 TSan 은 해당이 없다. 마지막 표(실측):

```text
== utilization summary (U = peak live payload / heap high-water) ==
  trace               impl-1st exp-first  exp-next  exp-best
  T1 small-uniform       0.685     0.665     0.665     0.698
  T2 mixed               0.897     0.732     0.783     0.887
  T3 realloc-grow        0.719     0.302     0.362     0.572
  T4 binary-pattern      0.537     0.537     0.537     0.537
59/59 passed
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 블록 크기를 `n + 8` 로 계산 | 푸터 자리가 없다. 또는 16 정렬이 깨진다 | `(n + 16 + 15) & ~15`, 최소 32 |
| 분할 기준을 `csize > asize` 로 | 16 B 짜리 "블록"이 생긴다. 포인터 두 개가 다음 블록 헤더를 덮는다 | `csize - asize >= MIN_BLOCK` |
| coalesce 에서 이웃을 리스트에서 안 빼고 합침 | 리스트에 이미 흡수된 블록이 남는다. 나중에 그 주소를 또 할당 → 이중 할당 | 합치기 전에 `fl_remove(next/prev)` |
| 앞 블록 흡수 후 `bp` 를 안 옮김 | 헤더를 bp 에 쓰게 되어 앞 블록 헤더가 옛 크기로 남는다 | `bp = prev; set_block(bp, size, 0)` |
| next fit rover 를 free 된 블록에 둔 채로 | 그 블록이 이웃에 흡수되면 rover 가 블록 **중간**을 가리킨다 | `fl_remove` 에서 `if (rover == bp) rover = next` |
| `p = realloc(p, n)` | 실패하면 NULL 이 p 를 덮어 원본이 누수된다 | `q = realloc(p, n); if (!q) {…} else p = q;` |
| realloc 이동 시 `old 크기` 만큼 복사 | 축소 이동이면 새 블록을 넘친다 | `min(size, 옛 payload 용량)` |
| realloc 에서 옛 블록을 먼저 free | 새 malloc 이 그 자리를 받으면 `memcpy` 가 겹친다. 실패하면 원본도 없다 | 새 블록 먼저 → 복사 → 옛 블록 free |
| `extend_heap` 후 epilogue 를 안 씀 | 다음 걷기가 쓰레기 크기를 읽고 힙 밖으로 나간다 | `wr(hdrp(next_blkp(bp)), pack(0, 1))` |
| 힙 검사기가 포인터를 먼저 따라감 | 망가진 리스트에서 검사기가 크래시하거나 무한 루프에 빠진다 | 범위·정렬 먼저 검사, 블록 수로 순환 상한 |
| 헤더를 `*(size_t *)p` 와 `*(char **)p` 로 섞어 접근 | 같은 바이트를 다른 타입으로 읽으면 엄밀히는 effective type 위반이다 | `memcpy` 로 읽고 쓴다(-O2 에서 load/store 하나로 바뀐다) |

## 8. 면접 Q&A

<details><summary>Q1. 암시적 리스트와 명시적 리스트의 차이는? 명시적이 왜 최소 블록을 키우나요?</summary>

암시적 리스트는 헤더의 크기만으로 다음 블록을 찾는다. 별도 포인터가 없어서 최소 블록이 작다(hdr + ftr + 정렬). 하지만 할당 블록까지 다 걸으므로 탐색이 O(전체 블록)이다. 명시적 리스트는 free 블록의 payload 안에 next/prev 를 넣어 free 블록끼리만 잇는다. 탐색이 O(free 블록)이고, 삭제가 O(1)이다(이중 연결이라서). 대가로 free 블록이 포인터 16 B 를 담아야 해서 최소 블록이 32 B 가 된다. 실측: T2 에서 scans/malloc 147.2 → 3.9, 처리량 8.0M → 192.6M ops/s. 꼬리질문: "포인터를 32비트 오프셋으로 줄이면?" → 힙이 4 GB 이하면 `base + offset` 으로 8 B 를 아낄 수 있다. 그러면 최소 블록이 24 B 가 되지만 16 정렬 때문에 결국 32 가 된다. MCU 처럼 8 B 정렬이면 이득이 있다.
</details>

<details><summary>Q2. 푸터(boundary tag)는 왜 필요하고, 없앨 수 있나요?</summary>

free 할 때 **앞** 이웃이 free 인지 알아야 합칠 수 있다. 앞 블록의 푸터가 내 헤더 바로 앞에 있으므로 `bp − 16` 에서 크기를 읽어 O(1)에 찾는다(Knuth 의 boundary tag). 없애는 방법: 할당 블록에서는 푸터를 빼고, 내 헤더의 bit1 에 "앞 블록이 할당됨(prev_alloc)"을 기록한다. 앞이 free 일 때만 그 푸터가 필요한데, free 블록은 푸터를 계속 가진다. 할당 블록당 8 B 를 아끼므로 작은 블록이 많을 때 활용률이 오른다. 대가는 블록 상태가 바뀔 때마다 **다음** 블록 헤더의 bit1 도 고쳐야 한다는 것이다. 이 레슨은 쓰지 않았다(SHARED 규칙상 선택 사항). 꼬리질문: "그럼 최소 블록은?" → free 블록은 여전히 hdr + next + prev + ftr = 32 B 다.
</details>

<details><summary>Q3. first / next / best fit 중 무엇을 쓰겠어요? 리스트 순서는요?</summary>

정책만큼 **리스트 순서**가 중요하다는 것이 실측 결론이다. 같은 first fit 이라도 LIFO 는 T3 U 0.302, 주소순은 0.719 였다. 주소순 first fit 은 best fit 에 가까운 활용률(T2 0.897 vs best 0.887)을 내고 탐색이 짧다. 하지만 free 할 때 삽입이 O(리스트 길이)이다(트리나 segregated list 로 줄인다). next fit 은 앞쪽에 작은 조각이 쌓이는 것을 피하려는 생각이지만 이 트레이스에서는 LIFO first 보다 조금 나았을 뿐(T2 0.783)이다. best fit 은 리스트 전체를 보므로 T2 처리량이 절반이었다. 실무에서는 크기 클래스로 나눈 리스트(segregated, OS-M05)에서 클래스 안 first fit 을 쓴다. 이것이 대략 best fit 이면서 빠르다(glibc 는 bin, TLSF 는 2단계 비트맵 — DS-A06). 꼬리질문: "worst fit 은?" → 큰 구멍을 쪼개서 중간 크기를 남기려는 생각이다. OSTEP 17장도 실험적으로 나쁘다고 정리한다. 연습 4의 malloc.py 예에서도 25 B 요청이 worst/first 에서는 실패하고 best 에서만 성공했다.
</details>

<details><summary>Q4. 내부 단편화와 외부 단편화를 설명하고, T4 가 왜 0.537 인지 말해 보세요.</summary>

내부 단편화는 블록 **안**의 낭비다. hdr/ftr, 16 정렬 올림, 분할하지 못한 나머지(< 32 B)가 해당한다. 외부 단편화는 free 바이트는 충분한데 **연속**이 아니어서 요청을 못 받는 것이다. T4 는 순수한 외부 단편화다. free 된 464 B 블록 500개(합계 232000 B)가 64 B 할당 블록 사이에 끼어 있어서 528 B 블록이 하나도 안 들어간다. 그래서 힙을 264000 B 더 늘린다. 실측 U = 288000 / 536032 = 0.537 이고, 정책 4개와 재사용 0인 starter 모두 같은 값이었다. 고치려면 크기가 다른 객체를 다른 영역에 둬야 한다(segregated storage, 슬랩). 정량 계측(1 − 최대 free / 전체 free 같은 지표)은 OS-M05 에서 한다. 꼬리질문: "compaction 은요?" → C 는 포인터를 찾아 고칠 수 없어서 불가능하다. GC 언어나 핸들(간접 포인터) 기반 할당기만 할 수 있다.
</details>

<details><summary>Q5. realloc 의 정확한 약속은? realloc(p, 0) 은 무엇을 돌려주나요?</summary>

실패하면 NULL 을 돌려주고 **원본 블록은 그대로 유효**하다(C11 7.22.3.5). 그래서 `p = realloc(p, n)` 은 실패 시 누수다. macOS man 페이지도 "For realloc(), the input pointer is still valid if reallocation failed" 라고 쓰고, 실패 시 원본을 free 해 주는 `reallocf()` 를 따로 둔다(이 맥의 `man 3 malloc` 에서 확인). `realloc(p, 0)` 은 구현마다 다르다. macOS man 페이지는 "a new, minimum sized object is allocated and the original object is freed" 라고 쓴다. 이 맥에서 실측하니 non-NULL, `malloc_size` 16 이었다. `malloc(0)` 도 non-NULL, 16 B 였다. 이 레슨은 free 후 NULL 을 선택했다(SHARED 규칙). C17 까지는 구현 정의이고, C23 은 크기 0 realloc 을 미정의 동작으로 바꿨다고 알려져 있다(기억 기반 — 이식 코드는 0을 넘기지 말 것). 꼬리질문: "제자리 확장은 언제 되나요?" → 다음 블록이 free 이고 충분할 때, 또는 힙 끝일 때다. T3 실측에서 LIFO first fit 은 7511번 제자리로, 4792번 이동으로 처리했다.
</details>

<details><summary>Q6. 힙 손상 버그는 어떻게 잡나요? 직접 만든 할당기라면요?</summary>

① 힙 검사기(이 레슨의 `mm_check`)를 디버그 빌드에서 매 N 연산마다 돌린다. 손상 **직후**를 잡아야 원인 코드가 가깝다. libmalloc 에도 같은 기능이 있다: `MallocCheckHeapStart`/`MallocCheckHeapEach` 환경 변수(이 맥의 `man 3 malloc` 에 있음). ② 카나리·채움 패턴: 할당 시 `0xAA`, free 시 `0x55` 로 채운다(`MallocScribble` 이 같은 생각이다). ③ 가드 페이지: 큰 블록 뒤에 접근 불가 페이지를 둔다(`MallocGuardEdges`, Linux 의 Electric Fence 류). ④ ASan: libc malloc 은 자동으로 잡지만 **직접 만든 힙은 못 본다**(demo_asan: 8 B 넘침에 침묵). 할당기가 hdr/ftr 를 `ASAN_POISON_MEMORY_REGION` 으로 막으면 그 순간 멈춘다(실측 use-after-poison). 꼬리질문: "필드에서 터지는 펌웨어라면?" → 헤더에 magic 이나 체크섬을 두고 free 할 때 검사해서 실패하면 호출자 주소를 크래시 로그(백업 RAM)에 남긴다. FreeRTOS heap_4 의 `vPortFree` 도 헤더의 할당 비트와 `next == NULL` 을 `configASSERT` 로 확인한다(RTOS-M10 에서 heap_4.c:369–370 으로 확인한 내용).
</details>

<details><summary>Q7. 펌웨어에서 malloc 을 쓰나요? 쓴다면 어떻게?</summary>

안전 관련 펌웨어는 보통 **초기화 이후 동적 할당 금지**다. MISRA C:2012 Rule 21.3 이 `<stdlib.h>` 의 할당 함수 사용을 금지한다(규칙 번호는 기억 기반). 이유는 세 가지다. 실행 시간이 리스트 길이에 따라 달라 비결정적이고(이 레슨의 T4: scans/malloc 166.7), 몇 달 돌면 단편화로 실패할 수 있고, 실패 처리 경로가 테스트되지 않는다. 대안은 정적 할당, 고정 크기 풀(DS-M02, O(1), 단편화 없음), RTOS 힙을 초기화 때만 쓰기(FreeRTOS heap_1, RTOS-M10), 꼭 필요하면 시간 상한이 있는 TLSF(DS-A06)다. 동적 할당을 쓴다면 최소 여유 바이트(min-ever free)와 최대 연속 free 블록을 감시하고 실패를 로그로 남긴다(RTOS-M10 의 `vPortGetHeapStats`). 꼬리질문: "newlib malloc 은 어디서 메모리를 받나요?" → `_sbrk` 스텁이다. 이 레슨의 `mem_sbrk` 자리이고, 스택과 충돌하지 않게 검사하는 것이 보드 코드의 책임이다.
</details>

<details><summary>Q8. 이 할당기를 멀티스레드에서 쓰면? 시그널 핸들러에서는?</summary>

스레드 안전하지 않다. `free_head`, `rover`, `mem_brk` 가 전역이다. 가장 쉬운 방법은 전역 뮤텍스 하나다. 그러면 모든 malloc 이 직렬화된다. 실제 할당기(glibc ptmalloc 의 arena, jemalloc/mimalloc 의 스레드 캐시)는 스레드별 캐시와 여러 arena 로 경합을 피한다. 그 구조와 다른 스레드가 free 하는 경로(remote free)가 OS-M06 이다. 시그널 핸들러에서 malloc 을 부르면 안 된다. 메인 코드가 리스트를 고치는 중간에 끼어들면 반쯤 고쳐진 리스트를 보게 된다. 락이 있으면 같은 스레드가 자기 락을 기다리는 교착이 된다. POSIX 의 async-signal-safe 목록에 malloc 이 없다(OS-B07). ISR 에서 `pvPortMalloc` 을 부르지 않는 것도 같은 이유다. 꼬리질문: "fork 한 자식에서는?" → 멀티스레드 프로세스에서 fork 하면 다른 스레드가 쥐던 malloc 락이 잠긴 채 복사될 수 있다. 그래서 자식에서는 exec 전까지 async-signal-safe 함수만 쓰는 것이 원칙이다(POSIX fork 설명).
</details>

## 9. 추가 연습

### 연습 1 — `mm_calloc`: 곱셈 오버플로와 0 채우기

`mm_calloc(n, size)` 를 만든다. `n * size` 가 넘치면 NULL 을 돌려준다. 재사용된 블록에도 0 을 채운다. 테스트: `calloc(SIZE_MAX/2+1, 2)` 와 `calloc(2, SIZE_MAX/2+1)` 은 NULL, `calloc(0, 8)` 도 NULL(우리 `malloc(0)` 선택과 같게), `0xAB` 로 채웠다가 free 한 블록을 다시 받으면 전부 0.

<details><summary>답</summary>

`if (size && n > SIZE_MAX / size) return NULL;` 로 곱하기 전에 검사한다. 넘친 곱(예: 2 × (2^63) = 0)을 그대로 쓰면 **아주 작은 블록**을 받고, 호출자는 큰 배열이라고 믿고 쓰게 된다. 이것은 실제 보안 버그 패턴이다. 0 채우기를 빼먹으면 우리 힙은 mmap 영역을 재사용하므로 옛 데이터가 그대로 보인다(테스트가 `q == p` 로 같은 블록을 받은 것을 확인한다). 실제 libc 는 mmap 에서 새로 받은 페이지가 이미 0 이면 memset 을 생략하기도 한다고 알려져 있다(최적화, 미확인).
</details>

### 연습 2 — `mm_realloc_prev`: 앞 free 블록까지 흡수하기

다음 블록만으로 모자라고 **앞** 블록이 free 면, 앞 + 나 (+ 다음) 를 합쳐 제자리 비슷하게 키운다. 데이터는 앞으로 옮겨야 한다. 테스트: 앞 224 + 나 128 → 272 요청(남는 80 분할), 앞 128 + 나 128 + 뒤 128 → 320 요청(남는 64), 앞이 할당이면 기존 경로(이동). 그리고 T3 전체를 이 함수로 돌린다.

<details><summary>답</summary>

`fl_remove(prev)`(그리고 다음이 free 면 `fl_remove(next)`) → `memmove(prev, bp, csize − 16)` → `set_block(prev, total, 1)` → `place(prev, asize)` 순서다. 영역이 겹치므로 **memmove** 를 써야 하고, hdr/ftr 쓰기는 복사가 **끝난 뒤에** 해야 한다(합친 블록의 푸터 자리가 옛 payload 끝 근처일 수 있다). 실측은 기대와 달랐다. T3 에서 앞 흡수가 1912번 일어났지만 U 는 0.302 → 0.294 로 거의 그대로였다(오히려 약간 나빠졌다). LIFO 리스트의 근본 문제(큰 구멍을 작은 할당이 먹는 것)는 그대로라서, 복사만 memmove 로 바뀌었을 뿐 힙 성장은 줄지 않았다(해석은 추정). glibc 의 realloc 도 앞 블록 흡수를 하는지는 확인하지 않았다.
</details>

### 연습 3 — 주소 순서 명시적 리스트 = 암시적 first fit

`fl_insert` 에 `g_addr_order` 모드를 넣는다. 리스트를 주소 순서로 걸어 제자리에 끼운다. ① 블록 20개 중 홀수 번째를 뒤섞인 순서로 free 한 뒤, 리스트 순서가 힙 걷기의 free 블록 순서와 **같은지** 확인한다. ② 네 트레이스에서 암시적 first fit 과 high-water·peak 가 **정확히 같은지** 확인한다. ③ 그러면서 scans/malloc 은 더 작은지 확인한다.

<details><summary>답</summary>

같은 free 블록 집합을 같은 순서(주소)로 보고 처음 맞는 것을 고르므로, 두 할당기는 매번 같은 블록을 고른다. 분할·병합·realloc 규칙도 같다. 그래서 heap high-water 가 바이트 단위로 같다. 실측:

```text
  trace              impl-1st LIFO-1st ADDR-1st   scans/malloc impl vs addr
  T1 small-uniform      0.685    0.665    0.685   166.6 vs 9.0
  T2 mixed              0.897    0.732    0.897   147.2 vs 22.2
  T3 realloc-grow       0.719    0.302    0.719   341.3 vs 53.2
  T4 binary-pattern     0.537    0.537    0.537   749.5 vs 166.7
```

대가는 free 할 때마다 O(리스트 길이)의 삽입이다. 이 비용을 줄이는 것이 주소 정렬 트리(balanced tree)나 크기 클래스(OS-M05)이다. FreeRTOS heap_4 의 free list 도 주소순이다(RTOS-M10 그림). 병합을 쉽게 하려는 목적이 크다.
</details>

### 연습 4 — OSTEP `malloc.py` 모델 재현 (first / best / worst)

OSTEP 숙제 시뮬레이터 `vm-freespace/malloc.py` 의 모델(헤더 0, 정렬 없음, ADDRSORT, 병합 없음, 분할은 앞에서 자름)을 C 로 옮기고, 같은 연산열에서 **반환 주소와 탐색 수**가 시뮬레이터 출력과 같은지 CHECK 한다.

실제 실행한 명령과 출력(정책마다 마지막 4개 할당만 발췌, 시뮬레이터는 읽기 전용으로 `python3 -I`):

```text
$ python3 -I …/vm-freespace/malloc.py -S 100 -p FIRST -l ADDRSORT \
      -A +10,+5,+30,+5,+15,+5,-0,-2,-4,+12,+8,+20,+25 -c
Free List [ Size 4 ]: [ addr:1000 sz:10 ][ addr:1015 sz:30 ][ addr:1050 sz:15 ][ addr:1070 sz:30 ]
ptr[6] = Alloc(12) returned 1015 (searched 2 elements)
ptr[7] = Alloc(8) returned 1000 (searched 1 elements)
ptr[8] = Alloc(20) returned 1070 (searched 4 elements)
ptr[9] = Alloc(25) returned -1 (searched 4 elements)
$ … -p BEST …
ptr[6] = Alloc(12) returned 1050 (searched 4 elements)
ptr[7] = Alloc(8) returned 1000 (searched 4 elements)
ptr[8] = Alloc(20) returned 1015 (searched 4 elements)
ptr[9] = Alloc(25) returned 1070 (searched 4 elements)
$ … -p WORST …
ptr[6] = Alloc(12) returned 1015 (searched 4 elements)
ptr[7] = Alloc(8) returned 1070 (searched 4 elements)
ptr[8] = Alloc(20) returned 1078 (searched 4 elements)
ptr[9] = Alloc(25) returned -1 (searched 4 elements)
```

<details><summary>답</summary>

free list 를 `(addr, size)` 배열로 두고, free 는 주소순 삽입(병합 없음)으로 한다. malloc 은 시뮬레이터처럼 FIRST 는 처음 맞는 곳에서 멈추고, BEST/WORST 는 **끝까지 세며** 같은 크기면 앞의 것을 고른다(`<` / `>` 가 엄격 비교라서). 딱 맞으면 항목을 지우고, 크면 앞에서 잘라 `(addr+size, rest)` 로 바꾼다. 우리 C 모델의 출력은 `FIRST: Alloc(12)=1015[2] Alloc(8)=1000[1] Alloc(20)=1070[4] Alloc(25)=-1[4]` 로 세 정책 모두 위와 일치했다. 이 작은 예에서 25 B 요청은 best 만 성공했다. first/worst 는 30 B 구멍 두 개를 잘게 쪼개 놓았기 때문이다. 이 결과는 **병합 없는** 모델 기준이다. 우리 할당기의 정책 비교는 §5 의 표를 볼 것.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_san && ./pr_san
```

실제 출력: `16/16 passed` (0.46 초, ASan/UBSan 빌드도 16/16, 보고 없음).

## 10. 핵심 정리

- 블록 = hdr 8 + payload(16 정렬) + ftr 8, 크기 = `align16(n + 16)`, 최소 32 이다(free 블록은 next/prev 를 담아야 하므로). prologue/epilogue 를 "할당됨"으로 두면 경계 검사가 사라진다.
- 암시적 리스트는 O(전체 블록)이고, 명시적 이중 연결 리스트는 O(free 블록)이다. 실측 T2 scans/malloc 147.2 → 3.9, 처리량 20배 이상.
- 병합 4가지: (할당, 할당) 그대로 / (할당, free) 뒤 흡수 / (free, 할당) 앞으로 이동 / (free, free) 셋 합침. 이웃은 **먼저 리스트에서 뺀다**. 앞 이웃은 푸터로 O(1)에 찾는다(boundary tag).
- 분할은 남는 부분 ≥ 32 일 때만 한다. 그보다 작으면 내부 단편화로 남긴다.
- 정책보다 리스트 순서가 크다. 주소순 first fit(= 암시적 first fit)의 U 는 T2 0.897, T3 0.719 이고, LIFO first fit 은 0.732, 0.302 다. best fit 은 U 가 좋지만 리스트 전체를 본다.
- T4 의 0.537 은 어떤 배치 정책으로도 못 고친다. 크기를 섞어 놓은 외부 단편화이고, 해결은 크기별 분리(OS-M05, 슬랩)다.
- realloc 은 싼 것부터 시도한다: 축소 분할 → 다음 흡수 → 힙 끝 확장 → 이동. 실패하면 원본은 유효하다(`p = realloc(p, n)` 금지).
- `mm_check` 는 망가진 힙에서도 크래시하지 않아야 한다(범위 먼저, 순환 상한). ASan 은 내 힙 안의 넘침을 못 보므로 poison 하거나 검사기로 잡는다.

## 참고자료

- R. Bryant, D. O'Hallaron, *Computer Systems: A Programmer's Perspective* 3e, §9.9 Dynamic Memory Allocation — memlib(`mem_sbrk`), 묵시적 리스트 할당기 예제, 명시적 free list, segregated list, malloc lab 트레이스(binary-bal 류) (절 하위 번호는 기억 기반)
- OSTEP 17장 *Free-Space Management* — 17.2 분할·병합·헤더·free list 내장, 17.3 best/worst/first/next fit, 17.4 segregated·buddy: <https://pages.cs.wisc.edu/~remzi/OSTEP/vm-freespace.pdf> (로컬 `book-md/C17_free_space_management.md` 로 절 제목 확인), 숙제 시뮬레이터 `ostep-homework/vm-freespace/malloc.py`
- P. R. Wilson, M. S. Johnstone, M. Neely, D. Boles, "Dynamic Storage Allocation: A Survey and Critical Review", IWMM 1995 — 주소순 first fit vs LIFO, 단편화 정의 (기억 기반, 세부 미확인)
- D. E. Knuth, *TAOCP* Vol. 1, §2.5 Dynamic Storage Allocation — boundary tag (기억 기반)
- ISO/IEC 9899:2011 §7.22.3 Memory management functions (`realloc` 실패 시 원본 유지, 크기 0 은 구현 정의) · POSIX `malloc`/`realloc`: <https://pubs.opengroup.org/onlinepubs/9699919799/functions/realloc.html>
- macOS `man 3 malloc` (이 맥에서 읽음) — `realloc(p, 0)` 은 최소 크기 객체, `reallocf`, `MallocScribble`/`MallocGuardEdges`/`MallocCheckHeapStart`·`Each` 환경 변수
- Clang AddressSanitizer — manual poisoning (`ASAN_POISON_MEMORY_REGION`, `<sanitizer/asan_interface.h>`): <https://clang.llvm.org/docs/AddressSanitizer.html>
- MISRA C:2012 Rule 21.3 (동적 할당 금지 — 규칙 번호 기억 기반)
- 관련 레슨: OS-M05 (segregated list · 단편화 계측), OS-M06 (arena · tcache · remote free), RTOS-M10 (FreeRTOS heap_1~5, heap_4 병합), DS-M02 (고정 크기 풀), DS-M03 (슬랩), DS-M04 (버디), DS-A06 (TLSF), C-A05 (정렬 보장 할당), OS-B07 (async-signal-safe), OS-B09 (mmap)
