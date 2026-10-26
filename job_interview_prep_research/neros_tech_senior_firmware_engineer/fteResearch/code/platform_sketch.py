#!/usr/bin/env python3
"""platform_sketch.py — R09 스케일러블 테스트 플랫폼 뼈대 (표준 라이브러리만, 전부 fake).

R09 단원(chapters/2026-10-24_R09_scalable_reusable_test_platform.md)에서 쓰는 예제.
보여 주는 것:
  1. capability 기반 DUT 모델 (상속 대신 조합): DUT 는 RcInput / Telemetry / Power / Flash
     어댑터를 "가지고" 있다. RcInput 은 CRSF 또는 MAVLink, Telemetry 는 MSP 또는 MAVLink.
  2. 제품 descriptor = 데이터 (dict, YAML 로 바꿔도 같은 모양). 코드 안 고치고 기체 추가.
  3. 테스트 케이스가 필요한 capability 를 선언 → selector 가 DUT 별 실행/스킵 결정.
  4. scheduler: 빈 벤치에 테스트 배정 (벤치 lock, 교정 만료/고장 벤치 제외).
  5. 구조화된 결과 (JSON lines): fw git sha, bench id, dut serial, 계측기 교정 정보,
     측정값 + limit.
  6. 반복 실행 결과로 flaky 판정 + 벤치 상관 분석.

실행:  python3 platform_sketch.py          -> 데모 + 자체 테스트, 마지막 줄 "SELF-TEST PASS"
       python3 platform_sketch.py --jsonl  -> 결과 JSON lines 만 출력
Python 3.9+.
"""
from __future__ import annotations

import json
import random
import sys
from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional, Tuple

# ---------------------------------------------------------------------------
# 0. Fake 기체 (펌웨어 동작을 흉내 내는 아주 작은 상태 기계)
# ---------------------------------------------------------------------------


class FakeAirframe:
    """FC 펌웨어 흉내. 실제로는 보드 + 펌웨어가 이 자리에 있다.

    rc_link_glitch: 벤치 배선 불량을 흉내 냄 (확률적으로 RC 프레임 유실 → flaky 원인).
    """

    def __init__(self, failsafe_delay_ms: float, rng: random.Random, rc_link_glitch: float = 0.0):
        self.failsafe_delay_ms = failsafe_delay_ms
        self.rng = rng
        self.rc_link_glitch = rc_link_glitch
        self.powered = False
        self.armed = False
        self.rc_present = False
        self.failsafe = False
        self.vbat = 0.0
        self.fw_sha = "none"
        self.good_frames = 0
        self.low_bat_warn_v = 3.5 * 4  # 4S 기준 경고 전압 (fake)

    # --- 펌웨어 쪽 동작 ---
    def rc_frame(self, throttle: float, arm_switch: bool) -> None:
        if not self.powered:
            return
        if self.rng.random() < self.rc_link_glitch:
            self.good_frames = 0  # 벤치 배선 문제로 프레임이 사라짐 → 연속 수신 카운터 리셋
            return
        self.good_frames += 1
        self.rc_present = True
        self.failsafe = False
        # arm 조건: 연속으로 5 프레임 이상 정상 수신 + 스로틀 낮음 (fake 규칙)
        if arm_switch and throttle < 0.05 and self.good_frames >= 5:
            self.armed = True
        if not arm_switch:
            self.armed = False

    def rc_stop(self) -> float:
        """RC 끊김 → failsafe 까지 걸린 시간(ms) 반환 (펌웨어가 '측정'하는 값을 흉내)."""
        self.rc_present = False
        jitter = self.rng.gauss(0, 8.0)
        self.failsafe = True
        self.armed = False
        return max(0.0, self.failsafe_delay_ms + jitter)

    def low_battery_warning(self) -> bool:
        return self.powered and self.vbat < self.low_bat_warn_v


# ---------------------------------------------------------------------------
# 1. Capability 인터페이스 (WHAT 과 HOW 사이의 계약)
# ---------------------------------------------------------------------------


class RcInput:
    """조종 입력 주입 capability. 구현: CRSF / MAVLink(RC override)."""

    protocol = "abstract"

    def __init__(self, af: FakeAirframe):
        self.af = af

    def send(self, throttle: float, arm_switch: bool) -> None:
        raise NotImplementedError

    def stop(self) -> float:
        raise NotImplementedError


class CrsfRcInput(RcInput):
    protocol = "crsf"

    def send(self, throttle: float, arm_switch: bool) -> None:
        # 실제: 0xC8 sync + RC_CHANNELS_PACKED(0x16) 프레임을 USB-UART 로 송신
        self.af.rc_frame(throttle, arm_switch)

    def stop(self) -> float:
        return self.af.rc_stop()


class MavlinkRcInput(RcInput):
    protocol = "mavlink"

    def send(self, throttle: float, arm_switch: bool) -> None:
        # 실제: RC_CHANNELS_OVERRIDE 또는 MANUAL_CONTROL 메시지 송신
        self.af.rc_frame(throttle, arm_switch)

    def stop(self) -> float:
        return self.af.rc_stop()


class Telemetry:
    """상태 읽기 capability. 구현: MSP / MAVLink."""

    protocol = "abstract"

    def __init__(self, af: FakeAirframe):
        self.af = af

    def status(self) -> Dict[str, object]:
        # 프로토콜마다 실제 디코딩은 다르지만 테스트에는 같은 dict 로 정규화해서 준다
        return {"armed": self.af.armed, "failsafe": self.af.failsafe,
                "vbat": round(self.af.vbat, 2), "low_bat": self.af.low_battery_warning(),
                "fw_sha": self.af.fw_sha}


class MspTelemetry(Telemetry):
    protocol = "msp"  # 실제: MSP_STATUS / MSP_ANALOG / MSP_FC_VERSION ...


class MavlinkTelemetry(Telemetry):
    protocol = "mavlink"  # 실제: HEARTBEAT / SYS_STATUS / AUTOPILOT_VERSION ...


class Power:
    """프로그래머블 PSU capability (실제: SCPI over LAN/USB)."""

    def __init__(self, af: FakeAirframe, cal_due: str):
        self.af = af
        self.cal_due = cal_due  # 계측기 교정 만료일 → 결과에 같이 기록

    def on(self, volts: float) -> None:
        self.af.powered = True
        self.af.vbat = volts

    def set_voltage(self, volts: float) -> None:
        self.af.vbat = volts

    def off(self) -> None:  # idempotent: 몇 번 불러도 안전
        self.af.powered = False
        self.af.good_frames = 0
        self.af.armed = False
        self.af.vbat = 0.0


class Flash:
    """펌웨어 굽기 capability (실제: DFU / SWD / bootloader)."""

    def __init__(self, af: FakeAirframe):
        self.af = af

    def program(self, fw_sha: str) -> None:
        self.af.fw_sha = fw_sha


class Companion:
    """Companion 컴퓨터 링크 capability (실제: Jetson ↔ FC MAVLink, ssh)."""

    def __init__(self, af: FakeAirframe):
        self.af = af

    def heartbeat_ok(self) -> bool:
        return self.af.powered


# 어댑터 레지스트리: descriptor 의 문자열 → 구현 클래스. 새 프로토콜 = 여기 한 줄.
ADAPTERS: Dict[Tuple[str, str], Callable[..., object]] = {
    ("rc_input", "crsf"): CrsfRcInput,
    ("rc_input", "mavlink"): MavlinkRcInput,
    ("telemetry", "msp"): MspTelemetry,
    ("telemetry", "mavlink"): MavlinkTelemetry,
    ("power", "scpi_psu"): Power,
    ("flash", "dfu"): Flash,
    ("flash", "swd"): Flash,
    ("companion", "jetson_mavlink"): Companion,
}


# ---------------------------------------------------------------------------
# 2. 제품 descriptor = 데이터 (Neros 실제 제품 구성은 모름 → 전부 가상 예시 [추정])
# ---------------------------------------------------------------------------

PRODUCTS: Dict[str, Dict[str, object]] = {
    "fpv_bf": {
        "stack": "betaflight-fork",
        "hw_rev": "FC-H7-revB",
        "capabilities": {"rc_input": "crsf", "telemetry": "msp", "power": "scpi_psu", "flash": "dfu"},
        "limits": {"failsafe_latency_ms": (0.0, 150.0), "low_bat_warn": (1, 1)},
        "fake": {"failsafe_delay_ms": 100.0},
    },
    "auto_px4": {
        "stack": "px4",
        "hw_rev": "FC-H7-revC+Jetson",
        "capabilities": {"rc_input": "mavlink", "telemetry": "mavlink", "power": "scpi_psu",
                         "flash": "swd", "companion": "jetson_mavlink"},
        "limits": {"failsafe_latency_ms": (0.0, 600.0), "low_bat_warn": (1, 1)},
        "fake": {"failsafe_delay_ms": 500.0},
    },
    # 전원 계측기 없는 책상 셋업: power 가 없으니 전압 테스트는 자동 스킵된다
    "fpv_bf_desk": {
        "stack": "betaflight-fork",
        "hw_rev": "FC-H7-revB",
        "capabilities": {"rc_input": "crsf", "telemetry": "msp", "flash": "dfu"},
        "limits": {"failsafe_latency_ms": (0.0, 150.0)},
        "fake": {"failsafe_delay_ms": 100.0},
    },
}


@dataclass
class Dut:
    """DUT = capability 들의 묶음 (조합). 상속 계층 없음."""

    serial: str
    product: str
    hw_rev: str
    stack: str
    caps: Dict[str, object]
    limits: Dict[str, Tuple[float, float]]
    airframe: FakeAirframe

    def has(self, *names: str) -> bool:
        return all(n in self.caps for n in names)

    def cap(self, name: str):
        if name not in self.caps:
            raise LookupError(f"{self.product} has no capability '{name}'")
        return self.caps[name]


def build_dut(product: str, serial: str, rng: random.Random, cal_due: str = "2027-01-01",
              rc_link_glitch: float = 0.0) -> Dut:
    d = PRODUCTS[product]
    fake = d["fake"]  # type: ignore[index]
    af = FakeAirframe(fake["failsafe_delay_ms"], rng, rc_link_glitch)  # type: ignore[index]
    caps: Dict[str, object] = {}
    for cap_name, impl in d["capabilities"].items():  # type: ignore[union-attr]
        cls = ADAPTERS[(cap_name, impl)]
        caps[cap_name] = cls(af, cal_due) if cap_name == "power" else cls(af)
    return Dut(serial, product, d["hw_rev"], d["stack"], caps, dict(d["limits"]), af)  # type: ignore[arg-type]


# ---------------------------------------------------------------------------
# 3. 테스트 케이스: 필요한 capability 선언 + 측정만 하고 판정은 limit 데이터로
# ---------------------------------------------------------------------------


@dataclass
class TestCase:
    name: str
    requires: Tuple[str, ...]
    fn: Callable[["Ctx"], None]
    tags: Tuple[str, ...] = ()
    duration_s: int = 30  # 스케줄링용 예상 시간


REGISTRY: List[TestCase] = []


def test_case(*requires: str, tags: Tuple[str, ...] = (), duration_s: int = 30):
    def deco(fn: Callable[["Ctx"], None]):
        REGISTRY.append(TestCase(fn.__name__, tuple(requires), fn, tags, duration_s))
        return fn
    return deco


@dataclass
class Measurement:
    name: str
    value: float
    low: Optional[float]
    high: Optional[float]
    unit: str = ""

    @property
    def passed(self) -> bool:
        if self.low is not None and self.value < self.low:
            return False
        if self.high is not None and self.value > self.high:
            return False
        return True


class Ctx:
    """테스트에 넘기는 컨텍스트. 테스트는 dut 와 measure() 만 안다 (벤치/계측기 모름)."""

    def __init__(self, dut: Dut):
        self.dut = dut
        self.measurements: List[Measurement] = []

    def measure(self, name: str, value: float, unit: str = "") -> None:
        low, high = self.dut.limits.get(name, (None, None))
        self.measurements.append(Measurement(name, float(value), low, high, unit))

    def check(self, name: str, cond: bool) -> None:
        self.measure(name, 1.0 if cond else 0.0)
        if name not in self.dut.limits:  # limit 없는 boolean 체크는 1 이어야 통과
            self.measurements[-1].low = 1.0


@test_case("rc_input", "telemetry", tags=("smoke",), duration_s=20)
def test_arm_disarm(ctx: Ctx) -> None:
    rc, tm = ctx.dut.cap("rc_input"), ctx.dut.cap("telemetry")
    for _ in range(5):
        rc.send(throttle=0.0, arm_switch=True)
    ctx.check("armed_after_arm", bool(tm.status()["armed"]))
    for _ in range(5):
        rc.send(throttle=0.0, arm_switch=False)
    ctx.check("disarmed_after_disarm", not tm.status()["armed"])


@test_case("rc_input", "telemetry", tags=("smoke", "failsafe"), duration_s=40)
def test_rc_loss_failsafe(ctx: Ctx) -> None:
    rc, tm = ctx.dut.cap("rc_input"), ctx.dut.cap("telemetry")
    for _ in range(5):
        rc.send(throttle=0.0, arm_switch=True)
    ctx.check("armed_before_loss", bool(tm.status()["armed"]))
    latency = rc.stop()  # 펌웨어/계측기가 잰 값을 받아 온다 (FW measures, host judges)
    ctx.measure("failsafe_latency_ms", latency, "ms")
    ctx.check("disarmed_on_failsafe", not tm.status()["armed"])


@test_case("power", "telemetry", tags=("power",), duration_s=60)
def test_low_voltage_warning(ctx: Ctx) -> None:
    pw, tm = ctx.dut.cap("power"), ctx.dut.cap("telemetry")
    pw.set_voltage(16.8)
    ctx.check("no_warn_at_full", not tm.status()["low_bat"])
    pw.set_voltage(13.2)
    ctx.measure("low_bat_warn", 1.0 if tm.status()["low_bat"] else 0.0)


@test_case("companion", tags=("autonomy",), duration_s=30)
def test_companion_heartbeat(ctx: Ctx) -> None:
    ctx.check("companion_heartbeat", ctx.dut.cap("companion").heartbeat_ok())


def select(tests: List[TestCase], dut: Dut, tag: Optional[str] = None):
    """DUT capability 와 tag 로 실행할 테스트를 고른다. (run, skipped[(name, reason)])"""
    run, skipped = [], []
    for t in tests:
        if tag and tag not in t.tags:
            continue
        missing = [c for c in t.requires if not dut.has(c)]
        if missing:
            skipped.append((t.name, "missing capability: " + ",".join(missing)))
        else:
            run.append(t)
    return run, skipped


# ---------------------------------------------------------------------------
# 4. 벤치 + 스케줄러 (WHERE)
# ---------------------------------------------------------------------------


class BenchBusy(Exception):
    pass


@dataclass
class Bench:
    bench_id: str
    dut: Dut
    pool: str  # 예: "hil-fpv", "hil-auto"
    cal_due: str
    healthy: bool = True
    holder: Optional[str] = None
    busy_until: int = 0

    def acquire(self, who: str) -> None:
        if self.holder is not None:
            raise BenchBusy(f"{self.bench_id} held by {self.holder}")
        self.holder = who

    def release(self, who: str) -> None:
        if self.holder == who:  # 남의 lock 은 못 풀고, 두 번 풀어도 안전
            self.holder = None

    def usable(self, today: str) -> bool:
        return self.healthy and self.cal_due >= today


def setup(dut: Dut, fw_sha: str) -> None:
    """idempotent setup: 이전 테스트가 어떤 상태로 끝났든 같은 출발점으로."""
    if dut.has("power"):
        dut.cap("power").off()
        dut.cap("power").on(16.8)
    else:
        dut.airframe.powered = True  # 책상 셋업: USB 전원이라 가정
    dut.cap("flash").program(fw_sha)


def teardown(dut: Dut) -> None:
    if dut.has("power"):
        dut.cap("power").off()
    dut.airframe.armed = False


def run_one(t: TestCase, bench: Bench, fw_sha: str, run_id: str, start: int) -> Dict[str, object]:
    ctx = Ctx(bench.dut)
    status, error = "pass", None
    setup(bench.dut, fw_sha)
    try:
        t.fn(ctx)
        if not all(m.passed for m in ctx.measurements):
            status = "fail"
    except Exception as e:  # 테스트 코드 에러는 'error' 로 구분 (fail 과 다르다)
        status, error = "error", f"{type(e).__name__}: {e}"
    finally:
        teardown(bench.dut)
    return {
        "run_id": run_id, "test": t.name, "status": status, "error": error,
        "fw_sha": fw_sha, "product": bench.dut.product, "stack": bench.dut.stack,
        "hw_rev": bench.dut.hw_rev, "dut_serial": bench.dut.serial, "bench_id": bench.bench_id,
        "instrument_cal_due": bench.cal_due, "t_start": start, "duration_s": t.duration_s,
        "measurements": [{"name": m.name, "value": round(m.value, 3), "low": m.low, "high": m.high,
                          "unit": m.unit, "passed": m.passed} for m in ctx.measurements],
    }


def schedule(jobs: List[Tuple[TestCase, str]], benches: List[Bench], fw_sha: str, run_id: str,
             today: str) -> Tuple[List[Dict[str, object]], List[str]]:
    """간단한 이산 시간 스케줄러. job=(test, product). 가장 빨리 비는 맞는 벤치에 배정."""
    results: List[Dict[str, object]] = []
    unschedulable: List[str] = []
    for t, product in jobs:
        candidates = [b for b in benches if b.dut.product == product and b.usable(today)
                      and select([t], b.dut)[0]]
        if not candidates:
            unschedulable.append(f"{t.name}@{product}")
            continue
        b = min(candidates, key=lambda x: (x.busy_until, x.bench_id))
        who = f"{run_id}:{t.name}"
        b.acquire(who)
        try:
            results.append(run_one(t, b, fw_sha, run_id, b.busy_until))
            b.busy_until += t.duration_s
        finally:
            b.release(who)
    return results, unschedulable


# ---------------------------------------------------------------------------
# 5. Flaky 판정 (같은 fw_sha 에서 pass/fail 이 섞이면 flaky)
# ---------------------------------------------------------------------------


def flaky_report(records: List[Dict[str, object]], quarantine_rate: float = 0.02):
    by_key: Dict[Tuple[str, str, str], List[Dict[str, object]]] = {}
    for r in records:
        by_key.setdefault((str(r["test"]), str(r["product"]), str(r["fw_sha"])), []).append(r)
    out = {}
    for key, rs in sorted(by_key.items()):
        n = len(rs)
        fails = [r for r in rs if r["status"] != "pass"]
        rate = len(fails) / n
        if not fails:
            verdict = "stable_pass"
        elif len(fails) == n:
            verdict = "stable_fail"  # 진짜 회귀일 가능성 높음 → 사람에게 바로
        else:
            verdict = "flaky"
        bench_fail: Dict[str, List[int]] = {}
        for r in rs:
            s = bench_fail.setdefault(str(r["bench_id"]), [0, 0])
            s[0] += 1
            s[1] += r["status"] != "pass"
        suspect = None
        if verdict == "flaky":
            worst = max(bench_fail.items(), key=lambda kv: kv[1][1] / kv[1][0])
            others = [v for k, v in bench_fail.items() if k != worst[0]]
            other_fails = sum(v[1] for v in others)
            if worst[1][1] >= 2 and other_fails == 0:
                suspect = worst[0]  # 실패가 한 벤치에만 몰림 → 펌웨어보다 벤치 의심
        out[key] = {"runs": n, "fail_rate": round(rate, 3), "verdict": verdict,
                    "quarantine": verdict == "flaky" and rate > quarantine_rate,
                    "suspect_bench": suspect,
                    "per_bench": {k: f"{v[1]}/{v[0]}" for k, v in sorted(bench_fail.items())}}
    return out


# ---------------------------------------------------------------------------
# 6. 데모 + 자체 테스트
# ---------------------------------------------------------------------------


def make_fleet(seed: int = 7) -> List[Bench]:
    rng = random.Random(seed)
    return [
        Bench("B1", build_dut("fpv_bf", "FPV-0001", rng), "hil-fpv", "2027-03-01"),
        Bench("B2", build_dut("fpv_bf", "FPV-0002", rng), "hil-fpv", "2027-03-01"),
        # B3: RC 배선 불량 흉내 (프레임 8% 유실 → arm 실패 약 1/3) → flaky 의 원인이 벤치임을 탐지해야 함
        Bench("B3", build_dut("fpv_bf", "FPV-0003", rng, rc_link_glitch=0.08), "hil-fpv", "2027-03-01"),
        Bench("B4", build_dut("auto_px4", "AUT-0001", rng), "hil-auto", "2027-03-01"),
        # B5: 교정 만료 → 스케줄러가 쓰지 말아야 함
        Bench("B5", build_dut("auto_px4", "AUT-0002", rng), "hil-auto", "2026-01-01"),
        Bench("D1", build_dut("fpv_bf_desk", "FPV-DESK", rng), "desk", "2027-03-01"),
    ]


def self_test() -> None:
    rng = random.Random(1)
    # (a) selector: desk 셋업엔 power 없음 → low voltage 테스트 스킵, companion 도 스킵
    desk = build_dut("fpv_bf_desk", "X", rng)
    run, skipped = select(REGISTRY, desk)
    assert {t.name for t in run} == {"test_arm_disarm", "test_rc_loss_failsafe"}, run
    assert dict(skipped)["test_low_voltage_warning"].startswith("missing capability: power")
    px4 = build_dut("auto_px4", "Y", rng)
    assert {t.name for t in select(REGISTRY, px4)[0]} == {t.name for t in REGISTRY}
    assert isinstance(px4.cap("rc_input"), MavlinkRcInput)
    assert isinstance(desk.cap("rc_input"), CrsfRcInput)
    assert [t.name for t in select(REGISTRY, px4, tag="smoke")[0]] == ["test_arm_disarm", "test_rc_loss_failsafe"]

    # (b) 같은 테스트 코드, 다른 limit: PX4 failsafe 500ms 는 PX4 limit(600) 에선 pass
    b = Bench("T", px4, "hil-auto", "2027-01-01")
    r = run_one(next(t for t in REGISTRY if t.name == "test_rc_loss_failsafe"), b, "abc1234", "r0", 0)
    m = {x["name"]: x for x in r["measurements"]}
    assert m["failsafe_latency_ms"]["high"] == 600.0 and r["status"] == "pass", r
    assert b.dut.airframe.powered is False, "teardown must power off"

    # (c) lock: 두 번 acquire 는 BenchBusy, release 는 idempotent
    b.acquire("a")
    try:
        b.acquire("b")
        raise AssertionError("expected BenchBusy")
    except BenchBusy:
        pass
    b.release("b")  # 남의 lock: 무시
    assert b.holder == "a"
    b.release("a")
    b.release("a")
    assert b.holder is None

    # (d) 스케줄러 + flaky: 같은 fw 로 20회 nightly 반복
    benches = make_fleet()
    records: List[Dict[str, object]] = []
    unsched_all: List[str] = []
    for i in range(20):
        jobs = [(t, p) for p in ("fpv_bf", "auto_px4") for t in REGISTRY]
        recs, unsched = schedule(jobs, benches, "deadbee", f"nightly-{i}", today="2026-10-24")
        records += recs
        unsched_all += unsched
    used = {r["bench_id"] for r in records}
    assert "B5" not in used, "expired-cal bench must not be used"
    assert "D1" not in used, "desk bench is a different product"
    assert "test_companion_heartbeat@fpv_bf" in unsched_all
    for r in records:  # 결과 레코드 traceability 필드
        for k in ("fw_sha", "bench_id", "dut_serial", "hw_rev", "instrument_cal_due"):
            assert r[k], (k, r)
    json.loads(json.dumps(records[0]))  # JSON 직렬화 가능

    rep = flaky_report(records)
    arm = rep[("test_arm_disarm", "fpv_bf", "deadbee")]
    assert arm["verdict"] == "flaky" and arm["suspect_bench"] == "B3" and arm["quarantine"], arm
    px = rep[("test_rc_loss_failsafe", "auto_px4", "deadbee")]
    assert px["verdict"] == "stable_pass", px

    # (e) 진짜 회귀: failsafe 가 느려진 펌웨어 → 모든 벤치에서 실패 → stable_fail
    for bch in benches:
        if bch.dut.product == "fpv_bf":
            bch.dut.airframe.failsafe_delay_ms = 400.0
            bch.dut.airframe.rc_link_glitch = 0.0
    recs, _ = schedule([(t, "fpv_bf") for t in REGISTRY if t.name == "test_rc_loss_failsafe"] * 6,
                       benches, "bad0001", "regress", today="2026-10-24")
    rep2 = flaky_report(recs)
    assert rep2[("test_rc_loss_failsafe", "fpv_bf", "bad0001")]["verdict"] == "stable_fail"


def demo() -> List[Dict[str, object]]:
    benches = make_fleet()
    records: List[Dict[str, object]] = []
    for i in range(10):
        jobs = [(t, p) for p in ("fpv_bf", "auto_px4") for t in REGISTRY]
        recs, unsched = schedule(jobs, benches, "deadbee", f"nightly-{i}", today="2026-10-24")
        records += recs
    return records


if __name__ == "__main__":
    if "--jsonl" in sys.argv:
        for rec in demo():
            print(json.dumps(rec, ensure_ascii=False))
        sys.exit(0)
    recs = demo()
    print("== sample result record (JSON line) ==")
    print(json.dumps(recs[1], ensure_ascii=False))
    print("== selector on desk setup ==")
    run, skipped = select(REGISTRY, build_dut("fpv_bf_desk", "X", random.Random(0)))
    print(" run    :", [t.name for t in run])
    print(" skipped:", skipped)
    print("== flaky report (10 nightly runs, fw deadbee) ==")
    for k, v in flaky_report(recs).items():
        print(" ", "/".join(k), v)
    self_test()
    print("SELF-TEST PASS")
