#!/usr/bin/env python3
"""HWLAB-B09 첫 전원 인가: 단락 검사·Golden 보드 비교·Inrush — smoke-test 스크립트

새 보드(DUT)에 처음 전원을 넣기 전후를 데이터로 판정한다.
  Part A  레일별 GND 저항표(DUT 1장 + golden 5장) → golden median/robust sigma, 비율·z-score 로 SHORT/OPEN/LOW/HIGH 판정
          전류 제한 벤치 전원 투입 캡처 → 정상 전류(I_ss), inrush 피크·지속시간·초과 전하(Q)
  Part B  함정: mean/stdev z-score 가 속는 두 경우(빽빽한 golden, 오염된 golden), golden 자체 검사,
          1-샘플 글리치를 피크로 읽는 실수(→ morphological opening), CC(전류 제한)에 잘린 피크, hiccup
  Part C  ORCA-EVB rev B SN0007 시나리오: 저항 스크리닝 → BLOCK → 재작업 → GO → 전류 제한값 추천 → 전원 투입 판정 리포트

  python3 -I solution.py        # 테스트 + Part C 리포트 ("N/M passed", 실패 시 exit 1)

Python 3.9 표준 라이브러리만 쓴다 (math, statistics, csv, os, sys, re).
"""
import math
import os
import re
import statistics
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OL = math.inf                     # DMM 오버로드(open) 표시를 무한대로 다룬다
MAD_TO_SIGMA = 1.4826             # 정규분포에서 sigma = 1.4826 * MAD


def path(name):
    return os.path.join(HERE, name)


# ---------------------------------------------------------------- 공통: 읽기 (주어진 코드)
def load_res_table(name):
    """rail_res.csv → [{'rail','v_nom','golden':[str...],'gnames':[...],'dut':str}] (값은 문자열 그대로)"""
    rows = []
    with open(path(name), encoding="utf-8") as f:
        lines = [ln.strip() for ln in f if ln.strip() and not ln.startswith("#")]
    head = lines[0].split(",")
    gcols = [k for k, h in enumerate(head) if re.match(r"^G\d+$", h)]
    dcol = head.index("DUT")
    for ln in lines[1:]:
        c = ln.split(",")
        rows.append({"rail": c[0], "v_nom": float(c[1]), "gnames": [head[k] for k in gcols],
                     "golden": [c[k] for k in gcols], "dut": c[dcol]})
    return rows


def load_capture(name):
    """캡처 CSV → (meta dict, t[s], v[V], i[A]). 헤더의 '# key=value' 줄이 meta."""
    meta, t, v, i = {}, [], [], []
    with open(path(name), encoding="utf-8") as f:
        for ln in f:
            ln = ln.strip()
            if not ln:
                continue
            if ln.startswith("#"):
                k, _, val = ln[1:].strip().partition("=")
                meta[k.strip()] = val.strip()
            elif ln[0].isdigit() or ln[0] == "-":
                a, b, c = ln.split(",")
                t.append(int(a) * 1e-6)
                v.append(float(b))
                i.append(float(c))
    return meta, t, v, i


def fmt_ohms(x):
    if math.isinf(x):
        return "OL"
    if x >= 1e6:
        return "%.3gM" % (x / 1e6)
    if x >= 1e3:
        return "%.3gk" % (x / 1e3)
    return "%.3g" % x


# ---------------------------------------------------------------- Part A: 저항 스크리닝
def parse_ohms(s):
    """DMM 표시 문자열 → 옴(float). '410' '41.0' '2.20k' '1.2M' '0.95Ω' → 숫자, 'OL'/'O.L'/'OVLD' → OL(inf).
    빈 문자열·음수·알 수 없는 접미사 → ValueError"""
    s = str(s).strip().replace("Ω", "").replace("ohm", "").strip()
    if s.upper() in ("OL", "O.L", "OVLD", "OPEN"):
        return OL
    m = re.match(r"^(\d+(?:\.\d*)?|\.\d+)\s*([kKmM]?)$", s)
    if not m:
        raise ValueError("not an ohms reading: %r" % (s,))
    mult = {"": 1.0, "k": 1e3, "K": 1e3, "M": 1e6, "m": 1e-3}[m.group(2)]
    return float(m.group(1)) * mult


def golden_stats(golden, rel_floor=0.05, abs_floor=0.05):
    """golden 값(옴, OL 가능) → {'n','med','mad','sigma'}
    med   : 유한한 값들의 중앙값
    mad   : median(|x - med|)
    sigma : max(1.4826*MAD, rel_floor*med, abs_floor)  ← 5장짜리 산포는 믿을 수 없으니 바닥(floor)을 둔다"""
    xs = sorted(x for x in golden if not math.isinf(x))
    n = len(xs)
    if n == 0:
        return {"n": 0, "med": OL, "mad": 0.0, "sigma": OL}
    med = statistics.median(xs)
    mad = statistics.median(abs(x - med) for x in xs)
    sigma = max(MAD_TO_SIGMA * mad, rel_floor * med, abs_floor)
    return {"n": n, "med": med, "mad": mad, "sigma": sigma}


def classify_rail(golden, dut, k=4.0, short_ratio=0.2, open_ratio=5.0, min_golden=3):
    """golden 목록과 DUT 값(옴) → {'verdict','ratio','z','med','sigma'}
    판정 (비율 = DUT/med, z = (DUT-med)/sigma):
      golden 유한값 < min_golden          → NOGOLD (비교 불가)
      DUT 가 OL                           → OPEN
      ratio <= short_ratio 이고 z <= -k   → SHORT
      z <= -k                             → LOW
      ratio >= open_ratio 이고 z >= k     → OPEN
      z >= k                              → HIGH
      그 외                                → OK"""
    st = golden_stats(golden)
    med, sigma = st["med"], st["sigma"]
    if st["n"] < min_golden:
        return {"verdict": "NOGOLD", "ratio": None, "z": None, "med": med, "sigma": sigma}
    if math.isinf(dut):
        return {"verdict": "OPEN", "ratio": OL, "z": OL, "med": med, "sigma": sigma}
    ratio = dut / med
    z = (dut - med) / sigma
    if ratio <= short_ratio and z <= -k:
        verdict = "SHORT"
    elif z <= -k:
        verdict = "LOW"
    elif ratio >= open_ratio and z >= k:
        verdict = "OPEN"
    elif z >= k:
        verdict = "HIGH"
    else:
        verdict = "OK"
    return {"verdict": verdict, "ratio": ratio, "z": z, "med": med, "sigma": sigma}


# ---------------------------------------------------------------- Part A: inrush 캡처 측정
def percentile(xs, p):
    """nearest-rank 백분위수 (p in 0..100)"""
    s = sorted(xs)
    r = max(1, int(math.ceil(p / 100.0 * len(s))))
    return s[r - 1]


def steady_state(t, x, window=10e-3):
    """캡처 마지막 window 초 구간의 (median, p99, min, max)"""
    t_last = t[-1]
    w = [xv for tv, xv in zip(t, x) if tv >= t_last - window + 1e-12]
    return statistics.median(w), percentile(w, 99), min(w), max(w)


# ---------------------------------------------------------------- Part B: 함정 대비 도구
def naive_z(golden, dut):
    """(틀린 방법, 비교용으로 주어짐) mean/stdev z-score"""
    xs = [x for x in golden if not math.isinf(x)]
    return (dut - statistics.mean(xs)) / statistics.stdev(xs)


def golden_suspects(rows, lo=0.5, hi=2.0):
    """golden 자체 검사: 각 golden 값을 '나머지 golden 들의 중앙값' 과 비교해 [lo, hi] 밖이면 의심.
    rows = load_res_table 결과 → [(rail, golden_name, value, others_median, ratio)]"""
    out = []
    for r in rows:
        vals = [parse_ohms(s) for s in r["golden"]]
        for j, x in enumerate(vals):
            others = [y for k, y in enumerate(vals) if k != j and not math.isinf(y)]
            if len(others) < 2:
                continue
            om = statistics.median(others)
            ratio = OL if math.isinf(x) else x / om
            if ratio < lo or ratio > hi:
                out.append((r["rail"], r["gnames"][j], x, om, ratio))
    return out


def opening(x, k=3):
    """1-D morphological opening (폭 k 의 min 필터 → 같은 폭의 max 필터).
    폭이 k 샘플보다 좁은 위쪽 스파이크를 지우고, k 이상 지속된 모양(높이·폭)은 보존한다."""
    n = len(x)
    h = k // 2
    ero = [min(x[max(0, j - h):min(n, j - h + k)]) for j in range(n)]
    return [max(ero[max(0, j - (k - 1 - h)):min(n, j - (k - 1 - h) + k)]) for j in range(n)]


def measure_inrush(cap, k=3, window=10e-3, th_frac=0.1, cc_frac=0.97):
    """캡처 → 측정값 dict
      i_ss, i_ss_p99, v_ss, v_pp_ss : 마지막 window 의 정상 상태 (전류 median/p99, 전압 median/pk-pk)
      started     : v_ss >= 0.95*vset 이고 v_pp_ss <= 0.05*vset
      peak_naive  : 원시 최대값,  peak : opening(k) 후 최대값
      i_th        : i_ss + th_frac*(peak - i_ss)  (inrush '켜짐' 문턱)
      t_start, t_end, dur : opening 신호가 i_th 이상인 첫·마지막 샘플 (정상 구간 이전), dur = 개수*dt
      dur_naive   : 같은 문턱을 원시 신호에 적용했을 때
      q           : [t_start, t_end] 에서 sum(max(0, i - i_ss))*dt  (정상 부하를 넘는 전하 ≈ 입력 커패시터 충전량)
      cc_time     : opening 신호가 cc_frac*ilim 이상인 샘플 수 * dt,  cc_pct : 캡처(t>=0) 대비 %
                    (원시 신호로 세면 글리치 1개가 'CC 진입' 으로 오인된다)
      clipped     : cc_time >= k*dt  (전원이 CC 로 들어감 → peak 는 보드가 원한 값이 아니라 설정값)"""
    meta, t, v, i = cap
    vset, ilim = float(meta["vset_V"]), float(meta["ilim_A"])
    dt = t[1] - t[0]
    i_ss, i_p99, _, _ = steady_state(t, i, window)
    v_ss, _, v_min, v_max = steady_state(t, v, window)
    started = v_ss >= 0.95 * vset and (v_max - v_min) <= 0.05 * vset
    io = opening(i, k)
    peak, peak_naive = max(io), max(i)
    i_th = i_ss + th_frac * (peak - i_ss)
    t_win = t[-1] - window
    hit = [j for j in range(len(t)) if io[j] >= i_th and t[j] < t_win]
    hit_naive = [j for j in range(len(t)) if i[j] >= i_th and t[j] < t_win]
    j0, j1 = (hit[0], hit[-1]) if hit else (0, -1)
    q = sum(max(0.0, i[j] - i_ss) for j in range(j0, j1 + 1)) * dt
    n_cc = sum(1 for tv, iv in zip(t, io) if tv >= 0 and iv >= cc_frac * ilim)
    n_on = sum(1 for tv in t if tv >= 0)
    return {"vset": vset, "ilim": ilim, "dt": dt, "i_ss": i_ss, "i_ss_p99": i_p99, "v_ss": v_ss,
            "v_pp_ss": v_max - v_min, "started": started, "peak": peak, "peak_naive": peak_naive,
            "i_th": i_th, "t_start": t[j0] if hit else None, "t_end": t[j1] if hit else None,
            "dur": len(range(j0, j1 + 1)) * dt if hit else 0.0,
            "dur_naive": (hit_naive[-1] - hit_naive[0] + 1) * dt if hit_naive else 0.0,
            "q": q, "cc_time": n_cc * dt, "cc_pct": 100.0 * n_cc / n_on, "clipped": n_cc >= k}


# ---------------------------------------------------------------- Part C: 추천 · 판정 · 리포트
def recommend_limit(g, t_allow=5e-3, margin=1.3, step=0.05):
    """golden 측정(g = measure_inrush, CC 에 안 잘린 것) → 전류 제한 추천
      by_ss     = margin * i_ss_p99                (정상 동작 + 부팅 활동 여유)
      by_inrush = i_ss + q / t_allow                (CC 로 잘려도 입력 캡 충전이 t_allow 안에 끝나도록)
      rec       = max(둘) 을 step 단위로 올림, fault_w = vset * rec (CC 에서 고장점에 들어갈 수 있는 최대 전력)
    golden 이 clipped 이거나 started 가 아니면 ValueError (그 캡처로는 inrush 를 모른다)."""
    if g["clipped"] or not g["started"]:
        raise ValueError("golden capture is CC-clipped or did not start: re-capture with a higher limit")
    by_ss = margin * g["i_ss_p99"]
    by_inrush = g["i_ss"] + g["q"] / t_allow
    raw = max(by_ss, by_inrush)
    step_ma = int(round(step * 1000))
    rec = math.ceil(round(raw * 1000, 6) / step_ma) * step_ma / 1000.0
    return {"by_ss": by_ss, "by_inrush": by_inrush, "rec": rec, "fault_w": g["vset"] * rec}


def poweron_verdict(m, g, t_allow=5e-3, tol=0.15):
    """DUT 측정 m 을 golden 측정 g 와 비교 → (verdict, 설명)
      not started                 → FAIL (CC 에 묶여 못 켜짐 / hiccup)
      |i_ss/g.i_ss - 1| > tol     → FAIL (정상 전류 이상)
      cc_time > t_allow           → WARN (느린 기동 — 시퀀서 타임아웃 위험)
      그 외                        → PASS"""
    if not m["started"]:
        return "FAIL", ("did not start: CC-limited %.0f%% of capture, V_ss=%.2fV pp=%.2fV (hiccup) "
                        "-> limit below startup need" % (m["cc_pct"], m["v_ss"], m["v_pp_ss"]))
    dev = m["i_ss"] / g["i_ss"] - 1.0
    if abs(dev) > tol:
        return "FAIL", "I_ss %.3fA vs golden %.3fA (%+.1f%% > %.0f%%)" % (m["i_ss"], g["i_ss"], 100 * dev, 100 * tol)
    if m["cc_time"] > t_allow:
        return "WARN", "slow start: CC %.1f ms > %.1f ms" % (m["cc_time"] * 1e3, t_allow * 1e3)
    return "PASS", "started, CC %.1f ms (<= %.0f ms), I_ss %.3fA vs golden %.3fA (%+.1f%%)" % (
        m["cc_time"] * 1e3, t_allow * 1e3, m["i_ss"], g["i_ss"], 100 * dev)


def decide(results):
    """[(rail, verdict)] → (decision, reason)
      SHORT 가 하나라도 있으면 BLOCK, NOGOLD/OPEN/LOW/HIGH 가 있으면 WARN(전원은 넣되 낮은 제한·관찰),
      모두 OK 면 GO. reason 에 해당 레일을 판정별로 나열한다 (SHORT 먼저)."""
    order = ["SHORT", "OPEN", "LOW", "HIGH", "NOGOLD"]
    parts = []
    for v in order:
        rails = [r for r, vv in results if vv == v]
        if rails:
            parts.append("%s %s" % (v, ",".join(rails)))
    if any(vv == "SHORT" for _, vv in results):
        return "BLOCK", "do not apply power: " + "; ".join(parts)
    if parts:
        return "WARN", "power with low limit and watch: " + "; ".join(parts)
    return "GO", "all rails match golden"


def screen_report(table_name, sn="0007"):
    """저항 스크리닝 리포트 줄 목록 + decision"""
    rows = load_res_table(table_name)
    out = ["[R2GND] %-13s %8s %10s %7s %7s  %s" % ("rail", "DUT", "golden_med", "ratio", "z", "verdict")]
    res = []
    for r in rows:
        g = [parse_ohms(s) for s in r["golden"]]
        d = parse_ohms(r["dut"])
        c = classify_rail(g, d)
        res.append((r["rail"], c["verdict"]))
        rs = "-" if c["ratio"] is None else ("inf" if math.isinf(c["ratio"]) else "%.3g" % c["ratio"])
        zs = "-" if c["z"] is None else ("inf" if math.isinf(c["z"]) else "%.1f" % c["z"])
        out.append("[R2GND] %-13s %8s %10s %7s %7s  %s" % (r["rail"], fmt_ohms(d), fmt_ohms(c["med"]), rs, zs,
                                                           c["verdict"]))
    for rail, gn, x, om, ratio in golden_suspects(rows):
        out.append("[GOLDEN] %s %s = %s vs others %s (x%.2f) -> re-measure / check entry"
                   % (gn, rail, fmt_ohms(x), fmt_ohms(om), ratio))
    dec, why = decide(res)
    out.append("[DECISION] SN%s %s: %s" % (sn, dec, why))
    return out, dec, res


def smoke_report():
    lines = ["=== SMOKE TEST ORCA-EVB rev B  SN0007  (golden G1..G5) ==="]
    l1, d1, _ = screen_report("rail_res.csv")
    lines += l1
    lines.append("--- after rework (U7 bridge removed, FB12 installed) ---")
    l2, d2, _ = screen_report("rail_res_rework.csv")
    lines += l2
    g = measure_inrush(load_capture("cap_golden_G1_3A0.csv"))
    lines.append("[INRUSH] golden G1 @%.3fA: peak %.2fA (raw max %.2fA, glitch rejected), %.2f ms above %.2fA, "
                 "Q %.2f mC, I_ss %.3fA" % (g["ilim"], g["peak"], g["peak_naive"], g["dur"] * 1e3, g["i_th"],
                                            g["q"] * 1e3, g["i_ss"]))
    rec = recommend_limit(g)
    lines.append("[LIMIT] recommend %.2fA = max(1.3 x p99 %.3fA = %.3fA, I_ss + Q/5ms = %.3fA); "
                 "worst-case fault power %.0fV x %.2fA = %.1f W" % (rec["rec"], g["i_ss_p99"], rec["by_ss"],
                                                                   rec["by_inrush"], g["vset"], rec["rec"],
                                                                   rec["fault_w"]))
    verdicts = {}
    for name in ("cap_dut_0A50.csv", "cap_dut_0A90.csv"):
        m = measure_inrush(load_capture(name))
        v, why = poweron_verdict(m, g)
        verdicts[name] = v
        lines.append("[POWER] SN0007 @%.3fA: %s %s" % (m["ilim"], v, why))
    return lines, d1, d2, rec, verdicts


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


def near(x, ref, tol):
    """abs(x-ref) <= tol 을 확인하고 측정값을 같이 보여 준다"""
    ok = x is not None and abs(x - ref) <= tol
    print("      measured %s  expected %s ± %s" % (x, ref, tol))
    return ok


def raises(exc, fn):
    try:
        fn()
    except exc:
        return True
    return False


def safe(fn, default=None):
    """테스트 준비 계산. starter 의 미구현 함수가 예외를 던져도 하네스가 계속 돌게 한다."""
    try:
        return fn()
    except Exception as e:  # noqa: BLE001
        print("      (setup failed: %s: %s)" % (type(e).__name__, e))
        return default


def row(rows, rail):
    r = next(x for x in rows if x["rail"] == rail)
    return [parse_ohms(s) for s in r["golden"]], parse_ohms(r["dut"])


def run_tests():
    rows = load_res_table("rail_res.csv")

    print("-- Part A: R-to-GND screening --")
    check("parse_ohms('2.20k') == 2200", lambda: near(parse_ohms("2.20k"), 2200.0, 1e-9))
    check("parse_ohms('1.2M') == 1.2e6", lambda: near(parse_ohms("1.2M"), 1.2e6, 1e-6))
    check("parse_ohms('0.95Ω') == 0.95", lambda: near(parse_ohms("0.95Ω"), 0.95, 1e-12))
    check("parse_ohms('OL') is inf", lambda: math.isinf(parse_ohms("OL")) and math.isinf(parse_ohms("O.L")))
    check("parse_ohms('-3') / '12x' raise ValueError",
          lambda: raises(ValueError, lambda: parse_ohms("-3")) and raises(ValueError, lambda: parse_ohms("12x")))
    g18, d18 = safe(lambda: row(rows, "VDD_1V8"), (None, None))
    st = safe(lambda: golden_stats(g18))
    check("VDD_1V8 golden median == 408 (robust to G4 typo 41.0)", lambda: near(st["med"], 408.0, 1e-9))
    check("VDD_1V8 golden MAD == 3", lambda: near(st["mad"], 3.0, 1e-9))
    check("VDD_1V8 sigma == max(1.4826*3, 5%*408) = 20.4", lambda: near(st["sigma"], 20.4, 1e-9))
    c18 = safe(lambda: classify_rail(g18, d18))
    check("VDD_1V8 DUT 2.1 ohm -> SHORT (ratio 0.005, z -19.9)",
          lambda: c18["verdict"] == "SHORT" and near(c18["z"], (2.1 - 408) / 20.4, 1e-9))
    check("VDD_3V3_AUD DUT OL -> OPEN", lambda: classify_rail(*row(rows, "VDD_3V3_AUD"))["verdict"] == "OPEN")
    cddr = safe(lambda: classify_rail(*row(rows, "VDD_1V2_DDR")))
    check("VDD_1V2_DDR 72.4 vs 95.5 -> LOW (z -4.8, not a hard short)",
          lambda: cddr["verdict"] == "LOW" and near(cddr["z"], (72.4 - 95.5) / (0.05 * 95.5), 1e-9))
    check("VIN_12V / VDD_5V0 / VDD_3V3 -> OK",
          lambda: all(classify_rail(*row(rows, r))["verdict"] == "OK" for r in ("VIN_12V", "VDD_5V0", "VDD_3V3")))
    check("VDD_0V8_CORE 1.88 ohm -> OK (low ohms is normal for a core rail)",
          lambda: classify_rail(*row(rows, "VDD_0V8_CORE"))["verdict"] == "OK")
    check("HIGH: 3V3 at 800 ohm (z +5.2, ratio 1.29)",
          lambda: classify_rail(row(rows, "VDD_3V3")[0], 800.0)["verdict"] == "HIGH")
    check("NOGOLD: only 2 finite golden values",
          lambda: classify_rail([10.0, OL, 11.0, OL, OL], 10.5)["verdict"] == "NOGOLD")

    print("-- Part A: inrush capture --")
    gm = safe(lambda: measure_inrush(load_capture("cap_golden_G1_3A0.csv")))
    check("golden I_ss ~= P/V + V/R_bleed = 5/11.98 + 0.006 = 0.4234 A", lambda: near(gm["i_ss"], 0.4234, 0.006))
    check("golden started (V_ss >= 0.95*12 V, flat)", lambda: gm["started"] and near(gm["v_ss"], 11.98, 0.03))
    check("golden peak ~= C*dV/dt + load = 220uF*12V/1ms + ~0.13 = 2.77 A",
          lambda: near(gm["peak"], 2.77, 0.08))
    check("golden inrush duration ~= supply rise time 1.0 ms", lambda: near(gm["dur"] * 1e3, 1.0, 0.1))
    check("golden excess charge q ~= C*V - I_ss*1ms + load = 2.64-0.42+0.05 = 2.27 mC",
          lambda: near(gm["q"] * 1e3, 2.27, 0.12))
    check("golden not clipped at 3 A limit (glitch must not count as CC)",
          lambda: not gm["clipped"] and gm["cc_time"] == 0.0)

    print("-- Part B: traps --")
    zn18 = safe(lambda: naive_z(g18, d18))
    check("naive mean/stdev z on VDD_1V8 = -2.0 -> |z|<3 would MISS the short (contaminated golden)",
          lambda: near(zn18, -2.02, 0.01) and abs(zn18) < 3)
    gp, dp = safe(lambda: row(rows, "VDD_0V9_PCIE"), (None, None))
    znp = safe(lambda: naive_z(gp, dp))
    check("naive z on VDD_0V9_PCIE = +9.8 -> false alarm on a 1.6% difference",
          lambda: near(znp, 9.80, 0.05) and abs(znp) > 3)
    cp = safe(lambda: classify_rail(gp, dp))
    check("robust VDD_0V9_PCIE -> OK (sigma floor 5% = 2.61 ohm, z +0.31)",
          lambda: cp["verdict"] == "OK" and near(cp["z"], 0.8 / 2.61, 1e-6))
    check("absolute rule 'R < 10 ohm = short' would flag VDD_0V8_CORE (golden med 1.91) -> need golden",
          lambda: row(rows, "VDD_0V8_CORE")[1] < 10.0 and classify_rail(*row(rows, "VDD_0V8_CORE"))["verdict"] == "OK")
    gs = safe(lambda: golden_suspects(rows))
    check("golden self-check flags exactly G4 VDD_1V8 (41.0 vs others 409, x0.10)",
          lambda: len(gs) == 1 and gs[0][:2] == ("VDD_1V8", "G4") and near(gs[0][4], 41.0 / 409.0, 1e-9))
    check("opening(k=3) removes 1-sample spike, keeps 3-sample plateau",
          lambda: opening([0, 0, 5, 0, 0, 2, 2, 2, 0, 0], 3) == [0, 0, 0, 0, 0, 2, 2, 2, 0, 0])
    check("golden raw max is the glitch (> 4.5 A) but robust peak < 2.9 A",
          lambda: gm["peak_naive"] > 4.5 and gm["peak"] < 2.9)
    check("glitch at 12.34 ms stretches naive duration to > 12 ms; robust stays ~1 ms",
          lambda: gm["dur_naive"] > 12e-3 and 0.9e-3 <= gm["dur"] <= 1.1e-3)
    m50 = safe(lambda: measure_inrush(load_capture("cap_dut_0A50.csv")))
    check("0.50 A capture: clipped, peak == limit (not a measurement of inrush)",
          lambda: m50["clipped"] and near(m50["peak"], 0.50, 0.02))
    check("0.50 A capture: did not start (V_ss ~5 V, pk-pk > 1 V hiccup), CC > 90% of capture",
          lambda: (not m50["started"]) and m50["v_ss"] < 7 and m50["v_pp_ss"] > 1.0 and m50["cc_pct"] > 90)
    check("0.50 A > golden I_ss 0.42 A, yet too low: constant-power load needs P/V = 5W/4.5V = 1.1 A near UVLO",
          lambda: 0.50 > gm["i_ss"] and 5.0 / 4.5 > 0.50)

    print("-- Part C: SN0007 smoke-test scenario --")
    rec = safe(lambda: recommend_limit(gm))
    check("recommend_limit: by_inrush = I_ss + Q/5ms ~= 0.88 A", lambda: near(rec["by_inrush"], 0.88, 0.03))
    check("recommend_limit: rec == 0.90 A (ceil to 50 mA), fault power 10.8 W",
          lambda: near(rec["rec"], 0.90, 1e-9) and near(rec["fault_w"], 10.8, 1e-9))
    check("recommend_limit refuses a clipped golden capture", lambda: raises(ValueError, lambda: recommend_limit(m50)))
    m90 = safe(lambda: measure_inrush(load_capture("cap_dut_0A90.csv")))
    check("0.90 A capture: started, CC during inrush only (< 5 ms)",
          lambda: m90["started"] and m90["clipped"] and 0 < m90["cc_time"] < 5e-3)
    lines, d1, d2, _, verdicts = safe(smoke_report, (["(no report)"], None, None, None, {}))
    print("\n".join("      " + ln for ln in lines))
    check("report: first screen BLOCK (SHORT VDD_1V8)", lambda: d1 == "BLOCK" and any(
        "BLOCK" in ln and "SHORT VDD_1V8" in ln for ln in lines))
    check("report: G4 VDD_1V8 flagged as golden suspect",
          lambda: any(ln.startswith("[GOLDEN] G4 VDD_1V8") for ln in lines))
    check("report: after rework WARN (LOW VDD_1V2_DDR only)",
          lambda: d2 == "WARN" and any("WARN" in ln and "LOW VDD_1V2_DDR" in ln and "SHORT" not in ln for ln in lines))
    check("report: SN0007 @0.50A FAIL did not start, @0.90A PASS",
          lambda: verdicts == {"cap_dut_0A50.csv": "FAIL", "cap_dut_0A90.csv": "PASS"})

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


if __name__ == "__main__":
    sys.exit(0 if run_tests() else 1)
