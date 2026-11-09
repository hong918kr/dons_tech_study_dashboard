---
id: LLAT-M02
time: 90분
updated: 2026-11-08
---
# LLAT-M02 Seqlock: 단일 writer 스냅샷 공유

## 학습 목표

- C++ 메모리 모델에 맞는 seqlock(Boehm 2012 형태)을 직접 짠다: `atomic` 시퀀스, 데이터는 relaxed atomic 워드로 복사, writer "홀수 → release fence → 데이터 → release 짝수", reader "acquire → 복사 → acquire fence → relaxed 재로드".
- fence 두 개가 **왜 그 자리에 있어야 하는지** happens-before 로 설명하고, 빼면 이 맥(Apple M6)에서 실제로 찢어진 스냅샷이 나온다는 걸 숫자로 보여 준다 (reader fence 없음 0.035%, writer fence 없음 0.30%).
- 깨지는 경우 네 가지(검사 없는 복사, 홀수 검사 누락, writer 2개, 보통 `memcpy`)를 강제 스케줄로 재현하고, TSan 이 그중 무엇을 보고 무엇을 못 보는지 안다.
- reader 지연을 `pthread_rwlock_t`, `std::mutex` 와 writer 속도별로 재서, "reader 가 공유 라인에 쓰지 않는다 → writer 를 막지 않는다 → reader 끼리 확장된다" 와 그 대가(writer 가 바쁘면 reader 가 굶는다)를 숫자로 말한다.
- Linux `seqcount_t` / `seqlock_t` / latch, 그리고 펌웨어의 "ISR 이 쓰고 main 이 읽는 64비트 스냅샷" 에 같은 구조를 적용한다.

## 1. 문제

시장 데이터 피드 핸들러는 종목마다 최우선 호가(top of book: bid/ask 가격·수량·시퀀스·타임스탬프)를 초당 수백만 번 고치고, 전략 스레드 여러 개가 그걸 계속 읽는다. writer 는 **절대 기다리면 안 되고**(피드를 놓친다), reader 는 **한 순간의 온전한** 스냅샷이 필요하다(bid 는 새 값, ask 는 옛 값이면 crossed book 으로 오판). 펌웨어도 같다 — 엔코더/타이머 ISR 이 64비트 위치·타임스탬프를 쓰고 main 루프가 읽을 때, 32비트 MCU 에서는 64비트 읽기가 두 번의 load 라 찢어진다. 인터럽트를 끄지 않고 이걸 푸는 표준 해법이 seqlock 이고, 저지연 면접(HFT)과 커널/펌웨어 면접 모두에서 "메모리 순서를 정확히 아느냐" 를 확인하는 단골 문제다.

이미 아는 것 (다시 가르치지 않는다): 단일 코어 ISR↔main 에서 `volatile` 버전 카운터로 하는 seqlock 과 "reader 가 ISR 이면 안 된다" (C-M14), release/acquire 의미와 arm64 명령 (C-A03), fence–fence / fence–op 동기화 규칙 (MCPP-M13), 원자 연산과 경합 비용 (LLAT-B06), relaxed 플래그 메시지 패싱이 이 칩에서 실제로 깨진다는 것 (OS-M08). 여기서는 **멀티코어 + C++ 메모리 모델** 에서 seqlock 을 정확하게 만들고 재는 일만 한다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">seqlock: writer 는 seq 를 홀수로 → 데이터 → 짝수로. reader 는 seq 를 앞뒤로 읽어 비교만 한다</text>
<text x="10" y="58" fill="currentColor">writer</text>
<rect x="80" y="40" width="140" height="28" rx="4" class="f-ok" stroke="currentColor"/><text x="92" y="59" fill="currentColor">seq = 4 (안정)</text>
<rect x="220" y="40" width="220" height="28" rx="4" class="f-warn" stroke="currentColor"/><text x="232" y="59" fill="currentColor">seq = 5 (홀수) 데이터 쓰는 중</text>
<rect x="440" y="40" width="290" height="28" rx="4" class="f-ok" stroke="currentColor"/><text x="452" y="59" fill="currentColor">seq = 6 (안정)</text>
<text x="10" y="113" fill="currentColor">reader A</text>
<rect x="90" y="96" width="120" height="26" rx="4" class="f-ok" stroke="currentColor"/><text x="98" y="114" fill="currentColor" font-size="12">s1=4 복사 s2=4</text>
<text x="218" y="114" fill="currentColor" font-size="12">→ 성공 (writer 전 값)</text>
<text x="10" y="163" fill="currentColor">reader B</text>
<rect x="150" y="146" width="150" height="26" rx="4" class="f-warn" stroke="currentColor"/><text x="158" y="164" fill="currentColor" font-size="12">s1=4 복사… s2=5</text>
<text x="306" y="164" fill="currentColor" font-size="12">≠ → 버리고 재시도</text>
<rect x="460" y="146" width="130" height="26" rx="4" class="f-ok" stroke="currentColor"/><text x="468" y="164" fill="currentColor" font-size="12">s1=6 복사 s2=6</text>
<text x="10" y="213" fill="currentColor">reader C</text>
<rect x="300" y="196" width="80" height="26" rx="4" class="f-warn" stroke="currentColor"/><text x="308" y="214" fill="currentColor" font-size="12">s1=5 홀수</text>
<text x="386" y="214" fill="currentColor" font-size="12">→ 바로 재시도</text>
<rect x="480" y="196" width="130" height="26" rx="4" class="f-ok" stroke="currentColor"/><text x="488" y="214" fill="currentColor" font-size="12">s1=6 복사 s2=6</text>
<path d="M220 72 L220 230 M440 72 L440 230" stroke="currentColor" stroke-dasharray="4 3" fill="none"/>
<rect x="10" y="240" width="720" height="52" rx="6" class="f-mut" stroke="currentColor"/>
<text x="22" y="261" fill="currentColor">reader 는 공유 메모리에 아무것도 쓰지 않는다 → writer 를 못 막고, reader 끼리도 라인을 뺏지 않는다.</text>
<text x="22" y="282" fill="currentColor">대가: writer 가 쉬지 않고 쓰면 reader 는 계속 재시도한다 (굶주림). writer 가 둘이면 seq 자체가 망가진다.</text>
</svg>
```

체크 테스트(`starter.cpp` → `solution.cpp`)는 **매 실행 항상 성립하는 것만** 본다: 강제 스케줄(hook)로 고정한 인터리빙의 결과, 스트레스에서 찢어진 스냅샷 0 개, 정확한 버전 수. 지연·재시도 분포·깨진 변형의 찢어짐 비율은 데모 출력이다.

### Part A — Boehm 형태 seqlock

- `SeqLock<T>` (주어짐): `alignas(128) std::atomic<uint64_t> seq` + `std::atomic<uint64_t> w[sizeof(T)/8]`. `T` 는 trivially copyable, 크기는 8 의 배수. `T` ↔ 워드 배열 변환은 `std::bit_cast`.
- `sl_store(sl, v, hook)` — writer (단 하나). `sl_try_load(sl, out, hook)` — reader 1회 시도, 실패하면 `out` 을 건드리지 않는다. `sl_load(sl, out, hook)` — 성공할 때까지, 반환 = 재시도 횟수.
- hook: writer 는 데이터 워드 k 를 **쓴 직후**, reader 는 워드 k 를 **읽은 직후** `hook(k)` 를 부른다. 테스트는 hook 안에서 다른 쪽 동작을 실행해 "복사 도중 쓰기", "쓰기 도중 읽기" 를 결정적으로 만든다.

### Part B — 무엇이 깨지나, 어떻게 막나

- 제공된 깨진 reader 두 개 — `naive_load`(seq 를 안 봄), `load_no_odd_check`(s1 == s2 만 봄) — 가 강제 스케줄에서 **찢어진 스냅샷을 받아들이는 것** 을 테스트가 보여 준다. writer 두 개가 단일 writer 용 `sl_store` 를 겹쳐 부르면 seq 가 **뒤로 가고**, 찢어진 데이터가 "안정(짝수)" 으로 보인다.
- `sl_try_store_mw` — writer 가 여럿일 때: seq 를 짝수→홀수로 CAS 해서 쓰기 권한을 얻는다 (실패하면 아무것도 안 쓰고 false).
- `sl_load_bounded(sl, out, max_tries, hook)` — 실시간 reader: 상한까지만 시도, 다 실패하면 -1 (지난 스냅샷을 계속 쓰는 판단은 호출자 몫).

### Part C — 시장 데이터 북

종목 4 개, 종목마다 `SeqLock<Quote>` (64 B `Quote`, 마지막 워드 = 체크섬). 피드 writer 는 **자기 전용 사본(master)** 만 고치고 게시한다. 전략 reader 는 복사한 스냅샷의 체크섬과 종목별 `seq_no` 단조성을 검사하고 재시도 히스토그램을 쌓는다.

- `feed_on_update(book, feed, update)` — side 0 bid / 1 ask / 2 trade 를 master 에 반영, `seq_no+1`, `ts_ns`, `flags = side`, 체크섬 재계산, `sl_store`.
- `strat_poll(book, inst, reader, out)` — `sl_load` → `reads++`, `hist[bucket(재시도)]++` → 체크섬 틀림 `torn++` / `seq_no` 역행 `regress++` / 새 값이면 `fresh++` 후 true.
- 스트레스: 피드 1 + 전략 3, 업데이트 60만 개 → torn 0, regress 0, 마지막 스냅샷 == master, 버전 합계 == 업데이트 수 + 4.

## 2. Problem (English)

You maintain a feed handler that publishes a 64-byte top-of-book `Quote` per instrument. One writer thread updates each quote millions of times per second; several strategy threads read them. The writer must never block, and readers must never act on a torn snapshot.

**Part A.** Implement a seqlock for any trivially-copyable `T` (size multiple of 8) that is *data-race-free under the C++20 memory model*:
`void sl_store(SeqLock<T>&, const T&, const Hook* = nullptr)` (single writer), `bool sl_try_load(const SeqLock<T>&, T& out, const Hook* = nullptr)` (one attempt; `out` untouched on failure), `unsigned sl_load(const SeqLock<T>&, T& out, const Hook* = nullptr)` (retry until success, return the number of failed attempts). Data words must be `std::atomic<uint64_t>`; justify every memory order and fence. The hook is a test seam called after each data word is stored/loaded.

**Part B.** Explain (and the tests demonstrate) why a reader that skips the sequence check, a reader that skips the odd check, and two concurrent writers all accept torn data. Implement `bool sl_try_store_mw(...)` for multiple writers (CAS the sequence from even to odd; return false if another writer holds it) and `int sl_load_bounded(const SeqLock<T>&, T&, int max_tries, const Hook*)` (attempts used, or -1).

**Part C.** Implement `feed_on_update` (writer keeps a private master copy, never reads the shared book) and `strat_poll` (checksum + per-instrument monotonic `seq_no` validation, retry histogram). Stress with 1 feed + 3 strategy threads: zero torn, zero regressions, exact version counts.

Follow-up: measure reader latency against `pthread_rwlock_t` and a `std::mutex` copy with one writer at several rates; show the writer never blocks; show reader starvation when the writer is saturated.

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread starter.cpp -o starter && ./starter
```

처음 실행 결과 (실제 출력 일부, exit 1, 0.09초에 끝남 — `sl_load` 스텁은 루프를 돌지 않고 `~0u` 를 돌려준다. 멀티스레드 스트레스는 같은 Part 의 단위 테스트가 다 통과해야 돈다):

```text
FAIL starter.cpp:210  sl.seq.load() == 2u && sl_version(sl) == 1u
FAIL starter.cpp:211  sl_try_load(sl, out) && same(out, q1)
...
FAIL starter.cpp:229  sl_load(sl, out, &hz) == 2u && out.seq_no == 6u && quote_ok(out)
FAIL starter.cpp:230  sl_load(sl, out) == 0u && sl_version(sl) == 6u
SKIP 3 stress checks (Part A): unit checks failed
FAIL starter.cpp:278  y.fired == 1 && y.reader_ok && !quote_ok(y.seen)
...
FAIL starter.cpp:311  sl_load_bounded(sl, out, 5, &hz2) == 3 && out.seq_no == 21u && quote_ok(out)
SKIP 2 stress checks (Part B): unit checks failed
FAIL starter.cpp:353  !strat_poll(*b, 1, r, out) && r.reads == 1u && r.torn == 0u && r.fresh == 0u
...
SKIP 3 stress checks (Part C): unit checks failed
2/36 passed
```

관찰용 파일 (하네스와 별개, 숫자는 CHECK 하지 않는다):

```bash
c++ -std=c++20 -Wall -Wextra -O2 -pthread demo_torn.cpp -o torn && ./torn          # 깨진 변형 6개 찢어짐 비율 (약 7초)
c++ -std=c++20 -Wall -Wextra -O2 -pthread demo_bench.cpp -o bench && ./bench 300   # seqlock vs rwlock vs mutex (약 8초)
c++ -std=c++20 -g -O1 -fsanitize=thread -pthread demo_tsan.cpp -o dts && ./dts plain   # memcpy|plain|correct|no_fence
c++ -std=c++20 -O2 -c demo_asm.cpp -o a.o && xcrun llvm-objdump -d --no-show-raw-insn a.o           # arm64 명령
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_asm_m4.c -o -              # Cortex-M4 명령
```

## 4. 힌트

<details>
<summary>힌트 1 — 데이터를 왜 atomic 워드로?</summary>

reader 가 복사하는 동안 writer 가 같은 바이트를 쓰는 건 피할 수 없다 — seqlock 은 "겹치면 나중에 버린다" 는 설계다. 그런데 C++ 에서 겹치는 일반(non-atomic) 접근은 그 자체로 data race = UB 다 ([intro.races]). 버릴 값이라도 읽는 순간 이미 UB. 그래서 데이터도 `std::atomic<uint64_t>` 워드로 두고 relaxed 로 읽고 쓴다. arm64 에서 relaxed 64비트 load/store 는 그냥 `ldr` / `str` 이라 공짜다 (5. 풀이 asm 표).
</details>

<details>
<summary>힌트 2 — writer: 홀수 store 를 release 로 하면 되지 않나?</summary>

release store 는 **앞의** 접근이 자기보다 먼저 보이게 할 뿐, **뒤의** 데이터 store 가 홀수보다 먼저 보이는 건 막지 못한다. 필요한 건 "홀수 → 데이터" 순서다 → 홀수 store **뒤에** `atomic_thread_fence(release)`. 마지막 짝수 store 는 "데이터 → 짝수" 순서라 release store 로 충분하다.
</details>

<details>
<summary>힌트 3 — reader: s2 를 acquire 로 읽으면 되지 않나?</summary>

같은 함정의 거울상이다. acquire load 는 **뒤의** 접근이 앞으로 오는 걸 막는다. 필요한 건 "데이터 load → s2" 순서 (데이터를 다 읽은 **다음에** seq 를 다시 본다). 그래서 데이터 load 와 s2 사이에 `atomic_thread_fence(acquire)`. 첫 load s1 은 "s1 → 데이터" 순서라 acquire load 로 충분하다.
</details>

<details>
<summary>힌트 4 — sl_try_store_mw 의 CAS</summary>

`s = seq.load(relaxed)`; s 가 홀수면 바로 false. 아니면 `compare_exchange_strong(s, s + 1, acquire, relaxed)` — 실패하면 다른 writer 가 먼저 잡은 것. acquire 는 앞 writer 의 짝수 release store 와 짝이 되어 "앞 writer 의 데이터 store → 내 데이터 store" 순서를 만든다. 그 다음은 단일 writer 와 똑같이 release fence → 데이터 → 짝수 release store.
</details>

<details>
<summary>힌트 5 — strat_poll 의 순서</summary>

`sl_load` 가 성공한 스냅샷은 seqlock 이 맞다면 항상 온전하다. 그래도 체크섬을 먼저 보는 이유: 이 함수가 seqlock 구현의 **검증기** 역할을 하기 때문이다 (torn 이 1 개라도 나오면 seqlock 이 틀린 것). seq_no 는 종목별로 `last_seq[inst]` 와 비교: 작으면 역행, 같으면 새 소식 없음, 크면 fresh.
</details>

## 5. 풀이

### Part A — 세 가지 결정

**결정 1. 데이터를 relaxed atomic 워드로 복사한다.** Boehm(2012)의 첫 번째 지적이다: 보통 `memcpy` seqlock 은 "겹친 읽기는 버리니까 괜찮다" 고 생각하기 쉽지만, C/C++ 표준에서는 겹친 일반 접근 자체가 data race → 프로그램 전체가 UB 다. 컴파일러는 "race 가 없다" 고 가정하고 최적화해도 된다 (예: 복사한 값을 다시 메모리에서 읽기, 검사 전에 값을 쓰기 시작하기). 그래서:

| 방법 | 표준상 | 비용 (이 맥 arm64 실측 asm) |
|---|---|---|
| 데이터 `std::atomic<uint64_t> w[N]`, relaxed | race 없음 ✔ | `ldr` / `str` — 일반 접근과 같은 명령 |
| 보통 구조체 + 워드마다 `std::atomic_ref` (연습 2) | race 없음 ✔ (동시 접근 중엔 **모든** 접근이 atomic_ref 여야 함) | 같음 (`rd_atomic_ref` asm 이 `rd_correct` 와 동일) |
| 보통 구조체 + `memcpy` | data race = UB ✘ | 같음 — 그래서 "실무에선 돌아간다" 는 말이 나온다 |
| P1478 바이트 단위 atomic memcpy (`atomic_load_per_byte_memcpy`) | 표준화 진행 중 (제안서 기준, 채택 여부 미확인) | — |

`std::bit_cast<std::array<uint64_t, N>>(v)` 로 `T` 를 워드로 바꾸고, 다 읽은 워드 배열을 `bit_cast<T>` 로 되돌린다 (`memcpy` 와 같은 일을 하지만 지역 변수끼리라 race 가 아니다). 그래서 `Quote` 는 패딩이 없게 설계했다 (패딩 비트는 bit_cast 결과가 unspecified).

**결정 2. writer: 홀수 store → release fence → 데이터 relaxed store → 짝수 release store.**
**결정 3. reader: s1 acquire load → 데이터 relaxed load → acquire fence → s2 relaxed load.**

왜 이 네 자리인지 happens-before 로 따라가 보자 (C++20 [atomics.fences]/2 fence–fence 동기화, [intro.races] 쓰기–읽기 일관성).

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">두 개의 동기화 고리 — 어느 쪽으로 겹쳐도 reader 가 알아챈다</text>
<text x="40" y="48" fill="currentColor" font-weight="bold">writer (버전 5→6)</text>
<text x="450" y="48" fill="currentColor" font-weight="bold">reader</text>
<rect x="20" y="60" width="270" height="30" rx="4" class="f-warn" stroke="currentColor"/><text x="30" y="80" fill="currentColor" font-family="monospace" font-size="12">W1 seq.store(5, relaxed)</text>
<rect x="20" y="104" width="270" height="30" rx="4" class="f-acc" stroke="currentColor"/><text x="30" y="124" fill="currentColor" font-family="monospace" font-size="12">W2 fence(release)</text>
<rect x="20" y="148" width="270" height="30" rx="4" class="f-mut" stroke="currentColor"/><text x="30" y="168" fill="currentColor" font-family="monospace" font-size="12">W3 w[k].store(new, relaxed)</text>
<rect x="20" y="192" width="270" height="30" rx="4" class="f-ok" stroke="currentColor"/><text x="30" y="212" fill="currentColor" font-family="monospace" font-size="12">W4 seq.store(6, release)</text>
<rect x="440" y="60" width="280" height="30" rx="4" class="f-ok" stroke="currentColor"/><text x="450" y="80" fill="currentColor" font-family="monospace" font-size="12">R1 s1 = seq.load(acquire)</text>
<rect x="440" y="104" width="280" height="30" rx="4" class="f-mut" stroke="currentColor"/><text x="450" y="124" fill="currentColor" font-family="monospace" font-size="12">R2 t[k] = w[k].load(relaxed)</text>
<rect x="440" y="148" width="280" height="30" rx="4" class="f-acc" stroke="currentColor"/><text x="450" y="168" fill="currentColor" font-family="monospace" font-size="12">R3 fence(acquire)</text>
<rect x="440" y="192" width="280" height="30" rx="4" class="f-warn" stroke="currentColor"/><text x="450" y="212" fill="currentColor" font-family="monospace" font-size="12">R4 s2 = seq.load(relaxed)</text>
<path d="M290 207 L440 75" stroke="currentColor" fill="none"/><text x="300" y="104" fill="currentColor" font-size="12">① R1 이 6 을 읽으면</text><text x="300" y="120" fill="currentColor" font-size="12">W4 sw R1 → W3 hb R2</text>
<path d="M290 163 L440 119" stroke="currentColor" stroke-dasharray="5 3" fill="none"/><text x="300" y="150" fill="currentColor" font-size="12">② R2 가 새 값을 읽으면</text>
<path d="M290 119 L440 163" stroke="currentColor" stroke-width="2" fill="none"/><text x="300" y="186" fill="currentColor" font-size="12">W2 sw R3 (fence–fence)</text>
<rect x="10" y="240" width="720" height="92" rx="6" fill="none" stroke="currentColor"/>
<text x="22" y="262" fill="currentColor">① 복사 시작 전에 쓰기가 끝났다: R1 이 6 을 읽으면 W3 의 데이터가 R2 에 다 보인다 → 온전한 새 값.</text>
<text x="22" y="284" fill="currentColor">② 복사 도중 쓰기가 시작됐다: R2 가 새 데이터를 하나라도 읽으면 W2 sw R3 → W1 hb R4</text>
<text x="22" y="304" fill="currentColor">   → R4 는 5 이상을 읽어야 한다 → s2 ≠ s1(4) → 재시도. W2 나 R3 가 없으면 이 고리가 끊긴다.</text>
<text x="22" y="324" fill="currentColor">s1 이 acquire 가 아니면 R2 가 R1 보다 먼저 실행돼 "옛 데이터 + 새 seq" 를 볼 수 있다 (①이 끊김).</text>
</svg>
```

②를 표준 문장으로: 릴리스 fence A(W2)가 atomic store X(W3)보다 앞서고(sequenced before), Y(R2)가 X 가 쓴 값을 읽고, Y 가 acquire fence B(R3)보다 앞서면 A 는 B 와 동기화한다. 그러면 W1 → W2 → R3 → R4 가 happens-before 로 이어지고, 쓰기–읽기 일관성 때문에 R4 는 W1(5) 이상의 값을 읽어야 한다.

**"그 자리 대신 이건 안 되나?"** — 면접에서 그대로 나오는 변형들:

| 대안 | 되나 | 이유 |
|---|---|---|
| s2 를 `load(acquire)` 로 (fence 없이) | ✘ | acquire 는 **뒤** 접근을 막는다. 앞의 데이터 load 가 s2 뒤로 가는 건 허용 → ②가 끊김 |
| 데이터 load 를 전부 acquire | ✔ | 각 데이터 load 뒤로 s2 가 못 간다. 대신 워드마다 `ldapr` (실측 asm), 일부 아키텍처에선 비쌈 |
| s2 를 `fetch_add(0, release)` (Boehm 의 "read-dont-modify-write") | ✔ | release RMW 앞의 load 는 뒤로 못 간다. 실측 asm `ldsetl xzr, x13, [x0]` — **reader 가 라인에 쓴다** → rwlock 처럼 reader 끼리 경합 (측정 안 함, 원리상) |
| 홀수 store 를 `store(release)` (fence 없이) | ✘ | release 는 **앞** 접근만 막는다. 데이터 store 가 홀수보다 먼저 보일 수 있음 → demo_torn `no_wr_fence` 0.30% |
| 데이터 store 를 전부 release | ✔ | 각 데이터 store 앞의 홀수 store 가 먼저 보인다. 워드마다 `stlr` |
| 전부 `seq_cst` 기본값 | ✔ | 맞지만 데이터 접근마다 `ldar`/`stlr` |

<!-- code: demo_asm.cpp -->

<!-- code: demo_asm_m4.c -->

**실제 명령** — `demo_asm.cpp` 를 `c++ -std=c++20 -O2 -c` → `xcrun llvm-objdump -d` (데이터 4 워드로 줄인 판, 실측):

```text
<wr_correct> (ltmp0)          <_rd_correct>                  <_rd_no_fence>
ldr   x8, [x0]                ldapr x10, [x0]      ; s1      ldapr x10, [x0]
add   x9, x8, #0x1            tbnz  w10, #0x0, ...           tbnz  w10, #0x0, ...
str   x9, [x0]      ; 홀수    ldr   x11, [x0, #0x8]          ldr   x11, [x0, #0x8]
dmb   ish           ; fence   ldr   x12, [x0, #0x10]         ldr   x12, [x0, #0x10]
ldr/str ×4          ; 데이터   ldr   x8,  [x0, #0x18]         ldr   x8,  [x0, #0x18]
add   x8, x8, #0x2            ldr   x9,  [x0, #0x20]         ldr   x9,  [x0, #0x20]
stlr  x8, [x0]      ; 짝수    dmb   ishld          ; fence   ldr   x13, [x0]   ; s2 (fence 없음)
                              ldr   x13, [x0]      ; s2      cmp ...
```

읽는 법: acquire load 가 `ldar` 이 아니라 **`ldapr`** (FEAT_LRCPC, RCpc acquire) 로 나왔다 — Apple clang 이 이 맥의 기본 타깃에서 고른 것. seqlock 의 s1 에는 충분하다 (ldapr 과 ldar 의 차이는 "앞의 store-release 와의 순서" 뿐, ARM-M05). release fence = `dmb ish`, acquire fence = `dmb ishld` (load 만 정렬하는 가벼운 배리어). 데이터 접근은 일반 `ldr`/`str`. `rd_no_fence` 는 asm 상 load 순서가 그대로다 — 즉 아래 demo_torn 에서 보이는 찢어짐은 **컴파일러가 아니라 CPU 가** load 순서를 바꾼 결과다. `rd_atomic_ref` 는 `rd_correct` 와 한 글자도 다르지 않았다.

Cortex-M4 (`demo_asm_m4.c`, 32비트 워드, 실측): writer `str` 홀수 → `dmb sy` → `str ×4` → `dmb sy` → `str` 짝수. reader `ldr` s1 → `dmb sy` → `ldr ×4` → `dmb sy` → `ldr` s2. v7-M 에는 `ldar`/`stlr`·`dmb ishld` 가 없어 acquire/release 가 전부 `dmb sy` 가 된다. 단일 코어 전용 판(`atomic_signal_fence` 만)은 `dmb` 가 하나도 없는 `ldr` 나열이 됐다 — 같은 코어의 ISR 은 그 코어의 프로그램 순서대로 보이므로 컴파일러 배리어로 충분하다는 C-M14 / C-A02 의 결론과 같다.

**스트레스 결과** (`solution.cpp` 실측, writer 1 + reader 3, 쓰기 30만 번, 4번에 1번만 1 µs 쉼, load average 3.64):

```text
Part A stress: writes=300000 reads=1034429/1038110/2989117 retries=17882457/17141639/19548044
```

받아들인 읽기 506만 번에 찢어짐 0, seq_no 역행 0. 재시도가 성공보다 훨씬 많다 — writer 가 거의 쉬지 않기 때문이고, 이게 Part C / 측정 절의 "reader 굶주림" 이다.

### Part B — 깨진 변형을 실제로 돌려 보면

**강제 스케줄 (결정적, 체크 테스트)**:

| 테스트 | 스케줄 | 결과 |
|---|---|---|
| ① `naive_load` | reader 가 워드 3 을 읽은 직후 writer 가 통째로 지나감 | 워드 0–3 은 v1, 4–7 은 v2 → 체크섬 틀림 |
| ② `load_no_odd_check` | writer 가 워드 3 을 쓴 직후 reader 가 처음부터 끝까지 실행 | s1 = s2 = 5(홀수) → **같으니 통과** → 찢어진 걸 받아들임. 올바른 `sl_try_load` 는 같은 자리에서 거절 |
| ③ writer 2개 + `sl_store` | A 가 워드 3 을 쓴 직후 B 가 통째로 실행 | 아래 표 |
| B1 `sl_try_store_mw` | 같은 자리 | B 는 홀수를 보고 물러남, A 완료 후 B 성공 |
| B2 `sl_load_bounded(5)` | 매 시도마다 쓰기 | -1, out 그대로, 시도 5번 |

③의 seq 흐름 — 단일 writer 코드는 "seq 를 바꾸는 건 나뿐" 이라 relaxed load 한 값을 믿는다:

| 단계 | 누가 | seq | 데이터 |
|---|---|---|---|
| 시작 | — | 2 | v1 |
| A: s=2 읽고 3 store, 워드 0–3 쓰기 | A | 3 | A 0–3, v1 4–7 |
| B: s=**3** 읽고 **4**(짝수!) store, 전부 쓰기, **5** store | B | 4 → 5 | B 전부 |
| A: 워드 4–7 쓰기, s+2 = **4** store | A | 5 → **4** (역행) | B 0–3 + A 4–7 |
| reader | — | 4, 4 | "안정" 이라 받아들임 → 찢어짐 |

B 가 쓰는 동안 seq 가 짝수(4)였으니 그 사이 reader 도 찢어진 걸 받아들일 수 있고, 끝난 뒤에도 영원히 찢어진 상태가 "안정" 으로 남는다. 고치는 법은 writer 끼리 배제 하나뿐이다: CAS(B1), 또는 mutex/spinlock + 단일 writer seqlock (연습 3, Linux `seqlock_t` 구조).

**진짜 스레드에서 찢어짐 비율** (`demo_torn.cpp`, writer 1(two_writers 는 2) + reader 3, 변형마다 1초, 실측 2회, load average 3.9–5.4):

<!-- code: demo_torn.cpp -->

| 변형 | 바꾼 것 | 받아들인 읽기 | torn (1회차) | torn (2회차) |
|---|---|---|---|---|
| correct | — | 1.56억 / 1.27억 | **0** | **0** |
| memcpy | 데이터를 보통 메모리 + `memcpy` (UB) | 1.35억 / 1.06억 | 0 | 0 |
| no_rd_fence | reader acquire fence 삭제 | 4.38억 / 4.53억 | 151 661 (0.035%) | 171 407 (0.038%) |
| no_wr_fence | writer release fence 삭제 | 1.33억 / 2.08억 | 417 063 (0.31%) | 624 498 (0.30%) |
| all_relaxed | fence 둘 다 삭제 + seq 전부 relaxed | 7.79억 / 10.3억 | 3 068 920 (0.39%) | 1 270 043 (0.12%) |
| no_odd | reader 홀수 검사 삭제 | 0.70억 / 0.96억 | 3 557 553 (5.1%) | 4 259 790 (4.4%) |
| two_writers | 올바른 단일 writer 코드를 2 스레드가 | 0.10억 / 0.12억 | 1 297 558 (13.6%) | 1 597 010 (13.4%) |

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">찢어진 스냅샷 비율 (demo_torn 2회차, 로그 축, Apple M6)</text>
<path d="M170 40 L170 250" stroke="currentColor" fill="none"/>
<text x="150" y="268" fill="currentColor" font-size="12">0.01%</text><text x="282" y="268" fill="currentColor" font-size="12">0.1%</text><text x="420" y="268" fill="currentColor" font-size="12">1%</text><text x="550" y="268" fill="currentColor" font-size="12">10%</text><text x="676" y="268" fill="currentColor" font-size="12">100%</text>
<path d="M300 40 L300 252 M430 40 L430 252 M560 40 L560 252 M690 40 L690 252" stroke="currentColor" stroke-dasharray="2 4" fill="none"/>
<text x="10" y="62" fill="currentColor">correct</text><text x="180" y="62" fill="currentColor" font-size="12">0 (1.27억 중)</text>
<text x="10" y="92" fill="currentColor">memcpy (UB)</text><text x="180" y="92" fill="currentColor" font-size="12">0 관찰 — 맞다는 뜻 아님</text>
<text x="10" y="122" fill="currentColor">no_rd_fence</text><rect x="170" y="108" width="75" height="20" class="f-warn" stroke="currentColor"/><text x="251" y="123" fill="currentColor" font-size="12">0.038%</text>
<text x="10" y="152" fill="currentColor">no_wr_fence</text><rect x="170" y="138" width="192" height="20" class="f-warn" stroke="currentColor"/><text x="368" y="153" fill="currentColor" font-size="12">0.30%</text>
<text x="10" y="182" fill="currentColor">all_relaxed</text><rect x="170" y="168" width="140" height="20" class="f-warn" stroke="currentColor"/><text x="316" y="183" fill="currentColor" font-size="12">0.12%</text>
<text x="10" y="212" fill="currentColor">no_odd</text><rect x="170" y="198" width="344" height="20" class="f-acc" stroke="currentColor"/><text x="520" y="213" fill="currentColor" font-size="12">4.4%</text>
<text x="10" y="242" fill="currentColor">two_writers</text><rect x="170" y="228" width="407" height="20" class="f-acc" stroke="currentColor"/><text x="583" y="243" fill="currentColor" font-size="12">13.4%</text>
</svg>
```

정직하게 읽기:

- **fence 를 빼면 이 칩에서 실제로 찢어진다.** asm 상 load/store 순서는 그대로였으니(위 asm) CPU 재배치다. writer 쪽(store→store)이 reader 쪽(load→load)보다 약 10배 자주 보였다. 원인 분석(쓰기 버퍼 drain 순서 등)은 하지 않았다 — 추정만.
- **memcpy 판은 0 이었다.** 이 바이너리에서 clang 은 fence 를 `dmb` 로 남겼고 `memcpy` 도 그 사이에 머물렀기 때문으로 보인다. 그래도 표준상 UB 이고, 다른 컴파일러/최적화/인라인 조합에서 보장이 없다. "돌아간다" 와 "맞다" 를 구분하는 게 이 레슨의 요점이다.
- 비율은 부하·스케줄에 따라 2–3배씩 흔들린다 (all_relaxed 0.39% → 0.12%). 0 이냐 아니냐만 믿는다.

**TSan 이 보는 것** (`demo_tsan.cpp`, 실측):

<!-- code: demo_tsan.cpp -->

| 모드 | 무엇 | TSan |
|---|---|---|
| plain | 데이터 보통 메모리, 워드 단위 load/store | **data race 보고**, exit 134 |
| memcpy | 같은 race 인데 `memcpy` 로 | **보고 없음** (exit 0) — 이 맥에서 놓침 |
| correct | Boehm 형태 | 보고 없음 (정상) |
| no_fence | reader fence 삭제 (실제로 찢어지는 판) | 보고 없음 — 접근이 전부 atomic 이라 race 가 아니다 |

```text
$ ./dts plain
WARNING: ThreadSanitizer: data race (pid=86441)
  Read of size 8 at 0x000100984000 by thread T1:
    #0 void* std::__1::__thread_proxy[abi:nqe210106]<...main::$_0>>(void*) thread.h:168 (dts:arm64+0x100000bd0)
  Previous write of size 8 at 0x000100984000 by main thread:
    #0 main demo_tsan.cpp:57 (dts:arm64+0x10000094c)
  Location is global 'g_plain' at 0x000100984000 (dts+0x100008000)
  …
ThreadSanitizer: reported 2 warnings
```

memcpy 판을 따로 줄여서(`/tmp` 의 t3.cpp: 양쪽 모두 `memcpy`, fence 도 seq 도 없음) 돌려도 보고가 없었다. `-S` 로 보면 두 복사 모두 `bl ___tsan_memcpy` 로 계측은 되어 있다 — 그런데 race 가 안 잡힌다. 원인(이 런타임의 memcpy 인터셉터 동작)은 확인하지 않았다. 교훈: **TSan 이 조용하다 ≠ race 가 없다**, 그리고 no_fence 처럼 **atomic 끼리의 순서 버그는 TSan 이 원리상 못 본다** (race 정의에 안 걸림). 순서 버그는 강제 스케줄 테스트 + 하드웨어 스트레스 + 코드 리뷰로 잡는다.

### Part C — 북 설계에서 고른 것

- **writer 는 공유 북을 읽지 않는다.** `Feed::master` 가 진실이고 `sl_store` 는 게시만 한다. writer 가 공유 데이터를 읽어 고치면(read-modify-publish) 자기 라인을 다시 가져오고, 읽은 값이 reader 의 코어에서 막 돌아온 라인이라 느리다. 단일 writer 의 특권: 자기 사본만 믿으면 된다.
- **종목마다 128 B 정렬.** `sysctl hw.cachelinesize` = 128. `SeqLock<Quote>` = seq 8 B + 데이터 64 B = 72 B → `alignas(128)` 로 종목마다 라인 하나. 종목 A 의 쓰기가 종목 B reader 를 재시도시키지 않는다 (false sharing 은 ARCH-M08).
- **reader 검사는 검증기다.** 올바른 seqlock 이면 torn 과 regress 는 영원히 0 이다. 운영 코드에서는 체크섬을 빼도 되지만, 테스트 빌드에서는 남겨서 seqlock 이 깨지면 바로 보이게 한다.
- **재시도 히스토그램** (실측, 피드는 쉬지 않고 60만 업데이트, 종목 4개):

```text
Part C stress: updates=600000 reads=842134/825501/845052 fresh=443312/440595/443478
  retry histogram 0/1/2/3/4-7/8-15/16+: 1459574 15358 808 751 6203 10634 1019359
```

분포가 0 과 16+ 로 **두 봉우리** 다. 재시도 1회는 대부분 "홀수 seq 를 보고 곧장 실패" 하는 load 하나짜리라 아주 짧다 — writer 가 쓰는 중(수십~수백 ns, reader 들이 같은 라인을 두드리면 더 길어짐)에 들어가면 그 동안 수십 번 빠르게 실패한다. 그래서 "재시도 횟수" 보다 "읽기 1번의 시간" (아래 측정)이 진짜 지표다.

### 측정 — reader 지연: seqlock vs `pthread_rwlock_t` vs `std::mutex`

`demo_bench.cpp`: 64 B 스냅샷, writer 1개를 목표 속도로 돌리고 reader R 개가 쉬지 않고 읽는다. 타이머(`clock_gettime_nsec_np(CLOCK_UPTIME_RAW)`)의 눈금이 41.7 ns 라서 읽기 1번(약 3 ns)을 따로 잴 수 없다 → **64번 연속 읽기의 평균** 을 표본 하나로 쓰고, 그 표본들의 p50/p99/p99.9 를 낸다 (그래서 p99 는 "가장 나쁜 64묶음의 평균" 이고 진짜 단일 읽기 꼬리는 더 길다). writer 표본은 쓰기 1번(락 대기 포함)을 직접 잰 값(눈금 41.7 ns 단위).

<!-- code: demo_bench.cpp -->

기계: `Apple M6`, perflevel0/1/2 = 2/4/6 코어 (Super/Performance/Efficiency), `Apple clang version 21.0.0 (clang-2100.0.123.102)`. 다른 작성 에이전트들의 벤치마크와 동시에 돌았다 (load average 5.86 → 5.87, 아래 표 실행 전후). **배수만 믿는다.** 코어 고정 불가 (macOS).

reader 4개 (단위 ns/읽기, Mreads/s = reader 4개 합계):

| writer 속도 | 방식 | p50 | p99 | p99.9 | Mreads/s | 실제 쓰기 수 (0.3초) | writer p99 ns |
|---|---|---|---|---|---|---|---|
| 0 | seqlock | 3.9 | 31.9 | 47.5 | 703.6 | — | — |
| 0 | rwlock | 427.1 | 915.4 | 1198.6 | 9.1 | — | — |
| 0 | mutex | 14.3 | 820.3 | 1576.8 | 36.8 | — | — |
| 100k/s | seqlock | 3.9 | 29.9 | 41.7 | 700.7 | 30 000 | 125 |
| 100k/s | rwlock | 5.9 | 9968.1 | 10080.1 | 8.7 | 30 000 | 15 209 |
| 100k/s | mutex | 29.3 | 812.5 | 1321.0 | 32.6 | 30 000 | 24 750 |
| 1M/s | seqlock | 3.9 | 32.6 | 44.3 | 563.0 | **300 000** | 125 |
| 1M/s | rwlock | 7162.1 | 9628.9 | 10701.8 | 0.6 | **41 615** | 13 834 |
| 1M/s | mutex | 44.3 | 890.6 | 1459.6 | 28.0 | 299 968 | 13 875 |
| 최대 | seqlock | 37.8 | 1392.6 | 25882.8 | 21.1 | 1 709 484 | 208 |
| 최대 | rwlock | 5133.5 | 8371.8 | 13007.8 | 0.9 | 53 891 | 13 708 |
| 최대 | mutex | 25.4 | 888.0 | 1615.9 | 30.7 | 579 614 | 10 041 |

reader 1개 (p50 / p99 ns): writer 0 → seqlock 2.6 / 5.9, rwlock 5.9 / 7.2, mutex 4.6 / 10.4. writer 1M/s → seqlock 2.6 / 5.2, rwlock 20.2 / 5546.9 (쓰기 127 094 번만 성공), mutex 6.5 / 118.5. writer 최대 → seqlock 39.1 / 846.4 (재시도 64회 이상인 읽기 34%), mutex 6.5 / 556.0.

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">reader 4개의 p99 (64묶음 평균 ns, 로그 축) — writer 속도별</text>
<path d="M70 40 L70 250 L730 250" stroke="currentColor" fill="none"/>
<text x="40" y="254" fill="currentColor" font-size="12">1</text><text x="34" y="212" fill="currentColor" font-size="12">10</text><text x="28" y="170" fill="currentColor" font-size="12">100</text><text x="20" y="128" fill="currentColor" font-size="12">1 µs</text><text x="14" y="86" fill="currentColor" font-size="12">10 µs</text><text x="6" y="44" fill="currentColor" font-size="12">100 µs</text>
<path d="M70 208 L730 208 M70 166 L730 166 M70 124 L730 124 M70 82 L730 82" stroke="currentColor" stroke-dasharray="2 4" fill="none"/>
<rect x="90" y="187" width="40" height="63" class="f-ok" stroke="currentColor"/><rect x="132" y="126" width="40" height="124" class="f-warn" stroke="currentColor"/><rect x="174" y="128" width="40" height="122" class="f-mut" stroke="currentColor"/>
<text x="110" y="272" fill="currentColor" font-size="12">writer 0</text>
<rect x="250" y="188" width="40" height="62" class="f-ok" stroke="currentColor"/><rect x="292" y="82" width="40" height="168" class="f-warn" stroke="currentColor"/><rect x="334" y="128" width="40" height="122" class="f-mut" stroke="currentColor"/>
<text x="270" y="272" fill="currentColor" font-size="12">100k/s</text>
<rect x="410" y="186" width="40" height="64" class="f-ok" stroke="currentColor"/><rect x="452" y="83" width="40" height="167" class="f-warn" stroke="currentColor"/><rect x="494" y="126" width="40" height="124" class="f-mut" stroke="currentColor"/>
<text x="436" y="272" fill="currentColor" font-size="12">1M/s</text>
<rect x="570" y="118" width="40" height="132" class="f-ok" stroke="currentColor"/><rect x="612" y="86" width="40" height="164" class="f-warn" stroke="currentColor"/><rect x="654" y="126" width="40" height="124" class="f-mut" stroke="currentColor"/>
<text x="596" y="272" fill="currentColor" font-size="12">최대</text>
<rect x="90" y="292" width="14" height="14" class="f-ok" stroke="currentColor"/><text x="110" y="304" fill="currentColor" font-size="12">seqlock (32 / 30 / 33 / 1393)</text>
<rect x="320" y="292" width="14" height="14" class="f-warn" stroke="currentColor"/><text x="340" y="304" fill="currentColor" font-size="12">rwlock (915 / 9968 / 9629 / 8372)</text>
<rect x="560" y="292" width="14" height="14" class="f-mut" stroke="currentColor"/><text x="580" y="304" fill="currentColor" font-size="12">mutex (820 / 813 / 891 / 888)</text>
</svg>
```

읽는 법:

1. **reader 가 확장되는가.** writer 0 에서 reader 를 1 → 4 로 늘리면 seqlock 은 합계 340 → 704 Mreads/s 로 늘고, rwlock 은 155 → **9.1** 로 1/17 이 된다. rwlock 의 `rdlock` 은 "reader 수" 를 원자적으로 올리고 내리는 **쓰기** 라서, 읽기만 하는데도 4 코어가 같은 라인을 뺏는다 (LLAT-B06 의 공유 카운터 경합과 같은 그림). seqlock reader 는 load 만 한다 — 라인이 4 코어의 캐시에 동시에 Shared 로 있을 수 있다.
2. **writer 가 막히는가.** 1M/s 목표에서 seqlock writer 는 30만 번을 정확히 썼고 쓰기 p99 125 ns (= 눈금 3개, 시계 호출 포함). rwlock writer 는 reader 4개에 막혀 **4만 번** 밖에 못 썼다 (p99 13.8 µs). 피드 핸들러라면 이건 "피드를 놓쳤다" 는 뜻이다. mutex 도 writer p99 가 14 µs 다.
3. **대가: writer 가 쉬지 않으면 reader 가 굶는다.** writer 최대 속도에서 seqlock reader p99 가 1.4 µs, p99.9 가 26 µs 로 뛰고 읽기의 33% 가 64번 이상 재시도했다. 이때는 오히려 mutex(공정하게 번갈아 줌) 의 p99 가 더 낮다. seqlock 은 **writer 가 드문드문 쓰는(read-mostly) 데이터** 에 맞는 도구다 — 시장 데이터라도 종목당 업데이트 간격 ≫ 쓰기 시간(수십 ns)이면 맞는다.
4. macOS `pthread_rwlock_t` 는 writer 가 기다리기 시작하면 reader 도 줄 세우는 것으로 보인다 (writer 100k/s 에서 rwlock reader p50 5.9 ns 인데 p99 10 µs — 두 봉우리). 구현 세부(libpthread 의 writer 선호 정책, 커널 psynch 대기)는 확인하지 않았다.

### Linux 와 펌웨어에서

- **Linux** (커널 문서 "Sequence counters and sequential locks" 기준, 소스 미확인 부분은 추정): `seqcount_t` = 순수 카운터 (writer 직렬화는 호출자 책임, writer 구간은 선점·인터럽트 불가여야 함), `seqcount_LOCKNAME_t` (예: `seqcount_spinlock_t`, writer 락을 lockdep 과 연결 — PREEMPT_RT 에서는 reader 가 그 락을 잠깐 잡아 선점된 writer 를 기다린다고 문서에 설명), `seqlock_t` = seqcount + spinlock (`write_seqlock`/`write_sequnlock`, reader `read_seqbegin`/`read_seqretry`). `write_seqcount_begin` = 증가 후 `smp_wmb()`, `read_seqcount_retry` = `smp_rmb()` 후 비교 — 이 레슨의 release fence / acquire fence 자리와 같다. `read_seqbegin` 은 홀수면 짝수가 될 때까지 돈다. 커널은 데이터를 일반 접근으로 읽는다 (C++ 표준이 아니라 LKMM 과 컴파일러 옵션으로 관리, KCSAN 은 seqlock 읽기 구간을 특별 취급 — 추정). reader 굶주림 대책 `read_seqbegin_or_lock()` (한 번 실패하면 락을 잡고 읽기), NMI 처럼 기다릴 수 없는 reader 용 latch (`raw_write_seqcount_latch`, 연습 4). 쓰임새: 32비트 커널의 `jiffies_64` 읽기, timekeeping.
- **펌웨어 (ISR writer → main reader)**: 엔코더/타이머 ISR 이 `{pos:int64, ts:uint64, vel, idx}` 를 쓰고 main 이 읽는다 (연습 1). main 은 인터럽트를 끄지 않는다 → **인터럽트 지연 0** (PRIMASK 로 막는 방식은 그 구간만큼 모든 IRQ 를 늦춘다, C-M14). 단일 코어면 main 은 홀수를 볼 수 없고(ISR 은 끝까지 돈다) 복사 도중 ISR 이 오면 s1 ≠ s2 로 재시도한다. reader 가 ISR 이면 seqlock 은 무한 루프 → latch 또는 임계 구역 (C-M14). 듀얼 코어(STM32H7 M7+M4, nRF5340, RP2040)라면 위 M4 asm 처럼 진짜 `dmb` 가 필요하다. seqlock 은 RMW 가 필요 없어서 LDREX 가 없는 v6-M(M0/M0+)에서도 일반 load/store + `__DMB()` 로 만들 수 있다 (단 clang 의 C++ `std::atomic` 이 M0+ 에서 libcall 이 되는 문제는 CPP-A04).

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread solution.cpp -o solution && ./solution      # 36/36 passed (6번 실행 모두)
c++ -std=c++20 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.cpp -o so_asan && ./so_asan   # 36/36, 보고 없음
c++ -std=c++20 -g -O1 -fsanitize=thread -pthread solution.cpp -o so_tsan && ./so_tsan         # 36/36, 보고 없음 (1.8초)
```

TSan 빌드가 조용한 이유는 "데이터까지 atomic 이라 race 가 없다" 이지, "fence 가 맞다" 를 TSan 이 확인해서가 아니다 (위 no_fence 판도 조용했다). fence 의 정당성은 강제 스케줄 + 스트레스 + happens-before 논증으로 확인한다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 데이터를 보통 구조체 + `memcpy` | 겹친 읽기 자체가 data race(UB). "버릴 값" 이어도. 이 맥 TSan 은 memcpy 경로를 놓치기까지 했다 | 데이터 `std::atomic<uint64_t> w[N]` relaxed, 또는 워드마다 `atomic_ref` |
| writer: `seq.store(odd, release)` 하고 fence 없음 | release 는 앞 접근만 막는다. 데이터가 홀수보다 먼저 보임 (실측 0.30%) | `seq.store(odd, relaxed); atomic_thread_fence(release);` |
| reader: `s2 = seq.load(acquire)` 하고 fence 없음 | acquire 는 뒤 접근만 막는다. 데이터 load 가 s2 뒤로 감 (실측 0.035%) | 데이터 load 뒤 `atomic_thread_fence(acquire); s2 = seq.load(relaxed);` |
| 홀수 검사 생략 (`s1 == s2` 만) | reader 가 writer 한가운데에 통째로 들어가면 s1 = s2 = 홀수 (실측 4–5%) | `if (s1 & 1) retry;` |
| writer 2개가 단일 writer 코드 | `load → store(s+1)` 이 원자적이지 않다 → seq 역행, 찢어진 "안정" 상태 (실측 13%) | CAS 짝수→홀수, 또는 writer mutex + seqcount |
| 복사한 스냅샷 안의 **포인터** 를 검증 전에 따라감 | 찢어진 값의 포인터는 쓰레기/해제된 메모리 → 검증 전에 segfault | 검증(s1 == s2) 뒤에만 사용. 포인터 대상의 수명은 RCU/EBR (OS-A03) |
| writer 가 공유 데이터를 읽어 고치기 | 자기가 쓴 라인을 reader 에게서 다시 가져옴 + 단일 writer 가정이 흐려짐 | writer 전용 master 사본 → 게시만 |
| latch 에서 홀수 store 를 relaxed 로 | latch reader 는 홀수일 때 **copy[1] 을 읽는다** → 직전 copy[1] 쓰기가 홀수보다 먼저 보여야 함. 연습 4 를 처음에 이렇게 짜서 스트레스가 5/5 실패 (torn 1–87 / 약 520만–900만 읽기) | `seq.store(s+1, release); fence(release);` |
| 재시도 루프에 상한·양보 없음 (실시간 경로) | writer 가 선점되거나 계속 쓰면 reader 가 무한정 돈다 | `sl_load_bounded` + 지난 값 사용, 또는 `read_seqbegin_or_lock` 식 폴백 |

## 8. 면접 Q&A

<details>
<summary>Q1. seqlock reader 는 언제 재시도해야 하고, writer 굶주림(starvation)은 누가 누구를 굶기나?</summary>

재시도 조건 두 가지: ① 처음 읽은 seq 가 홀수 (writer 가 쓰는 중), ② 복사 뒤 다시 읽은 seq 가 처음 값과 다름 (복사 도중 writer 가 지나감). seqlock 에서 굶는 건 **reader** 다 — writer 는 reader 를 전혀 기다리지 않으니까. writer 가 쉬지 않고 쓰면 reader 는 성공할 틈이 없다 (실측: writer 최대 속도에서 reader 읽기의 33% 가 64회 이상 재시도, p99.9 26 µs). rwlock 은 반대로 reader 가 writer 를 굶길 수 있다 (실측: 1M/s 목표 writer 가 4만 번만 씀). 꼬리: "reader 굶주림을 어떻게 막나?" → writer 빈도를 낮추기(배치/합치기), 재시도 상한 후 락 폴백(Linux `read_seqbegin_or_lock`), 또는 RCU 처럼 새 버전을 따로 만들어 포인터만 바꾸기 (OS-A03).
</details>

<details>
<summary>Q2. 겹친 읽기는 어차피 버리는데 왜 데이터를 atomic 으로 읽어야 하나? 실무 코드는 memcpy 쓰던데.</summary>

C++ [intro.races]: 서로 다른 스레드의 충돌하는 접근 중 하나라도 non-atomic 이고 happens-before 가 없으면 data race 이고, 프로그램 전체가 UB 다. 값을 버리는지는 상관없다. 컴파일러는 race 가 없다고 가정해 읽기를 다시 하거나(재로드) 합칠 수 있고, 그러면 검증과 사용 사이에 값이 바뀔 수 있다. Boehm(2012)의 결론: 데이터를 relaxed atomic 으로 읽고 fence 로 순서를 잡아라. arm64 에서 relaxed load/store 는 `ldr`/`str` 그대로라 비용이 없다 (실측 asm). memcpy 판이 "돌아가는" 이유는 대부분의 컴파일러가 실제로 그렇게 최적화하지 않기 때문이고(이 맥 실측도 torn 0), Linux 커널은 C 표준 대신 자체 메모리 모델(LKMM)과 컴파일러 옵션으로 이를 관리한다. 바이트 단위 atomic memcpy 제안(P1478)이 이 문제를 겨냥한다 (채택 여부 미확인).
</details>

<details>
<summary>Q3. reader 의 두 번째 seq load 를 acquire 로 바꾸면 fence 를 빼도 되나?</summary>

안 된다. acquire 는 "이 load **뒤** 의 접근이 앞으로 오지 못한다" 이다. 필요한 건 "앞의 데이터 load 가 s2 **뒤로** 가지 못한다" — 방향이 반대다. 되는 방법은 세 가지: s2 앞에 `atomic_thread_fence(acquire)` (arm64 `dmb ishld` 하나, 실측), 데이터 load 를 전부 acquire (`ldapr` 여러 개), s2 를 `fetch_add(0, release)` (실측 `ldsetl` — reader 가 라인에 쓰므로 확장성을 잃는다). 꼬리: "writer 의 홀수 store 를 release 로?" → 똑같이 방향이 반대. 홀수 store **뒤** 에 release fence.
</details>

<details>
<summary>Q4. writer 가 두 개면 정확히 뭐가 깨지나? 어떻게 고치나?</summary>

단일 writer 는 `s = seq.load(); seq.store(s+1)` 을 쓰는데 이건 원자적인 증가가 아니다. A 가 홀수로 만든 뒤 B 가 그 홀수를 읽고 +1 하면 **짝수** 가 된다 → B 가 쓰는 동안 reader 가 "안정" 으로 보고 받아들인다. 끝나면 A 의 `s+2` 가 B 의 값보다 작아 seq 가 **역행** 하고, 데이터는 A·B 섞인 채 영원히 "안정" 이다 (강제 스케줄 테스트 + 실측 torn 13%). 고치기: writer 끼리 배제 — seq 를 짝수→홀수 CAS 로 잡거나(B1), writer 전용 mutex/spinlock 을 잡고 단일 writer 코드 실행 (연습 3, Linux `seqlock_t`). reader 쪽은 바꿀 게 없다. 꼬리: "seq 를 `fetch_add` 로 올리면?" → 두 writer 가 홀수/짝수를 하나씩 올려 버려 홀수 구간이 겹치고 짝수가 될 때 한쪽은 아직 쓰는 중일 수 있다. 배제가 필요하다.
</details>

<details>
<summary>Q5. 왜 rwlock 보다 빠른가? 언제는 rwlock/mutex 가 낫나?</summary>

rwlock 의 reader 도 락 워드에 **쓴다** (reader 수 증감). reader 가 여러 코어에 있으면 그 라인이 코어 사이를 오간다 — 실측 reader 4개에서 rwlock 9.1 Mreads/s vs seqlock 704 (77배). seqlock reader 는 load 만 하니 라인이 모든 코어에 Shared 로 복제된다. 그리고 writer 가 reader 를 기다리지 않는다 (1M/s 목표: seqlock 30만 번 다 씀, rwlock 4만 번). 반대로 seqlock 이 나쁜 경우: 데이터가 커서 복사가 비쌀 때 (재시도할 때마다 전부 다시 복사), writer 가 자주 쓸 때 (실측 writer 최대 속도에서 mutex p99 888 ns < seqlock 1393 ns), reader 가 데이터 안의 포인터를 따라가야 할 때, reader 가 부수 효과를 내야 할 때 (재시도하면 두 번 일어남). 그때는 rwlock/mutex 또는 RCU.
</details>

<details>
<summary>Q6. Linux 의 seqcount_t, seqlock_t, latch 는 각각 뭔가?</summary>

(커널 문서 기준) `seqcount_t` 는 카운터만 — writer 직렬화와 "writer 구간에서 선점·인터럽트 금지" 는 호출자 책임. writer 가 선점된 채 홀수로 남으면 reader 가 그 동안 돈다. `seqcount_LOCKNAME_t` 는 writer 락을 카운터에 연결해 lockdep 이 검사하게 하고, PREEMPT_RT 에서는 reader 가 그 락을 통해 writer 를 기다린다. `seqlock_t` 는 seqcount + spinlock 묶음 (`write_seqlock` 이 락도 잡음, 연습 3 과 같은 구조). latch(`seqcount_latch_t`)는 데이터 사본 2개를 두고 writer 가 번갈아 고쳐서, NMI 처럼 기다릴 수 없는 reader 가 항상 안정된 사본을 읽게 한다 (연습 4). 배리어는 writer `smp_wmb()`, reader `smp_rmb()` 로 이 레슨의 fence 자리와 같다.
</details>

<details>
<summary>Q7. 펌웨어에서 ISR 이 64비트 타임스탬프를 쓰고 main 이 읽는다. seqlock 이 인터럽트 끄기보다 나은 점과 못 쓰는 경우는?</summary>

나은 점: main 이 PRIMASK/BASEPRI 로 인터럽트를 막지 않으니 **인터럽트 지연이 늘지 않는다** — 고속 모터 제어/통신 ISR 에서 중요. writer(ISR)는 기다리는 일이 없다. 단일 코어에서는 main 이 홀수를 볼 수 없고, 복사 도중 ISR 이 오면 seq 가 달라져 한 번 더 읽는다 (연습 1: 강제 ISR → 재시도 1번, ISR 뒤 값). 못 쓰는 경우: reader 가 ISR 이고 writer 가 main 이면 ISR 이 main 의 쓰기 완료를 기다릴 수 없어 무한 루프 (C-M14) → latch(사본 2개) 또는 main 쪽 임계 구역. 멀티코어 MCU 면 컴파일러 배리어로는 부족하고 `dmb` 가 필요하다 (M4 asm 실측: acquire/release 가 `dmb sy`).
</details>

<details>
<summary>Q8. 이 seqlock 이 맞다는 걸 어떻게 테스트하나? TSan 이면 충분한가?</summary>

TSan 은 non-atomic 접근의 race 만 본다. 데이터를 atomic 으로 바꾼 뒤에는 fence 를 빼도 TSan 이 조용하다 (실측 no_fence) — 순서 버그는 TSan 의 영역 밖이다. 게다가 이 맥의 TSan 은 memcpy 로 한 race 를 놓쳤다. 그래서 세 겹으로 한다: ① 강제 스케줄 테스트 (hook 으로 "복사 도중 쓰기", "쓰기 도중 읽기", "writer 겹침" 을 결정적으로 재현), ② 하드웨어 스트레스 + 체크섬 (약한 메모리 칩에서, 올바른 판은 torn 0 이어야 하고 일부러 깨뜨린 판은 실제로 찢어지는지 확인 → 테스트가 버그를 잡을 힘이 있다는 증거), ③ happens-before 논증 (5. 풀이 그림). 형식 도구로는 CDSChecker/GenMC 같은 C11 모델 체커가 있다 (써 보지 않음).
</details>

## 9. 추가 연습

### 연습 1 — 엔코더 스냅샷: ISR 이 쓰고 main 이 읽는다

`EncSnap {int64 pos; uint64 ts_ns; int32 vel; uint32 idx}` (24 B) 를 `SeqLock<EncSnap>` 로 공유한다. `enc_isr(sl, state, delta, ts)` 는 ISR 전용 사본을 고치고 게시, `enc_read(sl, &retries, hook)` 는 main 쪽 읽기. hook 으로 "main 이 워드 1 을 읽은 직후 ISR" 을 강제해 재시도 1번 + ISR 뒤 값을 확인하라.

<details>
<summary>답 보기</summary>

```cpp
static void enc_isr(SeqLock<EncSnap> &sl, EncIsrState &st, std::int32_t delta, std::uint64_t ts) {
    st.cur.pos += delta; st.cur.vel = delta; st.cur.ts_ns = ts; st.cur.idx++;
    sl_store(sl, st.cur);                         // ISR 은 자기 사본만 믿는다 (단일 writer)
}
static EncSnap enc_read(const SeqLock<EncSnap> &sl, unsigned *retries, const Hook *h = nullptr) {
    EncSnap s{};
    *retries = sl_load(sl, s, h);
    return s;
}
```

단일 코어 MCU 라면 `sl_store` 안의 release fence 와 reader 의 acquire fence 를 `atomic_signal_fence` 로 낮춰도 된다 (같은 코어 ISR, `demo_asm_m4.c` 의 `main_read_1core` → `dmb` 0 개). 하지만 듀얼 코어로 옮기는 순간 틀린 코드가 되니 기본은 thread fence 로 두고, 측정해서 필요할 때만 낮춘다. 테스트: 강제 ISR 1번 → retries 1, pos 15, idx 2; 3번 연속 → retries 3. 스레드 판(ISR 을 스레드로 20만 번)에서 불변식 `pos == idx`, `ts == idx-1` 위반 0.

</details>

### 연습 2 — `std::atomic_ref` 판: C 와 공유하는 보통 구조체

C 헤더에 `struct { uint64_t seq; uint64_t w[8]; }` 가 이미 있고 바꿀 수 없다 (C 코드, DMA, 공유 메모리 레이아웃). `std::atomic_ref<uint64_t>` 로 같은 seqlock 을 만들어라.

<details>
<summary>답 보기</summary>

```cpp
using ARef = std::atomic_ref<std::uint64_t>;
static void ar_store(PlainSlot &p, const Snap &v) {
    const auto in = std::bit_cast<std::array<std::uint64_t, 8>>(v);
    const std::uint64_t s = ARef(p.seq).load(memory_order_relaxed);
    ARef(p.seq).store(s + 1, memory_order_relaxed);
    std::atomic_thread_fence(memory_order_release);
    for (int k = 0; k < 8; k++) ARef(p.w[k]).store(in[k], memory_order_relaxed);
    ARef(p.seq).store(s + 2, memory_order_release);
}
```

reader 도 같은 모양 (`ARef(p.w[k]).load(relaxed)`). 규칙 두 개: ① 객체가 `atomic_ref::required_alignment` 를 만족해야 한다 (`static_assert(alignof(PlainSlot) >= ...)`), ② atomic_ref 로 동시에 접근되는 동안에는 **모든** 접근이 atomic_ref 여야 한다 ([atomics.ref.generic]) — 테스트의 `p.seq == 2u` 직접 읽기는 스레드가 없을 때라 괜찮다. asm 은 `std::atomic` 판과 완전히 같았다 (`demo_asm.cpp` 의 `rd_atomic_ref`). C 쪽에서 같은 구조를 쓰려면 C11 `atomic_load_explicit` 은 `_Atomic` 타입이 필요하므로 C 쪽은 `__atomic_load_n` 빌트인을 쓴다 (MCPP-M13 의 atomic_ref 절 참고).

</details>

### 연습 3 — writer 여러 개: mutex + seqcount

writer 스레드 2개가 같은 스냅샷을 쓴다. reader 는 여전히 무락이어야 한다. `LockedSeq { std::mutex wm; SeqLock<Snap> sl; }` 와 `locked_store` 를 써라.

<details>
<summary>답 보기</summary>

```cpp
static void locked_store(LockedSeq &l, const Snap &v) {
    std::lock_guard<std::mutex> g(l.wm);   // writer 끼리만 배제
    sl_store(l.sl, v);                     // 그 안은 단일 writer seqlock 그대로
}
```

Linux `seqlock_t` (seqcount + spinlock) 와 같은 구조다. B1 의 CAS 판과 비교: CAS 판은 락 객체가 없고 실패 시 호출자가 다시 시도(스핀)해야 한다. mutex 판은 writer 가 블록될 수 있지만 reader 에게는 아무 영향이 없다 — reader 는 mutex 를 모른다. writer 가 mutex 를 잡은 채 선점되면 reader 는 그 동안 홀수를 보며 돈다 (Linux 가 writer 구간 선점 금지를 요구하는 이유). 테스트: writer 2 × 10만 + 초기 1 → seq == 2·(200001), torn 0.

</details>

### 연습 4 — latch: reader 가 쓰는 중인 writer 를 절대 기다리지 않게

reader 가 NMI/ISR 이라 기다릴 수 없다. 데이터 사본 2개를 두고 seq 홀수면 copy[1], 짝수면 copy[0] 을 읽게 하라. writer 는 seq+1 → copy[0] 쓰기 → seq+1 → copy[1] 쓰기.

<details>
<summary>답 보기</summary>

```cpp
template <class T> static void latch_store(Latch<T> &l, const T &v, const Hook *h = nullptr) {
    const auto in = std::bit_cast<typename SeqLock<T>::Words>(v);
    const std::uint64_t s = l.seq.load(memory_order_relaxed);
    l.seq.store(s + 1, memory_order_release);   // ★ release: 지난번 copy[1] 쓰기가 먼저 보여야 한다
    std::atomic_thread_fence(memory_order_release);
    for (std::size_t k = 0; k < SeqLock<T>::N; k++) l.copy[0].w[k].store(in[k], memory_order_relaxed);
    l.seq.store(s + 2, memory_order_release);
    std::atomic_thread_fence(memory_order_release);
    for (std::size_t k = 0; k < SeqLock<T>::N; k++) l.copy[1].w[k].store(in[k], memory_order_relaxed);
}
template <class T> static bool latch_try_load(const Latch<T> &l, T &out) {
    const std::uint64_t s = l.seq.load(memory_order_acquire);
    const auto &c = l.copy[s & 1u];             // 홀수 → copy[1], 짝수 → copy[0]
    /* 워드 relaxed load → acquire fence → seq 재확인 (seqlock 과 같다) */
}
```

강제 스케줄: writer 가 copy[0] 워드 3 을 쓴 직후 reader 실행 → **첫 시도에 성공**, 옛 값 (seqlock 이었다면 홀수라 실패). 그 대신 reader 는 최신이 아닌 값을 받을 수 있다 — "항상 온전하지만 한 버전 늦을 수 있음". ★ 줄이 핵심 함정이다: 처음엔 일반 seqlock 처럼 홀수 store 를 relaxed 로 짰고, 단위 테스트는 다 통과했는데 스트레스가 5번 모두 실패했다 (실측 torn 1, 21, 87, 18, 85 / 약 520만–900만 읽기). seqlock 의 reader 는 홀수일 때 데이터를 안 읽지만 latch reader 는 홀수일 때 **copy[1] 을 읽는다** — 그래서 홀수 store 가 직전 copy[1] 쓰기의 release 역할을 해야 한다. Linux `raw_write_seqcount_latch` 가 증가 앞뒤로 `smp_wmb()` 를 두는 이유와 같다 (커널 문서 기준).

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread practice.cpp -o practice && ./practice                          # 13/13 passed (8번 실행 모두)
c++ -std=c++20 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread practice.cpp -o pr_asan && ./pr_asan   # 13/13, 보고 없음
c++ -std=c++20 -g -O1 -fsanitize=thread -pthread practice.cpp -o pr_tsan && ./pr_tsan                             # 13/13, 보고 없음
```

## 10. 핵심 정리

- seqlock = writer 는 seq 홀수 → 데이터 → 짝수, reader 는 seq 앞뒤 비교 (홀수거나 다르면 재시도). reader 는 아무것도 쓰지 않는다 → writer 를 못 막고, reader 끼리 확장된다.
- C++ 정답 형태 (Boehm 2012): 데이터도 atomic 워드(relaxed, arm64 에선 `ldr`/`str`), writer `store(odd, relaxed)` → **fence(release)** → 데이터 → `store(even, release)`, reader `load(acquire)` → 데이터 → **fence(acquire)** → `load(relaxed)`.
- acquire 는 뒤를, release 는 앞을 막는다 — 그래서 "s2 를 acquire 로", "홀수를 release 로" 는 틀린 답. arm64 실측: `ldapr` / `dmb ishld` / `dmb ish` / `stlr`. Cortex-M4: 전부 `dmb sy`.
- 이 맥에서 fence 를 빼면 실제로 찢어진다: reader 쪽 0.035%, writer 쪽 0.30%, 홀수 검사 생략 4–5%, writer 2개 13%. `memcpy` 판은 0 이었지만 UB.
- TSan: 보통 접근 race 는 잡지만(이 맥에선 memcpy 경로는 놓침) atomic 끼리의 순서 버그는 원리상 못 본다 → 강제 스케줄 + 스트레스 + 체크섬.
- 실측 (reader 4개): seqlock 704 vs rwlock 9.1 Mreads/s, 1M/s writer 가 seqlock 에선 30만 번 다 쓰고 rwlock 에선 4만 번. 대가: writer 가 쉬지 않으면 reader 굶주림 (p99.9 26 µs).
- writer 는 하나 (여럿이면 CAS 나 mutex 로 배제), writer 는 자기 사본만 믿고, reader 는 검증 전에 포인터를 따라가지 않는다. 기다릴 수 없는 reader 는 latch(사본 2개, 홀수 store 도 release).
- 펌웨어: ISR writer + main reader 면 인터럽트를 끄지 않아도 된다 (인터럽트 지연 0). reader 가 ISR 이면 latch 또는 임계 구역.

## 참고자료

- Hans-J. Boehm, "Can Seqlocks Get Along With Programming Language Memory Models?", MSPC 2012 (HP Labs 기술 보고서 HPL-2012-68): <https://www.hpl.hp.com/techreports/2012/HPL-2012-68.pdf> — 데이터 atomic + fence 형태, read-dont-modify-write 대안 (이 레슨은 논문의 결론을 기준으로 했고 절 번호는 인용하지 않음)
- C++20 (N4861) [intro.races] (data race, 쓰기–읽기 일관성), [atomics.fences] (fence–fence / fence–op 동기화), [atomics.order], [atomics.ref.generic] (atomic_ref 규칙), [bit.cast]: <https://timsong-cpp.github.io/cppwp/n4861/>
- P1478 "Byte-wise atomic memcpy" (Boehm 외, seqlock 용 제안): <https://wg21.link/p1478>
- Linux kernel docs — Sequence counters and sequential locks (`seqcount_t`, `seqcount_LOCKNAME_t`, `seqlock_t`, latch, `read_seqbegin_or_lock`): <https://docs.kernel.org/locking/seqlock.html>, `include/linux/seqlock.h`: <https://elixir.bootlin.com/linux/latest/source/include/linux/seqlock.h>
- Arm Architecture Reference Manual A-profile (DDI 0487) — LDAPR(FEAT_LRCPC), LDAR/STLR, DMB 옵션(ISH, ISHLD), LDSET: <https://developer.arm.com/documentation/ddi0487/latest/>
- ARMv7-M Architecture Reference Manual (DDI 0403) — DMB: <https://developer.arm.com/documentation/ddi0403/latest/>
- Clang ThreadSanitizer 문서: <https://clang.llvm.org/docs/ThreadSanitizer.html>
- Paul E. McKenney, *Is Parallel Programming Hard...* — 9장 Deferred Processing 의 Sequence Locks 절: <https://mirrors.edge.kernel.org/pub/linux/kernel/people/paulmck/perfbook/perfbook.html>
- 관련 레슨: C-M14 (ISR↔main, volatile seqlock, reader 가 ISR 인 경우), C-A02 (컴파일러 배리어·signal fence), C-A03 (acquire/release, arm64·M4 asm), MCPP-M13 (fence 동기화 규칙·atomic_ref), OS-M08 (relaxed 메시지 패싱 실측), ARM-M05 (LDAR/LDAPR/DMB), LLAT-B06 (원자 연산·경합 비용), CPP-A04 (Cortex-M lock-free 여부), OS-M07 (스핀락), OS-A01 (futex 뮤텍스), OS-A03 (RCU/EBR — 포인터 교체형 스냅샷), LLAT-M05 (코어 간 핸드오프 지연), ARCH-M08 (false sharing)
