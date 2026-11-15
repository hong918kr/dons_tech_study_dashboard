---
id: CPP-M14
time: 80분
updated: 2026-11-08
---
# CPP-M14 static_assert·type traits로 HAL 오용 차단

## 학습 목표

- 핀별 허용 AF(alternate function) 표를 `constexpr` 로 정의하고, `configure<Pin, Signal>()` 이 **허용되지 않은 조합에서 컴파일 에러**가 나게 만든다.
- 실패 이유를 **값(`PinError`)** 으로 먼저 계산해서, (1) 이유마다 다른 `static_assert` 메시지를 내고 (2) 그 판정 로직 자체를 테스트한다.
- C++20 `static_assert` 메시지는 문자열 리터럴뿐이라는 제약 아래서 **문제가 된 핀·기능 이름이 에러에 찍히게** 만드는 기법들을 비교한다 (실제 clang 21 / g++ 14.2.1 출력).
- 템플릿 안의 `static_assert(false)` (dependent-false 함정, CWG2518)가 **지금 컴파일러에서 실제로 어떻게 동작하는지** 돌려 보고 설명한다.
- 보드 전체 핀 배치 표를 컴파일 타임에 검증하고, 검증 비용이 **런타임 0** 임을 Cortex-M4 디스어셈블리로 확인한다.

## 1. 문제

핀에 어떤 기능을 물릴 수 있는지는 칩마다 표로 고정되어 있고, 잘못 고르면 보드에서 "신호가 안 나온다"는 증상만 남는다. 면접에서는 "이런 배선 실수를 어떻게 빌드 단계에서 잡겠냐"로 `constexpr` + 템플릿 + 진단 품질을 한꺼번에 본다.

STM32 같은 MCU의 핀은 내부 멀티플렉서로 기능을 고른다. `PA9` 는 USART1_TX 를 낼 수 있지만 I2C1_SCL 은 못 낸다. C 코드에서는 AF 번호를 숫자로 쓰기 때문에 틀려도 컴파일이 된다.

```c
GPIOA->AFR[1] |= (4u << 4);   /* PA9 에 AF4 (I2C 번호) — 컴파일 OK, 보드에서 신호 없음 */
```

이 레슨은 그 표를 코드로 옮기고, 잘못된 조합을 **빌드 에러**로 바꾼다. 핀 타입 자체(`Pin<Port, N>`, BSRR 쓰기)는 CPP-M04 소관이라 여기서는 아주 작은 것만 쓰고, 레지스터 타입(`Reg<...>`)은 CPP-M01 소관이다. 여기서 레지스터는 mock 구조체다.

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">configure&lt;P, S, O&gt;() — 컴파일 타임 판정 흐름</text>
  <rect x="10" y="40" width="190" height="58" rx="8" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="62">configure&lt;PA9,</text>
    <text x="20" y="82">  Signal::I2C1_SCL&gt;()</text>
  </g>
  <g stroke="currentColor" fill="none"><path d="M200 69 L250 69"/><path d="M445 69 L490 69"/></g>
  <rect x="250" y="40" width="195" height="58" rx="8" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor"><text x="260" y="62">pin_error&lt;P, S, O&gt;()</text><text x="260" y="82">이유를 enum 값으로 계산</text></g>
  <rect x="490" y="40" width="240" height="58" rx="8" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor"><text x="500" y="62">AF_TABLE 행을 constexpr 로 탐색</text><text x="500" y="82">find_af(port, pin, signal)</text></g>
  <g stroke="currentColor">
    <rect x="10" y="130" width="170" height="60" rx="6" class="f-warn"/>
    <rect x="195" y="130" width="170" height="60" rx="6" class="f-warn"/>
    <rect x="380" y="130" width="170" height="60" rx="6" class="f-warn"/>
    <rect x="565" y="130" width="165" height="60" rx="6" class="f-ok"/>
  </g>
  <g fill="currentColor">
    <text x="20" y="152">NotAPin</text><text x="20" y="172" font-size="12">P 가 Pin 타입이 아님</text>
    <text x="205" y="152">PinOutOfRange</text><text x="205" y="172" font-size="12">핀 번호 16 이상</text>
    <text x="390" y="152">SignalNotOnPin</text><text x="390" y="172" font-size="12">표에 없는 조합</text>
    <text x="575" y="152">None</text><text x="575" y="172" font-size="12">apply_pin 호출</text>
  </g>
  <text x="10" y="222" fill="currentColor">이유마다 static_assert 하나 → 메시지가 정확해지고, 실패 원인은 항상 한 개만 보고된다.</text>
  <text x="10" y="244" fill="currentColor">판정이 enum 값이므로 CHECK / static_assert 로 "이 조합은 이 이유로 실패"까지 테스트할 수 있다.</text>
  <text x="10" y="266" fill="currentColor">(그림에서 생략: 네 번째 이유 I2cNeedsOpenDrain — Part B)</text>
</svg>
```

### Part A — AF 표와 configure

- `AF_TABLE[]` 은 `{port, pin, signal, af}` 행의 배열이다. 주어진다.
- `find_af(port, pin, signal)` : 행을 찾아 AF 번호, 없으면 -1.
- `table_ok(table)` : 표 자체의 오타 검사 (pin < 16, af < 16, 같은 키 중복 없음). `static_assert(table_ok(AF_TABLE))` 로 고정한다.
- `is_pin_v<T>` : `Pin<P, N>` 인가 (`const` 포함). 부분 특수화로 만든다.
- `configure<P, S>()` : AFR → OTYPER → MODER 순서로 레지스터를 쓴다 (클럭 먼저, MODER 는 마지막).

### Part B — 읽기 쉬운 실패

- `pin_error<P, S, O>()` : 네 가지 이유를 enum 으로 돌려준다. `P::number` 를 함부로 건드리면 `P = int` 에서 다른 에러가 터진다.
- I2C 는 오픈 드레인만 허용 (`O` 태그 타입: `PushPull` / `OpenDrain`).
- `otype_bit<O>()` : `if constexpr` 사슬의 마지막에 dependent-false `static_assert`.
- `configure` 는 이유별 `static_assert` 4개 + `if constexpr` 로 **2차 에러를 막는다**.

### Part C — 보드 표 전체 검증과 범위 밖 설정

- `first_bad_row(board)`, `first_duplicate_pin(board)`, `first_unpaired_usart(board)` : 문제 행의 번호, 없으면 -1.
- `apply_board<BOARD>()` : 검사 3개를 `static_assert` 로 걸고, 통과하면 행을 **템플릿 인자로 펼쳐서** 적용한다.
- `baud_error(pclk, baud)` / `usart_brr<Pclk, Baud>()` : 분주값 범위(16 ≤ BRR ≤ 0xFFFF)와 오차(2.5% 이하, 이 레슨의 정책)를 검사한다.

## 2. Problem (English)

> **Compile-time pin-mux validation for a GPIO HAL.**
>
> 1. Given a `constexpr` table of `{port, pin, signal, af}` rows, implement `constexpr int find_af(Port, unsigned pin, Signal)` returning the AF number or -1, and `table_ok(table)` rejecting out-of-range pins/AF numbers and duplicate keys.
> 2. Implement `template <typename P, Signal S, typename O = PushPull> void configure()`. It must fail to compile with a *specific, readable* message if `P` is not a `Pin<Port, N>`, if the pin number is out of range, if `S` is not routed to `P`, or if an I2C signal is not open-drain. Each failure must produce exactly one primary error (no cascades). The valid path writes clock enable, AFR, OTYPER, then MODER, in that order.
> 3. A `static_assert` message in C++20 must be a string literal. Show how to make the compiler output name the offending pin and signal anyway.
> 4. Validate a whole-board table at compile time (unknown pin/signal rows, a pin used twice, a USART TX without its RX) and expose the offending row index. Apply a valid board with no run-time table walk.
> 5. Compute a USART BRR value at compile time and reject baud rates whose divisor or rate error is out of range.
>
> Example: `configure<PA9, Signal::USART1_TX>()` compiles and writes AF7 to AFRH; `configure<PA9, Signal::I2C1_SCL>()` must not compile.

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
# 처음엔 "17/59 passed" — TODO 를 채워 59/59 를 만든다
```

## 4. 힌트

<details>
<summary>힌트 1 — find_af 와 table_ok 는 평범한 루프</summary>

`constexpr` 함수에서 범위 기반 for 를 그대로 쓴다. `table_ok` 는 이중 루프 (`j = i + 1` 부터). 표는 배열 참조로 받는다: `template <std::size_t N> constexpr bool table_ok(const AfRow (&t)[N])`. 그래야 잘못된 표를 넘겨 테스트할 수 있다.

</details>

<details>
<summary>힌트 2 — is_pin_v 는 부분 특수화 한 줄</summary>

`template <Port P, unsigned N> struct is_pin_impl<Pin<P, N>> : std::true_type {};`. `const PA9` 도 받으려면 사용하는 쪽에서 `std::remove_cv_t` 를 씌운다 (시작 코드에 이미 있다).

</details>

<details>
<summary>힌트 3 — pin_error 에서 P::number 를 언제 건드리나</summary>

`P = int` 이면 `P::number` 자체가 에러다. 일반 `if (!is_pin_v<P>)` 는 뒤쪽 코드도 컴파일하므로 안 된다. `if constexpr (!is_pin_v<P>) { ... } else { ... }` 처럼 `else` 안에 넣어야 버려진다 ([stmt.if]).

</details>

<details>
<summary>힌트 4 — static_assert 조건 안에서 함수 인자를 쓸 수 없다</summary>

`constexpr` 함수의 인자는 상수식이 아니다. 그래서 `configure` 는 값을 템플릿 인자로 받고, `constexpr PinError e = pin_error<P, S, O>();` 처럼 **상수 지역 변수**를 만들어 `static_assert(e != ...)` 에 쓴다.

</details>

<details>
<summary>힌트 5 — 2차 에러를 막으려면</summary>

`static_assert` 가 실패해도 컴파일러는 계속 간다. 뒤의 `apply_pin<P::port, ...>` 가 또 터지면 진짜 원인이 묻힌다. 본문을 `if constexpr (e == PinError::None)` 으로 감싼다. 확인은 `demo_errors.cpp` 로 (`-DSHOW_ERROR=1`).

</details>

<details>
<summary>힌트 6 — 보드 표를 행 단위 템플릿으로 펼치기</summary>

C++20 부터 구조체를 NTTP 로 받을 수 있다: `template <PinAssign R> void apply_row()`. 표는 `template <const auto& B>` 로 받고, `[]<std::size_t... I>(std::index_sequence<I...>) { (apply_row<B[I]>(), ...); }(std::make_index_sequence<N>{})` 로 펼친다.

</details>

## 5. 풀이

### Part A — 표를 코드로, 번호 대신 이름으로

**왜 AF 번호가 아니라 `Signal` 이름을 받나.** `configure<PA9, 7>` 은 "PA9 의 AF7" 이지 무슨 기능인지 말해 주지 않는다. 같은 7 이 다른 핀에선 다른 기능이다. 이름을 받고 번호는 표에서 찾으면 호출부가 의도를 그대로 말하고, 번호를 잘못 옮기는 실수가 사라진다.

`AF_TABLE` 은 STM32F4 계열의 AF 표를 본뜬 학습용 일부다. **데이터시트와 한 줄씩 대조하지는 않았으므로(미확인)** 실제 칩에 쓸 때는 해당 데이터시트의 AF 표로 바꿔야 한다.

**표 자체도 틀릴 수 있다.** 그래서 표를 검증한다. 일부러 `{Port::A, 9, Signal::USART1_TX, 8}` 행을 추가했을 때의 실제 출력:

```text
typo.cpp:104:15: error: static assertion failed due to requirement 'table_ok(AF_TABLE)': AF_TABLE: pin/af out of range or duplicated row
```

**레지스터 배치** (AF 번호 4비트, MODER 2비트).

```svg
<svg viewBox="0 0 740 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">PA9 = AF7 일 때 쓰는 비트</text>
  <g stroke="currentColor">
    <rect x="10" y="40" width="340" height="30" class="f-mut"/>
    <rect x="360" y="40" width="340" height="30" class="f-mut"/>
    <rect x="391" y="40" width="42" height="30" class="f-acc"/>
    <rect x="130" y="110" width="30" height="30" class="f-acc"/>
    <rect x="10" y="110" width="340" height="30" class="f-mut" fill-opacity="0.4"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="60">AFRL: 핀 0..7</text>
    <text x="440" y="60">AFRH: 핀 8..15</text>
    <text x="394" y="88">7</text>
    <text x="134" y="158">10b</text>
  </g>
  <g fill="currentColor">
    <text x="480" y="88">핀 9 → (9 % 8) * 4 = 비트 4..7</text>
    <text x="360" y="130">MODER: 핀 9 → 비트 18..19 = 10b (AF 모드)</text>
    <text x="10" y="186">쓰기 순서 = 클럭 → AFR → OTYPER → MODER. MODER 가 AF 모드가 되는 순간 AF 번호가 이미 맞아 있어야 한다.</text>
  </g>
</svg>
```

왜 MODER 를 마지막에 쓰나: MODER 를 먼저 AF 로 바꾸면 AFR 이 쓰이기 전까지 그 핀은 이전 AF 번호(리셋 값은 AF0)로 동작한다. 그 사이에 엉뚱한 신호가 잠깐 나갈 수 있다. ST HAL 의 `HAL_GPIO_Init` 도 AFR 을 먼저 쓴다고 알고 있으나 여기서 소스를 열어 확인하지는 않았다(미확인). 하네스는 mock 로그로 순서를 검사한다 (`log[1].reg == AFRH`, `log[3].reg == MODER`). 이 순서를 뒤집은 돌연변이 코드는 `58/59` 로 잡혔다.

**trait.** `is_pin_v` 는 전형적인 "템플릿의 특정 인스턴스인가" 판별이다. 주 템플릿은 `false_type`, `Pin<P, N>` 부분 특수화만 `true_type`. `Pin<Port::A, 16>` 도 **타입 자체는 유효**하고 `is_pin_v` 는 true 다. 범위 검사는 별도 단계에서 한다 (왜 `Pin` 안에 `static_assert` 를 안 넣었는지는 Part B).

### Part B — 왜 이유를 값으로 먼저 계산하나

`configure` 안의 `static_assert` 는 테스트할 수 없다. 실패하면 **빌드가 끝나 버리므로** 런타임 CHECK 도, `requires` 도 못 쓴다. 게다가 `requires { configure<int, S>(); }` 는 **true** 다. 선언은 멀쩡하고 본문은 인스턴스화되지 않기 때문이다 (CPP-B12 참고). 하네스의 `can_name_configure` 가 이것을 그대로 보여 준다.

그래서 판정을 분리한다.

```text
pin_error<P, S, O>()   ← 순수 함수. 값으로 테스트 가능 (bad3 == SignalNotOnPin ...)
configure<P, S, O>()   ← 그 값을 사람이 읽을 메시지로 바꾸고, 맞으면 레지스터를 쓴다
```

이렇게 하면 `configure` 는 얇아지고, 판정표는 전부 하네스로 검증된다.

**if constexpr 가드가 필요한 이유.** `demo_errors.cpp` 의 `configure_noguard` 는 `static_assert` 뒤에 그냥 본문을 썼다. 실제 결과 (`-DSHOW_ERROR=13`):

```text
demo_errors.cpp:119:19: error: static assertion failed due to requirement 'is_pin_v<int>': configure: P must be a Pin<Port, N> type
demo_errors.cpp:167:5: note: in instantiation of function template specialization 'configure_noguard<int, Signal::USART1_TX>' requested here
demo_errors.cpp:120:34: error: type 'int' cannot be used prior to '::' because it has no members
2 errors generated.
```

GCC 는 같은 코드에서 `error:` 를 4개 낸다 (`'port' is not a member of 'int'`, `'number' is not a member of 'int'`, `non-constant condition for static assertion` 까지). 가드를 쓴 진짜 `configure<int, ...>` 는 양쪽 모두 1개만 낸다.

**이제 진짜 에러 메시지** (`demo_errors.cpp`, 맥 clang 21, `-DSHOW_ERROR=N -fsyntax-only`, 소스 인용 줄만 제거):

```text
# 1) configure<PA9, Signal::I2C1_SCL>() — 표에 없는 조합
demo_errors.cpp:63:19: error: static assertion failed due to requirement 'e != PinError::SignalNotOnPin': configure<P, S>: signal S is not routed to pin P on this chip (see AF_TABLE / datasheet)
demo_errors.cpp:146:5: note: in instantiation of function template specialization 'configure<Pin<Port::A, 9>, Signal::I2C1_SCL, PushPull>' requested here
demo_errors.cpp:63:21: note: expression evaluates to '3 != 3'

# 3) configure<int, Signal::USART1_TX>() — 핀이 아님
demo_errors.cpp:61:19: error: static assertion failed due to requirement 'e != PinError::NotAPin': configure<P, S>: P must be a Pin<Port, N> type (e.g. PA9)
demo_errors.cpp:150:5: note: in instantiation of function template specialization 'configure<int, Signal::USART1_TX, PushPull>' requested here

# 4) configure<PB6, Signal::I2C1_SCL>() — 기본값 PushPull
demo_errors.cpp:65:19: error: static assertion failed due to requirement 'e != PinError::I2cNeedsOpenDrain': configure<P, S, O>: I2C pins must be configured OpenDrain

# 5) configure<PA9, Signal::USART1_TX, int>() — 잘못된 출력 타입 (dependent-false 발동)
demo_errors.cpp:43:26: error: static assertion failed due to requirement 'dependent_false_v<int>': otype_bit<O>: O must be PushPull or OpenDrain
demo_errors.cpp:68:50: note: in instantiation of function template specialization 'otype_bit<int>' requested here
demo_errors.cpp:154:5: note: in instantiation of function template specialization 'configure<Pin<Port::A, 9>, Signal::USART1_TX, int>' requested here
```

읽는 법: 첫 줄은 **무엇이 틀렸는가**(내가 쓴 메시지), `in instantiation ... requested here` 줄은 **어느 핀이 틀렸는가**(템플릿 인자 그대로), `requested here` 의 위치는 **호출한 곳**이다. `expression evaluates to '3 != 3'` 은 enum 의 숫자 값이라 도움이 안 된다.

**문자열 리터럴 제약 안에서 이름을 찍는 기법들.** C++20 까지 `static_assert` 의 메시지는 문자열 리터럴이다 ([dcl.pre]). 템플릿 인자를 끼워 넣은 문장은 쓸 수 없다. 대신 쓰는 것들을 같은 입력(`PA9` + `I2C1_SCL`)으로 비교했다.

| 기법 | clang 21 실제 출력의 핵심 | g++ 14.2.1 실제 출력의 핵심 |
|---|---|---|
| T0 `requested here` 체인 (기본) | `configure<Pin<Port::A, 9>, Signal::I2C1_SCL, PushPull>` | `[with P = Pin<Port::A, 9>; Signal S = Signal::I2C1_SCL; O = PushPull]` |
| T1 메시지 없는 비교식 | `find_af(..., (Signal)'\x02') >= 0` / `-1 >= 0` | `the comparison reduces to '(-1 >= 0)'` |
| T2 증인(witness) 템플릿 | `implicit instantiation of undefined template 'PIN_HAS_NO_SUCH_AF<Port::A, 9, Signal::I2C1_SCL>'` | `'PIN_HAS_NO_SUCH_AF<Port::A, 9, Signal::I2C1_SCL> oops' has incomplete type` |
| T3 consteval 실패 경로 | `in call to 'checked_af(0, 9, 2)'` — **숫자로 찍힘** | `checked_af(Port::A, 9, Signal::I2C1_SCL)` — 이름으로 찍힘 |
| T4 표의 행 번호 | `expression evaluates to '1 == -1'` | `the comparison reduces to '(1 == -1)'` |
| T5 행을 NTTP 로 | `check_row<PinAssign{0, 9, 2, true}>` — 숫자 | `check_row() [with PinAssign R = PinAssign{Port::A, 9, Signal::I2C1_SCL, true}]` |

정리하면, **메시지 문자열은 "규칙"을, `requested here` 는 "값"을 맡긴다**. 추가 도구가 꼭 필요하지 않다. T2 는 clang 에서 enumerator 이름을 살리는 확실한 방법이지만, 메시지 대신 "undefined template" 이 나오므로 팀에 설명이 필요하다. T1 은 clang 에서 enum 을 `(Signal)'\x02'` 로 찍어 오히려 읽기 나쁘다.

**C++26 이면 진짜 문장을 만들 수 있다** (P2741). `.size()` / `.data()` 를 가진 상수식 객체를 메시지로 넘긴다. `demo_msg_cxx26.cpp` 가 표를 돌며 대안 핀까지 문장으로 조립한다. 실제 출력:

```text
# clang 21, -std=c++2c
demo_msg_cxx26.cpp:48:19: error: static assertion failed due to requirement 'find_af(Pin<Port::A, 9>::port, Pin<Port::A, 9>::number, (Signal)'\x01') >= 0': PA9 has no I2C1_SCL; try: PB6 PB8

# arm-none-eabi-g++ 14.2.1, -std=c++26
demo_msg_cxx26.cpp:48:50: error: static assertion failed: PA9 has no I2C1_SCL; try: PB6 PB8
```

`-std=c++20` 으로 빌드하면 clang 은 `warning: 'static_assert' with a user-generated message is a C++26 extension [-Wc++26-extensions]` 경고와 함께 같은 메시지를 낸다 (GCC 14.2.1 도 비슷한 경고 후 동작, `-Werror` 면 빌드 실패). 즉 `-std=c++20 -Werror` 프로젝트에서는 쓸 수 없다. 참고로 `__cpp_static_assert` 는 맥 clang 21 이 `-std=c++20` 에서도 `202306L` 을 정의했고, GCC 14.2.1 은 `-std=c++20` 에서 `201411L`, `-std=c++26` 에서 `202306L` 이었다. 이 매크로로 C++26 여부를 판단하면 clang 에서 틀릴 수 있다.

**dependent-false 함정 (CWG2518).** 고전적인 규칙은 이랬다: 템플릿 정의 안의 `static_assert(false)` 는 "유효한 특수화를 하나도 만들 수 없는 템플릿" 이라 ill-formed NDR 이었고 ([temp.res]), 실제로 많은 컴파일러가 인스턴스화 전에 바로 실패시켰다. 그래서 `static_assert(dependent_false_v<T>)` 라는 우회가 관례가 됐다. 이후 CWG2518 / P2593 으로 규칙이 바뀌어 템플릿 정의에 있는 `static_assert` 는 인스턴스화 전에는 효력이 없다 ([dcl.pre]). `demo_dep_false.cpp` 로 지금 컴파일러를 직접 확인한 결과:

| 코드 | clang 21 (`-std=c++17/20/23/2c`) | g++ 14.2.1 (`-std=c++17/20/23`) |
|---|---|---|
| 형태 A `static_assert(false, ...)`, 인스턴스화 안 함 | 통과 (rc=0, 경고 없음) | 통과 (경고 없음) |
| 형태 B `static_assert(dependent_false_v<T>, ...)`, 인스턴스화 안 함 | 통과 | 통과 |
| 형태 C 비의존 조건 `static_assert(sizeof(int) == 1)`, 정의만 있음 | 통과 (의외일 수 있음) | 통과 |
| A/B/C 를 **인스턴스화** | 실패 | 실패 |

```text
# clang 21, SHOW_ERROR=1 — static_assert(false) 를 인스턴스화했을 때
demo_dep_false.cpp:13:24: error: static assertion failed: width_a<T>: unsupported T
demo_dep_false.cpp:31:10: note: in instantiation of function template specialization 'width_a<float>' requested here

# clang 21, SHOW_ERROR=2 — dependent_false_v 쪽. 조건이 이름으로 찍혀 약간 더 낫다
demo_dep_false.cpp:20:24: error: static assertion failed due to requirement 'dependent_false_v<float>': width_b<T>: unsupported T
```

현재 도구로는 형태 A 가 안전하다. 그래도 실무에서 `dependent_false_v` 를 계속 쓰는 이유: 낡은 컴파일러(arm-none-eabi 구버전 등)와 **표준 이전 동작**에서도 안전하고, clang 은 조건식을 찍어 준다. 어느 버전부터 바뀌었는지(clang 17, GCC 13 으로 알고 있음)는 이 환경에서 돌려 보지 못했으므로 **미확인**이다. 프로젝트 툴체인에서 위 표를 직접 돌려 보면 된다.

`-Wreturn-type` 주의: g++ 는 `else` 쪽에 반환문이 없는 형태에서 `no return statement` 를 추가로 냈다 (constexpr 함수에서는 에러). 그래서 `otype_bit` 의 마지막 가지는 `static_assert(...); return 0u;` 로 썼다.

**`Pin` 안에 static_assert 를 넣지 않은 이유.** 넣으면 `requires` 로 물어보는 순간 하드 에러다 (`-DSHOW_ERROR=17`):

```text
demo_errors.cpp:138:56: error: static assertion failed due to requirement '16U < 16': StrictPin<N>: N must be 0..15
demo_errors.cpp:139:62: note: in instantiation of template class 'StrictPin<16>' requested here
demo_errors.cpp:139:62: note: in instantiation of requirement here
```

클래스 인스턴스화 중의 `static_assert` 실패는 SFINAE 의 즉시 문맥이 아니다. "트레잇으로 조용히 물어볼 수 없다"는 뜻이다. 판단은 `pin_error` 쪽에 모아야 테스트가 쉽다. 또 하나의 함정: 반환 타입이 `auto` 인 함수는 `requires` 가 **본문까지 인스턴스화**한다. `template <typename T> auto f()` 의 `static_assert` 가 `requires { f<int>(); }` 에서 터지는 것을 clang 21 로 확인했다 (`note: in instantiation of requirement here`).

**concepts 로 바꾸면.** 공개 API 를 `template <PinType P, Signal S> requires RoutedTo<P, S>` 처럼 제약하면 `requires` 로 검사 가능해지고 오버로드와도 어울린다. 다만 "왜 안 되는지" 문장은 직접 쓸 수 없어 에러가 제약식 이름에 의존한다. 자세한 비교는 MCPP-M05 와 CPP-B12 소관이다.

### Part C — 보드 표는 전체를 본다

핀 하나씩 맞아도 보드 전체로는 틀릴 수 있다. PB6 을 USART1_TX 와 I2C1_SCL 양쪽에 배정하거나, TX 만 배치하고 RX 를 빠뜨리는 실수가 그렇다.

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">apply_board&lt;BOARD&gt;() — 컴파일 타임 검증 후 행별 전개</text>
  <rect x="10" y="40" width="200" height="118" rx="8" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="62">BOARD[] (constexpr)</text>
    <text x="20" y="84">{A, 9, USART1_TX}</text>
    <text x="20" y="104">{A,10, USART1_RX}</text>
    <text x="20" y="124">{B, 6, I2C1_SCL }</text>
    <text x="20" y="144">{B, 7, I2C1_SDA }</text>
  </g>
  <g stroke="currentColor" fill="none"><path d="M210 99 L255 99"/><path d="M500 99 L540 99"/></g>
  <rect x="255" y="40" width="245" height="118" rx="8" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="265" y="62">static_assert 3개</text>
    <text x="265" y="86" font-size="12">first_bad_row == -1</text>
    <text x="265" y="106" font-size="12">first_duplicate_pin == -1</text>
    <text x="265" y="126" font-size="12">first_unpaired_usart == -1</text>
    <text x="265" y="148" font-size="12">실패 시 행 번호가 에러에 보인다</text>
  </g>
  <rect x="540" y="40" width="190" height="118" rx="8" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="550" y="62">fold 로 행 전개</text>
    <text x="550" y="86" font-size="12" font-family="monospace">apply_row&lt;B[0]&gt;()</text>
    <text x="550" y="106" font-size="12" font-family="monospace">apply_row&lt;B[1]&gt;() ...</text>
    <text x="550" y="130" font-size="12">표는 런타임에 남지 않음</text>
  </g>
  <text x="10" y="194" fill="currentColor">-DSHOW_ERROR=6 실제 출력: expression evaluates to '1 == -1' → 1번 행이 범인.</text>
  <text x="10" y="216" fill="currentColor">행을 NTTP 로 넘기면 체인에 그 행의 값이 찍힌다 (clang 은 숫자, GCC 는 이름).</text>
</svg>
```

```text
# SHOW_ERROR=6 (clang 21)
demo_errors.cpp:85:19: error: static assertion failed due to requirement 'first_bad_row(BOARD_BAD) == -1': board: row is not in AF_TABLE, or I2C row without open_drain
demo_errors.cpp:85:36: note: expression evaluates to '1 == -1'
demo_errors.cpp:82:19: error: static assertion failed due to requirement 'find_af(PinAssign{0, 9, 2, true}.port, PinAssign{0, 9, 2, true}.pin, PinAssign{0, 9, 2, true}.signal) >= 0': board row: signal is not routed to this pin
demo_errors.cpp:157:5: note: in instantiation of function template specialization 'check_row<PinAssign{0, 9, 2, true}>' requested here
```

(`check_row` 는 demo 에서 NTTP 찍힘을 보여 주려고 따로 부른 것이다.) 이런 `== -1` 형태는 Part B 의 T4 다. "몇 번째 행" 정보가 메시지 없이도 컴파일러의 expansion 으로 나온다.

**fold 로 전개한 이유.** `for (auto& r : B) apply_row(r)` 처럼 런타임 루프를 쓰면 AF 번호를 런타임에 찾거나 표를 플래시에 남겨야 한다. 행을 템플릿 인자로 넘기면 모든 값이 상수이고 표는 사라진다. 비용은 아래에서 실측한다.

**범위 밖 설정.** `usart_brr<16'000'000, 2'000'000>()` 의 실제 에러 (`-DSHOW_ERROR=7`):

```text
demo_errors.cpp:94:19: error: static assertion failed due to requirement 'div >= 16': usart_brr: baud too high for this PCLK (BRR < 16)
demo_errors.cpp:159:11: note: in instantiation of function template specialization 'usart_brr<16000000U, 2000000U>' requested here
demo_errors.cpp:94:23: note: expression evaluates to '8 >= 16'
```

`solution.cpp` 의 `usart_brr` 는 `constexpr BaudError e = baud_error(Pclk, Baud);` 를 먼저 계산한다. `baud_error` 자체는 평범한 `constexpr` 함수라 인자가 변수여도 호출할 수 있고, 하네스의 표 기반 테스트는 이 함수를 경계값(BRR=16 하한 경계, `245` 통과, `244` 초과, 975000 오차 초과)으로 직접 찌른다. `static_assert` 조건식에는 함수 인자를 넣을 수 없다 (`-DSHOW_ERROR=15`):

```text
demo_errors.cpp:132:19: error: static assertion expression is not an integral constant expression
demo_errors.cpp:132:19: note: function parameter 'pin' with unknown value cannot be used in a constant expression
```

**정말 런타임 비용이 0 인가.** `demo_target.cpp` 는 헤더 없이 Cortex-M4 오브젝트를 만들어 `configure<PA9, USART1_TX>()` (`cfg_hal`) 와 손으로 쓴 RMW (`cfg_hand`) 를 비교한다. `clang++ --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c++20 -O2 -fno-exceptions -fno-rtti -c`, `xcrun llvm-objdump -d` 실제 출력:

```text
00000000 <cfg_hal>:
       0:      	movw	r0, #0x3830
       4:      	movt	r0, #0x4002
       8:      	ldr	r1, [r0]
       a:      	movs	r2, #0x7
       c:      	orr	r1, r1, #0x1
      10:      	str	r1, [r0]
      12:      	movs	r0, #0x0
      14:      	movt	r0, #0x4002
      18:      	ldr	r1, [r0, #0x24]
      1a:      	bfi	r1, r2, #4, #4
      1e:      	str	r1, [r0, #0x24]
      20:      	ldr	r1, [r0, #0x4]
      22:      	movs	r2, #0x2
      24:      	bic	r1, r1, #0x200
      28:      	str	r1, [r0, #0x4]
      2a:      	ldr	r1, [r0]
      2c:      	bfi	r1, r2, #18, #2
      30:      	str	r1, [r0]
      32:      	bx	lr
```

`cfg_hand` 를 주소만 지우고 `diff` 하면 19줄이 전부 같다. 크기(`llvm-size -A`, `llvm-nm -C --size-sort`):

| 오브젝트 | 빌드 | `.text` | AF_TABLE 섹션 | 비고 |
|---|---|---|---|---|
| `cfg_hal` + `cfg_hand` | 기본 | 104 | 없음 | 둘 다 `0x34`=52바이트 |
| + 런타임 표 탐색 `cfg_runtime` | `-DWITH_RUNTIME` | 322 | `.rodata.AF_TABLE` 20바이트 | `cfg_runtime` 은 `0xda`=218바이트 |
| + `apply_board` 식 전개 `cfg_board` (3행) | `-DWITH_BOARD` | 236 | 없음 | `cfg_board`=`0x84`=132바이트, 행당 RMW 3회 + 클럭 |

"런타임 탐색은 표가 플래시에 남고 잘못된 입력은 실행 중에야 -1 로 드러난다, 컴파일 타임은 표가 안 남고 잘못된 입력은 빌드가 실패한다." — 이것이 수치로 확인된 부분이다. Thumb-2 코드의 동일성은 위 조건(clang, -O2)에서 확인한 것이며, arm-none-eabi-g++ 의 출력은 비교하지 않았다.

**돌연변이 검증.** 맞는 코드를 일부러 망가뜨린 변형 10개를 돌렸고 전부 잡혔다 (MODER 값, AFRL/AFRH 경계, shift 계산, 표 검사 경계, I2C 오픈 드레인 빠뜨림, 중복 핀 비교, BRR 상한, 오차 계산, 반올림, 쓰기 순서). 오차 계산 변형은 CHECK 이전에 `usart_brr` 의 `static_assert` 가 컴파일 단계에서 막았다.

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
F="-std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti"
c++ $F solution.cpp -o solution && ./solution        # 59/59 passed
c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer solution.cpp -o sol_asan && ./sol_asan   # 깨끗하게 59/59

# 에러 관찰 (N = 1..7, 10..17). -Werror 없이 돌린다 (SHOW_ERROR=14 경고 확인용)
c++ -std=c++20 -Wall -Wextra -fno-exceptions -fno-rtti -DSHOW_ERROR=1 -fsyntax-only demo_errors.cpp

# dependent-false 실험 (-std 만 바꿔 가며)
c++ -std=c++20 -Wall -Wextra -fno-exceptions -fno-rtti -fsyntax-only -DSHOW_ERROR=1 demo_dep_false.cpp

# C++26 메시지
c++ -std=c++2c -Wall -Wextra -fno-exceptions -fno-rtti -fsyntax-only -DSHOW_ERROR demo_msg_cxx26.cpp

# Cortex-M4 비용 확인
clang++ --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c++20 -O2 -fno-exceptions -fno-rtti -c demo_target.cpp -o /tmp/t.o
xcrun llvm-objdump -d --no-show-raw-insn /tmp/t.o; xcrun llvm-size -A /tmp/t.o; xcrun llvm-nm -C --size-sort /tmp/t.o
# 표가 플래시에 남는 판: -DWITH_RUNTIME, 보드 전개: -DWITH_BOARD
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| AF 번호를 템플릿 인자로 직접 받기 | 같은 번호가 핀마다 다른 뜻. 번호를 잘못 옮겨도 표는 그 번호를 "유효" 로 볼 수 있음 | `Signal` 이름을 받고 번호는 표에서 찾기 |
| `if (!is_pin_v<P>) ... P::number` | 일반 `if` 의 양쪽 가지는 다 컴파일. `P = int` 에서 `P::number` 하드 에러 | `if constexpr` 로 버리기 |
| `static_assert` 뒤에 본문을 그대로 | 조건이 false 여도 컴파일 계속 → 2차 에러가 원인을 가린다 (clang 2개, GCC 4개 실측) | `if constexpr (e == PinError::None) { ... }` |
| `constexpr int f(int x) { static_assert(x > 0); }` | 인자는 상수식 아님. 에러: `function parameter 'pin' with unknown value` | NTTP 로 받기 / consteval 함수에서 실패 경로 호출 |
| `Pin` 클래스 안에 범위 `static_assert` | 클래스 인스턴스화 중의 실패는 SFINAE 아님 → 트레잇으로 못 물어봄 (`-DSHOW_ERROR=17`) | 판정은 별도 `pin_error` 로 |
| 판정 로직을 `configure` 본문의 `static_assert` 에만 두기 | 실패 경로는 테스트 불가 (빌드가 끝나 버림) | `PinError` 값을 만드는 순수 함수로 분리 |
| `requires { configure<int, S>(); }` 로 부정 테스트 | 본문 `static_assert` 는 안 보임 → true → 테스트가 거짓 통과 | `pin_error<...>() == ...` 를 비교 |
| 반환 타입 `auto` + 본문 `static_assert` + `requires` | 반환 타입 추론 때문에 본문까지 인스턴스화 → 부정 검사 도중 하드 에러 | 반환 타입을 명시하거나 판정 함수로 분리 |
| `static_assert(cond, make_msg())` 를 C++20 에서 | 표준에선 문자열 리터럴만 (C++26 P2741). clang/GCC 는 확장으로 받되 경고 → `-Werror` 면 실패 | 리터럴 + `requested here` 활용, 혹은 C++26 |
| `__cpp_static_assert >= 202306L` 로 C++26 메시지 분기 | 맥 clang 21 은 `-std=c++20` 에서도 202306L (실측). GCC 는 201411L | `__cplusplus` 확인 또는 그냥 쓰지 않기 |
| 표를 복붙하고 검증 안 함 | 같은 행 중복, AF ≥ 16 도 조용히 컴파일 | `static_assert(table_ok(AF_TABLE))` |
| MODER 를 먼저 쓰기 | AFR 이 쓰이기 전 잠깐 다른 AF 로 동작 | 클럭 → AFR → OTYPER → MODER |
| `CHECK(is_pin_v<Pin<Port::C, 15>>)` | 템플릿 인자의 쉼표를 매크로가 인자 구분자로 읽음 | `CHECK((is_pin_v<Pin<Port::C, 15>>))` 괄호 추가 |

## 8. 면접 Q&A

<details>
<summary>Q1. AF 번호 대신 신호 이름을 인자로 받으면 뭐가 좋은가요?</summary>

의도가 호출부에 남고, 번호 옮겨 적기 실수가 사라지고, 표가 칩을 바꾸는 단 하나의 지점이 된다. `configure<PA9, Signal::USART1_TX>()` 는 PB6 으로 옮겨도 AF 번호를 신경 쓸 필요가 없다. 칩이 바뀌면 표만 바꾸고 다시 빌드해서 깨지는 호출을 찾는다. 꼬리질문 "번호가 필요한 저수준 API 는?" → 번호 범위와 "그 핀에 그 번호가 있나"만 검증하는 별도 함수를 둔다 (추가 연습 2).

</details>

<details>
<summary>Q2. static_assert 메시지에는 템플릿 인자를 못 넣는데, 어떻게 문제 핀을 알려 주나요?</summary>

C++20 까지 메시지는 문자열 리터럴이다 ([dcl.pre]). 그래서 메시지는 **규칙**을 적고 값은 다른 경로로 얻는다. 기본은 `in instantiation of ... 'configure<Pin<Port::A, 9>, Signal::I2C1_SCL, PushPull>' requested here` 체인이다. 더 강하게 하려면 정의 없는 증인 템플릿 `PIN_HAS_NO_SUCH_AF<P::port, P::number, S>` 에 값을 실어 "undefined template" 에러로 타입 이름을 찍는다. 조건식을 `first_bad_row(b) == -1` 로 쓰면 clang 이 `'1 == -1'` 로 행 번호를 알려 준다. C++26 부터는 P2741 로 상수식 문자열을 메시지로 쓸 수 있다 (clang 21 / g++ 14.2.1 에서 `-std=c++2c`/`c++26` 로 동작 확인).

</details>

<details>
<summary>Q3. 템플릿 안에 static_assert(false) 를 써도 되나요?</summary>

표준상 예전에는 "유효한 특수화가 없는 템플릿" 으로 ill-formed NDR 이었고 많은 컴파일러가 인스턴스화 전에 바로 실패시켰다. 그래서 `static_assert(dependent_false_v<T>)` 관례가 생겼다. CWG2518/P2593 이후 템플릿 정의의 `static_assert` 는 인스턴스화 전에 효력이 없고, 맥 clang 21 과 arm-none-eabi-g++ 14.2.1 에서 `-std=c++17` 이상 전부 통과함을 직접 확인했다 (비의존 조건인 `sizeof(int) == 1` 도 정의만 있으면 통과). 낡은 툴체인을 지원해야 하면 여전히 `dependent_false_v` 가 안전하다.

</details>

<details>
<summary>Q4. static_assert 로 막는 것과 concept 으로 막는 것은 언제 고르나요?</summary>

`static_assert` 는 메시지를 직접 쓸 수 있고 체인에 인자가 다 찍히지만 오버로드나 `requires` 로 물어볼 수 없다. concept 은 선언의 일부라 오버로드와 함께 쓸 수 있지만 사람이 읽을 문장은 쓸 수 없다. 저는 공개 API 가 오버로드/템플릿 메타 쪽에서 쓰이면 concept, HAL 호출부의 "이 핀에 이 기능 없음" 은 `static_assert` 로 간다. 두 가지 다 쓰는 것도 흔하다 (자세한 건 MCPP-M05, CPP-B12).

</details>

<details>
<summary>Q5. 이 검증들의 비용은요? 표는 어디로 갔나요?</summary>

Cortex-M4 로 컴파일해 `configure<PA9, Signal::USART1_TX>()` 의 디스어셈블리를 손으로 쓴 RMW 와 비교했을 때 `diff` 결과 19줄이 전부 동일했다. 표는 상수식에서만 ODR-use 없이 읽히기 때문에 오브젝트에 `AF_TABLE` 섹션이 생기지 않는다 (`.text` 104바이트, 표 0바이트). 대조로 런타임 표 탐색판을 넣으면 `.rodata.AF_TABLE` 20바이트와 218바이트 함수가 생긴다. 꼬리질문 "컴파일 시간은?" → 표가 작을 땐 무시 가능하고, 큰 표에 O(N²) 검사를 하면 컴파일 시간이 늘 수 있다 (여기서는 측정하지 않았다).

</details>

<details>
<summary>Q6. constexpr 함수에서 인자 범위를 검사하고 싶은데 static_assert 가 안 됩니다. 왜 그렇죠?</summary>

인자는 런타임 값일 수 있어서 상수식이 아니다. 에러: `function parameter 'pin' with unknown value cannot be used in a constant expression`. 방법은 셋이다: (1) 값을 NTTP 로 받기 (`template <unsigned Pin>`). (2) `consteval` 함수 안에서 잘못된 경우 constexpr 아닌 함수를 호출 — 상수 평가가 그 경로에 닿을 때만 실패하므로 정상 입력은 통과. 단 `-fno-exceptions` 라 `throw` 경로 대신 정의 없는 일반 함수를 호출하는 식으로 쓴다. (3) C++20 `std::is_constant_evaluated()` 로 나눌 수는 있지만 런타임 입력까지 막지는 못한다.

</details>

<details>
<summary>Q7. 표를 코드에 손으로 쓰면 또 틀리지 않나요?</summary>

그래서 두 겹으로 막는다. `table_ok` 가 오타를 컴파일 타임에 잡고, 표 자체는 벤더 데이터(칩 벤더의 핀 설명 파일이나 pinout 데이터 — 포맷은 칩마다 다름)에서 스크립트로 생성하는 게 이상적이다. 표 생성 코드에 CI 에서 대조 테스트를 건다. 동일 부품군에서도 패키지에 따라 없는 핀이 있으니 패키지 enum 을 하나 더 받아 필터링하는 설계로 확장한다.

</details>

<details>
<summary>Q8. 왜 Pin 클래스 템플릿 안에 static_assert 를 안 넣었나요?</summary>

넣으면 `Pin<A, 16>` 에 대해 묻는 순간 하드 에러가 나서 트레잇으로 조용히 판정할 수 없다 (`in instantiation of template class`, SFINAE 즉시 문맥 아님). 판정을 함수(`pin_error`)로 빼면 잘못된 입력도 값으로 다룰 수 있어 테스트가 된다. 사용자에게 보이는 에러는 `configure` 가 낸다.

</details>

## 9. 추가 연습

1. **`pin_count(signal)`**: 그 기능을 낼 수 있는 핀 수. 에러 메시지에 대안을 알려 주는 재료가 된다.

<details>
<summary>풀이</summary>

표를 한 번 훑어 세는 `constexpr` 함수. `static_assert(pin_count(Signal::TIM3_CH1) == 1)` 처럼 표의 사실을 문서처럼 고정할 수 있다.

</details>

2. **AF 번호 검증판**: 번호를 직접 받는 저수준 API용 `af_number_valid<P, Af>()` (범위 + 그 핀에 그 번호가 있나).

<details>
<summary>풀이</summary>

`Af >= 16` 은 무조건 false, 그 다음 표에서 `(port, pin, af)` 를 찾는다. 신호 이름은 비교하지 않는다는 점이 `find_af` 와 다르다.

</details>

3. **`first_duplicate_signal(board)`**: 같은 기능을 두 핀에 배정한 첫 행.

<details>
<summary>풀이</summary>

`first_duplicate_pin` 과 같은 이중 루프에서 비교만 `signal` 로 바꾼다. 하나의 인터페이스를 여러 핀에 연결하는 합법적인 경우가 드물게 있으므로 실제 보드에서는 정책 옵션으로 둘 수 있다.

</details>

4. **PWM 분주 범위**: `(PSC+1)(ARR+1) = clk/freq`, 두 값 16비트. `pwm<Clk, Freq>()` 와 `pwm_error`.

<details>
<summary>풀이</summary>

`ticks = clk/freq`, `psc = ticks > 65536 ? (ticks - 1) / 65536 : 0`, `arr = ticks / (psc + 1) - 1`. 32비트 클럭이면 16비트 PSC 로 항상 도달 가능하므로 "주파수가 너무 낮음" 은 없다 (처음에 그 분기를 넣었다가 도달 불가능해서 지웠다). 나눗셈에 나머지가 있으면 주파수 오차가 생긴다는 것도 확인해 보자.

</details>

5. **같은 패턴, 다른 표**: DMA 요청표 `(stream, channel, 요청)`. `dma_channel<Stream, Req>()` 가 없는 조합을 막는다. 표는 STM32F4 DMA2 를 본뜬 학습용 (미확인).

<details>
<summary>풀이</summary>

행 구조체만 바꾸면 `find_af` 와 `configure` 패턴을 그대로 쓸 수 있다. 표 기반 HAL 은 대부분 이 모양이다: 핀 멀티플렉스, DMA 요청, 인터럽트 번호, 클럭 게이트 비트.

</details>

6. **`first_unmapped_signal(table)`**: 표에 한 번도 안 나오는 enum 값 찾기.

<details>
<summary>풀이</summary>

`0..SIGNAL_COUNT` 의 각 값을 표에서 찾는다. 열거자를 추가하고 표를 안 고친 실수를 잡는다. 표의 일부만 가진 `PARTIAL` 로 부정 테스트한다.

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice   # 13/13 passed
```

## 10. 핵심 정리

- AF 번호가 아니라 **기능 이름**을 받고, 번호는 `constexpr` 표에서 찾는다. 표 자체도 `static_assert(table_ok(...))` 로 검증한다.
- 판정은 **순수 함수가 enum 으로** 돌려주고(테스트 가능), `configure` 는 그 값을 이유별 `static_assert` 로 바꾼다.
- `static_assert` 뒤 본문은 `if constexpr` 로 감싼다. 안 그러면 2차 에러가 쏟아진다 (clang 2개, GCC 4개).
- C++20 메시지는 문자열 리터럴 뿐이다. 값은 `in instantiation ... requested here` 체인, 증인 템플릿, `== -1` 행 번호로 전달한다. C++26 은 P2741 로 문장 조립 가능.
- 템플릿 안의 `static_assert(false)` 는 CWG2518/P2593 이후 인스턴스화 전까지 무해하다. 맥 clang 21, g++ 14.2.1 에서 확인. 낡은 툴체인 대비로 `dependent_false_v` 관례는 여전히 안전하다.
- `requires` 는 본문을 안 본다 (단 `auto` 반환이면 본다). 함수 인자는 상수식이 아니라 `static_assert` 에 못 쓴다.
- Cortex-M4 실측: 검증된 호출은 손으로 쓴 RMW 와 명령이 **완전히 같고**, 표는 오브젝트에 남지 않는다 (런타임 탐색판은 표 20바이트 + 함수 218바이트).

## 참고자료

- cppreference: [static_assert](https://en.cppreference.com/w/cpp/language/static_assert), [if constexpr](https://en.cppreference.com/w/cpp/language/if)
- [P2741R3 user-generated static_assert messages](https://wg21.link/P2741) (C++26)
- [CWG2518](https://wg21.link/cwg2518), [P2593R1 Allowing static_assert(false)](https://wg21.link/P2593)
- C++ 표준 [dcl.pre] (static_assert-declaration), [stmt.if], [temp.res], [temp.inst], [expr.const], [temp.param] (구조체 NTTP)
- STM32F405/407 데이터시트 DS8626 (AF 표), 레퍼런스 매뉴얼 RM0090 (GPIO, USART)
- 관련 레슨: CPP-B03, CPP-B12, CPP-M01, CPP-M04, MCPP-M05
