#!/usr/bin/env python3
"""HWLAB-M03 PDN 타겟 임피던스와 디커플링 커패시터 선정

전원 분배망(PDN)의 임피던스 |Z(f)| 를 주파수 영역에서 직접 계산해 디커플링 구성을 숫자로 고른다.
  Part A  Ztarget = V·ripple%/ΔI, 커패시터 Z = ESR + j(ωL − 1/ωC), VRM ∥ 커패시터 병렬망 |Z(f)|,
          10 kHz–1 GHz 로그 그리드, 국소 최대(반공진 피크) 찾기
  Part B  반공진(anti-resonance) 해석식 vs 수치 피크, ESR 이 낮을수록 피크가 높아지는 이유,
          거친 그리드·'SRF 지점만 보기' 가 피크를 놓쳐 PASS 라고 거짓말하는 것
  Part C  VALBRD-A rev C 의 VDD_CORE_0V9: 1/10/100 µF 개수를 탐색해 최소 BOM 을 고르고,
          조립된 두 보드의 VNA 2-port shunt-thru 측정(.s2p)에서 Z 를 복원해 '빠진 커패시터' 를 찾는다

  python3 -I solution.py        # "N/M passed", 실패 있으면 exit 1
  python3 -I solution.py --profile   # rev C BOM 의 |Z(f)| 표 (10 kHz–1 GHz, decade 당 5 점)

Python 3.9+, 표준 라이브러리만 (math, json, itertools, os, sys).
"""
import itertools
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TWO_PI = 2.0 * math.pi
GR = (math.sqrt(5.0) - 1.0) / 2.0          # 황금비 0.618…


# ---------------------------------------------------------------- 주어진 도구
def load_spec(path=None):
    with open(path or os.path.join(HERE, "pdn_spec.json"), encoding="utf-8") as f:
        return json.load(f)


def part_esl(p):
    """실장 ESL = 부품 자체 ESL + 패드·비아·평면까지의 실장(mounting) 인덕턴스."""
    return p["esl_part_h"] + p["l_mount_h"]


def log_grid(f1, f2, ppd):
    """f1..f2 로그 등간격, decade 당 ppd 점 (양 끝 포함)."""
    n = int(round(math.log10(f2 / f1) * ppd))
    return [f1 * 10.0 ** (k * math.log10(f2 / f1) / n) for k in range(n + 1)]


def vrm_z(f, vrm):
    """폐루프 VRM 출력 임피던스 1차 근사: R_flat + jωL_eq."""
    return complex(vrm["r_ohm"], TWO_PI * f * vrm["l_h"])


def load_s2p(path):
    """Touchstone v1 2-port (.s2p) → (freqs[Hz], S21[complex]). 옵션 줄 '# <unit> S <DB|MA|RI> R <z0>' 지원."""
    mult = {"HZ": 1.0, "KHZ": 1e3, "MHZ": 1e6, "GHZ": 1e9}
    unit, fmt, z0 = 1e9, "MA", 50.0                     # Touchstone 기본값
    freqs, s21 = [], []
    with open(path, encoding="ascii") as f:
        for line in f:
            s = line.split("!")[0].strip()
            if not s:
                continue
            if s.startswith("#"):
                tok = s[1:].upper().split()
                for i, x in enumerate(tok):
                    if x in mult:
                        unit = mult[x]
                    elif x in ("DB", "MA", "RI"):
                        fmt = x
                    elif x == "R":
                        z0 = float(tok[i + 1])
                continue
            v = [float(x) for x in s.split()]
            a, b = v[3], v[4]                            # f, S11(2), S21(2), S12(2), S22(2)
            if fmt == "RI":
                s = complex(a, b)
            else:
                mag = 10.0 ** (a / 20.0) if fmt == "DB" else a
                s = mag * complex(math.cos(math.radians(b)), math.sin(math.radians(b)))
            freqs.append(v[0] * unit)
            s21.append(s)
    return freqs, s21, z0


def bom_str(bom):
    return " ".join("%s×%d" % (k.split("_")[0][1:], n) for k, n in bom.items())


def bom_cost(bom, parts):
    return round(sum(n * parts[k]["cost_usd"] for k, n in bom.items()), 4)


# ---------------------------------------------------------------- Part A: Ztarget, 커패시터 Z, 병렬망
def z_target(v_nom, ripple_pct, delta_i):
    """Ztarget = V · (허용 리플 %/100) / ΔI  [Ω]."""
    # TODO: V × (ripple_pct/100) / ΔI
    return None


def cap_z(f, c, esr, esl):
    """직렬 RLC 커패시터 모델: Z = ESR + j(ωL − 1/(ωC))."""
    # TODO: ω = 2πf, 실수부 ESR, 허수부 ωL − 1/(ωC)  → complex(...)
    return None


def srf(c, esl):
    """자기 공진 주파수 SRF = 1/(2π√(LC)). 여기서 |Z| = ESR (최소)."""
    # TODO: 1/(2π√(L·C))
    return None


def pdn_z(f, bom, parts, vrm=None):
    """VRM(있으면) ∥ (부품 k 를 n_k 개) 병렬망의 Z(f). 병렬 = 어드미턴스 합: Y = 1/Z_vrm + Σ n_k / Z_k."""
    # TODO: y = 1/vrm_z(f, vrm) (vrm 이 있으면) + Σ n / cap_z(f, C, ESR, part_esl(p))  → 1/y
    #       (n == 0 인 부품은 건너뛰고, y == 0 이면 무한대 complex(inf, 0))
    return None


def find_peaks(zmag):
    """국소 최대 인덱스: zmag[i] > zmag[i-1] 그리고 zmag[i] >= zmag[i+1] (양 끝 제외)."""
    # TODO: 1..len-2 에서 왼쪽보다 '크고' 오른쪽보다 '크거나 같은' 점 (평탄한 꼭대기는 한 번만)
    return []


# ---------------------------------------------------------------- Part B: 반공진, 피크 정밀화, 대역 최대
def anti_resonance(c1, l1, c2, l2):
    """두 직렬 LC 가지의 병렬 공진(무손실): Z1 + Z2 = 0 → ω² = (1/C1 + 1/C2)/(L1 + L2)."""
    # TODO: Cs = C1·C2/(C1+C2), f = 1/(2π√((L1+L2)·Cs))
    return None


def refine_peak(zfun, fa, fb, iters=50):
    """[fa, fb] 안에서 |zfun(f)| 의 최대를 log f 축 황금분할 탐색으로. 반환 (f, |Z|)."""
    # TODO: x = log10 f 축에서 황금분할 탐색 (GR = 0.618…). a, b = log10(fa), log10(fb)
    #       c = b − GR(b−a), d = a + GR(b−a); g(c) > g(d) 면 [a, d] 로, 아니면 [c, b] 로 줄인다 (iters 번)
    #       반환 (10**x, |Z|)  — x = (a+b)/2
    return None


def max_z_in_band(zfun, f1, f2, ppd, refine=True):
    """대역 [f1, f2] 의 최악 |Z| 와 그 주파수.
    그리드 값 + (refine 이면) 국소 최대마다 이웃 두 점 사이를 정밀화 + 양 끝이 오르막이면 끝 구간도 정밀화."""
    # TODO ① fs = log_grid(f1, f2, ppd), zm = |zfun(f)|, 그리드 최대 → best
    # TODO ② refine 이면: find_peaks(zm) 의 각 i 에 대해 (fs[i-1], fs[i+1]) 구간을 refine_peak,
    #         양 끝이 오르막(zm[0] >= zm[1], zm[-1] >= zm[-2])이면 끝 구간도 정밀화 → 더 크면 best 갱신
    return None


def srf_check(bom, parts, vrm, f1, f2, ztarget):
    """흔한 '순진한' 검사 (비교용): 실장된 각 부품의 SRF + decade 점 + 대역 끝에서만 |Z| ≤ Ztarget 인지 본다."""
    pts = [srf(parts[k]["c_f"], part_esl(parts[k])) for k, n in bom.items() if n]
    f = f1
    while f <= f2 * (1 + 1e-9):
        pts.append(f)
        f *= 10.0
    pts.append(f2)
    worst = max(abs(pdn_z(x, bom, parts, vrm)) for x in pts)
    return worst <= ztarget, worst


# ---------------------------------------------------------------- Part C: 최소 BOM 탐색, VNA shunt-thru
def min_bom(spec, objective=None):
    """부품 개수 조합을 전수 탐색해 대역 내 max|Z| ≤ Ztarget 인 '최소 BOM' 을 고른다.
    1) decade 당 ppd_grid 점 그리드로 빠르게 거른다 (어드미턴스를 미리 계산해 Y = Y_vrm + Σ n_k·Y_k)
    2) 통과 후보를 목적함수 순(cost: (비용, 개수, max|Z|), count: (개수, 비용, max|Z|))으로 정렬
    3) 앞에서부터 max_z_in_band(refine=True) 로 재검증 — 첫 통과가 답. 그리드만 통과한 후보는 rejected 로.
    반환 dict(bom, cost, count, zmax, fmax, ztarget, n_grid_pass, rejected=[(bom, zmax, fmax)])."""
    # TODO ① zt = z_target(...), 그리드 fs = log_grid(f1, f2, ppd_grid), 부품 이름은 C 큰 순서
    # TODO ② VRM·부품 어드미턴스를 그리드에서 미리 계산 → 개수 조합(itertools.product)마다 Y = Y_vrm + Σ n·Y_k,
    #         그리드 max|1/Y| ≤ zt 인 조합만 후보 (넘는 순간 break 하면 빠르다)
    # TODO ③ 후보 정렬: cost → (bom_cost, 개수, zmax), count → (개수, bom_cost, zmax)
    # TODO ④ 앞에서부터 max_z_in_band(..., refine=True) 재검증: 통과하면 dict(...) 반환, 아니면 rejected 에 추가
    return None


def s21_to_z(s21, z0=50.0):
    """2-port shunt-thru: S21 = 2Z/(2Z + Z0)  →  Z = (Z0/2) · S21/(1 − S21)."""
    # TODO: S21 = 2Z/(2Z + Z0) 를 Z 에 대해 푼다
    return None


def fit_population(freqs, zmeas, spec, f_hi=1e8):
    """측정 |Z| 와 모델 |Z| 의 log10 차이 RMS 가 최소인 부품 개수 조합 (f ≤ f_hi 만 사용).
    반환 (bom, rms[decade], 2등 rms)."""
    # TODO ① f ≤ f_hi 인 측정 점만, log10|Zmeas| 와 VRM·부품 어드미턴스 미리 계산
    # TODO ② 개수 조합마다 rms = √(mean((log10|Zmeas| − log10|Zmodel|)²))   (|Zmodel| = 1/|Y|)
    # TODO ③ rms 오름차순 정렬 → (가장 좋은 bom dict, 1등 rms, 2등 rms)
    return None


def board_report(sn, path, spec):
    """보드 한 장의 .s2p → Z(f) → 대역 내 최악 |Z| 판정 + 실장 개수 추정 → 한 줄 리포트."""
    rail = spec["rail"]
    zt = z_target(rail["v_nom"], rail["ripple_pct"], rail["delta_i_a"])
    f1, f2 = rail["band_hz"]
    freqs, s21, z0 = load_s2p(path)
    z = [s21_to_z(s, z0) for s in s21]
    inband = [i for i, f in enumerate(freqs) if f1 <= f <= f2]
    k = max(inband, key=lambda i: abs(z[i]))
    fit, rms, rms2 = fit_population(freqs, z, spec)
    want = spec["bom_rev_c"]
    diff = ["%s %d missing" % (p, want[p] - fit[p]) for p in want if fit[p] < want[p]]
    diff += ["%s %d extra" % (p, fit[p] - want[p]) for p in want if fit[p] > want[p]]
    verdict = "PASS" if abs(z[k]) <= zt else "FAIL"
    exc, cur = [], None                                  # Ztarget 을 넘는 연속 구간들 (측정 점 기준)
    for i in inband:
        if abs(z[i]) > zt:
            cur = [freqs[i], freqs[i], abs(z[i])] if cur is None else [cur[0], freqs[i], max(cur[2], abs(z[i]))]
        elif cur is not None:
            exc.append(cur)
            cur = None
    if cur is not None:
        exc.append(cur)
    line = ("%s %s %s %s: |Z|max=%.1fmΩ @ %.2fMHz %s Ztarget %.1fmΩ | fit %s (rms %.3f dec, next %.3f) vs BOM rev C %s -> %s"
            % (verdict, rail["board"], sn, rail["name"], abs(z[k]) * 1e3, freqs[k] / 1e6,
               "<=" if verdict == "PASS" else ">", zt * 1e3, bom_str(fit), rms, rms2, bom_str(want),
               ", ".join(diff) or "population OK"))
    if exc:
        line += "\n    over Ztarget: " + "; ".join("%.3g–%.3g MHz (peak %.1fmΩ)" % (a / 1e6, b / 1e6, p * 1e3)
                                               for a, b, p in exc)
    return dict(verdict=verdict, line=line, fit=fit, zmax=abs(z[k]), fmax=freqs[k], freqs=freqs, z=z, exc=exc)


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


def near(x, ref, tol):
    return x is not None and abs(x - ref) <= tol


def rel(x, ref):
    return abs(x / ref - 1.0)


def safe(fn, default=None):
    """테스트 준비 계산을 감싼다: starter 의 미구현 함수가 예외를 내도 하네스는 계속 돈다."""
    try:
        return fn()
    except Exception as e:  # noqa: BLE001
        print("      (setup failed: %s: %s)" % (type(e).__name__, e))
        return default


def run_tests():
    spec = load_spec()
    parts, vrm, rail = spec["parts"], spec["vrm"], spec["rail"]
    P100, P10, P1 = parts["C100u_1206"], parts["C10u_0603"], parts["C1u_0402"]

    # ---- Part A
    print("-- Part A: Ztarget / capacitor Z / parallel network / peaks --")
    zt = safe(lambda: z_target(rail["v_nom"], rail["ripple_pct"], rail["delta_i_a"]), 0.0)
    check("Ztarget(0.9V, 3%, 2A) = 13.5 mOhm", lambda: (near(zt, 0.0135, 1e-12), "%.4f mOhm" % (zt * 1e3)))
    check("Ztarget(1.8V, 5%, 0.5A) = 180 mOhm", lambda: near(z_target(1.8, 5.0, 0.5), 0.18, 1e-12))
    L100 = part_esl(P100)
    f0 = safe(lambda: srf(P100["c_f"], L100), 1.0)
    check("SRF(100uF, 1.4nH) = 425.4 kHz", lambda: (near(f0, 425.4e3, 0.1e3), "%.2f kHz" % (f0 / 1e3)))
    check("|Z(SRF)| == ESR (reactances cancel)",
          lambda: (near(abs(cap_z(f0, P100["c_f"], P100["esr_ohm"], L100)), P100["esr_ohm"], 1e-9),
                   "%.4f mOhm" % (abs(cap_z(f0, P100["c_f"], P100["esr_ohm"], L100)) * 1e3)))
    check("|Z(SRF/100)| ~ 1/(wC) within 0.1%, |Z(100*SRF)| ~ wL within 0.1%",
          lambda: rel(abs(cap_z(f0 / 100, P100["c_f"], P100["esr_ohm"], L100)), 1 / (TWO_PI * f0 / 100 * P100["c_f"])) < 1e-3
          and rel(abs(cap_z(f0 * 100, P100["c_f"], P100["esr_ohm"], L100)), TWO_PI * f0 * 100 * L100) < 1e-3)
    check("n identical caps in parallel -> Z/n exactly (no VRM)",
          lambda: abs(pdn_z(3e6, {"C10u_0603": 4}, parts) * 4 - cap_z(3e6, P10["c_f"], P10["esr_ohm"], part_esl(P10))) < 1e-15)
    check("VRM only: Z = R + jwL",
          lambda: abs(pdn_z(1e5, {}, parts, vrm) - complex(vrm["r_ohm"], TWO_PI * 1e5 * vrm["l_h"])) < 1e-15)
    fs = log_grid(rail["scan_hz"][0], rail["scan_hz"][1], 40)
    check("scan grid 10 kHz..1 GHz @ 40/dec = 201 points, exact ends",
          lambda: len(fs) == 201 and fs[0] == 1e4 and near(fs[-1], 1e9, 1e-3))
    bomC = spec["bom_rev_c"]
    zC = safe(lambda: [abs(pdn_z(f, bomC, parts, vrm)) for f in fs], [])
    pk = safe(lambda: find_peaks(zC), [])
    safe(lambda: print("      rev C peaks on 40/dec grid: " +
                       ", ".join("%.3g Hz %.2f mOhm" % (fs[i], zC[i] * 1e3) for i in pk)))
    check("rev C network (VRM + 2/5/3) has 3 anti-resonance peaks in 10k..1G",
          lambda: (len(pk) == 3, "found %d" % len(pk)))
    check("find_peaks edge cases: plateau counted once, ends ignored",
          lambda: find_peaks([3, 1, 2, 2, 1, 5]) == [2] and find_peaks([1, 2, 3]) == [])

    # ---- Part B
    print("-- Part B: anti-resonance, ESR, grid resolution, naive SRF check --")
    pair = {"C100u_1206": 1, "C1u_0402": 1}
    L1u = part_esl(P1)
    far = safe(lambda: anti_resonance(P100["c_f"], L100, P1["c_f"], L1u))
    check("anti_resonance(100uF/1.4nH, 1uF/0.8nH) = 3.411 MHz", lambda: (near(far, 3.411e6, 2e3), "%.4f MHz" % (far / 1e6)))
    far = far or 3.41e6                         # (starter: 미구현이어도 아래 테스트가 돌도록 대략값)
    zpair = lambda f: pdn_z(f, pair, parts)                                    # noqa: E731
    fpk, zpk = safe(lambda: refine_peak(zpair, far / 2, far * 2), None) or (1.0, 1e-9)
    x1 = TWO_PI * far * L100 - 1 / (TWO_PI * far * P100["c_f"])
    approx = x1 * x1 / (P100["esr_ohm"] + P1["esr_ohm"])
    print("      100uF||1uF: f_ar(lossless)=%.4f MHz  refined peak %.4f MHz %.2f mOhm  X^2/(R1+R2)=%.2f mOhm"
          % (far / 1e6, fpk / 1e6, zpk * 1e3, approx * 1e3))
    check("refined |Z| peak frequency within 3% of lossless f_ar (Q~2 shifts it a little)",
          lambda: (rel(fpk, far) < 0.03, "%+.2f%%" % ((fpk / far - 1) * 100)))
    check("peak height within 10% of X^2/(R1+R2)", lambda: (rel(zpk, approx) < 0.10, "%+.1f%%" % ((zpk / approx - 1) * 100)))
    lowesr = {k: dict(v) for k, v in parts.items()}
    for v in lowesr.values():
        v["esr_ohm"] *= 0.25
    zlow = lambda f: pdn_z(f, pair, lowesr)                                    # noqa: E731
    fpl, zpl = safe(lambda: refine_peak(zlow, far / 2, far * 2), None) or (1.0, 1e-9)
    check("ESR x0.25 -> anti-resonance peak x>3 (lower ESR = higher Q = taller peak)",
          lambda: (zpl / zpk > 3.0, "%.1f -> %.1f mOhm (x%.2f)" % (zpk * 1e3, zpl * 1e3, zpl / zpk)))
    dense = safe(lambda: max(abs(zlow(f)) for f in log_grid(far / 1.2, far * 1.2, 50000)), 1.0)
    check("refine_peak matches dense brute force (50k/dec) within 0.01%",
          lambda: (rel(zpl, dense) < 1e-4, "%.4f vs %.4f mOhm" % (zpl * 1e3, dense * 1e3)))
    print("      grid density vs worst |Z| found (low-ESR pair, 10k..20M):")
    rows = []
    for ppd in (5, 10, 20, 40, 100):
        g = safe(lambda: max_z_in_band(zlow, 1e4, 2e7, ppd, refine=False), None) or (0.0, 0.0)
        rows.append((ppd, g[1]))
        print("        %4d/dec  grid max %7.2f mOhm  (%5.1f%% of true peak)" % (ppd, g[1] * 1e3, g[1] / dense * 100))
    gr = safe(lambda: max_z_in_band(zlow, 1e4, 2e7, 10, refine=True), None) or (0.0, 0.0)
    print("          10/dec + refine  %7.2f mOhm @ %.4f MHz" % (gr[1] * 1e3, gr[0] / 1e6))
    check("10/dec grid alone under-reads the sharp peak by > 20%",
          lambda: (0 < rows[1][1] < 0.8 * dense, "%.1f%% of true" % (rows[1][1] / dense * 100)))
    check("10/dec grid + peak refinement recovers the true peak within 0.01%",
          lambda: (rel(gr[1], dense) < 1e-4, "%.4f mOhm" % (gr[1] * 1e3)))
    naive_bom = {"C100u_1206": 2, "C10u_0603": 0, "C1u_0402": 6}
    ok_naive, worst_naive = safe(lambda: srf_check(naive_bom, parts, vrm, 1e4, 2e7, zt), None) or (False, 0.0)
    tf, tz = safe(lambda: max_z_in_band(lambda f: pdn_z(f, naive_bom, parts, vrm), 1e4, 2e7, 40), None) or (0.0, 0.0)
    print("      naive BOM 100u x2 + 1u x6: SRF/decade check worst %.2f mOhm -> %s | true max %.2f mOhm @ %.3f MHz"
          % (worst_naive * 1e3, "PASS" if ok_naive else "FAIL", tz * 1e3, tf / 1e6))
    check("'check only at SRFs + decades' says PASS ...", lambda: ok_naive)
    check("... but the true in-band max (anti-resonance between them) exceeds Ztarget",
          lambda: (tz > zt, "%.2f > %.2f mOhm @ %.3f MHz" % (tz * 1e3, zt * 1e3, tf / 1e6)))

    # ---- Part C
    print("-- Part C: minimum BOM search + VNA shunt-thru check of two built boards --")
    res = safe(lambda: min_bom(spec, "cost"))
    resn = safe(lambda: min_bom(spec, "count"))
    for tag, r in (("min-cost ", res), ("min-count", resn)):
        if r:
            print("      %s: %s  $%.2f  %d parts  max|Z|=%.2f mOhm @ %.3f MHz  (grid-pass %d, rejected after refine %d)"
                  % (tag, bom_str(r["bom"]), r["cost"], r["count"], r["zmax"] * 1e3, r["fmax"] / 1e6,
                     r["n_grid_pass"], len(r["rejected"])))
            for b, z, f in r["rejected"][:3]:
                print("        rejected %s: grid OK but refined %.2f mOhm @ %.3f MHz" % (bom_str(b), z * 1e3, f / 1e6))
    check("min-cost BOM == bom_rev_c in pdn_spec.json", lambda: (res["bom"] == bomC, bom_str(res["bom"])))
    check("min-cost BOM really meets Ztarget over 10k..20M (dense 2000/dec)",
          lambda: (max(abs(pdn_z(f, res["bom"], parts, vrm)) for f in log_grid(1e4, 2e7, 2000)) <= zt))
    check("at least one grid-passing candidate was rejected by peak refinement",
          lambda: (len(res["rejected"]) >= 1, "%d rejected" % len(res["rejected"])))
    check("min-count BOM uses fewer parts but costs more than min-cost",
          lambda: (resn["count"] < res["count"] and resn["cost"] > res["cost"],
                   "%d parts $%.2f vs %d parts $%.2f" % (resn["count"], resn["cost"], res["count"], res["cost"])))
    check("s21_to_z: Z=10 mOhm -> S21=2Z/(2Z+50) -> back to 10 mOhm",
          lambda: abs(s21_to_z(2 * 0.01 / (2 * 0.01 + 50)) - 0.01) < 1e-12)
    reps = {}
    for sn, fname in spec["boards"].items():
        reps[sn] = safe(lambda: board_report(sn, os.path.join(HERE, fname), spec))
        if reps[sn]:
            print(reps[sn]["line"])
    check("SN0106 population fit == BOM rev C and PASS",
          lambda: (reps["SN0106"]["fit"] == bomC and reps["SN0106"]["verdict"] == "PASS", bom_str(reps["SN0106"]["fit"])))
    check("SN0107 fit finds injected truth: 10uF populated 2 of 5 (3 missing)",
          lambda: (reps["SN0107"]["fit"] == {"C100u_1206": 2, "C10u_0603": 2, "C1u_0402": 3},
                   bom_str(reps["SN0107"]["fit"])))
    check("SN0107 FAIL: measured in-band |Z| above Ztarget in 2 separate bands (VRM-bulk peak + HF end)",
          lambda: (reps["SN0107"]["verdict"] == "FAIL" and len(reps["SN0107"]["exc"]) == 2,
                   "%.2f mOhm @ %.2f MHz, %d bands" % (reps["SN0107"]["zmax"] * 1e3, reps["SN0107"]["fmax"] / 1e6,
                                                      len(reps["SN0107"]["exc"]))))

    print("%d/%d passed" % (g_pass, g_total))
    return g_pass == g_total


def profile():
    spec = load_spec()
    bom = spec["bom_rev_c"]
    print("    f [Hz]   |Z| rev C [mOhm]   VRM only   100u only x2   10u only x5   1u only x3")
    for f in log_grid(1e4, 1e9, 5):
        row = [abs(pdn_z(f, bom, spec["parts"], spec["vrm"]))]
        row.append(abs(vrm_z(f, spec["vrm"])))
        for k in ("C100u_1206", "C10u_0603", "C1u_0402"):
            row.append(abs(pdn_z(f, {k: bom[k]}, spec["parts"])))
        print("  %9.3g  " % f + "  ".join("%12.2f" % (x * 1e3) for x in row))


def main(argv):
    if len(argv) == 2 and argv[1] == "--profile":
        profile()
        return 0
    return 0 if run_tests() else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
