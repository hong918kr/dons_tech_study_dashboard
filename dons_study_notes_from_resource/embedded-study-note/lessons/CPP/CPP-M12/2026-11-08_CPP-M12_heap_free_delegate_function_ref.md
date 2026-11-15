---
id: CPP-M12
time: 90분
updated: 2026-11-08
---
# CPP-M12 무힙 콜백: std::function 대신 delegate / function_ref

## 학습 목표

- 객체 포인터 + 스텁 함수 포인터 **두 워드**로 된 `Delegate<R(Args...)>` 를 구현해 자유 함수, 멤버 함수(NTTP 멤버 포인터), const 멤버 함수, 캡처 없는 람다를 힙 없이 바인딩한다.
- 임시 객체·const 불일치·시그니처 불일치를 컴파일 에러로 막고, 다중 상속 `this` 보정과 `void` 반환 버리기까지 스텁에서 올바르게 처리한다.
- 소유하는 콜백(`Delegate`, 테이블에 저장)과 비소유 인자 전용 뷰(`function_ref`)를 구분하고 수명 규칙을 설명한다.
- 타이머 콜백 테이블을 만들어 wrap-safe 만료 판정, 드리프트 없는 주기, 낡은 핸들 검출, ISR과 메인의 2워드 쓰기 충돌을 처리한다.
- `std::function` 과 `sizeof`·`operator new` 횟수·호출 어셈블리·링크 크기를 **직접 측정**해서 비교한다.

## 1. 문제

펌웨어에서 콜백은 어디에나 있고(타이머, DMA 완료, UART 수신) 힙은 대개 금지다. 그래서 "멤버 함수를 콜백으로 어떻게 등록하나, `std::function` 은 왜 안 쓰나"는 C++ 펌웨어 면접의 단골 질문이다.

C에서는 이렇게 쓴다.

```c
typedef void (*timer_cb_t)(void* ctx, uint32_t now);
void timer_arm(timer_cb_t cb, void* ctx, uint32_t delay);

static void led_on_tick(void* ctx, uint32_t now) {
    struct led* l = ctx;          /* 캐스트는 사람 몫, 틀려도 컴파일러는 모른다 */
    l->toggles++;
}
```

이 패턴 자체는 훌륭하다. 두 워드이고, 힙이 없고, 호출은 간접 분기 한 번이다. 약점은 `void*` 캐스트를 **사람이 손으로** 쓴다는 것이다. `Delegate` 는 이 C 패턴을 그대로 유지하면서 캐스트를 **컴파일러가 생성하는 스텁(stub)** 안으로 옮긴다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">Delegate = 문맥 + 스텁. 대상 멤버 함수는 스텁의 "템플릿 인자"에 박혀 있다</text>
  <rect x="10" y="44" width="230" height="110" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="22" y="66" fill="currentColor" font-weight="bold">Delegate&lt;void(u32)&gt; (Cortex-M: 8바이트)</text>
  <rect x="22" y="78" width="206" height="28" class="f-ok" stroke="currentColor"/>
  <rect x="22" y="114" width="206" height="28" class="f-ok" stroke="currentColor"/>
  <text x="32" y="97" fill="currentColor" font-family="monospace" font-size="12">ctx_  = &amp;led (void*)</text>
  <text x="32" y="133" fill="currentColor" font-family="monospace" font-size="12">stub_ = obj_stub&lt;&amp;Led::on, Led&gt;</text>
  <rect x="300" y="44" width="430" height="110" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="312" y="66" fill="currentColor" font-weight="bold">컴파일러가 바인딩마다 찍어내는 스텁</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="312" y="88">static void obj_stub(Ctx c, u32 now) {</text>
    <text x="312" y="108">  static_cast&lt;Led*&gt;(c.obj)-&gt;on(now);</text>
    <text x="312" y="128">}  // 캐스트가 여기에만 있다. 타입은 템플릿이 보장</text>
  </g>
  <path d="M228 128 L298 110" stroke="currentColor" fill="none"/>
  <text x="10" y="184" fill="currentColor" font-weight="bold">std::function&lt;void(u32)&gt; (이 툴체인 thumb: 16바이트, 64비트 호스트: 32바이트)</text>
  <rect x="10" y="196" width="400" height="60" rx="8" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="22" y="216">[ 인라인 버퍼 ] [ manager ] [ invoker ]</text>
    <text x="22" y="238">  캡처가 크면 버퍼 대신 힙 포인터 → operator new</text>
  </g>
  <rect x="430" y="196" width="300" height="60" rx="8" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="442" y="216">복사·소멸·수명 관리까지 맡는다 (소유)</text>
    <text x="442" y="238">빈 상태 호출은 throw/abort</text>
  </g>
  <text x="10" y="284" fill="currentColor">Delegate 는 복사해도 "포인터 복사"다. 대상 객체를 소유하지 않으므로 수명은 호출자 책임.</text>
  <text x="10" y="306" fill="currentColor">이게 바로 공짜인 이유이자 이 레슨의 Part B, Part C 가 다루는 위험의 원인이다.</text>
</svg>
```

`std::function` 구현(수동 vtable + SBO)은 MCPP-M07 이 맡는다. 여기서는 **쓰는 쪽의 비용**만 잰다. 예외를 끈 빌드와 `Result` 는 CPP-B13, `operator new` 금지는 CPP-B14, RAII 가드는 CPP-B06 참고.

### Part A — Delegate 핵심

`template <typename Sig> class Delegate;` 를 `Delegate<R(Args...)>` 로 부분 특수화한다.

- `Delegate::bind<&free_fn>()` — 자유 함수. 호출 대상이 템플릿 인자에 박힌다.
- `Delegate::bind<&Foo::method>(foo)` — 멤버 함수와 const 멤버 함수. `Foo` 가 const 면 const 멤버만 된다.
- `Delegate::from_lambda(lambda)` — 캡처 없는 람다. `bind<+[](int){...}>()` 경로도 동작해야 한다.
- 빈 상태(`explicit operator bool`), `d = {}` 로 초기화, `sizeof == 2 워드`, trivially copyable, `constinit` 전역 가능.

### Part B — 면접관이 파고드는 부분

- **컴파일 에러로 막기**: 비 const 멤버 + const 객체, 시그니처 불일치, **임시 객체(const 임시 포함)**, 캡처 있는 람다.
- **스텁의 정확성**: 다중 상속의 `this` 보정, `R == void` 일 때 반환값 버리기, 오버로드된 멤버 선택, 값 인자 이동 횟수.
- **런타임 함수 포인터** (`from_ptr`): 왜 `void*` 에 넣으면 안 되는가.
- **동등 비교** (`operator==`): 테이블에서 콜백을 찾아 지우는 데 필요하다.
- **`FunctionRef<R(Args...)>`**: 캡처 람다도 받는 비소유 인자 전용 뷰 (C++26 `std::function_ref` 축소판).

### Part C — 타이머 콜백 테이블

`TimerTable<N>` : `arm(now, delay, period, cb) → TimerId`, `cancel(id)`, `tick(now)`.

- 32비트 카운터 wrap-around 안전. 주기 타이머는 드리프트 없이, 놓친 주기는 overrun 으로 센다.
- 콜백 안에서 `arm`/`cancel` 해도 안전 (재등록, 뒤 슬롯 취소, 새 타이머는 다음 tick 부터).
- 낡은 핸들은 남의 타이머를 끄지 못한다 (세대 번호 generation).
- `irq::Guard` 로 메인의 다중 워드 쓰기를 보호한다. 쓰는 도중 ISR 이 끼어들면 해제 시점까지 밀린다.
- `ScopedTimer` (RAII): 객체가 죽으면 타이머도 해제.

## 2. Problem (English)

> **Heap-free callbacks.** Implement a non-owning, two-word callable wrapper for a firmware build (`-fno-exceptions -fno-rtti`, no heap).
>
> 1. `Delegate<R(Args...)>` with factories `bind<&free>()`, `bind<&Class::member>(obj)` (const-correct), `from_lambda(captureless)`, and `from_ptr(fp)`. It must be trivially copyable, exactly `2 * sizeof(void*)`, usable in `constinit` globals, and comparable with `==`.
> 2. These must **fail to compile**: binding a non-const member to a const object, a mismatched signature, binding any temporary (including `const` rvalues), passing a capturing lambda to `from_lambda`.
> 3. `FunctionRef<R(Args...)>`: a non-owning parameter-only view accepting any callable, including capturing and mutable lambdas.
> 4. `TimerTable<N>`: `TimerId arm(uint32_t now, uint32_t delay, uint32_t period, TimerCb cb)`, `bool cancel(TimerId)`, `void tick(uint32_t now)`. Handle 32-bit wrap, drift-free periods with overrun counting, re-arming and cancelling from inside a callback, and stale handles.
>
> Example: `auto d = Delegate<int(int)>::bind<&Counter::add>(c); d(5);` calls `c.add(5)`. Compare with `std::function` by measuring `sizeof`, allocation count, and the generated call sequence on Cortex-M4.

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
# 처음엔 "18/86 passed" — TODO 를 채워 86/86 을 만든다
```

스타터의 스텁은 `R()` 을 돌려주고 `bind` 는 빈 `Delegate` 를 돌려주므로, 빈 콜백을 호출해도 크래시 없이 FAIL 만 찍힌다. 제약 조건 `callable_as_v`, `captureless_v` 는 일부러 `true` 로 느슨하게 둬서 "컴파일되면 안 되는 경우"가 FAIL 로 보인다.

## 4. 힌트

<details>
<summary>힌트 1 — 스텁의 시그니처는 전부 같아야 한다</summary>

저장할 함수 포인터 타입은 하나여야 하므로 스텁은 전부 `R(Ctx, Args...)` 모양이다. 호출 대상은 값이 아니라 **템플릿 인자** `template <auto M, typename T> static R obj_stub(...)` 로 스텁 안에 박아 넣는다. 그래서 멤버 포인터를 저장할 공간이 필요 없다.

</details>

<details>
<summary>힌트 2 — 객체를 저장한 타입 그대로 되돌려라</summary>

`static_cast<void*>(obj)` 로 넣었다면 꺼낼 때도 **같은 타입** `T*` 로 되돌려야 한다. `Multi` 를 넣고 `BaseB*` 로 꺼내면 `this` 보정이 빠진다. 보정은 `std::invoke(M, *static_cast<T*>(p), ...)` 가 `T& → BaseB&` 변환으로 해 준다.

</details>

<details>
<summary>힌트 3 — T&amp; 만으로는 const 임시 객체를 못 막는다</summary>

`Counter{}` 는 `T&` 에 안 붙지만, `const Counter` prvalue 는 `T = const Counter` 로 추론되어 붙는다. `template <auto M, typename T> static Delegate bind(const T&&) = delete;` 처럼 rvalue 전용 오버로드를 지워야 한다. `const T&&` 는 전달 참조(forwarding reference)가 아니라서 lvalue 를 삼키지 않는다.

</details>

<details>
<summary>힌트 4 — void 반환</summary>

`Delegate<void(int)>` 에 `int add(int)` 를 묶으면 `is_invocable_r_v<void, ...>` 는 true 인데, `return std::invoke(...)` 는 void 함수에서 값을 반환하므로 컴파일 에러다. `if constexpr (std::is_void_v<R>)` 로 갈라서 버린다.

</details>

<details>
<summary>힌트 5 — tick 안의 순서</summary>

만료 판정은 `static_cast<int32_t>(now - deadline) >= 0`. 콜백을 **로컬에 복사**하고, one-shot 이면 **호출 전에** 슬롯을 비운다. 그래야 콜백이 같은 슬롯을 다시 써도 망가지지 않는다.

</details>

## 5. 풀이

### Part A — 스텁으로 타입 지우기

타입 소거(type erasure)의 최소 형태다. 저장소에는 타입이 없는 문맥 하나, 타입이 고정된 함수 포인터 하나만 있다.

```cpp
template <auto M, typename T>
static R obj_stub(Ctx c, Args... a) {
    return call_(M, *static_cast<T*>(c.obj), std::forward<Args>(a)...);
}
```

| 바인딩 | 저장되는 문맥 | 스텁이 하는 일 |
|---|---|---|
| `bind<&f>()` | 없음 (`nullptr`) | `f(a...)` 직접 호출 |
| `bind<&C::m>(c)` | `&c` | `(T*)` 복원 후 멤버 호출 |
| `bind<&C::m>(cc)` (const) | `const_cast` 한 `&cc` | `static_cast<const C*>` 로 복원 |
| `from_lambda(l)` | 없음 | `L l{}; l(a...)` |
| `from_ptr(fp)` | `fn` 멤버 | 원래 타입으로 되돌려 호출 |

**왜 멤버 포인터를 저장하지 않나.** 멤버 함수 포인터는 일반 포인터가 아니다. 이 측정에서 `sizeof(void (Motor::*)(int))` 는 Thumb 에서 8바이트, 64비트에서 16바이트로 **두 워드**다. 클래스 타입에도 묶여 있어서 하나의 공통 타입으로 저장할 수 없다. NTTP 로 넘기면 멤버 포인터는 런타임 저장소 없이 스텁 코드에만 존재한다.

**`const_cast` 는 UB 가 아닌가.** const 객체를 **수정**해야 UB 다 ([dcl.type.cv]). 여기서는 저장 타입을 맞추려고 잠깐 벗길 뿐, 스텁은 `T = const Counter` 로 되돌려 const 경로로만 접근한다.

캡처 없는 람다는 두 경로가 있다. 첫째, C++20 부터 캡처 없는 클로저 타입은 기본 생성 가능하다([expr.prim.lambda.closure]). 그래서 스텁 안에서 `L l{}` 로 만들어 호출한다. 둘째, `+` 로 함수 포인터로 바꾸면 NTTP 로 넘길 수 있다. 람다 자체를 NTTP 로 넘기는 것은 클로저 타입이 구조적(structural) 타입인지가 미지정이라 이식성이 없다.

### Part B — 막아야 할 것, 보정해야 할 것

`demo_errors.cpp` 를 `-DSHOW_ERROR=N` 으로 돌린 실제 에러다(앞줄만 잘랐다). 1 = 비 const 멤버 + const 객체, 2 = 임시 객체, 3 = 시그니처 불일치, 4 = 캡처 있는 람다, 5 = 오버로드 집합을 `auto` NTTP 로 넘김.

```text
$ c++ -std=c++20 -fsyntax-only -DSHOW_ERROR=1 demo_errors.cpp
demo_errors.cpp:12:14: error: no matching function for call to 'bind'
./solution.cpp:74:45: note: candidate template ignored: constraints not satisfied [with M = &Counter::add, T = const Counter]
./solution.cpp:73:44: note: because 'callable_as_v<int, decltype(&Counter::add), const Counter &, int>' evaluated to false
$ c++ -std=c++20 -fsyntax-only -DSHOW_ERROR=2 demo_errors.cpp
demo_errors.cpp:14:14: error: call to deleted function 'bind'
./solution.cpp:77:51: note: candidate function [with M = &Counter::add, T = Counter] has been explicitly deleted
$ c++ -std=c++20 -fsyntax-only -DSHOW_ERROR=3 demo_errors.cpp
./solution.cpp:73:44: note: because 'callable_as_v<int, decltype(&Counter::add), Counter &, const char *>' evaluated to false
$ c++ -std=c++20 -fsyntax-only -DSHOW_ERROR=4 demo_errors.cpp
demo_errors.cpp:19:14: error: no matching function for call to 'from_lambda'
./solution.cpp:80:37: note: because 'captureless_v<(lambda at demo_errors.cpp:19:29)>' evaluated to false
$ c++ -std=c++20 -fsyntax-only -DSHOW_ERROR=5 demo_errors.cpp
./solution.cpp:74:45: note: candidate template ignored: invalid explicitly-specified argument for template parameter 'M'
```

**함수 포인터를 왜 `union` 에 넣나.** 함수 포인터 ↔ `void*` 변환은 conditionally-supported 다 ([expr.reinterpret.cast]). Cortex-M 과 POSIX 에서는 되지만 표준은 보장하지 않는다. 반면 함수 포인터끼리의 `reinterpret_cast` 왕복은 값이 보존된다. 그래서 `union Ctx { void* obj; void (*fn)(); }` 로 두고, 어느 멤버가 활성인지는 **스텁이 무엇인가**로 판단한다. `operator==` 도 스텁을 먼저 비교하고 그 결과로 읽을 멤버를 고른다. 비활성 멤버를 읽으면 UB 이기 때문이다.

**값 인자는 두 번 이동한다.** `operator()` 매개변수 → 스텁 매개변수 → 대상 매개변수로 넘어가기 때문이다. 테스트 `moves == 2` 가 이를 고정한다. 복사 비용이 큰 타입은 `Delegate<void(const Frame&)>` 처럼 참조로 받는다.

**FunctionRef 와 Delegate 의 차이.** `FunctionRef` 는 `F&&` 를 받아 주소만 저장한다. 임시 람다를 넘겨도 호출식이 끝날 때까지는 살아 있으므로 **인자로만** 쓰면 안전하다.

```cpp
apply3([&total](int x) { total += 10 * x; });   // OK: 임시 람다는 full-expression 끝까지 산다
FunctionRef<void(int)> r = [&](int) {};         // 위험: 이 줄이 끝나면 람다는 죽는다
```

| | `Delegate` | `FunctionRef` | `std::function` |
|---|---|---|---|
| 소유 | 안 함 | 안 함 | **함** (복사·소멸) |
| 캡처 람다 | 불가 | 가능 | 가능 |
| 저장 | 가능 (대상이 살아 있는 동안) | 금지 | 가능 |
| 용도 | 드라이버·타이머 콜백 슬롯 | 함수 인자 (`retry`, `for_each`) | 호스트 코드 |

C++26 `std::function_ref` (P0792R14) 는 같은 아이디어를 표준화한 것이다. 멤버 바인딩용 태그는 처음에 `std::nontype` 이었고 이름을 바꾸는 논의가 있었다 (P3774R1 이 `constant_arg` 로 개명 기록). libc++, libstdc++ 도 2026년 커밋에서 각자 수정 중이라 **최종 표기는 미확인**이다. 이 레슨의 툴체인에는 아직 없다.

```text
$ c++ -dM -E -x c++ -std=c++2c -include version /dev/null | grep -E "function_ref|move_only_function|copyable_function|__cpp_lib_invoke_r"
#define __cpp_lib_invoke_r 202106L
```

Apple clang 21 은 `invoke_r` 만 정의했다. VM 의 `g++ 15.2`, `arm-none-eabi-g++ 14.2` 에서 `-std=c++26` 으로 같은 매크로를 찾아도 `function_ref` 는 나오지 않았다.

### Part C — 타이머 테이블

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">메인이 슬롯을 쓰는 중에 타이머 ISR 이 오면</text>
  <rect x="10" y="40" width="340" height="108" rx="8" class="f-warn" stroke="currentColor"/>
  <text x="22" y="60" fill="currentColor" font-weight="bold">보호 없음: Delegate 대입은 2워드</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="22" y="82">str ctx   ← 새 객체</text>
    <text x="22" y="102">   ▲ 여기서 ISR: 새 ctx + 옛 stub</text>
    <text x="22" y="122">str stub  ← 새 스텁</text>
    <text x="22" y="142">→ 엉뚱한 this 로 멤버 호출</text>
  </g>
  <rect x="390" y="40" width="340" height="108" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="402" y="60" fill="currentColor" font-weight="bold">Guard + active 마지막</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="402" y="82">cpsid i (PRIMASK 저장)</text>
    <text x="402" y="102">cb → deadline → period → active=1</text>
    <text x="402" y="122">PRIMASK 복원</text>
    <text x="402" y="142">→ 밀린 ISR 이 완성된 슬롯만 본다</text>
  </g>
  <text x="10" y="178" fill="currentColor" font-weight="bold">tick() 안에서 슬롯 하나 처리 순서</text>
  <g stroke="currentColor">
    <rect x="10"  y="192" width="150" height="44" class="f-mut"/>
    <rect x="186" y="192" width="150" height="44" class="f-mut"/>
    <rect x="362" y="192" width="176" height="44" class="f-mut"/>
    <rect x="564" y="192" width="166" height="44" class="f-acc"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="18" y="212">(int32)(now-deadline)</text>
    <text x="18" y="228">&gt;= 0 인가</text>
    <text x="194" y="212">cb 를 로컬에 복사</text>
    <text x="194" y="228">(슬롯 재사용 대비)</text>
    <text x="370" y="212">one-shot: active=0</text>
    <text x="370" y="228">주기: deadline += k*period</text>
    <text x="572" y="212">cb(now) 호출</text>
    <text x="572" y="228">테이블 조작 허용</text>
  </g>
  <g stroke="currentColor" fill="none"><path d="M160 214 L186 214"/><path d="M336 214 L362 214"/><path d="M538 214 L564 214"/></g>
  <text x="10" y="266" fill="currentColor">delay 0 을 거부하면 "콜백이 이번 tick 에 새로 등록한 타이머"는 deadline &gt; now 이므로</text>
  <text x="10" y="286" fill="currentColor">같은 tick 에 돌지 않는다. 별도 플래그 없이 재진입 순서가 결정적이 된다.</text>
  <text x="10" y="308" fill="currentColor">now 는 2^32 에서 wrap 한다. delay, period 는 2^31 미만이어야 부호 비교가 유효하다.</text>
</svg>
```

| 항목 | 잘못된 코드 | 결과 | 맞는 코드 |
|---|---|---|---|
| 만료 판정 | `now >= deadline` | wrap 직전 조기 발화 (테스트 `w.toggles == 0`) | `(int32_t)(now - deadline) >= 0` |
| 주기 갱신 | `deadline = now + period` | 처리 지연만큼 누적 드리프트 | `deadline += (missed + 1) * period` |
| one-shot 해제 | 콜백 **후** `active = false` | 콜백이 재등록한 슬롯을 지운다 | 콜백 **전**에 해제, 콜백은 로컬 복사본 호출 |
| `cancel` 인자 | 슬롯 번호만 | 재사용된 슬롯의 남의 타이머를 끈다 | `{idx, gen}` 핸들 비교 |

**torn write 실측** (`demo_torn.cpp`, 의도적 UB, 호스트 관찰용). `memcpy` 두 번으로 "첫 워드만 쓰인 찰나"를 만들어 호출했다.

```text
pump   @0x16b60687c, heater @0x16b606878
before update:
  Pump::on   this=0x16b60687c rpm=1000
ISR fires between the two stores (ctx=Heater, stub=Pump):
  Pump::on   this=0x16b606878 rpm=50
after update:
  Heater::on this=0x16b606878 watt=50
```

`Pump::on` 이 `Heater` 객체를 `Pump` 로 해석해서 `rpm=50` 을 읽었다. 크기가 다른 타입이었다면 메모리를 망가뜨렸을 것이다. Cortex-M4 에서 8바이트 대입이 `STRD` 든 `STR` 두 번이든 중간에 인터럽트가 끼어들 수 있으므로 하나의 원자적 쓰기로 믿으면 안 된다. (본문은 ARM 실측이 아니라 호스트 재현이다.)

실제 칩의 `Guard` 는 PRIMASK 저장 + `cpsid i` 이고 CMSIS `cmsis_gcc.h` 의 `__disable_irq` 는 `"memory"` clobber 를 가지므로 컴파일러 재배치도 막는다(헤더 기억 기준, 이번에 확인은 안 함). ISR 과 메인 사이의 메모리 순서 일반론은 CPP-A04 참고.

**수명 — 왜 RAII 가 필요한가.** `Delegate` 는 대상의 수명을 모른다. `demo_dangling.cpp` 를 ASan 으로 돌린 실제 결과다.

```text
==60800==ERROR: AddressSanitizer: stack-use-after-scope on address 0x00016dbea8c0 at pc 0x000102225f68 bp 0x00016dbea5f0 sp 0x00016dbea5e8
READ of size 4 at 0x00016dbea8c0 thread T0
    #0 0x000102225f64 in Led::on_tick(unsigned int) solution.cpp:320
    #5 0x00010221fcd8 in Delegate<void (unsigned int)>::operator()(unsigned int) const solution.cpp:94
    #6 0x00010221fbb0 in main demo_dangling.cpp:31
    [64, 68) 'led' (line 26) <== Memory access at offset 64 is inside this variable
```

주의: **`-O0` 에서만 잡혔다.** 같은 데모를 `-O1` 로 빌드하면 ASan 보고 없이 `unreachable under ASan` 까지 그대로 실행됐다. 최적화가 지역 변수의 수명 표시를 없앤 탓으로 추정하지만 원인은 미확인이다. 어쨌든 교훈은 "ASan 이 조용하다고 수명 문제가 없는 것은 아니다".

컴파일러 경고도 약하다. 지역 객체를 묶은 Delegate 를 반환해도 `-Wall` 은 아무 말 없고, 맨 포인터 반환만 경고한다.

```text
$ c++ -std=c++20 -fsyntax-only -Wall -DSHOW_RETURN demo_dangling.cpp
demo_dangling.cpp:16:13: warning: address of stack memory associated with local variable 'led' returned [-Wreturn-stack-address]
```

`bind(T& obj [[clang::lifetimebound]])` 로 스크래치 복사본을 고쳐 보면 Delegate 반환(12번째 줄)에도 같은 경고가 추가로 나왔다. clang 전용 속성이고 solution 에는 넣지 않았다. 함수 안에서 끝나는 경우만 잡으므로 `ScopedTimer` 같은 구조적 해결을 대신하지 못한다.

### 측정 — sizeof

`measure.sh alloc`(호스트), `measure.sh asm`(Thumb). 전부 실제 출력값이다.

| 타입 | arm64 libc++ (Apple clang 21) | aarch64 libstdc++ (g++ 15.2) | thumbv7em libstdc++ 14.2 |
|---|---|---|---|
| `Delegate<...>` | 16 | 16 | 8 |
| `FunctionRef<...>` | 16 | 16 | 8 |
| `std::function<...>` | 32 | 32 | **16** |
| 멤버 함수 포인터 | 16 | 16 | 8 |

Cortex-M 에서 콜백 슬롯 하나당 `Delegate` 8바이트, `std::function` 16바이트로 두 배다. (libc++ 를 Thumb 에 쓴 빌드는 이 환경에 없다.)

### 측정 — 할당 횟수 (전역 operator new 치환)

과제의 "malloc 훅"은 C++ 에서는 **전역 `operator new`/`operator delete` 치환**이 이식 가능한 대응이다 ([replacement.functions]). `malloc` 자체를 후킹할 필요가 없다.

```text
stdlib: libc++ 210106
callable in std::function       size   ctor   call   copy   move   bytes
function pointer                   8      0      0      0      0       0
captureless lambda                 1      0      0      0      0       0
capture 8 bytes (trivial)          8      0      0      0      0       0
capture 16 bytes (trivial)        16      0      0      0      0       0
capture 24 bytes (trivial)        24      1      0      1      0      32
capture 32 bytes (trivial)        32      1      0      1      0      40
capture 48 bytes (trivial)        48      1      0      1      0      56
8B capture, copy not noexcept      8      1      0      1      0      16
8B capture, copy noexcept          8      0      0      0      0       0
Delegate stored in function       16      0      0      0      0       0

Delegate alone: operator new calls = 0, delete calls = 0
```

VM g++ 15.2 (`-fno-assume-sane-operators-new-delete` 로 빌드, 이유는 아래):

```text
stdlib: libstdc++ 20260321
callable in std::function       size   ctor   call   copy   move   bytes
function pointer                   8      0      0      0      0       0
captureless lambda                 1      0      0      0      0       0
capture 8 bytes (trivial)          8      0      0      0      0       0
capture 16 bytes (trivial)        16      0      0      0      0       0
capture 24 bytes (trivial)        24      1      0      1      0      24
capture 32 bytes (trivial)        32      1      0      1      0      32
capture 48 bytes (trivial)        48      1      0      1      0      48
8B capture, copy not noexcept      8      1      0      1      0       8
8B capture, copy noexcept          8      1      0      1      0       8
Delegate stored in function       16      0      0      0      0       0

Delegate alone: operator new calls = 0, delete calls = 0
```

읽는 법:

- **"std::function 은 항상 힙을 쓴다"는 틀렸다.** 작은 trivial 캡처는 양쪽 다 0회다. 힙이 필요한 시점은 캡처 크기와 복사 특성이 정한다.
- 경계는 구현마다 다르다. 양쪽 다 64비트에서 16바이트까지 인라인이었다. **8바이트라도 복사 생성자가 trivial 하지 않으면** libstdc++ 는 힙으로 갔고, libc++ 는 `noexcept` 가 있으면 인라인이었다. 드라이버 코드가 이런 라이브러리 내부 규칙에 기대면 안 된다.
- 호출(`call`)은 어디서든 0회, 복사(`copy`)는 힙 캡처마다 1회다. **테이블을 복사하는 코드**가 숨은 할당 지점이다.
- Thumb 의 인라인 버퍼는 직접 재지 않았다 (`_Any_data` 크기로 8바이트 추정). 아래 ELF 측정에서 포인터 1개(4바이트) 캡처는 `operator new` 를 끌어오지 않았고 12바이트 캡처는 끌어왔다.

**측정 함정**: g++ 15.2 에서 플래그 없이 빌드하면 표가 이렇게 나왔다.

```text
capture 24 bytes (trivial)        24      0      1      1      0       0
```

`ctor` 가 0, `call` 이 1 — 호출에서 할당한다는 **거짓** 결과다. GCC 가 치환된 `operator new` 는 프로그램의 전역 변수를 건드리지 않는다고 가정해서(`-fassume-sane-operators-new-delete`) 카운터 읽기를 호출 앞으로 당겼기 때문으로 보인다. `-fno-assume-sane-operators-new-delete` 를 주자 위의 표처럼 정상이 됐다. 카운터 방식의 할당 측정을 믿기 전에 이 플래그를 확인한다.

### 측정 — 호출 어셈블리

`bash measure.sh asm` (clang, `-O2`, thumbv7em 에는 libstdc++ 헤더). 대상은 다른 TU 에 있다고 가정해 인라인이 불가능한 호출부다.

```text
=================== thumb
00001c6c <call_raw>:
    1c6c:      	mov	r3, r0
    1c6e:      	mov	r0, r1
    1c70:      	mov	r1, r2
    1c72:      	bx	r3
00001c74 <call_delegate>:
    1c74:      	ldr	r2, [r0]
    1c76:      	ldr	r3, [r0, #0x4]
    1c78:      	mov	r0, r2
    1c7a:      	bx	r3
00001c7c <call_ref>:
    1c7c:      	mov	r3, r1
    1c7e:      	mov	r1, r2
    1c80:      	bx	r3
    1c82:      	nop
00001c84 <call_function>:
    1c84:      	push	{r7, lr}
    1c86:      	sub	sp, #0x8
    1c88:      	ldr	r2, [r0, #0x8]
    1c8a:      	str	r1, [sp, #0x4]
    1c8c:      	cbz	r2, 0x1c98 <call_function+0x14> @ imm = #0x8
    1c8e:      	ldr	r2, [r0, #0xc]
    1c90:      	add	r1, sp, #0x4
    1c92:      	blx	r2
    1c94:      	add	sp, #0x8
    1c96:      	pop	{r7, pc}
    1c98:      	bl	0x1c98 <call_function+0x14> @ imm = #-0x4
```

arm64 호스트의 `_call_delegate` 는 `ldp x0, x2, [x0]` + `br x2` 두 명령어였다.

| Cortex-M4 호출부 | 명령어 수 | 특징 |
|---|---|---|
| C `fp(ctx, v)` | 4 | 레지스터 정리 + `bx` (꼬리 호출) |
| `Delegate` (참조로 받음) | 4 | 두 워드 로드 + `bx` |
| `FunctionRef` (값으로 받음) | 3 (+ 정렬용 `nop`) | 이미 레지스터에 있음 |
| `std::function` | 전체 11, 정상 경로 10 | 스택 프레임, 인자를 **메모리에 저장하고 주소 전달**, null 검사 + throw 분기 |

`std::function` 은 인자를 `Args&&` 로 호출하므로 `int` 하나도 스택에 내려서 포인터로 넘긴다(`str r1,[sp,#4]; add r1,sp,#4`). Delegate 는 값 그대로 레지스터로 넘긴다. 사이클 수는 재지 않았다 (맥에서 DWT 없음). 위는 명령어 수만이다.

스텁 자체도 본다. 대상 `Motor::set` 이 다른 TU 일 때:

```text
--- stub: ZN8DelegateIFviEE8obj_stubIXadL_ZN5Motor3setEiEES3_EEvNS1_3CtxEi
00000000 <_ZN8DelegateIFviEE8obj_stubIXadL_ZN5Motor3setEiEES3_EEvNS1_3CtxEi>:
       0:      	b.w	0x0 <_ZN8DelegateIFviEE8obj_stubIXadL_ZN5Motor3setEiEES3_EEvNS1_3CtxEi> @ imm = #-0x4
```

재배치 전 `.o` 라 자기 주소로 보일 뿐, 실제로는 `Motor::set` 으로 가는 꼬리 분기다.

호출 경로는 `call_delegate` → (`bx`) → 스텁 → (`b.w`) → `Motor::set`. **간접 분기 1회 + 직접 분기 1회**이고, 스텁은 인자를 건드리지 않는다. `std::invoke`, `std::forward`, `call_` 헬퍼는 `-O2` 에서 전부 사라졌다.

**"대상이 보이면?"** 이 경우는 예상과 달랐다.

```text
00001cac <known_delegate>:
    1cac:      	adds	r0, #0x1
    1cae:      	bx	lr
00001cb0 <known_function>:
    1cb0:      	adds	r0, #0x1
    1cb2:      	bx	lr
```

한 함수 안에서 생성하고 바로 호출하면 `std::function` 도 clang 이 전부 접어 버렸다. Delegate 만 인라인된다는 말은 **틀렸다**. 차이는 대상이 테이블·멤버처럼 **값이 런타임에 저장된 경우**에 나고, 그때는 위의 호출부 비교가 적용된다.

### 측정 — 링크 크기 (ELF, arm-none-eabi-g++ 14.2.1)

같은 4슬롯 테이블을 `demo_size.cpp` 로 링크했다 (`-O2 -ffunction-sections -fdata-sections -Wl,--gc-sections`, VM). 레슨의 solution 코드가 같이 컴파일되지만 `--gc-sections` 로 제거된다.

`function1` = 포인터 1개 캡처(SBO 안), `function2` = 12바이트 캡처(힙). 열은 `text data bss dec hex filename` 이다.

```text
    200	      0	     56	    256	    100	delegate-nosys.specs.elf  [nosys.specs]
    596	      8	    496	   1100	    44c	function1-nosys.specs.elf  [nosys.specs]
  15537	   1364	    912	  17813	   4595	function2-nosys.specs.elf  [nosys.specs]
    200	      0	     56	    256	    100	delegate-nano.specs.elf  [nano.specs]
    432	      4	     92	    528	    210	function1-nano.specs.elf  [nano.specs]
   1932	     84	    428	   2444	    98c	function2-nano.specs.elf  [nano.specs]
--- delegate .init_array:
(없음)
--- function1 .init_array:
  2 .init_array   00000008  00018254  00018254  00008254  2**2
--- function2 .init_array:
  4 .init_array   0000000c  0001bcb4  0001bcb4  0000bcb4  2**2
--- function2 (full libstdc++): heap/throw related
00008478 T operator new(unsigned int)
00008708 T __cxa_throw
0000adc8 T _malloc_r
```

`function1-nosys` 의 크기순 심볼 상위에는 `__atexit0`(0x190 = 400바이트 bss), `__register_exitproc`(0x64), `_GLOBAL__sub_I__Z13solution_mainv`(0x30) 가 있었다.

읽는 법 (숫자를 있는 그대로):

- `std::function` 자체가 15 KB 라는 뜻이 **아니다**. 힙을 쓰는 순간 `operator new` → `malloc` + 예외 경로가 링크되는 것이고, `nano.specs` 에서는 1.9 KB 로 줄어든다.
- 힙 없는 `std::function`(function1)도 `Delegate` 보다 크다. 관리 코드 + **전역 소멸자 등록** `__register_exitproc`/`__atexit0`(`nosys.specs` 기준 400바이트 bss) 때문이다. 펌웨어는 끝나지 않으므로 이 소멸자는 영원히 쓸모가 없다.
- `Delegate` 쪽은 `constinit` 이 되므로 `.init_array` 가 없고 **시작 코드가 전혀 필요 없다**. 스텁 하나는 8바이트였다 (`obj_stub<&Led::on_tick, Led>`). 나머지 `fn_stub<twice>` 4바이트는 solution 잔여물로, gc 되지 않은 이유는 미확인이다.
- 처음엔 링크가 실패했다: `undefined reference to '__dso_handle'` 와 `_fini`. `-nostartfiles` 로 crt 를 뺐기 때문이고, 데모에 스텁을 넣어서 해결했다. 실제 펌웨어에서 처음 `std::function` 전역을 두면 만나는 에러이기도 하다.
- `-ffreestanding` 이면 libstdc++ 에 `std::function` 자체가 없다: `error: no template named 'function' in namespace 'std'` (그래서 측정 스크립트는 hosted 모드). 또한 `-fno-rtti` 에서는 `target_type()` 이 사라진다: `error: no member named 'target_type'` (libc++ 실측).

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

## 7. 자주 하는 실수

**1. `T&` 로 임시 객체를 다 막았다고 믿는다.** `const Counter makeC(); bind<&Counter::get>(makeC())` 는 `T = const Counter` 로 붙어서 곧바로 dangling 이다.

```cpp
template <auto M, typename T> static Delegate bind(const T&&) = delete;   // rvalue 전용, lvalue 는 못 삼킨다
```

**2. 스텁에서 멤버 포인터의 클래스로 캐스트한다.** `Multi` 를 넣고 `BaseB*` 로 꺼내면 `this` 보정이 빠져서 `BaseA` 데이터를 읽는다. 저장한 타입 `T*` 로 되돌리고 변환은 `std::invoke` 에 맡긴다.

**3. 함수 포인터를 `void*` 에 넣는다.** conditionally-supported 다. `union` 에 따로 저장하고 스텁으로 활성 멤버를 구분한다.

**4. 메인에서 슬롯을 보호 없이 쓴다.** 2워드 대입 중간에 ISR 이 들어오면 새 `this` + 옛 스텁 조합이 호출된다 (`demo_torn.cpp`). `Guard` 안에서 쓰고 `active` 는 마지막에 켠다.

**5. `Slot` 멤버를 초기화하지 않고 전역에 둔다.** `demo_size.cpp` 초판에서 `struct Slot { Cb cb; std::uint32_t deadline; };` 를 전역에 두었더니 Delegate 빌드에도 `_GLOBAL__sub_I` 와 `.init_array` 가 생겼다. `deadline = 0` 을 주고 `constinit` 을 붙이니 사라졌다. 초기화되지 않은 멤버가 있으면 상수 초기화 대상이 아니다.

**6. 멤버 순서를 틀린다.** 생성자에서 `*this` 로 콜백을 등록할 때 콜백이 쓰는 멤버가 뒤에 있으면 초기화 전에 ISR 이 그 멤버를 만날 수 있다. 콜백이 쓰는 멤버를 타이머 멤버보다 **앞에** 선언한다 (`Blinker`).

**7. 할당 측정 값을 그대로 믿는다.** GCC 15 에서 `-fno-assume-sane-operators-new-delete` 없이 재면 거짓 결과가 나왔다 (5절).

## 8. 면접 Q&A

<details>
<summary>Q1. 펌웨어에서 std::function 을 왜 안 쓰나요? 힙 때문인가요?</summary>

힙만이 아니다. 측정 결과 작은 캡처는 힙을 안 썼다. 진짜 이유는 ① **언제 힙을 쓰는지가 라이브러리 내부 규칙**이라 코드만 봐서는 보장이 없고(8바이트 캡처도 복사 생성자가 비자명하면 libstdc++ 는 힙), ② 크기가 슬롯당 2배(Thumb 16 vs 8)이고, ③ 호출부가 스택 프레임 + null 검사 + 인자 메모리 전달로 길고, ④ 소유권 관리 때문에 전역에 두면 동적 초기화와 소멸자 등록이 따라오기 때문이다. 빈 `std::function` 호출은 throw 경로로 빠지고 `-fno-exceptions` 빌드에서는 프로그램 종료로 끝난다(실행해 보지는 않았다).

</details>

<details>
<summary>Q2. 멤버 함수 포인터를 그냥 저장하면 안 되나요?</summary>

클래스별로 타입이 달라서 공통 타입으로 저장할 수 없고, 측정상 2워드(Thumb 8바이트)다. 문맥까지 합치면 3워드가 된다. NTTP 로 넘기면 멤버 포인터는 스텁 코드에 상수로 들어가서 저장 공간이 0이고, 컴파일러가 스텁을 직접 호출로 최적화할 수 있다. 대가는 **대상이 컴파일 시점에 정해져야** 한다는 것이다. 런타임에 멤버 포인터를 고르는 일은 드물지만 필요하다면 별도의 슬롯이 필요하다.

</details>

<details>
<summary>Q3. Delegate 와 function_ref 는 뭐가 다르죠? 둘 다 포인터 두 개인데요.</summary>

표현은 거의 같고 **의도와 수명 규칙**이 다르다. `function_ref` 는 함수 **인자**로만 쓰는 뷰라 캡처 람다도 받고, 호출이 끝날 때까지 임시 객체가 살아 있으니 안전하다. 멤버로 저장하면 dangling 이다. `Delegate` 는 저장용이라 임시 객체를 지워서 막고, 캡처 람다는 받지 않는다(받으면 임시 객체 저장이 쉽게 일어난다). 면접에서는 "저장하느냐 안 하느냐"로 정리한다.

</details>

<details>
<summary>Q4. ISR 에서 Delegate 를 호출해도 되나요? 메인이 그 슬롯을 바꾸면요?</summary>

호출 자체는 ISR-safe 다 (힙 없음, 잠금 없음). 문제는 **갱신**이다. 8바이트 대입은 인터럽트로 쪼개질 수 있어서 새 `this` + 옛 스텁 조합이 호출될 수 있다. 메인 쪽 쓰기를 PRIMASK 가드로 감싸거나, 슬롯의 `active` 를 끄고 쓴 뒤 마지막에 켠다. 이 레슨 구현은 둘 다 한다.

꼬리질문 "`std::atomic<Delegate>` 면 되지 않나?" — 8바이트 lock-free 는 Cortex-M4 에서 지원되지 않는다 (LDREXD 없음, CPP-A04 참고). 포인터 하나로 줄이려면 슬롯을 가리키는 포인터만 원자적으로 교체하는 구조로 바꾼다.

</details>

<details>
<summary>Q5. 대상 객체가 먼저 죽으면요? 어떻게 막습니까?</summary>

Delegate 자체는 못 막는다. ① 객체를 정적 수명으로 두거나(임베디드 기본), ② `ScopedTimer` 처럼 RAII 로 해제를 소멸자에 묶거나, ③ 핸들 + generation 으로 "이미 죽은 대상"을 런타임에 검출한다. `[[clang::lifetimebound]]` 는 간단한 반환 탈출만 잡았고, ASan 은 `-O0` 에서만 잡았다(실측). 도구는 보조 수단이고 소유 구조로 해결한다.

</details>

<details>
<summary>Q6. cancel() 이 true 를 돌려주면 콜백이 더 안 돈다고 장담합니까?</summary>

**단일 코어**라면 그렇다. ISR 이 메인을 선점하는 구조에서 메인의 `cancel` 이 끝났다는 것은 ISR 이 이미 끝났다는 뜻이고, 쓰기는 가드 안에서 일어난다. **멀티코어**(또는 다른 코어/스레드에서 `tick`)라면 콜백이 지금 다른 코어에서 실행 중일 수 있다. 그래서 리눅스에는 끝날 때까지 기다리는 `del_timer_sync`(최신 이름 `timer_delete_sync`)가 따로 있다. 콜백 안에서 `*_sync` 를 부르면 교착이다.

</details>

<details>
<summary>Q7. Delegate 두 개가 같은지 비교할 수 있나요? 함정은요?</summary>

`{ctx, stub}` 비교로 된다. 함정은 **스텁 주소의 동일성**이다. 표준상 다른 함수는 주소가 달라야 하지만, 링커의 공격적 ICF(동일 코드 병합, lld/gold `--icf=all`)는 코드가 같은 스텁을 합칠 수 있어서 서로 다른 바인딩이 같아 보일 수 있다. `--icf=safe` 는 주소를 취한 함수를 합치지 않는다. 반대로 `from_ptr(f)` 와 `bind<&f>()` 는 대상이 같아도 다른 Delegate 다 (테스트로 고정함).

</details>

<details>
<summary>Q8. 바인딩마다 스텁이 생기면 코드 부풀림이 있지 않나요?</summary>

있다. 다만 실측에서 스텁은 `-O2` 로 8바이트(작은 대상은 대상 코드가 인라인)였고, 대상이 다른 TU 면 `b.w` 하나였다. 바인딩 `(대상, 클래스)` 조합 수만큼만 늘어나고 객체 수에는 비례하지 않는다. 같은 클래스 인스턴스 100개를 묶어도 스텁은 하나다. `std::function` 은 대신 타입당 manager + invoker 를 만든다.

</details>

## 9. 추가 연습

아래 답을 모두 담은 코드는 `practice.cpp` 다.

**1. `MultiDelegate<void(A...), N>`** — 고정 용량 멀티캐스트. `add`/`remove`/`fire`, 중복 거부, 콜백 안에서 자기 제거해도 다음 구독자를 건너뛰지 않기.

<details>
<summary>답</summary>

`remove` 는 **자리를 비우기만** 하고 뒤를 당기지 않는다. 당기면 순회 중 다음 원소를 건너뛴다. `fire` 는 슬롯을 로컬로 복사해서 호출한다. 인자는 `std::forward` 하지 **않는다**. 구독자가 여럿이라 첫 호출이 인자를 이동해 가면 나머지는 moved-from 값을 받는다.

</details>

**2. C HAL 다리** — `void hal_dma_start(void (*cb)(void*, int), void* arg)` 에 멤버 함수를 연결한다.

<details>
<summary>답</summary>

정적 트램펄린 + `arg = &done_`(Delegate 멤버의 주소). `arg` 로 `App*` 를 넘기면 또 손으로 캐스트하게 된다. 트램펄린은 하나, 타입 안전성은 Delegate 쪽이 책임진다. 테스트는 `g_hal_arg != &app` 도 확인한다.

</details>

**3. ROM 디스패치 테이블** — `constexpr std::array<Delegate<void()>, N>` 인터럽트 테이블을 만들고, 빠진 항목은 `static_assert` 로 막는다.

<details>
<summary>답</summary>

`bind(T&)` 가 `constexpr` 이고 대상이 정적 수명이면 `constexpr` 테이블이 된다. `consteval bool all_bound(...)` 로 `!f` 를 전수 검사한다. `const` 는 **얕다**. 테이블은 ROM 에 있어도 대상 객체는 수정 가능하다 (`g_usart1.hits` 증가). 실제 `.rodata` 배치 자체는 측정하지 않았고, ELF 측정의 `g_d_free` 는 `.rodata` 섹션이었다 (`--print-gc-sections` 출력).

</details>

**4. 힙 없는 재시도** — `retry(FunctionRef<bool()> op, unsigned max, FunctionRef<void(unsigned)> backoff)`.

<details>
<summary>답</summary>

`FunctionRef` 를 두 개 인자로 받으면 호출자는 캡처 람다를 그 자리에서 넘길 수 있고 호출이 끝나면 람다도 끝나므로 수명 문제가 없다. 마지막 시도 뒤에는 `backoff` 를 부르지 않는다.

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice
# 18/18 passed
```

## 10. 핵심 정리

- `Delegate` = `{ctx, stub}` 두 워드. C의 `void* ctx` 패턴에서 캐스트만 컴파일러에게 넘긴 것이다.
- 대상은 NTTP 로 스텁에 박는다. 멤버 포인터 저장 불필요, trivially copyable, `constinit` 가능, `.init_array` 없음.
- `T&` 만으로는 const 임시를 못 막는다. `bind(const T&&) = delete`.
- 저장은 `Delegate`, 인자는 `FunctionRef`, 소유가 필요한 호스트 코드는 `std::function`.
- 측정: Thumb 슬롯 8 vs 16바이트, 호출부 4 vs 10 명령어(정상 경로). 다만 "std::function 은 항상 힙"과 "절대 인라인 안 됨"은 둘 다 **틀렸다**.
- 2워드 대입은 원자적이지 않다. 메인 쪽은 가드 + `active` 마지막.
- 타이머: `(int32_t)(now - deadline) >= 0`, `deadline += k*period`, 복사 후 해제 후 호출, `{idx, gen}` 핸들.
- 수명은 Delegate 가 모른다. 정적 수명 또는 RAII 해제. ASan 이 조용하다고 안전한 것은 아니다.

## 참고자료

- P0792R14 `function_ref`: https://wg21.link/P0792R14
- P3774R1 Rename `std::nontype`: https://open-std.org/JTC1/SC22/WG21/docs/papers/2025/p3774r1.html
- cppreference replacement functions: https://en.cppreference.com/w/cpp/memory/new/operator_new
- 표준 조항: [expr.reinterpret.cast], [expr.prim.lambda.closure], [dcl.type.cv], [replacement.functions], [func.wrap.ref]
- GCC 옵션 `-fassume-sane-operators-new-delete`: https://gcc.gnu.org/onlinedocs/gcc/C_002b_002b-Dialect-Options.html
- ETL `etl::delegate`: https://www.etlcpp.com/delegate.html
- Sergey Ryazanov, "The Impossibly Fast C++ Delegates": https://www.codeproject.com/Articles/11015/The-Impossibly-Fast-C-Delegates
- 관련 레슨: CPP-B06 (RAII), CPP-B09 (static init), CPP-B13/B14 (예외·힙 금지), CPP-A04 (ISR 메모리 순서), MCPP-M07 (`std::function` 구현)
