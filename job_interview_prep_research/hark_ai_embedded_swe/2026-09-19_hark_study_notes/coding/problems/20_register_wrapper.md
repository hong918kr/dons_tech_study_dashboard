# 20. 타입 안전 레지스터 필드 접근

> **주제**: 템플릿 · `constexpr` 마스크 계산 · `static_assert` · 열거형 필드 · **난이도**: 중급 · **목표 시간**: 30분
> **해설은 보지 말 것**: 먼저 `starters/20_register_wrapper.cpp`를 채워 `make run N=20`으로 통과시킨다.

## 면접관의 문장

"Look at this line from our driver: `UART->CR = (UART->CR & ~(0x3u << 1)) | (2u << 1);`. It works, but nobody can read it, and last month somebody wrote `4u << 1` into a two-bit field and quietly turned on the stop-bit flag next door. It took two days to find.

Do it with the type system instead. I want a field descriptor that takes the MSB and LSB as template parameters and computes the mask, the shift, and the maximum value at compile time. A register wrapper with `get<Field>()` and `set<Field>(value)`.

Three things have to be compile errors, not runtime surprises: a field declared with MSB below LSB, a bit number above 31, and a constant that doesn't fit the field width. And when I change three fields in one register, I want to see one read and one write — not three."

## 요구사항

1. `ones(width)`는 하위 `width`개 비트가 1인 값을 돌려주는 `constexpr` 함수다. `ones(0) == 0`, `ones(8) == 0xFF`, `ones(32) == 0xFFFFFFFF`. `width == 32`에서 `1u << 32`(미정의 동작)를 밟지 않아야 한다.
2. `Field<Msb, Lsb, E>`는 `width`, `max`, `mask`를 `static constexpr` 멤버로 갖는다. `E`는 이 필드가 의미하는 타입이며 기본값은 `std::uint32_t`다.
3. `Field`는 `Msb < 32`와 `Lsb <= Msb`를 `static_assert`로 강제한다. `Field<3, 7>`이나 `Field<32, 30>`은 컴파일되지 않아야 한다.
4. `fits(raw)`는 값이 필드 폭에 들어가는지, `encode(raw)`는 값을 필드 자리로 옮긴 비트 패턴을, `decode(reg)`는 레지스터 값에서 필드만 뽑아 0부터 시작하는 값을 돌려준다. 셋 다 `constexpr`이다.
5. `disjoint<A, B>()`는 두 필드의 마스크가 겹치지 않는지 `constexpr`로 판정한다. 데이터시트 오독을 빌드 시점에 잡기 위한 것이다.
6. `Reg32`는 `volatile std::uint32_t`를 가리키는 포인터 하나만 갖는다. `sizeof(Reg32) == sizeof(void*)`여야 한다.
7. `get<F>()`의 반환 타입은 `F::value_type`이다. 열거형 필드는 열거형이 나온다.
8. `set<F>(v)`의 인자 타입도 `F::value_type`이다. 열거형 필드에 정수를 넘기면 컴파일 에러여야 한다. 내부적으로 read-modify-write를 **정확히 한 번** 한다(다른 필드 보존).
9. `set_const<F, V>()`는 값 `V`를 `static_assert(F::fits(V))`로 컴파일 타임에 검사한다.
10. `test<F>()`는 1비트 필드를 `bool`로 읽는다. 폭이 1이 아닌 필드에 쓰면 `static_assert`로 거부한다.
11. `modify_masked(mask, value)`는 미리 계산한 마스크와 값으로 한 번의 RMW를 한다. 필드 3개를 바꾸는 드라이버 함수가 MMIO 읽기 1회, 쓰기 1회만 하도록 만드는 데 쓴다.
12. 레지스터 초기값 전체를 `constexpr` 상수로 조립할 수 있어야 한다. 그 경우 MMIO 읽기는 0회, 쓰기는 1회다.
13. 경계 필드가 정확해야 한다. `Field<31,31>`(최상위 1비트), `Field<31,24>`, `Field<31,0>`(전체 폭)에서 마스크와 값 왕복이 맞아야 한다.

## 인터페이스

```cpp
constexpr std::uint32_t ones(std::uint32_t width);

template <std::uint32_t Msb, std::uint32_t Lsb, typename E = std::uint32_t>
struct Field {
    using value_type = E;
    static constexpr std::uint32_t msb, lsb, width, max, mask;
    static constexpr bool fits(std::uint32_t raw);
    static constexpr std::uint32_t encode(std::uint32_t raw);
    static constexpr std::uint32_t decode(std::uint32_t reg);
};

template <typename A, typename B> constexpr bool disjoint();

class Reg32 {
public:
    explicit Reg32(volatile std::uint32_t *addr) noexcept;
    std::uint32_t raw() const noexcept;
    void write_raw(std::uint32_t v) noexcept;
    template <typename F> typename F::value_type get() const noexcept;
    template <typename F> void set(typename F::value_type v) noexcept;
    template <typename F, std::uint32_t V> void set_const() noexcept;
    template <typename F> bool test() const noexcept;
    void modify_masked(std::uint32_t mask, std::uint32_t value) noexcept;
};
```

## 제약

- C++17. 힙 금지. 예외·RTTI 없음. 허용 헤더: `<cstdint> <cstddef> <new> <type_traits> <cassert> <cstdio> <utility>`.
- 레지스터 접근은 주어진 `mmio_read` / `mmio_write`를 통해서만 한다(접근 횟수를 세기 위한 계측이 들어 있다). 실제 타깃에서는 `volatile` 접근 그 자체다.
- 런타임 값의 범위는 `assert`로만 막을 수 있다. 상수는 `static_assert`로 막는다. 이 둘의 차이를 코드로 보여야 한다.
- 실제 칩의 레지스터 이름을 쓰지 않는다. 이 문제의 UART CR 비트 배치는 예시다.
- `c++ -std=c++17 -Wall -Wextra -O2 -g -fno-exceptions -fno-rtti`에서 경고 0개.

## 예시 동작

예시 UART 제어 레지스터:

```
 31        24 23 22 21 20            8  7   6 5   4 3   2   1 0
 +-----------+--+--+--+---------------+---+---+---+---+---+---+
 | PRESCALE  |  |  |DMA|    BAUDDIV   |   |WLEN |STP| PARITY|EN|
 +-----------+--+--+--+---------------+---+---+---+---+---+---+
```

| 필드 | 선언 | width | mask |
|---|---|---|---|
| EN | `Field<0,0>` | 1 | `0x00000001` |
| PARITY | `Field<2,1,Parity>` | 2 | `0x00000006` |
| STOP | `Field<3,3,Stop>` | 1 | `0x00000008` |
| WLEN | `Field<5,4>` | 2 | `0x00000030` |
| BAUDDIV | `Field<20,8>` | 13 | `0x001FFF00` |
| DMA | `Field<21,21>` | 1 | `0x00200000` |
| PRESCALE | `Field<31,24>` | 8 | `0xFF000000` |

```
s_uart_cr = 0xFFFFFFFF
cr.set<F_Parity>(Parity::None)   -> 0xFFFFFFF9   읽기 1회, 쓰기 1회
cr.set<F_Parity>(Parity::Odd)    -> 0xFFFFFFFD   이웃 비트 보존

uart_configure(cr, 556, Parity::Odd, Stop::Two)  필드 3개, 읽기 1회 쓰기 1회

kUartInitValue = 0x02222C33   (컴파일 타임 조립, 읽기 0회 쓰기 1회)
필드를 하나씩 7번 set 한 결과도 0x02222C33 — 대신 읽기 7회 쓰기 7회
```

## 스스로 점검할 질문

1. `1u << 32`가 왜 미정의 동작인가? `ones(32)`를 어떻게 안전하게 만들었는가?
2. `mask`를 `static constexpr` 멤버로 두면 flash에 무엇이 남는가? 남지 않게 하려면?
3. 열거형 필드에 정수를 넘기는 것을 막는 장치는 정확히 어디에 있는가?
4. `set<F>()`가 `volatile` 레지스터를 두 번 읽지 않는다고 어떻게 보장하는가?
5. 필드 3개를 따로 `set`하면 하드웨어가 중간 상태를 보게 된다. 그게 문제가 되는 레지스터의 예를 들어 보라.
6. 이 래퍼가 C 매크로 버전과 같은 기계어를 만든다고 어떻게 확인하는가?
7. write-only 레지스터나 read-clear 비트가 있는 레지스터에서 RMW는 어떤 사고를 내는가?
8. `Field`를 `enum class`가 아닌 `struct`로 만든 이유는 무엇인가? 타입 태그로 쓰려면 무엇이 필요한가?

## follow-up (면접관이 이어서 물을 것)

1. "Make a register that can only be written, and one that can only be read. How does the type stop the wrong call?"
2. "Now the register is 16 bits on one chip and 32 on another. Same driver code?"
3. "How would you generate these field definitions from an SVD file?"
4. "Add a `modify` that takes several field/value pairs and does one write, with the mask computed at compile time."
5. "Where does this abstraction actually cost you code size?"

---

**해설**: [notes/L3_embedded_cpp.md](../drills/L3_embedded_cpp.html) §6 · **답안**: `solutions/20_register_wrapper.cpp`
