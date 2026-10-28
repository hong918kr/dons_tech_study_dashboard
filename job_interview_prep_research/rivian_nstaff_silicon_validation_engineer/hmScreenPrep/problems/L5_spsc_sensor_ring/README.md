# L5 · Lock-free SPSC Ring for a Sensor Capture → Logger Pipeline

> **ID**: L5 · **Priority**: P0 · **Pattern**: SPSC ring + free-running indices + acquire/release + 2-thread stress test · **Time**: 30 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh L5` / `./run.sh L5 sol`

## 0. 문제 한국어로 먼저 보기

> 영어 원문(§1)을 읽기 전에 **무슨 상황이고 뭘 만들어야 하는지**만 빠르게 잡는 요약입니다. 풀이 힌트는 없습니다.

**한 줄 요약**: 센서 캡처 쪽(ISR / DMA 완료 핸들러 역할)과 로거 쪽(저장소에 기록)을 잇는 **락 없는 SPSC(single-producer single-consumer) 링 버퍼**를 C11 atomics로 짜고, 2-thread 스트레스 테스트로 검증합니다.

**상황**
- 센서 박스(MPSoC 위 Linux)에서 **capture thread**가 64-byte 고정 크기 레코드를 만듭니다. 실제 시스템에서는 ISR이나 DMA 완료 핸들러 자리입니다.
- **logger thread**는 링에서 레코드를 **여러 개씩 묶어(batch)** 꺼내 저장소에 씁니다. 저장소는 가끔 느려집니다.
- 트래픽은 **bursty**합니다. 몰려 들어올 때 링이 꽉 찰 수 있습니다.
- capture 쪽은 **절대 block되면 안 됩니다**. 링이 꽉 차면 기다리지 않고 버립니다.
- 레코드: `seq`(캡처 순번), `len`(유효 payload 바이트 1..48), `ts_ns`(타임스탬프), `payload[48]` → 합쳐서 정확히 64바이트(캐시 라인 1개).
- `Ring` 구조체 레이아웃은 주어집니다. `head`(producer만 씀)와 `tail`(consumer만 씀)은 **서로 다른 캐시 라인**에 있습니다.
- `head`/`tail`은 **free-running `uint32_t`** 카운터이고 2^32를 넘어 wrap됩니다. slot 번호는 `idx & (cap − 1)`입니다.
- 테스트 훅 `ring_test_set_index(r, start)`가 주어집니다. 빈 링의 인덱스를 2^32 근처에서 시작하게 해 wrap을 빨리 테스트합니다.

**구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `ring_init(r, buf, cap)` | 링 초기화 | `cap`이 0이거나 2의 거듭제곱이 아니면, 또는 `r`/`buf`가 NULL이면 `-EINVAL`. 인덱스와 통계를 0으로 |
| `ring_push(r, rec)` | 레코드 1개 넣기 (**producer만 호출**) | 성공하면 `true`. 꽉 찼으면 **새로 들어온 레코드를 버리고**(drop-newest) `dropped` +1, `false`. 절대 block 안 함. `high_water` = 지금까지 본 최대 점유 개수 |
| `ring_pop_batch(r, out, max)` | 최대 `max`개를 오래된 순서로 `out[]`에 복사 (**consumer만 호출**) | 꺼낸 개수 반환, 비었으면 0. 복사한 slot은 producer가 다시 쓸 수 있게 반납 |
| `ring_stats(r, out)` | 통계 보고 | `pushed`(받아들인 개수), `dropped`, `high_water`. 아무 thread에서나 호출 가능 |

**예시를 말로 풀면**
- **cap = 8, 레코드 10개 push**: 앞의 8개(seq 0..7)만 들어가고 seq 8, 9는 버려짐 → `dropped = 2`, `high_water = 8`. pop하면 seq 0..7이 순서대로 나옴.
- **5개 넣고 `pop_batch(max = 2)`**: seq 0, 1만 나옴. `max = 0`이면 0개. 다시 `max = 16`이면 남은 seq 2, 3, 4.
- **인덱스를 `0xFFFFFFFE`에서 시작**: 5개 push하면 `head`는 2^32를 넘어 **3**이 됨. seq 0은 slot 6, seq 1은 slot 7, seq 2는 slot 0에 들어감. 그래도 점유 개수는 5로 계산돼야 함.
- **2-thread 스트레스**: producer thread가 seq 0..999,999 레코드 100만 개를 burst로 push, consumer(main thread)가 batch로 pop하며 모든 바이트를 검사. 끝나면 `pushed + dropped == 1,000,000`, `popped == pushed`, seq는 **엄격히 증가**, seq 빈칸 수 == `dropped`, payload 체크섬 일치.

**지켜야 할 조건**
- C11 `<stdatomic.h>`만 사용. **mutex 금지, malloc 금지**. 버퍼는 호출자가 줌.
- producer 1개, consumer 1개만 있다고 가정해도 됨.
- `head`/`tail`은 `uint32_t`라서 2^32를 넘어 wrap될 수 있음.
- macOS clang에서 ASan/UBSan으로 통과해야 하고, 테스트 전체가 3초 안에 끝나야 함.

## 1. Problem

You are writing the capture → logger path of a sensor box (Linux on an MPSoC). A **capture thread** — standing in for an ISR or a DMA-completion handler — produces fixed-size 64-byte sensor records. A **logger thread** drains them in batches and writes them to storage, which sometimes stalls. Traffic is bursty. The capture side **must never block**: when the ring is full it drops and counts.

Implement a lock-free **single-producer / single-consumer** ring with C11 atomics. No locks, no malloc; the record buffer is caller-provided and its capacity is a power of two.

```c
#define PAYLOAD_N 48u
typedef struct {
    uint32_t seq;                 /* capture sequence number */
    uint32_t len;                 /* valid payload bytes, 1 .. PAYLOAD_N */
    uint64_t ts_ns;               /* capture timestamp */
    uint8_t  payload[PAYLOAD_N];
} Record;                         /* exactly 64 bytes = one cache line */

typedef struct {
    Record  *buf;                 /* read-only after init */
    uint32_t cap, mask;
    _Alignas(CACHE_LINE) _Atomic uint32_t head;   /* written by producer only (free-running) */
    _Atomic uint32_t pushed, dropped, high_water; /* stats, written by producer only */
    _Alignas(CACHE_LINE) _Atomic uint32_t tail;   /* written by consumer only (free-running) */
} Ring;

typedef struct { uint32_t pushed, dropped, high_water; } RingStats;

int      ring_init(Ring *r, Record *buf, uint32_t cap);          /* 0 or -EINVAL */
bool     ring_push(Ring *r, const Record *rec);                  /* producer only */
uint32_t ring_pop_batch(Ring *r, Record *out, uint32_t max);     /* consumer only */
void     ring_stats(Ring *r, RingStats *out);
static void ring_test_set_index(Ring *r, uint32_t start);        /* provided test hook */
```

Requirements:

- **`ring_init`**: `cap` must be a non-zero power of two and `r`, `buf` non-NULL, else `-EINVAL`. Reset indices and stats.
- **`ring_push`**: copy the record into slot `head & mask` and publish it. If the ring is full, **drop the new record**, increment `dropped`, return `false`. Never block, never spin. On success increment `pushed` and update `high_water` (maximum occupancy ever seen).
- **`ring_pop_batch`**: copy up to `max` records, oldest first, into `out[]`, then release those slots to the producer. Return the count (0 if empty).
- **`ring_stats`**: report `pushed`, `dropped`, `high_water`.
- `head`/`tail` are free-running and wrap past 2^32; occupancy is `head − tail` in unsigned arithmetic.
- Choose the memory ordering so that the consumer never sees a slot before its record is fully written, and the producer never overwrites a slot the consumer is still copying.

### Example

```
cap = 8
push seq 0..9          -> 8 x true, 2 x false   stats: pushed 8, dropped 2, high_water 8
pop_batch(max 16)      -> 8 records, seq 0..7   (8 and 9 were dropped: drop-newest)

push seq 0..4; pop_batch(max 2) -> seq 0,1;  pop_batch(max 0) -> 0;  pop_batch(max 16) -> seq 2,3,4

ring_test_set_index(r, 0xFFFFFFFE); push seq 0..4
  -> head = 3 (wrapped), tail = 0xFFFFFFFE, occupancy = 5
     seq 0 in slot 6, seq 1 in slot 7, seq 2 in slot 0

2 threads, 1,000,000 records:
  pushed + dropped == 1,000,000,  popped == pushed,
  consumer sees strictly increasing seq,  #missing seq == dropped,  payload checksum matches
```

### Constraints

- C11 with `<stdatomic.h>`; no mutex, no malloc. Exactly one producer thread and one consumer thread.
- `cap` is a power of two (8 and 1024 in the tests). Indices are `uint32_t` and may wrap.
- Must pass under `-fsanitize=address,undefined` on macOS clang; the whole test runs in under 3 s.

### Clarifying questions to ask (first 3 minutes)

1. Exactly one producer and one consumer? Is the producer a thread, an ISR, or a DMA engine (hardware)?
2. When full: drop the newest, overwrite the oldest, or block? Is losing data acceptable if it is counted?
3. Fixed-size records, or variable length?
4. Target CPU and memory model: Arm (Cortex-A53/A72 on Zynq MPSoC) or x86? Multi-core?
5. Can the consumer sleep (needs a wake-up: eventfd / futex / semaphore), or does it poll?
6. Is the buffer shared with a DMA master, and is that path cache-coherent?

## 2. Your workspace

`./run.sh L5`로 바로 테스트하세요. 테스트는 6묶음(CHECK 46개)이고, 마지막 묶음은 2-thread 스트레스 테스트입니다. stub 상태에서도 멈추지 않고 실패만 나오게 되어 있고, 혹시 push를 block하게 짜면 20초 watchdog(`alarm`)이 끊어 줍니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 점유 개수는 head − tail 하나로 끝</summary>

`head`, `tail`을 증가만 시키고 slot은 `idx & mask`입니다. 점유 개수 `used = head - tail`은 `uint32_t` 뺄셈이라 2^32를 넘어도 맞습니다. `used == 0`이면 empty, `used == cap`이면 full이라 "한 칸 비워 두기"가 필요 없습니다. `if (head >= tail)` 같은 분기는 쓰지 마세요.

</details>

<details>
<summary>Hint 2 — 각 인덱스는 쓰는 쪽이 딱 하나</summary>

`head`는 producer만, `tail`은 consumer만 씁니다. 그래서 CAS가 필요 없고 load + store면 됩니다. 순서 규칙은 두 쌍입니다.
- producer: 레코드 복사 → `head`를 **release**로 store. consumer: `head`를 **acquire**로 load → 레코드 복사.
- consumer: 레코드 복사 → `tail`을 **release**로 store. producer: `tail`을 **acquire**로 load → 그 slot에 쓰기.

자기 인덱스를 읽을 때는 relaxed로 충분합니다(자기가 쓴 값이니까).

</details>

<details>
<summary>Hint 3 — batch는 복사 다 하고 tail은 한 번만</summary>

`n = min(head - tail, max)`개를 먼저 다 복사하고, 끝나고 `tail = t + n`을 한 번만 store합니다. 한 개마다 `tail`을 쓰면 캐시 라인이 producer 코어로 n번 왔다 갔다 합니다. 통계(`pushed`, `dropped`, `high_water`)도 producer만 쓰니 relaxed load + store면 됩니다.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — free-running 인덱스와 mask

인덱스를 slot 번호로 저장하지 않고 **계속 증가하는 카운터**로 둡니다. slot은 쓸 때만 `idx & (cap − 1)`로 계산합니다. 그러면 full/empty를 `head − tail` 하나로 구분하고, 2^32 wrap도 unsigned 산술이 알아서 처리합니다.

<div class="chart" data-src="chart1.svg" data-caption="테스트 5번 상황: 인덱스를 0xFFFFFFFE에서 시작해 5개 push. head는 wrap되어 3, tail은 0xFFFFFFFE인데 head − tail = 5로 정확합니다. cap이 2의 거듭제곱이어야 2^32가 cap의 배수라서 wrap 순간에도 slot이 7 → 0으로 자연스럽게 이어집니다"></div>

- **cap이 2의 거듭제곱이어야 하는 진짜 이유**: `%` 대신 `&`가 빠르다는 것도 있지만, 더 중요한 건 **정확성**입니다. cap = 6에 `idx % 6`을 쓰면 `0xFFFFFFFF % 6 = 3` 다음이 `0 % 6 = 0`이라서 slot 4, 5를 건너뜁니다.
- **drop-newest를 고른 이유**: SPSC에서 drop-oldest를 하려면 producer가 `tail`을 움직여야 합니다. 그러면 `tail`의 writer가 둘이 되어 CAS가 필요하고, consumer가 그 slot을 **복사하는 도중에** producer가 덮어쓸 수 있습니다(torn read). 로거 입장에서도 이미 쌓인 연속 구간을 지키고, 빠진 구간은 `seq` 빈칸 + `dropped` 카운터로 정확히 알 수 있으면 충분합니다. "최신 값만 중요"(화면 표시용 telemetry)일 때만 drop-oldest가 맞습니다 → §6.

### 4.2 acquire / release 타임라인

<div class="chart" data-src="chart2.svg" data-caption="두 쌍의 synchronizes-with. ③ release → ④ acquire: 레코드 쓰기(②)가 consumer에게 보인 뒤에만 새 head가 보입니다. ⑥ release → ⑦ acquire: consumer가 slot을 다 읽은 뒤에만 producer가 그 slot을 덮어씁니다"></div>

- **②→③ 순서가 핵심**: 레코드를 먼저 다 쓰고, 그 다음 `head`를 release로 공개합니다. release는 "이 store 앞의 모든 메모리 접근이 이 store보다 먼저 보인다"는 약속입니다.
- **④ acquire**: consumer가 새 `head`를 봤다면, 그 이후의 읽기(⑤)는 ② 이전 값을 볼 수 없습니다.
- **반대 방향(⑥ → ⑦)도 필요**: 이걸 빠뜨리면 WAR(write-after-read) 문제가 생깁니다. Arm에서는 consumer의 slot 읽기가 `tail` store보다 늦게 실행될 수 있어서, producer가 먼저 덮어쓴 값을 consumer가 읽을 수 있습니다.
- **실측**: 이 Mac(arm64, Apple M-series)에서 `head` store/load만 relaxed로 바꾼 버전을 돌려 보면 테스트 6번이 `-O2`에서 5번 중 5번, ASan `-O0`에서도 3번 중 2번 실패했습니다(bad record + checksum 불일치). `tail`만 relaxed로 바꾼 버전은 이 실험에서는 통과했습니다. 통과했다고 맞는 코드는 아닙니다. 메모리 모델상 틀린 코드는 언젠가 다른 코어/컴파일러에서 터집니다.

### 4.3 손으로 따라가기 (trace)

테스트 5번: `cap = 8`, `ring_test_set_index(0xFFFFFFFE)`.

| 동작 | head 뒤 | tail 뒤 | used | 결과 | 비고 |
|---|---|---|---|---|---|
| push seq 0..4 | 3 | 0xFFFFFFFE | 5 | 5 × true | seq 0 → slot 6, seq 1 → slot 7, seq 2 → slot 0, seq 4 → slot 2. head는 0xFFFFFFFE + 5 = 2^32 + 3 → **3** |
| push seq 5, 6, 7 | 6 | 0xFFFFFFFE | 8 | 3 × true | slot 3, 4, 5. 이제 full |
| push seq 8 | 6 | 0xFFFFFFFE | 8 | **false** | `used == cap` → drop-newest, `dropped = 1` |
| pop_batch(max 3) | 6 | **1** | 5 | 3개: seq 0, 1, 2 | tail 0xFFFFFFFE + 3 = 2^32 + 1 → **1** |
| push seq 9, 10, 11 | 9 | 1 | 8 | 3 × true | slot 6, 7, 0 재사용. 다시 full |
| pop_batch(max 16) | 9 | 9 | 0 | 8개: seq 3, 4, 5, 6, 7, 9, 10, 11 | seq 8 자리가 빈칸 → `dropped`와 일치 |
| stats | | | | pushed 11, dropped 1, high_water 8 | |

`ring_push` 한 번의 판단 (위 표 3번째 줄): `h = 6`, `t = 0xFFFFFFFE` → `used = 6 − 0xFFFFFFFE = 8 (mod 2^32)` = `cap` → drop.

스트레스 테스트(6번) 출력 예 — **실행마다 숫자가 다릅니다** (스케줄링에 따라):

```
  run 0: cap 1024  pushed 1000000  dropped      0  high_water  109  batches 57312
  run 1: cap    8  pushed  460815  dropped 539185  high_water    8  batches 115236
ALL PASS
```

- run 0 (cap 1024, burst 256 뒤 잠깐 쉼): 보통 drop 0, high_water 100~700대. 가끔 스케줄러가 consumer를 늦게 깨우면 drop 수천 개 + high_water 1024가 나옵니다. 이것도 검사는 통과합니다(카운트가 다 맞으니까). 실제 시스템에서 "가끔 storage stall" 상황과 같습니다.
- run 1 (cap 8, burst 64): 일부러 drop을 많이 내서 drop 경로와 `gaps == dropped` 검사를 동시성 하에서 확인합니다. 테스트는 여기서 `dropped > 0`, `high_water == 8`을 요구합니다.
- 숫자가 바뀌어도 항상 성립해야 하는 불변식만 CHECK합니다: `pushed + dropped == N`, `popped == pushed`, seq 엄격 증가, `gaps == dropped`, 체크섬 일치, 레코드 바이트 전부 일치.

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- **자기 인덱스는 relaxed, 상대 인덱스는 acquire, 자기 인덱스 공개는 release**. 이 세 줄 패턴이 SPSC의 전부입니다. Linux `kfifo`, DPDK `rte_ring`(SP/SC 모드), LMAX Disruptor도 같은 구조입니다.
- `stat_inc`는 `atomic_fetch_add`(RMW) 대신 load + store입니다. writer가 하나라서 RMW가 필요 없고, Arm에서 LL/SC 루프나 `LDADD`를 피합니다. 그래도 `_Atomic`으로 둔 이유는 다른 thread가 `ring_stats`로 읽을 때 data race(UB)가 되지 않게 하기 위해서입니다.
- `Record`는 정확히 64바이트이고 `_Static_assert`로 고정합니다. slot 하나 = 캐시 라인 하나라서, producer가 slot k를 쓰는 동안 consumer가 slot k−1을 읽어도 같은 라인을 두고 싸우지 않습니다.
- `head`와 `tail`을 `_Alignas(CACHE_LINE)`으로 떼어 놓은 이유는 **false sharing**입니다. 같은 라인에 있으면 producer가 `head`를 쓸 때마다 consumer 코어의 그 라인이 invalidate되고, consumer가 `tail`을 쓸 때도 반대로 됩니다. 정확성은 같지만 처리량이 크게 떨어집니다. Zynq MPSoC의 A53은 64바이트, Apple M-series는 128바이트 라인입니다.
- 테스트의 consumer 종료 조건: `pop == 0`이고 producer의 `done`(release/acquire)을 본 뒤 **한 번 더 pop**해서 0이어야 끝냅니다. `done`을 본 순간 링에 마지막 레코드가 남아 있을 수 있기 때문입니다. `popped > N`이면 링이 고장 난 것이니 바로 빠져나와 hang을 막습니다.
- 검증은 **두 개의 독립된 측정이 일치하는지**를 봅니다: 링 내부 `dropped` 카운터 vs consumer가 본 seq 빈칸 수. 하나만 보면 카운터 버그를 못 잡습니다.

### 4.5 복잡도

- `ring_push`: O(1), 64바이트 복사 1번 + atomic load 2번 + store 1~3번. 대기/스핀 없음 → **wait-free**(유한 단계 안에 반드시 끝남).
- `ring_pop_batch`: O(n), n = 꺼낸 개수. `tail` store는 batch당 1번.
- 메모리: 호출자 버퍼 `cap × 64` 바이트 + `Ring` 구조체(캐시 라인 3개 정도).

---

## 5. 임베디드 관점으로 말할 것

- **메모리 순서: x86에선 되고 Arm에선 깨지는 이유**: x86은 TSO라서 store끼리, load끼리 순서가 유지됩니다. 그래서 relaxed로 짜도 CPU 쪽에선 우연히 동작합니다(컴파일러 재배치는 여전히 가능). Arm은 weakly ordered라서 레코드 store보다 `head` store가 먼저 다른 코어에 보일 수 있습니다. C11 release/acquire는 Arm64에서 `STLR`/`LDAR`(또는 `LDAPR`)로, x86에서는 일반 `MOV`로 컴파일됩니다. 즉 x86에선 공짜이고 Arm에서만 비용이 생기는데, 그 비용이 정확히 필요한 비용입니다.
- **`volatile`로는 부족한 이유**: `volatile`은 "이 접근을 생략하거나 다른 volatile 접근과 순서를 바꾸지 마라"는 **컴파일러에게만** 하는 말입니다. (1) volatile이 아닌 레코드 복사는 volatile `head` store 앞뒤로 자유롭게 옮겨질 수 있고, (2) CPU 배리어(`dmb`)를 하나도 만들지 않으며, (3) C 표준상 thread 간 동기화 의미가 없어서 data race = UB입니다. `volatile`은 MMIO 레지스터용입니다. Linux 커널은 `smp_store_release()` / `smp_load_acquire()`를 씁니다.
- **베어메탈 ISR 버전 (single core)**: producer = ISR, consumer = main loop. 코어가 하나면 CPU는 자기 store를 프로그램 순서대로 봅니다. 그래서 **IRQ disable이 필요 없습니다**: 각 인덱스의 writer가 하나이고, 정렬된 32-bit load/store는 Cortex-M에서 원자적입니다(쪼개지지 않음). 필요한 건 **컴파일러 배리어**뿐입니다(`atomic_signal_fence` 또는 C11 atomics를 그대로 써도 됨). MPMC거나 64-bit 카운터를 32-bit 코어에서 쓰면 그때는 IRQ disable(critical section)이 필요합니다.
- **producer가 HW(DMA)일 때**: 레코드를 CPU가 아니라 DMA가 DDR에 씁니다. 캐시 coherent가 아니면(Zynq MPSoC의 HP 포트 등) consumer는 새 `head`(HW 레지스터나 descriptor DONE 비트)를 본 뒤 `dma_rmb()` → 레코드 영역 **cache invalidate** → 읽기 순서를 지켜야 합니다. 레코드를 캐시 라인 크기·정렬에 맞춰 두면 invalidate가 옆 데이터를 망가뜨리지 않습니다. coherent 포트(ACP/HPC)를 쓰면 invalidate는 빠지고 배리어만 남습니다 → [V3](../V3_dma_desc_ring/index.html).
- **drop / high-water는 검증 신호**: `dropped`는 qualification 런에서 0이어야 하는 pass/fail 지표이고, `high_water / cap`은 **여유(headroom)**입니다. 예를 들어 최악 burst에서 70%면 저장소 stall이 조금만 길어져도 drop이 난다는 경고입니다. 링 크기는 `burst 크기 + (최장 storage stall × 입력 rate)`로 잡고, 이 두 카운터를 런마다 로그·대시보드에 올립니다. 로거 파일의 seq 빈칸 검사와 교차 확인하면 카운터 자체의 버그도 잡힙니다 → [V12](../V12_frame_timestamp_check/index.html), 처리량 측정은 [L3](../L3_rfc2544_throughput/index.html).
- **mutex를 쓰는 게 맞는 경우**: producer나 consumer가 여럿(MPMC)일 때, 레코드가 가변 길이라 reserve/commit이 복잡할 때, consumer가 폴링 대신 **잠들어야** 할 때(condvar), 처리량이 낮아 단순함이 더 중요할 때. 단 RT 경로에서 mutex를 쓰면 priority inversion을 막기 위해 PI mutex(`PTHREAD_PRIO_INHERIT`)를 씁니다. ISR에서는 mutex를 아예 못 씁니다.

## 5.1 면접관 Iris Lee(SW·시스템 출신 Principal)가 파고들 포인트

- **"Why release/acquire and not relaxed — and how would you know it's wrong?"** → x86은 TSO라 relaxed 버그가 숨고, Arm(MPSoC의 A53/A72)에서만 드러납니다. 실제로 이 Mac(arm64)에서 relaxed head 버전이 스트레스 테스트에서 bad record를 냈습니다. 그래서 동시성 코드는 **타깃 아키텍처에서** seq + 체크섬 스트레스로 검증합니다. *"Relaxed passes on x86 because of TSO; on the A53 the head store can overtake the payload stores, so I test on the Arm target with sequence numbers and checksums, not just on my laptop."*
- **"How do you prove it's thread-safe? What does your test actually check?"** → 숫자가 매번 달라도 성립하는 **불변식**만 검사합니다: `pushed + dropped == N`, `popped == pushed`, seq 엄격 증가, seq 빈칸 == `dropped`, 바이트 단위 내용 일치. 인덱스는 2^32 근처에서 시작해 wrap도 같이 지납니다. 그리고 일부러 relaxed로 바꾼 버전이 테스트를 **빨갛게 만드는지** 확인합니다(mutation). *"I check invariants that hold for any interleaving, start the indices near 2^32, and I mutate the ordering to relaxed to confirm the test actually turns red."*
- **"On the real box, the producer is an ISR or a DMA engine. What changes?"** → ISR + single core면 IRQ disable 없이 컴파일러 배리어만 필요합니다. DMA가 producer면 HW가 보여 주는 인덱스를 읽고 `dma_rmb()` + (non-coherent면) cache invalidate 후 읽습니다. Linux에서는 이 링이 보통 커널 드라이버 ↔ 유저 공간 사이(`mmap`된 링, UIO)에 놓입니다 → [L1](../L1_uio_mmio_poll/index.html). *"Single-core ISR: no IRQ masking, just a compiler barrier. DMA producer: read the hardware index, dma_rmb, invalidate if non-coherent, then read the record."*
- **"The storage write stalls for 50 ms. Does the capture side survive? How did you size the ring?"** → capture는 절대 block하지 않으니 살아남고, 대신 drop이 카운트됩니다. 링 크기 = 최장 stall × 입력 rate + burst. 예: 20k records/s × 50 ms = 1,000개 → 1024는 빠듯하고 2048이 안전. `high_water`로 실측 여유를 확인합니다. *"Capture never blocks; the ring absorbs the stall. I size it as worst-case stall times input rate plus a burst, and I watch high-water to see the real margin."*
- **"When would you just use a mutex?"** → 여러 producer/consumer, 가변 길이, consumer가 잠들어야 할 때, 그리고 처리량이 낮아 측정해 봐도 락 비용이 안 보일 때. lock-free는 비용(복잡도, 검증 난이도)이 있는 최적화입니다. *"Lock-free earns its complexity only for one producer, one consumer on a hot path; with multiple writers or low rates I'd take a mutex plus a condition variable and measure first."*

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| Now there are two capture threads. What breaks? | `head`의 writer가 둘이 되어 load → store 사이에 서로 덮어씁니다. 방법: (1) 센서마다 SPSC 링 하나씩(가장 단순, 락 없음, 로거가 여러 링을 순회), (2) MPSC: CAS로 slot을 예약하고 slot마다 "ready" 플래그/sequence를 둬서 순서대로 commit(DPDK `rte_ring` MP 모드, Vyukov 큐), (3) mutex. 저는 (1)을 먼저 제안합니다. |
| How would you implement drop-oldest instead? | producer가 full일 때 `tail`을 밀면 writer가 둘이 되어 위험합니다. 안전한 방법: producer는 무조건 덮어쓰고 레코드에 seq를 넣음, consumer가 읽은 뒤 seq를 확인해서 "한 바퀴 밀렸다(lapped)"면 버리고 `tail`을 `head − cap`으로 점프. 또는 slot마다 seqlock(쓰기 전 홀수, 쓴 뒤 짝수)을 둬서 consumer가 복사 도중 덮어쓰기를 감지합니다. |
| The logger should sleep when the ring is empty, not spin. | consumer가 링이 비면 `eventfd`/futex/semaphore에서 대기하고, producer는 **empty → non-empty로 바뀔 때만** 깨웁니다(매 push마다 syscall 하면 느림). lost wake-up을 막으려면 consumer가 잠들기 전에 "sleeping" 플래그를 세우고 한 번 더 링을 확인해야 하며, 그 사이에는 seq_cst fence가 필요합니다(store→load 순서는 acquire/release로 보장되지 않음). |
| Can you avoid the double copy (record → ring → out)? | reserve/commit API: `Record *ring_reserve()`로 slot 포인터를 받아 그 자리에 직접 채우고 `ring_commit()`에서 release store. consumer도 `peek(n)`으로 slot 포인터를 받아 바로 storage에 쓰고 `release(n)`. DMA가 링에 직접 쓰는 zero-copy 구조와 같은 모양입니다. |
| Your counters are uint32_t. Is that a problem? | `head`/`tail`은 차이만 보니 wrap해도 괜찮습니다(테스트 5번). `pushed`/`dropped`는 100k rec/s면 약 12시간에 wrap합니다. 장기 런이면 `uint64_t`로 바꾸는데, 32-bit 코어(Cortex-R/M)에서는 `_Atomic uint64_t`가 lock-free가 아닐 수 있으니 `atomic_is_lock_free`로 확인하거나, 리포터가 주기적으로 읽어 delta를 누적합니다. |
| Producer and consumer always touch each other's index. Can you reduce that traffic? | 상대 인덱스를 로컬에 **캐시**합니다. producer는 `tail_cache`를 두고 `head − tail_cache < cap`이면 `tail`을 안 읽고 진행, 꽉 찬 것처럼 보일 때만 진짜 `tail`을 acquire로 다시 읽습니다. consumer도 `head_cache`. 이러면 공유 라인 이동이 batch당 한 번 수준으로 줄어듭니다(rigtorp SPSCQueue, DPDK가 쓰는 방식). |
| Why must cap be a power of two? Could you support cap = 1000? | free-running 인덱스 + `idx % cap`은 2^32가 cap의 배수일 때만 wrap에서 연속입니다. cap = 1000을 지원하려면 인덱스를 `[0, 2·cap)` 범위에서 직접 감거나(`if (++i == 2*cap) i = 0`), slot 번호 + "한 칸 비우기" 방식으로 바꿔야 합니다. 보통은 그냥 1024로 올립니다. |
| How would you benchmark this ring? | producer/consumer를 서로 다른 코어에 고정(`pthread_setaffinity_np`)하고, N개 전송 시간을 재서 Mrec/s, 레코드에 넣은 `ts_ns`로 push→pop 지연의 p50/p99를 봅니다. false sharing 버전(head/tail 같은 라인)과 비교하면 `_Alignas` 효과가 숫자로 보입니다 → [V4](../V4_perf_bench_stats/index.html). |

## 7. 자주 하는 실수

- `head`를 레코드 복사 **전에** 올림 → consumer가 아직 안 쓴 slot을 읽음. release를 써도 순서가 틀리면 소용없음
- 전부 `memory_order_relaxed`(또는 `volatile`)로 짬 → x86 노트북에선 통과, Arm 타깃에선 가끔 깨진 레코드 (이 Mac에서도 테스트 6번 실패)
- `tail` 쪽 acquire/release를 빠뜨림 → producer가 consumer가 읽는 중인 slot을 덮어씀(WAR). 드물게만 터져서 더 위험
- full 판정을 `head >= tail + cap`이나 `if (head > tail)` 분기로 함 → 인덱스가 2^32를 넘는 순간 틀림 (테스트 5번)
- slot을 `head % cap`으로 계산하면서 cap이 2의 거듭제곱인지 init에서 검사 안 함 → cap = 6이면 wrap에서 slot이 튐
- full일 때 producer가 빈 칸이 날 때까지 spin → "capture never blocks" 위반, 로거가 멈추면 ISR도 멈춤 (테스트 watchdog)
- drop-oldest를 하려고 producer가 `tail`을 직접 증가 → writer 둘, consumer 복사 중 덮어쓰기
- batch pop에서 레코드마다 `tail`을 store → 맞긴 하지만 캐시 라인 핑퐁으로 느림
- 스트레스 테스트에서 producer `done`만 보고 바로 종료 → 링에 남은 마지막 레코드를 놓쳐 `popped != pushed`
