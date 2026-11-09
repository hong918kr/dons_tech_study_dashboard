#!/usr/bin/env python3
"""SIV-A09 검증 캠페인 설계: 신규 센싱 SoC — 계획 lint · 일정 계산 · Day-0 게이팅 스크립트 골격

  Part A  검증 계획(단계·입출구 기준·소유자·IP 커버리지)을 데이터로 두고 lint + 게이트 판정
  Part B  일정(critical path) · 벤치 용량 · triage 적체 · 슬립 시 무엇을 먼저 자를까 (전수 탐색)
  Part C  Day-0 스크립트: power → clock → JTAG → boot ROM → SRAM → DDR → PCIe → CSI-2/SerDes
          의존성 게이팅, FAIL/ERROR/BLOCKED 구분, 안전 중단, triage packet, 보드 8장 캠페인

  python3 -I solution.py         # 테스트 ("N/M passed", 실패 시 exit 1)
  python3 -I solution.py --demo  # 캠페인 리포트 + 게이팅 한계 표 출력

  Python 3.9 표준 라이브러리만. 칩은 전부 mock 이고 수치는 대표값/가정이다 (md 참고).
  이 코드는 게이팅과 보고만 맡는다: 실제 측정 내용은 SIV-M04/M05/M06/M12 가 소유한다.
"""
import collections
import dataclasses
import itertools
import json
import sys
import zlib

# ── Part A: 검증 계획 ─────────────────────────────────────────────
PHASES = ("pre_si", "day0", "feature", "pvt", "stress", "handoff")
IPS = ("POWER_CLOCK", "BOOT", "DDR", "PCIE", "CSI2", "SERDES")
OPS = {">=": lambda a, b: a >= b, "<=": lambda a, b: a <= b}


def crit(metric, op, value, waivable=False):
    return {"metric": metric, "op": op, "value": value, "waivable": waivable}


def phase(name, owner, after, exits, covers=IPS):
    return {"phase": name, "owner": owner, "after": after, "exit": exits, "covers": tuple(covers)}


def good_plan():
    """md §5 Part A 의 표와 같은 계획 (수치는 대표값)."""
    return [
        phase("pre_si", "validation-lead", None, [
            crit("day0_scripts_on_emulation_pct", ">=", 100),
            crit("debug_access_open_items", "<=", 0),
            crit("board_review_open_items", "<=", 0),
            crit("benches_commissioned", ">=", 6, waivable=True)]),
        phase("day0", "bringup-lead", "pre_si", [
            crit("boards_passing_day0", ">=", 3),
            crit("open_blocker_bugs", "<=", 0),
            crit("os_boot_success_pct", ">=", 95, waivable=True)]),
        phase("feature", "ip-validation-leads", "day0", [
            crit("test_plan_executed_pct", ">=", 95),
            crit("test_plan_blocked_pct", "<=", 5),
            crit("test_plan_pass_pct", ">=", 90, waivable=True),
            crit("open_s1_bugs", "<=", 0)]),
        phase("pvt", "characterization-lead", "feature", [
            crit("corners_completed_pct", ">=", 100),
            crit("samples_per_corner", ">=", 5, waivable=True),
            crit("vmin_guardband_mv", ">=", 30),
            crit("ddr_min_eye_ui_pct", ">=", 30, waivable=True)]),
        phase("stress", "system-validation-lead", "pvt", [
            crit("soak_hours_no_uncorrectable", ">=", 168),
            crit("serdes_ber_upper95_log10", "<=", -12),
            crit("unexplained_failures", "<=", 0)]),
        phase("handoff", "product-test-lead", "stress", [
            crit("errata_documented_pct", ">=", 100),
            crit("ate_slt_correlation_pct", ">=", 95, waivable=True),
            crit("open_s1_bugs", "<=", 0)]),
    ]


def lint_plan(plan):
    """계획의 구조 결함 → [(code, phase, detail)] (문제 없으면 [])."""
    # TODO: E_ORDER: phase 이름 나열이 PHASES 와 다르면 ("E_ORDER", "*", 실제 나열)
    #       각 phase: E_NO_OWNER(owner 비었음) · E_ENTRY_UNLINKED(after 가 직전 phase 이름이 아님, 첫 phase 는 None)
    #       E_VAGUE_EXIT(exit 비었음 또는 metric/op/숫자 value 를 못 갖춘 항목. bool 은 숫자로 치지 않는다)
    #       E_UNKNOWN_IP(covers 에 IPS 밖의 이름) · E_COVERAGE_HOLE(IPS 중 covers 에 없는 IP, detail=IP 이름)
    raise NotImplementedError("TODO lint_plan")


def gate_decision(ph, metrics, waivers=()):
    """단계 출구 판정 → {"decision": GO|GO_WITH_WAIVER|NO_GO, "failed": [], "waived": [], "missing": []}."""
    # TODO: exit 항목마다: metric 이 metrics 에 없으면 missing (데이터 없음 ≠ 통과).
    #       OPS[op](측정값, value) 가 거짓이면: waivable 이고 metric 이 waivers 에 있으면 waived, 아니면 failed.
    #       failed 또는 missing 이 있으면 NO_GO, waived 만 있으면 GO_WITH_WAIVER, 아니면 GO.
    raise NotImplementedError("TODO gate_decision")


# ── Part B: 일정 · 용량 · 적체 · 자르기 ───────────────────────────
# 주 단위, 모두 가정. T0(첫 실리콘 도착)는 silicon_fab 끝나는 시점.
SCHEDULE = {
    "silicon_fab": (20, ()),
    "board_design_fab": (16, ()),
    "day0_scripts_on_emu": (14, ()),
    "lab_automation": (18, ()),
    "day0_bringup": (2, ("silicon_fab", "board_design_fab", "day0_scripts_on_emu")),
    "ddr_feature": (6, ("day0_bringup",)),
    "pcie_feature": (5, ("day0_bringup",)),
    "csi2_feature": (5, ("day0_bringup",)),
    "serdes_feature": (6, ("day0_bringup",)),
    "pvt_char": (8, ("ddr_feature", "pcie_feature", "csi2_feature", "serdes_feature", "lab_automation")),
    "stress_soak": (7, ("ddr_feature", "pcie_feature", "csi2_feature", "serdes_feature", "lab_automation")),
    "a1_metal_spin": (12, ("ddr_feature", "pcie_feature", "csi2_feature", "serdes_feature")),
    "a1_regression": (4, ("a1_metal_spin",)),
    "production_handoff": (3, ("pvt_char", "stress_soak", "a1_regression")),
}

# PVT 수요: IP당 코너 1개·샘플 1개에 드는 bench-hour (소크 포함, 대표값), 9 코너, 샘플 5개
PVT_DEMAND = [{"name": n, "hours": h, "corners": 9, "samples": 5}
              for n, h in (("ddr", 16), ("pcie", 6), ("csi2", 8), ("serdes", 10), ("power_thermal", 4))]

# (이름, 절약 주, 잃는 위험 점수 1~10, must_keep) — 위험 점수는 팀이 매기는 대표값
CUT_ITEMS = [
    ("dashboard_polish", 1, 1, False),
    ("pcie_low_rate_char", 1, 2, False),
    ("serdes_cable_matrix_trim", 2, 3, False),
    ("pvt_corners_9_to_5", 3, 7, False),
    ("csi2_lane_rate_matrix_trim", 2, 5, False),
    ("stress_168h_to_72h", 1, 6, False),
    ("power_sequencing_and_thermal_trip", 2, 10, True),
    ("first_failure_safety_and_abort_path", 1, 10, True),
]


def critical_path(tasks):
    """tasks = {이름: (기간, (선행…))} → (총 기간, 임계 경로 [이름…]). 동률은 이름 사전순으로 고른다."""
    # TODO: 선행 이름이 tasks 에 없으면 ValueError("E_UNKNOWN_DEP: ...").
    #       위상 정렬(ready 는 sorted) 하며 finish[t] = 기간 + max(선행 finish). 정렬이 다 못 끝나면 ValueError("E_CYCLE: ...").
    #       각 task 의 pred = finish 최대인 선행 (동률은 사전순 작은 것).
    #       끝점 = finish 최대인 task (동률 사전순), pred 를 거슬러 경로를 만든다.
    raise NotImplementedError("TODO critical_path")


def capacity_weeks(demands, benches, hours_per_day, days_per_week, uptime):
    """필요 bench-hour / 주당 가용 bench-hour. demands = [{"hours": h, "corners": c, "samples": s}, ...]."""
    # TODO: 수요 = Σ hours*corners*samples. 공급 = benches*hours_per_day*days_per_week*uptime. 수요/공급 반환.
    raise NotImplementedError("TODO capacity_weeks")


def triage_backlog(fails_per_day, dedup, engineers, dispositions_per_engineer_day, days):
    """날짜별 분류 대기 고유 실패 수 [b1..bn]. dedup = 이미 아는 시그니처 비율."""
    # TODO: 하루 유입 = fails_per_day*(1-dedup), 하루 처리 = engineers*dispositions_per_engineer_day.
    #       b[d] = max(0, b[d-1] + 유입 - 처리), b[0]=0 에서 시작해 days 개를 돌려준다.
    raise NotImplementedError("TODO triage_backlog")


def greedy_cut(items, need):
    """(주어진 순진한 방법) 주당 위험이 낮은 것부터 need 주를 채울 때까지 자른다."""
    pool = sorted((it for it in items if not it[3]), key=lambda it: (it[2] / it[1], it[0]))
    names, weeks, risk = [], 0, 0
    for it in pool:
        if weeks >= need:
            break
        names.append(it[0])
        weeks += it[1]
        risk += it[2]
    return {"names": sorted(names), "weeks": weeks, "risk": risk} if weeks >= need else None


def best_cut(items, need):
    """must_keep 이 아닌 항목 중 절약 합 ≥ need 이고 위험 합이 최소인 부분집합. 불가능하면 None."""
    # TODO: 전수 탐색 (항목 수 ≤ 20 가정). 목표 키 = (위험 합, 항목 수, 정렬된 이름들) 최소.
    #       반환 {"names": 정렬된 이름들, "weeks": 절약 합, "risk": 위험 합}, 조건 만족 조합이 없으면 None.
    raise NotImplementedError("TODO best_cut")


# ── Part C: Day-0 게이팅 ──────────────────────────────────────────
SATISFIED = ("PASS", "FLAKY", "DEGRADED")   # 의존 스테이지가 이 상태면 다음으로 간다
EXIT = {"PASS": 0, "FAIL": 1, "ABORTED": 1, "CONFIG": 2, "INCONCLUSIVE": 3, "DEGRADED": 4}


class ConfigError(Exception):
    def __init__(self, code, detail):
        Exception.__init__(self, "%s: %s" % (code, detail))
        self.code = code


class InstrumentError(Exception):
    """계측기·프로브 문제: 칩에 대해 아무것도 말해주지 않는다."""
    def __init__(self, code, msg):
        Exception.__init__(self, msg)
        self.code = code


class SafetyAbort(Exception):
    """계속하면 칩이나 보드를 태울 수 있다: 전체 중단."""
    def __init__(self, code, msg, evidence):
        Exception.__init__(self, msg)
        self.code, self.evidence = code, evidence


Outcome = collections.namedtuple("Outcome", "status code msg evidence")


def ok(ev=None):
    return Outcome("PASS", "", "", ev or {})


def bad(code, msg, ev=None):
    return Outcome("FAIL", code, msg, ev or {})


def warn(code, msg, ev=None):
    return Outcome("DEGRADED", code, msg, ev or {})


class FakeClock:
    def __init__(self):
        self.now_ms = 0

    def advance(self, ms):
        self.now_ms += ms


FAULT_STAGE = {
    "rail_low": "power", "overcurrent": "power", "psu_timeout": "power",
    "no_refclk": "clock", "pll_nolock": "clock",
    "tdo_stuck1": "jtag_idcode", "wrong_part": "jtag_idcode", "probe_lost": "jtag_idcode",
    "rev_bump": "jtag_idcode",
    "bootrom_hang": "boot_rom", "bootrom_crc": "boot_rom",
    "sram_bit": "sram",
    "ddr_lane": "ddr_training", "ddr_margin": "ddr_training", "thermal_runaway": "ddr_training",
    "latent_cold_ddr": "ddr_training",
    "pcie_detect": "pcie_link", "pcie_polling": "pcie_link", "pcie_downshift": "pcie_link",
    "csi2_nolock": "csi2_loopback", "csi2_crc": "csi2_loopback", "csi2_ecc": "csi2_loopback",
    "serdes_nolock": "serdes_loopback", "serdes_ber": "serdes_loopback", "serdes_flaky": "serdes_loopback",
}
# 해당 블록이 실제로 동작하지 못하게 만드는 결함 (나머지는 측정값만 나빠지거나 계측 문제)
BREAKING = {"rail_low", "overcurrent", "no_refclk", "pll_nolock", "tdo_stuck1", "bootrom_hang", "bootrom_crc",
            "sram_bit", "ddr_lane", "pcie_detect", "pcie_polling", "csi2_nolock", "csi2_crc", "serdes_nolock",
            "serdes_ber"}

RAILS = {"VDD_CORE": (0.75, 0.03, 8.0, 3.1), "VDD_DDR": (1.05, 0.03, 4.0, 1.2),
         "VDD_IO": (1.80, 0.05, 2.0, 0.4), "VDD_PHY": (0.90, 0.03, 3.0, 0.9)}  # 공칭 V, ±비율, 전류한계 A, 평상시 A
REFCLK_HZ, REFCLK_PPM = 38.4e6, 50.0
IDCODE_EXPECT, IDCODE_MASK = 0x0B7EA0C9, 0x0FFFFFFF   # [31:28]=버전 [27:12]=부품 [11:1]=제조사 [0]=1
DDR_HARD, DDR_TARGET = 15, 30                         # lane eye % UI
PCIE_TARGET = (4, 4)                                  # Gen4 x4 (가정)
SERDES_BITS, SERDES_MAX_ERR = 3 * 10 ** 9, 0          # 스모크: 3e9 비트 무오류 ≈ BER < 1e-9 @95% (SIV-M06)


class MockBench:
    """가상 SoC SENS-1 + 계측기. PHYS 는 칩의 물리 의존 진실이고, 스크립트가 선언한 deps 와 별개다."""
    PHYS = {"power": (), "clock": ("power",), "jtag_idcode": ("power",), "boot_rom": ("clock",),
            "sram": ("boot_rom",), "ddr_training": ("sram",), "pcie_link": ("sram",),
            "csi2_loopback": ("ddr_training",), "serdes_loopback": ("sram",)}

    def __init__(self, faults=(), board="BRD-01", temp_c=25.0):
        unknown = set(faults) - set(FAULT_STAGE)
        if unknown:
            raise ValueError("unknown fault %s" % sorted(unknown))
        self.faults, self.board, self.temp_c = set(faults), board, temp_c
        self.clock, self.ops, self.safe_calls = FakeClock(), [], 0
        self.boot_polls, self.serdes_tries = 0, 0
        self.meta = {"board": board, "chip": "SENS-1 A0", "script_rev": "day0-0.3", "temp_c": temp_c}

    def _op(self, name, cost_ms):
        self.ops.append(name)
        self.clock.advance(cost_ms)

    def _active(self, fault):
        if fault == "latent_cold_ddr":
            return fault in self.faults and self.temp_c < 0
        return fault in self.faults

    def up(self, stage):
        for f in self.faults:
            if FAULT_STAGE[f] == stage and f in BREAKING and self._active(f):
                return False
        if stage == "ddr_training" and self._active("latent_cold_ddr"):
            return False
        return all(self.up(d) for d in self.PHYS[stage])

    def seen(self, stage):
        return self.up(stage) and self.up("jtag_idcode")   # JTAG 로 읽어야 보이는 블록

    def safe_state(self):
        self.safe_calls += 1
        self.ops.append("safe_state")

    def rails(self):
        self._op("rails", 5)
        if "psu_timeout" in self.faults:
            raise InstrumentError("E_PSU_TIMEOUT", "PSU did not answer *OPC?")
        out = {r: (s[0], s[3]) for r, s in RAILS.items()}
        if "rail_low" in self.faults:
            out["VDD_CORE"] = (0.62, 3.0)
        if "overcurrent" in self.faults:
            out["VDD_DDR"] = (0.93, 12.4)
        return out

    def refclk_hz(self):
        self._op("refclk", 2)
        if "no_refclk" in self.faults or not self.up("power"):
            return 0.0
        return REFCLK_HZ * (1 + 10e-6)

    def pll_locked(self):
        self._op("pll_poll", 1)
        return self.up("clock")

    def idcode(self):
        self._op("idcode", 2)
        if "probe_lost" in self.faults:
            raise InstrumentError("E_PROBE_LOST", "debug probe USB disappeared")
        if not self.up("power"):
            return 0x00000000
        if "tdo_stuck1" in self.faults:
            return 0xFFFFFFFF
        if "wrong_part" in self.faults:
            return 0x0C0DE0C9
        return (1 << 28) | IDCODE_EXPECT if "rev_bump" in self.faults else IDCODE_EXPECT

    def boot_status(self):
        self._op("boot_poll", 10)
        self.boot_polls += 1
        if not self.seen("boot_rom") and "bootrom_crc" not in self.faults:
            return {"done": False, "crc_ok": False}
        if self.boot_polls < 3:
            return {"done": False, "crc_ok": False}
        return {"done": True, "crc_ok": "bootrom_crc" not in self.faults}

    def sram_walk(self):
        self._op("sram_walk", 40)
        if "sram_bit" in self.faults:
            return {"addr": 0x10004A80, "exp": 0x55555555, "act": 0x55555455}
        if not self.seen("boot_rom"):
            return {"addr": 0x10000000, "exp": 0x55555555, "act": 0xFFFFFFFF}
        return None

    def ddr_train(self):
        self._op("ddr_train", 800)
        if not self.seen("sram"):
            return [0, 0, 0, 0]
        if not self.up("ddr_training"):
            return [41, 44, 39, 0]
        if "ddr_margin" in self.faults:
            return [41, 18, 39, 43]
        return [41, 44, 39, 43]

    def die_temp_c(self):
        self._op("die_temp", 1)
        return 131.0 if "thermal_runaway" in self.faults else 47.0

    def pcie_train(self):
        self._op("pcie_train", 300)
        if not self.seen("sram"):
            return {"state": "DETECT", "gen": 0, "width": 0}
        if "pcie_detect" in self.faults:
            return {"state": "DETECT", "gen": 0, "width": 0}
        if "pcie_polling" in self.faults:
            return {"state": "POLLING", "gen": 0, "width": 0}
        if "pcie_downshift" in self.faults:
            return {"state": "L0", "gen": 1, "width": 4}
        return {"state": "L0", "gen": PCIE_TARGET[0], "width": PCIE_TARGET[1]}

    def csi2_loopback(self):
        self._op("csi2_lb", 400)
        if not self.seen("ddr_training"):
            return {"lock": False, "packets": 0, "crc_err": 0, "ecc_corrected": 0}
        if "csi2_nolock" in self.faults:
            return {"lock": False, "packets": 0, "crc_err": 0, "ecc_corrected": 0}
        if "csi2_crc" in self.faults:
            return {"lock": True, "packets": 1000, "crc_err": 17, "ecc_corrected": 0}
        return {"lock": True, "packets": 1000, "crc_err": 0, "ecc_corrected": 5 if "csi2_ecc" in self.faults else 0}

    def serdes_prbs(self):
        self._op("serdes_prbs", 500)
        self.serdes_tries += 1
        if not self.seen("sram") or "serdes_nolock" in self.faults:
            return {"lock": False, "bits": 0, "errors": 0}
        if "serdes_flaky" in self.faults and self.serdes_tries == 1:
            return {"lock": False, "bits": 0, "errors": 0}
        return {"lock": True, "bits": SERDES_BITS, "errors": 120 if "serdes_ber" in self.faults else 0}


def poll_until(bench, fn, budget_ms, step_ms=10):
    """fake clock 기반 poll-with-timeout (SIV-B09 축약). 성공 시 fn() 값, 시간 초과 시 None."""
    start = bench.clock.now_ms
    while True:
        v = fn()
        if v:
            return v
        if bench.clock.now_ms - start >= budget_ms:
            return None
        bench.clock.advance(step_ms)


def chk_power(b, budget):
    rails = b.rails()
    for name, (nom, tol, ilim, _) in RAILS.items():
        if rails[name][1] > ilim:
            raise SafetyAbort("E_OVERCURRENT", "%s draws %.1f A > limit %.1f A" % (name, rails[name][1], ilim),
                              {"rail": name, "amps": rails[name][1], "volts": rails[name][0]})
    for name, (nom, tol, ilim, _) in RAILS.items():
        if abs(rails[name][0] - nom) > nom * tol:
            return bad("E_RAIL_OOR", "%s = %.3f V, want %.3f V +/-%d%%" % (name, rails[name][0], nom, tol * 100),
                       {"rail": name, "volts": rails[name][0]})
    return ok({"rails": {k: v[0] for k, v in sorted(rails.items())}})


def chk_clock(b, budget):
    hz = b.refclk_hz()
    if hz == 0.0:
        return bad("E_REFCLK_ABSENT", "no reference clock at test point", {"hz": hz})
    ppm = (hz - REFCLK_HZ) / REFCLK_HZ * 1e6
    if abs(ppm) > REFCLK_PPM:
        return bad("E_REFCLK_PPM", "refclk off by %.1f ppm" % ppm, {"ppm": round(ppm, 1)})
    if not poll_until(b, b.pll_locked, budget):
        return bad("E_PLL_NOLOCK", "PLL no lock within %d ms" % budget, {"ppm": round(ppm, 1), "budget_ms": budget})
    return ok({"ppm": round(ppm, 1)})


def chk_jtag(b, budget):
    v = b.idcode()
    ev = {"idcode": "0x%08X" % v}
    if v == 0xFFFFFFFF:
        return bad("E_TDO_STUCK_HIGH", "IDCODE reads all ones", ev)
    if v == 0:
        return bad("E_TDO_STUCK_LOW", "IDCODE reads all zeros", ev)
    if not v & 1:
        return bad("E_IDCODE_LSB", "IDCODE bit 0 is not 1", ev)
    if (v & IDCODE_MASK) != (IDCODE_EXPECT & IDCODE_MASK):
        return bad("E_WRONG_PART", "unexpected part/manufacturer", ev)
    ev["rev"] = v >> 28
    return ok(ev)


def chk_boot(b, budget):
    st = poll_until(b, lambda: (lambda s: s if s["done"] else None)(b.boot_status()), budget)
    if st is None:
        return bad("E_BOOTROM_TIMEOUT", "boot ROM not done within %d ms" % budget, {"budget_ms": budget})
    if not st["crc_ok"]:
        return bad("E_BOOTROM_CRC", "boot ROM self-check CRC mismatch", st)
    return ok({"boot_polls": b.boot_polls})


def chk_sram(b, budget):
    r = b.sram_walk()
    if r:
        ev = {"addr": "0x%08X" % r["addr"], "exp": "0x%08X" % r["exp"], "act": "0x%08X" % r["act"],
              "diff": "0x%08X" % (r["exp"] ^ r["act"])}
        return bad("E_SRAM_BIT", "SRAM mismatch at %s" % ev["addr"], ev)
    return ok()


def chk_ddr(b, budget):
    lanes = b.ddr_train()
    temp = b.die_temp_c()
    if temp > 125.0:
        raise SafetyAbort("E_THERMAL", "die %.0f C during DDR training" % temp, {"die_c": temp})
    ev = {"eye_ui_pct": lanes, "die_c": temp}
    worst = min(range(len(lanes)), key=lambda i: lanes[i])
    if lanes[worst] < DDR_HARD:
        return bad("E_DDR_LANE", "lane %d eye %d%% UI < %d%%" % (worst, lanes[worst], DDR_HARD), dict(ev, lane=worst))
    if lanes[worst] < DDR_TARGET:
        return warn("E_DDR_MARGIN", "lane %d eye %d%% UI < target %d%%" % (worst, lanes[worst], DDR_TARGET),
                    dict(ev, lane=worst))
    return ok(ev)


def chk_pcie(b, budget):
    r = b.pcie_train()
    if r["state"] != "L0":
        return bad("E_PCIE_STUCK_%s" % r["state"], "LTSSM ended in %s, want L0" % r["state"], r)
    if (r["gen"], r["width"]) != PCIE_TARGET:
        return warn("E_PCIE_DOWNSHIFT", "L0 at Gen%d x%d, want Gen%d x%d" % (r["gen"], r["width"], PCIE_TARGET[0],
                                                                             PCIE_TARGET[1]), r)
    return ok(r)


def chk_csi2(b, budget):
    r = b.csi2_loopback()
    if not r["lock"]:
        return bad("E_CSI2_NO_LOCK", "receiver did not lock on test pattern", r)
    if r["crc_err"]:
        return bad("E_CSI2_CRC", "%d/%d long packets failed payload CRC" % (r["crc_err"], r["packets"]), r)
    if r["ecc_corrected"]:
        return warn("E_CSI2_ECC_CORRECTED", "%d packet headers needed ECC correction" % r["ecc_corrected"], r)
    return ok(r)


def chk_serdes(b, budget):
    r = b.serdes_prbs()
    if not r["lock"]:
        return bad("E_SERDES_NO_LOCK", "PRBS checker did not lock", r)
    if r["errors"] > SERDES_MAX_ERR:
        return bad("E_SERDES_BER", "%d errors in %d bits" % (r["errors"], r["bits"]), r)
    return ok(r)


@dataclasses.dataclass(frozen=True)
class Stage:
    name: str
    deps: tuple
    check: object
    budget_ms: int = 100
    retries: int = 0
    owner: str = "bringup"


def day0_stages():
    """선언된 DAG. JTAG 는 전원만 있으면 TCK 를 프로브가 준다. PCIe/SerDes 는 DDR 없이 SRAM 펌웨어로 돈다(가정)."""
    return [
        Stage("power", (), chk_power, 50, 0, "power-integrity"),
        Stage("clock", ("power",), chk_clock, 50, 0, "clocks"),
        Stage("jtag_idcode", ("power",), chk_jtag, 20, 1, "dft-debug"),
        Stage("boot_rom", ("clock", "jtag_idcode"), chk_boot, 200, 0, "boot-fw"),
        Stage("sram", ("boot_rom",), chk_sram, 100, 0, "soc-integration"),
        Stage("ddr_training", ("sram",), chk_ddr, 2000, 0, "memory"),
        Stage("pcie_link", ("sram",), chk_pcie, 1000, 1, "pcie"),
        Stage("csi2_loopback", ("ddr_training",), chk_csi2, 1000, 1, "camera-io"),
        Stage("serdes_loopback", ("sram",), chk_serdes, 1000, 1, "radar-io"),
    ]


def chain_stages(stages):
    """직선 사슬 모드: 각 스테이지가 바로 앞 스테이지에만 의존."""
    out, prev = [], None
    for s in stages:
        out.append(dataclasses.replace(s, deps=(prev,) if prev else ()))
        prev = s.name
    return out


NEXT_ACTION = {
    "E_OVERCURRENT": "Keep rail off. Measure rail-to-GND resistance unpowered, thermal-camera at low current limit.",
    "E_THERMAL": "Stop. Check heatsink/contact and sensor correlation before any retry.",
    "E_RAIL_OOR": "Probe rail at the die-side test point; check PMIC config and sense lines.",
    "E_REFCLK_ABSENT": "Scope the oscillator and its enable; check clock buffer power.",
    "E_REFCLK_PPM": "Measure with a frequency counter; check crystal load / oscillator part.",
    "E_PLL_NOLOCK": "Read PLL status over JTAG (JTAG still ran); check PLL rail noise and divider config.",
    "E_TDO_STUCK_HIGH": "Check TAP reset, strap for JTAG mux, TDO pull-up, and scan-chain continuity.",
    "E_TDO_STUCK_LOW": "Check target power at JTAG header (VTref) and TDO short to ground.",
    "E_WRONG_PART": "Confirm part marking and which die is on this board; check JTAG chain order.",
    "E_BOOTROM_TIMEOUT": "Read PC/boot breadcrumb over JTAG; check boot straps and secure-boot/fuse state.",
    "E_BOOTROM_CRC": "Compare ROM image against tape-out checksum; re-read at a lower clock.",
    "E_SRAM_BIT": "Repeat March test at address neighbours; check SRAM rail and repair/fuse state.",
    "E_DDR_LANE": "Dump per-lane training results; swap DRAM/board; check that lane's DQS routing.",
    "E_PCIE_STUCK_DETECT": "No receiver detected: check partner presence, AC caps, reset and refclk.",
    "E_PCIE_STUCK_POLLING": "Receiver seen, no training sets: check polarity/lane swap and TX eye.",
    "E_CSI2_NO_LOCK": "Check PHY lane enable, HS settle timing, and the test-pattern source.",
    "E_CSI2_CRC": "Count CRC vs lane; lower the lane rate; check PHY termination and receiver timing.",
    "E_SERDES_NO_LOCK": "Check link partner, cable, and SerDes lock/PLL status registers.",
    "E_SERDES_BER": "Run PRBS per lane with EQ sweep (SIV-M06); shorten cable to isolate channel.",
    "E_PROBE_LOST": "Bench problem. Reconnect or swap probe, re-run; do not file a silicon bug.",
    "E_PSU_TIMEOUT": "Bench problem. Check PSU link; re-run; do not file a silicon bug.",
    "E_HARNESS_BUG": "Script bug: fix the stage code, then re-run; no statement about silicon.",
}


def validate_stages(stages):
    """이름 중복·미지 의존·순환을 하드웨어를 건드리기 전에 잡고, 실행 순서(이름 목록)를 돌려준다."""
    # TODO: 이름 중복 → ConfigError("E_DUP_STAGE", 이름). 없는 의존 → ConfigError("E_UNKNOWN_DEP", ...).
    #       선언 순서를 지키는 위상 정렬: 남은 스테이지 중 "의존이 다 끝난 첫 스테이지"를 반복해서 꺼낸다.
    #       더 못 꺼내면 ConfigError("E_CYCLE", 남은 이름들).
    raise NotImplementedError("TODO validate_stages")


def new_result(stage, status, code="", msg="", evidence=None):
    return {"stage": stage.name, "status": status, "code": code, "msg": msg, "evidence": evidence or {},
            "owner": stage.owner, "attempt_codes": [], "blocked_by": [], "roots": [], "kind": "",
            "safety": False, "t_start_ms": 0, "t_end_ms": 0}


def run_day0(bench, stages, skip=(), gating=True):
    """스테이지를 순서대로 돌려 {이름: 결과 dict} (실행 순서)를 돌려준다."""
    # TODO: order = validate_stages(stages) 를 맨 먼저 (ConfigError 는 그대로 전파, 벤치 무접촉).
    #       try/finally 로 bench.safe_state() 를 정확히 1번 보장.
    #       이미 안전 중단됐으면 남은 스테이지: BLOCKED, blocked_by=[중단 스테이지], roots 같음, kind="safety".
    #       skip 에 있으면 SKIPPED. gating 이고 의존 중 SATISFIED 아닌 것이 있으면 BLOCKED:
    #         blocked_by = 그 의존들, roots = 각 의존의 roots(없으면 의존 자신) 합집합을 실행 순서로,
    #         kind = FAIL 인 뿌리가 있으면 "dut", 없고 ERROR 뿌리가 있으면 "infra", 아니면 "plan".
    #       실행: 최대 retries+1 회 check(bench, budget_ms).
    #         SafetyAbort → FAIL, safety=True, 재시도 금지, 이후 전부 중단.
    #         InstrumentError → ERROR(재시도 가능). 그 밖의 Exception → ERROR "E_HARNESS_BUG"(재시도 안 함).
    #         PASS/DEGRADED 가 나오면 그만. 앞에 실패 시도가 있었는데 PASS 면 status="FLAKY", code=첫 실패 코드.
    #       attempt_codes(시도별 code 목록), t_start_ms/t_end_ms(bench.clock) 기록.
    raise NotImplementedError("TODO run_day0")


def verdict_of(results):
    """(verdict, exit_code). 우선순위 ABORTED > FAIL > INCONCLUSIVE > DEGRADED > PASS."""
    # TODO: safety 플래그가 있으면 ABORTED. status FAIL 이 있으면 FAIL. ERROR 가 있으면 INCONCLUSIVE.
    #       FLAKY/DEGRADED/SKIPPED/BLOCKED 가 남아 있으면 DEGRADED. 아니면 PASS. exit code 는 EXIT 표.
    raise NotImplementedError("TODO verdict_of")


def signature(keys):
    return "%08x" % zlib.crc32("|".join(sorted(keys)).encode()) if keys else "00000000"


def triage_packet(results, meta):
    """디버그 엔지니어에게 넘길 결정적 JSON 호환 dict."""
    # TODO: verdict, exit = verdict_of(results). roots = 상태 FAIL/ERROR 결과들(실행 순서).
    #       flags = FLAKY/DEGRADED/SKIPPED 결과들. keys = "stage:code" (roots 가 있으면 roots 로, 없으면 flags 로).
    #       root 항목: stage,status,code,msg,evidence,attempt_codes,
    #         owner(ERROR 는 "lab-infra", E_HARNESS_BUG 는 "validation-sw", 그 외 스테이지 owner),
    #         next_action(NEXT_ACTION.get(code, "Investigate with stage owner.")).
    #       packet 키: schema="day0-triage/1", meta, verdict, exit_code, signature=signature(keys), root_keys=sorted(keys),
    #         first_failure(roots[0] 또는 None), roots, flags=[{stage,status,code,msg}],
    #         blocked=[{stage,blocked_by,roots,kind}], counts(상태별 개수 dict),
    #         timeline=[{t_ms,stage,status,code}].
    raise NotImplementedError("TODO triage_packet")


def summarize_campaign(packets):
    """보드 여러 장의 packet → 시그니처별 집계 행 (건수 내림차순, 동률은 시그니처 순)."""
    # TODO: 분모 n_valid = verdict 가 INCONCLUSIVE 가 아닌 packet 수. verdict PASS 는 행을 만들지 않는다.
    #       signature 로 묶어 행 {signature, root_keys, verdict, count, boards(정렬), attribution}.
    #       attribution: verdict 가 INCONCLUSIVE → "LAB_INFRA"; n_valid < 3 → "INSUFFICIENT_SAMPLES";
    #       count ≥ 2 이고 count/n_valid ≥ 0.5 → "SUSPECT_SYSTEMATIC"; count == 1 → "SUSPECT_BOARD_OR_DIE";
    #       그 밖 → "SUSPECT_SUBPOPULATION".
    raise NotImplementedError("TODO summarize_campaign")


# ── 주어진 보고 · 데모 ────────────────────────────────────────────
def render_report(p):
    m = p["meta"]
    lines = ["=== Day-0 triage: %s  chip=%s  script=%s  T=%.0fC ===" % (m["board"], m["chip"], m["script_rev"], m["temp_c"]),
             "verdict  : %s (exit %d)   signature %s" % (p["verdict"], p["exit_code"], p["signature"]),
             "counts   : " + "  ".join("%s=%d" % kv for kv in p["counts"].items())]
    for r in p["roots"]:
        lines.append("root     : %-16s %-6s %-20s owner=%s attempts=%d" % (r["stage"], r["status"], r["code"], r["owner"],
                                                                         len(r["attempt_codes"])))
        lines.append("  detail : %s" % r["msg"])
        lines.append("  evidence: %s" % json.dumps(r["evidence"], sort_keys=True))
        lines.append("  next   : %s" % r["next_action"])
    for b in p["blocked"]:
        lines.append("blocked  : %-16s <- %-24s [%s]" % (b["stage"], ",".join(b["blocked_by"]), b["kind"]))
    for f in p["flags"]:
        lines.append("flag     : %-16s %-8s %-20s %s" % (f["stage"], f["status"], f["code"], f["msg"]))
    return "\n".join(lines)


FLEET = [("BRD-01", ()), ("BRD-02", ("pcie_detect",)), ("BRD-03", ("ddr_lane",)), ("BRD-04", ("ddr_lane",)),
         ("BRD-05", ("ddr_lane",)), ("BRD-06", ("ddr_lane", "csi2_crc")), ("BRD-07", ("probe_lost",)),
         ("BRD-08", ("overcurrent",))]
SINGLE_FAULTS = ("rail_low", "no_refclk", "tdo_stuck1", "bootrom_hang", "sram_bit", "ddr_lane", "pcie_detect",
                 "csi2_crc", "serdes_nolock")
EXPECT_CODE = {"rail_low": "E_RAIL_OOR", "no_refclk": "E_REFCLK_ABSENT", "tdo_stuck1": "E_TDO_STUCK_HIGH",
               "bootrom_hang": "E_BOOTROM_TIMEOUT", "sram_bit": "E_SRAM_BIT", "ddr_lane": "E_DDR_LANE",
               "pcie_detect": "E_PCIE_STUCK_DETECT", "csi2_crc": "E_CSI2_CRC", "serdes_nolock": "E_SERDES_NO_LOCK"}
MODES = ("chain", "dag", "no_gating")


def run_mode(faults, mode):
    st = day0_stages()
    if mode == "chain":
        st = chain_stages(st)
    return run_day0(MockBench(faults), st, gating=(mode != "no_gating"))


def is_true_fail(res, fault):
    r = res[FAULT_STAGE[fault]]
    return r["status"] == "FAIL" and r["code"] == EXPECT_CODE[fault]


def gating_limits():
    """모드별: 단일 결함 평균 측정 스테이지 수·거짓 FAIL 수, 결함 쌍 동시 검출률."""
    out = {}
    for mode in MODES:
        measured, false_fails = 0, 0
        for f in SINGLE_FAULTS:
            res = run_mode((f,), mode)
            measured += sum(1 for r in res.values() if r["status"] in ("PASS", "FAIL"))
            false_fails += sum(1 for n, r in res.items() if r["status"] == "FAIL" and n != FAULT_STAGE[f])
        pairs = list(itertools.combinations(SINGLE_FAULTS, 2))
        both = sum(1 for a, b in pairs if (lambda res: is_true_fail(res, a) and is_true_fail(res, b))(run_mode((a, b), mode)))
        out[mode] = {"avg_measured": measured / len(SINGLE_FAULTS), "false_fails": false_fails,
                     "both_detected": both, "pairs": len(pairs)}
    return out


def demo():
    packets = []
    for board, faults in FLEET:
        b = MockBench(faults, board=board)
        packets.append(triage_packet(run_day0(b, day0_stages()), b.meta))
    for i in (2, 5, 6, 7):
        print(render_report(packets[i]))
        print()
    print("Campaign summary (%d boards)" % len(packets))
    print("%-9s %-13s %-5s %-22s %s" % ("signature", "verdict", "count", "attribution", "root / boards"))
    for r in summarize_campaign(packets):
        print("%-9s %-13s %-5d %-22s %s  %s" % (r["signature"], r["verdict"], r["count"], r["attribution"],
                                               ",".join(r["root_keys"]), ",".join(r["boards"])))
    print()
    print("critical path (weeks):", critical_path(SCHEDULE))
    no_spin = {k: v for k, v in SCHEDULE.items() if k not in ("a1_metal_spin", "a1_regression")}
    no_spin["production_handoff"] = (3, ("pvt_char", "stress_soak"))
    print("critical path, no respin  :", critical_path(no_spin))
    print("PVT capacity weeks @ 4/6/8 benches:", ["%.1f" % capacity_weeks(PVT_DEMAND, n, 20, 6, 0.7) for n in (4, 6, 8)])
    print("triage backlog after 10 days (60 fails/day, 4 engineers, 1.5 dispositions/day):")
    for dd in (0.5, 0.8, 0.9, 0.95):
        print("   dedup %.2f -> %.1f" % (dd, triage_backlog(60, dd, 4, 1.5, 10)[-1]))
    print("what to cut, weeks needed -> best_cut:")
    for need in range(1, 7):
        c = best_cut(CUT_ITEMS, need)
        print("   %d -> risk %-3d saves %d  %s" % (need, c["risk"], c["weeks"], ",".join(c["names"])))
    print("gating limits (9 single faults, 36 fault pairs):")
    print("%-10s %12s %12s %16s" % ("mode", "avg_measured", "false_fails", "both_detected"))
    for mode, r in gating_limits().items():
        print("%-10s %12.2f %12d %10d/%d" % (mode, r["avg_measured"], r["false_fails"], r["both_detected"], r["pairs"]))


# ── 하네스 ───────────────────────────────────────────────────────
PASSED, TOTAL = 0, 0


def check(name, fn):
    global PASSED, TOTAL
    TOTAL += 1
    try:
        r = fn()
        cond, detail = r if isinstance(r, tuple) else (r, "")
    except NotImplementedError as e:
        cond, detail = False, "NotImplementedError: %s" % e
    except Exception as e:  # 하네스는 끝까지 돈다
        cond, detail = False, "%s: %s" % (type(e).__name__, e)
    if cond:
        PASSED += 1
        print("PASS  %s" % name)
    else:
        print("FAIL  %s  (%s)" % (name, detail))


def codes(findings, code):
    return [f for f in findings if f[0] == code]


def mutated(fn):
    p = good_plan()
    fn(p)
    return p


def plan_cases():
    def drop_phase(p):
        del p[2]

    def swap(p):
        p[3], p[4] = p[4], p[3]

    def no_owner(p):
        p[1]["owner"] = ""

    def vague(p):
        p[3]["exit"][0] = {"text": "looks stable"}

    def unmeasured(p):
        p[4]["exit"][1]["value"] = "low"

    def boolish(p):
        p[4]["exit"][0]["value"] = True

    def unlinked(p):
        p[2]["after"] = "pre_si"

    def hole(p):
        p[3]["covers"] = tuple(i for i in IPS if i != "SERDES")

    def unknown_ip(p):
        p[1]["covers"] = IPS + ("NPU",)

    return [("drop feature", drop_phase, "E_ORDER", "*", None),
            ("swap pvt/stress", swap, "E_ORDER", "*", None),
            ("no owner day0", no_owner, "E_NO_OWNER", "day0", None),
            ("vague pvt exit", vague, "E_VAGUE_EXIT", "pvt", None),
            ("string value", unmeasured, "E_VAGUE_EXIT", "stress", None),
            ("bool value", boolish, "E_VAGUE_EXIT", "stress", None),
            ("unlinked entry", unlinked, "E_ENTRY_UNLINKED", "feature", None),
            ("coverage hole", hole, "E_COVERAGE_HOLE", "pvt", "SERDES"),
            ("unknown ip", unknown_ip, "E_UNKNOWN_IP", "day0", "NPU")]


def run_board(faults=(), board="BRD-01", temp_c=25.0, skip=()):
    b = MockBench(faults, board=board, temp_c=temp_c)
    res = run_day0(b, day0_stages(), skip=skip)
    return b, res, triage_packet(res, b.meta)


ORDER = ["power", "clock", "jtag_idcode", "boot_rom", "sram", "ddr_training", "pcie_link", "csi2_loopback",
         "serdes_loopback"]
ALL_AFTER_POWER = ORDER[1:]
AFTER_CLOCK = ORDER[3:]
AFTER_BOOT = ORDER[4:]
AFTER_SRAM = ORDER[5:]
# fault, 상태, 코드, blocked 목록(정확히), verdict
SINGLE_TABLE = [
    ("rail_low", "FAIL", "E_RAIL_OOR", ALL_AFTER_POWER, "FAIL"),
    ("no_refclk", "FAIL", "E_REFCLK_ABSENT", AFTER_CLOCK, "FAIL"),
    ("pll_nolock", "FAIL", "E_PLL_NOLOCK", AFTER_CLOCK, "FAIL"),
    ("tdo_stuck1", "FAIL", "E_TDO_STUCK_HIGH", AFTER_CLOCK, "FAIL"),
    ("wrong_part", "FAIL", "E_WRONG_PART", AFTER_CLOCK, "FAIL"),
    ("bootrom_hang", "FAIL", "E_BOOTROM_TIMEOUT", AFTER_BOOT, "FAIL"),
    ("bootrom_crc", "FAIL", "E_BOOTROM_CRC", AFTER_BOOT, "FAIL"),
    ("sram_bit", "FAIL", "E_SRAM_BIT", AFTER_SRAM, "FAIL"),
    ("ddr_lane", "FAIL", "E_DDR_LANE", ["csi2_loopback"], "FAIL"),
    ("pcie_detect", "FAIL", "E_PCIE_STUCK_DETECT", [], "FAIL"),
    ("pcie_polling", "FAIL", "E_PCIE_STUCK_POLLING", [], "FAIL"),
    ("csi2_nolock", "FAIL", "E_CSI2_NO_LOCK", [], "FAIL"),
    ("csi2_crc", "FAIL", "E_CSI2_CRC", [], "FAIL"),
    ("serdes_nolock", "FAIL", "E_SERDES_NO_LOCK", [], "FAIL"),
    ("serdes_ber", "FAIL", "E_SERDES_BER", [], "FAIL"),
    ("ddr_margin", "DEGRADED", "E_DDR_MARGIN", [], "DEGRADED"),
    ("pcie_downshift", "DEGRADED", "E_PCIE_DOWNSHIFT", [], "DEGRADED"),
    ("csi2_ecc", "DEGRADED", "E_CSI2_ECC_CORRECTED", [], "DEGRADED"),
    ("serdes_flaky", "FLAKY", "E_SERDES_NO_LOCK", [], "DEGRADED"),
    ("probe_lost", "ERROR", "E_PROBE_LOST", AFTER_CLOCK, "INCONCLUSIVE"),
    ("psu_timeout", "ERROR", "E_PSU_TIMEOUT", ALL_AFTER_POWER, "INCONCLUSIVE"),
]


def blocked_names(res):
    return [n for n, r in res.items() if r["status"] == "BLOCKED"]


def tests():
    print("-- given mock sanity --")
    check("good mock: all rails read back in spec", lambda: chk_power(MockBench(), 50).status == "PASS")
    check("JTAG bench: idcode good", lambda: chk_jtag(MockBench(), 20).status == "PASS")
    check("fault injection: tdo_stuck1 reads all ones", lambda: chk_jtag(MockBench(["tdo_stuck1"]), 20).code == "E_TDO_STUCK_HIGH")
    check("fake clock: bootrom_hang consumes the budget, returns timeout",
          lambda: chk_boot(MockBench(["bootrom_hang"]), 200).code == "E_BOOTROM_TIMEOUT")
    check("greedy_cut returns a covering set", lambda: greedy_cut(CUT_ITEMS, 3)["weeks"] >= 3)

    print("-- Part A: plan lint + gates --")
    check("good plan: no findings", lambda: (lint_plan(good_plan()) == [], str(lint_plan(good_plan()))))
    for name, mut, code, ph_name, detail in plan_cases():
        def t(mut=mut, code=code, ph_name=ph_name, detail=detail):
            hits = codes(lint_plan(mutated(mut)), code)
            good = [h for h in hits if h[1] == ph_name and (detail is None or h[2] == detail)]
            return bool(good), str(hits)
        check("lint: %s -> %s @%s" % (name, code, ph_name), t)
    pvt = good_plan()[3]
    good_metrics = {"corners_completed_pct": 100, "samples_per_corner": 5, "vmin_guardband_mv": 42, "ddr_min_eye_ui_pct": 33}
    check("gate: all met -> GO", lambda: gate_decision(pvt, good_metrics)["decision"] == "GO")
    check("gate: hard criterion fails -> NO_GO, named",
          lambda: gate_decision(pvt, dict(good_metrics, vmin_guardband_mv=12), waivers=("vmin_guardband_mv",))["failed"] == ["vmin_guardband_mv"])
    check("gate: waivable + waiver -> GO_WITH_WAIVER",
          lambda: gate_decision(pvt, dict(good_metrics, samples_per_corner=3), waivers=("samples_per_corner",))
          == {"decision": "GO_WITH_WAIVER", "failed": [], "waived": ["samples_per_corner"], "missing": []})
    check("gate: waivable without waiver -> NO_GO",
          lambda: gate_decision(pvt, dict(good_metrics, samples_per_corner=3))["decision"] == "NO_GO")
    def t_missing():
        m = dict(good_metrics)
        del m["ddr_min_eye_ui_pct"]
        d = gate_decision(pvt, m)
        return d["decision"] == "NO_GO" and d["missing"] == ["ddr_min_eye_ui_pct"], str(d)
    check("gate: missing metric is NO_GO, not a pass", t_missing)

    print("-- Part B: schedule, capacity, cuts --")
    check("critical path of SENS-1 schedule", lambda: (critical_path(SCHEDULE) == (47, ["silicon_fab", "day0_bringup", "ddr_feature",
          "a1_metal_spin", "a1_regression", "production_handoff"]), str(critical_path(SCHEDULE))))
    check("critical path tie breaks by name", lambda: critical_path({"a": (2, ()), "b": (2, ()), "c": (1, ("a", "b"))}) == (3, ["a", "c"]))
    def raises(code, fn):
        try:
            fn()
        except ValueError as e:
            return str(e).startswith(code), str(e)
        return False, "no exception"
    check("critical path: cycle -> E_CYCLE", lambda: raises("E_CYCLE", lambda: critical_path({"a": (1, ("b",)), "b": (1, ("a",))})))
    check("critical path: unknown dep", lambda: raises("E_UNKNOWN_DEP", lambda: critical_path({"a": (1, ("zz",))})))
    demand = [{"hours": 6, "corners": 9, "samples": 5}, {"hours": 2, "corners": 9, "samples": 5}]
    check("capacity: 360 bench-h / (4*20*6*0.75) = 1.0 week (exact arithmetic)",
          lambda: (abs(capacity_weeks(demand, 4, 20, 6, 0.75) - 1.0) < 1e-9, str(capacity_weeks(demand, 4, 20, 6, 0.75))))
    def t_backlog():
        got = triage_backlog(40, 0.7, 3, 2, 4)
        return all(abs(a - b) <= 1e-9 for a, b in zip(got, [6.0, 12.0, 18.0, 24.0])) and len(got) == 4, str(got)
    check("backlog: arrivals > capacity grows linearly (tol 1e-9, float arithmetic only)", t_backlog)
    check("backlog: stable when capacity >= arrivals, never negative", lambda: triage_backlog(20, 0.9, 3, 2, 3) == [0.0, 0.0, 0.0])
    check("best_cut: 3 weeks at minimum risk",
          lambda: (best_cut(CUT_ITEMS, 3) == {"names": ["dashboard_polish", "serdes_cable_matrix_trim"], "weeks": 3, "risk": 4},
                   str(best_cut(CUT_ITEMS, 3))))
    def t_keep():
        for need in range(1, 11):
            c = best_cut(CUT_ITEMS, need)
            if c and set(c["names"]) & {"power_sequencing_and_thermal_trip", "first_failure_safety_and_abort_path"}:
                return False, str(c)
        return True
    check("best_cut never touches must_keep items", t_keep)
    check("best_cut infeasible -> None", lambda: best_cut(CUT_ITEMS, 11) is None)
    def t_vs_brute():
        pool = [it for it in CUT_ITEMS if not it[3]]
        for need in range(1, 11):
            brute = min((sum(i[2] for i in c) for r in range(len(pool) + 1) for c in itertools.combinations(pool, r)
                         if sum(i[1] for i in c) >= need), default=None)
            got = best_cut(CUT_ITEMS, need)
            if (got["risk"] if got else None) != brute:
                return False, "need=%d got=%s brute=%s" % (need, got, brute)
        return True
    check("best_cut risk == independent brute-force minimum, need 1..10", t_vs_brute)
    def t_greedy_limit():
        items = [("a", 1, 1, False), ("b", 2, 4, False), ("c", 5, 11, False), ("d", 3, 6, False)]
        g, b = greedy_cut(items, 5), best_cut(items, 5)
        return g["risk"] > b["risk"], "greedy %s best %s" % (g, b)
    check("limit: greedy by risk/week can be worse than exact", t_greedy_limit)

    print("-- Part C: gating + classification --")
    check("good board: 9 PASS, exit 0", lambda: (lambda p: (p["verdict"], p["exit_code"], p["counts"]) == ("PASS", 0, {"PASS": 9}))(run_board()[2]))
    for fault, status, code, blocked, verdict in SINGLE_TABLE:
        def t(fault=fault, status=status, code=code, blocked=blocked, verdict=verdict):
            b, res, p = run_board((fault,))
            root = res[FAULT_STAGE[fault]]
            got = (root["status"], root["code"], blocked_names(res), p["verdict"])
            return got == (status, code, blocked, verdict), str(got)
        check("single fault %-14s -> %s %s, %d blocked, %s" % (fault, status, code, len(blocked), verdict), t)
    def t_rev():
        r = run_board(("rev_bump",))[1]["jtag_idcode"]
        return r["status"] == "PASS" and r["evidence"]["rev"] == 1, str(r["evidence"])
    check("revision field differs only: PASS, rev reported for the errata table", t_rev)
    def t_exit():
        return [run_board((f,))[2]["exit_code"] for f in ("rail_low", "probe_lost", "pcie_downshift", "overcurrent")] == [1, 3, 4, 1]
    check("exit codes: FAIL=1, INCONCLUSIVE=3, DEGRADED=4, ABORTED=1", t_exit)
    def t_dag_jtag_on_clock_fail():
        b, res, p = run_board(("pll_nolock",))
        return res["jtag_idcode"]["status"] == "PASS" and res["clock"]["status"] == "FAIL", str(res["jtag_idcode"]["status"])
    check("DAG: PLL dead, JTAG still runs (diagnostic path stays open)", t_dag_jtag_on_clock_fail)
    def t_ddr_not_block_pcie():
        b, res, p = run_board(("ddr_lane",))
        return res["pcie_link"]["status"] == "PASS" and res["serdes_loopback"]["status"] == "PASS", ""
    check("DAG: DDR dead does not block PCIe/SerDes", t_ddr_not_block_pcie)
    def t_kind():
        _, res, _ = run_board(("sram_bit",))
        _, res2, _ = run_board(("probe_lost",))
        return res["ddr_training"]["kind"] == "dut" and res2["sram"]["kind"] == "infra" and res2["sram"]["roots"] == ["jtag_idcode"], ""
    check("BLOCKED carries kind dut vs infra and transitive roots", t_kind)
    def t_multi():
        _, res, _ = run_board(("no_refclk", "tdo_stuck1"))
        r = res["boot_rom"]
        return r["blocked_by"] == ["clock", "jtag_idcode"] and r["roots"] == ["clock", "jtag_idcode"], str(r["roots"])
    check("two failed deps -> both listed as roots", t_multi)
    def t_overcurrent():
        b, res, p = run_board(("overcurrent",))
        after = b.ops[b.ops.index("rails") + 1:]
        return (p["verdict"] == "ABORTED" and res["power"]["safety"] and after == ["safe_state"] and b.safe_calls == 1
                and all(res[n]["kind"] == "safety" for n in ALL_AFTER_POWER)), str(after)
    check("overcurrent: abort, no further bench op, safe_state once", t_overcurrent)
    def t_thermal():
        b, res, p = run_board(("thermal_runaway",))
        return (res["ddr_training"]["code"] == "E_THERMAL" and blocked_names(res) == ["pcie_link", "csi2_loopback", "serdes_loopback"]
                and all(res[n]["kind"] == "safety" for n in blocked_names(res)) and b.safe_calls == 1), str(blocked_names(res))
    check("mid-run thermal: abort stops independent stages too", t_thermal)
    def t_retry():
        b, res, p = run_board(("serdes_flaky",))
        r = res["serdes_loopback"]
        return r["status"] == "FLAKY" and r["attempt_codes"] == ["E_SERDES_NO_LOCK", ""] and p["verdict"] == "DEGRADED", str(r)
    check("retry pass is FLAKY, not PASS; attempts kept", t_retry)
    def t_retry_exhaust():
        _, res, _ = run_board(("csi2_crc",))
        return res["csi2_loopback"]["attempt_codes"] == ["E_CSI2_CRC", "E_CSI2_CRC"], str(res["csi2_loopback"]["attempt_codes"])
    check("retry budget used, final FAIL keeps both attempts", t_retry_exhaust)
    def t_harness_bug():
        def boom(b, budget):
            raise KeyError("lane_map")
        st = [dataclasses.replace(s, check=boom) if s.name == "sram" else s for s in day0_stages()]
        b = MockBench()
        res = run_day0(b, st)
        p = triage_packet(res, b.meta)
        return (res["sram"]["status"] == "ERROR" and res["sram"]["code"] == "E_HARNESS_BUG" and p["verdict"] == "INCONCLUSIVE"
                and p["roots"][0]["owner"] == "validation-sw" and b.safe_calls == 1), str(res["sram"])
    check("script exception -> ERROR E_HARNESS_BUG, never silicon FAIL", t_harness_bug)
    def t_cfg(code, stages):
        b = MockBench()
        try:
            run_day0(b, stages)
        except ConfigError as e:
            return e.code == code and b.ops == [] and b.safe_calls == 0, "%s ops=%s" % (e, b.ops)
        return False, "no ConfigError"
    base = day0_stages()
    check("config: duplicate stage -> E_DUP_STAGE before touching bench", lambda: t_cfg("E_DUP_STAGE", base + [base[0]]))
    check("config: unknown dep -> E_UNKNOWN_DEP before touching bench",
          lambda: t_cfg("E_UNKNOWN_DEP", [dataclasses.replace(base[1], deps=("psu",))]))
    check("config: cycle -> E_CYCLE before touching bench",
          lambda: t_cfg("E_CYCLE", [dataclasses.replace(base[0], deps=("clock",)), base[1]]))
    def t_skip():
        _, res, p = run_board((), skip=("serdes_loopback",))
        _, res2, p2 = run_board((), skip=("sram",))
        return (res["serdes_loopback"]["status"] == "SKIPPED" and p["verdict"] == "DEGRADED"
                and res2["ddr_training"]["kind"] == "plan" and res2["ddr_training"]["roots"] == ["sram"]), str(p["verdict"])
    check("skip: SKIPPED + plan-kind blocks, verdict DEGRADED", t_skip)
    def t_timeout():
        b, res, _ = run_board(("bootrom_hang",))
        r = res["boot_rom"]
        return r["t_end_ms"] - r["t_start_ms"] >= 200, str(r["t_end_ms"] - r["t_start_ms"])
    check("timeout: stage spends its fake-clock budget", t_timeout)

    print("-- Part C: triage packet + campaign --")
    def t_packet():
        p = run_board(("ddr_lane",), board="BRD-03")[2]
        ff = p["first_failure"]
        return (ff["stage"] == "ddr_training" and ff["code"] == "E_DDR_LANE" and ff["owner"] == "memory" and ff["evidence"]["lane"] == 3
                and "training" in ff["next_action"] and p["blocked"][0]["stage"] == "csi2_loopback"
                and p["blocked"][0]["roots"] == ["ddr_training"] and p["meta"]["board"] == "BRD-03"
                and p["root_keys"] == ["ddr_training:E_DDR_LANE"]), str(ff)
    check("packet: root, owner, evidence, next action, blocked", t_packet)
    check("packet: infra root is owned by lab-infra", lambda: run_board(("probe_lost",))[2]["first_failure"]["owner"] == "lab-infra")
    check("packet: JSON round trip", lambda: json.loads(json.dumps(run_board(("csi2_crc",))[2])) == run_board(("csi2_crc",))[2])
    def t_bytes():
        a = json.dumps(run_board(("sram_bit",), board="B")[2], sort_keys=True)
        c = json.dumps(run_board(("sram_bit",), board="B")[2], sort_keys=True)
        return a == c
    check("packet: same input -> byte-identical JSON", t_bytes)
    def t_sig():
        s = lambda f, b="X": run_board((f,), board=b)[2]["signature"]
        return s("ddr_lane", "A") == s("ddr_lane", "B") and s("ddr_lane") != s("pcie_detect") and s("ddr_margin") != "00000000"
    check("signature: stable across boards, differs by root, flags count", t_sig)
    def fleet():
        return [run_board(f, board=b)[2] for b, f in FLEET]
    def t_campaign():
        rows = summarize_campaign(fleet())
        got = {r["root_keys"][0]: (r["count"], r["attribution"], r["boards"]) for r in rows}
        want = {"ddr_training:E_DDR_LANE": (4, "SUSPECT_SYSTEMATIC", ["BRD-03", "BRD-04", "BRD-05", "BRD-06"]),
                "pcie_link:E_PCIE_STUCK_DETECT": (1, "SUSPECT_BOARD_OR_DIE", ["BRD-02"]),
                "power:E_OVERCURRENT": (1, "SUSPECT_BOARD_OR_DIE", ["BRD-08"]),
                "jtag_idcode:E_PROBE_LOST": (1, "LAB_INFRA", ["BRD-07"])}
        return got == want and rows[0]["count"] == 4, str(got)
    check("campaign: planted fleet attributed correctly", t_campaign)
    check("campaign: two valid boards -> INSUFFICIENT_SAMPLES (honest)",
          lambda: summarize_campaign(fleet()[2:4])[0]["attribution"] == "INSUFFICIENT_SAMPLES")
    check("campaign: 2 of 7 valid -> SUSPECT_SUBPOPULATION", lambda: summarize_campaign(
        [run_board(("csi2_crc",), board="A")[2], run_board(("csi2_crc",), board="B")[2]]
        + [run_board(board="G%d" % i)[2] for i in range(5)])[0]["attribution"] == "SUSPECT_SUBPOPULATION")

    print("-- limits: what gating and Day-0 cannot see --")
    def t_masked():
        p = fleet()[5]
        keys = p["root_keys"]
        blocked = [b["stage"] for b in p["blocked"]]
        return keys == ["ddr_training:E_DDR_LANE"] and "csi2_loopback" in blocked, str(keys)
    check("masking: BRD-06 has a CSI-2 defect, packet can only say csi2 BLOCKED", t_masked)
    def t_limits():
        lim = gating_limits()
        print("      measured %s" % json.dumps(lim, sort_keys=True))
        d, c, n = lim["dag"], lim["chain"], lim["no_gating"]
        return (d["avg_measured"] > c["avg_measured"] and d["false_fails"] == 0 and c["false_fails"] == 0 and n["false_fails"] > 0
                and d["both_detected"] > c["both_detected"] and d["both_detected"] < d["pairs"]), json.dumps(lim, sort_keys=True)
    check("limits: DAG measures more than chain, no false FAIL, masking remains", t_limits)
    def t_latent():
        warm = run_board(("latent_cold_ddr",), temp_c=25.0)[2]
        cold = run_board(("latent_cold_ddr",), temp_c=-40.0)[2]
        return warm["verdict"] == "PASS" and cold["verdict"] == "FAIL" and cold["first_failure"]["stage"] == "ddr_training", warm["verdict"]
    check("limit: cold-only defect is invisible to a room-temp Day-0", t_latent)
    def t_no_gating_noise():
        res = run_mode(("rail_low",), "no_gating")
        fails = [n for n, r in res.items() if r["status"] == "FAIL"]
        return len(fails) == 9 and fails[0] == "power", str(fails)
    check("limit: without gating one dead rail yields 9 FAILs (8 false)", t_no_gating_noise)

    print("%d/%d passed" % (PASSED, TOTAL))
    return PASSED == TOTAL


if __name__ == "__main__":
    if "--demo" in sys.argv:
        demo()
        sys.exit(0)
    sys.exit(0 if tests() else 1)
