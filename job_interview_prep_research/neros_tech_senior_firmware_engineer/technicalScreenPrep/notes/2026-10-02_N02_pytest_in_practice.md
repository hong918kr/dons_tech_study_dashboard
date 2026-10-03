# 🧪 N02 · pytest 실전 — 라이브 코딩에서 바로 쓰는 fixture · parametrize · mock · conftest

> 개념 설명(discovery, assert rewriting, scope, plugin, HIL 프레임워크 계층)은 이미 [FTE N04 pytest 노트](../../firmwareTestEngineerPrep/site/notes/2026-09-28_N04_pytest_framework.html)에 있다. 이 노트는 **손**을 위한 것이다: 공유 에디터에서 "write tests for this"를 받았을 때 15분 안에 쓰는 순서, 외워 둘 문법, mock 패턴, 그리고 면접관이 이어서 묻는 질문의 영어 답. 실제로 돌려 볼 venv가 `technicalScreenPrep/.venv`에 있다.

## 0. 환경 — 직접 돌려 보기

```bash
cd ~/workspace/dons_tech_study_dashboard/job_interview_prep_research/neros_tech_senior_firmware_engineer/technicalScreenPrep
python3 -m venv .venv && .venv/bin/pip install pytest        # 이미 설치됨 (pytest 8.4.2)
python3 python/run.py 02            # 내 테스트 채점 (mutant 9개)
.venv/bin/python -m pytest -c python/pytest.ini --rootdir python python/solutions/02_pytest_ring_buffer.py -v
```

| 옵션 | 언제 |
|---|---|
| `-x` / `--maxfail=2` | 첫 실패에서 멈춤 |
| `-k "wrap and not slow"` | 이름으로 고르기 |
| `-m hw` / `-m "not hw"` | marker로 고르기 |
| `--lf` / `--ff` | 지난번 실패만 / 실패 먼저 |
| `-vv` · `-s` · `-rs` | 자세히 · print 보이기 · skip 이유 |
| `--tb=short` / `--tb=line` | traceback 길이 |
| `--durations=5` | 느린 테스트 5개 |

## 1. 외워 둘 문법 — 한 화면

```python
import pytest

def test_basic():
    assert add(2, 3) == 5                      # 실패하면 pytest가 양쪽 값을 보여 준다

def test_raises():
    with pytest.raises(IndexError, match="empty"):
        RingBuffer(1).pop()

def test_float():
    assert 0.1 + 0.2 == pytest.approx(0.3)

@pytest.mark.parametrize("cap, n, expected", [
    (3, 5, [2, 3, 4]),
    (1, 2, [1]),
], ids=["cap3", "cap1"])
def test_overwrite(cap, n, expected): ...

@pytest.fixture
def rb():                                      # function scope (기본): 테스트마다 새로
    return RingBuffer(4)

@pytest.fixture(scope="session")
def rig():
    r = open_rig()
    yield r                                    # yield 뒤 = teardown, 테스트가 실패해도 실행
    r.close()

@pytest.fixture
def make_frame():                              # factory fixture: 인자 있는 준비물
    def _make(payload, corrupt=False): ...
    return _make
```

| 내장 fixture | 용도 | 한 줄 예 |
|---|---|---|
| `tmp_path` | 테스트별 임시 폴더 (`pathlib.Path`) | `log = tmp_path / "dut.log"` |
| `monkeypatch` | 속성·환경변수·dict 임시 교체, 자동 복구 | `monkeypatch.setenv("RIG", "sim")` |
| `capsys` / `capfd` | stdout/stderr 캡처 (fd는 C 출력까지) | `out, err = capsys.readouterr()` |
| `caplog` | logging 레코드 검사 | `assert "retry" in caplog.text` |
| `request` | 현재 테스트 정보, config 옵션 | `request.config.getoption("--rig")` |
| `tmp_path_factory` | session scope 임시 폴더 | 문제 07의 `.so` 빌드 |
| `pytester` | conftest·플러그인 자체를 테스트 | 문제 04 채점기 |

marker: `@pytest.mark.skip(reason=…)`, `@pytest.mark.skipif(sys.platform == "win32", reason=…)`, `@pytest.mark.xfail(reason="bug #123", strict=True)`, 커스텀 `@pytest.mark.hw` (pytest.ini `markers =`에 등록, `--strict-markers`).

## 2. "Write tests for X" — 15분 순서

1. **(1분) 스펙을 소리 내 요약**: "So the contract is: … and the failure modes are …"
2. **(2분) 테스트 목록부터 이름으로**: 본문 없이 `def test_…` 6~8개. 이름이 스펙 문장이 되게
3. **(2분) fixture**: 상태별 준비물(빈 / 가득 / wrap된) — 본문을 "행동 → 검증"만 남기기
4. **(6분) 경계부터 채우기**: 빈 것 → 하나 → 정확히 가득 → 하나 넘침 → 여러 바퀴 → 잘못된 입력
5. **(2분) parametrize로 표 만들기**: 비슷한 테스트 3개가 보이면 하나로
6. **(2분) 마무리 멘트**: 무엇을 안 했는지(동시성, 성능, 실제 HW)와 어떻게 하겠는지

> 테스트 목록 체크리스트: **0 · 1 · N · N+1 · 경계 ± 1 · 잘못된 타입 · 예외 경로 · 자원 정리 · 순서 · 멱등성**

## 3. mock — 하드웨어 · 시간 · 외부 의존을 끊는 법

```python
from unittest.mock import Mock, MagicMock, call, patch, create_autospec

port = Mock(spec=["write", "readline", "reset_input_buffer", "close"])   # 없는 메서드 부르면 AttributeError
port.readline.return_value = b"TEMP=25.0\r\n"                           # 매번 같은 값
port.readline.side_effect = [b"", b"TEMP=21.0\r\n"]                     # 호출마다 다음 값
port.readline.side_effect = TimeoutError("no reply")                    # 예외 던지기
port.readline.side_effect = lambda: next(script)                        # 함수로 계산

port.write.assert_called_once_with(b"TEMP?\n")
assert port.write.call_args_list == [call(b"TEMP?\n")] * 3
port.close.assert_not_called()
assert port.write.call_count == 1

with patch("mydriver.time.sleep") as fake_sleep:     # '쓰는 쪽' 모듈 경로를 패치
    ...
fake = create_autospec(serial.Serial, instance=True)  # 실제 시그니처까지 검사 (pyserial 있을 때)
```

| 용어 | 뜻 | 예 |
|---|---|---|
| **stub** | 정해진 값만 돌려줌 | `readline.return_value = …` |
| **mock** | 호출을 기록하고 검증 | `assert_called_once_with` |
| **fake** | 단순하지만 실제로 동작하는 구현 | `FakeSerial`(버퍼 + 가짜 시계), `FakeRig`, SQLite in-memory |
| **spy** | 실제 객체를 감싸 호출만 기록 | `Mock(wraps=real_port)` |

- **원칙**: 경계(포트, 시계, 네트워크, 파일시스템)만 바꾼다. 내부 메서드를 mock 하면 구현에 묶인 테스트 → 리팩터링마다 깨진다
- **DI > patch**: 생성자로 `port`, `sleep`, `clock`을 받으면 패치가 필요 없다 ([문제 03](../python/problems/03_mock_serial_driver.md), [FTE 문제 04](../../firmwareTestEngineerPrep/site/problems/04_serial_dut_driver.html))
- **patch 경로 함정**: `from time import sleep`으로 가져온 모듈이면 `patch("mydriver.sleep")`, `import time`이면 `patch("mydriver.time.sleep")`. "patch where it's looked up, not where it's defined"

## 4. conftest.py — 프레임워크가 되는 지점

| 무엇 | 어떻게 | 이 폴더의 예 |
|---|---|---|
| 공용 fixture | 같은 폴더·하위 폴더 테스트가 import 없이 사용 | `rig`, `dut` |
| CLI 옵션 | `pytest_addoption` → `request.config.getoption` | `--rig=sim` |
| marker 기반 skip | `pytest_collection_modifyitems` | hw 테스트를 랩톱에서 skip |
| 실패 시 증거 | `pytest_runtest_makereport` hookwrapper → fixture에서 `request.node.rep_call.failed` | `save_log(tmp_path/…)` |
| 리포트 | `--junitxml=report.xml` → CI가 수집 | [FTE N04 8절](../../firmwareTestEngineerPrep/site/notes/2026-09-28_N04_pytest_framework.html) |

→ 직접 써 보기: [문제 04 HIL conftest](../python/problems/04_hil_conftest.md) (채점기가 `pytester`로 검사)

## 5. 테스트 설계 패턴 — 말로 꺼낼 무기

- **model-based (참조 모델 비교)**: `deque`를 정답으로, 무작위 연산 1000번을 seed 고정으로 → [문제 02](../python/problems/02_pytest_ring_buffer.md), [07](../python/problems/07_ctypes_c_ring_buffer.md)
- **chunking invariance**: 같은 바이트 스트림을 1, 2, 7, 64, 전체 크기로 잘라도 같은 결과 → [문제 06](../python/problems/06_stream_framer.md)
- **property-based (Hypothesis)**: `@given(st.binary())` — 파서가 절대 크래시하지 않음, 정상 프레임은 항상 복구. 반례를 최소화해서 보여 준다
- **mutation testing**: 내 테스트가 버그를 잡는지 버그를 심어서 확인 (`mutmut`). 이 폴더의 run.py가 그 축소판
- **golden file / snapshot**: 파서 출력이나 리포트를 파일로 저장해 두고 비교, 의도된 변경이면 갱신
- **record/replay**: 실제 장치 응답을 한 번 녹취해 fixture로 재생 → mock이 현실과 어긋나는 걸 막는다

## 6. 면접관이 이어서 묻는 질문 — 영어 30초 답

**Q. Fixture vs setUp/tearDown?**

> "A fixture is requested by name, so each test declares exactly what it needs. Fixtures compose — one can depend on another — they have scopes, and with yield the teardown sits right next to the setup and runs even if the test fails. setUp runs for every test in the class whether it needs it or not."

**Q. How do you choose a fixture scope?**

> "Expensive and stateless-ish things go wide — a rig connection, a compiled library, a database container at session scope. Anything with mutable state stays function-scoped, or I add a function-scoped fixture that resets it. Wide for cost, narrow for state."

**Q. parametrize vs a loop inside one test?**

> "Parametrize gives each case its own pass/fail and its own name in the report, so one bad case doesn't hide the others, and I can rerun just that case with -k."

**Q. When do you mock, and when not?**

> "I mock at the boundaries — the serial port, time, the network — so tests are fast and deterministic. I avoid mocking internals of the thing under test. And I keep at least one test against the real thing, on a rig, so the mocks can't drift away from reality."

**Q. How do you deal with flaky tests?**

> "First, measure: track pass rates per test across runs, so flaky is a number, not a feeling. Then classify — test bug, product race, or rig problem. I'll quarantine with a marker and a ticket so it doesn't block merges, but I won't hide it with blind reruns. And infrastructure failures get reported separately from product failures."

**Q. How do you test the test framework itself?**

> "Unit tests for the drivers with fakes, and pytester for conftest and plugin behavior — I run a throwaway test session and assert on outcomes, like 'hardware tests are skipped without a rig' or 'the log is saved only on failure'."

## 7. 함정 — 실제로 자주 틀리는 것

- `pytest.raises` 블록 안에 **여러 줄** → 첫 줄에서 예외가 나면 뒤는 실행 안 됨. 예외 날 한 줄만 넣기
- mutable 기본 인자를 fixture에서 공유 → 테스트 순서 의존. 매번 새로 만들기
- `assert x == None` 대신 `assert x is None`, 부동소수는 `approx`
- 테스트 이름이 `test_`로 안 시작하거나 파일이 `test_*.py`가 아니면 **조용히 수집 안 됨** (이 폴더는 pytest.ini에서 `NN_*.py`도 수집하게 설정)
- `time.sleep`으로 순서 맞추기 → 느린 CI에서 flaky. 조건을 polling 하는 `wait_until(pred, timeout)` 사용 → [FTE 문제 08](../../firmwareTestEngineerPrep/site/problems/08_retry_and_wait.html)
- session fixture가 상태를 가짐 → 테스트 순서에 따라 결과가 달라짐. `pytest-randomly`로 순서를 섞어 돌리면 드러난다

## 체크

- [ ] 1절 문법 블록을 보지 않고 빈 파일에 다시 써 보기 (5분)
- [ ] [문제 02](../python/problems/02_pytest_ring_buffer.md)를 25분 타이머로 → mutant 9/9
- [ ] [문제 03](../python/problems/03_mock_serial_driver.md)를 25분 타이머로 → mutant 9/9
- [ ] [문제 04](../python/problems/04_hil_conftest.md) conftest를 보지 않고 → 채점기 5/5
- [ ] 6절 답 6개를 소리 내어 한 번씩
