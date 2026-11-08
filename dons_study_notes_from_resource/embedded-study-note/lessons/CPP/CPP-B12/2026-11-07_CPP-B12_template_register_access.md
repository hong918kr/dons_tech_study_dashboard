---
id: CPP-B12
time: 50분
updated: 2026-11-07
---
# CPP-B12 템플릿 기초: 폭별 레지스터 read/write

## 학습 목표

- 함수 템플릿(function template) `template <typename T> T reg_read(uintptr_t)` 하나로 8/16/32/64비트 레지스터 접근을 일반화하고, 컴파일러가 **T마다 함수를 하나씩 찍어낸다(instantiation)** 는 것을 `nm | c++filt` 로 직접 확인한다.
- T가 "unsigned 정수 레지스터 타입"이 아니면 컴파일 에러가 나게 만든다 — **C++17 `static_assert` + type trait** 방식과 **C++20 concept** 방식을 둘 다 쓰고, 에러 메시지와 SFINAE 친화성(requires 로 검사 가능한가)의 차이를 설명한다.
- **템플릿 인자 추론(template argument deduction)** 규칙을 안다: `reg_write(a, 5)` 는 왜 `int` 가 되고, `reg_read(a)` 는 왜 `<T>` 를 꼭 적어야 하는가.
- mock 메모리를 바이트 배열 + `std::memcpy` 로 만들어 **strict aliasing UB 없이** 테스트하고, 실제 MMIO는 `volatile T*` 캐스트라는 것을 구분한다.
- 자연 정렬(natural alignment) 검사와 "레지스터 폭을 틀리면 옆 레지스터가 깨진다"는 펌웨어 버그를 재현한다.

## 1. 문제

레지스터 폭(8/16/32비트)을 틀리게 접근하는 것은 실제 칩에서 HardFault나 "옆 레지스터가 조용히 덮어써지는" 버그로 이어진다. 면접에서는 "C의 `REG8/REG16/REG32` 매크로를 C++로 어떻게 안전하게 바꾸겠냐"는 질문으로 템플릿 기초 + type traits + 추론 규칙을 한꺼번에 본다.

C에서는 보통 이렇게 쓴다.

```c
#define REG8(a)  (*(volatile uint8_t  *)(a))
#define REG16(a) (*(volatile uint16_t *)(a))
#define REG32(a) (*(volatile uint32_t *)(a))
```

폭마다 이름이 하나씩 필요하고, `(*(volatile float *)(a))` 같은 실수도 아무도 막지 않는다. C++ 함수 템플릿은 **코드 한 벌**로 모든 폭을 처리하면서, 허용할 타입을 **컴파일 타임**에 제한할 수 있다. 그리고 컴파일러는 쓰인 T마다 C에서 손으로 쓴 것과 같은 함수를 하나씩 만든다 — 런타임 비용은 0이다.

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">템플릿 1개 → 쓰인 T 마다 함수 1개 (암시적 인스턴스화)</text>
  <rect x="10" y="40" width="250" height="150" rx="8" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="22" y="64">template &lt;RegWord T&gt;</text>
    <text x="22" y="84">T reg_read(uintptr_t a) {</text>
    <text x="22" y="104">  return *(volatile const T*)a;</text>
    <text x="22" y="124">}</text>
    <text x="22" y="156">호출: reg_read&lt;uint8_t&gt;(..)</text>
    <text x="22" y="176">      reg_read&lt;uint32_t&gt;(..) ...</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M262 80 L330 58"/><path d="M262 100 L330 98"/><path d="M262 120 L330 138"/><path d="M262 140 L330 178"/>
  </g>
  <g stroke="currentColor">
    <rect x="332" y="44" width="400" height="28" class="f-ok"/>
    <rect x="332" y="84" width="400" height="28" class="f-ok"/>
    <rect x="332" y="124" width="400" height="28" class="f-ok"/>
    <rect x="332" y="164" width="400" height="28" class="f-ok"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="342" y="63">reg_read&lt;unsigned char&gt;       → ldrb w0,[x0]</text>
    <text x="342" y="103">reg_read&lt;unsigned short&gt;      → ldrh w0,[x0]</text>
    <text x="342" y="143">reg_read&lt;unsigned int&gt;        → ldr  w0,[x0]</text>
    <text x="342" y="183">reg_read&lt;unsigned long long&gt;  → ldr  x0,[x0]</text>
  </g>
  <text x="10" y="222" fill="currentColor">reg_read&lt;int&gt; / &lt;bool&gt; / &lt;float&gt; 는 static_assert 또는 concept 가 막는다 → 인스턴스가 생기지 않는다.</text>
  <text x="10" y="242" fill="currentColor">오른쪽 명령: arm64 Mac 실제 출력 (demo_instantiation.cpp).</text>
  <text x="10" y="262" fill="currentColor">Cortex-M 에서도 LDRB / LDRH / LDR 로 같은 모양일 것 (예상).</text>
</svg>
```

호스트(맥)에서 `0x4000'0000` 을 진짜로 읽으면 segfault 이므로, 이 레슨은 **64바이트 바이트 배열 `mock::mem` 을 주소 공간처럼** 쓴다. `mock::BASE + off` 주소를 `mem[off]` 로 바꾸는 `mock_ptr()` 가 "버스" 역할을 하고, 모든 접근을 `mock::log` 에 (주소, 폭, 읽기/쓰기, 값)으로 기록해서 테스트가 "몇 비트로 접근했나"까지 확인한다.

### Part A — 폭별 read/write와 템플릿 인자 추론

- `template <typename T> T reg_read(std::uintptr_t addr)` / `template <typename T> void reg_write(std::uintptr_t addr, T value)` 를 구현한다.
- 백킹 스토어는 `std::uint8_t mem[64]` 이므로 `std::memcpy` 로 `sizeof(T)` 바이트를 복사한다 (uint8_t 배열을 `uint32_t*` 로 읽는 것은 strict aliasing 위반).
- 성공한 접근은 `mock::record(addr, sizeof(T), is_write, value)` 로 기록한다.
- 테스트는 little-endian 바이트 순서(`reg_read<uint8_t>(A)` 는 최하위 바이트)와 추론 결과(`reg_write(a, std::uint8_t{..})` 는 1바이트, `reg_write(a, 0xA5u)` 는 4바이트)를 확인한다.

### Part B — 타입 규칙과 정렬·범위 검사

- `is_reg_word_v<T>` (C++17 type trait)와 `concept RegWord` (C++20)를 같은 규칙으로 채운다: **unsigned 정수, `bool` 아님, 8바이트 이하**. 함정: `bool` 은 `std::is_unsigned_v` 가 **true** 다. `unsigned __int128` 도 libc++에선 integral 이다.
- `mock_ptr(addr, width)`: `[BASE, BASE+SIZE)` 안에 `width` 바이트가 다 들어가고 `addr % width == 0` 이면 포인터, 아니면 `bus_faults++` 후 `nullptr`. 폴트면 read 는 0, write 는 무시.
- 하네스는 `requires` 식으로 "이 호출이 컴파일되는가"를 bool 로 받아서 static_assert 버전과 concept 버전의 차이를 확인한다.

### Part C — 폭이 섞인 가짜 주변장치 드라이버

가짜 센서 인터페이스 `SNS` (`PERIPH = BASE + 0x20`): `CR`(32비트, bit0 EN), `SR`(16비트, bit0 TXE), `DR`(8비트 송신 FIFO), **바로 옆 0x09에 하드웨어 소유 `FIFO_LVL`(8비트)**, `BRR`(16비트), `ID`(32비트, 읽기 전용).

- `sns_init(brr)`: ID 확인 → BRR 16비트 쓰기 → CR 32비트 read-modify-write 로 EN set. ID가 다르면 아무것도 쓰지 않고 `false`.
- `sns_send(std::span<const uint8_t>)` (span은 CPP-B11 참고): 바이트마다 SR의 TXE를 확인하고 DR에 **8비트**로 쓴다. 보낸 개수 반환.
- 테스트는 접근 로그로 각 레지스터의 접근 폭과, `FIFO_LVL` 이 덮어써지지 않았는지 확인한다.

## 2. Problem (English)

> **Width-generic register access with compile-time type checking.**
>
> 1. Implement `template <typename T> T reg_read(std::uintptr_t addr)` and `template <typename T> void reg_write(std::uintptr_t addr, T value)` over a mock 64-byte address space starting at `0x4000'0000`. Use a byte array as backing store and copy with `std::memcpy` (no type-punned pointer casts). Explain what the real MMIO version looks like.
> 2. Reject at compile time any `T` that is not an unsigned integer type of at most 8 bytes (`bool`, signed types, floating point, enums, pointers, `unsigned __int128` must all fail). Do it twice: C++17 (`static_assert` + `<type_traits>`) and C++20 (a `concept`). Explain why `requires { reg_read<int>(a); }` is `true` for the first and `false` for the second.
> 3. Reject misaligned (`addr % sizeof(T) != 0`) and out-of-range accesses by counting a bus fault and not touching memory.
> 4. Write `sns_init` / `sns_send` for a peripheral with 32-, 16- and 8-bit registers, where an 8-bit data register sits right next to a hardware-owned byte.
>
> Example: after `reg_write<uint32_t>(A, 0x11223344)`, `reg_read<uint8_t>(A) == 0x44`, `reg_read<uint16_t>(A + 2) == 0x1122`, and `reg_read<uint32_t>(A + 2)` faults. `reg_write_c(A, 5)` must not compile (`5` deduces `int`).

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 깨짐 대비
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
# 처음엔 "8/32 passed" — TODO 7곳을 채워 32/32 를 만든다
```

## 4. 힌트

<details>
<summary>힌트 1 — read 는 세 줄: 포인터 얻기 → memcpy → 기록</summary>

`T v{}; if (auto* p = mock_ptr(addr, sizeof(T))) { std::memcpy(&v, p, sizeof(T)); mock::record(...); } return v;` — 폴트면 `v` 는 0 그대로. write 는 memcpy 방향만 반대.

</details>

<details>
<summary>힌트 2 — 범위 검사는 빼기를 먼저</summary>

`addr + width > BASE + SIZE` 는 addr 가 최댓값 근처면 덧셈이 넘친다(wrap-around). `addr < BASE` 를 먼저 거르고 `addr - BASE > SIZE - width` 로 비교하면 넘침이 없다. 정렬은 width 가 2의 거듭제곱이니 `addr & (width - 1)`.

</details>

<details>
<summary>힌트 3 — bool 은 왜 통과하나?</summary>

`std::is_unsigned<T>` 의 정의는 "산술 타입이고 `T(0) < T(-1)`". `bool(0)` 은 false, `bool(-1)` 은 true → `false < true` → **true**. 그래서 `!std::is_same_v<T, bool>` 을 따로 붙여야 한다.

</details>

<details>
<summary>힌트 4 — concept 은 && 로 쪼개 써라</summary>

`concept RegWord = std::unsigned_integral<T> && !std::same_as<T, bool> && (sizeof(T) <= 8);` 처럼 **atomic constraint** 들의 `&&` 로 쓰면, 실패했을 때 clang 이 "어느 조건이 false 인지"를 짚어 준다.

</details>

<details>
<summary>힌트 5 — Part C 는 폭을 명시해서 호출</summary>

`reg_write<std::uint8_t>(sns::DR, b)` 처럼 레지스터 폭을 호출부에 적는다. CR 의 EN set 은 `reg_write<uint32_t>(CR, reg_read<uint32_t>(CR) | CR_EN)`.

</details>

## 5. 풀이

### Part A — 템플릿 한 벌, memcpy 로 안전하게

**왜 memcpy 인가 (strict aliasing).** 백킹 스토어 `mem` 의 원소는 `uint8_t` 객체다. `*reinterpret_cast<uint32_t*>(mem + off)` 로 읽으면 "uint8_t 객체를 uint32_t 타입 glvalue 로 접근"하게 되고, 이것은 [basic.lval]/11 의 허용 목록(같은 타입, signed/unsigned 짝, `char`/`unsigned char`/`std::byte`)에 없으므로 **UB** 다. 반대 방향(아무 객체를 `unsigned char*` 로 읽기)만 허용된다. 게다가 `mem + 2` 를 `uint32_t*` 로 바꾸면 정렬도 깨진다. `std::memcpy` 는 바이트 복사라서 두 문제가 다 없다 (자세한 건 C-M13 참고).

비용이 걱정되면 직접 확인하면 된다. 4바이트 / 2바이트 memcpy 를 -O2 로 컴파일한 arm64 실제 출력:

```text
__Z4rd32m:                         __Z4rd16m:
  adrp x8, _mem@PAGE                 adrp x8, _mem@PAGE
  add  x8, x8, _mem@PAGEOFF          add  x8, x8, _mem@PAGEOFF
  ldr  w0, [x8, x0]                  ldrh w0, [x8, x0]
  ret                                ret
```

memcpy 호출은 사라지고 load 명령 1개만 남는다. 다른 방법: C++20 `std::bit_cast` (추가 연습 5). **C에서 쓰던 union 타입 퍼닝은 C++에선 UB** 이니 쓰지 않는다 (C는 C11 6.5.2.3 각주로 허용).

**실제 칩에서는?** 주변장치 레지스터 자리에는 C++ 객체가 없다. 그래서 정수 주소를 포인터로 바꾸고(`reinterpret_cast`, 정수→포인터 변환은 구현 정의 [expr.reinterpret.cast]/5) `volatile` 로 접근한다:

```cpp
template <RegWord T> T    mmio_read(std::uintptr_t a)       { return *reinterpret_cast<volatile const T*>(a); }
template <RegWord T> void mmio_write(std::uintptr_t a, T v) { *reinterpret_cast<volatile T*>(a) = v; }
```

`volatile` 접근은 관찰 가능한 동작(observable behavior, [intro.abstract]/6)이라 생략·병합되지 않는다 (C-B09, CPP-B05 참고). 표준이 "정확히 sizeof(T) 폭의 명령 1개"를 글자로 보장하지는 않지만, GCC/Clang 은 자연 정렬된 워드 이하 volatile 스칼라에 그 폭의 load/store 1개를 낸다 — CMSIS 헤더 전체가 이 동작에 기대고 있다. `reinterpret_cast` 는 constexpr 안에서 못 쓰므로 이 함수들은 constexpr 이 될 수 없다.

**템플릿 인자 추론** ([temp.deduct.call]). 함수 인자의 타입에서 T 를 거꾸로 알아낸다. 반환 타입은 추론에 쓰이지 않는다.

| 호출 | 추론된 T | 결과 |
|---|---|---|
| `reg_read(a)` | 추론할 단서 없음 | 에러 → `reg_read<uint16_t>(a)` 처럼 꼭 명시 |
| `reg_write(a, 0xA5u)` | `unsigned int` | 4바이트 쓰기 |
| `reg_write(a, std::uint8_t{0xA5})` | `unsigned char` | 1바이트 쓰기 |
| `reg_write(a, b)` (`const uint8_t& b`) | `unsigned char` | 값 전달이라 참조·최상위 const 는 버려짐 |
| `reg_write(a, 5)` | `int` | 거부 (static_assert / concept) |
| `reg_write(a, b \| 1)` (`uint8_t b`) | `int` | 정수 승격(integral promotion) → 거부 (C-B02 참고) |
| `reg_write(a, 1ul)` | `unsigned long` | **맥 8바이트, Cortex-M 4바이트** — 플랫폼마다 폭이 다름 |
| `reg_write<uint8_t>(a, 0x1FF)` | 명시 → 추론 안 함 | int→uint8_t 변환, 값 잘림 경고 (`-Werror` 면 에러) |

결론: 레지스터 코드에서는 **폭을 호출부에 명시**(`reg_write<uint16_t>(...)`)하는 습관이 안전하다. 강제하고 싶으면 값 인자를 `std::type_identity_t<T>` 로 만들어 추론을 끈다 (추가 연습 2).

### Part B — 타입 규칙: static_assert vs concept

| T | `is_integral` | `is_unsigned` | 규칙 결과 | 이유 |
|---|---|---|---|---|
| `uint8_t`…`uint64_t` | ✓ | ✓ | 허용 | |
| `int` | ✓ | ✗ | 거부 | signed |
| `bool` | ✓ | **✓** | 거부 | `bool(0) < bool(-1)` 이 true → 따로 제외 |
| `float` | ✗ | ✗ | 거부 | 산술이지만 정수 아님 |
| `enum class Mode : uint32_t` | ✗ | ✗ | 거부 | scoped enum 은 integral 이 아님 (CPP-B03 참고) |
| `unsigned __int128` | ✓ (libc++) | ✓ | 거부 | `sizeof == 16` — 128비트 버스 접근은 없음 |
| `char` | ✓ | 플랫폼마다 | 맥: 거부 / Cortex-M: 허용 | `char` 부호는 구현 정의. Apple arm64 는 signed, AAPCS(ARM) 는 unsigned |

(맥에서 직접 찍어 본 값: `is_integral_v<unsigned __int128>=1`, `is_unsigned_v<bool>=1`, `std::unsigned_integral<bool>=1`, `is_unsigned_v<char>=0`, `is_unsigned_v<char8_t>=1`.)

**두 방식은 "언제 검사하나"가 다르다.**

```svg
<svg viewBox="0 0 760 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">reg_read&lt;int&gt;(a) 를 컴파일러가 처리하는 순서</text>
  <g stroke="currentColor">
    <rect x="10" y="40" width="160" height="50" rx="6" class="f-mut"/>
    <rect x="190" y="40" width="190" height="50" rx="6" class="f-mut"/>
    <rect x="400" y="40" width="150" height="50" rx="6" class="f-mut"/>
    <rect x="570" y="40" width="180" height="50" rx="6" class="f-mut"/>
  </g>
  <g fill="currentColor">
    <text x="20" y="62">① 이름 찾기 · 후보</text><text x="20" y="80">템플릿 인자 대입</text>
    <text x="200" y="62">② 제약 검사 (concept)</text><text x="200" y="80">→ 후보에서 제외 가능</text>
    <text x="410" y="62">③ 오버로드 결정</text><text x="410" y="80">함수 하나 선택</text>
    <text x="580" y="62">④ 본문 인스턴스화</text><text x="580" y="80">static_assert 평가</text>
  </g>
  <g stroke="currentColor" fill="none"><path d="M170 65 L190 65"/><path d="M380 65 L400 65"/><path d="M550 65 L570 65"/></g>
  <rect x="190" y="110" width="210" height="60" rx="6" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor"><text x="200" y="132">concept 버전: 여기서 탈락</text><text x="200" y="152">"no matching function"</text><text x="200" y="166" font-size="12">requires{...} → false (조용히)</text></g>
  <rect x="560" y="110" width="190" height="60" rx="6" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor"><text x="570" y="132">static_assert 버전:</text><text x="570" y="152">여기서야 하드 에러</text><text x="570" y="166" font-size="12">requires{...} → true (!)</text></g>
  <text x="10" y="205" fill="currentColor">requires 식 / decltype 같은 미평가 문맥은 ①~③만 하고 ④(본문)는 보지 않는다.</text>
  <text x="10" y="227" fill="currentColor">→ static_assert 버전은 "호출 가능해 보이는데 실제로 부르면 터지는" 함수 (SFINAE 비친화).</text>
  <text x="10" y="249" fill="currentColor">→ concept 버전은 오버로드 집합에서 깔끔하게 빠지므로 다른 오버로드가 대신 선택될 수도 있다.</text>
</svg>
```

하네스의 `can_read17<int>` 가 **true** 로 통과하는 이유가 이것이다. `reg_read<int>` 의 선언(반환 타입 `int`, 인자 `uintptr_t`)만 보면 멀쩡하고, static_assert 는 본문을 인스턴스화할 때([temp.inst]) 비로소 평가된다. `requires` 식의 피연산자는 미평가 피연산자(unevaluated operand)라서 odr-use 가 아니고 본문 인스턴스화를 일으키지 않는다. 반면 concept 제약은 선언의 일부라서 후보를 고를 때([temp.constr], [over.match.viable]) 검사된다.

> 주의: `requires` 식을 템플릿 밖에서 쓰면 안의 잘못된 식이 그냥 하드 에러다 ([expr.prim.req] 의 note). 그래서 하네스는 `template <typename T> constexpr bool can_read20 = requires(...) {...};` 처럼 변수 템플릿으로 감쌌다.

**실제 에러 메시지** (`demo_errors.cpp`, `-DSHOW_ERROR=N -fsyntax-only` 로 실행한 맥 clang 21 출력, 경로만 잘라냄):

```text
# SHOW_ERROR=1 — static_assert 버전, reg_read<int>(0)
demo_errors.cpp:22:19: error: static assertion failed due to requirement 'is_reg_word_v<int>':
      reg_read<T>: T must be an unsigned integer register type (uint8/16/32/64_t)
demo_errors.cpp:40:11: note: in instantiation of function template specialization 'reg_read<int>' requested here

# SHOW_ERROR=2 — concept 버전, reg_read_c<int>(0)
demo_errors.cpp:43:11: error: no matching function for call to 'reg_read_c'
demo_errors.cpp:32:3: note: candidate template ignored: constraints not satisfied [with T = int]
demo_errors.cpp:31:11: note: because 'int' does not satisfy 'RegWord'
demo_errors.cpp:18:19: note: because 'int' does not satisfy 'unsigned_integral'
.../__concepts/arithmetic.h:34:46: note: because '!signed_integral<int>' evaluated to false

# SHOW_ERROR=3 — concept 버전, reg_read_c<bool>(0)
demo_errors.cpp:18:48: note: because '!std::same_as<bool, bool>' evaluated to false

# SHOW_ERROR=4 — 추론 함정, reg_write_c(0, 5)
demo_errors.cpp:34:6: note: candidate template ignored: constraints not satisfied [with T = int]
demo_errors.cpp:18:19: note: because 'int' does not satisfy 'unsigned_integral'

# SHOW_ERROR=5 — T 명시 + 넘치는 리터럴, reg_write<std::uint8_t>(0, 0x1FF)
demo_errors.cpp:52:32: error: implicit conversion from 'int' to 'unsigned char' changes value from 511 to 255
      [-Werror,-Wconstant-conversion]
```

| | C++17 `static_assert` | C++20 concept |
|---|---|---|
| 검사 시점 | 본문 인스턴스화 (④) | 후보 선택 (②) |
| 메시지 | 내가 쓴 문자열 — 의도 설명에 좋음 | 실패한 **조건을 한 단계씩** 짚음 (`!same_as<bool,bool>`) |
| `requires`/SFINAE 로 검사 | 불가 (항상 true) | 가능 |
| 오버로드와 함께 | 다른 후보로 넘어가지 못함 | 제외되고 다른 후보가 선택될 수 있음 |
| 쓸 수 있는 곳 | C++11~ | C++20~ (arm-none-eabi-g++ 10 이상에서 `-std=c++20`) |

실무 조합: 공개 API 는 concept 으로 제약하고, 그 안에서 더 구체적인 이유를 문자열로 남기고 싶으면 static_assert 를 추가한다 (HAL 오용 차단 전반은 CPP-M14).

**정렬·범위 검사.** `mock_ptr` 는 실제 버스가 하는 일을 흉내 낸다. Cortex-M0/M0+ (ARMv6-M)는 비정렬 접근이 **항상 HardFault**. Cortex-M3/M4 (ARMv7-M)는 Normal 메모리에 대한 LDR/LDRH 비정렬은 허용(`CCR.UNALIGN_TRP=0` 일 때)하지만 LDRD/LDM 등은 항상 UsageFault 이고, 주변장치(Device) 영역의 비정렬 접근은 아키텍처가 결과를 보장하지 않는다. 그래서 레지스터는 **항상 자연 정렬**로 접근한다. 범위 검사는 `addr - BASE > SIZE - width` 형태로 써서 덧셈 오버플로를 피했다 (`addr = BASE - 4` 는 첫 조건 `addr < BASE` 에서 걸린다).

### Part C — 폭을 틀리면 옆 레지스터가 깨진다

```svg
<svg viewBox="0 0 740 240" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">SNS 레지스터 맵 (PERIPH = BASE + 0x20) — 0x08~0x0B 워드</text>
  <g stroke="currentColor">
    <rect x="60" y="40" width="80" height="34" class="f-acc"/>
    <rect x="140" y="40" width="80" height="34" class="f-warn"/>
    <rect x="220" y="40" width="80" height="34" class="f-mut"/>
    <rect x="300" y="40" width="80" height="34" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="72" y="62">DR  0x08</text><text x="148" y="62">FIFO 0x09</text><text x="228" y="62">rsvd 0x0A</text><text x="308" y="62">rsvd 0x0B</text>
  </g>
  <g fill="currentColor">
    <text x="400" y="52">DR: 송신 데이터 (8비트 쓰기 = FIFO push 1개)</text>
    <text x="400" y="70">FIFO_LVL: 하드웨어가 채우는 값 (읽기 전용)</text>
  </g>
  <rect x="60" y="95" width="80" height="30" class="f-ok" stroke="currentColor"/>
  <text x="150" y="115" fill="currentColor">reg_write&lt;uint8_t&gt;(DR, b)  → strb : 0x08 한 바이트만. FIFO_LVL = 7 유지</text>
  <rect x="60" y="140" width="320" height="30" class="f-warn" stroke="currentColor"/>
  <text x="390" y="160" fill="currentColor">reg_write&lt;uint32_t&gt;(DR, b) → str : 4바이트</text>
  <text x="60" y="195" fill="currentColor">0x09~0x0B 에 0 이 써진다 → FIFO_LVL 이 7 → 0 (Part C 테스트가 잡는다). 정렬은 맞아도 폭이 틀렸다.</text>
  <text x="60" y="217" fill="currentColor">(STM32 SPI 의 data packing: SPI_DR 에 8비트 접근은 1바이트, 16비트 접근은 2바이트를 FIFO에 넣는다.)</text>
</svg>
```

정답 드라이버는 레지스터마다 폭을 적는다: ID/CR 은 `uint32_t`, SR/BRR 은 `uint16_t`, DR 은 `uint8_t`. 일부러 `reg_write<std::uint32_t>(sns::DR, b)` 로 바꿔서 돌려 본 실제 결과:

```text
FAIL wrongwidth.cpp:185  hw_peek<std::uint8_t>(sns::FIFO_LVL) == 7u
FAIL wrongwidth.cpp:194  dr_writes == 3 && bad_width == 0
30/32 passed
```

DR(0x28)은 4의 배수라 정렬 검사는 통과한다 — **정렬이 맞아도 폭이 틀리면 잘못된 코드**다. 반대로 `reg_write(sns::DR, b | 0x00)` 처럼 쓰면 `b | 0` 이 `int` 로 승격되어 `static assertion failed due to requirement 'is_reg_word_v<int>'` 로 컴파일이 막힌다 (이것도 실제로 확인). 타입 규칙이 "실수로 int 폭 쓰기"를 잡아 주는 셈이다.

CR 의 EN set 은 32비트 read-modify-write 다. ISR 이 같은 CR 을 건드린다면 경쟁이 생긴다 (C-B01 Part C, CPP-B06 참고).

**인스턴스화를 눈으로 보기** (`demo_instantiation.cpp` — 진짜 MMIO 스타일 `volatile T*` 캐스트, 실행하지 않고 오브젝트만 만든다):

```bash
c++ -std=c++20 -O0 -fno-exceptions -fno-rtti -c demo_instantiation.cpp -o /tmp/inst_O0.o
nm /tmp/inst_O0.o | c++filt
```

```text
0000000000000000 T poll_all()
00000000000000b8 T unsigned char reg_read<unsigned char>(unsigned long)
00000000000000e8 T unsigned int reg_read<unsigned int>(unsigned long)
00000000000000d0 T unsigned short reg_read<unsigned short>(unsigned long)
0000000000000100 T unsigned long long reg_read<unsigned long long>(unsigned long)
0000000000000118 T void reg_write<unsigned char>(unsigned long, unsigned char)
0000000000000138 T void reg_write<unsigned int>(unsigned long, unsigned int)
```

- 쓰인 T 4개 → `reg_read` 4개, 쓰인 T 2개 → `reg_write` 2개. 안 쓰인 T(예: `uint16_t` write)는 **만들어지지 않는다**.
- `reg_write(PERIPH, 0x1u)` 는 추론으로 `reg_write<unsigned int>` 가 됐다.
- 맹글링된 원래 이름(`nm` 만): `__Z8reg_readIhET_m`, `__Z8reg_readItET_m`, `__Z8reg_readIjET_m`, `__Z8reg_readIyET_m`, `__Z9reg_writeIhEvmT_`. `I…E` 가 템플릿 인자(h=unsigned char, t=unsigned short, j=unsigned int, y=unsigned long long), `T_` 는 "반환 타입 = 첫 템플릿 인자", `m` = `unsigned long`(= 맥의 `uintptr_t`). Mach-O 는 앞에 `_` 가 하나 더 붙는다 (맹글링 자체는 CPP-B02 참고).
- `nm -m` 으로 보면 템플릿 인스턴스는 전부 `weak external` 이다. 여러 .cpp 가 같은 `reg_read<uint32_t>` 를 만들어도 링커가 하나만 남긴다. ELF(arm-none-eabi)에서는 COMDAT 그룹 섹션(`.text._Z8reg_readIjET_j` 류)에 들어가고 `nm` 에 `W` 로 보인다 (예상 — 직접 실행 못 함). 또한 arm-none-eabi 에서는 `uintptr_t` 가 `unsigned int`(`j`), `uint32_t` 가 `unsigned long`(`m`) 이라 같은 함수라도 맹글링 문자가 달라진다 (예상).
- `-O2` 로 다시 만들면 `nm` 에 `poll_all()` 만 남는다 — 전부 인라인됐다. `-O2 -S` 의 실제 arm64 코드:

```text
__Z8poll_allv:
	mov	w8, #8
	movk	w8, #16384, lsl #16       ; x8 = 0x40000008
	ldrb	w9, [x8]                  ; reg_read<uint8_t>(PERIPH+0x08)
	ldurh	w10, [x8, #-4]            ; reg_read<uint16_t>(PERIPH+0x04)
	add	x9, x10, x9
	mov	w10, #1073741824           ; 0x40000000
	ldr	w11, [x10]                ; reg_read<uint32_t>(PERIPH)
	ldr	x12, [x8, #8]             ; reg_read<uint64_t>(PERIPH+0x10)
	mov	w13, #90
	strb	w13, [x8]                 ; reg_write<uint8_t>(PERIPH+0x08, 0x5A)
	mov	w8, #1
	str	w8, [x10]                 ; reg_write<unsigned>(PERIPH, 1)
	...                               ; (add 2개 + ret 생략, ; 주석은 설명용으로 붙임)
```

C 에서 `REG8/REG16/REG32` 매크로로 쓴 것과 같은 명령이다. Cortex-M 에서는 `LDRB/LDRH/LDR/STRB/STR` 로 같은 모양이 나오고, `uint64_t` volatile 읽기는 32비트 버스라 LDRD 또는 LDR 2개로 쪼개진다 (예상) — 그래서 Cortex-M 용으로는 8바이트를 막는 더 엄격한 규칙이 낫다 (추가 연습 4). 코드 크기 관점: 인스턴스 N개 = 손으로 쓴 함수 N개와 같고, 대부분 인라인되어 0개가 된다.

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution   # 32/32 passed
c++ -std=c++20 -g -O1 -fsanitize=address,undefined -fno-exceptions -fno-rtti solution.cpp -o sol_asan && ./sol_asan   # 깨끗하게 32/32

# 관찰용 데모
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti -DSHOW_ERROR=2 -fsyntax-only demo_errors.cpp   # N=1..5
c++ -std=c++20 -O0 -fno-exceptions -fno-rtti -c demo_instantiation.cpp -o /tmp/inst_O0.o && nm /tmp/inst_O0.o | c++filt
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| mock 에서 `*reinterpret_cast<uint32_t*>(mem + off)` | uint8_t 객체를 uint32_t 로 접근 → strict aliasing UB, 비정렬 포인터도 UB. -O2 에서 "가끔" 틀린 값 | `std::memcpy(&v, mem + off, sizeof v)` 또는 `std::bit_cast` |
| C 습관으로 `union { uint32_t w; uint8_t b[4]; }` 퍼닝 | C++ 에선 비활성 멤버 읽기 = UB | memcpy / bit_cast |
| 규칙을 `std::is_unsigned_v<T>` 만으로 | `bool` 이 통과한다 (`reg_write(a, true)` → 1바이트 쓰기) | `&& !std::is_same_v<T, bool>` |
| `reg_write(a, 5)` / `reg_write(a, b \| 1)` | 리터럴·승격 결과가 `int` → 의도와 다른 폭 (규칙이 없으면 4바이트 쓰기) | `reg_write<uint8_t>(a, ...)` 로 폭 명시 |
| `reg_write(a, 1ul)` | `unsigned long` 은 맥 8바이트 / Cortex-M 4바이트 — 호스트 테스트와 타깃 동작이 다름 | 고정 폭 `uint32_t{1}` 또는 `<uint32_t>` 명시 |
| `reg_read<char>(a)` | `char` 부호는 구현 정의 → 맥에선 거부, arm-none-eabi 에선 허용. 이식성 없음 | `uint8_t` 사용, 엄격 규칙에서 `char` 제외 (연습 4) |
| 범위 검사를 `addr + width <= BASE + SIZE` 로 | `addr` 가 `UINTPTR_MAX` 근처면 덧셈이 넘쳐 통과 | `addr >= BASE && addr - BASE <= SIZE - width` |
| static_assert 버전을 `requires` 로 검사 | 본문은 안 보므로 항상 true — 테스트가 거짓 통과 | concept 으로 제약하고 그걸 검사 |
| 8비트 레지스터를 32비트 RMW 로 | 옆 바이트 레지스터를 덮어씀 (Part C 재현) | 데이터시트의 접근 폭 그대로 |

## 8. 면접 Q&A

<details>
<summary>Q1. C 의 REG8/REG16/REG32 매크로 대신 함수 템플릿을 쓰면 뭐가 좋나요? 비용은요?</summary>

이름 하나로 모든 폭을 처리하고, 허용 타입을 컴파일 타임에 제한하며(매크로는 `float` 도 받아 줌), 인자가 한 번만 평가되고(매크로 다중 평가 없음, C-B15), 타입 검사와 디버거 심볼이 생긴다. 비용: 쓰인 T 마다 함수가 하나 생기는데 이는 손으로 쓴 `reg_read8/16/32` 와 같은 양이고, -O2 에서 대부분 인라인되어 `ldrb/ldrh/ldr` 하나만 남는다 (nm 으로 확인). 꼬리질문 "코드 블로트는?" → 바디가 큰 템플릿을 T 가 많게 쓰면 커진다. 공통 부분을 비템플릿 함수로 빼거나 `extern template` + 명시적 인스턴스화로 한 TU 에만 만들게 한다.

</details>

<details>
<summary>Q2. static_assert 로 막는 것과 concept 으로 막는 것의 차이는?</summary>

static_assert 는 본문 인스턴스화 때 평가 → 하드 에러, 오버로드 결정에 참여 못 함, `requires`/SFINAE 로는 "호출 가능"으로 보임. concept 은 선언의 제약이라 후보 선택 단계에서 탈락 → 다른 오버로드가 선택될 수 있고, `requires` 로 검사 가능, clang 이 실패한 atomic constraint 를 짚어 준다. static_assert 는 사람이 읽을 문자열을 남길 수 있다는 장점. C++17 이하에서 SFINAE 친화로 하려면 `std::enable_if_t<is_reg_word_v<T>, int> = 0` 템플릿 인자를 썼다.

</details>

<details>
<summary>Q3. reg_read(addr) 는 왜 T 를 추론 못 하나요? reg_write(addr, 5) 는 무슨 일이 생기나요?</summary>

추론은 함수 인자 타입에서만 일어나고 반환 타입은 쓰이지 않는다 ([temp.deduct.call]). `addr` 는 `uintptr_t` 라 T 와 무관 → `reg_read<uint16_t>(a)` 로 명시해야 한다. `reg_write(a, 5)` 는 `5` 가 `int` 라 T=int → 규칙에 걸려 에러. 규칙이 없었다면 조용히 4바이트 쓰기가 된다. 폭 명시를 강제하려면 값 인자를 `std::type_identity_t<T>` 로 만들어 non-deduced context 로 만든다 ([temp.deduct.type]/5).

</details>

<details>
<summary>Q4. 테스트용 mock 에서 memcpy 를 쓴 이유는? 실제 칩에서도 memcpy 를 쓰나요?</summary>

mock 은 진짜 C++ 객체(uint8_t 배열)라 다른 타입 포인터로 읽으면 strict aliasing 위반([basic.lval]/11)이고 비정렬일 수도 있다. memcpy 는 바이트 복사라 둘 다 안전하고 -O2 에서 load 하나로 바뀐다. 실제 칩에서는 **memcpy 를 쓰면 안 된다**: memcpy 는 volatile 이 아니고 폭·횟수·순서를 보장하지 않는다 (바이트 단위로 쪼개거나, 합치거나, 없앨 수 있음). 레지스터는 `*reinterpret_cast<volatile T*>(addr)` 로 정확히 한 번, 그 폭으로 접근한다.

</details>

<details>
<summary>Q5. std::is_unsigned_v&lt;bool&gt; 은? char 는?</summary>

`bool` 은 true — `is_unsigned` 의 정의가 `T(0) < T(-1)` 이고 `false < true` 이기 때문. `std::unsigned_integral<bool>` 도 만족한다. 표준 용어로 bool 은 "unsigned integer type" 이 아니지만 trait 은 true 를 준다. `char` 는 부호가 구현 정의라 Apple arm64 에선 signed(false), ARM AAPCS(Cortex-M, arm-none-eabi)·AAPCS64 Linux 에선 unsigned(true). 그래서 레지스터 규칙에서 bool 은 명시적으로 빼고, char 는 쓰지 말고 `uint8_t` 를 쓴다.

</details>

<details>
<summary>Q6. 비정렬 레지스터 접근은 Cortex-M 에서 어떻게 되나요? 왜 템플릿에서 정렬을 검사하나요?</summary>

ARMv6-M(M0/M0+)는 모든 비정렬 접근이 HardFault. ARMv7-M(M3/M4/M7)은 Normal 메모리의 LDR/LDRH/STR/STRH 비정렬은 하드웨어가 처리(`CCR.UNALIGN_TRP=1` 이면 UsageFault)하지만 LDM/STM/LDRD/LDREX 는 항상 fault, Device 영역은 결과를 보장하지 않는다. 템플릿은 `sizeof(T)` 를 알기 때문에 `addr % sizeof(T)` 를 한 곳에서 검사할 수 있다 — 주소가 상수면 `static_assert` 로 컴파일 타임에(CPP-B04, CPP-M01), 아니면 디버그 빌드 assert 로.

</details>

<details>
<summary>Q7. 같은 reg_read&lt;uint32_t&gt; 를 10개 .cpp 에서 쓰면 함수가 10개 생기나요?</summary>

각 TU 가 암시적 인스턴스화를 하지만 weak / COMDAT(linkonce_odr) 심볼로 내보내서 링커가 하나만 남긴다 (맥 `nm -m` 에 `weak external`, ELF `nm` 에 `W`). ODR 상 모든 TU 의 정의가 같아야 한다. 컴파일 시간을 줄이려면 헤더에 `extern template std::uint32_t reg_read<std::uint32_t>(std::uintptr_t);` 를 두고 한 .cpp 에서 `template std::uint32_t reg_read<std::uint32_t>(std::uintptr_t);` 로 명시적 인스턴스화한다 (단, 인라인 기회는 줄 수 있음).

</details>

## 9. 추가 연습

### 연습 1 — reg_modify: 템플릿 read-modify-write

`reg_modify<T>(addr, clr, set)` = `(read & ~clr) | set` 을 쓰되, `reg_modify<std::uint16_t>(a, 0x00F0, 0x0030)` 처럼 **int 리터럴로 불러도** 되게 하라. `uint8_t` 에서 `~clr` 의 승격도 처리.

<details>
<summary>답 보기</summary>

`void reg_modify(std::uintptr_t a, std::type_identity_t<T> clr, std::type_identity_t<T> set)`. `type_identity_t<T>` 는 `typename type_identity<T>::type` 이라 중첩 이름 왼쪽의 T 는 추론에 쓰이지 않는다 → T 는 명시한 것으로 확정되고 int 리터럴은 그 타입으로 변환된다. `~clr` 은 `uint8_t` 라도 int 로 승격되어 상위 비트가 1이 되므로 `static_cast<T>(~clr)` 로 잘라 준다 (C-B02).

</details>

### 연습 2 — 폭 명시 강제

`reg_write_x<T>(addr, v)` 를 T 를 안 적으면 컴파일이 안 되게 만들고, `requires` 로 증명하라.

<details>
<summary>답 보기</summary>

값 인자를 `std::type_identity_t<T>` 로: `reg_write_x(a, 5u)` 는 T 를 추론할 곳이 없어 실패, `reg_write_x<std::uint8_t>(a, 0x7F)` 는 OK. 검사는 `template <typename V> constexpr bool can = requires(std::uintptr_t a, V v) { reg_write_x(a, v); };` → `!can<unsigned>`. "레지스터 폭은 호출부에서 보여야 한다"는 코딩 규칙을 컴파일러가 강제하는 셈.

</details>

### 연습 3 — 비타입 템플릿 인자로 필드 읽기

`field_read<T, Shift, Width>(addr)` 를 만들고 `Width == 0` 이나 `Shift + Width > 8*sizeof(T)` 는 호출 자체가 안 되게 하라.

<details>
<summary>답 보기</summary>

`template <RegWord T, unsigned Shift, unsigned Width> requires(Width >= 1 && Shift + Width <= 8 * sizeof(T))`. 마스크는 `constexpr T mask = (Width == 8*sizeof(T)) ? ~T{0} : (T{1} << Width) - 1` — 전체 폭일 때 시프트 UB 를 피한다 (C-B03). requires 절이라 `field_ok<std::uint8_t, 6, 4>` 같은 검사가 false 로 나온다. 레지스터·필드를 타입으로 묶는 본격 설계는 CPP-M01/M02.

</details>

### 연습 4 — Cortex-M 용 엄격한 concept

32비트 버스용 `Bus32Word`: `RegWord` 이면서 4바이트 이하, `char`/`char8_t`/`char16_t`/`char32_t`/`wchar_t` 제외. `uint8_t` 는 허용해야 한다.

<details>
<summary>답 보기</summary>

`concept Bus32Word = RegWord<T> && (sizeof(T) <= 4) && !std::same_as<T, char> && ...`. `uint8_t` 는 `unsigned char` 의 별명이라 목록에 없으니 통과. `char8_t` 는 `std::unsigned_integral` 을 만족해서 `RegWord` 는 통과하지만(맥에서 확인), 문자 타입이라 레지스터 폭 의도가 불분명 → 제외. `uint64_t` 는 Cortex-M 에서 단일 버스 접근이 아니므로 제외.

</details>

### 연습 5 — std::bit_cast 로 바이트 → 워드

`constexpr std::uint32_t load_le32(std::array<std::uint8_t, 4>)` 를 memcpy 없이 쓰고 `static_assert` 로 검증하라.

<details>
<summary>답 보기</summary>

`return std::bit_cast<std::uint32_t>(b);` — 크기가 같고 둘 다 trivially copyable 이면 되고, memcpy 와 달리 **constexpr** 이다 (C++20, `<bit>`). `static_assert(load_le32({0x44,0x33,0x22,0x11}) == 0x11223344)` 는 little-endian 타깃에서만 참 — 엔디언 독립 직렬화는 C-M05 참고.

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice   # 17/17 passed
```

## 10. 핵심 정리

- `template <typename T> T reg_read(uintptr_t)` — 쓰인 T 마다 함수 1개가 인스턴스화된다(`nm | c++filt`). -O2 면 인라인되어 `ldrb/ldrh/ldr` 하나. C 매크로와 비용 동일.
- 허용 타입 = unsigned integral **&& !bool** && sizeof ≤ 8 (Cortex-M 은 ≤ 4). `is_unsigned_v<bool>` 은 true, `char` 부호는 구현 정의.
- static_assert: 본문에서, 늦게, 하드 에러, `requires` 로 못 잡음. concept: 선언에서, 후보 단계, SFINAE 친화, 실패 조건을 짚어 줌.
- 추론은 인자에서만: `reg_read` 는 `<T>` 필수, `reg_write(a, 5)` 는 int, `b | 1` 도 int, `1ul` 은 플랫폼마다 폭이 다름 → 폭은 호출부에 명시 (`type_identity_t` 로 강제 가능).
- 테스트 mock 은 바이트 배열 + memcpy/bit_cast (strict aliasing 안전). 실제 MMIO 는 `*reinterpret_cast<volatile T*>(addr)` — memcpy 금지.
- 레지스터는 자연 정렬 + 데이터시트 폭 그대로. 폭이 틀리면 옆 레지스터가 깨진다.

## 참고자료

- [cppreference — Template argument deduction](https://en.cppreference.com/w/cpp/language/template_argument_deduction) · [Function template / instantiation](https://en.cppreference.com/w/cpp/language/function_template)
- [cppreference — Constraints and concepts](https://en.cppreference.com/w/cpp/language/constraints) · [Requires expression](https://en.cppreference.com/w/cpp/language/requires)
- [cppreference — std::is_unsigned](https://en.cppreference.com/w/cpp/types/is_unsigned) · [std::unsigned_integral](https://en.cppreference.com/w/cpp/concepts/unsigned_integral) · [std::type_identity](https://en.cppreference.com/w/cpp/types/type_identity) · [std::bit_cast](https://en.cppreference.com/w/cpp/numeric/bit_cast)
- [cppreference — Type aliasing (strict aliasing)](https://en.cppreference.com/w/cpp/language/reinterpret_cast#Type_aliasing)
- C++20 표준: [temp.deduct.call], [temp.deduct.type]/5 (non-deduced context), [temp.inst], [temp.constr], [expr.prim.req], [basic.lval]/11, [expr.reinterpret.cast], [intro.abstract]/6
- [Itanium C++ ABI — Mangling](https://itanium-cxx-abi.github.io/cxx-abi/abi.html#mangling)
- ARMv7-M Architecture Reference Manual (DDI 0403) — A3.2 Alignment support · ARMv6-M ARM (DDI 0419)
- [Apple — Writing ARM64 code for Apple platforms (char 는 signed)](https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms)
- STM32F0/F3/L4 Reference Manual — SPI "Data packing" (DR 접근 폭에 따른 FIFO 동작)
