#!/usr/bin/env python3
"""SIV-A06 합성 로그 생성기 (결정적, Python 3.9 stdlib).

보드: SENSE-EVB rev A (가상). 레일/타이밍은 모두 대표값(가정)이며 어떤 실제 제품의 스펙도 아니다.
출력(이 파일 옆):
  board_*.csv       DAQ 내보내기 형식: t_us,<rail>... (Ts=10 us, 1200행)
  clock_meas.json   보드별 주파수 카운터 읽음값(gate_s, timebase_ppm, counts)
  truth.json        심어 넣은 결함의 정답(체커는 이 값으로만 채점된다)
실행: python3 -I gen_rail_logs.py
"""
import json
import math
import os
import random
from fractions import Fraction

HERE = os.path.dirname(os.path.abspath(__file__))
TS_US = 10.0
ROWS = 1200

# name, nom, parent, t_on, T_ramp(0->100% 선형 soft-start), t_off, tau_off
BASE_RAILS = [
    ("VIN_12V", 12.00, 200.0, 600.0, 11200.0, 120.0),
    ("VDD_0V8", 0.80, 1100.0, 400.0, 10400.0, 100.0),
    ("VDD_1V8", 1.80, 1900.0, 400.0, 9600.0, 100.0),
    ("VDD_1V1", 1.10, 2700.0, 400.0, 8800.0, 100.0),
    ("VDD_3V3", 3.30, 3500.0, 400.0, 8000.0, 100.0),
]
OVERSHOOT = 0.025      # ramp 끝 overshoot 비율
OVERSHOOT_TAU = 40.0   # us
JITTER_US = 0.1        # 타임스탬프 지터(1 sigma)

CLOCK_TREE = {
    "XO_24M": {"hz": 24000000},
    "PLL_CPU": {"parent": "XO_24M", "mul": 100, "div": 1},
    "CPU_CLK": {"parent": "PLL_CPU", "div": 2},
    "PLL_DDR": {"parent": "XO_24M", "mul": 88, "div": 1},
    "DDR_CK": {"parent": "PLL_DDR", "div": 2},
    "PLL_SYS": {"parent": "XO_24M", "mul": 125, "div": 1},
    "AXI_CLK": {"parent": "PLL_SYS", "div": 6},
    "APB_CLK": {"parent": "PLL_SYS", "div": 30},
    "PCIE_REF": {"parent": "PLL_SYS", "div": 30},
    "PLL_AUD": {"parent": "XO_24M", "mul": 40, "frac": 16106127, "frac_bits": 24, "div": 1},
    "AUD_MCLK": {"parent": "PLL_AUD", "div": 80},
}
MEASURED = ["XO_24M", "CPU_CLK", "DDR_CK", "AXI_CLK", "APB_CLK", "PCIE_REF", "AUD_MCLK"]


def wave(t, nom, t_on, t_ramp, t_off, tau, glitches, dc_scale):
    """레일의 '진짜' 전압. 선형 soft-start + 감쇠 overshoot, 지수 방전, 사각 glitch."""
    lv = nom * dc_scale
    if t < t_on:
        return 0.0
    if t < t_on + t_ramp:
        v = lv * (t - t_on) / t_ramp
    else:
        v = lv + lv * OVERSHOOT * math.exp(-(t - t_on - t_ramp) / OVERSHOOT_TAU)
    if t >= t_off:
        v = lv * math.exp(-(t - t_off) / tau)
    for t0, w, d in glitches:
        if t0 <= t < t0 + w:
            v = lv * (1.0 - d)
    return v


def noise_sigma(nom):
    return max(0.002, nom * 0.002)


def write_log(name, seed, mods=None, stamp=None):
    """mods: rail -> dict(t_on,t_ramp,t_off,tau,glitches,dc_scale)."""
    mods = mods or {}
    rng = random.Random(seed)
    rows = []
    for k in range(ROWS):
        t = k * TS_US
        row = [t + rng.gauss(0.0, JITTER_US)]
        for rn, nom, t_on, t_ramp, t_off, tau in BASE_RAILS:
            m = mods.get(rn, {})
            v = wave(t, nom, m.get("t_on", t_on), m.get("t_ramp", t_ramp),
                     m.get("t_off", t_off), m.get("tau", tau),
                     m.get("glitches", []), m.get("dc_scale", 1.0))
            v += rng.gauss(0.0, noise_sigma(nom))
            row.append(max(0.0, v))
        rows.append(row)
    if stamp:
        stamp(rows)
    path = os.path.join(HERE, name + ".csv")
    with open(path, "w", newline="\n") as f:
        f.write("t_us," + ",".join(r[0] for r in BASE_RAILS) + "\n")
        for row in rows:
            f.write("%.1f," % row[0] + ",".join("%.3f" % x for x in row[1:]) + "\n")
    return path


def clock_expected(tree):
    memo = {}

    def go(n):
        if n in memo:
            return memo[n]
        nd = tree[n]
        if "hz" in nd:
            f = Fraction(nd["hz"])
        else:
            frac = Fraction(nd.get("frac", 0), 1 << nd.get("frac_bits", 0)) if nd.get("frac") else 0
            f = go(nd["parent"]) * (nd.get("mul", 1) + frac) / nd["div"]
        memo[n] = f
        return f

    return {n: go(n) for n in tree}


def clock_board(rng, xo_ppm=0.0, gate_s=1.0, overrides=None):
    tree = json.loads(json.dumps(CLOCK_TREE))
    tree["XO_24M"]["hz"] = Fraction(24000000) * (1 + Fraction(int(round(xo_ppm * 1000)), 10 ** 9))
    for n, ov in (overrides or {}).items():
        tree[n].update(ov)
    exp = clock_expected(tree)
    counts = {}
    for n in MEASURED:
        f = float(exp[n])
        counts[n] = int(math.floor(f * gate_s + rng.random()))
    return {"gate_s": gate_s, "timebase_ppm": 0.1, "counts": counts}


def edge(on_or_off, t_ref, t_ramp, tau, frac):
    """soft-start 또는 지수 방전의 frac 레벨 통과 시각(참값)."""
    if on_or_off == "on":
        return t_ref + frac * t_ramp
    return t_ref + tau * math.log(1.0 / frac)


def main():
    truth = {"Ts_us": TS_US, "logs": {}, "clock": {}}
    write_log("board_good", 1001)
    truth["logs"]["board_good"] = {"status": "PASS"}

    # 1) 순서 위반: PMIC OTP 오류로 3V3이 1V1보다 먼저 올라옴
    t_on_33 = 2300.0
    write_log("board_bad_order", 1002, {"VDD_3V3": {"t_on": t_on_33}})
    delta = edge("on", t_on_33, 400.0, 100.0, 0.1) - edge("on", 2700.0, 400.0, 100.0, 0.9)
    truth["logs"]["board_bad_order"] = {
        "status": "FAIL", "check": "order_on", "a": "VDD_1V1", "b": "VDD_3V3", "delta_us": delta}

    # 2) 느린 ramp: 3V3 soft-start 커패시터 값 오류 (0->100% 1800us -> 10-90% 1440us)
    write_log("board_bad_ramp", 1003, {"VDD_3V3": {"t_ramp": 1800.0}})
    truth["logs"]["board_bad_ramp"] = {
        "status": "FAIL", "check": "ramp", "rail": "VDD_3V3", "ramp_us": 0.8 * 1800.0}

    # 3) brownout: 1V8에 12% / 80us 강하, 자식 1V1은 20us 뒤 8% / 60us
    write_log("board_bad_brownout", 1004, {
        "VDD_1V8": {"glitches": [(6000.0, 80.0, 0.12)]},
        "VDD_1V1": {"glitches": [(6020.0, 60.0, 0.08)]},
    })
    truth["logs"]["board_bad_brownout"] = {
        "status": "FAIL", "check": "glitch", "primary": "VDD_1V8", "secondary": "VDD_1V1",
        "t0_us": 6000.0, "width_us": 80.0, "depth_pct": 12.0}

    # 4) 전원 차단 순서 위반: 0V8이 1V8보다 먼저 내려감
    write_log("board_bad_powerdown", 1005, {"VDD_0V8": {"t_off": 9000.0}})
    delta = edge("off", 9000.0, 0, 100.0, 0.9) - edge("off", 9600.0, 0, 100.0, 0.1)
    truth["logs"]["board_bad_powerdown"] = {
        "status": "FAIL", "check": "order_off", "a": "VDD_1V8", "b": "VDD_0V8", "delta_us": delta}

    # 5) DC 오차: 1V1 레귤레이터 FB 저항 값 오류 -> +6.4%
    write_log("board_bad_dc", 1006, {"VDD_1V1": {"dc_scale": 1.17 / 1.10}})
    truth["logs"]["board_bad_dc"] = {
        "status": "FAIL", "check": "dc", "rail": "VDD_1V1", "dc_v": 1.17}

    # 6) 측정 자체가 망가진 로그: 전압은 정상, 타임스탬프가 뒤로 감 + 중복 (DUT 불량이 아님)
    def bad_stamp(rows):
        for k in range(300, 330):
            rows[k][0] -= 500.0
        rows[700][0] = rows[699][0]

    write_log("board_bad_timestamps", 1007, stamp=bad_stamp)
    truth["logs"]["board_bad_timestamps"] = {"status": "INVALID"}

    rng = random.Random(2001)
    boards = {
        "good": clock_board(rng, xo_ppm=8.0),
        "bad_pll_ddr": clock_board(rng, xo_ppm=8.0, overrides={"PLL_DDR": {"mul": 87}}),
        "bad_xo": clock_board(rng, xo_ppm=120.0),
        "bad_apb_div": clock_board(rng, xo_ppm=-5.0, overrides={"APB_CLK": {"div": 32}}),
        "short_gate": clock_board(rng, xo_ppm=8.0, gate_s=0.001),
    }
    truth["clock"] = {
        "good": {"status": "PASS", "faults": []},
        "bad_pll_ddr": {"status": "FAIL", "faults": [["PLL_DDR", "87/88"]]},
        "bad_xo": {"status": "FAIL", "faults": [["XO_24M", None]]},
        "bad_apb_div": {"status": "FAIL", "faults": [["APB_CLK", "15/16"]]},
        "short_gate": {"status": "INDET", "faults": []},
    }
    with open(os.path.join(HERE, "clock_meas.json"), "w", newline="\n") as f:
        json.dump(boards, f, indent=1, sort_keys=True)
        f.write("\n")
    with open(os.path.join(HERE, "truth.json"), "w", newline="\n") as f:
        json.dump(truth, f, indent=1, sort_keys=True)
        f.write("\n")
    print("wrote %d logs, clock_meas.json, truth.json" % len(truth["logs"]))


if __name__ == "__main__":
    main()
