# 🧰 04 · HIL 리그용 conftest.py — CLI 옵션 · marker skip · session fixture · 실패 시 로그

> pytest를 "써 본" 사람과 "프레임워크로 세운" 사람의 차이는 `conftest.py`에서 난다. 리그를 세션당 한 번 열고, 테스트마다 DUT를 리셋하고, 하드웨어가 없으면 hw 테스트를 skip 하고, **실패한 테스트에서만** 로그를 저장하는 conftest를 직접 쓴다. 채점기는 `pytester`로 내 conftest를 임시 프로젝트에 넣고 여러 옵션으로 돌려 본다.

## 왜 나오나

- "How would you structure a pytest framework for HIL?" — 테스트 팀 면접의 단골 질문. 말로만 답하면 누구나 비슷하다. **코드로 보여 줄 수 있으면** 확실한 차별점
- Full Stack 관점: 웹 백엔드에서도 같은 패턴이다 — session-scoped DB, test마다 트랜잭션 롤백, `--integration` 옵션. "리그 = DB 서버"로 번역해 말할 수 있다
- 시스템 디자인 라운드의 "테스트 프레임워크 설계"와 직결 → [N04 system design](../../notes/2026-10-02_N04_system_design_test_tooling.md)

## 문제

> "Write a conftest.py for our HIL suite. The rig is expensive to open — once per session. Each test should start from a clean DUT. People run the suite on laptops with no rig, so hardware tests must skip there. And when a test fails, save the DUT log so we can debug it from CI artifacts."

| # | 요구사항 | pytest 기능 |
|---|---|---|
| 1 | `--rig` 옵션: `none`(기본) / `sim`, 그 밖은 usage error | `pytest_addoption`, `choices=` |
| 2 | `@pytest.mark.hw` 테스트는 `--rig=none`이면 skip (reason에 "no rig"), `--strict-markers`에서도 통과 | `pytest_configure` marker 등록, `pytest_collection_modifyitems` |
| 3 | `rig` fixture: session scope, `FakeRig(name)` → `power_on` → 끝나면 **실패해도** `power_off` → `close` | `scope="session"`, `yield` + `try/finally` |
| 4 | `dut` fixture: 테스트마다 `reset_dut()` 후 `rig.dut` | function scope, fixture 의존 |
| 5 | 테스트 본체가 실패하면 `rig.save_log(tmp_path / "dut.log")` | `pytest_runtest_makereport` hookwrapper, `request.node` |

- 가짜 리그: [lib/fakerig.py](../lib/fakerig.py) — 모든 동작을 `FakeRig.events`에 기록
- 예시 테스트: [starters/04_hil_conftest/test_rig.py](../starters/04_hil_conftest/test_rig.py)
- 직접 돌려 보기: `cd python/starters/04_hil_conftest && ../../../.venv/bin/python -m pytest -c ../../pytest.ini --rootdir ../.. --rig=sim -v`

## 채점기가 확인하는 것

| 시나리오 | 기대 |
|---|---|
| 옵션 없음 | hw 3개 skip, 일반 1개 pass, 리그를 **열지 않음** |
| `--rig=sim` | 4개 pass, 리그 open **1번**, reset **3번**, 마지막 두 동작이 `power_off`, `close` |
| `--rig=sim` + 실패 테스트 1개 | save_log **1번**(실패한 테스트 경로), 그래도 `power_off` → `close` |
| `--rig=bogus` | exit code 4 (usage error) |
| `--strict-markers --rig=sim` | 4개 pass (marker 등록됨) |

## 힌트

1. marker skip 표준 패턴 (pytest 문서의 "control skipping of tests according to command line option"):

```python
def pytest_collection_modifyitems(config, items):
    if config.getoption("--rig") != "none":
        return
    skip_hw = pytest.mark.skip(reason="no rig (run with --rig=sim)")
    for item in items:
        if "hw" in item.keywords:
            item.add_marker(skip_hw)
```

2. 실패 여부를 fixture에서 알기 (pytest 문서 "making test result information available in fixtures"):

```python
@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    rep = outcome.get_result()
    setattr(item, f"rep_{rep.when}", rep)      # rep_setup / rep_call / rep_teardown
```

3. fixture teardown 순서: function fixture(`dut`) teardown → … → 세션 끝에 `rig` teardown. 그래서 save_log는 리그가 아직 켜져 있을 때 실행된다

## 풀이 해설

- **session scope의 대가**: 테스트 사이에 상태가 샌다 → function-scoped `dut`가 매번 리셋해서 막는다. "비싼 건 넓게, 상태는 좁게"
- **`try/finally`를 yield 주변에**: yield 이후 코드는 테스트가 실패해도 실행되지만, **fixture 자체**(power_on 이후)에서 예외가 나는 경우까지 대비
- **실패 시 아티팩트**: CI에서 가장 비싼 건 "재현 안 되는 실패". 실패 순간의 DUT 로그·리그 상태를 `tmp_path`에 남기고 CI가 artifact로 수집 → [FTE N05 CI](../../../firmwareTestEngineerPrep/site/notes/2026-09-28_N05_ci_cd_git.html)
- **`pytester`**: pytest 플러그인·conftest를 테스트하는 공식 도구. 채점기가 그걸 쓴다는 것 자체가 "테스트 인프라도 테스트한다"는 이야깃거리

## 말하면서 풀기

- "Opening the rig is slow, so it's session-scoped. But I don't want state leaking between tests, so a function-scoped dut fixture resets it every time."
- "Hardware tests are marked, and collection skips them when there's no rig — so the same suite runs on a laptop and on the rig runner."
- "On failure I want evidence, not just a red X: the hook attaches the result to the test item, and the fixture saves the DUT log only when the call phase failed."

## 꼬리 질문

- "Multiple rigs in parallel?" → `pytest-xdist` 워커마다 리그 하나: `worker_id` fixture로 리그 선택, 또는 리그 풀에서 lock으로 예약 → [N04 system design](../../notes/2026-10-02_N04_system_design_test_tooling.md) HIL 팜
- "A test hangs on hardware." → `pytest-timeout`, 그리고 teardown에서 전원 차단(하드 리셋)이 보장되게
- "How do you pass rig config (ports, firmware path)?" → `--rig-config rig.yaml` 옵션 + session fixture가 읽어서 dataclass로 → [FTE N04 11.2](../../../firmwareTestEngineerPrep/site/notes/2026-09-28_N04_pytest_framework.html)
- "Retry flaky hardware tests automatically?" → `pytest-rerunfailures`는 증상을 숨긴다. 재시도는 허용하되 **flaky로 기록**하고 대시보드에서 추적

## 파일

- [starter conftest](../starters/04_hil_conftest/conftest.py) · [모범답안 conftest](../solutions/04_hil_conftest/conftest.py) · [예시 테스트](../solutions/04_hil_conftest/test_rig.py) · [채점기](../graders/test_04_hil_conftest.py)
- 채점: `python3 python/run.py 04` · `python3 python/run.py 04 --sol`
