---
id: CPP-M03
time: 75분
updated: 2026-11-08
---
# CPP-M03 enum class 플래그 연산자: Flags<E>

## 학습 목표

- `enum class` 의 타입 안전을 그대로 두고 `|`, `&`, `^`, `~`, `test()` 를 쓸 수 있는 **`Flags<E>` 값 타입**을 구현한다.
- **opt-in**: 특수화한 enum 에만 연산자가 켜지게 만들고, 다른 enum 과 섞으면 컴파일 에러가 남을 `requires` 식과 실제 컴파일러 메시지로 증명한다.
- `~` 를 **valid 마스크로 자르는 이유**와, 그 때문에 레지스터 RMW 에서 생기는 함정(`reg & (~f).raw()` 가 다른 필드를 지운다)을 설명하고 고친다.
- 연산자 이름 찾기의 세 조각 — **hidden friend, ADL, 템플릿 인자 추론** — 이 왜 `Flags` 설계를 결정하는지 말할 수 있다.
- raw `uint32_t` 와 생성 코드를 arm64, thumbv7em(Cortex-M4), arm-none-eabi-g++ 로 비교하고, **어디가 같고 어디가 왜 다른지** 측정값으로 말한다.

## 1. 문제

플래그 레지스터(인터럽트 enable, 상태 비트)는 펌웨어 코드의 절반쯤을 차지하는데, `uint32_t` 로 다루면 "DMA 플래그를 UART 레지스터에 쓰는" 실수를 아무도 막아 주지 않는다. 면접에서는 "enum class 로 비트 플래그를 어떻게 안전하게 만들겠느냐"로 시작해 `~` 의 의미, 이름 찾기, 제로 코스트 여부까지 파고든다.

CPP-B03 에서는 enum 하나를 단일 값 필드로 다루고, `enable_bitmask<E>` 로 `E | E → E` 까지 해 봤다. 거기엔 두 가지 한계가 있다. `E` 는 "플래그 하나"인데 `A | B` 의 결과도 `E` 타입이라서 **집합과 원소가 구별되지 않고**, `~A` 는 `0xFFFFFFFE` 처럼 **정의되지 않은 비트까지 켠다**. 이 레슨은 집합 전용 타입 `Flags<E>` 로 둘 다 고친다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">원소(E)와 집합(Flags&lt;E&gt;)을 타입으로 나눈다</text>
  <rect x="20" y="44" width="190" height="60" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="115" y="68" text-anchor="middle" fill="currentColor" font-family="monospace" font-size="13">hal::IrqFlag</text>
  <text x="115" y="88" text-anchor="middle" fill="currentColor" font-size="12">플래그 하나 (단일 비트)</text>
  <rect x="470" y="44" width="250" height="60" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="595" y="68" text-anchor="middle" fill="currentColor" font-family="monospace" font-size="13">fw::Flags&lt;IrqFlag&gt;</text>
  <text x="595" y="88" text-anchor="middle" fill="currentColor" font-size="12">플래그 집합 (항상 valid 의 부분집합)</text>
  <path d="M212 74 L466 74" stroke="currentColor" fill="none"/>
  <path d="M466 74 l-8 -5 v10 z" fill="currentColor"/>
  <text x="340" y="66" text-anchor="middle" fill="currentColor" font-size="12">암시적 변환 / E | E</text>
  <rect x="20" y="136" width="190" height="52" rx="8" class="f-warn" stroke="currentColor"/>
  <text x="115" y="158" text-anchor="middle" fill="currentColor" font-family="monospace" font-size="13">hal::DmaFlag</text>
  <text x="115" y="176" text-anchor="middle" fill="currentColor" font-size="12">다른 레지스터의 플래그</text>
  <path d="M212 160 L466 92" stroke="currentColor" stroke-dasharray="5 4" fill="none"/>
  <text x="340" y="148" text-anchor="middle" fill="currentColor" font-size="12">변환 없음 → 컴파일 에러</text>
  <rect x="20" y="204" width="190" height="52" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="115" y="226" text-anchor="middle" fill="currentColor" font-family="monospace" font-size="13">std::uint32_t</text>
  <text x="115" y="244" text-anchor="middle" fill="currentColor" font-size="12">레지스터 원시값</text>
  <rect x="470" y="204" width="250" height="52" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="595" y="226" text-anchor="middle" fill="currentColor" font-family="monospace" font-size="12">Flags::from_raw(x)  /  f.raw()</text>
  <text x="595" y="244" text-anchor="middle" fill="currentColor" font-size="12">이름 있는 변환만 허용</text>
  <path d="M212 230 L466 230" stroke="currentColor" fill="none"/>
  <path d="M466 230 l-8 -5 v10 z" fill="currentColor"/>
  <text x="340" y="222" text-anchor="middle" fill="currentColor" font-size="12">reserved 비트는 여기서 잘린다</text>
  <text x="10" y="284" fill="currentColor">bool 로의 변환도 explicit: if (f &amp; Rxne) 는 되고, f + 1 은 안 된다.</text>
</svg>
```

실제 칩 레지스터 대신 `volatile uint32_t` 세 개짜리 구조체를 mock 으로 쓴다. reserved 비트에 쓰레기가 읽히는 상황, 다른 필드가 같은 레지스터에 있는 상황을 테스트가 직접 만든다.

### Part A — Flags<E> 기본 연산

- 값 하나(`value_type bits_`)를 들고 있는 `template <FlagEnum E> class Flags` 를 완성한다. 생성은 `Flags{}`(빈 집합), `Flags f = Irq::Rxne`(원소 → 집합, 암시적).
- `|`, `&`, `^` 와 `|=`, `&=`, `^=`, `==`, `explicit operator bool`, `test(mask)`(전부 켜짐), `any_of(mask)`(하나라도).
- 인자 조합을 빠짐없이 지원한다: `E|E`, `E|Flags`, `Flags|E`, `Flags|Flags`. 8/16/32/64비트 기저 타입, bit 31 / bit 63 경계까지.

### Part B — 타입 규칙, ~ 의 의미, RMW 함정

- `FlagEnum` concept 을 채운다: `enum class` 만, 기저 타입은 `unsigned`(단 `bool` 제외), 그리고 opt-in(`flag_traits<E>` 특수화)된 것만.
- `mask_of(...)` consteval 함수: 열거자 목록을 받아 valid 마스크를 만들되, 값이 0이거나 2비트 이상이거나 중복이면 **컴파일 에러**.
- `~` 는 valid 안에서의 여집합이다. `from_raw` 와 `E` 생성자도 valid 로 잘라 불변식을 유지한다.
- 레지스터 RMW 용 `set_in(reg)` / `clear_in(reg)`, 집합 차 `without(mask)`, 켜진 플래그 순회 `for_each(fn)`.

### Part C — 가짜 UART 인터럽트 드라이버

- `uart_irq_enable/disable`: `IER` 의 다른 필드(FIFO 임계값 `[23:16]`)를 보존한다.
- `uart_pending()`: `SR & IER` 를 `Flags` 로. reserved 쓰레기는 들어오지 못한다.
- `uart_isr()`: 에러 플래그 묶음은 한 번만 세고 나머지는 낮은 비트부터 처리, 처리한 것만 W1C `ICR` 에 **한 번의 store** 로 ack.

## 2. Problem (English)

> **Type-safe flag set over `enum class`.**
>
> Implement `template <FlagEnum E> class Flags` with `|`, `&`, `^`, `~`, compound assignment, `==`, `explicit operator bool`, and `test(mask)` (all bits set). Enums opt in by specializing `flag_traits<E>` with a `valid` mask; no operator may apply to an enum that did not opt in.
>
> Requirements:
> 1. `A | B` on two enumerators yields a `Flags<E>`. Every mix of `E` and `Flags<E>` works for `|` and `&`; **mixing two different flag enums, or a flag set with an integer, must not compile** (prove it with `requires`-expressions).
> 2. `~f` is the complement inside `valid`. `Flags::all()` and `~Flags::none()` must compare equal, and `~f` must never set a reserved bit.
> 3. Provide a helper to clear flags in a raw register **without destroying bits outside `valid`**. Explain why `reg & (~f).raw()` is wrong.
> 4. Reject at compile time: unscoped enums, signed or missing underlying type, and a mask list with a zero, multi-bit, or duplicate enumerator.
> 5. Show the generated code against hand-written `uint32_t` code on Cortex-M4 and explain every difference.
>
> Example: for `valid = 0x8000000F`, `(Rxne | Txe).raw() == 0x3`, `(~Rxne).raw() == 0x8000000E`, `Flags::from_raw(0xFFFFFFFF) == Flags::all()`, `IrqSet{Rxne}.clear_in(0x00AB0003) == 0x00AB0002`, and `Rxne | DmaFlag::Tc` is a compile error.
>
> Follow-ups: why are the binary operators hidden friends? Why does `E | E` need a namespace-scope template, and how does it get found? Is `Flags` ISR-safe?

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 깨짐 대비
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
# 처음엔 "39/128 passed" — TODO 를 채워 128/128 을 만든다
```

타입 수준 증명(`static_assert`)은 선언만 보고 판정되므로 starter 에서도 이미 컴파일된다. 연산자 **시그니처는 주어져 있고** 몸통이 비어 있다 — 왜 저런 시그니처인지는 §5 Part A 에서 다룬다.

## 4. 힌트

<details>
<summary>힌트 1 — 불변식 하나를 정하고 시작하라</summary>

"`bits_` 는 항상 `valid` 의 부분집합"을 불변식으로 잡는다. 그러면 바깥에서 비트가 들어오는 입구(`from_raw`, `Flags(E)`)와 비트를 새로 만드는 연산(`~`)만 잘라 주면 되고, `|`, `&`, `^` 는 부분집합끼리의 연산이라 **다시 자를 필요가 없다**.

</details>

<details>
<summary>힌트 2 — 8비트 기저 타입과 정수 승격</summary>

`uint8_t a; ~a` 의 타입은 `int` 이고 값은 음수다 (C-B02 참고). `static_cast<value_type>` 로 돌려놓거나 valid 와 AND 한 뒤 돌려놓는다. 어느 쪽이든 unsigned 로의 변환은 모듈러라서 정의되어 있다.

</details>

<details>
<summary>힌트 3 — mask_of 에서 컴파일 에러를 내는 법</summary>

`-fno-exceptions` 빌드라 `throw` 는 쓸 수 없다. 대신 consteval 함수의 잘못된 경로에서 **constexpr 이 아닌 함수를 호출**하면 그 호출이 평가되는 순간 상수식이 깨진다. 함수 이름을 에러 메시지처럼 지어 두면 clang 이 그 이름을 그대로 출력한다. 호출되는 경로가 없으면 그 함수는 정의가 없어도 링크 에러가 나지 않는다.

</details>

<details>
<summary>힌트 4 — clear_in 에 ~(*this) 를 쓰면 왜 안 되나</summary>

`(~f).raw()` 에는 valid 밖의 비트가 전부 0 이다. 레지스터에 AND 하면 valid 밖의 비트가 전부 지워진다. 필요한 것은 `reg & ~bits_` 처럼 **`f` 자신의 비트만 뒤집은 값**이다.

</details>

<details>
<summary>힌트 5 — 가장 낮은 1비트</summary>

`x & -x` (unsigned 니까 `0 - x` 로 쓰면 모듈러 연산으로 정의됨) 가 가장 낮은 1비트만 남기고, `x & (x - 1)` 이 그 비트를 지운다 (C-B02 참고). `std::countr_zero` + 시프트로 써도 값은 같다 — §5 의 측정 참고.

</details>

<details>
<summary>힌트 6 — uart_isr 의 순서</summary>

`pending` 을 한 번만 읽어서 지역 변수에 둔다. 에러 묶음을 먼저 처리해 `rest` 에서 빼고, 나머지는 `for_each`. 마지막에 처리한 전체 집합을 `ICR` 에 한 번 store 한다.

</details>

## 5. 풀이

### Part A — 값 타입과 연산자 배치

불변식과 표현은 단순하다.

| 멤버 | 하는 일 | 불변식과의 관계 |
|---|---|---|
| `bits_` | `value_type` 하나 | 항상 `valid` 의 부분집합 |
| `Flags(E)` | 원소 → 집합 | `clip` 으로 자름 (입구) |
| `from_raw(x)` | 원시값 → 집합 | `clip` 으로 자름 (입구) |
| `operator~` | valid 안의 여집합 | `from_raw` 로 자름 (새 비트를 만드므로) |
| `\| & ^` | 집합 연산 | `make` — 부분집합끼리라 자를 필요 없음 |

`make` 를 따로 둔 것은 성능 때문이다. 처음 버전은 `|`, `&`, `^` 도 `from_raw` 를 거치게 썼고, 그 결과 Cortex-M4 에서 `flags_or` 가 `raw_or` 의 2배 크기였다 (아래 측정). 불변식이 보장하는 일을 다시 하지 않는 것이 "제로 코스트"의 절반이다.

**연산자 배치.** 인자 조합별로 어떤 연산자가 필요한지가 설계의 핵심이다.

| 식 | 쓰이는 것 | 이유 |
|---|---|---|
| `Rxne \| Txe` (E, E) | 네임스페이스 템플릿 `operator\|(E, E)` | `Flags` 인자가 없어서 hidden friend 가 ADL 로 안 보인다 |
| `f \| Rxne`, `Rxne \| f` | hidden friend | 비템플릿 함수라서 `E → Flags` 암시적 변환이 일어난다 |
| `f \| g` | hidden friend | |
| `~Rxne` | 템플릿 `operator~(E)` | 위와 같음 |
| `~f` | hidden friend | |
| `Rxne & Txe` (E, E) | **없음(의도)** | 서로 다른 단일 비트의 `&` 는 항상 0 — 있으면 버그 |

**hidden friend** 는 클래스 안에서 정의한 friend 함수다. 일반 이름 찾기에는 보이지 않고 인자에 그 클래스가 있을 때 ADL 로만 찾아지며, **템플릿이 아닌 함수**이므로 인자에 암시적 변환이 적용된다. 반면 `template <typename E> Flags<E> operator|(Flags<E>, Flags<E>)` 처럼 자유 템플릿으로 쓰면 `f | Irq::Txe` 는 컴파일되지 않는다. 템플릿 인자 추론은 암시적 변환을 고려하지 않기 때문이다 ([temp.deduct.call]). `demo_errors.cpp` 의 E9 가 그 실물이다:

```text
demo_errors.cpp:78:16: error: invalid operands to binary expression ('TFlags<IrqFlag>' and 'hal::IrqFlag')
   78 |     auto e = t | IrqFlag::Txe;
demo_errors.cpp:37:21: note: candidate template ignored: could not match 'TFlags<E>' against 'hal::IrqFlag'
```

**`E | E` 는 어떻게 찾아지나.** `fw::operator|` 템플릿은 `fw` 안에 있는데, `hal::IrqFlag` 의 ADL 은 `hal` 만 뒤진다. 그래서 `hal` 안에 `using fw::operator|;` 를 둔다. ADL 은 using-**directive** 는 무시하지만 using-**declaration** 은 그 네임스페이스의 선언으로 찾는다 ([basic.lookup.argdep]). 이게 빠지면 opt-in 은 했어도 연산자를 못 찾는다 (E6):

```text
demo_errors.cpp:71:30: error: invalid operands to binary expression ('other::Led' and 'other::Led')
   71 |     auto e = other::Led::Red | other::Led::Green;
```

후보 목록에 `fw::operator|` 가 아예 없는 것이 단서다 (자유 템플릿 `TFlags` 후보만 나온다). 연습 1 에서 이 한 줄을 매크로로 숨긴다. Qt 의 `Q_DECLARE_OPERATORS_FOR_FLAGS` 도 같은 문제를 푸는 도구다.

`test()` 의 의미를 정해 둬야 한다. 여기서 `test` 는 **all-of** 이고 `if (f & m)` / `any_of` 는 **any-of** 다.

| `have` | `mask` | `test` (all-of) | `any_of` |
|---|---|---|---|
| Rxne\|Txe | Rxne | true | true |
| Rxne | Rxne\|Txe | **false** | **true** |
| Rxne | 빈 집합 | true (공허) | false |

빈 마스크의 `test` 가 true 인 것은 수학적으로 맞지만(모든 원소가 포함됨, vacuous truth) 호출부가 기대한 뜻이 아닐 수 있어서, `mask_of` 가 값 0인 열거자를 막아 `None = 0` 같은 함정을 아예 없앴다.

### Part B — 타입 규칙

```cpp
template <typename T>
concept FlagEnum = std::is_enum_v<T>
    && (!std::is_convertible_v<T, std::underlying_type_t<T>>)   // enum class 만
    && std::is_unsigned_v<std::underlying_type_t<T>>
    && (!std::is_same_v<std::underlying_type_t<T>, bool>)
    && requires { { flag_traits<T>::valid } -> std::convertible_to<std::underlying_type_t<T>>; };
```

- `std::is_enum_v` 가 첫 조건이므로 `FlagEnum<int>` 에서는 뒤의 `underlying_type_t<int>` 가 평가되지 않는다 (제약의 `&&` 는 단락 평가된다, [temp.constr.op]). 
- 스코프 판별은 C++23 `std::is_scoped_enum` 의 C++20 대용이다: enum class 만 기저 타입으로 암시적 변환이 안 된다.
- `enum class NoWidth { X = 1 };` 처럼 기저 타입을 안 쓰면 **int** 다. 플래그에 signed 는 곤란하므로 거부한다 — 실제 에러는 어떤 조건이 false 인지 짚어 준다 (E8):

```text
demo_errors.cpp:75:9: error: constraints not satisfied for class template 'Flags' [with E = NoWidth]
./flags_final.hpp:43:11: note: because 'NoWidth' does not satisfy 'FlagEnum'
./flags_final.hpp:39:8: note: because 'std::is_unsigned_v<std::underlying_type_t<NoWidth>>' evaluated to false
```

**opt-in 은 왜 필요한가.** 전역 `template <typename E> operator|(E, E)` 를 enum 전부에 켜면 `Mode::On | Mode::Sleep` 같은, 비트 플래그가 아닌 enum 의 연산이 컴파일되어 버린다. `flag_traits` 를 특수화한 enum 만 통과시키면 그 위험이 없다. 다른 enum 과 섞은 경우의 실제 메시지 (E1, E3):

```text
demo_errors.cpp:58:28: error: invalid operands to binary expression ('hal::IrqFlag' and 'hal::DmaFlag')
./flags_final.hpp:110:42: note: candidate template ignored: deduced conflicting types for parameter 'E' ('hal::IrqFlag' vs. 'hal::DmaFlag')

demo_errors.cpp:63:23: error: invalid operands to binary expression ('hal::Mode' and 'hal::Mode')
./flags_final.hpp:110:42: note: candidate template ignored: constraints not satisfied [with E = hal::Mode]
./flags_final.hpp:41:21: note: because 'flag_traits<T>::valid' would be invalid: implicit instantiation of undefined template 'fw::flag_traits<hal::Mode>'
```

**특수화는 enum 바로 뒤에.** concept 만족 여부는 캐시된다. 특수화 전에 한 번 질의했다가 뒤에 특수화하면 결과가 달라져야 하는데, 이는 ill-formed NDR 이다 ([temp.constr.atomic]/3). Apple clang 21 로 직접 해 보면 **캐시된 false 가 그대로 나온다**:

```text
demo_errors.cpp:93:15: error: static assertion failed
   93 | static_assert(fw::FlagEnum<Late>);
demo_errors.cpp:93:15: note: because 'Late' does not satisfy 'FlagEnum'
./flags_final.hpp:41:21: note: because 'flag_traits<T>::valid' would be invalid: implicit instantiation of undefined template 'fw::flag_traits<Late>'
```

(E11. 93번 줄 바로 앞 줄에 `flag_traits<Late>` 특수화가 있는데도 "undefined template" 이라고 나온다.)

**mask_of.** consteval 안에서 `std::has_single_bit` 로 검사하고, 어기면 정의 없는 함수를 호출한다. 에러 메시지에 함수 이름이 나온다 (E7):

```text
demo_errors.cpp:73:20: error: constexpr variable 'e' must be initialized by a constant expression
./flags_final.hpp:28:38: note: non-constexpr function 'enumerator_is_zero_or_not_single_bit' cannot be used in a constant expression
demo_errors.cpp:73:24: note: in call to 'mask_of<Two>(3)'
```

`valid` 를 손으로 `0x8000000F` 라고 쓰지 않고 **열거자 목록으로 계산**하면 열거자를 추가했는데 마스크 갱신을 잊는 실수가 "목록에 안 넣음" 하나로 줄어든다. 하네스는 `MaskOk<Tiny::A, Tiny::A>` 같은 concept 으로 거부를 런타임 없이 확인한다 (템플릿 인자의 상수식이 깨지면 치환 실패).

**explicit operator bool.** explicit 이 빠지면 무슨 일이 생기나. 하네스의 `static_assert(CanOr<BadFlags<IrqFlag>, BadFlags<hal::DmaFlag>>)` 가 "컴파일된다"를 증명한다. 서로 다른 타입이 둘 다 bool → int 로 변환되어 연산되기 때문이다. `demo_errors.cpp` 의 실제 출력:

```text
BadFlags<Irq>{4} == BadFlags<Dma>{1} -> 1
BadFlags<Irq>{4} +  BadFlags<Dma>{1} -> 2
```

`4 == 1` 인데 true 다. 컴파일러가 잡아 주는 범위도 확인했다: Apple clang 21 `-Wall` 은 `a | b`, `a & b` 만 `-Wbitwise-instead-of-logical` 로 경고하고 (E10), `^`, `<`, `==`, `+` 는 경고하지 않는다. arm-none-eabi-g++ 14.2.1 은 `-Wall -Wextra` 로도 이 여섯 연산 전부 경고가 없었다. explicit 은 `if (f)`, `!f`, `f && g`, `f ? :` 같은 **문맥 변환(contextual conversion, [conv]/4)** 에서만 동작하므로 필요한 곳은 다 된다.

### Part B — ~ 는 왜 자르는가, 그리고 그 대가

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="20" fill="currentColor" font-weight="bold" font-size="13">valid = bit 31, 3..0 / IER[23:16] 는 FIFO 임계값(enum 밖의 필드)</text>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="288" y="44">31</text><text x="350" y="44">30:24</text><text x="420" y="44">23:16</text><text x="490" y="44">15:4</text>
    <text x="548" y="44">3</text><text x="598" y="44">2</text><text x="648" y="44">1</text><text x="698" y="44">0</text>
  </g>
  <g stroke="currentColor">
    <rect x="262" y="52" width="52" height="30" class="f-mut"/><rect x="314" y="52" width="72" height="30" class="f-mut"/>
    <rect x="386" y="52" width="68" height="30" class="f-acc"/><rect x="454" y="52" width="72" height="30" class="f-mut"/>
    <rect x="526" y="52" width="50" height="30" class="f-mut"/><rect x="576" y="52" width="50" height="30" class="f-mut"/>
    <rect x="626" y="52" width="50" height="30" class="f-ok"/><rect x="676" y="52" width="50" height="30" class="f-ok"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="288" y="72">0</text><text x="350" y="72">0</text><text x="420" y="72">AB</text><text x="490" y="72">0</text>
    <text x="548" y="72">0</text><text x="598" y="72">0</text><text x="648" y="72">1</text><text x="698" y="72">1</text>
  </g>
  <text x="10" y="72" fill="currentColor" font-family="monospace">reg (IER 현재값)</text>
  <g stroke="currentColor">
    <rect x="262" y="92" width="52" height="30" class="f-mut"/><rect x="314" y="92" width="72" height="30" class="f-mut"/>
    <rect x="386" y="92" width="68" height="30" class="f-mut"/><rect x="454" y="92" width="72" height="30" class="f-mut"/>
    <rect x="526" y="92" width="50" height="30" class="f-mut"/><rect x="576" y="92" width="50" height="30" class="f-mut"/>
    <rect x="626" y="92" width="50" height="30" class="f-mut"/><rect x="676" y="92" width="50" height="30" class="f-ok"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="288" y="112">0</text><text x="350" y="112">0</text><text x="420" y="112">0</text><text x="490" y="112">0</text>
    <text x="548" y="112">0</text><text x="598" y="112">0</text><text x="648" y="112">0</text><text x="698" y="112">1</text>
  </g>
  <text x="10" y="112" fill="currentColor" font-family="monospace">f = Rxne</text>
  <g stroke="currentColor">
    <rect x="262" y="132" width="52" height="30" class="f-ok"/><rect x="314" y="132" width="72" height="30" class="f-warn"/>
    <rect x="386" y="132" width="68" height="30" class="f-warn"/><rect x="454" y="132" width="72" height="30" class="f-warn"/>
    <rect x="526" y="132" width="50" height="30" class="f-ok"/><rect x="576" y="132" width="50" height="30" class="f-ok"/>
    <rect x="626" y="132" width="50" height="30" class="f-ok"/><rect x="676" y="132" width="50" height="30" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="288" y="152">1</text><text x="350" y="152">7F</text><text x="420" y="152">FF</text><text x="490" y="152">FFF</text>
    <text x="548" y="152">1</text><text x="598" y="152">1</text><text x="648" y="152">1</text><text x="698" y="152">0</text>
  </g>
  <text x="10" y="152" fill="currentColor" font-family="monospace">~f (자르지 않으면)</text>
  <g stroke="currentColor">
    <rect x="262" y="172" width="52" height="30" class="f-ok"/><rect x="314" y="172" width="72" height="30" class="f-mut"/>
    <rect x="386" y="172" width="68" height="30" class="f-mut"/><rect x="454" y="172" width="72" height="30" class="f-mut"/>
    <rect x="526" y="172" width="50" height="30" class="f-ok"/><rect x="576" y="172" width="50" height="30" class="f-ok"/>
    <rect x="626" y="172" width="50" height="30" class="f-ok"/><rect x="676" y="172" width="50" height="30" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="288" y="192">1</text><text x="350" y="192">0</text><text x="420" y="192">0</text><text x="490" y="192">0</text>
    <text x="548" y="192">1</text><text x="598" y="192">1</text><text x="648" y="192">1</text><text x="698" y="192">0</text>
  </g>
  <text x="10" y="192" fill="currentColor" font-family="monospace">~f (valid 로 자름)</text>
  <g stroke="currentColor">
    <rect x="262" y="212" width="52" height="30" class="f-mut"/><rect x="314" y="212" width="72" height="30" class="f-mut"/>
    <rect x="386" y="212" width="68" height="30" class="f-warn"/><rect x="454" y="212" width="72" height="30" class="f-mut"/>
    <rect x="526" y="212" width="50" height="30" class="f-mut"/><rect x="576" y="212" width="50" height="30" class="f-mut"/>
    <rect x="626" y="212" width="50" height="30" class="f-ok"/><rect x="676" y="212" width="50" height="30" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="288" y="232">0</text><text x="350" y="232">0</text><text x="420" y="232">00 !</text><text x="490" y="232">0</text>
    <text x="548" y="232">0</text><text x="598" y="232">0</text><text x="648" y="232">1</text><text x="698" y="232">0</text>
  </g>
  <text x="10" y="232" fill="currentColor" font-family="monospace">reg &amp; (~f).raw()  틀림</text>
  <g stroke="currentColor">
    <rect x="262" y="252" width="52" height="30" class="f-mut"/><rect x="314" y="252" width="72" height="30" class="f-mut"/>
    <rect x="386" y="252" width="68" height="30" class="f-ok"/><rect x="454" y="252" width="72" height="30" class="f-mut"/>
    <rect x="526" y="252" width="50" height="30" class="f-mut"/><rect x="576" y="252" width="50" height="30" class="f-mut"/>
    <rect x="626" y="252" width="50" height="30" class="f-ok"/><rect x="676" y="252" width="50" height="30" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="288" y="272">0</text><text x="350" y="272">0</text><text x="420" y="272">AB</text><text x="490" y="272">0</text>
    <text x="548" y="272">0</text><text x="598" y="272">0</text><text x="648" y="272">1</text><text x="698" y="272">0</text>
  </g>
  <text x="10" y="272" fill="currentColor" font-family="monospace">f.clear_in(reg)   맞음</text>
  <text x="10" y="312" fill="currentColor" font-size="13">집합 안에서는 잘린 ~ 가 맞고, 레지스터 원시값에는 f 자신의 비트만 뒤집어야 한다.</text>
</svg>
```

자르지 않은 `~` 의 문제는 추상적이지 않다. `~all()` 이 reserved 비트를 켠 값이 되면 `!(~all())` 이 false 이고, `~f` 를 레지스터에 쓰면 reserved 비트에 1을 쓰게 된다. 잘라 두면 Boolean 대수가 닫힌다: `~~a == a`, `a | ~a == all()`, `a & ~a == none()`, 드 모르간. 하네스는 xorshift32 고정 시드로 2000쌍을 돌려 이 법칙들과 `uint32_t` 기준 모델(`ma | mb`, `~ma & VALID` ...)을 비교한다.

대가는 위 그림의 다섯째 줄이다. 값 공간에서 올바른 `~` 가 레지스터 공간에서는 틀린다. 레지스터 RMW 는 "`f` 의 비트만 내린다"이지 "`valid` 안에서 여집합을 AND 한다"가 아니기 때문이다. 그래서 RMW 는 전용 함수로 분리했다:

```cpp
constexpr value_type clear_in(value_type reg) const { return static_cast<value_type>(reg & ~bits_); }
```

### 제로 코스트 측정 — 무엇이 같고 무엇이 다른가

`demo_asm.cpp` 에 같은 일을 하는 `raw_xxx` / `flags_xxx` 쌍을 넣고 세 컴파일러로 컴파일했다 (`bash asm_compare.sh`). 함수 몸통의 명령을 비교한 결과다 (`-O2 -fno-exceptions -fno-rtti`, 표의 숫자는 `ret`/`bx lr` 포함 명령 수).

| 비교 쌍 | 내용 | arm64 clang | Cortex-M4 clang | Cortex-M4 GCC 14.2 |
|---|---|---|---|---|
| `raw_or` / `flags_or` | `a \| Rxne \| Txe` | 동일 (2) | 동일 (2) | 동일 (2) |
| `raw_test1` / `flags_test1` | 비트 1개 검사 | 2 vs 2 (x/w 레지스터 이름만 다름) | 동일 (2) | 동일 (2) |
| `raw_test2` / `flags_test2` | all-of | 4 vs 4 (다른 명령 조합) | 동일 (5) | 동일 (5) |
| `raw_any` / `flags_any` | any-of | 3 vs 3 (x/w 만 다름) | 동일 (4) | 동일 (4) |
| `raw_set` / `flags_set` | RMW set | 동일 (4) | 동일 (4) | 동일 (4) |
| `raw_clear` / `flags_clear` | RMW clear | 동일 (4) | 동일 (4) | 동일 (4) |
| `raw_each` / `flags_each` | 비트 순회 + 호출 | 14 vs 14 (cbz 위치만 다름) | 동일 (13) | 동일 (11) |
| `raw_del` / `flags_del` | `a & ~Rxne` | **2 vs 3** | **2 vs 4** | **2 vs 3** |
| `raw_del` / `flags_del2` | `a.without(Rxne)` | 동일 (2) | 동일 (2) | 동일 (2) |
| `raw_not_v` / `flags_not` | `~a & VALID` | 동일 (3) | 동일 (4) | 동일 (3) |
| `raw_not` / `flags_not` | raw `~a` (자르지 않음) | **2 vs 3** | **2 vs 4** | **2 vs 3** |
| `raw_pending_v` / `flags_pending` | `SR & IER & VALID` | 동일 (5) | 동일 (5) | 동일 (6) |
| `raw_pending` / `flags_pending` | raw `SR & IER` (자르지 않음) | **4 vs 5** | **4 vs 5** | **4 vs 6** |
| `raw_from_enum` / `flags_from_enum` | 런타임 `E` → 집합 | **1 vs 2** | **1 vs 2** | **1 vs 3** |
| `raw8_test` / `flags8_test` | 8비트 기저 타입 검사 | 2 vs 2 | **2 vs 3** (uxtb) | 2 vs 2 |

결론을 정직하게 쓰면 이렇다. **"Flags 는 항상 같은 코드"는 거짓이고, "자르는 일을 한다면 같은 코드"가 참이다.** 같은 의미를 raw 로 쓴 것(`raw_not_v`, `raw_pending_v`)과는 전부 동일하다. 다른 줄은 raw 쪽이 자르는 일을 안 하는 것이다.

Cortex-M4 실제 출력 (clang, `flags_del` 과 그 대안):

```text
raw_del(unsigned int):                 flags_del(fw::Flags<Irq>):         flags_del2(fw::Flags<Irq>):
    bic r0, r0, #1                         movs r1, #14                       bic r0, r0, #1
    bx  lr                                 movt r1, #32768                    bx  lr
                                           ands r0, r1
                                           bx  lr
```

`a & ~Irq::Rxne` 는 `~Rxne` 가 먼저 valid 로 잘려 `0x8000000E` 상수가 되고, 이 상수는 Thumb-2 의 수정 즉시값(modified immediate)이 아니라 `movs/movt` 두 개로 만든다. GCC 는 `ldr r3, .L12` (리터럴 풀) 로 만든다. 결과값은 같지만 코드가 다르므로, 빈번한 경로라면 `without` 을 쓴다. Part C 의 `uart_isr` 도 그렇게 했다.

```text
Cortex-M4 clang, llvm-nm --print-size 값을 쌍별로 모은 요약 (바이트)
raw_del 6   flags_del 10   flags_del2 6
raw_not 4   flags_not 12   raw_not_v 12
raw_pending 8   flags_pending 12   raw_pending_v 12
raw_or 6    flags_or 6     raw_each 32   flags_each 32
raw_from_enum 2   flags_from_enum 6
```

몇 가지 더. (1) `flags_or` 가 `raw_or` 와 같아진 것은 `|` 에서 재-clip 을 뺀 뒤다. 처음 코드는 Cortex-M4 에서 12바이트(raw 는 6)였다. (2) `for_each` 는 처음에 `std::countr_zero` + 시프트로 썼는데, GCC 14.2 가 `rbit/clz/lsl` 로 번역해 `raw_each` 보다 명령이 늘었다. `x & -x` 로 바꾸자 GCC 도 raw 와 같아졌다 (clang 은 양쪽 모두 같은 코드). (3) `flags8_test` 의 `uxtb` 는 호출 규약 때문이다. AAPCS32 Stage B 의 B.2 는 word 보다 작은 정수 기본 타입을 "zero- or sign-extended to a full word" 하라고 정하지만, 구조체(composite)에는 그런 확장 규칙이 없고 크기만 4의 배수로 올린다 (B.4). 그래서 clang 은 `Flags<u8>` 인자의 위쪽 비트를 믿지 않고 `uxtb` 로 정리한다. 반환 쪽은 문서가 명시한다: 4바이트 이하 composite 는 r0 로 반환하고 "Any bits in r0 that lie outside the bounds of the result have unspecified values". `flags8_or` 의 `orn r0, r0, #126` 이 위쪽 비트를 1로 채워도 되는 이유다. `Flags<u8>` 를 함수 경계에 많이 쓰는 코드라면 이 차이가 쌓일 수 있고, 호출이 인라인되면 사라진다. (4) `flags_from_enum` 의 mask 는 실행 시간에 `E` 값이 넘어올 때만 남는다. 상수 enum 인자는 위 표의 다른 줄들처럼 전부 접힌다.

### Part C — 드라이버

```text
uart_isr()
 ├─ pending = from_raw(SR) & from_raw(IER)     ← SR 읽기 1회, IER 읽기 1회, reserved 쓰레기 제거
 ├─ if (pending & kErrors): errors++ / rest = rest.without(kErrors)
 ├─ rest.for_each(...)                        ← 낮은 비트부터 rx / tx / wake
 └─ if (pending) ICR = pending.raw()          ← W1C: 처리한 비트만, store 1회
```

세 가지를 짚는다.

- **`ICR` 은 RMW 하지 않는다.** W1C 레지스터에 `ICR |= f` 를 쓰면 읽어 온 다른 비트까지 1로 써서 지우게 된다 (RMW 의 W1C 위험은 CPP-M02 참고). 여기서는 `pending.raw()` 를 바로 store 한다.
- **volatile 은 드라이버 구조체에 그대로 둔다.** C++20 은 volatile 대상의 복합 대입(`reg |= x`)을 deprecated 로 표시했고, `reg = reg | x` 처럼 명시하면 "읽기 1회 + 쓰기 1회"가 코드에 드러난다 (P2327 등에 따라 C++23 에서 일부 되돌려졌다 — 정확한 범위는 미확인).
- **enable 비트와 상태 비트가 같은 자리**라는 것은 mock 의 가정이다. 실제 칩은 다른 경우가 많다. 그때는 두 enum 으로 나누면 "상태 플래그를 enable 레지스터에 쓰는" 실수가 컴파일 에러가 된다 (연습 4).

테스트는 `isr_rows` 표 8줄로 입력(SR, IER)과 기대값(handled, ICR, 카운터, 남은 SR)을 비교한다. 꺼진 플래그가 SR 에 남아 있는 경우, bit 31 + reserved 쓰레기, reserved 만 있는 경우가 경계값이다.

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

## 7. 자주 하는 실수

**1. 연산자를 전부 자유 템플릿으로 쓴다.**
`template <FlagEnum E> Flags<E> operator|(Flags<E>, Flags<E>)` → `f | Irq::Rxne` 가 컴파일되지 않는다. 추론은 변환을 안 본다. 고친 코드: 클래스 안의 hidden friend.

**2. `E | E` 를 빠뜨리거나 네임스페이스를 안 맞춘다.**
hidden friend 만 쓰면 `Irq::Rxne | Irq::Txe` 는 후보가 없다. 템플릿을 `fw` 에 두고 enum 쪽 네임스페이스에 `using fw::operator|;`.

**3. 레지스터에 `~` 를 그대로 AND.**
```cpp
reg = reg & (~f).raw();   // 틀림: valid 밖 비트(다른 필드, reserved)까지 지운다
reg = f.clear_in(reg);    // 맞음
```

**4. `operator bool` 을 explicit 없이 쓴다.**
서로 다른 플래그 타입이 bool → int 로 섞인다. `4 == 1` 이 true 가 된다. `explicit` 을 붙이면 `if (f)` 는 그대로 된다.

**5. 기저 타입을 안 쓴다.**
`enum class Irq { Top = 1u << 31 };` 기저 타입 미지정은 `int` 라서 `1u << 31` 이 narrowing 에러다. `: std::uint32_t` 를 쓴다.

**6. `None = 0` 열거자를 플래그 enum 에 넣는다.**
`f.test(None)` 은 항상 true(공허 참)이고 `mask_of` 에서 컴파일 에러다. 빈 집합은 `Flags{}` / `Flags::none()` 으로 표현한다.

**7. 특수화를 첫 사용 뒤에 한다.**
concept 평가가 캐시되어 ill-formed NDR. enum 선언 바로 뒤에 특수화한다.

**8. `f |= x` 가 원자적이라고 믿는다.**
값 타입의 연산일 뿐이다. ISR 과 공유하면 lost update 가 난다 (연습 5, CPP-A04 참고).

## 8. 면접 Q&A

<details>
<summary>Q1. `enum class` 를 쓰는 것과 `uint32_t` + `#define` 의 차이가 뭔가요? 비용은요?</summary>

`uint32_t` 는 모든 플래그가 같은 타입이라 `DMA_TC` 를 UART 레지스터에 쓰는 실수를 못 막는다. `Flags<IrqFlag>` 는 그걸 컴파일 에러로 바꾼다. 비용: Cortex-M4 clang/GCC 양쪽에서 OR, 검사, RMW, 순회는 raw 와 명령이 같았다 (측정 표). 자르는 연산(`~`, `from_raw`, 런타임 `E`)만 raw 의 자르지 않는 코드보다 명령이 더 많은데, 그건 raw 가 그 일을 안 하기 때문이다.

꼬리: "왜 raw 쪽에 `& VALID` 가 없죠?" → 대부분 실제 C 코드는 그걸 안 한다. 그래서 reserved 비트가 집합에 섞여 들어온다. 같은 일을 시키면 같은 코드다.

</details>

<details>
<summary>Q2. ~ 를 valid 로 자르는 이유와, 자르면 생기는 문제는요?</summary>

이유: `~all() == none()`, `~~a == a`, `a | ~a == all()` 등 Boolean 대수가 닫히고, reserved 비트가 켜지지 않는다. 문제: 값 공간의 여집합이므로 레지스터 RMW 에 쓰면 다른 필드까지 지운다. 그래서 RMW 는 `clear_in(reg)` (`reg & ~bits_`) 로 분리한다. 대안은 `~` 를 자르지 않는 것인데, 그러면 모든 검사가 "valid 안의 비트만 본다"를 스스로 해야 해서 불변식이 사라진다.

</details>

<details>
<summary>Q3. 연산자를 왜 hidden friend 로 했나요? 템플릿으로 쓰면 안 되나요?</summary>

템플릿 인자 추론은 암시적 변환을 고려하지 않아서 `f | Irq::Rxne` 에서 `E` 를 정할 수 없다 ([temp.deduct.call]). hidden friend 는 `Flags<E>` 가 인스턴스화될 때 만들어지는 **비템플릿 함수**라서 `E → Flags<E>` 변환이 된다. 부수 효과로 이 연산자는 인자에 `Flags` 가 있을 때만 후보가 되므로 일반 이름 찾기를 오염시키지 않고, 오버로드 후보가 적어 컴파일 시간과 에러 메시지에도 유리하다.

꼬리: "`E | E` 는요?" → 인자에 `Flags` 가 없어서 ADL 로 hidden friend 를 못 찾는다. 그래서 네임스페이스 템플릿을 따로 두고, `FlagEnum` 제약으로 opt-in 한 enum 만 통과시킨다.

</details>

<details>
<summary>Q4. 왜 `using fw::operator|` 가 필요한가요? 전역에 operator| 템플릿을 두면 안 되나요?</summary>

ADL 은 인자 타입의 네임스페이스만 뒤진다. `hal::IrqFlag` 면 `hal` 이고, `fw` 는 안 뒤진다. using-declaration 을 `hal` 에 두면 ADL 이 찾는다. 전역에 템플릿을 둬도 다른 네임스페이스 안의 코드에서는 일반 이름 찾기가 그 네임스페이스의 `operator|` 에서 멈춰 전역을 못 볼 수 있다(name hiding). 제약 없는 전역 템플릿은 모든 enum 에 켜지는 문제도 있다. 이 문제를 매크로로 숨기는 게 연습 1(`FW_FLAGS`)이고, Qt 의 `Q_DECLARE_OPERATORS_FOR_FLAGS` 도 같은 목적이다.

</details>

<details>
<summary>Q5. opt-in 은 왜 필요하고, 방법은 어떤 것들이 있나요?</summary>

전역으로 켜면 `Mode::On | Mode::Sleep` 같은 비플래그 enum 까지 연산이 된다. 방법: (a) trait 특수화 `flag_traits<E>` — 명시적이지만 enum 의 네임스페이스를 닫고 특수화해야 한다, (b) ADL 훅 `flag_valid_mask(E)` — enum 옆에 함수 하나로 끝난다 (연습 1), (c) 매크로로 묶기. CPP-B03 의 `enable_bitmask<E>` 는 (a) 의 가장 단순한 형태다. 함정은 특수화가 **첫 사용 전에** 보여야 한다는 것 — concept 평가가 캐시되며, 어기면 ill-formed NDR 이다. Apple clang 21 에서 실제로 캐시된 false 가 나왔다.

</details>

<details>
<summary>Q6. `operator bool` 이 왜 explicit 이어야 하죠? if 는 되나요?</summary>

된다. explicit 변환은 `if`, `while`, `!`, `&&`, `||`, 삼항 조건 같은 **문맥 변환(contextual conversion)** 에서 허용된다 ([conv]/4). explicit 이 없으면 서로 다른 플래그 타입이 bool 을 거쳐 `==`, `+` 까지 컴파일된다. 측정: Apple clang 21 은 `|`, `&` 만 경고하고, arm-none-eabi-g++ 14.2.1 은 `-Wall -Wextra` 로 아무 경고도 안 냈다. 경고에 기대면 안 된다.

꼬리: "bool 대신 safe-bool 관용구는요?" → C++11 이전의 우회책이고 explicit 이 그걸 대체했다.

</details>

<details>
<summary>Q7. Flags 는 ISR 에서 안전한가요?</summary>

아니다. `f |= x` 는 읽기-수정-쓰기라서 메인과 ISR 이 같은 변수를 건드리면 lost update 가 난다. 공유 변수는 `std::atomic` 기반 `AtomicFlags`(연습 5)처럼 `fetch_or`/`exchange` 로 만들거나, 임계 구역으로 보호한다. Cortex-M0(LDREX 없음)에서의 atomic 비용은 CPP-A04 참고. 하드웨어 쪽에서 비슷한 문제를 푸는 레지스터가 W1S/W1C(Nordic `INTENSET`/`INTENCLR` 류)다.

꼬리: "그럼 Flags 를 volatile 로?" → volatile 은 원자성을 주지 않는다 (CPP-B05 참고).

</details>

<details>
<summary>Q8. 8비트 플래그 타입은 정말 uint8_t 와 같나요?</summary>

`sizeof` 는 1로 같지만(`static_assert`) ABI 는 다를 수 있다. AAPCS32 는 `uint8_t` 인자를 word 로 확장하라고 정하지만(B.2) 구조체 인자에는 그런 규칙이 없다. 그래서 Cortex-M4 clang 에서 `flags8_test` 는 `uxtb` 가 하나 더 붙었다 (GCC 14.2 는 `ubfx` 하나로 처리해 명령 수가 같았다). 호출이 인라인되면 사라진다.

꼬리: "Flags 를 NTTP 로 쓸 수 있나요?" → 지금은 `bits_` 가 private 이라 안 된다. clang 메시지 (E12): `'IrqSet' (aka 'Flags<hal::IrqFlag>') is not a structural type because it has a non-static data member that is not public` ([temp.param]). 쓰고 싶으면 멤버를 public 으로 열거나 raw 값을 NTTP 로 받는다.

</details>

## 9. 추가 연습

### 연습 1 — ADL 훅 + 한 줄 opt-in 매크로

trait 특수화는 enum 의 네임스페이스를 닫고 다시 열어야 한다. enum 옆에 쓰는 한 줄로 opt-in 을 끝내라: `FW_FLAGS(Led, Led::Red, Led::Green);`

<details>
<summary>답</summary>

concept 을 `requires(T t) { { flag_valid_mask(t) } -> ...; }` 로 바꾸고, `valid = flag_valid_mask(E{})`. 매크로는 `consteval` 함수 하나 + `using ::fw::operator|; using ::fw::operator~;`. using-declaration 은 같은 네임스페이스에 두 번 있어도 된다 (클래스 안에서만 금지). `practice.cpp` 의 `FW_FLAGS` 참고.

</details>

### 연습 2 — range-for

`for (Led l : leds)` 가 낮은 비트부터 돌게 하라. `constexpr` 함수 안에서도 동작해야 한다.

<details>
<summary>답</summary>

`Flags` 를 고치지 않고 `begin(Flags<E>)`/`end(Flags<E>)` 자유 함수를 `fw` 에 둔다. 범위 for 는 멤버 `begin` 이 없으면 ADL 로 찾는다 ([stmt.ranged]). 이터레이터는 남은 비트(`rest_`)를 들고, `operator*` 는 `rest_ & -rest_`, `++` 는 `rest_ & (rest_ - 1)`. end 는 0.

</details>

### 연습 3 — 경계 있는 포맷터

`format_flags(f, names, buf, cap)` 이 `"RED|BLUE"` 를 쓰게 하라. `snprintf` 처럼 필요한 길이를 돌려주고, 항상 NUL 종단하고, `cap == 0` 이면 버퍼를 건드리지 않는다.

<details>
<summary>답</summary>

`put` 람다가 글자마다 `len` 을 늘리되 `len + 1 < cap` 일 때만 쓴다 (마지막 칸은 NUL 자리). 끝에 `cap != 0` 이면 `buf[min(len, cap-1)] = '\0'`. 주의: `format_flags(Led::Red, ...)` 는 **컴파일되지 않는다** — 함수 템플릿이라 추론이 변환을 안 본다. 본문과 같은 이유이고, `LedSet{Led::Red}` 처럼 명시한다.

</details>

### 연습 4 — 상태 비트 → enable 비트 번역

UART 상태(`Rxne = bit5`, `Txe = bit7`, `Ore = bit3`, `Idle = bit4`)와 enable(`RxneIe = bit0`, ...)의 비트 자리가 다를 때, `constexpr` 표 하나로 `Flags<Status>` → `Flags<IntEn>` 을 번역하라. 두 집합은 서로 비교조차 되면 안 된다.

<details>
<summary>답</summary>

`{from, to}` 쌍의 배열을 받아 `in.test(row.from)` 이면 `out |= row.to`. 표가 `constexpr` 이면 `translate` 도 상수식에서 돈다 (`static_assert(ie.raw() == 0x5u)`). `CanEq<Flags<Status>, Flags<IntEn>>` 이 false 인 것까지 증명한다.

</details>

### 연습 5 — AtomicFlags 와 lost update

ISR 과 메인이 공유하는 pending 비트를 `AtomicFlags<E>` 로 만들어라 (`set` = fetch_or, `clear` = fetch_and, `take` = exchange(0)). 진짜 스레드 없이 "메인의 읽기 → ISR 의 쓰기 → 메인의 쓰기" 순서를 코드로 재현해 lost update 를 보여라.

<details>
<summary>답</summary>

`shared` 에 대해 `tmp = shared; shared |= 2; shared = tmp | 1;` 을 순서대로 실행하면 ISR 의 비트가 사라져 `shared == 1`. `AtomicFlags::set` 은 읽기-쓰기가 한 덩어리다. 주의: 하네스의 CHECK 는 인터리빙을 **순서로 시뮬레이션**한 것이고, `std::atomic` 이 정말 lock-free 인지는 타깃별로 다르다 (Cortex-M0 는 CPP-A04 참고).

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
# 24/24 passed
```

## 10. 핵심 정리

- `E` 는 원소, `Flags<E>` 는 집합. 원소 → 집합은 암시적, 정수와는 `from_raw()`/`raw()` 로만 오간다.
- opt-in: `flag_traits<E>` 특수화(또는 ADL 훅) + `FlagEnum` concept. 전역 `operator|` 는 금지.
- 이항 연산자는 **hidden friend** (추론이 변환을 안 보므로). `E | E` 만 네임스페이스 템플릿 + enum 쪽 `using`.
- `~` 는 valid 안의 여집합. 레지스터 RMW 는 `clear_in`/`set_in` — `reg & (~f).raw()` 는 다른 필드를 지운다.
- 불변식은 입구에서 한 번만: `|`, `&`, `^` 를 다시 자르지 않는다. `a & ~b` 대신 `a.without(b)`.
- `explicit operator bool`: `if (f & m)` 는 되고 `f + 1` 은 안 된다.
- 측정 결론: 같은 일을 시키면 raw 와 같은 코드. 다른 줄은 raw 가 자르는 일을 안 하기 때문이다.
- `Flags` 는 값 타입일 뿐, ISR 공유에는 atomic 이 필요하다.

## 참고자료

- [cppreference: Enumeration declaration](https://en.cppreference.com/w/cpp/language/enum) — 고정 기저 타입의 값 범위 ([dcl.enum]/8)
- [cppreference: Argument-dependent lookup](https://en.cppreference.com/w/cpp/language/adl) — using-directive 는 무시, using-declaration 은 찾음 ([basic.lookup.argdep])
- [cppreference: Friend declaration](https://en.cppreference.com/w/cpp/language/friend) — 클래스 안에서 정의한 friend 는 ADL 로만 찾아진다
- [cppreference: Constraints and concepts](https://en.cppreference.com/w/cpp/language/constraints) — 원자 제약, 다른 지점에서 결과가 달라지면 ill-formed NDR ([temp.constr.atomic]/3)
- [cppreference: Implicit conversions — Contextual conversions](https://en.cppreference.com/w/cpp/language/implicit_conversion) — `explicit operator bool` ([conv]/4)
- [cppreference: std::is_scoped_enum (C++23)](https://en.cppreference.com/w/cpp/types/is_scoped_enum), [std::has_single_bit (C++20)](https://en.cppreference.com/w/cpp/numeric/has_single_bit)
- [bitmask.types] — 표준 라이브러리의 비트마스크 타입 요구사항 (`std::ios_base::fmtflags` 등)
- [Arm AAPCS32](https://github.com/ARM-software/abi-aa/blob/main/aapcs32/aapcs32.rst) — 인자 전달 Stage B (B.2 정수 확장, B.4 composite 크기), 4바이트 이하 composite 반환
- [Qt: QFlags](https://doc.qt.io/qt-6/qflags.html) — 같은 문제의 널리 쓰이는 구현 (`testFlag(0)` 의 의미는 구현마다 다르니 문서를 확인할 것 — 미확인)
- 이 레슨의 실험: `demo_asm.cpp` + `asm_compare.sh` (arm64 / Cortex-M4 clang / arm-none-eabi-g++ 14.2.1), `demo_errors.cpp` (`-DSHOW_ERROR=1..12`), `flags_final.hpp` (solution.cpp 의 라이브러리 복사본)
- 교차 참조: CPP-B03(단일 값 필드, enable_bitmask), CPP-B05(volatile), CPP-M02(W1C/reserved RMW), CPP-A04(atomic), C-B02(비트 연산, 정수 승격)
