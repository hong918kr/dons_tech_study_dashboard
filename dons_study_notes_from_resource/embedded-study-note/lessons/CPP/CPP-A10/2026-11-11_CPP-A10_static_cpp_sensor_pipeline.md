---
id: CPP-A10
time: 150분
updated: 2026-11-11
---
# CPP-A10 종합: 정적 할당 C++ 센서 파이프라인

## 학습 목표

- mock I2C 버스 → IMU 드라이버 → 고정소수점(fixed-point) 필터 → 텔레메트리 프레임을 **힙 0 · 예외 0** 으로 엮고, 층마다 따로 검사하는 테스트와 전체를 가상 시간으로 돌리는 시나리오 테스트를 쓴다.
- 앞 레슨의 부품(Reg 권한 CPP-M01, static_vector CPP-M10, Result CPP-M11, RAII 가드 CPP-B06, constexpr CRC 표 CPP-M13)을 **소형판으로 다시 만들며** 어디를 줄였고 그 대가가 무엇인지 말할 수 있다.
- Q15 곱셈의 반올림·포화, 1차 IIR 의 소수 비트와 데드밴드(dead band)를 설명하고, double 기준 모델과의 **오차 한계를 유도해서 테스트로 확인** 한다.
- 리틀 엔디언 패킹 + CRC-32 + COBS 프레임을 독립 오라클(Python `zlib`/`struct`)로 만든 golden 바이트와 비교하고, 디코더가 모든 1비트 오류를 거르는지 확인한다.
- Cortex-M4 이미지의 `.text/.rodata/.data/.bss` 와 컴포넌트별 크기를 **실측** 해 8 KB 예산과 비교하고(`budget.sh`), `new` 한 줄이 이미지를 어떻게 바꾸는지 보인다.

## 1. 문제

"센서 하나 붙여서 데이터 올리는 펌웨어를 C++ 로 짜 보세요. 힙 금지, 예외 금지, 플래시 8 KB." — 시니어 펌웨어 면접의 마지막 과제나 take-home 이 이런 모양이다. 개별 기법은 이미 배웠다. 이 레슨은 그것들을 **한 스택으로 엮을 때 생기는 판단** — 에러를 어느 층에서 잡고 어느 층으로 올릴지, 재시도는 누가 할지, 숫자를 어떤 형식으로 들고 다닐지, 바이트를 어떻게 선에 실을지, 크기와 RAM 을 어떻게 증명할지 — 를 연습한다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">한 샘플이 지나가는 길 (Pipeline::step 한 번)</text>
  <rect x="10" y="40" width="120" height="58" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="20" y="62" fill="currentColor">IMU (mock)</text>
  <text x="20" y="82" fill="currentColor" font-family="monospace" font-size="12">regs[128]+장애</text>
  <rect x="160" y="40" width="130" height="58" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="170" y="62" fill="currentColor">bus::xfer</text>
  <text x="170" y="82" fill="currentColor" font-family="monospace" font-size="12">BusLock + Error</text>
  <rect x="320" y="40" width="130" height="58" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="330" y="62" fill="currentColor">imu::read_sample</text>
  <text x="330" y="82" fill="currentColor" font-family="monospace" font-size="12">STATUS → 12 B</text>
  <rect x="480" y="40" width="120" height="58" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="490" y="62" fill="currentColor">dsp 필터</text>
  <text x="490" y="82" fill="currentColor" font-family="monospace" font-size="12">Q15 IIR → mg</text>
  <rect x="620" y="40" width="110" height="58" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="630" y="62" fill="currentColor">tlm 프레임</text>
  <text x="630" y="82" fill="currentColor" font-family="monospace" font-size="12">LE+CRC+COBS</text>
  <path d="M130 69 H158 M290 69 H318 M450 69 H478 M600 69 H618" stroke="currentColor" stroke-width="2"/>
  <text x="10" y="128" fill="currentColor" font-weight="bold">실패하면 어디서 무엇이 되나</text>
  <g fill="currentColor" font-size="12">
    <text x="10" y="150">• 버스 Timeout/DataNack/ArbLost → retry_transient 가 1번 더. AddrNack 은 재시도 안 함</text>
    <text x="10" y="170">• STATUS 에 새 데이터 없음 (NotReady) → 0 바이트, 상태 변화 없음 (오류 아님)</text>
    <text x="10" y="190">• 재시도 소진 → err_count++ , 다음 프레임 flags.bit0 = 1 (수신 측이 "그 사이 오류" 를 안다)</text>
    <text x="10" y="210">• 출력 버퍼 부족 → NoSpace, 반쪽 프레임 금지, seq 는 이미 증가 → 수신 측이 seq 틈으로 유실을 안다</text>
    <text x="10" y="230">• 선 위 비트 오류 → 수신 측 decode_frame 이 BadCrc (CRC-32 는 1비트 오류를 전부 잡는다)</text>
  </g>
</svg>
```

재사용하는 부품과 이 레슨에서의 모양 (각 레슨이 주인 — 여기선 다시 가르치지 않고 **줄인 판** 을 쓴다):

| 부품 | 주인 레슨 | 이 레슨의 소형판 | 줄인 대가 |
|---|---|---|---|
| `Reg<Addr,T,Access>` | CPP-M01 | `DevReg<Addr,Access>` — I2C 원격 레지스터. 권한은 타입, 버스는 **객체** | MMIO 와 달리 읽기가 실패할 수 있다 → `Result` 반환 |
| `static_vector<T,N>` | CPP-M10 | trivially copyable T 전용, `std::array` + 크기 | 비자명 T 불가 (`static_assert`) |
| `Result<T,E>` | CPP-M11 | trivial T 전용, `A10_TRY` (표준 C++ 만) | union·수명 관리 없음, 에러 계층 대신 `Error{code, bus, reg}` 한 덩어리 |
| RAII 가드 | CPP-B06 | `BusLock` — 공유 I2C 버스 소유권 | — |
| constexpr CRC 표 | CPP-M13 | 같은 CRC-32 표 (1 KB `.rodata`) | 예산의 1/3 → 연습 1 |

### Part A — 층마다 핵심 하나씩 (A1–A7)

- **A1 `bus::xfer`** — `BusLock` 으로 버스를 잡고 `[reg, payload...]` 를 쓴 뒤 `rx` 만큼 읽는다. 실패하면 `Error{Bus, 원인, reg}`, 잠금 실패 `Busy`, payload 초과 `BadConfig`. **어떤 return 이든 잠금은 풀린다.**
- **A2 `imu::probe`** — WHO_AM_I 확인. **A3 `imu::configure`** — `CTRL3_C`(BDU|IF_INC) 를 **먼저**, 그다음 `CTRL1_XL`, `CTRL2_G`, 끝으로 0x10–0x12 를 버스트 1번으로 읽어 검증. **A4 `imu::read_sample`** — STATUS 확인 후 12바이트 버스트 → LE int16 6개.
- **A5 `mul_q15`** (half-up 반올림 + 포화), **A6 `LowPass<F>::step`** (소수 비트 F 개를 더 가진 1차 IIR), **A7 `pack_frame`** (26바이트 LE + CRC-32).

### Part B — 무엇이 잘못되나 (B1–B4 + 경계 테스트)

- **B1 `retry_transient`** — 일시적 오류만 최대 `extra` 번 더. 장애 주입 테스트: 버스트 NACK 1번 → 회복, Timeout 2번 → 실패, AddrNack → 재시도 0, 남이 쥔 잠금 → `Busy` 이고 남의 잠금을 풀지 않음, 응답 비트 오류 → `BadId`/`BadConfig`.
- 고정소수점 경계: `(-1)×(-1)`, ±0.5 LSB 반올림의 비대칭, 극단 교대 입력, F=0 의 데드밴드, double 기준과의 오차 한계, raw→mg 변환 전 범위(65536개) 오차.
- **B2/B3 COBS** 인코드/디코드 (위키백과 예제 + 무작위 400개 round-trip + 잘못된 입력), **B4 `decode_frame`** (길이 → CRC → 버전 순서로 검사).

### Part C — 파이프라인과 타깃 예산

- **C1 `Pipeline::step`** — 위 그림의 규칙 그대로. 시나리오: 50 틱(10 ms 간격) 중 1번 일시 Timeout(회복), 1번 재시도 소진, 1번 새 데이터 없음 → 프레임 48개, 수신 측에서 디코드해 기대 프레임과 **전부** 일치.
- golden frame, 버퍼 부족 시 seq 틈, 부팅 시 probe 실패, 하네스 전체에서 `operator new` 호출 0.
- `budget.sh` — 같은 라이브러리를 Cortex-M4 이미지(`fw_main.cpp` + `fw.ld`)로 링크해 크기·금지 심볼·`.init_array` 를 검사, `.text` > 8 KB 면 exit 1.

## 2. Problem (English)

> **Static-allocation C++ sensor pipeline (no heap, no exceptions, 8 KB `.text`).**
>
> Build `mock I2C bus → IMU driver → fixed-point filter → telemetry frame` for a Cortex-M4 with `-fno-exceptions -fno-rtti`.
>
> 1. `Result<void,Error> bus::xfer(B&, uint8_t dev, uint8_t reg, span<const uint8_t> payload, span<uint8_t> rx)` for any `B` satisfying `concept I2cBus` (`write_read`, `try_lock`, `unlock`). The bus lock must be released on every path. Typed wrappers `read_reg<R>/write_reg<R>/read_burst<R>` must reject writes to read-only registers at compile time.
> 2. IMU driver: `probe` (WHO_AM_I = 0x6C), `configure` (write CTRL3_C = BDU|IF_INC first, then CTRL1_XL/CTRL2_G, verify all three with one burst read), `read_sample` (STATUS gate, one 12-byte burst, little-endian int16).
> 3. `int16_t mul_q15(int16_t, int16_t)` with round-half-up and saturation; `LowPass<F>` first-order IIR with Q15 α and F extra fractional state bits. Prove `|y − y_double| ≤ 0.5 + 2^-(F+1)/α`.
> 4. Frame v1 (26 bytes, LE): `ver u8, flags u8, seq u16, t_ms u32, accel_mg i16×3, gyro i16×3, err_count u16, crc32 u32` (CRC-32/zlib over bytes 0..21), COBS-encoded, terminated by `0x00`. Write the decoder (`Cobs`, `BadLength`, `BadCrc`, `BadVersion`).
> 5. `Pipeline<B>::step(t_ms, static_vector<uint8_t,N>& out)`: retry transient errors once; `NotReady` → 0 bytes; other errors → count + flag the next frame; never write half a frame; `seq` advances when a frame is produced.
>
> Constraints: zero `operator new` calls (replace it with a trap in tests), no `.init_array`, `.text ≤ 8192` bytes on `arm-none-eabi-g++ -Os -mcpu=cortex-m4` with `--gc-sections`.
>
> Follow-ups: Why not `memcpy` the struct onto the wire? Who retries — driver or app? Why COBS and not SLIP? Why fixed point on an M4F? What did one `new` cost? How do you know the stack fits?

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
```

처음 실행 결과 (실제 출력, PASS 줄과 FAIL 63줄 중 대부분 생략):

```text
--- Part A: 드라이버 · 고정소수점 · 프레임 ---
FAIL  starter.cpp:516  id.ok() && id.value() == 0x6C
FAIL  starter.cpp:517  m.last_tx_len == 1 && m.last_tx[0] == 0x0F && m.txns == 1
FAIL  starter.cpp:518  !m.locked() && m.locks == 1 && m.unlocks == 1 && m.unlocked_xfers == 0
...
      LowPass<8> α=0.125: max |y - y_double| = 23205.000000 (bound 0.515625)
FAIL  starter.cpp:664  max_err <= bound
...
FAIL  starter.cpp:813  !r && r.error().code == Code::BadId && m.txns == 1
      sizeof: Pipeline<MockI2c>=72  LowPass<8>=8  Frame=24  Result<Raw,Error>=16
19/82 passed
```

하네스 설계: 스텁은 전부 "틀리지만 안전" 하다 (`fail(...)` 또는 0 반환, 루프 없음). `value()` 는 에러 상태에서 트랩하므로 테스트는 항상 `r.ok() && r.value()...` 로 단락 평가한다. 시작부터 PASS 인 19개는 mock 자체의 성질(IF_INC 를 안 켜면 버스트가 같은 레지스터만 읽는다 등)과 주어진 부분(CRC, `to_mg`, `alpha_q15`)을 고정하는 검사다. 파일 맨 위 `#ifndef A10_LIB_ONLY` 아래가 호스트 전용(mock·하네스·힙 트랩)이고, 위쪽 라이브러리만 타깃 이미지에 들어간다.

관찰용 파일 (하네스와 별개, 결과는 5. 풀이에 실제 출력으로):

```bash
python3 golden_check.py                 # 독립 오라클: struct.pack + zlib.crc32 + 따로 쓴 COBS → golden 바이트
bash budget.sh                          # Lima VM elinux 의 arm-none-eabi-g++ 14.2 로 M4 이미지, 예산 검사 (exit 0/1)
A10_EXTRA=-DA10_DEMO_HEAP bash budget.sh   # new 한 줄 넣은 이미지 → exit 1 이 정상
bash asm_targets.sh size | asm | crc    # 맥 clang (recipe b): 함수별 크기, LowPass asm, CRC 세 구현 크기
c++ -std=c++20 -Wall -Wextra -Werror -fno-exceptions -fno-rtti -fsyntax-only -DSHOW_ERROR=1 demo_errors.cpp   # 1..5
c++ -std=c++20 -O2 -fno-exceptions -fno-rtti demo_heap_trap.cpp -o heap_trap && ./heap_trap
```

## 4. 힌트

<details>
<summary>힌트 1 — xfer 의 모든 return 경로에서 잠금이 풀리게 하는 방법</summary>

`try_lock()`/`unlock()` 을 손으로 짝지으면 에러 return 하나를 놓친다. `BusLock<B> lock(b);` 를 만들고 `lock.owned()` 만 확인하라. 남이 쥔 잠금을 못 잡았으면 `owned_ == false` 라 소멸자가 **풀지 않는다** — 이게 테스트의 "남의 잠금은 안 푼다" 다.

</details>

<details>
<summary>힌트 2 — configure 순서와 검증 버스트</summary>

버스트 읽기는 IF_INC 가 켜져 있어야 주소가 증가한다. 그래서 CTRL3_C 가 첫 쓰기다. 검증은 0x10 부터 3바이트를 한 번에 읽으면 CTRL1_XL, CTRL2_G, CTRL3_C 가 순서대로 온다 — 트랜잭션 4개(쓰기 3 + 읽기 1)로 끝난다.

</details>

<details>
<summary>힌트 3 — LowPass 의 정수 범위</summary>

상태 `s` 는 출력 × 2^F. `d = x·2^F − s` 의 절댓값은 최대 65535·2^F 이고 F ≤ 15 면 int32 에 들어간다. `d·α` 는 최대 2^31·2^15 이라 int64 로 곱하고, `+2^14` 후 `>> 15` (C++20 에서 음수 `>>` 는 산술 시프트로 정의). 첫 샘플은 상태를 입력으로 채워야 시작 과도응답이 없다.

</details>

<details>
<summary>힌트 4 — COBS 의 254 경계</summary>

코드 바이트 = "다음 0 까지의 거리". 블록에 비영 바이트가 254개 차면 코드 0xFF 로 닫고 **0 을 붙이지 않는다** (디코더도 0xFF 블록 뒤에는 0 을 넣지 않음). 입력이 정확히 254개 블록에서 끝나면 새 블록을 열지 않는다.

</details>

<details>
<summary>힌트 5 — step 의 상태 변화 순서</summary>

샘플을 얻은 뒤에만 필터를 돌린다. 프레임을 만들면 seq 를 올린다(버퍼에 못 넣어도). 에러 플래그는 **out 에 다 넣은 뒤에만** 내린다 — 못 보낸 프레임이 플래그를 삼키면 안 된다.

</details>

## 5. 풀이

### Part A — 층마다 하나의 책임

**A1 `xfer`: 잠금·버퍼·문맥.**

```cpp
if (payload.size() > kMaxPayload) return fail(Error{Code::BadConfig, BusErr::None, reg});
BusLock<B> lock(b);
if (!lock.owned()) return fail(Error{Code::Busy, BusErr::None, reg});
std::array<std::uint8_t, 1 + kMaxPayload> tx{};          // 스택 10바이트 — 힙도, VLA 도 아님
tx[0] = reg;
for (std::size_t i = 0; i < payload.size(); ++i) tx[1 + i] = payload[i];
const auto r = b.write_read(dev, std::span<const std::uint8_t>(tx.data(), 1 + payload.size()), rx);
if (!r) return fail(Error{Code::Bus, r.error(), reg});    // 어느 레지스터에서였는지 붙인다
return {};
```

| 결정 | 이유 |
|---|---|
| 잠금 = RAII (`BusLock`) | 에러 return 이 4곳. 수동 `unlock()` 은 하나를 놓친다 — 뮤턴트 "수동 잠금" 은 6개 CHECK 로 잡혔다 (6. 정답 코드) |
| `Error.reg` 문맥 | 로그에 "Timeout" 만 있으면 쓸모없다. "STATUS(0x1E) 읽다 Timeout" 이어야 한다. 계층 매핑은 CPP-M11 |
| payload 상한 `kMaxPayload = 8` | 버퍼 크기가 타입에 있어야 RAM 이 정적으로 정해진다. 넘치면 `BadConfig` (자르지 않는다) |
| `I2cBus` concept + 템플릿 | 호스트는 `MockI2c`, 타깃은 `HwI2c` — 같은 드라이버 코드. 가상 함수·링크 심과의 비교는 CPP-A08 |

권한은 CPP-M01 처럼 타입에 있다. `write_reg<WHO_AM_I>` 는 오버로드 후보에서 빠진다 (`demo_errors.cpp -DSHOW_ERROR=1`, Apple clang 21 실제 출력):

```text
demo_errors.cpp:18:11: error: no matching function for call to 'write_reg'
./solution.cpp:153:21: note: candidate template ignored: constraints not satisfied [with R = a10::imu::WHO_AM_I, B = NullBus]
./solution.cpp:152:39: note: because 'DevReg<'\x0f', a10::bus::RO>::access::can_write' evaluated to false
```

하네스는 같은 사실을 `static_assert(!CanWrite<imu::WHO_AM_I>)` (requires-expression) 로 빌드마다 확인한다. `-DSHOW_ERROR=2` (Result 무시) 는 `ignoring return value of type 'Result<void, a10::Error>' declared with 'nodiscard' attribute [-Werror,-Wunused-value]`, `=5` (`Pipeline<NotABus>`) 는 `because 'NotABus' does not satisfy 'I2cBus'` … `too many arguments to function call, expected 1, have 3` 였다.

**A2–A4 드라이버.** 레지스터 맵은 **대표값** 이다 (LSM6DSO 류를 본떴지만 데이터시트로 확인하지 않음: WHO_AM_I 0x0F=0x6C, CTRL1_XL 0x10, CTRL2_G 0x11, CTRL3_C 0x12, STATUS 0x1E, 출력 0x22–0x2D 자이로 먼저). 의미만 정리하면:

- **IF_INC** — 버스트 읽기에서 주소 자동 증가. mock 은 리셋값을 0 으로 뒀다 (실제 칩 리셋값 미확인 — 그래서 드라이버는 리셋값에 기대지 않고 명시적으로 켠다). IF_INC 없이 버스트를 읽으면 **같은 레지스터 12번** 이 오는데 I2C 는 그걸 오류로 모른다 — 테스트 `g[0] == 0x0202 && a[2] == 0x0202` ("성공한 쓰레기").
- **BDU** (block data update) — 하위 바이트를 읽은 뒤 상위 바이트를 읽기 전에 새 샘플로 바뀌는 것을 막는 기능 (류의 칩에서 흔한 의미, 이 mock 은 모델링하지 않음). 16비트 값을 바이트 둘로 읽는 모든 장치의 tearing 문제다 (C-M14 의 64비트 tearing 과 같은 구조).
- **LE 해석** — `static_cast<int16_t>(uint16_t(p[0] | p[1] << 8))`. `uint16 → int16` 범위 밖 변환은 C++20 부터 모듈러로 **정의됨** ([conv.integral], P1236). `memcpy` 로 `int16_t` 에 복사하는 방법은 호스트 엔디언에 묶인다.

**A5–A6 고정소수점.** Q15 = 부호 있는 16비트를 [-1, 1) 로 읽는 형식 (1 LSB = 2^-15).

| `mul_q15(a, b)` | 실수 값 | 결과 | 이유 |
|---|---|---|---|
| 16384 × 16384 | 0.5 × 0.5 = 0.25 | 8192 | 정확 |
| -32768 × -32768 | (-1)(-1) = +1 | **32767** | +1 은 Q15 로 표현 불가 → 포화. 포화 없으면 -32768 (부호 뒤집힘) |
| 1 × 16384 / -1 × 16384 | +0.5 / -0.5 LSB | 1 / 0 | half-up (+2^14 후 floor) 은 **비대칭**: -0.5 는 0 쪽으로 |
| 3 × 16384 / -3 × 16384 | +1.5 / -1.5 LSB | 2 / -1 | 같은 이유. 대칭이 필요하면 round-half-away 나 convergent(짝수) 반올림 |

`LowPass<F>` 는 `y += α(x − y)` 를 상태 `s = y·2^F` 로 돈다:

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">LowPass&lt;8&gt;::step — 비트 폭과 반올림 위치</text>
  <rect x="10" y="38" width="150" height="40" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="18" y="63" fill="currentColor" font-family="monospace" font-size="12">x·2^8  (int32)</text>
  <rect x="190" y="38" width="160" height="40" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="198" y="63" fill="currentColor" font-family="monospace" font-size="12">d = x·2^8 − s</text>
  <rect x="380" y="38" width="190" height="40" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="388" y="63" fill="currentColor" font-family="monospace" font-size="12">(d·α + 2^14) &gt;&gt; 15</text>
  <rect x="600" y="38" width="130" height="40" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="608" y="63" fill="currentColor" font-family="monospace" font-size="12">s += …</text>
  <path d="M160 58 H188 M350 58 H378 M570 58 H598" stroke="currentColor" stroke-width="2"/>
  <rect x="380" y="100" width="190" height="40" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="388" y="125" fill="currentColor" font-family="monospace" font-size="12">sat16((s + 128) &gt;&gt; 8)</text>
  <path d="M665 78 V120 H572" stroke="currentColor" stroke-width="2" fill="none"/>
  <g fill="currentColor" font-size="12">
    <text x="10" y="112">|d| ≤ 65535·2^8 → int32 OK</text>
    <text x="10" y="130">d·α → int64 (M4: smlal 1개)</text>
    <text x="10" y="165">상태 반올림 오차 ≤ 2^-9 LSB/샘플, 감쇠 (1−α) 로 누적 → ≤ 2^-9/α</text>
    <text x="10" y="185">출력 반올림 ≤ 0.5 LSB → 한계 0.5 + 2^-(F+1)/α = 0.515625 (α = 0.125)</text>
    <text x="10" y="205">F = 0 이면 |α(x−y)| &lt; 0.5 에서 더 못 움직인다 → 데드밴드 ≈ 0.5/α LSB</text>
  </g>
</svg>
```

오차 한계 유도: 기준 모델은 **양자화된 α** 를 쓴 double IIR 이다 (계수 양자화와 산술 오차를 섞지 않으려고). 고정소수점 상태 오차 `e_n = (1−α)e_{n−1} + r_n`, `|r_n| ≤ 0.5·2^-F` 이므로 `|e| ≤ 0.5·2^-F / α`. 출력 반올림 0.5 를 더하면 `0.5 + 2^-(F+1)/α`. 실측 (20000 샘플, 절반은 전 범위 무작위, 절반은 ±1000):

```text
      LowPass<8> α=0.125: max |y - y_double| = 0.506877 (bound 0.515625)
      to_mg ±2g (k=3998): max |mg - raw*0.061| = 0.650000 (bound 0.75)
```

`to_mg` 는 Q16.16 배율 `k = round(61 µg × 65536 / 1000) = 3998` (감도 0.061 mg/LSB 는 대표값). 한계 = 출력 반올림 0.5 + 배율 양자화 `|raw|·(0.5/65536)` ≤ 0.25 → 0.75. raw 65536개 전부를 돌린 결과다. `k < 2^15` 이면 `raw·k < 2^30` 이라 32비트 곱 하나로 충분하다 — `static_assert(scale_q16(488) < 32768)` 가 ±16g 까지 그걸 지킨다.

`α` 는 `consteval alpha_q15(5.0, 104.0)` = 7602 (Python 으로도 7602, α ≈ 0.232). `std::exp` 는 C++20 에서 constexpr 이 아니라서 RC 이산화 `ω/(ω+1)` 를 썼다. 범위 밖이면 정의 없는 non-constexpr 함수를 불러 컴파일을 멈춘다 (`-DSHOW_ERROR=3` 실제 출력: `non-constexpr function 'alpha_out_of_range' cannot be used in a constant expression` → `call to consteval function 'a10::dsp::alpha_q15' is not a constant expression`). 메시지를 더 다듬는 기법은 CPP-M14, 클럭 트리 같은 큰 constexpr 계산은 CPP-A06.

실제 M4 코드 (`budget.sh` 의 ELF, GCC 14.2 `-Os`, `objdump -d` 그대로):

```text
0800005c <a10::dsp::LowPass<8>::step(short)>:
 800005c:	mov	r3, r0
 800005e:	lsls	r2, r1, #8              @ x·2^8
 8000060:	mov	r0, r1
 8000062:	ldrb	r1, [r3, #6]            @ primed_
 8000064:	push	{r4, r5, lr}
 8000066:	cbnz	r1, 8000070
 ...                                        @ (첫 샘플: s = x·2^8, primed = 1)
 8000070:	ldr	r1, [r3, #0]            @ s
 8000072:	ldrh	r5, [r3, #4]            @ α
 8000074:	subs	r0, r2, r1              @ d
 8000076:	movs	r4, #0
 8000078:	mov.w	r2, #16384              @ 반올림 상수 2^14 를 누산기 초기값으로
 800007c:	smlal	r2, r4, r0, r5          @ 64비트 d·α + 2^14 — 명령 1개
 8000080:	lsrs	r2, r2, #15
 8000082:	orr.w	r2, r2, r4, lsl #17     @ 64비트 >> 15 의 하위 32비트
 8000086:	add	r1, r2
 8000088:	add.w	r0, r1, #128            @ 출력 반올림
 800008c:	ssat	r0, #16, r0, asr #8     @ (>>8) + int16 포화 — 명령 1개
 8000090:	str	r1, [r3, #0]
 8000092:	sxth	r0, r0
 8000094:	b.n	800006e
```

`int64_t` 곱은 라이브러리 호출(`__aeabi_lmul`)이 아니라 `smlal` 하나, `sat16` 의 두 비교는 `ssat` 하나가 됐다. 맥 clang 21 (`bash asm_targets.sh asm`)도 같은 `smlal … ssat r1, #0x10, r0, asr #8` 를 냈다. "고정소수점은 느리다/복잡하다" 가 아니라 **M4 의 DSP 명령으로 그대로 내려간다** 는 증거다.

**A7 `pack_frame`.** 왜 `memcpy(&wire, &frame, sizeof frame)` 가 아닌가 — 필드 합은 22 B 인데 실측 `sizeof(Frame) == 24` — `uint32_t t_ms` 때문에 구조체 정렬이 4 라 끝에 패딩 2 B 가 붙는다(이 필드 순서에선 중간 패딩은 없지만, 필드 하나만 옮겨도 생긴다). 구조체를 그대로 보내면 값이 정해지지 않은 패딩 바이트가 선에 나가고, 엔디언은 CPU 를 따르며, 레이아웃은 컴파일러·ABI 가 정한다. 와이어 형식은 **오프셋 표** 이고 `put_le16/put_le32` 시프트는 어떤 CPU 에서도 같은 바이트를 만든다.

```svg
<svg viewBox="0 0 740 150" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">와이어 프레임 v1 (26 B, LE) → COBS (+1) → 0x00 (+1) = 28 B</text>
  <g stroke="currentColor">
    <rect x="10" y="32" width="26" height="36" class="f-mut"/><rect x="36" y="32" width="26" height="36" class="f-mut"/>
    <rect x="62" y="32" width="52" height="36" class="f-acc"/><rect x="114" y="32" width="104" height="36" class="f-mut"/>
    <rect x="218" y="32" width="156" height="36" class="f-ok"/><rect x="374" y="32" width="156" height="36" class="f-ok"/>
    <rect x="530" y="32" width="52" height="36" class="f-warn"/><rect x="582" y="32" width="104" height="36" class="f-acc"/>
  </g>
  <g fill="currentColor">
    <text x="14" y="55">ver</text><text x="38" y="55">flg</text><text x="70" y="55">seq</text><text x="140" y="55">t_ms</text>
    <text x="240" y="55">accel_mg ×3</text><text x="400" y="55">gyro ×3</text><text x="536" y="55">errs</text><text x="608" y="55">crc32</text>
    <g font-family="monospace">
      <text x="14" y="86">0</text><text x="40" y="86">1</text><text x="66" y="86">2</text><text x="118" y="86">4</text>
      <text x="222" y="86">8</text><text x="378" y="86">14</text><text x="534" y="86">20</text><text x="586" y="86">22..25</text>
    </g>
    <text x="10" y="112">CRC-32 (zlib) 은 바이트 0..21 위에. 수신: 0x00 으로 자르기 → COBS 해제 → 길이 26 → CRC → 버전 → 필드.</text>
    <text x="10" y="132">golden: 01 00 34 12 EF CD AB 89 E8 03 FE FF 00 40 00 80 FF 7F 00 00 02 01 | 5B FE 2E B5</text>
  </g>
</svg>
```

golden 은 C++ 이 아닌 독립 오라클로 만들었다 (`python3 golden_check.py` 실제 출력):

```text
crc32('123456789') = 0xCBF43926
frame (26 B): 01 00 34 12 ef cd ab 89 e8 03 fe ff 00 40 00 80 ff 7f 00 00 02 01 5b fe 2e b5
crc32 = 0xB52EFE5B
wire  (28 B): 02 01 0b 34 12 ef cd ab 89 e8 03 fe ff 02 40 04 80 ff 7f 01 07 02 01 5b fe 2e b5 00
alpha_q15(5,104) = 7602  (alpha = 0.231996)
scale_q16(61) = 3998
```

`struct.pack('<BBHI3h3hH')` 가 패킹을, `zlib.crc32` 가 CRC 를 독립적으로 정한다. 하네스의 `kGoldenWire` 는 이 바이트를 그대로 옮긴 것이고 C++ 출력과 일치했다. 오라클의 COBS 자체 검사 벡터는 위키백과 예제 표와 대조했다(2026-11-11 확인).

### Part B — 무엇이 잘못되나

**재시도는 누가, 몇 번, 무엇을.**

| 오류 | 일시적? | 이 설계 | 근거 |
|---|---|---|---|
| `Timeout`, `DataNack`, `ArbLost` | 예 | 1번 더 | 노이즈, 다른 마스터, 순간 클럭 스트레칭 |
| `AddrNack` | 아니오 (이 장치에선) | 즉시 실패 | 장치 없음/전원 꺼짐 — 재시도는 시간 낭비. EEPROM 처럼 쓰기 사이클 중 NACK 하는 장치면 정책이 달라진다 |
| `BadId`, `BadConfig` | 아니오 | 즉시 실패 | 다른 칩, 배선 오류 — 재시도로 안 고쳐짐 |
| `NotReady` | 오류 아님 | 0 바이트 | 폴링 주기가 ODR 보다 빠르면 정상 상황 |

재시도는 **트랜잭션 단위가 아니라 `read_sample` 단위** 다 (`retry_transient([&]{ return imu::read_sample(bus_); }, 1)`). 버스트에서 NACK 이 나면 STATUS 부터 다시 읽는다 — 그 사이에 새 샘플이 왔을 수 있기 때문이다. 테스트: 버스트 NACK → 트랜잭션 4개 (STATUS+버스트 ×2) 후 원래 샘플, Timeout 2연속 → 2개 후 `Error{Bus, Timeout, 0x1E}`, AddrNack (`extra = 3` 이어도) → 1개. 재시도 루프에 "일시적인가" 조건을 빼먹는 뮤턴트는 AddrNack 테스트 하나로 잡혔다.

어디서 재시도하나: 드라이버 안에 넣으면 모든 호출자가 같은 정책을 강제로 받고, 앱에 두면 앱이 버스 오류 종류를 알아야 한다. 이 설계는 **판정(is_transient)은 bus 층, 정책(몇 번)은 app 층** 이다. 버스가 SDA low 로 묶인 경우(9 클럭 복구)는 컨트롤러 드라이버 몫 — 여기선 다루지 않는다.

**I2C 가 모르는 오류.** I2C 에는 데이터 무결성 검사가 없다. mock 의 `flip_rx` 로 응답 비트를 뒤집으면 트랜잭션은 성공한다. 드라이버가 잡을 수 있는 건 **의미 검사** 뿐이다: WHO_AM_I 비교 → `BadId`, 설정 read-back → `Error{BadConfig, reg = 0x10}` (첫 번째로 틀린 레지스터). 측정값의 비트 오류는 센서 쪽에 CRC 가 없으면 못 잡는다 (일부 센서는 I2C 데이터에 CRC-8 을 붙인다 — 예: Sensirion 류, 이 레슨에서 확인하지 않음).

**고정소수점 경계 (실측 = 테스트 값).**

- 극단 교대 입력 (±32767/-32768, α = 32767/32768): 상태는 입력 범위를 넘지 않고 출력 범위는 [-32768, 32765] — α < 1 이라 위쪽 끝에 못 닿는다. UBSan 빌드에서도 보고 없음.
- F = 0 (소수 비트 없음), α = 0.125: 0 → 100 계단은 **97 에서 멈추고**, 100 → 0 은 **4 에서 멈춘다**. 올라갈 땐 `α·3 = 0.375 → +0`, 내려갈 땐 `α·(−4) = −0.5 → half-up 으로 0`. 데드밴드 크기도 반올림 비대칭 때문에 방향마다 다르다. F = 8 이면 정확히 100 에 도달 (테스트).
- 왜 F = 8 인가: F 를 키우면 한계는 줄지만 (F=15: 0.5 + 2^-16/α) 입력 상한 여유가 없어진다. F = 8 이면 오차 0.516 LSB 로 사실상 "출력 반올림만큼" 이고 int32 여유도 크다.

**COBS (Consistent Overhead Byte Stuffing).** 0x00 을 프레임 구분자로 예약하고, 데이터 안의 0 을 "다음 0 까지 거리" 코드로 바꾼다. 오버헤드는 254바이트당 최대 1바이트 — 26바이트 프레임은 항상 +1 (SLIP 은 최악 2배). 수신 측은 0x00 하나만 보면 언제든 재동기할 수 있다 (연습 2).

| 입력 | 인코드 (구분자 제외) | 확인 |
|---|---|---|
| `00` | `01 01` | 위키백과 예 1 |
| `11 22 00 33` | `03 11 22 02 33` | 예 4 |
| (빈 입력) | `01` | — |
| `01 … FE` (254개) | `FF 01 … FE` (255 B, 꼬리 블록 없음) | 예 7 |
| `01 … FF` (255개) | `FF 01 … FE 02 FF` (257 B) | 예 9 |

`code == 0xFF` 로 블록을 닫을 때 "입력이 더 남았거나 방금 바이트가 0 일 때만" 새 블록을 연다 — 이 조건을 빼면 254개로 끝나는 입력에 꼬리 `01` 이 붙는다 (그것도 디코드는 되는 변형이지만 위키백과 표와 다르다). 그 뮤턴트는 254 경계 테스트 하나로 잡혔다. 무작위 400개 (길이 0–599, 0x00 비율 4단계) round-trip 과 "인코드 결과에 0x00 없음", "길이 ≤ n + n/254 + 1" 을 함께 확인한다.

**디코더의 검사 순서: COBS → 길이 → CRC → 버전.** 버전을 CRC 보다 먼저 보면 깨진 바이트를 "새 버전" 으로 오해한다. 프레임의 208개 1비트 오류(26 B × 8)를 하나씩 넣어 본 결과 **208개 모두 `BadCrc`** — CRC 는 생성 다항식 항이 2개 이상이면 모든 1비트 오류를 검출한다 (CRC-32 는 길이 ≤ 32 의 버스트 오류도 모두 검출 — 이론, 테스트 안 함). 길이를 26+1 버퍼로 디코드하는 이유: 27바이트 이상을 `Cobs`(형식 오류)가 아니라 `BadLength` 로 구별하려고. golden 을 20바이트로 자르면 우연히 COBS 로는 유효해서 `BadLength`, 18바이트면 블록이 끝을 넘어 `Cobs` 다 (둘 다 테스트).

### Part C — 파이프라인, 힙 0, 예산

**`step` 이 바꾸는 상태 (C1 정답의 표):**

| 경우 | 반환 | 필터 | seq | err_count / flag | out |
|---|---|---|---|---|---|
| NotReady | `0` | 그대로 | 그대로 | 그대로 | 그대로 |
| 재시도 소진 | 에러 | 그대로 | 그대로 | +1 / 켬 | 그대로 |
| NoSpace | 에러 | 갱신됨 | **+1** | 그대로 / 유지 | 그대로 (반쪽 금지) |
| 성공 | 28 | 갱신 | +1 | 프레임에 기록 / 끔 | +28 B |

NoSpace 에서 seq 를 올리는 건 의도다: 샘플은 이미 필터에 먹었고 프레임은 버려졌다 → 수신 측에 **틈** 으로 보여야 한다 (테스트: seq 0 다음에 2). "성공했을 때만 seq++" 로 바꾼 뮤턴트는 이 테스트 **하나** 로만 잡혔다 — 정상 경로 테스트만 있었으면 통과했을 버그다.

시나리오 결과 (테스트가 확인한 값): 50 틱 → `idle == 1`, `errors == 1`, 프레임 48개 × 28 B = 1344 B, `p.seq() == 48`, 수신 측 디코드 48개가 기준 필터로 따로 계산한 기대 프레임과 필드까지 전부 같음, 오류 직후 프레임(i = 21 틱, t = 210 ms)만 `flags == 1`, 그 뒤 `err_count == 1` 유지, 잠금 `locks == unlocks`.

**힙 0 — 호스트.** 하네스는 전역 `operator new`/`new[]` 를 트랩으로 바꾼다 (교체 가능 함수, [replacement.functions]). 82개 테스트 끝에 `g_heap_calls == 0`. 트랩이 정말 잡는지는 `demo_heap_trap.cpp` 로 확인 (실제 출력):

```text
before
empty vector: no allocation
HEAP TRAP: operator new(4)
exit=133
```

(133 = 128 + SIGTRAP 5.) 빈 `std::vector` 는 할당하지 않고 첫 `push_back` 이 4바이트를 요청했다.

**힙 0 · 예외 0 — 타깃, 그리고 8 KB.** `bash budget.sh` 실제 출력 (Lima VM, `arm-none-eabi-g++ 14.2.1`, `-Os -mcpu=cortex-m4 -mthumb -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections -Wl,--gc-sections -T fw.ld`; 긴 이름은 잘랐다):

```text
### -O2 (참고)
   3708	     68	     76	   3852	    f0c	fw_O2.elf
### -Os (예산 대상) — Berkeley (text = .isr_vector+.text+.rodata+..., 플래시엔 + data)
   text	   data	    bss	    dec	    hex	filename
   3272	     68	     76	   3416	    d58	fw.elf
### 섹션 (size -A)
.isr_vector         64   134217728
.text             2184   134217792
.rodata           1024   134219976
.data               68   536870912
.bss                76   536870980
  ---- 합계 (컴포넌트)                               text rodata   data    bss vector
  imu (probe·configure·read_sample)               870      0      0      0      0
  기타 (libc·startup)                               156      0      0      0      0
  fw (MMIO I2C·UART·벡터·리셋)                        198      0     68     73     64
  app (Pipeline::step)                            600      0      0      0      0
  bus (xfer)                                      296      0      0      0      0
  dsp (LowPass)                                    58      0      0      0      0
  tlm (CRC·COBS·frame)                              0   1024      0      0      0
### 스택 (-fstack-usage, 함수별 프레임 바이트 — 호출 깊이 합은 아님)
    168 static   a10::Result<unsigned int, a10::Error> a10::app::Pipeline<B>::step(uint32_t, …
     96 static   a10::Result<a10::imu::Raw, a10::Error> a10::imu::read_sample(B&) [with B = a10::fw::HwI2c]
     88 static   a10::Result<void, a10::Error> a10::imu::configure(B&, const Config&) …
     56 static   a10::Result<void, a10::Error> a10::imu::probe(B&) …
     48 static   a10::Result<void, a10::Error> a10::bus::xfer(B&, uint8_t, uint8_t, …
     24 static   void a10::fw::run()
### 예산: .text = 2184 / 8192 바이트
OK: .text 예산 이내
OK: 힙·예외 심볼 없음 (_Znwj/malloc/_sbrk/__cxa_*/_Unwind_*/__gxx_personality_v0/abort)
OK: .init_array 없음 (constinit)
### exit 0
```

읽는 법:

- **`.text` 2184 B / 8192 B (27%).** Berkeley `text` 3272 는 `.isr_vector + .text + .rodata` 합이다 — "text 예산" 을 말할 때 어느 숫자인지 먼저 정해야 한다. 플래시 실사용은 3272 + `.data` 초기값 68 = 3340 B. `-O2` 는 Berkeley 기준 +436 B.
- **컴포넌트 숫자는 인라인 뒤의 숫자다.** `tlm` 의 text 가 0 인 건 `pack_frame`·`cobs_encode`·`crc32` 가 `Pipeline::step`(600 B) 안으로 들어갔기 때문이고, `HwI2c::write_read` 는 `xfer`(296 B) 안에 있다. 층별 크기를 알고 싶으면 `-fno-inline` 빌드를 따로 보거나 clang 오브젝트(아래)처럼 함수가 남는 빌드를 본다.
- **CRC 표 1024 B 가 `.rodata` 의 전부, 플래시의 31%.** 니블 표면 64 B (연습 1).
- **`memset` 156 B 는 누가?** `Reset_Handler` 가 아니라 `Pipeline::step` 의 `bl memset` 3개다 (`objdump` 로 확인). `std::array tx{}`, `static_vector w` 의 `buf_{}`, `Frame f{}` 같은 **값 초기화가 memset 호출** 이 됐다. `{}` 는 공짜가 아니다 — 곧 덮어쓸 버퍼라면 초기화를 빼는 것도 선택지다 (대신 읽기 전 쓰기를 보장해야 한다).
- **`g_pipe` 는 `.bss` 가 아니라 `.data` 68 B.** `constinit` 이라 생성자 코드는 0 이지만 α·배율·버스 참조가 0 이 아니라서 초기값 이미지가 플래시에 들어가고 `Reset_Handler` 가 RAM 으로 복사한다. 그래도 `.init_array` 는 없다 — 동적 초기화 0 (CPP-B09/M13 의 constinit).
- **스택:** 최악 호출 경로 `run(24) → step(168) → read_sample(96) → xfer(48)` = 336 B, 여기에 `Reset_Handler` 8 B 와 SysTick 예외 스택 프레임(FPU 없는 빌드: 8워드 = 32 B)을 더하면 ≈ 376 B. 정적 RAM 144 B (`.data` 68 + `.bss` 76) 와 합쳐 **약 520 B** 가 이 스택의 RAM 이다. 이 합은 `.su` 숫자를 손으로 더한 것이다 — 호출 그래프 도구(`-fcallgraph-info`, puncover 류)나 스택 페인팅으로 확인하는 게 실무 절차다.

RAM 회계 (실측 `sizeof` / nm):

| 객체 | 호스트 arm64 | M4 | 비고 |
|---|---|---|---|
| `Pipeline<…>` | 72 B | 68 B (`g_pipe`) | 버스 참조 8→4 B. `LowPass<8>` 8 B × 6 = 48 B 가 대부분 |
| `static_vector<uint8_t,64>` | — | 68 B (`g_tx`) | 64 + `size_t` 4 |
| `Result<Raw,Error>` | 16 B | — | 값 12 + Error 3 + bool 1. AAPCS32 에선 4 B 넘는 복합 타입이라 메모리로 반환 (CPP-M11/B13) |
| `HwI2c` / `g_ms` | — | 1 / 4 B | |

**`new` 한 줄의 값 — `A10_EXTRA=-DA10_DEMO_HEAP bash budget.sh`.** `run()` 첫 줄에 `new int(1)` 을 넣으면 처음에는 **링크가 안 됐다** (`fw.ld` 에 아직 언와인더용 심볼이 없던 판, 실제 출력 일부):

```text
libgcc.a(unwind-arm.o): in function `get_eit_entry': … undefined reference to `__exidx_start'
libnosys.a(sbrk.o): in function `_sbrk': … undefined reference to `end'
```

`-fno-exceptions` 로 컴파일한 우리 코드가 아니라 **libstdc++ 의 `operator new` 가 `bad_alloc` 을 던질 수 있게 빌드돼 있어서** 언와인더와 `_sbrk` 가 끌려온 것이다. 링커 스크립트에 `__exidx_start/__exidx_end` 와 `end` 를 주자 링크는 됐고, 검사가 이렇게 실패했다 (실제 출력 일부):

```text
  16392	   1416	    488	  18296	   4778	fw.elf
.text                                                            14264   134217792
.ARM.extab.text.__gxx_personality_v0                                36   134233612
.ARM.exidx                                                         400   134233720
### 예산: .text = 14264 / 8192 바이트
FAIL: .text 예산 초과
FAIL: 금지 심볼 (힙/예외/abort)
  08000838 T _Znwj
  080010a0 T __gxx_personality_v0
  08000af0 T __cxa_throw
  08002efc T _malloc_r
  080037d8 T _sbrk
  080028d4 T abort
  … (44개)
### exit 1
```

| | 힙 0 | `new int` 1개 | 차이 |
|---|---|---|---|
| `.text` | 2184 | 14264 | **+12080 B (6.5배)** |
| `.data` / `.bss` | 68 / 76 | 1416 / 488 | malloc 상태(`__malloc_av_` 1032 B), `_impure_data` 288 B … |
| 금지 심볼 | 0 | 44 | `_Znwj`, `malloc`, `_sbrk`, `__cxa_*`, `_Unwind_*`, `__gxx_personality_v0`, `abort` |

4바이트 할당 하나가 예산을 넘겼다. 그래서 힙 0 은 코드 리뷰가 아니라 **빌드 게이트** (`nm` 검사, exit 1)로 지킨다.

**같은 소스, 맥 clang (recipe b).** `bash asm_targets.sh size` 실제 출력 (오브젝트, 링크 전, `-Os`):

```text
== clang 21.0.0 (clang-2100.0.123.102)  -Os  (오브젝트, 링크 전)
.text           1986 B in 18 sections
.ARM.exidx       136 B in 17 sections
.rodata         1024 B in 1 sections
.data             68 B in 1 sections
.bss              73 B in 3 sections
== 외부 의존 (U)
         U __aeabi_memclr
         U __aeabi_memclr4
         U __aeabi_memcpy
...
== -fno-unwind-tables -fno-asynchronous-unwind-tables
.text           1986 B in 18 sections
.ARM.exidx       136 B in 17 sections
```

clang 은 덜 인라인한다 (`cobs_encode` 158 B, `pack_frame` 88 B, `read_reg<STATUS>` 74 B 가 따로 남음) — 그래서 층별 크기는 이쪽이 읽기 쉽다. 값 초기화는 `memset` 대신 ARM EABI 의 `__aeabi_memclr` 로 나왔다. 그리고 **함수마다 8 B `.ARM.exidx` 항목** (17 × 8 = 136 B)이 `-fno-exceptions` 에서도, `-fno-unwind-tables` 를 줘도 남았다 (GCC 이미지에는 `.ARM.exidx` 섹션이 없었다). 링크 후 플래시에 남는지, 이를 끄는 clang 옵션이 무엇인지는 이 레슨에서 확인하지 못했다 — 미확인. 컴파일러를 바꾸면 예산표를 다시 재야 하는 이유다.

### 아키텍처와 면접 워크스루

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">층 · 의존 방향 · 정적 메모리 (M4 실측)</text>
  <rect x="10" y="36" width="460" height="44" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="20" y="54" fill="currentColor">app::Pipeline&lt;B&gt; — 재시도 정책 · seq · err 플래그 · 필터 6개</text>
  <text x="20" y="72" fill="currentColor" font-family="monospace" font-size="12">step 600 B text · g_pipe 68 B .data</text>
  <rect x="10" y="92" width="225" height="44" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="20" y="110" fill="currentColor">dsp — Q15 / Q16.16</text>
  <text x="20" y="128" fill="currentColor" font-family="monospace" font-size="12">LowPass 58 B (smlal, ssat)</text>
  <rect x="245" y="92" width="225" height="44" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="255" y="110" fill="currentColor">tlm — LE · CRC-32 · COBS</text>
  <text x="255" y="128" fill="currentColor" font-family="monospace" font-size="12">표 1024 B .rodata</text>
  <rect x="10" y="148" width="460" height="44" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="20" y="166" fill="currentColor">imu — DevReg 맵 · probe · configure · read_sample</text>
  <text x="20" y="184" fill="currentColor" font-family="monospace" font-size="12">870 B text (템플릿, B = HwI2c)</text>
  <rect x="10" y="204" width="460" height="44" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="20" y="222" fill="currentColor">bus — I2cBus concept · BusLock (RAII) · xfer · retry 판정</text>
  <text x="20" y="240" fill="currentColor" font-family="monospace" font-size="12">xfer 296 B (write_read 인라인)</text>
  <rect x="10" y="260" width="225" height="44" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="20" y="278" fill="currentColor">host: MockI2c</text>
  <text x="20" y="296" fill="currentColor" font-family="monospace" font-size="12">regs[128] · 장애 주입</text>
  <rect x="245" y="260" width="225" height="44" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="255" y="278" fill="currentColor">target: HwI2c (MMIO Reg)</text>
  <text x="255" y="296" fill="currentColor" font-family="monospace" font-size="12">바운드 폴링 · 1 B .bss</text>
  <g fill="currentColor" font-size="12">
    <text x="490" y="54">core (모든 층이 씀):</text>
    <text x="490" y="74">• Result&lt;T,Error&gt; [[nodiscard]]</text>
    <text x="490" y="94">• static_vector&lt;T,N&gt;</text>
    <text x="490" y="114">• A10_TRY (표준 C++)</text>
    <text x="490" y="148">이미지 (GCC -Os):</text>
    <text x="490" y="168">.text 2184 / 8192 B</text>
    <text x="490" y="188">.rodata 1024 · .data 68 · .bss 76</text>
    <text x="490" y="208">스택 최악 경로 ≈ 376 B</text>
    <text x="490" y="228">heap 0 · .init_array 0</text>
    <text x="490" y="262">위 층은 아래 층만 안다.</text>
    <text x="490" y="282">버스 선택은 템플릿 인자 하나 —</text>
    <text x="490" y="302">테스트와 펌웨어가 같은 드라이버.</text>
  </g>
</svg>
```

면접에서 5분 안에 설명하는 순서:

1. **경계부터.** "힙 없음, 예외 없음, 8 KB" 를 *빌드가 검사* 하게 했다 — `nm` 금지 심볼, `.init_array` 부재, `.text` 숫자에서 exit 1. 리뷰로 지키는 규칙은 결국 깨진다. `new` 하나가 +12 KB 라는 실측이 근거.
2. **버스 추상화는 템플릿 + concept.** 가상 함수 없이 mock 과 MMIO 를 갈아 끼운다. 대가: 버스 종류마다 드라이버가 인스턴스화된다 (imu 870 B 는 `HwI2c` 하나 분). 버스가 2–3종 이상이거나 런타임에 바뀌면 인터페이스가 낫다 — CPP-A08 의 비교표.
3. **에러는 값으로, 문맥과 함께.** `Result<T, Error{code, bus, reg}>`, `[[nodiscard]]` 로 무시를 컴파일 에러로. 판정(일시적인가)은 bus 층, 정책(몇 번)은 app 층.
4. **RAII 는 "모든 return 경로" 문제에만.** 버스 잠금 하나. ISR 과 공유하는 카운터(`g_ms`)는 lock-free `std::atomic` relaxed (M4 에서 읽기는 `ldr` 하나 — CPP-A04).
5. **숫자 형식.** M4 에 FPU(M4F)가 있으면 float 도 합리적이다. 고정소수점을 고른 이유는 ① 호스트와 타깃이 **비트 단위로 같은 결과** → golden 테스트 가능 ② FPU 없는 M0+ 로 옮겨도 그대로 ③ 오차 한계를 증명할 수 있음. 대가는 범위 관리 — 그래서 모든 곱에 범위 주석과 `static_assert` 를 달았다.
6. **와이어 형식은 오프셋 표.** 구조체 memcpy 금지 (필드 22 B vs `sizeof` 24 B 의 꼬리 패딩, 엔디언, ABI 레이아웃). CRC 는 링크 무결성, COBS 는 재동기, seq 는 유실 탐지, flags 는 "그 사이 오류" — 각 필드가 수신 측의 한 질문에 답한다.
7. **증명.** 층별 단위 테스트 + 장애 주입 + 가상 시간 시나리오 + 독립 오라클 golden + 뮤턴트 6개가 각각 잡힘 + 타깃 크기/스택 실측.

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution          # 82/82 passed
c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer solution.cpp -o sol_asan && ./sol_asan   # 82/82, 보고 없음
bash budget.sh                                   # exit 0 (위 출력)
```

테스트가 정말 버그를 잡는지 — `solution.cpp` 사본에 그럴듯한 버그를 하나씩 넣어 돌린 결과 (실측):

| 뮤턴트 | 잡은 CHECK |
|---|---|
| `LowPass` 에서 반올림 상수 `+ (1 << 14)` 삭제 | F=0 데드밴드 (97/4), double 오차 한계 — 2개 |
| `configure` 에서 CTRL1_XL 을 CTRL3_C 보다 먼저 | 쓰기 순서 로그 — 1개 |
| `xfer` 를 `BusLock` 대신 수동 `try_lock` (해제 누락) | 잠금·트랜잭션 수 — 6개 |
| COBS: 254 블록 뒤 항상 새 블록 | 254 경계 — 1개 |
| `step`: seq 를 out 에 넣은 뒤에만 증가 | NoSpace 후 seq 틈 — 1개 |
| `retry_transient` 가 모든 오류를 재시도 | AddrNack 재시도 0 — 1개 |

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `try_lock()` … `unlock()` 을 손으로 | 에러 return 하나가 버스를 영원히 잠근다 | `BusLock<B> lock(b);` |
| 잠금 가드가 못 잡았어도 소멸자에서 `unlock()` | 남(다른 드라이버)이 쥔 잠금을 풀어 버린다 | `owned_` 일 때만 해제 |
| 리셋값을 믿고 IF_INC 설정 생략 | 버스트가 같은 레지스터 12번 — I2C 상으론 "성공" | 첫 쓰기로 CTRL3_C, read-back 검증 |
| 모든 오류를 N번 재시도 | 장치 없음(AddrNack)에 시간 낭비, 영구 오류를 가린다 | `is_transient` 판정 + 횟수 상한 |
| `(a * b) >> 15` 로 Q15 곱 | `(-1)(-1)` 이 -1 로 감싸지고, 항상 내림 → 음의 편향 | `sat16((p + (1 << 14)) >> 15)` |
| IIR 상태를 출력과 같은 폭(F=0)으로 | 데드밴드: 계단 100 이 97 에서 멈춤 | 상태에 소수 비트 F 개 (F=8) |
| `d * α` 를 int32 로 | `|d| ≤ 65535·2^F`, α ≤ 2^15 → 넘친다 (UB) | `std::int64_t{d} * a_` (M4: `smlal` 하나) |
| `memcpy(wire, &frame, sizeof frame)` | 꼬리 패딩(필드 22 B, `sizeof` 24 B), 값 미정인 패딩 바이트, CPU 엔디언 | `put_le16/put_le32` 오프셋 표 |
| 버전을 CRC 보다 먼저 검사 | 비트 오류를 "모르는 버전" 으로 분류 | COBS → 길이 → CRC → 버전 |
| 출력 버퍼에 들어가는 만큼만 쓰기 | 반쪽 프레임 → 수신 측이 다음 프레임까지 버림 | 남은 공간 확인 후 통째로, 아니면 `NoSpace` |
| 프레임을 버릴 때 seq 도 그대로 | 수신 측이 유실을 모른다 | 생산 시점에 seq++ |
| 전역 파이프라인을 일반 전역 객체로 | 생성자가 있으면 `.init_array` + 초기화 순서 문제 (CPP-B09) | `constexpr` 생성자 + `constinit` |
| "힙 안 써요" 를 리뷰로만 | 라이브러리 함수 하나가 `new` 를 부를 수 있다 | `nm` 금지 심볼 게이트 + 호스트 `operator new` 트랩 |

## 8. 면접 Q&A

<details>
<summary>Q1. 힙을 안 쓴다는 걸 어떻게 증명하나요?</summary>

두 층으로. ① 호스트 테스트에서 전역 `operator new`/`new[]` 를 트랩으로 교체 ([replacement.functions]) — 테스트 전체가 돌고 호출 0 (`demo_heap_trap` 로 트랩이 실제로 걸리는 것도 확인, `vector::push_back` 에서 `operator new(4)`). ② 타깃 ELF 에서 `nm` 으로 `_Znwj`, `malloc`, `_sbrk` 등을 찾으면 빌드 실패. 실측: `new int` 하나가 libstdc++ 의 `operator new` → `bad_alloc` 경로 → 언와인더·`malloc`·`_sbrk`·`abort` 까지 44개 심볼, `.text` +12080 B. 꼬리: "`operator new` 를 정의 안 하면 링크 에러로 막히지 않나요?" → 아니다, g++ 드라이버가 libstdc++ 를 링크하므로 조용히 링크된다(실측). 막으려면 nm 게이트나, 직접 정의한 트랩 `operator new` 를 링크해 첫 호출에서 멈추게 한다.

</details>

<details>
<summary>Q2. 재시도는 드라이버가 하나요, 앱이 하나요?</summary>

이 설계는 나눴다: **판정** ("이 오류가 일시적인가")은 버스 층이 안다 (`is_transient`: Timeout/DataNack/ArbLost), **정책** (몇 번, 얼마나 기다리나)은 앱이 정한다 (`kExtraRetries = 1`). 드라이버 안에 고정하면 부팅 시 probe 처럼 빨리 실패해야 하는 호출도 같은 지연을 받는다. 재시도 단위는 트랜잭션이 아니라 `read_sample` 전체 — 버스트가 실패하면 STATUS 부터 다시 봐야 새 샘플과 옛 샘플을 섞지 않는다. AddrNack 은 이 장치에선 영구 오류로 봤지만 EEPROM 처럼 쓰기 사이클 동안 NACK 하는 장치면 일시적이다 — 장치별 정책. 꼬리: "버스가 SDA low 로 묶이면?" → 재시도로 안 풀린다. 컨트롤러 드라이버가 SCL 9펄스 + STOP 으로 복구하고, 그래도 안 되면 센서 전원 사이클.

</details>

<details>
<summary>Q3. M4F 에 FPU 가 있는데 왜 고정소수점이죠?</summary>

M4F 라면 float IIR 도 합리적인 선택이다 — 성능만이면 큰 차이가 없을 수 있다(이 레슨에서 사이클은 재지 않음). 고정소수점을 고른 근거는 ① 결정성: 호스트와 타깃이 **같은 비트** 를 내므로 golden 프레임과 기준 필터 비교가 정확 일치로 된다 (float 은 컴파일러의 FMA 축약·플래그에 따라 마지막 비트가 달라질 수 있음) ② 이식성: FPU 없는 M0+ 로 그대로 ③ 증명 가능한 오차 한계 (`0.5 + 2^-(F+1)/α`, 실측 0.5069 ≤ 0.5156). 그리고 ISR 에서 FPU 를 쓰면 lazy stacking 등 문맥 저장 비용을 따져야 한다. 비용은 범위 관리 — `|d| ≤ 65535·2^F` 같은 주석과 `static_assert`. 실측 asm 은 `smlal` + `ssat` 로, "복잡한 정수 산술" 이 명령 몇 개였다.

</details>

<details>
<summary>Q4. 왜 구조체를 그대로 보내지 않고 바이트를 하나씩 쓰나요? 느리지 않나요?</summary>

필드 합 22 B 인데 `sizeof(Frame) == 24` (실측): 정렬 때문에 패딩이 끼고(여기선 꼬리 2 B), 엔디언은 CPU 를 따르며, 패딩 바이트 값은 정해지지 않는다 — 같은 펌웨어의 다음 버전이나 다른 컴파일러가 다른 바이트를 보낼 수 있다. `#pragma pack` 은 비정렬 접근을 낳고 엔디언은 여전히 남는다. 시프트 패킹은 26바이트에 store 몇십 개 수준이라 115200 bps UART 로 28바이트를 보내는 시간(≈ 2.4 ms) 앞에서 무시할 만하다. 꼬리: "Protobuf/nanopb 는?" → 스키마 진화가 필요하면 좋은 선택이지만 코드·스택 비용이 생긴다. 이 레슨은 버전 바이트 + CRC 로 최소한의 진화만 허용한다.

</details>

<details>
<summary>Q5. 왜 COBS 인가요? SLIP 이나 길이 접두는?</summary>

길이 접두(length prefix)는 길이 바이트가 깨지면 이후 스트림 전체가 어긋나고 재동기 규칙이 따로 필요하다. SLIP 은 구분자를 이스케이프해서 최악 2배로 커진다. COBS 는 0x00 이 데이터에 절대 안 나타나므로 수신 측은 **0x00 만 보면 언제든 재동기** 하고, 오버헤드는 254바이트당 최대 1바이트로 고정(26 B 프레임 → 항상 +1, 실측 28 B). 함정: 254개 연속 비영 바이트 경계 — 꼬리 블록을 만드느냐 마느냐의 두 변형이 있고 디코더는 둘 다 받아야 한다 (이 레슨의 디코더는 둘 다 받는다). 꼬리: "COBS 가 오류를 검출하나요?" → 형식 오류 일부만. 무결성은 CRC 몫이다.

</details>

<details>
<summary>Q6. 이 이미지의 RAM 사용량은? 스택은 어떻게 아나요?</summary>

정적: `.data` 68 (파이프라인 — 0 이 아닌 constinit 초기값) + `.bss` 76 (출력 버퍼 68, 틱 4, 버스 1, 정렬) = 144 B. 스택: `-fstack-usage` 의 프레임 크기를 최악 호출 경로로 더하면 `run 24 → step 168 → read_sample 96 → xfer 48` = 336 B, 리셋 8 B, SysTick 예외 프레임 32 B (FPU 없는 빌드) → ≈ 376 B. 손 계산이라 한계가 있다(재귀·함수 포인터·인라인 변화). 실무에선 `-fcallgraph-info`/정적 분석 도구 + 런타임 스택 페인팅(0xDEADBEEF 로 채우고 최고 수위 확인) 두 가지로 확인한다. 꼬리: "`step` 이 168 B 나 쓰는 이유?" → `Frame`, 26 B 프레임, 28 B 인코딩 버퍼, `Result` 들이 전부 지역 변수다. 줄이려면 인코딩을 out 에 바로 쓰고(대신 반쪽 프레임 방지를 다르게) 중간 버퍼를 없앤다.

</details>

<details>
<summary>Q7. `constinit` 전역 Pipeline 이 왜 `.bss` 가 아니라 `.data` 인가요?</summary>

`constinit` 은 "정적 초기화(상수 초기화)로 끝나야 한다" 를 강제할 뿐 ([dcl.constinit]) 값이 0 이라는 뜻이 아니다. `g_pipe` 는 α(7602), 배율(3998), 버스 참조(주소)를 갖고 있어 초기값 이미지가 필요하다 → `.data` (플래시에 68 B 복사본 + RAM 68 B, `Reset_Handler` 가 복사). 모두 0 이면 `.bss` 로 간다(`g_tx`). 중요한 건 `.init_array` 가 없다는 것: 생성자 코드가 `main` 전에 돌지 않으므로 정적 초기화 순서 문제(CPP-B09)도 없다. 꼬리: "참조 멤버가 있는데 상수 초기화가 되나요?" → 정적 저장 기간 객체의 주소는 상수식이라 된다 (clang/GCC 모두 컴파일, `.init_array` 없음 실측).

</details>

<details>
<summary>Q8. 같은 코드를 clang 으로 빌드하면 예산이 같나요?</summary>

아니다. 실측(오브젝트, 링크 전): clang `-Os` `.text` 1986 B 로 GCC 링크 이미지의 2184 B(여기엔 newlib `memset` 156 B 포함)와 비슷하지만 ① 인라인 결정이 달라 함수 목록이 다르고 ② 값 초기화가 `memset` 대신 `__aeabi_memclr` 로 나가고 ③ 함수마다 `.ARM.exidx` 8 B (136 B) 가 `-fno-exceptions -fno-unwind-tables` 에서도 남았다(GCC 이미지엔 없음; 끄는 방법은 미확인). 그래서 예산은 "컴파일러 + 플래그 + 링커 스크립트" 조합에 대한 숫자이고, CI 에서 배포용 툴체인으로 재야 한다. 꼬리: "LTO 를 켜면?" → 층 경계를 넘어 더 인라인되어 보통 줄지만, 컴포넌트별 귀속은 더 어려워진다 (이 레슨에서 LTO 는 재지 않음).

</details>

## 9. 추가 연습

### 연습 1 — CRC-32 를 64 B 표로

CRC 표 1024 B 는 이 이미지 플래시의 31% 다. 4비트(니블) 표 16개로 같은 CRC 를 만들고, 바이트 표·비트 루프와 무작위 300개 입력에서 같은지 확인하라. 크기/속도 트레이드오프를 말하라.

<details>
<summary>답 보기</summary>

```cpp
for (std::uint8_t b : d) {
    c ^= b;
    c = kT16[c & 0xFu] ^ (c >> 4);   // 아래 니블
    c = kT16[c & 0xFu] ^ (c >> 4);   // 위 니블
}
```

니블 표의 i 번째 항목 = i 를 4번 시프트·XOR 한 값. 바이트당 표 조회 2번. `bash asm_targets.sh crc` (`demo_crc_size.cpp`) 실측 (clang 21, M4, `-Os`): `.text` 바이트 표 46 B / 니블 표 60 B / 비트 루프 60 B, `.rodata` 1024 / 64 / 0 B. 함수 전체 명령 수(`-Os`): 15 / 18 / 23 — 하지만 비트 루프는 바이트당 안쪽 루프를 8번 돈다. 이 이미지라면 니블 표로 바꿔 ≈ 950 B 를 아끼고, 프레임마다 CRC 하는 22바이트에서 표 조회가 22번 더 드는 정도다 (사이클은 재지 않음). STM32 처럼 하드웨어 CRC 유닛이 있으면 그 다항식·반사 설정이 zlib CRC-32 와 같은지부터 확인한다 (기본 설정이 다른 칩이 있다 — 칩 RM 확인 필요).

</details>

### 연습 2 — 수신 측 FrameSplitter

UART RX 에서 바이트가 하나씩 들어온다. `feed(uint8_t)` 가 0x00 을 만나면 모은 바이트를 판정해 `Frame`/`Bad`/`None` 을 돌려주는 `FrameSplitter` 를 정적 버퍼(27 B)로 써라. 송신이 중간에 끊긴 프레임, 쓰레기 50바이트, 비트 오류, 연속 구분자, 0x00 없이 시작한 스트림을 처리해야 한다.

<details>
<summary>답 보기</summary>

```cpp
if (b != 0) { if (n_ < buf_.size()) buf_[n_++] = b; else overrun_ = true; return Ev::None; }
const bool over = overrun_; const std::size_t len = n_;
n_ = 0; overrun_ = false;
if (len == 0) return Ev::None;          // 연속 0x00 = 빈 프레임
if (over) { ++bad; return Ev::Bad; }    // 과길이: 다음 0x00 까지 버린 것
// cobs_decode → 길이 26 → CRC → Frame
```

핵심은 **과길이에서 버퍼를 넘기지 않고 플래그만 세우고 다음 0x00 에서 재동기** 하는 것. 테스트: 40개 중 3개(잘림·쓰레기·비트 오류)만 `Bad`, 37개 정상, 순서·시간 필드 일치. 실제 펌웨어에선 ISR 이 바이트를 링 버퍼에 넣고(DS 트랙) 메인 루프가 `feed` 를 부른다 — ISR 안에서 CRC 까지 하지 않는다.

</details>

### 연습 3 — u16 seq 감싸기를 넘어 유실 세기

`LossTracker::on_seq(uint16_t)` 가 유실 수와 "옛 번호(중복·순서 뒤바뀜)" 수를 세게 하라. 65535 → 0 은 유실이 아니다.

<details>
<summary>답 보기</summary>

```cpp
const std::uint16_t gap = static_cast<std::uint16_t>(s - next_);   // 모듈러 2^16
if (gap < 0x8000u) { lost += gap; next_ = s + 1; } else ++stale;
```

부호 없는 뺄셈의 모듈러 거리로 "앞으로 몇 칸" 을 구하고, 절반(32768) 이상이면 뒤로 간 것으로 본다 — TCP 시퀀스 비교와 같은 아이디어. 10 ms 주기라면 32768 프레임 ≈ 5.5분 이상 끊기면 판정이 틀어진다는 것도 말할 수 있어야 한다 (그땐 `t_ms` 로 보조 판정).

</details>

### 연습 4 — 정수 이동평균 `MovingAverage<N>`

2의 거듭제곱 창 N, 누적합 int32, O(1) 갱신의 이동평균을 만들고 무작위 5000개 입력에서 매번 무차별 합과 같은지 확인하라. 왜 float 누적합은 이렇게 하면 안 되나?

<details>
<summary>답 보기</summary>

```cpp
sum_ += x - win_[i_];  win_[i_] = x;  i_ = (i_ + 1) & (N - 1);
return static_cast<std::int16_t>((sum_ + (1 << (kShift - 1))) >> kShift);
```

`static_assert` 두 개: N 이 2의 거듭제곱(나눗셈 대신 시프트), `32768·N ≤ 2^31` (누적합이 int32 에 들어감). 정수 누적합은 더하고 빼는 값이 정확히 상쇄되므로 **오차가 쌓이지 않는다** (테스트: 5000개 전부 무차별 합과 일치). float 으로 같은 일을 하면 반올림 오차가 누적돼 주기적으로 재계산해야 한다. IIR 과 비교: 이동평균은 N 샘플 뒤 정확히 수렴(계단 100 → 4샘플 뒤 100)하지만 RAM 이 N×2 B 든다.

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice   # 14/14 passed
c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer practice.cpp -o practice_s && ./practice_s   # 14/14, 보고 없음
```

## 10. 핵심 정리

- 층마다 책임 하나: bus(잠금·전송·오류 판정) → imu(레지스터 의미) → dsp(숫자) → tlm(바이트) → app(정책·상태). 버스는 템플릿 + concept 로 갈아 끼운다 (mock ↔ MMIO).
- 에러는 `Result<T, Error{code, bus, reg}>` + `[[nodiscard]]`. 재시도: 일시적 오류만, 횟수 상한, 샘플 단위. I2C 는 데이터 오류를 모른다 → WHO_AM_I·read-back 검증.
- Q15 곱 = `sat16((a·b + 2^14) >> 15)`; half-up 은 비대칭. IIR 상태에 소수 비트 F 개 → 오차 ≤ `0.5 + 2^-(F+1)/α` (실측 0.507 ≤ 0.516), F=0 은 데드밴드(97/4). M4 에서 `smlal` + `ssat`.
- 와이어 = 오프셋 표 (구조체 memcpy 금지: 필드 22 B, `sizeof` 24 B). 디코드 순서 COBS → 길이 → CRC → 버전. CRC-32 는 1비트 오류 208/208 검출. golden 은 독립 오라클로.
- seq 는 생산 시점에 증가(유실이 틈으로 보이게), 반쪽 프레임 금지, 오류 플래그는 전송 성공 후에만 끔.
- 힙 0 은 빌드 게이트로: `new int` 하나 = `.text` +12080 B, 금지 심볼 44개. constinit 전역 → `.init_array` 0 (단, 0 아닌 초기값은 `.data`).
- 예산 실측 (GCC 14.2 `-Os`, M4): `.text` 2184 / 8192 B, `.rodata` 1024 (CRC 표가 전부), `.data` 68, `.bss` 76, 스택 최악 ≈ 376 B. 컴파일러를 바꾸면 다시 잰다 (clang: `.ARM.exidx` 136 B).

## 참고자료

- C++ 표준 (N4861 C++20 초안): [conv.integral] (부호 있는 정수로의 모듈러 변환, C++20), [expr.shift] (음수 `>>` 정의, C++20), [dcl.constinit], [dcl.consteval], [expr.const], [dcl.attr.nodiscard], [temp.constr] (concept·requires), [replacement.functions] / [new.delete.single] (`operator new` 교체), [basic.start.static] (상수 초기화) — <https://isocpp.org/files/papers/N4861.pdf>
- P1236R1 — Alternative Wording for P0907R4 Signed Integers are Two's Complement: <https://wg21.link/P1236>
- S. Cheshire, M. Baker, "Consistent Overhead Byte Stuffing", IEEE/ACM Trans. Networking 7(2), 1999: <http://www.stuartcheshire.org/papers/COBSforToN.pdf>
- Wikipedia — Consistent Overhead Byte Stuffing (인코딩 예제 표, 2026-11-11 확인): <https://en.wikipedia.org/wiki/Consistent_Overhead_Byte_Stuffing>
- zlib `crc32` / CRC-32 (IEEE 802.3) 검사값 0xCBF43926 — Python `zlib.crc32` 로 확인: <https://docs.python.org/3/library/zlib.html#zlib.crc32>
- Arm — Cortex-M4 Technical Reference Manual (SMLAL, SSAT 등 DSP 명령, 예외 스택 프레임): <https://developer.arm.com/documentation/100166/latest/>
- ARM ARMv7-M Architecture Reference Manual (DDI 0403) — `SSAT`, `SMLAL` 명령 정의: <https://developer.arm.com/documentation/ddi0403/latest/>
- ST LSM6DSO 데이터시트 (이 레슨의 레지스터 맵은 이것을 본뜬 대표값 — 실제 값은 데이터시트로 확인): <https://www.st.com/resource/en/datasheet/lsm6dso.pdf>
- GCC — `-fstack-usage`, `-ffunction-sections`, `--gc-sections`: <https://gcc.gnu.org/onlinedocs/gcc/Developer-Options.html>
- 관련 레슨: CPP-M01 (Reg 권한), CPP-M10 (static_vector), CPP-M11 (Result·에러 계층), CPP-B06 (RAII 가드), CPP-M13 (constexpr CRC 표), CPP-B09 (정적 초기화), CPP-B14 (operator new 금지), CPP-A03 (`__cxa_*` 런타임 심볼), CPP-A04 (ISR 공유 atomic), CPP-A06 (constexpr 클럭·보레이트), CPP-A08 (DI vs 링크 심), CPP-M14 (static_assert 메시지), C-M14 (tearing)
