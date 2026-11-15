---
id: RTOS-B04
time: 65분
updated: 2026-11-11
---
# RTOS-B04 미니 커널 1: 우선순위 비트맵 ready list (CLZ)

## 학습 목표

- ready 비트맵 1워드(32비트) + 우선순위별 FIFO 리스트 32개로 **"가장 급한 READY 태스크 고르기"를 O(1)** 로 만들고, 불변식 `bit p == 1 ⇔ head[p] != NULL` 을 말과 코드로 지킬 수 있다.
- CLZ(count leading zeros)를 내장 함수 없이 이진 탐색으로 직접 쓰고, `__builtin_clz` 와 단일·두 비트 전수 + 무작위 100만 개(+ demo 로 2^32 전수)로 대조한다. `31 - clz(bm)` 의 뜻과 `clz(0)` 함정을 안다.
- intrusive 이중 연결 리스트에서 **중간 삭제가 O(1)** 인 이유와, 마지막 원소가 빠질 때만 비트를 끄는 이유를 설명한다.
- 이 ready list 를 ucontext 미니 커널의 `sched_pick_next()` / `task_yield()` 로 써서, 같은 우선순위 라운드 로빈과 "높은 우선순위가 먼저"를 타임라인 + 독립 오라클로 증명한다.
- arm64·Cortex-M4 에서 `clz` 한 명령, Cortex-M0 에서는 `__clzsi2` 호출이 되는 것을 실제 asm 으로 보고, FreeRTOS `configUSE_PORT_OPTIMISED_TASK_SELECTION` / `portGET_HIGHEST_PRIORITY` 소스와 연결한다.

## 1. 문제

"RTOS 스케줄러는 다음 태스크를 어떻게 고르나요? 태스크가 100개면 느려지나요?" — RTOS 면접의 단골이고, 답의 핵심은 **비트맵 + CLZ 한 명령** 이다. 스케줄러는 PendSV 같은 문맥 교환 경로에서 인터럽트를 막고 도는 코드라서, 고르는 시간이 태스크 수에 따라 늘면 **최악 응답 시간(WCET)** 이 같이 늘어난다. 그래서 FreeRTOS 의 Cortex-M3/M4F/M7 포트(GCC 포트 기본값, 소스 확인)가 "우선순위마다 리스트 하나 + 어느 리스트가 비어 있지 않은지 비트 하나" 구조를 쓰고, µC/OS 와 Zephyr(MULTIQ 옵션)도 같은 계열이다(문서 기준, 미검증).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">ready list = 비트맵 1워드 + 우선순위별 FIFO 32개 (Part C2, tick 0 상태)</text>
  <text x="10" y="59" fill="currentColor" font-family="monospace">bitmap</text>
  <rect x="70" y="42" width="120" height="24" class="f-mut" stroke="currentColor"/>
  <text x="84" y="59" fill="currentColor" font-family="monospace">31…8 : 0</text>
  <rect x="190" y="42" width="36" height="24" stroke="currentColor" fill="none"/> <text x="204" y="59" fill="currentColor">0</text>
  <rect x="226" y="42" width="36" height="24" class="f-acc" stroke="currentColor"/> <text x="240" y="59" fill="currentColor">1</text>
  <rect x="262" y="42" width="36" height="24" stroke="currentColor" fill="none"/> <text x="276" y="59" fill="currentColor">0</text>
  <rect x="298" y="42" width="36" height="24" class="f-acc" stroke="currentColor"/> <text x="312" y="59" fill="currentColor">1</text>
  <rect x="334" y="42" width="36" height="24" stroke="currentColor" fill="none"/> <text x="348" y="59" fill="currentColor">0</text>
  <rect x="370" y="42" width="36" height="24" class="f-acc" stroke="currentColor"/> <text x="384" y="59" fill="currentColor">1</text>
  <rect x="406" y="42" width="36" height="24" stroke="currentColor" fill="none"/> <text x="420" y="59" fill="currentColor">0</text>
  <rect x="442" y="42" width="36" height="24" stroke="currentColor" fill="none"/> <text x="456" y="59" fill="currentColor">0</text>
  <text x="198" y="82" fill="currentColor" font-size="12">b7</text> <text x="234" y="82" fill="currentColor" font-size="12">b6</text> <text x="270" y="82" fill="currentColor" font-size="12">b5</text> <text x="306" y="82" fill="currentColor" font-size="12">b4</text> <text x="342" y="82" fill="currentColor" font-size="12">b3</text> <text x="378" y="82" fill="currentColor" font-size="12">b2</text> <text x="414" y="82" fill="currentColor" font-size="12">b1</text> <text x="450" y="82" fill="currentColor" font-size="12">b0</text>
  <text x="490" y="59" fill="currentColor" font-family="monospace">= 0x00000054</text>
  <text x="20" y="122" fill="currentColor" font-family="monospace">head[6] →</text>
  <rect x="110" y="104" width="80" height="26" rx="4" class="f-acc" stroke="currentColor"/> <text x="124" y="122" fill="currentColor">RADIO</text>
  <text x="20" y="166" fill="currentColor" font-family="monospace">head[4] →</text>
  <rect x="110" y="148" width="80" height="26" rx="4" class="f-mut" stroke="currentColor"/> <text x="134" y="166" fill="currentColor">IMU</text>
  <path d="M190 157 L216 157 M216 165 L190 165" stroke="currentColor"/>
  <rect x="216" y="148" width="80" height="26" rx="4" class="f-mut" stroke="currentColor"/> <text x="236" y="166" fill="currentColor">CTRL</text>
  <text x="304" y="166" fill="currentColor" font-family="monospace">← tail[4]</text>
  <text x="20" y="210" fill="currentColor" font-family="monospace">head[2] →</text>
  <rect x="110" y="192" width="80" height="26" rx="4" class="f-mut" stroke="currentColor"/> <text x="134" y="210" fill="currentColor">LOG</text>
  <path d="M190 201 L216 201 M216 209 L190 209" stroke="currentColor"/>
  <rect x="216" y="192" width="80" height="26" rx="4" class="f-mut" stroke="currentColor"/> <text x="230" y="210" fill="currentColor">BLINK</text>
  <text x="304" y="210" fill="currentColor" font-family="monospace">← tail[2]</text>
  <text x="20" y="246" fill="currentColor">나머지 head[p] = NULL ⇔ bit p = 0</text>
  <rect x="470" y="96" width="260" height="140" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="482" y="118" fill="currentColor" font-weight="bold">sched_pick_next()</text>
  <text x="482" y="142" fill="currentColor" font-family="monospace">① p = 31 - clz(0x54)</text>
  <text x="482" y="162" fill="currentColor" font-family="monospace">     = 31 - 25 = 6</text>
  <text x="482" y="186" fill="currentColor" font-family="monospace">② t = head[6] = RADIO</text>
  <text x="482" y="210" fill="currentColor" font-family="monospace">③ 빼기: head[6]==NULL</text>
  <text x="482" y="228" fill="currentColor" font-family="monospace">   → bit 6 끔 → 0x14</text>
  <text x="10" y="282" fill="currentColor">같은 우선순위는 꼬리에 넣고 머리에서 뺀다 (FIFO). 루프가 없다 → 태스크가 몇 개든 고르는 시간은 같다.</text>
</svg>
```

이 레슨은 RTOS 트랙의 **미니 커널** 시리즈 첫 조각이다. TCB 와 상태 전이(RTOS-B02), ucontext 문맥 교환(RTOS-B03)은 다른 레슨이 다룬다 — 여기서는 커널 뼈대(ucontext 배관)를 하네스로 주고, 학생은 **ready list 와 고르기** 만 쓴다. 선점(preemption)은 아직 없다: 태스크가 `task_yield()` / `task_block()` 을 부를 때만 바뀐다 (협력형, 선점은 RTOS-B05).

### Part A — 비트맵 + CLZ + 우선순위별 FIFO

`readyq_t { bitmap, head[32], tail[32], count }`, TCB 안에 링크 `next/prev` 와 `in_rq` (FreeRTOS 의 `xStateListItem` + `pxContainer` 에 해당).

- `my_clz32(x)` — MSB 쪽 연속 0 개수, `x == 0` 이면 32. **내장 함수 금지** (이진 탐색 5단계).
- `bm_highest(bm)` — `31 - clz`, 비었으면 `-1` (= idle).
- `rq_push_tail(q, t)` — 꼬리에 붙이고 비트 켜기. 이미 들어 있거나 `prio >= 32` 이면 false.
- `rq_remove(q, t)` — 머리·중간·꼬리 어디서든 O(1) 삭제, 리스트가 **비었을 때만** 비트 끄기. 없으면 false.
- `rq_pop_highest(q)` — 루프 없이 최고 우선순위 리스트의 머리를 꺼낸다.

### Part B — 대조 테스트 (경계 + 무작위)

1. CLZ: `0`, 단일 비트 32개, 두 비트 C(32,2)=496개, 하위 채움 마스크 32개, 무작위 100만 개(크기가 고르게 `>> 0..31`)를 `__builtin_clz` 와 대조. 2^32 전수는 demo 로.
2. 경계: prio 31 (`1u << 31`), prio 32 거부, 이중 삽입 거부, 없는 태스크 삭제 거부, 빈 큐 pop = NULL.
3. ready list 무작위 200,000 연산(push / 아무 위치 삭제 / pop)을 **독립 참조 모델**(우선순위별 배열 + 31→0 선형 탐색)과 비교. 매 연산 `bm_highest == 참조 최고`, 64 연산마다 불변식 전수 검사(비트↔머리, prev/next 대칭, tail, count).

### Part C — ucontext 미니 커널: nRF52 BLE 센서 노드

커널(하네스)은 tick 하나마다 ① ISR 훅 → ② `sched_pick_next()` → ③ 뽑힌 태스크가 yield/block/return 할 때까지 한 slice 를 돈다. 학생은 `sched_pick_next()` 와 `task_yield()` 를 채운다. 오라클은 **태스크 표 전체** 를 훑어 "READY 중 최고 우선순위, 같으면 먼저 READY 된 것"을 따로 계산해 매 tick 비교한다.

- **C1**: A·B·C(p3) 가 3번씩 일하고 양보, H(p5) 도 같은 코드. 기대 트레이스 `HHHHABCABCABCABC`.
- **C2**: RADIO(p6, BLE connection event 마다 송신), IMU(p4, DRDY 인터럽트마다 FIFO 읽기 → 양보 → 필터), CTRL(p4, 제어 3스텝), LOG·BLINK(p2). ISR: IMU 를 tick 2·7·12 에, RADIO 를 tick 7·22 에 깨운다 (tick 7 은 IMU 를 **먼저** 깨움). 기대 트레이스 `RICICICRCIILIIBLBLBLB.R`, 이벤트 유실 0, idle 1 tick.

## 2. Problem (English)

> **O(1) ready queue for a 32-priority RTOS, then use it as the scheduler of a tiny kernel.**
>
> ```c
> int    my_clz32(uint32_t x);                  // leading zeros, 32 for x == 0, no compiler builtins
> int    bm_highest(uint32_t bm);               // index of highest set bit, -1 if bm == 0
> bool   rq_push_tail(readyq_t *q, tcb_t *t);   // append to FIFO of t->prio; false if already queued or prio >= 32
> bool   rq_remove(readyq_t *q, tcb_t *t);      // O(1) unlink from anywhere; false if not queued
> tcb_t *rq_pop_highest(readyq_t *q);           // head of the highest non-empty priority, NULL if empty
> ```
>
> Priorities are 0..31, larger = more urgent. Invariant: bit p of `bitmap` is set iff list p is non-empty. All operations must be O(1) — no loop over priorities or tasks.
>
> 1. Prove `my_clz32` against `__builtin_clz` (all 1- and 2-bit patterns + 1M random values), and the queue against a naive reference model under 200k random operations including removals from the middle.
> 2. Plug the queue into a cooperative kernel: `sched_pick_next()` and `task_yield()` (re-queue at the tail of the same priority). Show round-robin among equal priorities and that a task woken by an ISR at a higher priority runs next even if it was woken last.
>
> Follow-ups: What does `__builtin_clz(0)` return? What does FreeRTOS do on Cortex-M0? Why max 32 priorities with the optimised selector — how would you support 256? Where does a preempted task go? What happens if you change a task's priority while it is in the ready list?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

`#define _XOPEN_SOURCE 600` 은 맥의 `<ucontext.h>` 때문이다. 빼면 실제로 `ucontext.h:51:2: error: The deprecated ucontext routines require _XOPEN_SOURCE to be defined`. 넣으면 이번엔 `'swapcontext' is deprecated: first deprecated in macOS 10.6` 가 `-Werror` 로 에러 4개가 된다. 그래서 커널 부분만 `#pragma clang diagnostic ignored "-Wdeprecated-declarations"` 로 감쌌다 (push/pop — 학생 코드의 다른 경고는 그대로 잡힌다). ucontext 자체 설명은 RTOS-B03.

처음 실행 결과 (실제 출력 일부, 0.1 초 안에 끝남):

```text
== Part A: CLZ, 비트맵, 우선순위별 FIFO ==
FAIL starter.c:138  my_clz32(0) == 32 && my_clz32(1) == 31 && my_clz32(0x80000000u) == 0
...
clz vs __builtin_clz: single 32 + double 496 + low-mask 32 + random 1000000 -> mismatches 953324
ready list random: ops=200000 pops=66664 middle-removes=0 pop-mismatch=66479 invariant-bad=199431 ret-bad=66507
...
== Part C2: BLE 센서 노드 (RADIO p6, IMU p4, CTRL p4, LOG p2, BLINK p2) ==
trace: ........................................  (end tick 40, idle ticks 40)
app log:   tx=0 samples=0 ctrl=0 isr_lost=5 order_err=40
1/28 passed
```

하네스 설계:

- **멈추지 않는다.** `kernel_run(limit)` 은 tick 상한(40)에서 끝난다. 스텁 상태에선 아무도 ready list 에 안 들어가 40 tick 모두 idle 이다. pop 이 `READY` 아닌 태스크를 돌려주면 커널은 **실행을 거부**하고 `g_not_ready_pick++` (끝난 문맥으로 swapcontext 하는 사고 방지).
- **오라클은 학생 코드와 독립.** `ready_seq`(READY 가 된 순번)는 커널 서비스 `k_make_ready()` 가 매기고, `rq_*` 는 이 값을 보지 않는다. 오라클은 태스크 표를 선형으로 훑는다 — 비트맵을 어떻게 짰든 결과만 본다.
- **Part B 의 참조 모델도 독립.** 배열 + 선형 탐색이라 틀리기 어려운 코드다. 리스트 순회에는 고리(cycle) 방어 상한이 있어, 잘못된 링크로도 무한 루프에 빠지지 않는다.

## 4. 힌트

<details>
<summary>힌트 1 — CLZ 이진 탐색: "위 절반이 전부 0 인가?"</summary>

`x <= 0x0000FFFF` 이면 위 16비트가 0 → `n += 16`, `x <<= 16` 로 아래 절반을 위로 올린다. 그다음 8, 4, 2, 1. 비교 5번, 루프 없음. 마지막 단계는 `x <= 0x7FFFFFFF` (= MSB 가 0) 이면 `n += 1`. `x == 0` 은 맨 앞에서 따로 32.

</details>

<details>
<summary>힌트 2 — 비트 연산은 반드시 <code>1u</code></summary>

`1 << 31` 은 `int` 를 넘친다 → 정의되지 않은 동작(UB, C11 6.5.7p4). prio 31 테스트가 이걸 잡는다. `q->bitmap |= 1u << p;` / `q->bitmap &= ~(1u << p);`.

</details>

<details>
<summary>힌트 3 — 이중 연결 리스트 삭제는 "앞이 없으면 head, 뒤가 없으면 tail"</summary>

`if (t->prev) t->prev->next = t->next; else q->head[p] = t->next;` 그리고 대칭으로 next/tail. 머리·중간·꼬리·혼자 네 경우가 이 두 줄로 끝난다. 그다음 `head[p] == NULL` 일 때만 비트를 끈다 — 같은 우선순위에 다른 태스크가 남아 있으면 비트는 켜져 있어야 한다.

</details>

<details>
<summary>힌트 4 — pop 은 "고르기 + 삭제"</summary>

`p = bm_highest(bitmap)`, `p < 0` 이면 NULL, 아니면 `t = head[p]; rq_remove(q, t);`. 삭제 코드를 재사용하면 비트 끄기를 두 군데서 관리하지 않아도 된다.

</details>

<details>
<summary>힌트 5 — yield = "다시 줄 서기" + 전환</summary>

`task_yield()` 는 `k_make_ready(g_cur)` (READY + 순번 + 꼬리에 push) 다음 `k_switch_to_sched()`. 줄을 다시 서지 않고 전환만 하면 그 태스크는 영원히 사라진다(아무 리스트에도 없음). `sched_pick_next()` 는 `rq_pop_highest(&g_rq)` 한 줄.

</details>

## 5. 풀이

### Part A — CLZ 와 ready list

`my_clz32(0x54)` 를 손으로 따라가면 (0x54 = 0b0101_0100, C2 의 tick 0 비트맵):

| 단계 | 비교 | 참? | n | x (16진) |
|---|---|---|---|---|
| 시작 | — | — | 0 | 0x00000054 |
| 16 | `x <= 0x0000FFFF` | 참 | 16 | 0x00540000 |
| 8 | `x <= 0x00FFFFFF` | 참 | 24 | 0x54000000 |
| 4 | `x <= 0x0FFFFFFF` | 거짓 | 24 | 그대로 |
| 2 | `x <= 0x3FFFFFFF` | 거짓 | 24 | 그대로 |
| 1 | `x <= 0x7FFFFFFF` | 참 (MSB 0) | **25** | — |

`bm_highest = 31 - 25 = 6` → RADIO 의 우선순위. 같은 일을 ARM 은 `CLZ` 명령 하나로 한다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">① 31 - clz(bm) = 가장 높은 켜진 비트    ② 중간 삭제 O(1)</text>
  <rect x="10" y="34" width="500" height="26" class="f-mut" stroke="currentColor"/>
  <text x="20" y="52" fill="currentColor" font-family="monospace">0000 0000 0000 0000 0000 0000 0</text>
  <rect x="510" y="34" width="120" height="26" class="f-acc" stroke="currentColor"/>
  <text x="520" y="52" fill="currentColor" font-family="monospace">101 0100</text>
  <text x="200" y="80" fill="currentColor">clz = 25 (앞의 0)</text>
  <text x="510" y="80" fill="currentColor">bit 6 = 31 - 25</text>
  <text x="640" y="52" fill="currentColor">= 0x54</text>
  <rect x="40" y="120" width="90" height="30" rx="4" class="f-mut" stroke="currentColor"/> <text x="72" y="140" fill="currentColor">x</text>
  <rect x="230" y="120" width="90" height="30" rx="4" class="f-warn" stroke="currentColor"/> <text x="262" y="140" fill="currentColor">y</text>
  <rect x="420" y="120" width="90" height="30" rx="4" class="f-mut" stroke="currentColor"/> <text x="452" y="140" fill="currentColor">z</text>
  <path d="M130 128 L228 128 M320 128 L418 128" stroke="currentColor" stroke-dasharray="4 3"/>
  <path d="M228 142 L130 142 M418 142 L320 142" stroke="currentColor" stroke-dasharray="4 3"/>
  <path d="M85 150 Q275 205 465 150" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M455 150 L465 150 L461 160" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="150" y="192" fill="currentColor">x.next = z ,  z.prev = x  (y.prev / y.next 로 바로 찾는다)</text>
  <text x="530" y="132" fill="currentColor">p7 에 y 를 빼도</text>
  <text x="530" y="150" fill="currentColor">x, z 가 남음 → bit 7 유지</text>
  <text x="10" y="232" fill="currentColor">단일 연결이면 y 의 앞(x)을 찾으려고 리스트를 훑어야 한다 (O(n)). 이중 연결 + TCB 안의 링크라서 O(1).</text>
</svg>
```

불변식 세 개만 지키면 된다:

| 불변식 | 깨지면 생기는 일 | 지키는 곳 |
|---|---|---|
| `bit p == 1 ⇔ head[p] != NULL` | 비트만 켜져 있음 → `head[p]` NULL 역참조. 비트만 꺼져 있음 → 그 우선순위 태스크가 **영원히 안 뽑힘** | push 에서 켜고, remove 에서 비었을 때만 끔 |
| prev/next 대칭, `tail[p]` = 마지막 | 순회 고리, 엉뚱한 태스크 실행 | remove 의 두 줄 |
| `in_rq` = 실제 소속 | 이중 삽입 → 자기 자신을 가리키는 고리 | push 는 `in_rq` 면 거부 |

Part A 단위 테스트 (전부 PASS): `a,b,c(p5) d(p9) e(p0)` 를 넣고 pop → `dabce` (높은 것 먼저, 같은 p5 는 넣은 순). p7 에 x y z 를 넣고 y(중간) → x(머리) → z(마지막) 순으로 지우면 비트 7 은 마지막에만 꺼지고 최고가 7 → 3 으로 내려간다. prio 31 은 `bitmap == 0x80000008`, prio 32 는 거부.

### Part B — 대조 테스트 결과 (실제 출력)

```text
clz vs __builtin_clz: single 32 + double 496 + low-mask 32 + random 1000000 -> mismatches 0
ready list random: ops=200000 pops=66596 middle-removes=884 pop-mismatch=0 invariant-bad=0 ret-bad=0
```

- 무작위 값은 `xorshift32() >> (0..31)` 로 만들었다. 그냥 32비트 난수는 절반이 MSB=1 이라 clz 가 거의 0 이어서 아래 단계들이 시험되지 않는다. `v == 0` 은 건너뛴다 (`__builtin_clz(0)` 은 UB — Q2).
- 무작위 연산에서 우선순위를 절반은 p0–3 에 몰았다. 32개에 고르게 뿌리면 리스트 길이가 대부분 0–1 이라 **중간 삭제** 가 125번뿐이었다(첫 시도 실측). 몰아서 884번으로 늘렸다.
- **2^32 전수** (demo, 인자 `exhaustive`): `exhaustive: 4294967295 values (1..0xFFFFFFFF), mismatches 0, 10.0 s` — 다른 작업과 CPU 를 나눠 쓴 상태의 wall time (user 5.1 s).

### asm — CLZ 는 실제로 무엇이 되나 (demo_clz_asm.c, 실제 출력)

<!-- code: demo_clz_asm.c -->

빌드 명령은 파일 머리 주석 (`cc -O2 -S` / `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -S` / `--target=thumbv6m-none-eabi -mcpu=cortex-m0`). 아래는 실제 `-S` 출력에서 `.cfi`/`.p2align` 같은 지시어만 걸러 세 타깃을 나란히 놓은 것이다.

```text
# arm64 (이 맥, Apple clang 21)            # Cortex-M4 (thumbv7em)              # Cortex-M0 (thumbv6m)
_highest_builtin:                          highest_builtin:                      highest_builtin:
  clz  w8, w0                                clz    r0, r0                         push  {r7, lr}
  mov  w9, #31                               rsb.w  r0, r0, #31                    cmp   r0, #0
  sub  w0, w9, w8                            bx     lr                             beq   .LBB0_2
  ret                                                                              bl    __clzsi2
                                           highest_port_asm:  (FreeRTOS 식)        b     .LBB0_3
_lowest_builtin:   (__builtin_ctz)           clz    r0, r0     @APP 인라인 asm   .LBB0_2: movs r0, #32
  rbit w8, w0                                uxtb   r0, r0     (uint8_t 반환)    .LBB0_3: movs r1, #31
  clz  w0, w8                                rsb.w  r0, r0, #31                    subs  r0, r1, r0
  ret                                        bx     lr                             pop   {r7, pc}
```

- arm64·M4: **`clz` 한 명령**. ctz 는 `rbit`(비트 뒤집기) + `clz` (arm64 와 M4 모두 관찰).
- M4 의 FreeRTOS 식 `ucPortCountLeadingZeros` 는 반환형이 `uint8_t` 라서 `uxtb` 가 하나 더 붙는다.
- **M0 (ARMv6-M) 에는 CLZ 명령이 없다** → 컴파일러가 `bl __clzsi2` (libgcc/compiler-rt 런타임 함수) 호출을 넣고, 0 이면 32 를 돌려주는 분기까지 직접 만든다. 같은 M0 에서 `__builtin_ctz` 는 호출 대신 `x & -x` 에 `0x077CB531` 을 곱하고 32칸 표를 찾는 **de Bruijn** 코드를 인라인했다 (연습 4 와 같은 기법).
- 내 이진 탐색 `highest_portable` 은 arm64 에서 분기 없는 명령 27개(`csel` 8개 포함), M4 에서 `it`/`lsllo` 조건 실행 22줄(bx 포함), M0 에서 `beq` 분기 8개짜리 코드가 됐다. 컴파일러가 이 패턴을 `clz` 로 바꿔 주지는 **않았다**.

FreeRTOS V11.3.2 가 실제로 하는 일 (소스 직접 확인):

- `portable/GCC/ARM_CM4F/portmacro.h:144–145` — 이 포트는 `configUSE_PORT_OPTIMISED_TASK_SELECTION` 이 정의 안 됐으면 **기본 1**. 151–158 `ucPortCountLeadingZeros()` = `__asm volatile ( "clz %0, %1" ...)`. 161–163 `configMAX_PRIORITIES > 32` 면 `#error` ("It is very rare that a system requires more than 10 to 15 difference priorities..."). 166–167 `portRECORD_READY_PRIORITY` = `|= ( 1UL << uxPriority )`, `portRESET_READY_PRIORITY` = `&= ~( 1UL << uxPriority )`. 171 `portGET_HIGHEST_PRIORITY` = `31UL - ucPortCountLeadingZeros( uxReadyPriorities )`.
- `tasks.c:227–261` (optimised 경로) — 238–245 `taskSELECT_HIGHEST_PRIORITY_TASK()` = `portGET_HIGHEST_PRIORITY` → `listGET_OWNER_OF_NEXT_ENTRY( pxCurrentTCB, &pxReadyTasksLists[ uxTopPriority ] )`. 253–259 `taskRESET_READY_PRIORITY` 는 **리스트 길이가 0 일 때만** 비트를 끈다 — 내 `rq_remove` 와 같은 규칙.
- `tasks.c:178–225` (일반 C 경로, 기본값 0 — `include/FreeRTOS.h:2803–2805`) — 비트맵 대신 `uxTopReadyPriority` 를 "최고 우선순위 값" 으로 캐시하고, 197–212 에서 그 리스트가 비면 `--uxTopPriority` 로 **한 칸씩 내려간다** (최악 configMAX_PRIORITIES 번). 같은 변수 이름이 경로에 따라 "숫자" 또는 "비트맵" 이 된다 (tasks.c:4022–4025 주석: "the uxTopReadyPriority variable is used as a bit map").
- `tasks.c:287–293` `prvAddTaskToReadyList` = 비트 기록 + `listINSERT_END` (꼬리). `tasks.c:478` `pxReadyTasksLists[ configMAX_PRIORITIES ]`, `tasks.c:5273` `vTaskSwitchContext` 안에서 고르기, Cortex-M4F 에서는 PendSV 핸들러가 `msr basepri` 로 인터럽트를 막고 `bl vTaskSwitchContext` (port.c:524–530).
- **M0**: `portable/GCC/ARM_CM0/` 에는 이 매크로가 아예 없다(grep 0건) → 기본값 0 = 일반 C 경로. ARMv8-M baseline(M23)은 `ARM_CM33/non_secure/portmacrocommon.h:556–562` 에서 Main Extension 이 없으면 0, 억지로 1 이면 586–588 `#error ARMv8-M baseline implementations (such as Cortex-M23) do not support port-optimised task selection`. RISC-V 포트는 `__builtin_clz` 를 쓴다 (`portable/GCC/RISC-V/portmacro.h:153`). SMP 는 금지 (`FreeRTOS.h:2917–2918`).

### demo_pick_bench.c — 고르는 비용 (실제 출력)

<!-- code: demo_pick_bench.c -->

이 맥(Apple Silicon, -O2), 2천만 번 × 5회 중 최소, 두 번 실행. **비율만** 보라 (다른 작업과 CPU 공유).

| 패턴 | linear (머리 32개 훑기) | my_clz (이진 탐색) | builtin (`clz` 1개) |
|---|---|---|---|
| idle 만 ready (bit 0) — 선형 최악 | 13.14 / 10.38 ns | 2.36 / 2.30 ns | 0.41 / 0.38 ns |
| 무작위 비트맵 | 0.77 / 0.82 ns | 2.12 / 2.24 ns | 0.58 / 0.36 ns |
| bit 31 만 — 선형 최선 | 0.45 / 0.54 ns | 1.89 / 2.43 ns | 0.36 / 0.44 ns |

선형 탐색은 **패턴에 따라 25배 이상 흔들린다** (0.45 → 13 ns). CLZ 는 패턴과 무관하게 일정하다 — RTOS 가 원하는 것은 평균이 아니라 이 **일정함(결정성)** 이다. 내 C 이진 탐색은 의존 연산 사슬이라 무작위 패턴에선 오히려 선형보다 느리지만, 최악이 상수로 묶인다. Cortex-M4 의 실제 숫자는 이 맥과 다르다(미측정) — 거기선 `clz` 1 사이클 vs 메모리 로드 32번이다.

### Part C — ucontext 미니 커널

학생 코드 두 함수가 커널 전체의 "정책" 이다:

```c
static tcb_t *sched_pick_next(void) { return rq_pop_highest(&g_rq); }
static void task_yield(void)        { k_make_ready(g_cur); k_switch_to_sched(); }
```

실행 중인 태스크는 **ready list 에 없다** (pop 으로 꺼냈으니까). yield 하면 같은 우선순위 꼬리로 돌아가고, block 하면 아무 리스트에도 없다가 ISR 의 `task_unblock()` 이 꼬리에 넣는다. FreeRTOS 는 다르다 — 실행 중인 태스크도 ready list 에 **남아 있고** `pxIndex` 커서만 돈다 (연습 2).

**C1 (실제 출력)**: `trace: HHHHABCABCABCABC  (end tick 16)`, `order_err=0`.

```svg
<svg viewBox="0 0 656 194" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">Part C1 실제 타임라인 — A,B,C = p3, H = p5 (협력형 yield)</text> <text x="84" y="38" fill="currentColor">0</text> <text x="120" y="38" fill="currentColor">1</text> <text x="156" y="38" fill="currentColor">2</text> <text x="192" y="38" fill="currentColor">3</text> <text x="228" y="38" fill="currentColor">4</text> <text x="264" y="38" fill="currentColor">5</text> <text x="300" y="38" fill="currentColor">6</text> <text x="336" y="38" fill="currentColor">7</text> <text x="372" y="38" fill="currentColor">8</text> <text x="408" y="38" fill="currentColor">9</text> <text x="444" y="38" fill="currentColor">10</text> <text x="480" y="38" fill="currentColor">11</text> <text x="516" y="38" fill="currentColor">12</text> <text x="552" y="38" fill="currentColor">13</text> <text x="588" y="38" fill="currentColor">14</text> <text x="624" y="38" fill="currentColor">15</text>
  <text x="10" y="61" fill="currentColor" font-family="monospace">A</text> <rect x="70" y="48" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="106" y="48" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="142" y="48" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="178" y="48" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="214" y="48" width="36" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="250" y="48" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="286" y="48" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="322" y="48" width="36" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="358" y="48" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="394" y="48" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="430" y="48" width="36" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="466" y="48" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="502" y="48" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="538" y="48" width="36" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="85" fill="currentColor" font-family="monospace">B</text> <rect x="70" y="72" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="106" y="72" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="142" y="72" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="178" y="72" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="214" y="72" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="250" y="72" width="36" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="286" y="72" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="322" y="72" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="358" y="72" width="36" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="394" y="72" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="430" y="72" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="466" y="72" width="36" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="502" y="72" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="538" y="72" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="574" y="72" width="36" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="109" fill="currentColor" font-family="monospace">C</text> <rect x="70" y="96" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="106" y="96" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="142" y="96" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="178" y="96" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="214" y="96" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="250" y="96" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="286" y="96" width="36" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="322" y="96" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="358" y="96" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="394" y="96" width="36" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="430" y="96" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="466" y="96" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="502" y="96" width="36" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="538" y="96" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="574" y="96" width="36" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="610" y="96" width="36" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="133" fill="currentColor" font-family="monospace">H</text> <rect x="70" y="120" width="36" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="106" y="120" width="36" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="142" y="120" width="36" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="178" y="120" width="36" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <rect x="10" y="148" width="14" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <text x="30" y="159" fill="currentColor">R 실행</text> <rect x="90" y="148" width="14" height="12" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <text x="110" y="159" fill="currentColor">r READY (ready list 안)</text> <rect x="290" y="148" width="14" height="12" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="310" y="159" fill="currentColor">B 이벤트 대기</text> <text x="420" y="159" fill="currentColor">빈칸 = 끝남(DELETED)</text> <text x="10" y="182" fill="currentColor">H 는 yield 해도 bit 5 가 혼자 최고 → 다시 뽑힌다. 그다음 p3 은 A B C 순환.</text>
</svg>
```

H(p5) 는 `task_yield()` 를 세 번 불렀지만 매번 비트 5 가 유일한 최고라 다시 뽑혔다 — **yield 는 같은(또는 높은) 우선순위에게만 양보한다**. 4번째 slice 는 루프를 빠져나와 return(DELETED). 그다음 p3 의 A B C 가 꼬리 재삽입 순서대로 돈다.

**C2 (실제 출력)**:

```text
trace: RICICICRCIILIIBLBLBLB.R  (end tick 23, idle ticks 1)
tick : 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2
RADIO: R B B B B B B R B B B B B B B B B B B B B B R
IMU  : r R r R r R B r r R R B R R . . . . . . . . .
CTRL : r r R r R r R r R . . . . . . . . . . . . . .
LOG  : r r r r r r r r r r r R r r r R r R r R . . .
BLINK: r r r r r r r r r r r r r r R r R r R r R . .
bitmap@pick: 54 14 14 14 14 14 14 54 14 14 14 04 14 14 04 04 04 04 04 04 04 00 40
app log: CiCICRiILiIBLBLBR  tx=2 samples=3 ctrl=3 isr_lost=0 order_err=0
```

```svg
<svg viewBox="0 0 724 282" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">Part C2 실제 타임라인 — BLE 센서 노드 (solution.c 출력 그대로, 1 tick = 1 slice)</text> <text x="80" y="38" fill="currentColor">0</text> <text x="108" y="38" fill="currentColor">1</text> <text x="136" y="38" fill="currentColor">2</text> <text x="164" y="38" fill="currentColor">3</text> <text x="192" y="38" fill="currentColor">4</text> <text x="220" y="38" fill="currentColor">5</text> <text x="248" y="38" fill="currentColor">6</text> <text x="276" y="38" fill="currentColor">7</text> <text x="304" y="38" fill="currentColor">8</text> <text x="332" y="38" fill="currentColor">9</text> <text x="360" y="38" fill="currentColor">10</text> <text x="388" y="38" fill="currentColor">11</text> <text x="416" y="38" fill="currentColor">12</text> <text x="444" y="38" fill="currentColor">13</text> <text x="472" y="38" fill="currentColor">14</text> <text x="500" y="38" fill="currentColor">15</text> <text x="528" y="38" fill="currentColor">16</text> <text x="556" y="38" fill="currentColor">17</text> <text x="584" y="38" fill="currentColor">18</text> <text x="612" y="38" fill="currentColor">19</text> <text x="640" y="38" fill="currentColor">20</text> <text x="668" y="38" fill="currentColor">21</text> <text x="696" y="38" fill="currentColor">22</text>
  <text x="10" y="61" fill="currentColor" font-family="monospace">RADIO</text> <rect x="70" y="48" width="28" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="98" y="48" width="28" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="108" y="61" fill="currentColor">B</text> <rect x="126" y="48" width="28" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="136" y="61" fill="currentColor">B</text> <rect x="154" y="48" width="28" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="164" y="61" fill="currentColor">B</text> <rect x="182" y="48" width="28" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="192" y="61" fill="currentColor">B</text> <rect x="210" y="48" width="28" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="220" y="61" fill="currentColor">B</text> <rect x="238" y="48" width="28" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="248" y="61" fill="currentColor">B</text> <rect x="266" y="48" width="28" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="294" y="48" width="28" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="304" y="61" fill="currentColor">B</text> <rect x="322" y="48" width="28" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="332" y="61" fill="currentColor">B</text> <rect x="350" y="48" width="28" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="360" y="61" fill="currentColor">B</text> <rect x="378" y="48" width="28" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="388" y="61" fill="currentColor">B</text> <rect x="406" y="48" width="28" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="416" y="61" fill="currentColor">B</text> <rect x="434" y="48" width="28" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="444" y="61" fill="currentColor">B</text> <rect x="462" y="48" width="28" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="472" y="61" fill="currentColor">B</text> <rect x="490" y="48" width="28" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="500" y="61" fill="currentColor">B</text> <rect x="518" y="48" width="28" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="528" y="61" fill="currentColor">B</text> <rect x="546" y="48" width="28" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="556" y="61" fill="currentColor">B</text> <rect x="574" y="48" width="28" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="584" y="61" fill="currentColor">B</text> <rect x="602" y="48" width="28" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="612" y="61" fill="currentColor">B</text> <rect x="630" y="48" width="28" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="640" y="61" fill="currentColor">B</text> <rect x="658" y="48" width="28" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="668" y="61" fill="currentColor">B</text> <rect x="686" y="48" width="28" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="85" fill="currentColor" font-family="monospace">IMU</text> <rect x="70" y="72" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="98" y="72" width="28" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="126" y="72" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="154" y="72" width="28" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="182" y="72" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="210" y="72" width="28" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="238" y="72" width="28" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="248" y="85" fill="currentColor">B</text> <rect x="266" y="72" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="294" y="72" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="322" y="72" width="28" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="350" y="72" width="28" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="378" y="72" width="28" height="18" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="388" y="85" fill="currentColor">B</text> <rect x="406" y="72" width="28" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="434" y="72" width="28" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="109" fill="currentColor" font-family="monospace">CTRL</text> <rect x="70" y="96" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="98" y="96" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="126" y="96" width="28" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="154" y="96" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="182" y="96" width="28" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="210" y="96" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="238" y="96" width="28" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="266" y="96" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="294" y="96" width="28" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="133" fill="currentColor" font-family="monospace">LOG</text> <rect x="70" y="120" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="98" y="120" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="126" y="120" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="154" y="120" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="182" y="120" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="210" y="120" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="238" y="120" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="266" y="120" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="294" y="120" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="322" y="120" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="350" y="120" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="378" y="120" width="28" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="406" y="120" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="434" y="120" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="462" y="120" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="490" y="120" width="28" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="518" y="120" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="546" y="120" width="28" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="574" y="120" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="602" y="120" width="28" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="157" fill="currentColor" font-family="monospace">BLINK</text> <rect x="70" y="144" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="98" y="144" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="126" y="144" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="154" y="144" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="182" y="144" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="210" y="144" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="238" y="144" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="266" y="144" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="294" y="144" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="322" y="144" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="350" y="144" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="378" y="144" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="406" y="144" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="434" y="144" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="462" y="144" width="28" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="490" y="144" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="518" y="144" width="28" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="546" y="144" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="574" y="144" width="28" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <rect x="602" y="144" width="28" height="18" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <rect x="630" y="144" width="28" height="18" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
  <text x="10" y="181" fill="currentColor" font-family="monospace">bitmap</text> <text x="73" y="181" fill="currentColor" font-family="monospace" font-size="12">54</text> <text x="101" y="181" fill="currentColor" font-family="monospace" font-size="12">14</text> <text x="129" y="181" fill="currentColor" font-family="monospace" font-size="12">14</text> <text x="157" y="181" fill="currentColor" font-family="monospace" font-size="12">14</text> <text x="185" y="181" fill="currentColor" font-family="monospace" font-size="12">14</text> <text x="213" y="181" fill="currentColor" font-family="monospace" font-size="12">14</text> <text x="241" y="181" fill="currentColor" font-family="monospace" font-size="12">14</text> <text x="269" y="181" fill="currentColor" font-family="monospace" font-size="12">54</text> <text x="297" y="181" fill="currentColor" font-family="monospace" font-size="12">14</text> <text x="325" y="181" fill="currentColor" font-family="monospace" font-size="12">14</text> <text x="353" y="181" fill="currentColor" font-family="monospace" font-size="12">14</text> <text x="381" y="181" fill="currentColor" font-family="monospace" font-size="12">04</text> <text x="409" y="181" fill="currentColor" font-family="monospace" font-size="12">14</text> <text x="437" y="181" fill="currentColor" font-family="monospace" font-size="12">14</text> <text x="465" y="181" fill="currentColor" font-family="monospace" font-size="12">04</text> <text x="493" y="181" fill="currentColor" font-family="monospace" font-size="12">04</text> <text x="521" y="181" fill="currentColor" font-family="monospace" font-size="12">04</text> <text x="549" y="181" fill="currentColor" font-family="monospace" font-size="12">04</text> <text x="577" y="181" fill="currentColor" font-family="monospace" font-size="12">04</text> <text x="605" y="181" fill="currentColor" font-family="monospace" font-size="12">04</text> <text x="633" y="181" fill="currentColor" font-family="monospace" font-size="12">04</text> <text x="661" y="181" fill="currentColor" font-family="monospace" font-size="12">00</text> <text x="689" y="181" fill="currentColor" font-family="monospace" font-size="12">40</text> <path d="M140.0 208 L140.0 196" stroke="currentColor" stroke-width="1.5"/> <path d="M136.0 201 L140.0 195 L144.0 201" stroke="currentColor" fill="none" stroke-width="1.5"/> <text x="128.0" y="222" fill="currentColor" font-size="12">IMU</text> <path d="M280.0 208 L280.0 196" stroke="currentColor" stroke-width="1.5"/> <path d="M276.0 201 L280.0 195 L284.0 201" stroke="currentColor" fill="none" stroke-width="1.5"/> <text x="268.0" y="222" fill="currentColor" font-size="12">IMU+RAD</text> <path d="M420.0 208 L420.0 196" stroke="currentColor" stroke-width="1.5"/> <path d="M416.0 201 L420.0 195 L424.0 201" stroke="currentColor" fill="none" stroke-width="1.5"/> <text x="408.0" y="222" fill="currentColor" font-size="12">IMU</text> <path d="M700.0 208 L700.0 196" stroke="currentColor" stroke-width="1.5"/> <path d="M696.0 201 L700.0 195 L704.0 201" stroke="currentColor" fill="none" stroke-width="1.5"/> <text x="688.0" y="222" fill="currentColor" font-size="12">RAD</text>
  <rect x="10" y="236" width="14" height="12" class="f-acc" stroke="currentColor" stroke-width="0.5"/> <text x="30" y="247" fill="currentColor">R 실행</text> <rect x="90" y="236" width="14" height="12" class="f-mut" stroke="currentColor" stroke-width="0.5"/> <text x="110" y="247" fill="currentColor">r READY (ready list 안)</text> <rect x="290" y="236" width="14" height="12" class="f-warn" stroke="currentColor" stroke-width="0.5"/> <text x="310" y="247" fill="currentColor">B 이벤트 대기</text> <text x="420" y="247" fill="currentColor">빈칸 = 끝남(DELETED)</text> <text x="10" y="270" fill="currentColor">ISR 화살표: 그 tick 시작에 task_unblock. tick 7 은 IMU 를 먼저 깨웠지만 RADIO(p6)가 먼저 돈다.</text>
</svg>
```

| tick | ISR | 비트맵(고르기 전) | p | 뽑힘 | 한 일 → 결과 |
|---|---|---|---|---|---|
| 0 | — | 0x54 | 6 | RADIO | 첫 줄 `task_block()` → B |
| 1 | — | 0x14 | 4 | IMU | `task_block()` → B (p4 엔 CTRL 만) |
| 2 | IMU | 0x14 | 4 | CTRL | p4 = [CTRL, IMU] — **IMU 는 꼬리**. CTRL step1, yield |
| 3 | — | 0x14 | 4 | IMU | FIFO 읽기(`i`), yield |
| 4–6 | — | 0x14 | 4 | C I C | IMU 필터(`I`) 후 다시 block (tick 5) |
| 7 | IMU → RADIO 순서로 | 0x54 | 6 | RADIO | IMU 가 먼저 줄 섰지만 **비트 6 이 이긴다**. 송신, block |
| 8 | — | 0x14 | 4 | CTRL | 루프 끝 → return (DELETED) |
| 9–10 | — | 0x14 | 4 | I I | 읽기, 필터 → block → p4 비어서 **bit 4 꺼짐** |
| 11 | — | 0x04 | 2 | LOG | **처음 실행** — tick 0 부터 11 tick 동안 r |
| 12–13 | IMU | 0x14 | 4 | I I | 세 번째 샘플 → return |
| 14–20 | — | 0x04 | 2 | B L B L B L B | p2 라운드 로빈 (BLINK 가 머리였음) |
| 21 | — | 0x00 | −1 | idle | 모두 끝났거나 B — 실제 RTOS 면 idle 태스크(WFI) |
| 22 | RADIO | 0x40 | 6 | RADIO | 송신 → return. tick 23 에 모두 DELETED → 종료 |

읽을 거리 두 개: ① LOG 는 11 tick 굶었다 — 협력형이든 선점형이든 우선순위 스케줄러에서 **낮은 우선순위는 높은 쪽이 쉴 때만** 돈다. 그래서 높은 태스크는 일을 마치면 반드시 block 해야 한다(폴링 루프 금지). ② tick 7 에서 깨운 순서는 IMU → RADIO 인데 RADIO 가 먼저 돌았다 — FIFO 는 **같은 우선순위 안에서만** 의미가 있다.

### 호스트 모델 → 실제 API 대응표

| 이 레슨 | FreeRTOS V11.3.2 (소스 확인) | Zephyr (문서 기준, 미검증) | CMSIS-RTOS2 (문서 기준, 미검증) |
|---|---|---|---|
| `readyq_t.bitmap` | `uxTopReadyPriority` (optimised 일 때 비트맵, tasks.c:507) | `CONFIG_SCHED_MULTIQ` 의 비트맵 (DUMB/SCALABLE 은 다른 구조) | 구현(RTX5 등) 내부 |
| `head/tail[p]` | `pxReadyTasksLists[p]` (tasks.c:478) | 우선순위별 큐 (MULTIQ) | — |
| `rq_push_tail` | `prvAddTaskToReadyList` (tasks.c:287–293) | 커널 내부 | — |
| `rq_remove` + 비트 끄기 | `uxListRemove` + `taskRESET_READY_PRIORITY` (tasks.c:253–259) | 커널 내부 | — |
| `bm_highest` | `portGET_HIGHEST_PRIORITY` (ARM_CM4F portmacro.h:171) | 커널 내부 | — |
| `sched_pick_next` | `taskSELECT_HIGHEST_PRIORITY_TASK` in `vTaskSwitchContext` (tasks.c:5273) | 커널 내부 | — |
| `task_yield` | `taskYIELD()` (task.h:227 → `portYIELD`) | `k_yield()` | `osThreadYield()` |
| `task_create(name, prio, fn)` | `xTaskCreate(fn, name, stack, arg, prio, &h)` | `k_thread_create(...)` | `osThreadNew(fn, arg, &attr)` |
| 우선순위 변경 (연습 1) | `vTaskPrioritySet` (tasks.c:2844) | `k_thread_priority_set()` | `osThreadSetPriority()` |
| 방향 | 클수록 급함, 0 = idle | **작을수록 급함**, 음수 = 협력형 스레드 | 클수록 급함 (`osPriorityRealtime` > `osPriorityNormal`) |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 28/28 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o so_asan && ./so_asan   # 28/28, 경고 0
```

5번 실행 출력 md5 가 모두 같았다 (`08659e69…`, 결정적). 실행 시간 0.1 초 이하. **ASan + swapcontext**: 리눅스 ASan 은 makecontext/swapcontext 에 대해 "false positive 가 날 수 있다" 경고를 내는 것으로 알려져 있어 시도했는데, 이 맥(Apple clang 21)에서는 경고 0, `ASAN_OPTIONS=detect_stack_use_after_return=1` 로도 깨끗했다 — 그래서 특별한 ASAN_OPTIONS 없이 쓴다. UBSan 단독(`-fsanitize=undefined`)도 깨끗. pthread 가 없으니 TSan 대상 아님.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `bitmap \|= 1 << p` | p=31 에서 `int` 넘침 = UB (C11 6.5.7p4). 최적화가 이상한 코드를 낼 수 있다 | `1u << p` (FreeRTOS 는 `1UL`) |
| 비트맵 0 에서 `31 - __builtin_clz(0)` | `__builtin_clz(0)` 은 UB. ARM `CLZ` 명령은 32 를 주므로 31-32 = −1 → `pxReadyTasksLists[-1]` | `bm ? 31 - clz(bm) : -1`, 또는 FreeRTOS 처럼 idle 이 항상 READY 라 0 이 될 수 없게 |
| remove 에서 비트를 무조건 끔 | 같은 우선순위에 남은 태스크가 **안 보인다** → 영원히 안 뽑힘 (조용한 굶주림) | `if (q->head[p] == NULL) bitmap &= ~(1u << p);` |
| remove 후 비트를 안 끔 | `head[p]` 가 NULL 인데 비트가 켜짐 → pop 이 NULL 역참조 (HardFault) | 위와 같은 한 줄 |
| yield 에서 push 를 머리에 | 같은 우선순위가 LIFO → 양보한 자기가 바로 다시 뽑힘, 나머지 굶음 | 꼬리에 (`listINSERT_END` 와 같음) |
| ready 상태에서 prio 를 먼저 바꾸고 remove | remove 가 **새** 우선순위 리스트를 건드린다. 연습 1 버그 판: 불변식 위반 2개, `head[5]` 에 이미 p9 인 x 가 남음 | 옛 prio 로 빼고 → 바꾸고 → push (FreeRTOS `uxPriorityUsedOnEntry`, tasks.c:2946–3004) |
| 이중 삽입 검사 없음 | `t->prev = tail = t` → 자기 자신 고리, 순회 무한 루프 | `in_rq` (FreeRTOS: `pxContainer`) 확인 |
| yield 가 줄을 다시 안 섬 | 태스크가 어느 리스트에도 없음 = 사라짐 (이벤트도 없으니 영원히) | `k_make_ready(g_cur)` 후 전환 |
| Zephyr 코드에 clz 를 그대로 | Zephyr 는 숫자가 작을수록 급하다 → 최하위 비트를 찾아야 | `x & -x` + clz, 또는 ctz (연습 5) |

## 8. 면접 Q&A

<details>
<summary>Q1. 스케줄러가 다음 태스크를 O(1) 로 고르는 방법은? 왜 O(1) 이어야 하죠?</summary>

우선순위마다 FIFO 리스트, 그리고 "비어 있지 않은 리스트" 를 표시하는 비트맵 한 워드. 최고 우선순위 = `31 - clz(bitmap)`, 그 리스트의 머리가 답. 삽입(꼬리)·삭제(이중 연결)·고르기 모두 상수 시간. 이유는 **최악 시간을 상수로 묶기 위해서**다. 고르기는 문맥 교환 경로(Cortex-M 의 PendSV, 인터럽트를 BASEPRI 로 막은 구간)에서 돈다. 태스크 수에 비례하면 인터럽트 지연과 WCET 가 설계마다 달라진다. 실측(demo_pick_bench)에서도 선형 탐색은 패턴에 따라 0.45 → 13 ns 로 25배 넘게 흔들리고, CLZ 는 0.4 ns 근처로 일정했다. 꼬리: "FreeRTOS 일반 C 경로는?" → `uxTopReadyPriority` 캐시 + 빈 리스트면 한 칸씩 내려가기 (tasks.c:197–212). 최악은 configMAX_PRIORITIES 번 — 우선순위 수가 작으면 충분히 빠르고, 이식성이 좋다.

</details>

<details>
<summary>Q2. <code>__builtin_clz(0)</code> 은 뭘 돌려주나요? 비트맵이 0 이면?</summary>

정의되지 않음(GCC 문서: "If x is 0, the result is undefined"). ARM 의 `CLZ` **명령** 자체는 0 에 32 를 준다 — 그래도 C 에서 0 을 넘기면 컴파일러가 "0 이 아니다" 라고 가정하고 최적화할 수 있다. 실제로 M0 용 코드에서 clang 은 `cmp r0,#0 / beq → 32` 분기를 직접 넣었다. 비트맵 0 = READY 태스크 없음. FreeRTOS 는 idle 태스크(우선순위 0)가 절대 block 하지 않고, 고르기가 태스크를 리스트에서 빼지 않으므로(tasks.c:238–245 는 `listGET_OWNER_OF_NEXT_ENTRY` 만) 실행 중에도 비트 0 이 켜져 있어 0 이 될 수 없다 — 그래서 포트 매크로에 0 검사가 없다. 이 레슨 모델에는 idle 태스크가 없어서 `bm_highest(0) = -1` 을 idle 로 쓴다 (C2 tick 21). 꼬리: "idle 훅에서 block 하면?" → 비트 0 이 꺼져 0 이 될 수 있다. FreeRTOS 가 idle hook 에서 블록 금지를 요구하는 이유 중 하나(문서 기준).

</details>

<details>
<summary>Q3. Cortex-M0 처럼 CLZ 가 없으면요?</summary>

ARMv6-M 에는 CLZ 가 없다. FreeRTOS `ARM_CM0` 포트는 optimised 선택을 정의하지 않아 일반 C 경로(캐시 + 감소 루프)를 쓴다. M23(ARMv8-M baseline)은 1 로 켜면 `#error` (portmacrocommon.h:586–588). C 로 `__builtin_clz` 를 쓰면 `bl __clzsi2` 런타임 호출이 된다(관찰). 대안: ① de Bruijn — 최상위 비트 아래를 모두 1 로 채우고(`v |= v>>1 ... >>16`) `0x07C4ACDD` 곱 + 32칸 표 (연습 4), ② 8비트 표 256칸 + 바이트 검사 (µC/OS-II 의 `OSUnMapTbl` 이 이 계열 — 문서 기준, 미검증), ③ 우선순위 수가 8 이하라면 그냥 표 하나. 메모리(표 크기) vs 사이클 trade-off. 꼬리: "M0 에서 ctz 는?" → clang 이 de Bruijn 을 직접 인라인했다 (`0x77cb531`, 관찰).

</details>

<details>
<summary>Q4. 같은 우선순위 태스크들은 어떻게 돌죠? 선점당한 태스크는 어디로 가나요?</summary>

같은 우선순위 = FIFO 라운드 로빈. 이 레슨은 실행 중인 태스크를 꺼내고 yield 때 꼬리에 다시 넣는다. FreeRTOS 는 실행 중인 태스크를 리스트에 **그대로 두고** `listGET_OWNER_OF_NEXT_ENTRY` 가 `pxIndex` 커서를 한 칸 옮긴다 (list.h:286–297). 새로 READY 된 태스크는 `listINSERT_END` 로 커서 **바로 앞** = 한 바퀴의 맨 끝에 들어간다 (list.h:362–385). 결과는 같은 라운드 로빈이다 (연습 2: `ABCABCDA|BCD`). 차이는 선점: A 가 돌다가 높은 H 에게 선점되고 H 가 block 하면, 커서는 아직 A 를 가리키므로 **다음 칸 B 가 돈다** — 연습 2 의 모델에서 `AHB`. 즉 이 코드대로라면 선점당한 태스크가 자기 차례를 잃는다 (list.h 코드 기준, 실기 미검증). 꼬리: "time slicing 은?" → tick 마다 `listCURRENT_LIST_LENGTH( &pxReadyTasksLists[ pxCurrentTCB->uxPriority ] ) > 1` 이면 전환 요청 (tasks.c:4967–4974, `configUSE_TIME_SLICING`) — 길이에 **실행 중인 자기도 포함**된다는 것이 "리스트에 남는다" 의 증거. 자세히는 RTOS-B05.

</details>

<details>
<summary>Q5. FreeRTOS optimised 선택은 왜 우선순위 32개가 한계죠? 256 개가 필요하면?</summary>

비트맵이 32비트 워드 하나이기 때문이다 (portmacro.h:161–163 `#error`, 메시지 그대로 "10 to 15" 면 충분하다고 한다). 우선순위마다 `List_t` 가 하나씩 RAM 을 먹으니(32비트 타깃, integrity check 끔, `configUSE_MINI_LIST_ITEM` 기본 1(FreeRTOS.h:608–609) 기준 필드 합 4+4+12 = 20바이트 — 구조체로 계산, 미측정) 실무에선 5–10개가 흔하다. 256 개가 필요하면 **2단 비트맵**: 그룹 워드의 비트 g = "워드 g 가 0 이 아님", 최고 = `32·hi(grp) + hi(w[g])` — CLZ 두 번 (연습 3, 무작위 10만 연산 대조 0 mismatch). µC/OS-III 와 리눅스 2.6 의 O(1) 스케줄러(140 우선순위 비트맵)도 같은 생각이다 (문서 기준, 미검증). 꼬리: "우선순위를 많이 쓰면 좋은가?" → 아니다. 같은 우선순위 + time slicing 이 더 단순할 때가 많고, RMA(RTOS-M11) 는 주기 순으로 몇 단계면 충분하다.

</details>

<details>
<summary>Q6. 왜 intrusive 이중 연결 리스트를 쓰나요? 배열 큐(링)로는 안 되나요?</summary>

ready list 에서 빼는 일은 머리에서만 일어나지 않는다. 태스크가 suspend/delete 되거나, 우선순위가 바뀌거나(상속 포함, RTOS-M04), 다른 코어로 옮겨질 때 **중간에서** 뺀다. 링 버퍼는 중간 삭제가 O(n). 이중 연결 + 링크가 TCB 안에 있으면(intrusive) TCB 포인터만으로 O(1) 삭제, 그리고 malloc 이 없다 — 노드가 TCB 와 함께 정적으로 할당된다. FreeRTOS 의 `xStateListItem` / `xEventListItem` 이 정확히 이것 (TCB 하나가 ready/delayed 리스트와 이벤트 대기 리스트에 동시에 들어갈 수 있게 두 개 — RTOS-B02). 꼬리: "`pvOwner` / `pxContainer` 는 왜?" → 리스트 노드에서 TCB 로(owner), 노드에서 소속 리스트로(container) 바로 가기 위해. 내 `in_rq` 는 container 의 축소판이다.

</details>

<details>
<summary>Q7. ready 상태인 태스크의 우선순위를 바꾸면 무슨 일이 생기나요?</summary>

리스트는 우선순위로 색인돼 있으므로 **옛 리스트에서 빼고, 숫자를 바꾸고, 새 리스트 꼬리에 넣어야** 한다. 순서를 틀리면(숫자 먼저) 삭제가 새 리스트를 건드려 구조가 깨진다 (연습 1 버그 판: 불변식 위반 2). FreeRTOS `vTaskPrioritySet` 은 바꾸기 전에 `uxPriorityUsedOnEntry = pxTCB->uxPriority` 를 저장하고 (tasks.c:2946), `listIS_CONTAINED_WITHIN( &pxReadyTasksLists[ uxPriorityUsedOnEntry ], ...)` 이면 `uxListRemove` → 비었으면 `portRESET_READY_PRIORITY( uxPriorityUsedOnEntry, ...)` → `prvAddTaskToReadyList` (tasks.c:2987–3004). block 상태면 숫자만 바꾼다. 꼬리: "뮤텍스 우선순위 상속에서도?" → 같은 경로다 — 상속받은 보유자를 새 우선순위 리스트로 옮긴다 (`xTaskPriorityInherit`, tasks.c:6745 의 6782–6791, RTOS-M04).

</details>

## 9. 추가 연습

### 연습 1 — `rq_set_prio` (vTaskPrioritySet)

READY 태스크는 옛 리스트에서 빼고 → 바꾸고 → 새 꼬리, block 태스크는 숫자만. 일부러 "숫자 먼저" 버그 판도 만들어 불변식 검사가 잡는지 보라.

<details>
<summary>답 보기</summary>

`if (t->in_rq) { rq_remove(q, t); t->prio = newp; rq_push_tail(q, t); } else t->prio = newp;`. 실제 출력:

```text
after set_prio: pop order cab
buggy set_prio: invariant violations = 2, head[5]=x (x 는 이미 p9 인데)
```

a(p5)→p9 는 기존 c(p9) **뒤**(꼬리)에 선다 → `cab`. 버그 판은 `rq_remove` 가 `prio=9` 로 계산해 `head[9]` 를 건드리고 `head[5]` 에는 x 가 그대로 남는다. 펌웨어에선 이게 "가끔 어떤 태스크가 안 돈다" 로 나타난다.

</details>

### 연습 2 — FreeRTOS 식: 실행 중에도 리스트에 남고 커서가 돈다

`list.h` 의 `listINSERT_END` / `listGET_OWNER_OF_NEXT_ENTRY` 와 `list.c` 의 `uxListRemove`(커서가 지워진 칸이면 앞으로 옮김, list.c:232–235)를 표지(sentinel) 원형 리스트로 옮겨 적고, 비트맵과 합쳐라. A,B,C 를 넣고 4번 고르기 → D 추가 → 4번 → A 삭제 → 3번. 그리고 A 실행 중 H(p5) 선점 → H block → 다음은?

<details>
<summary>답 보기</summary>

실제 출력: `select sequence: ABCABCDA|BCD`, `preempted A, then: AHB`. D 는 커서(A) 바로 앞 = 한 바퀴 맨 끝에 들어가 B, C 다음에 나온다 — 소스 주석 그대로 "every other item within the list will be returned ... before the pxIndex parameter again points to the item being inserted" (list.h:350–353). 선점 뒤에는 커서가 A 다음으로 넘어가 **B** 가 돈다. 본문 모델(pop/push-tail)이라면 A 가 머리에 남아 A 가 돈다 — 설계 차이를 설명할 수 있으면 된다.

</details>

### 연습 3 — 256 우선순위 2단 비트맵

`grp` 비트 g ⇔ `w[g] != 0`. set 은 두 비트, clear 는 워드가 0 이 될 때만 그룹 비트를 끈다. 최고 = `g*32 + hi(w[g])`.

<details>
<summary>답 보기</summary>

실제 출력: `bm256 random 100000 ops: mismatches 0, group-invariant violations 0` (256칸 bool 배열 선형 탐색과 대조). 테스트: {0, 37, 200} → 200, `grp == 0x43`, 200 을 지우면 37. 64비트 CPU 라면 `uint64_t` + `__builtin_clzll` 로 1단 64개, 2단 4096개. Cortex-M 은 32비트 CLZ 가 기본 단위.

</details>

### 연습 4 — CLZ 없는 M0: de Bruijn

`v |= v >> 1; ... v |= v >> 16;` 로 최상위 비트 아래를 채우면 v 는 2^(k+1) − 1 꼴의 32가지뿐이다. `v * 0x07C4ACDD` 의 위 5비트가 32가지 모두 다르게 나오도록 고른 상수 → 32칸 표.

<details>
<summary>답 보기</summary>

실제 출력: `de Bruijn vs 31-__builtin_clz: 969447 cases, mismatches 0` (단일 32 + 두 비트 496 + 무작위; 0 은 제외). `hb_debruijn(0x54) == 6`. 곱셈 1번 + 시프트 5번 + 표 1번 — M0 은 1사이클 곱셈기 옵션이 있는 칩이 많아(구현 선택, 칩마다 다름) 꽤 빠르다. 표 32바이트는 flash 에 둔다. 상수·표 출처: Sean Eron Anderson, *Bit Twiddling Hacks* ("Find the log base 2 of an N-bit integer in O(lg(N)) operations with multiply and lookup").

</details>

### 연습 5 — 작은 숫자가 급한 커널 (Zephyr / µC/OS 방향)

최하위 켜진 비트 = `x & -x` (unsigned 라 정의됨) → 그 비트 하나의 clz → `31 - clz`.

<details>
<summary>답 보기</summary>

실제 출력: `bm_lowest vs __builtin_ctz: mismatches 0`. `bm_lowest(0x54) == 2`. arm64·M4 에서 `__builtin_ctz` 는 `rbit + clz` 두 명령이었다(demo 관찰). 방향만 다르고 구조는 같다 — 면접에서 "우리 RTOS 는 0 이 최고 우선순위" 라고 하면 이 변환을 말하면 된다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 17/17 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o pr_asan && ./pr_asan   # 17/17, 경고 0
```

## 10. 핵심 정리

- ready list = **비트맵 1워드 + 우선순위별 FIFO**. 불변식 `bit p ⇔ head[p] != NULL` 하나만 지키면 고르기는 `31 - clz(bitmap)` → `head[p]`.
- push 는 꼬리 + 비트 켜기, remove 는 이중 연결 두 줄 + **비었을 때만** 비트 끄기. 모두 O(1), malloc 없음 (TCB 안의 intrusive 링크).
- `1u << p` (p=31 UB 방지), `clz(0)` 은 UB/32 — 비트맵 0 을 따로 처리하거나 idle 이 항상 READY 이게.
- arm64·Cortex-M3/M4/M7: `clz` 1명령. M0/M23: CLZ 없음 → `__clzsi2` 호출, de Bruijn/표, FreeRTOS 는 일반 C 경로.
- FreeRTOS: `configUSE_PORT_OPTIMISED_TASK_SELECTION` (CM3/CM4F 포트 기본 1, 최대 32 우선순위), `portGET_HIGHEST_PRIORITY = 31 - clz`, 리스트 비면 `taskRESET_READY_PRIORITY`.
- yield 는 **같거나 높은** 우선순위에게만 양보한다. 같은 우선순위 = FIFO 라운드 로빈, 낮은 우선순위는 높은 쪽이 block 할 때만 돈다.
- 우선순위 변경은 "옛 prio 로 빼고 → 바꾸고 → 새 꼬리" 순서.

## 참고자료

- FreeRTOS-Kernel V11.3.2 소스 (로컬 `~/.esn_scratch/FreeRTOS-Kernel`): `tasks.c` (178–261 task selection 매크로 두 경로, 287–293 `prvAddTaskToReadyList`, 478, 507, 2844–3004 `vTaskPrioritySet`, 4022–4025, 4967–4974 time slicing, 5215/5273 `vTaskSwitchContext`, 6745–6791 `xTaskPriorityInherit`), `include/list.h` (143–179 `ListItem_t`/`List_t`, 286–297, 346–385), `list.c` (217–247 `uxListRemove`), `include/FreeRTOS.h` (608–609, 2803–2805, 2917–2918), `portable/GCC/ARM_CM4F/portmacro.h` (144–171), `portable/GCC/ARM_CM4F/port.c:524–530`, `portable/GCC/ARM_CM33/non_secure/portmacrocommon.h` (554–600), `portable/GCC/RISC-V/portmacro.h` (136–153): <https://github.com/FreeRTOS/FreeRTOS-Kernel>
- FreeRTOS 문서 — `configUSE_PORT_OPTIMISED_TASK_SELECTION`, `configMAX_PRIORITIES`: <https://www.freertos.org> → Documentation → Kernel → Customization (FreeRTOSConfig.h)
- ARM, *ARMv7-M Architecture Reference Manual* — CLZ, RBIT (ARMv6-M 에는 없음): <https://developer.arm.com/documentation/ddi0403/latest>
- GCC 문서 — `__builtin_clz` ("If x is 0, the result is undefined"): <https://gcc.gnu.org/onlinedocs/gcc/Bit-Operation-Builtins.html>
- Henry S. Warren, *Hacker's Delight* 2판, 5-3 "Counting Leading 0's" (이진 탐색 nlz)
- Sean Eron Anderson, *Bit Twiddling Hacks* — de Bruijn log2 / trailing zeros: <https://graphics.stanford.edu/~seander/bithacks.html>
- Zephyr — Scheduling (ready queue: `CONFIG_SCHED_DUMB` / `SCALABLE` / `MULTIQ`, 우선순위 방향): <https://docs.zephyrproject.org/latest/kernel/services/scheduling/index.html>
- CMSIS-RTOS2 — Thread Management (`osThreadYield`, `osPriority_t`): <https://arm-software.github.io/CMSIS_6/latest/RTOS2/group__CMSIS__RTOS__ThreadMgmt.html>
- Jean J. Labrosse, *µC/OS-III: The Real-Time Kernel* — ready list 비트맵, `CPU_CntLeadZeros` (문서 기준)
- 관련 레슨: RTOS-B02 (TCB·상태 전이, xStateListItem/xEventListItem), RTOS-B03 (ucontext 문맥 교환), RTOS-B05 (선점·time slicing), RTOS-B06 (지연 리스트), RTOS-B09 (대기 리스트 우선순위→FIFO), RTOS-M04 (우선순위 상속 = 리스트 이동), RTOS-M11 (RMA), RTOS-A01 (Cortex-M PendSV 미니 RTOS), C-B01 (비트 연산), DS-B (연결 리스트)
