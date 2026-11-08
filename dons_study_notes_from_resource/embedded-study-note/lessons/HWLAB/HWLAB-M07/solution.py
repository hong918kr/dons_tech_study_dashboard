#!/usr/bin/env python3
"""HWLAB-M07 클럭 지터 측정: Period·Cycle-to-Cycle·TIE

스코프의 지터 분석 소프트웨어(EZJIT, DPOJET 류)가 클럭 캡처에서 하는 일을 직접 구현한다.
  Part A  보간 임계 교차로 상승 에지 타임스탬프 → period / c2c / TIE(최소제곱 이상 클럭 대비)
          c2c = period 의 차분, period − T = TIE 의 차분 (항등식) / 백색 RJ 에서 √2·√6 배 관계
  Part B  측정이 거짓말하는 곳: 공칭 주파수 기준 TIE 의 ppm 경사, 전압 잡음 → 지터 바닥(floor),
          차분의 주파수 응답 2·sin(πν), 4·sin²(πν) (PJ 주입), SSC 가 TIE 를 지배하는 현상
  Part C  NETBRD-C 두 보드의 CLK25_PHY 캡처(.bin) → SSC 검출 + 스펙 판정 리포트

  python3 -I solution.py                       # "N/M passed", 실패 있으면 exit 1
  python3 -I solution.py --report SN0108_tp44_clk25.bin   # 캡처 하나에 대한 리포트만

Python 3.9+, 표준 라이브러리만 (math, random, json, os, sys).
"""
import json
import math
import os
import random
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))


# ---------------------------------------------------------------- 합성 클럭 (주어짐)
def ssc_dev(t, spread, fm, u0=0.0):
    """down-spread 삼각파 SSC 의 분수 주파수 편차 y(t) ∈ [−spread, 0]. 위상 u = t·fm + u0 (u=0 에서 최고 주파수)."""
    u = (t * fm + u0) % 1.0
    return -spread * (2.0 * u if u < 0.5 else 2.0 - 2.0 * u)


def clock_edges(n, f0, ppm=0.0, rj=0.0, pj=(), ssc=None, t_first=3.1e-9, seed=1):
    """참 상승 에지 n 개 (초). 반송파 τ_k: 주기 T/(1+y(τ)) 로 누적(SSC). 에지 = τ_k + Σ PJ + RJ.
    pj = [(amp_s, freq_hz), ...] 사인 PJ, rj = 백색 가우스 TIE σ, ssc = (spread, fm, u0) 또는 None."""
    rng = random.Random(seed)
    T = 1.0 / (f0 * (1.0 + ppm * 1e-6))
    out = []
    tau = t_first
    for _ in range(n):
        j = sum(a * math.sin(2 * math.pi * f * tau) for a, f in pj)
        if rj:
            j += rng.gauss(0.0, rj)
        out.append(tau + j)
        y = ssc_dev(tau, *ssc) if ssc else 0.0
        tau += T / (1.0 + y)
    return out


def render(rises, fs, t10_90, vlo, vhi, noise=0.0, seed=2):
    """상승 에지 → 균일 샘플 파형. 전이 = tanh (10–90 % = tc·ln 9), 하강 에지 = 이웃 상승 에지 가운데.
    샘플 i 의 시각 = i/fs, 마지막 에지 뒤 반 주기까지."""
    rng = random.Random(seed)
    tc = t10_90 / math.log(9.0)
    edges = []
    for k, r in enumerate(rises):
        nxt = rises[k + 1] if k + 1 < len(rises) else 2 * r - rises[k - 1]
        edges += [(r, +1), (0.5 * (r + nxt), -1)]
    n = int((edges[-1][0] + 0.25 * (rises[-1] - rises[-2])) * fs)
    mid, half = 0.5 * (vlo + vhi), 0.5 * (vhi - vlo)
    v = []
    j = 0
    for i in range(n):
        t = i / fs
        while j + 1 < len(edges) and t > 0.5 * (edges[j][0] + edges[j + 1][0]):
            j += 1
        te, s = edges[j]
        x = mid + s * half * math.tanh((t - te) / tc)
        v.append(x + rng.gauss(0.0, noise) if noise else x)
    return v


def load_capture(path):
    """스코프 BYTE 포맷 흉내: 첫 줄 JSON 프리앰블, 그 뒤 (zlib 압축된) int8 코드. → (프리앰블 dict, 볼트 리스트)."""
    with open(path, "rb") as f:
        pre = json.loads(f.readline().decode("ascii"))
        raw = f.read()
    if pre.get("encoding") == "zlib":
        raw = zlib.decompress(raw)
    yinc, yorg = pre["yinc"], pre["yorg"]
    v = [yorg + (b - 256 if b > 127 else b) * yinc for b in raw]
    return pre, v


# ---------------------------------------------------------------- Part A: 에지 → period / c2c / TIE
def edge_times(v, dt, thr, hyst, t0=0.0):
    """상승 에지의 thr 교차 시각 (선형 보간). 히스테리시스: thr−hyst 아래로 내려가야 무장(arm),
    thr+hyst 이상이 되면 에지 확정 → 그 사이 thr 를 '마지막으로' 넘은 샘플 쌍 (j, j+1) 에서 보간.
    샘플 i 의 시각 = t0 + i·dt."""
    lo, hi = thr - hyst, thr + hyst
    out = []
    armed = False
    last_below = None
    for i, x in enumerate(v):
        if x < thr:
            last_below = i
        if x <= lo:
            armed = True
        elif armed and x >= hi:
            j = last_below
            v0, v1 = v[j], v[j + 1]
            out.append(t0 + (j + (thr - v0) / (v1 - v0)) * dt)
            armed = False
    return out


def periods(edges):
    """P[n] = e[n+1] − e[n]  (N 에지 → N−1 주기)"""
    return [b - a for a, b in zip(edges, edges[1:])]


def c2c_direct(edges):
    """c2c[n] = P[n+1] − P[n] 를 에지에서 직접: e[n+2] − 2·e[n+1] + e[n]  (N 에지 → N−2 개)"""
    return [edges[k + 2] - 2.0 * edges[k + 1] + edges[k] for k in range(len(edges) - 2)]


def tie_ls(edges):
    """이상 클럭 t̂_n = a + b·n 을 최소제곱으로 맞추고 TIE[n] = e[n] − t̂_n.  반환 (tie, T_fit=b, a).
    수치 안정: e[0] 을 빼고 n 도 평균을 빼서 푼다 (정규방정식이 대각이 된다)."""
    N = len(edges)
    e0 = edges[0]
    nm = (N - 1) / 2.0
    ym = sum(e - e0 for e in edges) / N
    sxy = sum((k - nm) * (e - e0 - ym) for k, e in enumerate(edges))
    sxx = N * (N * N - 1) / 12.0                     # Σ (k − nm)²
    b = sxy / sxx
    a = e0 + ym - b * nm
    return [e - (a + b * k) for k, e in enumerate(edges)], b, a


def tie_nominal(edges, f_nom):
    """'순진한' TIE: 첫 에지에서 시작하는 공칭 주파수 이상 클럭 기준. ppm 오차가 경사로 남는다."""
    T = 1.0 / f_nom
    return [e - (edges[0] + k * T) for k, e in enumerate(edges)]


def stats(x):
    """(mean, rms(평균 제거 = 표준편차, 모집단), pk-pk, max|x−mean|)"""
    n = len(x)
    m = sum(x) / n
    rms = math.sqrt(sum((a - m) ** 2 for a in x) / n)
    return m, rms, max(x) - min(x), max(abs(a - m) for a in x)


def jitter_metrics(edges):
    """에지 타임스탬프 → 세 지표 한 벌 (초)."""
    P = periods(edges)
    C = c2c_direct(edges)
    tie, T, _ = tie_ls(edges)
    pm, prms, ppk, _ = stats(P)
    _, crms, cpk, cabs = stats(C)
    _, trms, tpk, _ = stats(tie)
    return dict(n_edges=len(edges), T_fit=T, f_fit=1.0 / T, period_mean=pm, period_rms=prms, period_pkpk=ppk,
                c2c_rms=crms, c2c_pkpk=cpk, c2c_absmax=max(abs(c) for c in C), tie_rms=trms, tie_pkpk=tpk)


# ---------------------------------------------------------------- Part B: 성분의 주파수 응답
def sine_amp(x, nu):
    """x[n] ≈ c + p·cos(2πνn) + q·sin(2πνn) 를 최소제곱으로 맞춰 진폭 √(p²+q²) 를 돌려준다 (ν = cycles/샘플).
    평균을 뺀 뒤 2×2 정규방정식 [[Scc, Scs], [Scs, Sss]]·[p, q] = [Sxc, Sxs] 를 푼다 (록인 검출과 같은 일)."""
    n = len(x)
    m = sum(x) / n
    scc = scs = sss = sxc = sxs = 0.0
    for k, a in enumerate(x):
        c, s = math.cos(2 * math.pi * nu * k), math.sin(2 * math.pi * nu * k)
        scc += c * c
        scs += c * s
        sss += s * s
        sxc += (a - m) * c
        sxs += (a - m) * s
    det = scc * sss - scs * scs
    p = (sxc * sss - sxs * scs) / det
    q = (sxs * scc - sxc * scs) / det
    return math.hypot(p, q)


# ---------------------------------------------------------------- Part C: SSC 검출 + 판정
def ssc_profile(edges, W=32):
    """순간 주파수 프로파일. y_n = T_fit / P[n] − 1 (평균 주파수 대비 분수 편차)을 W 주기 이동평균한 뒤
    spread = max − min, 변조 주파수 fm = 평균선 교차(히스테리시스 ±spread/4) 간격에서.
    반환 dict(spread=분수, fm_hz 또는 None, ymin, ymax).
    W 주기 이동평균 = (e[n+W] − e[n]) / W  — 주기 W 개의 합은 에지 두 개의 차이라서 O(N) 이다."""
    _, T, _ = tie_ls(edges)
    sm = [T * W / (edges[k + W] - edges[k]) - 1.0 for k in range(len(edges) - W)]
    ymin, ymax = min(sm), max(sm)
    spread = ymax - ymin
    mid = 0.5 * (ymin + ymax)
    h = spread / 4.0
    crossings = []
    state = None
    for k, y in enumerate(sm):
        s = 1 if y > mid + h else (-1 if y < mid - h else None)
        if s is not None and s != state:
            if state is not None:
                crossings.append(k)
            state = s
    fm = None
    if len(crossings) >= 2:
        span = (crossings[-1] - crossings[0]) * T
        fm = (len(crossings) - 1) / (2.0 * span)
    return dict(spread=spread, fm_hz=fm, ymin=ymin, ymax=ymax, n_cross=len(crossings))


def judge(m, ssc, spec):
    """측정(jitter_metrics) + ssc_profile + 스펙 JSON → 항목 목록 [(이름, 측정 문자열, 한계 문자열, ok)], verdict.
    항목 순서: freq_ppm, inst_ppm, ssc, period_rms, c2c_absmax, tie_pkpk."""
    f_nom = spec["f_nom_hz"]
    ppm = (m["f_fit"] / f_nom - 1.0) * 1e6
    lo_ppm = ((1.0 + ppm * 1e-6) * (1.0 + ssc["ymin"]) - 1.0) * 1e6       # 순간 주파수의 공칭 대비 범위
    hi_ppm = ((1.0 + ppm * 1e-6) * (1.0 + ssc["ymax"]) - 1.0) * 1e6
    worst = max(abs(lo_ppm), abs(hi_ppm))
    detected = ssc["spread"] > spec["ssc_detect_spread"]
    items = [
        ("freq_ppm", "%+.1fppm" % ppm, "|.| <= %gppm" % spec["ppm_max"], abs(ppm) <= spec["ppm_max"]),
        ("inst_ppm", "%+.0f..%+.0fppm" % (lo_ppm, hi_ppm), "|.| <= %gppm" % spec["inst_ppm_max"],
         worst <= spec["inst_ppm_max"]),
        ("ssc", ("spread %.2f%% @ %.1fkHz" % (ssc["spread"] * 100, (ssc["fm_hz"] or 0) / 1e3)) if detected else "none",
         "allowed" if spec["ssc_allowed"] else "not allowed", spec["ssc_allowed"] or not detected),
        ("period_rms", "%.1fps" % (m["period_rms"] * 1e12), "<= %gps" % spec["period_rms_max_ps"],
         m["period_rms"] * 1e12 <= spec["period_rms_max_ps"]),
        ("c2c_absmax", "%.1fps" % (m["c2c_absmax"] * 1e12), "<= %gps" % spec["c2c_absmax_ps"],
         m["c2c_absmax"] * 1e12 <= spec["c2c_absmax_ps"]),
        ("tie_pkpk", "%.1fps" % (m["tie_pkpk"] * 1e12), "<= %gps" % spec["tie_pkpk_max_ps"],
         m["tie_pkpk"] * 1e12 <= spec["tie_pkpk_max_ps"]),
    ]
    return items, ("PASS" if all(ok for *_, ok in items) else "FAIL")


def analyze_capture(path, spec):
    pre, v = load_capture(path)
    mid = 0.5 * (spec["vlo"] + spec["vhi"])
    e = edge_times(v, pre["xinc"], mid, spec["hyst_v"], pre["xorg"])
    m = jitter_metrics(e)
    sp = ssc_profile(e)
    items, verdict = judge(m, sp, spec)
    return pre, e, m, sp, items, verdict


def report(path, spec):
    pre, e, m, sp, items, verdict = analyze_capture(path, spec)
    lines = ["%s %s %s@%s -> %s  edges=%d f=%.6fMHz" % (pre["board"], pre["serial"], pre["net"], pre["probe"],
                                                       verdict, m["n_edges"], m["f_fit"] / 1e6)]
    for name, val, lim, ok in items:
        lines.append("  %-4s %-10s = %-26s %s" % ("ok" if ok else "BAD", name, val, lim))
    lines.append("  info: period_pkpk=%.1fps c2c_rms=%.2fps tie_rms=%.1fps" %
                 (m["period_pkpk"] * 1e12, m["c2c_rms"] * 1e12, m["tie_rms"] * 1e12))
    return "\n".join(lines), verdict, m, sp, items


# ---------------------------------------------------------------- 테스트 하네스
g_pass = 0
g_total = 0


def check(name, fn):
    """fn() → bool 또는 (bool, 설명). 예외도 FAIL (starter 의 미구현 함수가 하네스를 죽이지 않게)."""
    global g_pass, g_total
    g_total += 1
    detail = ""
    try:
        r = fn()
        ok, detail = (r if isinstance(r, tuple) else (r, ""))
        ok = bool(ok)
    except Exception as ex:  # noqa: BLE001
        ok, detail = False, "%s: %s" % (type(ex).__name__, ex)
    if ok:
        g_pass += 1
        print("PASS  %s%s" % (name, ("   [" + detail + "]") if detail else ""))
    else:
        print("FAIL  %s: %s" % (name, detail or "returned False"))


def safe(fn, default=None):
    """테스트 준비 계산을 감싼다: starter 의 미구현 함수가 예외를 내도 하네스는 계속 돈다."""
    try:
        return fn()
    except Exception as ex:  # noqa: BLE001
        print("      (setup failed: %s: %s)" % (type(ex).__name__, ex))
        return default


def rel(x, ref, tol):
    return x is not None and abs(x / ref - 1.0) <= tol


def ps(x):
    return "None" if x is None else "%.3fps" % (x * 1e12)


# 공통 캡처 조건 (Part A/B): 100 MHz HCSL 레퍼런스 클럭을 차동 프로브로, 20 GS/s, 10–90 % 0.8 ns, 임계 0 V
F0, FS, TR, VLO, VHI, THR, HYST = 100e6, 20e9, 0.8e-9, -0.75, 0.75, 0.0, 0.1


def capture(rises, noise=0.0, seed=2, fs=FS):
    """참 에지 → 파형 → 보간 교차로 다시 뽑은 에지 (측정 체인 전체)."""
    return edge_times(render(rises, fs, TR, VLO, VHI, noise, seed), 1.0 / fs, THR, HYST)


def run_tests():
    # ---- Part A: 100 MHz +25 ppm, 2048 에지, 백색 RJ 2 ps (TIE σ)
    print("-- Part A: edges -> period / c2c / TIE --")
    N = 2048
    truth0 = clock_edges(N, F0, ppm=25.0)
    e0 = safe(lambda: capture(truth0), [])

    def ext():
        d = [a - b for a, b in zip(e0, truth0)]
        _, r, _, _ = stats(d)
        return len(e0) == N and abs(sum(d) / N) < 1e-12 and r < 0.05e-12, \
            "n=%d mean(e-truth)=%s rms=%s" % (len(e0), ps(sum(d) / N if d else None), ps(r if d else None))
    check("edge_times: 2048 edges, interpolated == injected truth (clean)", ext)

    def lsfit():
        _, T, _ = tie_ls(e0)
        ppm = (1.0 / T / F0 - 1) * 1e6
        return abs(ppm - 25.0) < 0.01, "f_fit=%.6fMHz -> %+.4fppm (truth +25)" % (1 / T / 1e6, ppm)
    check("tie_ls: least-squares frequency == 100MHz +25ppm (+-0.01ppm)", lsfit)

    truth = clock_edges(N, F0, ppm=25.0, rj=2e-12, seed=11)
    eA = safe(lambda: capture(truth), [])

    def ident():
        P, C = periods(eA), c2c_direct(eA)
        tie, T, _ = tie_ls(eA)
        e1 = max(abs(C[k] - (P[k + 1] - P[k])) for k in range(len(C)))
        e2 = max(abs((P[k] - T) - (tie[k + 1] - tie[k])) for k in range(len(P)))
        return len(P) == N - 1 and len(C) == N - 2 and e1 < 1e-18 and e2 < 1e-18, \
            "max|c2c-dP|=%.1es max|(P-T)-dTIE|=%.1es" % (e1, e2)
    check("identities: c2c == diff(period), period - T_fit == diff(TIE)", ident)

    mA = safe(lambda: jitter_metrics(eA), {})
    safe(lambda: print("      RJ 2ps: tie_rms=%s period_rms=%s c2c_rms=%s  (ratios %.3f, %.3f)" % (
        ps(mA["tie_rms"]), ps(mA["period_rms"]), ps(mA["c2c_rms"]),
        mA["period_rms"] / mA["tie_rms"], mA["c2c_rms"] / mA["tie_rms"])))
    check("white RJ: tie_rms == 2ps (+-5%)", lambda: (rel(mA["tie_rms"], 2e-12, 0.05), ps(mA["tie_rms"])))
    check("white RJ: period_rms == sqrt2 * 2ps = 2.828ps (+-5%)",
          lambda: (rel(mA["period_rms"], math.sqrt(2) * 2e-12, 0.05), ps(mA["period_rms"])))
    check("white RJ: c2c_rms == sqrt6 * 2ps = 4.899ps (+-5%)",
          lambda: (rel(mA["c2c_rms"], math.sqrt(6) * 2e-12, 0.05), ps(mA["c2c_rms"])))

    # ---- Part B
    print("-- Part B: where jitter numbers lie --")
    tn = safe(lambda: tie_nominal(eA, F0), [])
    tl = safe(lambda: tie_ls(eA)[0], [])

    def nominal_ramp():
        ramp = 25e-6 / F0 * (N - 1)                   # ppm·T·(N−1) 만큼 끝에서 벌어진다
        pk = max(tn) - min(tn)
        return rel(pk, ramp, 0.02) and stats(tl)[2] < 0.05 * pk, \
            "nominal-ref TIE pkpk=%s (ppm ramp %s) vs LS TIE pkpk=%s" % (ps(pk), ps(ramp), ps(stats(tl)[2]))
    check("TIE vs nominal 100MHz: +25ppm leaves a 512ps ramp; LS reference removes it", nominal_ramp)

    # 샘플이 에지 위에 너무 적으면: 5 GS/s(10–90 % 위에 4 샘플)에서 깨끗한 클럭의 TIE 에 보간 오차 패턴이 생긴다
    def interp_floor():
        lo = jitter_metrics(capture(truth0, fs=5e9))["tie_rms"]
        hi = jitter_metrics(e0)["tie_rms"]
        return lo > 1.0e-12 and hi < 0.05e-12, "clean +25ppm: tie_rms @5GS/s=%s  @20GS/s=%s" % (ps(lo), ps(hi))
    check("too few samples per edge: 5GS/s interpolation error shows up as >1ps 'jitter'", interp_floor)

    # 전압 잡음 → 지터 바닥: 깨끗한 클럭 + 3 mV 잡음으로 바닥을 재고, RJ 1.5 ps 캡처에서 제곱합으로 뺀다
    tb = clock_edges(N, F0, ppm=25.0, rj=1.5e-12, seed=21)
    floor = safe(lambda: jitter_metrics(capture(truth0, noise=3e-3, seed=22))["tie_rms"])
    meas = safe(lambda: jitter_metrics(capture(tb, noise=3e-3, seed=23))["tie_rms"])
    slew = 0.5 * (VHI - VLO) / (TR / math.log(9.0))     # tanh 중앙 기울기 = (A/2)/tc [V/s]

    def floor_sub():
        pred = 3e-3 / slew
        rj = math.sqrt(max(meas ** 2 - floor ** 2, 0.0))
        return (0.6 * pred <= floor <= 1.05 * pred) and meas > 1.15 * 1.5e-12 and rel(rj, 1.5e-12, 0.10), \
            "floor=%s (sigma_v/slew=%s) measured=%s -> sqrt(meas^2-floor^2)=%s vs injected 1.5ps" % (
                ps(floor), ps(pred), ps(meas), ps(rj))
    check("noise floor: 3mV/slew adds jitter; subtract in quadrature -> RJ 1.5ps (+-10%)", floor_sub)

    # PJ 두 톤: 2 MHz (ν=0.02) 와 25 MHz (ν=0.25), 각 10 ps, RJ 0.3 ps
    tp = clock_edges(N, F0, rj=0.3e-12, pj=[(10e-12, 2e6), (10e-12, 25e6)], seed=31)
    eP = safe(lambda: capture(tp), [])

    def gains(nu):
        tie, T, _ = tie_ls(eP)
        a_t, a_p, a_c = sine_amp(tie, nu), sine_amp(periods(eP), nu), sine_amp(c2c_direct(eP), nu)
        return a_t, a_p, a_c

    for f in (2e6, 25e6):
        nu = f / F0

        def pj_case(nu=nu, f=f):
            a_t, a_p, a_c = gains(nu)
            gp, gc = 2 * math.sin(math.pi * nu), 4 * math.sin(math.pi * nu) ** 2
            ok = rel(a_t, 10e-12, 0.03) and rel(a_p, gp * 10e-12, 0.03) and rel(a_c, gc * 10e-12, 0.05)
            return ok, "TIE=%s period=%s (2sin=%.4f->%s) c2c=%s (4sin^2=%.4f->%s)" % (
                ps(a_t), ps(a_p), gp, ps(gp * 10e-12), ps(a_c), gc, ps(gc * 10e-12))
        check("PJ %gMHz (nu=%.2f): period = 2sin(pi nu)*TIE, c2c = 4sin^2(pi nu)*TIE" % (f / 1e6, nu), pj_case)

    # SSC: −0.5 % down-spread, 31.5 kHz 삼각파, RJ 2 ps. 평균 주파수 f0(1−δ/2) 에서 변조 2 주기 = 6334 에지.
    # 위상 u0=0.25 (주파수가 평균을 지나 내려가는 순간에서 시작) → TIE 가 창 가운데에 대해 대칭 → LS 경사 보정 0
    delta, fm = 0.005, 31.5e3
    n_ssc = int(round(2 * F0 * (1 - delta / 2) / fm)) + 1
    ts = clock_edges(n_ssc, F0, rj=2e-12, ssc=(delta, fm, 0.25), seed=41)
    mS = safe(lambda: jitter_metrics(capture(ts)), {})
    safe(lambda: print("      SSC: tie_pkpk=%.3fns tie_rms=%.3fns period_rms=%s c2c_rms=%s" % (
        mS["tie_pkpk"] * 1e9, mS["tie_rms"] * 1e9, ps(mS["period_rms"]), ps(mS["c2c_rms"]))))
    check("SSC dominates TIE: pkpk ~= delta*Tm/8 = 19.84ns (+-3%), > 1000x RJ",
          lambda: (rel(mS["tie_pkpk"], delta / fm / 8, 0.03) and mS["tie_pkpk"] > 1000 * 2e-12,
                   "%.3fns vs %.3fns" % (mS["tie_pkpk"] * 1e9, delta / fm / 8 * 1e9)))
    pred_p = math.sqrt(2 * (2e-12) ** 2 + (delta / F0) ** 2 / 12)
    check("SSC in period jitter: rms ~= sqrt(2 RJ^2 + (delta T0)^2/12) = 14.71ps (+-5%)",
          lambda: (rel(mS["period_rms"], pred_p, 0.05), "%s vs %s" % (ps(mS["period_rms"]), ps(pred_p))))
    check("SSC invisible in c2c: rms ~= sqrt6 * 2ps = 4.899ps (+-5%)",
          lambda: (rel(mS["c2c_rms"], math.sqrt(6) * 2e-12, 0.05), ps(mS["c2c_rms"])))

    # ---- Part C: NETBRD-C rev A, CLK25_PHY @ TP44
    print("-- Part C: NETBRD-C CLK25_PHY @ TP44 (ethernet PHY XI, SSC not allowed) --")
    with open(os.path.join(HERE, "spec_clk25_phy.json"), encoding="utf-8") as f:
        spec = json.load(f)
    res = {}
    for sn in ("SN0107", "SN0108"):
        res[sn] = safe(lambda: report(os.path.join(HERE, "%s_tp44_clk25.bin" % sn), spec))
        if res[sn]:
            print(res[sn][0])
    check("SN0107 (golden) -> PASS", lambda: res["SN0107"][1] == "PASS")
    check("SN0108 (SS_EN strap R231 populated) -> FAIL", lambda: res["SN0108"][1] == "FAIL")

    def ssc_found():
        sp = res["SN0108"][3]
        return rel(sp["spread"], 0.005, 0.10) and rel(sp["fm_hz"], 31.5e3, 0.05), \
            "spread=%.3f%% fm=%.2fkHz (injected 0.50%%, 31.5kHz)" % (sp["spread"] * 100, sp["fm_hz"] / 1e3)
    check("SN0108 SSC detected: spread 0.5% (+-10%), fm 31.5kHz (+-5%)", ssc_found)

    def which_items():
        bad = [n for n, _, _, ok in res["SN0108"][4] if not ok]
        return bad == ["freq_ppm", "inst_ppm", "ssc", "period_rms", "tie_pkpk"], "failed=%s" % bad
    check("SN0108 fails ppm/inst/ssc/period/TIE but c2c PASSES (c2c alone would miss SSC)", which_items)
    check("SN0107 freq +12ppm (+-2ppm), no SSC (spread < 0.02%)",
          lambda: (abs((res["SN0107"][2]["f_fit"] / 25e6 - 1) * 1e6 - 12) < 2 and res["SN0107"][3]["spread"] < 2e-4,
                   "ppm=%+.2f spread=%.4f%%" % ((res["SN0107"][2]["f_fit"] / 25e6 - 1) * 1e6,
                                                res["SN0107"][3]["spread"] * 100)))

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


def main(argv):
    if len(argv) == 3 and argv[1] == "--report":
        with open(os.path.join(HERE, "spec_clk25_phy.json"), encoding="utf-8") as f:
            spec = json.load(f)
        print(report(argv[2], spec)[0])
        return 0
    return 0 if run_tests() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
