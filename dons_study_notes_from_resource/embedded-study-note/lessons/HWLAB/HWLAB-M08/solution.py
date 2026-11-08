#!/usr/bin/env python3
"""HWLAB-M08 위상 잡음 → RMS 지터 적분과 레퍼런스 클럭 판정

오실레이터 데이터시트의 위상 잡음 표 L(f) [dBc/Hz] 를 시간 영역 RMS 지터로 바꾸고 스펙을 판정한다.
  Part A  로그-로그 보간(구간마다 멱법칙) + 구간별 닫힌식 적분 → σ_φ = √(2∫10^(L/10)df), σ_t = σ_φ/(2πf0)
          (평탄 / -20 dB/dec / -10 dB/dec 표처럼 정답을 손으로 계산할 수 있는 표로 확인)
  Part B  순진한 적분(선형 사다리꼴·dB 를 선형 f 로 보간·직사각형 합)의 오차, 스퍼(dBc) 처리,
          가중 적분(로그 축 Simpson), PCIe common-clock 형 필터 |H(f)|² (PLL 2개 차 · 지연 · CDR HPF)
  Part C  SSDB-4 의 PCIE_REFCLK 후보 Y2 / Y3 — 12k–20M 헤드라인 숫자 vs 필터 적용 숫자, 판정 리포트

  python3 -I solution.py            # "N/M passed", 실패 있으면 exit 1
  python3 -I solution.py --report   # Part C 리포트만 출력

Python 3.9+, 표준 라이브러리만 (math, cmath, json, os, sys).
"""
import cmath
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TWO_PI = 2.0 * math.pi


# ---------------------------------------------------------------- 주어진 도구
def load_json(name):
    with open(os.path.join(HERE, name), encoding="utf-8") as f:
        return json.load(f)


def lin(db):
    """dB → 선형 전력비."""
    return 10.0 ** (db / 10.0)


def breakpoints(table, f1, f2):
    """[f1, (f1..f2 사이 표의 오프셋들), f2] — 적분은 이 점들 사이 구간(segment)마다 한다."""
    return [f1] + [float(f) for f, _ in table if f1 < f < f2] + [f2]


def pll_peaking_db(zeta):
    """2차 type-2 PLL 폐루프 H(s) = (2ζωn·s + ωn²)/(s² + 2ζωn·s + ωn²) 의 최대 이득 [dB].
    x = (ω/ωn)², a = 4ζ² 로 두면 |H|² = (1 + a·x)/((1 − x)² + a·x), 최대는 x* = (√(1+2a) − 1)/a."""
    a = 4.0 * zeta * zeta
    x = (math.sqrt(1.0 + 2.0 * a) - 1.0) / a
    return 10.0 * math.log10((1.0 + a * x) / ((1.0 - x) ** 2 + a * x))


def zeta_from_peaking(pk_db):
    """peaking [dB] → ζ (peaking 은 ζ 에 대해 단조 감소 → 로그 이분법)."""
    lo, hi = 0.05, 500.0
    for _ in range(200):
        mid = math.sqrt(lo * hi)
        if pll_peaking_db(mid) > pk_db:
            lo = mid
        else:
            hi = mid
    return math.sqrt(lo * hi)


def wn_from_bw(bw_hz, zeta):
    """−3 dB 대역폭 → ωn.  |H|² = 1/2 를 풀면 (ω3dB/ωn)² = (1+2ζ²) + √((1+2ζ²)² + 1)."""
    x = (1.0 + 2.0 * zeta * zeta) + math.sqrt((1.0 + 2.0 * zeta * zeta) ** 2 + 1.0)
    return TWO_PI * bw_hz / math.sqrt(x)


def pll_H(s, wn, zeta):
    """2차 type-2 PLL 폐루프 전달함수 (지터 저역통과)."""
    return (2.0 * zeta * wn * s + wn * wn) / (s * s + 2.0 * zeta * wn * s + wn * wn)


def pcie_combos(flt):
    """스펙의 (BW, peaking) 모서리 조합 2×2×2×2 = 16 개. 각 조합 dict 에 ζ, ωn 미리 계산."""
    out = []
    for b1 in flt["pll1_bw_hz"]:
        for p1 in flt["pll1_peak_db"]:
            for b2 in flt["pll2_bw_hz"]:
                for p2 in flt["pll2_peak_db"]:
                    z1, z2 = zeta_from_peaking(p1), zeta_from_peaking(p2)
                    out.append(dict(bw1=b1, pk1=p1, bw2=b2, pk2=p2, z1=z1, z2=z2,
                                    wn1=wn_from_bw(b1, z1), wn2=wn_from_bw(b2, z2),
                                    T=flt["delay_s"], fcdr=flt["cdr_hpf_hz"]))
    return out


def integrate_naive(table, f1, f2, method):
    """비교용 '순진한' 적분 3종 (Part B). 구간 끝점의 L 은 interp_L 로 구한다.
      'trapz'   : 선형 전력 S=10^(L/10) 를 선형 f 축에서 사다리꼴 (점 사이를 직선으로)
      'db_linf' : dB 값을 선형 f 에 대해 직선 보간 (= 지수함수) 후 그 구간을 정확히 적분
      'rect'    : 왼쪽 점의 S 에 구간 폭을 곱한 직사각형 합 ("L × 대역폭")
    """
    pts = breakpoints(table, f1, f2)
    Ls = [interp_L(table, f) for f in pts]
    total = 0.0
    for (fa, fb), (La, Lb) in zip(zip(pts, pts[1:]), zip(Ls, Ls[1:])):
        Sa, Sb = lin(La), lin(Lb)
        if method == "trapz":
            total += 0.5 * (Sa + Sb) * (fb - fa)
        elif method == "db_linf":
            k = (Lb - La) / (fb - fa) * math.log(10.0) / 10.0     # S(f) = Sa·e^{k(f−fa)}
            total += Sa * (fb - fa) if k == 0.0 else (Sb - Sa) / k
        elif method == "rect":
            total += Sa * (fb - fa)
        else:
            raise ValueError(method)
    return total


# ---------------------------------------------------------------- Part A: 보간·닫힌식 적분·RMS 지터
def interp_L(table, f):
    """표 [(f, L)] (f 오름차순) 에서 오프셋 f 의 L [dBc/Hz]. L 은 두 점 사이에서 log10(f) 에 대해 직선.
    f 가 표 범위 밖이면 ValueError (외삽 금지 — 데이터시트가 말하지 않은 구간을 지어내지 않는다)."""
    f_lo, f_hi = table[0][0], table[-1][0]
    if f < f_lo * (1 - 1e-12) or f > f_hi * (1 + 1e-12):
        raise ValueError("offset %.6g Hz outside table [%.6g, %.6g]" % (f, f_lo, f_hi))
    for (fa, La), (fb, Lb) in zip(table, table[1:]):
        if f <= fb * (1 + 1e-12):
            return La + (Lb - La) * math.log10(f / fa) / math.log10(fb / fa)
    return table[-1][1]


def band_integral(table, f1, f2):
    """∫_{f1}^{f2} 10^(L(f)/10) df  — 단측파대(SSB) 잡음 전력 / 반송파, 단위 없음 (rad² 의 절반).

    구간 [fa, fb] 에서 S(f) = Sa·(f/fa)^b,  b = log(Sb/Sa)/log(fb/fa)  (= 기울기[dB/dec]/10)
      b ≠ −1 : ∫ = Sa·fa/(b+1) · ((fb/fa)^(b+1) − 1)
      b = −1 : ∫ = Sa·fa · ln(fb/fa)
    """
    pts = breakpoints(table, f1, f2)
    total = 0.0
    for fa, fb in zip(pts, pts[1:]):
        Sa, Sb = lin(interp_L(table, fa)), lin(interp_L(table, fb))
        b = math.log(Sb / Sa) / math.log(fb / fa)
        if abs(b + 1.0) < 1e-9:
            total += Sa * fa * math.log(fb / fa)
        else:
            total += Sa * fa / (b + 1.0) * ((fb / fa) ** (b + 1.0) - 1.0)
    return total


def rms_jitter(table, f0, f1, f2):
    """(σ_φ [rad], σ_t [s]):  σ_φ = √(2·∫L df)  (×2: L 은 한쪽 측파대, 위상 PSD S_φ = 2·L),
    σ_t = σ_φ / (2π·f0)."""
    phi = math.sqrt(2.0 * band_integral(table, f1, f2))
    return phi, phi / (TWO_PI * f0)


# ---------------------------------------------------------------- Part B: 스퍼, 가중 적분, PCIe 형 필터
def spur_phase_var(spurs, f1, f2, w=None):
    """스퍼 [(f, dBc)] 중 f1 <= f <= f2 인 것들의 위상 분산 [rad²] 합.
    단측 스퍼 S dBc 하나 = 정현 위상 변조의 한쪽 측파대 → 위상 분산 2·10^(S/10) (L 적분의 ×2 와 같은 규약).
    w(f) 가 있으면 그 스퍼의 분산에 |H(f)|² = w(f) 를 곱한다."""
    total = 0.0
    for f, dbc in spurs:
        if f1 <= f <= f2:
            total += 2.0 * lin(dbc) * (w(f) if w else 1.0)
    return total


def integrate_weighted(table, f1, f2, w, ppd=100):
    """∫_{f1}^{f2} 10^(L(f)/10) · w(f) df  — 수치 적분.
    u = ln f 로 바꾸면 df = f·du → ∫ S(f)·w(f)·f du. 표의 꺾인 점마다 구간을 나누고(꺾임이 노드에 오게),
    각 구간을 decade 당 ppd 칸(짝수)으로 나눠 Simpson 1/3 규칙."""
    pts = breakpoints(table, f1, f2)
    total = 0.0
    for fa, fb in zip(pts, pts[1:]):
        n = max(2, int(math.ceil(math.log10(fb / fa) * ppd)))
        n += n % 2
        du = math.log(fb / fa) / n
        acc = 0.0
        for i in range(n + 1):
            f = fa * math.exp(i * du)
            y = lin(interp_L(table, f)) * w(f) * f
            acc += y * (1.0 if i in (0, n) else (4.0 if i % 2 else 2.0))
        total += acc * du / 3.0
    return total


def pcie_mag2(f, c):
    """PCIe common-clock 형 지터 전달함수의 |H(f)|².
      H(s) = [H1(s)·e^{−sT} − H2(s)] · H3(s),   H3(s) = s / (s + 2π·fcdr)   (CDR 1차 고역통과)
    H1, H2 = pll_H(s, wn, ζ) (조합 c 의 wn1/z1, wn2/z2), T = c['T'], fcdr = c['fcdr'], s = j2πf."""
    s = complex(0.0, TWO_PI * f)
    h1 = pll_H(s, c["wn1"], c["z1"])
    h2 = pll_H(s, c["wn2"], c["z2"])
    h3 = s / (s + TWO_PI * c["fcdr"])
    return abs((h1 * cmath.exp(-s * c["T"]) - h2) * h3) ** 2


def filtered_jitter(osc, spec, ppd=100):
    """필터 적용 RMS 지터 — 16 조합 각각 σ_t 를 구하고 최악값.
    σ_φ² = 2·∫L|H|²df (band_filtered) + Σ 스퍼 2·10^(S/10)·|H(f_spur)|².
    반환 dict(worst_s, worst_combo, per_combo=[(combo, σ_t), ...])."""
    f1, f2 = spec["band_filtered_hz"]
    f0 = osc["f0_hz"]
    per = []
    for c in pcie_combos(spec["filter"]):
        def w(f, c=c):
            return pcie_mag2(f, c)
        var = 2.0 * integrate_weighted(osc["table"], f1, f2, w, ppd) + spur_phase_var(osc["spurs"], f1, f2, w)
        per.append((c, math.sqrt(var) / (TWO_PI * f0)))
    worst = max(per, key=lambda p: p[1])
    return dict(worst_s=worst[1], worst_combo=worst[0], per_combo=per)


# ---------------------------------------------------------------- Part C: 판정 리포트
def unfiltered_jitter(osc, spec):
    """데이터시트 헤드라인 방식: band_unfiltered (12k–20M) 랜덤 + 그 구간 스퍼, 필터 없음. σ_t [s]."""
    f1, f2 = spec["band_unfiltered_hz"]
    var = 2.0 * band_integral(osc["table"], f1, f2) + spur_phase_var(osc["spurs"], f1, f2)
    return math.sqrt(var) / (TWO_PI * osc["f0_hz"])


def judge(osc, spec):
    """후보 하나 판정. 반환 dict(ref, unf_ps, filt_ps, combo, verdict, line).
    verdict = 'PASS' if filt_ps <= osc_alloc_ps else 'FAIL'.
    line 예: 'FAIL net=PCIE_REFCLK osc=Y3 filt=0.338ps (PCIe형, worst of 16: PLL1 4MHz/0.01dB PLL2 5MHz/0.01dB) > 0.25ps alloc; 12k-20M=0.935ps'
    """
    unf = unfiltered_jitter(osc, spec) * 1e12
    fj = filtered_jitter(osc, spec)
    filt, c = fj["worst_s"] * 1e12, fj["worst_combo"]
    lim = spec["osc_alloc_ps"]
    verdict = "PASS" if filt <= lim else "FAIL"
    line = ("%s net=%s osc=%s filt=%.3fps (PCIe형, worst of %d: PLL1 %gMHz/%gdB PLL2 %gMHz/%gdB) %s %.2fps alloc; "
            "12k-20M=%.3fps" % (verdict, spec["net"], osc["ref"], filt, len(fj["per_combo"]),
                                c["bw1"] / 1e6, c["pk1"], c["bw2"] / 1e6, c["pk2"],
                                "<=" if verdict == "PASS" else ">", lim, unf))
    return dict(ref=osc["ref"], unf_ps=unf, filt_ps=filt, combo=c, verdict=verdict, line=line)


def decide(results):
    """PASS 중 필터 지터가 가장 작은 후보. 헤드라인(12k–20M) 순위와 다르면 경고를 붙인다."""
    ok = [r for r in results if r["verdict"] == "PASS"]
    if not ok:
        return "DECISION: none - all candidates FAIL"
    best = min(ok, key=lambda r: r["filt_ps"])
    head = min(results, key=lambda r: r["unf_ps"])
    msg = "DECISION: %s (filt %.3fps)" % (best["ref"], best["filt_ps"])
    if head["ref"] != best["ref"]:
        msg += "; WARNING 12k-20M headline would pick %s (%.3fps) - wrong metric for PCIe" % (head["ref"], head["unf_ps"])
    return msg


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
    except Exception as e:  # noqa: BLE001
        ok, detail = False, "%s: %s" % (type(e).__name__, e)
    if ok:
        g_pass += 1
        print("PASS  %s%s" % (name, ("   [" + detail + "]") if detail else ""))
    else:
        print("FAIL  %s: %s" % (name, detail or "returned False"))


def rel(x, ref):
    return abs(x / ref - 1.0)


def safe(fn, default=None):
    try:
        return fn()
    except Exception as e:  # noqa: BLE001
        print("      (setup failed: %s: %s)" % (type(e).__name__, e))
        return default


FLAT = [(1e3, -150.0), (1e8, -150.0)]                                   # 평탄 −150 dBc/Hz
SLOPE20 = [(1e3, -80.0), (1e4, -100.0), (1e5, -120.0), (1e6, -140.0), (1e7, -160.0)]   # S = 1e-2 / f²
SLOPE10 = [(1e3, -100.0), (1e4, -110.0), (1e5, -120.0), (1e6, -130.0)]  # S = 1e-7 / f


def run_tests():
    f0 = 100e6
    # ---- Part A: 정답을 손으로 계산할 수 있는 표
    print("-- Part A: log-log interpolation, closed-form segment integral, sigma_phi -> sigma_t --")
    check("interp_L at a table point == table value",
          lambda: abs(interp_L(SLOPE20, 1e4) - (-100.0)) < 1e-12)
    check("interp_L at log-midpoint sqrt(1e4*1e5) == -110 (straight line in log f)",
          lambda: (abs(interp_L(SLOPE20, math.sqrt(1e9)) + 110.0) < 1e-9, "%.6f" % interp_L(SLOPE20, math.sqrt(1e9))))

    def outside():
        try:
            interp_L(SLOPE20, 5e7)
        except ValueError:
            return True
        return False, "no ValueError for 50MHz on a table ending at 10MHz"
    check("interp_L outside table -> ValueError (no extrapolation)", outside)
    truth_flat = 1e-15 * (20e6 - 12e3)
    check("flat -150dBc/Hz, 12k-20M: integral == 1e-15*(20e6-12e3)",
          lambda: (rel(band_integral(FLAT, 12e3, 20e6), truth_flat) < 1e-9,
                   "%.6e vs %.6e" % (band_integral(FLAT, 12e3, 20e6), truth_flat)))
    truth20 = 1e-2 * (1 / 12e3 - 1 / 10e6)
    check("-20dB/dec (S=1e-2/f^2), 12k-10M: integral == 1e-2*(1/12k - 1/10M)",
          lambda: (rel(band_integral(SLOPE20, 12e3, 10e6), truth20) < 1e-9,
                   "%.6e vs %.6e" % (band_integral(SLOPE20, 12e3, 10e6), truth20)))
    truth10 = 1e-7 * math.log(1e6 / 12e3)
    check("-10dB/dec (S=1e-7/f, b=-1 branch), 12k-1M: integral == 1e-7*ln(1M/12k)",
          lambda: (rel(band_integral(SLOPE10, 12e3, 1e6), truth10) < 1e-9,
                   "%.6e vs %.6e" % (band_integral(SLOPE10, 12e3, 1e6), truth10)))
    phi_t = math.sqrt(2 * truth_flat)
    check("rms_jitter flat -150, 12k-20M @100MHz: sigma_phi=sqrt(2*int), sigma_t=318.2fs",
          lambda: (rel(rms_jitter(FLAT, f0, 12e3, 20e6)[0], phi_t) < 1e-9
                   and rel(rms_jitter(FLAT, f0, 12e3, 20e6)[1], phi_t / (TWO_PI * f0)) < 1e-9,
                   "sigma_phi=%.4e rad sigma_t=%.2f fs" % (rms_jitter(FLAT, f0, 12e3, 20e6)[0],
                                                           rms_jitter(FLAT, f0, 12e3, 20e6)[1] * 1e15)))
    check("same L(f) table at 2*f0: sigma_phi same, sigma_t halves",
          lambda: (rel(rms_jitter(SLOPE20, 2 * f0, 12e3, 10e6)[1] * 2, rms_jitter(SLOPE20, f0, 12e3, 10e6)[1]) < 1e-12,
                   "%.2ffs @100M vs %.2ffs @200M" % (rms_jitter(SLOPE20, f0, 12e3, 10e6)[1] * 1e15,
                                                    rms_jitter(SLOPE20, 2 * f0, 12e3, 10e6)[1] * 1e15)))

    # ---- Part B: 순진한 적분, 스퍼, 가중 적분, PCIe 형 필터
    print("-- Part B: naive integration error, spurs, weighted integral, PCIe-style filter --")
    errs = safe(lambda: {m: integrate_naive(SLOPE20, 12e3, 10e6, m) / truth20 - 1 for m in ("trapz", "db_linf", "rect")}, {})
    safe(lambda: print("      -20dB/dec truth=%.4e | trapz %+.1f%% | db_linf %+.1f%% | rect %+.1f%% (integral error)"
                       % (truth20, errs["trapz"] * 100, errs["db_linf"] * 100, errs["rect"] * 100)))
    check("naive trapezoid (linear S between table points) overestimates > +100%",
          lambda: (errs["trapz"] > 1.0, "%+.1f%%" % (errs["trapz"] * 100)))
    check("naive dB-linear-in-f overestimates > +50%",
          lambda: (errs["db_linf"] > 0.5, "%+.1f%%" % (errs["db_linf"] * 100)))
    check("naive rect (L x bandwidth, left point) overestimates > +300%",
          lambda: (errs["rect"] > 3.0, "%+.1f%%" % (errs["rect"] * 100)))
    sp_truth = math.sqrt(2) * 1e-3 / (TWO_PI * f0)
    check("spur -60dBc @1MHz in band: sigma_t = sqrt(2)*10^(-60/20)/(2pi f0) = 2.2508ps",
          lambda: (rel(math.sqrt(spur_phase_var([(1e6, -60.0)], 12e3, 20e6)) / (TWO_PI * f0), sp_truth) < 1e-12,
                   "%.4f ps" % (math.sqrt(spur_phase_var([(1e6, -60.0)], 12e3, 20e6)) / (TWO_PI * f0) * 1e12)))
    check("spur outside band (30MHz vs 12k-20M) contributes 0",
          lambda: spur_phase_var([(30e6, -60.0)], 12e3, 20e6) == 0.0)
    y2t = [(100.0, -82.0), (1e3, -97.0), (1e4, -108.0), (1e5, -121.0), (1e6, -145.0), (1e7, -158.0), (5e7, -160.0)]
    check("integrate_weighted(w=1) == closed form within 1e-6 (Simpson in ln f, split at table points)",
          lambda: (rel(integrate_weighted(y2t, 12e3, 20e6, lambda f: 1.0), band_integral(y2t, 12e3, 20e6)) < 1e-6,
                   "rel err %.2e" % rel(integrate_weighted(y2t, 12e3, 20e6, lambda f: 1.0), band_integral(y2t, 12e3, 20e6))))
    fc = 10e6
    hpf_truth = 1e-15 * ((50e6 - 1e3) - fc * (math.atan(50e6 / fc) - math.atan(1e3 / fc)))
    check("flat -150 x 1st-order HPF |f^2/(f^2+fc^2)| == analytic (f2-f1) - fc*(atan(f2/fc)-atan(f1/fc))",
          lambda: (rel(integrate_weighted(FLAT, 1e3, 50e6, lambda f: f * f / (f * f + fc * fc)), hpf_truth) < 1e-6,
                   "rel err %.2e" % rel(integrate_weighted(FLAT, 1e3, 50e6, lambda f: f * f / (f * f + fc * fc)), hpf_truth)))
    spec = load_json("refclk_spec.json")
    combos = pcie_combos(spec["filter"])

    def pll_given():
        c = combos[5]
        g3 = abs(pll_H(complex(0, TWO_PI * c["bw1"]), c["wn1"], c["z1"])) ** 2
        return (abs(g3 - 0.5) < 1e-9 and abs(pll_peaking_db(c["z1"]) - c["pk1"]) < 1e-9,
                "|H(BW)|^2=%.6f peak=%.4fdB (target %.2f) zeta=%.4f" % (g3, pll_peaking_db(c["z1"]), c["pk1"], c["z1"]))
    check("given PLL helpers: |H(j2pi BW)|^2 = 0.5 and peaking(zeta) == spec", pll_given)

    def low_f():
        c = combos[0]
        f = 1e3
        approx = (TWO_PI * f * c["T"] * f / c["fcdr"]) ** 2          # |H1e^{-sT}-H2| ≈ ωT, |H3| ≈ f/fcdr
        return rel(pcie_mag2(f, c), approx) < 0.05, "|H|^2(1kHz)=%.3e approx (wT*f/fcdr)^2=%.3e" % (pcie_mag2(f, c), approx)
    check("pcie_mag2 low-f asymptote ~ (2pi f T * f/fcdr)^2 within 5%", low_f)

    def same_pll():
        c = dict(combos[0])
        c.update(wn2=c["wn1"], z2=c["z1"], T=0.0)
        return max(pcie_mag2(f, c) for f in (1e4, 1e6, 3e6, 1e7)) < 1e-20
    check("pcie_mag2 == 0 when PLL1 == PLL2 and T = 0 (common clock jitter cancels)", same_pll)

    def lower_bound_insensitive():
        c = combos[0]
        a = integrate_weighted(y2t, 1e3, 50e6, lambda f: pcie_mag2(f, c))
        b = integrate_weighted(y2t, 12e3, 50e6, lambda f: pcie_mag2(f, c))
        return rel(a, b) < 1e-3, "1k vs 12k lower bound: rel diff %.1e (unfiltered: %.0f%%)" % (
            rel(a, b), (band_integral(y2t, 1e3, 20e6) / band_integral(y2t, 12e3, 20e6) - 1) * 100)
    check("filtered integral: lower bound 1kHz vs 12kHz changes < 0.1% (filter kills close-in)", lower_bound_insensitive)

    # ---- Part C: SSDB-4 PCIE_REFCLK 후보 판정
    print("-- Part C: SSDB-4 rev A PCIE_REFCLK, Y2 vs Y3 --")
    oscs = [load_json("osc_Y2.json"), load_json("osc_Y3.json")]
    check("spec + 2 oscillator tables loaded (100MHz, 7 points each)",
          lambda: all(o["f0_hz"] == 100e6 and len(o["table"]) == 7 for o in oscs) and spec["osc_alloc_ps"] == 0.25)
    res = [r for r in (safe(lambda o=o: judge(o, spec)) for o in oscs) if r]
    for r in res:
        print("      " + r["line"])
    dec = safe(lambda: decide(res), "")
    print("      " + str(dec))
    by = {r["ref"]: r for r in res}
    check("Y2 -> PASS (filtered < 0.25ps alloc)", lambda: by["Y2"]["verdict"] == "PASS")
    check("Y3 -> FAIL (filtered > 0.25ps alloc)", lambda: by["Y3"]["verdict"] == "FAIL")
    check("headline 12k-20M ranks them the other way (Y2 > Y3)",
          lambda: (by["Y2"]["unf_ps"] > by["Y3"]["unf_ps"], "Y2 %.3f ps, Y3 %.3f ps" % (by["Y2"]["unf_ps"], by["Y3"]["unf_ps"])))
    check("filter shrinks Y2 by > 10x but Y3 by < 4x (where the noise sits matters)",
          lambda: (by["Y2"]["unf_ps"] / by["Y2"]["filt_ps"] > 10 and by["Y3"]["unf_ps"] / by["Y3"]["filt_ps"] < 4,
                   "Y2 x%.1f, Y3 x%.1f" % (by["Y2"]["unf_ps"] / by["Y2"]["filt_ps"], by["Y3"]["unf_ps"] / by["Y3"]["filt_ps"])))
    check("decision picks Y2 and warns the headline metric would pick Y3",
          lambda: str(dec).startswith("DECISION: Y2") and "would pick Y3" in str(dec))

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


def main(argv):
    if len(argv) == 2 and argv[1] == "--report":
        spec = load_json("refclk_spec.json")
        res = [judge(load_json(n), spec) for n in ("osc_Y2.json", "osc_Y3.json")]
        for r in res:
            print(r["line"])
        print(decide(res))
        return 0
    return 0 if run_tests() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
