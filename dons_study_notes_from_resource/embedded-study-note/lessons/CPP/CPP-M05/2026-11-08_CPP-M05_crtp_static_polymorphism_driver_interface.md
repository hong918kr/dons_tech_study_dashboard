---
id: CPP-M05
time: 80분
updated: 2026-11-08
---
# CPP-M05 CRTP 정적 다형성 드라이버 인터페이스

## 학습 목표

- CRTP(Curiously Recurring Template Pattern)로 `virtual` 없는 드라이버 인터페이스 `UartBase<Derived>` 를 만들고, 공통 코드(`write`, `print`)를 기반 클래스에 한 번만 쓴다.
- `class B : UartBase<A>` 복붙 실수를 **private 생성자 + `friend Derived`** 로 컴파일 에러로 만들고, `protected` 만으로는 왜 못 막는지, C++17 의 aggregate 구멍이 무엇인지 설명한다.
- 구현 함수를 빠뜨렸을 때의 에러 품질을 세 방식(가드 없음 / `static_assert` / `requires` 절)으로 비교하고, 공개 API 와 구현의 이름을 왜 다르게 해야 하는지(무한 재귀) 설명한다.
- 같은 API 의 `virtual` 버전과 Cortex-M4 어셈블리를 비교해 **인라인 여부**를 실제 출력으로 확인하고, 타입 수에 따른 코드 크기 증가(template bloat)를 측정한다.
- "CRTP 는 이질 컨테이너가 안 된다"는 말의 정확한 의미를 알고, 우회 방법 3가지(타입 지우기 / `std::variant` / virtual 어댑터)를 고른다.

## 1. 문제

펌웨어 면접에서 "`virtual` 없이 드라이버 인터페이스를 어떻게 만드나요, 그 대가는 뭔가요?"는 단골 질문이다. CRTP 는 답의 절반이고, 나머지 절반은 **코드 크기와 이질 컨테이너**라는 대가를 숫자로 말하는 것이다.

CRTP 는 "기반 클래스의 템플릿 인자로 파생 클래스 자신을 넘기는" 패턴이다. 기반 클래스가 파생 타입을 알고 있으므로, 어떤 함수를 부를지가 **컴파일 타임**에 정해진다. C 로 치면 `uart_put` 을 함수 포인터 표로 호출하는 대신, 빌드할 때 어느 함수인지 박아 넣는 것이다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">같은 u.put(b) — 호출이 결정되는 시점</text>
  <rect x="10" y="40" width="350" height="255" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="24" y="64" fill="currentColor" font-weight="bold">CRTP (정적 다형성)</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="24" y="90">UartBase&lt;Stm32Uart&gt;::put(b)</text>
    <text x="24" y="110">  → static_cast&lt;Derived&amp;&gt;(*this)</text>
    <text x="24" y="130">  → Stm32Uart::put_impl(b)</text>
    <text x="24" y="170">컴파일러가 전부 본다 → 인라인</text>
    <text x="24" y="196">ldr  r0, [r2]      @ SR 폴링</text>
    <text x="24" y="216">lsls r0, r0, #24</text>
    <text x="24" y="236">bpl  .LBB0_1</text>
    <text x="24" y="256">str  r1, [r2, #4]  @ DR 쓰기</text>
    <text x="24" y="282">함수 호출이 사라진다 (실제 -Os 출력)</text>
  </g>
  <rect x="380" y="40" width="350" height="255" rx="8" class="f-warn" stroke="currentColor"/>
  <text x="394" y="64" fill="currentColor" font-weight="bold">virtual (동적 다형성)</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="394" y="90">IUart* → vptr → vtable[0] → put</text>
    <text x="394" y="130">컴파일러는 어느 구현인지 모른다</text>
    <text x="394" y="170">ldr  r2, [r0]      @ vptr 읽기</text>
    <text x="394" y="190">ldr  r2, [r2]      @ 슬롯 읽기</text>
    <text x="394" y="210">bx   r2            @ 간접 분기</text>
    <text x="394" y="256">폴링 코드는 구현 함수 안에 1벌만</text>
    <text x="394" y="282">호출마다 간접 분기 (실제 -Os 출력)</text>
  </g>
  <text x="10" y="318" fill="currentColor">clang 21, thumbv7em, -Os. 대가: CRTP 는 타입마다 코드가 복제될 수 있고, 한 배열에 담을 수 없다.</text>
</svg>
```

실제 칩 없이 테스트하려고, 이 레슨의 `hw::` 는 USART 2개를 흉내 낸다. 레지스터 배치는 STM32F4(`SR` 0x00, `DR` 0x04, `BRR` 0x08, `CR1` 0x0C)와 같고, `DR` 에 쓴 바이트는 `tx[]`(= 선)에 쌓이며, 그 뒤 `busy_polls` 번은 `TXE` 가 0 이다. Cortex-M 에서의 진짜 어셈블리는 별도의 `demo_inline.cpp` 로 본다.

### Part A — CRTP 인터페이스와 구현 2개

- `template <class Derived> class UartBase` 의 공개 API: `init(baud)`, `put(b)`, `try_get(out)`, 그리고 공통 코드 `write(span)`, `print(cstr)`.
- 파생 클래스가 구현할 것: `bool init_impl(u32)`, `bool put_impl(u8)`, `bool get_impl(u8&)`. 구현 함수는 **private** 이고 `friend UartBase<자신>` 으로 기반에만 연다.
- `MockUart`: 버퍼에 쌓는 테스트용 구현. `Stm32Uart<Base>`: 포트 주소가 타입에 박힌 구현. `init_impl` 은 `BRR = round(PCLK / baud)` 후 `CR1 = UE|TE|RE`, `put_impl` 은 `TXE` 를 최대 8번 폴링.

### Part B — 잘못 쓰면 컴파일이 안 되게

- `struct WrongDerived : UartBase<MockUart> {}` (이름 복붙 실수) 를 생성 불가로 만든다. `UartBase<MockUart>` 를 직접 만드는 것도 막는다.
- `implements_interface()` : `Derived` 가 `*_impl` 3개를 **시그니처대로** 가졌는지 `requires` 식으로 검사. `put_impl` 이 없는 타입, 반환 타입이 `void` 인 타입은 `false`.
- 그 결과를 `self()` 안 `static_assert` 로 연결해 사람이 읽을 수 있는 에러를 낸다.

### Part C — 펌웨어 시나리오: 프레임 전송과 다중 포트 로깅

- `send_frame(UartBase<D>&, payload)`: `0x02, LEN, payload..., XOR` 형식. 255 초과면 한 바이트도 안 보낸다. Mock 과 Stm32 에서 선 위의 바이트가 같아야 한다(고정 시드 무작위 200회).
- `UartBase<MockUart>` 와 `UartBase<Usart1>` 는 **서로 관계없는 타입**이라 한 배열에 담을 수 없다. 손으로 만든 타입 지우기 `AnyUart` (`void*` + 함수 포인터) 로 우회하고, `broadcast()` 로 여러 포트에 보낸다. 한 포트가 고장 나도 나머지는 계속 받아야 한다.

## 2. Problem (English)

> **Static-polymorphic UART driver interface using CRTP.**
>
> 1. Implement `template <class Derived> class UartBase` exposing `bool init(u32 baud)`, `bool put(u8)`, `bool try_get(u8&)`, `size_t write(std::span<const u8>)` and `size_t print(const char*)`. Dispatch to private `init_impl` / `put_impl` / `get_impl` in `Derived` without using `virtual`. Implement `MockUart` and `template <uintptr_t Base> class Stm32Uart`. `write` stops at the first failed byte and returns the count sent.
> 2. Make misuse fail at compile time: `struct Wrong : UartBase<MockUart> {}` must not be constructible, and `UartBase<MockUart>` must not be constructible directly. Provide `static consteval bool implements_interface()` that checks all three `*_impl` signatures, including private ones.
> 3. Implement `template <class D> bool send_frame(UartBase<D>&, std::span<const u8> payload)` with the wire format `0x02, LEN, payload..., XOR(LEN, payload...)`. A payload larger than 255 bytes sends nothing and returns `false`.
> 4. Implement `AnyUart`, a two-pointer non-owning type-erased handle, `make_any(UartBase<D>&)`, and `size_t broadcast(std::span<const AnyUart>, std::span<const u8>)` returning the number of ports that accepted all data. A failing port must not stop the others.
> 5. Compare with an equivalent `virtual` interface on Cortex-M: was the call inlined, and how does code size scale with the number of distinct port types?
>
> Example: `send_frame(uart, {0x10, 0x20})` puts `02 02 10 20 32` on the wire. No heap, no RTTI, no exceptions.

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 깨짐 대비
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
# 처음엔 "27/73 passed", exit 1 — TODO 를 채워 73/73 을 만든다
```

## 4. 힌트

<details>
<summary>힌트 1 — self() 한 줄이 CRTP 의 전부다</summary>

`Derived& self() { return static_cast<Derived&>(*this); }` — 기반 → 파생 `static_cast`. `*this` 가 진짜로 `Derived` 객체의 일부일 때만 올바르다. 그래서 Part B 에서 그 전제를 강제한다.

</details>

<details>
<summary>힌트 2 — 멤버 함수 본문은 언제 컴파일되나</summary>

`class MockUart : public UartBase<MockUart>` 를 만나는 순간 `UartBase<MockUart>` **클래스**가 인스턴스화되는데 이때 `MockUart` 는 아직 불완전하다. 하지만 **멤버 함수 본문**은 그 함수가 쓰일 때 인스턴스화되므로, 본문 안에서는 `Derived` 의 멤버를 써도 된다.

</details>

<details>
<summary>힌트 3 — 복붙 실수를 막는 생성자</summary>

생성자를 `private` 으로 두고 `friend Derived;` 를 선언한다. `struct WrongDerived : UartBase<MockUart>` 의 암시적 생성자는 `UartBase<MockUart>` 의 private 생성자를 부를 수 없다 — friend 인 것은 `MockUart` 뿐이다.

</details>

<details>
<summary>힌트 4 — requires 식은 어디에 쓰나</summary>

`implements_interface()` 를 `UartBase` 의 **멤버**로 두면 그 안의 `requires` 식은 `UartBase<D>` 의 접근 권한으로 평가된다. `D` 가 `friend UartBase<D>` 를 선언했으면 private `put_impl` 도 보인다. 네임스페이스 범위 `concept` 로 쓰면 private 이 안 보인다.

</details>

<details>
<summary>힌트 5 — make_any 의 람다</summary>

`[](void* p, u8 b) { return static_cast<UartBase<D>*>(p)->put(b); }` — 캡처가 없으므로 함수 포인터로 변환된다. `D` 마다 다른 함수가 생기고, `void*` 에 넣을 때와 꺼낼 때의 타입(`UartBase<D>*`)을 맞춰야 한다.

</details>

## 5. 풀이

### Part A — 패턴의 동작 순서

```cpp
template <class Derived>
class UartBase {
public:
    bool put(u8 b) { return self().put_impl(b); }
    std::size_t write(std::span<const u8> data) { /* put 만 쓴다: 공통 코드 */ }
private:
    Derived& self() { return static_cast<Derived&>(*this); }
};
class MockUart : public UartBase<MockUart> { /* put_impl */ };
```

**"불완전한 타입인데 왜 되나?"** 순서는 이렇다.

| 시점 | 무슨 일이 일어나나 | `Derived` 상태 |
|---|---|---|
| `class MockUart : public UartBase<MockUart>` 의 기반 절 | `UartBase<MockUart>` 클래스 정의 인스턴스화 | **불완전** — `sizeof(Derived)`, 멤버 접근 불가 |
| `MockUart` 의 닫는 `}` | 클래스 완성 | 완전 |
| `m.put(b)` 를 쓰는 함수 | `UartBase<MockUart>::put` 본문 인스턴스화 ([temp.point]) | 완전 — `self().put_impl` 가능 |

불완전한 시점에 파생 타입을 쓰면 실제로 에러가 난다. (`-DSHOW_ERROR=6`)

```text
demo_errors.cpp:72:49: error: invalid application of 'sizeof' to an incomplete type 'E6'
demo_errors.cpp:73:13: note: in instantiation of template class 'Early<E6>' requested here
   73 | struct E6 : Early<E6> {};
demo_errors.cpp:73:8: note: definition of 'E6' is not complete until the closing '}'
```

그래서 "파생이 인터페이스를 지켰나" 검사를 클래스 본문의 `static_assert` 에 쓸 수 없고, **멤버 함수 본문**(여기서는 `self()`)에 둔다. 클래스 본문에 둔 검사는 항상 불완전 타입을 만나서 실패한다.

`hello_through_base(UartBase<D>&)` 에 `MockUart` 를 넘기면 `D` 가 왜 추론되나? 템플릿 인자 추론은 원칙적으로 변환을 허용하지 않지만, 매개변수가 `simple-template-id` 형태의 클래스일 때 **파생 → 기반**만큼은 허용한다 ([temp.deduct.call]). 이 규칙 덕분에 CRTP 를 함수 인자로 받을 수 있다.

**빈 기반 클래스(EBO).** `UartBase` 에는 데이터 멤버가 없으므로 `sizeof(Usart1) == 1` 이다 (하네스가 확인, CPP-B08 참고). vptr 이 없으니 객체 크기도 안 늘어난다. `Stm32Uart` 는 포트 주소가 타입에 들어 있어서 객체에 저장할 것이 없다.

### Part B — 세 겹의 방어

**방어 1: 잘못된 기반을 막는다.** 왜 막아야 하는가? 가드가 없으면 `struct B : Naive<A> {}` 는 조용히 컴파일되고, `static_cast<A&>(*this)` 는 `B` 객체가 `A` 가 아니므로 **UB** 다 ([expr.static.cast]). `demo_wrong_base_ub.cpp` 를 ASan 으로 돌린 실제 결과:

```text
$ c++ -std=c++20 -g -O0 -fsanitize=address -fno-omit-frame-pointer demo_wrong_base_ub.cpp -o /tmp/ub && /tmp/ub
==37109==ERROR: AddressSanitizer: stack-buffer-overflow on address 0x00016b75e8e8 at pc 0x0001046a0a78 ...
READ of size 8 at 0x00016b75e8e8 thread T0
    #0 0x0001046a0a74 in Naive<A>::hit() demo_wrong_base_ub.cpp:8
    #1 0x0001046a095c in main demo_wrong_base_ub.cpp:16
  This frame has 1 object(s):
    [32, 33) 'b' (line 14) <== Memory access at offset 40 overflows this variable
```

`A::count_` 는 오프셋 8 이고 `B` 객체는 1바이트다. 펌웨어라면 ASan 이 없으니 옆 변수가 조용히 깨진다.

고치는 방법과 각각의 한계:

| 방법 | `class B : UartBase<A>` 를 막나 | 실제 확인 |
|---|---|---|
| `public` 생성자 | 못 막음 | 시작 코드 상태 |
| `protected` 생성자 | **못 막음** — 파생 클래스면 누구나 부를 수 있다 | `demo_errors.cpp` 의 `WrongProt w;` 컴파일 성공 |
| `private` + `friend Derived` | 막음 | 아래 에러 |

```text
$ c++ -std=c++20 -fsyntax-only -DSHOW_ERROR=1 demo_errors.cpp
demo_errors.cpp:60:19: error: call to implicitly-deleted default constructor of 'Wrong'
   60 | void e1() { Wrong w; (void)w; }     // 잘못된 기반 → 생성 불가
demo_errors.cpp:30:16: note: default constructor of 'Wrong' is implicitly deleted because base class 'Guarded<Good>' has an inaccessible default constructor
```

`friend Derived;` 는 템플릿 타입 인자를 friend 로 지정하는 C++11 문법이다 ([class.friend]).

**C++17 구멍.** `UartBase() = default;` 는 C++17 에서는 user-provided 가 아니므로 `UartBase` 가 aggregate 일 수 있고, `Wrong w{};` 는 생성자를 부르지 않는 **aggregate 초기화**가 된다. C++20 은 P1008 로 "user-declared 생성자가 있으면 aggregate 아님"으로 바꿔서 이 구멍을 막았다. 직접 돌려 본 결과:

```text
$ c++ -std=c++17 -fsyntax-only -DSHOW_CXX17_HOLE demo_errors.cpp ; echo rc=$?
rc=0                                       # C++17: Wrong w{} 가 컴파일된다
$ c++ -std=c++20 -fsyntax-only -DSHOW_CXX17_HOLE demo_errors.cpp
demo_errors.cpp:57:23: error: base class 'Guarded<Good>' has private default constructor
```

C++17 코드베이스라면 `UartBase() {}` 처럼 user-provided 로 쓰면 aggregate 가 아니게 되어 구멍이 닫힌다 (이 줄은 표준 규칙에서 추론한 것이고, 이 레슨에서 별도로 돌리지는 않았다).

**방어 2: 구현 누락을 잡는다.** 세 방식의 실제 에러를 비교한다.

가드 없음 (`-DSHOW_ERROR=2`) — 호출 체인을 거슬러 올라가야 한다:

```text
demo_errors.cpp:25:53: error: no member named 'put_impl' in 'NoImplN'
demo_errors.cpp:26:30: note: in instantiation of member function 'Naive<NoImplN>::put' requested here
demo_errors.cpp:63:33: note: in instantiation of member function 'Naive<NoImplN>::send' requested here
```

`static_assert` 가드 (`-DSHOW_ERROR=3`) — **메시지는 생기지만 원래 에러도 그대로 나온다**:

```text
demo_errors.cpp:11:36: error: no member named 'put_impl' in 'NoImplG'
demo_errors.cpp:17:23: error: static assertion failed due to requirement 'requires (NoImplG &d, unsigned char b) { { <<error-expression>> }; }': Guarded<D>: D must define bool put_impl(u8)
2 errors generated.
```

`requires` 절 (`-DSHOW_ERROR=5`) — **호출 지점에서 에러 1개**:

```text
demo_errors.cpp:76:33: error: invalid reference to function 'put': constraints not satisfied
   76 | bool e5() { NoImplC x; return x.put(1); }    // requires 절 — 호출 지점에서 1개 에러
demo_errors.cpp:36:50: note: because 'd.put_impl(b)' would be invalid: no member named 'put_impl' in 'NoImplC'
1 error generated.
```

여기서 정직하게 말하면, 본문 해답(`self()` 안 `static_assert`)은 에러가 2개 나온다. `requires` 절이 에러 품질은 더 좋다. 대신 모든 공개 함수에 절을 달아야 한다. 해답에서 `static_assert` 를 쓴 이유는 **검사 코드가 한 곳(`self()`)** 에 모이고 `implements_interface()` 와 같은 조건을 재사용하기 때문이다. 공개 API 가 적으면 `requires` 절도 좋은 선택이다 (자세한 concepts 는 MCPP-M05 참고). 또한 이 demo 의 `PrivOk` 가 컴파일되므로, `requires` 절 안에서도 friend 권한으로 private `put_impl` 이 보인다 (Apple clang 21 에서 확인).

**방어 3: 이름을 다르게.** 공개 API 와 구현의 이름이 같으면 훨씬 나쁜 일이 생긴다.

```cpp
template <class D>
struct SameName { bool put(u8 b) { return static_cast<D&>(*this).put(b); } };
struct Forgot : SameName<Forgot> {};            // put 을 안 만들었다
```

`Forgot` 에 `put` 이 없으면 이름 찾기가 기반의 `put` 을 찾는다 — **자기 자신을 호출**한다. 에러가 아니라 무한 재귀다. 실제 출력:

```text
$ c++ -std=c++20 -Wall -Wextra -O2 -S -o - demo_same_name.cpp     # Apple clang 21, arm64
demo_same_name.cpp:7:20: warning: all paths through this function will call itself [-Winfinite-recursion]
__Z11forgot_callR6Forgot:
	brk	#0x1
```

```text
$ arm-none-eabi-g++ -std=c++20 -Wall -Wextra -O2 -mcpu=cortex-m4 -mthumb -S demo_same_name.cpp -o -
demo_same_name.cpp:7:10: warning: infinite recursion detected [-Winfinite-recursion]
_Z11forgot_callR6Forgot:
	b	.L2
```

두 컴파일러 모두 **경고만** 낸다. clang 은 함수를 `brk` 한 줄로, GCC 는 자기 자신으로 점프하는 무한 루프로 만들었다. `-Werror` 를 안 쓰면 그대로 빌드되고, 칩에서는 HardFault 나 멈춤으로 나타난다. 그래서 이름을 `put` / `put_impl` 로 나누면 빠뜨렸을 때 곧바로 컴파일 에러가 된다.

> **C++23 `deducing this`** (P0847): `bool put(this auto& self, u8 b) { return self.put_impl(b); }` 처럼 쓰면 템플릿 기반 클래스 없이 같은 효과를 낸다. 이 레슨의 대상은 C++20 이지만, Apple clang 21 `-std=c++23` 과 arm-none-eabi-g++ 14.2.1 `-std=c++23` 모두에서 컴파일되는 것은 확인했다. 언급만 하고 다루지는 않는다.

### Part C — 프레임, 그리고 "한 배열에 못 담는다"의 정확한 뜻

`send_frame` 은 `put` 만 쓰므로 Mock 과 Stm32 에서 동일하게 동작한다. 하네스는 200회 무작위(고정 시드 xorshift32, `busy_polls` 0..3)로 두 구현의 선 위 바이트가 같은지 확인한다.

```svg
<svg viewBox="0 0 740 240" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">UartBase&lt;A&gt; 와 UartBase&lt;B&gt; 는 공통 부모가 없다</text>
  <rect x="20" y="40" width="200" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="120" y="67" text-anchor="middle" fill="currentColor" font-family="monospace" font-size="12">UartBase&lt;MockUart&gt;</text>
  <rect x="270" y="40" width="200" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="370" y="67" text-anchor="middle" fill="currentColor" font-family="monospace" font-size="12">UartBase&lt;Usart1&gt;</text>
  <rect x="520" y="40" width="200" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="620" y="67" text-anchor="middle" fill="currentColor" font-family="monospace" font-size="12">UartBase&lt;Usart2&gt;</text>
  <text x="370" y="110" text-anchor="middle" fill="currentColor">이름만 비슷한 무관한 타입 3개 — 포인터 변환 없음, 공통 포인터 타입 없음</text>
  <rect x="120" y="130" width="500" height="44" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="370" y="150" text-anchor="middle" fill="currentColor" font-family="monospace" font-size="12">AnyUart { void* obj; bool (*put_fn)(void*, u8); }</text>
  <text x="370" y="167" text-anchor="middle" fill="currentColor" font-size="12">D 를 아는 make_any&lt;D&gt; 가 함수 포인터 하나를 찍어서 만든다</text>
  <g stroke="currentColor" fill="none">
    <path d="M120 84 L260 130"/><path d="M370 84 L370 130"/><path d="M620 84 L480 130"/>
  </g>
  <text x="10" y="198" fill="currentColor">결국 "간접 호출" 을 되찾는다: 이질 컨테이너가 필요한 순간 정적 다형성의 인라인 이득은 그 경계에서 끝난다.</text>
  <text x="10" y="222" fill="currentColor" font-size="12">이질 배열 시도의 실제 에러: cannot initialize an array element of type 'Guarded&lt;A&gt; *' with an rvalue of type 'B *'</text>
</svg>
```

`Guarded<A>* arr[2] = {&a, &b};` 의 실제 에러 (`-DSHOW_ERROR=4`):

```text
demo_errors.cpp:70:49: error: cannot initialize an array element of type 'Guarded<A> *' with an rvalue of type 'B *'
```

우회 방법 세 가지:

| 방법 | 저장 | 호출 비용 | 언제 쓰나 |
|---|---|---|---|
| 손수 타입 지우기 `AnyUart` | 포인터 2개, 비소유 | 함수 포인터 1번 | 필요한 연산이 적을 때. 수명 관리는 호출자 책임 (CPP-M12 참고) |
| `std::variant` + `std::visit` | 가장 큰 타입 + 판별자 | 판별 후 분기 | 타입 집합이 닫혀 있고 값으로 저장하고 싶을 때 |
| virtual 어댑터 `UartAdapter<D>` | vptr + 참조 | 가상 호출 1번 | 다른 모듈에 안정된 ABI 를 내줘야 할 때 (CPP-M06 참고) |

`broadcast` 의 함정: 한 포트가 실패했다고 전체를 `return` 하면 다음 포트가 굶는다. 포트별 루프 안에서 `break` 하고 바깥 루프는 계속 돈다.

### CRTP vs virtual — 어셈블리를 직접 본다

`demo_inline.cpp` 의 같은 API 를 clang 으로 Cortex-M4 빌드 (`-Os`, 지시어 제거한 실제 출력):

```text
crtp_put:                  @ u.put(b) — 인라인됨, 호출 명령 없음
	movw	r2, #17408
	movt	r2, #16384
	ldr	r0, [r2]
	lsls	r0, r0, #24
	bpl	.LBB0_1
	movs	r0, #1
	str	r1, [r2, #4]
	bx	lr
virt_put:                  @ u.put(b) — vptr → 슬롯 → 간접 분기 (tail call)
	ldr	r2, [r0]
	ldr	r2, [r2]
	bx	r2
```

호출부만 비교하면 `virt_put` 은 6바이트로 더 작다. 하지만 구현 함수가 어딘가에 또 있고, 분기가 있다. 진짜 차이는 루프에서 드러난다. `virt_write` 는 바이트마다 `blx r2` 를 호출하지만 `crtp_write` 는 폴링과 `str` 이 루프 안에 그대로 있다.

```text
crtp_write 루프:                       virt_write 루프:
	ldrb.w	r12, [r1, lr]                 	ldr	r0, [r6]
	ldr	r0, [r3]                       	ldrb	r1, [r5], #1
	lsls	r0, r0, #24                    	ldr	r2, [r0]
	bpl	.LBB3_3                         	mov	r0, r6
	add.w	lr, lr, #1                     	blx	r2
	cmp	lr, r2                          	subs	r4, #1
	str.w	r12, [r3, #4]                  	bne	.LBB4_2
	bne	.LBB3_2
```

그런데 기대와 다른 결과도 나온다. 그래서 숫자가 필요하다 (함수별 바이트, `llvm-nm -S` / `arm-none-eabi-nm -S`):

| 함수 | clang -Os | clang -O2 | gcc -Os | gcc -O2 |
|---|---:|---:|---:|---:|
| `crtp_put` | 20 | 40 | 16 | 20 |
| `virt_put` | 6 | 6 | 6 | **40** |
| `virt_put_known` | 22 | 42 | 18 | 20 |
| `crtp_write` | 44 | **288** | 28 | 36 |
| `virt_write` | 34 | 38 | 28 | **64** |

표에서 읽어야 할 세 가지:

1. **`virt_put_known`: 컴파일러가 구체 타입을 보면 virtual 도 인라인된다** (devirtualization, 두 컴파일러 모두 `bx`/`blx` 없음). "virtual 은 항상 간접 호출"은 틀린 말이다 (CPP-M06 참고).
2. **gcc `-O2` 의 `virt_put` 이 40바이트로 커진 이유**: 단일 구현만 보이는 TU 에서 GCC 가 **추측성 devirtualization**(vtable 슬롯을 `Stm32UartV::put` 주소와 `cmp` 해서 같으면 인라인, 다르면 `bx r3`) 을 했다. 위 출력의 `cmp r3, r2` / `bne` 가 그 흔적이다.
3. **`crtp_write` 가 clang `-O2` 에서 288바이트**: 인라인 + 루프 unroll 때문이다. 폴링 루프까지 4배로 펼쳐졌다. CRTP 라서 항상 작다는 것은 **틀렸다** — 빠를 수는 있어도 `-O2` 에서는 커질 수 있다. 펌웨어는 대개 `-Os` 나 파일별 최적화를 쓴다.

사이클 수는 이 레슨에서 재지 않았다 (CPP-M06 참고: 맥에 DWT 없음).

### 코드 크기 — "타입 수"가 늘면 얼마나 늘어나는가

`demo_bloat.cpp` 는 `STX + LEN + 이스케이프 + CRC-8` 프레임 코드를 포트 N개에 대해 네 가지 구조로 만든다. `bloat.sh` 로 Lima VM 에서 빌드(`arm-none-eabi-g++ 14.2.1`, `--gc-sections`, 링크된 ELF 의 `.text`):

```text
== -Os   (.text 바이트)
mode                                    N=1      N=2      N=4      N=8
CRTP, 포트마다 타입               192      316      572     1084
CRTP, 타입 1개+런타임 base        192      264      344      504
CRTP, 공통 코드 hoist               244      312      424      648
virtual, 구현 1개                    240      276      348      492
== -O2   (.text 바이트)
mode                                    N=1      N=2      N=4      N=8
CRTP, 포트마다 타입               228      452      900     1796
CRTP, 타입 1개+런타임 base        228      452      900     1796
CRTP, 공통 코드 hoist               260      332      476      764
virtual, 구현 1개                    424      460      532      676
== -O2 -DFRAME_NOINLINE   (.text 바이트)
mode                                    N=1      N=2      N=4      N=8
CRTP, 포트마다 타입               248      492      980     1956
CRTP, 타입 1개+런타임 base        256      300      388      564
CRTP, 공통 코드 hoist               260      332      476      764
virtual, 구현 1개                    444      480      552      696
```

`virtual` 의 `.rodata` 는 N=1 에서 `.text` 와 별도로 +12바이트(28 vs 16, vtable. `g_payload` 16바이트 포함) 다. vtable 회계는 CPP-M06 몫이다.

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">복제되는 것은 "객체 수"가 아니라 "구별되는 타입(인스턴스화) 수"</text>
  <g font-family="monospace" font-size="12" fill="currentColor">
    <text x="10" y="58">Stm32Uart&lt;0x4000'4400&gt;</text>
    <text x="10" y="84">Stm32Uart&lt;0x4000'4800&gt;</text>
    <text x="10" y="110">Stm32Uart&lt;0x4000'4C00&gt;</text>
    <text x="10" y="136">Stm32Uart&lt;0x4000'5000&gt;</text>
  </g>
  <rect x="210" y="44" width="220" height="22" class="f-warn" stroke="currentColor"/>
  <rect x="210" y="70" width="220" height="22" class="f-warn" stroke="currentColor"/>
  <rect x="210" y="96" width="220" height="22" class="f-warn" stroke="currentColor"/>
  <rect x="210" y="122" width="220" height="22" class="f-warn" stroke="currentColor"/>
  <text x="440" y="60" fill="currentColor" font-size="12">write_frame 220바이트 × 4</text>
  <text x="440" y="86" fill="currentColor" font-size="12">(nm: 인스턴스별 0xdc)</text>
  <text x="10" y="176" fill="currentColor" font-weight="bold">런타임 base (타입 1개, 객체 4개)</text>
  <g font-family="monospace" font-size="12" fill="currentColor">
    <text x="10" y="204">Uart u0..u3 (타입 1개)</text>
  </g>
  <rect x="210" y="190" width="206" height="22" class="f-ok" stroke="currentColor"/>
  <text x="440" y="206" fill="currentColor" font-size="12">write_frame 206바이트 × 1 (nm: 0xce)</text>
  <text x="10" y="248" fill="currentColor">-O2 -DFRAME_NOINLINE, N=4 실제 nm 결과. 얇은 drive&lt;I&gt; 호출 코드는 모든 구조에 공통으로 있다.</text>
  <text x="10" y="270" fill="currentColor" font-size="12">기본 -O2 에서는 타입 1개여도 호출처마다 인라인되어 같은 값(900)이 나왔다 — 복제의 원인이 하나가 아니다.</text>
</svg>
```

실제 `nm -C --size-sort -S` 로 복제를 직접 본다 (`-O2 -DFRAME_NOINLINE`):

```text
== MODE=0 N=4
00008000 000000dc t UartBase<Uart<1073759232u> >::write_frame(unsigned char const*, unsigned int) [clone .isra
000080dc 000000dc t UartBase<Uart<1073760256u> >::write_frame(...)
000081b8 000000dc t UartBase<Uart<1073761280u> >::write_frame(...)
00008294 000000dc t UartBase<Uart<1073762304u> >::write_frame(...)
== MODE=1 N=4
00008000 000000ce t UartBase<Uart>::write_frame(unsigned char const*, unsigned int) [clone .isra.0]
```

해석:

- `-Os`, N=1 에서는 CRTP 192B < virtual 240B. **포트가 하나일 때는 CRTP 가 더 작다.**
- `-Os` 에서 N=2 부터 역전: CRTP N타입 316B > virtual 276B. 포트마다 다른 타입이면 포트 1개당 약 128B 씩 늘고, virtual 은 36B 씩 는다.
- `-DFRAME_NOINLINE` 표가 가장 깨끗하다: N타입 CRTP 는 8포트에서 1956B, 타입 1개면 564B. 4개 인스턴스의 `write_frame` 이 각각 220바이트(0xdc)로 복제된다.
- **기본 `-O2` 에서 타입 1개 구조가 N타입 구조와 똑같은 수치(1796B)** 가 나왔다. 복제 원인이 "템플릿 인스턴스 수"만은 아니라는 뜻이다: GCC 가 `write_frame` 을 호출처마다 인라인했다. 이 데모에서는 `drive<I>` 가 호출처 역할을 한다. 그래서 측정 없이 "타입 1개면 복제가 없다"고 단정할 수 없다.
- 줄이는 방법: **공통 코드를 비-템플릿으로 끌어올린다**(hoist, MODE=2). `-O2` 8포트 기준 1796B → 764B. 대신 바이트마다 함수 포인터 간접 호출이 생겨 정적 다형성의 이득을 그 구간에서 포기한다. 또는 `Base` 를 NTTP 가 아니라 런타임 멤버로 두어 타입 수 자체를 줄인다. 포트 주소가 런타임 값이면 레지스터 접근에 literal pool 로드 대신 객체 로드가 필요하다는 트레이드오프가 있다.

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

## 7. 자주 하는 실수

**1) 공개 API 와 구현 이름을 같게 둔다.** `put` 을 빠뜨려도 컴파일되어 무한 재귀가 된다 (5장 실제 출력 참고). 경고만 나오므로 `-Werror` 없이는 그대로 빌드된다.

```cpp
bool put(u8 b) { return self().put(b); }        // 틀림: Derived 에 없으면 자기 자신
bool put(u8 b) { return self().put_impl(b); }   // 맞음: 없으면 컴파일 에러
```

**2) `protected` 생성자로 복붙 실수를 막았다고 생각한다.** 파생 클래스면 누구나 부를 수 있다. `demo_errors.cpp` 의 `WrongProt w;` 가 컴파일된다. `private` + `friend Derived` 라야 한다.

**3) `static_cast` 대신 `reinterpret_cast` 로 `self()` 를 쓴다.** 다중 상속이면 기반의 주소와 파생의 주소가 다를 수 있다. `static_cast` 는 오프셋을 보정하지만 `reinterpret_cast` 는 그대로 쓴다. clang 은 이를 경고하고, `-Werror` 면 빌드가 멈춘다. 이 레슨 작성 중 실제로 찍힌 메시지:

```text
error: 'reinterpret_cast' to class 'Mixed *' from its base at non-zero offset 'Tagged<Mixed> *' behaves differently from 'static_cast' [-Werror,-Wreinterpret-base-class]
```

**4) 기반 클래스의 생성자에서 `self()` 를 호출한다.** 기반 클래스 생성자가 도는 동안 파생 객체는 아직 생성되지 않았다. virtual 은 생성자 안에서 현재 클래스로 디스패치되어 안전하지만, CRTP 는 아직 수명이 시작되지 않은 `Derived` 의 멤버에 접근한다. 비자명 생성자를 가진 파생의 멤버를 그 전에 참조하면 UB 다 ([class.cdtor]).

```cpp
UartBase() { self().init_impl(115200); }        // 틀림: Derived 생성 전
// 맞음: 생성자 밖에서 명시적으로 init() 호출 (펌웨어는 어차피 init 순서를 명시적으로 잡는다)
```

**5) 파생의 파생에서 `put_impl` 을 덮어쓰면 된다고 생각한다.** `class Fast : public Stm32Uart<B>` 에 `put_impl` 을 새로 만들어도 `UartBase<Stm32Uart<B>>::self()` 는 `Stm32Uart<B>` 로 내리므로 `Fast::put_impl` 은 불리지 않는다. virtual 의 override 가 아니라 이름 숨김(hiding)이다. CRTP 는 한 단계 동작을 컴파일 타임에 묶는다.

**6) 클래스 본문에서 파생을 검사한다.** `static_assert(sizeof(Derived) > 0)` 를 기반 클래스 본문에 쓰면 불완전 타입 에러가 난다 (5장 `-DSHOW_ERROR=6`). 검사는 멤버 함수 본문이나 `requires` 절에 둔다.

**7) "CRTP = 코드 크기 절약"이라고 단정한다.** 측정 결과 `-Os` 2포트부터 virtual 보다 컸다. 면접에서는 숫자와 조건(포트 수, 최적화 레벨)을 함께 말한다.

## 8. 면접 Q&A

<details>
<summary>Q1. CRTP 란 무엇이고, virtual 대비 얻는 것과 잃는 것은?</summary>

`class D : Base<D>` 로 기반이 파생 타입을 컴파일 타임에 아는 패턴이다. 얻는 것: 호출이 정적으로 묶여 인라인 가능, vptr 없음(`sizeof` 1바이트까지 확인), vtable 없음. 잃는 것: `Base<A>` 와 `Base<B>` 가 무관한 타입이라 공통 포인터가 없고, 구현을 런타임에 고를 수 없으며, 타입마다 코드가 복제될 수 있다. 측정 조건을 같이 말한다: "`-Os` 포트 1개는 CRTP 192B vs virtual 240B, 2개부터 역전".

</details>

<details>
<summary>Q2. `static_cast<Derived&>(*this)` 는 왜 안전한가? 언제 안전하지 않은가?</summary>

`*this` 가 실제로 `Derived` 객체의 기반 서브오브젝트일 때만 잘 정의된다 ([expr.static.cast]). `class B : Base<A>` 면 B 가 A 가 아니므로 UB 다. ASan 으로 재현하면 stack-buffer-overflow 가 나온다. 그래서 private 생성자 + `friend Derived` 로 그 경우를 생성 불가로 만든다.

꼬리질문: "`friend` 말고 다른 방법은?" — `std::derived_from<T, Base<T>>` concept 로 사용 측에서 검사할 수 있다(연습 1). 다만 그건 쓰는 쪽에서만 검사한다.

</details>

<details>
<summary>Q3. 왜 기반 클래스 본문에서는 Derived 멤버를 못 쓰는데 멤버 함수 본문에서는 되나?</summary>

기반 클래스는 파생의 기반 절을 처리할 때 인스턴스화되어 그 시점에 `Derived` 가 불완전하다. 멤버 함수 본문은 사용될 때 인스턴스화되고 그 인스턴스화 지점([temp.point])에서는 `Derived` 가 완전하다. 그래서 `static_assert` 같은 검사는 멤버 함수 본문이나 `requires` 절에 둔다.

</details>

<details>
<summary>Q4. CRTP 드라이버를 쓰는데, 런타임에 UART 를 골라야 하는 요구가 생겼다. 어떻게 하나?</summary>

공통 인터페이스 위에 타입 지우기를 올린다. 작은 경우 `void*` + 함수 포인터(`AnyUart`), 닫힌 타입 집합이면 `std::variant` + `std::visit`, 모듈 경계면 `IUart` + `UartAdapter<D>`. 핵심은 **경계에서만** 비용을 내는 것이다. 드라이버 내부의 빠른 경로는 CRTP 로 유지한다. `std::variant` 는 `-fno-exceptions` 빌드에서도 `practice.cpp` 로 컴파일·동작을 확인했다(잘못된 접근 시 예외 대신 종료 경로로 간다, 그 동작 자체는 이 레슨에서 실행하지 않았다).

</details>

<details>
<summary>Q5. 코드 크기가 문제라면 CRTP 를 어떻게 다듬나?</summary>

세 가지. (1) 타입 수를 줄인다: 포트 주소를 NTTP 대신 런타임 멤버로 두면 인스턴스가 하나다. (2) 공통 코드를 비-템플릿 함수로 끌어올린다(`frame_core`). 8포트 `-O2` 에서 1796B → 764B 측정. (3) `-Os` + `--gc-sections` 로 빌드한다. 단, LTO 나 ICF 효과는 이 레슨에서 측정하지 않았다. "측정 먼저"가 답의 핵심이다: 기본 `-O2` 에서는 타입 1개인데도 호출처마다 인라인되어 N타입과 같은 크기가 나왔다.

</details>

<details>
<summary>Q6. virtual 호출은 항상 간접 분기인가?</summary>

아니다. 구체 타입이 보이면 devirtualization 으로 인라인된다(`virt_put_known`). GCC `-O2` 는 구체 타입을 모르는 `virt_put` 에서도 vtable 슬롯을 비교해 추측성으로 인라인했다. 반대로 CRTP 라고 항상 작은 것도 아니다. clang `-O2` `crtp_write` 는 unroll 로 288바이트가 됐다. 자세한 비용은 CPP-M06.

</details>

<details>
<summary>Q7. 구현 함수를 private 으로 두는 이유는? 그러면 기반이 어떻게 부르나?</summary>

사용자가 `u.put_impl()` 로 기반의 공통 처리(카운터, 상태 검사)를 건너뛰지 못하게 하기 위해서다. 파생에 `friend UartBase<자신>` 을 선언하면 기반만 접근할 수 있다. 이 레슨의 `implements_interface()` 도 `requires` 식을 기반의 멤버로 뒀기 때문에 private 멤버를 검사할 수 있다. 네임스페이스 범위 concept 은 private 멤버가 안 보인다.

</details>

## 9. 추가 연습

**연습 1 — concept 로 CRTP 검증.** `template <class T> concept CrtpUart = std::derived_from<T, UartBase<T>>;` 를 만들고, `MockA` 는 통과, `WrongDerived`, `int` 는 거부를 `static_assert` 로 확인한다. `greet<CrtpUart U>` 로 사용한다.

<details><summary>답 보기</summary>

`std::derived_from<T, UartBase<T>>` 는 `UartBase<T>` 가 `T` 의 public 비모호 기반인지 본다. `WrongDerived` 는 `UartBase<MockA>` 를 상속하므로 `UartBase<WrongDerived>` 가 아니라서 거부된다. `int` 는 클래스가 아니어서 거부된다.

</details>

**연습 2 — CRTP 믹스인 두 개와 포인터 보정.** `class Mixed : public Stats<Mixed>, public Tagged<Mixed>` 에서 `Tagged` 의 `self()` 가 올바른 주소를 돌려주는지 확인한다. 두 번째 기반의 주소는 `&m` 과 다르다.

<details><summary>답 보기</summary>

`Stats<Mixed>` 가 `u32 bytes` 를 가지므로 `Tagged<Mixed>` 는 오프셋 4 에 놓인다. `static_cast<Mixed&>(*this)` 는 이 오프셋을 보정해 `&m` 을 돌려준다. `void*` 를 거쳐 보정 없이 바꾸면 주소가 다르다 — 역참조하면 UB. 이것이 `self()` 에 `static_cast` 만 써야 하는 이유다.

</details>

**연습 3 — `std::variant` 로 이질 배열.** `using Port = std::variant<MockA, MockB>;` 로 두 포트를 값으로 담고 `std::visit` 으로 `put` 한다. `sizeof(Port)` 가 `sizeof(MockA)` 보다 큼을 확인한다.

<details><summary>답 보기</summary>

`std::visit([b](auto& u) { return u.put(b); }, p)` — 제네릭 람다가 타입별로 인스턴스화된다. 판별자가 추가되어 크기가 늘고, `-fno-exceptions` 빌드에서도 컴파일·실행된다 (`practice.cpp`). 힙도 RTTI 도 쓰지 않는다.

</details>

**연습 4 — virtual 어댑터.** `class IUart` 와 `template <class D> class UartAdapter final : public IUart` 로 서로 다른 어댑터를 `IUart*` 배열에 담는다. 소멸자는 `protected` 로 두어 인터페이스를 통한 `delete` 를 막는다.

<details><summary>답 보기</summary>

어댑터는 `D&` 만 들고 `put` 을 전달한다. 드라이버 안쪽은 CRTP 로 인라인되고 경계에서만 가상 호출 1번을 낸다. 힙이 없으면 인터페이스를 통해 delete 할 일이 없으므로 protected 비가상 소멸자로 충분하다 (deleting destructor 문제는 CPP-M06 참고).

</details>

**연습 5 — 공통 코드 끌어올리기.** `frame_core(void* ctx, PutFn, span)` 를 비-템플릿으로 만들고, `send_frame_thin<D>` 는 트램펄린만 만들어 넘긴다. 본문 `send_frame` 과 같은 바이트가 나와야 한다.

<details><summary>답 보기</summary>

`D` 별로 남는 것은 캡처 없는 람다 하나뿐이다. `demo_bloat.cpp` MODE=2 가 이 구조이고 `-O2` 8포트에서 1796B → 764B 로 줄었다. 대신 바이트마다 간접 호출을 낸다 — 크기와 속도를 맞바꾸는 지점을 숫자로 정한다.

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
# 20/20 passed
```

## 10. 핵심 정리

- CRTP: `class D : Base<D>` → 호출이 컴파일 타임에 묶여 인라인 가능, vptr 없음.
- 기반 본문에서 Derived 는 불완전. 파생 멤버는 **멤버 함수 본문**이나 `requires` 절에서만 쓴다.
- 복붙 실수는 `private` 생성자 + `friend Derived` 로 막는다. `protected` 로는 못 막는다. C++17 은 aggregate 구멍이 있다.
- 공개 API 와 구현은 **이름을 다르게** (`put` / `put_impl`). 같으면 빠뜨렸을 때 무한 재귀가 된다.
- 구현 누락 에러: `requires` 절이 1개로 가장 깨끗, `static_assert` 는 메시지는 생기지만 원 에러도 남는다.
- `UartBase<A>` 와 `UartBase<B>` 는 무관한 타입. 이질 컨테이너는 타입 지우기·`variant`·virtual 어댑터로 경계에서만 해결.
- 인라인 ≠ 작다: clang `-O2` `crtp_write` 288B. 포트 1개는 CRTP 가 작고(192B vs 240B, `-Os`), 2개부터 역전(316B vs 276B). 타입 수와 최적화 레벨을 함께 말한다.
- 복제의 원인은 타입 수만이 아니다: 기본 `-O2` 는 타입 1개에서도 호출처별 인라인으로 같은 크기가 나왔다. 측정이 먼저다.

## 참고자료

- C++ 표준: [temp.deduct.call] (파생→기반 추론), [temp.point] (인스턴스화 지점), [expr.static.cast] (다운캐스트 UB), [class.friend] (`friend T;`), [class.cdtor] (생성 중 멤버 접근)
- P1008R1 — Prohibit aggregates with user-declared constructors (C++20): https://wg21.link/P1008R1
- P0847R7 — Deducing this (C++23): https://wg21.link/P0847R7
- cppreference: CRTP https://en.cppreference.com/w/cpp/language/crtp
- RM0090 (STM32F4 Reference Manual) — USART 레지스터 (SR/DR/BRR/CR1)
- 관련 레슨: CPP-B08 (EBO), CPP-B12 (템플릿 레지스터), CPP-M06 (vtable 회계), CPP-M12 (delegate), MCPP-M05 (concepts)
