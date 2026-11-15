---
id: CPP-A08
time: 110분
updated: 2026-11-11
---
# CPP-A08 테스트 가능한 C++ 드라이버 설계: 의존성 주입 vs 링크 심

## 학습 목표

- I2C 센서 드라이버를 **한 번만** 쓰고, 하드웨어와의 이음매(seam)를 세 가지로 바꿔 끼울 수 있다: (1) 템플릿 파라미터 주입 `TempSensor<FakeBus>`, (2) 인터페이스(virtual) 주입 `TempSensor<II2c>`, (3) 링크 타임 대체 `TempSensor<LinkBus>` + `i2c_hw.cpp` / `i2c_fake.cpp`.
- 스크립트 응답·호출 로그·오류 주입(NACK, 타임아웃, CRC, 엉뚱한 ID, 설정 무시)을 가진 **상태 기반 가짜(fake)** 를 만들고, **같은 테스트 묶음** 을 세 이음매에 돌린다 (`TEST_CASE_TEMPLATE`, 진짜 doctest 로도 그대로 컴파일됨을 확인).
- 세 방식의 비용을 **실측 숫자** 로 말한다: Cortex-M4 ELF 의 `.text` (GCC 14.2 `-O2`/`-Os`/LTO, gc-sections), vtable·typeinfo 유무, 호출 지점 asm (`str` 인라인 / `ldr`×3 + `blx` / `bl`).
- 링크 심의 함정을 실제 링커 출력으로 설명한다: 전역 가짜의 테스트 격리, 중복 정의 에러, 정적 라이브러리(.a)가 그 에러를 **조용히 없애는** 것, GNU ld 의 순서 의존성, 툴체인을 섞으면 `int32_t` 맹글링이 달라 링크가 깨지는 것.
- "무엇을 언제" 를 판단 근거(플래시, 테스트 편의성, 컴파일 시간, 레거시 C)와 함께 답한다.

## 1. 문제

"드라이버를 보드 없이 테스트하려면 어떻게 설계하죠? virtual 쓰면 느리지 않나요? 링크 심이면 공짜 아닌가요?" — 시니어 펌웨어 면접의 단골이다. 정답은 하나가 아니라 **비용과 편의의 교환** 이고, 면접관은 "숫자로 아는지" 와 "함정을 밟아 봤는지" 를 본다. 이 레슨은 C 쪽 기법(링크 심·함수 포인터·전처리기 — TEST-B04, CMock 식 기대 큐 — TEST-B05)과 vtable 회계(CPP-M06)는 다시 하지 않고, **C++ 에서 세 이음매를 같은 드라이버·같은 테스트로 비교** 하는 데 집중한다.

용어: Michael Feathers 는 "코드를 고치지 않고 동작을 바꿀 수 있는 곳" 을 이음매(seam)라 부르고 전처리 이음매, 링크 이음매, 객체 이음매로 나눴다 (*Working Effectively with Legacy Code*, 4장). C++ 의 템플릿 파라미터는 여기에 "컴파일 타임 이음매" 를 하나 더 보탠다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">드라이버는 한 번, 이음매는 셋 — 무엇이 언제 정해지나</text>
  <rect x="220" y="36" width="300" height="44" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="236" y="56" fill="currentColor" font-family="monospace">template &lt;I2cBus Bus&gt;</text>
  <text x="236" y="72" fill="currentColor" font-family="monospace">class TempSensor  (본체 1개)</text>
  <path d="M300 80 L120 110 M370 80 L370 110 M440 80 L620 110" stroke="currentColor" fill="none"/>
  <rect x="10" y="110" width="220" height="74" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="20" y="130" fill="currentColor" font-weight="bold">(1) 템플릿 주입</text>
  <text x="20" y="148" fill="currentColor" font-family="monospace" font-size="12">TempSensor&lt;FakeBus&gt;</text>
  <text x="20" y="164" fill="currentColor" font-family="monospace" font-size="12">TempSensor&lt;HwBus&gt;</text>
  <text x="20" y="178" fill="currentColor" font-size="12">컴파일 시점 · 호출 인라인</text>
  <rect x="260" y="110" width="220" height="74" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="270" y="130" fill="currentColor" font-weight="bold">(2) virtual 주입</text>
  <text x="270" y="148" fill="currentColor" font-family="monospace" font-size="12">TempSensor&lt;II2c&gt; + II2c&amp;</text>
  <text x="270" y="164" fill="currentColor" font-family="monospace" font-size="12">FakeBusV / HwBusV</text>
  <text x="270" y="178" fill="currentColor" font-size="12">실행 시점 · vtable 경유 blx</text>
  <rect x="510" y="110" width="220" height="74" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="520" y="130" fill="currentColor" font-weight="bold">(3) 링크 심</text>
  <text x="520" y="148" fill="currentColor" font-family="monospace" font-size="12">TempSensor&lt;LinkBus&gt;</text>
  <text x="520" y="164" fill="currentColor" font-family="monospace" font-size="12">U i2c::write_read</text>
  <text x="520" y="178" fill="currentColor" font-size="12">링크 시점 · 직접 bl</text>
  <g fill="currentColor" font-size="12">
    <text x="10" y="214" font-weight="bold">테스트 빌드</text>
    <text x="10" y="232">FakeBus{FakeTmp&amp;}</text>
    <text x="260" y="214" font-weight="bold">테스트 빌드</text>
    <text x="260" y="232">FakeBusV final : II2c</text>
    <text x="510" y="214" font-weight="bold">테스트 빌드</text>
    <text x="510" y="232">+ i2c_fake.o (전역 FakeTmp)</text>
    <text x="10" y="262" font-weight="bold">제품 빌드</text>
    <text x="10" y="280">HwBus (빈 struct, MMIO 인라인)</text>
    <text x="260" y="262" font-weight="bold">제품 빌드</text>
    <text x="260" y="280">HwBusV (vptr 4 B, vtable 16 B)</text>
    <text x="510" y="262" font-weight="bold">제품 빌드</text>
    <text x="510" y="280">+ i2c_hw.o (같은 이름, 다른 몸통)</text>
  </g>
  <text x="10" y="316" fill="currentColor" font-size="12">세 Rig 이 같은 가짜 장치 FakeTmp 를 쓴다 → 같은 TEST_CASE_TEMPLATE 가 세 번 (34 케이스 / 289 CHECK).</text>
</svg>
```

센서는 가상의 "TS117" 이다: 레지스터 맵은 TI TMP117 류를 본떴고 (TEMP 0x00, CONFIG 0x01, DEVICE_ID 0x0F = 0x0117, 1 LSB = 7.8125 m°C) 각 16비트 워드 뒤에 Sensirion SHT3x 류 CRC-8 (다항식 0x31, 초기값 0xFF) 을 붙였다. **실제 TMP117 에는 CRC 가 없다** — 오류 주입을 한 칩에 모으려고 합친 대표값이며, 데이터시트 원문과 대조하지 않았다. CONFIG 값 0x0220 도 대표값이다.

### Part A — 프로토콜 코어, 가짜 장치, 드라이버 본체

- `crc8(p, n)` (poly 0x31, init 0xFF, 검사값 `{0xBE,0xEF} → 0x92`), `to_milli_c(raw)` (2의 보수 16비트 × 125/16, 정수, 0 쪽 버림).
- `FakeTmp::write_read / write` — 레지스터 파일을 가진 센서 모형: 주소가 다르면 NACK, TEMP 읽기는 스크립트 값을 순서대로, 응답은 `MSB, LSB, CRC`, 쓰기는 CRC 가 맞을 때만 CONFIG 갱신. 모든 트랜잭션을 `Call{op, addr, reg, len, value, result}` 로 기록.
- `TempSensor<Bus>::init / read_milli_c / read_reg16 / write_reg16` — ID 확인 → CONFIG 쓰기 → 다시 읽어 확인. init 전 읽기는 버스에 아무것도 보내지 않고 `NotInit`.

### Part B — 오류 주입과 재시도 정책, 세 이음매 어댑터, 테스트 격리

- 정책: NACK·CRC 오류는 최대 3번 시도(재시도 2번), 타임아웃은 **재시도하지 않음**, ID 가 틀리면 CONFIG 를 **쓰지 않음**, 설정이 안 먹으면 `ConfigMismatch`. 통계(`retries/nacks/timeouts/crc_errors`) 를 센다.
- `FakeBusV final : II2c` — virtual 주입용 가짜. `link_fake_reset()` — 링크 심 가짜는 프로그램에 하나뿐인 전역이라 **테스트마다** 초기화해야 한다.
- 진짜 파일 분리 빌드(`link_seam.sh`): `sensor.o` 는 `i2c::write_read` 를 정의하지 않고 부르기만 하고, 링커가 `i2c_fake.o` 또는 `i2c_hw.o` 를 고른다.

### Part C — 감시 루프 `TempMonitor<Sensor>` (100 ms tick)

- Running: 4점 이동 평균, 과열 알람 히스테리시스 (≥ 85.000 °C 켬, ≤ 80.000 °C 끔).
- 연속 실패 3번 → Faulted (알람 강제 ON = fail-safe), 10 tick 마다 `init()` 재시도, 성공하면 Running. 드라이버가 흡수한 NACK 은 실패가 아니다.
- 가상 시간(tick 횟수)으로 상태·알람·**버스 트랜잭션 수** 를 정확히 검증한다 (Faulted 대기 중 버스는 조용해야 한다).

## 2. Problem (English)

> **Design a host-testable I2C temperature-sensor driver in C++ and compare three seams.**
>
> Write `template <I2cBus Bus> class TempSensor` once (`init()`, `read_milli_c(int32_t&)`, retry policy: NACK/CRC up to 3 attempts, timeout never retried, wrong `DEVICE_ID` → `BadId` without touching `CONFIG`, read-back mismatch → `ConfigMismatch`). Make it testable through:
> 1. **template injection** — `TempSensor<FakeBus>` in tests, `TempSensor<HwBus>` in firmware;
> 2. **interface injection** — `TempSensor<II2c>` holding `II2c&` (pure virtual `write_read`/`write`, protected non-virtual destructor);
> 3. **link-time substitution** — `TempSensor<LinkBus>` calling `i2c::write_read()`, defined either in `i2c_hw.cpp` or `i2c_fake.cpp`.
>
> Run the **same** test suite (doctest-style `TEST_CASE_TEMPLATE`) against all three with a scripted fake (register file, response script, call log, fault injection: NACK, timeout, bad CRC, wrong ID). Then build the firmware for Cortex-M4 three ways and report `.text`, vtable/typeinfo presence and the call-site assembly. Finally write a 100 ms `TempMonitor` (moving average, 85/80 °C hysteresis alarm, Faulted after 3 consecutive failures, re-init every 10 ticks) and verify it with virtual time.
>
> Constraints: `-std=c++20 -fno-exceptions -fno-rtti`, no heap, no real hardware.
>
> Follow-ups: Is template injection always smaller? What exactly does virtual cost on an M4? Why can a link seam silently pick the wrong implementation? How do you inject a different fake per test with a link seam? Strict mocks or fakes?

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
```

처음 실행 결과 (실제 출력 일부 — 케이스 줄 34개는 전부 FAIL, 실패한 CHECK 는 들여쓴 줄):

```text
    FAIL starter.cpp:355  crc8(beef, 2) == 0x92
    FAIL starter.cpp:357  crc8(zero, 2) == 0x81
    FAIL starter.cpp:358  crc8(beef, 0) == 0xFF
    FAIL starter.cpp:359  to_milli_c(0x0C80) == 25'000
    ...
FAIL A: crc8 and temperature conversion
FAIL A: init probes ID, writes CONFIG, reads it back <template>
FAIL A: init probes ID, writes CONFIG, reads it back <virtual>
FAIL A: init probes ID, writes CONFIG, reads it back <link>
...
FAIL C: init fails (wrong chip) -> Faulted until fixed <link>
45/289 passed
```

하네스: doctest 와 **같은 매크로 이름** (`TEST_CASE`, `TEST_CASE_TEMPLATE`, `CHECK`) 만 쓰는 50줄 남짓한 미니 하네스다. 케이스마다 PASS/FAIL 한 줄, 실패한 CHECK 는 파일:줄, 끝에 CHECK 단위 `N/M passed`. `TEST_CASE_TEMPLATE(name, R, RigT, RigV, RigL)` 은 C++20 템플릿 람다 `[]<class U>(){...}` 로 타입마다 함수를 등록한다. `-DUSE_DOCTEST` 로 빌드하면 미니 하네스 대신 doctest.h 를 쓴다 (5. 풀이 Part A). 스텁은 모두 "틀리지만 안전" 하다 — 무한 루프·크래시 없음, 45개 PASS 는 "init 전 읽기는 NotInit" 처럼 스텁이 우연히 맞히는 CHECK 다.

관찰용 파일 (하네스와 별개, 결과는 5. 풀이에 실제 출력으로):

| 파일 | 역할 |
|---|---|
| `sensor.hpp` / `sensor.cpp` | solution.cpp 의 프로토콜·드라이버 구간 그대로 + `extern template` / 명시적 인스턴스화 |
| `fake_tmp.hpp`, `i2c_fake.cpp`, `test_seam.cpp` | 링크 심 테스트 쪽 (가짜 정의 + 4개 시나리오) |
| `hw_i2c.hpp`, `i2c_hw.cpp`, `app_target.cpp` | 제품 쪽: 가상의 FIFO 형 I2C 컨트롤러(주소만 STM32F4 I2C1 0x4000'5400 대표값), `-DVARIANT=1/2/3` |
| `demo_errors.cpp` | `-DSHOW_ERROR=1..4` 컴파일 실패 4종 |

```bash
bash link_seam.sh                 # 맥: 파일 분리 링크 심 빌드·실행 + nm + 중복/미정의/.a 실험 + M4 오브젝트 → exit 0
bash size_targets.sh mac          # 맥: clang --target=thumbv7em 오브젝트별 함수 크기, 호출 지점 asm
bash size_targets.sh vm           # Lima VM elinux 에서 arm-none-eabi-g++ 14.2.1 실제 ELF 링크 표
bash size_targets.sh vm archive   # VM: GNU ld 가 .a 와 객체 순서에 따라 다르게 고르는 실험
bash doctest_check.sh             # doctest v2.4.11 을 /tmp 로 받아 solution.cpp 를 그대로 빌드·실행 → exit 0
c++ -std=c++20 -fsyntax-only -DSHOW_ERROR=1 demo_errors.cpp   # 1..4
```

## 4. 힌트

<details>
<summary>힌트 1 — 이음매를 정하기 전에 버스가 "무엇을 할 수 있어야 하나" 부터</summary>

`write_read(addr, reg, span<uint8_t> rx)` 와 `write(addr, span<const uint8_t> tx)` 두 개면 이 센서에 충분하다. 이걸 concept `I2cBus` 로 적으면 세 이음매가 모두 같은 요구를 만족하는지 `static_assert` 로 확인할 수 있다: `II2c` 는 가상 함수로, `LinkBus` 는 자유 함수로 전달하는 멤버로, `FakeBus` 는 그냥 멤버로.

</details>

<details>
<summary>힌트 2 — 가짜는 "값을 돌려주는 함수" 가 아니라 "작은 장치" 다</summary>

레지스터 배열 + 주입 큐(다음 트랜잭션에 적용할 Fault) + TEMP 스크립트 큐 + 로그. 주입 큐는 트랜잭션마다 하나씩 소비하고, TEMP 스크립트는 **전송이 성공했을 때만** 소비해야 "NACK 뒤 재시도로 같은 값을 받는" 현실과 맞는다. CRC 오류 주입은 버스 결과는 Ok 인데 CRC 바이트만 틀린 것이다.

</details>

<details>
<summary>힌트 3 — 재시도 루프에서 오류 종류별로 갈라라</summary>

시도마다 `rx[3]` 를 새로 받는다. Timeout → 즉시 반환 (버스가 멈췄으면 같은 걸 3번 해도 같다, 그리고 그동안 다른 장치도 막힌다). Nack → 기록하고 계속. CRC 불일치 → 기록하고 계속. 루프가 끝나면 **마지막** 오류를 돌려준다. 실패하면 `out` 을 건드리지 않는다.

</details>

<details>
<summary>힌트 4 — 링크 심 Rig 가 앞 테스트의 흔적을 본다면</summary>

`TempSensor<LinkBus>` 는 매번 새로 만들어도 `i2c::write_read` 뒤의 가짜는 전역 하나다. Rig 생성자에서 그 가짜를 초기화하라. 이걸 빼먹으면 `<link>` 케이스만 골라서 실패한다 — 템플릿·virtual Rig 은 테스트마다 새 `FakeTmp` 를 멤버로 갖기 때문이다.

</details>

<details>
<summary>힌트 5 — 감시 루프는 switch 하나</summary>

Init / Running / Faulted. Faulted 의 대기 카운터는 `++wait_ < 10` 이면 그냥 return (버스 접근 없음). Running 에서 성공하면 연속 실패 카운터를 **0 으로** — 이걸 빼먹으면 "실패 2, 성공 1, 실패 1" 에서 Faulted 로 잘못 간다 (테스트가 그 순서를 넣어 두었다).

</details>

## 5. 풀이

### Part A — 프로토콜 코어와 상태 기반 가짜

CRC-8 은 C 에서 하던 비트 루프 그대로다 (`constexpr` 라 테스트에서 `static_assert` 로도 쓸 수 있다). 검사값은 `{0xBE,0xEF} → 0x92`, `{0x00,0x00} → 0x81`, 빈 입력 → 초기값 `0xFF`. 온도 변환은 부동소수 없이 `int16_t(raw) * 125 / 16` — `uint16_t → int16_t` 변환은 C++20 부터 모듈러 산술로 정의된다 ([conv.integral], P1236). 0 쪽으로 버리므로 `0x0001 → 7`, `0xFFFF → -7` 이다 (바닥 함수면 -8).

| raw | m°C | 비고 |
|---|---|---|
| `0x0C80` | 25 000 | 3200 × 7.8125 |
| `0xFF80` | −1 000 | −128 LSB |
| `0x7FFF` | 255 992 | 255.992 1875 °C 버림 |
| `0x8000` | −256 000 | 최소값 |

가짜 장치 `FakeTmp` 는 **상태 기반(state-based) 가짜** 다. 테스트는 "어떤 호출이 어떤 순서로 왔는가" 보다 "결과 상태가 맞는가" (CONFIG 레지스터 값, 반환 코드, 통계) 를 주로 보고, 순서가 중요한 곳(ID 를 먼저 읽고 나서 CONFIG 를 쓴다)만 로그로 본다. 엄격한 기대 목(mock)은 연습 3 에서 비교한다.

```cpp
BusErr write_read(uint8_t a, uint8_t reg, std::span<uint8_t> rx) {
    const Fault f = next_fault();                      // 트랜잭션마다 하나 소비
    ...
    if (a != addr || f == Fault::Nack) res = BusErr::Nack;
    else if (f == Fault::Timeout) res = BusErr::Timeout;
    else {
        v = regs[reg & 0x0Fu];
        if (reg == ts::kRegTemp && tq_n_ > 0) { v = tq_[tq_head_]; ... }   // 성공했을 때만 스크립트 소비
        uint8_t frame[3] = {MSB, LSB, crc8(frame, 2)};
        if (f == Fault::BadCrc) frame[2] ^= 0x01u;     // 버스는 Ok, 데이터만 오염
        ...
    }
    log(Call{Op::WriteRead, a, reg, len, v, res});     // 실패한 호출도 기록
```

드라이버 본체는 Bus 에 대해 아무것도 모른다. 하나의 본체가 세 이음매에 쓰인다는 것이 이 레슨 설계의 핵심 결정이다: **이음매 선택은 드라이버가 아니라 조립 지점(composition root — `RigT/RigV/RigL`, `app_target.cpp`)이 한다.** 그래서 비교가 공정하다 — 크기 차이는 오직 이음매에서 온다.

같은 테스트를 세 번 돌리는 장치:

```cpp
struct RigT { FakeTmp dev; FakeBus  bus{dev}; TempSensor<FakeBus> s{bus}; FakeTmp& fake() { return dev; } };
struct RigV { FakeTmp dev; FakeBusV bus{dev}; TempSensor<II2c>    s{bus}; FakeTmp& fake() { return dev; } };
struct RigL { LinkBus bus; TempSensor<LinkBus> s{bus}; RigL() { link_fake_reset(); } FakeTmp& fake() { return g_link_fake; } };

TEST_CASE_TEMPLATE("B: timeout is not retried", R, RigT, RigV, RigL) {
    R r;
    CHECK(r.s.init() == DrvErr::Ok);
    r.fake().inject(Fault::Timeout);
    ...
```

이 테스트 본문이 **진짜 doctest 로도 그대로** 컴파일되는지 확인했다 (`doctest_check.sh`, doctest 2.4.11, 같은 `-Wall -Wextra -Werror -fno-exceptions -fno-rtti`, 실제 출력):

```text
doctest 2.4.11
[doctest] test cases:  34 |  34 passed | 0 failed | 0 skipped
[doctest] assertions: 289 | 289 passed | 0 failed |
[doctest] Status: SUCCESS!
doctest.h:1407:9: error: static assertion failed due to requirement 'deferred_false<bool>::value': Expression Too Complex Please Rewrite As Binary Comparison!
```

케이스 34개·CHECK 289개가 미니 하네스와 정확히 같다. 마지막 줄은 일부러 만든 함정이다: doctest 의 `CHECK` 는 식을 `lhs == rhs` 로 **분해(decompose)** 해서 양쪽 값을 출력하므로 `CHECK(a == 1 && b == 2)` 는 컴파일 에러다. 그래서 이 레슨의 테스트는 CHECK 하나에 비교 하나씩 쓴다 (미니 하네스는 `&&` 도 받지만, 이식성을 위해 같은 규칙을 지켰다). doctest 는 `-fno-exceptions` 를 감지해 예외 없이 동작했다.

### Part B — 재시도 정책과 오류 주입

| 버스 결과 | 드라이버 반응 | 이유 |
|---|---|---|
| NACK | 기록 후 재시도 (최대 3번 시도) | 변환 중이거나 클럭 스트레칭·노이즈 등 일시적일 수 있다. 3번 연속이면 장치가 없거나 주소가 틀렸다 |
| CRC 불일치 | 기록 후 재시도 | 버스 노이즈. 틀린 값을 쓰는 것보다 다시 읽는 게 싸다 |
| Timeout | **즉시** `Timeout` | 버스가 멈췄다(SDA 를 누가 잡고 있음). 반복해도 같은 결과이고 그동안 버스의 다른 장치도 막힌다. 복구(9 클럭 펄스, 주변장치 리셋)는 상위 계층 — Part C 는 재초기화로 대응 |
| ID 불일치 | `BadId`, CONFIG 안 씀 | 엉뚱한 칩(ADDR 핀 오배선, 대체 부품)에 쓰기를 하면 그 칩의 설정을 망칠 수 있다 |
| 다시 읽은 CONFIG 가 다름 | `ConfigMismatch`, not ready | 쓰기가 무시됨(전원 레일, 쓰기 보호). 조용히 기본 설정으로 동작하는 것이 가장 나쁘다 |

테스트가 "정확히 몇 번" 을 본다는 것이 중요하다: NACK 4개를 주입하면 `read_milli_c` 는 **정확히 3번** 시도하고 (`ncalls()` 차이 3) 4번째 주입은 큐에 남는다 (`pending_faults() == 1`). 타임아웃은 트랜잭션 1번. 이런 숫자 CHECK 가 없으면 "타임아웃도 재시도" 같은 변이가 살아남는다 (아래 버그 주입 결과).

**virtual 어댑터** 는 한 줄짜리 전달이다. `final` 을 붙인 이유: 테스트 코드가 가짜를 다시 상속해서 "스파이" 를 만드는 순간 가짜가 두 겹이 되고, `final` 은 호출을 정적으로 확정할 수 있다는 정보이기도 하다 (CPP-M06). `II2c` 는 `protected` 비가상 소멸자 — 기반 포인터로 `delete` 할 일이 없는 펌웨어에서 vtable 의 소멸자 슬롯과 그것이 끌고 오는 `operator delete`/`free` 사슬(CPP-M06 Part C 실측 +2.6 KB)을 아예 만들지 않는다.

**링크 심을 진짜로 나누면** — `link_seam.sh` 실제 출력 (발췌):

```text
== 2. 호스트 테스트 빌드 (sensor.o + i2c_fake.o + test_seam.o)
12/12 passed
== 3. 누가 정의하고 누가 부르나 (xcrun llvm-nm -C)
-- sensor.o
     0000000000000028 T TempSensor<II2c>::init()
     00000000000005f4 T TempSensor<LinkBus>::init()
                      U i2c::write_read(unsigned char, unsigned char, std::__1::span<unsigned char, 18446744073709551615ul>)
-- i2c_fake.o
     000000000000000c T i2c::write_read(unsigned char, unsigned char, std::__1::span<...>)
-- i2c_hw.o
     0000000000000000 T i2c::write_read(unsigned char, unsigned char, std::__1::span<...>)
== 4. 링커는 하나만 받는다
     duplicate symbol 'i2c::write_read(unsigned char, unsigned char, std::__1::span<unsigned char, 18446744073709551615ul>)' in:
  ok   fake + hw → 링크 실패 (기대대로)
     Undefined symbols for architecture arm64:
       "link_fake()", referenced from:
  ok   정의 없음 → 링크 실패 (기대대로)
== 5. 정적 라이브러리 함정: libhw.a 에 넣으면 '중복' 이 조용히 사라진다 (Mac ld64)
  ok   [OBJ LIB] 링크 성공 + 테스트 통과 — 가짜가 이겼고 libhw.a(i2c_hw.o) 는 끌려오지 않았다 (경고 없음)
  ok   [LIB OBJ] 링크 성공 + 테스트 통과 — 가짜가 이겼고 libhw.a(i2c_hw.o) 는 끌려오지 않았다 (경고 없음)
== 6. Cortex-M4 오브젝트 (recipe b)
              U i2c::write_read(unsigned char, unsigned char, std::span<unsigned char, 4294967295u>)
     00000000 T i2c::write_read(unsigned char, unsigned char, std::span<unsigned char, 4294967295u>)
link_seam.sh: ALL OK
```

같은 실험을 GNU ld (VM, `size_targets.sh vm archive`) 로 하면 **순서가 결과를 바꾼다** (실제 출력):

```text
-- app3.o sensor.o i2c_fake.o libhw.a
   link OK (경고 없음); i2c_fake.o 의 link_fake() 가 ELF 에 있다: 1개 → 가짜가 이겼다
-- app3.o sensor.o libhw.a i2c_fake.o
   multiple definition of `i2c::write_read(unsigned char, unsigned char, span<unsigned char>)'; /tmp/CPP-A08/out/libhw.a(i2c_hw.o):i2c_hw.cpp:(.text+0x0): first defined here
```

GNU ld 는 명령줄 순서대로 읽다가 아카이브를 만난 순간의 미정의 심볼만 채운다: `libhw.a` 를 먼저 만나면 `i2c_hw.o` 가 끌려오고, 뒤의 `i2c_fake.o` 와 충돌한다. Mac ld64 는 두 순서 모두 성공했다. 언어 규칙으로 보면 둘 다 "정의가 하나인 프로그램" 이라 합법이다 — 끌려오지 않은 아카이브 멤버는 프로그램의 일부가 아니다 ([basic.def.odr] 은 프로그램 안의 정의 수를 말한다). 위험은 **어느 쪽이 들어갔는지 아무도 알려주지 않는다** 는 데 있다. 실무 규칙: 가짜는 아카이브가 아닌 객체 파일로 링크하고, 테스트 바이너리의 `nm` 에서 가짜 전용 심볼(`link_fake`)을 확인하는 단계를 CI 에 둔다.

**툴체인을 섞으면 이음매가 끊긴다.** 같은 `sensor.cpp` 의 `TempSensor<LinkBus>::read_milli_c(int32_t&)` 가 clang (arm-none-eabi 헤더 사용) 에서는 `_ZN10TempSensorI7LinkBusE12read_milli_cERi` (`int&`), GCC 14.2 에서는 `...ERl` (`long&`) 로 맹글링됐다 — newlib 의 `int32_t` 가 컴파일러가 정의하는 `__INT32_TYPE__` 를 따르기 때문이다. clang 으로 만든 `sensor.o` 를 GCC 객체와 링크하면 (VM, 실제 출력):

```text
app_target.cpp:(.text+0x20): undefined reference to `TempSensor<LinkBus>::read_milli_c(long&)'
```

`i2c::write_read(unsigned char, unsigned char, span)` 는 고정 폭 별칭을 안 써서 우연히 맞았다. 이음매 함수의 시그니처는 ABI 계약이다 — 섞어 빌드할 계획이면 `extern "C"` + C 타입으로 고정하거나, 한 툴체인으로 통일한다.

**컴파일 시점 증명** (solution.cpp 의 `static_assert`, starter 에서도 성립):

```cpp
template <class B> constexpr bool can_make_sensor = requires { typename TempSensor<B>; };
static_assert(I2cBus<FakeBus> && I2cBus<II2c> && I2cBus<LinkBus>);
static_assert(!I2cBus<OnlyRead> && !I2cBus<int>);
static_assert(can_make_sensor<FakeBus> && !can_make_sensor<OnlyRead>);   // 제약 불만족 → 템플릿 id 자체가 무효
static_assert(std::is_abstract_v<II2c> && !std::is_destructible_v<II2c>);
static_assert(!std::is_polymorphic_v<TempSensor<II2c>>);                // vptr 는 드라이버가 아니라 버스 쪽에
```

`requires { typename TempSensor<B>; }` 를 네임스페이스 범위에서 바로 쓰지 않고 변수 템플릿으로 감싼 이유: 템플릿 밖의 requires-expression 에서 비의존적인 무효 타입은 false 가 아니라 바로 에러가 될 수 있다 ([expr.prim.req] 의 "치환 실패 → false" 는 템플릿 인자 치환에 대한 규칙). `demo_errors.cpp` 실제 에러 (Apple clang 21):

```text
== 1  TempSensor<OnlyRead>
demo_errors.cpp:22:5: error: constraints not satisfied for class template 'TempSensor' [with Bus = OnlyRead]
./sensor.hpp:69:11: note: because 'OnlyRead' does not satisfy 'I2cBus'
./sensor.hpp:46:9: note: because 'b.write(addr, tx)' would be invalid: no member named 'write' in 'OnlyRead'
== 2  delete (II2c*)
demo_errors.cpp:25:5: warning: delete called on 'II2c' that is abstract but has non-virtual destructor [-Wdelete-abstract-non-virtual-dtor]
demo_errors.cpp:25:12: error: calling a protected destructor of class 'II2c'
== 3  write 를 override 하지 않은 가짜
demo_errors.cpp:27:14: error: variable type 'HalfFake' is an abstract class
./sensor.hpp:52:20: note: unimplemented pure virtual method 'write' in 'HalfFake'
== 4  final 가짜 상속
demo_errors.cpp:29:18: error: base 'FakeBusV' is marked 'final'
```

concept 의 에러가 **빠진 멤버 이름** (`write`) 까지 짚어 준다 — 템플릿 주입의 오래된 약점("에러 메시지가 길다") 을 C++20 이 많이 줄였다. 다만 concept 는 "이름과 시그니처" 만 본다. `write` 가 진짜로 쓰는지는 테스트가 본다.

### Part C — 감시 루프

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">TempMonitor 상태 (100 ms tick, 가상 시간으로 검증)</text>
  <rect x="20" y="100" width="120" height="46" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="58" y="128" fill="currentColor" font-weight="bold">Init</text>
  <rect x="300" y="50" width="170" height="60" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="350" y="74" fill="currentColor" font-weight="bold">Running</text>
  <text x="310" y="96" fill="currentColor" font-size="12">4점 평균 · 85↑ 켬 / 80↓ 끔</text>
  <rect x="300" y="160" width="170" height="60" rx="8" class="f-warn" stroke="currentColor"/>
  <text x="352" y="184" fill="currentColor" font-weight="bold">Faulted</text>
  <text x="310" y="206" fill="currentColor" font-size="12">알람 강제 ON · 버스 조용</text>
  <path d="M140 112 L298 82" stroke="currentColor" fill="none"/>
  <text x="160" y="84" fill="currentColor" font-size="12">init() Ok</text>
  <path d="M140 134 L298 186" stroke="currentColor" fill="none"/>
  <text x="160" y="178" fill="currentColor" font-size="12">init() 실패</text>
  <path d="M440 110 L440 158" stroke="currentColor" fill="none"/>
  <text x="448" y="140" fill="currentColor" font-size="12">읽기 실패 연속 3번</text>
  <path d="M330 158 L330 112" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="200" y="140" fill="currentColor" font-size="12">10 tick 마다 init() Ok</text>
  <path d="M470 200 Q 560 230 560 190 Q 560 160 472 182" stroke="currentColor" fill="none"/>
  <text x="570" y="190" fill="currentColor" font-size="12">init() 실패:</text>
  <text x="570" y="206" fill="currentColor" font-size="12">대기 다시 10 tick</text>
  <text x="500" y="70" fill="currentColor" font-size="12">NACK 2번 + 성공 =</text>
  <text x="500" y="86" fill="currentColor" font-size="12">드라이버가 흡수 → 실패 아님</text>
</svg>
```

평균과 히스테리시스를 tick 단위로 손으로 계산해 기대값 표를 만들었다 (`C: monitor heats up` — 링 버퍼 쓰기 위치는 0,1,2,3,0,… 이고 첫 샘플이 창 전체를 채운다):

| tick | 읽은 값 (°C) | 창 | filtered (m°C) | 알람 |
|---|---|---|---|---|
| 2 | 25 | 25 25 25 25 | 25 000 | off |
| 4 | 90 | 25 90 25 25 | 41 250 | off |
| 7 | 90 | 90 90 90 90 | 90 000 | **on** (≥ 85 000) |
| 10 | 80 | 90 84 84 80 | 84 500 | on — On 문턱 아래지만 Off 문턱 위 |
| 12 | 79 | 79 79 84 80 | 80 500 | on |
| 13 | 79 | 79 79 79 80 | 79 250 | **off** (≤ 80 000) |

경계는 별도 테스트로 박았다: 85.000 °C 정확히 → 켜짐, 80.000 °C 정확히 → 꺼짐 (`>=`, `<=`). 정수 m°C 라서 경계 비교가 정확하다 — float 이었다면 `84.99999` 같은 값이 테스트를 흔든다.

`C: three failures` 시나리오의 버스 트랜잭션 계산: init 3 + 25 °C 1 + (NACK 2 + 성공 1) 3 + 타임아웃 2 + 성공 1 + 타임아웃 3 = **13** 에서 Faulted. tick 10–18 동안 13 그대로 (대기 중 버스 조용), tick 19 재초기화 +3, tick 20 읽기 → 알람 해제. "성공하면 연속 실패 카운터 0" 을 빼먹은 변이는 tick 7 에서 Faulted 로 가서 이 테스트에 걸린다.

`TempMonitor<Sensor>` 도 템플릿이라 세 드라이버 모두에 쓰인다. 감시 루프 자체를 테스트하려고 **센서를 또 가짜로 만들 필요가 없다** 는 점이 상태 기반 가짜의 이점이다 — 진짜 드라이버 + 가짜 버스로 통합 수준까지 한 번에 본다.

### 실측: 제품 빌드의 크기와 호출 코드

`app_target.cpp` 는 init 후 무한히 온도를 읽는 최소 펌웨어다. `size_targets.sh vm` 실제 출력 (Lima VM, `arm-none-eabi-g++ 14.2.1 -mcpu=cortex-m4 -mthumb -fno-exceptions`, `-nostartfiles`, 단위 바이트):

```text
t_gc           text=524   data=16   bss=12   vtables=0(0B)   typeinfo=0(0B)    init_array=0
v_gc           text=776   data=20   bss=8    vtables=1(16B)  typeinfo=0(0B)    init_array=0
l_gc           text=740   data=16   bss=12   vtables=0(0B)   typeinfo=0(0B)    init_array=0
t_nogc         text=516   data=16   bss=12   vtables=0(0B)   typeinfo=0(0B)    init_array=0
v_nogc: LINK FAILED
sensor.cpp:(.text._ZN10TempSensorI7LinkBusE10read_reg16EhRt[...]+0x2e): undefined reference to `i2c::write_read(...)'
l_nogc         text=1320  data=16   bss=12   vtables=0(0B)   typeinfo=0(0B)    init_array=0
t_lto          text=512   data=16   bss=12   vtables=0(0B)   typeinfo=0(0B)    init_array=0
v_lto          text=820   data=20   bss=8    vtables=1(16B)  typeinfo=0(0B)    init_array=0
l_lto          text=508   data=16   bss=12   vtables=0(0B)   typeinfo=0(0B)    init_array=0
v_rtti         text=9196  data=1360 bss=384  vtables=3(104B) typeinfo=10(150B) init_array=0
t_rtti         text=524   data=16   bss=12   vtables=0(0B)   typeinfo=0(0B)    init_array=0
t_os           text=480   data=16   bss=12   vtables=0(0B)   typeinfo=0(0B)    init_array=0
v_os           text=658   data=20   bss=8    vtables=1(16B)  typeinfo=0(0B)    init_array=0
l_os           text=622   data=16   bss=12   vtables=0(0B)   typeinfo=0(0B)    init_array=0
```

(t = 템플릿, v = virtual, l = 링크 심. gc = `-ffunction-sections -fdata-sections -Wl,--gc-sections`, 기본 `-O2`.)

```svg
<svg viewBox="0 0 740 310" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">.text 바이트 (Cortex-M4 ELF, GCC 14.2.1, gc-sections, 실측)</text>
  <g fill="currentColor"><text x="10" y="50" font-weight="bold">-O2</text><text x="10" y="130" font-weight="bold">-Os</text><text x="10" y="210" font-weight="bold">-O2 -flto</text></g>
  <g font-size="12" fill="currentColor">
    <text x="90" y="71">템플릿</text><text x="90" y="91">virtual</text><text x="90" y="111">링크 심</text>
    <text x="90" y="151">템플릿</text><text x="90" y="171">virtual</text><text x="90" y="191">링크 심</text>
    <text x="90" y="231">템플릿</text><text x="90" y="251">virtual</text><text x="90" y="271">링크 심</text>
  </g>
  <rect x="160" y="58" width="314" height="16" class="f-ok" stroke="currentColor"/><text x="480" y="71" fill="currentColor" font-size="12">524</text>
  <rect x="160" y="78" width="466" height="16" class="f-warn" stroke="currentColor"/><text x="632" y="91" fill="currentColor" font-size="12">776 (+48%)</text>
  <rect x="160" y="98" width="444" height="16" class="f-acc" stroke="currentColor"/><text x="610" y="111" fill="currentColor" font-size="12">740 (+41%)</text>
  <rect x="160" y="138" width="288" height="16" class="f-ok" stroke="currentColor"/><text x="454" y="151" fill="currentColor" font-size="12">480</text>
  <rect x="160" y="158" width="395" height="16" class="f-warn" stroke="currentColor"/><text x="561" y="171" fill="currentColor" font-size="12">658 (+37%)</text>
  <rect x="160" y="178" width="373" height="16" class="f-acc" stroke="currentColor"/><text x="539" y="191" fill="currentColor" font-size="12">622 (+30%)</text>
  <rect x="160" y="218" width="307" height="16" class="f-ok" stroke="currentColor"/><text x="473" y="231" fill="currentColor" font-size="12">512</text>
  <rect x="160" y="238" width="492" height="16" class="f-warn" stroke="currentColor"/><text x="658" y="251" fill="currentColor" font-size="12">820 (커짐!)</text>
  <rect x="160" y="258" width="305" height="16" class="f-acc" stroke="currentColor"/><text x="471" y="271" fill="currentColor" font-size="12">508</text>
  <text x="10" y="300" fill="currentColor" font-size="12">-frtti: virtual 판 9196 (+8420, type_info 사슬). 템플릿 판은 다형 타입이 없어 524 그대로.</text>
</svg>
```

함수별로 보면 차이가 어디서 오는지 보인다 (`nm -C -S --size-sort`, 10진):

| 함수 | t_gc | v_gc | l_gc |
|---|---|---|---|
| `_start` (init·read_milli_c 인라인 여부에 따라) | 304 | 68 | 68 |
| `read_reg16` | 220 | 194 | 174 |
| `write_reg16` / `init` / `read_milli_c` | (`_start` 안으로 인라인) | 154 / 106 / 54 | 146 / 106 / 54 |
| 버스 함수 (MMIO `hw_xfer` 인라인 사본) | 0 (호출 지점에 녹음) | `HwBusV::write` 84 + `write_read` 92 | `i2c::write` 88 + `write_read` 96 |
| vtable | — | 16 (`.rodata`) | — |

- **템플릿**: 버스 코드가 호출 지점에 녹아들고, 한 번만 쓰이는 `init`/`write_reg16` 은 `_start` 로 인라인됐다. 호출 경계가 없으니 함수 프롤로그·인자 전달도 없다.
- **virtual**: 드라이버 함수가 각자 남고(다른 TU 라 인라인 불가), 버스 함수 2개가 각각 `hw_xfer` 사본을 품는다. `.data` +4 B 는 `HwBusV g_bus` 의 vptr (상수 초기화라 `.init_array` 없음), `.rodata` +16 B 는 vtable (offset-to-top, typeinfo 0, 슬롯 2개 — `objdump -s` 로 `00000000 00000000 55800000 01800000` 확인, 슬롯 값의 bit0=1 은 Thumb).
- **링크 심**: virtual 과 거의 같다 — 다른 TU 의 함수는 인라인할 수 없다는 같은 이유. vtable 만 없다.
- **LTO 는 이음매마다 다르게 작동했다.** 링크 심 + LTO = 508 B 로 템플릿(512)과 같아졌다: LTO 는 TU 경계를 지우고, 직접 호출은 인라인할 수 있다. virtual + LTO 는 오히려 **커졌다** (776 → 820). 아래 asm 처럼 GCC 가 "추측 탈가상화(speculative devirtualization)" 로 `HwBusV::write_read` 를 인라인하면서도 다른 구현일 경우를 위한 간접 호출 경로를 남겼기 때문이다.
- `v_nogc` 링크 실패는 이 레슨의 파일 구성이 만든 실제 함정이다: `sensor.cpp` 가 `TempSensor<II2c>` 와 `TempSensor<LinkBus>` 를 **한 .o** 에서 인스턴스화하므로, gc-sections 없이 virtual 판을 링크해도 안 쓰는 링크 심 판이 `i2c::write_read` 를 요구한다. 이음매별로 .cpp 를 나누거나 gc-sections 를 켠다.

호출 지점 asm (VM ELF, `size_targets.sh dis <v>`, 실제 출력 발췌):

```text
@ t_gc — TempSensor<HwBus>::read_reg16: 호출 없음, MMIO 저장이 바로 나온다
801a:	str.w	sl, [r2, #8]        @ ADDR
8022:	str	r6, [r2, #12]           @ TXLEN = 1
8024:	str.w	r9, [r2, #16]       @ RXLEN = 3
8028:	str	r4, [r2, #20]           @ TXFIFO = reg
802a:	str	r6, [r2, #0]            @ CTRL = START
8032:	ldr	r1, [r2, #4]            @ STATUS 폴링 ...

@ v_gc — TempSensor<II2c>::read_reg16: 참조 → vptr → 슬롯 → blx, 5번째 인자는 스택
810a:	ldr	r0, [r4, #0]            @ bus_ (II2c&) = this
810c:	ldrb	r1, [r4, #4]        @ addr_
810e:	ldr	r3, [r0, #0]            @ vptr
8114:	ldr.w	fp, [r3]            @ 슬롯 0 = write_read
8120:	str	r7, [sp, #0]            @ span.size() = 3 → 스택 (r0..r3 소진)
8122:	mov	r3, sl                  @ span.data()
8124:	mov	r2, r6                  @ reg
8126:	blx	fp

@ l_gc — TempSensor<LinkBus>::read_reg16: 직접 호출, span 은 r2:r3
8062:	ldrb	r0, [r4, #4]
8068:	ldmia.w	r9, {r2, r3}
806c:	mov	r1, r7
8072:	bl	822c <i2c::write_read(...)>

@ v_lto — 추측 탈가상화: 슬롯이 HwBusV::write_read 면 인라인, 아니면 원래대로 blx
80d6:	ldr.w	sl, [r3]            @ 슬롯 0
80de:	cmp	sl, fp                  @ fp = &HwBusV::write_read
80e4:	bne.n	8180                @ 다르면 → 8186: blx sl
80e8:	str	r1, [r4, #8]            @ 같으면 → MMIO 인라인 (ADDR ...)
```

virtual 호출 1번의 추가 비용은 `ldr` 3개(참조·vptr·슬롯) + `blx` + 인자 하나 더(`this`). AAPCS 는 정수 인자 4개를 r0–r3 로 넘기므로 `this` 때문에 `span` 의 size 가 스택으로 밀렸다. 사이클 수는 측정하지 않았다 (DWT 없음) — Cortex-M4 의 분기·로드 사이클은 TRM 의 명령 타이밍 표를 봐야 하며 여기서는 명령 수로만 말한다. I2C 트랜잭션 하나가 100 kHz 에서 수백 µs 라는 점을 생각하면, 이 드라이버에서 호출 비용은 무시할 만하고 **플래시 바이트** 가 더 실질적인 차이다.

**clang 으로 같은 비교를 하면 그림이 뒤집힌다.** `size_targets.sh mac` (Apple clang 21, `--target=thumbv7em`, `-O2`, 링크 전 오브젝트) 실제 함수 크기 합계:

| | 템플릿 (`app1.o`) | virtual (`app2.o` + `sensor.o` 의 II2c 판) | 링크 심 (`app3.o` + `sensor.o` 의 LinkBus 판 + `i2c_hw.o`) |
|---|---|---|---|
| 합계 | 188 + 574 + 942 = **1704** | 100 + 320 + 372 + 16 + 56 + 112 + 336 + 352 = **1664** | 100 + 56 + 112 + 324 + 338 + 372 + 382 = **1684** |

clang `-O2` 는 CRC 의 8번 비트 루프를 완전히 펼치고 `hw_xfer` 를 재시도 루프 안의 호출 지점마다 인라인했다 (`write_reg16` 942 B). 인라인이 코드를 **복제** 할 때는 템플릿 주입이 오히려 크다. "템플릿 = 제로 비용" 은 호출 오버헤드에 대한 말이지 플래시 크기에 대한 보장이 아니다 — 크기가 중요하면 `-Os` 와 실제 링크 결과로 판단한다 (GCC `-Os` 에서는 `hw_xfer` 80 B 가 한 벌만 남았다).

`sizeof` (recipe b 로 확인): Cortex-M4 에서 `TempSensor<HwBus>`, `TempSensor<II2c>`, `TempSensor<LinkBus>` 모두 **16 B** (버스 참조 4 + addr 1 + ready 1 + 패딩 2 + 통계 8), `HwBusV` 4 B (vptr), `HwBus` 1 B (빈 struct). 상태 없는 버스를 값으로 들면 참조 4 B 가 사라진다 (연습 1).

### 트레이드오프 표와 선택

| 항목 | (1) 템플릿 | (2) virtual | (3) 링크 심 |
|---|---|---|---|
| `.text` GCC `-O2` gc / `-Os` / LTO | **524** / **480** / 512 | 776 / 658 / 820 | 740 / 622 / **508** |
| vtable · typeinfo | 없음 | vtable 16 B + 버스 객체마다 vptr 4 B. `-frtti` 면 +8.4 KB 사슬 | 없음 |
| 호출 | 인라인 (MMIO 직접) | `ldr`×3 + `blx`, 탈가상화는 LTO 추측뿐 | `bl` 직접, LTO 면 인라인 |
| 테스트마다 다른 가짜 | O (객체) | O (객체) | X — 프로그램당 정의 1개 → 전역 가짜 + reset |
| 같은 바이너리에 실물·가짜 공존 | O | O | X (중복 정의) — 테스트 바이너리를 따로 |
| 인스턴스 여러 개 (버스 2개) | 자연스럽다 | 자연스럽다 | 이음매 API 에 버스 번호 필요 (연습 2) |
| 헤더 노출 · 재컴파일 | 본체가 헤더에 → 바꾸면 쓰는 TU 전부 재컴파일 | 본체를 .cpp 에 (`extern template` 또는 일반 클래스) | 본체 .cpp, HAL 헤더는 선언만 |
| 컴파일 에러 품질 | concept 로 많이 좋아짐 (빠진 멤버 이름까지) | 좋다 | 링크 단계에서야 발견 (미정의·중복·맹글링) |
| 레거시 C / 벤더 HAL | 코드 수정 필요 | 코드 수정 필요 | **수정 없이** 가능 (`HAL_I2C_Mem_Read` 같은 C 함수를 가짜로) |
| 조용한 실패 위험 | 낮음 | 낮음 | .a 순서, weak 기본 구현, 툴체인 혼합 |

(컴파일 시간은 이 크기의 코드로는 측정할 만한 차이가 없어 재지 않았다. "헤더 노출" 행은 구조적 사실이다: `app2.o` 의 nm 은 `U TempSensor<II2c>::init()` — `extern template` 덕에 사용자 TU 는 본체를 컴파일하지 않았다 ([temp.explicit]: 명시적 인스턴스화 **선언** 은 인라인이 아닌 멤버의 암시적 인스턴스화를 막는다). 클래스 안에서 정의한 멤버는 암시적 inline 이라 이 효과가 없으므로, 본체 멤버를 클래스 밖에 정의했다.)

언제 무엇을:

- **템플릿 주입 — 펌웨어 기본값.** 보드마다 버스가 정해져 있고(대부분), 드라이버가 ISR 이나 타이트한 루프에서 불리고, 플래시가 빠듯할 때. 테스트에서는 가짜 타입 하나로 끝난다. 대가: 본체가 헤더에 있고, 인라인 복제로 커질 수 있다(`-Os` 로 확인).
- **virtual 주입 — 실행 시점에야 정해질 때.** 보드 리비전을 부팅 때 감지해 다른 버스/센서를 고르거나, 같은 드라이버를 여러 버스 구현(비트뱅 I2C, DMA I2C)에 꽂을 때, 또는 큰 코드베이스에서 컴파일 방화벽이 필요할 때. 조건: `-fno-rtti`, protected 비가상 소멸자, `final` 구현, 객체는 정적 할당.
- **링크 심 — 소스를 못 바꿀 때.** 레거시 C 모듈, 벤더 HAL, 생성된 코드. 테스트 대상 코드는 그대로 두고 링크할 파일만 바꾼다. 제품에서는 LTO 를 켜면 비용이 템플릿과 같아진다(508 vs 512). 대가: 테스트 격리(전역 reset), 한 바이너리에 가짜 하나, 링크 단계의 조용한 실패.

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution     # 289/289 passed
c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer solution.cpp -o sol_san && ./sol_san   # 289/289, 보고 없음
bash link_seam.sh && bash doctest_check.sh     # 둘 다 exit 0
```

테스트가 실제로 버그를 잡는지 확인하려고 solution.cpp 의 사본에 버그 6개를 하나씩 넣어 돌렸다 (사본은 레슨 폴더 밖):

| 넣은 버그 | 결과 |
|---|---|
| 타임아웃도 재시도 (`continue`) | `B: timeout is not retried`, `C: three failures` × 3 Rig = 6 케이스 FAIL (253/289) |
| `link_fake_reset()` 비움 | **`<link>` 케이스만** 8개 FAIL (260/289) — 테스트 격리 문제의 전형적인 모습 |
| 알람 켜짐 `>` (경계 미포함) | `C: alarm thresholds are inclusive` × 3 FAIL (283/289) — 승온 시나리오만으로는 경계값을 지나지 않아 이 테스트를 따로 넣었다 |
| CRC 검사 생략 | `B: bad CRC ...` × 3 FAIL (277/289) |
| BadId 인데 CONFIG 를 씀 | `B: wrong ID ...`, `C: init fails ...` × 3 = 6 FAIL (277/289, 호출 수 CHECK 가 잡음) |
| 성공해도 연속 실패 카운터 유지 | 처음엔 **살아남았다** (전부 PASS) → "실패 2, 성공, 실패 3" 순서를 테스트에 넣은 뒤 `C: three failures` × 3 FAIL (271/289) |

마지막 줄이 이 연습의 교훈이다: 처음 테스트는 연속 실패만 넣어서 "성공이 카운터를 리셋하는가" 를 묻지 않았다. 변이가 살아남으면 테스트에 빠진 질문이 있다는 뜻이다 (TEST-A03 mutation testing).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 링크 심 가짜를 테스트마다 초기화하지 않음 | 정의가 하나뿐이라 가짜도 전역 하나 — 앞 테스트의 로그·주입·주소가 샌다. 실행 순서에 따라 결과가 바뀐다 | Rig 생성자(또는 setUp)에서 `link_fake_reset()` |
| 가짜를 `.a` 에, 실물도 `.a` 에 | 링커가 먼저 만난 쪽을 조용히 고른다 (GNU ld 는 순서 의존, ld64 는 성공) | 가짜는 객체 파일로 직접 링크, CI 에서 `nm` 으로 가짜 심볼 확인 |
| 이음매 함수를 헤더에 `inline` 으로 정의 | 호출 지점에 이미 인라인돼 링크할 심볼이 없다 → 대체 불가 | 링크 심 함수는 .cpp 에 비인라인으로 (선언만 헤더에) |
| weak 기본 구현 + 테스트에서 강한 정의 | 강한 정의를 빠뜨리면 **경고 없이** 기본 구현이 들어간다. 그리고 weak 함수는 같은 TU 에서도 인라인되지 않는다 (맥 clang 실측: `b __Z8hal_readv`) | 기본 구현을 두지 않거나, 빌드 산출물 `nm` 검사 |
| `II2c` 에 `virtual ~II2c()` | vtable 에 deleting dtor 슬롯 → `operator delete` → `free`/`malloc` 사슬 (CPP-M06 실측 +2.6 KB) | `protected: ~II2c() = default;` |
| virtual 판을 `-frtti` 로 빌드 | typeinfo 와 `__cxxabiv1` 사슬로 `.text` 776 → 9196 | `-fno-rtti` (CPP-B13) |
| "템플릿이니까 무조건 작다" | clang `-O2` 실측에서 템플릿 판이 가장 컸다 (인라인 복제) | `-Os` + 실제 링크 결과로 판단 |
| 재시도 루프가 타임아웃도 재시도 | 멈춘 버스를 3배 오래 붙잡는다 — 다른 장치도 같이 굶는다 | 타임아웃은 즉시 반환, 복구는 상위 계층 |
| ID 확인 전에 CONFIG 쓰기 | 엉뚱한 칩에 설정을 쓴다 | ID → CONFIG → 다시 읽기 순서를 로그로 테스트 |
| 테스트가 반환 코드만 확인 | "3번 시도했나, 버스가 조용했나" 를 모르면 재시도 버그가 산다 | 호출 로그·횟수·통계까지 CHECK |
| 툴체인 섞어 빌드 (clang 라이브러리 + GCC 앱) | `int32_t` 가 `int` vs `long` → 맹글링이 달라 `undefined reference` (실측) | 한 툴체인, 또는 경계를 `extern "C"` + C 타입 |
| doctest 에서 `CHECK(a && b)` | 식 분해 불가 → `Expression Too Complex` 컴파일 에러 (실측) | CHECK 를 나누거나 `CHECK((a && b))` (분해 없이 bool 하나로) |

## 8. 면접 Q&A

<details>
<summary>Q1. 새 센서 드라이버를 쓴다면 셋 중 무엇을 기본으로 하나요?</summary>

드라이버 본체를 `template <I2cBus Bus>` 로 **한 번** 쓰고, 기본 조립은 템플릿 주입이다. 근거: Cortex-M4 실측에서 가장 작고(GCC `-O2` 524 B vs virtual 776, 링크 심 740), 호출이 MMIO 저장으로 바로 녹고, 테스트는 가짜 타입 하나로 된다. 이렇게 써 두면 나중에 실행 시점 선택이 필요해지면 `TempSensor<II2c>` 로, 레거시 빌드에 끼워야 하면 `TempSensor<LinkBus>` 로 **본체 수정 없이** 바꿀 수 있다 — 이음매 선택을 조립 지점으로 미룬 것이 진짜 설계 포인트다. 꼬리: "모든 드라이버를 템플릿으로 하면?" → 상위 계층까지 전부 템플릿이 되어 헤더가 비대해진다. 보통 leaf 드라이버는 템플릿, 그 위 서비스 계층은 좁은 인터페이스(virtual 또는 함수 포인터) 로 끊는다.

</details>

<details>
<summary>Q2. 템플릿 주입은 항상 제로 비용인가요?</summary>

호출 오버헤드는 0 이지만 크기는 보장하지 않는다. 같은 소스를 clang 21 `-O2` 로 thumbv7em 오브젝트를 만들면 템플릿 판 함수 합계가 1704 B 로 virtual(1664)·링크 심(1684)보다 컸다 — CRC 비트 루프를 완전히 펼치고 `hw_xfer` 를 재시도 루프의 호출 지점마다 인라인했기 때문이다. GCC 14.2 의 실제 링크에서는 반대로 가장 작았다(524). 그리고 버스 타입이 N 개면 본체도 N 벌 생긴다 (CPP-M05 에서 측정한 템플릿 팽창, CPP-A05 의 thin template). 판단은 `-Os` + gc-sections + 실제 링크 결과로. 꼬리: "인스턴스가 여러 버스 타입이면?" → 본체의 버스 무관 부분(CRC, 변환, 재시도 정책)을 비템플릿 함수로 빼고 버스 접근만 템플릿에 남긴다.

</details>

<details>
<summary>Q3. virtual 주입의 비용을 Cortex-M4 에서 정확히 말해 보세요.</summary>

실측 기준: ① 플래시 — vtable 16 B (`.rodata`: offset-to-top, typeinfo 포인터(=0), 슬롯 2개) + 다른 TU 라 인라인이 막혀서 생긴 함수 경계들, 합계 `.text` +252 B (+48%, `-O2`) / +178 B (`-Os`). ② RAM — 버스 객체마다 vptr 4 B (`HwBusV g_bus` 가 `.bss` 1 B 에서 `.data` 4 B 로; 상수 초기화라 `.init_array` 없음). ③ 호출 — `ldr` 3개(참조, vptr, 슬롯) + `blx`, 그리고 `this` 가 r0 를 차지해서 5번째 인자(span size)가 스택으로 갔다. ④ `-frtti` 를 켜면 typeinfo + libstdc++ `__cxxabiv1` 사슬로 `.text` 9196 B. ⑤ `virtual ~II2c()` 를 쓰면 deleting dtor → `operator delete` → malloc 사슬 (CPP-M06). 꼬리: "LTO 로 탈가상화되나?" → GCC 14 는 추측 탈가상화를 했다: 슬롯을 `&HwBusV::write_read` 와 `cmp` 해서 같으면 인라인, 다르면 `blx` — 그래서 오히려 820 B 로 커졌다. 확정 탈가상화는 `final` + 정적 타입이 보일 때(CPP-M06) 또는 clang 의 whole-program-vtables.

</details>

<details>
<summary>Q4. 링크 심의 함정을 아는 대로 말해 보세요.</summary>

① 정의가 프로그램당 하나라 가짜도 전역 하나 — 테스트마다 reset 하지 않으면 순서 의존 (이 레슨에서 reset 을 빼면 `<link>` 케이스 8개만 실패). 테스트마다 다른 가짜가 필요하면 가짜 TU 에 제어 API(스크립트·주입)를 두거나 테스트 실행 파일을 나눈다. ② 실물과 가짜를 같이 링크하면 중복 정의 에러 — 그런데 한쪽이 `.a` 안에 있으면 에러가 사라지고 링커가 조용히 고른다 (GNU ld 는 순서 의존, ld64 는 두 순서 모두 성공, 실측). ③ 이음매 함수가 헤더에 inline 이면 대체할 심볼이 없다. ④ weak 기본 구현은 강한 정의를 빠뜨려도 조용하다. ⑤ 시그니처가 ABI 다 — clang 과 GCC 가 `int32_t` 를 `int`/`long` 으로 달리 맹글링해서 섞은 링크가 `undefined reference` (실측). ⑥ 인스턴스 2개를 구분하려면 이음매 API 에 버스 번호가 들어가야 한다. 장점도 분명하다: 테스트 대상 소스를 **한 글자도** 안 바꾸고, LTO 를 켜면 제품 비용이 템플릿과 같다 (508 vs 512 B).

</details>

<details>
<summary>Q5. 가짜(fake)와 엄격한 목(mock) 중 무엇을 쓰나요?</summary>

기본은 상태 기반 가짜다: 장치 모형(레지스터 파일)을 만들고 결과 상태와 반환 코드를 본다. 구현이 바뀌어도(읽기 순서, 버스트 읽기로 최적화) 행동이 같으면 테스트가 깨지지 않는다. 엄격한 목(CMock/gMock 식 expect 큐)은 **순서 자체가 요구사항** 인 곳에 쓴다: "ID 확인 전에 CONFIG 를 쓰면 안 된다", "전원 시퀀스 A→B→C". 연습 3 의 `ExpectBus` 는 순서가 바뀐 init 을 `mismatched` 로, 빠진 호출을 `missing` 으로, 남는 호출을 `unexpected` 로 잡는다. 대가는 취약함 — 리팩터링마다 기대를 고쳐야 한다. 이 레슨의 가짜는 그 중간으로, 호출 로그를 남겨 필요한 테스트만 순서를 본다. 꼬리: "목 생성기는?" → 헤더를 파싱해 expect/return 큐를 생성하는 것이 CMock (TEST-B05). C++ 에선 gMock 이 virtual 인터페이스나 템플릿 파라미터에 붙는다 (펌웨어 타깃에서는 안 쓰고 호스트 테스트 전용).

</details>

<details>
<summary>Q6. 같은 테스트를 세 이음매에 어떻게 돌렸고, 그게 왜 의미가 있나요?</summary>

`TEST_CASE_TEMPLATE(name, R, RigT, RigV, RigL)` — 각 Rig 은 `s` (드라이버) 와 `fake()` (같은 `FakeTmp` 모형) 를 노출한다. 테스트 본문은 이음매를 모른다. 의미: ① 이음매를 바꿔도 행동이 같다는 증거 — 리팩터링(예: virtual → 템플릿)을 안전하게 한다. ② 이음매 고유의 버그를 드러낸다 — reset 누락은 `<link>` 만 실패시켰다. ③ 프레임워크 이식성 — 같은 본문이 doctest 2.4.11 로도 `-Werror -fno-exceptions -fno-rtti` 에서 컴파일·통과했다 (34 케이스, 289 단언). 미니 하네스는 C++20 템플릿 람다 `[]<class U>(){ f<U>(); }` 와 캡처 없는 람다의 기본 생성(C++20)으로 타입마다 함수를 등록한다. 꼬리: "GoogleTest 라면?" → `TYPED_TEST_SUITE` / `TYPED_TEST` 가 같은 역할 (이 환경에 설치돼 있지 않아 실행해 보진 않음).

</details>

<details>
<summary>Q7. extern template 을 왜 썼고, 정말 효과가 있었나요?</summary>

virtual·링크 심 판의 장점 중 하나가 "본체를 .cpp 에 숨긴다" 인데, 이 레슨은 본체를 템플릿 하나로 썼다. 그래서 `sensor.hpp` 에 `extern template class TempSensor<II2c>;` 를 두고 `sensor.cpp` 에서 명시적 인스턴스화를 했다. 효과는 nm 으로 확인: `app2.o` 에 `U TempSensor<II2c>::init()` — 사용자 TU 는 본체를 만들지 않고 참조만 한다. 주의: 클래스 안에서 정의한 멤버 함수는 암시적 inline 이라 명시적 인스턴스화 선언이 그 인스턴스화를 막지 않는다 ([temp.explicit]) — 그래서 본체 멤버를 클래스 밖에 정의했다. 부작용도 실측했다: 한 .cpp 에 두 판을 인스턴스화하니 gc-sections 없는 virtual 빌드가 링크 심 판의 `i2c::write_read` 미정의로 실패했다.

</details>

<details>
<summary>Q8. 타임아웃을 재시도하지 않는 이유와, 감시 루프에서 Faulted 일 때 알람을 켜는 이유는?</summary>

타임아웃은 "응답이 늦다" 가 아니라 "버스가 끝나지 않았다" (SDA 가 낮게 묶임, 슬레이브가 트랜잭션 중간에 리셋 등) 일 가능성이 높다. 같은 트랜잭션을 반복하면 매번 스핀 상한(여기선 1만 번 폴링)만큼 CPU 와 버스를 붙잡고, 같은 버스의 다른 장치도 굶는다. 복구는 9 클럭 펄스 + STOP, 주변장치 리셋 같은 별도 절차이고, 이 레슨은 상위(감시 루프)의 재초기화로 대응한다. Faulted 에서 알람을 켜는 것은 fail-safe: 온도를 모르는 상태를 "정상" 으로 보고하는 것이 더 위험하다 (팬을 최대로, 출력 제한 등 안전 쪽 동작). 대기 중 버스를 조용히 두는 것도 테스트한다 — 고장 난 장치에 매 tick 트랜잭션을 퍼붓지 않는다. 꼬리: "연속 3번이라는 숫자는?" → 샘플 주기 × 3 = 300 ms 가 시스템의 허용 지연(FTTI 같은 안전 시간) 안에 들어오는지로 정한다 (TEST-M07).

</details>

## 9. 추가 연습

### 연습 1 — 상태 없는 버스는 값으로: `[[no_unique_address]]`

본문 `TempSensor` 는 버스를 참조로 들고 있어 `LinkBus` 처럼 빈 어댑터에도 포인터 하나를 낸다. 버스가 비어 있고 기본 생성 가능하면 값으로, 아니면 참조로 드는 `BusSlot<Bus>` 를 만들어 크기를 줄여라.

<details>
<summary>답 보기</summary>

```cpp
template <class Bus> struct BusSlot { Bus& ref; constexpr explicit BusSlot(Bus& b) : ref(b) {} constexpr Bus& get() { return ref; } };
template <class Bus> requires(std::is_empty_v<Bus> && std::is_default_constructible_v<Bus>)
struct BusSlot<Bus> { [[no_unique_address]] Bus b; constexpr explicit BusSlot(Bus&) {} constexpr Bus& get() { return b; } };
// TempSensor 안: [[no_unique_address]] BusSlot<Bus> slot_;
```

실측 (호스트 arm64): 참조 판 24 B → 값 판 10 B (`addr` 1 + `ready` 1 + 통계 8). Cortex-M4 에서는 참조 판이 16 B 였다 (recipe b). `BusSlot` 안의 멤버와 `TempSensor` 의 `slot_` **둘 다** 에 속성을 붙여야 한다 — 하나만 붙이면 빈 `BusSlot` 이 1바이트를 차지하고 정렬 때문에 더 커진다. [dcl.attr.nouniqueaddr]. MSVC 는 이 속성을 무시하고 `[[msvc::no_unique_address]]` 를 쓴다 (문서 기준, 미확인). 장치가 수십 개인 보드(센서 배열)라면 의미가 있고, 하나면 4 B 라 신경 쓸 일이 아니다.

</details>

### 연습 2 — 링크 심 + 인스턴스 2개

보드에 온도 센서가 두 개(CPU 쪽 I2C1, 모터 쪽 I2C2)다. 링크 심으로 테스트하면서 **I2C2 만** 고장 내려면 이음매 API 가 어떻게 바뀌어야 하나? `i2c2::write_read(BusId, ...)` 와 버스마다 가짜 하나로 구현하라.

<details>
<summary>답 보기</summary>

```cpp
struct LinkBusN { BusId id; BusErr write_read(uint8_t a, uint8_t r, std::span<uint8_t> rx) { return i2c2::write_read(id, a, r, rx); } ... };
MiniTmp g_bus_fakes[2];                       // "i2c_fake.cpp": 버스 번호로 가짜를 고른다
BusErr i2c2::write_read(BusId bus, ...) { return g_bus_fakes[static_cast<uint8_t>(bus)].write_read(...); }
```

검증: I2C2 에 NACK 3개 → `motor.read_milli_c` 만 `Nack`, `cpu` 는 25 °C, 호출 수 I2C1 = 4, I2C2 = 6 (init 3 + 실패한 읽기 3). 교훈: 링크 심의 "인스턴스" 는 함수 인자로만 표현된다. 벤더 HAL 이 `HAL_I2C_Mem_Read(&hi2c1, ...)` 처럼 핸들 포인터를 받는 이유도 같다 — 그 핸들이 가짜를 고르는 열쇠가 된다. 이제 `LinkBusN` 은 빈 타입이 아니라서 `BusSlot` 은 참조 쪽을 고른다.

</details>

### 연습 3 — CMock 식 엄격한 기대 목 `ExpectBus`

`expect_read(addr, reg, reply)`, `expect_write(addr, reg, value)` 로 기대 큐를 쌓고, 실제 호출이 기대와 다르면 `mismatched`, 기대가 바닥났는데 오면 `unexpected`, 끝났는데 남은 기대는 `missing()` 으로 세는 목을 써라. 올바른 init 은 통과하고, CONFIG 를 먼저 쓰는 init 은 걸려야 한다.

<details>
<summary>답 보기</summary>

```cpp
const Exp* next(Kind k, uint8_t a, uint8_t reg, uint16_t v, bool check_value) {
    if (pos_ >= n_) { ++unexpected; return nullptr; }
    const Exp& e = q_[pos_++];
    if (e.kind != k || e.addr != a || e.reg != reg || (check_value && e.value != v)) { ++mismatched; return nullptr; }
    return &e;
}
bool verify() const { return unexpected == 0 && mismatched == 0 && missing() == 0; }
```

기대 밖·불일치 호출에는 NACK 로 대답한다 — 드라이버의 오류 경로를 타게 해서 테스트가 멈추지 않게. 4가지 확인: 올바른 순서 → `verify()`; 순서가 바뀐 `init_config_first` → `mismatched ≥ 1`; 기대 2개만 적으면 3번째 호출이 `unexpected` (재시도로 3번); ID 가 틀려 BadId 에서 멈추면 CONFIG 쓰기 기대가 `missing() == 1`. 같은 검증을 상태 기반 가짜로는 "로그의 0번이 ID 읽기" 한 줄로 했다 — 순서가 정말 요구사항일 때만 엄격한 목을 쓴다.

</details>

### 연습 4 — C 식 ops 테이블을 네 번째 이음매로

TEST-B04 의 함수 포인터 주입을 C++ concept 에 꽂아라: `struct I2cOps { BusErr (*write_read)(void* ctx, ...); BusErr (*write)(void* ctx, ...); };` 를 `constexpr` 테이블로 두고, `OpsBus{const I2cOps*, void* ctx}` 어댑터가 `I2cBus` 를 만족하게.

<details>
<summary>답 보기</summary>

```cpp
struct OpsBus {
    const I2cOps* ops; void* ctx;
    BusErr write_read(uint8_t a, uint8_t r, std::span<uint8_t> rx) { return ops->write_read(ctx, a, r, rx.data(), rx.size()); }
    BusErr write(uint8_t a, std::span<const uint8_t> tx) { return ops->write(ctx, a, tx.data(), tx.size()); }
};
constexpr I2cOps kMiniOps = {
    [](void* c, uint8_t a, uint8_t r, uint8_t* rx, size_t n) { return static_cast<MiniTmp*>(c)->write_read(a, r, {rx, n}); },
    [](void* c, uint8_t a, const uint8_t* tx, size_t n) { return static_cast<MiniTmp*>(c)->write(a, {tx, n}); },
};
```

캡처 없는 람다는 함수 포인터로 변환되고, C++17 부터 그 변환이 `constexpr` 라 테이블이 상수 초기화된다 (플래시에 놓일 수 있다). 이것은 손으로 만든 vtable 이다: `OpsBus` 는 포인터 2개(테이블 + ctx) = vptr 1개 + `this` 와 같은 몫이고 `is_polymorphic` 은 false. virtual 과 비교한 장점은 C 코드와 테이블을 공유할 수 있다는 것(레거시 HAL 이 이미 ops 구조체를 쓰는 경우), 단점은 타입 안전성(`void*` 캐스트)을 사람이 지켜야 한다는 것. NACK 2번 → 재시도 2번으로 25 °C — 같은 드라이버 본체가 네 번째 이음매에서도 그대로 동작한다.

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice   # 23/23 passed
c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer practice.cpp -o pr_san && ./pr_san   # 23/23, 보고 없음
```

## 10. 핵심 정리

- 드라이버 본체는 `template <I2cBus Bus>` 로 한 번 쓰고, 이음매(템플릿 / virtual / 링크 심)는 조립 지점이 고른다. 세 Rig 에 같은 `TEST_CASE_TEMPLATE` — 진짜 doctest 로도 그대로 컴파일된다.
- 가짜는 작은 장치다: 레지스터 파일 + 스크립트 + 오류 주입 큐 + 호출 로그. 반환 코드뿐 아니라 시도 횟수·버스 침묵까지 CHECK 해야 재시도 버그가 잡힌다 (변이 6개 모두 검출, 하나는 테스트 보강 후).
- 실측 (M4, GCC 14.2, gc): `.text` 템플릿 524 / virtual 776 / 링크 심 740 (`-Os` 480 / 658 / 622). LTO 면 링크 심 508 ≈ 템플릿 512, virtual 은 추측 탈가상화로 오히려 820. clang `-O2` 오브젝트에선 템플릿이 가장 컸다 — "제로 비용" 은 크기 보장이 아니다.
- virtual 비용: vtable 16 B, 객체마다 vptr 4 B, `ldr`×3 + `blx`, `this` 로 인자 하나 밀림, `-frtti` 면 +8.4 KB. 소멸자는 protected 비가상.
- 링크 심: 소스 수정 0, 대신 전역 가짜 reset, 바이너리당 가짜 하나, 인스턴스는 인자로, `.a` 는 중복 에러를 숨기고 GNU ld 는 순서 의존, 툴체인 혼합은 맹글링(`int32_t`)으로 깨진다.
- 선택: 기본 템플릿, 실행 시점 선택·컴파일 방화벽이면 virtual, 못 고치는 레거시·벤더 HAL 이면 링크 심(+LTO).

## 참고자료

- Michael Feathers, *Working Effectively with Legacy Code* (2004), 4장 "The Seam Model" — 이음매·enabling point, 전처리/링크/객체 이음매
- James Grenning, *Test-Driven Development for Embedded C* (Pragmatic, 2011) — link-time / function-pointer / preprocessor substitution, 가짜와 테스트 더블 (TEST-B03/B04 의 출처)
- doctest — `TEST_CASE_TEMPLATE`, 식 분해, `-fno-exceptions` 지원: <https://github.com/doctest/doctest> (이 레슨은 v2.4.11 로 확인)
- GoogleTest typed tests (`TYPED_TEST_SUITE`): <https://google.github.io/googletest/advanced.html#typed-tests>
- C++ 표준 (N4861 C++20 초안): [temp.explicit] (명시적 인스턴스화 선언과 inline 예외), [temp.names]/[temp.constr] (제약 불만족 템플릿 id), [expr.prim.req] (requires-expression), [class.abstract], [class.dtor], [dcl.attr.nouniqueaddr], [basic.def.odr] (정의 하나 규칙), [conv.integral] — <https://isocpp.org/files/papers/N4861.pdf>
- GNU ld 매뉴얼 — 아카이브 검색 순서 (`-l`/아카이브는 나타난 위치에서 미정의 심볼만 채움, `--start-group`): <https://sourceware.org/binutils/docs/ld/Options.html>
- Procedure Call Standard for the Arm Architecture (AAPCS32) — r0–r3 인자 전달, 나머지는 스택: <https://github.com/ARM-software/abi-aa/releases>
- Itanium C++ ABI — vtable 배치(offset-to-top, typeinfo, 가상 함수 슬롯): <https://itanium-cxx-abi.github.io/cxx-abi/abi.html#vtable>
- TI TMP117 데이터시트 (레지스터 맵 대표값의 출처, 원문 대조 안 함): <https://www.ti.com/product/TMP117> · Sensirion SHT3x 데이터시트 (CRC-8 0x31/0xFF 예시): <https://sensirion.com/products/catalog/SHT31-DIS-B>
- 관련 레슨: TEST-B04 (C 의 링크 심·함수 포인터·전처리기 이음매), TEST-B05 (CMock 식 기대 큐 생성기), TEST-A03 (mutation testing), TEST-M04 (결함 주입), CPP-M06 (vtable·typeinfo·deleting dtor 회계), CPP-M05 (CRTP·템플릿 팽창), CPP-A05 (thin template), CPP-M11 (Result·오류 범주), CPP-B13 (`-fno-rtti`), CPP-M12 (delegate — 함수 포인터 + 객체 포인터), CPP-A10 (센서 파이프라인 종합)
