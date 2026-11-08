#!/usr/bin/env python3
"""HWLAB-B02 추가 연습 — 같은 parts.json / board.json 으로

  연습 1  series_r_for_phantom()   : 꺼진 레일 phantom 전압을 v_lim 이하로 만드는 직렬 R, 그 대가(RC 상승 시간)
  연습 2  divider_check()          : 3.3V → 1.8V 저항 분압기의 worst-case (abs max, NMH/NML, Thevenin R)
  연습 3  sequence_hazards()       : 전원 시퀀스(레일 on/off 타임라인)에서 back-powering 구간 찾기
  연습 4  max_load_ma()            : NMH >= MIN_NM 을 지키는 최대 부하 전류 (선형 R_out 외삽 여부 표시)

  python3 -I practice.py           # 테스트 ("N/M passed", 실패 시 exit 1)
"""
import itertools
import json
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MIN_NM_V = 0.10
VF_ESD = 0.6
_LEVEL = re.compile(r"^(?:(\d+(?:\.\d+)?)\*)?VDD(?:([+-])(\d+(?:\.\d+)?))?$")


def load(name):
    with open(os.path.join(HERE, name), encoding="utf-8") as f:
        return json.load(f)


# ---- 본문(solution.py) Part A/B 의 도구를 축약해 넣었다 (레슨 파일끼리 import 하지 않는다) ----
def eval_level(spec, vdd):
    if isinstance(spec, (int, float)) and not isinstance(spec, bool):
        return float(spec)
    s = str(spec).replace(" ", "")
    m = _LEVEL.match(s)
    if not m:
        return float(s)
    k = float(m.group(1)) if m.group(1) else 1.0
    off = float(m.group(3)) if m.group(3) else 0.0
    return k * vdd + (-off if m.group(2) == "-" else off)


def out_level(points, i_load_ma, vdd, high):
    vals = [eval_level(e, vdd) for e, i_test in points if i_test >= i_load_ma]
    if not vals:
        return None
    return max(vals) if high else min(vals)


def r_out_ohm(part, vdd):
    return min((vdd - eval_level(e, vdd)) / (i / 1000.0) for e, i in part["voh"])


def inject(v_src, r_src, v_rail, r_rail, vf=VF_ESD):
    i = (v_src - vf - v_rail) / (r_src + r_rail)
    if i <= 0:
        return 0.0, v_rail
    return i * 1000.0, v_rail + i * r_rail


def corners(rails, names):
    uniq = sorted(set(names))
    for combo in itertools.product(("min", "max"), repeat=len(uniq)):
        yield {n: rails[n][c] for n, c in zip(uniq, combo)}


# ---------------------------------------------------------------- 연습 1
def series_r_for_phantom(v_drv, r_out, r_off, v_lim, vf=VF_ESD):
    """phantom = (v_drv - vf) * r_off / (r_out + Rs + r_off) <= v_lim 을 Rs 로 푼다."""
    return (v_drv - vf) * r_off / v_lim - r_out - r_off


def rise_10_90_ns(r_ohm, c_pf):
    """단일 RC: 10-90% = ln(9) * RC ≈ 2.197 RC"""
    return math.log(9.0) * r_ohm * c_pf * 1e-3


# ---------------------------------------------------------------- 연습 2
def divider_check(rt, rb, tol, drv, rcv, rails, rd, rr, c_pf=10.0):
    """드라이버 → [rt] → 노드 → [rb] → GND, 노드 = 리시버 입력. 저항 공차 tol(예 0.01) 까지 worst-case."""
    k_max = rb * (1 + tol) / (rt * (1 - tol) + rb * (1 + tol))
    k_min = rb * (1 - tol) / (rt * (1 + tol) + rb * (1 - tol))
    vd_min, vd_max = rails[rd]["min"], rails[rd]["max"]
    vr_min, vr_max = rails[rr]["min"], rails[rr]["max"]
    i_div_ma = vd_max / ((rt + rb) * (1 - tol)) * 1000.0
    voh = out_level(drv["voh"], i_div_ma, vd_min, True)
    vol = out_level(drv["vol"], i_div_ma, vd_max, False)
    v_node_max = vd_max * k_max                      # High 는 무부하에서 VDD 까지
    abs_max = eval_level(rcv["vin_abs_max"], vr_min)
    r_th = rt * rb / (rt + rb)
    return {
        "k_min": k_min, "k_max": k_max, "i_div_ma": i_div_ma,
        "v_node_max": v_node_max, "abs_max": abs_max, "abs_ok": v_node_max <= abs_max,
        "nmh": voh * k_min - eval_level(rcv["vih"], vr_max),
        "nml": eval_level(rcv["vil"], vr_min) - vol * k_max,
        "r_th": r_th, "t_rise_ns": rise_10_90_ns(r_th, c_pf),
    }


# ---------------------------------------------------------------- 연습 3
def sequence_hazards(board, parts, events):
    """events: [(t_ms, rail, "on"|"off")] 시간순. 처음엔 모든 레일 OFF.
    direct 링크에서 '드라이버 레일 ON & 리시버 레일 OFF & 리시버가 Ioff 아님 & ESD 다이오드 있음' 인 구간.
    반환 [(link_id, t_start, t_end | None)] (None = 끝까지)."""
    on = {r: False for r in board["rails"]}
    open_at = {}
    out = []
    times = sorted(set(t for t, _, _ in events))

    def hazard(lk):
        rcv = parts[lk["receiver"]["part"]]
        return (on[lk["driver"]["rail"]] and not on[lk["receiver"]["rail"]]
                and not rcv["ioff"] and rcv["esd_diode_to_vdd"])

    for t in times:
        for tt, rail, st in events:
            if tt == t:
                on[rail] = (st == "on")
        for lk in board["links"]:
            if lk.get("topology", "direct") != "direct":
                continue
            h = hazard(lk)
            if h and lk["id"] not in open_at:
                open_at[lk["id"]] = t
            elif not h and lk["id"] in open_at:
                out.append((lk["id"], open_at.pop(lk["id"]), t))
    for lid, t0 in open_at.items():
        out.append((lid, t0, None))
    return sorted(out, key=lambda x: (x[0], x[1]))


# ---------------------------------------------------------------- 연습 4
def max_load_ma(drv, rcv, rails, rd, rr):
    """NMH >= MIN_NM_V 를 지키는 최대 High 부하 전류. 반환 (I_max mA | None, 외삽 여부).
    ① IOH 시험점(가장 큰 전류)의 보장 VOH 로 worst 코너 NMH 를 먼저 본다 — 여기서 이미 MIN 미만이면 None
       (데이터시트로는 어떤 부하에서도 보장 못 함).
    ② 그 너머는 그 점을 지나는 선형 모델 VOH(I) = VDD - I*R_out 로 외삽 → 시험 전류보다 크면 '외삽' 표시."""
    i_spec = max(i for _, i in drv["voh"])
    best = math.inf
    for c in corners(rails, [rd, rr]):
        vd, vr = c[rd], c[rr]
        vih = eval_level(rcv["vih"], vr)
        if out_level(drv["voh"], i_spec, vd, True) - vih < MIN_NM_V:
            return None, False
        best = min(best, (vd - vih - MIN_NM_V) / r_out_ohm(drv, vd) * 1000.0)
    return best, best > i_spec


# ---------------------------------------------------------------- 하네스
g_pass = 0
g_total = 0


def check(name, fn):
    global g_pass, g_total
    g_total += 1
    try:
        ok = bool(fn())
        why = ""
    except Exception as e:  # noqa: BLE001
        ok, why = False, "  (%s: %s)" % (type(e).__name__, e)
    if ok:
        g_pass += 1
        print("PASS  %s" % name)
    else:
        print("FAIL  %s%s" % (name, why))


def near(x, ref, tol=1e-3):
    ok = x is not None and abs(x - ref) <= tol
    if not ok:
        print("      got %r, expected %r +/- %g" % (x, ref, tol))
    return ok


def main():
    parts = load("parts.json")["parts"]
    board = load("board.json")
    rails = board["rails"]
    MCU, SOC, GNSS = parts["MCU33_GPIO"], parts["SOC18_GPIO"], parts["GNSS_IO"]

    print("-- 연습 1: series R vs phantom rail --")
    rs = series_r_for_phantom(3.465, 50.0, 1000.0, 0.3)
    check("Rs for phantom <= 0.3V = 8500 ohm", lambda: near(rs, 8500.0, 0.5))
    check("inject with Rs=8500 -> phantom 0.300V, 0.300mA",
          lambda: near(inject(3.465, 50.0 + rs, 0.0, 1000.0)[1], 0.3) and near(inject(3.465, 50.0 + rs, 0.0, 1000.0)[0], 0.3))
    t_r = rise_10_90_ns(rs + 50.0, 15.0)
    print("      Rs=%.0f ohm, C=15pF -> t_r(10-90) = %.1f ns; UART 115200 bit = %.0f ns" % (rs, t_r, 1e9 / 115200))
    check("t_r(10-90) with 8.55k x 15pF = 281.8ns (< 5% of 115200 baud bit)",
          lambda: near(t_r, 281.8, 0.1) and t_r < 0.05 * 1e9 / 115200)

    print("-- 연습 2: resistor divider 3.3V -> 1.8V --")
    bad = divider_check(1000, 2000, 0.01, MCU, SOC, rails, "VDD_3V3", "VDD_1V8")
    good = divider_check(1000, 1200, 0.01, MCU, SOC, rails, "VDD_3V3", "VDD_1V8")
    for nm, d in (("1k/2k", bad), ("1k/1.2k", good)):
        print("      %-8s Vnode_max %.3fV (abs %.3fV) NMH %+.3f NML %+.3f Rth %.0f ohm t_r %.1fns @10pF"
              % (nm, d["v_node_max"], d["abs_max"], d["nmh"], d["nml"], d["r_th"], d["t_rise_ns"]))
    check("1k/2k: node 2.325V > abs max 2.010V -> not ok",
          lambda: near(bad["v_node_max"], 2.3254) and not bad["abs_ok"])
    check("1k/1.2k: node 1.907V ok, NMH +0.250, NML +0.378",
          lambda: good["abs_ok"] and near(good["v_node_max"], 1.9072) and near(good["nmh"], 0.2498)
          and near(good["nml"], 0.3783))
    check("1k/1.2k: Rth 545 ohm, t_r 12.0ns @10pF", lambda: near(good["r_th"], 545.45, 0.01) and near(good["t_rise_ns"], 11.98, 0.01))

    print("-- 연습 3: power sequencing hazards --")
    events = [(0, "VDD_3V3", "on"), (0, "VDD_3V0", "on"), (2, "VDD_1V8", "on"), (3, "VDD_3V3_SNS", "on"),
              (5, "VDD_3V3_GNSS", "on"), (100, "VDD_3V3_GNSS", "off")]
    hz = sequence_hazards(board, parts, events)
    for h in hz:
        print("      %s back-power window %s ms .. %s" % (h[0], h[1], "end" if h[2] is None else "%s ms" % h[2]))
    check("hazards = L1[0,2) L4[0,5) L4[100,-) L6[0,3)",
          lambda: hz == [("L1", 0, 2), ("L4", 0, 5), ("L4", 100, None), ("L6", 0, 3)])
    check("L5 (Ioff buffer) never appears", lambda: all(h[0] != "L5" for h in hz))
    hz2 = sequence_hazards(board, parts, [(0, "VDD_1V8", "on"), (0, "VDD_3V0", "on"), (0, "VDD_3V3_SNS", "on"),
                                          (1, "VDD_3V3_GNSS", "on"), (1, "VDD_3V3", "on")])
    for h in hz2:
        print("      (reversed) %s back-power window %s ms .. %s ms" % h)
    check("receivers first, MCU rail last -> hazard moves to the reverse link L2[0,1)",
          lambda: hz2 == [("L2", 0, 1)])

    print("-- 연습 4: max load current for NMH >= 0.1V --")
    i4, ex4 = max_load_ma(MCU, GNSS, rails, "VDD_3V3", "VDD_3V3_GNSS")
    i1, ex1 = max_load_ma(SOC, SOC, rails, "VDD_1V8", "VDD_1V8")
    i6, ex6 = max_load_ma(MCU, parts["SENS33_IO"], rails, "VDD_3V0", "VDD_3V3_SNS")
    print("      MCU->GNSS %.2fmA (extrapolated=%s), SoC->SoC %.3fmA (extrapolated=%s), L6 %s"
          % (i4, ex4, i1, ex1, i6))
    check("MCU->GNSS: 12.19mA but > 8mA spec point -> extrapolated", lambda: near(i4, 12.19, 0.01) and ex4)
    check("SoC->SoC same rail: 2.216mA > 2mA -> extrapolated", lambda: near(i1, 2.2156, 0.001) and ex1)
    check("L6 (NMH at the 8mA spec point already < 0.1V) -> None", lambda: i6 is None)

    print("%d/%d passed" % (g_pass, g_total))
    return 0 if g_pass == g_total else 1


if __name__ == "__main__":
    sys.exit(main())
