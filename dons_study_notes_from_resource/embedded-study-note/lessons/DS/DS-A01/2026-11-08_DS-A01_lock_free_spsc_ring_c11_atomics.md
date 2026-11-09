---
id: DS-A01
time: 110분
updated: 2026-11-08
---
# DS-A01 Lock-free SPSC 큐: C11 atomics

## 학습 목표

- 2의 거듭제곱 용량 + **자유 증가(free-running) head/tail** 로 SPSC 링을 만들고, 빈 칸 낭비 없이 `tail - head == 0`(empty) / `== cap`(full) 로 판별하는 이유를 unsigned 산수로 설명한다.
- push/pop 의 **load 2개 + store 1개 각각에 어떤 memory_order 가 필요한지, 왜 그런지** 를 happens-before 두 갈래(게시 방향, 반납 방향)로 그려 말할 수 있다.
- 순서를 relaxed 로 약하게 만든 버전이 이 맥(arm64)에서 **실제로 옛 메시지를 읽는 것** 을 측정하고, `ldapr/stlr` vs `ldr/str` 차이를 실제 `-S` 출력으로 보인다. TSan 이 무엇을 잡는지도 확인한다.
- 상대 인덱스 캐싱(cached opposite index)으로 2억 개 메시지 처리량이 몇 배 바뀌는지 재고, 그 이유를 캐시 라인 이동 횟수로 설명한다.
- 펌웨어로 옮긴다: UART RX ISR → main, Cortex-M4 에서 atomics 가 `dmb sy` 로 내려가는 것, 단일 코어면 `atomic_signal_fence` 로 충분한 이유, DMA 는 atomics 가 아니라 캐시 유지보수 문제라는 것, 생산자가 둘이면 무엇이 깨지는지.

## 1. 문제

**한국어 문제 소개.** 생산자 스레드 하나, 소비자 스레드 하나가 고정 크기 링 버퍼로 메시지를 주고받는다. 락은 쓰지 않는다. `head`(소비자가 다음에 읽을 위치)와 `tail`(생산자가 다음에 쓸 위치)을 `atomic_uint` 로 두고, 각 인덱스는 **자기 쪽만 쓴다**. 이 두 인덱스의 load/store 에 붙이는 memory_order 만으로 "소비자는 다 써진 메시지만 읽고, 생산자는 다 읽힌 칸만 덮어쓴다" 를 보장해야 한다. 그다음 두 스레드로 1천만 개(검사)·2억 개(측정) 메시지를 보내 순서·무손실·무중복을 확인하고, 인덱스 캐싱 전후의 처리량을 잰다.

왜 중요한가: SPSC 링은 펌웨어에서 가장 많이 쓰는 lock-free 구조다 — UART/SPI RX ISR → main 루프, 듀얼 코어 MCU 의 코어 간 메일박스, 오디오 DMA 블록 전달, 로거(LLAT-A06). 면접에서는 "head/tail 에 각각 어떤 memory_order 가 필요한가?" 가 거의 그대로 나온다 (동시성 가이드 5층 면접 체크 1번).

이미 배운 것은 다시 설명하지 않는다: release/acquire 게시와 반납 방향의 필요성은 C-A03 의 1칸 채널(이 레슨은 그것의 N칸 버전), 자유 증가 카운터의 랩어라운드 증명은 DS-B04, ISR 공유 변수는 C-M14, 메시지 패싱 리트머스는 OS-M08, atomic 비용은 LLAT-B06.

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">spsc_t 배치: 줄(line)마다 쓰는 스레드가 하나뿐 (128 B 줄, Apple M6)</text>
  <rect x="10" y="34" width="230" height="58" class="f-acc" stroke="currentColor"/>
  <text x="20" y="54" fill="currentColor">line 0 — 생산자만 씀</text>
  <text x="20" y="74" fill="currentColor" font-family="monospace">tail, head_cache</text>
  <rect x="255" y="34" width="230" height="58" class="f-ok" stroke="currentColor"/>
  <text x="265" y="54" fill="currentColor">line 1 — 소비자만 씀</text>
  <text x="265" y="74" fill="currentColor" font-family="monospace">head, tail_cache</text>
  <rect x="500" y="34" width="230" height="58" class="f-mut" stroke="currentColor"/>
  <text x="510" y="54" fill="currentColor">line 2 — init 후 읽기 전용</text>
  <text x="510" y="74" fill="currentColor" font-family="monospace">mask, buf</text>
  <text x="10" y="126" fill="currentColor">cap = 8 (mask = 7), head = 13, tail = 18 → 칸 번호 = 인덱스 &amp; 7</text>
  <g stroke="currentColor">
    <rect x="40" y="140" width="80" height="40" class="f-acc"/>
    <rect x="120" y="140" width="80" height="40" class="f-acc"/>
    <rect x="200" y="140" width="80" height="40" fill="none"/>
    <rect x="280" y="140" width="80" height="40" fill="none"/>
    <rect x="360" y="140" width="80" height="40" fill="none"/>
    <rect x="440" y="140" width="80" height="40" class="f-acc"/>
    <rect x="520" y="140" width="80" height="40" class="f-acc"/>
    <rect x="600" y="140" width="80" height="40" class="f-acc"/>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="58" y="165">[0] 16</text><text x="138" y="165">[1] 17</text>
    <text x="218" y="165">[2]</text><text x="298" y="165">[3]</text><text x="378" y="165">[4]</text>
    <text x="458" y="165">[5] 13</text><text x="538" y="165">[6] 14</text><text x="618" y="165">[7] 15</text>
  </g>
  <path d="M480 220 L480 186" stroke="currentColor" stroke-width="2"/>
  <path d="M474 194 L480 184 L486 194" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="430" y="238" fill="currentColor">head = 13 (13 &amp; 7 = 5)</text>
  <path d="M240 220 L240 186" stroke="currentColor" stroke-width="2"/>
  <path d="M234 194 L240 184 L246 194" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="190" y="238" fill="currentColor">tail = 18 (18 &amp; 7 = 2)</text>
  <g fill="currentColor">
    <text x="10" y="268">size = tail − head = 5 (unsigned). empty: tail − head == 0, full: tail − head == 8 → 8칸 전부 사용.</text>
    <text x="10" y="290">인덱스는 mask 하지 않고 계속 증가 (2^32 에서 0 으로 넘어가도 차이는 맞다, DS-B04 증명).</text>
    <text x="10" y="312">채운 칸 = 메시지 seq (생산 순서). 13 이 가장 오래된 것.</text>
  </g>
</svg>
```

| 이 레슨 (DS-A01) | 다른 레슨 |
|---|---|
| SPSC 링의 정확성(memory_order 근거), 캐싱 1단계 전후 처리량 | 패딩·배치·슬롯 플래그·p99 지연 튜닝 = LLAT-M01 |
| 생산자 둘이면 깨지는 것(강제 스케줄) | 다중 생산자/소비자 = DS-A03 (Vyukov), Rust 타입으로 SPSC 보장 = RUST-M07 |
| UART RX 바이트 링(ISR→main) | 단일 코어 ISR 링 배리어 분석·bip buffer = DS-A02 |

### Part A — SPSC 링

- `spsc_init_at(q, buf, cap, start)` — cap 이 2의 거듭제곱이고 1..2^31 이면 head = tail = start. (테스트는 start 를 `0xFFFFFFFE` 처럼 잡아 랩어라운드를 바로 밟는다.)
- `spsc_push(q, &m)` — 가득이면 false. 생산자: **relaxed load(tail) → acquire load(head) → 칸 쓰기 → release store(tail)**.
- `spsc_pop(q, &out)` — 비었으면 false. 소비자는 거울: relaxed load(head) → acquire load(tail) → 칸 읽기 → release store(head).
- `spsc_size(q)` — 소비자 쪽에서 본 개수.
- 검사: 단위 테스트(FIFO, 4칸 전부 사용, full/empty) + 2스레드 1천만 개: seq 가 정확히 0,1,2,…(순서·무손실·무중복), `payload == mix(seq)`(내용 무결), 체크섬 일치.

### Part B — 깨진 버전과 인덱스 캐싱

- (a) **전부 relaxed** 버전(`demo_relaxed.c`): 이 맥에서 실제로 옛 메시지를 읽는지 세어 보고, 컴파일된 명령 차이를 `-S` 로 본다. TSan 결과도 본다.
- (b) 시작 코드의 TODO 스텁은 결정적으로 FAIL 한다 (스트레스는 단위 테스트가 통과해야만 돈다 → 멈추지 않음).
- `spsc_push_cached` / `spsc_pop_cached` — 상대 인덱스의 사본(`head_cache`, `tail_cache`)을 먼저 믿고, "가득/빔" 으로 보일 때만 진짜 인덱스를 acquire 로 다시 읽는다. 경계 검사: 인덱스가 `0xFFFFFFFF → 0` 을 넘는 경우, 용량 1, 캐시가 낡아서 "가득" 이라 하지만 다시 읽으면 여유가 있는 경우.
- `demo_bench.c`: 2억 개 × 3회, 캐싱 전/후 Mops/s 와 load average.

### Part C — UART RX: ISR 생산자 → main 소비자

- `uart_rx_isr(r, byte)` — 수신 인터럽트가 한 바이트를 넣는다. 가득이면 **새 바이트를 버리고** `overrun` 을 센 뒤 false (ISR 은 기다리지 않는다, 오래된 바이트는 main 소유라 건드릴 수 없다).
- `uart_read(r, dst, max)` — main 이 한 번에 최대 max 바이트를 가져간다: acquire 1번 → `memcpy` 최대 2조각(버퍼 끝에서 감김) → release 1번.
- 검사: main 이 안 읽을 때 300 바이트 → 정확히 256 저장 + 44 overrun, 인덱스 랩 + 두 조각 복사, 2스레드("ISR" 스레드) 250만 바이트 스트림 정확.

## 2. Problem (English)

> **Lock-free single-producer/single-consumer ring buffer with C11 atomics.**
>
> ```c
> typedef struct { uint64_t seq; uint64_t payload; } msg_t;
> typedef struct {
>     _Alignas(128) atomic_uint tail; uint32_t head_cache;   // producer-owned line
>     _Alignas(128) atomic_uint head; uint32_t tail_cache;   // consumer-owned line
>     _Alignas(128) uint32_t mask; msg_t *buf;               // read-only after init
> } spsc_t;
> bool spsc_init_at(spsc_t *q, msg_t *buf, uint32_t cap, uint32_t start);
> bool spsc_push(spsc_t *q, const msg_t *m);       // false if full
> bool spsc_pop(spsc_t *q, msg_t *out);            // false if empty
> bool spsc_push_cached(spsc_t *q, const msg_t *m);
> bool spsc_pop_cached(spsc_t *q, msg_t *out);
> ```
>
> Constraints: `cap` is a power of two in `[1, 2^31]`; all `cap` slots are usable (no wasted slot); indices are free-running 32-bit counters. Exactly one thread calls push, exactly one calls pop. No locks, no RMW instructions. Use the weakest memory orders that are correct and justify each one. Then implement a UART RX ring: `bool uart_rx_isr(uart_rb_t*, uint8_t)` (drop + count on overrun, never blocks) and `size_t uart_read(uart_rb_t*, uint8_t *dst, size_t max)` (bulk, one acquire and one release per call).
>
> Example: `cap = 4`, start = `0xFFFFFFFE`; four pushes succeed and leave `tail == 2`; `tail - head == 4` → full; one pop returns seq 0; the next push succeeds.
>
> Follow-ups: Which ordering failure does each relaxed operation allow on ARM? Is push wait-free? What does the cached index buy, and why is it still correct? What breaks with two producers? What do these atomics compile to on a single-core Cortex-M4, and what changes when the producer is a DMA engine?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력, exit 1, 0.1 s):

```text
DS-A01 SPSC ring  (stress N = 10000000)
Part A — SPSC ring
FAIL starter.c:264  spsc_init(&q, b4, 4u)
FAIL starter.c:268  all4 && spsc_size(&q) == 4u
FAIL starter.c:270  spsc_pop(&q, &out) && out.seq == 0u && out.payload == mix(0)
FAIL starter.c:271  spsc_push(&q, &m)
FAIL starter.c:274  ord && !spsc_pop(&q, &out)
FAIL starter.c:275  atomic_load(&q.tail) == 5u && atomic_load(&q.head) == 5u
SKIP 5 stress checks (Part A): unit checks failed
Part B — cached index + edges
FAIL starter.c:287  spsc_init_at(&q, b4, 4u, 0xFFFFFFFEu)
…(Part B 단위 테스트 FAIL 10줄 더)…
SKIP 10 stress checks (Part B): unit checks failed
Part C — UART RX ring
FAIL starter.c:325  stored == 256u && atomic_load(&g_uart.overrun) == 44u
FAIL starter.c:330  okc
FAIL starter.c:338  okw
SKIP 2 stress checks (Part C): unit checks failed
7/44 passed
```

하네스 설계:

- 각 Part 의 2스레드 스트레스는 **그 Part 의 단위 테스트가 모두 통과해야** 돈다. 그래서 `return false` 스텁을 기다리며 무한 루프에 빠지지 않는다. 그래도 혹시 모를 버그(예: 캐시를 다시 안 읽는 pop)에 대비해 한 메시지를 5 s 동안 못 넣거나 못 빼면 포기하고 FAIL 한다.
- CHECK 는 올바른 구현이면 **매번** 성립하는 것만 본다: 받은 개수 == N, seq 가 기대값과 같지 않은 횟수 == 0, `payload != mix(seq)` 횟수 == 0, 보낸 체크섬 == 받은 체크섬, overrun == ISR 이 본 false 횟수. Mops/s 와 ISR drop 개수는 정보로만 출력한다.
- 메시지 수: 기본 1e7, ASan/TSan 빌드는 `__has_feature` 로 감지해 1e6, 환경변수 `SPSC_N=…` 로 덮어쓸 수 있다.

관찰용 파일 (하네스와 별개, 모두 실제로 돌린 명령):

```bash
cc -std=c11 -O2 -pthread demo_relaxed.c -o dr && ./dr 3                    # 정답 순서: 깨짐 0
cc -std=c11 -O2 -pthread -DALL_RELAXED demo_relaxed.c -o drr && ./drr 5    # 전부 relaxed
cc -std=c11 -O2 -pthread -DPUB_ST=memory_order_relaxed demo_relaxed.c ...  # 한 연산만 약하게 (PUB_LD/RET_ST/RET_LD)
cc -std=c11 -g -O1 -fsanitize=thread -pthread -DALL_RELAXED -DN=100000 demo_relaxed.c -o drt && ./drt 1
cc -std=c11 -O2 -S demo_asm.c -o -                                          # arm64 (이 맥)
clang --target=aarch64-none-elf -mcpu=cortex-a53 -O2 -S demo_asm.c -o -     # ARMv8.0 코어 (ldar)
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_asm.c -o -
cc -std=c11 -O2 -pthread demo_bench.c -o db && ./db 200000000 3             # 2억 개 × 3회, 약 35 s
```

## 4. 힌트

<details>
<summary>힌트 1 — full/empty 를 빈 칸 없이 구분하려면</summary>

인덱스를 `& mask` 해서 저장하면 head == tail 이 "비었음" 과 "가득" 둘 다를 뜻하게 된다(그래서 한 칸을 비워 두는 고전 방식이 나온다, DS-B03). 인덱스를 **mask 하지 않고 계속 증가** 시키고, 칸에 접근할 때만 `& mask` 하자. 그러면 `tail - head` 가 0..cap 의 cap+1 가지 값을 모두 구분한다. unsigned 뺄셈이 2^32 에서 넘어가도 맞는 이유는 DS-B04 Part B.
</details>

<details>
<summary>힌트 2 — 내 인덱스는 relaxed 로 읽어도 된다</summary>

tail 을 쓰는 스레드는 생산자 하나뿐이다. 생산자가 자기 tail 을 읽으면 coherence(같은 객체의 수정 순서) 때문에 **자기가 마지막에 쓴 값** 을 본다 (C11 5.1.2.4 의 write-read coherence). 다른 스레드에서 받아 올 데이터가 없으니 acquire 가 할 일이 없다. 사실 지역 변수에 들고 있어도 된다.
</details>

<details>
<summary>힌트 3 — 상대 인덱스 load 는 무엇을 "받는가"</summary>

소비자가 tail 을 acquire 로 읽어 새 값을 보면, 생산자가 그 tail 을 release 로 쓰기 **전에** 한 칸 쓰기가 전부 보인다 (게시 방향). 생산자가 head 를 acquire 로 읽어 새 값을 보면, 소비자가 head 를 release 로 쓰기 전에 한 칸 **읽기** 가 끝났다는 것이 보장된다 → 그 칸을 덮어써도 된다 (반납 방향). 둘 다 필요하다 (C-A03 Part B 와 같은 논리).
</details>

<details>
<summary>힌트 4 — 캐시는 왜 낡아도 안전한가</summary>

head 는 증가만 한다. 생산자의 `head_cache` 는 예전에 읽은 head 이므로 `head_cache ≤ 실제 head`. 캐시로 계산한 여유 `cap - (t - head_cache)` 는 실제 여유보다 **작거나 같다** → 캐시로 "여유 있음" 이면 진짜 여유가 있다. 캐시로 "가득" 일 때만 진짜 head 를 다시 읽으면 된다. 소비자의 `tail_cache` 도 같은 논리. 단, "가득/빔" 일 때 **반드시 다시 읽어야** 한다 — 안 그러면 영원히 가득/빔이다.
</details>

<details>
<summary>힌트 5 — uart_read 의 두 조각</summary>

`n = wr - rd` (최대 max). 시작 칸 `off = rd & (SIZE-1)`, 첫 조각 `min(n, SIZE - off)`, 나머지는 칸 0 부터. 두 `memcpy` 가 끝난 **다음에** `rd + n` 을 release 로 한 번 쓴다. 바이트마다 release 하면 정확하지만 줄 이동이 n 배다.
</details>

## 5. 풀이

### Part A — 인덱스 산수와 네 개의 memory_order

**인덱스 산수.** 진짜(무한) 누적 개수를 H(뺀 총량), T(넣은 총량)라 하면 불변식은 `0 ≤ T − H ≤ cap`. 메모리에는 `T mod 2^32`, `H mod 2^32` 가 있다. C 의 unsigned 뺄셈은 mod 2^32 결과를 주고(6.2.5p9), 진짜 차이가 이미 `0..cap` 이고 `cap ≤ 2^31 < 2^32` 이므로 `tail - head` 는 진짜 차이와 **같다**. 칸 번호 `idx & mask = idx mod cap` 은 cap 이 2^32 의 약수(2의 거듭제곱)일 때만 랩어라운드를 넘어서도 이어진다 (DS-B04). 그래서 init 이 2의 거듭제곱이 아니면 거절한다. 테스트의 `start = 0xFFFFFFFE` 는 2^32 번 돌리지 않고 랩 경계를 바로 밟는 기법이다.

```c
static bool spsc_push(spsc_t *q, const msg_t *m) {
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_relaxed);   /* ① 내 인덱스 */
    uint32_t h = atomic_load_explicit(&q->head, memory_order_acquire);   /* ② 반납 받기 */
    if (t - h == q->mask + 1u) return false;
    q->buf[t & q->mask] = *m;                                            /*    일반 쓰기 */
    atomic_store_explicit(&q->tail, t + 1u, memory_order_release);       /* ③ 게시 */
    return true;
}
```

| 연산 | 순서 | 왜 이것인가 | 약하게 하면 (ARM 에서 허용되는 일) |
|---|---|---|---|
| 생산자 load(tail) | relaxed | 나만 쓰는 값, 받을 데이터 없음 | — (이미 가장 약함) |
| 생산자 load(head) | **acquire** | 소비자의 칸 읽기 → 내 덮어쓰기 순서 (반납) | 칸 쓰기가 head load 보다 먼저 수행 → 소비자가 아직 읽는 칸을 덮음 |
| 생산자 store(tail) | **release** | 칸 쓰기 → 게시 순서 | tail 이 칸 데이터보다 먼저 보임 → 소비자가 옛 칸을 읽음 |
| 소비자 load(head) | relaxed | 나만 쓰는 값 | — |
| 소비자 load(tail) | **acquire** | 게시 → 칸 읽기 순서 | 칸 읽기가 tail load 보다 먼저(추측 실행) → 옛 칸 |
| 소비자 store(head) | **release** | 칸 읽기 → 반납 순서 | head 가 칸 읽기 완료 전에 보임 → 생산자가 덮어씀 |

```svg
<svg viewBox="0 0 740 346" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">happens-before 두 갈래: ① 게시 (tail) ② 반납 (head) — 칸 k 하나를 따라간다</text>
  <text x="10" y="64" fill="currentColor">생산자</text>
  <text x="10" y="224" fill="currentColor">소비자</text>
  <rect x="70" y="44" width="140" height="32" class="f-mut" stroke="currentColor"/>
  <text x="78" y="65" fill="currentColor" font-family="monospace">buf[k] = m</text>
  <rect x="220" y="44" width="170" height="32" class="f-acc" stroke="currentColor"/>
  <text x="226" y="65" fill="currentColor" font-family="monospace">st(tail,k+1,rel)</text>
  <rect x="470" y="44" width="160" height="32" class="f-acc" stroke="currentColor"/>
  <text x="476" y="65" fill="currentColor" font-family="monospace">ld(head,acq)&gt;k</text>
  <rect x="640" y="44" width="92" height="32" class="f-warn" stroke="currentColor"/>
  <text x="646" y="65" fill="currentColor" font-family="monospace">buf[k]=m'</text>
  <rect x="150" y="204" width="160" height="32" class="f-acc" stroke="currentColor"/>
  <text x="156" y="225" fill="currentColor" font-family="monospace">ld(tail,acq)&gt;k</text>
  <rect x="320" y="204" width="130" height="32" class="f-mut" stroke="currentColor"/>
  <text x="328" y="225" fill="currentColor" font-family="monospace">out = buf[k]</text>
  <rect x="460" y="204" width="170" height="32" class="f-acc" stroke="currentColor"/>
  <text x="466" y="225" fill="currentColor" font-family="monospace">st(head,k+1,rel)</text>
  <path d="M305 76 L235 204" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M236 192 L235 204 L245 197" stroke="currentColor" stroke-width="2" fill="none"/>
  <text x="275" y="140" fill="currentColor">① synchronizes-with</text>
  <path d="M545 204 L550 76" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M544 87 L550 76 L556 87" stroke="currentColor" stroke-width="2" fill="none"/>
  <text x="560" y="150" fill="currentColor">② synchronizes-with</text>
  <text x="560" y="168" fill="currentColor">(cap 칸 뒤, 다음 바퀴)</text>
  <g fill="currentColor">
    <text x="10" y="270">① buf[k] 쓰기 →sb release tail →sw acquire tail →sb buf[k] 읽기: 온전한 m 을 본다.</text>
    <text x="10" y="292">② buf[k] 읽기 →sb release head →sw acquire head →sb buf[k] 덮어쓰기: 읽기가 먼저.</text>
    <text x="10" y="314">둘 다 happens-before 이므로 buf[k] 의 일반 접근은 데이터 레이스가 아니다 (C11 5.1.2.4p25).</text>
    <text x="10" y="336">sb = sequenced-before (같은 스레드 안의 순서), sw = synchronizes-with (release → 그 값을 읽은 acquire)</text>
  </g>
</svg>
```

**정확성 논증 (짧게).** 칸 k(인덱스 i, `k = i & mask`)에 대한 접근은 세 가지뿐이다: i 번째 쓰기 W_i(생산자), i 번째 읽기 R_i(소비자), 다음 바퀴 쓰기 W_{i+cap}.

1. 소비자가 R_i 를 하려면 `ld(tail, acq)` 가 어떤 값 v ≥ i+1 을 읽어야 한다. 그 값을 쓴 것은 생산자의 `st(tail, v, rel)` 하나다. acquire load 가 release store 의 값을 읽었으므로 둘은 synchronizes-with. W_i 는 `st(tail, i+1)` 이전에, 따라서 `st(tail, v)` 이전에 sequenced-before → **W_i happens-before R_i**. (tail 의 store 가 모두 release 라서 release sequence 규칙까지 갈 필요가 없다.)
2. 생산자가 W_{i+cap} 을 하려면 `t − h < cap` 즉 `h ≥ i+1` 을 acquire 로 읽어야 한다 → 소비자의 `st(head, ≥ i+1, rel)` 과 synchronizes-with, R_i 는 그보다 sequenced-before → **R_i happens-before W_{i+cap}**.
3. 따라서 같은 칸의 접근은 W_i → R_i → W_{i+cap} 로 전부 happens-before 로 줄 서고, R_i 는 W_i 의 값을 읽는다 (중간에 다른 쓰기 없음). 순서·무손실·무중복은 인덱스가 하나씩 증가하는 것에서 나온다.

RMW(CAS, fetch_add)가 하나도 없다는 점이 중요하다. 각 인덱스의 쓰는 사람이 하나뿐이라 "읽고-더하고-쓰기" 가 원자일 필요가 없다. 그래서 LDREX/STREX 가 없는 Cortex-M0 에서도 원리상 가능하다 (Part C 의 asm 참고).

**push/pop 은 wait-free 다.** 반복 루프가 없고 항상 몇 단계 안에 true/false 로 끝난다 (상대가 멈춰 있어도). 하네스의 "넣을 때까지 돌기" 래퍼는 wait-free 가 아니다 — 가득 차 있으면 소비자가 진행해야 끝난다. lock-free / wait-free / obstruction-free 의 정확한 정의는 DS-A03.

### Part B — 깨진 버전: relaxed 는 이 맥에서 실제로 깨진다

<!-- code: demo_relaxed.c -->

`demo_relaxed.c` 는 cap = 4 (경계에 자주 닿게), 메시지 2천만 개를 보내고 소비자가 `stale`(seq 가 기대보다 작음 = 지난 바퀴 내용), `ahead`(seq 가 큼 = 읽기 전에 덮임), `torn`(payload 불일치)을 센다. 측정: Apple M6, `sysctl -n hw.perflevel0.physicalcpu hw.perflevel1.physicalcpu hw.perflevel2.physicalcpu` = 2 4 6, Apple clang 21.0.0, load average 4.96–5.69 (다른 작성 세션들이 같이 벤치마크 중).

| 빌드 | tail st / ld | head st / ld | 실행 (각 2e7 개) stale | ahead | torn |
|---|---|---|---|---|---|
| 기본 (정답) | rel / acq | rel / acq | 0, 0, 0 | 0 | 0 |
| `-DALL_RELAXED` | rlx / rlx | rlx / rlx | 24346, 21884, 14870, 13193, 19812 | 0 | 0 |
| `-DPUB_ST=… -DPUB_LD=…relaxed` | rlx / rlx | rel / acq | 6034, 9526, 6312 | 0 | 0 |
| `-DPUB_ST=memory_order_relaxed` | rlx / acq | rel / acq | 18, 23, 10 | 0 | 0 |
| `-DPUB_LD=memory_order_relaxed` | rel / rlx | rel / acq | 50, 36, 54 | 0 | 0 |
| `-DRET_ST=… -DRET_LD=…relaxed` | rel / acq | rlx / rlx | 0, 0, 0 | 0 | 0 |

읽는 법 (정직하게):

- **게시 방향(tail)을 약하게 하면 실제로 깨진다.** 전부 relaxed 면 약 0.1% (2e7 중 1.3만–2.4만) 가 지난 바퀴의 옛 메시지다. store 쪽만 약하게 해도(ARM 의 store→store 재배치), load 쪽만 약하게 해도(load→load 재배치, 소비자 CPU 가 칸 읽기를 tail 읽기보다 먼저 추측 실행) 각각 수십 개씩 나온다. 둘 다 약하면 훨씬 많다.
- **반납 방향(head)을 약하게 한 것은 6천만 개 동안 한 번도 관찰되지 않았다.** 그렇다고 안전한 것이 아니다. 아키텍처는 "소비자의 칸 읽기가 head store 보다 늦게 수행되는 것"(load→store, LB 형태)을 허용하고, C11 기준 데이터 레이스(UB)이며, 아래처럼 TSan 은 바로 잡는다. 실측 0 은 "이 코어 구현에서는 store 가 앞의 load 보다 먼저 보이게 되는 일이 드물다" 정도의 뜻이다.
- **torn 은 0.** 16 B 칸을 생산자는 `stp` 한 개, 소비자는 `ldp` 한 개로 접근한다 (아래 asm). ARMv8.4 의 LSE2 는 정렬된 16 B `ldp/stp` 를 단일 복사 원자적(single-copy atomic)으로 만든다고 알고 있지만, 이 칩에서 그것이 이유인지는 확인하지 않았다 (미검증). 큰 메시지면 torn 도 나올 수 있다.
- 컴파일러는 이 경우 순서를 바꾸지 않았다(아래 asm: 칸 `stp` 다음에 tail `str`). 깨짐은 전부 **CPU** 재배치다. 다른 컴파일러/최적화 수준에서는 컴파일러가 바꿀 수도 있다 — relaxed 는 둘 다 허용한다.

**TSan** (전부 relaxed, N = 1e5, exit 134). 실행 결과는 우연히 깨짐 0 이었지만 TSan 은 칸 접근을 레이스로 보고한다:

```text
WARNING: ThreadSanitizer: data race (pid=298)
  Read of size 8 at 0x000104208100 by main thread:
    #0 main demo_relaxed.c:90 (drt_tsan:arm64+0x10000077c)
  Previous write of size 8 at 0x000104208100 by thread T1:
    #0 producer demo_relaxed.c:69 (drt_tsan:arm64+0x100000930)
  Location is global 'q' at 0x000104208000 (drt_tsan+0x100008100)
…
ThreadSanitizer: reported 2 warnings
```

반납 방향만 relaxed 인 빌드도 TSan 은 잡는다 — 방향이 반대인 보고다: `Write of size 8 … by thread T1: #0 producer demo_relaxed.c:69` / `Previous read of size 8 … by main thread: #0 main demo_relaxed.c:90`. 실측으로는 0 이었던 버그를 TSan 이 찾는다. 정답 순서 빌드는 TSan 보고 0. (TSan 은 release/acquire 로 만든 happens-before 를 추적하므로 이 레슨의 정답 코드는 오탐 없이 깨끗하다. 독립 `atomic_thread_fence` 만으로 순서를 만들면 Apple TSan 이 오탐한다 — MCPP-M13.)

<!-- code: demo_asm.c -->

**명령 차이** (`cc -std=c11 -O2 -S demo_asm.c -o -`, 핵심 줄만):

```text
_push_acqrel:                       _push_relaxed:
    ldr   w8, [x0]          ; tail      ldr  w9, [x0]
    add   x9, x0, #32                   ldr  w8, [x0, #32]   ; head: 그냥 ldr
    ldapr w9, [x9]          ; head acq  ...
    ...                                 str  w1, [x10, #64]  ; 칸
    str   w1, [x10, #64]    ; 칸        add  w9, w9, #1
    add   w8, w8, #1                    str  w9, [x0]        ; tail: 그냥 str
    stlr  w8, [x0]          ; tail rel
_pop_acqrel:
    ldr   w8, [x0, #32]     ; head (relaxed)
    ldapr w9, [x0]          ; tail acquire
    ldr   w10, [x10, #64]   ; 칸
    stlr  w10, [x11]        ; head release
```

- 이 맥의 clang 은 acquire load 를 **`ldapr`**(ARMv8.3 RCpc acquire)로, release store 를 `stlr` 로 낸다. relaxed 는 `ldr/str` 그대로. 배리어(`dmb`)는 없다.
- `ldar` 가 아니라 `ldapr` 인 이유: acquire 만 필요하면 RCpc 로 충분하다 (`stlr` 다음의 `ldapr` 은 서로 순서를 바꿀 수 있지만 SPSC 에는 store→load 순서가 필요 없다). seq_cst load 는 `ldar` 로 나온다 (`atomic_load` 기본값을 따로 `-S` 해 확인: `ldar w0, [x0]`). ARMv8.0 코어(`--target=aarch64-none-elf -mcpu=cortex-a53`)에서는 acquire 도 `ldar w8, [x8]` 이다 (실제 출력).
- relaxed 가 "공짜로 빠른" 것도 아니다: 이 코드에서 차이는 `ldapr/stlr` vs `ldr/str` 몇 개뿐이고, 처리량의 대부분은 아래의 캐시 라인 이동이 결정한다.

### Part B — 상대 인덱스 캐싱: 왜 정확하고 왜 빠른가

```c
static bool spsc_push_cached(spsc_t *q, const msg_t *m) {
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_relaxed);
    if (t - q->head_cache == q->mask + 1u) {                         /* 캐시로는 가득 */
        q->head_cache = atomic_load_explicit(&q->head, memory_order_acquire);
        if (t - q->head_cache == q->mask + 1u) return false;         /* 진짜 가득 */
    }
    q->buf[t & q->mask] = *m;
    atomic_store_explicit(&q->tail, t + 1u, memory_order_release);
    return true;
}
```

**정확성.** `head_cache` 는 생산자만 읽고 쓰는 일반 변수다(생산자 줄에 있다). 캐시 값은 예전에 acquire 로 읽은 head 다. 그때 이미 "그 head 이전의 칸 읽기 → 내 이후 동작" happens-before 가 생겼으므로, 캐시를 믿고 쓰는 칸(인덱스 < head_cache + cap)은 모두 안전하다. 더 최근 head 를 모를 뿐이다(보수적). 단위 테스트 `CHECK(q.head_cache == 0xFFFFFFFFu)` 는 "가득으로 보일 때 진짜로 다시 읽었는가" 를 본다.

<!-- code: demo_bench.c -->

**속도 측정** (`./db 200000000 3`, cap = 1024 × 8 B, 소비자는 값 0,1,2,… 검사, bad = 0, head/tail 은 둘 다 128 B 분리):

| 버전 | run 0 | run 1 | run 2 | 중앙값 | 메시지당 소비자 빈 큐 회전 |
|---|---|---|---|---|---|
| plain (매번 상대 인덱스 acquire) | 18.8 | 19.0 | 18.1 | **18.8 Mops/s** | ≈ 36 |
| cached | 257.2 | 228.1 | 244.3 | **244.3 Mops/s** | ≈ 0.5 |

load average 6.68 → 6.67 (측정 전/후, 다른 세션 벤치마크와 동시 실행). 2억 개 한 번에 plain ≈ 10.6 s, cached ≈ 0.8 s. 절대값은 믿지 말고 **배수(≈ 13×)** 를 보자.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">SPSC 처리량 (Mops/s, 중앙값, 실측) — 상대 인덱스 캐싱 전/후</text>
  <text x="10" y="58" fill="currentColor">cap 1024 plain</text>
  <rect x="150" y="42" width="38" height="24" class="f-mut" stroke="currentColor"/>
  <text x="196" y="59" fill="currentColor">18.8</text>
  <text x="10" y="94" fill="currentColor">cap 1024 cached</text>
  <rect x="150" y="78" width="489" height="24" class="f-acc" stroke="currentColor"/>
  <text x="647" y="95" fill="currentColor">244.3</text>
  <text x="10" y="140" fill="currentColor">cap 16 plain</text>
  <rect x="150" y="124" width="40" height="24" class="f-mut" stroke="currentColor"/>
  <text x="198" y="141" fill="currentColor">19.8</text>
  <text x="10" y="176" fill="currentColor">cap 16 cached</text>
  <rect x="150" y="160" width="94" height="24" class="f-ok" stroke="currentColor"/>
  <text x="252" y="177" fill="currentColor">47.0</text>
  <path d="M150 34 L150 196" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="10" y="220">cap 1024: N = 2e8 × 3회. cap 16: N = 5e7 × 3회. 막대 1 px = 0.5 Mops/s. load avg 6.4–7.2.</text>
    <text x="10" y="240">cap 16 은 링이 늘 가득/빔 경계라 캐시를 자주 다시 읽어야 해서 이득이 2.4× 로 줄어든다.</text>
  </g>
</svg>
```

**왜 13배인가 (캐시 라인 관점).** plain 에서는 메시지 하나마다:

1. 생산자가 `head` 를 읽는다 → head 줄은 소비자가 방금 썼으므로 소비자 코어에 Modified 상태 → 코어 간 전송 1회.
2. 생산자가 `tail` 을 쓴다 → 소비자가 방금 tail 을 읽어 공유(Shared) 중 → 무효화 1회.
3. 소비자가 `tail` 을 읽는다 → 전송 1회. 소비자가 `head` 를 쓴다 → 무효화 1회.

즉 **메시지마다 인덱스 줄 두 개가 코어 사이를 왕복** 한다 (데이터 줄 이동은 128 B 줄당 16 메시지라 상대적으로 적다). cached 에서는 소비자가 tail 을 "빈 것처럼 보일 때" 만 읽으므로 한 번 읽으면 그동안 쌓인 수십~수백 개를 한꺼번에 가져가고, 생산자는 링이 가득 찰 때(cap 1024 이면 드물게)만 head 를 읽는다. 인덱스 줄 이동이 메시지당 ~2회 → 수십~수백 메시지당 1회로 줄어든다. 표의 "빈 큐 회전" 이 36 → 0.5 로 떨어진 것이 그 흔적이다.

곁가지 관찰 (각 5e7 × 2회, 단일 측정이라 참고만): cap 16 은 plain 19.5–21.0, cached 44.9–47.7. cap 65536(512 KB, L1 을 넘음)은 plain 21.3 / 46.9 / 24.2, cached 61.6 / 46.9 / 86.1 로 **실행마다 크게 흔들렸다** — 코어 고정이 안 되는 macOS 에서 두 스레드가 같은 클러스터(L2 공유)에 놓이느냐에 따라 달라지는 것으로 추정한다 (미검증). 패딩 유무, 배치 게시, 슬롯 플래그, p99 지연은 LLAT-M01 에서 하나씩 분리해 잰다.

### Part C — UART RX: ISR → main, 그리고 Cortex-M 에서는

```c
static size_t uart_read(uart_rb_t *r, uint8_t *dst, size_t max) {
    uint32_t d = atomic_load_explicit(&r->rd, memory_order_relaxed);
    uint32_t w = atomic_load_explicit(&r->wr, memory_order_acquire);   /* 한 번 */
    uint32_t n = w - d;
    if ((size_t)n > max) n = (uint32_t)max;
    if (n == 0u) return 0;
    uint32_t off = d & (UART_RB_SIZE - 1u);
    uint32_t first = UART_RB_SIZE - off;
    if (first > n) first = n;
    memcpy(dst, &r->buf[off], first);
    memcpy(dst + first, &r->buf[0], n - first);
    atomic_store_explicit(&r->rd, d + n, memory_order_release);        /* 한 번 */
    return n;
}
```

- **overrun 정책.** ISR 은 기다릴 수 없고 오래된 바이트(main 소유 칸)를 지울 수도 없다 — rd 는 소비자만 쓰기 때문이다. 그래서 "새 바이트 버리기 + 카운터" 가 SPSC 에서 자연스러운 정책이다. 실제 UART 하드웨어도 RX 레지스터를 안 읽으면 ORE(overrun error) 플래그를 세우고 새 바이트를 잃는다(STM32 USART 의 ORE 예). 오래된 것을 버리고 싶으면 소비자 인덱스를 생산자가 건드려야 하므로 CAS 가 필요해진다 (SPSC 가 아니게 됨, DS-M10 의 덮어쓰기 링은 단일 스레드).
- **`overrun` 카운터** 는 ISR 만 쓰므로 `load + 1 → store`(relaxed)로 충분하다. RMW(`fetch_add`)가 필요 없다 — 쓰는 사람이 하나뿐이다. main 은 통계로만 읽는다.
- **스트레스 테스트의 트릭.** 호스트 "ISR" 스레드는 false 를 받으면 같은 바이트를 다시 시도한다(테스트용). 그래서 main 이 받은 스트림은 정확히 패턴 그대로여야 하고, `overrun == ISR 이 본 false 횟수` 가 결정적으로 성립한다. drop 개수 자체(실측 881만 회, 실행마다 다름)는 출력만 한다.

**Cortex-M4 에서 이 atomics 는 무엇이 되나** (`clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_asm.c -o -`, 실제 출력 일부):

```text
push_acqrel:                     push_relaxed / push_sigfence:
    ldr.w  r12, [r0]   ; tail        ldr  r3, [r0]
    ldr    r3, [r0, #32] ; head      ldr  r2, [r0, #32]
    dmb    sy          ; acquire     ...
    ...                              str  r1, [r2, #64]
    str    r1, [r2, #64] ; 칸        adds r1, r3, #1
    add.w  r1, r12, #1               str  r1, [r0]
    dmb    sy          ; release
    str    r1, [r0]    ; tail
```

- ARMv7-M 에는 `ldar/stlr` 가 없어서 acquire = `ldr; dmb sy`, release = `dmb sy; str` 이다 (`dmb ish` 가 아니라 `dmb sy` — ARMv7-M 의 DMB 는 사실상 SY 옵션만 정의한다, 문서 기준 미검증).
- **단일 코어 ISR ↔ main 이면 이 `dmb` 는 필요 없다.** ISR 과 main 은 같은 코어에서 돌고, 한 코어는 자기 Normal 메모리 접근을 프로그램 순서대로 관찰한다 (ARMv7-M ARM 의 메모리 순서 규칙 요지 — 절 번호는 미확인). 막아야 할 것은 **컴파일러 재배치** 뿐이다. 그래서 relaxed + `atomic_signal_fence(acquire/release)` 를 쓰면 `dmb` 없이 같은 순서가 보장된다. C11 7.17.4 의 signal fence 는 "같은 스레드에서 실행되는 시그널 핸들러" 와의 순서만 보장하는데, 이것이 정확히 ISR 모델이다. 위 오른쪽 열(`push_relaxed` 의 출력): `push_sigfence` 도 레지스터 배정만 다를 뿐 **같은 `ldr/str` 만 나오고 `dmb` 가 없다** — 펜스는 컴파일러에게만 작용한다 (추가 연습 3 은 진짜 시그널 핸들러로 검증).
- `dmb sy` 비용은 M4 에서 몇 사이클 수준이라 실제로는 그냥 acquire/release 를 쓰는 팀도 많다. 코드를 듀얼 코어(STM32H7 M7+M4, nRF5340)로 옮겨도 맞는 쪽은 acquire/release 다. signal fence 버전은 "이 큐는 같은 코어 전용" 이라는 주석과 함께 쓴다 (DS-A02 가 이 분석을 자세히 다룬다).
- **Cortex-M0/M0+**: `clang --target=thumbv6m-none-eabi -mcpu=cortex-m0plus` 로 같은 파일을 컴파일하면 `-Watomic-alignment` 경고("max lock-free size (0 bytes)")와 함께 relaxed load 조차 `bl __atomic_load_4` 라이브러리 호출이 된다 (실제 출력, 24개 `bl`). 정렬된 32비트 `ldr/str` 는 하드웨어적으로 원자적이지만 clang 의 이 타깃은 atomic 을 lock-free 로 취급하지 않는다. M0 펌웨어에서는 보통 `volatile` + 컴파일러 장벽으로 SPSC 를 짜거나(load/store 만 필요하므로 가능), 툴체인이 주는 구현을 쓴다. GCC arm-none-eabi 의 동작은 이 맥에 없어서 확인 못 했다. Rust 쪽은 RUST-M07 이 thumbv6m 빌드를 직접 보여 준다.
- **DMA 가 생산자라면** atomics 는 답이 아니다. DMA 엔진은 CPU 의 메모리 모델 밖에 있는 버스 마스터라서, (1) "tail" 은 DMA 의 남은 전송 수 레지스터(STM32 의 NDTR 등)에서 계산하고, (2) D-cache 가 있는 코어(Cortex-M7)에서는 읽기 전에 해당 줄을 **무효화**(CMSIS `SCB_InvalidateDCache_by_Addr`, 32 B 정렬 버퍼) 하거나 버퍼를 non-cacheable 영역(MPU)에 둬야 한다. C-M14 와 DS-A02(DMA 연속 영역용 bip buffer) 참고.

### 생산자가 둘이면 무엇이 깨지나 (→ DS-A03)

SPSC 의 정확성은 "tail 을 쓰는 스레드는 하나" 에 기대고 있다. 두 생산자가 같은 `spsc_push` 를 부르면, 둘 다 같은 tail 을 읽고 같은 칸에 쓰고 둘 다 `t+1` 을 store 한다 → 메시지 하나가 사라진다. "그럼 tail 을 `fetch_add` 로 예약하면?" — 예약은 겹치지 않지만, 소비자는 tail 만 보고 "칸이 다 써졌다" 고 믿는다. 늦게 예약한 생산자가 먼저 끝내 tail 이 2 가 되어도 앞 칸은 아직 쓰는 중일 수 있다 → 소비자가 쓰기 전 칸을 읽는다. 추가 연습 2 가 두 경우를 강제 스케줄로 결정적으로 재현한다. 해결은 "칸이 쓰였다" 를 **칸마다** 표시하는 것 — Vyukov 큐의 per-slot sequence (DS-A03). 펌웨어 함정: **우선순위가 다른 두 ISR 이 같은 링에 넣으면 그것이 바로 생산자 둘이다** (높은 우선순위 ISR 이 낮은 ISR 의 push 를 선점).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o solution && ./solution
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o so_asan && ./so_asan
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o so_tsan && ./so_tsan
```

실제 출력 (정상 빌드, 5회 연속 모두 `44/44 passed`, exit 0, 약 1.2 s):

```text
DS-A01 SPSC ring  (stress N = 10000000)
Part A — SPSC ring
  A: push/pop cap=1024         10000000 msgs  0.52 s  (19.3 Mops/s, 정보용)
Part B — cached index + edges
  B: cached/cached cap=1024    10000000 msgs  0.38 s  (26.4 Mops/s, 정보용)
  B: cached/plain cap=2 wrap   1000000 msgs  0.05 s  (18.4 Mops/s, 정보용)
Part C — UART RX ring
  C: uart stream 2500000 bytes, ISR drops 8817727 (timing-dependent, 정보용)
44/44 passed
```

(여기서 Mops/s 가 demo_bench 보다 낮은 것은 메시지마다 `mix()` 해시를 양쪽에서 계산하고 시간 제한 검사를 하기 때문이다.) ASan+UBSan: `44/44 passed`, 보고 없음 (N = 1e6, 0.3 s). TSan: `44/44 passed`, 보고 없음 (N = 1e6, 1.3 s).

## 7. 자주 하는 실수

1. **인덱스를 mask 해서 저장한다** (`tail = (t + 1) & mask`). → head == tail 이 빔/가득 둘 다라서 한 칸을 버리거나 count 변수를 따로 둬야 한다. count 를 두면 그것을 양쪽이 고쳐야 하므로 RMW(원자 증가/감소)가 필요해져 SPSC 의 장점이 사라진다. 고침: 자유 증가 인덱스, 칸 접근 때만 `& mask`.
2. **반납 방향을 relaxed 로 둔다** (생산자의 head load 가 relaxed). 실측에서 안 보이므로 오래 숨어 있다 (Part B 표: 6천만 개 중 0). 고침: `ld(head, acquire)` / `st(head, release)`. TSan 이 잡는다.
3. **`volatile` 인덱스로 짠다.** 컴파일러 재배치는 volatile 끼리만 막고, 일반 변수인 칸 데이터는 volatile 접근을 넘어 움직일 수 있다. CPU 재배치는 전혀 막지 못한다 (C-A02). 단일 코어 ISR 이라도 최소한 컴파일러 장벽(`atomic_signal_fence`)이 칸 접근과 인덱스 사이에 있어야 한다.
4. **head 와 tail 을 붙여 둔다** (`struct { atomic_uint head, tail; }`). 정확성은 맞지만 한 줄을 두 코어가 번갈아 쓰는 false sharing. 고침: `_Alignas(128)` (Apple M = 128 B, Cortex-A53/A72 = 64 B, Cortex-M7 D-cache = 32 B). 효과의 분리 측정은 LLAT-M01.
5. **캐시 버전에서 다시 읽기를 잊는다.** `if (tail_cache == h) return false;` 만 쓰면 캐시가 영원히 갱신되지 않아 큐가 영원히 비어 보인다 (하네스는 5 s 시간 제한으로 FAIL 시킨다). 고침: "빔으로 보이면 acquire 로 다시 읽고 다시 판단".
6. **캐시 변수를 엉뚱한 줄에 둔다** (`head_cache` 를 소비자 줄에). 생산자가 매 push 마다 소비자 줄을 쓰게 되어 캐싱의 이득이 사라지고 오히려 false sharing 이 생긴다. 고침: `head_cache` 는 생산자 줄(tail 옆), `tail_cache` 는 소비자 줄.
7. **모니터 스레드에서 `tail - head` 를 그대로 쓴다.** 두 load 사이에 상대가 진행하면 음수(거대값) 또는 cap 초과가 나온다. 고침: head 먼저(acquire), tail 나중, cap 으로 clamp (추가 연습 1).
8. **"ISR 하나 + main 하나" 라고 믿었는데 생산자가 둘** — 우선순위가 다른 두 ISR, 또는 ISR + 다른 태스크가 같은 링에 쓴다. 고침: 생산자마다 링을 따로 두거나(N개 SPSC 팬인), 그 경로만 짧게 인터럽트를 끄거나, MPMC(DS-A03).

## 8. 면접 Q&A

<details>
<summary>Q1. SPSC 링 버퍼에서 head/tail 에 각각 어떤 memory_order 가 필요한가?</summary>

자기 인덱스 load 는 relaxed(나만 씀). 상대 인덱스 load 는 acquire, 자기 인덱스 store 는 release. 생산자: `ld(tail, rlx)`, `ld(head, acq)`, 칸 쓰기, `st(tail, rel)`. 소비자: `ld(head, rlx)`, `ld(tail, acq)`, 칸 읽기, `st(head, rel)`. tail 의 rel/acq 는 "칸 쓰기 → 칸 읽기"(게시), head 의 rel/acq 는 "칸 읽기 → 다음 바퀴 덮어쓰기"(반납)를 happens-before 로 만든다. seq_cst 는 필요 없다 — store 다음 load 의 순서(Dekker 형태)를 요구하는 곳이 없다.

꼬리: "acq_rel 이나 seq_cst 로 하면?" → 정확하지만 더 비쌀 수 있다. arm64 에서 seq_cst load 는 `ldar`(acquire 는 `ldapr`), x86 에서 seq_cst store 는 `xchgl`(release store 는 일반 `movl`). 둘 다 이 맥에서 `-S`(`--target=x86_64-apple-macos`)로 확인했다.
</details>

<details>
<summary>Q2. x86 에서는 전부 relaxed 로 해도 잘 돌던데요?</summary>

x86(TSO)은 하드웨어가 store→store, load→load 순서를 지켜 주므로 relaxed 로 써도 CPU 재배치로는 거의 안 깨진다. 하지만 (1) 컴파일러는 relaxed 를 근거로 칸 쓰기와 tail store 순서를 바꿀 수 있고, (2) C11 기준 데이터 레이스라 UB 이며, (3) 같은 코드를 ARM(휴대폰, Apple Silicon, Cortex-A/R, M7 듀얼 코어)에 올리면 깨진다. 이 맥에서 전부 relaxed 는 2천만 개 중 1.3만–2.4만 개가 옛 메시지였다. x86 에서는 release/acquire 가 일반 `mov` 로 나오므로 올바르게 써도 비용이 0 이다 — relaxed 로 아낄 것이 없다.
</details>

<details>
<summary>Q3. 왜 자유 증가 인덱스인가? 한 칸 비우기, count 변수와 비교하면?</summary>

| 방식 | 용량 | 공유 쓰기 | 비고 |
|---|---|---|---|
| 한 칸 비우기 (`(t+1)&mask == h` 이면 full) | cap − 1 | 각자 자기 인덱스만 | 간단, 칸 하나 낭비 (큰 메시지면 아까움) |
| count 변수 | cap | count 를 양쪽이 씀 → RMW 필요 | SPSC 장점 상실 |
| 자유 증가 + `t - h` | cap | 각자 자기 인덱스만 | cap 은 2의 거듭제곱, ≤ 2^31 |

마지막이 Linux kfifo 와 같은 아이디어다(kfifo 도 in/out 을 계속 증가시키고 크기를 2의 거듭제곱으로 강제한다 — 커널 소스 기준, 세부는 버전마다 다를 수 있음). 2^31 제한은 `t - h` 가 0..cap 의 cap+1 가지를 구분해야 하기 때문이다 (DS-B04 연습 3).
</details>

<details>
<summary>Q4. 이 큐는 lock-free 인가 wait-free 인가?</summary>

`spsc_push`/`spsc_pop` 자체는 **wait-free** 다: 루프가 없고, 상대 스레드가 멈춰 있든 말든 상수 단계 안에 true/false 로 끝난다. lock-free 는 "시스템 전체로 누군가는 유한 단계 안에 진행한다", wait-free 는 "모든 스레드가 각자 유한 단계 안에 끝난다" 로 더 강하다. 다만 "가득이면 될 때까지 돌기" 래퍼를 씌우면 그 호출은 상대(소비자)가 진행해야 끝나므로 wait-free 가 아니다. 진행 보장의 정확한 정의와 "Vyukov 큐는 엄밀히 lock-free 가 아니다" 는 DS-A03.
</details>

<details>
<summary>Q5. 인덱스 캐싱은 왜 정확하고, 얼마나 빨라졌나?</summary>

상대 인덱스는 증가만 하므로 캐시 값 ≤ 실제 값 → 캐시로 계산한 여유는 실제 여유보다 작거나 같다(보수적). 캐시 값도 예전에 acquire 로 읽은 것이라 그 값까지의 happens-before 는 이미 성립했다. 가득/빔으로 보일 때만 다시 읽으면 된다. 이 맥에서 cap 1024, 2억 개: 18.8 → 244.3 Mops/s (중앙값, ≈ 13×, load avg 6.7). 이유는 메시지마다 인덱스 줄 두 개가 코어 사이를 왕복하던 것이 "가끔" 으로 줄기 때문이다. cap 16 처럼 링이 늘 경계에 있으면 2.4× 로 줄었다. rigtorp/SPSCQueue 가 쓰는 방식이고, 추가 튜닝은 LLAT-M01.
</details>

<details>
<summary>Q6. 생산자가 둘이 되면 무엇이 깨지고, 어떻게 고치나?</summary>

같은 tail 을 읽어 같은 칸에 쓰고 둘 다 t+1 을 써서 메시지 하나가 사라진다 (추가 연습 2a: 2개 보냈는데 1개 도착). tail 을 `fetch_add` 로 예약하면 칸은 겹치지 않지만, tail 이 "예약됨" 만 말하고 "쓰기 끝남" 은 말하지 않아서 소비자가 아직 안 쓴 칸을 읽는다 (2b: 옛 값 `0xDEAD` 를 읽음). 고침: (1) 칸마다 sequence 를 둬 "쓰기 끝남" 을 칸 단위로 게시 — Vyukov MPMC (DS-A03), (2) 생산자마다 SPSC 링을 따로 두고 소비자가 돌아가며 읽기 (가장 흔하고 빠름), (3) 생산자 쪽만 락(또는 ISR 이면 짧게 인터럽트 금지).
</details>

<details>
<summary>Q7. Cortex-M4 단일 코어에서 UART ISR → main 링이면 배리어가 필요한가?</summary>

clang 은 acquire/release 를 `ldr; dmb sy` / `dmb sy; str` 로 낸다(실제 `-S`). 단일 코어에서 ISR 과 main 은 같은 코어라 하드웨어 재배치는 문제가 아니고, 컴파일러 재배치만 막으면 된다 → relaxed + `atomic_signal_fence` 면 `dmb` 없이 충분하다(같은 `-S` 에서 relaxed 버전처럼 `ldr/str` 만, `dmb` 없음). 단, 듀얼 코어/멀티 마스터로 가면 진짜 acquire/release 가 필요하다. 그리고 생산자가 DMA 라면 atomics 가 아니라 캐시 무효화(M7)와 DMA 카운터 레지스터 문제다. M0+ 에서 clang 은 32비트 atomic load 도 `__atomic_load_4` 호출로 만든다는 것도 함정이다.
</details>

<details>
<summary>Q8. 이 큐를 어떻게 테스트하나? TSan 만 돌리면 충분한가?</summary>

세 겹이다. (1) 결정적 단위 테스트: full/empty 경계, 용량 1, 인덱스 랩(`start = 0xFFFFFFFE`), 캐시 재읽기. (2) 2스레드 스트레스: seq 번호로 순서·손실·중복을 한 번에, payload = hash(seq) 로 찢어짐/옛값을, 체크섬으로 전체를 확인하고, 용량을 작게(2, 4) 해서 경계에 자주 닿게 한다. (3) TSan: 실측으로 안 보이는 반납 방향 버그까지 잡는다. TSan 의 한계도 안다 — 실행된 경로만 보고, 독립 펜스는 오탐(MCPP-M13), 그리고 "정답 순서로 돌면 깨지지 않음" 은 증명이 아니다. 그래서 1. 문제의 happens-before 논증이 같이 있어야 한다. 더 나가면 모델 체커(CDSChecker, GenMC, Rust 쪽은 loom — RUST-M07).
</details>

## 9. 추가 연습

### 연습 1 — 모니터 스레드의 size

생산자·소비자가 아닌 제3 스레드(통계, 워치독)가 큐 깊이를 읽는다. `tail` 을 먼저 읽고 `head` 를 나중에 읽으면 무엇이 나올 수 있나? 반대 순서면? 항상 0..cap 을 주는 함수를 만들어라. 테스트는 두 load 사이에 hook 을 넣어 상대 진행을 강제한다.

<details>
<summary>답</summary>

tail 먼저(t), head 나중(h): 그 사이 생산자가 2개 넣고 소비자가 전부 빼면 `h = t + 2` → `t - h = 0xFFFFFFFE` (거대값). head 먼저(h), tail 나중(t): `t ≥ h` 는 항상 성립(tail 은 head 를 앞서므로)하지만 그 사이 소비+생산이 진행되면 `t - h` 가 cap 을 넘는다(테스트: 7, cap 4) → `min(t - h, cap)` 으로 clamp. 그리고 head load 는 **acquire** 여야 한다: 두 load 가 서로 다른 변수라 relaxed 면 CPU 가 tail load 를 head load 보다 먼저 수행할 수 있고, 그러면 "tail 먼저" 와 같은 문제가 돌아온다. 결과는 "읽는 동안 어느 순간의 개수" 가 아니라 근사값이다 — 통계용으로만.
</details>

### 연습 2 — 생산자 둘 (강제 스케줄)

push 를 `push_begin`(tail 읽고 칸 쓰기) / `push_commit`(tail+1 게시)로 쪼개고 P1.begin → P2.begin → P1.commit → P2.commit 순서로 실행하라. 몇 개가 도착하나? 다음으로 tail 을 `fetch_add` 로 예약하되 P1 이 칸을 쓰기 전에 소비자가 pop 하게 하라.

<details>
<summary>답</summary>

(a) 둘 다 t = 0 을 읽어 칸 0 에 A, B 를 차례로 쓰고 둘 다 tail = 1 을 쓴다 → 소비자는 1개(B)만 받는다. A 는 흔적도 없이 사라진다. (b) P1 이 0, P2 가 1 을 예약 → tail = 2. P2 가 칸 1 을 다 썼을 때 소비자가 pop 하면 tail(2) ≠ head(0) 이라 칸 0 을 읽는데 P1 은 아직 안 썼다 → 지난 바퀴 값 `0xDEAD`. 실제 스레드에서는 P1 이 예약 직후 선점되면(또는 ISR 에 걸리면) 똑같이 일어난다. "칸 단위 완료 표시" 가 필요하다는 것이 DS-A03 의 출발점이다. practice.c 의 CHECK 는 "깨진다" 를 확인한다(강제 스케줄이라 매번 같다).
</details>

### 연습 3 — 단일 코어 ISR: relaxed + signal fence

16 바이트 링을 relaxed load/store + `atomic_signal_fence` 로 만들고, `SIGUSR1` 핸들러를 "UART ISR" 로 써서(한 번에 3바이트 수신 시도, 가득이면 drop) main 루프 중간과 **pop 의 칸 읽기 직전** 에 `raise()` 로 인터럽트를 강제하라. 받은 바이트는 끊김 없는 순서여야 하고 `받은 수 + drop == 3 × 인터럽트 수` 여야 한다.

<details>
<summary>답</summary>

같은 스레드의 시그널 핸들러는 그 스레드를 선점하고 끝까지 돈 뒤 돌아온다 — 단일 코어 ISR 과 같은 구조다. 순서 문제는 컴파일러뿐이므로 `atomic_signal_fence` 로 칸 접근과 인덱스 접근 사이를 막는다 (C11 7.17.4.2). pop 의 칸 읽기 직전에 ISR 이 들어와도, head 를 아직 반납하지 않았으므로 ISR 은 그 칸을 "사용 중" 으로 보고 링이 가득이면 drop 한다. 인덱스는 lock-free atomic 이라 핸들러 안에서 접근해도 된다 (C11 7.14.1.1p5). 실측: 인터럽트 2286회, 받은 바이트 5419, drop 1439 (5419 + 1439 = 3 × 2286). 이 방식은 **같은 코어 전용** 이다 — 진짜 스레드 사이에서는 하드웨어 순서를 보장하지 않는다.
</details>

### 연습 4 — zero-copy reserve/commit

256 B 메시지를 복사 없이 칸 안에서 바로 쓰고 읽는 API: `z_write_slot`(빈 칸 주소 또는 NULL) / `z_commit_write`, `z_read_slot` / `z_commit_read`. 2스레드로 2백만 개(새니타이저 빌드 20만) 검증.

<details>
<summary>답</summary>

`push` 를 "칸 주소 얻기" 와 "게시" 로 쪼갠 것뿐이라 memory_order 는 같다: write_slot 은 acquire(head), commit_write 는 release(tail); read_slot 은 acquire(tail), commit_read 는 release(head). 규칙: write_slot 이 돌려준 칸은 commit 전까지 생산자 소유(소비자에게 안 보임 — 테스트 `z_read_slot == NULL`), read_slot 의 칸은 commit_read 전까지 소비자 소유. 큰 메시지(오디오 블록, 카메라 라인, 패킷)에서 memcpy 두 번을 없앤다. 주의: commit 없이 write_slot 을 두 번 부르면 같은 칸을 받는다 (예약 상태를 따로 저장하지 않으므로). DMA 친화적 연속 영역이 필요하면 DS-A02 bip buffer.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice
```

실제 출력 (exit 0, 0.2 s; ASan+UBSan / TSan 빌드도 `14/14 passed`, 보고 없음):

```text
  연습 3: 2286 interrupts, received 5419 bytes, ISR drops 1439
  연습 4: 2000000 big messages
14/14 passed
```

## 10. 핵심 정리

- 인덱스는 **mask 하지 않고 자유 증가**, 칸 접근만 `& mask`. `tail - head` 가 0 이면 빔, cap 이면 가득 → 빈 칸 낭비 없음. cap 은 2의 거듭제곱, ≤ 2^31.
- 각 인덱스는 한 스레드만 쓴다 → RMW 불필요. 자기 인덱스 load = relaxed, 상대 인덱스 load = **acquire**, 자기 인덱스 store = **release**.
- tail 의 rel/acq = 게시(쓰기 → 읽기), head 의 rel/acq = 반납(읽기 → 다음 바퀴 덮어쓰기). 둘 다 필요.
- 이 맥에서 전부 relaxed = 2e7 중 ~1–2만 개 옛 메시지 (실측). 반납 방향만 relaxed 는 실측 0 이지만 TSan 이 잡는다 → 실측 0 ≠ 안전.
- arm64: acquire = `ldapr`, release = `stlr`, relaxed = `ldr/str`, 배리어 없음. Cortex-M4: `dmb sy` 앞뒤. 단일 코어 ISR 이면 relaxed + `atomic_signal_fence` 로 `dmb` 없이 충분.
- 상대 인덱스 캐싱: 캐시 ≤ 실제 값이라 보수적으로 정확, 가득/빔일 때만 다시 읽기 → 18.8 → 244 Mops/s (≈ 13×, cap 1024).
- push/pop 은 wait-free. 생산자 둘이면 손실 또는 미완성 칸 읽기 → 칸별 sequence(DS-A03) 또는 생산자별 SPSC.
- DMA 생산자는 atomics 가 아니라 캐시 무효화 + DMA 카운터 문제. 우선순위 다른 두 ISR = 생산자 둘.

## 참고자료

- ISO/IEC 9899:2011 (C11) 5.1.2.4 (data race, happens-before, coherence), 6.2.5p9 (unsigned 모듈러 산술), 7.17.3 (memory_order), 7.17.4 (fences, `atomic_signal_fence`), 7.14.1.1p5 (시그널 핸들러에서 lock-free atomic). 초안 N1570: https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf
- Arm Architecture Reference Manual for A-profile (DDI 0487) — LDAPR(RCpc)/LDAR/STLR, 메모리 순서 모델. ARMv7-M Architecture Reference Manual (DDI 0403) — DMB, 메모리 순서. ARMv6-M (DDI 0419) — exclusive 명령 없음.
- Erik Rigtorp, SPSCQueue (cached index 기법): https://github.com/rigtorp/SPSCQueue
- Linux kernel `include/linux/kfifo.h`, `lib/kfifo.c` — 2의 거듭제곱 크기, 자유 증가 in/out (세부는 커널 버전에 따라 다름).
- Dmitry Vyukov, 1024cores "Producer-Consumer Queues" / "Bounded MPMC queue": https://www.1024cores.net/home/lock-free-algorithms/queues
- Paul E. McKenney, *Is Parallel Programming Hard, And, If So, What Can You Do About It?* — 메모리 순서와 캐시 라인 이동 비용.
- Martin Thompson 외, LMAX Disruptor 기술 문서 — 시퀀스 캐싱, false sharing 회피.
- CMSIS-Core `SCB_InvalidateDCache_by_Addr` / `SCB_CleanDCache_by_Addr` (Cortex-M7 캐시 유지보수), STM32 USART ORE 플래그 (레퍼런스 매뉴얼).
- 이 노트의 관련 레슨: C-A03 (release/acquire 1칸 채널), DS-B03/DS-B04 (full/empty, 자유 증가 카운터), C-M14 (ISR 공유 데이터), C-A02 (컴파일러 vs CPU 재배치), OS-M08 (메시지 패싱 리트머스), MCPP-M13 (fence, TSan 오탐), LLAT-B06 (atomic 비용), LLAT-M01 (SPSC 지연 튜닝), DS-A02 (ISR 링·bip buffer), DS-A03 (Vyukov MPMC), RUST-M07 (Rust SPSC split).
