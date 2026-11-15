---
id: CPP-M02
time: 80분
updated: 2026-11-08
---
# CPP-M02 비트필드 대체: Field<Reg, Offset, Width> RMW

## 학습 목표

- `Field<Reg, Offset, Width>` 의 `read` / `write` / `modify` 를 구현하고, 경계(폭 32, 최상위 비트, 8/16비트 레지스터의 정수 승격)에서 UB 없이 동작하게 만든다.
- 값 범위 검사를 **상수는 컴파일 타임**(consteval · NTTP+`static_assert`), **변수는 명시한 런타임 정책**(Wrap / Saturate / Reject)으로 나눠 설계한다.
- `write(F1 = a, F2 = b)` 를 만들어 **읽기 1번 + 쓰기 1번**을 mock 로그로 증명하고, 다른 레지스터의 필드나 겹치는 필드는 **컴파일이 안 되게** 한다.
- RMW 의 위험(W1C 플래그, 예약 비트, ISR 끼어들기)을 mock 하드웨어로 재현하고, 어떤 건 고칠 수 있고 어떤 건 못 고치는지 구분한다.
- C 비트필드의 배치가 왜 구현 정의인지([class.bit]) 여러 타깃으로 직접 확인하고, thumbv7em 어셈블리로 Field 가 손으로 쓴 C 와 같은 명령인지 비교한다.

## 1. 문제

레지스터의 일부 비트만 바꾸는 RMW(Read-Modify-Write)는 펌웨어에서 가장 많이 쓰는 동작이면서, 면접에서 "`SR |= EN` 이 왜 버그일 수 있나요?" 로 W1C 를 묻는 단골 주제다.

C 에서는 보통 두 가지로 쓴다.

```c
/* 방법 1: 마스크 매크로 — 맞게 쓰면 이식성 있음. 그런데 타입 검사가 없다 */
#define PRIO_Pos 2
#define PRIO_Msk (3u << PRIO_Pos)
CSR = (CSR & ~PRIO_Msk) | (prio << PRIO_Pos);   /* prio=9 면? 옆 비트가 깨진다 */

/* 방법 2: 비트필드 — 읽기 좋다. 그런데 배치가 구현 정의이고, 대입마다 RMW 가 따로 나간다 */
struct { volatile uint32_t en:1, dir:1, prio:2; } *csr;
csr->en = 1; csr->prio = 2;                     /* 읽기 2번, 쓰기 2번 */
```

방법 1 은 값이 비트 범위를 넘어도, 다른 레지스터의 마스크를 섞어도 아무 말이 없다. 방법 2 는 비트 순서가 ABI 에 달려 있고, **W1C 비트가 같은 레지스터에 있으면 컴파일러가 만든 RMW 가 그 비트를 그대로 되써서 지워 버린다**. 이 레슨은 C++ 템플릿으로 둘의 장점만 취한다: 마스크 매크로의 이식성 + 비트필드의 이름 있는 접근 + 컴파일 타임 검사, 그리고 런타임 비용 0 (뒤에서 실측).

레지스터 주소를 타입으로 다루는 `Reg<Addr, T, Access>` 와 버스 추상화는 CPP-M01 이 담당한다. 여기서는 그것을 짧게 줄여 쓰고, **한 레지스터 안의 필드**에 집중한다. enum 필드는 CPP-B03, `volatile` 은 CPP-B05, `requires` 식은 CPP-B12 참고.

```svg
<svg viewBox="0 0 740 262" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">가짜 DMA 채널의 CSR 레지스터 — 비트마다 RMW 규칙이 다르다</text>
  <g stroke="currentColor">
    <rect x="10" y="34" width="210" height="44" class="f-mut"/>
    <rect x="220" y="34" width="60" height="44" class="f-warn"/>
    <rect x="280" y="34" width="60" height="44" class="f-warn"/>
    <rect x="340" y="34" width="60" height="44" class="f-warn"/>
    <rect x="400" y="34" width="110" height="44" class="f-mut"/>
    <rect x="510" y="34" width="100" height="44" class="f-ok"/>
    <rect x="610" y="34" width="60" height="44" class="f-ok"/>
    <rect x="670" y="34" width="60" height="44" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="60" y="60">31:11 예약 (res0)</text>
    <text x="238" y="60">TE</text><text x="298" y="60">HT</text><text x="358" y="60">TC</text>
    <text x="415" y="60">7:4 예약</text>
    <text x="520" y="60">3:2 PRIO</text>
    <text x="625" y="60">DIR</text><text x="688" y="60">EN</text>
    <text x="224" y="95">bit10</text><text x="284" y="95">bit9</text><text x="344" y="95">bit8</text>
    <text x="528" y="95">RW</text><text x="625" y="95">RW</text><text x="688" y="95">RW</text>
  </g>
  <text x="10" y="126" fill="currentColor" font-weight="bold">write(EN = true, PRIO = 2) 가 내보내는 값</text>
  <g stroke="currentColor">
    <rect x="10" y="138" width="210" height="34" class="f-ok"/>
    <rect x="220" y="138" width="180" height="34" class="f-ok"/>
    <rect x="400" y="138" width="110" height="34" class="f-ok"/>
    <rect x="510" y="138" width="100" height="34" class="f-acc"/>
    <rect x="610" y="138" width="60" height="34" class="f-mut"/>
    <rect x="670" y="138" width="60" height="34" class="f-acc"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="85" y="160">항상 0</text><text x="265" y="160">항상 0 (= 안 지움)</text><text x="430" y="160">항상 0</text>
    <text x="528" y="160">새 값 2</text><text x="620" y="160">유지</text><text x="682" y="160">새 값</text>
  </g>
  <text x="10" y="202" fill="currentColor" font-weight="bold">흔한 C: CSR |= EN — 읽은 값을 그대로 되쓴다</text>
  <g stroke="currentColor">
    <rect x="10" y="214" width="210" height="34" class="f-warn"/>
    <rect x="220" y="214" width="180" height="34" class="f-warn"/>
    <rect x="400" y="214" width="110" height="34" class="f-warn"/>
    <rect x="510" y="214" width="220" height="34" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="30" y="236">읽힌 junk 를 다시 씀</text><text x="225" y="236">pending 이면 1 → 지워짐!</text><text x="415" y="236">junk</text>
    <text x="590" y="236">유지 / EN=1</text>
  </g>
</svg>
```

호스트에서는 진짜 주소를 읽을 수 없으므로, 이 레슨의 `mock` 은 레지스터 배열과 **접근 로그**를 가진다. W1C(1 쓰면 지움)와 예약 비트(읽으면 junk, 1 쓰면 위반 카운트)도 mock 이 흉내 낸다. 테스트는 "결과 값"뿐 아니라 "읽기 몇 번, 쓰기 몇 번, 무슨 값을 썼나"까지 검사한다.

### Part A — Field 하나: 기하, 값 범위, read/write/modify

- `low_mask<W>(width)`: 하위 `width` 비트 마스크. `width` 가 W 의 전체 폭이면 `(W{1} << width)` 는 UB ([expr.shift]) 이므로 따로 처리한다.
- `field_fits_reg<R>(offset, width)`: 폭 0 금지, 레지스터 밖 금지(`Offset + Width` 덧셈 넘침 주의), 예약 비트와 겹침 금지, 일부만 W1C 인 필드 금지.
- 상수는 컴파일 타임에: `THRESH = 256` (consteval 생성자), `THRESH.of<256>()` / `THRESH.write<256>()` (NTTP + `static_assert`).
- 변수는 정책을 고르게 한다: `THRESH.with<Reject>(x)` / `Saturate` / `Wrap`. **`THRESH = x` 는 컴파일 에러**여야 한다.
- 범위 비교는 호출자의 원래 타입으로: `-1`, `0x1'0000'0005` 를 32비트로 먼저 줄이면 범위 안처럼 보인다.
- `modify(fn)`: 읽기 1번 → 옛 값에 `fn` 적용 → 쓰기 1번. 정책이 거부하면 쓰지 않는다.

### Part B — 여러 필드, RMW 한 번

- `write(F1 = a, F2 = b, ...)`: 읽기 1번 + 쓰기 1번. 필드 순서는 결과에 영향 없음.
- 다른 레지스터의 필드, 같은 필드 중복, 비트가 겹치는 필드는 **컴파일 금지** (`requires` 로 하네스에서 증명).
- 런타임 값 하나라도 `Reject` 되면 **버스 접근 0번** 으로 `false`.

### Part C — W1C · 예약 비트가 있는 DMA 드라이버

- 일반 RMW 가 W1C 비트와 예약 비트에 항상 0 을 쓰도록 한다.
- `ack(TC, HT)`: 고른 플래그만 지운다. 레지스터 전체가 W1C+예약이면 **읽지 않고** 쓰기 1번.
- W1C 필드에 `=` 금지, RW 필드에 `ack` 금지 (컴파일 에러).
- `naive_enable()` (흔한 `|=`) 이 pending 플래그를 지우는 것을 mock 으로 재현하고, 읽기~쓰기 사이에 ISR 이 RW 비트를 바꾸면 **이 RMW 로도 변경이 사라진다**는 것을 테스트로 고정한다.

## 2. Problem (English)

> **Type-safe register fields with a single read-modify-write.**
>
> Given a register type `R` with `word`, `read()`, `write()`, implement `template <typename R, unsigned Offset, unsigned Width, typename V = typename R::word> struct Field`:
>
> 1. Geometry: `max`, `mask`, correct at `Width == 32` and `Offset == 31`, and correct on 8- and 16-bit registers. Invalid geometry (zero width, out of register, overlapping reserved bits, partly W1C) must not compile.
> 2. Values: `F = constant` is checked at compile time; `F = variable` must not compile. `F.with<Policy>(x)` accepts a runtime value of any integer type, with policies `Wrap`, `Saturate`, `Reject`. Range checks must use the caller's type (no narrowing first).
> 3. `F.read()`, `F.write(constant)`, `F.write<constant>()`, `F.modify<Policy>(fn)`.
> 4. `write(F1 = a, F2 = b, ...)` performs **exactly one read and one write**. It must not compile if the fields belong to different registers or their masks overlap. If any value was rejected, it performs no bus access and returns `false`.
> 5. A plain RMW must always write 0 to W1C and reserved bits. `ack(FlagA, FlagB)` clears only those flags, and skips the read when the register has no bits worth preserving.
>
> Example: with `CFG == 0xABCD0000`, `write(BURST = 5, XW = XWidth::B16, THRESH = 0x3C)` leaves `CFG == 0xABCD3C15` after one read and one write. `write(EN = true, BURST = 1)` does not compile because `EN` and `BURST` live in different registers.

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 깨짐 대비
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
# 처음엔 "37/134 passed" — TODO 를 채워 134/134 를 만든다
```

## 4. 힌트

<details>
<summary>힌트 1 — low_mask 의 함정</summary>

`(W{1} << width) - 1` 은 `width == 32` 에서 UB 다. 전체 폭이면 `~W{0}` 을 돌려준다. `uint8_t` 에서는 `~W{0}` 이 `int` 로 승격되어 `-1` 이 되니 `static_cast<W>` 로 되돌린다.

</details>

<details>
<summary>힌트 2 — 범위 비교는 std::cmp_* 로</summary>

`if (v > max)` 에서 `v` 가 음수 `int` 이고 `max` 가 `unsigned` 면 `v` 가 큰 양수로 변환된다. C++20 `<utility>` 의 `std::cmp_less`, `std::cmp_greater` 는 부호를 고려해 수학적으로 비교한다. 단, `bool` 과 `char` 계열은 받지 않는다 — 그래서 `PlainInt` concept 이 있다.

</details>

<details>
<summary>힌트 3 — consteval 생성자로 "상수만" 받기</summary>

`consteval Lit(U u)` 의 본문에서 범위를 검사하고, 틀리면 constexpr 이 아닌 함수를 호출한다. consteval 은 호출마다 상수 평가되므로 그 경로를 타는 순간 컴파일 에러다. 인자가 변수면 애초에 상수 표현식이 아니라서 그것도 컴파일 에러다. `throw` 는 `-fno-exceptions` 에서 쓸 수 없다 (실측: `cannot use 'throw' with exceptions disabled`).

</details>

<details>
<summary>힌트 4 — 파라미터 팩의 fold</summary>

`Rest` 가 비어 있을 수 있으니 이항 fold 로 초기값을 준다: `(W{0} | ... | Rest::field::mask)`. 단항 fold `(... | x)` 는 팩이 비면 에러다. 런타임 값은 `(first.ok && ... && rest.ok)`.

</details>

<details>
<summary>힌트 5 — keep 마스크에서 빼야 할 것</summary>

유지할 비트는 "바꿀 필드"만 빼면 안 된다: `~(mask | R::w1c | R::res0)`. 그래야 읽은 W1C 값이 되쓰이지 않는다.

</details>

## 5. 풀이

### Part A — 기하: 왜 함수로 뺐나

`std::uint32_t` 에서 `1u << 32` 는 UB 다 ([expr.shift]). 기계어 수준에서는 x86 `shl` 이 카운트를 5비트로 잘라 1 을, ARM `lsl`(레지스터 시프트) 은 0 을 내는 식으로 칩마다 다르다. constexpr 문맥에서는 더 좋다 — **UB 가 있으면 컴파일이 멈춘다**. `low_mask` 에서 전체 폭 처리를 일부러 빼 보면 실제로:

```text
mut.cpp:187:15: error: substitution into constraint expression resulted in a non-constant expression
```

8/16비트 레지스터의 정수 승격도 같은 곳에 숨어 있다. `W{1} << width` 는 `int` 로 승격되어 계산되고, `~W{0}` 도 `int` 다 ([conv.prom]). 이 값을 마스크 연산에 그대로 쓰면 상위 비트가 1 로 번지므로 **W 로 되돌리는 캐스트가 필수**다. 그래서 코드 전체에 `static_cast<W>` 가 많다.

`field_fits_reg` 에서 덧셈 대신 뺄셈을 쓰는 이유: `offset + width` 는 `unsigned` 끼리 더하다 wrap 할 수 있다. `ValidField<CFG, 0xFFFF'FFFFu, 2>` 는 더하면 1 이 되어 통과해 버린다.

```cpp
if (width == 0 || offset >= bits || width > bits - offset) return false;   // offset < bits 가 확인된 뒤라 뺄셈 안전
```

### Part A — 값 범위: 세 갈래 길

| 표현 | 받는 값 | 실패 시점 | 특징 |
|---|---|---|---|
| `THRESH = 0x3C` | 상수만 | 컴파일 (consteval `Lit`) | 변수면 에러. 메시지는 함수 이름 |
| `THRESH.of<0x3C>()` | 상수만 | 컴파일 (`static_assert`) | C++17 부터. 메시지 직접 작성. `requires` 로 못 잡음 |
| `THRESH.with<Reject>(x)` | 변수 | 런타임 (`ok=false`) | `write()` 가 버스 접근 0번으로 `false` |

세 경로 모두 `FieldValue<F>{bits, ok}` 를 만들 뿐, 레지스터는 아직 건드리지 않는다.

**consteval 으로 변수를 막는다.** `THRESH = 3` 의 `=` 는 `Field::operator=(Lit<Field>) const` 다. 인자 `3` 은 `Lit` 의 consteval 변환 생성자를 거치는데, 이 호출은 매번 상수 평가되어야 하므로 ([dcl.constexpr], [expr.const]의 immediate invocation) 변수를 넘기면 에러다. 필드 객체가 `inline constexpr` (const) 이므로 `operator=` 도 `const` 멤버여야 한다. 암시적 복사 대입은 const 객체에 쓸 수 없어 `THRESH = THRESH` 도 자동으로 금지된다.

**실제 에러 (`demo_errors.cpp`)**

```text
$ c++ -std=c++20 -Wall -Wextra -Werror -fsyntax-only -DSHOW_ERROR=3 demo_errors.cpp   # THRESH = 256
demo_errors.cpp:14:30: error: call to consteval function 'hw::Lit<hw::Field<hw::Reg<1073872900>, 8, 8>>::Lit<int>' is not a constant expression
./solution.cpp:182:28: note: non-constexpr function 'value_does_not_fit_in_field' cannot be used in a constant expression
  182 |         if (!Fld::fits(u)) value_does_not_fit_in_field();
demo_errors.cpp:14:30: note: in call to 'Lit<int>(256)'

$ ... -DSHOW_ERROR=4   # THRESH.of<256>()
./solution.cpp:241:23: error: static assertion failed due to requirement 'fits(256)': Field::of<v>: value out of range for this field's width
demo_errors.cpp:16:28: note: in instantiation of function template specialization 'hw::Field<hw::Reg<1073872900>, 8, 8>::of<256>' requested here

$ ... -DSHOW_ERROR=5   # void e5(unsigned x) { hw::write(THRESH = x); }
demo_errors.cpp:18:42: error: call to consteval function 'hw::Lit<hw::Field<hw::Reg<1073872900>, 8, 8>>::Lit<unsigned int>' is not a constant expression
demo_errors.cpp:18:42: note: function parameter 'x' with unknown value cannot be used in a constant expression

$ ... -DSHOW_ERROR=7   # THRESH = 3;  (값을 버림)
demo_errors.cpp:22:20: error: ignoring return value of function declared with 'nodiscard' attribute [-Werror,-Wunused-result]
```

함수 이름 `value_does_not_fit_in_field` 가 곧 에러 메시지다. C++20 에는 consteval 안에서 사용자 문자열로 실패하는 표준 방법이 없어서 이렇게 한다 (GCC 15 도 같은 형태의 메시지를 낸다: `call to non-'constexpr' function 'void hw::value_does_not_fit_in_field()'`).

**함정: requires 로는 consteval 실패를 못 잡는다.** 처음에 나는 하네스에 `requires { f = 256; }` 을 넣으려 했다. 이건 **항상 true** 다. requires 식 안의 식은 평가되지 않는 피연산자(unevaluated operand, [expr.prim.req])인데, immediate invocation 은 "potentially-evaluated" 인 호출에만 해당하므로 ([expr.const]) 상수 평가가 일어나지 않는다. 타입만 맞으면 통과다. 그래서 하네스는 두 가지를 분리해서 보여 준다:

```cpp
CHECK(CanAssign<TH, int>);        // requires { f = u; } → 범위와 무관하게 true (타입 검사만)
CHECK(!LitOk<TH, 256>);           // 템플릿 인자 자리에 넣어 상수 평가를 강제 → false
//   template <typename F, auto v> concept LitOk = requires { typename std::integral_constant<int, (hw::Lit<F>(v), 0)>; };
```

템플릿 인자는 상수 표현식이어야 하므로 거기 넣으면 consteval 실패가 substitution failure 로 잡힌다. Apple clang 21 과 GCC 15 모두 `134/134` 로 같은 동작을 확인했다.

**`static_assert` 판(`of<v>`)과의 차이.** 과제가 요구한 전통적 방법이다. NTTP 는 호출부에서 값을 템플릿 인자로 받으니 C++17 에서도 가능하고 에러 메시지를 직접 쓸 수 있다. 단, 본문 `static_assert` 는 인스턴스화 시점의 hard error 라서 `requires` 로 잡히지 않는다 (CPP-B12 참고). 그래서 하네스에서는 `F::fits(v)` 를 직접 CHECK 한다.

**런타임 정책.** 변수 범위는 컴파일러가 모르니 호출부에서 정책을 말하게 한다.

| 정책 | 입력 256 (8비트) | 입력 -1 | 언제 쓰나 |
|---|---|---|---|
| `Wrap` | 0 (상위 비트 버림) | 255 | 값이 항상 맞다고 확신하거나, 카운터처럼 wrap 이 의도일 때. C 의 `(v & MAX)` |
| `Saturate` | 255 | 0 | 설정값을 가능한 범위로 깎고 진행 |
| `Reject` | `ok=false` | `ok=false` | 범위 밖이면 설정 자체를 하지 않는다 |

`fit` 은 호출자의 타입 `I` 로 비교한다. 입력을 파라미터에서 `W` 로 받으면 `0x1'0000'0005` (`int64_t`) 가 비교 전에 5 가 되어 범위 안처럼 통과한다 — 표의 마지막 줄 테스트가 이것을 잡는다. `Reject` 의 런타임 분기는 이 정책을 **고른 호출부에서만** 생긴다.

### Part B — RMW 를 하나로 묶기

```cpp
template <typename First, typename... Rest>
    requires SameRegDisjoint<First, Rest...>
bool write(First first, Rest... rest) {
    using R = typename First::field::reg;
    using W = typename R::word;
    if (!(first.ok && ... && rest.ok)) return false;          // 전부 아니면 전무
    constexpr W mask = static_cast<W>(First::field::mask | (W{0} | ... | Rest::field::mask));
    constexpr W keep = static_cast<W>(~(mask | R::w1c | R::res0));
    const W bits = static_cast<W>(first.bits | (W{0} | ... | rest.bits));
    R::write(static_cast<W>((R::read() & keep) | bits));
    return true;
}
```

- **왜 `F = v` 는 쓰기를 하지 않나.** 인자를 평가하는 순서는 정해져 있지 않고 ([expr.call]), 쓰기를 각각 하면 RMW 가 여러 번이 된다. `F = v` 는 "무엇을 어디에"라는 값(`FieldValue`)만 만든다. 레지스터 접근은 `write()` 안에서 **단 한 번** 일어난다.
- **제약(`requires`)으로 쓴 이유.** 본문 `static_assert` 면 `CanWrite<...>` 로 테스트할 수 없고 hard error 다. `requires` 면 `write` 가 후보에서 빠지고 "no matching function" + 어느 조건이 false 인지 나온다.
- **겹침 검사.** `popcount(a) + popcount(b) == popcount(a | b)`. 두 마스크가 겹치면 OR 에서 그 비트가 한 번만 세어져 합이 달라진다. 같은 필드 중복도 자동으로 걸린다. 맞닿은 필드(bit15 끝, bit16 시작)는 합이 같으니 통과한다.

**실제 에러**

```text
$ ... -DSHOW_ERROR=1   # write(EN = true, BURST = 1)
demo_errors.cpp:10:11: error: no matching function for call to 'write'
./solution.cpp:162:14: note: because 'SameRegDisjoint<...>' evaluated to false
./solution.cpp:156:27: note: because 'same_register_v<...>' evaluated to false

$ ... -DSHOW_ERROR=2   # write(BURST = 1, BURST = 2)
demo_errors.cpp:12:11: error: no matching function for call to 'write'
./solution.cpp:156:62: note: because 'disjoint_v<...>' evaluated to false

$ ... -DSHOW_ERROR=8   # hw::Field<CSR, 4, 4> e8;  (예약 비트 위)
demo_errors.cpp:24:5: error: constraints not satisfied for class template 'Field' [with R = hw::Reg<1073872896, unsigned int, 1792, 4294965488>, Offset = 4, Width = 4, V = unsigned int]
./solution.cpp:188:15: note: because 'field_fits_reg<...>(4U, 4U)' evaluated to false
```

(`<...>` 는 긴 타입 이름을 줄인 것이고 나머지는 실제 출력이다.) 레지스터 주소가 10진수(`1073872896` = `0x40020000`)로 나오는 것은 NTTP 의 한계다.

### 비교: C 비트필드는 왜 안 되나

**[class.bit]** "Allocation of bit-fields within a class object is implementation-defined." 비트필드의 순서, 저장 단위를 넘는 필드의 처리, 패딩은 전부 구현 정의다 (비트필드가 "오른쪽에서 왼쪽 또는 왼쪽에서 오른쪽"으로 배치될 수 있다는 것도 표준의 note 에 적혀 있다). 또한 `sizeof(Bits)` 만 맞는다고 레지스터에 쓸 수 있는 것도 아니다.

직접 확인 (`demo_bitfield_layout.cpp`, 같은 소스, LLVM IR 로 타깃별 결과만 뽑음):

```text
for t in thumbv7em-none-eabi thumbebv7em-none-eabi powerpc-unknown-linux-gnu x86_64-pc-windows-msvc arm64-apple-macos14; do
  clang++ --target=$t -std=c++20 -O2 -emit-llvm -S demo_bitfield_layout.cpp -o - | grep -E 'size_|ret i32'; done

struct Bits { unsigned a:4; unsigned b:12; unsigned c:16; };  // a=0x1, b=0x234, c=0x5678 를 넣고 32비트로 읽음
struct Mixed { unsigned char a:4; unsigned b:4; };
```

| 타깃 | Bits 를 u32 로 읽은 값 | sizeof(Mixed) |
|---|---|---|
| thumbv7em (Cortex-M, little-endian) | `0x56782341` (a 가 최하위) | 4 |
| thumbebv7em (big-endian ARM) | `0x12345678` (a 가 최상위) | 4 |
| powerpc linux (big-endian) | `0x12345678` | 4 |
| x86_64 windows-msvc | `0x56782341` | **8** |
| arm64 macOS | `0x56782341` | 4 |

같은 소스가 타깃에 따라 비트 순서도 `sizeof` 도 달라진다. MSVC ABI 는 선언 타입이 바뀌면 새 저장 단위를 쓴다. 이식성 이야기가 추상적이지 않다.

`Field` 는 `(reg & mask) >> offset` 이라는 **값 연산**만 쓰므로 CPU 엔디언과 ABI 에 무관하다. (주의: 레지스터 자체의 엔디언·접근 폭은 별개 문제다.)

### 실측: 명령이 정말 같은가 — thumbv7em

`demo_target.cpp` 는 호스트에서 실행하지 않는 Cortex-M 용 파일이다 (실제 MMIO, `#include` 없음). `demo_target.cpp` 안의 `Field` 는 consteval 연산자로 줄인 축약판이지만 `write()` 의 keep/bits 계산은 본문과 같다.

```text
$ clang++ --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c++20 -O2 -fno-exceptions -fno-rtti -c demo_target.cpp -o dt.o
$ xcrun llvm-objdump -d --no-show-raw-insn -C dt.o     (bf_two_const·runtime 은 주소 로드 줄과 bx lr 생략, 나머지는 실제 출력)

00000000 <field_two_const>:               00000014 <c_two_const>:
       0:  movs r0, #0x0                        14:  movs r0, #0x0
       2:  movt r0, #0x4002                     16:  movt r0, #0x4002
       6:  ldr  r1, [r0]                        1a:  ldr  r1, [r0]
       8:  and  r1, r1, #0x2                    1c:  and  r1, r1, #0x2
       c:  adds r1, #0x9                        20:  adds r1, #0x9
       e:  str  r1, [r0]                        22:  str  r1, [r0]
      10:  bx   lr                              24:  bx   lr

00000028 <bf_two_const>:                  (volatile 비트필드 대입 2개)
      2e:  ldr  r1, [r0]
      30:  movs r2, #0x2
      32:  orr  r1, r1, #0x1
      36:  str  r1, [r0]
      38:  ldr  r1, [r0]
      3a:  bfi  r1, r2, #2, #2
      3e:  str  r1, [r0]

00000044 <field_runtime>:                 0000005c <c_runtime>:
      4a:  ldr  r2, [r1]                        62:  ldr  r2, [r1]
      4c:  and  r0, r0, #0x3                    64:  and  r0, r0, #0x3
      50:  and  r2, r2, #0x3                    68:  and  r2, r2, #0x3
      54:  orr.w r0, r2, r0, lsl #2             6c:  orr.w r0, r2, r0, lsl #2
      58:  str  r0, [r1]                        70:  str  r0, [r1]
```

`arm-none-eabi-g++ 14.2.1` (Lima VM, `-O2 -mcpu=cortex-m4 -mthumb`) 도 같은 결론이다. 주소를 리터럴 풀에서 읽는 것만 다르고 Field 와 손으로 쓴 C 가 일치한다.

```text
00000000 <field_two_const>:               00000014 <c_two_const>:
   0:  ldr    r2, [pc, #12]                 14:  ldr    r2, [pc, #12]
   2:  ldr    r3, [r2, #0]                  16:  ldr    r3, [r2, #0]
   4:  and.w  r3, r3, #2                    18:  and.w  r3, r3, #2
   8:  orr.w  r3, r3, #9                    1c:  orr.w  r3, r3, #9
   c:  str    r3, [r2, #0]                  20:  str    r3, [r2, #0]
   e:  bx     lr                            22:  bx     lr
```

| 비교 | ldr(읽기) | str(쓰기) | 명령 수 (clang / GCC, `bx lr` 포함, 패딩 nop 제외) |
|---|---|---|---|
| Field 필드 2개 | 1 | 1 | 7 / 6 |
| 손으로 쓴 C (같은 의미) | 1 | 1 | 7 / 6 |
| volatile 비트필드 대입 2개 | **2** | **2** | 10 / 9 |

`and r1, #2` 는 keep 마스크다 (DIR 만 유지). W1C 와 예약 비트가 **and 로 전부 지워진 뒤** `adds #9` 로 EN=1, PRIO=2 가 들어간다. volatile 접근은 observable 이라 ([intro.abstract]) 컴파일러가 두 대입을 하나로 합칠 수 없다 — 대입 2개는 RMW 2쌍으로 나왔다 (volatile glvalue 접근의 정확한 의미 자체는 구현 정의 [dcl.type.cv]). 그리고 `bf_en_only` 를 보면 비트필드 RMW 는 읽은 값을 되쓴다 (주소 로드·`bx lr` 줄 생략):

```text
00000074 <bf_en_only>:
      7a:  ldr  r1, [r0]
      7c:  orr  r1, r1, #0x1      ← W1C 비트가 1 로 읽혔으면 그대로 1 로 쓴다. 비트필드는 "W1C" 를 모른다.
      80:  str  r1, [r0]

00000084 <bf8_en_only>:           (struct { volatile unsigned char en:1; })
      8a:  ldrb r1, [r0]
      90:  strb r1, [r0]
```

`unsigned char` 로 선언하면 **접근 폭이 8비트**가 된다 (clang, GCC 둘 다 `ldrb/strb` 실측). 32비트 접근만 허용하는 주변장치에서는 문제가 된다. `volatile uint32_t` 로 선언해야 ldr/str 이 나오는 것은, GCC 쪽은 `arm-none-eabi-gcc -Q --help=common` 에서 `-fstrict-volatile-bitfields [enabled]` 로 확인했다 (AAPCS 의 volatile bit-field 규칙으로 알고 있지만 이 레슨에서 문서를 다시 열어 보진 않았다: 미확인). 그래도 비트 배치 자체는 ABI 약속이지 C++ 표준의 약속이 아니다.

정리: Field 는 **같은 명령을 내면서** 범위·겹침·레지스터 혼용을 컴파일 타임에 잡는다. "비트필드 대신 Field" 의 비용은 런타임이 아니라 템플릿 컴파일 시간과 에러 메시지 길이에 있다.

### Part C — W1C · 예약 비트의 함정

**W1C (Write-1-to-Clear).** 인터럽트 플래그는 흔히 "1 을 쓰면 지워진다." 이 규칙 아래서 `CSR |= EN` 은:

1. 읽기: TC, HT 가 pending 이면 1 로 읽힘
2. EN 을 OR
3. 쓰기: TC, HT 자리에 1 이 쓰임 → **지워짐**. ISR 이 처리하기 전에 인터럽트가 사라진다.

하네스의 기준선 테스트가 이것을 실제로 재현한다:

```cpp
mock::hw_set(CSR::addr, 0x100u | 0x200u);   // TC, HT pending
naive_enable();                             // CSR::write(CSR::read() | 1u)
CHECK(regs[0].value == 0x1u);               // EN 만 남고 플래그 2개가 사라졌다
CHECK(mock::res0_violations == 1u);         // junk 로 읽힌 예약 비트도 되썼다
```

고치는 법은 `keep` 에서 W1C 와 예약 비트를 빼는 것이다. 그러면 `dma_start` 의 쓰기 값은 플래그 자리가 항상 0 이다.

**ack.** 플래그를 지울 때는 고른 비트만 1 로 쓴다. 나머지 W1C 는 0 (= 지우지 않음), 일반 RW 비트는 유지:

```cpp
constexpr W keep = static_cast<W>(~(R::w1c | R::res0));
if constexpr (keep == 0) R::write(flags);                                 // 순수 W1C: 읽기 0번
else R::write(static_cast<W>((R::read() & keep) | flags));                // 혼합: 읽기 1번
```

읽을 필요가 없는 레지스터를 읽지 않는 것은 사소한 최적화가 아니다. 읽기에 부작용이 있는 레지스터(읽으면 지워지는 FIFO, RC(read-clear) 상태)에서는 읽기 자체가 위험하다. STM32 `EXTI_PR` (pending, rc_w1) 은 `EXTI->PR |= bit` 이 다른 pending 까지 지우는 고전적인 함정이다 (RM0090 기준 — 이 레슨에서 PDF 를 다시 열어 보진 않았다: 미확인).

**타입으로 막기.** W1C 필드는 `is_w1c` 이고, `operator=` 와 `with`, `of` 는 `requires writable` 이다. `TC = true` 는 쓰는 사람이 "1 을 썼으니 켜진다"고 오해할 표현이라 아예 막고, `ack(TC)` 라는 다른 이름을 강제한다. 실제 에러:

```text
$ ... -DSHOW_ERROR=6   # hw::write(TC = true)
demo_errors.cpp:20:24: error: no viable overloaded '='
./solution.cpp:221:47: note: candidate function not viable: constraints not satisfied
./solution.cpp:221:88: note: because 'writable' evaluated to false
```

**예약 비트.** 매뉴얼에 따라 "읽은 값을 유지"(UNK/SBZP 류)일 수도, "0 으로 써야"(RES0)일 수도 있다. 이 레슨의 mock 은 후자(읽으면 junk, 1 을 쓰면 위반)이고 `Reg` 의 `Res0` 가 그 비트를 지정한다. 어느 쪽인지는 칩의 reference manual 을 보고 정한다 — "예약 비트는 전부 RMW 로 보존" 이 항상 옳지 않다.

```svg
<svg viewBox="0 0 740 232" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">RMW 시간선 — 무엇이 안전하고 무엇이 안전하지 않은가</text>
  <g stroke="currentColor" fill="none">
    <path d="M40 60 L700 60"/>
    <path d="M160 48 L160 72"/><path d="M500 48 L500 72"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="130" y="40">읽기(ldr)</text><text x="470" y="40">쓰기(str)</text>
    <text x="270" y="86">이 구간에 ISR / 하드웨어가 끼어든다</text>
  </g>
  <rect x="160" y="96" width="340" height="30" class="f-ok" stroke="currentColor"/>
  <text x="170" y="116" fill="currentColor" font-size="12">구간 중 하드웨어가 W1C 플래그 set → 우리가 0 을 쓰므로 유지 (테스트로 확인)</text>
  <rect x="160" y="134" width="340" height="30" class="f-warn" stroke="currentColor"/>
  <text x="170" y="154" fill="currentColor" font-size="12">구간 중 ISR 이 같은 레지스터의 RW 비트 변경 → 옛 값으로 덮어씀 (사라짐)</text>
  <text x="40" y="186" fill="currentColor">읽기 1번·쓰기 1번은 "원자적"이 아니다. RW 비트까지 안전하려면 임계 구역 (CPP-B06) 이나</text>
  <text x="40" y="206" fill="currentColor">set/clear 전용 레지스터(BSRR 류, CPP-M04), bit-band, 또는 레지스터 분리가 필요하다.</text>
</svg>
```

이 그림의 아래쪽 줄을 하네스가 고정해 둔다. "Field 를 쓰면 안전"이 아니라는 것을 정직하게 남긴 테스트다.

```cpp
mock::after_read = [] { regs[0].value |= 0x2u; };   // 읽기 직후 ISR 이 DIR 을 켬
CHECK(hw::write(EN = true));
CHECK((regs[0].value & 0x2u) == 0u);                // ISR 의 변경이 사라졌다
```

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```text
$ c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp  -o st && ./st | tail -1
37/134 passed
$ c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o so && ./so | tail -1
134/134 passed
$ c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer solution.cpp -o sa && ./sa | tail -1
134/134 passed
```

(ASan/UBSan 경고 없음. 같은 solution.cpp 는 Lima VM 의 GCC 15.2 `g++ -std=c++20 -Wall -Wextra -Werror` 로도 134/134.)

## 7. 자주 하는 실수

**1. `1u << width` 를 폭 32 에서도 그대로 쓴다.**
UB. `(W{1} << width) - 1` 은 전체 폭에서 분기가 필요하다. `constexpr` 이면 컴파일 에러로 드러나지만, 런타임 코드로 쓰면 ARM 과 x86 에서 값이 다르게 나온다.

**2. 범위 비교 전에 좁힌다.**
```cpp
template <typename W> bool fits_bad(W v, W max) { return v <= max; }   // fits_bad<uint32_t>(-1, 255) → -1 은 이미 0xFFFFFFFF
                                                                       // fits_bad<uint32_t>(0x1'0000'0005ull, 255) → 5 → 통과!
```
파라미터를 호출자 타입 `U` 로 받고 `std::cmp_less_equal(u, max)` 로 비교한다.

**3. keep 마스크를 `~mask` 로만 만든다.**
```cpp
R::write((R::read() & ~mask) | bits);                        // W1C 플래그를 되써서 지운다
R::write((R::read() & ~(mask | R::w1c | R::res0)) | bits);   // 위험 비트는 항상 0
```

**4. 필드 값을 만드는 순간 쓴다고 착각한다.**
`THRESH = 3;` 은 `FieldValue` 를 만들 뿐이고 레지스터는 그대로다. `[[nodiscard]]` 가 없으면 조용히 아무 일도 안 일어난다. `write(THRESH = 3)` 이 진짜 쓰기다. `-Werror` 빌드에서 이 실수는 에러가 된다 (실측: `SHOW_ERROR=7`).

**5. 필드마다 RMW 를 따로 한다.**
```cpp
write(EN = true);                  // 읽기·쓰기 1쌍
write(PRIO = 2u);                  // 또 1쌍 → 그 사이 EN 만 켜진 상태가 하드웨어에 보인다
write(EN = true, PRIO = 2u);       // 한 번에
```
일부 주변장치는 EN 을 켜는 순간 다른 설정을 래치한다. 중간 상태가 보이면 안 되는 레지스터에서 특히 치명적이다. (EN 과 설정을 다른 순서로 써야 하는 칩도 있다 — 데이터시트의 순서를 따를 것.)

**6. `requires` 안에서 `F = 256` 이 false 라고 믿는다.**
requires 식은 unevaluated 다. consteval 이 실행되지 않으므로 타입만 맞으면 `true`. 값까지 보려면 템플릿 인자 자리로 상수 평가를 강제한다 (`LitOk`).

**7. 템플릿 안에서 `.template` 을 빠뜨린다.**
```cpp
template <typename F> void f() { F::with<hw::Wrap>(1); }           // <, > 를 비교 연산자로 파싱
template <typename F> void f() { F::template with<hw::Wrap>(1); }  // 맞음 (fuzz_field 참고)
```

**8. `bool` 필드에 정수를 넣는다.**
`EN = 2` 는 암시적 변환으로 `true` 가 되어 조용히 통과할 수 있다. 이 레슨의 bool `operator=` 는 `std::same_as<bool> B` 템플릿으로 그 변환을 끊는다 (`CanAssign<FT(EN), int>` 가 false 인 이유). 정수 필드에 `bool` 을 넣는 것도 `PlainInt` 가 막는다.

## 8. 면접 Q&A

<details>
<summary>Q1. C 비트필드로 레지스터를 정의하면 안 되는 이유는? 그런데 왜 많은 코드가 그렇게 쓰나?</summary>

[class.bit] 에 따라 저장 단위 안의 배치, 정렬, 저장 단위를 넘는 필드의 처리가 구현 정의다. 실제로 thumbv7em 과 big-endian 타깃, MSVC ABI 에서 값과 `sizeof` 가 달랐다 (본문 표). 또 대입마다 RMW 가 나가고, W1C 를 모른다. 그럼에도 쓰는 이유: 단일 컴파일러 + 단일 ABI(AAPCS) 로 고정된 프로젝트에서는 실무적으로 동작하고, 읽기 좋기 때문이다. 다만 벤더 헤더(CMSIS)는 `_Pos` / `_Msk` 매크로를 표준으로 삼는다.

</details>

<details>
<summary>Q2. `write(F1=a, F2=b)` 가 정확히 읽기 1번, 쓰기 1번임을 어떻게 보장하나? 컴파일러가 합칠 수도 있지 않나?</summary>

`F = v` 는 값만 만들고, `write()` 안에서 `R::read()` 한 번, `R::write()` 한 번을 호출한다. `volatile` 접근은 observable behavior 라 ([intro.abstract]) 컴파일러가 줄이거나 늘리거나 합칠 수 없다. 테스트는 mock 로그로 횟수와 순서를 직접 세고, thumbv7em asm 에서도 ldr 1개, str 1개를 확인했다 (필드 값은 상수 접기). 꼬리: "그럼 RMW 가 원자적인가?" → 아니다. Q3.

</details>

<details>
<summary>Q3. 읽기 1번 + 쓰기 1번이면 ISR 과 안전한가?</summary>

아니다. 읽기와 쓰기 사이에 ISR 이 같은 레지스터의 RW 비트를 바꾸면 옛 값으로 덮어쓴다 (본문의 "정직한 테스트"). 해결: 임계 구역으로 감싸기(CPP-B06), set/clear 전용 레지스터 사용(CPP-M04, BSRR), Cortex-M3/M4 의 bit-band, 또는 같은 레지스터를 ISR 과 공유하지 않는 설계. Cortex-M7 에는 bit-band 가 없다. 다만 RMW 구간에 **W1C 플래그**가 set 되는 경우는 안전하다 — 우리가 그 자리에 0 을 쓰기 때문이다. 이 둘의 구분이 핵심이다. 꼬리로 rc_w0 를 물으면 Q5.

</details>

<details>
<summary>Q4. consteval 과 `static_assert` 중 어느 쪽으로 범위 검사를 하나?</summary>

- `consteval` 생성자: `F = 3` 처럼 자연스러운 문법. 변수는 자동으로 거부. 메시지는 함수 이름으로만 전달된다. 호출마다 상수 평가.
- NTTP + `static_assert`: `F.of<3>()`. 메시지를 직접 쓸 수 있고 C++17 에서 가능. 하지만 `requires` 로 탐지 불가(hard error)이고 호출부 문법이 시끄럽다.
- 함수 본문 `static_assert(v < 8)` 는 함수 파라미터가 상수가 아니므로 불가능하다 — "constexpr 함수 인자로 static_assert" 는 C++ 에 없다. 꼬리: `if consteval` 은 C++23 이다 (C++20 의 `std::is_constant_evaluated` 와 구분).

</details>

<details>
<summary>Q5. STM32 TIM 의 상태 레지스터는 W1C 가 아니라 rc_w0 다. 무엇이 달라지나?</summary>

0 을 쓰면 지워지고, 1 을 쓰면 무변화. `SR &= ~FLAG` 는 RMW 이므로 읽은 뒤에 생긴 플래그는 0 으로 읽혔던 값이 되써져 **지워진다** (W1C 때와 정반대 방향의 사고). 올바른 방법은 읽지 않고 `SR = ~FLAG` (다른 비트는 1 → 무변화). 추가 연습 5 가 이 동작을 mock 으로 보여 준다. (TIMx_SR 의 rc_w0 는 RM0090 등의 기억 기준 — 이 레슨에서 PDF 재확인은 못 했다: 미확인.)

</details>

<details>
<summary>Q6. `svd2rust` 의 `write` / `modify` 와 이 API 의 `write` 는 뭐가 다른가?</summary>

svd2rust 의 `write(|w| ...)` 는 **리셋 값에서 시작해 쓰기만**(읽기 0번), `modify(|r, w| ...)` 가 RMW 다. 이 레슨의 `write(F=..)` 는 과제 요구대로 RMW (읽기 1, 쓰기 1) 이고, 읽기 없는 전체 쓰기는 `overwrite` (추가 연습 1) 이다. API 설계 교훈: 이름만 보고 읽기 유무를 판단하지 말고 문서로 고정하라. 꼬리로 "읽기 없이 쓰면 뭐가 좋나?" → 읽기 부작용 회피, 버스 사이클 절약, RMW 경쟁 구간 제거.

</details>

<details>
<summary>Q7. `Field` 의 값 타입을 enum 으로 했을 때 `read()` 결과를 믿어도 되나?</summary>

읽은 값은 enumerator 에 없을 수 있다 (하드웨어가 `0b11` 을 돌려주는데 enum 은 `B8/B16/B32` 뿐일 때). 고정 기저 타입 enum 에 그 값을 캐스트하는 것 자체는 UB 가 아니다 — 값의 범위는 기저 타입이다 ([dcl.enum]). 다만 `switch` 에서 `default` 없이 처리하면 놓친다. 필요하면 `read_checked()` 같은 경로로 유효성 검사를 추가한다.

</details>

## 9. 추가 연습

아래 5개 답은 모두 `practice.cpp` 에 있다. 코어 `Field`/`Reg` 는 이 파일만으로 빌드되도록 축약해 넣었다.

### 연습 1 — `overwrite`: 읽기 없는 전체 쓰기

주어진 `base` 위에 필드들을 끼워 넣어 **쓰기 1번, 읽기 0번**으로 내보내라. `base` 가 전부 1 이어도 W1C 비트와 예약 비트에는 안전한 값이 나가야 한다. (rc_w0 비트라면 1.)

<details>
<summary>답</summary>

`v = base & ~(mask | w1c | res0 | w0c)` 로 위험 비트를 지운 뒤 `v | w0c | bits`. `w1c` 는 0 (= 지우지 않음), `w0c` 는 1 (= 지우지 않음), 일반 비트는 `base` 값, 필드는 새 값. 테스트에서 `base=0xFFFFFFFF` 일 때 결과가 `0x0000FFF2` (플래그 유지, 예약 비트 안 켜짐, 비필드 RW 비트는 `base` 대로) 임을 확인한다.

</details>

### 연습 2 — `Snapshot<R>`: 한 번 읽고 여러 필드 해석

여러 필드를 읽을 때 `read()` 를 필드마다 부르면 값이 찢어진다 (읽는 사이에 바뀔 수 있다). 레지스터를 한 번 읽어 두고 `s.get(F)` 로 해석하는 클래스를 만들어라. 다른 레지스터의 필드는 컴파일 에러.

<details>
<summary>답</summary>

생성자에서 `raw_ = R::read()`, `get` 은 `F::decode(raw_)`. `requires same_as<typename F::reg, R>` 로 레지스터 혼용을 막는다. `get` 은 `R::read()` 를 호출하지 않으므로 필드 수와 무관하게 읽기는 1번이다.

</details>

### 연습 3 — 부호 있는 필드

온도·오프셋 필드는 2의 보수로 저장된다. `SignedField<R, Offset, Width>` 의 `fits`, `decode`, `encode` 를 만들어라. 경계: `Width=4` → −8 ~ 7, `Width=32` 전체 폭.

<details>
<summary>답</summary>

`decode`: 필드를 꺼낸 뒤 `int64_t(f ^ sign) - int64_t(sign)`, `sign = 1 << (Width-1)`. 부호 비트를 뒤집고 빼면 음수가 복원된다. `int` 시프트(`1 << 31`)를 쓰지 않는 것이 포인트. `encode`: `(word(v) & max_raw) << Offset` — 음수의 unsigned 변환은 모듈러로 정의되어 있다 ([conv.integral]).

</details>

### 연습 4 — 데이터시트 마스크에서 필드 만들기

`FieldFromMask<R, 0x0FF0>` 는 `offset=4, width=8` 인 필드와 같다. 마스크가 끊어져 있거나(0x5) 0 이면 컴파일이 안 되어야 한다.

<details>
<summary>답</summary>

`offset = countr_zero(mask)`, `width = popcount(mask)`. 연속성: `shifted = mask >> countr_zero(mask)` 후 `(shifted & (shifted + 1)) == 0`. `mask = 0xFFFFFFFF` 는 `shifted + 1` 이 0 으로 wrap 하므로 unsigned 로 계산해야 맞다. 별칭 템플릿에 `requires (contiguous<...>(Mask))` 를 붙여 잘못된 마스크를 거른다.

</details>

### 연습 5 — rc_w0 플래그: 읽지 않고 지우기

Q5 의 시나리오를 mock 으로 재현하라. `clear_w0_naive` 는 읽기 직후 생긴 플래그를 잃어야 하고, `clear_w0` 는 읽기 0번으로 그 플래그를 보존해야 한다.

<details>
<summary>답</summary>

`clear_w0_naive`: `R::write(R::read() & ~flag)`. 훅이 읽기 직후 flag1 을 세우면, 읽은 값의 flag1 이 0 이었으므로 0 이 쓰여 지워진다. `clear_w0`: `R::write(R::w0c & ~flag)` — 지울 비트만 0, 나머지 rc_w0 는 1 (무변화), 예약 비트는 0.

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice | tail -1
# 36/36 passed
```

## 10. 핵심 정리

- C 비트필드의 배치는 구현 정의다 ([class.bit]). 같은 소스가 thumbv7em 과 big-endian, MSVC ABI 에서 다른 결과와 `sizeof` 를 낸다 (실측).
- `Field<R, Offset, Width>` 는 `(reg & mask) >> offset` 이라는 값 연산만 쓰므로 이식 가능하다. 경계: 폭 32 는 시프트 UB, 8/16비트는 정수 승격.
- 범위 검사는 둘로 나눈다: 상수는 `consteval`/`static_assert` 로 컴파일 타임에, 변수는 `Wrap / Saturate / Reject` 정책으로 호출부에서 명시.
- `F = v` 는 값만 만든다. `write(F1=a, F2=b)` 가 **읽기 1번 + 쓰기 1번**으로 내보낸다. 다른 레지스터·겹치는 필드는 컴파일 에러.
- RMW 의 keep 마스크는 `~(mask | w1c | res0)`. W1C 를 되쓰면 플래그가 지워진다. 비트필드는 W1C 를 모른다.
- W1C 필드는 `=` 금지, `ack()` 로만. 순수 W1C 레지스터는 읽지 않는다.
- 읽기 1 + 쓰기 1 은 원자적이지 않다. 플래그는 안전, RW 비트는 안전하지 않다 — 임계 구역이나 set/clear 레지스터로 해결.
- thumbv7em 실측: Field 와 손으로 쓴 C 는 clang·GCC 모두 같은 명령. volatile 비트필드 대입 2개는 ldr/str 2쌍.

## 참고자료

- [class.bit] Bit-fields — 배치가 구현 정의: https://eel.is/c++draft/class.bit
- [expr.prim.req], [expr.const] — requires 식과 immediate invocation: https://eel.is/c++draft/expr.prim.req · https://eel.is/c++draft/expr.const
- [utility.intcmp] `std::cmp_less` 등 — C++20: https://en.cppreference.com/w/cpp/utility/intcmp
- [expr.shift] 시프트 UB, [conv.prom] 정수 승격, [conv.integral] 모듈러 변환
- AAPCS32 — volatile bit-fields 절: https://github.com/ARM-software/abi-aa/blob/main/aapcs32/aapcs32.rst
- ST RM0090 — `EXTI_PR`(rc_w1), `TIMx_SR`(rc_w0) 표기
- svd2rust 의 `read / write / modify` 모델: https://docs.rs/svd2rust/
- 이 레슨의 실측 파일: `demo_errors.cpp` (에러 메시지), `demo_bitfield_layout.cpp` (타깃별 배치), `demo_target.cpp` (thumbv7em asm)
