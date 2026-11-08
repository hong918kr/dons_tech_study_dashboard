---
id: DS-M09
time: 75분
updated: 2026-11-07
---
# DS-M09 계층 비트맵: O(1) 최고 우선순위 탐색

## 학습 목표

- 1024개 우선순위를 **32비트 요약(summary) 1워드 + 32비트 하위(leaf) 32워드**로 나눠 담고, CLZ 두 번으로 최고 우선순위를 **루프 없이** 찾는다.
- 불변식 "요약 비트 g == (leaf[g] ≠ 0)" 을 set/clear 모두에서 지키고, 이것이 깨지면 왜 `__builtin_clz(0)` UB 로 이어지는지 설명한다.
- 마스크 두 단계로 "p보다 작은 것 중 최고"(`hb_prev`)를 구하면서 **시프트 양 32 UB** 를 피한다.
- 우선순위별 FIFO + 계층 비트맵으로 **O(1) RTOS 준비 큐(ready queue)** 를 만들고, 선형 탐색 참조 스케줄러와 랜덤 대조로 검증한다.
- "큰 번호가 급한가, 작은 번호가 급한가" 관례에 따라 CLZ/CTZ/뒤집어 저장을 고르고, FreeRTOS·Linux·µC/OS 가 각각 어떻게 하는지 말할 수 있다.

## 1. 문제

RTOS 스케줄러는 틱 인터럽트와 모든 블록/깨움마다 "지금 준비된 태스크 중 가장 급한 것"을 고른다. 이 시간이 준비된 태스크 수나 우선순위 분포에 따라 달라지면 **최악 실행 시간(WCET)** 을 보장할 수 없다. 그래서 FreeRTOS·µC/OS·Linux RT 스케줄러는 모두 "우선순위마다 1비트 + 비트 탐색 명령" 으로 이 선택을 **상수 시간**에 끝낸다. 면접에서는 "우선순위 1024개 스케줄러를 O(1)로" → "CLZ 없는 코어에서는?" → "ISR 과 동시에 쓰면?" 순으로 꼬리질문이 나온다.

DS-B08 에서 만든 평평한 비트맵은 빈 워드를 건너뛰어도 1024비트면 최악 32워드를 훑는다. 이 레슨은 그 위에 **요약 워드** 하나를 얹는다: 요약 비트 g 는 "leaf[g] 에 1이 하나라도 있다"는 뜻. 그러면 요약에서 CLZ 한 번으로 그룹을, 그 leaf 에서 CLZ 한 번으로 비트를 찾는다 — 32 × 32 = 1024 이라 정확히 두 단계.

**이 레슨의 관례 (끝까지 고정)**: 우선순위 번호 p = 0..1023, **숫자가 클수록 급하다** (FreeRTOS 관례, idle = 0). 그래서 "최고 우선순위" = **set 된 비트 중 가장 큰 p** = 가장 높은 1비트 = `31 - clz`. 반대 관례(0 이 가장 급함: Linux RT, µC/OS, Zephyr)는 연습 1 에서 다룬다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">우선순위 {5, 31, 32, 700, 1023} 이 set 된 계층 비트맵 (132바이트)</text>
  <text x="10" y="52" fill="currentColor" font-family="monospace">top</text>
  <g stroke="currentColor" fill="none">
    <rect x="60" y="36" width="640" height="24"/>
  </g>
  <rect x="60" y="36" width="20" height="24" class="f-acc" stroke="currentColor"/>
  <rect x="260" y="36" width="20" height="24" class="f-acc" stroke="currentColor"/>
  <rect x="660" y="36" width="20" height="24" class="f-acc" stroke="currentColor"/>
  <rect x="680" y="36" width="20" height="24" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">
    <text x="70" y="75">31</text><text x="270" y="75">21</text><text x="670" y="75">1</text><text x="690" y="75">0</text>
    <text x="470" y="53">나머지 28비트 = 0</text>
  </g>
  <text x="560" y="28" fill="currentColor" font-family="monospace" font-size="12">= 0x80200003</text>
  <g stroke="currentColor" fill="none">
    <path d="M70 80 L95 150"/><path d="M270 80 L275 150"/><path d="M670 80 L475 150"/><path d="M690 80 L655 150"/>
  </g>
  <g stroke="currentColor" class="f-mut">
    <rect x="20" y="150" width="150" height="56" rx="4"/><rect x="200" y="150" width="150" height="56" rx="4"/>
    <rect x="400" y="150" width="150" height="56" rx="4"/><rect x="580" y="150" width="150" height="56" rx="4"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="95" y="170">leaf[31]</text><text x="95" y="186">0x80000000</text><text x="95" y="201">b=31 → p=1023</text>
    <text x="275" y="170">leaf[21]</text><text x="275" y="186">0x10000000</text><text x="275" y="201">b=28 → p=700</text>
    <text x="475" y="170">leaf[1]</text><text x="475" y="186">0x00000001</text><text x="475" y="201">b=0 → p=32</text>
    <text x="655" y="170">leaf[0]</text><text x="655" y="186">0x80000020</text><text x="655" y="201">b=31,5 → p=31,5</text>
  </g>
  <g fill="currentColor">
    <text x="10" y="236">p = 32·g + b,   g = p &gt;&gt; 5 (그룹 = leaf 번호),   b = p &amp; 31 (leaf 안 비트)</text>
    <text x="10" y="258">불변식: top 의 비트 g == (leaf[g] != 0).  나머지 28개 leaf 는 0 이고 그 top 비트도 0.</text>
    <text x="10" y="280">최고: 31 − clz(top) = 31 → 31 − clz(leaf[31]) = 31 → p = 31·32 + 31 = 1023 (워드 2개, 루프 0)</text>
  </g>
</svg>
```

자료구조 (malloc 없음):

```c
typedef struct {
    uint32_t top;          /* 비트 g = leaf[g] 에 set 비트가 있음 */
    uint32_t leaf[32];     /* leaf[g] 의 비트 b = 우선순위 32g+b */
} hbm_t;                   /* 4 + 128 = 132 바이트 */
```

### Part A — set / test / highest (CLZ 두 번)

- `hb_set(h, p)` — leaf 비트를 켜고 **요약 비트도** 켠다 (중복 set 은 무해).
- `hb_test(h, p)` — leaf 비트 하나 읽기.
- `int hb_highest(h)` — set 된 가장 큰 p, 비었으면 `HB_NONE (-1)`. `top == 0` 을 **먼저** 걸러야 한다 (clz(0) UB, DS-B08 참고).
- 테스트: 그룹 경계(31/32), 양 끝(0/1023), 중복 set, 1024개 단일 비트 각각에서 `hb_highest == p` 이고 두 가지 단순 선형 탐색(비트 단위, 워드 단위)과 일치.

### Part B — clear 의 요약 동기화, lowest, prev

- `hb_clear(h, p)` — leaf 비트를 끄고, **그 leaf 가 0 이 되면 요약 비트도 끈다**. 이 한 줄이 이 자료구조의 핵심이다.
- `int hb_lowest(h)` — 가장 작은 p (CTZ 두 번).
- `int hb_prev(h, p)` — **p 보다 엄격히 작은** set 비트 중 가장 큰 것 (p = 0..1024, 그 이상은 1024 로 자름). `for (p = hb_highest(h); p != HB_NONE; p = hb_prev(h, p))` 로 내림차순 순회.
- 테스트: 정해진 경계 + 랜덤 set/clear 60000회를 `bool[1024]` 참조 모델과 **매 연산** 대조(highest/lowest/prev/test + 불변식 검사). 7/8 의 연산을 그룹 경계 비트가 섞인 핫스팟 24칸에 몰아 그룹이 자주 비었다 찼다 하게 하고, 4096번마다 전부 비워서 "완전히 빈" 경계도 지난다.

### Part C — O(1) 준비 큐 (RTOS 스케줄러)

우선순위마다 FIFO 리스트 `q[1024]` (최소 침입형 이중 리스트를 인라인으로 — DS-M01 에서 자세히) + 계층 비트맵. 새 불변식: **비트 p == (q[p] 가 비어 있지 않음)**.

- `rq_ready(rq, t)` — `q[t->prio]` 맨 뒤에 붙이고 비트 set. 이미 ready 면 무시.
- `rq_unready(rq, t)` — 리스트에서 빼고, **그 줄이 비었을 때만** 비트 clear.
- `tcb_t *rq_pick(rq)` — 최고 우선순위 줄의 맨 앞 (빼지 않음). 비었으면 `NULL`.
- 주어짐: `rq_yield` (같은 줄 맨 뒤로 = 라운드 로빈), `rq_set_prio` (우선순위 변경 = 뺐다 다시 넣기).
- 테스트: 선점·FIFO·yield 시나리오 + 태스크 200개로 랜덤 20000 연산(깨우기/블록/yield/우선순위 변경)을 **참조 스케줄러**(전체 선형 스캔, 같은 우선순위는 들어온 순번이 작은 쪽)와 매번 대조하고, 매번 "비트 ↔ 줄" 불변식 1024칸을 전부 검사.

## 2. Problem (English)

> **O(1) highest-priority lookup with a two-level bitmap.**
>
> There are 1024 priority levels `0..1023`; **a larger number means a more urgent priority**. Store the set of active levels in a `uint32_t top` summary word plus `uint32_t leaf[32]`, where bit `b` of `leaf[g]` represents level `32*g + b` and bit `g` of `top` is set iff `leaf[g] != 0`. No dynamic allocation.
>
> 1. Implement `void hb_set(hbm_t*, uint32_t p)`, `void hb_clear(hbm_t*, uint32_t p)`, `bool hb_test(const hbm_t*, uint32_t p)`.
> 2. Implement `int hb_highest(const hbm_t*)` and `int hb_lowest(const hbm_t*)` in O(1) using count-leading-zeros / count-trailing-zeros, returning `-1` when empty. No loops.
> 3. Implement `int hb_prev(const hbm_t*, uint32_t p)`: the largest set level strictly less than `p`, or `-1`.
> 4. Build a scheduler ready queue: one FIFO per level plus the bitmap; `ready(task)`, `unready(task)`, `pick()` must all be O(1). Tasks of equal priority are served FIFO.
> 5. Cross-check against a linear scan on random operation sequences, verifying the summary invariant after every operation.
>
> Example: set {5, 31, 32, 700, 1023} → `highest = 1023`, `lowest = 5`; clear 1023 → `highest = 700`, `top == 0x00200003`; `prev(700) = 32`, `prev(32) = 31`.
>
> Follow-ups: What happens if `clear` forgets to update the summary? Is this safe if an ISR calls `set` while a task runs `highest`? What do you do on a core without a CLZ instruction?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크 문제 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "6/27 passed", exit 1 (비어 있을 때 -1/NULL 이 답인 검사만 우연히 통과) — TODO 9곳을 채워 27/27
```

TODO: `hb_set`, `hb_test`, `hb_highest` (Part A) / `hb_clear`, `hb_lowest`, `hb_prev` (Part B) / `rq_ready`, `rq_unready`, `rq_pick` (Part C). 불변식 검사 `hb_check()`·`rq_check()`, 두 선형 탐색, 참조 모델, 리스트 도우미, `fls32()`/`ffs32()` 는 주어진다.

## 4. 힌트

<details>
<summary>힌트 1 — 인덱스 쪼개기는 DS-B08 과 같다</summary>

`g = p >> 5`, `b = p & 31`. 비트맵이 2층이 된 것뿐이라 set 은 "leaf 에 `1u << b`, top 에 `1u << g`" 두 줄이다. 마스크는 반드시 `1u` — `1 << 31` 은 `int` 오버플로라 UB (C11 6.5.7p4).

</details>

<details>
<summary>힌트 2 — highest 는 "그룹 먼저, 그다음 비트"</summary>

top 에서 가장 높은 1비트 = 1이 있는 가장 높은 그룹. 그 leaf 는 불변식 덕분에 **반드시 0이 아니므로** 두 번째 CLZ 는 안전하다. 그래서 `top == 0` 검사 하나만 있으면 된다. `fls32(x) = 31 - __builtin_clz(x)`.

</details>

<details>
<summary>힌트 3 — clear 는 비대칭이다</summary>

set 은 요약 비트를 무조건 켜도 되지만, clear 는 요약 비트를 무조건 끄면 안 된다 — 같은 그룹의 다른 31비트가 남아 있을 수 있다. "leaf 가 0 이 됐을 때만" 끈다.

</details>

<details>
<summary>힌트 4 — prev 는 두 단계 마스크</summary>

후보는 `q = p - 1` 이하. ① q 의 그룹 leaf 에서 비트 0..b(q) 만 남긴다: `0xFFFFFFFFu >> (31 - b)` (시프트 양 0..31). ② 거기서 못 찾으면 top 에서 **g 보다 아래** 그룹만: `(1u << g) - 1u` (g = 0 이면 0). ③ 그 그룹은 불변식상 비어 있지 않으니 그냥 fls. `(1u << (b + 1)) - 1` 로 쓰면 b = 31 에서 시프트 32 → UB.

</details>

<details>
<summary>힌트 5 — 준비 큐의 비트 = "줄이 비어 있지 않음"</summary>

비트는 태스크가 아니라 **줄**의 상태다. 같은 우선순위에 태스크가 셋이면 하나가 블록돼도 비트는 켜진 채여야 한다. 그래서 unready 는 `list_del` 후 `list_empty(&q[prio])` 일 때만 `hb_clear`. pick 은 `(tcb_t *)q[p].next` — `link` 가 `tcb_t` 의 첫 멤버라 캐스트가 유효하다 (C11 6.7.2.1p15).

</details>

## 5. 풀이

### 불변식 두 개

| 이름 | 내용 | 깨지면 |
|---|---|---|
| I1 (요약) | 모든 g 에 대해 `top` 의 비트 g == (`leaf[g] != 0`) | 비트 g 가 켜졌는데 leaf 가 0 → `clz(0)` UB. 꺼졌는데 leaf 가 0 아님 → 그 그룹의 태스크가 **영원히 안 뽑힘** |
| I2 (준비 큐) | 모든 p 에 대해 비트 p == (`q[p]` 비어 있지 않음) | 켜졌는데 줄이 빔 → `rq_pick` 이 센티넬을 태스크로 착각. 꺼졌는데 줄에 태스크 → 기아(starvation) |

`hb_check()` 는 I1 을 32번 비교로, `rq_check()` 는 I2 를 1024칸 + 줄 안의 모든 노드(prio 일치, `next->prev == self`, 개수)로 검사한다. 테스트는 이것을 **매 연산 뒤에** 돌린다 — 결과값 대조만으로는 "지금은 우연히 맞지만 구조가 틀어진" 상태를 놓친다.

### Part A — set / highest

```c
static void hb_set(hbm_t *h, uint32_t p) {
    h->leaf[GRP(p)] |= 1u << BIT(p);
    h->top |= 1u << GRP(p);
}
static int hb_highest(const hbm_t *h) {
    if (h->top == 0) return HB_NONE;
    uint32_t g = fls32(h->top);                  /* 1 이 있는 가장 높은 그룹 */
    return (int)(g * 32u + fls32(h->leaf[g]));   /* I1 덕분에 leaf[g] != 0 */
}
```

그림의 예 {5, 31, 32, 700, 1023} 를 손으로 따라가면 (실제 실행으로 확인한 값):

| 단계 | 값 | 계산 |
|---|---|---|
| top | `0x80200003` | 그룹 31(1023), 21(700), 1(32), 0(5, 31) |
| clz(top) | 0 | 비트 31 이 1 |
| g | 31 | `31 - 0` |
| leaf[31] | `0x80000000` | clz = 0 → b = 31 |
| 답 | 1023 | `31·32 + 31` |

**정말 O(1) 인가?** 로드 2번, CLZ 2번, 시프트·덧셈 몇 개, 분기 1개(빈 검사). 입력 데이터에 따라 반복 횟수가 달라지는 루프가 **없다**. 이게 RTOS 에서 말하는 "결정적(deterministic)" — 평균만 빠른 게 아니라 최악이 평균과 같다. 엄밀히는 단계 수가 ⌈log₃₂ N⌉ 이라 N 이 고정(1024)일 때 상수다. N = 32768 이면 3단계 (연습 3), 2²⁰ 이면 4단계.

Cortex-M4 에서 실제로 몇 명령이 되는지 `demo_codegen.c` 로 확인했다 (Apple clang 21, `-O2`, 지시어 줄 제거):

```bash
cc --target=thumbv7em-none-eabi -mcpu=cortex-m4    -ffreestanding -O2 -S -o - demo_codegen.c
cc --target=thumbv6m-none-eabi  -mcpu=cortex-m0plus -ffreestanding -O2 -S -o - demo_codegen.c
```

```text
== cortex-m4
hb_highest:                             @ @hb_highest
@ %bb.0:
	ldr	r1, [r0]
	cmp	r1, #0
	itt	eq
	moveq.w	r0, #-1
	bxeq	lr
.LBB0_1:
	clz	r1, r1
	eor	r1, r1, #31
	add.w	r0, r0, r1, lsl #2
	ldr	r0, [r0, #4]
	lsls	r2, r1, #5
	add.w	r1, r2, #31
	clz	r0, r0
	subs	r0, r1, r0
	bx	lr
```

```text
== cortex-m0+
hb_highest:                             @ @hb_highest
@ %bb.0:
	push	{r4, r5, r7, lr}
	mov	r4, r0
	ldr	r0, [r0]
	cmp	r0, #0
	beq	.LBB0_3
@ %bb.1:
	bl	__clzsi2
	movs	r1, #31
	eors	r1, r0
	lsls	r5, r1, #5
	adds	r5, #31
	lsls	r0, r1, #2
	adds	r0, r4, r0
	ldr	r0, [r0, #4]
	cmp	r0, #0
	beq	.LBB0_4
@ %bb.2:
	bl	__clzsi2
	subs	r0, r5, r0
	pop	{r4, r5, r7, pc}
.LBB0_3:
	movs	r0, #0
	mvns	r0, r0
	pop	{r4, r5, r7, pc}
.LBB0_4:
	movs	r0, #32
	subs	r0, r5, r0
	pop	{r4, r5, r7, pc}
```

- M4: 빈 검사(`itt eq` 조건 복귀) 말고는 분기 없는 14명령, `clz` 두 개. `31 - clz(x)` 가 `eor #31` 로 바뀐 것은 clz 결과가 0..31 이라 `31 - x == 31 ^ x` 이기 때문이다 (x ≠ 0 가정을 컴파일러가 이용).
- M0+ (ARMv6-M): **CLZ 명령이 없어** `__clzsi2` 런타임 함수 호출 두 번. 여전히 상수 시간이지만 함수 호출에 비트 탐색까지 수십 사이클 — 그래서 µC/OS-II 는 256바이트 표를 썼다 (연습 2, 면접 Q5). 두 번째 호출 앞에는 컴파일러가 0 검사(`.LBB0_4`: 결과를 32 로)를 넣었다 — `__clzsi2` 가 0 입력을 받지 않으니 직접 거른 것으로 보이며(첫 호출은 `top == 0` 검사 뒤라 생략), C 코드 입장에서 `clz(0)` 은 여전히 UB 라 이 동작에 기대면 안 된다.

### Part B — clear 와 요약 동기화

```c
static void hb_clear(hbm_t *h, uint32_t p) {
    uint32_t g = GRP(p);
    h->leaf[g] &= ~(1u << BIT(p));
    if (h->leaf[g] == 0) h->top &= ~(1u << g);   /* 그룹의 마지막 비트였을 때만 */
}
```

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m09a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">clear(1023): leaf[31] 이 0 이 되므로 top 비트 31 도 끈다</text>
  <text x="20" y="48" fill="currentColor" font-weight="bold">전</text>
  <text x="390" y="48" fill="currentColor" font-weight="bold">후</text>
  <g stroke="currentColor" class="f-mut">
    <rect x="20" y="60" width="330" height="34" rx="4"/><rect x="20" y="110" width="160" height="34" rx="4"/><rect x="190" y="110" width="160" height="34" rx="4"/>
    <rect x="390" y="60" width="330" height="34" rx="4"/><rect x="560" y="110" width="160" height="34" rx="4"/>
  </g>
  <rect x="390" y="110" width="160" height="34" rx="4" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="30" y="82">top = 0x80200003   clz = 0  → g = 31</text>
    <text x="28" y="132">leaf[31]=0x80000000</text><text x="198" y="132">leaf[21]=0x10000000</text>
    <text x="400" y="82">top = 0x00200003   clz = 10 → g = 21</text>
    <text x="398" y="132">leaf[31]=0x00000000</text><text x="568" y="132">leaf[21]=0x10000000</text>
  </g>
  <g fill="currentColor"><text x="20" y="172">highest: clz(leaf[31]) = 0 → b = 31 → 1023</text>
    <text x="390" y="172">highest: clz(leaf[21]) = 3 → b = 28 → p = 700</text></g>
  <path d="M355 100 L385 100" stroke="currentColor" stroke-width="2" marker-end="url(#m09a)"/>
  <rect x="20" y="192" width="700" height="96" rx="6" fill="none" stroke="currentColor" stroke-dasharray="6 4"/>
  <g fill="currentColor">
    <text x="34" y="214" font-weight="bold">요약 비트를 안 끄면 (흔한 버그)</text>
    <text x="34" y="236">top 비트 31 이 남아 g = 31 → clz(leaf[31] = 0) → C 에서는 UB.</text>
    <text x="34" y="256">위 M4 코드라면 CLZ 명령이 0 에 32 를 주므로 31·32 + 31 − 32 = 991 이 나온다.</text>
    <text x="34" y="276">991 은 set 된 적 없는 우선순위 → 빈 줄의 센티넬을 TCB 로 착각해 실행 → 크래시.</text>
  </g>
</svg>
```

**왜 set 은 무조건이고 clear 는 조건부인가**: 요약은 "그룹 안 32비트의 OR" 이다. OR 에 1을 더하면 결과는 무조건 1 이지만, 1 하나를 빼면 결과는 나머지에 달렸다. 그래서 clear 만 leaf 를 다시 봐야 한다 — 비용은 비교 1개라 여전히 O(1). µC/OS-II 의 고전 코드도 정확히 이 모양이다: `OSRdyTbl[y] &= ~bitx; if (OSRdyTbl[y] == 0) OSRdyGrp &= ~bity;`.

**lowest** 는 highest 의 거울 — `fls32` 대신 `ffs32` (CTZ). Cortex-M3+ 에서 CTZ 는 `RBIT` + `CLZ` 두 명령이다 (DS-B08 참고).

**prev — 두 단계 마스크**. 예: 위 "후" 상태 {5, 31, 32, 700} 에서 `prev(700)`:

| 단계 | 계산 | 값 |
|---|---|---|
| q, g, b | `q = 699`, `g = 21`, `b = 27` | |
| ① leaf 마스크 | `leaf[21] & (0xFFFFFFFF >> (31 − 27))` = `0x10000000 & 0x0FFFFFFF` | 0 → 이 그룹엔 없음 |
| ② top 마스크 | `top & ((1u << 21) − 1)` = `0x00200003 & 0x001FFFFF` | `0x3` → 그룹 1, 0 |
| ③ | `g = fls(0x3) = 1`, `fls(leaf[1] = 1) = 0` | **32** |

경계 세 가지가 함정이다:

| 상황 | 잘못된 마스크 | 문제 | 이 코드 |
|---|---|---|---|
| b = 31 (그룹 전체) | `(1u << (b + 1)) − 1` | 시프트 32 → UB (C11 6.5.7p3) | `0xFFFFFFFFu >> (31 − b)` — 시프트 0 |
| g = 0 (아래 그룹 없음) | — | `(1u << 0) − 1 = 0` 이라 자연히 "없음" | 그대로 |
| p = 0 / p > 1024 | `q = p − 1` 이 0xFFFFFFFF | 배열 밖 그룹 | 앞에서 `HB_NONE` / 1024 로 자름 |

랜덤 대조 실제 출력:

```text
[B] random 60000 set/clear vs bool[1024] reference: match (sets=30156 clears=29830, summary-bit clears=309, full drains=14)
```

요약 비트가 실제로 꺼진 순간이 309번, 완전히 빈 상태까지 비운 것이 14번 — 동기화 코드가 "통과만 한" 게 아니라 실제로 실행됐다는 증거다. 일부러 버그를 넣어 보면 (`-O1 -fsanitize=undefined` 빌드에서: 요약 동기화 줄 삭제 → 15/27, 동기화를 무조건 끄기로 → 24/27, prev 의 top 마스크를 `(2u << g) − 1` 로 → 24/27, `rq_ready` 가 줄 맨 앞에 붙이기 → 22/27) 모두 잡힌다.

### 단순 선형 탐색과 대조 — 측정

`demo_bench.c` 는 세 방법을 같은 비트맵 256개에 돌린다: **bits** (p = 1023 부터 비트 하나씩), **words** (leaf 31 부터 워드 하나씩, 0 아닌 첫 워드에 CLZ — 요약 없는 DS-B08 방식), **2-level** (이 레슨). 세 함수 모두 `noinline` 으로 호출 비용을 맞췄고, 결과 합이 서로 같은지도 확인한다.

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
```

```text
scenario             | bits: ns  avg/max bits     | words: ns  avg/max words   | 2-level: ns  words
1 bit, uniform p     |  130.1    520.1 / 1019     |   5.08    16.8 / 32        |   0.69  2
1 bit, p<32 (idle)   |  246.9   1008.5 / 1024     |   3.77    32.0 / 32        |   0.68  2
dense 50%            |    1.7      2.1 /    8     |   0.89     1.0 /  1        |   0.69  2
idle + 3 in 0..127   |  238.8    930.6 / 1010     |   4.83    29.6 / 32        |   0.69  2
```

이 Mac (Apple Silicon, `-O2`) 의 값이다. 절대 ns 가 아니라 **비율과 흔들림**을 보라.

| 관찰 | 의미 |
|---|---|
| 2-level 은 네 시나리오 모두 0.68~0.69 ns, 읽는 워드 항상 2 | 데이터와 무관 = 결정적. WCET = 평균 |
| bits 는 1.7 ns ~ 247 ns (약 145배 차이) | 최악은 "idle 만 ready" — RTOS 에서 **가장 흔한** 상태가 최악이다 |
| words 는 최악 32워드, 0.89 ~ 5.08 ns | 요약 없이도 bits 보다 수십 배 낫지만, 여전히 분포에 따라 변한다 |
| words: idle(32워드) 3.77 ns < uniform(평균 16.8워드) 5.08 ns | 일을 덜 하는데 더 느림 — uniform 은 루프 탈출 위치가 매번 달라 **분기 예측 실패**. 비순차 코어의 함정이고, Cortex-M 같은 순차 코어에서는 워드 수에 비례한다 |
| "idle + 낮은 대역 3개" 도 bits 930 / words 29.6 | 실제 시스템처럼 높은 우선순위가 대부분 블록 상태면 선형 탐색은 거의 끝까지 간다 |

### Part C — O(1) 준비 큐

```svg
<svg viewBox="0 0 740 296" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m09b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">준비 큐 = 계층 비트맵 (어느 줄이 비어 있지 않나) + 우선순위별 FIFO</text>
  <g stroke="currentColor" fill="none"><rect x="20" y="40" width="150" height="200" rx="4"/></g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="30" y="60">hbm_t map</text>
    <text x="30" y="90">bit 900 = 1</text><text x="30" y="150">bit 100 = 1</text><text x="30" y="210">bit 0   = 1</text>
    <text x="30" y="120">(그 외 0)</text>
  </g>
  <g stroke="currentColor" class="f-mut">
    <rect x="200" y="74" width="70" height="26" rx="4"/><rect x="200" y="134" width="70" height="26" rx="4"/><rect x="200" y="194" width="70" height="26" rx="4"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">
    <text x="235" y="92">q[900]</text><text x="235" y="152">q[100]</text><text x="235" y="212">q[0]</text>
  </g>
  <rect x="300" y="74" width="80" height="26" rx="4" class="f-acc" stroke="currentColor"/>
  <g stroke="currentColor" class="f-ok">
    <rect x="300" y="134" width="80" height="26" rx="4"/><rect x="410" y="134" width="80" height="26" rx="4"/><rect x="520" y="134" width="80" height="26" rx="4"/>
    <rect x="300" y="194" width="80" height="26" rx="4"/>
  </g>
  <g fill="currentColor" text-anchor="middle"><text x="340" y="92">T3</text><text x="340" y="152">T1</text><text x="450" y="152">T2</text><text x="560" y="152">T4</text><text x="340" y="212">idle</text></g>
  <g stroke="currentColor" fill="none" marker-end="url(#m09b)">
    <path d="M170 87 L198 87"/><path d="M170 147 L198 147"/><path d="M170 207 L198 207"/>
    <path d="M270 87 L298 87"/><path d="M270 147 L298 147"/><path d="M380 147 L408 147"/><path d="M490 147 L518 147"/><path d="M270 207 L298 207"/>
  </g>
  <g fill="currentColor">
    <text x="420" y="92">← pick = q[hb_highest()] 의 맨 앞 = T3</text>
    <text x="610" y="152">FIFO (맨 뒤에 붙임)</text>
    <text x="20" y="264">T3 블록 → q[900] 이 비어 비트 900 clear → pick = T1.</text>
    <text x="20" y="284">T1 블록 → q[100] 에 T2, T4 남음 → 비트 100 은 켜진 채 유지.</text>
  </g>
</svg>
```

```c
static void rq_unready(runq_t *rq, tcb_t *t) {
    if (!t->ready) return;
    list_del(&t->link);
    if (list_empty(&rq->q[t->prio])) hb_clear(&rq->map, t->prio);   /* 줄이 빌 때만 */
    t->ready = false;
}
static tcb_t *rq_pick(const runq_t *rq) {
    int p = hb_highest(&rq->map);
    return p == HB_NONE ? NULL : (tcb_t *)rq->q[p].next;
}
```

모든 연산이 O(1): 리스트 붙이기/떼기는 포인터 4개, 비트맵은 위에서 본 대로. 태스크 수와도, 우선순위 분포와도 무관하다.

**메모리**: 비트맵 132바이트 + 센티넬 1024개. 센티넬은 포인터 2개라 Cortex-M(32비트 포인터)에서 1024 × 8 = **8KB**, 이 Mac(64비트)에서는 16KB. 우선순위 1024개가 정말 다 필요한가는 설계 문제다 — FreeRTOS 가 우선순위 수 `configMAX_PRIORITIES` 를 작게 잡으라고 권하는 이유가 이 배열 크기다 (우선순위마다 리스트 하나). 비트맵 자체의 오버헤드는 우선순위당 1비트 + 요약 1/32 비트 (약 3%).

**테스트 시나리오** (`main` 의 Part C 첫 블록):

| 단계 | 줄 상태 (앞 → 뒤) | pick | 확인 |
|---|---|---|---|
| ready T0(0), T1(100), T2(100) | q[100]: T1 T2 / q[0]: T0 | T1 | 같은 우선순위는 FIFO |
| ready T3(900) | + q[900]: T3 | T3 | 선점 |
| unready T3 | q[900] 빔 | T1 | 비트 900 꺼짐 |
| yield T1 | q[100]: T2 T1 | T2 | 라운드 로빈 |
| ready T4(100), yield T2 | q[100]: T1 T4 T2 | T1 | |
| unready T1, T4, T2 | q[100] 빔 | T0 | 비트 100 꺼짐, idle |
| ready T0 (중복) | 그대로 | — | 이중 삽입 안 됨 (`rq_check` 개수 1) |

랜덤 대조 실제 출력:

```text
[C] random 20000 ready/block/yield/set_prio vs reference scheduler: match (final ready=134)
27/27 passed
```

참조 스케줄러는 200개 태스크를 전부 훑어 "prio 최대, 같으면 seq 최소" 를 고른다 — 비트맵과 리스트를 전혀 쓰지 않는 독립 구현이라 둘이 같은 버그를 공유할 수 없다. 우선순위는 8대역 × {0, 31, 32, 127} 의 32가지만 써서 같은 줄에 태스크가 여러 개 쌓이고(FIFO 검증), 그룹 경계(31/32)와 1023 도 지나게 했다.

**FreeRTOS 와 비교**: 구조가 같다. `configUSE_PORT_OPTIMISED_TASK_SELECTION = 1` 이면 `tasks.c` 의 `uxTopReadyPriority` 가 **우선순위 비트맵 자체**가 되고 (`taskRECORD_READY_PRIORITY` → 포트의 `portRECORD_READY_PRIORITY` 가 `|= 1UL << prio`), 선택은 `portGET_HIGHEST_PRIORITY` = `31 - clz`, 블록할 때는 `taskRESET_READY_PRIORITY` 가 **그 우선순위 리스트 길이가 0 일 때만** 비트를 끈다 — 이 레슨의 I2 와 똑같은 규칙. 다만 1단계(32비트 한 워드)라 `configMAX_PRIORITIES` 가 32 이하로 제한된다. 1024개가 필요하면 이 레슨처럼 한 층을 더 얹으면 된다. (같은 우선순위 순환은 FreeRTOS 가 노드를 옮기지 않고 리스트의 `pxIndex` 를 돌리는 `listGET_OWNER_OF_NEXT_ENTRY` 로 한다는 점이 다르다.)

**펌웨어 연결**:

- **임계 구역**: `hb_set` 은 두 워드를 따로 쓴다. 중간에 ISR 이 `hb_highest` 를 부르면 I1 이 깨진 순간을 본다 → 면접 Q4. RTOS 는 준비 리스트 조작 전체를 임계 구역(또는 스케줄러 정지) 안에서 하므로 비트맵도 그 안에 넣는다. 임계 구역 길이가 **상수**라는 점이 인터럽트 지연(latency) 상한에 직접 들어간다.
- **SSD/FTL**: 블록 1024개짜리 free 비트맵(비트 1 = 빈 블록)에도 그대로 쓴다 — "가장 낮은 빈 블록"을 CTZ 두 번에 (연습 4). 다이(die)·플레인 별로 "빈 블록 있는 그룹" 요약을 두는 것과 같은 발상.
- **인터럽트 컨트롤러**: GIC/NVIC 도 내부적으로 "pending & enabled 중 최고 우선순위" 를 하드웨어로 고른다 — 소프트웨어 인터럽트 디스패처(예: 공유 IRQ 라인 뒤의 소스 32개 × 32)를 짤 때 같은 구조가 나온다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 27/27 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o solution_asan && ./solution_asan   # 경고 없이 27/27
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| clear 에서 요약 비트를 안 끔 | I1 깨짐 → 다음 highest 가 `clz(0)` (UB, ARM 에서는 991 같은 유령 우선순위) | `if (leaf[g] == 0) top &= ~(1u << g);` |
| clear 에서 요약 비트를 무조건 끔 | 같은 그룹의 다른 우선순위가 안 보임 → 그 태스크들 기아 | leaf 가 0 일 때만 |
| set 에서 요약 비트를 안 켬 | 그룹이 처음 채워질 때 highest 가 못 봄 | 두 줄 모두 |
| `hb_highest` 에서 `top == 0` 검사 누락 | 빈 비트맵에서 `clz(0)` UB — 명령마다 결과가 다르고(ARM CLZ 는 32, x86 BSR 은 목적지 미정) 컴파일러는 0 이 아니라고 가정해 최적화한다 | 빈 검사 먼저, `HB_NONE` 반환 |
| `1 << 31`, `1 << g` (int) | g = 31 에서 부호 있는 오버플로 UB | `1u << g` |
| 범위 마스크 `(1u << (b + 1)) - 1` | b = 31 이면 시프트 32 → UB (x86 은 시프트 양을 mod 32 해서 마스크가 0 이 된다) | `0xFFFFFFFFu >> (31u - b)` |
| 관례 혼동: "큰 번호가 급함" 인데 CTZ 사용 | 가장 **덜** 급한 태스크(idle)가 뽑힘. 테스트에 우선순위 2개 이상 섞지 않으면 안 보인다 | 관례를 주석 첫 줄에 고정, 큰 번호 = CLZ |
| 준비 큐에서 unready 할 때 무조건 `hb_clear` | 같은 우선순위의 나머지 태스크가 안 뽑힘 | `list_empty` 일 때만 |
| ready 상태 확인 없이 `rq_ready` 두 번 | 같은 노드를 두 번 `list_add_tail` → 리스트 순환 꼬임 | `if (t->ready) return;` |
| ISR 과 태스크가 잠금 없이 공유 | 두 워드 갱신 사이에 끼어들면 I1 이 잠깐 깨진 상태를 읽음 | 임계 구역 안에서만 갱신·탐색 (Q4) |

## 8. 면접 Q&A

<details>
<summary>Q1. "O(1)" 이라고 했는데 정확히 무엇이 상수인가요? 우선순위가 100만 개면?</summary>

단계 수가 ⌈log_w N⌉ (w = 워드 비트 수) 이고, 단계마다 로드 1 + CLZ 1 이다. N = 1024, w = 32 면 2단계로 고정이라 상수. N = 2²⁰ 이면 32비트로 4단계, 64비트 워드면 ⌈20/6⌉ = 4단계. 즉 N 에 대해 O(log_w N) 이지만 w 가 크고 N 이 설계 시점에 고정이라 실무에서는 "상수, 그리고 **데이터에 무관**" 이 핵심이다. 측정에서도 2-level 은 모든 분포에서 0.68~0.69 ns 였고 비트 루프는 1.7~247 ns 로 흔들렸다. 꼬리질문 "메모리는?" → 요약 층들은 N/32 + N/1024 + … ≈ N/31 비트, 즉 leaf 대비 약 3% 추가.

</details>

<details>
<summary>Q2. FreeRTOS 는 최고 우선순위 태스크를 어떻게 고르나요?</summary>

두 방식이 있다. ① 일반(generic) 방식: `uxTopReadyPriority` 에 "ready 가 있을 수 있는 가장 높은 우선순위 번호" 를 기억해 두고, 선택할 때 그 번호부터 `pxReadyTasksLists[]` 가 빌 때마다 하나씩 내려간다 — 루프, 우선순위 수에 비례. ② `configUSE_PORT_OPTIMISED_TASK_SELECTION = 1`: `uxTopReadyPriority` 를 비트맵으로 쓰고, 포트의 `portGET_HIGHEST_PRIORITY` 가 `31 - clz` 로 한 번에 구한다 (Cortex-M3/M4 GCC 포트는 `clz` 인라인 어셈블리). 그래서 이 모드는 `configMAX_PRIORITIES ≤ 32` 제한이 있다. FreeRTOS 는 **큰 번호가 높은 우선순위**(idle = 0)라 CLZ 와 바로 맞는다. 블록 시 `taskRESET_READY_PRIORITY` 는 그 우선순위의 리스트 길이가 0 일 때만 비트를 끈다 — I2 와 같은 규칙.

</details>

<details>
<summary>Q3. "0 이 가장 급한" 관례(Linux RT, µC/OS, Zephyr)에서는 어떻게 바꾸나요?</summary>

세 가지. ① 저장은 그대로, **CTZ** 로 가장 작은 p (Cortex-M 에서 `RBIT`+`CLZ` 2명령). ② **뒤집어 저장**: p 를 그룹 `31 − (p>>5)` 의 비트 `31 − (p&31)` 에 두면 가장 작은 p 가 가장 높은 비트가 되어 CLZ 한 명령씩 — µC/OS-III 의 `OS_PrioGetHighest()` 가 `CPU_CntLeadZeros()` 로 이렇게 한다. ③ 저장 시 `p' = 1023 − p` 로 바꿔 넣기 (②와 같은 것). 연습 1 에서 ①과 ②가 랜덤 40000 연산에서 같은 답을 내는지 확인한다. Linux RT 스케줄러는 `struct rt_prio_array` 에 비트맵 + 우선순위별 `list_head` 를 두고 `pick_next_rt_entity()` 가 `sched_find_first_bit()` 로 가장 낮은 번호를 고른다 — 우선순위 100개라 64비트 워드 2개를 차례로 보는 1단계로 충분하다. 재미있는 점은 비트맵 크기를 `MAX_RT_PRIO + 1` 로 잡고 맨 끝 비트를 항상 켜 둔 **구분자(delimiter)** 로 쓴다는 것 — 탐색이 절대 0 워드에서 끝나지 않으니 빈 검사가 필요 없다 (센티넬 기법). Zephyr 도 `CONFIG_SCHED_MULTIQ` 를 켜면 우선순위별 리스트 + 비트맵 방식이 된다.

</details>

<details>
<summary>Q4. ISR 이 태스크를 깨우면서(hb_set) 동시에 태스크가 hb_highest 를 부르면 안전한가요?</summary>

아니다. 갱신이 두 워드라 원자적이지 않다. set 순서가 leaf → top 이면 끼어든 읽기는 새 비트를 못 볼 뿐이지만(조금 늦게 깨운 것과 같음), **clear** 는 leaf 가 0 이 된 뒤 top 을 끄기 전 순간이 있어 그때 읽으면 `clz(0)` → 유령 우선순위. 순서를 아무리 바꿔도 "읽기가 top 과 leaf 를 서로 다른 시점에 본다" 는 문제는 남는다. 해법: ① 갱신과 탐색을 모두 임계 구역 안에서 (RTOS 가 실제로 하는 것 — 준비 리스트 조작 자체가 임계 구역 안이다). 임계 구역 길이가 상수라 인터럽트 지연 상한이 명확하다. ② ISR 은 "깨울 태스크" 를 큐에 넣기만 하고 비트맵은 스케줄러 문맥만 만진다 (FreeRTOS 의 `xPendingReadyList` 가 스케줄러가 정지돼 있을 때 이렇게 미룬다). ③ SMP 라면 코어별 준비 큐 + 스핀락 (Linux 는 CPU 마다 `rq` 가 있다). 원자 연산 `atomic_fetch_or` 로 각 워드를 갱신해도 두 워드 사이의 일관성은 안 생긴다는 점을 말하면 좋다.

</details>

<details>
<summary>Q5. Cortex-M0/M0+ 처럼 CLZ 명령이 없으면?</summary>

ARMv6-M (M0, M0+) 과 ARMv8-M Baseline (M23) 에는 CLZ 가 없다. `__builtin_clz` 를 쓰면 위 실제 어셈블리처럼 `__clzsi2` 라이브러리 호출이 된다 — 상수 시간이긴 하나 수십 사이클. 대안: ① 이진 탐색 5단계 (16/8/4/2/1비트 마스크 검사) ② 바이트 표 256칸 + 바이트 고르기 — µC/OS-II 가 우선순위 64개를 `OSRdyGrp` (8비트 요약) + `OSRdyTbl[8]` + 256바이트 `OSUnMapTbl[]` 로 푼 방식이 바로 **8×8 계층 비트맵 + 표 조회**다. ③ de Bruijn 곱셈 (DS-B08 Q3, 단 곱셈기가 느린 M0 구성에서는 주의). 연습 2 에서 ①②를 `31 - __builtin_clz` 와 100만 개 값으로 대조한다. 핵심은 "명령이 없어도 **상수 시간**은 유지된다" — 상수가 커질 뿐.

</details>

<details>
<summary>Q6. 그냥 이진 힙(DS-B12)이나 정렬 리스트를 쓰면 안 되나요?</summary>

힙은 **임의의 키**(만료 시각 같은 32비트 값)를 다루고 O(log n) 이다. 비트맵은 **키 범위가 작고 고정**(우선순위 0..1023)일 때만 쓸 수 있는 대신 O(1) 이고 같은 키의 원소 여러 개는 키별 리스트가 맡는다 — 일종의 "버킷 큐(bucket queue)". 스케줄러 우선순위는 범위가 작고 고정이라 비트맵이 맞고, 타이머 만료 시각은 범위가 커서 힙이나 타이머 휠(DS-M06)이 맞다. 정렬 리스트 하나(모든 ready 태스크를 우선순위순으로)는 pick 이 O(1) 이지만 삽입이 O(n) 이라 ISR 이 태스크를 깨울 때 지연이 태스크 수에 비례한다.

</details>

<details>
<summary>Q7. 64비트 코어(Cortex-A, RISC-V RV64)라면 구조를 어떻게 바꾸나요?</summary>

`uint64_t` 와 `__builtin_clzll` 로 64 × 64 = 4096 우선순위가 2단계에 들어간다. 1024개만 필요하면 64비트 leaf 16개 + 요약 16비트. AArch64 는 `clz` 가 64비트 레지스터에 바로 되고, RISC-V 는 Zbb 확장의 `clz`/`ctz` 명령이 있어야 하드웨어 1명령이다 (없으면 M0 와 같은 처지). Linux 의 `DECLARE_BITMAP` 이 `unsigned long` 배열인 것도 같은 이유 — 워드 폭을 그 아키텍처의 레지스터 폭에 맞춘다.

</details>

## 9. 추가 연습

### 연습 1 — 0 이 가장 급한 관례: CTZ 버전 vs 뒤집어 저장 + CLZ

Linux RT · µC/OS 처럼 **작은 번호가 급한** 1024 우선순위 비트맵을 두 가지로 구현하고 같은 답을 내는지 대조하라. ① 비트 p 를 그대로 두고 CTZ ② 그룹 `31 − (p>>5)`, 비트 `31 − (p&31)` 에 저장하고 CLZ.

<details>
<summary>답 보기</summary>

②에서 `clz(top)` 은 곧 `p >> 5` 이고, `clz(leaf[31 − clz(top)])` 는 곧 `p & 31` 이다 — "31 − 위치" 를 저장할 때 미리 했으니 꺼낼 때는 뺄셈도 없다. p = 0 만 set 하면 `top == 0x80000000`, `leaf[31] == 0x80000000`. Cortex-M3+ 에서 ①은 단계마다 RBIT+CLZ, ②는 CLZ 하나. 랜덤 set/clear 40000번마다 두 버전과 `bool` 참조의 "가장 작은 set 번호" 를 비교한다.

</details>

### 연습 2 — CLZ 없는 코어용 fls32

`fls32_bsearch` (이진 탐색 5단계) 와 `fls32_table` (256칸 바이트 표) 를 만들고, `31 − __builtin_clz` 와 모든 단일 비트·"그 아래 전부 1" 경계값·랜덤 100만 개에서 대조하라. 그걸로 `hb_highest_portable` 을 만들어 원래 버전과 비교하라.

<details>
<summary>답 보기</summary>

이진 탐색: `if (x & 0xFFFF0000) { x >>= 16; r += 16; }` 다음 8, 4, 2, 1. 분기 5개, 표 없음. 표: 상위 바이트부터 0 이 아닌 바이트를 골라 `8k + fls8[byte]` — 표 256바이트를 플래시에 둔다 (µC/OS-II `OSUnMapTbl` 은 같은 크기로 가장 **낮은** 비트를 준다). 랜덤 값은 `xorshift32() >> (i & 31)` 로 가장 높은 비트 위치가 고르게 퍼지게 한다 — 그냥 랜덤 32비트는 절반이 비트 31 이라 낮은 위치를 거의 시험하지 못한다.

</details>

### 연습 3 — 3단계 32768 우선순위

`l0` (32비트) → `mid[32]` → `leaf[1024]` 로 32 × 32 × 32 = 32768 을 담아라. clear 는 아래가 비었을 때만 한 층씩 위로 올라가며 끈다. 매 연산 후 두 요약 층을 모두 검사하라.

<details>
<summary>답 보기</summary>

clear 는 "leaf 워드가 0 이 아니면 return → mid 비트 끄기 → mid 가 0 이 아니면 return → l0 비트 끄기". highest 는 CLZ 3번. 크기는 4 + 128 + 4096 = 4228 바이트. 참조는 leaf 1024워드를 위에서부터 훑는 선형 탐색, 끝에 전부 지워 `l0 == 0` 인지 확인. 실제 출력:

```text
[3] 3-level 32768: random 20000 ops match (mid-level summary clears=621), sizeof=4228 bytes
```

중간 층 요약 비트가 621번 꺼졌다 — 2단 전파가 실제로 실행됐다.

</details>

### 연습 4 — O(1) 블록 할당기 (비트 1 = 빈 블록)

1024 블록 free 비트맵으로 "가장 낮은 번호의 빈 블록" 을 주는 `ba_alloc()` 과 `ba_free(p)` 를 계층 비트맵으로 만들어라. 범위 밖·이중 해제는 거절. 선형 first-free 참조와 랜덤 50000번 대조.

<details>
<summary>답 보기</summary>

의미를 뒤집어 **비트 1 = 빈 블록** 으로 두면 요약 비트 = "이 그룹에 빈 블록이 있음" 이 되어 할당 = `lowest` (CTZ 두 번) + `clear`, 해제 = `set`. 초기화는 모든 비트 1 (`top = 0xFFFFFFFF`). 비트 1 = 사용 중으로 두면 "0 을 찾기" 가 되어 요약이 "그룹에 0 이 하나라도 있음" 이라는 AND 성질을 가져야 해서 더 헷갈린다 — **찾고 싶은 쪽을 1로** 두는 것이 요령. 이중 해제는 "이미 1 인 비트를 set" 이므로 set 전에 test. 실제 출력:

```text
[4] block allocator: random 50000 alloc/free vs linear first-free: match (in use at end=94)
14/14 passed
```

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 14/14 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o practice_asan && ./practice_asan
```

## 10. 핵심 정리

- 관례 고정: 이 레슨은 **큰 번호 = 급함** → 최고 = 가장 높은 set 비트 = `31 − clz`. 반대 관례면 CTZ 또는 뒤집어 저장 + CLZ.
- 구조: `top` (요약 32비트) + `leaf[32]`, `p = 32g + b`, 132바이트. highest = `g = fls(top)`, `b = fls(leaf[g])` — 로드 2, CLZ 2, 루프 0.
- 불변식 I1: top 비트 g == (leaf[g] ≠ 0). set 은 둘 다 무조건 켜고, clear 는 **leaf 가 0 이 될 때만** 요약을 끈다.
- I1 이 깨지면 `clz(0)` UB → ARM 에서 유령 우선순위 → HardFault. 빈 검사는 `top == 0` 하나면 충분.
- 마스크: `0xFFFFFFFFu >> (31 − b)` (b 이하), `(1u << g) − 1u` (g 미만). 시프트 32 와 `1 << 31`(int) 은 UB.
- 준비 큐: 비트 p == 줄 p 비어 있지 않음. unready 는 줄이 빌 때만 clear. FreeRTOS `taskRESET_READY_PRIORITY` 와 같은 규칙.
- 측정 (이 Mac): 2-level 은 분포와 무관하게 0.68~0.69 ns, 비트 루프 1.7~247 ns, 워드 루프 최대 32워드.
- 두 워드 갱신은 원자적이지 않다 → 임계 구역. M0 엔 CLZ 가 없다 → `__clzsi2` / 표 / 이진 탐색, 여전히 상수 시간.

## 참고자료

- [FreeRTOS — `configUSE_PORT_OPTIMISED_TASK_SELECTION`, `configMAX_PRIORITIES` (FreeRTOSConfig.h 설명)](https://www.freertos.org/a00110.html)
- [FreeRTOS-Kernel `tasks.c` — `taskRECORD_READY_PRIORITY`, `taskSELECT_HIGHEST_PRIORITY_TASK`, `taskRESET_READY_PRIORITY`](https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/tasks.c)
- [FreeRTOS-Kernel `portable/GCC/ARM_CM4F/portmacro.h` — `portGET_HIGHEST_PRIORITY`, `ucPortCountLeadingZeros`](https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/portable/GCC/ARM_CM4F/portmacro.h)
- [Linux `kernel/sched/rt.c` — `pick_next_rt_entity()`](https://github.com/torvalds/linux/blob/master/kernel/sched/rt.c), [`include/asm-generic/bitops/sched.h` — `sched_find_first_bit()`](https://github.com/torvalds/linux/blob/master/include/asm-generic/bitops/sched.h)
- Jean J. Labrosse, *MicroC/OS-II: The Real-Time Kernel*, 2nd ed., Ch. 3 — `OSRdyGrp` / `OSRdyTbl[]` / `OSUnMapTbl[]` 준비 리스트
- [µC/OS-III `os_prio.c` — `OS_PrioGetHighest()`](https://github.com/weston-embedded/uC-OS3/blob/master/Source/os_prio.c)
- [Arm — ARMv7-M Architecture Reference Manual (CLZ, RBIT)](https://developer.arm.com/documentation/ddi0403/latest/), [CMSIS-Core `__CLZ()` 내장 함수](https://arm-software.github.io/CMSIS_6/latest/Core/group__intrinsic__CPU__gr.html)
- [GCC — Other Built-in Functions (`__builtin_clz` / `__builtin_ctz`: 인자 0 이면 결과 미정의)](https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html)
- ISO/IEC 9899:2011 §6.5.7 비트 시프트 연산자 (시프트 양 ≥ 폭 → UB, 부호 있는 오버플로 → UB)
