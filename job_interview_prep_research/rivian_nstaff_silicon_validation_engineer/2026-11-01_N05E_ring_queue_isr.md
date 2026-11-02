# N05E · 링버퍼 · 큐 · ISR 동시성
<!-- theme: pastel -->
> **묶음**: E · 링버퍼 · 큐 · ISR↔main 공유 데이터 · **문제 수**: 8 (★★★ E1 · E3) · **목표**: 문제당 종이 20분 + "이 변수는 누가 쓰나, barrier는 어디에"를 영어로 설명 · **선행**: [N05 목록](2026-11-01_N05_c_paper_drill_bank.html) · [C 정리 C5 링버퍼](2026-10-29_c_basics_cram.html) · [L5 SPSC 링 (hmScreenPrep)](hmScreenPrep/problems/L5_spsc_sensor_ring/index.html) · **이전/다음**: [N05D 버퍼·문자열·메모리](2026-11-01_N05D_buffers_strings_memory.html) / [N05F 메모리 테스트·무결성](2026-11-01_N05F_memtest_integrity.html) · **코드**: onsitePrep/n05/e/ (`./run.sh` → E1–E8 PASS)

## 0. TL;DR · 사용법

- **훈련하는 것**: 링버퍼 full/empty 판정 3방식(free-running · 한 칸 비우기 · count), ISR producer ↔ main consumer에서 **변수마다 writer 1명** 원칙, barrier 위치, IRQ 마스킹 구간 최소화, 공유 변수 레이스 찾기, DMA 버퍼 소유권.
- **외울 한 줄 ①**: "count = head − tail in unsigned; full is count == N; N must be a power of two." (E1)
- **외울 한 줄 ②**: "Producer: write data, then release-store head. Consumer: acquire-load head, then read data." (E3)
- **외울 한 줄 ③**: "Every shared variable has exactly one writer. If two contexts write it, I need IRQ masking or an atomic RMW." (E6 · E7)
- **★★★ = E1 · E3** (온사이트 확률 가장 높음). E6 · E7은 Staff 변별용 꼬리 질문 단골, E8은 센서·카메라 캡처 검증에서 자주 나오는 형태 [추정].
- **이미 한 것과 안 겹치게**: 기본 링(byte, put/get)과 C11 멀티코어 버전은 [C 정리 C5](2026-10-29_c_basics_cram.html), 2-thread 스트레스·batch pop·high_water는 [L5](hmScreenPrep/problems/L5_spsc_sensor_ring/index.html). 여기서는 uint16 + bulk read(E1), 임의 크기(E2), **UART RX ISR + overflow + Cortex-M 매핑**(E3)으로 변형.
- **ISR 시뮬레이션**: 테스트가 ISR 함수를 직접 호출(단일 스레드, 결정적). E6 · E7은 `irq_save()/irq_restore()`가 PRIMASK를 흉내 내고 `PREEMPT_POINT()`에서 IRQ를 "끼워 넣음" — 마스크 중이면 pending → restore 때 실행. E3만 pthread 스트레스(ASan + TSan 통과). 추천 순서: E1 → E3 → E2 → E6 → E7 → E4 → E8 → E5.

## 1. 문제지 (인쇄용 — 답 없음)

### E1 · Power-of-2 ring buffer (free-running indices)

Implement a ring of `uint16_t` ADC samples, capacity `RB_N` (a power of two). Use free-running 32-bit `head`/`tail` that are never wrapped manually; full and empty must be distinguishable without wasting a slot. Also implement a bulk read that copies up to `max` samples, oldest first.

```c
#define RB_N 8u
typedef struct { uint16_t buf[RB_N]; uint32_t head, tail; } rb_t;
bool   rb_push(rb_t *r, uint16_t v);                 /* false if full */
bool   rb_pop (rb_t *r, uint16_t *out);              /* false if empty */
size_t rb_read(rb_t *r, uint16_t *out, size_t max);  /* returns count */
```

Example: push 100..107 → 8× true, push 999 → false; `rb_read(out, 2)` → 2, out = {100, 101}.

### E2 · Arbitrary-size ring: count vs one-slot-empty

Capacity `QN = 5` (not a power of two). Implement two FIFOs of bytes: (a) with a `count` field, (b) "one slot always empty" with only `head`/`tail`. Compare them.

```c
#define QN 5u
typedef struct { uint8_t buf[QN]; uint32_t head, tail, count; } cq_t;
typedef struct { uint8_t buf[QN]; uint32_t head, tail; } sq_t;
bool cq_push(cq_t *q, uint8_t v);  bool cq_pop(cq_t *q, uint8_t *out);
bool sq_push(sq_t *q, uint8_t v);  bool sq_pop(sq_t *q, uint8_t *out);
uint32_t sq_count(const sq_t *q);
```

Example: from empty, `cq` accepts 5 pushes, `sq` accepts only 4; both pop 0,1,2,… in order.

### E3 · UART RX ISR → main loop SPSC ring

The UART RX interrupt calls `uart_rx_isr()`, which reads one byte from the data register (`uart_read_dr()`, reading it clears the IRQ) and pushes it into a 16-byte ring. The main loop calls `rx_getc()`. No locks, no IRQ masking. If the ring is full the ISR drops the byte and counts an overflow. Say exactly where `volatile` / barriers go, and what they become on a Cortex-M.

```c
uint8_t uart_read_dr(void);   /* given */
void uart_rx_isr(void);       /* producer */
int  rx_getc(void);           /* consumer: 0..255, or -1 if empty */
```

Example: ISR fires 19 times with bytes 0x40..0x52 → overflow = 3; `rx_getc()` returns 0x40..0x4F, then -1.

### E4 · Overwrite-oldest trace log

A fixed trace log of `TL_N = 4` events that never fails: when full, the newest event overwrites the oldest. `trace_dump` copies the newest `min(valid, max)` events into `out[]` in oldest-to-newest order.

```c
typedef struct { uint32_t ts; uint16_t code; uint16_t arg; } trace_ev_t;
typedef struct { trace_ev_t ev[4]; uint32_t next; bool wrapped; } trace_log_t;
void   trace_log (trace_log_t *t, uint32_t ts, uint16_t code, uint16_t arg);
size_t trace_dump(const trace_log_t *t, trace_ev_t *out, size_t max);
```

Example: log ts = 10..15 → dump(max 8) = ts {12, 13, 14, 15}; dump(max 2) = {14, 15}.

### E5 · Software timer list

Implement one-shot software timers driven by a periodic `timer_tick()`. `timer_add` must be O(n) and `timer_tick` must be O(1) plus the expired timers. Timers with the same expiry fire in the order they were added. A callback may re-add its own timer.

```c
typedef void (*timer_cb_t)(void *arg);
typedef struct sw_timer { struct sw_timer *next; uint32_t delta; timer_cb_t cb; void *arg; } sw_timer_t;
void timer_add(sw_timer_t *t, uint32_t ticks, timer_cb_t cb, void *arg);
void timer_tick(void);
```

Example: add A=3, B=1, C=3, D=5 ticks at t=0 → fires B@1, A@3, C@3, D@5.

### E6 · Event flags: ISR sets, main fetch-and-clears

ISRs signal the main loop by setting bits in a 32-bit event word. The main loop calls `evt_take(mask)` which returns the pending bits in `mask` and clears exactly those bits — atomically with respect to the ISR. Given `uint32_t irq_save(void)` / `void irq_restore(uint32_t)`. Also give a lock-free version.

```c
void     evt_set_from_isr(uint32_t mask);
uint32_t evt_take(uint32_t mask);
```

Example: ISR sets RX|TX → `evt_take(TX)` = TX, events now = RX; an RX that arrives during `evt_take` must not be lost.

### E7 · Find the race (paper)

For each snippet, say what goes wrong, show the interleaving, and fix it (single-core MCU, ISR preempts main, main never preempts an ISR).

```c
/* (1) */ volatile uint32_t rx_count;   void rx_isr(void){ rx_count++; }
          void main_consume(uint32_t n){ rx_count -= n; }
/* (2) */ static bool done; static uint32_t result;
          void dma_isr(void){ result = 0xC0FFEE; done = true; }
          uint32_t wait(void){ while (!done) {} done = false; return result; }
/* (3) */ volatile uint32_t tick_lo, tick_hi;  void tick_isr(void){ if (++tick_lo == 0) tick_hi++; }
          uint64_t ticks64(void){ return ((uint64_t)tick_hi << 32) | tick_lo; }
```

Example: (3) with hi = 0, lo = 0xFFFFFFFF and a tick between the two reads → returns 0x1_FFFFFFFF instead of 0x1_00000000.

### E8 · Ping-pong DMA buffer handoff + overrun

A DMA fills one of two buffers; on transfer-complete the ISR hands the full buffer to the CPU and re-arms the DMA on the other one. Track ownership per buffer. If the DMA completes while the CPU still owns the other buffer, count an overrun (data is dropped, the DMA refills the same buffer).

```c
typedef struct { uint16_t buf[2][4]; volatile uint8_t owner[2], dma_idx; volatile uint32_t overrun; } pingpong_t;
void pp_dma_done_isr(pingpong_t *p);
int  pp_acquire(const pingpong_t *p);       /* ready index or -1 */
void pp_release(pingpong_t *p, int i);
```

Example: frame0 done → acquire = 0; frame1 done while CPU holds 0 → overrun = 1; release(0); frame2 done → acquire = 1 (frame 1 lost).

## E1 · 2의 거듭제곱 링버퍼: free-running index (★★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 크기가 2^k인 `uint16_t` 링버퍼를, 계속 증가만 하는 32-bit `head`/`tail`로 만들고, 여러 개를 한 번에 꺼내는 bulk read까지 구현.
- **상황**: ADC/센서 샘플을 ISR이 넣고 main이 꺼내는 가장 흔한 큐. validation 펌웨어에서 "샘플을 하나도 잃지 않았나"를 확인하는 기본 부품이고, 온사이트 링버퍼 질문의 표준형 [추정].

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `rb_push(r, v)` | 샘플 1개 넣기 | 꽉 찼으면 `false` (새 값 버림) |
| `rb_pop(r, &out)` | 가장 오래된 샘플 1개 꺼내기 | 비었으면 `false` |
| `rb_read(r, out, max)` | 최대 `max`개를 오래된 순서로 복사 | 꺼낸 개수 반환, `max = 0`이면 0 |

- **예시를 말로 풀면**: 빈 링에 100..107 8개를 넣으면 다 성공, 9번째는 실패. 5개 넣고 `rb_read(max 2)` 하면 앞의 2개만 나오고 3개가 남음.
- **지켜야 할 조건**: 칸 낭비 없이 용량 = `RB_N` 전체. `head`/`tail`이 2^32를 넘어 wrap해도 맞아야 함. `%` 대신 mask.

### 1. Problem (English)

"We have an ADC ISR producing 16-bit samples and a main loop draining them. Write a fixed-size ring buffer, capacity a power of two, using free-running indices so you don't waste a slot. Add a bulk read for the consumer."

```c
#define RB_N 8u
typedef struct { uint16_t buf[RB_N]; uint32_t head, tail; } rb_t;
bool   rb_push(rb_t *r, uint16_t v);
bool   rb_pop (rb_t *r, uint16_t *out);
size_t rb_read(rb_t *r, uint16_t *out, size_t max);
```

- push 100..107 into empty → all true; push 999 → false; pop ×8 → 100..107; pop → false.
- push 0xA000..0xA004; `rb_read(out, 0)` → 0; `rb_read(out, 2)` → 2 {0xA000, 0xA001}; `rb_read(out, 16)` → 3.
- head = tail = 0xFFFFFFFE, push 5 → head = 3, count = 5, samples land in slots 6, 7, 0, 1, 2.

### 2. 먼저 물어볼 것 (clarify)

- "Is the capacity guaranteed to be a power of two?" → Yes, enforce with `_Static_assert`.
- "When full, drop the new sample or overwrite the oldest?" → Drop newest, return false (caller counts drops). Overwrite is E4.
- "Single producer, single consumer?" → Yes; concurrency details are E3, here just the data structure.
- "Element type and capacity fixed at compile time?" → Yes, `uint16_t`, `RB_N = 8`.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (2^k? drop vs overwrite?) |
| 2–5 | head=tail=0xFFFFFFFE 예시로 wrap 손 계산, 접근 한 문장 |
| 5–15 | struct → count 헬퍼 → push → pop → read |
| 15–18 | trace: wrap 지점 5개 push |
| 18–20 | 테스트 5개 + "ISR-safe로 만들려면" 확장 |

- 소리 내서: "Head and tail only ever increase. I mask them only when indexing the array, so count is just `head - tail` in unsigned arithmetic, and that stays correct across the 2^32 wrap because 2^32 is a multiple of N."
- 소리 내서: "Full is `count == N`, empty is `head == tail` — no wasted slot, no shared count variable."

<details><summary>힌트 1 (막히면 5분 후)</summary>

`head`, `tail`을 슬롯 번호가 아니라 "지금까지 쓴 개수 / 읽은 개수"로 생각하세요. 그러면 개수는 뺄셈 한 번입니다.

</details>

<details><summary>힌트 2</summary>

슬롯 = `idx & (RB_N - 1)`. 2^32는 RB_N의 배수라서 idx가 0xFFFFFFFF → 0 으로 넘어가도 슬롯 7 → 0 순서가 끊기지 않습니다. bulk read는 `n = min(count, max)`개를 `tail + i`로 복사한 뒤 `tail += n` 한 번.

</details>

<details><summary>모범 답 (C)</summary>

```c
#define RB_N    8u                               /* capacity, must be 2^k */
#define RB_MASK (RB_N - 1u)
_Static_assert(RB_N != 0 && (RB_N & (RB_N - 1u)) == 0, "RB_N must be a power of 2");

typedef struct {
    uint16_t buf[RB_N];
    uint32_t head;              /* total items ever written (free-running) */
    uint32_t tail;              /* total items ever read    (free-running) */
} rb_t;

static uint32_t rb_count(const rb_t *r) { return r->head - r->tail; } /* wrap-safe */
static uint32_t rb_space(const rb_t *r) { return RB_N - rb_count(r); }

bool rb_push(rb_t *r, uint16_t v)
{
    if (rb_count(r) == RB_N)
        return false;                            /* full: drop newest, caller counts */
    r->buf[r->head & RB_MASK] = v;               /* mask only when indexing */
    r->head++;
    return true;
}

bool rb_pop(rb_t *r, uint16_t *out)
{
    if (r->head == r->tail)
        return false;                            /* empty */
    *out = r->buf[r->tail & RB_MASK];
    r->tail++;
    return true;
}

size_t rb_read(rb_t *r, uint16_t *out, size_t max)   /* bulk pop, oldest first */
{
    size_t n = rb_count(r);
    if (n > max)
        n = max;
    for (size_t i = 0; i < n; i++)
        out[i] = r->buf[(r->tail + (uint32_t)i) & RB_MASK];
    r->tail += (uint32_t)n;                      /* release all slots at once */
    return n;
}
```

- 시간 O(1) push/pop, O(n) read · 공간 O(N), 낭비 슬롯 0.
- 파일: onsitePrep/n05/e/E1_pow2_ring.c (테스트: 3가지 시작 인덱스 × empty/full/FIFO/bulk + 2^32 wrap + 10만 회 랜덤 interleave vs 참조 모델).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

시작 head = tail = 0xFFFFFFFE, RB_N = 8 (mask 7), push 0,1,2,3,4:

| 스텝 | head (push 전) | slot = head & 7 | head (push 후) | count = head − tail |
|---|---|---|---|---|
| push 0 | 0xFFFFFFFE | 6 | 0xFFFFFFFF | 1 |
| push 1 | 0xFFFFFFFF | 7 | 0x00000000 | 2 |
| push 2 | 0x00000000 | 0 | 0x00000001 | 3 |
| push 4 | 0x00000002 | 2 | 0x00000003 | 5 (= 3 − 0xFFFFFFFE mod 2^32) |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- empty: pop → false, count 0, space N.
- full boundary: N개 push 성공, N+1번째 false, 그다음 pop은 첫 값.
- FIFO 순서: 넣은 순서대로 나옴.
- wrap: head = tail = 0xFFFFFFFC 또는 0xFFFFFFFF 근처에서 위 테스트 반복.
- bulk: `max = 0` → 0, `max < count` → 부분, `max > count` → 전부, 다시 → 0.
- 랜덤 interleave를 참조 카운터(wr, rd)와 비교 — 이 파일에서 10만 회.

</details>

### 4. 꼬리 질문

**Q1. Why not just use `% N` and allow any N?**

<details><summary>답</summary>

With free-running indices, `idx % N` is only continuous across the 2^32 wrap if N divides 2^32, i.e. N is a power of two. For N = 5, index 0xFFFFFFFF maps to slot 0 and the next index 0 also maps to slot 0, so the order breaks. For arbitrary N I'd wrap the indices explicitly and use one of the two schemes in E2.

한국어 핵심: free-running + `%`는 N이 2^32의 약수(= 2^k)일 때만 wrap에서 연속. 아니면 E2 방식.

</details>

**Q2. How would you make this safe between an ISR producer and a main-loop consumer?**

<details><summary>답</summary>

It's already structured for it: head is written only by the producer and tail only by the consumer, so there's no shared read-modify-write. What remains is ordering — the data write must be visible before the head update, and the data read must finish before the tail update. On a single-core MCU that's `volatile` plus a compiler barrier; on multi-core it's release/acquire. That's E3.

한국어 핵심: 단일 writer 구조라 lock 불필요, 남은 건 순서(data → index) 보장뿐.

</details>

**Q3. How would you make the bulk read faster for large buffers?**

<details><summary>답</summary>

Split it into at most two contiguous chunks — from the tail slot to the end of the array, then from slot 0 — and memcpy each. That's what Linux's `CIRC_CNT_TO_END` and kfifo do. It also lets a DMA engine drain the ring in one or two descriptors.

한국어 핵심: wrap 지점에서 2조각으로 나눠 memcpy (kfifo 방식).

</details>

### 5. 채점 · 자주 틀리는 것

- `head`/`tail`을 push 때 `& MASK`로 저장해 버림 → full과 empty가 둘 다 `head == tail`.
- count를 `int`로 계산하거나 `head > tail` 비교 → wrap에서 깨짐. 항상 `uint32_t` 뺄셈.
- `_Static_assert`로 2^k 강제 안 함.
- bulk read에서 `tail`을 루프 안에서 하나씩 올림 (동작은 맞지만 ISR과 공유 시 슬롯을 일찍 반납) — 복사 끝나고 한 번에.
- `max = 0`, empty일 때 0 반환 확인 안 함.

## E2 · 임의 크기 링버퍼: count 방식 vs 한 칸 비우기 방식 (★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 크기 5처럼 2의 거듭제곱이 아닌 링버퍼를 두 가지 방식(count 변수 / 한 칸 비우기)으로 짜고 장단점 비교.
- **상황**: 프로토콜이 "최대 5개 outstanding command"처럼 크기를 정해 주거나, RAM이 빠듯해서 2^k로 못 늘릴 때. 기존 코드 리뷰에서 둘 중 하나를 자주 만남.

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `cq_push / cq_pop` | count 방식 FIFO | 용량 = N (5개 다 사용) |
| `sq_push / sq_pop` | 한 칸 비우기 FIFO | 용량 = N − 1 (4개) |
| `sq_count` | 한 칸 비우기 방식의 현재 개수 | head, tail만으로 계산 |

- **예시를 말로 풀면**: 빈 상태에서 count 방식은 5개까지, 한 칸 비우기 방식은 4개까지 들어가고 그다음 push는 실패. 둘 다 넣은 순서대로 나옴.
- **지켜야 할 조건**: head/tail은 항상 0..N−1 범위. 비교 결과(어느 쪽이 ISR과 공유하기 좋은가)를 말로 설명.

### 1. Problem (English)

"Our command queue must hold exactly 5 entries, so we can't round up to a power of two. Implement it two ways — with a count field, and with one slot left empty — and tell me which one you'd use between an ISR and the main loop."

```c
#define QN 5u
typedef struct { uint8_t buf[QN]; uint32_t head, tail, count; } cq_t;
typedef struct { uint8_t buf[QN]; uint32_t head, tail; } sq_t;
bool cq_push(cq_t *q, uint8_t v);  bool cq_pop(cq_t *q, uint8_t *out);
bool sq_push(sq_t *q, uint8_t v);  bool sq_pop(sq_t *q, uint8_t *out);
uint32_t sq_count(const sq_t *q);
```

- empty → `cq_pop`, `sq_pop` false.
- push 0..4 → `cq` all true, 6th false; `sq` 0..3 true, 5th false, `sq_count` = 4.
- 10만 회 push/pop 섞어도 순서 · 개수 일치, head/tail < 5 유지.

### 2. 먼저 물어볼 것 (clarify)

- "Must all 5 slots be usable?" → For (a) yes; for (b) we accept capacity N−1 and discuss the trade-off.
- "Is it shared with an ISR?" → Discuss it; implement single-context first.
- "Is `%` acceptable?" → It's fine but `%` by a non-constant/non-2^k is a division on an M0 (no hardware divide) — I'll show a compare-and-reset `next()` for (b).

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (용량 N vs N−1, ISR 공유?) |
| 2–5 | 두 방식의 empty/full 조건 표로 그리기 |
| 5–15 | (a) 8줄 + (b) 10줄 + sq_count |
| 15–18 | N = 5에서 (b) full 직전 trace |
| 18–20 | 비교 한 문장 + 테스트 |

- 소리 내서: "With a count field both sides write `count`, so between an ISR and main it's a shared read-modify-write and needs IRQ masking. The one-empty-slot version keeps head producer-only and tail consumer-only, so it's lock-free for SPSC at the cost of one slot."
- 소리 내서: "Empty is `head == tail`; full is `next(head) == tail`."

<details><summary>힌트 1 (막히면 5분 후)</summary>

(b)에서 head == tail을 empty로 쓰면, full을 같은 조건으로 표현할 수 없습니다. 그래서 "head가 한 칸 더 가면 tail과 만난다"를 full로 정의합니다.

</details>

<details><summary>힌트 2</summary>

`sq_count = (head + N - tail) % N` — `+ N`을 먼저 해서 unsigned 언더플로를 피합니다. (a)는 head/tail이 아니라 `count`로만 empty/full 판단.

</details>

<details><summary>모범 답 (C)</summary>

```c
#define QN 5u                                    /* any size, NOT a power of 2 */

/* (a) count-based: storage N, capacity N. `count` is written by BOTH sides. */
typedef struct { uint8_t buf[QN]; uint32_t head, tail, count; } cq_t;

bool cq_push(cq_t *q, uint8_t v)
{
    if (q->count == QN) return false;            /* full */
    q->buf[q->head] = v;
    q->head = (q->head + 1u) % QN;
    q->count++;
    return true;
}

bool cq_pop(cq_t *q, uint8_t *out)
{
    if (q->count == 0) return false;             /* empty */
    *out = q->buf[q->tail];
    q->tail = (q->tail + 1u) % QN;
    q->count--;
    return true;
}

/* (b) one-slot-empty: storage N, capacity N-1. head: producer only, tail: consumer only. */
typedef struct { uint8_t buf[QN]; uint32_t head, tail; } sq_t;

static uint32_t sq_next(uint32_t i) { return (i + 1u == QN) ? 0u : i + 1u; } /* no % */

bool sq_push(sq_t *q, uint8_t v)
{
    uint32_t n = sq_next(q->head);
    if (n == q->tail) return false;              /* full: one slot always kept empty */
    q->buf[q->head] = v;
    q->head = n;
    return true;
}

bool sq_pop(sq_t *q, uint8_t *out)
{
    if (q->head == q->tail) return false;        /* empty */
    *out = q->buf[q->tail];
    q->tail = sq_next(q->tail);
    return true;
}

uint32_t sq_count(const sq_t *q) { return (q->head + QN - q->tail) % QN; }
```

- 시간 O(1) · 공간 O(N). (a) 용량 N, (b) 용량 N − 1.
- 비교 한 줄: (a)는 `count`를 양쪽이 씀 → ISR 공유 시 critical section 필요. (b)는 변수마다 writer 1명 → SPSC lock-free 가능.
- 파일: onsitePrep/n05/e/E2_any_size_ring.c (테스트 마지막: `0xFFFFFFFF % 5 == 0` 이고 `0 % 5 == 0` → free-running + `% 5`는 wrap에서 슬롯 0이 두 번 나옴을 assert로 확인).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

(b) 한 칸 비우기, N = 5, 빈 상태에서 push a, b, c, d, e:

| 스텝 | head 전 | next(head) | tail | 결과 | sq_count |
|---|---|---|---|---|---|
| push a | 0 | 1 | 0 | OK, head = 1 | 1 |
| push d | 3 | 4 | 0 | OK, head = 4 | 4 |
| push e | 4 | 0 | 0 | next == tail → full, false | 4 |
| pop | 4 | – | 0 → 1 | a 반환 | 3 = (4 + 5 − 1) % 5 |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- empty pop → false (두 방식 모두).
- full: (a) 5개 후 실패, (b) 4개 후 실패.
- FIFO 순서, 그리고 head가 4 → 0으로 도는 구간을 여러 번 통과.
- `sq_count`가 head < tail 상태(예: head = 1, tail = 3)에서도 맞는지.
- 랜덤 interleave vs 참조 카운터 + 인덱스 범위 불변식 `head < N`.

</details>

### 4. 꼬리 질문

**Q1. You want all N slots and lock-free SPSC with N not a power of two. Options?**

<details><summary>답</summary>

Keep head and tail in the range 0..2N−1 — a "mirror" bit. Empty is `head == tail`, full is `head == tail + N` modulo 2N, and the slot is `idx < N ? idx : idx - N`. Each side still writes only its own index. Alternatively free-running indices with a modulus, but only if N divides 2^32.

한국어 핵심: 인덱스를 0..2N−1로 돌리는 mirror 방식이면 칸 낭비 없이 단일 writer 유지.

</details>

**Q2. Why avoid `%` on a Cortex-M0?**

<details><summary>답</summary>

ARMv6-M has no hardware divide instruction, so `% 5` becomes a library call that takes tens of cycles — bad inside an ISR. A compare-and-reset `next()` is one compare and one branch. On M3/M4 there's `UDIV`, but compare-and-reset is still cheaper.

한국어 핵심: M0에는 나눗셈 명령이 없음 → ISR에서 `%` 대신 비교 후 0으로.

</details>

**Q3. In the count version, which side needs the critical section?**

<details><summary>답</summary>

Only the main-loop side. The ISR's `count++` can't be interrupted by main on a single core, but main's `count--` is load/subtract/store, and if the ISR runs between the load and the store, its increment is lost. So main disables IRQs around its update — exactly the E7 snippet (1).

한국어 핵심: main 쪽 RMW만 IRQ 마스킹 필요 (ISR은 main에게 선점당하지 않음).

</details>

### 5. 채점 · 자주 틀리는 것

- (b)에서 용량이 N − 1이라는 걸 테스트/설명에서 빼먹음.
- `sq_count`를 `head - tail`로 계산 → head < tail일 때 거대한 unsigned 값.
- (a)에서 empty/full을 head == tail로 판단 (count를 만들어 놓고 안 씀).
- free-running index에 `% 5`를 써도 된다고 말함 (2^32 wrap에서 깨짐).
- ISR 공유 시 (a)의 `count`가 공유 RMW라는 점을 말하지 않음.

## E3 · ISR producer / main consumer SPSC — volatile · barrier 어디에 (★★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: UART RX 인터럽트가 받은 바이트를 링에 넣고(producer), main 루프가 꺼내는(consumer) 락 없는 큐를 짜고, `volatile`과 barrier가 정확히 어디에 왜 들어가는지 설명.
- **상황**: 디버그 콘솔, 센서 UART, 모뎀 AT 명령 — bring-up 펌웨어에서 제일 먼저 만드는 ISR↔main 경로. 여기서 순서를 틀리면 "가끔 바이트가 틀리게 나옴" 같은 재현 어려운 버그가 됨.

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `uart_rx_isr()` | DR 읽기 → 링에 push | 절대 block/spin 안 함. 꽉 찼으면 버리고 `rx_overflow` +1. 꽉 찼어도 DR은 읽어야 함 |
| `rx_getc()` | 링에서 1바이트 꺼내기 | 0..255 반환, 비었으면 -1 |

- **예시를 말로 풀면**: 링 16칸에 ISR이 19번 들어오면 앞의 16바이트만 저장, 3바이트는 overflow로 카운트. main은 첫 16바이트를 순서대로 받고 그다음 -1.
- **지켜야 할 조건**: lock 없음, IRQ 마스킹 없음. `head`는 ISR만, `tail`은 main만 씀. 0xFF 바이트가 -1(empty)과 헷갈리면 안 됨. 인덱스 2^32 wrap에서도 정상.

### 1. Problem (English)

"Here's our UART receive interrupt. Write the ISR and the main-loop `getc` around a lock-free ring. Then walk me through where you need `volatile` or memory barriers, and what changes between a single-core Cortex-M and a multi-core application processor."

```c
uint8_t uart_read_dr(void);   /* given: read data register, clears RXNE */
void uart_rx_isr(void);
int  rx_getc(void);           /* -1 if empty */
```

- 19 ISRs with 0x40..0x52 into a 16-slot ring → `rx_overflow` = 3, `uart_read_dr` called 19 times; `rx_getc` → 0x40..0x4F, then -1.
- ISR with byte 0xFF → `rx_getc()` returns 255, not -1.
- head = tail = 0xFFFFFFF8 start → same results across the 2^32 wrap.

### 2. 먼저 물어볼 것 (clarify)

- "Single core, ISR versus main loop only? Any DMA or second core touching the buffer?" → Single core for the main answer; discuss multi-core as the extension.
- "When the ring is full — drop new, overwrite old, or assert flow control?" → Drop new and count; never block in the ISR.
- "Does reading the data register clear the interrupt?" → Yes, so the ISR must read it even when the ring is full, or the IRQ re-fires forever.
- "Do we have C11 atomics, or CMSIS intrinsics?" → Show C11 acquire/release, map to `volatile` + `__DMB()`.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (single-core? full 정책? DR read clears IRQ?) |
| 2–5 | "누가 무엇을 쓰나" 표 그리기: head = ISR, tail = main, buf[slot] = 소유권 이동 |
| 5–15 | ISR 10줄 + getc 9줄, barrier 위치에 번호 주석 |
| 15–18 | 순서가 뒤집힌 경우 trace (head 먼저 갱신 → main이 쓰레기 읽음) |
| 18–20 | 테스트 + Cortex-M / 멀티코어 차이 말하기 |

- 소리 내서: "Each index has one writer, so there's no read-modify-write race. The only issue is ordering: the producer must make the data visible before it publishes head, and the consumer must finish reading the slot before it publishes tail."
- 소리 내서: "On a single-core M4, the CPU sees its own accesses in order, so the real danger is the compiler reordering — `volatile` or a compiler barrier handles that. On multi-core or with a cache, I need a real DMB, which is what release/acquire compile to."

<details><summary>힌트 1 (막히면 5분 후)</summary>

종이에 공유 변수 3개(`buf[slot]`, `head`, `tail`)를 적고 각각 옆에 "writer: ISR / main"을 쓰세요. writer가 둘인 변수가 없으면 lock은 필요 없고, 남은 건 "어떤 쓰기가 어떤 쓰기보다 먼저 보여야 하나"입니다.

</details>

<details><summary>힌트 2</summary>

producer: data 쓰기 → (release) → head 쓰기. consumer: head 읽기 → (acquire) → data 읽기 → (release) → tail 쓰기. producer가 tail을 읽을 때도 acquire (consumer가 그 슬롯을 다 읽었음을 보장). full이어도 `uart_read_dr()`는 먼저 호출.

</details>

<details><summary>모범 답 (C)</summary>

```c
typedef _Atomic uint32_t idx_t;                  /* bare metal: volatile uint32_t */
#define LD_OWN(x)    atomic_load_explicit(&(x), memory_order_relaxed)       /* own index  */
#define ST_OWN(x, v) atomic_store_explicit(&(x), (v), memory_order_relaxed)
#define LD_ACQ(x)    atomic_load_explicit(&(x), memory_order_acquire)       /* M: v=x; __DMB() */
#define ST_REL(x, v) atomic_store_explicit(&(x), (v), memory_order_release) /* M: __DMB(); x=v */

#define RX_N 16u                                 /* 2^k */
static uint8_t rx_buf[RX_N];
static idx_t   rx_head;                          /* written ONLY by the ISR (producer) */
static idx_t   rx_tail;                          /* written ONLY by main    (consumer) */
static idx_t   rx_overflow;                      /* written ONLY by the ISR */

void uart_rx_isr(void)                           /* producer: never blocks, never spins */
{
    uint8_t  b = uart_read_dr();                 /* read DR even if full: clears RXNE */
    uint32_t h = LD_OWN(rx_head);
    uint32_t t = LD_ACQ(rx_tail);                /* consumer is done with the slot we reuse */
    if (h - t == RX_N) {
        ST_OWN(rx_overflow, LD_OWN(rx_overflow) + 1u);   /* drop newest, count it */
        return;
    }
    rx_buf[h & (RX_N - 1u)] = b;                 /* 1. write data                 */
    ST_REL(rx_head, h + 1u);                     /* 2. barrier, then publish head */
}

int rx_getc(void)                                /* consumer: byte, or -1 if empty */
{
    uint32_t t = LD_OWN(rx_tail);
    uint32_t h = LD_ACQ(rx_head);                /* see head first, THEN read data */
    if (h == t)
        return -1;
    uint8_t b = rx_buf[t & (RX_N - 1u)];
    ST_REL(rx_tail, t + 1u);                     /* finish reading, then free slot */
    return b;
}
```

- 시간 O(1) · 공간 O(N). 이 머신 검증: ASan/UBSan + **TSan(ThreadSanitizer) 둘 다 깨끗**, pthread producer가 200만 바이트를 넣고 main이 순서·값 전부 확인.
- 파일: onsitePrep/n05/e/E3_isr_spsc.c (run.sh는 `-pthread` 추가).

**host 코드 ↔ bare-metal 매핑** (종이에는 오른쪽 열로 써도 됨)

| host (C11) | 단일 코어 Cortex-M (ISR ↔ main) | 멀티코어 / M7 + cache / DMA 관여 |
|---|---|---|
| `idx_t` = `_Atomic uint32_t` | `volatile uint32_t` (aligned 32-bit 읽기·쓰기는 한 번에 일어남) | `_Atomic` 또는 `volatile` + barrier |
| `ST_REL(head, h+1)` | data 쓰기 → compiler barrier → `head = h+1` | `__DMB();` 후 `head = h+1` (ARMv8: `STLR`) |
| `LD_ACQ(head)` | `h = head;` → compiler barrier → data 읽기 | `h = head; __DMB();` (ARMv8: `LDAR`) |
| `LD_OWN(head)` | 자기 인덱스는 그냥 읽기 | 동일 |

- 단일 코어에서 CPU는 자기 접근 순서를 자기에게는 지키므로, ISR↔main 사이에서 막아야 하는 건 주로 **컴파일러 재배치** — `volatile` 끼리는 순서가 유지되지만 **non-volatile `buf[]`** 쓰기가 volatile `head` 쓰기 뒤로 밀릴 수 있어서 barrier(또는 buf도 volatile)가 필요. Arm은 이식성을 위해 DMB를 넣으라고 권장 [추정, Arm AN321 — 이번 세션에서 원문 열람 실패].
- compiler barrier = `__asm volatile("" ::: "memory")` 또는 C11 `atomic_signal_fence(memory_order_seq_cst)` (같은 스레드와 그 "signal handler" 사이 순서 = ISR 상황에 정확히 맞는 의미).
- 참고: 처음엔 relaxed load/store + `atomic_thread_fence`로 짰는데, 동작은 PASS였지만 **TSan이 `rx_buf`에 data race를 보고** → TSan이 standalone fence를 모델링하지 못하는 한계로 보임 [추정; GCC는 이 경우 `-Wtsan` 경고]. 그래서 release-store/acquire-load 형태로 바꿈 (Linux `circular-buffers.txt`도 같은 형태: `smp_store_release` / `smp_load_acquire`).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

**맞는 순서** vs **head를 먼저 쓴 버그** (slot 0, 바이트 0x41, 처음엔 slot 0에 이전 값 0x00):

| 시점 | 맞는 ISR | 버그 ISR (`head++` 먼저) | main이 보는 것 |
|---|---|---|---|
| t1 | `buf[0] = 0x41` | `head = 1` | 버그: head ≠ tail → slot 0 읽음 = **0x00 (쓰레기)** |
| t2 | release → `head = 1` | `buf[0] = 0x41` | 맞음: head = 1 확인 후 읽음 → 0x41 |
| t3 | – | – | main `tail = 1` (읽기 끝난 뒤) |

ISR은 main에게 선점되지 않으므로 "버그 ISR" 시나리오는 단일 코어에서는 **컴파일러가 store 순서를 바꾼 경우**에 일어나고, 멀티코어에서는 CPU 재정렬로도 일어남.

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- empty → -1.
- 16개 채우고 3개 더 → overflow 3, 그리고 **DR 읽기 19번** (IRQ ack 누락 검출).
- FIFO 순서 0x40..0x4F.
- 0xFF 바이트 → 255 반환 (`int` 반환이라 -1과 구분).
- 인덱스 0xFFFFFFF8 시작으로 wrap.
- 2-thread 스트레스 (producer thread = ISR 대역): 200만 바이트 순서 검증 + TSan.

</details>

### 4. 꼬리 질문

**Q1. Is `volatile` alone enough here?**

<details><summary>답</summary>

On a single-core MCU it's nearly enough if every shared object, including the buffer, is volatile, because the compiler keeps volatile accesses in order and the core observes its own program order. But `volatile` doesn't order a non-volatile buffer write against the volatile head write, and it does nothing about CPU or cache reordering on multi-core parts. So I use release/acquire, which on Cortex-M compiles to a DMB around the access.

한국어 핵심: volatile = 컴파일러가 접근을 없애거나 volatile끼리 재배치 못 하게 할 뿐, non-volatile 데이터와의 순서·CPU 재정렬은 못 막음.

</details>

**Q2. What changes if the consumer runs on a second core (e.g. Cortex-R or A53 cluster)?**

<details><summary>답</summary>

Now both CPU reordering and caches matter, so I need real barriers — `DMB` on ARMv7, or `STLR`/`LDAR` on ARMv8, which is exactly what C11 release/acquire emits. I'd also put head and tail on separate cache lines to avoid false sharing, and if the cores aren't cache-coherent, the buffer needs cache clean/invalidate or a non-cacheable region. The SPSC logic itself doesn't change.

한국어 핵심: 로직은 그대로, barrier가 실제 명령(DMB/STLR/LDAR)이 되고 head/tail 캐시 라인 분리 + 비코히런트면 cache 유지보수.

</details>

**Q3. The ISR needs to handle a 16-byte hardware FIFO, not one byte. What changes?**

<details><summary>답</summary>

Loop while the RX-not-empty flag is set, push each byte into the ring with a local head, and publish head once at the end with a single release store. That reduces barrier cost and keeps the ISR short. If the ring fills mid-burst, I still drain the hardware FIFO and count overflows so the interrupt clears.

한국어 핵심: 로컬 head로 여러 개 넣고 마지막에 한 번 publish, full이어도 FIFO는 끝까지 비움.

</details>

**Q4. How would you detect lost bytes during validation?**

<details><summary>답</summary>

Expose the overflow counter and the high-water mark, and in a test, send a known sequence (counter or PRBS) from the other side and check for gaps on the consumer. A nonzero overflow with no gap would point to the counter, a gap with zero overflow points to the hardware or the ISR latency (UART overrun error flag).

한국어 핵심: overflow 카운터 + high-water + 알려진 패턴 송신으로 gap 검사, UART OVR 플래그와 구분.

</details>

### 5. 채점 · 자주 틀리는 것

- head를 data보다 먼저 갱신 (가장 흔한 실수).
- consumer가 data 읽기 전에 tail을 먼저 갱신 → producer가 읽는 중인 슬롯을 덮음.
- full일 때 DR을 안 읽고 return → RXNE가 안 꺼져서 IRQ 폭주.
- ISR에서 full이면 대기(spin) — ISR은 절대 block 금지.
- `rx_getc`가 `uint8_t`를 반환해서 empty와 0xFF 구분 불가.
- "volatile이면 멀티코어도 OK"라고 말함.

## E4 · 덮어쓰기 trace 로그 링 + 오래된 순 dump (★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 꽉 차면 가장 오래된 이벤트를 덮어쓰는 고정 크기 trace 로그와, 최신 `max`개를 오래된 → 최신 순서로 꺼내는 dump.
- **상황**: crash/hang 직전의 마지막 N개 이벤트를 남기는 "flight recorder". SSD 펌웨어의 event log, SoC validation의 post-mortem trace와 같은 구조 — Don이 SSD 시절 쓰던 것과 같은 개념 [추정].

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `trace_log(t, ts, code, arg)` | 이벤트 1개 기록 | 절대 실패 안 함. 꽉 찼으면 가장 오래된 것 덮어씀 |
| `trace_dump(t, out, max)` | 최신 `min(유효 개수, max)`개를 오래된 순서로 복사 | 복사 개수 반환. 로그 자체는 안 바꿈 |

- **예시를 말로 풀면**: 4칸 로그에 ts 10..15 6개를 기록하면 10, 11은 덮여서 사라지고 12, 13, 14, 15가 남음. `max = 2`로 dump하면 가장 최근 14, 15.
- **지켜야 할 조건**: 아직 한 바퀴 안 돈 상태(유효 개수 < N)와 돈 상태를 구분. N은 2^k가 아니어도 됨.

### 1. Problem (English)

"We keep a tiny in-RAM trace log of the last few events so we can dump it after a hang. Logging must never fail — overwrite the oldest. Write `log` and a `dump` that returns the most recent events in chronological order."

```c
#define TL_N 4u
typedef struct { uint32_t ts; uint16_t code; uint16_t arg; } trace_ev_t;
typedef struct { trace_ev_t ev[TL_N]; uint32_t next; bool wrapped; } trace_log_t;
void   trace_log (trace_log_t *t, uint32_t ts, uint16_t code, uint16_t arg);
size_t trace_dump(const trace_log_t *t, trace_ev_t *out, size_t max);
```

- empty → dump returns 0.
- log ts 10, 11 → dump = {10, 11} (not wrapped).
- log ts 10..15 → dump(8) = {12, 13, 14, 15}; dump(2) = {14, 15}; dump(0) = 0.

### 2. 먼저 물어볼 것 (clarify)

- "When `max` is smaller than the number of events, which ones — oldest or newest?" → Newest; that's what you want after a crash.
- "Is `log` called from ISRs too?" → Assume one context for now; ISR-safe version is a follow-up.
- "Do we need to survive reset?" → Mention placing it in a no-init RAM section as an extension.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (newest vs oldest, ISR?) |
| 2–5 | 4칸 그림에 6개 기록, next/wrapped 변화 그리기 |
| 5–15 | log 5줄 + dump 7줄 |
| 15–18 | dump(2) trace |
| 18–20 | 테스트 + ISR/리셋 생존 확장 |

- 소리 내서: "`next` is where I write next. Once the log has wrapped, `next` is also the oldest entry, so the dump starts there; before it wraps, the oldest is slot 0."
- 소리 내서: "If max is smaller, I skip the oldest `n - max` entries so the dump shows the most recent events."

<details><summary>힌트 1 (막히면 5분 후)</summary>

`wrapped`가 false면 유효 = `next`개, 시작 = 0. true면 유효 = N개, 시작 = `next`.

</details>

<details><summary>힌트 2</summary>

`skip = n > max ? n - max : 0`, 복사 인덱스는 `(start + skip + i) % N`. 쓰기 쪽은 `++next == N`일 때 0으로 돌리고 `wrapped = true`.

</details>

<details><summary>모범 답 (C)</summary>

```c
#define TL_N 4u                                  /* any size */

typedef struct { uint32_t ts; uint16_t code; uint16_t arg; } trace_ev_t;

typedef struct {
    trace_ev_t ev[TL_N];
    uint32_t   next;          /* slot written next, 0..N-1 (= oldest once wrapped) */
    bool       wrapped;       /* set on first wrap: buffer is full from then on */
} trace_log_t;

void trace_log(trace_log_t *t, uint32_t ts, uint16_t code, uint16_t arg)
{
    t->ev[t->next] = (trace_ev_t){ ts, code, arg };   /* never fails: overwrite oldest */
    if (++t->next == TL_N) {
        t->next = 0;
        t->wrapped = true;
    }
}

/* copy the NEWEST min(valid, max) events into out[], oldest first */
size_t trace_dump(const trace_log_t *t, trace_ev_t *out, size_t max)
{
    size_t n     = t->wrapped ? TL_N : t->next;       /* valid entries */
    size_t start = t->wrapped ? t->next : 0;          /* index of the oldest */
    size_t skip  = (n > max) ? n - max : 0;           /* drop the oldest ones */
    for (size_t i = 0; i < n - skip; i++)
        out[i] = t->ev[(start + skip + i) % TL_N];
    return n - skip;
}
```

- 시간 O(1) log, O(min(n, max)) dump · 공간 O(N).
- 파일: onsitePrep/n05/e/E4_trace_log.c (테스트: empty, 부분, 정확히 full, 덮어쓰기, max 2/0, 1000회 연속 기록마다 "마지막 N개 순서" 확인).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

TL_N = 4, ts 10..15 기록 후 `trace_dump(max = 2)`:

| 단계 | 값 |
|---|---|
| ev[] 내용 | slot0 = 14, slot1 = 15, slot2 = 12, slot3 = 13 |
| next, wrapped | next = 2, wrapped = true |
| n, start | n = 4, start = 2 (= 가장 오래된 12) |
| skip | 4 − 2 = 2 |
| i = 0 | slot (2 + 2 + 0) % 4 = 0 → ts 14 |
| i = 1 | slot (2 + 2 + 1) % 4 = 1 → ts 15 |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- empty → 0.
- 부분 (wrapped 전) → slot 0부터.
- 정확히 N개 → wrapped = true, next = 0, dump = 전부 순서대로 (경계).
- 덮어쓰기 후 → 가장 오래된 것 사라짐.
- `max < n` → 최신 max개, `max = 0` → 0.
- 오래 기록하면서 매번 dump가 "마지막 N개 연속 ts"인지 (불변식).

</details>

### 4. 꼬리 질문

**Q1. Multiple ISRs at different priorities call `trace_log`. How do you make it safe?**

<details><summary>답</summary>

The slot reservation is the shared read-modify-write, so either mask IRQs for those few instructions, or reserve the slot with an atomic fetch-add on a free-running index and write the entry afterwards. With the atomic version, a dump may see a half-written entry, so I'd add a sequence number or a valid marker written last.

한국어 핵심: 슬롯 예약(`next++`)이 공유 RMW → IRQ 마스킹 또는 atomic fetch_add + 마지막에 seq 기록.

</details>

**Q2. How do you keep the log across a watchdog reset?**

<details><summary>답</summary>

Put the struct in a RAM section that the startup code doesn't zero (a `.noinit` section via the linker script), add a magic word and a checksum, and on boot check the magic before trusting it. After dumping, clear the magic.

한국어 핵심: `.noinit` 섹션 + magic + checksum, 부팅 시 검증 후 dump.

</details>

**Q3. Why a `wrapped` flag instead of a count?**

<details><summary>답</summary>

Either works; the flag is one bit of state that only changes once, so it's simpler to reason about. With a free-running write index and N a power of two, I wouldn't need either: valid = min(idx, N) and oldest = idx − valid.

한국어 핵심: 2^k + free-running index면 flag도 count도 필요 없음.

</details>

### 5. 채점 · 자주 틀리는 것

- wrap 전에도 `start = next`로 시작 → 비어 있는 슬롯을 출력.
- `max`가 작을 때 최신이 아니라 가장 오래된 것을 반환 (요구사항 확인 안 함).
- `next == N` 체크를 증가 전에 해서 off-by-one (슬롯 N에 씀 → 배열 밖).
- dump가 로그 상태를 바꿈 (`const` 아님).
- ISR에서 호출될 때의 공유 RMW를 언급 안 함.

## E5 · 소프트웨어 타이머 리스트: add · tick · 만료 콜백 (★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 1 ms tick 하나로 여러 개의 one-shot 타이머를 돌리는 delta list — add는 정렬 삽입, tick은 맨 앞만 1 감소.
- **상황**: RTOS 없는 bare-metal 테스트 펌웨어에서 timeout, 재시도, LED 깜빡임, 주기적 상태 폴링을 하나의 HW 타이머로 처리. RTOS의 timer list(FreeRTOS timer task 등)의 축소판.

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `timer_add(t, ticks, cb, arg)` | 지금부터 `ticks` 후 만료 타이머 등록 | 노드는 호출자 소유(malloc 없음). `ticks = 0`은 다음 tick. 같은 만료 시각이면 먼저 추가한 것이 먼저 |
| `timer_tick()` | 1 tick 경과 처리 | 만료된 것 모두 콜백. 콜백 안에서 다시 `timer_add` 가능 |

- **예시를 말로 풀면**: t = 0에 A 3, B 1, C 3, D 5 등록 → t = 1에 B, t = 3에 A 그리고 C(같은 시각, 추가 순서), t = 5에 D.
- **지켜야 할 조건**: tick은 타이머 개수와 무관하게 O(1) (만료 처리 제외). 각 노드는 "앞 노드 만료 후 몇 tick"(delta)만 저장.

### 1. Problem (English)

"We have one 1-ms tick interrupt and want many software one-shot timers. Make `tick` cheap — it shouldn't walk the whole list. Callers own the timer structs; no malloc."

```c
typedef void (*timer_cb_t)(void *arg);
typedef struct sw_timer { struct sw_timer *next; uint32_t delta; timer_cb_t cb; void *arg; } sw_timer_t;
void timer_add(sw_timer_t *t, uint32_t ticks, timer_cb_t cb, void *arg);
void timer_tick(void);
```

- add A3, B1, C3, D5, E0 at t = 0 → list deltas B1 E0 A2 C0 D2 → fires B@1, E@1, A@3, C@3, D@5.
- add A10, tick 4×, add B2, C6 → B@6, A@10, C@10.
- periodic: callback re-adds itself with 3 → fires at 3, 6, 9, 12.

### 2. 먼저 물어볼 것 (clarify)

- "One-shot or periodic?" → One-shot; periodic = re-add in the callback.
- "Is `timer_add` called from main while `tick` runs in the ISR?" → Single context for the code; discuss the critical section.
- "What does ticks = 0 mean?" → Fire on the next tick, never synchronously inside `add`.
- "Do I need cancel?" → Follow-up.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (one-shot, ticks = 0, ISR?) |
| 2–5 | 예시로 delta list 그리기: 절대 시각 1, 3, 3, 5 → delta 1, 2, 0, 2 |
| 5–15 | add (pointer-to-pointer 삽입) + tick |
| 15–18 | add C3 trace (delta 빼면서 이동) |
| 18–20 | 테스트 + cancel / ISR 안전 확장 |

- 소리 내서: "Each node stores ticks relative to the previous node, so on every tick I only decrement the head, and everything with delta zero behind it expires in the same tick."
- 소리 내서: "When inserting I subtract each node's delta as I walk; after inserting I subtract my delta from the successor so its absolute time doesn't change."

<details><summary>힌트 1 (막히면 5분 후)</summary>

절대 만료 시각이 5, 8, 8, 12라면 delta는 5, 3, 0, 4. 삽입할 때 "남은 ticks"가 다음 노드 delta보다 크거나 같은 동안 빼면서 앞으로 갑니다.

</details>

<details><summary>힌트 2</summary>

`sw_timer_t **pp = &g_head`로 걸으면 head 삽입 특수 처리가 없어집니다. 같은 만료 시각을 FIFO로 하려면 비교를 `<=`로. tick에서는 콜백 **전에** unlink (콜백이 같은 노드를 다시 add할 수 있으니까).

</details>

<details><summary>모범 답 (C)</summary>

```c
typedef void (*timer_cb_t)(void *arg);

typedef struct sw_timer {
    struct sw_timer *next;
    uint32_t   delta;          /* ticks AFTER the previous node expires */
    timer_cb_t cb;
    void      *arg;
} sw_timer_t;                  /* caller owns the node: no malloc */

static sw_timer_t *g_head;     /* sorted by expiry; head->delta = ticks until first */

void timer_add(sw_timer_t *t, uint32_t ticks, timer_cb_t cb, void *arg)
{
    sw_timer_t **pp = &g_head;
    if (ticks == 0)
        ticks = 1;                               /* fire on next tick, never "now" */
    while (*pp && (*pp)->delta <= ticks) {       /* <= : equal expiry stays FIFO */
        ticks -= (*pp)->delta;
        pp = &(*pp)->next;
    }
    t->delta = ticks; t->cb = cb; t->arg = arg;
    t->next = *pp;
    if (t->next)
        t->next->delta -= ticks;                 /* successor now relative to t */
    *pp = t;
}

void timer_tick(void)                            /* O(1) per tick + expired ones */
{
    if (!g_head)
        return;
    g_head->delta--;                             /* only the head counts down */
    while (g_head && g_head->delta == 0) {
        sw_timer_t *t = g_head;
        g_head = t->next;                        /* unlink BEFORE cb: cb may re-add */
        t->cb(t->arg);
    }
}
```

- 시간: add O(n), tick O(1) + 만료 개수 · 공간: 노드당 포인터 1 + delta.
- 파일: onsitePrep/n05/e/E5_sw_timers.c (테스트: 빈 리스트 tick, delta 리스트 모양 assert, 같은 시각 FIFO, ticks = 0, 실행 중 중간 삽입, 콜백에서 재등록 주기 타이머).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

리스트 B1 → A2 (절대 1, 3) 상태에서 `timer_add(C, 3)`:

| 단계 | *pp | (*pp)->delta ≤ ticks? | ticks |
|---|---|---|---|
| 시작 | B (1) | 1 ≤ 3 예 → 빼고 전진 | 2 |
| 2 | A (2) | 2 ≤ 2 예 (`<=` → A 뒤로) | 0 |
| 3 | NULL | 끝 | 0 |
| 삽입 | C.delta = 0, C.next = NULL | 후속 없음 | 리스트 B1 A2 C0 |

tick 3번: t1 B.delta 1→0 → B 실행 / t2 A 2→1 / t3 A 1→0 → A 실행, 이어서 C(0) 실행.

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 빈 리스트에서 tick → 아무 일 없음, crash 없음.
- 순서 섞어 add → 만료 순서·시각 확인.
- 같은 만료 시각 → 추가 순서대로 (FIFO).
- `ticks = 0` → 다음 tick.
- 리스트가 이미 진행 중일 때 중간 삽입 → 기존 타이머 시각 안 바뀜.
- 콜백에서 자기 자신 재등록 → 주기 동작.

</details>

### 4. 꼬리 질문

**Q1. `timer_tick` runs in the SysTick ISR and `timer_add` in main. What breaks and how do you fix it?**

<details><summary>답</summary>

`timer_add` walks and relinks the list and adjusts the successor's delta; if the tick ISR decrements the head or pops it in the middle, the list or the deltas get corrupted. The simplest fix is to mask the tick interrupt around `timer_add`. Better for latency: the ISR only increments a pending-ticks counter and the main loop calls `timer_tick` that many times, so the list has a single owner.

한국어 핵심: 리스트 소유자를 하나로 — ISR은 pending tick 카운트만 올리고 main이 처리 (또는 add 동안 IRQ 마스킹).

</details>

**Q2. Add `timer_cancel`.**

<details><summary>답</summary>

Walk with a pointer-to-pointer until I find the node, unlink it, and add its delta to the successor so later timers keep their absolute times. Return false if it wasn't in the list (already fired). It's O(n); a doubly linked list makes it O(1).

한국어 핵심: 지울 때 자기 delta를 다음 노드에 더해 줘야 함.

```c
bool timer_cancel(sw_timer_t *t) {
    for (sw_timer_t **pp = &g_head; *pp; pp = &(*pp)->next)
        if (*pp == t) { if (t->next) t->next->delta += t->delta; *pp = t->next; return true; }
    return false;
}
```

검증: 이 스니펫은 E5_sw_timers.c 안에 그대로 있고 테스트(취소 후 뒤 타이머 시각 유지, 두 번 취소 → false, 이미 만료 → false) PASS.

</details>

**Q3. Why a delta list rather than storing absolute expiry times?**

<details><summary>답</summary>

With absolute times, tick only compares the head's expiry against `now`, which is also O(1), but I must compare with wrap-safe unsigned subtraction `(int32_t)(expiry - now) <= 0`. The delta list avoids the wrap question entirely and never needs a global `now`; the cost is that insertion has to do the subtraction walk.

한국어 핵심: 절대 시각 방식도 O(1)이지만 32-bit wrap 비교를 조심해야 함; delta는 wrap 문제 없음.

</details>

### 5. 채점 · 자주 틀리는 것

- 삽입 후 후속 노드의 delta를 안 빼서 뒤 타이머들이 늦게 터짐.
- `<` 비교로 같은 시각 타이머가 역순(LIFO)으로 실행.
- 콜백 후 unlink → 콜백이 재등록하면 리스트 꼬임.
- delta 0인 후속 노드를 같은 tick에서 처리 안 함 (`if` 대신 `while`).
- `ticks = 0` 처리 없음 → head delta 0에서 `--` 하면 0xFFFFFFFF.

## E6 · 이벤트 플래그: ISR이 비트 set → main이 atomic fetch-and-clear (★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: 여러 ISR이 32-bit 이벤트 워드에 비트를 세우고, main이 원하는 비트만 "읽고 지우기"를 ISR에 대해 원자적으로 하는 함수 + lock-free 버전.
- **상황**: super-loop 펌웨어의 기본 구조 — ISR은 짧게 플래그만, 실제 처리는 main. "가끔 이벤트 하나가 안 처리됨" 버그의 단골 원인이 바로 read와 clear 사이의 틈.

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `evt_set_from_isr(mask)` | ISR이 비트 set | 비트를 세우기만 함 |
| `evt_take(mask)` | `mask` 중 pending 비트 반환 + 그 비트만 clear | IRQ 마스킹 구간 최소, 이전 PRIMASK 복원 |
| `evt_set_a` / `evt_take_a` | 같은 동작의 lock-free 버전 | C11 atomics |

- **예시를 말로 풀면**: ISR이 RX와 TX를 세움 → main이 TX만 가져가면 TX 반환, RX는 남음. main이 읽은 직후 RX ISR이 또 오면, 그 RX는 사라지면 안 됨.
- **지켜야 할 조건**: `irq_save()/irq_restore()`는 주어짐 (테스트에서는 시뮬레이션: 마스크 중 IRQ는 pending → restore 때 실행). 중첩 호출(이미 마스크된 상태에서 호출)에서 IRQ를 멋대로 켜면 안 됨.

### 1. Problem (English)

"ISRs post events by setting bits. The main loop calls `evt_take(mask)` to get and clear the bits it handles. Make it race-free against the ISRs with the shortest possible critical section, then show me a version without disabling interrupts."

```c
uint32_t irq_save(void);          /* given: returns old PRIMASK, then masks IRQs */
void     irq_restore(uint32_t k); /* given */
void     evt_set_from_isr(uint32_t mask);
uint32_t evt_take(uint32_t mask);
```

- set RX|TX → `evt_take(TX)` = TX, events = RX → `evt_take(~0)` = RX, events = 0.
- events = TX, RX ISR fires between read and clear → `evt_take(~0)` = TX, and RX is still pending afterwards.
- called with IRQs already masked → still masked on return.

### 2. 먼저 물어볼 것 (clarify)

- "Can a higher-priority ISR preempt a lower one that is also setting bits?" → Assume no nesting for the masked version; the atomic version handles it.
- "Events are flags, not counts — two RX interrupts before main runs can merge into one?" → Yes; if counts matter, use a ring (E3).
- "Which core?" → Cortex-M4 (has LDREX/STREX); mention M0.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (nesting? flag vs count?) |
| 2–5 | 버그 버전 interleaving 그리기: read → ISR set → `g_events = 0` |
| 5–15 | ISR 1줄 + take 6줄 + atomic 2줄 |
| 15–18 | ISR이 critical section 안에서 들어오는 trace |
| 18–20 | 테스트 + WFI 레이스 확장 |

- 소리 내서: "The ISR side is a read-modify-write too, but main can't preempt an ISR on a single core, so only main's clear needs protection."
- 소리 내서: "I clear only the bits I'm returning, and I restore the previous PRIMASK instead of blindly enabling, so it's safe to call from an already-masked region."

<details><summary>힌트 1 (막히면 5분 후)</summary>

버그는 "읽은 값"과 "지우는 값"이 다를 수 있다는 것. 지울 때는 `0`이 아니라 `~got`로 AND 하세요. 그리고 read와 clear를 IRQ 마스킹 안에 넣습니다.

</details>

<details><summary>힌트 2</summary>

lock-free: `atomic_fetch_and(&ev, ~mask)`는 **이전 값**을 반환 → 거기에 `& mask` 하면 가져간 비트. ISR 쪽은 `atomic_fetch_or`.

</details>

<details><summary>모범 답 (C)</summary>

```c
static volatile uint32_t g_events;               /* bit i = event i pending */

void evt_set_from_isr(uint32_t mask)             /* ISR side: only SETS bits */
{
    g_events |= mask;      /* RMW is OK here: main can't preempt an ISR (single core).
                              If a higher-priority ISR also sets bits -> use the atomic version. */
}

uint32_t evt_take(uint32_t mask)                 /* main: fetch-and-clear `mask` bits */
{
    uint32_t key = irq_save();                   /* save PRIMASK, then cpsid i */
    uint32_t got = g_events & mask;
    PREEMPT_POINT();                             /* sim: an IRQ here is held pending */
    g_events &= ~got;                            /* clear ONLY what we return */
    irq_restore(key);                            /* restore, don't blindly enable */
    return got;                                  /* critical section = 3 instructions */
}

/* lock-free alternative: C11 atomics (Cortex-M3+: LDREX/STREX loop; M0: not available) */
static _Atomic uint32_t g_events_a;
void evt_set_a(uint32_t mask)
{
    atomic_fetch_or_explicit(&g_events_a, mask, memory_order_release);
}
uint32_t evt_take_a(uint32_t mask)
{
    return atomic_fetch_and_explicit(&g_events_a, ~mask, memory_order_acquire) & mask;
}
```

- 시간 O(1) · critical section = load, AND, store 3개 명령.
- `PREEMPT_POINT()`는 테스트용 시뮬레이션 훅 — 종이에는 안 써도 됨. 테스트는 버그 버전(`g_events = 0`)이 RX를 잃는 것과, 고친 버전에서 RX가 pending → restore 후 전달되는 것, 중첩 마스크에서 안쪽 restore가 IRQ를 켜지 않는 것을 assert.
- 파일: onsitePrep/n05/e/E6_event_flags.c.

**Cortex-M 매핑**

- `irq_save()` = `k = __get_PRIMASK(); __disable_irq();` (`cpsid i`), `irq_restore(k)` = `__set_PRIMASK(k);` (CMSIS-Core).
- 모든 IRQ 대신 낮은 우선순위만 막고 싶으면 `BASEPRI` (ARMv7-M 이상, M0에는 없음).
- `atomic_fetch_or/and` → ARMv7-M(M3/M4/M7)에서는 `LDREX`/`STREX` 재시도 루프. 예외 진입/복귀 때 local exclusive monitor가 clear되므로 ISR이 끼어들면 `STREX`가 실패하고 다시 시도 → ISR에 대해 원자적 [추정 — 아키텍처 매뉴얼 기억].
- ARMv6-M(M0/M0+)에는 `LDREX`/`STREX`가 없음 → atomic RMW가 lock-free가 아니라 라이브러리 호출(`__atomic_fetch_or_4` 등)이 되거나 링크 실패 [추정 — 툴체인마다 다름]. M0에서는 PRIMASK 버전이 정답.

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

시작 events = TX, `evt_take(~0)` 중간에 RX IRQ 도착:

| 시점 | 버그 버전 (`got = ev; ...; ev = 0`) | 고친 버전 (마스킹 + `&= ~got`) |
|---|---|---|
| t1 | got = TX | PRIMASK = 1, got = TX |
| t2 | RX ISR 실행 → ev = TX 와 RX 둘 다 | RX IRQ는 pending (실행 안 됨) |
| t3 | ev = 0 → **RX 사라짐** | ev &= ~TX → ev = 0 |
| t4 | return TX | PRIMASK 복원 → RX ISR 실행 → ev = RX (보존) |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 아무것도 없을 때 → 0.
- 같은 비트 두 번 set → 한 번만 보임 (flag 의미).
- 일부 비트만 take → 나머지 그대로.
- 비트 31 (부호 비트 경계, `1u << 31`).
- critical section 중 IRQ 도착 → 잃지 않음 (버그 버전은 잃는 것도 같이 보여 줌).
- 이미 마스크된 상태에서 호출 → 리턴 후에도 마스크 유지.

</details>

### 4. 꼬리 질문

**Q1. The main loop sleeps with WFI when there are no events. What race is there and how do you fix it?**

<details><summary>답</summary>

If I check "no events" and then an interrupt sets a bit just before WFI, I go to sleep with a pending event and may never wake. The fix is to mask interrupts, check the flags, and execute WFI with PRIMASK still set — a pending interrupt still wakes the core from WFI — then unmask so the ISR runs. So: `cpsid i; if (!events) __WFI(); cpsie i;`.

한국어 핵심: 마스크한 채로 검사 → WFI → 해제. PRIMASK = 1이어도 pending IRQ는 WFI를 깨움.

</details>

**Q2. Why not just make `g_events` `volatile` and write `g_events &= ~mask` in main?**

<details><summary>답</summary>

`volatile` makes the compiler do the load and the store, but it's still two separate accesses with an ALU op in between. An ISR in that window sets a bit that the store then overwrites. `volatile` is about visibility, not atomicity.

한국어 핵심: volatile ≠ atomic. `&=`는 load·AND·store 3단계.

</details>

**Q3. What if two cores post events?**

<details><summary>답</summary>

Masking interrupts on one core does nothing to the other, so the PRIMASK version breaks. I'd use the atomic version — LDREX/STREX or LSE atomics on ARMv8.1 — or a hardware mailbox/semaphore block if the cores don't share coherent memory.

한국어 핵심: 멀티코어에서는 IRQ 마스킹이 무의미 → atomic RMW 또는 HW mailbox.

</details>

### 5. 채점 · 자주 틀리는 것

- clear를 `g_events = 0`으로 → 읽은 뒤 들어온 비트 손실.
- `__enable_irq()`로 끝냄 → 중첩 호출 시 바깥 critical section을 깸. 이전 상태 복원.
- critical section 안에서 이벤트 처리(콜백)까지 함 → IRQ latency 증가.
- `atomic_fetch_and`의 반환값이 **이전 값**이라는 걸 놓치고 `& mask`를 빼먹음.
- `1 << 31` (signed) — `1u << 31`.

## E7 · 레이스 찾기 (종이): ISR과 main이 공유하는 counter/flag 코드 3개 (★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: ISR과 main이 공유하는 짧은 코드 3개 — 카운터, 완료 플래그, 64-bit tick — 에서 버그를 찾고, 터지는 interleaving을 보여 주고, 고치기.
- **상황**: 온사이트 "코드 리뷰해 보세요"형 질문, 또는 "가끔 카운터가 1 모자라요 / 가끔 루프가 안 끝나요 / 가끔 타임스탬프가 40억 tick 튀어요" 디버그 질문 [추정].

| 코드 | 공유 데이터 | 찾을 것 |
|---|---|---|
| (1) | `rx_count` (ISR `++`, main `-= n`) | 어느 쪽 RMW가 깨지나, 어떻게 고치나 |
| (2) | `done` 플래그 + `result` | 컴파일러가 루프를 어떻게 바꾸나, 데이터 순서 |
| (3) | `tick_lo`, `tick_hi` (32-bit CPU에서 64-bit) | 찢어진 읽기(tearing) 값과 lock 없는 수정 |

- **예시를 말로 풀면**: (3)에서 hi = 0, lo = 0xFFFFFFFF일 때 main이 lo를 읽고 hi를 읽기 전에 tick이 오면 결과가 2^32 + 0xFFFFFFFF — 실제보다 약 2^32 tick 앞.
- **지켜야 할 조건**: 단일 코어, ISR은 main을 선점하지만 main은 ISR을 선점 못 함. 수정은 최소한으로 (IRQ 마스킹은 꼭 필요한 곳만).

### 1. Problem (English)

"Here are three snippets from a driver where an ISR and the main loop share data. For each: what's the bug, show me an interleaving that triggers it, and fix it."

```c
/* (1) */ volatile uint32_t rx_count;   void rx_isr(void){ rx_count++; }
          void main_consume(uint32_t n){ rx_count -= n; }
/* (2) */ static bool done; static uint32_t result;
          void dma_isr(void){ result = 0xC0FFEE; done = true; }
          uint32_t wait(void){ while (!done) {} done = false; return result; }
/* (3) */ volatile uint32_t tick_lo, tick_hi;  void tick_isr(void){ if (++tick_lo == 0) tick_hi++; }
          uint64_t ticks64(void){ return ((uint64_t)tick_hi << 32) | tick_lo; }
```

- (1) count = 5, main consumes 2, an RX interrupt lands between main's load and store → count = 3 (should be 4).
- (2) at -O2 the loop can become `if (!done) for(;;);` → hangs forever.
- (3) hi = 0, lo = 0xFFFFFFFF, tick between reads → 0x1_FFFFFFFF (should be 0x1_00000000).

### 2. 먼저 물어볼 것 (clarify)

- "Single core? Can ISRs nest?" → Single core, no nesting.
- "Is the CPU 32-bit with no 64-bit atomic load?" → Yes (Cortex-M4).
- "Can I change the ISR, or only main?" → Either, minimal change preferred.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify |
| 2–5 | 각 코드마다 "writer가 둘인가? 여러 번 접근하나?" 표시 |
| 5–15 | 3개 수정 코드 |
| 15–18 | (1)과 (3) interleaving trace |
| 18–20 | 테스트 방법 (시뮬레이션 주입) |

- 소리 내서: "For each shared variable I ask two questions: does more than one context write it, and is it accessed in more than one instruction? Yes to either means a race."
- 소리 내서: "For (3) I don't need to disable interrupts — read high, read low, re-read high, and retry if high changed."

<details><summary>힌트 1 (막히면 5분 후)</summary>

(1) `-=`는 load · sub · store. ISR은 main을 선점하지만 반대는 아님 → 어느 쪽만 보호하면 될까? (2) `done`이 non-volatile이면 컴파일러는 루프 안에서 `done`이 바뀔 수 없다고 가정. (3) 두 워드를 서로 다른 시점에 읽음.

</details>

<details><summary>힌트 2</summary>

(1) main 쪽에만 `irq_save/restore`. (2) `volatile bool` + payload와 flag 사이 compiler barrier + timeout. (3) `do { hi = H; lo = L; } while (hi != H);`.

</details>

<details><summary>모범 답 (C)</summary>

```c
/* (1) shared counter: ISR adds, main subtracts. Bug: main's `-=` is load/sub/store. */
static volatile uint32_t g_rx_count;
void rx_isr(void) { g_rx_count++; }              /* ISR side OK: main can't preempt it */
void main_consume(uint32_t n)                    /* FIX: mask IRQs around main's RMW */
{
    uint32_t key = irq_save();
    uint32_t v = g_rx_count;                     /* load */
    PREEMPT_POINT();                             /* sim: IRQ here is now held pending */
    g_rx_count = v - n;                          /* store */
    irq_restore(key);
}

/* (2) completion flag. Bug: `static bool g_done;` -> compiler hoists the load, spins forever,
 *     and may move the g_result read above the flag check. FIX: volatile + compiler barrier. */
static volatile bool g_done;
static uint32_t      g_result;
void dma_isr(void)
{
    g_result = 0xC0FFEEu;                                 /* 1. payload */
    atomic_signal_fence(memory_order_release);            /* 2. compiler barrier */
    g_done = true;                                        /* 3. flag    */
}
bool wait_done(uint32_t spins, uint32_t *out)             /* bounded wait, never hang */
{
    while (!g_done) {
        if (spins-- == 0) return false;
        PREEMPT_POINT();
    }
    atomic_signal_fence(memory_order_acquire);
    *out = g_result;
    g_done = false;
    return true;
}

/* (3) 64-bit tick kept as two 32-bit words on a 32-bit CPU. Bug: read lo, hi -> torn value
 *     when lo wraps in between. FIX: hi-lo-hi retry (no IRQ masking needed). */
static volatile uint32_t g_tick_lo, g_tick_hi;
void tick_isr(void) { if (++g_tick_lo == 0) g_tick_hi++; }
uint64_t ticks64(void)
{
    uint32_t hi, lo;
    do {
        hi = g_tick_hi;
        PREEMPT_POINT();
        lo = g_tick_lo;
    } while (hi != g_tick_hi);                   /* hi changed -> lo may be torn -> retry */
    return ((uint64_t)hi << 32) | lo;
}
```

- 모두 O(1). (3)은 최악에도 재시도 1번 (tick 간격 ≫ 루프 시간).
- `PREEMPT_POINT()`는 테스트용 IRQ 주입 훅 — 종이 답에는 없음. 테스트는 **원래 버그 버전도 같이 실행**해서 (1) count = 3, (3) 0x1_FFFFFFFF가 실제로 나오는 걸 assert한 뒤, 고친 버전이 4 / 0x1_00000000을 내는지 확인.
- (2)의 non-hang은 런타임 테스트로 증명할 수 없음 (최적화 결과에 의존) — 테스트는 수정 버전의 동작(값 전달 · timeout)만 확인. "volatile 없으면 hoist" 주장은 `[미검증]` (이 머신에서 어셈블리로 확인 안 함; N05H H3에서 다룸).
- 파일: onsitePrep/n05/e/E7_find_races.c.

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

(1) rx_count = 5, `main_consume(2)` 원본:

| 시점 | main | ISR | rx_count |
|---|---|---|---|
| t1 | `r0 = rx_count` (5) | – | 5 |
| t2 | (선점됨) | `rx_count++` | 6 |
| t3 | `rx_count = r0 - 2` | – | **3** (기대값 4) |

(3) hi = 0, lo = 0xFFFFFFFF, 수정 버전:

| 시점 | main | ISR | 결과 |
|---|---|---|---|
| t1 | hi = 0 | – | – |
| t2 | – | lo = 0, hi = 1 | – |
| t3 | lo = 0, 다시 읽은 hi = 1 ≠ 0 → retry | – | – |
| t4 | hi = 1, lo = 0, hi 그대로 | – | 0x1_00000000 |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- (1) 주입 없음 → 정상, 주입 있음 → 원본은 1 손실, 수정본은 손실 없음 (ISR은 restore 후 실행).
- (1) `n = 0`, count 0 경계.
- (2) ISR이 k번째 spin에서 옴 → 값 전달, ISR 안 옴 → timeout으로 false (hang 없음).
- (3) lo wrap 경계에서 주입 → 원본 torn, 수정본 일관.
- (3) wrap 아닌 tick 주입 → 한 번에 통과, (3) 0xFFFFFFFF_FFFFFFFF → UINT64_MAX.

</details>

### 4. 꼬리 질문

**Q1. For (1), could you avoid disabling interrupts?**

<details><summary>답</summary>

Yes — split it into two single-writer counters: the ISR increments `produced`, main increments `consumed`, and the pending count is `produced - consumed` in unsigned arithmetic. That's the same idea as the free-running ring indices in E1. Or use `atomic_fetch_sub` on cores with LDREX/STREX.

한국어 핵심: writer가 둘인 카운터를 writer 하나짜리 두 개로 쪼개면 lock 불필요.

</details>

**Q2. For (3), when is hi-lo-hi not enough?**

<details><summary>답</summary>

If the reader can be delayed for longer than a full low-word period between the reads — for example, a long higher-priority ISR on a fast counter — hi could wrap back to the same value. With a 1 ms tick that's 49 days, so it's fine; for a hardware cycle counter at hundreds of MHz the low word wraps in seconds, so I'd make sure the read path can't be stalled that long, or use the hardware's latch-on-read register (see N05C C4).

한국어 핵심: 두 읽기 사이 지연이 하위 워드 한 바퀴보다 짧아야 함. 빠른 HW 카운터면 latch 레지스터.

</details>

**Q3. Is `volatile bool done` enough for (2) on a multi-core SoC?**

<details><summary>답</summary>

No. `volatile` keeps the compiler honest but the other core may see `done` before `result` because of CPU or interconnect reordering. I'd use `atomic_store_explicit(&done, true, memory_order_release)` and an acquire load in the waiter. On a single core, the compiler barrier I added is enough.

한국어 핵심: 단일 코어는 compiler barrier면 충분, 멀티코어는 release/acquire.

</details>

### 5. 채점 · 자주 틀리는 것

- (1)에서 ISR 쪽에 IRQ disable을 넣음 (불필요) 또는 main 쪽을 안 고침.
- (1)에 `volatile`이 이미 있으니 안전하다고 말함.
- (2)에서 timeout 없는 무한 대기를 그대로 둠 — validation 코드는 항상 bounded wait.
- (2)에서 payload와 flag 순서를 언급 안 함.
- (3)에서 "IRQ 끄고 읽기"만 제시하고 lock-free 재시도를 모름 — 둘 다 말하기.
- (3)에서 `(uint64_t)` 캐스트 없이 `hi << 32` → 32-bit 폭 시프트 UB.

## E8 · ping-pong(double) DMA 버퍼 핸드오프 + overrun 검출 (★★ · 20분)

### 0. 문제 한국어로 먼저 보기

- **한 줄 요약**: DMA가 두 버퍼를 번갈아 채우고, 완료 ISR이 찬 버퍼를 CPU에게 넘기며, CPU가 늦으면 overrun을 세는 소유권(owner) 핸드오프.
- **상황**: ADC/I2S/카메라 라인 캡처의 표준 구조. validation에서는 "DMA가 CPU가 처리 중인 버퍼를 덮어써서 데이터가 깨졌다"를 검출하는 게 목표.

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `pp_dma_done_isr(p)` | DMA가 `dma_idx` 버퍼를 다 채웠을 때 | 다른 버퍼가 CPU 소유면 overrun +1, 같은 버퍼 다시 채움(데이터 버림). 아니면 찬 버퍼를 CPU에, DMA는 다른 버퍼로 |
| `pp_acquire(p)` | main: 처리할 버퍼 번호 | 준비된 게 없으면 -1 |
| `pp_release(p, i)` | main: 처리 끝, DMA에게 반납 | 마지막 접근 후에 소유권 변경 |

- **예시를 말로 풀면**: 프레임 0 완료 → CPU가 버퍼 0 처리 시작. 그동안 프레임 1(버퍼 1) 완료 → 버퍼 0이 아직 CPU 소유라 넘길 곳이 없음 → overrun 1, 프레임 1 버림. CPU가 버퍼 0 반납 → 프레임 2가 버퍼 1에 들어오고 CPU가 받음.
- **지켜야 할 조건**: DMA는 시뮬레이션 (테스트가 `buf[dma_idx]`에 쓰고 ISR 호출). 어떤 순간에도 한 버퍼의 owner는 하나.

### 1. Problem (English)

"An ADC streams into memory with DMA using two buffers. On each transfer-complete interrupt, hand the full buffer to the main loop and re-arm the DMA on the other one. Track ownership so the DMA never writes a buffer the CPU is reading, and count overruns when the CPU falls behind."

```c
#define PP_LEN 4u
enum { OWN_DMA = 0, OWN_CPU = 1 };
typedef struct { uint16_t buf[2][PP_LEN]; volatile uint8_t owner[2], dma_idx; volatile uint32_t overrun; } pingpong_t;
void pp_init(pingpong_t *p);
void pp_dma_done_isr(pingpong_t *p);
int  pp_acquire(const pingpong_t *p);
void pp_release(pingpong_t *p, int i);
```

- init → acquire = -1; frame0 done → acquire = 0, dma_idx = 1.
- frame1 done while CPU holds 0 → overrun = 1, dma_idx stays 1; release(0); frame2 done → acquire = 1 with frame 2's data.
- CPU keeps up for 1000 frames → buffers alternate 0, 1, 0, …, overrun = 0.

### 2. 먼저 물어볼 것 (clarify)

- "Does the DMA re-arm by software in the ISR, or does it switch buffers automatically in hardware (circular double-buffer mode)?" → Software re-arm for the code; discuss hardware auto-switch.
- "On overrun, drop the new block or the one the CPU holds?" → Drop the new block; never yank a buffer from the CPU.
- "Is there a data cache?" → Assume no for the code; Cortex-M7 cache handling is a follow-up.

### 3. 20분 타임박스

| 분 | 할 일 |
|---|---|
| 0–2 | clarify (SW re-arm vs HW auto, 정책, cache) |
| 2–5 | 상태 그림: (owner0, owner1, dma_idx) 전이 3단계 |
| 5–15 | init + ISR 7줄 + acquire 2줄 + release 1줄 |
| 15–18 | overrun 시나리오 trace |
| 18–20 | 테스트 + cache / HW 자동 전환 확장 |

- 소리 내서: "Each buffer has exactly one owner. The DMA side flips ownership to CPU only in the ISR, and the CPU flips it back only in release — so ownership is the handshake, like a descriptor's OWN bit."
- 소리 내서: "If the other buffer is still owned by the CPU when the DMA completes, there's nowhere to put the next block, so I count an overrun and refill the same buffer instead of corrupting the one being processed."

<details><summary>힌트 1 (막히면 5분 후)</summary>

상태는 `owner[0]`, `owner[1]`, `dma_idx` 세 개뿐. ISR이 해야 하는 판단은 "다음 버퍼(`dma_idx ^ 1`)를 DMA가 쓸 수 있나?" 하나입니다.

</details>

<details><summary>힌트 2</summary>

준비된 버퍼는 항상 DMA가 지금 채우지 않는 쪽 (`dma_idx ^ 1`) — 그 owner가 CPU면 반환. release는 처리의 **마지막 접근 뒤**에 owner = DMA.

</details>

<details><summary>모범 답 (C)</summary>

```c
#define PP_LEN 4u
enum { OWN_DMA = 0, OWN_CPU = 1 };

typedef struct {
    uint16_t          buf[2][PP_LEN];
    volatile uint8_t  owner[2];   /* who may touch buf[i] right now */
    volatile uint8_t  dma_idx;    /* buffer the DMA is filling */
    volatile uint32_t overrun;    /* DMA finished but the CPU still held the other buffer */
} pingpong_t;

void pp_init(pingpong_t *p)
{
    p->owner[0] = p->owner[1] = OWN_DMA;
    p->dma_idx = 0;
    p->overrun = 0;
}

void pp_dma_done_isr(pingpong_t *p)             /* DMA transfer-complete IRQ */
{
    uint8_t done = p->dma_idx, nxt = done ^ 1u;
    if (p->owner[nxt] == OWN_CPU) {             /* CPU is late: nowhere to put next block */
        p->overrun++;                           /* policy: re-fill `done`, its data is dropped */
        return;                                 /* dma_idx unchanged (re-arm same buffer) */
    }
    p->owner[done] = OWN_CPU;                   /* hand the full buffer to the CPU */
    p->dma_idx = nxt;                           /* hw: program DMA address = buf[nxt] */
}

int pp_acquire(const pingpong_t *p)             /* main: ready buffer index, or -1 */
{
    int i = p->dma_idx ^ 1u;                    /* ready one is never the DMA target */
    return (p->owner[i] == OWN_CPU) ? i : -1;   /* M7: invalidate D-cache for buf[i] here */
}

void pp_release(pingpong_t *p, int i)           /* main: done processing buf[i] */
{
    p->owner[i] = OWN_DMA;                      /* last access: give it back */
}
```

- 시간 O(1) · 공간 2 × PP_LEN.
- 불변식: CPU 소유 버퍼는 최대 1개 (ISR이 다음 버퍼가 DMA 소유일 때만 넘기므로).
- 파일: onsitePrep/n05/e/E8_pingpong_dma.c (테스트: 빈 상태, overrun 1회 + 복구 + 프레임 1 손실 확인, 1000프레임 정상 교대, 5프레임 stall → overrun 5 후 복구).

**bare-metal에서 더 필요한 것**

- Cortex-M7처럼 D-cache가 있으면: DMA가 쓴 버퍼를 CPU가 읽기 전에 해당 주소 범위 **invalidate** (CMSIS `SCB_InvalidateDCache_by_Addr`), 버퍼는 cache line(32B) 정렬·크기 배수로. 아니면 버퍼를 MPU로 non-cacheable 영역에 둠 [추정 — 칩/링커 설정마다 다름].
- `pp_release`에서 owner를 DMA로 넘기기 전 `__DMB()` — CPU의 마지막 읽기/쓰기가 DMA 재시작보다 먼저 끝나도록 (DMA 재설정이 레지스터 쓰기면 `__DSB()`까지) [추정].
- 많은 MCU DMA는 circular 모드 + half-transfer(HT)/transfer-complete(TC) 인터럽트로 버퍼 하나를 반씩 ping-pong 하거나, HW double-buffer 모드로 자동 전환 — 이 경우 ISR이 DMA를 멈출 수 없어서 overrun은 "막기"가 아니라 "검출"만 가능 (꼬리 Q1).

</details>

<details><summary>Trace 표 (종이에 이렇게)</summary>

| 이벤트 | owner[0] | owner[1] | dma_idx | overrun | acquire |
|---|---|---|---|---|---|
| init | DMA | DMA | 0 | 0 | -1 |
| frame0 done | CPU | DMA | 1 | 0 | 0 |
| frame1 done (CPU 아직 0 처리 중) | CPU | DMA | 1 | 1 | 0 |
| release(0) | DMA | DMA | 1 | 1 | -1 |
| frame2 done | DMA | CPU | 0 | 1 | 1 (frame 2 데이터) |

</details>

<details><summary>테스트 케이스 (말로 할 것)</summary>

- 초기 → -1.
- 정상 교대 1000프레임 → 인덱스 0, 1 교대, 데이터 = 프레임 번호, overrun 0.
- CPU 1회 지연 → overrun 1, 그 프레임만 손실, 다음 프레임 정상.
- 장기 stall (5프레임) → overrun 5, release 후 복구.
- 같은 버퍼 두 번 acquire/release 하지 않는지 (owner 상태 assert).

</details>

### 4. 꼬리 질문

**Q1. The DMA is in hardware double-buffer mode — it switches buffers by itself. How does overrun handling change?**

<details><summary>답</summary>

The ISR can no longer stop the DMA from writing the next buffer, so if the CPU still owns it, the data is being overwritten while it's processed. I can only detect it: in the ISR, if the buffer the DMA just switched to is still CPU-owned, set an overrun flag and a sequence number; the main loop checks the sequence before and after processing and discards the block if it changed. The real fix is a bigger margin — more buffers or faster processing.

한국어 핵심: HW 자동 전환이면 막을 수 없고 검출만 — seq 번호를 처리 전후로 비교해 깨진 블록 폐기.

</details>

**Q2. How would you extend it to N buffers?**

<details><summary>답</summary>

It becomes a descriptor ring: each descriptor has an OWN bit, the DMA fills descriptors in order and clears OWN, the CPU processes and sets OWN back. It's the same SPSC ring as E3, with the hardware as the producer. Overrun is the DMA finding the next descriptor not owned by it — many controllers raise a "descriptor unavailable" interrupt for that.

한국어 핵심: N개 = OWN 비트 descriptor ring (E3 링의 producer가 HW인 버전).

</details>

**Q3. How would you validate this path on real silicon?**

<details><summary>답</summary>

Feed a known pattern — a DAC ramp or an internal test-pattern generator — and check each block's contents and sequence in the main loop, while sweeping the processing delay to find where overruns start. Log the overrun counter and the max processing time versus the block period so you get the margin as a number, not just pass/fail.

한국어 핵심: 알려진 패턴 + 처리 지연 sweep → overrun 시작점과 margin을 숫자로.

</details>

### 5. 채점 · 자주 틀리는 것

- overrun 때 CPU가 처리 중인 버퍼를 DMA에 다시 줌 (데이터 오염).
- release 전에 처리를 끝내지 않음 / owner를 먼저 바꾸고 마지막 원소를 읽음.
- 공유 상태(owner, dma_idx, overrun)에 `volatile` 없음.
- D-cache 있는 코어에서 invalidate 언급 없음.
- overrun을 세기만 하고 main이 확인·보고하는 경로가 없음.

## 출처

1. Linux kernel docs — Circular Buffers (producer/consumer ordering, `smp_store_release`/`smp_load_acquire`, `CIRC_CNT`/`CIRC_SPACE`, 2^k 크기) — https://www.kernel.org/doc/html/latest/core-api/circular-buffers.html (이번 세션에서 열람)
2. Linux kernel `include/linux/kfifo.h` (2^k free-running index FIFO) — https://github.com/torvalds/linux/blob/master/include/linux/kfifo.h (열람 안 함, 표준 레퍼런스)
3. cppreference — C memory_order / atomic_thread_fence / atomic_signal_fence — https://en.cppreference.com/w/c/atomic/memory_order (열람 안 함, 표준 레퍼런스)
4. C11 draft N1570 §7.17 Atomics — https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf (열람 안 함)
5. Arm AN321 "ARM Cortex-M Programming Guide to Memory Barrier Instructions" — https://developer.arm.com/documentation/dai0321/latest/ (열람 시도했으나 본문 로드 실패 → 관련 주장은 [추정])
6. CMSIS-Core intrinsics (`__disable_irq`, `__get_PRIMASK`, `__DMB`, `SCB_InvalidateDCache_by_Addr`) — https://arm-software.github.io/CMSIS_5/Core/html/index.html (열람 안 함)
7. 이 머신 실측: onsitePrep/n05/e/run.sh (Apple clang 21, arm64, ASan/UBSan) + E3 TSan 실행 결과
