---
id: CPP-B03
time: 50분
updated: 2026-11-07
---
# CPP-B03 enum class와 고정 기저 타입으로 레지스터 필드 명명

## 학습 목표

- 레지스터 필드(UART 모드·패리티·스톱 비트)를 `enum class X : uint32_t` 로 정의하고, 정수 ↔ enum 경계를 `to_u()` / `static_cast` **두 개의 문**으로만 넘게 만든다.
- 다른 enum 끼리 섞기, enum ↔ 정수 암시적 변환이 **컴파일 에러**가 됨을 `requires` 식·type trait 으로 증명하고, 실제 에러 메시지를 읽을 수 있다.
- 기저 타입이 **고정(fixed)** 된 enum 에서는 열거자에 없는 값(reserved 인코딩, `0xFF`)도 **정의된 값**이라는 것, 고정되지 않은 C 스타일 enum 에서는 범위 밖 `static_cast` 가 **UB** 라는 것을 구분한다.
- 플래그 enum 에 `| & ~ |= &=` 를 오버로드하고, 비트마스크가 열거자 이름이 없는 값을 만들어도 왜 괜찮은지 설명한다.
- C enum 버전과 `-O2` 어셈블리가 **명령어 하나까지 같다**(비용 0)는 것을 arm64 와 Cortex-M4 Thumb-2 에서 직접 확인한다.

## 1. 문제

레지스터 필드를 "매직 넘버 `2`" 대신 이름으로 부르는 건 펌웨어의 기본이고, 면접에서는 **"C enum 대신 왜 enum class 를 쓰나요? 비용은요?"** 가 C++ 펌웨어 질문의 단골 첫 질문이다.

C 에서는 `#define UART_PARITY_EVEN 2u` 또는 `enum { UART_PARITY_EVEN = 2 };` 를 쓴다. 둘 다 결국 `int` 라서 `cfg.parity = UART_STOP_2;` 처럼 **다른 필드의 값을 넣어도 컴파일러가 아무 말도 안 한다**. 크기도 컴파일러 옵션(`-fshort-enums`)에 따라 바뀐다. C++11 의 `enum class X : uint32_t` 는 ① 이름이 `X::` 안에 갇히고(scoped) ② 정수로 몰래 변하지 않으며 ③ 크기·부호·값 범위가 `uint32_t` 로 **고정(fixed underlying type)** 된다.

이번 문제의 mock UART 제어 레지스터(CR) 레이아웃 (STM32 `USART_CR1`/`CR2` 에서 따와 한 레지스터로 합침):

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">mock UART CR (하위 16비트) — 필드마다 enum class 하나</text>
  <g fill="currentColor" text-anchor="middle" font-size="12" opacity=".75">
    <text x="70" y="44">15</text><text x="150" y="44">13</text><text x="190" y="44">12</text>
    <text x="470" y="44">5</text><text x="510" y="44">4</text><text x="550" y="44">3</text>
    <text x="590" y="44">2</text><text x="630" y="44">1</text><text x="670" y="44">0</text>
  </g>
  <g stroke="currentColor" fill="none">
    <rect x="50" y="52" width="640" height="34"/>
    <line x1="90" y1="52" x2="90" y2="86"/><line x1="130" y1="52" x2="130" y2="86"/>
    <line x1="210" y1="52" x2="210" y2="86"/><line x1="450" y1="52" x2="450" y2="86"/>
    <line x1="530" y1="52" x2="530" y2="86"/><line x1="570" y1="52" x2="570" y2="86"/>
    <line x1="650" y1="52" x2="650" y2="86"/>
  </g>
  <rect x="130" y="52" width="80" height="34" class="f-acc"/>
  <rect x="450" y="52" width="80" height="34" class="f-ok"/>
  <rect x="530" y="52" width="40" height="34" class="f-mut"/>
  <rect x="570" y="52" width="80" height="34" class="f-warn"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="170" y="74">STOP</text><text x="330" y="74" opacity=".6">… (다른 비트, 보존)</text>
    <text x="490" y="74">PARITY</text><text x="550" y="74" font-size="12">rsv</text>
    <text x="610" y="74">MODE</text><text x="670" y="74">UE</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="122">enum class StopBits : uint32_t { One=0b00, Half=0b01, Two=0b10, OneAndHalf=0b11 };</text>
    <text x="20" y="146">enum class Parity   : uint32_t { None=0b00, Even=0b10, Odd=0b11 };  // 0b01 reserved</text>
    <text x="20" y="170">enum class UartMode : uint32_t { Async=0b00, Sync=0b01, IrDA=0b10, SmartCard=0b11 };</text>
    <text x="20" y="194">enum class UartIrq  : uint32_t { Rxne=1u&lt;&lt;0, Txe=1u&lt;&lt;1, ..., Wakeup=1u&lt;&lt;31 }; // IER 플래그</text>
  </g>
  <text x="20" y="226" fill="currentColor">PARITY = PCE(bit5, 켬) + PS(bit4, 0=even 1=odd) → 01 은 "끔 + odd" = reserved. 열거자가 없는 값.</text>
  <text x="20" y="250" fill="currentColor">encode: (to_u(field) &lt;&lt; Pos) &amp; Msk 를 OR — decode: static_cast&lt;Enum&gt;((cr &amp; Msk) &gt;&gt; Pos)</text>
  <text x="20" y="272" fill="currentColor" opacity=".75">값 예: {Sync, Even, Two} → 0x2022,  {SmartCard, Odd, OneAndHalf} → 0x3036</text>
</svg>
```

### Part A — enum class 정의와 인코딩

enum 정의는 주어진다. 다음을 구현한다.

- `to_u(e)` — 어떤 enum 이든 **기저 타입 값**을 돌려주는 `constexpr` 헬퍼. `std::to_underlying` 은 C++23 이라 C++20 빌드에는 없다 (이 맥의 libc++ 에서 실제로 확인 — 5절).
- `encode_cr(cfg)` — `UartConfig{mode, parity, stop}` 을 위 레이아웃의 32비트 값으로 합친다.

그리고 하네스의 `static_assert` 들이 왜 통과하는지 이해한다: `UartMode → uint32_t` 암시적 변환 없음, `Parity& = StopBits` 대입 불가, `UartIrq | UartMode` 불가.

### Part B — 레지스터 → enum, 범위 밖 값

- `decode_mode / decode_parity / decode_stop(cr)` — 필드를 꺼내 enum 으로 바꾼다.
- 하드웨어가 reserved 인코딩 `PARITY=0b01` 을 돌려줄 수 있다. `decode_parity` 는 그 값을 **그대로** `Parity` 에 담아야 하고(값 1), `is_valid(p)` 가 그걸 걸러낸다.
- `encode_cr` 에 `static_cast<UartMode>(7)` 같은 범위 밖 값이 들어와도 **옆 필드(bit 3)를 침범하지 않아야** 한다.

### Part C — 플래그 enum 과 mock UART 드라이버

- `UartIrq` 에 `operator| & ~` 와 `any()` 를 구현한다 (`|= &=` 는 주어짐). `Wakeup = 1u << 31` 도 문제없어야 한다.
- `UartRegs{CR, IER, SR}` mock 레지스터 블록에 대해 `uart_configure`(MODE/PARITY/STOP 만 RMW, UE·reserved·다른 비트 보존), `uart_enable_irq`, `uart_disable_irq`(`operator~` 사용), `uart_irq_enabled` 를 구현한다.

## 2. Problem (English)

> **Type-safe register fields with scoped enumerations.**
>
> A UART control register `CR` holds `MODE` at bits [2:1], `PARITY` at [5:4] (PCE|PS; `0b01` is reserved) and `STOP` at [13:12]. Bits outside these fields (UE at bit 0, a reserved bit 3, anything else) must be preserved.
>
> 1. Using `enum class UartMode/Parity/StopBits : uint32_t`, implement `template<class E> constexpr std::underlying_type_t<E> to_u(E)` (C++20, no `std::to_underlying`) and `constexpr uint32_t encode_cr(const UartConfig&)`. Out-of-range enum values must not leak into neighbouring fields.
> 2. Implement `decode_mode/decode_parity/decode_stop(uint32_t cr)` and `bool is_valid(Parity)`. A reserved encoding must round-trip unchanged and be reported invalid. Explain why `static_cast<Parity>(1)` is well-defined here but `static_cast<E>(7)` is UB for `enum E { A, B, C, D };`.
> 3. Make `UartIrq` a bitmask type (`| & ~ |= &=`, `any()`) without enabling `|` on non-flag enums, then write a small driver over a mock register block. Prove at compile time that mixing enum types or converting implicitly to/from `uint32_t` does not compile.
> 4. Show that the `-O2` code is identical to a plain C `enum` + `#define` version.
>
> Example: `encode_cr({UartMode::Sync, Parity::Even, StopBits::Two}) == 0x2022`; `to_u(~UartIrq::Rxne) == 0xFFFFFFFE`.

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 깨짐 대비
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
# 처음엔 FAIL 20줄, "4/24 passed" — TODO 를 채워 24/24 를 만든다
```

`static_assert` 블록은 starter 에서도 이미 통과한다 — 타입 안전성은 **enum class 를 선언한 순간** 생기고, 여러분이 채우는 것은 경계를 넘는 변환 코드뿐이다.

## 4. 힌트

<details>
<summary>힌트 1 — to_u 는 static_cast 한 줄</summary>

`std::underlying_type_t<E>` 가 기저 타입을 알려준다. `static_cast<std::underlying_type_t<E>>(e)` 가 끝이다. `requires std::is_enum_v<E>` 는 enum 이 아닌 타입이 들어왔을 때 에러 메시지를 깔끔하게 만든다.

</details>

<details>
<summary>힌트 2 — encode 는 C-B04 의 SET_FIELD 와 같다</summary>

`(to_u(c.mode) << CR_MODE_Pos) & CR_MODE_Msk` 를 세 필드에 대해 OR. 시프트 **후에** 마스크해야 범위 밖 값의 높은 비트가 잘린다 (C-B04 참고).

</details>

<details>
<summary>힌트 3 — decode 는 "정수 → enum" 방향의 유일한 문: static_cast</summary>

`static_cast<UartMode>((cr & CR_MODE_Msk) >> CR_MODE_Pos)`. 기저 타입이 `uint32_t` 로 고정돼 있으므로 0~3 중 어떤 값이든(이름 없는 0b01 포함) 정의된 동작이다.

</details>

<details>
<summary>힌트 4 — is_valid 는 switch + switch 밖 return</summary>

열거자 세 개를 `case` 로 나열해 `true`, **switch 다음 줄**에서 `false`. `default:` 를 쓰지 않으면 나중에 열거자를 추가했을 때 `-Wswitch` 가 알려준다.

</details>

<details>
<summary>힌트 5 — 플래그 연산자는 "내리고 → 정수 연산 → 올리고"</summary>

`static_cast<UartIrq>(to_u(a) | to_u(b))`. `~` 도 같다. enum class 에는 bool 변환을 정의할 수 없으므로 `any(f)` 는 `to_u(f) != 0`.

</details>

## 5. 풀이

### Part A — 타입의 벽과 두 개의 문

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">정수 세계와 enum 세계 사이: 문은 딱 두 개</text>
  <rect x="20" y="40" width="250" height="150" rx="10" class="f-mut" stroke="currentColor"/>
  <rect x="470" y="40" width="250" height="150" rx="10" class="f-ok" stroke="currentColor"/>
  <rect x="352" y="34" width="36" height="166" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="35" y="64" font-weight="bold">uint32_t 세계 (레지스터)</text>
    <text x="35" y="90" font-family="monospace" font-size="12">r.CR, r.IER, 0x2022</text>
    <text x="35" y="114">비트 연산, 시프트, 마스크</text>
    <text x="485" y="64" font-weight="bold">enum class 세계 (의미)</text>
    <text x="485" y="90" font-family="monospace" font-size="12">UartMode, Parity, UartIrq</text>
    <text x="485" y="114">서로 섞이지 않음, 정수 아님</text>
  </g>
  <path d="M470 140 L285 140" stroke="currentColor" fill="none" marker-end="url(#a1)"/>
  <path d="M270 172 L455 172" stroke="currentColor" fill="none" marker-end="url(#a1)"/>
  <defs><marker id="a1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="300" y="134">to_u(e)</text>
    <text x="275" y="166">static_cast&lt;E&gt;(n)</text>
  </g>
  <text x="20" y="218" fill="currentColor">암시적 변환(=, 함수 인자, 비교, |)은 벽에 막힘 → 경계를 넘는 곳이 코드에서 눈에 보인다 (grep 가능).</text>
</svg>
```

| 성질 | C `enum` / `#define` | C++ unscoped `enum E : uint32_t` | C++ `enum class E : uint32_t` |
|---|---|---|---|
| 이름 범위 | 전역 (`UART_PARITY_` 접두사로 회피) | 바깥 스코프로 샘 | `E::` 안에 갇힘 |
| 정수로 암시적 변환 | 됨 | 됨 (integral promotion) | **안 됨** — `to_u` / `static_cast` 필요 |
| 정수 → enum 암시적 | C 는 됨 | 안 됨 | 안 됨 |
| 다른 enum 끼리 비교·대입 | 됨 (C++20 에선 산술은 deprecated 경고) | 비교는 됨(deprecated) | **에러** |
| 크기 | 구현 정의 (`-fshort-enums` 면 1바이트) | `uint32_t` 고정 | `uint32_t` 고정 |
| 전방 선언 | 불가 (C23 이전) | `enum E : uint32_t;` 가능 | `enum class E : uint32_t;` 가능 |
| 범위 밖 값 | C: 아무 정수나 OK | 기저 타입 값 전부 OK | 기저 타입 값 전부 OK |

**`to_u` 를 따로 두는 이유**: `static_cast<uint32_t>(mode)` 를 여기저기 쓰면 나중에 기저 타입을 `uint8_t` 로 바꿨을 때 캐스트가 조용히 틀린 타입을 유지한다. `to_u` 는 항상 "그 enum 의 진짜 기저 타입"을 돌려준다. C++23 의 `std::to_underlying` 이 정확히 이 함수다 (P1682). 이 맥에서 확인:

```text
$ c++ -std=c++20 tu.cpp     →  error: no member named 'to_underlying' in namespace 'std'
$ c++ -std=c++23 tu.cpp     →  OK, __cpp_lib_to_underlying == 202102
```

그래서 C++20 펌웨어 코드는 헬퍼를 직접 만들고, 추가 연습 4처럼 `#if __cpp_lib_to_underlying` 으로 C++23 이면 표준 함수로 넘어가게 한다. `requires std::is_enum_v<E>` 는 필수는 아니지만, C++20 의 `std::underlying_type<int>` 는 "멤버 `type` 없음"(SFINAE 친화, P0340 — C++17 까지는 UB)이라 에러가 템플릿 깊숙이에서 나는 걸 앞으로 당겨준다.

**타입 안전성 증명** — 하네스의 검사들:

```cpp
template <class A, class B> concept BitOrable = requires(A a, B b) { a | b; };
static_assert(!std::is_convertible_v<UartMode, uint32_t>);   // enum → 정수 ✗
static_assert(!std::is_assignable_v<Parity&, StopBits>);     // cfg.parity = StopBits::Two ✗
static_assert(!BitOrable<UartIrq, UartMode>);                // 플래그 | 모드 ✗
static_assert(BitOrable<CParity, CStop>);                    // C 스타일은 통과해 버림
```

`requires` 식이 **템플릿 매개변수에 의존**해야 한다는 점에 주의. 템플릿 밖에서 `requires { UartIrq::Rxne | UartMode::Sync; }` 처럼 쓰면 "false" 가 아니라 그냥 컴파일 에러다 ([expr.prim.req]: 템플릿이 아닌 곳의 requires 식에 잘못된 식이 있으면 ill-formed). 그래서 concept 로 감싼다.

실제 에러 메시지는 `demo_mix_error.cpp` 로 본다:

```bash
c++ -std=c++20 -Wall -Wextra -c demo_mix_error.cpp               # C 스타일: 경고 1개, 컴파일 성공
c++ -std=c++20 -Wall -Wextra -c demo_mix_error.cpp -DSHOW_ERROR  # enum class: 에러 7개
```

```text
demo_mix_error.cpp:18:31: warning: bitwise operation between different enumeration types ('c_parity' and 'c_stop') is deprecated [-Wdeprecated-enum-enum-conversion]
demo_mix_error.cpp:31:19: error: cannot initialize a variable of type 'std::uint32_t' (aka 'unsigned int') with an rvalue of type 'Parity'
demo_mix_error.cpp:32:18: error: assigning to 'Parity' from incompatible type 'StopBits'
demo_mix_error.cpp:33:18: error: invalid operands to binary expression ('UartMode' and 'Parity')
demo_mix_error.cpp:34:14: error: cannot initialize a variable of type 'UartMode' with an rvalue of type 'int'
demo_mix_error.cpp:35:17: error: constant expression evaluates to -1 which cannot be narrowed to type 'UartMode' [-Wc++11-narrowing]
demo_mix_error.cpp:36:28: error: invalid operands to binary expression ('Parity' and 'Parity')
demo_mix_error.cpp:37:20: error: no member named 'to_underlying' in namespace 'std'
1 warning and 7 errors generated.
```

C 스타일 쪽은 `cfg.stop == STOP_2`, `PARITY_EVEN | STOP_2` 같은 혼용이 **경고 하나**로 끝나고 컴파일은 성공한다 (그 경고도 C++20 의 P1120 덕분). 같은 코드를 C(`cc -std=c11 -Wall -Wextra`)로 빌드해 보면 clang 은 다른 enum 대입·비교에 `-Wimplicit-enum-enum-cast` / `-Wenum-compare` 경고를 주지만 `|` 혼용은 조용하다 — 어느 쪽도 **에러는 아니다**. 참고로 `UartMode m{1u};` (direct-list-init) 는 C++17 부터 **고정 기저 타입 enum 에 한해** 허용된다 (P0138). `UartMode m = 1;`, `UartMode m = {1};` 은 여전히 에러, `UartMode m{-1};` 은 narrowing 이라 에러다.

### Part B — 범위 밖 값: 고정 기저 타입이면 "이름 없는 정상 값"

```svg
<svg viewBox="0 0 740 220" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">enum 이 가질 수 있는 값의 집합 ([dcl.enum])</text>
  <text x="10" y="58" fill="currentColor" font-family="monospace" font-size="12">enum Legacy {L0..L3}</text>
  <rect x="210" y="42" width="90" height="26" class="f-ok" stroke="currentColor"/>
  <rect x="300" y="42" width="420" height="26" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="218" y="60">0..3 정의</text>
    <text x="310" y="60">4 이상으로 static_cast = UB (C++17 CWG1766)</text>
  </g>
  <text x="10" y="112" fill="currentColor" font-family="monospace" font-size="12">enum class Parity</text>
  <text x="10" y="128" fill="currentColor" font-family="monospace" font-size="12">  : uint32_t</text>
  <rect x="210" y="98" width="510" height="26" class="f-ok" stroke="currentColor"/>
  <g stroke="currentColor"><line x1="225" y1="94" x2="225" y2="128"/><line x1="265" y1="94" x2="265" y2="128"/><line x1="305" y1="94" x2="305" y2="128"/></g>
  <g fill="currentColor" font-size="12">
    <text x="210" y="146">None</text><text x="252" y="146">Even</text><text x="294" y="146">Odd</text>
    <text x="318" y="116">0 ~ 2³²−1 모두 정의된 값 (1, 0xFF … = 이름만 없음)</text>
  </g>
  <text x="10" y="180" fill="currentColor">고정 기저 타입: 값 집합 = 기저 타입의 값 집합. 고정 아님: 열거자를 모두 담는 최소 비트폭 범위만.</text>
  <text x="10" y="204" fill="currentColor">enum class 는 ': 타입' 을 안 써도 int 로 고정 → enum class 에는 이 UB 가 (int 범위 안에서는) 없다.</text>
</svg>
```

표준 근거:

- [dcl.enum]: 기저 타입이 고정된 enum 의 값들은 **기저 타입의 값들**이다. 고정되지 않았으면 "모든 열거자를 표현하는 최소 폭 비트필드"의 값들뿐이다 (`{L0..L3}` → 2비트 → 0~3). `enum class` 는 `: T` 를 생략해도 기저 타입이 `int` 로 **고정**된다.
- [expr.static.cast]: 정수 → enum `static_cast` 는 고정 기저 타입이면 먼저 그 타입으로 정수 변환(모듈러) 후 enum 이 된다 → 항상 정의됨. 고정되지 않았고 값이 범위 밖이면 **UB** (C++17 의 CWG 1766 이전엔 unspecified 였다).

그래서 `decode_parity(0x10)` 은 그냥 `static_cast<Parity>(1)` — 이름이 없는 **유효한 값**이다. 하네스의 `to_u(bad) == 1u` 가 그걸 확인한다. 이걸 실제로 보이는 실험 (`demo_out_of_range.cpp`):

```bash
c++ -std=c++20 -Wall -Wextra -c demo_out_of_range.cpp -DSHOW_ERROR
c++ -std=c++20 -g -O1 -fsanitize=enum demo_out_of_range.cpp -o oor && ./oor
```

```text
demo_out_of_range.cpp:16:18: error: constexpr variable 'l7' must be initialized by a constant expression
demo_out_of_range.cpp:16:23: note: integer value 7 is outside the valid range of values [0, 3] for the enumeration type 'Legacy'

demo_out_of_range.cpp:40:82: runtime error: load of value 7, which is not a valid value for type 'Legacy'
fixed=7 scoped=9 legacy=7
```

`constexpr Fixed f7 = static_cast<Fixed>(7);` 과 `static_cast<Scoped>(9)` 는 아무 진단 없이 통과하고, `Legacy` 만 상수식 평가(컴파일러는 상수식 안의 UB 를 반드시 잡아야 한다)와 UBSan 둘 다에서 걸린다. 펌웨어 의미: **레지스터에서 읽은 값을 enum 에 담을 땐 기저 타입을 고정하라.** 그래야 하드웨어가 reserved 값을 돌려줘도 "UB 로 최적화가 망가지는" 일이 없고, `is_valid()` 로 정상적인 에러 처리를 할 수 있다.

**switch 의 함정**: 값이 범위 밖일 수 있다는 건 "모든 열거자를 case 로 다룬 switch" 도 빠져나올 수 있다는 뜻이다. `demo_out_of_range.cpp` 의 `name_bad()` 는 switch 뒤에 `return` 이 없는데, Apple clang 21 은 `-Wall -Wextra` 에서도 **경고하지 않는다** (실행해서 확인). 범위 밖 값이 들어오면 값 없이 함수 끝에 도달 = UB. GCC 는 이런 경우 `-Wreturn-type`("control reaches end of non-void function")을 내는 것으로 알려져 있다(여기선 실행 못 함). 그래서 `is_valid` 처럼 **switch 밖에 fallback return** 을 둔다. `default:` 를 쓰지 않는 이유는 `-Wswitch`(‑Wall 포함)가 "새 열거자를 case 에서 빠뜨림" 을 잡아주게 두기 위해서다.

**encode 쪽 방어**: `(to_u(mode) << 1) & CR_MODE_Msk` — `mode = 7` 이면 `7 << 1 = 0b1110` 이고 마스크 후 `0b0110`. 마스크가 없으면 bit 3(reserved)을 1로 써버린다. 값 범위가 타입으로 보장되지 않으니(고정 기저 타입의 대가), 비트 필드 경계는 여전히 마스크가 지킨다.

### Part C — 플래그 enum: 비트마스크 연산자

`UartIrq` 는 "한 값 = 비트 하나" 가 아니라 **비트들의 집합**이다. 표준 라이브러리도 같은 패턴을 쓴다 (`std::filesystem::perms` 는 비트마스크 연산자가 정의된 `enum class`, [bitmask.types]).

| 연산자 | 구현 | 결과가 열거자가 아닐 수 있나 |
|---|---|---|
| `a \| b` | `static_cast<UartIrq>(to_u(a) \| to_u(b))` | 예 (`0x3`) — 고정 기저 타입이라 정의됨 |
| `a & b` | `static_cast<UartIrq>(to_u(a) & to_u(b))` | 예 (`0`, 또는 `None`) |
| `~a` | `static_cast<UartIrq>(~to_u(a))` | 예 (`0xFFFFFFFE`) — Part B 덕분에 합법 |
| `a \|= b` | `return a = a \| b;` | — |
| `any(a)` | `to_u(a) != 0` | `if (flags & Rxne)` 는 컴파일 에러라서 필요 |

- `operator~` 가 범위 밖 값을 만드는 게 바로 **고정 기저 타입이 필수인 이유**다. 고정 안 된 `enum { RXNE=1, TXE=2 }` 에 `~RXNE` 결과를 다시 넣으면 범위 [0,3] 밖 → UB.
- `UartIrq::Wakeup = 1u << 31`: C(C23 이전) 에서는 열거 상수가 `int` 로 표현 가능해야 해서 `#define` 으로 도망가야 한다 (C11 6.7.2.2). `enum class X { W = 1u << 31 }` 처럼 **기저 타입을 생략**해도 기본 `int` 라 이렇게 에러가 난다:

```text
error: enumerator value evaluates to 2147483648, which cannot be narrowed to type 'int' [-Wc++11-narrowing]
```

- `uart_configure` 는 **읽기 1번, 쓰기 1번**: `keep = r.CR & ~(MODE|PAR|STOP 마스크)` → `r.CR = keep | encode_cr(c)`. 필드마다 따로 RMW 하면 volatile 쓰기가 3번 생기고 중간 상태(모드는 바뀌고 패리티는 아직 옛값)가 하드웨어에 보인다. 실제 STM32 는 `PCE/PS/M` 같은 필드를 **UE=0 일 때만** 바꿀 수 있으니, 진짜 드라이버는 UE 를 끄고 → 설정 → 다시 켠다 (mock 은 UE 를 보존만 한다). volatile 접근 모델링은 CPP-B05 참고.
- `uart_irq_enabled` 는 레지스터 원시값을 `static_cast<UartIrq>(r.IER)` 로 **한 번** enum 세계로 올린 뒤 `&`. 경계를 넘는 지점이 한 줄로 보인다.

### 비용 0 확인 — C 버전과 어셈블리 비교

`uart_c.c` (C enum + `#define`) 와 `uart_cpp.cpp` (enum class + `to_u` + `operator~`) 에 같은 함수 4개(`encode_cr`, `uart_configure`, `uart_disable_irq`, `parity_is_valid`)를 두고, C++ 쪽은 `extern "C"` 로 심볼 이름을 맞췄다 (CPP-B02 참고). `asm_compare.sh` 가 두 `.s` 에서 지시어·주석을 지우고 diff 한다:

```bash
sh asm_compare.sh   # ① 호스트 arm64 (Mach-O)  ② clang --target=thumbv7em-none-eabi -mcpu=cortex-m4
```

```text
== [arm64] C (enum + #define) ==
_encode_cr:
	ubfiz	w0, w0, #1, #2
	bfi	w0, w1, #4, #2
	bfi	w0, w2, #12, #2
	ret
_uart_configure:
	ldr	w8, [x0]
	mov	w9, #-12343
	and	w8, w8, w9
	ubfiz	w9, w1, #1, #2
	bfi	w9, w2, #4, #2
	bfi	w9, w3, #12, #2
	orr	w8, w9, w8
	str	w8, [x0]
	ret
_uart_disable_irq:
	ldr	w8, [x0]
	bic	w8, w8, w1
	str	w8, [x0]
	ret
_parity_is_valid:
	cmp	w0, #4
	cset	w8, lo
	and	x9, x0, #0xf
	mov	w10, #13
	lsr	w9, w10, w9
	and	w0, w8, w9
	ret
== [arm64] diff C vs C++ (enum class) ==
IDENTICAL: 28 lines
...
== [cm4] C (enum + #define) ==
encode_cr:
	movs	r3, #6
	and.w	r0, r3, r0, lsl #1
	bfi	r0, r1, #4, #2
	and	r1, r2, #3
	orr.w	r0, r0, r1, lsl #12
	bx	lr
...
== [cm4] diff C vs C++ (enum class) ==
IDENTICAL: 35 lines
```

읽는 법:

- `ubfiz w0, w0, #1, #2` = "하위 2비트를 꺼내 1칸 왼쪽에 넣기" = `(m << 1) & 0x6` 한 명령. `bfi` = 비트필드 삽입 = 마스크+시프트+OR 한 명령. `to_u` 와 `static_cast` 는 **명령어를 하나도 만들지 않는다** — 타입은 컴파일 타임에만 존재한다.
- `mov w9, #-12343` = `~0x3036` = 세 필드를 지우는 마스크 (상수 접기).
- `parity_is_valid` 는 switch 가 **비트 집합 검사**로 바뀌었다: `13 = 0b1101` 의 비트 0·2·3 = None·Even·Odd. `(13 >> p) & 1` 이고 `p < 4` 조건이 범위 밖 값을 걸러낸다.
- Cortex-M4 결과는 Apple clang 의 ARM 백엔드로 **실제로** `thumbv7em-none-eabi` Thumb-2 코드를 만든 것이다 (`bfi`, `and.w ... lsl #1`, `bic.w`). arm-none-eabi-g++ (GCC) 는 레지스터 배정·명령 선택이 조금 다를 수 있지만, enum class 가 추가 명령을 만들지 않는다는 결론은 같을 것으로 예상한다 (GCC 로는 실행 못 함).
- 단, `-O0` 에서는 `to_u` 가 진짜 함수 호출(`bl __Z4to_uI8UartModeEu17__underlying_typeIT_ES1_`)로 남는다 (실행 확인: `to_u`·`operator~` 호출 6번). 디버그 빌드 크기/속도가 문제면 `-Og` 를 쓴다.

**크기 고정의 진짜 이득 (-fshort-enums)**: 같은 `sizeof` 를 두 플래그로 찍어본 결과 (`thumbv7em-none-eabi` clang):

| 타입 | 기본 | `-fshort-enums` |
|---|---|---|
| `enum CParity {0,2,3}` | 4 | **1** |
| `enum class Parity : unsigned` | 4 | 4 |

arm-none-eabi-**gcc** 는 bare-metal EABI 에서 short enum 이 기본인 것으로 알려져 있다 (그래서 섞어 링크하면 "uses variable-size enums yet the output is to use 32-bit enums" 링커 경고가 난다 — 여기서 직접 재현은 못 함). 구조체 안에 C enum 을 넣어 레지스터·프로토콜 레이아웃을 맞추면 툴체인에 따라 크기가 바뀐다. `: uint32_t` / `: uint8_t` 를 명시하면 그 문제가 사라진다.

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution   # 24/24 passed
c++ -std=c++20 -g -O1 -fsanitize=address,undefined,enum solution.cpp -o sol_san && ./sol_san              # 24/24, 진단 없음
```

UBSan 의 `-fsanitize=enum` 이 조용한 것 자체가 Part B 의 증명이다: `static_cast<UartMode>(7)`, `static_cast<Parity>(1)`, `~UartIrq::Rxne` 모두 고정 기저 타입이라 "유효하지 않은 enum 값 로드" 가 아니다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `enum class Irq { Wakeup = 1u << 31 };` | 기저 타입 생략 = `int` → 2³¹ 은 narrowing 에러 | `enum class Irq : uint32_t { ... }` |
| `return static_cast<uint32_t>(m) << 1;` (마스크 없음) | 범위 밖 값(7)이 옆 reserved 비트를 덮어씀 | `(to_u(m) << Pos) & Msk` |
| switch 로 모든 열거자 처리 후 `return` 없음 | 범위 밖 값이면 함수 끝 도달 = UB, clang 은 경고도 없음 | switch 밖에 `return false;` / `"reserved"` |
| 레지스터 값을 C 스타일 `enum E {A,B,C,D}` 로 캐스트 | 하드웨어가 reserved 값을 주면 범위 밖 static_cast = UB | 기저 타입 고정 (`: uint32_t`) |
| `if (flags & UartIrq::Rxne)` | enum class 는 bool 로 문맥 변환 불가 (`not contextually convertible to 'bool'`) | `if (any(flags & UartIrq::Rxne))` |
| `operator\|` 가 `uint32_t` 를 반환 | 결과가 정수 세계로 떨어져 타입 안전성 상실, `Rxne \| 5` 같은 혼용 허용 | `UartIrq` 반환, 정수 변환은 레지스터 쓰기 직전 `to_u` 한 번 |
| 모든 enum 에 템플릿 `operator\|` 를 무조건 정의 | `UartMode::Sync \| UartMode::IrDA` 같은 의미 없는 식까지 허용 | opt-in trait (추가 연습 2) |
| `UartMode m = {2};` | copy-list-init 은 정수 → enum 불가 (`cannot initialize ... with an rvalue of type 'int'`) | `UartMode m{2u};` (C++17) 또는 `static_cast` |
| `std::to_underlying` 를 C++20 빌드에서 사용 | C++23 라이브러리 함수 — 이 libc++ 은 `-std=c++23` 에서만 제공 | `to_u` 헬퍼 + `__cpp_lib_to_underlying` 검사 |

## 8. 면접 Q&A

<details>
<summary>Q1. enum 과 enum class 의 차이를 세 가지 말해보세요.</summary>

① **스코프**: 열거자가 `Parity::Even` 처럼 타입 안에 갇힌다 (C 는 `UART_PARITY_EVEN` 접두사로 충돌 회피). ② **암시적 변환 없음**: 정수로도, 다른 enum 으로도 몰래 변하지 않는다 — 혼용이 컴파일 에러. ③ **기저 타입이 항상 고정**: 생략하면 `int`, 지정하면 그 타입 → 크기 예측 가능, 전방 선언(`enum class Parity : uint32_t;`) 가능, 범위 밖 값도 정의됨. 꼬리질문 "unscoped enum 에도 `: uint32_t` 를 붙일 수 있나?" → 예 (C++11). 크기·전방 선언·범위는 해결되지만 암시적 변환과 스코프 누수는 그대로다.

</details>

<details>
<summary>Q2. enum class 쓰면 런타임 비용이 있나요? 어떻게 확인했나요?</summary>

`-O2` 에서는 없다. 같은 함수를 C enum + `#define` 버전과 enum class 버전으로 만들어 `-S` 로 비교하면 arm64 와 Cortex-M4 Thumb-2 둘 다 명령어가 **완전히 동일**했다 (`ubfiz`/`bfi`/`bic`). enum 은 컴파일 타임 타입 정보일 뿐이고 `static_cast` 는 같은 비트 패턴을 재해석하는 것이라 명령어가 없다. 꼬리: "디버그 빌드는?" → `-O0` 에선 `to_u` 같은 헬퍼가 실제 `bl` 호출로 남는다. `-Og` 나 always_inline 으로 완화.

</details>

<details>
<summary>Q3. 레지스터에서 읽은 값을 enum 으로 캐스트했는데 그 값에 해당하는 열거자가 없으면 어떻게 되나요?</summary>

기저 타입이 고정이면(모든 enum class, 그리고 `: T` 를 붙인 unscoped enum) **정의된 동작**이다 — enum 의 값 집합이 기저 타입의 값 전체이기 때문 ([dcl.enum]). 고정되지 않은 `enum E {A,B,C,D}` 는 값 집합이 0~3 이라 7 을 `static_cast` 하면 **UB** (C++17, CWG 1766). clang 은 상수식에서 `integer value 7 is outside the valid range of values [0, 3]` 로 거부하고, UBSan `-fsanitize=enum` 은 런타임에 잡는다. 실무 결론: 하드웨어 값을 담는 enum 은 반드시 기저 타입을 고정하고, `is_valid()` 로 검증한다.

</details>

<details>
<summary>Q4. enum class 로 플래그(비트마스크)를 표현하려면 어떻게 하나요? 결과가 열거자가 아닌데 괜찮나요?</summary>

`operator| & ^ ~` 와 `|= &=` 를 constexpr 로 오버로드: `static_cast<E>(to_u(a) | to_u(b))`. 결과(`0x3`, `~Rxne = 0xFFFFFFFE`)는 열거자가 아니지만 기저 타입이 고정이라 유효한 값이다. 모든 플래그 enum 에 복붙하지 않으려면 `enable_bitmask<E>` trait 을 특수화한 enum 에만 템플릿 연산자를 켠다 (추가 연습 2). 꼬리: "`if (f & Rxne)` 는?" → enum class 는 bool 로 변환 안 되고 enum 에는 멤버 함수(`explicit operator bool`)를 둘 수 없으니 `any()` 같은 자유 함수를 쓴다.

</details>

<details>
<summary>Q5. 왜 `: uint32_t` 를 굳이 명시하나요? enum class 는 어차피 int 아닌가요?</summary>

① 레지스터는 unsigned 32비트다 — `1u << 31` 같은 열거자를 `int` 에 넣으면 narrowing 에러. ② 구조체/프로토콜 레이아웃: `: uint8_t` 로 1바이트를 보장 (C enum 은 `-fshort-enums` 여부로 1 또는 4바이트가 되고, arm-none-eabi-gcc 는 short enum 이 기본으로 알려져 있다). ③ `~` 같은 비트 연산이 부호 있는 타입에서 일어나지 않게. ④ 헤더에서 `enum class UartMode : uint32_t;` 로 전방 선언해 의존성 줄이기.

</details>

<details>
<summary>Q6. 모든 열거자를 case 로 다룬 switch 에 default 를 넣어야 하나요?</summary>

`default:` 를 넣으면 나중에 열거자를 추가했을 때 `-Wswitch` 경고(“enumeration value not handled”)를 잃는다. 그렇다고 아무것도 안 두면 범위 밖 값(고정 기저 타입이라 합법)이 들어왔을 때 switch 를 빠져나가 함수 끝에 도달 — 값 반환 함수면 UB 이고, clang 은 이걸 경고하지 않는다. 그래서 **default 없이 switch 를 쓰고, switch 다음에 fallback return/에러 처리**를 둔다. 반대로 "default 가 꼭 있어야 한다" 는 MISRA 같은 규칙을 따르는 팀이면 `-Wswitch-enum` 으로 누락을 잡는다.

</details>

<details>
<summary>Q7. C HAL 함수에 enum class 를 넘겨도 되나요?</summary>

C 헤더는 enum class 를 모르므로 경계에서 `to_u(mode)` 로 `uint32_t` 를 넘기는 게 정석이다. `extern "C" void f(UartMode)` 처럼 enum class 를 C 링크 함수 인자로 쓰는 것도 컴파일되고, 주요 ABI(AAPCS, AArch64) 에서 enum 은 기저 타입처럼 전달되므로 C 쪽 `uint32_t` 선언과 맞는다 — 이 레슨의 asm 비교가 그렇게 했다. 다만 C 쪽이 기저 타입이 다른 `enum` 으로 선언하면(`-fshort-enums` 등) 어긋날 수 있으니 C 쪽은 고정 폭 정수로 선언한다 (CPP-B02 참고).

</details>

## 9. 추가 연습

### 연습 1 — 기저 타입으로 크기 고르기, namespace 안 enum 의 한계

`DataBits/Parity/StopBits : uint8_t` 로 3바이트짜리 `WireFormat` 구조체(EEPROM·프로토콜에 그대로 저장)를 만들고 `sizeof == 3` 을 `static_assert` 하라. 또 C++03 식 "namespace 로 감싼 unscoped enum" 이 여전히 정수로 새는 것을 보여라.

<details>
<summary>답 보기</summary>

`enum class DataBits : uint8_t {...}` → `sizeof == 1`, 세 개를 담은 구조체는 정렬 1, 크기 3. `namespace legacy { enum Mode : uint32_t {...}; }` 는 이름 충돌은 막지만 `uint32_t x = legacy::Sync;` 가 컴파일된다 → `static_assert(std::is_convertible_v<legacy::Mode, uint32_t>)`. 스코프와 변환 차단은 별개의 기능이고 enum class 만 둘 다 준다.

</details>

### 연습 2 — opt-in 비트마스크 연산자

플래그 enum 마다 연산자 5개를 복붙하지 말고, `enable_bitmask<E>` 를 `true_type` 으로 특수화한 enum 에만 템플릿 `operator| & ~ |=` 가 켜지게 하라. `DmaFlag` 는 되고 `Mode` 는 안 됨을 concept 로 증명하라.

<details>
<summary>답 보기</summary>

`template <class E> concept Bitmask = std::is_enum_v<E> && enable_bitmask<E>::value;` 후 `template <Bitmask E> constexpr E operator|(E a, E b)`. 제약을 만족하지 않는 타입에는 후보가 사라지므로 `BitOrable<Mode, Mode>` 는 false, `int | int` 는 내장 연산자 그대로. `uint8_t` 기저 타입의 `~` 는 `int` 로 승격되어 음수가 되지만 `static_cast<Tiny>` 가 고정 기저 타입으로 모듈러 변환해 `0xFE` 가 된다 (정의됨).

</details>

### 연습 3 — reserved 값 거르기: optional + using enum

`std::optional<Parity> parity_from_bits(uint32_t)` — reserved 면 `nullopt`. `parity_name(Parity)` 는 `using enum Parity;` (C++20, P1099) 로 case 를 짧게 쓰고 범위 밖이면 `"reserved"`.

<details>
<summary>답 보기</summary>

`static_cast<Parity>(bits & 0x3u)` 후 switch 로 이름 있는 값만 `return p;`, switch 밖에서 `return std::nullopt;`. `std::optional` 은 힙을 쓰지 않고 `-fno-exceptions` 에서도 `has_value()` / `*` / `==` 는 문제없다 (`value()` 는 예외를 던지는 API 라 피한다). `using enum Parity;` 는 그 스코프에 열거자 이름을 들여와 `case None:` 처럼 쓰게 해준다.

</details>

### 연습 4 — std::to_underlying 이 있으면 쓰고, 없으면 헬퍼

`to_u` 를 `#if defined(__cpp_lib_to_underlying)` 으로 분기해 C++23 에선 `std::to_underlying` 을 호출하게 하라. `-std=c++20` 과 `-std=c++23` 둘 다로 빌드해 출력 차이를 확인하라.

<details>
<summary>답 보기</summary>

`<utility>` 를 include 한 뒤 기능 테스트 매크로 `__cpp_lib_to_underlying >= 202102L` 로 분기. 이 맥에서 실제 출력: `-std=c++20` → `std::to_underlying available: no (__cplusplus=202002)`, `-std=c++23` → `yes (__cplusplus=202302)`, 둘 다 `14/14 passed`. `__cplusplus` 값으로 분기하는 것보다 기능 매크로가 정확하다 (표준 모드는 23인데 라이브러리가 아직 구현 안 한 경우를 구분).

</details>

### 연습 5 — enum 을 인덱스로: UART 프레임 길이

`WireFormat` 의 1 프레임 길이를 **반 비트 단위**로 계산하라 (start 1 + data + parity(PCE 면 1) + stop 0.5/1/1.5/2). 8N1 → 20, 9E2 → 26, 7O1.5 → 21.

<details>
<summary>답 보기</summary>

`stop_half[4] = {2, 1, 4, 3}` 를 `to_u(stop) & 0x3u` 로 인덱싱 — 마스크로 배열 범위를 보장한다 (범위 밖 enum 값이 와도 out-of-bounds 없음). 패리티 비트 존재 여부는 PCE 비트 `to_u(parity) & 0b10`. `2 * (1 + data + parity) + stop_half`. 보드레이트로 나누면 바이트당 시간 → DMA 타임아웃 계산에 쓴다.

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice   # 14/14 passed
c++ -std=c++23 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice23 && ./practice23 # std::to_underlying 경로
```

## 10. 핵심 정리

- 레지스터 필드 = `enum class Field : uint32_t { ... }`. 스코프 + 암시적 변환 차단 + 크기 고정을 한 번에 얻는다.
- 경계는 두 개의 문: enum → 정수 `to_u(e)` (C++23 `std::to_underlying`), 정수 → enum `static_cast<E>(n)`. 나머지 혼용은 전부 컴파일 에러.
- 기저 타입이 고정이면 열거자 밖 값도 **정의된 값** (reserved 인코딩, `~flag`). 고정 안 된 C 스타일 enum 의 범위 밖 `static_cast` 는 UB.
- 그래서: encode 는 시프트 후 **마스크**, decode 후 `is_valid()`, switch 는 default 없이 + switch 밖 fallback.
- 플래그 enum 은 `| & ~ |= &=` + `any()` 를 오버로드 (가능하면 opt-in trait). `if (f & X)` 는 안 된다.
- 비용 0: C enum 버전과 `-O2` 어셈블리가 arm64·Cortex-M4 모두 동일. `-O0` 에선 헬퍼 호출이 남는다.
- `-fshort-enums`(arm-none-eabi-gcc 기본으로 알려짐)에 흔들리지 않으려면 기저 타입을 명시.

## 참고자료

- [cppreference — Enumeration declaration (scoped enum, fixed underlying type, 값 범위)](https://en.cppreference.com/w/cpp/language/enum)
- [cppreference — static_cast: 정수 → enum 변환 규칙](https://en.cppreference.com/w/cpp/language/static_cast)
- [cppreference — std::to_underlying (C++23)](https://en.cppreference.com/w/cpp/utility/to_underlying), [std::underlying_type](https://en.cppreference.com/w/cpp/types/underlying_type)
- [CWG 1766 — Values outside the range of the values of an enumeration](https://cplusplus.github.io/CWG/issues/1766.html)
- [P0138R2 — Construction Rules for enum class Values (C++17)](https://wg21.link/p0138r2), [P1099R5 — using enum (C++20)](https://wg21.link/p1099r5), [P1682R3 — std::to_underlying](https://wg21.link/p1682r3), [P1120R0 — enum 산술 변환 deprecate](https://wg21.link/p1120r0)
- [N3030 — C23 Enhanced Enumerations (C 에도 고정 기저 타입)](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3030.htm)
- [Clang UBSan — -fsanitize=enum](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html)
- [STM32F4 Reference Manual RM0090 — USART_CR1/CR2 (PCE, PS, STOP 필드)](https://www.st.com/resource/en/reference_manual/rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- C++ 표준 [dcl.enum], [expr.static.cast], [expr.prim.req], [bitmask.types]
