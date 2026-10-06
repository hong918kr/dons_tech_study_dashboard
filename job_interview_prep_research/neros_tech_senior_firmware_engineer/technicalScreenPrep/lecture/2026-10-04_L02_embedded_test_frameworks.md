# 🧰 L02 · pytest 말고 — 임베디드 테스트 프레임워크 10종, 시나리오별로 직접 돌려 본 비교

> "pytest 말고 뭘 써 봤나 / 뭘 쓰겠나"에 답하기 위한 강의. **C 펌웨어 단위 테스트**(Unity · CMock · Ceedling · CppUTest · GoogleTest/gMock), **Python 시스템 · HIL 테스트**(unittest · Robot Framework · behave · pytest), **공장 스테이션**(OpenHTF), **경량 Raspberry Pi HIL**(py-micro-hil)을 같은 피검 코드 · 같은 가짜 DUT로 **이 Mac에서 전부 실행**했다(2026-10-04, 10종 모두 ✓). 아래 코드는 실행한 파일 그대로다. 실행: `bash lecture/frameworks/run_all.sh`.

## 0. 결론 먼저 — 시나리오 → 프레임워크

| 시나리오 | 1순위 | 대안 | 이유 한 줄 |
|---|---|---|---|
| **C 펌웨어 모듈을 host에서 단위 테스트** (ring buffer, 파서, 상태 머신) | **Unity + Ceedling** | CppUTest, GoogleTest | 순수 C, 작고 MCU에도 올라감, mock 자동 생성 |
| C 코드인데 **HAL을 mock** 해야 함 (I2C · UART 드라이버 로직) | **Ceedling (CMock 자동)** | CppUTest mock, gMock | 헤더만 주면 mock이 생긴다 |
| 코드베이스가 **C++** (autonomy, companion) | **GoogleTest + gMock** | CppUTest | 업계 표준, IDE · CI 지원 최고 |
| **실제 DUT / HIL 리그**를 Python으로 테스트 | **pytest** | unittest | fixture · parametrize · plugin · JUnit XML |
| 테스트를 **비개발자**(테스트 기술자, QA, 운용자)가 읽고 고친다 | **Robot Framework** | behave | keyword 표 형식, HTML 로그가 뛰어남 |
| 요구사항을 **사람 말(시나리오)**로 합의하고 그대로 실행 | **behave** (Gherkin) | Robot | Given/When/Then = 스펙 문서 |
| **공장 테스트 스테이션** (시리얼 번호 · 측정값 · 한계 · 기록) | **OpenHTF** | pytest + 직접 만든 레코드 | phase · measurement · limit · test record가 내장 |
| Raspberry Pi에 GPIO/I2C/SPI를 직접 붙인 **소규모 HIL** | py-micro-hil | pytest + 직접 만든 드라이버 | YAML 하나로 핀 설정 · HTML 리포트. 단 아직 0.1.x (7절) |

> Neros 맥락 [추정]: 공고에 특정 프레임워크 이름은 없고 **Python · HIL · GitLab CI/Jenkins**가 있다 [확인됨 공고 4941340007]. 그래서 기본은 **pytest(시스템 · HIL) + Unity/Ceedling 또는 GoogleTest(펌웨어 단위)**, factory test FW를 세울 때 **OpenHTF**를 검토한다는 답이 자연스럽다.

## 1. 테스트 지도 — 어느 층에서 무엇을 쓰나

```text
                    누가 쓰나            어디서 도나                 프레임워크
 ┌──────────────┐
 │ 공장 스테이션   │  테스트 엔지니어       스테이션 PC + 픽스처        OpenHTF  (pytest+레코드)
 ├──────────────┤
 │ 시스템 · HIL   │  테스트 엔지니어       리그 PC ↔ 실제 DUT          pytest · Robot · behave · unittest · py-micro-hil
 ├──────────────┤
 │ 통합 (host)    │  FW + 테스트          CI 컨테이너, SITL           pytest + ctypes (L00/문제 07), GoogleTest
 ├──────────────┤
 │ 단위 (host)    │  FW 개발자            CI 컨테이너 (x86/arm64)     Unity/Ceedling · CppUTest · GoogleTest
 ├──────────────┤
 │ 단위 (target)  │  FW 개발자            실제 MCU (UART로 결과)       Unity · CppUTest (작은 footprint)
 └──────────────┘
```

- 같은 피검 코드를 썼다: **C** = [`rb.c`](frameworks/c/fw/rb.c) (ring buffer) + [`sensor.c`](frameworks/c/fw/sensor.c) (I2C로 온도를 읽고 실패 시 1회 재시도, HAL은 `hal_i2c.h`) / **Python** = [`fake_dut.py`](frameworks/common/fake_dut.py) (UART CLI 펌웨어 흉내: `ver` · `arm` · `status` · `temp`, 고장 주입 `mute`)
- **mock이 버그를 잡는지 확인**: `sensor.c`에서 재시도 두 줄을 지우고 돌렸더니 Unity+CMock · Ceedling · CppUTest · GoogleTest **모두 2개씩 실패**(재시도 테스트 · 포기 테스트) → 복원 후 전부 통과

피검 C 코드:

```c
#include "sensor.h"
#include "hal_i2c.h"

int sensor_read_temp_centi(int16_t *out)
{
    uint8_t b[2];
    int rc = hal_i2c_read(SENSOR_ADDR, SENSOR_REG_TEMP, b, sizeof b);
    if (rc != 0)
        rc = hal_i2c_read(SENSOR_ADDR, SENSOR_REG_TEMP, b, sizeof b);   /* one retry */
    if (rc != 0)
        return -1;
    int16_t t = (int16_t)((b[0] << 8) | b[1]);
    if (t < -4000 || t > 12500)
        return -2;
    *out = t;
    return 0;
}
```

---

## Part A · C 펌웨어 단위 테스트 (host에서)

### A1. Unity — 가장 작은 C 테스트 프레임워크

- **무엇**: ThrowTheSwitch의 순수 C 단위 테스트. `unity.c` 하나 + 헤더. MCU 위에서도 돈다
- **언제**: C 모듈(ring buffer, CRC, 파서, 상태 머신)을 host CI에서 빠르게
- **장점**: 아주 작다 · 의존성 없음 · `TEST_ASSERT_EQUAL_UINT8` 같은 **임베디드 타입별 assert** · target에서도 실행 가능
- **단점**: 테스트 러너(`main`)를 직접 쓰거나 Ruby 스크립트로 생성해야 함 · fixture가 `setUp/tearDown` 하나뿐 · 파라미터화가 약함

```c
/* Unity: plain C unit tests for rb.c — setUp/tearDown run around every test. */
#include "unity.h"
#include "rb.h"

static rb_t rb;

void setUp(void)    { rb_init(&rb); }
void tearDown(void) {}

void test_new_buffer_is_empty(void)
{
    uint8_t v;
    TEST_ASSERT_EQUAL_UINT32(0, rb_count(&rb));
    TEST_ASSERT_FALSE(rb_pop(&rb, &v));
}

void test_all_16_slots_usable_then_full(void)
{
    for (int i = 0; i < 16; i++)
        TEST_ASSERT_TRUE(rb_push(&rb, (uint8_t)i));
    TEST_ASSERT_FALSE_MESSAGE(rb_push(&rb, 99), "17th push must fail (drop-new)");
}

void test_fifo_across_uint32_wrap(void)
{
    uint8_t v;
    rb.head = rb.tail = UINT32_MAX - 3;      /* indices wrap during the test */
    for (uint8_t i = 0; i < 10; i++)
        rb_push(&rb, i);
    TEST_ASSERT_EQUAL_UINT32(10, rb_count(&rb));
    for (uint8_t i = 0; i < 10; i++) {
        TEST_ASSERT_TRUE(rb_pop(&rb, &v));
        TEST_ASSERT_EQUAL_UINT8(i, v);
    }
}
```

실행 결과 (`make -C lecture/frameworks/c/unity test`):

```text
test_rb.c:10:test_new_buffer_is_empty:PASS
test_rb.c:17:test_all_16_slots_usable_then_full:PASS
test_rb.c:24:test_fifo_across_uint32_wrap:PASS
3 Tests 0 Failures 0 Ignored
```

### A2. CMock — C 함수 mock 자동 생성 (Unity와 한 세트)

- **무엇**: 헤더(`hal_i2c.h`)를 읽고 `mock_hal_i2c.c/.h`를 **자동 생성**. 테스트는 "이 함수가 이 인자로 불리고 이걸 돌려줄 것"을 기대(expect)로 적는다
- **장점**: 하드웨어 없이 드라이버 로직(재시도, 에러 처리, 순서) 검증 · 호출 횟수 · 순서 · 인자까지 자동 검사
- **단점**: Ruby 필요 · 생성 코드가 커서 처음엔 헷갈림 · 플러그인(`return_thru_ptr`, `ignore_arg`)을 알아야 출력 버퍼를 다룰 수 있다

```c
/* Unity + CMock: hal_i2c_read is replaced by a generated mock (mock_hal_i2c.h). */
#include "unity.h"
#include "sensor.h"
#include "mock_hal_i2c.h"

void setUp(void)    {}
void tearDown(void) {}

void test_reads_temperature(void)
{
    uint8_t raw[2] = {0x09, 0x60};                 /* 0x0960 = 2400 → 24.00 degC */
    int16_t t;
    hal_i2c_read_ExpectAndReturn(SENSOR_ADDR, SENSOR_REG_TEMP, NULL, 2, 0);
    hal_i2c_read_IgnoreArg_buf();
    hal_i2c_read_ReturnArrayThruPtr_buf(raw, 2);
    TEST_ASSERT_EQUAL(0, sensor_read_temp_centi(&t));
    TEST_ASSERT_EQUAL_INT16(2400, t);
}

void test_retries_once_then_succeeds(void)
{
    uint8_t raw[2] = {0x00, 0x64};                 /* 1.00 degC */
    int16_t t;
    hal_i2c_read_ExpectAnyArgsAndReturn(-5);       /* 1st read: bus error */
    hal_i2c_read_ExpectAnyArgsAndReturn(0);        /* retry: ok */
    hal_i2c_read_ReturnArrayThruPtr_buf(raw, 2);
    TEST_ASSERT_EQUAL(0, sensor_read_temp_centi(&t));
    TEST_ASSERT_EQUAL_INT16(100, t);
}

void test_gives_up_after_two_errors(void)
{
    int16_t t = 7;
    hal_i2c_read_ExpectAnyArgsAndReturn(-5);
    hal_i2c_read_ExpectAnyArgsAndReturn(-5);
    TEST_ASSERT_EQUAL(-1, sensor_read_temp_centi(&t));
    TEST_ASSERT_EQUAL_INT16(7, t);                 /* output untouched on error */
}

void test_out_of_range_rejected(void)
{
    uint8_t raw[2] = {0x7F, 0xFF};                 /* 327.67 degC */
    int16_t t;
    hal_i2c_read_ExpectAnyArgsAndReturn(0);
    hal_i2c_read_ReturnArrayThruPtr_buf(raw, 2);
    TEST_ASSERT_EQUAL(-2, sensor_read_temp_centi(&t));
}
```

```yaml
:cmock:
  :mock_prefix: mock_
  :mock_path: build/mocks
  :plugins:
    - :ignore_arg
    - :expect_any_args
    - :return_thru_ptr
  :treat_as_void: []
```

```text
test_sensor.c:9:test_reads_temperature:PASS
test_sensor.c:20:test_retries_once_then_succeeds:PASS
test_sensor.c:31:test_gives_up_after_two_errors:PASS
test_sensor.c:40:test_out_of_range_rejected:PASS
4 Tests 0 Failures 0 Ignored
```

- 재시도를 지웠을 때 나온 실패 메시지: `test_retries_once_then_succeeds:FAIL: Expected 0 Was -1`, `test_gives_up_after_two_errors:FAIL: Function hal_i2c_read. Called fewer times than expected.`

### A3. Ceedling — Unity + CMock를 묶은 빌드 · 실행 도구

- **무엇**: Ruby(rake) 기반. `test/test_*.c`를 찾아 **러너와 mock을 자동 생성**하고 컴파일 · 링크 · 실행 · 리포트까지. `#include "mock_hal_i2c.h"`만 쓰면 mock이 생긴다
- **장점**: Makefile을 안 써도 된다 · 파일 단위 독립 빌드 · 플러그인(gcov 커버리지, JUnit XML `junit_tests_report`, FFF fake 함수)
- **단점**: Ruby 의존 · 큰 프로젝트에서 설정(`project.yml`)이 복잡 · 크로스 컴파일 · 타깃 실행 설정은 손이 간다
- 버전 주의: 최신 **Ceedling 1.x는 Ruby 3.0+** 필요. 이 Mac의 시스템 Ruby는 2.6이라 **0.31.1**을 로컬 폴더에 설치했고, Rake 13.4와 충돌해서 13.0.6으로 고정했다 (`c/ceedling/run.sh`)

```yaml
# Ceedling 0.31.x project (Ruby 2.x 호환 마지막 계열). 최신 1.x 는 Ruby 3 필요 + 설정 키가 일부 다르다.
:project:
  :use_exceptions: FALSE
  :use_test_preprocessor: TRUE
  :build_root: build
  :test_file_prefix: test_

:paths:
  :test:
    - test/**
  :source:
    - ../fw/**            # 피검 코드 (rb.c, sensor.c) — hal_i2c.h 는 헤더만 있고 mock 으로 대체된다
  :include:
    - ../fw/**

:defines:
  :test:
    - TEST

:cmock:
  :mock_prefix: mock_
  :when_no_prototypes: :warn
  :enforce_strict_ordering: TRUE
  :plugins:
    - :ignore_arg
    - :expect_any_args
    - :return_thru_ptr

:plugins:
  :enabled:
    - stdout_pretty_tests_report
```

```text
Generating include list for hal_i2c.h...
Creating mock for hal_i2c...
Generating runner for test_sensor.c...
TESTED:  7   PASSED:  7   FAILED:  0   IGNORED: 0
```

- 테스트 파일은 A1 · A2와 **동일**. Ceedling은 "어떻게 빌드하고 돌릴지"를 대신해 줄 뿐이다

### A4. CppUTest — C/C++ 겸용, 임베디드 TDD의 고전

- **무엇**: C++로 작성됐지만 C 코드 테스트용으로 설계. 메모리 누수 탐지가 내장. *Test-Driven Development for Embedded C*(James Grenning)의 프레임워크
- **장점**: `TEST_GROUP`의 setup/teardown · **메모리 누수 자동 검출** · 내장 mock(`mock().expectOneCall`) · `-ojunit` JUnit 출력 · target에도 올릴 수 있음
- **단점**: 테스트는 C++로 작성 · mock을 손으로 연결(stub 함수가 `mock().actualCall` 호출) · GoogleTest보다 생태계가 작다

```cpp
// CppUTest + CppUTestExt mocks: hal_i2c_read is a hand-written stub that reports to mock().
#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

extern "C" {
#include "hal_i2c.h"
#include "sensor.h"

int hal_i2c_read(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n)
{
    return mock().actualCall("hal_i2c_read")
        .withParameter("addr", addr)
        .withParameter("reg", reg)
        .withOutputParameter("buf", buf)
        .withParameter("n", n)
        .returnIntValueOrDefault(0);
}
}

TEST_GROUP(Sensor)
{
    void teardown() override
    {
        mock().checkExpectations();   // 기대한 호출이 다 일어났나
        mock().clear();
    }
};

TEST(Sensor, ReadsTemperature)
{
    const uint8_t raw[2] = {0x09, 0x60};
    int16_t t;
    mock().expectOneCall("hal_i2c_read")
        .withParameter("addr", (uint8_t)SENSOR_ADDR)
        .withParameter("reg", (uint8_t)SENSOR_REG_TEMP)
        .withOutputParameterReturning("buf", raw, sizeof raw)
        .withParameter("n", (size_t)2)
        .andReturnValue(0);
    LONGS_EQUAL(0, sensor_read_temp_centi(&t));
    LONGS_EQUAL(2400, t);
}

TEST(Sensor, RetriesOnceThenSucceeds)
{
    const uint8_t raw[2] = {0x00, 0x64};
    int16_t t;
    mock().expectOneCall("hal_i2c_read").ignoreOtherParameters().andReturnValue(-5);
    mock().expectOneCall("hal_i2c_read").withOutputParameterReturning("buf", raw, sizeof raw)
        .ignoreOtherParameters().andReturnValue(0);
    LONGS_EQUAL(0, sensor_read_temp_centi(&t));
    LONGS_EQUAL(100, t);
}

TEST(Sensor, GivesUpAfterTwoErrors)
{
    int16_t t = 7;
    mock().expectNCalls(2, "hal_i2c_read").ignoreOtherParameters().andReturnValue(-5);
    LONGS_EQUAL(-1, sensor_read_temp_centi(&t));
    LONGS_EQUAL(7, t);
}
```

```text
TEST(Sensor, GivesUpAfterTwoErrors) - 0 ms
TEST(Sensor, RetriesOnceThenSucceeds) - 0 ms
TEST(Sensor, ReadsTemperature) - 0 ms
TEST(RingBuffer, FifoAcrossUint32Wrap) - 0 ms
TEST(RingBuffer, All16SlotsUsableThenFull) - 0 ms
TEST(RingBuffer, NewBufferIsEmpty) - 0 ms
OK (6 tests, 6 ran, 58 checks, 0 ignored, 0 filtered out, 0 ms)
```

- 재시도를 지웠을 때: `Mock Failure: Expected call WAS NOT fulfilled.` (2 failures)

### A5. GoogleTest + gMock — C++ 업계 표준

- **무엇**: Google의 C++ 테스트 프레임워크. fixture(`TEST_F`), 파라미터화(`TEST_P`), death test, gMock(매처 · 순서 · 횟수)
- **장점**: 가장 널리 쓰인다 · IDE/CI 지원 최고(`--gtest_output=xml`) · **`TEST_P` 파라미터화** · 강력한 매처(`_`, `SetArrayArgument`, `InSequence`, `StrictMock`)
- **단점**: C++17 필요(1.15 기준) · 무겁다(MCU 타깃에는 부적합, host 전용으로 생각) · C 함수를 mock 하려면 **C 함수 → 전역 mock 객체** 브리지를 손으로 만든다

```cpp
// GoogleTest: fixture class (TEST_F) + value-parameterized test (TEST_P), like pytest fixture/parametrize.
#include <gtest/gtest.h>

extern "C" {
#include "rb.h"
}

class RingBufferTest : public ::testing::Test {
protected:
    void SetUp() override { rb_init(&rb); }
    rb_t rb;
};

TEST_F(RingBufferTest, NewBufferIsEmpty)
{
    uint8_t v;
    EXPECT_EQ(0u, rb_count(&rb));
    EXPECT_FALSE(rb_pop(&rb, &v));
}

TEST_F(RingBufferTest, All16SlotsUsableThenFull)
{
    for (int i = 0; i < 16; i++)
        ASSERT_TRUE(rb_push(&rb, static_cast<uint8_t>(i))) << "push #" << i;
    EXPECT_FALSE(rb_push(&rb, 99)) << "17th push must fail (drop-new)";
}

// 같은 테스트를 시작 인덱스만 바꿔 여러 번 (pytest.mark.parametrize 와 같은 역할)
class RingBufferWrap : public ::testing::TestWithParam<uint32_t> {};

TEST_P(RingBufferWrap, FifoFromAnyStartIndex)
{
    rb_t rb;
    uint8_t v;
    rb.head = rb.tail = GetParam();
    for (uint8_t i = 0; i < 10; i++)
        rb_push(&rb, i);
    ASSERT_EQ(10u, rb_count(&rb));
    for (uint8_t i = 0; i < 10; i++) {
        ASSERT_TRUE(rb_pop(&rb, &v));
        EXPECT_EQ(i, v);
    }
}

INSTANTIATE_TEST_SUITE_P(StartIndex, RingBufferWrap,
                         ::testing::Values(0u, 5u, UINT32_MAX - 3, UINT32_MAX));
```

```cpp
// GoogleTest + gMock: a C HAL function forwards to a mock object (the usual "C function → C++ mock" bridge).
#include <gmock/gmock.h>
#include <gtest/gtest.h>

extern "C" {
#include "hal_i2c.h"
#include "sensor.h"
}

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArrayArgument;

class MockI2c {
public:
    MOCK_METHOD(int, read, (uint8_t addr, uint8_t reg, uint8_t *buf, size_t n));
};

static MockI2c *g_i2c;                               // 테스트 동안만 유효

extern "C" int hal_i2c_read(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n)
{
    return g_i2c->read(addr, reg, buf, n);
}

class SensorTest : public ::testing::Test {
protected:
    void SetUp() override { g_i2c = &i2c; }
    void TearDown() override { g_i2c = nullptr; }
    ::testing::StrictMock<MockI2c> i2c;              // 예상 밖 호출은 실패
};

TEST_F(SensorTest, ReadsTemperature)
{
    const uint8_t raw[2] = {0x09, 0x60};
    EXPECT_CALL(i2c, read(SENSOR_ADDR, SENSOR_REG_TEMP, _, 2))
        .WillOnce(DoAll(SetArrayArgument<2>(raw, raw + 2), Return(0)));
    int16_t t;
    EXPECT_EQ(0, sensor_read_temp_centi(&t));
    EXPECT_EQ(2400, t);
}

TEST_F(SensorTest, RetriesOnceThenSucceeds)
{
    const uint8_t raw[2] = {0x00, 0x64};
    ::testing::InSequence seq;                       // 순서까지 검사
    EXPECT_CALL(i2c, read(_, _, _, _)).WillOnce(Return(-5));
    EXPECT_CALL(i2c, read(_, _, _, _))
        .WillOnce(DoAll(SetArrayArgument<2>(raw, raw + 2), Return(0)));
    int16_t t;
    EXPECT_EQ(0, sensor_read_temp_centi(&t));
    EXPECT_EQ(100, t);
}

TEST_F(SensorTest, GivesUpAfterTwoErrors)
{
    EXPECT_CALL(i2c, read(_, _, _, _)).Times(2).WillRepeatedly(Return(-5));
    int16_t t = 7;
    EXPECT_EQ(-1, sensor_read_temp_centi(&t));
    EXPECT_EQ(7, t);
}
```

```text
[----------] 4 tests from StartIndex/RingBufferWrap
[ RUN      ] StartIndex/RingBufferWrap.FifoFromAnyStartIndex/2
[       OK ] StartIndex/RingBufferWrap.FifoFromAnyStartIndex/2 (0 ms)
...
[==========] 9 tests from 3 test suites ran. (0 ms total)
[  PASSED  ] 9 tests.
```

### A 정리 — C 단위 테스트 4종 비교

| | Unity | Ceedling (Unity+CMock) | CppUTest | GoogleTest + gMock |
|---|---|---|---|---|
| 테스트 언어 | C | C | C++ | C++ |
| 크기 / 타깃 실행 | 아주 작음 / ✅ | 작음 / ✅ (설정 필요) | 작음 / ✅ | 큼 / host 위주 |
| mock | CMock (별도 생성) | **CMock 자동** | 내장 (손으로 연결) | gMock (브리지 필요) |
| 파라미터화 | 약함 | 약함 | 약함 | **`TEST_P`** |
| 누수 검출 | ❌ | ❌ | **✅ 내장** | ❌ (ASan 사용) |
| JUnit XML | `auto/stylize_as_junit.rb` | `junit_tests_report` 플러그인 | `-ojunit` | `--gtest_output=xml` |
| 외부 의존 | 없음 (러너 생성 시 Ruby) | Ruby | 없음 | 없음 (C++17) |
| 이 Mac에서 | ✓ 3+4 | ✓ 7 | ✓ 6 | ✓ 9 |

> 면접 한 문장: **"For C firmware on a Cortex-M, I'd use Unity with Ceedling — CMock generates HAL mocks from headers, and the same tests can even run on target. If the codebase is C++, GoogleTest and gMock."**

---

## Part B · Python 시스템 · HIL 테스트 (실제 DUT를 바깥에서)

공통 가짜 DUT (실제 리그에선 `FakeSerial` 자리에 `serial.Serial("/dev/ttyUSB0", 115200, timeout=0.5)`):

```python
"""모든 Python 프레임워크 예제가 공유하는 '가짜 DUT' — 실제로는 USB-UART 너머의 FC 펌웨어 CLI.

FakeSerial: pyserial 처럼 write(bytes) / readline() -> bytes. 안에 작은 펌웨어 흉내(FakeFirmware)가 있다.
DutClient : 테스트가 쓰는 드라이버. send("arm") -> "OK". 응답이 없으면 DutTimeout.
실제 리그에선 FakeSerial 대신 serial.Serial("/dev/ttyUSB0", 115200, timeout=0.5) 를 넘기면 된다.
"""


class DutTimeout(Exception):
    pass


class FakeFirmware:
    def __init__(self, temp_c=23.5):
        self.state = "DISARMED"
        self.temp_c = temp_c
        self.mute = False                 # True 면 응답 안 함 (고장 주입)

    def handle(self, cmd):
        if self.mute:
            return None
        if cmd == "ver":
            return "1.4.2"
        if cmd == "status":
            return self.state
        if cmd == "arm":
            if self.state != "DISARMED":
                return "ERR busy"
            self.state = "ARMED"
            return "OK"
        if cmd == "disarm":
            self.state = "DISARMED"
            return "OK"
        if cmd == "temp":
            return f"TEMP={self.temp_c:.1f}"
        return "ERR unknown"


class FakeSerial:
    def __init__(self, firmware=None):
        self.fw = firmware or FakeFirmware()
        self._rx = []

    def write(self, data):
        reply = self.fw.handle(data.decode().strip())
        if reply is not None:
            self._rx.append(reply.encode() + b"\r\n")

    def readline(self):
        return self._rx.pop(0) if self._rx else b""      # b"" = pyserial 의 timeout

    def close(self):
        pass


class DutClient:
    def __init__(self, port):
        self.port = port

    def send(self, cmd):
        self.port.write(cmd.encode() + b"\n")
        line = self.port.readline()
        if not line:
            raise DutTimeout(f"no reply to {cmd!r}")
        return line.decode().strip()

    def temperature(self):
        return float(self.send("temp").split("=")[1])
```

### B1. unittest (PyUnit) — 표준 라이브러리 xUnit

- **무엇**: Python 내장. JUnit을 본뜬 `TestCase` 클래스, `setUp/tearDown`, `setUpClass`, `self.assertEqual`, `subTest`
- **장점**: **설치 불필요** (리그 PC에 pip를 못 쓸 때) · 익숙한 xUnit 구조 · **pytest가 그대로 실행해 준다** (점진적 이전 가능)
- **단점**: 장황함(`self.assert...`) · fixture 조합 · 파라미터화가 약함 · 플러그인 생태계 · JUnit XML은 별도 패키지(또는 pytest로 실행)

```python
"""unittest (= PyUnit, 표준 라이브러리): xUnit 스타일 — TestCase 클래스, setUp/tearDown, self.assert*.

  cd lecture/frameworks && python3 -m unittest -v unittest_demo.test_dut_unittest
"""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "common"))
from fake_dut import DutClient, DutTimeout, FakeFirmware, FakeSerial  # noqa: E402


class DutCliTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):                       # 리그 연결처럼 비싼 것: 클래스당 한 번
        cls.fw = FakeFirmware()

    def setUp(self):                           # 테스트마다: 깨끗한 상태로
        self.fw.state, self.fw.mute = "DISARMED", False
        self.dut = DutClient(FakeSerial(self.fw))

    def tearDown(self):
        self.dut.send("disarm")                # 항상 안전 상태로 되돌리기

    def test_version(self):
        self.assertEqual(self.dut.send("ver"), "1.4.2")

    def test_arm_then_status(self):
        self.assertEqual(self.dut.send("arm"), "OK")
        self.assertEqual(self.dut.send("status"), "ARMED")

    def test_double_arm_rejected(self):
        self.dut.send("arm")
        self.assertTrue(self.dut.send("arm").startswith("ERR"))

    def test_temperature_in_range(self):
        for temp in (-40.0, 23.5, 85.0):       # subTest = 표 하나에서 여러 케이스, 실패도 각각 보고
            with self.subTest(temp=temp):
                self.fw.temp_c = temp
                self.assertAlmostEqual(self.dut.temperature(), temp, places=1)

    def test_no_reply_raises(self):
        self.fw.mute = True
        with self.assertRaises(DutTimeout):
            self.dut.send("ver")
        self.fw.mute = False


if __name__ == "__main__":
    unittest.main(verbosity=2)
```

```text
test_arm_then_status ... ok
test_double_arm_rejected ... ok
test_no_reply_raises ... ok
test_temperature_in_range ... ok
test_version ... ok
Ran 5 tests — OK          (pytest 로 같은 파일 실행: 5 passed, 3 subtests passed)
```

### B2. pytest — 기준점

- 개념과 예제는 [L00 Part 2](2026-10-04_L00_special_lecture.md) · [N02](../notes/2026-10-02_N02_pytest_in_practice.md). 같은 DUT 시나리오를 pytest로 쓴 비교용 파일:

```python
"""pytest: 같은 DUT 시나리오를 pytest 로 — 다른 프레임워크와 1:1 비교용.

  cd lecture/frameworks && ../../.venv-fw/bin/python -m pytest -q pytest_demo
"""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "common"))
from fake_dut import DutClient, DutTimeout, FakeFirmware, FakeSerial  # noqa: E402


@pytest.fixture
def fw():
    return FakeFirmware()


@pytest.fixture
def dut(fw):
    d = DutClient(FakeSerial(fw))
    yield d
    fw.mute = False
    d.send("disarm")                              # 항상 안전 상태로


def test_version(dut):
    assert dut.send("ver") == "1.4.2"


def test_arm_then_status(dut):
    assert dut.send("arm") == "OK"
    assert dut.send("status") == "ARMED"


def test_double_arm_rejected(dut):
    dut.send("arm")
    assert dut.send("arm").startswith("ERR")


@pytest.mark.parametrize("temp", [-40.0, 23.5, 85.0])
def test_temperature(fw, dut, temp):
    fw.temp_c = temp
    assert dut.temperature() == pytest.approx(temp, abs=0.05)


def test_silent_dut_times_out(fw, dut):
    fw.mute = True
    with pytest.raises(DutTimeout):
        dut.send("ver")
```

- unittest와 비교: fixture가 **필요한 테스트만 이름으로** 받고, 정리(yield 뒤)가 준비 바로 옆에 있고, 파라미터화가 데코레이터 한 줄

### B3. Robot Framework — keyword 기반, 비개발자도 읽는다

- **무엇**: 표 형식 `.robot` 파일에 **keyword**(사람이 읽는 동작 이름)를 나열. keyword는 Python 라이브러리 메서드로 구현 (`arm_should_succeed` → `Arm Should Succeed`)
- **장점**: 테스트 기술자 · QA · 운용 쪽이 읽고 고칠 수 있다 · **HTML log/report가 아주 좋다**(단계별 펼치기) · 태그로 골라 실행(`--include smoke`) · `--xunit`으로 JUnit · SSH · Serial · REST 라이브러리가 많다
- **단점**: 로직(반복 · 분기 · 자료구조)이 들어가면 금방 읽기 어려워진다 · 디버깅이 Python보다 불편 · 결국 Python 라이브러리를 누군가 짜야 한다(2층 구조)

```robot
*** Settings ***
Documentation     FC CLI smoke test — keyword-driven. 테스트 엔지니어가 아닌 사람도 읽고 고칠 수 있게.
Library           DutLibrary.py
Suite Setup       Open DUT    sim
Suite Teardown    Close DUT
Test Teardown     Send Command    disarm

*** Variables ***
${EXPECTED_FW}    1.4.2

*** Test Cases ***
Firmware Version Is Released Version
    [Tags]    smoke
    Firmware Version Should Be    ${EXPECTED_FW}

Arm Then Status Reports Armed
    [Tags]    smoke    arming
    ${reply}=    Send Command    arm
    Should Be Equal    ${reply}    OK
    ${state}=    Send Command    status
    Should Be Equal    ${state}    ARMED

Double Arm Is Rejected
    [Tags]    arming
    Send Command    arm
    ${reply}=    Send Command    arm
    Should Start With    ${reply}    ERR

Temperature Within Operating Range
    [Template]    Temperature Case
    -40    -41    -39
    23.5   20     30
    85     84     86

Silent DUT Times Out
    [Tags]    fault
    Make DUT Silent
    Command Should Time Out    ver
    [Teardown]    Make DUT Responsive

*** Keywords ***
Temperature Case
    [Arguments]    ${set}    ${low}    ${high}
    Set Simulated Temperature    ${set}
    Temperature Should Be Between    ${low}    ${high}
```

```python
"""Robot Framework keyword 라이브러리: Python 메서드 하나 = .robot 파일의 keyword 하나.
메서드 이름 'arm_should_succeed' → keyword 'Arm Should Succeed'.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "common"))
from fake_dut import DutClient, DutTimeout, FakeFirmware, FakeSerial  # noqa: E402


class DutLibrary:
    ROBOT_LIBRARY_SCOPE = "SUITE"             # 스위트 동안 같은 인스턴스 (리그 연결 재사용)

    def __init__(self):
        self.fw = FakeFirmware()
        self.dut = None

    def open_dut(self, port="sim"):
        """실제 리그라면 serial.Serial(port, 115200, timeout=0.5)."""
        self.dut = DutClient(FakeSerial(self.fw))

    def close_dut(self):
        if self.dut:
            self.dut.send("disarm")
            self.dut = None

    def send_command(self, cmd):
        return self.dut.send(cmd)

    def firmware_version_should_be(self, expected):
        got = self.dut.send("ver")
        if got != expected:
            raise AssertionError(f"version {got} != {expected}")

    def set_simulated_temperature(self, celsius):
        self.fw.temp_c = float(celsius)

    def temperature_should_be_between(self, low, high):
        t = self.dut.temperature()
        if not float(low) <= t <= float(high):
            raise AssertionError(f"temperature {t} not in [{low}, {high}]")
        return t

    def make_dut_silent(self):
        self.fw.mute = True

    def make_dut_responsive(self):
        self.fw.mute = False

    def command_should_time_out(self, cmd):
        try:
            self.dut.send(cmd)
        except DutTimeout:
            return
        raise AssertionError(f"{cmd!r} unexpectedly got a reply")
```

```text
Firmware Version Is Released Version                                  | PASS |
Arm Then Status Reports Armed                                         | PASS |
Double Arm Is Rejected                                                | PASS |
Temperature Within Operating Range                                    | PASS |
Silent DUT Times Out                                                  | PASS |
5 tests, 5 passed, 0 failed
--variable EXPECTED_FW:9.9.9 로 돌리면 → "version 1.4.2 != 9.9.9", 5 tests, 4 passed, 1 failed
```

### B4. behave — BDD (Given / When / Then)

- **무엇**: Gherkin 문법의 `.feature` 파일(사람 말 시나리오) + Python step 함수. Cucumber의 Python판
- **장점**: **요구사항 문서 = 실행되는 테스트** · PM · 시스템 엔지니어와 합의하기 좋다 · `Scenario Outline` + `Examples` 표로 파라미터화 · `--junit`
- **단점**: step 문장이 늘어나면 관리 비용 · 문장 매칭이 깨지기 쉽다 · 단순 기능 테스트엔 과하다 · fixture 대신 `environment.py` 훅

```gherkin
Feature: Arming over the CLI
  As a pilot I can only arm a disarmed aircraft,
  so that a second "arm" never re-triggers the motors.

  Background:
    Given a connected DUT in state DISARMED

  Scenario: Arm a disarmed DUT
    When I send "arm"
    Then the reply is "OK"
    And the status is "ARMED"

  Scenario: Arming twice is rejected
    Given the DUT is armed
    When I send "arm"
    Then the reply starts with "ERR"

  Scenario: A silent DUT is reported as a timeout
    Given the DUT stops responding
    When I send "ver"
    Then the command times out

  Scenario Outline: Temperature readout matches the sensor
    Given the sensor reads <temp> C
    Then the reported temperature is <temp> C

    Examples:
      | temp  |
      | -40.0 |
      | 23.5  |
      | 85.0  |
```

```python
"""Gherkin 문장 → Python 함수. {name} 은 문자열 인자, {temp:g} 는 숫자로 파싱."""
from behave import given, then, when

from fake_dut import DutTimeout


@given("a connected DUT in state DISARMED")
def step_connected(context):
    assert context.dut.send("status") == "DISARMED"


@given("the DUT is armed")
def step_armed(context):
    assert context.dut.send("arm") == "OK"


@given("the DUT stops responding")
def step_silent(context):
    context.fw.mute = True


@given("the sensor reads {temp:g} C")
def step_sensor(context, temp):
    context.fw.temp_c = temp


@when('I send "{cmd}"')
def step_send(context, cmd):
    try:
        context.reply, context.error = context.dut.send(cmd), None
    except DutTimeout as e:
        context.reply, context.error = None, e


@then('the reply is "{expected}"')
def step_reply_is(context, expected):
    assert context.reply == expected, f"got {context.reply!r}"


@then('the reply starts with "{prefix}"')
def step_reply_starts(context, prefix):
    assert context.reply.startswith(prefix), f"got {context.reply!r}"


@then('the status is "{state}"')
def step_status(context, state):
    assert context.dut.send("status") == state


@then("the command times out")
def step_timeout(context):
    assert isinstance(context.error, DutTimeout)


@then("the reported temperature is {temp:g} C")
def step_temp(context, temp):
    assert abs(context.dut.temperature() - temp) < 0.05
```

```python
"""behave 훅: pytest 의 fixture / conftest 역할. 시나리오마다 새 DUT, 끝나면 안전 상태로."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "common"))
from fake_dut import DutClient, FakeFirmware, FakeSerial  # noqa: E402


def before_scenario(context, scenario):
    context.fw = FakeFirmware()
    context.dut = DutClient(FakeSerial(context.fw))


def after_scenario(context, scenario):
    context.fw.mute = False
    context.dut.send("disarm")
```

```text
features/arming.feature  ......
1 feature passed, 0 failed, 0 skipped
6 scenarios passed, 0 failed, 0 skipped
21 steps passed, 0 failed, 0 skipped
```

### B 정리 — 같은 DUT 시나리오, 네 가지 스타일

| | unittest | pytest | Robot Framework | behave |
|---|---|---|---|---|
| 테스트 모양 | 클래스 · 메서드 | 함수 | keyword 표 | 사람 말 시나리오 |
| 준비 · 정리 | setUp / setUpClass | **fixture (scope · yield)** | Suite/Test Setup | environment.py 훅 |
| 파라미터화 | subTest | **parametrize** | Test Template | Scenario Outline |
| 읽는 사람 | 개발자 | 개발자 · 테스트 엔지니어 | **비개발자 포함** | **PM · 시스템 엔지니어** |
| 리포트 | 텍스트 | 텍스트 + JUnit · HTML 플러그인 | **HTML log/report** + xunit | 텍스트 + JUnit |
| 설치 | 내장 | pip | pip | pip |
| 이 Mac에서 | ✓ 5 | ✓ 7 | ✓ 5 | ✓ 6 시나리오 |

---

## Part C · 공장 테스트 스테이션 — OpenHTF

- **무엇**: Google의 하드웨어 테스트 프레임워크. **phase**(단계) 함수 · **measurement**(이름 + 한계 + 단위) · **plug**(장비 · DUT 연결 객체) · **test record**(시리얼 번호, 모든 측정값, 판정)
- **장점**: 결과가 pass/fail이 아니라 **측정값 데이터** → 수율 · 한계 조정 · 추적성 · 웹 운용자 UI(station server) · 운용자 프롬프트 · 출력 콜백(JSON, MfgInspector)
- **단점**: 개발 단위 테스트용이 아니다 · 문서가 얇고 커뮤니티가 작다 · **Python 3.10+**(1.6 기준) · JUnit 콜백이 기본 제공되지 않는다(설치본의 `output/callbacks`에는 `console_summary` · `json_factory` · `mfg_inspector`만 있었다)
- Neros 연결: Michael이 **factory test FW가 아직 없다**고 했다 [확인됨 10-01 Michael] → 스테이션 쪽 프레임워크로 OpenHTF를 **검토 후보**로 말할 수 있다 ([S07](../design/2026-10-03_S07_manufacturing_factory_test.md))

```python
"""OpenHTF: 공장 테스트 스테이션 스타일. phase(단계) → measurement(측정값 + 합격 범위) → test record(JSON).

  cd lecture/frameworks && ../../.venv-fw/bin/python openhtf_demo/fc_station.py SN0001
  ../../.venv-fw/bin/python openhtf_demo/fc_station.py SN0002 --hot     # 온도 한계 초과 → FAIL 기록

pytest 와 다른 점: '통과/실패'만이 아니라 **모든 측정값과 한계, 단위, 시리얼 번호**가 기록으로 남는다 → 수율·추적성.
"""
import sys
from pathlib import Path

import openhtf as htf
from openhtf.output.callbacks import json_factory
from openhtf.plugs import BasePlug
from openhtf.util import units

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "common"))
from fake_dut import DutClient, FakeFirmware, FakeSerial  # noqa: E402

HOT = "--hot" in sys.argv


class DutPlug(BasePlug):
    """plug = 장비/DUT 연결. OpenHTF 가 테스트 시작 때 만들고 끝나면 tearDown 을 부른다."""

    def __init__(self):
        self.fw = FakeFirmware(temp_c=71.0 if HOT else 24.0)
        self.dut = DutClient(FakeSerial(self.fw))

    def send(self, cmd):
        return self.dut.send(cmd)

    def tearDown(self):
        self.dut.send("disarm")


@htf.plug(dut=DutPlug)
@htf.measures(htf.Measurement("fw_version").equals("1.4.2"))
def check_firmware(test, dut):
    test.measurements.fw_version = dut.send("ver")


@htf.plug(dut=DutPlug)
@htf.measures(
    htf.Measurement("board_temp").in_range(0, 60).with_units(units.DEGREE_CELSIUS)
    .doc("Board temperature right after power-up"))
def check_temperature(test, dut):
    test.measurements.board_temp = float(dut.send("temp").split("=")[1])


@htf.plug(dut=DutPlug)
@htf.measures(htf.Measurement("arm_reply").equals("OK"),
              htf.Measurement("state_after_arm").equals("ARMED"))
def check_arming(test, dut):
    test.measurements.arm_reply = dut.send("arm")
    test.measurements.state_after_arm = dut.send("status")


def main():
    serial = next((a for a in sys.argv[1:] if not a.startswith("--")), "SN0001")
    out = Path(__file__).resolve().parent / "out"
    out.mkdir(exist_ok=True)
    test = htf.Test(check_firmware, check_temperature, check_arming,
                    test_name="fc_board_functional", station_id="FCT-01")
    test.add_output_callbacks(json_factory.OutputToJSON(str(out / "{dut_id}.json"), indent=2))
    passed = test.execute(test_start=lambda: serial)
    print(f"{serial}: {'PASS' if passed else 'FAIL'} → {out / (serial + '.json')}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
```

실행 결과 — 정상 보드와 과열 보드:

```text
=================== test: fc_board_functional  outcome: PASS ===================
SN0001: PASS → openhtf_demo/out/SN0001.json
=================== test: fc_board_functional  outcome: FAIL ===================
SN0002: FAIL → openhtf_demo/out/SN0002.json

SN0001 PASS  fw_version=1.4.2 PASS · board_temp=24.0 PASS · arm_reply=OK PASS · state_after_arm=ARMED PASS
SN0002 FAIL  fw_version=1.4.2 PASS · board_temp=71.0 FAIL · arm_reply=OK PASS · state_after_arm=ARMED PASS
record keys: dut_id, start_time_millis, end_time_millis, outcome, outcome_details, marginal, metadata, phases, ...
```

> 면접 한 문장: **"For factory stations I'd want measurements with limits, units and serial numbers recorded on every unit, not just pass/fail — that's what OpenHTF models directly. For engineering tests in CI, pytest."**

---

## Part D · 경량 Raspberry Pi HIL — py-micro-hil

- **무엇**: Raspberry Pi에 DUT의 GPIO · I2C · SPI · UART · Modbus RTU를 직접 연결하고, YAML로 핀을 정의하고, `hil_tests/` 아래 테스트 그룹을 실행해 HTML 리포트를 만드는 경량 프레임워크 ([PyPI](https://pypi.org/project/py-micro-hil/), [GitHub](https://github.com/niwciu/PY_MICRO_HIL/)). 이 강의 기준 최신은 0.1.8
- **장점**: 설정이 YAML 하나 · 핀 충돌 검사 · 리소스 정리 자동 · Unity 스타일 assert(`TEST_ASSERT_EQUAL`) · HTML 리포트 · 값싼 리그(Pi 한 대)
- **단점 — 실제로 돌려 보며 확인한 것**:
- PyPI 메타데이터는 Python ≥3.8이라고 하지만 코드에 `str | None` 문법이 있어 **실제로는 3.10+** 필요 (3.9에서 import 단계 TypeError)
- 의존성에 **`RPi.GPIO` · `spidev`가 조건 없이** 들어 있어 macOS에서 `pip install`이 빌드 실패 → `--no-deps`로 설치
- 0.1.8은 PC에서 `import RPi.GPIO`를 **가짜 모듈로 바꾸기 전에** 실행해서 RPi가 아닌 PC에선 import 단계에서 죽는다 → 가짜 모듈을 먼저 등록하는 `run_sim.py`로 우회
- 내장 PC 시뮬레이션의 GPIO는 **상태가 없다**(`input()`이 항상 0) → 상태와 배선(17→27)을 흉내 내는 fake GPIO로 바꿔 끼웠다
- 결론: **아이디어(YAML 리그 정의 + 그룹 + HTML 리포트)는 좋지만 아직 초기 단계**. 실무 리그에는 pytest + 직접 만든 드라이버 계층이 안전하다 [추정]

```yaml
# 이 리그에 무엇이 어디 붙어 있나 (Raspberry Pi 핀 번호, BCM)
peripherals:
  gpio:
    - pin: 17
      mode: out
      initial: low
      name: arm_led          # DUT 의 ARM 입력에 연결된 출력
    - pin: 27
      mode: in
      name: dut_ready        # DUT 가 준비되면 HIGH
```

```python
from py_micro_hil.assertions import *
from py_micro_hil.framework_API import *


def setup_group():
    TEST_INFO_MESSAGE("ARM line group: drive pin 17, watch pin 27")


def teardown_group():
    get_RPiGPIO_peripheral().write("arm_led", 0)      # 끝나면 항상 안전 상태로


def test_arm_line_drives_high():
    gpio = get_RPiGPIO_peripheral()
    gpio.write("arm_led", 1)
    TEST_ASSERT_EQUAL(1, gpio.read("arm_led"))


def test_dut_reports_ready_when_armed():
    gpio = get_RPiGPIO_peripheral()
    gpio.write("arm_led", 1)
    TEST_ASSERT_EQUAL(1, gpio.read("dut_ready"))    # 배선을 따라 입력이 올라와야 한다


def test_toggle_returns_low():
    gpio = get_RPiGPIO_peripheral()
    gpio.write("arm_led", 1)
    gpio.toggle("arm_led")
    TEST_ASSERT_EQUAL(0, gpio.read("arm_led"))
```

```python
"""py-micro-hil 0.1.8 를 Raspberry Pi 가 아닌 PC(macOS/Linux)에서 시뮬레이션 모드로 돌리는 실행기.

0.1.8 의 RPiPeripherals.py 는 'import RPi.GPIO' 를 먼저 하고 그 다음에 가짜 모듈로 바꿔 끼우기 때문에
RPi.GPIO 가 없는 PC 에서는 import 단계에서 죽는다. 여기서 가짜 모듈을 먼저 등록한 뒤 hiltest CLI 를 부른다.

  cd lecture/frameworks/py_micro_hil && ../../../.venv-fw/bin/python run_sim.py
"""
import importlib.util
import sys
import types
from pathlib import Path

spec = importlib.util.find_spec("py_micro_hil")
mock_path = Path(spec.origin).parent / "peripherals" / "dummyRPiPeripherals.py"
mock_spec = importlib.util.spec_from_file_location("_pmh_dummy", mock_path)
mock = importlib.util.module_from_spec(mock_spec)
mock_spec.loader.exec_module(mock)

# 기본 dummy GPIO 는 상태가 없다 (input() 이 항상 0). 핀 값을 기억하고, 배선(LOOPBACK)을 흉내 내는 fake 로 바꾼다.
LOOPBACK = {17: 27}          # 리그 배선: 출력 17 → DUT → 입력 27 이 따라온다고 가정
_pins = {}


def _output(pin, value):
    _pins[pin] = int(bool(value))
    if pin in LOOPBACK:
        _pins[LOOPBACK[pin]] = _pins[pin]


fake_gpio = types.SimpleNamespace(**vars(mock.GPIO))
fake_gpio.output = _output
fake_gpio.input = lambda pin: _pins.get(pin, 0)
fake_gpio.setup = lambda pin, mode, **kw: _pins.setdefault(pin, int(kw.get("initial", 0) or 0))

rpi = types.ModuleType("RPi")
rpi.GPIO = fake_gpio
sys.modules.update({"RPi": rpi, "RPi.GPIO": fake_gpio, "spidev": mock.spidev})

from py_micro_hil.cli import main  # noqa: E402

sys.exit(main())
```

```text
[PASS] test_arm_line -> test_arm_line_drives_high
[PASS] test_arm_line -> test_dut_reports_ready_when_armed
[PASS] test_arm_line -> test_toggle_returns_low
> Total Tests Run: 3   Passed: 3 ✅   Failed: 0 ❌
OVERALL STATUS: ✅ PASSED
(기본 dummy GPIO 그대로였을 때는 test_arm_line_drives_high: Expected = 1, actual = 0 → FAIL)
```

---

## Part E · 이름은 알아 둘 것 (이번에 실행하지 않음 [미검증])

| 이름 | 한 줄 | 언제 꺼내나 |
|---|---|---|
| pytest-embedded (Espressif) | pytest 플러그인: 빌드 · 플래시 · 시리얼 expect를 fixture로 | ESP32 · QEMU 타깃 테스트 |
| labgrid | 보드 · 전원 · 시리얼 · USB를 원격 리소스로 관리하는 Python 라이브러리 (pytest 플러그인) | HIL 팜 리그 예약 · 원격 제어 (N04 문제 B) |
| Zephyr ztest + Twister | Zephyr RTOS의 테스트 프레임워크 + 다중 보드 실행기 | Zephyr를 쓰는 팀 |
| Hypothesis | Python property-based 테스트 (반례 자동 축소) | 파서 · 프로토콜 퍼징 (L00, 문제 06) |
| FFF (Fake Function Framework) | 헤더 하나짜리 C fake 함수 매크로 (Ceedling 플러그인도 있음) | CMock이 무거울 때 |
| LAVA | 대규모 보드 팜 자동화 (Linaro) | 리눅스 보드 CI |

## Part F · 종합 비교

| 프레임워크 | 층 | 언어 | 강점 | 약점 | JUnit/CI | 이번 실행 |
|---|---|---|---|---|---|---|
| Unity | C 단위 | C | 아주 작음, target 가능 | 러너 생성 필요 | 변환 스크립트 | ✓ |
| CMock | C mock | C (+Ruby) | 헤더→mock 자동 | 플러그인 학습 | (Unity와 함께) | ✓ |
| Ceedling | C 단위 빌드 | C (+Ruby) | 러너 · mock · 리포트 자동 | Ruby, 설정 | 플러그인 | ✓ |
| CppUTest | C/C++ 단위 | C++ | 누수 검출, 내장 mock | 수동 mock 연결 | `-ojunit` | ✓ |
| GoogleTest/gMock | C++ 단위 | C++ | 표준, `TEST_P`, 매처 | 무거움, C 브리지 | `--gtest_output=xml` | ✓ |
| unittest | Python 시스템 | Python | 내장, 설치 불필요 | 장황, 약한 fixture | 별도/pytest | ✓ |
| pytest | Python 시스템 · HIL | Python | fixture · parametrize · 플러그인 | (기준점) | `--junitxml` | ✓ |
| Robot Framework | 시스템 · 인수 | DSL + Python | 비개발자, HTML 로그 | 로직 많으면 난해 | `--xunit` | ✓ |
| behave | 인수 (BDD) | Gherkin + Python | 스펙 = 테스트 | step 관리 비용 | `--junit` | ✓ |
| OpenHTF | 공장 스테이션 | Python | 측정값 · 한계 · 레코드 | 문서 · 커뮤니티, 3.10+ | JSON 콜백 | ✓ |
| py-micro-hil | 경량 Pi HIL | Python | YAML 리그 정의, HTML | 0.1.x, 설치 · import 문제 | HTML | ✓ (우회) |

## Part G · 면접 질문과 영어 답

**Q. Have you used anything besides pytest?**

> "For firmware unit tests in C, Unity with CMock — Ceedling generates the runners and mocks HAL headers, so I can test retry and error handling without hardware. I know CppUTest and GoogleTest as alternatives; GoogleTest if the code is C++. On the system side, unittest is fine when you can't install anything, Robot Framework when non-developers need to read and maintain tests, and OpenHTF for factory stations where every measurement needs limits and a serial number."

**Q. How would you choose a framework for our test team?**

> "By layer. Firmware developers own unit tests on the host — Unity and Ceedling for C, GoogleTest for C++. The test team owns system and HIL tests in pytest, because fixtures map naturally onto rigs and it emits JUnit XML for GitLab. For the factory, a station framework that records measurements — OpenHTF, or pytest with a results schema. I'd avoid mixing too many: two or three frameworks with a shared results pipeline."

**Q. Why not Robot Framework for everything?**

> "It's great when the readers aren't programmers, and its logs are excellent. But once tests need loops, data structures or tricky timing, the keyword layer gets hard to read and debug, and someone still writes the Python library underneath. For an engineering-heavy test team I'd default to pytest and use Robot only where the audience demands it."

**Q. How do you test C code that talks to hardware registers or a HAL?**

> "Put a thin HAL boundary in a header, then on the host replace it with a mock — CMock generates one from the header, or a hand-written fake. That lets me test retries, timeouts and error paths that are hard to trigger on real hardware. Then a smaller set of the same scenarios runs on the rig against the real peripheral."

## Part H · 직접 돌려 보기

```bash
cd ~/workspace/dons_tech_study_dashboard/job_interview_prep_research/neros_tech_senior_firmware_engineer/technicalScreenPrep
bash lecture/frameworks/setup_fw_env.sh      # Python 3.12 환경 .venv-fw (uv, 폴더 안에만 설치)
bash lecture/frameworks/c/fetch_vendor.sh    # Unity · CMock · CppUTest · GoogleTest 소스 (c/_vendor, git 무시)
bash lecture/frameworks/run_all.sh           # 10종 실행 → ✓/✗ 요약
make -C lecture/frameworks/c/googletest test # 하나씩도 가능
```

| 폴더 | 내용 |
|---|---|
| `frameworks/c/fw/` | 피검 C 코드 (rb, sensor, hal_i2c.h) |
| `frameworks/c/{unity,ceedling,cpputest,googletest}/` | 프레임워크별 테스트 + Makefile / project.yml |
| `frameworks/common/fake_dut.py` | Python 예제 공통 가짜 DUT |
| `frameworks/{unittest_demo,pytest_demo,robot_demo,behave_demo,openhtf_demo,py_micro_hil}/` | Python 프레임워크 예제 |

## 체크

- [ ] 0절 표를 가리고 "시나리오 → 프레임워크"를 6개 말하기
- [ ] A2 CMock 테스트에서 `ExpectAndReturn` · `ReturnArrayThruPtr`가 무엇을 하는지 설명
- [ ] B 정리 표: 같은 시나리오를 unittest · pytest · Robot · behave로 쓰면 무엇이 다른지 30초
- [ ] OpenHTF가 pytest와 다른 점 한 문장 (측정값 · 한계 · 시리얼 번호 기록)
- [ ] Part G 첫 질문 답을 소리 내어 두 번
- [ ] `run_all.sh`를 직접 돌려 10개 ✓ 확인
