#!/usr/bin/env python3
"""HWLAB-B02 데이터시트 로직 레벨: 노이즈 마진과 전압 도메인 호환성

parts.json (데이터시트 표: VIH/VIL, VOH/VOL@IOH/IOL, 절대최대정격, ESD 다이오드, Ioff) 과
board.json (보드 레일 nom/min/max + 6개 인터페이스) 를 읽어
  Part A  레벨 식 계산, 부하 전류에서 보장되는 VOH/VOL 선택, DC 노이즈 마진 NMH/NML
  Part B  레일 코너 스윕(같은 레일은 같이 움직인다), 출력 저항 추정, ESD 클램프 주입 전류 / back-powering
  Part C  인터페이스별 판정 (직결 / open-drain FET 레벨시프터 / 전원 꺼진 리시버) 과 보드 리포트

  python3 -I solution.py            # 테스트 ("N/M passed", 실패 시 exit 1)
  python3 -I solution.py --report   # 테스트 + RB-A0 보드 리포트   (starter.py 도 같은 방식)

Python 3 표준 라이브러리만. 단위: 전압 V, 전류 mA, 저항 Ω.
"""
import copy
import itertools
import json
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MIN_NM_V = 0.10     # 팀 정책: DC 노이즈 마진 하한 (JEDEC 이 정한 값이 아니다)
VF_ESD = 0.6        # ESD 클램프 다이오드 순방향 전압 가정 (mA 영역)
RANK = {"PASS": 0, "MARGINAL": 1, "FAIL": 2}
_LEVEL = re.compile(r"^(?:(\d+(?:\.\d+)?)\*)?VDD(?:([+-])(\d+(?:\.\d+)?))?$")


def load(name):
    with open(os.path.join(HERE, name), encoding="utf-8") as f:
        return json.load(f)


# ---------------------------------------------------------------- Part A: 레벨과 노이즈 마진
def eval_level(spec, vdd):
    """레벨 스펙 → 전압. 2.0 / "0.45" / "0.65*VDD" / "VDD-0.45" / "0.7*VDD+0.1". 그 외 → ValueError"""
    # TODO: 숫자면 float. 문자열은 공백 제거 후 'k*VDD', 'VDD±x', 'k*VDD±x', 순수 숫자를 처리한다.
    #       알 수 없는 식(예: 'VCC-0.2')·nan/inf·bool → ValueError.
    return 0.0


def out_level(points, i_load_ma, vdd, high):
    """출력 레벨 점 [[식, 시험전류 mA], ...] 에서 부하 전류 i_load_ma 일 때 '보장되는' 값.
    시험 전류 >= 부하 전류인 점만 유효 (VOH 는 전류가 클수록 낮아지고 VOL 은 높아진다).
    high=True(VOH) → 유효 점 중 최댓값, False(VOL) → 최솟값. 유효 점이 없으면 None."""
    # TODO: 시험 전류 >= 부하 전류인 점만 골라 eval_level 로 계산. VOH 는 max, VOL 은 min. 없으면 None.
    return None


def margins(drv, rcv, vdd_drv, vdd_rcv, i_load_ma):
    """push-pull 드라이버 → 리시버 (NMH, NML). NMH = VOH - VIH, NML = VIL - VOL. 미보장 → None"""
    # TODO: out_level 로 VOH/VOL(부하 전류 기준), eval_level 로 VIH/VIL(리시버 VDD 기준).
    return None


def classify(margin):
    """None 또는 음수 → FAIL, MIN_NM_V 미만 → MARGINAL, 그 이상 → PASS"""
    # TODO: 정책 MIN_NM_V 로 3단계 판정
    return "PASS"


# ---------------------------------------------------------------- Part B: 코너, 클램프, back-powering
def corners(rails, names):
    """레일 이름들의 min/max 모든 조합 → {rail: V}. 같은 이름은 한 번만 흔든다 (같은 레일 = 같은 전압)."""
    # TODO: 중복 제거한 레일 이름마다 ("min", "max") 를 고르는 모든 조합을 yield. (itertools.product)
    return iter(())


def worst_margins(fn, rails, rail_d, rail_r, correlated=True):
    """fn(vd, vr) -> (nmh, nml) | None 을 모든 코너에서 돌려 최솟값을 찾는다.
    반환 {"nmh", "nml", "at_h", "at_l"} (at_* = 그 최솟값이 나온 (Vdrv, Vrcv)). 한 코너라도 None → nmh/nml None.
    correlated=False 는 같은 레일도 독립으로 흔드는 '틀린' 방법 (비교용)."""
    # TODO: correlated 면 corners(rails, [rail_d, rail_r]) 로, 아니면 두 레일을 독립으로 min/max 조합.
    #       각 (vd, vr) 에서 fn 을 불러 nmh/nml 최솟값과 그 코너를 기록. None 이 하나라도 나오면 None.
    return {"nmh": None, "nml": None, "at_h": None, "at_l": None}


def r_out_ohm(part, vdd):
    """VOH 점들로 High 출력 저항 추정: min over (VDD - VOH)/IOH.
    VOH 는 보장 '하한' 이라 이 R 은 실제의 상한 → 이 R 로 구한 주입 전류는 하한(실제는 더 클 수 있다)."""
    # TODO: VOH 점마다 (vdd - VOH) / (IOH[mA] / 1000) 의 최솟값
    return 0.0


def inject(v_src, r_src, v_rail, r_rail, vf=VF_ESD):
    """드라이버(Thevenin v_src, r_src) → ESD 다이오드(vf) → 리시버 VDD 레일(v_rail, 등가 r_rail).
    켜진 레일 = (V, 0Ω) 이상 소스, 꺼진 레일 = (0V, off_load_ohm).
    반환 (주입 전류 mA, 다이오드 뒤 레일 노드 전압 V). 다이오드가 안 켜지면 (0.0, v_rail)."""
    # TODO: i = (v_src - vf - v_rail) / (r_src + r_rail) [A]. i <= 0 이면 다이오드 꺼짐.
    #       반환 (i * 1000 [mA], v_rail + i * r_rail)
    return 0.0, v_rail


# ---------------------------------------------------------------- Part C: 인터페이스 판정
def finding(res, sev, code, msg):
    res["findings"].append((sev, code, msg))
    if RANK[sev] > RANK[res["status"]]:
        res["status"] = sev


def check_rail_ranges(res, parts, rails, ends):
    for part_name, rail in ends:
        lo, hi = parts[part_name]["vdd_range"]
        r = rails[rail]
        if r["min"] < lo or r["max"] > hi:
            finding(res, "FAIL", "RAIL_RANGE", "%s on %s %.3f-%.3fV outside part range %.2f-%.2fV"
                    % (part_name, rail, r["min"], r["max"], lo, hi))


def add_margins(res, label, nom, w):
    res["dirs"].append({"dir": label, "nom": nom, "worst": w})
    if nom is None or w["nmh"] is None:
        finding(res, "FAIL", "UNSPEC_LOAD", "%s: VOH/VOL not guaranteed at this load current" % label)
        return
    for key, code, idx in (("nmh", "NM_HIGH", 0), ("nml", "NM_LOW", 1)):
        sev = classify(w[key])
        if sev != "PASS":
            vd, vr = w["at_" + key[-1]]
            finding(res, sev, code, "%s %s=%+.3fV at Vdrv=%.3fV Vrcv=%.3fV (nominal %+.3fV)"
                    % (label, key.upper(), w[key], vd, vr, nom[idx]))


def check_overvoltage(res, link, drv, rcv, rails):
    """리시버 전원 ON. 입력 최대 = 드라이버 레일(High 는 무부하에서 VDD 까지 간다) vs 절대최대정격(리시버 레일).
    넘으면 ESD 다이오드로 흐르는 클램프 전류를 추정해 IINJ 와 비교한다."""
    rd, rr = link["driver"]["rail"], link["receiver"]["rail"]
    rs = link.get("r_series_ohm", 0)
    worst_abs, at, worst_i = math.inf, None, 0.0
    for c in corners(rails, [rd, rr]):
        vd, vr = c[rd], c[rr]
        m = eval_level(rcv["vin_abs_max"], vr) - vd
        if m < worst_abs:
            worst_abs, at = m, (vd, vr)
        if rcv["esd_diode_to_vdd"]:
            worst_i = max(worst_i, inject(vd, r_out_ohm(drv, vd) + rs, vr, 0.0)[0])
    res["clamp_ma"] = worst_i
    if worst_abs >= 0:
        return
    vd, vr = at
    msg = "Vin %.3fV > abs max %.3fV (%s=%.3fV)" % (vd, vd + worst_abs, rr, vr)
    if not rcv["esd_diode_to_vdd"]:
        finding(res, "FAIL", "ABS_MAX", msg)
    elif worst_i == 0.0:
        finding(res, "MARGINAL", "ABS_MAX", msg + "; below diode knee (%.1fV), paper violation at opposite rail corners"
                % VF_ESD)
    elif worst_i <= rcv["iinj_max_ma"]:
        finding(res, "MARGINAL", "CLAMP", msg + "; clamp %.2fmA <= IINJ %gmA (relies on ESD clamp, check %s back-feed)"
                % (worst_i, rcv["iinj_max_ma"], rr))
    else:
        finding(res, "FAIL", "ABS_MAX", msg)
        finding(res, "FAIL", "CLAMP", "~%.1fmA into %s via ESD diode > IINJ %gmA" % (worst_i, rr, rcv["iinj_max_ma"]))


def check_power_off(res, link, drv, rcv, rails):
    """리시버 레일이 꺼질 수 있으면, 드라이버가 High(≈VDD) 인 채로 꺼진 칩 입력을 보는 경우를 판정."""
    # TODO: 레일이 switchable 이 아니면 return. 드라이버 레일 max 에서
    #       ioff → abs max(VDD=0) 넘으면 FAIL ABS_MAX_OFF / ESD 다이오드 → inject(.., 0V, off_load_ohm) 로
    #       res["backpower"] = (mA, V) 저장 후 FAIL BACKPOWER / 둘 다 아니면 MARGINAL OFF_UNSPEC.
    return


def od_fet_dir(drv, rcv, side_d, side_r, fet, vd, vr):
    """FET 레벨시프터에서 side_d 가 Low 로 끌 때 side_r 에서 본 (NMH, NML).
    풀업은 각자 자기 레일 → High = Vr. 드라이버 sink 전류 = 양쪽 풀업 전류 합.
    리시버 쪽 Low = VOL + (리시버 쪽 풀업 전류) x Rds_on. VOL 미보장 → None."""
    # TODO: i_d = vd/Rpu_d, i_r = vr/Rpu_r. VOL 은 (i_d+i_r) mA 에서. 리시버 Low = VOL + i_r*Rds_on.
    #       High = vr (리시버 쪽 풀업 레일). (NMH, NML) 반환.
    return None


def check_link(link, parts, rails):
    topo = link.get("topology", "direct")
    res = {"id": link["id"], "net": link["net"], "topology": topo, "status": "PASS", "findings": [], "dirs": []}
    if topo == "direct":
        dp, rp = link["driver"]["part"], link["receiver"]["part"]
        rd, rr = link["driver"]["rail"], link["receiver"]["rail"]
        drv, rcv = parts[dp], parts[rp]
        check_rail_ranges(res, parts, rails, [(dp, rd), (rp, rr)])
        i = link.get("i_load_ma", 0.1)
        nom = margins(drv, rcv, rails[rd]["nom"], rails[rr]["nom"], i)
        w = worst_margins(lambda vd, vr: margins(drv, rcv, vd, vr, i), rails, rd, rr)
        add_margins(res, "%s->%s" % (dp, rp), nom, w)
        check_overvoltage(res, link, drv, rcv, rails)
        check_power_off(res, link, drv, rcv, rails)
    elif topo == "od_fet":
        a, b, fet = link["a"], link["b"], link["fet"]
        check_rail_ranges(res, parts, rails, [(a["part"], a["rail"]), (b["part"], b["rail"])])
        lo = a if rails[a["rail"]]["nom"] <= rails[b["rail"]]["nom"] else b
        gate_min = rails[lo["rail"]]["min"]
        res["fet_overdrive"] = gate_min - fet["vgs_th_max"]
        if res["fet_overdrive"] <= 0:
            finding(res, "FAIL", "FET_VGS", "gate rail %s min %.3fV <= Vgs(th)max %.2fV: FET never turns on"
                    % (lo["rail"], gate_min, fet["vgs_th_max"]))
        elif gate_min < fet["rds_on_vgs"]:
            finding(res, "MARGINAL", "FET_RDS", "Rds_on spec'd at Vgs=%.2fV but gate rail min is %.3fV"
                    % (fet["rds_on_vgs"], gate_min))
        for sd, sr in ((a, b), (b, a)):
            pd, pr = parts[sd["part"]], parts[sr["part"]]
            fn = (lambda pd_, pr_, sd_, sr_: lambda vd, vr: od_fet_dir(pd_, pr_, sd_, sr_, fet, vd, vr))(pd, pr, sd, sr)
            nom = fn(rails[sd["rail"]]["nom"], rails[sr["rail"]]["nom"])
            w = worst_margins(fn, rails, sd["rail"], sr["rail"])
            add_margins(res, "%s->%s" % (sd["part"], sr["part"]), nom, w)
    else:
        raise ValueError("unknown topology %r" % topo)
    return res


def _fmt(x):
    return "   n/a" if x is None else "%+.3f" % x


def board_report(board, parts):
    rails = board["rails"]
    results = [check_link(lk, parts, rails) for lk in board["links"]]
    out = ["%s - logic-level audit (MIN_NM=%.2fV, VF_ESD=%.1fV, worst = rail corner sweep)"
           % (board["board"], MIN_NM_V, VF_ESD),
           "%-3s %-12s %-7s %-26s %-15s %-15s %s" % ("id", "net", "topo", "direction", "NMH nom/worst",
                                                    "NML nom/worst", "status")]
    for r in results:
        for k, d in enumerate(r["dirs"]):
            nom, w = d["nom"], d["worst"]
            out.append("%-3s %-12s %-7s %-26s %s/%s  %s/%s  %s" % (
                r["id"] if k == 0 else "", r["net"] if k == 0 else "", r["topology"] if k == 0 else "",
                d["dir"], _fmt(nom and nom[0]), _fmt(w["nmh"]), _fmt(nom and nom[1]), _fmt(w["nml"]),
                r["status"] if k == 0 else ""))
        for sev, code, msg in r["findings"]:
            out.append("    %-8s %-11s %s" % (sev, code, msg))
    cnt = {s: sum(1 for r in results if r["status"] == s) for s in RANK}
    out.append("%d links: %d PASS, %d MARGINAL, %d FAIL"
               % (len(results), cnt["PASS"], cnt["MARGINAL"], cnt["FAIL"]))
    return results, "\n".join(out)


# ---------------------------------------------------------------- 테스트 하네스
g_pass = 0
g_total = 0


def check(name, fn):
    """fn() 이 True 면 PASS. 예외도 FAIL 로 센다 (starter 의 미구현 함수가 하네스를 죽이지 않게)."""
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
    """x 가 ref ± tol 안인가. 측정값을 같이 찍어 FAIL 때 무엇이 나왔는지 보이게 한다."""
    ok = x is not None and abs(x - ref) <= tol
    if not ok:
        print("      got %r, expected %r +/- %g" % (x, ref, tol))
    return ok


def raises(exc, fn):
    try:
        fn()
    except exc:
        return True
    return False


def codes(res):
    return [c for _, c, _ in res["findings"]]


def run_tests():
    parts = load("parts.json")["parts"]
    board = load("board.json")
    rails = board["rails"]
    SOC, MCU, SENS = parts["SOC18_GPIO"], parts["MCU33_GPIO"], parts["SENS33_IO"]

    print("-- Part A: levels & noise margin --")
    check("eval_level('0.65*VDD', 1.8) = 1.170", lambda: near(eval_level("0.65*VDD", 1.8), 1.17))
    check("eval_level('VDD-0.45', 1.8) = 1.350", lambda: near(eval_level("VDD-0.45", 1.8), 1.35))
    check("eval_level('VDD+0.3', 0.0) = 0.300 (unpowered)", lambda: near(eval_level("VDD+0.3", 0.0), 0.3))
    check("eval_level(2.0) / '0.45' are absolute",
          lambda: eval_level(2.0, 3.3) == 2.0 and eval_level("0.45", 1.8) == 0.45)
    check("eval_level('0.7*VDD + 0.1', 3.3) = 2.410", lambda: near(eval_level("0.7*VDD + 0.1", 3.3), 2.41))
    check("eval_level('VCC-0.2') / 'nan' -> ValueError",
          lambda: raises(ValueError, lambda: eval_level("VCC-0.2", 1.8))
          and raises(ValueError, lambda: eval_level("nan", 1.8)))
    check("SoC VOH @0.1mA, 1.8V = 1.600 (VDD-0.2 point)",
          lambda: near(out_level(SOC["voh"], 0.1, 1.8, True), 1.6))
    check("SoC VOH @1mA, 1.8V = 1.350 (only the 2mA point covers 1mA)",
          lambda: near(out_level(SOC["voh"], 1.0, 1.8, True), 1.35))
    check("SoC VOH @3mA -> None (beyond every IOH test point)",
          lambda: out_level(SOC["voh"], 3.0, 1.8, True) is None)
    check("SoC VOL @0.05mA = 0.200 (best of 0.2/0.45)",
          lambda: near(out_level(SOC["vol"], 0.05, 1.8, False), 0.2))
    check("JESD8-7 style SoC->SoC @1.8V: NMH = NML = 0.430",
          lambda: near(margins(SOC, SOC, 1.8, 1.8, 0.1)[0], 0.43) and near(margins(SOC, SOC, 1.8, 1.8, 0.1)[1], 0.43))
    check("MCU->MCU @3.3V: NMH 0.900, NML 0.400",
          lambda: near(margins(MCU, MCU, 3.3, 3.3, 0.1)[0], 0.9) and near(margins(MCU, MCU, 3.3, 3.3, 0.1)[1], 0.4))
    check("SoC(1.8)->MCU(3.3) nominal NMH = -0.400 (1.6V < VIH 2.0V)",
          lambda: near(margins(SOC, MCU, 1.8, 3.3, 0.1)[0], -0.4))
    check("margins at 5mA load -> None", lambda: margins(SOC, SOC, 1.8, 1.8, 5.0) is None)
    check("classify: -0.01 FAIL, 0.05 MARGINAL, 0.10 PASS, None FAIL",
          lambda: [classify(x) for x in (-0.01, 0.05, 0.10, None)] == ["FAIL", "MARGINAL", "PASS", "FAIL"])

    print("-- Part B: corners, clamp, back-powering --")
    check("corners(two rails) = 4, corners(same rail twice) = 2",
          lambda: len(list(corners(rails, ["VDD_1V8", "VDD_3V3"]))) == 4
          and len(list(corners(rails, ["VDD_1V8", "VDD_1V8"]))) == 2)
    f6 = lambda vd, vr: margins(MCU, SENS, vd, vr, 0.1)  # noqa: E731
    check("L6 nominal NMH = +0.290 (looks fine)", lambda: near(f6(3.0, 3.3)[0], 0.29))
    check("L6 worst NMH = -0.045 at VDD_3V0=2.85, VDD_3V3_SNS=3.564",
          lambda: near(worst_margins(f6, rails, "VDD_3V0", "VDD_3V3_SNS")["nmh"], -0.0448)
          and worst_margins(f6, rails, "VDD_3V0", "VDD_3V3_SNS")["at_h"] == (2.85, 3.564))
    fss = lambda vd, vr: margins(SOC, SOC, vd, vr, 2.0)  # noqa: E731
    check("same rail @2mA, correlated: NMH = 0.35*1.71-0.45 = +0.1485 (PASS)",
          lambda: near(worst_margins(fss, rails, "VDD_1V8", "VDD_1V8")["nmh"], 0.1485)
          and classify(worst_margins(fss, rails, "VDD_1V8", "VDD_1V8")["nmh"]) == "PASS")
    check("same rail @2mA, independent (wrong): +0.0315 -> false MARGINAL",
          lambda: near(worst_margins(fss, rails, "VDD_1V8", "VDD_1V8", correlated=False)["nmh"], 0.0315)
          and classify(worst_margins(fss, rails, "VDD_1V8", "VDD_1V8", correlated=False)["nmh"]) == "MARGINAL")
    check("worst_margins -> None when a corner is unspecified (5mA)",
          lambda: worst_margins(lambda vd, vr: margins(SOC, SOC, vd, vr, 5.0), rails, "VDD_1V8", "VDD_1V8")["nmh"] is None)
    check("r_out MCU = 50 ohm (0.4V/8mA), SoC = 225 ohm (0.45V/2mA)",
          lambda: near(r_out_ohm(MCU, 3.3), 50.0) and near(r_out_ohm(SOC, 1.8), 225.0))
    check("inject 3.465V/50ohm into powered 1.71V rail = 23.10mA",
          lambda: near(inject(3.465, 50.0, 1.71, 0.0)[0], 23.1, 0.01))
    check("inject 3.465V/50ohm into OFF rail (1k) = 2.729mA, phantom 2.729V",
          lambda: near(inject(3.465, 50.0, 0.0, 1000.0)[0], 2.729) and near(inject(3.465, 50.0, 0.0, 1000.0)[1], 2.729))
    check("series 10k only shrinks it: 0.259mA, phantom 0.259V (still > 0)",
          lambda: near(inject(3.465, 10050.0, 0.0, 1000.0)[0], 0.2593) and inject(3.465, 10050.0, 0.0, 1000.0)[0] > 0)
    check("1.89V into 1.71V rail: diode stays off -> 0mA",
          lambda: inject(1.89, 225.0, 1.71, 0.0) == (0.0, 1.71))

    print("-- Part C: RB-A0 interface audit --")
    try:
        results, text = board_report(board, parts)
    except Exception as e:  # noqa: BLE001  (starter: 미구현 함수 때문에 리포트가 죽어도 하네스는 계속)
        print("      board_report raised %s: %s" % (type(e).__name__, e))
        results, text = [], ""
    by = {r["id"]: r for r in results}
    check("statuses L1..L6 = FAIL FAIL PASS FAIL PASS FAIL",
          lambda: [by[k]["status"] for k in ("L1", "L2", "L3", "L4", "L5", "L6")]
          == ["FAIL", "FAIL", "PASS", "FAIL", "PASS", "FAIL"])
    check("L1 3.3->1.8: margins fine but ABS_MAX + CLAMP (logic levels lie)",
          lambda: codes(by["L1"]) == ["ABS_MAX", "CLAMP"] and by["L1"]["dirs"][0]["worst"]["nmh"] > 1.0)
    check("L1 clamp ~23.1mA", lambda: near(by["L1"]["clamp_ma"], 23.1, 0.01))
    check("L2 1.8->3.3: NM_HIGH worst = 1.51-2.0 = -0.490",
          lambda: codes(by["L2"]) == ["NM_HIGH"] and near(by["L2"]["dirs"][0]["worst"]["nmh"], -0.49))
    check("L3 FET shifter: 2 directions, worst NML +0.197 (IMU->SoC), overdrive 0.71V",
          lambda: len(by["L3"]["dirs"]) == 2 and near(by["L3"]["dirs"][1]["worst"]["nml"], 0.1967)
          and near(by["L3"]["fet_overdrive"], 0.71))
    check("L4 GNSS rail OFF: BACKPOWER phantom 2.73V, 2.73mA",
          lambda: "BACKPOWER" in codes(by["L4"]) and near(by["L4"]["backpower"][1], 2.729))
    check("L4 powered: two '3.3V' rails at opposite corners -> MARGINAL ABS_MAX (3.465 > 3.135+0.3)",
          lambda: by["L4"]["findings"][0][:2] == ("MARGINAL", "ABS_MAX") and by["L4"]["clamp_ma"] == 0.0)
    check("L5 Ioff buffer: no findings", lambda: by["L5"]["findings"] == [])
    check("L6: nominal NMH +0.290 PASS but worst FAIL",
          lambda: classify(by["L6"]["dirs"][0]["nom"][0]) == "PASS" and codes(by["L6"]) == ["NM_HIGH"])

    def series_10k_still_fails():
        lk = copy.deepcopy(board["links"][3])
        lk["r_series_ohm"] = 10000
        r = check_link(lk, parts, rails)
        return "BACKPOWER" in codes(r) and near(r["backpower"][1], 0.259)
    check("L4 + 10k series: still BACKPOWER (0.26V phantom)", series_10k_still_fails)

    def clamp_within_iinj():
        lk = copy.deepcopy(board["links"][0])       # MCU(3V3) -> MCU(3V0): IINJ 5mA 인 리시버
        lk["receiver"] = {"part": "MCU33_GPIO", "rail": "VDD_3V0"}
        r = check_link(lk, parts, rails)
        return [(s, c) for s, c, _ in r["findings"]] == [("MARGINAL", "CLAMP")] and near(r["clamp_ma"], 0.3)
    check("3V3 -> 3V0 MCU pin: 0.30mA clamp <= IINJ 5mA -> MARGINAL CLAMP", clamp_within_iinj)

    def high_vth_fet_fails():
        lk = copy.deepcopy(board["links"][2])
        lk["fet"]["vgs_th_max"] = 1.8
        return "FET_VGS" in codes(check_link(lk, parts, rails))
    check("L3 with Vgs(th)max 1.8V FET -> FET_VGS", high_vth_fet_fails)
    check("report summary line", lambda: text.splitlines()[-1] == "6 links: 2 PASS, 0 MARGINAL, 4 FAIL")

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


def main(argv):
    ok = run_tests()
    if "--report" in argv:
        print()
        print(board_report(load("board.json"), load("parts.json")["parts"])[1])
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
