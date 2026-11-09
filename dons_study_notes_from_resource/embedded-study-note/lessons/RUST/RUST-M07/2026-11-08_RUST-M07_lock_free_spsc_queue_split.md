---
id: RUST-M07
time: 90분
updated: 2026-11-08
---
# RUST-M07 Lock-free SPSC 큐: ISR 생산자·main 소비자 split

## 학습 목표

- `AtomicUsize` head/tail 과 `[UnsafeCell<MaybeUninit<T>>; N]` 버퍼로 `Queue<T, N>` 을 만들고, 네 개의 load/store 가 각각 왜 `Relaxed` / `Acquire` / `Release` 인지 happens-before 로 설명할 수 있다.
- `split(&mut self) -> (Producer<'_>, Consumer<'_>)` 와 `&mut self` 메서드가 "생산자 하나, 소비자 하나" 를 **타입 성질** 로 만든다는 것을 실제 컴파일 오류(E0499 · E0599 · E0596 · E0597 · E0277)로 보여 줄 수 있다.
- `unsafe impl Sync` 의 근거(슬롯 소유권 이전, `T: Send` 면 충분한 이유)를 쓰고, `Drop` 에서 남은 항목만 정확히 한 번 해제할 수 있다.
- loom 으로 순서를 `Relaxed` 로 약화한 버전이 실패하는 인터리빙을 찾고, loom 이 **못 찾는** 경우(load buffering)도 실제로 확인해 그 한계를 말할 수 있다.
- `heapless::spsc` 와 비교(용량 N vs N-1, `%` vs 마스크)하고, 같은 `#![no_std]` 라이브러리가 Cortex-M0+ 에서도 빌드되는 이유(load/store 만 씀)를 asm 으로 보여 줄 수 있다.

## 1. 문제

임베디드 Rust 면접에서 "UART RX 인터럽트가 받은 바이트를 main 루프로 넘기는 큐를 짜 보세요" 는 RUST-M06(ISR 공유 상태) 다음에 바로 나오는 질문이다. C 에서는 링 버퍼 + `volatile` 인덱스로 끝냈지만(C 판은 see:DS-A01 · DS-A02), Rust 에서는 두 가지를 더 증명해야 한다. 하나는 **메모리 순서**(어느 load 가 Acquire 이고 왜)이고, 다른 하나는 **"정말 생산자가 하나뿐인가"** 를 컴파일러가 보장하게 만드는 API 설계다. 이 큐는 `heapless::spsc` 로 실제 펌웨어(RTIC, Embassy 이전 세대 코드)에 많이 깔려 있어서, 면접관이 "heapless 는 왜 용량이 N-1 이죠?" 까지 묻기도 한다.

상황: STM32F4(Cortex-M4F) 보드. `USART2_IRQHandler` 가 바이트를 받을 때마다 큐에 넣고, main 루프가 꺼내서 줄을 조립한다. ISR 은 **절대 기다리면 안 된다**(main 보다 우선순위가 높아서, ISR 이 기다리면 main 이 큐를 비울 기회가 없다). 그래서 가득 차면 버리고 센다. 같은 코드를 Cortex-M0+(STM32G0, RP2040 한 코어) 에도 쓰고 싶다. M0+ 에는 CAS 가 없다(RUST-M06 에서 `fetch_add` 가 E0599 로 막힌 것을 봤다).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">Queue&lt;u8, 8&gt;: free-running head = 10, tail = 14 → 슬롯 = 인덱스 &amp; 7</text>
  <rect x="10" y="40" width="120" height="50" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="22" y="62" fill="currentColor">USART2 ISR</text>
  <text x="22" y="80" fill="currentColor" font-family="monospace" font-size="12">Producer&lt;'a&gt;</text>
  <rect x="610" y="40" width="120" height="50" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="622" y="62" fill="currentColor">main 루프</text>
  <text x="622" y="80" fill="currentColor" font-family="monospace" font-size="12">Consumer&lt;'a&gt;</text>
  <g stroke="currentColor">
    <rect x="170" y="130" width="50" height="40" class="f-mut"/>
    <rect x="220" y="130" width="50" height="40" class="f-mut"/>
    <rect x="270" y="130" width="50" height="40" class="f-ok"/>
    <rect x="320" y="130" width="50" height="40" class="f-ok"/>
    <rect x="370" y="130" width="50" height="40" class="f-ok"/>
    <rect x="420" y="130" width="50" height="40" class="f-ok"/>
    <rect x="470" y="130" width="50" height="40" class="f-mut"/>
    <rect x="520" y="130" width="50" height="40" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="190" y="186">0</text><text x="240" y="186">1</text><text x="290" y="186">2</text><text x="340" y="186">3</text>
    <text x="390" y="186">4</text><text x="440" y="186">5</text><text x="490" y="186">6</text><text x="540" y="186">7</text>
    <text x="282" y="155">'a'</text><text x="332" y="155">'b'</text><text x="382" y="155">'c'</text><text x="430" y="155">'\n'</text>
    <text x="178" y="155">uninit</text><text x="228" y="155">uninit</text><text x="478" y="155">uninit</text><text x="528" y="155">uninit</text>
  </g>
  <line x1="295" y1="110" x2="295" y2="128" stroke="currentColor"/>
  <text x="250" y="106" fill="currentColor" font-size="12">head=10 (main 만 씀)</text>
  <line x1="495" y1="110" x2="495" y2="128" stroke="currentColor"/>
  <text x="455" y="106" fill="currentColor" font-size="12">tail=14 (ISR 만 씀)</text>
  <line x1="130" y1="65" x2="495" y2="100" stroke="currentColor" stroke-dasharray="4 3"/>
  <line x1="610" y1="65" x2="295" y2="100" stroke="currentColor" stroke-dasharray="4 3"/>
  <rect x="170" y="205" width="16" height="12" class="f-ok" stroke="currentColor"/>
  <text x="192" y="216" fill="currentColor" font-size="12">head..tail: 초기화됨, 소비자 소유</text>
  <rect x="420" y="205" width="16" height="12" class="f-mut" stroke="currentColor"/>
  <text x="442" y="216" fill="currentColor" font-size="12">나머지: 미초기화, 생산자 소유</text>
  <text x="10" y="248" fill="currentColor" font-family="monospace" font-size="12">len = tail.wrapping_sub(head) = 4   full: len == N   empty: head == tail</text>
  <text x="10" y="270" fill="currentColor" font-family="monospace" font-size="12">let (p, c) = q.split();   // &amp;'a mut Queue → 두 끝점, 'a 동안 q 는 다른 데서 못 씀</text>
  <text x="10" y="292" fill="currentColor" font-size="12">인덱스를 N 으로 접지 않고 계속 올린다 → 슬롯 하나도 안 버림 (heapless 0.9 는 % N 으로 접고 N-1 개)</text>
</svg>
```

**호스트 모델.** 맥에는 인터럽트가 없으니 테스트가 띄우는 `std::thread`(주로 `thread::scope`)를 ISR 로 삼는다. 진짜 병렬이라 단일 코어 ISR 보다 **더 가혹하다**(ISR 은 main 을 선점할 뿐 동시에 돌지 않는다). 라이브러리는 `#![no_std]` 이고, `RUSTFLAGS="--cfg loom"` 이면 내부 `sync` 모듈이 loom 의 `AtomicUsize`/`UnsafeCell` 로 바뀐다(loom 문서가 권하는 방식).

### Part A — enqueue / dequeue 와 네 개의 Ordering

- `Producer::enqueue(&mut self, v: T) -> Result<(), T>`: 가득 차면 값을 `Err(v)` 로 **그대로 돌려준다**(ISR 이 버릴지 셀지 정한다).
- `Consumer::dequeue(&mut self) -> Option<T>`: 비었으면 `None`.
- 인덱스는 free-running(`wrapping_add`), 슬롯은 `index & (N-1)`. `N` 이 2의 거듭제곱이 아니면 **컴파일 오류**(`const { assert!(..) }`).
- 각 load/store 에 Ordering 을 고르고, 주석으로 "누구의 무엇과 짝인지" 를 쓴다.

### Part B — 소유권: split, Drop, peek, Send/Sync

- `split(&mut self)` 는 주어진다. 왜 두 번 split 이 안 되는지, Producer 를 왜 Clone 하면 안 되는지 실제 오류로 확인한다(`demo_types`, 문서 테스트 4개).
- `Drop for Queue`: head..tail 의 항목만 **정확히 한 번** drop. 인덱스가 `usize::MAX` 를 넘어가는 경우 포함.
- `Consumer::peek(&self) -> Option<&T>`, `len()` 세 개(큐·생산자·소비자 시점).
- `unsafe impl<T: Send, const N: usize> Sync for Queue<T, N>` 의 SAFETY 주석을 읽고 설명할 수 있어야 한다.

### Part C — 스레드 ISR 경쟁 테스트 · loom · heapless · 크로스 빌드

- 스레드 ISR → main: 200,000개 메시지가 순서대로, 빠짐·중복 없이, 4워드 페이로드가 찢어지지 않고 온다. `N = 2` 극한 경합, `Box` 페이로드 + 남은 항목 해제 개수 일치.
- `tests/loom_spsc.rs`: loom 이 모든 인터리빙을 돌려도 통과. `demo_loom` 에서 Ordering 을 하나씩 `Relaxed` 로 약화하면 loom 이 잡는가?
- `heapless::spsc::Queue<u8, 8>` 은 7개, 우리 것은 8개 — 테스트로 확인하고 소스를 읽어 이유를 댄다.
- `cargo build --target thumbv7em-none-eabihf` / `thumbv6m-none-eabi` 둘 다 성공 + asm 에 `dmb` 가 어디 있는지.

## 2. Problem (English)

> **Lock-free single-producer/single-consumer queue for ISR → main, with the SPSC property enforced by types.**
>
> `#![no_std]` library. `Queue<T, const N: usize>` holds `head: AtomicUsize`, `tail: AtomicUsize` (free-running, `N` a power of two — reject others at compile time) and `buf: [UnsafeCell<MaybeUninit<T>>; N]`. Capacity is `N` (no wasted slot). `Queue::new()` is a `const fn` so it can live in a `static`.
>
> - `split(&mut self) -> (Producer<'_, T, N>, Consumer<'_, T, N>)`.
> - `Producer::enqueue(&mut self, v: T) -> Result<(), T>` — returns the value back when full; never blocks.
> - `Consumer::dequeue(&mut self) -> Option<T>`, `Consumer::peek(&self) -> Option<&T>`.
> - `len()` on the queue and on both endpoints. `Drop` releases exactly the items still queued.
> - Justify every `Ordering` and the `unsafe impl Sync`.
>
> Example: `N = 4`: four `enqueue`s succeed, the fifth returns `Err(v)`; after one `dequeue` one more `enqueue` succeeds. Starting at `head = tail = usize::MAX - 2` the queue still works across the wrap.
>
> Follow-ups: Show with loom which `Relaxed` weakening breaks it. Does loom catch all of them? Why is `heapless::spsc::Queue<T, 8>` capacity 7? Why does this build for `thumbv6m-none-eabi` when `AtomicUsize::fetch_add` doesn't exist there? Is this queue lock-free or wait-free?

## 3. 시작 코드

<!-- code: starter/src/lib.rs -->

<!-- code: starter/Cargo.toml -->

```bash
# rustup PATH (Homebrew rustup) + 맥 SDK 링크 오류 회피 + target/ 을 레슨 폴더 밖에
export PATH=/opt/homebrew/opt/rustup/bin:$PATH
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
export CARGO_TARGET_DIR=/tmp/RUST-M07_target
cd lessons/RUST/RUST-M07/starter
cargo test                                   # 처음: "test result: FAILED. 2 passed; 14 failed; … finished in 3.00s" (exit 101)
cargo clippy --all-targets -- -D warnings    # 경고 0
cargo build --target thumbv6m-none-eabi      # no_std 라이브러리가 M0+ 로도 빌드되는지
RUSTFLAGS="--cfg loom" cargo test --release --test loom_spsc   # starter: "Model exceeded maximum number of branches" 로 FAIL
```

구성: `starter/`, `solution/`, `practice/` 는 **각각 독립된 작은 라이브러리 크레이트** 다(`Cargo.lock` 포함). starter 와 solution 은 라이브러리 이름이 둘 다 `spsc_m07` 이라 문서 테스트·`tests/loom_spsc.rs` 가 글자 그대로 같다. `src/lib.rs` 맨 아래 `mod tests` 가 하네스이고, 처음부터 통과하는 2개는 빈 큐 `dequeue`(a1)와 Send/Sync 정적 확인(b6)이다. 스레드 테스트는 모든 대기 루프에 3초 `DEADLINE` 이 있어서 TODO 스텁(`Err(v)` / `None` 만 돌려줌)에서도 **멈추지 않고** 3초 뒤 FAIL 한다. starter 의 `#![allow(dead_code, unused_imports)]` 는 스텁이 필드를 아직 안 쓰는 경고를 끄려는 것이다. solution 은 그 줄 없이도 clippy 가 깨끗했다.

관찰용 크레이트 네 개 (하네스와 별개, 모두 `solution` 을 path 의존성으로 쓴다):

```bash
(cd demo_types && cargo run)                                  # 정상 사용: "main got [0, 1, 2, 3, 4]"
(cd demo_types && cargo build --features split-twice)         # E0499  (clone-producer / shared-producer / outlive / rc-to-isr 도)
(cd demo_loom && RUSTFLAGS="--cfg loom" cargo test --release --features weak-tail)   # loom 이 잡나?
(cd demo_targets && cargo rustc --release --lib --target thumbv7em-none-eabihf -- --emit asm)
(cd demo_bench && cargo run --release)                        # 2 스레드 처리량 (약 15–20 초)
```

## 4. 힌트

<details><summary>힌트 1 — 자기 인덱스는 Relaxed 로 읽어도 된다</summary>

tail 을 쓰는 스레드는 생산자 하나뿐이다. 한 스레드는 자기가 마지막으로 쓴 값을 항상 본다(같은 원자 변수에 대한 coherence). 그래서 생산자의 `tail.load` 는 `Relaxed` 로 충분하다. 소비자의 `head.load` 도 같다. 순서가 필요한 것은 **상대 인덱스** 를 읽을 때와 **내 인덱스를 게시할** 때다.
</details>

<details><summary>힌트 2 — "데이터 먼저, 인덱스 나중" 을 양쪽에</summary>

생산자는 슬롯에 쓴 **다음에** tail 을 올려야 한다(`Release`). 소비자는 tail 을 `Acquire` 로 읽은 **다음에** 슬롯을 읽는다. 반대 방향도 똑같다. 소비자가 슬롯을 다 읽은 **다음에** head 를 `Release` 로 올리고, 생산자는 head 를 `Acquire` 로 읽은 **다음에** 그 슬롯을 덮어쓴다. 두 번째 짝을 잊는 사람이 많다.
</details>

<details><summary>힌트 3 — 가득 참 / 빔 판정</summary>

`t.wrapping_sub(h) == N` 이면 가득 참, `h == t` 이면 빔. `usize` 는 2^64 로 감기고 N 이 2^64 의 약수(2의 거듭제곱)라서, 감겨도 `index & (N-1)` 이 연속이다. 슬롯 접근: `q.buf[t & Queue::<T, N>::MASK].with_mut(|p| unsafe { p.write(MaybeUninit::new(v)) })`.
</details>

<details><summary>힌트 4 — Drop 과 peek</summary>

Drop 은 `&mut self` 라 다른 스레드가 없다. head 부터 tail 까지 `wrapping_add` 로 돌며 `assume_init_drop()` 을 부른다. `while i != t` 로 돌아야 `usize::MAX` 를 넘어도 맞다(`for i in h..t` 는 h > t 면 0번 돈다). peek 은 dequeue 와 같은 검사를 하고 `assume_init_ref()` 를 돌려주되 head 는 그대로 둔다.
</details>

## 5. 풀이

### 0. 모델: Cortex-M ↔ 이 레슨의 호스트 모델

| | 칩 (STM32F4) | 호스트 테스트 | loom |
|---|---|---|---|
| 생산자 | `USART2_IRQHandler` 가 `Producer<'static>` 을 씀 | `thread::scope` 의 스레드 | `loom::thread::spawn` (thread #1) |
| 소비자 | main 루프 | 테스트 본문 (thread #0) | 모델 클로저 본문 (thread #0) |
| 동시성 | 선점만 (ISR 은 끝까지 실행) | 진짜 병렬 (12코어) | 모든 인터리빙 + C11 약한 메모리 일부 |
| atomic | `ldr`/`str` + `dmb sy` | `ldapr`/`stlr` (arm64) | 벡터 클록 + store 이력 |

`sync` 모듈 하나로 core 와 loom 을 바꿔 끼운다. 코드가 loom 판과 같은 모양이어야 해서 core 쪽에도 `with` / `with_mut` 가 있는 얇은 `UnsafeCell` 래퍼를 둔다. loom 의 `AtomicUsize::new` 는 `const fn` 이 아니라서 `#[cfg(loom)] pub fn new()` 를 따로 둔다.

### Part A — 네 개의 load/store 와 그 이유

| 위치 | 연산 | Ordering | 짝 / 이유 |
|---|---|---|---|
| 생산자 ① | `tail.load` | `Relaxed` | 내가 유일한 writer. 내 마지막 store 가 보인다 |
| 생산자 ② | `head.load` | `Acquire` | 소비자 ⑤ `head.store(Release)` 와 짝 → "그 슬롯 읽기가 끝났다" 를 받아 온다 (edge B) |
| 생산자 ⑤ | `tail.store` | `Release` | 슬롯 쓰기 ④ 를 이 store 앞에 묶어 게시 (edge A) |
| 소비자 ① | `head.load` | `Relaxed` | 내가 유일한 writer |
| 소비자 ② | `tail.load` | `Acquire` | 생산자 ⑤ 와 짝 → 슬롯 쓰기가 보인다 (edge A) |
| 소비자 ⑤ | `head.store` | `Release` | 슬롯 읽기 ④ 를 이 store 앞에 묶어 반납 (edge B) |

```svg
<svg viewBox="0 0 740 430" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs>
    <marker id="m7a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">happens-before: 두 개의 synchronizes-with 가 슬롯 소유권을 넘긴다</text>
  <text x="60" y="44" fill="currentColor" font-weight="bold">생산자 (ISR, thread #1)</text>
  <text x="470" y="44" fill="currentColor" font-weight="bold">소비자 (main, thread #0)</text>
  <g stroke="currentColor">
    <rect x="20" y="56" width="270" height="30" rx="5" class="f-mut"/>
    <rect x="20" y="102" width="270" height="30" rx="5" class="f-ok"/>
    <rect x="20" y="148" width="270" height="30" rx="5" class="f-acc"/>
    <rect x="440" y="194" width="280" height="30" rx="5" class="f-acc"/>
    <rect x="440" y="240" width="280" height="30" rx="5" class="f-ok"/>
    <rect x="440" y="286" width="280" height="30" rx="5" class="f-acc"/>
    <rect x="20" y="332" width="270" height="30" rx="5" class="f-acc"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="30" y="76">① t = tail.load(Relaxed)</text>
    <text x="30" y="122">④ buf[t&amp;7].write(v)   (비원자)</text>
    <text x="30" y="168">⑤ tail.store(t+1, Release)</text>
    <text x="450" y="214">② t = tail.load(Acquire) == t+1</text>
    <text x="450" y="260">④ v = buf[h&amp;7].read() (비원자)</text>
    <text x="450" y="306">⑤ head.store(h+1, Release)</text>
    <text x="30" y="352">② 다음 바퀴 head.load(Acquire)</text>
  </g>
  <line x1="155" y1="86" x2="155" y2="100" stroke="currentColor" marker-end="url(#m7a)"/>
  <line x1="155" y1="132" x2="155" y2="146" stroke="currentColor" marker-end="url(#m7a)"/>
  <line x1="580" y1="224" x2="580" y2="238" stroke="currentColor" marker-end="url(#m7a)"/>
  <line x1="580" y1="270" x2="580" y2="284" stroke="currentColor" marker-end="url(#m7a)"/>
  <line x1="290" y1="163" x2="438" y2="209" stroke="currentColor" stroke-width="2" marker-end="url(#m7a)"/>
  <text x="300" y="176" fill="currentColor" font-size="12">edge A: sync-with</text>
  <line x1="438" y1="301" x2="292" y2="347" stroke="currentColor" stroke-width="2" marker-end="url(#m7a)"/>
  <text x="300" y="312" fill="currentColor" font-size="12">edge B: sync-with</text>
  <text x="20" y="386" fill="currentColor" font-size="12">edge A 가 없으면: 소비자 ④읽기 ∥ 생산자 ④쓰기 = 데이터 레이스 (loom: weak-tail*)</text>
  <text x="20" y="404" fill="currentColor" font-size="12">edge B 가 없으면: 생산자 덮어쓰기 ∥ 소비자 ④읽기 = 데이터 레이스 (loom: weak-head)</text>
  <text x="20" y="422" fill="currentColor" font-size="12">세로 화살표 = 같은 스레드 안의 sequenced-before, 굵은 화살표 = Release → Acquire</text>
</svg>
```

정리하면 **슬롯의 소유권이 두 번 넘어간다.** 생산자 → 소비자는 tail 로(edge A), 소비자 → 생산자는 head 로(edge B). Rust 의 `Acquire`/`Release` 는 C11/C++ 의 `memory_order_acquire/release` 와 같은 모델이다(Rust 문서 `core::sync::atomic` 는 C++20 모델을 따른다고 적는다). 메시지 패싱 일반론은 see:C-A03 · OS-M08, 펜스/릴리스 시퀀스는 see:MCPP-M13, C11 판 SPSC 의 정답 논증은 see:DS-A01 에 있다. 여기서는 Rust 쪽 특유의 것만 본다.

**왜 free-running + 2의 거듭제곱인가.** `t.wrapping_sub(h)` 는 감겨도 정확한 개수다(mod 2^64 산술). 슬롯 번호 `t & (N-1)` 이 감김 전후로 연속이려면 N 이 2^64 의 약수여야 한다. N = 3 이면 `usize::MAX % 3 == 0` 이고 다음 인덱스 `0 % 3 == 0` 이라 **같은 슬롯을 연달아 두 번 쓴다**(7. 자주 하는 실수). 테스트 `a5` 는 `with_start_index(usize::MAX - 2)` 로 시작해서 세 바퀴 동안 감김을 지나간다. 장점은 "가득 참" 과 "빔" 을 구별하려고 슬롯을 하나 비워 둘 필요가 없다는 것이다.

### Part B — split 과 타입: 실제 컴파일 오류

`split(&mut self)` 는 `&'a mut Queue` 를 `&'a Queue` 두 개로 바꿔 Producer·Consumer 에 하나씩 넣는다. 이 둘이 사는 동안(`'a`) 원래의 `&mut` 빌림이 계속 살아 있다. 그래서 컴파일러가 다음을 막는다. `demo_types` 의 실제 출력이다(경고 줄 생략).

```text
$ cargo build --features split-twice
error[E0499]: cannot borrow `q` as mutable more than once at a time
  --> src/main.rs:10:25
 7 |     let (mut p, mut c) = q.split();
   |                          - first mutable borrow occurs here
10 |     let (mut p2, _c2) = q.split(); // 두 번째 생산자를 만들려는 시도
   |                         ^ second mutable borrow occurs here
31 |                 p.enqueue(b).unwrap();
   |                 - first borrow later captured here by closure

$ cargo build --features clone-producer
error[E0599]: no method named `clone` found for struct `Producer<'a, T, N>` in the current scope

$ cargo build --features shared-producer        # let shared = &p; 두 스레드가 shared.enqueue(..)
error[E0596]: cannot borrow `*shared` as mutable, as it is behind a `&` reference
error[E0524]: two closures require unique access to `*shared` at the same time

$ cargo build --features outlive                # 블록 안 지역 큐에서 split().0 을 꺼내 밖으로
error[E0597]: `local` does not live long enough

$ cargo build --features rc-to-isr              # Queue<Rc<u8>, 4> 의 Producer 를 스레드로
error[E0277]: `Rc<u8>` cannot be sent between threads safely
    = note: required for `Queue<Rc<u8>, 4>` to implement `Sync`
    = note: required for `&Queue<Rc<u8>, 4>` to implement `Send`
note: required because it appears within the type `Producer<'_, Rc<u8>, 4>`
```

여기서 읽을 것:

- **E0499**: 생산자를 둘 만들 수 없다. "단일 생산자" 가 런타임 검사가 아니라 빌림 규칙이다.
- **E0599**: Producer 에 `Clone` 을 구현하지 않는 것 자체가 설계다. `#[derive(Clone)]` 한 줄이 SPSC 를 MPSC 로 바꿔 버린다(그러면 tail 을 두 스레드가 쓰게 되어 깨진다).
- **E0596**: Producer 는 `&Queue` 만 들고 있으니 **자동으로 `Sync`** 다. 그래도 안전한 이유는 `enqueue` 가 `&mut self` 이기 때문이다. `&Producer` 를 여러 스레드가 나눠 가져도 enqueue 는 못 부른다. 만약 `enqueue(&self)` 로 만들었다면 이 오류 없이 컴파일되고 생산자가 둘이 된다. heapless 도 `enqueue(&mut self)` 다.
- **E0597**: 큐보다 오래 사는 Producer 는 없다(use-after-free 방지). 펌웨어에서 ISR 에 넘기려면 큐가 `'static` 이어야 한다 → 연습 3 의 `StaticQueue`, 또는 heapless 문서 예제처럼 `split_const` 로 `static` 초기화 안에서 나눈 뒤 RUST-M06 의 `Mutex<RefCell<Option<Producer>>>` 에 넣는다.
- **E0277**: 오류 체인이 `unsafe impl<T: Send> Sync for Queue` 의 바운드를 그대로 보여 준다.

같은 네 가지(E0499 · E0599 · E0597 · E0277)는 `solution/src/lib.rs` 의 `compile_fail,E0xxx` 문서 테스트로도 박혀 있다. `cargo test` 의 "Doc-tests … 4 passed" 가 그것이다. rustdoc 은 오류 코드까지 맞는지 확인한다.

**`unsafe impl Sync` 의 근거** (코드의 SAFETY 주석 요약):

1. `&Queue` 로 할 수 있는 일은 Producer/Consumer 를 통한 접근뿐이다. 각각 하나씩이고(split 이 `&mut`), 메서드가 `&mut self` 다. 그래서 tail 의 writer 도 하나, head 의 writer 도 하나다.
2. 슬롯 i 는 `head ≤ i < tail` 이면 소비자, 아니면 생산자 소유다. 소유권은 edge A/B 로만 넘어간다. 그래서 같은 슬롯을 동시에 만지는 일이 없다.
3. `T` 값은 스레드 사이를 **이동** 하고 공유되지 않는다. 그래서 `T: Send` 면 충분하고 `T: Sync` 는 필요 없다. 예를 들어 `Cell<u8>` 은 Send 이지만 Sync 가 아닌데, 이 큐로 보내도 된다. `peek` 이 `&T` 를 주지만 그것은 소비자 스레드 안에서만 쓰인다.

`Send` 는 자동이다. `AtomicUsize` 와 `UnsafeCell<MaybeUninit<T>>` 는 `T: Send` 면 Send 다. Producer/Consumer 는 `&Queue` 를 들고 있으므로 `Queue: Sync` 이면 자동으로 Send 다. 그래서 heapless 처럼 `unsafe impl Send for Producer` 를 따로 쓸 필요가 없다. heapless 0.9.3 의 spsc.rs 에는 큐에 대한 `Sync` impl 이 없고(grep 결과 0건), 대신 끝점에 `unsafe impl<T> Send for Producer<'_, T> where T: Send` 를 직접 단다(spsc.rs:744, 753). 큐 자체를 공유하지 않고 끝점만 보내는 설계다. 결과는 같다.

**Drop.** `MaybeUninit<T>` 는 drop 을 하지 않으므로 직접 정리해야 한다. 테스트 `b1` 은 5개 넣고 1개 꺼내 버린 뒤 큐를 drop 해서 총 5회를 확인한다. `b2` 는 `usize::MAX - 1` 에서 시작해 남은 항목이 감김 경계에 걸친 경우를, `b3` 는 `Err(v)` 로 돌려준 값이 drop 되지 않음을 확인한다. `c3` 는 스레드 사이로 `Box` 를 20,000개 보내고 15,000개만 꺼낸 뒤, drop 횟수가 15,000 → 20,000 이 되는지 센다.

**peek 의 수명.** `peek(&self) -> Option<&T>` 의 `&T` 는 `&self` 에 묶인다. 그것이 살아 있는 동안 `dequeue(&mut self)` 는 컴파일되지 않으니 head 가 안 움직이고, 생산자는 그 슬롯을 덮어쓸 수 없다. 보호 장치가 런타임 락이 아니라 빌림 검사다.

### Part C-1 — loom: 정답은 통과, 약화하면?

`solution/tests/loom_spsc.rs` 는 두 시나리오를 돈다. N=2 에 3개를 보내 full 경계와 슬롯 재사용을 반드시 지나게 하는 것, 그리고 String 페이로드에 "가득 차면 버리는 ISR" 이다. 실제 결과(`LOOM_LOG=info`):

```text
$ RUSTFLAGS="--cfg loom" cargo test --release --test loom_spsc
INFO loom::model: Completed in 69 iterations       ← loom_string_payload_isr_drops_when_full
INFO loom::model: Completed in 6096 iterations     ← loom_three_items_through_two_slots
test result: ok. 2 passed; 0 failed; … finished in 0.45s
```

`demo_loom` 은 같은 큐를 u64 전용으로 줄이고, 네 Ordering 을 feature 로 바꾼다. 같은 "3개를 2칸으로" 시나리오를 돌린 실제 결과(`LOOM_LOCATION=1`; lib.rs:64 = 생산자 슬롯 쓰기, :78 = 소비자 슬롯 읽기):

| feature | 바꾼 것 | 슬롯 | loom 결과 |
|---|---|---|---|
| (없음) | 정답 | `UnsafeCell<u64>` | ok, 6096 iterations |
| `weak-tail` | P⑤ store · C② load → Relaxed | UnsafeCell | **FAIL** `Causality violation: Concurrent read and write accesses.` read: thread #0 @ lib.rs:78, write: thread #1 @ lib.rs:64 |
| `weak-tail-store` | P⑤ store 만 Relaxed | UnsafeCell | **FAIL** (같은 메시지·위치) |
| `weak-tail-load` | C② load 만 Relaxed | UnsafeCell | **FAIL** (같은 메시지·위치) |
| `weak-head` | C⑤ store · P② load → Relaxed | UnsafeCell | **FAIL** `… Concurrent read and write accesses to UnsafeCell.` (같은 위치: 소비자 읽기 ∥ 생산자 덮어쓰기) |
| `atomic-slots` | 정답, 슬롯 = `AtomicU64` Relaxed | Atomic | ok, 6096 iterations |
| `atomic-slots,weak-tail` | tail 약화 | Atomic | **FAIL** `assertion failed  left: [1, 2, 1]  right: [1, 2, 3]` |
| `atomic-slots,weak-head` | head 약화 | Atomic | **ok — loom 이 못 잡음** (6096 iterations) |

읽는 법:

- **UnsafeCell 슬롯**: loom 은 `UnsafeCell::with/with_mut` 접근마다 벡터 클록으로 happens-before 를 검사한다. 짝 하나(Release **또는** Acquire)만 빠져도 edge 가 없어져 "동시 읽기/쓰기" 로 잡힌다. 값이 우연히 맞아도 잡는다. C11 에서 이것은 데이터 레이스, 즉 UB 다.
- **`[1, 2, 1]`**: 슬롯이 atomic 이면 레이스는 아니지만 값이 틀릴 수 있다. 소비자가 Relaxed 로 tail=3 을 봤어도 슬롯 0 의 세 번째 쓰기(3)와는 happens-before 가 없다. 그래서 loom 이 store 이력에서 옛 값 1 을 돌려준 실행을 찾았다. loom 은 원자 변수마다 최근 store 7개를 들고 다니며(`MAX_ATOMIC_HISTORY = 7`, rt/mod.rs:65) 이런 **오래된 값 읽기** 를 모델링한다.
- **못 잡은 경우 `atomic-slots,weak-head`**: 버그가 되려면 소비자의 `slot.load` 가 생산자의 **나중** store(head 를 보고 나서 쓴 값)를 읽어야 한다. 이것은 load buffering(LB) 모양이고 C11 은 Relaxed 에서 이 결과를 허용한다. loom 문서(lib.rs "Relaxed Memory Ordering")는 "한 스레드 안에서 B 가 A 보다 먼저 실행되는 재배치는 loom 이 흉내 낼 수 없다" 고 적는다. 즉 loom 은 이미 실행된 store 중에서만 값을 고른다. 그래서 **loom 이 통과했다고 Relaxed 가 맞다는 뜻은 아니다.** 실제 ARMv8 하드웨어는 이 특정 모양(load → 제어 의존 → store)을 막는 것으로 알고 있다(Arm 메모리 모델의 dependency 규칙, 미검증). 하지만 언어 모델이 허용하는 이상 컴파일러가 깨도 할 말이 없다.

loom 의 다른 한계도 실제로 걸린다. 스레드는 main 포함 최대 5개(`MAX_THREADS = 5`, rt/mod.rs:62)다. loom 타입으로 바꾼 코드만 보인다(std 의 Arc 를 쓰면 그 부분은 안 보임). 스핀 루프에는 `thread::yield_now()` 가 필요하다. starter 처럼 영원히 `Err` 를 받는 루프는 "Model exceeded maximum number of branches" 로 끝난다(실제 확인). 모델이 커지면 `LOOM_MAX_PREEMPTIONS=2~3` 같은 경계를 둔다.

### Part C-2 — heapless::spsc 와 비교 (heapless 0.9.3 소스)

`~/.cargo/registry/src/index.crates.io-*/heapless-0.9.3/src/spsc.rs` 를 직접 읽었다.

| | 이 레슨 `Queue<T, N>` | `heapless::spsc::Queue<T, N>` 0.9.3 |
|---|---|---|
| 용량 | N | **N-1** (`capacity()` = `self.n() - 1`, spsc.rs:195) |
| 인덱스 | free-running, `& (N-1)` | `increment = (val + 1) % self.n()` (spsc.rs:184) — 접힌 인덱스 |
| 가득 참 | `t - h == N` | `next_tail == head` (spsc.rs:288) → 한 칸을 비워 둬야 빔과 구별 |
| N 제약 | 2의 거듭제곱 (컴파일 오류) | `N > 1` (const assert). 2의 거듭제곱 권장(문서 warning, `%` 비용) |
| Ordering | Relaxed 내 것 / Acquire 상대 / Release 게시 | **같다**: enqueue spsc.rs:285 `Relaxed`, :288 `Acquire`, :292 `Release`. dequeue :327 `Relaxed`, :329 `Acquire`, :335 `Release` |
| `len()` | 끝점마다 상대 인덱스 Acquire | 큐의 `len()` 은 둘 다 Relaxed (spsc.rs:202–203) |
| 끝점 타입 | `Producer<'a, T, N>` | `Producer<'a, T>` — `QueueView<T>`(unsized) 참조라 N 이 타입에서 지워짐 |
| `'static` 얻기 | 연습 3 `StaticQueue` | `split_const` (const fn) — 문서 예제는 `static` 초기화 블록 안의 `static mut Q` + `#[allow(static_mut_refs)]` |
| M0+ | core 만으로 빌드 (load/store) | lib.rs:219–228: `target_has_atomic = "ptr"` **또는** build.rs 목록(`has_atomic_load_store`) 또는 `portable-atomic` 기능. 주석: "spsc requires load/store only, not CAS" |
| unchecked API | 없음 | `enqueue_unchecked` / `dequeue_unchecked` (unsafe) |

테스트 `c4` 가 같은 `N = 8` 에서 heapless 7개, 우리 것 8개를 확인한다. **N-1 은 버그가 아니라 설계 선택이다.** 접힌 인덱스는 감김 걱정이 없고 N 이 아무 값이나 되지만 한 칸을 쓴다. free-running 은 칸을 아끼지만 N 이 2의 거듭제곱이어야 한다. RAM 1 KB 짜리 MCU 에서 `Queue<[u8; 64], 4>` 라면 한 칸이 64 B 다.

### Part C-3 — 같은 코드, 다른 칩: 크로스 빌드와 asm

```text
$ cd solution && cargo build --target thumbv7em-none-eabihf   → Finished
$ cd solution && cargo build --target thumbv6m-none-eabi      → Finished
$ rustc --print cfg --target thumbv6m-none-eabi | grep atomic
target_has_atomic_primitive_alignment="16" / "32" / "8" / "ptr"     ← target_has_atomic 는 없음 (CAS 없음)
```

RUST-M06 에서는 같은 thumbv6m 빌드가 `fetch_add`/`swap` 때문에 E0599 10개로 실패했다. 이 큐는 **`load` 와 `store` 만** 쓰니 M0+ 에서도 그대로 빌드된다. SPSC 는 각 인덱스의 writer 가 하나라서 "읽고-바꾸고-쓰기" 가 필요 없다. 그래서 CAS 가 없는 코어에서 쓸 수 있는 거의 유일한 lock-free 자료구조다. `demo_targets` 가 `Producer<'static, u8, 64>::enqueue` 와 `dequeue` 를 구체화해 낸 실제 asm 이다(`cargo rustc --release --lib --target … -- --emit asm`, opt-level s, 디렉티브 생략).

```text
@ thumbv7em uart_isr_push (enqueue)    @ thumbv6m uart_isr_push            // aarch64-apple-darwin
  ldr   r0, [r0]                         ldr  r2, [r0]                       ldr   x8, [x0]
  ldr   r3, [r0, #68]   @ tail Relaxed   ldr  r3, [r2, #68]                  ldr   x10, [x8, #128]  // tail Relaxed
  ldr   r2, [r0, #64]   @ head           ldr  r0, [r2, #64]                  ldapr x9, [x8]         // head Acquire
  dmb   sy              @ ← Acquire      subs r0, r3, r0                     sub   x9, x10, x9
  sub.w r12, r3, r2                      dmb  sy                             cmp   x9, #64
  cmp.w r12, #64                         cmp  r0, #64                        b.eq  LBB8_2
  beq   .LBB8_2                          beq  .LBB8_2                        and   x11, x10, #0x3f
  and   r2, r3, #63                      movs r4, #63 / ands r4, r3          add   x11, x8, x11
  strb  r1, [r0, r2]    @ 슬롯 쓰기       strb r1, [r2, r4]                   strb  w1, [x11, #256]
  adds  r1, r3, #1                       adds r1, r3, #1                     add   x10, x10, #1
  dmb   sy              @ ← Release      dmb  sy                             stlur x10, [x8, #128]  // tail Release
  str   r1, [r0, #68]                    str  r1, [r2, #68]
```

```text
load_acquire:  thumbv7em/v6m  ldr r0, [r0] ; dmb sy        aarch64  ldapr x0, [x0]
store_release: thumbv7em/v6m  dmb sy ; str r1, [r0]        aarch64  stlr  x1, [x0]
load_relaxed / store_relaxed: 모든 타깃에서 dmb 없는 ldr / str
```

읽는 법:

- **Cortex-M**: Acquire load = `ldr` 다음 `dmb sy`, Release store = `dmb sy` 다음 `str`. M4 와 M0+ 가 같은 모양이다. 단일 코어에서 ISR ↔ main 만이라면 하드웨어적으로 `dmb` 는 사실 필요 없다(한 코어는 자기 프로그램 순서를 보고, 예외는 정밀하다). 컴파일러 재배치만 막으면 된다. LLVM 은 언어 모델을 보수적으로 지켜서 넣는다. 비용은 한 번에 몇 사이클이다. 굳이 빼려면 Relaxed + `compiler_fence` 를 쓰는데, 멀티코어(RP2040 두 코어, STM32H7 듀얼)로 옮기는 순간 틀린다. 측정 없이 하지 않는다.
- **레이아웃**: 칩에서는 `Padded` 가 정렬을 안 하니 head 가 +64, tail 이 +68, 버퍼가 +0(rustc 가 필드를 재배치함)이다. 호스트에서는 head +0, tail +128, 버퍼 +256 이다. 이 맥의 `hw.cachelinesize = 128` 에 맞춘 패딩이 실제로 적용됐다.
- **arm64 호스트**: Acquire load 가 `ldar` 가 아니라 **`ldapr`/`ldapur`**(FEAT_LRCPC, RCpc acquire)다. rustc 의 aarch64-apple-darwin 기본 CPU 가 이것을 켠다. 이 큐에는 store→load 순서(SC)가 필요 없어서 RCpc 로 충분하다(RCsc 와 RCpc 의 차이는 see:ARM-M05).
- **DMA 는 다른 문제다.** UART RX 를 DMA 로 받으면 쓰는 쪽이 CPU 가 아니다. 캐시가 있는 M7 이라면 atomics 가 아니라 캐시 무효화(`SCB_InvalidateDCache_by_Addr`)와 DMA 완료 플래그가 필요하다(see:C-M14). 이 큐는 "CPU 가 쓰는 ISR → CPU 가 읽는 main" 용이다.

### Part C-4 — 처리량: lock-free 는 "빠르다" 는 뜻이 아니다

`demo_bench`: 생산자 1 + 소비자 1 스레드, u64 1,000만 개, 용량 약 1024, 바쁜 대기, 6종을 라운드 로빈으로 7바퀴 돌린 중앙값이다. 기계: Apple M6 (Super 2 + Performance 4 + Efficiency 6), rustc 1.99.1, `--release`. 다른 레슨 작성 세션이 동시에 벤치마크를 돌리고 있었고 코어 고정도 불가하다. **절대값이 아니라 배수만 보라.**

| 구현 (Mops/s 중앙값, min–max) | 실행 1 (load 6.50 → 6.26) | 실행 2 (load 6.26 → 6.56) |
|---|---|---|
| ours `Queue<u64, 1024>` | 20.6 (15.0–27.4) | 19.6 (15.9–26.1) |
| heapless N=1025 (용량 1024) | 18.4 (17.0–19.5) | 17.4 (16.3–21.3) |
| heapless N=1024 (용량 1023) | 18.0 (17.5–22.0) | 17.2 (15.9–22.0) |
| `Mutex<VecDeque>` | 20.7 (5.5–24.0) | 23.4 (14.0–25.7) |
| `mpsc::sync_channel(1024)` | 87.6 (61.3–135.1) | 121.5 (64.7–146.5) |
| cached (ours + 상대 인덱스 캐시, 가설 확인용) | 62.0 (43.8–92.7) | 53.1 (50.9–89.4) |

```svg
<svg viewBox="0 0 740 268" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">2 스레드 SPSC 처리량 (실행 2 중앙값, Mops/s — 배수만 볼 것)</text>
  <g fill="currentColor" font-size="12">
    <text x="10" y="52">ours (N=1024)</text>
    <text x="10" y="82">heapless N=1025</text>
    <text x="10" y="112">heapless N=1024</text>
    <text x="10" y="142">Mutex&lt;VecDeque&gt;</text>
    <text x="10" y="172">std sync_channel</text>
    <text x="10" y="202">cached (가설 확인)</text>
  </g>
  <g stroke="currentColor">
    <rect x="200" y="40" width="68" height="18" class="f-acc"/>
    <rect x="200" y="70" width="60" height="18" class="f-mut"/>
    <rect x="200" y="100" width="60" height="18" class="f-mut"/>
    <rect x="200" y="130" width="81" height="18" class="f-warn"/>
    <rect x="200" y="160" width="421" height="18" class="f-ok"/>
    <rect x="200" y="190" width="184" height="18" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="274" y="54">19.6</text>
    <text x="266" y="84">17.4</text>
    <text x="266" y="114">17.2</text>
    <text x="287" y="144">23.4</text>
    <text x="627" y="174">121.5</text>
    <text x="390" y="204">53.1</text>
  </g>
  <line x1="200" y1="32" x2="200" y2="214" stroke="currentColor"/>
  <text x="10" y="238" fill="currentColor" font-size="12">막대 길이 = 값 × 3.46 px. 다른 세션 벤치마크와 동시 실행 (load ≈ 6.5)</text>
  <text x="10" y="256" fill="currentColor" font-size="12">코어 고정 불가 → 같은 구현도 실행마다 ±30% 흔들린다 (표의 min–max)</text>
</svg>
```

정직한 결론:

- **이 레슨의 큐(그리고 heapless)는 2 스레드 처리량이 `Mutex<VecDeque>` 와 비슷하고, std `sync_channel` 보다 4–6배 느렸다.** 그래도 lock-free 로 만드는 이유는 처리량이 아니다. **ISR 은 뮤텍스를 잡을 수 없다**(단일 코어에서 main 이 잡은 채로 ISR 이 오면 영원히 못 푼다). 그리고 enqueue/dequeue 가 유한한 단계에 끝난다(Q&A Q8: wait-free).
- 느린 원인으로 가장 그럴듯한 가설은 이렇다. 매 메시지마다 생산자가 head 줄을, 소비자가 tail 줄을 읽는다. 상대가 그 줄을 계속 쓰고 있으니 메시지당 캐시 라인 이동이 몇 번씩 생긴다. 가설 확인용 `cached` 변형(막힐 때만 상대 인덱스를 다시 읽음)은 2.5–3배 빨라졌다. 원인 분해(패딩/캐시/배치/슬롯 플래그 단계별, p99 지연)는 **LLAT-M01** 이 하고, 캐시된 인덱스 전후 정식 측정은 **DS-A01** 이 한다. 여기서는 방향만 확인한다.
- std `mpsc` 는 Rust 1.67 부터 crossbeam-channel 기반으로 알고 있다(문서·릴리스 노트 기준, 소스 미확인). bounded 채널은 슬롯마다 stamp 를 두는 방식(see:DS-A03 의 Vyukov 큐와 같은 계열)에 백오프까지 있어서, 상대 인덱스를 매번 읽지 않는다. 그래서 "MPMC 인데 더 빠르다" 가 모순이 아니다.

### 어느 것을 언제

| 상황 | 고를 것 |
|---|---|
| ISR → main, 단일 코어 MCU (M0+ 포함) | 이 큐 / `heapless::spsc` — ISR 은 Err 면 버리고 센다 |
| 생산자가 여럿 (ISR 두 개 → main) | SPSC 를 생산자마다 하나씩 두거나, critical-section 으로 감싼 큐 (RUST-M06) 또는 MPMC (DS-A03) |
| 호스트 스레드 간, 처리량 중요 | `std::sync::mpsc` / crossbeam 부터 측정 |
| 큰 프레임 (복사 비싼 것) | 슬롯 안에서 읽기 `dequeue_with` (연습 4) 또는 버퍼 풀 + 인덱스만 큐로 |

## 6. 정답 코드

<!-- code: solution/src/lib.rs diff=starter/src/lib.rs -->

<!-- code: solution/tests/loom_spsc.rs -->

```bash
cd lessons/RUST/RUST-M07/solution          # rustup PATH · SDKROOT · CARGO_TARGET_DIR 는 3장과 같이
cargo test                                  # test result: ok. 16 passed; 0 failed  +  Doc-tests: 4 passed (compile_fail)
cargo test --release                        # 16 passed
RUSTFLAGS="--cfg loom" cargo test --release --test loom_spsc   # 2 passed (6096 / 69 iterations)
cargo clippy --all-targets -- -D warnings   # 경고 0 (loom cfg 에서도 0)
cargo build --target thumbv7em-none-eabihf  # OK
cargo build --target thumbv6m-none-eabi     # OK — load/store 만 쓰므로
```

검증 기록: `cargo test` 를 디버그로 7번, `--release` 1번 돌려 모두 16/16 통과했다(문서 테스트 4/4). loom 2/2 통과. starter 는 `2 passed; 14 failed` 로 3.00초 만에 끝났다(exit 101, 멈춤 없음). **C 레슨의 ASan/UBSan/TSan 에 해당하는 도구는 돌리지 못했다.** Miri 는 `cargo miri test` → "the 'miri' component … is not available for the 'stable-aarch64-apple-darwin' toolchain" 이고, `-Zsanitizer=thread` 도 nightly 전용이다. 그래서 `unsafe` 블록(슬롯 write/read/drop)의 근거는 ① SAFETY 주석의 불변식 ② loom 의 UnsafeCell 인과 검사(정답은 통과, 약화하면 정확히 그 줄을 지목) ③ 스레드 스트레스 + drop 개수 테스트, 이 세 겹에 기댄다. Miri 로 같은 테스트를 돌리는 것은 RUST-M08 의 몫이다.

## 7. 자주 하는 실수

**1. 소비자가 head 를 먼저 올리고 나서 슬롯을 읽는다.** "어차피 내 지역 변수로 읽을 건데" 하고 순서를 바꾸면, head 를 본 생산자가 아직 읽지 않은 슬롯을 덮어쓴다. loom `weak-head` 와 같은 레이스다.

```rust
// 틀림                                         // 맞음
q.head.0.store(h + 1, Ordering::Release);       let v = slot.with(|p| unsafe { (*p).assume_init_read() });
let v = slot.with(|p| unsafe { … });            q.head.0.store(h.wrapping_add(1), Ordering::Release);
```

**2. `enqueue(&self)`.** `&self` 로 만들면 편해 보인다. 그런데 Producer 는 자동 `Sync` 라서 `&Producer` 를 두 스레드(또는 ISR 두 개)가 쓸 수 있게 되고, 컴파일러가 아무 말도 하지 않는다. `&mut self` 로 두면 E0596 이 막아 준다.

**3. ISR 안에서 가득 차면 기다린다.** `while p.enqueue(b).is_err() {}` 를 ISR 에 쓰면, main 은 ISR 보다 우선순위가 낮아 큐를 비울 수 없다. 단일 코어에서 영원히 멈춘다. ISR 은 `Err(b)` 면 버리고 overrun 을 센다(연습 2). 테스트 스레드에서 기다리는 것은 진짜 병렬이라 괜찮을 뿐이다.

**4. N 이 2의 거듭제곱이 아닌데 `%` 로 free-running.** `N = 3` 이면 `usize::MAX % 3 == 0`, 다음 `0 % 3 == 0` 이라 감김 순간에 같은 슬롯을 두 번 쓴다(2^64 ≡ 1 mod 3). 접힌 인덱스(heapless 방식, N-1 용량)를 쓰거나, 이 레슨처럼 `const { assert!(N.is_power_of_two()) }` 로 막는다.

**5. Drop 을 `for i in 0..N` 으로.** 미초기화 슬롯을 `assume_init_drop` 하면 UB 다. 반대로 Drop 을 안 쓰면 `Box`/`String` 이 샌다. `head..tail` 만, `while i != t` 로(감김 대비) 돈다. `for i in h..t` 는 `h > t`(감김 후)면 0번 돌아서 조용히 샌다.

**6. "x86 에서는 Relaxed 로도 잘 돌았다".** x86(TSO)은 store→store, load→load 를 재배치하지 않으니 Relaxed tail 이 우연히 맞는다. ARM(이 맥 포함)과 언어 모델에서는 아니다. loom 이 `weak-tail-store` 하나만으로도 잡는다. 컴파일러도 Relaxed store 를 슬롯 쓰기 앞으로 옮길 자유가 있다.

**7. loom 이 통과했으니 Relaxed 여도 된다.** `atomic-slots,weak-head` 처럼 load buffering 이 필요한 버그는 loom 이 못 찾는다(실측). loom 통과는 "loom 이 모델링하는 범위에서 반례 없음" 일 뿐이다. 순서는 happens-before 논증으로 정하고, loom 은 확인용으로 쓴다.

## 8. 면접 Q&A

<details><summary>Q1. SPSC 링 버퍼에서 head/tail 에 각각 필요한 memory order 는?</summary>

생산자: 자기 tail 은 `Relaxed` load, 상대 head 는 `Acquire` load, 슬롯을 쓴 뒤 tail 을 `Release` store. 소비자: 자기 head 는 `Relaxed`, 상대 tail 은 `Acquire`, 슬롯을 읽은 뒤 head 를 `Release`. synchronizes-with 가 두 개다. tail 의 Release→Acquire 는 "데이터가 다 써졌다" 를, head 의 Release→Acquire 는 "슬롯을 다 읽었으니 다시 써도 된다" 를 넘긴다. **꼬리질문:** "SeqCst 로 다 하면?" → 맞긴 한데 필요 없다. store→load 순서(Dekker 모양)가 없으니 SC 가 줄 추가 보장을 아무도 쓰지 않는다. 비용은 실제로 생긴다. 같은 opt-level s 로 `-S` 해 보면 arm64 SeqCst load 는 `ldapr` 대신 `ldar`(store 는 둘 다 `stlr`)이고, thumbv7em SeqCst store 는 `dmb sy; str; dmb sy` 로 **뒤에 `dmb` 가 하나 더** 붙는다(Release 는 `dmb sy; str`).
</details>

<details><summary>Q2. 자기 인덱스를 Relaxed 로 읽어도 되는 근거는?</summary>

그 원자 변수의 writer 가 나 하나뿐이고, 한 스레드는 같은 원자 변수에 대해 자기의 sequenced-before 이전 store 보다 오래된 값을 읽을 수 없다(write-read coherence). 상대는 그 변수를 읽기만 하니 "더 새로운 값" 도 없다. heapless 0.9.3 도 똑같다(spsc.rs:285, :327). 더 나아가 생산자는 tail 을 지역 변수에 들고 있다가 아예 읽지 않을 수도 있다(연습/LLAT-M01 의 캐시 변형).
</details>

<details><summary>Q3. split(&mut self) 가 어떻게 "단일 생산자" 를 보장하나? Producer 가 Sync 인데 괜찮나?</summary>

`split` 이 `&'a mut Queue` 를 빌리고, 끝점 둘이 그 수명을 이어받는다. 그래서 끝점이 살아 있는 동안 두 번째 split 은 E0499 다. Producer 는 `Clone` 이 아니어서(E0599) 복제할 수도 없다. Producer 는 `&Queue` 만 들고 있으니 자동 `Sync` 다. 하지만 `enqueue` 가 `&mut self` 라 `&Producer` 로는 못 부른다(E0596 실측). 즉 "Sync 이지만 공유해 봐야 할 수 있는 게 len/ready 같은 읽기뿐" 이라 안전하다. **꼬리질문:** "런타임에 생산자를 바꾸려면?" → Producer 를 move 하면 된다. `Option<Producer>` 를 RUST-M06 의 `Mutex<RefCell<..>>` 에 넣고 ISR 이 `take()` 하는 것이 heapless 문서 예제다.
</details>

<details><summary>Q4. unsafe impl Sync for Queue 의 바운드가 왜 T: Send 이고 T: Sync 가 아닌가?</summary>

값이 스레드 사이를 **이동** 하기 때문이다(생산자 스레드에서 만들어져 소비자 스레드에서 drop 된다). 두 스레드가 같은 `&T` 를 동시에 보는 일은 없다. 슬롯은 한 번에 한쪽만 소유한다. 이동에 필요한 것은 Send 다. `Rc<u8>` 은 Send 가 아니라서 E0277 이 정확히 이 바운드를 짚는다("required for `Queue<Rc<u8>, 4>` to implement `Sync`"). 반대로 `Cell<T>` 는 Send 라서 보낼 수 있다. `std::sync::mpsc::Sender<T>` 가 `T: Send` 만 요구하는 것과 같은 논리다.
</details>

<details><summary>Q5. loom 으로 Relaxed 버그를 잡았나? loom 의 한계는?</summary>

잡았다. 슬롯이 `UnsafeCell` 이면 tail/head 짝 중 **하나만** 약화해도 `Causality violation: Concurrent read and write accesses` 가 나고, 위치(lib.rs:78 읽기 ∥ :64 쓰기)까지 지목한다. 슬롯이 Relaxed atomic 이면 tail 약화는 옛 값 읽기 `[1, 2, 1]` 로 잡는다. 하지만 head 약화는 **통과했다.** load buffering(나중 store 의 값을 먼저 읽기)을 loom 이 모델링하지 못한다(loom 문서가 명시). 다른 한계: 스레드 최대 5개(main 포함), loom 타입만 보임, 스핀 루프엔 `yield_now`, 상태 폭발 → `LOOM_MAX_PREEMPTIONS`. 그래서 증명은 happens-before 로 하고 loom 은 회귀 테스트로 쓴다. 더 완전한 모델 검사는 GenMC 같은 도구가 있다(미사용).
</details>

<details><summary>Q6. heapless::spsc 는 왜 용량이 N-1 인가?</summary>

0.9.3 은 인덱스를 `(val + 1) % n` 으로 접어 둔다(spsc.rs:184). 이러면 head == tail 이 "빔" 인지 "가득 참" 인지 구별할 수 없다. 그래서 `next_tail == head` 를 가득 참으로 정의해 한 칸을 비워 둔다(spsc.rs:288, `capacity() = n - 1`). 대가로 N 이 아무 값이나 된다(2의 거듭제곱이면 `%` 가 마스크로 최적화). 이 레슨은 free-running 인덱스로 N 칸을 다 쓰는 대신 N 을 2의 거듭제곱으로 제한했다. Ordering 은 둘이 같다. **꼬리질문:** "세 번째 방법은?" → 별도 `full` 플래그나 count 를 두는 것인데, 둘이 같이 쓰게 되어 SPSC 의 "변수마다 writer 하나" 가 깨진다(RMW 가 필요).
</details>

<details><summary>Q7. Cortex-M0+ 에는 CAS 가 없는데 이 큐가 왜 빌드되고 동작하나? asm 은?</summary>

SPSC 는 각 인덱스의 writer 가 하나라 RMW 가 필요 없고, 32비트 정렬 load/store 는 M0+ 에서도 원자적이다. Rust 는 thumbv6m 에서 `target_has_atomic` 을 끄지만(그래서 `fetch_add` 는 E0599, RUST-M06) `AtomicUsize::load/store` 는 남긴다. 실제 asm 은 Acquire = `ldr; dmb sy`, Release = `dmb sy; str` 이고 M4 와 같다. 단일 코어 ISR↔main 이면 `dmb` 는 하드웨어적으로 불필요하지만 무해하고, 듀얼 코어로 옮길 때 안전하다. heapless 도 lib.rs:222 주석에서 "spsc requires load/store only, not CAS" 라고 적는다.
</details>

<details><summary>Q8. 이 큐는 lock-free 인가 wait-free 인가? 그런데 왜 Mutex 큐와 처리량이 비슷하나?</summary>

**wait-free** 다. `enqueue`/`dequeue` 는 루프가 없고 상대가 무엇을 하든(멈춰 있어도) 유한한 단계 안에 `Ok`/`Err`/`Some`/`None` 으로 끝난다. lock-free 는 "누군가는 진행한다"(시스템 전체 진행), wait-free 는 "모두가 유한 단계에 끝난다"(각 스레드 진행)이고, wait-free ⊂ lock-free 다. 가득 차서 재시도하는 루프는 호출자가 고른 정책이지 자료구조의 성질이 아니다. 처리량은 progress 보장과 별개다. 매 메시지 상대 인덱스 줄을 읽는 캐시 라인 핑퐁 때문에 이 맥에서 ~20 Mops/s 로 `Mutex<VecDeque>` 와 비슷했고, 상대 인덱스를 캐시하면 2.5–3배, std `sync_channel` 은 4–6배였다(load ≈ 6.5, 배수만). 펌웨어에서 lock-free 를 쓰는 이유는 처리량이 아니라 "ISR 이 블록되지 않는다" 이다.
</details>

<details><summary>Q9. 호스트 스레드 테스트가 진짜 ISR 을 얼마나 대신하나?</summary>

순서·빠짐·중복·찢어짐·drop 개수 같은 불변식은 진짜 병렬이 더 가혹하게 검사한다(ISR 은 main 과 동시에 돌지 않는다). 대신하지 못하는 것: ISR 우선순위(ISR 이 기다리면 영원히 멈추는 것, 실수 3), 단일 코어에서 `dmb` 가 필요한가, 인터럽트 지연, 칩의 메모리 맵·캐시·DMA. 그래서 ① 호스트 스트레스 ② loom(인터리빙 + 약한 메모리 일부) ③ thumbv6m/v7em 크로스 빌드 + asm ④ 온타깃 테스트(probe-rs/defmt-test, RUST-A06)를 겹쳐 쓴다.
</details>

## 9. 추가 연습

`practice/` 크레이트는 solution 의 큐를 (loom shim 없이) 복사해 두고 그 위에 확장한다.

### 연습 1 — 배치: enqueue_slice / dequeue_into

`T: Copy` 일 때 `Producer::enqueue_slice(&mut self, &[T]) -> usize`(넣은 개수)와 `Consumer::dequeue_into(&mut self, &mut [T]) -> usize` 를 만들어라. 인덱스 게시(Release store)는 **배치당 한 번** 이어야 한다. UART DMA 반쪽 버퍼를 한 번에 넣는 상황이다.

<details><summary>답</summary>

`free = N - t.wrapping_sub(h)`, `k = free.min(items.len())`. k 개 슬롯을 쓴 뒤 `tail.store(t + k, Release)` 한 번이다. Release 하나가 앞의 k 개 쓰기를 모두 게시한다(그 store 앞의 모든 쓰기가 Acquire 쪽에 보인다). 소비자도 같다. 37개씩 넣고 50개씩 꺼내는 30만 개 스레드 테스트로 순서를 확인한다(N=256 과 서로소 크기라 감김 경계가 매번 다른 곳에 온다). 지연과 처리량의 트레이드오프(배치가 크면 첫 항목이 늦게 보임) 측정은 LLAT-M01.
</details>

### 연습 2 — UART RX: 가득 차면 버리고 센다

`UartIsr { p: Producer<u8, N>, overruns }` 의 `on_byte(b)` 는 절대 기다리지 않는다. `LineReader` 는 main 에서 바이트를 모아 `'\n'` 마다 줄을 낸다(32바이트 넘으면 자르고 `truncated`). 정확히 맞아야 하는 회계는 무엇인가?

<details><summary>답</summary>

`받은 바이트(rd.bytes) + overruns == 보낸 바이트` 다. 바이트 하나하나는 큐에 들어가 소비되거나 버려져 세어진다. 이것은 스레드 타이밍과 무관하게 **매번** 성립하므로 CHECK 할 수 있다. 몇 줄이 온전히 왔는지는 타이밍에 따라 달라지니 CHECK 하지 않는다. 결정적 테스트: main 이 안 읽는 동안 `"hello\nworld"`(11바이트)를 N=8 에 넣으면 overruns == 3, 첫 줄 "hello", 나머지 "wo" 는 줄 끝이 없어 None. 실제 펌웨어에서는 overrun 카운터를 `AtomicU32` 로 두고 main 이 진단용으로 읽는다(RUST-M06 Part A).
</details>

### 연습 3 — StaticQueue: static 하나에서 split 을 딱 한 번

`static RX: StaticQueue<u32, 16> = StaticQueue::new();` 의 `take_split(&'static self)` 가 처음 한 번만 `Some((Producer<'static>, Consumer<'static>))` 을 돌려주게 하라. `static mut` 없이.

<details><summary>답</summary>

`taken: AtomicBool` + `q: UnsafeCell<Queue>`. `if taken.swap(true, Relaxed) { return None }` 다음 `unsafe { &mut *q.get() }.split()`. swap 이 원자적이라 8 스레드가 동시에 불러도 승자는 하나다(테스트 `p3_concurrent_take_exactly_one_winner`). 게시할 데이터가 없으니(큐는 const 초기화된 static) Relaxed 로 충분하다. `'static` 끝점이라 scoped 가 아닌 `std::thread::spawn` 으로 보낼 수 있다. cortex-m 의 `singleton!` 이나 heapless 의 `split_const` 와 같은 역할이다. **주의:** `swap` 은 CAS 계열이라 thumbv6m 에 없다. 그래서 이 타입은 `#[cfg(target_has_atomic = "8")]` 이고, practice 크레이트는 M0+ 에서도 빌드되지만 StaticQueue 만 빠진다. M0+ 에서는 critical-section 으로 플래그를 지키면 된다.
</details>

### 연습 4 — dequeue_with: 큰 프레임을 슬롯 안에서 읽기

256바이트 프레임을 꺼내 복사하지 않고 `dequeue_with(|f: &Frame| ...) -> Option<R>` 로 슬롯 안에서 읽은 뒤 반납하라. drop 은 언제 해야 하나?

<details><summary>답</summary>

검사(Acquire tail) → `f(assume_init_ref())` → **`assume_init_drop()` → 그 다음** `head.store(h+1, Release)`. drop 도 슬롯 접근이다. head 를 먼저 올리면 생산자가 drop 중인 슬롯을 덮어쓸 수 있다(실수 1 과 같은 모양). 테스트는 drop 횟수로 "읽은 프레임은 슬롯 안에서 정확히 한 번, 남은 것은 Queue::drop 이" 를 확인한다(3개 넣고 2개 읽고 1개 더 넣고 끝 → drop 4).
</details>

### 한 번에 검증

<!-- code: practice/src/lib.rs -->

<!-- code: practice/Cargo.toml -->

```bash
cd lessons/RUST/RUST-M07/practice           # rustup PATH · SDKROOT · CARGO_TARGET_DIR 는 3장과 같이
cargo test                                  # test result: ok. 8 passed; 0 failed  (5번 반복 확인)
cargo clippy --all-targets -- -D warnings   # 경고 0
cargo build --target thumbv7em-none-eabihf  # OK (StaticQueue 포함)
cargo build --target thumbv6m-none-eabi     # OK (StaticQueue 는 cfg 로 빠짐)
```

관찰용 코드:

<!-- code: demo_types/src/main.rs -->

<!-- code: demo_loom/src/lib.rs -->

<!-- code: demo_loom/tests/loom.rs -->

<!-- code: demo_targets/src/lib.rs -->

<!-- code: demo_bench/src/main.rs -->

## 10. 핵심 정리

- SPSC = 변수마다 writer 하나. 내 인덱스는 `Relaxed` load, 상대 인덱스는 `Acquire` load, 내 인덱스 게시는 `Release` store. synchronizes-with 두 개(tail: 데이터 게시, head: 슬롯 반납)가 슬롯 소유권을 넘긴다.
- free-running 인덱스 + N = 2의 거듭제곱 → `t - h` 로 개수, `& (N-1)` 로 슬롯, 용량 N. heapless 0.9.3 은 `% n` 으로 접어서 N-1(설계 선택, Ordering 은 같다).
- `split(&mut self)` + Clone 없음 + `enqueue(&mut self)` = "단일 생산자/소비자" 가 타입 성질(E0499/E0599/E0596). `'a` 는 큐보다 오래 사는 끝점을 막는다(E0597).
- `unsafe impl<T: Send> Sync for Queue` — 값은 이동만 하므로 T: Send 면 충분. Producer/Consumer 의 Send 는 자동.
- Drop 은 `head..tail` 만, `while i != t` 로. 가득 찬 enqueue 는 값을 `Err(v)` 로 돌려준다. ISR 은 기다리지 말고 버리고 센다.
- loom: UnsafeCell 슬롯이면 짝 하나만 약화해도 인과 위반을 줄 번호까지 잡는다. 하지만 load buffering 은 못 잡는다(atomic 슬롯 + weak-head 통과, 실측). loom 통과 ≠ 정답.
- load/store 만 쓰므로 thumbv6m(M0+)에서도 빌드된다. Cortex-M 은 `ldr; dmb sy` / `dmb sy; str`, arm64 는 `ldapr` / `stlr`.
- lock-free(여기선 wait-free)는 진행 보장이지 속도가 아니다. 이 맥에서 2 스레드 처리량은 Mutex 큐와 비슷했다. 상대 인덱스 캐시(LLAT-M01, DS-A01)와 std channel 이 훨씬 빠르다.

## 참고자료

- Rust `core::sync::atomic` 문서 — Ordering, "Memory model for atomic accesses" (C++20 모델 참조), `compiler_fence`: https://doc.rust-lang.org/core/sync/atomic/
- Rust Nomicon "Atomics", "Send and Sync": https://doc.rust-lang.org/nomicon/atomics.html · https://doc.rust-lang.org/nomicon/send-and-sync.html
- Mara Bos, *Rust Atomics and Locks* (O'Reilly 2023) — 3장 Memory Ordering, 5장 Building Our Own Channels (one-shot channel 의 split/수명 설계): https://marabos.nl/atomics/
- loom 0.7.3 — `src/lib.rs` "Limitations and Caveats / Relaxed Memory Ordering", `src/rt/mod.rs` (MAX_THREADS=5, MAX_ATOMIC_HISTORY=7), `src/rt/cell.rs` (Causality violation): https://docs.rs/loom/0.7.3
- CDSChecker (loom 의 기반): Norris & Demsky, OOPSLA 2013, http://demsky.eecs.uci.edu/publications/c11modelcheck.pdf
- heapless 0.9.3 `src/spsc.rs` (increment :184, capacity :195, inner_enqueue :284–296, inner_dequeue :326–339, split :435, split_const :497, Producer/Consumer Send :744/:753), `src/lib.rs:219–228`: https://docs.rs/heapless/0.9.3/heapless/spsc/
- ISO C++ [intro.races], [atomics.order] / C11 5.1.2.4 — happens-before, coherence 규칙
- Arm Architecture Reference Manual (DDI 0487) 메모리 모델 장 (LDAPR = RCpc, dependency 규칙 — 절 번호 미확인), ARMv6-M ARM (DDI 0419) — exclusive 명령 없음
- 관련 레슨: RUST-M06 (ISR 공유 상태, M0+ 의 fetch_add E0599), DS-A01 (C11 SPSC 정답 논증·캐시 인덱스 측정), LLAT-M01 (SPSC 지연 튜닝), DS-A03 (Vyukov MPMC), DS-A02 (C 링 버퍼 ISR 판), C-A03 · OS-M08 · MCPP-M13 (release/acquire), ARM-M05 (RCsc vs RCpc), RUST-M08 (Miri)
