---
id: LLAT-M01
time: 90분
updated: 2026-11-08
---
# LLAT-M01 SPSC 큐 지연 튜닝: 캐시된 인덱스·패딩·배치

## 학습 목표

- 맞게 동작하는 SPSC 링 큐(v0)에서 시작해 **128 B 패딩 → 상대 인덱스 캐시 → 배치 → 슬롯 시퀀스** 를 한 번에 하나씩 적용하고, 단계마다 처리량(Mops/s)과 왕복 지연(p50/p99/p99.9)을 따로 잰다.
- 각 단계가 왜 효과가 있는지(또는 없는지)를 **메시지 하나당 코히어런스 미스(다른 코어가 고친 라인을 읽거나 뺏어 오는 일)** 로 설명한다. 이 맥에서는 "패딩만 하면 오히려 느려진다" 가 실측으로 나온다. 그 이유를 말할 수 있다.
- `std::hardware_destructive_interference_size` 가 이 맥의 libc++ 에서 **256** 이고(실제 라인은 128 B), 그 값이 어디서 오는지, 128 과 256 중 무엇을 고를지 말한다.
- 배치가 처리량을 크게 올리지만 **지연을 해치는 경우** (생산자가 모으는 동안 기다림) 를 재 보고, `max_delay` 로 지연에 상한을 둔다.
- 41.67 ns 눈금 시계로 80 ns 짜리 왕복을 재는 방법과 그 한계, QoS(P/E 코어 유도) 가 숫자를 어떻게 바꾸는지 설명한다.

## 1. 문제

"SPSC 큐 짜 보세요" 다음에 나오는 질문은 거의 항상 **"이걸 더 빠르게 하려면?"** 이다. HFT 의 피드 핸들러 → 전략 스레드, 로봇의 센서 스레드 → 제어 루프, 펌웨어의 UART/DMA ISR → main 루프가 모두 이 큐 하나로 연결된다. 면접관이 듣고 싶은 답은 "인덱스 캐시하고 패딩합니다" 같은 기술 이름이 아니다. **"메시지 하나에 캐시 라인이 몇 번 코어 사이를 오가고, 각 기법이 그중 무엇을 없애는가"** 를 숫자로 말하는 것이다.

이미 아는 것(다시 가르치지 않는다): SPSC 링의 정확성 — free-running 인덱스, `tail - head` 로 개수 세기, 생산자 "내 tail relaxed · 상대 head acquire · 데이터 · tail release", 소비자는 그 거울상, happens-before 그림 (**DS-A01**, 이 레슨과 같은 시기에 작성 중). release/acquire 와 arm64 `stlr`/`ldapr` (C-A03), 원자 연산 하나의 비용 (LLAT-B06), 캐시 라인 하나로 하는 ping-pong 의 바닥값 약 80–90 ns 와 41.67 ns 시계·QoS (LLAT-M05), false sharing 측정 자체 (see:ARCH-M08). 여기서는 **같은 정확한 큐를 지연 관점에서 단계별로 고치고, 단계마다 효과를 따로 재는 일** 만 한다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">단계별 메모리 배치 (상자 하나 = 128 B 캐시 라인)</text>
<text x="10" y="58" fill="currentColor">v0</text>
<rect x="60" y="40" width="250" height="28" rx="4" class="f-warn" stroke="currentColor"/><text x="68" y="59" fill="currentColor" font-size="12">tail · head · 캐시 · 카운터 · buf[0..7]</text>
<rect x="310" y="40" width="150" height="28" rx="4" class="f-mut" stroke="currentColor"/><text x="318" y="59" fill="currentColor" font-size="12">buf[8..23] …</text>
<text x="470" y="59" fill="currentColor" font-size="12">P 와 C 가 한 라인에 둘 다 쓴다</text>
<text x="10" y="106" fill="currentColor">v1</text>
<rect x="60" y="88" width="150" height="28" rx="4" class="f-acc" stroke="currentColor"/><text x="68" y="107" fill="currentColor" font-size="12">tail (P 가 씀)</text>
<rect x="210" y="88" width="150" height="28" rx="4" class="f-ok" stroke="currentColor"/><text x="218" y="107" fill="currentColor" font-size="12">head (C 가 씀)</text>
<rect x="360" y="88" width="100" height="28" rx="4" class="f-mut" stroke="currentColor"/><text x="368" y="107" fill="currentColor" font-size="12">buf …</text>
<text x="470" y="107" fill="currentColor" font-size="12">라인은 나눴지만 매번 상대 라인을 읽는다</text>
<text x="10" y="154" fill="currentColor">v2</text>
<rect x="60" y="136" width="150" height="28" rx="4" class="f-acc" stroke="currentColor"/><text x="68" y="155" fill="currentColor" font-size="12">tail + head_cache</text>
<rect x="210" y="136" width="150" height="28" rx="4" class="f-ok" stroke="currentColor"/><text x="218" y="155" fill="currentColor" font-size="12">head + tail_cache</text>
<rect x="360" y="136" width="100" height="28" rx="4" class="f-mut" stroke="currentColor"/><text x="368" y="155" fill="currentColor" font-size="12">buf …</text>
<text x="470" y="155" fill="currentColor" font-size="12">꽉 참/빔으로 보일 때만 상대를 읽는다</text>
<text x="10" y="202" fill="currentColor">v3</text>
<text x="60" y="203" fill="currentColor" font-size="12">v2 와 같은 배치. push_n/pop_n 이 B 개를 쓰고 인덱스를 1번만 게시 → 인덱스 라인 이동 1/B</text>
<text x="10" y="250" fill="currentColor">v4</text>
<rect x="60" y="232" width="200" height="28" rx="4" class="f-mut" stroke="currentColor"/><text x="68" y="251" fill="currentColor" font-size="12">slot 0..7: {seq, val} × 8</text>
<rect x="260" y="232" width="60" height="28" rx="4" class="f-mut" stroke="currentColor"/><text x="268" y="251" fill="currentColor" font-size="12">…</text>
<rect x="320" y="232" width="70" height="28" rx="4" class="f-acc" stroke="currentColor"/><text x="328" y="251" fill="currentColor" font-size="12">tail_</text>
<rect x="390" y="232" width="70" height="28" rx="4" class="f-ok" stroke="currentColor"/><text x="398" y="251" fill="currentColor" font-size="12">head_</text>
<text x="470" y="251" fill="currentColor" font-size="12">공유 인덱스 없음. 위치는 각자 전용</text>
<rect x="60" y="284" width="14" height="14" class="f-acc" stroke="currentColor"/><text x="80" y="296" fill="currentColor" font-size="12">생산자만 씀</text>
<rect x="170" y="284" width="14" height="14" class="f-ok" stroke="currentColor"/><text x="190" y="296" fill="currentColor" font-size="12">소비자만 씀</text>
<rect x="280" y="284" width="14" height="14" class="f-warn" stroke="currentColor"/><text x="300" y="296" fill="currentColor" font-size="12">둘 다 씀 (라인이 매번 오간다)</text>
<rect x="490" y="284" width="14" height="14" class="f-mut" stroke="currentColor"/><text x="510" y="296" fill="currentColor" font-size="12">데이터 (P 가 쓰고 C 가 읽음)</text>
<text x="60" y="320" fill="currentColor" font-size="12">각 단계는 코드에서 별도 타입이다 → demo_bench 표를 누구나 그대로 다시 만들 수 있다.</text>
</svg>
```

체크 테스트(`starter.cpp` → `solution.cpp`)는 **매 실행 항상 성립하는 것만** 본다: FIFO 순서, 손실·중복 0, 꽉 참/빔 경계, 인덱스 wrap, 그리고 단일 스레드에서 센 "상대 인덱스 load 수 / 내 인덱스 store 수" 카운터(결정적). 처리량·지연은 `demo_bench.cpp` 의 출력이다.

### Part A — v0 기준선과 v1 패딩

- `SpscRing<T, N, Pad, Cache>` 하나로 v0–v3 를 만든다. `N` 은 2의 거듭제곱, 인덱스는 free-running `size_t`, 슬롯 낭비 없이 `N` 개가 다 들어간다.
- `Indices<Pad>` (주어짐): `Pad == 0` 이면 `alignas(0)` 이 무시되어 tail·head·캐시·카운터가 붙는다(v0). `Pad == 128` 이면 생산자 필드 / 소비자 필드 / 버퍼가 서로 다른 128 B 라인에 놓인다(v1).
- 구현: `try_push`, `try_pop`, 그리고 캐시 **없는** `prod_free` / `cons_avail` (매번 상대 인덱스를 `acquire` 로 읽고 `head_loads++` / `tail_loads++`).
- 테스트: 빈/꽉 참/순서/wrap(`SIZE_MAX - 2` 에서 시작), 레이아웃(라인이 서로 다른지), 카운터(캐시 없는 판은 push 9번에 head load 9번), 2스레드 200만 개 스트레스 (용량 1024 와 4), 큐 2개 왕복 10만 번.

### Part B — v2 상대 인덱스 캐시, v3 배치

- 캐시 **있는** `prod_free` / `cons_avail`: 생산자는 마지막으로 본 head 를 `head_cache` 에 두고, 그 값으로 계산한 빈 칸이 `need` 개보다 적을 때만 진짜 head 를 다시 읽는다 (rigtorp/SPSCQueue 방식). 소비자도 같다.
- `push_n(src, n)` / `pop_n(dst, max)`: 들어간/나온 개수를 돌려주고 (꽉 차면 일부만), **인덱스 store 는 호출당 1번**.
- 테스트(결정적 카운터): 빈 큐에 8개를 넣는 동안 head load 0번, 9번째(꽉 참)에서 1번. 첫 pop 에서 tail load 1번이면 나머지 7개는 load 없이. `push_n` 5 → 5개·store 1번, 다시 5 → 3개(부분), 다시 → 0개(store 없음). 무작위 크기 배치 스트레스.

### Part C — v4 슬롯 시퀀스, 지연 상한이 있는 배치

- `SpscSlot<T, N>`: 슬롯마다 `std::atomic<uint64_t> seq`. 슬롯의 seq 는 `pos`(빔, pos 에 쓸 차례) → `pos+1`(찼음) → `pos+N`(다음 바퀴에 빔). 위치 `tail_`/`head_` 는 각자 전용 보통 변수. 공유 인덱스가 아예 없다 (Vyukov MPMC 슬롯 방식의 SPSC 판 — MPMC 는 DS-A03).
- `BatchWriter<Q, T, B>`: 생산자 쪽에서 모은다. staged 가 `max_batch` 개가 되면 `add()` 가 바로 flush, 가장 오래된 staged 메시지가 `max_delay_ns` 이상 기다렸으면 `poll(now)` 가 flush. 큐가 차서 일부만 들어가면 남은 것은 순서대로 앞으로 당겨 둔다.
- 테스트: seq 값 자체(push 뒤 슬롯0 = 1, pop 뒤 = 8), wrap, 스트레스, 왕복. BatchWriter 는 가짜 시계로 flush 시점·부분 flush·거절을 정확히 확인하고, 진짜 스레드 + 가짜 시계 스트레스로 순서·손실을 확인.

## 2. Problem (English)

You own the queue between a market-data feed thread and a strategy thread (or a sensor thread and a control loop). It is a correct single-producer/single-consumer ring; your job is to make it fast **and to show, step by step, what each change buys**.

**Part A.** Implement `bool try_push(const T&)` / `bool try_pop(T&)` for `template <class T, size_t N, size_t Pad, bool Cache> class SpscRing` with free-running `size_t` indices (power-of-two `N`, all `N` slots usable). With `Cache == false`, the producer must load `head` (acquire) on every push and the consumer must load `tail` (acquire) on every pop; publish with release stores. Count every opposite-index load and every own-index store. `Pad == 0` packs both indices into one cache line (v0); `Pad == 128` separates producer fields, consumer fields and the buffer (v1).

**Part B.** With `Cache == true`, keep a private copy of the opposite index and reload it only when the copy says "full"/"empty" for the request (v2). Add `size_t push_n(const T*, size_t)` / `size_t pop_n(T*, size_t)` that move up to `n` items and publish the index once per call (v3).

**Part C.** Implement `SpscSlot<T, N>` with a per-slot `std::atomic<uint64_t> seq` and no shared indices (v4). Implement `BatchWriter` that stages items and flushes when `max_batch` items are staged or the oldest staged item has waited `max_delay_ns`.

Constraints: C++20, `-Wall -Wextra -Werror -O2 -pthread`, TSan clean. Follow-up: measure throughput (Mops/s) and round-trip latency (p50/p99/p99.9) for every step on this machine, explain each change in terms of cache-line transfers per message, and show when batching hurts latency.

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread starter.cpp -o starter && ./starter
```

처음 실행 결과 (실제 출력 일부, exit 1, 0.09초에 끝남 — 스텁은 `false`/`0` 을 돌려주고, 스레드 스트레스는 같은 Part 의 단위 테스트가 다 통과해야 돈다. 스트레스 안의 모든 스핀에는 5초 마감이 있어서 틀린 구현도 멈추지 않는다):

```text
FAIL starter.cpp:360  unit_fifo(*q0, 0) && unit_fifo(*q0, kWrap)
FAIL starter.cpp:361  unit_fifo(*q1, 0) && unit_fifo(*q1, kWrap)
FAIL starter.cpp:371  q1->pstats().head_loads == 9u && q1->pstats().tail_stores == 8u
...
SKIP 3 stress checks (Part A): unit checks failed
FAIL starter.cpp:393  unit_fifo(*q2, 0) && unit_fifo(*q2, kWrap)
FAIL starter.cpp:398  !q2->try_push(8) && q2->pstats().head_loads == 1u
...
SKIP 3 stress checks (Part C): unit checks failed
3/38 passed
```

통과하는 3개는 레이아웃 검사(주어진 `Indices` 가 이미 맞다)와 "빈 큐에 넣는 동안 head load 0번" (스텁이 아무것도 안 읽으니 우연히 맞음) 이다. Part C 의 BatchWriter 테스트는 Part B 의 `push_n` 을 쓰므로 B 를 먼저 끝낸다.

관찰용 파일 (하네스와 별개, 숫자는 CHECK 하지 않는다. 둘 다 `solution.cpp` 를 `LLAT_M01_NO_MAIN` 으로 include 해서 레슨과 같은 타입을 잰다):

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread demo_bench.cpp -o bench
./bench tput 5     # v0..v4 처리량, 5 run (약 11초)
./bench rtt 3      # 왕복 지연, 표본 = 64 왕복 (약 4초)
./bench rtt1 2     # 표본 = 왕복 1번 (시계 눈금 보기)
./bench paced 1000 # 1 µs 마다 메시지 → 즉시 vs 배치 vs 지연 상한 배치 (1초)
./bench qos        # v2 를 QoS ui/ui · ut/ut · bg/bg · ui/bg 로 (2초)
c++ -std=c++20 -O2 -c demo_asm.cpp -o a.o && xcrun llvm-objdump -d --no-show-raw-insn a.o   # hot path 명령
```

## 4. 힌트

<details>
<summary>힌트 1 — 내 인덱스는 relaxed, 상대 인덱스는 acquire</summary>

생산자의 tail 은 생산자만 쓴다. 자기가 쓴 값을 자기가 읽는 데는 순서가 필요 없으니 relaxed. 상대(head)를 읽을 때 acquire 인 이유는 "소비자가 그 슬롯을 다 읽었다" (소비자의 release store) 를 보고 나서 내가 그 슬롯을 덮어써야 하기 때문이다. 자세한 happens-before 는 DS-A01.
</details>

<details>
<summary>힌트 2 — 캐시가 낡아도 왜 안전한가</summary>

`head_cache` 는 언젠가 읽은 head 다. head 는 늘기만 하므로 `head_cache <= 진짜 head`. 그래서 `N - (t - head_cache)` 는 **진짜 빈 칸 수보다 작거나 같다** — 낡은 캐시는 "덜 비었다" 고 말할 뿐 "비었다" 고 거짓말하지 않는다. 모자랄 때만 다시 읽으면 된다. 다시 읽을 때는 여전히 acquire.
</details>

<details>
<summary>힌트 3 — 카운터를 어디서 세나</summary>

`prod_free` 안에서 head 를 실제로 load 할 때만 `ix_.ps.head_loads++`. 캐시 없는 판은 매번, 캐시 있는 판은 다시 읽을 때만. `tail_stores` 는 `try_push`/`push_n` 에서 tail 을 store 할 때 (`push_n` 은 k != 0 일 때 1번).
</details>

<details>
<summary>힌트 4 — v4 의 두 비교</summary>

생산자: `slot.seq == tail_` 이면 빈 칸 (이 위치에 쓸 차례). 소비자: `slot.seq == head_ + 1` 이면 찬 칸. 다 읽은 소비자는 `head_ + N` 을 써서 "다음 바퀴의 생산자 차례" 로 만든다. N ≥ 2 라서 세 값(pos, pos+1, pos+N)이 서로 다르다.
</details>

<details>
<summary>힌트 5 — BatchWriter 의 flush 와 남은 것</summary>

`k = q.push_n(stage, n)`. k 가 0 이면 아무것도 안 바꾼다. 아니면 `stage[k..n)` 을 앞으로 당기고 `n -= k`. `first_ns` 는 그대로 둔다 — 남은 것들은 이미 그만큼 기다렸다 (다음 `poll` 에서 바로 다시 시도한다).
</details>

## 5. 풀이

### Part A — 기준선 v0 와 패딩 v1: "패딩만 하면 느려진다"

정확성 쪽은 표 하나로 정리한다 (논증은 DS-A01):

| 위치 | 연산 | 순서 | 짝 / 이유 |
|---|---|---|---|
| 생산자 `tail.load` | 내 인덱스 | relaxed | 나만 쓴다 |
| 생산자 `head.load` | 상대 인덱스 | acquire | 소비자 `head.store(release)` — 슬롯을 다 읽은 뒤에 내가 덮어쓴다 |
| 생산자 `tail.store(t+1)` | 게시 | release | 소비자 `tail.load(acquire)` — 데이터를 쓴 뒤에 보인다 |
| 소비자 `head.store(h+1)` | 반납 | release | 생산자 `head.load(acquire)` |

`hardware_destructive_interference_size` 부터 확인했다 (`demo_bench` 첫 줄 + `clang -dM -E`):

| 대상 | `__GCC_DESTRUCTIVE_SIZE` (= `std::hardware_destructive_interference_size`) | constructive |
|---|---|---|
| `arm64-apple-macos` (이 맥) | **256** | 64 |
| `aarch64-linux-gnu` | 256 | 64 |
| `x86_64-apple-macos` | 64 | 64 |
| `thumbv7em-none-eabi` | 64 | 64 |

이 맥의 실제 라인은 `sysctl hw.cachelinesize` = **128**. libc++ 헤더(`__new/interference_size.h`)는 컴파일러 매크로를 그대로 쓴다. 즉 256 은 "Apple 칩의 값" 이 아니라 **clang 이 AArch64 전체에 주는 보수적인 값** 이다. 어떤 AArch64 칩은 라인이 크거나 인접 라인을 같이 가져오므로(추정) 넉넉하게 잡은 것이다. 그래서 코드는 `kLine = 128` 을 쓰고 `static_assert(hdis >= kLine)` 만 둔다. 256 으로 패딩한 판(v1w)도 따로 잰다. 주의할 점: 이 상수는 컴파일러 버전·`-mcpu` 에 따라 바뀔 수 있다. 그래서 라이브러리 경계를 넘는 구조체(ABI)에 쓰면 위험하다 (GCC 는 이 때문에 `-Winterference-size` 경고를 둔다고 알고 있다, 미확인).

**실측 (처리량, 생산자 1 · 소비자 1, 2000만 개, 5 run 중앙값, Mops/s)**. 표는 아래 "측정" 절에 모았다. 결론만 먼저 보면 v0 40–58, **v1 18–30**, v1w 20–32 다. 라인을 나눴더니 **더 느려졌다** (네 번 돌려 네 번 다). 왕복 지연은 반대로 v0 p50 230–237 ns → v1 104–173 ns 로 좋아졌다.

왜 그런가 — **false sharing 과 true sharing 은 다르다.** 캐시 없는 v0/v1 은 *모든* push 가 head 를, *모든* pop 이 tail 을 읽는다. 이건 진짜로 공유하는 데이터(true sharing)라서 라인을 나눠도 없어지지 않는다. 메시지 하나당 라인 이동을 세어 보면 (처리량 측정 때처럼 큐가 계속 흐르는 상태):

| 단계 | 생산자 한 번 | 소비자 한 번 | 메시지당 인덱스 라인 이동 (대략) | 실측 상대 인덱스 load / 메시지 |
|---|---|---|---|---|
| v0 | 라인 L 을 읽고 씀 | 라인 L 을 읽고 씀 | L 이 P↔C 로 왕복 ≈ 2 | head 1.0–1.3, tail 2.4–5.2 |
| v1 | H 읽기(미스) + T 쓰기(C 사본 무효화) | T 읽기(미스) + H 쓰기(P 사본 무효화) | T, H 가 각각 왕복 ≈ 4 | head 1.3–1.5, tail 2.5–4.6 |
| v2 | 캐시가 "꽉 참" 일 때만 H 읽기 | 캐시가 "빔" 일 때만 T 읽기 | ≈ 0.03 + 0.4 | head 0.02–0.12, tail 0.43–0.56 |
| v3 (B=32) | 32개당 T 쓰기 1번 | 32개당 H 쓰기 1번 | ≈ 1/32 | head 0.006–0.008, tail 0.05–0.12 |

v0 에서는 생산자의 "head 읽기 + tail 쓰기" 가 **한 라인에서** 끝난다. v1 에서는 그게 **두 라인** 이다. 패딩은 남이 쓰는 이웃 때문에 내 라인을 잃는 것(false sharing)을 없앤다. 그런데 v0/v1 의 진짜 비용은 "상대 인덱스를 매번 읽는 것" 이라서, 패딩만으로는 라인 수만 늘린다. 소비자가 빈 큐에서 tail 을 계속 읽는 것도 비용이다 (tail load/메시지 2–5 = 빈 큐 스핀). 그 읽기가 생산자에게서 T 라인을 계속 뺏는다. **패딩은 캐시(v2)와 함께 써야 효과가 있다.** 거꾸로도 그렇다: 캐시는 하되 패딩이 없는 v2u 는 처리량 95–118 (v2 114–160), 왕복 p50 154–165 ns (v2 86–96) 다. 캐시 변수가 상대가 쓰는 라인에 같이 있으면, 캐시를 읽는 것만으로도 미스가 난다.

(카운터 비용: 벤치마크도 레슨 타입을 그대로 쓰므로 `++` 카운터가 들어 있다. 각 카운터는 자기 쪽이 이미 가진 라인에서 하는 보통 덧셈이다. v0 에서는 공유 라인 안에 있다 — v0 이 원래 그런 배치라는 뜻이다.)

### Part B — v2 상대 인덱스 캐시, v3 배치

캐시의 핵심은 힌트 2 의 부등식 하나다: `head_cache <= head` 이므로 캐시로 계산한 빈 칸 수는 항상 **보수적** 이다. 틀릴 수 있는 방향은 "자리가 있는데 없다고 본다" 뿐이다. 그때만 진짜 head 를 다시 읽는다. 큐가 반쯤 차서 흐를 때 생산자는 N 개 가까이 head 를 안 읽는다. 실측 head load 가 메시지당 0.02 (= 50개에 1번) 다.

hot path 가 정말 짧아졌는지 asm 으로 확인했다 (`demo_asm.cpp`, `-O2`, `xcrun llvm-objdump -d`):

<!-- code: demo_asm.cpp -->

```text
v1_push (objdump 은 첫 함수를 ltmp0 으로 표시)      ; 주석은 내가 붙임
       0:  ldr   x8, [x0]              ; tail (relaxed, 내 라인)
       4:  add   x9, x0, #0x80
       8:  ldapr x9, [x9]              ; head (acquire) — 매번 상대 라인
       …
      2c:  str   x1, [x10, #0x100]     ; 데이터
      30:  add   x8, x8, #0x1
      34:  stlr  x8, [x0]              ; tail (release)
_v2_push
      50:  ldr   x8, [x0]              ; tail
      54:  ldr   x9, [x0, #0x8]        ; head_cache (내 라인)
      58:  sub   x9, x9, x8
      5c:  cmn   x9, #0x400            ; head_cache - t == -N ? (꽉 참으로 보이나)
      60:  b.ne  0x8c                  ; 아니면 바로 쓰기로 (적중 경로)
      64:  add   x9, x0, #0x80
      68:  ldapr x9, [x9]              ; head 재로드 — 꽉 참으로 보일 때만
       …
      94:  str   x1, [x9, #0x100]      ; 데이터
      98:  add   x8, x8, #0x1
      9c:  stlr  x8, [x0]              ; tail (release)
_v3_push_n (복사 루프 일부와 게시)
     1f0:  ldp   q0, q1, [x12, #-0x20]
     200:  stp   q0, q1, [x15]
     18c:  stlr  x9, [x0]              ; tail 게시 1번
_v4_push
     22c:  ldapr x8, [x10]             ; slot.seq (acquire)
     23c:  str   x1, [x10, #0x8]       ; slot.val
     244:  stlr  x11, [x10]            ; slot.seq = tail_+1 (release) — 같은 슬롯 라인
```

- acquire load 는 `ldar` 가 아니라 **`ldapr`** (RCpc acquire, FEAT_LRCPC) 로 나왔다. release store 는 `stlr`. 둘 다 이 칩에서 따로 펜스가 붙지 않는다 (C-A03/ARM-M05 와 같은 관찰).
- v2 의 적중 경로에는 **`ldapr` 이 아예 없다** — 내 라인의 보통 `ldr` 두 개와 비교뿐이다. `ldapr x9, [x9]` (head 재로드) 는 `b.ne` 로 건너뛴 쪽, 즉 꽉 참으로 보일 때만 실행된다. 상대 라인을 읽지 않는 것이 이득이다 — 명령 하나가 싸서가 아니다.
- v3 `push_n` 의 복사는, 개수가 크면(asm 상 14개 이상) clang 이 `ldp q0, q1` / `stp q0, q1` (16바이트 SIMD 레지스터 두 개씩) 루프로 펼쳤다. 작으면 스칼라 루프다. 어느 쪽이든 그 뒤 `stlr` 은 **한 번** 이다.

배치(v3)는 인덱스 store 와 그 라인의 이동을 1/B 로 줄인다. 데이터 라인은 원래 128 B / 8 B = 16 메시지마다 한 번 움직인다. 그래서 v2 에서 남은 비용은 대부분 인덱스 라인이다. 그 결과 처리량은 v2 114–160 → **v3 B=8 450–1000, B=32 1070–1510 Mops/s** 다 (약 8–12배). 이 정도면 메시지당 1 ns 이하로, 복사 비용이 보이기 시작하는 영역이다.

### Part C — v4 슬롯 시퀀스, 그리고 "배치가 지연을 해칠 때"

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">v4: 슬롯 하나의 seq 가 지나는 세 상태 (N = 용량)</text>
<rect x="20" y="50" width="170" height="50" rx="6" class="f-ok" stroke="currentColor"/>
<text x="32" y="72" fill="currentColor">seq = pos</text><text x="32" y="90" fill="currentColor" font-size="12">빔: pos 에 쓸 차례</text>
<rect x="285" y="50" width="170" height="50" rx="6" class="f-acc" stroke="currentColor"/>
<text x="297" y="72" fill="currentColor">seq = pos + 1</text><text x="297" y="90" fill="currentColor" font-size="12">찼음: 소비자 차례</text>
<rect x="550" y="50" width="170" height="50" rx="6" class="f-ok" stroke="currentColor"/>
<text x="562" y="72" fill="currentColor">seq = pos + N</text><text x="562" y="90" fill="currentColor" font-size="12">빔: 다음 바퀴 pos+N</text>
<path d="M190 75 L280 75" stroke="currentColor" fill="none"/><path d="M272 70 L282 75 L272 80" stroke="currentColor" fill="none"/>
<path d="M455 75 L545 75" stroke="currentColor" fill="none"/><path d="M537 70 L547 75 L537 80" stroke="currentColor" fill="none"/>
<text x="196" y="122" fill="currentColor" font-size="12">P: seq==tail_ (acquire)</text>
<text x="196" y="138" fill="currentColor" font-size="12">val 쓰기 → seq=pos+1 (release)</text>
<text x="460" y="122" fill="currentColor" font-size="12">C: seq==head_+1 (acquire)</text>
<text x="460" y="138" fill="currentColor" font-size="12">val 읽기 → seq=pos+N (release)</text>
<rect x="20" y="160" width="700" height="78" rx="6" class="f-mut" stroke="currentColor"/>
<text x="32" y="182" fill="currentColor" font-size="12">좋은 점: 소비자는 seq 와 val 이 같은 16 B 에 있어 미스 한 번에 둘 다 얻는다. 공유 인덱스 라인이 없다.</text>
<text x="32" y="202" fill="currentColor" font-size="12">나쁜 점: 소비자의 반납 store(pos+N)가 생산자가 다음에 쓸 슬롯의 라인에 떨어진다 (8 슬롯 = 1 라인).</text>
<text x="32" y="222" fill="currentColor" font-size="12">큐가 거의 비어 둘이 같은 라인 위에 있으면 메시지마다 라인이 오간다 → 실측 왕복 p50 124–143 ns (v2 86–96).</text>
</svg>
```

v4 는 "공유 인덱스를 없앤다" 는 아이디어다 (FastForward, PPoPP 2008 의 NULL 표시 슬롯과 같은 계열 — 연습 2). 실측 결과는 반반이다: 처리량은 v2 와 비슷하거나 조금 높고 (140–184 vs 114–160), **왕복 지연은 더 나쁘다** (p50 124–143 vs 86–96 ns, p99 317–1013 vs 112–213). 왕복 측정에서는 메시지가 하나뿐이라 생산자와 소비자가 항상 같은 슬롯 라인 위에 있다. 그림 아래 칸의 "나쁜 점" 이 메시지마다 일어난다고 본다 (추정 — 이 맥에서 PMC(성능 카운터)로 미스를 직접 세지는 못했다). FastForward 논문이 생산자와 소비자를 일부러 몇 라인 떨어뜨려 두는 "temporal slipping" 을 제안한 것도 같은 문제 때문이다 (논문 기억 기준, 세부 미확인). v4 의 진짜 가치는 지연이 아니라 **확장성** 이다. 같은 seq 규칙에 CAS 만 더하면 MPMC 가 된다 (DS-A03).

**배치가 지연을 해치는 경우.** 처리량 측정은 "생산자가 쉬지 않고 넣는" 상황이다. 실제 피드는 메시지가 **띄엄띄엄** 온다. 생산자가 B 개를 모으는 동안 첫 메시지는 기다린다. `./bench paced 1000` (1 µs 마다 메시지 1개, 20만 개, 한 방향 지연 = 소비자가 꺼낸 시각 − 메시지 생성 시각):

```svg
<svg viewBox="0 0 740 215" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">1 µs 마다 오는 메시지 (점) 와 게시 시점 (세로 막대)</text>
<path d="M90 50 L730 50 M90 100 L730 100 M90 150 L730 150" stroke="currentColor" stroke-dasharray="2 4" fill="none"/>
<text x="10" y="54" fill="currentColor" font-size="12">즉시</text>
<text x="10" y="104" fill="currentColor" font-size="12">16개 모음</text>
<text x="10" y="154" fill="currentColor" font-size="12">상한 2 µs</text>
<circle cx="100" cy="50" r="4" class="f-acc" stroke="currentColor"/><circle cx="140" cy="50" r="4" class="f-acc" stroke="currentColor"/><circle cx="180" cy="50" r="4" class="f-acc" stroke="currentColor"/><circle cx="220" cy="50" r="4" class="f-acc" stroke="currentColor"/>
<path d="M103 40 L103 60 M143 40 L143 60 M183 40 L183 60 M223 40 L223 60" stroke="currentColor" fill="none"/>
<text x="260" y="54" fill="currentColor" font-size="12">메시지마다 게시: p50 83 ns, 게시 1.0 / 메시지</text>
<circle cx="100" cy="100" r="4" class="f-acc" stroke="currentColor"/><circle cx="140" cy="100" r="4" class="f-acc" stroke="currentColor"/><circle cx="180" cy="100" r="4" class="f-acc" stroke="currentColor"/><circle cx="220" cy="100" r="4" class="f-acc" stroke="currentColor"/>
<text x="240" y="104" fill="currentColor" font-size="12">… 16번째까지</text>
<path d="M700 88 L700 112" stroke="currentColor" stroke-width="3" fill="none"/>
<text x="100" y="125" fill="currentColor" font-size="12">첫 메시지는 15 µs 기다림 → p50 8.0 µs, p99 15 µs, 게시 0.06/메시지</text>
<circle cx="100" cy="150" r="4" class="f-acc" stroke="currentColor"/><circle cx="140" cy="150" r="4" class="f-acc" stroke="currentColor"/><circle cx="180" cy="150" r="4" class="f-acc" stroke="currentColor"/>
<path d="M181 138 L181 162" stroke="currentColor" stroke-width="3" fill="none"/>
<circle cx="220" cy="150" r="4" class="f-acc" stroke="currentColor"/><circle cx="260" cy="150" r="4" class="f-acc" stroke="currentColor"/><circle cx="300" cy="150" r="4" class="f-acc" stroke="currentColor"/>
<path d="M301 138 L301 162" stroke="currentColor" stroke-width="3" fill="none"/>
<text x="320" y="154" fill="currentColor" font-size="12">2 µs 상한: p50 1.08 µs, 게시 0.33/메시지</text>
<text x="10" y="200" fill="currentColor" font-size="12">눈금: 점 사이 = 1 µs (그림은 축소). 숫자는 아래 실측 표.</text>
</svg>
```

| 생산자 정책 | p50 | p99 | p99.9 | 게시 / 메시지 |
|---|---|---|---|---|
| v2 즉시 `try_push` | 83 ns | 208–583 ns | 5.5–6.2 µs | 1.000 |
| 16개 모음, 시간 상한 없음 | **8.0 µs** | 15.1 µs | 18–22 µs | 0.062 |
| 16개 또는 2 µs | 1.08 µs | 2.1–6.1 µs | 6.4 µs (1 run 은 1.5 ms) | 0.33 |
| 64개 또는 2 µs | 1.08 µs | 2.1–3.4 µs | 6.5–9.7 µs | 0.33 |

(2 run, load average 12.1 / 10.4. load 4.8 에서 한 번 더 돌린 run 은 p50 이 83 ns / 7.2 µs / 1.17 µs / 1.08 µs 로 같았지만, p99 이상이 **모든 정책에서** 수 ms 까지 튀었다 — 즉시 게시도 p99.9 0.94 ms. 그 run 의 꼬리는 큐가 아니라 선점이다.) 메시지 간격(1 µs)이 큐 비용(약 0.1 µs)보다 훨씬 크면 생산자 쪽 배치는 **지연만 늘린다**. 16개 모음의 p50 8 µs 는 "평균 7.5개를 더 기다린다" 와 같다. 시간 상한을 두면 지연이 `max_delay` 안으로 묶인다. 2 µs 상한이면 메시지가 3개씩(대기 약 2 · 1 · 0 µs) 나가므로 게시 0.33 / 메시지, 중앙값 약 1 µs 다. ms 단위 꼬리(1.5 ms 등)는 스레드가 선점된 것으로 본다 — load average 와 상관없이 run 마다 나왔다 안 나왔다 했다. 정리하면:

- **생산자 쪽 배치(모으기)**: 메시지가 몰려 올 때(버스트, 큐가 밀릴 때)는 처리량 이득이 크다. 띄엄띄엄 올 때는 순수한 지연 손해다 → `max_delay` 로 상한을 둔다. 펌웨어의 UART DMA + IDLE line 인터럽트가 정확히 이 모양이다 (DMA 가 반/가득 찰 때 또는 줄이 쉬면 flush).
- **소비자 쪽 배치(있는 만큼)**: `pop_n` 은 기다리지 않고 지금 있는 것만 가져간다 → 지연 손해가 없다. 실측: 왕복에서 v3 (echo 가 `pop_n`) p50 85–100 ns 로 같은 세트의 v2 와 같다 (세트 1 85–89 vs 86–88, 세트 2 98–100 vs 96). 위 표의 즉시 판에서도 소비자 pop/메시지가 0.99 다 — 가끔 2개가 한 번에 왔다. LMAX Disruptor 의 "batching effect" 가 이 소비자 쪽 배치다 (설명 수준, 세부 미확인). 연습 3.

### 측정 — 처리량과 왕복 지연

<!-- code: demo_bench.cpp -->

기계: `Apple M6`, perflevel0/1/2 = 2/4/6 코어 (Super/Performance/Efficiency), `hw.cachelinesize` 128, `Apple clang version 21.0.0 (clang-2100.0.123.102)`. **이 레이어의 작성 에이전트 8개 + 다른 세션이 동시에 벤치마크를 돌렸다** (load average 4.8–13). 절대 숫자보다 **배수** 를 믿을 것. 코어 고정 불가 (macOS). 두 스레드 모두 `QOS_CLASS_USER_INTERACTIVE`. `E%` 는 각 스레드가 cpu 0–5 (LLAT-M05 가 E 코어로 추정한 번호) 에서 관측된 비율이다.

**처리량** (`./bench tput 5`, 큐 용량 1024 × 8 B, 2000만 개, 5 run 의 중앙값 [최소–최대] Mops/s):

| 단계 | run 1 (load 9.4→9.2) | run 2 (load 12.5→13.2) | run 3 (load 9.3→8.3) | run 4 (load 5.2→5.0) | v0 대비 (run 3 / 4) |
|---|---|---|---|---|---|
| v0 붙은 인덱스 | 39.6 [38.2–40.1] | 55.0 [50.0–63.2] | 39.9 [38.6–44.6] | 57.6 [51.4–61.2] | 1.0 / 1.0 |
| v1 128 B 패딩 | 27.2 [26.8–27.9] | 18.2 [13.3–42.4] | 30.5 [23.4–33.7] | 24.1 [18.1–25.2] | 0.76 / 0.42 |
| v1w 256 B 패딩 (hdis) | 30.1 [28.1–31.2] | 32.4 [29.3–42.0] | 29.8 [26.4–36.2] | 20.3 [17.3–25.7] | 0.75 / 0.35 |
| v2u 캐시, 패딩 없음 | — | 104.0 [94.7–105.8] | 118.2 [66.6–122.7] | 94.5 [83.5–104.6] | 3.0 / 1.6 |
| v2 캐시 + 패딩 | 160.4 [155.5–171.4] | 114.3 [94.0–127.5] | 126.1 [101.2–143.1] | 127.0 [96.0–158.4] | 3.2 / 2.2 |
| v3 배치 8 | 802.7 [642–931] | 784.7 [726–825] | 451.2 [382–668] | 999.1 [865–1087] | 11 / 17 |
| v3 배치 32 | 1327.6 [1099–1440] | 1071.0 [896–1389] | 1191.9 [952–1309] | 1505.3 [1272–1727] | 30 / 26 |
| v4 슬롯 seq | 183.8 [177.3–205.7] | 141.7 [137.7–147.9] | 140.3 [109.3–151.0] | 154.7 [144.4–157.4] | 3.5 / 2.7 |

**왕복 지연** (`./bench rtt 3`, 큐 2개로 echo, 표본 = 64 왕복의 평균, 20 000 표본, 앞 10% 버림, ns/왕복). 세트 1 = load 10.8→10.4, 세트 2 = load 5.0→4.8 (각 3 run):

| 단계 | p50 세트 1 | p99 세트 1 | p99.9 세트 1 | p50 세트 2 | p99 세트 2 |
|---|---|---|---|---|---|
| v0 | 238 / 231 / 230 | 460 / 766 / 477 | 898 / 1356 / 973 | 231 / 236 / 237 | 326 / 365 / 342 |
| v1 | 130 / 104 / 129 | 361 / 301 / 316 | 816 / 464 / 1135 | 173 / 173 / 173 | 324 / 308 / 244 |
| v1w | 133 / 138 / 135 | 983 / 341 / 201 | 13491 / 1361 / 348 | 163 / 163 / 162 | 226 / 234 / 278 |
| v2u | 155 / 154 / 154 | 227 / 215 / 281 | 346 / 387 / 582 | 164 / 164 / 165 | 307 / 308 / 270 |
| v2 | **87 / 88 / 86** | 213 / 115 / 204 | 423 / 290 / 361 | **96 / 96 / 96** | 143 / 112 / 135 |
| v3 (echo `pop_n`) | 87 / 85 / 89 | 96 / 105 / 181 | 359 / 316 / 345 | 98 / 100 / 100 | 144 / 164 / 126 |
| v4 | 124 / 126 / 126 | 317 / 325 / 341 | 591 / 641 / 668 | 142 / 143 / 126 | 428 / 1013 / 320 |

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">왼쪽: 처리량 (Mops/s, 로그 축, run 3)   오른쪽: 왕복 p50 (ns, 세트 1 중앙값)</text>
<path d="M130 40 L130 290 M130 290 L400 290" stroke="currentColor" fill="none"/>
<path d="M247 40 L247 290 M365 40 L365 290" stroke="currentColor" stroke-dasharray="2 4" fill="none"/>
<text x="122" y="308" fill="currentColor" font-size="12">10</text><text x="236" y="308" fill="currentColor" font-size="12">100</text><text x="350" y="308" fill="currentColor" font-size="12">1000</text>
<path d="M470 40 L470 290 L730 290" stroke="currentColor" fill="none"/>
<path d="M570 40 L570 290 M670 40 L670 290" stroke="currentColor" stroke-dasharray="2 4" fill="none"/>
<text x="466" y="308" fill="currentColor" font-size="12">0</text><text x="558" y="308" fill="currentColor" font-size="12">100</text><text x="658" y="308" fill="currentColor" font-size="12">200</text>
<text x="10" y="66" fill="currentColor" font-size="12">v0 붙음</text>
<rect x="130" y="52" width="70" height="20" class="f-warn" stroke="currentColor"/><text x="205" y="67" fill="currentColor" font-size="12">40</text>
<rect x="470" y="52" width="231" height="20" class="f-warn" stroke="currentColor"/><text x="705" y="67" fill="currentColor" font-size="12">231</text>
<text x="10" y="100" fill="currentColor" font-size="12">v1 패딩</text>
<rect x="130" y="86" width="57" height="20" class="f-warn" stroke="currentColor"/><text x="192" y="101" fill="currentColor" font-size="12">31</text>
<rect x="470" y="86" width="129" height="20" class="f-mut" stroke="currentColor"/><text x="604" y="101" fill="currentColor" font-size="12">129</text>
<text x="10" y="134" fill="currentColor" font-size="12">v1w 256</text>
<rect x="130" y="120" width="56" height="20" class="f-warn" stroke="currentColor"/><text x="191" y="135" fill="currentColor" font-size="12">30</text>
<rect x="470" y="120" width="135" height="20" class="f-mut" stroke="currentColor"/><text x="610" y="135" fill="currentColor" font-size="12">135</text>
<text x="10" y="168" fill="currentColor" font-size="12">v2u 캐시만</text>
<rect x="130" y="154" width="126" height="20" class="f-mut" stroke="currentColor"/><text x="261" y="169" fill="currentColor" font-size="12">118</text>
<rect x="470" y="154" width="154" height="20" class="f-mut" stroke="currentColor"/><text x="629" y="169" fill="currentColor" font-size="12">154</text>
<text x="10" y="202" fill="currentColor" font-size="12">v2 캐시+패딩</text>
<rect x="130" y="188" width="129" height="20" class="f-ok" stroke="currentColor"/><text x="264" y="203" fill="currentColor" font-size="12">126</text>
<rect x="470" y="188" width="87" height="20" class="f-ok" stroke="currentColor"/><text x="562" y="203" fill="currentColor" font-size="12">87</text>
<text x="10" y="236" fill="currentColor" font-size="12">v3 배치 32</text>
<rect x="130" y="222" width="244" height="20" class="f-acc" stroke="currentColor"/><text x="379" y="237" fill="currentColor" font-size="12">1192</text>
<rect x="470" y="222" width="87" height="20" class="f-ok" stroke="currentColor"/><text x="562" y="237" fill="currentColor" font-size="12">87</text>
<text x="10" y="270" fill="currentColor" font-size="12">v4 슬롯 seq</text>
<rect x="130" y="256" width="135" height="20" class="f-mut" stroke="currentColor"/><text x="270" y="271" fill="currentColor" font-size="12">140</text>
<rect x="470" y="256" width="126" height="20" class="f-mut" stroke="currentColor"/><text x="601" y="271" fill="currentColor" font-size="12">126</text>
<text x="470" y="326" fill="currentColor" font-size="12">LLAT-M05 의 순수 라인 ping-pong 바닥 ≈ 80–90 ns</text>
</svg>
```

읽는 법:

1. **v2 의 왕복 86–96 ns 는 하드웨어 바닥이다.** LLAT-M05 에서 캐시 라인 하나로 ping-pong 한 왕복이 같은 P 코어 쪽에서 80–90 ns 였다. 메시지 하나가 "생산자 쪽에서 쓴 라인을 소비자가 가져가는" 한 방향 핸드오프 2번이므로, v2 는 큐 비용을 거의 다 없앤 것이다. 이보다 더 줄이려면 큐가 아니라 **배치(코어 배치)** 를 바꿔야 한다 (같은 클러스터, 고정 — 리눅스에서 `pthread_setaffinity_np`, LLAT-M05).
2. **왕복과 처리량은 다른 문제다.** 왕복에서는 메시지가 하나뿐이라 배치할 게 없다 (v3 = v2). 처리량에서는 배치(B=32)가 v0 의 26–30배다. "지연 튜닝" 을 물으면 어느 쪽을 묻는지 먼저 확인한다.
3. **v1w(256) vs v1(128)**: 처리량은 run 1·2 에서 v1w 가 조금 나았고, run 3 에서는 같았고, run 4 에서는 나빴다. 왕복은 세트 1 에서 v1w 가 조금 나빴고 세트 2 에서 조금 나았다 (163 vs 173). 인접 라인 프리페치 효과가 있는지는 이 소음 속에서 **결론을 못 냈다**. 실무에서는 Apple/Linux arm64 에서 256 을 쓰면 메모리를 조금 더 써서 안전 쪽을 택하는 것이다.
4. 꼬리(p99.9)는 run 마다 수백 ns – 13 µs 로 흔들린다 (v1w run 1 = E% 38/40, E 코어로 밀려남). load 10 의 기계에서 p99.9 는 큐가 아니라 **스케줄러** 를 잰다.

**시계 해상도.** `mach_absolute_time` 1 tick = 41.67 ns (timebase 125/3). 왕복이 2 tick 정도라 왕복마다 재면 값이 41.67 의 배수로만 나온다. `./bench rtt1 2` (표본 = 왕복 1번): v2 p50 **125.0** / p99 166.7–333.3 / p99.9 208.3–416.7 ns (3 run) — 모두 41.67 의 정수배(3, 4, 5, 8, 10 tick) 다. 64 왕복 평균(86–96 ns)보다 큰 이유는 눈금 반올림과 시계 읽기 자체의 비용(표본마다 두 번)이 왕복 1번에 다 얹히기 때문이다. 그래서 표의 숫자는 64 왕복 평균이다. 대가: 표본이 평균이라 **꼬리가 눌린다** — p99 는 "가장 나쁜 64 묶음의 평균" 이고, 진짜 단일 메시지 꼬리는 더 길다 (LLAT-M05 와 같은 한계, Cortex-M 이면 `DWT->CYCCNT` 로 사이클 단위가 된다).

**QoS (P/E 코어 유도)** (`./bench qos`, v2, 2 run, load 10.4–12.8):

| QoS (ping/echo · 생산/소비) | 왕복 p50 (ns) | 왕복 p99 (ns) | 왕복 E% | 처리량 Mops/s | 처리량 E% (P/C) | head load / 메시지 |
|---|---|---|---|---|---|---|
| ui / ui | 91–95 | 162–201 | 0/0 | 166–181 | 0/0 | 0.01 |
| ut / ut | 88–96 | 171–946 | 0/0–14 | 141–172 | 0/23–92 | 1.3–1.4 |
| bg / bg | 198–316 | 491–4100 | 0–2/100 | 220–222 | 1–9/100 | 0.3–0.6 |
| ui / bg | 194–264 | 659–2927 | 0–8/100 | 136–233 | 0/100 | 0.10–0.13 |

- `BACKGROUND` 스레드는 **항상 E 코어** 에 갔다 (E% 100). 하지만 같은 BACKGROUND 를 요청한 다른 쪽 스레드는 P 코어에 남은 경우가 많았다 (E% 0–9). QoS 는 고정이 아니라 힌트다 (LLAT-M05 와 같은 결론).
- 한쪽이 E 코어면 왕복이 2–3배 (200–300 ns): 라인이 P 클러스터와 E 클러스터 사이를 오간다.
- **처리량은 소비자가 E 코어일 때 오히려 높았다** (bg/bg 220 vs ui/ui 166–181). 소비자가 느리면 큐가 거의 **꽉 찬** 상태로 돈다. 그러면 소비자는 생산자가 한참 전에 다 쓴 라인만 읽는다 (tail load 0.03–0.05 / 메시지 — 빈 큐 스핀이 사라짐). 소비자가 빠르면 큐가 거의 **빈** 상태로 돈다. 그러면 소비자가 생산자가 막 쓰는 라인을 바로 따라가며 뺏는다 (tail load 0.30–0.44). 처리량에는 "큐가 반쯤 차서 둘이 다른 라인 위에 있는 것" 이 좋다 (temporal slipping 과 같은 이야기). ui/bg 처리량이 run 마다 136 / 233 으로 크게 다른 것은 설명하지 못했다.

### 펌웨어에서

- **단일 코어 Cortex-M (ISR → main, see:DS-A01, C-M14)**: 코히어런스 미스가 없다. 패딩은 의미가 없다 (M4 는 D-캐시도 없다). 캐시된 인덱스는 `volatile`/atomic load 하나를 아끼는 정도다. 오히려 **배치** 가 중요하다 — 인터럽트 한 번의 진입/복귀 비용(수십 사이클)이 바이트 하나의 처리보다 크므로 UART 는 DMA + 반/가득 인터럽트 + IDLE line(또는 receiver timeout) 인터럽트로 모아서 게시한다. 그게 BatchWriter 의 `max_batch` + `max_delay` 다.
- **D-캐시가 있는 M7 + DMA**: 라인은 32 B. 여기서 라인 정렬이 필요한 이유는 코히어런스가 아니라 **캐시 유지보수(clean/invalidate) 단위** 다. DMA 버퍼가 다른 변수와 한 라인을 나누면 invalidate 가 그 변수를 날린다 (C-A05 의 DMA 버퍼 정렬).
- **듀얼 코어 MCU**: RP2040 의 SIO 코어 간 FIFO 는 하드웨어로 만든 SPSC 큐다 (32비트 폭, 방향마다 하나 — 깊이는 데이터시트 확인 필요). STM32H7 (M7+M4) 처럼 캐시가 있는 코어 하나와 없는 코어 하나가 SRAM 으로 큐를 나누면, 공유 영역을 MPU 로 non-cacheable 로 두거나 clean/invalidate 를 해야 한다. 이 레슨의 "라인 이동" 비용이 그곳에서는 버스 접근 비용이 된다.

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread solution.cpp -o solution && ./solution     # 38/38 passed (7번 실행 모두, 0.7초)
c++ -std=c++20 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.cpp -o so_asan && ./so_asan   # 38/38, 보고 없음
c++ -std=c++20 -g -O1 -fsanitize=thread -pthread solution.cpp -o so_tsan && ./so_tsan       # 38/38, 보고 없음 (1.0초)
```

새니타이저 빌드에서는 `__has_feature(thread_sanitizer)` / `__has_feature(address_sanitizer)` 로 `LLAT_M01_SAN` 이 켜져 스트레스가 200만 → 10만 개, 왕복이 10만 → 1만 번으로 줄어든다. 데이터 슬롯(`buf_[]`, `Slot::val`)은 보통 변수다. 그래도 TSan 이 조용한 이유는 모든 접근이 tail/head/seq 의 release→acquire 로 순서가 잡혀 있기 때문이다 (독립 fence 를 안 써서 TSan 오탐 문제도 없다, MCPP-M13).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| "false sharing 이니까 패딩부터" | v0/v1 의 비용은 매번 상대 인덱스를 읽는 **true sharing** 이다. 패딩만 하면 라인 수가 늘어 처리량이 v0 의 0.35–0.8배 (실측 4 run) | 캐시(v2)와 패딩을 같이. 캐시만(v2u)도 패딩 없이는 왕복 154–165 ns |
| 캐시 변수를 상대 인덱스 옆에 둠 | `head_cache` 는 생산자가 매번 읽는데, 소비자가 쓰는 head 라인에 있으면 읽을 때마다 미스 | 캐시는 **자기** 인덱스 라인에 (`tail` + `head_cache`) |
| 캐시를 다시 읽을 때 relaxed | 재로드는 "소비자가 슬롯을 다 읽었다" 를 받는 지점 → acquire 가 빠지면 덮어쓰기가 읽기보다 먼저 보일 수 있다 | `head_cache = head.load(acquire)` |
| `push_n` 이 원소마다 tail store | 배치의 이득(인덱스 라인 이동 1/B)이 사라지고, 소비자가 반쯤 쓴 배치를 보게 된다 (그 자체는 맞지만 느림) | 다 쓰고 `tail.store(t + k, release)` 1번 |
| 생산자 쪽 배치에 시간 상한 없음 | 메시지가 띄엄띄엄 오면 첫 메시지가 B−1 개를 기다린다 (실측 p50 8 µs) | `max_delay` flush (`poll(now)`), 또는 소비자 쪽 배치만 |
| `hardware_destructive_interference_size` 를 공용 헤더 구조체에 | 값이 컴파일러/옵션에 따라 다를 수 있다 → 두 바이너리의 레이아웃이 달라진다 (ABI) | 상수 `128`(또는 256) 을 명시하고 `static_assert(hdis >= kLine)` |
| `alignas(128)` 객체를 C++14 식 `new` 로 | C++17 이전에는 정렬 new 가 없어 128 정렬이 보장 안 됨 | C++17+ `new`/`make_unique` (aligned new), 또는 정적 할당 |
| 41.67 ns 시계로 왕복 1번씩 재고 p50 비교 | 값이 tick 배수로 양자화된다 (실측 125.0 = 3 tick) | N 왕복 묶어 나누기 (꼬리는 눌린다는 점을 명시) |

## 8. 면접 Q&A

<details>
<summary>Q1. lock-free SPSC 큐를 더 빠르게 하라. 무엇을 어떤 순서로 하고 각각 얼마나 얻나?</summary>

① **상대 인덱스 캐시** — 꽉 참/빔으로 보일 때만 상대 인덱스를 읽는다. 메시지당 상대 라인 load 가 1–5 → 0.02–0.5 로 준다. ② **패딩** — 생산자 필드 / 소비자 필드 / 버퍼를 다른 라인에. ①과 같이 해야 효과가 있다. 실측: 둘 다 해서 처리량 v0 의 2–4배, 왕복 230–237 → 86–96 ns (하드웨어 바닥 ≈ LLAT-M05 ping-pong). ③ **배치** — 인덱스 게시를 B 개에 1번. 처리량 다시 8–12배 (1.1–1.5 G msg/s), 왕복은 그대로. ④ 그 밖에: 2의 거듭제곱 용량(나머지 연산 → `and`), 원소를 라인 크기에 맞추기, 코어 배치(같은 클러스터·고정), busy-poll. 꼬리: "어느 하나만 한다면?" → 캐시. 이 맥에서 패딩만 하면 오히려 느려졌다.
</details>

<details>
<summary>Q2. 왜 패딩만 했더니 느려졌나? false sharing 아니었나?</summary>

false sharing 은 "서로 다른 변수인데 한 라인이라 뺏기는 것" 이다. 캐시 없는 SPSC 에서 생산자는 매번 head 를 **진짜로 필요해서** 읽는다 — true sharing 이다. 라인을 나눠도 head 를 읽는 미스는 그대로다. 오히려 생산자 한 번이 "head 라인 읽기 + tail 라인 쓰기" 로 두 라인을 건드리게 된다 (v0 에서는 한 라인). 실측 처리량 v0 40–58 → v1 18–30. 그래서 순서는 "먼저 공유 자체를 줄이고(캐시) → 남은 false sharing 을 패딩으로" 다. 꼬리: "그럼 v0 은 왕복에서 왜 느렸나?" → 왕복에서는 메시지가 하나뿐이라 양쪽이 같은 라인을 동시에 스핀/쓰기한다. 라인 하나를 둘이 계속 뺏는 비용이 그대로 지연이 된다 (230–237 vs 104–173 ns).
</details>

<details>
<summary>Q3. std::hardware_destructive_interference_size 를 쓰면 되지 않나? 이 맥에서 값은?</summary>

이 맥의 libc++ 에서 **256** 이다. 실제 라인은 128 (`hw.cachelinesize`). libc++ 는 clang 의 `__GCC_DESTRUCTIVE_SIZE` 를 그대로 쓰고, clang 은 이 값을 AArch64 전체(macOS·Linux)에 256 으로 준다 (x86-64 와 thumbv7em 은 64). 즉 "가장 나쁜 AArch64 칩에 대비한 값" 이다. 쓰면 안전하지만 메모리를 더 쓴다 (인덱스마다 256 B). 실측에서 256(v1w) vs 128(v1) 차이는 소음 안에 있었다. 문제는 ABI 다: 이 상수는 컴파일러 버전·대상 CPU 옵션에 따라 바뀔 수 있다. 그래서 헤더로 공개되는 구조체에 쓰면 두 번역 단위의 레이아웃이 달라질 수 있다 (GCC 의 `-Winterference-size` 경고가 이걸 겨냥한다고 알고 있다). 실무: 플랫폼 상수(128 또는 256)를 명시하고 `static_assert` 로 hdis 와 비교한다.
</details>

<details>
<summary>Q4. 캐시된 head 가 낡았는데 그걸로 판단해도 정확한가? 재로드는 relaxed 로 해도 되나?</summary>

정확하다. head 는 단조 증가라서 `head_cache <= head` 이고, 캐시로 계산한 빈 칸은 진짜보다 작거나 같다. 낡은 값은 "자리가 없다" 쪽으로만 틀린다 (그때 재로드). 재로드는 **acquire** 여야 한다. 그 load 로 "소비자가 그 슬롯을 다 읽었다 (release store)" 를 받아야, 내가 그 슬롯에 새로 쓰는 것이 소비자의 읽기 뒤로 순서가 잡힌다. relaxed 면 이론상 소비자가 아직 읽는 슬롯을 덮어쓸 수 있다 (data race = UB). 캐시 적중 경로에는 acquire 가 없다 — 이미 예전 acquire 로 받은 정보를 재사용하는 것이다 (asm: 적중 경로에 `ldapr` 없음).
</details>

<details>
<summary>Q5. 배치가 지연을 해치는 건 언제인가? 어떻게 막나?</summary>

**생산자가 모을 때**, 메시지가 띄엄띄엄 오면 첫 메시지가 나머지가 찰 때까지 기다린다. 실측: 1 µs 간격, 16개 모으기 → p50 8.0 µs, p99 15 µs (즉시 게시 83 ns). 대책 세 가지. ① 시간 상한 `max_delay` (2 µs → p50 1.08 µs). 하드웨어에서는 UART IDLE line / receiver timeout, NIC 의 interrupt coalescing 타이머가 같은 것이다. ② **소비자 쪽 배치만** 쓴다 — `pop_n` 으로 "지금 있는 것" 만 가져가면 기다림이 없다 (왕복 v3 ≈ v2, 85–100 ns). ③ 큐가 밀릴 때만 모으는 적응형 (밀리면 배치 커짐). 꼬리: "그럼 처리량 26–30배는 어디서?" → 쉬지 않고 넣는(포화) 상황에서 인덱스 라인 이동이 1/B 가 되어서다. 포화가 아니면 이 이득은 없다.
</details>

<details>
<summary>Q6. 인덱스 대신 슬롯마다 플래그/시퀀스를 두면(v4) 뭐가 좋고 뭐가 나쁜가?</summary>

좋은 점: 공유 인덱스 라인이 없다. 소비자는 seq 와 값이 같은 16 B 에 있어 미스 한 번에 둘 다 얻는다. 같은 seq 규칙에 CAS 를 더하면 MPMC 가 된다 (Vyukov, DS-A03). 처리량은 v2 와 비슷하거나 조금 높았다 (140–184 vs 114–160). 나쁜 점: 소비자의 반납 store 가 생산자가 다음에 쓸 슬롯과 같은 라인에 떨어진다. 큐가 거의 비면 메시지마다 그 라인이 오간다 → 실측 왕복 p50 124–143 vs 86–96 ns (원인은 추정, 미스를 직접 세지 못함). 슬롯이 커져서(값 + 8 B) 메모리도 더 든다. 꼬리: "SPSC 인데 bool 이면 충분하지 않나?" → 충분하다 (FastForward, 연습 2). 주체가 둘뿐이라 "누구 차례" 1비트면 된다. seq 는 바퀴 수까지 담아서 MPMC 에서 "예약만 된 슬롯" 과 "다 쓴 슬롯" 을 구분할 때 필요하다.
</details>

<details>
<summary>Q7. 80 ns 짜리 왕복을 어떻게 쟀나? 그 p99 를 믿어도 되나?</summary>

`mach_absolute_time` 은 24 MHz (1 tick = 41.67 ns) 라서 왕복 1번을 재면 0/41.7/83.3/125… 만 나온다 (실측 rtt1: p50 125.0 = 3 tick). 그래서 64 왕복을 한 표본으로 재고 64로 나눴다. 표본 2만 개, 앞 10% 는 워밍업으로 버렸다. 한계: 표본이 평균이라 p99/p99.9 는 "나쁜 묶음의 평균" 이다. 진짜 단일 메시지 꼬리는 더 길고, load 10 인 기계에서는 꼬리가 스케줄러(선점·E 코어 이동)를 잰다. 믿을 수 있는 것은 같은 조건에서 단계끼리의 **배수** 다 (왕복 p50 v0 : v1 : v2 ≈ 2.4–2.7 : 1.5–1.8 : 1). 리눅스라면 `rdtsc`/`cntvct_el0` + 코어 고정 + `isolcpus` 로 더 깨끗하게 잰다.
</details>

<details>
<summary>Q8. P/E 코어가 섞인 맥에서 QoS 를 바꾸면? 왜 E 코어 소비자가 처리량이 더 높았나?</summary>

`BACKGROUND` 스레드는 E 코어로 갔고(관측 100%), 한쪽이 E 면 왕복이 2–3배(200–300 ns)가 됐다 — 라인이 클러스터 사이를 오간다. 처리량은 반대로 bg/bg 220 vs ui/ui 166–181 이었다. 느린 소비자 때문에 큐가 꽉 찬 상태로 돌면 둘이 서로 다른 라인 위에서 일한다 (소비자의 빈 큐 스핀 = tail load 가 0.30–0.44 → 0.03–0.05 / 메시지). 소비자가 빠르면 생산자 바로 뒤를 따라가며 방금 쓴 라인을 뺏는다. 결론: SPSC 처리량은 "양쪽 속도의 균형" 과 큐가 차 있는 정도에 크게 좌우된다. 벤치마크 숫자를 말할 때는 코어 배치와 큐 상태를 같이 말해야 한다. QoS 는 힌트일 뿐이다 (같은 BACKGROUND 요청의 다른 스레드는 P 에 남음).
</details>

<details>
<summary>Q9. 이 튜닝 중 펌웨어(단일 코어 Cortex-M)에서도 의미 있는 것은?</summary>

코히어런스가 없으니 패딩과 캐시는 거의 의미가 없다 (load 한 번 = SRAM 1–2 사이클). 의미 있는 것은 **배치와 지연 상한** 이다. 인터럽트 한 번의 오버헤드가 바이트 하나보다 크므로 DMA 로 모으고, 반/가득 + IDLE line 으로 flush 한다 (= `max_batch` + `max_delay`). 듀얼 코어(STM32H7, nRF5340 등)나 캐시 있는 코어 + DMA 에서는 라인 정렬이 다시 중요해진다. 단 이유가 coherence 가 아니라 캐시 유지보수 단위다 (C-A05). 단일 코어에서 ISR → main SPSC 가 asm 으로 어떻게 되는지(`dmb` 유무)는 DS-A01.
</details>

## 9. 추가 연습

### 연습 1 — zero-copy: `alloc()/commit()`, `front()/pop()`

64 B `Tick` 을 큐에 넣을 때 복사하지 않고, 생산자는 슬롯 포인터를 받아 그 자리에 쓰고(`alloc` → 쓰기 → `commit`), 소비자는 슬롯을 그 자리에서 읽고 반납(`front` → 읽기 → `pop`)하게 하라. 용량 4 에서 "꽉 참은 재로드 1번", 용량 16 에서 체크섬 스트레스 100만 개.

<details>
<summary>답 보기</summary>

v2 의 캐시 규칙을 그대로 쓰되 게시를 분리한다. `alloc()` 은 `t - head_cache == N` 일 때만 head 를 acquire 로 다시 읽고, 그래도 꽉 차면 nullptr. `commit()` 은 `tail.store(t+1, release)`. `front()` / `pop()` 은 거울상. 주의: `front()` 가 준 포인터는 `pop()` 전까지만 유효하다 — `pop()` 의 release 뒤에는 생산자가 그 슬롯을 덮어쓴다. rigtorp/SPSCQueue 의 `front()`/`pop()` 이 이 계약이다. 이득은 원소가 클 때 복사 1번을 줄이는 것이다. 단 생산자가 슬롯에 쓰는 동안 그 라인은 이미 "공유 버퍼" 라서, 쓰는 시간이 길면 소비자의 다음 미스가 늦어지지는 않는다 (게시 전이라 소비자는 보지 않는다).
</details>

### 연습 2 — FastForward: 슬롯마다 `full` 플래그

v4 의 64비트 seq 대신 `std::atomic<uint32_t> full` (0 = 생산자 차례, 1 = 소비자 차례) 로 SPSC 큐를 만들어라. 단위 테스트 + 100만 개 스트레스.

<details>
<summary>답 보기</summary>

생산자: `full.load(acquire) != 0` 이면 꽉 참. 아니면 `val` 쓰고 `full.store(1, release)`. 소비자: `full.load(acquire) == 0` 이면 빔. 아니면 `val` 읽고 `full.store(0, release)`. 위치는 각자 전용. 주체가 둘뿐이라 1비트로 "누구 차례" 가 결정된다 — 바퀴 수는 필요 없다. 원 논문(Giacomoni 외, PPoPP 2008)은 플래그 대신 **값 자체가 NULL 인지** 로 판단해서 슬롯이 포인터 하나다. MPMC 로 가면 "생산자 둘이 같은 슬롯을 예약" 을 구분해야 해서 seq 가 필요하다 (DS-A03).
</details>

### 연습 3 — `consume_all(fn)`: 지연을 늘리지 않는 배치

소비자가 지금 있는 원소를 전부 `fn` 으로 처리하되 tail load 는 최대 1번, head store 는 최대 1번만 하게 하라. 5개 → load 1 · store 1, 빈 큐 → load 1 · store 0. 스트레스에서 head store 수 < 메시지 수 (배치가 실제로 일어났는지).

<details>
<summary>답 보기</summary>

`h = head(relaxed)`. `tail_cache == h` 일 때만 `tail_cache = tail.load(acquire)`. `k = tail_cache - h` 개를 처리하고 `k != 0` 이면 `head.store(h+k, release)`. 기다리지 않으므로 메시지가 하나뿐이면 하나만 처리한다 → 지연 손해가 없다 (Q5 ②). 처리 중에 새로 들어온 것은 다음 호출이 가져간다. 주의: `fn` 이 오래 걸리면 그동안 슬롯을 반납하지 않으므로 생산자가 꽉 참을 더 빨리 만난다 — 큰 배치는 상한(`max`)을 두는 게 좋다 (`pop_n` 의 `max`).
</details>

### 연습 4 — BatchWriter 의 지연 상한을 숫자로 증명

메시지가 100 틱마다 생기고 생산자 루프가 10 틱마다 `poll` 한다. `max_batch 8, max_delay 350` 이면 모든 메시지의 "게시 시각 − 생성 시각" 의 최댓값은? flush 횟수는? `max_batch 4` 로 바꾸면? 가짜 시계 시뮬레이션으로 정확히 확인하라.

<details>
<summary>답 보기</summary>

`max_delay 350`: 0, 100, 200, 300 에 생긴 4개가 350 에서 flush 된다 (첫 메시지 350 틱 대기). 이후 400 부터 같은 패턴 → 4개씩 250번, 최대 대기 **350**. 일반적으로 상한은 `max_delay + poll 간격 − 1` (도착이 poll 격자와 어긋날 때)이다. `max_batch 4` 이면 4번째 add(300)에서 바로 flush → 최대 대기 **300**, flush 250번. 즉 지연 상한 = `min(max_delay(+poll 오차), (max_batch − 1) × 도착 간격)`. 이 식이 바로 "배치 크기와 타임아웃을 어떻게 고르나" 라는 면접 질문의 답이다. 도착 간격이 짧을 때(버스트)는 `max_batch` 가, 길 때는 `max_delay` 가 지연을 정한다.
</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread practice.cpp -o practice && ./practice    # 13/13 passed
c++ -std=c++20 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread practice.cpp -o pr_asan && ./pr_asan   # 13/13
c++ -std=c++20 -g -O1 -fsanitize=thread -pthread practice.cpp -o pr_tsan && ./pr_tsan       # 13/13, 보고 없음
```

## 10. 핵심 정리

- SPSC 큐의 비용은 명령이 아니라 **메시지당 코어 사이를 오가는 캐시 라인 수** 다. 각 기법은 그중 하나를 없앤다.
- **상대 인덱스 캐시**가 1순위: `head_cache <= head` 라서 낡아도 보수적이다. 재로드는 acquire. 적중 경로에는 `ldapr` 이 없다.
- **패딩은 캐시와 같이.** 캐시 없이 라인만 나누면 true sharing 이 그대로라 이 맥에서 처리량 0.35–0.8배. 캐시만 하고 패딩을 안 하면 캐시 변수 자체가 미스.
- 이 맥: 라인 128 B, libc++ `hardware_destructive_interference_size` = **256** (clang 의 AArch64 공통값). ABI 경계에는 상수를 명시.
- **배치**: 인덱스 게시를 1/B 로 → 포화 처리량 v0 의 26–30배 (1.1–1.5 G msg/s). 띄엄띄엄 오면 생산자 배치는 지연만 늘린다 → `max_delay` 상한, 또는 소비자 쪽 배치(`pop_n`)만.
- v2 왕복 86–96 ns ≈ 라인 ping-pong 바닥 (LLAT-M05). 더 줄이려면 큐가 아니라 코어 배치를 바꾼다. v4(슬롯 seq)는 왕복 124–143 ns 로 더 나빴다. 장점은 MPMC 확장.
- 41.67 ns 시계 → N 왕복 묶어 재기 (꼬리 눌림 명시). load 높은 기계에서는 **배수만**. QoS 는 힌트이고, E 코어 소비자는 왕복 2–3배.

## 참고자료

- Erik Rigtorp, *SPSCQueue* (github.com/rigtorp/SPSCQueue) — `readIdxCache_`/`writeIdxCache_` 캐시 인덱스, 라인 패딩, `front()`/`pop()` API (README·소스 기준, 버전 미확인). 같은 저자의 SPSC 최적화 글 (rigtorp.se).
- J. Giacomoni, T. Moseley, M. Vachharajani, "FastForward for Efficient Pipeline Parallelism: A Cache-Optimized Concurrent Lock-Free Queue", PPoPP 2008 — 슬롯 플래그(NULL) 방식, temporal slipping.
- P. P. C. Lee, T. Bu, G. Chandranmenon, "A Lock-Free, Cache-Efficient Multi-Core Synchronization Mechanism for Line-Rate Network Traffic Monitoring" (MCRingBuffer), IPDPS 2010 — 제어 변수 배치 갱신, 라인 분리 (연도·세부는 기억 기준).
- L. Lamport, "Specifying Concurrent Program Modules", ACM TOPLAS 1983 — 단일 생산자/소비자 링 버퍼의 원형.
- Dmitry Vyukov, 1024cores "Bounded MPMC queue" — 슬롯 시퀀스 방식 (v4 의 원형, DS-A03).
- C++ 표준 [hardware.interference] (`<new>`), libc++ `__new/interference_size.h` (MacOSX26.4.sdk), [dcl.align] (`alignas(0)` 무효과).
- Arm ARM DDI 0487 — LDAPR (FEAT_LRCPC), STLR. LMAX Disruptor 기술 문서 — batching effect (소비자 쪽 배치).
- 관련 레슨: DS-A01 (SPSC 정확성·memory_order·ISR 판, 병렬 작성), DS-A03 (Vyukov MPMC), DS-M10 (가변 길이 링), LLAT-B06 (원자 연산 비용), LLAT-M05 (ping-pong 바닥·시계·QoS), LLAT-A06 (SPSC 위 NanoLog 로거), C-A03, C-A05, C-M14, MCPP-M13, OS-M07, see:ARCH-M08 (false sharing).
