---
id: DS-B12
time: 60분
updated: 2026-11-07
---
# DS-B12 이진 힙과 우선순위 큐

## 학습 목표

- 완전 이진 트리를 **배열 하나**로 표현하는 인덱스 규칙(부모 `(i-1)/2`, 자식 `2i+1`, `2i+2`)을 외우지 않고 유도한다.
- min-heap의 **sift-up(push)** 과 **sift-down(pop)** 을 경계 조건(자식 1개, 빈 힙, 가득 찬 힙)까지 정확히 구현한다.
- Floyd의 **bottom-up heapify** 가 왜 O(n)인지 설명하고, 비교 횟수를 직접 재서 n번 push와 비교한다.
- 힙 속성 검사기 + 정렬 결과 대조로 자료구조를 검증하는 습관을 들인다.
- (만료 시각, 콜백 id) 힙으로 **소프트웨어 타이머 큐**를 만들고, tick 랩어라운드 같은 펌웨어 함정을 안다.

## 1. 문제

RTOS의 소프트웨어 타이머, 네트워크 스택의 재전송 타이머, 스케줄러의 "다음에 깨울 태스크"는 모두 **"가장 이른 것 하나를 빨리 꺼내기"** 문제다. 정렬 리스트는 꺼내기는 O(1)이지만 넣기가 O(n)이고, 이진 힙은 둘 다 O(log n)에 malloc 없이 배열 하나로 된다. 면접에서는 "타이머 매니저를 설계하라"의 핵심 부품이자, 손으로 짜게 하는 단골 문제다.

**힙 속성(heap property)**: 모든 노드는 자기 자식보다 작거나 같다. 그래서 루트 `a[0]` 이 항상 최소다. 형제끼리는 순서가 없다 — 힙은 "반쯤만 정렬된" 구조다. 완전 이진 트리(마지막 줄만 왼쪽부터 채움)라서 빈칸 없이 배열에 담긴다.

```svg
<svg viewBox="0 0 720 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">원소 = (expire, id) — push 7번 후의 min-heap. 트리와 배열은 같은 데이터</text>
  <g stroke="currentColor" fill="none">
    <path d="M360 75 L200 125"/><path d="M360 75 L520 125"/>
    <path d="M200 155 L120 195"/><path d="M200 155 L280 195"/>
    <path d="M520 155 L440 195"/><path d="M520 155 L600 195"/>
  </g>
  <rect x="320" y="45" width="80" height="30" rx="6" class="f-acc" stroke="currentColor"/>
  <g class="f-mut" stroke="currentColor">
    <rect x="160" y="125" width="80" height="30" rx="6"/><rect x="480" y="125" width="80" height="30" rx="6"/>
    <rect x="80" y="195" width="80" height="30" rx="6"/><rect x="240" y="195" width="80" height="30" rx="6"/>
    <rect x="400" y="195" width="80" height="30" rx="6"/><rect x="560" y="195" width="80" height="30" rx="6"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="360" y="65">5,0</text>
    <text x="200" y="145">10,1</text><text x="520" y="145">5,4</text>
    <text x="120" y="215">30,1</text><text x="280" y="215">10,2</text><text x="440" y="215">40,5</text><text x="600" y="215">20,3</text>
  </g>
  <g fill="currentColor" font-size="12"><text x="408" y="58">[0]</text><text x="246" y="138">[1]</text><text x="566" y="138">[2]</text>
    <text x="88" y="240">[3]</text><text x="248" y="240">[4]</text><text x="408" y="240">[5]</text><text x="568" y="240">[6]</text></g>
  <g stroke="currentColor" fill="none">
    <rect x="130" y="262" width="70" height="28"/><rect x="200" y="262" width="70" height="28"/><rect x="270" y="262" width="70" height="28"/>
    <rect x="340" y="262" width="70" height="28"/><rect x="410" y="262" width="70" height="28"/><rect x="480" y="262" width="70" height="28"/><rect x="550" y="262" width="70" height="28"/>
  </g>
  <rect x="130" y="262" width="70" height="28" class="f-acc"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="165" y="281">5,0</text><text x="235" y="281">10,1</text><text x="305" y="281">5,4</text><text x="375" y="281">30,1</text>
    <text x="445" y="281">10,2</text><text x="515" y="281">40,5</text><text x="585" y="281">20,3</text>
  </g>
  <text x="10" y="281" fill="currentColor">a[] =</text>
  <text x="10" y="318" fill="currentColor">부모 (i−1)/2, 왼쪽 자식 2i+1, 오른쪽 2i+2.  [1]의 10,1 과 [2]의 5,4 처럼 형제끼리는 순서 없음.</text>
</svg>
```

원소는 `tev_t { uint32_t expire; uint16_t cb_id; }`. 비교는 **expire 먼저, 같으면 cb_id** — 전순서(total order)로 만들어 두면 pop 순서가 하나로 정해져서 정렬 결과와 그대로 비교할 수 있다. 저장소는 `tev_t a[1024]` 고정 배열 (8바이트 × 1024 = 8KB).

### Part A — push / peek (sift-up)

- `sift_up(h, i)` — `a[i]` 가 부모보다 작으면 바꾸고 올라가기를 반복.
- `heap_push(h, e)` — 가득 차면 `false` (타이머 등록 실패를 호출자에게 알림). 아니면 맨 끝에 넣고 sift-up.
- `heap_peek(h, &out)` — 비었으면 `false`, 아니면 `a[0]`.
- 테스트: push **할 때마다** `heap_check()` 와 "peek == 지금까지의 최소"를 확인. 1024개를 역순으로 넣어 가득 채운 뒤 1025번째 push가 거절되는지.

### Part B — pop (sift-down) 과 정렬 대조

- `sift_down(h, i)` — 두 자식 중 **작은 쪽**과 비교해 크면 바꾸고 내려가기를 반복. 자식이 하나뿐인 노드 주의.
- `heap_pop(h, &out)` — 루트를 꺼내고, **마지막 잎을 루트로 옮긴 뒤** sift-down.
- 테스트: 7개를 넣고 꺼낸 순서가 정렬 순서와 같은지. 랜덤 push/pop 20000번을 **정렬 배열 참조 모델**과 매번 대조하고 매번 `heap_check()`.

### Part C — heapify, 비교 횟수 측정, 타이머 서비스

- `heap_heapify(h, src, n)` — 임의 배열을 Floyd 방식(마지막 내부 노드부터 0까지 sift-down)으로 O(n)에 힙으로 만든다. `n > 1024` 면 `false`.
- 비교 횟수 `g_cmp` 를 세어 n=1023개에 대해 "n번 push" vs "heapify" 를 random / ascending / descending 입력으로 비교한다.
- `timer_service(h, now, fired, max)` — 만료 시각 ≤ now 인 타이머를 **만료 순서대로** 꺼내 `fired[]` 에 콜백 id를 기록. 한 번에 최대 `max` 개.

## 2. Problem (English)

> **Fixed-capacity binary min-heap as a timer queue.**
>
> Elements are `struct { uint32_t expire; uint16_t cb_id; }`, ordered by `expire`, ties broken by `cb_id`. Storage is a static array of 1024 elements; no dynamic allocation.
>
> 1. Implement `bool heap_push(heap_t *h, tev_t e)`, `bool heap_peek(const heap_t *h, tev_t *out)`, and `bool heap_pop(heap_t *h, tev_t *out)`, each `O(log n)` or better. `push` returns `false` when full; `peek`/`pop` return `false` when empty.
> 2. Implement `bool heap_heapify(heap_t *h, const tev_t *src, uint32_t n)` in `O(n)`.
> 3. Implement `uint32_t timer_service(heap_t *h, uint32_t now, uint16_t *fired, uint32_t max)` that fires (records) up to `max` expired timers in expiry order and returns the count.
> 4. Verify the heap property after every operation and compare the pop sequence against a sorted reference.
>
> Example: push (30,1), (10,2), (20,3), (5,4), (10,1), (40,5), (5,0) → pops yield (5,0), (5,4), (10,1), (10,2), (20,3), (30,1), (40,5).

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "3/19 passed", exit 1 — TODO 7곳을 채워 19/19 를 만든다
```

TODO: `sift_up`, `heap_push`, `heap_peek` (Part A) / `sift_down`, `heap_pop` (Part B) / `heap_heapify`, `timer_service` (Part C). `heap_check()`, 참조 정렬, 비교 카운터 `ev_less()`, 하네스는 주어진다. 비교는 반드시 `ev_less()` 로 해야 Part C 측정에 잡힌다.

## 4. 힌트

<details>
<summary>힌트 1 — 인덱스 규칙 유도</summary>

줄 k(0부터)에는 노드가 2^k개, 그 줄의 첫 인덱스는 2^k − 1. 줄 안에서 j번째 노드(인덱스 i = 2^k − 1 + j)의 왼쪽 자식은 다음 줄의 2j번째 → 2^(k+1) − 1 + 2j = 2i + 1. 그래서 부모는 그 역 `(i − 1) / 2` (정수 나눗셈이 오른쪽 자식 2i+2도 같은 부모로 보낸다).

</details>

<details>
<summary>힌트 2 — push 는 "끝에 넣고 위로"</summary>

배열 끝 `a[n]` 은 완전 이진 트리의 다음 빈 잎이다. 넣으면 모양은 유지되고 힙 속성만 그 잎~루트 경로에서 깨질 수 있다 → 그 경로만 고친다(sift-up). `n++` 은 sift-up 전후 어느 쪽이든 되지만, `sift_up` 이 `n` 을 쓰지 않는지 확인.

</details>

<details>
<summary>힌트 3 — pop 은 "마지막을 루트로, 그리고 아래로"</summary>

루트를 빼면 구멍이 생긴다. 모양을 유지하는 유일한 방법은 마지막 잎을 루트로 옮기는 것. 그 다음 두 자식 중 **작은 쪽**과 비교한다 — 큰 쪽과 바꾸면 그 큰 자식이 작은 자식의 부모가 되어 힙 속성이 깨진다.

</details>

<details>
<summary>힌트 4 — heapify 는 아래에서 위로</summary>

잎(인덱스 ≥ n/2)은 이미 크기 1짜리 힙이다. `i = n/2 − 1` 부터 0까지 거꾸로 `sift_down(i)` 하면, 각 단계에서 i의 두 서브트리는 이미 힙이므로 sift-down 한 번이면 i를 루트로 하는 서브트리가 힙이 된다. `unsigned` 로 거꾸로 도는 루프는 `for (i = n/2; i-- > 0;)` 패턴이 안전하다.

</details>

## 5. 풀이

### Part A·B — 한 경로만 고친다

push와 pop 모두 **모양(완전 이진 트리)을 먼저 맞추고, 깨진 힙 속성을 경로 하나를 따라 고친다**. 트리 높이가 ⌊log₂ n⌋ 이라 둘 다 O(log n).

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b12a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">push(5,0): 끝에 넣고 sift-up</text>
  <text x="380" y="22" fill="currentColor" font-weight="bold">pop(): 마지막(20,3)을 루트로, sift-down</text>
  <g stroke="currentColor" fill="none">
    <path d="M180 85 L95 125"/><path d="M180 85 L265 125"/><path d="M95 155 L50 190"/><path d="M95 155 L140 190"/><path d="M265 155 L220 190"/><path d="M265 155 L310 190"/>
    <path d="M560 85 L475 125"/><path d="M560 85 L645 125"/><path d="M475 155 L430 190"/><path d="M475 155 L520 190"/><path d="M645 155 L600 190"/>
  </g>
  <g class="f-mut" stroke="currentColor">
    <rect x="148" y="55" width="64" height="30" rx="6"/><rect x="63" y="125" width="64" height="30" rx="6"/><rect x="233" y="125" width="64" height="30" rx="6"/>
    <rect x="18" y="190" width="64" height="30" rx="6"/><rect x="108" y="190" width="64" height="30" rx="6"/><rect x="188" y="190" width="64" height="30" rx="6"/>
    <rect x="443" y="125" width="64" height="30" rx="6"/><rect x="613" y="125" width="64" height="30" rx="6"/>
    <rect x="398" y="190" width="64" height="30" rx="6"/><rect x="488" y="190" width="64" height="30" rx="6"/><rect x="568" y="190" width="64" height="30" rx="6"/>
  </g>
  <rect x="278" y="190" width="64" height="30" rx="6" class="f-warn" stroke="currentColor"/>
  <rect x="528" y="55" width="64" height="30" rx="6" class="f-warn" stroke="currentColor"/>
  <rect x="658" y="190" width="64" height="30" rx="6" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="180" y="75">5,4</text><text x="95" y="145">10,1</text><text x="265" y="145">20,3</text>
    <text x="50" y="210">30,1</text><text x="140" y="210">10,2</text><text x="220" y="210">40,5</text><text x="310" y="210">5,0</text>
    <text x="560" y="75">20,3</text><text x="475" y="145">10,1</text><text x="645" y="145">5,4</text>
    <text x="430" y="210">30,1</text><text x="520" y="210">10,2</text><text x="600" y="210">40,5</text><text x="690" y="210" font-size="12">빠짐</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-width="2" marker-end="url(#b12a)">
    <path d="M318 188 L290 160"/><path d="M250 122 L208 92"/>
    <path d="M585 90 L625 120"/>
  </g>
  <g fill="currentColor" font-size="12"><text x="306" y="176">①</text><text x="232" y="100">②</text><text x="610" y="96">①</text></g>
  <g fill="currentColor">
    <text x="10" y="252">① 5,0 &lt; 부모 20,3 → 교환 (a[6]↔a[2])</text>
    <text x="10" y="274">② 5,0 &lt; 부모 5,4 (id 비교) → 교환 (a[2]↔a[0])</text>
    <text x="10" y="296">루트 도달 → 끝. 비교 2번, 교환 2번</text>
    <text x="380" y="252">① 자식 10,1 / 5,4 중 작은 5,4 와 교환</text>
    <text x="380" y="274">② a[2] 의 자식은 a[5]=40,5 하나뿐 (a[6] 없음)</text>
    <text x="380" y="296">   20,3 &lt; 40,5 → 멈춤</text>
    <text x="380" y="318">결과 a[] = [5,4 10,1 20,3 30,1 10,2 40,5]</text>
  </g>
</svg>
```

**sift-down 의 3가지 경계**:

| 상황 | 조건 | 처리 |
|---|---|---|
| 자식 2개 | `r < n` | 둘 중 작은 쪽 `m` 과 비교 |
| 자식 1개 (왼쪽만) | `l < n <= r` | 왼쪽과만 비교 — `r` 을 읽으면 이미 꺼낸 쓰레기 값과 비교하게 된다 |
| 자식 없음 (잎) | `l >= n` | 바로 멈춤 |

`m = i` 로 시작해서 `l`, `r` 이 "있고 더 작으면" `m` 을 갱신하는 패턴 하나로 세 경우가 다 처리된다. `m == i` 면 끝.

**pop 에서 n을 먼저 줄이는 이유**: `h->n--` 후 `a[0] = a[n]` 을 하면, sift-down이 보는 범위 `[0, n)` 에 옮겨 온 원소의 옛 자리(`a[n]`)가 포함되지 않는다. n=1일 때도 `a[0] = a[0]` 이 되고 sift-down은 바로 멈춘다 — 분기가 필요 없다.

**검증 전략**: `heap_check()` 는 모든 `i ≥ 1` 에 대해 "부모 ≤ 자식"만 보는 O(n) 검사다. 이걸 매 연산 후 돌리고, 동시에 정렬 배열을 참조 모델로 들고 다니며 pop 결과가 항상 `ref[0]` 인지 본다. 두 검사는 서로 다른 버그를 잡는다 — 힙 검사는 "구조가 맞나", 참조 대조는 "원소를 잃거나 복제하지 않았나". 랜덤 테스트 실제 출력:

```text
[B] random 20000 push/pop vs sorted reference: match (drained 1024)
```

push 확률이 3/4라 힙이 1024개로 꽉 찬 뒤 그 상태에서 push 거절 / pop 이 번갈아 일어난다 — 가득 찬 경계도 함께 시험된 것이다.

### Part C — heapify 가 O(n) 인 이유와 측정

n번 push는 각 원소가 **깊이만큼 위로** 올라갈 수 있어 최악 O(n log n)이다. Floyd heapify는 각 원소가 **높이만큼 아래로** 내려간다. 완전 이진 트리에서 노드의 절반은 높이 0(잎), 1/4은 높이 1, 1/8은 높이 2… 이므로

`총 작업 ≤ Σ (n / 2^(h+1)) · h = n · Σ h/2^(h+1) = n · 1 = O(n)`

대부분의 노드가 아래쪽에 몰려 있는데, heapify는 아래쪽 노드의 일을 0으로 만들고 push는 아래쪽 노드의 일을 최대로 만든다. 이게 차이의 전부다. sift-down 한 단계는 비교 2번이므로 비교 횟수 상한은 약 2n.

실제 측정 (n = 1023, 비교 = `ev_less()` 호출 수):

```text
[C] input       | n pushes cmp | heapify cmp | (n = 1023, n*log2(n) ~ 10230)
[C] random      |         2262 |        1900 |
[C] ascending   |         1022 |        1022 |
[C] descending  |         8194 |        2026 |
```

| 입력 | n번 push 비교 | heapify 비교 | 해석 |
|---|---|---|---|
| random | 2262 (2.2n) | 1900 (1.86n) | 무작위 입력의 push는 평균 O(1)로 거의 안 올라간다 — 둘 다 선형 |
| ascending | 1022 | 1022 | 이미 힙. push는 부모와 1번 비교, heapify는 내부 노드 511개 × 2번 |
| descending | **8194** (8.0n) | 2026 (1.98n) | push 최악: 새 원소가 매번 루트까지 — Σ⌊log₂ i⌋ (i=1..1023) = 8194 정확히 일치. heapify는 여전히 2n 이하 |

```svg
<svg viewBox="0 0 720 210" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">비교 횟수 (n = 1023, 막대 길이 = 비교 / 20)</text>
  <g fill="currentColor"><text x="10" y="62">random</text><text x="10" y="112">ascending</text><text x="10" y="162">descending</text></g>
  <rect x="110" y="44" width="113" height="16" class="f-mut" stroke="currentColor"/>
  <rect x="110" y="62" width="95" height="16" class="f-ok" stroke="currentColor"/>
  <rect x="110" y="94" width="51" height="16" class="f-mut" stroke="currentColor"/>
  <rect x="110" y="112" width="51" height="16" class="f-ok" stroke="currentColor"/>
  <rect x="110" y="144" width="410" height="16" class="f-warn" stroke="currentColor"/>
  <rect x="110" y="162" width="101" height="16" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="230" y="57">push 2262</text><text x="212" y="75">heapify 1900</text>
    <text x="168" y="107">push 1022</text><text x="168" y="125">heapify 1022</text>
    <text x="528" y="157">push 8194</text><text x="218" y="175">heapify 2026</text>
  </g>
  <text x="10" y="200" fill="currentColor">heapify 는 입력과 무관하게 2n 이하. push 는 내림차순(=min-heap 최악)에서 n·log n 쪽으로 간다.</text>
</svg>
```

**언제 heapify를 쓰나**: 부팅 시 NVM에 저장된 알람 목록을 한꺼번에 복원할 때, 또는 타이머 여러 개의 만료 시각을 일괄 조정(시계 보정)한 뒤 힙을 다시 세울 때. 하나씩 들어오는 이벤트는 push가 맞다.

### Part C — 타이머 서비스

```text
tick ISR (1 kHz)          타이머 태스크 (또는 ISR 하반부)
  now++                     e = peek()
  if peek.expire <= now      while e.expire <= now:
     태스크 깨움                  pop → callback(e.cb_id)
                               (max 개까지만 — 한 tick 에 처리량 제한)
```

ISR은 `peek` 만 보고(O(1)) 깨울지 판단하고, 무거운 pop·콜백은 태스크에서 한다. `max` 는 한 번에 너무 많은 타이머가 만료될 때 다른 태스크를 굶기지 않게 하는 상한이다 — 테스트 마지막 줄이 `max=1` 이면 하나만 처리하고 나머지는 다음 호출로 미루는 것을 확인한다. 같은 만료 시각(50)의 두 타이머는 id 순(2, 7)으로 나온다.

펌웨어 연결:

- **RAM 고정**: `tev_t a[1024]` 8KB가 `.bss` 에 확정. 가득 차면 push가 `false` — 호출자는 등록 실패를 처리해야 한다 (MISRA "모든 반환값 검사").
- **최악 시간 상한**: push/pop 모두 최대 ⌊log₂ 1024⌋ = 10단계. 정렬 리스트(FreeRTOS 타이머 리스트, Zephyr 타임아웃 리스트)는 삽입 O(n)이지만 타이머 수가 적으면 그게 더 단순하고 빠르다 — 수십 개 이하면 리스트, 수백 개 이상이면 힙이나 타이머 휠(DS 트랙 후반).
- **tick 랩어라운드**: 32비트 ms tick은 49.7일에 넘친다. `expire < now` 같은 직접 비교는 넘침 직후 순서가 뒤집힌다 → 연습 3. FreeRTOS는 넘칠 때 활성 리스트 두 개를 바꿔치기해서 이 문제를 푼다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 19/19 passed
# 메모리 오류 확인(선택)
cc -std=c11 -Wall -Wextra -O1 -g -fsanitize=address,undefined solution.c -o solution_asan && ./solution_asan
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| sift-down에서 왼쪽 자식과만 비교 / 큰 자식과 교환 | 작은 자식이 큰 자식의 자식이 되어 힙 속성 깨짐 | `m` 을 두 자식 중 작은 쪽으로 골라 교환 |
| 오른쪽 자식 존재 확인 누락 (`r < n`) | 꺼낸 자리의 쓰레기 값과 비교 → 원소 복제·유실 | `if (r < h->n && ...)` |
| 1-based 공식(`2i`, `2i+1`, `i/2`)을 0-based 배열에 사용 | 루트의 왼쪽 자식이 자기 자신(0) — 무한 루프나 잘못된 구조 | 0-based: `2i+1`, `2i+2`, `(i-1)/2` |
| `for (uint32_t i = n/2 - 1; i >= 0; i--)` | unsigned는 항상 ≥ 0 → 무한 루프, n=0이면 시작부터 거대 값 | `for (i = n/2; i-- > 0;)` |
| heapify를 0부터 위→아래로 sift-down | 아래 서브트리가 아직 힙이 아니라 결과가 힙이 아님 | 마지막 내부 노드부터 **거꾸로** |
| 만료 시각만 비교 (tie-break 없음) | 같은 시각의 순서가 구현마다 달라 테스트 비결정적. 힙은 **안정(stable)하지 않다** | `(expire, id)` 전순서, FIFO가 필요하면 삽입 순번 seq 추가 |
| `if (a.expire < b.expire)` 로 tick 비교 | 0xFFFFFF80 이 0x00000000 보다 "늦다"고 판단 (랩어라운드) | `(int32_t)(a - b) < 0` (연습 3) |
| push 실패 반환값 무시 | 타이머가 조용히 등록 안 됨 → 워치독 리셋 같은 원인 불명 버그 | 반환값 검사, 용량 부족은 설계 단계에서 상한 계산 |

## 8. 면접 Q&A

<details>
<summary>Q1. 배열 기반 힙에서 부모/자식 인덱스는? 왜 배열로 표현할 수 있나요?</summary>

0-based: 부모 `(i-1)/2`, 자식 `2i+1`, `2i+2`. 1-based면 `i/2`, `2i`, `2i+1` (곱셈·나눗셈이 시프트 하나라 교과서가 선호). 완전 이진 트리라서 레벨 순서로 빈칸 없이 채워지므로 포인터가 필요 없다 — 노드당 포인터 2~3개(8~24바이트)를 아끼고 캐시에도 유리하다.

</details>

<details>
<summary>Q2. heapify가 O(n log n)이 아니라 O(n)인 이유를 설명해 보세요.</summary>

각 sift-down 비용은 그 노드의 **높이**에 비례하고, 높이 h인 노드는 약 n/2^(h+1)개다. Σ h·n/2^(h+1) = n·Σ h/2^(h+1) = n. 노드 대부분이 잎 근처(높이 0~1)라 거의 일을 안 한다. 반대로 하나씩 push는 깊이에 비례하고 대부분이 깊은 곳에 있어 최악 n log n. 직접 재 보면 n=1023에서 heapify는 입력과 무관하게 ≤ 2026번, 내림차순 push는 8194번이었다.

</details>

<details>
<summary>Q3. 타이머 매니저를 설계하라 — 정렬 리스트, 힙, 타이머 휠 중 무엇을?</summary>

정렬 리스트: 삽입 O(n), 다음 만료 O(1), 취소 O(1)(이중 연결이면). 타이머 수십 개 이하인 RTOS에서 흔함(FreeRTOS, Zephyr). 이진 힙: 삽입/pop O(log n), 취소는 위치 맵이 있으면 O(log n)(연습 4). 수백~수천 개, 해상도가 높은 타이머(libuv, Go 런타임)에 적합. 타이머 휠: 삽입/취소 O(1), 만료 처리는 tick당 O(1) 평균, 해상도와 범위를 맞바꿈 — 대부분 만료 전에 취소되는 네트워크 타임아웃(Linux timer wheel)에 최적. 꼬리질문 "tick 넘침은?" → 차이 비교 `(int32_t)(a-b) < 0`, 또는 64비트 tick.

</details>

<details>
<summary>Q4. 힙에서 임의의 원소(예: 취소된 타이머)를 지우려면?</summary>

먼저 그 원소의 위치를 알아야 한다 — 선형 탐색 O(n) 대신 `pos[id]` 맵을 두고 swap 할 때마다 갱신한다. 지울 때는 마지막 원소로 그 자리를 메우고, 옮겨 온 원소가 부모보다 작으면 sift-up, 아니면 sift-down. 한쪽만 하면 틀린다 — 마지막 원소는 다른 서브트리에서 왔으므로 위로 갈 수도 있다. 대안은 "취소 표시만 하고 pop 때 버리기(lazy deletion)" — 구현은 쉽지만 힙이 취소된 원소로 차서 용량을 낭비한다.

</details>

<details>
<summary>Q5. k개의 가장 큰 값(또는 정렬된 k개 스트림 병합)을 힙으로 어떻게 푸나요?</summary>

top-k: 크기 k의 **min**-heap을 유지하고 새 값이 루트(현재 k번째로 큰 값)보다 클 때만 루트를 교체 후 sift-down. O(n log k), 메모리 O(k) — 센서 스트림에서 피크 k개 추적에 적합(연습 2). k-way 병합: 각 스트림의 머리를 min-heap에 넣고 pop할 때마다 그 스트림의 다음 원소를 push. O(N log k). 외부 정렬, LSM 트리 컴팩션, 여러 센서 타임스탬프 병합에 쓴다.

</details>

<details>
<summary>Q6. 이 힙을 ISR과 태스크가 공유한다면 무엇을 조심해야 하나요?</summary>

push/pop은 여러 원소를 옮기는 다단계 연산이라 중간 상태에서 ISR이 읽으면 힙 속성이 깨진 배열을 본다. 보통 ① ISR에서는 `peek` 만 하거나, ② ISR은 "등록 요청"을 SPSC 링 버퍼에 넣고 태스크가 힙을 단독 소유하거나(가장 깔끔), ③ 짧은 임계 구역으로 감싼다. ③의 경우 임계 구역 길이가 O(log n) = 최대 10단계로 **상한이 있다**는 점이 정렬 리스트(O(n)) 대비 장점이다.

</details>

## 9. 추가 연습

### 연습 1 — 제자리 힙 정렬

`uint32_t` 배열을 추가 메모리 없이 오름차순 정렬하라. 어떤 힙(min/max)을 써야 하나?

<details>
<summary>답 보기</summary>

**max**-heap. heapify 후 루트(최대)를 배열 끝과 바꾸고 힙 크기를 1 줄여 sift-down — 이걸 반복하면 뒤에서부터 큰 값이 쌓여 오름차순이 된다. O(n log n) 최악 보장 + 추가 메모리 O(1) + 재귀 없음이라 스택이 작은 MCU에서 퀵소트(최악 O(n²), 재귀 깊이) 대신 쓰기 좋다. 단점은 안정 정렬이 아니고 캐시 지역성이 나쁘다는 것. 1000개 무작위 값을 삽입 정렬 결과와 비교한다.

</details>

### 연습 2 — 스트림의 가장 큰 k개

5000개 값이 하나씩 들어올 때 가장 큰 10개를 유지하라. 메모리는 10칸만.

<details>
<summary>답 보기</summary>

크기 10의 **min**-heap. 처음 10개는 push, 이후엔 `x > top[0]` 일 때만 루트를 x로 바꾸고 sift-down. 루트는 "현재 커트라인(10등)"이다. max-heap을 쓰면 10등을 바로 알 수 없어 틀린다. 결과를 전체 정렬의 마지막 10개와 비교한다.

</details>

### 연습 3 — tick 랩어라운드에 안전한 비교

now = 0xFFFFFF00 에서 만료 now+0x80, now+0x100, now+0x180 인 타이머 3개(id 1, 2, 3)를 넣고 pop 순서를 직접 비교(`<`)와 `(int32_t)(a - b) < 0` 비교로 각각 출력하라.

<details>
<summary>답 보기</summary>

```text
[3] pop order  naive: 2 3 1 | wrap-safe: 1 2 3
```

직접 비교는 넘친 값 0x00000000(id 2), 0x00000080(id 3)을 0xFFFFFF80(id 1)보다 이르다고 본다 — 실제로 가장 먼저 만료될 타이머가 꼴찌. 차이를 부호 있는 수로 보면 "a가 b보다 2^31 미만만큼 앞서면 음수"라 올바르다. 조건: 힙 안의 모든 만료 시각이 서로 2^31 tick(ms 기준 24.8일) 이내. 힙 전체가 이 창 안에 있어야 비교가 추이적(transitive)이라 힙 속성이 의미를 가진다. (C-B02의 `time_after` 와 같은 원리 — Linux `time_after()` 매크로.)

</details>

### 연습 4 — 타이머 취소: 위치 맵 + 임의 삭제

`pos[id]` 맵을 유지하며 `th_cancel(id)` 를 O(log n)으로 구현하라. 랜덤 등록/취소/만료 20000번 동안 매번 힙 속성과 "pos[id] 가 가리키는 칸에 정말 그 id 가 있는지"를 검사하라.

<details>
<summary>답 보기</summary>

swap 할 때마다 두 원소의 `pos` 를 갱신한다(`th_swap`). 삭제는 마지막 원소로 메운 뒤 부모보다 작으면 sift-up, 아니면 sift-down. 지운 칸이 마지막 칸이면 옮길 게 없다. 같은 id를 두 번 등록하지 못하게 `pos[id] >= 0` 이면 거절한다. 실제 실행에서 220번의 성공한 취소를 포함한 20000 연산이 모두 검사를 통과했다:

```text
[4] random push/cancel/pop: ok (cancels=220)
```

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 7/7 passed
```

## 10. 핵심 정리

- 0-based 배열 힙: 부모 `(i-1)/2`, 자식 `2i+1`, `2i+2`. 루트 `a[0]` = 최소. 형제끼리는 순서 없음.
- push = 끝에 넣고 sift-up, pop = 마지막을 루트로 옮기고 sift-down. 둘 다 O(log n), 경로 하나만 고친다.
- sift-down은 **작은 자식**과 비교, `r < n` 확인. `m = i` 로 시작하는 패턴으로 경계 3가지 처리.
- Floyd heapify: `for (i = n/2; i-- > 0;) sift_down(i)` → O(n). 실측 n=1023에서 ≤ 2026 비교, 내림차순 push는 8194.
- 검증 = 매 연산 후 `heap_check()` + 정렬 참조 모델 대조.
- 타이머 큐: `(expire, id)` 전순서, ISR은 peek만, 처리량은 `max` 로 제한, tick 비교는 `(int32_t)(a-b) < 0`.
- 취소가 필요하면 `pos[id]` 맵 + 임의 삭제(sift-up 또는 sift-down).

## 참고자료

- CLRS *Introduction to Algorithms*, Ch. 6 Heapsort (6.3 Building a heap — O(n) 증명), 6.5 Priority queues
- Robert W. Floyd, "Algorithm 245: Treesort 3", *Communications of the ACM* 7(12), 1964
- [libuv `src/heap-inl.h` — 타이머용 이진 min-heap](https://github.com/libuv/libuv/blob/v1.x/src/heap-inl.h)
- [FreeRTOS `timers.c` — 정렬 리스트 기반 소프트웨어 타이머와 tick 넘침 리스트 교체](https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/timers.c)
- [Linux `include/linux/jiffies.h` — `time_after()` 랩어라운드 비교](https://github.com/torvalds/linux/blob/master/include/linux/jiffies.h)
- Varghese & Lauck, *Hashed and Hierarchical Timing Wheels* (1987) — 힙 대안 비교
