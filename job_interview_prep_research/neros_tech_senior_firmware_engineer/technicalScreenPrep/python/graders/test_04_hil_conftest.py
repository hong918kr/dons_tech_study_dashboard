"""문제 04 채점기 — pytester 로 '테스트 세션을 테스트'한다.

TS_04_DIR 환경변수(기본: solutions/04_hil_conftest)의 conftest.py 를 임시 폴더에 복사하고,
채점용 테스트 파일을 만들어 여러 옵션으로 pytest 를 돌린 뒤 결과와 FakeRig.events 를 검사한다.
"""
import os
from pathlib import Path

import pytest

from lib.fakerig import FakeRig

HERE = Path(__file__).resolve().parent.parent
SRC = HERE / os.environ.get("TS_04_DIR", "solutions/04_hil_conftest")

TESTS = '''
import pytest

@pytest.mark.hw
def test_a(dut):
    dut.arm()
    assert dut.state == "ARMED"

@pytest.mark.hw
def test_b(dut):
    assert dut.state == "DISARMED"

@pytest.mark.hw
def test_c(dut):
    assert dut.version() == "1.4.2"

def test_no_hw():
    assert True
'''

FAILING = '''
import pytest

@pytest.mark.hw
def test_ok(dut):
    assert True

@pytest.mark.hw
def test_broken(dut):
    assert dut.version() == "9.9.9"

@pytest.mark.hw
def test_ok_after(dut):
    assert True
'''


@pytest.fixture
def suite(pytester):
    src = (SRC / "conftest.py").read_text(encoding="utf-8")
    if "NotImplementedError" in src and "TODO" in src:
        pytest.fail(f"TODO: {SRC.parent.name}/{SRC.name}/conftest.py 아직 안 품 (TODO·NotImplementedError 표시를 지우면 채점 시작)", pytrace=False)
    pytester.makeconftest(src)
    FakeRig.events.clear()
    return pytester


def names(events):
    return [e[0] for e in events]


def test_no_rig_skips_hw_and_never_opens_rig(suite):
    suite.makepyfile(test_x=TESTS)
    res = suite.runpytest("-rs")
    res.assert_outcomes(passed=1, skipped=3)
    res.stdout.fnmatch_lines(["*no rig*"])
    assert "open" not in names(FakeRig.events)


def test_sim_rig_opened_once_reset_each_test_closed_at_end(suite):
    suite.makepyfile(test_x=TESTS)
    res = suite.runpytest("--rig=sim")
    res.assert_outcomes(passed=4)
    ev = names(FakeRig.events)
    assert ev.count("open") == 1, ev                 # session scope
    assert ev.count("reset") == 3, ev                # hw 테스트마다
    assert ev[-2:] == ["power_off", "close"], ev     # 정리 순서
    assert ev.index("power_on") < ev.index("reset")


def test_log_saved_only_for_failed_test_and_cleanup_still_runs(suite):
    suite.makepyfile(test_y=FAILING)
    res = suite.runpytest("--rig=sim")
    res.assert_outcomes(passed=2, failed=1)
    saves = [e for e in FakeRig.events if e[0] == "save_log"]
    assert len(saves) == 1, FakeRig.events
    assert "test_broken" in saves[0][1]              # tmp_path 경로에 테스트 이름이 들어간다
    assert names(FakeRig.events)[-2:] == ["power_off", "close"]


def test_invalid_rig_value_is_usage_error(suite):
    suite.makepyfile(test_x=TESTS)
    res = suite.runpytest("--rig=bogus")
    assert res.ret == pytest.ExitCode.USAGE_ERROR


def test_hw_marker_is_registered(suite):
    suite.makepyfile(test_x=TESTS)
    res = suite.runpytest("--strict-markers", "--rig=sim")
    res.assert_outcomes(passed=4)
