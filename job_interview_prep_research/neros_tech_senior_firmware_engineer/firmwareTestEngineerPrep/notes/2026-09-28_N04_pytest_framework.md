# 🧪 N04 · pytest 처음부터 — 그리고 HIL 테스트 프레임워크 설계

> JD: "Design, develop, and maintain **test suites**", "Build and enhance automated **testing frameworks and tools**", "Strong development skills with a scripting language (e.g. **Python**) for test automation". 레쥬메에 pytest가 명시돼 있지 않으므로, 여기서 **기본 문법 → fixture → 옵션·리포트 → mock → 프레임워크 아키텍처**까지 한 번에 채운다.
> 설치가 안 된 환경이면 `python3 -m pip install --user pytest` 한 번. 코딩 문제(`python/`)는 pytest 없이도 돌도록 만들어져 있다.

---

## 1. pytest가 하는 일 — 30초 요약

- `test_*.py` 파일 안의 `test_*` 함수를 **자동으로 찾아서**(discovery) 실행한다
- 그냥 `assert`를 쓰면 실패 시 **양쪽 값을 자세히 보여 준다**(assert rewriting)
- **fixture**로 준비·정리 코드를 테스트에서 분리하고 재사용한다
- **parametrize**로 같은 테스트를 입력만 바꿔 여러 번 돌린다
- **marker**로 테스트를 분류하고 골라서 돌린다 (`-m hil`)
- **plugin**으로 확장한다 (timeout, rerun, xdist 병렬, html 리포트)

비교: 표준 라이브러리 `unittest`는 클래스 상속 + `self.assertEqual` 스타일. pytest는 `unittest` 테스트도 그대로 돌릴 수 있다.

---

## 2. Discovery — 무엇을 테스트로 인식하나

| 대상 | 기본 규칙 |
|---|---|
| 파일 | `test_*.py` 또는 `*_test.py` |
| 함수 | 이름이 `test`로 시작 |
| 클래스 | `Test`로 시작하고 `__init__`이 없는 클래스 안의 `test*` 메서드 |
| 설정 | `pytest.ini`, `pyproject.toml`의 `[tool.pytest.ini_options]`, `setup.cfg` |

자주 쓰는 실행 옵션:

```bash
pytest                         # 현재 폴더 아래 전부
pytest tests/test_boot.py      # 파일 하나
pytest tests/test_boot.py::test_version_banner   # 함수 하나
pytest -k "failsafe and not gps"   # 이름 키워드로 선택
pytest -m hil                  # marker로 선택
pytest -x                      # 첫 실패에서 멈춤
pytest --lf                    # 지난번 실패한 것만 (last failed)
pytest -v -s                   # 자세히 + print 출력 보이기
pytest --junitxml=report.xml   # CI용 리포트
pytest --durations=10          # 가장 느린 10개
```

---

## 3. assert rewriting

```python
def test_telemetry_rate():
    rate = measure_rate()          # 9.2 라고 하자
    assert rate == pytest.approx(10.0, rel=0.05)
```

실패하면 pytest는 `assert 9.2 == 10.0 ± 5.0e-01` 처럼 **실제 값과 기대값**을 보여 준다. unittest처럼 메서드를 외울 필요가 없다.

- 실수 비교는 **`pytest.approx`** (rel 또는 abs 허용오차)
- 예외 검증은 **`pytest.raises`**

```python
def test_bad_frame_rejected():
    with pytest.raises(ValueError, match="crc"):
        parse_frame(b"\xc8\x04\x16\x00\x00\xff")
```

- 실패 메시지를 직접 붙이려면 `assert cond, f"got {x}"` — HIL에서는 **항상 실제 값을 메시지에 넣는다** (CI 로그만 보고 원인을 추정할 수 있게)

---

## 4. Fixture — 준비와 정리를 분리하기

### 4.1 기본

```python
import pytest

@pytest.fixture
def fc():
    dev = FlightController(port="/dev/hil/fc")
    dev.connect()
    yield dev              # ← 여기서 테스트가 실행된다
    dev.disarm()           # ← 테스트가 실패해도 teardown은 실행된다
    dev.close()

def test_version(fc):      # 인자 이름으로 fixture를 받는다
    assert fc.version().startswith("4.")
```

- `yield` 앞 = setup, 뒤 = teardown. **테스트가 실패해도 teardown은 돈다** → 하드웨어를 항상 안전한 상태(disarm)로 되돌리는 데 핵심
- fixture끼리 의존 가능: `def rc(fc): ...`

### 4.2 scope — 얼마나 오래 살리나

| scope | 생성 시점 | HIL 예시 |
|---|---|---|
| `function` (기본) | 테스트마다 | 설정 초기화된 FC 상태 |
| `class` | 클래스마다 | 드물게 사용 |
| `module` | 파일마다 | 특정 설정으로 플래시한 보드 |
| `session` | 전체 실행에 한 번 | 리그 연결, 펌웨어 플래시, PSU 핸들, 로그 디렉터리 |

```python
@pytest.fixture(scope="session")
def rig(request):
    cfg = load_rig_config(request.config.getoption("--rig"))
    r = Rig(cfg)
    r.health_check()           # 리그 고장이면 여기서 전체 중단 → "리그 문제"와 "FW 버그" 분리
    r.flash(request.config.getoption("--fw"))
    yield r
    r.power_off()

@pytest.fixture
def fc(rig):                    # function scope: 테스트마다 깨끗한 상태
    rig.power_cycle()
    rig.fc.wait_ready(timeout=10)
    yield rig.fc
    rig.fc.disarm()
```

- 비싼 것(플래시, 리그 연결)은 session, **테스트 간 상태 누수**를 막아야 하는 것은 function
- 트레이드오프: 매 테스트 power cycle은 느리지만 독립성이 높다 → smoke는 session 재사용, regression은 function 초기화 같은 식으로 선택

### 4.3 conftest.py

- 같은 폴더(와 하위 폴더) 테스트에서 **import 없이** 쓰는 fixture·hook을 두는 파일
- 보통 루트 `conftest.py`에 리그·옵션·리포트 hook, 하위 폴더 `conftest.py`에 기능별 fixture

### 4.4 내장 fixture 몇 개

- `tmp_path`: 테스트별 임시 폴더 (로그 파일 저장 테스트)
- `capsys` / `caplog`: stdout / logging 캡처
- `monkeypatch`: 환경변수·속성 임시 교체
- `request`: 현재 테스트 정보, CLI 옵션 접근 (`request.config.getoption`)

---

## 5. parametrize — 입력만 바꿔 여러 번

```python
@pytest.mark.parametrize("baud", [115200, 230400, 420000])
def test_crsf_link_at_baud(rc_emulator, fc, baud):
    rc_emulator.set_baud(baud)
    assert fc.wait_rc_valid(timeout=1.0)

@pytest.mark.parametrize("vbat, expected", [
    (16.8, "OK"),
    (14.0, "WARNING"),
    (13.2, "CRITICAL"),
], ids=["full", "warn", "crit"])
def test_battery_state(psu, fc, vbat, expected):
    psu.set_voltage(vbat)
    assert fc.wait_for_battery_state(expected, timeout=2.0)
```

- `ids`로 리포트에 읽기 좋은 이름이 나온다: `test_battery_state[warn]`
- 경계값 테스트(스윕)를 짧게 표현 → Don의 **shmoo(마진 스윕)** 경험과 같은 사고방식

---

## 6. Marker — 분류하고 골라 돌리기

```python
@pytest.mark.hil                 # 사용자 정의 marker
@pytest.mark.slow
def test_soak_30min(fc): ...

@pytest.mark.skipif(sys.platform != "linux", reason="needs udev paths")
def test_usb_reenumeration(rig): ...

@pytest.mark.xfail(reason="BUG-1234 telemetry drop at 1:2 ratio", strict=True)
def test_high_telem_ratio(fc): ...
```

- 사용자 marker는 `pytest.ini`에 등록 (`markers = hil: needs HIL rig`) + `--strict-markers`로 오타 방지
- `skip` / `skipif`: 조건이 안 맞으면 실행 안 함
- `xfail`: **알려진 버그**. 실패해도 빨간불 아님. `strict=True`면 **갑자기 통과하면 실패 처리** → 버그가 고쳐졌는데 xfail이 남아 있는 걸 잡는다
- CI에서: `pytest -m "not hil"` (서버) / `pytest -m "hil and smoke"` (MR) / `pytest -m hil` (nightly)

---

## 7. CLI 옵션 추가 — pytest_addoption

```python
# conftest.py
def pytest_addoption(parser):
    parser.addoption("--rig", default="local", help="rig config name")
    parser.addoption("--port", default=None, help="override FC serial port")
    parser.addoption("--board", default="rev-b", choices=["rev-a", "rev-b"])
    parser.addoption("--fw", default=None, help="path to firmware binary to flash")

@pytest.fixture(scope="session")
def board(request):
    return request.config.getoption("--board")
```

```bash
pytest -m hil --rig hil-fc-03 --board rev-b --fw build/fc.hex --junitxml=report.xml
```

- 같은 테스트 코드를 **리그·보드 리비전만 바꿔** 돌리는 게 핵심. 테스트 안에 포트 이름·경로를 하드코딩하지 않는다

---

## 8. 리포트 — JUnit XML과 GitLab

- `pytest --junitxml=report.xml` → GitLab CI에서 `artifacts:reports:junit: report.xml`로 올리면 **MR 화면에 실패한 테스트 목록**이 나온다 (N05)
- `record_property` fixture로 리포트에 값 추가 (리그 ID, 펌웨어 hash, 측정값)

```python
def test_boot_time(fc, record_property):
    t = fc.measure_boot_time()
    record_property("boot_time_s", round(t, 3))
    record_property("rig", fc.rig_id)
    assert t < 2.0
```

- 측정값을 리포트에 남기면 PASS여도 **추세**(부팅 시간이 커밋마다 늘어나는지)를 볼 수 있다

---

## 9. 자주 쓰는 plugin

| plugin | 용도 | 주의 |
|---|---|---|
| `pytest-timeout` | 테스트별 제한 시간 (`@pytest.mark.timeout(30)`) | HIL에서 필수. 하드웨어 대기가 영원히 멈추는 걸 막는다 |
| `pytest-rerunfailures` | 실패 시 재실행 (`--reruns 2`) | **flakiness를 숨긴다.** 쓰면 rerun 횟수를 반드시 기록·추적 |
| `pytest-xdist` | 병렬 실행 (`-n 4`) | 단위·SITL 테스트용. 하드웨어 1대를 공유하는 HIL엔 부적합 (리그별로 나눠야) |
| `pytest-html` | HTML 리포트 | 사람이 보는 nightly 결과 |
| `pytest-cov` | 커버리지 | 호스트 테스트 대상. MCU 코드 커버리지는 별도 도구 |

면접 포인트:

> Reruns are useful to keep the pipeline moving, but a test that passes on rerun is still a flaky test. I'd record every rerun and track the flaky rate per test, so retries never hide a real intermittent bug — on a drone, an intermittent failure is exactly the kind that shows up in the field.

---

## 10. unittest.mock — 하드웨어 없이 테스트 라이브러리를 테스트하기

테스트 **프레임워크 자체**도 코드다. 리그 없이 CI 서버에서 테스트할 수 있어야 한다.

```python
from unittest.mock import MagicMock, patch

def test_wait_ready_retries_until_banner():
    port = MagicMock()
    port.readline.side_effect = [b"", b"booting...\n", b"READY v4.5.1\n"]
    fc = FlightController(port)
    assert fc.wait_ready(timeout=1.0) == "v4.5.1"
    assert port.readline.call_count == 3

@patch("hil.drivers.serial.Serial")          # 모듈 안에서 import된 이름을 patch
def test_connect_opens_right_port(mock_serial):
    FlightController.open("/dev/hil/fc", baud=115200)
    mock_serial.assert_called_once_with("/dev/hil/fc", 115200, timeout=0.1)
```

- `MagicMock`: 아무 속성·메서드 호출을 받아 주고, 호출 기록을 남김
- `side_effect`: 호출마다 다른 값 반환(리스트), 또는 예외 발생
- `return_value`: 항상 같은 값
- `patch("모듈.이름")`: **사용되는 곳**의 이름을 patch해야 한다 (정의된 곳이 아니라) — 흔한 실수
- mock보다 **Fake 객체**(작은 가짜 구현, 예: `FakeSerial`이 바이트 버퍼를 가짐)가 더 읽기 쉬울 때가 많다. 코딩 문제들이 이 방식

---

## 11. HIL 테스트 프레임워크 아키텍처

### 11.1 계층 구조

```text
┌──────────────────────────────────────────────────────────────┐
│ Tests (pytest)            test_failsafe.py, test_boot.py      │  ← "무엇을" 검증하나. 요구사항 언어
├──────────────────────────────────────────────────────────────┤
│ Test library / keywords   fc.arm(), rc.set_sticks(), wait_for │  ← 도메인 동작. 재사용 단위
│                           _state(), motors.expect_idle()      │
├──────────────────────────────────────────────────────────────┤
│ Device drivers            FlightController, CrsfEmulator,     │  ← 장비·프로토콜별 API
│                           PowerSupply(SCPI), LogicAnalyzer    │
├──────────────────────────────────────────────────────────────┤
│ Transports                SerialTransport, UdpTransport,      │  ← 바이트를 주고받는 것만
│                           SimTransport (SITL), FakeTransport  │
└──────────────────────────────────────────────────────────────┘
          Rig config (YAML): 포트, 장비 주소, 보드 리비전, 허용오차
```

- **transport를 바꾸면 같은 테스트가 SITL·HIL·Fake에서 모두 돈다** → 이게 가장 중요한 설계 포인트
- 테스트 코드엔 sleep·포트 이름·바이트 파싱이 **없어야** 한다. 그건 아래 계층의 일
- Don의 챔버 SDK(온도 제어·스케줄링·UART 시퀀스 API)가 정확히 "device drivers + keywords" 계층

### 11.2 리그별 설정

```yaml
# rigs/hil-fc-03.yaml
rig_id: hil-fc-03
board: rev-b
fc:        { port: /dev/hil/fc03, baud: 115200 }
rc:        { port: /dev/hil/crsf03, baud: 420000 }
psu:       { visa: "TCPIP::10.0.3.21::INSTR", channel: 1 }
analyzer:  { serial: "ABC123" }
limits:    { boot_time_s: 2.0, telem_rate_hz: [9.5, 10.5] }
```

- 리그 정보는 코드가 아니라 **데이터**. 새 리그 추가 = YAML 추가
- 허용오차도 설정에 두되 **출처(스펙 문서)** 를 주석으로

### 11.3 로깅과 artifact

- 테스트마다 폴더: FC 시리얼 로그, telemetry 원본, logic analyzer 캡처(실패 시), PSU 전류 로그, blackbox 덤프
- 실패한 테스트에서만 무거운 캡처를 저장하려면 hook 사용:

```python
# conftest.py
@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    rep = outcome.get_result()
    if rep.when == "call" and rep.failed and "rig" in item.fixturenames:
        rig = item.funcargs["rig"]
        rig.dump_artifacts(item.nodeid)      # 시리얼 로그, 캡처, 크래시 덤프
```

- 모든 로그 줄에 **monotonic timestamp + 출처**(fc/rc/psu) → 나중에 시간순으로 합쳐서 본다
- 결과에 항상 기록: **펌웨어 git hash, 리그 ID, 보드 리비전, 테스트 코드 git hash**

### 11.4 하드웨어 잠금 (locking)

- 리그 하나를 두 잡이 동시에 쓰면 둘 다 망가진다
- 1차: CI 레벨 — GitLab `resource_group` 또는 리그당 runner 1개(`concurrent = 1`)
- 2차: 프레임워크 레벨 — 리그 호스트의 파일 lock (`fcntl.flock`)을 session fixture에서 잡는다. 사람이 수동으로 리그를 쓸 때도 같은 lock을 쓰게 CLI 제공
- 잠금 대기에도 타임아웃과 "누가 잡고 있나" 메시지

### 11.5 좋은 프레임워크의 기준 (면접 답변용 체크)

- 새 테스트를 **10줄 안에** 쓸 수 있다 (keyword가 충분)
- 실패 메시지만 보고 **리그 문제인지 FW 문제인지** 1차 판단이 된다
- 같은 테스트가 SITL/HIL/Fake에서 돈다
- 결과가 **추세**로 남는다 (flaky율, 측정값)
- 문서화: 테스트 이름 = 요구사항, docstring에 스펙 링크

---

## 12. Robot Framework — 한 문단

Robot Framework는 키워드 기반 테스트 프레임워크로, 테스트를 표 형식의 자연어 비슷한 문장(`Arm Drone`, `Set Throttle    30`)으로 쓰고 키워드 구현은 Python 라이브러리로 한다. 비개발자(테스트 기술자, 양산 라인)도 테스트를 읽고 쓰기 쉬워서 HIL·factory test에서 많이 쓰인다. 단점은 복잡한 로직·데이터 처리가 불편하고 디버깅이 Python보다 번거롭다. 면접에서는 "pytest든 Robot이든 **keyword 계층을 Python 라이브러리로 두면** 어느 러너를 써도 재사용된다"로 정리하면 된다. Neros가 무엇을 쓰는지는 모른다 [추정 불가] → 역질문 거리.

---

## 13. 면접 예상 질문 + 영어 모범 답변

### Q1. "How do you structure a test framework for HIL?"

> In layers. Tests are written in requirement language — arm, inject RC loss, expect failsafe within the deadline. They call a keyword library, which sits on device drivers for the flight controller, the RC emulator, the power supply and the capture hardware, and those sit on transports. Because the transport is swappable, the same test runs against SITL, a real rig, or a fake in unit tests. Rig details live in config files, not in test code. And every result records the firmware hash, rig ID and measured values, so failures are diagnosable from CI alone.

### Q2. "What's a pytest fixture and how would you use scopes on a rig?"

> A fixture is setup and teardown code that tests request by name; with yield, the teardown runs even if the test fails, which is how I guarantee the drone is disarmed and powered safely after every test. I'd use session scope for expensive things — connecting to the rig, health check, flashing firmware — and function scope for the device state each test needs, like a fresh power cycle, so tests don't leak state into each other.

### Q3. "Would you use automatic reruns for flaky tests?"

> Sparingly, and never silently. A rerun can keep a merge request unblocked, but a pass-on-rerun is recorded as flaky and tracked per test. Above a threshold the test goes to quarantine with an owner and a ticket. On a drone, an intermittent failure on the bench is exactly what becomes a field failure, so I want it visible.

### Q4. "How do you test your test code?"

> The drivers and keyword library get unit tests with fakes and unittest.mock — for example, a fake serial port that returns a boot banner in pieces, to check the wait logic handles partial reads and timeouts. That runs on every commit on a normal runner, so the framework itself doesn't break the HIL rigs.

### Q5. "Have you used pytest?"

> (정직하게) I've written Python test automation — at SK hynix I built the test-platform SDK and automation scripts that other engineers used for chamber testing. My resume doesn't say pytest specifically, so to be concrete: I'm comfortable with fixtures, parametrize, markers, custom CLI options and JUnit reports, and I'd structure the HIL library so it doesn't depend on the runner anyway. (Don: 실제 pytest 사용 경험이 있으면 첫 문장 교체)

---

## 14. 체크리스트

- [ ] pytest 설치 후 `python/solutions/` 파일 하나를 `pytest -v`로 돌려 보기
- [ ] fixture `yield` teardown이 **실패 시에도** 도는 걸 직접 확인 (일부러 assert False)
- [ ] session vs function scope 차이를 HIL 예시로 영어 30초 설명
- [ ] `pytest_addoption`으로 `--rig` 옵션 추가하는 코드를 안 보고 써 보기
- [ ] `xfail(strict=True)`가 왜 유용한지 한 문장
- [ ] `patch`는 "사용되는 곳"을 patch한다 — 예시로 설명
- [ ] §11.1 계층 그림을 그리고 Q1 답변 소리 내어 연습
- [ ] Q5 답변을 Don 실제 경험에 맞게 수정
