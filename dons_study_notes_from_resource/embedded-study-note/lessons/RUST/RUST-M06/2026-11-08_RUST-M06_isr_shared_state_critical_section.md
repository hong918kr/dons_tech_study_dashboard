---
id: RUST-M06
time: 90분
updated: 2026-11-08
---
# RUST-M06 ISR 공유 상태: static mut 대신 Mutex<RefCell<..>>

## 학습 목표

- C 의 "`volatile` 전역 + `__disable_irq()`" 를 `static mut` 로 그대로 옮기면 왜 막히는지 설명할 수 있다. 근거는 실제 rustc 메시지 두 가지다: edition 2024 의 `static_mut_refs` 오류, 그리고 "static 은 `Sync` 여야 한다" (E0277).
- **한 워드** 짜리 공유 상태는 `AtomicU32` / `AtomicBool` 의 `fetch_add` · `swap` 으로 만든다. 값과 valid 비트를 한 워드에 묶어 critical section 없이 우편함을 만들 수 있다.
- 필드가 여러 개이고 런타임에 만들어지는 드라이버는 `critical_section::Mutex<RefCell<Option<T>>>` 로 공유한다. 세 겹이 각각 무엇을 막는지(Sync / `&mut` 유일성 / 초기화 전) 말할 수 있다. Copy 데이터면 `Mutex<Cell<T>>` 로 충분하다.
- Cortex-M4 에는 `AtomicU64` 가 없고 Cortex-M0(+) 에는 CAS 가 없어 `fetch_add`/`swap` 자체가 없다. 그래서 `portable-atomic` 이 필요하다. 이것을 실제 컴파일 오류와 asm(`ldrex/strex` vs `cpsid i`)으로 보여 줄 수 있다.
- 호스트에서 `critical-section` 의 `std` 구현과 "ISR 역할 스레드" 로 경쟁 테스트를 하고, 이 모델이 실제 ISR 과 어디서 다른지 말할 수 있다.

## 1. 문제

임베디드 Rust 면접에서 "C 에서는 ISR 과 공유하는 변수를 `volatile` 전역으로 두고 인터럽트를 끄고 읽었는데, Rust 에서는 어떻게 하나요?" 는 첫 질문으로 거의 빠지지 않는다. 정답은 "`static mut` + `unsafe`" 가 아니다. **"공유 상태의 모양(한 워드인가 / 여러 필드인가 / 언제 만들어지나)에 맞는 타입을 고르면, 그 타입이 ISR 을 다른 스레드처럼 취급하도록 컴파일러를 설득한다"** 가 정답이다. 방산·로보틱스·자동차 펌웨어 팀(Anduril, Rivian, Oxide 등)이 Rust 를 들일 때 가장 먼저 부딪히는 지점이기도 하다.

상황: STM32F4(Cortex-M4F) 센서 보드. C 펌웨어는 이렇게 생겼다.

```c
volatile uint32_t g_ticks;          /* SysTick_Handler:   g_ticks++;                       */
volatile bool     g_btn;            /* EXTI0_IRQHandler:  g_btn = true;                    */
uart_rx_t         g_uart;           /* USART2_IRQHandler: uart_on_byte(&g_uart, USART2->DR) */
adc_win_t         g_win;            /* ADC_IRQHandler:    g_win.count++; g_win.sum += v;   */

uint32_t take_ticks(void) {         /* main 루프 */
    uint32_t pm = __get_PRIMASK(); __disable_irq();
    uint32_t n = g_ticks; g_ticks = 0;
    __set_PRIMASK(pm);
    return n;
}
```

이걸 Rust 로 한 줄씩 옮기면 `static mut G_TICKS: u32` 가 된다. 그런데 edition 2024 에서는 `&G_TICKS` 를 쓰는 순간, 심지어 `G_BUF.len()` 처럼 암묵적으로 참조를 만드는 순간에도 **컴파일 오류** 가 난다(5. 풀이에서 실제 메시지). 그래서 이 레슨은 같은 네 가지 공유 상태를 **`unsafe` 한 줄 없이** (`#![forbid(unsafe_code)]`) 다시 만든다.

```svg
<svg viewBox="0 0 740 310" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">ISR ↔ main 공유 상태: 모양을 보고 타입을 고른다</text>
  <rect x="10" y="40" width="230" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="60" fill="currentColor">공유할 상태가 있다</text>
  <text x="22" y="76" fill="currentColor" font-size="12">(카운터 · 플래그 · 드라이버 · 통계)</text>
  <rect x="290" y="40" width="200" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="302" y="60" fill="currentColor">32비트 한 워드로 되나?</text>
  <text x="302" y="76" fill="currentColor" font-size="12">값+valid 비트 묶기도 포함</text>
  <line x1="240" y1="62" x2="290" y2="62" stroke="currentColor"/>
  <rect x="530" y="40" width="200" height="44" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="540" y="60" fill="currentColor" font-family="monospace">AtomicU32 / AtomicBool</text>
  <text x="540" y="76" fill="currentColor" font-size="12">fetch_add · swap (Part A)</text>
  <line x1="490" y1="62" x2="530" y2="62" stroke="currentColor"/>
  <text x="500" y="56" fill="currentColor" font-size="12">예</text>
  <line x1="390" y1="84" x2="390" y2="120" stroke="currentColor"/>
  <text x="398" y="106" fill="currentColor" font-size="12">아니오 (필드 여러 개)</text>
  <rect x="290" y="120" width="200" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="302" y="140" fill="currentColor">Copy 이고 작은가?</text>
  <text x="302" y="156" fill="currentColor" font-size="12">통째로 get/set 해도 되나</text>
  <rect x="530" y="120" width="200" height="44" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="540" y="140" fill="currentColor" font-family="monospace">Mutex&lt;Cell&lt;T&gt;&gt;</text>
  <text x="540" y="156" fill="currentColor" font-size="12">ADC 통계 창 (Part C)</text>
  <line x1="490" y1="142" x2="530" y2="142" stroke="currentColor"/>
  <text x="500" y="136" fill="currentColor" font-size="12">예</text>
  <line x1="390" y1="164" x2="390" y2="200" stroke="currentColor"/>
  <text x="398" y="186" fill="currentColor" font-size="12">아니오 (&amp;mut 필요, init 이 런타임)</text>
  <rect x="290" y="200" width="440" height="44" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="302" y="220" fill="currentColor" font-family="monospace">critical_section::Mutex&lt;RefCell&lt;Option&lt;UartRx&gt;&gt;&gt;</text>
  <text x="302" y="236" fill="currentColor" font-size="12">main 이 init 한 드라이버를 ISR 과 공유 (Part B)</text>
  <rect x="10" y="120" width="250" height="124" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="22" y="142" fill="currentColor" font-family="monospace">static mut + &amp;X</text>
  <text x="22" y="162" fill="currentColor" font-size="12">edition 2024: 컴파일 오류</text>
  <text x="22" y="180" fill="currentColor" font-size="12">(static_mut_refs, deny)</text>
  <text x="22" y="204" fill="currentColor" font-family="monospace">static X: RefCell&lt;T&gt;</text>
  <text x="22" y="224" fill="currentColor" font-size="12">E0277: Sync 아님 → static 불가</text>
  <text x="10" y="274" fill="currentColor" font-size="12">Cortex-M0(+) 는 CAS 가 없어 fetch_add/swap 자체가 없다 → portable-atomic.</text>
  <text x="10" y="294" fill="currentColor" font-size="12">Cortex-M4 는 AtomicU64 가 없다 → 64비트는 Mutex&lt;Cell&lt;u64&gt;&gt;.</text>
</svg>
```

**호스트 모델.** 맥에는 인터럽트가 없으니 **테스트가 띄우는 `std::thread` 를 ISR 로 삼는다.** `critical_section::with` 의 구현은 칩에서는 `cortex-m` 크레이트(`critical-section-single-core`: PRIMASK 저장 → `cpsid i` → 복원)가, 호스트에서는 `critical-section` 크레이트의 `std` 기능(전역 `std::sync::Mutex` + 스레드별 중첩 플래그)이 맡는다. 라이브러리는 `#![no_std]` 이고 구현체는 고르지 않는다. 고르는 쪽은 최종 바이너리(칩) 또는 dev-dependency(테스트)다. 그래서 같은 `lib.rs` 를 `cargo test`(호스트)로도, `cargo build --target thumbv7em-none-eabihf`(칩)로도 빌드할 수 있다.

### Part A — 한 워드: `AtomicU32` / `AtomicBool`

- `TickCounter::isr_tick()` — SysTick ISR 이 1 증가. `take()` — main 이 쌓인 tick 을 **RMW 하나로** 가져가며 0 으로 만든다. 주어진 `take_naive(between)`(load → store 0)이 tick 을 잃는 것을 테스트가 먼저 보여 준다.
- `EventFlag::take()` — 버튼 이벤트를 "확인하면서 내린다". 처리 중 새로 온 이벤트가 남아 있어야 한다. 주어진 `poll_naive` 는 그 이벤트를 지운다.
- `Mailbox::isr_post(v) -> bool` / `take() -> Option<u16>` — ADC 최신값 하나를 `valid | v` **한 워드** 로 주고받는다. 덮어쓰면 overrun=true. 값과 플래그를 따로 둔 `MailboxSplit` 은 새 값을 영영 잃는다.

### Part B — 런타임에 만든 드라이버: `Mutex<RefCell<Option<UartRx>>>`

`UartRx`(줄 조립 상태: 버퍼, 완성 줄, bytes/lines/overruns/truncated)는 주어진다. 이 드라이버를 `static UART: SharedUart` 에 넣고 ISR 과 main 이 같이 쓰게 하는 `SharedUart` 의 메서드 6개를 채운다.

- `install(d) -> Result<(), UartRx>` (이미 있으면 받은 것을 돌려줌), `uninstall() -> Option<UartRx>`
- `with(f) -> Option<R>` — 임계 구역 안에서 `&mut UartRx` 를 빌려 준다. `try_with(f) -> Result<R, Access>` — 재진입이면 panic 대신 `Err(Busy)`.
- `isr_rx(b) -> bool` — USART2 RX ISR. 드라이버가 아직 없으면 `early` 를 세고 false. `take_line() -> Option<Line>` — 완성된 줄의 **복사본** 을 꺼내 임계 구역 밖에서 파싱한다.

### Part C — Copy 통계 + 스레드 ISR 경쟁 테스트

- `AdcStats::isr_sample(seq)` / `take_window() -> Window` — `Mutex<Cell<Window>>`. `Window { count, first, last, sum: u64 }` 의 네 필드가 **함께** 바뀌어야 한다. 64비트 합이라 Cortex-M4 에서도 atomic 으로는 안 된다.
- 테스트 3개가 진짜 스레드를 ISR 로 띄운다: tick 100,000개를 하나도 잃지 않기, 통계 창이 찢어지지 않고 이어지기(`count == last-first+1`, `sum == (first+last)·count/2`, 창 사이 빈틈·겹침 없음), UART 3,000줄이 순서대로 깨지지 않고 오기(`받은 줄 + overruns == 3000`).

## 2. Problem (English)

> **Share state between an ISR and the main loop without `static mut`.**
>
> Target: `#![no_std]` library that builds for `thumbv7em-none-eabihf`; host tests use `critical-section` with feature `std` and a `std::thread` as the ISR. `#![forbid(unsafe_code)]`.
>
> 1. Atomics. `TickCounter { isr_tick(&self); take(&self) -> u32 }`. `take` returns the pending ticks and zeroes them without losing a tick that arrives concurrently. `EventFlag::take(&self) -> bool` consumes the flag. An event raised while the caller handles the previous one must survive. `Mailbox { isr_post(&self, v: u16) -> bool /* true = overwrote unread value */; take(&self) -> Option<u16> }` uses a single `AtomicU32` (`1<<31` = valid).
> 2. Driver cell. `SharedUart` holds `critical_section::Mutex<RefCell<Option<UartRx>>>`. Implement `install(UartRx) -> Result<(), UartRx>`, `uninstall() -> Option<UartRx>`, `with(FnOnce(&mut UartRx) -> R) -> Option<R>`, `try_with(..) -> Result<R, Access /* NotInstalled | Busy */>`, `isr_rx(u8) -> bool` (counts bytes dropped before install), `take_line() -> Option<Line>`.
> 3. `AdcStats` holds `Mutex<Cell<Window>>` with `isr_sample(seq)` and `take_window()` (take-and-reset). Under a concurrent ISR thread, every window taken satisfies `count == last-first+1` and `sum == (first+last)*count/2`, and consecutive windows are contiguous.
>
> Example: `post(1); post(2) == true; take() == Some(2); take() == None`. `install` twice → `Err(d)` with `d.baud == 9600`. `with(|_| try_with(|_| ())) == Some(Err(Busy))`.
>
> Follow-ups: why is `&STATIC_MUT` rejected in edition 2024? Why doesn't `critical_section::Mutex::borrow` hand out `&mut T`? Why does `AtomicU32::fetch_add` not exist on `thumbv6m-none-eabi`, and what does `portable-atomic` do instead? Is the single-core critical-section implementation sound on RP2040?

## 3. 시작 코드

<!-- code: starter/src/lib.rs -->

<!-- code: starter/Cargo.toml -->

```bash
# rustup PATH (Homebrew rustup) + 맥 SDK 링크 오류 회피 (빌드 스크립트·테스트 바이너리 링크용)
export PATH=/opt/homebrew/opt/rustup/bin:$PATH
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
export CARGO_TARGET_DIR=/tmp/RUST-M06_target                            # target/ 을 레슨 폴더 밖에
cd lessons/RUST/RUST-M06/starter
cargo test                       # 처음: "test result: FAILED. 6 passed; 19 failed" (exit 101)
cargo build --target thumbv7em-none-eabihf      # no_std 라이브러리가 칩 타깃으로도 빌드되는지
cargo clippy --all-targets -- -D warnings       # 경고 0
```

구성: `starter/`, `solution/`, `practice/` 는 **각각 독립된 작은 라이브러리 크레이트** 다(워크스페이스 없음, `Cargo.lock` 포함). 각 크레이트의 `src/lib.rs` 맨 아래 `mod tests` 가 하네스이고, starter 와 solution 은 TODO 본문만 다르다. 처음부터 통과하는 6개는 주어진 naive 코드로 버그를 재현하는 테스트(`a3`, `a6`, `a11`, `c2`), `Sync` 확인(`a12`), 주어진 드라이버 로직(`b8`)이다. `SDKROOT` 가 없으면 이 맥에서는 `portable-atomic` 빌드 스크립트와 테스트 바이너리 링크가 `tapi error: malformed file … MacOSX27.2.sdk` 로 실패한다(실제로 겪음).

관찰용 파일 세 개 (하네스와 별개):

```bash
rustc --edition 2024 --cfg bad_ref demo_static_mut.rs      # static_mut_refs 오류 (bad_cell / bad_send 도)
(cd demo_targets && cargo build --target thumbv6m-none-eabi)               # fetch_add 없음
(cd demo_targets && cargo build --target thumbv6m-none-eabi --features pa-cs)  # portable-atomic 으로 해결
(cd demo_race && cargo run --release)                       # 진짜 스레드 ISR 로 naive 버전의 손실·찢어짐 세기
```

## 4. 힌트

<details>
<summary>힌트 1 — Part A: "읽고 → 쓰기" 두 단계 사이가 틈이다</summary>

`load` 와 `store` 는 **각각** 원자적이지만, 이어 붙인 "가져가고 비우기" 는 원자적이지 않다. 읽으면서 새 값을 쓰는 RMW 메서드가 `AtomicU32` / `AtomicBool` 에 있다. 덮어쓴 **이전 값** 을 돌려주는 그 메서드 하나로 A2, A3, A4, A5 를 모두 풀 수 있다.

</details>

<details>
<summary>힌트 2 — Part B: borrow(cs) 는 &RefCell 을 준다</summary>

`critical_section::with(|cs| ...)` 안에서 `self.cell.borrow_ref_mut(cs)` 는 `RefMut<Option<UartRx>>` 다. `Option` 쪽은 `is_some` / `take` / `as_mut().map(f)` 로 다룬다. `try_with` 는 `self.cell.borrow(cs)` 로 `&RefCell` 을 먼저 받고, `try_borrow_mut()` 의 `Err` 를 `Access::Busy` 로 바꾼다.

</details>

<details>
<summary>힌트 3 — Part B: isr_rx / take_line 은 with 위에 짓는다</summary>

`self.with(|d| d.on_byte(b))` 는 드라이버가 없으면 `None` 이다. `take_line` 은 `Option<Option<Line>>` 이 되니 `flatten()`. 줄을 **복사해서** 돌려주므로 파싱은 임계 구역 밖에서 하게 된다. 인터럽트를 막고 있는 시간은 복사 시간뿐이다.

</details>

<details>
<summary>힌트 4 — Part C: Cell 은 get / set / replace</summary>

`Window` 는 `Copy` 라 `&mut` 이 필요 없다. ISR 쪽은 `let c = self.win.borrow(cs); let mut w = c.get(); w.push(seq); c.set(w);`, main 쪽은 `replace(Window::EMPTY)` 로 "통째로 가져가며 비우기" 를 한다. 둘 다 같은 `critical_section::with` 안에서 해야 네 필드가 한 덩어리로 움직인다.

</details>

## 5. 풀이

### 0. 모델: Cortex-M ↔ 이 레슨의 호스트 모델

| 이 레슨 (호스트) | Cortex-M 실제 (Rust) | C / CMSIS 대응 | RTOS 대응 |
|---|---|---|---|
| 테스트가 띄우는 `std::thread` | `#[interrupt] fn USART2()`, `#[exception] fn SysTick()` (cortex-m-rt, RUST-M12) | `void USART2_IRQHandler(void)` | FreeRTOS ISR 에서 `xSemaphoreGiveFromISR` (RTOS-B08) |
| `critical_section::with` — `std` 구현: 전역 `std::sync::Mutex` + thread_local 중첩 플래그 | `cortex-m` `critical-section-single-core`: `primask::read_raw()` → `cpsid i` → `primask::write_raw(saved)` | `uint32_t pm=__get_PRIMASK(); __disable_irq(); … __set_PRIMASK(pm);` (C-M14) | FreeRTOS `taskENTER_CRITICAL()` / `taskENTER_CRITICAL_FROM_ISR()`, Zephyr `key = irq_lock(); … irq_unlock(key);` |
| `AtomicU32::fetch_add` / `swap` | M3/M4/M7: `ldrex/strex` 루프. M0/M0+: **메서드 없음** | C11 `atomic_fetch_add` (C-A03), C++ `std::atomic` (CPP-A04) | — |
| `portable_atomic::AtomicU32` | M0: critical section 또는 `mrs primask; cpsid i … msr primask` | M0 에서 clang 이 내는 `__atomic_*_4` libcall (C-M14 에서 관찰) | — |
| (다루지 않음) 멀티코어 | `rp2040-hal` 의 `critical-section-impl` (하드웨어 스핀락 + 인터럽트 끄기) | — | FreeRTOS SMP 의 크리티컬 섹션 스핀락 (RTOS-A08) |
| (다루지 않음) 컴파일 타임 락 | RTIC `#[shared]` 리소스 + `lock()` — 우선순위 천장, 필요한 우선순위만 마스크 (RUST-A02) | — | IPCP (RTOS-M05) |

`cortex-m 0.7.10` 의 실제 구현(`src/critical_section.rs`)은 위 표 그대로다. `acquire` 는 `primask::read_raw()` 로 이전 PRIMASK 를 저장한 뒤 `interrupt::disable()`(`cpsid i`), `release` 는 저장한 값을 `primask::write_raw` 로 되돌린다. 중첩해도 안전한 **저장/복원** 방식이고, C-M14 Part B 의 결론과 같다. 호스트 `std` 구현(`critical-section 1.2.0` `src/std.rs`)은 같은 스레드의 중첩을 `IS_LOCKED` thread_local 로 허용하고, 패닉으로 mutex 가 poison 되어도 `into_inner()` 로 무시한다. `with` 자체도 `Drop` 가드로 `release` 를 부르므로 클로저가 panic 해도 임계 구역이 풀린다(`src/lib.rs` 의 `with`). 테스트 `b7` 이 일부러 panic 해도 다른 테스트가 멀쩡한 이유다.

### `static mut` 은 왜 안 되나 — `demo_static_mut.rs` 실제 출력

C 를 그대로 옮긴 `static mut COUNTER: u32` 에 대해 main 이 `&COUNTER` 로 읽으면 (rustc 1.99.1):

```text
$ rustc --edition 2024 --cfg bad_ref demo_static_mut.rs
error: creating a shared reference to mutable static
  --> demo_static_mut.rs:23:28
   |
23 |     let r: &u32 = unsafe { &COUNTER }; // 참조: 사는 동안 ISR 이 쓰면 즉시 UB
   |                            ^^^^^^^^ shared reference to mutable static
   |
   = note: shared references to mutable statics are dangerous; it's undefined behavior if the static is mutated or if a mutable reference is created for it while the shared reference lives
   = note: for more information, see <https://doc.rust-lang.org/edition-guide/rust-2024/static-mut-references.html>
   = note: `#[deny(static_mut_refs)]` (part of `#[deny(rust_2024_compatibility)]`) on by default
help: use `&raw const` instead to create a raw pointer
```

같은 파일을 `--edition 2021` 로 빌드하면 `#[warn(static_mut_refs)]` **경고** 만 나고 컴파일·실행된다. 2024 에서 기본값이 deny 로 바뀐 것이다. 메서드 호출의 암묵적 참조도 똑같이 걸린다. 따로 확인한 `static mut BUF: [u8; 4]` 에서 `BUF.len()` 은 `creating a shared reference to mutable static`, `BUF.iter_mut()` 은 `creating a mutable reference to mutable static` 이 났다. DMA 버퍼를 `static mut` 배열로 두던 C 습관이 바로 막힌다(DMA 소유권은 RUST-A01).

**왜 참조가 문제인가.** Rust 의 `&T` 는 "살아 있는 동안 아무도 바꾸지 않는다", `&mut T` 는 "살아 있는 동안 나만 접근한다" 는 약속이고, 컴파일러는 이 약속을 근거로 읽기를 캐시하거나 순서를 바꾼다. ISR 은 언제든 끼어들 수 있으니 `static mut` 에 대한 참조는 이 약속을 지킬 방법이 없다. 컴파일러가 제안하는 `&raw const COUNTER` 로 바꾸면 컴파일은 된다(데모의 기본 빌드가 `(&raw const COUNTER).read_volatile()` 를 쓴다). 하지만 그건 **경고를 끈 것이지 고친 것이 아니다.** `volatile` 은 원자성도, 순서도 주지 않는다(C-B09). `COUNTER += 1` 같은 값 접근은 2024 에서도 컴파일되지만 여전히 `unsafe` 이고, 이 쓰기와 ISR 의 쓰기가 겹치면 데이터 레이스, 곧 UB 다.

**두 번째 벽: static 은 `Sync` 여야 한다.** Rust 는 ISR 을 "다른 스레드" 와 같게 본다. 그래서 `static` 에 둘 수 있는 타입은 여러 실행 흐름이 동시에 `&T` 를 가져도 안전한 `Sync` 타입뿐이다.

```text
$ rustc --edition 2024 --cfg bad_cell demo_static_mut.rs
error[E0277]: `RefCell<u32>` cannot be shared between threads safely
   = help: the trait `Sync` is not implemented for `RefCell<u32>`
   = note: shared static variables must have a type that implements `Sync`

$ rustc --edition 2024 --cfg bad_send demo_static_mut.rs
error[E0277]: `Rc<u8>` cannot be sent between threads safely
   = note: required for `std::sync::Mutex<Option<Rc<u8>>>` to implement `Sync`
   = note: shared static variables must have a type that implements `Sync`
```

(긴 줄 일부를 생략했다.) `critical_section::Mutex` 도 규칙이 같다: `unsafe impl<T> Sync for Mutex<T> where T: Send {}` (critical-section 1.2.0 `src/mutex.rs`). 테스트 `a12` 가 이 레슨의 공유 타입들이 모두 `Sync` 이고 `static` 에 들어간다는 것을 컴파일로 확인한다.

### Part A — 재현: load → store 사이에 온 tick

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">take_naive(): load 직후 SysTick 이 끼어들면 4번째 tick 이 사라진다</text>
  <text x="10" y="62" fill="currentColor">main</text>
  <text x="10" y="112" fill="currentColor">ISR</text>
  <rect x="80" y="42" width="170" height="30" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="90" y="62" fill="currentColor" font-family="monospace">n = ticks.load() → 3</text>
  <rect x="270" y="92" width="170" height="30" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="280" y="112" fill="currentColor" font-family="monospace">fetch_add → ticks=4</text>
  <rect x="460" y="42" width="170" height="30" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="470" y="62" fill="currentColor" font-family="monospace">ticks.store(0)</text>
  <text x="640" y="62" fill="currentColor">→ 3 반환</text>
  <path d="M250 57 L270 107" stroke="currentColor" fill="none"/>
  <path d="M440 107 L460 57" stroke="currentColor" fill="none"/>
  <text x="10" y="162" fill="currentColor">swap</text>
  <rect x="80" y="142" width="270" height="30" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="90" y="162" fill="currentColor" font-family="monospace">n = ticks.swap(0) → 3 (한 명령)</text>
  <rect x="365" y="142" width="170" height="30" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="375" y="162" fill="currentColor" font-family="monospace">fetch_add → ticks=1</text>
  <text x="545" y="162" fill="currentColor">다음 take() 가 1 을 가져감</text>
  <rect x="10" y="190" width="720" height="50" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="210" fill="currentColor">ISR 은 swap "도중" 에 끼어들 수 없다: M4 는 ldrex/strex 루프라 ISR 이 오면 strex 가 실패해 다시 돈다,</text>
  <text x="22" y="230" fill="currentColor">arm64 호스트는 swp 한 명령. 어느 쪽이든 tick 은 swap 앞이나 뒤에 붙고, 사라지지 않는다.</text>
</svg>
```

테스트 `a3` 이 이 그림 그대로다. `take_naive(|| raw_tick(&t))` 는 3 을 돌려주고 `pending() == 0` 이 된다. ISR 은 네 번 돌았는데 하나가 증발했다. 이벤트 플래그(`a6`)와 값·플래그를 따로 둔 우편함(`a11`)도 같은 구조다. 원자적 부품을 이어 붙였을 뿐 전체는 원자적이지 않다(C-M14 의 핵심 문장).

| 주어진 naive 코드 | 끼워 넣은 ISR | 결과 (테스트) | 고침 |
|---|---|---|---|
| `TickCounter::take_naive` | load 직후 tick | 3 반환, pending 0 → 1 tick 유실 (`a3`) | `ticks.swap(0)` |
| `EventFlag::poll_naive` | handle() 중 `isr_set` | 두 번째 poll false → 이벤트 유실 (`a6`) | `flag.swap(false)` 먼저, 처리는 그 뒤 |
| `MailboxSplit::take` | 값 읽은 직후 `post(2)` | 1 반환 후 `None` → 2 는 영영 안 읽힘 (`a11`) | `valid \| v` 를 한 워드에, `swap` 하나 |

**고친 코드 (solution 발췌):**

```rust
pub fn take(&self) -> u32 { self.ticks.swap(0, Ordering::Relaxed) }               // TickCounter
pub fn take(&self) -> bool { self.flag.swap(false, Ordering::Acquire) }           // EventFlag
pub fn isr_post(&self, v: u16) -> bool {                                          // Mailbox
    self.word.swap(MAILBOX_VALID | v as u32, Ordering::Release) & MAILBOX_VALID != 0
}
pub fn take(&self) -> Option<u16> {
    let w = self.word.swap(0, Ordering::Acquire);
    if w & MAILBOX_VALID != 0 { Some(w as u16) } else { None }
}
```

**우편함의 요령.** "값이 있다" 는 사실과 값 자체를 한 워드에 넣으면 둘이 찢어질 수 없다. 그러면 critical section 도 필요 없다. 비트 31 을 valid 로 쓰니 값 0 도 "비어 있음" 과 구분된다(`a10`). 우편함은 "최신값 하나" 의미라 덮어쓰기는 정상 동작이고, `isr_post` 가 돌려주는 overrun 비트로 셀 수 있다. 값이 32비트를 다 쓰면 이 요령은 안 통한다. 그때는 Part C 의 `Mutex<Cell<T>>` 나 SPSC 큐(RUST-M07)를 쓴다.

**`Ordering` 은 왜 이렇게?** 단일 코어에서 ISR 과 main 은 같은 코어의 같은 메모리 순서를 본다. 그래서 막아야 하는 것은 **컴파일러의 재배치** 다.

| 공유 | Ordering | 이유 |
|---|---|---|
| tick 카운터 (`fetch_add`/`swap`) | `Relaxed` | 카운터 값 자체가 전부다. 이 값으로 다른 데이터를 "게시" 하지 않는다 |
| 이벤트 플래그 | ISR `Release` / main `Acquire` | 플래그를 보고 ISR 이 그 전에 써 둔 버퍼를 읽는 패턴이 흔하다. 게시 순서를 보장한다 |
| 우편함 | `Release` / `Acquire` | 같은 이유. 값이 워드 안에 있어 사실 Relaxed 로도 이 레슨의 코드는 맞지만, 습관으로 둔다 |

acquire/release 자체의 의미는 C-A03 과 RUST-M07 이 맡는다. 다만 Rust 의 atomic 은 "다른 코어일 수도 있다" 고 가정하므로 단일 코어에서도 장벽 명령을 낸다. 아래 asm 의 `dmb sy` 가 그것이다.

### Part A — 같은 코드, 다른 칩: `demo_targets` 실제 출력

`demo_targets/src/lib.rs` 는 위 패턴을 `extern "C"` 함수 몇 개로 줄인 `#![no_std]` 라이브러리다. 먼저 Cortex-M0+ (`thumbv6m-none-eabi`):

```text
$ cargo build --target thumbv6m-none-eabi
error[E0599]: no method named `fetch_add` found for struct `Atomic<T>` in the current scope
  --> src/lib.rs:24:11
   |
24 |     TICKS.fetch_add(1, Ordering::Relaxed);
   |           ^^^^^^^^^ method not found in `Atomic<u32>`

error[E0599]: no method named `swap` found for struct `Atomic<T>` in the current scope
```

레슨의 `solution` 크레이트를 thumbv6m 으로 빌드해도 같은 E0599 가 **10개** 난다(`fetch_add` 4곳, `AtomicU32`/`AtomicBool` 의 `swap` 6곳). `load`/`store` 는 남아 있다. 타깃이 무엇을 지원하는지는 `rustc --print cfg` 로 보인다:

```text
thumbv7em-none-eabihf: target_has_atomic="8" "16" "32" "ptr"   (64 없음 → AtomicU64 없음)
thumbv6m-none-eabi:    target_has_atomic 없음 (target_has_atomic_primitive_alignment 만)
```

ARMv6-M 에는 LDREX/STREX 가 없다. 그래서 "읽고-바꾸고-쓰기" 를 한 번에 할 하드웨어 수단이 없고, Rust 는 그런 메서드를 **아예 만들지 않는다.** C 의 clang 은 같은 상황에서 `__atomic_fetch_add_4` 같은 라이브러리 호출을 조용히 만든다(C-M14 에서 관찰). Rust 는 컴파일 오류로 알려 준다. `try-u64` 기능으로 thumbv7em 에서 `core::sync::atomic::AtomicU64` 를 쓰면 `cannot find type AtomicU64 in module core::sync::atomic` 이 난다. M4 에서 64비트 카운터를 atomic 으로 둘 수 없다는 뜻이고, 그래서 Part C 는 `Mutex<Cell<Window>>` 를 쓴다.

**`portable-atomic` 으로 고치기.** `use portable_atomic::{AtomicU32, Ordering};` 로 바꾸면 API 는 그대로이고 빌드된다. 기능은 둘 중 하나를 고른다.

```text
$ cargo build --target thumbv6m-none-eabi --features pa-cs       → Finished (critical-section 기능)
$ cargo build --target thumbv6m-none-eabi --features pa-single   → Finished (unsafe-assume-single-core 기능)
```

실제 asm (`cargo rustc --release --lib --target … -- --emit asm`, opt-level s, 디렉티브 생략):

```text
@ Cortex-M4, core AtomicU32           @ M0+, portable-atomic critical-section    @ M0+, unsafe-assume-single-core
demo_isr_tick:                        demo_isr_tick:                             demo_isr_tick:
  movw r0, :lower16:TICKS               bl   _critical_section_1_0_acquire         mrs  r0, primask
  movt r0, :upper16:TICKS               ldr  r0, .LCPI1_0                          cpsid i
.LBB1_1:                                ldr  r1, [r0]                              ldr  r1, .LCPI1_0
  ldrex r1, [r0]                        adds r1, r1, #1                            ldr  r2, [r1]
  adds  r1, #1                          str  r1, [r0]                              adds r2, r2, #1
  strex r2, r1, [r0]                    bl   _critical_section_1_0_release         str  r2, [r1]
  cmp   r2, #0                                                                     msr  primask, r0
  bne   .LBB1_1
demo_take_ticks (swap):               demo_peek_ticks (load):                    demo_peek_ticks (load):
  ldrex r0, [r1] / strex r3, r2, [r1]   bl _critical_section_1_0_acquire           ldr r0, .LCPI2_0
  cmp r3, #0 / bne .LBB4_1              ldr r4, [r0] / bl …_release                ldr r0, [r0]
```

읽는 법:

- **M4**: `fetch_add` 는 `ldrex/adds/strex` 재시도 루프다. ISR 이 `ldrex` 와 `strex` 사이에 끼어들면 예외 진입/복귀가 배타 모니터를 지워 `strex` 가 실패(r2≠0)하고 다시 돈다(ARMv7-M ARM A3.4). 인터럽트를 끄지 않는다.
- **M0+ `critical-section`**: 매 연산이 `_critical_section_1_0_acquire/release` **함수 호출** 이다. 이 두 심볼은 최종 바이너리가 링크하는 구현체(예: `cortex-m` 의 `critical-section-single-core`)가 정의한다. 구현체가 없으면 링크가 `undefined reference to _critical_section_1_0_acquire` 로 실패한다(critical-section README "Undefined reference errors", 이 레슨은 라이브러리만 빌드해서 링크까지는 미검증). 눈여겨볼 점: **단순 `load` 도 임계 구역을 탄다.** README 의 "critical sections are taken for all atomic operations" 그대로다.
- **M0+ `unsafe-assume-single-core`**: `mrs primask; cpsid i … msr primask` 가 함수 안에 인라인된다. C-M14 의 `__get_PRIMASK/__disable_irq/__set_PRIMASK` 와 정확히 같은 세 명령이다. `load` 는 그냥 `ldr` 이다. 대신 이름 그대로 **멀티코어에서는 unsound** 이고(README WARNING), 특권 모드가 아니면 `cpsid` 가 무시된다.
- `demo_add_uptime`(64비트 `Mutex<Cell<u64>>`)은 M4 에서 `bl acquire; ldrd r1, r2, [r0]; adds; adc; strd; b release` 다. 64비트 접근 `ldrd/strd` 자체는 원자적이라는 보장이 없지만, 임계 구역 안이라 상관없다.
- **Release store** (`READY.store(true, Release)`): M4 와 M0+ 모두 `dmb sy` 다음 `strb`, arm64 호스트는 `stlrb` 한 명령이다. `fetch_add`(Relaxed) 는 호스트에서 `ldadd`, `swap` 은 `swp wzr, w0, [x8]` (ARMv8.1 LSE) 이다.

portable-atomic README 는 라이브러리가 `critical-section` 기능을 **스스로 켜는 것을 권하지 않는다**(최종 사용자가 더 효율적인 구현을 고를 기회를 뺏으므로). 라이브러리는 기능 선택을 남겨 두고 바이너리가 고르는 것이 원칙이다. `practice` 크레이트는 연습을 위해 직접 켰고, 그 사실을 `Cargo.toml` 주석에 적어 두었다.

### Part B — `Mutex<RefCell<Option<UartRx>>>`: 세 겹이 각각 막는 것

```svg
<svg viewBox="0 0 740 316" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">static UART: SharedUart — 바깥부터 한 겹씩</text>
  <rect x="10" y="36" width="380" height="220" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="22" y="56" fill="currentColor" font-family="monospace">critical_section::Mutex&lt;…&gt;</text>
  <text x="22" y="74" fill="currentColor" font-size="12">Sync 를 준다 (T: Send). borrow(cs) → &amp;T 만</text>
  <rect x="30" y="86" width="340" height="156" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="42" y="106" fill="currentColor" font-family="monospace">RefCell&lt;…&gt;</text>
  <text x="42" y="124" fill="currentColor" font-size="12">빌림 플래그로 &amp;mut 이 하나뿐인지 런타임 검사</text>
  <rect x="50" y="136" width="300" height="92" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="62" y="156" fill="currentColor" font-family="monospace">Option&lt;…&gt;</text>
  <text x="62" y="174" fill="currentColor" font-size="12">init 전 = None (ISR 은 early 만 센다)</text>
  <rect x="70" y="184" width="260" height="34" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="80" y="206" fill="currentColor" font-family="monospace">UartRx { acc, done, bytes, … }</text>
  <text x="420" y="56" fill="currentColor" font-weight="bold">시간 →</text>
  <text x="420" y="92" fill="currentColor">main</text>
  <rect x="470" y="74" width="180" height="28" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="478" y="93" fill="currentColor" font-size="12" font-family="monospace">with: take_line (cs)</text>
  <text x="420" y="142" fill="currentColor">IRQ</text>
  <rect x="520" y="124" width="70" height="28" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="528" y="143" fill="currentColor" font-size="12">펜딩</text>
  <rect x="655" y="124" width="75" height="28" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="661" y="143" fill="currentColor" font-size="12">isr_rx</text>
  <path d="M650 102 L660 124" stroke="currentColor" fill="none"/>
  <text x="420" y="186" fill="currentColor" font-size="12">칩: PRIMASK=1 동안 온 IRQ 는 펜딩 →</text>
  <text x="420" y="204" fill="currentColor" font-size="12">복원(msr primask) 직후 실행.</text>
  <text x="420" y="228" fill="currentColor" font-size="12">호스트: ISR 스레드가 전역 Mutex 에서</text>
  <text x="420" y="246" fill="currentColor" font-size="12">기다렸다가 들어온다.</text>
  <text x="10" y="282" fill="currentColor" font-size="12">하나라도 빼면: Mutex 없음 → E0277(Sync 아님), RefCell 없음 → &amp;mut 을 못 얻음,</text>
  <text x="10" y="302" fill="currentColor" font-size="12">Option 없음 → const 로 못 만드는 드라이버(런타임 init)를 static 에 못 둠.</text>
</svg>
```

**왜 `Mutex` 가 `&mut T` 를 주지 않나.** critical-section 1.2.0 `src/mutex.rs` 의 설계 주석이 직접 답한다. `CriticalSection` 토큰은 `Copy` 이고 임계 구역은 중첩할 수 있다. 그러니 같은 `with` 안에서 `borrow(cs)` 를 두 번 부를 수 있고, 그게 `&mut T` 를 준다면 **같은 데이터에 `&mut` 이 둘** 생긴다. 그래서 `Mutex` 는 `Sync` 만 더하고, `&mut` 이 유일한지는 `RefCell` 이 런타임에 검사한다(`std::sync::Mutex` 가 하는 일을 둘로 쪼갠 것). 필요 없으면 비용을 안 내면 된다. `Copy` 값이면 `Cell`(Part C), 한 워드면 atomic(Part A).

**solution 핵심:**

```rust
pub fn install(&self, d: UartRx) -> Result<(), UartRx> {
    critical_section::with(|cs| {
        let mut slot = self.cell.borrow_ref_mut(cs);
        if slot.is_some() { return Err(d); }          // 받은 드라이버를 돌려준다 (drop 하지 않는다)
        *slot = Some(d);
        Ok(())
    })
}
pub fn with<R>(&self, f: impl FnOnce(&mut UartRx) -> R) -> Option<R> {
    critical_section::with(|cs| self.cell.borrow_ref_mut(cs).as_mut().map(f))
}
pub fn try_with<R>(&self, f: impl FnOnce(&mut UartRx) -> R) -> Result<R, Access> {
    critical_section::with(|cs| {
        let mut slot = self.cell.borrow(cs).try_borrow_mut().map_err(|_| Access::Busy)?;
        slot.as_mut().map(f).ok_or(Access::NotInstalled)
    })
}
```

| 테스트 | 확인하는 것 |
|---|---|
| `b1` | install → ISR 이 `"AT+OK\n"` 6바이트 → `take_line() == "AT+OK"` |
| `b2` | 두 번째 install 은 `Err(d)` 로 **그 드라이버를 돌려준다** (`d.baud == 9600`), 원래 것은 그대로 |
| `b3` | init 전에 온 2바이트 → `isr_rx` false, `early_bytes() == 2` (C 라면 초기화 안 된 구조체에 쓰는 버그) |
| `b4` | `uninstall()` 이 상태째 돌려준다 (`bytes 3, lines 1`). 이후 ISR 바이트는 early |
| `b5` | `"1\n2\n"` → `(bytes, lines, overruns) == (4, 2, 1)` (첫 줄을 안 가져가서 덮임) |
| `b6` | `with` 안에서 `try_with` → `Some(Err(Busy))`. 빌림이 끝나면 다시 `Ok` |
| `b7` | `with` 안에서 `take_line`(= 또 `with`) → **panic "RefCell already borrowed"** (`#[should_panic]`) |

**`b7` 이 이 레슨의 핵심 장면이다.** C 에서 같은 실수(임계 구역 안의 콜백이 같은 구조체를 다시 수정)는 조용한 aliasing 이다. 반쯤 갱신된 구조체를 또 고치고, 아무 일 없다는 듯 지나간다. Rust 는 같은 컨텍스트의 재진입을 `RefCell` 이 잡아 **즉시 panic** 한다. 다만 칩에서 ISR 안의 panic 은 `panic-halt` 류 핸들러로 멈춘다는 뜻이다. 그래서 ISR 경로는 `try_with` 처럼 `Busy` 를 값으로 돌려받아 "이번 바이트는 버리고 센다" 로 처리하는 편이 낫다. 다른 컨텍스트(ISR ↔ main)끼리는 critical section 이 막으므로 `RefCell` 충돌이 생기지 않는다. 그 충돌은 **같은 컨텍스트의 재진입** 에서만 난다.

**`Option` 과 install/uninstall.** 실제 `UartRx` 는 PAC 의 `USART2` 핸들을 소유한다. 이 핸들은 main 이 `Peripherals::take()`(RUST-M03)로 얻은 뒤에야 생기므로 `static` 초기값으로 쓸 수 없다. 그래서 `None` 으로 시작해서 init 이 끝나면 `install` 한다. 인터럽트를 켜기 전에 install 하는 것이 정석이지만, `b3` 처럼 그 전에 IRQ 가 와도 안전하게 버리고 센다. `uninstall` 은 저전력 진입 전(BM-M11)이나 재설정 때 드라이버를 꺼내 `&mut` 으로 마음껏 다루게 해 준다.

**임계 구역은 짧게.** `take_line` 은 `Line`(16바이트 + 길이)을 **복사해서** 밖으로 가져간다. 파싱·로그·응답은 인터럽트가 켜진 상태에서 한다. 임계 구역 안에서 오래 일하면 그동안 같은 IRQ 가 두 번 오는 순간 하나로 합쳐져(NVIC 펜딩 비트는 IRQ 당 1개, C-M14) 바이트를 잃는다.

**참고: ISR 전용 상태.** ISR **혼자만** 쓰는 상태(예: 디바운스 카운터)는 공유가 아니다. `cortex-m-rt 0.7.7` 은 `#[interrupt]`/`#[exception]` 핸들러 맨 앞의 `static mut FOO: u32` 를 `let FOO: &mut u32` 로 바꿔 안전하게 쓰게 해 준다(소스 문서 기준). 공유는 하지 않는다는 점이 이 레슨의 경우와 다르다.

### Part C — `Mutex<Cell<Window>>` 와 스레드 ISR 경쟁

`Window` 는 `Copy` 다. `Cell` 의 `get`/`set`/`replace` 는 값을 통째로 복사해 넣고 빼므로 `&mut` 이 필요 없고, 빌림 플래그 비용도 없다.

```rust
pub fn isr_sample(&self, seq: u32) {
    critical_section::with(|cs| { let c = self.win.borrow(cs); let mut w = c.get(); w.push(seq); c.set(w); });
}
pub fn take_window(&self) -> Window {
    critical_section::with(|cs| self.win.borrow(cs).replace(Window::EMPTY))
}
```

**필드마다 atomic 이면?** 주어진 `AdcStatsSplit` 은 `count`, `sum` 을 각각 `AtomicU32` 로 둔다. 테스트 `c2`: 샘플 1, 2, 3 뒤 `count` 를 swap 한 직후 샘플 4 가 오면 `(3, 10)` 을 가져간다. 세 개의 합이 10 인, 존재한 적 없는 창이다. 다음 창은 `(1, 0)`, 샘플 1개에 합 0 이다.

**스레드 ISR 경쟁 테스트 (`c3`–`c5`).** 결정적인 것만 CHECK 한다. 어떤 스케줄로 돌든 맞아야 하는 불변식이다.

| 테스트 | ISR 스레드 | main | CHECK (매 실행 성립) |
|---|---|---|---|
| `c3` | `isr_tick()` × 100,000 | 끝날 때까지 `take()` 누적 | 합 == 100,000 |
| `c4` | `isr_sample(1..=100,000)` | 끝날 때까지 `take_window()` | 모든 창이 `check_window` 통과, 첫 창 first=1, 끝 창 last=N, 창이 빈틈·겹침 없이 이어짐, count 합 == N |
| `c5` | `"#00001\n"` … `"#03000\n"` (7바이트씩) | 끝날 때까지 `take_line()` | 깨진 줄 0, bytes == 21,000, lines == 3,000, 받은 줄 + overruns == 3,000, 번호 증가, 마지막 줄 == 3000 |

`c5` 의 마지막 CHECK 는 "최신 줄 우선(덮어쓰기)" 정책 덕분에 결정적이다. 마지막 줄은 덮을 다음 줄이 없으니 반드시 받는다. starter 에서는 `take()` 가 0, `take_window()` 가 빈 창을 돌려주니 이 셋은 FAIL 한다. 루프는 ISR 스레드가 끝나면(`is_finished()`) 멈추므로 **멈추지 않고(hang 없이)** 끝난다.

**진짜 경쟁에서 naive 버전은? — `demo_race` 실제 출력** (Apple M6, P코어 2 + E코어 4, rustc 1.99.1, `--release`, 3회 중 1회. 나머지 2회도 같은 경향):

```text
ticks  naive  gap= 0: lost 1955457 of 2000000 (polls 40514196)
ticks  naive  gap=50: lost 2000000 of 2000000 (polls 7867)
ticks  swap         : lost       0 of 2000000 (polls 2194727)
window split gap= 0: torn 865582 of 913163 windows
window split gap=50: torn   7542 of 7542 windows
window cs-Cell      : torn      0 of 6438 windows
cost/call (host, uncontended): AtomicU32::fetch_add 1.37 ns | AtomicBool load+store 1.56 ns | critical_section::with(std) 5.03 ns
```

(다른 두 번: naive gap=0 lost 1,844,136 / 1,908,659, split gap=0 torn 753,914/852,058 · 850,153/866,496, swap·cs-Cell 은 세 번 모두 0. 비용 fetch_add 1.36–1.60 ns, `with` 4.81–5.03 ns.)

읽는 법:

- naive 가 **92–98% 를 잃는 것** 은 호스트 스레드가 진짜 **병렬** 이기 때문이다. ISR 스레드는 다른 코어에서 쉬지 않고 증가시키고, main 의 `load` 와 `store(0)` 사이에도 계속 쌓인다. `gap=50`(사이에 `spin_loop` 50번)이면 그 사이에 쌓인 것이 전부 지워져 100% 를 잃는다. 실제 MCU 에서는 ISR 이 main 을 **선점** 해야만 끼어들 수 있고, 그 창은 명령 1–2개라 손실이 훨씬 드물다. 며칠에 한 번 나니 **더 위험하다.**
- `swap` 과 `Mutex<Cell<Window>>` 는 200만 번 중 0. 체크 테스트 `c3`/`c4` 가 매번 통과하는 것과 같은 사실이다.
- 비용은 호스트 값이라 MCU 와 다르다. 호스트에서 `with` 는 uncontended `std::sync::Mutex` 잠금/해제다. M4 에서 `fetch_add` 는 `ldrex/strex` 몇 사이클이고, `critical-section-single-core` 의 `with` 는 `mrs/cpsid/msr` 몇 사이클 + 함수 호출이다. MCU 에서 임계 구역의 진짜 비용은 **인터럽트 지연(latency)** 이다.

**스레드 모델은 실제 ISR 과 어디서 다른가.** 스레드는 ISR 보다 **더 가혹한** 면이 있고(진짜 병렬, 양방향 선점), **더 느슨한** 면도 있다(우선순위 없음, 임계 구역이 인터럽트 마스크가 아니라 mutex). 그래서 이 레슨처럼 "atomic RMW 하나" 나 "임계 구역 안에서 통째로" 인 코드는 스레드에서 맞으면 단일 코어 ISR 에서도 맞는다. 하지만 **"ISR 은 main 에게 선점되지 않는다" 를 이용하는 기법** 은 스레드에서 검증할 수 없다. 예를 들어 hi-lo-hi 재읽기(연습 3)는 writer 가 lo 를 쓰고 hi 를 쓰기 전에 reader 가 두 번 다 옛 hi 를 읽으면 찢어진 값을 받아들인다. 그런 코드는 이 레슨처럼 **주입 지점을 고정한 결정적 테스트** 로 확인한다. 반대로 칩에서만 생기는 문제(ISR 안 panic = 정지, 임계 구역 길이로 인한 IRQ 합쳐짐, 우선순위)는 호스트 테스트가 못 잡는다. 크로스 빌드 확인과 온타깃 테스트(RUST-A06)가 따로 필요하다.

### 어느 방법을 언제 쓰나

| 공유 상태 | 방법 | 인터럽트 지연 | M0(+) | 비고 |
|---|---|---|---|---|
| 카운터·플래그 (≤32비트) | `AtomicU32` / `AtomicBool` RMW | 0 | `portable-atomic` 필요 | 제일 가볍다 |
| 값 + valid (≤31비트 값) | 한 워드에 묶어 `swap` | 0 | 상동 | 우편함 |
| 작은 Copy 구조체·64비트 값 | `Mutex<Cell<T>>` | 복사 시간 | 그대로 됨 | `AtomicU64` 없는 M4 의 정답 |
| 런타임 init 드라이버 (`&mut` 필요) | `Mutex<RefCell<Option<T>>>` | `with` 클로저 시간 | 그대로 됨 | 재진입은 `try_with` |
| 바이트 스트림 ISR → main | lock-free SPSC 큐 (RUST-M07) | 0 | load/store 만으로 가능 | 대용량 |
| 우선순위 여러 개, 전역 끄기 싫다 | RTIC 리소스 (RUST-A02) | 필요한 우선순위만 | — | 컴파일 타임 락 |

## 6. 정답 코드

<!-- code: solution/src/lib.rs diff=starter/src/lib.rs -->

```bash
cd lessons/RUST/RUST-M06/solution          # rustup PATH · SDKROOT · CARGO_TARGET_DIR 는 3장과 같이
cargo test                                  # test result: ok. 25 passed; 0 failed
cargo clippy --all-targets -- -D warnings   # 경고 0
cargo build --target thumbv7em-none-eabihf  # OK (no_std)
cargo build --target thumbv6m-none-eabi     # E0599 10개 — 연습 4 에서 고친다
```

검증 기록: `cargo test` 를 디버그로 36번(6번 + 30번 반복), `--release` 1번, `--test-threads=1` 1번 돌려 모두 25/25 통과했다. 이 크레이트에는 `unsafe` 가 없다(`#![forbid(unsafe_code)]`). C 레슨의 ASan/UBSan/TSan 에 해당하는 Rust 도구(`-Zsanitizer=thread`, Miri)는 nightly 전용이라 이 맥(stable 만 설치)에서는 돌리지 않았다. safe Rust 만 쓴 코드에서는 데이터 레이스가 컴파일 단계에서 막히고, 남는 위험은 의존 크레이트의 `unsafe`(critical-section 의 `std`/`cortex-m` 구현)다.

## 7. 자주 하는 실수

**1. lint 만 피하기: `&raw const` + `read_volatile`.** 컴파일러의 `help:` 를 그대로 따르면 오류는 사라지지만 레이스는 그대로다. `volatile` 은 원자성이 아니다(C-B09). 고친 코드는 `static TICKS: AtomicU32` 다. 2021 크레이트를 2024 로 올리면서 `#[allow(static_mut_refs)]` 를 다는 것도 같은 실수다.

**2. load → store 로 "가져가고 비우기".** `let n = T.load(); T.store(0);` 은 사이에 온 tick 을 지운다(`a3`). `T.swap(0)` 한 번으로 한다. 플래그도 `if F.load() { handle(); F.store(false) }` 가 아니라 `if F.swap(false) { handle() }`.

**3. 임계 구역 안에서 일하기.**

```rust
// 틀림: 파싱·응답을 인터럽트 꺼진 채로
UART.with(|d| if let Some(l) = d.take_line() { parse_and_reply(l.as_bytes()) });
// 맞음: 복사만 안에서, 일은 밖에서
if let Some(l) = UART.take_line() { parse_and_reply(l.as_bytes()); }
```

**4. 클로저 안에서 같은 칸 다시 빌리기.** `UART.with(|d| { …; UART.take_line(); })` 는 `RefCell already borrowed` panic 이다(`b7`). 이미 `&mut UartRx` 를 쥐고 있으니 `d.take_line()` 을 직접 부른다. 경로가 복잡해 재진입 여부를 확신할 수 없으면 `try_with` 로 `Busy` 를 값으로 받는다. ISR 안에서는 특히 그렇다.

**5. 무조건 `Mutex<RefCell<T>>`.** 한 워드면 atomic, Copy 면 `Cell` 이 더 싸고 실수할 여지도 적다. `RefCell` 은 빌림 플래그(`isize`) 하나와 매 빌림의 검사·panic 경로를 추가한다.

**6. ISR 카운터를 `+= 1` 로.** 디버그 빌드에서 Rust 의 정수 덧셈은 넘치면 **panic** 이다. ISR 안이면 그대로 시스템 정지다. 언젠가 넘칠 카운터는 `wrapping_add` 로 쓰거나 atomic 의 `fetch_add`(정의상 wrap) 를 쓴다. 이 레슨의 `UartRx`/`Window` 카운터는 테스트 규모라 `+=` 로 두었다. 실제 펌웨어라면 `self.bytes = self.bytes.wrapping_add(1)`.

**7. 라이브러리가 구현체를 고르기.** 라이브러리 `Cargo.toml` 에 `cortex-m = { features = ["critical-section-single-core"] }` 나 `portable-atomic/unsafe-assume-single-core` 를 넣으면, 그 라이브러리를 RP2040(듀얼 코어)에서 쓰는 사람이 **unsound** 한 구현을 강제로 받는다. 구현체는 최종 바이너리가 고른다. 구현체가 둘 링크되면 심볼 중복 링크 오류가 난다(critical-section README).

## 8. 면접 Q&A

<details>
<summary>Q1. C 처럼 static mut COUNTER 로 ISR 과 공유하면 왜 안 되죠? edition 2021 에선 컴파일되잖아요.</summary>

2021 에서도 `static mut` 의 모든 접근은 `unsafe` 이고, ISR 의 쓰기와 main 의 비원자적 접근이 겹치면 데이터 레이스, 곧 UB 다. edition 2024 는 그중 특히 위험한 **참조 생성**(`&COUNTER`, `BUF.len()` 같은 암묵적 autoref 포함)을 `static_mut_refs` deny-by-default 로 막았다. `&T` 는 "사는 동안 불변" 이라는 약속인데 ISR 이 그 약속을 언제든 깰 수 있기 때문이다. `&raw const` 로 우회하면 컴파일은 되지만 고쳐진 것은 없다. 정답은 공유 상태를 `Sync` 타입(atomic, `critical_section::Mutex`)으로 바꾸는 것이다. 그러면 `unsafe` 가 사라지고 컴파일러가 접근 방식을 강제한다.

꼬리: "그럼 `static mut` 은 아예 못 쓰나요?" → 쓸 수는 있다. `&raw mut` 로 포인터를 만들어 DMA 레지스터에 넘기는 식의 저수준 코드에서는 여전히 쓰인다. cortex-m-rt 는 핸들러 지역 `static mut` 을 `&mut` 로 바꿔 준다(ISR 전용 상태). 다만 **공유** 상태에는 쓰지 않는다.

</details>

<details>
<summary>Q2. critical_section::Mutex 는 왜 &mut T 를 안 주고, 굳이 RefCell 을 한 겹 더 써야 하나요?</summary>

`CriticalSection` 토큰은 `Copy` 이고 임계 구역은 중첩된다. `borrow(cs)` 가 `&mut T` 를 준다면 같은 `with` 안에서 두 번 불러 `&mut` 둘을 만들 수 있고, 그건 unsound 다. 그래서 `Mutex` 는 "다른 컨텍스트(ISR) 가 동시에 못 들어온다"(= `Sync`)만 보장한다. "같은 컨텍스트 안에서 `&mut` 이 하나뿐" 은 `RefCell` 이 런타임에 검사한다. crate 문서의 설계 주석이 이렇게 설명한다. 비용이 싫으면 Copy 값은 `Cell`, 한 워드는 atomic 을 쓴다. `std::sync::Mutex` 는 이 두 역할(Sync + 내부 가변성)을 한 타입에 묶은 것이다.

</details>

<details>
<summary>Q3. AtomicU32 하나로 충분한 경우와 critical section 이 꼭 필요한 경우를 어떻게 구분하나요?</summary>

불변식이 **한 워드 안에** 있으면 atomic 으로 충분하다. 카운터, 플래그, 한 워드에 묶은 값+valid 가 그렇다. 불변식이 **여러 위치에 걸쳐** 있으면 각각을 atomic 으로 만들어도 소용없다. count 와 sum, hi 와 lo, 버퍼와 길이가 그렇다. `AdcStatsSplit` 이 `(3, 10)` 같은 존재한 적 없는 창을 만든 것이 증거다(`c2`, 진짜 스레드에서는 창의 89–98% 가 찢어졌다). 그때는 critical section 으로 통째로 바꾸거나, seqlock/재읽기(C-M14), 더블 버퍼, SPSC 큐 같은 프로토콜을 쓴다. 한 줄 요약: **원자적 부품의 합은 원자적 전체가 아니다.**

</details>

<details>
<summary>Q4. Cortex-M0+ 에서 AtomicU32::fetch_add 가 컴파일이 안 됩니다. 왜 그렇고, 어떻게 하죠?</summary>

ARMv6-M 에는 LDREX/STREX 가 없어 CAS/RMW 를 하드웨어로 할 수 없다. Rust 는 그런 타깃에서 `target_has_atomic="32"` 를 켜지 않고, `fetch_add`/`swap`/`compare_exchange` 메서드를 아예 제공하지 않는다(`load`/`store` 는 있다). 실제로 E0599 `no method named fetch_add found for struct Atomic<T>` 가 났다. 해결은 `portable-atomic` 이다. `critical-section` 기능이면 매 연산(load 포함)을 임계 구역으로 감싸고, 바이너리가 구현체를 고른다. `unsafe-assume-single-core` 기능이면 `mrs primask; cpsid i … msr primask` 를 인라인으로 넣는데, 단일 코어·특권 모드가 전제다. 둘 다 asm 으로 확인했다. 라이브러리는 기능을 강제하지 말고 바이너리가 고르게 한다. 어차피 critical section 안에서 하는 일이면 `Mutex<Cell<u32>>` 로 직접 쓰는 것도 방법이다(연습 1).

꼬리: "C 컴파일러는 어떻게 하나요?" → clang 은 `__atomic_fetch_add_4` 라이브러리 호출을 만들고 경고만 낸다(C-M14). 그 구현이 인터럽트를 끄는지는 런타임 라이브러리에 달렸다. Rust 는 컴파일 단계에서 선택을 강요한다.

</details>

<details>
<summary>Q5. 64비트 타임스탬프를 Cortex-M4 의 ISR 과 공유하려면요? AtomicU64 를 쓰면 되나요?</summary>

thumbv7em 에는 `target_has_atomic="64"` 가 없어서 `core::sync::atomic::AtomicU64` 자체가 없다(실제로 `cannot find type AtomicU64`). M4 의 `ldrd/strd` 도 64비트 단일 복사 원자성을 보장하지 않는다. 선택지는 셋이다. ① `Mutex<Cell<u64>>`: 임계 구역 안의 `ldrd/adds/adc/strd` 로 확인했다. ② ISR 은 32비트 두 워드만 쓰고 main 이 hi-lo-hi 로 재읽기(연습 3): 인터럽트 지연 0, 단 reader 가 ISR 일 때나 멀티코어에서는 안 된다. ③ 하드웨어 32비트 카운터 + 오버플로 카운트를 main 이 확장. portable-atomic 의 fallback 으로 `AtomicU64` 를 얻는 방법도 있지만, 그 구현 방식과 ISR 안전성은 이 레슨에서 확인하지 않았다(문서 확인 필요).

</details>

<details>
<summary>Q6. ISR ↔ main 공유에서 Ordering 은 뭘 써야 하나요? 단일 코어인데 Acquire/Release 가 필요한가요?</summary>

단일 코어에서 ISR 과 main 은 같은 코어라 하드웨어 재배치는 서로에게 보이지 않는다. 막아야 할 것은 **컴파일러 재배치** 다. 값 자체가 전부인 카운터는 `Relaxed` 로 충분하다. "버퍼를 채운 뒤 플래그를 올린다" 처럼 다른 데이터를 게시하는 경우는 Release(ISR) / Acquire(main) 로 순서를 표현한다. Rust 는 "다른 코어일 수도 있다" 고 보고 단일 코어에서도 장벽을 낸다. M4·M0+ 에서 Release store 앞에 `dmb sy` 가 생기는 것을 asm 으로 봤다. 비용이 문제면 `compiler_fence` 로 컴파일러만 막는 선택지가 있지만, DMA·멀티코어가 끼면 진짜 장벽이 필요하다. 자세한 것은 C-A03 / RUST-M07.

</details>

<details>
<summary>Q7. 호스트에서 스레드를 ISR 로 삼아 테스트하면 칩에서의 정확성이 보장되나요?</summary>

부분적으로만 보장된다. 보장되는 것: 공유 타입의 사용법(Sync, 빌림 규칙), atomic RMW·임계 구역 안 통째 갱신 같은 코드의 불변식이 모든 인터리빙에서 지켜지는지, 재진입 panic. 스레드는 진짜 병렬이라 오히려 더 가혹하다(naive 손실 92–98%). 보장되지 않는 것: "ISR 은 끝까지 실행되고 main 에게 선점되지 않는다" 를 이용한 코드(hi-lo-hi 는 스레드에서 오히려 틀린다), 인터럽트 우선순위와 지연, 임계 구역이 길어 IRQ 가 합쳐지는 현상, ISR 안 panic 이 곧 정지라는 점, 구현체 차이(호스트는 mutex, 칩은 PRIMASK). 그래서 ① 호스트 테스트(로직·불변식) ② 고정 주입점의 결정적 테스트(ISR 고유 가정) ③ `cargo build --target thumbv7em-none-eabihf` / thumbv6m 크로스 빌드 ④ 온타깃 테스트(probe-rs/defmt-test, RUST-A06)를 겹쳐 쓴다.

</details>

<details>
<summary>Q8. RP2040(듀얼 코어 M0+) 에서 cortex-m 의 critical-section-single-core 를 쓰면 어떻게 되죠?</summary>

unsound 하다. PRIMASK 는 **그 코어** 의 인터럽트만 막는다. 다른 코어는 같은 `Mutex` 안의 데이터를 계속 건드릴 수 있다. 이름 그대로 단일 코어 전용이고, `portable-atomic` 의 `unsafe-assume-single-core` 도 README 가 "멀티코어에서는 항상 unsound" 라고 경고한다. RP2040 은 `rp2040-hal`(또는 `embassy-rp`)의 `critical-section-impl` 이 하드웨어 스핀락 + 인터럽트 끄기로 두 코어를 다 막는다(critical-section README 의 구현체 목록 기준). 이것이 "인터럽트 끄기 = 상호 배제" 라는 단일 코어 가정이 깨지는 지점이다(RTOS-A08 이 시뮬레이션으로 보여 준다). 라이브러리가 구현체를 고르면 안 되는 이유이기도 하다.

</details>

<details>
<summary>Q9. RTIC 이나 Embassy 를 쓰면 이 Mutex<RefCell<..>> 가 필요 없나요?</summary>

RTIC 은 공유 리소스를 프레임워크가 선언·소유한다. 태스크 우선순위로 천장을 컴파일 타임에 계산해 `lock()` 이 **필요한 우선순위까지만** 마스크한다(Cortex-M3+ 는 BASEPRI, SRP = IPCP 계열, RUST-A02/RTOS-M05). 전역 `static` 도, 런타임 `RefCell` 검사도 없고, 같은 우선순위 태스크끼리는 락이 필요 없다. Embassy 는 async 태스크 사이 공유에 `embassy_sync` 의 `Mutex`/`Channel` 을 쓴다. ISR 과 공유할 때는 critical-section 기반의 blocking mutex(`Mutex<CriticalSectionRawMutex, RefCell<T>>` 형태, 문서 기준)를 쓴다. 결국 같은 원리를 프레임워크가 포장한 것이다. 이 레슨의 패턴은 프레임워크 없이 cortex-m-rt 만 쓸 때의 기본기다.

</details>

## 9. 추가 연습

### 연습 1 — fetch_or 없는 M0 용 에러 비트 래치

UART 프레이밍 오류, ADC 오버런처럼 여러 ISR 이 에러 비트를 OR 로 쌓고 main 이 읽으며 지우는 `ErrorLatch` 를 만들어라. `raise(mask) -> u32` 는 이전 값을 돌려준다(`fetch_or` 와 같은 계약). `take() -> u32` 는 가져가며 0 으로 만든다. thumbv6m 에서 빌드되어야 하고, ISR 스레드 둘(비트 0–15, 16–31)과 main 이 경쟁해도 모든 비트를 정확히 한 번씩 받아야 한다.

<details>
<summary>답 보기</summary>

`Mutex<Cell<u32>>` 안에서 `let old = c.get(); c.set(old | mask); old`, `take` 는 `replace(0)`. M4 라면 `AtomicU32::fetch_or`/`swap(0)` 이 `ldrex/orr/strex` 로 더 가볍다. M0 에는 그 메서드가 없으니 임계 구역이 정답이다. portable-atomic 의 `fetch_or` 도 결국 같은 일을 한다. 테스트 `p1_latch_two_isr_threads_no_bit_lost` 는 32비트가 모두 모이고(`seen == u32::MAX`), 어떤 비트도 두 번 오지 않았는지(`dup == false`)를 본다. "비트 OR 은 멱등이라 순서가 상관없다" 는 점도 래치가 단순해지는 이유다.

</details>

### 연습 2 — 제네릭 IrqShared<T>

Part B 의 `SharedUart` 를 아무 드라이버나 담는 `IrqShared<T>` 로 일반화하라: `const fn new()`, `install`, `take`, `try_with`. `static M: IrqShared<Motor>` 가 되어야 한다. `IrqShared<Rc<u8>>` 을 static 에 두면 어떻게 되는지 설명하라.

<details>
<summary>답 보기</summary>

본문의 메서드를 `T` 로 바꾸기만 하면 된다. `Mutex::new`, `RefCell::new`, `None` 이 모두 `const` 라 `const fn new()` 가 된다. static 에 두려면 `IrqShared<T>: Sync`, 곧 `Mutex<RefCell<Option<T>>>: Sync`, 곧 `T: Send` 가 필요하다. `Rc` 는 `Send` 가 아니라서 `demo_static_mut.rs --cfg bad_send` 와 같은 E0277(`Rc<u8> cannot be sent between threads safely … shared static variables must have a type that implements Sync`)이 난다. ISR 에 넘기는 것은 다른 스레드에 넘기는 것과 같기 때문이다. `p2_generic_cell_thread_isr` 는 `IrqShared<u64>` 에 ISR 스레드가 5만 번 `+1` 하는 동안 main 이 읽는 값이 감소하지 않고 마지막에 50,000 인지 본다.

</details>

### 연습 3 — AtomicU64 없이 64비트 시각: hi-lo-hi (C-M14 Part C 의 Rust 판)

`Uptime64 { hi: AtomicU32, lo: AtomicU32 }`. ISR 은 `isr_add(us)` 로 lo 를 더하고 자리올림이면 hi 를 올린다. main 의 `read(inject) -> (u64, tries)` 는 hi → lo → hi 를 읽고, 두 hi 가 다르면 다시 한다. `inject(k)` 는 k번째 시도의 hi 직후에 불리는 테스트용 ISR 주입점이다. 시작값 `0xFFFF_FC18`(lo = 2³² − 1000)에서 첫 시도에 1000 µs ISR 을 넣으면 naive 는 0, 재읽기는 `0x1_0000_0000`(2번 시도)이어야 한다. load/store 만 쓰므로 thumbv6m 에서도 빌드되는지 확인하라.

<details>
<summary>답 보기</summary>

`loop { h1 = hi.load(Acquire); inject(k); lo = lo.load(Acquire); h2 = hi.load(Relaxed); if h1 == h2 { return } }`. Acquire 는 뒤의 읽기가 앞으로 당겨지는 컴파일러 재배치를 막는다. 첫 Acquire 는 lo 읽기를, 두 번째는 h2 읽기를 지킨다. 단일 코어 ISR 모델에서는 ISR 이 끝까지 실행된 뒤 main 이 재개되므로 "h1 == h2 이면 lo 는 그 hi 와 짝" 이 성립한다. 그래서 이 연습은 **스레드 경쟁 테스트가 아니라** 주입점을 고정한 결정적 테스트로만 검증한다(5장 "스레드 모델은 어디서 다른가"). 진짜 병렬 writer 에서는 writer 가 lo 를 쓰고 hi 를 아직 안 쓴 사이에 reader 가 h1, lo, h2 를 다 읽어 찢어진 값을 받아들일 수 있다. reader 가 ISR 이거나 인터럽트가 막힌 상태에서 부르면 무한 루프나 오답이 되는 것도 C-M14 Q7 과 같다.

</details>

### 연습 4 — thumbv6m 포팅: cfg(target_has_atomic) 로 고르기

`CasCounter`(`isr_inc` = `fetch_add`, `take` = `swap(0)`)를 만들되, CAS 가 있는 타깃은 `core::sync::atomic::AtomicU32` 를, 없는 타깃은 `portable_atomic::AtomicU32` 를 쓰게 하라. `practice` 크레이트 전체가 `cargo build --target thumbv6m-none-eabi` 로 빌드되어야 한다.

<details>
<summary>답 보기</summary>

```rust
#[cfg(target_has_atomic = "32")]      use core::sync::atomic::AtomicU32 as CasU32;
#[cfg(not(target_has_atomic = "32"))] use portable_atomic::AtomicU32 as CasU32;
```

```toml
[target.'cfg(not(target_has_atomic = "32"))'.dependencies]
portable-atomic = { version = "1.15", default-features = false, features = ["critical-section"] }
```

Cargo 의 target 테이블은 `rustc --print cfg` 의 cfg 를 그대로 쓰므로 thumbv6m 에서만 portable-atomic 이 끌려온다. 실제로 `practice` 는 thumbv6m-none-eabi 와 thumbv7em-none-eabihf 에서 모두 `Finished` 됐고, clippy 도 두 타깃 모두 경고 0 이었다. 호스트와 M4 는 core 의 네이티브 atomic 을 쓰니 비용이 늘지 않는다. 앞서 말한 대로 재사용 라이브러리라면 `critical-section` 기능은 바이너리가 켜게 두고, 라이브러리는 `portable-atomic` 을 기능 없이 의존하는 편이 README 권고에 맞다. 칩 바이너리는 구현체(`cortex-m` + `critical-section-single-core`)를 함께 링크해야 한다.

</details>

### 한 번에 검증

<!-- code: practice/src/lib.rs -->

<!-- code: practice/Cargo.toml -->

```bash
cd lessons/RUST/RUST-M06/practice           # rustup PATH · SDKROOT · CARGO_TARGET_DIR 는 3장과 같이
cargo test                                  # test result: ok. 8 passed; 0 failed
cargo clippy --all-targets -- -D warnings
cargo build --target thumbv6m-none-eabi     # OK — 연습 4
cargo build --target thumbv7em-none-eabihf  # OK
```

관찰용 파일:

<!-- code: demo_static_mut.rs -->

<!-- code: demo_targets/src/lib.rs -->

<!-- code: demo_race/src/main.rs -->

## 10. 핵심 정리

- C 의 `volatile` 전역을 `static mut` 으로 옮기지 않는다. edition 2024 는 `&STATIC_MUT`(암묵적 autoref 포함)를 `static_mut_refs` **오류** 로 막는다. `&raw const` 우회는 고친 것이 아니다.
- Rust 는 ISR 을 다른 스레드로 본다. `static` 은 `Sync` 여야 하고(`RefCell` 은 E0277), `critical_section::Mutex<T>` 는 `T: Send` 일 때 `Sync` 다.
- 한 워드면 **atomic RMW**: `fetch_add`, `swap(0)`(가져가며 비우기), `swap(false)`(확인하며 내리기), 값+valid 를 한 워드에. load → store 두 단계는 tick 을 잃는다.
- 여러 필드면 **critical section 안에서 통째로**: `Mutex<Cell<T>>`(Copy), `Mutex<RefCell<Option<T>>>`(`&mut` 필요 + 런타임 init). `Mutex` 는 Sync 만, `&mut` 유일성은 `RefCell` 이 검사한다. 재진입은 panic 이니 `try_with`.
- 임계 구역은 짧게: 복사만 안에서, 일은 밖에서. 칩의 `with` = `mrs primask; cpsid i … msr primask` (cortex-m `critical-section-single-core`).
- M4 에는 `AtomicU64` 가 없다(64비트는 critical section 이나 hi-lo-hi). M0(+) 에는 CAS 가 없어 `fetch_add`/`swap` 이 **없다**. `portable-atomic`(critical-section / unsafe-assume-single-core)으로 해결하되, 구현체와 기능은 바이너리가 고른다. single-core 구현은 멀티코어에서 unsound.
- 호스트 스레드 ISR 테스트는 불변식과 빌림 규칙을 잡지만, "ISR 은 선점되지 않는다" 에 기대는 코드와 지연·우선순위는 못 잡는다. 결정적 주입 테스트 + 크로스 빌드 + 온타깃 테스트를 겹친다.

## 참고자료

- [Rust Edition Guide — static mut references (2024)](https://doc.rust-lang.org/edition-guide/rust-2024/static-mut-references.html)
- [critical-section 크레이트 (Mutex 설계 주석, std 구현, Undefined reference errors)](https://docs.rs/critical-section) — 이 레슨은 1.2.0 소스(`src/mutex.rs`, `src/std.rs`, `README.md`)를 직접 읽음
- [cortex-m 크레이트 — critical-section-single-core 구현](https://docs.rs/cortex-m) — 0.7.10 `src/critical_section.rs`
- [cortex-m-rt — #[interrupt]/#[exception] 의 핸들러 지역 static mut](https://docs.rs/cortex-m-rt) — 0.7.7 `src/lib.rs` 문서
- [portable-atomic — critical-section / unsafe-assume-single-core 기능과 경고](https://docs.rs/portable-atomic) — 1.15.0 `README.md`
- [The Embedded Rust Book — Concurrency (Mutex, Cell, RefCell, atomics)](https://docs.rust-embedded.org/book/concurrency/)
- [Rust Atomics and Locks (Mara Bos) — 1장 Send/Sync, 2–3장 atomics·memory ordering](https://marabos.nl/atomics/)
- [ARMv7-M Architecture Reference Manual (DDI0403) — A3.4 배타적 접근(LDREX/STREX, 모니터), B1.5 예외 모델](https://developer.arm.com/documentation/ddi0403/latest/)
- [ARMv6-M Architecture Reference Manual (DDI0419) — 배타적 접근 명령 없음](https://developer.arm.com/documentation/ddi0419/latest/)
- [RTIC Book — 리소스와 우선순위 천장](https://rtic.rs)
- 관련 레슨: C-M14 (ISR 공유 데이터·PRIMASK·재읽기), C-A03 (C11 atomics), CPP-A04 (std::atomic 과 Cortex-M), CPP-B06 (RAII 임계 구역 가드), RUST-M07 (SPSC 큐), RUST-A02 (RTIC), RTOS-A08 (SMP 에서 깨지는 단일 코어 가정)
