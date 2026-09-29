"""01 · UART 로그 파서 — 모범답안

DUT가 UART로 뱉은 부트/테스트 로그를 파싱해서 요약한다.

  [  0.001234] INFO  boot: fw v2.3.1
  [  1.502000] ERROR imu: WHO_AM_I mismatch 0x00
  [  2.100000] TEST  imu_selftest FAIL
  [  2.300000] TEST  baro_read PASS

summarize(lines) -> dict:
  counts       : 레벨별 줄 수 (Counter)
  errors       : ERROR 줄의 (timestamp, module, message) 리스트
  first_fail   : 첫 번째 FAIL 테스트의 timestamp (없으면 None)
  tests        : {test_name: "PASS"/"FAIL"} — 같은 테스트가 여러 번이면 마지막 결과
  malformed    : 형식이 안 맞는 줄 수 (UART 노이즈, 잘린 줄)
"""
import re
from collections import Counter

LINE = re.compile(
    r"^\[\s*(?P<ts>\d+\.\d+)\]\s+"          # [  1.502000]
    r"(?P<level>DEBUG|INFO|WARN|ERROR|TEST)\s+"
    r"(?P<rest>.*)$"
)
TEST = re.compile(r"^(?P<name>\w+)\s+(?P<result>PASS|FAIL)\b")
MSG = re.compile(r"^(?P<module>[\w.]+):\s*(?P<msg>.*)$")


def summarize(lines):
    counts = Counter()
    errors = []
    tests = {}
    first_fail = None
    malformed = 0

    for raw in lines:
        line = raw.strip()
        if not line:
            continue
        m = LINE.match(line)
        if not m:
            malformed += 1
            continue
        ts, level, rest = float(m["ts"]), m["level"], m["rest"].strip()
        counts[level] += 1

        if level == "TEST":
            t = TEST.match(rest)
            if not t:
                malformed += 1
                continue
            tests[t["name"]] = t["result"]
            if t["result"] == "FAIL" and first_fail is None:
                first_fail = ts
        elif level == "ERROR":
            mm = MSG.match(rest)
            module, msg = (mm["module"], mm["msg"]) if mm else ("?", rest)
            errors.append((ts, module, msg))

    return {"counts": counts, "errors": errors, "first_fail": first_fail,
            "tests": tests, "malformed": malformed}


# ------------------------------------------------------------------ tests
SAMPLE = """\
[  0.001234] INFO  boot: fw v2.3.1
[  0.010000] DEBUG clk: sysclk=480MHz
[  1.502000] ERROR imu: WHO_AM_I mismatch 0x00
garbage\x00\xff line from UART noise
[  2.100000] TEST  imu_selftest FAIL
[  2.300000] TEST  baro_read PASS
[  2.9000
[  3.000000] WARN  rx: link quality 40%
[  3.500000] ERROR rx: failsafe entered
[  4.000000] TEST  imu_selftest PASS
""".splitlines()


def test_counts():
    s = summarize(SAMPLE)
    assert s["counts"] == Counter({"INFO": 1, "DEBUG": 1, "ERROR": 2, "TEST": 3, "WARN": 1})


def test_errors():
    s = summarize(SAMPLE)
    assert s["errors"] == [(1.502, "imu", "WHO_AM_I mismatch 0x00"),
                           (3.5, "rx", "failsafe entered")]


def test_first_fail_and_last_result_wins():
    s = summarize(SAMPLE)
    assert s["first_fail"] == 2.1
    assert s["tests"] == {"imu_selftest": "PASS", "baro_read": "PASS"}


def test_malformed_tolerated():
    s = summarize(SAMPLE)
    assert s["malformed"] == 2


def test_empty_input():
    s = summarize([])
    assert s["counts"] == Counter() and s["first_fail"] is None and s["tests"] == {}


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("PASS", name)
