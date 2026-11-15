---
id: CPP-M11
time: 90분
updated: 2026-11-08
---
# CPP-M11 예외 없는 에러 처리: Result<T, E> (expected 스타일)

## 학습 목표

- `Result<T,E>` 를 **비자명 타입까지** 동작하게 만들고, `Result<void,E>` 와 `map` / `map_error` / `and_then` 을 구현한다.
- 버스 → 드라이버 → 앱으로 **에러를 층별로 번역하면서 컨텍스트(어느 레지스터, 어느 센서)를 잃지 않는** 에러 타입을 설계한다.
- `TRY` 전파 매크로를 statement expression 으로 만들고, 이 기능이 **비표준**이라는 점과 함정(형제 인자 평가, `-pedantic`)을 설명한다.
- `[[nodiscard]]` 가 **잡는 것과 못 잡는 것**을 표로 말할 수 있다 (`(void)`, 참조 반환, 함수 포인터, 변수에 받고 안 쓰기).
- thumbv7em 에서 에러 코드 / `Result` / C++23 `std::expected` 의 **코드 크기와 반환 규약**을 실측으로 비교하고, "4바이트면 r0" 가 언제 깨지는지 안다.

## 1. 문제

펌웨어 면접에서 "`-fno-exceptions` 면 에러는 어떻게 넘기나요?" 다음에 오는 질문은 "층이 세 개면요? 어디서 실패했는지 어떻게 알죠? 비용은요?" 이다. `Result` 를 만들 줄 아는 것보다 **에러 타입을 어떻게 설계했는지, 그 값이 레지스터로 돌아가는지**를 답해야 한다.

`CPP-B13` 에서 값 + 에러 + 플래그를 담는 기본 `Result` 와 `and_then` 을 이미 만들었다. 이 레슨은 그 다음 단계다: **비자명 타입, `Result<void,E>`, 에러 계층, `TRY`, 실측 비교.** 예외 자체의 비용 비교는 MCPP-M10 담당이라 여기서 다루지 않는다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">층마다 번역하되, 아래층 컨텍스트는 버리지 않는다</text>
  <rect x="20" y="40" width="210" height="84" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="32" y="62" fill="currentColor" font-weight="bold">1층 BusError (HAL)</text>
  <text x="32" y="84" fill="currentColor" font-family="monospace" font-size="12">AddrNack DataNack ArbLost</text>
  <text x="32" y="102" fill="currentColor" font-family="monospace" font-size="12">Timeout BusStuck</text>
  <rect x="265" y="40" width="210" height="84" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="277" y="62" fill="currentColor" font-weight="bold">2층 DrvError (드라이버)</text>
  <text x="277" y="84" fill="currentColor" font-family="monospace" font-size="12">[15:12] DrvCode</text>
  <text x="277" y="102" fill="currentColor" font-family="monospace" font-size="12">[11:8] BusError [7:0] reg</text>
  <rect x="510" y="40" width="210" height="84" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="522" y="62" fill="currentColor" font-weight="bold">3층 AppError (앱)</text>
  <text x="522" y="84" fill="currentColor" font-family="monospace" font-size="12">AppCode code; u8 sensor;</text>
  <text x="522" y="102" fill="currentColor" font-family="monospace" font-size="12">DrvError why;  // 보존</text>
  <path d="M230 82 L262 82 M256 76 L262 82 L256 88" stroke="currentColor" fill="none"/>
  <path d="M475 82 L507 82 M501 76 L507 82 L501 88" stroke="currentColor" fill="none"/>
  <text x="246" y="146" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">at_reg(reg)</text>
  <text x="491" y="146" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">to_app(sensor)</text>
  <text x="20" y="180" fill="currentColor" font-weight="bold">예: 트랜잭션 #1 (CONFIG 0x01) 에서 BusStuck 가 났을 때</text>
  <g font-family="monospace" font-size="12" fill="currentColor">
    <text x="20" y="204">BusError::BusStuck</text>
    <text x="20" y="224">→ DrvError(Bus, BusStuck, reg=0x01)                 raw = 0x1501</text>
    <text x="20" y="244">→ AppError{BusFatal, sensor=7, why=0x1501}           "무엇을 할까" + "왜"</text>
  </g>
  <text x="20" y="274" fill="currentColor">층 사이 암묵 변환은 없다: BusError → DrvError 는 reg 없이는 만들 수 없어 컴파일 에러.</text>
  <text x="20" y="296" fill="currentColor">그래서 TRY 한 줄이 번역을 강제한다: TRY(bus.write_read(...).map_error(at_reg(reg)));</text>
  <text x="20" y="318" fill="currentColor">앱은 code 로 분기하고(재시도?), 로그에는 why 를 그대로 남긴다.</text>
</svg>
```

### Part A — `Result<T,E>` 를 진짜 타입으로

저장소는 `union { T; E; } + bool` 이다 (`T` 기본 생성 불필요, CPP-B13 버전의 제약 해소). 수명 처리는 코드에 주어져 있다 — 근거는 CPP-M07 참고. 채울 것: `map_error`, `map`(void 반환 → `Result<void,E>`), `and_then`, 그리고 `Result<void,E>` 의 `map_error`, `and_then`.

### Part B — 에러 계층과 `TRY`

`DrvError` 는 **16비트 패킹**(`[15:12]` 코드, `[11:8]` 버스 원인, `[7:0]` 레지스터)이다. `at_reg`, `classify`(드라이버 → 앱 분류표), `to_app`, 그리고 `TRY` 매크로를 쓴다. `TRY` 는 `Result<void,E>` 에도 동작해야 한다.

### Part C — I2C 온도 센서 체인

TMP117 을 단순화한 mock 위에서 `read_reg` → `tmp_read_centi` → `app_read_temp` → `app_read_retry` 를 만든다. 실패 위치 × 버스 에러 전 조합과 고정 시드 퍼즈로 "실패하면 그 뒤로 버스를 건드리지 않는다, 어느 레지스터인지 정확하다" 를 검증한다. 데모로 `[[nodiscard]]` 구멍과 thumbv7em 코드 크기를 측정한다.

## 2. Problem (English)

> **Exception-free error propagation for an I2C sensor driver (`-fno-exceptions -fno-rtti`, C++20).**
>
> 1. Implement `Result<T,E>` (union storage, must support non-trivial and move-only `T`) with `map`, `map_error`, `and_then`, plus a `Result<void,E>` specialization. A `Result` of trivially copyable `T`/`E` must stay trivially copyable, and `sizeof(Result<int16_t, DrvError>) == 4`.
> 2. Design a three-layer error model: `BusError` → `DrvError` (16-bit packed: code, bus cause, register) → `AppError` (action code, sensor id, the original `DrvError`). There must be **no implicit conversion** between layers. Implement a `TRY(expr)` macro that evaluates to the value or early-returns the error from the enclosing function, and works for `Result<void,E>`.
> 3. On a mock I2C bus with fault injection, implement `read_reg`, `write_reg`, `tmp_probe` (ID check), `tmp_set_avg` (validate before any bus access, RMW), `tmp_read_centi` (DRDY check, `0x8000` = invalid, `raw * 100 / 128`), `app_read_temp`, `app_bringup`, and `app_read_retry` (retry only `Busy`; `tries == 0` → `BadConfig`).
>
> Constraints: after a failure there must be **no further bus transactions**, and the error must name the exact register that failed.
>
> Follow-ups: How do you know ignoring the return value is flagged? What does `(void)` do? Is your `Result` returned in a register on Cortex-M? How does this compare to `std::expected`?

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크 문제 회피
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
# 31/80 passed — TODO(A1..A5, B1..B6, C1..C8) 를 채워 80/80 을 만든다
```

관찰용 파일 (이 레슨 폴더):

| 파일 | 하는 일 | 실행 |
|---|---|---|
| `demo_nodiscard.cpp` | `[[nodiscard]]` 12가지 상황 | `bash checks.sh nodiscard` |
| `demo_try.cpp` | `TRY` 의 임시 객체·형제 인자·컴파일 에러 | `bash checks.sh try` |
| `demo_size_chain.cpp` | 같은 체인을 6가지 방식으로, thumbv7em 크기 | `bash checks.sh size` (clang), `bash checks.sh gcc` (Lima VM), `bash checks.sh asm 2` |

## 4. 힌트

<details>
<summary>힌트 1 — map_error / map / and_then 은 모두 "에러면 지나간다"</summary>

```text
map_error: 값이면 값을 새 타입 Result<T,G> 로 옮기고, 에러면 f(err) 로 바꾼다
map:       에러면 그대로 Result<U,E>, 값이면 Result<U,E>(f(val))
and_then:  에러면 그대로 R(Unexpected<E>), 값이면 f(val) 를 그대로 반환 (f 가 이미 Result 를 돌려줌)
```

`map` 의 void 반환은 `if constexpr (std::is_void_v<U>)` 로 갈라 `Result<U,E>()` 를 돌려준다. 에러 전파 경로에서 `f` 를 호출하면 안 된다 — 테스트의 `g_calls` 카운터가 확인한다.

</details>

<details>
<summary>힌트 2 — 패킹은 마스크 먼저</summary>

생성자에서 `(code & 0xF) << 12 | (cause & 0xF) << 8 | reg`. 마스크 없이 시프트하면 범위 밖 값이 다른 필드를 침범한다. 결과는 `uint16_t` 로 명시 캐스트한다 (정수 승격 때문에 시프트 식의 타입은 `int` 다). getter 는 시프트 후 마스크.

</details>

<details>
<summary>힌트 3 — classify 는 분기 두 단계</summary>

`DrvCode::Bus` 면 다시 `e.cause()` 로 분기: AddrNack/Timeout → `SensorOffline`, DataNack/ArbLost → `Busy`, BusStuck → `BusFatal`. 나머지 코드는 표대로. `switch` 에 `default:` 를 쓰지 말고 모든 enum 값을 나열하면 enum 이 늘었을 때 `-Wswitch` 가 알려 준다.

</details>

<details>
<summary>힌트 4 — TRY 는 값을 "남겨야" 한다</summary>

statement expression `({ ...; 마지막식; })` 의 값은 마지막 식이다. 먼저 `auto&& try_r_ = (__VA_ARGS__);` 로 임시를 수명 연장해 받고, 실패면 `return Unexpected{try_r_.error()};`, 성공이면 `std::forward<decltype(try_r_)>(try_r_).value();` 로 끝낸다. `Result<void,E>` 에서는 `value()` 가 `void` 라 전체 식도 void 가 되어 그대로 동작한다.

</details>

<details>
<summary>힌트 5 — 드라이버 함수 순서</summary>

`read_reg`: `tx[1]={reg}`, `rx[2]`, `TRY(write_read(...).map_error(at_reg(reg)))`, 조립 `rx[0]<<8 | rx[1]`. `tmp_set_avg`: **검증 먼저**(버스 접근 0회 테스트), 그다음 읽고 수정하고 쓰기. `tmp_read_centi`: `CONFIG` → DRDY 확인 → `TEMP` → `0x8000` 확인 → 변환. `app_read_temp` 는 각 드라이버 호출에 `.map_error(to_app(sensor))`.

</details>

## 5. 풀이

### Part A — 왜 trivial/non-trivial 저장소를 나누나

`union` 멤버가 비자명하면 `Result` 의 특수 멤버가 자동으로 삭제되므로 손으로 써야 한다. 그 방식으로 C++20 의 우아한 코드 **`requires kTrivial = default` 쌍**(P0848, 조건부 trivial 특수 멤버)을 쓰고 싶어진다. 나도 그렇게 썼고, `static_assert(is_trivially_copyable_v<...>)` 도 통과했다. 그런데 thumbv7em 로 컴파일해 보니 clang 에서 r0 에 안 들어갔다:

```text
fr1 (Res<int16_t, E16> 를 반환 — 복사 생성자가 requires 쌍일 때) clang++ thumbv7em -Os, 실제 출력:
    movs  r2, #9
    cmp   r1, #0            ; r1 = x  ← 첫 인자가 r1! r0 은 숨은 sret 포인터
    ...
    strh  r2, [r0]          ; 메모리에 저장
    strb  r1, [r0, #2]
    bx    lr
```

원인을 잘라 보면(매크로로 특수 멤버를 하나씩 켜서 빌드):

| 특수 멤버 | clang thumbv7em `-Os` 반환 |
|---|---|
| 선언 없음 | r0 |
| 복사 생성자 `requires C = default` + `requires(!C) {...}` 쌍 | **메모리** |
| 이동 생성자 쌍만 | r0 |
| 대입 쌍만 / 소멸자 쌍만 | r0 |

**같은 소스를 `arm-none-eabi-g++` 로 빌드하면 r0 로 돌아온다** (`bfi r0, r3, #0, #16` — 아래 asm). 그래서 clang 쪽 분류 문제로 보이지만, 표준 위반인지 clang 버그인지는 **미확인**이다 (LLVM 쪽에서 이 증상을 설명하는 이슈를 찾지 못했다. P0848 구현 커밋은 `canPassInRegisters` 가 ineligible 멤버를 고려하도록 고쳤다고 적었는데 실측은 위와 같다).

해결: `T`, `E` 가 trivial 일 때 쓰는 저장소는 **특수 멤버를 아예 선언하지 않는다.** 그래서 C++17 시절 방식인 부분 특수화 `detail::ResultStorage<T, E, true/false>` 로 갈랐다 (tl::expected 같은 C++14 구현에서 흔한 구조).

> **교훈: `is_trivially_copyable_v` 는 레지스터 반환을 보장하지 않는다. asm 을 봐야 안다.** 실제로 `solution.cpp` 를 thumbv7em 로 컴파일해 `tmp_probe(MockI2c&)` 가 r0 로 값을 받는 것을 확인했다 (첫 인자 `movs r1, #0xf` 뒤 `bl` 결과를 r0 에서 바로 쓴다).

`Result<void,E>` 는 값이 없으니 `E err_{}; bool has_ = true;` 로 충분하다. `return {};` 가 "성공" 이다 — `Result<int,E>` 는 기본 생성자가 없어 `return {}` 가 컴파일 에러가 되므로 "값 없는 성공" 실수를 막는다 (`static_assert(!ReturnsBraceEmpty<Result<int,DrvError>>)`).

`Unexpected<E>` 포장지는 `Result<int,int>` 에서 값/에러를 구별하게 해 주고, 변환 생성자 `Result(Unexpected<G>) requires convertible_to<G,E>` 가 `return Unexpected{e};` 를 가능하게 한다.

### Part B — 에러 계층 설계

한 개의 거대한 `enum Err` 는 처음엔 쉽지만 드라이버가 늘면 **값 공간이 전 코드베이스의 전역 변수**가 된다. 층을 나누면:

| 층 | 타입 | 답하는 질문 | 크기 |
|---|---|---|---|
| HAL | `BusError` | 전기적으로 무슨 일이? | 1 B |
| 드라이버 | `DrvError` | 어느 레지스터 접근에서 무엇이? | 2 B |
| 앱 | `AppError` | 그래서 무엇을 할까? (재시도/고장 처리) + 왜 | 4 B |

분류표 (`classify`):

| DrvCode / BusError | AppCode | 앱의 조치 |
|---|---|---|
| Bus / AddrNack, Timeout | SensorOffline | 고장 표시 |
| Bus / DataNack, ArbLost | Busy | 재시도 |
| Bus / BusStuck | BusFatal | 버스 복구(SCL 토글) 또는 리셋 |
| BadChipId | WrongPart | 치명적 (BOM 불일치) |
| NotReady | Busy | 재시도 |
| InvalidData | DataCorrupt | 값 폐기 |
| BadArg | BadConfig | 버그 — 로그 |

**핵심은 앱이 아래층 enum 을 분기 코드에서 직접 쓰지 않는다는 것.** `why` 는 로그로만 간다. 하드웨어를 바꿔도 `classify` 만 고치면 된다.

층 사이 **암묵 변환을 일부러 만들지 않았다.** `BusError` 만으로는 `DrvError` 를 못 만든다(레지스터를 모르니까). 컴파일 타임 증명:

```cpp
static_assert(!std::is_convertible_v<BusError, DrvError>);
static_assert(!std::is_constructible_v<Result<int, DrvError>, Unexpected<BusError>>);
static_assert(!std::is_convertible_v<Result<int, BusError>, Result<int, DrvError>>);
```

`TRY` 안에서 `return Unexpected<BusError>` 가 `Result<_, DrvError>` 로 변환되지 않으므로, **`map_error` 를 빼먹으면 컴파일 에러**다 (`demo_try.cpp`, `SHOW_ERROR=2`, 실제 clang 출력):

```text
demo_try.cpp:66:34: error: no viable conversion from returned value of type 'Unexpected<E1>' to function return type 'Result<int, E2>'
```

나머지 `SHOW_ERROR` 도 실제로 돌린 결과다:

```text
SHOW_ERROR=1  main (int 반환): no viable conversion from returned value of type 'Unexpected<E1>' to function return type 'int'
SHOW_ERROR=3  파일 범위:       statement expression not allowed at file scope
SHOW_ERROR=4  void 함수:       void function 'v' should not return a value [-Wreturn-mismatch]
```

#### `TRY` 는 표준 C++ 이 아니다

`({ ... })` 는 GNU statement expression — clang/gcc 전용 확장이고 MSVC 는 지원하지 않는다. 실제 `-pedantic`:

```text
clang: warning: use of GNU statement expression extension from macro expansion [-Wgnu-statement-expression-from-macro-expansion]
gcc:   warning: ISO C++ forbids braced-groups within expressions [-Wpedantic]
```

`__VA_ARGS__` 로 받은 이유는 `TRY(f<a, b>())` 의 콤마 때문이다. 중첩 `TRY(make_ok(TRY(a) + 1))` 은 동작하지만 `-Wshadow` 를 켜면 `try_r_` 중복 경고가 난다 (`solution.cpp:426 declaration shadows a local variable`, 실제 출력).

**함정 — 형제 인자.** `use2(side("left", 1), TRY(bad()))` 에서 early return 이 일어나면 왼쪽 인자가 이미 평가됐는지 알 수 없다 (C++17 부터 함수 인자 평가는 서로 비결정적 순서). GCC 문서도 "둘러싼 식의 다른 부분이 평가됐는지는 지정되지 않는다"고 적고 있다. 실제 출력(맥 clang 21, Lima `g++ 15` 모두 동일):

```text
siblings (left, TRY):
  eval left
siblings (TRY, right):
temp Guard + TRY:
  ctor temp
  dtor temp
local Guard + TRY:
  ctor local
  dtor local
```

이 두 컴파일러에서는 왼쪽부터 평가했고, 먼저 만든 임시 객체(`Guard("temp")`)도 early return 때 정상 소멸했다. **순서는 믿지 말고, 부수효과 있는 인자를 `TRY` 옆에 두지 말 것** — 값을 변수로 먼저 받는다.

표준만 쓰려면 `TRY_ASSIGN(const int x, expr);` 두 문장 매크로 (연습 1).

### Part C — I2C 드라이버 체인

```text
app_read_temp(bus, 7)
  ├─ tmp_probe       read_reg(0x0F)      #0  DEVICE_ID, 하위 12비트 == 0x117 ?
  │                  ID 불일치 → DrvError::at(BadChipId, 0x0F)
  └─ tmp_read_centi  read_reg(0x01)      #1  DRDY? 아니면 NotReady@0x01 (TEMP 안 읽음)
                     read_reg(0x00)      #2  0x8000 이면 InvalidData@0x00
                     raw * 100 / 128         (음수는 0 방향 절삭)
```

테스트의 핵심은 **표 전체**다: 실패 트랜잭션 `at ∈ {0,1,2}` × 버스 에러 5종 = 15케이스에서 전부 `AppError{기대 분류, 7, DrvError::bus(그 에러, 그 레지스터)}` 이면서 `bus.txn_count == at + 1` (실패 뒤 버스 접근 없음). 고정 시드 xorshift32 퍼즈 2000회가 같은 불변식을 재확인한다.

재시도는 `is_retryable` (Busy 만) 일 때만 하고, `WrongPart` 는 1회에 끝난다. 재시도 루프는 `Result` 복사(`return r;`)를 쓰는데, trivial 이라 레지스터 복사 비용뿐이다.

> mock 의 근거: TI TMP117 자료 기준 `Temp_Result`(00h) 리셋값 8000h, `Device_ID`(0Fh) 리셋값 0117h, `Configuration`(01h) 기본값 0220h 는 확인했다. Data_Ready 비트 위치(bit 13)와 AVG 필드([6:5])는 **TMP116 용 Zephyr 헤더로만 확인 — TMP117 데이터시트 표로는 미확인**이다. 실제 칩의 clear-on-read 동작은 mock 에 넣지 않았다 (mock 은 CONFIG 를 읽어도 DRDY 가 유지되도록 단순화).

### `[[nodiscard]]` — 실제로 무엇을 잡나 (`bash checks.sh nodiscard`, Apple clang 21)

```text
CASE  diag      메시지 (첫 줄)
1     경고    ignoring return value of type 'Res<int>' declared with 'nodiscard' attribute [-Wunused-value]
2     없음    -
3     없음    -
4     경고    ignoring return value of type 'Res<int>' declared with 'nodiscard' attribute [-Wunused-value]
5     없음    -
6     없음    -
7     경고    unused variable 'r' [-Wunused-variable]
8     없음    -
9     없음    -
10    경고    ignoring return value of function declared with 'nodiscard' attribute: 센서 고장을 놓친다 — 결과를 확인하라 [-Wunused-result]
11    없음    -
12    경고    ignoring return value of type 'Res<int>' declared with 'nodiscard' attribute [-Wunused-value]
```

| CASE | 코드 | 결과 | 의미 |
|---|---|---|---|
| 1 | `read_sensor();` | 경고 | 기본 |
| 2 | `(void)read_sensor();` | 없음 | 표준이 인정하는 **의도적 무시** ([dcl.attr.nodiscard]) |
| 3 | `std::ignore = read_sensor();` | 없음 | 대입이므로 "사용" |
| 4 | 함수 포인터로 호출 (타입에 nodiscard) | 경고 | **타입에 붙인 쪽이 포인터를 통과한다** |
| 5 | 함수 포인터로 호출 (함수에 nodiscard) | 없음 | 함수 속성은 포인터 타입에 없다 |
| 6 | `Result&` 반환 함수 | 없음 | nodiscard 타입이어도 값 반환이 아니면 해당 없음 |
| 7 | `auto r = read_sensor();` (trivial 소멸자) | 경고 | 사용 안 함 |
| 8 | `auto r = make_withdtor();` (non-trivial 소멸자) | 없음 | 소멸자 부수효과 때문에 "미사용" 이 아니라고 본다 |
| 9 | `int plain_status();` | 없음 | C 스타일의 약점 |
| 11 | `(void)gnu_wur();` (GNU `warn_unused_result`) | 없음 (clang) | GCC 에서는 **경고가 남는다** (아래) |
| 12 | `(read_sensor(), 0);` | 경고 | 콤마 왼쪽은 버려지는 식 |

**실무 함정 3개, 전부 실측:**

1. **clang 에서 타입 nodiscard 는 `-Wunused-value` 그룹이다.** `-Werror=unused-result` 로는 에러가 안 된다 (경고로 남음, 실제 확인). `-Werror=unused-value` 또는 그냥 `-Werror`. GCC 는 `-Wunused-result` 로 나온다 (`g++ 15` 실측: `-Werror=unused-result` 로 에러).
2. **GNU `warn_unused_result` 는 `(void)` 로 안 꺼진다.** `g++`/`arm-none-eabi-g++` 모두 `(void)gnu_wur();` 에 `warning: ignoring return value of 'int gnu_wur()' declared with attribute 'warn_unused_result' [-Wunused-result]` 를 낸다 (clang 은 침묵). 벤더 HAL 헤더에 이 속성이 있으면 `(void)` 무시 습관이 GCC 에서 깨진다.
3. **`g++ -fsyntax-only` 로는 이 GCC 경고를 볼 수 없다.** `warn_unused_result` 경고가 나오지 않아서 처음에 "(void) 로 꺼진다" 고 잘못 볼 뻔했다. `-c -o /dev/null` 로 실제 컴파일해야 나온다. (`[[nodiscard]]` 는 `-fsyntax-only` 에서도 나온다.)

**`[[nodiscard]]` 는 "권고" 일 뿐이다.** 표준은 경고를 권장(recommended practice)할 뿐이고, `(void)` 와 `std::ignore` 로 쉽게 우회된다. 강제하려면 ① clang-tidy `bugprone-unused-return-value`, ② 연습 2의 **런타임 must-check** (LLVM `Error` 방식, 디버그 빌드 전용), ③ 코드 리뷰로 `(void)` 를 grep — 의도적 무시는 `r.ignore()` 처럼 이름을 남기게 한다.

### 코드 크기: 에러 코드 vs Result vs `std::expected`

`demo_size_chain.cpp` 는 같은 체인(`read_reg` → `probe`, `read_centi` → `app_tick`)을 방식만 바꿔 만든다. 전부 같은 정보를 전달한다(패킹된 16비트 에러, `-DVARIANT=1..6`). V3·V5 는 `-std=c++23` 으로 clang + libstdc++ 14.2.1 헤더(recipe b)와 `arm-none-eabi-g++ 14.2.1` 양쪽에서 빌드됐다. 실제 출력:

```text
--- clang++ thumbv7em-none-eabihf -Os (.text bytes)
V   variant                  text   read_reg probe read_centi app_tick
1   C code + out-param        240    60  44  76  28
2   Result<i16,E16>           254    64  40  82  36
3   expected<i16,E16>         306    68  56  98  52     (C++23, libstdc++ 14.2.1)
4   Result<i16,EWide>         316    70  64  96  54     (8바이트)
5   expected<i16,EWide>       312    70  60  96  54
6   Result+constrained copy   316    68  70  98  48     (같은 Result 인데 requires 쌍)

--- clang, -Os, 중간 층 static (인라인 허용: -DINLINE_ALL)
1 C 208 | 2 Result 184 | 3 expected 204 | 4 Result-wide 202 | 5 expected-wide 202 | 6 constrained 204

--- arm-none-eabi-g++ 14.2.1 -Os, 링크된 ELF text (Wl,--gc-sections, _start 포함)
1 C 172 | 2 Result 276 | 3 expected 270 | 4 Result-wide 282 | 5 expected-wide 276 | 6 constrained 276
--- GCC -Os -flto
1 C 152 | 2 Result 248 | 3 expected 242 | 4 Result-wide 262 | 5 expected-wide 254 | 6 constrained 248
--- GCC -O2
1 C 192 | 2 Result 308 | 3 expected 304 | 4 Result-wide 320 | 5 expected-wide 292 | 6 constrained 308
--- GCC -Os, 인라인 허용
1 C 132 | 2 Result 184 | 3 expected 184 | 4 Result-wide 220 | 5 expected-wide 204 | 6 constrained 184
```

**"Result 는 공짜" 는 맞지 않다 — 컴파일러에 따라 다르다.** 정리:

| 관찰 | 숫자 | 해석 |
|---|---|---|
| clang, 4바이트 Result vs C | 240 → 254 (+6%) | 거의 같다. `Result` 가 r0 로 돌아오고 out-포인터 스택 슬롯이 사라져 `probe` 는 오히려 44 → 40 |
| clang, 인라인 허용 | C 208 vs Result 184 | **Result 가 더 작다** |
| clang, 4바이트 → 8바이트 Result | 254 → 316 (+24%) | **r0 에 안 들어가면 숨은 포인터 + `strb [r4,#4]` 저장** |
| clang, `expected<i16,E16>` | 306 | 4바이트인데 **메모리 반환** — 아래 |
| clang, Result + requires 쌍(V6) | 316 | 위 Part A 의 함정: 같은 기능인데 62바이트 증가 |
| GCC, C vs 4바이트 Result (-Os, 링크) | 172 → 276 (**+60%**) | 불리. GCC 는 r0 로 돌려주긴 하지만 스택에 썼다가 `bfi` 로 조립한다 |
| GCC, `-O2`, `-flto`, 인라인 | Result 가 계속 큼 (184 vs 132 까지 좁혀짐) | C 스타일이 가장 작다 |
| `std::expected` `.value()` (GCC 링크) | V3 와 동일 270 B, abort/throw 심볼 0개 | **`has_value()` 뒤의 `.value()` 는 최적화로 사라진다.** 검사 없는 `.value()` 는 `abort` (CPP-B13) |

왜 r0 인지가 asm 에 그대로 보인다 (`clang -Os`, 실제 출력 발췌):

```text
; V1 C 스타일 probe()                     ; V2 Result probe()
push {r7, lr}                              push {r7, lr}
sub  sp, #8                                movs r0, #0xf
add.w r1, sp, #6      ; &id               bl   read_reg          ; r0 = Result 전체!
movs r0, #0xf                              mov  r1, r0
bl   read_reg                              bfc  r1, #12, #20     ; id 하위 12비트
ldrh.w r1, [sp, #6]   ; 스택에서 다시 읽기  movw r2, #0x117
...                                        ...
                                           cmp.w r0, #0x10000    ; has_ 바이트 확인

; V3 expected probe() — 4바이트인데 메모리 반환
push {r4, lr}
sub  sp, #8
mov  r4, r0           ; r0 = 숨은 sret 포인터 (호출자가 만든 자리)
add  r0, sp, #4       ; 내가 부를 read_reg 의 sret 슬롯
movs r1, #0xf         ; 인자는 r1 로 밀림
bl   read_reg
ldrb.w r1, [sp, #6]   ; 스택에서 has_value 다시 읽기
```

```svg
<svg viewBox="0 0 740 240" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">반환 규약 (AAPCS32): 합성 타입 ≤ 4바이트면 r0, 그보다 크면 호출자가 준 메모리</text>
  <g stroke="currentColor">
    <rect x="20" y="42" width="130" height="34" class="f-acc"/>
    <rect x="150" y="42" width="65" height="34" class="f-warn"/>
    <rect x="215" y="42" width="65" height="34" class="f-mut"/>
  </g>
  <text x="85" y="64" fill="currentColor" text-anchor="middle">val_ / err_ (2B)</text>
  <text x="182" y="64" fill="currentColor" text-anchor="middle">has_</text>
  <text x="247" y="64" fill="currentColor" text-anchor="middle">pad</text>
  <text x="300" y="56" fill="currentColor">Result&lt;i16, DrvError&gt; = 4 B, trivial → r0 하나</text>
  <text x="300" y="74" fill="currentColor">bl 뒤 곧바로 r0 를 검사 (스택 쓰기 없음)</text>
  <g stroke="currentColor">
    <rect x="20" y="104" width="130" height="34" class="f-acc"/>
    <rect x="150" y="104" width="65" height="34" class="f-warn"/>
    <rect x="215" y="104" width="65" height="34" class="f-mut"/>
    <rect x="280" y="104" width="65" height="34" class="f-mut"/>
  </g>
  <text x="85" y="126" fill="currentColor" text-anchor="middle">val_/err_ (4B)</text>
  <text x="182" y="126" fill="currentColor" text-anchor="middle">has_</text>
  <text x="247" y="126" fill="currentColor" text-anchor="middle">pad</text>
  <text x="312" y="126" fill="currentColor" text-anchor="middle">pad</text>
  <text x="365" y="118" fill="currentColor">Result&lt;i16, Wide&gt; = 8 B → 숨은 포인터</text>
  <text x="365" y="136" fill="currentColor">r0 = 자리 주소, 인자는 r1 부터. 읽기 전에 저장·재적재</text>
  <text x="20" y="172" fill="currentColor" font-weight="bold">크기가 전부가 아니다 — 같은 4바이트여도:</text>
  <text x="20" y="194" fill="currentColor">std::expected (clang): 복사 생성자 requires 쌍 때문에 메모리. is_trivially_copyable 은 대입 때문에 false.</text>
  <text x="20" y="214" fill="currentColor">내 Result (solution.cpp): trivial 저장소는 특수 멤버 선언 없음 → clang·GCC 모두 r0.</text>
  <text x="20" y="232" fill="currentColor">→ 크기는 static_assert 로, 반환 규약은 asm 으로 확인한다.</text>
</svg>
```

`std::expected` 도 표준상 복사/이동 생성자는 `T`,`E` 가 trivial 이면 trivial 이다 — 하지만 **복사 대입 연산자는 trivial 이 아니라** `is_trivially_copyable_v` 는 false (libc++, libstdc++ 모두 실측: `is_trivially_copy_ctor=1, is_trivially_copyable=0`). "호출 규약상 trivial" 은 복사/이동 생성자·소멸자만 보므로 GCC 는 r0 로 돌려준다 (`fr2` 실측). clang 은 libstdc++ 의 복사 생성자 쌍 구조(`expected(const expected&) = default` + `requires` 비자명 버전)에서 메모리 반환이었다 — 위 V6 와 같은 패턴.

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution
# 80/80 passed
c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer solution.cpp -o sol_asan && ./sol_asan
# 80/80 passed, ASan/UBSan 경고 없음
```

`solution.cpp` 와 `practice.cpp` 는 Lima VM 의 `g++ 15.2 -std=c++20 -Wall -Wextra -Werror` 로도 빌드·통과를 확인했다 (80/80, 23/23).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `return e;` 로 에러를 그냥 반환 | `Result(T)` 로 갈 수 있으면 **값으로 처리**된다 (`T==E` 면 특히) | `return Unexpected{e};` |
| `return x;` 에서 `int32_t x` → `Result<int16_t,E>` | 암묵 축소 — `-Wall -Wextra` 로는 경고 없음 (실측). `-Wconversion` 에서만 `implicit conversion loses integer precision` | 범위 검사 후 `static_cast`, 또는 `-Wconversion` |
| `map_error` 안 쓰고 층 건너뛰기 | `Unexpected<BusError>` → `Result<_,DrvError>` 변환 없음, 컴파일 에러 — **이것이 원하는 동작** | `TRY(x.map_error(at_reg(reg)))` |
| 컨텍스트 없는 변환 생성자 `DrvError(BusError)` 추가 | 컴파일은 되지만 reg 를 잃어 디버그 불가 | mapper 로 reg 를 캡처 |
| `TRY` 를 lambda 안에서 사용 | `return` 이 lambda 에서 나간다 — 바깥 함수는 계속 실행 | lambda 밖으로, 또는 `and_then` |
| `foo(side(), TRY(x))` | 인자 평가 순서 미지정, early return 시 `side()` 실행 여부 불명 | `auto v = TRY(x); foo(side(), v);` |
| `-Werror=unused-result` 만 설정 | clang 은 타입 nodiscard 를 `-Wunused-value` 로 낸다 (실측) | `-Werror` 또는 `-Werror=unused-value` 도 |
| 함수에만 `[[nodiscard]]` | 함수 포인터 호출에서 사라진다 (CASE 5) | 클래스에 `[[nodiscard]]` |
| `Result&` 반환 | nodiscard 타입이어도 경고 없음 (CASE 6) | 값으로 반환 |
| 4바이트 넘는 에러 타입 | r0 에 안 들어가 코드가 커진다 (254 → 316) | 패킹, 또는 비용을 알고 수용 |
| "trivially copyable 이니 r0" 라고 단정 | V6: 트레이트는 true, clang 은 메모리 반환 | 대상 컴파일러의 asm 확인 |

## 8. 면접 Q&A

<details>
<summary>Q1. 예외를 끈 펌웨어에서 에러는 어떻게 전달합니까? Result 의 비용은요?</summary>

값과 에러를 한 타입에 담아 반환한다 (`Result<T,E>`, C++23 `std::expected`). 비용을 잴 때는 크기를 맞춘 Cortex-M 빌드로 본다. 내 측정: clang `-Os` 에서 `Result<int16,E16>` 은 C out-파라미터 대비 +6% (r0 반환이라 스택 슬롯이 사라져 어떤 함수는 더 작음), 인라인을 허용하면 C 보다 작았다. GCC 14 `-Os` 에서는 +60% (r0 로 돌려주지만 스택을 거쳐 조립). 꼬리질문 "무조건 Result 가 더 좋나?" → 안전성은 좋지만 크기는 컴파일러마다 다르고, 숫자로 확인해야 한다.

</details>

<details>
<summary>Q2. 에러 enum 을 어떻게 설계하시겠습니까? 드라이버 수십 개면요?</summary>

전역 enum 하나는 피하고 층별 타입으로 나눈다: `BusError`(HAL) → `DrvError`(드라이버) → `AppError`(앱). 각 층은 "그 층의 질문" 에 답한다. 층 경계에서 `map_error` 로 번역하되 아래층 값은 `why` 로 보존해 로그에 남긴다. 앱은 `AppCode` 로만 분기하고 `why` 로 분기하지 않는다 — 분기하면 하드웨어 교체 때 앱이 깨진다. 암묵 변환을 금지하면 번역 누락이 컴파일 에러가 된다.

</details>

<details>
<summary>Q3. `[[nodiscard]]` 로 에러 무시를 막을 수 있나요? 우회법은?</summary>

막지 못한다, 권고일 뿐이다. `(void)`(표준 허용), `std::ignore =`, `Result&` 반환(경고 없음), `auto r = f();` 후 미사용(소멸자가 non-trivial 이면 `-Wunused-variable` 도 안 남 — 실측)이 빠져나간다. 클래스에 붙이면 함수 포인터까지 커버되지만, 함수에 붙이면 안 된다. 강제하려면 clang-tidy, LLVM `Error` 방식의 런타임 must-check, 또는 리뷰 규칙. 꼬리질문 "`__attribute__((warn_unused_result))` 는?" → GCC 에서는 `(void)` 로 꺼지지 않는다(실측), clang 은 꺼진다.

</details>

<details>
<summary>Q4. Result&lt;T,E&gt; 가 r0 로 반환되려면 어떤 조건이 필요합니까?</summary>

AAPCS32: 합성 타입 ≤ 4바이트면 r0, 초과면 호출자가 준 메모리(숨은 포인터). 추가로 C++ 에서 "호출 규약상 trivial" 이어야 한다 (비자명 복사/이동/소멸자가 있으면 메모리로). 함정: `is_trivially_copyable_v` true 여도 복사 생성자를 `requires` 로 조건부 trivial 로 만든 경우 clang 21 에서 메모리 반환이었다. GCC 는 r0. 그래서 trivial 저장소에는 특수 멤버를 선언하지 않는 쪽으로 갔고, 크기는 `static_assert`, 규약은 asm 으로 확인했다. AArch64 는 16바이트까지 `x0/x1`.

</details>

<details>
<summary>Q5. std::expected 를 쓰지 왜 직접 구현합니까?</summary>

C++23 이 되면 `std::expected` 를 쓰는 것이 기본이다 (`-fno-exceptions` 에서도 쓸 수 있고, `-std=c++23` 필요). 다만 C++20 툴체인이거나, ABI·크기를 통제해야 하면 직접 구현한다. 측정: clang+libstdc++ 에서 `expected<i16,E16>` 은 메모리 반환이라 306 B (내 Result 254 B), GCC 에서는 `expected` 270 vs `Result` 276 으로 차이 없음. API 차이: `expected` 는 `.value()` 가 `-fno-exceptions` 에서 abort — 검사 뒤라면 최적화로 사라진다(실측 0 심볼). 또 `is_trivially_copyable` 이 false 다 (대입 때문).

</details>

<details>
<summary>Q6. TRY 매크로의 함정은?</summary>

① GNU 확장 — MSVC 불가, `-pedantic` 경고. ② `return` 하므로 lambda·void 함수·파일 범위에서 못 쓴다. ③ 형제 인자: early return 시 다른 인자의 평가 여부 불명. ④ 중첩 시 `-Wshadow`. 대안은 `TRY_ASSIGN` 두 문장 매크로, 또는 `and_then` 체인. 꼬리질문 "임시 객체는 정리되나?" → 내 실측에서 clang·GCC 모두 early return 때 소멸자를 불렀다.

</details>

<details>
<summary>Q7. 에러를 어디까지 전파하고 어디서 처리합니까?</summary>

처리할 수 있는 층까지. 드라이버는 재시도 정책을 모르므로 올리기만 하고, 재시도/저하 모드/고장 표시는 앱이 판단한다 (`is_retryable`). ISR 에서는 결과를 저장만 하고 태스크로 넘긴다. 현장 분석을 위해 `why` 를 32비트 코드로 패킹해 플래시 로그에 남긴다 (연습 4). 꼬리질문 "재시도 횟수 제한은?" → 무한 재시도는 WCET 를 깨뜨린다. 시도 수를 인자로, 실패 시 마지막 에러를 반환.

</details>

## 9. 추가 연습

### 연습 1 — 표준 C++ 만으로: `TRY_ASSIGN`

statement expression 없이 `TRY_ASSIGN(const int x, expr);` 를 두 문장으로 펼쳐라. 임시 변수 이름은 `__LINE__` 으로 유일하게.

<details>
<summary>답 보기</summary>

`auto try_tmp_LINE = (expr); if (!...) return Unexpected{...error()}; decl = ...value()`. 표준 C++ 이고 MSVC 에서도 되지만, 식 안에는 못 쓴다 (`int v = TRY_ASSIGN(...)` 불가). 같은 줄에 두 번 쓰면 이름이 충돌한다 — `__COUNTER__` 대안이 있지만 비표준이다.

</details>

### 연습 2 — must-check Status (LLVM `Error` 방식)

`[[nodiscard]]` 가 못 잡는 "받고 검사 안 함" 을 소멸자에서 잡는 `Status<true>` 를 만들어라. 이동하면 책임도 옮겨야 한다. 릴리스용 `Status<false>` 는 trivial 소멸자여야 한다.

<details>
<summary>답 보기</summary>

`checked_` 플래그를 `ok()/error()/ignore()` 에서 켜고, `~Status() requires kCheck` 에서 미검사면 카운트. 이동 생성자에서 원본을 `checked_ = true` 로 만들어 이중 집계를 막는다. 소멸자 때문에 디버그 빌드에서는 호출 규약이 바뀐다 (r0 → 메모리) — 그래서 LLVM 도 빌드 옵션으로 켜고 끈다. `(void)s3` 는 경고를 없앨 뿐 소멸자가 잡는다.

</details>

### 연습 3 — `and_then` 파이프라인 = TRY 체인

`pipe_monadic(a) = a.and_then(half).and_then(clamp)` 과 `pipe_try` 가 모든 입력에서 같음을 표로 검증하라.

<details>
<summary>답 보기</summary>

의미는 같다. `TRY` 는 중간 값을 이름으로 받기 쉽고, `and_then` 은 매크로가 없다. 팀 규칙에 따르면 된다. `and_then` 인자는 lambda 라 안에서 `TRY` 를 쓰면 lambda 에서만 return 된다 (실수 표 참고).

</details>

### 연습 4 — 플래시 로그용 32비트 코드

`[31:24] AppCode, [23:16] sensor, [15:12] DrvCode, [11:8] BusError, [7:0] reg` 로 패킹/복원 왕복.

<details>
<summary>답 보기</summary>

시프트 전에 `uint32_t` 로 캐스트 — `AppCode` 를 `int` 로 승격한 채 24칸 밀면 값이 크면 부호 비트에 닿는다 (C++20 부터 왼쪽 시프트 결과는 2의 보수로 정의되었지만, 음수 `int` 가 되는 것은 원하는 바가 아니다). `static_assert(to_log32(...) == 0x0612150F)` 로 컴파일 타임에 고정.

</details>

### 연습 5 — 최선형 초기화: 끝까지 돌고 첫 에러만

`first_error<E>(r1, r2, ...)`: 여러 센서 bring-up 처럼 fail-fast 가 아닌 경우. 앞의 에러를 돌려준다.

<details>
<summary>답 보기</summary>

재귀로 tail 을 구하고 `head.has_value() ? tail : head`. 인자는 호출 전에 모두 평가되므로 "모든 단계 시도" 가 보장된다 — `TRY` 의 정반대 의미. `Result<void,E>` 가 값이 없어 합치기 쉽다.

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice   # 23/23 passed
```

## 10. 핵심 정리

- `Result` 는 `union + bool`, `T`/`E` trivial 이면 특수 멤버를 선언하지 않는 저장소로. `Result<void,E>` 는 `return {};` 가 성공.
- 에러 계층: Bus → Drv → App, 층 사이 **암묵 변환 금지**, `map_error` 로 번역하며 `reg`/`sensor` 를 캡처, 원인은 `why` 로 보존. `DrvError` 16비트 패킹으로 `Result<int16_t,DrvError>` 가 4바이트.
- `TRY` 는 GNU statement expression — MSVC 불가, `-pedantic` 경고. 형제 인자 평가 불명. 표준 대안 `TRY_ASSIGN`.
- `[[nodiscard]]` 는 클래스에. `(void)`/`std::ignore`/참조 반환/변수에 받고 안 쓰기는 빠져나간다. clang 은 타입 nodiscard 를 `-Wunused-value` 로, GCC 는 `-Wunused-result` 로 낸다. GNU `warn_unused_result` 는 GCC 에서 `(void)` 로 안 꺼진다.
- AAPCS32: 4바이트 이하 합성 타입만 r0. clang 은 `requires` 복사 생성자 쌍에서 `is_trivially_copyable` 이어도 메모리 반환 (V6, `std::expected`). asm 으로 확인.
- 실측(-Os): clang C 240 / Result 254 / expected 306, 인라인 허용 시 Result 가 더 작음. GCC 링크 C 172 / Result 276 / expected 270 — **C 가 가장 작다.** 컴파일러별로 재라.
- `std::expected` 의 복사 대입은 trivial 이 아니다(is_trivially_copyable false). 검사 뒤 `.value()` 는 최적화로 사라진다.

## 참고자료

- C++ 표준 [dcl.attr.nodiscard], [expected], [class.prop], [special] — [cppreference: nodiscard](https://en.cppreference.com/w/cpp/language/attributes/nodiscard), [std::expected](https://en.cppreference.com/w/cpp/utility/expected)
- [AAPCS32 §6.4 Result Return](https://github.com/ARM-software/abi-aa/blob/main/aapcs32/aapcs32.rst) — 합성 타입 ≤ 4바이트 r0 규칙 (B13 참고)
- [P0848 — Conditionally Trivial Special Member Functions](https://wg21.link/P0848), [Clang P0848 구현 커밋(canPassInRegisters 언급)](https://lists.llvm.org/pipermail/cfe-commits/Week-of-Mon-20220822/434368.html)
- [GCC — Statements and Declarations in Expressions](https://gcc.gnu.org/onlinedocs/gcc/Statement-Exprs.html)
- [LLVM Programmer's Manual — Error handling: `Error`, `Expected`](https://llvm.org/docs/ProgrammersManual.html#error-handling)
- [TI TMP117 데이터시트 — 레지스터 맵](https://www.ti.com/document-viewer/TMP117/datasheet/GUID-C9F63613-3223-49D4-A8CC-13E55CCFF6F9), [Zephyr TMP116 헤더](https://codex.ac6.fr/Zephyr/xref/Zephyr-Core-3.7.0/drivers/sensor/ti/tmp116/tmp116.h)
- 데모 실행: `bash checks.sh nodiscard|try|size|gcc|asm <v>`
