# 🐍 11 · HIL test design — arming · failsafe 테스트 케이스 설계하고 쓰기

> 시뮬레이터(FakeFlightController)는 이미 있고, 내가 할 일은 **테스트를 설계하고 쓰는 것**이다. happy path · 거부 조건 표(parametrize) · 경계값 · 상태 전이 · 복구 · false positive. 그리고 이게 실제 HIL 리그에서 어떻게 바뀌는지까지 말한다.

## 왜 나오나

- JD: "**Design, develop, and maintain test suites** to validate the Neros drone & ground control software", "ensuring **comprehensive test coverage**, and enforcing testing best practices", "Hands-on experience building, setting up **HIL** test systems"
- 테스트 엔지니어 면접의 단골 질문은 "이 기능을 어떻게 테스트하겠나?"다. 코드를 짜라고 하든 말로 하라고 하든, **케이스를 체계적으로 뽑는 방법**을 보여 주는 게 핵심이다.
- arming과 failsafe는 드론에서 **안전과 직결되는** 기능이다. 사람 옆에서 모터가 갑자기 돌면 안 되고, 링크가 끊기면 반드시 멈춰야 한다. 방산 FPV라면 더 중요하다.

## 문제

> "Here's a simplified flight controller model. It arms only when the RC link is up, throttle is low, and it's not in failsafe. If the link is lost for more than the timeout while armed, it goes to failsafe and cuts the motors. When the link comes back, it drops to disarmed — no automatic re-arm. How would you test this? Write the tests."

시뮬레이터 규칙 (`FakeFlightController`):

| 조건 | 결과 |
|---|---|
| 링크 up | 마지막 RC 프레임 후 경과 ≤ timeout (경계 포함) |
| `arm()` 성공 | DISARMED + 링크 up + throttle ≤ 1050us |
| `arm()` 실패 | `(False, blockers)`: NO_RX_LINK / THROTTLE_HIGH / FAILSAFE / ALREADY_ARMED |
| ARMED 중 링크 끊김 | FAILSAFE, 모터 즉시 0 |
| FAILSAFE 중 링크 복구 | DISARMED (다시 arm 해야 함) |
| 모터 출력 | ARMED 아니면 [0,0,0,0], ARMED면 throttle → DShot 48–2047 |

> 이건 Betaflight를 단순화한 모델이다. 실제 Betaflight의 arming disable flags와 failsafe stage 1/2 동작은 더 복잡하다 [추정 — 면접에서 "실제로는 더 많은 조건이 있을 것"이라고만 말할 것].

## 먼저 개념: 테스트 케이스를 뽑는 네 가지 방법

- **동등 분할 (equivalence classes)**: 입력을 "같은 결과를 내는 묶음"으로 나눈다. throttle은 {low, high} 두 묶음, 링크는 {up, 한 번도 없음, stale} 세 묶음. 묶음마다 대표값 하나씩.
- **경계값 (boundary values)**: 버그는 경계에 산다. throttle `1050`(허용) vs `1051`(거부), timeout `100ms`(아직 up) vs `101ms`(failsafe). `<`와 `<=`를 헷갈린 코드를 잡는다.
- **부정 테스트 (negative)**: "해서는 안 되는 일이 안 일어나는지". 거부된 arm 뒤에 모터가 정말 0인지. 정상 20Hz RX에서 failsafe가 **안** 뜨는지 (false positive).
- **상태 전이 (state transition)**: 상태 × 이벤트 표를 그리고 모든 칸을 한 번씩 밟는다. DISARMED→ARMED→FAILSAFE→DISARMED→ARMED. 특히 **복구 경로**가 자주 빠진다.

| 상태 \ 이벤트 | arm() | disarm() | 링크 끊김 | 링크 복구 |
|---|---|---|---|---|
| DISARMED | 조건 만족 시 ARMED | 그대로 | 그대로 (arm 불가) | 그대로 |
| ARMED | 거부 ALREADY_ARMED | DISARMED | **FAILSAFE** | 그대로 |
| FAILSAFE | 거부 | 그대로 | 그대로 | **DISARMED** |

## 예시 (써야 할 테스트 7개)

- `test_arm_happy_path` — arm 성공, idle 48, throttle 올리면 출력 증가
- `test_arm_refused_conditions` — 거부 조건 표를 루프(= parametrize), 거부 시 모터 0까지 확인
- `test_arm_allowed_exactly_at_throttle_low` — 경계값 허용
- `test_failsafe_boundary` — timeout 정확히는 ARMED, +1ms는 FAILSAFE
- `test_steady_rx_never_failsafes` — 20Hz 10초 동안 failsafe 없음 (false positive)
- `test_recovery_requires_rearm_and_low_throttle` — 복구 후 DISARMED, 스틱 높으면 arm 거부
- `test_disarm_stops_motors_immediately`

## pytest로 쓰면 이렇게 된다

```python
import pytest

@pytest.fixture
def rig():
    clock = FakeClock()
    fc = FakeFlightController(clock, failsafe_timeout_ms=100)
    fc.rx_frame(1000)
    yield clock, fc
    # teardown: 실제 리그라면 여기서 disarm + 전원 차단 (테스트가 실패해도 실행됨)

@pytest.mark.parametrize("throttle_us, expected_ok", [
    (1000, True), (1050, True), (1051, False), (2000, False),
])
def test_arm_vs_throttle(rig, throttle_us, expected_ok):
    clock, fc = rig
    fc.rx_frame(throttle_us)
    ok, blockers = fc.arm()
    assert ok is expected_ok, blockers

@pytest.mark.hil                       # 실제 하드웨어 필요 → CI에서 HW 러너에서만
def test_failsafe_boundary(rig): ...
```

- **fixture**: 매 테스트마다 깨끗한 시작 상태. `yield` 뒤가 teardown → 실제 리그에서는 **안전 정리(disarm, 전원 off)** 를 여기 둔다.
- **parametrize**: 표 한 줄 = 테스트 한 개로 리포트에 따로 찍힌다. 어떤 경계에서 깨졌는지 바로 보인다.
- **marker**: `-m "not hil"`로 SIL만 빠르게, HW 러너에서만 `-m hil`.

## 실제 HIL 리그에서는 무엇이 바뀌나

| 시뮬레이터 | 실제 리그 |
|---|---|
| `fc.rx_frame(1000)` | RC 입력 주입: CRSF 프레임을 UART로 FC에 직접 송신 (또는 ELRS TX 모듈 제어) |
| `clock.advance(101)` | 실제 시간. RX 주입을 멈추고 `wait_until(state == FAILSAFE, timeout=…)` |
| `fc.state` | MSP로 상태·arming flags 조회, 또는 OSD/telemetry 파싱 |
| `fc.motor_outputs()` | logic analyzer로 모터 핀 DShot 캡처 → decode (09번 문제). **프롭 없이** |
| `make_fc()` | 전원 릴레이로 cold boot → 펌웨어 flash → 설정 로드 → 부팅 완료 대기 |
| 결정적 | 비결정적: USB 재열거, 부팅 시간 편차, RF 간섭 → timeout은 여유 있게, 경계 테스트는 SIL에서 촘촘히 |

- **테스트 피라미드**: 경계값 수십 개는 SIL/유닛(빠름, 결정적)에서, HIL에서는 **실제 타이밍과 하드웨어 경로**를 증명하는 대표 케이스만. 모든 걸 HIL에서 돌리면 느리고 flaky해진다.
- **안전**: HIL 리그에서도 프롭은 떼고, 전원은 전류 제한 공급기, 테스트 실패 시에도 teardown에서 disarm + 전원 차단.

## 엣지 케이스

- 거부 조건이 **여러 개 동시에** 성립할 때 blocker 목록에 다 나오는지 (디버깅할 때 첫 번째만 보이면 한 번에 하나씩 고치게 된다)
- 이미 ARMED에서 `arm()` 재호출 → 상태가 바뀌면 안 된다
- 복구 순간 스틱이 높은 채로 있으면 → 자동으로 모터가 튀면 안 된다 (안전의 핵심)
- timeout 경계: `>`인지 `>=`인지. 명세에 "100ms 초과"인지 "100ms 이상"인지 **먼저 확인**

## 힌트

1. `make_fc()`로 "정상 시작 상태"를 만들고, 각 테스트는 거기서 **한 가지만** 바꾼다.
2. 거부 조건은 `(이름, 준비 함수, 기대 blocker)` 표로 만들고 루프를 돈다. assert 메시지에 이름을 넣어야 어떤 케이스가 깨졌는지 보인다.
3. 경계 테스트는 `advance(timeout)` → 아직 up, `advance(1)` → failsafe. 시간을 직접 움직이니까 sleep이 필요 없다.

## 풀이 해설

- **테스트 하나 = 행동 하나**: 이름만 보고 무엇이 깨졌는지 알 수 있어야 한다. `test_failsafe_boundary`가 빨가면 timeout 비교 연산자를 보면 된다.
- **Arrange–Act–Assert**: 준비(make_fc, arm) → 동작(advance, tick) → 확인(state, motors). 섞지 않는다.
- **상태만 보지 말고 출력도 본다**: state가 FAILSAFE여도 모터가 돌면 의미 없다. 안전 관련 테스트는 **관측 가능한 물리 출력**(모터)까지 확인한다.
- **false positive 테스트**: failsafe가 뜨는 것만 테스트하면, 정상 비행 중 failsafe가 뜨는 버그(과민 반응)를 놓친다. 방산 드론에서는 이것도 치명적이다.
- **흔한 실수**: 테스트끼리 FC 인스턴스 공유(순서 의존) / `time.sleep`으로 timeout 테스트 / 거부 테스트에서 `ok == False`만 보고 모터는 안 봄 / 복구 경로 누락.

## 말하면서 풀기

- "Before writing code I'd list the cases: the happy path, one negative case per arming condition, the boundaries on throttle and on the link timeout, every state transition including recovery, and a false-positive check."
- "Each test starts from a known-good fixture and changes exactly one thing, so a failure points at one cause."
- "For safety features I assert on the physical output, not just the state flag — if the state says failsafe but a motor is still spinning, that's a failure."
- "I'd run the dense boundary tests in simulation where time is deterministic, and on the HIL rig run representative cases that prove the real timing and signal path — injecting CRSF over UART and capturing DShot on the motor pins, props off."
- "Teardown always disarms and cuts power, even when the test fails."

## 꼬리 질문

- 이 테스트를 CI 어디에 넣나? → SIL 버전은 매 MR, HIL 버전은 MR smoke 몇 개 + nightly 전체. HW 러너는 GitLab tagged runner.
- 실제 링크 손실은 어떻게 흉내 내나? → CRSF 주입 중단(가장 결정적), RF 감쇠기(attenuator)로 신호 약화, TX 모듈 전원 차단. 각각 테스트하는 대상이 다르다.
- 테스트가 가끔 FAILSAFE 진입이 늦어서 실패한다면? → 측정: 실제 진입 시간 분포를 로그로 남긴다. timeout + FC 루프 주기 + 관측 지연(MSP 폴링 주기)을 합친 허용치를 요구사항과 대조.
- 커버리지를 어떻게 말하나? → 코드 커버리지(%)보다 **요구사항 커버리지**: 요구사항 ID ↔ 테스트 매핑 표.
- Don 경험 연결: Apple factory test-node 아키텍처에서 "stress 기반 시나리오로 MP 전에 latent defect를 찾은" 경험 → 케이스 설계를 happy path가 아니라 **스트레스·경계 조건 중심**으로 한다는 스토리. (Don: 실제 시나리오 예시 한 줄 채우기)

## 파일

- [starter](../starters/11_hil_test_design.py) (시뮬레이터 완성, 테스트 7개가 TODO) · [모범답안](../solutions/11_hil_test_design.py)
- 채점: `python3 python/run.py 11` (내 테스트) · `python3 python/run.py 11 --sol` (답안)
