"""07 · CI results analyzer — 모범답안

(1) JUnit XML (pytest --junitxml, GitLab CI `artifacts:reports:junit` 이 읽는 포맷) 을 파싱해 요약.
(2) 여러 커밋에 걸친 실행 이력 [(commit_idx, test, "pass"|"fail")] 을 보고 테스트를
    stable / failing / flaky / regression / fixed 로 분류하고, regression 이면 처음 깨진 커밋을 찾는다.
(3) git bisect 처럼 "처음 나빠진 커밋"을 이진 탐색하는 first_bad_commit.

  python3 python/solutions/07_ci_results_analyzer.py
"""
import xml.etree.ElementTree as ET
from collections import defaultdict
from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional, Sequence, Tuple


@dataclass
class JUnitSummary:
    total: int = 0
    passed: int = 0
    failed: int = 0
    errors: int = 0
    skipped: int = 0
    time_s: float = 0.0
    failures: List[Tuple[str, str]] = field(default_factory=list)   # (classname::name, message)


def parse_junit(xml_text: str) -> JUnitSummary:
    """<testsuites> 루트든 <testsuite> 루트든 동작. suite 속성 대신 testcase 를 직접 센다."""
    root = ET.fromstring(xml_text)
    s = JUnitSummary()
    for case in root.iter("testcase"):
        s.total += 1
        s.time_s += float(case.get("time", 0) or 0)
        test_id = f'{case.get("classname", "")}::{case.get("name", "")}'
        failure, error = case.find("failure"), case.find("error")
        if failure is not None:
            s.failed += 1
            s.failures.append((test_id, failure.get("message", "")))
        elif error is not None:
            s.errors += 1
            s.failures.append((test_id, error.get("message", "")))
        elif case.find("skipped") is not None:
            s.skipped += 1
        else:
            s.passed += 1
    return s


def classify(history: Sequence[Tuple[int, str, str]]) -> Dict[str, Tuple[str, Optional[int]]]:
    """test -> (category, first_bad_commit).

    category
      stable      : 전부 pass
      failing     : 전부 fail
      flaky       : 같은 커밋에서 pass 와 fail 이 섞임, 또는 커밋 순으로 pass/fail 이 2번 넘게 뒤집힘
      regression  : pass ... pass fail ... fail  (first_bad = 처음 fail 커밋)
      fixed       : fail ... fail pass ... pass
    """
    by_test: Dict[str, Dict[int, set]] = defaultdict(lambda: defaultdict(set))
    for commit, test, result in history:
        by_test[test][commit].add(result)

    out: Dict[str, Tuple[str, Optional[int]]] = {}
    for test, commits in by_test.items():
        if any(len(results) > 1 for results in commits.values()):
            out[test] = ("flaky", None)                      # 코드가 같은데 결과가 다름
            continue
        seq = [(c, next(iter(commits[c]))) for c in sorted(commits)]
        results = [r for _, r in seq]
        flips = sum(1 for a, b in zip(results, results[1:]) if a != b)
        if flips == 0:
            out[test] = ("stable" if results[0] == "pass" else "failing", None)
        elif flips == 1 and results[0] == "pass":
            first_bad = next(c for c, r in seq if r == "fail")
            out[test] = ("regression", first_bad)
        elif flips == 1:
            out[test] = ("fixed", None)
        else:
            out[test] = ("flaky", None)                      # pass→fail→pass: 의심
    return out


def first_bad_commit(is_bad: Callable[[int], bool], good: int, bad: int) -> int:
    """git bisect: is_bad(good)==False, is_bad(bad)==True 를 가정. 처음으로 bad 인 커밋 반환.
    호출 횟수 O(log n) — HIL 한 번 돌리는 데 10분이면 이게 핵심."""
    while bad - good > 1:
        mid = (good + bad) // 2
        if is_bad(mid):
            bad = mid
        else:
            good = mid
    return bad


# ------------------------------------------------------------------ tests
SAMPLE_XML = """<?xml version="1.0" encoding="utf-8"?>
<testsuites>
  <testsuite name="hil_smoke" tests="5" failures="1" errors="1" skipped="1" time="12.5">
    <testcase classname="test_arming" name="test_arm_happy_path" time="2.0"/>
    <testcase classname="test_arming" name="test_arm_refused_throttle_high" time="1.5">
      <failure message="armed with throttle 1500">AssertionError</failure>
    </testcase>
    <testcase classname="test_telemetry" name="test_attitude_rate" time="5.0"/>
    <testcase classname="test_telemetry" name="test_gps_rate" time="4.0">
      <error message="serial port /dev/ttyACM0 not found">OSError</error>
    </testcase>
    <testcase classname="test_rf" name="test_elrs_bind" time="0">
      <skipped message="no RF chamber on this runner"/>
    </testcase>
  </testsuite>
</testsuites>"""


def test_parse_junit_counts():
    s = parse_junit(SAMPLE_XML)
    assert (s.total, s.passed, s.failed, s.errors, s.skipped) == (5, 2, 1, 1, 1)
    assert abs(s.time_s - 12.5) < 1e-9
    assert s.failures[0] == ("test_arming::test_arm_refused_throttle_high", "armed with throttle 1500")
    assert s.failures[1][0] == "test_telemetry::test_gps_rate"


def test_parse_junit_single_suite_root():
    xml = '<testsuite name="x"><testcase classname="a" name="b"/></testsuite>'
    s = parse_junit(xml)
    assert s.total == 1 and s.passed == 1


def test_classify_categories():
    history = []
    for c in range(6):
        history.append((c, "t_stable", "pass"))
        history.append((c, "t_failing", "fail"))
        history.append((c, "t_regress", "pass" if c < 4 else "fail"))
        history.append((c, "t_fixed", "fail" if c < 2 else "pass"))
        history.append((c, "t_alternate", "pass" if c % 2 == 0 else "fail"))
    history += [(3, "t_flaky", "pass"), (3, "t_flaky", "fail"), (4, "t_flaky", "pass")]
    got = classify(history)
    assert got["t_stable"] == ("stable", None)
    assert got["t_failing"] == ("failing", None)
    assert got["t_regress"] == ("regression", 4)
    assert got["t_fixed"] == ("fixed", None)
    assert got["t_alternate"] == ("flaky", None)
    assert got["t_flaky"] == ("flaky", None)


def test_classify_unordered_history():
    history = [(5, "t", "fail"), (1, "t", "pass"), (3, "t", "pass"), (4, "t", "fail")]
    assert classify(history)["t"] == ("regression", 4)


def test_first_bad_commit_log_calls():
    calls = []

    def is_bad(c):
        calls.append(c)
        return c >= 737

    assert first_bad_commit(is_bad, 0, 1000) == 737
    assert len(calls) <= 10                          # log2(1000) ≈ 10
    assert first_bad_commit(lambda c: True, 0, 1) == 1


if __name__ == "__main__":
    for _n, _f in list(globals().items()):
        if _n.startswith("test_") and callable(_f):
            _f()
            print("PASS", _n)
