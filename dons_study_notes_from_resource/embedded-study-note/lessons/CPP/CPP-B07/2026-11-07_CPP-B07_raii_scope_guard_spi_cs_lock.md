---
id: CPP-B07
time: 50분
updated: 2026-11-07
---
# CPP-B07 RAII 스코프 가드: SPI 칩셀렉트와 락

## 학습 목표

- 자원 두 개(버스 락, CS 핀)를 각각 RAII 가드로 만들고, **선언 순서만으로** `lock → CS low → 전송 → CS high → unlock` 순서를 모든 경로에서 보장할 수 있다.
- "블록을 빠져나가면 자동 객체는 생성의 **역순**으로 소멸한다"([stmt.jump])와 "멤버는 **선언 순서**로 생성·역순 소멸"([class.base.init], [class.dtor])을 근거로 설명할 수 있다.
- 복사 생성/대입을 `= delete` 하는 이유(이중 unlock, 전송 중간 CS high)를 말하고, `static_assert(!std::is_copy_constructible_v<...>)` 로 컴파일타임에 증명할 수 있다.
- 이동(move)을 허용할지 판단할 수 있다 — `std::lock_guard`(이동 불가) vs `std::unique_lock`(이동 가능)의 차이와 비용.
- 락 토큰(`const BusLock&` 매개변수) 패턴으로 "락을 잡은 사람만 버스를 쓴다"를 타입으로 강제할 수 있다.

## 1. 문제

SPI 버스 하나에 EEPROM·IMU·플래시가 같이 매달린 보드는 흔하고, "에러 경로에서 CS를 안 올렸다 / 락을 안 풀었다"는 실제 펌웨어 버그 1순위다 — 면접에서는 "C++로 이걸 어떻게 막겠냐"로 RAII와 객체 수명(lifetime) 이해도를 본다.

RAII 자체(생성자 = 획득, 소멸자 = 해제, early return에서도 해제)는 **CPP-B06**(CriticalSection 가드)에서 다뤘다. 이 레슨은 그 위에 세 가지를 얹는다: ① 자원이 **두 개**일 때 순서, ② 가드를 **복사/이동하면** 생기는 사고, ③ 같은 버스에 **장치가 여럿**일 때의 구조.

SPI 트랜잭션 하나는 항상 이 모양이다. 버스 락(뮤텍스)은 다른 태스크가 같은 버스를 쓰지 못하게 하고, CS(chip select, active-low)는 "지금 말하는 상대는 이 장치"라고 고른다.

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">SPI 트랜잭션 하나: 가드 두 개가 만드는 순서</text>
  <text x="10" y="70" fill="currentColor">버스 락</text>
  <text x="10" y="120" fill="currentColor">CS (PA4)</text>
  <text x="10" y="170" fill="currentColor">MOSI</text>
  <rect x="100" y="52" width="580" height="28" rx="4" class="f-acc"/>
  <text x="330" y="71" fill="currentColor">BusLock lock(bus);  (뮤텍스 보유)</text>
  <path d="M90 105 H150 V130 H610 V105 H700" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="250" y="124" fill="currentColor" font-size="12">ScopedChipSelect cs(...)  low = 선택됨</text>
  <g stroke="currentColor" fill="none">
    <rect x="190" y="155" width="90" height="26"/><rect x="290" y="155" width="90" height="26"/>
    <rect x="390" y="155" width="90" height="26"/><rect x="490" y="155" width="90" height="26"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">
    <text x="235" y="173">0x02</text><text x="335" y="173">addr</text><text x="435" y="173">data</text><text x="535" y="173">...</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="100" y="205">① lock</text><text x="150" y="222">② CS low</text>
    <text x="385" y="205">③ 전송</text>
    <text x="610" y="222">④ CS high</text><text x="680" y="205">⑤ unlock</text>
  </g>
  <text x="10" y="250" fill="currentColor">①② 는 생성자, ④⑤ 는 소멸자. 소멸은 생성의 역순이므로</text>
  <text x="10" y="270" fill="currentColor">lock 을 먼저 선언하기만 하면 ④ → ⑤ 순서가 저절로 보장된다.</text>
</svg>
```

mock 하드웨어: `GpioPort`(ODR/BSRR, C-B01 참고)의 PA4 = 25AA010A EEPROM CS, PA6 = IMU CS. `SpiBus` 는 뮤텍스 흉내(`locked`)와 미리 넣어 둔 MISO 응답을 갖는다. 모든 동작은 **이벤트 로그**(`Lock / Unlock / CsLow / CsHigh / Xfer`)에 쌓이고, mock 은 하드웨어 규칙 위반(이중 락, 안 잡은 락 해제, 락 없이 전송, CS 0개·2개 이상 low인 채 전송)을 `g_log.errors` 로 센다. 테스트는 **로그 전체를 정확히** 비교한다.

### Part A — BusLock 과 ScopedChipSelect

- `BusLock(SpiBus&)`: 생성자에서 `bus_lock`, 소멸자에서 `bus_unlock`. `bus()` 접근자는 주어져 있다.
- `ScopedChipSelect(GpioPort&, pin)`: 생성자에서 CS low(assert), 소멸자에서 CS high(deassert).
- 두 클래스 모두 **복사 생성·복사 대입 = delete**. 이동은 허용할지 스스로 결정하고 근거를 댄다(→ 풀이).
- 클래스 바로 아래에 `static_assert(!std::is_copy_constructible_v<...>)` 등으로 컴파일타임 증명.
- 이미 작성된 `ee_read_status()`(RDSR 명령)가 `Lock, CsLow(4), Xfer(05), Xfer(00), CsHigh(4), Unlock` 로그를 정확히 남겨야 한다.

### Part B — 조기 return 과 CS 프레임 여러 개

`Status ee_write_byte(SpiBus&, uint8_t addr, uint8_t data)` — 25xx EEPROM 한 바이트 쓰기:

1. 락은 **한 번** 잡는다.
2. CS 프레임 ①: `RDSR(0x05)` + 더미 → 상태 바이트에 `SR_WIP` 가 서 있으면 **그 자리에서 `return Status::Busy`** (CS와 락이 둘 다 자동으로 풀려야 함).
3. CS 프레임 ②: `WREN(0x06)` 만. (25xx 는 WREN 뒤 **CS가 올라가야** 쓰기 허용 래치가 선다 — 그래서 자기 프레임이 따로 필요.)
4. CS 프레임 ③: `WRITE(0x02), addr, data`.

### Part C — 같은 버스의 두 장치 + 락 토큰

`Status apply_imu_config(SpiBus&)`: EEPROM 0x10 번지에 저장된 IMU 설정값을 읽어 IMU 에 쓴다. 락 하나 안에서 CS 프레임 세 개(EEPROM 읽기 → IMU `PWR_MGMT0=0x0F` → IMU `ACCEL_CONFIG0=값`). EEPROM 값이 `0xFF`(지워진 상태)면 IMU 는 건드리지 않고 `Status::Blank`.

- 보조 함수 `ee_read(const BusLock& held, addr)`, `imu_write_reg(const BusLock& held, reg, val)` 는 **락을 다시 잡지 않는다**. 대신 "이미 락을 가진 사람만 호출할 수 있다"를 `const BusLock&` 매개변수로 강제한다(락 토큰). 하네스가 `std::is_invocable_v` 로 `ee_read(spi_bus, addr)` 형태 호출이 **불가능**함을 확인한다.
- 어느 순간에도 CS가 두 개 동시에 low 이면 안 된다(mock 이 잡는다).

## 2. Problem (English)

> **Scope guards for a shared SPI bus.**
>
> Several devices share one SPI bus. Every transaction must follow *acquire bus mutex → assert CS (drive low) → transfer → deassert CS (drive high) → release mutex*, on **every** exit path. The firmware is built with `-fno-exceptions -fno-rtti`; no heap.
>
> 1. Implement `class BusLock { explicit BusLock(SpiBus&); ~BusLock(); SpiBus& bus() const; }` and `class ScopedChipSelect { ScopedChipSelect(GpioPort&, unsigned pin); ~ScopedChipSelect(); }`. Neither may be copyable; prove it with `static_assert`. Decide whether they should be movable and justify the decision.
> 2. `Status ee_write_byte(SpiBus&, uint8_t addr, uint8_t data)` for a 25xx EEPROM: one lock, three CS frames (`RDSR`, `WREN`, `WRITE addr data`). If `RDSR` reports write-in-progress, return `Status::Busy` immediately.
> 3. `Status apply_imu_config(SpiBus&)`: under a single lock, read a config byte from the EEPROM and write two IMU registers. Helpers must take `const BusLock&` as a proof that the caller holds the bus and must not re-lock (the mutex is non-recursive).
>
> Example (`ee_write_byte(bus, 0x20, 0xAB)`, device idle):
> `Lock, CsLow(4), Xfer(05), Xfer(00), CsHigh(4), CsLow(4), Xfer(06), CsHigh(4), CsLow(4), Xfer(02), Xfer(20), Xfer(AB), CsHigh(4), Unlock`.
>
> Follow-ups: what breaks if you swap the two guard declarations? What if the guard were copyable? Why `explicit`?

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크 문제 회피
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
# 처음엔 FAIL 이 쭉 나오고 "11/24 passed" — 로그가 비었거나 Xfer 만 찍힌다. 24/24 를 만든다.
```

실패 줄에는 실제 로그가 `got: Lock(0x1) CsLow(0x4) ...` 형태로 같이 찍히므로, 기대 로그와 눈으로 비교하며 고치면 된다.

> 참고: starter 에서도 `!std::is_copy_assignable_v<ScopedChipSelect>` 는 **이미 PASS** 다. 멤버가 참조(`GpioPort&`)라서 컴파일러가 복사 대입을 삭제된 것으로 정의하기 때문([class.copy.assign]). 하지만 **복사 생성은 여전히 가능** — 진짜 위험한 쪽은 이쪽이다.

## 4. 힌트

<details>
<summary>힌트 1 — 가드 본체는 각각 한 줄</summary>

생성자에서 `bus_lock(bus_)` / `gpio_write(port_, pin_, false)`, 소멸자에서 `bus_unlock(bus_)` / `gpio_write(port_, pin_, true)`. CS는 active-low 이므로 assert = **low**.

</details>

<details>
<summary>힌트 2 — 복사 금지 두 줄, 이동은 따로 안 써도 된다</summary>

`T(const T&) = delete; T& operator=(const T&) = delete;`. 복사 생성자를 사용자 선언(user-declared)하면 이동 생성자는 **암시적으로 선언되지 않는다**([class.copy.ctor]/8). 그래서 `std::move(x)` 도 삭제된 복사 생성자로 가서 에러 — 이동 불가가 기본으로 따라온다. `std::lock_guard` 도 표준에 딱 이 두 줄만 있다.

</details>

<details>
<summary>힌트 3 — 순서는 선언 순서로 정한다</summary>

`BusLock lock(bus);` 를 먼저, `ScopedChipSelect cs(...);` 를 다음에. 소멸은 역순이니 CS가 먼저 올라가고 그 다음 락이 풀린다. 코드에 `unlock` 이나 `CS high` 를 직접 쓸 일이 없다.

</details>

<details>
<summary>힌트 4 — CS 프레임 여러 개 = 중괄호 블록 여러 개</summary>

`{ ScopedChipSelect cs(...); ... }` 블록이 끝나면 그 CS만 올라간다. 락은 함수 맨 위에서 한 번만 잡고 함수 끝까지 유지. 블록 안의 `return` 도 바깥 락까지 정리한다.

</details>

<details>
<summary>힌트 5 — 락 토큰</summary>

`ee_read(const BusLock& held, ...)` 안에서는 `held.bus()` 로 버스를 얻는다. 버스에 닿는 유일한 길이 "락 객체"이므로 락 없이 부를 방법이 없다. `BusLock` 생성자가 `explicit` 이 아니면 `ee_read(bus, addr)` 가 **임시 BusLock을 몰래 만들어** 컴파일된다 — 바깥에서 이미 잡고 있었다면 이중 락.

</details>

## 5. 풀이

### Part A — 가드 두 개, 복사는 왜 막고 이동은 왜 안 여나

**구현**은 CPP-B06 의 CriticalSection 과 같은 모양이다: 획득은 생성자, 해제는 소멸자.

```cpp
class [[nodiscard]] BusLock {
public:
    explicit BusLock(SpiBus& bus) : bus_(bus) { bus_lock(bus_); }
    ~BusLock() { bus_unlock(bus_); }
    BusLock(const BusLock&) = delete;
    BusLock& operator=(const BusLock&) = delete;
    SpiBus& bus() const { return bus_; }
private:
    SpiBus& bus_;
};
```

`[[nodiscard]]` 와 `explicit` 은 둘 다 "실수로 만든 임시 객체"를 막는 장치다(7절 표 1·5행, 힌트 5).

**복사가 왜 사고인가.** 소멸자만 쓰고 복사를 막지 않으면 컴파일러가 복사 생성자를 **만들어 준다**(멤버별 복사). 이 동작은 C++11부터 deprecated([depr.impldec])지만 여전히 생성되고, 이번 플래그(`-Wall -Wextra`)로는 경고도 안 나온다 — `-Wdeprecated` 를 따로 켜야 `definition of implicit copy constructor for 'G' is deprecated because it has a user-provided destructor` 가 나온다. 복사된 가드는 **획득은 안 했는데 해제는 한다**:

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti demo_noncopy.cpp -o demo && ./demo
```

```text
[1] NaiveLock 을 값으로 넘김
  lock   (was 0)
  unlock (was 1)
  ...여기는 아직 g 의 스코프 (락이 이미 풀림: g_locked=0)
  unlock (was 0)  <-- 이미 풀린 락을 또 풀었다!
```

| 복사된 가드 | 무슨 일이 생기나 | 실제 결과 |
|---|---|---|
| `BusLock` 복사본 | 복사본 소멸 시 unlock → 원본 스코프는 **락 없이** 계속 전송, 원본 소멸 시 또 unlock | 다른 태스크가 끼어들어 바이트가 섞임. 남이 잡은 뮤텍스를 풀어버릴 수도 있음 |
| `ScopedChipSelect` 복사본 | 복사 생성자는 `gpio_write(low)` 를 안 부르지만 소멸자는 `high` 를 부름 → 전송 **중간에** CS high | 25xx/NOR 플래시는 CS 상승 에지에서 명령을 끝내거나 버린다 → 쓰기 중단·잘못된 주소에 프로그램 |

**특수 멤버 함수가 언제 생기나** — 이걸 외워 두면 "왜 두 줄이면 충분한가"를 바로 답할 수 있다.

| 사용자가 선언한 것 | 복사 생성 | 복사 대입 | 이동 생성 | 이동 대입 |
|---|---|---|---|---|
| 없음 | 암시 | 암시 | 암시 | 암시 |
| 소멸자만 (CPP-B06 의 흔한 실수) | 암시 (deprecated) | 암시 (deprecated) | **선언 안 됨** → `std::move` 해도 복사 | 선언 안 됨 → 복사 |
| 복사 생성 `= delete` 만 | 삭제 | **여전히 암시** (deprecated) | 선언 안 됨 → 오버로드가 삭제된 복사 생성을 고름 → 에러 | 선언 안 됨 |
| 복사 생성 + 복사 대입 `= delete` (이 레슨) | 삭제 | 삭제 | 에러 | 에러 |
| (참고) 참조/const 멤버 | 영향 없음 | **삭제됨** | 영향 없음 | 삭제됨 |

근거: 이동 생성자는 사용자 선언 복사 생성자·복사 대입·이동 대입·소멸자가 **하나도 없을 때만** 암시 선언된다([class.copy.ctor]/8). 그래서 복사 두 줄만 지우면 이동까지 막힌다. 컴파일타임 증명:

```cpp
static_assert(!std::is_copy_constructible_v<BusLock>);
static_assert(!std::is_copy_assignable_v<BusLock>);
static_assert(!std::is_move_constructible_v<BusLock>);   // 복사 삭제 → 이동도 암시 선언 안 됨
```

`demo_noncopy.cpp` 를 `-DSHOW_ERROR` 로 빌드하면 실제로 이렇게 거부된다(Apple clang 21, 요약):

```text
$ c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti -DSHOW_ERROR demo_noncopy.cpp
demo_noncopy.cpp:49:13: error: call to deleted constructor of 'BusLock'
   49 |     BusLock b = a;                           // E1: 복사 생성
demo_noncopy.cpp:51:7: error: overload resolution selected deleted operator '='
   51 |     c = a;                                   // E2: 복사 대입
demo_noncopy.cpp:52:13: error: call to deleted constructor of 'BusLock'
   52 |     BusLock d = std::move(a);                // E3: 이동 (복사 삭제 → 이동 생성자 암시 선언 안 됨)
demo_noncopy.cpp:53:5: error: ignoring temporary of type 'BusLock' declared with 'nodiscard' attribute [-Werror,-Wunused-value]
   53 |     BusLock{bus};                            // E4: 임시 객체 — 즉시 lock+unlock ([[nodiscard]])
demo_noncopy.cpp:55:16: error: parentheses were disambiguated as redundant parentheses around declaration of variable named 'bus' [-Werror,-Wvexing-parse]
   55 |         BusLock(bus);                        // E5: "bus 라는 이름의 BusLock 변수" 선언으로 해석됨
demo_noncopy.cpp:55:17: error: no matching constructor for initialization of 'BusLock'
demo_noncopy.cpp:57:67: error: call to deleted constructor of 'BusLock'
   57 |     struct F { static BusLock make(Bus& x) { BusLock l(x); return l; } };   // E6: 이름 있는 지역 반환(NRVO)은 이동 필요
7 errors generated.
```

E3 메시지가 "deleted **constructor**"(복사 생성자)인 점에 주목 — 이동 생성자가 아예 없으니 오버로드 해석이 복사 생성자를 고른 것이다. E4 는 `[[nodiscard]]` 가 없으면 **아무 경고 없이 컴파일된다**(직접 확인함) — 락을 잡자마자 푸는 한 줄짜리 버그.

**이동을 허용할까? → 이 레슨의 답: 허용하지 않는다.**

| 관점 | 이동 불가 (`lock_guard` 모양, 선택) | 이동 가능 (`unique_lock` 모양, 연습 2) |
|---|---|---|
| 상태 | 항상 "소유 중" 하나 | "소유 중" + **빈(moved-from) 상태** |
| 소멸자 | 무조건 해제 | `if (owns) 해제` — 분기 1개 + 플래그/널 포인터 |
| 멤버 | 참조 OK | 참조는 재지정 불가 → 포인터로 바꿔야 함 |
| 읽는 사람 | "이 블록 = 이 락의 범위" 가 코드 모양 그대로 | 락이 함수 밖으로 나갈 수 있어 범위 추적이 어려움 |
| 오용 | 거의 불가 | 이동 후 원본 사용(use-after-move) 가능 |

스코프 가드의 가치는 "**중괄호가 곧 자원 범위**"라는 데 있다. 이동을 열면 그 성질이 사라지고, CS가 원래 스코프 밖에서 low 로 남는 코드도 쓸 수 있게 된다. 이동이 필요해 보이는 대부분의 경우도 이동 없이 해결된다:

- **팩토리 함수에서 반환**: C++17 보장된 복사 생략(guaranteed copy elision, P0135, [dcl.init]) 덕분에 prvalue 반환 `return BusLock{b};` 은 이동 생성자가 **없어도** 된다(연습 3, demo 의 `[2]`). 단, 이름 있는 지역 변수 `return l;` (NRVO)은 생략이 선택 사항이라 이동/복사 생성자가 필요하다(E6).
- **조건부 락**: `std::optional<BusLock>` + `emplace` (연습 5).
- 진짜로 소유권을 넘겨야 할 때(락을 잡은 채 다른 객체/함수에 넘김, try-lock 결과를 반환)만 별도 타입 `UniqueBusLock` 을 만든다(연습 2). 표준 라이브러리도 정확히 이렇게 나눴다: `std::lock_guard`·`std::scoped_lock` 은 복사/이동 불가, `std::unique_lock` 은 이동 가능 + `owns_lock()`.

### Part B — 소멸 순서가 곧 프로토콜 순서

규칙은 하나다: **블록을 어떤 식으로 빠져나가든(끝 도달, `return`, `break`, `goto`), 그 블록에서 생성된 자동 객체는 생성의 역순으로 소멸한다**([stmt.jump]/2). `ee_write_byte` 의 Busy 경로를 따라가 보면:

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">Busy 경로: 안쪽 블록의 return 이 바깥 락까지 정리한다</text>
  <g font-family="monospace" font-size="12" fill="currentColor">
    <text x="10" y="56">BusLock lock(bus);          // ①</text>
    <text x="10" y="80">{</text>
    <text x="10" y="104">  ScopedChipSelect cs(...); // ②</text>
    <text x="10" y="128">  spi_xfer(bus, RDSR);</text>
    <text x="10" y="152">  if (spi_xfer(..) &amp; WIP)</text>
    <text x="10" y="176">      return Status::Busy;  // ★</text>
    <text x="10" y="200">}</text>
  </g>
  <rect x="5" y="162" width="300" height="20" class="f-warn" opacity=".35"/>
  <text x="380" y="50" fill="currentColor">자동 객체 스택 (위가 최근)</text>
  <rect x="380" y="62" width="160" height="34" class="f-acc"/>
  <text x="392" y="84" fill="currentColor">② cs  (CS low)</text>
  <rect x="380" y="98" width="160" height="34" class="f-ok"/>
  <text x="392" y="120" fill="currentColor">① lock (락 보유)</text>
  <path d="M546 79 H566" stroke="currentColor"/>
  <path d="M546 115 H566" stroke="currentColor"/>
  <text x="572" y="83" fill="currentColor">★ 에서 1번째 소멸</text>
  <text x="572" y="119" fill="currentColor">★ 에서 2번째 소멸</text>
  <g fill="currentColor">
    <text x="380" y="172">★ return 실행 순서:</text>
    <text x="380" y="196">  1. 반환값 Status::Busy 준비</text>
    <text x="380" y="220">  2. ~ScopedChipSelect → CsHigh(4)</text>
    <text x="380" y="244">  3. ~BusLock → Unlock</text>
  </g>
  <text x="10" y="284" fill="currentColor">C 라면 return 앞마다 cs_high(); unlock(); 을 손으로 넣어야 한다.</text>
  <text x="10" y="306" fill="currentColor">return 경로가 하나 늘 때마다 빠뜨릴 기회도 하나 는다.</text>
</svg>
```

로그 비교 결과(테스트 그대로):

| 경로 | 기대 로그 |
|---|---|
| Busy (`RDSR`→`0x01`) | `Lock, CsLow4, X05, X00, CsHigh4, Unlock` |
| 정상 | `Lock, [CsLow4 X05 X00 CsHigh4], [CsLow4 X06 CsHigh4], [CsLow4 X02 X20 XAB CsHigh4], Unlock` |

**반환값은 소멸 전에 계산된다.** `ee_read_status()` 의 `return spi_xfer(bus, 0x00);` 에서 전송은 CS가 아직 low 일 때 일어난다. 표준이 순서를 정해 두었다: 반환값의 복사 초기화 → return 식의 임시 객체 소멸 → 지역 변수 소멸([stmt.return]). 그래서 로그에 `Xfer(00)` 이 `CsHigh(4)` 보다 앞에 있다.

**선언 순서를 뒤집으면?** `ScopedChipSelect cs` 를 `BusLock lock` 보다 먼저 쓰면 로그가 `CsLow, Lock, ..., Unlock, CsHigh` 가 된다(연습 1에서 재현).

| 구간 | 무엇이 잘못되나 |
|---|---|
| `CsLow` → `Lock` 사이 | 다른 태스크가 아직 버스를 쓰는 중일 수 있다. 그 태스크의 CS도 low → **CS 두 개 동시 low** → 두 장치가 MISO 를 동시에 구동(버스 충돌), 상대 트랜잭션 오염 |
| `Unlock` → `CsHigh` 사이 | 락을 넘겨받은 태스크가 자기 CS를 내리고 클럭을 시작 → 우리 장치도 아직 선택된 상태라 그 바이트를 **명령으로 받아먹는다** |

**WREN 은 왜 자기 프레임?** 25xx EEPROM/NOR 플래시는 명령을 CS **상승 에지**에서 확정한다. WREN 바이트 뒤 CS를 올려야 WEL(write enable latch)이 서고, WRITE 도 CS 상승 에지에서 내부 쓰기 사이클이 시작된다. 그래서 CS 프레임 = 중괄호 블록으로 표현하는 것이 프로토콜과 1:1 대응한다.

**C 와 비교 — 비용이 정말 0인가?** `demo_cost.cpp` 에 C식 `goto out_cs` 정리 버전(`write_c`)과 가드 버전(`write_raii`)을 같이 두고 어셈블리를 비교했다(arm64, Apple clang 21, HAL 함수는 extern 이라 인라인 불가):

```bash
c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -S -o - demo_cost.cpp   # 임베디드 설정
c++ -std=c++20 -O2 -S -o - demo_cost.cpp                              # 예외 켬 (비교용)
```

| 빌드 | `write_c` 명령 수 | `write_raii` 명령 수 | `__gcc_except_tab` |
|---|---|---|---|
| `-fno-exceptions` | 40 | **40 (한 줄도 안 다름)** | 없음 |
| 예외 켬 | 40 | 56 (+ landing pad, `__Unwind_Resume`, `___clang_call_terminate`) | 생김 |

`-fno-exceptions` 결과의 두 함수는 레이블 이름(`LBB0_2` / `LBB1_2`)만 빼고 똑같다 — 가드 생성자/소멸자가 모두 인라인되어 `bl _hal_bus_lock ... bl _hal_cs_write ... bl _hal_bus_unlock` 시퀀스가 되고, Busy 분기도 C 버전과 같은 공통 정리 블록으로 합쳐진다. 예외를 켜면 hot path 명령은 같고, "HAL 함수가 throw 하면 CS·락을 풀고 전파"하는 **cold landing pad** 와 예외 테이블이 추가된다(CPP-B13 참고). 기대(직접 실행 안 함): `arm-none-eabi-g++ -O2 -fno-exceptions` 로 Cortex-M(Thumb-2) 용으로 빌드해도 같은 원리로 두 함수가 같은 `push … bl … pop` 시퀀스가 되고, 정리용 `.ARM.extab`/`.gcc_except_table` 엔트리는 생기지 않을 것이다. 예외를 켜면 ARM EHABI 의 `.ARM.exidx`/`.ARM.extab` 에 정리 정보가 붙는다.

### Part C — 한 버스, 두 장치, 락 한 번

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">apply_imu_config(): 락 1개 안에 CS 프레임 3개, 한 번에 CS 하나만 low</text>
  <text x="10" y="64" fill="currentColor">BusLock</text>
  <text x="10" y="112" fill="currentColor">EE CS (PA4)</text>
  <text x="10" y="160" fill="currentColor">IMU CS (PA6)</text>
  <rect x="110" y="46" width="600" height="28" rx="4" class="f-acc"/>
  <text x="330" y="65" fill="currentColor">lock (apply_imu_config 스코프)</text>
  <path d="M100 98 H130 V122 H300 V98 H720" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M100 146 H330 V170 H480 V146 H510 V170 H660 V146 H720" stroke="currentColor" fill="none" stroke-width="2"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="140" y="117">03 10 00</text>
    <text x="340" y="165">4E 0F</text>
    <text x="520" y="165">50 odr</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="135" y="196">ee_read(lock, 0x10)</text>
    <text x="330" y="196">imu_write_reg(lock, ...)</text>
    <text x="515" y="196">imu_write_reg(lock, ...)</text>
  </g>
  <rect x="300" y="88" width="30" height="92" class="f-ok" opacity=".35"/>
  <text x="10" y="224" fill="currentColor">EEPROM 값이 0xFF 면 첫 프레임 뒤 return → IMU 프레임은 아예 없다.</text>
  <text x="10" y="244" fill="currentColor">초록 띠: 한 CS 가 올라간 뒤에야 다음 CS 가 내려간다.</text>
  <text x="10" y="264" fill="currentColor">(블록이 끝나야 다음 블록이 시작되므로 자동 보장)</text>
</svg>
```

**왜 보조 함수가 락을 잡으면 안 되나.** `ee_read()` 안에서 `BusLock` 을 또 만들면, 바깥 `apply_imu_config` 가 이미 잡은 **비재귀 뮤텍스**를 같은 태스크가 다시 잡는 셈이다. FreeRTOS `xSemaphoreCreateMutex()` 뮤텍스라면 `portMAX_DELAY` 로 영원히 블록(데드락). (Zephyr `k_mutex` 는 같은 스레드의 재귀 잠금을 허용하지만, 재귀 락은 "어디서 잡혔는지 모르는 락"을 만들어 설계 냄새로 본다.) mock 은 이 상황을 `errors++` 로 잡는다.

**락 토큰(lock token) 패턴.** 그렇다고 보조 함수가 아무 `SpiBus&` 나 받으면, 락 없이 호출해도 컴파일된다. 그래서 매개변수를 `const BusLock&` 으로 받고 버스는 `held.bus()` 로만 얻는다.

| 시그니처 | 락 없이 호출 | 이중 락 위험 | 비용 |
|---|---|---|---|
| `ee_read(SpiBus&, addr)` (내부에서 lock) | 가능, 안전 | **있음** (바깥에서 잡았으면 데드락) | 함수마다 lock/unlock |
| `ee_read(SpiBus&, addr)` (lock 안 함) | **가능 → 버그** | 없음 | 0 |
| `ee_read(const BusLock&, addr)` (이 레슨) | **컴파일 에러** | 없음 | 참조 1개 (인라인되면 0) |

하네스의 증명:

```cpp
CHECK((!std::is_invocable_v<decltype(&ee_read), SpiBus&, uint8_t>));      // 락 없이 호출 불가
CHECK((std::is_invocable_v<decltype(&ee_read), const BusLock&, uint8_t>));
```

첫 줄이 성립하는 이유가 `explicit` 이다. `BusLock(SpiBus&)` 가 explicit 이 아니면 `SpiBus& → BusLock` 암시 변환이 생겨 `ee_read(bus, addr)` 가 **임시 BusLock 을 만들어** 컴파일된다 — 바깥에서 이미 잡고 있었다면 이중 락이고, 아니어도 "락 범위 = 이 한 줄"이라는 숨은 동작이 된다. (CHECK 인자를 괄호로 한 번 더 감싼 건 템플릿 인자 속 쉼표가 매크로 인자 구분자로 읽히기 때문.)

**락을 얼마나 오래 잡나.** 설정 적용을 한 락 안에서 하면 다른 태스크가 "반쯤 설정된 IMU" 를 볼 일이 없다(원자적 적용). 대신 그동안 같은 버스의 다른 사용자(예: 1 kHz 센서 읽기 태스크)는 기다린다. 25xx WRITE 후 내부 쓰기 사이클(수 ms)을 **락을 잡은 채 폴링하지 않는** 이유도 같다 — Part B 가 Busy 를 즉시 돌려주는 설계.

**ISR 에서는?** `BusLock` 은 블로킹 뮤텍스라 ISR 에서 쓰면 안 된다(FreeRTOS 뮤텍스는 ISR 에서 사용 금지). DMA 완료 ISR 에서 CS를 올리는 설계라면 CS 수명이 스코프와 맞지 않으므로 스코프 가드가 아니라 명시적 상태 머신(또는 이동 가능한 소유 객체)으로 다뤄야 한다(Q&A Q6).

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution   # 24/24 passed
c++ -std=c++20 -g -O1 -fsanitize=address,undefined solution.cpp -o sol_asan && ./sol_asan                  # 24/24, 경고 없음
```

데모 파일:

| 파일 | 빌드 | 보여주는 것 |
|---|---|---|
| `demo_noncopy.cpp` | 위 `-fno-exceptions` 플래그 그대로 실행 / `-DSHOW_ERROR` 추가 | 복사 가능한 가드의 이중 unlock, prvalue 팩토리 / 복사·이동·임시객체·vexing parse·NRVO 컴파일 에러 7개 |
| `demo_cost.cpp` | `c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -S -o - demo_cost.cpp` | C `goto` 정리 vs RAII: 명령 40 = 40 |

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `BusLock{bus};` | 이름 없는 임시 객체 → 그 문장 끝에서 바로 소멸. lock 직후 unlock, 이후 전송은 락 없이 | `BusLock lock{bus};` + 클래스에 `[[nodiscard]]` (C++20부터 생성자 호출에도 적용, P1771) |
| `BusLock(bus);` | 문법상 "`bus` 라는 이름의 `BusLock` 변수 선언"(vexing parse). 기본 생성자가 없으면 에러, 있으면 엉뚱한 객체 | 변수 이름을 반드시 쓴다 |
| `ScopedChipSelect cs` 를 `BusLock` 보다 먼저 선언 | 락 밖에서 CS가 내려가고, 락을 푼 뒤에 CS가 올라간다 | 락 먼저, CS 나중 |
| 클래스 멤버로 묶으며 `ScopedChipSelect cs_; BusLock lock_;` 순서로 선언 | 생성 순서는 **멤버 선언 순서**([class.base.init]). 초기화 목록을 `: lock_(b), cs_(...)` 로 써도 소용없다 (`-Wreorder-ctor` 경고는 나옴) | 멤버 선언을 `BusLock lock_;` 먼저 (연습 1) |
| 소멸자만 쓰고 복사 금지를 빠뜨림 | 암시 복사 생성자가 생겨 이중 unlock / 전송 중 CS high. `-Wall -Wextra` 로는 경고 없음 | 복사 생성·대입 `= delete` + `static_assert` |
| 락 잡은 함수 안에서 `ee_read_status()` 같은 "자기가 락 잡는" 함수 호출 | 비재귀 뮤텍스 이중 획득 → 데드락 | 내부 함수는 `const BusLock&` 토큰을 받고 다시 잠그지 않는다 |
| CS가 low 인 동안 CPOL/CPHA 변경 | SCLK idle 레벨이 바뀌며 가짜 클럭 에지 → 장치가 비트를 하나 먹음 | 모드 복원 가드를 CS 가드보다 **먼저** 선언 (연습 4) |
| "소멸자는 어떤 경우에도 불린다"고 믿음 | `longjmp`, `vTaskDelete()` 로 태스크 삭제, `NVIC_SystemReset()`, `abort()` 에선 안 불린다 | 그 경로는 가드에 기대지 말 것 (Q&A Q4) |

## 8. 면접 Q&A

<details>
<summary>Q1. 스코프 가드는 왜 복사 불가로 만드나요? 이동은요?</summary>

복사본도 소멸자에서 해제를 하므로 해제가 두 번 일어난다(이중 unlock, 전송 중 CS high). 복사 생성·대입을 `= delete` 하면, 사용자 선언 복사 생성자 때문에 이동 생성자도 암시 선언되지 않아([class.copy.ctor]/8) 이동까지 막힌다. 스코프 가드는 "중괄호 = 자원 범위"가 핵심이라 이동을 열지 않는다(`std::lock_guard` 와 같은 선택). 소유권을 넘길 일이 있으면 moved-from 상태와 `owns_lock()` 을 가진 별도 타입(`unique_lock` 모양)을 쓴다. 꼬리질문 "그럼 팩토리에서 반환은?" → C++17 보장된 복사 생략으로 prvalue `return BusLock{b};` 는 이동 생성자 없이 된다. 이름 있는 지역 반환(`return l;`)은 안 된다.

</details>

<details>
<summary>Q2. -fno-exceptions 로 빌드하는데 RAII 가 의미가 있나요?</summary>

있다. RAII 소멸자는 예외 전용 기능이 아니라 **스코프 종료**(끝 도달, `return`, `break`, `continue`, `goto`)마다 실행된다([stmt.jump]). 펌웨어에서 가장 흔한 누수 경로가 에러 코드 `return` 이고, 바로 그걸 막는다. 예외를 끄면 사라지는 건 "throw 로 스택을 되감을 때 소멸자 호출" 하나뿐이고, 대신 landing pad·예외 테이블 비용이 사라진다(이 레슨 측정: 56 → 40 명령, `__gcc_except_tab` 없음 — CPP-B13 참고).

</details>

<details>
<summary>Q3. 소멸 순서는 무엇이 보장하나요? 클래스 멤버일 때는요?</summary>

지역 변수: 블록을 빠져나갈 때 생성의 역순으로 소멸([stmt.jump]/2). 반환값은 지역 변수 소멸 전에 초기화된다([stmt.return]) — 그래서 `return spi_xfer(...)` 는 CS가 low 일 때 실행된다. 멤버: 초기화 목록 순서와 무관하게 **클래스 안 선언 순서**로 생성되고([class.base.init]), 소멸자 본문이 끝난 뒤 역순으로 소멸한다([class.dtor]). 그래서 `SpiTransaction { BusLock lock_; ScopedChipSelect cs_; }` 순서가 중요하다. 전역/static 객체는 생성 완료의 역순으로 프로그램 종료 시 소멸하지만, 펌웨어는 `main` 에서 돌아오지 않으니 보통 의미가 없다(CPP-B09 참고).

</details>

<details>
<summary>Q4. 소멸자가 불리지 않는 경우를 말해보세요.</summary>

① `longjmp` 로 스코프를 건너뛸 때 — 같은 위치를 throw/catch 로 바꿨을 때 non-trivial 소멸자가 호출될 상황이면 **UB**([csetjmp.syn]). ② RTOS 에서 다른 태스크가 `vTaskDelete()` 로 가드를 가진 태스크를 지울 때 — 락이 영원히 잡힌 채 남는다. ③ `abort()`, `std::quick_exit`, 리셋(`NVIC_SystemReset()`), HardFault 무한 루프. ④ 무한 루프로 스코프를 영영 안 나갈 때(태스크 본체의 `for(;;)` 위에 가드를 두면 안 되는 이유). ⑤ `new` 로 만들고 `delete` 안 한 객체(펌웨어는 보통 힙 금지, CPP-B14).

</details>

<details>
<summary>Q5. BusLock 을 ISR 에서 써도 되나요? 크리티컬 섹션 가드와 무엇이 다른가요?</summary>

안 된다. 뮤텍스는 블로킹(대기)하고 소유 태스크 개념(우선순위 상속)이 있는데 ISR 은 태스크가 아니다. FreeRTOS 는 뮤텍스를 ISR 에서 쓰지 말라고 명시한다. CPP-B06 의 CriticalSection(PRIMASK 저장/복원)은 ISR 과 태스크 모두를 막는 대신 **모든 인터럽트를 멈추므로** 수 µs 이내 구간에만 쓴다. SPI 전송처럼 긴 구간은 뮤텍스(태스크끼리만 배제), ISR 과 공유하는 짧은 변수는 크리티컬 섹션 — 용도가 다르다.

</details>

<details>
<summary>Q6. DMA 로 SPI 전송하고 완료 인터럽트에서 CS 를 올려야 하면 이 가드를 어떻게 쓰나요?</summary>

CS의 수명이 함수 스코프보다 길어지므로 스코프 가드가 맞지 않는다. 선택지: ① 호출 태스크가 가드를 가진 채 "DMA 완료 세마포어"를 기다린다 — 가드 범위 = 대기 포함 전체(가장 단순, 태스크는 블록되어 CPU 를 양보). ② 드라이버 객체가 "전송 중" 상태를 소유하고 ISR 이 CS high + 락 해제를 수행하는 명시적 상태 머신 — 이때 "락 소유권을 ISR 쪽으로 넘긴다"는 개념이 필요하므로 이동 가능한 소유 객체나 명시적 API 가 맞다. 단, FreeRTOS 뮤텍스는 ISR 에서 반납할 수 없으므로 ②는 이진 세마포어로 바꿔야 한다.

</details>

<details>
<summary>Q7. Zephyr 나 Linux 에선 CS 를 누가 관리하나요? 그래도 이 패턴이 필요한가요?</summary>

Zephyr `spi_transceive()` 는 `spi_config` 의 `spi_cs_control`(GPIO CS) 로 드라이버가 전송마다 CS를 자동 처리하고, 여러 트랜잭션을 묶으려면 `SPI_HOLD_ON_CS`/`SPI_LOCK_ON` + `spi_release()` 를 쓴다 — 바로 이 "락 유지, CS 프레임 여러 개" 문제를 API 로 푼 것이고, `spi_release()` 짝을 맞추는 데 RAII 가드가 유용하다. Linux 는 `spi_message` 안의 `spi_transfer` 들을 하나의 CS 구간으로 보내고 `cs_change` 로 중간 CS 토글을 지정한다. 메시지 간 직렬화는 SPI 코어가 하고, 여러 메시지 동안 버스를 독점하려면 `spi_bus_lock()`/`spi_bus_unlock()` 을 쓴다. 베어메탈/자체 HAL 에서는 이 레슨처럼 직접 만든다.

</details>

<details>
<summary>Q8. lock_guard, unique_lock, scoped_lock 차이와 비용은?</summary>

`lock_guard<M>`: 생성 시 lock, 소멸 시 unlock, 복사·이동 불가 — 포인터(참조) 1개 크기, 분기 없음. `scoped_lock<M...>`(C++17): 여러 뮤텍스를 데드락 회피 알고리즘으로 한 번에 잡는 lock_guard. `unique_lock<M>`: 이동 가능, `defer_lock`/`try_to_lock`/`adopt_lock`, 중간 `unlock()`, `owns_lock()` — 포인터 + bool, 소멸자에 분기. 조건 변수(`condition_variable::wait`)는 `unique_lock` 을 요구한다. 펌웨어 RTOS 뮤텍스엔 표준 타입 대신 이 레슨처럼 같은 모양의 자체 가드를 만든다.

</details>

## 9. 추가 연습

### 연습 1 — 두 가드를 한 클래스로: 멤버 선언 순서

`SpiTransaction(bus, port, pin)` 클래스를 만들어 `BusLock` 과 `ScopedChipSelect` 를 멤버로 갖게 하라. 일부러 선언 순서만 뒤집은 `BadTransaction` 도 만들고, 각각의 로그를 비교하라.

<details>
<summary>답 보기</summary>

`BusLock lock_; ScopedChipSelect cs_;` 순서로 선언하면 `Lock, CsLow, Xfer, CsHigh, Unlock`. 선언을 `cs_; lock_;` 로 바꾸면 초기화 목록을 어떻게 쓰든 `CsLow, Lock, Xfer, Unlock, CsHigh` — 멤버 생성은 선언 순서, 소멸은 그 역순이기 때문([class.base.init], [class.dtor]). 초기화 목록 순서가 선언과 다르면 clang 이 `-Wreorder-ctor` 로 경고해 준다: `field 'b_' will be initialized after field 'a_'`.

</details>

### 연습 2 — 이동 가능한 UniqueBusLock

`std::unique_lock` 처럼 이동 가능한 락을 만들어라: 이동 생성/대입, `owns_lock()`. 이름 있는 지역 변수를 반환하는 `begin_session()` 이 컴파일되고, 이동 후 원본은 unlock 하지 않으며, 이동 대입은 **자기가 갖고 있던 락부터** 놓아야 한다.

<details>
<summary>답 보기</summary>

멤버를 `SpiBus*` 로 바꾸고 `nullptr` = 빈 상태. 이동 생성: `bus_(std::exchange(o.bus_, nullptr))`. 이동 대입: `if (this != &o) { if (bus_) bus_unlock(*bus_); bus_ = std::exchange(o.bus_, nullptr); }`. 소멸자: `if (bus_) bus_unlock(*bus_);`. 이동 연산은 `noexcept` 로. 비용: 소멸자 분기 1개와 moved-from 상태 — 이것이 Part A 에서 이동을 열지 않은 이유다. 테스트: `p(bus1)`, `q(bus2)`, `p = std::move(q)` → 로그 `Lock1, Lock2, Unlock1, Unlock2`.

</details>

### 연습 3 — 이동 불가 가드를 팩토리로 반환

`BusLock acquire(SpiBus&)` 를 작성하라. `BusLock` 은 이동 생성자가 없다(`static_assert(!std::is_move_constructible_v<BusLock>)`).

<details>
<summary>답 보기</summary>

`return BusLock{b};` — prvalue 반환은 C++17 부터 임시 객체가 **만들어지지 않고** 호출자의 `BusLock l = acquire(bus);` 자리에 바로 생성된다(보장된 복사 생략). 그래서 복사/이동 생성자가 삭제돼 있어도 컴파일된다. `BusLock l(b); return l;` 은 NRVO 가 허용일 뿐 보장이 아니라 이동/복사 생성자가 필요 → 에러(demo E6).

</details>

### 연습 4 — 범용 scope_exit 람다 가드로 SPI 모드 복원

한 버스(mode 0)에 mode 3 장치가 섞여 있다. 락 안에서 `CR1` 에 CPOL|CPHA 를 켜고, 어떤 경로로 나가든 원래 값으로 되돌리는 `ScopeExit` 템플릿을 만들어라. `dismiss()` 로 "성공 시 유지(커밋), 실패 시 롤백" 도 구현하라. CS가 low 인 동안 모드를 바꾸면 mock 이 에러로 센다.

<details>
<summary>답 보기</summary>

`template<class F> class [[nodiscard]] ScopeExit { F f_; bool active_ = true; ~ScopeExit(){ if (active_) f_(); } void dismiss(){ active_ = false; } /* 복사 금지 */ };` + CTAD 로 `ScopeExit restore{[&]{ spi_set_cr1(regs, saved); }};`. 선언 순서 `lock → 모드 변경 → restore 가드 → cs` 로 두면 소멸은 `CS high → 모드 복원 → unlock` 이 되어 CS가 올라간 뒤에 모드가 바뀐다. 로그: `Lock, Mode3, CsLow6, XF5, X00, CsHigh6, Mode0, Unlock`. `std::experimental::scope_exit`(Library Fundamentals TS v3)와 같은 아이디어지만 C++23 까지 표준 라이브러리엔 없다. (실제 STM32 는 CR1 의 CPOL/CPHA 를 `SPE=0` 상태에서 바꿔야 한다 — mock 은 그 단계를 생략.)

</details>

### 연습 5 — 조건부 락: std::optional + emplace

부트 초기화 단계(스케줄러 시작 전)에는 뮤텍스가 필요 없고 쓸 수도 없다. `bus.rtos_running` 일 때만 락을 잡는 `read_who_am_i()` 를 이동 불가 `BusLock` 그대로 작성하라.

<details>
<summary>답 보기</summary>

`std::optional<BusLock> lock; if (bus.rtos_running) lock.emplace(bus);` — `emplace` 는 optional 내부 저장소에 **제자리 생성**하므로 이동/복사가 필요 없다. `optional` 이 스코프를 벗어나면 값이 있을 때만 소멸자가 불린다. `ScopedChipSelect` 를 그 뒤에 선언하면 순서 보장도 그대로다. 비용: bool 1개 + 소멸 시 분기 (힙 없음).

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice   # 22/22 passed
```

## 10. 핵심 정리

- 자원 두 개면 가드 두 개, **획득 순서대로 선언**한다(lock → CS). 해제 순서(CS high → unlock)는 역순 소멸이 공짜로 보장한다([stmt.jump]).
- 멤버로 묶을 때는 **멤버 선언 순서**가 기준이다. 초기화 목록 순서는 무관([class.base.init]).
- `return f();` 의 `f()` 는 지역 가드가 소멸하기 **전에** 실행된다([stmt.return]).
- 가드는 `복사 생성 = delete; 복사 대입 = delete;` 두 줄 → 이동도 자동으로 막힌다. `static_assert(!std::is_copy_constructible_v<T>)` 로 증명.
- 이동은 기본적으로 열지 않는다(`lock_guard` 모양). prvalue 팩토리(보장된 복사 생략)와 `optional::emplace` 로 대부분 해결; 소유권 이전이 필요하면 `unique_lock` 모양 별도 타입.
- `[[nodiscard]]` 클래스 + `explicit` 생성자 → `BusLock{bus};` 임시 객체 버그와 암시 변환 이중 락을 컴파일 단계에서 차단.
- 락 토큰 `const BusLock&` 매개변수 = "락을 가진 사람만 호출" 을 타입으로 강제, 재잠금(데드락) 제거.
- `-fno-exceptions` 에서 RAII 비용 = 0 (C `goto` 정리 버전과 명령 40개 동일). 예외를 켜면 cold landing pad 만 추가.

## 참고자료

- C++ 표준(작업 초안) — [stmt.jump] 스코프 종료 시 역순 소멸, [stmt.return] 반환값 초기화 순서, [class.base.init]·[class.dtor] 멤버 생성/소멸 순서, [class.copy.ctor]/8 이동 생성자 암시 선언 조건, [depr.impldec]: <https://eel.is/c++draft/>
- cppreference — [`std::lock_guard`](https://en.cppreference.com/w/cpp/thread/lock_guard), [`std::unique_lock`](https://en.cppreference.com/w/cpp/thread/unique_lock), [copy elision](https://en.cppreference.com/w/cpp/language/copy_elision), [`[[nodiscard]]`](https://en.cppreference.com/w/cpp/language/attributes/nodiscard), [`std::experimental::scope_exit`](https://en.cppreference.com/w/cpp/experimental/scope_exit)
- P0135R1 *Guaranteed copy elision through simplified value categories* (C++17), P1771R1 *[[nodiscard]] for constructors* (C++20)
- Microchip 25AA010A/25LC010A 데이터시트 — READ/WRITE/WREN/RDSR 명령, WIP/WEL 비트, CS 상승 에지 동작
- TDK InvenSense ICM-42688-P 데이터시트 — SPI 주소 바이트 bit7(R/W), PWR_MGMT0(0x4E), ACCEL_CONFIG0(0x50), WHO_AM_I(0x75)
- FreeRTOS — [Mutexes](https://www.freertos.org/Real-time-embedded-RTOS-mutexes.html) (ISR 사용 금지, 재귀 뮤텍스는 별도 API), Zephyr — [SPI API](https://docs.zephyrproject.org/latest/hardware/peripherals/spi.html) (`spi_cs_control`, `SPI_LOCK_ON`, `spi_release`)
- C++ Core Guidelines — R.1 (RAII), C.21 (rule of five / 특수 멤버 함수는 모두 정의하거나 모두 삭제), CP.20 (`lock()/unlock()` 대신 RAII)
