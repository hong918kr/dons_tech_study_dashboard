#!/usr/bin/env python3
"""HWLAB-B03 I2C 풀업 저항 계산: 버스 커패시턴스·Rise Time

  Part A  UM10204 스펙 표(tr max, VOL@IOL, Cb max)로 Rp(min)/Rp(max) 창을 계산하고,
          Cb 50~400 pF 를 훑어 창이 닫히는 지점을 찾는다. RC 에지에서 30–70 % rise time 을 잰다.
  Part B  측정이 거짓말하는 경우: 10–90 % 기본값, 샘플 간격 양자화, 잡음 다중 교차, 잘린 캡처, 1.8 V 의 VOL 규칙.
  Part C  가상 보드 SNS-EVT1 의 스코프 캡처 4개(captures/*.csv) + 회로도 값(buses.json)
          → 버스별 tr·VOL·추정 Cb·추정 Rp → PASS/FAIL 리포트와 조치(E24 값 추천, DNP/이중 풀업 의심).

  python3 -I solution.py            # 테스트 ("N/M passed", 실패 시 exit 1) + 리포트
Python 3 표준 라이브러리만 쓴다. 단위는 SI (s, F, Ω, V, A).
"""
import csv
import json
import math
import os
import random
import statistics
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# 30 % → 70 % 사이를 지나는 데 걸리는 시간 = RC·ln(0.7/0.3) = 0.8473·RC (UM10204 식 2 의 상수)
K_30_70 = math.log(0.7 / 0.3)

# NXP UM10204 (I2C-bus specification) "Characteristics of the SDA and SCL bus lines" 표에서 가져온 값
#   tr_max : SDA/SCL rise time 최대 (0.3·VDD → 0.7·VDD)      tf_max : fall time 최대
#   cb_max : 버스 라인당 커패시티브 부하 최대                 vol/iol: VOL1 (VDD > 2 V) 과 그때의 sink 전류
SPEC = {
    "Sm":  {"f_hz": 100e3, "tr_max": 1000e-9, "tf_max": 300e-9, "cb_max": 400e-12, "vol": 0.4, "iol": 3e-3},
    "Fm":  {"f_hz": 400e3, "tr_max": 300e-9,  "tf_max": 300e-9, "cb_max": 400e-12, "vol": 0.4, "iol": 3e-3},
    "Fm+": {"f_hz": 1e6,   "tr_max": 120e-9,  "tf_max": 120e-9, "cb_max": 550e-12, "vol": 0.4, "iol": 20e-3},
}
# VDD ≤ 2 V 일 때는 VOL2 = 0.2·VDD @ 2 mA (UM10204 표의 VOL2 행). Sm 열은 비어 있어 이 레슨은 같은 값을 가정한다.
LOW_VDD = 2.0

# Cb 예산은 ±30 % 정도밖에 못 믿는다. 그보다 크게(1.6배) 어긋나면 Rp 자체를 의심한다 (DNP·이중 풀업은 2배 이상)
SUSPECT_RATIO = 1.6

E24 = (1.0, 1.1, 1.2, 1.3, 1.5, 1.6, 1.8, 2.0, 2.2, 2.4, 2.7, 3.0,
       3.3, 3.6, 3.9, 4.3, 4.7, 5.1, 5.6, 6.2, 6.8, 7.5, 8.2, 9.1)


# ---------------------------------------------------------------- Part A: 스펙 → Rp 창
def vol_iol(mode, vdd):
    """(VOL max, 그때의 IOL) — VDD > 2 V 면 표의 VOL1, VDD ≤ 2 V 면 VOL2 = 0.2·VDD @ 2 mA"""
    # TODO: VDD ≤ LOW_VDD 이면 (0.2·VDD, 2 mA), 아니면 SPEC[mode] 의 (vol, iol)
    return None


def rp_min(mode, vdd):
    """가장 약한 디바이스가 IOL 을 sink 하면서 VOL 이하로 끌어내릴 수 있는 최소 풀업 (UM10204 식 1)"""
    # TODO: (VDD − VOL) / IOL  — vol_iol() 사용
    return None


def rp_max(mode, cb):
    """Cb 를 tr_max 안에 0.3·VDD → 0.7·VDD 로 충전할 수 있는 최대 풀업 (UM10204 식 2)"""
    # TODO: tr_max / (K_30_70 · Cb)
    return None


def rp_window(mode, vdd, cb):
    """(Rp_min, Rp_max). Rp_min > Rp_max 이면 창이 닫힌 것 (어떤 저항으로도 불가)"""
    return rp_min(mode, vdd), rp_max(mode, cb)


def cb_close(mode, vdd):
    """Rp_min == Rp_max 가 되는 Cb (해석해). 이보다 크면 저항 풀업만으로는 스펙을 못 맞춘다"""
    # TODO: rp_min == rp_max 를 Cb 에 대해 풀어라
    return None


def sweep_close(mode, vdd, cbs):
    """cbs(오름차순) 중 창이 처음 닫히는 Cb. 끝까지 열려 있으면 None"""
    # TODO: rp_window 로 lo > hi 인 첫 cb
    return None


# ---------------------------------------------------------------- 합성 파형 (주어진 코드)
def rc_pulse(vdd, rp, c, dt, n, t_fall, t_rise, ron=40.0, v_low=None, noise=0.0, seed=1, t0=0.0):
    """오픈드레인 low 펄스 1개: VDD → (t_fall) → VOL → (t_rise) → RC 로 VDD 복귀. (t 목록, v 목록)"""
    rng = random.Random(seed)
    vol = vdd * ron / (ron + rp) if v_low is None else v_low
    tau_f = (ron * rp / (ron + rp)) * c
    v0 = vol + (vdd - vol) * math.exp(-(t_rise - t_fall) / tau_f)
    ts, vs = [], []
    for i in range(n):
        t = t0 + i * dt
        if t < t_fall:
            v = vdd
        elif t < t_rise:
            v = vol + (vdd - vol) * math.exp(-(t - t_fall) / tau_f)
        else:
            v = vdd - (vdd - v0) * math.exp(-(t - t_rise) / (rp * c))
        ts.append(t)
        vs.append(v + (rng.gauss(0.0, noise) if noise else 0.0))
    return ts, vs


def naive_rise_time(t, v, lo, hi):
    """흔한 첫 구현: low 상태(lo 아래)에 들어간 뒤, lo 이상인 첫 샘플과 hi 이상인 첫 샘플의 시간 차.
    보간 없음, 잡음 처리 없음, 첫 에지 하나만."""
    s = next(k for k, x in enumerate(v) if x < lo)
    i = next(k for k in range(s, len(v)) if v[k] >= lo)
    j = next(k for k in range(i, len(v)) if v[k] >= hi)
    return t[j] - t[i]


# ---------------------------------------------------------------- Part A/B: rise time 측정
def moving_average(v, k):
    """중심 이동평균(boxcar), 홀수 k. 양 끝은 창을 줄여서 평균 (길이 유지)"""
    # TODO: k ≤ 1 이면 복사본. 아니면 누적합(prefix sum)으로 [i-k//2, i+k//2] 평균 (양 끝은 창을 잘라서)
    return list(v)


def cross_time(t0, v0, t1, v1, level):
    """(t0,v0)-(t1,v1) 선분이 level 을 지나는 시각 (선형 보간)"""
    return None  # TODO: 선형 보간


def measure_rise_times(t, v, vdd, lo_frac=0.3, hi_frac=0.7, smooth=1):
    """상승 에지마다 (hi 교차 시각) − (그 직전 마지막 lo 상향 교차 시각). 단위 s, 목록.
    - lo 아래로 내려가야 무장(arm) → 잡음으로 hi 근처를 오르내려도 에지 1개로 센다.
    - 교차 시각은 선형 보간 → 샘플 간격(dt) 양자화 오차 제거.
    - smooth>1 이면 중심 이동평균 후 측정 (RC 지수 에지에서는 30–70 간격을 바꾸지 않는다: 풀이 참고).
    - hi 에 도달하지 못한 (잘린) 에지는 결과에 넣지 않는다."""
    lo, hi = lo_frac * vdd, hi_frac * vdd
    vs = moving_average(v, smooth)
    out = []
    # TODO: 인접 샘플 (a, b) 를 훑으며
    #   b < lo           → 무장(armed), t_lo 초기화
    #   armed, a<lo<=b   → t_lo = 보간한 lo 교차 시각 (마지막 것으로 덮어쓰기)
    #   armed, a<hi<=b   → out 에 (hi 교차 시각 − t_lo) 추가, 무장 해제
    return None


def levels(v, vdd):
    """(low, high) 상태 레벨 — VDD/2 아래/위 샘플의 중앙값 (정식 방법은 IEEE 181 히스토그램, HWLAB-B04)"""
    low = [x for x in v if x < 0.5 * vdd]
    high = [x for x in v if x >= 0.5 * vdd]
    return (statistics.median(low) if low else None, statistics.median(high) if high else None)


# ---------------------------------------------------------------- Part C: 보드 캡처 분석
def load_capture(name):
    """captures/<name>.csv ('#' 주석 줄, 헤더 t_ns,v) → (t[s], v[V])"""
    ts, vs = [], []
    with open(os.path.join(HERE, "captures", name + ".csv"), encoding="utf-8") as f:
        rows = csv.reader(line for line in f if not line.startswith("#"))
        next(rows)
        for r in rows:
            ts.append(float(r[0]) * 1e-9)
            vs.append(float(r[1]))
    return ts, vs


def load_buses():
    with open(os.path.join(HERE, "buses.json"), encoding="utf-8") as f:
        return json.load(f)


def cb_budget(b):
    """회로도 기반 버스 커패시턴스 예산 [F] (프로브 제외)"""
    return (b["devices"] * b["ci_pF"] + b["trace_cm"] * b["pf_per_cm"] + b["extra_pF"]) * 1e-12


def choose_e24(mode, vdd, cb, tol=0.01, cb_margin=0.2):
    """창 [Rp_min, Rp_max(Cb·(1+margin))] 안에 저항 공차까지 들어가는 E24 값 중 가장 큰 것. 없으면 None.
    가장 큰 값 = 정적 전류(VDD/Rp)와 VOL 이 가장 작으면서 tr 마진(Cb +20 %, R +1 %)을 지키는 값."""
    # TODO: 100 Ω ~ 91 kΩ 의 E24 값 r 중 r(1−tol) ≥ Rp_min 이고 r(1+tol) ≤ Rp_max(Cb·(1+margin)) 인 최댓값
    return None


def analyze_bus(ch, t, v, probe_f, smooth=5):
    """한 채널 캡처 + 회로도 값 → 측정·추정·판정 dict
    tr_meas  : 측정 30–70 % rise time (여러 에지면 중앙값)
    tr_bus   : 프로브 C 를 뺀 버스 자체 rise time  = tr_meas − K·Rp_sch·C_probe
    cb_impl  : 회로도 Rp 를 믿을 때의 버스 C       = tr_meas/(K·Rp_sch) − C_probe
    rp_impl  : Cb 예산을 믿을 때의 실효 Rp          = tr_meas/(K·(Cb_budget + C_probe))
    flags    : FAIL_TR, FAIL_VOL, SUSPECT_RP_HIGH(cb_impl > 1.6×예산), SUSPECT_RP_LOW(cb_impl < 예산/1.6)
    rec      : FAIL_TR 이고 의심 플래그가 없으면 choose_e24 추천값
    window   : Rp 창 — SUSPECT 면 cb_impl 을 못 믿으므로 Cb 예산으로, 아니면 cb_impl 로"""
    # TODO: 1) measure_rise_times(smooth=smooth) → 없으면 {"name":…, "flags":["NO_EDGE"], "verdict":"FAIL"}
    #       2) tr_meas = 중앙값, levels() 로 low/high, vol_iol() 로 vol_max, cb_budget(ch["budget"])
    #       3) tr_bus / cb_impl / rp_impl 계산 (docstring 식), flags 판정 (SUSPECT_RATIO 사용)
    #       4) FAIL_TR 이고 SUSPECT 가 없으면 rec = choose_e24(mode, vdd, cb_impl)
    #       반환 dict 키: name mode vdd rp n_edges tr_meas tr_bus low high vol_max cb_impl cb_budget rp_impl
    #                    window flags rec verdict("PASS"/"FAIL")
    raise NotImplementedError("analyze_bus")


# ---------------------------------------------------------------- 리포트 (주어진 코드)
def fmt_r(r):
    return "%.0f" % r if r < 1000 else ("%.1fk" % (r / 1000)).replace(".0k", "k")


def format_report(a, ch):
    if a["flags"] == ["NO_EDGE"]:
        return "%-9s FAIL NO_EDGE (capture never crossed 0.3/0.7*VDD)" % a["name"]
    tr_max = SPEC[a["mode"]]["tr_max"]
    lo, hi = a["window"]
    s = ("%-9s %-4s %.2fV %-3s Rp(sch)=%s %s: tr=%.1fns (bus %.1fns, max %.0fns) VOL=%.3fV (max %.2fV) "
         "Cb~%.0fpF (budget %.0fpF) Rp_eff~%s window[%s, %s]@%s"
         % (a["name"], a["verdict"], a["vdd"], a["mode"], fmt_r(a["rp"]), ch["rp_ref"],
            a["tr_meas"] * 1e9, a["tr_bus"] * 1e9, tr_max * 1e9, a["low"], a["vol_max"],
            a["cb_impl"] * 1e12, a["cb_budget"] * 1e12, fmt_r(a["rp_impl"]), fmt_r(lo), fmt_r(hi),
            "budget" if any(f.startswith("SUSPECT") for f in a["flags"]) else "Cb~"))
    for f in a["flags"]:
        if f == "FAIL_TR":
            s += "\n    FAIL_TR  bus tr %.0fns > %.0fns" % (a["tr_bus"] * 1e9, tr_max * 1e9)
            if a["rec"]:
                k = K_30_70 * a["rec"]
                s += " -> %s: %s -> %s (E24 1%%, Cb+20%%): tr~%.0fns, worst %.0fns" % (
                    ch["rp_ref"], fmt_r(a["rp"]), fmt_r(a["rec"]), k * a["cb_impl"] * 1e9,
                    k * 1.01 * a["cb_impl"] * 1.2 * 1e9)
        elif f == "FAIL_VOL":
            s += "\n    FAIL_VOL low level %.3fV > %.2fV (noise margin, HWLAB-B02)" % (a["low"], a["vol_max"])
        elif f == "SUSPECT_RP_HIGH":
            s += ("\n    SUSPECT_RP_HIGH implied Cb %.0fpF >> budget %.0fpF; Rp_eff~%s vs %s -> %s populated? (DNP, internal pull-up only)"
                  % (a["cb_impl"] * 1e12, a["cb_budget"] * 1e12, fmt_r(a["rp_impl"]), fmt_r(a["rp"]), ch["rp_ref"]))
        elif f == "SUSPECT_RP_LOW":
            s += ("\n    SUSPECT_RP_LOW implied Cb %.0fpF << budget %.0fpF; Rp_eff~%s < Rp(sch) %s -> second pull-up in parallel?"
                  % (a["cb_impl"] * 1e12, a["cb_budget"] * 1e12, fmt_r(a["rp_impl"]), fmt_r(a["rp"])))
    return s


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


def near(x, ref, rel):
    return x is not None and abs(x - ref) <= rel * abs(ref)


def show(label, x, ref, unit=""):
    """측정값 vs 기대값을 한 줄로 (테스트 이름에 같이 찍는다)"""
    try:
        return "%s = %.4g%s (expect %.4g%s)" % (label, x, unit, ref, unit)
    except TypeError:
        return "%s = %r (expect %.4g%s)" % (label, x, ref, unit)


def safe(fn, *a, **kw):
    try:
        return fn(*a, **kw)
    except Exception:  # noqa: BLE001
        return None


def run_tests():
    print("-- Part A: spec window --")
    check("K = ln(0.7/0.3) = 0.8473", lambda: abs(K_30_70 - 0.8473) < 5e-5)
    x = safe(rp_min, "Fm", 3.3)
    check(show("Rp_min(Fm, 3.3V)", x, 966.67, "ohm"), lambda: near(x, 966.67, 1e-3))
    x = safe(rp_min, "Fm+", 3.3)
    check(show("Rp_min(Fm+, 3.3V)", x, 145.0, "ohm"), lambda: near(x, 145.0, 1e-3))
    x = safe(rp_max, "Fm", 100e-12)
    check(show("Rp_max(Fm, 100pF)", x, 3540.6, "ohm"), lambda: near(x, 3540.6, 1e-3))
    x = safe(rp_max, "Sm", 400e-12)
    check(show("Rp_max(Sm, 400pF)", x, 2950.5, "ohm"), lambda: near(x, 2950.5, 1e-3))
    x = safe(cb_close, "Fm", 3.3)
    check(show("cb_close(Fm, 3.3V)", x and x * 1e12, 366.3, "pF"), lambda: near(x, 366.3e-12, 2e-3))
    cbs = [c * 1e-12 for c in range(50, 401, 10)]
    x = safe(sweep_close, "Fm", 3.3, cbs)
    check(show("sweep 50..400pF Fm 3.3V closes at", x and x * 1e12, 370, "pF"), lambda: near(x, 370e-12, 1e-6))
    x = safe(sweep_close, "Fm", 5.0, cbs)
    check(show("sweep Fm 5.0V closes at", x and x * 1e12, 240, "pF"), lambda: near(x, 240e-12, 1e-6))
    check("sweep Sm 3.3V: window open up to 400pF (None)", lambda: safe(sweep_close, "Sm", 3.3, cbs) is None
          and safe(cb_close, "Sm", 3.3) is not None)
    check("sweep Fm+ 3.3V: open up to Cb max 550pF (None)",
          lambda: safe(sweep_close, "Fm+", 3.3, [c * 1e-12 for c in range(50, 551, 10)]) is None)
    t, v = rc_pulse(3.3, 2200, 100e-12, 1e-9, 2000, 100e-9, 300e-9)
    x = safe(measure_rise_times, t, v, 3.3)
    tr = x[0] if x else None
    check(show("RC edge 2.2k*100pF: 30-70 tr", tr and tr * 1e9, 186.41, "ns"), lambda: near(tr, 186.41e-9, 2e-3))

    print("-- Part B: why the naive number lies --")
    t, v = rc_pulse(3.3, 2950, 100e-12, 1e-9, 3000, 100e-9, 300e-9)
    a = safe(measure_rise_times, t, v, 3.3)
    b = safe(measure_rise_times, t, v, 3.3, 0.1, 0.9)
    ok = bool(a) and bool(b)
    check(show("30-70 tr of Fm edge", a and a[0] * 1e9, 249.95, "ns") + " <= 300ns",
          lambda: near(a[0], 249.95e-9, 2e-3) and a[0] <= 300e-9)
    check(show("scope default 10-90 of SAME edge", b and b[0] * 1e9, 648.2, "ns") + " -> false FAIL",
          lambda: near(b[0], 648.2e-9, 2e-3) and b[0] > 300e-9)
    check(show("ratio 10-90/30-70", b[0] / a[0] if ok else None, math.log(9) / K_30_70),
          lambda: near(b[0] / a[0], math.log(9) / K_30_70, 2e-3))
    t1, v1 = rc_pulse(3.3, 2200, 100e-12, 1e-9, 2000, 100e-9, 300e-9, v_low=0.0)
    t2, v2 = rc_pulse(3.3, 2200, 100e-12, 1e-9, 2000, 100e-9, 300e-9, v_low=0.6)
    a, b = safe(measure_rise_times, t1, v1, 3.3), safe(measure_rise_times, t2, v2, 3.3)
    check("start level 0.0V vs 0.6V -> same 30-70 tr (exponential)",
          lambda: abs(a[0] - b[0]) < 0.5e-9 and near(a[0], 186.41e-9, 2e-3))
    # 샘플 간격 20 ns (50 MS/s), τ = 200 ns. 20가지 위상으로 에지를 옮겨 가며 오차 최대값
    ref = K_30_70 * 2000 * 100e-12
    e_naive, e_interp = 0.0, 0.0
    for ph in range(20):
        t, v = rc_pulse(3.3, 2000, 100e-12, 20e-9, 120, 100e-9, 400e-9, t0=-ph * 1e-9)
        e_naive = max(e_naive, abs(naive_rise_time(t, v, 0.99, 2.31) - ref))
        m = safe(measure_rise_times, t, v, 3.3)
        e_interp = max(e_interp, abs(m[0] - ref) if m else 1.0)
    check("dt=20ns: naive max err %.1fns > 5ns, interpolated %.2fns < 1ns" % (e_naive * 1e9, e_interp * 1e9),
          lambda: e_naive > 5e-9 and e_interp < 1e-9)
    # 느린 Sm 에지(τ = 1 µs) + 잡음 σ = 40 mV, dt = 2 ns. 10개 seed 의 상대 오차 [%]
    ref = K_30_70 * 10000 * 100e-12
    en, eh, es = [], [], []
    for seed in range(10):
        t, v = rc_pulse(3.3, 10000, 100e-12, 2e-9, 4000, 100e-9, 500e-9, noise=0.040, seed=seed)
        en.append((naive_rise_time(t, v, 0.99, 2.31) - ref) / ref * 100)
        m = safe(measure_rise_times, t, v, 3.3, smooth=1)
        eh.append((m[0] - ref) / ref * 100 if m and len(m) == 1 else 100.0)
        m = safe(measure_rise_times, t, v, 3.3, smooth=101)   # 101 샘플 = 202 ns ≈ 0.2 τ
        es.append((m[0] - ref) / ref * 100 if m and len(m) == 1 else 100.0)
    check("noise 40mV: naive err %+.1f..%+.1f%%, raw last-cross bias %+.1f%%, boxcar 0.2tau max|err| %.2f%% < 1.2%%"
          % (min(en), max(en), statistics.mean(eh), max(abs(e) for e in es)),
          lambda: max(abs(e) for e in en) > 3 and statistics.mean(eh) < -5 and max(abs(e) for e in es) < 1.2)
    t, v = rc_pulse(3.3, 10000, 100e-12, 2e-9, 4000, 100e-9, 500e-9, noise=0.040, seed=3)
    m = safe(measure_rise_times, t, v, 3.3, smooth=1)
    raw = sum(1 for a, b in zip(v, v[1:]) if a < 2.31 <= b)
    check("noise 40mV: %d raw up-crossings of 0.7*VDD, but exactly 1 edge (arm below 0.3*VDD)" % raw,
          lambda: raw > 1 and m is not None and len(m) == 1)
    t, v = rc_pulse(3.3, 2200, 100e-12, 1e-9, 380, 100e-9, 300e-9)
    check("truncated capture (ends at %.2fV < 0.7*VDD) -> [] not a fake number" % v[-1],
          lambda: safe(measure_rise_times, t, v, 3.3) == [])
    t, v = [], []
    for k in range(3):
        tk, vk = rc_pulse(3.3, 2200, 100e-12, 1e-9, 2000, k * 2e-6 + 100e-9, k * 2e-6 + 300e-9, t0=k * 2e-6)
        t += tk
        v += vk
    m = safe(measure_rise_times, t, v, 3.3)
    check("3 pulses -> 3 rise times", lambda: m is not None and len(m) == 3 and all(near(x, 186.41e-9, 2e-3) for x in m))
    x = safe(rp_min, "Fm", 1.8)
    check(show("Rp_min(Fm, 1.8V) uses VOL2=0.2*VDD@2mA", x, 720.0, "ohm") + "; naive 0.4V@3mA = 466.7",
          lambda: near(x, 720.0, 1e-6) and abs((1.8 - 0.4) / 3e-3 - 466.67) < 0.01)

    print("-- Part C: SNS-EVT1 bring-up captures --")
    cfg = load_buses()
    probe = cfg["probe_pF"] * 1e-12
    res = {}
    for ch in cfg["channels"]:
        t, v = load_capture(ch["name"])
        res[ch["name"]] = (safe(analyze_bus, ch, t, v, probe), ch)
    A = {k: r[0] or {} for k, r in res.items()}
    x = A["I2C0_SCL"].get("tr_meas")
    check(show("I2C0 tr_meas", x and x * 1e9, 167.8, "ns"), lambda: near(x, 167.8e-9, 0.03))
    x = A["I2C0_SCL"].get("cb_impl")
    check(show("I2C0 implied Cb", x and x * 1e12, 78, "pF"), lambda: near(x, 78e-12, 0.05))
    check("I2C0 PASS (no flags)", lambda: A["I2C0_SCL"]["verdict"] == "PASS")
    check("I2C1 FAIL_TR, recommend R140 = 1.6k",
          lambda: A["I2C1_SCL"]["flags"] == ["FAIL_TR"] and A["I2C1_SCL"]["rec"] == 1600.0)
    x = A["I2C2_SCL"].get("rp_impl")
    check(show("I2C2 SUSPECT_RP_HIGH, Rp_eff", x, 40000, "ohm") + " (R230 DNP?)",
          lambda: "SUSPECT_RP_HIGH" in A["I2C2_SCL"]["flags"] and "FAIL_TR" in A["I2C2_SCL"]["flags"]
          and near(x, 40000, 0.25) and A["I2C2_SCL"]["rec"] is None)
    x = A["I2C3_SDA"].get("low")
    check(show("I2C3 FAIL_VOL low level", x, 0.595, "V"),
          lambda: "FAIL_VOL" in A["I2C3_SDA"]["flags"] and near(x, 0.595, 0.05))
    x = A["I2C3_SDA"].get("rp_impl")
    check(show("I2C3 SUSPECT_RP_LOW, Rp_eff", x, 500, "ohm") + " < Rp_min 967 (double pull-up?)",
          lambda: "SUSPECT_RP_LOW" in A["I2C3_SDA"]["flags"] and near(x, 500, 0.25) and x < rp_min("Fm", 3.3))
    check("exactly 1 of 4 buses PASS", lambda: sum(a.get("verdict") == "PASS" for a in A.values()) == 1)
    print("")
    for k in sorted(res):
        a, ch = res[k]
        print(safe(format_report, a, ch) if a else "%-9s (analyze_bus not implemented)" % k)
    print("")
    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


if __name__ == "__main__":
    sys.exit(0 if run_tests() else 1)
