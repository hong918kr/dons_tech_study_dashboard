"""모의 B · 텔레메트리 로그 검사기 — 파싱 · 시퀀스 누락 · 멈춤(stall) · 최저 전압.

  .venv/bin/python lecture/mock/telemetry_check.py      # 테스트 실행 → PASS (pytest 필요)
"""
# --- solution ---
from dataclasses import dataclass, field


@dataclass
class Report:
    frames: int = 0
    malformed: int = 0
    missing: list = field(default_factory=list)     # 빠진 seq 번호들
    max_gap_ms: int = 0                              # 연속 프레임 사이 최대 간격
    min_vbat: float = float("inf")


def parse_line(line):
    """'t=1000 seq=1 vbat=16.4' → {'t': 1000, 'seq': 1, 'vbat': 16.4}. 형식이 틀리면 None."""
    try:
        kv = dict(part.split("=", 1) for part in line.split())
        return {"t": int(kv["t"]), "seq": int(kv["seq"]), "vbat": float(kv["vbat"])}
    except (ValueError, KeyError):
        return None


def check(lines):
    r = Report()
    prev = None
    for line in lines:
        f = parse_line(line)
        if f is None:
            r.malformed += 1
            continue
        r.frames += 1
        r.min_vbat = min(r.min_vbat, f["vbat"])
        if prev is not None:
            r.missing += range(prev["seq"] + 1, f["seq"])      # 1, 2, 5 → 3, 4
            r.max_gap_ms = max(r.max_gap_ms, f["t"] - prev["t"])
        prev = f
    return r
# --- end ---


# --- tests ---
import pytest

LOG = """t=1000 seq=1 vbat=16.4
t=1020 seq=2 vbat=16.3
garbage line
t=1100 seq=5 vbat=15.9
t=1120 seq=6 vbat=16.0""".splitlines()


def test_summary():
    r = check(LOG)
    assert (r.frames, r.malformed, r.missing) == (4, 1, [3, 4])
    assert r.max_gap_ms == 80 and r.min_vbat == pytest.approx(15.9)


@pytest.mark.parametrize("line", ["", "t=1 seq=2", "t=x seq=1 vbat=1", "t=1 seq=1 vbat"])
def test_malformed_lines_are_counted_not_fatal(line):
    assert parse_line(line) is None
    assert check([line]).malformed == 1


def test_empty_log():
    r = check([])
    assert r.frames == 0 and r.missing == [] and r.max_gap_ms == 0
# --- end ---


if __name__ == "__main__":
    test_summary()
    for bad in ["", "t=1 seq=2", "t=x seq=1 vbat=1", "t=1 seq=1 vbat"]:
        test_malformed_lines_are_counted_not_fatal(bad)
    test_empty_log()
    print("PASS")
