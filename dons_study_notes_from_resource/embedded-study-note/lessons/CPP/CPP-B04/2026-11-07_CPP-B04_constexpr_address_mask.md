---
id: CPP-B04
time: 50분
updated: 2026-11-07
---
# CPP-B04 constexpr 기초: 컴파일타임 주소·마스크 계산

## 학습 목표

- `constexpr` 함수로 `mask(offset, width)`·`bit(n)`·`field_prep()` 을 만들고, **width == 32 도 UB 없이** 처리한다.
- 주변장치 주소를 `base + offset` 의 `constexpr uintptr_t` 상수로 쌓고 `static_assert` 로 데이터시트 값에 못 박는다.
- "상수 평가(constant evaluation) 중의 UB 는 **컴파일 에러**가 된다" 는 성질로 잘못된 필드·주소를 빌드 단계에서 잡는다.
- `reinterpret_cast` 가 constexpr 에 못 들어가는 이유를 알고, **주소는 정수로 들고 다니다가 쓰는 순간에 캐스트**하는 패턴을 쓴다.
- `constexpr` / `consteval` / `#define` 의 차이를 설명하고, objdump 로 런타임 계산이 사라졌음을 확인한다.

## 1. 문제

레지스터 주소와 비트마스크는 C 에서 전부 `#define` 이었다. C++ 의 `constexpr` 는 같은 "0 비용 상수" 를 **타입·스코프·컴파일러 검사**와 함께 준다 — 면접에서 "C++ 를 펌웨어에 쓰면 뭐가 좋냐" 의 첫 번째 답이 이것이다.

STM32F4 의 GPIOA BSRR 주소는 이렇게 쌓인다: `PERIPH_BASE(0x4000'0000) + AHB1 오프셋(0x2'0000) + 포트 × 0x400 + 레지스터 오프셋(0x18)`. 이 덧셈을 C 는 전처리기 매크로로, C++ 는 `constexpr` 함수·변수로 한다. 어느 쪽이든 기계어에는 `0x4002'0018` 이라는 숫자 하나만 남아야 한다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">주소는 constexpr 덧셈으로 쌓고, 기계어엔 즉시값 하나만 남긴다</text>
  <g stroke="currentColor">
    <rect x="10" y="40" width="150" height="44" rx="6" class="f-mut"/>
    <rect x="185" y="40" width="150" height="44" rx="6" class="f-mut"/>
    <rect x="360" y="40" width="160" height="44" rx="6" class="f-mut"/>
    <rect x="545" y="40" width="185" height="44" rx="6" class="f-acc"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="58">PERIPH_BASE</text><text x="20" y="76">0x4000'0000</text>
    <text x="195" y="58">+ AHB1 0x2'0000</text><text x="195" y="76">= 0x4002'0000</text>
    <text x="370" y="58">+ port A × 0x400</text><text x="370" y="76">= 0x4002'0000</text>
    <text x="555" y="58">+ BSRR 0x18</text><text x="555" y="76">= 0x4002'0018</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M160 62 L182 62"/><path d="M335 62 L357 62"/><path d="M520 62 L542 62"/>
    <path d="M637 84 L637 120"/>
  </g>
  <rect x="380" y="120" width="350" height="80" rx="6" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="392" y="140">led_on():  (arm64, -O2, 실제 출력)</text>
    <text x="392" y="158">mov  w8, #0x18</text>
    <text x="392" y="174">movk w8, #0x4002, lsl #16</text>
    <text x="392" y="190">mov w9, #0x20 ; str w9, [x8]</text>
  </g>
  <rect x="10" y="120" width="340" height="80" rx="6" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="22" y="142">mask(30, 4)  → 30+4 &gt; 32</text>
    <text x="22" y="162">constexpr 변수 / static_assert 에서 쓰면</text>
    <text x="22" y="182">→ 컴파일 에러 (런타임 버그가 아니라)</text>
  </g>
  <text x="10" y="232" fill="currentColor">컴파일러 안의 "상수 평가기" 가 계산을 끝내거나, 계산 불가(UB·범위 위반)를 에러로 보고한다.</text>
</svg>
```

### Part A — constexpr 비트·마스크 함수

- `constexpr uint32_t bit(unsigned n)` — n 번 비트만 1. `n >= 32` 면 거부.
- `constexpr uint32_t mask(unsigned offset, unsigned width)` — `mask(4,4) == 0xF0`. **`mask(0, 32) == 0xFFFF'FFFF` 가 UB 없이** 나와야 한다 (`1u << 32` 금지).
- `constexpr uint32_t field_prep(uint32_t val, unsigned offset, unsigned width)` — `val` 을 필드 자리로 올린다. C-B01 의 `field_set` 은 넘치는 값을 **잘라냈지만**, 여기서는 상수라면 오타일 가능성이 크므로 **거부**한다.

테스트는 `CT<mask(4,4)>` 처럼 결과를 **템플릿 인자 자리에** 넣어서, 계산이 진짜 컴파일타임에 끝나는지까지 확인한다.

### Part B — 잘못된 필드는 컴파일 에러로

- `constexpr bool field_ok(offset, width)` — `1 ≤ width ≤ 32` 이고 `offset + width ≤ 32`. 단 `offset + width` 는 unsigned 덧셈이라 **랩어라운드(wrap-around)** 될 수 있다 (`0xFFFF'FFFF + 2 == 1`).
- Part A 함수들이 범위 밖 인자를 받으면 constexpr 가 **아닌** 함수 `invalid_field_spec()` 을 호출하게 한다. 그러면 상수 평가 중에는 컴파일 에러, 런타임에는 트랩이 된다.
- 하네스의 `MaskIsConstant<O, W>` concept 이 "이 식이 상수 평가 가능한가" 를 검사한다: `MaskIsConstant<29, 4>` 는 false 여야 한다.

### Part C — STM32F4 메모리 맵 + mock GPIO 드라이버

- `gpio_base(port)` (0=A … 10=K, 범위 밖 거부), `reg_addr(base, off)` (off 가 4의 배수가 아니면 거부).
- `LED_BSRR`, `AHB1ENR` 같은 `inline constexpr uintptr_t` 상수를 만들고 `static_assert` 로 RM0090 값을 못 박는다.
- mock 메모리 위에서 `gpio_output_init(port, pin)` (RCC 클럭 enable + MODER 필드 = 01), `led_on()/led_off()` (BSRR) 를 구현한다. 실제 칩이었다면 `mmio32()` 안의 한 줄만 `reinterpret_cast` 로 바뀐다.

## 2. Problem (English)

> **Compile-time register addresses and bit masks.**
>
> Using C++20 with `-fno-exceptions -fno-rtti`:
>
> 1. Write `constexpr uint32_t mask(unsigned offset, unsigned width)` returning `width` ones shifted left by `offset`, valid for `1 <= width <= 32` and `offset + width <= 32` (beware: `1u << 32` is undefined behavior). Also `bit(n)` and `field_prep(val, offset, width)`, which must *reject* (not truncate) a `val` wider than the field.
> 2. Invalid arguments must make any constant evaluation ill-formed (i.e. a compile error when used in `static_assert`, a `constexpr` variable, or a template argument), while remaining callable at run time with valid arguments. You cannot use `throw`.
> 3. Model the STM32F4 memory map (`PERIPH_BASE = 0x40000000`, AHB1 at `+0x20000`, GPIO ports every `0x400`, RCC at AHB1 `+0x3800`, `RCC_AHB1ENR` at `+0x30`, GPIO `MODER +0x00`, `BSRR +0x18`) as `constexpr` integers, pin the values with `static_assert`, and write `gpio_output_init(port, pin)`, `led_on()`, `led_off()` for an LED on PA5.
> 4. Explain why `constexpr GPIO_TypeDef* GPIOA = reinterpret_cast<GPIO_TypeDef*>(0x40020000);` does not compile, and show with a disassembler that `led_on()` contains no address arithmetic.
>
> Example: `mask(4, 4) == 0xF0`, `mask(0, 32) == 0xFFFFFFFF`, `field_prep(0xA, 4, 4) == 0xA0`, `static_assert(mask(30, 4))` → compile error.

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 깨짐 대비
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
# 처음엔 FAIL 이 쭉 나오고 "2/31 passed" — TODO 를 채워 31/31 을 만든다
```

## 4. 힌트

<details>
<summary>힌트 1 — constexpr 함수는 그냥 평범한 C 함수처럼 쓰면 된다</summary>

C++14 부터 constexpr 함수 안에 `if`, 지역 변수, 루프가 다 된다. `bit(n)` 은 `if (n >= 32) invalid_field_spec(); return 1u << n;` 두 줄이다.

</details>

<details>
<summary>힌트 2 — width == 32 는 따로</summary>

`(1u << width) - 1u` 는 width=32 에서 시프트 양 ≥ 타입 폭 → UB. `width == 32 ? 0xFFFF'FFFFu : (1u << width) - 1u` 로 분기. 분기 없이 "고쳐 보겠다" 고 starter 에 그냥 `(1u << width) - 1u` 를 쓰면… 하네스가 **컴파일조차 안 된다**. 왜 그런지가 이 레슨의 핵심이다 (5. 풀이 Part B).

</details>

<details>
<summary>힌트 3 — 랩어라운드 없는 범위 검사</summary>

`offset + width <= 32` 대신 `width <= 32 && offset <= 32 - width`. 앞에서 `width <= 32` 를 확인했으니 `32 - width` 는 음수(=랩)가 될 수 없다.

</details>

<details>
<summary>힌트 4 — throw 없이 "constexpr 실패" 만들기</summary>

constexpr 가 아닌 함수를 호출하는 경로를 만든다. 상수 평가가 그 경로에 **도달할 때만** 에러다 — 정상 인자로는 그 줄을 지나가지 않으므로 문제없다. 하네스 맨 위의 `invalid_field_spec()` 이 그 용도다.

</details>

<details>
<summary>힌트 5 — static_assert 는 namespace 안 아무 데나</summary>

`static_assert(gpio_base(0) == 0x4002'0000u, "GPIOA base");` — 함수 밖에 써도 된다. 실패하면 clang 이 양변의 실제 값까지 보여준다.

</details>

## 5. 풀이

### Part A — constexpr 함수 = "컴파일러가 실행해 볼 수 있는" 함수

| 키워드 | 붙는 곳 | 의미 |
|---|---|---|
| `constexpr` 변수 | `constexpr uint32_t M = mask(4,4);` | 초기값이 **반드시** constant expression. 아니면 에러. 암묵적 `const`. |
| `constexpr` 함수 | `constexpr uint32_t mask(...)` | 상수 평가 **가능**. 상수 문맥에서 부르면 컴파일타임, 아니면 그냥 런타임 함수. |
| `consteval` 함수 (C++20) | `consteval uint32_t ct_mask(...)` | **모든** 호출이 컴파일타임이어야 함 (immediate function). |
| `static_assert(식, "msg")` | 어디든 | 식은 상수여야 하고, false 면 컴파일 에러. |

"상수 문맥(manifestly constant-evaluated)" 은 정해져 있다: constexpr 변수 초기값, `static_assert`, 템플릿 인자, 배열 크기, `case` 라벨, `if constexpr` 조건, `constinit`, consteval 호출. 그 밖에서 constexpr 함수를 부르면 **표준은 컴파일타임 계산을 보장하지 않는다** ([dcl.constexpr], [expr.const]). 그래서 하네스는 `CT<mask(4,4)>` 로 결과를 템플릿 인자 자리에 넣는다.

```cpp
constexpr uint32_t mask(unsigned offset, unsigned width) {
    if (!field_ok(offset, width)) invalid_field_spec();
    const uint32_t ones = (width == 32) ? 0xFFFF'FFFFu : (1u << width) - 1u;   // 1u << 32 는 UB
    return ones << offset;
}
```

`field_prep` 은 `val >> width != 0` 이면 거부한다 (width=32 면 검사 생략 — 여기서도 `val >> 32` 는 UB). C 의 `field_set` 이 `& mask` 로 **조용히 잘라냈던** 것과 다르다: `field_prep(0x1F, 4, 4)` 가 상수라면 그건 거의 확실히 오타이므로 빌드를 멈추는 편이 낫다.

### Part B — 상수 평가 중의 UB 는 컴파일 에러다

C++ 표준 [expr.const]/5: 식을 평가하다가 **코어 언어의 정의되지 않은 동작(UB)** 이나 constexpr 가 아닌 함수 호출 등에 도달하면 그 식은 core constant expression 이 **아니다**. 그런데 상수 문맥은 constant expression 을 요구하므로 → 진단(에러)이 의무다. 런타임에선 "아무 일이나 일어날 수 있는" UB 가, 컴파일타임엔 **컴파일러가 반드시 잡아야 하는** 에러로 바뀐다.

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">같은 mask(0, 32) — 어디서 평가되느냐에 따라 결과가 다르다</text>
  <rect x="10" y="40" width="200" height="50" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="62" fill="currentColor" font-family="monospace" font-size="12">(1u &lt;&lt; width) - 1u</text>
  <text x="22" y="80" fill="currentColor" font-size="12">분기 없는 naive 구현</text>
  <g stroke="currentColor" fill="none"><path d="M210 65 L260 65 L260 120"/><path d="M260 65 L460 65 L460 120"/></g>
  <rect x="140" y="120" width="250" height="110" rx="6" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="152" y="142" font-weight="bold">런타임 (함수 인자 w = 32)</text>
    <text x="152" y="162">UB. arm64 LSL 은 시프트 양 mod 32</text>
    <text x="152" y="180">→ 1u&lt;&lt;32 == 1 → 마스크 0 (조용한 버그)</text>
    <text x="152" y="198">Cortex-M LSL(레지스터) 은 하위 8비트</text>
    <text x="152" y="216">→ 0 → 마스크 0xFFFFFFFF (우연히 맞음)</text>
  </g>
  <rect x="410" y="120" width="320" height="110" rx="6" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="422" y="142" font-weight="bold">상수 문맥 (CT&lt;mask(0,32)&gt;, static_assert)</text>
    <text x="422" y="162">컴파일러의 상수 평가기가 UB 를 감지</text>
    <text x="422" y="180" font-family="monospace">error: ... not a constant expression</text>
    <text x="422" y="198" font-family="monospace">note: shift count 32 &gt;= width of</text>
    <text x="422" y="216" font-family="monospace">      type 'unsigned int' (32 bits)</text>
  </g>
  <text x="10" y="258" fill="currentColor">→ constexpr 상수로 만들면 "플랫폼마다 다른 UB" 대신 "어디서나 같은 컴파일 에러".</text>
</svg>
```

(위 그림의 런타임 동작은 ISA 설명일 뿐 — UB 이므로 컴파일러가 그 명령을 그대로 쓴다는 보장도 없다.)

실제로 해 봤다. solution 의 `mask` 에서 width==32 분기를 지우고 하네스를 빌드하면 **실행 전에** 이렇게 멈춘다:

```text
naive.cpp:132:14: error: non-type template argument is not a constant expression
  132 |     CHECK(CT<mask(0, 32)> == 0xFFFF'FFFFu);          // width == 32
      |              ^~~~~~~~~~~
naive.cpp:32:31: note: shift count 32 >= width of type 'unsigned int' (32 bits)
   32 |     const uint32_t ones = (1u << width) - 1u;   // 1u << 32 는 UB
naive.cpp:132:14: note: in call to 'mask(0, 32)'
```

`demo_errors.cpp` 에 대표적인 경우를 모았다 (`-DSHOW_ERROR=N` 으로 하나씩):

```bash
c++ -std=c++20 -Wall -Wextra -O2 -fno-exceptions -fno-rtti demo_errors.cpp -o demo_errors && ./demo_errors
# mask(4,4)=0x000000F0  ct_mask(0,32)=0xFFFFFFFF  1<<31=-2147483648  mask(0,w)=0xFFFFFFFF
c++ -std=c++20 -fno-exceptions -fno-rtti -fsyntax-only -DSHOW_ERROR=2 demo_errors.cpp
```

| N | 코드 | clang 21 의 실제 메시지 (요약) |
|---|---|---|
| 1 | `constexpr uint32_t m1 = mask_naive(0, 32);` | `note: shift count 32 >= width of type 'unsigned int' (32 bits)` |
| 2 | `constexpr uint32_t m2 = mask(30, 4);` | `note: non-constexpr function 'invalid_field_spec' cannot be used in a constant expression` |
| 3 | `constexpr GpioRegs* GPIOA = reinterpret_cast<GpioRegs*>(GPIOA_BASE);` | `note: reinterpret_cast is not allowed in a constant expression` |
| 4 | `uint32_t f(unsigned w) { return ct_mask(0, w); }` (consteval) | `error: call to consteval function 'ct_mask' is not a constant expression` / `function parameter 'w' with unknown value` |
| 5 | `static_assert(GPIOB_BASE == GPIOA_BASE + 0x400, "...")` (GPIOB 오타) | `static assertion failed ...` + `note: expression evaluates to '1073874944 == 1073873920'` |
| 6 | `constexpr int neg = -1 << 1;` 를 **-std=c++17** 로 | `note: left shift of negative value -1` (C++20 에서는 통과: −2) |

**왜 throw 대신 constexpr 아닌 함수인가?** C++11/14 시절 관용구 `return w <= 32 ? x : throw "bad";` 는 `-fno-exceptions` 에서 `error: cannot use 'throw' with exceptions disabled` (직접 확인) 로 아예 안 된다. constexpr 아닌 함수 호출은 예외 없이도 같은 효과를 내고, 함수 이름이 에러 메시지에 그대로 찍히므로 이름을 설명적으로 짓는다.

**주의할 경계 3가지**

- 잡히는 건 **UB 뿐**이다. unsigned 랩어라운드는 정의된 동작이라 안 잡힌다 → `offset + width <= 32` 는 `offset = 0xFFFF'FFFF, width = 2` 에서 `1 <= 32` 로 **통과**해 버린다. 그래서 `offset <= 32 - width` 로 쓴다 (하네스 `!field_ok(0xFFFF'FFFFu, 2)`).
- 표준 라이브러리 안의 UB 는 진단이 **보장되지 않는다** ([expr.const]/5 마지막 문장: unspecified). 실무에선 대부분 잡히지만 믿고 설계하진 말자.
- C++20 은 signed 왼쪽 시프트를 모듈러로 정의했다 (P1236R1, 2의 보수 확정 P0907). `1 << 31` 은 C++17 에서도 CWG1457 로 이미 OK, `-1 << 1` 은 C++17 에선 UB (위 표 6번). 하지만 **시프트 양 ≥ 폭은 C++20 에서도 UB** ([expr.shift]/1) — 그래서 width==32 분기는 여전히 필요하다.

하네스의 concept 트릭은 "상수 평가 실패" 를 컴파일 에러 대신 **bool** 로 바꾼다:

```cpp
template <unsigned O, unsigned W>
concept MaskIsConstant = requires { typename std::integral_constant<uint32_t, mask(O, W)>; };
```

템플릿 인자가 constant expression 이 아니면 **치환 실패(substitution failure)** 로 처리되어 `false` 가 된다 (clang/gcc 공통 동작). 그래서 starter 에서 "잘못된 인자는 거부돼야 한다" 를 FAIL 줄로 보여줄 수 있었다.

### Part C — 주소는 정수로, 캐스트는 쓰는 순간에

```svg
<svg viewBox="0 0 740 220" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">constexpr 세계(정수) ↔ 경계(캐스트 한 줄) ↔ 런타임 세계(포인터)</text>
  <rect x="10" y="40" width="260" height="130" rx="8" class="f-ok" stroke="currentColor"/>
  <rect x="300" y="40" width="160" height="130" rx="8" class="f-acc" stroke="currentColor"/>
  <rect x="490" y="40" width="240" height="130" rx="8" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="22" y="62" font-weight="bold">constexpr uintptr_t</text>
    <text x="22" y="84" font-family="monospace">gpio_base(0) + 0x18</text>
    <text x="22" y="104">덧셈·검사·static_assert 자유</text>
    <text x="22" y="124">상수 평가기가 다 계산</text>
    <text x="22" y="144">타입 = 그냥 숫자</text>
    <text x="312" y="62" font-weight="bold">mmio32(addr)</text>
    <text x="312" y="84" font-family="monospace">reinterpret_cast</text>
    <text x="312" y="104">여기 한 곳에서만</text>
    <text x="312" y="124">상수 문맥 밖</text>
    <text x="312" y="144">(mock: 배열 인덱스)</text>
    <text x="502" y="62" font-weight="bold">volatile uint32_t&amp;</text>
    <text x="502" y="84">실제 읽기/쓰기 (str/ldr)</text>
    <text x="502" y="104">volatile 규칙은 CPP-B05</text>
    <text x="502" y="124">폭별 접근 템플릿은 CPP-B12</text>
  </g>
  <g stroke="currentColor" fill="none"><path d="M270 105 L297 105"/><path d="M460 105 L487 105"/></g>
  <text x="10" y="198" fill="currentColor" font-size="12">constexpr Regs* p = reinterpret_cast&lt;Regs*&gt;(addr); ← 경계를 상수 세계로 끌어오기 = 컴파일 에러</text>
</svg>
```

**왜 `reinterpret_cast` 는 constexpr 에 못 들어가나?** [expr.const]/5 가 `reinterpret_cast` 를 명시적으로 금지한다. 이유는 상수 평가기가 포인터를 "어떤 **객체**를 가리키는가" 로 추적하기 때문이다. `0x4002'0018` 이라는 정수에서 만든 포인터 뒤에는 추상 기계(abstract machine)가 아는 객체가 없다 — 컴파일러는 그 주소의 내용을 알 수 없고, 역참조가 UB 인지 판정할 수도 없다. 상수 평가는 "결정적이고, UB 를 반드시 잡는다" 가 약속이라 이런 캐스트를 받아들일 수 없다. C 스타일 캐스트 `(GpioRegs*)0x40020000` 도 내부적으로 `reinterpret_cast` 라 똑같다. (`std::bit_cast` 도 포인터 타입이면 constexpr 가 아니다. C++26 의 P2738 은 `void*` → 원래 타입 캐스트만 허용할 뿐 정수→포인터는 여전히 금지.)

그래서 실무 패턴은 3가지다:

| 방법 | 예 | 상수 평가 가능? | 비고 |
|---|---|---|---|
| ① 정수 constexpr + 사용 시 캐스트 | `constexpr uintptr_t LED_BSRR = …;` → `*reinterpret_cast<volatile uint32_t*>(LED_BSRR)` | 주소 계산까지 O | 이 레슨의 방식. static_assert 가능 |
| ② CMSIS 매크로 | `#define GPIOA ((GPIO_TypeDef*)GPIOA_BASE)` | — (텍스트 치환) | 벤더 헤더 표준. 타입·스코프 없음 |
| ③ `const` 포인터 전역 | `GpioRegs* const GPIOA = reinterpret_cast<…>(…);` | X (`constinit` 붙이면 에러) | clang 은 -O0 에서도 `__TEXT,__const` 에 정적 초기화함(확인). 단 표준상 보장 아님 → CPP-B09 |

`solution.cpp` 의 static_assert 들은 "데이터시트와 코드가 일치한다" 는 **실행 비용 0의 단위 테스트**다. 메모리 맵 오타(표 5번)는 런타임이면 엉뚱한 레지스터를 쓰는 디버깅 지옥이지만, 여기선 빌드가 실패하고 양변 값까지 보여준다.

**objdump 로 확인 — 런타임 계산이 남았나?** `demo_codegen.cpp` 를 실행하지 않고 오브젝트만 만들어 본다 (실제 MMIO 주소에 쓰므로 실행 금지).

```bash
c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -c demo_codegen.cpp -o /tmp/dc_O2.o && objdump -d -r --no-show-raw-insn /tmp/dc_O2.o
```

```text
0000000000000000 <ltmp0>:                    ; = led_on_constexpr() (nm 으로 확인: 0x0)
       0:  mov  w8, #0x18
       4:  movk w8, #0x4002, lsl #16          ; w8 = 0x4002'0018  ← 주소 계산 흔적 없음
       8:  mov  w9, #0x20                     ; bit(5)
       c:  str  w9, [x8]
      10:  ret
0000000000000014 <__Z12led_on_macrov>:       ; C 매크로 버전 — 완전히 같은 4개 명령
      14:  mov  w8, #0x18
      18:  movk w8, #0x4002, lsl #16
      1c:  mov  w9, #0x20
      20:  str  w9, [x8]
0000000000000028 <__Z11gpio_set_rtjj>:       ; 인자가 런타임 값이면?
      30:  cmp  w1, #0x20 ; b.hs …            ; bit() 범위 검사가 남는다
      38:  cmp  w0, #0xb  ; b.hs …            ; gpio_base() 범위 검사
      40:  mov  w8, #0x1 ; lsl w8, w8, w1     ; 1u << pin
      48:  lsl  w9, w0, #10                   ; port * 0x400
      54:  orr  x9, x9, x10 ; str w8, [x9]
      64:  bl   …   ARM64_RELOC_BRANCH26 __Z18invalid_field_specv
```

(Mach-O 는 섹션 첫 함수에 `ltmp0` 라는 로컬 라벨을 붙인다. 지면상 일부 줄은 생략하거나 `;` 로 한 줄에 합쳤다 — 명령·주소는 실제 출력 그대로.) 인자가 상수면 범위 검사 코드조차 사라지고, 런타임 인자면 검사가 **남는다** — constexpr 함수는 "공짜 검사" 가 아니라 "상수일 때만 공짜" 다.

**-O0 에서의 함정**: 같은 파일을 `-O0` 으로 빌드하면 —

```text
0000000000000000 <ltmp0>:                    ; led_on_constexpr() at -O0
       8:  mov  w0, #0x5
       c:  bl   …   ARM64_RELOC_BRANCH26 __Z3bitj     ; bit(LED_PIN) 를 런타임에 호출!
      10:  mov  x8, #0x18
      14:  movk x8, #0x4002, lsl #16                  ; LED_BSRR 는 constexpr 변수라 즉시값
00000000000001cc <__Z15moder_constexprv>:            ; constexpr uint32_t m = mask(10, 2);
     1d0:  mov  w0, #0xc00                            ; 즉시값
0000000000000100 <__Z11moder_plainv>:                ; uint32_t m = mask(10, 2);
     114:  bl   …  (mask)                             ; 런타임 호출
```

→ **constexpr 변수** 에 담은 값은 최적화 레벨과 무관하게 컴파일타임 확정, 그냥 함수 호출은 옵티마이저 재량이다. 디버그 빌드(-O0/-Og)로 ISR 타이밍을 재는 팀이라면 이 차이가 실제로 보인다. 확실히 하려면 `constexpr` 변수에 담거나 `consteval` 로 만든다.

**Cortex-M 은?** arm-none-eabi-g++ 는 이 Mac 에 없어서, 헤더 없이 바꾼 사본을 `clang++ --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -S` 로 뽑아 봤다 (clang 출력 — 실제 실행):

```text
_Z16led_on_constexprv:
	movs	r0, #24
	movt	r0, #16386        @ 0x4002 → r0 = 0x4002'0018
	movs	r1, #32
	str	r1, [r0]
	bx	lr
```

arm-none-eabi-g++ `-O2` 는 보통 같은 상수를 **리터럴 풀** (`ldr r3, .L3` + `.word 0x40020018`) 로 읽는다 (예상 — 직접 실행 못 함). 어느 쪽이든 덧셈·시프트 명령은 없다.

**constexpr vs consteval vs #define**

| | `#define` | `constexpr` | `consteval` (C++20) |
|---|---|---|---|
| 타입 | 없음 (텍스트) | 있음 | 있음 |
| 스코프 / namespace | 없음 (전역 오염) | 있음 | 있음 |
| 인자 다중 평가 | 있음 (C-B15) | 없음 | 없음 |
| 컴파일타임 보장 | 리터럴 식이면 사실상 | 상수 문맥에서만 | **항상** (아니면 에러) |
| 런타임 호출 | — | 가능 | 불가 |
| 범위 검사 → 컴파일 에러 | 불가 (`_Static_assert` 별도) | 상수 문맥에서 O | O |
| `#if` 에서 사용 | **가능** | 불가 (`-Wundef`: "'A' is not defined, evaluates to 0") | 불가 |
| 어셈블리(.S)·링커 스크립트 공유 | 가능 | 불가 | 불가 |

`#if` 와 startup.S 때문에 벤더 헤더는 여전히 매크로다. 앱·드라이버 계층에선 constexpr, "절대 런타임에 돌면 안 되는" 계산(SysTick 리로드, BRR, 테이블 생성)은 consteval 이 좋은 기본값이다.

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution   # 31/31 passed
c++ -std=c++20 -Wall -Wextra -g -O1 -fsanitize=address,undefined -fno-exceptions -fno-rtti solution.cpp -o sol_san && ./sol_san   # 31/31, 경고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `return ((1u << width) - 1u) << offset;` | width=32 에서 UB. 상수 문맥이면 컴파일 에러, 런타임이면 플랫폼마다 다른 값 | `width == 32 ? 0xFFFF'FFFFu : (1u << width) - 1u` |
| `offset + width <= 32` | unsigned 랩: `0xFFFF'FFFF + 2 == 1` → 통과 | `width <= 32 && offset <= 32 - width` |
| `constexpr auto* GPIOA = (GPIO_TypeDef*)0x40020000;` | C 캐스트 = reinterpret_cast → constexpr 불가 | 주소는 `constexpr uintptr_t`, 캐스트는 접근 함수 안에서 |
| "constexpr 함수니까 컴파일타임에 계산되겠지" | 상수 문맥 밖이면 보장 없음 (-O0 에서 실제로 `bl mask`) | `constexpr` 변수에 담기 / `consteval` / `static_assert` |
| 범위 위반을 `throw` 로 표시 | `-fno-exceptions` 에선 `cannot use 'throw' with exceptions disabled` | constexpr 아닌 함수(`invalid_field_spec()`) 호출 |
| 헤더에 `constexpr uint32_t X = …;` (namespace scope) | 그것도 OK(내부 링키지 const)지만 TU 마다 복사본. 주소를 비교하면 다름 | C++17 `inline constexpr` |
| `field_prep(0x1F, 4, 4)` 를 조용히 `& 0xF` | 상수 오타를 숨김 → 엉뚱한 모드로 설정 | 상수면 거부해서 빌드 실패시키기 |
| `static_assert` 에 메시지 없이 매직 넘버만 | 실패 시 원인 찾기 어려움 | `"GPIOA base (RM0090 2.3)"` 처럼 출처를 메시지에 |

## 8. 면접 Q&A

<details>
<summary>Q1. constexpr 함수는 항상 컴파일타임에 실행되나요?</summary>

아니다. 상수 문맥(constexpr 변수 초기값, static_assert, 템플릿 인자, 배열 크기, case 라벨 등)에서만 보장된다. 그 밖에선 일반 함수처럼 런타임에 실행될 수 있고, -O2 에서 접히는 건 옵티마이저 재량이다 (이 레슨의 -O0 objdump: `bl mask`). 꼬리질문 "반드시 컴파일타임이게 하려면?" → constexpr 변수에 담거나 C++20 `consteval`. "함수 안에서 지금 상수 평가 중인지 알 수 있나?" → C++20 `std::is_constant_evaluated()`, C++23 `if consteval`.

</details>

<details>
<summary>Q2. constexpr 계산에서 UB 가 생기면 어떻게 되나요? 그게 왜 유용하죠?</summary>

[expr.const]/5 에 따라 그 식은 constant expression 이 아니게 되고, 상수 문맥이면 컴파일러가 반드시 진단한다. 즉 `1u << 32`, signed 오버플로, 배열 범위 밖 접근, null 역참조가 **빌드 에러**로 바뀐다. 레지스터 마스크·주소·테이블처럼 상수로 쓰는 계산을 constexpr 로 만들면 "보드에서 가끔 틀리는" 버그를 빌드에서 막는다. 한계: 정의된 동작(unsigned 랩)은 안 잡히고, 라이브러리 UB 는 진단이 보장되지 않는다.

</details>

<details>
<summary>Q3. 왜 `constexpr GPIO_TypeDef* GPIOA = reinterpret_cast<GPIO_TypeDef*>(0x40020000);` 가 안 되나요? 그럼 어떻게 하나요?</summary>

reinterpret_cast 는 core constant expression 에서 금지다 ([expr.const]/5). 상수 평가기는 포인터를 "어떤 객체를 가리키는지" 로 추적하는데, 정수에서 만든 포인터엔 객체가 없어 역참조나 비교의 유효성을 판단할 수 없다. 해결: 주소를 `constexpr uintptr_t` 로 두고 접근하는 순간(인라인 접근 함수 안)에 캐스트한다. 주소 계산·static_assert 는 그대로 컴파일타임이고, -O2 기계어는 매크로 버전과 명령 단위로 동일하다 (objdump 확인). 템플릿 비타입 인자로 주소를 넘기는 방식은 CPP-B12.

</details>

<details>
<summary>Q4. constexpr, consteval, constinit, const 를 구분해 보세요.</summary>

`const`: 이 이름으로 수정 불가 (런타임 값이어도 됨). `constexpr` 변수: 초기값이 상수식 + const. `constexpr` 함수: 상수 평가 *가능*. `consteval`: 모든 호출이 상수 평가*여야* 함. `constinit` (C++20): 정적 저장 기간 변수의 초기화가 **정적 초기화**여야 함 — 변수 자체는 나중에 바꿀 수 있다 (예: 부팅 시 0 으로 시작하는 카운터를 동적 초기화 없이). 자세한 정적 초기화 순서는 CPP-B09.

</details>

<details>
<summary>Q5. CMSIS 는 왜 아직도 #define 을 쓰나요? constexpr 로 다 바꿔야 하나요?</summary>

CMSIS 헤더는 C 와 C++ 양쪽, 그리고 어셈블리(.S startup)·링커 스크립트와 공유되어야 하고, `#if` 조건부 컴파일에도 쓰인다 — constexpr 는 전처리기 단계에 존재하지 않는다. 그래서 하드웨어 정의 계층은 매크로, 그 위의 C++ 드라이버 계층에서 constexpr 로 감싸는 것이 현실적이다. 꼬리질문 "매크로의 단점?" → 타입·스코프 없음, 인자 다중 평가, 괄호 실수 (C-B15).

</details>

<details>
<summary>Q6. throw 없이 constexpr 함수에서 "이 인자는 잘못됐다" 를 컴파일 에러로 만드는 방법은?</summary>

잘못된 경로에서 constexpr 가 아닌 함수를 호출한다. 상수 평가가 그 호출에 도달하면 에러, 정상 인자는 그 경로를 지나가지 않으므로 OK. 런타임 호출이면 그 함수가 실제로 실행되므로 펌웨어에선 assert 훅·`__BKPT`·리셋 같은 처리를 넣는다. consteval 함수라면 런타임 경로가 없으니 더 간단하다. (참고: C++20 에서는 "어떤 인자로도 상수 평가가 불가능한 constexpr 함수" 는 ill-formed NDR 이었고 C++23 P2448 로 완화됨 — 정상 인자 경로가 있는 한 문제없다.)

</details>

<details>
<summary>Q7. 1 &lt;&lt; 31 은 C 와 C++ 에서 각각 UB 인가요?</summary>

C11: UB (결과가 int 로 표현 불가). C++14/17: CWG1457 로 "대응하는 unsigned 에 표현 가능하면 OK" → 정의됨, 단 `-1 << 1` 은 UB. C++20: 2의 보수 확정(P0907/P1236)으로 signed 왼쪽 시프트는 모듈러로 모두 정의. **그러나 시프트 양이 음수이거나 폭 이상이면 모든 버전에서 UB.** 그래도 레지스터 코드는 습관적으로 `1u` — C 와 공유되는 헤더가 많기 때문.

</details>

## 9. 추가 연습

### 연습 1 — 링버퍼 크기 검사와 인덱스 마스크

`constexpr bool is_pow2(uint32_t)`, `constexpr unsigned log2_exact(uint32_t)` (2의 거듭제곱 아니면 거부) 를 만들고, `RX_SIZE` 가 2의 거듭제곱임을 static_assert 로 강제하라.

<details>
<summary>답 보기</summary>

`x != 0 && (x & (x - 1u)) == 0`. log2 는 constexpr 함수 안 루프로 (`while ((x >>= 1) != 0) ++n;` — C++14 부터 허용). static_assert 덕분에 누가 `RX_SIZE = 200` 으로 바꾸면 `& RX_MASK` 래핑이 깨지기 **전에** 빌드가 멈춘다.

</details>

### 연습 2 — UART BRR 을 컴파일타임에, 오차 검사 포함

STM32 USART (OVER8=0) 는 `BRR = round(f_PCLK / baud)`. 오차가 2% 를 넘으면 거부하는 `uart_brr(fclk, baud)` 를 작성하라. `uart_brr(16 MHz, 115200) == 0x8B`.

<details>
<summary>답 보기</summary>

`(fclk + baud/2) / baud` 로 반올림, `actual = fclk / brr`, `|actual - baud| * 1000 / baud > 20` 이면 `invalid_arg()`. 1 MHz 에서 115200 은 BRR=9 → 111111 bps, 오차 3.5% → 상수로 쓰면 컴파일 에러. "클럭 트리 바꿨더니 UART 가 깨졌다" 를 빌드에서 잡는다. (중간 계산은 `uint64_t` 로 오버플로 회피.)

</details>

### 연습 3 — CRC-8 테이블 컴파일타임 생성

poly 0x07 CRC-8 의 256 엔트리 테이블을 `constexpr std::array` 로 만들고, `crc8("123456789") == 0xF4` 를 static_assert 로 확인하라.

<details>
<summary>답 보기</summary>

constexpr 함수 안에서 `std::array<uint8_t,256> t{}` 를 채워 반환 (std::array 자체는 CPP-B10). 결과는 런타임 초기화 코드 없이 읽기 전용 상수가 된다 — 런타임 `crc8()` 를 부르는 오브젝트를 만들어 보니 `__TEXT,__const` 에 0x100 바이트 `_CRC8_TABLE` 이 생겼다 (ELF 라면 `.rodata` → 플래시). 모든 사용이 상수 문맥이면 테이블은 바이너리에서 아예 사라진다 (practice.o 에는 `__const` 섹션 없음).

</details>

### 연습 4 — Cortex-M3/M4 bit-band 별칭 주소

`alias = 0x4200'0000 + (addr - 0x4000'0000) × 32 + bit × 4` (주변장치 영역 1 MB 안만). GPIOA_ODR(0x4002'0014) 비트 5 의 별칭은?

<details>
<summary>답 보기</summary>

`0x4200'0000 + 0x2'0014 × 32 + 20 = 0x4240'0294`. 범위 밖(0x4010'0000 이상)이나 bit > 31 은 거부. 그 주소에 1 워드를 쓰면 그 비트 하나만 원자적으로 바뀐다 (C-B01 의 RMW 경쟁 해결책 중 하나). Cortex-M7/M33 등에는 bit-band 가 없다.

</details>

### 연습 5 — consteval SysTick 리로드

`consteval uint32_t systick_reload(cpu_hz, tick_hz)` = `cpu_hz / tick_hz - 1`, 24비트(0xFF'FFFF) 초과면 거부.

<details>
<summary>답 보기</summary>

168 MHz / 1 kHz → 167999 OK. 168 MHz / 1 Hz → 167,999,999 > 0xFF'FFFF → 상수 평가 실패. consteval 이라 런타임 변수로 부르는 순간도 에러 → "이 값은 반드시 빌드 타임에 확정" 이라는 의도가 타입 시스템에 박힌다.

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice   # 13/13 passed
```

## 10. 핵심 정리

- 주소·마스크는 `inline constexpr uintptr_t / uint32_t` 상수 + `constexpr` 함수로. 기계어엔 즉시값만 남는다 (매크로와 명령 단위로 동일).
- `mask(o, w)`: `w == 32 ? 0xFFFF'FFFFu : (1u << w) - 1u`, 범위는 `w <= 32 && o <= 32 - w` (랩 방지).
- **상수 평가 중 UB = 컴파일 에러** ([expr.const]). 범위 위반은 constexpr 아닌 함수 호출로 에러를 유도 (`-fno-exceptions` 라 throw 불가).
- constexpr 함수는 상수 문맥에서만 컴파일타임 보장. 확정하려면 constexpr 변수 / `consteval` / `static_assert`.
- `reinterpret_cast`(C 캐스트 포함)는 상수식 불가 → 주소는 정수로 들고 다니다가 **접근 순간에** 캐스트.
- `static_assert(식, "출처")` 로 메모리 맵을 데이터시트에 못 박기 = 비용 0 단위 테스트.
- `#define` 은 `#if`·.S·링커 스크립트용으로 남고, 드라이버 계층은 constexpr.

## 참고자료

- [cppreference — constexpr specifier](https://en.cppreference.com/w/cpp/language/constexpr) / [consteval](https://en.cppreference.com/w/cpp/language/consteval) / [constant expressions](https://en.cppreference.com/w/cpp/language/constant_expression)
- C++20 표준 (N4861) [dcl.constexpr], [expr.const]/5 (core constant expression 제외 목록: UB, reinterpret_cast, 비constexpr 함수), [expr.shift]
- [P1236R1 — Alternative wording for P0907R4 Signed Integers are Two's Complement](https://wg21.link/p1236r1), [CWG1457](https://wg21.link/cwg1457)
- [STM32F4 Reference Manual RM0090 — 2.3 Memory map, RCC_AHB1ENR, GPIO registers, USART BRR](https://www.st.com/resource/en/reference_manual/rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [ARM Cortex-M4 Technical Reference Manual (DDI 0439) — Bit-banding 절](https://developer.arm.com/documentation/ddi0439/b/)
- Jason Turner, *C++ Weekly* (YouTube) — constexpr 관련 에피소드 다수
