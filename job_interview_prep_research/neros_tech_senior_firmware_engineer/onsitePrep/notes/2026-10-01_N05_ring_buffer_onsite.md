# 🔁 N05 · Ring buffer 1:1 라운드 플레이북

> 리크루터가 온사이트 1:1에서 **"ring buffer 구현"**을 묻는다고 미리 알려 줬다 [확인됨 2026-09-16 Devin]. 이 노트는 그 25분짜리 라운드를 통째로 대비한다. 면접관이 문제를 끌고 갈 순서, 처음 2분에 던질 질문, 화이트보드에 쓰는 순서, 정답 코드를 한 줄씩 설명하는 법, 흔한 버그, 내 링버퍼를 어떻게 테스트할지, 꼬리 질문에 대한 영어 답까지 정리했다. 코드는 전부 `onsitePrep/code/`에 있고 `make -C code test`로 컴파일과 테스트가 통과한다.

## 0. 한눈에 보기

| 항목 | 내용 |
|---|---|
| 예상 시간 | 20~30분 (1:1 하나를 통째로 쓰거나, C/C++ 라운드의 후반부) |
| 시작 문제 | "Implement a ring buffer (circular buffer) in C." |
| 확장 순서 [추정] | full/empty 구분 → power-of-2 마스킹 → ISR/스레드 안전 → overwrite vs drop → DMA → multi-producer |
| 최종 목표 | **ISR producer → main loop consumer**를 lock 없이 안전하게 만드는 SPSC 링버퍼를 설명할 수 있는가 |
| 차별화 포인트 | 구현 후 **"이렇게 테스트하겠다"**를 스스로 꺼낸다. 테스트 조직(Michael) 쪽 면접관에게 특히 먹힌다 |
| 코드 | [ring_buffer.c](../code/ring_buffer.c) · [ring_buffer_spsc.c](../code/ring_buffer_spsc.c) · [ring_buffer_overwrite.c](../code/ring_buffer_overwrite.c) · [ring_buffer_generic.c](../code/ring_buffer_generic.c) · [dma_circular_rx.c](../code/dma_circular_rx.c) |
| 더 깊게 | [링버퍼 완전정복 7레벨 42문제](../../research/ringbuffer_research/html/index.html) · [Neros C 연습 01 ring/logging](../../practice/html/01_ring_logging.html) |

## 1. 면접관이 라운드를 끌고 가는 방식

대부분 아래 순서로 한 단계씩 올라간다. 각 단계에서 면접관이 실제로 보려는 것을 같이 적었다.

| 단계 | 면접관 질문 (예상) | 실제로 보는 것 |
|---|---|---|
| 1 | "Write push and pop for a byte ring buffer." | 인덱스 처리, 경계 조건, 코드가 깔끔한지 |
| 2 | "How do you tell full from empty?" | 세 가지 방법(한 칸 비우기, count, free-running index)을 알고 고를 수 있는지 |
| 3 | "Why would you make the size a power of two?" | `%` 대신 `& mask`, unsigned wraparound 이해 |
| 4 | "Now the producer is a UART ISR. Is this safe?" | 공유 변수, 단일 writer 원칙, `volatile`의 한계, memory ordering |
| 5 | "What happens when it's full? What should a logger do?" | drop-new vs overwrite-oldest 정책과 그 비용 |
| 6 | "How would DMA fit in?" | circular DMA, NDTR, half/full/idle 이벤트, 캐시 |
| 7 | "Two producers?" | MPSC/MPMC는 어렵다는 것, 해결책(lock, CAS, per-producer ring) |
| 8 | "How would you test it?" | 경계값, wraparound, 스트레스, sanitizer. 테스트 마인드 |

단계 4~5까지 막힘없이 가면 이 라운드는 통과로 본다 [추정]. 6~8은 시니어 신호다.

## 2. 처음 2분 — 코드를 쓰기 전에 물어볼 것 (영어)

바로 코드를 쓰지 않는다. 요구사항을 확인하는 것 자체가 평가 항목이다.

> "Before I write anything, a few quick questions so I build the right thing."

| 질문 | 왜 묻나 | 답에 따라 바뀌는 것 |
|---|---|---|
| "What's the element type — bytes, or fixed-size messages?" | 바이트면 `uint8_t`, 메시지면 `memcpy` 버전 | [ring_buffer.c](../code/ring_buffer.c) vs [ring_buffer_generic.c](../code/ring_buffer_generic.c) |
| "Is the capacity fixed at compile time? Can I pick a power of two?" | 마스킹 가능 여부, 정적 할당 | `& mask` vs `% size` |
| "One producer and one consumer, or more?" | SPSC면 lock-free 가능 | 단일 writer 원칙 적용 여부 |
| "Is either side an interrupt handler or another thread?" | 동시성 요구 | atomics / critical section |
| "When it's full — drop the new data, overwrite the oldest, or block?" | 정책 | drop-new / overwrite / blocking |
| "Do we need bulk read/write, like for a UART DMA?" | 성능 | memcpy 두 번 버전 |

> 면접관이 "you decide"라고 하면: **"Then I'll start with a single-threaded byte buffer, power-of-two size, drop-new when full, and make it ISR-safe as a second step."** 작은 것부터 만들고 키워 가는 방식을 말로 보여 준다.

## 3. 화이트보드에 쓰는 순서

1. **자료구조 먼저**: `buf[SIZE]`, `head`, `tail`. 그리고 "head는 다음에 쓸 곳, tail은 다음에 읽을 곳"이라고 말로 정의한다
2. **불변식 한 줄**: `count = head - tail`, `0 <= count <= SIZE`
3. **`count` / `empty` / `full`** 헬퍼 세 줄
4. **`push`**: full 체크 → 쓰기 → head 증가
5. **`pop`**: empty 체크 → 읽기 → tail 증가
6. **예시로 손으로 돌려 보기**: SIZE=4로 push 4번, pop 1번, push 1번 (wraparound 확인)
7. **그다음에** 동시성, 정책, 테스트 이야기

> 면접관이 보는 앞에서 6번을 하는 게 중요하다. 버그를 스스로 잡는 모습을 보여 줄 수 있다.

## 4. 정답 코드 — 한 줄씩 왜

### 4.1 기본형: free-running index + power-of-2 mask

```c
#define RB_SIZE 16u                       /* power of 2 */
#define RB_MASK (RB_SIZE - 1u)

typedef struct {
    uint8_t  buf[RB_SIZE];
    uint32_t head;                        /* next write, never masked */
    uint32_t tail;                        /* next read */
} rb_t;

static uint32_t rb_count(const rb_t *rb) { return rb->head - rb->tail; }

static bool rb_push(rb_t *rb, uint8_t b)
{
    if (rb_count(rb) == RB_SIZE)
        return false;                     /* full: drop new */
    rb->buf[rb->head & RB_MASK] = b;
    rb->head++;
    return true;
}

static bool rb_pop(rb_t *rb, uint8_t *out)
{
    if (rb->head == rb->tail)
        return false;                     /* empty */
    *out = rb->buf[rb->tail & RB_MASK];
    rb->tail++;
    return true;
}
```

| 줄 | 왜 이렇게 쓰나 |
|---|---|
| `RB_SIZE`가 2의 거듭제곱 | `idx & RB_MASK`가 `idx % RB_SIZE`와 같아진다. 나눗셈이 없고, Cortex-M0처럼 하드웨어 나눗셈이 없는 코어에서 특히 중요하다 |
| `head`, `tail`을 마스킹하지 않고 계속 증가 | full과 empty가 구분된다. empty는 `head == tail`, full은 `head - tail == SIZE`. **슬롯을 한 칸 비워 둘 필요가 없다** |
| `head - tail`이 unsigned | head가 2^32에서 0으로 넘어가도 unsigned 뺄셈은 모듈로 2^32이라 결과가 맞다. 조건: SIZE가 2^32를 나누어야 한다 (2의 거듭제곱이면 항상 성립) |
| 쓰기 → 그다음 head 증가 | 소비자가 head를 보고 읽으러 왔을 때 데이터가 이미 있어야 한다. 순서를 바꾸면 동시성 버그 (4.2) |
| `bool` 반환 | 실패를 호출자에게 알린다. 실제 코드에서는 drop 카운터도 올린다 |

**full/empty 구분 세 가지를 비교해서 말할 수 있어야 한다**:

| 방법 | 장점 | 단점 |
|---|---|---|
| 한 칸 비우기 (`(head+1)%N == tail`이면 full) | 인덱스 두 개만, SPSC에 그대로 쓸 수 있음 | 용량이 N-1 |
| `count` 변수 따로 | 이해하기 쉬움 | count를 양쪽이 다 쓴다 → 동시성에서 lock 필요 |
| **free-running index** (이 코드) | 용량 N 전부, 각 인덱스는 한쪽만 씀 | 2의 거듭제곱 크기와 unsigned wrap 이해 필요 |

> "I use free-running indices: head and tail only ever increase, and I mask when I index the array. Empty is head equals tail, full is head minus tail equals the size. Unsigned subtraction handles the 32-bit wrap, and each index has exactly one writer, which matters as soon as an ISR gets involved."

### 4.2 ISR-safe SPSC: C11 acquire/release

전체 코드: [ring_buffer_spsc.c](../code/ring_buffer_spsc.c) (두 스레드로 1천만 개를 밀어 넣어 순서와 누락을 검사, ThreadSanitizer 통과)

```c
static bool spsc_push(spsc_t *q, uint32_t v)        /* producer: UART RX ISR */
{
    uint32_t h = atomic_load_explicit(&q->head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_acquire);
    if (h - t == Q_SIZE)
        return false;
    q->buf[h & Q_MASK] = v;
    atomic_store_explicit(&q->head, h + 1, memory_order_release);   /* publish */
    return true;
}

static bool spsc_pop(spsc_t *q, uint32_t *out)      /* consumer: main loop */
{
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&q->head, memory_order_acquire);
    if (h == t)
        return false;
    *out = q->buf[t & Q_MASK];
    atomic_store_explicit(&q->tail, t + 1, memory_order_release);   /* free slot */
    return true;
}
```

| 포인트 | 설명 |
|---|---|
| 단일 writer | head는 producer만, tail은 consumer만 쓴다. 그래서 read-modify-write 경쟁이 없다 |
| release store (head) | "이 store 전에 한 쓰기(데이터)는 이 store를 acquire로 본 쪽에 먼저 보인다" |
| acquire load (head) | head를 본 뒤에 데이터를 읽으므로 아직 안 쓰인 슬롯을 읽지 않는다 |
| 자기 인덱스는 relaxed | 자기만 쓰는 값이라 순서 보장이 필요 없다 |
| Cortex-M 단일 코어 | ISR과 main이 진짜로 동시에 돌지는 않지만, **컴파일러가 순서를 바꾸거나 레지스터에 캐시**할 수 있다. atomics가 이를 막는다. 정렬된 32비트 load/store는 Cortex-M에서 single-copy atomic이라 lock-free로 동작한다 |
| 멀티코어 / 약한 메모리 모델 | 같은 코드가 필요한 곳에 DMB 같은 barrier를 넣어 준다. 그래서 C11 atomics로 쓰는 게 이식성이 있다 |

> "The trick is that each index has a single writer. The producer writes the data first and then publishes the new head with a release store; the consumer reads head with acquire before touching the data. On a single-core Cortex-M the ISR and main loop never truly run in parallel, but the compiler can still reorder or cache those accesses, so I use C11 atomics rather than relying on volatile."

### 4.3 덮어쓰기 정책 (logger / blackbox)

전체 코드: [ring_buffer_overwrite.c](../code/ring_buffer_overwrite.c)

- 로거나 블랙박스는 **최신 데이터가 더 중요하다** → 가득 차면 가장 오래된 것을 버린다
- 문제: 버리려면 producer가 **tail을 움직여야** 한다. 이제 tail에 writer가 둘이라 SPSC가 깨진다. consumer가 읽는 중인 슬롯을 producer가 덮어쓸 수 있다
- 해결 1: push(덮어쓰기 포함)와 pop을 **짧은 critical section**으로 감싼다 (MCU에서는 IRQ 끄기, PRIMASK 저장/복원). 단순하고 이 파일이 쓰는 방법이다
- 해결 2: producer는 tail을 건드리지 않고, 레코드마다 **sequence number**를 붙인다. consumer가 seq가 건너뛴 걸 보고 dropped로 센다. lock-free
- 어느 쪽이든 **dropped 카운터**를 남긴다. "몇 개를 잃었는지 모르는 로그"는 디버깅에 위험하다

> "For a flight logger I'd overwrite the oldest, because the last seconds before a crash matter most. But overwrite means the producer moves the tail, so it's no longer single-writer. I'd either wrap push and pop in a short critical section, or keep it lock-free by stamping sequence numbers so the reader can detect and count gaps."

### 4.4 메시지 단위 링 (struct)

전체 코드: [ring_buffer_generic.c](../code/ring_buffer_generic.c)

- `elem_size`만큼 `memcpy`. 저장소와 제어 블록을 **매크로로 정적 할당** (`RING_DEFINE(g_imu_q, imu_msg_t, 4)`), malloc 없음
- `ring_front()`로 슬롯 포인터를 돌려주면 **zero-copy peek**. 다음 get 전까지만 유효하다고 계약을 명시한다

### 4.5 UART RX circular DMA

전체 코드: [dma_circular_rx.c](../code/dma_circular_rx.c)

- DMA가 `rx_buf`에 원형으로 쓴다. CPU가 아는 것은 **NDTR(남은 전송 수)**뿐이다. 쓰기 위치 = `SIZE - NDTR`
- consumer는 자기 `read_pos`를 들고 있다가 **Half-Transfer, Transfer-Complete, IDLE line** 인터럽트마다 `[read_pos, write_pos)`를 처리한다. wrap이면 두 조각
- 세 이벤트가 **같은 함수**를 부르게 하면 어떤 이벤트가 왔는지 신경 쓸 필요가 없다
- 제약: 이벤트 사이에 SIZE 바이트 넘게 들어오면 DMA가 reader를 추월해 데이터를 잃는다 → 버퍼 크기 산정 (6절)
- **캐시가 있는 코어**(예: Cortex-M7)에서는 읽기 전에 해당 범위를 invalidate하거나 버퍼를 non-cacheable 영역에 둔다. 안 그러면 CPU가 오래된 값을 읽는다
- STM32 HAL을 쓴다면 `HAL_UARTEx_ReceiveToIdle_DMA` 계열이 이 패턴이다 [추정: 버전별 API 확인]

> "With circular DMA the CPU only sees the remaining-count register, so the write position is size minus NDTR. I process from my read position to that write position on half-transfer, transfer-complete, and idle-line interrupts — all three call the same handler. On an M7 with D-cache I'd invalidate that range first."

## 5. 흔한 버그

| 버그 | 증상 | 바르게 |
|---|---|---|
| head 증가를 데이터 쓰기보다 먼저 | consumer가 쓰레기를 읽음 (가끔) | 데이터 → head 순서, release store |
| `count` 변수를 ISR과 main이 같이 `++`/`--` | 드물게 count가 틀어짐 | free-running index로 count를 계산 |
| `volatile`만 붙이고 끝 | 컴파일러는 막지만 **데이터와 인덱스 사이 순서**는 보장 안 됨 | atomics 또는 barrier |
| `% SIZE`를 non-power-of-2와 free-running index에 같이 사용 | 인덱스가 2^32에서 넘어갈 때 위치가 튐 | SIZE를 2의 거듭제곱으로, 또는 인덱스를 매번 wrap |
| full 체크를 `head == tail`로 | full과 empty가 구분 안 됨 | `head - tail == SIZE` |
| bulk 쓰기에서 wrap 처리 누락 | 배열 끝을 넘어 씀 (메모리 깨짐) | memcpy 두 번 (끝까지, 처음부터) |
| 16비트 인덱스를 8비트 MCU에서 | 인덱스 읽기가 두 번에 나뉘어 tearing | 원자적으로 읽히는 크기 사용, 또는 critical section |
| overwrite인데 lock 없음 | 읽는 중인 레코드가 덮어써짐 | critical section 또는 sequence number |
| DMA 버퍼가 캐시됨 | 새 데이터가 안 보이거나 오래된 데이터 | invalidate / non-cacheable |

## 6. 복잡도와 크기 산정

- push, pop: **O(1)**. bulk: O(n), memcpy 최대 두 번
- 메모리: SIZE × 원소 크기 + 인덱스 두 개. 동적 할당 없음
- **크기 산정 공식**: 필요한 깊이 ≥ (producer 최대 rate) × (consumer 최악 지연) + 여유
- 예: UART 420000 baud, 8N1이면 바이트당 10비트 → 초당 최대 42000바이트. main loop가 최악 5 ms 늦어지면 42000 × 0.005 = 210바이트. 2의 거듭제곱으로 올려 **256바이트**, 여유를 원하면 512
- 크기를 정한 뒤에는 **high-water mark**(최대 사용량)와 drop 카운터를 텔레메트리로 내보내서 실제로 검증한다

> "I size it from the producer's worst-case rate times the consumer's worst-case latency, then round up to a power of two with margin. At 420 kbaud that's about 42 kilobytes per second, so a 5 millisecond stall needs around 210 bytes — I'd use 256 or 512, and export a high-water mark so we can see the real margin in the field."

## 7. 내 링버퍼를 어떻게 테스트하나 (먼저 꺼낼 것)

구현이 끝나면 면접관이 묻기 전에 말한다. 테스트 조직 면접관이 들어올 수 있으니 특히 효과적이다.

| 테스트 | 무엇을 확인 | 코드 위치 |
|---|---|---|
| 빈 버퍼에서 pop/peek | false 반환, 크래시 없음 | ring_buffer.c |
| 정확히 SIZE개 push 후 한 개 더 | 마지막만 실패, 용량 N 전부 사용 | ring_buffer.c |
| 여러 바퀴 wraparound (bulk 포함) | 순서 보존 | ring_buffer.c |
| 인덱스를 `UINT32_MAX - 3`에서 시작 | 2^32 넘어갈 때도 count가 맞음 | ring_buffer.c |
| 두 스레드로 1천만 개 | 누락·중복·순서 뒤바뀜 없음 | ring_buffer_spsc.c |
| AddressSanitizer / UBSan 빌드 | 경계 밖 접근, 정의되지 않은 동작 | Makefile (`.san` 빌드) |
| ThreadSanitizer | data race 없음 | `cc -fsanitize=thread ring_buffer_spsc.c` |
| overwrite에서 seq 연속성 | 버려진 개수 = seq 공백 | ring_buffer_overwrite.c |
| DMA 시뮬레이션: 짧은 프레임, 정확히 끝, wrap 넘기, 가짜 이벤트 | 모든 경계에서 스트림이 이어짐 | dma_circular_rx.c |
| (HIL) 실제 UART에 카운터 패턴 최대 rate + 플래시 쓰기 부하 | 실제 ISR 지연에서 drop 0 | [N08 D4](../../firmwareTestEngineerPrep/site/start.html) 참고 |

> "Before calling it done I'd test the edges: empty, exactly full, many wraparounds, and the indices starting just below UINT32_MAX so the 32-bit wrap is exercised. For the SPSC version, two threads pushing millions of sequenced values under ThreadSanitizer, and on hardware, a counter pattern at max baud while the flash is busy, watching the drop counter."

## 8. 꼬리 질문과 영어 답

**Q. Why isn't `volatile` enough?**

> "Volatile stops the compiler from caching or eliminating the access, but it doesn't order it relative to the non-volatile data writes, and it says nothing to the CPU about memory ordering. What I need is: data written before the index is published. That's exactly acquire/release semantics, so I use C11 atomics — on Cortex-M that compiles to plain loads and stores plus barriers where required."

**Q. Do you need atomics on a single-core MCU?**

> "Not for hardware ordering on a simple single-core M4, but yes for the compiler. Without them the compiler may reorder the data store after the index store or keep the index in a register in the main loop. Atomics express the intent and stay correct if the code later runs on a multi-core part or a Linux companion."

**Q. Why a power of two?**

> "Two reasons. Masking is cheaper than modulo, especially on cores without a hardware divider. And with free-running unsigned indices, the 2^32 wrap stays consistent with the buffer wrap only if the size divides 2^32."

**Q. Explain the unsigned wraparound math.**

> "Unsigned arithmetic is modulo 2^32. If head wrapped to 2 and tail is 0xFFFFFFFE, head minus tail is still 4. As long as the true count never exceeds 2^32, the subtraction is correct."

**Q. What about cache and DMA?**

> "If DMA writes into cacheable memory, the CPU can read stale lines. Before reading a received range I invalidate it, and before starting a TX DMA I clean the source range. Or I put DMA buffers in a non-cacheable region via the MPU — simpler and less error-prone."

**Q. How do you handle multiple producers?**

> "Then head has multiple writers, so the simple scheme breaks. Options: a critical section around push, which is fine on an MCU if it's short; a compare-and-swap loop to reserve a slot plus a per-slot ready flag, which is more complex; or my usual preference — one SPSC ring per producer, and the consumer drains them all. That keeps every ring single-writer."

**Q. Drop-new or overwrite?**

> "It depends on what the data is for. Commands and protocol frames: drop new and count it, because silently losing the oldest command is dangerous. Logs and telemetry: overwrite oldest, because the latest state matters most. Either way I keep a counter so loss is visible."

**Q. Blocking version?**

> "On an RTOS I'd pair the ring with a semaphore or task notification: the ISR pushes and gives the semaphore, the task blocks on it. The ring stays lock-free; only the wake-up uses the RTOS."

**Q. Zero-copy?**

> "Expose a pointer to the next contiguous region instead of copying — the consumer processes in place and then commits by advancing tail. For bulk I return up to two regions, before and after the wrap. That's how you'd feed a DMA or a parser without extra copies."

**Q. How would you know in the field that the buffer is too small?**

> "Keep a high-water mark and a drop counter in the firmware and send them in telemetry or the log header. A nonzero drop count in a flight log turns a mystery into a sizing fix."

## 9. Don 경험과 연결 (레쥬메 범위)

| 레쥬메 사실 | 링버퍼 이야기로 잇는 한 문장 | 채울 칸 |
|---|---|---|
| SK hynix: NVMe telemetry 기반 time-sensitive 디버그 기능 (Microsoft, Dell, HPE) | "Capturing debug state at the moment of failure without hurting performance is the same problem as a lock-free logger ring." | (Don: 캡처 버퍼 구조, 크기, 트리거 방식) |
| Solidigm: 멀티코어 Cortex-R8/R82/M0+ bare-metal FW, DMA | "On multi-core SSD firmware, cross-core queues had exactly these ordering concerns." | (Don: 코어 간 통신 방식 — mailbox, shared memory 등) |
| SK hynix: UART 기반 테스트 시퀀스 자동화 (챔버, 다수 eSSD) | "I've been on the receiving end of UART streams at scale, so I care about drop counters and framing." | (Don: UART 로그 수집에서 겪은 문제) |
| Solidigm: error reporting/handling scheme | "An error log ring needs a policy for when it fills, and a counter for what was lost." | (Don: 에러 로그 저장 방식) |

> ⚠️ 위 연결은 원칙 수준에서만 말한다. 실제 구조를 기억하지 못하면 "(Don: …)" 칸 내용은 말하지 않는다.

## 10. 25분 리허설 스크립트

| 시간 | 할 일 |
|---|---|
| 0~2분 | 2절 질문 3~4개. 답을 화이트보드 구석에 적는다 |
| 2~4분 | 자료구조, 불변식, head/tail 정의 |
| 4~10분 | count/empty/full, push, pop 작성 |
| 10~12분 | SIZE=4로 손으로 돌리기 (wraparound) |
| 12~17분 | "Now let's make it ISR-safe": 단일 writer, release/acquire, volatile 한계 |
| 17~20분 | full 정책 (drop vs overwrite), 크기 산정 |
| 20~23분 | 테스트 계획 (7절) |
| 23~25분 | 남은 질문: DMA, multi-producer |

**연습법**: 종이에 [ring_buffer.c](../code/ring_buffer.c)의 push/pop을 보지 않고 쓴다 → `make -C code test`로 같은 테스트를 내 코드에 돌려 본다 → [ring_buffer_spsc.c](../code/ring_buffer_spsc.c)의 주석을 영어로 소리 내어 설명한다. 레벨별로 더 연습하려면 [링버퍼 완전정복](../../research/ringbuffer_research/html/index.html)의 L0(modulo) → L2(mask) → L4(SPSC) → L5(overwrite, zero-copy)와 노트 07(pitfalls, MPMC)을 본다.

## 체크

- [ ] 2절 질문을 영어로 막힘없이 말할 수 있다
- [ ] 기본형 push/pop/count를 5분 안에 종이에 쓰고 SIZE=4로 손으로 돌려 봤다
- [ ] full/empty 구분 세 가지 방법의 장단점을 말할 수 있다
- [ ] SPSC가 lock 없이 안전한 이유를 "single writer + release/acquire"로 30초 안에 설명할 수 있다
- [ ] `volatile`이 충분하지 않은 이유를 말할 수 있다
- [ ] overwrite가 SPSC를 깨는 이유와 해결책 두 가지를 말할 수 있다
- [ ] 크기 산정 공식으로 420 kbaud 예시를 계산할 수 있다
- [ ] DMA circular의 NDTR 계산과 HT/TC/IDLE 처리를 설명할 수 있다
- [ ] "how would you test it" 답을 먼저 꺼낼 수 있다
- [ ] `make -C code test`를 한 번 돌려서 전부 PASS를 확인했다
