---
id: CPP-B01
time: 50분
updated: 2026-11-07
---
# CPP-B01 C에서 C++로: 펌웨어용 C++ 부분집합

## 학습 목표

- C 드라이버를 C++ 컴파일러에 넣었을 때 나오는 에러 5종류(**암시적 `void*` 변환**, **C++ 키워드 식별자**, **지정 초기화(designated initializer) 제약**, **정수 → enum 변환**, **문자열 리터럴 const**)를 보고 바로 고칠 수 있다.
- 에러 없이 빌드되지만 **결과가 달라지는** C/C++ 차이(`sizeof('A')`, namespace 범위 `const` 의 링크, struct 태그 가림)를 안다.
- 범위 밖 정수를 enum 으로 바꾸는 것이 C++ 에서는 **UB** 이고, 상수식(constexpr)에서는 컴파일 에러로 드러난다는 것을 설명한다.
- 같은 드라이버의 C 빌드와 C++(`-fno-exceptions -fno-rtti`) 빌드를 `size` / `objdump` 로 비교해 **기계어가 같다**는 것을 직접 확인한다.

## 1. 문제

펌웨어 팀이 C++ 로 넘어갈 때 첫 작업은 언제나 "기존 C 드라이버를 C++ 컴파일러로 빌드되게 만들기"다. 면접에서도 "C 코드를 C++ 로 옮기면 뭐가 깨지나요? 코드가 커지나요?" 는 단골 질문이다 — 답이 **구체적인 에러 이름 + 실측 숫자**면 강하다.

C++ 는 C 의 "거의" 상위 집합이다. 그 "거의" 가 문제다. C 가 조용히 해 주던 변환을 C++ 는 거부하고(타입 안전성), 몇몇은 에러 없이 **의미가 바뀐다**. 펌웨어용 C++ 부분집합(subset)은 "C 에 타입 검사를 더하고, 비용이 숨은 기능(예외·RTTI·힙)은 빼는 것"이다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">ring_gpio.c 를 clang++ 에 넣으면 — C 관용구 → C++ 에러 → 고친 모양</text>
  <g stroke="currentColor" fill="none">
    <rect x="10" y="40" width="230" height="240" rx="8"/>
    <rect x="255" y="40" width="230" height="240" rx="8"/>
    <rect x="500" y="40" width="230" height="240" rx="8"/>
  </g>
  <rect x="10" y="40" width="230" height="26" rx="8" class="f-mut"/>
  <rect x="255" y="40" width="230" height="26" rx="8" class="f-warn"/>
  <rect x="500" y="40" width="230" height="26" rx="8" class="f-ok"/>
  <g fill="currentColor" font-weight="bold">
    <text x="20" y="58">C (C11 에서 OK)</text>
    <text x="265" y="58">clang++ 진단</text>
    <text x="510" y="58">C++ 로 고친 코드</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="92">① rb_t *rb = pool_alloc(..);</text>
    <text x="20" y="132">② uint32_t new; uint8_t class;</text>
    <text x="20" y="172">③ { .mode=.., .pin=5 }</text>
    <text x="20" y="190">   { [MODE_OUTPUT] = "out" }</text>
    <text x="20" y="226">④ return raw &amp; 3u; /* enum */</text>
    <text x="20" y="266">⑤ rb_create(16, "uart_rx");</text>
    <text x="265" y="92">cannot initialize 'rb_t *'</text>
    <text x="265" y="106">  with 'void *'</text>
    <text x="265" y="132">expected unqualified-id</text>
    <text x="265" y="172">-Wreorder-init-list</text>
    <text x="265" y="190">-Wc99-designator</text>
    <text x="265" y="226">cannot initialize</text>
    <text x="265" y="240">'gpio_mode_t' with unsigned</text>
    <text x="265" y="266">-Wwritable-strings</text>
    <text x="510" y="92">static_cast&lt;rb_t*&gt;(...)</text>
    <text x="510" y="106">또는 pool_new&lt;rb_t&gt;()</text>
    <text x="510" y="132">next, cls 로 이름 변경</text>
    <text x="510" y="172">{ .pin=5, .mode=.. } 선언 순</text>
    <text x="510" y="190">{ "in","out",.. } + assert</text>
    <text x="510" y="226">static_cast&lt;gpio_mode_t&gt;(</text>
    <text x="510" y="240">  raw &amp; 3u)</text>
    <text x="510" y="266">const char *name</text>
  </g>
</svg>
```

데모 파일 `ring_gpio.c` (C11 로는 경고 0개) 를 그대로 C++ 로 컴파일한 **실제 출력**이다 (`-Werror` 라 경고도 에러로 승격):

```text
$ c++ -x c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti -c ring_gpio.c
ring_gpio.c:29:11: error: cannot initialize a variable of type 'rb_t *' with an rvalue of type 'void *'
ring_gpio.c:31:15: error: assigning to 'uint8_t *' (aka 'unsigned char *') from incompatible type 'void *'
ring_gpio.c:39:14: error: expected unqualified-id                       ← uint32_t new = ...
ring_gpio.c:40:13: error: expected a type
ring_gpio.c:42:19: error: expected a type
ring_gpio.c:59:17: error: declaration of anonymous class must be a definition   ← uint8_t class;
ring_gpio.c:59:5: error: declaration does not declare anything [-Werror,-Wmissing-declarations]
ring_gpio.c:65:62: error: expected a field designator, such as '.field = 4'     ← .class = 1
ring_gpio.c:69:5: error: array designators are a C99 extension [-Werror,-Wc99-designator]
ring_gpio.c:78:12: error: cannot initialize return object of type 'gpio_mode_t' with an rvalue of type 'unsigned int'
ring_gpio.c:82:30: error: assigning to 'gpio_mode_t' from incompatible type 'unsigned int'
ring_gpio.c:86:51: error: ISO C++11 does not allow conversion from string literal to 'char *' [-Werror,-Wwritable-strings]
12 errors generated.
```

`.class` 를 `cls` 로 바꾸고 나면 그 뒤에 숨어 있던 진단이 하나 더 나온다:

```text
ring_gpio.c:65:58: warning: ISO C++ requires field designators to be specified in declaration order;
      field 'mode' will be initialized after field 'pin' [-Wreorder-init-list]
```

### Part A — C 관용구 고치기 (starter.cpp 의 A1~A4)

`ring_gpio.c` 를 C++ 로 옮긴 `starter.cpp` 에서 TODO 를 채운다.

- **A1** `template <typename T> T *pool_new()` — `void*` 를 돌려주는 C API `pool_alloc(n, align)` 을 감싸 **타입 있는** 할당을 만든다. 캐스트는 이 한 곳에만. placement new 로 `T{}` (0 초기화)를 만든다. 풀이 모자라면 `nullptr`.
- **A2** `rb_create(size, const char *name)` — `pool_new<rb_t>()` 와 `static_cast<uint8_t*>(pool_alloc(size, 1))` 로 링버퍼를 만든다.
- **A3** `constexpr gpio_cfg_t led_cfg` — 지정 초기화를 **선언 순서대로**: pin=5, mode=MODE_OUTPUT, cls=1.
- **A4** `kModeNames[]` — C 의 배열 지정자 `[MODE_OUTPUT] = "out"` 대신 enum 순서대로 쓰고, 길이가 어긋나면 `static_assert` 가 빌드를 깨게.

### Part B — enum ↔ 정수, 경계 조건

- **B1** `mode_from_bits(uint32_t raw)` — MODER 에서 밀어낸 값엔 **위쪽 쓰레기 비트**가 있다. C 는 `return raw;` 로 끝났지만 C++ 은 `static_cast` 가 필요하고, `raw` 가 0..3 밖이면 **UB** 다. `constexpr` 로 만들어 테스트의 `constexpr gpio_mode_t ce = mode_from_bits(7);` 가 컴파일되게.
- **B2** `next_mode(m)` — C 의 `c->mode++` (C++ 에서 에러) 대체. ANALOG 다음은 INPUT 으로 돈다.
- **A2 경계**: `size` 가 0 이거나 2의 거듭제곱이 아니면, 또는 풀(256 B)이 모자라면 `nullptr`.

### Part C — mock GPIO + UART RX "ISR"

- **C1** `gpio_init(g, cfg)` — MODER 의 핀당 2비트 필드에 mode 를 쓴다 (다른 핀 보존, C-B01 참고).
- **C2** `uart_rx_isr(u, g, byte)` — 받은 바이트를 링버퍼에 넣고, 가득 찼으면 `overruns++` 후 `led_cfg.pin` 의 에러 LED 를 **BSRR 한 번 쓰기**로 켠다.
- **C3** `uart_read(u, dst, n)` — 메인 루프에서 최대 n 바이트를 꺼낸다.

그리고 데모로 같은 드라이버의 **C 빌드 vs C++ 빌드**를 `size` / `objdump` 로 비교한다 (`build.sh`, 5절).

## 2. Problem (English)

> **Port a C driver to the embedded C++ subset.**
>
> You are given `ring_gpio.c`, a C11 ring buffer + GPIO driver that uses a static memory pool (no heap). It fails to compile with `clang++ -std=c++20 -fno-exceptions -fno-rtti -Werror`.
>
> 1. Fix it so that it builds as C++ without changing behavior: implicit `void*` conversions, identifiers that are C++ keywords (`new`, `class`), out-of-order and array designated initializers, implicit integer-to-enum conversions, and string literals bound to `char*`.
> 2. Implement `template<class T> T* pool_new()` on top of `void* pool_alloc(size_t n, size_t align)`, `rb_create(uint32_t size, const char* name)` (reject size 0 / non-power-of-two / pool exhaustion), `constexpr gpio_mode_t mode_from_bits(uint32_t raw)` that is well-defined for any `raw`, and `next_mode()` with wrap-around.
> 3. Implement a UART RX ISR that pushes into the ring buffer, counts overruns, and lights an error LED with a single BSRR write; and `uart_read()` for the main loop.
> 4. Compare `size` and `objdump -d` of the C build and the C++ build. Are they the same? What differs, and why?
>
> Example: `mode_from_bits(0xFFFFFFFD) == MODE_OUTPUT`; `next_mode(MODE_ANALOG) == MODE_INPUT`; `rb_create(12, "x") == nullptr`.

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크 깨짐 회피
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
# 처음엔 FAIL 19줄 + "5/24 passed" — TODO 를 채워 24/24 를 만든다
```

데모 파일 (이 레슨 폴더):

| 파일 | 내용 | 명령 |
|---|---|---|
| `ring_gpio.c` | 원본 C 드라이버 (C 로 OK, C++ 로 12 에러) | `cc -std=c11 -Wall -Wextra -Werror -O2 -c ring_gpio.c` |
| `ring_gpio_port.cpp` | 최소 수정 C++ 포팅 (로직 동일) | `c++ -std=c++20 ... -c ring_gpio_port.cpp` |
| `build.sh` | 두 빌드의 size / objdump / nm 비교 | `sh build.sh` |
| `demo_silent.c` | 둘 다 빌드되지만 결과가 다른 코드 | 파일 첫 줄 주석 참고 |
| `demo_show_error.cpp` | 고친 버전 / `-DSHOW_ERROR` 로 C 습관 에러 재현 | 파일 첫 줄 주석 참고 |

## 4. 힌트

<details>
<summary>힌트 1 — void* 는 "어디로든" 가지 않는다</summary>

C 는 `void*` → `T*` 를 암시적으로 바꿔 준다 (C11 6.3.2.3). C++ 은 반대 방향(`T*` → `void*`)만 암시적이다. `static_cast<T*>(p)` 가 정답 캐스트이고, 여러 곳에 흩어진 캐스트는 템플릿 하나(`pool_new<T>`)로 모은다. `alignof(T)` 를 넘기는 것도 잊지 말 것.

</details>

<details>
<summary>힌트 2 — placement new 는 힙이 아니다</summary>

`::new (p) T{}` 는 "이미 있는 메모리 p 에 T 객체를 만든다". `<new>` 의 placement `operator new(size_t, void*)` 는 `noexcept` 이고 `p` 를 그대로 돌려줄 뿐이라 할당도, 예외도 없다. `T{}` 는 집합체(aggregate)를 0 으로 초기화한다 — 풀의 0xA5 쓰레기가 남지 않는다.

</details>

<details>
<summary>힌트 3 — C++20 지정 초기화의 4가지 제약</summary>

① 선언 순서대로 ② 직접 멤버만 (`.a.b =` 중첩 X) ③ 배열 지정자 `[i] =` X ④ 지정/비지정 섞기 X. 빠진 멤버는 0 (또는 기본 멤버 초기화값). 그리고 중괄호 초기화라 **좁히는 변환(narrowing)** 은 에러.

</details>

<details>
<summary>힌트 4 — enum 의 "값 범위"</summary>

기저 타입을 고정하지 않은 `enum { A, B, C, D }` 의 값 범위는 0..3 (모든 enumerator 를 담는 최소 비트 폭 = 2비트). 그 밖의 값을 `static_cast` 하면 UB. 그러니 캐스트 **전에** `& 3u` 로 범위를 맞춘다. constexpr 함수로 만들면 컴파일러가 상수식에서 UB 를 잡아 준다.

</details>

<details>
<summary>힌트 5 — ISR 은 짧게, RMW 없이</summary>

`rb_push` 가 false 면 overrun. LED 는 `g->BSRR = 1u << led_cfg.pin;` 한 번이면 된다 (C-B01 의 BSRR). ODR 반영은 테스트의 `sim_apply_bsrr()` 가 하드웨어 역할을 한다.

</details>

## 5. 풀이

### Part A — 에러 5종류와 각각의 C++ 처방

| # | C 코드 | 왜 C++ 에서 막히나 | 처방 | 런타임 비용 |
|---|---|---|---|---|
| ① | `rb_t *rb = pool_alloc(...)` | `void*` → `T*` 암시적 변환 없음 ([conv.ptr], Annex C [diff.conv]) | `static_cast<T*>` / `pool_new<T>()` | 0 (캐스트는 컴파일 타임) |
| ② | `uint32_t new;` `uint8_t class;` | `new`, `class`, `template`, `this`, `delete`, `private`, `namespace`... 는 키워드 ([diff.lex]) | 이름 변경 (`next`, `cls`) | 0 |
| ③ | `{ .mode=.., .pin=5 }`, `[i] = ..` | C++20 지정 초기화는 선언 순서·직접 멤버만 ([dcl.init.aggr]/3, [diff.dcl]) | 순서 맞추기, 배열은 순서대로 + `static_assert` | 0 |
| ④ | `gpio_mode_t m = raw & 3u;` `m++` | 정수 → enum 암시적 변환 없음, enum 에 `++` 없음 ([diff.dcl]) | `static_cast<gpio_mode_t>(...)` | 0 |
| ⑤ | `char *name = "uart_rx";` | 리터럴 타입이 `const char[8]` — C++11 부터 `char*` 변환 제거 ([diff.lex]) | `const char *` | 0 |

**① 왜 C 에서는 "malloc 결과를 캐스트하지 마라" 였나?** C89 에서 `<stdlib.h>` 를 빼먹으면 `malloc` 이 암시적으로 `int` 반환 함수로 선언되는데, 캐스트가 그 경고를 덮었기 때문이다. C++ 에는 암시적 함수 선언이 없으므로 그 걱정이 없고, 대신 타입 검사를 위해 캐스트를 **요구**한다. C 스타일 `(rb_t*)p` 도 통하지만, C 캐스트는 `const_cast`/`reinterpret_cast` 까지 조용히 해 버리므로 `static_cast` 로 의도를 좁힌다.

**placement new 를 쓰는 이유 (A1).** `static_cast<rb_t*>(pool_alloc(...))` 만으로도 모든 컴파일러에서 동작하고, `ring_gpio_port.cpp` 도 그렇게 했다(C 와 동일 코드 비교용). 하지만 표준 문언으로는 "그 바이트 위에 `rb_t` 객체가 살아 있는가(lifetime)" 가 애매하다 — C++20 의 암시적 객체 생성([intro.object]/10–13)은 `malloc` 같은 지정된 연산에만 "적절한 객체를 가리키는 포인터"를 보장하고, 직접 만든 풀 함수는 해당하지 않는다(C++23 은 `std::start_lifetime_as` 를 추가). `::new (p) T{}` 는 이 논쟁을 끝낸다: 객체 수명을 시작하고 그 객체를 가리키는 포인터를 돌려준다. 비용은 0 초기화 store 몇 개뿐이고, `operator new` (힙)는 호출되지 않는다 (CPP-B14 참고).

**③ 지정 초기화 — C99 vs C++20.**

| 형태 | C99/C11 | C++20 표준 | clang++ (확장) |
|---|---|---|---|
| `{ .pin=5, .mode=M }` (선언 순) | OK | OK | OK |
| `{ .mode=M, .pin=5 }` (순서 바뀜) | OK | ill-formed | 경고 `-Wreorder-init-list` |
| `{ [2] = x }` 배열 지정자 | OK | 없음 | 경고 `-Wc99-designator` |
| `{ .in.x = 3 }` 중첩 | OK | 없음 | 경고 `-Wc99-designator` |
| `{ .a = 1, 2 }` 섞기 | OK | 없음 | 경고 `-Wc99-designator` |
| `{ .pin = pin_var }` (unsigned → uint8_t) | 조용히 잘림 | narrowing 에러 | 에러 |

위 clang 경고는 직접 확인한 것이다 (`-std=c++17` 에서는 `-pedantic` 을 줘야 `designated initializers are a C++20 extension [-Wc++20-designator]` 가 나온다 — 과제 문구의 `-std=c++17` 로도 이 레슨 코드는 빌드되지만 표준 기능은 C++20 부터다). clang 이 "경고" 로 봐준다고 끄면 안 된다: GCC(arm-none-eabi-g++)는 순서가 바뀐 지정 초기화를 **에러**로 처리한다 (이 맥에서는 돌려보지 못했다 — 기대치). 마지막 행은 진짜 이점이다: C 는 `uint8_t pin` 에 300 을 넣어도 말없이 44 로 자르지만 C++ 은 막는다:

```text
n.cpp:3:49: error: non-constant-expression cannot be narrowed from type 'unsigned int' to 'std::uint8_t'
      (aka 'unsigned char') in initializer list [-Wc++11-narrowing]
```

배열 지정자의 장점("순서 무관, 빠지면 NULL") 을 잃는 대신, C++ 에서는 `static_assert(std::size(kModeNames) == MODE_ANALOG + 1)` 로 **개수**를 지키고, 순서까지 지키고 싶으면 `switch` + `-Wswitch` (추가 연습 4) 를 쓴다.

### Part B — 정수 → enum 은 "범위" 를 생각한다

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">enum 의 값 범위 ([dcl.enum]/8) — static_cast 가 정의되는 구간</text>
  <text x="10" y="62" fill="currentColor">enum gpio_mode_t { 4개 }</text>
  <text x="10" y="80" fill="currentColor" opacity=".8">기저 타입 미지정</text>
  <rect x="200" y="46" width="120" height="30" class="f-ok" stroke="currentColor"/>
  <rect x="320" y="46" width="400" height="30" class="f-warn" stroke="currentColor"/>
  <text x="212" y="66" fill="currentColor" font-family="monospace">0 1 2 3</text>
  <text x="340" y="66" fill="currentColor">4 ~ … : static_cast 하면 UB (상수식이면 컴파일 에러)</text>
  <text x="10" y="132" fill="currentColor">enum gpio_mode8_t : uint8_t</text>
  <text x="10" y="150" fill="currentColor" opacity=".8">기저 타입 고정</text>
  <rect x="200" y="116" width="520" height="30" class="f-ok" stroke="currentColor"/>
  <text x="212" y="136" fill="currentColor" font-family="monospace">0 1 2 3  4 … 255  ← 전부 유효한 값 (이름은 없어도 됨)</text>
  <text x="10" y="190" fill="currentColor">처방: 캐스트 전에 범위를 맞춘다 → static_cast&lt;gpio_mode_t&gt;(raw &amp; 3u)</text>
  <text x="10" y="212" fill="currentColor">또는 기저 타입 고정 + is_valid() 검사 (enum class 는 CPP-B03)</text>
</svg>
```

C 에서 enum 은 그냥 `int` 호환 정수라 `mode = 7` 도 정상이다. C++ 은 다르다:

- 기저 타입을 **고정하지 않은** enum 의 값은 "모든 enumerator 를 담는 최소 비트 폭" 의 범위다. `{0,1,2,3}` 이면 2비트 → 0..3.
- 그 밖의 값을 `static_cast` 하면 **UB** ([expr.static.cast]/10, C++17 CWG 1766 부터 명시). 컴파일러는 "mode 는 0..3" 이라 가정하고 `switch` 의 범위 검사를 빼 버릴 수 있다.
- 다행히 **상수식(constant expression) 안에서는 UB 가 허용되지 않으므로** 컴파일러가 잡는다 (`demo_show_error.cpp -DSHOW_ERROR` 실제 출력):

```text
demo_show_error.cpp:13:23: error: constexpr variable 'bad' must be initialized by a constant expression
   13 | constexpr gpio_mode_t bad = static_cast<gpio_mode_t>(7);     // [dcl.enum]/8 범위 밖 → UB
demo_show_error.cpp:13:29: note: integer value 7 is outside the valid range of values [0, 3] for the enumeration type 'gpio_mode_t'
```

그래서 B1 을 `constexpr` 로 만들고 테스트에서 `constexpr gpio_mode_t ce = mode_from_bits(7);` 를 둔다 — `& 3u` 를 빼먹으면 **빌드가 실패**한다. 테스트가 런타임이 아니라 컴파일 타임에 UB 를 잡는 C++ 다운 방법이다. B2 도 같은 이유로 `(m + 1u) & 3u` 로 돈다 — `MODE_ANALOG + 1 = 4` 를 그대로 캐스트하면 UB.

**에러 없이 의미가 바뀌는 것들** (`demo_silent.c` 를 C 와 C++ 로 각각 빌드한 실제 출력):

```text
[C]  cc -std=c11 ...                  [C++]  c++ -x c++ -std=c++20 ...
sizeof('A')   = 4                     sizeof('A')   = 1
sizeof(true)  = 4                     sizeof(true)  = 1
frame_size()  = 64                    frame_size()  = 2
```

| 차이 | C | C++ | 근거 |
|---|---|---|---|
| 문자 리터럴 `'A'` | `int` | `char` | [diff.lex] |
| `true` | C11: `<stdbool.h>` 매크로 `1` (int) — C23 부터는 `bool` | `bool` | C11 7.18, [lex.bool] |
| 지역 `struct frame` 이 전역 `frame[64]` 을 가림 | 태그는 별도 이름공간 → 배열 | 클래스 이름이 가림 → struct | [diff.class] |
| namespace 범위 `const T x = ..;` | 외부 링크 | **내부 링크** | [basic.link]/3, [diff.basic] |
| `int x; int x;` (tentative definition) | OK | 재정의 에러 | [diff.basic] |
| `goto` 가 초기화를 건너뜀 | OK | 에러 | [stmt.dcl], [diff.stat] |

const 링크 차이는 실측으로 확인했다 — `ring_gpio_port.cpp` 를 `-DNAIVE_CONST` (extern 없이) 로 빌드하면:

```text
ring_gpio_port.cpp:71:23: warning: unused variable 'led_cfg' [-Wunused-const-variable]
ring_gpio_port.cpp:74:24: warning: unused variable 'mode_names' [-Wunused-const-variable]
    led_cfg / mode_names 심볼 없음 (내부 링크 → 미사용 → 제거)
```

C 에서는 다른 파일이 `extern const gpio_cfg_t led_cfg;` 로 가져다 쓰던 보정 테이블이, C++ 에서는 **심볼이 사라져 링크 에러**가 된다. 처방: 정의에도 `extern` 을 붙이거나, 정의 파일이 `extern` 선언이 있는 자기 헤더를 먼저 include 한다 (앞선 선언의 링크를 따른다). C++17 이면 헤더에 `inline constexpr` 도 방법이다. 나머지 tentative definition / goto 에러의 실제 출력은 추가 연습 2와 `demo_show_error.cpp` 에 있다.

### Part C — ISR 과 메인 루프 (단일 생산자 / 단일 소비자)

```svg
<svg viewBox="0 0 740 220" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">UART RX: ISR 이 head 를, 메인 루프가 tail 을 움직인다 (크기 4 → 3칸 사용)</text>
  <g stroke="currentColor">
    <rect x="220" y="50" width="60" height="40" class="f-acc"/>
    <rect x="280" y="50" width="60" height="40" class="f-acc"/>
    <rect x="340" y="50" width="60" height="40" class="f-acc"/>
    <rect x="400" y="50" width="60" height="40" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="250" y="75">'O'</text><text x="310" y="75">'K'</text><text x="370" y="75">'\n'</text><text x="430" y="75">빈칸</text>
  </g>
  <text x="225" y="112" fill="currentColor">↑ tail (메인: uart_read → rb_pop)</text>
  <text x="400" y="40" fill="currentColor">↓ head (ISR: rb_push)</text>
  <rect x="10" y="50" width="190" height="40" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="75" fill="currentColor">UART RX 인터럽트</text>
  <rect x="490" y="130" width="240" height="70" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="500" y="152" fill="currentColor">4번째 바이트 '!' → next == tail</text>
  <text x="500" y="172" fill="currentColor">→ push 실패, overruns++</text>
  <text x="500" y="192" fill="currentColor" font-family="monospace">GPIOA-&gt;BSRR = 1u &lt;&lt; 5</text>
  <text x="10" y="152" fill="currentColor">full 판정: (head+1) &amp; mask == tail</text>
  <text x="10" y="174" fill="currentColor">empty 판정: head == tail</text>
  <text x="10" y="196" fill="currentColor">한 칸을 비워 둬서 full/empty 를 구분한다</text>
</svg>
```

- `uart_rx_isr` 는 `head` 만, `uart_read` 는 `tail` 만 쓴다 → 단일 생산자/단일 소비자(SPSC) 링버퍼는 락 없이 쓸 수 있다. 단, 실제 칩에서는 `head`/`tail` 에 `volatile` 또는 `std::atomic` + 메모리 순서가 필요하다 (호스트 테스트에선 단일 스레드라 생략 — C-track volatile 레슨, 이후 CPP Mid 레벨 atomic 참고).
- 에러 LED 는 `BSRR` 단일 store — ISR 안에서 `ODR |=` 같은 RMW 를 하면 메인 루프의 GPIO 조작과 경쟁한다 (C-B01 참고).
- `const char *msg = "OK\n";` 처럼 C++ 에서는 리터럴을 받는 포인터가 자연스럽게 `const` 다. 플래시(.rodata)에 있는 리터럴에 쓰려는 코드는 이제 컴파일 단계에서 막힌다.

### 데모 — C 빌드 vs C++ 빌드: `size` / `objdump` 비교

`sh build.sh` 의 실제 출력 (임시 경로만 줄임):

```text
== 3) size (__TEXT / __DATA 합계, others = __LD,__compact_unwind)
__TEXT	__DATA	__OBJC	others	dec	hex
466	296	0	256	1018	3fa	c.o          ← cc -std=c11 -O2
466	296	0	0	762	2fa	cpp.o        ← c++ -std=c++20 -O2 -fno-exceptions -fno-rtti
466	296	0	256	1018	3fa	cpp_exc.o    ← c++ -std=c++20 -O2 (예외/RTTI 기본값)
440	264	0	0	704	2c0	cpp_naive.o  ← extern 없는 const 전역 (-DNAIVE_CONST)
== 4) size -m (섹션별)
--- c.o                                   --- cpp.o
	Section (__TEXT, __text): 432           	Section (__TEXT, __text): 432
	Section (__DATA, __bss): 264 (zerofill) 	Section (__DATA, __bss): 264 (zerofill)
	Section (__TEXT, __const): 12           	Section (__TEXT, __const): 12
	Section (__TEXT, __cstring): 22         	Section (__TEXT, __cstring): 22
	Section (__DATA, __const): 32           	Section (__DATA, __const): 32
	Section (__LD, __compact_unwind): 256   	total 762
	total 1018
== 5) objdump -d: 심볼 이름만 빼고 명령어 비교
    C   instructions:      108
    C++ instructions:      108
    IDENTICAL
```

(4번은 두 출력을 보기 좋게 나란히 놓았다.) 함수 하나를 직접 보면:

```text
000000000000009c <_rb_push>:                 000000000000009c <__Z7rb_pushP4rb_th>:
      9c:  ldp   w8, w9, [x0, #0x8]                9c:  ldp   w8, w9, [x0, #0x8]
      a0:  add   w10, w8, #0x1                     a0:  add   w10, w8, #0x1
      a4:  ldr   w11, [x0, #0x10]                  a4:  ldr   w11, [x0, #0x10]
      a8:  and   w10, w10, w11                     a8:  and   w10, w10, w11
      ac:  cmp   w10, w9                           ac:  cmp   w10, w9
      b0:  b.eq  0xc0                              b0:  b.eq  0xc0
      b4:  ldr   x11, [x0]                         b4:  ldr   x11, [x0]
      b8:  strb  w1, [x11, x8]                     b8:  strb  w1, [x11, x8]
      bc:  str   w10, [x0, #0x8]                   bc:  str   w10, [x0, #0x8]
      c0:  cmp   w10, w9                           c0:  cmp   w10, w9
      c4:  cset  w0, ne                            c4:  cset  w0, ne
      c8:  ret                                     c8:  ret
```

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">size -m 실측 (바이트) — 코드·데이터는 같고, 다른 건 unwind 메타데이터뿐</text>
  <text x="10" y="62" fill="currentColor">C  (c.o)</text>
  <text x="10" y="112" fill="currentColor">C++ no-exc</text>
  <text x="10" y="162" fill="currentColor">C++ 기본</text>
  <g stroke="currentColor">
    <rect x="110" y="45" width="216" height="26" class="f-acc"/>
    <rect x="326" y="45" width="132" height="26" class="f-mut"/>
    <rect x="458" y="45" width="34" height="26" class="f-ok"/>
    <rect x="492" y="45" width="128" height="26" class="f-warn"/>
    <rect x="110" y="95" width="216" height="26" class="f-acc"/>
    <rect x="326" y="95" width="132" height="26" class="f-mut"/>
    <rect x="458" y="95" width="34" height="26" class="f-ok"/>
    <rect x="110" y="145" width="216" height="26" class="f-acc"/>
    <rect x="326" y="145" width="132" height="26" class="f-mut"/>
    <rect x="458" y="145" width="34" height="26" class="f-ok"/>
    <rect x="492" y="145" width="128" height="26" class="f-warn"/>
  </g>
  <g fill="currentColor">
    <text x="180" y="63">__text 432</text><text x="350" y="63">__bss 264</text>
    <text x="630" y="63">unwind 256</text>
    <text x="180" y="113">__text 432</text><text x="350" y="113">__bss 264</text>
    <text x="500" y="113">unwind 0</text>
    <text x="180" y="163">__text 432</text><text x="350" y="163">__bss 264</text>
    <text x="630" y="163">unwind 256</text>
    <text x="10" y="205">초록 = const 데이터 66 (__TEXT,__const 12 + __cstring 22 + __DATA,__const 32), 폭은 대략 비례</text>
    <text x="10" y="228">unwind = __LD,__compact_unwind: 링크 때 __unwind_info 로 바뀌는 메타데이터 (실행 코드 아님)</text>
  </g>
</svg>
```

해석:

1. **`__text` 432 B, 명령어 108개가 1:1 동일.** 캐스트, 이름 변경, 지정 초기화 순서, `const char*` 는 모두 **컴파일 타임 개념**이라 기계어에 흔적이 없다. 다른 것은 심볼 이름뿐이다 — `_rb_push` vs `__Z7rb_pushP4rb_th` (맹글링, Mach-O 는 앞에 `_` 하나 더. CPP-B02 참고).
2. **`others` 256 B 의 정체**: `__LD,__compact_unwind`. `cc -### ` 로 보면 C 드라이버는 맥에서 `-funwind-tables=1` 을 기본으로 넘기고, C++ 에 `-fno-exceptions` 를 주면 이 플래그가 빠진다 (`-fno-exceptions -fasynchronous-unwind-tables` 를 주면 256 이 돌아오는 것도 확인했다). 즉 이 실험에서는 **C++ -fno-exceptions 쪽이 오히려 더 작다**. 반대로 예외를 켠 C++ 기본 빌드는 C 와 같은 256 B. 이 파일엔 throw 도 RTTI 도 없어서 차이가 그 정도다 — 예외·RTTI·typeinfo 가 실제로 있을 때의 비용은 CPP-B13 에서 잰다.
3. **NAIVE_CONST** 가 26 B(`__TEXT`) + 32 B(`__DATA`) 작아진 건 "최적화" 가 아니라 `led_cfg`/`mode_names` 가 **사라진 것**이다 — 다른 파일이 쓰면 링크 에러.

**ELF / Cortex-M 에서는?** (직접 돌리지 못했으니 기대치로 읽을 것) `arm-none-eabi-gcc -Os` 와 `arm-none-eabi-g++ -Os -fno-exceptions -fno-rtti` 로 같은 비교를 하면 `.text`/`.rodata`/`.data`/`.bss` 는 같은 크기의 Thumb-2 코드로 나올 것으로 본다 (이 코드엔 C++ 고유 기능이 없으므로). 차이가 나기 쉬운 곳은 `.ARM.exidx`/`.ARM.extab` (예외 unwind 테이블) 과 `.init_array` (전역 생성자 — CPP-B09) 이고, 링크 단계에서 libstdc++/libsupc++ 가 끌려 들어오는지(`std::terminate`, `__cxa_*` 심볼)는 `arm-none-eabi-size -A`, `arm-none-eabi-nm --size-sort` 로 확인한다. 맥 섹션 대응: `__TEXT,__text`↔`.text`, `__TEXT,__const`/`__cstring`↔`.rodata`, `__DATA,__bss`↔`.bss`, `__DATA,__const`(재배치가 필요한 포인터 테이블) ↔ PIC 가 아니면 `.rodata`.

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution   # 24/24 passed
c++ -std=c++20 -g -O1 -fsanitize=address,undefined -fno-exceptions -fno-rtti solution.cpp -o sol_asan && ./sol_asan   # 24/24, 리포트 없음
sh build.sh   # C vs C++ size / objdump / nm 비교
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 모든 `void*` 에 C 캐스트 `(T*)p` 를 뿌림 | 빌드는 되지만 C 캐스트는 const 제거·`reinterpret_cast` 까지 조용히 함 | `static_cast<T*>(p)`, 한 곳(`pool_new<T>`)으로 모으기 |
| `-Wno-reorder-init-list` 로 경고를 끔 | clang 확장일 뿐 — GCC(arm-none-eabi-g++)에선 에러 (기대치), 초기화 순서도 선언 순 | 선언 순서대로 다시 쓴다 |
| `{ .pin = pin_var }` 에서 narrowing 에러 → C 캐스트로 덮음 | 300 → 44 같은 잘림을 숨김 | 범위 검사 후 `static_cast<uint8_t>(pin_var)` |
| `static_cast<gpio_mode_t>(reg >> sh)` | 위쪽 비트가 남아 있으면 0..3 밖 → UB | `static_cast<gpio_mode_t>((reg >> sh) & 3u)` |
| `rb_create(16, const_cast<char*>("uart_rx"))` 후 이름을 수정 | 리터럴은 플래시/.rodata — 쓰기는 UB (Cortex-M 이면 쓰기 무시 또는 버스 폴트) | 필드를 `const char *` 로 |
| 헤더에 `const uint16_t kCal[] = {...};` (C 시절 `extern` 없는 정의 파일) | C++ 에선 내부 링크 → 다른 TU 의 `extern` 선언이 undefined symbol | 정의에 `extern const`, 또는 `inline constexpr` (C++17) |
| `clang++ file.c` 로 C 파일을 C++ 로 빌드 | `warning: treating 'c' input as 'c++' when in C++ mode, this behavior is deprecated` | C 파일은 C 로 두고 `extern "C"` 로 링크 (CPP-B02) 또는 `.cpp` 로 이름 변경 |
| `#define class cls` 같은 매크로로 키워드 회피 | 표준 헤더 안의 `class` 까지 바뀜 — 표준 헤더를 include 하는 TU 에서 키워드 이름을 `#define` 하는 것 자체가 금지([macro.names]) | 식별자 이름을 바꾼다 |

## 8. 면접 Q&A

<details>
<summary>Q1. C 드라이버를 C++ 컴파일러로 빌드하면 주로 어떤 에러가 나나요?</summary>

다섯 가지가 대부분이다: ① `void*` → `T*` 암시적 변환 (`malloc`/메모리 풀 결과) ② `new`/`class`/`this`/`template` 같은 C++ 키워드 식별자 ③ C99 지정 초기화 (순서 바뀜, 배열 지정자, 중첩) ④ 정수 → enum 암시적 변환과 enum 의 `++` ⑤ 문자열 리터럴 → `char*`. 그 외에 `goto` 가 초기화를 건너뛰는 코드, 같은 TU 의 tentative definition, VLA·`restrict` (C++ 에 없음, `__restrict` 확장). 꼬리질문 "에러 없이 결과가 달라지는 것은?" → `sizeof('A')`, namespace 범위 `const` 의 내부 링크, struct 태그가 바깥 이름을 가리는 것.

</details>

<details>
<summary>Q2. C 에선 malloc 결과를 캐스트하지 말라고 하는데 C++ 은 왜 캐스트를 요구하나요? 어떤 캐스트를 써야 하나요?</summary>

C 의 조언은 C89 에서 `<stdlib.h>` 누락 시 암시적 `int malloc()` 선언을 캐스트가 가려 주던 문제 때문이다. C++ 은 암시적 함수 선언이 없고, `void*` 를 아무 포인터로 바꾸는 건 타입 시스템 구멍이라 막는다. `void*` → `T*` 는 `static_cast`. `reinterpret_cast` 는 정수 주소 → 포인터(`reinterpret_cast<GPIO_TypeDef*>(0x48000000)`)처럼 관련 없는 타입 사이에서만. 꼬리질문 "펌웨어에서 풀에서 받은 메모리에 객체를 만드는 정석은?" → placement new `::new (p) T{}` (힙·예외 없음), 또는 C++23 `std::start_lifetime_as`.

</details>

<details>
<summary>Q3. C 를 C++ 로 바꾸면 코드가 커지나요? 실제로 재 봤나요?</summary>

언어 자체로는 아니다. 같은 링버퍼/GPIO 드라이버를 C 와 C++(`-O2 -fno-exceptions -fno-rtti`)로 빌드하면 `__text` 432 B, 명령어 108개가 objdump 로 1:1 동일했다. 다른 건 맹글링된 심볼 이름과 unwind 메타데이터(맥에서 C 는 `-funwind-tables` 기본, C++ -fno-exceptions 는 없음 → 오히려 256 B 작음)뿐. 코드가 커지는 건 기능을 쓸 때다: 예외(unwind 테이블 + 런타임), RTTI(typeinfo), virtual(vtable), 템플릿 인스턴스 남발, iostream, 전역 생성자(.init_array), 힙. 그래서 펌웨어 부분집합은 이것들을 끄거나 통제한다 (CPP-B09, B13, B14).

</details>

<details>
<summary>Q4. C99 지정 초기화와 C++20 지정 초기화는 뭐가 다른가요?</summary>

C++20 은 직접 멤버만, **선언 순서대로**, 지정/비지정 섞기 금지, 배열 지정자·중첩 지정자 없음. 이유는 C++ 의 멤버 초기화가 선언 순서로 일어나고(부수 효과가 있는 초기화식의 평가 순서를 소스 순서와 일치시키려고) 생성자·소멸자와 맞물리기 때문이다. 대신 중괄호 초기화라 narrowing 이 에러가 된다 — C 에서 조용히 잘리던 값을 잡는다. clang 은 순서 위반을 `-Wreorder-init-list` 경고로 봐주지만 GCC 는 에러이므로 이식성 있게 쓰려면 순서를 지킨다.

</details>

<details>
<summary>Q5. 헤더에 `const int kTimeoutMs = 100;` 을 두면 C 와 C++ 에서 각각 어떻게 되나요?</summary>

C: 외부 링크 + 초기화식이 있는 **정의**라 여러 .c 가 include 하면 multiple definition 링크 에러 (`-fcommon` 도 초기화식 없는 tentative definition 에만 해당). C++: namespace 범위 `const` (volatile 아님, extern 아님)는 내부 링크라 TU 마다 사본이 생기지만 에러는 없다 — 그래서 C++ 에선 헤더 상수가 관용구다. 반대 함정: C 에서 `.c` 에 정의하고 다른 파일이 `extern const` 로 쓰던 테이블을 C++ 로 옮기면 정의가 내부 링크가 되어 **undefined symbol**. 실측: extern 을 빼자 `led_cfg`/`mode_names` 심볼이 nm 에서 사라졌다. C++17 이후 권장은 `inline constexpr` (모든 TU 가 같은 하나의 객체).

</details>

<details>
<summary>Q6. 레지스터에서 읽은 값을 enum 으로 바꿀 때 주의할 점은?</summary>

C++ 에서 기저 타입을 고정하지 않은 enum 은 값 범위가 "enumerator 를 담는 최소 비트 폭" 이라, 그 밖의 값을 `static_cast` 하면 UB 다 ([expr.static.cast]/10). 하드웨어는 예약 값·쓰레기 비트를 줄 수 있으니 ① 마스크로 범위를 맞추고 캐스트 ② `enum X : uint32_t` 로 기저 타입을 고정 (모든 uint32_t 값이 유효, 이름 없는 값은 `is_valid()` 로 검사) ③ `enum class` 로 섞어 쓰기까지 막기 (CPP-B03). constexpr 함수로 만들면 상수식에서 UB 가 컴파일 에러로 잡힌다.

</details>

<details>
<summary>Q7. "펌웨어용 C++ 부분집합" 이라면 무엇을 쓰고 무엇을 빼나요?</summary>

빼는 것(또는 강하게 통제): 예외(`-fno-exceptions`), RTTI(`-fno-rtti`, `dynamic_cast`/`typeid`), 초기화 이후 힙(`new`, `std::vector`, `std::string`, `std::function`), iostream, 초기화 순서가 불명확한 전역 생성자. 쓰는 것: 클래스·네임스페이스, RAII (CriticalSection, ChipSelect — CPP-B06/B07), `constexpr`/`static_assert`, `enum class`, 참조, 템플릿(크기 측정하며), `std::array`/`std::span`, placement new. 근거 규칙집: MISRA C++:2023, AUTOSAR C++14, JSF AV C++. 핵심 원칙은 "C 에 타입 검사를 더하되 **숨은 런타임 비용이 없는 기능만**" — 그리고 그걸 `size`/`objdump` 로 증명한다.

</details>

## 9. 추가 연습

### 연습 1 — struct 태그가 전역 배열을 가린다

전역 `char frame[64];` 와 함수 안의 `struct frame { char hdr, len; };` 이 있을 때, 함수 안에서 `sizeof(frame)` 은 C 와 C++ 에서 각각 얼마인가? C++ 에서 전역 배열의 크기를 얻으려면?

<details>
<summary>답 보기</summary>

C: 64 (struct 태그는 별도 이름공간), C++: 2 (클래스 이름이 바깥 변수를 가림, [diff.class]) — `demo_silent.c` 실측. 전역을 원하면 `sizeof(::frame)`, 더 좋게는 이름을 다르게 짓는다. 이런 "조용한" 차이 때문에 C 파일을 C++ 로 옮길 때는 단위 테스트를 **양쪽 빌드에서** 돌려 보는 게 안전하다.

</details>

### 연습 2 — goto 에러 처리와 초기화

C 의 `if (n <= 0) goto fail; uint8_t crc = 0; ... fail: return -1;` 패턴은 C++ 에서 어떤 에러가 나고 어떻게 고치나?

<details>
<summary>답 보기</summary>

실제 출력 (`demo_show_error.cpp -DSHOW_ERROR`):

```text
demo_show_error.cpp:23:17: error: cannot jump from this goto statement to its label
demo_show_error.cpp:25:18: note: jump bypasses variable initialization
```

C++ 은 초기화가 있는 변수의 선언을 건너뛰어 그 변수의 스코프 안으로 점프하는 것을 금지한다 ([stmt.dcl] — 초기화식 없는 스칼라 등은 예외). 고치기: 초기화를 블록 `{ }` 으로 감싸 label 이 그 스코프 밖에 있게 하거나, early return 을 쓴다. 정리할 자원이 여러 개라면 C++ 다운 답은 RAII (CPP-B06/B07). 같은 파일에서 `uint32_t tick_count;` 를 두 번 쓰면 `error: redefinition of 'tick_count'` — C 의 tentative definition 은 C++ 에 없다.

</details>

### 연습 3 — 기저 타입을 고정한 enum

`enum gpio_mode8_t : uint8_t { ... 4개 ... };` 에 `static_cast<gpio_mode8_t>(0xFF)` 는 UB 인가? 상수식에서 확인하라.

<details>
<summary>답 보기</summary>

UB 아님. 기저 타입이 고정되면 값 범위 = 기저 타입 전체 (0..255). `static_assert(raw_mode(0xFF) == 0xFF && !is_valid(raw_mode(0xFF)));` 가 컴파일된다 = 상수식에서 UB 가 없다는 증거. 이름 없는 값이 들어올 수 있으니 `is_valid()` 로 검사한다. 섞어 쓰기까지 막으려면 `enum class` (CPP-B03).

</details>

### 연습 4 — 배열 지정자 대신 switch + 길이 sentinel

C 의 `[PAR_EVEN] = "E"` 테이블을, enumerator 를 빠뜨리면 빌드가 깨지는 C++ 코드로 바꿔라.

<details>
<summary>답 보기</summary>

① `constexpr` 함수 안의 `switch` — `default:` 없이 모든 enumerator 를 쓰면 하나 빠뜨렸을 때 `-Wswitch` (-Wall 포함) 가 잡는다. 실제 출력: `error: enumeration value 'MODE_ANALOG' not handled in switch [-Werror,-Wswitch]`. ② 레지스터 비트 테이블처럼 배열이 필요하면 마지막에 `PAR_COUNT` sentinel 을 두고 `static_assert(sizeof kParityBits == PAR_COUNT)`. 둘 다 런타임 비용 0.

</details>

### 연습 5 — const 를 안 지키는 벤더 HAL 에 리터럴 넘기기

바꿀 수 없는 C HAL 이 `void legacy_uart_puts(char *s)` (실제로는 읽기만 함) 이다. C++ 쪽 `uart_puts(const char*)` 에서 리터럴을 넘기려면?

<details>
<summary>답 보기</summary>

`legacy_uart_puts(const_cast<char*>(s))` — **호출 대상이 쓰지 않는다는 것을 확인했을 때만** 합법이다 (const 객체에 실제로 쓰는 순간 UB). 우리 쪽 API 는 `const char*` 로 두어 const 를 지키고, 위험한 캐스트는 HAL 경계 한 곳에 가둔다. 더 좋은 답: 벤더 헤더를 고칠 수 있으면 `const char *` 로 고친다. HAL 이 진짜로 버퍼를 쓴다면 RAM 버퍼에 복사해서 넘긴다.

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice   # 10/10 passed
```

## 10. 핵심 정리

- C → C++ 에러 5종: `void*` 암시 변환 / 키워드 식별자(`new`, `class`) / 지정 초기화 제약 / 정수 → enum / 리터럴 → `char*`.
- 처방은 전부 컴파일 타임: `static_cast`, 이름 변경, 선언 순 초기화 + `static_assert`, `const char*` — **기계어 동일** (objdump 108/108 명령어 일치).
- 지정 초기화: C++20 은 선언 순서·직접 멤버만, 배열 지정자 없음. 대신 narrowing 을 잡는다. clang 의 경고 허용은 GCC 에선 에러.
- 고정 기저 타입 없는 enum 에 범위 밖 정수 `static_cast` = UB → 먼저 마스크. constexpr 테스트로 컴파일 타임에 잡는다.
- 조용한 차이: `sizeof('A')` 1 vs 4, namespace 범위 `const` 는 **내부 링크** (extern 없으면 심볼 사라짐), struct 태그가 바깥 이름을 가림.
- `void*` 메모리에 객체 만들기의 정석 = placement new (힙·예외 없음).
- 코드 크기는 언어가 아니라 **기능**이 늘린다: 예외·RTTI·vtable·힙·전역 생성자 → `-fno-exceptions -fno-rtti` 와 `size`/`objdump` 로 증명.

## 참고자료

- C++ 표준 초안 Annex C "Compatibility with ISO C" — <https://eel.is/c++draft/diff.iso> ([diff.lex], [diff.basic], [diff.conv], [diff.stat], [diff.dcl], [diff.class])
- [dcl.init.aggr] 지정 초기화 — <https://eel.is/c++draft/dcl.init.aggr> · cppreference: <https://en.cppreference.com/w/cpp/language/aggregate_initialization>
- [dcl.enum] 값 범위, [expr.static.cast] — <https://eel.is/c++draft/dcl.enum>, <https://eel.is/c++draft/expr.static.cast>
- [basic.link] const 의 내부 링크 — <https://eel.is/c++draft/basic.link>
- [intro.object] 암시적 객체 생성, placement new — <https://eel.is/c++draft/intro.object>, <https://en.cppreference.com/w/cpp/language/new>
- 문자열 리터럴 타입 — <https://en.cppreference.com/w/cpp/language/string_literal>
- Clang 진단 레퍼런스 (`-Wreorder-init-list`, `-Wc99-designator`, `-Wwritable-strings`) — <https://clang.llvm.org/docs/DiagnosticsReference.html>
- Bjarne Stroustrup, "C and C++: Siblings" / "C and C++: A Case for Compatibility" — <https://www.stroustrup.com/compat_short.pdf>
- MISRA C++:2023, AUTOSAR C++14 Guidelines — 펌웨어 C++ 부분집합 규칙집
