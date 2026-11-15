---
id: DS-A06
time: 120분
updated: 2026-11-08
---
# DS-A06 TLSF 할당기: 실시간 O(1) malloc

## 학습 목표

- FL/SL 2단계 인덱스로 **임의 크기**를 상수 시간에 리스트 번호로 바꾸고, **삽입 매핑은 내림, 탐색 매핑은 올림**이어야 하는 이유를 "그 리스트의 어떤 블록이든 요청보다 크다"는 한 문장으로 증명한다.
- 비트맵 2단계와 `clz`/`ctz`로 "요청 이상에서 비어 있지 않은 가장 작은 리스트"를 **루프 없이** 찾고, 비트맵과 리스트의 동기화(마지막 원소를 뺄 때만 비트를 끈다)를 코드로 지킨다.
- 경계 태그(boundary tag, `prev_phys`)로 **물리적 앞뒤 이웃**을 O(1)에 찾아 해제 즉시 병합하고, 잘못된 해제를 힙을 건드리기 전에 거절한다.
- 힙 불변식 검사기(물리 순회 vs free list vs 비트맵)를 만들고, 일부러 망가뜨린 힙 13가지를 **정확한 오류 코드로** 잡아낸다.
- O(1)을 말로 넘기지 않고 **센 값으로** 증명한다. malloc은 비트맵 7 / 리스트 2 / 분할 1, free는 6 / 3 / 병합 2가 상한이고, 그 상한에 실제로 도달하는 시나리오를 테스트로 만든다. 100만 회 스트레스에서 넘지 않음을 확인한다.
- 단편화와 최악 시간 분포를 고정 풀, first-fit, 시스템 malloc과 **같은 요청열**로 비교하고, 무엇은 이 맥에서 잴 수 있고 무엇은 잴 수 없는지 구분해서 말한다.

## 1. 문제

"RTOS에서 malloc을 쓰면 왜 안 되죠? 그럼 크기가 제각각인 메시지 버퍼는 어떻게 하죠?" 펌웨어 시니어 면접의 단골 질문이고, 답의 한쪽 끝이 고정 풀(DS-M02)이면 다른 쪽 끝이 TLSF(Two-Level Segregated Fit)다. 일반 malloc의 문제는 평균이 아니라 **최악**이다. free list를 훑는 할당기는 응답 시간이 free 블록 수에 비례하고, 그 값은 부팅 후 몇 시간이 지나야 커진다. 인터럽트를 잠그고 그 시간을 쓰면 데드라인이 깨진다.

TLSF는 Masmano·Ripoll·Crespo·Real의 ECRTS 2004 논문이 제안한 할당기다. 논문의 설계 기준은 네 가지다. **해제 즉시 병합**(immediate coalescing), 최소 블록 16 바이트(splitting threshold), **크기 등급별 리스트로 good-fit**, 재배치 없음(no reallocation). 핵심 수법은 크기를 두 단계로 나눈 표다. 1단계(FL)는 2의 거듭제곱 구간, 2단계(SL)는 그 구간을 선형으로 2^SLI 등분한다. 각 칸의 free list가 비었는지를 비트맵이 들고 있어서 "맞는 칸 찾기"가 `ctz` 몇 번으로 끝난다.

이 레슨의 설정:

| 항목 | 값 | 이유 |
|---|---|---|
| 정렬 | 8 B | 호스트 `max_align_t`와 임베디드 대부분의 경우 |
| SLI | 4 (FL 하나를 16칸으로) | 논문이 "4나 5가 적당"하다고 한 범위 |
| FL | 12개: `fl=0`은 [0,128) 8 B 간격, `fl=k`는 [2^(k+6), 2^(k+7)) | 작은 크기는 정확히, 큰 크기는 상대 오차 일정하게 |
| 블록 크기 | payload 바이트 (8의 배수, 최소 8) | 헤더 8 B는 따로 |
| 최대 힙 | 256 KB, `uint32_t` 오프셋 | 32비트 MCU와 호스트가 같은 레이아웃 |
| 제어 구조 | 848 B (`sizeof(tlsf_t)`, 실측) | 리스트 192개 × 4 B + 비트맵 13워드 + 필드 |

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">2단계 인덱스: 크기 460 은 어디로 가나 (SLI=4, f=8, s=12)</text>
  <text x="10" y="50" fill="currentColor">fl_bitmap</text>
  <g stroke="currentColor">
    <rect x="90" y="36" width="40" height="22" class="f-mut"/><rect x="130" y="36" width="40" height="22" class="f-mut"/>
    <rect x="170" y="36" width="40" height="22" class="f-acc"/><rect x="210" y="36" width="40" height="22" class="f-mut"/>
    <rect x="250" y="36" width="40" height="22" class="f-mut"/><rect x="290" y="36" width="40" height="22" class="f-acc"/>
    <rect x="330" y="36" width="40" height="22" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="104" y="52">0</text><text x="144" y="52">1</text><text x="184" y="52">2</text><text x="224" y="52">3</text>
    <text x="264" y="52">4</text><text x="304" y="52">5</text><text x="344" y="52">...</text>
  </g>
  <text x="390" y="52" fill="currentColor">bit i = sl_bitmap[i] != 0</text>
  <text x="10" y="94" fill="currentColor">fl=2</text>
  <text x="10" y="112" fill="currentColor" font-size="12">[256,512)</text>
  <text x="10" y="128" fill="currentColor" font-size="12">폭 16</text>
  <g stroke="currentColor" font-size="12">
    <rect x="90" y="78" width="36" height="30" class="f-mut"/><rect x="126" y="78" width="36" height="30" class="f-mut"/>
    <rect x="162" y="78" width="36" height="30" class="f-acc"/><rect x="198" y="78" width="36" height="30" class="f-mut"/>
    <rect x="378" y="78" width="36" height="30" class="f-acc"/><rect x="414" y="78" width="36" height="30" class="f-mut"/>
    <rect x="450" y="78" width="36" height="30" class="f-mut"/><rect x="486" y="78" width="36" height="30" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="102" y="98">0</text><text x="138" y="98">1</text><text x="174" y="98">2</text><text x="210" y="98">3</text>
    <text x="280" y="98">. . .</text><text x="386" y="98">12</text><text x="422" y="98">13</text><text x="458" y="98">14</text><text x="494" y="98">15</text>
  </g>
  <text x="530" y="90" fill="currentColor" font-size="12">sl_bitmap[2]</text>
  <text x="530" y="108" fill="currentColor" font-size="12">bit j = head[2][j] 있음</text>
  <path d="M396 108 L396 150" stroke="currentColor" fill="none"/>
  <rect x="330" y="150" width="130" height="30" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="340" y="170" fill="currentColor" font-family="monospace" font-size="12">블록 448..463</text>
  <path d="M460 165 L510 165" stroke="currentColor" fill="none"/>
  <rect x="510" y="150" width="130" height="30" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="520" y="170" fill="currentColor" font-family="monospace" font-size="12">블록 448..463</text>
  <text x="330" y="202" fill="currentColor" font-size="12">head[2][12]: 정렬 안 된 양방향 리스트 (맨 앞 삽입, 맨 앞 사용)</text>

  <text x="10" y="240" fill="currentColor" font-weight="bold">size = 460 의 비트에서 바로 읽는다</text>
  <g fill="currentColor" font-family="monospace" font-size="14">
    <text x="20" y="270">0000 0001 1100 1100b</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="295">f = fls(460) = 8 (최상위 1의 위치) → fl = f - 6 = 2</text>
    <text x="20" y="315">그다음 4비트 1100 = 12 → sl = 12 (이 예는 논문 7.1절의 값과 같다)</text>
    <text x="20" y="335">연산은 clz 하나, 시프트 하나, xor 하나뿐. 크기에 비례하는 루프가 없다.</text>
  </g>
</svg>
```

| 파트 | 구현 | 체크 |
|---|---|---|
| A | `map_insert`, `map_search`, `list_insert/remove`, `find_suitable`, `tlsf_malloc` | 값 표 + 8의 배수 전 범위 전수, 비트맵·리스트 동기화, 분할 경계(나머지 8 / 16), 소진 |
| B | `tlsf_free`, `tlsf_check` | 병합 4가지, 오류 7가지, 힙 망가뜨리기 13가지, 매 연산 검사 랜덤 테스트 |
| C | `tlsf_stats`, `fw_alloc/free` | 작업량 상한 도달, "맞는데 실패" 재현, 임계 구역 균형, 100만 회 |

관찰 데모: `demo_bench.c` (연산별 시간 분포, 작업량, 단편화, 실패 수), `demo_codegen.c` (arm64 / Cortex-M4 / Cortex-M0 asm).

### Part A — 매핑, 비트맵, malloc

- **삽입 매핑**(내림): 블록 크기 s → `s`가 들어 있는 구간의 (fl, sl). 리스트 안의 블록은 그 칸의 하한 이상이다.
- **탐색 매핑**(올림): 요청 r → 칸의 **하한**이 r 이상인 가장 작은 (fl, sl). 범위를 넘으면 실패로 돌려준다(`false`).
- `find_suitable`: sl 비트맵 → 없으면 fl 비트맵 → `ctz`로 답. 루프 금지.
- `tlsf_malloc`: 크기 조정(8의 배수, 최소 8) → 탐색 → 리스트 제거 → 분할(나머지 블록이 헤더 포함 16 B 이상일 때만) → busy 표시.

### Part B — free와 병합, 불변식 검사기

- 검증을 먼저 끝내고 그다음에 힙을 고친다. 범위·정렬 → 크기 → 앞뒤 물리 연결(가짜 헤더 거의 다 잡힘) → 이중 해제.
- 앞 이웃이 free면 합치고, 뒤 이웃이 free면 합친다. 합쳐진 블록 다음 블록의 `prev_phys`를 고치고, 새 크기로 리스트에 넣는다. 인접 free 두 개는 절대 없다.
- `tlsf_check`: 물리 순회 / 모든 free list / 비트맵 교차 검사. 위반 종류마다 코드가 다르다.

### Part C — 작업량, 단편화, 펌웨어 힙

- 각 연산의 최대 작업량이 상수임을 **세어서** 보이고, 상한에 도달하는 시나리오를 만든다(논문의 Test-4, Test-5와 같은 모양).
- `tlsf_stats`로 외부 단편화 `1 - largest/free_bytes`를 계산한다.
- `fw_alloc`/`fw_free`: 임계 구역(인터럽트 잠금 흉내) 안에서 할당하고, 임계 구역 한 번의 작업량을 기록한다. 실패 경로에서도 잠금을 풀어야 한다.
- 100만 연산 스트레스: 크기 3구간(작은 메시지 / 패킷 / 큰 프레임) 난수로 할당해 tag 패턴으로 채우고, 해제할 때 그대로인지 확인한다. 전체 불변식은 4096 연산마다(K = 4096)와 끝에서 검사한다.

## 2. Problem (English)

> **Implement a two-level segregated-fit allocator (TLSF) with worst-case O(1) `malloc` and `free` over a caller-provided heap of up to 256 KiB.**
>
> Block layout (offsets are `uint32_t`, relative to the heap): 8-byte header `{prev_phys; size|free}`; free blocks store `{next_free; prev_free}` in the first 8 bytes of the payload. Payload is aligned to 8 bytes; minimum payload 8. A size-0 busy sentinel header terminates the heap so the next physical block always exists.
>
> Segregation: `SLI = 4`. Sizes below 128 map linearly (8-byte steps) to list `(0, s/8)`; otherwise `f = floor(log2 size)`, `sl = (size >> (f - 4)) ^ 16`, `fl = f - 6`.
>
> ```c
> void   map_insert(uint32_t size, uint32_t *fl, uint32_t *sl);   // block goes here (round down)
> bool   map_search(uint32_t size, uint32_t *fl, uint32_t *sl);   // first list guaranteed big enough (round up)
> void  *tlsf_malloc(tlsf_t *t, size_t n);
> tf_err tlsf_free(tlsf_t *t, void *p, uint32_t *out_size);
> tcheck_t tlsf_check(const tlsf_t *t);
> ```
>
> Constraints: no loops proportional to the number of blocks in `malloc` or `free`; split only if the remainder (including its header) is at least 16 bytes; coalesce immediately with free physical neighbours; reject double frees and corrupted headers without touching the heap; no `malloc` in the allocator.
>
> Example: on a fresh 4096-byte heap, `tlsf_malloc(t, 100)` returns `heap + 8` with 104 usable bytes and leaves a free block of 3968 bytes at offset 112.
>
> Follow-ups: Why must search round up when insert rounds down? What exactly is the worst-case operation count? When can TLSF fail although a large enough block exists? What changes on a Cortex-M0 without `CLZ`, or on a cached core? How would you free from an ISR?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행 결과(실제 출력의 앞과 끝, 중간 생략):

```text
sizeof(tlsf_t) = 848 bytes, 192 lists
== Part A: mapping / bitmaps / malloc
FAIL starter.c:264  fl == 0u && sl == 1u
FAIL starter.c:265  fl == 0u && sl == 15u
...
FAIL starter.c:387  blocks == 57u && nf == 0u && t->fl_bitmap == 0u
== Part B: free / merge / invariant checker
SKIP Part B: Part A failed
== Part C: work bounds / fragmentation / firmware heap
SKIP Part C: Part A/B failed
10/45 passed
```

하네스 설계:

- 스텁은 안전한 틀린 값을 돌려주므로 멈추거나 크래시하지 않는다. Part B는 Part A가 다 통과해야, Part C는 A와 B가 통과해야 실행된다.
- 힙 메모리는 `uint8_t` 배열이다. 헤더는 `memcpy` 기반 `rd32/wr32`로만 읽고 쓴다(strict aliasing과 정렬 UB가 없고, `-O2`에서 load/store 하나로 컴파일된다).
- ASan은 전역 배열 **안쪽**의 overflow를 못 잡는다. 그래서 모든 스트레스는 할당할 때 패턴으로 채우고 해제할 때 확인한다.
- 새니타이저 빌드도 100만 회 그대로 돈다(이 맥에서 0.5 초). 규모를 줄이는 `#if`는 넣지 않았다.

## 4. 힌트

<details><summary>힌트 1 — map_insert는 clz 한 번이다</summary>

`f = 31 - clz(size)`로 최상위 1의 위치를 얻는다. 그 비트를 뺀 뒤 다음 SLI 비트가 sl이다. `(size >> (f - SLI))`는 최상위 1을 포함한 5비트이므로 `^ 16`으로 그 1을 지운다. fl은 `f - 6`(FL 0칸이 [0,128)을 맡으므로 f = 7이 fl = 1)이다. 128 미만은 `sl = size >> 3`, `fl = 0`으로 끝난다.
</details>

<details><summary>힌트 2 — 왜 올림이 "다음 칸의 하한"이 되는가</summary>

칸 폭 G = 2^(f−SLI)이다. r에 G−1을 더하면 r이 칸 하한과 정확히 같을 때는 같은 칸에 머물고, 조금이라도 크면 다음 칸의 수로 넘어간다. `+G`로 하면 하한이 정확히 맞은 요청이 한 칸 밀려서(경계값) 낭비가 생기고, `+0`으로 하면(삽입 매핑을 그대로 쓰면) 칸의 작은 블록을 내주어 **요청보다 작은 블록**을 돌려준다.
</details>

<details><summary>힌트 3 — 올림 뒤에도 끝난 게 아니다</summary>

올림은 표의 끝(fl = FL_COUNT)을 넘을 수 있다. `*fl`을 그대로 인덱스로 쓰면 배열 밖이다. map_search가 false를 돌려주고 malloc이 NULL을 돌려줘야 한다. 그 경계의 요청 값은 얼마일까? (253952는 되고 253960은 안 된다.)
</details>

<details><summary>힌트 4 — 분할의 불변식</summary>

`rem = b + HDR + r`에 새 헤더를 쓰고 크기 `sz - r - HDR`, `prev_phys = b`로 초기화한다. 그리고 `after = b + HDR + sz`의 prev_phys도 `rem`으로 바꿔야 한다. 이 한 줄을 빠뜨리면 **나중에 그 블록을 free할 때** 조용히 잘못된 곳을 병합한다.
</details>

<details><summary>힌트 5 — free의 순서</summary>

병합 전에 검증한다. "뒤 블록의 prev_phys가 나인가"와 "앞 블록(있으면)의 끝이 나인가" 두 개가 대부분의 가짜 포인터와 헤더 덮어쓰기를 잡는다. 병합은 ① 앞 ② 뒤 순서로 하고, 뒤를 합친 다음 그 다음 블록의 `prev_phys`를 start로 바꾼다. sentinel은 busy이므로 자동으로 멈춘다.
</details>

<details><summary>힌트 6 — 검사기가 다뤄야 할 세 관점</summary>

물리 순회가 진실이고, free list와 비트맵은 파생 정보다. 물리 순회로 "free 블록 개수"와 "진짜 블록 시작 위치들"(비트맵)을 만들고, 그다음 모든 리스트를 걸으며 노드가 그 시작 위치 중 하나이고 free 표시이며 자기 칸에 맞는 크기인지 본다. 사이클은 노드 수가 물리 free 수를 넘는 순간 잡는다.
</details>

## 5. 풀이

### 불변식 — 먼저 무엇이 항상 참이어야 하나

| 번호 | 불변식 | 깨지면 |
|---|---|---|
| I1 | 물리 순회가 0에서 끝 sentinel까지 정확히 닿고, 각 블록의 `prev_phys`가 맞다 | 잘못된 병합 |
| I2 | 물리적으로 붙은 free 블록 두 개는 없다 | 병합 누락 → 외부 단편화 |
| I3 | free 블록은 정확히 하나의 리스트, `map_insert(size)`의 칸에 있다 | 탐색이 못 찾거나 잘못 찾음 |
| I4 | `sl_bitmap[i]` bit j ⇔ `head[i][j] != NIL` | ctz가 빈 리스트로 가거나 블록을 놓침 |
| I5 | `fl_bitmap` bit i ⇔ `sl_bitmap[i] != 0` | 위와 같음 |
| I6 | 탐색 매핑의 칸 하한 ≥ r | **요청보다 작은 블록 반환** (치명적) |

I6이 이 알고리즘의 심장이다. 리스트는 **정렬되어 있지 않다**. 그러니 칸에서 맨 앞 블록을 꺼낼 때 그 블록이 r 이상임은 칸의 **하한**으로만 보장할 수 있다.

### Part A — 매핑

```c
static void map_insert(uint32_t size, uint32_t *fl, uint32_t *sl) {
    if (size < SMALL_LIMIT) { *fl = 0u; *sl = size >> ALIGN_LOG2; return; }
    uint32_t f = fls32(size);
    *sl = (size >> (f - SLI)) ^ SL_COUNT;
    *fl = f - (FL_SHIFT - 1u);
}
static bool map_search(uint32_t size, uint32_t *fl, uint32_t *sl) {
    if (size >= SMALL_LIMIT) size += (1u << (fls32(size) - SLI)) - 1u;
    map_insert(size, fl, sl);
    return *fl < FL_COUNT;
}
```

아래 수직선을 보면 왜 두 매핑이 달라야 하는지 분명하다. f = 8이면 칸 폭 G = 16이다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">f = 8 구간 (폭 G = 16): 삽입은 내림, 탐색은 올림</text>
  <line x1="40" y1="90" x2="710" y2="90" stroke="currentColor"/>
  <g stroke="currentColor">
    <line x1="60" y1="80" x2="60" y2="100"/><line x1="220" y1="80" x2="220" y2="100"/>
    <line x1="380" y1="80" x2="380" y2="100"/><line x1="540" y1="80" x2="540" y2="100"/>
    <line x1="700" y1="80" x2="700" y2="100"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="48" y="118">256</text><text x="208" y="118">272</text><text x="368" y="118">288</text><text x="528" y="118">304</text><text x="688" y="118">320</text>
  </g>
  <rect x="60" y="56" width="160" height="20" class="f-mut" stroke="currentColor"/>
  <rect x="220" y="56" width="160" height="20" class="f-acc" stroke="currentColor"/>
  <rect x="380" y="56" width="160" height="20" class="f-mut" stroke="currentColor"/>
  <rect x="540" y="56" width="160" height="20" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="120" y="71">칸 sl=0</text><text x="280" y="71">칸 sl=1</text><text x="440" y="71">칸 sl=2</text><text x="600" y="71">칸 sl=3</text>
  </g>

  <text x="10" y="156" fill="currentColor" font-weight="bold">삽입: 블록 크기 s</text>
  <circle cx="310" cy="176" r="5" class="f-acc" stroke="currentColor"/>
  <text x="322" y="181" fill="currentColor">s = 280 → 칸 sl=1 (280 &gt;= 272). 하한 이상이기만 하면 된다</text>

  <text x="10" y="214" fill="currentColor" font-weight="bold">탐색: 요청 r (8의 배수)</text>
  <circle cx="140" cy="234" r="5" class="f-warn" stroke="currentColor"/>
  <text x="152" y="239" fill="currentColor">r = 264: 칸 sl=0 안에 있지만 sl=0 의 블록은 256 일 수도 있다</text>
  <text x="152" y="257" fill="currentColor">→ 264 + 15 = 279 → 칸 sl=1 (하한 272 &gt;= 264). 무조건 맞다</text>
  <circle cx="60" cy="282" r="5" class="f-ok" stroke="currentColor"/>
  <text x="72" y="287" fill="currentColor">r = 256: 경계값 → 256 + 15 = 271 → 칸 sl=0. 한 칸도 낭비하지 않는다</text>
</svg>
```

전 범위를 **전수로** 확인했다(`test_mapping`). 8의 배수 크기 s = 8..262136 전부에 대해 `class_lo ≤ s < class_hi`이고, 표에 들어가는 모든 요청 r ≤ 253952에 대해 `class_lo(search(r)) ≥ r`이며 `class_lo - r < 칸 폭`(더 위로 가지 않음)이다.

| r | 삽입 칸 [lo, hi) | r + G − 1 | 탐색 칸 (fl, sl) | 칸 하한 |
|---|---|---|---|---|
| 120 | (0,15) [120,128) | (linear) | (0,15) | 120 |
| 128 | (1,0) [128,136) | 135 | (1,0) | 128 |
| 256 | (2,0) [256,272) | 271 | (2,0) | 256 |
| 264 | (2,0) [256,272) | 279 | (2,1) | 272 |
| 504 | (2,15) [496,512) | 519 | **(3,0)** — FL 경계를 넘음 | 512 |
| 253952 | (11,15) | 262143 | (11,15) | 253952 (표에 들어가는 최댓값) |
| 253960 | (11,15) | 262151 | fl = 12 → **false** | — |

마지막 줄이 좋은 면접 소재다. 힙이 256 KB여도 **253 952 B보다 큰 요청은 실패**한다. 블록 하나가 있어도 표의 맨 위 칸 하한이 그 값이기 때문이다. 큰 단일 요청이 필요한 설계라면 표를 더 크게 잡거나 그 크기를 별도로 처리해야 한다.

### Part A — 비트맵 탐색

```c
uint32_t sl_map = t->sl_bitmap[f] & (~0u << *sl);           /* 같은 FL 안에서 이 칸 이상 */
if (sl_map == 0u) {
    uint32_t fl_map = t->fl_bitmap & (~0u << (f + 1u));     /* 더 큰 FL */
    if (fl_map == 0u) return NIL;
    f = ctz32(fl_map);
    sl_map = t->sl_bitmap[f];
}
*fl = f; *sl = ctz32(sl_map);
```

워드 3개 이내로 끝나고 분기는 하나뿐이다. `sl_map`은 이미 `~0u << sl`로 깎였으므로 0이 아니면 ctz가 안전하다. `f + 1`은 `FL_COUNT`(12) 이하이고 32 미만임을 `_Static_assert`로 못 박았다(시프트 32는 C에서 UB).

### Part A — malloc 추적

4096 B 힙에서 `tlsf_malloc(100)` (`test_malloc`이 검사한다):

| 단계 | 상태 |
|---|---|
| 초기 | free 블록 @0, payload 4080, 칸 (5,15)[3968,4096) 하나. 비트맵 그 비트만 켜짐 |
| 크기 조정 | 100 → r = 104 |
| 탐색 | (0,13)에서 시작. sl 비트맵 비어 있음 → fl 비트맵 → (5,15), head = 0 |
| 제거 | 리스트 빈다 → sl 비트, fl 비트 끔 |
| 분할 | 4080 − 104 = 3976 ≥ 16 → 나머지 @112, payload 3968, prev_phys = 0, sentinel(@4088).prev_phys = 112 |
| 삽입 | payload 3968도 (5,15)이므로 같은 칸에 다시 들어감 → 비트 켬 |
| 결과 | `heap + 8` 반환, usable 104 |

### Part B — 병합

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">블록 배치와 해제: 앞뒤 물리 이웃을 O(1)에</text>
  <rect x="20" y="40" width="130" height="50" class="f-acc" stroke="currentColor"/>
  <rect x="150" y="40" width="100" height="50" class="f-mut" stroke="currentColor"/>
  <rect x="250" y="40" width="130" height="50" class="f-acc" stroke="currentColor"/>
  <rect x="380" y="40" width="200" height="50" class="f-mut" stroke="currentColor"/>
  <rect x="580" y="40" width="130" height="50" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="30" y="60">FREE</text><text x="30" y="78" font-family="monospace">A @0</text>
    <text x="160" y="60">BUSY</text><text x="160" y="78" font-family="monospace">B</text>
    <text x="260" y="60">BUSY (해제)</text><text x="260" y="78" font-family="monospace">C</text>
    <text x="390" y="60">FREE</text><text x="390" y="78" font-family="monospace">D</text>
    <text x="590" y="60">BUSY</text><text x="590" y="78" font-family="monospace">E</text>
  </g>
  <text x="20" y="112" fill="currentColor">헤더 8 B: +0 prev_phys, +4 size|free. free 블록만 payload 앞 8 B 에 next/prev_free.</text>
  <path d="M250 96 C210 130 190 130 170 96" stroke="currentColor" fill="none"/>
  <text x="178" y="140" fill="currentColor" font-size="12">prev_phys: 앞 블록은 O(1)</text>
  <path d="M380 96 C420 130 500 130 582 96" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="430" y="140" fill="currentColor" font-size="12">다음 블록 = b + 8 + size: 뒤는 산수로</text>

  <text x="10" y="176" fill="currentColor" font-weight="bold">C 를 해제 (앞 이웃 B 는 BUSY → 앞 병합 없음, 뒤 이웃 D 는 FREE → 병합)</text>
  <rect x="20" y="192" width="130" height="50" class="f-acc" stroke="currentColor"/>
  <rect x="150" y="192" width="100" height="50" class="f-mut" stroke="currentColor"/>
  <rect x="250" y="192" width="330" height="50" class="f-ok" stroke="currentColor"/>
  <rect x="580" y="192" width="130" height="50" class="f-acc" stroke="currentColor"/>
  <text x="30" y="222" fill="currentColor" font-size="12">FREE A</text>
  <text x="160" y="222" fill="currentColor" font-size="12">BUSY B</text>
  <text x="340" y="222" fill="currentColor" font-size="12">FREE C+D (크기 = C + 8 + D)</text>
  <text x="590" y="222" fill="currentColor" font-size="12">BUSY E</text>
  <text x="20" y="264" fill="currentColor" font-size="12">① D 를 free list 에서 제거 ② 크기 합치기 ③ E.prev_phys = C ④ 합친 크기로 삽입</text>
  <text x="20" y="284" fill="currentColor" font-size="12">B 까지 해제하면 앞 병합 A + B + C+D, E.prev_phys = A. 각 단계는 O(1), 루프 없음.</text>
  <text x="20" y="312" fill="currentColor">핵심: 이웃을 찾는 데 리스트를 쓰지 않는다. 주소 정렬이 필요 없으니 삽입은 항상 맨 앞이다.</text>
</svg>
```

free의 주요 경로(`test_free_merge`가 다섯 단계로 검사):

```c
if (pv != NIL && b_is_free(t, pv)) { list_remove(pv); start = pv; total += psz + HDR; }
if (b_is_free(t, nx))              { list_remove(nx); total += nsz + HDR; nx = nx + HDR + nsz; }
b_set(t, start, total, true);
b_set_prev(t, nx, start);   /* 합친 덩어리 다음 블록 */
list_insert(t, start, map_insert(total));
```

앞 병합과 뒤 병합의 순서를 바꿔도 결과는 같다. 다만 `nx`는 앞 병합 전에 계산했으므로 뒤 블록을 찾는 데 쓸 수 있고, 뒤를 합친 뒤에는 `nx`를 합친 크기만큼 옮겨서 **그다음** 블록을 가리키게 한다.

병합과 리스트 재삽입이 동시에 일어나므로 free는 리스트 연산을 최대 3번 한다. 이것이 작업량 표의 list=3이다.

오류 처리(모두 힙을 건드리지 않고 돌려준다. `test_free_errors`가 검사):

| 입력 | 결과 | 어떻게 감지하나 |
|---|---|---|
| `NULL` | OK, 아무 일 없음 | 표준 `free`와 같음 |
| 범위 밖 / 힙 시작보다 앞 | `TF_RANGE` | `uintptr_t`로 비교 (포인터 빼기 UB 회피) |
| 8의 배수 아님 | `TF_ALIGN` | |
| 블록 안쪽 포인터 (패턴 0x5A로 덮인 곳) | `TF_CORRUPT` | 가짜 크기가 경계를 넘거나 `next.prev_phys != b` |
| 이미 free, 앞뒤 busy | `TF_DOUBLE` | F 비트 |
| 앞 블록과 병합되어 사라진 블록 | `TF_CORRUPT` | 다음 블록의 prev_phys가 이미 다른 곳을 가리킴 |
| 앞 payload가 내 헤더를 덮음 | `TF_CORRUPT` | `next.prev_phys` 불일치. 힙 상태는 그대로 |

이 검증은 보안 경계가 아니다. 공격자가 정교하게 만든 가짜 헤더는 통과할 수 있다. 목표는 **버그를 빨리 잡는 것**이다.

### Part B — 불변식 검사기

순서가 중요하다. 물리 순회로 **진실**(블록 시작 위치, free 수)을 만들고, 리스트와 비트맵이 그 진실에 맞는지 본다. 위반별로 코드가 다르며 `test_checker_catches`가 하나씩 일부러 망가뜨려 **정확한 코드**를 확인한다.

| 망가뜨린 것 | 기대 코드 |
|---|---|
| busy 블록 크기 +8 | `TC_PREV` (다음 "헤더"의 prev가 맞지 않음) |
| busy 블록 크기 0 | `TC_WALK` |
| prev_phys 잘못 씀 | `TC_PREV` |
| sentinel 크기 ≠ 0 | `TC_SENTINEL` |
| busy 블록을 free로 표시 (앞이 free) | `TC_ADJ` — 병합을 빠뜨린 코드의 전형적 결과 |
| free 블록을 busy로 표시 | `TC_NOTFREE` |
| 블록을 다른 칸 리스트로 이동 | `TC_CLASS` |
| sl 비트 끔 / 빈 칸에 sl 비트 켬 / fl 비트 뒤집음 | `TC_BITMAP` ×3 |
| free list 사이클 / 블록 시작이 아닌 곳을 가리킴 | `TC_LINK` ×2 |
| free 블록을 리스트에서만 제거 | `TC_COUNT` |

이 13가지를 통과해야 그다음의 랜덤 테스트가 의미를 갖는다. 작은 힙(8 KB)에서 30000 연산, **매 연산마다** `tlsf_check` + 바이트 소유 맵으로 겹침을 잡는다.

```text
random-every-op: 14719 allocs, 572 NULLs over 30000 ops (tlsf_check after every op)
```

### Part C — O(1)을 세어서 증명

말로 "루프가 없다"고 하면 면접관이 이렇게 묻는다. "그럼 실제로 몇 번이요?" 비트맵 워드를 읽거나 쓸 때마다, 리스트 연산마다, 분할·병합마다 카운터를 올린다. malloc과 free 안에 **블록 수에 비례하는 반복이 없으므로** 상한은 프로그램 구조에서 그대로 나온다.

| 작업 | malloc 최악 | 어디서 |
|---|---|---|
| 비트맵 | 7 | 탐색 3 (sl, fl, sl) + 제거 2 + 나머지 삽입 2 |
| 리스트 | 2 | 제거 1 + 나머지 삽입 1 |
| 분할 | 1 | |
| 병합 | 0 | malloc은 병합하지 않는다 (나머지 블록의 뒤는 busy 이기 때문) |

| 작업 | free 최악 | 어디서 |
|---|---|---|
| 비트맵 | 6 | 앞 제거 2 + 뒤 제거 2 + 삽입 2 |
| 리스트 | 3 | 앞 제거 + 뒤 제거 + 삽입 |
| 병합 | 2 | |
| 분할 | 0 | |

"malloc에서 나머지 블록을 다음 블록과 병합할 일이 없다"는 것도 불변식이다. 분할 전 블록은 free였고, I2 때문에 그 다음 블록은 busy다.

상한이 헐겁지 않다는 것도 테스트로 만든다(`test_work_bounds`).

- **malloc 최악**: 힙이 큰 블록 하나뿐일 때 작은 요청(논문 Test-4와 같은 모양). 비어 있는 시작 칸(1), 더 큰 FL 찾기(1), 그 칸의 워드(1), 제거로 비트 둘 끔(2), 나머지 삽입(2) = 비트맵 7.
- **free 최악**: 양쪽 이웃이 각각 자기 FL의 유일한 블록(논문 Test-5와 같은 모양: "3개 할당, 1번·3번 해제, 2번 해제"). 앞 제거 2 + 뒤 제거 2 + 삽입 2 = 6, 리스트 3, 병합 2.

100만 회 스트레스의 실제 결과(`solution.c` 출력, 결정적):

```text
stress: 1000000 ops, allocs 480729, frees 479414, NULL 39857, peak block bytes 243232 of 262144
stress: worst external frag at a checkpoint 0.975 (free 24816, largest 608, free blocks 190)
stress: at end, live 1315, free 24752, largest 872, free blocks 176, frag 0.965
stress: max work per malloc bit=7 list=2 split=1 merge=0; per free bit=6 list=3 split=0 merge=2
```

상한에 정확히 도달했고 넘지 않았다. 체크리스트: 패턴 손상 0, 잘못된 해제 0, 임계 구역 불균형 0, 전부 해제 후 블록 하나로 복원(`b_size(0) == 256 KB - 16`). 끝의 free 24 KB는 조각으로 흩어져 있어 큰 요청을 못 받는다. 왜 이렇게 나쁜지는 아래 단편화 절에서 다룬다.

### 코드젠 — 비트 연산은 무엇으로 내려가나 (실제 `-S` 출력)

<!-- code: demo_codegen.c -->

```bash
cc -std=c11 -O2 -S demo_codegen.c -o -                                                 # arm64
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S -o - demo_codegen.c  # Cortex-M4
clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0 -mthumb -O2 -S -o - demo_codegen.c  # Cortex-M0
```

| 타깃 | `map_key` (매핑) | `find_bit` |
|---|---|---|
| arm64 (이 맥, `cc -O2`) | `clz` 사용, 분기 없음(`csel`) | `rbit` + `clz`로 ctz |
| Cortex-M4 (`thumbv7em`) | `clz` 인라인, 작은 크기는 IT 블록으로 조기 반환 | `rbit` + `clz` |
| Cortex-M0 (`thumbv6m`) | **`bl __clzsi2` 호출** (`demo_codegen.c` 전체에서 `bl __clzsi2` 3곳) | ctz는 `__ctzsi2` 호출 없이 곱셈 + 표 조회로 인라인 |

`clang --target=thumbv6m-none-eabi -mcpu=cortex-m0 -mthumb -O2 -S` 결과의 `map_key` 앞부분이다.

```text
	cmp	r0, #127
	bhi	.LBB0_2
	...
.LBB0_2:
	mov	r4, r0
	bl	__clzsi2
```

**O(1)이라는 말은 CLZ/CTZ 명령이 있을 때의 말이다.** Cortex-M0처럼 CLZ가 없으면 런타임 라이브러리 호출이 되고, 그 호출의 최악 사이클은 그 라이브러리 구현을 봐야 안다. 비트 폭(32)에 대한 상수 시간이긴 하지만 "명령 하나"라고 말하면 틀리다. 재는 도구는 대상 칩에서 DWT 사이클 카운터(M3 이상)나 GPIO 토글 + 스코프다.

### demo_bench — TLSF vs first-fit vs 고정 풀 vs 시스템 malloc

```bash
cc -std=gnu11 -O2 demo_bench.c -o db && ./db   # 약 2 초
```

요청열은 시드 고정, 슬롯 800개, 크기는 70% 8–96 / 25% 128–1024 / 5% 2048–8192. 힙은 256 KB. 고정 풀은 같은 256 KB를 나눴다. `pool-1class`는 8192 B × 32, `pool-3class`는 128 B × 209 + 1024 B × 84 + 8192 B × 18(바이트 수요 비율로 나눔). 3번 실행 결과가 같은 경향이었다. 아래는 3번째 실행의 실제 출력이다(머리 3줄 생략).

<!-- code: demo_bench.c -->

```text
mean ns per op, no per-op timers, no payload memset (median of 5 runs, min..max)
TLSF            26.5  (24.5..30.0)
first-fit       81.4  (78.5..85.3)
pool-1class      7.3  (7.3..7.7)
pool-3class     13.5  (12.0..14.1)
sys malloc      24.9  (22.7..27.7)

wall-clock per op, ns (median of 5 runs; each cell includes the timer pair)
allocator    | alloc: p50  p99   p99.9   max (run min..max)     | free: p50  p99   p99.9   max (run min..max)
TLSF         |        0    42      83 135751 ( 14750..1521625) |       41    42      84  87834 ( 12834..144500)
first-fit    |       83   209     292 167043 ( 77084..252209) |       42   208     250 136668 ( 33042..5313708)
pool-1class  |        0    42      42  49375 ( 12375..111000) |        0    42      42    125 (   125..   291)
pool-3class  |        0    42      42   9000 (  8375.. 13417) |        0    42      83  12916 (   126.. 28917)
sys malloc   |       41    83     125  69417 ( 36750..345750) |       41    83     125 132875 ( 22250..211375)

work per op (deterministic: TLSF = bit+list+split+merge, first-fit = free-list nodes visited)
TLSF         | alloc avg    4.26 max     10 | free avg    3.90 max     11 | free blocks max 98
first-fit    | alloc avg   40.02 max    142 | free avg   39.91 max    143 | free blocks max 138

fragmentation and failures (same in every run: the request stream is fixed)
allocator    | attempts   NULL   (large) | external avg/max (1-largest/free) | internal avg (held-req)/held
TLSF         |   500697   1011    1011 |    0.823 /  0.932                 | 0.029
first-fit    |   500427    452     452 |    0.661 /  0.905                 | 0.029
pool-1class  |   959459 918886   45851 | (n/a: class only)                  | 0.939
pool-3class  |   617511 234712   10382 | (n/a: class only)                  | 0.420
sys malloc   |   500211      0       0 |    (n/a: opaque)                  | 0.080 (malloc_size)
```

이 맥은 Apple M6, 클럭 측정은 `cntvct_el0`(CPU brand string `Apple M6` 확인). 측정의 한계를 먼저 말해 둔다. `cntfrq_el0`는 1 GHz를 보고하고 값의 단위도 ns인데, 실제 값은 41 또는 42씩 뛴다(직접 200만 번 읽어 분포를 봤다: 증가폭은 거의 전부 0, 41, 42이고 극히 일부만 더 크다). 그래서 **연산별 시간은 41.67 ns 단위로 양자화**된다. 위 표의 0, 41, 42, 83은 "0틱, 1틱, 2틱"이고 타이머 쌍 자체 비용도 포함이다. 할당기 연산이 수십 ns 수준이라 **백분위 칸은 대부분 양자화 그 자체**다.

읽는 법:

1. **평균(타이머 없는 ns/op)**이 가장 믿을 만하다. 3번 실행의 중앙값이 TLSF 25.7 / 27.4 / 26.5 ns, first-fit 78.3 / 82.4 / 81.4 ns, 3-class 풀 13.8 / 14.5 / 13.5 ns, 1-class 풀 7.9 / 9.4 / 7.3 ns, 시스템 malloc 28.4 / 29.4 / 24.9 ns였다. 경향은 일정하다. **고정 풀 < TLSF ≈ 시스템 malloc < first-fit**. first-fit이 3배 느린 것은 free list를 평균 40칸 걷기 때문이다(아래 작업량).
2. **최악(max) 칸은 할당기의 최악이 아니다.** TLSF와 풀의 max가 µs ~ 수백 µs로 뒤죽박죽이고 실행마다 수십 배 흔들린다. 인터럽트, 코어 이동, 스케줄링이 지배한다. macOS에서는 스레드를 코어에 고정할 수 없다(`QOS_CLASS_USER_INTERACTIVE`로 힌트만 줬다). **이 표에서 "TLSF의 최악 시간이 고정 풀보다 낫다/못하다"를 읽어 내면 안 된다.** 할당기의 최악은 시간이 아니라 작업량 칸에서 보아야 한다.
3. **작업량이 진짜 결과다.** 결정적이고 실행마다 같다. TLSF는 한 연산당 최대 10/11(`7+2+1`, `6+3+2`), first-fit은 최대 **142/143 노드 방문**. first-fit은 힙 상태에 따라 커진다(free 블록 최대 138개). first-fit은 할당이 앞쪽 작은 블록을 많이 지나고 해제가 삽입 위치를 찾느라 걷는다. TLSF의 작업량은 **free 블록 수와 무관**하다. 이것이 실시간에서 원하는 성질이다.
4. **단편화 — TLSF가 공짜가 아니다.** 같은 요청열, 같은 256 KB에서 first-fit의 실패가 452번인데 TLSF는 1011번(전부 큰 요청)이고, 외부 단편화 평균은 TLSF 0.823 vs first-fit 0.661이다. 이 워크로드에서 **first-fit이 메모리를 더 잘 쓴다**. 그럴듯한 설명 하나는 TLSF가 칸 하한으로만 판단해 "맞는 블록을 놓치는" 것(Part C의 `test_fits_but_fails`)이고, first-fit은 실제 크기를 비교한다는 점이다. 다만 이 차이의 원인을 따로 분리해서 재지는 않았다. 내부 단편화는 TLSF 0.029 = first-fit 0.029로 같다(둘 다 헤더 8 B + 8의 배수로 올림). 비교 조건은 한 시드, 한 분포, 한 크기이므로 일반 법칙으로 쓰면 안 되고 **"이번 측정에서는 그랬다"**로만 받아야 한다.
5. **고정 풀의 대가는 내부 단편화.** 1-class는 작은 요청에도 8 KB를 쓰니 0.939이고 슬롯이 32개라 96%가 실패한다. 3-class도 0.420에 23만 번 실패다(클래스별 분할이 수요와 어긋난 순간 비어 있는 다른 클래스를 쓸 수 없다). 풀은 **크기를 정해 놓고 쓰는 곳에서** 빛난다(DS-M02). 크기가 제각각이면 TLSF가 맞다.
6. 시스템 malloc은 무제한 힙이라 실패 0, 내부 단편화 0.080(`malloc_size` 기준, 메타데이터 미포함)이다. 동작 원리가 다르므로(libmalloc의 크기별 전략) 같은 표에 놓는 것은 기준선 역할일 뿐이다.

스트레스의 `frag 0.965`가 나쁘게 보여도 정상이다. 힙이 거의 찬 상태에서 큰 프레임이 실패하고 작은 조각만 남은 시점이다. **단편화 지표는 그때의 점유율과 같이 봐야 한다.**

### "맞는데 실패" — good-fit의 값

리스트는 정렬되어 있지 않고 탐색은 칸 **하한**만 믿는다. 그래서 칸의 위쪽에 있는 블록을 요청이 놓칠 수 있다. 논문 8.1절이 이것을 정면으로 설명한다(SLI = 5일 때 5887 블록이 있어도 5761 요청이 실패하며 그 차이가 126 B). 이 레슨의 SLI = 4 수치로 재현했다.

| 상태 | 요청 | 결과 | 이유 |
|---|---|---|---|
| free 블록 5880 B 하나뿐 | 5640 | **NULL** | 5640 → 칸 [5888, 6144)부터 찾음. 5880은 [5632, 5888) 칸 |
| 같은 상태 | 5632 | 성공, usable 5632 | 하한 그대로 — 그 칸을 본다 |

`test_fits_but_fails`가 정확히 이 값으로 확인한다. 미달 폭의 최댓값은 칸 폭 G − 8이다(SLI 4, f=12에서 248 B. 연습 5의 전수 계산). SLI를 올리면 폭이 절반이 되는 대신 리스트와 제어 구조가 두 배가 된다.

**흔한 오해.** "칸 올림 때문에 내부 낭비가 G만큼 생긴다"는 틀렸다. 블록을 r까지 잘라 쓰고 나머지는 분할해서 돌려주므로 내부 낭비는 정렬 올림 + 헤더 + (16 미만 나머지)뿐이다. 칸 올림의 대가는 **못 찾은 기회**, 즉 외부 단편화와 실패다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 124/124 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o s_san && ./s_san  # 깨끗함, 약 0.5 초
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| malloc에서 삽입 매핑을 그대로 사용 | 칸의 작은 블록이 r 미만일 수 있다 → **힙 오버플로** | `size += (1u << (fls32(size) - SLI)) - 1u;` (`map_search`) |
| 올림에 `-1`을 빠뜨림 | 경계값 요청이 한 칸 밀림. 낭비가 늘고 맞는 블록을 놓침 (`test_mapping`이 잡음) | `... - 1u` |
| 올림 후 범위 검사 없음 | `head[12][...]` 배열 밖 접근 | `return *fl < FL_COUNT;` |
| `~0u << (f)` 로 fl 검색 | 같은 FL의 낮은 칸을 다시 보게 되어 작은 블록 반환 | `~0u << (f + 1u)` |
| 마지막 원소를 뺄 때 fl 비트를 안 끔 / 먼저 끔 | ctz가 빈 워드나 빈 리스트로 간다 | sl 워드가 0이 된 뒤에만 fl 비트 끔 |
| `list_remove`에서 prev가 NIL인 경우를 head 갱신 없이 처리 | 지운 노드가 head로 남음 | `t->head[fl][sl] = n;` |
| 분할 후 `after.prev_phys` 안 고침 | 당장은 아무 일 없고 **나중의 free에서** 잘못된 곳과 병합. 가장 찾기 어려운 버그 | `b_set_prev(t, after, rem);` |
| 병합 후 다음 블록 `prev_phys` 안 고침 | 같음 | `b_set_prev(t, nx, start);` |
| 나머지가 헤더 + 링크(16 B)보다 작은데 분할 | free 링크 쓸 자리가 없어 다음 블록 헤더를 덮음 | `sz - r >= MIN_BLOCK` |
| 앞 병합에서 헤더 바이트(8 B)를 크기에 안 더함 | 힙이 한 번 병합될 때마다 8 B씩 사라짐 (전부 해제해도 복원 안 됨) | `total += psz + HDR` |
| 해제할 때 먼저 F 비트를 켜고 검증 | 실패 시 힙이 망가진 채로 돌아감 | 검증 → 수정 순서 |
| `size + 7` 먼저 하고 범위 검사 | `SIZE_MAX` 근처에서 올림 overflow → 작은 값으로 성공 | `n > MAX_HEAP` 먼저 |
| 검사기 memset 크기 계산 | 실제로 겪었다: 처음엔 `size/8/8 + 4`였는데 최대 힙(256 KB)에서 정적 배열을 4 B 넘어 썼고, 실행이 exit 133(SIGTRAP)으로 끝났다. 작은 힙 테스트는 모두 통과했다 | 워드 수로 계산: `((t->size/ALIGN_SZ + 31)/32) * sizeof starts[0]` |
| `cntvct_el0` 값에 mach 타임베이스(125/3)를 곱함 | 이 맥에서 `cntfrq_el0`이 1 GHz라서 값이 이미 ns다. mach tick 환산은 41배 틀린다 (실제로 해 봤고 413 ns짜리 가짜 타이머 비용이 나왔다) | `ns = 1e9 / cntfrq` |

## 8. 면접 Q&A

<details><summary>Q1. TLSF가 왜 O(1)인가요? "O(1)"이 정확히 뭘 뜻합니까?</summary>

malloc과 free에 블록 수에 비례하는 반복이 없다. 크기를 리스트 번호로 바꾸는 것은 `clz` + 시프트, 칸 찾기는 비트맵 워드 최대 3개 + `ctz`, 그다음은 리스트 맨 앞을 떼고 넣는 O(1) 연산뿐이다. 이 레슨 설정의 상한은 malloc = 비트맵 7, 리스트 2, 분할 1 / free = 비트맵 6, 리스트 3, 병합 2다. 100만 연산에서 그대로 관찰했고, 상한에 도달하는 입력도 테스트로 만들었다.

다만 이 O(1)은 "명령 수의 상한이 힙 상태와 무관하다"는 뜻이다. 절대 시간은 칩, 캐시 상태, CLZ 유무에 따라 다르다. 대상 하드웨어에서 DWT 사이클 카운터로 재서 임계 구역 예산과 비교해야 한다.
</details>

<details><summary>Q2. 왜 삽입 매핑과 탐색 매핑이 달라야 합니까? 같으면 어떻게 되나요?</summary>

리스트가 정렬돼 있지 않다. 그래서 칸의 블록이 r 이상임은 **칸 하한**으로만 보장된다. 삽입은 블록을 자기가 속한 칸에 넣기만 하면 되므로 내림이고, 탐색은 "이 칸의 어떤 블록이든 r 이상"이어야 하므로 올림이다.

같은 매핑(내림)을 쓰면 칸에서 하한만 맞는 작은 블록을 꺼내 r보다 작은 블록을 돌려준다. 곧바로 힙 오버플로다. 반대로 탐색에서 올림을 한 칸 더 하면(`-1` 빠뜨림) 경계값 요청이 맞는 칸을 건너뛰어 낭비와 실패가 늘어난다. 둘 다 `test_mapping`이 전 범위로 잡고, 뮤턴트로 확인했다.
</details>

<details><summary>Q3. 경계 태그를 왜 prev_phys 포인터로 했습니까? 푸터(footer) 대신?</summary>

Knuth의 원래 경계 태그는 블록 **끝**에 크기를 적어 앞 블록을 찾게 한다. 그러면 busy 블록에도 푸터가 필요해 오버헤드가 늘어난다. 논문은 이것을 "다음 블록 헤더 안에 앞 블록 시작 위치를 두는" 형태로 단순화했다. 이 레슨은 그대로 8 B 헤더(`prev_phys`, `size|F`)이고 busy 블록 오버헤드는 8 B이다.

추가 절약: mattconte의 구현은 `prev_phys`를 **앞 블록이 free일 때만 유효**하게 하고 그 앞 블록의 payload 끝 자리에 저장한다. 앞 블록이 free면 어차피 그 payload는 안 쓰이기 때문이다. busy 블록의 헤더는 size 워드 하나만 남는다(bit1이 "앞 블록이 free" 표시). 대신 "앞 블록이 free인지"를 다른 곳에서 알아야 하므로 bit가 하나 더 필요하다. 코드가 까다로워지는 대신 busy 블록마다 포인터 하나(32비트에서 4 B)를 아낀다.
</details>

<details><summary>Q4. 고정 풀, 슬랩, 버디, TLSF를 어떻게 고릅니까?</summary>

- **고정 풀**(DS-M02): 크기가 하나로 정해져 있을 때. 가장 빠르고(이 측정에서 7 ns) 단편화 없음. 크기가 섞이면 내부 낭비가 폭발한다(1-class 0.939).
- **슬랩**(DS-M03): 객체 종류 몇 개가 반복될 때. 풀 + 클래스 간 페이지 재배분.
- **버디**(DS-M04): 2^k 단위의 큰 블록(페이지, DMA 영역), 병합이 구조에 내장. 내부 단편화 최악이 커서 작은 객체에 안 맞는다.
- **TLSF**: 크기가 제각각이고 **상한이 있는 응답 시간**이 필요할 때 — 메시지 버퍼, 프로토콜 스택 등. 단편화 면에서 first-fit보다 나을 것이라 가정하면 안 된다(이번 측정은 반대였다). 시스템의 나머지와 같은 시점에 초기화해 단일 영역으로 관리할 때 이득이 크다.

실제 펌웨어에서는 섞어 쓴다. 자주 쓰는 고정 크기는 풀로 빼고, 나머지를 TLSF로 받으면 TLSF의 조각도 줄어든다.
</details>

<details><summary>Q5. ISR에서 tlsf_free를 불러도 됩니까? 임계 구역은?</summary>

단일 코어 RTOS에서 이 코드는 태스크-태스크, 태스크-ISR 모두 임계 구역이 필요하다(리스트와 비트맵은 공유 상태). 상한이 있으니 "임계 구역이 얼마나 길어지는가"를 분석할 수 있다는 게 TLSF의 가치다. `fw_heap_t`가 임계 구역 한 번의 최대 작업량을 기록한다.

그래도 ISR 안에서 할당기를 부르는 것보다 나은 선택이 있다. ISR은 해제할 포인터를 **lock-free 스택**(DS-A04)이나 SPSC 링(DS-A01)에 넣기만 하고, 저우선순위 태스크가 모아서 `tlsf_free`를 부른다. 그러면 ISR의 실행 시간에서 힙 연산이 빠지고, 잠금 범위는 태스크 간 공유로 줄어든다. 멀티코어라면 코어별 힙을 두거나 전역 락을 짧게 쥔다(이 레슨은 멀티코어 전역 락의 경합을 재지 않았다).
</details>

<details><summary>Q6. 단편화는 어디서 나오나요? SLI는 어떻게 정합니까?</summary>

내부 단편화는 정렬 올림 + 헤더 + 16 B 미만 나머지뿐이다(분할하므로). 측정 0.029. 외부 단편화는 두 가지 원인이다. ① 해제된 블록 사이에 busy 블록이 끼어 큰 덩어리를 못 만드는 것(어느 할당기나 동일). ② good-fit의 "맞는데 실패"(이 칸의 위쪽 블록을 못 봄). ②의 크기는 G − 8(SLI 4, 4–8 KB 구간에서 248 B)이다.

SLI를 1 올리면 미달 폭이 절반, 리스트 수와 제어 구조는 약 2배다(연습 5 실측: SLI 4 = 820 B, SLI 5 = 1456 B). RAM 예산과 "가장 큰 요청이 칸 폭 때문에 실패해도 되는가"로 정한다. 논문의 권장은 4나 5다.
</details>

<details><summary>Q7. 다른 코어로 옮기면 뭐가 깨지나요? (Cortex-M0, M7, 64비트)</summary>

- **M0/M0+**: CLZ가 없다. 실제 `-S`를 보면 clz는 `__clzsi2` 호출이 된다(`demo_codegen.c` 전체 3곳). ctz는 이 빌드에서 곱셈과 표로 인라인됐다. 표 기반 CLZ를 직접 넣어 시간을 고정할 수 있다.
- **M7 (D-cache)**: TLSF가 건드리는 곳은 블록 헤더 몇 개와 앞뒤 노드(`next`/`prev`)로 상수 개이지만, 그 상수 개가 **서로 다른 캐시 라인**이다. 최악 시간에 캐시 미스 몇 번이 포함된다. 힙이 DTCM에 있으면 이 문제가 없다.
- **멀티코어**: 비트맵과 리스트는 공유 데이터. 락을 쓰거나 코어별 힙 + 다른 코어로 보내는 원격 해제 큐를 둔다.
- **64비트 호스트**: 이 레슨처럼 `uint32_t` 오프셋이면 그대로 동작한다. 포인터 방식이면 free 링크가 16 B가 되어 최소 블록이 바뀐다.
</details>

<details><summary>Q8. 이런 할당기를 어떻게 테스트합니까?</summary>

① 망가뜨린 힙으로 검사기를 먼저 검증한다. 검사기가 틀리면 다른 테스트가 전부 무의미하다. ② 작은 힙에서 매 연산 후 전체 검사 + 바이트 소유 맵(겹침). ③ 큰 힙에서 패턴 쓰기/검증, K 연산마다 전체 검사. ④ 경계값 전수: 매핑은 8의 배수 전 범위. ⑤ 전부 해제 후 블록 하나로 복원(이게 안 되면 어딘가 샌다). ⑥ 뮤턴트: 일부러 넣은 버그 16개(올림 빠뜨림, 병합 생략, prev_phys 안 고침, 분할 경계 등)가 전부 실패로 잡히는지 확인했다.

ASan은 이 힙(전역 배열) **내부의** overflow를 못 본다. 패턴 검증을 쓰거나 `__asan_poison_memory_region`으로 free 영역에 독을 칠 수 있다(DS-M02의 demo 참고).
</details>

<details><summary>Q9. first-fit이 이번 측정에서 단편화가 더 좋았는데, 왜 TLSF를 쓰죠?</summary>

first-fit은 주소순으로 정확히 맞는 블록을 찾기 때문에 메모리를 더 알뜰하게 쓴 것으로 보인다(이번 측정에서 실패 452 vs 1011, 외부 0.661 vs 0.823). 대신 최대 143칸을 걷는다. 실시간 시스템에서 "평균적으로 메모리를 더 잘 씀"은 "최악 임계 구역이 힙 상태에 비례함"을 정당화하지 못한다. 힙 여유를 조금 더 주고 상한을 얻는 거래다. 데드라인이 없는 시스템이라면 first-fit이나 범용 malloc이 맞을 수 있다.
</details>

## 9. 추가 연습

### 연습 1 — `tlsf_resize_inplace`

p를 옮기지 않고 n 바이트로 바꾼다(realloc의 제자리 경로). 줄일 때 남는 꼬리는 free로 돌려주고, **뒤 이웃이 free면 합쳐서** 돌려준다. 늘릴 때는 뒤 이웃이 free이고 합쳐서 r 이상이면 흡수한다. 불가능하면 false(블록 그대로).

<details><summary>답</summary>

핵심은 꼬리를 그냥 free로 표시하면 뒤의 free 블록과 **붙은 채** 남는다는 것이다. `shrink_tail`이 뒤 이웃을 먼저 흡수해서 불변식 I2를 지킨다. 늘릴 때는 뒤 블록을 리스트에서 빼서 합친 다음, 남는 부분을 `shrink_tail`로 잘라 돌려주므로 코드를 공유한다. "늘렸더니 r + 8만 남아 못 쪼갬"은 usable이 r보다 큰 채로 끝나는 정상 동작이다.
</details>

### 연습 2 — `tlsf_memalign`

정렬(2의 거듭제곱, 8 이상) 할당. DMA 버퍼에 필요하다. 넉넉히 할당한 뒤 앞쪽 gap은 free 블록으로, 뒤쪽 남는 부분은 잘라 돌려준다. 앞쪽 gap이 16 B 미만이면 다음 정렬점으로 민다.

<details><summary>답</summary>

`r + align + MIN_BLOCK`을 요청하면 gap이 최대 `align + MIN_BLOCK - 8`이어도 r이 들어간다. gap 부분의 앞 블록(`b`)은 원래 free 블록에서 나온 것이므로 그 앞은 busy다(I2 덕분에 병합 불필요). 그래서 free 표시 후 리스트에 바로 넣는다. 일반 malloc 대비 **요청이 `align + 16`만큼 부풀어서** 큰 정렬은 큰 블록을 잠깐 필요로 한다. 전부 해제 후 블록 하나로 복원되는지 확인했다(20000회 랜덤 정렬: `NULL 0, bad 0`).
</details>

### 연습 3 — `tlsf_add_pool`

한 힙에 영역을 더 추가한다(예: 내장 SRAM + CCM). 맞닿은 영역이어도 **병합되지 않아야** 한다.

<details><summary>답</summary>

풀마다 맨 끝에 busy sentinel을 두면 병합이 자동으로 멈춘다. 첫 블록의 `prev_phys = NIL`이 "앞이 없음" 표시이므로 `tlsf_free`는 그대로 쓸 수 있다(그래서 solution은 `b == 0` 대신 `pv != NIL`로 판단한다). DMA 불가 영역과 가능 영역을 분리하려면 풀을 묶지 말고 TLSF 인스턴스를 나누는 게 맞다. 여기서는 맞닿은 풀과 떨어진 풀을 만들고, 전부 해제한 뒤 각각 풀 크기로 복원되는지 본다(풀을 가로지른 병합이 있으면 실패).
</details>

### 연습 4 — `tlsf_malloc_peek`

"맞는데 실패"를 줄이는 O(1) 보조 탐색. 정규 탐색이 실패하면 r 자신이 속한 칸의 **head 하나만** 확인한다.

<details><summary>답</summary>

5880 B 블록 하나, 요청 5640이면 정규 탐색은 NULL이지만 peek은 성공한다. 하지만 head가 5640이고 그 뒤에 5880이 있으면 5760 요청을 못 본다(리스트를 걷지 않으므로). 논문도 "이전 리스트를 루프로 훑으면 단편화는 줄지만 응답 시간이 유계가 아니게 된다"고 지적한다. head 하나만 보는 것은 그 중간 지점이다. 실제 이득은 작았다: 같은 난수 요청열에서 실패가 `plain 20215, peek 20212`로 3건만 줄었다. 칸 폭 미달로 실패하는 경우보다 큰 덩어리가 아예 없는 경우가 대부분이기 때문이다. 코드 복잡도와 비교해서 판단할 일이다.
</details>

### 연습 5 — SLI 스윕

SLI = 1..5에 대해 리스트 수, 제어 구조 크기, 4–8 KB 구간의 최대 미달 폭(블록 B가 있어도 받아 줄 수 없는 요청 중 가장 큰 B−r)을 전수로 계산한다.

<details><summary>답</summary>

실제 출력:

```text
SLI  FL  lists  control_bytes  worst_shortfall@[4096,8192)  G(f=12)
  1  15     30            184                         2040     2048
  2  14     56            284                         1016     1024
  3  13    104            472                          504      512
  4  12    192            820                          248      256
  5  11    352           1456                          120      128
```

미달 폭의 최댓값은 **G − 8**(이 레슨은 8 정렬)이다. 칸 하한만 믿고 내주기 때문에 블록이 칸의 맨 위쪽에 있으면 그만큼 놓친다. SLI를 하나 올리면 미달 폭은 절반, 리스트는 거의 두 배. 논문 8.1절의 예시(SLI = 5, 5887 블록과 5761 요청, 126 B)는 블록과 요청을 한 워드 단위로 잡아 식 `2^(f-SLI) - 2`, 즉 G − 2로 계산했다. 구조는 같다. control_bytes는 비트맵 + 리스트 head만의 크기다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice       # 37/37 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o p_san && ./p_san
```

## 10. 핵심 정리

- TLSF = FL(2의 거듭제곱) + SL(선형 2^SLI 등분) 표 + 2단계 비트맵. 칸 찾기는 `clz`/`ctz`, 루프 없음.
- **삽입 = 내림, 탐색 = 올림**(`size + G − 1`). 리스트가 정렬돼 있지 않으니 칸 하한으로만 안전을 보장한다. 올림 뒤에는 표 범위 검사 필요.
- 비트맵은 "마지막 원소가 빠질 때만" 끈다. sl 워드가 0이 될 때 fl 비트도 끈다.
- 물리 이웃은 `prev_phys`와 `b + 8 + size`로 O(1). 해제 즉시 병합, 인접 free 금지(I2), 마지막에 다음 블록 prev_phys 갱신.
- 작업량 상한: malloc 비트맵 7/리스트 2/분할 1, free 6/3/병합 2. 힙 상태와 무관하다. 100만 회에서 도달했고 넘지 않았다.
- good-fit의 대가는 외부 단편화와 "맞는데 실패"다(미달 폭 최대 G − 8). 내부 낭비는 작다. 이번 측정에서 first-fit보다 실패가 많았다.
- O(1)은 CLZ/CTZ가 있을 때의 말이다. M0은 `__clzsi2` 호출. 이 맥의 per-op 시간은 41.67 ns로 양자화되고 tail은 OS 영향이니, 결정적인 작업량으로 판단한다.
- 검사기는 망가뜨린 힙으로 먼저 검증하고, 전부 해제 후 블록 하나 복원을 항상 확인한다.

## 참고자료

- M. Masmano, I. Ripoll, A. Crespo, J. Real, "TLSF: a New Dynamic Memory Allocator for Real-Time Systems", ECRTS 2004 — 이 레슨에서 직접 확인한 내용: 설계 기준(즉시 병합, 최소 블록 16 B, good-fit, 재배치 없음), 매핑 함수와 460 → (8, 12) 예, SLI 4~5 권장, malloc/free의 O(1) 논증, 8.1절 단편화(5887/5761/126 예), Test-4·Test-5 시나리오
- mattconte/tlsf (`tlsf.c`) — `mapping_insert`, `mapping_search`, `search_suitable_block`, `prev_phys_block`가 앞 블록 끝에 겹치는 구현 확인
- D. E. Knuth, *The Art of Computer Programming* Vol. 1 — 경계 태그 (논문이 인용하는 출처)
- 관련 레슨: DS-M02 (고정 풀), DS-M03 (슬랩), DS-M04 (버디), DS-M09 (계층 비트맵), DS-A04 (lock-free 스택, ISR free 지연), DS-M10 (경계 태그 레코드)
