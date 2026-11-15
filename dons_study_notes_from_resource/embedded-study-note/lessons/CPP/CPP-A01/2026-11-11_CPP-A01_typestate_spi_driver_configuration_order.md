---
id: CPP-A01
time: 110분
updated: 2026-11-11
---
# CPP-A01 Typestate 패턴: 설정 순서를 타입으로 강제

## 학습 목표

- SPI 드라이버를 `Spi<Disabled>` → `configure()` → `Spi<Configured>` → `enable()` → `Spi<Enabled>` (+ `disable()` 로 되돌아가기) 로 설계하고, **잘못된 순서가 컴파일 에러** 임을 requires-expression + `static_assert` 로 증명한다.
- "비용 0" 을 말로 하지 않고 **보여 준다**: `sizeof == 1`, trivially copyable, Cortex-M4 asm 에서 typestate 드라이버와 C 스타일 드라이버의 명령 수를 나란히 놓는다 (clang 21 · arm-none-eabi-g++ 14.2.1, `-O2` / `-Os`). 숫자가 다르면 **왜** 다른지까지.
- C++ typestate 의 구멍을 직접 말할 수 있다: Rust 와 달리 **moved-from 객체도 이름이 살아 있고 타입도 그대로** 다. 완화책 4가지(`&&` 한정 전이, prvalue 체인, `[[nodiscard]]`, clang-tidy `bugprone-use-after-move` / clang `-Wconsumed`)를 실제 출력으로 비교한다.
- 상태를 **런타임에만** 아는 경우(부트로더 핸드오프)를 `std::variant` 로 받아(`adopt`) 원하는 상태로 정규화(`normalize`)하고, 그 비용(`std::visit` 의 abort 경로, 바이트 수)을 실측으로 판단한다.
- 실패할 수 있는 전이는 **원래 상태를 돌려준다** 는 설계와, 주변장치 하나에 객체 하나만 있게 하는 **소유권 토큰** 을 구현한다.

## 1. 문제

"설정 안 하고 enable 하면?", "enable 안 하고 전송하면?" — 펌웨어 버그의 상당수는 **순서** 버그다. C 드라이버는 이걸 `assert(spi->state == ENABLED)` 나 문서로 막지만, 둘 다 실행해 봐야(또는 읽어 봐야) 안다. 시니어 펌웨어 면접에서 C++ 를 쓴다고 하면 "그걸 타입으로 막을 수 있나요? 공짜인가요? 구멍은?" 이 세트로 나온다.

이미 배운 것은 다시 하지 않는다: 접근 권한을 타입에 담는 `Reg<Addr,T,Access>` 와 mock 버스 치환(CPP-M01), RAII 가드(CPP-B06), 템플릿 함수 제약·개념 문법(CPP-B12, MCPP-M05), `Result` 설계(CPP-M11). 이 레슨의 새 내용은 **"레지스터가 무엇을 허용하나" 가 아니라 "지금 이 주변장치가 어느 상태인가" 를 타입에 담는 것** 이다. Rust 쪽 같은 문제는 RUST-M04 (Typestate GPIO) — 거기서는 move 가 원본을 컴파일러 차원에서 죽이지만, C++ 는 그렇지 않다. 이 차이가 Part B 의 절반이다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">상태 = 타입. 화살표 = && 한정 멤버 함수 (부르면 새 타입의 객체가 나온다)</text>
  <rect x="20" y="60" width="150" height="56" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="40" y="84" fill="currentColor" font-family="monospace">Spi&lt;Disabled&gt;</text>
  <text x="40" y="103" fill="currentColor" font-size="12">CR1 = 0</text>
  <rect x="290" y="60" width="160" height="56" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="305" y="84" fill="currentColor" font-family="monospace">Spi&lt;Configured&gt;</text>
  <text x="305" y="103" fill="currentColor" font-size="12">CR1 = cfg, SPE = 0</text>
  <rect x="570" y="60" width="150" height="56" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="590" y="84" fill="currentColor" font-family="monospace">Spi&lt;Enabled&gt;</text>
  <text x="590" y="103" fill="currentColor" font-size="12">SPE = 1, transfer()</text>
  <g stroke="currentColor" fill="none" stroke-width="1.5">
    <path d="M170 76 L286 76"/><path d="M280 71 L288 76 L280 81"/>
    <path d="M450 76 L566 76"/><path d="M560 71 L568 76 L560 81"/>
    <path d="M570 104 L454 104"/><path d="M460 99 L452 104 L460 109"/>
    <path d="M290 104 L174 104"/><path d="M180 99 L172 104 L180 109"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="190" y="68">configure(c)</text>
    <text x="475" y="68">enable()</text>
    <text x="465" y="124">disable() *</text>
    <text x="200" y="124">reset()</text>
  </g>
  <rect x="20" y="150" width="150" height="40" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="32" y="175" fill="currentColor" font-family="monospace" font-size="12">PeriphToken&lt;Hw&gt;</text>
  <g stroke="currentColor" fill="none" stroke-width="1.5">
    <path d="M60 150 L60 120"/><path d="M55 126 L60 118 L65 126"/>
    <path d="M130 120 L130 150"/><path d="M125 144 L130 152 L135 144"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="66" y="140" font-family="monospace">from_reset</text>
    <text x="136" y="140" font-family="monospace">release</text>
    <text x="190" y="166">take() 는 프로그램 전체에서 한 번 → SPI1 을 가진 객체는 하나</text>
    <text x="190" y="186">* disable 은 실패할 수 있다 (BSY): 실패하면 Spi&lt;Enabled&gt; 를 그대로 돌려받는다</text>
    <text x="20" y="222">상태를 모를 때: adopt(token) → std::variant&lt;Spi&lt;Disabled&gt;, Spi&lt;Configured&gt;, Spi&lt;Enabled&gt;&gt;</text>
    <text x="20" y="240">→ normalize(variant, cfg) → 다시 하나의 정적 타입으로 들어온다 (Part B)</text>
  </g>
</svg>
```

하드웨어는 STM32F4 류 SPI 블록이다: CR1(0x00) = CPHA·CPOL·MSTR·BR[2:0]·SPE(bit6)·LSBFIRST·SSI·SSM, SR(0x08) = RXNE·TXE·BSY(bit7), DR(0x0C). 비트 위치는 **RM0090 대표값** 으로 쓰고, 테스트는 mock 이 한다. mock 은 접근을 전부 기록하고 하드웨어 규칙 위반을 센다: `cfg_while_on` = **SPE=1 인 채로 SPE 이외의 CR1 비트를 바꾼 쓰기** (RM 은 통신 중 BR/CPOL/CPHA 변경을 금지한다), `dropped_tx` = 꺼진 상태에서 DR 쓰기.

### Part A — 상태 전이와 "컴파일 안 됨" 증명

- `configure(c) &&` (Disabled 전용): CR1 에 `encode_cr1(c)` 를 **한 번** 쓴다. `enable() &&` (Configured 전용): SPE 만 RMW 로 켠다.
- `transfer(tx) &` (Enabled 전용): TXE 대기 → DR 쓰기 → RXNE 대기 → DR 읽기, 각 대기는 `max_polls` 로 제한.
- `disable(max_polls) &&` (Enabled 전용): BSY=0 을 기다린 뒤 SPE 만 끈다. 반환형은 `Fallible<Spi<Configured>, Spi<Enabled>>` = `std::variant` — 실패하면 아무것도 쓰지 않고 **자기 자신** 을 돌려준다.
- 하네스의 `static_assert` 15개: Configured 에서 transfer, 상태 건너뛰기, `std::move` 없는 전이, 복사, 토큰 위조, SPI1 토큰으로 SPI2, 켜진 채 `release` 가 전부 "안 됨" 이고, 객체는 빈 클래스·trivially copyable 이다.

### Part B — 어려운 부분: 런타임 상태, 소유권, moved-from

- `adopt(token)`: CR1 을 한 번 읽고 **아무것도 쓰지 않고** 상태를 판별해 `AnySpi<Hw>` (variant) 로 돌려준다.
- `normalize(any, cfg)`: 어떤 상태든 원하는 설정의 `Spi<Configured>` 로. 켜져 있으면 disable → reset → configure. disable 이 실패하면 받은 Enabled 를 그대로 돌려준다. **켜진 채로 설정을 바꾸면 안 된다** (`cfg_while_on == 0`).
- 주어진 `PeriphToken<Hw>::take()` (한 번만 성공) 과, 주어진 `buggy_sleep_then_send` (disable 로 넘긴 `en` 을 다시 쓰는 버그 — **컴파일된다**) 의 동작을 테스트로 확인한다.

### Part C — 부트로더 핸드오프 + SPI NOR JEDEC ID

- `read_jedec_id(Spi<Enabled>&)`: CS low 동안 `9Fh` + 더미 3바이트 → `0xEF4018` (W25Q128 류, 대표값). 전송 실패 시 nullopt, CS 는 반드시 올린다 (CPP-B06 RAII).
- `boot_flash(token, cfg, expect_id)`: adopt → normalize → enable → ID 확인. 부트로더가 SPI 를 Mode3/Div2 로 켜 둔 채 넘겨줘도 위반 없이, BSY 가 안 풀리면 설정을 건드리지 않고, **어느 경로든 SPI 객체(토큰)를 결과 안에 담아** 돌려준다.

## 2. Problem (English)

> **Typestate SPI driver.** Model an STM32-style SPI peripheral (CR1 @0x00, SR @0x08, DR @0x0C) so that the *state* of the peripheral is part of the C++ type.
>
> 1. `template <SpiState S, class Hw> class Spi` with `S ∈ {Disabled, Configured, Enabled}`. Transitions are `&&`-qualified and `[[nodiscard]]`: `Spi<Disabled>::configure(Config) && -> Spi<Configured>`, `Spi<Configured>::enable() && -> Spi<Enabled>`, `Spi<Configured>::reset() && -> Spi<Disabled>`, `Spi<Enabled>::disable(unsigned max_polls) && -> std::variant<Spi<Configured>, Spi<Enabled>>` (on BSY timeout, return the original object and write nothing). `transfer(uint8_t) & -> Xfer{ok, rx}` exists only on `Spi<Enabled>`.
> 2. Prove with `static_assert` + requires-expressions that calling `transfer` before `enable`, skipping a state, transitioning an lvalue, copying a state object, forging an ownership token, or using an SPI1 token for SPI2 does not compile. Prove `sizeof(Spi<Enabled>) == 1` and that it is trivially copyable.
> 3. Ownership: `PeriphToken<Hw>::take()` returns the only token for that peripheral (then `nullopt`). `Spi<Disabled>::from_reset(token&&)` assumes reset state.
> 4. Unknown state at boot: `adopt(token&&) -> std::variant<Spi<Disabled>, Spi<Configured>, Spi<Enabled>>` reads CR1 once, writes nothing. `normalize(variant&&, Config) -> variant` brings any state to `Configured` with the requested config without ever changing configuration bits while `SPE=1`.
> 5. `boot_flash(token, cfg, expected_jedec_id)` reads the JEDEC ID (`0x9F`) of a SPI NOR flash under a chip-select guard and returns the SPI object in whatever state it ended up in.
>
> Constraints: `-std=c++20 -fno-exceptions -fno-rtti`, no heap, polls are bounded. Show on a Cortex-M4 object that the typestate driver emits the same register accesses as a hand-written C driver.
>
> Follow-ups: Rust consumes `self` — what does C++ do with the moved-from object? What would a destructor that disables the peripheral cost? How do you handle state you only learn at runtime? What does `std::visit` add to a `-fno-exceptions` build? Why return the old state from a failed transition?

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
```

처음 실행 결과 (실제 출력, PASS 줄 생략 — 줄 번호가 같은 `false` 4개는 "Part A 경로가 끝까지 못 갔다" 표시):

```text
-- Part A: Disabled → Configured → Enabled → (disable) --
FAIL starter.cpp:340  g_mock[0].log_is({{'W', reg::CR1, 0x314u}})
FAIL starter.cpp:344  g_mock[0].log_is({{'R', reg::CR1, 0x314u}, {'W', reg::CR1, 0x354u}})
FAIL starter.cpp:349  x.ok && x.rx == 0xA5u
FAIL starter.cpp:350  g_mock[0].log_is({{'R', reg::SR, 0x2u}, {'W', reg::DR, 0x5Au}, {'R', reg::SR, 0x3u}, {'R', reg::DR, 0xA5u}})
FAIL starter.cpp:359  all_ok && g_mock[0].dropped_tx == 0
FAIL starter.cpp:363  r.index() == 0u
FAIL starter.cpp:364  g_mock[0].log_is({{'R', reg::SR, 0x2u}, {'R', reg::CR1, 0x354u}, {'W', reg::CR1, 0x314u}})
FAIL starter.cpp:381  false        (×4)
-- Part B: 소유권 토큰, 실패하는 전이, 런타임 상태 (variant) --
FAIL starter.cpp:398  a.index() == 0u && g_mock[0].log_is({{'R', reg::CR1, 0u}})
FAIL starter.cpp:403  a.index() == 1u && g_mock[0].writes_to(reg::CR1) == 0
FAIL starter.cpp:408  a.index() == 2u && g_mock[0].n == 1
FAIL starter.cpp:415  a.index() == 1u && g_mock[0].cr1 == 0x314u
FAIL starter.cpp:416  g_mock[0].cr1_writes_are({0x307u, 0u, 0x314u}) && g_mock[0].cfg_while_on == 0
FAIL starter.cpp:421  a.index() == 1u && g_mock[0].cr1_writes_are({0u, 0x314u})
FAIL starter.cpp:426  a.index() == 1u && g_mock[0].cr1_writes_are({0x314u})
FAIL starter.cpp:431  a.index() == 2u && g_mock[0].writes_to(reg::CR1) == 0 && g_mock[0].cfg_while_on == 0
FAIL starter.cpp:440  r.index() == 1u && (g_mock[0].cr1 & reg::CR1_SPE) != 0u
FAIL starter.cpp:443  back->transfer(0x0F).rx == 0xF0u
FAIL starter.cpp:449  r2.index() == 0u && sr_reads == 4 && g_mock[0].cr1 == 0x314u
FAIL starter.cpp:461  !x.ok && g_mock[0].dropped_tx == 1
-- Part C: 부트로더 핸드오프 + SPI NOR JEDEC ID --
FAIL starter.cpp:471  b.ok && b.jedec == kId && b.spi.index() == 2u
...  (Part C 나머지 7개)
9/40 passed
```

하네스 설계: 타입 쪽(상태 태그, 멤버 선언, requires-clause, 토큰)은 시작 코드에 **이미 완성** 되어 있다 — 그래서 `static_assert` 15개는 처음부터 통과하고, 여러분이 채우는 것은 함수 몸통(레지스터 순서와 런타임 판단)이다. 스텁은 "틀렸지만 안전" 하다: 쓰기를 안 하거나, `disable` 이 항상 실패(자기 자신을 돌려줌)하거나, `adopt` 가 항상 Disabled 라고 가정한다 — 마지막 것이 바로 이 레슨이 막으려는 버그다.

관찰용 파일 (하네스와 별개, 결과는 5. 풀이에 실제 출력으로):

```bash
bash asm_targets.sh asm         # demo_target.cpp → thumbv7em (-Os, -O2) 명령 수 + 디스어셈블 + size/nm
bash asm_targets.sh errors      # demo_errors.cpp -DSHOW_ERROR=1..9 → 실제 에러 (각각 실패해야 정상)
bash asm_targets.sh tidy        # demo_moved_from.cpp → clang-tidy bugprone-use-after-move, clang -Wconsumed
bash asm_targets.sh gcc         # Lima VM elinux: arm-none-eabi-g++ 14.2.1 로 같은 비교 + GCC 에러 메시지
bash asm_targets.sh moveassign  # 이동 대입 delete vs default → std::visit 의 abort 경로
bash asm_targets.sh all         # 전부, 성공하면 "all OK" / exit 0
```

`spi_typestate.hpp` 는 solution.cpp 의 드라이버 부분과 같은 내용이다 (demo 전용). `ARM_INC` 가 없으면 스크립트가 VM 에서 arm-none-eabi 헤더를 `/tmp/arm-none-eabi-include` 로 한 번 복사한다 (CPP-A04 와 같은 방식).

## 4. 힌트

<details>
<summary>힌트 1 — 상태 객체는 "증명서" 일 뿐, 데이터를 들고 있지 않다</summary>

`Spi<Configured>` 객체가 존재한다는 것 자체가 "CR1 이 설정됐고 SPE=0" 이라는 증명이다. 설정 값은 하드웨어(CR1)에 있으니 객체에 저장할 필요가 없다. 그래서 `enable()` 은 `Hw::read(CR1) | SPE` 를 쓰면 되고, 새 상태 객체는 토큰을 옮겨 담아 `Spi<Enabled, Hw>{std::move(tok_)}` 로 만든다 (private 생성자, 모든 `Spi<S,Hw>` 가 서로 friend).

</details>

<details>
<summary>힌트 2 — 실패한 전이가 "이전 상태" 를 잃으면 안 된다</summary>

`disable()` 이 `std::optional<Spi<Configured>>` 를 돌려준다고 해 보자. BSY 가 안 풀려서 nullopt 가 나오면, 호출자는 `std::move(en)` 으로 이미 Enabled 를 넘겼으므로 **어떤 상태 객체도 손에 없다** — 하드웨어는 켜져 있는데 그걸 쓸 수 있는 타입이 사라진다. 그래서 반환형은 `std::variant<Spi<Configured>, Spi<Enabled>>`, 실패 경로는 `std::in_place_index<1>, std::move(*this)`.

</details>

<details>
<summary>힌트 3 — adopt 는 읽기 한 번, normalize 는 if constexpr</summary>

`adopt`: SPE 를 **먼저** 본다 (SPE=1 이면 CR1 ≠ 0 도 참이므로 순서가 바뀌면 Enabled 를 Configured 로 오판한다). `normalize`: `std::visit([&](auto&& s) -> AnySpi<Hw> { ... }, std::move(any))` 안에서 `typename std::remove_cvref_t<decltype(s)>::state` 로 분기한다. 각 분기의 반환값(`Spi<Configured>` prvalue)은 variant 의 변환 생성자로 들어간다.

</details>

<details>
<summary>힌트 4 — boot_flash 에서 토큰을 잃지 않는 법</summary>

`normalize` 결과에서 `std::get_if<Spi<Configured, Hw>>(&any)` 가 null 이면 (끄지 못함) `any` 를 통째로 돌려준다. 아니면 `std::move(*cfg).enable()` 로 Enabled 를 만들고, ID 를 읽은 다음 `AnySpi<Hw>{std::move(en)}` 로 담아 돌려준다. ID 가 틀려도 SPI 는 Enabled 로 남는다 — 재시도·진단을 위해.

</details>

## 5. 풀이

### Part A — 상태 = 타입, 전이 = `&&` 멤버 함수

핵심 선언 네 줄 (solution.cpp):

```cpp
[[nodiscard]] Spi<Configured, Hw> configure(const Config& c) && noexcept requires std::same_as<S, Disabled>;
[[nodiscard]] Spi<Enabled, Hw>    enable() && noexcept                requires std::same_as<S, Configured>;
[[nodiscard]] Xfer transfer(std::uint8_t tx, unsigned max_polls = kPolls) & noexcept requires std::same_as<S, Enabled>;
[[nodiscard]] Fallible<Spi<Configured, Hw>, Spi> disable(unsigned max_polls = kPolls) && noexcept requires std::same_as<S, Enabled>;
```

각 장치가 막는 실수:

| 장치 | 문법 근거 | 막는 것 | 실패 시 메시지 (clang 21, 실제) |
|---|---|---|---|
| trailing `requires` | [dcl.decl], [over.match.viable] (제약 불만족 → 후보 아님) | 상태에 없는 연산 | `invalid reference to function 'transfer': constraints not satisfied` + `because 'std::same_as<Configured, Enabled>' evaluated to false` |
| `&&` ref-qualifier | [over.match.funcs] (암시적 객체 매개변수가 rvalue 참조) | 이름 있는 객체에서 "몰래" 전이 | `'this' argument to member function 'configure' is an lvalue, but function has rvalue ref-qualifier` |
| `&` ref-qualifier (transfer) | 같음 | 임시 Enabled 에 전송 (토큰을 그 자리에서 버림) | 하네스 `!CanTransferRvalue` |
| `[[nodiscard]]` | [dcl.attr.nodiscard] | 전이 결과를 버림 (상태 객체·토큰 분실) | `-Werror,-Wunused-result` |
| 복사 delete, 생성자 private | [class.copy.ctor], [class.access] | 상태 복제, 맨손으로 Enabled 만들기 | `call to deleted constructor` |

**왜 클래스 하나 + requires 인가 (특수화 3개가 아니라).** 특수화 `template<> class Spi<Enabled, Hw>` 3개로 나누는 방식도 흔하다. 그러면 Configured 에 `transfer` 가 아예 없어서 에러가 `no member named 'transfer'` 가 된다 — **"왜"** 가 사라진다. requires 방식은 `because 'std::same_as<Configured, Enabled>' evaluated to false` 로 현재 상태와 필요한 상태를 둘 다 말해 준다. 자유 함수 오버로드 집합(`xfer_free(Spi<Enabled>&, ...)`)과도 비교했다 (SHOW_ERROR=9):

```text
demo_errors.cpp:40:11: error: no matching function for call to 'xfer_free'
demo_errors.cpp:14:30: note: candidate function not viable: no known conversion from 'Spi<Configured, FakeHw>' to 'Sp<Enabled> &' (aka 'Spi<Enabled, FakeHw> &') for 1st argument
```

같은 정보지만 "변환이 안 된다" 로 돌려 말한다. GCC 14.2.1 은 requires 방식에서 이렇게 낸다 (VM, 실제):

```text
demo_errors.cpp:20:21: error: no matching function for call to 'Spi<Configured, FakeHw>::transfer(int)'
spi_typestate.hpp:108:24: note: candidate: 'Xfer Spi<S, Hw>::transfer(uint8_t, unsigned int) & requires  same_as<S, Enabled> [with S = Configured; Hw = FakeHw; uint8_t = unsigned char]'
spi_typestate.hpp:108:24: note: constraints not satisfied
```

**컴파일 실패를 하네스 안에서 증명하기.** "컴파일 안 됨" 을 테스트하려면 실패를 `false` 로 바꿔야 한다. requires-expression 을 **개념(concept) 안에** 두면 대체 실패가 `false` 가 된다 ([expr.prim.req]). 템플릿 밖의 맨 requires-expression 에 잘못된 식을 넣는 것은 표준상 ill-formed 라서 (clang 이 관대하게 넘어가더라도) 이식성이 없다.

```cpp
template <class T> concept CanTransfer      = requires(T& s, std::uint8_t b) { s.transfer(b); };
template <class T> concept CanConfigureLval = requires(T& s, Config c) { s.configure(c); };
static_assert(CanTransfer<S1<Enabled>> && !CanTransfer<S1<Configured>> && !CanTransfer<S1<Disabled>>);
static_assert(!CanConfigureLval<S1<Disabled>>, "전이는 && — std::move 없이 부르면 컴파일 에러");
```

양성(`CanTransfer<S1<Enabled>>`)도 꼭 같이 둔다. 개념에 오타가 있으면 **모든 것이 false** 가 되어 음성 단언이 공짜로 통과하기 때문이다. 이 단언이 진짜로 일하는지 확인하려고 solution 사본에서 `transfer` 의 requires 를 지웠더니 `static assertion failed due to requirement '!CanTransfer<Spi<Configured, MockHw<0>>>'` 로 빌드가 멈췄고, `configure` 의 `&&` 를 지웠더니 `!CanConfigureLval` 단언이 멈췄다 (실측).

**레지스터 순서 (테스트가 고정하는 로그).**

| 전이 | mock 로그 (정확히) | 이유 |
|---|---|---|
| `configure(Config{})` | `W CR1 0x314` | SPE=0 일 때 한 번에 설정. MSTR·BR=Div8·SSI·SSM |
| `enable()` | `R CR1 0x314`, `W CR1 0x354` | SPE 만 추가 (RMW). 설정 값을 객체가 기억하지 않으니 읽는다 |
| `transfer(0x5A)` | `R SR 0x2`, `W DR 0x5A`, `R SR 0x3`, `R DR 0xA5` | TXE → 쓰기 → RXNE → 읽기 (RXNE 는 DR 읽기로 클리어) |
| `disable()` | `R SR 0x2`, `R CR1 0x354`, `W CR1 0x314` | BSY=0 확인 후 SPE 만 끈다 |

`disable` 은 RM 의 절차(마지막 데이터 수신 → TXE=1 → BSY=0 → SPE 클리어)를 BSY 대기 하나로 단순화한 **대표값** 이다. 실제 칩 드라이버라면 모드(전이중/수신 전용)에 따라 절차가 다르다.

**`disable()` 후에도 설정이 남는다.** 그래서 Configured 로 돌아가고, 다시 `enable()` 만 하면 된다 (Part A 테스트: `cr1 == 0x354`, `transfer(0x01).rx == 0xFE`). 토큰을 반납하려면 `reset()` → Disabled → `release()` — `release` 를 Disabled 에만 둔 이유는 "켜진 주변장치의 소유권을 놓으면 아무도 끌 수 없다" 이다.

### Part A — 비용 0 의 증거: 타입 쪽

```cpp
static_assert(sizeof(S1<Enabled>) == 1 && std::is_empty_v<S1<Enabled>> && std::is_empty_v<PeriphToken<H1>>);
static_assert(std::is_trivially_copyable_v<S1<Enabled>> && std::is_trivially_destructible_v<S1<Enabled>>);
```

- 토큰 멤버에 `[[no_unique_address]]` ([dcl.attr.nouniqueaddr]) — Spi 자체가 빈 클래스로 남는다 (`is_empty` 참).
- 복사는 delete, 이동은 `= default` → 유일하게 쓸 수 있는 복사/이동 연산이 trivial 이므로 trivially copyable ([class.prop]). C++ ABI (Itanium, ARM C++ ABI 도 따름) 에서 "trivial for the purposes of calls" 인 타입은 숨은 참조 없이 일반 값처럼 레지스터로 전달된다 — 소멸자 호출이나 숨은 포인터가 없다.
- 소멸자는 trivial: **하드웨어를 건드리지 않는다.** 이건 의도적인 결정이다 (Q4 참고).

### Part A — 비용 0 의 증거: Cortex-M4 asm

`demo_target.cpp` 는 같은 레지스터 접근을 하는 함수 쌍을 담는다. `c_bringup_send_x` 는 **대조군** 이다 — typestate 없이, 같은 `Xfer{ok, rx}` 구조체를 돌려주는 C 스타일 inline 함수. 차이가 typestate 탓인지 `Xfer` 탓인지 가르려고 넣었다. `bash asm_targets.sh asm` / `gcc` 의 실제 명령 수 (리터럴 풀 `.word` 제외):

| 함수 | clang 21 `-O2` | clang 21 `-Os` | GCC 14.2.1 `-O2` | GCC 14.2.1 `-Os` |
|---|---|---|---|---|
| `ts_bringup_send` (from_reset→configure→enable→transfer) | **171** | 18 + `transfer` 호출 | 39 | 16 + 호출 2개 |
| `c_bringup_send` (C, 같은 접근) | **171** | 28 | 31 | 26 |
| `c_bringup_send_x` (C + `Xfer` 대조군) | **171** | 28 | 41 | 34 |
| `ts_send(Spi<Enabled>&, b)` | **166** | 6 + 호출 | 23 | 6 + 호출 |
| `ts_send_val(Spi<Enabled>, b)` (값 전달) | **166** | 10 + 호출 | 23 | 8 + 호출 |
| `c_send(b)` | **166** | 23 | 27 | 22 |
| 공유: `Spi<Enabled>::transfer` (weak, 한 벌) | — | 26 | — | 30 |
| 공유: `configure.constprop` / `disable` (GCC) | — | — | — | 7 / 16 |

읽는 법:

1. **clang `-O2`: 세 줄이 정확히 같다 (171/171/171, 166/166/166).** 숫자가 큰 것은 1000번 폴링 루프를 clang 이 펼쳤기(unroll) 때문이고, 양쪽이 똑같이 펼쳤다. 상태 타입, 토큰, 빈 객체 인자는 명령을 하나도 남기지 않았다.
2. **`-Os`: typestate 쪽이 오히려 짧다.** 크기 최적화에서 clang·GCC 모두 `transfer` 를 인라인하지 않고 **한 벌만** 만들어(템플릿 멤버 = weak/COMDAT) 호출한다. C 쪽은 함수마다 같은 루프를 복제했다. 이건 typestate 의 공이 아니라 "함수로 묶었다" 의 효과지만, 적어도 **비용은 아니다**.
3. **GCC `-O2` 의 39 vs 31 은 typestate 탓이 아니다.** 대조군 `c_bringup_send_x` 가 41 — `Xfer` 구조체를 쓴 C 코드도 똑같이(더) 길다. 디스어셈블에서 원인이 보인다: GCC 가 `Xfer` 를 완전히 스칼라화하지 못해 `sub sp, #12` 스택 프레임과 접힌 상수 비교(`movs r3, #1; cmp r3, #0; it eq; moveq r0, #0`)를 남겼다. 즉 **`{bool, uint8_t}` 반환 설계의 비용** 이고, `ts_send` 처럼 `.rx` 만 쓰면 23 < 27 로 오히려 짧다.
4. **빈 객체 인자의 흔적.** `-Os` 에서 `ts_send_val`(값 전달)은 `ts_send`(참조)보다 4명령 길다: 인라인되지 않은 `transfer` 가 `this` 주소를 요구하므로 빈 객체를 스택에 만들고(`add.w r0, sp, #0x7`) 그 주소를 넘긴다. C++ 의 빈 클래스는 크기 1이라 AAPCS 인자 레지스터 하나를 차지한다 — 인라인되면 사라지지만, 인라인 경계에서는 **참조로 넘기는 편이 싸다**.

clang `-Os` 실제 조각 — typestate 쪽의 레지스터 쓰기가 C 와 같은 순서로 남는다:

```text
00000000 <ts_bringup_send(PeriphToken<MmioHw<1073819648u>>&&, unsigned char)>:
       0:      	push	{r7, lr}
       2:      	sub	sp, #0x8
       4:      	movw	r0, #0x3000
       8:      	movt	r0, #0x4001          @ SPI1 = 0x40013000
       c:      	mov.w	r2, #0x314
      10:      	str	r2, [r0]             @ configure: CR1 = 0x314
      12:      	ldr	r2, [r0]
      14:      	orr	r2, r2, #0x40
      18:      	str	r2, [r0]             @ enable: CR1 |= SPE
      1a:      	add.w	r0, sp, #0x7         @ 빈 Spi<Enabled> 객체의 주소 (this)
      1e:      	mov.w	r2, #0x3e8           @ max_polls = 1000
      22:      	bl	... Spi<Enabled, ...>::transfer(unsigned char, unsigned int) &
      26:      	uxth	r1, r0               @ Xfer{ok, rx} 를 r0 하나로 받음
      28:      	ands	r0, r0, #0x1
      2c:      	it	ne
      2e:      	lsrne	r0, r1, #0x8
      30:      	add	sp, #0x8
      32:      	pop	{r7, pc}
```

(주석 `@ ...` 은 읽기 편하라고 붙였다.) `Xfer` 가 r0 한 개로 돌아오는 것도 보인다 — AAPCS 에서 4바이트 이하 복합형은 r0 로 반환된다.

### Part B — 실패하는 전이: 원래 상태를 돌려준다

```cpp
while ((Hw::read(reg::SR) & reg::SR_BSY) != 0u)
    if (++polls >= max_polls) return Fallible<Spi<Configured, Hw>, Spi>{std::in_place_index<1>, std::move(*this)};
Hw::write(reg::CR1, Hw::read(reg::CR1) & ~reg::CR1_SPE);
return Fallible<Spi<Configured, Hw>, Spi>{std::in_place_index<0>, Spi<Configured, Hw>{std::move(tok_)}};
```

테스트: BSY 영원히 → `r.index() == 1`, SPE 그대로, CR1 쓰기 0번, 돌려받은 객체로 `transfer` 가 **실제로 동작** (`0x0F → 0xF0`). 그다음 BSY 가 3번 읽은 뒤 풀리면 SR 을 정확히 4번 읽고 `cr1 == 0x314`. Rust 의 `Result<New, (Error, Old)>` 관용구와 같은 생각이다. `std::optional<New>` 였다면 실패 시 하드웨어는 켜져 있는데 그걸 다룰 타입이 사라진다 (주입 버그 m5 가 바로 이것 — 실패 경로에서 Configured 를 돌려주게 바꾸자 하네스 2개가 FAIL).

### Part B — 런타임에만 아는 상태: `adopt` + `normalize`

부트로더가 SPI 로 외부 플래시에서 이미지를 읽고 점프했다면, 애플리케이션 `main` 이 시작할 때 SPI1 은 **켜져 있을 수도** 있다 (소프트 리셋, 워치독 리셋 후 RCC 리셋 여부도 칩마다 다르다). `from_reset` 은 그 상황에서 거짓말을 한다 — 그래서 이름에 "가정" 을 드러냈다. 정답은 하드웨어를 읽어서 **런타임 값 → 타입의 합(sum type)** 으로 올리는 것이다:

```cpp
const std::uint32_t cr1 = Hw::read(reg::CR1);
if ((cr1 & reg::CR1_SPE) != 0u) return AnySpi<Hw>{Spi<Enabled, Hw>{std::move(t)}};   // SPE 를 먼저!
if (cr1 != 0u)                  return AnySpi<Hw>{Spi<Configured, Hw>{std::move(t)}};
return AnySpi<Hw>{Spi<Disabled, Hw>{std::move(t)}};
```

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">런타임 → 타입: 읽기 1번으로 variant, visit 으로 다시 정적 타입 하나</text>
  <rect x="10" y="50" width="120" height="44" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="22" y="70" fill="currentColor" font-family="monospace" font-size="12">CR1 읽기</text>
  <text x="22" y="86" fill="currentColor" font-size="12">(쓰기 0번)</text>
  <rect x="170" y="40" width="190" height="64" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="180" y="60" fill="currentColor" font-family="monospace" font-size="12">AnySpi = variant&lt;</text>
  <text x="180" y="76" fill="currentColor" font-family="monospace" font-size="12"> Disabled|Configured|</text>
  <text x="180" y="92" fill="currentColor" font-family="monospace" font-size="12"> Enabled&gt;  (+ index)</text>
  <rect x="400" y="30" width="200" height="26" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="408" y="48" fill="currentColor" font-size="12">D: configure</text>
  <rect x="400" y="62" width="200" height="26" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="408" y="80" fill="currentColor" font-size="12">C: reset → configure</text>
  <rect x="400" y="94" width="200" height="26" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="408" y="112" fill="currentColor" font-size="12">E: disable → reset → configure</text>
  <rect x="614" y="50" width="120" height="44" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="620" y="70" fill="currentColor" font-family="monospace" font-size="12">Spi&lt;Configured&gt;</text>
  <text x="620" y="86" fill="currentColor" font-size="12">원하는 cfg</text>
  <g stroke="currentColor" fill="none" stroke-width="1.5">
    <path d="M130 72 L166 72"/><path d="M160 67 L168 72 L160 77"/>
    <path d="M360 72 L396 43"/><path d="M360 72 L396 75"/><path d="M360 72 L396 107"/>
    <path d="M600 43 L612 66"/><path d="M600 75 L612 72"/><path d="M600 107 L612 80"/>
    <path d="M500 120 L500 150"/><path d="M495 144 L500 152 L505 144"/>
  </g>
  <rect x="400" y="154" width="200" height="30" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="408" y="174" fill="currentColor" font-size="12">BSY 안 풀림 → Enabled 그대로</text>
  <g fill="currentColor" font-size="12">
    <text x="10" y="130">핸드오프 예: CR1 = 0x347 (Mode3, Div2, SPE=1)</text>
    <text x="10" y="148" font-family="monospace">CR1 쓰기: 0x307 → 0 → 0x314</text>
    <text x="10" y="166">C 의 naive_init: 0x314|SPE 한 번 → cfg_while_on = 1</text>
    <text x="10" y="210">비용: variant 인덱스 1바이트 (libstdc++) / 4바이트 (맥 libc++ ABI v1), visit 의 분기</text>
  </g>
</svg>
```

핸드오프 테스트의 CR1 쓰기는 정확히 `0x307 → 0 → 0x314` 이고 위반 0. 같은 상황에서 주어진 C 스타일 `naive_init` (상태를 안 보고 `CR1 = cfg | SPE`)은 `cfg_while_on == 1` — 켜진 채로 모드와 분주를 바꾼다. 실제 칩에서 이건 "가끔 첫 바이트가 깨진다" 는 재현 안 되는 버그가 된다.

**variant 의 크기는 라이브러리마다 다르다 (실측).**

| | 빈 대안 3개짜리 `std::variant` | 이유 |
|---|---|---|
| arm-none-eabi libstdc++ 14.2.1 (`demo_target.cpp` 의 static_assert) | 2 바이트 | 인덱스 = 대안 수에 맞는 가장 작은 정수 |
| 맥 Apple libc++ (호스트) | 8 바이트 | ABI v1 은 인덱스가 `unsigned int`. `__configuration/abi.h` 에서 `_LIBCPP_ABI_VARIANT_INDEX_TYPE_OPTIMIZATION` 은 ABI v2 블록에만 있다 |

그래서 하네스는 variant 크기를 단언하지 않고, 타깃 데모에만 `sizeof(AnySpi<Hw1>) == 2` 를 둔다. "variant 는 작다" 는 **라이브러리 ABI 에 달린 주장** 이다.

**`std::visit` 의 비용 — `-Os` 에서 보이는 것.** clang `-Os` 의 `nm`:

```text
00000024 W ... normalize<MmioHw<1073819648u>>(...)            (36 B)
00000040 T ts_adopt_normalize(PeriphToken<...>&&)               (64 B)
00000040 T ts_adopt_normalize_switch(PeriphToken<...>&&)        (64 B)
0000003e T c_adopt_normalize                                     (62 B)
00000054 W ... std::__invoke_impl<...normalize...lambda...>      (84 B)
00000056 W ... normalize_switch<MmioHw<1073819648u>>(...)       (86 B)
0000005a W ... std::__do_visit<...>                              (90 B)
-- undefined:
         U abort
```

| 경로 | clang `-Os` 바이트 | clang `-O2` 명령 | GCC `-Os` 명령 | GCC `-O2` 명령 | 미정의 심볼 |
|---|---|---|---|---|---|
| `adopt` + `normalize` (`std::visit`) | 64+36+84+90 = **274** | 99 | 28 + `disable` 16 | 34 | clang `-Os`: **`abort`** |
| `adopt` + `normalize_switch` (`index()` + `get_if`) | 64+86 = **150** | 99 | 40 | 34 | 없음 |
| C 의 if 사슬 | **62** | 99 | 25 | 31 | 없음 |

- `-O2` 에서는 셋 다 **99 명령으로 같다** (clang). 모두 인라인되면 variant 는 사라지고 레지스터 접근만 남는다. GCC `-O2` 의 34 vs 31 은 디스어셈블상 같은 접근 순서에 출구를 상태별로 복제(tail duplication)한 배치 차이다 — 반대로 C 쪽은 베이스 주소를 리터럴 풀에서 세 번 다시 읽는다.
- `-Os` 에서 인라인이 멈추면 진짜 비용이 드러난다: `normalize` 가 `const Config&` 를 받아 **상수 전파가 끊기고** (`ldrb` 3개 + `orr` 로 0x314 를 런타임에 계산), `std::visit` 은 `__do_visit` + `__invoke_impl` 두 함수를 만든다.
- **`abort` 는 어디서 왔나.** 재배치를 보면 `normalize` 의 `R_ARM_THM_CALL abort` 하나다. 디스어셈블의 `cmp r1, #0xff; beq → bl` 이 "variant 가 valueless 인가" 검사이고, `-fno-exceptions` 에서 libstdc++ 의 `_GLIBCXX_THROW_OR_ABORT` 는 `__builtin_abort()` 가 된다 (`bits/c++config.h`). 링크하면 newlib 의 `abort` 가 딸려 온다.
- **왜 valueless 검사가 남나 — 우리 설계 탓이다.** libstdc++ `<variant>` 의 `__never_valueless()` 는 `_Traits<_Types...>::_S_move_assign && (_Never_valueless_alt<_Types> && ...)` 이다 (444–462행). 우리는 `Spi& operator=(Spi&&) = delete;` 로 이동 대입을 막았으므로 첫 항이 거짓 → "valueless 일 수 있음" → 검사 유지. `bash asm_targets.sh moveassign` 으로 두 연산자를 `= default` 로 바꾼 사본을 빌드하면 **`abort` 가 사라지고** `normalize` 래퍼도 호출자 안으로 인라인된다 (0x40 → 0x4a 한 함수). "안전하려고 지운 이동 대입" 이 `-fno-exceptions` 빌드에 abort 경로를 남긴 셈이다.

판단: ① 부팅 때 한 번 도는 코드이므로 274 B 는 보통 문제가 아니다. ② 문제가 되면 `index()` switch + `get_if` (150 B, abort 없음)로 바꾸거나, 이동 대입을 trivial 로 허용한다 — typestate 객체에서 이동 대입은 "같은 타입의 증명서를 덮어쓴다" 일 뿐이라 위험이 거의 없다. ③ 어느 쪽이든 **`nm -u` 로 확인** 한다. GCC 14.2.1 은 같은 헤더로 `-Os`/`-O2` 모두 미정의 심볼 없음 (같은 함수 안에서 index 값 범위를 증명해 검사를 지운 것으로 보인다 — 메커니즘은 미확인).

### Part B — 소유권 토큰

```cpp
[[nodiscard]] static std::optional<PeriphToken> take() noexcept {
    if (taken_.exchange(true, std::memory_order_acq_rel)) return std::nullopt;
    return PeriphToken{};
}
```

- 생성자 private, 복사 delete → 위조·복제 불가 (SHOW_ERROR=5 `calling a private constructor`). 타입이 `PeriphToken<Hw>` 라서 SPI1 토큰으로 SPI2 를 만들 수 없다 (SHOW_ERROR=6 `no viable conversion ... to 'PeriphToken<FakeHw2>'`).
- 비용 (clang `-Os`): `ts_take` 12명령 = `dmb sy; ldrexb/strexb` 루프; `dmb sy`, `.bss` 1바이트 (`V PeriphToken<...>::taken_`). 부팅 때 한 번. M0+ 라면 `atomic<bool>::exchange` 가 libcall 이 된다 (CPP-A04) — 그 경우 `CriticalSection` 안에서 일반 bool 로.
- Rust 임베디드의 `Peripherals::take()` (Option 반환) 와 같은 모양이다. 차이: Rust 는 토큰을 쓰면 원본 변수가 컴파일러에게 죽는다.

### Part B — 구멍: moved-from 객체는 살아 있다

```cpp
template <class Hw> Xfer buggy_sleep_then_send(Spi<Enabled, Hw>& en) {
    auto r = std::move(en).disable();   // 여기서 en 은 논리적으로 끝났다
    (void)r;
    return en.transfer(0x11, 20);       // 그런데 en 은 여전히 Spi<Enabled> 이름이다 — 컴파일된다
}
```

C++ 에서 `std::move` 는 캐스트일 뿐이고, 객체의 수명은 스코프 끝까지 계속된다 ([basic.life], [stmt.dcl] — 소멸자도 그때 불린다). 타입은 바뀌지 않는다. 그래서 하드웨어는 꺼졌는데 `en.transfer` 가 컴파일되고, mock 에서 DR 쓰기가 무시되어(`dropped_tx == 1`) 타임아웃으로 끝난다 (하네스가 이 구멍이 **존재함** 을 확인한다). 토큰도 마찬가지: `from_reset(std::move(t))` 를 두 번 쓰면 SPI1 객체가 둘 생긴다.

완화책 비교 (`bash asm_targets.sh tidy`, 실제 출력):

```text
== clang-tidy bugprone-use-after-move (version 23.1.4)
demo_moved_from.cpp:17:12: warning: 'en' used after it was moved [bugprone-use-after-move]
demo_moved_from.cpp:15:28: note: move occurred here
demo_moved_from.cpp:23:25: warning: 'd' used after it was moved [bugprone-use-after-move]
demo_moved_from.cpp:22:28: note: move occurred here
demo_moved_from.cpp:29:49: warning: 't' used after it was moved [bugprone-use-after-move]
demo_moved_from.cpp:28:14: note: move occurred here
demo_moved_from.cpp:76:12: warning: 'e' used after it was moved [bugprone-use-after-move]
demo_moved_from.cpp:74:29: note: move occurred here
== clang -Wconsumed (typestate 속성)
demo_moved_from.cpp:76:14: warning: invalid invocation of method 'xfer' on object 'e' while it is in the 'consumed' state [-Wconsumed]
== 기본 빌드 (-Wall -Wextra): 경고 없이 통과 — 컴파일러는 이 구멍을 기본으로는 안 본다
OK
```

| 완화책 | 무엇을 잡나 | 비용·한계 (실측) |
|---|---|---|
| `&&` 한정 전이 | 전이를 부르려면 `std::move(x)` 를 **써야** 한다 → 재사용이 코드에 드러난다 | 0. 이게 있어야 clang-tidy 가 "이동" 을 본다: 같은 버그를 lvalue 전이(`LooseOff::enable()`)로 쓰면 **경고 0개** |
| prvalue 체인 `from_reset(t).configure(c).enable()` | 중간 상태에 이름이 없으니 재사용할 이름도 없다 | 0. 분기가 필요한 곳에서는 못 쓴다 |
| `[[nodiscard]]` | 전이 결과를 버리기 (SHOW_ERROR=7) | 0. moved-from 재사용은 못 잡는다 |
| clang-tidy `bugprone-use-after-move` | 위 구멍 3개 + consumed 예제, 전부 | CI 에 tidy 단계 필요. 함수 경계를 넘는 흐름(다른 함수에서 이동)은 못 본다 |
| clang `-Wconsumed` + `[[clang::consumable]]` 등 | 속성을 단 타입의 "consumed 상태에서 호출" | clang 전용. GCC 14.2.1 은 `'clang::consumable' scoped attribute directive ignored [-Wattributes]` (VM 실측) → 매크로로 감싼다 |
| 런타임 `live_` 플래그 (연습 1 `CheckedSpi`) | 실행 중 죽은 객체 사용을 전부 | 1바이트 + 매 연산 분기, trivially copyable 을 잃는다 (사용자 정의 이동) — 디버그 빌드용 |

### Part C — 부팅 경로: 어떤 실패에서도 토큰을 잃지 않는다

```cpp
AnySpi<Hw> any = normalize(adopt(std::move(tok)), c);
auto* cfg = std::get_if<Spi<Configured, Hw>>(&any);
if (cfg == nullptr) return {std::move(any), 0u, false};        // 끄지 못함 → Enabled 그대로
Spi<Enabled, Hw> en = std::move(*cfg).enable();
const std::optional<std::uint32_t> id = read_jedec_id(en);
return {AnySpi<Hw>{std::move(en)}, id.value_or(0u), id.has_value() && *id == expect_id};
```

| 시나리오 | 결과 (테스트가 확인) |
|---|---|
| 콜드 리셋 | ok, `0xEF4018`, CR1 쓰기 `0x314, 0x354`, CS 에지 2, DR 쓰기 4 |
| 부트로더가 Mode3/Div2 로 켜 둠 | ok, CR1 쓰기 `0x307, 0, 0x314, 0x354`, 위반 0, 이후 `take()` 는 nullopt (토큰은 `b.spi` 안) |
| BSY 안 풀림 | !ok, `spi.index() == 2`, CR1 쓰기 0, CS 에지 0 (칩을 건드리지도 않음) |
| 다른 칩 (`0xC24018`) | !ok, Enabled 유지, CS high |
| ID 읽는 중 전송 실패 | nullopt, 그래도 CS 에지 2 (`ScopedCs` 소멸자, CPP-B06) |

`std::get_if` 를 쓴 이유: `std::get` 은 잘못된 인덱스에서 `bad_variant_access` 를 던지는데 `-fno-exceptions` 에선 abort 경로가 된다 (위의 `abort` 와 같은 뿌리). `get_if` 는 포인터를 돌려줄 뿐이다.

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution   # 40/40 passed
c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer solution.cpp -o solution_san && ./solution_san   # 40/40, 보고 없음
```

하네스가 일하는지 확인한 주입 버그 (solution 사본, 레슨 폴더 밖): ① `enable` 이 `CR1 = SPE` 로 덮어씀 → 14개 FAIL, ② `disable` 이 BSY 를 안 기다림 → 6개, ③ `adopt` 에서 `cr1 != 0` 을 SPE 보다 먼저 검사 → 5개, ④ `read_jedec_id` 에서 `ScopedCs` 빠뜨림 → 5개, ⑤ disable 실패 시 Configured 를 돌려줌 → 5개, ⑥ `transfer` 의 requires 삭제 / ⑦ `configure` 의 `&&` 삭제 → 각각 `static_assert` 로 빌드 실패.

## 7. 자주 하는 실수

1. **`adopt` 에서 "CR1 ≠ 0 → Configured" 를 먼저 검사.** SPE=1 인 CR1 도 0 이 아니므로 Enabled 를 Configured 로 오판하고, `normalize` 가 켜진 채로 `reset()` (CR1=0) 을 쓴다 → `cfg_while_on`. 고침: SPE 비트를 먼저.
2. **실패하는 전이를 `std::optional<New>` 로.** 실패 시 원래 상태 객체가 사라진다. 고침: `std::variant<New, Old>` 로 원래 객체를 돌려준다.
3. **소멸자에서 하드웨어 끄기 (RAII 처럼).** C++ 는 moved-from 객체의 소멸자도 부른다 — `configure` 로 넘긴 `Spi<Disabled>` 가 스코프 끝에서 SPI 를 꺼 버린다. 고치려면 "살아 있음" 플래그가 필요하고 그 순간 크기 0·trivial 을 잃는다. 고침: 상태 객체의 소멸자는 trivial, 끄기는 명시적 전이로.
4. **전이를 lvalue 멤버로 (`Spi<Enabled> enable();`).** `auto e = c.enable(); auto e2 = c.enable();` 이 경고 없이 컴파일되고 clang-tidy 도 못 잡는다 (`std::move` 가 없으니까). 고침: `&&` 한정.
5. **`static_assert(!requires { ... })` 를 템플릿 밖에 맨손으로.** 잘못된 식이 하드 에러가 되거나 컴파일러마다 다르게 동작한다. 고침: 개념 안에 넣고 양성 단언도 같이.
6. **"variant 는 1~2바이트" 라고 단정.** 맥 libc++ 에서는 8바이트다 (ABI v1, 실측). 고침: 타깃 툴체인에서 `static_assert` 로 확인.
7. **`std::get<Spi<Configured>>(any)` 로 꺼내기.** 틀린 인덱스면 `-fno-exceptions` 에선 abort. 고침: `std::get_if` + null 검사, 또는 visit.

## 8. 면접 Q&A

<details>
<summary>Q1. typestate 가 "비용 0" 이라는 근거를 어떻게 보여 주나요?</summary>

세 겹으로. ① 타입: `sizeof == 1`, `is_empty`, trivially copyable/destructible 을 `static_assert` 로 — 상태는 타입에만 있고 런타임 바이트가 없다. ② 같은 레지스터 접근을 하는 C 함수와 thumbv7em asm 을 나란히: clang `-O2` 에서 171/171, 166/166, 99/99 로 명령 수가 같았다. ③ 다르면 원인을 가른다: GCC `-O2` 에서 39 vs 31 이 나왔지만, 같은 `Xfer` 반환 구조체를 쓴 C 대조군이 41 이라 원인은 typestate 가 아니라 `{bool,u8}` 반환이었다. 그리고 "0" 이 아닌 것도 말한다: 런타임 상태(variant)는 `-Os` 에서 274 B vs 62 B, 인라인 경계의 빈 객체 값 전달은 인자 레지스터 하나.

</details>

<details>
<summary>Q2. Rust 는 move 하면 원본을 못 쓰는데, C++ 는요? 어떻게 막죠?</summary>

C++ 의 `std::move` 는 rvalue 로의 캐스트일 뿐이고 원본의 수명과 타입은 그대로다. 언어 차원에서 막을 방법은 없다. 그래서 겹으로 줄인다: `&&` 한정 전이(재사용하려면 `std::move` 를 다시 써야 해서 눈에 띄고 도구가 본다), prvalue 체인(중간 상태에 이름을 안 붙인다), `[[nodiscard]]`, CI 의 clang-tidy `bugprone-use-after-move` (이 레슨의 구멍 3개를 전부 잡음), clang 이면 `-Wconsumed` 속성. 디버그 빌드에선 `live_` 플래그로 런타임 검사 (1바이트 + 분기). 하나 더: 상태 객체가 아무 일도 안 하는 빈 증명서라서, 죽은 객체를 써도 "레지스터 접근 하나" 이상의 피해(이중 해제 같은)는 없다 — 피해 범위를 설계로 줄인 것이다.

</details>

<details>
<summary>Q3. 부트로더가 넘겨준 주변장치처럼 상태를 런타임에만 알면요?</summary>

하드웨어를 읽어 `std::variant<Spi<Disabled>, Spi<Configured>, Spi<Enabled>>` 로 올리고(`adopt`, 읽기 1번·쓰기 0번), visit 으로 원하는 하나의 정적 상태로 내린다(`normalize`). 이후 코드는 다시 컴파일 시간 검사를 받는다. 즉 런타임 검사는 **경계 한 곳** 에만 있다. 비용: libstdc++ 에서 인덱스 1바이트, `-O2` 에서는 C 와 같은 명령 수, `-Os` 에서는 `std::visit` 이 함수 두 개 + valueless 검사 → `abort` 의존. 줄이려면 `index()` switch + `get_if` (150 B, abort 없음) 또는 이동 대입을 trivial 로 허용해 libstdc++ 가 never-valueless 로 판단하게 한다 (실측). 대안은 `DynSpi` 처럼 상태 변수를 들고 다니며 매 연산 검사하는 것인데, 그러면 모든 오용이 에러 코드가 되고 매 호출 분기가 붙는다 (`dyn_send` 는 C 대비 clang `-Os` +4명령).

</details>

<details>
<summary>Q4. 상태 객체가 소멸할 때 SPI 를 꺼 주면 더 안전하지 않나요?</summary>

C++ 에서는 moved-from 객체의 소멸자도 불린다. `std::move(d).configure(c)` 뒤 `d` 가 스코프를 벗어나며 SPI 를 끄면 방금 만든 Configured 가 거짓말이 된다. 이를 피하려면 객체가 "아직 소유 중" 플래그를 들고, 이동 생성자가 원본 플래그를 끄고, 소멸자가 검사해야 한다 → 크기 1바이트의 실제 데이터, 사용자 정의 이동 → trivially copyable 상실(AAPCS 에서 메모리로 전달), 모든 전이에 분기. Rust 는 move 된 값의 Drop 을 부르지 않아서 이 비용이 없다. 그래서 C++ 판에서는 소멸자를 trivial 로 두고, "버리면 그 상태 그대로 누수(leak)" 를 문서화한다 — Rust 의 `mem::forget` 과 같은 의미. 꼭 RAII 가 필요하면 최상위 소유자(예: 앱 객체) 한 곳에서만.

</details>

<details>
<summary>Q5. requires-clause, 특수화, 자유 함수 오버로드 중 무엇으로 상태별 API 를 나누나요?</summary>

requires-clause: 한 클래스에 모든 API 가 보이고, 에러가 `because 'std::same_as<Configured, Enabled>' evaluated to false` 로 상태를 말해 준다. 특수화: 상태마다 멤버·데이터를 완전히 다르게 할 수 있지만 공통 코드가 중복되고 에러가 `no member named` 로 짧아진다. 자유 함수: `transfer(Spi<Enabled>&, ...)` — 확장이 쉽지만(다른 헤더에서 추가) 에러가 "변환 불가" 로 돌려 말해진다 (SHOW_ERROR=9). 상태마다 저장 데이터가 다르면(예: Enabled 만 DMA 핸들 보유) 특수화, 아니면 requires. C++17 이하라면 `std::enable_if` 로 같은 일을 하지만 메시지가 훨씬 나쁘다 (MCPP-M05).

</details>

<details>
<summary>Q6. 같은 SPI1 을 두 객체가 갖지 못하게 하는 방법과 그 비용은?</summary>

`PeriphToken<Hw>`: private 생성자 + 복사 금지 + `take()` 가 `atomic<bool>::exchange` 로 한 번만 성공. 드라이버는 토큰을 받아야만 만들어지고(`from_reset`, `adopt`), `release` 로만 돌려준다. 타입이 Hw 별이라 SPI1 토큰으로 SPI2 는 컴파일 에러. 비용: `.bss` 1바이트, 부팅 때 `ldrexb/strexb` 루프 + `dmb` 2개 (12명령, 실측). 구멍: 토큰도 이동 후 재사용할 수 있다 — clang-tidy 가 잡았다 (`'t' used after it was moved`). 테스트에서는 `reset_for_test()` 가 필요하다는 것도 설계의 일부(프로덕션 빌드에서 `#ifdef` 로 빼는 게 보통).

</details>

<details>
<summary>Q7. 다른 컴파일러·툴체인으로 옮기면 무엇이 깨질 수 있나요?</summary>

① `[[no_unique_address]]` — MSVC 는 이 속성을 무시하고 `[[msvc::no_unique_address]]` 를 쓴다 (MSVC 는 이 레슨에서 확인 안 함, 미확인). 빈 객체 크기가 커질 수 있으니 `static_assert(sizeof == 1)` 로 지킨다. ② variant 크기와 never-valueless 판단은 라이브러리 구현 세부 (libc++ 8B vs libstdc++ 2B, 실측). ③ `-Wconsumed` 속성은 clang 전용, GCC 는 `-Wattributes` 경고 (실측) — 매크로로. ④ 인라인 판단은 컴파일러·최적화 수준마다 다르다 (`-Os` 에서 clang·GCC 모두 `transfer` 를 한 벌로 분리). ⑤ 에러 메시지 모양 (clang: `invalid reference ... constraints not satisfied`, GCC: `no matching function ... constraints not satisfied`). 그래서 "비용 0" 은 타깃 툴체인의 CI 에서 asm/size 로 지킨다.

</details>

<details>
<summary>Q8. 이 드라이버를 어떻게 테스트하나요? 타입 검사만으로 충분한가요?</summary>

타입은 **순서** 를 지키지만 각 전이 안의 레지스터 값·순서는 못 지킨다 (주입 버그 ① `enable` 이 CR1 을 덮어써도 타입 검사는 통과). 그래서 두 층: ① 컴파일 시간 — 개념 기반 `static_assert` (양성+음성), ② 런타임 — Hw 정책을 mock 으로 바꿔 접근 로그를 정확히 비교하고 하드웨어 규칙 위반을 카운터로(`cfg_while_on`, `dropped_tx`). 실패 주입(BSY 고정, 슬레이브 모드, 다른 칩 ID)으로 실패 경로가 상태와 CS 를 잃지 않는지 확인한다. 마지막으로 하네스 자체를 검증하려고 버그를 주입해 FAIL 이 나는지 본다 (5개 런타임 + 2개 컴파일 시간 모두 잡힘).

</details>

## 9. 추가 연습

### 연습 1 — `CheckedSpi`: moved-from 사용을 런타임에 잡기 (디버그 빌드용)

상태 객체에 `bool live_` 를 두고, 이동 생성자와 전이가 원본을 죽이게 하라. 죽은 객체의 연산은 하드웨어에 닿기 전에 `g_dead_use` 를 올리고 실패를 돌려준다. `buggy_sleep_then_send` 같은 코드가 DR 을 건드리지 않고 잡혀야 한다.

<details>
<summary>답 보기</summary>

```cpp
CheckedSpi(CheckedSpi&& o) noexcept : live_(o.live_) { o.live_ = false; }
CheckedSpi<Enabled> enable() && noexcept requires std::same_as<S, Configured> {
    if (!alive()) return CheckedSpi<Enabled>{false};      // 죽은 객체 → 죽은 다음 상태
    live_ = false;
    cr1_write(g.cr1 | SPE);
    return CheckedSpi<Enabled>{true};
}
int transfer(std::uint8_t b) & noexcept requires std::same_as<S, Enabled> {
    if (!alive()) return -1;
    return dr_xfer(b);
}
```

테스트: disable 뒤 옛 `en.transfer` → `-1`, `g_dead_use == 1`, `dropped == 0`. 같은 Configured 를 두 번 enable → 두 번째는 죽은 객체, CR1 쓰기 4번(설정·켜기·끄기·켜기)에서 멈춤. 비용: `sizeof == 1` (토큰 대신 플래그), 그리고 `!std::is_trivially_copyable_v` — 사용자 정의 이동 생성자 때문에 Q1 의 "레지스터로 전달" 이 깨진다. 그래서 디버그 빌드 전용이 적당하다.

</details>

### 연습 2 — `DynSpi`: 같은 오용이 "에러 코드" 가 되는 탈출구

상태를 `enum class St` 변수로 들고 다니는 SPI 를 써라. 모든 연산은 상태가 틀리면 `Err::WrongState` 를 돌려주고 하드웨어를 건드리지 않는다. 핸드오프(켜진 상태로 시작)에서 `configure` 가 거부되고 `disable` 후 가능해야 한다.

<details>
<summary>답 보기</summary>

```cpp
Err transfer(std::uint8_t b, std::uint8_t& rx) noexcept {
    if (st_ != St::Enabled) return Err::WrongState;
    rx = dr_xfer(b); return Err::Ok;
}
Err configure(std::uint32_t cfg) noexcept {
    if (st_ != St::Disabled) return Err::WrongState;
    cr1_write(cfg); st_ = St::Configured; return Err::Ok;
}
```

typestate 에서 컴파일 에러였던 `transfer` before `enable` 이 여기선 테스트에서야 보인다. 대신 상태를 한 객체 안에서 바꿀 수 있어 저장·전달이 쉽다 (멤버 하나, `sizeof == 1`). 비용은 매 호출 비교 1개 + 에러 경로: `demo_target.cpp` 의 `dyn_send` 는 clang `-Os` 27명령 vs `c_send` 23, `-O2` 172 vs 166. 실무 조합: 경계(부팅·복구)는 DynSpi/variant, 정상 경로는 typestate.

</details>

### 연습 3 — 필수 필드를 강제하는 typestate 빌더

`ConfigBuilder<HasMode, HasBaud>` 를 만들어 `mode()`·`baud()` 는 각각 한 번만, `build()` 는 둘 다 정했을 때만 부를 수 있게 하라. 순서는 자유, `lsb_first()` 는 선택. 전부 `constexpr`.

<details>
<summary>답 보기</summary>

```cpp
constexpr ConfigBuilder<true, HasBaud> mode(Mode m) const noexcept requires (!HasMode) { return {m, b_, lsb_}; }
constexpr ConfigBuilder<HasMode, true> baud(Baud b) const noexcept requires (!HasBaud) { return {m_, b, lsb_}; }
constexpr Config build() const noexcept requires (HasMode && HasBaud) { return {m_, b_, lsb_}; }

constexpr Config kCfg = ConfigBuilder<>{}.baud(Baud::Div2).lsb_first().mode(Mode::M3).build();
static_assert(encode(kCfg) == 0x387u);
static_assert(!CanBuild<ConfigBuilder<true, false>> && !CanSetMode<ConfigBuilder<true, false>>);
```

typestate 의 가장 흔한 실무 형태다: 상태가 bool 템플릿 인자이고, 컴파일 시간에 끝나면 `kCfg` 는 상수 3바이트일 뿐이다. 하드웨어 상태가 아니라 **값의 완성도** 를 타입에 담았다는 점이 SPI 와 다르다. 필드가 많아지면 `bool` 대신 비트마스크 NTTP 하나로.

</details>

### 연습 4 — CS 트랜잭션을 타입으로: `transfer` 는 `Selected` 에만

`Bus` (Enabled SPI) 에는 `transfer` 가 없고, `select() &` 가 돌려준 `Selected` 객체가 살아 있는 동안만 CS=low 이며 그 객체에만 `transfer` 가 있다. `Selected` 는 복사·이동 불가.

<details>
<summary>답 보기</summary>

```cpp
class Selected {
public:
    Selected(const Selected&) = delete;
    ~Selected() { g.cs_low = false; ++g.cs_edges; }
    std::uint8_t transfer(std::uint8_t b) noexcept { return dr_xfer(b); }
private:
    friend class Bus;
    explicit Selected(Bus& b) noexcept : bus_(b) { g.cs_low = true; ++g.cs_edges; }
    Bus& bus_;
};
[[nodiscard]] Selected select() & noexcept { return Selected{*this}; }   // C++17 보장된 복사 생략
```

복사·이동이 모두 없어도 prvalue 반환은 된다 ([stmt.return], 보장된 복사 생략) — 그래서 `auto t = bus.select();` 는 되지만 `Selected` 를 다른 곳으로 옮겨 CS 를 오래 붙잡을 수는 없다. 한계: `bus_` 참조의 수명은 C++ 가 검사하지 않는다 (Rust 의 빌림 검사가 하는 일). 테스트: `read_id` 후 CS 에지 2, DR 쓰기 4, 스코프 블록 후 CS high.

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice   # 19/19 passed
c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer practice.cpp -o practice_san && ./practice_san   # 19/19, 보고 없음
```

## 10. 핵심 정리

- typestate = 주변장치 상태를 **타입** 으로. 전이는 `[[nodiscard]]`, `&&` 한정, 새 타입의 객체를 돌려준다. 상태별 API 는 trailing `requires` → 에러가 현재/필요 상태를 말해 준다.
- "컴파일 안 됨" 은 개념(requires-expression) + `static_assert` 로 하네스에 고정하고, 양성 단언도 같이 둔다.
- 비용 0 은 실측으로: 빈 클래스(`[[no_unique_address]]` 토큰), trivially copyable, clang `-O2` 에서 C 와 명령 수 동일 (171/166/99). GCC 의 차이는 `Xfer` 반환 탓이었다 (대조군 41).
- C++ 의 구멍: moved-from 객체는 이름도 타입도 살아 있다. `&&` + prvalue 체인 + clang-tidy `bugprone-use-after-move` (+ clang `-Wconsumed`) 로 줄이고, 소멸자는 trivial 로 둔다 (moved-from 도 소멸자가 불리므로).
- 실패하는 전이는 `variant<New, Old>` — 실패해도 상태와 토큰을 잃지 않는다.
- 런타임에만 아는 상태는 경계 한 곳에서 `adopt` (읽기 1번 → variant) → `normalize` (visit) 로 정적 타입에 다시 들어온다. `-Os` 에서 `std::visit` 은 274 B + `abort` 의존 (이동 대입 delete 탓), switch+`get_if` 는 150 B.
- 소유권 토큰: private 생성자 + `take()` 한 번 (`atomic::exchange`, `.bss` 1바이트) — Hw 별 타입이라 SPI1 토큰으로 SPI2 불가.

## 참고자료

- C++ 표준 (N4861 C++20 초안): [dcl.decl] (trailing requires-clause), [over.match.funcs] (ref-qualifier 와 암시적 객체 매개변수), [over.match.viable] (제약 불만족 후보 제외), [expr.prim.req] (requires-expression), [temp.constr], [dcl.attr.nodiscard], [dcl.attr.nouniqueaddr], [class.prop] (trivially copyable), [basic.life], [stmt.dcl] / [class.dtor] (스코프 끝 소멸), [stmt.return] / [dcl.init] (보장된 복사 생략), [variant] (valueless_by_exception)
- cppreference — Constraints and concepts: <https://en.cppreference.com/w/cpp/language/constraints>, `std::variant`: <https://en.cppreference.com/w/cpp/utility/variant>
- Clang Attribute Reference — Consumed Annotation Checking (`consumable`, `callable_when`, `set_typestate`, `return_typestate`, `param_typestate`): <https://clang.llvm.org/docs/AttributeReference.html>
- clang-tidy `bugprone-use-after-move`: <https://clang.llvm.org/extra/clang-tidy/checks/bugprone/use-after-move.html>
- STM32F4 Reference Manual RM0090 — SPI 레지스터(SPI_CR1/SR/DR)와 SPI 끄기 절차 (이 레슨의 비트 위치는 대표값): <https://www.st.com/resource/en/reference_manual/rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf>
- Procedure Call Standard for the Arm Architecture (AAPCS32) — 복합형 반환·인자 전달: <https://github.com/ARM-software/abi-aa/releases>
- The Embedded Rust Book — Static Guarantees / Typestate Programming (Rust 쪽 같은 패턴): <https://docs.rust-embedded.org/book/static-guarantees/typestate-programming.html>
- libstdc++ `<variant>` (`_Never_valueless_alt`, `__never_valueless`) — arm-none-eabi 14.2.1 헤더 444–462행에서 확인; libc++ `__configuration/abi.h` (`_LIBCPP_ABI_VARIANT_INDEX_TYPE_OPTIMIZATION`, ABI v2)
- 관련 레슨: CPP-M01 (Reg 접근 권한·mock 버스), CPP-B06 (RAII CriticalSection), CPP-B12 / MCPP-M05 (개념·requires), CPP-M11 (Result 설계), CPP-A04 (`atomic` 의 M0+ libcall), CPP-A03 (`abort`·`__cxa_*` 등 런타임 의존 제거), RUST-M04 (Rust typestate GPIO), C-M07 (테이블 FSM)
