# T2-1 · SPSC Ring Buffer of Temperature Samples

> **ID**: T2-1 · **Priority**: P0 · **Pattern**: ring buffer + free-running indices · **Time**: 25 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh T2-1` / `./run.sh T2-1 sol`

## 1. Problem

A temperature sensor on the robot raises an interrupt every 10 ms. The **ISR** reads one sample (`int16_t`, tenths of a degree C) and pushes it into a ring buffer. The **control task** pops samples later, at its own pace. Implement the ring buffer.

- Capacity `RB_SIZE` is a **power of two**, and **all `RB_SIZE` slots are usable** (no "waste one slot" trick).
- `head` and `tail` are **free-running `uint32_t` counters**: they only ever increase (and wrap at 2³²). The slot index is `counter & RB_MASK`, and `count = head - tail`.
- One producer (the ISR) writes only `head`; one consumer (the task) writes only `tail`. No locks.
- `rb_push` on a full buffer returns `false` and **drops the new sample**.
- `rb_pop` / `rb_peek` on an empty buffer return `false` and leave `*out` untouched.

```c
#define RB_SIZE 8u                        /* must be a power of two */
#define RB_MASK (RB_SIZE - 1u)

typedef struct {
    int16_t buf[RB_SIZE];
    volatile uint32_t head;               /* written only by the producer (ISR)  */
    volatile uint32_t tail;               /* written only by the consumer (task) */
} TempRing;

void     rb_init (TempRing *rb);
uint32_t rb_count(const TempRing *rb);
bool     rb_push (TempRing *rb, int16_t v);
bool     rb_pop  (TempRing *rb, int16_t *out);
bool     rb_peek (const TempRing *rb, int16_t *out);
```

### Example

```
RB_SIZE = 8
push 801, 802, ..., 808   -> all true, count = 8 (full)
push 809                  -> false (dropped)
pop                       -> true, 801
push 809                  -> true
state: head = 9, tail = 1, count = 8
```

### Constraints

- `RB_SIZE` is a power of two, `2 <= RB_SIZE <= 2^31`.
- Exactly one producer context and one consumer context.
- The counters may run for weeks: they **will** wrap past `UINT32_MAX`.

### Clarifying questions to ask (first 3 minutes)

1. When the buffer is full, drop the **new** sample or overwrite the **oldest**?
2. Who calls push and who calls pop — ISR and task, or two threads? Single-core or multi-core?
3. Can I require the size to be a power of two?
4. Do I need all N slots, or is capacity N−1 acceptable?
5. Should pop on empty return an error code or block?

## 2. Your workspace

빈 에디터처럼 아래 `stub.c`를 복사해서 시작하거나, 로컬에서 `./run.sh T2-1`로 바로 테스트하세요. 테스트 6개가 들어 있습니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 개수는 뺄셈 한 번</summary>

`head`, `tail`을 `RB_SIZE`로 줄이지 말고 그냥 계속 증가시킵니다. 그러면 `count = head - tail`이고, `uint32_t` 뺄셈은 랩어라운드가 나도 정확합니다(모듈로 2³² 산술). 빈 상태는 `count == 0`, 꽉 찬 상태는 `count == RB_SIZE`입니다.

</details>

<details>
<summary>Hint 2 — 슬롯 위치는 마스크</summary>

`RB_SIZE`가 2의 거듭제곱이면 `counter % RB_SIZE == counter & (RB_SIZE - 1)`입니다. 나눗셈이 없고, 2³²가 `RB_SIZE`의 배수라서 카운터가 2³²에서 0으로 넘어가도 슬롯 순서가 끊기지 않습니다.

</details>

<details>
<summary>Hint 3 — 쓰는 순서</summary>

push: **데이터를 먼저 쓰고** 그다음 `head`를 올립니다. pop: **데이터를 먼저 읽고** 그다음 `tail`을 올립니다. 순서가 반대면 상대편이 아직 안 쓴(또는 이미 덮어쓴) 슬롯을 봅니다.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 그림으로 보기

카운터는 끝없이 증가하고, 배열 위치만 `& RB_MASK`로 접습니다. 아래는 `tail = 6`, `head = 11`인 순간입니다. 데이터는 슬롯 6, 7을 지나 0, 1, 2로 이어집니다.

<div class="chart" data-src="chart1.svg" data-caption="free-running 카운터: 개수 = 11 − 6 = 5. 슬롯 위치만 마스크로 접어서 7 다음에 0이 옵니다"></div>

### 4.2 빈 상태와 꽉 찬 상태

이 문제의 핵심 함정입니다. 마스크한 인덱스만 들고 있으면 빈 상태와 꽉 찬 상태가 **똑같이** `head == tail`로 보입니다.

<div class="chart" data-src="chart2.svg" data-caption="두 상태 모두 head &amp; 7 == tail &amp; 7 == 1. 구분은 head − tail(0 vs 8)로만 가능합니다"></div>

해결책은 세 가지이고, 면접에서 이름을 대면 좋습니다.

| 방법 | 용량 | 비용 |
|---|---|---|
| free-running 카운터 (이 풀이) | N | 크기가 2의 거듭제곱이어야 함 |
| 한 칸 비워 두기: full = `(head+1) % N == tail` | N−1 | 슬롯 1개 낭비, 크기 제약 없음 |
| 별도 `count` 변수 | N | 양쪽이 모두 `count`를 써서 SPSC에서도 원자적 갱신 필요 |

### 4.3 손으로 따라가기 (trace)

§1 예제(테스트 3번)를 따라갑니다.

| 단계 | 연산 | 결과 | head | tail | count = head − tail | 쓰거나 읽은 슬롯 |
|---|---|---|---|---|---|---|
| 0 | `rb_init` | – | 0 | 0 | 0 | – |
| 1 | push 801 | true | 1 | 0 | 1 | [0] = 801 |
| 2 | push 802 … 808 | true ×7 | 8 | 0 | 8 | [1]…[7] |
| 3 | push 809 | **false** (꽉 참) | 8 | 0 | 8 | – |
| 4 | pop | true, 801 | 8 | 1 | 7 | [0] 읽음 |
| 5 | push 809 | true | 9 | 1 | 8 | [8 & 7 = 0] = 809 |

마지막 상태가 chart2의 FULL 그림입니다. 다음 pop은 `tail & 7 = 1`에서 802를 꺼냅니다.

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `head`, `tail`을 지역 변수로 한 번 읽어(`h`, `t`) 씁니다. `volatile`이라 매번 메모리를 다시 읽기 때문에, 한 함수 안에서 값이 바뀌어 보이지 않게 하려는 것입니다.
- push는 `tail`을 **읽기만** 하고, pop은 `head`를 **읽기만** 합니다. 각 변수의 writer가 하나뿐이라서 락이 필요 없습니다(SPSC의 핵심).
- 테스트 6번은 카운터를 `UINT32_MAX - 2`에서 시작시켜 2³² 랩어라운드를 직접 검사합니다. `head`가 3으로 넘어가도 `3 - (UINT32_MAX - 2) = 6`이 됩니다.

### 4.5 복잡도

- 모든 연산 **O(1)**, 분기 1~2개.
- 메모리: `RB_SIZE × 2 B + 8 B`. 동적 할당 없음.

---

## 5. 임베디드 관점으로 말할 것

- **ISR producer / task consumer**: ISR에서는 짧게 `rb_push`만 하고 끝냅니다. 꽉 차서 버린 샘플 수를 `overrun` 카운터로 세 두면 현장에서 "task가 너무 느리다"를 바로 알 수 있습니다.
- **메모리 순서(memory ordering)**: 단일 코어 Cortex-M에서는 정렬된 32비트 store가 원자적이고, `volatile`이 컴파일러 재배치를 막아 대부분 충분합니다. 하지만 C 표준상 `volatile`은 **다른 일반 변수(`buf`)와의 순서**를 보장하지 않습니다. 멀티코어(A53 + M7 등)이거나 엄밀하게 쓰려면 C11 atomics를 씁니다(아래).

```c
/* producer */  buf[h & MASK] = v;  atomic_store_explicit(&rb->head, h + 1, memory_order_release);
/* consumer */  uint32_t h = atomic_load_explicit(&rb->head, memory_order_acquire);  /* then read buf */
```

release는 "이 store 전에 쓴 데이터가 먼저 보인다", acquire는 "이 load 뒤의 읽기가 앞으로 당겨지지 않는다"입니다. ARM에서는 `DMB` 명령으로 구현됩니다.

- **캐시 + DMA**: UART DMA가 버퍼를 직접 채우는 구조라면 캐시 무효화(invalidate)와 정렬이 추가로 필요합니다(M7 D-cache).
- 이 구조는 [T2-2 패킷 파서](../T2-2_packet_parser/index.html)로 바로 이어집니다: UART ISR이 바이트를 링버퍼에 넣고, task가 꺼내 파서에 한 바이트씩 먹입니다.

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| Why does `head - tail` still work after `head` wraps past `UINT32_MAX`? | 부호 없는 뺄셈은 모듈로 2³² 산술입니다. 실제 차이가 `RB_SIZE` 이하이고 2³²가 `RB_SIZE`의 배수이므로 결과가 정확합니다. |
| What if `RB_SIZE` is not a power of two? | 마스크 대신 `% RB_SIZE`를 써야 하고, 2³²가 `RB_SIZE`의 배수가 아니라서 카운터 랩어라운드 때 슬롯 순서가 깨집니다. 그때는 인덱스를 `0..N-1`로 유지하고 "한 칸 비우기" 방식을 씁니다. |
| Overwrite the oldest sample instead of dropping the new one? | producer가 `tail`을 건드려야 해서 SPSC가 깨집니다. 짧게 인터럽트를 막거나, consumer가 "건너뛴 개수"를 감지하는 시퀀스 번호 방식을 씁니다. |
| Two ISRs push into the same buffer? | MPSC가 되어 `head` 갱신이 경쟁합니다. 짧은 critical section(`__disable_irq`) 또는 CAS(`atomic_compare_exchange`) 루프가 필요합니다. |
| Is `volatile` enough? | 단일 코어 + 컴파일러 배리어 수준에서는 대개 동작하지만, 표준상 순서 보장이 없습니다. 정답은 C11 `memory_order_release/acquire` 또는 명시적 `__DMB()`입니다. |
| Pop many samples at once? | `n = min(count, want)`를 구하고, 끝 경계에서 두 번의 `memcpy`로 나눠 복사한 뒤 `tail += n`을 한 번만 씁니다. |

## 7. 자주 하는 실수

- `head == tail`만으로 empty/full 판단 → 꽉 찼을 때 비었다고 착각(테스트 3번 실패)
- `head`를 `% RB_SIZE`로 줄이면서 `count = head - tail` 사용 → 음수 랩어라운드로 엉뚱한 개수
- `rb->head++` 후에 데이터 쓰기 → consumer가 쓰기 전 슬롯을 읽음(순서 버그, 테스트로는 잘 안 잡힘)
- pop이 비었을 때 `*out`을 0으로 덮어씀 → 테스트 2번 실패
- `int` 카운터 사용 → 2³¹에서 signed overflow(UB). 반드시 `uint32_t`
