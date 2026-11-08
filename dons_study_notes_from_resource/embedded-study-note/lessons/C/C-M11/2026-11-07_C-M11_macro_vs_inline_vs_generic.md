---
id: C-M11
time: 75분
updated: 2026-11-07
---
# C-M11 매크로 vs inline 함수 vs _Generic

## 학습 목표

- C-B15 의 함수형 매크로(MAX / ABS / SQUARE / REG_FIELD_WRITE / ARRAY_SIZE)를 `static inline` 함수로 바꾸고, 바꾸면서 **고쳐지는 버그**(다중 평가, `ABS(INT32_MIN)`·`SQUARE(50000)` 의 오버플로 UB, NaN 순서 의존)를 테스트로 보여준다.
- C11 `_Generic` 으로 `max_of(a, b)` · `abs_of(x)` · `type_name(x)` 를 만들고, 제어식(controlling expression)이 **평가되지 않고 lvalue 변환을 거친 타입만** 쓰인다는 규칙에서 나오는 함정(한정자 제거, 배열 decay, `char` 3형제, `'a'` 는 `int`, `int32_t` 의 정체)을 설명한다.
- `-O0` / `-O2` / Cortex-M4 `-Os` 의 실제 어셈블리와 `.text` 크기를 측정해 "inline 함수는 매크로보다 느리다/크다" 가 언제 맞고 언제 틀린지 숫자로 말한다.
- 그래도 매크로가 필요한 곳 — `#` 문자열화, `##` 토큰 붙이기, `__FILE__`/`__LINE__`/`__func__` 캡처, 정수 상수식(배열 크기·`case`·`_Static_assert`), `#if` 플랫폼 분기 — 을 구분하고, "매크로는 껍데기만, 로직은 inline" 하이브리드로 쓴다.

## 1. 문제

C-B15 에서 매크로를 괄호·do-while(0)·GNU statement expression 으로 "덜 위험하게" 만들었다. 면접의 다음 꼬리질문은 거의 항상 **"그럼 그냥 함수로 쓰면 안 돼요? 성능은요? 타입마다 함수를 만들어야 하나요?"** 다. MISRA C(Dir 4.9)와 대부분의 펌웨어 코딩 규칙도 "함수로 바꿀 수 있으면 함수로" 를 요구하므로, 무엇을 함수로 바꾸고 무엇을 매크로로 남기는지 **근거(asm, 크기, 표준 조항)** 를 들어 말할 수 있어야 한다.

C11 에는 도구가 세 개 있다. 셋은 **번역의 서로 다른 단계** 에서 일하기 때문에 할 수 있는 일이 다르다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">같은 MAX 를 세 가지로 — 누가, 언제, 무엇을 보고 처리하나</text>
  <g stroke="currentColor">
    <rect x="10" y="34" width="230" height="44" rx="6" class="f-mut"/>
    <rect x="255" y="34" width="230" height="44" rx="6" class="f-mut"/>
    <rect x="500" y="34" width="230" height="44" rx="6" class="f-mut"/>
  </g>
  <g fill="currentColor">
    <text x="20" y="52" font-weight="bold">① 전처리기 (번역 단계 4)</text><text x="20" y="70" font-size="12">토큰(글자)만 본다, 타입 모름</text>
    <text x="265" y="52" font-weight="bold">② 컴파일러 의미 분석</text><text x="265" y="70" font-size="12">식의 타입을 안다</text>
    <text x="510" y="52" font-weight="bold">③ 최적화 · 코드 생성</text><text x="510" y="70" font-size="12">값과 레지스터를 다룬다</text>
  </g>
  <g stroke="currentColor" fill="none"><path d="M240 56 L253 56 M485 56 L498 56"/></g>
  <g stroke="currentColor">
    <rect x="10" y="90" width="230" height="200" rx="8" class="f-warn"/>
    <rect x="255" y="90" width="230" height="200" rx="8" class="f-acc"/>
    <rect x="500" y="90" width="230" height="200" rx="8" class="f-ok"/>
  </g>
  <g fill="currentColor" font-weight="bold">
    <text x="20" y="110">매크로</text><text x="265" y="110">_Generic (C11)</text><text x="510" y="110">static inline (C99)</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="130">((a) &gt; (b) ? (a) : (b))</text>
    <text x="265" y="130">_Generic((a)+(b), int: …)</text>
    <text x="510" y="130">max_i32(int32_t, int32_t)</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="155">+ # , ## 로 이름 · 문자열 생성</text>
    <text x="20" y="175">+ __FILE__ / __LINE__ 캡처</text>
    <text x="20" y="195">+ 상수식 (case, 배열 크기)</text>
    <text x="20" y="225">− 타입 검사 없음</text>
    <text x="20" y="245">− 인자를 여러 번 평가</text>
    <text x="20" y="265">− 디버거로 들어갈 수 없음</text>
    <text x="265" y="155">+ 타입별 함수 선택</text>
    <text x="265" y="175">+ 선택은 컴파일 타임 (비용 0)</text>
    <text x="265" y="195">+ 표준 C11, 확장 불필요</text>
    <text x="265" y="225">− 재사용하려면 매크로로 감싸야</text>
    <text x="265" y="245">− 승격 · 한정자 규칙의 함정</text>
    <text x="265" y="265">− 표에 없는 타입은 에러/default</text>
    <text x="510" y="155">+ 타입 검사 · 변환이 명시적</text>
    <text x="510" y="175">+ 인자를 정확히 한 번 평가</text>
    <text x="510" y="195">+ 디버거 스텝, 주소 가능</text>
    <text x="510" y="225">− 타입마다 함수 하나</text>
    <text x="510" y="245">− #, __LINE__, 상수식 불가</text>
    <text x="510" y="265">− -O0 에서는 실제 bl 호출</text>
  </g>
</svg>
```

`starter.c` 는 C-B15 의 매크로를 대체할 함수·`_Generic` 매크로의 **껍데기**만 있다. 테스트 56개 중 18개만 통과한다. TODO 만 채워서 56/56 을 만든다.

### Part A — static inline: 타입이 정해진 함수

- `max_i32`, `max_u32`: 인자는 호출 전에 한 번만 평가된다 → `max_i32(i++, j)` 는 `i` 를 한 번만, `max_u32(uart_read_dr(), 4u)` 는 FIFO 에서 한 바이트만 꺼낸다. (비교용으로 C-B15 의 `MAX` 는 두 바이트를 꺼내는 것을 테스트가 같이 보여준다.)
- `max_f`: `fmaxf` 와 같은 NaN 규칙 — 한쪽만 NaN 이면 다른 쪽을 돌려준다. `MAX(1.0f, NAN)` 은 NaN, `MAX(NAN, 1.0f)` 은 1 — **순서에 따라 다르다**.
- `uabs_i32(int32_t) → uint32_t`: `INT32_MIN` 도 `2147483648u` 로. (`ABS(INT32_MIN)` 은 부호 있는 오버플로 UB.)
- `square_i32(int32_t) → int64_t`: `square_i32(50000) == 2500000000`. 함수는 **반환 타입을 고를 수 있다** — 매크로에는 없는 능력.

### Part B — _Generic: 타입별 디스패치

- `max_of(a, b)`: `(a) + (b)` 의 타입으로 `max_i32 / max_u32 / max_i64 / max_u64 / max_f / max_d` 를 고른다. 결과 타입도 보존(`max_of(1u, 2u)` 는 `unsigned int`). `default` 는 두지 않는다 — 표에 없는 타입은 컴파일 에러.
- `abs_of(x)`: 정수는 부호 없는 결과(`abs_of(INT32_MIN) == 2147483648u`), 실수는 `fabsf`/`fabs`.
- `type_name(x)`: 식의 타입 이름 문자열. 테스트가 `_Generic` 함정 12개를 찌른다 — `'A'`, `const int`, `volatile`, 배열, 문자열 리터럴, `true`, `uint8_t + uint8_t` …

### Part C — 매크로가 꼭 필요한 곳만 매크로로

- `LOG_VAR(x)`: `"solution.c:276 rx_cnt=5"` 처럼 **파일·줄·변수 이름·값** 을 남기는 펌웨어 로그. 이름(`#x`)과 위치(`__FILE__`, `__LINE__`)는 매크로만 얻을 수 있고, 값 포맷은 `_Generic` 이 고른 inline 함수가 한다. `x` 는 정확히 한 번 평가.
- `reg_field_write()`: C-B15 의 `REG_FIELD_WRITE` 를 **"`##` 이름 조립은 매크로, read-modify-write 는 inline 함수"** 로 쪼갠다 (필드 연산은 C-B04, RMW 위험은 C-M02 참고).
- `IS_ARRAY(a)`: 배열이면 1, 포인터면 0 인 **정수 상수식**. 이걸로 `ARRAY_SIZE(포인터)` 를 컴파일 에러로 만든다 — ARRAY_SIZE 는 함수가 될 수 없는 대표 사례.

## 2. Problem (English)

> **Replace macros with type-safe code.** A legacy firmware header defines
>
> ```c
> #define MAX(a, b)       ((a) > (b) ? (a) : (b))
> #define ABS(x)          ((x) < 0 ? -(x) : (x))
> #define SQUARE(x)       ((x) * (x))
> #define ARRAY_SIZE(a)   (sizeof(a) / sizeof((a)[0]))
> ```
>
> 1. Write `static inline` replacements `max_i32`, `max_u32`, `max_f` (NaN-tolerant like `fmaxf`), `uabs_i32(int32_t) -> uint32_t` (correct for `INT32_MIN`) and `square_i32(int32_t) -> int64_t`. Each argument must be evaluated exactly once (`max_u32(UART->DR, 4)` pops one byte).
> 2. Using C11 `_Generic`, write `max_of(a, b)` that dispatches on the common type of `a + b` and preserves the result type, `abs_of(x)`, and `type_name(x)` returning `"int"`, `"char *"`, … Explain what `type_name('A')`, `type_name(const_int_var)`, `type_name(int_array)` and `type_name("abc")` return and why.
> 3. Compare the generated code of `MAX`, `max_i32` and `max_of` at `-O0` and `-O2` (host and Cortex-M4), and the `.text` size of a small DSP-style module written each way.
> 4. Some things cannot be functions. Write `LOG_VAR(x)` that logs `file:line name=value` (value formatted by type, `x` evaluated once), a `REG_FIELD_WRITE` whose token pasting stays in a macro but whose read-modify-write is an inline function, and an `IS_ARRAY(a)` integer constant expression so that `ARRAY_SIZE(pointer)` fails to compile.
>
> Constraints: `-std=c11 -Wall -Wextra -Werror`; no GNU statement expressions. `__typeof__` is allowed only inside `IS_ARRAY` (state that it is an extension in C11, standard `typeof` in C23).

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크 문제 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# FAIL 38줄 + "18/56 passed" — TODO 11곳(함수 6, 매크로 5)을 채워 56/56
```

> 18개가 처음부터 통과하는 건 의도다: 이미 채워진 64비트 함수들과, "C-B15 매크로는 FIFO 를 두 번 읽는다 / NaN 이 샌다" 같은 **비교용 테스트** 다.

## 4. 힌트

<details>
<summary>힌트 1 — INT32_MIN 의 절댓값은 int32_t 에 없다</summary>

`-x` 는 `x == INT32_MIN` 일 때 오버플로(UB). 부호 없는 타입으로 바꾼 **뒤에** 뒤집으면 모듈러 연산이라 안전하다: `0u - (uint32_t)x`. (C-B02 의 변환 규칙: 음수 → unsigned 는 2^32 를 더한 값.)

</details>

<details>
<summary>힌트 2 — NaN 은 모든 비교에서 거짓</summary>

`a > b` 는 둘 중 하나라도 NaN 이면 거짓이라 `b` 를 돌려준다. 그래서 순진한 MAX 는 "NaN 이 뒤에 있으면 NaN" 이 된다. `isnan(a)` 를 먼저 보면 된다 (`<math.h>` 의 매크로, `-ffast-math` 에서는 믿을 수 없다).

</details>

<details>
<summary>힌트 3 — _Generic 의 결과는 "함수 이름" 이어도 된다</summary>

`_Generic(식, 타입1: 결과1, 타입2: 결과2, default: 결과)` 는 식 하나를 고른다. 고른 게 함수 지정자(function designator)면 뒤에 `(인자)` 를 붙여 바로 호출할 수 있다: `_Generic((a) + (b), int: max_i32, ...)((a), (b))`. 인자는 이 호출에서 한 번만 평가된다.

</details>

<details>
<summary>힌트 4 — 제어식에 무엇을 넣을지가 설계다</summary>

`(a)` 만 넣으면 `max_of(1, 2u)` 는 `int` 함수로 가서 `2u` 가 int 로 변환된다. `(a) + (b)` 를 넣으면 C 의 **보통 산술 변환** 결과(공통 타입)로 고른다 — 매크로 `MAX` 와 똑같은 비교 의미가 된다. 작은 정수(`uint8_t`)는 `+` 에서 `int` 로 승격되므로 표에 `unsigned char` 를 넣을 필요가 없다.

</details>

<details>
<summary>힌트 5 — type_name 에 const int 항목을 넣지 마라</summary>

제어식은 lvalue 변환을 거친 타입으로 비교된다: 최상위 `const`/`volatile` 이 떨어지고, 배열은 포인터로, 함수는 함수 포인터로 바뀐다. 그래서 `const int` 항목은 영원히 선택되지 않는다 (clang 은 경고까지 낸다). 반면 `const char *` 의 const 는 **가리키는 쪽** 이라 남는다.

</details>

<details>
<summary>힌트 6 — LOG_VAR 의 재료</summary>

`#x` (이름 문자열), `__FILE__`, `__LINE__` 은 매크로가 **쓰인 줄** 에서 펼쳐진다. 포맷 함수는 이미 4개(`log_i64/u64/dbl/str`) 있으니 `_Generic((x) + 0, ...)` 으로 고르고 `(__FILE__, __LINE__, #x, (x))` 로 부른다. `+ 0` 은 `uint8_t` 를 `int` 로, `char dev[6]` 을 `char *` 로 만든다.

</details>

<details>
<summary>힌트 7 — IS_ARRAY: &amp; 는 decay 를 막는다</summary>

`&arr` 의 타입은 `int (*)[4]` (배열을 가리키는 포인터), `&p` 는 `int **`. 그러니 "`&(a)` 가 `T (*)[N]` 인가?" 를 `_Generic` 으로 물으면 된다. `T` 는 `__typeof__(*(a))`, `N` 은 `sizeof(a) / sizeof(*(a))`. clang 의 `-Wsizeof-pointer-div` 경고는 나눗셈 오른쪽을 괄호로 한 번 더 감싸면 꺼진다.

</details>

## 5. 풀이

### Part A — 함수로 바꾸면 무엇이 고쳐지나

| C-B15 매크로 | 문제 | inline 함수 | 고쳐지는 이유 |
|---|---|---|---|
| `MAX(uart_read_dr(), 4u)` | 인자 2번 평가 → FIFO 2바이트 pop, 결과 9 | `max_u32(uart_read_dr(), 4u)` → 7, 1바이트 | 함수 호출은 인자를 **호출 전에 한 번** 평가 (C11 6.5.2.2p4: 인자 값이 매개변수에 대입) |
| `ABS(INT32_MIN)` | `-(x)` 오버플로 → **UB** | `uabs_i32` → `2147483648u` | 반환 타입을 `uint32_t` 로, 뒤집기는 unsigned 에서 |
| `SQUARE(50000)` | `int * int` 오버플로 → **UB** | `square_i32` → `int64_t` 2500000000 | `(int64_t)x * x` — 곱셈 전에 넓힌다 |
| `MAX(1.0f, NAN)` | NaN, `MAX(NAN, 1.0f)` 은 1 | `max_f` → 둘 다 1 | 분기를 마음대로 쓸 수 있다 |
| `MAX(i++, j)` | i 가 2 증가 (UB 는 아님, C-B15) | `max_i32(i++, j)` → i 1 증가 | 한 번 평가 |

UB 두 줄은 테스트에 넣을 수 없으니 `demo_macro_ub.c` 로 UBSan 을 돌렸다 (실제 출력):

```text
$ cc -std=c11 -g -O0 -fsanitize=undefined demo_macro_ub.c -o demo_macro_ub && ./demo_macro_ub
demo_macro_ub.c:17:42: runtime error: negation of -2147483648 cannot be represented in type 'int32_t' (aka 'int'); cast to an unsigned type to negate this value to itself
demo_macro_ub.c:18:42: runtime error: signed integer overflow: 50000 * 50000 cannot be represented in type 'int32_t' (aka 'int')
ABS(INT32_MIN)      = -2147483648
SQUARE(50000)       = -1794967296
uabs_i32(INT32_MIN) = 2147483648
square_i32(50000)   = 2500000000
```

UBSan 메시지가 고치는 법("cast to an unsigned type")까지 알려준다. sanitizer 없이 `-O2` 로 돌려도 이 맥에서는 같은 숫자가 나왔지만, UB 이므로 "그 숫자가 나온다" 는 보장은 없다 (C-M12 참고).

**함수가 고쳐주지 않는 것**: 타입이 고정되면 인자가 **조용히 변환** 된다. `max_u32(-1, 1)` 은 `-1` 이 `4294967295` 로 바뀌어 `4294967295` 를 돌려주고, `-Wall -Wextra` 로는 경고가 없다(`-Wsign-conversion` 을 켜야 잡힌다). 함수는 타입을 *명시적으로* 만들 뿐, 잘못된 타입을 고르는 실수까지 막아 주지는 않는다.

### Part A — 코드 생성: -O0 / -O2 어셈블리 (`demo_codegen.c`)

같은 일을 하는 세 함수 `f_macro` (`MAX`), `f_inline` (`max_i32`), `f_generic` (`max_of`) 를 컴파일했다. Apple clang 21, arm64, `cc -std=c11 -O0 -S demo_codegen.c -o -` 실제 출력(디렉티브 생략):

```text
_f_macro:                         _f_inline:                       _f_generic:
  sub   sp, sp, #16                 sub   sp, sp, #32                (f_inline 과 한 글자도 안 다름)
  str   w0, [sp, #12]               stp   x29, x30, [sp, #16]          ...
  str   w1, [sp, #8]                add   x29, sp, #16                 bl    _max_i32
  ldr   w8, [sp, #12]               stur  w0, [x29, #-4]               ...
  ldr   w9, [sp, #8]                str   w1, [sp, #8]
  subs  w8, w8, w9                  ldur  w0, [x29, #-4]
  b.le  LBB0_2                      ldr   w1, [sp, #8]
  ...                               bl    _max_i32          ← 진짜 호출
  ret                               ldp   x29, x30, [sp, #16]
                                    add   sp, sp, #32
                                    ret
                                  _max_i32:  (로컬 심볼 t, 본문 17명령)
```

- `-O0` 에서 `static inline` 은 **인라인되지 않는다**. `inline` 은 "인라인해도 된다" 는 허락이지 명령이 아니다(C11 6.7.4p6: "the extent to which such suggestions are effective is implementation-defined"). 매크로는 전처리 단계에서 이미 펼쳐졌으니 `-O0` 에서도 호출이 없다.
- `f_generic` 이 `bl _max_i32` 로 **바로** 간다 — 타입 검사·분기 코드가 없다. `_Generic` 은 컴파일 중에 끝나는 선택이라 `-O0` 에서도 런타임 비용이 0이다.

`-O2` 에서는 셋이 **바이트 단위로 같다**:

```text
_f_macro:               _f_inline:              _f_generic:
  cmp   w0, w1            cmp   w0, w1            cmp   w0, w1
  csel  w0, w0, w1, gt    csel  w0, w0, w1, gt    csel  w0, w0, w1, gt
  ret                     ret                     ret
```

차이가 남는 건 **의미가 다른 경우** 뿐이다. `MAX(UART_DR, 4u)` vs `max_u32(UART_DR, 4u)` (`UART_DR` 은 `extern volatile uint32_t`), `-O2` arm64:

```text
_peak_macro:                              _peak_inline:
  adrp  x8, _UART_DR@GOTPAGE                adrp  x8, _UART_DR@GOTPAGE
  ldr   x8, [x8, _UART_DR@GOTPAGEOFF]       ldr   x8, [x8, _UART_DR@GOTPAGEOFF]
  ldr   w9, [x8]          ← 1번째 읽기      ldr   w8, [x8]          ← 딱 한 번
  cmp   w9, #5                              mov   w9, #4
  b.lo  LBB3_2                              cmp   w8, #4
  ldr   w0, [x8]          ← 2번째 읽기!     csel  w0, w8, w9, hi
  ret                                       ret
```

Cortex-M4 (`clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -Os -S`) 도 같은 이야기다:

```text
peak_macro:                          peak_inline:
  movw  r0, :lower16:UART_DR           movw  r0, :lower16:UART_DR
  movt  r0, :upper16:UART_DR           movt  r0, :upper16:UART_DR
  ldr   r1, [r0]                       ldr   r0, [r0]
  cmp   r1, #5                         cmp   r0, #4
  ite   hs                             it    ls
  ldrhs r0, [r0]     ← 조건부 재읽기    movls r0, #4
  movlo r0, #4                         bx    lr
  bx    lr
```

`volatile` 은 "소스에 적힌 접근을 빼지 마라" 이므로(C-B09) 컴파일러는 매크로가 만든 **두 번째 읽기를 지울 수 없다**. 최적화기가 고쳐주는 게 아니라, 소스 수준에서 한 번만 읽게 써야 한다.

### Part A — 코드 크기: 측정한 숫자 (`demo_size.c`, `demo_crc_size.c`)

`demo_size.c` 는 펌웨어스러운 함수 3개(`peak_abs` 샘플 절댓값 피크, `clamp_buf` 버퍼 클램프, `rx_peak` FIFO 최대값)를 `-DIMPL=1` 매크로 / `2` inline / `3` `_Generic` 으로 빌드한다. 호스트는 `size -m` 의 `Section (__TEXT, __text)`, Cortex-M4 는 `xcrun llvm-size -A` 의 `.text`:

| 최적화 | arm64 매크로 | arm64 inline | arm64 _Generic | M4 매크로 | M4 inline | M4 _Generic |
|---|---|---|---|---|---|---|
| `-O0` | 668 | 632 | 632 | 376 | 342 | 342 |
| `-Os` | 148 | 144 | 144 | 96 | 98 | 98 |
| `-O2` | 552 | 540 | 540 | 486 | 504 | 504 |

함수별로 쪼개 보면 (M4, `xcrun llvm-nm -S`):

| | `peak_abs` | `clamp_buf` | `rx_peak` | 헬퍼 함수 |
|---|---|---|---|---|
| `-O0` 매크로 | 154 | 140 | 80 | 없음 |
| `-O0` inline | 68 | 70 | 58 | `max_i32`·`max_u32`·`min_i32`·`uabs_i32` 각 34 |
| `-Os` 매크로 / inline | 34 / 34 | 28 / 28 | **34 / 36** | 없음 |
| `-O2` 매크로 / inline | 180 / 180 | 186 / 186 | **118 / 136** | 없음 |

읽는 법:

- `_Generic` 열은 inline 열과 **항상 같다** — `_Generic` 은 함수를 고를 뿐 코드를 만들지 않는다.
- `-O0`: 매크로는 쓰는 곳마다 본문이 복사되고, inline 은 본문 1벌 + `bl`. 이 예에서는 inline 이 작았다(342 vs 376) — 대신 호출 비용이 있다.
- `-Os`/`-O2`: `peak_abs`, `clamp_buf` 는 **바이트까지 동일**. 차이는 `rx_peak` 하나뿐인데, 그건 매크로가 DR 을 두 번 읽는 **다른 프로그램** 이기 때문이다. M4 에서는 틀린 쪽(매크로)이 2~18바이트 작고, arm64 에서는 오히려 크다. 크기는 정확성보다 뒤다.
- `-O2` 가 `-O0` 보다 큰 건 루프 펼치기 때문이다. 플래시가 작은 MCU 는 보통 `-Os`/`-Oz` 를 쓴다.

```svg
<svg viewBox="0 0 740 345" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">Cortex-M4 .text 바이트 (clang --target=thumbv7em-none-eabi, 실측)</text>
  <g fill="currentColor">
    <text x="10" y="48" font-weight="bold">demo_size -O0</text>
    <text x="10" y="118" font-weight="bold">demo_size -Os</text>
    <text x="10" y="188" font-weight="bold">demo_size -O2</text>
    <text x="10" y="258" font-weight="bold">demo_crc -Os</text>
    <text x="10" y="273">(본문 큰 헬퍼,</text><text x="10" y="288">호출 22곳)</text>
  </g>
  <g stroke="currentColor">
    <rect x="160" y="36" width="207" height="14" class="f-warn"/>
    <rect x="160" y="54" width="188" height="14" class="f-ok"/>
    <rect x="160" y="72" width="188" height="14" class="f-acc"/>
    <rect x="160" y="106" width="53" height="14" class="f-warn"/>
    <rect x="160" y="124" width="54" height="14" class="f-ok"/>
    <rect x="160" y="142" width="54" height="14" class="f-acc"/>
    <rect x="160" y="176" width="267" height="14" class="f-warn"/>
    <rect x="160" y="194" width="277" height="14" class="f-ok"/>
    <rect x="160" y="212" width="277" height="14" class="f-acc"/>
    <rect x="160" y="246" width="428" height="14" class="f-warn"/>
    <rect x="160" y="264" width="428" height="14" class="f-ok"/>
    <rect x="160" y="282" width="105" height="14" class="f-mut"/>
  </g>
  <g fill="currentColor">
    <text x="375" y="47">매크로 376</text><text x="356" y="65">inline 342</text><text x="356" y="83">_Generic 342</text>
    <text x="221" y="117">매크로 96</text><text x="222" y="135">inline 98</text><text x="222" y="153">_Generic 98</text>
    <text x="435" y="187">매크로 486</text><text x="445" y="205">inline 504</text><text x="445" y="223">_Generic 504</text>
    <text x="596" y="257">매크로 778</text><text x="596" y="275">inline 778</text><text x="273" y="293">noinline 190</text>
  </g>
  <text x="10" y="318" fill="currentColor">막대 길이 = 바이트 × 0.55. inline 과 _Generic 은 모든 경우에 같은 크기다.</text>
  <text x="10" y="336" fill="currentColor">매크로는 쓰는 곳마다 복사를 강제하고, 함수에는 noinline 이라는 선택지가 있다.</text>
</svg>
```

`demo_crc_size.c` 는 반대 상황 — **본문이 큰 헬퍼**(비트 단위 CRC-8 한 바이트 처리, 8회 루프)를 22곳에서 쓴다:

| 빌드 (M4) | `-Os` .text | `-O2` .text | 함수별 (`-Os`) |
|---|---|---|---|
| `-DUSE_MACRO` 문장 매크로 | 778 | 2344 | `blk_crc` 572, `hdr_crc` 138, `cmd_crc` 68 |
| 기본 `static inline` | 778 | 2344 | 위와 동일 (clang 이 22곳 전부 인라인) |
| `-DNOINLINE` (`__attribute__((noinline))`) | **190** | **260** | `crc8_step` 34 + `blk_crc` 104, `hdr_crc` 32, `cmd_crc` 20 |

솔직한 관찰: clang 은 `-Os` 에서도 이 헬퍼를 전부 인라인해서 inline 버전이 매크로와 **똑같이 778** 바이트였다. "inline 함수면 컴파일러가 알아서 크기를 줄인다" 는 이 측정에서는 성립하지 않았다. 하지만 함수에는 **손잡이** 가 있다 — `noinline` 한 줄로 778 → 190 (−76%). 매크로는 "호출할 때마다 복사" 외의 선택지가 없다. 플래시 16 KB 짜리 부트로더라면 이 차이가 결정적이다. (`noinline`/`always_inline` 은 GCC/clang 확장, IAR 은 `#pragma inline=never`.)

### Part B — _Generic 은 어떻게 고르나

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">r = max_of(i++, j);  (int i = 5, j = 3) — 펼치고, 타입만 보고, 고르고, 호출</text>
  <rect x="10" y="34" width="150" height="34" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="56" fill="currentColor" font-family="monospace">max_of(i++, j)</text>
  <path d="M160 51 L180 51" stroke="currentColor" fill="none"/><path d="M174 46 L180 51 L174 56" stroke="currentColor" fill="none"/>
  <rect x="185" y="34" width="545" height="34" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="195" y="56" fill="currentColor" font-family="monospace" font-size="12">_Generic((i++) + (j), int: max_i32, unsigned int: max_u32, …)((i++), (j))</text>
  <rect x="10" y="90" width="230" height="150" rx="8" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="20" y="110" font-weight="bold" font-size="13">① 제어식 (i++) + (j)</text>
    <text x="20" y="132">평가하지 않는다 → i 는 그대로 5</text>
    <text x="20" y="152">타입만 계산: int + int → int</text>
    <text x="20" y="172">lvalue 변환: const/volatile 제거</text>
    <text x="20" y="192">배열 · 함수 → 포인터로 decay</text>
    <text x="20" y="212">+ 연산이라 승격·공통 타입 적용</text>
    <text x="20" y="232">(uint8_t + uint8_t → int)</text>
  </g>
  <path d="M240 165 L253 165" stroke="currentColor" fill="none"/><path d="M247 160 L253 165 L247 170" stroke="currentColor" fill="none"/>
  <rect x="255" y="90" width="230" height="150" rx="8" fill="none" stroke="currentColor"/>
  <text x="265" y="110" fill="currentColor" font-weight="bold">② 연관 목록에서 호환 타입 찾기</text>
  <rect x="263" y="118" width="214" height="20" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="270" y="133">int          → max_i32  ✔</text>
    <text x="270" y="153">unsigned int → max_u32</text>
    <text x="270" y="171">long         → MAX_LONG_FN</text>
    <text x="270" y="189">float        → max_f</text>
    <text x="270" y="207">double       → max_d</text>
  </g>
  <text x="265" y="230" fill="currentColor" font-size="12">없고 default 도 없으면 → 컴파일 에러</text>
  <path d="M485 165 L498 165" stroke="currentColor" fill="none"/><path d="M492 160 L498 165 L492 170" stroke="currentColor" fill="none"/>
  <rect x="500" y="90" width="230" height="150" rx="8" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="510" y="110" font-weight="bold" font-size="13">③ 결과 식 = 함수 지정자</text>
    <text x="510" y="134" font-family="monospace">max_i32((i++), (j))</text>
    <text x="510" y="158">여기서 i++ 가 처음이자</text>
    <text x="510" y="176">마지막으로 평가된다</text>
    <text x="510" y="200">r = 5, i = 6</text>
    <text x="510" y="224">-O0 에서도 bl _max_i32 직행</text>
  </g>
  <text x="10" y="265" fill="currentColor" font-size="12">C11 6.5.1.1p3: 제어식은 평가되지 않고, 선택되지 않은 결과 식도 평가되지 않는다.</text>
  <text x="10" y="284" fill="currentColor" font-size="12">그래서 (i++) 가 글자로는 두 번 나와도 실행은 한 번이다.</text>
  <text x="10" y="303" fill="currentColor" font-size="12">단, 선택되지 않은 결과 식도 "유효한 식" 이어야 컴파일된다 (추가 연습 1 의 함정).</text>
</svg>
```

`cc -E -P solution.c` 로 본 실제 확장 (macOS 라 `long` 이 `max_i64` 로 갔다):

```text
r = _Generic((i++) + (j), int: max_i32, unsigned int: max_u32, long: max_i64, unsigned long: max_u64, long long: max_i64, unsigned long long: max_u64, float: max_f, double: max_d)((i++), (j));
```

**_Generic 규칙 (C11 6.5.1.1)**

| 규칙 | 결과 | 이 레슨에서 |
|---|---|---|
| 제어식은 평가 안 됨 (p3) | `max_of(i++, j)` 의 제어식 `i++` 는 실행 안 됨 | 테스트 `i == 6` |
| 같은(호환) 타입 항목 2개 금지 (p2) | `int: f, int32_t: g` → 맥/ARM 에서 **에러** (`int32_t` 가 곧 `int`) | 아래 "typedef 함정" |
| default 는 최대 1개, 없으면 반드시 하나와 맞아야 함 (p2) | `max_of(long double, …)` → 컴파일 에러 | `demo_generic.c -DLONG_DOUBLE` |
| 항목 타입은 완전한 객체 타입, VLA 류 불가 (p2) | `int[4]:` 는 문법상 가능하지만 decay 때문에 `x` 로는 절대 안 걸림 | `IS_ARRAY` 는 그래서 `&(a)` 를 쓴다 |
| 제어식의 타입 = lvalue 변환 후 타입 | 최상위 `const`/`volatile`/`_Atomic` 제거, 배열·함수 decay | 아래 표 |

마지막 줄은 C11 원문이 애매해서 컴파일러마다 달랐고, DR 481 을 거쳐 C17 에서 "lvalue 변환·배열→포인터·함수→포인터 변환을 거친 것처럼" 이라고 명문화됐다. 지금의 clang 은 친절하게 경고까지 한다 (`demo_generic.c -DCONST_ASSOC` 실제 출력):

```text
demo_generic.c:69:41: warning: due to lvalue conversion of the controlling expression, association of type 'const int' will never be selected because it is qualified [-Wunreachable-code-generic-assoc]
```

**type_name 이 실제로 돌려준 것** — `demo_generic.c` 맥 실행 결과와, 같은 `_Generic` 을 `clang --target=thumbv7em-none-eabi -O2 -S` 로 컴파일해 고른 문자열(`.asciz`)을 확인한 결과 (실행은 못 하므로 asm 으로 확인):

| 식 | arm64 macOS | Cortex-M4 (clang) | 이유 |
|---|---|---|---|
| `'a'` | int | int | 문자 상수는 C 에서 `int` (6.4.4.4p10). C++ 은 `char` |
| `(char)'a'` | char | char | `char` 는 `signed char`·`unsigned char` 와 **다른 타입** (6.2.5p15) |
| `(uint8_t)1` / `(int8_t)1` | unsigned char / signed char | 같음 | `uint8_t` 는 `unsigned char` 의 별명일 뿐 |
| `CHAR_MIN` | -128 | **0** | ARM AAPCS 의 plain `char` 는 unsigned. 그래도 타입은 여전히 `char` |
| `const int ci` / `volatile unsigned vr` | int / unsigned int | 같음 | 최상위 한정자 제거 |
| `int arr[4]` / `"abc"` | int * / char * | 같음 | decay. 문자열 리터럴은 C 에서 `char[4]` (const 아님, 6.4.5p6) |
| `true` | int | int | C11 `<stdbool.h>` 의 `true` 는 매크로 `1` (7.18p3). C23 부터 `bool` |
| `(uint8_t)1 + (uint8_t)1` | int | int | 정수 승격 (C-B02) |
| `(int32_t)0` | int | int | clang 기준. **arm-none-eabi-gcc(newlib)는 `long`** 으로 알려져 있다 (이 맥엔 gcc 가 없어 직접 확인 못 함) |
| `(int64_t)0` | long long | long long | x86-64 Linux glibc 에서는 `long` |
| `(size_t)0`, `sizeof(int)` | unsigned long | **unsigned int** | `size_t` 는 플랫폼마다 다르다 |
| `enum led_state {OFF, ON}` 변수 | unsigned int | unsigned int | 호환 정수 타입은 구현 정의 (6.7.2.2p4). 음수 값이 있는 enum 은 `int`. 열거 **상수** `LED_ON` 은 항상 `int` |

이 표가 Part B 설계를 결정한다.

1. **제어식: `(a)` vs `(a) + (b)` vs `(x) + 0`.** `max_of` 는 `(a) + (b)` — 매크로 `MAX` 와 같은 비교 의미(공통 타입)를 얻고 작은 정수를 표에서 뺄 수 있다. `abs_of`, `LOG_VAR` 는 `(x) + 0` — 승격과 decay 만 원한다. 반대로 `bswap_of` (추가 연습 2)는 `uint16_t` 를 지켜야 하므로 `+ 0` 을 **쓰면 안 된다**.
2. **같은 함정은 그대로 남는다.** `max_of(si, ua)` (`int -1`, `unsigned 3`)는 공통 타입이 `unsigned int` 라 `max_u32(4294967295, 3)` → `4294967295`. `_Generic` 은 타입을 *고를* 뿐 C-B02 의 변환 규칙을 고쳐주지 않는다. 막고 싶으면 섞인 타입을 에러로 만드는 `max_strict` (추가 연습 1).
3. **typedef 를 항목 타입으로 쓰지 말고 표준 타입 이름으로.** `int32_t:` / `int64_t:` 로 표를 짜면 맥에선 `long` 이 (`int64_t` = `long long`이므로) 표에 없어 에러, Linux 에선 `long long` 이 에러, gcc ARM(newlib, `int32_t` = `long` 으로 알려짐)에선 리터럴 `5`(`int`)가 에러가 된다. 그래서 `int / unsigned / long / unsigned long / long long / unsigned long long` 를 다 쓰고, 크기가 플랫폼마다 다른 `long` 만 `#if LONG_MAX == INT64_MAX` 로 함수를 고른다 — **이 `#if` 는 매크로가 여전히 필요한 곳** 이다.
4. **default 를 둘지 말지.** `type_name` 은 `default: "other"` (무엇이든 받아야 하니까). `max_of`/`abs_of` 는 default 없음 — 실수로 `long double` 이나 `unsigned` 를 넣으면 **컴파일 에러** 가 나는 편이 조용히 엉뚱한 함수로 가는 것보다 낫다. 맥 실제 에러:

```text
demo_generic.c:76:23: error: controlling expression type 'long double' not compatible with any generic association type
```

### Part C — 매크로가 꼭 필요한 곳만 매크로로

**C-1. LOG_VAR — 껍데기는 매크로, 일은 함수**

```c
#define LOG_VAR(x) _Generic((x) + 0,                                                     \
        int: log_i64, long: log_i64, long long: log_i64,                                 \
        unsigned int: log_u64, unsigned long: log_u64, unsigned long long: log_u64,      \
        float: log_dbl, double: log_dbl,                                                 \
        char *: log_str, const char *: log_str)(__FILE__, __LINE__, #x, (x))
```

| 재료 | 왜 함수로는 안 되나 |
|---|---|
| `#x` | 함수는 인자의 **값** 만 받는다. 소스 글자 `rx_cnt` 는 전처리기만 안다 |
| `__FILE__`, `__LINE__` | 쓰인 곳에서 펼쳐진다. 함수 안에 쓰면 **함수 정의 줄** 이 찍힌다 (`demo_still_macro.c`: `line_inline()` 은 두 번 불러도 28, `LINE_HERE()` 는 부른 줄 52) |
| 타입별 포맷 | `_Generic` — `uint32_t 0xFFFFFFFF` 가 `-1` 이 아니라 `4294967295` 로 |
| 한 번 평가 | 제어식은 평가 안 되고, `(x)` 는 호출 인자로 한 번 |

로그를 리셋하지 않고 모아 찍은 실제 출력 (solution.c 의 테스트 줄 번호 그대로):

```text
solution.c:276 rx_cnt=5
solution.c:277 temp=-40
solution.c:278 status=4294967295
solution.c:279 mode="IDLE"
solution.c:280 dev="uart1"
solution.c:281 gain=1.5
solution.c:283 uart_read_dr()=7
```

펌웨어 메모: `__FILE__` 은 빌드 경로 전체가 될 수 있어 플래시를 먹는다. clang 9+/GCC 12+ 의 `__FILE_NAME__`(파일 이름만), `-fmacro-prefix-map=OLD=NEW` 로 줄이거나, 아예 문자열 대신 파일 ID·줄 번호만 남기고 호스트에서 복원하는 방식(Zephyr dictionary logging, Trice 같은 것)을 쓴다. 실제 포맷 엔진(`%lld` 없이 UART 로 찍기)은 C-M10 참고.

**C-2. REG_FIELD_WRITE — `##` 는 매크로, RMW 는 inline**

```c
static inline void reg_field_write(volatile uint32_t *reg, uint32_t msk, uint32_t pos, uint32_t v) {
    *reg = (*reg & ~msk) | ((v << pos) & msk);     /* 읽기 1번, 쓰기 1번 */
}
#define REG_FIELD_WRITE(inst, P, R, F, v) reg_field_write(&(inst)->R, REG_MSK(P, R, F), REG_POS(P, R, F), (v))
```

C-B15 판은 `do { uint32_t reg_v_ = ...; } while (0)` 문장 매크로였다. 이제 매크로는 **이름 조립만** 하고 결과는 함수 호출 *식* 이다 → do-while(0) 없이도 `if (go) REG_FIELD_WRITE(...); else ...` 가 그냥 된다, 임시 변수 이름 포획도 없다, `v` 는 `uint32_t` 로 변환되어 한 번 평가된다(`REG_FIELD_WRITE(..., uart_read_dr())` 테스트), 디버거로 `reg_field_write` 에 들어갈 수 있다. 실제 확장:

```text
reg_field_write(&((&fake_uart1))->CR2, (3u << 12u), 12u, (2u));
```

CMSIS 의 `_VAL2FLD(field, value)` 도 같은 구조다 — `field ## _Pos`, `field ## _Msk` 만 매크로고 나머지는 평범한 식. (W1C 비트가 섞인 레지스터에서 이런 RMW 가 왜 위험한지는 C-M02.)

**C-3. IS_ARRAY / ARRAY_SIZE — 함수가 될 수 없는 대표 선수**

ARRAY_SIZE 가 함수가 될 수 없는 이유 두 가지: ① 배열을 함수 인자로 넘기는 순간 포인터로 decay 된다(C-B06) ② 결과가 배열 크기·`_Static_assert` 에 쓰이는 **정수 상수식** 이어야 한다 — 함수 호출은 상수식이 아니다(6.6p6).

```c
#define IS_ARRAY(a) _Generic(&(a), __typeof__(*(a)) (*)[sizeof(a) / (sizeof(*(a)))]: 1, default: 0)
```

| 인자 | `&(a)` 의 타입 | 만든 항목 타입 | 결과 |
|---|---|---|---|
| `int arr4[4]` | `int (*)[4]` | `int (*)[16/4]` = `int (*)[4]` | 1 |
| `int *p4` | `int **` | `int (*)[8/4]` = `int (*)[2]` | 0 |
| `int *const pc4` | `int *const *` | `int (*)[2]` | 0 |
| `volatile uint32_t regs3[3]` | `volatile uint32_t (*)[3]` | `__typeof__` 가 volatile 을 유지 → 같음 | 1 |
| `int m34[3][4]` | `int (*)[3][4]` | `*(a)` 가 `int[4]` → `int (*)[3][4]` | 1 |
| `"abc"` | `char (*)[4]` | `char (*)[4]` | 1 |

- `&(a)` 는 decay 를 막는다 (`&` 의 피연산자는 배열 → 포인터 변환 예외, 6.3.2.1p3). 그래서 제어식이 lvalue 변환을 거쳐도 "배열을 가리키는 포인터" 정보가 남는다.
- `_Generic` 의 결과가 정수 상수라 전체가 정수 상수식으로 쓰인다(clang/GCC). 그걸 `sizeof(struct { _Static_assert(...); char c_; })` 안에 넣으면 식 안에서 정적 단언을 할 수 있다 (C11 은 구조체 멤버 자리에 `_Static_assert` 를 허용, 6.7.2.1p1). `0 *` 를 곱해 값에는 영향이 없다.
- 오른쪽 `(sizeof(*(a)))` 의 군더더기 괄호는 clang `-Wsizeof-pointer-div` 를 끄려고 넣었다 (포인터일 때 "그게 포인터 크기다" 경고가 나는데, 여기선 일부러 그 계산을 하는 것).
- `__typeof__` 는 C11 에서 GNU 확장(C23 표준 `typeof`). `-std=c11 -pedantic -Werror` 에서도 solution.c 는 경고 없이 통과했다 (밑줄 철자 덕분).
- 한계: 크기가 포인터보다 큰 구조체를 가리키는 포인터면 `T (*)[0]` 이 되어 `-pedantic` 에서 "zero size arrays are an extension" 경고가 난다(어차피 거부해야 할 경우라 결과는 같다).

포인터를 넘긴 실제 결과 (`demo_still_macro.c -DARRAY_PTR`, 배열 매개변수 `const uint32_t tbl[5]` 는 사실 포인터):

```text
demo_still_macro.c:45:58: warning: sizeof on array function parameter will return size of 'const uint32_t *' (aka 'const unsigned int *') instead of 'const uint32_t[5]' (aka 'const unsigned int[5]') [-Wsizeof-array-argument]
demo_still_macro.c:45:58: warning: 'sizeof (tbl)' will return the size of the pointer, not the array itself [-Wsizeof-pointer-div]
demo_still_macro.c:45:58: error: static assertion failed: ARRAY_SIZE: argument is not an array
```

clang 은 순진한 ARRAY_SIZE 에도 **경고** 는 낸다. IS_ARRAY 는 그걸 경고 플래그·컴파일러와 무관한 **에러** 로 만든다. Linux 의 `ARRAY_SIZE(arr) = sizeof(arr)/sizeof((arr)[0]) + __must_be_array(arr)`, Zephyr 의 `IS_ARRAY` 가 같은 아이디어를 `__builtin_types_compatible_p` 로 구현한다.

**C-4. `static const` 는 상수식이 아니다 (C 에서는)**

`demo_still_macro.c -DTRY_CONST` 는 `#define RX_FIFO_DEPTH 16u` 를 `static const uint32_t RX_FIFO_DEPTH = 16u;` 로 바꾼다. 표준상 const 객체는 정수 상수식이 아니므로(6.6p6) 파일 범위 배열 크기·`_Static_assert`·`case` 셋 다 제약 위반이다. 그런데 이 맥의 clang 21 실제 결과는:

```text
$ cc -std=c11 -Wall -Wextra -DTRY_CONST -c demo_still_macro.c
demo_still_macro.c:17:16: warning: variable length array folded to constant array as an extension [-Wgnu-folding-constant]
$ cc -std=c11 -Wall -Wextra -pedantic -DTRY_CONST -c demo_still_macro.c
demo_still_macro.c:17:16: warning: variable length array folded to constant array as an extension [-Wgnu-folding-constant]
demo_still_macro.c:18:16: warning: expression is not an integer constant expression; folding it to a constant is a GNU extension [-Wgnu-folding-constant]
demo_still_macro.c:22:10: warning: expression is not an integer constant expression; folding it to a constant is a GNU extension [-Wgnu-folding-constant]
```

즉 clang 은 **확장으로 접어 주고** `-pedantic` 에서야 세 곳 모두 "상수식이 아니다" 라고 말한다. 다른 컴파일러(IAR, Keil, 구버전 GCC)에서는 에러가 될 수 있는 이식성 없는 코드다. 대안: `#define`, 또는 `int` 범위면 `enum { RX_FIFO_DEPTH = 16 };` (열거 상수는 진짜 상수식이고 디버거에도 보인다), C23 이면 `constexpr`. C++ 의 `const int` 는 상수식이라 여기서 C 와 갈린다.

### 정리: 언제 무엇을

| 하고 싶은 일 | 도구 | 이유 |
|---|---|---|
| 값 계산 (max, abs, clamp, 필드 RMW) | `static inline` 함수 | 타입 검사, 한 번 평가, `-O1` 이상에서 매크로와 같은 코드 |
| 여러 타입에 같은 이름 | `_Generic` 으로 inline 함수 선택 (매크로로 감쌈) | 표준 C11, 런타임 비용 0, 표에 없는 타입은 에러 |
| 소스 글자 · 이름 만들기 | 매크로 `#`, `##` | 전처리기만 토큰을 본다 |
| 호출 위치 (`__FILE__`, `__LINE__`, `__func__`) | 매크로 (얇게) → 함수에 넘기기 | 쓰인 곳에서 정해진다 |
| 정수 상수식 (배열 크기, `case`, `_Static_assert`, `#if`) | `#define` / `enum` / (C23 `constexpr`) | C 의 const 변수는 상수식 아님 |
| 플랫폼 분기 (`long` 크기, 칩 종류) | `#if` | 컴파일 전에 결정 |
| 큰 본문 + 많은 호출 지점 | 일반 함수 (필요하면 `noinline`) | 매크로는 복사 강제 (778 → 190 B) |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution                  # 56/56 passed
cc -std=c11 -Wall -Wextra -Werror -pedantic -O2 solution.c -o solution && ./solution        # __typeof__ 철자 덕분에 통과
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution && ./solution   # 56/56, 보고 없음
```

실험용 파일 (모두 이 폴더, 결과는 5. 풀이에 붙인 그대로):

| 파일 | 명령 | 보여주는 것 |
|---|---|---|
| `demo_codegen.c` | `cc -std=c11 -O0 -S demo_codegen.c -o -` (`-O2`), `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c11 -Os -S demo_codegen.c -o -` | -O0 의 `bl _max_i32`, -O2 의 동일 `cmp/csel`, 매크로의 DR 두 번 읽기 |
| `demo_size.c` | `-DIMPL={1,2,3} -c` 후 `size -m` / `xcrun llvm-size -A` | 매크로·inline·_Generic 의 `.text` 크기 |
| `demo_crc_size.c` | `--target=thumbv7em-none-eabi -Os [-DUSE_MACRO\|-DNOINLINE] -c` | 큰 헬퍼 22곳: 778 / 778 / 190 바이트 |
| `demo_generic.c` | `cc -std=c11 -Wall -Wextra demo_generic.c` (`-DCONST_ASSOC`, `-DMIXED`, `-DNO_DEFAULT`, `-DLONG_DOUBLE`) | 타입 이름 표, 한정자 경고, 섞인 타입·표 밖 타입 에러 |
| `demo_still_macro.c` | `cc -std=c11 -Wall -Wextra demo_still_macro.c` (`-DTRY_CONST`, `-DARRAY_PTR`) | static const 상수식 문제, `__LINE__` 캡처, `#`/`##`, ARRAY_SIZE 포인터 에러 |
| `demo_macro_ub.c` | `cc -std=c11 -g -O0 -fsanitize=undefined demo_macro_ub.c` | `ABS(INT32_MIN)`, `SQUARE(50000)` UB vs inline 함수 |

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `static inline int32_t abs_i32(int32_t x) { return x < 0 ? -x : x; }` | 함수로 바꿔도 `-INT32_MIN` 오버플로 UB 는 그대로 | 반환을 `uint32_t` 로, `0u - (uint32_t)x` |
| `inline int f(...)` 를 헤더에 (static 없이) | C99 inline 정의는 외부 정의가 아니다 → `-O0` 에서 링크 에러 (Q2) | 헤더엔 `static inline` |
| `_Generic(x, const int: ..., int: ...)` | 제어식은 한정자가 떨어진 타입 → `const int` 는 영원히 안 걸림 | 한정자 없는 타입만 나열 (가리키는 쪽 const 는 별개) |
| `_Generic(x, int32_t: f, int: g)` | 맥/ARM clang 에서 `int32_t` == `int` → 중복 항목 컴파일 에러 | 표준 타입 이름으로 표를 짠다 |
| `_Generic(c, uint8_t: ...)` 에 `char` 를 넘김 | `char` 는 `unsigned char` 와 다른 타입 — 플랫폼 char 부호와 무관 | `char:` 항목을 따로 두거나 `+ 0` 으로 승격 |
| `bswap_of(x)` 제어식을 `(x) + 0` 으로 | `uint16_t` 가 `int` 로 승격돼 `uint16_t` 항목을 못 찾음 | 크기 보존이 필요하면 `(x)` 그대로 |
| `max_of` 에 `default: max_i64` | `float`·`long double` 이 조용히 정수 함수로 → 값 잘림 | default 를 빼서 컴파일 에러로 |
| 중첩 `_Generic` 의 안쪽에 default 없음 | 선택 안 된 바깥 항목 안쪽도 검사됨 → 올바른 호출도 에러 | 안쪽마다 `default: 호출불가_이름` |
| `static const uint32_t N = 16; uint8_t buf[N];` (파일 범위) | C 에서 const 변수는 상수식 아님 — clang 은 확장으로 접지만 이식성 없음 | `#define N 16u` 또는 `enum { N = 16 };` |
| 로그 헬퍼를 함수로 만들고 안에서 `__LINE__` | 항상 헬퍼 정의 줄이 찍힘 | 얇은 매크로가 `__FILE__, __LINE__` 을 넘긴다 |
| `ARRAY_SIZE(param)` (배열 매개변수) | 매개변수는 포인터 → 2 같은 엉뚱한 값 | `IS_ARRAY` 정적 단언으로 에러화, 크기는 별도 인자로 |
| "inline 이니까 크기 걱정 없음" | 측정상 clang -Os 도 큰 헬퍼를 22곳 전부 인라인 (778 B) | 크기가 중요하면 일반 함수 / `noinline` 후 측정 |

## 8. 면접 Q&A

<details>
<summary>Q1. 매크로 대신 inline 함수를 쓰면 느려지지 않나요?</summary>

최적화를 켜면(-O1 이상, -Os 포함) 작은 `static inline` 은 인라인되어 **매크로와 같은 명령** 이 나온다 — 이 레슨에서 arm64 -O2 `cmp/csel/ret`, Cortex-M4 -Os `cmp/it le/movle/bx lr` 이 세 버전 모두 명령 하나까지 같았다. 차이는 `-O0` 디버그 빌드: `inline` 은 힌트라서(6.7.4p6) 실제 `bl` 호출이 생긴다. 디버그 빌드에서도 꼭 펼쳐야 하는 ISR 경로라면 `__attribute__((always_inline))`(GCC/clang)을 쓴다. 꼬리질문 "그럼 매크로가 더 빠른 경우는?" → 매크로가 인자를 두 번 평가해 **다른 프로그램** 이 되는 경우(volatile DR 두 번 읽기) 정도이고, 그건 빠른 게 아니라 틀린 것이다.

</details>

<details>
<summary>Q2. 헤더에 왜 `inline` 이 아니라 `static inline` 을 쓰나요?</summary>

C99/C11 의 `inline`(static·extern 없음) 정의는 **inline 정의** 일 뿐 외부 정의를 만들지 않는다(6.7.4p7). 컴파일러가 인라인하지 않기로 하면 외부 심볼을 찾는데, 어느 .c 에도 `extern inline int inc(int);` 같은 외부 정의가 없으면 링크 에러다. 이 맥에서 실제로:

```text
$ cc -std=c11 -O0 inl.c      # inline int inc(int x) { return x + 1; }  만 있는 파일
Undefined symbols for architecture arm64:
  "_inc", referenced from:
      _main in inl-76eebc.o
$ cc -std=c11 -O2 inl.c      # 인라인되니 링크 성공
```

`static inline` 은 각 번역 단위에 내부 연결 사본을 허락하므로 이 문제가 없고, 안 쓰면 아예 코드가 안 나온다. (GCC 의 옛 `-std=gnu89` 에서는 `extern inline` 의 의미가 C99 와 반대다 — 오래된 커널 코드를 읽을 때 주의.) 참고로 clang 은 **.c 파일 안의** 안 쓰는 `static inline` 에 `-Wunused-function` 을 내지만 헤더 안의 것은 조용하다 (`demo_size.c` 에서 실제로 걸렸다).

</details>

<details>
<summary>Q3. _Generic 은 런타임에 타입을 검사하나요? 제어식에 i++ 를 넣으면?</summary>

아니다. 선택은 컴파일 중 의미 분석 단계에서 끝나고 결과 식 하나만 남는다 — `-O0` 에서도 `f_generic` 은 `bl _max_i32` 로 직행했다. 제어식은 **평가되지 않는다**(6.5.1.1p3)는 규칙 때문에 `max_of(i++, j)` 의 `(i++) + (j)` 는 타입 계산에만 쓰이고, 실제 증가는 호출 인자에서 한 번 일어난다. 선택되지 않은 연관 식도 평가되지 않는다. 다만 "평가 안 됨" 이지 "검사 안 됨" 은 아니어서, 선택 안 된 항목도 문법·의미상 유효해야 한다 (C++ 템플릿의 SFINAE 같은 것은 없다).

</details>

<details>
<summary>Q4. `_Generic(ci, const int: ...)` 가 안 걸리는 이유, 그리고 char 와 'a' 는?</summary>

제어식은 lvalue 변환을 거친 타입으로 비교된다 — 최상위 `const/volatile/_Atomic` 이 떨어지고 배열·함수는 포인터로 바뀐다 (C17 에서 DR 481 로 명문화). 그래서 `const int` 항목은 죽은 코드이고 clang 은 `-Wunreachable-code-generic-assoc` 경고를 낸다. `const char *` 의 const 는 가리키는 대상의 한정자라 남는다. `char`/`signed char`/`unsigned char` 는 서로 다른 세 타입이라 각각 항목이 필요하고(ARM 처럼 `char` 가 unsigned 인 플랫폼에서도 `char` 는 `unsigned char` 가 아니다), `'a'` 는 C 에서 `int` 라 `char:` 항목에 안 걸린다. 문자열 리터럴은 `char[N]` → `char *` (C 에서는 const 가 아니다; 쓰면 UB 일 뿐).

</details>

<details>
<summary>Q5. 연관 타입에 int32_t, int64_t 같은 typedef 를 써도 되나요?</summary>

typedef 는 새 타입이 아니라 별명이다. 그래서 ① `int: f, int32_t: g` 는 같은 타입 두 번 → 컴파일 에러(6.5.1.1p2) ② `int64_t` 의 정체가 macOS `long long`, x86-64 Linux `long` 이라 표에 빠진 쪽이 플랫폼마다 다르다 ③ arm-none-eabi-gcc(newlib)에서는 `int32_t` 가 `long` 으로 알려져 있어(이 맥에는 gcc 가 없어 직접 확인은 못 함), `int32_t:` 로만 짠 표에 리터럴 `5`(`int`)를 넣으면 에러가 난다. 그래서 표는 **표준 타입 이름**(`int, long, long long` …)으로 다 덮고, 크기가 다른 `long` 은 `#if LONG_MAX == INT64_MAX` 로 함수를 정한다. 반대로 `bswap_of` 처럼 "uint16_t/32/64 셋 중 하나" 가 의미인 경우는 고정폭 typedef 로 짜도 되지만, `0x...ull` 리터럴(`unsigned long long`)이 Linux 의 `uint64_t`(`unsigned long`)와 안 맞는 것은 감수해야 한다.

</details>

<details>
<summary>Q6. 그래도 매크로가 꼭 필요한 경우를 정리해 보세요.</summary>

① `#` 문자열화 — 변수 이름 로그, `CHECK(expr)` 의 식 출력, 버전 문자열 `"fw v" XSTR(FW_MAJOR)` ② `##` 토큰 붙이기 — `UART_CR2_STOP_Msk` 같은 이름 조립, X-macro ③ 호출 위치 캡처 — `__FILE__`, `__LINE__`, `__func__` 는 쓰인 곳에서 정해진다 ④ 정수 상수식 — 배열 크기, `case` 라벨, `_Static_assert`, `#if` (C 의 const 변수는 상수식이 아님; `enum` 이 대안) ⑤ `_Generic` 을 재사용 가능한 이름으로 묶기 — `_Generic` 은 식이라 매크로로 감싸야 `max_of(...)` 처럼 쓸 수 있다 ⑥ `_Generic` 없이 타입 제네릭 — `__typeof__` + statement expression(GNU) ⑦ 조건부 컴파일, 호출자 문맥 바꾸기(`return`, 임계 구역 감싸기). 원칙: **매크로는 이것들만 하는 얇은 껍데기, 나머지 로직은 inline 함수로** (LOG_VAR, REG_FIELD_WRITE).

</details>

<details>
<summary>Q7. C++ 이었다면 어떻게 다른가요?</summary>

템플릿(`std::max<T>`)과 오버로딩이 `_Generic` 을, `constexpr` 이 상수용 매크로를, `const int` 가 그대로 상수식이 되어 배열 크기를, C++20 `std::source_location::current()` 를 기본 인자로 두면 **함수가 호출 위치를 받을 수 있어** `__LINE__` 매크로까지 대체한다. `std::size(arr)` 는 배열 참조 템플릿이라 포인터를 넣으면 컴파일 에러 — IS_ARRAY 트릭이 필요 없다. 남는 건 `#`/`##` 정도. C 에는 이 중 C23 `constexpr`(객체만, 함수는 아님)과 `typeof` 만 들어왔다.

</details>

<details>
<summary>Q8. MISRA / 코드 리뷰 관점에서는?</summary>

MISRA C:2012 Dir 4.9 (advisory): 함수와 바꿔 쓸 수 있는 함수형 매크로는 함수로. Rule 1.2: 언어 확장 금지(advisory) — `__typeof__`, statement expression, `noinline` 을 쓰면 deviation 기록 대상. `_Generic` 은 C11 지원이 추가된 개정판(Amendment 3 등)에서 별도 규칙을 받았는데, 대표적으로 "generic selection 은 매크로 확장으로만 쓸 것" 같은 권고가 있다. 리뷰 체크리스트로는: 인자를 두 번 쓰는 매크로가 없나, `_Generic` 표에 `default` 가 조용히 잘못된 함수로 보내지 않나, 표에 typedef 가 섞여 플랫폼마다 깨지지 않나, 헤더의 `inline` 에 `static` 이 붙었나.

</details>

## 9. 추가 연습

### 연습 1 — 섞인 타입을 거부하는 max_strict

`max_of(-1, 1u)` 가 `4294967295` 를 돌려주는 건 매크로와 같은 함정이다. 두 인자의 (승격 후) 타입이 같을 때만 컴파일되는 `max_strict(a, b)` 를 `_Generic` 두 겹으로 만들어라.

<details>
<summary>답 보기</summary>

바깥 `_Generic((a) + 0, ...)` 이 a 의 타입으로, 안쪽 `_Generic((b) + 0, ...)` 이 b 의 타입으로 고른다. **함정**: 처음에 안쪽에 default 없이 짜면 올바른 `max_strict(si, sj)` 도 에러가 난다 — 선택되지 않은 `unsigned int:` 항목의 안쪽 `_Generic((b) + 0, unsigned int: max_u32)` 도 유효한 식이어야 하기 때문이다. 실제 메시지(`demo_generic.c -DNO_DEFAULT`):

```text
demo_generic.c:67:42: error: controlling expression type 'int' not compatible with any generic association type
```

그래서 안쪽마다 `default: max_strict_TYPE_MISMATCH` (선언만 있는 인자 0개 함수)를 둔다. 선택되지 않으면 아무 일도 없고(코드도 참조도 안 생겨 `-O0` 링크도 OK), 정말 섞여서 골라지면 `(a, b)` 두 인자 호출이라 `error: too many arguments to function call, expected 0, have 2` 가 난다 (`-DMIXED`). `+ 0` 덕분에 `uint8_t` 와 `int` 는 같은 짝으로 허용된다 — 둘 다 `int` 로 표현 가능하니 안전하다.

</details>

### 연습 2 — bswap_of: 여기서는 `+ 0` 을 쓰면 안 된다

`uint16_t/uint32_t/uint64_t` 에 맞는 바이트 스왑(C-B13)을 고르고 결과 타입도 유지하는 `bswap_of(x)` 를 써라.

<details>
<summary>답 보기</summary>

`#define bswap_of(x) _Generic((x), uint16_t: bswap16, uint32_t: bswap32, uint64_t: bswap64)(x)`. 제어식에 `+ 0` 을 붙이면 `uint16_t` 가 `int` 로 승격되어 `uint16_t` 항목을 못 찾고 컴파일 에러가 난다 — `max_of` 와 정반대 설계. 여기선 "몇 바이트짜리인가" 가 의미라 고정폭 typedef 를 항목으로 쓰는 게 맞다. 단 `bswap_of(0x1122334455667788ull)` 은 macOS(`uint64_t` = `unsigned long long`)에선 되지만 x86-64 Linux(`unsigned long`)에선 에러 → `(uint64_t)` 캐스트를 붙여 부른다. 프로토콜 직렬화 자체는 C-M05 처럼 바이트 단위로 하는 편이 엔디안 독립적이다.

</details>

### 연습 3 — GNU 확장 없는 SWAP_ANY

C-B15 의 `SWAP` 은 `__typeof__` 가 필요했다. 표준 C11 만으로, 크기가 다른 두 변수를 넣으면 컴파일 에러가 나는 `SWAP_ANY(a, b)` 를 써라.

<details>
<summary>답 보기</summary>

일은 `static inline void swap_bytes(void *a, void *b, size_t n)` (바이트 단위 교환)이 하고, 매크로는 `&(a), &(b), sizeof(a)` 를 넘기면서 `sizeof(struct { _Static_assert(sizeof(a) == sizeof(b), "..."); char c_; })` 에 0 을 곱해 더한다 — IS_ARRAY 와 같은 "식 안의 정적 단언" 트릭. `a`, `b` 는 `&` 로 한 번씩만 평가된다. 한계: 크기만 보므로 `int` 와 `float` 처럼 크기가 같은 다른 타입이면 비트 패턴을 그대로 바꿔 넣는다. 구조체 교환(`struct cfg`)도 된다.

</details>

### 연습 4 — clamp_of: 인자 셋, 각각 한 번

`clamp_of(x, lo, hi)` 를 `int`/`unsigned`/`float` 에 대해 만들고 `clamp_of(uart_read_dr(), 2u, 5u)` 가 FIFO 에서 1바이트만 꺼내는지 확인하라.

<details>
<summary>답 보기</summary>

제어식은 `(x) + (lo) + (hi)` — 셋의 공통 타입. 매크로 `CLAMP(x, lo, hi) MIN(MAX(x, lo), hi)` 는 `x` 를 최대 2번, `lo`/`hi` 도 여러 번 평가하지만, 함수 호출은 셋 다 한 번이다. 7 → 5, `rx_pos == 1`. 펌웨어에서 PWM 듀티·ADC 값 포화에 매일 쓰는 함수다. `lo > hi` 같은 잘못된 범위는 `assert` 로 잡는 게 보통이다.

</details>

### 연습 5 — TRACE(): __func__ 와 __LINE__

호출한 함수 이름과 줄을 링 버퍼에 남기는 `TRACE()` 를 쓰고, 같은 걸 inline 함수 안에서 `__func__` 로 하면 무엇이 찍히는지 확인하라.

<details>
<summary>답 보기</summary>

`#define TRACE() trace_at(__func__, __LINE__)`. `__func__` 는 매크로가 아니라 각 함수 안에 암묵적으로 선언된 `static const char __func__[]` 이다(6.4.2.2) — 그래도 "쓰인 함수" 기준이라는 점은 `__LINE__` 과 같아서, 헬퍼 함수 `who_am_i()` 안에서 쓰면 `"who_am_i"` 가 나온다. 매크로가 호출자 쪽에서 `__func__` 를 집어 함수에 넘겨야 `"motor_start"` 가 기록된다. 실제 제품에서는 문자열 대신 PC(`__builtin_return_address(0)`)를 저장해 플래시를 아끼기도 한다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 19/19 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o practice && ./practice   # 19/19, 보고 없음
```

## 10. 핵심 정리

- 값 계산은 `static inline` 함수로: 타입 검사, **인자 한 번 평가**, 반환 타입 선택(`uabs_i32 → uint32_t`, `square_i32 → int64_t`) — 매크로의 다중 평가·숨은 오버플로 UB 가 사라진다.
- `-O1` 이상에서 inline 은 매크로와 같은 코드(측정: arm64 `cmp/csel`, M4 `cmp/it/movle` 동일). `-O0` 에서는 `bl` 호출. `inline` 은 힌트, 헤더엔 `static inline`.
- `_Generic` 은 컴파일 타임 선택이라 비용 0. 제어식은 **평가 안 됨**, 타입은 **lvalue 변환 후**(const/volatile 제거, 배열 decay). `char`·`signed char`·`unsigned char` 는 서로 다르고 `'a'` 는 `int`.
- 제어식 설계: `(a)+(b)` = 공통 타입(매크로와 같은 비교 의미, -1 vs 1u 함정도 같음), `(x)+0` = 승격만, `(x)` = 크기 보존. default 를 빼면 표 밖 타입이 컴파일 에러.
- 연관 타입은 표준 타입 이름으로 — `int32_t`/`int64_t` 의 정체는 플랫폼마다 다르다 (`long` 크기는 `#if` 로).
- 매크로가 꼭 필요한 곳: `#`, `##`, `__FILE__`/`__LINE__`/`__func__`, 정수 상수식(배열 크기·`case`·`_Static_assert`·`#if`), `_Generic` 묶기. 매크로는 얇은 껍데기, 로직은 inline.
- `ARRAY_SIZE` 는 함수가 될 수 없다 → `IS_ARRAY` 정적 단언으로 포인터를 컴파일 에러로.
- 코드 크기는 측정하라: 작은 헬퍼는 세 방식 동일, 큰 헬퍼는 매크로·inline 모두 복사(778 B) — `noinline` 일반 함수로 190 B.

## 참고자료

- C11 표준 (N1570) — 6.5.1.1 Generic selection, 6.7.4 Function specifiers (`inline`), 6.6 Constant expressions, 6.3.2.1 lvalue 변환·배열 decay, 6.4.4.4 문자 상수, 6.4.5 문자열 리터럴, 6.7.2.1 구조체 안 `_Static_assert`, 6.7.2.2 enum — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- WG14 DR 481 — `_Generic` 제어식의 lvalue 변환: <https://www.open-std.org/jtc1/sc22/wg14/www/docs/summary.htm>
- C23 (N3220 초안) — `typeof`/`typeof_unqual`, `constexpr`, `bool`/`true` 키워드: <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3220.pdf>
- GCC Manual — [An Inline Function is As Fast As a Macro](https://gcc.gnu.org/onlinedocs/gcc/Inline.html), [Common Function Attributes (`always_inline`, `noinline`)](https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html), [Typeof](https://gcc.gnu.org/onlinedocs/gcc/Typeof.html)
- Clang — [Diagnostic flags (`-Wunreachable-code-generic-assoc`, `-Wsizeof-pointer-div`, `-Wgnu-folding-constant`)](https://clang.llvm.org/docs/DiagnosticsReference.html), [UndefinedBehaviorSanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html)
- Linux `include/linux/array_size.h`·`compiler.h` (`ARRAY_SIZE`, `__must_be_array`), Zephyr `include/zephyr/sys/util.h` (`IS_ARRAY`, `ARRAY_SIZE`)
- ARM CMSIS-Core — `_VAL2FLD` / `_FLD2VAL`: <https://arm-software.github.io/CMSIS_6/latest/Core/index.html>
- MISRA C:2012 Dir 4.9 (함수형 매크로 대신 함수), Rule 1.2 (언어 확장), 그리고 C11 기능을 다루는 개정판(Amendment 3)
