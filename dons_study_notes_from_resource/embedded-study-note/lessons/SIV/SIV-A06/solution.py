#!/usr/bin/env python3
"""SIV-A06 보드 레벨 검증: 전원 시퀀싱과 클럭 — Python 3.9 stdlib only.

python3 -I solution.py            # 체크 실행
python3 -I solution.py --report   # 보드별 리포트만 출력
보드/스펙/숫자는 모두 대표값(가정). 실제 값은 SoC 데이터시트 · PMIC OTP · 보드 스펙에서 온다.
"""
import csv
import json
import math
import os
import random
import statistics
import sys
from fractions import Fraction

HERE = os.path.dirname(os.path.abspath(__file__))

# ---------------------------------------------------------------- 스펙 (대표값)
SPEC = {
    "Ts_us": 10.0,
    "settle_us": 300.0,          # 대역 진입 후 이만큼 지난 뒤부터 대역을 판정
    "hyst_frac": 0.01,           # glitch 재무장 히스테리시스 (nom 대비)
    "glitch_must_see_us": 20.0,  # 이 폭 이상의 glitch는 반드시 보여야 한다
    "glitch_corr_us": 100.0,     # 부모 레일 glitch 뒤 이 시간 안의 자식 glitch = 2차 현상
    "rails": {
        "VIN_12V": {"nom": 12.00, "tol_pct": 5.0, "ramp_us": [100.0, 900.0], "parent": None},
        "VDD_0V8": {"nom": 0.80, "tol_pct": 5.0, "ramp_us": [100.0, 800.0], "parent": "VIN_12V"},
        "VDD_1V8": {"nom": 1.80, "tol_pct": 5.0, "ramp_us": [100.0, 800.0], "parent": "VIN_12V"},
        "VDD_1V1": {"nom": 1.10, "tol_pct": 5.0, "ramp_us": [100.0, 800.0], "parent": "VDD_1V8"},
        "VDD_3V3": {"nom": 3.30, "tol_pct": 5.0, "ramp_us": [100.0, 800.0], "parent": "VIN_12V"},
    },
    # a가 a_frac까지 간 뒤 b가 b_frac에 닿을 때까지 최소 min_us
    "on_rules": [
        {"a": "VIN_12V", "a_frac": 0.9, "b": "VDD_0V8", "b_frac": 0.1, "min_us": 100.0},
        {"a": "VDD_0V8", "a_frac": 0.9, "b": "VDD_1V8", "b_frac": 0.1, "min_us": 100.0},
        {"a": "VDD_1V8", "a_frac": 0.9, "b": "VDD_1V1", "b_frac": 0.1, "min_us": 100.0},
        {"a": "VDD_1V1", "a_frac": 0.9, "b": "VDD_3V3", "b_frac": 0.1, "min_us": 100.0},
    ],
    "off_rules": [
        {"a": "VDD_3V3", "a_frac": 0.1, "b": "VDD_1V1", "b_frac": 0.9, "min_us": 100.0},
        {"a": "VDD_1V1", "a_frac": 0.1, "b": "VDD_1V8", "b_frac": 0.9, "min_us": 100.0},
        {"a": "VDD_1V8", "a_frac": 0.1, "b": "VDD_0V8", "b_frac": 0.9, "min_us": 100.0},
        {"a": "VDD_0V8", "a_frac": 0.1, "b": "VIN_12V", "b_frac": 0.9, "min_us": 100.0},
    ],
}

CLOCK_TREE = {
    "XO_24M": {"hz": 24000000},
    "PLL_CPU": {"parent": "XO_24M", "mul": 100, "div": 1, "vco": [800000000, 3200000000]},
    "CPU_CLK": {"parent": "PLL_CPU", "div": 2},
    "PLL_DDR": {"parent": "XO_24M", "mul": 88, "div": 1, "vco": [800000000, 3200000000]},
    "DDR_CK": {"parent": "PLL_DDR", "div": 2},
    "PLL_SYS": {"parent": "XO_24M", "mul": 125, "div": 1, "vco": [800000000, 3200000000]},
    "AXI_CLK": {"parent": "PLL_SYS", "div": 6},
    "APB_CLK": {"parent": "PLL_SYS", "div": 30},
    "PCIE_REF": {"parent": "PLL_SYS", "div": 30},
    "PLL_AUD": {"parent": "XO_24M", "mul": 40, "frac": 16106127, "frac_bits": 24, "div": 1,
                "vco": [800000000, 3200000000]},
    "AUD_MCLK": {"parent": "PLL_AUD", "div": 80},
}
# ppm 허용치. PCIE_REF ±300 ppm 은 PCIe Base Spec 의 Refclk 허용치, 나머지는 대표값.
CLOCK_LIMITS_PPM = {"XO_24M": 30.0, "PCIE_REF": 300.0}
CLOCK_DEFAULT_LIMIT_PPM = 100.0


# ---------------------------------------------------------------- 주어진 도구
class Log:
    def __init__(self, t, rails, name=""):
        self.t, self.rails, self.name = t, rails, name


def load_log(path):
    with open(path, newline="") as f:
        rd = csv.reader(f)
        hdr = next(rd)
        cols = [[] for _ in hdr]
        for row in rd:
            for c, x in zip(cols, row):
                c.append(float(x))
    return Log(cols[0], dict(zip(hdr[1:], cols[1:])), os.path.splitext(os.path.basename(path))[0])


def _interp(t0, v0, t1, v1, level):
    if v1 == v0:
        return t0
    return t0 + (level - v0) * (t1 - t0) / (v1 - v0)


def local_slope(t, v, tc, half=2):
    """tc 부근 기울기(V/us). 경계에서는 창을 줄인다."""
    k = min(range(len(t)), key=lambda i: abs(t[i] - tc))
    a, b = max(0, k - half), min(len(t) - 1, k + half)
    if t[b] == t[a]:
        return 0.0
    return (v[b] - v[a]) / (t[b] - t[a])


def mad_sigma(xs):
    """글리치에 강건한 표준편차 추정 (1.4826 x MAD)."""
    m = statistics.median(xs)
    return 1.4826 * statistics.median([abs(x - m) for x in xs])


# ================================================================ Part A
def first_crossing(t, v, level, rising=True, hold=1, start=1):
    """level을 처음 통과하는 시각(선형 보간). 직전 샘플은 반대쪽, 통과 후 hold개 샘플이 계속
    넘어간 상태여야 인정. 못 찾거나 로그가 이미 넘어간 상태로 시작하면 None."""
    sign = 1 if rising else -1
    n = len(v)
    for i in range(max(start, 1), n):
        if not (sign * (v[i - 1] - level) < 0 <= sign * (v[i] - level)):
            continue
        if i + hold > n:
            return None
        if all(sign * (x - level) >= 0 for x in v[i:i + hold]):
            return _interp(t[i - 1], v[i - 1], t[i], v[i], level)
    return None


def last_crossing(t, v, level, rising=False):
    """마지막으로 level을 통과해 끝까지 넘어간 상태로 남는 시각(전원 차단 판정용).
    끝까지 안 넘어가면 None."""
    sign = 1 if rising else -1
    for j in range(len(v) - 1, -1, -1):
        if sign * (v[j] - level) < 0:
            if j == len(v) - 1:
                return None
            return _interp(t[j], v[j], t[j + 1], v[j + 1], level)
    return None


def measure_ramp(t, v, low, high, rising=True):
    """10-90% (rising) 또는 90-10% (falling) 전이 시간. far 첫 통과(hold=3)를 잡고,
    거기서 거꾸로 near의 마지막 통과를 찾는다. 반환 (t_near, t_far, dt) 또는 None."""
    span = high - low
    near, far = low + 0.1 * span, low + 0.9 * span
    if not rising:
        near, far = far, near
    t_far = first_crossing(t, v, far, rising, hold=3)
    if t_far is None:
        return None
    sign = 1 if rising else -1
    k = next(i for i in range(len(t)) if t[i] >= t_far)
    for j in range(k, 0, -1):
        if sign * (v[j - 1] - near) < 0 <= sign * (v[j] - near):
            t_near = _interp(t[j - 1], v[j - 1], t[j], v[j], near)
            return (t_near, t_far, t_far - t_near)
    return None


def detect_glitches(t, v, lo, hi, hyst=0.0, min_samples=1):
    """[lo,hi] 이탈 이벤트. hi/lo를 넘으면 시작, 안쪽으로 hyst 이상 돌아와야 종료.
    반환: dict(kind, t0, t1, width, extreme, samples, truncated) 리스트.
    t0/t1은 한계선 통과 보간 시각, samples는 한계 밖 샘플 수."""
    if lo >= hi or hyst < 0:
        raise ValueError("bad limits")
    out = []
    n = len(v)
    i = 0
    while i < n:
        if lo <= v[i] <= hi:
            i += 1
            continue
        over = v[i] > hi
        sign = 1 if over else -1
        limit = hi if over else lo
        rearm = limit - sign * hyst
        i0 = i_last = i
        extreme = v[i]
        nout = 1
        i += 1
        while i < n:
            x = v[i]
            if sign * (x - limit) > 0:
                i_last = i
                nout += 1
                extreme = max(extreme, x) if over else min(extreme, x)
            if sign * (x - rearm) <= 0:
                break
            i += 1
        truncated = i0 == 0 or i_last == n - 1
        t0 = t[0] if i0 == 0 else _interp(t[i0 - 1], v[i0 - 1], t[i0], v[i0], limit)
        t1 = t[-1] if i_last == n - 1 else _interp(t[i_last], v[i_last], t[i_last + 1], v[i_last + 1], limit)
        if nout >= min_samples:
            out.append({"kind": "over" if over else "under", "t0": t0, "t1": t1, "width": t1 - t0,
                        "extreme": extreme, "samples": nout, "truncated": truncated})
    return out


def eval_rule(delta, min_us, max_us=None, u=0.0):
    """측정 불확도 u를 고려한 3값 판정 PASS/FAIL/INDET.
    확실히 만족 → PASS, 확실히 위반 → FAIL, 그 사이 → INDET."""
    if delta - u >= min_us and (max_us is None or delta + u <= max_us):
        return "PASS"
    if delta + u < min_us or (max_us is not None and delta - u > max_us):
        return "FAIL"
    return "INDET"


def dc_check(level, nom, tol_pct):
    """정착 구간 중앙값의 ±tol% 판정. 반환 (ok, err_pct)."""
    err = (level - nom) / nom * 100.0
    return (abs(err) <= tol_pct, err)


def ppm_error(f_meas, f_nom):
    """(f_meas - f_nom) / f_nom x 1e6"""
    return (float(f_meas) - float(f_nom)) / float(f_nom) * 1e6


def clock_tree_expected(tree):
    """트리 사전으로 모든 노드의 기대 주파수를 Fraction으로 계산.
    f = parent x (mul + frac/2^bits) / div. 사이클/없는 부모/VCO 범위 위반은 ValueError."""
    memo, visiting = {}, set()

    def go(n):
        if n in memo:
            return memo[n]
        if n in visiting:
            raise ValueError("cycle at %s" % n)
        if n not in tree:
            raise ValueError("unknown parent %s" % n)
        visiting.add(n)
        nd = tree[n]
        if "hz" in nd:
            f = Fraction(nd["hz"])
        else:
            frac = Fraction(nd["frac"], 1 << nd["frac_bits"]) if nd.get("frac") else Fraction(0)
            f = go(nd["parent"]) * (nd.get("mul", 1) + frac) / nd.get("div", 1)
        if "vco" in nd and not nd["vco"][0] <= f <= nd["vco"][1]:
            raise ValueError("%s out of VCO range: %s" % (n, float(f)))
        visiting.discard(n)
        memo[n] = f
        return f

    return {n: go(n) for n in tree}


def counter_read(f_hz, gate_s, phase):
    """gate_s 동안 입력 에지 수를 세는 gated counter. 0 <= phase < 1 은 게이트 시작 위상."""
    return int(math.floor(f_hz * gate_s + phase))


def gate_resolution_ppm(f_hz, gate_s):
    """±1 count 양자화의 상대 분해능(ppm)."""
    return 1e6 / (f_hz * gate_s)


def required_gate_s(f_hz, ppm):
    """±1 count가 ppm 이내가 되는 최소 게이트 시간."""
    return 1e6 / (f_hz * ppm)


# ================================================================ Part B
def check_timestamps(t, Ts_nom, gap_factor=1.5):
    """타임스탬프 검증. dt<=0 → nonmono, dt>Ts*gap_factor → gap, dt<Ts/gap_factor → short.
    중앙값 Ts가 Ts_nom과 1% 넘게 다르면 rate. 반환 dict(ok, issues, Ts_us, jitter_us)."""
    dts = [b - a for a, b in zip(t, t[1:])]
    issues = []
    for i, d in enumerate(dts):
        if d <= 0:
            issues.append(("nonmono", i + 1, t[i], t[i + 1]))
        elif d > gap_factor * Ts_nom:
            issues.append(("gap", i + 1, t[i], t[i + 1]))
        elif d < Ts_nom / gap_factor:
            issues.append(("short", i + 1, t[i], t[i + 1]))
    Ts = statistics.median(dts) if dts else 0.0
    if abs(Ts - Ts_nom) > 0.01 * Ts_nom:
        issues.append(("rate", 0, Ts, Ts_nom))
    good = [d for d in dts if d > 0]
    jitter = statistics.pstdev(good) if len(good) > 1 else 0.0
    return {"ok": not issues, "issues": issues, "Ts_us": Ts, "jitter_us": jitter}


def crossing_uncertainty(Ts, slope, sigma_v, jitter=0.0, skew=0.0):
    """한 번의 임계 통과 시각 불확도(us, 보수적). 파형 곡률/구간 Ts/2 + 다채널 skew
    + 3 x hypot(타임스탬프 지터, 전압 잡음/기울기). slope <= 0 이면 ValueError."""
    if slope <= 0:
        raise ValueError("slope must be positive")
    return Ts / 2.0 + skew + 3.0 * math.hypot(jitter, sigma_v / slope)


def group_simultaneous(times, tie_us):
    """{rail: 시각} → 시간순 그룹 리스트. 인접 간격 <= tie_us면 같은 그룹(단일 연결).
    그룹 안은 측정 잡음에 의존하지 않도록 이름순."""
    items = sorted(times.items(), key=lambda kv: (kv[1], kv[0]))
    groups, last = [], None
    for name, tc in items:
        if last is not None and tc - last <= tie_us:
            groups[-1].append(name)
        else:
            groups.append([name])
        last = tc
    return [sorted(g) for g in groups]


def acquire_depth(width, depth, Ts, phase, mode):
    """폭 width, 깊이 depth의 사각 dip([0,width))를 샘플러가 얼마나 깊게 보고하는가.
    샘플 시각 phase+k*Ts (0<=phase<Ts). mode: point(순간 샘플)/boxcar(Ts 평균)/peak(구간 min-max)."""
    if mode == "point":
        return depth if phase < width else 0.0
    if mode == "peak":
        return depth if width > 0 else 0.0
    if mode == "boxcar":
        best = 0.0
        for k in range(0, int(math.ceil(width / Ts)) + 2):
            lo = phase + (k - 1) * Ts
            hi = phase + k * Ts
            best = max(best, max(0.0, min(width, hi) - max(0.0, lo)))
        return depth * best / Ts
    raise ValueError(mode)


def detect_rate(width, depth, Ts, limit_depth, mode, n_phase=1000):
    """위상 격자 n_phase개에서 보고 깊이 >= limit_depth 인 비율."""
    hits = 0
    for j in range(n_phase):
        phase = (j + 0.5) / n_phase * Ts
        if acquire_depth(width, depth, Ts, phase, mode) >= limit_depth:
            hits += 1
    return hits / n_phase


def clock_verdict(err_ppm, limit_ppm, u_ppm):
    """가드밴드 3값 판정: |err|+u <= limit → PASS, |err|-u > limit → FAIL, 그 외 INDET."""
    if abs(err_ppm) + u_ppm <= limit_ppm:
        return "PASS"
    if abs(err_ppm) - u_ppm > limit_ppm:
        return "FAIL"
    return "INDET"


# ================================================================ Part C
def measure_rail(t, v, name, r, spec, ts):
    """레일 하나 측정 + DC/ramp/glitch 판정. 반환 (meas, findings, glitch 리스트)."""
    nom, tol = r["nom"], r["tol_pct"]
    lo, hi = nom * (1 - tol / 100.0), nom * (1 + tol / 100.0)
    Ts = ts["Ts_us"]
    findings = []
    t_up = first_crossing(t, v, lo, True, hold=3)
    if t_up is None:
        if v[0] >= lo:
            findings.append({"sev": "INDET", "check": "capture", "rail": name,
                             "msg": "rise edge not in capture (v[0]=%.3fV already in band)" % v[0]})
        else:
            findings.append({"sev": "FAIL", "check": "no_rise", "rail": name,
                             "msg": "never reached band: vmax=%.3fV lo=%.3fV" % (max(v), lo)})
        return {"reached": False}, findings, []
    t_down = last_crossing(t, v, lo, False)
    w0 = t_up + spec["settle_us"]
    w1 = t_down - 2.0 * Ts if t_down is not None else t[-1]
    idx = [i for i in range(len(t)) if w0 <= t[i] <= w1]
    if len(idx) < 5:
        findings.append({"sev": "INDET", "check": "capture", "rail": name,
                         "msg": "plateau window too short (%d samples)" % len(idx)})
        return {"reached": False, "t_up": t_up}, findings, []
    wt = [t[i] for i in idx]
    wv = [v[i] for i in idx]
    level = statistics.median(wv)
    sigma = mad_sigma(wv)
    low = statistics.median(v[:8])
    meas = {"reached": True, "t_up": t_up, "t_down": t_down, "dc_v": level, "sigma_v": sigma}
    ok, err = dc_check(level, nom, tol)
    meas["dc_err_pct"] = err
    glitches = []
    if not ok:
        findings.append({"sev": "FAIL", "check": "dc", "rail": name, "dc_v": level,
                         "msg": "dc=%.3fV err=%+.2f%% limit=+/-%.1f%%" % (level, err, tol)})
    else:
        margin = min(level - lo, hi - level)
        if sigma > 0 and margin / sigma < 5.0:
            findings.append({"sev": "WARN", "check": "noise", "rail": name,
                             "msg": "margin %.1f sigma: limit close to noise, false-alarm risk" % (margin / sigma)})
        for g in detect_glitches(wt, wv, lo, hi, hyst=spec["hyst_frac"] * nom):
            g["rail"] = name
            ref = nom - g["extreme"] if g["kind"] == "under" else g["extreme"] - nom
            g["depth_pct"] = ref / nom * 100.0
            glitches.append(g)
    rm = measure_ramp(t, v, low, level, True)
    if rm is None:
        findings.append({"sev": "INDET", "check": "ramp", "rail": name, "msg": "ramp not measurable"})
    else:
        meas["ramp_us"] = rm[2]
        rlo, rhi = r["ramp_us"]
        if not rlo <= rm[2] <= rhi:
            findings.append({"sev": "FAIL", "check": "ramp", "rail": name, "ramp_us": rm[2],
                             "msg": "ramp10-90=%.1fus limit=[%.0f,%.0f]us" % (rm[2], rlo, rhi)})
    return meas, findings, glitches


def _rail_event(log, spec, rail, frac, direction):
    t, v = log.t, log.rails[rail]
    level = frac * spec["rails"][rail]["nom"]
    if direction == "on":
        return first_crossing(t, v, level, True, hold=3)
    return last_crossing(t, v, level, False)


def check_rails(log, spec):
    """로그 전체 판정. 반환 dict(name, status, findings, meas, blind, on_order, ts).
    status: INVALID > FAIL > INDET > PASS."""
    out = {"name": log.name, "status": "PASS", "findings": [], "meas": {}, "blind": [],
           "on_order": [], "ts": None}
    ts = check_timestamps(log.t, spec["Ts_us"])
    out["ts"] = ts
    if not ts["ok"]:
        kinds = sorted(set(x[0] for x in ts["issues"]))
        first = ts["issues"][0]
        out["status"] = "INVALID"
        out["findings"].append({"sev": "INVALID", "check": "timestamps", "rail": "-",
                                "msg": "%d issues %s first at row %d (t=%.1f->%.1f): capture unusable, re-take" %
                                       (len(ts["issues"]), kinds, first[1], first[2], first[3])})
        return out
    Ts = ts["Ts_us"]
    out["blind"].append("glitch narrower than Ts=%.1fus can fall between samples" % Ts)
    if Ts > spec["glitch_must_see_us"]:
        out["findings"].append({"sev": "INDET", "check": "coverage", "rail": "-",
                                "msg": "Ts=%.1fus > must-see %.1fus: glitch check not covered" %
                                       (Ts, spec["glitch_must_see_us"])})
    glitches = []
    for name, r in spec["rails"].items():
        m, f, g = measure_rail(log.t, log.rails[name], name, r, spec, ts)
        out["meas"][name] = m
        out["findings"].extend(f)
        glitches.extend(g)

    def edge_u(rail, tc):
        v = log.rails[rail]
        sigma = out["meas"][rail].get("sigma_v", 0.0)
        return crossing_uncertainty(Ts, max(abs(local_slope(log.t, v, tc)), 1e-9), sigma,
                                    ts["jitter_us"], spec.get("skew_us", 0.0))

    for key, direction in (("on_rules", "on"), ("off_rules", "off")):
        for rule in spec[key]:
            a, b = rule["a"], rule["b"]
            ta = _rail_event(log, spec, a, rule["a_frac"], direction)
            tb = _rail_event(log, spec, b, rule["b_frac"], direction)
            rname = "%s@%d%%->%s@%d%%" % (a, rule["a_frac"] * 100, b, rule["b_frac"] * 100)
            if ta is None or tb is None:
                out["findings"].append({"sev": "INDET", "check": "order_" + direction, "rail": b,
                                        "a": a, "b": b, "msg": "rule=%s edge not in capture" % rname})
                continue
            u = edge_u(a, ta) + edge_u(b, tb)
            delta = tb - ta
            verdict = eval_rule(delta, rule["min_us"], rule.get("max_us"), u)
            if verdict != "PASS":
                out["findings"].append({"sev": verdict, "check": "order_" + direction, "rail": b,
                                        "a": a, "b": b, "delta_us": delta,
                                        "msg": "rule=%s dt=%.1fus min=%.1fus u=+/-%.1fus" %
                                               (rname, delta, rule["min_us"], u)})
    on10 = {}
    for name, r in spec["rails"].items():
        tc = first_crossing(log.t, log.rails[name], 0.1 * r["nom"], True, hold=3)
        if tc is not None:
            on10[name] = tc
    out["on_order"] = group_simultaneous(on10, 2.0 * Ts)

    for g in glitches:
        p = spec["rails"][g["rail"]]["parent"]
        while p is not None:
            if any(h["rail"] == p and 0 <= g["t0"] - h["t0"] <= spec["glitch_corr_us"] for h in glitches):
                g["secondary_of"] = p
                break
            p = spec["rails"][p]["parent"]
    for g in sorted(glitches, key=lambda x: ("secondary_of" in x, x["t0"])):
        tag = "secondary_of=" + g["secondary_of"] if "secondary_of" in g else "primary"
        out["findings"].append(dict(g, sev="FAIL", check="glitch",
                                    msg="t=%.1fus dur=%.1fus depth=%.1f%% kind=%s samples=%d %s" %
                                        (g["t0"], g["width"], g["depth_pct"], g["kind"], g["samples"], tag)))
    sevs = [f["sev"] for f in out["findings"]]
    out["status"] = "INVALID" if "INVALID" in sevs else "FAIL" if "FAIL" in sevs else \
        "INDET" if "INDET" in sevs else "PASS"
    return out


def localize_clock_fault(tree, exp, meas_hz, tol_ppm, report_ppm=0.0):
    """측정 노드들의 비율 meas/exp로 고장 위치를 위에서 아래로 좁힌다.
    노드의 측정 자손 중 과반이 같은 비율(≠1)이면 그 노드가 원인, 비율을 자손에서 제거하고 내려간다.
    abs(ppm) > report_ppm 만 기록. 반환 [{node, ratio, ppm, hint, confounded}]."""
    kids = {n: [] for n in tree}
    roots = []
    for n, nd in tree.items():
        if "parent" in nd:
            kids[nd["parent"]].append(n)
        else:
            roots.append(n)

    measured_below = {}

    def collect(n):
        s = [n] if n in meas_hz else []
        for c in kids[n]:
            s += collect(c)
        measured_below[n] = s
        return s

    for r in roots:
        collect(r)

    faults = []

    def visit(n, inherited):
        ms = measured_below[n]
        if not ms:
            return
        rs = [meas_hz[m] / float(exp[m]) / inherited for m in ms]
        r0 = statistics.median_low(rs)
        agree = [x for x in rs if abs(x / r0 - 1.0) * 1e6 <= tol_ppm]
        is_here = abs(r0 - 1.0) * 1e6 > tol_ppm and len(agree) * 2 > len(rs)
        next_inh = inherited
        if is_here:
            next_inh = inherited * r0
            ppm = (r0 - 1.0) * 1e6
            if abs(ppm) > report_ppm:
                frac = Fraction(r0).limit_denominator(256)
                hint = None
                if frac != 1 and abs(float(frac) / r0 - 1.0) * 1e6 <= 3.0 * max(tol_ppm, 1.0):
                    hint = "%d/%d" % (frac.numerator, frac.denominator)
                confounded = []
                if len(ms) == 1:
                    path, cur = [], ms[0]
                    while cur != n:
                        path.append(cur)
                        cur = tree[cur]["parent"]
                    path.append(n)
                    confounded = list(reversed(path)) if len(path) > 1 else []
                faults.append({"node": n, "ratio": r0, "ppm": ppm, "hint": hint, "confounded": confounded})
        for c in kids[n]:
            visit(c, next_inh)

    for r in roots:
        visit(r, 1.0)
    return faults


def check_clocks(tree, board, limits=None, default_limit=CLOCK_DEFAULT_LIMIT_PPM):
    """board = {gate_s, timebase_ppm, counts{node:count}}. 노드별 ppm/불확도/판정 + 고장 위치.
    u = ±1 count 분해능 + 카운터 타임베이스. 반환 dict(status, rows, faults)."""
    limits = limits if limits is not None else CLOCK_LIMITS_PPM
    exp = clock_tree_expected(tree)
    gate = board["gate_s"]
    rows, meas = [], {}
    for node in sorted(board["counts"]):
        f = board["counts"][node] / gate
        meas[node] = f
        e = float(exp[node])
        err = ppm_error(f, e)
        u = gate_resolution_ppm(e, gate) + board["timebase_ppm"]
        lim = limits.get(node, default_limit)
        rows.append({"node": node, "exp_hz": e, "meas_hz": f, "ppm": err, "u_ppm": u, "limit": lim,
                     "verdict": clock_verdict(err, lim, u)})
    verdicts = [r["verdict"] for r in rows]
    status = "FAIL" if "FAIL" in verdicts else "INDET" if "INDET" in verdicts else "PASS"
    faults = []
    if status == "FAIL":
        tol = max(2.0 * max(r["u_ppm"] for r in rows), 1.0)
        faults = localize_clock_fault(tree, exp, meas, tol, report_ppm=min(limits.values()))
    return {"status": status, "rows": rows, "faults": faults}


# ---------------------------------------------------------------- 주어진 리포트
NEXT_STEP = {
    "order_on": "check PMIC OTP/EN chain and PG wiring of the named rails",
    "order_off": "check discharge FET and EN release order on power-down",
    "ramp": "check soft-start cap / load; compare to golden board",
    "dc": "check FB divider and remote-sense; DMM at load",
    "glitch": "re-trigger scope on this rail with peak-detect; look for load steps at t",
    "no_rise": "check EN/PG/fuse for this rail",
}


def format_report(res):
    lines = ["=== %s  STATUS=%s ===" % (res["name"], res["status"])]
    ts = res["ts"]
    if ts and ts["ok"]:
        lines.append("capture: Ts=%.2fus jitter=%.3fus" % (ts["Ts_us"], ts["jitter_us"]))
    for b in res["blind"]:
        lines.append("blind: " + b)
    if res["on_order"]:
        lines.append("observed on-order: " + " < ".join("=".join(g) for g in res["on_order"]))
    for f in res["findings"]:
        lines.append("%s rail=%s check=%s %s" % (f["sev"], f["rail"], f["check"], f["msg"]))
        if f["sev"] == "FAIL" and f["check"] in NEXT_STEP and "secondary_of" not in f:
            lines.append("  next: " + NEXT_STEP[f["check"]])
    for rail, m in res["meas"].items():
        if m.get("reached"):
            lines.append("  %-8s dc=%.3fV (%+.2f%%) ramp10-90=%sus sigma=%.1fmV" %
                         (rail, m["dc_v"], m["dc_err_pct"],
                          "%.1f" % m["ramp_us"] if "ramp_us" in m else "n/a", m["sigma_v"] * 1000))
    return "\n".join(lines)


def format_clock_report(name, res):
    lines = ["=== clock %s  STATUS=%s ===" % (name, res["status"])]
    for r in res["rows"]:
        lines.append("  %-9s exp=%14.3fHz ppm=%+11.2f u=%6.2f limit=%5.0f %s" %
                     (r["node"], r["exp_hz"], r["ppm"], r["u_ppm"], r["limit"], r["verdict"]))
    for f in res["faults"]:
        lines.append("FAULT node=%s ppm=%+.1f ratio_hint=%s confounded_with=%s" %
                     (f["node"], f["ppm"], f["hint"], f["confounded"] or "-"))
    return "\n".join(lines)


# ---------------------------------------------------------------- 하네스
_pass = 0
_fail = 0


def CHECK(name, cond, detail=""):
    global _pass, _fail
    if cond:
        _pass += 1
        print("PASS %s" % name)
    else:
        _fail += 1
        print("FAIL %s: %s" % (name, detail))


def near(x, ref, tol):
    return x is not None and abs(x - ref) <= tol


def run(fn):
    """테스트 묶음 실행: TODO 스텁이나 예외는 FAIL 한 줄로 집계하고 다음 묶음으로 간다."""
    try:
        fn()
    except NotImplementedError as e:
        CHECK(fn.__name__, False, "TODO: %s" % e)
    except Exception as e:  # pylint: disable=broad-except
        CHECK(fn.__name__, False, "exception: %r" % (e,))


def ramp_wave(Ts, n, t_on, T, nom, sigma, seed):
    """선형 soft-start 합성 파형 (측정값, 참값 기준 t_on+frac*T)."""
    rng = random.Random(seed)
    t = [k * Ts for k in range(n)]
    v = []
    for x in t:
        y = 0.0 if x < t_on else nom * min(1.0, (x - t_on) / T)
        v.append(y + rng.gauss(0.0, sigma))
    return t, v


def Q(x):
    return 0.5 * math.erfc(x / math.sqrt(2.0))


def data(fname):
    return os.path.join(HERE, fname)


def truth():
    with open(data("truth.json")) as f:
        return json.load(f)


# ---- 주어진 도구 확인 (starter에서도 통과)
def test_given_tools():
    log = load_log(data("board_good.csv"))
    CHECK("given.load_rows", len(log.t) == 1200, str(len(log.t)))
    CHECK("given.load_rails", set(log.rails) == set(SPEC["rails"]), str(sorted(log.rails)))
    CHECK("given.local_slope", near(local_slope([0, 1, 2, 3, 4], [0, 2, 4, 6, 8], 2.0), 2.0, 1e-9))
    CHECK("given.mad_sigma", near(mad_sigma([0, 1, 2, 3, 4, 5, 100]), 2.9652, 1e-3))
    CHECK("given.truth", truth()["Ts_us"] == 10.0)


# ---- Part A
def test_a1_crossing():
    t = [0.0, 10.0, 20.0, 30.0, 40.0]
    v = [0.0, 1.0, 2.0, 3.0, 4.0]
    CHECK("a1.rise_interp", near(first_crossing(t, v, 1.5), 15.0, 1e-9))
    CHECK("a1.fall_interp", near(first_crossing(t, v[::-1], 1.5, rising=False), 25.0, 1e-9))
    CHECK("a1.level_on_sample", near(first_crossing(t, v, 2.0), 20.0, 1e-9))
    CHECK("a1.never", first_crossing(t, v, 9.0) is None)
    CHECK("a1.starts_above_is_none", first_crossing(t, [5.0, 5.0, 5.0, 5.0, 5.0], 1.0) is None)
    sp = [0.0, 0.9, 0.0, 0.0, 1.5, 1.5, 1.5]
    tt = [float(i) for i in range(len(sp))]
    CHECK("a1.hold_rejects_spike", near(first_crossing(tt, sp, 0.5, hold=2), 3.0 + 0.5 / 1.5, 1e-9),
          str(first_crossing(tt, sp, 0.5, hold=2)))
    CHECK("a1.no_hold_sees_spike", near(first_crossing(tt, sp, 0.5, hold=1), 0.5 / 0.9, 1e-9))
    CHECK("a1.hold_past_end_none", first_crossing([0.0, 1.0], [0.0, 1.0], 0.5, hold=3) is None)
    drop = [1.0, 1.0, 0.2, 1.0, 1.0, 0.0, 0.0]
    CHECK("a1.last_fall_skips_glitch", near(last_crossing(tt, drop, 0.5, False), 4.5, 1e-9),
          str(last_crossing(tt, drop, 0.5, False)))
    CHECK("a1.last_never_falls", last_crossing(tt, [1.0] * 7, 0.5, False) is None)


def test_a2_ramp():
    # 표: (Ts, T, nom, sigma). 허용오차 2*Ts = 보간 + 잡음/기울기에 충분(주석: 기대 오차 < Ts).
    cases = [(10.0, 400.0, 1.8, 0.0036), (10.0, 800.0, 0.8, 0.002), (5.0, 300.0, 3.3, 0.0066)]
    for i, (Ts, T, nom, sig) in enumerate(cases):
        t, v = ramp_wave(Ts, 600, 500.0, T, nom, sig, 11 + i)
        r = measure_ramp(t, v, 0.0, nom)
        CHECK("a2.ramp_case%d" % i, r is not None and near(r[2], 0.8 * T, 2 * Ts),
              "got %s want %.1f tol %.1f" % (r and round(r[2], 2), 0.8 * T, 2 * Ts))
    t, v = ramp_wave(10.0, 400, 500.0, 500.0, 3.3, 0.0, 3)
    r = measure_ramp(t, v, 0.0, 3.3)
    CHECK("a2.clean_exact", r is not None and near(r[2], 400.0, 1e-6), str(r))
    CHECK("a2.t10_t90", r is not None and near(r[0], 550.0, 1e-6) and near(r[1], 950.0, 1e-6), str(r))
    tf = [k * 1.0 for k in range(1000)]
    vf = [3.3 * math.exp(-x / 100.0) if x >= 0 else 3.3 for x in tf]
    rf = measure_ramp(tf, vf, 0.0, 3.3, rising=False)
    want = 100.0 * math.log(9.0)  # 지수 감쇠 90->10% = tau*ln 9
    CHECK("a2.falling_exponential", rf is not None and near(rf[2], want, 0.1), str(rf))
    t2, v2 = ramp_wave(10.0, 300, 500.0, 400.0, 1.8, 0.0, 5)
    CHECK("a2.never_reached", measure_ramp(t2, [x * 0.5 for x in v2], 0.0, 1.8) is None)


def test_a3_glitch():
    t = [float(k) for k in range(8)]
    v = [1.0, 1.0, 0.5, 0.5, 1.0, 1.0, 1.0, 1.0]
    g = detect_glitches(t, v, 0.9, 1.1)
    CHECK("a3.one_event", len(g) == 1, str(g))
    CHECK("a3.times_interp", near(g[0]["t0"], 1.2, 1e-9) and near(g[0]["t1"], 3.8, 1e-9), str(g[0]))
    CHECK("a3.width_extreme", near(g[0]["width"], 2.6, 1e-9) and g[0]["extreme"] == 0.5 and g[0]["samples"] == 2)
    g = detect_glitches(t, [1.0, 1.0, 1.3, 1.0, 1.0, 1.0, 1.0, 1.0], 0.9, 1.1)
    CHECK("a3.over_kind", len(g) == 1 and g[0]["kind"] == "over" and g[0]["extreme"] == 1.3, str(g))
    g = detect_glitches(t, [1.0, 0.5, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0], 0.9, 1.1, min_samples=2)
    CHECK("a3.min_samples_drops_one_sample", g == [], str(g))
    # 한계 부근에서 재진입했다 다시 나가도 히스테리시스 안이면 한 이벤트
    w = [1.0, 0.85, 0.92, 0.85, 1.0, 1.0, 1.0, 1.0]
    CHECK("a3.no_hyst_two_events", len(detect_glitches(t, w, 0.9, 1.1)) == 2)
    g = detect_glitches(t, w, 0.9, 1.1, hyst=0.05)
    CHECK("a3.hyst_merges", len(g) == 1 and g[0]["samples"] == 2, str(g))
    g = detect_glitches(t, [0.5, 0.5, 1.0, 1.0, 1.0, 1.0, 1.0, 0.5], 0.9, 1.1)
    CHECK("a3.truncated_both_ends", len(g) == 2 and g[0]["truncated"] and g[1]["truncated"], str(g))
    CHECK("a3.bad_limits", _raises(lambda: detect_glitches(t, v, 1.1, 0.9)))
    # 깨끗한 평탄부 + 잡음 → 이벤트 없음 (대역이 25 sigma 밖)
    rng = random.Random(7)
    vn = [1.8 + rng.gauss(0, 0.0036) for _ in range(2000)]
    CHECK("a3.noise_inside_band_quiet", detect_glitches(list(range(2000)), vn, 1.71, 1.89, hyst=0.018) == [])


def _raises(fn):
    try:
        fn()
    except ValueError:
        return True
    return False


def test_a4_rules():
    table = [
        (150.0, 100.0, None, 0.0, "PASS"), (100.0, 100.0, None, 0.0, "PASS"), (99.9, 100.0, None, 0.0, "FAIL"),
        (120.0, 100.0, None, 10.0, "PASS"), (105.0, 100.0, None, 10.0, "INDET"), (85.0, 100.0, None, 10.0, "FAIL"),
        (500.0, 100.0, 400.0, 10.0, "FAIL"), (395.0, 100.0, 400.0, 10.0, "INDET"), (300.0, 100.0, 400.0, 10.0, "PASS"),
        (-720.0, 100.0, None, 20.0, "FAIL"),
    ]
    for d, mn, mx, u, want in table:
        got = eval_rule(d, mn, mx, u)
        CHECK("a4.rule %s/%s/%s/u%s->%s" % (d, mn, mx, u, want), got == want, got)
    ok, err = dc_check(1.17, 1.10, 5.0)
    CHECK("a4.dc_fail", (not ok) and near(err, 6.3636, 1e-3), str(err))
    ok, err = dc_check(0.78, 0.80, 5.0)
    CHECK("a4.dc_pass_edge", ok and near(err, -2.5, 1e-9), str(err))


def test_a5_clock():
    CHECK("a5.ppm_plus", near(ppm_error(24000120, 24000000), 5.0, 1e-9))
    CHECK("a5.ppm_minus", near(ppm_error(99970000, 100000000), -300.0, 1e-6))
    exp = clock_tree_expected(CLOCK_TREE)
    CHECK("a5.cpu_exact", exp["CPU_CLK"] == 1200000000, str(exp["CPU_CLK"]))
    CHECK("a5.ddr_exact", exp["DDR_CK"] == 1056000000, str(exp["DDR_CK"]))
    CHECK("a5.apb_exact", exp["APB_CLK"] == 100000000, str(exp["APB_CLK"]))
    aud_err = abs(ppm_error(exp["AUD_MCLK"], 12288000))
    CHECK("a5.fracN_residual_small_nonzero", 0 < aud_err < 0.001, str(aud_err))
    bad = {"A": {"parent": "B", "div": 1}, "B": {"parent": "A", "div": 1}}
    CHECK("a5.cycle", _raises(lambda: clock_tree_expected(bad)))
    CHECK("a5.unknown_parent", _raises(lambda: clock_tree_expected({"A": {"parent": "Z", "div": 1}})))
    vco = {"X": {"hz": 24000000}, "P": {"parent": "X", "mul": 200, "div": 1, "vco": [800000000, 3200000000]}}
    CHECK("a5.vco_range", _raises(lambda: clock_tree_expected(vco)))
    rng = random.Random(17)
    worst, f = 0.0, 12288321.0  # f*gate가 정수면 양자화가 안 드러나서 일부러 비정수
    gate = 0.001
    for _ in range(2000):
        n = counter_read(f, gate, rng.random())
        worst = max(worst, abs(n / gate - f))
    CHECK("a5.counter_within_1_count", worst <= 1.0 / gate + 1e-6, str(worst))
    CHECK("a5.counter_uses_quantization", worst > 0.5 / gate, str(worst))  # 실제로 양자화가 드러난다
    CHECK("a5.gate_res_100M_1ms", near(gate_resolution_ppm(100e6, 0.001), 10.0, 1e-9))
    CHECK("a5.gate_res_24M_1ms", near(gate_resolution_ppm(24e6, 0.001), 41.6667, 1e-3))
    CHECK("a5.required_gate", near(required_gate_s(12.288e6, 0.1), 0.81380, 1e-4))


# ---- Part B
def test_b1_timestamps():
    Ts = 10.0
    t = [i * Ts for i in range(200)]
    r = check_timestamps(t, Ts)
    CHECK("b1.good", r["ok"] and r["Ts_us"] == 10.0 and r["issues"] == [], str(r["issues"][:2]))
    bad = t[:]
    bad[50] = bad[49] - 5.0
    r = check_timestamps(bad, Ts)
    CHECK("b1.backward_detected", not r["ok"] and r["issues"][0][0] == "nonmono" and r["issues"][0][1] == 50, str(r["issues"][:2]))
    bad = t[:]
    bad[80] = bad[79]
    r = check_timestamps(bad, Ts)
    CHECK("b1.duplicate_detected", not r["ok"] and any(x[0] == "nonmono" for x in r["issues"]), str(r["issues"][:3]))
    gap = t[:60] + [x + 40.0 for x in t[60:]]
    r = check_timestamps(gap, Ts)
    CHECK("b1.gap_detected", not r["ok"] and r["issues"][0][0] == "gap", str(r["issues"][:2]))
    rate = [i * 11.0 for i in range(200)]
    r = check_timestamps(rate, Ts)
    CHECK("b1.rate_mismatch", not r["ok"] and any(x[0] == "rate" for x in r["issues"]))
    rng = random.Random(23)
    jit = [i * Ts + rng.gauss(0, 0.2) for i in range(5000)]
    r = check_timestamps(jit, Ts)
    # dt의 표준편차 = sqrt(2)*sigma (연속 두 타임스탬프 잡음의 차) → 0.283, 허용 ±0.02
    CHECK("b1.jitter_measured_sqrt2", r["ok"] and near(r["jitter_us"], 0.2 * math.sqrt(2), 0.02), str(r["jitter_us"]))


def test_b2_uncertainty():
    CHECK("b2.u_formula", near(crossing_uncertainty(10.0, 0.01, 0.002, 0.1, 1.0), 5.0 + 1.0 + 3.0 * math.hypot(0.1, 0.2), 1e-9))
    CHECK("b2.u_bad_slope", _raises(lambda: crossing_uncertainty(10.0, 0.0, 0.1)))
    # 경험 검증: 참값 대비 오차가 u 안에 들어가는 비율 ≥ 99% (u는 '3 sigma + Ts/2' 설계, 0.99는 표본 300의 여유)
    Ts, T, nom, sig = 10.0, 400.0, 1.8, 0.0036
    level = 0.5 * nom
    slope = nom / T
    u = crossing_uncertainty(Ts, slope, sig)
    inside = 0
    worst = 0.0
    N = 300
    for s in range(N):
        t_on = 1000.0 + (s * 0.37) % Ts
        t, v = ramp_wave(Ts, 400, t_on, T, nom, sig, 5000 + s)
        tc = first_crossing(t, v, level, True, hold=3)
        err = abs(tc - (t_on + 0.5 * T))
        worst = max(worst, err)
        if err <= u:
            inside += 1
    CHECK("b2.empirical_coverage", inside / N >= 0.99, "%d/%d u=%.3f worst=%.3f" % (inside, N, u, worst))
    CHECK("b2.u_not_absurdly_loose", u < 8.0 * max(worst, 1.0), "u=%.3f worst=%.3f" % (u, worst))
    # 다채널 skew가 판정을 바꾼다: 실제로 동시인 레일이 skew 3us 때문에 어긋나 보임
    v_rule = eval_rule(3.0, 0.0, None, crossing_uncertainty(1.0, 0.05, 0.001, 0.0, 5.0))
    CHECK("b2.skew_makes_indet", v_rule == "INDET", v_rule)


def test_b3_ties():
    g = group_simultaneous({"A": 100.0, "B": 300.0, "C": 301.0, "D": 700.0}, 5.0)
    CHECK("b3.simple", g == [["A"], ["B", "C"], ["D"]], str(g))
    g = group_simultaneous({"C": 300.0, "B": 301.0}, 5.0)
    CHECK("b3.tie_sorted_by_name_not_time", g == [["B", "C"]], str(g))
    g = group_simultaneous({"A": 0.0, "B": 4.0, "C": 8.0}, 5.0)
    CHECK("b3.chain_is_nontransitive", g == [["A", "B", "C"]], str(g))
    g = group_simultaneous({"A": 0.0, "B": 6.0}, 5.0)
    CHECK("b3.outside_tie", g == [["A"], ["B"]], str(g))
    CHECK("b3.empty", group_simultaneous({}, 5.0) == [])


def test_b4_sampling_limits():
    Ts, D, lim = 10.0, 0.10, 0.05  # 깊이는 한계의 2배 → boxcar는 절반 이상 겹쳐야 보임
    grid = 1.0 / 1000.0
    table = [  # width/Ts, point, boxcar, peak (해석해)
        (0.25, 0.25, 0.00, 1.0), (0.50, 0.50, 0.50, 1.0), (0.75, 0.75, 0.75, 1.0),
        (1.00, 1.00, 1.00, 1.0), (1.50, 1.00, 1.00, 1.0),
    ]
    for wr, pt, bx, pk in table:
        w = wr * Ts
        a = detect_rate(w, D, Ts, lim, "point")
        b = detect_rate(w, D, Ts, lim, "boxcar")
        c = detect_rate(w, D, Ts, lim, "peak")
        tol = 2 * grid + 1e-9  # 위상 격자 해상도 때문의 경계 오차
        CHECK("b4.point_W%.2fTs" % wr, near(a, pt, tol), str(a))
        CHECK("b4.boxcar_W%.2fTs" % wr, near(b, bx, tol), str(b))
        CHECK("b4.peak_W%.2fTs" % wr, near(c, pk, tol), str(c))
    # 깊이 과소평가: boxcar는 폭비에 비례해 줄어든다
    CHECK("b4.boxcar_attenuates", near(acquire_depth(2.5, 0.10, 10.0, 7.0, "boxcar"), 0.025, 1e-9))
    CHECK("b4.bad_mode", _raises(lambda: acquire_depth(1, 1, 1, 0, "fft")))


def test_b5_noise_vs_threshold():
    rng = random.Random(29)
    N = 200000
    xs = [rng.gauss(0.0, 1.0) for _ in range(N)]
    tt = [float(i) for i in range(N)]
    for m in (3.0, 3.5):
        ev = len(detect_glitches(tt, xs, -m, 1e9))
        exp = N * Q(m)
        tol = 5.0 * math.sqrt(exp) + 2.0
        CHECK("b5.false_alarm_m%.1f" % m, abs(ev - exp) <= tol, "ev=%d exp=%.1f tol=%.1f" % (ev, exp, tol))
    ev3 = len(detect_glitches(tt, xs, -3.0, 1e9))
    ev2 = len(detect_glitches(tt, xs, -3.0, 1e9, min_samples=2))
    CHECK("b5.min_samples_kills_white_noise", ev2 <= ev3 / 20.0, "ev3=%d ev2=%d" % (ev3, ev2))
    # 한계에 딱 붙은 레일: 히스테리시스가 없으면 수백 번 채터링, 6 sigma면 소수 이벤트로 합쳐짐
    M = 2000
    ys = [rng.gauss(0.0, 1.0) for _ in range(M)]
    tt2 = [float(i) for i in range(M)]
    chatter = len(detect_glitches(tt2, ys, 0.0, 1e9))
    merged = len(detect_glitches(tt2, ys, 0.0, 1e9, hyst=6.0))
    CHECK("b5.chatter_without_hyst", chatter > 300, str(chatter))
    CHECK("b5.hyst_merges_chatter", merged <= 3, str(merged))


def test_b6_low_threshold_hold():
    # 임계값이 잡음(1.5 sigma) 안 → hold=1은 가짜 조기 통과, hold=5는 참값 근처
    Ts, T, nom, sig = 10.0, 400.0, 1.8, 0.002
    level = 1.5 * sig
    t, v = ramp_wave(Ts, 400, 2000.0, T, nom, sig, 41)
    naive = first_crossing(t, v, level, True, hold=1)
    held = first_crossing(t, v, level, True, hold=5)
    want = 2000.0 + level / (nom / T)
    CHECK("b6.naive_early", naive is not None and naive < want - 500.0, str(naive))
    CHECK("b6.hold_recovers", near(held, want, 3 * Ts), "%s want %.2f" % (held, want))


def test_b7_clock_guard():
    table = [(5.0, 30.0, 1.0, "PASS"), (29.5, 30.0, 1.0, "INDET"), (35.0, 30.0, 1.0, "FAIL"),
             (-29.5, 30.0, 1.0, "INDET"), (-35.0, 30.0, 1.0, "FAIL"), (0.0, 30.0, 40.0, "INDET")]
    for e, l, u, want in table:
        got = clock_verdict(e, l, u)
        CHECK("b7.verdict %s/%s/%s->%s" % (e, l, u, want), got == want, got)


# ---- Part C
_RES = {}


def res(name):
    if name not in _RES:
        _RES[name] = check_rails(load_log(data(name + ".csv")), SPEC)
    return _RES[name]


def fails_of(r, sev="FAIL"):
    return [f for f in r["findings"] if f["sev"] == sev]


def test_c1_good():
    r = res("board_good")
    CHECK("c1.good_pass", r["status"] == "PASS", format_report(r))
    CHECK("c1.good_no_findings", r["findings"] == [], str(r["findings"][:2]))
    want = [["VIN_12V"], ["VDD_0V8"], ["VDD_1V8"], ["VDD_1V1"], ["VDD_3V3"]]
    CHECK("c1.on_order", r["on_order"] == want, str(r["on_order"]))
    # 참값 10-90% = 0.8*T; 허용 2*Ts = 20us (기대 오차는 몇 us)
    for rail, T in (("VDD_1V8", 400.0), ("VDD_3V3", 400.0), ("VDD_0V8", 400.0), ("VIN_12V", 600.0)):
        m = r["meas"][rail]
        CHECK("c1.ramp_truth %s" % rail, near(m["ramp_us"], 0.8 * T, 20.0), str(m["ramp_us"]))
    # 잡음 추정: 참값 sigma=max(2mV, 0.2%nom), MAD 기반, 허용 25%
    for rail in r["meas"]:
        nom = SPEC["rails"][rail]["nom"]
        sig = max(0.002, nom * 0.002)
        CHECK("c1.sigma_truth %s" % rail, near(r["meas"][rail]["sigma_v"], sig, 0.25 * sig + 0.0005), str(r["meas"][rail]["sigma_v"]))


def test_c2_bad_order():
    T = truth()["logs"]["board_bad_order"]
    r = res("board_bad_order")
    f = fails_of(r)
    CHECK("c2.exactly_one_fail", len(f) == 1, str([x["check"] for x in f]))
    CHECK("c2.names_rule", f and f[0]["check"] == "order_on" and f[0]["a"] == T["a"] and f[0]["b"] == T["b"], str(f))
    CHECK("c2.delta_truth", f and near(f[0]["delta_us"], T["delta_us"], 20.0), str(f and f[0]["delta_us"]))
    want = [["VIN_12V"], ["VDD_0V8"], ["VDD_1V8"], ["VDD_3V3"], ["VDD_1V1"]]
    CHECK("c2.observed_order_shows_swap", r["on_order"] == want, str(r["on_order"]))


def test_c3_bad_ramp():
    T = truth()["logs"]["board_bad_ramp"]
    r = res("board_bad_ramp")
    f = fails_of(r)
    CHECK("c3.one_ramp_fail", len(f) == 1 and f[0]["check"] == "ramp" and f[0]["rail"] == T["rail"], str(f))
    CHECK("c3.ramp_truth", f and near(f[0]["ramp_us"], T["ramp_us"], 20.0), str(f and f[0]["ramp_us"]))


def test_c4_brownout():
    T = truth()["logs"]["board_bad_brownout"]
    r = res("board_bad_brownout")
    f = fails_of(r)
    CHECK("c4.two_glitches", len(f) == 2 and all(x["check"] == "glitch" for x in f), str([x["check"] for x in f]))
    prim = [x for x in f if "secondary_of" not in x]
    sec = [x for x in f if "secondary_of" in x]
    CHECK("c4.primary_is_parent", len(prim) == 1 and prim[0]["rail"] == T["primary"], str(prim))
    CHECK("c4.secondary_named", len(sec) == 1 and sec[0]["rail"] == T["secondary"] and sec[0]["secondary_of"] == T["primary"], str(sec))
    CHECK("c4.primary_listed_first", f and "secondary_of" not in f[0])
    if prim:
        g = prim[0]
        # 경계는 Ts 안에서 흔들리므로 시각·폭은 Ts+여유, 깊이는 잡음 2 sigma 수준(0.5%p) 여유
        CHECK("c4.t0_truth", near(g["t0"], T["t0_us"], 10.0), str(g["t0"]))
        CHECK("c4.width_truth", near(g["width"], T["width_us"], 20.0), str(g["width"]))
        CHECK("c4.depth_truth", near(g["depth_pct"], T["depth_pct"], 0.5), str(g["depth_pct"]))


def test_c5_power_down():
    T = truth()["logs"]["board_bad_powerdown"]
    r = res("board_bad_powerdown")
    f = fails_of(r)
    CHECK("c5.one_fail", len(f) == 1 and f[0]["check"] == "order_off", str(f))
    CHECK("c5.names_rule", f and f[0]["a"] == T["a"] and f[0]["b"] == T["b"], str(f))
    CHECK("c5.delta_truth", f and near(f[0]["delta_us"], T["delta_us"], 20.0), str(f and f[0]["delta_us"]))


def test_c6_dc_and_invalid():
    T = truth()["logs"]["board_bad_dc"]
    r = res("board_bad_dc")
    f = fails_of(r)
    CHECK("c6.dc_only", len(f) == 1 and f[0]["check"] == "dc" and f[0]["rail"] == T["rail"], str(f))
    CHECK("c6.dc_truth", f and near(f[0]["dc_v"], T["dc_v"], 0.005), str(f and f[0]["dc_v"]))
    r = res("board_bad_timestamps")
    CHECK("c6.invalid_not_fail", r["status"] == "INVALID" and len(fails_of(r)) == 0, r["status"])
    CHECK("c6.invalid_says_why", "nonmono" in r["findings"][0]["msg"], r["findings"][0]["msg"])


def test_c7_clocks():
    with open(data("clock_meas.json")) as f:
        boards = json.load(f)
    T = truth()["clock"]
    for name in sorted(boards):
        r = check_clocks(CLOCK_TREE, boards[name])
        want = T[name]
        CHECK("c7.%s.status" % name, r["status"] == want["status"], r["status"])
        got = [[x["node"], x["hint"]] for x in r["faults"]]
        CHECK("c7.%s.faults" % name, got == want["faults"], str(got))
    r = check_clocks(CLOCK_TREE, boards["bad_pll_ddr"])
    f = r["faults"][0]
    CHECK("c7.confounded_pll_vs_divider", f["confounded"] == ["PLL_DDR", "DDR_CK"], str(f["confounded"]))
    rows = {x["node"]: x for x in r["rows"]}
    CHECK("c7.bad_pll_ppm_truth", near(rows["DDR_CK"]["ppm"], (87.0 / 88.0 - 1) * 1e6 + 8.0, 1.0), str(rows["DDR_CK"]["ppm"]))
    r = check_clocks(CLOCK_TREE, boards["bad_xo"])
    CHECK("c7.xo_not_blamed_on_children", [x["node"] for x in r["faults"]] == ["XO_24M"], str(r["faults"]))
    rows = {x["node"]: x for x in r["rows"]}
    CHECK("c7.pcie_passes_while_xo_fails", rows["PCIE_REF"]["verdict"] == "PASS" and rows["XO_24M"]["verdict"] == "FAIL")
    r = check_clocks(CLOCK_TREE, boards["short_gate"])
    rows = {x["node"]: x for x in r["rows"]}
    CHECK("c7.short_gate_u_dominates", rows["XO_24M"]["u_ppm"] > 30.0 and rows["XO_24M"]["verdict"] == "INDET", str(rows["XO_24M"]))


def test_c8_limits():
    # 한계 1: Ts보다 좁은 glitch가 샘플 사이에 끼면 데이터에 흔적이 없다 → 체커는 PASS, 보고서가 그 사실을 말해야 한다
    log = load_log(data("board_good.csv"))
    rail = log.rails["VDD_1V8"]
    between = [1.8 * 0.85 if 6003.0 <= x < 6007.0 else y for x, y in zip(log.t, rail)]
    CHECK("c8.sub_Ts_glitch_leaves_no_trace", between == rail)
    r = check_rails(Log(log.t, dict(log.rails, VDD_1V8=between), "blind"), SPEC)
    CHECK("c8.sub_Ts_glitch_invisible", r["status"] == "PASS" and any("Ts=" in b for b in r["blind"]), str(r["blind"]))
    # 같은 폭의 glitch를 샘플 시각(약 6000us)에 맞추면 보인다 — 위상의 문제이지 깊이의 문제가 아니다
    hit = [1.8 * 0.85 if 5998.0 <= x < 6002.0 else y for x, y in zip(log.t, rail)]
    r = check_rails(Log(log.t, dict(log.rails, VDD_1V8=hit), "one_sample"), SPEC)
    f = fails_of(r)
    CHECK("c8.same_glitch_on_sample_detected", len(f) == 1 and f[0]["check"] == "glitch" and f[0]["samples"] == 1, str(f))
    # 한계 2: 판정 불가 경계 — 캡처가 끝나서 전원 차단 에지가 없으면 FAIL이 아니라 INDET
    cut = 700
    log4 = Log(log.t[:cut], {k: v[:cut] for k, v in log.rails.items()}, "truncated")
    r = check_rails(log4, SPEC)
    CHECK("c8.truncated_indet", r["status"] == "INDET" and any(f["check"] == "order_off" for f in r["findings"]), r["status"])
    # 한계 3: 샘플 주기가 must-see보다 길면 glitch 검사는 커버되지 않음
    log5 = load_log(data("board_good.csv"))
    spec2 = json.loads(json.dumps(SPEC))
    spec2["glitch_must_see_us"] = 5.0
    r = check_rails(log5, spec2)
    CHECK("c8.coverage_indet", r["status"] == "INDET" and any(f["check"] == "coverage" for f in r["findings"]), r["status"])
    # 한계 4: 공통모드 편차는 XO 고장으로 보고되지만, 측정 타임베이스 고장과는 데이터만으로 구별 못 한다
    CHECK("c8.report_renders", "STATUS=PASS" in format_report(check_rails(load_log(data("board_good.csv")), SPEC)))
    with open(data("clock_meas.json")) as f:
        boards = json.load(f)
    r = check_clocks(CLOCK_TREE, boards["bad_xo"])
    CHECK("c8.common_mode_reported_at_root", r["faults"] and r["faults"][0]["node"] == "XO_24M" and not r["faults"][0]["confounded"], str(r["faults"]))


def print_reports():
    for name in ["board_good", "board_bad_order", "board_bad_ramp", "board_bad_brownout",
                 "board_bad_powerdown", "board_bad_dc", "board_bad_timestamps"]:
        print(format_report(check_rails(load_log(data(name + ".csv")), SPEC)))
        print()
    with open(data("clock_meas.json")) as f:
        boards = json.load(f)
    for name in sorted(boards):
        print(format_clock_report(name, check_clocks(CLOCK_TREE, boards[name])))
        print()


def main():
    if "--report" in sys.argv:
        print_reports()
        return 0
    for fn in (test_given_tools, test_a1_crossing, test_a2_ramp, test_a3_glitch, test_a4_rules, test_a5_clock,
               test_b1_timestamps, test_b2_uncertainty, test_b3_ties, test_b4_sampling_limits,
               test_b5_noise_vs_threshold, test_b6_low_threshold_hold, test_b7_clock_guard,
               test_c1_good, test_c2_bad_order, test_c3_bad_ramp, test_c4_brownout, test_c5_power_down,
               test_c6_dc_and_invalid, test_c7_clocks, test_c8_limits):
        run(fn)
    print("%d/%d passed" % (_pass, _pass + _fail))
    return 0 if _fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
