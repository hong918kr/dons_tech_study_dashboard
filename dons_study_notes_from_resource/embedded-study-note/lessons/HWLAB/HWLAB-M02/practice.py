#!/usr/bin/env python3
"""HWLAB-M02 추가 연습 — 본문 측정 라이브러리(solution.py)를 확장한다

  연습 1  zeta_from_pm        : PM → ζ 정확 역변환 (스펙 'PM ≥ 45°' 를 ζ 문턱으로)
  연습 2  부하 해제(release)   : 3 → 0.5 A 스텝의 overshoot · ζ 를 같은 코드로
  연습 3  zout_peak           : |Zout(f)| 피크 → Q → ζ ≈ 1/(2Q), 과도응답 ζ 와 교차 검증 + 교차 주파수 fc
  연습 4  classify            : 성장하는 링잉(ζ ≤ 0)·저마진·정상을 구분하는 판정기

  python3 -I practice.py      # "N/M passed", 실패 있으면 exit 1
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)            # -I 는 스크립트 디렉터리를 sys.path 에서 빼므로 직접 넣는다
sys.dont_write_bytecode = True     # 레슨 디렉터리에 __pycache__ 를 남기지 않는다
import gen_capture as gen           # noqa: E402
import solution as S                # noqa: E402  (같은 레슨의 본문 라이브러리)


# ---------------------------------------------------------------- 연습 1
def zeta_from_pm(pm_deg):
    """PM = atan(2ζ/√(√(1+4ζ⁴) − 2ζ²)) 의 역함수: ζ = tan(PM)·√(cos PM) / 2  (0 < PM < 90°)."""
    p = math.radians(pm_deg)
    return math.tan(p) * math.sqrt(math.cos(p)) / 2.0


# ---------------------------------------------------------------- 연습 3
def zout_mag(f, zeta, fn, C):
    """|Zout(j2πf)| of (1/C)·s/(s² + 2ζωn s + ωn²)  [Ω]."""
    s = 2j * math.pi * f
    wn = 2.0 * math.pi * fn
    return abs((s / C) / (s * s + 2.0 * zeta * wn * s + wn * wn))


def zout_peak(zeta, fn, C, f_lo=1e3, f_hi=1e7, n=4000):
    """로그 스윕으로 피크 근처를 찾고, 황금분할로 다듬는다 → (f_pk, |Z|_pk)."""
    best = max((f_lo * (f_hi / f_lo) ** (k / n) for k in range(n + 1)), key=lambda f: zout_mag(f, zeta, fn, C))
    r = (f_hi / f_lo) ** (1.0 / n)
    a, b = math.log(best / r), math.log(best * r)
    g = (math.sqrt(5) - 1) / 2
    for _ in range(80):
        c, d = b - g * (b - a), a + g * (b - a)
        if zout_mag(math.exp(c), zeta, fn, C) > zout_mag(math.exp(d), zeta, fn, C):
            b = d
        else:
            a = c
    fp = math.exp(0.5 * (a + b))
    return fp, zout_mag(fp, zeta, fn, C)


def crossover_hz(zeta, fn):
    """2차 루프 교차 주파수 fc = fn·√(√(1+4ζ⁴) − 2ζ²)."""
    return fn * math.sqrt(math.sqrt(1 + 4 * zeta ** 4) - 2 * zeta ** 2)


# ---------------------------------------------------------------- 연습 4
def classify(m, pm_min=45.0, pm_marginal=30.0):
    """측정 dict → 'UNSTABLE' (ζ ≤ 0: 감쇠 없음/성장) · 'MARGINAL' (PM < 30°) · 'LOW_PM' (< 45°) · 'OK' · 'UNKNOWN'."""
    if m["zeta"] is not None:
        if m["zeta"] <= 0.0:
            return "UNSTABLE"
        if m["pm"] < pm_marginal:
            return "MARGINAL"
        return "LOW_PM" if m["pm"] < pm_min else "OK"
    if m["pm_min"] is not None and m["pm_min"] >= pm_min:
        return "OK"
    return "UNKNOWN"


# ---------------------------------------------------------------- 하네스
g_pass = 0
g_total = 0


def check(name, fn):
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


def main():
    near = S.near
    # 연습 1
    check("ex1 zeta_from_pm round-trips phase_margin (zeta 0.05..0.95)",
          lambda: all(near(zeta_from_pm(S.phase_margin(z)[0]), z, 1e-9) for z in (0.05, 0.2, 0.42, 0.7, 0.95)))
    check("ex1 PM 45deg -> zeta 0.420 (rule 100z would say 0.45); PM 60deg -> 0.612",
          lambda: (near(zeta_from_pm(45), 0.4204, 1e-4) and near(zeta_from_pm(60), 0.6124, 1e-4),
                   "45deg->%.4f 60deg->%.4f" % (zeta_from_pm(45), zeta_from_pm(60))))

    # 연습 2: 부하 해제 3 → 0.5 A
    pR = dict(gen.CAPTURES["U12_before_comp.csv"], i0=3.0, i1=0.5)
    trR = gen.truth(pR, 8e-3)
    t, v, i = S.capture(pR, seed=51)
    mR = S.analyze(t, v, i, fsw=1e6, band=8e-3)
    check("ex2 release step: edges falling, slew 1A/us +-3%",
          lambda: (mR["edges"]["i1"] < mR["edges"]["i0"] and near(mR["edges"]["slew"], 1e6, 0.03e6),
                   "i0=%.2f i1=%.2f slew=%.3fA/us" % (mR["edges"]["i0"], mR["edges"]["i1"],
                                                     mR["edges"]["slew"] * 1e-6)))
    check("ex2 release overshoot (filtered) within 0.5mV of truth, zeta +-0.005",
          lambda: (near(mR["droop"], trR["droop"], 0.5e-3) and near(mR["zeta"], 0.15, 0.005),
                   "overshoot %.2f/%.2fmV zeta=%.4f vmax_raw=%.4fV" % (mR["droop"] * 1e3, trR["droop"] * 1e3,
                                                                      mR["zeta"], mR["vmax_raw"])))

    # 연습 3: Zout 피크 ↔ ζ, 교차 주파수
    p = gen.CAPTURES["U12_before_comp.csv"]
    fp, zp = zout_peak(p["zeta"], p["fn"], p["C"])
    z0 = 1.0 / (2 * math.pi * p["fn"] * p["C"])
    check("ex3 parallel-RLC Zout peaks at fn with |Z| = Z0/(2 zeta)",
          lambda: (near(fp, p["fn"], 1.0) and near(zp, z0 / (2 * p["zeta"]), 1e-6),
                   "f_pk=%.1fHz |Z|pk=%.2fmOhm Z0=%.2fmOhm Q=%.2f" % (fp, zp * 1e3, z0 * 1e3, zp / z0)))
    tB, vB, iB = S.load_csv(os.path.join(HERE, "U12_before_comp.csv"))
    mB = S.analyze(tB, vB, iB, fsw=1e6)
    wd = 2 * math.pi / mB["Td"]
    fn_meas = wd / math.sqrt(1 - mB["zeta"] ** 2) / (2 * math.pi)
    check("ex3 transient-derived Q=1/(2zeta) and fn match the Zout peak (Q +-3%, fn +-2%)",
          lambda: (near(1 / (2 * mB["zeta"]), zp / z0, 0.03 * zp / z0) and near(fn_meas, fp, 0.02 * fp),
                   "Q_transient=%.2f Q_Zout=%.2f fn_meas=%.2fkHz" % (1 / (2 * mB["zeta"]), zp / z0, fn_meas / 1e3)))
    check("ex3 crossover fc from (zeta, fn) and fsw/fc sanity (>= 10)",
          lambda: (lambda fc: (near(fc, 46.9e3, 0.2e3) and 1e6 / fc >= 10.0, "fc=%.2fkHz fsw/fc=%.1f"
                               % (fc / 1e3, 1e6 / fc)))(crossover_hz(mB["zeta"], fn_meas)))

    # 연습 4: 판정기
    pU = dict(gen.CAPTURES["U12_before_comp.csv"], zeta=-0.02)
    tU, vU, iU = S.capture(pU, seed=61)
    mU = S.analyze(tU, vU, iU, fsw=1e6)
    tA, vA, iA = S.load_csv(os.path.join(HERE, "U12_after_comp.csv"))
    mA = S.analyze(tA, vA, iA, fsw=1e6)
    pH = dict(gen.CAPTURES["U12_after_comp.csv"], zeta=0.8)
    tH, vH, iH = S.capture(pH, seed=41)
    mH = S.analyze(tH, vH, iH, fsw=1e6)
    check("ex4 zeta=-0.02 (growing ring): zeta<=0, recovery None -> UNSTABLE",
          lambda: (mU["zeta"] is not None and mU["zeta"] <= 0 and mU["recovery"] is None
                   and classify(mU) == "UNSTABLE", "zeta=%.4f rec=%s lobes=%d" % (mU["zeta"], mU["recovery"], mU["n"])))
    check("ex4 before -> MARGINAL, after -> OK, zeta=0.8 (bound only) -> OK",
          lambda: (classify(mB) == "MARGINAL" and classify(mA) == "OK" and classify(mH) == "OK",
                   "%s / %s / %s (PM_min=%.1f)" % (classify(mB), classify(mA), classify(mH), mH["pm_min"])))

    print("%d/%d passed" % (g_pass, g_total))
    return 0 if g_pass == g_total else 1


if __name__ == "__main__":
    sys.exit(main())
