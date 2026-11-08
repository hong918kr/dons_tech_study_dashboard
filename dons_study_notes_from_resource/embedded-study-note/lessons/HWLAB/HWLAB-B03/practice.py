#!/usr/bin/env python3
"""HWLAB-B03 추가 연습 — 같은 스펙 표와 같은 캡처로 한 걸음 더

  연습 1  measure_fall_times()   : 0.7·VDD → 0.3·VDD fall time. 최종값이 VOL(≠0)이면 0.8473·τ 가 아니다
  연습 2  cb_limit_current_src() : 저항 대신 정전류 풀업이면 Fm 3.3 V 에서 Cb 를 어디까지 버티나
  연습 3  split_plan()           : 520 pF 버스를 버퍼로 나누면 세그먼트마다 E24 풀업이 생긴다
  연습 4  fit_tau()              : ln(Vhigh − v) 직선 맞춤으로 τ 를 구해 임계값 방식 tr 과 교차 검증 (Vhigh 는 히스토그램 최빈값)

  python3 -I practice.py
"""
import csv
import math
import os
import statistics
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# ---- 본문(solution.py)에서 필요한 부분만 옮겨 왔다 (레슨 파일끼리 import 하지 않는다) ----
K_30_70 = math.log(0.7 / 0.3)
SPEC = {
    "Sm":  {"tr_max": 1000e-9, "tf_max": 300e-9, "cb_max": 400e-12, "vol": 0.4, "iol": 3e-3},
    "Fm":  {"tr_max": 300e-9,  "tf_max": 300e-9, "cb_max": 400e-12, "vol": 0.4, "iol": 3e-3},
    "Fm+": {"tr_max": 120e-9,  "tf_max": 120e-9, "cb_max": 550e-12, "vol": 0.4, "iol": 20e-3},
}
E24 = (1.0, 1.1, 1.2, 1.3, 1.5, 1.6, 1.8, 2.0, 2.2, 2.4, 2.7, 3.0,
       3.3, 3.6, 3.9, 4.3, 4.7, 5.1, 5.6, 6.2, 6.8, 7.5, 8.2, 9.1)


def vol_iol(mode, vdd):
    return (0.2 * vdd, 2e-3) if vdd <= 2.0 else (SPEC[mode]["vol"], SPEC[mode]["iol"])


def rp_min(mode, vdd):
    vol, iol = vol_iol(mode, vdd)
    return (vdd - vol) / iol


def rp_max(mode, cb):
    return SPEC[mode]["tr_max"] / (K_30_70 * cb)


def cb_close(mode, vdd):
    vol, iol = vol_iol(mode, vdd)
    return SPEC[mode]["tr_max"] * iol / (K_30_70 * (vdd - vol))


def choose_e24(mode, vdd, cb, tol=0.01, cb_margin=0.2):
    lo, hi, best = rp_min(mode, vdd), rp_max(mode, cb * (1.0 + cb_margin)), None
    for dec in (100.0, 1000.0, 10000.0):
        for m in E24:
            r = round(m * dec, 6)
            if r * (1.0 - tol) >= lo and r * (1.0 + tol) <= hi:
                best = r if best is None or r > best else best
    return best


def cross_time(t0, v0, t1, v1, level):
    return t0 + (level - v0) * (t1 - t0) / (v1 - v0)


def moving_average(v, k):
    if k <= 1:
        return list(v)
    h, pre, n = k // 2, [0.0], len(v)
    for x in v:
        pre.append(pre[-1] + x)
    return [(pre[min(n, i + h + 1)] - pre[max(0, i - h)]) / (min(n, i + h + 1) - max(0, i - h)) for i in range(n)]


def measure_rise_times(t, v, vdd, lo_frac=0.3, hi_frac=0.7, smooth=1):
    lo, hi = lo_frac * vdd, hi_frac * vdd
    vs, out, armed, t_lo = moving_average(v, smooth), [], False, None
    for i in range(1, len(vs)):
        a, b = vs[i - 1], vs[i]
        if b < lo:
            armed, t_lo = True, None
            continue
        if not armed:
            continue
        if a < lo <= b:
            t_lo = cross_time(t[i - 1], a, t[i], b, lo)
        if a < hi <= b and t_lo is not None:
            out.append(cross_time(t[i - 1], a, t[i], b, hi) - t_lo)
            armed, t_lo = False, None
    return out


def levels(v, vdd):
    low = [x for x in v if x < 0.5 * vdd]
    high = [x for x in v if x >= 0.5 * vdd]
    return statistics.median(low), statistics.median(high)


def load_capture(name):
    ts, vs = [], []
    with open(os.path.join(HERE, "captures", name + ".csv"), encoding="utf-8") as f:
        rows = csv.reader(line for line in f if not line.startswith("#"))
        next(rows)
        for r in rows:
            ts.append(float(r[0]) * 1e-9)
            vs.append(float(r[1]))
    return ts, vs


def rc_fall(vdd, vol, tau, dt, n, t_fall):
    ts = [i * dt for i in range(n)]
    return ts, [vdd if t < t_fall else vol + (vdd - vol) * math.exp(-(t - t_fall) / tau) for t in ts]


# ---------------------------------------------------------------- 연습 1: fall time
def measure_fall_times(t, v, vdd, hi_frac=0.7, lo_frac=0.3, smooth=1):
    """하강 에지마다 (lo 하향 교차) − (그 직전 마지막 hi 하향 교차). hi 위로 올라가야 무장."""
    lo, hi = lo_frac * vdd, hi_frac * vdd
    vs, out, armed, t_hi = moving_average(v, smooth), [], False, None
    for i in range(1, len(vs)):
        a, b = vs[i - 1], vs[i]
        if b > hi:
            armed, t_hi = True, None
            continue
        if not armed:
            continue
        if a > hi >= b:
            t_hi = cross_time(t[i - 1], a, t[i], b, hi)
        if a > lo >= b and t_hi is not None:
            out.append(cross_time(t[i - 1], a, t[i], b, lo) - t_hi)
            armed, t_hi = False, None
    return out


def tf_model(vdd, vol, tau):
    """v → VOL 지수 하강의 0.7·VDD → 0.3·VDD 시간 = τ·ln((0.7VDD − VOL)/(0.3VDD − VOL))"""
    return tau * math.log((0.7 * vdd - vol) / (0.3 * vdd - vol))


# ---------------------------------------------------------------- 연습 2: 정전류 풀업
def cb_limit_current_src(mode, vdd, i_src=None):
    """정전류 I 로 충전하면 선형 램프: tr = Cb·(0.7−0.3)·VDD / I. sink 가 I 를 감당해야 하므로 I ≤ IOL."""
    vol, iol = vol_iol(mode, vdd)
    i = iol if i_src is None else min(i_src, iol)
    return SPEC[mode]["tr_max"] * i / (0.4 * vdd)


# ---------------------------------------------------------------- 연습 3: 버퍼로 버스 나누기
def split_plan(mode, vdd, segments):
    """세그먼트별 Cb 목록 → 세그먼트별 E24 풀업 목록. 하나라도 불가하면 None"""
    out = []
    for cb in segments:
        if cb > SPEC[mode]["cb_max"]:
            return None
        r = choose_e24(mode, vdd, cb)
        if r is None:
            return None
        out.append(r)
    return out


# ---------------------------------------------------------------- 연습 4: τ 직선 맞춤
def fit_tau(t, v, vdd_level, lo_frac=0.2, hi_frac=0.8):
    """첫 상승 에지에서 lo..hi 구간 샘플로 ln(Vhigh − v) = c − t/τ 최소제곱. τ 반환.
    Vhigh 는 측정한 high 레벨(=풀업 전원)."""
    lo, hi = lo_frac * vdd_level, hi_frac * vdd_level
    start = next(i for i, x in enumerate(v) if x < lo)            # low 상태 진입
    k0 = next(i for i in range(start, len(v)) if v[i] >= lo)       # 에지 시작
    k1 = next(i for i in range(k0, len(v)) if v[i] >= hi)          # 에지 80 %
    xs = [t[i] for i in range(k0, k1) if v[i] < vdd_level]
    ys = [math.log(vdd_level - v[i]) for i in range(k0, k1) if v[i] < vdd_level]
    mx, my = statistics.mean(xs), statistics.mean(ys)
    slope = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sum((x - mx) ** 2 for x in xs)
    return -1.0 / slope


def high_mode(v, vdd):
    """upper half 샘플의 히스토그램 최빈값 (8비트 스코프 값은 이미 양자화돼 있다) — IEEE 181 방식의 축약, HWLAB-B04"""
    counts = {}
    for x in v:
        if x >= 0.5 * vdd:
            k = round(x, 5)
            counts[k] = counts.get(k, 0) + 1
    return max(sorted(counts), key=lambda k: counts[k])


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


def near(x, ref, rel):
    return abs(x - ref) <= rel * abs(ref)


def run_tests():
    print("-- 연습 1: fall time --")
    t, v = rc_fall(3.3, 0.0, 10e-9, 0.1e-9, 2000, 20e-9)
    m = measure_fall_times(t, v, 3.3)
    check("VOL=0: tf = 0.8473*tau = %.3fns (expect 8.473)" % (m[0] * 1e9), lambda: near(m[0], K_30_70 * 10e-9, 2e-3))
    t, v = rc_fall(3.3, 0.6, 10e-9, 0.1e-9, 2000, 20e-9)
    m = measure_fall_times(t, v, 3.3)
    check("VOL=0.6V: tf = %.3fns = tau*ln((2.31-0.6)/(0.99-0.6)) = %.3fns, NOT 8.47"
          % (m[0] * 1e9, tf_model(3.3, 0.6, 10e-9) * 1e9), lambda: near(m[0], tf_model(3.3, 0.6, 10e-9), 2e-3))
    t, v = load_capture("I2C3_SDA")
    low, _ = levels(v, 3.3)
    tau_f = (110 * 500 / 610) * 92e-12          # 정답 모델 값 (gen_captures.py 의 숨은 값)
    m = measure_fall_times(t, v, 3.3, smooth=3)
    check("I2C3 capture tf = %.2fns, model %.2fns (VOL %.3fV), << tf_max 300ns"
          % (m[0] * 1e9, tf_model(3.3, low, tau_f) * 1e9, low),
          lambda: len(m) == 1 and near(m[0], tf_model(3.3, low, tau_f), 0.1) and m[0] < 300e-9)

    print("-- 연습 2: constant-current pull-up --")
    cr, cc = cb_close("Fm", 3.3), cb_limit_current_src("Fm", 3.3)
    check("Fm 3.3V: resistor closes at %.1fpF, 3mA current source reaches %.1fpF (x%.3f)"
          % (cr * 1e12, cc * 1e12, cc / cr),
          lambda: near(cr, 366.3e-12, 2e-3) and near(cc, 681.8e-12, 2e-3) and near(cc / cr, K_30_70 * 2.9 / 1.32, 1e-6))
    check("400pF Fm 3.3V: resistor impossible, current source OK",
          lambda: rp_min("Fm", 3.3) > rp_max("Fm", 400e-12) and cb_limit_current_src("Fm", 3.3) > 400e-12)

    print("-- 연습 3: split with a buffer --")
    check("520pF one segment -> None (also > Cb max 400pF)", lambda: split_plan("Fm", 3.3, [520e-12]) is None)
    p = split_plan("Fm", 3.3, [260e-12, 260e-12])
    check("260pF + 260pF -> %s" % p, lambda: p == [1100.0, 1100.0])
    p = split_plan("Fm", 3.3, [360e-12, 160e-12])
    check("360pF + 160pF -> %s (360pF side: margin Cb+20%% kills it)" % p, lambda: p is None)

    print("-- 연습 4: log-linear tau fit vs threshold method --")
    t, v = load_capture("I2C2_SCL")
    _, med = levels(v, 3.3)
    hm = high_mode(v, 3.3)
    bad = fit_tau(t, v, med)
    check("I2C2 slow edge: upper-half MEDIAN %.4fV is %.0fmV low (tail samples) -> fit tau %.0fns, 0.8473*tau %.0fns (biased)"
          % (med, (hm - med) * 1e3, bad * 1e9, K_30_70 * bad * 1e9), lambda: hm - med > 0.02)
    for name, vdd in (("I2C0_SCL", 3.3), ("I2C1_SCL", 1.8), ("I2C2_SCL", 3.3)):
        t, v = load_capture(name)
        high = high_mode(v, vdd)
        tau = fit_tau(t, v, high)
        tr = statistics.median(measure_rise_times(t, v, vdd, smooth=5))
        check("%s: Vhigh(mode)=%.4fV fit tau=%.1fns -> 0.8473*tau=%.1fns vs threshold tr=%.1fns"
              % (name, high, tau * 1e9, K_30_70 * tau * 1e9, tr * 1e9), lambda: near(K_30_70 * tau, tr, 0.02))

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


if __name__ == "__main__":
    sys.exit(0 if run_tests() else 1)
