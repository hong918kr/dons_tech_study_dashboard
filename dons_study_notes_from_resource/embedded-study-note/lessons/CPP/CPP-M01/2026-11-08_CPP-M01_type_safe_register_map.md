---
id: CPP-M01
time: 80분
updated: 2026-11-08
---
# CPP-M01 타입 안전 레지스터 맵: Reg<Addr, T, Access>

## 학습 목표

- 주소·폭·접근 권한(RO/WO/RW)을 **타입 하나**에 담은 `Reg<Addr, T, Access, Bus>` 를 구현하고, 잘못된 접근(RO 쓰기, WO 읽기, WO 에 RMW)을 "그런 함수가 없다"는 컴파일 에러로 만든다.
- 주소를 왜 **정수 NTTP** 로 두는지 설명한다: `reinterpret_cast` 는 상수식(constant expression)에 쓸 수 없으므로 포인터 NTTP 나 `constexpr` 포인터는 불가능하고, 캐스트는 접근하는 순간 버스 안에서 일어나야 한다.
- 버스(bus)를 정책 템플릿 인자로 빼서 **같은 드라이버 소스**를 실제 MMIO 경로와 mock 배열 경로로 컴파일하고, mock 접근 로그로 "읽기 1번 + 쓰기 1번" 같은 하드웨어 계약을 테스트한다.
- "제로 코스트"를 말로 하지 않고 실측한다: Cortex-M4 로 C 스타일과 나란히 컴파일해서 clang·GCC 모두 같은 명령인지 비교하고, `-O0` 비용과 UART 여러 개일 때의 코드 크기 비용까지 정직하게 말한다.

## 1. 문제

"C 레지스터 헤더를 그대로 쓰면 뭐가 문제고, C++ 로 어떻게 고치겠어요?" 는 펌웨어 면접의 단골이다. 여기서 보는 것은 템플릿 문법이 아니라 **"컴파일러가 대신 잡아 줄 수 있는 버그를 얼마나 타입으로 옮겼는가"** 와 **"그 코드를 하드웨어 없이 테스트할 수 있는가"** 다.

CMSIS 스타일 헤더에서 흔한 사고는 이런 것들이다.

```c
USART1->ISR = 0;              // 읽기 전용 레지스터에 쓰기. 레지스터가 __IO 로 선언돼 있으면 컴파일 통과, 하드웨어는 무시
x = USART1->ICR;              // 쓰기 전용 레지스터 읽기. 컴파일 통과, 항상 0 이거나 쓰레기
USART1->ICR |= USART_ICR_ORECF; // 쓰기 전용에 RMW. 읽은 쓰레기가 그대로 써진다
```

이미 배운 것은 다시 하지 않는다: 폭별 함수 템플릿과 타입 규칙(CPP-B12), `volatile` 의미(CPP-B05), `constexpr` 주소와 마스크(CPP-B04), 필드 단위 접근(CPP-M02 가 소유). 이 레슨의 소유 주제는 둘이다: **접근 권한을 타입에 넣기**, 그리고 **버스를 mock 으로 갈아 끼우는 설계**.

```svg
<svg viewBox="0 0 750 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">드라이버 소스는 한 벌, 버스만 바꿔 끼운다</text>
  <rect x="245" y="36" width="260" height="62" rx="8" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="258" y="58">template &lt;typename Map&gt;</text>
    <text x="258" y="76">uart_send / uart_recv / uart_init</text>
    <text x="258" y="92">Map::SR::read(), Map::TDR::write(b)</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M320 98 L175 140"/>
    <path d="M430 98 L575 140"/>
  </g>
  <rect x="20" y="140" width="320" height="86" rx="8" class="f-ok" stroke="currentColor"/>
  <rect x="410" y="140" width="320" height="86" rx="8" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="32" y="162">UartMap&lt;MmioBus&gt;  (펌웨어)</text>
    <text x="32" y="182">Reg&lt;0x40004400, u32, RO, MmioBus&gt;</text>
    <text x="32" y="200">→ *reinterpret_cast&lt;volatile T*&gt;(addr)</text>
    <text x="32" y="218">→ LDR/STR 한 줄</text>
    <text x="422" y="162">UartMap&lt;MockBus&gt;  (테스트)</text>
    <text x="422" y="182">Reg&lt;0x40004400, u32, RO, MockBus&gt;</text>
    <text x="422" y="200">→ mem[addr - BASE] + memcpy + 로그</text>
    <text x="422" y="218">→ hook 으로 장치 모델 호출</text>
  </g>
  <text x="10" y="255" fill="currentColor">Addr 값은 두 경로에서 똑같다. mock 은 주소를 바꾸지 않고, 버스 안에서 배열 칸으로 "번역"한다.</text>
  <text x="10" y="277" fill="currentColor">Access 태그(RO/WO/RW)가 어떤 멤버 함수가 존재할지를 정한다 → 잘못된 호출은 양쪽 경로 모두 컴파일 에러.</text>
</svg>
```

### Part A — Reg 뼈대와 mock 버스

- `template <std::uintptr_t Addr, typename T, typename A, typename Bus = MmioBus> struct Reg` 를 완성한다. 상태가 없는 빈 타입이고 전부 static 이다. `read()`, `write(v)`, 그리고 타입에서 꺼내는 `address`, `width`, `value_type`, `access`, `bus`.
- `MockBus` 는 `BASE` 와 같은 실제 주소를 받아 `mem[addr - BASE]` 로 번역한다. 바이트 배열 + `memcpy` 로 접근(왜 memcpy 인지는 CPP-B12 참고)하고, 접근마다 `(주소, 폭, 읽기/쓰기, 값)` 을 로그에 남긴 뒤 `hook` 을 부른다.
- 표 기반 테스트: 폭 3종 × 경계값 6개 왕복. 접근 1번당 로그 1줄, 같은 값을 두 번 읽어도 접근은 2번, 옆 바이트 보존, 범위 밖은 fault 로 카운트.

### Part B — 권한을 타입으로, 그리고 제약 전체

- `read` / `write` / `modify` / `set_bits` / `clear_bits` 에 `requires` 절을 붙인다. RO 에는 write 가 **없고**, WO 에는 read 가 없고, RMW 는 RW 에만 있다 (읽을 수 없으면 RMW 도 불가능).
- `modify(clear, set)` 은 정확히 읽기 1번 → 쓰기 1번. `uint8_t` / `uint16_t` 에서 `~clear` 의 정수 승격(integral promotion)에도 값이 맞아야 한다. 고정 시드 xorshift32 로 2000 라운드, 그림자 값(shadow)과 비교.
- 클래스 수준 제약: `Addr % sizeof(T) == 0`, `RegWord<T>`, `AccessPolicy<A>`, `RegisterBus<Bus>`. `Bus` 를 고르는 순간 멤버 템플릿 `read<T>` / `write<T>` 자격을 검사한다.
- 전부 `requires` 식과 `static_assert` 로 증명한다. 하네스의 `#if 0` 블록을 켜서 `static_assert` 도 통과시킨다.

### Part C — 같은 맵으로 진짜 드라이버

가짜 UART 맵 `UartMap<Bus, Base>`: `SR`(RO, u32), `RDR`(RO, **u8, 읽으면 FIFO 가 빠짐**), `TDR`(WO, u8), `CR`(RW, u32), `BRR`(RW, u16), `ICR`(WO, u32 플래그 클리어), `ID`(RO, u32).

- `uart_init<Map>(brr)`: ID 확인 → BRR 16비트 쓰기 → CR.EN 을 RMW 로 set. ID 가 틀리면 아무것도 쓰지 않는다. 테스트는 로그로 **접근 순서**까지 확인한다.
- `uart_send<Map>(span, max_polls)`: 바이트마다 TXE 폴링, 타임아웃이면 보낸 수 반환.
- `uart_recv<Map>(span)`: RXNE 동안 RDR 을 **정확히 1번** 읽기 (2번 읽으면 데이터가 사라진다).
- `uart_clear_overrun<Map>()`: ORE 가 서 있으면 `ICR` 에 쓰기 (RMW 금지). 테스트는 "ICR 은 읽지 않았다"까지 본다.
- 하네스의 장치 모델이 `hook` 으로 TX FIFO busy, RX FIFO pop, ICR 클리어를 흉내 낸다. 그리고 `(void)&uart_send<Real>` 로 **MMIO 쪽도 같은 소스로 컴파일됨**을 확인한다.

## 2. Problem (English)

> **Type-safe memory-mapped register map with a swappable bus.**
>
> Implement `Reg<Addr, T, Access, Bus>` where `Addr` is an integer non-type template parameter, `T` is an unsigned register word (`uint8/16/32_t`), `Access` is one of `RO`, `WO`, `RW`, and `Bus` defaults to `MmioBus`.
>
> 1. `Reg` is an empty type with only static members: `T read()`, `void write(T)`, `void modify(T clear, T set)`, `void set_bits(T)`, `void clear_bits(T)`, plus `address`, `width`, `value_type`, `access`, `bus`.
> 2. Illegal operations must be **compile errors**, not silent runtime behavior: writing an `RO` register, reading a `WO` register, and any read-modify-write on `RO` or `WO`. Prove it with requires-expressions (`CanWrite<RoReg> == false`).
> 3. Misaligned registers (`Addr % sizeof(T) != 0`), non-register types (`int`, `bool`, `uint64_t`) and invalid bus types must not be nameable.
> 4. `MmioBus` performs `*reinterpret_cast<volatile T*>(addr)`. `MockBus` maps the same integer addresses onto a byte array, logs every access (address, width, direction, value) and calls an optional hook. Both must satisfy the same `RegisterBus` concept.
> 5. Using only `Reg` types, write `uart_init`, `uart_send` (with poll timeout), `uart_recv` and `uart_clear_overrun` as templates over a register map, so that the same source builds against both buses.
>
> Example: `using SR = Reg<0x4000'4400, uint32_t, RO, MockBus>; SR::write(1);` does not compile. `R::modify(0xF0, 0x30)` produces exactly one read followed by exactly one write on the bus log. Reading `RDR` once per byte must not lose data.

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
# 실제 출력의 마지막 줄: 18/123 passed (exit 1) — TODO 를 채워 123/123 을 만든다
```

## 4. 힌트

<details>
<summary>힌트 1 — Reg::read 는 한 줄이다, 그런데 왜 template 키워드가 붙나?</summary>

`Bus::template read<T>(Addr)`. `Bus` 가 템플릿 매개변수에 의존하는 이름이라 `read` 가 멤버 **템플릿**인지 컴파일러가 알 수 없다. 뒤의 `<` 를 비교 연산자로 파싱하지 않게 하려고 `template` 을 적는다 ([temp.names]).

</details>

<details>
<summary>힌트 2 — "함수가 없다"는 requires 절로 만든다</summary>

클래스 템플릿의 멤버 함수는 "템플릿화된 함수(templated function)"이므로 자기 템플릿 인자가 없어도 뒤에 `requires A::can_write` 를 붙일 수 있다 ([dcl.decl]). 조건이 거짓이면 그 함수는 오버로드 후보에서 빠져서 호출이 에러가 되고, `requires { R::write(v); }` 는 **false** 가 된다. 본문의 `static_assert` 로는 이게 안 된다 (CPP-B12 Part B).

</details>

<details>
<summary>힌트 3 — modify 는 RW 일 때만, 그리고 "읽기 1번"</summary>

`requires (A::can_read && A::can_write)`. 괄호 없이 `requires A::can_read && A::can_write` 도 문법상 되지만, 복잡해지면 괄호를 쓴다. 본문은 `const T cur = read(); write(...);`. `read()` 를 표현식 안에서 두 번 쓰면 로그가 바로 알려 준다.

</details>

<details>
<summary>힌트 4 — Addr % sizeof(T) 는 어디에 쓰나?</summary>

클래스 템플릿 머리의 `requires` 절에 `&& (Addr % sizeof(T) == 0)` 로 추가한다. 앞의 `RegWord<T>` 가 거짓이면 뒤는 평가되지 않는다 — 원자 제약의 논리곱(conjunction)은 왼쪽부터 단락(short-circuit)된다 ([temp.constr.op]).

</details>

<details>
<summary>힌트 5 — RegisterBus 는 멤버 템플릿을 어떻게 검사하나?</summary>

`requires(std::uintptr_t a, std::uint32_t v) { { B::template read<std::uint32_t>(a) } -> std::same_as<std::uint32_t>; B::template write<std::uint32_t>(a, v); }`. 한 폭만 검사해도 "버스 자격"을 대표한다. 반환 타입까지 확인하려면 복합 요구사항 `{ expr } -> concept;` 을 쓴다.

</details>

<details>
<summary>힌트 6 — uart_recv 의 루프 조건</summary>

`while (n < out.size() && (Map::SR::read() & RXNE) != 0) out[n++] = Map::RDR::read();`. **버퍼 공간 검사를 앞에** 둔다. 순서가 반대면 버퍼가 찼을 때도 SR 을 한 번 더 읽고, 더 나쁘면 RDR 을 읽어 버린다. 테스트의 `count_log(RDR, read)` 가 정확히 잡는다.

</details>

## 5. 풀이

### Part A — 주소는 정수, 캐스트는 접근 순간

**첫 번째 설계 결정: NTTP 를 무엇으로 할까.** C 의 `#define` 을 C++ 로 옮기려는 사람들이 처음 시도하는 세 가지는 전부 컴파일이 안 된다 (`demo_errors.cpp`, Apple clang 21 실제 출력, `-DSHOW_ERROR=N -fsyntax-only`):

```text
# SHOW_ERROR=7 — 실제 주소를 포인터 NTTP 로
demo_errors.cpp:12:21: error: non-type template argument is not a constant expression
   12 | using Bad7 = PtrReg<reinterpret_cast<volatile std::uint32_t*>(0x40004400u)>;
demo_errors.cpp:12:21: note: reinterpret_cast is not allowed in a constant expression

# SHOW_ERROR=9 — constexpr 포인터 변수
demo_errors.cpp:25:35: error: constexpr variable 'CR_PTR' must be initialized by a constant expression
demo_errors.cpp:25:44: note: reinterpret_cast is not allowed in a constant expression

# SHOW_ERROR=8 — mock 배열의 "주소값"을 정수 NTTP 로
demo_errors.cpp:19:18: error: non-type template argument is not a constant expression
   19 | using Bad8 = Reg<reinterpret_cast<std::uintptr_t>(mock_mem), std::uint32_t, RW>;
demo_errors.cpp:19:18: note: reinterpret_cast is not allowed in a constant expression
```

규칙은 [expr.const] 하나다: 상수식 안에 `reinterpret_cast` 가 들어갈 수 없다. 정수 → 포인터는 구현 정의(implementation-defined) 변환([expr.reinterpret.cast]/5)이고, 컴파일 시간에 평가할 대상이 아니다. 같은 이유로 `read()` 에 `constexpr` 을 붙여도 소용없다 — 비템플릿 함수에 붙이면 clang 은 `constexpr function never produces a constant expression` 으로 바로 거부한다(`-Winvalid-constexpr`, 기본 에러, 실제 확인). 표준으로는 C++20 까지 "상수식이 될 수 없는 constexpr 함수"는 진단 불요 오류(ill-formed NDR, [dcl.constexpr]) 이고 P2448 이 C++23 에서 이 규칙을 없앴지만, 어쨌든 상수식으로 쓰이지 않으니 붙일 이유가 없다.

그래서 답은 **두 단계로 나누는 것**이다.

| | 컴파일 시간 | 실행 시간 |
|---|---|---|
| 주소 | `std::uintptr_t Addr` (정수 NTTP, 완전한 상수식) | `Bus` 안에서만 `reinterpret_cast` |
| 폭 | `T` (타입) | 명령 폭으로 내려감 |
| 권한 | `A` (태그 타입) | **아무것도 안 남는다** |
| 버스 | `Bus` (정책 타입) | 호출이 인라인됨 |

`Reg` 자체에는 데이터가 없다(`sizeof == 1`, `std::is_empty_v`). 드라이버는 값을 만들 필요 없이 `Map::SR::read()` 처럼 타입 이름으로 부른다. 런타임에 "어떤 레지스터인가"를 들고 다니는 바이트는 없다.

`Addr` 이 그대로 "주소값이 곧 설계 정보"가 되므로 타입을 보고 검증할 수 있다: `Reg<...>::address` 는 constexpr 이고 `static_assert` 로 맵을 검사할 수 있다(연습 5). `reinterpret_cast<uintptr_t>(ptr)` 처럼 **포인터에서 온 값은 NTTP 가 될 수 없다는 점**이 mock 설계를 결정한다: mock 배열의 주소를 `Addr` 자리에 넣을 방법이 없으므로, mock 은 "주소는 그대로 두고, 버스가 번역"할 수밖에 없다.

**두 번째 결정: 버스를 정책(policy)으로.** 버스의 일은 두 함수뿐이다. `MmioBus` 는 `*reinterpret_cast<volatile T*>(a)`. `MockBus` 는 같은 정수 주소를 `mem[a - BASE]` 로 번역한다.

```cpp
if (auto* p = slot(a, sizeof(T))) { std::memcpy(&v, p, sizeof(T)); after({a, sizeof(T), false, v}); }
```

`BASE = 0x4000'4400` 은 레지스터 맵의 기본 주소와 일부러 같게 뒀다. 그래서 `UartMap<MockBus>::SR::address == UartMap<MmioBus>::SR::address` 다 (하네스 CHECK). 레지스터 맵은 **한 벌만** 있고, 테스트용 복사본이 없다. 주소 오타는 양쪽에 똑같이 들어가니 맵 자체의 오류는 mock 이 못 잡지만, 맵이 둘이어서 생기는 어긋남은 없다.

mock 쪽 `after()` 가 로그를 쌓고 `hook` 을 부른다. hook 이 **접근 직후** 불리는 것이 중요하다: 읽기는 값을 복사한 다음에 부작용(FIFO pop)이 일어나고, 쓰기는 값을 기록한 다음에 하드웨어가 반응한다. 이 순서가 "읽으면 지워지는 레지스터"를 흉내 내는 핵심이다.

### Part B — 권한을 타입에 넣는 두 줄

```cpp
[[nodiscard]] static T read() noexcept requires A::can_read { ... }
static void write(T v) noexcept requires A::can_write { ... }
```

`can_read`/`can_write` 가 거짓이면 그 멤버는 호출 후보에서 빠진다. 에러 메시지가 어떻게 나오나 (실제 출력, clang 21 → GCC 14):

```text
# SHOW_ERROR=1 — RO 레지스터에 쓰기
demo_errors.cpp:32:12: error: invalid reference to function 'write': constraints not satisfied
   32 |     U::SR::write(0);                                  // E1: RO 레지스터에 쓰기
./reg.hpp:47:46: note: because 'RO::can_write' evaluated to false

# SHOW_ERROR=2 — WO 레지스터 읽기
demo_errors.cpp:35:19: error: invalid reference to function 'read': constraints not satisfied
./reg.hpp:46:53: note: because 'WO::can_read' evaluated to false

# SHOW_ERROR=3 — WO 레지스터에 set_bits
demo_errors.cpp:38:13: error: invalid reference to function 'set_bits': constraints not satisfied
./reg.hpp:52:50: note: because 'WO::can_read' evaluated to false

# arm-none-eabi-g++ 14.2.1, 같은 E1 (앞부분만)
demo_errors.cpp:32:17: error: no matching function for call to 'Reg<1073759232, long unsigned int, RO, MmioBus>::write(int)'
reg.hpp:47:17: note: candidate: 'static void Reg<Addr, T, A, Bus>::write(T) requires  A::can_write [with ...
reg.hpp:47:17: note: constraints not satisfied
```

메시지가 "왜"까지 말해 준다: `'RO::can_write' evaluated to false`. (Cortex-M 의 `uint32_t` 는 `unsigned long` 이다 — GCC 메시지의 `long unsigned int` 가 그 증거.)

**왜 태그 타입인가, enum 이 아니라.** `enum class Access { RO, WO, RW }` 로도 된다. 태그는 "새 권한 종류를 `Reg` 를 고치지 않고 추가한다"에서 낫다: `struct W1C { can_read=true, can_write=true, can_rmw=false; }` 를 추가하고 `Reg` 는 `requires A::can_rmw` 만 보게 하면, 이후의 RC(read-to-clear), W1S 같은 종류는 태그 하나씩만 늘리면 된다 (연습 1). 단점은 태그를 틀리게 정의할 수 있다는 것 — 그래서 `AccessPolicy` concept 으로 멤버 존재를 검사했다.

**클래스 제약 vs 함수 제약.** 두 층이 있다.

| 무엇을 막나 | 어디서 | 언제 터지나 |
|---|---|---|
| 잘못된 `T`, 비정렬 `Addr`, 나쁜 `Bus` | 클래스 `requires` | `Reg<...>` 를 **이름 짓는 순간** |
| RO 쓰기, WO 읽기, 잘못된 RMW | 멤버 `requires` | 그 멤버를 **호출하는 순간** |

클래스 쪽 실패 (실제 출력):

```text
# SHOW_ERROR=4 — 4바이트 레지스터가 비정렬 주소
demo_errors.cpp:41:5: error: constraints not satisfied for class template 'Reg' [with Addr = 1073759234, T = unsigned int, A = RW, Bus = MmioBus]
./reg.hpp:38:68: note: because '1073759234UL % sizeof(unsigned int) == 0' (2 == 0) evaluated to false

# SHOW_ERROR=5 — 레지스터 폭 타입이 아님
demo_errors.cpp:45:5: error: constraints not satisfied for class template 'Reg' [with Addr = 1073759232, T = int, A = RW, Bus = MmioBus]
./reg.hpp:38:14: note: because 'int' does not satisfy 'RegWord'
./reg.hpp:10:19: note: because 'int' does not satisfy 'unsigned_integral'

# SHOW_ERROR=6 — 버스 자격 미달
demo_errors.cpp:50:5: error: constraints not satisfied for class template 'Reg' [with Addr = 1073759232, T = unsigned int, A = RW, Bus = NoBus]
./reg.hpp:38:47: note: because 'NoBus' does not satisfy 'RegisterBus'
./reg.hpp:33:19: note: because 'B::template read<std::uint32_t>(a)' would be invalid: no member named 'read' in 'NoBus'
```

`constraints not satisfied for class template` 라서, 비정렬 주소가 데이터시트 오타라면 **맵 헤더를 빌드하는 순간** 바로 잡힌다. 하네스는 같은 조건을 `reg_ok<B + 2, uint32_t> == false` 로 증명한다. `requires { typename Reg<...>; }` 를 템플릿 안에 넣어야 하드 에러가 아니라 `false` 가 된다는 점은 CPP-B12 의 주의와 같다.

**`modify` 의 함정 두 가지.**

1. **RMW 는 읽기가 필요하다.** WO 레지스터를 "읽는" 건 하드웨어가 0이나 쓰레기를 돌려주므로 RMW 는 다른 비트를 망가뜨린다. `requires (A::can_read && A::can_write)` 가 이걸 막는다. 이 규칙이 C 헤더의 `USART1->ICR |= ...` 사고를 컴파일 에러로 바꾼다.
2. **정수 승격.** `uint8_t` 의 `~clear_mask` 는 int 로 승격되어 상위 비트가 1이 된다. 하지만 `cur` 도 승격되면서 0 확장되므로, `cur & ~clear_mask` 의 결과는 8비트 범위 안에 있다. 값이 틀어질 일은 없고, `R8::modify(0x80, 0x01)` 테스트가 이를 확인한다. 코드의 `static_cast<T>` 는 "결과를 T 로 돌린다"를 드러내려는 표시일 뿐이다 — 캐스트 없는 `(cur & ~clr) | set` 도 clang 21 과 GCC 14 의 `-Wconversion` 이 모두 경고하지 않았다 (직접 확인).

### Part C — 로그가 찾아내는 드라이버 버그

mock 의 진짜 가치는 **접근 순서와 횟수를 검사하는 것**이다. "RDR 을 두 번 읽는" 버그를 일부러 넣어 본 실제 결과 (solution 에서 `out[n++] = ...` 앞에 `(void)Map::RDR::read();` 추가):

```text
FAIL dbl.cpp:445  out[0] == 0xAA && out[1] == 0xBB
FAIL dbl.cpp:446  uart_recv<Mock>(std::span(out, 8)) == 2u && out[0] == 0xCC && out[1] == 0xDD
FAIL dbl.cpp:483  bad == 0
120/123 passed
```

`TDR` 을 `uint32_t` 로 잘못 선언하면 (실제 결과):

```text
FAIL wide.cpp:429  MockBus::log[1].width == 1
122/123 passed
```

컴파일은 통과하므로 **타입이 못 잡는 것**(주소·폭 오타는 데이터시트 지식)을 mock 로그가 잡아 준다. 타입과 mock 은 서로 다른 구멍을 메운다.

| 잡는 도구 | 버그 |
|---|---|
| 타입 (컴파일 에러) | RO 쓰기, WO 읽기, WO RMW, 비정렬, 나쁜 T |
| Mock 로그 (테스트) | RDR 이중 읽기, RMW 추가 읽기, 접근 순서, 레지스터 폭 오타, 타임아웃 없는 폴링 |
| 실제 보드만 | 레지스터 **의미** 오해(데이터시트 해석), 타이밍, 메모리 순서, 클록 게이팅 |

마지막 줄이 면접에서 중요하다: mock 이 통과해도 칩에서 안 도는 건 정상이다. mock 은 "내가 생각한 하드웨어 동작"을 인코딩한 것이고, 그 생각이 틀리면 테스트도 틀린다.

```svg
<svg viewBox="0 0 750 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">uart_recv 한 바이트 — 버스에서 보이는 것</text>
  <g stroke="currentColor">
    <rect x="20" y="42" width="170" height="44" rx="6" class="f-mut"/>
    <rect x="200" y="42" width="170" height="44" rx="6" class="f-acc"/>
    <rect x="380" y="42" width="170" height="44" rx="6" class="f-ok"/>
    <rect x="560" y="42" width="170" height="44" rx="6" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="30" y="60">SR::read()</text><text x="30" y="77">RXNE=1 ?</text>
    <text x="210" y="60">RDR::read()</text><text x="210" y="77">값을 복사</text>
    <text x="390" y="60">hook 호출</text><text x="390" y="77">rx_head++ / 다음 바이트</text>
    <text x="570" y="60">SR::read()</text><text x="570" y="77">RXNE 재평가</text>
  </g>
  <g stroke="currentColor" fill="none"><path d="M190 64 L200 64"/><path d="M370 64 L380 64"/><path d="M550 64 L560 64"/></g>
  <text x="20" y="118" fill="currentColor">로그: R SR → R RDR → R SR → R RDR → ... RDR 줄 수 = 받은 바이트 수</text>
  <rect x="20" y="136" width="710" height="62" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="32" y="158" fill="currentColor" font-family="monospace" font-size="12">if (RDR::read() != 0) buf[n++] = RDR::read();   // 흔한 버그: 2번 읽음 → 바이트 절반이 사라짐</text>
  <text x="32" y="180" fill="currentColor">volatile 이라 컴파일러가 두 읽기를 합치지 않는다 (cpp_read_twice 실측) — 버그가 그대로 칩까지 간다.</text>
  <text x="20" y="226" fill="currentColor">권한 태그는 "RDR 을 읽어도 되는가" 까지만 안다. "몇 번 읽어야 하는가"는 타입이 아니라 테스트의 몫이다.</text>
  <text x="20" y="250" fill="currentColor">그래서 mock 은 값뿐 아니라 접근 횟수까지 기록한다. 이게 스텁과 mock 의 차이.</text>
</svg>
```

**같은 소스, 진짜 MMIO 경로를 실제로 돌려 보기.** 맥에서 `0x4000'4400` 은 못 건드리니, `mmap` 으로 고정 주소에 페이지를 받아 "주변장치인 척" 시키고 진짜 `reinterpret_cast<volatile T*>` 경로로 드라이버를 돌렸다. `demo_host_mmio.cpp`:

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti demo_host_mmio.cpp -o demo_host_mmio && ./demo_host_mmio
```

```text
sent over MmioBus(mmap page at 0x700000000): 3
sent over MockBus(array):             3  (mock accesses=10)
register block bytes identical: yes
CR=0x1 BRR=0x683 TDR=0x30
```

`UartMap<MmioBus, HOST_BASE>` 와 `UartMap<MockBus>` 가 **같은 템플릿 드라이버**로 돌았고, 레지스터 블록 64바이트가 완전히 같다. (`MAP_FIXED` 없이 힌트로 줬더니 커널이 `0x7000000000` 을 돌려줘서 `MAP_FIXED` 로 고쳤다. `MAP_FIXED` 는 그 주소의 기존 매핑을 덮어쓰므로, 이 데모는 맥에서 그 영역이 비어 있다는 관찰에 기댄다.)

**"제로 코스트" 실측.** `demo_target.cpp` 를 Cortex-M4 로 컴파일. C 는 CMSIS 스타일 매크로 `*(volatile uint32_t*)(BASE + off)`. clang (`bash asm_target.sh asm -O2`, 실제 출력, 두 함수를 나란히 붙여 놓았다):

```text
00000000 <cpp_write_cr>:              0000000c <c_write_cr>:
   0: movw r1, #0x440c                  c: movw r1, #0x440c
   4: movt r1, #0x4000                 10: movt r1, #0x4000
   8: str  r0, [r1]                    14: str  r0, [r1]
   a: bx   lr                          16: bx   lr
```

```text
00000030 <cpp_set_en>:                00000044 <c_set_en>:
  30: movw r0, #0x440c                 44: movw r0, #0x440c
  34: movt r0, #0x4000                 48: movt r0, #0x4000
  38: ldr  r1, [r0]                    4c: ldr  r1, [r0]
  3a: orr  r1, r1, #0x1                4e: orr  r1, r1, #0x1
  3e: str  r1, [r0]                    52: str  r1, [r0]
  40: bx   lr                          54: bx   lr
```

`volatile` 은 두 읽기를 합치지 않는다 — `Uart1::SR::read() + Uart1::SR::read()`:

```text
000000d0 <cpp_read_twice>:
      d8: ldr r1, [r0]
      da: ldr r0, [r0]
      dc: add r0, r1
```

여러 최적화 수준에서 명령 단위로 비교했다 (`bash asm_target.sh asm -O2 | python3 cmp_asm.py`, GCC 는 VM 에서 `arm-none-eabi-objdump` 출력에 같은 스크립트를 적용):

| 컴파일러 / 옵션 | write_cr | read_sr | set_en | modify_cr | putc |
|---|---|---|---|---|---|
| clang 21, -O1 | 4=4 ✓ | 4=4 ✓ | 6=6 ✓ | 7=7 ✓ | 7=7 ✓ |
| clang 21, -O2 | 4=4 ✓ | 4=4 ✓ | 6=6 ✓ | 7=7 ✓ | 16=16 ✓ |
| clang 21, -Os | 4=4 ✓ | 4=4 ✓ | 6=6 ✓ | 7=7 ✓ | 7=7 ✓ |
| GCC 14.2, -O2 | 3=3 ✓ | 3=3 ✓ | 5=5 ✓ | 6=6 ✓ | 7=7 ✓ |
| GCC 14.2, -Os | 3=3 ✓ | 3=3 ✓ | 5=5 ✓ | 6=6 ✓ | 합쳐짐 (아래) |
| GCC 14.2, -Og | 3=3 ✓ | 3=3 ✓ | 5=5 ✓ | 6=6 ✓ | 7=7 ✓ |
| GCC 14.2, **-O0** | 다름 | 다름 | 다름 | 다름 | 다름 |

(숫자는 함수 하나당 명령 수 `cpp=c`, `✓` 는 명령이 한 줄씩 같다는 뜻. nop 패딩은 뺐다.)

GCC `-Os` 의 `putc` 칸이 흥미롭다: `c_putc` 가 아예 `b.w 78 <cpp_putc>` 한 줄이다. 두 함수 본문이 같다고 판단한 컴파일러(identical code folding 계열 최적화)가 하나로 합쳐 버렸다. "같은 명령"의 가장 강한 증거다.

**정직한 비용: `-O0`.** 제로 코스트는 인라이닝이 켜졌을 때의 이야기다.

```text
GCC 14.2, text 합계:   -O0  750 바이트 · -Og 248 바이트 · -O2 252 바이트 · -Os 212 바이트
clang 21,  text 합계:  -O0  760 바이트 · -O2 370 바이트
```

`-O0` 의 `cpp_write_cr` 는 `bl` 로 실제 호출을 한다. `nm` 을 보면 약한(weak) 인스턴스가 레지스터마다 생긴다 (clang `-O0`, 일부):

```text
00000010 W Reg<1073759232u, unsigned int, RO, MmioBus>::read()
00000018 W Reg<1073759244u, unsigned int, RW, MmioBus>::write(unsigned int)
00000020 W Reg<1073759244u, unsigned int, RW, MmioBus>::modify(unsigned int, unsigned int)
0000000c W unsigned int MmioBus::read<unsigned int>(unsigned int)
```

주소가 타입의 일부이므로 **레지스터마다 함수가 따로** 나온다 (GCC `-O0` weak 심볼 21개, `-O2` 0개). 그래서 레지스터 접근 코드는 `-Og` 이상으로 빌드해야 하고, 디버그 빌드에서 flash 가 모자라면 이 파일들에만 `-Og` 를 준다. `-Og` 는 GCC 에서 C 와 같은 명령까지 내려갔다 (위 표).

**타입에 주소가 있을 때의 진짜 대가: 인스턴스 수.** UART 3개에 같은 드라이버를 쓰면 코드가 3벌이 된다 (`demo_instances.cpp`):

| 방식 | clang -O2 text | GCC -O2 text | GCC -Os text |
|---|---|---|---|
| 런타임 베이스 주소 1벌 `tx_any(base, ...)` | 300 | 128 | 108 |
| `Reg` 맵 템플릿 3벌 | 764 | 260 | 208 |

2~2.5배. 템플릿 쪽은 상수 주소가 명령에 박혀 호출당 약간 싸지만, 함수가 인스턴스마다 복제된다 (`tx_u1`/`tx_u2`/`tx_u3` 각 0xE8 바이트, clang 은 폴링 루프를 언롤해서 더 크다). 해결책: 드라이버의 큰 부분은 비템플릿으로 빼서 `Reg` 는 얇은 진입점으로 두거나, 인스턴스가 많은 주변장치는 런타임 base 를 쓴다. 인스턴스화 비용 측정 일반은 CPP-M05 가 다룬다.

**설계 대안 표.**

| 방식 | 런타임 비용 | 테스트 교체 | 단점 |
|---|---|---|---|
| Bus 정책 템플릿 (이 레슨) | 0 (인라인) | 타입 인자 한 칸 | 드라이버가 템플릿 → 헤더 전염, 인스턴스 복제 |
| `#ifdef UNIT_TEST` 로 매크로 교체 | 0 | 빌드 옵션 | 두 소스 경로가 조건부, 전역 설정 |
| 링크 시 교체(link seam, 다른 `.o` 제공) | 함수 호출 1회 | 링크 옵션 | 인라인 불가 (LTO 없으면), 컴파일러가 볼 수 없음 |
| 가상 함수 `IBus` | 간접 호출 | 런타임 주입 | vtable, flash·cycle 비용 (CPP-M06) |
| 런타임 base 포인터 struct | 약간 | 포인터 교체 | 권한을 타입으로 못 막음 (CMSIS 처럼), 주소 정렬은 수동 |

**`MockBus` 가 static 인 대가.** 버스의 멤버가 전부 static 이라 `Bus::read` 로 호출할 수 있고 객체를 들고 다니지 않는다 (제로 사이즈). 대신 mock 상태가 전역이다. 그래서 테스트마다 `reset()` 해야 하고, 두 mock 을 분리하려면 `MockBusT<Id>` 처럼 태그로 타입을 나눠야 한다 (`practice.cpp` 끝부분). 인스턴스 버스(`Bus&` 를 들고 있는 드라이버)는 더 유연하지만 포인터 하나를 저장·전달하는 비용이 생긴다. Cortex-M 에서는 드라이버가 몇 개 안 되니 static 쪽이 보통 이득이다.

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution   # 123/123 passed
c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer solution.cpp -o sol_asan && ./sol_asan   # 123/123, 깨끗함

# 컴파일 실패 데모 (N=1..9)
c++ -std=c++20 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -DSHOW_ERROR=1 -fsyntax-only demo_errors.cpp
# 진짜 MMIO 경로를 호스트 mmap 위에서
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti demo_host_mmio.cpp -o demo_host_mmio && ./demo_host_mmio
# Cortex-M4: 맥 clang (ARM_INC 기본값 /tmp/arm-none-eabi-include, 없으면 VM 에서 복사)
bash asm_target.sh asm -O2 | python3 cmp_asm.py
bash asm_target.sh size -O0
bash asm_target.sh instances
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `template <volatile uint32_t* P> struct Reg` | 실제 주소를 NTTP 로 줄 수 없다. `reinterpret_cast` 는 상수식 불가 (E7) | `template <std::uintptr_t Addr, ...>` + 접근 시 캐스트 |
| `constexpr volatile uint32_t* p = reinterpret_cast<...>(0x4000'0000)` | 같은 이유 (E9) | `constexpr std::uintptr_t` 로 저장, 쓸 때 캐스트 |
| mock 배열의 주소를 `Addr` 로 `Reg<(uintptr_t)mem, ...>` | 포인터→정수 변환도 상수식 불가 (E8) | 주소는 그대로, 버스가 `addr - BASE` 로 번역 |
| `static void write(T v) { static_assert(A::can_write); ... }` | 호출해야 터진다. `requires` 로 검사하면 `true` 로 나와 테스트가 거짓 통과 (CPP-B12) | 멤버에 `requires A::can_write` |
| `operator=` 로 쓰기, 변환 연산자로 읽기 | `r = 1;` 이 MMIO 쓰기라는 게 안 보이고, `if (r)` 같은 곳에서 읽기가 몰래 일어난다 | 명시적 `R::write(1)` |
| WO 에 `modify` 를 허용 | "읽은 값"이 쓰레기라 다른 비트를 망친다 | `requires (A::can_read && A::can_write)` |
| `Map::SR::read()` 를 드라이버 안에서 두 번 쓴다 | volatile 이라 합쳐지지 않아 레지스터를 2번 읽는다. 두 값이 다를 수도 있다 | `const auto sr = Map::SR::read();` 를 먼저 받고 비트 검사 |
| `MockBus` 에서 바이트 배열을 `uint32_t*` 로 읽기 | strict aliasing 위반 UB (CPP-B12) | `std::memcpy` |
| Mock 에 값만 저장, 접근 횟수는 안 셈 | 이중 읽기·순서 버그를 못 잡는다 | 로그에 (주소, 폭, 방향, 값) |
| 맵을 `#ifdef UNIT_TEST` 로 두 벌 유지 | 두 맵이 어긋나도 아무도 모른다 | 맵은 한 벌, `Bus` 만 교체 |
| 디버그 빌드 `-O0` 그대로 | 레지스터마다 함수가 생기고 호출이 남는다 (GCC 750B vs 252B) | 드라이버는 `-Og` |

## 8. 면접 Q&A

<details>
<summary>Q1. 레지스터 주소를 왜 포인터가 아니라 정수 템플릿 인자로 받나요?</summary>

실제 주소에서 포인터를 만드는 유일한 방법이 `reinterpret_cast` 인데, 이것은 상수식에 쓸 수 없다([expr.const]). 그래서 `template <volatile uint32_t* P>` 나 `constexpr volatile uint32_t* p = ...` 는 컴파일되지 않는다 (실측 E7, E9). 정수 NTTP 는 완전한 상수식이라 `Addr % sizeof(T) == 0` 같은 컴파일 타임 검사도 할 수 있다. 캐스트는 접근하는 순간 버스 안에서 런타임에 한다 — 그러나 최적화 후 명령은 `*(volatile uint32_t*)0x4000440C` 와 같다 (실측). 정수→포인터 변환이 구현 정의([expr.reinterpret.cast]/5)인 점은 CMSIS 도 똑같이 기대고 있는 부분이다.

</details>

<details>
<summary>Q2. CMSIS 의 __IM / __OM 과 뭐가 다른가요?</summary>

CMSIS 는 struct 멤버에 `__IM`(`volatile const`), `__OM`(`volatile`), `__IOM`(`volatile`) 을 붙인다. `__IM` 은 const 라서 RO 쓰기는 컴파일 에러가 나지만, **`__OM` 은 const 가 아니므로 WO 읽기와 WO `|=` 는 그대로 통과한다.** `Reg` 는 `requires` 로 세 경우 모두를 막는다. 또 `Reg` 는 비정렬·폭·버스를 타입에서 검사하고, 버스 교체로 테스트가 가능하다. 대가: 드라이버를 템플릿으로 써야 하고 에러 메시지가 복잡하며, `-O0` 에서 코드가 커진다. CMSIS 구조체는 그런 비용이 없고 벤더 헤더를 그대로 쓰는 장점이 있다. 수작업 오타를 줄이려면 벤더 SVD 파일에서 `Reg` 맵을 코드 생성하는 절충이 있다. 덧붙여 STM32 디바이스 헤더는 대부분의 레지스터를 `__IO`(= `volatile`) 로만 선언해서 RO 쓰기조차 막지 못한다 (기억에 의존한 설명, 이 레슨에서 헤더를 직접 확인하지는 않았다 — 미확인).

</details>

<details>
<summary>Q3. static_assert 와 requires, 권한 제약에서는 어느 쪽을 쓰죠?</summary>

`requires`. 멤버 `requires` 는 "함수가 없다"를 뜻하므로 `requires { R::write(v); }` 로 **테스트할 수 있고**, 오버로드에서 빠지며, 메시지가 `'RO::can_write' evaluated to false` 로 원인을 짚는다. 본문 `static_assert` 는 호출이 인스턴스화될 때까지 평가되지 않아 `requires` 검사에서 `true` 가 나온다 (CPP-B12). 단, 사람이 읽을 긴 설명이 필요하면 static_assert 메시지가 낫다. 트레일링 `requires` 는 템플릿화된 함수에만 허용된다 — 클래스 템플릿의 멤버는 자기 템플릿 인자가 없어도 해당된다.

</details>

<details>
<summary>Q4. 하드웨어 없이 드라이버를 어떻게 테스트하나요? Mock 이 통과하면 끝인가요?</summary>

Bus 를 템플릿 인자로 빼서 `Map<MockBus>` 로 컴파일하고, mock 이 접근 로그를 남기게 하면 "읽기 1번 + 쓰기 1번", "RDR 은 바이트당 1번", "ICR 은 읽지 않음" 같은 순서·횟수 계약을 검증할 수 있다. hook 으로 FIFO, busy, 플래그 클리어를 흉내 낸다. 끝이 아니다: mock 은 **내가 이해한 데이터시트**를 코드로 옮긴 것이어서, 그 이해가 틀리면 테스트도 틀린다. 또 타이밍, 메모리 순서(DMA·barrier), 클록/파워 게이팅, 실제 fault 동작은 mock 이 못 본다. 그래서 mock 은 "제어 흐름과 레지스터 사용"을 고정하고, HIL 테스트와 실보드 검증이 나머지를 맡는다.

</details>

<details>
<summary>Q5. 제로 코스트라고 하셨는데, 언제 아닌가요?</summary>

실측 기준으로, clang 21 의 `-O1`/`-O2`/`-Os` 와 GCC 14 의 `-O2`/`-Os`/`-Og` 에서는 C 스타일과 같은 명령이 나왔다 (GCC `-O1` 은 돌려 보지 않았다). `-O0` 에서는 `Reg::write` → `MmioBus::write` 호출이 그대로 남아 GCC 기준 text 750B vs `-O2` 252B 이고, 약한 심볼이 레지스터마다 생긴다. 그리고 "레지스터 접근 자체"는 공짜여도 **드라이버 템플릿 인스턴스**는 공짜가 아니다: UART 3개면 드라이버가 3벌이다 (GCC -O2 260B vs 런타임 base 128B, 실측). 큰 드라이버는 비템플릿 공통부 + 얇은 템플릿 진입점으로 쪼갠다.

</details>

<details>
<summary>Q6. UART 가 여러 개인 칩에서는 어떻게 설계하죠?</summary>

두 가지. 주소를 타입에 넣으면 인스턴스별로 코드가 복제되지만 호출 시 상수가 박혀 싸다 (`UartMap<MmioBus, 0x4000'4400>`, `0x4000'4800` ...). 런타임 base 포인터는 코드 1벌이지만 권한을 타입으로 강제하지 못한다. 절충: 드라이버의 큰 부분(버퍼, 상태머신)을 비템플릿 클래스로 두고, 바이트 입출력만 얇은 함수 포인터나 템플릿으로 묶는다 (CPP-M12 Delegate 와 연결). 인스턴스가 2개뿐이면 복제를 그냥 받아들이는 경우가 많다. 측정해서 결정하라: `arm-none-eabi-size` 로 비교한다.

</details>

<details>
<summary>Q7. volatile 을 붙였으니 ISR 과 공유해도 안전한가요?</summary>

아니다. `volatile` 은 "이 접근은 생략·합치기 불가"([intro.abstract]) 뿐이다. `modify` 의 RMW 는 LDR-OR-STR 3명령이라 그 사이에 ISR 이 같은 레지스터를 바꾸면 lost update 가 일어난다 — 멀티비트 필드도 마찬가지다. 해결: 그런 레지스터는 set/clear 전용 레지스터(BSRR, ICR)를 쓰거나 critical section (CPP-B06, CPP-A04). `Reg` 타입은 여기서 도와주지 않는다 — 권한은 타입에 있어도 **원자성은 타입에 없다.**

</details>

## 9. 추가 연습

### 연습 1 — W1C: 읽기·쓰기는 되지만 RMW 는 금지

W1C(write-1-to-clear) 플래그 레지스터는 `read()` 와 `write(mask)` 가 둘 다 있지만, `|=` 하면 **이미 서 있던 다른 플래그까지 지워진다** (RMW 해저드의 상세는 CPP-M02). 태그에 `can_rmw` 를 추가해 W1C 에서 `modify` 를 막아라.

<details>
<summary>답 보기</summary>

```cpp
struct RW  { static constexpr bool can_read = true, can_write = true, can_rmw = true;  };
struct W1C { static constexpr bool can_read = true, can_write = true, can_rmw = false; };
// Reg: static void modify(T c, T s) noexcept requires A::can_rmw { ... }
```

`requires A::can_read && A::can_write` 대신 태그의 `can_rmw` 를 보게 바꾸면, 앞으로 RC(read-to-clear) 같은 종류도 태그만 추가하면 된다.

</details>

### 연습 2 — 권한 낮추기

디버그 모듈에는 `Ctrl` 레지스터를 읽기만 허용하고 싶다. `ReadOnly<R>` 별칭 템플릿을 만들어라.

<details>
<summary>답 보기</summary>

`template <typename R> using ReadOnly = Reg<R::address, typename R::value_type, RO, typename R::bus>;`. 주소·폭·버스는 그대로, 권한만 RO. 다른 타입이므로 그 타입을 받는 함수는 쓰기를 호출할 수 없다 — C 의 `const` 포인터로 권한을 낮추는 것과 같은 아이디어를 타입 수준에서 한 것이다.

</details>

### 연습 3 — 등간격 레지스터 배열

DMA 채널처럼 레지스터가 `Stride` 간격으로 N개 반복된다. `RegArray<Base, Stride, N, T, A, Bus>::at<I>` 를 만들고 `I >= N` 은 이름조차 못 짓게 하라.

<details>
<summary>답 보기</summary>

```cpp
template <std::size_t I> requires (I < N)
using at = Reg<Base + I * Stride, T, A, Bus>;
```

별칭 템플릿도 `requires` 절을 가질 수 있다. 하네스의 `has_index<Ch, 4>` 가 false 다. `I` 가 런타임 값이어야 하면 이 방식은 맞지 않는다 — 그때는 런타임 base 를 쓰거나 인덱스 범위를 `assert` 로 검사한다.

</details>

### 연습 4 — 리셋값 스윕 (실리콘 검증)

칩 bring-up 에서 "리셋 직후 레지스터들이 데이터시트 값인가"를 검사한다. 단, **WO 레지스터를 읽으면 안 된다.** `count_mismatches<Expect<R, V>...>()` 를 fold expression 과 `if constexpr` 로 만들어라.

<details>
<summary>답 보기</summary>

```cpp
template <typename E> unsigned mismatch_one() {
    if constexpr (E::reg::access::can_read) return E::reg::read() != E::value ? 1u : 0u;
    else return 0u;
}
template <typename... Es> unsigned count_mismatches() { return (0u + ... + mismatch_one<Es>()); }
```

`if constexpr` 의 버려진 가지는 인스턴스화되지 않는다([stmt.if]) — WO 에서 `read()` 를 부르는 코드 자체가 생기지 않아 컴파일 에러도, 런타임 읽기도 없다. 하네스는 `Bus0::reads == 2` 로 Tx 를 건드리지 않았음을 확인한다.

</details>

### 연습 5 — 맵 겹침 검사

레지스터 정의 실수를 잡기 위해, `Reg` 타입 팩을 받아 주소 범위가 겹치면 `false` 를 내는 `consteval disjoint<Rs...>()` 를 만들어라. `static_assert` 로 맵에 적용한다.

<details>
<summary>답 보기</summary>

`std::array{Span{Rs::address, Rs::address + Rs::width}...}` 로 펼친 뒤 이중 루프 `lo1 < hi2 && lo2 < hi1`. `Addr` 가 정수 NTTP 이기 때문에 상수식에서 다룰 수 있다 — 포인터 NTTP 였다면 불가능했다. `practice.cpp` 의 `disjoint<Byte0, Half>` 처럼 u8 과 u16 이 겹치는 것을 잡는다.

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice   # 20/20 passed
```

## 10. 핵심 정리

- 주소는 **정수 NTTP**: `reinterpret_cast` 는 상수식 불가 → 포인터 NTTP / `constexpr` 포인터는 안 되고, 캐스트는 접근 순간 버스 안에서.
- `Reg<Addr, T, Access, Bus>`: 빈 타입 + static 함수. 권한은 태그 타입 + `requires` → 잘못된 접근은 "함수 없음" 컴파일 에러 (RO 쓰기, WO 읽기, WO RMW).
- 두 층의 제약: 클래스(`T`, 정렬, 버스)는 이름 지을 때, 멤버(권한)는 호출할 때. `requires` 로 검사 가능 (static_assert 는 불가).
- 버스 정책: `MmioBus`(volatile 캐스트) / `MockBus`(주소 → 배열 번역 + 로그 + hook). 맵은 한 벌, 주소도 같다.
- Mock 은 값뿐 아니라 **접근 순서·횟수**를 검증한다. 타입이 못 잡는 RDR 이중 읽기를 로그가 잡는다. 하지만 하드웨어 이해가 틀리면 테스트도 틀린다.
- 실측: clang -O1/-O2/-Os, GCC -O2/-Os/-Og 에서 C 와 명령이 같다. `-O0` 는 호출이 남고 레지스터마다 weak 심볼 (GCC 750B vs 252B). UART N개 = 드라이버 N벌 (260B vs 128B).
- 권한은 타입에 있어도 **원자성은 없다**: `modify` 는 여전히 LDR-OR-STR. ISR 공유 시 set/clear 레지스터나 임계 구역.

## 참고자료

- C++20 표준: [expr.const] (상수식에서 금지된 것: `reinterpret_cast`), [expr.reinterpret.cast]/5 (정수→포인터), [temp.arg.nontype], [temp.names] (`template` 키워드), [dcl.decl] (trailing requires-clause), [temp.constr.op] (conjunction 단락), [dcl.constexpr] (NDR 규칙), [stmt.if] (`if constexpr`), [intro.abstract] (volatile 관찰 가능 동작)
- P2448R2 — Relaxing some constexpr restrictions (C++23)
- [cppreference — Constraints and concepts](https://en.cppreference.com/w/cpp/language/constraints) · [Requires expression](https://en.cppreference.com/w/cpp/language/requires) · [Template non-type arguments](https://en.cppreference.com/w/cpp/language/template_parameters)
- [CMSIS-Core: `__IM` / `__OM` / `__IOM` (core_cm4.h)](https://arm-software.github.io/CMSIS_5/Core/html/index.html)
- [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) (arm-none-eabi-gcc), STM32 RM (ISR/ICR/RDR/TDR 레지스터 분리 — 이 레슨의 가짜 UART 는 이를 단순화했다)
- CPP-B04 (constexpr 주소), CPP-B05 (volatile), CPP-B12 (함수 템플릿·타입 규칙), CPP-M02 (Field RMW·W1C), CPP-M05 (템플릿 인스턴스 비용)
